#include "Development/LDGrowthDirector.h"
#include "Core/LDLog.h"

#include "Combat/LDAssistRules.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDProgressionRules.h"
#include "Engine/World.h"
#include "Evaluation/LDEvaluationSeeds.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Serialization/MemoryWriter.h"
#include "World/LDCampDirector.h"

THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
constexpr int32 GrowthRewardWindow = 64;

#if WITH_DEV_AUTOMATION_TESTS
bool GrowthEvaluationAI(const FLDEvaluationSeeds* Seeds, const FGuid& Contender)
{
    if (!Seeds || !Contender.IsValid()) { return false; }
    for (const FGuid& Id : Seeds->AIContenders)
    {
        if (Id == Contender) { return true; }
    }
    return false;
}
#endif

void GrowthWriteVictim(FMemoryWriter& Writer, const FLDVictimKey& Victim)
{
    uint8 Kind = uint8(Victim.Kind);
    FGuid Entity = Victim.EntityId;
    uint32 Life = Victim.Life;
    Writer << Kind << Entity << Life;
}

// Every field of the immutable committed fact; a different fact under the same DeathKey conflicts.
void GrowthDeathFactBytes(const FLDCommittedDeath& Death, TArray<uint8>& Out)
{
    Out.Reset();
    FMemoryWriter Writer(Out);
    FString Tag = TEXT("death-reward");
    FGuid LedgerMatch = Death.Key.LedgerMatch;
    Writer << Tag << LedgerMatch;
    GrowthWriteVictim(Writer, Death.Key.Victim);
    GrowthWriteVictim(Writer, Death.VictimBody.Victim);
    FGuid VictimBody = Death.VictimBody.BodyIdentity;
    FGuid VictimContender = Death.VictimContender;
    FGuid FatalEffect = Death.FatalEffectId;
    FGuid CreditOwner = Death.CreditOwner;
    uint8 SourceKind = uint8(Death.ReleasedSource.Kind);
    FString SourceDefinition = Death.ReleasedSource.DefinitionId.ToString();
    Writer << VictimBody << VictimContender << FatalEffect << CreditOwner << SourceKind << SourceDefinition;
    GrowthWriteVictim(Writer, Death.SourceBodyAtRelease.Victim);
    FGuid SourceBody = Death.SourceBodyAtRelease.BodyIdentity;
    FString RewardDefinition = Death.RewardBasis.RewardDefinitionId.ToString();
    uint8 VictimLevel = Death.RewardBasis.VictimLevelAtDeath;
    uint8 CampStage = Death.RewardBasis.CampStageAtDeath;
    uint64 VictimBuild = Death.RewardBasis.VictimBuildRevisionAtDeath;
    uint8 Eligible = Death.RewardBasis.bCreditOwnerEligibleAtDeath ? 1 : 0;
    uint8 NoRewardReason = uint8(Death.RewardBasis.NoRewardReason);
    uint8 Cause = uint8(Death.Cause);
    double OccurredAtSeconds = Death.OccurredAtSeconds;
    int64 OccurredAtMs = Death.OccurredAtMs;
    Writer << SourceBody << RewardDefinition << VictimLevel << CampStage << VictimBuild << Eligible << NoRewardReason << Cause
        << OccurredAtSeconds << OccurredAtMs;
    int32 AssisterCount = Death.Assisters.Num();
    Writer << AssisterCount;
    for (FGuid Assister : Death.Assisters)
    {
        Writer << Assister;
    }
}

ELDGrowthReason GrowthNoRewardReason(ELDNoRewardReason Reason)
{
    switch (Reason)
    {
    case ELDNoRewardReason::NoOwner: return ELDGrowthReason::NoRewardNoOwner;
    case ELDNoRewardReason::SelfInflicted: return ELDGrowthReason::NoRewardSelf;
    case ELDNoRewardReason::OwnerLeft: return ELDGrowthReason::NoRewardOwnerLeft;
    case ELDNoRewardReason::PKArena: return ELDGrowthReason::NoRewardPKArena;
    default: return ELDGrowthReason::DataInvalid;
    }
}

ELDRewardOutcome GrowthCombatOutcome(ELDCombatPrepareError Error, ELDGrowthReason& Reason)
{
    switch (Error)
    {
    case ELDCombatPrepareError::CapacityExceeded:
        Reason = ELDGrowthReason::CapacityExceeded;
        return ELDRewardOutcome::RetryableNotCommitted;
    case ELDCombatPrepareError::NumericUnderflow:
        Reason = ELDGrowthReason::NumericUnderflow;
        return ELDRewardOutcome::PermanentlyRejected;
    case ELDCombatPrepareError::NumericDomain:
        Reason = ELDGrowthReason::NumericDomain;
        return ELDRewardOutcome::PermanentlyRejected;
    case ELDCombatPrepareError::StaleBuild:
        Reason = ELDGrowthReason::BuildStale;
        return ELDRewardOutcome::PermanentlyRejected;
    case ELDCombatPrepareError::UnknownContender:
        Reason = ELDGrowthReason::NotContender;
        return ELDRewardOutcome::PermanentlyRejected;
    case ELDCombatPrepareError::NotAuthority:
    case ELDCombatPrepareError::StaleMatch:
        Reason = ELDGrowthReason::NotAuthority;
        return ELDRewardOutcome::PermanentlyRejected;
    default:
        Reason = ELDGrowthReason::DataInvalid;
        return ELDRewardOutcome::PermanentlyRejected;
    }
}

bool GrowthIsFault(uint16 Reason)
{
    return LDGrowthWire::Reason(Reason) == ELDGrowthReason::AuthorityFault;
}
}

ALDGrowthDirector::ALDGrowthDirector()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
}

ALDGameMode* ALDGrowthDirector::Mode() const
{
    UWorld* World = GetWorld();
    return World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
}

void ALDGrowthDirector::Terms(const TArray<uint8>& Canonical, uint8 (&Out)[32])
{
    verifyf(SHA256(Canonical.GetData(), size_t(Canonical.Num()), Out) != nullptr, TEXT("SHA-256 of a growth business record failed."));
}

bool ALDGrowthDirector::SpawnDomain(ALDGameMode& InMode, const FActorSpawnParameters& Spawn, FString& Error)
{
    if (InMode.Growth || InMode.Development)
    {
        Error = TEXT("The growth domain already exists for this authority core.");
        return false;
    }
    UWorld* World = InMode.GetWorld();
    InMode.Growth = World->SpawnActor<ALDGrowthDirector>(Spawn);
    InMode.Development = World->SpawnActor<ALDDevelopmentDirector>(Spawn);
    if (!InMode.Growth || !InMode.Development)
    {
        RetireDomain(InMode);
        Error = TEXT("The growth or development director could not be created.");
        return false;
    }
    return true;
}

