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

    FLDBodyCombatState Staged = *Body;
    Out.HpBeforeP2 = Staged.Resources.HpP2;
    Out.MpBeforeP2 = Staged.Resources.MpP2;
    HealBody(Staged, LDAirdrop::ShareP2(Staged.Resources.MaxHpP2, P.AirdropHealMaxHPBp));
    GrantMana(Staged, LDAirdrop::ShareP2(Staged.Resources.MaxMpP2, P.AirdropManaMaxMPBp));
    LDTactical::ApplyTimedBonus(Staged.TimedBonuses, Bonus);
    if (!RefreshTemporary(Staged, Actor, NowMs, Error)) { return false; }
    Staged.StateRevision = Body->StateRevision + 1;
    *Body = MoveTemp(Staged);
    Out.HpAfterP2 = Body->Resources.HpP2;
    Out.MpAfterP2 = Body->Resources.MpP2;
    Out.MoveSpeedBonusUntilMs = Bonus.ExpiresAtMs;
    bOutApplied = true;
    Error.Reset();
    return true;
}
