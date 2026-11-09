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
#include "UI/LDChoicesPanel.h"
#include "Components/RichTextBlock.h"
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

// Owned-connection growth scenario (docs/demo/PROGRESSION_AND_REWARDS.md). The owning client earns XP and chooses only
// through its own connection; the Host plays the victim and audits the growth records afterwards. The probe never
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

FString GrowthProbeProgressionText(const FLDProgressionView& View)
{
    return FString::Printf(TEXT("level=%u xp=%lld earned=%u consumed=%u pending=%u offer=%d progression=%llu build=%llu"),
        uint32(View.Level), View.CumulativeXP, uint32(View.EarnedThrough), uint32(View.ConsumedThrough),
        uint32(View.PendingChoiceCount), View.bHasOffer ? 1 : 0, View.ProgressionRevision, View.BuildRevision);
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

bool GrowthProbeSameChoice(const FLDChoiceReceipt& A, const FLDChoiceReceipt& B)
{
    return GrowthProbeSameHeader(A.Header, B.Header) && A.OfferId == B.OfferId && A.OptionId == B.OptionId
        && A.OpportunityOrdinal == B.OpportunityOrdinal && A.ConsumedThrough == B.ConsumedThrough && A.Kind == B.Kind
        && A.DefinitionId == B.DefinitionId && A.NewRank == B.NewRank && A.bReplaced == B.bReplaced
        && A.ReplacedSlot == B.ReplacedSlot && A.ReplacedSkillId == B.ReplacedSkillId && A.ReplacedRank == B.ReplacedRank
        && A.bHasNextOffer == B.bHasNextOffer && A.NextOfferId == B.NextOfferId
        && A.OpeningActiveId == B.OpeningActiveId;
}

const FLDCombatEntryView* GrowthProbeEntry(const FLDCombatOwnerView& Own, ELDCombatEntry Wanted)
{
    return Own.Entries.FindByPredicate([Wanted](const FLDCombatEntryView& EntryRow) { return EntryRow.Entry == Wanted; });
}

// Owner growth state a stale request must leave untouched: level, XP, opportunities, build and revisions.
bool GrowthProbeSameOwner(const FLDProgressionView& ProgA, const FLDProgressionView& ProgB)
{
    return ProgA.Level == ProgB.Level && ProgA.CumulativeXP == ProgB.CumulativeXP
        && ProgA.ProgressionRevision == ProgB.ProgressionRevision && ProgA.BuildRevision == ProgB.BuildRevision
        && ProgA.EarnedThrough == ProgB.EarnedThrough && ProgA.ConsumedThrough == ProgB.ConsumedThrough
        && ProgA.PendingChoiceCount == ProgB.PendingChoiceCount && ProgA.bHasOffer == ProgB.bHasOffer
        && ProgA.Skills.Num() == ProgB.Skills.Num() && ProgA.AttributeCards.Num() == ProgB.AttributeCards.Num()
        && ProgA.OpeningCard == ProgB.OpeningCard;
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

// The buyer's first seven opportunities in order, staged by the Host in the Lobby through the DEV growth fixture
// (-LDDevGrowthFixture); the first is the match's opening offer. The buyer takes Cards[0] every time: not_mad into Q
// (the A10 choice), not_mad to rank 2 (advanced), snark_bolt into W, pants_down into the free E slot, seethe over Q's
// cooling rank-2 not_mad, not_mad again over W, then attr_canteen while dead. Later opportunities draw ordinary offers.
struct FGrowthProbeOffer
{
    const TCHAR* Cards[3];
};

const FGrowthProbeOffer GrowthProbePlan[] = {
    {{TEXT("active_not_mad"), TEXT("attr_protein"), TEXT("attr_all_nighter")}},
    {{TEXT("active_not_mad"), TEXT("attr_hand_out"), TEXT("attr_big_talk")}},
    {{TEXT("active_snark_bolt"), TEXT("attr_protein"), TEXT("attr_all_nighter")}},
    {{TEXT("active_pants_down"), TEXT("attr_hand_out"), TEXT("attr_big_talk")}},
    {{TEXT("active_seethe"), TEXT("attr_hand_out"), TEXT("attr_big_talk")}},
    {{TEXT("active_not_mad"), TEXT("attr_protein"), TEXT("attr_all_nighter")}},
    {{TEXT("attr_canteen"), TEXT("attr_protein"), TEXT("attr_all_nighter")}},
};

constexpr int32 GrowthProbePlannedOffers = UE_ARRAY_COUNT(GrowthProbePlan);
// DEV test commands (#78): the Host's own DEV XP grant in the second match (probe-only, an arbitrary small value below
// any level threshold, not a product number), and the bound for one DEV answer to settle in the owner view (terminal
// result plus the replicated owner state).
constexpr int64 GrowthProbeHostDevXP = 7;
constexpr double GrowthProbeDevSettleSeconds = 15.0;
// Probe-only DEV attribute term: +12 AD points (P2), an arbitrary visible value, not a product number.
constexpr int64 GrowthProbeDevADP2 = 1200;
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

TArray<FName> GrowthProbePlannedCards(int32 Index)
{
    if (Index == 0)
    {
        return {TEXT("opening_grinder"), TEXT("active_not_mad"), TEXT("opening_athlete"), TEXT("active_pants_down"),
            TEXT("opening_keyboard"), TEXT("active_snark_bolt")};
    }
    const FGrowthProbeOffer& Planned = GrowthProbePlan[Index];
    return {FName(Planned.Cards[0]), FName(Planned.Cards[1]), FName(Planned.Cards[2])};
}

// The head offer is the preset of its opportunity: the same three cards in the same order.
bool GrowthProbeIsPreset(const FLDProgressionView& View)
{
    const int32 Index = int32(View.Offer.OpportunityOrdinal) - 1;
    if (!View.bHasOffer || Index < 0 || Index >= GrowthProbePlannedOffers || View.Offer.Options.Num() != 3)
    {
        return false;
    }
    const TArray<FName> Planned = GrowthProbePlannedCards(Index);
    for (int32 Card = 0; Card < 3; ++Card)
    {
        if (View.Offer.Options[Card].DefinitionId != Planned[Planned.Num() == 6 ? Card * 2 : Card])
        {
            return false;
        }
    }
    return true;
}

ULDChoicesPanel* GrowthProbeChoices(const ALDPlayerController& PC)
{
    const FObjectProperty* Property = PC.HUDUI ? FindFProperty<FObjectProperty>(ULDHUD::StaticClass(), TEXT("ChoicesPanel")) : nullptr;
    return Property ? Cast<ULDChoicesPanel>(Property->GetObjectPropertyValue_InContainer(PC.HUDUI)) : nullptr;
}

const FLDLearnedSkillView* GrowthProbeSkill(const FLDProgressionView& View, FName Id)
{
    return View.Skills.FindByPredicate([Id](const FLDLearnedSkillView& SkillRow) { return SkillRow.DefinitionId == Id; });
}

const FLDLearnedSkillView* GrowthProbeActiveIn(const FLDProgressionView& View, uint8 Slot)
{
    return View.Skills.FindByPredicate([Slot](const FLDLearnedSkillView& SkillRow) { return SkillRow.bActive && SkillRow.Slot == Slot; });
}

FString GrowthProbeSkillsText(const FLDProgressionView& View)
{
    FString Text;
    for (const FLDLearnedSkillView& SkillRow : View.Skills)
    {
        Text += FString::Printf(TEXT("%s%s r%u%s"), Text.IsEmpty() ? TEXT("") : TEXT(", "), *SkillRow.DefinitionId.ToString(),
            uint32(SkillRow.Rank), SkillRow.bActive ? (SkillRow.Slot == 0 ? TEXT(" Q") : SkillRow.Slot == 1 ? TEXT(" W") : TEXT(" E")) : TEXT(""));
    }
    return Text.IsEmpty() ? FString(TEXT("none")) : Text;
}
}

// Global name, matching the other DEV probe classes; it only uses public game mode, controller and growth reads.
class FLDDemoGrowthProbe : public TSharedFromThis<FLDDemoGrowthProbe>
{
public:
    enum class EStage : uint8
    {
        // #153: Countdown runs between the normal start and the first playable instant (T=0) of the first match.
        Join, Lobby, Countdown, Spawned, Victim,
        ToSite, KillSeek, KillWait, Choice, ChoiceQuery,
        // Q/W replacement and a choice while dead on the Host's preset offers: two more planned choices, Q's skill in
        // use, the replacement, the relearn, then the Host kills the buyer and it chooses while dead.
        Advance, UseSkill, Replace, Relearn, AwaitDeath, DeadChoice, Respawn,
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
    double RespawnSeconds = 0;
    FGuid HostId, ClientId;

    // Owning-client notifications in arrival order; listeners only record.
    TArray<FLDSubmitResult> Submits;
    TArray<FLDChoiceReceiptQueryResult> ChoiceResults;
    FDelegateHandle SubmitHandle, ChoiceHandle;

    // Client1 plan: the kill count comes from the catalog's XP curve.
    int32 KillsPlanned = 0, Kills = 0, AttackOrders = 0, NavigateAttempts = 0;
    int64 KillXP = 0;
    uint8 VictimLevel = 0;
    FLDMoveIdentity AttackOrder;
    FLDProgressionView KillBefore;
    double KillStartedAt = 0;
    TArray<TSharedPtr<FJsonValue>> KillRows;

    // Client1: the single choice and its receipt query.
    bool bChoiceDone = false, bChoiceSent = false, bQuerySent = false;
    bool bOpeningRerollSent = false, bOpeningRerollChecked = false, bOpeningFaceChecked = false, bOpeningObserverChecked = false;
    FLDChoiceCommand OpeningReroll;
    FLDProgressionView BeforeOpeningReroll;
    FLDProgressionView ChoiceBefore;
    FLDChoiceOption ChoicePicked;
    FLDChoiceCommand ChoiceSent;
    FLDChoiceReceipt ChoiceFinal;
    int32 ChoiceSubmitMark = 0, ChoiceResultMark = 0;
    uint64 ChoicePublishMark = 0;
    TSharedPtr<FJsonObject> ChoiceRow;

    // Host: the victim's current life.
    uint32 VictimLife = 0;

    // One planned choice in flight after the kills (replacement, relearn, choice while dead) and what it must change.
    struct FChoiceCall
    {
        FString Step;
        FLDChoiceCommand Sent;
        FLDChoiceOption Picked;
        FLDProgressionView Before;
        int32 SubmitMark = 0;
        int32 ResultMark = 0;
        uint64 PublishMark = 0;
        double SentAt = 0;
        double QuietUntil = 0;
        bool bSent = false;
    };
    FChoiceCall Planned;

    // #153 opening countdown of the first match: its T=0, and Client1's refused attack-move, Shift attack-move and Q, then
    // its admitted move (2026-11-07 user decision: movement and choices are allowed, attacks are not; #144 removed trades).
    bool bCountdownSeen = false, bCountdownRefused = false;
    int32 CountdownStep = 0;
    int64 CountdownStartedAtMs = 0;
    FVector CountdownFoot = FVector::ZeroVector;
    FLDMoveIdentity CountdownAttack, CountdownShift, CountdownMove;
    uint32 CountdownCombatMark = 0;
    TSharedPtr<FJsonObject> CountdownRow;

    // Client1: Q/W replacement. The forgotten skill's persistent track and deadline, and the rejected unconfirmed try.
    bool bUnconfirmedDone = false, bSkillSent = false, bSkillCooling = false;
    uint32 SkillReceiptMark = 0;
    FName ForgottenId;
    uint8 ForgottenRank = 0;
    FGuid ForgottenTrack;
    int64 ForgottenReadyAtMs = 0;
    TSharedPtr<FJsonObject> ReplaceRow;

    // Client1: the death the Host deals and the choice confirmed while dead.
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
    bool bPresetsStaged = false;
    FGuid StagedClientId;
    TSharedPtr<FJsonObject> OpeningRow, SecondOpeningRow;

    // New-match isolation. The first match's identities and dirty owner state, then the second match's checks.
    bool bSecondMatch = false, bResultChecked = false, bReplaySent = false;
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
    FName DevCardId;
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
        ChoiceHandle = Subject.OnChoiceReceipt.AddLambda([Weak](const FLDChoiceReceiptQueryResult& Received)
        {
            if (const TSharedPtr<FLDDemoGrowthProbe> Pinned = Weak.Pin()) { Pinned->ChoiceResults.Add(Received); }
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
        Subject->OnChoiceReceipt.Remove(ChoiceHandle);
        Subject->OnDevTestResult.Remove(DevHandle);
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
        if (IsHost() && Exists(TEXT("Client1.json")) && !Exists(TEXT("Client1DevDone.json")))
        {
            return Fail(TEXT("Net"), TEXT("The buyer ended its scenario early; see Client1.json for its failed check"));
        }
        switch (Stage)
        {
        case EStage::Join: return TickJoin(*Current, *PC, Lobby, Driver);
        case EStage::Lobby: return TickLobby(Now, *PC, Lobby);
        case EStage::Countdown: return TickCountdown(Now, *PC, *Public, *Registry);
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
        // #153: the first match's countdown is checked from both roles before anyone acts.
        if (!bSecond && Lobby.Phase == ELDMatchPhase::Countdown)
        {
            SetStage(EStage::Countdown);
            return true;
        }
        if (Lobby.Phase == ELDMatchPhase::Starting || Lobby.Phase == ELDMatchPhase::Countdown) { return true; }
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

    // Host, in the Lobby before Start: the DEV fixture stages the buyer's six offers, its opening offer first. The growth
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
        const TArray<FName> Twice = {TEXT("opening_grinder"), TEXT("active_not_mad"), TEXT("opening_grinder"),
            TEXT("active_pants_down"), TEXT("opening_keyboard"), TEXT("active_snark_bolt")};
        Check(!LDDevGrowthFixture::StageOffer(LobbyWorld, ClientId, Twice, Refusal) && !Refusal.IsEmpty(), TEXT("Fixture"),
            TEXT("The fixture refuses a preset that repeats a card, with a reason"), Refusal);
        for (int32 Index = 0; Index < LDDemoGrowthScenario::GrowthProbePlannedOffers; ++Index)
        {
            const TArray<FName> Cards = LDDemoGrowthScenario::GrowthProbePlannedCards(Index);
            FString QueueError;
            const bool bQueued = LDDevGrowthFixture::StageOffer(LobbyWorld, ClientId, Cards, QueueError);
            if (!Check(bQueued, TEXT("Fixture"), FString::Printf(TEXT("The fixture stages the buyer's offer %d"), Index + 1), QueueError))
            {
                return false;
            }
        }
        return Mark(TEXT("OffersStaged.json"));
    }

    // D10: the owner's first view of a match holds exactly one unconsumed opening opportunity with its kept offer of three
    // distinct cards; the buyer's first-match opening offer is the first offer the Host staged in the Lobby.
    bool CheckOpening(const FLDProgressionView& View, const TCHAR* Id, TSharedPtr<FJsonObject>& Row, bool bStagedPreset)
    {
        const TArray<FLDChoiceOption>& Cards = View.Offer.Options;
        const bool bDistinct = Cards.Num() == 3 && Cards[0].DefinitionId != Cards[1].DefinitionId
            && Cards[0].DefinitionId != Cards[2].DefinitionId && Cards[1].DefinitionId != Cards[2].DefinitionId;
        Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("level"), View.Level);
        Row->SetNumberField(TEXT("earned"), View.EarnedThrough);
        Row->SetNumberField(TEXT("consumed"), View.ConsumedThrough);
        Row->SetNumberField(TEXT("pending"), View.PendingChoiceCount);
        Row->SetNumberField(TEXT("ordinal"), View.bHasOffer ? View.Offer.OpportunityOrdinal : 0);
        Row->SetStringField(TEXT("offer"), LDDemoGrowthScenario::GrowthProbeGuid(View.Offer.OfferId));
        Row->SetStringField(TEXT("cards"), FString::JoinBy(Cards, TEXT(","), [](const FLDChoiceOption& Card) { return Card.DefinitionId.ToString(); }));
        Row->SetBoolField(TEXT("staged_preset"), LDDemoGrowthScenario::GrowthProbeIsPreset(View));
        bool bGood = Check(View.Level == 1 && View.EarnedThrough == 1 && View.ConsumedThrough == 0 && View.PendingChoiceCount == 1
            && View.bHasOffer && View.Offer.OpportunityOrdinal == 1 && View.Offer.OfferId.IsValid() && bDistinct, Id,
            TEXT("The match opens with exactly one pending three-choice: its kept opening offer of three distinct cards"),
            LDDemoGrowthScenario::GrowthProbeProgressionText(View));
        bGood &= Check(View.Skills.IsEmpty() && View.OpeningCard.IsNone()
            && Cards.Num() == 3 && Cards.ContainsByPredicate([](const FLDChoiceOption& Card)
                { return Card.Kind == ELDChoiceOptionKind::Opening; })
            && !FindFProperty<FProperty>(FLDChoiceOption::StaticStruct(), TEXT("HiddenActive")),
            Id, TEXT("The owning view exposes only direction cards; no hidden active assignment or learned skill is present"));
        for (const auto& Card : Cards)
        {
            bGood &= Check(Card.Kind == ELDChoiceOptionKind::Opening && Card.FromRank == 0 && Card.ToRank == 1
                && Catalog->Get().OpeningCards.ContainsByPredicate([&Card](const FLDDemoOpeningCardDefinition& D)
                    { return FName(*D.Id) == Card.DefinitionId; }), Id, TEXT("Every visible opening option is an approved direction"));
        }
        if (bStagedPreset)
        {
            bGood &= Check(LDDemoGrowthScenario::GrowthProbeIsPreset(View), TEXT("Fixture"),
                TEXT("The buyer's opening offer is exactly the first offer the Host staged in the Lobby"), Row->GetStringField(TEXT("cards")));
        }
        return bGood;
    }

    bool TickSpawned(ALDPlayerController& PC, const ULDMapRegistry& Registry, const FLDLobbyView& Lobby)
    {
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        if (!Cast<ALDCharacter>(PC.GetPawn()) || PC.OwnerState.LifePhase != ELDLifePhase::Alive || !PC.OwnerState.BodyRef.IsValid()
            || !PC.CanWorldCommand() || !ProgView)
        {
            return true;
        }
        if (!ResolveRoster(PC, Lobby) || !PrepareSites(Registry)) { return Finish(); }
        if (IsHost() && !bGrowthChecked && !CheckStartGrowth(PC, Lobby)) { return Finish(); }
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        Check(PC.OwnerState.Life == 1 && PC.OwnerState.ProtectedUntilMs == 0, TEXT("Start"),
            TEXT("The first body is an unprotected initial life"));
        Check(ProgView->Level == 1 && ProgView->CumulativeXP == Defs.Progression.XPStart && ProgView->CatalogDigest == Defs.Identity.Root,
            TEXT("Start"), TEXT("Growth starts at level 1 with the approved XP start and the current catalog"),
            LDDemoGrowthScenario::GrowthProbeProgressionText(*ProgView));
        CheckOpening(*ProgView, TEXT("Opening"), OpeningRow, !IsHost());
        if (!Passed) { return Finish(); }
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

    // #153: the first match's opening countdown, seen from both roles, while the opening three-choice is open. Client1, a
    // real owning remote connection, has its attack-move, Shift attack-move and Q refused by the server and its ground
    // move admitted and walked. T=0 then starts development with every match timer counted from it; the untaken choice is kept.
    bool TickCountdown(double Now, ALDPlayerController& PC, const ALDGameState& Public, const ULDMapRegistry& Registry)
    {
        const FLDPublicMatchView& View = Public.PublicMatch;
        const FLDDemoContentParameters& Parameters = Catalog->Get().Parameters;
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        int64 NowMs = 0;
        const bool bClock = PC.ReadDemoAuthorityTimeMs(NowMs);
        if (Now - StageAt > Parameters.CountdownMs / 1000. + LDDemoGrowthScenario::GrowthProbeRequestSeconds)
        {
            return Fail(TEXT("Countdown"), TEXT("The opening countdown did not end with its checks complete"));
        }
        if (!bOpeningFaceChecked && ProgView && View.Phase == ELDMatchPhase::Countdown)
        {
            ULDChoicesPanel* Panel = LDDemoGrowthScenario::GrowthProbeChoices(PC);
            if (Panel && Panel->IsPanelOpen())
            {
                const URichTextBlock* Face = Cast<URichTextBlock>(Panel->WidgetTree->FindWidget(TEXT("ChoiceSentence0")));
                const UButton* Reroll = Cast<UButton>(Panel->WidgetTree->FindWidget(TEXT("ChoicesReroll")));
                if (Face && Face->GetText().ToString().Contains(LDTerms::OpeningHiddenActive))
                {
                    Check(Reroll && !Reroll->GetIsEnabled(), TEXT("Opening"),
                        TEXT("The rendered face keeps the active hidden and disables opening reroll"));
                    bOpeningFaceChecked = true;
                }
            }
        }
        if (!bCountdownSeen)
        {
            if (View.Generation != PC.OwnerState.Match || !Body || PC.OwnerState.LifePhase != ELDLifePhase::Alive || !ProgView || !bClock)
            {
                return true;
            }
            if (View.Phase != ELDMatchPhase::Countdown)
            {
                return Fail(TEXT("Countdown"), TEXT("The match left the opening countdown before this role saw its spawned body"));
            }
            bCountdownSeen = true;
            CountdownStartedAtMs = View.StartedAtMs;
            CountdownFoot = Body->GetActorLocation();
            Check(View.StartedAtMs > NowMs && View.StartedAtMs - NowMs <= Parameters.CountdownMs
                && View.OpenAtMs == View.StartedAtMs + Parameters.DevelopmentMs && View.CircleState == ELDCircleState::NotOpen,
                TEXT("Countdown"), TEXT("The match opens with the approved countdown before T=0; development is due a full phase after T=0"),
                FString::Printf(TEXT("now=%lld started=%lld open=%lld"), NowMs, View.StartedAtMs, View.OpenAtMs));
            Check(ProgView->bHasOffer && ProgView->bCanChoose && ProgView->ConsumedThrough == 0 && ProgView->PendingChoiceCount == 1,
                TEXT("Countdown"), TEXT("The opening three-choice is open and choosable during the countdown"),
                LDDemoGrowthScenario::GrowthProbeProgressionText(*ProgView));
            CheckOpening(*ProgView, TEXT("Opening"), OpeningRow, !IsHost());
            Check(PC.CanWorldCommand(), TEXT("Countdown"), TEXT("Camera and commands stay usable; the server answers each world command"));
            if (IsHost())
            {
                const ALDGameMode* Mode = PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
                Check(Mode && Mode->IsOpeningCountdown(Mode->GetMatchContext()) && Mode->ReadPublicMatch().StartedAtMs == View.StartedAtMs
                    && Mode->GetAirdrop().Schedule.StartedAtMs == View.StartedAtMs
                    && Mode->GetLeaderNotice().Rules.StartedAtMs == View.StartedAtMs
                    && Mode->Camps && Mode->Camps->GetBoss().Rules.StartedAtMs == View.StartedAtMs,
                    TEXT("Countdown"), TEXT("The authority counts the airdrop, the leader notice and the Boss from T=0"));
            }
            if (!Passed) { return Finish(); }
            if (IsHost())
            {
                bCountdownRefused = true;
                return true;
            }
            // Client1 sends everything at once, while T=0 is still well ahead.
            const FLDMapBinding* Capture = Registry.Resolve(FName(*Catalog->Get().World.CaptureId), Registry.Generation);
            if (!Capture)
            {
                return Fail(TEXT("Countdown"), TEXT("The countdown requests could not be built from the public world"));
            }
            PC.Navigation->AttackMove(Capture->Position, Capture->Surface);
            CountdownAttack = PC.Navigation->LocalIdentity();
            CountdownCombatMark = PC.CombatReceipts.Serial();
            PC.ActivateCombatEntry(ELDCombatEntry::Q);
            CountdownStep = 1;
            return true;
        }
        if (!bCountdownRefused)
        {
            const FLDOrderReceipt& Order = PC.Navigation->LatestReceipt();
            const FLDMapBinding* Capture = Registry.Resolve(FName(*Catalog->Get().World.CaptureId), Registry.Generation);
            const auto Refused = [&Order](const FLDMoveIdentity& Sent)
            { return Order.Identity == Sent && Order.Status != ELDOrderStatus::Queued; };
            if (CountdownStep == 1)
            {
                if (!Refused(CountdownAttack)) { return true; }
                Check(Order.Status == ELDOrderStatus::Rejected && Order.Failure == ELDOrderFailure::NotStarted, TEXT("Countdown"),
                    TEXT("The server refuses an attack-move before T=0"),
                    FString::Printf(TEXT("status=%u failure=%u"), uint32(Order.Status), uint32(Order.Failure)));
                PC.Navigation->AttackMove(Capture->Position, Capture->Surface, true);
                CountdownShift = PC.Navigation->LocalIdentity();
                CountdownStep = 2;
                return true;
            }
            if (CountdownStep == 2)
            {
                if (!Refused(CountdownShift)) { return true; }
                // A freshly spawned body may still be settling; a ground order sent before it stands fails its local plan.
                if (Body && Body->GetCharacterMovement() && !Body->GetCharacterMovement()->IsMovingOnGround()
                    && View.Phase == ELDMatchPhase::Countdown) { return true; }
                Check(Order.Status == ELDOrderStatus::Rejected && Order.Failure == ELDOrderFailure::NotStarted, TEXT("Countdown"),
                    TEXT("The server refuses a Shift-queued attack-move before T=0"),
                    FString::Printf(TEXT("status=%u failure=%u"), uint32(Order.Status), uint32(Order.Failure)));
                if (!bClock || NowMs >= CountdownStartedAtMs - 1500 || !Body)
                {
                    return Fail(TEXT("Countdown"), TEXT("T=0 was too close to walk inside the countdown"));
                }
                CountdownFoot = Body->GetActorLocation();
                PC.Navigation->NavigateGround(Capture->Position, Capture->Surface);
                CountdownMove = PC.Navigation->LocalIdentity();
                CountdownStep = 3;
                return true;
            }
            if (CountdownStep == 3)
            {
                if (Order.Identity == CountdownMove && Order.Status == ELDOrderStatus::Rejected)
                {
                    return Fail(TEXT("Countdown"), TEXT("The server refused a ground move during the countdown: failure ")
                        + LexToString(uint32(Order.Failure)));
                }
                const double Walked = Body ? FVector::Dist2D(CountdownFoot, Body->GetActorLocation()) : 0.;
                if (Walked < 100.)
                {
                    if (View.Phase == ELDMatchPhase::Countdown) { return true; }
                    return Fail(TEXT("Countdown"), FString::Printf(TEXT("The countdown ended before the admitted move walked (%.0f cm)"), Walked));
                }
                Check(View.Phase == ELDMatchPhase::Countdown && bClock && NowMs < CountdownStartedAtMs, TEXT("Countdown"),
                    TEXT("A ground move is admitted and walks before T=0"), FString::Printf(TEXT("walked=%.0f cm now=%lld"), Walked, NowMs));
                PC.Navigation->Stop();
                CountdownStep = 4;
                return true;
            }
            const FLDCombatActionReceipt* Action = PC.CombatReceipts.FindLatestSince(CountdownCombatMark,
                [](const FLDCombatActionReceipt& Receipt) { return Receipt.Entry == ELDCombatEntry::Q; });
            if (!Action || Action->Phase != ELDCombatPhase::Rejected) { return true; }
            Check(Action->Reject == ELDCombatReject::NotStarted, TEXT("Countdown"), TEXT("The server refuses Q before T=0"),
                StaticEnum<ELDCombatReject>()->GetNameStringByValue(int64(Action->Reject)));
            if (!Passed) { return Finish(); }
            bCountdownRefused = true;
            return true;
        }
        if (View.Phase == ELDMatchPhase::Countdown) { return true; }
        if (View.Phase != ELDMatchPhase::Development || !ProgView || !Body)
        {
            return Fail(TEXT("Countdown"), TEXT("The countdown did not end in the development phase"));
        }
        Check(View.StartedAtMs == CountdownStartedAtMs && View.ServerNowMs >= View.StartedAtMs
            && View.OpenAtMs == View.StartedAtMs + Parameters.DevelopmentMs, TEXT("Countdown"),
            TEXT("Development starts at T=0 and the flagpole opens a full development phase later"),
            FString::Printf(TEXT("server=%lld started=%lld open=%lld"), View.ServerNowMs, View.StartedAtMs, View.OpenAtMs));
        Check(ProgView->bHasOffer && ProgView->ConsumedThrough == 0 && ProgView->PendingChoiceCount == 1, TEXT("Countdown"),
            TEXT("An opening choice not taken in the countdown is kept, never chosen for the player"),
            LDDemoGrowthScenario::GrowthProbeProgressionText(*ProgView));
        Check(bOpeningFaceChecked, TEXT("Opening"), TEXT("Opening direction cards were rendered before T=0"));
        CountdownRow = MakeShared<FJsonObject>();
        CountdownRow->SetNumberField(TEXT("started_at_ms"), double(View.StartedAtMs));
        CountdownRow->SetNumberField(TEXT("open_at_ms"), double(View.OpenAtMs));
        CountdownRow->SetBoolField(TEXT("refusals_checked"), !IsHost());
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
        RespawnSeconds = double(Defs.Parameters.RespawnMs) / 1000.0;
        const auto* Geometry = Registry.GetGeometry();
        if (!CaptureAnchor || !Geometry || VisionReach <= 0 || CaptureReach <= 0 || AirdropReach <= 0 || DormReach <= 0
            || BodyRadius <= 0 || AttackReach <= 0 || RespawnSeconds <= 0)
        {
            return Check(false, TEXT("Start"), TEXT("The public capture anchor, the world geometry and the approved radii exist"));
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
        // The fewest kills that reach the level whose opportunities (one per level, the opening one included) every planned
        // offer consumes: the first choice, the Q/W replacement and the choice while dead earn no further XP.
        KillsPlanned = 1;
        uint8 PlannedLevel = 0;
        ELDGrowthReason LevelReason = ELDGrowthReason::None;
        while ((LevelReason = LDProgression::LevelForXP(Defs.Progression,
            Defs.Progression.XPStart + int64(KillsPlanned) * Bounty.XP, PlannedLevel)) == ELDGrowthReason::None
            && PlannedLevel < LDDemoGrowthScenario::GrowthProbePlannedOffers && int32(PlannedLevel) < int32(Defs.Progression.MaxLevel))
        {
            ++KillsPlanned;
        }
        return Check(LevelReason == ELDGrowthReason::None && PlannedLevel >= LDDemoGrowthScenario::GrowthProbePlannedOffers, TEXT("N03"),
            TEXT("The planned kills reach a level with an opportunity for every planned offer"),
            FString::Printf(TEXT("kills=%d level=%u offers=%d %s"), KillsPlanned, uint32(PlannedLevel),
                LDDemoGrowthScenario::GrowthProbePlannedOffers, LDGrowthWire::ReasonName(LevelReason)));
    }

    // ---- Host: the victim never attacks until the buyer asks to die; it walks back to its duel site after every respawn. ----

    bool TickVictim(double Now, ALDPlayerController& PC)
    {
        ALDGameMode* Mode = PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
        if (!Mode) { return Fail(TEXT("Net"), TEXT("The Listen Host lost its authoritative game mode")); }
        if (!bOpeningObserverChecked && Mode->Development)
        {
            const FLDWorldView* Frame = LDDemoGrowthScenario::GrowthProbeFrame(PC);
            const FLDObservedUnitView* Seen = Frame ? LDDemoGrowthScenario::GrowthProbeContender(*Frame, ClientId) : nullptr;
            const FLDProgressionView Owner = Mode->Development->ReadProgression(ClientId);
            const FLDProgressionState* Private = Mode->Development->FindProgression(ClientId);
            if (Seen && Owner.ConsumedThrough == 0 && Private && Private->Offer.IsSet())
            {
                bool bHidden = Private->Offer->HiddenActives.Num() == 3 && Owner.Skills.IsEmpty() && Owner.OpeningCard.IsNone()
                    && Seen->Actives.Abilities[0] == 0 && Seen->Actives.Abilities[1] == 0 && Seen->ActionAbility == 0;
                const FString Observed = LDHUD::ObservedUnit(*Seen, TEXT("buyer"), &Catalog->Get(), 0);
                for (const FName Hidden : Private->Offer->HiddenActives)
                {
                    const FString Name = LDHUD::ContentName(&Catalog->Get(), Hidden.ToString());
                    bHidden &= !Observed.Contains(Hidden.ToString()) && !Observed.Contains(Name);
                    for (const FLDChoiceOption& Option : Owner.Offer.Options)
                    {
                        const auto Card = LDHUD::ChoiceCard(Option, Owner, &Catalog->Get(), {});
                        const FString Text = Card.Name + Card.Detail + Card.SentencePlain + Card.AdvancedPlain
                            + FString::Join(Card.Lines, TEXT("\n"));
                        bHidden &= !Text.Contains(Hidden.ToString()) && !Text.Contains(Name);
                    }
                }
                Check(bHidden, TEXT("Opening"),
                    TEXT("Actual owner and opponent views contain no hidden skill ID or Chinese display name before selection"));
                bOpeningObserverChecked = true;
                if (!Passed) { return Finish(); }
            }
        }
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

    // Host, once the buyer has replaced and relearned its skill: its own DEV XP grant first (up to the buyer's level, so the
    // duel is even), then its own attack orders kill the buyer for the choice while dead.
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
            if (!Buyer)
            {
                if (Now - HostKillStartedAt <= LDDemoGrowthScenario::GrowthProbeKillSeconds) { return true; }
                return Fail(TEXT("A14"), TEXT("The buyer left the Host's sight before it died"));
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
        FirstMatchRow->SetNumberField(TEXT("pending_choices"), FirstProgression.PendingChoiceCount);
        FirstMatchRow->SetNumberField(TEXT("earned_choices"), FirstProgression.EarnedThrough);
        // D10: deaths and respawns never re-grant the opening opportunity; only levels add opportunities.
        Check(ProgView && PC.OwnerState.Life > 1 && FirstProgression.EarnedThrough == FirstProgression.Level, TEXT("Opening"),
            TEXT("After dying and respawning the owner holds exactly its opening opportunity plus one per level crossed"),
            FString::Printf(TEXT("life %u: %s"), PC.OwnerState.Life, *LDDemoGrowthScenario::GrowthProbeProgressionText(FirstProgression)));
        FirstMatchRow->SetNumberField(TEXT("skills"), FirstProgression.Skills.Num());
        FirstMatchRow->SetNumberField(TEXT("attribute_cards"), FirstProgression.AttributeCards.Num());
    }

    // Host: end the first match without a winner through the Host's own session command.
    bool EndFirstMatch(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        Check(bOpeningObserverChecked, TEXT("Opening"), TEXT("A real opponent view was checked before direction selection"));
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
            || !PC.OwnerState.BodyRef.IsValid() || !PC.CanWorldCommand() || !ProgView
            || Own.Generation != SecondGeneration || Own.Life != PC.OwnerState.Life || !Own.bHasBody)
        {
            if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeTransitionSeconds) { return true; }
            return Fail(TEXT("Iso"), TEXT("The second match never published a living first body with every own view"));
        }
        if (!ResolveRoster(PC, Lobby)) { return Finish(); }
        CheckSecondMatch(PC, *ProgView);
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
    void CheckSecondMatch(const ALDPlayerController& PC, const FLDProgressionView& Progress)
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
            && Progress.CumulativeXP == Defs.Progression.XPStart && Progress.CatalogDigest == Defs.Identity.Root, TEXT("Iso"),
            TEXT("Level and XP restart at the content's opening values"),
            FString::Printf(TEXT("now %s; first match ended at %s"), *LDDemoGrowthScenario::GrowthProbeProgressionText(Progress),
                *LDDemoGrowthScenario::GrowthProbeProgressionText(FirstProgression)));
        // D10: the new match grants its own single opening opportunity with a newly drawn offer; nothing of the first carries over.
        CheckOpening(Progress, TEXT("Iso"), SecondOpeningRow, false);
        Check(OpeningRow.IsValid() && Progress.Offer.OfferId.IsValid()
            && LDDemoGrowthScenario::GrowthProbeGuid(Progress.Offer.OfferId) != OpeningRow->GetStringField(TEXT("offer"))
            && Progress.Offer.OfferId != FirstProgression.Offer.OfferId, TEXT("Iso"),
            TEXT("The second match's opening offer is newly drawn, not the first match's opening or last head offer"),
            SecondOpeningRow.IsValid() ? SecondOpeningRow->GetStringField(TEXT("cards")) : FString());
        Check(Progress.Skills.IsEmpty() && Progress.AttributeCards.IsEmpty(), TEXT("Iso"),
            TEXT("No learned active or passive skill and no attribute card is carried into the second match"),
            FString::Printf(TEXT("skills %d->%d cards %d->%d"), FirstProgression.Skills.Num(), Progress.Skills.Num(),
                FirstProgression.AttributeCards.Num(), Progress.AttributeCards.Num()));
        // The innate D dash is every contender's source: it is held from the start, but a fresh match issues it a fresh,
        // ready track (no duration issued yet); Q and W start empty, with no learned skill's track carried over.
        const bool bNoSource = !Own.Entries.ContainsByPredicate([&Own](const FLDCombatEntryView& EntryRow)
        {
            if (EntryRow.Entry == ELDCombatEntry::Dash) { return EntryRow.ReadyAtMs > Own.AsOfMs || EntryRow.CooldownMs != 0; }
            return !EntryRow.DefinitionId.IsNone() || EntryRow.IssuedCooldownId.IsValid() || EntryRow.ReadyAtMs > Own.AsOfMs;
        });
        const bool bNoContribution = !Own.Attributes.ContainsByPredicate([](const FLDCombatAttributeRowView& Row)
        {
            return Row.Choices.Numerator != 0 || Row.Passives.Numerator != 0;
        });
        Check(bNoSource && bNoContribution && Own.NextAttackAtMs <= Own.AsOfMs, TEXT("Iso"),
            TEXT("Combat carries no learned source, no issued Q/W cooldown track and no build contribution"),
            FString::Printf(TEXT("entries=%d attributes=%d as_of=%lld"), Own.Entries.Num(), Own.Attributes.Num(), Own.AsOfMs));
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
            bFresh &= Record && Record->CumulativeXP == Defs.Progression.XPStart && Record->ConsumedThrough == 0
                && Record->EarnedThrough == 1 && Record->Records.IsEmpty();
        }
        Check(bFresh, TEXT("Iso"),
            TEXT("The second match opens fresh growth records under a new LedgerMatch: XP start, only the opening opportunity and no reward"),
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
        Check(Growth && Buyer && Growth->DevRecentRewards().IsEmpty() && Buyer->CumulativeXP == Catalog->Get().Progression.XPStart
            && Buyer->ConsumedThrough == 0 && Buyer->Records.IsEmpty(), TEXT("Iso"),
            TEXT("The stale first-match requests committed nothing: the buyer's second-match growth record is untouched"),
            Buyer ? FString::Printf(TEXT("xp=%lld consumed=%u rewards=%d"), Buyer->CumulativeXP, uint32(Buyer->ConsumedThrough),
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

    // The owner's own combat view row of one attribute, or null.
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
        const auto CardCount = [&PC](FName Id) -> int32
        {
            const FLDProgressionView* View = PC.ReadOwnProgression();
            const FLDAttributeCardView* Row = View ? View->AttributeCards.FindByPredicate([Id](const FLDAttributeCardView& Card)
                { return Card.DefinitionId == Id; }) : nullptr;
            return Row ? int32(Row->Count) : 0;
        };
        if (DevCardId.IsNone() && !Catalog->Get().Attributes.IsEmpty())
        {
            DevCardId = FName(*Catalog->Get().Attributes[0].Id);
        }
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
                // The grant stops one XP short of the next level, so the opening opportunity stays the only one for the learn below.
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
                        return XPIs(Dev.XPBefore + DevGrantXP) && View->Level == 1 && View->PendingChoiceCount == 1; });
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
                if (!Check(!DevCardId.IsNone() && Prog->PendingChoiceCount == 1 && CardCount(DevCardId) == 0, TEXT("DevTest"),
                    TEXT("The second match's opening opportunity is still open for a DEV learn"))) { return Finish(); }
                return SendDev(Now, PC, TEXT("A DEV learn takes any legal card as the open opportunity's choice"),
                    ELDDevTestOp::LearnOption, 0, DevCardId);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [&] { const FLDProgressionView* View = PC.ReadOwnProgression();
                    return View && CardCount(DevCardId) == 1 && View->PendingChoiceCount == 0; }));
        case 7:
            if (!Dev.bSent)
            {
                return SendDev(Now, PC, TEXT("A DEV learn with no opportunity left is refused as NoOffer"), ELDDevTestOp::LearnOption, 0,
                    DevCardId);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Rejected, ELDGrowthReason::NoOffer,
                [&] { return CardCount(DevCardId) == 1; }));
        case 8:
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
        case 9:
            if (!Dev.bSent)
            {
                return SendDev(Now, PC, TEXT("A DEV clear removes only the DEV term"), ELDDevTestOp::ClearAttributes, 0);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [&] { const FLDCombatAttributeRowView* AD = OwnAttribute(PC, ELDProjectedAttribute::AD);
                    return AD && AD->Dev.Numerator == 0
                        && ExactP2(AD->Final) == Dev.ADBeforeP2 - LDDemoGrowthScenario::GrowthProbeDevADP2; }));
        case 10:
            if (!Dev.bSent)
            {
                return SendDev(Now, PC, TEXT("A DEV reset puts the buyer back to the match start"), ELDDevTestOp::ResetToMatchStart, 0);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::Final, ELDReceiptOutcome::Applied, ELDGrowthReason::None,
                [&]
                {
                    const FLDProgressionView* View = PC.ReadOwnProgression();
                    const FLDCombatAttributeRowView* AD = OwnAttribute(PC, ELDProjectedAttribute::AD);
                    return View && AD && View->Level == 1 && View->CumulativeXP == 0 && View->AttributeCards.IsEmpty()
                        && View->PendingChoiceCount == 1 && View->bHasOffer && View->Offer.OpportunityOrdinal == 1
                        && AD->Choices.Numerator == 0 && AD->Dev.Numerator == 0;
                }));
        case 11:
            if (!Dev.bSent)
            {
                return SendDev(Now, PC, TEXT("A match-wide DEV camp refresh from an owning client is refused as NotAuthority"),
                    ELDDevTestOp::RefreshCamps, 0);
            }
            return AdvanceDev(AwaitDev(Now, ELDAdmissionStatus::AdmissionRejected, ELDReceiptOutcome::None,
                ELDGrowthReason::NotAuthority, [] { return true; }));
        case 12:
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
                // Development lasts the approved MATCH.DevelopmentSec from the start; the stage normally ends inside it, and
                // if it does not, the same command must be refused instead.
                bDevOpenedInDevelopment = Public->PublicMatch.Phase == ELDMatchPhase::Development;
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

    // ---- Client1: kills and choices, each only through this owning connection. ----

    bool TickClient(double Now, ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        // The buyer dies once, on purpose, after the replacement; until then it keeps its first life.
        const bool bDeathStage = Stage == EStage::AwaitDeath || Stage == EStage::DeadChoice || Stage == EStage::Respawn;
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        if (!bDeathStage && (!Body || PC.OwnerState.LifePhase != ELDLifePhase::Alive || PC.OwnerState.Life != 1))
        {
            return Fail(TEXT("N03"), TEXT("The buyer lost its first life before the Host was asked to kill it"));
        }
        switch (Stage)
        {
        case EStage::ToSite: return TickToSite(Now, PC, *Body, KillSite, EStage::KillSeek, TEXT("N03"));
        case EStage::KillSeek: return TickKillSeek(Now, PC);
        case EStage::KillWait: return TickKillWait(Now, PC);
        case EStage::Choice: return TickChoice(Now, PC);
        case EStage::ChoiceQuery: return TickChoiceQuery(Now, PC);
        case EStage::Advance: return TickAdvance(Now, PC);
        case EStage::UseSkill: return TickUseSkill(Now, PC);
        case EStage::Replace: return TickReplace(Now, PC);
        case EStage::Relearn: return TickRelearn(Now, PC);
        case EStage::AwaitDeath: return TickAwaitDeath(Now, PC);
        case EStage::DeadChoice: return TickDeadChoice(Now, PC);
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
            KillBefore = *ProgView;
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
        const FLDDemoProgressionDefinition& Table = Catalog->Get().Progression;
        uint8 ExpectedLevel = 0;
        const ELDGrowthReason LevelReason = LDProgression::LevelForXP(Table, KillBefore.CumulativeXP + KillXP, ExpectedLevel);
        const FString Detail = FString::Printf(TEXT("kill %d of %d, victim L%u: xp %lld->%lld (+%lld) %s"),
            Kills + 1, KillsPlanned, uint32(VictimLevel), KillBefore.CumulativeXP, ProgView.CumulativeXP, KillXP,
            *LDDemoGrowthScenario::GrowthProbeProgressionText(ProgView));
        Check(Ended.Status == ELDOrderStatus::Completed && Ended.Failure == ELDOrderFailure::TargetDied, TEXT("N03"),
            TEXT("The owning client's own attack order ended in the victim's death"), Detail);
        Check(ProgView.CumulativeXP == KillBefore.CumulativeXP + KillXP, TEXT("N03"),
            TEXT("One kill pays exactly the approved XP bounty of the frozen victim level in one owner publication"), Detail);
        Check(LevelReason == ELDGrowthReason::None && ProgView.Level == ExpectedLevel
            && ProgView.PendingChoiceCount == ProgView.Level - ProgView.ConsumedThrough, TEXT("A06"),
            TEXT("Level and pending opportunities follow the approved XP thresholds: the opening one plus one per level crossed"), Detail);
        const TSharedRef<FJsonObject> KillRow = MakeShared<FJsonObject>();
        KillRow->SetNumberField(TEXT("index"), Kills + 1);
        KillRow->SetNumberField(TEXT("victim_level"), VictimLevel);
        KillRow->SetNumberField(TEXT("xp_before"), double(KillBefore.CumulativeXP));
        KillRow->SetNumberField(TEXT("xp_after"), double(ProgView.CumulativeXP));
        KillRow->SetNumberField(TEXT("level_after"), ProgView.Level);
        KillRow->SetNumberField(TEXT("pending_after"), ProgView.PendingChoiceCount);
        KillRow->SetNumberField(TEXT("attack_orders"), AttackOrders);
        KillRow->SetNumberField(TEXT("seconds"), Now - KillStartedAt);
        KillRows.Add(MakeShared<FJsonValueObject>(KillRow));
        ++Kills;
        AttackOrders = 0;
        if (!Passed) { return Finish(); }
        // The victim stays dead for the approved respawn delay, so the choice cannot race a reward.
        if (!bChoiceDone && ProgView.Level >= Catalog->Get().Progression.RerollLevels[0])
        {
            SetStage(EStage::Choice);
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
        Check(Kills >= 1 && bChoiceDone, TEXT("A01"),
            TEXT("From zero: real kills and a choice came only through the owning connection, with no grant or teleport"),
            FString::Printf(TEXT("kills=%d planned=%d"), Kills, KillsPlanned));
        Check(Public && (Public->DemoLobby.Phase == ELDMatchPhase::Development || Public->DemoLobby.Phase == ELDMatchPhase::CaptureOpen),
            TEXT("A01"), TEXT("The match stayed open through the whole scenario"));
        if (!Passed) { return Finish(); }
        // The remaining preset offers drive the Q/W replacement and, after the Host kills the buyer, a choice while dead.
        Planned = FChoiceCall();
        SetStage(EStage::Advance);
        return true;
    }

    // The planned card of the head offer; the Host preset every offer of this match.
    const FLDChoiceOption* PickOption(const FLDProgressionView& View) const
    {
        const int32 Index = int32(View.Offer.OpportunityOrdinal) - 1;
        if (Index < 0 || Index >= LDDemoGrowthScenario::GrowthProbePlannedOffers)
        {
            return nullptr;
        }
        const FName Wanted = LDDemoGrowthScenario::GrowthProbePlannedCards(Index)[0];
        return View.Offer.Options.FindByPredicate([Wanted](const FLDChoiceOption& Card) { return Card.DefinitionId == Wanted; });
    }

    bool BuildShowsChoice(const FLDProgressionView& View) const
    {
        if (ChoicePicked.Kind == ELDChoiceOptionKind::Opening)
        {
            const auto* Learned = LDDemoGrowthScenario::GrowthProbeActiveIn(View, 0);
            return View.OpeningCard == ChoicePicked.DefinitionId && Learned
                && Learned->DefinitionId == ChoiceFinal.OpeningActiveId && Learned->Rank == 1;
        }
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
        if (!bOpeningRerollChecked)
        {
            if (!ProgView || !ProgView->bCanChoose) { return true; }
            if (!bOpeningRerollSent)
            {
                Check(ProgView->RerollTokens > 0 && ProgView->ConsumedThrough == 0, TEXT("Opening"),
                    TEXT("A real owning client reached a reroll level without choosing the opening direction"));
                BeforeOpeningReroll = *ProgView;
                OpeningReroll.OfferId = ProgView->Offer.OfferId;
                OpeningReroll.OfferRevision = ProgView->Offer.OfferRevision;
                OpeningReroll.ExpectedBuildRevision = ProgView->BuildRevision;
                OpeningReroll.CatalogDigest = ProgView->CatalogDigest;
                OpeningReroll.bReroll = true;
                ChoiceResultMark = ChoiceResults.Num();
                ELDGrowthReason Local = ELDGrowthReason::None;
                OpeningReroll.RequestId = PC.SubmitChoice(OpeningReroll, Local);
                bOpeningRerollSent = true;
                if (!Check(OpeningReroll.RequestId != 0, TEXT("Opening"), TEXT("The owning client submits a hostile opening reroll")))
                { return Finish(); }
                return true;
            }
            const auto* Answer = FindChoiceAnswer(OpeningReroll.RequestId, ChoiceResultMark);
            if (!Answer)
            {
                if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(TEXT("Opening"), TEXT("The opening reroll received no final answer"));
            }
            Check(Answer->bHasReceipt && Answer->Receipt.Header.Outcome == ELDReceiptOutcome::Rejected
                && LDGrowthWire::Reason(Answer->Receipt.Header.Reason) == ELDGrowthReason::OpeningNoReroll
                && ProgView->Offer.OfferId == BeforeOpeningReroll.Offer.OfferId
                && ProgView->RerollTokens == BeforeOpeningReroll.RerollTokens && ProgView->ConsumedThrough == 0,
                TEXT("Opening"), TEXT("The server refuses opening reroll despite earned tokens, preserving cards and tokens"));
            bOpeningRerollChecked = true;
            if (!Passed) { return Finish(); }
        }
        if (!bChoiceSent)
        {
            if (!ProgView || !ProgView->bHasOffer || !ProgView->bCanChoose)
            {
                if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(TEXT("A10"), TEXT("The earned opportunity never offered a choosable head offer"));
            }
            const FLDChoiceOption* Chosen = PickOption(*ProgView);
            if (!Check(LDDemoGrowthScenario::GrowthProbeIsPreset(*ProgView) && Chosen && Chosen->Kind == ELDChoiceOptionKind::Opening,
                TEXT("Fixture"), TEXT("The first head offer is exactly the Host's preset, and its planned card learns an active"),
                FString::Printf(TEXT("ordinal=%u"), uint32(ProgView->Offer.OpportunityOrdinal))))
            {
                return Finish();
            }
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
            ELDGrowthReason LocalReason = ELDGrowthReason::InvalidRequest;
            ULDChoicesPanel* Panel = LDDemoGrowthScenario::GrowthProbeChoices(PC);
            if (!Panel) { return Fail(TEXT("Opening"), TEXT("The owning client has no choices panel")); }
            if (!Check(bool(Panel->SubmitChoice), TEXT("Opening"), TEXT("The panel has its owning-connection submit adapter")))
            { return Finish(); }
            const auto Original = Panel->SubmitChoice;
            uint64 Submitted = 0;
            Panel->SubmitChoice = [&Original, &Submitted, &LocalReason](const FLDChoiceCommand& Sent, ELDGrowthReason& Reason)
            {
                Submitted = Original(Sent, Reason);
                LocalReason = Reason;
                return Submitted;
            };
            Panel->OpenPanel();
            const bool bClicked = Panel->ChooseCard(0);
            Panel->SubmitChoice = Original;
            if (!bClicked) { return Fail(TEXT("Opening"), TEXT("The opening card click failed")); }
            ChoiceSent.RequestId = Submitted;
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
        if (ChoiceFinal.Kind == ELDChoiceOptionKind::Opening)
        {
            const FString Revealed = LDTerms::OpeningRevealed(LDHUD::ContentName(&Catalog->Get(), ChoiceFinal.OpeningActiveId.ToString()));
            const auto* Q = LDDemoGrowthScenario::GrowthProbeEntry(PC.CombatView, ELDCombatEntry::Q);
            ULDChoicesPanel* Panel = LDDemoGrowthScenario::GrowthProbeChoices(PC);
            const FObjectProperty* Field = Panel ? FindFProperty<FObjectProperty>(ULDChoicesPanel::StaticClass(), TEXT("Feedback")) : nullptr;
            const auto* Feedback = Field ? Cast<UTextBlock>(Field->GetObjectPropertyValue_InContainer(Panel)) : nullptr;
            if ((!Q || !Feedback || !Feedback->GetText().ToString().Contains(Revealed))
                && Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            Check(Q && Q->DefinitionId == ChoiceFinal.OpeningActiveId && Q->Rank == 1 && Q->ReadyAtMs <= PC.CombatView.AsOfMs
                && Feedback && Feedback->GetText().ToString().Contains(Revealed), TEXT("Opening"),
                TEXT("The card click reveals the random active, teaches it in Q and makes its cooldown ready"));
        }
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
        ChoiceRow = MakeShared<FJsonObject>();
        ChoiceRow->SetNumberField(TEXT("request_id"), double(ChoiceSent.RequestId));
        ChoiceRow->SetStringField(TEXT("option_id"), LDDemoGrowthScenario::GrowthProbeGuid(ChoiceFinal.OptionId));
        ChoiceRow->SetStringField(TEXT("definition"), ChoiceFinal.DefinitionId.ToString());
        ChoiceRow->SetNumberField(TEXT("new_rank"), ChoiceFinal.NewRank);
        ChoiceRow->SetNumberField(TEXT("pending_before"), ChoiceBefore.PendingChoiceCount);
        ChoiceRow->SetBoolField(TEXT("next_offer"), ChoiceFinal.bHasNextOffer);
        bChoiceDone = true;
        return ContinueKills(PC);
    }

    // ---- Client1: Q/W replacement and a choice while dead, on the Host's preset offers ----

    // Sends the planned card of the head offer. ReplaceSlot >= 0 confirms the replacement of that Q/W slot; bUnconfirmed
    // sends the planned active without any replacement terms (it must be refused and consume nothing).
    bool SendPlanned(ALDPlayerController& PC, const FString& Step, int32 ReplaceSlot, bool bUnconfirmed = false)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        Planned = FChoiceCall();
        Planned.Step = Step;
        Planned.bSent = true;
        const FLDChoiceOption* Option = View ? PickOption(*View) : nullptr;
        if (!Check(View && View->bCanChoose && LDDemoGrowthScenario::GrowthProbeIsPreset(*View) && Option, TEXT("Fixture"),
            Step + TEXT(": the head offer is exactly the Host's preset for its opportunity, with its planned card"),
            View ? FString::Printf(TEXT("ordinal=%u %s"), uint32(View->Offer.OpportunityOrdinal),
                *LDDemoGrowthScenario::GrowthProbeProgressionText(*View)) : FString(TEXT("no progression view"))))
        {
            return false;
        }
        FLDChoiceCommand Command;
        Command.OfferId = View->Offer.OfferId;
        Command.OfferRevision = View->Offer.OfferRevision;
        Command.OptionId = Option->OptionId;
        Command.ExpectedBuildRevision = View->BuildRevision;
        Command.CatalogDigest = View->CatalogDigest;
        if (ReplaceSlot >= 0 && !bUnconfirmed)
        {
            const FLDLearnedSkillView* Old = LDDemoGrowthScenario::GrowthProbeActiveIn(*View, uint8(ReplaceSlot));
            if (!Check(Old && FLDDefinitionWire::Encode(Old->DefinitionId, Command.ExpectedOldSkillId), TEXT("A12"),
                Step + TEXT(": the slot to replace holds a learned active skill")))
            {
                return false;
            }
            Command.bReplace = true;
            Command.ReplaceSlot = uint8(ReplaceSlot);
            Command.ExpectedOldRank = Old->Rank;
            Command.bReplaceConfirmed = true;
        }
        Planned.Before = *View;
        Planned.Picked = *Option;
        // The exact terms SubmitChoice sends: it fills Generation, Life and the life phase from OwnerState.
        Planned.Sent = Command;
        Planned.Sent.Generation = PC.OwnerState.Match;
        Planned.Sent.Life = PC.OwnerState.Life;
        Planned.Sent.ExpectedLifePhase = PC.OwnerState.LifePhase;
        Planned.SubmitMark = Submits.Num();
        Planned.ResultMark = ChoiceResults.Num();
        Planned.PublishMark = PC.OwnerState.Growth.PublishRevision;
        Planned.SentAt = FPlatformTime::Seconds();
        const uint64 Peeked = PC.PeekGrowthRequestId(ELDGrowthFamily::Choice);
        ELDGrowthReason LocalReason = ELDGrowthReason::None;
        Planned.Sent.RequestId = PC.SubmitChoice(Command, LocalReason);
        return Check(Planned.Sent.RequestId != 0 && Planned.Sent.RequestId == Peeked && LocalReason == ELDGrowthReason::None, TEXT("A24"),
            Step + TEXT(" goes through SubmitChoice with the next choice request id"),
            FString::Printf(TEXT("request=%llu peek=%llu local=%s"), Planned.Sent.RequestId, Peeked, LDGrowthWire::ReasonName(LocalReason)));
    }

    // The planned choice's pending admission, its final receipt and the owner publication carrying it.
    LDDemoGrowthScenario::EGrowthProbeWait AwaitPlanned(double Now, const ALDPlayerController& PC, const FLDChoiceReceipt*& Out,
        FString& Why) const
    {
        Out = nullptr;
        const FLDSubmitResult* Admission = FindSubmit(ELDGrowthFamily::Choice, Planned.Sent.RequestId, Planned.SubmitMark);
        const FLDChoiceReceiptQueryResult* Answer = FindChoiceAnswer(Planned.Sent.RequestId, Planned.ResultMark);
        if (Admission && Admission->Status != ELDAdmissionStatus::AcceptedPending)
        {
            Why = TEXT("not admitted pending: ") + LDDemoGrowthScenario::GrowthProbeReason(Admission->Reason);
            return LDDemoGrowthScenario::EGrowthProbeWait::Failed;
        }
        const FLDProgressionView* View = PC.ReadOwnProgression();
        const bool bFinal = Admission && Answer && Answer->Lookup == ELDReceiptLookup::Final && Answer->bHasReceipt;
        const bool bPublished = bFinal && View && PC.OwnerState.Growth.PublishRevision > Planned.PublishMark
            && (Answer->Receipt.Header.Outcome != ELDReceiptOutcome::Applied
                || View->ProgressionRevision == Answer->Receipt.Header.Revisions.ProgressionRevision);
        if (!bPublished)
        {
            if (Now - Planned.SentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds)
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

    // Offers 2–4: not_mad in Q rises to rank 2, then snark_bolt and pants_down fill W and E.
    bool TickAdvance(double Now, ALDPlayerController& PC)
    {
        if (!Planned.bSent)
        {
            const FLDProgressionView* View = PC.ReadOwnProgression();
            if (!View || !View->bCanChoose || !View->bHasOffer)
            {
                if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(TEXT("A12"), TEXT("The next preset offer never became choosable"));
            }
            return SendPlanned(PC, View->ConsumedThrough == 1 ? TEXT("upgrade-q")
                : View->ConsumedThrough == 2 ? TEXT("learn-w") : TEXT("learn-e"), -1) ? true : Finish();
        }
        const FLDChoiceReceipt* Final = nullptr;
        FString Why;
        const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitPlanned(Now, PC, Final, Why);
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Failed)
        {
            return Fail(TEXT("A12"), Planned.Step + TEXT(" did not complete: ") + Why);
        }
        const FLDProgressionView* View = PC.ReadOwnProgression();
        const FLDLearnedSkillView* Q = View ? LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 0) : nullptr;
        const FLDLearnedSkillView* W = View ? LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 1) : nullptr;
        const FLDLearnedSkillView* E = View ? LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 2) : nullptr;
        const FString Detail = View ? LDDemoGrowthScenario::GrowthProbeSkillsText(*View) : FString(TEXT("no progression view"));
        const bool bApplied = Final->Header.Outcome == ELDReceiptOutcome::Applied && !Final->bReplaced && Final->DefinitionId == Planned.Picked.DefinitionId;
        if (Planned.Step == TEXT("upgrade-q"))
        {
            Check(bApplied && Final->Kind == ELDChoiceOptionKind::UpgradeActive && Final->NewRank == 2 && Q
                && Q->DefinitionId == Planned.Picked.DefinitionId && Q->Rank == 2 && !W, TEXT("A12"),
                TEXT("The Q skill rises to rank 2 before it is replaced"), Detail);
            if (!Passed) { return Finish(); }
            Planned = FChoiceCall();
            StageAt = Now;
            return true;
        }
        const bool bW=Planned.Step==TEXT("learn-w");
        Check(bApplied && Final->Kind == ELDChoiceOptionKind::LearnActive && Final->NewRank == 1 && Q && Q->Rank == 2
            && (bW ? W && W->DefinitionId==Planned.Picked.DefinitionId && W->Rank==1 && !E
                : W && W->Rank==1 && E && E->DefinitionId==Planned.Picked.DefinitionId && E->Rank==1), TEXT("A12"),
            bW ? TEXT("A second active takes the free W slot") : TEXT("A third active takes the free E slot, so Q/W/E are full"), Detail);
        if (!Passed) { return Finish(); }
        if (bW)
        {
            Planned=FChoiceCall();
            StageAt=Now;
            return true;
        }
        Planned = FChoiceCall();
        bSkillSent = false;
        SetStage(EStage::UseSkill);
        return true;
    }

    // Q's rank-2 skill puts its own persistent track on cooldown; that deadline has to survive forgetting the skill.
    bool TickUseSkill(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        const FLDLearnedSkillView* Q = View ? LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 0) : nullptr;
        const FLDCombatEntryView* Entry = CurrentEntry(PC, ELDCombatEntry::Q);
        const int64 AsOfMs = PC.CombatView.AsOfMs;
        if (!bSkillSent)
        {
            if (!Q || !Entry || Entry->DefinitionId != Q->DefinitionId || Entry->Rank != Q->Rank || !Entry->IssuedCooldownId.IsValid()
                || Entry->ReadyAtMs > AsOfMs)
            {
                if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(TEXT("A12"), TEXT("Combat never showed Q's rank-2 skill installed and ready on its own track"));
            }
            ForgottenId = Q->DefinitionId;
            ForgottenRank = Q->Rank;
            ForgottenTrack = Entry->IssuedCooldownId;
            SkillReceiptMark = PC.CombatReceipts.Serial();
            PC.ActivateCombatEntry(ELDCombatEntry::Q);
            bSkillSent = true;
            StageAt = Now;
            return true;
        }
        if (const FLDCombatActionReceipt* Refused = PC.CombatReceipts.FindLatestSince(SkillReceiptMark,
            [](const FLDCombatActionReceipt& Receipt) { return Receipt.Entry == ELDCombatEntry::Q && Receipt.Phase == ELDCombatPhase::Rejected; }))
        {
            return Fail(TEXT("A12"), TEXT("Q's active was rejected: ") + StaticEnum<ELDCombatReject>()->GetNameStringByValue(int64(Refused->Reject)));
        }
        if (!Entry || Entry->IssuedCooldownId != ForgottenTrack || Entry->ReadyAtMs <= AsOfMs)
        {
            if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("A12"), TEXT("Using Q's skill did not put its own track on cooldown"));
        }
        ForgottenReadyAtMs = Entry->ReadyAtMs;
        Check(true, TEXT("A12"), TEXT("Using Q's rank-2 skill cools its own persistent track until a fixed deadline"),
            FString::Printf(TEXT("%s r%u track %s ready at %lld as of %lld"), *ForgottenId.ToString(), uint32(ForgottenRank),
                *LDDemoGrowthScenario::GrowthProbeGuid(ForgottenTrack), ForgottenReadyAtMs, AsOfMs));
        bUnconfirmedDone = false;
        Planned = FChoiceCall();
        SetStage(EStage::Replace);
        return true;
    }

    // Offer 5: a fourth active while Q/W/E are held. Picking it without a confirmed replacement is refused and
    // consumes nothing; the confirmed replacement of Q forgets the cooling skill with its rank and installs the new one.
    bool TickReplace(double Now, ALDPlayerController& PC)
    {
        if (!Planned.bSent)
        {
            return SendPlanned(PC, bUnconfirmedDone ? TEXT("replace-q") : TEXT("pick-without-replacement"), 0, !bUnconfirmedDone)
                ? true : Finish();
        }
        const FLDChoiceReceipt* Final = nullptr;
        FString Why;
        const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitPlanned(Now, PC, Final, Why);
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Failed)
        {
            return Fail(TEXT("A12"), Planned.Step + TEXT(" did not complete: ") + Why);
        }
        const FLDProgressionView* View = PC.ReadOwnProgression();
        if (!bUnconfirmedDone)
        {
            Check(Final->Header.Outcome == ELDReceiptOutcome::Rejected
                && LDGrowthWire::Reason(Final->Header.Reason) == ELDGrowthReason::ReplacementRequired && View && View->bHasOffer
                && View->Offer.OfferId == Planned.Before.Offer.OfferId && View->ConsumedThrough == Planned.Before.ConsumedThrough
                && View->PendingChoiceCount == Planned.Before.PendingChoiceCount && View->BuildRevision == Planned.Before.BuildRevision
                && LDDemoGrowthScenario::GrowthProbeSkillsText(*View) == LDDemoGrowthScenario::GrowthProbeSkillsText(Planned.Before), TEXT("A12"),
                TEXT("Picking a new active while Q/W/E are held, without a confirmed replacement, is refused and consumes nothing"),
                FString::Printf(TEXT("reason=%s %s"), *LDDemoGrowthScenario::GrowthProbeReason(Final->Header.Reason),
                    View ? *LDDemoGrowthScenario::GrowthProbeProgressionText(*View) : TEXT("no progression view")));
            if (!Passed) { return Finish(); }
            bUnconfirmedDone = true;
            Planned = FChoiceCall();
            StageAt = Now;
            return true;
        }
        const FLDLearnedSkillView* Q = View ? LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 0) : nullptr;
        const FLDLearnedSkillView* W = View ? LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 1) : nullptr;
        const FLDLearnedSkillView* E = View ? LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 2) : nullptr;
        const FString Detail = View ? LDDemoGrowthScenario::GrowthProbeSkillsText(*View) : FString(TEXT("no progression view"));
        Check(Final->Header.Outcome == ELDReceiptOutcome::Applied && Final->Kind == ELDChoiceOptionKind::LearnActive
            && Final->DefinitionId == Planned.Picked.DefinitionId && Final->NewRank == 1 && Final->bReplaced && Final->ReplacedSlot == 0
            && Final->ReplacedSkillId == ForgottenId && Final->ReplacedRank == ForgottenRank, TEXT("A12"),
            TEXT("The confirmed replacement of Q applies once and names the forgotten skill and its rank"),
            FString::Printf(TEXT("replaced %s r%u in slot %u by %s r%u"), *Final->ReplacedSkillId.ToString(), uint32(Final->ReplacedRank),
                uint32(Final->ReplacedSlot), *Final->DefinitionId.ToString(), uint32(Final->NewRank)));
        Check(View && Q && Q->DefinitionId == Planned.Picked.DefinitionId && Q->Rank == 1 && W && W->Rank == 1 && E && E->Rank==1
            && !LDDemoGrowthScenario::GrowthProbeSkill(*View, ForgottenId), TEXT("A12"),
            TEXT("Q now holds the new skill at rank 1; the forgotten skill and its rank are gone from the build"), Detail);
        const FLDChoiceOption* Again = View && View->bHasOffer ? View->Offer.Options.FindByPredicate([this](const FLDChoiceOption& Card)
        {
            return Card.DefinitionId == ForgottenId;
        }) : nullptr;
        Check(Again && Again->Kind == ELDChoiceOptionKind::LearnActive && Again->FromRank == 0 && Again->ToRank == 1, TEXT("A12"),
            TEXT("The forgotten skill returns only as a rank-1 card: its old rank is kept nowhere"),
            Again ? FString::Printf(TEXT("kind=%u rank %u->%u"), uint32(Again->Kind), uint32(Again->FromRank), uint32(Again->ToRank))
                : FString(TEXT("not offered")));
        if (!Passed) { return Finish(); }
        Planned = FChoiceCall();
        SetStage(EStage::Relearn);
        return true;
    }

    // Combat binds Q's new skill to its own ready track. Offer 6 relearns the forgotten skill over W: it must bind the
    // old persistent track with its original deadline, so forgetting did not wash the cooldown.
    bool TickRelearn(double Now, ALDPlayerController& PC)
    {
        const int64 AsOfMs = PC.CombatView.AsOfMs;
        if (!Planned.bSent)
        {
            const FLDProgressionView* View = PC.ReadOwnProgression();
            const FLDLearnedSkillView* Q = View ? LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 0) : nullptr;
            const FLDCombatEntryView* QEntry = CurrentEntry(PC, ELDCombatEntry::Q);
            if (!Q || !QEntry || QEntry->DefinitionId != Q->DefinitionId)
            {
                if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(TEXT("A12"), TEXT("Combat never installed Q's new skill"));
            }
            Check(QEntry->Rank == 1 && QEntry->IssuedCooldownId.IsValid() && QEntry->IssuedCooldownId != ForgottenTrack
                && QEntry->ReadyAtMs <= AsOfMs, TEXT("A12"), TEXT("Q's new skill binds its own ready track: the slot hands no cooldown over"),
                FString::Printf(TEXT("%s track %s ready at %lld as of %lld"), *QEntry->DefinitionId.ToString(),
                    *LDDemoGrowthScenario::GrowthProbeGuid(QEntry->IssuedCooldownId), QEntry->ReadyAtMs, AsOfMs));
            if (!Passed) { return Finish(); }
            return SendPlanned(PC, TEXT("relearn-over-w"), 1) ? true : Finish();
        }
        const FLDChoiceReceipt* Final = nullptr;
        FString Why;
        const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitPlanned(Now, PC, Final, Why);
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Failed)
        {
            return Fail(TEXT("A12"), Planned.Step + TEXT(" did not complete: ") + Why);
        }
        const FLDCombatEntryView* WEntry = CurrentEntry(PC, ELDCombatEntry::W);
        if (!WEntry || WEntry->DefinitionId != ForgottenId)
        {
            if (Now - Planned.SentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("A12"), TEXT("Combat never installed the relearned skill in W"));
        }
        const FLDProgressionView* View = PC.ReadOwnProgression();
        const FLDLearnedSkillView* W = View ? LDDemoGrowthScenario::GrowthProbeActiveIn(*View, 1) : nullptr;
        Check(Final->Header.Outcome == ELDReceiptOutcome::Applied && Final->Kind == ELDChoiceOptionKind::LearnActive
            && Final->DefinitionId == ForgottenId && Final->NewRank == 1 && Final->bReplaced && Final->ReplacedSlot == 1 && W
            && W->DefinitionId == ForgottenId && W->Rank == 1 && WEntry->Rank == 1, TEXT("A12"),
            TEXT("Relearning the forgotten skill over W starts again at rank 1"),
            View ? LDDemoGrowthScenario::GrowthProbeSkillsText(*View) : FString(TEXT("no progression view")));
        const bool bStillCooling = ForgottenReadyAtMs > AsOfMs;
        Check(WEntry->IssuedCooldownId == ForgottenTrack && WEntry->ReadyAtMs == ForgottenReadyAtMs, TEXT("A12"),
            TEXT("The relearned skill binds its old persistent track with the original deadline: forgetting did not wash the cooldown"),
            FString::Printf(TEXT("track %s ready at %lld (original %lld) as of %lld, still cooling=%d"),
                *LDDemoGrowthScenario::GrowthProbeGuid(WEntry->IssuedCooldownId), WEntry->ReadyAtMs, ForgottenReadyAtMs, AsOfMs,
                bStillCooling ? 1 : 0));
        ReplaceRow = MakeShared<FJsonObject>();
        ReplaceRow->SetStringField(TEXT("forgotten"), ForgottenId.ToString());
        ReplaceRow->SetNumberField(TEXT("forgotten_rank"), ForgottenRank);
        ReplaceRow->SetStringField(TEXT("track"), LDDemoGrowthScenario::GrowthProbeGuid(ForgottenTrack));
        ReplaceRow->SetNumberField(TEXT("ready_at_ms"), double(ForgottenReadyAtMs));
        ReplaceRow->SetStringField(TEXT("relearned_track"), LDDemoGrowthScenario::GrowthProbeGuid(WEntry->IssuedCooldownId));
        ReplaceRow->SetNumberField(TEXT("relearned_ready_at_ms"), double(WEntry->ReadyAtMs));
        ReplaceRow->SetBoolField(TEXT("still_cooling"), bStillCooling);
        if (!Passed || !Mark(TEXT("KillMe.json"))) { return Finish(); }
        SetStage(EStage::AwaitDeath);
        return true;
    }

    // The Host's own attack kills the buyer: the dead owner keeps its growth, has no body and waits for its respawn.
    bool TickAwaitDeath(double Now, ALDPlayerController& PC)
    {
        const FLDCombatOwnerView& Own = PC.CombatView;
        const bool bDead = PC.OwnerState.LifePhase == ELDLifePhase::Dead && PC.OwnerState.Life == 1;
        const bool bViewDead = Own.Generation == PC.OwnerState.Match && Own.Life == PC.OwnerState.Life && !Own.bHasBody;
        if (!bDead || !bViewDead)
        {
            // The Host may first respawn and walk back before its grant and attack.
            const bool bWaiting = (PC.OwnerState.LifePhase == ELDLifePhase::Alive || bDead) && PC.OwnerState.Life == 1;
            if (bWaiting && Now - StageAt <= RespawnSeconds + LDDemoGrowthScenario::GrowthProbeReturnSeconds
                + LDDemoGrowthScenario::GrowthProbeKillSeconds)
            {
                return true;
            }
            return Fail(TEXT("A14"), FString::Printf(TEXT("The Host did not kill the buyer in time (phase %s, life %u)"),
                *StaticEnum<ELDLifePhase>()->GetNameStringByValue(int64(PC.OwnerState.LifePhase)), PC.OwnerState.Life));
        }
        DeathLife = PC.OwnerState.Life;
        DeathRespawnAtMs = PC.OwnerState.RespawnAtMs;
        DeadMaxHPP2 = Own.MaxHPP2;
        Check(PC.OwnerState.Hp == 0 && Own.HPP2 == 0 && DeathRespawnAtMs > 0 && DeadMaxHPP2 > 0, TEXT("A14"),
            TEXT("The Host's attack killed the buyer: dead, no body, zero HP, a respawn time set and the next life's build projected"),
            FString::Printf(TEXT("life %u respawn at %lld hp %.0f, view hp=%lld max=%lld"), DeathLife, DeathRespawnAtMs, PC.OwnerState.Hp,
                Own.HPP2, DeadMaxHPP2));
        if (!Passed) { return Finish(); }
        Planned = FChoiceCall();
        SetStage(EStage::DeadChoice);
        return true;
    }

    // Offer 6 while dead: the choice changes only the persistent build. The owner stays dead on the same life with the
    // same respawn time and no HP, while the projection of its next life grows by the card.
    bool TickDeadChoice(double Now, ALDPlayerController& PC)
    {
        if (!Planned.bSent)
        {
            const FLDProgressionView* View = PC.ReadOwnProgression();
            if (!View || !View->bCanChoose || View->LifePhase != ELDLifePhase::Dead)
            {
                if (Now - StageAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
                return Fail(TEXT("A14"), TEXT("The dead owner never saw its head offer as choosable"));
            }
            return SendPlanned(PC, TEXT("choose-while-dead"), -1) ? true : Finish();
        }
        const FLDChoiceReceipt* Final = nullptr;
        FString Why;
        const LDDemoGrowthScenario::EGrowthProbeWait Awaited = AwaitPlanned(Now, PC, Final, Why);
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Waiting) { return true; }
        if (Awaited == LDDemoGrowthScenario::EGrowthProbeWait::Failed)
        {
            return Fail(TEXT("A14"), Planned.Step + TEXT(" did not complete: ") + Why);
        }
        if (Planned.QuietUntil == 0)
        {
            const FLDProgressionView* View = PC.ReadOwnProgression();
            const FLDAttributeCardView* Before = Planned.Before.AttributeCards.FindByPredicate([this](const FLDAttributeCardView& CardRow)
            {
                return CardRow.DefinitionId == Planned.Picked.DefinitionId;
            });
            const FLDAttributeCardView* After = View ? View->AttributeCards.FindByPredicate([this](const FLDAttributeCardView& CardRow)
            {
                return CardRow.DefinitionId == Planned.Picked.DefinitionId;
            }) : nullptr;
            Check(Planned.Sent.ExpectedLifePhase == ELDLifePhase::Dead && Planned.Sent.Life == DeathLife
                && Final->Header.Outcome == ELDReceiptOutcome::Applied && Final->Header.OriginalLifePhase == ELDLifePhase::Dead
                && Final->Header.OriginalLife == DeathLife && Final->DefinitionId == Planned.Picked.DefinitionId, TEXT("A14"),
                TEXT("A choice confirmed while dead is admitted for the Dead phase of the current life and applies"),
                FString::Printf(TEXT("life %u card %s"), DeathLife, *Planned.Picked.DefinitionId.ToString()));
            Check(After && After->Count == (Before ? Before->Count : 0) + 1, TEXT("A14"), TEXT("The dead owner's build shows the chosen card"),
                View ? LDDemoGrowthScenario::GrowthProbeProgressionText(*View) : FString(TEXT("no progression view")));
            if (!Passed) { return Finish(); }
            // A choice that revived, healed or moved the respawn would show within this quiet period.
            Planned.QuietUntil = Now + LDDemoGrowthScenario::GrowthProbeQuietSeconds;
            return true;
        }
        const FLDCombatOwnerView& Own = PC.CombatView;
        const bool bGrown = Own.Generation == PC.OwnerState.Match && Own.Life == DeathLife && Own.MaxHPP2 != DeadMaxHPP2;
        if (Now < Planned.QuietUntil || (!bGrown && Now - Planned.SentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds))
        {
            return true;
        }
        GrownMaxHPP2 = Own.MaxHPP2;
        Check(PC.OwnerState.LifePhase == ELDLifePhase::Dead && PC.OwnerState.Life == DeathLife && PC.OwnerState.RespawnAtMs == DeathRespawnAtMs
            && PC.OwnerState.Hp == 0 && !Own.bHasBody && Own.HPP2 == 0 && GrownMaxHPP2 > DeadMaxHPP2, TEXT("A14"),
            TEXT("Choosing while dead changes only the build: no early respawn, the same respawn time, no heal, a larger next-life maximum"),
            FString::Printf(TEXT("phase %s life %u respawn at %lld (was %lld) hp %.0f; view body=%d hp=%lld max %lld -> %lld"),
                *StaticEnum<ELDLifePhase>()->GetNameStringByValue(int64(PC.OwnerState.LifePhase)), PC.OwnerState.Life,
                PC.OwnerState.RespawnAtMs, DeathRespawnAtMs, PC.OwnerState.Hp, Own.bHasBody ? 1 : 0, Own.HPP2, DeadMaxHPP2, GrownMaxHPP2));
        if (!Passed) { return Finish(); }
        SetStage(EStage::Respawn);
        return true;
    }

    // The normal respawn after the approved delay: a new life with full HP and MP at the maximum grown while dead.
    bool TickRespawn(double Now, ALDPlayerController& PC)
    {
        const FLDCombatOwnerView& Own = PC.CombatView;
        const bool bAlive = PC.OwnerState.LifePhase == ELDLifePhase::Alive && PC.OwnerState.Life == DeathLife + 1
            && Cast<ALDCharacter>(PC.GetPawn()) && Own.Generation == PC.OwnerState.Match && Own.Life == PC.OwnerState.Life && Own.bHasBody;
        if (!bAlive)
        {
            if (Now - StageAt <= RespawnSeconds + LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("A14"), TEXT("The buyer did not respawn after the approved delay"));
        }
        Check(Own.MaxHPP2 == GrownMaxHPP2 && Own.HPP2 == Own.MaxHPP2 && Own.MaxMPP2 > 0 && Own.MPP2 == Own.MaxMPP2, TEXT("A14"),
            TEXT("The normal respawn restores full HP and MP at the maximum grown while dead"),
            FString::Printf(TEXT("life %u hp %lld/%lld mp %lld/%lld"), PC.OwnerState.Life, Own.HPP2, Own.MaxHPP2, Own.MPP2, Own.MaxMPP2));
        DeathRow = MakeShared<FJsonObject>();
        DeathRow->SetNumberField(TEXT("life"), DeathLife);
        DeathRow->SetNumberField(TEXT("respawn_at_ms"), double(DeathRespawnAtMs));
        DeathRow->SetStringField(TEXT("card"), Planned.Picked.DefinitionId.ToString());
        DeathRow->SetNumberField(TEXT("max_hp_p2_before"), double(DeadMaxHPP2));
        DeathRow->SetNumberField(TEXT("max_hp_p2_after"), double(GrownMaxHPP2));
        DeathRow->SetNumberField(TEXT("respawned_life"), PC.OwnerState.Life);
        DeathRow->SetNumberField(TEXT("respawn_hp_p2"), double(Own.HPP2));
        if (!Passed) { return Finish(); }
        // The first match is done and dirty; the Host ends it and the second match must start from clean owner state.
        RecordFirstMatch(PC);
        if (!Mark(TEXT("Client1Match1Done.json"))) { return Finish(); }
        SetStage(EStage::AwaitLobby);
        return true;
    }

    // Client1, second match: the first match's exact choice and a query for its receipt, through this connection.
    bool TickSecondReplay(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* ProgView = PC.ReadOwnProgression();
        if (!ProgView) { return Fail(TEXT("Iso"), TEXT("The second match's owner views became unavailable")); }
        if (!bReplaySent)
        {
            ReplayProgBefore = *ProgView;
            ReplaySubmitMark = Submits.Num();
            ReplayResultMark = ChoiceResults.Num();
            PC.ServerGrowthChoice(ChoiceSent);
            FLDReceiptQuery Query;
            Query.Generation = FirstGeneration;
            Query.Family = ELDGrowthFamily::Choice;
            Query.RequestId = ChoiceSent.RequestId;
            PC.QueryGrowthReceipt(Query);
            bReplaySent = true;
            ReplaySentAt = Now;
            ReplayQuietUntil = 0;
            return true;
        }
        const FLDSubmitResult* ChoiceAnswer = FindSubmit(ELDGrowthFamily::Choice, ChoiceSent.RequestId, ReplaySubmitMark);
        const FLDChoiceReceiptQueryResult* QueryAnswer = nullptr;
        for (int32 ResultIndex = ReplayResultMark; ResultIndex < ChoiceResults.Num() && !QueryAnswer; ++ResultIndex)
        {
            const FLDChoiceReceiptQueryResult& Answer = ChoiceResults[ResultIndex];
            if (Answer.Query.Generation == FirstGeneration && Answer.Query.Family == ELDGrowthFamily::Choice
                && Answer.Query.RequestId == ChoiceSent.RequestId)
            {
                QueryAnswer = &Answer;
            }
        }
        if (!ChoiceAnswer || !QueryAnswer)
        {
            if (Now - ReplaySentAt <= LDDemoGrowthScenario::GrowthProbeRequestSeconds) { return true; }
            return Fail(TEXT("Iso"), FString::Printf(TEXT("The stale first-match requests were not all answered (choice=%d query=%d)"),
                ChoiceAnswer ? 1 : 0, QueryAnswer ? 1 : 0));
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
        Check(QueryAnswer->Lookup == ELDReceiptLookup::InvalidContext && !QueryAnswer->bHasReceipt && !QueryAnswer->bHasPending, TEXT("Iso"),
            TEXT("A receipt query for the first match's choice finds no receipt in the second match (InvalidContext)"));
        const FLDProgressionView* ProgAfter = PC.ReadOwnProgression();
        Check(ProgAfter && LDDemoGrowthScenario::GrowthProbeSameOwner(ReplayProgBefore, *ProgAfter)
            && PC.PeekGrowthRequestId(ELDGrowthFamily::Choice) == 1, TEXT("Iso"),
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
            ? TEXT("DEV-only owned-connection growth scenario, victim and growth-audit role: a rendered, participating Listen Host with -LDDevGrowthFixture that stages the buyer's offers in the Lobby (its opening offer first), opens each match with exactly one pending opening choice, never attacks until the buyer asks to die, then takes its own DEV XP grant, kills the buyer and both scavengers of an authored low camp and sees each exact XP reward in its own owner view and rendered HUD, ends the first match without a winner and checks its own state in a second normal match. Not AI, capture or full-match acceptance.")
            : TEXT("DEV-only owned-connection growth scenario, buyer role: exactly one pending opening choice at each match start (never re-granted by a respawn), real kill XP rewards, choices on the Host's preset offers, a confirmed Q replacement whose relearn keeps the old cooldown deadline, a choice confirmed while dead, then clean owner state and refused stale requests in a second normal match. Not camps, AI, capture or full-match acceptance."));
        Report->SetBoolField(TEXT("growth_fixture"), LDDevGrowthFixture::IsSwitchPresent());
        Report->SetStringField(TEXT("catalog_sha256"), CatalogDigest);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetNumberField(TEXT("stage"), int32(Stage));
        Report->SetArrayField(TEXT("checks"), Checks);
        Report->SetArrayField(TEXT("dev_test"), DevRows);
        Report->SetNumberField(TEXT("second_generation"), SecondGeneration);
        Report->SetObjectField(TEXT("match1"), FirstMatchRow.IsValid() ? FirstMatchRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
        Report->SetObjectField(TEXT("opening"), OpeningRow.IsValid() ? OpeningRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
        Report->SetObjectField(TEXT("countdown"), CountdownRow.IsValid() ? CountdownRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
        Report->SetObjectField(TEXT("second_opening"),
            SecondOpeningRow.IsValid() ? SecondOpeningRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
        if (const ALDPlayerController* PC = Controller.Get())
        {
            Report->SetNumberField(TEXT("generation"), PC->OwnerState.Match);
            Report->SetStringField(TEXT("entity"), LDDemoGrowthScenario::GrowthProbeGuid(PC->OwnerState.ContenderId));
            Report->SetNumberField(TEXT("life"), PC->OwnerState.Life);
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
            Report->SetNumberField(TEXT("host_grant_xp"), double(HostGrantXP));
            Report->SetBoolField(TEXT("buyer_killed"), bBuyerKilled);
            Report->SetObjectField(TEXT("host_killer"), HostKillerRow.IsValid() ? HostKillerRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
            Report->SetNumberField(TEXT("host_dev_xp"), double(DevStep > 0 ? LDDemoGrowthScenario::GrowthProbeHostDevXP : 0));
        }
        else
        {
            Report->SetNumberField(TEXT("kills_planned"), KillsPlanned);
            Report->SetNumberField(TEXT("kills"), Kills);
            Report->SetArrayField(TEXT("kill_rows"), KillRows);
            Report->SetObjectField(TEXT("choice"), ChoiceRow.IsValid() ? ChoiceRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
            Report->SetObjectField(TEXT("replacement"), ReplaceRow.IsValid() ? ReplaceRow : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));
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
    Probe.SetStage(FLDDemoGrowthProbe::EStage::Choice);
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
