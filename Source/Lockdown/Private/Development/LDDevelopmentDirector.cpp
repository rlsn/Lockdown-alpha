#include "Development/LDDevelopmentDirector.h"
#include "Core/LDLog.h"

#include "Combat/LDCombatDirector.h"
#include "Development/LDBreakoutPresets.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevGrowthFixture.h"
#include "Development/LDGrowthDirector.h"
#include "Engine/World.h"
#include "Evaluation/LDEvaluationSeeds.h"
#include "Framework/LDGameMode.h"

namespace
{
// The card target fields of a command, whitelisted and decoded; false when any of them is malformed.
bool DevelopmentDecodeCardTarget(const FLDBuildCommand& Command, FLDCardTarget& Out)
{
    Out = FLDCardTarget();
    if (!LDGrowthWire::IsKnownCardTarget(Command.CardTarget) || !LDGrowthWire::IsKnownElement(Command.Element)
        || !LDGrowthWire::DecodeOptional(Command.TargetDefinition, Out.Active))
    {
        Out = FLDCardTarget();
        return false;
    }
    Out.Kind = Command.CardTarget;
    Out.Element = Command.Element;
    return true;
}

bool DevelopmentBuildShapeValid(const FLDBuildCommand& Command, int32 StatCount)
{
    if (Command.Generation == 0 || Command.Life == 0 || Command.RequestId == 0 || Command.ExpectedBuildRevision == 0
        || !LDGrowthWire::IsBuildLifePhase(Command.ExpectedLifePhase) || !LDGrowthWire::IsKnownBuildKind(Command.Kind))
    {
        return false;
    }
    FName Decoded;
    FLDCardTarget Target;
    const bool bTarget = DevelopmentDecodeCardTarget(Command, Target);
    const bool bNoOffer = !Command.OfferId.IsValid() && Command.OfferRevision == 0 && Command.Option == FLDDefinitionWire();
    // The active and point kinds carry no card field; the card kinds no active or point field.
    const bool bNoCard = !Command.bInstallIfPossible && Command.CardSerial == 0 && Command.ReplacingSerial == 0 && bTarget
        && Target.IsEmpty();
    const bool bNoActive = Command.Blueprint == FLDDefinitionWire() && Command.PointCounts.IsEmpty() && !Command.bLearnIfPossible
        && Command.Slot == FLDBuildCommand::AnySlot && Command.Branch == ELDSkillBranch::None;
    const bool bOfferNamed = Command.OfferId.IsValid() && Command.OfferRevision != 0;
    switch (Command.Kind)
    {
    case ELDBuildCommandKind::ActiveChoice:
        return bOfferNamed && Command.Option.Decode(Decoded) && Command.Blueprint == FLDDefinitionWire()
            && Command.PointCounts.IsEmpty() && bNoCard && Command.Branch == ELDSkillBranch::None
            && (Command.Slot == FLDBuildCommand::AnySlot || (Command.bLearnIfPossible && Command.Slot < 3));
    case ELDBuildCommandKind::SkipActiveOffer:
        return bOfferNamed && Command.Option == FLDDefinitionWire() && bNoActive && bNoCard;
    case ELDBuildCommandKind::FinalMod:
        // Only the offer and the chosen card: a final mod binds no source.
        return Command.OfferId.IsValid() && Command.OfferRevision == 0 && Command.Option.Decode(Decoded)
            && Command.Blueprint == FLDDefinitionWire() && Command.PointCounts.IsEmpty() && !Command.bLearnIfPossible
            && Command.Slot == FLDBuildCommand::AnySlot && Command.Branch == ELDSkillBranch::None && !Command.bInstallIfPossible
            && Command.CardSerial == 0 && Command.ReplacingSerial == 0 && Command.CardTarget == ELDCardTargetKind::None
            && Command.Element == ELDDemoElement::None && Command.TargetDefinition == FLDDefinitionWire();
    case ELDBuildCommandKind::LearnBlueprint:
    case ELDBuildCommandKind::ReplaceActive:
        return bNoOffer && Command.Blueprint.Decode(Decoded) && !Command.bLearnIfPossible && Command.Slot < 3
            && Command.PointCounts.IsEmpty() && bNoCard && Command.Branch == ELDSkillBranch::None;
    case ELDBuildCommandKind::ForgetActive:
        return bNoOffer && Command.Blueprint == FLDDefinitionWire() && !Command.bLearnIfPossible && Command.Slot < 3
            && Command.PointCounts.IsEmpty() && bNoCard && Command.Branch == ELDSkillBranch::None;
    case ELDBuildCommandKind::RankUpSkill:
        return bNoOffer && Command.Blueprint == FLDDefinitionWire() && !Command.bLearnIfPossible && Command.Slot < 3
            && Command.PointCounts.IsEmpty() && bNoCard && Command.Branch != ELDSkillBranch::None
            && uint8(Command.Branch) <= uint8(ELDSkillBranch::B2);
    case ELDBuildCommandKind::Respec:
        return bNoOffer && bNoActive && bNoCard;
    // Spec §3.13: a shelf slot (or the skill point) of the stock revision OfferRevision; a refresh of that stock.
    case ELDBuildCommandKind::ShopBuy:
        return !Command.OfferId.IsValid() && Command.OfferRevision != 0 && Command.Option == FLDDefinitionWire()
            && Command.Blueprint == FLDDefinitionWire() && Command.PointCounts.IsEmpty() && !Command.bLearnIfPossible
            && Command.Branch == ELDSkillBranch::None && bNoCard
            && (Command.Slot < 8 || Command.Slot == FLDBuildCommand::ShopSkillPointSlot);
    case ELDBuildCommandKind::ShopRefresh:
        return !Command.OfferId.IsValid() && Command.OfferRevision != 0 && Command.Option == FLDDefinitionWire() && bNoActive && bNoCard;
    case ELDBuildCommandKind::AllocatePoints:
    {
        int32 Total = 0;
        for (const uint8 Count : Command.PointCounts)
        {
            Total += Count;
        }
        return bNoOffer && Command.Blueprint == FLDDefinitionWire() && !Command.bLearnIfPossible && bNoCard
            && Command.Slot == FLDBuildCommand::AnySlot && Command.Branch == ELDSkillBranch::None
            && Command.PointCounts.Num() == StatCount && Total >= 1;
    }
    case ELDBuildCommandKind::PickCard:
        // Without bInstallIfPossible the card only goes to the library, so no target rides along.
        return bOfferNamed && Command.Option.Decode(Decoded) && bNoActive && bTarget && Command.CardSerial == 0
            && Command.ReplacingSerial == 0 && (Command.bInstallIfPossible ? !Target.IsEmpty() : Target.IsEmpty());
    case ELDBuildCommandKind::SkipCards:
    case ELDBuildCommandKind::RerollCards:
        return bOfferNamed && Command.Option == FLDDefinitionWire() && bNoActive && bNoCard;
    case ELDBuildCommandKind::InstallCard:
        return bNoOffer && bNoActive && bTarget && !Target.IsEmpty() && Command.CardSerial != 0 && !Command.bInstallIfPossible
            && Command.ReplacingSerial != Command.CardSerial;
    case ELDBuildCommandKind::UninstallCard:
        return bNoOffer && bNoActive && bTarget && Target.IsEmpty() && Command.CardSerial != 0 && !Command.bInstallIfPossible
            && Command.ReplacingSerial == 0;
    default:
        return false;
    }
}

bool DevelopmentIsCardKind(ELDBuildCommandKind Kind)
{
    return Kind == ELDBuildCommandKind::PickCard || Kind == ELDBuildCommandKind::SkipCards || Kind == ELDBuildCommandKind::RerollCards
        || Kind == ELDBuildCommandKind::InstallCard
        || Kind == ELDBuildCommandKind::UninstallCard;
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
    if (DevelopmentIsCardKind(Command.Kind))
    {
        Receipt.CardSerial = Command.CardSerial;
        if (LDGrowthWire::DecodeOptional(Command.Option, Definition))
        {
            Receipt.CardId = Definition;
        }
        return;
    }
    if (Command.Kind == ELDBuildCommandKind::ActiveChoice || Command.Kind == ELDBuildCommandKind::FinalMod
        ? Command.Option.Decode(Definition) : Command.Blueprint.Decode(Definition))
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

// Level, allocated points, learned actives (and their branches), installed cards and the final mod are what Combat
// projects; blueprints, library cards and offers are not.
bool DevelopmentChangesCombat(const FLDGrowthBuild& Old, const FLDGrowthBuild& New)
{
    if (Old.Level != New.Level || Old.StatPoints != New.StatPoints || Old.Actives.Num() != New.Actives.Num()
        || Old.FinalMod != New.FinalMod)
    {
        return true;
    }
    for (int32 Index = 0; Index < Old.Actives.Num(); ++Index)
    {
        if (Old.Actives[Index].Id != New.Actives[Index].Id || Old.Actives[Index].Slot != New.Actives[Index].Slot
            || Old.Actives[Index].Branch != New.Actives[Index].Branch)
        {
            return true;
        }
    }
    const auto Installed = [](const FLDGrowthBuild& Build)
    {
        TArray<const FLDCardInstance*> Out;
        for (const FLDCardInstance& Card : Build.Cards)
        {
            if (Card.IsInstalled())
            {
                Out.Add(&Card);
            }
        }
        return Out;
    };
    const TArray<const FLDCardInstance*> OldInstalled = Installed(Old);
    const TArray<const FLDCardInstance*> NewInstalled = Installed(New);
    if (OldInstalled.Num() != NewInstalled.Num())
    {
        return true;
    }
    for (int32 Index = 0; Index < OldInstalled.Num(); ++Index)
    {
        if (OldInstalled[Index]->Serial != NewInstalled[Index]->Serial || OldInstalled[Index]->Id != NewInstalled[Index]->Id
            || !(OldInstalled[Index]->Target == NewInstalled[Index]->Target) || OldInstalled[Index]->Tier != NewInstalled[Index]->Tier)
        {
            return true;
        }
    }
    return false;
}

FLDCardInstanceView DevelopmentCardView(const FLDCardInstance& Card)
{
    FLDCardInstanceView View;
    View.Serial = Card.Serial;
    View.DefinitionId = Card.Id;
    View.OfferOrdinal = Card.OfferOrdinal;
    View.Target.Kind = Card.Target.Kind;
    View.Target.Active = Card.Target.Active;
    View.Target.Element = Card.Target.Element;
    View.Tier = FMath::Max<uint8>(Card.Tier, 1);
    return View;
}

FString DevelopmentCardTargetText(const FLDCardTarget& Target)
{
    if (Target.IsEmpty()) { return TEXT("-"); }
    FString Text = LDLog::EnumName(Target.Kind);
    if (!Target.Active.IsNone()) { Text += TEXT(":") + Target.Active.ToString(); }
    if (Target.Element != ELDDemoElement::None) { Text += TEXT(":") + LDLog::EnumName(Target.Element); }
    return Text;
}

FString DevelopmentSerialsText(const TArray<uint16>& Serials)
{
    return Serials.IsEmpty() ? FString(TEXT("-")) : FString::JoinBy(Serials, TEXT(","), [](uint16 Serial) { return FString::FromInt(Serial); });
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
    // Spec §6.4: every card offer shows CARDS.OfferSize distinct cards; a catalog that cannot fill one refuses the match.
    if (Definitions.Parameters.Cards.OfferSize <= 0 || Definitions.Cards.Num() < Definitions.Parameters.Cards.OfferSize
        || LDProgression::CardCapacity(Definitions.Parameters, 1) <= 0)
    {
        Error = FString::Printf(TEXT("The approved catalog cannot fill a card offer: %d cards, offers of %d, capacity %d."),
            Definitions.Cards.Num(), Definitions.Parameters.Cards.OfferSize, LDProgression::CardCapacity(Definitions.Parameters, 1));
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
        // Spec §6.4: the R0 opening card offer (drawn once the opening active is chosen) and the match's free rerolls.
        uint16 First = 0, Last = 0;
        if (LDProgression::QueueCardOffers(Record.Progression, ELDCardOfferSource::Opening, 1, First, Last) != ELDGrowthReason::None)
        {
            Error = TEXT("The opening card offer could not be queued.");
            return false;
        }
        Record.Progression.CardRerollsGranted = Definitions.Parameters.Cards.FreeRerolls;
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
    PaidPKAwards.Reset();
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
    View.CombatInstalledRevision = Record->CombatInstalled.BuildRevision;
    View.UnspentPoints = Unspent;
    View.ShopSpentPoints = Build.ShopSpentPoints;
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
        Row.Branch = Active.Branch;
    }
    View.SkillPoints = LDProgression::SkillPoints(Definitions, Build);
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
        View.ActiveOffer.SkipPoints = Definitions.Parameters.ActiveSkipPoints;
    }
    View.Cards.Reserve(Build.Cards.Num());
    for (const FLDCardInstance& Card : Build.Cards)
    {
        View.Cards.Add(DevelopmentCardView(Card));
    }
    View.CardCapacity = LDProgression::CardCapacity(Definitions.Parameters, Build.Level);
    View.QueuedCardOffers = uint16(Progression.CardQueue.Num());
    if (Progression.CardOffer.IsSet())
    {
        const FLDCardOfferState& Offer = Progression.CardOffer.GetValue();
        View.bHasCardOffer = true;
        View.CardOffer.OfferId = Offer.OfferId;
        View.CardOffer.Ordinal = Offer.Ordinal;
        View.CardOffer.OfferRevision = Offer.Revision;
        View.CardOffer.Source = Offer.Source;
        View.CardOffer.Options = Offer.Options;
    }
    View.CardRerolls = LDProgression::CardRerollsLeft(Progression);
    View.CatalogDigest = Definitions.Identity.Root;
    const FLDDemoShopParameters& ShopTerms = Definitions.Parameters.Shop;
    View.Shop.bStocked = Progression.Shop.bStocked;
    View.Shop.Revision = Progression.Shop.Revision;
    for (const FLDShopSlot& Slot : Progression.Shop.Slots)
    {
        FLDShopSlotView& Row = View.Shop.Slots.AddDefaulted_GetRef();
        Row.Kind = Slot.Kind;
        Row.DefinitionId = Slot.Definition;
        Row.Price = LDProgression::ShopSlotPrice(Definitions, Slot, Progression.Shop);
        Row.bSold = Slot.bSold;
    }
    View.Shop.RefreshPrice = LDProgression::ShopRefreshPrice(Definitions.Parameters, Progression.Shop);
    View.Shop.SkillPointPrice = LDProgression::ShopSkillPointPrice(Definitions.Parameters, Progression.Shop);
    View.Shop.SkillPointsBought = Build.BoughtSkillPoints;
    View.AffixSkillPoints = Build.AffixSkillPoints;
    View.Shop.SkillPointsMax = ShopTerms.SkillPointMaxPerMatch;
    View.HeldBlueprints = Progression.HeldBlueprints;
    View.FieldRewards = Progression.FieldRewards;
    View.bFinalModPending = Progression.FinalMod.IsPending();
    View.bExtraFinalModCard = Progression.bExtraFinalModCard;
    View.FinalModOfferId = Progression.FinalMod.OfferId;
    View.FinalModOptions = Progression.FinalMod.Options;
    View.FinalModChosen = Build.FinalMod.DefinitionId;
    View.FinalMod = Build.FinalMod;
    View.bBuildLocked = Progression.FinalMod.bLocked;
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
    Out.bDormRested = Out.Stage == LDProgression::EMatchStage::Live && GameMode->Combat->IsDormRested(LifeRead.Body);
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
    // Build aliases the record, which the install replaces: the receipt's point delta reads these copies.
    const int64 AwardedBefore = Build.AwardedPoints;
    const int64 ShopSpentBefore = Build.ShopSpentPoints;
    bool bProgressionChanged = false;
    LDProgression::FChoiceOutcome Choice;
    FName Definition;
    FName Forgotten;
    // ReplaceActive / ForgetActive / InstallCard with a replacement: the cards that went back to the library.
    TArray<uint16> Reclaimed;
    int64 Refunded = 0;
    uint8 Ordinal = 0;
    LDProgression::FCardPickOutcome Picked;
    LDProgression::FShopOutcome Shopped;
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
    case ELDBuildCommandKind::SkipActiveOffer:
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
        if (Command.Kind == ELDBuildCommandKind::ActiveChoice
            && (!Command.Option.Decode(Definition) || !Offer.Options.Contains(Definition)))
        {
            return Reject(ELDGrowthReason::InvalidOption);
        }
        // The single offer is always the head of a non-empty opportunity queue.
        if (Offer.Ordinal != Progression.ActiveConsumed + 1 || Progression.QueuedActiveOpportunities() == 0)
        {
            return Reject(ELDGrowthReason::DataInvalid);
        }
        int64 SkipPoints = 0;
        const ELDGrowthReason ChoiceReason = Command.Kind == ELDBuildCommandKind::ActiveChoice
            ? LDProgression::ApplyActiveChoice(Definitions, Build, Definition, Command.bLearnIfPossible, Command.Slot, Context,
                NewBuild, Choice, &Forgotten, &Reclaimed)
            : LDProgression::ApplySkipActiveOffer(Definitions, Build, Context, NewBuild, SkipPoints);
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
    case ELDBuildCommandKind::ShopBuy:
    case ELDBuildCommandKind::ShopRefresh:
    {
        ELDGrowthReason ShopReason = ELDGrowthReason::None;
        int32 Paid = 0;
        NewBuild = Build;
        if (Command.Kind == ELDBuildCommandKind::ShopBuy)
        {
            ShopReason = LDProgression::ApplyShopBuy(Definitions, Build, Progression, Command.Slot, Command.OfferRevision, Context,
                NewBuild, NewProgression, Shopped);
        }
        else
        {
            ShopReason = LDProgression::ApplyShopRefresh(Definitions, Build, Progression, Command.OfferRevision, MatchSecret,
                ContenderId, Context, NewBuild, NewProgression, Paid);
        }
        if (ShopReason == ELDGrowthReason::AuthorityFault)
        {
            return PointsFault();
        }
        if (ShopReason != ELDGrowthReason::None)
        {
            return Reject(ShopReason);
        }
        Choice = Shopped.Choice;
        bProgressionChanged = true;
        break;
    }
    case ELDBuildCommandKind::Respec:
    {
        const ELDGrowthReason RespecReason = LDProgression::ApplyRespec(Definitions, Build, Progression.CumulativeXP,
            Command.PointCounts, Context, NewBuild, Refunded);
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
            Command.Slot, Context, NewBuild, Forgotten, Reclaimed);
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
            Context, NewBuild, Forgotten, Reclaimed);
        if (ForgetReason != ELDGrowthReason::None)
        {
            return Reject(ForgetReason);
        }
        break;
    }
    case ELDBuildCommandKind::RankUpSkill:
    {
        const ELDGrowthReason RankReason = LDProgression::ApplyRankUpSkill(Definitions, Build, Command.Slot, Command.Branch, Context,
            NewBuild);
        if (RankReason != ELDGrowthReason::None)
        {
            return Reject(RankReason);
        }
        break;
    }
    case ELDBuildCommandKind::FinalMod:
    {
        Command.Option.Decode(Definition);
        const ELDGrowthReason FinalReason = LDProgression::ApplyFinalMod(Definitions, Build, Progression, Command.OfferId,
            Definition, Context, NewBuild, NewProgression);
        if (FinalReason != ELDGrowthReason::None)
        {
            return Reject(FinalReason);
        }
        bProgressionChanged = true;
        break;
    }
    case ELDBuildCommandKind::PickCard:
    case ELDBuildCommandKind::SkipCards:
    case ELDBuildCommandKind::RerollCards:
    {
        // The offer guards: the current offer by id, then its revision.
        if (!Progression.CardOffer.IsSet() || Progression.CardOffer->OfferId != Command.OfferId)
        {
            return Reject(ELDGrowthReason::NoCardOffer);
        }
        if (Command.OfferRevision != Progression.CardOffer->Revision)
        {
            return Reject(ELDGrowthReason::OfferGuardStale);
        }
        LDGrowthWire::DecodeOptional(Command.Option, Definition);
        ELDGrowthReason CardReason = ELDGrowthReason::None;
        NewBuild = Build;
        if (Command.Kind == ELDBuildCommandKind::PickCard)
        {
            FLDCardTarget Target;
            if (!DevelopmentDecodeCardTarget(Command, Target)) { return Reject(ELDGrowthReason::InvalidRequest); }
            CardReason = LDProgression::ApplyPickCard(Definitions, Build, Progression, Command.OfferId, Definition,
                Command.bInstallIfPossible, Target, Context, NewBuild, NewProgression, Picked);
        }
        else if (Command.Kind == ELDBuildCommandKind::SkipCards)
        {
            CardReason = LDProgression::ApplySkipCards(Definitions, Build, Progression, Command.OfferId, Context, NewBuild, NewProgression);
        }
        else
        {
            CardReason = LDProgression::ApplyRerollCards(Definitions, Build, Progression, Command.OfferId, MatchSecret, ContenderId,
                Context, NewProgression);
        }
        if (CardReason != ELDGrowthReason::None)
        {
            return Reject(CardReason);
        }
        bProgressionChanged = true;
        break;
    }
    case ELDBuildCommandKind::InstallCard:
    case ELDBuildCommandKind::UninstallCard:
    {
        FLDCardTarget Target;
        if (!DevelopmentDecodeCardTarget(Command, Target)) { return Reject(ELDGrowthReason::InvalidRequest); }
        const ELDGrowthReason CardReason = Command.Kind == ELDBuildCommandKind::InstallCard
            ? LDProgression::ApplyInstallCard(Definitions, Build, Command.CardSerial, Target, Context, NewBuild, Command.ReplacingSerial,
                &Reclaimed)
            : LDProgression::ApplyUninstallCard(Definitions, Build, Command.CardSerial, Context, NewBuild);
        if (CardReason != ELDGrowthReason::None)
        {
            return Reject(CardReason);
        }
        break;
    }
    default:
        return Reject(ELDGrowthReason::InvalidRequest);
    }
    // #198 G: a held high-tier blueprint is granted as soon as this change leaves an active eligible for it.
    if (LDProgression::ReleaseHeldBlueprints(Definitions, MatchSecret, ContenderId, NewBuild, NewProgression))
    {
        bProgressionChanged = true;
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
    if (Command.Kind == ELDBuildCommandKind::RankUpSkill)
    {
        const int32 Ranked = Record->Build.ActiveInSlot(Command.Slot);
        Receipt.DefinitionId = Ranked == INDEX_NONE ? NAME_None : Record->Build.Actives[Ranked].Id;
        Receipt.LearnedSlot = Command.Slot;
        Receipt.Branch = Command.Branch;
    }
    Receipt.SkillPointsAfter = LDProgression::SkillPoints(Definitions, Record->Build);
    Receipt.PointsRefunded = Refunded;
    Receipt.PointsDelta = (Record->Build.AwardedPoints - AwardedBefore) - (Record->Build.ShopSpentPoints - ShopSpentBefore);
    Receipt.UninstalledCards = Reclaimed;
    if (Command.Kind == ELDBuildCommandKind::PickCard)
    {
        Receipt.CardSerial = Picked.Serial;
    }
    if (Command.Kind == ELDBuildCommandKind::ShopBuy)
    {
        Receipt.CardSerial = Shopped.CardSerial;
        Receipt.DefinitionId = Shopped.Definition;
    }
    const FLDCardInstance* Committed = Receipt.CardSerial != 0 ? Record->Build.FindCard(Receipt.CardSerial) : nullptr;
    if (Committed)
    {
        Receipt.CardId = Committed->Id;
    }
    Receipt.bCardInstalled = Committed && Committed->IsInstalled();
    Receipt.InstallStoredReason = LDGrowthWire::Raw(Picked.StoredReason);
    Receipt.CardRerollsAfter = LDProgression::CardRerollsLeft(Record->Progression);
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
    else if (Command.Kind == ELDBuildCommandKind::ShopBuy || Command.Kind == ELDBuildCommandKind::ShopRefresh)
    {
        const FLDShopState& Shop = Record->Progression.Shop;
        UE_LOG(LogLDGrowth, Log, TEXT("LD_SHOP t=%lld contender=%s kind=%s slot=%s item=%s serial=%u points=%+lld unspent=%lld refreshes=%d stock=%s"),
            GameMode->GetAuthorityNowMs(), *LDLog::Id(ContenderId), *LDLog::EnumName(Command.Kind),
            Command.Kind == ELDBuildCommandKind::ShopBuy ? (Command.Slot == FLDBuildCommand::ShopSkillPointSlot ? TEXT("skill_point")
                : *FString::FromInt(Command.Slot)) : TEXT("-"),
            !Receipt.DefinitionId.IsNone() ? *Receipt.DefinitionId.ToString() : !Receipt.CardId.IsNone() ? *Receipt.CardId.ToString() : TEXT("-"),
            uint32(Receipt.CardSerial), Receipt.PointsDelta, Unspent, Shop.Refreshes,
            *FString::JoinBy(Shop.Slots, TEXT(","), [](const FLDShopSlot& Slot)
            { return Slot.bSold ? FString(TEXT("sold")) : Slot.Definition.ToString(); }));
    }
    else if (DevelopmentIsCardKind(Command.Kind))
    {
        UE_LOG(LogLDGrowth, Log, TEXT("LD_CARD t=%lld contender=%s kind=%s card=%s serial=%u installed=%s target=%s stored=%s reclaimed=%s rerolls=%d capacity=%d/%d"),
            GameMode->GetAuthorityNowMs(), *LDLog::Id(ContenderId), *LDLog::EnumName(Command.Kind),
            Receipt.CardId.IsNone() ? TEXT("-") : *Receipt.CardId.ToString(), uint32(Receipt.CardSerial),
            Receipt.bCardInstalled ? TEXT("1") : TEXT("0"), Committed ? *DevelopmentCardTargetText(Committed->Target) : TEXT("-"),
            Picked.StoredReason == ELDGrowthReason::None ? TEXT("-") : LDGrowthWire::ReasonName(Picked.StoredReason),
            *DevelopmentSerialsText(Reclaimed), Receipt.CardRerollsAfter, LDProgression::InstalledCards(Definitions, Record->Build),
            LDProgression::CardCapacity(Definitions.Parameters, Record->Build.Level));
    }
    else
    {
        const bool bNamesSlot = Command.Kind == ELDBuildCommandKind::LearnBlueprint || Command.Kind == ELDBuildCommandKind::ReplaceActive
            || Command.Kind == ELDBuildCommandKind::ForgetActive || Command.Kind == ELDBuildCommandKind::RankUpSkill;
        UE_LOG(LogLDGrowth, Log, TEXT("LD_BUILD t=%lld contender=%s kind=%s card=%s slot=%s points=%s unspent=%lld level=%u forgot=%s refunded=%lld awarded=%+lld ordinal=%u reclaimed=%s"),
            GameMode->GetAuthorityNowMs(), *LDLog::Id(ContenderId), *LDLog::EnumName(Command.Kind),
            Receipt.DefinitionId.IsNone() ? TEXT("-") : *Receipt.DefinitionId.ToString(),
            bNamesSlot ? DevelopmentSlotName(Command.Slot) : TEXT("-"),
            Command.Kind == ELDBuildCommandKind::AllocatePoints ? *DevelopmentPointsText(Command.PointCounts) : TEXT("-"),
            Unspent, uint32(Record->Build.Level), Forgotten.IsNone() ? TEXT("-") : *Forgotten.ToString(), Refunded,
            Receipt.PointsDelta, uint32(Ordinal), *DevelopmentSerialsText(Reclaimed));
    }
    if (Command.Kind == ELDBuildCommandKind::RankUpSkill)
    {
        UE_LOG(LogLDGrowth, Log, TEXT("LD_SKILL_RANK t=%lld contender=%s skill=%s slot=%s branch=%s rank=%u skill_points=%d"),
            GameMode->GetAuthorityNowMs(), *LDLog::Id(ContenderId), *Receipt.DefinitionId.ToString(), DevelopmentSlotName(Command.Slot),
            LDSkillBranch::StepKey(Command.Branch), uint32(LDSkillBranch::RankOf(Command.Branch)), Receipt.SkillPointsAfter);
    }
    if (Command.Kind == ELDBuildCommandKind::FinalMod)
    {
        UE_LOG(LogLDGrowth, Log, TEXT("LD_FINAL_MOD_LOCK t=%lld contender=%s chosen=%s slot=%s forgot=%s"), GameMode->GetAuthorityNowMs(),
            *LDLog::Id(ContenderId), *Receipt.DefinitionId.ToString(), TEXT("-"), TEXT("-"));
    }
    Record->Builds.Finalize(Receipt);
    Record->Unpublished.Add(Receipt);
    GameMode->Growth->MarkDirty(ContenderId);
    return Receipt;
}

