#include "Development/LDDevelopmentDirector.h"

#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevGrowthFixture.h"
#include "Economy/LDEconomyDirector.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Inventory/LDInventoryDirector.h"

namespace
{
bool DevelopmentChoiceShapeValid(const FLDChoiceCommand& Command, int32 MaxSkillRank)
{
    if (Command.Generation == 0 || Command.Life == 0 || Command.RequestId == 0 || !Command.OfferId.IsValid()
        || !Command.OptionId.IsValid() || Command.ExpectedBuildRevision == 0 || Command.OfferRevision == 0
        || !LDGrowthWire::IsChoicePhase(Command.ExpectedLifePhase))
    {
        return false;
    }
    if (!Command.bReplace)
    {
        return Command.ReplaceSlot == 0 && Command.ExpectedOldSkillId == FLDDefinitionWire() && Command.ExpectedOldRank == 0
            && !Command.bReplaceConfirmed;
    }
    FName OldSkillId;
    return Command.ReplaceSlot <= 1 && Command.ExpectedOldSkillId.Decode(OldSkillId) && Command.ExpectedOldRank >= 1
        && int32(Command.ExpectedOldRank) <= MaxSkillRank && Command.bReplaceConfirmed;
}

void DevelopmentFillHeader(FLDGrowthReceiptHeader& Header, const FLDChoiceCommand& Command, const FLDPendingReceiptInfo& Info,
    ELDReceiptOutcome Outcome, ELDGrowthReason Reason, const FLDGameplayWritePermit* Permit)
{
    Header.Generation = Command.Generation;
    Header.Family = ELDGrowthFamily::Choice;
    Header.RequestId = Command.RequestId;
    Header.OriginalLife = Info.OriginalLife;
    Header.OriginalLifePhase = Info.OriginalLifePhase;
    Header.Outcome = Outcome;
    Header.Reason = LDGrowthWire::Raw(Reason);
    Header.bHasCommitBatch = Permit != nullptr;
    Header.CommitBatch = Permit ? Permit->GetBoundary().BatchSerial : 0;
}

// Neither finalized nor applied: the Transactions dispatcher faults the authority boundary.
FLDChoiceReceipt DevelopmentFaultReceipt(const FLDChoiceCommand& Command, const FLDPendingReceiptInfo& Info)
{
    FLDChoiceReceipt Receipt;
    DevelopmentFillHeader(Receipt.Header, Command, Info, ELDReceiptOutcome::Rejected, ELDGrowthReason::AuthorityFault, nullptr);
    Receipt.OfferId = Command.OfferId;
    Receipt.OptionId = Command.OptionId;
    return Receipt;
}

ELDGrowthReason DevelopmentCombatReason(ELDCombatPrepareError Error)
{
    switch (Error)
    {
    case ELDCombatPrepareError::NumericUnderflow: return ELDGrowthReason::NumericUnderflow;
    case ELDCombatPrepareError::NumericDomain: return ELDGrowthReason::NumericDomain;
    case ELDCombatPrepareError::StaleBuild: return ELDGrowthReason::BuildStale;
    case ELDCombatPrepareError::UnknownContender: return ELDGrowthReason::NotContender;
    case ELDCombatPrepareError::CapacityExceeded: return ELDGrowthReason::CapacityExceeded;
    case ELDCombatPrepareError::InvalidInput:
    case ELDCombatPrepareError::Catalog: return ELDGrowthReason::DataInvalid;
    case ELDCombatPrepareError::NotAuthority:
    case ELDCombatPrepareError::StaleMatch: return ELDGrowthReason::MatchClosed;
    case ELDCombatPrepareError::None: break;
    }
    // A failed prepare without a typed error is a broken Combat contract, never a success.
    return ELDGrowthReason::PrepareUnavailable;
}

// PEE 6.2: the offer keeps its three cards; a newer build re-validates them and only moves the guard.
ELDGrowthReason DevelopmentRefreshOfferGuard(const FLDDemoContentSnapshot& Catalog, const FLDGrowthBuild& NewBuild,
    FLDProgressionState& Progression)
{
    if (!Progression.Offer.IsSet())
    {
        return ELDGrowthReason::None;
    }
    FLDChoiceOfferState& Offer = Progression.Offer.GetValue();
    for (const FLDChoiceOption& Option : Offer.Options)
    {
        if (!LDProgression::OptionStillLegal(Catalog, NewBuild, Option))
        {
            return ELDGrowthReason::DataInvalid;
        }
    }
    if (Progression.LastOfferRevision == MAX_uint64)
    {
        return ELDGrowthReason::ArithmeticOverflow;
    }
    ++Progression.LastOfferRevision;
    Offer.GuardBuildRevision = NewBuild.Revision;
    Offer.Revision = Progression.LastOfferRevision;
    return ELDGrowthReason::None;
}

FLDPrepareGrowthResult DevelopmentPrepareFailure(ELDGrowthReason Reason, const TCHAR* Message)
{
    FLDPrepareGrowthResult Result;
    Result.Reason = Reason;
    Result.Message = Message;
    return Result;
}
}

ALDDevelopmentDirector::ALDDevelopmentDirector()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
}

ALDGameMode* ALDDevelopmentDirector::Mode() const
{
    UWorld* World = GetWorld();
    return World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
}