void ALDGrowthDirector::RetireDomain(ALDGameMode& InMode)
{
    if (InMode.Development) { InMode.Development->Destroy(); InMode.Development = nullptr; }
    if (InMode.Growth) { InMode.Growth->Destroy(); InMode.Growth = nullptr; }
}

bool ALDGrowthDirector::BeginMatch(ALDGameMode& InMode, const FLDMatchContext& InMatch, const TArray<FGuid>& InRoster, FString& Error)
{
    Error.Reset();
    if (!InMatch.IsValid() || InMatch != InMode.GetMatchContext() || InRoster.IsEmpty())
    {
        Error = TEXT("Growth initialization needs the current match and its locked roster.");
        return false;
    }
    if (!InMode.Development)
    {
        Error = TEXT("Growth initialization requires the spawned development director.");
        return false;
    }
    TSharedPtr<const FLDDemoContentSnapshot> Loaded;
    if (!LDDemoEntry::LoadRuntimeContent(Loaded, Error) || !Loaded)
    {
        if (Error.IsEmpty()) { Error = TEXT("The approved Demo catalog is unavailable for growth."); }
        return false;
    }
    if (!InMode.Development->BeginMatch(InMatch, InRoster, Loaded, Error))
    {
        return false;
    }
    Match = InMatch;
    Content = Loaded;
    Roster = InRoster;
    ArrivalCounter = 0;
    Dirty = TSet<FGuid>(InRoster);
    PublishRevisions.Reset();
    ResolvedLifeThrough.Reset();
    RecentRewards.Reset();
    PaidEvents.Reset();
#if WITH_DEV_AUTOMATION_TESTS
    DevGrants.Reset();
    if (LDDevGrowthFixture::IsSwitchPresent())
    {
        UE_LOG(LogLDGrowth, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE -LDDevGrowthFixture: generation=%u accepts DevQueueOffer and DevGrantReward on this authority"),
            Match.Generation);
    }
#endif
    return true;
}

void ALDGrowthDirector::RecordReward(const FLDRewardReceipt& Receipt)
{
    ResolvedLifeThrough.Add(TPair<uint8, FGuid>(uint8(Receipt.Key.Victim.Kind), Receipt.Key.Victim.EntityId), Receipt.Key.Victim.Life);
    if (RecentRewards.Num() == GrowthRewardWindow)
    {
        RecentRewards.RemoveAt(0, 1, EAllowShrinking::No);
    }
    RecentRewards.Add(Receipt);
}

FLDRewardReceipt ALDGrowthDirector::CommitDeathReward(const FLDCommittedDeath& Death, const FLDGameplayWritePermit& Permit)
{
    FLDRewardReceipt Receipt;
    Receipt.Key = Death.Key;
    TArray<uint8> FactBytes;
    GrowthDeathFactBytes(Death, FactBytes);
    Terms(FactBytes, Receipt.Fingerprint);
    auto Finish = [&Receipt](ELDRewardOutcome Outcome, ELDGrowthReason Reason)
    {
        Receipt.Outcome = Outcome;
        Receipt.Reason = Reason;
        return Receipt;
    };

    ALDGameMode* M = Mode();
    if (!M || !M->Development || !M->Combat || !Content
        || M->ValidateWritePermit(Permit, ELDCommitPhase::DeathsAndRewards) != ELDCommitReject::None
        || Permit.GetBoundary().Match != Match)
    {
        return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::NotAuthority);
    }
    if (!Death.Key.IsValid() || Death.Key.LedgerMatch != Match.LedgerMatch)
    {
        return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::OldMatch);
    }
    const TPair<uint8, FGuid> Victim(uint8(Death.Key.Victim.Kind), Death.Key.Victim.EntityId);
    if (const uint32* Through = ResolvedLifeThrough.Find(Victim); Through && Death.Key.Victim.Life <= *Through)
    {
        const FLDRewardReceipt* Previous = RecentRewards.FindByPredicate(
            [&](const FLDRewardReceipt& Row) { return Row.Key == Death.Key; });
        if (!Previous)
        {
            return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::StaleDeath);
        }
        if (FMemory::Memcmp(Previous->Fingerprint, Receipt.Fingerprint, sizeof(Receipt.Fingerprint)) != 0)
        {
            return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::DeathConflict);
        }
        return *Previous;
    }

    const FLDRewardBasis& Basis = Death.RewardBasis;
    const bool bContenderVictim = Death.Key.Victim.Kind == ELDVictimKind::Contender;
    if (!Basis.bCreditOwnerEligibleAtDeath || !Death.CreditOwner.IsValid())
    {
        // A locked fact without an eligible owner is a handled death with no payout, never a later retry.
        if (Basis.bCreditOwnerEligibleAtDeath || Basis.NoRewardReason == ELDNoRewardReason::None || !Death.Assisters.IsEmpty())
        {
            return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::DataInvalid);
        }
        Receipt.Recipient = Death.CreditOwner;
        Finish(ELDRewardOutcome::NoReward, GrowthNoRewardReason(Basis.NoRewardReason));
        RecordReward(Receipt);
        return Receipt;
    }
    const FGuid Recipient = Death.CreditOwner;
    if (Basis.NoRewardReason != ELDNoRewardReason::None || !M->Development->FindBuild(Recipient)
        || !M->Development->FindProgression(Recipient)
        || (bContenderVictim && (Recipient == Death.VictimContender || Recipient == Death.Key.Victim.EntityId)))
    {
        return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::DataInvalid);
    }
    // Assisters exist only for a contender victim: distinct contenders of this match other than the killer and victim.
    for (int32 Index = 0; Index < Death.Assisters.Num(); ++Index)
    {
        const FGuid& Assister = Death.Assisters[Index];
        if (!bContenderVictim || !Assister.IsValid() || Assister == Recipient || Assister == Death.Key.Victim.EntityId
            || (Index > 0 && !(Death.Assisters[Index - 1] < Assister))
            || !M->Development->FindProgression(Assister))
        {
            return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::DataInvalid);
        }
    }

    LDProgression::FRewardAmounts Amounts;
    const ELDGrowthReason AmountReason = bContenderVictim
        ? (Basis.RewardDefinitionId == FName(*Content->Get().Character.Id)
            ? LDProgression::ContenderKillReward(Content->Get().Parameters, uint8(Content->Get().Progression.MaxLevel),
                Basis.VictimLevelAtDeath, Amounts)
            : ELDGrowthReason::DataInvalid)
        : LDProgression::MonsterKillReward(*Content, Basis.RewardDefinitionId, Basis.CampStageAtDeath, Amounts);
    if (AmountReason != ELDGrowthReason::None)
    {
        return Finish(ELDRewardOutcome::PermanentlyRejected, AmountReason);
    }

    // The killer and the assisters split the kill XP evenly.
    FLDAssistRewardShares Split;
    if (!LDAssist::SplitKillReward(Amounts.XP, Death.Assisters.Num(), Split))
    {
        return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::ArithmeticOverflow);
    }
    TArray<FRewardShare> Shares;
    FRewardShare& Killer = Shares.AddDefaulted_GetRef();
    Killer.Recipient = Recipient;
    Killer.Amounts.XP = Split.KillerXP;
    for (const FGuid& Assister : Death.Assisters)
    {
        FRewardShare& Share = Shares.AddDefaulted_GetRef();
        Share.Recipient = Assister;
        Share.Amounts.XP = Split.AssisterXP;
    }
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const FString Detail = FString::Printf(TEXT("victim=%s victim_life=%u victim_kind=%s"), *LDLog::Id(Death.Key.Victim.EntityId),
        Death.Key.Victim.Life, bContenderVictim ? TEXT("contender") : TEXT("camp"));
    const ELDRewardOutcome Outcome = CommitRewardShares(Shares, TEXT("death"), Permit, Receipt, Reason, Detail);
    if (Outcome != ELDRewardOutcome::Granted)
    {
        return Finish(Outcome, Reason);
    }
    Finish(ELDRewardOutcome::Granted, ELDGrowthReason::None);
    RecordReward(Receipt);
    return Receipt;
}