ELDGrowthReason ALDDevelopmentDirector::GrantPKPoints(const FGuid& ContenderId, int64 Points, const FGuid& AwardId,
    bool bExtraFinalModCard, const FLDGameplayWritePermit& Permit)
{
    ALDGameMode* GameMode = Mode();
    FRecord* Record = Records.Find(ContenderId);
    // PK settlements and trending survival points pay in Transactions.
    if (!GameMode || GameMode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None)
    {
        return ELDGrowthReason::AuthorityFault;
    }
    if (!Record) { return ELDGrowthReason::NotContender; }
    if (Points < 1 || !AwardId.IsValid()) { return ELDGrowthReason::InvalidRequest; }
    if (PaidPKAwards.Contains(AwardId)) { return ELDGrowthReason::PKAwardDuplicate; }
    FLDGrowthBuild NewBuild = Record->Build;
    if (NewBuild.AwardedPoints > MAX_int64 - Points) { return ELDGrowthReason::ArithmeticOverflow; }
    NewBuild.AwardedPoints += Points;
    FLDProgressionState NewProgression = Record->Progression;
    const bool bNewCard = bExtraFinalModCard && !NewProgression.bExtraFinalModCard;
    if (bNewCard && NewProgression.FinalMod.OfferId.IsValid()) { return ELDGrowthReason::PhaseClosed; }
    NewProgression.bExtraFinalModCard |= bNewCard;
    const ELDGrowthReason Reason = InstallBuild(ContenderId, *Record, MoveTemp(NewBuild), MoveTemp(NewProgression), bNewCard, Permit);
    if (Reason == ELDGrowthReason::None)
    {
        PaidPKAwards.Add(AwardId);
        GameMode->Growth->MarkDirty(ContenderId);
        UE_LOG(LogLDGrowth, Display, TEXT("LD_PK_POINTS contender=%s award=%s points=%lld awarded_total=%lld"),
            *LDLog::Id(ContenderId), *LDLog::Id(AwardId), Points, Record->Build.AwardedPoints);
    }
    return Reason;
}