bool ALDDevelopmentDirector::BeginMatch(const FLDMatchContext& InMatch, const TArray<FGuid>& Roster,
    TSharedPtr<const FLDDemoContentSnapshot> InContent, FString& Error)
{
    Error.Reset();
    if (!InMatch.IsValid() || !InContent.IsValid())
    {
        Error = TEXT("Growth needs the current match and the approved Demo catalog.");
        return false;
    }
    if (Roster.IsEmpty())
    {
        Error = TEXT("Growth needs at least one locked roster contender.");
        return false;
    }
    TSet<FGuid> Seen;
    for (const FGuid& Id : Roster)
    {
        bool bDuplicate = false;
        Seen.Add(Id, &bDuplicate);
        if (!Id.IsValid() || bDuplicate)
        {
            Error = TEXT("Growth roster contenders must be valid and distinct.");
            return false;
        }
    }
    const FLDDemoContentDefinitions& Definitions = InContent->Get();
    uint8 StartLevel = 0;
    const ELDGrowthReason LevelReason = LDProgression::LevelForXP(Definitions.Progression, 0, StartLevel);
    if (LevelReason != ELDGrowthReason::None || StartLevel != 1)
    {
        Error = FString::Printf(TEXT("Growth progression table is invalid (%s); zero XP must be level 1."),
            LDGrowthWire::ReasonName(LevelReason));
        return false;
    }
    // DrawThree multiplies the weight by the remaining pool; this bound keeps every later head draw no-fail.
    const int64 PoolBound = FMath::Max<int64>(
        static_cast<int64>(Definitions.Abilities.Num()) + static_cast<int64>(Definitions.Attributes.Num()), 1);
    if (Definitions.Parameters.ChoiceBaseWeight <= 0 || Definitions.Parameters.ChoiceBaseWeight > MAX_int64 / PoolBound)
    {
        Error = TEXT("CHOICE.BaseWeight must be a positive weight that fits the whole candidate pool.");
        return false;
    }
    if (Definitions.Progression.ActiveSlots != 2)
    {
        Error = TEXT("Growth requires exactly two active skill slots (Q/W).");
        return false;
    }
    TArray<LDProgression::FLegalOption> FirstPool;
    if (LDProgression::LegalPool(*InContent, FLDGrowthBuild(), FirstPool) != ELDGrowthReason::None || FirstPool.Num() < 3)
    {
        Error = TEXT("The approved catalog cannot offer three distinct choices to a level-1 build.");
        return false;
    }
    TMap<FGuid, FRecord> NewRecords;
    NewRecords.Reserve(Roster.Num());
    for (const FGuid& Id : Roster)
    {
        FRecord& Record = NewRecords.Add(Id);
        Record.Build.Level = 1;
        Record.Build.Revision = 1;
        Record.Progression.Revision = 1;
        // D10: every contender enters Development holding its one opening opportunity; respawns never add another.
        Record.Progression.EarnedThrough = 1;
        Record.CombatInstalled = FLDExpectedBuildVersions();
    }
    Match = InMatch;
    Content = MoveTemp(InContent);
    MatchSecret = FGuid::NewGuid();
    Records = MoveTemp(NewRecords);
#if WITH_DEV_AUTOMATION_TESTS
    // Presets staged in the Lobby serve this match's first heads, the opening offer included; nothing older survives.
    LDDevGrowthFixture::TakeStaged(Roster, DevOfferQueue);
#endif
    // The opening offer is drawn and kept now, exactly like a later queue head; closing, death or a retry never redraws it.
    for (const FGuid& Id : Roster)
    {
        DrawHead(Id, Records.FindChecked(Id), FirstPool);
    }
    return true;
}

FLDProgressionView ALDDevelopmentDirector::ReadProgression(const FGuid& ContenderId) const
{
    FLDProgressionView View;
    const FRecord* Record = Records.Find(ContenderId);
    if (!Record || !Match.IsValid() || !Content.IsValid())
    {
        return View;
    }
    const FLDDemoContentDefinitions& Definitions = Content->Get();
    const FLDDemoProgressionDefinition& Table = Definitions.Progression;
    const FLDGrowthBuild& Build = Record->Build;
    const FLDProgressionState& Progression = Record->Progression;
    View.bValid = true;
    View.Generation = Match.Generation;
    const ALDGameMode* GameMode = Mode();
    FLDContenderLifeRead LifeRead;
    FString LifeError;
    if (GameMode && GameMode->ReadContenderLife(Match, ContenderId, LifeRead, LifeError))
    {
        View.Life = LifeRead.Life;
        View.LifePhase = LifeRead.Phase;
        View.bCanChoose = Progression.Offer.IsSet() && LDGrowthWire::IsChoicePhase(LifeRead.Phase)
            && GameMode->IsGameplayWriteOpen(Match);
    }
    View.Level = Build.Level;
    View.MaxLevel = static_cast<uint8>(Table.MaxLevel);
    View.CumulativeXP = Progression.CumulativeXP;
    View.LevelThresholdXP = Table.XPThresholds[Build.Level - 1];
    if (Build.Level < Table.MaxLevel)
    {
        View.bHasNextThreshold = true;
        View.NextThresholdXP = Table.XPThresholds[Build.Level];
    }
    View.ProgressionRevision = Progression.Revision;
    View.BuildRevision = Build.Revision;
    View.EarnedThrough = Progression.EarnedThrough;
    View.ConsumedThrough = Progression.ConsumedThrough;
    View.PendingChoiceCount = Progression.PendingCount();
    if (Progression.Offer.IsSet())
    {
        const FLDChoiceOfferState& Offer = Progression.Offer.GetValue();
        View.bHasOffer = true;
        View.Offer.OfferId = Offer.OfferId;
        View.Offer.OpportunityOrdinal = Offer.Ordinal;
        View.Offer.OfferRevision = Offer.Revision;
        View.Offer.GuardBuildRevision = Offer.GuardBuildRevision;
        View.Offer.Options = Offer.Options;
    }
    View.Skills.Reserve(Build.Skills.Num());
    for (const FLDLearnedSkill& Skill : Build.Skills)
    {
        FLDLearnedSkillView& Row = View.Skills.AddDefaulted_GetRef();
        Row.DefinitionId = Skill.Id;
        Row.bActive = Skill.bActive;
        Row.Rank = Skill.Rank;
        Row.Slot = Skill.bActive ? Skill.Slot : uint8(0);
    }
    View.AttributeCards.Reserve(Build.Cards.Num());
    for (const FLDAttributeCardCount& Card : Build.Cards)
    {
        FLDAttributeCardView& Row = View.AttributeCards.AddDefaulted_GetRef();
        Row.DefinitionId = Card.Id;
        Row.Count = Card.Count;
    }
    View.CatalogDigest = Definitions.Identity.Root;
    return View;
}

