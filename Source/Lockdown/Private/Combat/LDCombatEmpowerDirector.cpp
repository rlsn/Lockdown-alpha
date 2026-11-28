#include "Combat/LDCombatDirector.h"

#include "Character/LDCharacter.h"
#include "Combat/LDAutoEmpower.h"
#include "Combat/LDCombatElements.h"
#include "Core/LDLog.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"

// Auto-empower (自动强化, decision M-12) on Combat's records (docs/systems/COMBAT_AND_GROWTH.md "自动强化实验"). The pure rules
// are LDAutoEmpower; this file owns the host choice, the admission, the payment and the processors' Combat-side parts. Every
// entry here is inert while the experiment is off (EmpowerTerms() is null).

namespace LDCombatEmpowerDetail
{
    double Cm(const FLDDemoRational& Value) { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    ELDDemoSkillElement FromCardElement(ELDDemoElement Element)
    {
        switch (Element)
        {
        case ELDDemoElement::Fire: return ELDDemoSkillElement::Fire;
        case ELDDemoElement::Water: return ELDDemoSkillElement::Water;
        case ELDDemoElement::Electric: return ELDDemoSkillElement::Shock;
        case ELDDemoElement::Chill: return ELDDemoSkillElement::Chill;
        case ELDDemoElement::Rot: return ELDDemoSkillElement::Rot;
        default: return ELDDemoSkillElement::None;
        }
    }
}

const FLDDemoEmpowerParameters* ALDCombatDirector::EmpowerTerms() const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { return nullptr; }
    const FLDDemoEmpowerParameters& P=Content->Get().Parameters.Empower;
    return LDAutoEmpower::Enabled(P) ? &P : nullptr;
}

bool ALDCombatDirector::IsEmpoweredHit(const FLDCombatHit& Hit)
{
    return Hit.EmpowerSerial>0 || (Hit.CastLedger.IsValid() && Hit.CastLedger->EmpowerSerial>0);
}

ELDCombatEntry ALDCombatDirector::EmpowerHostOf(const FLDCombatEntityRecord& Record) const
{
    if (Record.EmpowerHost!=ELDCombatEntry::None) { return Record.EmpowerHost; }
    if (const ELDCombatEntry Forced=LDAutoEmpower::SwitchHost(); Forced!=ELDCombatEntry::None) { return Forced; }
    const ALDGameMode* Mode=Core();
    // A human starts on the basic (spec: no automatic host for players); an AI contender takes its best-invested entry.
    if (!Mode || Mode->FindContenderController(Record.Entity)) { return ELDCombatEntry::Basic; }
    TArray<LDAutoEmpower::FHostCandidate> Candidates;
    const auto CardsOn=[&Record](ELDCardTargetKind Kind, FName Active)
    {
        int32 Score=0;
        for (const FLDInstalledCardValue& Card : Record.Build.Cards)
        {
            if (Card.TargetKind==Kind && (Kind!=ELDCardTargetKind::Skill || Card.Active==Active)) { Score+=FMath::Max<int32>(Card.Tier,1); }
        }
        return Score;
    };
    for (const ELDCombatEntry Entry : {ELDCombatEntry::Q,ELDCombatEntry::W,ELDCombatEntry::E})
    {
        LDAutoEmpower::FHostCandidate& Row=Candidates.AddDefaulted_GetRef();
        Row.Entry=Entry;
        if (const FLDLearnedActiveValue* Learned=Record.Build.Actives.FindByPredicate(
            [Entry](const FLDLearnedActiveValue& Active) { return Active.Entry==Entry; }))
        {
            Row.bHeld=true;
            Row.Score=CardsOn(ELDCardTargetKind::Skill,Learned->AbilityId)+LDSkillBranch::RankOf(Learned->Branch)-1;
        }
    }
    Candidates.Add({ELDCombatEntry::Basic,true,CardsOn(ELDCardTargetKind::Body,NAME_None)});
    Candidates.Add({ELDCombatEntry::Dash,true,CardsOn(ELDCardTargetKind::Dash,NAME_None)});
    return LDAutoEmpower::AutoHost(Candidates);
}

ELDEmpowerProcessor ALDCombatDirector::EmpowerProcessorOf(const FLDCombatEntityRecord& Record, ELDCombatEntry Entry) const
{
    if (Entry==ELDCombatEntry::Basic || Entry==ELDCombatEntry::Dash) { return LDAutoEmpower::ProcessorOf(Entry,nullptr,nullptr); }
    FLDCombatSourceRef Source;
    const FLDDemoAbilityDefinition* Ability=nullptr;
    FLDCombatActiveTerms Terms;
    if (!ResolveEntry(Record,Entry,Source,Ability) || !ActiveOf(Record,Source,Ability,Terms)) { return ELDEmpowerProcessor::None; }
    return LDAutoEmpower::ProcessorOf(Entry,&Terms.Target,&Terms.Values);
}