ELDRewardOutcome ALDGrowthDirector::CommitFieldReward(const FGuid& Recipient, FName Event, uint64 Serial, int64 XP,
    ELDFieldRewardKind Kind, const FLDGameplayWritePermit& Permit, FLDRewardReceipt& Receipt, ELDGrowthReason& Reason)
{
    Reason = ELDGrowthReason::None;
    const bool bKnownKind = Kind == ELDFieldRewardKind::None || Kind == ELDFieldRewardKind::BuildDraw
        || Kind == ELDFieldRewardKind::Blueprint || Kind == ELDFieldRewardKind::ComponentChoice;
    if (!Recipient.IsValid() || Event.IsNone() || !Serial || XP < 0 || !bKnownKind || (Kind == ELDFieldRewardKind::None && XP == 0))
    {
        Reason = ELDGrowthReason::DataInvalid;
        return ELDRewardOutcome::PermanentlyRejected;
    }
    // The business identity is the event alone, so each event pays once whoever claims it.
    const TPair<FName, uint64> Key(Event, Serial);
    if (PaidEvents.Contains(Key))
    {
        Reason = ELDGrowthReason::FieldRewardDuplicate;
        return ELDRewardOutcome::PermanentlyRejected;
    }
    TArray<FRewardShare> Shares;
    FRewardShare& Share = Shares.AddDefaulted_GetRef();
    Share.Recipient = Recipient;
    Share.Amounts.XP = XP;
    Share.FieldKind = Kind;
    Share.Event = Event;
    Share.Serial = Serial;
    const ELDRewardOutcome Outcome = CommitRewardShares(Shares, TEXT("map_event"), Permit, Receipt, Reason,
        FString::Printf(TEXT("event=%s serial=%llu"), *Event.ToString(), Serial));
    if (Outcome == ELDRewardOutcome::Granted)
    {
        PaidEvents.Add(Key);
        UE_LOG(LogLDGrowth, Log, TEXT("LD_FIELD_REWARD t=%lld contender=%s event=%s serial=%llu xp=%lld result=%s definition=%s"),
            Permit.GetBoundary().AtMs, *LDLog::Id(Recipient), *Event.ToString(), Serial, XP, *LDLog::EnumName(Receipt.FieldResult),
            Receipt.FieldDefinition.IsNone() ? TEXT("-") : *Receipt.FieldDefinition.ToString());
    }
    return Outcome;
}

ELDRewardOutcome ALDGrowthDirector::CommitRewardParts(const FGuid& Recipient, int64 XP, const TCHAR* Cause,
    const FLDGameplayWritePermit& Permit, FLDRewardReceipt& Receipt, ELDGrowthReason& Reason, const FString& LogDetail)
{
    TArray<FRewardShare> Shares;
    FRewardShare& Share = Shares.AddDefaulted_GetRef();
    Share.Recipient = Recipient;
    Share.Amounts.XP = XP;
    return CommitRewardShares(Shares, Cause, Permit, Receipt, Reason, LogDetail);
}

