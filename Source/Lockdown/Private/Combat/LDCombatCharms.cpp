#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "World/LDCharms.h"

// #198 field charms: the effect side of a claim. Heal and mana use the same one-time resource paths as every other
// recovery (HealBody, GrantMana); move speed is an ordinary timed bonus of its own source, so the phase cleanup that
// clears temporary bonuses removes it and a later pickup refreshes rather than stacks it.

bool ALDCombatDirector::ApplyCharm(const FGuid& Contender, ELDCharmKind Kind, const FLDGameplayWritePermit& Permit,
    bool& bOutApplied, FLDCharmEffectFact& Out, FString& Error)
{
    bOutApplied = false;
    Out = {};
    ALDGameMode* Mode = Core();
    const FLDDemoContentSnapshot* Content = Catalog();
    if (!Mode || Mode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || !Content
        || !Contender.IsValid() || Kind == ELDCharmKind::None)
    {
        Error = TEXT("A charm needs the current Transactions permit, the approved content, a contender and a kind.");
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
    Out.HpBeforeP2 = Body->Resources.HpP2;
    Out.MpBeforeP2 = Body->Resources.MpP2;
    switch (Kind)
    {
    case ELDCharmKind::Heal:
        HealBody(*Body, LDCharms::ShareP2(Body->Resources.MaxHpP2, P.CharmHealMaxHPBp));
        break;
    case ELDCharmKind::Mana:
        GrantMana(*Body, LDCharms::ShareP2(Body->Resources.MaxMpP2, P.CharmManaMaxMPBp));
        break;
    case ELDCharmKind::MoveSpeed:
    {
        FLDCombatTimedBonus Bonus;
        Bonus.EffectId = FGuid::NewGuid();
        Bonus.SourceDefinition = LDCharms::MoveSpeedSource();
        FLDDemoStatBonus& Stat = Bonus.Stats.AddDefaulted_GetRef();
        Stat.Stat = ELDDemoBonusStat::MoveSpeedBonusRatio;
        Stat.Amount = P.CharmMoveSpeedBp;
        if (!LDMatchLifecycle::AddDurationMs(NowMs, P.CharmMoveSpeedMs, Bonus.ExpiresAtMs, Error)) { return false; }
        LDTactical::ApplyTimedBonus(Body->TimedBonuses, Bonus);
        if (!RefreshTemporary(*Body, Actor, NowMs, Error)) { return false; }
        ++Body->StateRevision;
        MarkViewChanged(Body->Victim.EntityId);
        Out.BonusUntilMs = Bonus.ExpiresAtMs;
        break;
    }
    default:
        Error = FString::Printf(TEXT("Charm kind %u has no effect."), uint32(Kind));
        return false;
    }
    Out.HpAfterP2 = Body->Resources.HpP2;
    Out.MpAfterP2 = Body->Resources.MpP2;
    bOutApplied = true;
    Error.Reset();
    return true;
}
