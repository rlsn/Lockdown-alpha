#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatKeywords.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"

// #198 B base components on Combat's body records (docs/systems/COMBAT_AND_GROWTH.md, "基础构件"): the element hook, the
// B02 ledger, the B17 idle amplification and the empowered-basic packages (S11/S17 at release, B12 at a dash's completion).
namespace LDCombatComponentsDetail
{
    // floor(ValueP2 x Bp / 10000) for nonnegative inputs, without overflow for any P2 attribute.
    int64 Share(int64 ValueP2, int64 Bp)
    {
        if (ValueP2<=0 || Bp<=0) { return 0; }
        return ValueP2/10000*Bp+ValueP2%10000*Bp/10000;
    }

    // Base + AD and AP shares of one effect from a body's current attributes; false outside the checked domain.
    bool PackageP2(const FLDDemoEffectDefinition& Effect, const FLDProjectedAttributes& Final, int64& Out)
    {
        Out=0;
        if (Effect.BaseP2<0 || Effect.ADRatioBp<0 || Effect.APRatioBp<0) { return false; }
        const int64 AD=Share(Final.ADP2,Effect.ADRatioBp), AP=Share(Final.APP2,Effect.APRatioBp);
        if (AD>MAX_int64-Effect.BaseP2 || AP>MAX_int64-Effect.BaseP2-AD) { return false; }
        Out=Effect.BaseP2+AD+AP;
        return true;
    }
}

bool ALDCombatDirector::ApplyComponentElement(FLDCombatHit& Hit, ELDDemoElement Element, const FLDCombatSourceRef& Component,
    FString& Error) const
{
    switch (Element)
    {
    case ELDDemoElement::Fire:
        if (Hit.ComponentBurnStacks>0 && Hit.ComponentBurnSource!=Component)
        {
            Error=FString::Printf(TEXT("Hit %s carries fire of two components (%s, %s)."),*Hit.HitId.ToString(),
                *LDLog::Source(Hit.ComponentBurnSource),*LDLog::Source(Component));
            return false;
        }
        Hit.ComponentBurnStacks+=1;
        Hit.ComponentBurnSource=Component;
        return true;
    case ELDDemoElement::Water:
    case ELDDemoElement::Electric:
    {
        // The component's own application (spec §11.1/§11.4): C09 and M11 read this source, never the parent attack's.
        FLDCombatSourceRef& Slot=Element==ELDDemoElement::Water ? Hit.ComponentWetSource : Hit.ComponentElectricSource;
        if (Slot.Kind!=ELDCombatSourceKind::None && Slot!=Component)
        {
            Error=FString::Printf(TEXT("Hit %s carries %s of two components (%s, %s)."),*Hit.HitId.ToString(),
                Element==ELDDemoElement::Water ? TEXT("water") : TEXT("electric"),*LDLog::Source(Slot),*LDLog::Source(Component));
            return false;
        }
        Slot=Component;
        return true;
    }
    default:
        Error=FString::Printf(TEXT("%s names no element."),*LDLog::Source(Component));
        return false;
    }
}