ELDRewardOutcome ALDGrowthDirector::CommitRewardShares(const TArray<FRewardShare>& Shares, const TCHAR* Cause,
    const FLDGameplayWritePermit& Permit, FLDRewardReceipt& Receipt, ELDGrowthReason& Reason, const FString& LogDetail)
{
    Reason = ELDGrowthReason::None;
    ALDGameMode* M = Mode();
    if (!M || !M->Development || !M->Combat)
    {
        Reason = ELDGrowthReason::AuthorityFault;
        return ELDRewardOutcome::PermanentlyRejected;
    }
    TSet<FGuid> Seen;
    for (const FRewardShare& Share : Shares)
    {
        bool bDuplicate = false;
        Seen.Add(Share.Recipient, &bDuplicate);
        if (bDuplicate || Share.Amounts.XP < 0 || !M->Development->FindProgression(Share.Recipient))
        {
            Reason = ELDGrowthReason::DataInvalid;
            return ELDRewardOutcome::PermanentlyRejected;
        }
    }
    if (Shares.IsEmpty())
    {
        Reason = ELDGrowthReason::DataInvalid;
        return ELDRewardOutcome::PermanentlyRejected;
    }

    TArray<TOptional<FLDPreparedGrowthChange>> GrowthPlans;
    TArray<TOptional<FLDPreparedCombatProjection>> CombatPlans;
    for (const FRewardShare& Share : Shares)
    {
        FLDPrepareGrowthResult Growth =
#if WITH_DEV_AUTOMATION_TESTS
            !Share.DevLevelComponents.IsEmpty()
                ? M->Development->DevPrepareEvaluationExperience(Share.Recipient, Share.Amounts.XP, Share.DevLevelComponents)
                :
#endif
            M->Development->PrepareFieldReward(Share.Recipient, Share.Amounts.XP, Share.FieldKind, Share.Event, Share.Serial);
        if (!Growth.IsSuccess())
        {
            Reason = Growth.Reason;
            return ELDRewardOutcome::PermanentlyRejected;
        }
        FLDPreparedGrowthChange& GrowthPlan = Growth.Value.GetValue();
        TOptional<FLDPreparedCombatProjection>& CombatPlan = CombatPlans.AddDefaulted_GetRef();
        if (GrowthPlan.ChangesBuild())
        {
            FLDContenderLifeRead Life;
            FString LifeError;
            if (!M->ReadContenderLife(Match, Share.Recipient, Life, LifeError))
            {
                Reason = ELDGrowthReason::StaleContext;
                return ELDRewardOutcome::PermanentlyRejected;
            }
            // A recipient that left after the locked fatal hit is settled without a future body to project.
            if (Life.Phase != ELDLifePhase::Left)
            {
                FLDExpectedBuildVersions Expected;
                if (!M->Development->ReadCombatInstalled(Share.Recipient, Expected))
                {
                    Reason = ELDGrowthReason::NotContender;
                    return ELDRewardOutcome::PermanentlyRejected;
                }
                FLDCombatPrepareResult Projection = M->Combat->PrepareBuildChange(M->Development->MakeCaller(Share.Recipient),
                    M->Development->MakeProjectionInput(GrowthPlan.Build()), Expected);
                if (!Projection.IsSuccess())
                {
                    const ELDRewardOutcome Outcome = GrowthCombatOutcome(Projection.Error, Reason);
                    UE_LOG(LogLDGrowth, Warning, TEXT("LD_GROWTH_REWARD_PROJECTION %s %s"), LDGrowthWire::ReasonName(Reason), *Projection.Message);
                    return Outcome;
                }
                CombatPlan.Emplace(MoveTemp(Projection.Value.GetValue()));
                GrowthPlan.SetCombatInstall({GrowthPlan.Build().Revision});
            }
        }
        GrowthPlans.Emplace(MoveTemp(GrowthPlan));
    }
    if (M->ValidateWritePermit(Permit, Permit.GetPhase()) != ELDCommitReject::None)
    {
        Reason = ELDGrowthReason::NotAuthority;
        return ELDRewardOutcome::PermanentlyRejected;
    }

    // Every part of every recipient is prepared; the composite install below cannot fail.
    Receipt.Assists.Reset();
    for (int32 Index = 0; Index < Shares.Num(); ++Index)
    {
        const FRewardShare& Share = Shares[Index];
        const FLDPreparedGrowthChange& GrowthPlan = GrowthPlans[Index].GetValue();
        const FLDProgressionState* Progression = M->Development->FindProgression(Share.Recipient);
        if (Index == 0)
        {
            Receipt.Recipient = Share.Recipient;
            Receipt.XPDelta = Share.Amounts.XP;
            Receipt.LevelBefore = GrowthPlan.LevelBefore();
            Receipt.LevelAfter = GrowthPlan.Build().Level;
            Receipt.OpportunityFirst = uint8(Progression->ActiveEarned + 1);
            Receipt.OpportunityLast = GrowthPlan.Progression().ActiveEarned;
            Receipt.ComponentGrants = GrowthPlan.ComponentGrants();
            Receipt.FieldResult = GrowthPlan.FieldGrant().Result;
            Receipt.FieldDefinition = GrowthPlan.FieldGrant().Definition;
            Receipt.Revisions.bHasProgression = true;
            Receipt.Revisions.ProgressionRevision = GrowthPlan.Progression().Revision;
            Receipt.Revisions.bHasBuild = true;
            Receipt.Revisions.BuildRevision = GrowthPlan.Build().Revision;
            continue;
        }
        FLDRewardShareReceipt& Part = Receipt.Assists.AddDefaulted_GetRef();
        Part.Recipient = Share.Recipient;
        Part.XPDelta = Share.Amounts.XP;
        Part.LevelBefore = GrowthPlan.LevelBefore();
        Part.LevelAfter = GrowthPlan.Build().Level;
        Part.OpportunityFirst = uint8(Progression->ActiveEarned + 1);
        Part.OpportunityLast = GrowthPlan.Progression().ActiveEarned;
        Part.ComponentGrants = GrowthPlan.ComponentGrants();
    }
    for (int32 Index = 0; Index < Shares.Num(); ++Index)
    {
        M->Development->ApplyPreparedGrowth(MoveTemp(GrowthPlans[Index].GetValue()), Permit);
        if (CombatPlans[Index].IsSet())
        {
            M->Combat->ApplyBuildChange(MoveTemp(CombatPlans[Index].GetValue()), Permit);
        }
        MarkDirty(Shares[Index].Recipient);
    }
    // Host playtest record of every installed part (docs/DEVELOPMENT.md): who got what, why, and the levels it reached.
    const bool bDeath = FCString::Strcmp(Cause, TEXT("death")) == 0;
    for (int32 Index = 0; Index < Shares.Num(); ++Index)
    {
        const FRewardShare& Share = Shares[Index];
        const uint8 Before = Index == 0 ? Receipt.LevelBefore : Receipt.Assists[Index - 1].LevelBefore;
        const uint8 After = Index == 0 ? Receipt.LevelAfter : Receipt.Assists[Index - 1].LevelAfter;
        const uint8 First = Index == 0 ? Receipt.OpportunityFirst : Receipt.Assists[Index - 1].OpportunityFirst;
        const uint8 Last = Index == 0 ? Receipt.OpportunityLast : Receipt.Assists[Index - 1].OpportunityLast;
        const FLDProgressionState* Progression = M->Development->FindProgression(Share.Recipient);
        UE_LOG(LogLDGrowth, Log, TEXT("LD_REWARD t=%lld contender=%s cause=%s role=%s xp=%lld xp_after=%lld level=%u->%u %s"),
            Permit.GetBoundary().AtMs, *LDLog::Id(Share.Recipient), Cause,
            Index == 0 ? (Shares.Num() > 1 || bDeath ? TEXT("killer") : TEXT("recipient")) : TEXT("assist"),
            Share.Amounts.XP, Progression ? Progression->CumulativeXP : -1, uint32(Before), uint32(After), *LogDetail);
        if (After > Before)
        {
            int64 Unspent = 0;
            const FLDGrowthBuild* Build = M->Development->FindBuild(Share.Recipient);
            if (!Build || !Progression || LDProgression::UnspentPoints(Content->Get(), *Build, Progression->CumulativeXP,
                Progression->EmptyPoolGrants, Unspent) != ELDGrowthReason::None)
            {
                Unspent = -1;
            }
            // #198 B: the component of each level crossed; "-" marks a level whose exhausted pool paid points instead.
            const TArray<FLDComponentGrant>& Grants = Index == 0 ? Receipt.ComponentGrants : Receipt.Assists[Index - 1].ComponentGrants;
            const FString Components = FString::JoinBy(Grants, TEXT(","),
                [](const FLDComponentGrant& Grant) { return Grant.Component.IsNone() ? FString(TEXT("-")) : Grant.Component.ToString(); });
            const int32 EmptyPool = Grants.FilterByPredicate([](const FLDComponentGrant& Grant) { return Grant.Component.IsNone(); }).Num();
            UE_LOG(LogLDGrowth, Log, TEXT("LD_LEVEL_UP t=%lld contender=%s level=%u->%u choices_earned=%d unspent_points=%lld components=%s empty_pool_grants=%d"),
                Permit.GetBoundary().AtMs, *LDLog::Id(Share.Recipient), uint32(Before), uint32(After),
                Last >= First ? int32(Last) - int32(First) + 1 : 0, Unspent, Components.IsEmpty() ? TEXT("-") : *Components, EmptyPool);
        }
    }
    return ELDRewardOutcome::Granted;
}

