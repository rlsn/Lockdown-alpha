#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDLifeAuthority.h"

// Spec §6.5 chains on Combat's records (docs/systems/COMBAT_AND_GROWTH.md, "连锁"). A contender's action opens a chain keyed
// by a ChainId; the release's effect id maps to it, so its projectiles, zones and follow-ups stay in it, and every hit derived
// while a hit of the chain resolves inherits it (ApplyPendingHits). Steps are recorded while the chain is current.

FGuid ALDCombatDirector::OpenChain(const FLDBodyCombatState& Body, const FGuid& EffectId, const FGuid& Inherit, int64 NowMs)
{
    if (Body.Victim.Kind!=ELDVictimKind::Contender || Body.bTrainingDummy || !EffectId.IsValid()) { return FGuid(); }
    // A free repeat (K06, A08) stays in the chain of the action that queued it while that chain is open.
    if (Inherit.IsValid())
    {
        if (FChainTrack* Open=OpenChains.Find(Inherit))
        {
            EffectChains.Add(EffectId,Inherit);
            Open->Effects.Add(EffectId);
            return Inherit;
        }
    }
    const FGuid Chain=FGuid::NewGuid();
    FChainTrack& Track=OpenChains.Add(Chain);
    Track.Owner=Body.Victim.EntityId;
    Track.LastStepMs=NowMs;
    Track.Effects.Add(EffectId);
    EffectChains.Add(EffectId,Chain);
    return Chain;
}

FGuid ALDCombatDirector::ChainOfHit(const FLDCombatHit& Hit) const
{
    if (Hit.ChainId.IsValid()) { return OpenChains.Contains(Hit.ChainId) ? Hit.ChainId : FGuid(); }
    const FGuid* Chain=EffectChains.Find(Hit.EffectId);
    return Chain ? *Chain : FGuid();
}

void ALDCombatDirector::ChainStep(const FGuid& Entity, FName Kind, int64 NowMs)
{
    FChainTrack* Track=CurrentChain.IsValid() ? OpenChains.Find(CurrentChain) : nullptr;
    // A step belongs to the chain's owner: another body's answer (a reflection, an aura) is no step of this chain.
    if (!Track || Track->Owner!=Entity || Kind.IsNone()) { return; }
    Track->Steps.Add(Kind);
    Track->LastStepMs=FMath::Max(Track->LastStepMs,NowMs);
}

void ALDCombatDirector::ChainDamage(const FLDCombatHit& Hit, const FLDBodyCombatState& Victim, int64 DamageP2)
{
    if (DamageP2<=0 || !Hit.CreditOwner.IsValid() || Hit.CreditOwner==Victim.Victim.EntityId || Victim.bTrainingDummy) { return; }
    const FGuid Chain=ChainOfHit(Hit);
    if (FChainTrack* Track=Chain.IsValid() ? OpenChains.Find(Chain) : nullptr; Track && Track->Owner==Hit.CreditOwner)
    {
        Track->DamageP2+=DamageP2;
    }
    // 结算“本局最离谱”: the single strongest hit on a contender.
    if (Victim.Victim.Kind!=ELDVictimKind::Contender) { return; }
    const FLDCombatEntityRecord* Credited=FindRecord(Hit.CreditOwner);
    if (!Credited || Credited->Kind!=ELDVictimKind::Contender) { return; }
    FChainOwner& Record=ChainOwners.FindOrAdd(Hit.CreditOwner);
    if (DamageP2<=Record.Best.BestDamageP2) { return; }
    Record.Best.BestDamageP2=DamageP2;
    Record.Best.BestTargetMaxHpP2=Victim.Resources.MaxHpP2;
    // A reaction's damage names its reaction; every other hit its source.
    if (Hit.ReactionKind!=ELDCombatReaction::None)
    {
        Record.Best.BestSourceKind=uint8(ELDCombatSourceKind::None);
        Record.Best.BestSource=LDChain::ReactionStep(LDCombatElements::ReactionName(Hit.ReactionKind));
    }
    else
    {
        Record.Best.BestSourceKind=uint8(Hit.Source.Kind);
        Record.Best.BestSource=Hit.Source.Kind==ELDCombatSourceKind::Basic ? FName(TEXT("basic")) : Hit.Source.DefinitionId;
    }
}