bool ALDCombatDirector::EmpowerFoeNear(const FLDBodyCombatState& Body, bool bCamp) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    const ALDCharacter* Actor=ActorOf(Body);
    if (!Content || !Actor) { return false; }
    const double Radius=LDCombatEmpowerDetail::Cm(Content->Get().Parameters.VisionRadiusCm);
    const FVector Foot=FootOf(*Actor);
    for (const auto& Pair : Bodies)
    {
        const FLDBodyCombatState& Other=Pair.Value;
        const ALDCharacter* OtherActor=Pair.Key.Get();
        if (!OtherActor || &Other==&Body || Other.bPendingDeath || Other.bSuspended || IsIsolated(Body.Victim.EntityId,Other)) { continue; }
        const bool bContender=Other.Victim.Kind==ELDVictimKind::Contender && !Other.bTrainingDummy
            && (Other.PKSide==0 || Other.PKSide!=Body.PKSide);
        const bool bWanted=bCamp ? (Other.Victim.Kind==ELDVictimKind::CampMember || Other.bTrainingDummy) : bContender;
        if (bWanted && FVector::Dist2D(Foot,FootOf(*OtherActor))<=Radius) { return true; }
    }
    return false;
}

ELDEmpowerProcessor ALDCombatDirector::AdmitEmpower(const FLDBodyCombatState& Body, const FLDCombatEntityRecord& Record,
    ELDCombatEntry Entry, const FLDCombatBodyRef* Target, int64 NowMs) const
{
    const FLDDemoEmpowerParameters* P=EmpowerTerms();
    if (!P || Body.Victim.Kind!=ELDVictimKind::Contender || Body.bTrainingDummy || Body.bPendingDeath
        || EmpowerHostOf(Record)!=Entry || !LDAutoEmpower::Ready(*P,Body.Resources.MomentumP2))
    { return ELDEmpowerProcessor::None; }
    if (Entry==ELDCombatEntry::Basic)
    {
        // A punch at a contender; at a camp member or a dummy only while no enemy contender is near (no waste on a camp).
        const FLDBodyCombatState* Struck=Target ? FindBody(*Target) : nullptr;
        if (!Struck) { return ELDEmpowerProcessor::None; }
        const bool bContender=Struck->Victim.Kind==ELDVictimKind::Contender && !Struck->bTrainingDummy;
        if (!bContender && EmpowerFoeNear(Body,false)) { return ELDEmpowerProcessor::None; }
    }
    else if (Entry==ELDCombatEntry::Dash)
    {
        // D's later split segments are never their own action; a D taken far from any fight (travel) is not empowered.
        if (Body.SplitDashLeft>0 && NowMs<=Body.SplitDashUntilMs) { return ELDEmpowerProcessor::None; }
        if (!EmpowerFoeNear(Body,false) && !EmpowerFoeNear(Body,true)) { return ELDEmpowerProcessor::None; }
    }
    else
    {
        // A branch's free second release is part of the first one.
        FLDCombatSourceRef Source;
        const FLDDemoAbilityDefinition* Ability=nullptr;
        if (!ResolveEntry(Record,Entry,Source,Ability) || BranchRecastOpen(Body,Source,NowMs)) { return ELDEmpowerProcessor::None; }
    }
    return EmpowerProcessorOf(Record,Entry);
}