bool ALDGrowthDirector::ProcessTransactions(const FLDGameplayWritePermit& Permit, FString& Error)
{
    Error.Reset();
    ALDGameMode* M = Mode();
    if (!M || !M->Development || !Content)
    {
        Error = TEXT("The growth transaction domain is not initialized for this match.");
        return false;
    }
    if (M->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || Permit.GetBoundary().Match != Match)
    {
        Error = TEXT("Growth transactions require the current Transactions permit.");
        return false;
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (!CommitDevGrants(Permit, Error))
    {
        return false;
    }
#endif
    TArray<FLDPendingGrowthIntent> Pending;
    M->Development->CollectPending(Pending);
#if WITH_DEV_AUTOMATION_TESTS
    for (const TPair<FGuid, FDevTestRecord>& Entry : DevTests)
    {
        if (const auto* DevPending = Entry.Value.Stream.GetPending())
        {
            FLDPendingGrowthIntent& Intent = Pending.AddDefaulted_GetRef();
            Intent.Kind = FLDPendingGrowthIntent::EKind::DevTest;
            Intent.Contender = Entry.Key;
            Intent.Arrival = DevPending->Arrival;
        }
    }
#endif
    Pending.Sort([](const FLDPendingGrowthIntent& A, const FLDPendingGrowthIntent& B) { return A.Arrival < B.Arrival; });
    for (const FLDPendingGrowthIntent& Intent : Pending)
    {
        switch (Intent.Kind)
        {
        case FLDPendingGrowthIntent::EKind::Build:
            if (GrowthIsFault(M->Development->CommitPendingBuild(Intent.Contender, Permit).Header.Reason))
            {
                Error = TEXT("A growth transaction lost its write permit inside the Transactions phase.");
                return false;
            }
            break;
#if WITH_DEV_AUTOMATION_TESTS
        case FLDPendingGrowthIntent::EKind::DevTest:
            if (!CommitPendingDevTest(Intent.Contender, Permit, Error))
            {
                return false;
            }
            break;
#endif
        }
    }
    return true;
}

bool ALDGrowthDirector::PublishGrowth(const FLDAuthorityBoundary& Boundary, FString& Error)
{
    Error.Reset();
    ALDGameMode* M = Mode();
    if (!M || !M->Development || !Content)
    {
        Error = TEXT("The growth domain is not initialized for this match.");
        return false;
    }
    if (Boundary.Match != Match)
    {
        Error = TEXT("Growth publication received a boundary of another match.");
        return false;
    }
    if (Dirty.IsEmpty())
    {
        return true;
    }
    TMap<FGuid, ALDPlayerController*> Owners;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (ALDPlayerController* Controller = Cast<ALDPlayerController>(It->Get()))
        {
            const FGuid Id = M->ContenderId(Controller);
            if (Id.IsValid())
            {
                Owners.Add(Id, Controller);
            }
        }
    }
    const TSet<FGuid> Changed = MoveTemp(Dirty);
    Dirty.Reset();
    for (const FGuid& Id : Changed)
    {
        uint64& Revision = PublishRevisions.FindOrAdd(Id);
        ++Revision;
        TArray<FLDBuildReceipt> BuildReceipts;
        M->Development->TakeFinalReceipts(Id, BuildReceipts);
#if WITH_DEV_AUTOMATION_TESTS
        TArray<FLDDevTestReceipt> DevReceipts;
        if (FDevTestRecord* DevRecord = DevTests.Find(Id))
        {
            DevReceipts = MoveTemp(DevRecord->Unpublished);
            DevRecord->Unpublished.Reset();
        }
#endif
        ALDPlayerController* const* OwnerController = Owners.Find(Id);
        if (!OwnerController)
        {
            // Server AI reads the same results through the query entries.
            continue;
        }
        M->SyncOwner(*OwnerController);
#if WITH_DEV_AUTOMATION_TESTS
        // Final DEV receipts follow the owner refresh like the build receipts below.
        for (const FLDDevTestReceipt& DevReceipt : DevReceipts)
        {
            FLDDevTestResult Result;
            Result.Generation = DevReceipt.Header.Generation;
            Result.RequestId = DevReceipt.Header.RequestId;
            Result.Status = ELDAdmissionStatus::Final;
            Result.Reason = DevReceipt.Header.Reason;
            Result.bSequenceReserved = true;
            Result.bHasReceipt = true;
            Result.Receipt = DevReceipt;
            (*OwnerController)->ClientDevTestResult(Result);
        }
#endif
        for (const FLDBuildReceipt& Build : BuildReceipts)
        {
            FLDBuildReceiptQueryResult Result;
            Result.Query.Generation = Build.Header.Generation;
            Result.Query.Family = ELDGrowthFamily::Build;
            Result.Query.RequestId = Build.Header.RequestId;
            Result.Lookup = ELDReceiptLookup::Final;
            Result.bHasReceipt = true;
            Result.Receipt = Build;
            (*OwnerController)->ClientBuildReceipt(Result);
        }
    }
    return true;
}