FLDSubmitResult ALDDevelopmentDirector::QueueChoice(const FGuid& ContenderId, const FLDChoiceCommand& Command)
{
    FLDSubmitResult Result;
    Result.Generation = Command.Generation;
    Result.Family = ELDGrowthFamily::Choice;
    Result.RequestId = Command.RequestId;
    Result.Status = ELDAdmissionStatus::AdmissionRejected;
    const int32 MaxSkillRank = Content.IsValid() ? Content->Get().Progression.MaxSkillRank : MAX_uint8;
    if (!DevelopmentChoiceShapeValid(Command, MaxSkillRank))
    {
        Result.Reason = LDGrowthWire::Raw(ELDGrowthReason::InvalidRequest);
        return Result;
    }
    if (!Match.IsValid() || Command.Generation != Match.Generation)
    {
        Result.Reason = LDGrowthWire::Raw(ELDGrowthReason::OldMatch);
        return Result;
    }
    FRecord* Record = Records.Find(ContenderId);
    if (!Record)
    {
        Result.Reason = LDGrowthWire::Raw(ELDGrowthReason::NotContender);
        return Result;
    }
    ALDGameMode* GameMode = Mode();
    if (!GameMode || !GameMode->Economy)
    {
        UE_LOG(LogTemp, Error, TEXT("LD_GROWTH_CHOICE_ADMISSION_FAULT contender=%s: no authority mode or economy director"),
            *ContenderId.ToString());
        Result.Reason = LDGrowthWire::Raw(ELDGrowthReason::NotAuthority);
        return Result;
    }
    // Only a request that opens the next sequence takes a server arrival number; a closed match opens none,
    // while replays of an accepted or final request still answer from the stream.
    const bool bOpensSequence = !Record->Choices.GetPending()
        && Command.RequestId == Record->Choices.GetFinalizedThrough() + 1;
    if (bOpensSequence && !GameMode->IsGameplayWriteOpen(Match))
    {
        Result.Reason = LDGrowthWire::Raw(ELDGrowthReason::MatchClosed);
        return Result;
    }
    FLDPendingReceiptInfo Info;
    Info.OriginalLife = Command.Life;
    Info.OriginalLifePhase = Command.ExpectedLifePhase;
    const uint64 Arrival = bOpensSequence ? GameMode->Economy->NextArrival() : 0;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const FLDChoiceReceipt* Final = nullptr;
    bool bReserved = false;
    Result.Status = Record->Choices.Admit(Command, Command.RequestId, Info, Arrival, Reason, Final, bReserved);
    Result.Reason = LDGrowthWire::Raw(Reason);
    Result.bSequenceReserved = bReserved;
    if (Result.Status == ELDAdmissionStatus::Final && Final)
    {
        Result.bHasChoiceReceipt = true;
        Result.ChoiceReceipt = *Final;
    }
    return Result;
}

FLDChoiceReceiptQueryResult ALDDevelopmentDirector::QueryChoiceReceipt(const FGuid& ContenderId, const FLDReceiptQuery& Query) const
{
    FLDChoiceReceiptQueryResult Result;
    Result.Query = Query;
    const FRecord* Record = Records.Find(ContenderId);
    if (Query.Family != ELDGrowthFamily::Choice || !Match.IsValid() || Query.Generation != Match.Generation || !Record)
    {
        Result.Lookup = ELDReceiptLookup::InvalidContext;
        return Result;
    }
    const FLDChoiceReceipt* Final = nullptr;
    const FLDPendingReceiptInfo* PendingInfo = nullptr;
    Result.Lookup = Record->Choices.Lookup(Query.RequestId, Final, PendingInfo);
    if (Result.Lookup == ELDReceiptLookup::Pending && PendingInfo)
    {
        Result.bHasPending = true;
        Result.Pending = *PendingInfo;
    }
    else if (Result.Lookup == ELDReceiptLookup::Final && Final)
    {
        Result.bHasReceipt = true;
        Result.Receipt = *Final;
    }
    return Result;
}