bool ALDCombatDirector::ApplyEmpowerTerms(const FLDBodyCombatState& Body, const FLDCombatEntityRecord& Record,
    const FLDDemoAbilityDefinition* Ability, ELDEmpowerProcessor Processor, FLDCombatActiveTerms& Terms, FString& Error) const
{
    using namespace LDCombatEmpowerDetail;
    const FLDDemoEmpowerParameters* P=EmpowerTerms();
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!P || !Content) { Error=TEXT("An empowered release needs the running EMPOWER terms."); return false; }
    switch (Processor)
    {
    case ELDEmpowerProcessor::Strike: LDAutoEmpower::ApplyStrike(*P,Terms.Target,Terms.Values); break;
    case ELDEmpowerProcessor::Guard: LDAutoEmpower::ApplyGuard(*P,Terms.Values); break;
    case ELDEmpowerProcessor::Ward: LDAutoEmpower::ApplyWard(*P,Terms.Values); break;
    case ELDEmpowerProcessor::Fist: LDAutoEmpower::ApplyFistSkill(*P,Terms.Values); break;
    default: break;
    }
    if (Processor!=ELDEmpowerProcessor::Strike && Processor!=ELDEmpowerProcessor::Ward) { return true; }
    // The element layer: the active's element, else its learned branch's, else its slot medal's.
    ELDDemoSkillElement Element=Ability ? Ability->Element : ELDDemoSkillElement::None;
    if (Element==ELDDemoSkillElement::None && Ability)
    {
        if (const FLDDemoAbilityBranch* Branch=Ability->FindBranch(Terms.Branch); Branch && Branch->Element!=ELDDemoSkillElement::None)
        { Element=Branch->Element; }
    }
    if (Element==ELDDemoSkillElement::None && Ability)
    {
        TArray<FLDPassiveEffectRef> Medals;
        if (!CardsOn(Body.AttributeProjection,ELDDemoEffectOp::SkillElement,{ELDCombatSourceKind::Learned,FName(*Ability->Id)},Medals,Error))
        { return false; }
        for (const FLDPassiveEffectRef& Ref : Medals)
        {
            if (const ELDDemoSkillElement Mapped=FromCardElement(Ref.Effect.Element); Mapped!=ELDDemoSkillElement::None) { Element=Mapped; break; }
        }
    }
    if (Element==ELDDemoSkillElement::None) { return true; }
    const auto Has=[&Terms](ELDDemoEffectOp Op)
    { return Terms.Values.Effects.ContainsByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; }); };
    const bool bAreaMode=Terms.Target.Mode==ELDDemoTargetMode::Self || Terms.Target.Mode==ELDDemoTargetMode::DirectionDash;
    const bool bCanDeliverHostile=!bAreaMode || Has(ELDDemoEffectOp::SelfArea) || Has(ELDDemoEffectOp::DashContact);
    int64 ReactionP2=0;
    if (Element==ELDDemoSkillElement::Shock)
    {
        FLDElementTerms Reaction;
        if (!ElementTerms(Reaction,Error)) { return false; }
        ReactionP2=LDCombatElements::ReactionDamageP2(Record.Build.Level,Body.AttributeProjection.Final.APP2,Reaction);
    }
    LDAutoEmpower::ApplyElement(*P,Element,bCanDeliverHostile,ReactionP2,Content->Get().Parameters.Elements.RotMs,Terms.Values);
    Error.Reset();
    return true;
}

int32 ALDCombatDirector::PayEmpower(FLDBodyCombatState& Body, const FLDCombatAction& Action, bool bApplied, int64 NowMs)
{
    const FLDDemoEmpowerParameters* P=EmpowerTerms();
    const int64 BeforeP2=Body.Resources.MomentumP2;
    int32 Serial=0;
    if (bApplied && P)
    {
        if (!Action.bEmpowerPrepaid)
        {
            const int64 Paid=-GrantMomentum(Body,-P->CostP2,ELDMomentumBook::Spent);
            Body.EmpowerSpentP2+=Paid;
        }
        Serial=++Body.Empowerments;
        Body.LastEmpowerProcessor=Action.EmpowerProcessor;
        Body.LastEmpowerAtMs=NowMs;
        ++Body.StateRevision;
        MarkViewChanged(Body.Victim.EntityId);
    }
    UE_LOG(LogLDCombat,Log,TEXT("LD_EMPOWER t=%lld src=%s life=%u host=%s processor=%s source=%s serial=%d paid=%s before=%s after=%s outcome=%s"),
        NowMs,*LDLog::Id(Body.Victim.EntityId),Body.Victim.Life,LDAutoEmpower::EntryName(Action.Entry),
        LDAutoEmpower::ProcessorName(Action.EmpowerProcessor),*LDLog::Source(Action.Source),Serial,
        *LDLog::P2(BeforeP2-Body.Resources.MomentumP2),*LDLog::P2(BeforeP2),*LDLog::P2(Body.Resources.MomentumP2),
        Serial>0 ? (Action.bEmpowerPrepaid ? TEXT("prepaid") : TEXT("applied")) : TEXT("short"));
    return Serial;
}

void ALDCombatDirector::MarkEmpoweredChain()
{
    if (FChainTrack* Track=CurrentChain.IsValid() ? OpenChains.Find(CurrentChain) : nullptr) { Track->bEmpowered=true; }
}

