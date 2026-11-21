#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDLifeAuthority.h"
#include "World/LDAirdrop.h"

bool ALDCombatDirector::ApplyAirdropReward(const FGuid& Contender, const FLDGameplayWritePermit& Permit, bool& bOutApplied,
    FLDAirdropRewardFact& Out, FString& Error)
{
    bOutApplied = false;
    Out = {};
    ILDLifeAuthority* Mode = Authority();
    const FLDDemoContentSnapshot* Content = Catalog();
    if (!Mode || Mode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || !Content
        || !Contender.IsValid())
    {
        Error = TEXT("An airdrop reward needs the current Transactions permit, approved content and a contender.");
        return false;
    }
    FLDBodyCombatState* Body = nullptr;
    ALDCharacter* Actor = nullptr;
    for (auto& Pair : Bodies)
    {
        if (Pair.Key.IsValid() && Pair.Value.Victim.Kind == ELDVictimKind::Contender && Pair.Value.Victim.EntityId == Contender)
        {
            Body = &Pair.Value;
            Actor = Pair.Key.Get();
            break;
        }
    }
    if (!Body || Body->bPendingDeath || Body->Resources.HpP2 <= 0)
    {
        Error.Reset();
        return true;
    }
    const auto& P = Content->Get().Parameters;
    const int64 NowMs = Permit.GetBoundary().AtMs;
    FLDCombatTimedBonus Bonus;
    Bonus.EffectId = FGuid::NewGuid();
    Bonus.SourceDefinition = FName(TEXT("airdrop_move_speed"));
    FLDDemoStatBonus& Stat = Bonus.Stats.AddDefaulted_GetRef();
    Stat.Stat = ELDDemoBonusStat::MoveSpeedBonusRatio;
    Stat.Amount = P.AirdropMoveSpeedBp;
    if (!LDMatchLifecycle::AddDurationMs(NowMs, P.AirdropMoveSpeedMs, Bonus.ExpiresAtMs, Error)) { return false; }

    Out.HpBeforeP2 = Body->Resources.HpP2;
    Out.MomentumBeforeP2 = Body->Resources.MomentumP2;
    HealBody(*Body, LDAirdrop::ShareP2(Body->Resources.MaxHpP2, P.AirdropHealMaxHPBp));
    GrantMomentum(*Body, P.AirdropMomentumP2, ELDMomentumBook::Other);
    LDTactical::ApplyTimedBonus(Body->TimedBonuses, Bonus);
    if (!RefreshTemporary(*Body, Actor, NowMs, Error)) { return false; }
    ++Body->StateRevision;
    Out.HpAfterP2 = Body->Resources.HpP2;
    Out.MomentumAfterP2 = Body->Resources.MomentumP2;
    Out.MoveSpeedBonusUntilMs = Bonus.ExpiresAtMs;
    bOutApplied = true;
    Error.Reset();
    return true;
}

bool ALDCombatDirector::ApplyCampRecoveryOrb(const FGuid& Contender, const FLDGameplayWritePermit& Permit, bool& bOutApplied,
    FString& Error)
{
    bOutApplied = false;
    ILDLifeAuthority* Mode = Authority();
    if (!Mode || Mode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || !Contender.IsValid())
    {
        Error = TEXT("A camp recovery orb needs the current Transactions permit and a contender.");
        return false;
    }
    FLDBodyCombatState* Body = nullptr;
    ALDCharacter* Actor = nullptr;
    for (auto& Pair : Bodies)
    {
        if (Pair.Key.IsValid() && Pair.Value.Victim.Kind == ELDVictimKind::Contender && Pair.Value.Victim.EntityId == Contender)
        {
            Body = &Pair.Value;
            Actor = Pair.Key.Get();
            break;
        }
    }
    if (!Body || Body->bPendingDeath || Body->Resources.HpP2 <= 0)
    {
        Error.Reset();
        return true;
    }
    // Camp-clear recovery orb: 12% MaxHP + 25 momentum. Mutate *Body in place so HealBody's card_grow_fat reprojection stays live.
    HealBody(*Body, LDAirdrop::ShareP2(Body->Resources.MaxHpP2, 1200));
    GrantMomentum(*Body, 2500, ELDMomentumBook::Other);
    if (!RefreshTemporary(*Body, Actor, Permit.GetBoundary().AtMs, Error)) { return false; }
    ++Body->StateRevision;
    bOutApplied = true;
    Error.Reset();
    return true;
}