void ALDDevelopmentDirector::CollectPending(TArray<FLDPendingGrowthIntent>& Out) const
{
    for (const auto& Pair : Records)
    {
        if (const auto* PendingEntry = Pair.Value.Choices.GetPending())
        {
            FLDPendingGrowthIntent& Intent = Out.AddDefaulted_GetRef();
            Intent.Kind = FLDPendingGrowthIntent::EKind::Choice;
            Intent.Contender = Pair.Key;
            Intent.Arrival = PendingEntry->Arrival;
        }
    }
}

FLDChoiceReceipt ALDDevelopmentDirector::CommitPendingChoice(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit)
{
    FRecord* Record = Records.Find(ContenderId);
    const auto* PendingEntry = Record ? Record->Choices.GetPending() : nullptr;
    if (!PendingEntry)
    {
        UE_LOG(LogTemp, Error, TEXT("LD_GROWTH_CHOICE_FAULT contender=%s: no pending choice to commit"), *ContenderId.ToString());
        FLDChoiceCommand Missing;
        Missing.Generation = Match.Generation;
        return DevelopmentFaultReceipt(Missing, FLDPendingReceiptInfo());
    }
    // Copies: finalizing the stream releases the pending entry.
    const FLDChoiceCommand Command = PendingEntry->Command;
    const FLDPendingReceiptInfo Info = PendingEntry->Info;
    ALDGameMode* GameMode = Mode();
    const ELDCommitReject PermitReject = GameMode
        ? GameMode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) : ELDCommitReject::NotAuthority;
    if (PermitReject != ELDCommitReject::None || !GameMode->Combat || !GameMode->Economy || !Content.IsValid()
        || Permit.GetBoundary().Match != Match)
    {
        UE_LOG(LogTemp, Error, TEXT("LD_GROWTH_CHOICE_FAULT contender=%s request=%llu permit=%d: no Transactions permit or domain"),
            *ContenderId.ToString(), Command.RequestId, int32(PermitReject));
        return DevelopmentFaultReceipt(Command, Info);
    }
    const auto Reject = [&](ELDGrowthReason RejectReason)
    {
        FLDChoiceReceipt RejectReceipt = RejectPending(*Record, Command, RejectReason, &Permit);
        GameMode->Economy->MarkDirty(ContenderId);
        return RejectReceipt;
    };

    if (Command.Generation != Match.Generation)
    {
        return Reject(ELDGrowthReason::OldMatch);
    }
    if (Command.CatalogDigest != Content->Get().Identity.Root)
    {
        return Reject(ELDGrowthReason::CatalogMismatch);
    }
    FLDContenderLifeRead LifeRead;
    FString LifeError;
    if (!GameMode->ReadContenderLife(Match, ContenderId, LifeRead, LifeError))
    {
        return Reject(ELDGrowthReason::StaleContext);
    }
    if (LifeRead.Phase == ELDLifePhase::Left)
    {
        return Reject(ELDGrowthReason::LeftMatch);
    }
    if (Command.Life != LifeRead.Life)
    {
        return Reject(ELDGrowthReason::OldLife);
    }
    if (LifeRead.Phase != Command.ExpectedLifePhase || !LDGrowthWire::IsChoicePhase(LifeRead.Phase))
    {
        return Reject(ELDGrowthReason::LifePhaseChanged);
    }
    const FLDGrowthBuild& Build = Record->Build;
    const FLDProgressionState& Progression = Record->Progression;
    const bool bOfferConsumed = Progression.Records.ContainsByPredicate(
        [&Command](const FLDChoiceRecord& Row) { return Row.OfferId == Command.OfferId; });
    if (!Progression.Offer.IsSet())
    {
        return Reject(bOfferConsumed ? ELDGrowthReason::OfferAlreadyCommitted : ELDGrowthReason::NoOffer);
    }
    const FLDChoiceOfferState& Offer = Progression.Offer.GetValue();
    if (Offer.OfferId != Command.OfferId)
    {
        return Reject(bOfferConsumed ? ELDGrowthReason::OfferAlreadyCommitted : ELDGrowthReason::UnknownOffer);
    }
    if (Command.OfferRevision != Offer.Revision || Command.ExpectedBuildRevision != Build.Revision)
    {
        return Reject(ELDGrowthReason::OfferGuardStale);
    }
    const FLDChoiceOption* Option = Offer.Options.FindByPredicate(
        [&Command](const FLDChoiceOption& Row) { return Row.OptionId == Command.OptionId; });
    if (!Option)
    {
        return Reject(ELDGrowthReason::InvalidOption);
    }
    // The single offer is always the head of a non-empty opportunity queue.
    if (Offer.Ordinal != Progression.ConsumedThrough + 1 || Progression.ConsumedThrough >= Progression.EarnedThrough)
    {
        return Reject(ELDGrowthReason::DataInvalid);
    }

    LDProgression::FReplacement Replacement;
    if (Command.bReplace)
    {
        if (!Command.ExpectedOldSkillId.Decode(Replacement.OldSkillId))
        {
            return Reject(ELDGrowthReason::InvalidRequest);
        }
        Replacement.Slot = Command.ReplaceSlot;
        Replacement.OldRank = Command.ExpectedOldRank;
        Replacement.bConfirmed = Command.bReplaceConfirmed;
    }
    FLDGrowthBuild NewBuild;
    LDProgression::FAppliedOption AppliedOption;
    const ELDGrowthReason OptionReason = LDProgression::ApplyOption(*Content, Build, *Option,
        Command.bReplace ? &Replacement : nullptr, NewBuild, AppliedOption);
    if (OptionReason != ELDGrowthReason::None)
    {
        return Reject(OptionReason);
    }
    if (Build.Revision == MAX_uint64 || Progression.Revision == MAX_uint64)
    {
        return Reject(ELDGrowthReason::ArithmeticOverflow);
    }
    NewBuild.Revision = Build.Revision + 1;
    FLDProgressionState NewProgression = Progression;
    NewProgression.ConsumedThrough = static_cast<uint8>(Progression.ConsumedThrough + 1);
    NewProgression.Revision = Progression.Revision + 1;
    NewProgression.Offer.Reset();
    FLDChoiceRecord& Consumed = NewProgression.Records.AddDefaulted_GetRef();
    Consumed.Ordinal = Offer.Ordinal;
    Consumed.OfferId = Offer.OfferId;
    Consumed.OptionId = Option->OptionId;
    Consumed.RequestId = Command.RequestId;

    // The next head is drawn only after this choice is installed; its pool is proven now.
    TArray<LDProgression::FLegalOption> NextPool;
    if (NewProgression.PendingCount() > 0)
    {
        const ELDGrowthReason PoolReason = LDProgression::LegalPool(*Content, NewBuild, NextPool);
        if (PoolReason != ELDGrowthReason::None || NextPool.Num() < 3)
        {
            return Reject(PoolReason != ELDGrowthReason::None ? PoolReason : ELDGrowthReason::DataInvalid);
        }
        if (NewProgression.LastOfferRevision == MAX_uint64)
        {
            return Reject(ELDGrowthReason::ArithmeticOverflow);
        }
    }

    const LDEconomy::FLedger& Ledger = GameMode->Economy->GetLedger();
    const uint64 InventoryRevision = Ledger.InventoryRevision(ContenderId);
    if (InventoryRevision == 0)
    {
        return Reject(ELDGrowthReason::NotContender);
    }
    TArray<FLDEquippedItemValue> Equipped;
    ALDInventoryDirector::EquippedItems(Ledger.State(), ContenderId, Equipped);
    FLDCombatPrepareResult Projection = GameMode->Combat->PrepareBuildChange(MakeCaller(ContenderId),
        MakeProjectionInput(NewBuild, Equipped, InventoryRevision), Record->CombatInstalled);
    if (!Projection.IsSuccess())
    {
        const ELDGrowthReason CombatReason = DevelopmentCombatReason(Projection.Error);
        if (CombatReason == ELDGrowthReason::DataInvalid || CombatReason == ELDGrowthReason::PrepareUnavailable)
        {
            UE_LOG(LogTemp, Warning, TEXT("LD_GROWTH_CHOICE_PROJECTION contender=%s %s: %s"), *ContenderId.ToString(),
                LDGrowthWire::ReasonName(CombatReason), *Projection.Message);
        }
        return Reject(CombatReason);
    }

    FLDChoiceReceipt Receipt;
    DevelopmentFillHeader(Receipt.Header, Command, Info, ELDReceiptOutcome::Applied, ELDGrowthReason::None, &Permit);
    Receipt.Header.Revisions.bHasProgression = true;
    Receipt.Header.Revisions.ProgressionRevision = NewProgression.Revision;
    Receipt.Header.Revisions.bHasBuild = true;
    Receipt.Header.Revisions.BuildRevision = NewBuild.Revision;
    Receipt.OfferId = Offer.OfferId;
    Receipt.OptionId = Option->OptionId;
    Receipt.OpportunityOrdinal = Offer.Ordinal;
    Receipt.ConsumedThrough = NewProgression.ConsumedThrough;
    Receipt.Kind = AppliedOption.Kind;
    Receipt.DefinitionId = AppliedOption.DefinitionId;
    Receipt.NewRank = AppliedOption.NewRank;
    if (AppliedOption.bReplaced)
    {
        Receipt.bReplaced = true;
        Receipt.ReplacedSlot = AppliedOption.Slot;
        Receipt.ReplacedSkillId = AppliedOption.ReplacedId;
        Receipt.ReplacedRank = AppliedOption.ReplacedRank;
    }
    if (GameMode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None)
    {
        UE_LOG(LogTemp, Error, TEXT("LD_GROWTH_CHOICE_FAULT contender=%s request=%llu: permit lost before apply"),
            *ContenderId.ToString(), Command.RequestId);
        return DevelopmentFaultReceipt(Command, Info);
    }

    // Everything is prepared and the permit re-checked; the install below cannot fail. Offer, Option and the
    // Build/Progression references must not be read after this point.
    FLDExpectedBuildVersions Installed;
    Installed.BuildRevision = NewBuild.Revision;
    Installed.InventoryRevision = InventoryRevision;
    GameMode->Combat->ApplyBuildChange(MoveTemp(Projection.Value.GetValue()), Permit);
    Record->Build = MoveTemp(NewBuild);
    Record->Progression = MoveTemp(NewProgression);
    Record->CombatInstalled = Installed;
    if (Record->Progression.PendingCount() > 0)
    {
        DrawHead(ContenderId, *Record, NextPool);
        Receipt.bHasNextOffer = true;
        Receipt.NextOfferId = Record->Progression.Offer->OfferId;
        Receipt.Header.Revisions.bHasOffer = true;
        Receipt.Header.Revisions.OfferRevision = Record->Progression.Offer->Revision;
    }
    Record->Choices.Finalize(Receipt);
    Record->Unpublished.Add(Receipt);
    GameMode->Economy->MarkDirty(ContenderId);
    return Receipt;
}