FLDGrowthOwnerView ALDGrowthDirector::ReadOwnerView(const FGuid& ContenderId) const
{
    FLDGrowthOwnerView View;
    ALDGameMode* M = Mode();
    if (!M || !M->Development || !Content)
    {
        return View;
    }
    View.Generation = Match.Generation;
    if (const uint64* Revision = PublishRevisions.Find(ContenderId))
    {
        View.PublishRevision = *Revision;
    }
    View.Progression = M->Development->ReadProgression(ContenderId);
    const FLDAirdropState& Drop = M->GetAirdrop();
    View.AirdropClaimedSerial = Drop.Phase == ELDAirdropPhase::Claimed && Drop.ClaimedBy == ContenderId ? Drop.Serial : 0;
    return View;
}

bool ALDGrowthDirector::ReadFinalLevel(const FGuid& ContenderId, uint8& Level, FString& Error) const
{
    Level = 0;
    ALDGameMode* M = Mode();
    const FLDGrowthBuild* Build = M && M->Development ? M->Development->FindBuild(ContenderId) : nullptr;
    if (!Build)
    {
        Error = TEXT("The contender has no growth record in this match.");
        return false;
    }
    Level = Build->Level;
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
uint64 ALDGrowthDirector::DevGrantReward(const FGuid& ContenderId, int64 XP, FString& Error)
{
    if (!LDDevGrowthFixture::Admit(GetWorld(), Error))
    {
        return 0;
    }
    ALDGameMode* M = Mode();
    if (!M || !M->Development || !Content || M->GetMatchContext() != Match || !M->IsGameplayWriteOpen(Match))
    {
        Error = TEXT("A DEV grant needs the current open match.");
        return 0;
    }
    if (!M->Development->FindBuild(ContenderId))
    {
        Error = TEXT("The DEV grant recipient is not a contender of this match.");
        return 0;
    }
    if (XP <= 0)
    {
        Error = FString::Printf(TEXT("A DEV grant needs positive XP (xp=%lld)."), XP);
        return 0;
    }
    FDevGrant& Grant = DevGrants.AddDefaulted_GetRef();
    Grant.Ticket = uint64(DevGrants.Num());
    Grant.Contender = ContenderId;
    Grant.XP = XP;
    UE_LOG(LogLDGrowth, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE queued grant ticket=%llu contender=%s xp=%lld"), Grant.Ticket,
        *ContenderId.ToString(), XP);
    return Grant.Ticket;
}

uint64 ALDGrowthDirector::DevGrantEvaluationReward(const FGuid& ContenderId, int64 XP, const TArray<FName>& LevelComponents,
    FString& Error)
{
    if (!LDDevGrowthFixture::Admit(GetWorld(), Error)) { return 0; }
    const FLDEvaluationSeeds* Seeds = LDEvaluationSeeds::Armed();
    if (LevelComponents.IsEmpty() || !GrowthEvaluationAI(Seeds, ContenderId))
    {
        Error = TEXT("A component preset needs a nonempty grant list and an armed evaluation AI slot.");
        return 0;
    }
    const uint64 Ticket = DevGrantReward(ContenderId, XP, Error);
    if (!Ticket) { return 0; }
    DevGrants[int32(Ticket - 1)].LevelComponents = LevelComponents;
    const ALDGameMode* M = Mode();
    UE_LOG(LogLDGrowth, Display, TEXT("LD_EVAL_COMPONENT_PRESET t=%lld contender=%s ticket=%llu components=%s"),
        M ? M->GetAuthorityNowMs() : -1, *LDLog::Id(ContenderId), Ticket,
        *FString::JoinBy(LevelComponents, TEXT(","), [](FName Id) { return Id.ToString(); }));
    return Ticket;
}

ELDDevGrantStatus ALDGrowthDirector::DevGrantStatus(uint64 Ticket, FString& Detail) const
{
    Detail.Reset();
    if (Ticket == 0 || Ticket > uint64(DevGrants.Num()))
    {
        Detail = TEXT("No DEV grant with this ticket in the current match.");
        return ELDDevGrantStatus::Unknown;
    }
    const FDevGrant& Grant = DevGrants[int32(Ticket - 1)];
    Detail = Grant.Detail;
    return Grant.Status;
}

bool ALDGrowthDirector::CommitDevGrants(const FLDGameplayWritePermit& Permit, FString& Error)
{
    for (FDevGrant& Grant : DevGrants)
    {
        if (Grant.Status != ELDDevGrantStatus::Pending)
        {
            continue;
        }
        FLDRewardReceipt Receipt;
        ELDGrowthReason Reason = ELDGrowthReason::None;
        ELDRewardOutcome Outcome;
        if (Grant.LevelComponents.IsEmpty())
        {
            Outcome = CommitRewardParts(Grant.Contender, Grant.XP, TEXT("dev_fixture"), Permit, Receipt, Reason);
        }
        else
        {
            const FLDEvaluationSeeds* Seeds = LDEvaluationSeeds::Armed();
            if (!GrowthEvaluationAI(Seeds, Grant.Contender))
            {
                Grant.Status = ELDDevGrantStatus::Rejected;
                Grant.Detail = TEXT("The armed evaluation AI slot no longer matches the component preset.");
                continue;
            }
            TArray<FRewardShare> Shares;
            FRewardShare& Share = Shares.AddDefaulted_GetRef();
            Share.Recipient = Grant.Contender;
            Share.Amounts.XP = Grant.XP;
            Share.DevLevelComponents = Grant.LevelComponents;
            Outcome = CommitRewardShares(Shares, TEXT("dev_fixture"), Permit, Receipt, Reason);
        }
        if (Reason == ELDGrowthReason::AuthorityFault)
        {
            Error = TEXT("A DEV growth grant lost the growth domain inside the Transactions phase.");
            return false;
        }
        if (Outcome == ELDRewardOutcome::RetryableNotCommitted)
        {
            // Nothing was installed; the grant stays pending for the next Transactions phase, like a retried death reward.
            continue;
        }
        if (Outcome == ELDRewardOutcome::Granted)
        {
            Grant.Status = ELDDevGrantStatus::Granted;
            Grant.Detail = FString::Printf(TEXT("level %u->%u, opportunities %u..%u, xp +%lld"),
                uint32(Receipt.LevelBefore), uint32(Receipt.LevelAfter), uint32(Receipt.OpportunityFirst),
                uint32(Receipt.OpportunityLast), Receipt.XPDelta);
        }
        else
        {
            Grant.Status = ELDDevGrantStatus::Rejected;
            Grant.Detail = FString::Printf(TEXT("outcome %u: %s"), uint32(Outcome), LDGrowthWire::ReasonName(Reason));
        }
        UE_LOG(LogLDGrowth, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE grant ticket=%llu contender=%s %s"), Grant.Ticket,
            *Grant.Contender.ToString(), *Grant.Detail);
    }
    return true;
}

FLDDevTestResult ALDGrowthDirector::QueueDevTest(const FGuid& ContenderId, const FLDDevTestCommand& Command, bool bHostConnection)
{
    FString Error;
    if (!LDDevGrowthFixture::Admit(GetWorld(), Error))
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::NotAuthority, Error);
    }
    return AdmitDevTest(ContenderId, Command, bHostConnection);
}

