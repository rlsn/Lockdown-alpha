#include "Tests/LDDemoGrowthProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/LDCharacter.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDProgressionRules.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDEconomyLedger.h"
#include "Framework/LDCaptureTypes.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDMatchProviders.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDWorldViewRelay.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// Owned-connection growth/shop scenario (PROGRESSION_EQUIPMENT_ECONOMY §8, §12). The owning client earns, chooses and
// trades only through its own connection; the Host plays the victim and audits the ledger afterwards. The probe never
// calls a growth driver entry, a Commit*/OpenPending* handler or an _Implementation. Named, not anonymous: unity
// builds merge files.
namespace LDDemoGrowthScenario
{
// AttackUnit orders allowed per kill when the server ended a lock without a death (lost sight, blocked path).
constexpr int32 GrowthProbeAttackOrdersPerKill = 6;
constexpr int32 GrowthProbeNavigateAttempts = 4;
// A ground order may stop this far from its point when another body stands near it.
constexpr double GrowthProbeArrivalCm = 150.0;
// Bounded waits in seconds: one request's admission, final receipt and owner publication; one kill after its attack
// order; the victim's walk back after the approved respawn delay.
constexpr double GrowthProbeRequestSeconds = 20.0;
constexpr double GrowthProbeKillSeconds = 90.0;
constexpr double GrowthProbeReturnSeconds = 75.0;
// Ending a match, returning the room to a new Lobby generation or starting the next match.
constexpr double GrowthProbeTransitionSeconds = 60.0;
// A replay that must change nothing is watched this long before the owner state is compared again.
constexpr double GrowthProbeQuietSeconds = 1.5;

enum class EGrowthProbeWait : uint8 { Waiting, Ready, Failed };

double GrowthProbeRational(const FLDDemoRational& Value)
{
    return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : -1.0;
}

FString GrowthProbeGuid(const FGuid& Id)
{
    return Id.ToString(EGuidFormats::DigitsWithHyphensLower);
}

FString GrowthProbeReason(uint16 Raw)
{
    return LDGrowthWire::ReasonName(LDGrowthWire::Reason(Raw));
}

FString GrowthProbeProgressionText(const FLDProgressionView& View)
{
    return FString::Printf(TEXT("level=%u xp=%lld earned=%u consumed=%u pending=%u offer=%d progression=%llu build=%llu"),
        uint32(View.Level), View.CumulativeXP, uint32(View.EarnedThrough), uint32(View.ConsumedThrough),
        uint32(View.PendingChoiceCount), View.bHasOffer ? 1 : 0, View.ProgressionRevision, View.BuildRevision);
}

FString GrowthProbeShopText(const FLDShopReceipt& Receipt)
{
    return FString::Printf(TEXT("request=%llu outcome=%u reason=%s instance=%s serial=%llu slot=%u delta=%lld ledger=%llu batch=%llu"),
        Receipt.Header.RequestId, uint32(Receipt.Header.Outcome), *GrowthProbeReason(Receipt.Header.Reason),
        *GrowthProbeGuid(Receipt.Instance), Receipt.IssueSerial, uint32(Receipt.SlotIndex), Receipt.ActualGoldDelta,
        Receipt.LedgerSerial, Receipt.Header.CommitBatch);
}

bool GrowthProbeSameRevisions(const FLDResultRevisions& A, const FLDResultRevisions& B)
{
    return A.bHasAccount == B.bHasAccount && A.AccountRevision == B.AccountRevision && A.bHasProgression == B.bHasProgression
        && A.ProgressionRevision == B.ProgressionRevision && A.bHasBuild == B.bHasBuild && A.BuildRevision == B.BuildRevision
        && A.bHasInventory == B.bHasInventory && A.InventoryRevision == B.InventoryRevision && A.bHasOffer == B.bHasOffer
        && A.OfferRevision == B.OfferRevision;
}

bool GrowthProbeSameHeader(const FLDGrowthReceiptHeader& A, const FLDGrowthReceiptHeader& B)
{
    return A.Generation == B.Generation && A.Family == B.Family && A.RequestId == B.RequestId && A.OriginalLife == B.OriginalLife
        && A.OriginalLifePhase == B.OriginalLifePhase && A.Outcome == B.Outcome && A.Reason == B.Reason
        && A.bHasCommitBatch == B.bHasCommitBatch && A.CommitBatch == B.CommitBatch
        && GrowthProbeSameRevisions(A.Revisions, B.Revisions);
}

bool GrowthProbeSameChoice(const FLDChoiceReceipt& A, const FLDChoiceReceipt& B)
{
    return GrowthProbeSameHeader(A.Header, B.Header) && A.OfferId == B.OfferId && A.OptionId == B.OptionId
        && A.OpportunityOrdinal == B.OpportunityOrdinal && A.ConsumedThrough == B.ConsumedThrough && A.Kind == B.Kind
        && A.DefinitionId == B.DefinitionId && A.NewRank == B.NewRank && A.bReplaced == B.bReplaced
        && A.ReplacedSlot == B.ReplacedSlot && A.ReplacedSkillId == B.ReplacedSkillId && A.ReplacedRank == B.ReplacedRank
        && A.bHasNextOffer == B.bHasNextOffer && A.NextOfferId == B.NextOfferId;
}

bool GrowthProbeSameShop(const FLDShopReceipt& A, const FLDShopReceipt& B)
{
    return GrowthProbeSameHeader(A.Header, B.Header) && A.Kind == B.Kind && A.Instance == B.Instance
        && A.IssueSerial == B.IssueSerial && A.DefinitionId == B.DefinitionId && A.SlotIndex == B.SlotIndex
        && A.ActualGoldDelta == B.ActualGoldDelta && A.bHasLedgerSerial == B.bHasLedgerSerial && A.LedgerSerial == B.LedgerSerial
        && A.bHasSource == B.bHasSource && A.IssuedCooldownId == B.IssuedCooldownId && A.ReadyAtMs == B.ReadyAtMs;
}

bool GrowthProbeSameSlot(const FLDEquipmentSlotView& A, const FLDEquipmentSlotView& B)
{
    return A.SlotIndex == B.SlotIndex && A.bOccupied == B.bOccupied && A.Instance == B.Instance && A.IssueSerial == B.IssueSerial
        && A.DefinitionId == B.DefinitionId && A.ItemRevision == B.ItemRevision;
}

const FLDEquipmentSlotView* GrowthProbeSlot(const TArray<FLDEquipmentSlotView>& Rows, uint8 Wanted)
{
    return Rows.FindByPredicate([Wanted](const FLDEquipmentSlotView& SlotRow) { return SlotRow.SlotIndex == Wanted; });
}

// Equipment slot 0 is the Item1 entry and slot 1 is Item2 (the Combat projection input of the growth module).
ELDCombatEntry GrowthProbeItemEntry(uint8 SlotIndex)
{
    return SlotIndex == 0 ? ELDCombatEntry::Item1 : ELDCombatEntry::Item2;
}

const FLDCombatEntryView* GrowthProbeEntry(const FLDCombatOwnerView& Own, ELDCombatEntry Wanted)
{
    return Own.Entries.FindByPredicate([Wanted](const FLDCombatEntryView& EntryRow) { return EntryRow.Entry == Wanted; });
}

// Owner growth state a stale request must leave untouched: level, XP, opportunities, build, gold, revisions and slots.
bool GrowthProbeSameOwner(const FLDProgressionView& ProgA, const FLDEquipmentView& EquipA, const FLDProgressionView& ProgB,
    const FLDEquipmentView& EquipB)
{
    bool bSame = ProgA.Level == ProgB.Level && ProgA.CumulativeXP == ProgB.CumulativeXP
        && ProgA.ProgressionRevision == ProgB.ProgressionRevision && ProgA.BuildRevision == ProgB.BuildRevision
        && ProgA.EarnedThrough == ProgB.EarnedThrough && ProgA.ConsumedThrough == ProgB.ConsumedThrough
        && ProgA.PendingChoiceCount == ProgB.PendingChoiceCount && ProgA.bHasOffer == ProgB.bHasOffer
        && ProgA.Skills.Num() == ProgB.Skills.Num() && ProgA.AttributeCards.Num() == ProgB.AttributeCards.Num()
        && EquipA.Gold == EquipB.Gold && EquipA.AccountRevision == EquipB.AccountRevision
        && EquipA.InventoryRevision == EquipB.InventoryRevision && EquipA.Slots.Num() == EquipB.Slots.Num();
    for (int32 SlotIndex = 0; bSame && SlotIndex < EquipA.Slots.Num(); ++SlotIndex)
    {
        bSame = GrowthProbeSameSlot(EquipA.Slots[SlotIndex], EquipB.Slots[SlotIndex]);
    }
    return bSame;
}

// The viewer's current authorized observation frame, or null while it is unavailable or stale.
const FLDWorldView* GrowthProbeFrame(const ALDPlayerController& Viewer)
{
    const ALDWorldViewRelay* Relay = Viewer.LocalView();
    if (!Relay) { return nullptr; }
    const FLDObservedFrameRead FrameRead = Relay->ReadObservedFrame();
    return FrameRead.Status == ELDObservedViewStatus::Current ? FrameRead.Frame : nullptr;
}

const FLDObservedUnitView* GrowthProbeContender(const FLDWorldView& View, const FGuid& ContenderId)
{
    return View.Units.FindByPredicate([&ContenderId](const FLDObservedUnitView& Candidate)
    {
        return Candidate.Kind == ELDObservedUnitKind::Contender && Candidate.ContenderId == ContenderId;
    });
}

bool GrowthProbeProtected(const FLDObservedUnitView& Seen)
{
    const uint8 Protection = uint8(ELDDemoStatusId::RespawnProtection);
    return Seen.Statuses.ContainsByPredicate([Protection](const FLDObservedStatus& Cue) { return Cue.StatusId == Protection; });
}

TArray<TSharedPtr<FJsonValue>> GrowthProbeItems(const FLDEquipmentView& View)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const FLDEquipmentSlotView& SlotRow : View.Slots)
    {
        if (!SlotRow.bOccupied) { continue; }
        const TSharedRef<FJsonObject> ItemRow = MakeShared<FJsonObject>();
        ItemRow->SetStringField(TEXT("instance"), GrowthProbeGuid(SlotRow.Instance));
        ItemRow->SetStringField(TEXT("definition"), SlotRow.DefinitionId.ToString());
        ItemRow->SetNumberField(TEXT("slot"), SlotRow.SlotIndex);
        ItemRow->SetNumberField(TEXT("issue_serial"), double(SlotRow.IssueSerial));
        ItemRow->SetNumberField(TEXT("item_revision"), double(SlotRow.ItemRevision));
        Rows.Add(MakeShared<FJsonValueObject>(ItemRow));
    }
    return Rows;
}
}

// Global name, matching the other DEV probe classes; it only uses public game mode, controller and growth reads.
class FLDDemoGrowthProbe : public TSharedFromThis<FLDDemoGrowthProbe>
{
public:
    enum class EStage : uint8
    {
        Join, Lobby, Spawned, Victim,
        ToSite, KillSeek, KillWait, Choice, ChoiceQuery,
        ShopApproach, ShopOpen, Buy1, Buy2, BuyFull, Replay, Activate, Sell, WalkOut, Outside,
        // New-match isolation: the Host ends the first match, the room returns to a new Lobby generation and a
        // second normal match must start from clean owner state.
        EndFirst, AwaitLobby, SecondLobby, SecondSpawned, SecondReplay, SecondQuote, SecondAudit
    };

    // One shop request: the exact terms sent and the owner state it must change or leave untouched.
    struct FShopCall
    {
        FString Step;
        FLDShopCommand Sent;
        int32 SubmitMark = 0;
        int32 ResultMark = 0;
        uint64 PublishMark = 0;
        uint64 PeekBefore = 0;
        int64 GoldBefore = 0;
        uint64 AccountBefore = 0;
        uint64 InventoryBefore = 0;
        uint64 BuildBefore = 0;
        TArray<FLDEquipmentSlotView> SlotsBefore;
        double SentAt = 0;
        double QuietUntil = 0;
        bool bSent = false;
    };

    TWeakObjectPtr<ALDPlayerController> Controller;
    TWeakObjectPtr<UWorld> World;
    FString Output, Role, Error, CatalogDigest, StartBlocker;
    double Deadline = 0, StageAt = 0, NextCommandAt = 0, NextReadyAt = 0, StartRequestedAt = 0;
    EStage Stage = EStage::Join;
    bool Passed = true, Written = false, bJoined = false, bLobbyConfigured = false, bStartIssued = false, bSitesReady = false;
    bool bLedgerChecked = false;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TArray<TSharedPtr<FJsonValue>> Checks;

    // Public anchors and approved radii: the duel happens beside the shop, outside its reach and the capture circle.
    FName ShopId;
    FVector ShopCentre = FVector::ZeroVector, KillSite = FVector::ZeroVector, HostSite = FVector::ZeroVector;
    double ShopReach = 0, RespawnSeconds = 0;
    FGuid HostId, ClientId;

    // Owning-client notifications in arrival order; listeners only record.
    TArray<FLDSubmitResult> Submits;
    TArray<FLDChoiceReceiptQueryResult> ChoiceResults;
    TArray<FLDShopReceiptQueryResult> ShopResults;
    TArray<FLDShopOpenResult> OpenResults;
    TArray<TPair<FName, uint32>> Arrivals;
    FDelegateHandle SubmitHandle, ChoiceHandle, ShopHandle, OpenHandle, ArrivalHandle;

    // Client1 plan: item A is the cheapest approved equipment (ties by id); goal and kill count come from the catalog.
    FName ItemId;
    int64 ItemBuyPrice = 0, ItemSellGold = 0, GoldGoal = 0;
    int32 KillsPlanned = 0, Kills = 0, AttackOrders = 0, NavigateAttempts = 0, ApproachAttempts = 0;
    int64 KillXP = 0, KillGold = 0, GoldBeforeKill = 0;
    uint8 VictimLevel = 0;
    FLDMoveIdentity AttackOrder;
    FLDProgressionView KillBefore;
    double KillStartedAt = 0;
    TArray<TSharedPtr<FJsonValue>> KillRows, ShopRows;