FLDChoiceReceipt ALDDevelopmentDirector::RejectPending(FRecord& Record, const FLDChoiceCommand& Command,
    ELDGrowthReason Reason, const FLDGameplayWritePermit* Permit)
{
    const auto* PendingEntry = Record.Choices.GetPending();
    checkf(PendingEntry && PendingEntry->Command.RequestId == Command.RequestId && Reason != ELDGrowthReason::None,
        TEXT("A choice rejection needs the pending request it finalizes and an explicit reason."));
    FLDChoiceReceipt Receipt;
    DevelopmentFillHeader(Receipt.Header, Command, PendingEntry->Info, ELDReceiptOutcome::Rejected, Reason, Permit);
    // Current guards only as a sync hint; nothing was committed.
    Receipt.Header.Revisions.bHasBuild = true;
    Receipt.Header.Revisions.BuildRevision = Record.Build.Revision;
    if (Record.Progression.Offer.IsSet())
    {
        Receipt.Header.Revisions.bHasOffer = true;
        Receipt.Header.Revisions.OfferRevision = Record.Progression.Offer->Revision;
    }
    Receipt.OfferId = Command.OfferId;
    Receipt.OptionId = Command.OptionId;
    Record.Choices.Finalize(Receipt);
    Record.Unpublished.Add(Receipt);
    return Receipt;
}