bool ALDDevelopmentDirector::GrantExtraFinalModCard(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit, FString& Error)
{
    ALDGameMode* GameMode = Mode();
    FRecord* Record = Records.Find(ContenderId);
    if (!GameMode || !GameMode->Growth || Permit.GetBoundary().Match != Match
        || (GameMode->ValidateWritePermit(Permit, ELDCommitPhase::DeathsAndRewards) != ELDCommitReject::None
            && GameMode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None) || !Record)
    {
        Error = TEXT("The extra final-mod option requires the current death or Transactions permit and a contender's growth record.");
        return false;
    }
    FLDProgressionState& Progression = Record->Progression;
    if (Progression.bExtraFinalModCard) { Error.Reset(); return true; }
    if (Progression.FinalMod.OfferId.IsValid() || Progression.Revision == MAX_uint64)
    {
        Error = TEXT("The extra final-mod option cannot be applied after the offer opens or its revision is exhausted.");
        return false;
    }
    Progression.bExtraFinalModCard = true;
    ++Progression.Revision;
    GameMode->Growth->MarkDirty(ContenderId);
    Error.Reset();
    return true;
}

bool ALDDevelopmentDirector::GrantSkillPoint(const FGuid& ContenderId, const TCHAR* Cause, const FLDGameplayWritePermit& Permit,
    FString& Error)
{
    ALDGameMode* GameMode = Mode();
    FRecord* Record = Records.Find(ContenderId);
    if (!GameMode || !GameMode->Growth || !Content.IsValid() || Permit.GetBoundary().Match != Match
        || GameMode->ValidateWritePermit(Permit, Permit.GetPhase()) != ELDCommitReject::None || !Record)
    {
        Error = TEXT("A skill point grant needs a world permit and a contender's growth record.");
        return false;
    }
    FLDGrowthBuild NewBuild = Record->Build;
    ++NewBuild.AffixSkillPoints;
    FLDProgressionState NewProgression = Record->Progression;
    const ELDGrowthReason Reason = InstallBuild(ContenderId, *Record, MoveTemp(NewBuild), MoveTemp(NewProgression), false, Permit);
    if (Reason != ELDGrowthReason::None)
    {
        Error = FString::Printf(TEXT("The %s skill point to %s was refused: %s."), Cause, *LDLog::Id(ContenderId), LDGrowthWire::ReasonName(Reason));
        return false;
    }
    GameMode->Growth->MarkDirty(ContenderId);
    UE_LOG(LogLDGrowth, Display, TEXT("LD_SKILL_POINT contender=%s cause=%s"), *LDLog::Id(ContenderId), Cause);
    return true;
}