bool ALDCombatDirector::RecordLedgerEntries(FLDBodyCombatState& Target, const FLDCombatHit& Hit, const FLDBodyCombatState& Source,
    const TArray<FLDPassiveEffectRef>& Ledgers, int64 NowMs, FString& Error)
{
    const FLDProjectedAttributes& Final=Source.AttributeProjection.Final;
    for (const FLDPassiveEffectRef& Ref : Ledgers)
    {
        FLDDemoEffectDefinition Effect=Ref.Effect;
        // #198 F M10: the owner's new batches take its count and window, every entry its multiplier.
        TArray<FLDPassiveEffectRef> Variants;
        if (!WorkshopOn(Source.AttributeProjection,ELDDemoEffectOp::LedgerVariant,Ref.Source,Variants,Error)) { return false; }
        int64 EntryScaleBp=10000;
        for (const FLDPassiveEffectRef& Variant : Variants)
        {
            Effect.Count=Variant.Effect.Count;
            Effect.DurationMs=Variant.Effect.DurationMs;
            EntryScaleBp=Variant.Effect.MultiplierBp;
        }
        // Each entry is valued when it is added: the grown base plus the owner's AD/AP shares now.
        int64 ValueP2=0;
        if (!LDCombatComponentsDetail::PackageP2(Effect,Final,ValueP2))
        { Error=FString::Printf(TEXT("A %s ledger entry is outside the checked numeric domain."),*Ref.Key); return false; }
        ValueP2=LDCombatComponentsDetail::Share(ValueP2,EntryScaleBp);
        FLDCombatLedger* Ledger=Target.Ledgers.FindByPredicate([&Source,&Ref](const FLDCombatLedger& Row)
        { return Row.Owner==Source.Victim.EntityId && Row.Source==Ref.Source; });
        if (!Ledger)
        {
            Ledger=&Target.Ledgers.AddDefaulted_GetRef();
            Ledger->Owner=Source.Victim.EntityId;
            Ledger->Source=Ref.Source;
        }
        int64 CashOutP2=0;
        bool bRefused=false;
        const bool bCashOut=LDCombatKeywords::AddLedgerEntry(*Ledger,ValueP2,NowMs,Effect.Count,Effect.DurationMs,CashOutP2,bRefused);
        if (bRefused)
        {
            Error=FString::Printf(TEXT("The %s ledger refused an entry (count %d, window %lld ms)."),*Ref.Key,Effect.Count,Effect.DurationMs);
            return false;
        }
        if (!bCashOut) { continue; }
        // One magic skill-class package of the whole batch; a settle hit, so it never feeds a ledger or B18 itself. #198 F:
        // M01 spreads it around the target and M02 locks it for its delayed settle.
        if (!CashOutLedger(Source,Hit.Target,Ref.Source,CashOutP2,Effect.DamageType,NowMs,Error)) { return false; }
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_LEDGER_CASHOUT t=%lld owner=%s victim=%s life=%u amount=%s"),NowMs,
            *LDLog::Id(Source.Victim.EntityId),*LDLog::Id(Target.Victim.EntityId),Target.Victim.Life,*LDLog::P2(CashOutP2));
    }
    Target.Ledgers.RemoveAll([](const FLDCombatLedger& Row) { return Row.EntriesP2.IsEmpty(); });
    return true;
}

bool ALDCombatDirector::IdleReadyAmpBp(const FLDBodyCombatState& Body, const FLDCombatTrack& Track, int64 NowMs, int64& OutBp,
    FString& Error) const
{
    OutBp=0;
    Error.Reset();
    if (Track.Kind!=ELDCombatSourceKind::Learned) { return true; }
    TArray<FLDPassiveEffectRef> Amps;
    if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::IdleReadyAmp,Amps,Error)) { return false; }
    for (const FLDPassiveEffectRef& Ref : Amps)
    {
        if (Ref.Effect.DelayMs<0 || Ref.Effect.RatioBp<0)
        { Error=FString::Printf(TEXT("idle_ready_amp of %s needs a nonnegative delay and ratio."),*Ref.Key); return false; }
        // From the active's real ReadyAt: it stood ready and unused for DelayMs.
        if (NowMs>=Ref.Effect.DelayMs && Track.ReadyAtMs<=NowMs-Ref.Effect.DelayMs) { OutBp+=Ref.Effect.RatioBp; }
    }
    return true;
}

bool ALDCombatDirector::AlternateAmpBp(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, int64 NowMs, int64& OutBp,
    FString& Error) const
{
    OutBp=0;
    Error.Reset();
    if (Source.Kind!=ELDCombatSourceKind::Learned || Body.LastActiveRelease.IsNone() || Body.LastActiveRelease==Source.DefinitionId
        || NowMs<Body.LastActiveReleaseAtMs)
    { return true; }
    TArray<FLDPassiveEffectRef> Amps;
    if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::AlternateAmp,Amps,Error)) { return false; }
    for (const FLDPassiveEffectRef& Ref : Amps)
    {
        if (Ref.Effect.DurationMs<=0 || Ref.Effect.RatioBp<0)
        { Error=FString::Printf(TEXT("alternate_amp of %s needs a positive window and a nonnegative ratio."),*Ref.Key); return false; }
        if (NowMs-Body.LastActiveReleaseAtMs<=Ref.Effect.DurationMs) { OutBp+=Ref.Effect.RatioBp; }
    }
    return true;
}

void ALDCombatDirector::NoteActiveRelease(FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, int64 NowMs)
{
    if (Source.Kind!=ELDCombatSourceKind::Learned) { return; }
    Body.LastActiveRelease=Source.DefinitionId;
    Body.LastActiveReleaseAtMs=NowMs;
}