FLDPrepareGrowthResult ALDDevelopmentDirector::PrepareExperience(const FGuid& ContenderId, int64 GrantXP) const
{
    const FRecord* Record = Records.Find(ContenderId);
    if (!Record || !Content.IsValid())
    {
        return DevelopmentPrepareFailure(ELDGrowthReason::NotContender, TEXT("The XP recipient has no growth record in this match."));
    }
    LDProgression::FExperienceChange Change;
    const ELDGrowthReason PlanReason = LDProgression::PlanExperience(Content->Get().Progression,
        Record->Progression.CumulativeXP, Record->Build.Level, Record->Progression.EarnedThrough, GrantXP, Change);
    if (PlanReason != ELDGrowthReason::None)
    {
        return DevelopmentPrepareFailure(PlanReason, TEXT("The XP grant does not fit the contender's progression."));
    }
    FLDPreparedGrowthChange Prepared;
    Prepared.ContenderId = ContenderId;
    Prepared.ExpectedBuildRevision = Record->Build.Revision;
    Prepared.ExpectedProgressionRevision = Record->Progression.Revision;
    Prepared.OldLevel = Record->Build.Level;
    Prepared.NewBuild = Record->Build;
    Prepared.NewProgression = Record->Progression;
    FLDGrowthBuild& NewBuild = Prepared.NewBuild;
    FLDProgressionState& NewProgression = Prepared.NewProgression;
    // A zero grant changes nothing and advances no revision.
    if (GrantXP > 0)
    {
        if (NewProgression.Revision == MAX_uint64)
        {
            return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("The progression revision is exhausted."));
        }
        ++NewProgression.Revision;
        NewProgression.CumulativeXP = Change.NewXP;
        NewProgression.EarnedThrough = Change.EarnedThroughAfter;
    }
    if (Change.LevelAfter != Change.LevelBefore)
    {
        if (NewBuild.Revision == MAX_uint64)
        {
            return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("The build revision is exhausted."));
        }
        NewBuild.Level = Change.LevelAfter;
        ++NewBuild.Revision;
        const ELDGrowthReason GuardReason = DevelopmentRefreshOfferGuard(*Content, NewBuild, NewProgression);
        if (GuardReason != ELDGrowthReason::None)
        {
            return DevelopmentPrepareFailure(GuardReason, TEXT("The persistent offer cannot follow the new level."));
        }
    }
    if (!NewProgression.Offer.IsSet() && NewProgression.PendingCount() > 0)
    {
        const ELDGrowthReason PoolReason = LDProgression::LegalPool(*Content, NewBuild, Prepared.HeadPool);
        if (PoolReason != ELDGrowthReason::None || Prepared.HeadPool.Num() < 3)
        {
            return DevelopmentPrepareFailure(PoolReason != ELDGrowthReason::None ? PoolReason : ELDGrowthReason::DataInvalid,
                TEXT("The catalog cannot offer three distinct choices for the new opportunity."));
        }
        if (NewProgression.LastOfferRevision == MAX_uint64)
        {
            return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("The offer revision is exhausted."));
        }
        Prepared.bDrawHead = true;
    }
    FLDPrepareGrowthResult Result;
    Result.Value.Emplace(MoveTemp(Prepared));
    return Result;
}