bool ALDDevelopmentDirector::RollMedal(const FGuid& ContenderId, int64 ChanceBp, uint64 Key, const TCHAR* Cause,
    const FLDGameplayWritePermit& Permit, FString& Error)
{
    ALDGameMode* GameMode = Mode();
    if (!GameMode || !GameMode->Growth || !Content.IsValid() || Permit.GetBoundary().Match != Match
        || GameMode->ValidateWritePermit(Permit, Permit.GetPhase()) != ELDCommitReject::None)
    {
        Error = TEXT("A medal drop needs a world permit and the growth domain.");
        return false;
    }
    FRecord* Record = Records.Find(ContenderId);
    FName Medal;
    if (!Record || !LDProgression::RollMedal(Content->Get(), LDProgression::MedalSeed(MatchSecret, ContenderId, Key), ChanceBp, Medal))
    {
        return true;
    }
    FLDGrowthBuild NewBuild = Record->Build;
    uint16 Serial = 0;
    ELDGrowthReason Reason = LDProgression::ReceiveCard(Content->Get(), NewBuild, Medal, 0, Serial);
    if (Reason == ELDGrowthReason::None)
    {
        FLDProgressionState NewProgression = Record->Progression;
        Reason = InstallBuild(ContenderId, *Record, MoveTemp(NewBuild), MoveTemp(NewProgression), false, Permit);
    }
    if (Reason != ELDGrowthReason::None)
    {
        Error = FString::Printf(TEXT("The %s medal to %s was refused: %s."), Cause, *LDLog::Id(ContenderId), LDGrowthWire::ReasonName(Reason));
        return false;
    }
    GameMode->Growth->MarkDirty(ContenderId);
    UE_LOG(LogLDGrowth, Display, TEXT("LD_MEDAL contender=%s cause=%s medal=%s serial=%u"), *LDLog::Id(ContenderId), Cause,
        *Medal.ToString(), uint32(Serial));
    return true;
}