bool ALDCombatDirector::AdvanceChains(int64 NowMs, FString& Error, bool bAll)
{
    if (OpenChains.IsEmpty()) { return true; }
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Chains require the approved Demo content."); return false; }
    const FLDDemoContentParameters& P=Content->Get().Parameters;
    TArray<FGuid> Ended;
    for (const TPair<FGuid,FChainTrack>& Pair : OpenChains)
    {
        if (bAll || NowMs-Pair.Value.LastStepMs>=P.ChainEndMs) { Ended.Add(Pair.Key); }
    }
    // A deterministic order for the logs and serials.
    Ended.Sort([this](const FGuid& A, const FGuid& B)
    {
        const FChainTrack& Left=OpenChains[A];
        const FChainTrack& Right=OpenChains[B];
        return Left.LastStepMs!=Right.LastStepMs ? Left.LastStepMs<Right.LastStepMs : A<B;
    });
    for (const FGuid& Chain : Ended)
    {
        const FChainTrack Track=OpenChains.FindAndRemoveChecked(Chain);
        for (const FGuid& Effect : Track.Effects) { EffectChains.Remove(Effect); }
        TArray<FName> Key;
        LDChain::Key(Track.Steps,Key);
        if (!LDChain::Counts(Key,P.ChainMinKeySteps)) { continue; }
        FChainOwner& Holder=ChainOwners.FindOrAdd(Track.Owner);
        FLDChainEndView& End=Holder.Recent.AddDefaulted_GetRef();
        End.Serial=Holder.NextSerial++;
        End.Key=Key;
        End.Steps=Track.Steps.Num();
        End.DamageP2=Track.DamageP2;
        if (End.Steps>Holder.Best.LongestSteps)
        {
            Holder.Best.LongestSteps=End.Steps;
            Holder.Best.LongestKey=Key;
        }
        const int32 Steps=End.Steps;
        if (Holder.Recent.Num()>LDChain::OwnerRecentEnds) { Holder.Recent.RemoveAt(0); }
        MarkViewChanged(Track.Owner);
        // Each contender's longest few of the match are logged (spec §6.5); the others only reach their owner.
        if (LDChain::KeepLongest(Holder.Logged,Steps,LDChain::LoggedPerOwner))
        {
            UE_LOG(LogLDCombat,Log,TEXT("LD_CHAIN t=%lld owner=%s steps=%d key=%s damage=%s"),NowMs,*LDLog::Id(Track.Owner),Steps,
                *LDChain::Text(Key,TEXT(">")),*LDLog::P2(Track.DamageP2));
        }
    }
    return true;
}

void ALDCombatDirector::ReadOwnerChains(const FGuid& Entity, TArray<FLDChainEndView>& Out) const
{
    const FChainOwner* Holder=ChainOwners.Find(Entity);
    Out=Holder ? Holder->Recent : TArray<FLDChainEndView>();
}

void ALDCombatDirector::ReadResultHighlights(TArray<FLDResultHighlight>& Out) const
{
    Out.Reset();
    for (const auto& Pair : Records)
    {
        if (Pair.Value.Kind!=ELDVictimKind::Contender) { continue; }
        FLDResultHighlight& Row=Out.AddDefaulted_GetRef();
        if (const FChainOwner* Holder=ChainOwners.Find(Pair.Key)) { Row=Holder->Best; }
        Row.Contender=Pair.Key;
    }
    Out.Sort([](const FLDResultHighlight& A, const FLDResultHighlight& B) { return A.Contender<B.Contender; });
}