    // Client1: the single choice and its receipt query.
    bool bChoiceDone = false, bChoiceSent = false, bQuerySent = false;
    FLDProgressionView ChoiceBefore;
    FLDChoiceOption ChoicePicked;
    FLDChoiceCommand ChoiceSent;
    FLDChoiceReceipt ChoiceFinal;
    int32 ChoiceSubmitMark = 0, ChoiceResultMark = 0;
    uint64 ChoicePublishMark = 0;
    TSharedPtr<FJsonObject> ChoiceRow;

    // Client1: the shop.
    FShopCall Call;
    bool bApproachSent = false;
    int32 OpenMark = 0, ArrivalMark = 0;
    FLDMoveIdentity ApproachOrder;
    FLDShopOpenResult Opened;
    FLDShopCommand Buy1Sent;
    FLDShopReceipt Buy1Receipt, Buy2Receipt;

    // Client1: the first copy's active puts only its own persistent track on cooldown; selling it leaves Combat's debt.
    bool bActivateSent = false;
    uint32 ActivateReceiptMark = 0;
    FGuid UsedTrack, KeptTrack;
    int64 UsedReadyAtMs = 0, FirstDebtReadyAtMs = 0;

    // Host: the victim's current life.
    uint32 VictimLife = 0;

    // New-match isolation. The first match's identities and dirty owner state, then the second match's checks.
    bool bSecondMatch = false, bResultChecked = false, bReplaySent = false;
    uint32 FirstGeneration = 0, SecondGeneration = 0;
    FLDCombatBodyRef FirstBody;
    FGuid FirstLedgerMatch, FirstSessionId;
    TSharedPtr<FJsonObject> FirstMatchRow;
    FLDProgressionView FirstProgression;
    FLDEquipmentView FirstEquipment;
    FLDProgressionView ReplayProgBefore;
    FLDEquipmentView ReplayEquipBefore;
    int32 ReplaySubmitMark = 0, ReplayResultMark = 0;
    double ReplaySentAt = 0, ReplayQuietUntil = 0;

    bool IsHost() const
    {
        return Role == TEXT("Host");
    }

    void SetStage(EStage Next)
    {
        Stage = Next;
        StageAt = FPlatformTime::Seconds();
    }

    bool Check(bool bValue, const TCHAR* Id, const FString& Name, const FString& Detail = FString())
    {
        const TSharedRef<FJsonObject> CheckRow = MakeShared<FJsonObject>();
        CheckRow->SetStringField(TEXT("id"), Id);
        CheckRow->SetStringField(TEXT("name"), Name);
        CheckRow->SetBoolField(TEXT("passed"), bValue);
        CheckRow->SetStringField(TEXT("detail"), Detail);
        Checks.Add(MakeShared<FJsonValueObject>(CheckRow));
        Passed &= bValue;
        if (!bValue && Error.IsEmpty())
        {
            Error = FString(Id) + TEXT(" ") + Name + (Detail.IsEmpty() ? FString() : TEXT(": ") + Detail);
        }
        return bValue;
    }

    bool Fail(const TCHAR* Id, const FString& Why)
    {
        Check(false, Id, Why);
        return Finish();
    }