FLDPrepareGrowthResult ALDDevelopmentDirector::PrepareEquipmentChange(const FGuid& ContenderId) const
{
    const FRecord* Record = Records.Find(ContenderId);
    if (!Record || !Content.IsValid())
    {
        return DevelopmentPrepareFailure(ELDGrowthReason::NotContender, TEXT("The equipment owner has no growth record in this match."));
    }
    if (Record->Build.Revision == MAX_uint64)
    {
        return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("The build revision is exhausted."));
    }
    FLDPreparedGrowthChange Prepared;
    Prepared.ContenderId = ContenderId;
    Prepared.ExpectedBuildRevision = Record->Build.Revision;
    Prepared.ExpectedProgressionRevision = Record->Progression.Revision;
    Prepared.OldLevel = Record->Build.Level;
    Prepared.NewBuild = Record->Build;
    Prepared.NewProgression = Record->Progression;
    ++Prepared.NewBuild.Revision;
    const ELDGrowthReason GuardReason = DevelopmentRefreshOfferGuard(*Content, Prepared.NewBuild, Prepared.NewProgression);
    if (GuardReason != ELDGrowthReason::None)
    {
        return DevelopmentPrepareFailure(GuardReason, TEXT("The persistent offer cannot follow the equipment change."));
    }
    FLDPrepareGrowthResult Result;
    Result.Value.Emplace(MoveTemp(Prepared));
    return Result;
}

void ALDDevelopmentDirector::ApplyPreparedGrowth(FLDPreparedGrowthChange&& Prepared, const FLDGameplayWritePermit& Permit)
{
    ALDGameMode* GameMode = Mode();
    checkf(GameMode && GameMode->Economy
        && (Permit.GetPhase() == ELDCommitPhase::Transactions || Permit.GetPhase() == ELDCommitPhase::DeathsAndRewards)
        && GameMode->ValidateWritePermit(Permit, Permit.GetPhase()) == ELDCommitReject::None,
        TEXT("Growth apply needs the current Transactions or DeathsAndRewards permit of one uninterrupted commit."));
    FRecord* Record = Records.Find(Prepared.ContenderId);
    checkf(Record && Record->Build.Revision == Prepared.ExpectedBuildRevision
        && Record->Progression.Revision == Prepared.ExpectedProgressionRevision,
        TEXT("Growth plan no longer matches the contender; prepare and apply must be one uninterrupted commit."));
    Record->Build = MoveTemp(Prepared.NewBuild);
    Record->Progression = MoveTemp(Prepared.NewProgression);
    if (Prepared.bCombatInstall)
    {
        Record->CombatInstalled = Prepared.CombatInstall;
    }
    if (Prepared.bDrawHead)
    {
        DrawHead(Prepared.ContenderId, *Record, Prepared.HeadPool);
    }
    GameMode->Economy->MarkDirty(Prepared.ContenderId);
}

void ALDDevelopmentDirector::DrawHead(const FGuid& ContenderId, FRecord& Record, const TArray<LDProgression::FLegalOption>& Pool)
{
    FLDProgressionState& Progression = Record.Progression;
    checkf(Content.IsValid() && !Progression.Offer.IsSet() && Progression.PendingCount() > 0
        && Progression.LastOfferRevision < MAX_uint64,
        TEXT("A head draw needs an empty offer slot over a non-empty opportunity queue."));
    const uint8 Ordinal = static_cast<uint8>(Progression.ConsumedThrough + 1);
    const int64 Weight = Content->Get().Parameters.ChoiceBaseWeight;
    const uint64 Seed = LDProgression::OfferSeed(MatchSecret, ContenderId, Ordinal);
    TArray<LDProgression::FLegalOption> Picked;
#if WITH_DEV_AUTOMATION_TESTS
    TArray<TArray<FName>>* Presets = DevOfferQueue.Find(ContenderId);
    TArray<TArray<FName>> NoPresets;
    bool bPreset = false;
    FString PresetError;
    const ELDGrowthReason DrawReason = LDDevGrowthFixture::PickHead(Pool, Weight, Seed, Presets ? *Presets : NoPresets, Picked,
        bPreset, PresetError);
    if (!PresetError.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEV_FIXTURE_PRESET_REFUSED contender=%s ordinal=%u, drawn normally: %s"),
            *ContenderId.ToString(), uint32(Ordinal), *PresetError);
    }
    else if (bPreset)
    {
        UE_LOG(LogTemp, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE preset offer contender=%s ordinal=%u cards=%s,%s,%s"),
            *ContenderId.ToString(), uint32(Ordinal), *Picked[0].DefinitionId.ToString(), *Picked[1].DefinitionId.ToString(),
            *Picked[2].DefinitionId.ToString());
    }
    if (Presets && Presets->IsEmpty())
    {
        DevOfferQueue.Remove(ContenderId);
    }
#else
    const ELDGrowthReason DrawReason = LDProgression::DrawThree(Pool, Weight, Seed, Picked);
#endif
    verifyf(DrawReason == ELDGrowthReason::None && Picked.Num() == 3, TEXT("The prepared head pool must yield three distinct options."));
    FLDChoiceOfferState Offer;
    Offer.OfferId = FGuid::NewGuid();
    Offer.Ordinal = Ordinal;
    Offer.Options.Reserve(Picked.Num());
    for (const LDProgression::FLegalOption& Legal : Picked)
    {
        FLDChoiceOption& Option = Offer.Options.AddDefaulted_GetRef();
        Option.OptionId = FGuid::NewGuid();
        Option.Kind = Legal.Kind;
        Option.DefinitionId = Legal.DefinitionId;
        Option.FromRank = Legal.FromRank;
        Option.ToRank = Legal.ToRank;
    }
    Offer.GeneratedBuildRevision = Record.Build.Revision;
    Offer.GuardBuildRevision = Record.Build.Revision;
    Offer.Revision = ++Progression.LastOfferRevision;
    Progression.Offer.Emplace(MoveTemp(Offer));
}