FLDDevTestResult ALDGrowthDirector::AdmitDevTest(const FGuid& ContenderId, const FLDDevTestCommand& Command, bool bHostConnection)
{
    const ELDGrowthReason Shape = LDDevTest::ValidateShape(Command);
    if (Shape != ELDGrowthReason::None)
    {
        return LDDevTest::Refused(Command, Shape, FString::Printf(
            TEXT("Op %u with amount %lld is not a DEV test command: grants need a positive amount, the other ops none."),
            uint32(Command.Op), Command.Amount));
    }
    if (LDDevTest::IsMatchWide(Command.Op) && !bHostConnection)
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::NotAuthority,
            TEXT("Camp refreshes and an early capture opening change the match for everyone; only the Listen Host may send them."));
    }
    ALDGameMode* M = Mode();
    if (!M || !M->Development || !Content)
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::NotAuthority, TEXT("The growth domain of this match is not initialized."));
    }
    if (!Match.IsValid() || Command.Generation != Match.Generation)
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::OldMatch, TEXT("The command names another match."));
    }
    if (!M->Development->FindBuild(ContenderId))
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::NotContender, TEXT("The caller is not a contender of this match."));
    }
    FDevTestRecord& Record = DevTests.FindOrAdd(ContenderId);
    // As for build commands: only a request that opens the next sequence takes an arrival number and needs an open match;
    // replays of an accepted or final request still answer from the stream.
    const bool bOpensSequence = !Record.Stream.GetPending() && Command.RequestId == Record.Stream.GetFinalizedThrough() + 1;
    if (bOpensSequence && !M->IsGameplayWriteOpen(Match))
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::MatchClosed, TEXT("The match no longer accepts gameplay writes."));
    }
    FLDPendingReceiptInfo Info;
    Info.OriginalLife = Command.Life;
    const uint64 Arrival = bOpensSequence ? NextArrival() : 0;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const FLDDevTestReceipt* Final = nullptr;
    bool bReserved = false;
    FLDDevTestResult Result;
    Result.Generation = Command.Generation;
    Result.RequestId = Command.RequestId;
    Result.Status = Record.Stream.Admit(Command, Command.RequestId, Info, Arrival, Reason, Final, bReserved);
    Result.Reason = LDGrowthWire::Raw(Reason);
    Result.bSequenceReserved = bReserved;
    if (Result.Status == ELDAdmissionStatus::Final && Final)
    {
        Result.bHasReceipt = true;
        Result.Receipt = *Final;
    }
    else if (Result.Status == ELDAdmissionStatus::AdmissionRejected)
    {
        Result.Detail = LDGrowthWire::ReasonName(Reason);
    }
    else if (bOpensSequence)
    {
        UE_LOG(LogLDGrowth, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE queued test command contender=%s request=%llu op=%s amount=%lld"),
            *ContenderId.ToString(), Command.RequestId, LDDevTest::OpName(Command.Op), Command.Amount);
    }
    return Result;
}