bool ALDDevelopmentDirector::OpenFinalMods(const FLDGameplayWritePermit& Permit, FString& Error)
{
    ALDGameMode* GameMode = Mode();
    if (!GameMode || !GameMode->Growth || !Content.IsValid() || Permit.GetBoundary().Match != Match
        || GameMode->ValidateWritePermit(Permit, Permit.GetPhase()) != ELDCommitReject::None)
    {
        Error = TEXT("final mods need a world permit and the growth domain");
        return false;
    }
    TArray<TPair<FGuid, TArray<FName>>> Prepared;
    for (const TPair<FGuid, FRecord>& Pair : Records)
    {
        const FLDProgressionState& Progression = Pair.Value.Progression;
        if (Progression.FinalMod.OfferId.IsValid() || !GameMode->IsContenderPresent(Pair.Key)) { continue; }
        if (Progression.Revision == MAX_uint64) { Error = TEXT("final mod offer revision overflow"); return false; }
        TArray<FName> Options;
        const int32 Count = Progression.bExtraFinalModCard ? 4 : 3;
        const ELDGrowthReason Reason = LDProgression::DrawFinalModOptions(Content->Get(),
            LDProgression::FinalModSeed(MatchSecret, Pair.Key), Options, Count);
        if (Reason != ELDGrowthReason::None || Options.Num() != Count)
        {
            Error = FString::Printf(TEXT("final mod offer for %s: %s"), *LDLog::Id(Pair.Key), LDGrowthWire::ReasonName(Reason));
            return false;
        }
        Prepared.Emplace(Pair.Key, MoveTemp(Options));
    }
    for (TPair<FGuid, TArray<FName>>& Pair : Prepared)
    {
        FLDProgressionState& Progression = Records.FindChecked(Pair.Key).Progression;
        Progression.FinalMod.OfferId = FGuid::NewGuid();
        Progression.FinalMod.Options = MoveTemp(Pair.Value);
        ++Progression.Revision;
        GameMode->Growth->MarkDirty(Pair.Key);
        UE_LOG(LogLDGrowth, Log, TEXT("LD_FINAL_MOD_OPEN t=%lld contender=%s cards=%s"), GameMode->GetAuthorityNowMs(),
            *LDLog::Id(Pair.Key), *FString::JoinBy(Progression.FinalMod.Options, TEXT(","), [](FName Id) { return Id.ToString(); }));
    }
    return true;
}

bool ALDDevelopmentDirector::GrantRestRerolls(const FLDGameplayWritePermit& Permit, FString& Error)
{
    ALDGameMode* GameMode = Mode();
    if (!GameMode || !GameMode->Growth || !Content.IsValid() || Permit.GetBoundary().Match != Match
        || GameMode->ValidateWritePermit(Permit, Permit.GetPhase()) != ELDCommitReject::None)
    {
        Error = TEXT("rest rerolls need a world permit and the growth domain");
        return false;
    }
    const int32 Grant = Content->Get().Parameters.Cards.RerollsPerRest;
    if (Grant <= 0) { return true; }
    for (TPair<FGuid, FRecord>& Pair : Records)
    {
        FLDProgressionState& Progression = Pair.Value.Progression;
        if (Progression.Revision == MAX_uint64 || Progression.CardRerollsGranted > MAX_int32 - Grant)
        {
            Error = FString::Printf(TEXT("rest rerolls of %s overflow"), *LDLog::Id(Pair.Key));
            return false;
        }
    }
    for (TPair<FGuid, FRecord>& Pair : Records)
    {
        FLDProgressionState& Progression = Pair.Value.Progression;
        Progression.CardRerollsGranted += Grant;
        ++Progression.Revision;
        GameMode->Growth->MarkDirty(Pair.Key);
        UE_LOG(LogLDGrowth, Log, TEXT("LD_CARD_REROLLS t=%lld contender=%s granted=%d left=%d"), GameMode->GetAuthorityNowMs(),
            *LDLog::Id(Pair.Key), Grant, LDProgression::CardRerollsLeft(Progression));
    }
    return true;
}