    bool SaveText(const FString& Text, const FString& Name) const
    {
        const FString File = FPaths::Combine(Output, Name);
        return FFileHelper::SaveStringToFile(Text, *(File + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*File, *(File + TEXT(".tmp")), true, true);
    }

    bool Save(const TSharedRef<FJsonObject>& Value, const FString& Name) const
    {
        FString Text;
        return FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Text)) && SaveText(Text, Name);
    }

    bool Mark(const FString& Name)
    {
        const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetBoolField(TEXT("ready"), true);
        return Save(Value, Name) || Check(false, TEXT("Net"), TEXT("The isolated scenario could not persist its marker ") + Name);
    }

    bool Exists(const FString& Name) const
    {
        return FPaths::FileExists(FPaths::Combine(Output, Name));
    }

    // Records the owning controller's growth/shop notifications; the module's own handlers stay bound as well.
    void Bind(ALDPlayerController& Subject)
    {
        const TWeakPtr<FLDDemoGrowthProbe> Weak = AsShared();
        SubmitHandle = Subject.OnGrowthSubmit.AddLambda([Weak](const FLDSubmitResult& Received)
        {
            if (const TSharedPtr<FLDDemoGrowthProbe> Pinned = Weak.Pin()) { Pinned->Submits.Add(Received); }
        });
        ChoiceHandle = Subject.OnChoiceReceipt.AddLambda([Weak](const FLDChoiceReceiptQueryResult& Received)
        {
            if (const TSharedPtr<FLDDemoGrowthProbe> Pinned = Weak.Pin()) { Pinned->ChoiceResults.Add(Received); }
        });
        ShopHandle = Subject.OnShopReceipt.AddLambda([Weak](const FLDShopReceiptQueryResult& Received)
        {
            if (const TSharedPtr<FLDDemoGrowthProbe> Pinned = Weak.Pin()) { Pinned->ShopResults.Add(Received); }
        });
        OpenHandle = Subject.OnShopOpenResult.AddLambda([Weak](const FLDShopOpenResult& Received)
        {
            if (const TSharedPtr<FLDDemoGrowthProbe> Pinned = Weak.Pin()) { Pinned->OpenResults.Add(Received); }
        });
        ArrivalHandle = Subject.Navigation->OnShopArrived.AddLambda([Weak](FName ArrivedShopId, uint32 ArrivedLife)
        {
            if (const TSharedPtr<FLDDemoGrowthProbe> Pinned = Weak.Pin()) { Pinned->Arrivals.Emplace(ArrivedShopId, ArrivedLife); }
        });
    }

    void Unbind()
    {
        ALDPlayerController* Subject = Controller.Get();
        if (!Subject) { return; }
        Subject->OnGrowthSubmit.Remove(SubmitHandle);
        Subject->OnChoiceReceipt.Remove(ChoiceHandle);
        Subject->OnShopReceipt.Remove(ShopHandle);
        Subject->OnShopOpenResult.Remove(OpenHandle);
        if (Subject->Navigation) { Subject->Navigation->OnShopArrived.Remove(ArrivalHandle); }
    }

    const FLDSubmitResult* FindSubmit(ELDGrowthFamily Family, uint64 RequestId, int32 From) const
    {
        for (int32 SubmitIndex = From; SubmitIndex < Submits.Num(); ++SubmitIndex)
        {
            if (Submits[SubmitIndex].Family == Family && Submits[SubmitIndex].RequestId == RequestId)
            {
                return &Submits[SubmitIndex];
            }
        }
        return nullptr;
    }

    const FLDShopReceipt* FindShopFinal(uint64 RequestId, int32 From) const
    {
        for (int32 ResultIndex = From; ResultIndex < ShopResults.Num(); ++ResultIndex)
        {
            const FLDShopReceiptQueryResult& Answer = ShopResults[ResultIndex];
            if (Answer.Lookup == ELDReceiptLookup::Final && Answer.bHasReceipt && Answer.Query.Family == ELDGrowthFamily::Shop
                && Answer.Query.RequestId == RequestId)
            {
                return &Answer.Receipt;
            }
        }
        return nullptr;
    }

    // Any lookup answer for this choice request, final or not.
    const FLDChoiceReceiptQueryResult* FindChoiceAnswer(uint64 RequestId, int32 From) const
    {
        for (int32 ResultIndex = From; ResultIndex < ChoiceResults.Num(); ++ResultIndex)
        {
            if (ChoiceResults[ResultIndex].Query.Family == ELDGrowthFamily::Choice
                && ChoiceResults[ResultIndex].Query.RequestId == RequestId)
            {
                return &ChoiceResults[ResultIndex];
            }
        }
        return nullptr;
    }

    bool Tick()
    {
        const double Now = FPlatformTime::Seconds();
        if (Written) { return TickAfterReport(); }
        if (Now >= Deadline)
        {
            return Fail(TEXT("Net"), FString::Printf(TEXT("The scenario did not finish within its bounded deadline (stage %d)"), int32(Stage)));
        }
        ALDPlayerController* PC = Controller.Get();
        UWorld* Current = World.Get();
        if (!PC || !Current)
        {
            return Fail(TEXT("Net"), TEXT("The owning controller or its world ended before the scenario finished"));
        }
        const ALDGameState* Public = Current->GetGameState<ALDGameState>();
        const ULDMapRegistry* Registry = Current->GetSubsystem<ULDMapRegistry>();
        if (!Public || !Registry || !Registry->Enabled()) { return true; }
        if (!Catalog)
        {
            FString LoadError;
            if (!LDDemoEntry::LoadRuntimeContent(Catalog, LoadError) || !Catalog
                || !LDDemoEntry::VerifyRuntimeFiles(LoadError, &CatalogDigest))
            {
                return Fail(TEXT("Start"), TEXT("The approved runtime content is unavailable: ") + LoadError);
            }
        }
        const UNetDriver* Driver = Current->GetNetDriver();
        if (Stage != EStage::Join)
        {
            const bool bConnected = IsHost() ? Driver && Driver->ClientConnections.Num() == 1
                : Driver && Driver->ServerConnection != nullptr;
            if (!bConnected) { return Fail(TEXT("Net"), TEXT("The owning connection dropped before the scenario finished")); }
        }
        const FLDLobbyView& Lobby = Public->DemoLobby;
        // The buyer stopped early on a failed check: the Host closes its own report instead of waiting for markers.
        if (IsHost() && Exists(TEXT("Client1.json")) && !Exists(TEXT("Client1Match2Done.json")))
        {
            return Fail(TEXT("Net"), TEXT("The buyer ended its scenario early; see Client1.json for its failed check"));
        }
        switch (Stage)
        {
        case EStage::Join: return TickJoin(*Current, *PC, Lobby, Driver);
        case EStage::Lobby: return TickLobby(Now, *PC, Lobby);
        case EStage::Spawned: return TickSpawned(*PC, *Registry, Lobby);
        case EStage::Victim: return TickVictim(Now, *PC);
        case EStage::EndFirst: return TickEndFirst(Now, *PC, *Public);
        case EStage::AwaitLobby: return TickAwaitLobby(Now, *PC, *Public);
        case EStage::SecondLobby: return TickLobby(Now, *PC, Lobby);
        case EStage::SecondSpawned: return TickSecondSpawned(Now, *PC, Lobby);
        case EStage::SecondAudit: return TickSecondAudit(*PC);
        default: return TickClient(Now, *PC, Lobby);
        }
    }

    bool TickJoin(const UWorld& InWorld, ALDPlayerController& PC, const FLDLobbyView& Lobby, const UNetDriver* Driver)
    {
        if (!PC.IsLocalController() || !PC.GetLocalPlayer() || !Lobby.Generation) { return true; }
        if (!bJoined)
        {
            if (!Mark(Role + TEXT("Joined.json")) || (IsHost() && !Mark(TEXT("HostReady.json")))) { return Finish(); }
            bJoined = true;
        }
        if (IsHost())
        {
            if (!Exists(TEXT("Client1Joined.json")) || Lobby.Members.Num() != 2 || !Driver || Driver->ClientConnections.Num() != 1)
            {
                return true;
            }
            Check(InWorld.GetNetMode() == NM_ListenServer && PC.HasAuthority() && InWorld.GetAuthGameMode<ALDGameMode>() != nullptr,
                TEXT("Net"), TEXT("A participating Listen Host with its own local player serves one real owning connection"));
        }
        else
        {
            Check(InWorld.GetNetMode() == NM_Client && !PC.HasAuthority() && PC.GetNetConnection() != nullptr, TEXT("Net"),
                TEXT("Client1 plays through a real owning remote connection"));
        }
        if (!Passed) { return Finish(); }
        SetStage(EStage::Lobby);
        return true;
    }

    // The ordinary lobby through owning session RPCs: no AI seats, every human ready.
    bool TickLobby(double Now, ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        const bool bSecond = Stage == EStage::SecondLobby;
        const TCHAR* Id = bSecond ? TEXT("Iso") : TEXT("Start");
        if (bSecond && Lobby.Generation != SecondGeneration)
        {
            return Fail(Id, FString::Printf(TEXT("The room left Lobby generation %u for %u before the second match started"),
                SecondGeneration, Lobby.Generation));
        }
        if (Lobby.Phase == ELDMatchPhase::Development || Lobby.Phase == ELDMatchPhase::CaptureOpen)
        {
            SetStage(bSecond ? EStage::SecondSpawned : EStage::Spawned);
            return true;
        }
        if (Lobby.Phase == ELDMatchPhase::Starting) { return true; }
        if (Lobby.Phase != ELDMatchPhase::Lobby)
        {
            return Fail(Id, TEXT("The room left the Lobby without opening play: ")
                + StaticEnum<ELDMatchPhase>()->GetNameStringByValue(int64(Lobby.Phase)));
        }
        const FString Configured = bSecond ? TEXT("LobbyConfigured2.json") : TEXT("LobbyConfigured.json");
        if (IsHost() && !bLobbyConfigured)
        {
            if (Lobby.AICount != 0)
            {
                if (Now >= NextCommandAt)
                {
                    PC.SetDemoAICount(0);
                    NextCommandAt = Now + 1;
                }
                return true;
            }
            if (!Mark(Configured)) { return Finish(); }
            bLobbyConfigured = true;
        }
        if (!Exists(Configured)) { return true; }
        if (!PC.OwnerState.bReady && Now >= NextReadyAt)
        {
            PC.SetDemoReady(true);
            NextReadyAt = Now + 1;
        }
        if (!IsHost()) { return true; }
        const bool bAllReady = Lobby.Members.Num() == 2 && !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& LobbyMember)
        {
            return LobbyMember.Kind != ELDContenderKind::Human || !LobbyMember.Ready;
        });
        if (!bAllReady) { return true; }
        return RequestStart(Now, PC, Lobby);
    }

    // Every normal-start provider is registered, so the Host presses Start through its own session command once the
    // room reports CanStart: ProcessSessionCommand runs the provider gate, the real initializer, every provider Prepare
    // (Progression opens the ledger and growth records) and the initial spawn.
    bool RequestStart(double Now, ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        const TCHAR* Id = bSecondMatch ? TEXT("Iso") : TEXT("Start");
        if (!bStartIssued)
        {
            bStartIssued = true;
            StartRequestedAt = Now;
            FString InputError;
            Check(LDDemoEntry::VerifyRuntimeFiles(InputError) && LDDemoEntry::VerifyWorldPackages(InputError), Id,
                TEXT("Approved runtime files and world packages verify before the start"), InputError);
            const FLDMatchProviderHooks* ProgressionHooks = LDMatchProviders::Find(ELDMatchProvider::Progression);
            const FLDMatchProviderHooks* ShopHooks = LDMatchProviders::Find(ELDMatchProvider::Shop);
            Check(ProgressionHooks && ProgressionHooks->Prepare && ProgressionHooks->ReadFinalLevel && ShopHooks && ShopHooks->Prepare,
                Id, TEXT("Progression and Shop are registered normal-start providers"));
            const FString Missing = LDMatchProviders::MissingForStart(false);
            Check(Missing.IsEmpty(), Id, TEXT("Every normal-start provider is registered, so the room starts normally"),
                FString::Printf(TEXT("missing=[%s]"), *Missing));
            if (!Passed) { return Finish(); }
        }
        StartBlocker = Lobby.StartBlocker;
        if (!Lobby.CanStart)
        {
            return Now - StartRequestedAt < 30.0 ? true
                : Fail(Id, TEXT("Every human is ready but the room still refuses the start: ") + StartBlocker);
        }
        if (Now >= NextCommandAt)
        {
            PC.StartDemoMatch();
            NextCommandAt = Now + 2.0;
        }
        return true;
    }

    // Host, first spawned frame: the normal start's Progression Prepare opened the ledger for the locked roster.
    bool CheckStartLedger(const ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        bLedgerChecked = true;
        const ALDGameMode* Mode = PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
        const LDEconomy::FLedger* Book = Mode && Mode->Economy ? &Mode->Economy->GetLedger() : nullptr;
        bool bAccounts = Book && Book->IsInitialized() && Mode->Economy->GetMatch() == Mode->GetMatchContext()
            && Book->State().Accounts.Num() == Lobby.Members.Num();
        for (const FLDLobbyMember& LobbyMember : Lobby.Members)
        {
            const LDEconomy::FAccount* Account = Book ? Book->Account(LobbyMember.ContenderId) : nullptr;
            bAccounts &= Account && Account->Gold == Catalog->Get().Parameters.StartingGold;
        }
        return Check(bAccounts, TEXT("Start"),
            TEXT("The Progression provider opened one ledger account per locked contender at the approved starting gold"));
    }

    bool TickSpawned(ALDPlayerController& PC, const ULDMapRegistry& Registry, const FLDLobbyView& Lobby)
    {
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        if (!Cast<ALDCharacter>(PC.GetPawn()) || PC.OwnerState.LifePhase != ELDLifePhase::Alive || !PC.OwnerState.BodyRef.IsValid()
            || !PC.CanWorldCommand() || !ProgView || !EquipView)
        {
            return true;
        }
        if (!ResolveRoster(PC, Lobby) || !PrepareSites(Registry)) { return Finish(); }
        if (IsHost() && !bLedgerChecked && !CheckStartLedger(PC, Lobby)) { return Finish(); }
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        bool bSlotsEmpty = EquipView->Slots.Num() == Defs.Progression.EquipmentSlots;
        for (const FLDEquipmentSlotView& SlotRow : EquipView->Slots)
        {
            bSlotsEmpty &= !SlotRow.bOccupied;
        }
        Check(PC.OwnerState.Life == 1 && PC.OwnerState.ProtectedUntilMs == 0, TEXT("Start"),
            TEXT("The first body is an unprotected initial life"));
        Check(ProgView->Level == 1 && ProgView->CumulativeXP == Defs.Progression.XPStart && ProgView->EarnedThrough == 0
            && ProgView->PendingChoiceCount == 0 && !ProgView->bHasOffer && ProgView->CatalogDigest == Defs.Identity.Root,
            TEXT("Start"), TEXT("Growth starts at level 1 with the approved XP start, no opportunity and the current catalog"),
            LDDemoGrowthScenario::GrowthProbeProgressionText(*ProgView));
        Check(EquipView->Gold == Defs.Parameters.StartingGold && bSlotsEmpty && PC.ReadOwnShop() == nullptr, TEXT("Start"),
            TEXT("The account starts at the approved starting gold with two explicit empty slots and no shop session"),
            FString::Printf(TEXT("gold=%lld slots=%d"), EquipView->Gold, EquipView->Slots.Num()));
        if (!Passed) { return Finish(); }
        if (IsHost())
        {
            SetStage(EStage::Victim);
            return true;
        }
        if (!PlanKills()) { return Finish(); }
        PC.Navigation->Navigate(KillSite);
        NavigateAttempts = 1;
        SetStage(EStage::ToSite);
        return true;
    }

    bool ResolveRoster(const ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        HostId.Invalidate();
        ClientId.Invalidate();
        int32 Humans = 0;
        for (const FLDLobbyMember& LobbyMember : Lobby.Members)
        {
            if (LobbyMember.Kind != ELDContenderKind::Human) { continue; }
            ++Humans;
            if (LobbyMember.Host)
            {
                HostId = LobbyMember.ContenderId;
            }
            else
            {
                ClientId = LobbyMember.ContenderId;
            }
        }
        const FGuid& OwnId = IsHost() ? HostId : ClientId;
        return Check(Humans == 2 && HostId.IsValid() && ClientId.IsValid() && HostId != ClientId
            && OwnId == PC.OwnerState.ContenderId, TEXT("Start"), TEXT("The public roster names this owner and exactly one other human"));
    }

    // Both roles derive the same two duel sites from public anchors and approved radii, then prove them legal.
    bool PrepareSites(const ULDMapRegistry& Registry)
    {
        if (bSitesReady) { return true; }
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        ShopId = FName(*Defs.World.ShopId);
        const FLDMapBinding* ShopAnchor = Registry.Resolve(ShopId, Registry.Generation);
        const FLDMapBinding* CaptureAnchor = Registry.Resolve(FName(*Defs.World.CaptureId), Registry.Generation);
        ShopReach = LDDemoGrowthScenario::GrowthProbeRational(Defs.Parameters.ShopInteractRadiusCm);
        const double CaptureReach = LDDemoGrowthScenario::GrowthProbeRational(Defs.Parameters.CaptureRadiusCm);
        const double BodyRadius = LDDemoGrowthScenario::GrowthProbeRational(Defs.Parameters.CollisionRadiusCm);
        const double AttackReach = LDDemoGrowthScenario::GrowthProbeRational(Defs.Parameters.AttackRangeCm.Base);
        RespawnSeconds = double(Defs.Parameters.RespawnMs) / 1000.0;
        if (!ShopAnchor || !CaptureAnchor || ShopReach <= 0 || CaptureReach <= 0 || BodyRadius <= 0 || AttackReach <= 0
            || RespawnSeconds <= 0)
        {
            return Check(false, TEXT("Start"), TEXT("The public shop and capture anchors and the approved radii exist"));
        }
        ShopCentre = ShopAnchor->Position;
        const FVector Outward = (ShopAnchor->Position - CaptureAnchor->Position).GetSafeNormal2D();
        const FVector Across(Outward.Y, -Outward.X, 0.0);
        KillSite = ShopAnchor->Position + Across * (2.0 * ShopReach);
        HostSite = KillSite - Outward * AttackReach;
        FCollisionShape Capsule;
        FString SiteError;
        bool bLegal = !Outward.IsNearlyZero() && ULDMapRegistry::ContentCapsule(*Catalog, Capsule, SiteError);
        for (const FVector& Site : {KillSite, HostSite})
        {
            FLDLegalGround LegalSite;
            ELDWorldQueryFailure SiteFailure = ELDWorldQueryFailure::None;
            bool bWithin = true;
            bLegal = bLegal
                && Registry.QueryGround(Registry.Revision(), Site, Capsule, ShopAnchor->Surface, LegalSite, SiteFailure, SiteError)
                && Registry.QueryShopReach(Registry.Revision(), Site, bWithin, SiteFailure, SiteError) && !bWithin
                && FVector::Dist2D(Site, CaptureAnchor->Position) > CaptureReach + 2.0 * BodyRadius;
        }
        bSitesReady = bLegal;
        return Check(bLegal, TEXT("Start"), TEXT("Both duel sites are legal ground outside the shop reach and the capture circle"),
            FString::Printf(TEXT("kill=%s host=%s %s"), *KillSite.ToCompactString(), *HostSite.ToCompactString(), *SiteError));
    }

    bool PlanKills()
    {
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        // Item A: the cheapest approved equipment (ties by id) whose active needs no target, so the buyer can put a real
        // persistent cooldown track in motion and the next match can prove it was not carried over.
        const FLDDemoEquipmentDefinition* Cheapest = nullptr;
        for (const FLDDemoEquipmentDefinition& Candidate : Defs.Equipment)
        {
            if (!Candidate.HasActive || Candidate.Active.Target.Mode != ELDDemoTargetMode::Self || Candidate.Active.Rank.CooldownMs <= 0)
            {
                continue;
            }
            if (!Cheapest || Candidate.BuyPrice < Cheapest->BuyPrice
                || (Candidate.BuyPrice == Cheapest->BuyPrice && Candidate.Id.Compare(Cheapest->Id, ESearchCase::CaseSensitive) < 0))
            {
                Cheapest = &Candidate;
            }
        }
        LDProgression::FRewardAmounts Bounty;
        const ELDGrowthReason BountyReason = LDProgression::ContenderKillReward(Defs.Parameters, uint8(Defs.Progression.MaxLevel), 1, Bounty);
        if (!Check(Cheapest && Cheapest->BuyPrice > 0 && BountyReason == ELDGrowthReason::None && Bounty.Gold > 0 && Bounty.XP > 0,
            TEXT("N03"), TEXT("The catalog sells an item with an untargeted, cooling active and pays a positive bounty for a level-1 contender"),
            LDGrowthWire::ReasonName(BountyReason)))
        {
            return false;
        }
        ItemId = FName(*Cheapest->Id);
        ItemBuyPrice = Cheapest->BuyPrice;
        ItemSellGold = Cheapest->SellGold;
        // The vendor reports a full inventory before the balance, so two held copies are enough for the
        // third Buy to prove the EquipmentFull refusal.
        GoldGoal = 2 * ItemBuyPrice;
        const int64 Shortfall = GoldGoal - Defs.Parameters.StartingGold;
        KillsPlanned = Shortfall <= 0 ? 1 : int32((Shortfall + Bounty.Gold - 1) / Bounty.Gold);
        return true;
    }

    // ---- Host: the victim never attacks; it walks back to its duel site after every respawn. ----

    bool TickVictim(double Now, ALDPlayerController& PC)
    {
        ALDGameMode* Mode = PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
        if (!Mode) { return Fail(TEXT("Net"), TEXT("The Listen Host lost its authoritative game mode")); }
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        if (PC.OwnerState.LifePhase == ELDLifePhase::Alive && Body && PC.CanWorldCommand())
        {
            if (PC.OwnerState.BodyRef.IsValid()) { FirstBody = PC.OwnerState.BodyRef; }
            if (PC.OwnerState.Life != VictimLife)
            {
                VictimLife = PC.OwnerState.Life;
                NavigateAttempts = 0;
                NextCommandAt = 0;
            }
            const bool bAtSite = FVector::Dist2D(Body->GetNavAgentLocation(), HostSite) <= LDDemoGrowthScenario::GrowthProbeArrivalCm;
            if (!bAtSite && !PC.Navigation->HasMoveOrder() && Now >= NextCommandAt)
            {
                if (NavigateAttempts >= LDDemoGrowthScenario::GrowthProbeNavigateAttempts)
                {
                    return Fail(TEXT("N03"), TEXT("The victim could not return to its duel site: ") + PC.IntentFeedback);
                }
                PC.Navigation->Navigate(HostSite);
                ++NavigateAttempts;
                NextCommandAt = Now + 1.0;
            }
        }
        return Exists(TEXT("Client1Match1Done.json")) ? EndFirstMatch(Now, PC, *Mode) : true;
    }

    // Both roles: the first match's identities and dirty owner state, kept for the second match and the report.
    void RecordFirstMatch(const ALDPlayerController& PC)
    {
        FirstGeneration = PC.OwnerState.Match;
        // The Host may be between lives here; it keeps the last living body it was bound to.
        if (PC.OwnerState.BodyRef.IsValid()) { FirstBody = PC.OwnerState.BodyRef; }
        FirstSessionId = Opened.View.SessionId;
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        FirstProgression = ProgView ? *ProgView : FLDProgressionView();
        FirstEquipment = EquipView ? *EquipView : FLDEquipmentView();
        FirstMatchRow = MakeShared<FJsonObject>();
        FirstMatchRow->SetNumberField(TEXT("generation"), FirstGeneration);
        FirstMatchRow->SetStringField(TEXT("entity"), LDDemoGrowthScenario::GrowthProbeGuid(PC.OwnerState.ContenderId));
        FirstMatchRow->SetNumberField(TEXT("life"), PC.OwnerState.Life);
        FirstMatchRow->SetNumberField(TEXT("gold"), double(FirstEquipment.Gold));
        FirstMatchRow->SetNumberField(TEXT("account_revision"), double(FirstEquipment.AccountRevision));
        FirstMatchRow->SetArrayField(TEXT("items"), LDDemoGrowthScenario::GrowthProbeItems(FirstEquipment));
        FirstMatchRow->SetNumberField(TEXT("level"), FirstProgression.Level);
        FirstMatchRow->SetNumberField(TEXT("xp"), double(FirstProgression.CumulativeXP));
        FirstMatchRow->SetNumberField(TEXT("pending_choices"), FirstProgression.PendingChoiceCount);
        FirstMatchRow->SetNumberField(TEXT("skills"), FirstProgression.Skills.Num());
        FirstMatchRow->SetNumberField(TEXT("attribute_cards"), FirstProgression.AttributeCards.Num());
        FirstMatchRow->SetNumberField(TEXT("cooldown_debt_ready_at_ms"), double(FirstDebtReadyAtMs));
    }

    // Host: export the first match's ledger, then end the match without a winner through the Host's own session command.
    bool EndFirstMatch(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        Check(Mode.IsGameplayWriteOpen(Mode.GetMatchContext()), TEXT("Ledger"),
            TEXT("The first match stayed open for the whole scenario: no capture result, fault or reset"));
        FString EvidenceText, ExportError;
        const bool bExported = Mode.Economy && Mode.Economy->ExportLedgerEvidence(EvidenceText, ExportError)
            && SaveText(EvidenceText, TEXT("LedgerEvidence.json"));
        Check(bExported, TEXT("Ledger"), TEXT("The authority exports the first match's ledger evidence for the independent checker"),
            ExportError);
        if (!Passed) { return Finish(); }
        RecordFirstMatch(PC);
        FirstLedgerMatch = Mode.GetMatchContext().LedgerMatch;
        PC.EndDemoMatch();
        NextCommandAt = Now + 3.0;
        SetStage(EStage::EndFirst);
        return true;
    }

    // Host: the locked HostEnded result, then the room back to a new Lobby generation through ReturnToLobby.
    bool TickEndFirst(double Now, ALDPlayerController& PC, const ALDGameState& Public)
    {
        const FLDPublicMatchView& View = Public.PublicMatch;
        const FLDResultView& Result = Public.MatchResult;
        const bool bLocked = View.Generation == FirstGeneration && View.Phase == ELDMatchPhase::Results
            && Result.Generation == FirstGeneration && Result.ResultId.IsValid() && View.ResultId == Result.ResultId;
        if (!bLocked)
        {
            if (Now - StageAt > LDDemoGrowthScenario::GrowthProbeTransitionSeconds)
            {
                return Fail(TEXT("Iso"), TEXT("The Host's EndMatch did not lock a result: ") + PC.IntentFeedback);
            }
            if (View.Phase != ELDMatchPhase::Results && Now >= NextCommandAt)
            {
                PC.EndDemoMatch();
                NextCommandAt = Now + 3.0;
            }
            return true;
        }
        const FLDResultRow* Buyer = Result.Rows.FindByPredicate([this](const FLDResultRow& Row) { return Row.ContenderId == ClientId; });
        Check(Result.Reason == ELDMatchEndReason::HostEnded && !Result.Winner.IsValid() && Buyer && Buyer->FinalLevel > 1, TEXT("Iso"),
            TEXT("The Host ends the first match without a winner; the frozen result keeps the buyer's grown final level"),
            FString::Printf(TEXT("reason=%u winner=%s buyer_level=%d"), uint32(Result.Reason),
                *LDDemoGrowthScenario::GrowthProbeGuid(Result.Winner), Buyer ? int32(Buyer->FinalLevel) : -1));
        if (!Passed) { return Finish(); }
        PC.ReturnDemoToLobby();
        NextCommandAt = Now + 3.0;
        SetStage(EStage::AwaitLobby);
        return true;
    }

    // Both roles: the same connections in a new, unready Lobby generation whose owner publication carries no growth.
    bool TickAwaitLobby(double Now, ALDPlayerController& PC, const ALDGameState& Public)
    {
        const FLDLobbyView& Lobby = Public.DemoLobby;
        const bool bNewLobby = FirstGeneration != 0 && Lobby.Generation != FirstGeneration && Lobby.Phase == ELDMatchPhase::Lobby
            && PC.OwnerState.Match == Lobby.Generation && !PC.GetPawn();
        if (!bNewLobby)
        {
            if (IsHost() && Public.PublicMatch.Generation == FirstGeneration && Public.PublicMatch.Phase == ELDMatchPhase::Results
                && Now >= NextCommandAt)
            {
                PC.ReturnDemoToLobby();
                NextCommandAt = Now + 3.0;
            }
            if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeTransitionSeconds) { return true; }
            return Fail(TEXT("Iso"), FString::Printf(TEXT("The room did not return to a new Lobby generation (generation %u, phase %s, owner match %u)"),
                Lobby.Generation, *StaticEnum<ELDMatchPhase>()->GetNameStringByValue(int64(Lobby.Phase)), PC.OwnerState.Match));
        }
        Check(Lobby.Generation > FirstGeneration && Lobby.Members.Num() == 2
            && !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& LobbyMember) { return LobbyMember.Ready; }), TEXT("Iso"),
            TEXT("The same two connections return to a new, unready Lobby generation"),
            FString::Printf(TEXT("generation %u -> %u, members=%d"), FirstGeneration, Lobby.Generation, Lobby.Members.Num()));
        Check(PC.ReadOwnProgression() == nullptr && PC.ReadOwnEquipment() == nullptr && PC.ReadOwnShop() == nullptr, TEXT("Iso"),
            TEXT("Between matches the owner publication carries no growth, equipment or shop view"));
        if (!Passed) { return Finish(); }
        SecondGeneration = Lobby.Generation;
        bSecondMatch = true;
        bLobbyConfigured = false;
        bStartIssued = false;
        NextCommandAt = 0;
        NextReadyAt = 0;
        SetStage(EStage::SecondLobby);
        return true;
    }

    // Both roles, the first frame on which every own view describes the second match's living first body.
    bool TickSecondSpawned(double Now, ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        const FLDCombatOwnerView& Own = PC.CombatView;
        if (!Cast<ALDCharacter>(PC.GetPawn()) || PC.OwnerState.Match != SecondGeneration || PC.OwnerState.LifePhase != ELDLifePhase::Alive
            || !PC.OwnerState.BodyRef.IsValid() || !PC.CanWorldCommand() || !ProgView || !EquipView
            || Own.Generation != SecondGeneration || Own.Life != PC.OwnerState.Life || !Own.bHasBody)
        {
            if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeTransitionSeconds) { return true; }
            return Fail(TEXT("Iso"), TEXT("The second match never published a living first body with every own view"));
        }
        if (!ResolveRoster(PC, Lobby)) { return Finish(); }
        CheckSecondMatch(PC, *ProgView, *EquipView);
        if (!Passed) { return Finish(); }
        if (IsHost())
        {
            SetStage(EStage::SecondAudit);
            return true;
        }
        bReplaySent = false;
        SetStage(EStage::SecondReplay);
        return true;
    }

    // Each role reads only its own projection: every value the first match dirtied is back at its opening value.
    void CheckSecondMatch(const ALDPlayerController& PC, const FLDProgressionView& Progress, const FLDEquipmentView& Equipment)
    {
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        const FLDOwnerState& Self = PC.OwnerState;
        const FLDCombatOwnerView& Own = PC.CombatView;
        Check(Self.Match == SecondGeneration && Self.Life == 1 && Progress.Life == Self.Life && FirstBody.IsValid()
            && Self.BodyRef != FirstBody && Self.ProtectedUntilMs == 0, TEXT("Iso"),
            TEXT("The second match starts the first life of a fresh, unprotected body"),
            FString::Printf(TEXT("generation %u life %u (first match ended on life %u)"), Self.Match, Self.Life,
                FirstMatchRow.IsValid() ? uint32(FirstMatchRow->GetNumberField(TEXT("life"))) : 0u));
        uint8 StartLevel = 0;
        const bool bStartLevel = LDProgression::LevelForXP(Defs.Progression, Defs.Progression.XPStart, StartLevel) == ELDGrowthReason::None;
        Check(bStartLevel && Progress.Generation == SecondGeneration && Progress.Level == StartLevel
            && Progress.CumulativeXP == Defs.Progression.XPStart && Progress.EarnedThrough == 0 && Progress.ConsumedThrough == 0
            && Progress.PendingChoiceCount == 0 && !Progress.bHasOffer && Progress.CatalogDigest == Defs.Identity.Root, TEXT("Iso"),
            TEXT("Level, XP and opportunities restart at the content's opening values with no offer"),
            FString::Printf(TEXT("now %s; first match ended at %s"), *LDDemoGrowthScenario::GrowthProbeProgressionText(Progress),
                *LDDemoGrowthScenario::GrowthProbeProgressionText(FirstProgression)));
        Check(Progress.Skills.IsEmpty() && Progress.AttributeCards.IsEmpty(), TEXT("Iso"),
            TEXT("No learned active or passive skill and no attribute card is carried into the second match"),
            FString::Printf(TEXT("skills %d->%d cards %d->%d"), FirstProgression.Skills.Num(), Progress.Skills.Num(),
                FirstProgression.AttributeCards.Num(), Progress.AttributeCards.Num()));
        bool bSlotsEmpty = Equipment.Slots.Num() == Defs.Progression.EquipmentSlots;
        for (const FLDEquipmentSlotView& SlotRow : Equipment.Slots)
        {
            bSlotsEmpty &= !SlotRow.bOccupied;
        }
        Check(Equipment.Generation == SecondGeneration && Equipment.Gold == Defs.Parameters.StartingGold && Equipment.AccountRevision == 1
            && Equipment.InventoryRevision == 1 && bSlotsEmpty && PC.ReadOwnShop() == nullptr, TEXT("Iso"),
            TEXT("Gold restarts at the approved starting gold in a fresh account, both slots are empty and no shop session exists"),
            FString::Printf(TEXT("gold %lld->%lld account rev %llu inventory rev %llu, first match held %d items"), FirstEquipment.Gold,
                Equipment.Gold, Equipment.AccountRevision, Equipment.InventoryRevision,
                FirstEquipment.Slots.FilterByPredicate([](const FLDEquipmentSlotView& SlotRow) { return SlotRow.bOccupied; }).Num()));
        const bool bNoSource = !Own.Entries.ContainsByPredicate([&Own](const FLDCombatEntryView& EntryRow)
        {
            return !EntryRow.DefinitionId.IsNone() || EntryRow.ItemInstance.IsValid() || EntryRow.IssuedCooldownId.IsValid()
                || EntryRow.ReadyAtMs > Own.AsOfMs;
        });
        const bool bNoContribution = !Own.Attributes.ContainsByPredicate([](const FLDCombatAttributeRowView& Row)
        {
            return Row.Choices.Numerator != 0 || Row.Passives.Numerator != 0 || Row.Equipment.Numerator != 0;
        });
        Check(bNoSource && bNoContribution && Own.EquipmentPassives.IsEmpty() && Own.NextAttackAtMs <= Own.AsOfMs, TEXT("Iso"),
            TEXT("Combat carries no learned or equipped source, no issued cooldown track or debt and no build contribution"),
            FString::Printf(TEXT("entries=%d attributes=%d as_of=%lld first-match debt until %lld"), Own.Entries.Num(),
                Own.Attributes.Num(), Own.AsOfMs, FirstDebtReadyAtMs));
        if (!IsHost()) { return; }
        // Host only: the authority ledger of the second match.
        const ALDGameMode* Mode = PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
        const LDEconomy::FLedger* Book = Mode && Mode->Economy ? &Mode->Economy->GetLedger() : nullptr;
        bool bFresh = Book && Book->IsInitialized() && Mode->Economy->GetMatch() == Mode->GetMatchContext()
            && Mode->GetMatchContext().Generation == SecondGeneration && Mode->GetMatchContext().LedgerMatch != FirstLedgerMatch
            && Book->GetMatch() == Mode->GetMatchContext().LedgerMatch && Book->CommittedSerial() == 0 && Book->IssuedThrough() == 0
            && Book->State().LiveItems.IsEmpty() && Book->State().Accounts.Num() == 2;
        if (Book)
        {
            for (const TPair<FGuid, LDEconomy::FAccount>& Pair : Book->State().Accounts)
            {
                bFresh &= Pair.Value.Gold == Defs.Parameters.StartingGold && Pair.Value.Revision == 1 && !Pair.Value.bRetired;
            }
        }
        Check(bFresh, TEXT("Iso"),
            TEXT("The second match opens a fresh ledger under a new LedgerMatch: starting gold, no items and no commit"),
            FString::Printf(TEXT("ledger match %s -> %s"), *LDDemoGrowthScenario::GrowthProbeGuid(FirstLedgerMatch),
                Mode ? *LDDemoGrowthScenario::GrowthProbeGuid(Mode->GetMatchContext().LedgerMatch) : TEXT("none")));
    }

    // Host: once the buyer finished its stale replays and the second shop visit, export the second match's ledger.
    bool TickSecondAudit(ALDPlayerController& PC)
    {
        if (!Exists(TEXT("Client1Match2Done.json"))) { return true; }
        ALDGameMode* Mode = PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
        FString EvidenceText, ExportError;
        const bool bExported = Mode && Mode->Economy && Mode->Economy->ExportLedgerEvidence(EvidenceText, ExportError)
            && SaveText(EvidenceText, TEXT("LedgerEvidence2.json"));
        Check(bExported && Mode->Economy->GetLedger().CommittedSerial() == 0, TEXT("Iso"),
            TEXT("The stale first-match requests committed nothing: the second match's ledger is exported with no commit"),
            ExportError);
        Check(Mode && Mode->IsGameplayWriteOpen(Mode->GetMatchContext()), TEXT("Iso"), TEXT("The second match stayed open for its checks"));
        return Finish();
    }

    // After the reports each process exits on the runner's signal: the client on FinishClients, the Host on Finish.
    bool TickAfterReport() const
    {
        if (!Exists(IsHost() ? TEXT("Finish") : TEXT("FinishClients"))) { return true; }
        FPlatformMisc::RequestExitWithStatus(false, 0);
        return false;
    }

    // ---- Client1: kills, the choice and the shop, each only through this owning connection. ----

    bool TickClient(double Now, ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        if (!Body || PC.OwnerState.LifePhase != ELDLifePhase::Alive || PC.OwnerState.Life != 1)
        {
            return Fail(TEXT("N03"), TEXT("The buyer lost its first life although the victim never attacks"));
        }
        switch (Stage)
        {
        case EStage::ToSite: return TickToSite(Now, PC, *Body, KillSite, EStage::KillSeek, TEXT("N03"));
        case EStage::KillSeek: return TickKillSeek(Now, PC);
        case EStage::KillWait: return TickKillWait(Now, PC);
        case EStage::Choice: return TickChoice(Now, PC);
        case EStage::ChoiceQuery: return TickChoiceQuery(Now, PC);
        case EStage::ShopApproach: return TickShopApproach(Now, PC);
        case EStage::ShopOpen: return TickShopOpen(Now, PC);
        case EStage::Buy1: return TickBuy(Now, PC, true);
        case EStage::Buy2: return TickBuy(Now, PC, false);
        case EStage::BuyFull: return TickBuyFull(Now, PC);
        case EStage::Replay: return TickReplay(Now, PC);
        case EStage::Activate: return TickActivate(Now, PC);
        case EStage::Sell: return TickSell(Now, PC);
        case EStage::WalkOut: return TickToSite(Now, PC, *Body, KillSite, EStage::Outside, TEXT("A17"));
        case EStage::Outside: return TickOutside(Now, PC, *Body, Lobby);
        case EStage::SecondReplay: return TickSecondReplay(Now, PC);
        case EStage::SecondQuote: return TickSecondQuote(PC);
        default: return Fail(TEXT("Net"), TEXT("The buyer reached a stage of another role"));
        }
    }

    bool TickToSite(double Now, ALDPlayerController& PC, const ALDCharacter& Body, const FVector& Site, EStage Next, const TCHAR* Id)
    {
        if (PC.Navigation->HasMoveOrder()) { return true; }
        if (FVector::Dist2D(Body.GetNavAgentLocation(), Site) <= LDDemoGrowthScenario::GrowthProbeArrivalCm)
        {
            SetStage(Next);
            return true;
        }
        if (Now - StageAt < 1.0) { return true; }
        if (NavigateAttempts >= LDDemoGrowthScenario::GrowthProbeNavigateAttempts)
        {
            return Fail(Id, TEXT("The owned body could not reach its public duel site: ") + PC.IntentFeedback);
        }
        PC.Navigation->Navigate(Site);
        ++NavigateAttempts;
        StageAt = Now;
        return true;
    }

    // The victim must be visible in this owner's own frame and no longer protected; the attack is the ordinary
    // AttackUnit intent on its observer handle (what a right click on the unit sends).
    bool TickKillSeek(double Now, ALDPlayerController& PC)
    {
        const FLDWorldView* Frame = LDDemoGrowthScenario::GrowthProbeFrame(PC);
        const FLDObservedUnitView* Victim = Frame ? LDDemoGrowthScenario::GrowthProbeContender(*Frame, HostId) : nullptr;
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        if (!Victim || LDDemoGrowthScenario::GrowthProbeProtected(*Victim) || !ProgView || !EquipView)
        {
            if (Now - StageAt <= RespawnSeconds + LDDemoGrowthScenario::GrowthProbeReturnSeconds) { return true; }
            return Fail(TEXT("N03"), TEXT("The victim never became visible and unprotected at its duel site"));
        }
        if (AttackOrders == 0)
        {
            const FLDDemoContentDefinitions& Defs = Catalog->Get();
            LDProgression::FRewardAmounts Bounty;
            const ELDGrowthReason BountyReason = LDProgression::ContenderKillReward(Defs.Parameters,
                uint8(Defs.Progression.MaxLevel), Victim->Level, Bounty);
            if (!Check(BountyReason == ELDGrowthReason::None, TEXT("N03"), TEXT("The observed victim level has an approved bounty"),
                LDGrowthWire::ReasonName(BountyReason)))
            {
                return Finish();
            }
            VictimLevel = Victim->Level;
            KillXP = Bounty.XP;
            KillGold = Bounty.Gold;
            KillBefore = *ProgView;
            GoldBeforeKill = EquipView->Gold;
            KillStartedAt = Now;
        }
        if (AttackOrders >= LDDemoGrowthScenario::GrowthProbeAttackOrdersPerKill)
        {
            return Fail(TEXT("N03"), TEXT("The attack lock kept ending without a death: ") + PC.Navigation->Status);
        }
        PC.Navigation->AttackUnit(Victim->Handle, Victim->ObservationEpoch);
        AttackOrder = PC.Navigation->LocalIdentity();
        ++AttackOrders;
        SetStage(EStage::KillWait);
        return true;
    }

    bool TickKillWait(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        const FLDOrderReceipt& Latest = PC.Navigation->LatestReceipt();
        const bool bOrderEnded = Latest.Identity == AttackOrder && (Latest.Status == ELDOrderStatus::Rejected
            || Latest.Status == ELDOrderStatus::Cancelled || Latest.Status == ELDOrderStatus::Completed);
        const bool bRewarded = ProgView && EquipView && ProgView->ProgressionRevision != KillBefore.ProgressionRevision;
        if (bRewarded && bOrderEnded) { return VerifyKill(Now, *ProgView, *EquipView, Latest); }
        if (!bRewarded && bOrderEnded && Latest.Failure != ELDOrderFailure::TargetDied)
        {
            // The server ended the lock without a death (lost sight, blocked path): seek the same victim again.
            SetStage(EStage::KillSeek);
            return true;
        }
        if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeKillSeconds) { return true; }
        return Fail(TEXT("N03"), bOrderEnded ? TEXT("The victim died but no reward reached the owner's growth view")
            : TEXT("The attack order did not end in the victim's death within its budget"));
    }

    bool VerifyKill(double Now, const FLDProgressionView& ProgView, const FLDEquipmentView& EquipView, const FLDOrderReceipt& Ended)
    {
        const FLDDemoProgressionDefinition& Table = Catalog->Get().Progression;
        uint8 ExpectedLevel = 0;
        const ELDGrowthReason LevelReason = LDProgression::LevelForXP(Table, KillBefore.CumulativeXP + KillXP, ExpectedLevel);
        const FString Detail = FString::Printf(TEXT("kill %d of %d, victim L%u: xp %lld->%lld (+%lld) gold %lld->%lld (+%lld) %s"),
            Kills + 1, KillsPlanned, uint32(VictimLevel), KillBefore.CumulativeXP, ProgView.CumulativeXP, KillXP, GoldBeforeKill,
            EquipView.Gold, KillGold, *LDDemoGrowthScenario::GrowthProbeProgressionText(ProgView));
        Check(Ended.Status == ELDOrderStatus::Completed && Ended.Failure == ELDOrderFailure::TargetDied, TEXT("N03"),
            TEXT("The owning client's own attack order ended in the victim's death"), Detail);
        Check(ProgView.CumulativeXP == KillBefore.CumulativeXP + KillXP && EquipView.Gold == GoldBeforeKill + KillGold, TEXT("N03"),
            TEXT("One kill pays exactly the approved bounty of the frozen victim level, gold and XP in one owner publication"), Detail);
        Check(LevelReason == ELDGrowthReason::None && ProgView.Level == ExpectedLevel
            && ProgView.PendingChoiceCount == ProgView.Level - 1 - ProgView.ConsumedThrough, TEXT("A06"),
            TEXT("Level and pending opportunities follow the approved XP thresholds: one opportunity per level crossed"), Detail);
        const TSharedRef<FJsonObject> KillRow = MakeShared<FJsonObject>();
        KillRow->SetNumberField(TEXT("index"), Kills + 1);
        KillRow->SetNumberField(TEXT("victim_level"), VictimLevel);
        KillRow->SetNumberField(TEXT("xp_before"), double(KillBefore.CumulativeXP));
        KillRow->SetNumberField(TEXT("xp_after"), double(ProgView.CumulativeXP));
        KillRow->SetNumberField(TEXT("gold_before"), double(GoldBeforeKill));
        KillRow->SetNumberField(TEXT("gold_after"), double(EquipView.Gold));
        KillRow->SetNumberField(TEXT("level_after"), ProgView.Level);
        KillRow->SetNumberField(TEXT("pending_after"), ProgView.PendingChoiceCount);
        KillRow->SetNumberField(TEXT("attack_orders"), AttackOrders);
        KillRow->SetNumberField(TEXT("seconds"), Now - KillStartedAt);
        KillRows.Add(MakeShared<FJsonValueObject>(KillRow));
        ++Kills;
        AttackOrders = 0;
        if (!Passed) { return Finish(); }
        // The victim stays dead for the approved respawn delay, so the choice cannot race a reward.
        if (!bChoiceDone && (ProgView.PendingChoiceCount >= 2 || EquipView.Gold >= GoldGoal))
        {
            SetStage(EStage::Choice);
            return true;
        }
        return ContinueKills(EquipView.Gold);
    }

    bool ContinueKills(int64 CurrentGold)
    {
        if (CurrentGold >= GoldGoal)
        {
            if (!Check(Kills == KillsPlanned && bChoiceDone, TEXT("N03"),
                TEXT("The gold goal is reached after exactly the planned kills, with the choice made between them"),
                FString::Printf(TEXT("kills=%d planned=%d gold=%lld goal=%lld"), Kills, KillsPlanned, CurrentGold, GoldGoal)))
            {
                return Finish();
            }
            bApproachSent = false;
            ApproachAttempts = 0;
            SetStage(EStage::ShopApproach);
            return true;
        }
        if (Kills >= KillsPlanned)
        {
            return Fail(TEXT("N03"), FString::Printf(TEXT("The planned %d kills did not reach the %lld gold goal"), KillsPlanned, GoldGoal));
        }
        SetStage(EStage::KillSeek);
        return true;
    }

    // First card of the head offer that needs no Q/W replacement.
    const FLDChoiceOption* PickOption(const FLDProgressionView& View) const
    {
        int32 Actives = 0;
        for (const FLDLearnedSkillView& Learned : View.Skills)
        {
            Actives += Learned.bActive ? 1 : 0;
        }
        const bool bActiveSlotFree = Actives < Catalog->Get().Progression.ActiveSlots;
        return View.Offer.Options.FindByPredicate([bActiveSlotFree](const FLDChoiceOption& Card)
        {
            return Card.Kind != ELDChoiceOptionKind::LearnActive || bActiveSlotFree;
        });
    }

    bool BuildShowsChoice(const FLDProgressionView& View) const
    {
        if (ChoicePicked.Kind == ELDChoiceOptionKind::Attribute)
        {
            const FLDAttributeCardView* Before = ChoiceBefore.AttributeCards.FindByPredicate([this](const FLDAttributeCardView& CardRow)
            {
                return CardRow.DefinitionId == ChoicePicked.DefinitionId;
            });
            const FLDAttributeCardView* After = View.AttributeCards.FindByPredicate([this](const FLDAttributeCardView& CardRow)
            {
                return CardRow.DefinitionId == ChoicePicked.DefinitionId;
            });
            return After && After->Count == (Before ? Before->Count : 0) + 1;
        }
        const bool bActiveKind = ChoicePicked.Kind == ELDChoiceOptionKind::LearnActive
            || ChoicePicked.Kind == ELDChoiceOptionKind::UpgradeActive;
        const FLDLearnedSkillView* Learned = View.Skills.FindByPredicate([this](const FLDLearnedSkillView& SkillRow)
        {
            return SkillRow.DefinitionId == ChoicePicked.DefinitionId;
        });
        return Learned && Learned->Rank == ChoicePicked.ToRank && Learned->bActive == bActiveKind;
    }

    bool TickChoice(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        if (!bChoiceSent)
        {
            if (!ProgView || !ProgView->bHasOffer || !ProgView->bCanChoose)
            {
                if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(TEXT("A10"), TEXT("The earned opportunity never offered a choosable head offer"));
            }
            const FLDChoiceOption* Chosen = PickOption(*ProgView);
            if (!Chosen) { return Fail(TEXT("A10"), TEXT("The head offer has no card that is legal without a replacement")); }
            FLDChoiceCommand Command;
            Command.OfferId = ProgView->Offer.OfferId;
            Command.OfferRevision = ProgView->Offer.OfferRevision;
            Command.OptionId = Chosen->OptionId;
            Command.ExpectedBuildRevision = ProgView->BuildRevision;
            Command.CatalogDigest = ProgView->CatalogDigest;
            ChoiceBefore = *ProgView;
            ChoicePicked = *Chosen;
            // The exact terms SubmitChoice sends: it fills Generation, Life and the life phase from OwnerState.
            ChoiceSent = Command;
            ChoiceSent.Generation = PC.OwnerState.Match;
            ChoiceSent.Life = PC.OwnerState.Life;
            ChoiceSent.ExpectedLifePhase = PC.OwnerState.LifePhase;
            ChoiceSubmitMark = Submits.Num();
            ChoiceResultMark = ChoiceResults.Num();
            ChoicePublishMark = PC.OwnerState.Growth.PublishRevision;
            const uint64 Peeked = PC.PeekGrowthRequestId(ELDGrowthFamily::Choice);
            ELDGrowthReason LocalReason = ELDGrowthReason::None;
            ChoiceSent.RequestId = PC.SubmitChoice(Command, LocalReason);
            bChoiceSent = true;
            if (!Check(ChoiceSent.RequestId != 0 && ChoiceSent.RequestId == Peeked && LocalReason == ELDGrowthReason::None,
                TEXT("A10"), TEXT("The owning client submits the head offer with the next choice request id"),
                FString::Printf(TEXT("request=%llu peek=%llu local=%s"), ChoiceSent.RequestId, Peeked, LDGrowthWire::ReasonName(LocalReason))))
            {
                return Finish();
            }
            return true;
        }
        const FLDSubmitResult* Admission = FindSubmit(ELDGrowthFamily::Choice, ChoiceSent.RequestId, ChoiceSubmitMark);
        const FLDChoiceReceiptQueryResult* Answer = FindChoiceAnswer(ChoiceSent.RequestId, ChoiceResultMark);
        if (Admission && Admission->Status == ELDAdmissionStatus::AdmissionRejected)
        {
            return Fail(TEXT("A10"), TEXT("The choice was refused at admission: ") + LDDemoGrowthScenario::GrowthProbeReason(Admission->Reason));
        }
        if (Answer && (!Answer->bHasReceipt || Answer->Receipt.Header.Outcome != ELDReceiptOutcome::Applied))
        {
            return Fail(TEXT("A10"), TEXT("The choice did not apply: ") + LDDemoGrowthScenario::GrowthProbeReason(Answer->Receipt.Header.Reason));
        }
        const bool bPublished = Admission && Answer && ProgView && PC.OwnerState.Growth.PublishRevision > ChoicePublishMark
            && ProgView->ProgressionRevision == Answer->Receipt.Header.Revisions.ProgressionRevision;
        if (!bPublished)
        {
            if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("A10"), TEXT("The choice did not receive its admission, final receipt and owner publication in time"));
        }
        ChoiceFinal = Answer->Receipt;
        const FString Detail = LDDemoGrowthScenario::GrowthProbeProgressionText(*ProgView)
            + FString::Printf(TEXT(" card=%s rank=%u"), *ChoicePicked.DefinitionId.ToString(), uint32(ChoicePicked.ToRank));
        Check(Admission->Status == ELDAdmissionStatus::AcceptedPending && Answer->Lookup == ELDReceiptLookup::Final
            && ChoiceFinal.OptionId == ChoicePicked.OptionId && !ChoiceFinal.bReplaced, TEXT("A10"),
            TEXT("The choice is accepted pending, then finally applies exactly the chosen card without a replacement"), Detail);
        Check(BuildShowsChoice(*ProgView) && ProgView->PendingChoiceCount + 1 == ChoiceBefore.PendingChoiceCount, TEXT("A10"),
            TEXT("The owner build shows the learned card and exactly one consumed opportunity"), Detail);
        Check(ChoiceFinal.bHasNextOffer
            ? ProgView->bHasOffer && ProgView->Offer.OfferId == ChoiceFinal.NextOfferId && ChoiceFinal.NextOfferId != ChoiceBefore.Offer.OfferId
            : !ProgView->bHasOffer && ProgView->PendingChoiceCount == 0, TEXT("A10"),
            TEXT("Only the applied choice draws the next head offer, and it differs from the consumed one"), Detail);
        if (!Passed) { return Finish(); }
        bQuerySent = false;
        SetStage(EStage::ChoiceQuery);
        return true;
    }

    bool TickChoiceQuery(double Now, ALDPlayerController& PC)
    {
        if (!bQuerySent)
        {
            FLDReceiptQuery Query;
            Query.Generation = ChoiceSent.Generation;
            Query.Family = ELDGrowthFamily::Choice;
            Query.RequestId = ChoiceSent.RequestId;
            ChoiceResultMark = ChoiceResults.Num();
            PC.QueryGrowthReceipt(Query);
            bQuerySent = true;
            return true;
        }
        const FLDChoiceReceiptQueryResult* Answer = FindChoiceAnswer(ChoiceSent.RequestId, ChoiceResultMark);
        if (!Answer)
        {
            if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("A24"), TEXT("The receipt query was not answered"));
        }
        if (!Check(Answer->Lookup == ELDReceiptLookup::Final && Answer->bHasReceipt && !Answer->bHasPending
            && Answer->Query.Generation == ChoiceSent.Generation && Answer->Query.Family == ELDGrowthFamily::Choice
            && LDDemoGrowthScenario::GrowthProbeSameChoice(Answer->Receipt, ChoiceFinal), TEXT("A24"),
            TEXT("A query by Generation, Family and RequestId returns the same immutable final receipt")))
        {
            return Finish();
        }
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        if (!EquipView) { return Fail(TEXT("A24"), TEXT("The owner equipment view became unavailable after the choice")); }
        ChoiceRow = MakeShared<FJsonObject>();
        ChoiceRow->SetNumberField(TEXT("request_id"), double(ChoiceSent.RequestId));
        ChoiceRow->SetStringField(TEXT("option_id"), LDDemoGrowthScenario::GrowthProbeGuid(ChoiceFinal.OptionId));
        ChoiceRow->SetStringField(TEXT("definition"), ChoiceFinal.DefinitionId.ToString());
        ChoiceRow->SetNumberField(TEXT("new_rank"), ChoiceFinal.NewRank);
        ChoiceRow->SetNumberField(TEXT("pending_before"), ChoiceBefore.PendingChoiceCount);
        ChoiceRow->SetBoolField(TEXT("next_offer"), ChoiceFinal.bHasNextOffer);
        bChoiceDone = true;
        return ContinueKills(EquipView->Gold);
    }

    // ---- Client1: the shop. Every Expected* field comes from the owner's current Shop/Equipment/Progression views. ----

    bool BeginCall(const ALDPlayerController& PC, const FString& StepName)
    {
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        Call = FShopCall();
        Call.Step = StepName;
        Call.bSent = true;
        if (!EquipView || !ProgView)
        {
            return Check(false, TEXT("A16"), StepName + TEXT(" starts from the owner's current growth views"));
        }
        Call.GoldBefore = EquipView->Gold;
        Call.AccountBefore = EquipView->AccountRevision;
        Call.InventoryBefore = EquipView->InventoryRevision;
        Call.BuildBefore = ProgView->BuildRevision;
        Call.SlotsBefore = EquipView->Slots;
        Call.SubmitMark = Submits.Num();
        Call.ResultMark = ShopResults.Num();
        Call.PublishMark = PC.OwnerState.Growth.PublishRevision;
        Call.PeekBefore = PC.PeekGrowthRequestId(ELDGrowthFamily::Shop);
        Call.SentAt = FPlatformTime::Seconds();
        return true;
    }

    bool SendShop(ALDPlayerController& PC, const FString& StepName, const FLDShopCommand& Command)
    {
        if (!BeginCall(PC, StepName)) { return false; }
        // The exact terms SubmitShopCommand sends: it fills Generation, Life and the RequestId.
        Call.Sent = Command;
        Call.Sent.Generation = PC.OwnerState.Match;
        Call.Sent.Life = PC.OwnerState.Life;
        ELDGrowthReason LocalReason = ELDGrowthReason::None;
        Call.Sent.RequestId = PC.SubmitShopCommand(Command, LocalReason);
        return Check(Call.Sent.RequestId != 0 && Call.Sent.RequestId == Call.PeekBefore && LocalReason == ELDGrowthReason::None,
            TEXT("A24"), StepName + TEXT(" goes through SubmitShopCommand with the next shop request id"),
            FString::Printf(TEXT("request=%llu peek=%llu local=%s"), Call.Sent.RequestId, Call.PeekBefore, LDGrowthWire::ReasonName(LocalReason)));
    }

    bool ShopTerms(const ALDPlayerController& PC, ELDShopCommandKind Kind, FLDShopCommand& OutCommand) const
    {
        OutCommand = FLDShopCommand();
        const FLDShopView* ShopView = PC.ReadOwnShop();
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        if (!ShopView || !EquipView || !ProgView || !FLDDefinitionWire::Encode(ShopView->WorldMapId, OutCommand.WorldMapId))
        {
            return false;
        }
        OutCommand.Kind = Kind;
        OutCommand.SessionId = ShopView->SessionId;
        OutCommand.QuoteRevision = ShopView->QuoteRevision;
        OutCommand.CatalogDigest = ShopView->CatalogDigest;
        OutCommand.WorldLayoutRevision = ShopView->WorldLayoutRevision;
        OutCommand.WorldGeometryRevision = ShopView->WorldGeometryRevision;
        OutCommand.ExpectedAccountRevision = EquipView->AccountRevision;
        OutCommand.ExpectedInventoryRevision = EquipView->InventoryRevision;
        OutCommand.ExpectedBuildRevision = ProgView->BuildRevision;
        return true;
    }

    bool BuyTerms(const ALDPlayerController& PC, FLDShopCommand& OutCommand) const
    {
        if (!ShopTerms(PC, ELDShopCommandKind::Buy, OutCommand)) { return false; }
        const FLDShopItemView* Offered = PC.ReadOwnShop()->Items.FindByPredicate([this](const FLDShopItemView& ItemRow)
        {
            return ItemRow.DefinitionId == ItemId;
        });
        if (!Offered || Offered->BuyPrice <= 0 || !FLDDefinitionWire::Encode(Offered->DefinitionId, OutCommand.DefinitionId))
        {
            return false;
        }
        OutCommand.BuyPrice = Offered->BuyPrice;
        return true;
    }

    const FLDShopSellView* SellQuote(const ALDPlayerController& PC, const FGuid& SoldInstance) const
    {
        const FLDShopView* ShopView = PC.ReadOwnShop();
        return ShopView ? ShopView->Sellable.FindByPredicate([&SoldInstance](const FLDShopSellView& SellRow)
        {
            return SellRow.Instance == SoldInstance;
        }) : nullptr;
    }

    // Pure lookup: an admission of the request, its final receipt and the owner publication that follows the receipt.
    LDDemoGrowthScenario::EGrowthProbeWait AwaitShop(double Now, const ALDPlayerController& PC, const FLDShopReceipt*& OutReceipt,
        FString& OutWhy) const
    {
        bool bAdmitted = false;
        for (int32 SubmitIndex = Call.SubmitMark; SubmitIndex < Submits.Num(); ++SubmitIndex)
        {
            const FLDSubmitResult& Admitted = Submits[SubmitIndex];
            if (Admitted.Family != ELDGrowthFamily::Shop || Admitted.RequestId != Call.Sent.RequestId) { continue; }
            if (Admitted.Status == ELDAdmissionStatus::AdmissionRejected)
            {
                OutWhy = TEXT("refused at admission: ") + LDDemoGrowthScenario::GrowthProbeReason(Admitted.Reason);
                return LDDemoGrowthScenario::EGrowthProbeWait::Failed;
            }
            bAdmitted = true;
        }
        OutReceipt = FindShopFinal(Call.Sent.RequestId, Call.ResultMark);
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        const bool bApplied = OutReceipt && OutReceipt->Header.Outcome == ELDReceiptOutcome::Applied;
        const bool bPublished = OutReceipt && EquipView && PC.OwnerState.Growth.PublishRevision > Call.PublishMark
            && (!bApplied || (EquipView->AccountRevision == OutReceipt->Header.Revisions.AccountRevision
                && EquipView->InventoryRevision == OutReceipt->Header.Revisions.InventoryRevision));
        if (bAdmitted && bPublished) { return LDDemoGrowthScenario::EGrowthProbeWait::Ready; }
        if (Now - Call.SentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds)
        {
            return LDDemoGrowthScenario::EGrowthProbeWait::Waiting;
        }
        OutWhy = FString::Printf(TEXT("admitted=%d receipt=%d publish=%llu after %llu"), bAdmitted ? 1 : 0, OutReceipt ? 1 : 0,
            PC.OwnerState.Growth.PublishRevision, Call.PublishMark);
        return LDDemoGrowthScenario::EGrowthProbeWait::Failed;
    }

    // Applied: the receipt and the owner's gold move by exactly the quoted price; the owner view carries its revisions.
    void CheckApplied(const ALDPlayerController& PC, const FLDShopReceipt& Seen, int64 QuotedDelta)
    {
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        const FLDResultRevisions& Revs = Seen.Header.Revisions;
        Check(Seen.Header.Outcome == ELDReceiptOutcome::Applied && Seen.ActualGoldDelta == QuotedDelta && EquipView
            && EquipView->Gold == Call.GoldBefore + QuotedDelta && EquipView->AccountRevision == Revs.AccountRevision
            && EquipView->InventoryRevision == Revs.InventoryRevision, TEXT("A16"),
            Call.Step + TEXT(" applies once: receipt and owner gold move by exactly the quoted price"),
            FString::Printf(TEXT("gold %lld->%lld %s"), Call.GoldBefore, EquipView ? EquipView->Gold : -1,
                *LDDemoGrowthScenario::GrowthProbeShopText(Seen)));
    }

    // Rejected: the wanted reason, no payload, and the owner state is untouched.
    void CheckRejected(const ALDPlayerController& PC, const FLDShopReceipt& Seen, ELDGrowthReason Wanted, const TCHAR* Id, const FString& Name)
    {
        FString Drift;
        const bool bSame = Unchanged(PC, Drift);
        Check(Seen.Header.Outcome == ELDReceiptOutcome::Rejected && LDGrowthWire::Reason(Seen.Header.Reason) == Wanted
            && !Seen.Instance.IsValid() && Seen.ActualGoldDelta == 0 && bSame, Id, Name,
            LDDemoGrowthScenario::GrowthProbeShopText(Seen) + TEXT(" ") + Drift);
    }

    bool Unchanged(const ALDPlayerController& PC, FString& OutDrift) const
    {
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        if (!EquipView || !ProgView)
        {
            OutDrift = TEXT("owner views unavailable");
            return false;
        }
        bool bSame = EquipView->Gold == Call.GoldBefore && EquipView->AccountRevision == Call.AccountBefore
            && EquipView->InventoryRevision == Call.InventoryBefore && ProgView->BuildRevision == Call.BuildBefore
            && EquipView->Slots.Num() == Call.SlotsBefore.Num();
        for (int32 SlotIndex = 0; bSame && SlotIndex < EquipView->Slots.Num(); ++SlotIndex)
        {
            bSame = LDDemoGrowthScenario::GrowthProbeSameSlot(EquipView->Slots[SlotIndex], Call.SlotsBefore[SlotIndex]);
        }
        OutDrift = FString::Printf(TEXT("gold %lld->%lld account %llu->%llu inventory %llu->%llu build %llu->%llu"), Call.GoldBefore,
            EquipView->Gold, Call.AccountBefore, EquipView->AccountRevision, Call.InventoryBefore, EquipView->InventoryRevision,
            Call.BuildBefore, ProgView->BuildRevision);
        return bSame;
    }

    void RecordShop(const ALDPlayerController& PC, const FString& Outcome, const FString& Reason, const FLDShopReceipt* Seen)
    {
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        const TSharedRef<FJsonObject> ShopRow = MakeShared<FJsonObject>();
        ShopRow->SetStringField(TEXT("step"), Call.Step);
        ShopRow->SetNumberField(TEXT("request_id"), double(Call.Sent.RequestId));
        ShopRow->SetStringField(TEXT("outcome"), Outcome);
        ShopRow->SetStringField(TEXT("reason"), Reason);
        ShopRow->SetNumberField(TEXT("gold_before"), double(Call.GoldBefore));
        ShopRow->SetNumberField(TEXT("gold_after"), EquipView ? double(EquipView->Gold) : -1.0);
        if (Seen)
        {
            ShopRow->SetNumberField(TEXT("gold_delta"), double(Seen->ActualGoldDelta));
            ShopRow->SetStringField(TEXT("instance"), LDDemoGrowthScenario::GrowthProbeGuid(Seen->Instance));
            ShopRow->SetNumberField(TEXT("slot"), Seen->SlotIndex);
        }
        ShopRows.Add(MakeShared<FJsonValueObject>(ShopRow));
    }

    // Navigation's arrival of the current ApproachShop binding makes the module's own handler request the session.
    bool TickShopApproach(double Now, ALDPlayerController& PC)
    {
        if (!bApproachSent)
        {
            OpenMark = OpenResults.Num();
            ArrivalMark = Arrivals.Num();
            PC.Navigation->ApproachShop(ShopId);
            ApproachOrder = PC.Navigation->LocalIdentity();
            ++ApproachAttempts;
            bApproachSent = true;
            StageAt = Now;
            FName BoundShop;
            uint32 BoundLife = 0;
            return Check(PC.ReadShopInteraction(BoundShop, BoundLife) && BoundShop == ShopId && BoundLife == PC.OwnerState.Life,
                TEXT("A17"), TEXT("ApproachShop binds the catalog's public shop to the current life"), ShopId.ToString()) ? true : Finish();
        }
        if (Arrivals.Num() > ArrivalMark)
        {
            SetStage(EStage::ShopOpen);
            return true;
        }
        const FLDOrderReceipt& Latest = PC.Navigation->LatestReceipt();
        const bool bEnded = Latest.Identity == ApproachOrder
            && (Latest.Status == ELDOrderStatus::Rejected || Latest.Status == ELDOrderStatus::Cancelled);
        if (!bEnded && Now - StageAt <= 2.0 * LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
        if (ApproachAttempts >= 2)
        {
            return Fail(TEXT("A17"), FString::Printf(TEXT("The shop approach ended without an arrival: %s (latest receipt order=%u status=%u failure=%u)"),
                *PC.Navigation->Status, Latest.Identity.Order, uint32(Latest.Status), uint32(Latest.Failure)));
        }
        bApproachSent = false;
        return true;
    }

    bool TickShopOpen(double Now, ALDPlayerController& PC)
    {
        const FLDShopOpenResult* Terminal = nullptr;
        for (int32 OpenIndex = OpenMark; OpenIndex < OpenResults.Num() && !Terminal; ++OpenIndex)
        {
            const FLDShopOpenResult& Result = OpenResults[OpenIndex];
            if (Result.OpenStatus == ELDShopOpenStatus::Opened || Result.OpenStatus == ELDShopOpenStatus::Rejected)
            {
                Terminal = &Result;
            }
        }
        if (Terminal && Terminal->OpenStatus == ELDShopOpenStatus::Rejected)
        {
            return Fail(TEXT("A17"), TEXT("The shop session was refused: ") + LDDemoGrowthScenario::GrowthProbeReason(Terminal->Reason));
        }
        // The open result is an RPC; the owner shop view arrives later with the replicated OwnerState.
        const FLDShopView* ShopView = PC.ReadOwnShop();
        if (!Terminal || !ShopView || ShopView->SessionId != Terminal->View.SessionId)
        {
            if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("A17"), TEXT("No Opened result and owner shop publication followed the arrival"));
        }
        Opened = *Terminal;
        FName BoundShop;
        uint32 BoundLife = 0;
        Check(Opened.Generation == PC.OwnerState.Match && Opened.Life == PC.OwnerState.Life && Opened.ShopId == ShopId && Opened.bHasView
            && PC.ReadShopInteraction(BoundShop, BoundLife) && BoundShop == ShopId && BoundLife == PC.OwnerState.Life, TEXT("A17"),
            TEXT("The arrival opened this generation's and life's shop session and the interaction binding is current"));
        if (!Passed) { return Finish(); }
        Call = FShopCall();
        SetStage(bSecondMatch ? EStage::SecondQuote : EStage::Buy1);
        return true;
    }

    // Buy 1 and Buy 2 are two explicit purchases of the same definition; each applies exactly once.
    bool TickBuy(double Now, ALDPlayerController& PC, bool bFirst)
    {
        if (!Call.bSent)
        {
            FLDShopCommand Command;
            if (!BuyTerms(PC, Command))
            {
                return Fail(TEXT("A16"), TEXT("The open quote does not offer the planned item for a Buy"));
            }
            if (!SendShop(PC, bFirst ? TEXT("buy-1") : TEXT("buy-2"), Command)) { return Finish(); }
            if (bFirst) { Buy1Sent = Call.Sent; }
            return true;
        }
        const FLDShopReceipt* Seen = nullptr;
        FString Why;
        const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitShop(Now, PC, Seen, Why);
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Failed)
        {
            return Fail(TEXT("A16"), Call.Step + TEXT(" did not complete: ") + Why);
        }
        CheckApplied(PC, *Seen, -Call.Sent.BuyPrice);
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        const FLDEquipmentSlotView* Filled = EquipView ? LDDemoGrowthScenario::GrowthProbeSlot(EquipView->Slots, Seen->SlotIndex) : nullptr;
        const FLDEquipmentSlotView* WasEmpty = LDDemoGrowthScenario::GrowthProbeSlot(Call.SlotsBefore, Seen->SlotIndex);
        Check(Filled && Filled->bOccupied && Filled->Instance == Seen->Instance && Filled->DefinitionId == ItemId && WasEmpty
            && !WasEmpty->bOccupied && (bFirst || (Seen->Instance != Buy1Receipt.Instance && Seen->SlotIndex != Buy1Receipt.SlotIndex)),
            TEXT("A15"), bFirst ? TEXT("The first purchase creates one real instance in an empty slot")
                : TEXT("A second purchase of the same definition holds a second, distinct instance in the other slot"),
            LDDemoGrowthScenario::GrowthProbeShopText(*Seen));
        if (bFirst)
        {
            Buy1Receipt = *Seen;
        }
        else
        {
            Buy2Receipt = *Seen;
        }
        RecordShop(PC, TEXT("Applied"), LDDemoGrowthScenario::GrowthProbeReason(Seen->Header.Reason), Seen);
        if (!Passed) { return Finish(); }
        Call = FShopCall();
        SetStage(bFirst ? EStage::Buy2 : EStage::BuyFull);
        return true;
    }

    bool TickBuyFull(double Now, ALDPlayerController& PC)
    {
        if (!Call.bSent)
        {
            const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
            FLDShopCommand Command;
            if (!EquipView || !BuyTerms(PC, Command))
            {
                return Fail(TEXT("A15"), TEXT("A third Buy could not be formed from the open quote"));
            }
            // Capacity is refused before the balance: with both slots held the third copy is EquipmentFull either way.
            const bool bFull = !EquipView->Slots.ContainsByPredicate([](const FLDEquipmentSlotView& SlotRow) { return !SlotRow.bOccupied; });
            if (!Check(bFull, TEXT("A15"), TEXT("Both slots are held before the third Buy"),
                FString::Printf(TEXT("gold=%lld price=%lld"), EquipView->Gold, Command.BuyPrice)))
            {
                return Finish();
            }
            return SendShop(PC, TEXT("buy-3-full"), Command) ? true : Finish();
        }
        const FLDShopReceipt* Seen = nullptr;
        FString Why;
        const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitShop(Now, PC, Seen, Why);
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Failed)
        {
            return Fail(TEXT("A15"), Call.Step + TEXT(" did not complete: ") + Why);
        }
        CheckRejected(PC, *Seen, ELDGrowthReason::EquipmentFull, TEXT("A15"),
            TEXT("A third copy with both slots held is refused EquipmentFull; nothing is sold or replaced"));
        RecordShop(PC, TEXT("Rejected"), LDDemoGrowthScenario::GrowthProbeReason(Seen->Header.Reason), Seen);
        if (!Passed) { return Finish(); }
        Call = FShopCall();
        SetStage(EStage::Replay);
        return true;
    }

    // Buy 1's exact command again through the owning RPC after it is final: the same receipt, no second charge.
    bool TickReplay(double Now, ALDPlayerController& PC)
    {
        if (!Call.bSent)
        {
            if (!BeginCall(PC, TEXT("replay-buy-1"))) { return Finish(); }
            Call.Sent = Buy1Sent;
            PC.ServerShopCommand(Buy1Sent);
            return true;
        }
        const FLDSubmitResult* Answered = FindSubmit(ELDGrowthFamily::Shop, Buy1Sent.RequestId, Call.SubmitMark);
        if (!Answered)
        {
            if (Now - Call.SentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("A24"), TEXT("The replayed Buy was not answered"));
        }
        // A replay that executed anyway would publish within this quiet period.
        if (Call.QuietUntil == 0) { Call.QuietUntil = Now + LDDemoGrowthScenario::GrowthProbeQuietSeconds; }
        if (Now < Call.QuietUntil) { return true; }
        FString Drift;
        Check(Answered->Status == ELDAdmissionStatus::Final && Answered->bHasShopReceipt
            && LDDemoGrowthScenario::GrowthProbeSameShop(Answered->ShopReceipt, Buy1Receipt) && Unchanged(PC, Drift), TEXT("A24"),
            TEXT("Resending the first Buy's exact command returns its original final receipt and charges nothing again"), Drift);
        RecordShop(PC, TEXT("Replayed"), LDDemoGrowthScenario::GrowthProbeReason(Answered->Reason),
            Answered->bHasShopReceipt ? &Answered->ShopReceipt : nullptr);
        if (!Passed) { return Finish(); }
        Call = FShopCall();
        bActivateSent = false;
        SetStage(EStage::Activate);
        return true;
    }

    // The first copy's untargeted active through the owner's own entry (what its hotkey sends): only that copy's
    // persistent track starts its cooldown, the second copy's track stays ready.
    bool TickActivate(double Now, ALDPlayerController& PC)
    {
        const ELDCombatEntry UsedEntry = LDDemoGrowthScenario::GrowthProbeItemEntry(Buy1Receipt.SlotIndex);
        const ELDCombatEntry KeptEntry = LDDemoGrowthScenario::GrowthProbeItemEntry(Buy2Receipt.SlotIndex);
        const FLDCombatOwnerView& Own = PC.CombatView;
        const bool bCurrent = Own.Generation == PC.OwnerState.Match && Own.Life == PC.OwnerState.Life;
        const FLDCombatEntryView* Used = bCurrent ? LDDemoGrowthScenario::GrowthProbeEntry(Own, UsedEntry) : nullptr;
        const FLDCombatEntryView* Kept = bCurrent ? LDDemoGrowthScenario::GrowthProbeEntry(Own, KeptEntry) : nullptr;
        if (!bActivateSent)
        {
            const bool bBound = Used && Kept && Used->DefinitionId == ItemId && Kept->DefinitionId == ItemId
                && Used->ItemInstance == Buy1Receipt.Instance && Kept->ItemInstance == Buy2Receipt.Instance
                && Used->IssuedCooldownId.IsValid() && Kept->IssuedCooldownId.IsValid();
            if (!bBound)
            {
                if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(TEXT("A19"), TEXT("The Combat owner view never showed both purchased copies bound to their own tracks"));
            }
            if (!Check(Used->IssuedCooldownId != Kept->IssuedCooldownId && Used->ReadyAtMs <= Own.AsOfMs && Kept->ReadyAtMs <= Own.AsOfMs
                && Used->bHasActive && Used->TargetMode == uint8(ELDDemoTargetMode::Self), TEXT("A19"),
                TEXT("Two copies of one definition hold two distinct persistent cooldown tracks, both ready before use"),
                FString::Printf(TEXT("tracks %s / %s ready at %lld / %lld as of %lld"),
                    *LDDemoGrowthScenario::GrowthProbeGuid(Used->IssuedCooldownId), *LDDemoGrowthScenario::GrowthProbeGuid(Kept->IssuedCooldownId),
                    Used->ReadyAtMs, Kept->ReadyAtMs, Own.AsOfMs)))
            {
                return Finish();
            }
            UsedTrack = Used->IssuedCooldownId;
            KeptTrack = Kept->IssuedCooldownId;
            ActivateReceiptMark = PC.CombatReceiptSerial;
            PC.ActivateCombatEntry(UsedEntry);
            bActivateSent = true;
            StageAt = Now;
            return true;
        }
        const FLDCombatActionReceipt& Latest = PC.LatestCombatReceipt;
        if (PC.CombatReceiptSerial != ActivateReceiptMark && Latest.Entry == UsedEntry && Latest.Phase == ELDCombatPhase::Rejected)
        {
            return Fail(TEXT("A19"), TEXT("The item's active was rejected: ")
                + StaticEnum<ELDCombatReject>()->GetNameStringByValue(int64(Latest.Reject)));
        }
        if (!Used || Used->IssuedCooldownId != UsedTrack || Used->ReadyAtMs <= Own.AsOfMs)
        {
            if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("A19"), TEXT("Using the first copy's active did not put its own track on cooldown"));
        }
        UsedReadyAtMs = Used->ReadyAtMs;
        Check(Kept && Kept->IssuedCooldownId == KeptTrack && Kept->ReadyAtMs <= Own.AsOfMs, TEXT("A19"),
            TEXT("Using one copy's active cools only its own track; the other copy's track stays ready"),
            FString::Printf(TEXT("used ready at %lld, kept ready at %lld, as of %lld"), UsedReadyAtMs, Kept ? Kept->ReadyAtMs : -1,
                Own.AsOfMs));
        if (!Passed) { return Finish(); }
        Call = FShopCall();
        SetStage(EStage::Sell);
        return true;
    }

    bool TickSell(double Now, ALDPlayerController& PC)
    {
        if (!Call.bSent)
        {
            const FLDShopSellView* Resale = SellQuote(PC, Buy1Receipt.Instance);
            FLDShopCommand Command;
            if (!Resale || !ShopTerms(PC, ELDShopCommandKind::Sell, Command))
            {
                return Fail(TEXT("A15"), TEXT("The open quote does not offer the first copy for sale"));
            }
            Command.Instance = Resale->Instance;
            Command.ExpectedItemRevision = Resale->ItemRevision;
            Command.SellPrice = Resale->SellPrice;
            return SendShop(PC, TEXT("sell-1"), Command) ? true : Finish();
        }
        const FLDShopReceipt* Seen = nullptr;
        FString Why;
        const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitShop(Now, PC, Seen, Why);
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Failed)
        {
            return Fail(TEXT("A15"), Call.Step + TEXT(" did not complete: ") + Why);
        }
        CheckApplied(PC, *Seen, Call.Sent.SellPrice);
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        const FLDEquipmentSlotView* Freed = EquipView ? LDDemoGrowthScenario::GrowthProbeSlot(EquipView->Slots, Buy1Receipt.SlotIndex) : nullptr;
        const FLDEquipmentSlotView* Kept = EquipView ? LDDemoGrowthScenario::GrowthProbeSlot(EquipView->Slots, Buy2Receipt.SlotIndex) : nullptr;
        Check(Seen->Instance == Buy1Receipt.Instance && Freed && !Freed->bOccupied && Kept && Kept->bOccupied
            && Kept->Instance == Buy2Receipt.Instance, TEXT("A15"),
            TEXT("Selling consumes exactly the first instance and frees its slot; the second copy stays"),
            LDDemoGrowthScenario::GrowthProbeShopText(*Seen));
        // The sold copy's track left with its instance, yet its cooldown debt stays with Combat: the quote refreshed by
        // the sale's own publication shows that a new copy would inherit exactly that debt.
        const FLDShopView* ShopView = PC.ReadOwnShop();
        const FLDShopItemView* Quote = ShopView ? ShopView->Items.FindByPredicate([this](const FLDShopItemView& ItemRow)
        {
            return ItemRow.DefinitionId == ItemId;
        }) : nullptr;
        Check(Quote && Quote->bInheritsCooldown && Quote->InheritedReadyAtMs == UsedReadyAtMs
            && Quote->InheritedReadyAtMs > PC.CombatView.AsOfMs, TEXT("A21"),
            TEXT("Selling the used copy unbinds it, yet its cooldown debt stays: the quote shows a new copy would inherit it"),
            FString::Printf(TEXT("inherits=%d ready_at=%lld used_ready_at=%lld as_of=%lld"), Quote && Quote->bInheritsCooldown ? 1 : 0,
                Quote ? Quote->InheritedReadyAtMs : -1, UsedReadyAtMs, PC.CombatView.AsOfMs));
        FirstDebtReadyAtMs = Quote ? Quote->InheritedReadyAtMs : 0;
        RecordShop(PC, TEXT("Applied"), LDDemoGrowthScenario::GrowthProbeReason(Seen->Header.Reason), Seen);
        if (!Passed) { return Finish(); }
        Call = FShopCall();
        PC.Navigation->Navigate(KillSite);
        NavigateAttempts = 1;
        SetStage(EStage::WalkOut);
        return true;
    }

    // Leaving ends the interaction binding, not the session; the Transactions phase re-checks the real reach.
    bool TickOutside(double Now, ALDPlayerController& PC, const ALDCharacter& Body, const FLDLobbyView& Lobby)
    {
        if (!Call.bSent)
        {
            FName BoundShop;
            uint32 BoundLife = 0;
            const double Distance = FVector::Dist2D(Body.GetNavAgentLocation(), ShopCentre);
            if (!Check(Distance > ShopReach && !PC.ReadShopInteraction(BoundShop, BoundLife) && PC.ReadOwnShop() != nullptr, TEXT("A17"),
                TEXT("Walking away ends the interaction binding while the session stays open, outside the approved reach"),
                FString::Printf(TEXT("distance=%.1f reach=%.1f"), Distance, ShopReach)))
            {
                return Finish();
            }
            FLDShopCommand Command;
            if (!BuyTerms(PC, Command))
            {
                return Fail(TEXT("A17"), TEXT("The still-open quote does not offer the planned item for an out-of-range Buy"));
            }
            return SendShop(PC, TEXT("buy-outside"), Command) ? true : Finish();
        }
        const FLDShopReceipt* Seen = nullptr;
        FString Why;
        const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitShop(Now, PC, Seen, Why);
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Failed)
        {
            return Fail(TEXT("A17"), Call.Step + TEXT(" did not complete: ") + Why);
        }
        CheckRejected(PC, *Seen, ELDGrowthReason::OutsideRange, TEXT("A17"),
            TEXT("A Buy with a valid session from outside the reach is refused OutsideRange at commit"));
        RecordShop(PC, TEXT("Rejected"), LDDemoGrowthScenario::GrowthProbeReason(Seen->Header.Reason), Seen);
        Check(Kills >= 1 && bChoiceDone && Buy1Receipt.Header.Outcome == ELDReceiptOutcome::Applied, TEXT("A01"),
            TEXT("From zero: real kills, a choice and a purchase came only through the owning connection, with no grant or teleport"),
            FString::Printf(TEXT("kills=%d"), Kills));
        Check(Lobby.Phase == ELDMatchPhase::Development || Lobby.Phase == ELDMatchPhase::CaptureOpen, TEXT("A01"),
            TEXT("The match stayed open through the whole scenario"));
        if (!Passed) { return Finish(); }
        // The first match is done and dirty; the Host ends it and the second match must start from clean owner state.
        RecordFirstMatch(PC);
        if (!Mark(TEXT("Client1Match1Done.json"))) { return Finish(); }
        SetStage(EStage::AwaitLobby);
        return true;
    }

    // Client1, second match: the first match's exact choice and Buy, and a query for that Buy, through this connection.
    bool TickSecondReplay(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        const FLDEquipmentView* EquipView = PC.ReadOwnEquipment();
        if (!ProgView || !EquipView) { return Fail(TEXT("Iso"), TEXT("The second match's owner views became unavailable")); }
        if (!bReplaySent)
        {
            ReplayProgBefore = *ProgView;
            ReplayEquipBefore = *EquipView;
            ReplaySubmitMark = Submits.Num();
            ReplayResultMark = ShopResults.Num();
            PC.ServerGrowthChoice(ChoiceSent);
            PC.ServerShopCommand(Buy1Sent);
            FLDReceiptQuery Query;
            Query.Generation = FirstGeneration;
            Query.Family = ELDGrowthFamily::Shop;
            Query.RequestId = Buy1Sent.RequestId;
            PC.QueryGrowthReceipt(Query);
            bReplaySent = true;
            ReplaySentAt = Now;
            ReplayQuietUntil = 0;
            return true;
        }
        const FLDSubmitResult* ChoiceAnswer = FindSubmit(ELDGrowthFamily::Choice, ChoiceSent.RequestId, ReplaySubmitMark);
        const FLDSubmitResult* ShopAnswer = FindSubmit(ELDGrowthFamily::Shop, Buy1Sent.RequestId, ReplaySubmitMark);
        const FLDShopReceiptQueryResult* QueryAnswer = nullptr;
        for (int32 ResultIndex = ReplayResultMark; ResultIndex < ShopResults.Num() && !QueryAnswer; ++ResultIndex)
        {
            const FLDShopReceiptQueryResult& Answer = ShopResults[ResultIndex];
            if (Answer.Query.Generation == FirstGeneration && Answer.Query.RequestId == Buy1Sent.RequestId)
            {
                QueryAnswer = &Answer;
            }
        }
        if (!ChoiceAnswer || !ShopAnswer || !QueryAnswer)
        {
            if (Now - ReplaySentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("Iso"), FString::Printf(TEXT("The stale first-match requests were not all answered (choice=%d shop=%d query=%d)"),
                ChoiceAnswer ? 1 : 0, ShopAnswer ? 1 : 0, QueryAnswer ? 1 : 0));
        }
        // A stale request that executed anyway would publish within this quiet period.
        if (ReplayQuietUntil == 0) { ReplayQuietUntil = Now + LDDemoGrowthScenario::GrowthProbeQuietSeconds; }
        if (Now < ReplayQuietUntil) { return true; }
        Check(ChoiceAnswer->Status == ELDAdmissionStatus::AdmissionRejected && ChoiceAnswer->Generation == FirstGeneration
            && LDGrowthWire::Reason(ChoiceAnswer->Reason) == ELDGrowthReason::OldMatch && !ChoiceAnswer->bSequenceReserved
            && !ChoiceAnswer->bHasChoiceReceipt, TEXT("Iso"),
            TEXT("The first match's exact choice request is refused as OldMatch at admission in the second match"),
            FString::Printf(TEXT("generation=%u request=%llu reason=%s"), ChoiceAnswer->Generation, ChoiceAnswer->RequestId,
                *LDDemoGrowthScenario::GrowthProbeReason(ChoiceAnswer->Reason)));
        Check(ShopAnswer->Status == ELDAdmissionStatus::AdmissionRejected && ShopAnswer->Generation == FirstGeneration
            && LDGrowthWire::Reason(ShopAnswer->Reason) == ELDGrowthReason::OldMatch && !ShopAnswer->bSequenceReserved
            && !ShopAnswer->bHasShopReceipt, TEXT("Iso"),
            TEXT("The first match's exact Buy with its old session is refused as OldMatch at admission in the second match"),
            FString::Printf(TEXT("generation=%u request=%llu session=%s reason=%s"), ShopAnswer->Generation, ShopAnswer->RequestId,
                *LDDemoGrowthScenario::GrowthProbeGuid(Buy1Sent.SessionId), *LDDemoGrowthScenario::GrowthProbeReason(ShopAnswer->Reason)));
        Check(QueryAnswer->Lookup == ELDReceiptLookup::InvalidContext && !QueryAnswer->bHasReceipt && !QueryAnswer->bHasPending, TEXT("Iso"),
            TEXT("A receipt query for the first match's Buy finds no receipt in the second match (InvalidContext)"));
        const FLDProgressionView* ProgAfter = PC.ReadOwnProgression();
        const FLDEquipmentView* EquipAfter = PC.ReadOwnEquipment();
        Check(ProgAfter && EquipAfter && LDDemoGrowthScenario::GrowthProbeSameOwner(ReplayProgBefore, ReplayEquipBefore, *ProgAfter, *EquipAfter)
            && PC.ReadOwnShop() == nullptr && PC.PeekGrowthRequestId(ELDGrowthFamily::Choice) == 1
            && PC.PeekGrowthRequestId(ELDGrowthFamily::Shop) == 1, TEXT("Iso"),
            TEXT("The stale requests change no growth, gold, equipment, shop session or request sequence of the second match"),
            ProgAfter ? LDDemoGrowthScenario::GrowthProbeProgressionText(*ProgAfter) : FString(TEXT("no progression view")));
        if (!Passed) { return Finish(); }
        bApproachSent = false;
        ApproachAttempts = 0;
        SetStage(EStage::ShopApproach);
        return true;
    }

    // Client1, second match: a fresh session whose quote shows no inherited cooldown, so no track or debt survived.
    bool TickSecondQuote(ALDPlayerController& PC)
    {
        const FLDShopView* ShopView = PC.ReadOwnShop();
        const FLDShopItemView* Quote = ShopView ? ShopView->Items.FindByPredicate([this](const FLDShopItemView& ItemRow)
        {
            return ItemRow.DefinitionId == ItemId;
        }) : nullptr;
        const bool bNoDebt = ShopView && !ShopView->Items.ContainsByPredicate([](const FLDShopItemView& ItemRow)
        {
            return ItemRow.bInheritsCooldown || ItemRow.InheritedReadyAtMs != 0;
        });
        Check(ShopView && Opened.View.SessionId != FirstSessionId && Quote && Quote->bHasActive && bNoDebt && ShopView->Sellable.IsEmpty(),
            TEXT("Iso"), TEXT("The second match's fresh shop quote shows no inherited cooldown: the first match's debt did not survive"),
            FString::Printf(TEXT("session %s -> %s, first-match debt until %lld"), *LDDemoGrowthScenario::GrowthProbeGuid(FirstSessionId),
                ShopView ? *LDDemoGrowthScenario::GrowthProbeGuid(ShopView->SessionId) : TEXT("none"), FirstDebtReadyAtMs));
        if (Passed) { Mark(TEXT("Client1Match2Done.json")); }
        return Finish();
    }

    bool Finish()
    {
        if (Written) { return true; }
        Unbind();
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), Passed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetBoolField(TEXT("dev_probe"), true);
        Report->SetBoolField(TEXT("normal_start"), true);
        Report->SetStringField(TEXT("start_path"),
            TEXT("Lobby AI count, readiness and Start through owning session RPCs with every normal-start provider registered"));
        Report->SetStringField(TEXT("scope"), IsHost()
            ? TEXT("DEV-only owned-connection growth/shop scenario, victim and ledger-export role: a rendered, participating Listen Host that never attacks, ends the first match without a winner and checks its own state in a second normal match. Not camps, AI, capture or full-match acceptance.")
            : TEXT("DEV-only owned-connection growth/shop scenario, buyer role: real kill rewards, one choice, two-slot shop trades and an item active through the owning connection, then clean owner state and refused stale requests in a second normal match. Not camps, AI, capture or full-match acceptance."));
        Report->SetStringField(TEXT("catalog_sha256"), CatalogDigest);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetNumberField(TEXT("stage"), int32(Stage));
        Report->SetArrayField(TEXT("checks"), Checks);
        Report->SetNumberField(TEXT("second_generation"), SecondGeneration);
        Report->SetObjectField(TEXT("match1"), FirstMatchRow.IsValid() ? FirstMatchRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
        if (const ALDPlayerController* PC = Controller.Get())
        {
            Report->SetNumberField(TEXT("generation"), PC->OwnerState.Match);
            Report->SetStringField(TEXT("entity"), LDDemoGrowthScenario::GrowthProbeGuid(PC->OwnerState.ContenderId));
            Report->SetNumberField(TEXT("life"), PC->OwnerState.Life);
            if (const FLDEquipmentView* EquipView = PC->ReadOwnEquipment())
            {
                Report->SetNumberField(TEXT("final_gold"), double(EquipView->Gold));
                Report->SetNumberField(TEXT("final_account_revision"), double(EquipView->AccountRevision));
                Report->SetArrayField(TEXT("final_items"), LDDemoGrowthScenario::GrowthProbeItems(*EquipView));
            }
            if (const FLDProgressionView* ProgView = PC->ReadOwnProgression())
            {
                Report->SetNumberField(TEXT("final_level"), ProgView->Level);
                Report->SetNumberField(TEXT("final_xp"), double(ProgView->CumulativeXP));
                Report->SetNumberField(TEXT("final_pending_choices"), ProgView->PendingChoiceCount);
            }
        }
        if (IsHost())
        {
            Report->SetStringField(TEXT("start_gate_blocker"), StartBlocker);
        }
        else
        {
            Report->SetStringField(TEXT("item"), ItemId.ToString());
            Report->SetNumberField(TEXT("item_buy_price"), double(ItemBuyPrice));
            Report->SetNumberField(TEXT("item_sell_gold"), double(ItemSellGold));
            Report->SetNumberField(TEXT("gold_goal"), double(GoldGoal));
            Report->SetNumberField(TEXT("kills_planned"), KillsPlanned);
            Report->SetNumberField(TEXT("kills"), Kills);
            Report->SetArrayField(TEXT("kill_rows"), KillRows);
            Report->SetObjectField(TEXT("choice"), ChoiceRow.IsValid() ? ChoiceRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
            Report->SetArrayField(TEXT("shop_steps"), ShopRows);
        }
        Written = Save(Report, Role + TEXT(".json"));
        UE_LOG(LogTemp, Display, TEXT("LD_DEMO_GROWTH_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
        if (!Passed || !Written)
        {
            FPlatformMisc::RequestExitWithStatus(false, 2);
            return false;
        }
        return true;
    }
};

void LDDemoTests::StartGrowthProbe(ALDPlayerController& Controller)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoGrowthProbe")) || !Controller.IsLocalController()
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap)
    {
        return;
    }
    const TSharedRef<FLDDemoGrowthProbe> Probe = MakeShared<FLDDemoGrowthProbe>();
    int32 Timeout = 1500;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoGrowthTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoGrowthOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoGrowthRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1")) || Timeout < 300 || Timeout > 2400
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_GROWTH_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    Probe->Controller = &Controller;
    Probe->World = Controller.GetWorld();
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    Probe->Bind(Controller);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .1f);
}
#endif