FLDBuildProjectionInput ALDDevelopmentDirector::MakeProjectionInput(const FLDGrowthBuild& Build,
    const TArray<FLDEquippedItemValue>& Equipment, uint64 ResultInventoryRevision) const
{
    FLDBuildProjectionInput Input;
    Input.Level = Build.Level;
    for (const FLDLearnedSkill& Skill : Build.Skills)
    {
        if (!Skill.bActive)
        {
            Input.Passives.Emplace(Skill.Id.ToString().ToLower(), Skill.Rank);
            continue;
        }
        FLDLearnedActiveValue& Active = Input.Actives.AddDefaulted_GetRef();
        Active.AbilityId = Skill.Id;
        Active.Rank = Skill.Rank;
        // An out-of-range slot stays None so Combat rejects the input instead of guessing a key.
        Active.Entry = Skill.Slot == 0 ? ELDCombatEntry::Q : (Skill.Slot == 1 ? ELDCombatEntry::W : ELDCombatEntry::None);
    }
    for (const FLDAttributeCardCount& Card : Build.Cards)
    {
        Input.AttributeCards.Emplace(Card.Id.ToString().ToLower(), Card.Count);
    }
    Input.Equipment = Equipment;
    Input.ResultBuildRevision = Build.Revision;
    Input.ResultInventoryRevision = ResultInventoryRevision;
    return Input;
}

FLDCombatCaller ALDDevelopmentDirector::MakeCaller(const FGuid& ContenderId) const
{
    FLDCombatCaller Caller;
    Caller.Match = Match;
    Caller.Kind = ELDVictimKind::Contender;
    Caller.EntityId = ContenderId;
    return Caller;
}

bool ALDDevelopmentDirector::ReadCombatInstalled(const FGuid& ContenderId, FLDExpectedBuildVersions& Out) const
{
    Out = FLDExpectedBuildVersions();
    const FRecord* Record = Records.Find(ContenderId);
    if (!Record)
    {
        return false;
    }
    Out = Record->CombatInstalled;
    return true;
}

const FLDGrowthBuild* ALDDevelopmentDirector::FindBuild(const FGuid& ContenderId) const
{
    const FRecord* Record = Records.Find(ContenderId);
    return Record ? &Record->Build : nullptr;
}

const FLDProgressionState* ALDDevelopmentDirector::FindProgression(const FGuid& ContenderId) const
{
    const FRecord* Record = Records.Find(ContenderId);
    return Record ? &Record->Progression : nullptr;
}

void ALDDevelopmentDirector::TakeFinalReceipts(const FGuid& ContenderId, TArray<FLDChoiceReceipt>& Out)
{
    if (FRecord* Record = Records.Find(ContenderId))
    {
        Out.Append(MoveTemp(Record->Unpublished));
        Record->Unpublished.Reset();
    }
}

#if WITH_DEV_AUTOMATION_TESTS
bool ALDDevelopmentDirector::DevQueueOffer(const FGuid& ContenderId, const TArray<FName>& DefinitionIds, FString& Error)
{
    if (!LDDevGrowthFixture::Admit(GetWorld(), Error))
    {
        return false;
    }
    const ALDGameMode* GameMode = Mode();
    const FRecord* Record = Records.Find(ContenderId);
    if (!Match.IsValid() || !Content.IsValid() || !GameMode || GameMode->GetMatchContext() != Match
        || !GameMode->IsGameplayWriteOpen(Match))
    {
        Error = TEXT("A preset offer needs the current open match; in the Lobby use LDDevGrowthFixture::StageOffer.");
        return false;
    }
    if (!Record)
    {
        Error = TEXT("The preset recipient is not a contender of this match.");
        return false;
    }
    TArray<LDProgression::FLegalOption> Resolved;
    if (!LDDevGrowthFixture::ResolvePreset(*Content, Record->Build, DefinitionIds, Resolved, Error))
    {
        return false;
    }
    // Every opportunity up to MaxChoices draws one head; the current head, if any, is already drawn.
    const TArray<TArray<FName>>* Queued = DevOfferQueue.Find(ContenderId);
    const int32 Drawn = int32(Record->Progression.ConsumedThrough) + (Record->Progression.Offer.IsSet() ? 1 : 0);
    if (Drawn + (Queued ? Queued->Num() : 0) >= Content->Get().Progression.MaxChoices)
    {
        Error = FString::Printf(TEXT("No offer is left to preset: %d drawn and %d queued of %d."), Drawn, Queued ? Queued->Num() : 0,
            Content->Get().Progression.MaxChoices);
        return false;
    }
    const int32 Position = DevOfferQueue.FindOrAdd(ContenderId).Add(DefinitionIds) + 1;
    UE_LOG(LogTemp, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE queued offer contender=%s position=%d cards=%s,%s,%s"),
        *ContenderId.ToString(), Position, *DefinitionIds[0].ToString(), *DefinitionIds[1].ToString(), *DefinitionIds[2].ToString());
    return true;
}

#endif
