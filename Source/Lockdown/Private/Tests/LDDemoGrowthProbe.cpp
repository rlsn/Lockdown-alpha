#include "Tests/LDDemoGrowthProbe.h"
#include "Tests/LDScenarioWalk.h"
#include "Core/LDLog.h"
#include "UI/LDTerms.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/LDCharacter.h"
#include "Combat/LDCombatTypes.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Development/LDDevGrowthFixture.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDGrowthDirector.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDProgressionRules.h"
#include "Framework/LDCaptureTypes.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDMatchProviders.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Navigation/LDPathSteering.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "UI/LDDevTestPanel.h"
#include "UI/LDHUD.h"
#include "UI/LDHUDModel.h"
#include "UI/LDUIRoot.h"
#include "UI/LDWorldFeedback.h"
#include "UObject/UnrealType.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDWorldViewRelay.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
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
#include "GameFramework/CharacterMovementComponent.h"
#include "World/LDCampDirector.h"

// Owned-connection growth scenario (docs/demo/PROGRESSION_AND_REWARDS.md). The owning client earns XP and sends its
// build commands only through its own connection; the Host plays the victim and audits the growth records afterwards.
// The probe never calls a growth driver entry, a Commit* handler or an _Implementation. Named, not anonymous: unity
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
// #198: a match opens in the rest R0, whose private rest body is each contender's life 1; the R0 return retires it without
// a death and places the first field life, life 2. Later field lives follow respawns and the mid rests R1..R3 (each rest
// retires the field life, places a rest life and returns a new field life) and are tracked, never assumed.
constexpr uint32 GrowthProbeFieldLife = 2;
// The client sends its R0 return refusals only while at least this much of the return countdown is left (probe margin).
constexpr int64 GrowthProbeReturnMarginMs = 1500;
// An in-combat probe (#222) is sent only while the combat window has at least this long left, so the server commits it in
// combat rather than after the window passed (probe timing margin, not a product number).
constexpr int64 GrowthProbeUnsafeMarginMs = 500;

enum class EGrowthProbeWait : uint8 { Waiting, Ready, Failed };

double GrowthProbeRational(const FLDDemoRational& Value)
{
    return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : -1.0;
}

using FGrowthProbeKeepout = LDScenarioWalk::FKeepout;