bool ALDDevelopmentDirector::StockRestShops(const FLDGameplayWritePermit& Permit, FString& Error)
{
    ALDGameMode* GameMode = Mode();
    if (!GameMode || !GameMode->Growth || !Content.IsValid() || Permit.GetBoundary().Match != Match
        || GameMode->ValidateWritePermit(Permit, Permit.GetPhase()) != ELDCommitReject::None)
    {
        Error = TEXT("rest shops need a world permit and the growth domain");
        return false;
    }
    // Every shelf is drawn before any is installed, so a failure leaves every shop as it was.
    TMap<FGuid, FLDProgressionState> Stocked;
    for (TPair<FGuid, FRecord>& Pair : Records)
    {
        FLDProgressionState Next = Pair.Value.Progression;
        if (Next.Revision == MAX_uint64) { Error = FString::Printf(TEXT("the progression revision of %s is exhausted"), *LDLog::Id(Pair.Key)); return false; }
        const ELDGrowthReason Reason = LDProgression::StockShop(Content->Get(), Pair.Value.Build, Next, MatchSecret, Pair.Key, true);
        Next.Shop.bSale = GameMode->HasDaily(ELDDailyModifier::Sale);
        if (Reason != ELDGrowthReason::None)
        {
            Error = FString::Printf(TEXT("the rest shop of %s does not stock: %s"), *LDLog::Id(Pair.Key), LDGrowthWire::ReasonName(Reason));
            return false;
        }
        ++Next.Revision;
        Stocked.Add(Pair.Key, MoveTemp(Next));
    }
    for (TPair<FGuid, FLDProgressionState>& Pair : Stocked)
    {
        Records[Pair.Key].Progression = MoveTemp(Pair.Value);
        GameMode->Growth->MarkDirty(Pair.Key);
        const FLDShopState& Shop = Records[Pair.Key].Progression.Shop;
        UE_LOG(LogLDGrowth, Log, TEXT("LD_SHOP_STOCK t=%lld contender=%s stock=%u slots=%s"), GameMode->GetAuthorityNowMs(),
            *LDLog::Id(Pair.Key), Shop.Stocks, *FString::JoinBy(Shop.Slots, TEXT(","), [](const FLDShopSlot& Slot) { return Slot.Definition.ToString(); }));
    }
    return true;
}

bool ALDDevelopmentDirector::ReadBuildRequestCursor(const FGuid& ContenderId,uint64& Next,uint64& Pending,FString& Error) const
{
    const FRecord* Record=Records.Find(ContenderId);
    if(!Record||Record->Builds.GetFinalizedThrough()==MAX_uint64)
    {Error=TEXT("Reconnect build cursor has no current stream or exhausted its sequence.");return false;}
    Next=Record->Builds.GetFinalizedThrough()+1;
    Pending=Record->Builds.PendingRequestId();
    Error.Reset();return true;
}

bool ALDDevelopmentDirector::LockFinalMods(const FLDGameplayWritePermit& Permit, FString& Error)
{
    ALDGameMode* GameMode = Mode();
    if (!GameMode || !GameMode->Growth || Permit.GetBoundary().Match != Match
        || GameMode->ValidateWritePermit(Permit, Permit.GetPhase()) != ELDCommitReject::None)
    {
        Error = TEXT("locking final mods needs a world permit and the growth domain");
        return false;
    }
    for (TPair<FGuid, FRecord>& Pair : Records)
    {
        if (!Pair.Value.Progression.FinalMod.bLocked && Pair.Value.Progression.Revision == MAX_uint64)
        { Error = TEXT("final mod lock revision overflow"); return false; }
    }
    for (TPair<FGuid, FRecord>& Pair : Records)
    {
        FLDProgressionState& Progression = Pair.Value.Progression;
        if (Progression.FinalMod.bLocked)
        {
            continue;
        }
        Progression.FinalMod.bLocked = true;
        ++Progression.Revision;
        GameMode->Growth->MarkDirty(Pair.Key);
        UE_LOG(LogLDGrowth, Log, TEXT("LD_FINAL_MOD_LOCK t=%lld contender=%s chosen=-"), GameMode->GetAuthorityNowMs(),
            *LDLog::Id(Pair.Key));
    }
    return true;
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
    // A build command commits in Transactions. A reward that only changes the record and never Combat's projection (the
    // audit affix's skill point at a camp clear) lands in DeathsAndRewards like the other rewards of that kill.
    if (GameMode->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None
        && (Projection.IsSet() || GameMode->ValidateWritePermit(Permit, ELDCommitPhase::DeathsAndRewards) != ELDCommitReject::None))
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
    const ELDGrowthReason PlanReason = LDProgression::PlanExperience(Content->Get().Progression,
        Current.CumulativeXP, Record->Build.Level, Current.ActiveEarned, GrantXP, Change);
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
        NewProgression.ActiveEarned = Change.ActiveEarnedAfter;
    }
    if (Change.LevelAfter != Change.LevelBefore)
    {
        if (NewBuild.Revision == MAX_uint64)
        {
            return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("The build revision is exhausted."));
        }
        NewBuild.Level = Change.LevelAfter;
        ++NewBuild.Revision;
        // Spec §6.4: each level crossed that CARDS.OfferEveryLevels divides queues one card offer in this same commit; the level is reached once per match, so no
        // reward, death or reconnect ever queues it again.
        const ELDGrowthReason QueueReason = LDProgression::QueueCardOffers(NewProgression, ELDCardOfferSource::LevelUp,
            LDProgression::LevelUpCardOffers(Content->Get().Parameters.Cards, Change.LevelBefore, Change.LevelAfter),
            Prepared.OfferFirst, Prepared.OfferLast);
        if (QueueReason != ELDGrowthReason::None)
        {
            return DevelopmentPrepareFailure(QueueReason, TEXT("The level-up card offers do not fit the queue."));
        }
        // A level that queues no card offer may drop an element medal (MEDAL.LevelChance), in this same commit.
        for (const int32 Level : LDProgression::MedalLevels(Content->Get().Parameters.Cards, Change.LevelBefore, Change.LevelAfter))
        {
            FName Medal;
            if (!LDProgression::RollMedal(Content->Get(), LDProgression::MedalSeed(MatchSecret, ContenderId, 0x4c56000000ull + uint64(Level)),
                Content->Get().Parameters.Medal.LevelChanceBp, Medal)) { continue; }
            uint16 Serial = 0;
            if (const ELDGrowthReason MedalReason = LDProgression::ReceiveCard(Content->Get(), NewBuild, Medal, 0, Serial);
                MedalReason != ELDGrowthReason::None)
            {
                return DevelopmentPrepareFailure(MedalReason, TEXT("The level-up medal does not fit the build."));
            }
            // Prepared, not yet installed: the commit may still drop this plan and retry it.
            UE_LOG(LogLDGrowth, Display, TEXT("LD_MEDAL_PREPARE contender=%s cause=level_%d medal=%s serial=%u"), *LDLog::Id(ContenderId), Level,
                *Medal.ToString(), uint32(Serial));
        }
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

#if WITH_DEV_AUTOMATION_TESTS
FLDPrepareGrowthResult ALDDevelopmentDirector::DevPrepareEvaluationExperience(const FGuid& ContenderId, int64 GrantXP,
    const TArray<FName>& LevelCards) const
{
    FLDPrepareGrowthResult Result = PrepareExperience(ContenderId, GrantXP);
    if (!Result.IsSuccess()) { return Result; }
    FLDPreparedGrowthChange& Prepared = Result.Value.GetValue();
    const int32 Crossed = Prepared.OfferLast >= Prepared.OfferFirst ? int32(Prepared.OfferLast) - int32(Prepared.OfferFirst) + 1 : 0;
    if (LevelCards.Num() != Crossed)
    {
        return DevelopmentPrepareFailure(ELDGrowthReason::DataInvalid,
            *FString::Printf(TEXT("An evaluation preset names %d level cards for %d levels crossed."), LevelCards.Num(), Crossed));
    }
    // The preset replaces exactly the level-up offers this change queued: each crossed level receives its named card instead,
    // while keeping any bonus medals PrepareExperience rolled.
    FLDProgressionState& Progression = Prepared.NewProgression;
    Progression.CardQueue.SetNum(Progression.CardQueue.Num() - Crossed);
    Progression.CardOffersEarned = uint16(Progression.CardOffersEarned - Crossed);
    for (int32 Index = 0; Index < Crossed; ++Index)
    {
        uint16 Serial = 0;
        if (const ELDGrowthReason Reason = LDProgression::ReceiveCard(Content->Get(), Prepared.NewBuild, LevelCards[Index], 0, Serial);
            Reason != ELDGrowthReason::None)
        {
            return DevelopmentPrepareFailure(Reason, *FString::Printf(TEXT("'%s' is not a card of the approved catalog."),
                *LevelCards[Index].ToString()));
        }
    }
    Prepared.OfferFirst = 1;
    Prepared.OfferLast = 0;
    return Result;
}
#endif