bool ALDCombatDirector::EmpowerAfterRelease(FLDBodyCombatState& Body, ELDEmpowerProcessor Processor, const FLDCombatSourceRef& Exit,
    const FGuid& EffectId, const TSharedPtr<FLDCombatCastLedger>& Ledger, int64 NowMs, FString& Error)
{
    const FLDDemoEmpowerParameters* P=EmpowerTerms();
    if (!P) { Error=TEXT("An empowered release needs the running EMPOWER terms."); return false; }
    switch (Processor)
    {
    case ELDEmpowerProcessor::Guard:
        // The shield this release granted bursts for a share of its grant when it breaks or runs out.
        if (Body.ShieldLayers.ContainsByPredicate([&EffectId](const FLDBodyCombatState::FShieldLayer& Layer) { return Layer.EffectId==EffectId; }))
        {
            FLDCombatBranchState::FShieldBranch& Row=Body.Branch.ShieldBranchFor(EffectId);
            FLDDemoEffectDefinition Burst;
            Burst.Opcode=uint16(ELDDemoEffectOp::ShieldBurstBreak);
            Burst.RadiusCm=P->GuardBurstRadiusCm;
            Burst.BaseP2=LDAutoEmpower::Share(Body.LastShieldGrantP2,P->GuardBurstRatioBp);
            Row.BreakBurst=Burst;
            Row.bBurstOnLapse=true;
            Row.Exit=Exit;
        }
        break;
    case ELDEmpowerProcessor::Move:
        // The quake where the dash ends (a branch's own end burst keeps its place).
        if (!Body.Branch.DashEndBurst.IsSet())
        {
            FLDDemoEffectDefinition Quake;
            Quake.Opcode=uint16(ELDDemoEffectOp::DelayedBurst);
            Quake.RadiusCm=P->QuakeRadiusCm;
            Quake.SecondaryRatioBp=P->QuakeSlowBp;
            Quake.SecondaryDurationMs=P->QuakeSlowMs;
            Body.Branch.DashEndBurst=Quake;
            Body.Branch.DashEndExit=Exit;
            Body.Branch.DashEndEffectId=EffectId;
            Body.Branch.DashEndAtMs=FMath::Max(Body.DashUntilMs,NowMs);
            Body.Branch.DashEndLedger=Ledger;
        }
        break;
    case ELDEmpowerProcessor::Fist:
        // A single-package fist skill (S11): the punch it stores is the empowered punch.
        if (Body.Empowers.ContainsByPredicate([&EffectId](const FLDCombatEmpower& Package) { return Package.EffectId==EffectId && Package.Charges<=1; }))
        { Body.bEmpowerPunchPending=true; }
        break;
    default:
        break;
    }
    Error.Reset();
    return true;
}

bool ALDCombatDirector::EmpowerShieldBurst(FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Burst, const FLDCombatSourceRef& Exit,
    FString& Error)
{
    const ALDCharacter* Actor=ActorOf(Body);
    if (!Actor || Burst.BaseP2<=0) { return true; }
    TArray<FLDCombatBodyRef> Around;
    if (!CollectAreaTargets(FootOf(*Actor),LDCombatEmpowerDetail::Cm(Burst.RadiusCm),&Body,Body.Victim.EntityId,Around,Error)) { return false; }
    const FGuid BurstId=FGuid::NewGuid();
    for (const FLDCombatBodyRef& Other : Around)
    {
        FLDCombatHit& Blast=PendingHits.AddDefaulted_GetRef();
        Blast.HitId=FGuid::NewGuid();
        Blast.EffectId=BurstId;
        Blast.Target=Other;
        Blast.SourceBody={Body.Victim,Body.Identity};
        Blast.SourceEntity=Body.Victim.EntityId;
        Blast.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
        Blast.Source=Exit;
        Blast.HitClass=ELDCombatHitClass::Ability;
        Blast.BaseP2=Burst.BaseP2;
        Blast.DamageType=ELDDemoDamageType::Magical;
        Blast.bFromShieldTrigger=true;
        Blast.EmpowerSerial=FMath::Max(Body.Empowerments,1);
        if (!StampSourceTerms(Body,Blast,Error)) { return false; }
    }
    return true;
}