bool ALDGrowthDirector::CommitPendingDevTest(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit, FString& Error)
{
    FDevTestRecord* Record = DevTests.Find(ContenderId);
    const auto* PendingEntry = Record ? Record->Stream.GetPending() : nullptr;
    ALDGameMode* M = Mode();
    if (!PendingEntry || !M || !M->Combat || !M->Development
        || M->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None
        || Permit.GetBoundary().Match != Match)
    {
        Error = TEXT("A DEV test command commit needs its pending command, the growth domain and the current Transactions permit.");
        return false;
    }
    // A copy: finalizing the stream releases the pending entry.
    const FLDDevTestCommand Command = PendingEntry->Command;
    FLDDevTestReceipt Receipt;
    Receipt.Header.Generation = Command.Generation;
    Receipt.Header.RequestId = Command.RequestId;
    Receipt.Header.OriginalLife = Command.Life;
    Receipt.Header.CommitBatch = Permit.GetBoundary().BatchSerial;
    Receipt.Op = Command.Op;
    Receipt.Amount = Command.Amount;
    Receipt.Slot = Command.Slot;
    Receipt.Stat = Command.Stat;
    // The shape was proven at admission: a blueprint, learn or allocation names a valid definition, every other op none.
    FName Definition;
    Command.Definition.Decode(Definition);
    Receipt.Definition = Definition;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    FString Detail;
    FLDContenderLifeRead Life;
    FString LifeError;
    if (Command.Generation != Match.Generation)
    {
        Reason = ELDGrowthReason::OldMatch;
    }
    else if (!M->ReadContenderLife(Match, ContenderId, Life, LifeError))
    {
        Reason = ELDGrowthReason::StaleContext;
        Detail = LifeError;
    }
    else if (Life.Phase == ELDLifePhase::Left)
    {
        Reason = ELDGrowthReason::LeftMatch;
    }
    else if (Command.Life != Life.Life)
    {
        // A command of an earlier life never applies to the next one.
        Reason = ELDGrowthReason::OldLife;
    }
    else
    {
        switch (Command.Op)
        {
        case ELDDevTestOp::GrantExperience:
        {
            FLDRewardReceipt Reward;
            const ELDRewardOutcome Outcome = CommitRewardParts(ContenderId, Command.Amount, TEXT("dev_fixture"), Permit, Reward,
                Reason);
            if (Reason == ELDGrowthReason::AuthorityFault)
            {
                Error = TEXT("A DEV test grant lost the growth domain inside the Transactions phase.");
                return false;
            }
            if (Outcome == ELDRewardOutcome::RetryableNotCommitted)
            {
                // Nothing was installed; the command stays pending for the next Transactions phase.
                UE_LOG(LogLDGrowth, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE test command contender=%s request=%llu retries: %s"),
                    *ContenderId.ToString(), Command.RequestId, LDGrowthWire::ReasonName(Reason));
                return true;
            }
            if (Outcome == ELDRewardOutcome::Granted)
            {
                Reason = ELDGrowthReason::None;
                Detail = FString::Printf(TEXT("level %u->%u, xp +%lld"), uint32(Reward.LevelBefore), uint32(Reward.LevelAfter),
                    Reward.XPDelta);
            }
            else if (Reason == ELDGrowthReason::None)
            {
                Reason = ELDGrowthReason::DataInvalid;
            }
            break;
        }
        case ELDDevTestOp::RefillResources:
            if (Life.Phase != ELDLifePhase::Alive)
            {
                Reason = ELDGrowthReason::Dead;
            }
            else if (!M->Combat->DevRefillResources(ContenderId, Permit, Detail))
            {
                Reason = ELDGrowthReason::DataInvalid;
            }
            break;
        case ELDDevTestOp::ResetCooldowns:
            if (!M->Combat->DevResetCooldowns(ContenderId, Permit, Detail))
            {
                Reason = ELDGrowthReason::DataInvalid;
            }
            break;
        case ELDDevTestOp::GrantBlueprint:
        case ELDDevTestOp::LearnActive:
        case ELDDevTestOp::AllocatePoints:
            // Like a build command: Alive or Dead, without the build safe window.
            if (!LDGrowthWire::IsBuildLifePhase(Life.Phase))
            {
                Reason = ELDGrowthReason::LifePhaseChanged;
            }
            else if (Command.Op == ELDDevTestOp::GrantBlueprint)
            {
                Reason = M->Development->DevGrantBlueprint(ContenderId, Definition, Permit, Detail);
            }
            else if (Command.Op == ELDDevTestOp::LearnActive)
            {
                Reason = M->Development->DevLearnActive(ContenderId, Definition, Command.Slot, Permit, Detail);
            }
            else
            {
                Reason = M->Development->DevAllocatePoints(ContenderId, Definition, Command.Amount, Permit, Detail);
            }
            break;
        case ELDDevTestOp::ResetToMatchStart:
            // Like a build command: Alive or Dead; before the first life there is nothing to reset in Combat.
            Reason = LDGrowthWire::IsBuildLifePhase(Life.Phase)
                ? CommitDevReset(ContenderId, Permit, Detail)
                : ELDGrowthReason::LifePhaseChanged;
            break;
        case ELDDevTestOp::RefreshCamps:
        {
            int32 Waiting = 0;
            if (!M->Camps)
            {
                Reason = ELDGrowthReason::DataNotReady;
                Detail = TEXT("This match has no camp director.");
            }
            else if (!M->Camps->DevRefreshCleared(Permit, Waiting, Detail))
            {
                Reason = ELDGrowthReason::AuthorityFault;
            }
            else
            {
                Detail = FString::Printf(TEXT("%d cleared camps refresh at the next boundary"), Waiting);
            }
            break;
        }
        case ELDDevTestOp::OpenCapture:
        {
            bool bOutsideDevelopment = false;
            if (!M->DevOpenCaptureNow(Permit, bOutsideDevelopment, Detail))
            {
                Reason = bOutsideDevelopment ? ELDGrowthReason::StaleContext : ELDGrowthReason::DataInvalid;
            }
            break;
        }
        case ELDDevTestOp::SetAttribute:
        case ELDDevTestOp::ClearAttributes:
            // A persistent build term: Combat installs it on the living body or keeps it for the next life.
            Reason = M->Combat->DevSetAttribute(ContenderId, Command.Op == ELDDevTestOp::ClearAttributes,
                ELDDemoBonusStat(Command.Stat), Command.Amount, Permit, Detail);
            break;
        default:
            Reason = ELDGrowthReason::InvalidRequest;
            break;
        }
    }
    if (Reason == ELDGrowthReason::AuthorityFault)
    {
        Error = FString::Printf(TEXT("A DEV test command lost its growth domain or permit inside the Transactions phase: %s"), *Detail);
        return false;
    }
    Receipt.Header.Outcome = Reason == ELDGrowthReason::None ? ELDReceiptOutcome::Applied : ELDReceiptOutcome::Rejected;
    Receipt.Header.Reason = LDGrowthWire::Raw(Reason);
    Receipt.Detail = Reason == ELDGrowthReason::None || !Detail.IsEmpty() ? Detail : FString(LDGrowthWire::ReasonName(Reason));
    Record->Stream.Finalize(Receipt);
    Record->Unpublished.Add(Receipt);
    MarkDirty(ContenderId);
    UE_LOG(LogLDGrowth, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE test command contender=%s request=%llu op=%s amount=%lld %s: %s"),
        *ContenderId.ToString(), Command.RequestId, LDDevTest::OpName(Command.Op), Command.Amount,
        Reason == ELDGrowthReason::None ? TEXT("applied") : TEXT("rejected"), *Receipt.Detail);
    return true;
}

ELDGrowthReason ALDGrowthDirector::CommitDevReset(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit, FString& Detail)
{
    ALDGameMode* M = Mode();
    if (!M || !M->Development || !M->Combat || !Content.IsValid())
    {
        Detail = TEXT("A DEV reset needs the growth domain.");
        return ELDGrowthReason::AuthorityFault;
    }
    const uint8 LevelBefore = M->Development->FindBuild(ContenderId) ? M->Development->FindBuild(ContenderId)->Level : 0;
    FLDPrepareGrowthResult Growth = M->Development->DevPrepareReset(ContenderId);
    if (!Growth.IsSuccess())
    {
        Detail = Growth.Message;
        return Growth.Reason != ELDGrowthReason::None ? Growth.Reason : ELDGrowthReason::PrepareUnavailable;
    }
    const uint64 ResultBuildRevision = Growth.Value->Build().Revision;
    FLDCombatPrepareResult Combat = M->Combat->DevPrepareReset(ContenderId, ResultBuildRevision);
    if (!Combat.IsSuccess())
    {
        Detail = Combat.Message;
        return ELDGrowthReason::DataInvalid;
    }
    if (M->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None)
    {
        Detail = TEXT("The Transactions permit expired inside the reset.");
        return ELDGrowthReason::AuthorityFault;
    }
    Growth.Value->SetCombatInstall({ResultBuildRevision});
    M->Development->ApplyPreparedGrowth(MoveTemp(Growth.Value.GetValue()), Permit);
    M->Combat->ApplyBuildChange(MoveTemp(Combat.Value.GetValue()), Permit);
    const int32 Dropped = M->Combat->DevFinishReset(ContenderId, Permit.GetBoundary().AtMs);
    MarkDirty(ContenderId);
    Detail = FString::Printf(TEXT("level %u->1, %d cooldown tracks dropped"), uint32(LevelBefore), Dropped);
    return ELDGrowthReason::None;
}
#endif