FLDPrepareGrowthResult ALDDevelopmentDirector::PrepareFieldReward(const FGuid& ContenderId, int64 GrantXP, ELDFieldRewardKind Kind,
    FName Event, uint64 Serial) const
{
    FLDPrepareGrowthResult Result = PrepareExperience(ContenderId, GrantXP);
    if (!Result.IsSuccess() || Kind == ELDFieldRewardKind::None)
    {
        return Result;
    }
    if (Event.IsNone() || Serial == 0)
    {
        return DevelopmentPrepareFailure(ELDGrowthReason::DataInvalid, TEXT("A field reward needs its event identity."));
    }
    FLDPreparedGrowthChange& Prepared = Result.Value.GetValue();
    const FLDDemoContentDefinitions& Definitions = Content->Get();
    LDProgression::FFieldGrant Grant;
    const ELDGrowthReason DrawReason = LDProgression::DrawFieldReward(Definitions, Prepared.NewBuild, Kind,
        LDProgression::FieldRewardSeed(MatchSecret, ContenderId, Event, Serial), Grant);
    if (DrawReason != ELDGrowthReason::None)
    {
        return DevelopmentPrepareFailure(DrawReason, TEXT("The field reward does not draw from the approved supply."));
    }
    const FLDGrowthBuild BuildBefore = Prepared.NewBuild;
    const uint16 EarnedBefore = Prepared.NewProgression.CardOffersEarned;
    const ELDGrowthReason GrantReason = LDProgression::ApplyFieldGrant(Definitions, Grant, Event, Serial, GrantXP,
        Prepared.NewBuild, Prepared.NewProgression);
    if (GrantReason != ELDGrowthReason::None)
    {
        return DevelopmentPrepareFailure(GrantReason, TEXT("The field reward does not fit the contender's build."));
    }
    // One revision step each for the whole reward, whether or not the XP part already moved it.
    if (Prepared.NewBuild.Revision == Prepared.ExpectedBuildRevision
        && (Prepared.NewBuild.PendingBlueprints != BuildBefore.PendingBlueprints
            || Prepared.NewBuild.StatPoints != BuildBefore.StatPoints
            || Prepared.NewBuild.AwardedPoints != BuildBefore.AwardedPoints))
    {
        if (Prepared.NewBuild.Revision == MAX_uint64)
        {
            return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("The build revision is exhausted."));
        }
        ++Prepared.NewBuild.Revision;
    }
    if (Prepared.NewProgression.Revision == Prepared.ExpectedProgressionRevision)
    {
        if (Prepared.NewProgression.Revision == MAX_uint64)
        {
            return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("The progression revision is exhausted."));
        }
        ++Prepared.NewProgression.Revision;
    }
    int64 Unspent = 0;
    if (const ELDGrowthReason Reason = LDProgression::ValidateBuild(Definitions, Prepared.NewBuild); Reason != ELDGrowthReason::None)
    {
        return DevelopmentPrepareFailure(Reason, TEXT("The field reward leaves an invalid build."));
    }
    if (const ELDGrowthReason Reason = LDProgression::UnspentPoints(Definitions, Prepared.NewBuild, Prepared.NewProgression.CumulativeXP,
            Unspent); Reason != ELDGrowthReason::None)
    {
        return DevelopmentPrepareFailure(Reason, TEXT("The points of the field reward do not derive."));
    }
    // A field card offer joins the queue behind the level-up ones this change queued.
    if (Prepared.NewProgression.CardOffersEarned != EarnedBefore)
    {
        if (Prepared.OfferLast < Prepared.OfferFirst) { Prepared.OfferFirst = Prepared.NewProgression.CardOffersEarned; }
        Prepared.OfferLast = Prepared.NewProgression.CardOffersEarned;
    }
    Prepared.Field = MoveTemp(Grant);
    return Result;
}

FLDPrepareGrowthResult ALDDevelopmentDirector::PrepareDeathShare(const FGuid& ContenderId, int64 GrantXP, int64 BonusStatPoints,
    const TArray<ELDCardOfferSource>& ExtraOffers, int32 PvPKillSegment) const
{
    FLDPrepareGrowthResult Result = PrepareExperience(ContenderId, GrantXP);
    if (!Result.IsSuccess() || (BonusStatPoints <= 0 && ExtraOffers.IsEmpty() && PvPKillSegment == INDEX_NONE))
    {
        return Result;
    }
    FLDPreparedGrowthChange& Prepared = Result.Value.GetValue();
    const FLDDemoContentDefinitions& Definitions = Content->Get();
    const int64 AwardedBefore = Prepared.NewBuild.AwardedPoints;
    const uint16 EarnedBefore = Prepared.NewProgression.CardOffersEarned;
    int64 TotalBonusPoints = BonusStatPoints;
    TArray<ELDCardOfferSource> QueuedOffers = ExtraOffers;
    if (PvPKillSegment >= 0 && PvPKillSegment < 4)
    {
        if (Prepared.NewProgression.PvPKillOffersInSegment[PvPKillSegment] < 2)
        {
            ++Prepared.NewProgression.PvPKillOffersInSegment[PvPKillSegment];
            QueuedOffers.Insert(ELDCardOfferSource::PvPKill, 0);
        }
        else
        {
            TotalBonusPoints += 2;
        }
    }
    for (const ELDCardOfferSource Source : QueuedOffers)
    {
        uint16 First = 0, Last = 0;
        if (const ELDGrowthReason Reason = LDProgression::QueueCardOffers(Prepared.NewProgression, Source, 1, First, Last);
            Reason != ELDGrowthReason::None)
        {
            return DevelopmentPrepareFailure(Reason, TEXT("Could not queue death reward card offer."));
        }
    }
    if (TotalBonusPoints > 0)
    {
        if (Prepared.NewBuild.AwardedPoints > MAX_int64 - TotalBonusPoints)
        {
            return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("Awarded stat points overflowed."));
        }
        Prepared.NewBuild.AwardedPoints += TotalBonusPoints;
    }
    if (Prepared.NewBuild.Revision == Prepared.ExpectedBuildRevision && Prepared.NewBuild.AwardedPoints != AwardedBefore)
    {
        if (Prepared.NewBuild.Revision == MAX_uint64)
        {
            return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("The build revision is exhausted."));
        }
        ++Prepared.NewBuild.Revision;
    }
    if (Prepared.NewProgression.Revision == Prepared.ExpectedProgressionRevision
        && (Prepared.NewProgression.CardOffersEarned != EarnedBefore || PvPKillSegment != INDEX_NONE))
    {
        if (Prepared.NewProgression.Revision == MAX_uint64)
        {
            return DevelopmentPrepareFailure(ELDGrowthReason::ArithmeticOverflow, TEXT("The progression revision is exhausted."));
        }
        ++Prepared.NewProgression.Revision;
    }
    if (Prepared.NewProgression.CardOffersEarned != EarnedBefore)
    {
        if (Prepared.OfferLast < Prepared.OfferFirst) { Prepared.OfferFirst = EarnedBefore + 1; }
        Prepared.OfferLast = Prepared.NewProgression.CardOffersEarned;
    }
    int64 Unspent = 0;
    if (const ELDGrowthReason Reason = LDProgression::ValidateBuild(Definitions, Prepared.NewBuild); Reason != ELDGrowthReason::None)
    {
        return DevelopmentPrepareFailure(Reason, TEXT("Death share leaves an invalid build."));
    }
    if (const ELDGrowthReason Reason = LDProgression::UnspentPoints(Definitions, Prepared.NewBuild,
        Prepared.NewProgression.CumulativeXP, Unspent); Reason != ELDGrowthReason::None)
    {
        return DevelopmentPrepareFailure(Reason, TEXT("Death share points do not derive."));
    }
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