bool ALDCombatDirector::EmpowerPunch(const ALDCharacter& Actor, FLDBodyCombatState& Body, FLDCombatHit& Primary,
    TArray<FLDCombatHit>& OutCone, FString& Error)
{
    using namespace LDCombatEmpowerDetail;
    const FLDDemoEmpowerParameters* P=EmpowerTerms();
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!P || !Content) { Error=TEXT("An empowered punch needs the running EMPOWER terms."); return false; }
    ALDCharacter* TargetActor=nullptr;
    if (!FindBody(Primary.Target,&TargetActor) || !TargetActor) { return true; }
    const FVector Foot=FootOf(Actor);
    const FVector Ahead=(FootOf(*TargetActor)-Foot).GetSafeNormal2D();
    const double PushCm=Cm(P->FistPushCm);
    const int64 PushMs=Content->Get().Parameters.Elements.PushMs;
    // The target is pushed away from the puncher.
    if (PushCm>Primary.KnockbackCm)
    {
        Primary.KnockbackCm=PushCm;
        Primary.KnockbackMs=FMath::Max(Primary.KnockbackMs,PushMs);
        Primary.KnockbackAnchorCm=Foot;
        Primary.bKnockbackFromAnchor=true;
    }
    if (Ahead.IsNearlyZero()) { return true; }
    TArray<FLDCombatBodyRef> Around;
    if (!CollectAreaTargets(Foot,Cm(P->FistConeLengthCm),&Body,Body.Victim.EntityId,Around,Error)) { return false; }
    const double HalfCos=FMath::Cos(FMath::DegreesToRadians(Cm(P->FistConeAngleDegrees)*.5));
    for (const FLDCombatBodyRef& Other : Around)
    {
        ALDCharacter* OtherActor=nullptr;
        if (Other==Primary.Target || !FindBody(Other,&OtherActor) || !OtherActor) { continue; }
        const FVector To=(FootOf(*OtherActor)-Foot).GetSafeNormal2D();
        if (FVector::DotProduct(To,Ahead)<HalfCos) { continue; }
        // Another body in the cone takes FistConeRatio of the punch's own package and the push; it is added damage (no on-hit,
        // no combo step) that carries the punch's elements.
        FLDCombatHit Wave=Primary;
        Wave.HitId=FGuid::NewGuid();
        Wave.Target=Other;
        Wave.DeliveryScaleBp=LDAutoEmpower::Share(Primary.DeliveryScaleBp,P->FistConeRatioBp);
        Wave.ComboStep=0;
        Wave.bExtraStrike=true;
        Wave.bAddedDamage=true;
        Wave.OnHitScaleBp=0;
        Wave.bLifesteal=false;
        OutCone.Add(MoveTemp(Wave));
    }
    return true;
}

void ALDCombatDirector::TrackEmpowerArmed(FLDBodyCombatState& Body, int64 NowMs) const
{
    const FLDDemoEmpowerParameters* P=EmpowerTerms();
    const bool bArmed=P && Body.Victim.Kind==ELDVictimKind::Contender && Body.Resources.MomentumP2>=P->ThresholdP2;
    if (bArmed && Body.EmpowerArmedSinceMs<0) { Body.EmpowerArmedSinceMs=NowMs; }
    else if (!bArmed && Body.EmpowerArmedSinceMs>=0)
    {
        Body.EmpowerArmedMs+=FMath::Max<int64>(NowMs-Body.EmpowerArmedSinceMs,0);
        Body.EmpowerArmedSinceMs=-1;
    }
}

bool ALDCombatDirector::SubmitEmpowerHost(const FLDCombatCaller& Caller, uint64 Sequence, ELDCombatEntry Host, FString& Reason)
{
    const ALDGameMode* Mode=Core();
    if (!EmpowerTerms()) { Reason=TEXT("强化实验未开启"); return false; }
    if (!Mode || Caller.Kind!=ELDVictimKind::Contender || !Caller.Match.IsValid() || Caller.Match!=Mode->GetMatchContext())
    { Reason=TEXT("对局已变化"); return false; }
    FLDCombatEntityRecord* Record=FindRecord(Caller.EntityId);
    if (!Record || Record->Kind!=ELDVictimKind::Contender) { Reason=TEXT("没有可指定的构筑"); return false; }
    if (!LDAutoEmpower::IsHostEntry(Host)) { Reason=TEXT("这里不能作为强化宿主"); return false; }
    // A repeated or older request was already judged: it changes nothing.
    if (Sequence==0 || Sequence<=Record->EmpowerHostSequence) { return true; }
    Record->EmpowerHostSequence=Sequence;
    Record->EmpowerHost=Host;
    ++Record->ViewRevision;
    UE_LOG(LogLDCombat,Log,TEXT("LD_EMPOWER_HOST t=%lld src=%s host=%s sequence=%llu"),Mode->GetAuthorityNowMs(),*LDLog::Id(Caller.EntityId),
        LDAutoEmpower::EntryName(Host),Sequence);
    return true;
}