bool ALDCombatDirector::MakeEmpower(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
    const FLDDemoEffectDefinition& Effect, const FGuid& EffectId, int64 NowMs, FLDCombatEmpower& Out, FString& Error) const
{
    Out=FLDCombatEmpower();
    // Every package merges into the one physical primary hit (spec §10.2).
    if (Effect.DamageType!=ELDDemoDamageType::Physical || Effect.DurationMs<=0)
    {
        Error=FString::Printf(TEXT("empower_basic of %s must be a physical package with a positive window."),*LDLog::Source(Source));
        return false;
    }
    Out.EffectId=EffectId;
    Out.Source=Source;
    Out.Charges=FMath::Max(Effect.Count,1);
    if (!LDCombatComponentsDetail::PackageP2(Effect,Body.AttributeProjection.Final,Out.AmountP2))
    {
        Error=FString::Printf(TEXT("empower_basic of %s is outside the checked numeric domain."),*LDLog::Source(Source));
        return false;
    }
    return LDMatchLifecycle::AddDurationMs(NowMs,Effect.DurationMs,Out.ExpiresAtMs,Error);
}

bool ALDCombatDirector::ResolveDashCompletions(int64 NowMs, FString& Error)
{
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        if (!Body.VoluntaryDash.IsSet() || NowMs<Body.VoluntaryDash->EndMs) { continue; }
        const FLDCombatVoluntaryDash Dash=Body.VoluntaryDash.GetValue();
        Body.VoluntaryDash.Reset();
        const ALDCharacter* Actor=Pair.Key.Get();
        // A dash that went nowhere (blocked at once) is no completed dash.
        if (!Actor || Body.bPendingDeath || !LDCombatControl::DashCompleted(Dash,NowMs,FootOf(*Actor))) { continue; }
        TArray<FLDPassiveEffectRef> Held;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::EmpowerBasic,Held,Error)) { return false; }
        bool bArmed=false;
        for (const FLDPassiveEffectRef& Ref : Held)
        {
            if (Ref.Effect.Trigger!=ELDDemoEffectTrigger::DashComplete) { continue; }
            // Snapshotted at the completion; the same source's unused package is replaced, never stacked.
            FLDCombatEmpower Package;
            if (!MakeEmpower(Body,Ref.Source,Ref.Effect,FGuid::NewGuid(),NowMs,Package,Error)) { return false; }
            LDCombatControl::ArmEmpower(Body.Empowers,Package);
            bArmed=true;
        }
        if (bArmed)
        {
            ++Body.StateRevision;
            MarkViewChanged(Body.Victim.EntityId);
        }
        if (!RelocateArea(Body,Dash.Source,FootOf(*Actor),NowMs,Error)) { return false; }
    }
    return true;
}

bool ALDCombatDirector::RelocateArea(FLDBodyCombatState& Body, const FLDCombatSourceRef& Host, const FVector& Foot, int64 NowMs,
    FString& Error)
{
    TArray<FLDPassiveEffectRef> Moves;
    if (!WorkshopOn(Body.AttributeProjection,ELDDemoEffectOp::ZoneRelocate,Host,Moves,Error)) { return false; }
    if (Moves.IsEmpty()) { return true; }
    // C17 (dash end) and C22 (host hit): the most recent live zone (never a tethered one) or armed, untriggered trap of this body, once in its life.
    FLDCombatZone* Zone=nullptr;
    FLDCombatGroundEffect* Trap=nullptr;
    uint64 Newest=0;
    for (FLDCombatZone& Row : Zones)
    {
        if (Row.SourceEntity!=Body.Victim.EntityId || Row.bTether || Row.bRelocated || Row.EndMs<=NowMs || Row.CreatedSerial<=Newest) { continue; }
        Newest=Row.CreatedSerial;
        Zone=&Row;
    }
    for (FLDCombatGroundEffect& Row : GroundEffects)
    {
        if (!Row.bTrap || Row.bTriggered || Row.bRelocated || Row.ExpiresAtMs<=NowMs || Row.Payload.SourceEntity!=Body.Victim.EntityId
            || Row.CreatedSerial<=Newest)
        { continue; }
        Newest=Row.CreatedSerial;
        Trap=&Row;
        Zone=nullptr;
    }
    FVector From=FVector::ZeroVector;
    if (Trap)
    {
        From=Trap->CentreCm;
        Trap->CentreCm=Foot;
        Trap->bRelocated=true;
    }
    else if (Zone)
    {
        From=Zone->CentreCm;
        Zone->CentreCm=Foot;
        Zone->bRelocated=true;
    }
    else { return true; }
    MarkViewChanged(Body.Victim.EntityId);
    LedgerComponentTriggered(Body,Moves[0].Source.DefinitionId);
    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_ZONE_RELOCATE t=%lld holder=%s via=%s what=%s from=%s to=%s"),NowMs,
        *LDLog::Id(Body.Victim.EntityId),*LDLog::Source(Host),Trap ? TEXT("trap") : TEXT("zone"),*From.ToCompactString(),
        *Foot.ToCompactString());
    return true;
}