void ALDDevelopmentDirector::DrawCardHead(const FGuid& ContenderId, FRecord& Record)
{
    if (!Content.IsValid())
    {
        return;
    }
    bool bDrawn = false;
    const ELDGrowthReason Reason = LDProgression::RefreshCardOffer(Content->Get(), Record.Build, Record.Progression, MatchSecret,
        ContenderId, bDrawn);
    const ALDGameMode* GameMode = Mode();
    const int64 NowMs = GameMode ? GameMode->GetAuthorityNowMs() : int64(0);
    if (Reason != ELDGrowthReason::None)
    {
        // A validated catalog always fills an offer (BeginMatch); the queue keeps its head for the next change.
        UE_LOG(LogLDGrowth, Error, TEXT("LD_CARD_OFFER_FAULT t=%lld contender=%s reason=%s"), NowMs, *LDLog::Id(ContenderId),
            LDGrowthWire::ReasonName(Reason));
        return;
    }
    if (bDrawn)
    {
        const FLDCardOfferState& Offer = Record.Progression.CardOffer.GetValue();
        // Host playtest record of the cards shown.
        UE_LOG(LogLDGrowth, Log, TEXT("LD_CARD_OFFER t=%lld contender=%s ordinal=%u source=%s level=%u cards=%s queued=%d"), NowMs,
            *LDLog::Id(ContenderId), uint32(Offer.Ordinal), *LDLog::EnumName(Offer.Source), uint32(Record.Build.Level),
            *FString::JoinBy(Offer.Options, TEXT(","), [](FName Id) { return Id.ToString(); }), Record.Progression.CardQueue.Num());
    }
}

void ALDDevelopmentDirector::DrawHead(const FGuid& ContenderId, FRecord& Record)
{
    DrawCardHead(ContenderId, Record);
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
    return LDProgression::CombatProjectionInput(Content.IsValid() ? &Content->Get() : nullptr, Build);
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
    // Every level opportunity of the match draws one head; the current head, if any, is already drawn.
    const TArray<TArray<FName>>* Queued = DevOfferQueue.Find(ContenderId);
    const int32 Drawn = int32(Record->Progression.ActiveConsumed) + (Record->Progression.Offer.IsSet() ? 1 : 0);
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
            // DEV only: the active there is forgotten (its attachments reclaimed like a rest replacement); Combat keeps its
            // cooldown track for a relearn.
            Forgotten = NewBuild.Actives[Occupied].Id;
            NewBuild.Actives.RemoveAt(Occupied);
            TArray<uint16> Reclaimed;
            LDProgression::ReclaimAttachments(NewBuild, Forgotten, Reclaimed);
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

ELDGrowthReason ALDDevelopmentDirector::DevGrantSkillPoint(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit,
    FString& Detail)
{
    Detail.Reset();
    FRecord* Record = Records.Find(ContenderId);
    if (!Record || !Content.IsValid())
    {
        Detail = TEXT("The contender has no growth record in this match.");
        return ELDGrowthReason::NotContender;
    }
    FLDGrowthBuild NewBuild = Record->Build;
    if (NewBuild.DevSkillPoints == MAX_int32)
    {
        return ELDGrowthReason::ArithmeticOverflow;
    }
    ++NewBuild.DevSkillPoints;
    FLDProgressionState NewProgression = Record->Progression;
    const ELDGrowthReason Reason = InstallBuild(ContenderId, *Record, MoveTemp(NewBuild), MoveTemp(NewProgression), false, Permit);
    if (Reason == ELDGrowthReason::None)
    {
        Detail = FString::Printf(TEXT("%d unspent skill points"), LDProgression::SkillPoints(Content->Get(), Record->Build));
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
    const ELDGrowthReason PointsReason = LDProgression::UnspentPoints(Definitions, Record->Build, Record->Progression.CumulativeXP,
        Unspent);
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

ELDGrowthReason ALDDevelopmentDirector::DevApplyBreakoutPreset(const FGuid& ContenderId, FName PresetId,
    const FLDGameplayWritePermit& Permit, FString& Detail)
{
    Detail.Reset();
    FRecord* Record = Records.Find(ContenderId);
    const FLDBreakoutPreset* Preset = LDBreakoutPresets::Find(PresetId);
    if (!Record || !Content.IsValid())
    {
        Detail = TEXT("The contender has no growth record in this match.");
        return ELDGrowthReason::NotContender;
    }
    if (!Preset)
    {
        Detail = FString::Printf(TEXT("'%s' is not a breakout preset."), *PresetId.ToString());
        return ELDGrowthReason::InvalidOption;
    }
    const FLDDemoContentDefinitions& Definitions = Content->Get();
    FLDGrowthBuild NewBuild;
    int64 XP = 0;
    if (!LDBreakoutPresets::Build(Definitions, *Preset, NewBuild, XP, Detail))
    {
        return ELDGrowthReason::DataInvalid;
    }
    // Serials keep rising across the match, so no earlier command names one of these cards.
    const uint16 Base = uint16(Record->Build.NextCardSerial - 1);
    if (int32(NewBuild.NextCardSerial) + int32(Base) > int32(MAX_uint16))
    {
        Detail = TEXT("The card serials are exhausted.");
        return ELDGrowthReason::CapacityExceeded;
    }
    for (FLDCardInstance& Card : NewBuild.Cards) { Card.Serial = uint16(Card.Serial + Base); }
    NewBuild.NextCardSerial = uint16(NewBuild.NextCardSerial + Base);
    FLDProgressionState NewProgression = Record->Progression;
    NewProgression.CumulativeXP = XP;
    NewProgression.ActiveEarned = LDProgression::ActiveOpportunitiesAt(Definitions.Progression, NewBuild.Level);
    NewProgression.ActiveConsumed = NewProgression.ActiveEarned;
    NewProgression.Offer.Reset();
    NewProgression.Records.Reset();
    for (uint8 Ordinal = 1; Ordinal <= NewProgression.ActiveEarned; ++Ordinal)
    {
        FLDActiveOpportunityRecord& Row = NewProgression.Records.AddDefaulted_GetRef();
        Row.Ordinal = Ordinal;
        Row.OfferId = FGuid::NewGuid();
        Row.Chosen = NewBuild.Actives.IsValidIndex(Ordinal - 1) ? NewBuild.Actives[Ordinal - 1].Id : NAME_None;
    }
    NewProgression.CardQueue.Reset();
    NewProgression.CardOffer.Reset();
    const ELDGrowthReason Reason = InstallBuild(ContenderId, *Record, MoveTemp(NewBuild), MoveTemp(NewProgression), true, Permit);
    if (Reason == ELDGrowthReason::None)
    {
        Detail = FString::Printf(TEXT("%s: level %u, %d cards installed"), Preset->Label, uint32(Record->Build.Level),
            LDProgression::InstalledCards(Definitions, Record->Build));
        UE_LOG(LogLDGrowth, Log, TEXT("LD_DEV_BREAKOUT_PRESET contender=%s preset=%s level=%u cards=%d skill_points=%d"),
            *LDLog::Id(ContenderId), Preset->Id, uint32(Record->Build.Level), LDProgression::InstalledCards(Definitions, Record->Build),
            LDProgression::SkillPoints(Definitions, Record->Build));
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
    // The opening card offer and the free rerolls again; card serials and offer revisions keep rising, so no old command
    // names a new card or offer.
    Prepared.NewBuild.NextCardSerial = Record->Build.NextCardSerial;
    Prepared.NewProgression.LastCardOfferRevision = Record->Progression.LastCardOfferRevision;
    Prepared.NewProgression.CardOffersEarned = Record->Progression.CardOffersEarned;
    if (LDProgression::QueueCardOffers(Prepared.NewProgression, ELDCardOfferSource::Opening, 1, Prepared.OfferFirst, Prepared.OfferLast)
        != ELDGrowthReason::None)
    {
        return DevelopmentPrepareFailure(ELDGrowthReason::CapacityExceeded, TEXT("The card offer ordinals are exhausted."));
    }
    Prepared.NewProgression.CardRerollsGranted = Content->Get().Parameters.Cards.FreeRerolls;
    FLDPrepareGrowthResult Result;
    Result.Value.Emplace(MoveTemp(Prepared));
    return Result;
}

#endif

bool ALDDevelopmentDirector::ReadFinalMod(const FGuid& ContenderId, FName& Out) const
{
    const FRecord* Record = Records.Find(ContenderId);
    Out = Record ? Record->Build.FinalMod.DefinitionId : NAME_None;
    return Record != nullptr;
}