int32 GrowthProbeRouteKeepout(const TArray<FVector>& Path, const TArray<FGrowthProbeKeepout>& Keepouts)
{
    return LDScenarioWalk::RouteKeepout(Path, Keepouts);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGrowthProbeDetourGeometryTest, "Lockdown.Rules.DemoGrowthProbe.CampDetourGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

FString GrowthProbeGuid(const FGuid& Id)
{
    return Id.ToString(EGuidFormats::DigitsWithHyphensLower);
}

FString GrowthProbeReason(uint16 Raw)
{
    return LDGrowthWire::ReasonName(LDGrowthWire::Reason(Raw));
}

FString GrowthProbePointsText(const FLDProgressionView& View)
{
    return FString::JoinBy(View.StatPoints, TEXT(","), [](const FLDStatPointsView& Row)
        { return FString::Printf(TEXT("%s:%d"), *Row.StatId.ToString(), Row.Points); });
}

FString GrowthProbeActivesText(const FLDProgressionView& View)
{
    static const TCHAR* const Slots[] = {TEXT("Q"), TEXT("W"), TEXT("E")};
    const FString Text = FString::JoinBy(View.Actives, TEXT(","), [](const FLDActiveSlotView& Row)
        { return Row.DefinitionId.ToString() + TEXT("/") + (Row.Slot < 3 ? Slots[Row.Slot] : TEXT("?")); });
    return (Text.IsEmpty() ? FString(TEXT("none")) : Text) + TEXT(" blueprints=")
        + FString::JoinBy(View.PendingBlueprints, TEXT(","), [](FName Id) { return Id.ToString(); });
}

FString GrowthProbeProgressionText(const FLDProgressionView& View)
{
    return FString::Printf(TEXT("level=%u xp=%lld earned=%u consumed=%u queued=%u offer=%d unspent=%lld points=[%s] actives=%s "
        "progression=%llu build=%llu"), uint32(View.Level), View.CumulativeXP, uint32(View.ActiveOpportunitiesEarned),
        uint32(View.ActiveOpportunitiesConsumed), uint32(View.QueuedActiveOpportunities), View.bHasActiveOffer ? 1 : 0,
        View.UnspentPoints, *GrowthProbePointsText(View), *GrowthProbeActivesText(View),
        View.ProgressionRevision, View.BuildRevision);
}

bool GrowthProbeSameRevisions(const FLDResultRevisions& A, const FLDResultRevisions& B)
{
    return A.bHasProgression == B.bHasProgression && A.ProgressionRevision == B.ProgressionRevision
        && A.bHasBuild == B.bHasBuild && A.BuildRevision == B.BuildRevision && A.bHasOffer == B.bHasOffer
        && A.OfferRevision == B.OfferRevision;
}

bool GrowthProbeSameHeader(const FLDGrowthReceiptHeader& A, const FLDGrowthReceiptHeader& B)
{
    return A.Generation == B.Generation && A.Family == B.Family && A.RequestId == B.RequestId && A.OriginalLife == B.OriginalLife
        && A.OriginalLifePhase == B.OriginalLifePhase && A.Outcome == B.Outcome && A.Reason == B.Reason
        && A.bHasCommitBatch == B.bHasCommitBatch && A.CommitBatch == B.CommitBatch
        && GrowthProbeSameRevisions(A.Revisions, B.Revisions);
}

bool GrowthProbeSameBuild(const FLDBuildReceipt& A, const FLDBuildReceipt& B)
{
    return GrowthProbeSameHeader(A.Header, B.Header) && A.Kind == B.Kind && A.OfferId == B.OfferId
        && A.OpportunityOrdinal == B.OpportunityOrdinal && A.DefinitionId == B.DefinitionId && A.ChoiceResult == B.ChoiceResult
        && A.LearnedSlot == B.LearnedSlot && A.StoredReason == B.StoredReason && A.PointsSpent == B.PointsSpent
        && A.UnspentPointsAfter == B.UnspentPointsAfter && A.bHasNextOffer == B.bHasNextOffer && A.NextOfferId == B.NextOfferId;
}

const FLDCombatEntryView* GrowthProbeEntry(const FLDCombatOwnerView& Own, ELDCombatEntry Wanted)
{
    return Own.Entries.FindByPredicate([Wanted](const FLDCombatEntryView& EntryRow) { return EntryRow.Entry == Wanted; });
}

// Owner growth state a stale request must leave untouched: level, XP, opportunities, points, build and revisions.
bool GrowthProbeSameOwner(const FLDProgressionView& ProgA, const FLDProgressionView& ProgB)
{
    return ProgA.Level == ProgB.Level && ProgA.CumulativeXP == ProgB.CumulativeXP
        && ProgA.ProgressionRevision == ProgB.ProgressionRevision && ProgA.BuildRevision == ProgB.BuildRevision
        && ProgA.ActiveOpportunitiesEarned == ProgB.ActiveOpportunitiesEarned
        && ProgA.ActiveOpportunitiesConsumed == ProgB.ActiveOpportunitiesConsumed
        && ProgA.QueuedActiveOpportunities == ProgB.QueuedActiveOpportunities && ProgA.bHasActiveOffer == ProgB.bHasActiveOffer
        && ProgA.ActiveOffer.OfferId == ProgB.ActiveOffer.OfferId && ProgA.UnspentPoints == ProgB.UnspentPoints
        && GrowthProbePointsText(ProgA) == GrowthProbePointsText(ProgB) && GrowthProbeActivesText(ProgA) == GrowthProbeActivesText(ProgB);
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

// The buyer's three active offers in order, staged by the Host in the Lobby through the DEV growth fixture
// (-LDDevGrowthFixture): the match's opening offer, then the opportunities of the two approved offer levels. The buyer
// takes the first option every time: snark_bolt learned into Q in the opening rest R0, not_mad learned into W while its
// combat window still runs, pants_down learned into E while dead (#222: build commands at all times). A preset may name any active; the opening one stays inside the opening pool.
const TCHAR* const GrowthProbePlan[][3] = {
    {TEXT("active_snark_bolt"), TEXT("active_pants_down"), TEXT("active_seethe")},
    {TEXT("active_not_mad"), TEXT("active_stare_down"), TEXT("active_flyer_trap")},
    {TEXT("active_pants_down"), TEXT("active_stare_down"), TEXT("active_seethe")},
};
constexpr int32 GrowthProbePlannedOffers = UE_ARRAY_COUNT(GrowthProbePlan);
// Every probe allocation goes to this stat, so each one shows as a larger maximum HP (a probe choice, not a product rule).
const TCHAR* const GrowthProbePointStat = TEXT("stat_physique");
// DEV test commands (#78): the Host's own DEV XP grant in the second match (probe-only, an arbitrary small value below
// any level threshold, not a product number), and the bound for one DEV answer to settle in the owner view (terminal
// result plus the replicated owner state).
constexpr int64 GrowthProbeHostDevXP = 7;
constexpr double GrowthProbeDevSettleSeconds = 15.0;
// Probe-only DEV attribute term: +12 AD points (P2), an arbitrary visible value, not a product number.
constexpr int64 GrowthProbeDevADP2 = 1200;
// The DEV learn names an active outside the opening pool, so it is never one of the second match's opening options; the
// DEV allocation names the stat whose points show in the AD row.
const TCHAR* const GrowthProbeDevActive = TEXT("active_not_mad");
const TCHAR* const GrowthProbeDevStat = TEXT("stat_strength");
constexpr double GrowthProbeCampSearchCm = 600.0;
constexpr int32 GrowthProbeLowCampMembers = 2;

// Private rendered HUD widgets, read through reflection without changing the HUD for the probe.
const UWidget* GrowthProbeHudWidget(const ALDPlayerController& PC, const TCHAR* Field)
{
    const ULDHUD* Hud = PC.HUDUI;
    const FObjectProperty* Property = Hud ? FindFProperty<FObjectProperty>(ULDHUD::StaticClass(), Field) : nullptr;
    return Property ? Cast<UWidget>(Property->GetObjectPropertyValue_InContainer(Hud)) : nullptr;
}

FString GrowthProbeHudText(const ALDPlayerController& PC, const TCHAR* Field)
{
    const UTextBlock* Text = Cast<UTextBlock>(GrowthProbeHudWidget(PC, Field));
    return Text ? Text->GetText().ToString() : FString();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGrowthProbeHudRewardBindingTest, "Lockdown.Rules.DemoGrowthProbe.HudRewardBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDGrowthProbeHudRewardBindingTest::RunTest(const FString& Parameters)
{
    {
        const auto* Property = FindFProperty<FObjectProperty>(ULDHUD::StaticClass(), TEXT("LevelBadgeText"));
        TestTrue(TEXT("LevelBadgeText is an actual HUD text widget"),
            Property && Property->PropertyClass->IsChildOf(UTextBlock::StaticClass()));
    }
    const auto* Level = FindFProperty<FObjectProperty>(ULDHUD::StaticClass(), TEXT("LevelBar"));
    TestTrue(TEXT("The XP display is the actual HUD progress widget"),
        Level && Level->PropertyClass->IsChildOf(UProgressBar::StaticClass()));
    return true;
}

// The own reward lines the Host's rendered world feedback presented this match, read through reflection like the HUD
// texts above; null when the layer does not exist.
const TArray<FString>* GrowthProbeRewardLines(const ALDPlayerController& PC)
{
    const ULDUIRoot* Root = PC.RootUI;
    const FObjectProperty* Property = Root ? FindFProperty<FObjectProperty>(ULDUIRoot::StaticClass(), TEXT("WorldFeedback")) : nullptr;
    const ULDWorldFeedback* Feedback = Property ? Cast<ULDWorldFeedback>(Property->GetObjectPropertyValue_InContainer(Root)) : nullptr;
    return Feedback ? &Feedback->DevPresentedRewards() : nullptr;
}

// True when the presented lines end with rewards adding up to exactly XP: walking back from the newest line,
// "+N <LDTerms::Experience>" lines are summed until the total is reached. Every reward floats one XP line.
bool GrowthProbeRewardTail(const TArray<FString>& Lines, int64 XP, FString& Seen)
{
    int64 SumXP = 0;
    int32 XPLines = 0;
    for (int32 Index = Lines.Num() - 1; Index >= 0 && SumXP < XP; --Index)
    {
        Seen = Lines[Index] + (Seen.IsEmpty() ? FString() : TEXT(" | ") + Seen);
        FString Number = Lines[Index];
        if (!Number.RemoveFromEnd(FString(TEXT(" ")) + LDTerms::Experience) || !Number.RemoveFromStart(TEXT("+"))
            || !Number.IsNumeric()) { return false; }
        SumXP += FCString::Atoi64(*Number);
        ++XPLines;
    }
    return SumXP == XP && XPLines > 0;
}

TArray<FName> GrowthProbePlannedOptions(int32 Index)
{
    const TCHAR* const* Planned = GrowthProbePlan[Index];
    return {FName(Planned[0]), FName(Planned[1]), FName(Planned[2])};
}

// The head offer is the preset of its opportunity: the same three actives in the same order.
bool GrowthProbeIsPreset(const FLDProgressionView& View)
{
    const int32 Index = int32(View.ActiveOffer.OpportunityOrdinal) - 1;
    return View.bHasActiveOffer && Index >= 0 && Index < GrowthProbePlannedOffers
        && View.ActiveOffer.Options == GrowthProbePlannedOptions(Index);
}

const FLDActiveSlotView* GrowthProbeActiveIn(const FLDProgressionView& View, uint8 Slot)
{
    return View.Actives.FindByPredicate([Slot](const FLDActiveSlotView& Row) { return Row.Slot == Slot; });
}

// Allocated points of one stat in the owner view; -1 when the view has no row for it.
int32 GrowthProbePointsOf(const FLDProgressionView& View, FName StatId)
{
    const FLDStatPointsView* Row = View.StatPoints.FindByPredicate([StatId](const FLDStatPointsView& Candidate)
        { return Candidate.StatId == StatId; });
    return Row ? Row->Points : -1;
}
}

// Global name, matching the other DEV probe classes; it only uses public game mode, controller and growth reads.
class FLDDemoGrowthProbe : public TSharedFromThis<FLDDemoGrowthProbe>
{
public:
    enum class EStage : uint8
    {
        // #198: Rest0 is the first match's opening rest R0, between the normal start and field 1 (T=0); the buyer chooses its
        // opening active and allocates its opening points in its build step.
        Join, Lobby, Rest0, Spawned, Victim,
        ToSite, KillSeek, KillWait,
        // #222 after the kill that reaches the first offer level: the offer learned into W and the points allocated while the
        // own combat window still runs.
        Window,
        // After the last planned kill the Host kills the buyer: the next offer learned into E and points allocated while dead,
        // then E still learned after the respawn.
        AwaitDeath, Dead, Respawn,
        // New-match isolation: the Host ends the first match, the room returns to a new Lobby generation and a
        // second normal match must start from clean owner state.
        EndFirst, AwaitLobby, SecondLobby, SecondSpawned, SecondReplay, SecondAudit,
        // DEV test commands (#78) after the clean second match: the buyer's remote owner RPC, the Host's own as Listen Host.
        SecondDev, HostDev
    };

    TWeakObjectPtr<ALDPlayerController> Controller;
    TWeakObjectPtr<UWorld> World;
    FString Output, Role, Error, CatalogDigest, StartBlocker;
    double Deadline = 0, StageAt = 0, NextCommandAt = 0, NextReadyAt = 0, StartRequestedAt = 0;
    EStage Stage = EStage::Join;
    bool Passed = true, Written = false, bJoined = false, bLobbyConfigured = false, bStartIssued = false, bSitesReady = false;
    bool bGrowthChecked = false;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TArray<TSharedPtr<FJsonValue>> Checks;

    // Public anchors and approved radii: the duel happens on a ring around the capture anchor, outside the capture
    // circle, the airdrop pickups, the dorms and the camp/Boss walking exclusions.
    FVector KillSite = FVector::ZeroVector, HostSite = FVector::ZeroVector;
    FVector WalkSite = FVector::ZeroVector;
    TArray<LDDemoGrowthScenario::FGrowthProbeKeepout> WalkKeepouts;
    TArray<FVector> WalkWaypoints;
    uint32 WalkLife = 0;
    uint32 WalkMatch = 0;
    int32 WalkWaypoint = 0;
    double WalkArrivalCm = 0;
    FGuid HostId, ClientId;

    // Owning-client notifications in arrival order; listeners only record.
    TArray<FLDSubmitResult> Submits;
    TArray<FLDBuildReceiptQueryResult> BuildResults;
    FDelegateHandle SubmitHandle, BuildHandle;

    // Client1 plan: the kill count comes from the catalog's XP curve and offer levels.
    int32 KillsPlanned = 0, Kills = 0, AttackOrders = 0, NavigateAttempts = 0;
    int64 KillXP = 0;
    uint8 VictimLevel = 0;
    FLDMoveIdentity AttackOrder;
    FLDProgressionView KillBefore;
    double KillStartedAt = 0;
    TArray<TSharedPtr<FJsonValue>> KillRows;

    // Host: the victim's current life.
    uint32 VictimLife = 0;

    // One build command in flight and the owner state it was sent against.
    struct FBuildCall
    {
        FString Step;
        FLDBuildCommand Sent;
        FLDProgressionView Before;
        int64 MaxHPBeforeP2 = 0;
        int32 SubmitMark = 0;
        int32 ResultMark = 0;
        uint64 PublishMark = 0;
        double SentAt = 0;
        double QuietUntil = 0;
        bool bSent = false;
    };
    FBuildCall Build;
    // Position inside the current build stage (R0, window, death).
    int32 BuildStep = 0;
    // The applied opening choice: queried again after T=0 and replayed in the second match.
    FLDBuildCommand OpeningSent;
    FLDBuildReceipt OpeningFinal;
    bool bRest0Built = false, bOpeningQuerySent = false, bOpeningQueried = false;
    int32 OpeningQueryMark = 0;

    // #198 opening rest R0 of the first match: Client1's admitted move in its build step, then its refused attack-move,
    // Shift attack-move and Q in its return countdown, and field 1 starting at T=0 when it ends.
    bool bRest0Seen = false, bRest0Refused = false, bRest0ReturnSeen = false;
    int32 Rest0Step = 0;
    int64 Rest0StartedAtMs = 0, Rest0BuildEndsAtMs = 0;
    FVector Rest0Foot = FVector::ZeroVector;
    FLDMoveIdentity Rest0Attack, Rest0Shift, Rest0Move, Rest0QOrder;
    bool bRest0QOrder = false;
    uint32 Rest0CombatMark = 0;
    TSharedPtr<FJsonObject> Rest0Row;

    // #198 mid rests R1..R3 of the first match (TickRestHold). RestHeldAt is the wall time the current hold began, 0 while
    // not holding; the rest's index, entry instant, frozen T, the own field life and public death count at its entry and
    // the newest own life seen while held (the rest life).
    double RestHeldAt = 0;
    uint8 RestHeldIndex = 0;
    uint32 RestLifeBefore = 0, RestLife = 0;
    int64 RestEnteredAtMs = -1, RestFrozenT = -1, RestDeathsBefore = -1;
    // Host: the authority kept both owners in their private rest spaces with T frozen during the held rest.
    bool bRestSpacesSeen = false;
    TArray<TSharedPtr<FJsonValue>> RestRows;
    // This owner's current field life: the first one after R0, then the life after each rest return or respawn.
    uint32 FieldLife = 0;
    // Wall time this owner's view first showed no living FieldLife without any death (a phase boundary that reached the
    // owner view before the public phase); 0 otherwise.
    double FieldLifeLostAt = 0;
    // Waits for a field segment with room for a stage that must finish inside one segment.
    int32 FieldRoomWaits = 0;
    bool bFieldRoomWaiting = false;

    // Client1: the in-combat build after the kill that reached the first offer level.
    bool bWindowDone = false;
    TSharedPtr<FJsonObject> WindowRow;

    // Client1: the death the Host deals and the build commands while dead.
    uint32 DeathLife = 0;
    int64 DeathRespawnAtMs = 0, DeadMaxHPP2 = 0, GrownMaxHPP2 = 0;
    TSharedPtr<FJsonObject> DeathRow;

    // Host: the DEV fixture presets, its own grant and the attack that kills the buyer.
    bool bHostGrantSent = false, bHostGranted = false, bBuyerKilled = false;
    uint64 HostGrantTicket = 0;
    int64 HostGrantXP = 0;
    uint8 HostGrantLevel = 0;
    int32 HostAttackOrders = 0;
    FLDMoveIdentity HostAttackOrder;
    double HostKillStartedAt = 0;

    // Host as killer: its own view and rendered HUD after its kill of the buyer, then after two low-camp last hits.
    struct FHostReward
    {
        int64 XPBefore = 0;
        uint64 PublishBefore = 0;
        double Since = 0;
    };
    FHostReward HostKillReward, HostCampReward;
    uint8 HostKillVictimLevel = 0;
    bool bHostKillRewardSeen = false, bHostAtCamp = false, bHostCampDone = false;
    int32 HostCampKills = 0, HostCampOrders = 0;
    FLDMoveIdentity HostCampOrder;
    FString HostCampAnchor;
    FVector HostCampApproach = FVector::ZeroVector;
    TSharedPtr<FJsonObject> HostKillerRow;

    // D10: the buyer's presets staged in the Lobby, and each match's opening opportunity as the owner first saw it.
    bool bPresetsStaged = false, bOpponentViewChecked = false;
    FGuid StagedClientId;
    TSharedPtr<FJsonObject> OpeningRow, SecondOpeningRow;

    // New-match isolation. The first match's identities and dirty owner state, then the second match's checks.
    bool bSecondMatch = false, bReplaySent = false;
    uint32 FirstGeneration = 0, SecondGeneration = 0;
    FLDCombatBodyRef FirstBody;
    FGuid FirstLedgerMatch;
    TSharedPtr<FJsonObject> FirstMatchRow;
    FLDProgressionView FirstProgression;
    FLDProgressionView ReplayProgBefore;
    int32 ReplaySubmitMark = 0, ReplayResultMark = 0;
    double ReplaySentAt = 0, ReplayQuietUntil = 0;

    // DEV test commands: the request in flight, every owner answer in arrival order and the committed XP grant.
    struct FDevCall
    {
        FString Name;
        FLDDevTestCommand Sent;
        bool bSent = false;
        int32 ResultMark = 0;
        double SentAt = 0;
        // The owner's cumulative XP when the command was sent; -1 without a progression view.
        int64 XPBefore = 0;
        // The owner's projected AD in P2 when the command was sent; -1 when the view had no exact AD row.
        int64 ADBeforeP2 = -1;
    };
    FDevCall Dev;
    int32 DevStep = 0;
    TArray<FLDDevTestResult> DevResults;
    FDelegateHandle DevHandle;
    FLDDevTestCommand DevXPCommand;
    int64 DevGrantXP = 0;
    bool bDevOpenedInDevelopment = false;
    TArray<TSharedPtr<FJsonValue>> DevRows;

    bool IsHost() const
    {
        return Role == TEXT("Host");
    }

    void SetStage(EStage Next)
    {
        Stage = Next;
        StageAt = FPlatformTime::Seconds();
        if (Next != EStage::ToSite) { WalkWaypoints.Reset(); }
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

    // Records the owning controller's growth notifications; the module's own handlers stay bound as well.
    void Bind(ALDPlayerController& Subject)
    {
        const TWeakPtr<FLDDemoGrowthProbe> Weak = AsShared();
        SubmitHandle = Subject.OnGrowthSubmit.AddLambda([Weak](const FLDSubmitResult& Received)
        {
            if (const TSharedPtr<FLDDemoGrowthProbe> Pinned = Weak.Pin()) { Pinned->Submits.Add(Received); }
        });
        BuildHandle = Subject.OnBuildReceipt.AddLambda([Weak](const FLDBuildReceiptQueryResult& Received)
        {
            if (const TSharedPtr<FLDDemoGrowthProbe> Pinned = Weak.Pin()) { Pinned->BuildResults.Add(Received); }
        });
        DevHandle = Subject.OnDevTestResult.AddLambda([Weak](const FLDDevTestResult& Received)
        {
            if (const TSharedPtr<FLDDemoGrowthProbe> Pinned = Weak.Pin()) { Pinned->DevResults.Add(Received); }
        });
    }

    void Unbind()
    {
        ALDPlayerController* Subject = Controller.Get();
        if (!Subject) { return; }
        Subject->OnGrowthSubmit.Remove(SubmitHandle);
        Subject->OnBuildReceipt.Remove(BuildHandle);
        Subject->OnDevTestResult.Remove(DevHandle);
    }

    const FLDSubmitResult* FindSubmit(uint32 Generation, uint64 RequestId, int32 From) const
    {
        for (int32 SubmitIndex = From; SubmitIndex < Submits.Num(); ++SubmitIndex)
        {
            const FLDSubmitResult& Answer = Submits[SubmitIndex];
            if (Answer.Family == ELDGrowthFamily::Build && Answer.Generation == Generation && Answer.RequestId == RequestId)
            {
                return &Answer;
            }
        }
        return nullptr;
    }

    // Any lookup answer for this build request, final or not.
    const FLDBuildReceiptQueryResult* FindBuildAnswer(uint32 Generation, uint64 RequestId, int32 From) const
    {
        for (int32 ResultIndex = From; ResultIndex < BuildResults.Num(); ++ResultIndex)
        {
            const FLDReceiptQuery& Query = BuildResults[ResultIndex].Query;
            if (Query.Family == ELDGrowthFamily::Build && Query.Generation == Generation && Query.RequestId == RequestId)
            {
                return &BuildResults[ResultIndex];
            }
        }
        return nullptr;
    }

    // ---- #198 build commands, only through SubmitBuild on the owning connection ----

    // The authority time as this owner reads it; false until the clock is known.
    static bool AuthorityNowMs(const ALDPlayerController& PC, int64& Out)
    {
        return PC.ReadDemoAuthorityTimeMs(Out);
    }

    int32 PointStatIndex(const TCHAR* StatId) const
    {
        return Catalog->Get().StatDesignIndex(StatId);
    }

    static FLDBuildCommand ChoiceCommand(const FLDProgressionView& View, FName Option)
    {
        FLDBuildCommand Command;
        Command.Kind = ELDBuildCommandKind::ActiveChoice;
        Command.OfferId = View.ActiveOffer.OfferId;
        Command.OfferRevision = View.ActiveOffer.OfferRevision;
        FLDDefinitionWire::Encode(Option, Command.Option);
        Command.bLearnIfPossible = true;
        Command.Slot = FLDBuildCommand::AnySlot;
        return Command;
    }

    static FLDBuildCommand LearnCommand(FName Blueprint, uint8 Slot)
    {
        FLDBuildCommand Command;
        Command.Kind = ELDBuildCommandKind::LearnBlueprint;
        FLDDefinitionWire::Encode(Blueprint, Command.Blueprint);
        Command.Slot = Slot;
        return Command;
    }

    // Points to the probe's point stat, zero elsewhere.
    FLDBuildCommand PointsCommand(int64 Points) const
    {
        FLDBuildCommand Command;
        Command.Kind = ELDBuildCommandKind::AllocatePoints;
        Command.PointCounts.Init(0, Catalog->Get().Stats.Num());
        const int32 Index = PointStatIndex(LDDemoGrowthScenario::GrowthProbePointStat);
        if (Index != INDEX_NONE) { Command.PointCounts[Index] = uint8(FMath::Clamp<int64>(Points, 0, MAX_uint8)); }
        return Command;
    }

    // Sends one build command against the current owner view. SubmitBuild fills Generation, Life, the life phase and the
    // request id; the probe records the exact terms it sent.
    bool SendBuild(ALDPlayerController& PC, const FString& Step, const FLDBuildCommand& Business)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        Build = FBuildCall();
        Build.Step = Step;
        Build.bSent = true;
        if (!Check(View != nullptr && (Business.Kind != ELDBuildCommandKind::AllocatePoints
            || PointStatIndex(LDDemoGrowthScenario::GrowthProbePointStat) != INDEX_NONE), TEXT("A24"),
            Step + TEXT(": the owner progression view and the probe's point stat exist")))
        {
            return false;
        }
        Build.Before = *View;
        Build.MaxHPBeforeP2 = PC.CombatView.MaxHPP2;
        Build.Sent = Business;
        Build.Sent.ExpectedBuildRevision = View->BuildRevision;
        Build.Sent.CatalogDigest = View->CatalogDigest;
        Build.Sent.Generation = PC.OwnerState.Match;
        Build.Sent.Life = PC.OwnerState.Life;
        Build.Sent.ExpectedLifePhase = PC.OwnerState.LifePhase;
        Build.SubmitMark = Submits.Num();
        Build.ResultMark = BuildResults.Num();
        Build.PublishMark = PC.OwnerState.Growth.PublishRevision;
        Build.SentAt = FPlatformTime::Seconds();
        const uint64 Peeked = PC.PeekGrowthRequestId(ELDGrowthFamily::Build);
        ELDGrowthReason Local = ELDGrowthReason::None;
        Build.Sent.RequestId = PC.SubmitBuild(Build.Sent, Local);
        return Check(Build.Sent.RequestId != 0 && Build.Sent.RequestId == Peeked && Local == ELDGrowthReason::None, TEXT("A24"),
            Step + TEXT(" goes through SubmitBuild with the next build request id"),
            FString::Printf(TEXT("request=%llu peek=%llu local=%s"), Build.Sent.RequestId, Peeked, LDGrowthWire::ReasonName(Local)));
    }

    // The build command's pending admission, its final receipt and, when it applied, the owner publication carrying it.
    LDDemoGrowthScenario::EGrowthProbeWait AwaitBuild(double Now, const ALDPlayerController& PC, const FLDBuildReceipt*& Out,
        FString& Why) const
    {
        Out = nullptr;
        const FLDSubmitResult* Admission = FindSubmit(Build.Sent.Generation, Build.Sent.RequestId, Build.SubmitMark);
        const FLDBuildReceiptQueryResult* Answer = FindBuildAnswer(Build.Sent.Generation, Build.Sent.RequestId, Build.ResultMark);
        if (Admission && Admission->Status != ELDAdmissionStatus::AcceptedPending)
        {
            Why = TEXT("not admitted pending: ") + LDDemoGrowthScenario::GrowthProbeReason(Admission->Reason);
            return LDDemoGrowthScenario::EGrowthProbeWait::Failed;
        }
        const FLDProgressionView* View = PC.ReadOwnProgression();
        const bool bFinal = Admission && Answer && Answer->Lookup == ELDReceiptLookup::Final && Answer->bHasReceipt;
        const FLDResultRevisions& Revisions = bFinal ? Answer->Receipt.Header.Revisions : FLDResultRevisions();
        const bool bPublished = bFinal && View && (Answer->Receipt.Header.Outcome != ELDReceiptOutcome::Applied
            || (PC.OwnerState.Growth.PublishRevision > Build.PublishMark
                && (!Revisions.bHasBuild || View->BuildRevision == Revisions.BuildRevision)
                && (!Revisions.bHasProgression || View->ProgressionRevision == Revisions.ProgressionRevision)));
        if (!bPublished)
        {
            if (Now - Build.SentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds)
            {
                return LDDemoGrowthScenario::EGrowthProbeWait::Waiting;
            }
            Why = TEXT("no admission, final receipt and owner publication arrived in time");
            return LDDemoGrowthScenario::EGrowthProbeWait::Failed;
        }
        Out = &Answer->Receipt;
        return LDDemoGrowthScenario::EGrowthProbeWait::Ready;
    }

    const FLDCombatEntryView* CurrentEntry(const ALDPlayerController& PC, ELDCombatEntry Wanted) const
    {
        const FLDCombatOwnerView& Own = PC.CombatView;
        return Own.Generation == PC.OwnerState.Match && Own.Life == PC.OwnerState.Life
            ? LDDemoGrowthScenario::GrowthProbeEntry(Own, Wanted) : nullptr;
    }

    // The owner's combat view row of one attribute, or null.
    static const FLDCombatAttributeRowView* OwnAttribute(const ALDPlayerController& PC, ELDProjectedAttribute Attribute)
    {
        return PC.CombatView.Attributes.FindByPredicate([Attribute](const FLDCombatAttributeRowView& Row)
            { return Row.Attribute == uint8(Attribute); });
    }

    // An exact P2 value of a rational, or -1 when it has none.
    static int64 ExactP2(const FLDDemoRational& Value)
    {
        return Value.Denominator > 0 && (Value.Numerator * 100) % Value.Denominator == 0 ? Value.Numerator * 100 / Value.Denominator : -1;
    }

    // The receipt of an applied allocation and the owner view and Combat projection after it: the probe's stat rose by the
    // sent count, nothing is left unspent and the projected maximum HP grew.
    bool AllocationShown(const ALDPlayerController& PC, const FLDBuildReceipt& Final) const
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        const FName Stat(LDDemoGrowthScenario::GrowthProbePointStat);
        const int32 Index = PointStatIndex(LDDemoGrowthScenario::GrowthProbePointStat);
        const int32 Sent = Index != INDEX_NONE ? Build.Sent.PointCounts[Index] : 0;
        return View && Index != INDEX_NONE && Final.Header.Outcome == ELDReceiptOutcome::Applied
            && Final.Kind == ELDBuildCommandKind::AllocatePoints && Final.PointsSpent == Build.Sent.PointCounts
            && Final.UnspentPointsAfter == View->UnspentPoints && View->UnspentPoints == Build.Before.UnspentPoints - Sent
            && LDDemoGrowthScenario::GrowthProbePointsOf(*View, Stat)
                == LDDemoGrowthScenario::GrowthProbePointsOf(Build.Before, Stat) + Sent
            && PC.CombatView.Generation == PC.OwnerState.Match && PC.CombatView.MaxHPP2 > Build.MaxHPBeforeP2;
    }

    // ---- #198 field segments and mid rests ----

    // The approved field respawn wait (LIFE.RespawnSec.Field1/Field2/Field3/Final) of a death in the field segment the
    // public timeline shows now; a field death's wait is fixed by its T and a rest ends it, so it is always this segment's.
    int64 RespawnMsNow(const ALDPlayerController& PC) const
    {
        const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>();
        const FLDDemoContentParameters& Parameters = Catalog->Get().Parameters;
        switch (Public ? Public->PublicMatch.Timeline.FieldSegment : 0)
        {
        case 0: return Parameters.RespawnField1Ms;
        case 1: return Parameters.RespawnField2Ms;
        case 2: return Parameters.RespawnField3Ms;
        default: return Parameters.RespawnFinalMs;
        }
    }

    double RespawnSecondsNow(const ALDPlayerController& PC) const
    {
        return double(RespawnMsNow(PC)) / 1000.0;
    }

    // The authority ms at which the running field segment ends (the next rest begins): MAX_int64 in the final field, -1
    // outside a field of this owner's match.
    static int64 FieldEndsAtMs(const ALDPlayerController& PC)
    {
        const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>();
        if (!Public) { return -1; }
        const FLDPublicMatchView& Match = Public->PublicMatch;
        if (Match.Generation != PC.OwnerState.Match
            || (Match.Phase != ELDMatchPhase::Development && Match.Phase != ELDMatchPhase::CaptureOpen))
        {
            return -1;
        }
        if (Match.Timeline.Stage == ELDMatchStage::FinalField) { return MAX_int64; }
        return Match.Timeline.Stage == ELDMatchStage::Field ? Match.Timeline.StepEndsAtMs : -1;
    }

    // Field time the running segment has left at this owner's authority time (T and authority time advance together inside
    // a segment): MAX_int64 in the final field, -1 outside a field or before the clock is known.
    static int64 FieldLeftMs(const ALDPlayerController& PC)
    {
        const int64 EndsAtMs = FieldEndsAtMs(PC);
        int64 NowMs = 0;
        if (EndsAtMs < 0 || !AuthorityNowMs(PC, NowMs)) { return -1; }
        return EndsAtMs == MAX_int64 ? MAX_int64 : EndsAtMs - NowMs;
    }

    // Field room, from approved values and the probe's own step budgets. The kill that reaches the first offer level
    // starts an attack order only while the order's budget plus GROWTH.BuildSafeWindowSec fits in the segment: the combat
    // window it opens then ends before the next rest, so every in-combat probe is judged in the field.
    int64 WindowKillNeedMs() const
    {
        return int64(LDDemoGrowthScenario::GrowthProbeKillSeconds * 1000.0) + Catalog->Get().Parameters.BuildSafeWindowMs;
    }

    // The Host starts an attack order on the buyer only while the order's budget, this segment's LIFE.RespawnSec wait (the
    // buyer's whole dead stage runs inside it) and one request bound to observe the respawned life fit in the segment, so a
    // rest can neither end the buyer's respawn wait nor judge one of its dead-stage commands.
    int64 HostKillNeedMs(const ALDPlayerController& PC) const
    {
        return int64((LDDemoGrowthScenario::GrowthProbeKillSeconds + LDDemoGrowthScenario::GrowthProbeRequestSeconds) * 1000.0)
            + RespawnMsNow(PC);
    }

    // One wait for a segment with room (logged once per wait; cleared when a gated step starts).
    void NoteFieldRoomWait(const TCHAR* What, int64 LeftMs, int64 NeedMs)
    {
        if (bFieldRoomWaiting) { return; }
        bFieldRoomWaiting = true;
        ++FieldRoomWaits;
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_GROWTH_PROBE_FIELD_ROOM role=%s wait=%s field_left_ms=%lld need_ms=%lld"),
            *Role, What, LeftMs, NeedMs);
    }

    // The public scoreboard's death count of one contender in a match. Only a real death moves it; a rest retirement
    // (RetireLifeForPhase, PhaseRetired) counts none. -1 while that match's kill feed has no row for the contender.
    static int64 PublicDeaths(const ALDGameState& Public, uint32 Generation, const FGuid& Contender)
    {
        if (Public.KillFeed.Generation != Generation) { return -1; }
        const FLDScoreRow* Row = Public.KillFeed.Scores.FindByPredicate([&Contender](const FLDScoreRow& Candidate)
            { return Candidate.ContenderId == Contender; });
        return Row ? int64(Row->Deaths) : -1;
    }

    static int64 OwnDeaths(const ALDPlayerController& PC)
    {
        const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>();
        return Public ? PublicDeaths(*Public, PC.OwnerState.Match, PC.OwnerState.ContenderId) : -1;
    }

    // A real death of this owner beyond Expected: its view holds a dead life with a field respawn wait (a phase retirement
    // sets none), or the public death count passed Expected. Seen describes both signals.
    bool OwnerDied(const ALDPlayerController& PC, int64 Expected, FString& Seen) const
    {
        const int64 Deaths = OwnDeaths(PC);
        const bool bWaiting = PC.OwnerState.LifePhase == ELDLifePhase::Dead && PC.OwnerState.RespawnAtMs > 0;
        Seen = FString::Printf(TEXT("phase %s life %u (field life %u) respawn_at_ms=%lld public deaths=%lld expected=%lld"),
            *StaticEnum<ELDLifePhase>()->GetNameStringByValue(int64(PC.OwnerState.LifePhase)), PC.OwnerState.Life, FieldLife,
            PC.OwnerState.RespawnAtMs, Deaths, Expected);
        return bWaiting || Deaths > Expected;
    }

    // Whether the buyer's kill in progress reaches the first offer level (the in-combat build stage follows it).
    bool IsWindowKill() const
    {
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        uint8 Level = 0;
        return !bWindowDone && KillXP > 0
            && LDProgression::LevelForXP(Defs.Progression, KillBefore.CumulativeXP + KillXP, Level) == ELDGrowthReason::None
            && int32(Level) >= Defs.Progression.ActiveOfferLevels[0];
    }

    // A held rest is not field time: every running wall-clock budget moves past it. The stages that wait for the victim at
    // its duel site restart, since the return may place it anywhere (a life dead at the rest entry takes a normal spawn).
    void ResumeClocks(double Now, double Held)
    {
        StageAt += Held;
        if (Build.bSent)
        {
            Build.SentAt += Held;
            if (Build.QuietUntil > 0) { Build.QuietUntil += Held; }
        }
        if (KillStartedAt > 0) { KillStartedAt += Held; }
        if (HostKillReward.Since > 0) { HostKillReward.Since += Held; }
        if (HostCampReward.Since > 0) { HostCampReward.Since += Held; }
        if (Stage == EStage::ToSite || Stage == EStage::KillSeek || Stage == EStage::KillWait || Stage == EStage::AwaitDeath)
        {
            StageAt = Now;
        }
        if (HostKillStartedAt > 0) { HostKillStartedAt = Now; }
        NextCommandAt = 0;
        FieldLifeLostAt = 0;
        bFieldRoomWaiting = false;
    }

    // #198 mid rests R1..R3 of the first match. From the first frame the public phase is a mid rest until this owner stands
    // in its next field life, neither role sends a world command or runs a stage budget. At the return the probe checks
    // that the rest retired the field life without a death (public death count unchanged, the rest life and then a new field
    // life) and that T stayed frozen, extends every running budget by the held time and resumes. Stages that must finish
    // inside one segment are gated by field room and fail explicitly if a rest still reaches them. bHeld: skip this tick.
    bool TickRestHold(double Now, ALDPlayerController& PC, const ALDGameState& Public, bool& bHeld)
    {
        bHeld = false;
        if (bSecondMatch || Stage < EStage::Spawned || Stage > EStage::AwaitLobby || Stage == EStage::EndFirst) { return true; }
        const FLDPublicMatchView& Match = Public.PublicMatch;
        const FLDTimelineView& Line = Match.Timeline;
        const FLDDemoContentParameters& Parameters = Catalog->Get().Parameters;
        const bool bRest = Match.Generation == PC.OwnerState.Match && Match.Phase == ELDMatchPhase::Rest && Line.RestIndex > 0;
        if (RestHeldAt == 0)
        {
            if (!bRest) { return true; }
            const FString Where = FString::Printf(TEXT("rest R%u at stage %d step %d"), uint32(Line.RestIndex), int32(Stage), BuildStep);
            if (Stage == EStage::Spawned)
            {
                return Fail(TEXT("Start"), TEXT("Field 1 ended before this role finished its first field checks: ") + Where);
            }
            if ((Stage == EStage::Window && BuildStep <= 2) || (Stage == EStage::KillWait && IsWindowKill()))
            {
                return Fail(TEXT("A15"), TEXT("A rest began inside the kill reaching the first offer level or its in-combat probes, "
                    "which must be judged in one field segment: ") + Where);
            }
            if (Stage == EStage::Dead || Stage == EStage::Respawn
                || (Stage == EStage::AwaitDeath && PC.OwnerState.LifePhase == ELDLifePhase::Dead && PC.OwnerState.RespawnAtMs > 0))
            {
                return Fail(TEXT("A14"), TEXT("A rest began inside the buyer's dead stage or before its respawn was checked, "
                    "which must finish in one field segment: ") + Where);
            }
            RestHeldAt = Now;
            RestHeldIndex = Line.RestIndex;
            RestLifeBefore = IsHost() ? VictimLife : FieldLife;
            RestLife = 0;
            RestEnteredAtMs = Line.Step == ELDRestStep::Build ? Line.StepStartedAtMs : -1;
            RestFrozenT = Line.FieldBaseMs;
            RestDeathsBefore = OwnDeaths(PC);
            bRestSpacesSeen = false;
            UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_GROWTH_PROBE_REST_HOLD role=%s %s life=%u field_life=%u deaths=%lld"),
                *Role, *Where, PC.OwnerState.Life, RestLifeBefore, RestDeathsBefore);
        }
        bHeld = true;
        const ALDGameMode* Mode = IsHost() ? PC.GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
        // The rest life: the Host reads its authority rest space; the buyer, alive at every rest entry, gets the next life.
        if (bRest && PC.OwnerState.LifePhase == ELDLifePhase::Alive
            && (IsHost() ? Mode && Mode->IsInRestSpace(HostId) : PC.OwnerState.Life == RestLifeBefore + 1))
        {
            RestLife = PC.OwnerState.Life;
        }
        if (IsHost() && bRest)
        {
            bRestSpacesSeen |= Mode && Mode->IsInRestSpace(HostId) && Mode->IsInRestSpace(ClientId)
                && Mode->GetFieldNowMs() == RestFrozenT;
        }
        // Without sign-ups a mid rest is its build step and the return countdown.
        if (Now - RestHeldAt > double(Parameters.RestBuildMs + Parameters.RestReturnMs) / 1000.
            + LDDemoGrowthScenario::GrowthProbeRequestSeconds)
        {
            return Fail(TEXT("Rest"), FString::Printf(TEXT("Rest R%u did not return this owner to a field life within its approved "
                "length (phase %s, life %u)"), uint32(RestHeldIndex), *StaticEnum<ELDMatchPhase>()->GetNameStringByValue(int64(Match.Phase)),
                PC.OwnerState.Life));
        }
        if (bRest) { return true; }
        if (Match.Generation != PC.OwnerState.Match
            || (Match.Phase != ELDMatchPhase::Development && Match.Phase != ELDMatchPhase::CaptureOpen))
        {
            // The buyer waits for the Lobby while the Host ends the first match; any other stage needs the field back.
            if (Stage != EStage::AwaitLobby)
            {
                return Fail(TEXT("Rest"), FString::Printf(TEXT("The first match left rest R%u without returning to the field"),
                    uint32(RestHeldIndex)));
            }
            ResumeClocks(Now, Now - RestHeldAt);
            RestHeldAt = 0;
            return true;
        }
        // The returned field life: at least two lives after the field life at the rest entry (its rest life between them).
        const bool bReturned = Cast<ALDCharacter>(PC.GetPawn()) && PC.OwnerState.LifePhase == ELDLifePhase::Alive
            && PC.OwnerState.Life >= RestLifeBefore + 2 && (!IsHost() || (Mode && !Mode->IsInRestSpace(HostId)))
            && PC.OwnerState.BodyRef.IsValid() && PC.CanWorldCommand();
        if (!bReturned) { return true; }
        const int64 DeathsAfter = OwnDeaths(PC);
        const FString Detail = FString::Printf(TEXT("rest R%u: field life %u -> rest life %u -> field life %u, public deaths %lld -> %lld"),
            uint32(RestHeldIndex), RestLifeBefore, RestLife, PC.OwnerState.Life, RestDeathsBefore, DeathsAfter);
        Check(RestLifeBefore > 0 && RestLife > RestLifeBefore && PC.OwnerState.Life == RestLife + 1 && RestDeathsBefore >= 0
            && DeathsAfter == RestDeathsBefore, TEXT("Rest"),
            TEXT("A mid rest retires the field life without a death: the public death count is unchanged, a rest life and then "
                "a new field life follow"), Detail);
        const int64 SegmentT = int64(RestHeldIndex) * Parameters.FieldSegmentMs;
        Check(Line.RestIndex == RestHeldIndex && Line.FieldSegment == RestHeldIndex && RestFrozenT == SegmentT
            && Line.FieldBaseMs == SegmentT && RestEnteredAtMs >= 0
            && Line.FieldStartedAtMs == RestEnteredAtMs + Parameters.RestBuildMs + Parameters.RestReturnMs, TEXT("Rest"),
            TEXT("Field T stays frozen through the rest: without sign-ups it skips its PK, and the next segment resumes at the T "
                "where the last one ended"),
            FString::Printf(TEXT("rest R%u entered at %lld with T=%lld; field segment %u started at %lld from T=%lld"),
                uint32(RestHeldIndex), RestEnteredAtMs, RestFrozenT, uint32(Line.FieldSegment), Line.FieldStartedAtMs, Line.FieldBaseMs));
        if (IsHost())
        {
            Check(bRestSpacesSeen, TEXT("Rest"),
                TEXT("The authority kept both owners in their private rest spaces with field T frozen during the rest"), Detail);
        }
        const double Held = Now - RestHeldAt;
        const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("rest"), RestHeldIndex);
        Row->SetNumberField(TEXT("stage"), int32(Stage));
        Row->SetNumberField(TEXT("field_life_before"), RestLifeBefore);
        Row->SetNumberField(TEXT("rest_life"), RestLife);
        Row->SetNumberField(TEXT("field_life_after"), PC.OwnerState.Life);
        Row->SetNumberField(TEXT("deaths_before"), double(RestDeathsBefore));
        Row->SetNumberField(TEXT("deaths_after"), double(DeathsAfter));
        Row->SetNumberField(TEXT("frozen_t_ms"), double(RestFrozenT));
        Row->SetNumberField(TEXT("rest_entered_at_ms"), double(RestEnteredAtMs));
        Row->SetNumberField(TEXT("field_started_at_ms"), double(Line.FieldStartedAtMs));
        Row->SetNumberField(TEXT("held_seconds"), Held);
        RestRows.Add(MakeShared<FJsonValueObject>(Row));
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_GROWTH_PROBE_REST_RESUME role=%s %s held=%.1fs"), *Role, *Detail, Held);
        if (!Passed) { return Finish(); }
        if (!IsHost()) { FieldLife = PC.OwnerState.Life; }
        ResumeClocks(Now, Held);
        RestHeldAt = 0;
        // A death the buyer waits for must be seen in its own segment; one hidden by the rest entry fails here, not later.
        if (!IsHost() && Stage == EStage::AwaitDeath && DeathsAfter > 0)
        {
            return Fail(TEXT("A14"), TEXT("The buyer's death by the Host was not seen inside its field segment: ") + Detail);
        }
        return true;
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
        if (IsHost() && Exists(TEXT("Client1.json")) && !Exists(TEXT("Client1DevDone.json")))
        {
            return Fail(TEXT("Net"), TEXT("The buyer ended its scenario early; see Client1.json for its failed check"));
        }
        bool bHeld = false;
        const bool bTicking = TickRestHold(Now, *PC, *Public, bHeld);
        if (!bTicking || bHeld) { return bTicking; }
        // The second match's checks finish inside its field 1; a mid rest there would change what its DEV commands judge.
        const FLDPublicMatchView& Match = Public->PublicMatch;
        if (bSecondMatch && Stage >= EStage::SecondSpawned && Match.Generation == SecondGeneration
            && Match.Phase == ELDMatchPhase::Rest && Match.Timeline.RestIndex > 0)
        {
            return Fail(TEXT("Iso"), FString::Printf(TEXT("The second match reached rest R%u before its checks finished (stage %d)"),
                uint32(Match.Timeline.RestIndex), int32(Stage)));
        }
        switch (Stage)
        {
        case EStage::Join: return TickJoin(*Current, *PC, Lobby, Driver);
        case EStage::Lobby: return TickLobby(Now, *PC, Lobby);
        case EStage::Rest0: return TickRest0(Now, *PC, *Public, *Registry);
        case EStage::Spawned: return TickSpawned(*PC, *Registry, Lobby);
        case EStage::Victim: return TickVictim(Now, *PC);
        case EStage::EndFirst: return TickEndFirst(Now, *PC, *Public);
        case EStage::AwaitLobby: return TickAwaitLobby(Now, *PC, *Public);
        case EStage::SecondLobby: return TickLobby(Now, *PC, Lobby);
        case EStage::SecondSpawned: return TickSecondSpawned(Now, *PC, Lobby);
        case EStage::SecondAudit: return TickSecondAudit(*PC);
        case EStage::HostDev: return TickHostDev(Now, *PC);
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
        // #198: the first match's opening rest R0 is checked from both roles before anyone acts in the field.
        if (!bSecond && Lobby.Phase == ELDMatchPhase::Rest)
        {
            SetStage(EStage::Rest0);
            return true;
        }
        if (Lobby.Phase == ELDMatchPhase::Starting || Lobby.Phase == ELDMatchPhase::Rest) { return true; }
        if (Lobby.Phase != ELDMatchPhase::Lobby)
        {
            return Fail(Id, TEXT("The room left the Lobby without opening play: ")
                + StaticEnum<ELDMatchPhase>()->GetNameStringByValue(int64(Lobby.Phase)));
        }
        const FString Configured = bSecond ? TEXT("LobbyConfigured2.json") : TEXT("LobbyConfigured.json");
        if (IsHost() && !bLobbyConfigured)
        {
            if (bSecond && !Exists(TEXT("BuyerSawSecondLobby.json")))
            {
                if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeTransitionSeconds) { return true; }
                return Fail(Id, TEXT("The buyer never confirmed the new, unready Lobby generation"));
            }
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
    // (Progression opens the growth records) and the initial spawn.
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
            Check(ProgressionHooks && ProgressionHooks->Prepare && ProgressionHooks->ReadFinalLevel,
                Id, TEXT("Progression is a registered normal-start provider"));
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
        // The opening offer is drawn when the match begins, so the buyer's presets are staged in the Lobby before Start.
        if (!bSecondMatch && !bPresetsStaged)
        {
            ClientId.Invalidate();
            for (const FLDLobbyMember& LobbyMember : Lobby.Members)
            {
                if (LobbyMember.Kind == ELDContenderKind::Human && !LobbyMember.Host) { ClientId = LobbyMember.ContenderId; }
            }
            if (!PresetBuyerOffers(PC)) { return Finish(); }
            StagedClientId = ClientId;
            bPresetsStaged = true;
        }
        if (Now >= NextCommandAt)
        {
            PC.StartDemoMatch();
            NextCommandAt = Now + 2.0;
        }
        return true;
    }

    // Host, first spawned frame: the normal start's Progression Prepare opened the growth records for the locked roster.
    bool CheckStartGrowth(const ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        bGrowthChecked = true;
        const ALDGameMode* Mode = PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
        bool bRecords = Mode && Mode->Growth && Mode->Development && Mode->Growth->GetMatch() == Mode->GetMatchContext()
            && Lobby.Members.Num() == 2;
        for (const FLDLobbyMember& LobbyMember : Lobby.Members)
        {
            const FLDProgressionState* Record = bRecords ? Mode->Development->FindProgression(LobbyMember.ContenderId) : nullptr;
            bRecords &= Record && Record->CumulativeXP == Catalog->Get().Progression.XPStart;
        }
        return Check(bRecords, TEXT("Start"),
            TEXT("The Progression provider opened one growth record per locked contender at the approved XP start"));
    }

    // Host, in the Lobby before Start: the DEV fixture stages the buyer's three offers, its opening offer first. The growth
    // domain does not exist yet, so the fixture holds them until BeginMatch draws the opening offer.
    bool PresetBuyerOffers(const ALDPlayerController& PC)
    {
        const UWorld* LobbyWorld = PC.GetWorld();
        const ALDGameMode* Mode = LobbyWorld->GetAuthGameMode<ALDGameMode>();
        if (!Check(LDDevGrowthFixture::IsSwitchPresent() && Mode && !Mode->Development && ClientId.IsValid(), TEXT("Fixture"),
            TEXT("The Listen Host runs with -LDDevGrowthFixture and sees the buyer in its Lobby, before any growth domain exists")))
        {
            return false;
        }
        FString Refusal;
        const TArray<FName> Twice = {TEXT("active_snark_bolt"), TEXT("active_pants_down"), TEXT("active_snark_bolt")};
        Check(!LDDevGrowthFixture::StageOffer(LobbyWorld, ClientId, Twice, Refusal) && !Refusal.IsEmpty(), TEXT("Fixture"),
            TEXT("The fixture refuses a preset that repeats an active, with a reason"), Refusal);
        for (int32 Index = 0; Index < LDDemoGrowthScenario::GrowthProbePlannedOffers; ++Index)
        {
            FString QueueError;
            const bool bQueued = LDDevGrowthFixture::StageOffer(LobbyWorld, ClientId,
                LDDemoGrowthScenario::GrowthProbePlannedOptions(Index), QueueError);
            if (!Check(bQueued, TEXT("Fixture"), FString::Printf(TEXT("The fixture stages the buyer's offer %d"), Index + 1), QueueError))
            {
                return false;
            }
        }
        return Mark(TEXT("OffersStaged.json"));
    }

    // D10: the owner's first view of a match holds exactly one queued active opportunity with its kept opening offer of three
    // distinct opening-pool actives, no active or blueprint and exactly the approved opening points unspent; the buyer's
    // first-match opening offer is the first offer the Host staged in the Lobby.
    bool CheckOpening(const FLDProgressionView& View, const TCHAR* Id, TSharedPtr<FJsonObject>& Row, bool bStagedPreset)
    {
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        const TArray<FName>& Options = View.ActiveOffer.Options;
        const bool bDistinct = Options.Num() == 3 && Options[0] != Options[1] && Options[0] != Options[2] && Options[1] != Options[2];
        Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("level"), View.Level);
        Row->SetNumberField(TEXT("earned"), View.ActiveOpportunitiesEarned);
        Row->SetNumberField(TEXT("consumed"), View.ActiveOpportunitiesConsumed);
        Row->SetNumberField(TEXT("queued"), View.QueuedActiveOpportunities);
        Row->SetNumberField(TEXT("ordinal"), View.bHasActiveOffer ? View.ActiveOffer.OpportunityOrdinal : 0);
        Row->SetNumberField(TEXT("unspent_points"), double(View.UnspentPoints));
        Row->SetStringField(TEXT("offer"), LDDemoGrowthScenario::GrowthProbeGuid(View.ActiveOffer.OfferId));
        Row->SetStringField(TEXT("options"), FString::JoinBy(Options, TEXT(","), [](FName Option) { return Option.ToString(); }));
        Row->SetBoolField(TEXT("staged_preset"), LDDemoGrowthScenario::GrowthProbeIsPreset(View));
        bool bGood = Check(View.Level == 1 && View.ActiveOpportunitiesEarned == 1 && View.ActiveOpportunitiesConsumed == 0
            && View.QueuedActiveOpportunities == 1 && View.bHasActiveOffer && View.ActiveOffer.OpportunityOrdinal == 1
            && View.ActiveOffer.OfferId.IsValid() && View.ActiveOffer.OfferRevision != 0 && bDistinct, Id,
            TEXT("The match opens with exactly one queued active opportunity: its kept opening offer of three distinct actives"),
            LDDemoGrowthScenario::GrowthProbeProgressionText(View));
        const bool bNoPoints = View.StatPoints.Num() == Defs.Stats.Num()
            && !View.StatPoints.ContainsByPredicate([](const FLDStatPointsView& Points) { return Points.Points != 0; });
        bGood &= Check(View.Actives.IsEmpty() && View.PendingBlueprints.IsEmpty() && bNoPoints
            && View.UnspentPoints == Defs.Parameters.OpeningPoints, Id,
            TEXT("The owner starts without an active or blueprint, one zero row per stat and exactly the approved opening points unspent"),
            LDDemoGrowthScenario::GrowthProbeProgressionText(View));
        for (const FName Option : Options)
        {
            const FLDDemoAbilityDefinition* Ability = nullptr;
            FString FindError;
            bGood &= Check(Catalog->FindAbility(Option.ToString(), Ability, FindError) && Ability->Opening, Id,
                TEXT("Every opening option is an approved active of the opening pool"), Option.ToString() + TEXT(" ") + FindError);
        }
        if (bStagedPreset)
        {
            bGood &= Check(LDDemoGrowthScenario::GrowthProbeIsPreset(View), TEXT("Fixture"),
                TEXT("The buyer's opening offer is exactly the first offer the Host staged in the Lobby"),
                Row->GetStringField(TEXT("options")));
        }
        return bGood;
    }

    bool TickSpawned(ALDPlayerController& PC, const ULDMapRegistry& Registry, const FLDLobbyView& Lobby)
    {
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        // The public phase may reach field 1 a frame before this owner's projection leaves its R0 rest life.
        if (!Cast<ALDCharacter>(PC.GetPawn()) || PC.OwnerState.LifePhase != ELDLifePhase::Alive || !PC.OwnerState.BodyRef.IsValid()
            || !PC.CanWorldCommand() || !ProgView || PC.OwnerState.Life < LDDemoGrowthScenario::GrowthProbeFieldLife)
        {
            return true;
        }
        if (!IsHost() && !bOpeningQueried)
        {
            return TickOpeningQuery(PC);
        }
        if (!ResolveRoster(PC, Lobby) || !PrepareSites(Registry)) { return Finish(); }
        if (IsHost() && !bGrowthChecked && !CheckStartGrowth(PC, Lobby)) { return Finish(); }
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>();
        const int64 ReturnProtectedUntilMs = Public ? Public->PublicMatch.Timeline.FieldStartedAtMs + Defs.Parameters.ProtectionMs : -1;
        Check(PC.OwnerState.Life == LDDemoGrowthScenario::GrowthProbeFieldLife
            && (PC.OwnerState.ProtectedUntilMs == 0 || PC.OwnerState.ProtectedUntilMs == ReturnProtectedUntilMs), TEXT("Start"),
            TEXT("The first field body is the life after the R0 rest life, with at most the shared R0 return protection"),
            FString::Printf(TEXT("life=%u protected_until_ms=%lld return_protection_until_ms=%lld"), PC.OwnerState.Life,
                PC.OwnerState.ProtectedUntilMs, ReturnProtectedUntilMs));
        Check(ProgView->Level == 1 && ProgView->CumulativeXP == Defs.Progression.XPStart && ProgView->CatalogDigest == Defs.Identity.Root,
            TEXT("Start"), TEXT("Growth starts at level 1 with the approved XP start and the current catalog"),
            LDDemoGrowthScenario::GrowthProbeProgressionText(*ProgView));
        Check(OpeningRow.IsValid(), TEXT("Opening"), TEXT("The opening opportunity was checked in R0, before field 1 (T=0)"));
        if (!Passed) { return Finish(); }
        FieldLife = PC.OwnerState.Life;
        if (IsHost())
        {
            if (!Check(bPresetsStaged && StagedClientId == ClientId, TEXT("Fixture"),
                TEXT("The Lobby presets were staged for exactly this match's buyer")) || !Mark(TEXT("OffersPreset.json")))
            {
                return Finish();
            }
            SetStage(EStage::Victim);
            return true;
        }
        if (!PlanKills()) { return Finish(); }
        if (!NavigateDuelSite(PC, KillSite)) { return Finish(); }
        SetStage(EStage::ToSite);
        return true;
    }

    // Client1 after T=0: a query by Generation, Family and RequestId returns the opening choice's same immutable final receipt.
    bool TickOpeningQuery(ALDPlayerController& PC)
    {
        if (!bOpeningQuerySent)
        {
            FLDReceiptQuery Query;
            Query.Generation = OpeningSent.Generation;
            Query.Family = ELDGrowthFamily::Build;
            Query.RequestId = OpeningSent.RequestId;
            OpeningQueryMark = BuildResults.Num();
            PC.QueryGrowthReceipt(Query);
            bOpeningQuerySent = true;
            StageAt = FPlatformTime::Seconds();
            return true;
        }
        const FLDBuildReceiptQueryResult* Answer = FindBuildAnswer(OpeningSent.Generation, OpeningSent.RequestId, OpeningQueryMark);
        if (!Answer)
        {
            if (FPlatformTime::Seconds() - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("A24"), TEXT("The receipt query was not answered"));
        }
        if (!Check(Answer->Lookup == ELDReceiptLookup::Final && Answer->bHasReceipt && !Answer->bHasPending
            && LDDemoGrowthScenario::GrowthProbeSameBuild(Answer->Receipt, OpeningFinal), TEXT("A24"),
            TEXT("A query by Generation, Family and RequestId returns the opening choice's same immutable final receipt")))
        {
            return Finish();
        }
        bOpeningQueried = true;
        return true;
    }

    // Client1 in the R0 build step, beside its admitted move: the opening choice learned into Q, then every opening point
    // allocated. The rest's build step needs no build safe window. Returns false after a failed check.
    bool TickRest0Build(double Now, ALDPlayerController& PC, const FLDPublicMatchView& Match)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        int64 NowMs = 0;
        const bool bClock = AuthorityNowMs(PC, NowMs);
        const FLDTimelineView& Line = Match.Timeline;
        const bool bBuildStep = Match.Phase == ELDMatchPhase::Rest && Line.Stage == ELDMatchStage::Rest && Line.RestIndex == 0
            && Line.Step == ELDRestStep::Build;
        if (bRest0Built || !View) { return true; }
        if (BuildStep == 0)
        {
            const FName Option = LDDemoGrowthScenario::GrowthProbePlannedOptions(0)[0];
            BuildStep = 1;
            return SendBuild(PC, TEXT("opening-choice"), ChoiceCommand(*View, Option));
        }
        const FLDBuildReceipt* Final = nullptr;
        FString Why;
        const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitBuild(Now, PC, Final, Why);
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Failed)
        {
            return Check(false, TEXT("A10"), Build.Step + TEXT(" did not complete: ") + Why);
        }
        if (BuildStep == 1)
        {
            const FName Option = LDDemoGrowthScenario::GrowthProbePlannedOptions(0)[0];
            const FLDCombatEntryView* Q = CurrentEntry(PC, ELDCombatEntry::Q);
            if ((!Q || Q->DefinitionId != Option) && Now - Build.SentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            const FString Detail = LDDemoGrowthScenario::GrowthProbeProgressionText(*View);
            Check(Final->Header.Outcome == ELDReceiptOutcome::Applied && Final->Kind == ELDBuildCommandKind::ActiveChoice
                && Final->DefinitionId == Option && Final->OfferId == Build.Before.ActiveOffer.OfferId && Final->OpportunityOrdinal == 1
                && Final->ChoiceResult == ELDBuildChoiceResult::Learned && Final->LearnedSlot == 0 && Final->StoredReason == 0
                && !Final->bHasNextOffer, TEXT("A10"),
                TEXT("The opening choice is accepted pending, then applies once: the chosen active is learned into Q"), Detail);
            Check(View->Actives.Num() == 1 && LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 0)
                && LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 0)->DefinitionId == Option && View->PendingBlueprints.IsEmpty()
                && View->ActiveOpportunitiesConsumed == 1 && View->QueuedActiveOpportunities == 0 && !View->bHasActiveOffer, TEXT("A10"),
                TEXT("The owner view shows the active in Q, the opening opportunity consumed and no further offer"), Detail);
            Check(Q && Q->DefinitionId == Option && Q->IssuedCooldownId.IsValid() && Q->ReadyAtMs <= PC.CombatView.AsOfMs,
                TEXT("Opening"), TEXT("Combat installs the chosen opening active in Q on its own ready cooldown track"),
                Q ? FString::Printf(TEXT("%s ready at %lld as of %lld"), *Q->DefinitionId.ToString(), Q->ReadyAtMs, PC.CombatView.AsOfMs)
                    : FString(TEXT("no Q entry")));
            if (!Passed) { return false; }
            OpeningSent = Build.Sent;
            OpeningFinal = *Final;
            if (!bClock || !bBuildStep || NowMs >= Line.StepEndsAtMs)
            {
                return Check(false, TEXT("Rest0"), TEXT("The R0 build step ended before the opening points could be allocated in it"));
            }
            BuildStep = 2;
            return SendBuild(PC, TEXT("rest0-points"), PointsCommand(View->UnspentPoints));
        }
        if (!AllocationShown(PC, *Final) && Now - Build.SentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
        Check(AllocationShown(PC, *Final) && Build.Before.UnspentPoints == Catalog->Get().Parameters.OpeningPoints, TEXT("A16"),
            TEXT("Every opening point is allocated in the R0 build step in one commit: points, unspent count and maximum HP move together"),
            FString::Printf(TEXT("%s max hp %lld -> %lld"), *LDDemoGrowthScenario::GrowthProbeProgressionText(*View), Build.MaxHPBeforeP2,
                PC.CombatView.MaxHPP2));
        Check(bBuildStep && bClock && NowMs < Line.StepEndsAtMs, TEXT("Rest0"),
            TEXT("The opening choice and the allocation both commit in the R0 build step, before field 1 (T=0)"),
            FString::Printf(TEXT("now=%lld build_ends=%lld"), NowMs, Line.StepEndsAtMs));
        if (!Passed) { return false; }
        Rest0Row->SetStringField(TEXT("opening_active"), OpeningFinal.DefinitionId.ToString());
        Rest0Row->SetNumberField(TEXT("opening_slot"), OpeningFinal.LearnedSlot);
        Rest0Row->SetNumberField(TEXT("points_allocated"), double(Build.Before.UnspentPoints));
        Rest0Row->SetNumberField(TEXT("max_hp_p2_before"), double(Build.MaxHPBeforeP2));
        Rest0Row->SetNumberField(TEXT("max_hp_p2_after"), double(PC.CombatView.MaxHPP2));
        bRest0Built = true;
        Build = FBuildCall();
        BuildStep = 0;
        return true;
    }

    // #198: the first match's opening rest R0, seen from both roles, while the opening offer is open. Each owner stands in its
    // private rest space. In the build step Client1, a real owning remote connection, has its ground move admitted and walked
    // and chooses its opening active and allocates its opening points; in the return countdown that follows (no sign-up, so
    // no PK) its attack-move, Shift attack-move and Q are refused by the server. Field 1 then starts at T=0 with every field
    // timer counted from it; the Host's untaken opening offer is kept.
    bool TickRest0(double Now, ALDPlayerController& PC, const ALDGameState& Public, const ULDMapRegistry& Registry)
    {
        const FLDPublicMatchView& View = Public.PublicMatch;
        const FLDTimelineView& Line = View.Timeline;
        const FLDDemoContentParameters& Parameters = Catalog->Get().Parameters;
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        const ALDGameMode* Mode = IsHost() ? PC.GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
        int64 NowMs = 0;
        const bool bClock = AuthorityNowMs(PC, NowMs);
        const bool bInRest = View.Phase == ELDMatchPhase::Rest && Line.Stage == ELDMatchStage::Rest && Line.RestIndex == 0;
        if (Now - StageAt > double(Parameters.RestOpeningBuildMs + Parameters.RestReturnMs) / 1000.
            + LDDemoGrowthScenario::GrowthProbeRequestSeconds)
        {
            return Fail(TEXT("Rest0"), TEXT("The opening rest R0 did not end with its checks complete"));
        }
        if (!bRest0Seen)
        {
            if (View.Generation != PC.OwnerState.Match || !Body || PC.OwnerState.LifePhase != ELDLifePhase::Alive || !ProgView || !bClock)
            {
                return true;
            }
            if (!bInRest || Line.Step != ELDRestStep::Build)
            {
                return Fail(TEXT("Rest0"), TEXT("The match left the R0 build step before this role saw its rest body"));
            }
            bRest0Seen = true;
            Rest0StartedAtMs = View.StartedAtMs;
            Rest0BuildEndsAtMs = Line.StepEndsAtMs;
            Rest0Row = MakeShared<FJsonObject>();
            Check(Line.StepStartedAtMs == View.StartedAtMs && Line.StepEndsAtMs == View.StartedAtMs + Parameters.RestOpeningBuildMs
                && NowMs < Line.StepEndsAtMs && Line.FieldSegment == 0 && Line.FieldBaseMs == 0 && Line.FieldEndT == Parameters.FieldSegmentMs
                && View.OpenAtMs == MAX_int64 && View.CircleState == ELDCircleState::NotOpen, TEXT("Rest0"),
                TEXT("The match opens in the approved R0 build step before field 1 (T=0); the flagpole is not scheduled before the final field"),
                FString::Printf(TEXT("now=%lld started=%lld build_ends=%lld field_end_t=%lld"), NowMs, View.StartedAtMs, Line.StepEndsAtMs,
                    Line.FieldEndT));
            Check(PC.OwnerState.Life == 1 && PC.OwnerState.ProtectedUntilMs == 0, TEXT("Rest0"),
                TEXT("The opening rest body is the match's first, unprotected life"),
                FString::Printf(TEXT("life=%u protected_until_ms=%lld"), PC.OwnerState.Life, PC.OwnerState.ProtectedUntilMs));
            CheckOpening(*ProgView, TEXT("Opening"), OpeningRow, !IsHost());
            Check(PC.CanWorldCommand(), TEXT("Rest0"), TEXT("Camera and commands stay usable; the server answers each world command"));
            if (IsHost())
            {
                bool bInRestSpaces = Mode != nullptr;
                for (const FLDLobbyMember& LobbyMember : Public.DemoLobby.Members)
                {
                    bInRestSpaces &= LobbyMember.Kind != ELDContenderKind::Human || (Mode && Mode->IsInRestSpace(LobbyMember.ContenderId));
                }
                Check(bInRestSpaces && Mode->IsRestPhase(Mode->GetMatchContext()) && !Mode->IsInputLocked(Mode->GetMatchContext())
                    && Mode->GetTimeline().GetRestIndex() == 0 && Mode->ReadPublicMatch().StartedAtMs == View.StartedAtMs
                    && Mode->GetFieldNowMs() == 0 && Mode->GetAirdrop().Schedule.StartedAtMs == 0
                    && Mode->GetLeaderNotice().Rules.StartedAtMs == 0
                    && Mode->Camps && Mode->Camps->GetBoss().Rules.StartedAtMs == 0,
                    TEXT("Rest0"), TEXT("The authority keeps every owner in its private rest space at T=0 and counts the airdrop, the leader notice and the Boss in field progress from T=0"));
            }
            if (!Passed) { return Finish(); }
            if (IsHost())
            {
                bRest0Refused = true;
                return true;
            }
            Rest0Step = 1;
            BuildStep = 0;
            return TickRest0Build(Now, PC, View) ? true : Finish();
        }
        if (!IsHost() && !TickRest0Build(Now, PC, View)) { return Finish(); }
        if (bInRest && Line.Step == ELDRestStep::Return && !bRest0ReturnSeen)
        {
            bRest0ReturnSeen = true;
            Check(Line.bPKSkipped && Line.StepStartedAtMs == Rest0BuildEndsAtMs
                && Line.StepEndsAtMs == Rest0BuildEndsAtMs + Parameters.RestReturnMs, TEXT("Rest0"),
                TEXT("Without sign-ups R0 skips its PK: the return countdown follows the build step at once for its approved length"),
                FString::Printf(TEXT("return=%lld..%lld build_ends=%lld"), Line.StepStartedAtMs, Line.StepEndsAtMs, Rest0BuildEndsAtMs));
            if (IsHost())
            {
                Check(Mode && Mode->IsInputLocked(Mode->GetMatchContext()), TEXT("Rest0"),
                    TEXT("The authority locks every input during the R0 return countdown"));
            }
            if (!Passed) { return Finish(); }
        }
        if (!bRest0Refused)
        {
            const FLDOrderReceipt& Order = PC.Navigation->LatestReceipt();
            const FLDMapBinding* Capture = Registry.Resolve(FName(*Catalog->Get().World.CaptureId), Registry.Generation);
            if (!Capture)
            {
                return Fail(TEXT("Rest0"), TEXT("The R0 requests could not be built from the public world"));
            }
            const auto Refused = [&Order](const FLDMoveIdentity& Sent)
            { return Order.Identity == Sent && Order.Status != ELDOrderStatus::Queued; };
            if (Rest0Step == 1)
            {
                if (!bInRest || Line.Step != ELDRestStep::Build || !Body)
                {
                    return Fail(TEXT("Rest0"), TEXT("The R0 build step ended before the ground move could be sent"));
                }
                // A freshly placed body may still be settling; a ground order sent before it stands fails its local plan.
                if (Body->GetCharacterMovement() && !Body->GetCharacterMovement()->IsMovingOnGround()) { return true; }
                Rest0Foot = Body->GetActorLocation();
                PC.Navigation->NavigateGround(Capture->Position, Capture->Surface);
                Rest0Move = PC.Navigation->LocalIdentity();
                Rest0Step = 2;
                return true;
            }
            if (Rest0Step == 2)
            {
                if (Order.Identity == Rest0Move && Order.Status == ELDOrderStatus::Rejected)
                {
                    return Fail(TEXT("Rest0"), TEXT("The server refused a ground move in the R0 build step: failure ")
                        + LexToString(uint32(Order.Failure)));
                }
                const double Walked = Body ? FVector::Dist2D(Rest0Foot, Body->GetActorLocation()) : 0.;
                if (Walked < 100.)
                {
                    if (bInRest && Line.Step == ELDRestStep::Build) { return true; }
                    return Fail(TEXT("Rest0"), FString::Printf(TEXT("The R0 build step ended before the admitted move walked (%.0f cm)"), Walked));
                }
                Check(bInRest && Line.Step == ELDRestStep::Build && bClock && NowMs < Rest0BuildEndsAtMs, TEXT("Rest0"),
                    TEXT("A ground move is admitted and walks in the R0 build step"),
                    FString::Printf(TEXT("walked=%.0f cm now=%lld"), Walked, NowMs));
                PC.Navigation->Stop();
                Rest0Step = 3;
                return true;
            }
            if (Rest0Step == 3)
            {
                if (bInRest && Line.Step == ELDRestStep::Build) { return true; }
                if (!bInRest || Line.Step != ELDRestStep::Return)
                {
                    return Fail(TEXT("Rest0"), TEXT("This owner never saw the R0 return countdown"));
                }
                if (!bRest0Built)
                {
                    return Fail(TEXT("Rest0"), TEXT("The opening choice and allocation did not complete in the R0 build step"));
                }
                if (!bClock || Line.StepEndsAtMs - NowMs < LDDemoGrowthScenario::GrowthProbeReturnMarginMs)
                {
                    return Fail(TEXT("Rest0"), TEXT("Field 1 was too close to test the refusals inside the R0 return countdown"));
                }
                // Q first, so its refusal cannot depend on the attack-moves that follow it on the same connection.
                Rest0CombatMark = PC.CombatReceipts.Serial();
                PC.ActivateCombatEntry(ELDCombatEntry::Q);
                // An aimed opening active (snark_bolt aims a direction) only arms on Q; the probe confirms it toward the
                // capture point like the player's left click, which sends it as a cast order the server must refuse.
                ELDCombatEntry Armed = ELDCombatEntry::None;
                if (PC.ReadArmedCombatEntry(Armed))
                {
                    const FVector Toward = Body ? (Capture->Position - Body->GetActorLocation()).GetSafeNormal2D() : FVector::ZeroVector;
                    if (Armed != ELDCombatEntry::Q || Toward.IsNearlyZero())
                    {
                        return Fail(TEXT("Rest0"), TEXT("Q did not arm the opening active with a usable aim in the R0 return countdown"));
                    }
                    FLDCombatTarget Aim;
                    Aim.Kind = ELDCombatTargetKind::Direction;
                    Aim.Direction = Toward;
                    const FLDMoveIdentity Before = PC.Navigation->LocalIdentity();
                    PC.ConfirmArmedEntry(Aim, &Capture->Position);
                    Rest0QOrder = PC.Navigation->LocalIdentity();
                    if (Rest0QOrder == Before)
                    {
                        return Fail(TEXT("Rest0"), TEXT("The confirmed Q aim sent no cast order in the R0 return countdown"));
                    }
                    bRest0QOrder = true;
                    Rest0Step = 7;
                    return true;
                }
                PC.Navigation->AttackMove(Capture->Position, Capture->Surface);
                Rest0Attack = PC.Navigation->LocalIdentity();
                Rest0Step = 4;
                return true;
            }
            if (Rest0Step == 7)
            {
                if (!Refused(Rest0QOrder)) { return true; }
                Check(Order.Status == ELDOrderStatus::Rejected && Order.Failure == ELDOrderFailure::NotStarted, TEXT("Rest0"),
                    TEXT("The server refuses Q's aimed cast order in the R0 return countdown"),
                    FString::Printf(TEXT("status=%u failure=%u"), uint32(Order.Status), uint32(Order.Failure)));
                PC.Navigation->AttackMove(Capture->Position, Capture->Surface);
                Rest0Attack = PC.Navigation->LocalIdentity();
                Rest0Step = 4;
                return true;
            }
            if (Rest0Step == 4)
            {
                if (!Refused(Rest0Attack)) { return true; }
                Check(Order.Status == ELDOrderStatus::Rejected && Order.Failure == ELDOrderFailure::NotStarted, TEXT("Rest0"),
                    TEXT("The server refuses an attack-move in the R0 return countdown"),
                    FString::Printf(TEXT("status=%u failure=%u"), uint32(Order.Status), uint32(Order.Failure)));
                PC.Navigation->AttackMove(Capture->Position, Capture->Surface, true);
                Rest0Shift = PC.Navigation->LocalIdentity();
                Rest0Step = 5;
                return true;
            }
            if (Rest0Step == 5)
            {
                if (!Refused(Rest0Shift)) { return true; }
                Check(Order.Status == ELDOrderStatus::Rejected && Order.Failure == ELDOrderFailure::NotStarted, TEXT("Rest0"),
                    TEXT("The server refuses a Shift-queued attack-move in the R0 return countdown"),
                    FString::Printf(TEXT("status=%u failure=%u"), uint32(Order.Status), uint32(Order.Failure)));
                Rest0Step = 6;
                return true;
            }
            // A direct Q (a self-cast opening active) answers with a combat receipt; an aimed one was refused as an order.
            if (!bRest0QOrder)
            {
                const FLDCombatActionReceipt* Action = PC.CombatReceipts.FindLatestSince(Rest0CombatMark,
                    [](const FLDCombatActionReceipt& Receipt) { return Receipt.Entry == ELDCombatEntry::Q; });
                if (!Action || Action->Phase != ELDCombatPhase::Rejected) { return true; }
                Check(Action->Reject == ELDCombatReject::NotStarted, TEXT("Rest0"), TEXT("The server refuses Q in the R0 return countdown"),
                    StaticEnum<ELDCombatReject>()->GetNameStringByValue(int64(Action->Reject)));
            }
            if (!Passed) { return Finish(); }
            bRest0Refused = true;
            return true;
        }
        if (View.Phase == ELDMatchPhase::Rest) { return true; }
        // The rest body may already be gone while the first field body is not yet bound; neither is needed here.
        if (View.Phase != ELDMatchPhase::Development || !ProgView)
        {
            return Fail(TEXT("Rest0"), TEXT("The opening rest R0 did not end in field 1"));
        }
        Check(bRest0ReturnSeen, TEXT("Rest0"), TEXT("This role saw the R0 return countdown before field 1"));
        Check(View.StartedAtMs == Rest0StartedAtMs && Line.Stage == ELDMatchStage::Field && Line.FieldSegment == 0
            && Line.FieldBaseMs == 0 && Line.FieldEndT == Parameters.FieldSegmentMs
            && Line.FieldStartedAtMs == Rest0BuildEndsAtMs + Parameters.RestReturnMs && View.ServerNowMs >= Line.FieldStartedAtMs
            && View.OpenAtMs == MAX_int64, TEXT("Rest0"),
            TEXT("Field 1 starts at T=0 exactly when R0 returns, runs one approved segment, and the flagpole stays unscheduled"),
            FString::Printf(TEXT("server=%lld started=%lld field_started=%lld field_end_t=%lld"), View.ServerNowMs, View.StartedAtMs,
                Line.FieldStartedAtMs, Line.FieldEndT));
        if (IsHost())
        {
            Check(ProgView->bHasActiveOffer && ProgView->ActiveOpportunitiesConsumed == 0 && ProgView->QueuedActiveOpportunities == 1
                && ProgView->Actives.IsEmpty(), TEXT("Rest0"),
                TEXT("An opening offer not chosen in R0 is kept, never chosen for the player"),
                LDDemoGrowthScenario::GrowthProbeProgressionText(*ProgView));
        }
        else
        {
            Check(bRest0Built, TEXT("Rest0"), TEXT("The buyer's opening choice and allocation completed in R0, before field 1"),
                LDDemoGrowthScenario::GrowthProbeProgressionText(*ProgView));
        }
        Rest0Row->SetNumberField(TEXT("started_at_ms"), double(View.StartedAtMs));
        Rest0Row->SetNumberField(TEXT("field_started_at_ms"), double(Line.FieldStartedAtMs));
        Rest0Row->SetBoolField(TEXT("refusals_checked"), !IsHost());
        if (!Passed) { return Finish(); }
        SetStage(EStage::Spawned);
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

    // Both roles derive the same two duel sites from public anchors and approved radii, then prove them legal. The probe's
    // own search geometry: a ring of 16 directions around the capture anchor at 2, 2.5 and 3 vision radii, the Host one
    // basic attack range nearer the centre than the buyer. A site stays clear of the capture circle, every airdrop pickup
    // (so no event reward lands inside a kill's XP accounting), every dorm, and the camp and Boss walking exclusions.
    bool PrepareSites(const ULDMapRegistry& Registry)
    {
        if (bSitesReady) { return true; }
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        const FLDMapBinding* CaptureAnchor = Registry.Resolve(FName(*Defs.World.CaptureId), Registry.Generation);
        const double VisionReach = LDDemoGrowthScenario::GrowthProbeRational(Defs.Parameters.VisionRadiusCm);
        const double CaptureReach = LDDemoGrowthScenario::GrowthProbeRational(Defs.Parameters.CaptureRadiusCm);
        const double AirdropReach = LDDemoGrowthScenario::GrowthProbeRational(Defs.Parameters.AirdropPickupRadiusCm);
        const double DormReach = LDDemoGrowthScenario::GrowthProbeRational(Defs.Parameters.DormRadiusCm);
        const double BodyRadius = LDDemoGrowthScenario::GrowthProbeRational(Defs.Parameters.CollisionRadiusCm);
        const double AttackReach = LDDemoGrowthScenario::GrowthProbeRational(Defs.Parameters.AttackRangeCm.Base);
        // Deaths happen in any field segment; each one's approved respawn wait (LIFE.RespawnSec.*) bounds the waits for it.
        const bool bRespawnWaits = Defs.Parameters.RespawnField1Ms > 0 && Defs.Parameters.RespawnField2Ms > 0
            && Defs.Parameters.RespawnField3Ms > 0 && Defs.Parameters.RespawnFinalMs > 0;
        const auto* Geometry = Registry.GetGeometry();
        if (!CaptureAnchor || !Geometry || VisionReach <= 0 || CaptureReach <= 0 || AirdropReach <= 0 || DormReach <= 0
            || BodyRadius <= 0 || AttackReach <= 0 || !bRespawnWaits)
        {
            return Check(false, TEXT("Start"),
                TEXT("The public capture anchor, the world geometry, the approved radii and the field respawn waits exist"));
        }
        FString SiteError;
        // Every airdrop pickup and dorm the authored world places, with the reach a site keeps from it.
        TArray<LDDemoGrowthScenario::FGrowthProbeKeepout> Pickups;
        for (const auto& Anchor : Geometry->Anchors)
        {
            const bool bAirdrop = Anchor.Kind == ELDDemoWorldAnchorKind::Airdrop;
            if (!bAirdrop && Anchor.Kind != ELDDemoWorldAnchorKind::Dorm) { continue; }
            FVector Centre;
            if (!Anchor.PositionCm.ToVector(Centre, SiteError))
            { return Check(false, TEXT("Start"), TEXT("The authored airdrop and dorm positions are valid"), Anchor.Id + TEXT(": ") + SiteError); }
            Pickups.Add({Centre, bAirdrop ? AirdropReach + BodyRadius + LDDemoGrowthScenario::GrowthProbeArrivalCm : DormReach + BodyRadius});
        }
        const FVector Outward(1.0, 0.0, 0.0);
        const FVector Across(Outward.Y, -Outward.X, 0.0);
        FCollisionShape Capsule;
        const bool bCapsule = ULDMapRegistry::ContentCapsule(*Catalog, Capsule, SiteError);
        if (!PrepareWalkKeepouts(Registry, BodyRadius, SiteError))
        { return Check(false, TEXT("Start"), TEXT("The fixture's authored camp and Boss walking exclusions exist"), SiteError); }
        bool bLegal = false;
        for (const double DistanceFactor : {2., 2.5, 3.})
        {
            for (int32 Angle = 0; bCapsule && Angle < 16; ++Angle)
            {
                const double Radians = Angle * UE_TWO_PI / 16;
                const FVector Direction = Outward * FMath::Cos(Radians) + Across * FMath::Sin(Radians);
                KillSite = CaptureAnchor->Position + Direction * (DistanceFactor * VisionReach);
                HostSite = KillSite - Direction * AttackReach;
                FVector KillNormal, HostNormal;
                Registry.GetTerrain().Ground(FVector2D(KillSite), KillSite.Z, KillNormal);
                Registry.GetTerrain().Ground(FVector2D(HostSite), HostSite.Z, HostNormal);
                if (!FMath::IsNearlyEqual(KillSite.Z, HostSite.Z)
                    || !KillNormal.Equals(FVector::UpVector, 1e-4) || !HostNormal.Equals(FVector::UpVector, 1e-4)) { continue; }
                bool bPairLegal = true;
                for (FVector* Site : {&KillSite, &HostSite})
                {
                    FLDLegalGround LegalSite, OnMesh;
                    ELDWorldQueryFailure SiteFailure = ELDWorldQueryFailure::None;
                    if (!bPairLegal || !Registry.QueryGround(Registry.Revision(), *Site, Capsule, CaptureAnchor->Surface,
                        LegalSite, SiteFailure, SiteError)
                        || !LDPathSteering::PlanStart(Registry, LegalSite, Capsule, OnMesh, SiteFailure, SiteError))
                    { bPairLegal = false; break; }
                    *Site = OnMesh.SurfaceCm;
                    const auto Inside = [Site](const auto& Zone) { return FVector::Dist2D(*Site, Zone.Centre) < Zone.Radius; };
                    bPairLegal = FVector::Dist2D(*Site, CaptureAnchor->Position) > CaptureReach + 2.0 * BodyRadius
                        && !WalkKeepouts.ContainsByPredicate(Inside) && !Pickups.ContainsByPredicate(Inside);
                }
                bPairLegal &= FMath::IsNearlyEqual(KillSite.Z, HostSite.Z);
                if (bPairLegal) { bLegal = true; SiteError.Reset(); break; }
            }
            if (bLegal || !bCapsule) { break; }
        }
        if (!Check(bLegal, TEXT("Start"),
            TEXT("Both duel sites are legal ground outside the capture circle, the airdrop pickups, the dorms and the camp/Boss exclusions"),
            FString::Printf(TEXT("kill=%s host=%s %s"), *KillSite.ToCompactString(), *HostSite.ToCompactString(), *SiteError)))
        { return false; }
        const bool bCampReady = PrepareLowCamp(Registry, BodyRadius, SiteError);
        if (!Check(bCampReady, TEXT("N03"),
            TEXT("The Host's low-camp reward sample has authored homes and a legal native approach"),
            HostCampAnchor + TEXT(": ") + SiteError)) { return false; }
        for (const auto& Anchor : Registry.GetGeometry()->Anchors)
        {
            if (Anchor.Kind != ELDDemoWorldAnchorKind::Spawn) { continue; }
            FVector Spawn;
            if (!Anchor.PositionCm.ToVector(Spawn, SiteError))
            { return Check(false, TEXT("N03"), TEXT("The authored spawn position is valid"), Anchor.Id + TEXT(": ") + SiteError); }
            for (const FVector& Site : {KillSite, HostSite})
            {
                if (!PlanDuelWalk(Registry, Spawn, Site, SiteError))
                { return Check(false, TEXT("N03"), TEXT("Every authored spawn has a legal camp/Boss-free fixture route to both duel sites"),
                    Anchor.Id + TEXT(": ") + SiteError); }
            }
        }
        WalkWaypoints.Reset();
        bSitesReady = true;
        return Check(true, TEXT("N03"), TEXT("Every authored spawn has a legal camp/Boss-free fixture route to both duel sites"));
    }

    bool PrepareWalkKeepouts(const ULDMapRegistry& Registry, double BodyRadius, FString& WalkError)
    {
        const auto* Geometry = Registry.GetGeometry();
        if (!Geometry) { WalkError = TEXT("The registered world geometry is missing."); return false; }
        WalkArrivalCm = BodyRadius + LDPathSteering::WaypointToleranceCm;
        return LDScenarioWalk::Keepouts(*Geometry, *Catalog, BodyRadius, WalkArrivalCm, WalkKeepouts, WalkError);
    }

    bool PlanDuelWalk(const ULDMapRegistry& Registry, const FVector& Foot, const FVector& Site, FString& WalkError)
    {
        return LDScenarioWalk::Plan(Registry, *Catalog, Foot, Site, WalkKeepouts, WalkArrivalCm, WalkWaypoints, WalkError);
    }

    bool PrepareLowCamp(const ULDMapRegistry& Registry, double BodyRadius, FString& CampError)
    {
        const auto* Geometry = Registry.GetGeometry();
        FCollisionShape Capsule;
        if (!Geometry || !ULDMapRegistry::ContentCapsule(*Catalog, Capsule, CampError))
        { CampError = TEXT("The low-camp fixture has no registered geometry or capsule: ") + CampError; return false; }
        for (const auto& Placement : Catalog->Get().Camps)
        {
            if (Placement.MonsterId != TEXT("monster_scavenger")) { continue; }
            const auto* Anchor = Geometry->Anchors.FindByPredicate([&](const auto& Row) { return Row.Id == Placement.AnchorId; });
            const FLDDemoMonsterDefinition* Monster = nullptr;
            if (!Anchor || Anchor->Kind != ELDDemoWorldAnchorKind::Camp
                || Anchor->Members.Num() != LDDemoGrowthScenario::GrowthProbeLowCampMembers
                || !Catalog->FindMonster(Placement.MonsterId, Monster, CampError))
            { CampError = TEXT("The low-camp sample has invalid authored members: ") + Placement.AnchorId + TEXT(": ") + CampError; return false; }
            FVector Centre;
            if (!Anchor->PositionCm.ToVector(Centre, CampError)) { return false; }
            double HomeReach = 0;
            for (const auto& Member : Anchor->Members)
            {
                FVector Home;
                if (!Member.PositionCm.ToVector(Home, CampError)) { return false; }
                HomeReach = FMath::Max(HomeReach, FVector::Dist2D(Centre, Home));
            }
            const double ApproachRadius = HomeReach + LDDemoGrowthScenario::GrowthProbeRational(Monster->CollisionRadiusCm)
                + BodyRadius + LDDemoGrowthScenario::GrowthProbeArrivalCm + LDPathSteering::WaypointToleranceCm;
            const FVector TowardHost = (HostSite - Centre).GetSafeNormal2D();
            const FVector Across(TowardHost.Y, -TowardHost.X, 0);
            for (int32 Angle = 0; Angle < 16; ++Angle)
            {
                const double Radians = Angle * UE_TWO_PI / 16;
                FVector Approach = Centre + (TowardHost * FMath::Cos(Radians) + Across * FMath::Sin(Radians)) * ApproachRadius;
                FVector Normal;
                Registry.GetTerrain().Ground(FVector2D(Approach), Approach.Z, Normal);
                if (!FMath::IsNearlyEqual(Approach.Z, Centre.Z) || !Normal.Equals(FVector::UpVector, 1e-4)) { continue; }
                FLDLegalGround Ground, OnMesh;
                ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
                if (!Registry.QueryGround(Registry.Revision(), Approach, Capsule, FName(*Anchor->SurfaceId), Ground, Failure, CampError)
                    || !LDPathSteering::PlanStart(Registry, Ground, Capsule, OnMesh, Failure, CampError)
                    || !PlanDuelWalk(Registry, HostSite, OnMesh.SurfaceCm, CampError)) { continue; }
                HostCampAnchor = Anchor->Id;
                HostCampApproach = OnMesh.SurfaceCm;
                CampError.Reset();
                return true;
            }
        }
        CampError = TEXT("No authored two-scavenger camp has a legal fixture approach: ") + CampError;
        return false;
    }

    bool NavigateDuelSite(ALDPlayerController& PC, const FVector& Site, const TCHAR* Id = TEXT("N03"))
    {
        const auto* Body = Cast<ALDCharacter>(PC.GetPawn());
        const auto* Registry = PC.GetWorld()->GetSubsystem<ULDMapRegistry>();
        FString WalkError;
        if (!Body || !Registry) { return Fail(TEXT("N03"), TEXT("The fixture walking body or registered world is missing")); }
        if (WalkMatch != PC.OwnerState.Match || WalkLife != PC.OwnerState.Life || !WalkSite.Equals(Site) || WalkWaypoints.IsEmpty())
        {
            if (!PlanDuelWalk(*Registry, Body->GetNavAgentLocation(), Site, WalkError)) { return Fail(Id, WalkError); }
            WalkLife = PC.OwnerState.Life;
            WalkMatch = PC.OwnerState.Match;
            WalkSite = Site;
            WalkWaypoint = NavigateAttempts = 0;
            UE_LOG(LogLDNav, Display, TEXT("LD_GROWTH_WALK_FIXTURE life=%u site=%s waypoints=%d"),
                WalkLife, *Site.ToString(), WalkWaypoints.Num());
        }
        while (WalkWaypoint < WalkWaypoints.Num() - 1
            && FVector::Dist2D(Body->GetNavAgentLocation(), WalkWaypoints[WalkWaypoint]) <= WalkArrivalCm)
        { ++WalkWaypoint; NavigateAttempts = 0; }
        if (NavigateAttempts >= LDDemoGrowthScenario::GrowthProbeNavigateAttempts)
        { return Fail(Id, TEXT("The fixture could not reach its native walking waypoint: ") + PC.IntentFeedback); }
        PC.Navigation->Navigate(WalkWaypoints[WalkWaypoint]);
        ++NavigateAttempts;
        return true;
    }

    bool PlanKills()
    {
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        LDProgression::FRewardAmounts Bounty;
        const ELDGrowthReason BountyReason = LDProgression::ContenderKillReward(Defs.Parameters, uint8(Defs.Progression.MaxLevel), 1, Bounty);
        if (!Check(BountyReason == ELDGrowthReason::None && Bounty.XP > 0, TEXT("N03"),
            TEXT("The catalog pays a positive XP bounty for a level-1 contender"), LDGrowthWire::ReasonName(BountyReason)))
        {
            return false;
        }
        const TArray<int32>& OfferLevels = Defs.Progression.ActiveOfferLevels;
        if (!Check(OfferLevels.Num() + 1 == LDDemoGrowthScenario::GrowthProbePlannedOffers, TEXT("N03"),
            TEXT("The approved offer levels match the probe's planned offers after the opening one"),
            FString::Printf(TEXT("offer levels=%d planned=%d"), OfferLevels.Num(), LDDemoGrowthScenario::GrowthProbePlannedOffers)))
        {
            return false;
        }
        // The fewest kills of the level-1 victim that reach the last offer level; the window stage runs at the first one.
        KillsPlanned = 1;
        uint8 PlannedLevel = 0;
        ELDGrowthReason LevelReason = ELDGrowthReason::None;
        while ((LevelReason = LDProgression::LevelForXP(Defs.Progression,
            Defs.Progression.XPStart + int64(KillsPlanned) * Bounty.XP, PlannedLevel)) == ELDGrowthReason::None
            && int32(PlannedLevel) < OfferLevels.Last() && int32(PlannedLevel) < int32(Defs.Progression.MaxLevel))
        {
            ++KillsPlanned;
        }
        return Check(LevelReason == ELDGrowthReason::None && int32(PlannedLevel) >= OfferLevels.Last(), TEXT("N03"),
            TEXT("The planned kills reach the last approved offer level"),
            FString::Printf(TEXT("kills=%d level=%u last offer level=%d %s"), KillsPlanned, uint32(PlannedLevel), OfferLevels.Last(),
                LDGrowthWire::ReasonName(LevelReason)));
    }

    // ---- Host: the victim never attacks until the buyer asks to die; it walks back to its duel site after every respawn
    // and every rest return (a new life, so TickVictim plans the walk again). ----

    // An opponent's observation of the buyer carries its learned Q/W/E and nothing of its private build: no offer,
    // blueprint or point field exists in the observed unit, and the observed actives are exactly the authority's slots.
    bool CheckOpponentView(const ALDPlayerController& PC, const ALDGameMode& Mode)
    {
        const FLDWorldView* Frame = LDDemoGrowthScenario::GrowthProbeFrame(PC);
        const FLDObservedUnitView* Seen = Frame ? LDDemoGrowthScenario::GrowthProbeContender(*Frame, ClientId) : nullptr;
        const FLDGrowthBuild* BuyerBuild = Mode.Development ? Mode.Development->FindBuild(ClientId) : nullptr;
        if (!Seen || !BuyerBuild || BuyerBuild->Actives.IsEmpty()) { return true; }
        bool bSlots = true;
        FString Detail;
        for (uint8 Slot = 0; Slot < 3; ++Slot)
        {
            const int32 Held = BuyerBuild->ActiveInSlot(Slot);
            uint8 Expected = 0;
            if (Held != INDEX_NONE)
            {
                const FLDDemoAbilityDefinition* Ability = nullptr;
                FString FindError;
                bSlots &= Catalog->FindAbility(BuyerBuild->Actives[Held].Id.ToString(), Ability, FindError);
                Expected = Ability ? Ability->NativeId : 0;
            }
            bSlots &= Seen->Actives.Abilities[Slot] == Expected;
            Detail += FString::Printf(TEXT("%s%u=%u/%u"), Detail.IsEmpty() ? TEXT("") : TEXT(" "), uint32(Slot),
                uint32(Seen->Actives.Abilities[Slot]), uint32(Expected));
        }
        const UScriptStruct* Observed = FLDObservedUnitView::StaticStruct();
        const bool bNoPrivate = !FindFProperty<FProperty>(Observed, TEXT("ActiveOffer"))
            && !FindFProperty<FProperty>(Observed, TEXT("PendingBlueprints")) && !FindFProperty<FProperty>(Observed, TEXT("StatPoints"))
            && !FindFProperty<FProperty>(Observed, TEXT("UnspentPoints"));
        Check(bSlots && bNoPrivate, TEXT("Opening"),
            TEXT("A real opponent view shows the buyer's learned Q/W/E only: no offer, blueprint or point field exists in it"), Detail);
        bOpponentViewChecked = true;
        return Passed;
    }

    bool TickVictim(double Now, ALDPlayerController& PC)
    {
        ALDGameMode* Mode = PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
        if (!Mode) { return Fail(TEXT("Net"), TEXT("The Listen Host lost its authoritative game mode")); }
        if (!bOpponentViewChecked && !CheckOpponentView(PC, *Mode)) { return Finish(); }
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
            // A Host that respawned elsewhere first walks back into sight of the buyer.
            if (Exists(TEXT("KillMe.json")) && !bBuyerKilled && (bAtSite || bHostGrantSent))
            {
                return TickKillBuyer(Now, PC, *Mode);
            }
            if (bBuyerKilled && !bHostCampDone)
            {
                return TickHostAsKiller(Now, PC);
            }
            if (!bAtSite && !PC.Navigation->HasMoveOrder() && Now >= NextCommandAt)
            {
                if (!NavigateDuelSite(PC, HostSite)) { return Finish(); }
                NextCommandAt = Now + 1.0;
            }
        }
        // The Host ends the first match only after its own kills were rewarded in its own view.
        return Exists(TEXT("Client1Match1Done.json")) && bHostCampDone ? EndFirstMatch(Now, PC, *Mode) : true;
    }

    // Host, once the buyer has used its offers and its in-combat build: its own DEV XP grant first (up to the buyer's level,
    // so the duel is even), then its own attack orders kill the buyer for its build commands while dead.
    bool TickKillBuyer(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        const FLDWorldView* Frame = LDDemoGrowthScenario::GrowthProbeFrame(PC);
        const FLDObservedUnitView* Buyer = Frame ? LDDemoGrowthScenario::GrowthProbeContender(*Frame, ClientId) : nullptr;
        const FLDProgressionView* OwnProgression = PC.ReadOwnProgression();
        if (HostKillStartedAt == 0)
        {
            HostKillStartedAt = Now;
        }
        if (!bHostGrantSent)
        {
            if (!Buyer || !OwnProgression || !Mode.Growth)
            {
                if (Now - HostKillStartedAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(TEXT("A14"), TEXT("The Host never saw the buyer it was asked to kill"));
            }
            const TArray<int64>& Thresholds = Catalog->Get().Progression.XPThresholds;
            HostGrantLevel = uint8(FMath::Clamp<int32>(Buyer->Level, 1, Thresholds.Num()));
            HostGrantXP = FMath::Max<int64>(0, Thresholds[HostGrantLevel - 1] - OwnProgression->CumulativeXP);
            FString GrantError;
            HostGrantTicket = Mode.Growth->DevGrantReward(HostId, HostGrantXP, GrantError);
            bHostGrantSent = true;
            HostKillStartedAt = Now;
            return Check(HostGrantTicket != 0, TEXT("Fixture"), TEXT("The fixture queues the Host's own XP grant and returns a ticket"),
                FString::Printf(TEXT("ticket=%llu xp=%lld level %u->%u %s"), HostGrantTicket, HostGrantXP,
                    uint32(OwnProgression->Level), uint32(HostGrantLevel), *GrantError))
                ? true : Finish();
        }
        if (!bHostGranted)
        {
            FString Detail;
            const ELDDevGrantStatus Status = Mode.Growth ? Mode.Growth->DevGrantStatus(HostGrantTicket, Detail) : ELDDevGrantStatus::Unknown;
            if (Status == ELDDevGrantStatus::Rejected || Status == ELDDevGrantStatus::Unknown)
            {
                return Fail(TEXT("Fixture"), TEXT("The Host's DEV grant was not committed: ") + Detail);
            }
            const bool bVisible = Status == ELDDevGrantStatus::Granted && OwnProgression && OwnProgression->Level == HostGrantLevel;
            if (!bVisible)
            {
                if (Now - HostKillStartedAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(TEXT("Fixture"), TEXT("The Host's DEV grant did not reach its own growth view in time: ") + Detail);
            }
            Check(true, TEXT("Fixture"),
                TEXT("The grant commits in a Transactions phase through the reward composite and reaches the Host's own view"),
                FString::Printf(TEXT("%s; level %u xp %lld"), *Detail, uint32(OwnProgression->Level), OwnProgression->CumulativeXP));
            bHostGranted = true;
            HostAttackOrders = 0;
            HostKillStartedAt = Now;
            HostKillReward = {OwnProgression->CumulativeXP, PC.OwnerState.Growth.PublishRevision, 0};
            return true;
        }
        const FLDOrderReceipt& Latest = PC.Navigation->LatestReceipt();
        const bool bOrderEnded = HostAttackOrders > 0 && Latest.Identity == HostAttackOrder && (Latest.Status == ELDOrderStatus::Rejected
            || Latest.Status == ELDOrderStatus::Cancelled || Latest.Status == ELDOrderStatus::Completed);
        if (bOrderEnded && Latest.Status == ELDOrderStatus::Completed && Latest.Failure == ELDOrderFailure::TargetDied)
        {
            bBuyerKilled = true;
            HostKillReward.Since = Now;
            Check(true, TEXT("A14"), TEXT("The Host's own attack order killed the buyer"),
                FString::Printf(TEXT("orders=%d seconds=%.1f victim level %u"), HostAttackOrders, Now - HostKillStartedAt,
                    uint32(HostKillVictimLevel)));
            return true;
        }
        if (HostAttackOrders == 0 || bOrderEnded)
        {
            // #198: an attack order starts only while this segment has room for the kill, the buyer's dead stage and its
            // respawn (HostKillNeedMs); until then the kill budget does not run. Tick holds through the rest that follows.
            const int64 Left = FieldLeftMs(PC);
            const int64 Need = HostKillNeedMs(PC);
            if (Left < Need)
            {
                NoteFieldRoomWait(TEXT("host-kill"), Left, Need);
                HostKillStartedAt = Now;
                return true;
            }
            bFieldRoomWaiting = false;
            // After a rest return the buyer carries the shared return protection; an attack waits for it to end.
            if (!Buyer || LDDemoGrowthScenario::GrowthProbeProtected(*Buyer))
            {
                if (Now - HostKillStartedAt <= LDDemoGrowthScenario::GrowthProbeKillSeconds) { return true; }
                return Fail(TEXT("A14"), Buyer ? TEXT("The buyer stayed protected until the Host's kill budget ran out")
                    : TEXT("The buyer left the Host's sight before it died"));
            }
            if (HostAttackOrders >= LDDemoGrowthScenario::GrowthProbeAttackOrdersPerKill)
            {
                return Fail(TEXT("A14"), TEXT("The Host's attack kept ending without the buyer's death: ") + PC.Navigation->Status);
            }
            HostKillVictimLevel = Buyer->Level;
            PC.Navigation->AttackUnit(Buyer->Handle, Buyer->ObservationEpoch);
            HostAttackOrder = PC.Navigation->LocalIdentity();
            ++HostAttackOrders;
            return true;
        }
        if (Now - HostKillStartedAt <= LDDemoGrowthScenario::GrowthProbeKillSeconds) { return true; }
        return Fail(TEXT("A14"), TEXT("The Host's attack did not kill the buyer within its budget"));
    }

    // The Host's own reward as the Listen Host's HUD shows it: the local controller's owner view (SyncOwner applies it
    // without replication) and the rendered HUD's level texts must reach the exact expected values.
    LDDemoGrowthScenario::EGrowthProbeWait AwaitHostReward(double Now, const ALDPlayerController& PC, const FHostReward& Mark,
        int64 ExpectXP, FString& Detail) const
    {
        const FLDProgressionView* OwnProgression = PC.ReadOwnProgression();
        const FString LevelText = LDDemoGrowthScenario::GrowthProbeHudText(PC, TEXT("LevelBadgeText"));
        const auto* LevelBar = Cast<UProgressBar>(LDDemoGrowthScenario::GrowthProbeHudWidget(PC, TEXT("LevelBar")));
        const FString LevelTooltip = LevelBar ? LevelBar->GetToolTipText().ToString() : FString();
        LDHUD::FLevelView Level;
        if (OwnProgression)
        {
            Level = LDHUD::Level(OwnProgression->Level, OwnProgression->MaxLevel, OwnProgression->CumulativeXP,
                OwnProgression->LevelThresholdXP, OwnProgression->bHasNextThreshold, OwnProgression->NextThresholdXP);
        }
        Detail = FString::Printf(TEXT("own view xp %lld->%lld (want %lld) publish %llu->%llu; HUD \"%s\" / \"%s\" fraction=%.6f"),
            Mark.XPBefore, OwnProgression ? OwnProgression->CumulativeXP : -1, ExpectXP, Mark.PublishBefore,
            PC.OwnerState.Growth.PublishRevision, *LevelText, *LevelTooltip, LevelBar ? LevelBar->GetPercent() : -1.f);
        const bool bView = OwnProgression && OwnProgression->CumulativeXP == ExpectXP
            && PC.OwnerState.Growth.PublishRevision > Mark.PublishBefore;
        // The rendered world feedback floats the same reward beside the own body: "+N <LDTerms::Experience>".
        const TArray<FString>* Lines = LDDemoGrowthScenario::GrowthProbeRewardLines(PC);
        FString Floated;
        const bool bFloated = Lines && LDDemoGrowthScenario::GrowthProbeRewardTail(*Lines, ExpectXP - Mark.XPBefore, Floated);
        Detail += FString::Printf(TEXT("; floated \"%s\""), Lines ? *Floated : TEXT("<no world feedback layer>"));
        const bool bHud = bView && bFloated && LevelBar && OwnProgression
            && LevelText == FString::FromInt(OwnProgression->Level)
            && LevelTooltip == Level.Level + TEXT(" · ") + Level.Experience
            && FMath::IsNearlyEqual(LevelBar->GetPercent(), Level.Fraction, 1e-4f);
        if (bHud)
        {
            return LDDemoGrowthScenario::EGrowthProbeWait::Ready;
        }
        return Now - Mark.Since <= LDDemoGrowthScenario::GrowthProbeRequestSeconds ? LDDemoGrowthScenario::EGrowthProbeWait::Waiting
            : LDDemoGrowthScenario::EGrowthProbeWait::Failed;
    }

    // Host as killer: its kill of the buyer, then its last hits on both scavengers of the authored low camp, each pay exactly the
    // approved reward into the Host's own owner view and HUD.
    bool TickHostAsKiller(double Now, ALDPlayerController& PC)
    {
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        FString Detail;
        if (!bHostKillRewardSeen)
        {
            LDProgression::FRewardAmounts Bounty;
            const ELDGrowthReason BountyReason = LDProgression::ContenderKillReward(Defs.Parameters, uint8(Defs.Progression.MaxLevel),
                HostKillVictimLevel, Bounty);
            if (!Check(BountyReason == ELDGrowthReason::None && Bounty.XP > 0, TEXT("N03"),
                TEXT("The buyer's level at death has a positive approved XP bounty"), LDGrowthWire::ReasonName(BountyReason)))
            {
                return Finish();
            }
            const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitHostReward(Now, PC, HostKillReward,
                HostKillReward.XPBefore + Bounty.XP, Detail);
            if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
            if (!Check(Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Ready, TEXT("N03"),
                TEXT("The Listen Host's own kill of a contender pays the exact XP bounty into its own owner view and rendered HUD"),
                FString::Printf(TEXT("victim L%u +%lld XP; %s"), uint32(HostKillVictimLevel), Bounty.XP, *Detail)))
            {
                return Finish();
            }
            HostKillerRow = MakeShared<FJsonObject>();
            HostKillerRow->SetNumberField(TEXT("contender_victim_level"), HostKillVictimLevel);
            HostKillerRow->SetNumberField(TEXT("contender_xp"), double(Bounty.XP));
            bHostKillRewardSeen = true;
            NextCommandAt = 0;
            return true;
        }
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        if (!bHostAtCamp)
        {
            if (FVector::Dist2D(Body->GetNavAgentLocation(), HostCampApproach)
                <= LDDemoGrowthScenario::GrowthProbeArrivalCm)
            {
                const FLDProgressionView* OwnProgression = PC.ReadOwnProgression();
                if (!OwnProgression) { return Fail(TEXT("N03"), TEXT("The Host's own growth view vanished at the camp")); }
                HostCampReward = {OwnProgression->CumulativeXP, PC.OwnerState.Growth.PublishRevision, Now};
                HostCampOrders = 0;
                bHostAtCamp = true;
                return true;
            }
            if (!PC.Navigation->HasMoveOrder() && Now >= NextCommandAt)
            {
                if (!NavigateDuelSite(PC, HostCampApproach)) { return false; }
                NextCommandAt = Now + 1.0;
            }
            return true;
        }
        if (HostCampKills < LDDemoGrowthScenario::GrowthProbeLowCampMembers)
        {
            const FLDOrderReceipt& Latest = PC.Navigation->LatestReceipt();
            const bool bOrderEnded = HostCampOrders > 0 && Latest.Identity == HostCampOrder && (Latest.Status == ELDOrderStatus::Rejected
                || Latest.Status == ELDOrderStatus::Cancelled || Latest.Status == ELDOrderStatus::Completed);
            if (HostCampOrders > 0 && !bOrderEnded)
            {
                if (Now - HostCampReward.Since <= LDDemoGrowthScenario::GrowthProbeKillSeconds) { return true; }
                return Fail(TEXT("N03"), TEXT("The Host's attack on the low camp did not end in time"));
            }
            if (bOrderEnded && Latest.Status == ELDOrderStatus::Completed && Latest.Failure == ELDOrderFailure::TargetDied)
            {
                ++HostCampKills;
                HostCampOrders = 0;
                if (HostCampKills >= LDDemoGrowthScenario::GrowthProbeLowCampMembers) { return true; }
            }
            // The camp retaliates as a group, so the next scavenger is near; a lost lock seeks the same camp again.
            const FLDWorldView* Frame = LDDemoGrowthScenario::GrowthProbeFrame(PC);
            const FLDObservedUnitView* Target = nullptr;
            double Best = FMath::Square(LDDemoGrowthScenario::GrowthProbeCampSearchCm);
            if (Frame)
            {
                for (const FLDObservedUnitView& Unit : Frame->Units)
                {
                    const double Distance = FVector::DistSquared2D(FVector(Unit.FeetCm), HostCampApproach);
                    if (Unit.Kind == ELDObservedUnitKind::CampMember && Unit.CreatureId == uint8(ELDDemoMonsterId::MonsterScavenger)
                        && Distance <= Best)
                    {
                        Target = &Unit;
                        Best = Distance;
                    }
                }
            }
            if (!Target)
            {
                if (Now - HostCampReward.Since <= LDDemoGrowthScenario::GrowthProbeKillSeconds) { return true; }
                return Fail(TEXT("N03"), FString::Printf(TEXT("No scavenger of authored low camp %s was visible to the Host (%d killed)"),
                    *HostCampAnchor, HostCampKills));
            }
            if (HostCampKills * LDDemoGrowthScenario::GrowthProbeAttackOrdersPerKill + HostCampOrders
                >= LDDemoGrowthScenario::GrowthProbeLowCampMembers * LDDemoGrowthScenario::GrowthProbeAttackOrdersPerKill)
            {
                return Fail(TEXT("N03"), TEXT("The Host's attacks on the low camp kept ending without a death: ") + PC.Navigation->Status);
            }
            PC.Navigation->AttackUnit(Target->Handle, Target->ObservationEpoch);
            HostCampOrder = PC.Navigation->LocalIdentity();
            ++HostCampOrders;
            return true;
        }
        LDProgression::FRewardAmounts Monster;
        const ELDGrowthReason MonsterReason = LDProgression::MonsterKillReward(*Catalog, FName(TEXT("monster_scavenger")), 0, Monster);
        if (!Check(MonsterReason == ELDGrowthReason::None && Monster.XP > 0, TEXT("N03"),
            TEXT("The low camp's scavenger has a positive approved XP reward"), LDGrowthWire::ReasonName(MonsterReason)))
        {
            return Finish();
        }
        if (HostCampReward.Since < HostKillReward.Since || HostCampKills != LDDemoGrowthScenario::GrowthProbeLowCampMembers)
        {
            return Fail(TEXT("N03"), TEXT("The Host's camp kills were counted out of order"));
        }
        const int64 Members = LDDemoGrowthScenario::GrowthProbeLowCampMembers;
        const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitHostReward(Now, PC, HostCampReward,
            HostCampReward.XPBefore + Members * Monster.XP, Detail);
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
        if (!Check(Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Ready, TEXT("N03"),
            TEXT("The Listen Host's last hits on both low-camp scavengers pay their exact rewards into its own owner view and rendered HUD"),
            FString::Printf(TEXT("%lld x (+%lld XP); %s"), Members, Monster.XP, *Detail)))
        {
            return Finish();
        }
        if (HostKillerRow.IsValid())
        {
            HostKillerRow->SetNumberField(TEXT("camp_kills"), HostCampKills);
            HostKillerRow->SetStringField(TEXT("camp_anchor"), HostCampAnchor);
            HostKillerRow->SetNumberField(TEXT("camp_xp"), double(Members * Monster.XP));
        }
        bHostCampDone = true;
        return true;
    }

    // Both roles: the first match's identities and dirty owner state, kept for the second match and the report.
    void RecordFirstMatch(const ALDPlayerController& PC)
    {
        FirstGeneration = PC.OwnerState.Match;
        // The Host may be between lives here; it keeps the last living body it was bound to.
        if (PC.OwnerState.BodyRef.IsValid()) { FirstBody = PC.OwnerState.BodyRef; }
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        FirstProgression = ProgView ? *ProgView : FLDProgressionView();
        FirstMatchRow = MakeShared<FJsonObject>();
        FirstMatchRow->SetNumberField(TEXT("generation"), FirstGeneration);
        FirstMatchRow->SetStringField(TEXT("entity"), LDDemoGrowthScenario::GrowthProbeGuid(PC.OwnerState.ContenderId));
        FirstMatchRow->SetNumberField(TEXT("life"), PC.OwnerState.Life);
        FirstMatchRow->SetNumberField(TEXT("level"), FirstProgression.Level);
        FirstMatchRow->SetNumberField(TEXT("xp"), double(FirstProgression.CumulativeXP));
        FirstMatchRow->SetNumberField(TEXT("earned_opportunities"), FirstProgression.ActiveOpportunitiesEarned);
        FirstMatchRow->SetNumberField(TEXT("queued_opportunities"), FirstProgression.QueuedActiveOpportunities);
        FirstMatchRow->SetNumberField(TEXT("unspent_points"), double(FirstProgression.UnspentPoints));
        FirstMatchRow->SetNumberField(TEXT("actives"), FirstProgression.Actives.Num());
        FirstMatchRow->SetNumberField(TEXT("blueprints"), FirstProgression.PendingBlueprints.Num());
        // D10: deaths, respawns and rest returns never re-grant the opening opportunity; only the approved offer levels add
        // opportunities. The public death count (never moved by a rest retirement) shows this owner died: the Host as the
        // buyer's victim at least once, the buyer exactly once, by the Host.
        const uint8 Expected = LDProgression::ActiveOpportunitiesAt(Catalog->Get().Progression, FirstProgression.Level);
        const int64 Deaths = OwnDeaths(PC);
        FirstMatchRow->SetNumberField(TEXT("deaths"), double(Deaths));
        Check(ProgView && Deaths >= 1 && (IsHost() || Deaths == 1) && FirstProgression.ActiveOpportunitiesEarned == Expected,
            TEXT("Opening"),
            TEXT("After dying and respawning the owner holds exactly its opening opportunity plus one per offer level reached"),
            FString::Printf(TEXT("life %u, deaths %lld, expected %u: %s"), PC.OwnerState.Life, Deaths, uint32(Expected),
                *LDDemoGrowthScenario::GrowthProbeProgressionText(FirstProgression)));
    }

    // Host: end the first match without a winner through the Host's own session command.
    bool EndFirstMatch(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        Check(bOpponentViewChecked, TEXT("Opening"), TEXT("A real opponent view of the buyer was checked during the first match"));
        Check(Mode.IsGameplayWriteOpen(Mode.GetMatchContext()), TEXT("Iso"),
            TEXT("The first match stayed open for the whole scenario: no capture result, fault or reset"));
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
            // The buyer also waits while the Host finishes its own rewarded kills before it ends the match.
            const double Budget = LDDemoGrowthScenario::GrowthProbeTransitionSeconds + (IsHost() ? 0.0 : LDDemoGrowthScenario::GrowthProbeKillSeconds);
            if (Now - StageAt <= Budget) { return true; }
            return Fail(TEXT("Iso"), FString::Printf(TEXT("The room did not return to a new Lobby generation (generation %u, phase %s, owner match %u)"),
                Lobby.Generation, *StaticEnum<ELDMatchPhase>()->GetNameStringByValue(int64(Lobby.Phase)), PC.OwnerState.Match));
        }
        Check(Lobby.Generation > FirstGeneration && Lobby.Members.Num() == 2
            && !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& LobbyMember) { return LobbyMember.Ready; }), TEXT("Iso"),
            TEXT("The same two connections return to a new, unready Lobby generation"),
            FString::Printf(TEXT("generation %u -> %u, members=%d"), FirstGeneration, Lobby.Generation, Lobby.Members.Num()));
        Check(PC.ReadOwnProgression() == nullptr, TEXT("Iso"), TEXT("Between matches the owner publication carries no growth view"));
        if (!Passed) { return Finish(); }
        // The Host readies only after the buyer saw the new Lobby unready, so its own ready cannot race the buyer's first look.
        if (!IsHost() && !Mark(TEXT("BuyerSawSecondLobby.json"))) { return Finish(); }
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
        const FLDCombatOwnerView& Own = PC.CombatView;
        if (!Cast<ALDCharacter>(PC.GetPawn()) || PC.OwnerState.Match != SecondGeneration || PC.OwnerState.LifePhase != ELDLifePhase::Alive
            || PC.OwnerState.Life < LDDemoGrowthScenario::GrowthProbeFieldLife
            || !PC.OwnerState.BodyRef.IsValid() || !PC.CanWorldCommand() || !ProgView
            || Own.Generation != SecondGeneration || Own.Life != PC.OwnerState.Life || !Own.bHasBody)
        {
            if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeTransitionSeconds) { return true; }
            return Fail(TEXT("Iso"), TEXT("The second match never published a living first body with every own view"));
        }
        if (!ResolveRoster(PC, Lobby)) { return Finish(); }
        CheckSecondMatch(PC, *ProgView);
        if (!Passed) { return Finish(); }
        FieldLife = PC.OwnerState.Life;
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
    void CheckSecondMatch(const ALDPlayerController& PC, const FLDProgressionView& Progress)
    {
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        const FLDOwnerState& Self = PC.OwnerState;
        const FLDCombatOwnerView& Own = PC.CombatView;
        const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>();
        const int64 ReturnProtectedUntilMs = Public ? Public->PublicMatch.Timeline.FieldStartedAtMs + Defs.Parameters.ProtectionMs : -1;
        Check(Self.Match == SecondGeneration && Self.Life == LDDemoGrowthScenario::GrowthProbeFieldLife && Progress.Life == Self.Life
            && FirstBody.IsValid() && Self.BodyRef != FirstBody
            && (Self.ProtectedUntilMs == 0 || Self.ProtectedUntilMs == ReturnProtectedUntilMs), TEXT("Iso"),
            TEXT("The second match starts its first field life (after its R0 rest life) in a fresh body with at most the shared return protection"),
            FString::Printf(TEXT("generation %u life %u (first match ended on life %u)"), Self.Match, Self.Life,
                FirstMatchRow.IsValid() ? uint32(FirstMatchRow->GetNumberField(TEXT("life"))) : 0u));
        uint8 StartLevel = 0;
        const bool bStartLevel = LDProgression::LevelForXP(Defs.Progression, Defs.Progression.XPStart, StartLevel) == ELDGrowthReason::None;
        Check(bStartLevel && Progress.Generation == SecondGeneration && Progress.Level == StartLevel
            && Progress.CumulativeXP == Defs.Progression.XPStart && Progress.CatalogDigest == Defs.Identity.Root, TEXT("Iso"),
            TEXT("Level and XP restart at the content's opening values"),
            FString::Printf(TEXT("now %s; first match ended at %s"), *LDDemoGrowthScenario::GrowthProbeProgressionText(Progress),
                *LDDemoGrowthScenario::GrowthProbeProgressionText(FirstProgression)));
        // D10: the new match grants its own single opening opportunity with a newly drawn offer and fresh opening points;
        // nothing of the first carries over.
        CheckOpening(Progress, TEXT("Iso"), SecondOpeningRow, false);
        Check(OpeningRow.IsValid() && Progress.ActiveOffer.OfferId.IsValid()
            && LDDemoGrowthScenario::GrowthProbeGuid(Progress.ActiveOffer.OfferId) != OpeningRow->GetStringField(TEXT("offer"))
            && Progress.ActiveOffer.OfferId != FirstProgression.ActiveOffer.OfferId, TEXT("Iso"),
            TEXT("The second match's opening offer is newly drawn, not the first match's opening or last head offer"),
            SecondOpeningRow.IsValid() ? SecondOpeningRow->GetStringField(TEXT("options")) : FString());
        // The innate D dash is every contender's source: it is held from the start, but a fresh match issues it a fresh,
        // ready track (no duration issued yet); Q, W and E start empty, with no learned active's track carried over.
        const bool bNoSource = !Own.Entries.ContainsByPredicate([&Own](const FLDCombatEntryView& EntryRow)
        {
            if (EntryRow.Entry == ELDCombatEntry::Dash) { return EntryRow.ReadyAtMs > Own.AsOfMs || EntryRow.CooldownMs != 0; }
            return !EntryRow.DefinitionId.IsNone() || EntryRow.IssuedCooldownId.IsValid() || EntryRow.ReadyAtMs > Own.AsOfMs;
        });
        const bool bNoContribution = !Own.Attributes.ContainsByPredicate([](const FLDCombatAttributeRowView& Row)
        {
            return Row.Choices.Numerator != 0 || Row.PercentBonus.Numerator != 0 || Row.Dev.Numerator != 0;
        });
        Check(bNoSource && bNoContribution && Own.NextAttackAtMs <= Own.AsOfMs && Own.BuildSafeAtMs == 0, TEXT("Iso"),
            TEXT("Combat carries no learned source, no issued Q/W/E cooldown track, no point contribution and no combat window"),
            FString::Printf(TEXT("entries=%d attributes=%d as_of=%lld safe_at=%lld"), Own.Entries.Num(), Own.Attributes.Num(), Own.AsOfMs,
                Own.BuildSafeAtMs));
        if (!IsHost()) { return; }
        // Host only: the authority growth records of the second match.
        const ALDGameMode* Mode = PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
        const ALDGrowthDirector* Growth = Mode ? Mode->Growth.Get() : nullptr;
        bool bFresh = Growth && Mode->Development && Growth->GetMatch() == Mode->GetMatchContext()
            && Mode->GetMatchContext().Generation == SecondGeneration && Mode->GetMatchContext().LedgerMatch != FirstLedgerMatch
            && Growth->DevRecentRewards().IsEmpty();
        for (const FGuid& Contender : {HostId, ClientId})
        {
            const FLDProgressionState* Record = bFresh ? Mode->Development->FindProgression(Contender) : nullptr;
            const FLDGrowthBuild* Built = bFresh ? Mode->Development->FindBuild(Contender) : nullptr;
            bFresh &= Record && Built && Record->CumulativeXP == Defs.Progression.XPStart && Record->ActiveEarned == 1
                && Record->ActiveConsumed == 0 && Record->Records.IsEmpty() && Built->Actives.IsEmpty()
                && Built->PendingBlueprints.IsEmpty() && !Built->StatPoints.ContainsByPredicate([](int32 Points) { return Points != 0; });
        }
        Check(bFresh, TEXT("Iso"),
            TEXT("The second match opens fresh growth records under a new LedgerMatch: XP start, only the opening opportunity, no build and no reward"),
            FString::Printf(TEXT("ledger match %s -> %s"), *LDDemoGrowthScenario::GrowthProbeGuid(FirstLedgerMatch),
                Mode ? *LDDemoGrowthScenario::GrowthProbeGuid(Mode->GetMatchContext().LedgerMatch) : TEXT("none")));
    }

    // Host: once the buyer finished its stale replays, the authority confirms they committed nothing.
    bool TickSecondAudit(ALDPlayerController& PC)
    {
        if (!Exists(TEXT("Client1Match2Done.json"))) { return true; }
        ALDGameMode* Mode = PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
        const ALDGrowthDirector* Growth = Mode ? Mode->Growth.Get() : nullptr;
        const FLDProgressionState* Buyer = Growth && Mode->Development ? Mode->Development->FindProgression(ClientId) : nullptr;
        const FLDGrowthBuild* Built = Growth && Mode->Development ? Mode->Development->FindBuild(ClientId) : nullptr;
        Check(Growth && Buyer && Built && Growth->DevRecentRewards().IsEmpty() && Buyer->CumulativeXP == Catalog->Get().Progression.XPStart
            && Buyer->ActiveConsumed == 0 && Buyer->Records.IsEmpty() && Built->Actives.IsEmpty(), TEXT("Iso"),
            TEXT("The stale first-match requests committed nothing: the buyer's second-match growth record is untouched"),
            Buyer ? FString::Printf(TEXT("xp=%lld consumed=%u rewards=%d"), Buyer->CumulativeXP, uint32(Buyer->ActiveConsumed),
                Growth->DevRecentRewards().Num()) : FString(TEXT("no growth record")));
        Check(Mode && Mode->IsGameplayWriteOpen(Mode->GetMatchContext()), TEXT("Iso"), TEXT("The second match stayed open for its checks"));
        if (!Passed || !Mark(TEXT("HostAudit2.json"))) { return Finish(); }
        SetStage(EStage::HostDev);
        return true;
    }

    // ---- DEV test commands (#78), second match: after the Host confirmed the clean second match, each role sends DEV test
    // commands through its own owning connection (Client1 remote, the Host as Listen Host) and sees every terminal answer
    // and committed owner change; stale, conflicting and illegal requests are refused without changing anything. ----

    enum class EDevWait : uint8 { Waiting, Done, Failed };

    bool SendDev(double Now, ALDPlayerController& PC, const FString& Name, ELDDevTestOp Op, int64 Amount,
        FName Definition = NAME_None, uint8 Slot = 0, uint8 Stat = 0)
    {
        const FLDProgressionView* Growth = PC.ReadOwnProgression();
        Dev = FDevCall();
        Dev.Name = Name;
        Dev.ResultMark = DevResults.Num();
        Dev.SentAt = Now;
        Dev.XPBefore = Growth ? Growth->CumulativeXP : -1;
        const FLDCombatAttributeRowView* AD = OwnAttribute(PC, ELDProjectedAttribute::AD);
        Dev.ADBeforeP2 = AD ? ExactP2(AD->Final) : -1;
        ELDGrowthReason Local = ELDGrowthReason::None;
        const uint64 RequestId = PC.SubmitDevTest(Op, Amount, Local, Definition, Slot, Stat);
        if (!Check(RequestId != 0, TEXT("DevTest"), Name + TEXT(" is sent through SubmitDevTest"), LDGrowthWire::ReasonName(Local)))
        {
            return Finish();
        }
        Dev.Sent.Generation = PC.OwnerState.Match;
        Dev.Sent.Life = PC.OwnerState.Life;
        Dev.Sent.RequestId = RequestId;
        Dev.Sent.Op = Op;
        Dev.Sent.Amount = Amount;
        Dev.Sent.Slot = Slot;
        Dev.Sent.Stat = Stat;
        if (!Definition.IsNone())
        {
            FLDDefinitionWire::Encode(Definition, Dev.Sent.Definition);
        }
        Dev.bSent = true;
        return true;
    }

    // A raw request through the owner RPC with its own ids: the stale, replayed and conflicting probes.
    // The HUD's 测试 entry is shown and opens its panel; returns the panel, or nullptr after recording why not.
    ULDDevTestPanel* OpenDevPanel(ALDPlayerController& PC, const FString& Name)
    {
        ULDHUD* Hud = PC.HUDUI.Get();
        ULDDevTestPanel* Panel = Hud ? Hud->DevTestPanel.Get() : nullptr;
        const UWidget* Entry = Hud && Hud->WidgetTree ? Hud->WidgetTree->FindWidget(TEXT("HUDDevTest")) : nullptr;
        const bool bOpen = Panel && Entry && Entry->IsVisible() && PC.RootUI && PC.RootUI->ShowMainPanel(ELDUIMainPanel::DevTest)
            && Panel->IsPanelOpen() && Panel->WidgetTree;
        Check(bOpen, TEXT("DevTest"), Name + TEXT(": the HUD shows the 测试 entry of this DEV test room and opens its panel"));
        return bOpen ? Panel : nullptr;
    }

    // Shown in the panel's tree: the widget and every ancestor visible.
    static bool ShownInTree(const UWidget* Widget)
    {
        for (const UWidget* At = Widget; At; At = At->GetParent())
        {
            if (!At->IsVisible()) { return false; }
        }
        return Widget != nullptr;
    }

    // One DEV command through the panel's own button (and its entry field), the same path a tester clicks. The request id
    // the panel got from the controller is recorded so the answer is matched like any other DEV call.
    bool SendDevByPanel(double Now, ALDPlayerController& PC, const FString& Name, ELDDevTestOp Op, int64 Amount, FName Button,
        FName Field = NAME_None, const FString& Text = FString())
    {
        ULDDevTestPanel* Panel = OpenDevPanel(PC, Name);
        if (!Panel) { return Finish(); }
        ULDCommandButton* Click = Cast<ULDCommandButton>(Panel->WidgetTree->FindWidget(Button));
        UEditableTextBox* Box = Field.IsNone() ? nullptr : Cast<UEditableTextBox>(Panel->WidgetTree->FindWidget(Field));
        if (!Check(Click && ShownInTree(Click) && (Field.IsNone() || Box), TEXT("DevTest"), Name + TEXT(": the panel shows its button")))
        {
            return Finish();
        }
        if (Box) { Box->SetText(FText::FromString(Text)); }
        const FLDProgressionView* Growth = PC.ReadOwnProgression();
        Dev = FDevCall();
        Dev.Name = Name;
        Dev.ResultMark = DevResults.Num();
        Dev.SentAt = Now;
        Dev.XPBefore = Growth ? Growth->CumulativeXP : -1;
        Dev.Sent.Generation = PC.OwnerState.Match;
        Dev.Sent.Life = PC.OwnerState.Life;
        Dev.Sent.Op = Op;
        Dev.Sent.Amount = Amount;
        uint64 Captured = 0;
        const TFunction<uint64(ELDDevTestOp, int64, ELDGrowthReason&, FName, uint8, uint8)> Original = Panel->Submit;
        Panel->Submit = [&Captured, &Original](ELDDevTestOp O, int64 A, ELDGrowthReason& R, FName D, uint8 S, uint8 T)
        {
            Captured = Original ? Original(O, A, R, D, S, T) : 0;
            return Captured;
        };
        // The id is known only once Submit returns, and a Listen Host answers inside it; the answer stays in DevResults.
        Click->Execute();
        Panel->Submit = Original;
        if (!Check(Captured != 0, TEXT("DevTest"), Name + TEXT(" is sent by the panel button"), Panel->FeedbackText()))
        {
            return Finish();
        }
        Dev.Sent.RequestId = Captured;
        Dev.bSent = true;
        return true;
    }

    bool SendDevDirect(double Now, ALDPlayerController& PC, const FString& Name, const FLDDevTestCommand& Raw)
    {
        const FLDProgressionView* Growth = PC.ReadOwnProgression();
        Dev = FDevCall();
        Dev.Name = Name;
        Dev.ResultMark = DevResults.Num();
        Dev.SentAt = Now;
        Dev.XPBefore = Growth ? Growth->CumulativeXP : -1;
        Dev.Sent = Raw;
        Dev.bSent = true;
        PC.ServerDevTestCommand(Raw);
        return true;
    }

    // The terminal answer to the request in flight: its final receipt or its admission refusal.
    const FLDDevTestResult* DevAnswer() const
    {
        for (int32 Index = Dev.ResultMark; Index < DevResults.Num(); ++Index)
        {
            const FLDDevTestResult& Answer = DevResults[Index];
            if (Answer.Generation == Dev.Sent.Generation && Answer.RequestId == Dev.Sent.RequestId
                && (Answer.Status == ELDAdmissionStatus::Final || Answer.Status == ELDAdmissionStatus::AdmissionRejected))
            {
                return &Answer;
            }
        }
        return nullptr;
    }

    // Waits for the terminal answer and the owner view it implies (Settled), bounded; then checks status, outcome and reason.
    EDevWait AwaitDev(double Now, ELDAdmissionStatus Status, ELDReceiptOutcome Outcome, ELDGrowthReason Reason,
        TFunctionRef<bool()> Settled)
    {
        const FLDDevTestResult* Answer = DevAnswer();
        const bool bSettled = Answer && Settled();
        if (!bSettled)
        {
            if (Now - Dev.SentAt < LDDemoGrowthScenario::GrowthProbeDevSettleSeconds) { return EDevWait::Waiting; }
            Check(false, TEXT("DevTest"), Dev.Name + (Answer ? TEXT(": the owner view never showed the committed result")
                : TEXT(": no terminal answer arrived")), Answer ? (Answer->bHasReceipt ? Answer->Receipt.Detail : Answer->Detail) : FString());
            return EDevWait::Failed;
        }
        const ELDGrowthReason Got = LDGrowthWire::Reason(Answer->bHasReceipt ? Answer->Receipt.Header.Reason : Answer->Reason);
        const bool bOutcome = Status != ELDAdmissionStatus::Final
            || (Answer->bHasReceipt && Answer->Receipt.Header.Outcome == Outcome && Answer->Receipt.Header.RequestId == Dev.Sent.RequestId);
        const FString Detail = FString::Printf(TEXT("status %u reason %s: %s"), uint32(Answer->Status), LDGrowthWire::ReasonName(Got),
            Answer->bHasReceipt ? *Answer->Receipt.Detail : *Answer->Detail);
        const bool bOk = Check(Answer->Status == Status && Got == Reason && bOutcome, TEXT("DevTest"), Dev.Name, Detail);
        const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("step"), Dev.Name);
        Row->SetStringField(TEXT("op"), LDDevTest::OpName(Dev.Sent.Op));
        Row->SetNumberField(TEXT("request"), double(Dev.Sent.RequestId));
        Row->SetStringField(TEXT("reason"), LDGrowthWire::ReasonName(Got));
        Row->SetStringField(TEXT("detail"), Detail);
        DevRows.Add(MakeShared<FJsonValueObject>(Row));
        return bOk ? EDevWait::Done : EDevWait::Failed;
    }

    bool AdvanceDev(EDevWait Wait)
    {
        if (Wait == EDevWait::Failed) { return Finish(); }
        if (Wait == EDevWait::Done)
        {
            ++DevStep;
            Dev = FDevCall();
        }
        return true;
    }

    // Client1, second match: every DEV op through the remote owning connection, after the Host confirmed the clean second match.
    bool TickSecondDev(double Now, ALDPlayerController& PC)
    {
        if (!Exists(TEXT("HostAudit2.json"))) { return true; }
        const FLDProgressionView* Prog = PC.ReadOwnProgression();
        if (!Prog || !Catalog) { return true; }
        const auto XPIs = [&PC](int64 Want) { const FLDProgressionView* View = PC.ReadOwnProgression(); return View && View->CumulativeXP == Want; };
        const FName DevActive(LDDemoGrowthScenario::GrowthProbeDevActive);
        const FName DevStat(LDDemoGrowthScenario::GrowthProbeDevStat);
        switch (DevStep)
        {
        case 0:
            if (!Dev.bSent)
            {
                FLDDevTestCommand Raw;
                Raw.Generation = FirstGeneration;
                Raw.Life = PC.OwnerState.Life;
                Raw.RequestId = 1;
                Raw.Op = ELDDevTestOp::GrantExperience;
                Raw.Amount = 5;
                return SendDevDirect(Now, PC, TEXT("A DEV command naming the first match is refused as OldMatch"), Raw);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::AdmissionRejected, ELDReceiptOutcome::None, ELDGrowthReason::OldMatch,
                [&] { return XPIs(Dev.XPBefore); }));
        case 1:
            if (!Dev.bSent)
            {
                // The grant stops one XP short of the next level, so the opening opportunity stays the only one.
                DevGrantXP = Prog->bHasNextThreshold ? Prog->NextThresholdXP - Prog->CumulativeXP - 1 : 0;
                if (!Check(DevGrantXP > 0 && Prog->Level == 1, TEXT("DevTest"),
                    TEXT("The second match's level 1 leaves room for a DEV XP grant below the next level"),
                    LDDemoGrowthScenario::GrowthProbeProgressionText(*Prog))) { return Finish(); }
                return SendDev(Now, PC, TEXT("A DEV XP grant commits once into the owner's own growth"), ELDDevTestOp::GrantExperience,
                    DevGrantXP);
            }
            {
                const EDevWait Wait = AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                    [&] { const FLDProgressionView* View = PC.ReadOwnProgression();
                        return XPIs(Dev.XPBefore + DevGrantXP) && View->Level == 1 && View->QueuedActiveOpportunities == 1; });
                if (Wait == EDevWait::Done)
                {
                    DevXPCommand = Dev.Sent;
                }
                return AdvanceDev(Wait);
            }
        case 2:
            if (!Dev.bSent)
            {
                return SendDevDirect(Now, PC, TEXT("A replay of the committed grant answers its final receipt and commits nothing"),
                    DevXPCommand);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [&] { return XPIs(Dev.XPBefore); }));
        case 3:
            if (!Dev.bSent)
            {
                FLDDevTestCommand Raw = DevXPCommand;
                ++Raw.Amount;
                return SendDevDirect(Now, PC, TEXT("The committed request id with other terms is refused as RequestConflict"), Raw);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::AdmissionRejected, ELDReceiptOutcome::None,
                ELDGrowthReason::RequestConflict, [&] { return XPIs(Dev.XPBefore); }));
        case 4:
            if (!Dev.bSent)
            {
                return SendDev(Now, PC, TEXT("A DEV cooldown reset applies to the owner's own tracks"), ELDDevTestOp::ResetCooldowns, 0);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [] { return true; }));
        case 5:
            if (!Dev.bSent)
            {
                return SendDev(Now, PC, TEXT("A DEV refill applies to the living body"), ELDDevTestOp::RefillResources, 0);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [] { return true; }));
        case 6:
            if (!Dev.bSent)
            {
                if (!Check(Prog->Actives.IsEmpty() && Prog->QueuedActiveOpportunities == 1 && Prog->bHasActiveOffer
                    && !Prog->ActiveOffer.Options.Contains(DevActive), TEXT("DevTest"),
                    TEXT("The second match's opening offer is still open and does not name the DEV learn's active"),
                    LDDemoGrowthScenario::GrowthProbeProgressionText(*Prog))) { return Finish(); }
                return SendDev(Now, PC, TEXT("A DEV learn puts an active into the lowest empty slot without an opportunity"),
                    ELDDevTestOp::LearnActive, 0, DevActive, 0);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [&] { const FLDProgressionView* View = PC.ReadOwnProgression();
                    const FLDActiveSlotView* Q = View ? LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 0) : nullptr;
                    const FLDCombatEntryView* Entry = CurrentEntry(PC, ELDCombatEntry::Q);
                    return Q && Q->DefinitionId == DevActive && View->Actives.Num() == 1 && View->QueuedActiveOpportunities == 1
                        && View->bHasActiveOffer && Entry && Entry->DefinitionId == DevActive; }));
        case 7:
            if (!Dev.bSent)
            {
                return SendDev(Now, PC, TEXT("A DEV learn of an already learned active is refused as InvalidOption"),
                    ELDDevTestOp::LearnActive, 0, DevActive, 0);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Rejected, ELDGrowthReason::InvalidOption,
                [&] { const FLDProgressionView* View = PC.ReadOwnProgression(); return View && View->Actives.Num() == 1; }));
        case 8:
            if (!Dev.bSent)
            {
                const FLDCombatAttributeRowView* AD = OwnAttribute(PC, ELDProjectedAttribute::AD);
                if (!Check(AD && ExactP2(AD->Final) > 0 && AD->Choices.Numerator == 0 && Prog->UnspentPoints > 0
                    && LDDemoGrowthScenario::GrowthProbePointsOf(*Prog, DevStat) == 0, TEXT("DevTest"),
                    TEXT("The owner has an exact AD row without a point term and unspent opening points before the DEV allocation"),
                    LDDemoGrowthScenario::GrowthProbeProgressionText(*Prog))) { return Finish(); }
                return SendDev(Now, PC, TEXT("A DEV allocation spends every unspent point on one stat, shown in its AD row"),
                    ELDDevTestOp::AllocatePoints, Prog->UnspentPoints, DevStat);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [&] { const FLDProgressionView* View = PC.ReadOwnProgression();
                    const FLDCombatAttributeRowView* AD = OwnAttribute(PC, ELDProjectedAttribute::AD);
                    return View && AD && View->UnspentPoints == 0
                        && LDDemoGrowthScenario::GrowthProbePointsOf(*View, DevStat) == int32(Dev.Sent.Amount)
                        && ExactP2(AD->Choices) > 0 && ExactP2(AD->Final) > Dev.ADBeforeP2; }));
        case 9:
            if (!Dev.bSent)
            {
                return SendDev(Now, PC, TEXT("A DEV allocation beyond the unspent points is refused as NotEnoughPoints"),
                    ELDDevTestOp::AllocatePoints, 1, DevStat);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Rejected, ELDGrowthReason::NotEnoughPoints,
                [&] { const FLDProgressionView* View = PC.ReadOwnProgression(); return View && View->UnspentPoints == 0; }));
        case 10:
            if (!Dev.bSent)
            {
                const FLDCombatAttributeRowView* AD = OwnAttribute(PC, ELDProjectedAttribute::AD);
                if (!Check(AD && ExactP2(AD->Final) > 0 && AD->Dev.Numerator == 0, TEXT("DevTest"),
                    TEXT("The owner's combat view has an exact AD row and no DEV term before the DEV attribute step"))) { return Finish(); }
                return SendDev(Now, PC, TEXT("A DEV AD term is installed on the owner's build and shown as its own Dev row"),
                    ELDDevTestOp::SetAttribute, LDDemoGrowthScenario::GrowthProbeDevADP2, NAME_None, 0, uint8(ELDDemoBonusStat::ADFlat));
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [&] { const FLDCombatAttributeRowView* AD = OwnAttribute(PC, ELDProjectedAttribute::AD);
                    return AD && ExactP2(AD->Dev) == LDDemoGrowthScenario::GrowthProbeDevADP2
                        && ExactP2(AD->Final) == Dev.ADBeforeP2 + LDDemoGrowthScenario::GrowthProbeDevADP2; }));
        case 11:
            if (!Dev.bSent)
            {
                return SendDev(Now, PC, TEXT("A DEV clear removes only the DEV term"), ELDDevTestOp::ClearAttributes, 0);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [&] { const FLDCombatAttributeRowView* AD = OwnAttribute(PC, ELDProjectedAttribute::AD);
                    return AD && AD->Dev.Numerator == 0 && ExactP2(AD->Choices) > 0
                        && ExactP2(AD->Final) == Dev.ADBeforeP2 - LDDemoGrowthScenario::GrowthProbeDevADP2; }));
        case 12:
            if (!Dev.bSent)
            {
                return SendDev(Now, PC, TEXT("A DEV reset puts the buyer back to the match start"), ELDDevTestOp::ResetToMatchStart, 0);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [&]
                {
                    const FLDProgressionView* View = PC.ReadOwnProgression();
                    const FLDCombatAttributeRowView* AD = OwnAttribute(PC, ELDProjectedAttribute::AD);
                    const FLDCombatEntryView* Entry = CurrentEntry(PC, ELDCombatEntry::Q);
                    return View && AD && Entry && View->Level == 1 && View->CumulativeXP == Catalog->Get().Progression.XPStart
                        && View->Actives.IsEmpty() && View->PendingBlueprints.IsEmpty()
                        && View->UnspentPoints == Catalog->Get().Parameters.OpeningPoints
                        && LDDemoGrowthScenario::GrowthProbePointsOf(*View, DevStat) == 0
                        && View->QueuedActiveOpportunities == 1 && View->bHasActiveOffer && View->ActiveOffer.OpportunityOrdinal == 1
                        && AD->Choices.Numerator == 0 && AD->Dev.Numerator == 0 && Entry->DefinitionId.IsNone();
                }));
        case 13:
            if (!Dev.bSent)
            {
                return SendDev(Now, PC, TEXT("A match-wide DEV camp refresh from an owning client is refused as NotAuthority"),
                    ELDDevTestOp::RefreshCamps, 0);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::AdmissionRejected, ELDReceiptOutcome::None,
                ELDGrowthReason::NotAuthority, [] { return true; }));
        case 14:
            if (!Dev.bSent)
            {
                // The owning client's own 测试 panel: no Host-only action, a malformed entry sends nothing, and its button
                // reaches the authority through the same owning connection.
                ULDDevTestPanel* Panel = OpenDevPanel(PC, TEXT("The owning client's 测试 panel"));
                if (!Panel) { return Finish(); }
                const bool bHostHidden = !ShownInTree(Panel->WidgetTree->FindWidget(TEXT("DevTestRefreshCamps")))
                    && !ShownInTree(Panel->WidgetTree->FindWidget(TEXT("DevTestOpenCapture")));
                Check(bHostHidden, TEXT("DevTest"), TEXT("An owning client's 测试 panel hides the Host-only match-wide actions"));
                UEditableTextBox* XP = Cast<UEditableTextBox>(Panel->WidgetTree->FindWidget(TEXT("DevTestXPAmount")));
                ULDCommandButton* Grant = Cast<ULDCommandButton>(Panel->WidgetTree->FindWidget(TEXT("DevTestXP")));
                const int32 Before = DevResults.Num();
                if (XP && Grant) { XP->SetText(FText::FromString(TEXT("12a"))); Grant->Execute(); }
                Check(XP && Grant && DevResults.Num() == Before && Panel->FeedbackText().Contains(TEXT("请输入正整数")), TEXT("DevTest"),
                    TEXT("A malformed panel amount is refused locally and sends nothing"), Panel->FeedbackText());
                if (!Passed) { return Finish(); }
                return SendDevByPanel(Now, PC, TEXT("The owning client's 测试 panel button clears its DEV attributes through the server"),
                    ELDDevTestOp::ClearAttributes, 0, TEXT("DevTestClearAttr"));
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [&PC] { const ULDDevTestPanel* Panel = PC.HUDUI ? PC.HUDUI->DevTestPanel.Get() : nullptr;
                    return Panel && Panel->FeedbackText().Contains(TEXT("已生效")); }));
        default:
            if (Passed) { Mark(TEXT("Client1DevDone.json")); }
            return Finish();
        }
    }

    // Host, second match: its own DEV XP grant as a Listen Host, then the match-wide ops once the buyer is done.
    bool TickHostDev(double Now, ALDPlayerController& PC)
    {
        if (DevStep == 0)
        {
            if (!Dev.bSent)
            {
                return SendDevByPanel(Now, PC, TEXT("The Listen Host's own DEV XP grant from its 测试 panel commits through its local owner RPC"),
                    ELDDevTestOp::GrantExperience, LDDemoGrowthScenario::GrowthProbeHostDevXP, TEXT("DevTestXP"), TEXT("DevTestXPAmount"),
                    FString::Printf(TEXT("%lld"), LDDemoGrowthScenario::GrowthProbeHostDevXP));
            }
            const auto XPIs = [&PC](int64 Want) { const FLDProgressionView* View = PC.ReadOwnProgression(); return View && View->CumulativeXP == Want; };
            const EDevWait Wait = AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [&] { return XPIs(Dev.XPBefore + LDDemoGrowthScenario::GrowthProbeHostDevXP); });
            if (Wait == EDevWait::Done)
            {
                // Review evidence of the rendered Host (never a check): its open 测试 panel with the committed XP.
                const ULDDevTestPanel* Panel = PC.HUDUI ? PC.HUDUI->DevTestPanel.Get() : nullptr;
                Check(Panel && Panel->IsPanelOpen() && ShownInTree(Panel->WidgetTree->FindWidget(TEXT("DevTestRefreshCamps"))), TEXT("DevTest"),
                    TEXT("The Listen Host's 测试 panel shows the Host-only match-wide actions"));
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(Output, TEXT("HostDevPanel.png")), true, false);
            }
            return AdvanceDev(Wait);
        }
        if (!Exists(TEXT("Client1DevDone.json"))) { return true; }
        // The match-wide ops, through the Listen Host's own connection, after the buyer's DEV stage.
        const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>();
        if (DevStep == 1)
        {
            if (!Dev.bSent)
            {
                return SendDevByPanel(Now, PC, TEXT("The Listen Host's DEV camp refresh from its 测试 panel is accepted for the whole match"),
                    ELDDevTestOp::RefreshCamps, 0, TEXT("DevTestRefreshCamps"));
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [] { return true; }));
        }
        if (DevStep == 2)
        {
            if (!Public) { return true; }
            if (!Dev.bSent)
            {
                // Field 1 lasts the approved MATCH.FieldSegmentSec after R0; the stage normally ends inside it, where the DEV
                // command skips the timeline to the final field and opens the flagpole. Outside a timed segment it must be
                // refused instead.
                bDevOpenedInDevelopment = Public->PublicMatch.Phase == ELDMatchPhase::Development
                    && Public->PublicMatch.Timeline.Stage == ELDMatchStage::Field;
                return SendDev(Now, PC, bDevOpenedInDevelopment
                    ? TEXT("The Listen Host's DEV early capture opens the circle during development")
                    : TEXT("The Listen Host's DEV early capture after development is refused as StaleContext"), ELDDevTestOp::OpenCapture, 0);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final,
                bDevOpenedInDevelopment ? ELDReceiptOutcome::Applied : ELDReceiptOutcome::Rejected,
                bDevOpenedInDevelopment ? ELDGrowthReason::None : ELDGrowthReason::StaleContext,
                [Public] { return Public->PublicMatch.Phase == ELDMatchPhase::CaptureOpen; }));
        }
        return Finish();
    }

    // After the reports each process exits on the runner's signal: the client on FinishClients, the Host on Finish.
    bool TickAfterReport() const
    {
        if (!Exists(IsHost() ? TEXT("Finish") : TEXT("FinishClients"))) { return true; }
        FPlatformMisc::RequestExitWithStatus(false, 0);
        return false;
    }

    // ---- Client1: kills and build commands, each only through this owning connection. ----

    bool TickClient(double Now, ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        // The buyer dies once, on purpose, after it asked the Host to kill it; until then it never dies. A mid rest retires
        // its field life without a death (Tick holds through it and then tracks the returned life as FieldLife), so only a
        // real death fails here: a dead life with a field respawn wait, or a public death count above zero.
        const bool bDeathStage = Stage == EStage::Dead || Stage == EStage::Respawn;
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        if (!bDeathStage)
        {
            const TCHAR* Id = bSecondMatch ? TEXT("Iso") : TEXT("N03");
            FString Seen;
            const bool bDied = OwnerDied(PC, 0, Seen);
            if (bDied && Stage != EStage::AwaitDeath)
            {
                return Fail(Id, TEXT("The buyer died before it asked the Host to kill it: ") + Seen);
            }
            const bool bOnFieldLife = Body && PC.OwnerState.LifePhase == ELDLifePhase::Alive && PC.OwnerState.Life == FieldLife;
            if (!bOnFieldLife && !bDied)
            {
                // A phase boundary may reach this owner's view a few frames before the public phase does; once the public
                // phase is Rest, Tick holds. Anything longer is a field life lost without a death or a rest.
                if (FieldLifeLostAt == 0) { FieldLifeLostAt = Now; }
                if (Now - FieldLifeLostAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(Id, TEXT("The buyer's field life ended without a death or a rest: ") + Seen);
            }
            FieldLifeLostAt = 0;
        }
        switch (Stage)
        {
        case EStage::ToSite: return TickToSite(Now, PC, *Body, KillSite, EStage::KillSeek, TEXT("N03"));
        case EStage::KillSeek: return TickKillSeek(Now, PC);
        case EStage::KillWait: return TickKillWait(Now, PC);
        case EStage::Window: return TickWindow(Now, PC);
        case EStage::AwaitDeath: return TickAwaitDeath(Now, PC);
        case EStage::Dead: return TickDead(Now, PC);
        case EStage::Respawn: return TickRespawn(Now, PC);
        case EStage::SecondReplay: return TickSecondReplay(Now, PC);
        case EStage::SecondDev: return TickSecondDev(FPlatformTime::Seconds(), PC);
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
        if (!NavigateDuelSite(PC, Site, Id)) { return Finish(); }
        StageAt = Now;
        return true;
    }

    // The victim must be visible in this owner's own frame and no longer protected; the attack is the ordinary
    // AttackUnit intent on its observer handle (what a right click on the unit sends).
    bool TickKillSeek(double Now, ALDPlayerController& PC)
    {
        // The Host presets every offer of this match before the first reward can draw one.
        if (Kills == 0 && AttackOrders == 0 && !Exists(TEXT("OffersPreset.json")))
        {
            if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("Fixture"), TEXT("The Host never confirmed the buyer's preset offers"));
        }
        const FLDWorldView* Frame = LDDemoGrowthScenario::GrowthProbeFrame(PC);
        const FLDObservedUnitView* Victim = Frame ? LDDemoGrowthScenario::GrowthProbeContender(*Frame, HostId) : nullptr;
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        if (!Victim || LDDemoGrowthScenario::GrowthProbeProtected(*Victim) || !ProgView)
        {
            if (Now - StageAt <= RespawnSecondsNow(PC) + LDDemoGrowthScenario::GrowthProbeReturnSeconds) { return true; }
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
            KillBefore = *ProgView;
            KillStartedAt = Now;
        }
        // #198: the kill that reaches the first offer level waits for a segment with room for its attack order and the combat
        // window it opens (WindowKillNeedMs); the wait does not run the seek budget, and Tick holds through the rest.
        if (IsWindowKill())
        {
            const int64 Left = FieldLeftMs(PC);
            if (Left < WindowKillNeedMs())
            {
                NoteFieldRoomWait(TEXT("window-kill"), Left, WindowKillNeedMs());
                StageAt = Now;
                return true;
            }
        }
        bFieldRoomWaiting = false;
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
        const FLDOrderReceipt& Latest = PC.Navigation->LatestReceipt();
        const bool bOrderEnded = Latest.Identity == AttackOrder && (Latest.Status == ELDOrderStatus::Rejected
            || Latest.Status == ELDOrderStatus::Cancelled || Latest.Status == ELDOrderStatus::Completed);
        const bool bRewarded = ProgView && ProgView->ProgressionRevision != KillBefore.ProgressionRevision;
        if (bRewarded && bOrderEnded) { return VerifyKill(Now, PC, *ProgView, Latest); }
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

    bool VerifyKill(double Now, const ALDPlayerController& PC, const FLDProgressionView& ProgView, const FLDOrderReceipt& Ended)
    {
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        uint8 ExpectedLevel = 0;
        const ELDGrowthReason LevelReason = LDProgression::LevelForXP(Defs.Progression, KillBefore.CumulativeXP + KillXP, ExpectedLevel);
        int64 EarnedPoints = 0;
        // The 18 common bases outnumber every component a match can grant, so no level-up of this probe pays the exhausted-pool points.
        const ELDGrowthReason PointsReason = LevelReason != ELDGrowthReason::None ? LevelReason
            : LDProgression::EarnedPoints(Defs, ExpectedLevel, KillBefore.CumulativeXP + KillXP, 0, EarnedPoints);
        int64 Allocated = 0;
        for (const FLDStatPointsView& Row : ProgView.StatPoints) { Allocated += Row.Points; }
        const uint8 ExpectedOpportunities = LevelReason == ELDGrowthReason::None
            ? LDProgression::ActiveOpportunitiesAt(Defs.Progression, ExpectedLevel) : 0;
        const FString Detail = FString::Printf(TEXT("kill %d of %d, victim L%u: xp %lld->%lld (+%lld) earned points %lld; %s"),
            Kills + 1, KillsPlanned, uint32(VictimLevel), KillBefore.CumulativeXP, ProgView.CumulativeXP, KillXP, EarnedPoints,
            *LDDemoGrowthScenario::GrowthProbeProgressionText(ProgView));
        Check(Ended.Status == ELDOrderStatus::Completed && Ended.Failure == ELDOrderFailure::TargetDied, TEXT("N03"),
            TEXT("The owning client's own attack order ended in the victim's death"), Detail);
        Check(ProgView.CumulativeXP == KillBefore.CumulativeXP + KillXP, TEXT("N03"),
            TEXT("One kill pays exactly the approved XP bounty of the frozen victim level in one owner publication"), Detail);
        Check(PointsReason == ELDGrowthReason::None && ProgView.Level == ExpectedLevel
            && ProgView.ActiveOpportunitiesEarned == ExpectedOpportunities && ProgView.UnspentPoints == EarnedPoints - Allocated,
            TEXT("A06"), TEXT("Level, points and active opportunities follow the approved thresholds: the opening points plus the "
                "points of every level crossed, and one opportunity per offer level reached"), Detail);
        const TSharedRef<FJsonObject> KillRow = MakeShared<FJsonObject>();
        KillRow->SetNumberField(TEXT("index"), Kills + 1);
        KillRow->SetNumberField(TEXT("victim_level"), VictimLevel);
        KillRow->SetNumberField(TEXT("xp_before"), double(KillBefore.CumulativeXP));
        KillRow->SetNumberField(TEXT("xp_after"), double(ProgView.CumulativeXP));
        KillRow->SetNumberField(TEXT("level_after"), ProgView.Level);
        KillRow->SetNumberField(TEXT("queued_after"), ProgView.QueuedActiveOpportunities);
        KillRow->SetNumberField(TEXT("unspent_after"), double(ProgView.UnspentPoints));
        KillRow->SetNumberField(TEXT("attack_orders"), AttackOrders);
        KillRow->SetNumberField(TEXT("seconds"), Now - KillStartedAt);
        if (const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>())
        {
            KillRow->SetNumberField(TEXT("field_segment"), Public->PublicMatch.Timeline.FieldSegment);
        }
        KillRows.Add(MakeShared<FJsonValueObject>(KillRow));
        ++Kills;
        AttackOrders = 0;
        if (!Passed) { return Finish(); }
        // The kill that reaches the first offer level left the buyer's own combat window running; the victim stays
        // dead for the approved respawn delay, so no reward races the window stage.
        if (!bWindowDone && int32(ProgView.Level) >= Defs.Progression.ActiveOfferLevels[0])
        {
            Build = FBuildCall();
            BuildStep = 0;
            SetStage(EStage::Window);
            return true;
        }
        return ContinueKills(PC);
    }

    bool ContinueKills(const ALDPlayerController& PC)
    {
        if (Kills < KillsPlanned)
        {
            SetStage(EStage::KillSeek);
            return true;
        }
        const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>();
        const FLDProgressionView* View = PC.ReadOwnProgression();
        Check(Kills >= 1 && bWindowDone, TEXT("A01"),
            TEXT("From zero: real kills, the offers, the learns and the allocations came only through the owning connection, with no grant or teleport"),
            FString::Printf(TEXT("kills=%d planned=%d"), Kills, KillsPlanned));
        Check(Public && (Public->DemoLobby.Phase == ELDMatchPhase::Development || Public->DemoLobby.Phase == ELDMatchPhase::CaptureOpen),
            TEXT("A01"), TEXT("The match stayed open through the whole scenario"));
        Check(View && View->QueuedActiveOpportunities == 1 && LDDemoGrowthScenario::GrowthProbeIsPreset(*View)
            && View->ActiveOffer.OpportunityOrdinal == LDDemoGrowthScenario::GrowthProbePlannedOffers, TEXT("Fixture"),
            TEXT("The last offer level's head offer is exactly the Host's last preset, kept until the buyer chooses"),
            View ? LDDemoGrowthScenario::GrowthProbeProgressionText(*View) : FString(TEXT("no progression view")));
        if (!Passed || !Mark(TEXT("KillMe.json"))) { return Finish(); }
        SetStage(EStage::AwaitDeath);
        return true;
    }

    // #222: the kill that reached the first offer level left the buyer in combat (its own basic attacks and hits; Combat's
    // combat window, BuildSafeAtMs, still runs). Combat no longer blocks the build: the offer's choice is learned into the
    // empty W slot at once and every unspent point is allocated in one commit while the window still runs.
    bool TickWindow(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        int64 NowMs = 0;
        if (!View || !AuthorityNowMs(PC, NowMs))
        {
            return Fail(TEXT("A15"), TEXT("The owner progression view or the authority clock vanished"));
        }
        const FName Option = LDDemoGrowthScenario::GrowthProbePlannedOptions(1)[0];
        const FString Detail = LDDemoGrowthScenario::GrowthProbeProgressionText(*View) + FString::Printf(TEXT(" now=%lld"), NowMs);
        // Combat's own window of the current life (the HUD's combat fold reads the same field).
        const int64 CombatUntilMs = PC.CombatView.Generation == PC.OwnerState.Match && PC.CombatView.Life == PC.OwnerState.Life
            ? PC.CombatView.BuildSafeAtMs : 0;
        // Every in-combat probe leaves with a clear margin before the window's end, so the server commits it in combat.
        const auto SendInCombat = [this, &PC, CombatUntilMs, NowMs](const FString& Step, const FLDBuildCommand& Command)
        {
            return Check(CombatUntilMs - NowMs >= LDDemoGrowthScenario::GrowthProbeUnsafeMarginMs, TEXT("A15"),
                Step + TEXT(": the combat window still runs when the probe is sent"),
                FString::Printf(TEXT("now=%lld combat_until=%lld"), NowMs, CombatUntilMs)) && SendBuild(PC, Step, Command);
        };
        if (BuildStep == 0)
        {
            Check(LDDemoGrowthScenario::GrowthProbeIsPreset(*View) && View->ActiveOffer.OpportunityOrdinal == 2
                && View->QueuedActiveOpportunities == 1 && View->UnspentPoints > 0, TEXT("Fixture"),
                TEXT("The first offer level's head offer is exactly the Host's second preset, with new points unspent"), Detail);
            // #198: the kill waited for field room (WindowKillNeedMs), so the combat window ends before the next rest begins.
            const int64 SegmentEndsAtMs = FieldEndsAtMs(PC);
            Check(SegmentEndsAtMs >= 0 && CombatUntilMs < SegmentEndsAtMs, TEXT("A15"),
                TEXT("The combat window the kill opened ends inside this field segment, so every in-combat probe is judged in the field"),
                FString::Printf(TEXT("%s segment_ends_at=%lld"), *Detail, SegmentEndsAtMs));
            if (!Passed) { return Finish(); }
            WindowRow = MakeShared<FJsonObject>();
            WindowRow->SetNumberField(TEXT("level"), View->Level);
            WindowRow->SetNumberField(TEXT("combat_until_ms"), double(CombatUntilMs));
            WindowRow->SetNumberField(TEXT("segment_ends_at_ms"), double(SegmentEndsAtMs));
            WindowRow->SetNumberField(TEXT("unspent_before"), double(View->UnspentPoints));
            BuildStep = 1;
            return SendInCombat(TEXT("choose-in-combat"), ChoiceCommand(*View, Option)) ? true : Finish();
        }
        const FLDBuildReceipt* Final = nullptr;
        FString Why;
        const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitBuild(Now, PC, Final, Why);
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Failed)
        {
            return Fail(TEXT("A15"), Build.Step + TEXT(" did not complete: ") + Why);
        }
        if (BuildStep == 1)
        {
            const FLDCombatEntryView* W = CurrentEntry(PC, ELDCombatEntry::W);
            if ((!W || W->DefinitionId != Option) && Now - Build.SentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            const FLDActiveSlotView* Learned = LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 1);
            Check(Final->Header.Outcome == ELDReceiptOutcome::Applied && Final->Kind == ELDBuildCommandKind::ActiveChoice
                && Final->DefinitionId == Option && Final->OpportunityOrdinal == 2
                && Final->ChoiceResult == ELDBuildChoiceResult::Learned && Final->LearnedSlot == 1
                && LDGrowthWire::Reason(Final->StoredReason) == ELDGrowthReason::None && !Final->bHasNextOffer, TEXT("A15"),
                TEXT("A choice in combat applies once and learns the active into the empty W slot (#222)"), Detail);
            Check(Learned && Learned->DefinitionId == Option && View->PendingBlueprints.IsEmpty() && W && W->DefinitionId == Option
                && View->QueuedActiveOpportunities == 0 && !View->bHasActiveOffer
                && View->ActiveOpportunitiesConsumed == Build.Before.ActiveOpportunitiesConsumed + 1, TEXT("A12"),
                TEXT("The opportunity is consumed, no further offer is drawn, and Combat installs the active in W"), Detail);
            WindowRow->SetNumberField(TEXT("learned_slot"), Final->LearnedSlot);
            if (!Passed) { return Finish(); }
            BuildStep = 2;
            return SendInCombat(TEXT("allocate-in-combat"), PointsCommand(View->UnspentPoints)) ? true : Finish();
        }
        if (!AllocationShown(PC, *Final) && Now - Build.SentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
        Check(AllocationShown(PC, *Final) && View->UnspentPoints == 0, TEXT("A16"),
            TEXT("In combat every unspent point is allocated in one commit with the larger maximum HP (#222)"),
            FString::Printf(TEXT("%s max hp %lld -> %lld"), *Detail, Build.MaxHPBeforeP2, PC.CombatView.MaxHPP2));
        WindowRow->SetNumberField(TEXT("points_allocated"), double(Build.Before.UnspentPoints));
        WindowRow->SetNumberField(TEXT("max_hp_p2_before"), double(Build.MaxHPBeforeP2));
        WindowRow->SetNumberField(TEXT("max_hp_p2_after"), double(PC.CombatView.MaxHPP2));
        if (!Passed) { return Finish(); }
        bWindowDone = true;
        Build = FBuildCall();
        BuildStep = 0;
        return ContinueKills(PC);
    }

    // The Host's own attack kills the buyer: the dead owner keeps its growth, has no body and waits for its respawn.
    bool TickAwaitDeath(double Now, ALDPlayerController& PC)
    {
        const FLDCombatOwnerView& Own = PC.CombatView;
        // The Host's kill ends the buyer's current field life with a field respawn wait; a rest retirement sets none, and
        // Tick holds through every rest, after which FieldLife is the returned life.
        const bool bDead = PC.OwnerState.LifePhase == ELDLifePhase::Dead && PC.OwnerState.Life == FieldLife
            && PC.OwnerState.RespawnAtMs > 0;
        const bool bViewDead = Own.Generation == PC.OwnerState.Match && Own.Life == PC.OwnerState.Life && !Own.bHasBody;
        if (!bDead || !bViewDead)
        {
            // The Host may first respawn and walk back before its grant and attack.
            const bool bWaiting = (PC.OwnerState.LifePhase == ELDLifePhase::Alive || bDead) && PC.OwnerState.Life == FieldLife;
            // The Host attacks only while this segment has room for the kill, the dead stage and the respawn
            // (HostKillNeedMs); until then this budget does not run.
            const int64 Left = FieldLeftMs(PC);
            if (bWaiting && Left >= 0 && Left < HostKillNeedMs(PC))
            {
                StageAt = Now;
                return true;
            }
            if (bWaiting && Now - StageAt <= RespawnSecondsNow(PC) + LDDemoGrowthScenario::GrowthProbeReturnSeconds
                + LDDemoGrowthScenario::GrowthProbeKillSeconds)
            {
                return true;
            }
            return Fail(TEXT("A14"), FString::Printf(TEXT("The Host did not kill the buyer in time (phase %s, life %u, field life %u)"),
                *StaticEnum<ELDLifePhase>()->GetNameStringByValue(int64(PC.OwnerState.LifePhase)), PC.OwnerState.Life, FieldLife));
        }
        DeathLife = PC.OwnerState.Life;
        DeathRespawnAtMs = PC.OwnerState.RespawnAtMs;
        DeadMaxHPP2 = Own.MaxHPP2;
        Check(PC.OwnerState.Hp == 0 && Own.HPP2 == 0 && DeathRespawnAtMs > 0 && DeadMaxHPP2 > 0, TEXT("A14"),
            TEXT("The Host's attack killed the buyer: dead, no body, zero HP, a respawn time set and the next life's build projected"),
            FString::Printf(TEXT("life %u respawn at %lld hp %.0f, view hp=%lld max=%lld"), DeathLife, DeathRespawnAtMs, PC.OwnerState.Hp,
                Own.HPP2, DeadMaxHPP2));
        // #198: the respawn wait and the respawn check fit in this segment, so no rest ends the wait or judges a dead step.
        const int64 SegmentEndsAtMs = FieldEndsAtMs(PC);
        Check(SegmentEndsAtMs >= 0 && (SegmentEndsAtMs == MAX_int64 || DeathRespawnAtMs
            + int64(LDDemoGrowthScenario::GrowthProbeRequestSeconds * 1000.0) <= SegmentEndsAtMs), TEXT("A14"),
            TEXT("The Host's kill leaves the buyer's whole respawn wait and its respawn check inside this field segment"),
            FString::Printf(TEXT("respawn at %lld, segment ends at %lld"), DeathRespawnAtMs, SegmentEndsAtMs));
        if (!Passed) { return Finish(); }
        Build = FBuildCall();
        BuildStep = 0;
        SetStage(EStage::Dead);
        return true;
    }

    // While dead (#222): the last offer's choice is learned into the empty E slot at once and every unspent point is
    // allocated. The owner stays dead on the same life with the same respawn time and no HP, while the projection of its
    // next life grows by the points.
    bool TickDead(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        const FName Option = LDDemoGrowthScenario::GrowthProbePlannedOptions(LDDemoGrowthScenario::GrowthProbePlannedOffers - 1)[0];
        if (!View) { return Fail(TEXT("A14"), TEXT("The dead owner's progression view vanished")); }
        const FString Detail = LDDemoGrowthScenario::GrowthProbeProgressionText(*View);
        if (BuildStep == 0)
        {
            if (View->LifePhase != ELDLifePhase::Dead || !View->bHasActiveOffer)
            {
                if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(TEXT("A14"), TEXT("The dead owner never saw its head offer: ") + Detail);
            }
            BuildStep = 1;
            return SendBuild(PC, TEXT("choose-while-dead"), ChoiceCommand(*View, Option)) ? true : Finish();
        }
        const FLDBuildReceipt* Final = nullptr;
        FString Why;
        if (Build.QuietUntil == 0)
        {
            const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitBuild(Now, PC, Final, Why);
            if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
            if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Failed)
            {
                return Fail(TEXT("A14"), Build.Step + TEXT(" did not complete: ") + Why);
            }
        }
        if (BuildStep == 1)
        {
            Check(Build.Sent.ExpectedLifePhase == ELDLifePhase::Dead && Build.Sent.Life == DeathLife
                && Final->Header.Outcome == ELDReceiptOutcome::Applied && Final->Header.OriginalLifePhase == ELDLifePhase::Dead
                && Final->Header.OriginalLife == DeathLife && Final->DefinitionId == Option
                && Final->ChoiceResult == ELDBuildChoiceResult::Learned && Final->LearnedSlot == 2
                && LDGrowthWire::Reason(Final->StoredReason) == ELDGrowthReason::None, TEXT("A14"),
                TEXT("A choice confirmed while dead is admitted for the Dead phase of the current life and learned into the empty E (#222)"),
                Detail);
            const FLDActiveSlotView* Learned = LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 2);
            Check(Learned && Learned->DefinitionId == Option && View->PendingBlueprints.IsEmpty() && View->Actives.Num() == 3
                && View->QueuedActiveOpportunities == 0, TEXT("A14"),
                TEXT("While dead the chosen active takes E in the persistent build, so Q/W/E are full"), Detail);
            if (!Passed) { return Finish(); }
            if (!Check(View->UnspentPoints > 0, TEXT("A14"), TEXT("The dead owner has unspent points to allocate"), Detail)) { return Finish(); }
            BuildStep = 3;
            return SendBuild(PC, TEXT("allocate-while-dead"), PointsCommand(View->UnspentPoints)) ? true : Finish();
        }
        if (Build.QuietUntil == 0)
        {
            Check(Final->Header.Outcome == ELDReceiptOutcome::Applied && Final->Header.OriginalLifePhase == ELDLifePhase::Dead
                && View->UnspentPoints == 0, TEXT("A14"), TEXT("Points allocated while dead apply to the persistent build"), Detail);
            if (!Passed) { return Finish(); }
            // An allocation that revived, healed or moved the respawn would show within this quiet period.
            Build.QuietUntil = Now + LDDemoGrowthScenario::GrowthProbeQuietSeconds;
            return true;
        }
        const FLDCombatOwnerView& Own = PC.CombatView;
        const bool bGrown = Own.Generation == PC.OwnerState.Match && Own.Life == DeathLife && Own.MaxHPP2 > DeadMaxHPP2;
        if (Now < Build.QuietUntil || (!bGrown && Now - Build.SentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds))
        {
            return true;
        }
        GrownMaxHPP2 = Own.MaxHPP2;
        Check(PC.OwnerState.LifePhase == ELDLifePhase::Dead && PC.OwnerState.Life == DeathLife && PC.OwnerState.RespawnAtMs == DeathRespawnAtMs
            && PC.OwnerState.Hp == 0 && !Own.bHasBody && Own.HPP2 == 0 && bGrown, TEXT("A14"),
            TEXT("Allocating while dead changes only the build: no early respawn, the same respawn time, no heal, a larger next-life maximum"),
            FString::Printf(TEXT("phase %s life %u respawn at %lld (was %lld) hp %.0f; view body=%d hp=%lld max %lld -> %lld"),
                *StaticEnum<ELDLifePhase>()->GetNameStringByValue(int64(PC.OwnerState.LifePhase)), PC.OwnerState.Life,
                PC.OwnerState.RespawnAtMs, DeathRespawnAtMs, PC.OwnerState.Hp, Own.bHasBody ? 1 : 0, Own.HPP2, DeadMaxHPP2, GrownMaxHPP2));
        if (!Passed) { return Finish(); }
        DeathRow = MakeShared<FJsonObject>();
        DeathRow->SetNumberField(TEXT("life"), DeathLife);
        DeathRow->SetNumberField(TEXT("respawn_at_ms"), double(DeathRespawnAtMs));
        if (const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>())
        {
            DeathRow->SetNumberField(TEXT("field_segment"), Public->PublicMatch.Timeline.FieldSegment);
        }
        DeathRow->SetStringField(TEXT("learned_while_dead"), Option.ToString());
        DeathRow->SetNumberField(TEXT("points_allocated"), double(Build.Before.UnspentPoints));
        DeathRow->SetNumberField(TEXT("max_hp_p2_before"), double(DeadMaxHPP2));
        DeathRow->SetNumberField(TEXT("max_hp_p2_after"), double(GrownMaxHPP2));
        Build = FBuildCall();
        BuildStep = 0;
        SetStage(EStage::Respawn);
        return true;
    }

    // The normal respawn after the approved delay: a new life with full HP and MP at the maximum grown while dead, and the
    // active learned while dead already on E (#222: the new life follows the build changed while dead).
    bool TickRespawn(double Now, ALDPlayerController& PC)
    {
        const FLDCombatOwnerView& Own = PC.CombatView;
        const FName Option = LDDemoGrowthScenario::GrowthProbePlannedOptions(LDDemoGrowthScenario::GrowthProbePlannedOffers - 1)[0];
        if (BuildStep == 0)
        {
            const bool bAlive = PC.OwnerState.LifePhase == ELDLifePhase::Alive && PC.OwnerState.Life == DeathLife + 1
                && Cast<ALDCharacter>(PC.GetPawn()) && Own.Generation == PC.OwnerState.Match && Own.Life == PC.OwnerState.Life && Own.bHasBody;
            const FLDProgressionView* View = PC.ReadOwnProgression();
            const ALDGameState* Public = PC.GetWorld()->GetGameState<ALDGameState>();
            const int64 Deaths = OwnDeaths(PC);
            int64 NowMs = 0;
            if (!bAlive || !View || View->LifePhase != ELDLifePhase::Alive || !AuthorityNowMs(PC, NowMs) || !Public || Deaths < 1)
            {
                if (Now - StageAt <= RespawnSecondsNow(PC) + LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(TEXT("A14"), FString::Printf(TEXT("The buyer did not respawn after the approved delay, or its death never "
                    "reached the public kill feed (life %u, public deaths %lld)"), PC.OwnerState.Life, Deaths));
            }
            // The only real death of the buyer this match is the Host's kill: the public count never moved for a rest.
            const FLDKillNotice* Notice = nullptr;
            for (const FLDKillNotice& Row : Public->KillFeed.Notices)
            {
                if (Row.Victim == ClientId) { Notice = &Row; }
            }
            Check(Deaths == 1 && Notice && Notice->Killer == HostId && Notice->Cause == ELDKillCause::Contender, TEXT("A14"),
                TEXT("The public kill feed counts exactly one death of the buyer this match, dealt by the Host; no rest retirement counted"),
                FString::Printf(TEXT("deaths=%lld killer=%s cause=%u"), Deaths,
                    Notice ? *LDDemoGrowthScenario::GrowthProbeGuid(Notice->Killer) : TEXT("none"), Notice ? uint32(Notice->Cause) : 0u));
            Check(Own.MaxHPP2 == GrownMaxHPP2 && Own.HPP2 == Own.MaxHPP2 && Own.MaxMPP2 > 0 && Own.MPP2 == Own.MaxMPP2, TEXT("A14"),
                TEXT("The normal respawn restores full HP and MP at the maximum grown while dead"),
                FString::Printf(TEXT("life %u hp %lld/%lld mp %lld/%lld"), PC.OwnerState.Life, Own.HPP2, Own.MaxHPP2, Own.MPP2, Own.MaxMPP2));
            const FLDCombatEntryView* E = CurrentEntry(PC, ELDCombatEntry::E);
            const FLDActiveSlotView* Learned = LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 2);
            Check(Learned && Learned->DefinitionId == Option && View->Actives.Num() == 3 && View->PendingBlueprints.IsEmpty()
                && E && E->DefinitionId == Option, TEXT("A12"),
                TEXT("The new life follows the build changed while dead: the active learned while dead is ready on E, so Q/W/E are full"),
                FString::Printf(TEXT("now=%lld %s"), NowMs, *LDDemoGrowthScenario::GrowthProbeProgressionText(*View)));
            if (!Passed) { return Finish(); }
            FieldLife = PC.OwnerState.Life;
            DeathRow->SetNumberField(TEXT("respawned_life"), PC.OwnerState.Life);
            DeathRow->SetNumberField(TEXT("respawn_hp_p2"), double(Own.HPP2));
            DeathRow->SetNumberField(TEXT("learned_slot"), 2);
        }
        else
        {
            return Fail(TEXT("A12"), TEXT("The respawn check runs once"));
        }
        // The first match is done and dirty; the Host ends it and the second match must start from clean owner state.
        RecordFirstMatch(PC);
        if (!Mark(TEXT("Client1Match1Done.json"))) { return Finish(); }
        SetStage(EStage::AwaitLobby);
        return true;
    }

    // Client1, second match: the first match's exact opening choice and a query for its receipt, through this connection.
    bool TickSecondReplay(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        if (!ProgView) { return Fail(TEXT("Iso"), TEXT("The second match's owner views became unavailable")); }
        if (!bReplaySent)
        {
            ReplayProgBefore = *ProgView;
            ReplaySubmitMark = Submits.Num();
            ReplayResultMark = BuildResults.Num();
            PC.ServerGrowthBuild(OpeningSent);
            FLDReceiptQuery Query;
            Query.Generation = FirstGeneration;
            Query.Family = ELDGrowthFamily::Build;
            Query.RequestId = OpeningSent.RequestId;
            PC.QueryGrowthReceipt(Query);
            bReplaySent = true;
            ReplaySentAt = Now;
            ReplayQuietUntil = 0;
            return true;
        }
        const FLDSubmitResult* BuildAnswer = FindSubmit(FirstGeneration, OpeningSent.RequestId, ReplaySubmitMark);
        const FLDBuildReceiptQueryResult* QueryAnswer = FindBuildAnswer(FirstGeneration, OpeningSent.RequestId, ReplayResultMark);
        if (!BuildAnswer || !QueryAnswer)
        {
            if (Now - ReplaySentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("Iso"), FString::Printf(TEXT("The stale first-match requests were not all answered (build=%d query=%d)"),
                BuildAnswer ? 1 : 0, QueryAnswer ? 1 : 0));
        }
        // A stale request that executed anyway would publish within this quiet period.
        if (ReplayQuietUntil == 0) { ReplayQuietUntil = Now + LDDemoGrowthScenario::GrowthProbeQuietSeconds; }
        if (Now < ReplayQuietUntil) { return true; }
        Check(BuildAnswer->Status == ELDAdmissionStatus::AdmissionRejected && BuildAnswer->Generation == FirstGeneration
            && LDGrowthWire::Reason(BuildAnswer->Reason) == ELDGrowthReason::OldMatch && !BuildAnswer->bSequenceReserved
            && !BuildAnswer->bHasBuildReceipt, TEXT("Iso"),
            TEXT("The first match's exact opening choice is refused as OldMatch at admission in the second match"),
            FString::Printf(TEXT("generation=%u request=%llu reason=%s"), BuildAnswer->Generation, BuildAnswer->RequestId,
                *LDDemoGrowthScenario::GrowthProbeReason(BuildAnswer->Reason)));
        Check(QueryAnswer->Lookup == ELDReceiptLookup::InvalidContext && !QueryAnswer->bHasReceipt && !QueryAnswer->bHasPending, TEXT("Iso"),
            TEXT("A receipt query for the first match's choice finds no receipt in the second match (InvalidContext)"));
        const FLDProgressionView* ProgAfter = PC.ReadOwnProgression();
        Check(ProgAfter && LDDemoGrowthScenario::GrowthProbeSameOwner(ReplayProgBefore, *ProgAfter)
            && PC.PeekGrowthRequestId(ELDGrowthFamily::Build) == 1, TEXT("Iso"),
            TEXT("The stale requests change no growth or request sequence of the second match"),
            ProgAfter ? LDDemoGrowthScenario::GrowthProbeProgressionText(*ProgAfter) : FString(TEXT("no progression view")));
        if (!Passed || !Mark(TEXT("Client1Match2Done.json"))) { return Finish(); }
        SetStage(EStage::SecondDev);
        return true;
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
            ? TEXT("DEV-only owned-connection growth scenario, victim and growth-audit role: a rendered, participating Listen Host with -LDDevGrowthFixture that stages the buyer's active offers in the Lobby (its opening offer first), opens each match with exactly one queued opening opportunity and its opening points, keeps its own untaken opening offer past T=0, checks its opponent view of the buyer, never attacks until the buyer asks to die, holds through every mid rest (a rest retirement is no death; T stays frozen) and walks back after each return, then takes its own DEV XP grant, kills the buyer inside one field segment and both scavengers of an authored low camp and sees each exact XP reward in its own owner view and rendered HUD, ends the first match without a winner and checks its own state in a second normal match. Not AI, capture or full-match acceptance.")
            : TEXT("DEV-only owned-connection growth scenario, buyer role: exactly one queued opening opportunity at each match start (never re-granted by a respawn), the opening active learned into Q and the opening points allocated in the opening rest R0, real kill XP, points and offers across the mid rests (each rest retires the field life without a death, and the buyer never dies before it asks to), the level offer learned into W and the points allocated while its own combat window still runs, the next offer learned into E and points allocated while dead and E still learned after the respawn, then clean owner state and refused stale requests in a second normal match. Not camps, AI, capture or full-match acceptance."));
        Report->SetBoolField(TEXT("growth_fixture"), LDDevGrowthFixture::IsSwitchPresent());
        Report->SetStringField(TEXT("catalog_sha256"), CatalogDigest);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetNumberField(TEXT("stage"), int32(Stage));
        Report->SetArrayField(TEXT("checks"), Checks);
        Report->SetArrayField(TEXT("dev_test"), DevRows);
        Report->SetNumberField(TEXT("second_generation"), SecondGeneration);
        Report->SetObjectField(TEXT("match1"), FirstMatchRow.IsValid() ? FirstMatchRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
        Report->SetObjectField(TEXT("opening"), OpeningRow.IsValid() ? OpeningRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
        Report->SetObjectField(TEXT("rest0"), Rest0Row.IsValid() ? Rest0Row : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
        Report->SetObjectField(TEXT("second_opening"),
            SecondOpeningRow.IsValid() ? SecondOpeningRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
        Report->SetArrayField(TEXT("rests"), RestRows);
        Report->SetNumberField(TEXT("field_room_waits"), FieldRoomWaits);
        if (const ALDPlayerController* PC = Controller.Get())
        {
            Report->SetNumberField(TEXT("generation"), PC->OwnerState.Match);
            Report->SetStringField(TEXT("entity"), LDDemoGrowthScenario::GrowthProbeGuid(PC->OwnerState.ContenderId));
            Report->SetNumberField(TEXT("life"), PC->OwnerState.Life);
            if (const FLDProgressionView* ProgView = PC->ReadOwnProgression())
            {
                Report->SetNumberField(TEXT("final_level"), ProgView->Level);
                Report->SetNumberField(TEXT("final_xp"), double(ProgView->CumulativeXP));
                Report->SetNumberField(TEXT("final_queued_opportunities"), ProgView->QueuedActiveOpportunities);
                Report->SetNumberField(TEXT("final_unspent_points"), double(ProgView->UnspentPoints));
            }
        }
        if (IsHost())
        {
            Report->SetStringField(TEXT("start_gate_blocker"), StartBlocker);
            Report->SetNumberField(TEXT("host_grant_xp"), double(HostGrantXP));
            Report->SetBoolField(TEXT("buyer_killed"), bBuyerKilled);
            Report->SetBoolField(TEXT("opponent_view_checked"), bOpponentViewChecked);
            Report->SetObjectField(TEXT("host_killer"), HostKillerRow.IsValid() ? HostKillerRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
            Report->SetNumberField(TEXT("host_dev_xp"), double(DevStep > 0 ? LDDemoGrowthScenario::GrowthProbeHostDevXP : 0));
        }
        else
        {
            Report->SetNumberField(TEXT("kills_planned"), KillsPlanned);
            Report->SetNumberField(TEXT("kills"), Kills);
            Report->SetArrayField(TEXT("kill_rows"), KillRows);
            Report->SetObjectField(TEXT("window"), WindowRow.IsValid() ? WindowRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
            Report->SetObjectField(TEXT("dead_choice"), DeathRow.IsValid() ? DeathRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
            Report->SetNumberField(TEXT("dev_grant_xp"), double(DevGrantXP));
        }
        Written = Save(Report, Role + TEXT(".json"));
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_GROWTH_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
        if (!Passed || !Written)
        {
            FPlatformMisc::RequestExitWithStatus(false, 2);
            return false;
        }
        return true;
    }
};

namespace LDDemoGrowthScenario
{
bool FLDGrowthProbeDetourGeometryTest::RunTest(const FString& Parameters)
{
    const TArray<FGrowthProbeKeepout> Keepouts = {{FVector(500, 0, 0), 80}, {FVector(200, 0, 0), 80}};
    TestEqual(TEXT("The first encountered camp wins, not the first catalog row"),
        GrowthProbeRouteKeepout({FVector(0, 0, 100), FVector(600, 0, 100)}, Keepouts), 1);
    TestEqual(TEXT("A native route segment bypassing the camp bodies is accepted"),
        GrowthProbeRouteKeepout({FVector(0, 100, 0), FVector(600, 100, 0)}, Keepouts), INDEX_NONE);
    TestEqual(TEXT("Route segment order wins over proximity on a later segment"),
        GrowthProbeRouteKeepout({FVector(600, 0, 0), FVector(400, 0, 0), FVector(0, 0, 0)}, Keepouts), 0);
    FLDDemoGrowthProbe Probe;
    Probe.WalkWaypoints.Add(FVector(200, 100, 0));
    Probe.SetStage(FLDDemoGrowthProbe::EStage::ToSite);
    TestEqual(TEXT("Entering a walk retains its newly planned route"), Probe.WalkWaypoints.Num(), 1);
    Probe.SetStage(FLDDemoGrowthProbe::EStage::Window);
    TestTrue(TEXT("A later walk cannot reuse the previous spawn's route"), Probe.WalkWaypoints.IsEmpty());
    Probe.WalkWaypoints.Add(FVector(200, 100, 0));
    Probe.SetStage(FLDDemoGrowthProbe::EStage::ToSite);
    TestEqual(TEXT("The next walk retains its newly planned route"), Probe.WalkWaypoints.Num(), 1);
    return true;
}
}

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
        UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_GROWTH_PROBE Invalid isolated test arguments."));
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
