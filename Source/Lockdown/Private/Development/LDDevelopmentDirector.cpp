#include "Development/LDDevelopmentDirector.h"
#include "Core/LDLog.h"

#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevGrowthFixture.h"
#include "Development/LDGrowthDirector.h"
#include "Engine/World.h"
#include "Evaluation/LDEvaluationSeeds.h"
#include "Framework/LDGameMode.h"

namespace
{
bool DevelopmentBuildShapeValid(const FLDBuildCommand& Command, int32 StatCount)
{
    if (Command.Generation == 0 || Command.Life == 0 || Command.RequestId == 0 || Command.ExpectedBuildRevision == 0
        || !LDGrowthWire::IsBuildLifePhase(Command.ExpectedLifePhase))
    {
        return false;
    }
    FName Decoded;
    const bool bNoOffer = !Command.OfferId.IsValid() && Command.OfferRevision == 0 && Command.Option == FLDDefinitionWire();
    // Only the whitelisted kinds have a shape; an unknown number is refused, never mapped to a neighbour.
    switch (Command.Kind)
    {
    case ELDBuildCommandKind::ActiveChoice:
        return Command.OfferId.IsValid() && Command.OfferRevision != 0 && Command.Option.Decode(Decoded)
            && Command.Blueprint == FLDDefinitionWire() && Command.PointCounts.IsEmpty()
            && (Command.Slot == FLDBuildCommand::AnySlot || (Command.bLearnIfPossible && Command.Slot < 3));
    case ELDBuildCommandKind::LearnBlueprint:
    case ELDBuildCommandKind::ReplaceActive:
        return bNoOffer && Command.Blueprint.Decode(Decoded) && !Command.bLearnIfPossible && Command.Slot < 3
            && Command.PointCounts.IsEmpty();
    case ELDBuildCommandKind::ForgetActive:
        return bNoOffer && Command.Blueprint == FLDDefinitionWire() && !Command.bLearnIfPossible && Command.Slot < 3
            && Command.PointCounts.IsEmpty();
    case ELDBuildCommandKind::BuyNpcOffer:
    case ELDBuildCommandKind::Respec:
        return bNoOffer && Command.Blueprint == FLDDefinitionWire() && !Command.bLearnIfPossible
            && Command.Slot == FLDBuildCommand::AnySlot && Command.PointCounts.IsEmpty();
    case ELDBuildCommandKind::AllocatePoints:
    {
        int32 Total = 0;
        for (const uint8 Count : Command.PointCounts)
        {
            Total += Count;
        }
        return bNoOffer && Command.Blueprint == FLDDefinitionWire() && !Command.bLearnIfPossible
            && Command.Slot == FLDBuildCommand::AnySlot && Command.PointCounts.Num() == StatCount && Total >= 1;
    }
    default:
        return false;
    }
}

void DevelopmentFillHeader(FLDGrowthReceiptHeader& Header, const FLDBuildCommand& Command, const FLDPendingReceiptInfo& Info,
    ELDReceiptOutcome Outcome, ELDGrowthReason Reason, const FLDGameplayWritePermit* Permit)
{
    Header.Generation = Command.Generation;
    Header.Family = ELDGrowthFamily::Build;
    Header.RequestId = Command.RequestId;
    Header.OriginalLife = Info.OriginalLife;
    Header.OriginalLifePhase = Info.OriginalLifePhase;
    Header.Outcome = Outcome;
    Header.Reason = LDGrowthWire::Raw(Reason);
    Header.bHasCommitBatch = Permit != nullptr;
    Header.CommitBatch = Permit ? Permit->GetBoundary().BatchSerial : 0;
}

void DevelopmentFillCommand(FLDBuildReceipt& Receipt, const FLDBuildCommand& Command)
{
    Receipt.Kind = Command.Kind;
    Receipt.OfferId = Command.OfferId;
    FName Definition;
    if (Command.Kind == ELDBuildCommandKind::ActiveChoice ? Command.Option.Decode(Definition) : Command.Blueprint.Decode(Definition))
    {
        Receipt.DefinitionId = Definition;
    }
}

// Neither finalized nor applied: the Transactions dispatcher faults the authority boundary.
FLDBuildReceipt DevelopmentFaultReceipt(const FLDBuildCommand& Command, const FLDPendingReceiptInfo& Info)
{
    FLDBuildReceipt Receipt;
    DevelopmentFillHeader(Receipt.Header, Command, Info, ELDReceiptOutcome::Rejected, ELDGrowthReason::AuthorityFault, nullptr);
    DevelopmentFillCommand(Receipt, Command);
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

FLDPrepareGrowthResult DevelopmentPrepareFailure(ELDGrowthReason Reason, const TCHAR* Message)
{
    FLDPrepareGrowthResult Result;
    Result.Reason = Reason;
    Result.Message = Message;
    return Result;
}

// Level, allocated points and learned actives are what Combat projects; blueprints and opportunities are not.
bool DevelopmentChangesCombat(const FLDGrowthBuild& Old, const FLDGrowthBuild& New)
{
    if (Old.Level != New.Level || Old.StatPoints != New.StatPoints || Old.Actives.Num() != New.Actives.Num())
    {
        return true;
    }
    for (int32 Index = 0; Index < Old.Actives.Num(); ++Index)
    {
        if (Old.Actives[Index].Id != New.Actives[Index].Id || Old.Actives[Index].Slot != New.Actives[Index].Slot)
        {
            return true;
        }
    }
    return false;
}

const TCHAR* DevelopmentSlotName(uint8 Slot)
{
    return Slot == 0 ? TEXT("Q") : Slot == 1 ? TEXT("W") : Slot == 2 ? TEXT("E") : TEXT("-");
}

FString DevelopmentPointsText(const TArray<uint8>& Counts)
{
    return FString::JoinBy(Counts, TEXT(","), [](uint8 Count) { return FString::FromInt(Count); });
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
    if (Definitions.Progression.ActiveSlots != 3 || Definitions.Stats.IsEmpty())
    {
        Error = TEXT("Growth requires exactly three active skill slots (Q/W/E) and the point-bought stats.");
        return false;
    }
    const FLDGrowthBuild StartBuild = LDProgression::StartBuild(Definitions);
    int64 OpeningPoints = 0;
    const ELDGrowthReason PointsReason = LDProgression::EarnedPoints(Definitions, 1, 0, OpeningPoints);
    if (PointsReason != ELDGrowthReason::None)
    {
        Error = FString::Printf(TEXT("Growth points parameters are invalid (%s)."), LDGrowthWire::ReasonName(PointsReason));
        return false;
    }
    // #198: the opening offer is three distinct actives of the opening pool; a short catalog refuses the match.
    TArray<FName> OpeningPool;
    if (LDProgression::EligibleActives(Definitions, StartBuild, 1, OpeningPool) != ELDGrowthReason::None || OpeningPool.Num() < 3)
    {
        Error = FString::Printf(TEXT("The approved catalog cannot fill the opening offer: %d opening actives, three needed."),
            OpeningPool.Num());
        return false;
    }
    TMap<FGuid, FRecord> NewRecords;
    NewRecords.Reserve(Roster.Num());
    for (const FGuid& Id : Roster)
    {
        FRecord& Record = NewRecords.Add(Id);
        Record.Build = StartBuild;
        Record.Progression.Revision = 1;
        // Every contender enters the opening rest holding its opening opportunity; respawns never add another.
        Record.Progression.ActiveEarned = LDProgression::ActiveOpportunitiesAt(Definitions.Progression, 1);
        Record.CombatInstalled = FLDExpectedBuildVersions();
    }
    Match = InMatch;
    Content = MoveTemp(InContent);
    MatchSecret = FGuid::NewGuid();
#if WITH_DEV_AUTOMATION_TESTS
    // Build evaluation (-LDBuildEvaluation only): an armed Case replays every offer of this match.
    if (const FLDEvaluationSeeds* Seeds = LDEvaluationSeeds::Armed()) { MatchSecret = Seeds->MatchSecret; }
#endif
    Records = MoveTemp(NewRecords);
#if WITH_DEV_AUTOMATION_TESTS
    // Presets staged in the Lobby serve this match's first heads, the opening offer included; nothing older survives.
    LDDevGrowthFixture::TakeStaged(Roster, DevOfferQueue);
#endif
    // The opening offer is drawn and kept now; closing, death or a retry never redraws it.
    for (const FGuid& Id : Roster)
    {
        DrawHead(Id, Records.FindChecked(Id));
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
    int64 Unspent = 0;
    const ELDGrowthReason PointsReason = LDProgression::UnspentPoints(Definitions, Build, Progression.CumulativeXP, Unspent);
    if (PointsReason != ELDGrowthReason::None)
    {
        UE_LOG(LogLDGrowth, Error, TEXT("LD_GROWTH_VIEW_FAULT contender=%s: the points of the committed build do not derive (%s)"),
            *ContenderId.ToString(), LDGrowthWire::ReasonName(PointsReason));
        return View;
    }
    View.bValid = true;
    View.Generation = Match.Generation;
    const ALDGameMode* GameMode = Mode();
    FLDContenderLifeRead LifeRead;
    FString LifeError;
    if (GameMode && GameMode->ReadContenderLife(Match, ContenderId, LifeRead, LifeError))
    {
        View.Life = LifeRead.Life;
        View.LifePhase = LifeRead.Phase;
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
    View.UnspentPoints = Unspent;
    View.StatPoints.Reserve(Definitions.Stats.Num());
    const TArray<const FLDDemoStatDefinition*> StatOrder = Definitions.StatsInDesignOrder();
    for (int32 Index = 0; Index < StatOrder.Num(); ++Index)
    {
        FLDStatPointsView& Row = View.StatPoints.AddDefaulted_GetRef();
        Row.StatId = FName(*StatOrder[Index]->Id);
        Row.Points = Build.StatPoints[Index];
    }
    View.Actives.Reserve(Build.Actives.Num());
    for (const FLDLearnedActive& Active : Build.Actives)
    {
        FLDActiveSlotView& Row = View.Actives.AddDefaulted_GetRef();
        Row.DefinitionId = Active.Id;
        Row.Slot = Active.Slot;
    }
    View.PendingBlueprints = Build.PendingBlueprints;
    View.ActiveOpportunitiesEarned = Progression.ActiveEarned;
    View.ActiveOpportunitiesConsumed = Progression.ActiveConsumed;
    View.QueuedActiveOpportunities = Progression.QueuedActiveOpportunities();
    if (Progression.Offer.IsSet())
    {
        const FLDActiveOfferState& Offer = Progression.Offer.GetValue();
        View.bHasActiveOffer = true;
        View.ActiveOffer.OfferId = Offer.OfferId;
        View.ActiveOffer.OpportunityOrdinal = Offer.Ordinal;
        View.ActiveOffer.OfferRevision = Offer.Revision;
        View.ActiveOffer.Options = Offer.Options;
    }
    View.BuildSafeAtMs = GameMode && GameMode->Combat ? GameMode->Combat->ReadBuildSafeAtMs(ContenderId) : 0;
    View.CatalogDigest = Definitions.Identity.Root;
    View.PermanentSpentPoints = Build.PermanentSpentPoints;
    return View;
}

FLDSubmitResult ALDDevelopmentDirector::QueueBuild(const FGuid& ContenderId, const FLDBuildCommand& Command)
{
    FLDSubmitResult Result;
    Result.Generation = Command.Generation;
    Result.Family = ELDGrowthFamily::Build;
    Result.RequestId = Command.RequestId;
    Result.Status = ELDAdmissionStatus::AdmissionRejected;
    if (!DevelopmentBuildShapeValid(Command, Content.IsValid() ? Content->Get().Stats.Num() : 0))
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
    if (!GameMode || !GameMode->Growth)
    {
        UE_LOG(LogLDGrowth, Error, TEXT("LD_GROWTH_BUILD_ADMISSION_FAULT contender=%s: no authority mode or growth director"),
            *ContenderId.ToString());
        Result.Reason = LDGrowthWire::Raw(ELDGrowthReason::NotAuthority);
        return Result;
    }
    // Only a request that opens the next sequence takes a server arrival number; a closed match opens none,
    // while replays of an accepted or final request still answer from the stream.
    const bool bOpensSequence = !Record->Builds.GetPending()
        && Command.RequestId == Record->Builds.GetFinalizedThrough() + 1;
    if (bOpensSequence && !GameMode->IsGameplayWriteOpen(Match))
    {
        Result.Reason = LDGrowthWire::Raw(ELDGrowthReason::MatchClosed);
        return Result;
    }
    FLDPendingReceiptInfo Info;
    Info.OriginalLife = Command.Life;
    Info.OriginalLifePhase = Command.ExpectedLifePhase;
    const uint64 Arrival = bOpensSequence ? GameMode->Growth->NextArrival() : 0;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const FLDBuildReceipt* Final = nullptr;
    bool bReserved = false;
    Result.Status = Record->Builds.Admit(Command, Command.RequestId, Info, Arrival, Reason, Final, bReserved);
    Result.Reason = LDGrowthWire::Raw(Reason);
    Result.bSequenceReserved = bReserved;
    if (Result.Status == ELDAdmissionStatus::Final && Final)
    {
        Result.bHasBuildReceipt = true;
        Result.BuildReceipt = *Final;
    }
    return Result;
}

FLDBuildReceiptQueryResult ALDDevelopmentDirector::QueryBuildReceipt(const FGuid& ContenderId, const FLDReceiptQuery& Query) const
{
    FLDBuildReceiptQueryResult Result;
    Result.Query = Query;
    const FRecord* Record = Records.Find(ContenderId);
    if (Query.Family != ELDGrowthFamily::Build || !Match.IsValid() || Query.Generation != Match.Generation || !Record)
    {
        Result.Lookup = ELDReceiptLookup::InvalidContext;
        return Result;
    }
    const FLDBuildReceipt* Final = nullptr;
    const FLDPendingReceiptInfo* PendingInfo = nullptr;
    Result.Lookup = Record->Builds.Lookup(Query.RequestId, Final, PendingInfo);
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
        if (const auto* PendingEntry = Pair.Value.Builds.GetPending())
        {
            FLDPendingGrowthIntent& Intent = Out.AddDefaulted_GetRef();
            Intent.Kind = FLDPendingGrowthIntent::EKind::Build;
            Intent.Contender = Pair.Key;
            Intent.Arrival = PendingEntry->Arrival;
        }
    }
}

bool ALDDevelopmentDirector::ReadBuildContext(const FGuid& ContenderId, LDProgression::FBuildContext& Out, uint32& OutLife) const
{
    Out = LDProgression::FBuildContext();
    OutLife = 0;
    const ALDGameMode* GameMode = Mode();
    FLDContenderLifeRead LifeRead;
    FString LifeError;
    if (!GameMode || !GameMode->Combat || !GameMode->ReadContenderLife(Match, ContenderId, LifeRead, LifeError))
    {
        return false;
    }
    OutLife = LifeRead.Life;
    Out.LifePhase = LifeRead.Phase;
    Out.Stage = GameMode->ReadBuildStage(Match);
    // The server re-reads Combat's window at the commit; the client's view of it is only a hint. Only Live reads it.
    Out.bSafe = GameMode->Combat->ReadBuildSafeAtMs(ContenderId) <= GameMode->GetAuthorityNowMs();
    return true;
}

FLDBuildReceipt ALDDevelopmentDirector::CommitPendingBuild(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit)
{
    FRecord* Record = Records.Find(ContenderId);
    const auto* PendingEntry = Record ? Record->Builds.GetPending() : nullptr;
    if (!PendingEntry)
    {
        UE_LOG(LogLDGrowth, Error, TEXT("LD_GROWTH_BUILD_FAULT contender=%s: no pending build command to commit"), *ContenderId.ToString());
        FLDBuildCommand Missing;
        Missing.Generation = Match.Generation;
        return DevelopmentFaultReceipt(Missing, FLDPendingReceiptInfo());
    }
    // Copies: finalizing the stream releases the pending entry.
    const FLDBuildCommand Command = PendingEntry->Command;
    const FLDPendingReceiptInfo Info = PendingEntry->Info;
    ALDGameMode* GameMode = Mode();
    const ELDCommitReject PermitReject = GameMode
        ? GameMode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) : ELDCommitReject::NotAuthority;
    if (PermitReject != ELDCommitReject::None || !GameMode->Combat || !GameMode->Growth || !Content.IsValid()
        || Permit.GetBoundary().Match != Match)
    {
        UE_LOG(LogLDGrowth, Error, TEXT("LD_GROWTH_BUILD_FAULT contender=%s request=%llu permit=%d: no Transactions permit or domain"),
            *ContenderId.ToString(), Command.RequestId, int32(PermitReject));
        return DevelopmentFaultReceipt(Command, Info);
    }
    const auto Reject = [&](ELDGrowthReason RejectReason)
    {
        FLDBuildReceipt RejectReceipt = RejectPending(*Record, Command, RejectReason, &Permit);
        GameMode->Growth->MarkDirty(ContenderId);
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
    LDProgression::FBuildContext Context;
    uint32 Life = 0;
    if (!ReadBuildContext(ContenderId, Context, Life))
    {
        return Reject(ELDGrowthReason::StaleContext);
    }
    if (Context.LifePhase == ELDLifePhase::Left)
    {
        return Reject(ELDGrowthReason::LeftMatch);
    }
    if (Command.Life != Life)
    {
        return Reject(ELDGrowthReason::OldLife);
    }
    if (Context.LifePhase != Command.ExpectedLifePhase || !LDGrowthWire::IsBuildLifePhase(Context.LifePhase))
    {
        return Reject(ELDGrowthReason::LifePhaseChanged);
    }
    if (Command.ExpectedBuildRevision != Record->Build.Revision)
    {
        return Reject(ELDGrowthReason::BuildStale);
    }
    const FLDDemoContentDefinitions& Definitions = Content->Get();
    const FLDGrowthBuild& Build = Record->Build;
    const FLDProgressionState& Progression = Record->Progression;
    FLDGrowthBuild NewBuild;
    FLDProgressionState NewProgression = Progression;
    bool bProgressionChanged = false;
    LDProgression::FChoiceOutcome Choice;
    FName Definition;
    FName Forgotten;
    int64 Refunded = 0;
    int64 NpcPaid = 0;
    uint8 Ordinal = 0;
    // The points of the committed record no longer derive: the boundary must fault.
    const auto PointsFault = [&]()
    {
        UE_LOG(LogLDGrowth, Error, TEXT("LD_GROWTH_BUILD_FAULT contender=%s request=%llu: allocated and paid points exceed the earned ones"),
            *ContenderId.ToString(), Command.RequestId);
        return DevelopmentFaultReceipt(Command, Info);
    };
    switch (Command.Kind)
    {
    case ELDBuildCommandKind::ActiveChoice:
    {
        const bool bOfferConsumed = Progression.Records.ContainsByPredicate(
            [&Command](const FLDActiveOpportunityRecord& Row) { return Row.OfferId == Command.OfferId; });
        if (!Progression.Offer.IsSet())
        {
            return Reject(bOfferConsumed ? ELDGrowthReason::OfferAlreadyCommitted : ELDGrowthReason::NoActiveOffer);
        }
        const FLDActiveOfferState& Offer = Progression.Offer.GetValue();
        if (Offer.OfferId != Command.OfferId)
        {
            return Reject(bOfferConsumed ? ELDGrowthReason::OfferAlreadyCommitted : ELDGrowthReason::UnknownOffer);
        }
        if (Command.OfferRevision != Offer.Revision)
        {
            return Reject(ELDGrowthReason::OfferGuardStale);
        }
        if (!Command.Option.Decode(Definition) || !Offer.Options.Contains(Definition))
        {
            return Reject(ELDGrowthReason::InvalidOption);
        }
        // The single offer is always the head of a non-empty opportunity queue.
        if (Offer.Ordinal != Progression.ActiveConsumed + 1 || Progression.QueuedActiveOpportunities() == 0)
        {
            return Reject(ELDGrowthReason::DataInvalid);
        }
        const ELDGrowthReason ChoiceReason = LDProgression::ApplyActiveChoice(Definitions, Build, Definition,
            Command.bLearnIfPossible, Command.Slot, Context, NewBuild, Choice);
        if (ChoiceReason != ELDGrowthReason::None)
        {
            return Reject(ChoiceReason);
        }
        Ordinal = Offer.Ordinal;
        FLDActiveOpportunityRecord& Consumed = NewProgression.Records.AddDefaulted_GetRef();
        Consumed.Ordinal = Offer.Ordinal;
        Consumed.OfferId = Offer.OfferId;
        Consumed.Chosen = Definition;
        Consumed.RequestId = Command.RequestId;
        NewProgression.ActiveConsumed = uint8(Progression.ActiveConsumed + 1);
        NewProgression.Offer.Reset();
        bProgressionChanged = true;
        break;
    }
    case ELDBuildCommandKind::LearnBlueprint:
    {
        Command.Blueprint.Decode(Definition);
        const ELDGrowthReason LearnReason = LDProgression::ApplyLearnBlueprint(Definitions, Build, Definition, Command.Slot,
            Context, NewBuild);
        if (LearnReason != ELDGrowthReason::None)
        {
            return Reject(LearnReason);
        }
        Choice.Result = ELDBuildChoiceResult::Learned;
        Choice.Slot = Command.Slot;
        break;
    }
    case ELDBuildCommandKind::AllocatePoints:
    {
        const ELDGrowthReason PointsReason = LDProgression::ApplyAllocation(Definitions, Build, Progression.CumulativeXP,
            Command.PointCounts, Context, NewBuild);
        if (PointsReason == ELDGrowthReason::AuthorityFault)
        {
            return PointsFault();
        }
        if (PointsReason != ELDGrowthReason::None)
        {
            return Reject(PointsReason);
        }
        break;
    }
    case ELDBuildCommandKind::BuyNpcOffer:
    {
        LDProgression::FNpcOfferOutcome Npc;
        const ELDGrowthReason NpcReason = LDProgression::ApplyBuyNpcOffer(Definitions, Build, Progression, MatchSecret, ContenderId,
            Context, NewBuild, NewProgression, Npc);
        if (NpcReason == ELDGrowthReason::AuthorityFault)
        {
            return PointsFault();
        }
        if (NpcReason != ELDGrowthReason::None)
        {
            return Reject(NpcReason);
        }
        // The payment and its fixed offer install together on the next build revision (InstallBuild).
        NewProgression.Offer->GeneratedBuildRevision = Build.Revision + 1;
        Ordinal = Npc.Ordinal;
        NpcPaid = Npc.PointsPaid;
        bProgressionChanged = true;
        break;
    }
    case ELDBuildCommandKind::Respec:
    {
        const ELDGrowthReason RespecReason = LDProgression::ApplyRespec(Definitions, Build, Progression.CumulativeXP, Context,
            NewBuild, Refunded);
        if (RespecReason == ELDGrowthReason::AuthorityFault)
        {
            return PointsFault();
        }
        if (RespecReason != ELDGrowthReason::None)
        {
            return Reject(RespecReason);
        }
        break;
    }
    case ELDBuildCommandKind::ReplaceActive:
    {
        Command.Blueprint.Decode(Definition);
        const ELDGrowthReason ReplaceReason = LDProgression::ApplyReplaceActive(Definitions, Build, Progression, Definition,
            Command.Slot, Context, NewBuild, Forgotten);
        if (ReplaceReason != ELDGrowthReason::None)
        {
            return Reject(ReplaceReason);
        }
        Choice.Result = ELDBuildChoiceResult::Learned;
        Choice.Slot = Command.Slot;
        break;
    }
    case ELDBuildCommandKind::ForgetActive:
    {
        const ELDGrowthReason ForgetReason = LDProgression::ApplyForgetActive(Definitions, Build, Progression, Command.Slot,
            Context, NewBuild, Forgotten);
        if (ForgetReason != ELDGrowthReason::None)
        {
            return Reject(ForgetReason);
        }
        break;
    }
    default:
        return Reject(ELDGrowthReason::InvalidRequest);
    }

    const ELDGrowthReason InstallReason = InstallBuild(ContenderId, *Record, MoveTemp(NewBuild), MoveTemp(NewProgression),
        bProgressionChanged, Permit);
    if (InstallReason == ELDGrowthReason::AuthorityFault)
    {
        UE_LOG(LogLDGrowth, Error, TEXT("LD_GROWTH_BUILD_FAULT contender=%s request=%llu: permit or domain lost before apply"),
            *ContenderId.ToString(), Command.RequestId);
        return DevelopmentFaultReceipt(Command, Info);
    }
    if (InstallReason != ELDGrowthReason::None)
    {
        return Reject(InstallReason);
    }

    FLDBuildReceipt Receipt;
    DevelopmentFillHeader(Receipt.Header, Command, Info, ELDReceiptOutcome::Applied, ELDGrowthReason::None, &Permit);
    DevelopmentFillCommand(Receipt, Command);
    Receipt.Header.Revisions.bHasProgression = true;
    Receipt.Header.Revisions.ProgressionRevision = Record->Progression.Revision;
    Receipt.Header.Revisions.bHasBuild = true;
    Receipt.Header.Revisions.BuildRevision = Record->Build.Revision;
    Receipt.OpportunityOrdinal = Ordinal;
    Receipt.ChoiceResult = Choice.Result;
    Receipt.LearnedSlot = Choice.Slot;
    Receipt.StoredReason = LDGrowthWire::Raw(Choice.StoredReason);
    if (Command.Kind == ELDBuildCommandKind::AllocatePoints)
    {
        Receipt.PointsSpent = Command.PointCounts;
    }
    Receipt.ForgottenId = Forgotten;
    Receipt.PointsRefunded = Refunded;
    Receipt.NpcPointsPaid = NpcPaid;
    int64 Unspent = 0;
    LDProgression::UnspentPoints(Definitions, Record->Build, Record->Progression.CumulativeXP, Unspent);
    Receipt.UnspentPointsAfter = Unspent;
    if (Record->Progression.Offer.IsSet())
    {
        Receipt.bHasNextOffer = true;
        Receipt.NextOfferId = Record->Progression.Offer->OfferId;
        Receipt.Header.Revisions.bHasOffer = true;
        Receipt.Header.Revisions.OfferRevision = Record->Progression.Offer->Revision;
    }
    if (Command.Kind == ELDBuildCommandKind::ActiveChoice)
    {
        UE_LOG(LogLDGrowth, Log, TEXT("LD_CHOICE t=%lld contender=%s ordinal=%u kind=ActiveChoice card=%s result=%s slot=%s stored=%s level=%u pending=%u"),
            GameMode->GetAuthorityNowMs(), *LDLog::Id(ContenderId), uint32(Ordinal), *Receipt.DefinitionId.ToString(),
            Choice.Result == ELDBuildChoiceResult::Learned ? TEXT("learned") : TEXT("blueprint"),
            Choice.Result == ELDBuildChoiceResult::Learned ? DevelopmentSlotName(Choice.Slot) : TEXT("-"),
            Choice.StoredReason == ELDGrowthReason::None ? TEXT("-") : LDGrowthWire::ReasonName(Choice.StoredReason),
            uint32(Record->Build.Level), uint32(Record->Progression.QueuedActiveOpportunities()));
    }
    else
    {
        const bool bNamesSlot = Command.Kind == ELDBuildCommandKind::LearnBlueprint || Command.Kind == ELDBuildCommandKind::ReplaceActive
            || Command.Kind == ELDBuildCommandKind::ForgetActive;
        UE_LOG(LogLDGrowth, Log, TEXT("LD_BUILD t=%lld contender=%s kind=%s card=%s slot=%s points=%s unspent=%lld level=%u forgot=%s refunded=%lld paid=%lld ordinal=%u"),
            GameMode->GetAuthorityNowMs(), *LDLog::Id(ContenderId), *LDLog::EnumName(Command.Kind),
            Receipt.DefinitionId.IsNone() ? TEXT("-") : *Receipt.DefinitionId.ToString(),
            bNamesSlot ? DevelopmentSlotName(Command.Slot) : TEXT("-"),
            Command.Kind == ELDBuildCommandKind::AllocatePoints ? *DevelopmentPointsText(Command.PointCounts) : TEXT("-"),
            Unspent, uint32(Record->Build.Level), Forgotten.IsNone() ? TEXT("-") : *Forgotten.ToString(), Refunded, NpcPaid,
            uint32(Ordinal));
        if (Command.Kind == ELDBuildCommandKind::BuyNpcOffer && Record->Progression.Offer.IsSet())
        {
            // Host playtest record of the actives shown, like every drawn head (DrawHead).
            UE_LOG(LogLDGrowth, Log, TEXT("LD_OFFER t=%lld contender=%s ordinal=%u level=%u cards=%s source=npc"),
                GameMode->GetAuthorityNowMs(), *LDLog::Id(ContenderId), uint32(Ordinal), uint32(Record->Build.Level),
                *FString::JoinBy(Record->Progression.Offer->Options, TEXT(","), [](FName Id) { return Id.ToString(); }));
        }
    }
    Record->Builds.Finalize(Receipt);
    Record->Unpublished.Add(Receipt);
    GameMode->Growth->MarkDirty(ContenderId);
    return Receipt;
}

ELDGrowthReason ALDDevelopmentDirector::InstallBuild(const FGuid& ContenderId, FRecord& Record, FLDGrowthBuild&& NewBuild,
    FLDProgressionState&& NewProgression, bool bProgressionChanged, const FLDGameplayWritePermit& Permit)
{
    ALDGameMode* GameMode = Mode();
    if (!GameMode || !GameMode->Combat || !GameMode->Growth || !Content.IsValid())
    {
        return ELDGrowthReason::AuthorityFault;
    }
    const FLDDemoContentDefinitions& Definitions = Content->Get();
    if (Record.Build.Revision == MAX_uint64 || Record.Progression.Revision == MAX_uint64
        || Record.Progression.LastOfferRevision == MAX_uint64)
    {
        return ELDGrowthReason::ArithmeticOverflow;
    }
    NewBuild.Revision = Record.Build.Revision + 1;
    NewProgression.Revision = bProgressionChanged ? Record.Progression.Revision + 1 : Record.Progression.Revision;
    if (const ELDGrowthReason BuildReason = LDProgression::ValidateBuild(Definitions, NewBuild); BuildReason != ELDGrowthReason::None)
    {
        return BuildReason;
    }
    int64 Unspent = 0;
    if (const ELDGrowthReason PointsReason = LDProgression::UnspentPoints(Definitions, NewBuild, NewProgression.CumulativeXP, Unspent);
        PointsReason != ELDGrowthReason::None)
    {
        return PointsReason;
    }
    // Only what Combat projects goes through it; a blueprint or an opportunity alone never moves its sources.
    TOptional<FLDPreparedCombatProjection> Projection;
    if (DevelopmentChangesCombat(Record.Build, NewBuild))
    {
        FLDCombatPrepareResult Prepared = GameMode->Combat->PrepareBuildChange(MakeCaller(ContenderId),
            MakeProjectionInput(NewBuild), Record.CombatInstalled);
        if (!Prepared.IsSuccess())
        {
            const ELDGrowthReason CombatReason = DevelopmentCombatReason(Prepared.Error);
            if (CombatReason == ELDGrowthReason::DataInvalid || CombatReason == ELDGrowthReason::PrepareUnavailable)
            {
                UE_LOG(LogLDGrowth, Warning, TEXT("LD_GROWTH_BUILD_PROJECTION contender=%s %s: %s"), *ContenderId.ToString(),
                    LDGrowthWire::ReasonName(CombatReason), *Prepared.Message);
            }
            return CombatReason;
        }
        Projection.Emplace(MoveTemp(Prepared.Value.GetValue()));
    }
    if (GameMode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None)
    {
        return ELDGrowthReason::AuthorityFault;
    }

    // Everything is prepared and the permit re-checked; the install below cannot fail.
    if (Projection.IsSet())
    {
        GameMode->Combat->ApplyBuildChange(MoveTemp(Projection.GetValue()), Permit);
        Record.CombatInstalled.BuildRevision = NewBuild.Revision;
    }
    Record.Build = MoveTemp(NewBuild);
    Record.Progression = MoveTemp(NewProgression);
    DrawHead(ContenderId, Record);
    return ELDGrowthReason::None;
}

FLDBuildReceipt ALDDevelopmentDirector::RejectPending(FRecord& Record, const FLDBuildCommand& Command,
    ELDGrowthReason Reason, const FLDGameplayWritePermit* Permit)
{
    const auto* PendingEntry = Record.Builds.GetPending();
    checkf(PendingEntry && PendingEntry->Command.RequestId == Command.RequestId && Reason != ELDGrowthReason::None,
        TEXT("A build rejection needs the pending request it finalizes and an explicit reason."));
    FLDBuildReceipt Receipt;
    DevelopmentFillHeader(Receipt.Header, Command, PendingEntry->Info, ELDReceiptOutcome::Rejected, Reason, Permit);
    DevelopmentFillCommand(Receipt, Command);
    // Current guards only as a sync hint; nothing was committed.
    Receipt.Header.Revisions.bHasBuild = true;
    Receipt.Header.Revisions.BuildRevision = Record.Build.Revision;
    if (Record.Progression.Offer.IsSet())
    {
        Receipt.Header.Revisions.bHasOffer = true;
        Receipt.Header.Revisions.OfferRevision = Record.Progression.Offer->Revision;
    }
    Record.Builds.Finalize(Receipt);
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
    const FLDProgressionState& Current = Record->Progression;
    // NPC opportunities are not earned by level; the plan only sees the level part and the purchases carry over.
    if (Current.NpcPurchased > Current.ActiveEarned)
    {
        return DevelopmentPrepareFailure(ELDGrowthReason::DataInvalid, TEXT("The contender bought more opportunities than it earned."));
    }
    const ELDGrowthReason PlanReason = LDProgression::PlanExperience(Content->Get().Progression,
        Current.CumulativeXP, Record->Build.Level, uint8(Current.ActiveEarned - Current.NpcPurchased), GrantXP, Change);
    if (PlanReason != ELDGrowthReason::None)
    {
        return DevelopmentPrepareFailure(PlanReason, TEXT("The XP grant does not fit the contender's progression."));
    }
    if (int32(Change.ActiveEarnedAfter) + int32(Current.NpcPurchased) > MAX_uint8)
    {
        return DevelopmentPrepareFailure(ELDGrowthReason::CapacityExceeded, TEXT("The active opportunity count is exhausted."));
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
        NewProgression.ActiveEarned = uint8(Change.ActiveEarnedAfter + NewProgression.NpcPurchased);
    }
    if (Change.LevelAfter != Change.LevelBefore)
    {
        if (NewBuild.Revision == MAX_uint64)
        {
            return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("The build revision is exhausted."));
        }
        NewBuild.Level = Change.LevelAfter;
        ++NewBuild.Revision;
    }
    // The points follow from the new level and XP; their derivation must hold before anything installs.
    int64 Unspent = 0;
    const ELDGrowthReason PointsReason = LDProgression::UnspentPoints(Content->Get(), NewBuild, NewProgression.CumulativeXP, Unspent);
    if (PointsReason != ELDGrowthReason::None)
    {
        return DevelopmentPrepareFailure(PointsReason, TEXT("The points of the new level and XP do not derive."));
    }
    if (NewProgression.LastOfferRevision == MAX_uint64)
    {
        return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("The offer revision is exhausted."));
    }
    FLDPrepareGrowthResult Result;
    Result.Value.Emplace(MoveTemp(Prepared));
    return Result;
}

void ALDDevelopmentDirector::ApplyPreparedGrowth(FLDPreparedGrowthChange&& Prepared, const FLDGameplayWritePermit& Permit)
{
    ALDGameMode* GameMode = Mode();
    checkf(GameMode && GameMode->Growth
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
    // A newly earned opportunity without an offer draws now (and waits when no active is eligible).
    DrawHead(Prepared.ContenderId, *Record);
    GameMode->Growth->MarkDirty(Prepared.ContenderId);
}

void ALDDevelopmentDirector::DrawHead(const FGuid& ContenderId, FRecord& Record)
{
    FLDProgressionState& Progression = Record.Progression;
    if (!Content.IsValid() || Progression.Offer.IsSet() || Progression.QueuedActiveOpportunities() == 0)
    {
        return;
    }
    checkf(Progression.LastOfferRevision < MAX_uint64, TEXT("A head draw needs an offer revision left."));
    const FLDDemoContentDefinitions& Definitions = Content->Get();
    const uint8 Ordinal = static_cast<uint8>(Progression.ActiveConsumed + 1);
    const uint64 Seed = LDProgression::OfferSeed(MatchSecret, ContenderId, Ordinal);
    TArray<FName> Picked;
#if WITH_DEV_AUTOMATION_TESTS
    TArray<TArray<FName>>* Presets = DevOfferQueue.Find(ContenderId);
    TArray<TArray<FName>> NoPresets;
    bool bPreset = false;
    FString PresetError;
    const ELDGrowthReason DrawReason = LDDevGrowthFixture::PickHead(Definitions, Record.Build, Ordinal, Seed,
        Presets ? *Presets : NoPresets, Picked, bPreset, PresetError);
    if (!PresetError.IsEmpty())
    {
        UE_LOG(LogLDGrowth, Error, TEXT("LD_DEV_FIXTURE_PRESET_REFUSED contender=%s ordinal=%u, drawn normally: %s"),
            *ContenderId.ToString(), uint32(Ordinal), *PresetError);
    }
    else if (bPreset)
    {
        UE_LOG(LogLDGrowth, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE preset offer contender=%s ordinal=%u cards=%s"),
            *ContenderId.ToString(), uint32(Ordinal), *FString::JoinBy(Picked, TEXT(","), [](FName Id) { return Id.ToString(); }));
    }
    if (Presets && Presets->IsEmpty())
    {
        DevOfferQueue.Remove(ContenderId);
    }
#else
    const ELDGrowthReason DrawReason = LDProgression::DrawActiveOffer(Definitions, Record.Build, Ordinal, Seed, Picked);
#endif
    verifyf(DrawReason == ELDGrowthReason::None, TEXT("A validated build always has an eligible-active pool."));
    const ALDGameMode* GameMode = Mode();
    const int64 NowMs = GameMode ? GameMode->GetAuthorityNowMs() : int64(0);
    if (Picked.IsEmpty())
    {
        // No active is eligible: the opportunity waits without an offer until a build change makes one eligible.
        UE_LOG(LogLDGrowth, Log, TEXT("LD_OFFER_WAITING t=%lld contender=%s ordinal=%u level=%u"), NowMs, *LDLog::Id(ContenderId),
            uint32(Ordinal), uint32(Record.Build.Level));
        return;
    }
    FLDActiveOfferState Offer;
    Offer.OfferId = FGuid::NewGuid();
    Offer.Ordinal = Ordinal;
    Offer.Options = MoveTemp(Picked);
    Offer.GeneratedBuildRevision = Record.Build.Revision;
    Offer.Revision = ++Progression.LastOfferRevision;
    // Host playtest record of the actives shown (docs/DEVELOPMENT.md).
    UE_LOG(LogLDGrowth, Log, TEXT("LD_OFFER t=%lld contender=%s ordinal=%u level=%u cards=%s"), NowMs, *LDLog::Id(ContenderId),
        uint32(Ordinal), uint32(Record.Build.Level), *FString::JoinBy(Offer.Options, TEXT(","), [](FName Id) { return Id.ToString(); }));
    Progression.Offer.Emplace(MoveTemp(Offer));
}

FLDBuildProjectionInput ALDDevelopmentDirector::MakeProjectionInput(const FLDGrowthBuild& Build) const
{
    FLDBuildProjectionInput Input;
    Input.Level = Build.Level;
    if (Content.IsValid() && Build.StatPoints.Num() == Content->Get().Stats.Num())
    {
        const TArray<const FLDDemoStatDefinition*> StatOrder = Content->Get().StatsInDesignOrder();
        for (int32 Index = 0; Index < Build.StatPoints.Num(); ++Index)
        {
            if (Build.StatPoints[Index] != 0)
            {
                Input.StatPoints.Emplace(StatOrder[Index]->Id, Build.StatPoints[Index]);
            }
        }
    }
    for (const FLDLearnedActive& Active : Build.Actives)
    {
        FLDLearnedActiveValue& Value = Input.Actives.AddDefaulted_GetRef();
        Value.AbilityId = Active.Id;
        // An out-of-range slot stays None so Combat rejects the input instead of guessing a key.
        Value.Entry = Active.Slot == 0 ? ELDCombatEntry::Q
            : Active.Slot == 1 ? ELDCombatEntry::W
            : Active.Slot == 2 ? ELDCombatEntry::E
            : ELDCombatEntry::None;
    }
    Input.ResultBuildRevision = Build.Revision;
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

void ALDDevelopmentDirector::TakeFinalReceipts(const FGuid& ContenderId, TArray<FLDBuildReceipt>& Out)
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
    if (!LDDevGrowthFixture::ResolvePreset(Content->Get(), Record->Build, DefinitionIds, Error))
    {
        return false;
    }
    // Every level opportunity of the match draws one head; the current head, if any, is already drawn. NPC opportunities
    // draw their fixed offer at payment and never take a preset.
    const TArray<TArray<FName>>* Queued = DevOfferQueue.Find(ContenderId);
    const int32 Drawn = int32(Record->Progression.ActiveConsumed) + (Record->Progression.Offer.IsSet() ? 1 : 0)
        - int32(Record->Progression.NpcPurchased);
    const int32 Opportunities = 1 + Content->Get().Progression.ActiveOfferLevels.Num();
    if (Drawn + (Queued ? Queued->Num() : 0) >= Opportunities)
    {
        Error = FString::Printf(TEXT("No offer is left to preset: %d drawn and %d queued of %d."), Drawn, Queued ? Queued->Num() : 0,
            Opportunities);
        return false;
    }
    const int32 Position = DevOfferQueue.FindOrAdd(ContenderId).Add(DefinitionIds) + 1;
    UE_LOG(LogLDGrowth, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE queued offer contender=%s position=%d cards=%s"),
        *ContenderId.ToString(), Position, *FString::JoinBy(DefinitionIds, TEXT(","), [](FName Id) { return Id.ToString(); }));
    return true;
}

ELDGrowthReason ALDDevelopmentDirector::DevGrantBlueprint(const FGuid& ContenderId, FName DefinitionId,
    const FLDGameplayWritePermit& Permit, FString& Detail)
{
    Detail.Reset();
    FRecord* Record = Records.Find(ContenderId);
    if (!Record || !Content.IsValid())
    {
        Detail = TEXT("The contender has no growth record in this match.");
        return ELDGrowthReason::NotContender;
    }
    const bool bActive = Content->Get().Abilities.ContainsByPredicate(
        [DefinitionId](const FLDDemoAbilityDefinition& Row) { return FName(*Row.Id) == DefinitionId; });
    if (!bActive || Record->Build.FindActive(DefinitionId) || Record->Build.HasBlueprint(DefinitionId))
    {
        Detail = FString::Printf(TEXT("'%s' is not an active this build may receive (unknown, learned or already a blueprint)."),
            *DefinitionId.ToString());
        return ELDGrowthReason::InvalidOption;
    }
    FLDGrowthBuild NewBuild = Record->Build;
    NewBuild.PendingBlueprints.Add(DefinitionId);
    LDProgression::NormalizeBuild(NewBuild);
    FLDProgressionState NewProgression = Record->Progression;
    const ELDGrowthReason Reason = InstallBuild(ContenderId, *Record, MoveTemp(NewBuild), MoveTemp(NewProgression), false, Permit);
    if (Reason == ELDGrowthReason::None)
    {
        Detail = FString::Printf(TEXT("%s is a pending blueprint (%d pending)"), *DefinitionId.ToString(),
            Record->Build.PendingBlueprints.Num());
    }
    return Reason;
}

ELDGrowthReason ALDDevelopmentDirector::DevLearnActive(const FGuid& ContenderId, FName DefinitionId, uint8 Slot,
    const FLDGameplayWritePermit& Permit, FString& Detail)
{
    Detail.Reset();
    FRecord* Record = Records.Find(ContenderId);
    if (!Record || !Content.IsValid())
    {
        Detail = TEXT("The contender has no growth record in this match.");
        return ELDGrowthReason::NotContender;
    }
    const bool bActive = Content->Get().Abilities.ContainsByPredicate(
        [DefinitionId](const FLDDemoAbilityDefinition& Row) { return FName(*Row.Id) == DefinitionId; });
    if (!bActive || Record->Build.FindActive(DefinitionId) || Slot > 3)
    {
        Detail = FString::Printf(TEXT("'%s' is not an active this build may learn (unknown or already learned)."), *DefinitionId.ToString());
        return ELDGrowthReason::InvalidOption;
    }
    FLDGrowthBuild NewBuild = Record->Build;
    NewBuild.PendingBlueprints.Remove(DefinitionId);
    uint8 Target = 0;
    FName Forgotten;
    if (Slot == 0)
    {
        if (!LDProgression::PickLearnSlot(NewBuild, FLDBuildCommand::AnySlot, Target))
        {
            Detail = TEXT("Q, W and E are all learned; name a slot to replace.");
            return ELDGrowthReason::NoEmptySlot;
        }
    }
    else
    {
        Target = uint8(Slot - 1);
        const int32 Occupied = NewBuild.ActiveInSlot(Target);
        if (Occupied != INDEX_NONE)
        {
            // DEV only: the active there is forgotten; Combat keeps its cooldown track for a relearn.
            Forgotten = NewBuild.Actives[Occupied].Id;
            NewBuild.Actives.RemoveAt(Occupied);
        }
    }
    NewBuild.Actives.Add({DefinitionId, Target});
    LDProgression::NormalizeBuild(NewBuild);
    FLDProgressionState NewProgression = Record->Progression;
    const ELDGrowthReason Reason = InstallBuild(ContenderId, *Record, MoveTemp(NewBuild), MoveTemp(NewProgression), false, Permit);
    if (Reason == ELDGrowthReason::None)
    {
        Detail = FString::Printf(TEXT("%s learned in %s"), *DefinitionId.ToString(), DevelopmentSlotName(Target));
        if (!Forgotten.IsNone())
        {
            Detail += FString::Printf(TEXT(", %s forgotten"), *Forgotten.ToString());
        }
    }
    return Reason;
}

ELDGrowthReason ALDDevelopmentDirector::DevAllocatePoints(const FGuid& ContenderId, FName StatId, int64 Amount,
    const FLDGameplayWritePermit& Permit, FString& Detail)
{
    Detail.Reset();
    FRecord* Record = Records.Find(ContenderId);
    if (!Record || !Content.IsValid())
    {
        Detail = TEXT("The contender has no growth record in this match.");
        return ELDGrowthReason::NotContender;
    }
    const FLDDemoContentDefinitions& Definitions = Content->Get();
    const int32 StatIndex = Definitions.StatDesignIndex(StatId.ToString());
    if (StatIndex == INDEX_NONE)
    {
        Detail = FString::Printf(TEXT("'%s' is not a stat of the approved catalog."), *StatId.ToString());
        return ELDGrowthReason::InvalidOption;
    }
    int64 Unspent = 0;
    const ELDGrowthReason PointsReason = LDProgression::UnspentPoints(Definitions, Record->Build, Record->Progression.CumulativeXP, Unspent);
    if (PointsReason != ELDGrowthReason::None)
    {
        Detail = TEXT("The committed points do not derive.");
        return PointsReason;
    }
    if (Amount < 1 || Amount > Unspent)
    {
        Detail = FString::Printf(TEXT("%lld points asked, %lld unspent."), Amount, Unspent);
        return ELDGrowthReason::NotEnoughPoints;
    }
    FLDGrowthBuild NewBuild = Record->Build;
    if (NewBuild.StatPoints[StatIndex] > MAX_int32 - Amount)
    {
        return ELDGrowthReason::ArithmeticOverflow;
    }
    NewBuild.StatPoints[StatIndex] += int32(Amount);
    FLDProgressionState NewProgression = Record->Progression;
    const ELDGrowthReason Reason = InstallBuild(ContenderId, *Record, MoveTemp(NewBuild), MoveTemp(NewProgression), false, Permit);
    if (Reason == ELDGrowthReason::None)
    {
        Detail = FString::Printf(TEXT("%s %d points, %lld unspent"), *StatId.ToString(), Record->Build.StatPoints[StatIndex],
            Unspent - Amount);
    }
    return Reason;
}

FLDPrepareGrowthResult ALDDevelopmentDirector::DevPrepareReset(const FGuid& ContenderId) const
{
    const FRecord* Record = Records.Find(ContenderId);
    if (!Record || !Content.IsValid())
    {
        return DevelopmentPrepareFailure(ELDGrowthReason::NotContender, TEXT("The reset contender has no growth record in this match."));
    }
    if (Record->Build.Revision == MAX_uint64 || Record->Progression.Revision == MAX_uint64
        || Record->Progression.LastOfferRevision == MAX_uint64)
    {
        return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("A growth revision is exhausted."));
    }
    FLDPreparedGrowthChange Prepared;
    Prepared.ContenderId = ContenderId;
    Prepared.ExpectedBuildRevision = Record->Build.Revision;
    Prepared.ExpectedProgressionRevision = Record->Progression.Revision;
    Prepared.OldLevel = Record->Build.Level;
    // The BeginMatch state with every revision still rising; Apply draws the opening offer.
    Prepared.NewBuild = LDProgression::StartBuild(Content->Get());
    Prepared.NewBuild.Revision = Record->Build.Revision + 1;
    Prepared.NewProgression = FLDProgressionState();
    Prepared.NewProgression.Revision = Record->Progression.Revision + 1;
    Prepared.NewProgression.ActiveEarned = LDProgression::ActiveOpportunitiesAt(Content->Get().Progression, 1);
    Prepared.NewProgression.LastOfferRevision = Record->Progression.LastOfferRevision;
    FLDPrepareGrowthResult Result;
    Result.Value.Emplace(MoveTemp(Prepared));
    return Result;
}

#endif
