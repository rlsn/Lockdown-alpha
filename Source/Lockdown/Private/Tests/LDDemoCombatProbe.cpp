#include "Tests/LDDemoCombatProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/LDCharacter.h"
#include "Character/LDFlightTrailComponent.h"
#include "Combat/LDCombatCues.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDAttributeProjection.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Development/LDDevGrowthFixture.h"
#include "Development/LDDevelopmentDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Framework/LDCommandLiveness.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Navigation/LDPathSteering.h"
#include "Navigation/LDPointerFeedback.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDWorldViewRelay.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDHUD.h"
#include "UI/LDUIRoot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Camera/CameraActor.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

// Owned-connection combat and orders scenario (COMBAT_AND_ATTRIBUTES, CONTROLS_AND_TARGETING). Client1 acts only
// through its own connection and the calls its hotkeys make; the participating Listen Host stands still while it is
// attacked, audits those hits from its authority and then kills Client1 with an ordinary right-click order. Afterwards
// both learn approved skills from offers the Host's DEV growth fixture (-LDDevGrowthFixture) presets and grants XP
// for, choose them with the ordinary SubmitChoice and cast them through their hotkey entries. The Host renders its
// main view and keeps a capture of each observed skill moment for presentation review. The probe never calls a
// driver entry, an _Implementation or a Combat write. Named, not anonymous: unity builds merge files.
namespace LDDemoCombatScenario
{
constexpr int32 CombatProbeNavigateAttempts = 4;
constexpr int32 CombatProbeAttackOrders = 6;
// A ground order may stop this far from its point when another body stands near it.
constexpr double CombatProbeArrivalCm = 150.0;
// Client1 waits this far from the Host: inside sight, outside basic range, so the attack-move must close in.
constexpr double CombatProbeGapCm = 600.0;
// Bounded waits in seconds: one order or receipt; one walk or kill; the Host's sighting of the protected respawn.
constexpr double CombatProbeStepSeconds = 20.0;
constexpr double CombatProbeKillSeconds = 90.0;
constexpr double CombatProbeSightingSeconds = 1.5;
// A refused intent is watched this long to prove that protection does not come back.
constexpr double CombatProbeQuietSeconds = 1.5;
// Frames the owning client needs to apply a camera pan before it is measured.
constexpr double CombatProbeSettleSeconds = 0.5;
// Settings stays open past the order lease, so only a live heartbeat keeps the walk under it accepted.
constexpr double CombatProbeSettingsSeconds = LDCommandLiveness::Seconds + 0.5;
// Half-second heartbeats the open window must still carry, and the share of the walk the body must still cover.
constexpr uint32 CombatProbeSettingsHeartbeats = 4;
constexpr double CombatProbeSettingsProgress = 0.4;
// After Settings closes the same walk must still be running, or have arrived, with nothing replayed.
constexpr double CombatProbeSettingsAfterSeconds = 1.0;
// Chase-goal regression: fixed bearings and reach around the Host's site, a coarse step that cannot skip a body-wide
// off-mesh band, and the resolution of the edge search.
constexpr int32 CombatProbeChaseBearings = 16;
constexpr double CombatProbeChaseStepCm = 25.0;
constexpr double CombatProbeChaseReachCm = 3000.0;
constexpr double CombatProbeChaseResolutionCm = 0.5;
// Skills: Client1 waits this far from the Host's site on the open duel line: inside the snare's range and beyond the
// reach-carded basic range even when both bodies stop at the far edge of their arrival slack, so the reach card's
// chase must close in before its hit lands. Client1 throws its arc bolt from its farther duel site, so the flight
// spans several 100 ms observation publications the Host can render.
constexpr double CombatProbeSkillGapCm = 420.0;
// A finished ground order stands within Navigation's 40 cm goal acceptance of its point; this adds slack.
constexpr double CombatProbeSkillArrivalCm = 60.0;
// Client1's Shift-queue walk to the skill site: a detour waypoint at one of these fractions of the straight walk and this
// far off it (either side; the first with legal ground and complete routes both ways wins), passed within the pass
// radius before the queued site.
constexpr double CombatProbeQueueAlong[] = {.5, .35, .65, .2, .8};
constexpr double CombatProbeQueueDetourCm[] = {320.0, -320.0, 220.0, -220.0, 150.0, -150.0};
constexpr double CombatProbeQueuePassCm = 90.0;
// The queued walk starts at Client1's random respawn point, up to the map's width from the skill site, with a detour.
constexpr double CombatProbeQueueWalkSeconds = 40.0;
// Root, shield, bolt and basic-hit facts each arrive within this window of their cause.
constexpr double CombatProbeSkillSeconds = 3.0;
// The chalk capture waits until the drawn flight is this far from Client1's feet, clear of its cast orb.
constexpr double CombatProbeFlightClearCm = 120.0;
// Control feel: the body has moved once it left its order's starting point by this much; the Host cruises this long
// at full speed and needs this much walk left before its measured S.
constexpr double CombatProbeFeelMovedCm = 2.0;
constexpr double CombatProbeFeelCruiseSeconds = 0.15;
constexpr double CombatProbeFeelStopRoomCm = 300.0;
// Below this ground speed a stopped body stands still.
constexpr double CombatProbeFeelStillCmPerSec = 5.0;
// The timed first walk waits for this long a run of frames none longer than the movement component's simulation step
// (MaxSimulationTimeStep), past the match's one-time first-frame stalls.
constexpr double CombatProbeFeelSettleSeconds = 0.5;
// The Host's ground click for the marker capture lands this far beside its duel site.
constexpr double CombatProbeBesideCm = 250.0;

// The fixture's preset cards each role learns, in level order; two attribute cards only fill each offer.
TArray<FName> CombatProbeSkillPlan(bool bHost)
{
    return bHost ? TArray<FName>{FName(TEXT("attr_reach")), FName(TEXT("active_binding_snare"))}
        : TArray<FName>{FName(TEXT("active_guard_pulse")), FName(TEXT("active_arc_bolt"))};
}

TArray<FName> CombatProbePreset(FName Card)
{
    return {Card, FName(TEXT("attr_might")), FName(TEXT("attr_focus"))};
}

double CombatProbeRational(const FLDDemoRational& Value)
{
    return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : -1.0;
}

FString CombatProbeGuid(const FGuid& Id)
{
    return Id.ToString(EGuidFormats::DigitsWithHyphensLower);
}

// The viewer's current authorized observation frame, or null while it is unavailable or stale.
const FLDWorldView* CombatProbeFrame(const ALDPlayerController& Viewer)
{
    const ALDWorldViewRelay* Relay = Viewer.LocalView();
    if (!Relay) { return nullptr; }
    const FLDObservedFrameRead FrameRead = Relay->ReadObservedFrame();
    return FrameRead.Status == ELDObservedViewStatus::Current ? FrameRead.Frame : nullptr;
}

const FLDObservedUnitView* CombatProbeContender(const FLDWorldView& View, const FGuid& ContenderId)
{
    return View.Units.FindByPredicate([&ContenderId](const FLDObservedUnitView& Candidate)
    {
        return Candidate.Kind == ELDObservedUnitKind::Contender && Candidate.ContenderId == ContenderId;
    });
}

const FLDObservedStatus* CombatProbeProtection(const TArray<FLDObservedStatus>& Statuses)
{
    const uint8 Protection = uint8(ELDDemoStatusId::RespawnProtection);
    return Statuses.FindByPredicate([Protection](const FLDObservedStatus& Cue) { return Cue.StatusId == Protection; });
}

const FLDObservedStatus* CombatProbeStatus(const TArray<FLDObservedStatus>& Statuses, ELDDemoStatusId Id)
{
    return Statuses.FindByPredicate([Id](const FLDObservedStatus& Cue) { return Cue.StatusId == uint8(Id); });
}

const FLDCombatStatusView* CombatProbeOwnStatus(const FLDCombatOwnerView& View, ELDDemoStatusId Id)
{
    return View.Statuses.FindByPredicate([Id](const FLDCombatStatusView& Row) { return Row.StatusId == uint8(Id); });
}
}

class FLDDemoCombatProbe : public TSharedFromThis<FLDDemoCombatProbe>
{
public:
    enum class EStage : uint8
    {
        Join, Lobby, Spawned, ToSite,
        // Client1
        AttackMove, Engaged, Stopping, AwaitDeath, AwaitRespawn, Sighting, Spend, Settle, ToSkillSite, Rooted, Shielded,
        ToThrowSite, Throw, SkillsDone,
        // Host
        Struck, Quiet, Kill, KillWait, Respawn, Removal, Grant, Return, Snare, Rooting, ShieldSeen, BoltFlight, Reach,
        ReachHit, Wrap,
        // Both
        Learn
    };

    TWeakObjectPtr<ALDPlayerController> Controller;
    TWeakObjectPtr<UWorld> World;
    FString Output, Role, Error, CatalogDigest;
    double Deadline = 0, StageAt = 0, NextCommandAt = 0, NextReadyAt = 0, BlockedSince = 0;
    EStage Stage = EStage::Join;
    bool Passed = true, Written = false, bJoined = false, bLobbyConfigured = false, bStartIssued = false, bSitesReady = false;
    bool bPresetsStaged = false;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TArray<TSharedPtr<FJsonValue>> Checks;

    // Deterministic public duel sites beside the shop, outside its reach and the capture circle.
    FVector HostSite = FVector::ZeroVector, ClientSite = FVector::ZeroVector;
    double RespawnSeconds = 0;
    FGuid HostId, ClientId;
    int32 NavigateAttempts = 0, AttackOrders = 0;

    // Client1, on its first walk: Settings opened over the accepted order suppresses new world commands only.
    enum class ESettingsStep : uint8 { Await, Open, Closed, Done };
    ESettingsStep SettingsStep = ESettingsStep::Await;
    FLDMoveIdentity SettingsOrder;
    FVector SettingsFrom = FVector::ZeroVector;
    uint32 SettingsHeartbeats = 0, SettingsCombatReceipts = 0;
    double SettingsAt = 0;
    FGuid DiagnosticsFixture=FGuid::NewGuid();
    // Client1: the attack-move, its stop and the spent protection.
    FLDMoveIdentity AttackMoveOrder, StopOrder;
    int64 HostHpAtOrder = 0, ProtectedUntilSeenMs = 0;
    bool bEngagedSeen = false, bHitSeen = false;
    uint64 LifeRevisionBefore = 0;
    uint32 ReceiptSerialBefore = 0;
    FLDCombatActionReceipt SpentReceipt;
    // Client1 while dead: the minimap pan its static camera must follow.
    FVector DeathPanPoint = FVector::ZeroVector;
    double DeathPanAt = 0;
    bool bDeathCameraChecked = false;

    // Host: the hits it took, its kill and the audited respawn.
    bool bChaseGoalChecked = false;
    int64 HpAtStopP2 = -1, BasicIntervalMs = 0, RespawnAtMs = 0, ProtectedUntilMs = 0;
    double StopSampledAt = 0;
    FLDMoveIdentity KillOrder;
    FString RespawnAnchor;

    // Skills, both roles: the preset cards learned so far and the choice in flight.
    int32 SkillsLearned = 0;
    uint64 ChoiceRequest = 0;
    // Client1: the skill site on the open duel line, the snare's root deadline and the guard pulse's shield.
    FVector SkillSite = FVector::ZeroVector;
    int64 RootUntilMs = 0;
    // Host: the fixture grants, then each cast's baseline on the struck authority body and what the casts showed.
    uint64 HostGrant = 0, ClientGrant = 0;
    int64 HitBeforeMs = 0, PoolBeforeP2 = 0, FlightFromMs = 0;
    FGuid BoltKey;
    FString FlightError;
    int32 ProjectileSamples = 0;
    bool bBoltHeading = true, bCastCueSeen = false, bBoltLanded = false;
    double SkillGapCm = 0, ReachHitCm = 0;
    FLDMoveIdentity ReachOrder;
    // Client1, on its walk to the skill site: a ground order to a detour waypoint, then a Shift-queued order to the
    // site over the owning connection. The queued step must wait for the first one's arrival, then run to the site.
    enum class EQueueStep : uint8 { Send, Walking, Done };
    EQueueStep QueueStep = EQueueStep::Send;
    FLDMoveIdentity QueueFirst, QueueSecond;
    FVector QueueWaypoint = FVector::ZeroVector;
    double QueueNearestCm = TNumericLimits<double>::Max();

    // Host: rendered evidence. One capture of the main view with its UI at a time; the engine's screenshot delegate
    // hands over the frame it read back, which is kept only while that very frame still shows the moment.
    struct FShot
    {
        FString File;                     // name in the evidence directory
        TFunction<bool(FString&)> Shows;  // the moment is on the Host's current frame; fills what it shows
        double Deadline = 0;
        int32 ReadBacks = 0;
        bool bSaved = false;
        FString Facts, Problem;
    };
    TSharedPtr<FShot> Shot;
    FDelegateHandle ShotHandle;
    bool bShotTicking = false, bRenderWarm = false, bStruckSeen = false, bReachMeasured = false;
    TArray<TSharedPtr<FJsonValue>> Screenshots;

    // Control feel, measured and reported (not compared to a target): both roles' first right-click walk, ordered once
    // the match's first frames have settled, from the order to the body's first motion and full speed, the Host's S
    // from full speed to standing still and its next walk, and Client1's S to its server Stopped receipt. Sampled every
    // engine frame.
    struct FFeel
    {
        double MaxSpeed = 0;
        double SentAt = -1, FirstMoveAt = -1, FullSpeedAt = -1;
        double StopAt = -1, StoppedAt = -1, StopSpeed = 0, StopDistanceCm = -1;
        double RestartAt = -1, RestartMoveAt = -1;
        double CancelAt = -1, CancelDoneAt = -1;
        double FirstFrameAt = -1, LastFrameAt = -1;
        int32 Frames = 0;
        // From the first alive frame: frames run steady since SteadySince (no frame over the settle bound). The first
        // walk's window counts its frames and its longest one, so a stall inside it shows beside the latency.
        double SpawnAt = -1, SteadySince = -1, ClickLongestFrameMs = 0;
        int32 ClickFrames = 0;
        FVector From = FVector::ZeroVector, StopFrom = FVector::ZeroVector, RestartFrom = FVector::ZeroVector;
    };
    FFeel Feel;
    bool bFeelTicking = false;

    // Host pointer feedback through the ordinary right click, the DEV pointer standing in for the mouse: a ground click
    // beside its duel site (marker, then back to the site) and the first kill order on Client1's drawn body (hover
    // ring, then the attack marker).
    enum class EClickStep : uint8 { Aim, Shot, Next, Done };
    EClickStep MoveClick = EClickStep::Aim, KillClick = EClickStep::Aim;
    double MoveClickAt = 0;

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

    bool Save(const TSharedRef<FJsonObject>& Value, const FString& Name) const
    {
        FString Text;
        const FString File = FPaths::Combine(Output, Name);
        return FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Text))
            && FFileHelper::SaveStringToFile(Text, *(File + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*File, *(File + TEXT(".tmp")), true, true);
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
        ALDGameMode* Mode = Current->GetAuthGameMode<ALDGameMode>();
        if (IsHost() && Stage > EStage::Lobby && (!Mode || !Mode->Combat))
        {
            return Fail(TEXT("Net"), TEXT("The Listen Host lost its authoritative game mode or Combat owner"));
        }
        switch (Stage)
        {
        case EStage::Join: return TickJoin(*Current, *PC, Lobby, Driver);
        case EStage::Lobby: return TickLobby(Now, *PC, Lobby, Mode);
        case EStage::Spawned: return TickSpawned(*PC, *Registry, Lobby);
        case EStage::ToSite: return TickToSite(Now, *PC);
        case EStage::AttackMove: return TickAttackMove(Now, *PC);
        case EStage::Engaged: return TickEngaged(Now, *PC);
        case EStage::Stopping: return TickStopping(Now, *PC);
        case EStage::AwaitDeath: return TickAwaitDeath(Now, *PC);
        case EStage::AwaitRespawn: return TickAwaitRespawn(Now, *PC);
        case EStage::Sighting: return TickSighting(Now, *PC);
        case EStage::Spend: return TickSpend(Now, *PC);
        case EStage::Settle: return TickSettle(Now, *PC);
        case EStage::ToSkillSite: return TickToSkillSite(Now, *PC);
        case EStage::Rooted: return TickRooted(Now, *PC);
        case EStage::Shielded: return TickShielded(Now, *PC);
        case EStage::ToThrowSite: return TickToThrowSite(Now, *PC);
        case EStage::Throw: return TickThrow(Now, *PC);
        case EStage::SkillsDone: return TickSkillsDone(Now, *PC);
        case EStage::Struck: return TickStruck(Now, *PC, *Mode);
        case EStage::Quiet: return TickQuiet(Now, *PC, *Mode);
        case EStage::Kill: return TickKill(Now, *PC);
        case EStage::KillWait: return TickKillWait(Now, *PC, *Mode, *Public);
        case EStage::Respawn: return TickRespawn(Now, *Mode, *Registry);
        case EStage::Removal: return TickRemoval(Now, *Mode);
        case EStage::Grant: return TickGrant(Now, *PC, *Mode);
        case EStage::Return: return TickReturn(Now, *PC);
        case EStage::Snare: return TickSnare(Now, *PC, *Mode);
        case EStage::Rooting: return TickRooting(Now, *PC, *Mode);
        case EStage::ShieldSeen: return TickShieldSeen(Now, *PC, *Mode);
        case EStage::BoltFlight: return TickBoltFlight(Now, *PC, *Mode);
        case EStage::Reach: return TickReach(Now, *PC, *Mode);
        case EStage::ReachHit: return TickReachHit(Now, *PC, *Mode);
        case EStage::Wrap: return TickWrap(*Public, *PC);
        case EStage::Learn: return TickLearn(Now, *PC);
        default: return Fail(TEXT("Net"), TEXT("The scenario reached an unknown stage"));
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

    // The ordinary lobby through owning session RPCs: no AI seats, every human ready, the Host's normal Start.
    bool TickLobby(double Now, ALDPlayerController& PC, const FLDLobbyView& Lobby, ALDGameMode* Mode)
    {
        if (Lobby.Phase == ELDMatchPhase::Development || Lobby.Phase == ELDMatchPhase::CaptureOpen)
        {
            if (IsHost())
            {
                Check(bStartIssued, TEXT("Start"), TEXT("Play opened from the Host's ordinary lobby Start request"));
            }
            SetStage(EStage::Spawned);
            return Passed ? true : Finish();
        }
        if (Lobby.Phase == ELDMatchPhase::Starting) { return true; }
        if (Lobby.Phase != ELDMatchPhase::Lobby)
        {
            return Fail(TEXT("Start"), TEXT("The room left the Lobby without opening play: ")
                + StaticEnum<ELDMatchPhase>()->GetNameStringByValue(int64(Lobby.Phase)));
        }
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
            if (!Mark(TEXT("LobbyConfigured.json"))) { return Finish(); }
            bLobbyConfigured = true;
        }
        if (!Exists(TEXT("LobbyConfigured.json"))) { return true; }
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
        if (!bAllReady)
        {
            BlockedSince = 0;
            return true;
        }
        if (Lobby.CanStart)
        {
            BlockedSince = 0;
            // The opening offer is drawn when the match begins, so the presets are staged in the Lobby before Start.
            if (!bPresetsStaged)
            {
                for (const FLDLobbyMember& LobbyMember : Lobby.Members)
                {
                    (LobbyMember.Host ? HostId : ClientId) = LobbyMember.ContenderId;
                }
                if (!Mode || !QueuePresets(*Mode)) { return Finish(); }
                bPresetsStaged = true;
            }
            if (Now >= NextCommandAt)
            {
                PC.StartDemoMatch();
                bStartIssued = true;
                NextCommandAt = Now + 2;
            }
            return true;
        }
        if (BlockedSince == 0) { BlockedSince = Now; }
        if (Now - BlockedSince <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
        return Fail(TEXT("Start"), TEXT("The ordinary lobby Start stays refused with every human ready: ") + Lobby.StartBlocker);
    }

    bool TickSpawned(ALDPlayerController& PC, const ULDMapRegistry& Registry, const FLDLobbyView& Lobby)
    {
        if (!Cast<ALDCharacter>(PC.GetPawn()) || PC.OwnerState.LifePhase != ELDLifePhase::Alive || !PC.OwnerState.BodyRef.IsValid()
            || !PC.CanWorldCommand())
        {
            return true;
        }
        // The match's first frames on each process build and load what the match shows (one-time stalls of about a
        // hundred milliseconds). The timed first walk waits until this process's frames have settled, so it measures
        // the order's response rather than that stall; a run whose frames never settle walks anyway, after one step.
        const double Now = FPlatformTime::Seconds();
        if (Feel.SpawnAt < 0)
        {
            Feel.SpawnAt = Now;
            EnsureFeelTicking();
        }
        const bool bSettled = Feel.SteadySince >= 0 && Feel.LastFrameAt - Feel.SteadySince >= LDDemoCombatScenario::CombatProbeFeelSettleSeconds;
        if (!bSettled && Now - Feel.SpawnAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
        // Start with full pools and wait through the recovery delay: no HP/MP mutation can trigger publication.
        // Both the Listen Host and its real owning client must receive the changed effective rates anyway.
        const auto& RegenParameters=Catalog->Get().Parameters;
        if(Now-Feel.SpawnAt<double(RegenParameters.OutOfCombatRegenDelayMs)/1000.+.5){return true;}
        const auto& View=PC.CombatView;
        const auto HP=View.Attributes.FindByPredicate([](const FLDCombatAttributeRowView& Row){return Row.Attribute==uint8(ELDProjectedAttribute::HPRegen);});
        const auto MP=View.Attributes.FindByPredicate([](const FLDCombatAttributeRowView& Row){return Row.Attribute==uint8(ELDProjectedAttribute::MPRegen);});
        const double ExpectedHP=HP?LDDemoCombatScenario::CombatProbeRational(HP->Final)+double(View.MaxHPP2)*double(RegenParameters.OutOfCombatMaxHPRegenBp)/1000000.:-1;
        const double ExpectedMP=MP?LDDemoCombatScenario::CombatProbeRational(MP->Final)+double(View.MaxMPP2)*double(RegenParameters.OutOfCombatMaxMPRegenBp)/1000000.:-1;
        if(!Check(View.Generation==PC.OwnerState.Match&&View.Life==PC.OwnerState.Life&&View.bHasBody
            &&View.HPP2==View.MaxHPP2&&View.MPP2==View.MaxMPP2&&ExpectedHP>0&&ExpectedMP>0
            &&FMath::IsNearlyEqual(View.HPRegenPerSec,ExpectedHP)&&FMath::IsNearlyEqual(View.MPRegenPerSec,ExpectedMP),TEXT("Start"),
            TEXT("Full resources still publish effective HP/MP regen after the recovery delay on this owning connection"),
            FString::Printf(TEXT("hp_regen=%.3f expected=%.3f mp_regen=%.3f expected=%.3f"),View.HPRegenPerSec,ExpectedHP,View.MPRegenPerSec,ExpectedMP)))
        {return Finish();}
        const FGuid StagedHost = HostId, StagedClient = ClientId;
        if (!ResolveRoster(PC, Lobby) || !PrepareSites(Registry)) { return Finish(); }
        Check(PC.OwnerState.Life == 1 && PC.OwnerState.ProtectedUntilMs == 0, TEXT("Start"),
            TEXT("The first body is an unprotected initial life"));
        if (IsHost())
        {
            Check(StagedHost == HostId && StagedClient == ClientId, TEXT("Skills"),
                TEXT("The Lobby presets were staged for exactly this match's two contenders"));
        }
        if (!Passed) { return Finish(); }
        StartFeel(PC);
        PC.Navigation->Navigate(IsHost() ? HostSite : ClientSite);
        NavigateAttempts = 1;
        SetStage(EStage::ToSite);
        return true;
    }

    // Host, in the Lobby before Start: the fixture stages each role's first offers, the opening one included, one planned card per
    // offer. The growth domain does not exist before Start, so the fixture holds them until BeginMatch.
    bool QueuePresets(ALDGameMode& Mode)
    {
        FString QueueError;
        const UWorld* LobbyWorld = Mode.GetWorld();
        bool bQueued = LDDevGrowthFixture::IsSwitchPresent() && HostId.IsValid() && ClientId.IsValid();
        for (const FName& Card : LDDemoCombatScenario::CombatProbeSkillPlan(true))
        {
            bQueued = bQueued && LDDevGrowthFixture::StageOffer(LobbyWorld, HostId, LDDemoCombatScenario::CombatProbePreset(Card), QueueError);
        }
        for (const FName& Card : LDDemoCombatScenario::CombatProbeSkillPlan(false))
        {
            bQueued = bQueued && LDDevGrowthFixture::StageOffer(LobbyWorld, ClientId, LDDemoCombatScenario::CombatProbePreset(Card), QueueError);
        }
        return Check(bQueued, TEXT("Skills"),
            TEXT("The Host's DEV growth fixture (-LDDevGrowthFixture) stages both roles' opening and next offers in the Lobby"), QueueError);
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

    // Both roles derive the same two sites from public anchors and approved radii, then prove them legal.
    bool PrepareSites(const ULDMapRegistry& Registry)
    {
        if (bSitesReady) { return true; }
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        const FName ShopId = Defs.World.ShopIds.IsEmpty() ? FName() : FName(*Defs.World.ShopIds[0]);
        const FLDMapBinding* ShopAnchor = Registry.Resolve(ShopId, Registry.Generation);
        const FLDMapBinding* CaptureAnchor = Registry.Resolve(FName(*Defs.World.CaptureId), Registry.Generation);
        const double ShopReach = LDDemoCombatScenario::CombatProbeRational(Defs.Parameters.ShopInteractRadiusCm);
        const double CaptureReach = LDDemoCombatScenario::CombatProbeRational(Defs.Parameters.CaptureRadiusCm);
        const double BodyRadius = LDDemoCombatScenario::CombatProbeRational(Defs.Parameters.CollisionRadiusCm);
        const double AttackReach = LDDemoCombatScenario::CombatProbeRational(Defs.Parameters.AttackRangeCm.Base);
        const double SightReach = LDDemoCombatScenario::CombatProbeRational(Defs.Parameters.VisionRadiusCm);
        const double Gap = LDDemoCombatScenario::CombatProbeGapCm;
        RespawnSeconds = double(Defs.Parameters.RespawnMs) / 1000.0;
        if (!ShopAnchor || !CaptureAnchor || ShopReach <= 0 || CaptureReach <= 0 || BodyRadius <= 0 || AttackReach <= 0
            || RespawnSeconds <= 0 || Defs.Parameters.ProtectionMs <= 0
            || Gap <= AttackReach + 2.0 * BodyRadius || Gap >= SightReach - 2.0 * BodyRadius)
        {
            return Check(false, TEXT("Start"), TEXT("The public anchors and approved radii admit an out-of-range, in-sight duel"));
        }
        const FVector Outward = (ShopAnchor->Position - CaptureAnchor->Position).GetSafeNormal2D();
        const FVector Across(Outward.Y, -Outward.X, 0.0);
        HostSite = ShopAnchor->Position + Across * (2.0 * ShopReach);
        FCollisionShape Capsule;
        FString SiteError;
        const auto Legal = [&](const FVector& Site, FLDLegalGround& LegalSite)
        {
            ELDWorldQueryFailure SiteFailure = ELDWorldQueryFailure::None;
            bool bWithin = true;
            return Registry.QueryGround(Registry.Revision(), Site, Capsule, ShopAnchor->Surface, LegalSite, SiteFailure, SiteError)
                && Registry.QueryShopReach(Registry.Revision(), ShopId, Site, bWithin, SiteFailure, SiteError) && !bWithin
                && FVector::Dist2D(Site, CaptureAnchor->Position) > CaptureReach + 2.0 * BodyRadius;
        };
        FLDLegalGround HostGround;
        const bool bHostLegal = !Outward.IsNearlyZero() && ULDMapRegistry::ContentCapsule(*Catalog, Capsule, SiteError)
            && Legal(HostSite, HostGround);
        // Only Client1 walks to its own site: eight fixed bearings around the Host's site, the first with legal ground
        // and a near-straight complete route from the Host's site (open ground in sight) wins. Its skill site lies on
        // that open line, nearer the Host, and must be legal too.
        bool bClientLegal = IsHost();
        for (int32 Bearing = 0; bHostLegal && !bClientLegal && Bearing < 8; ++Bearing)
        {
            const FVector Direction = Across.RotateAngleAxis(45.0 * Bearing, FVector::UpVector);
            ClientSite = HostSite + Direction * Gap;
            SkillSite = HostSite + Direction * LDDemoCombatScenario::CombatProbeSkillGapCm;
            FLDLegalGround ClientGround, SkillGround;
            FLDCompletePath Route;
            ELDWorldQueryFailure RouteFailure = ELDWorldQueryFailure::None;
            bClientLegal = Legal(ClientSite, ClientGround) && Legal(SkillSite, SkillGround)
                && Registry.QueryCompletePath(Registry.Revision(), HostGround, ClientGround, Capsule, Route, RouteFailure, SiteError)
                && Route.LengthCm <= 1.2 * Gap;
        }
        bSitesReady = bHostLegal && bClientLegal;
        return Check(bSitesReady, TEXT("Start"),
            TEXT("The duel sites are legal ground outside the shop reach and the capture circle, joined by an open complete route"),
            FString::Printf(TEXT("host=%s client=%s skill=%s %s"), *HostSite.ToCompactString(), *ClientSite.ToCompactString(),
                *SkillSite.ToCompactString(), *SiteError));
    }

    bool TickToSite(double Now, ALDPlayerController& PC)
    {
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        if (!Body || PC.OwnerState.LifePhase != ELDLifePhase::Alive || PC.OwnerState.Life != 1)
        {
            return Fail(TEXT("Start"), TEXT("The first life ended before the duel although nobody attacks yet"));
        }
        const FVector& Site = IsHost() ? HostSite : ClientSite;
        if (!IsHost() && SettingsStep != ESettingsStep::Done)
        {
            bool bHandled = true;
            const bool bContinue = TickSettings(Now, PC, *Body, bHandled);
            if (bHandled) { return bContinue; }
        }
        // The Host's walk, once at full speed and well short of its site: S from full speed, measured to standing still.
        if (IsHost() && Feel.StopAt < 0 && Feel.FullSpeedAt >= 0 && Now - Feel.FullSpeedAt >= LDDemoCombatScenario::CombatProbeFeelCruiseSeconds
            && FVector::Dist2D(Body->GetNavAgentLocation(), Site) >= LDDemoCombatScenario::CombatProbeFeelStopRoomCm)
        {
            Feel.StopAt = Now;
            Feel.StopFrom = Body->GetActorLocation();
            Feel.StopSpeed = Body->GetVelocity().Size2D();
            PC.StopCombatAndOrders();
            return true;
        }
        if (PC.Navigation->HasMoveOrder()) { return true; }
        if (FVector::Dist2D(Body->GetNavAgentLocation(), Site) <= LDDemoCombatScenario::CombatProbeArrivalCm)
        {
            if (IsHost() && !bChaseGoalChecked)
            {
                bChaseGoalChecked = true;
                if (!CheckChaseGoal(PC)) { return Finish(); }
            }
            // The first read-back of a rendered view can stall the game thread for seconds, so the Host takes it here,
            // before any timed moment, and keeps it as its view of the duel site.
            if (IsHost() && !bRenderWarm)
            {
                if (!Shot)
                {
                    if (!Check(FApp::CanEverRender() && GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWorld() == PC.GetWorld(),
                        TEXT("Render"), TEXT("The participating Listen Host renders its main view (run with an RHI, e.g. -RenderOffScreen)")))
                    {
                        return Finish();
                    }
                    BeginShot(TEXT("HostSees-DuelSite.png"), [this](FString& Facts) { return ShowsOwn(ELDObservedActionCue::None, 0.f, 0.f, Facts); },
                        LDDemoCombatScenario::CombatProbeStepSeconds);
                    return true;
                }
                if (ShotPending()) { return true; }
                if (!ShotSaved(TEXT("its own body at the duel site"))) { return Finish(); }
                bRenderWarm = true;
            }
            if (IsHost() && MoveClick != EClickStep::Done) { return TickMoveClick(Now, PC, *Body, Site); }
            if (!Mark(Role + TEXT("AtSite.json"))) { return Finish(); }
            SetStage(IsHost() ? EStage::Struck : EStage::AttackMove);
            return true;
        }
        if (Now - StageAt < 1.0) { return true; }
        if (NavigateAttempts >= LDDemoCombatScenario::CombatProbeNavigateAttempts)
        {
            return Fail(TEXT("Start"), TEXT("The owned body could not reach its public duel site: ") + PC.IntentFeedback);
        }
        if (Feel.StopAt >= 0 && Feel.RestartAt < 0)
        {
            Feel.RestartAt = Now;
            Feel.RestartFrom = Body->GetActorLocation();
        }
        PC.Navigation->Navigate(Site);
        ++NavigateAttempts;
        StageAt = Now;
        return true;
    }

    // Deterministic chase-goal regression on the real Demo world and navmesh. Along fixed bearings from the Host's public
    // duel site (anchors and approved radii only, never where a body stopped), the first legal foot just outside the
    // eroded navmesh edge stands where a target pressed against a wall or another body, or cutting a corner, stands. As
    // a clicked ground goal it projects to the nearest reachable navmesh stand (#80); as a chased body's goal (the planner
    // of AttackUnit admission and chase replans) it routes to its nearest navmesh point within one capsule radius.
    bool CheckChaseGoal(const ALDPlayerController& PC)
    {
        const ULDMapRegistry* Registry = PC.GetWorld()->GetSubsystem<ULDMapRegistry>();
        auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(PC.GetWorld());
        const ANavigationData* Data = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
        const FName Surface = PC.Navigation->GroundSurfaceHint();
        FCollisionShape Capsule;
        FLDLegalGround HostGround, Start;
        ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
        FString Problem;
        if (!Registry || !Data || Surface.IsNone() || !ULDMapRegistry::ContentCapsule(*Catalog, Capsule, Problem)
            || !Registry->QueryGround(Registry->Revision(), HostSite, Capsule, Surface, HostGround, Failure, Problem)
            || !LDPathSteering::PlanStart(*Registry, HostGround, Capsule, Start, Failure, Problem))
        {
            return Check(false, TEXT("ChaseGoal"), TEXT("The Host's duel site has registered ground and navigation for the chase-goal check"), Problem);
        }
        const double HalfHeight = Capsule.GetCapsuleHalfHeight();
        const auto OnMesh = [Nav, Data, HalfHeight](const FVector& Point)
        {
            FNavLocation Projected;
            return Nav->ProjectPointToNavigation(Point, Projected, FVector(0.1, 0.1, HalfHeight), Data)
                && FVector::DistSquared2D(Projected.Location, Point) <= UE_KINDA_SMALL_NUMBER;
        };
        if (!OnMesh(Start.SurfaceCm))
        {
            return Check(false, TEXT("ChaseGoal"), TEXT("The Host's planned start lies on the navmesh"), Start.SurfaceCm.ToCompactString());
        }
        FVector Margin = FVector::ZeroVector;
        bool bFound = false;
        for (int32 Bearing = 0; Bearing < LDDemoCombatScenario::CombatProbeChaseBearings && !bFound; ++Bearing)
        {
            const FVector Direction = FVector::ForwardVector.RotateAngleAxis(
                360.0 * Bearing / LDDemoCombatScenario::CombatProbeChaseBearings, FVector::UpVector);
            double Inside = 0, Outside = -1;
            for (double Along = LDDemoCombatScenario::CombatProbeChaseStepCm; Along <= LDDemoCombatScenario::CombatProbeChaseReachCm;
                Along += LDDemoCombatScenario::CombatProbeChaseStepCm)
            {
                if (!OnMesh(Start.SurfaceCm + Direction * Along)) { Outside = Along; break; }
                Inside = Along;
            }
            if (Outside < 0) { continue; }
            while (Outside - Inside > LDDemoCombatScenario::CombatProbeChaseResolutionCm)
            {
                const double Mid = 0.5 * (Inside + Outside);
                (OnMesh(Start.SurfaceCm + Direction * Mid) ? Inside : Outside) = Mid;
            }
            // Off the mesh yet legal for the capsule: inside Recast's margin, not past a floor edge or into a wall.
            const FVector Candidate = Start.SurfaceCm + Direction * Outside;
            FLDLegalGround Legal;
            ELDWorldQueryFailure CandidateFailure = ELDWorldQueryFailure::None;
            FString CandidateError;
            if (Registry->QueryGround(Registry->Revision(), Candidate, Capsule, Surface, Legal, CandidateFailure, CandidateError))
            {
                Margin = Candidate;
                bFound = true;
            }
        }
        if (!bFound)
        {
            return Check(false, TEXT("ChaseGoal"), TEXT("A legal foot just outside the eroded navmesh edge exists around the Host's site"),
                Start.SurfaceCm.ToCompactString());
        }
        FLDCompletePath Strict, Chase;
        FLDLegalGround StrictGoal, ChaseGoal;
        ELDWorldQueryFailure StrictFailure = ELDWorldQueryFailure::None, ChaseFailure = ELDWorldQueryFailure::None;
        FString StrictError, ChaseError;
        const bool bStrict = LDPathSteering::PlanRoute(*Registry, HostGround, Margin, Surface, LDPathSteering::EGoal::Ground,
            Capsule, Strict, StrictGoal, StrictFailure, StrictError);
        const bool bChase = LDPathSteering::PlanRoute(*Registry, HostGround, Margin, Surface, LDPathSteering::EGoal::Body,
            Capsule, Chase, ChaseGoal, ChaseFailure, ChaseError);
        const FString Where = TEXT("foot=") + Margin.ToCompactString();
        Check(bStrict && !Strict.Points.IsEmpty() && OnMesh(StrictGoal.SurfaceCm)
            && FVector::Dist2D(StrictGoal.SurfaceCm, Margin) <= Capsule.GetCapsuleRadius(),
            TEXT("ChaseGoal"), TEXT("A legal foot just outside the eroded navmesh edge, clicked as ground, projects to a reachable navmesh stand within one capsule radius"),
            Where + TEXT(" planned=") + StrictGoal.SurfaceCm.ToCompactString() + TEXT(" ") + StrictError);
        const bool bChaseOk = Check(bChase && !Chase.Points.IsEmpty() && FVector::Dist2D(ChaseGoal.SurfaceCm, Margin) <= Capsule.GetCapsuleRadius(),
            TEXT("ChaseGoal"),
            TEXT("The same foot as a chased body's goal routes completely to its nearest navmesh point within one capsule radius"),
            Where + TEXT(" planned=") + ChaseGoal.SurfaceCm.ToCompactString() + TEXT(" ") + ChaseError);
        // #97: a click on a trunk lands beside that trunk. The navmesh around a forest has thousands of polygons, and a
        // projection that ranks only some of them sends the body to an unrelated spot across the map.
        const FLDDemoWorldBlocker* Tree = nullptr;
        double TreeDistance = DBL_MAX;
        FVector TreeCentre = FVector::ZeroVector;
        for (const FLDDemoWorldBlocker& Blocker : Registry->GetGeometry()->Blockers)
        {
            FVector Centre;
            FString Ignored;
            if (Blocker.Shape != ELDDemoWorldBlockerShape::Cylinder || !Blocker.CenterCm.ToVector(Centre, Ignored)) { continue; }
            const double Distance = FVector::Dist2D(Centre, HostSite);
            if (Distance < TreeDistance) { Tree = &Blocker; TreeDistance = Distance; TreeCentre = Centre; }
        }
        if (!Tree)
        {
            return Check(false, TEXT("ChaseGoal"), TEXT("The world has a tree trunk to click for the trunk-click check"), HostSite.ToCompactString());
        }
        const double TrunkRadius = double(Tree->RadiusCm.Numerator) / double(Tree->RadiusCm.Denominator);
        FLDCompletePath Around;
        FLDLegalGround AroundGoal;
        ELDWorldQueryFailure AroundFailure = ELDWorldQueryFailure::None;
        FString AroundError;
        const bool bAround = LDPathSteering::PlanRoute(*Registry, HostGround, TreeCentre, Surface, LDPathSteering::EGoal::Ground,
            Capsule, Around, AroundGoal, AroundFailure, AroundError);
        const double Beside = TrunkRadius + 4 * Capsule.GetCapsuleRadius();
        return Check(bAround && !Around.Points.IsEmpty() && FVector::Dist2D(AroundGoal.SurfaceCm, TreeCentre) <= Beside, TEXT("ChaseGoal"),
            TEXT("A ground click on the trunk nearest the Host projects to a reachable stand beside that trunk"),
            FString::Printf(TEXT("tree=%s centre=%s planned=%s distance=%.0f limit=%.0f %s"), *Tree->Id, *TreeCentre.ToCompactString(),
                *AroundGoal.SurfaceCm.ToCompactString(), FVector::Dist2D(AroundGoal.SurfaceCm, TreeCentre), Beside, *AroundError)) && bChaseOk;
    }

    // Client1 opens Settings through its production entry over the accepted first walk and holds it past the order
    // lease (UI_AND_SETTINGS 4, 7). bHandled stays false only while that walk is not accepted yet.
    bool TickSettings(double Now, ALDPlayerController& PC, const ALDCharacter& Body, bool& bHandled)
    {
        ULDNavigationComponent& Nav = *PC.Navigation;
        const FLDOrderReceipt& Latest = Nav.LatestReceipt();
        const bool bOpen = PC.ControlsUI && PC.ControlsUI->IsOpen();
        const auto Running = [&Latest, this](bool bArrived)
        {
            return Latest.Identity == SettingsOrder && (Latest.Status == ELDOrderStatus::Activated
                || Latest.Status == ELDOrderStatus::Progress || (bArrived && Latest.Status == ELDOrderStatus::Completed));
        };
        if (SettingsStep == ESettingsStep::Await)
        {
            if (!Nav.HasMoveOrder() || Latest.Identity != Nav.LocalIdentity()
                || (Latest.Status != ELDOrderStatus::Activated && Latest.Status != ELDOrderStatus::Progress))
            {
                bHandled = false;
                return true;
            }
            const double Speed = LDDemoCombatScenario::CombatProbeRational(Catalog->Get().Parameters.MoveSpeedCmPerSec.Base);
            const double Needed = Speed * (LDDemoCombatScenario::CombatProbeSettingsSeconds + LDDemoCombatScenario::CombatProbeSettleSeconds);
            const double Remaining = FVector::Dist2D(Body.GetNavAgentLocation(), ClientSite);
            if (Speed <= 0 || Remaining < Needed || !PC.RootUI)
            {
                return Fail(TEXT("Settings"), FString::Printf(
                    TEXT("The accepted first walk cannot hold Settings open over it (remaining=%.0f needed=%.0f ui=%d)"),
                    Remaining, Needed, int32(PC.RootUI != nullptr)));
            }
            SettingsOrder = Nav.LocalIdentity();
            SettingsFrom = Body.GetNavAgentLocation();
            SettingsHeartbeats = Nav.HeartbeatsSent();
            SettingsCombatReceipts = PC.CombatReceipts.Serial();
            PC.Controls=CastChecked<ULDGameInstance>(PC.GetGameInstance())->UseIsolatedLocalSettings(DiagnosticsFixture);
            // The entry the HUD and menu 设置 buttons use.
            PC.RootUI->ToggleMainPanel(ELDUIMainPanel::Settings);
            Check(PC.ControlsUI && PC.ControlsUI->IsOpen() && PC.IsWorldLifeValid() && PC.IsWorldInputSuppressed()
                && !PC.CanWorldCommand(), TEXT("Settings"),
                TEXT("Settings opens over the accepted walk: the life stays valid and only new world commands are suppressed"));
            // The diagnostics live on the interface page; it is chosen through its production tab, and each toggle must then
            // be on screen, not merely present in the tree.
            auto* InterfaceTab=PC.ControlsUI->WidgetTree->FindWidget<UButton>(TEXT("TabInterface"));
            if (!Check(InterfaceTab && InterfaceTab->GetIsEnabled(),TEXT("Settings"),TEXT("The production interface tab is available")))
            { return Finish(); }
            InterfaceTab->OnClicked.Broadcast();
            const auto Shown=[&PC](const UWidget* Widget)
            {
                if (!Widget || !PC.ControlsUI->IsVisible()) { return false; }
                for (const UWidget* Current=Widget; Current; Current=Current->GetParent())
                {
                    if (Current->GetVisibility()==ESlateVisibility::Collapsed || Current->GetVisibility()==ESlateVisibility::Hidden) { return false; }
                }
                return true;
            };
            for (const FName Name : {FName(TEXT("ToggleNetworkDiagnostics")),FName(TEXT("ToggleFPS"))})
            {
                auto* Button=PC.ControlsUI->WidgetTree->FindWidget<UButton>(Name);
                if (!Check(Button && Button->GetIsEnabled() && Shown(Button),TEXT("Settings"),TEXT("The production diagnostics toggle is available on its page"),Name.ToString()))
                { return Finish(); }
                Button->OnClicked.Broadcast();
            }
            const auto Loaded=ULDControlSettings::LoadLocalSettings(FLDLocalSettingsStorage::Automation(DiagnosticsFixture));
            Check(PC.Controls->bShowNetworkDiagnostics && PC.Controls->bShowFPS
                && Loaded.Settings && Loaded.Status==ELDLocalSettingsLoadStatus::Loaded
                && Loaded.Settings->bShowNetworkDiagnostics && Loaded.Settings->bShowFPS,
                TEXT("Settings"),TEXT("Both real diagnostic buttons apply and persist independently of the display draft"));
            // A ground order and S from under Settings stay local: neither replaces nor stops the walk.
            Nav.Navigate(SettingsFrom);
            PC.StopCombatAndOrders();
            Check(Nav.LocalIdentity() == SettingsOrder && Nav.HasMoveOrder(), TEXT("Settings"),
                TEXT("A ground order and S issued under Settings are refused locally and leave the accepted walk running"));
            if (!Passed) { return Finish(); }
            SettingsAt = Now;
            SettingsStep = ESettingsStep::Open;
            return true;
        }
        if (SettingsStep == ESettingsStep::Open)
        {
            if (!bOpen || !Nav.HasMoveOrder() || Nav.LocalIdentity() != SettingsOrder || !Running(false))
            {
                return Fail(TEXT("Settings"), FString::Printf(TEXT("Settings ended or stopped the accepted walk (open=%d status=%d failure=%d): %s"),
                    int32(bOpen), int32(Latest.Status), int32(Latest.Failure), *Nav.Status));
            }
            if (Now - SettingsAt < LDDemoCombatScenario::CombatProbeSettingsSeconds) { return true; }
            if (!FParse::Param(FCommandLine::Get(),TEXT("nullrhi")))
            {
                const auto* Diagnostics=PC.HUDUI && PC.HUDUI->WidgetTree
                    ? PC.HUDUI->WidgetTree->FindWidget<UTextBlock>(TEXT("NetworkDiagnosticsText")) : nullptr;
                const FString Metrics=Diagnostics ? Diagnostics->GetText().ToString() : FString();
                Check(Diagnostics && Metrics.Contains(TEXT("FPS ")) && Metrics.Contains(TEXT("RTT "))
                    && Metrics.Contains(TEXT("ms")) && Metrics.Contains(TEXT("%")),
                    TEXT("Settings"),TEXT("A real owning connection renders FPS, RTT, jitter and packet loss while walking"),Metrics);
            }
            const double Speed = LDDemoCombatScenario::CombatProbeRational(Catalog->Get().Parameters.MoveSpeedCmPerSec.Base);
            const uint32 Beats = Nav.HeartbeatsSent() - SettingsHeartbeats;
            const double Moved = FVector::Dist2D(Body.GetNavAgentLocation(), SettingsFrom);
            Check(Beats >= LDDemoCombatScenario::CombatProbeSettingsHeartbeats
                && Moved >= LDDemoCombatScenario::CombatProbeSettingsProgress * Speed * LDDemoCombatScenario::CombatProbeSettingsSeconds,
                TEXT("Settings"),
                TEXT("Held open past the order lease, Settings keeps the heartbeat: the same walk stays accepted and the body keeps moving"),
                FString::Printf(TEXT("heartbeats=%u moved_cm=%.0f open_s=%.2f"), Beats, Moved, Now - SettingsAt));
            if (!Passed) { return Finish(); }
            PC.RootUI->ToggleMainPanel(ELDUIMainPanel::Settings);
            SettingsHeartbeats = Nav.HeartbeatsSent();
            SettingsAt = Now;
            SettingsStep = ESettingsStep::Closed;
            return true;
        }
        if (Now - SettingsAt < LDDemoCombatScenario::CombatProbeSettingsAfterSeconds) { return true; }
        Check(PC.ControlsUI && !bOpen && Nav.LocalIdentity() == SettingsOrder && Running(true)
            && PC.CombatReceipts.Serial() == SettingsCombatReceipts && (!Nav.HasMoveOrder() || Nav.HeartbeatsSent() > SettingsHeartbeats),
            TEXT("Settings"), TEXT("Closing Settings replays nothing: the same walk goes on with its heartbeat and no order, stop or action is sent"),
            FString::Printf(TEXT("open=%d status=%d active=%d"), int32(bOpen), int32(Latest.Status), int32(Nav.HasMoveOrder())));
        if (!Passed) { return Finish(); }
        SettingsStep = ESettingsStep::Done;
        return true;
    }

    // ---- Client1: attack-move, S, death, the protected respawn and the empty Q, only through this connection. ----

    // The A confirmation on the Host's ground point; the server alone picks the nearest legal visible candidate.
    bool TickAttackMove(double Now, ALDPlayerController& PC)
    {
        if (!Exists(TEXT("HostAtSite.json")))
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeKillSeconds) { return true; }
            return Fail(TEXT("AttackMove"), TEXT("The Host never reached its duel site"));
        }
        const FLDWorldView* Frame = LDDemoCombatScenario::CombatProbeFrame(PC);
        const FLDObservedUnitView* Host = Frame ? LDDemoCombatScenario::CombatProbeContender(*Frame, HostId) : nullptr;
        if (!Host || Host->Health.MaxP2 <= 0)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
            return Fail(TEXT("AttackMove"), TEXT("The Host never became visible with its health from the duel site"));
        }
        HostHpAtOrder = Host->Health.CurrentP2;
        PC.Navigation->AttackMove(HostSite, PC.Navigation->GroundSurfaceHint());
        AttackMoveOrder = PC.Navigation->LocalIdentity();
        SetStage(EStage::Engaged);
        return true;
    }

    bool TickEngaged(double Now, ALDPlayerController& PC)
    {
        const FLDOrderReceipt& Latest = PC.Navigation->LatestReceipt();
        if (Latest.Identity == AttackMoveOrder)
        {
            if (Latest.Status == ELDOrderStatus::Rejected || Latest.Status == ELDOrderStatus::Cancelled
                || Latest.Status == ELDOrderStatus::Completed)
            {
                return Fail(TEXT("AttackMove"), TEXT("The attack-move ended before engaging the Host: ") + PC.Navigation->Status);
            }
            bEngagedSeen |= Latest.Kind == ELDSpatialOrderKind::AttackMove && Latest.bEngaged;
        }
        const FLDWorldView* Frame = LDDemoCombatScenario::CombatProbeFrame(PC);
        const FLDObservedUnitView* Host = Frame ? LDDemoCombatScenario::CombatProbeContender(*Frame, HostId) : nullptr;
        bHitSeen |= Host && Host->Health.CurrentP2 < HostHpAtOrder;
        if (bEngagedSeen && bHitSeen)
        {
            Check(true, TEXT("AttackMove"),
                TEXT("The attack-move acquired the nearest visible contender, closed in, engaged it and its hits show in this connection's view"));
            PC.StopCombatAndOrders();
            StopOrder = PC.Navigation->LocalIdentity();
            Feel.CancelAt = FPlatformTime::Seconds();
            SetStage(EStage::Stopping);
            return true;
        }
        if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
        return Fail(TEXT("AttackMove"), FString::Printf(TEXT("No engaged attack-move with observed hits (engaged=%d hit=%d): %s"),
            int32(bEngagedSeen), int32(bHitSeen), *PC.Navigation->Status));
    }

    // The server answers both the stopped order and the stop request; the stop's own receipt arrives last.
    bool TickStopping(double Now, ALDPlayerController& PC)
    {
        const FLDOrderReceipt& Latest = PC.Navigation->LatestReceipt();
        const bool bStopped = !PC.Navigation->HasMoveOrder() && Latest.Identity == StopOrder
            && Latest.Status == ELDOrderStatus::Cancelled && Latest.Failure == ELDOrderFailure::Stopped;
        if (!bStopped)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
            return Fail(TEXT("Stop"), TEXT("S did not stop the engaged attack-move: ") + PC.Navigation->Status);
        }
        Check(Latest.Status == ELDOrderStatus::Cancelled && Latest.Failure == ELDOrderFailure::Stopped, TEXT("Stop"),
            TEXT("S ends the engaged attack-move with a server Stopped receipt for this connection"), PC.Navigation->Status);
        if (!Passed || !Mark(TEXT("Client1Stopped.json"))) { return Finish(); }
        SetStage(EStage::AwaitDeath);
        return true;
    }

    // Client1 stays passive: no order, no action, until the Host's right-click kill lands.
    bool TickAwaitDeath(double Now, ALDPlayerController& PC)
    {
        if (PC.OwnerState.Life == 1 && PC.OwnerState.LifePhase == ELDLifePhase::Dead)
        {
            Check(PC.OwnerState.RespawnAtMs > 0 && PC.OwnerState.ProtectedUntilMs == 0 && !PC.IsWorldLifeValid()
                && !PC.CanWorldCommand() && PC.IsAwaitingRespawn(), TEXT("Death"),
                TEXT("The owner projection reports its own death with a respawn instant: no valid life, no world command"),
                FString::Printf(TEXT("respawn_at_ms=%lld"), PC.OwnerState.RespawnAtMs));
            if (!Passed) { return Finish(); }
            // The minimap's camera gesture (what a left drag sends); the Host's public duel site is static ground.
            DeathPanPoint = HostSite;
            PC.Navigation->PanTo(DeathPanPoint);
            DeathPanAt = Now;
            SetStage(EStage::AwaitRespawn);
            return true;
        }
        if (PC.OwnerState.Life != 1 || PC.OwnerState.LifePhase != ELDLifePhase::Alive)
        {
            return Fail(TEXT("Death"), TEXT("The first life left Alive other than by the Host's kill"));
        }
        if (Now - StageAt <= 2.0 * LDDemoCombatScenario::CombatProbeKillSeconds) { return true; }
        return Fail(TEXT("Death"), TEXT("The Host never killed this owned body"));
    }

    bool TickAwaitRespawn(double Now, ALDPlayerController& PC)
    {
        if (!bDeathCameraChecked && PC.OwnerState.LifePhase == ELDLifePhase::Dead)
        {
            if (Now - DeathPanAt < LDDemoCombatScenario::CombatProbeSettleSeconds) { return true; }
            const ACameraActor* View = Cast<ACameraActor>(PC.GetViewTarget());
            const float ArmLength = PC.Controls ? PC.Controls->Zoom : -1.f;
            const FVector Expected = ALDCharacter::DefaultArmCameraPose(DeathPanPoint, ArmLength).GetLocation();
            Check(View && ArmLength > 0 && !PC.GetPawn() && !PC.IsWorldLifeValid() && PC.CanCameraCommand()
                && View->GetActorLocation().Equals(Expected, 1.0),
                TEXT("DeathCamera"), TEXT("A minimap pan moves the dead owner's static camera over the map, with no body or world command"),
                View ? FString::Printf(TEXT("camera=%s expected=%s arm=%.0f"), *View->GetActorLocation().ToCompactString(),
                    *Expected.ToCompactString(), ArmLength) : FString(TEXT("the view target is not a camera")));
            bDeathCameraChecked = true;
            if (!Passed) { return Finish(); }
        }
        const bool bViewCurrent = PC.CombatView.Generation == PC.OwnerState.Match && PC.CombatView.Life == PC.OwnerState.Life;
        if (PC.OwnerState.Life != 2 || PC.OwnerState.LifePhase != ELDLifePhase::Alive || !Cast<ALDCharacter>(PC.GetPawn())
            || !PC.CanWorldCommand() || !bViewCurrent)
        {
            if (Now - StageAt <= RespawnSeconds + LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
            return Fail(TEXT("Respawn"), TEXT("The owned contender did not return as a second commandable life after the approved delay"));
        }
        const FLDCombatEntryView* QEntry = PC.CombatView.Entries.FindByPredicate([](const FLDCombatEntryView& Row)
        {
            return Row.Entry == ELDCombatEntry::Q;
        });
        Check(PC.OwnerState.ProtectedUntilMs > 0, TEXT("Respawn"), TEXT("The second life arrives protected in its own projection"),
            FString::Printf(TEXT("protected_until_ms=%lld"), PC.OwnerState.ProtectedUntilMs));
        Check(bDeathCameraChecked && PC.GetViewTarget() == PC.GetPawn(), TEXT("DeathCamera"),
            TEXT("The respawned body takes the camera back from the static death camera"));
        Check(!QEntry || QEntry->DefinitionId.IsNone(), TEXT("Protection"),
            TEXT("Q holds no learned skill, so pressing it is an empty-slot activation attempt"));
        if (!Passed) { return Finish(); }
        ProtectedUntilSeenMs = PC.OwnerState.ProtectedUntilMs;
        LifeRevisionBefore = PC.OwnerState.LifeRevision;
        SetStage(EStage::Sighting);
        return true;
    }

    // The Host records the protected respawn from its authority before Client1 spends it.
    bool TickSighting(double Now, ALDPlayerController& PC)
    {
        if (!Exists(TEXT("HostSawProtection.json")))
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeSightingSeconds) { return true; }
            return Fail(TEXT("Respawn"), TEXT("The Host did not confirm the protected respawn within its window"));
        }
        int64 ServerNowMs = 0;
        if (!PC.ReadDemoAuthorityTimeMs(ServerNowMs) || ServerNowMs >= ProtectedUntilSeenMs)
        {
            return Fail(TEXT("Protection"), FString::Printf(
                TEXT("The protection window closed before the scenario could spend it (now=%lld until=%lld)"), ServerNowMs, ProtectedUntilSeenMs));
        }
        ReceiptSerialBefore = PC.CombatReceipts.Serial();
        // The Q hotkey's own entry: an empty slot still reaches the server as an activation attempt.
        PC.ActivateCombatEntry(ELDCombatEntry::Q);
        SetStage(EStage::Spend);
        return true;
    }

    bool TickSpend(double Now, ALDPlayerController& PC)
    {
        const FLDCombatActionReceipt* Answer = PC.CombatReceipts.FindLatestSince(ReceiptSerialBefore,
            [](const FLDCombatActionReceipt& Receipt) { return Receipt.Entry == ELDCombatEntry::Q && Receipt.Life == 2; });
        if (!Answer)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
            return Fail(TEXT("Protection"), TEXT("The empty Q attempt received no receipt over the owning connection"));
        }
        if (SpentReceipt.Entry == ELDCombatEntry::None)
        {
            const FLDCombatActionReceipt& Seen = *Answer;
            SpentReceipt = Seen;
            Check(Seen.Origin == ELDCombatRequestOrigin::Direct && Seen.Phase == ELDCombatPhase::Rejected
                && Seen.Reject == ELDCombatReject::SourceUnavailable, TEXT("Protection"),
                TEXT("The empty-slot Q reaches the server and is refused there as an empty slot"),
                FString::Printf(TEXT("phase=%d reject=%d"), int32(Seen.Phase), int32(Seen.Reject)));
            Check(Seen.bProtectionRemoved && Seen.LifeRevision > LifeRevisionBefore, TEXT("Protection"),
                TEXT("The admitted aggressive intent ends protection although its action is refused"),
                FString::Printf(TEXT("removed=%d life_revision=%llu before=%llu"), int32(Seen.bProtectionRemoved),
                    Seen.LifeRevision, LifeRevisionBefore));
            if (!Passed) { return Finish(); }
        }
        if (PC.OwnerState.ProtectedUntilMs != 0 || PC.OwnerState.LifeRevision < SpentReceipt.LifeRevision)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
            return Fail(TEXT("Protection"), TEXT("The owner projection never published the removed protection"));
        }
        SetStage(EStage::Settle);
        return true;
    }

    bool TickSettle(double Now, ALDPlayerController& PC)
    {
        if (Now - StageAt < LDDemoCombatScenario::CombatProbeQuietSeconds) { return true; }
        Check(PC.OwnerState.Life == 2 && PC.OwnerState.LifePhase == ELDLifePhase::Alive && PC.OwnerState.ProtectedUntilMs == 0,
            TEXT("Protection"), TEXT("A refused action never restores protection on the same life"));
        if (!Passed) { return Finish(); }
        SetStage(EStage::Learn);
        return true;
    }

    // ---- Both: the fixture's preset card from each head offer, taken with the ordinary SubmitChoice. ----

    bool TickLearn(double Now, ALDPlayerController& PC)
    {
        const TArray<FName> Plan = LDDemoCombatScenario::CombatProbeSkillPlan(IsHost());
        const FName Wanted = Plan[SkillsLearned];
        const FLDProgressionView* View = PC.ReadOwnProgression();
        if (ChoiceRequest != 0)
        {
            const bool bLearned = View && (View->Skills.ContainsByPredicate([&Wanted](const FLDLearnedSkillView& Row)
            {
                return Row.DefinitionId == Wanted && Row.Rank == 1;
            }) || View->AttributeCards.ContainsByPredicate([&Wanted](const FLDAttributeCardView& Row)
            {
                return Row.DefinitionId == Wanted && Row.Count == 1;
            }));
            if (!bLearned)
            {
                if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
                return Fail(TEXT("Skills"), TEXT("The chosen preset card never reached the owner build: ") + Wanted.ToString());
            }
            ChoiceRequest = 0;
            StageAt = Now;
            if (++SkillsLearned < Plan.Num()) { return true; }
            Check(true, TEXT("Skills"), TEXT("Every preset card was offered on its level and learned through the ordinary choice"),
                FString::JoinBy(Plan, TEXT(","), [](const FName& Card) { return Card.ToString(); }));
            NavigateAttempts = 0;
            SetStage(IsHost() ? EStage::Return : EStage::ToSkillSite);
            return true;
        }
        // The next head offer arrives with the build that consumed the previous one, or after the fixture's grant.
        const FLDChoiceOption* Card = View && View->bHasOffer && View->bCanChoose
            ? View->Offer.Options.FindByPredicate([&Wanted](const FLDChoiceOption& Option) { return Option.DefinitionId == Wanted; })
            : nullptr;
        if (!Card)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
            FString Cards;
            for (const FLDChoiceOption& Option : View && View->bHasOffer ? View->Offer.Options : TArray<FLDChoiceOption>())
            {
                Cards += Option.DefinitionId.ToString() + TEXT(" ");
            }
            return Fail(TEXT("Skills"), FString::Printf(TEXT("No choosable head offer carries the preset card %s (offer: %s)"),
                *Wanted.ToString(), *Cards));
        }
        FLDChoiceCommand Command;
        Command.OfferId = View->Offer.OfferId;
        Command.OfferRevision = View->Offer.OfferRevision;
        Command.OptionId = Card->OptionId;
        Command.ExpectedBuildRevision = View->BuildRevision;
        Command.CatalogDigest = View->CatalogDigest;
        ELDGrowthReason LocalReason = ELDGrowthReason::None;
        ChoiceRequest = PC.SubmitChoice(Command, LocalReason);
        if (!Check(ChoiceRequest != 0 && LocalReason == ELDGrowthReason::None, TEXT("Skills"),
            TEXT("The owning connection submits the preset card as an ordinary choice"), Wanted.ToString()))
        {
            return Finish();
        }
        StageAt = Now;
        return true;
    }

    // A ground walk to Site with bounded retries; bArrived once the body stands within Tolerance of it.
    bool Walk(double Now, ALDPlayerController& PC, const FVector& Site, double Tolerance, bool& bArrived)
    {
        bArrived = false;
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        if (!Body || PC.OwnerState.LifePhase != ELDLifePhase::Alive)
        {
            return Fail(TEXT("Skills"), TEXT("The owned body ended its life during a skill-stage walk"));
        }
        if (PC.Navigation->HasMoveOrder() || (NavigateAttempts > 0 && Now - StageAt < 1.0)) { return true; }
        if (FVector::Dist2D(Body->GetNavAgentLocation(), Site) <= Tolerance)
        {
            bArrived = true;
            return true;
        }
        if (NavigateAttempts >= LDDemoCombatScenario::CombatProbeNavigateAttempts)
        {
            return Fail(TEXT("Skills"), TEXT("The owned body could not reach its skill-stage site: ") + PC.Navigation->Status);
        }
        PC.Navigation->Navigate(Site);
        ++NavigateAttempts;
        StageAt = Now;
        return true;
    }

    // The learned Q/W entry holding Definition, or None while the owner view does not show it yet.
    ELDCombatEntry EntryOf(const ALDPlayerController& PC, FName Definition) const
    {
        const bool bCurrent = PC.CombatView.Generation == PC.OwnerState.Match && PC.CombatView.Life == PC.OwnerState.Life;
        const FLDCombatEntryView* Row = bCurrent ? PC.CombatView.Entries.FindByPredicate([&Definition](const FLDCombatEntryView& Entry)
        {
            return Entry.DefinitionId == Definition && Entry.SourceKind == ELDCombatSourceKind::Learned;
        }) : nullptr;
        return Row ? Row->Entry : ELDCombatEntry::None;
    }

    // ---- Client1: rooted by the Host's snare, it shields and cleanses itself with its own Q. ----

    // Shift queue over the real owning connection (2026-11-06 decision): bHandled stays true until the queued walk ended.
    bool TickQueuedWalk(double Now, ALDPlayerController& PC, bool& bHandled)
    {
        bHandled = true;
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        if (!Body || PC.OwnerState.LifePhase != ELDLifePhase::Alive)
        {
            return Fail(TEXT("Queue"), TEXT("The owned body ended its life during the queued walk"));
        }
        const FVector Foot = Body->GetNavAgentLocation();
        if (QueueStep == EQueueStep::Send)
        {
            if (PC.Navigation->HasMoveOrder()) { return true; }
            const auto* Registry = PC.GetWorld()->GetSubsystem<ULDMapRegistry>();
            FCollisionShape Capsule;
            FString QueueError;
            FLDLegalGround From, Detour, To;
            ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
            const FName Surface = PC.Navigation->GroundSurfaceHint();
            const FVector Span(SkillSite.X - Foot.X, SkillSite.Y - Foot.Y, 0.0);
            const FVector Across = FVector(Span.Y, -Span.X, 0.0).GetSafeNormal();
            bool bLegal = false;
            if (Registry && !Across.IsNearlyZero() && ULDMapRegistry::ContentCapsule(*Catalog, Capsule, QueueError)
                && Registry->QueryGround(Registry->Revision(), Foot, Capsule, Surface, From, Failure, QueueError)
                && Registry->QueryGround(Registry->Revision(), SkillSite, Capsule, Surface, To, Failure, QueueError))
            {
                FString Tried;
                for (int32 Index = 0; !bLegal && Index < UE_ARRAY_COUNT(LDDemoCombatScenario::CombatProbeQueueAlong); ++Index)
                {
                    for (const double Offset : LDDemoCombatScenario::CombatProbeQueueDetourCm)
                    {
                        QueueWaypoint = Foot + Span * LDDemoCombatScenario::CombatProbeQueueAlong[Index] + Across * Offset;
                        QueueWaypoint.Z = Foot.Z;
                        FLDCompletePath First, Second;
                        FString Why;
                        if (Registry->QueryGround(Registry->Revision(), QueueWaypoint, Capsule, Surface, Detour, Failure, Why)
                            && Registry->QueryCompletePath(Registry->Revision(), From, Detour, Capsule, First, Failure, Why)
                            && Registry->QueryCompletePath(Registry->Revision(), Detour, To, Capsule, Second, Failure, Why))
                        {
                            QueueWaypoint = Detour.SurfaceCm;
                            bLegal = true;
                            break;
                        }
                        Tried += FString::Printf(TEXT("[%s %s] "), *QueueWaypoint.ToCompactString(), *Why);
                    }
                }
                if (!bLegal) { QueueError = Tried; }
            }
            if (!Check(bLegal, TEXT("Queue"), TEXT("A legal detour waypoint off the straight walk to the skill site exists"),
                FString::Printf(TEXT("from=%s site=%s %s"), *Foot.ToCompactString(), *SkillSite.ToCompactString(), *QueueError)))
            {
                return Finish();
            }
            PC.Navigation->Navigate(QueueWaypoint);
            QueueFirst = PC.Navigation->LocalIdentity();
            PC.Navigation->Navigate(SkillSite, true);
            QueueSecond = PC.Navigation->LocalIdentity();
            const ALDPointerFeedback* Feedback = PC.Navigation->PointerFeedback();
            if (!Check(QueueSecond.Order == QueueFirst.Order + 1 && PC.Navigation->QueuedMarkCount() == 1
                && Feedback && Feedback->ShownWaypoints() == 1, TEXT("Queue"),
                TEXT("A Shift order is sent behind the current one and shows its waypoint")))
            {
                return Finish();
            }
            QueueStep = EQueueStep::Walking;
            StageAt = Now;
            return true;
        }
        QueueNearestCm = FMath::Min(QueueNearestCm, FVector::Dist2D(Foot, QueueWaypoint));
        const FLDOrderReceipt& Latest = PC.Navigation->LatestReceipt();
        const bool bEnded = Latest.Status == ELDOrderStatus::Rejected || Latest.Status == ELDOrderStatus::Cancelled;
        if (Latest.Identity == QueueFirst && bEnded)
        {
            return Fail(TEXT("Queue"), FString::Printf(TEXT("The first order ended early (failure %u): the queued one replaced it"),
                uint32(Latest.Failure)));
        }
        if (Latest.Identity == QueueSecond && bEnded)
        {
            return Fail(TEXT("Queue"), FString::Printf(TEXT("The queued order ended without arriving (failure %u)"), uint32(Latest.Failure)));
        }
        if (Latest.Identity == QueueSecond && Latest.Status == ELDOrderStatus::Completed)
        {
            const ALDPointerFeedback* Feedback = PC.Navigation->PointerFeedback();
            Check(QueueNearestCm <= LDDemoCombatScenario::CombatProbeQueuePassCm && PC.Navigation->QueuedMarkCount() == 0
                && Feedback && Feedback->ShownWaypoints() == 0, TEXT("Queue"),
                TEXT("The queued order waited for the first one's arrival at its waypoint, then completed at the skill site"),
                FString::Printf(TEXT("nearest_waypoint_cm=%.1f"), QueueNearestCm));
            if (!Passed) { return Finish(); }
            QueueStep = EQueueStep::Done;
            bHandled = false;
            return true;
        }
        if (Now - StageAt <= LDDemoCombatScenario::CombatProbeQueueWalkSeconds) { return true; }
        return Fail(TEXT("Queue"), TEXT("The queued walk did not finish: ") + PC.Navigation->Status);
    }

    bool TickToSkillSite(double Now, ALDPlayerController& PC)
    {
        if (QueueStep != EQueueStep::Done)
        {
            bool bHandled = true;
            const bool bContinue = TickQueuedWalk(Now, PC, bHandled);
            if (bHandled) { return bContinue; }
        }
        bool bArrived = false;
        if (!Walk(Now, PC, SkillSite, LDDemoCombatScenario::CombatProbeSkillArrivalCm, bArrived)) { return false; }
        if (!bArrived) { return true; }
        if (!Mark(TEXT("Client1AtSkillSite.json"))) { return Finish(); }
        SetStage(EStage::Rooted);
        return true;
    }

    bool TickRooted(double Now, ALDPlayerController& PC)
    {
        const FLDCombatStatusView* Root = LDDemoCombatScenario::CombatProbeOwnStatus(PC.CombatView, ELDDemoStatusId::Root);
        if (RootUntilMs == 0)
        {
            if (!Root || PC.CombatView.Life != PC.OwnerState.Life)
            {
                if (Now - StageAt <= LDDemoCombatScenario::CombatProbeKillSeconds) { return true; }
                return Fail(TEXT("Skills"), TEXT("The Host's snare never rooted this owned body"));
            }
            RootUntilMs = Root->ExpiresAtMs;
            Check(PC.CombatView.bRooted && (Root->ControlBlocks & uint8(ELDCombatControlBlock::Move)) != 0, TEXT("Skills"),
                TEXT("The snare (active_binding_snare) roots this owned body: its own Combat projection shows the root blocking movement"),
                FString::Printf(TEXT("root_until_ms=%lld"), RootUntilMs));
            if (!Passed) { return Finish(); }
            StageAt = Now;
        }
        // The Host must observe the root before it is cleansed.
        if (!Exists(TEXT("HostSawRoot.json")))
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeSkillSeconds) { return true; }
            return Fail(TEXT("Skills"), TEXT("The Host did not confirm the observed root in time"));
        }
        const ELDCombatEntry Guard = EntryOf(PC, LDDemoCombatScenario::CombatProbeSkillPlan(false)[0]);
        if (Guard == ELDCombatEntry::None) { return Fail(TEXT("Skills"), TEXT("The learned guard pulse holds no Q/W entry")); }
        ReceiptSerialBefore = PC.CombatReceipts.Serial();
        // A Self skill sends from its hotkey entry at once.
        PC.ActivateCombatEntry(Guard);
        SetStage(EStage::Shielded);
        return true;
    }

    bool TickShielded(double Now, ALDPlayerController& PC)
    {
        if (const FLDCombatActionReceipt* Seen = PC.CombatReceipts.FindLatestSince(ReceiptSerialBefore,
            [](const FLDCombatActionReceipt& Receipt) { return Receipt.Phase == ELDCombatPhase::Rejected; }))
        {
            return Fail(TEXT("Skills"), FString::Printf(TEXT("The guard pulse was refused (reject=%d)"), int32(Seen->Reject)));
        }
        const FLDCombatStatusView* Shield = LDDemoCombatScenario::CombatProbeOwnStatus(PC.CombatView, ELDDemoStatusId::Shield);
        const FLDCombatStatusView* Root = LDDemoCombatScenario::CombatProbeOwnStatus(PC.CombatView, ELDDemoStatusId::Root);
        if (!Shield || Root || PC.CombatView.ShieldP2 <= 0)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeSkillSeconds) { return true; }
            return Fail(TEXT("Skills"), TEXT("The guard pulse never showed its shield without the root in this owner's projection"));
        }
        // The shield and the dispel are one release: the shield's start must precede the root's own end.
        const FLDDemoAbilityDefinition* Guard = nullptr;
        FString GuardError;
        int64 ShieldMs = 0;
        if (Catalog->FindAbility(LDDemoCombatScenario::CombatProbeSkillPlan(false)[0].ToString(), Guard, GuardError) && Guard
            && Guard->Ranks.Num() > 0)
        {
            for (const FLDDemoEffectDefinition& Effect : Guard->Ranks[0].Effects)
            {
                ShieldMs = Effect.GetOp() == ELDDemoEffectOp::Shield ? Effect.DurationMs : ShieldMs;
            }
        }
        const int64 ShieldFromMs = Shield->ExpiresAtMs - ShieldMs;
        Check(ShieldMs > 0 && ShieldFromMs < RootUntilMs && !PC.CombatView.bRooted, TEXT("Skills"),
            TEXT("The guard pulse (active_guard_pulse) shields this body and dispels the snare's root before the root would have ended"),
            FString::Printf(TEXT("shield_p2=%lld shield_from_ms=%lld root_until_ms=%lld %s"), PC.CombatView.ShieldP2, ShieldFromMs,
                RootUntilMs, *GuardError));
        if (!Passed || !Mark(TEXT("Client1Shielded.json"))) { return Finish(); }
        NavigateAttempts = 0;
        SetStage(EStage::ToThrowSite);
        return true;
    }

    // Once the Host has captured the shield, Client1 walks back to its farther duel site to throw from there.
    bool TickToThrowSite(double Now, ALDPlayerController& PC)
    {
        if (!Exists(TEXT("HostSawShield.json")))
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
            return Fail(TEXT("Skills"), TEXT("The Host did not confirm the observed shield in time"));
        }
        bool bArrived = false;
        if (!Walk(Now, PC, ClientSite, LDDemoCombatScenario::CombatProbeSkillArrivalCm, bArrived)) { return false; }
        if (bArrived) { SetStage(EStage::Throw); }
        return true;
    }

    // The arc bolt flies along this owner's aim at the Host's observed feet.
    bool TickThrow(double Now, ALDPlayerController& PC)
    {
        const FLDWorldView* Frame = LDDemoCombatScenario::CombatProbeFrame(PC);
        const FLDObservedUnitView* Target = Frame ? LDDemoCombatScenario::CombatProbeContender(*Frame, HostId) : nullptr;
        const ELDCombatEntry Bolt = EntryOf(PC, LDDemoCombatScenario::CombatProbeSkillPlan(false)[1]);
        const FLDCombatEntryView* Row = PC.CombatView.Entries.FindByPredicate([Bolt](const FLDCombatEntryView& Entry) { return Entry.Entry == Bolt; });
        const ALDCharacter* Own = Cast<ALDCharacter>(PC.GetPawn());
        if (!Target || !Row || Row->Unavailable != ELDCombatReject::None || !Own)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeSkillSeconds) { return true; }
            return Fail(TEXT("Skills"), FString::Printf(TEXT("The arc bolt never became castable at a visible Host (unavailable=%d)"),
                Row ? int32(Row->Unavailable) : -1));
        }
        const FVector Direction = (FVector(Target->FeetCm) - ALDCombatDirector::FootOf(*Own)).GetSafeNormal2D();
        if (Direction.IsNearlyZero())
        {
            return Fail(TEXT("Skills"), TEXT("Client1 has no planar aim at the Host's observed feet"));
        }
        FLDCombatTarget Aim;
        Aim.Kind = ELDCombatTargetKind::Direction;
        Aim.Direction = Direction;
        if (!ArmAndConfirm(PC, Bolt, Aim)) { return Finish(); }
        SetStage(EStage::SkillsDone);
        return true;
    }

    // Client1 stands still while its bolt lands and the Host's reach-carded basic attack follows, then reports on its
    // second life.
    bool TickSkillsDone(double Now, ALDPlayerController& PC)
    {
        if (const FLDCombatActionReceipt* Refused = Refusal(PC))
        {
            return Fail(TEXT("Skills"), FString::Printf(TEXT("The arc bolt was refused (reject=%d)"), int32(Refused->Reject)));
        }
        if (!Exists(TEXT("HostSkillsDone.json")))
        {
            if (PC.OwnerState.Life == 2 && PC.OwnerState.LifePhase == ELDLifePhase::Alive
                && Now - StageAt <= LDDemoCombatScenario::CombatProbeKillSeconds)
            {
                return true;
            }
            return Fail(TEXT("Skills"), TEXT("The Host's checks did not finish while this second life stood at its duel site"));
        }
        return Finish();
    }

    // ---- Host: stands still while attacked, audits the hits, kills Client1 and audits its respawn. ----

    const FLDBodyCombatState* AuthorityState(const ALDGameMode& Mode, const ALDPlayerController* Owner) const
    {
        const ALDCharacter* Body = Owner ? Cast<ALDCharacter>(Owner->GetPawn()) : nullptr;
        return Body && Mode.Combat ? Mode.Combat->StateFor(Body) : nullptr;
    }

    bool TickStruck(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        const FLDBodyCombatState* Own = AuthorityState(Mode, &PC);
        if (!Own || PC.OwnerState.LifePhase != ELDLifePhase::Alive || PC.OwnerState.Life != 1)
        {
            return Fail(TEXT("AttackMove"), TEXT("The Host's first life ended during the attack-move"));
        }
        // The own body's reaction to the remote basic hit is captured from the frame the hit lands on.
        if (!Shot && !bStruckSeen)
        {
            BeginShot(TEXT("HostSees-OwnHit.png"), [this](FString& Facts) { return ShowsOwn(ELDObservedActionCue::Hit, 0.f, .25f, Facts); },
                2.0 * LDDemoCombatScenario::CombatProbeKillSeconds);
        }
        if (!bStruckSeen && Own->Resources.HpP2 < Own->Resources.MaxHpP2)
        {
            Check(true, TEXT("AttackMove"), TEXT("The Host's authority body takes the remote attack-move's basic hits"),
                FString::Printf(TEXT("hp_p2=%lld/%lld"), Own->Resources.HpP2, Own->Resources.MaxHpP2));
            bStruckSeen = true;
            // A later hit of the same attack-move still serves; after that the capture has missed the moment.
            if (Shot) { Shot->Deadline = FMath::Min(Shot->Deadline, FPlatformTime::Seconds() + LDDemoCombatScenario::CombatProbeSkillSeconds); }
        }
        if (bStruckSeen)
        {
            if (ShotPending()) { return true; }
            if (!ShotSaved(TEXT("its own body reacting to a remote basic hit"))) { return Finish(); }
            SetStage(EStage::Quiet);
            return true;
        }
        if (Now - StageAt <= 2.0 * LDDemoCombatScenario::CombatProbeKillSeconds) { return true; }
        return Fail(TEXT("AttackMove"), TEXT("The remote attack-move never damaged the Host"));
    }

    bool TickQuiet(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        if (!Exists(TEXT("Client1Stopped.json")))
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeKillSeconds) { return true; }
            return Fail(TEXT("Stop"), TEXT("Client1 never reported its Stop"));
        }
        const FLDBodyCombatState* Own = AuthorityState(Mode, &PC);
        if (!Own) { return Fail(TEXT("Stop"), TEXT("The Host lost its body while the stop was checked")); }
        if (StopSampledAt == 0)
        {
            const ALDPlayerController* Remote = Mode.FindContenderController(ClientId);
            const FLDBodyCombatState* Attacker = AuthorityState(Mode, Remote);
            Check(Remote && Attacker && !Remote->Navigation->IsAuthorityActive() && !Attacker->Action.IsSet(), TEXT("Stop"),
                TEXT("S on the owning connection cleared its authority order and every unreleased basic attack"));
            if (!Passed) { return Finish(); }
            BasicIntervalMs = Attacker->BasicTiming.IntervalMs;
            HpAtStopP2 = Own->Resources.HpP2;
            StopSampledAt = Now;
            return true;
        }
        if (Now - StopSampledAt < 2.0 * double(BasicIntervalMs) / 1000.0 + 0.5) { return true; }
        Check(Own->Resources.HpP2 >= HpAtStopP2, TEXT("Stop"),
            TEXT("After S no further hit lands for two attack intervals; only regeneration may raise the Host's HP"),
            FString::Printf(TEXT("before_p2=%lld after_p2=%lld interval_ms=%lld"), HpAtStopP2, Own->Resources.HpP2, BasicIntervalMs));
        if (!Passed) { return Finish(); }
        SetStage(EStage::Kill);
        return true;
    }

    // The ordinary right-click intent on the observer handle in the Host's own frame.
    bool TickKill(double Now, ALDPlayerController& PC)
    {
        const FLDWorldView* Frame = LDDemoCombatScenario::CombatProbeFrame(PC);
        const FLDObservedUnitView* Victim = Frame ? LDDemoCombatScenario::CombatProbeContender(*Frame, ClientId) : nullptr;
        if (!Victim)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
            return Fail(TEXT("Death"), TEXT("Client1 was not visible to the Host after the stop"));
        }
        if (AttackOrders >= LDDemoCombatScenario::CombatProbeAttackOrders)
        {
            return Fail(TEXT("Death"), TEXT("The Host's attack lock kept ending without a death: ") + PC.Navigation->Status);
        }
        if (KillClick != EClickStep::Done) { return TickKillClick(PC, *Victim); }
        PC.Navigation->AttackUnit(Victim->Handle, Victim->ObservationEpoch);
        KillOrder = PC.Navigation->LocalIdentity();
        ++AttackOrders;
        SetStage(EStage::KillWait);
        return true;
    }

    bool TickKillWait(double Now, ALDPlayerController& PC, ALDGameMode& Mode, const ALDGameState& Public)
    {
        const ALDPlayerController* Remote = Mode.FindContenderController(ClientId);
        if (!Remote) { return Fail(TEXT("Death"), TEXT("Client1's server controller disappeared")); }
        const FLDOwnerState& Victim = Remote->OwnerState;
        const FLDOrderReceipt& Latest = PC.Navigation->LatestReceipt();
        if (Victim.Life == 1 && Victim.LifePhase == ELDLifePhase::Dead)
        {
            const FLDKillNotice* Notice = nullptr;
            for (const FLDKillNotice& Row : Public.KillFeed.Notices)
            {
                if (Row.Victim == ClientId) { Notice = &Row; }
            }
            const bool bOrderEnded = Latest.Identity == KillOrder && Latest.Failure == ELDOrderFailure::TargetDied;
            if (!Notice || !bOrderEnded)
            {
                if (Now - StageAt <= LDDemoCombatScenario::CombatProbeKillSeconds) { return true; }
                return Fail(TEXT("Death"), FString::Printf(TEXT("The death facts never completed (feed=%d order=%s)"),
                    int32(Notice != nullptr), *PC.Navigation->Status));
            }
            const int64 Delay = Catalog->Get().Parameters.RespawnMs;
            Check(Notice->Killer == HostId && Notice->Cause == ELDKillCause::Contender, TEXT("Death"),
                TEXT("The public kill feed credits the Host contender with the right-click kill"));
            Check(Victim.RespawnAtMs == Notice->OccurredAtMs + Delay, TEXT("Death"),
                TEXT("The death schedules exactly the approved respawn delay"),
                FString::Printf(TEXT("died_ms=%lld respawn_at_ms=%lld delay_ms=%lld"), Notice->OccurredAtMs, Victim.RespawnAtMs, Delay));
            Check(true, TEXT("Death"), TEXT("The right-click order ends with TargetDied when its target dies"));
            if (!Passed) { return Finish(); }
            RespawnAtMs = Victim.RespawnAtMs;
            SetStage(EStage::Respawn);
            return true;
        }
        if (Victim.Life != 1 || Victim.LifePhase != ELDLifePhase::Alive)
        {
            return Fail(TEXT("Death"), TEXT("Client1 left its first life other than by this kill"));
        }
        const bool bEnded = Latest.Identity == KillOrder && (Latest.Status == ELDOrderStatus::Rejected
            || Latest.Status == ELDOrderStatus::Cancelled || Latest.Status == ELDOrderStatus::Completed);
        if (bEnded)
        {
            // The server ended the lock without a death (lost sight, blocked path): seek the same victim again.
            SetStage(EStage::Kill);
            return true;
        }
        if (Now - StageAt <= LDDemoCombatScenario::CombatProbeKillSeconds) { return true; }
        return Fail(TEXT("Death"), TEXT("The right-click kill did not finish: ") + PC.Navigation->Status);
    }

    // The new life: a spawn-anchor offset, the exact protection window and the public status other observers get.
    bool TickRespawn(double Now, ALDGameMode& Mode, const ULDMapRegistry& Registry)
    {
        const ALDPlayerController* Remote = Mode.FindContenderController(ClientId);
        const ALDCharacter* Body = Remote ? Cast<ALDCharacter>(Remote->GetPawn()) : nullptr;
        if (!Remote || !Body || Remote->OwnerState.Life != 2 || Remote->OwnerState.LifePhase != ELDLifePhase::Alive)
        {
            if (Now - StageAt <= RespawnSeconds + LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
            return Fail(TEXT("Respawn"), TEXT("Client1 did not respawn after the approved delay"));
        }
        const FLDOwnerState& Victim = Remote->OwnerState;
        const FLDDemoWorldGeometry* Geometry = Registry.GetGeometry();
        if (!Geometry) { return Fail(TEXT("Respawn"), TEXT("The registered world geometry is unavailable")); }
        int32 SpawnAnchors = 0;
        RespawnAnchor.Reset();
        for (const FLDDemoWorldAnchor& Anchor : Geometry->Anchors)
        {
            if (Anchor.Kind != ELDDemoWorldAnchorKind::Spawn) { continue; }
            ++SpawnAnchors;
            const FLDMapBinding* Bound = Registry.Resolve(FName(*Anchor.Id), Registry.Generation);
            if (!Bound) { continue; }
            for (const FLDDemoWorldPoint& Point : Geometry->SpawnOffsetsCm)
            {
                FVector Offset;
                FString OffsetError;
                if (Point.ToVector(Offset, OffsetError) && FVector::Dist2D(Body->GetActorLocation(), Bound->Position + Offset) <= 1.0)
                {
                    RespawnAnchor = Anchor.Id;
                }
            }
        }
        Check(SpawnAnchors == 10 && !RespawnAnchor.IsEmpty(), TEXT("Respawn"),
            TEXT("The new body stands on an approved offset of one of the ten spawn anchors"),
            FString::Printf(TEXT("spawn_anchors=%d anchor=%s at=%s"), SpawnAnchors, *RespawnAnchor, *Body->GetActorLocation().ToCompactString()));
        ProtectedUntilMs = Victim.ProtectedUntilMs;
        const int64 Window = Catalog->Get().Parameters.ProtectionMs;
        Check(ProtectedUntilMs == RespawnAtMs + Window && Mode.IsContenderProtected(ClientId), TEXT("Respawn"),
            TEXT("The respawned life is protected for exactly the approved window from its respawn instant"),
            FString::Printf(TEXT("respawn_at_ms=%lld protected_until_ms=%lld window_ms=%lld"), RespawnAtMs, ProtectedUntilMs, Window));
        FLDObservedPublicFacts Facts;
        FString FactsError;
        const bool bFacts = Mode.ReadObservedPublicFacts(Mode.GetMatchContext(), Victim.BodyRef, Facts, FactsError);
        const FLDObservedStatus* Cue = bFacts ? LDDemoCombatScenario::CombatProbeProtection(Facts.Statuses) : nullptr;
        Check(Cue && Cue->ExpiresAtServerMs == ProtectedUntilMs, TEXT("Respawn"),
            TEXT("Observers of the new body are offered respawn_protection with its exact server expiry"),
            Cue ? FString::Printf(TEXT("expires_ms=%lld"), Cue->ExpiresAtServerMs) : FactsError);
        if (!Passed || !Mark(TEXT("HostSawProtection.json"))) { return Finish(); }
        SetStage(EStage::Removal);
        return true;
    }

    bool TickRemoval(double Now, ALDGameMode& Mode)
    {
        const ALDPlayerController* Remote = Mode.FindContenderController(ClientId);
        if (!Remote) { return Fail(TEXT("Protection"), TEXT("Client1's server controller disappeared")); }
        const FLDOwnerState& Victim = Remote->OwnerState;
        if (Victim.ProtectedUntilMs != 0 || Mode.IsContenderProtected(ClientId))
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
            return Fail(TEXT("Protection"), TEXT("Client1's protection never ended"));
        }
        FLDObservedPublicFacts Facts;
        FString FactsError;
        const bool bFacts = Mode.ReadObservedPublicFacts(Mode.GetMatchContext(), Victim.BodyRef, Facts, FactsError);
        Check(bFacts && !LDDemoCombatScenario::CombatProbeProtection(Facts.Statuses) && Victim.Life == 2
            && Victim.LifePhase == ELDLifePhase::Alive, TEXT("Protection"),
            TEXT("Ended protection retires the public respawn_protection status of the same living body"), FactsError);
        if (!Passed) { return Finish(); }
        SetStage(EStage::Grant);
        return true;
    }

    // ---- Host: skills from the fixture's presets, the snare, the observed shield, Client1's arc bolt and the reach card. ----

    // The fixture's XP grants raise each role one level past its planned cards (the opening offer took the first), as before
    // the opening choice, so level-dependent checks keep their values; that last opportunity stays unchosen.
    bool TickGrant(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        if (!Mode.Economy) { return Fail(TEXT("Skills"), TEXT("The Listen Host has no Economy owner for the fixture's grants")); }
        if (HostGrant == 0)
        {
            const ALDPlayerController* Remote = Mode.FindContenderController(ClientId);
            const FLDProgressionView* Own = PC.ReadOwnProgression();
            const FLDProgressionView* Other = Remote ? Remote->ReadOwnProgression() : nullptr;
            const TArray<int64>& Thresholds = Catalog->Get().Progression.XPThresholds;
            const int32 HostLevel = LDDemoCombatScenario::CombatProbeSkillPlan(true).Num() + 1;
            const int32 ClientLevel = LDDemoCombatScenario::CombatProbeSkillPlan(false).Num() + 1;
            if (!Own || !Other || !Thresholds.IsValidIndex(HostLevel - 1) || !Thresholds.IsValidIndex(ClientLevel - 1)
                || Own->CumulativeXP >= Thresholds[HostLevel - 1] || Other->CumulativeXP >= Thresholds[ClientLevel - 1])
            {
                return Fail(TEXT("Skills"), TEXT("Both roles' progression is readable and below the levels of their planned cards"));
            }
            FString HostError, ClientError;
            HostGrant = Mode.Economy->DevGrantReward(HostId, Thresholds[HostLevel - 1] - Own->CumulativeXP, 0, HostError);
            ClientGrant = Mode.Economy->DevGrantReward(ClientId, Thresholds[ClientLevel - 1] - Other->CumulativeXP, 0, ClientError);
            if (!Check(HostGrant != 0 && ClientGrant != 0, TEXT("Skills"), TEXT("The DEV growth fixture queues an XP grant for each role"),
                HostError + ClientError))
            {
                return Finish();
            }
            return true;
        }
        FString HostDetail, ClientDetail;
        const ELDDevGrantStatus HostStatus = Mode.Economy->DevGrantStatus(HostGrant, HostDetail);
        const ELDDevGrantStatus ClientStatus = Mode.Economy->DevGrantStatus(ClientGrant, ClientDetail);
        if ((HostStatus == ELDDevGrantStatus::Pending || ClientStatus == ELDDevGrantStatus::Pending)
            && Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds)
        {
            return true;
        }
        Check(HostStatus == ELDDevGrantStatus::Granted && ClientStatus == ELDDevGrantStatus::Granted, TEXT("Skills"),
            TEXT("Both grants commit through the reward composite at a later Transactions phase"),
            FString::Printf(TEXT("host=%d %s client=%d %s"), int32(HostStatus), *HostDetail, int32(ClientStatus), *ClientDetail));
        if (!Passed) { return Finish(); }
        SetStage(EStage::Learn);
        return true;
    }

    // Back to the public duel site, then the snare waits for Client1 on its skill site.
    bool TickReturn(double Now, ALDPlayerController& PC)
    {
        bool bArrived = false;
        if (!Walk(Now, PC, HostSite, LDDemoCombatScenario::CombatProbeSkillArrivalCm, bArrived)) { return false; }
        if (bArrived) { SetStage(EStage::Snare); }
        return true;
    }

    const ALDCharacter* ClientBody(const ALDGameMode& Mode) const
    {
        const ALDPlayerController* Remote = Mode.FindContenderController(ClientId);
        return Remote ? Cast<ALDCharacter>(Remote->GetPawn()) : nullptr;
    }

    // Arms a learned Q/W entry through its hotkey path and confirms it on an explicit aim; false when it cannot arm.
    bool ArmAndConfirm(ALDPlayerController& PC, ELDCombatEntry Entry, const FLDCombatTarget& Aim)
    {
        ELDCombatEntry Armed = ELDCombatEntry::None;
        PC.ActivateCombatEntry(Entry);
        if (!Check(PC.ReadArmedCombatEntry(Armed) && Armed == Entry, TEXT("Skills"),
            TEXT("A targeted skill's hotkey arms its entry and waits for a confirmation instead of sending")))
        {
            return false;
        }
        ReceiptSerialBefore = PC.CombatReceipts.Serial();
        return Check(PC.ConfirmArmedEntry(Aim), TEXT("Skills"), TEXT("The armed entry confirms on its aim"));
    }

    // The server's refusal of the cast confirmed last; null while it is not refused.
    const FLDCombatActionReceipt* Refusal(const ALDPlayerController& PC) const
    {
        return PC.CombatReceipts.FindLatestSince(ReceiptSerialBefore,
            [](const FLDCombatActionReceipt& Receipt) { return Receipt.Phase == ELDCombatPhase::Rejected; });
    }

    bool TickSnare(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        const FLDWorldView* Frame = LDDemoCombatScenario::CombatProbeFrame(PC);
        const FLDObservedUnitView* Target = Frame ? LDDemoCombatScenario::CombatProbeContender(*Frame, ClientId) : nullptr;
        const ELDCombatEntry Snare = EntryOf(PC, LDDemoCombatScenario::CombatProbeSkillPlan(true)[1]);
        const FLDCombatEntryView* Row = PC.CombatView.Entries.FindByPredicate([Snare](const FLDCombatEntryView& Entry) { return Entry.Entry == Snare; });
        const ALDCharacter* Own = Cast<ALDCharacter>(PC.GetPawn());
        const ALDCharacter* Other = ClientBody(Mode);
        const FLDBodyCombatState* Victim = Other ? Mode.Combat->StateFor(Other) : nullptr;
        if (!Exists(TEXT("Client1AtSkillSite.json")) || !Target || !Row || Row->Unavailable != ELDCombatReject::None || !Own || !Victim)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeKillSeconds) { return true; }
            return Fail(TEXT("Skills"), FString::Printf(TEXT("The snare never became castable on a visible Client1 at its skill site (unavailable=%d)"),
                Row ? int32(Row->Unavailable) : -1));
        }
        SkillGapCm = FVector::Dist2D(ALDCombatDirector::FootOf(*Own), ALDCombatDirector::FootOf(*Other));
        if (!Check(SkillGapCm <= double(Row->RangeCm) && SkillGapCm > double(PC.CombatView.AttackRangeCm), TEXT("Skills"),
            TEXT("Client1 waits inside the snare's range and beyond the Host's reach-carded basic range"),
            FString::Printf(TEXT("gap_cm=%.0f snare_cm=%d basic_cm=%.0f"), SkillGapCm, Row->RangeCm, PC.CombatView.AttackRangeCm)))
        {
            return Finish();
        }
        FLDCombatTarget Aim;
        Aim.Kind = ELDCombatTargetKind::Unit;
        Aim.Observed = Target->Handle;
        Aim.ObservationEpoch = Target->ObservationEpoch;
        HitBeforeMs = Victim->Cues.LastHitAtMs;
        if (!ArmAndConfirm(PC, Snare, Aim)) { return Finish(); }
        SetStage(EStage::Rooting);
        return true;
    }

    // Observers must see the root: the Host's own observation frame shows it on Client1 with its server deadline, and
    // the Host keeps a rendered capture of it before Client1 may cleanse it.
    bool TickRooting(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        if (Shot)
        {
            if (ShotPending()) { return true; }
            if (!ShotSaved(TEXT("Client1 rooted by the snare")) || !Mark(TEXT("HostSawRoot.json"))) { return Finish(); }
            SetStage(EStage::ShieldSeen);
            return true;
        }
        if (const FLDCombatActionReceipt* Refused = Refusal(PC))
        {
            return Fail(TEXT("Skills"), FString::Printf(TEXT("The snare was refused (reject=%d)"), int32(Refused->Reject)));
        }
        const FLDWorldView* Frame = LDDemoCombatScenario::CombatProbeFrame(PC);
        const FLDObservedUnitView* Target = Frame ? LDDemoCombatScenario::CombatProbeContender(*Frame, ClientId) : nullptr;
        const FLDObservedStatus* Root = Target ? LDDemoCombatScenario::CombatProbeStatus(Target->Statuses, ELDDemoStatusId::Root) : nullptr;
        const ALDCharacter* Other = ClientBody(Mode);
        const FLDBodyCombatState* Victim = Other ? Mode.Combat->StateFor(Other) : nullptr;
        if (!Root || !Victim)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeSkillSeconds) { return true; }
            return Fail(TEXT("Skills"), TEXT("The Host never observed the snare's root on Client1"));
        }
        Check(Victim->Cues.LastHitAtMs > HitBeforeMs && Root->ExpiresAtServerMs > Victim->Cues.LastHitAtMs, TEXT("Skills"),
            TEXT("The snare lands on Client1 and the Host's observation frame shows its root with the server deadline"),
            FString::Printf(TEXT("hit_ms=%lld root_until_ms=%lld"), Victim->Cues.LastHitAtMs, Root->ExpiresAtServerMs));
        if (!Passed) { return Finish(); }
        BeginShot(TEXT("HostSees-Root.png"), [this](FString& Facts) { return ShowsClient(ELDDemoStatusId::Root, false, Facts); },
            LDDemoCombatScenario::CombatProbeSkillSeconds);
        return true;
    }

    bool TickShieldSeen(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        if (Shot)
        {
            if (ShotPending()) { return true; }
            if (!ShotSaved(TEXT("Client1's shield after its dispelled root"))) { return Finish(); }
            // Client1's bolt targets the Host: its own authority body is the baseline, and the flight is sampled and
            // captured on every frame until it lands.
            const FLDBodyCombatState* Own = AuthorityState(Mode, &PC);
            if (!Own) { return Fail(TEXT("Skills"), TEXT("The Host lost its authority body before Client1's bolt")); }
            HitBeforeMs = Own->Cues.LastHitAtMs;
            PoolBeforeP2 = Own->Resources.HpP2 + Own->Resources.ShieldP2;
            FlightFromMs = Mode.GetAuthorityNowMs();
            BeginShot(TEXT("HostSees-ChalkFlight.png"), [this](FString& Facts) { return ShowsFlight(Facts); },
                LDDemoCombatScenario::CombatProbeStepSeconds);
            if (!Mark(TEXT("HostSawShield.json"))) { return Finish(); }
            SetStage(EStage::BoltFlight);
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(AsShared(), &FLDDemoCombatProbe::SampleFlight), 0.f);
            return true;
        }
        const FLDWorldView* Frame = LDDemoCombatScenario::CombatProbeFrame(PC);
        const FLDObservedUnitView* Target = Frame ? LDDemoCombatScenario::CombatProbeContender(*Frame, ClientId) : nullptr;
        const bool bShield = Target && LDDemoCombatScenario::CombatProbeStatus(Target->Statuses, ELDDemoStatusId::Shield);
        const bool bRoot = Target && LDDemoCombatScenario::CombatProbeStatus(Target->Statuses, ELDDemoStatusId::Root);
        if (!Exists(TEXT("Client1Shielded.json")) || !bShield || bRoot)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeSkillSeconds) { return true; }
            return Fail(TEXT("Skills"), FString::Printf(TEXT("The Host never observed Client1 shielded and free of its root (shield=%d root=%d)"),
                int32(bShield), int32(bRoot)));
        }
        Check(true, TEXT("Skills"), TEXT("The Host observes Client1's shield and the dispelled root"));
        BeginShot(TEXT("HostSees-Shield.png"), [this](FString& Facts) { return ShowsClient(ELDDemoStatusId::Shield, true, Facts); },
            LDDemoCombatScenario::CombatProbeSkillSeconds);
        return true;
    }

    // Every frame of Client1's bolt: the authority's projectile facts and Client1's public cue.
    bool SampleFlight(float)
    {
        UWorld* Current = World.Get();
        ALDGameMode* Mode = Current ? Current->GetAuthGameMode<ALDGameMode>() : nullptr;
        const ALDPlayerController* PC = Controller.Get();
        if (Written || Stage != EStage::BoltFlight || !Mode || !Mode->Combat || !PC || !FlightError.IsEmpty()) { return false; }
        TArray<FLDObservedProjectileFact> Flying;
        if (!Mode->Combat->ReadObservedProjectiles(Flying, FlightError)) { return false; }
        const ALDPlayerController* Remote = Mode->FindContenderController(ClientId);
        const ALDCharacter* Own = Cast<ALDCharacter>(PC->GetPawn());
        const ALDCharacter* Other = ClientBody(*Mode);
        // Client1 aims from its feet at the Host's: the flight heads along the same line while both stand still.
        const FVector Toward = Own && Other
            ? (ALDCombatDirector::FootOf(*Own) - ALDCombatDirector::FootOf(*Other)).GetSafeNormal2D() : FVector::ZeroVector;
        for (const FLDObservedProjectileFact& Fact : Flying)
        {
            if (Fact.Look != ELDObservedProjectileLook::Chalk) { continue; }
            ++ProjectileSamples;
            BoltKey = Fact.Key;
            bBoltHeading &= !Toward.IsNearlyZero() && FVector2D::DotProduct(Fact.Heading, FVector2D(Toward)) > 0.99;
        }
        FLDObservedPublicFacts Caster;
        FString CasterError;
        bCastCueSeen |= Remote && Mode->ReadObservedPublicFacts(Mode->GetMatchContext(), Remote->OwnerState.BodyRef, Caster, CasterError)
            && Caster.ActionCue == ELDObservedActionCue::Cast && Caster.ActionCueAtMs >= FlightFromMs;
        return true;
    }

    bool TickBoltFlight(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        if (!FlightError.IsEmpty()) { return Fail(TEXT("Skills"), TEXT("Combat refused to read its projectiles: ") + FlightError); }
        if (!bBoltLanded)
        {
            const FLDBodyCombatState* Own = AuthorityState(Mode, &PC);
            if (!Own || Own->Cues.LastHitAtMs <= HitBeforeMs)
            {
                if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
                return Fail(TEXT("Skills"), FString::Printf(TEXT("Client1's arc bolt never hit the Host (projectile samples=%d)"),
                    ProjectileSamples));
            }
            TArray<FLDObservedProjectileFact> Flying;
            FString FlyingError;
            const bool bRead = Mode.Combat->ReadObservedProjectiles(Flying, FlyingError);
            FLDObservedPublicFacts Struck;
            FString StruckError;
            const bool bStruck = Mode.ReadObservedPublicFacts(Mode.GetMatchContext(), PC.OwnerState.BodyRef, Struck, StruckError);
            const int64 PoolP2 = Own->Resources.HpP2 + Own->Resources.ShieldP2;
            const int64 HitAgeMs=Mode.GetAuthorityNowMs()-Own->Cues.LastHitAtMs;
            // A synchronous screenshot readback may span the entire cue window. Damage remains committed,
            // but the public cue must expire normally rather than being held alive for this probe.
            const bool bHitCueCorrect=HitAgeMs>=0 && HitAgeMs<LDCombatCues::HitHoldMs
                ? Struck.ActionCue==ELDObservedActionCue::Hit && Struck.ActionCueAtMs==Own->Cues.LastHitAtMs
                : HitAgeMs>=LDCombatCues::HitHoldMs && Struck.ActionCue==ELDObservedActionCue::None && Struck.ActionCueAtMs==0;
            const FString Detail = FString::Printf(TEXT("samples=%d heading=%d pool_p2=%lld->%lld cast_cue=%d struck_cue=%d hit_ms=%lld cue_ms=%lld hit_age_ms=%lld %s%s"),
                ProjectileSamples, int32(bBoltHeading), PoolBeforeP2, PoolP2, int32(bCastCueSeen), int32(Struck.ActionCue),
                Own->Cues.LastHitAtMs, Struck.ActionCueAtMs, HitAgeMs, *FlyingError, *StruckError);
            Check(ProjectileSamples > 0 && bBoltHeading, TEXT("Skills"),
                TEXT("Client1's arc bolt (active_arc_bolt) flies as a Chalk projectile fact from Client1 toward the Host"), Detail);
            Check(bRead && PoolP2 < PoolBeforeP2 && !Flying.ContainsByPredicate([this](const FLDObservedProjectileFact& Fact) { return Fact.Key == BoltKey; }),
                TEXT("Skills"), TEXT("The arc bolt hits the Host, takes its shield or health and ends its flight"), Detail);
            Check(bCastCueSeen && bStruck && bHitCueCorrect,
                TEXT("Skills"), TEXT("Public facts show Client1's Cast cue and the Host's exact landing Hit cue or its on-time expiry"), Detail);
            if (!Passed) { return Finish(); }
            bBoltLanded = true;
            // The landed flight leaves the frame at the next publication at the latest.
            if (Shot) { Shot->Deadline = FMath::Min(Shot->Deadline, FPlatformTime::Seconds() + 1.0); }
        }
        if (ShotPending()) { return true; }
        if (!ShotSaved(TEXT("Client1's chalk in flight on its own observation frame"))) { return Finish(); }
        SetStage(EStage::Reach);
        return true;
    }

    // ---- Host: rendered evidence for presentation review, never compared by pixels. ----

    // What the Host's current frame shows of Client1: where it stands, its cue and its public statuses.
    static FString SeenFacts(const FLDObservedUnitView& Unit)
    {
        FString Statuses;
        for (const FLDObservedStatus& Status : Unit.Statuses)
        {
            Statuses += FString::Printf(TEXT("%s%u"), Statuses.IsEmpty() ? TEXT("") : TEXT(","), uint32(Status.StatusId));
        }
        return FString::Printf(TEXT("client1_feet=%s client1_cue=%d client1_cue_age_ms=%u client1_statuses=[%s]"),
            *FVector(Unit.FeetCm).ToCompactString(), int32(Unit.ActionCue), uint32(Unit.ActionCueAgeMs), *Statuses);
    }

    // Client1 on the Host's current frame with Status, and without a root when bFree.
    bool ShowsClient(ELDDemoStatusId Status, bool bFree, FString& Facts) const
    {
        const ALDPlayerController* PC = Controller.Get();
        const FLDWorldView* Frame = PC ? LDDemoCombatScenario::CombatProbeFrame(*PC) : nullptr;
        const FLDObservedUnitView* Unit = Frame ? LDDemoCombatScenario::CombatProbeContender(*Frame, ClientId) : nullptr;
        if (!Unit || !LDDemoCombatScenario::CombatProbeStatus(Unit->Statuses, Status)
            || (bFree && LDDemoCombatScenario::CombatProbeStatus(Unit->Statuses, ELDDemoStatusId::Root)))
        {
            return false;
        }
        Facts = SeenFacts(*Unit);
        return true;
    }

    // The Host's own body shows Wanted between MinAge and MaxAge seconds into it (None: any moment), by the same cue rule
    // and clock its own body is drawn from; fills what it shows, with Client1 as the Host's frame shows it.
    bool ShowsOwn(ELDObservedActionCue Wanted, float MinAge, float MaxAge, FString& Facts) const
    {
        const ALDPlayerController* PC = Controller.Get();
        int64 NowMs = 0;
        if (!PC || !PC->ReadDemoAuthorityTimeMs(NowMs) || PC->CombatView.Generation != PC->OwnerState.Match
            || PC->CombatView.Life != PC->OwnerState.Life)
        {
            return false;
        }
        const LDCombatCues::FCue Own = LDCombatCues::Select(PC->CombatView.Cues, NowMs);
        const int64 AgeMs = Own.Cue == ELDObservedActionCue::None ? 0 : NowMs - Own.AtMs;
        if (Wanted != ELDObservedActionCue::None
            && (Own.Cue != Wanted || AgeMs < int64(1000.f * MinAge) || AgeMs > int64(1000.f * MaxAge)))
        {
            return false;
        }
        const FLDWorldView* Frame = LDDemoCombatScenario::CombatProbeFrame(*PC);
        const FLDObservedUnitView* Unit = Frame ? LDDemoCombatScenario::CombatProbeContender(*Frame, ClientId) : nullptr;
        Facts = FString::Printf(TEXT("own_cue=%d own_cue_age_ms=%lld "), int32(Own.Cue), AgeMs)
            + (Unit ? SeenFacts(*Unit) : FString(TEXT("client1=unseen")));
        return true;
    }

    // A chalk drawn mid-flight on the Host's screen: a shown projectile proxy that has left Client1's hand. The drawn
    // proxy, not the frame, decides, so presentation smoothing cannot capture a flight that is not on screen.
    bool ShowsFlight(FString& Facts) const
    {
        const ALDPlayerController* PC = Controller.Get();
        UWorld* Current = World.Get();
        const FLDWorldView* Frame = PC ? LDDemoCombatScenario::CombatProbeFrame(*PC) : nullptr;
        const FLDObservedUnitView* Unit = Frame ? LDDemoCombatScenario::CombatProbeContender(*Frame, ClientId) : nullptr;
        if (!Current || !Unit) { return false; }
        for (TActorIterator<ALDObservedProjectileProxy> It(Current); It; ++It)
        {
            const FVector Drawn = It->GetActorLocation();
            const double ClearCm = FVector::Dist2D(Drawn, FVector(Unit->FeetCm));
            if (It->IsHidden() || ClearCm < LDDemoCombatScenario::CombatProbeFlightClearCm) { continue; }
            // The chalk-dust trail is drawn behind the flight in the same frame (issue #57).
            const ULDFlightTrailComponent* Trail = It->GetTrail();
            if (!Trail || Trail->ShownPuffs() == 0) { continue; }
            Facts = FString::Printf(TEXT("flights=%d chalk_drawn_at=%s chalk_clear_of_client1_cm=%.0f trail_puffs=%d trail_cm=%.0f "),
                Frame->Projectiles.Num(), *Drawn.ToCompactString(), ClearCm, Trail->ShownPuffs(), Trail->LengthCm()) + SeenFacts(*Unit);
            return true;
        }
        return false;
    }

    // Host at its duel site: one right click on open ground beside it shows the ground marker for its capture; then
    // it walks back to the site.
    bool TickMoveClick(double Now, ALDPlayerController& PC, const ALDCharacter& Body, const FVector& Site)
    {
        if (MoveClick == EClickStep::Aim)
        {
            FVector Beside;
            FVector2D Screen;
            if (!PickBeside(PC, Site, Beside, Screen))
            {
                return Fail(TEXT("Pointer"), TEXT("No open ground beside the Host's duel site lies in its view"));
            }
            PC.Navigation->DevSetPointer(Screen);
            const uint32 Before = PC.Navigation->LocalIdentity().Order;
            PC.Navigation->ClickMove();
            if (!Check(PC.Navigation->LocalIdentity().Order != Before, TEXT("Pointer"),
                TEXT("A right click on open ground beside the Host sends its ground order"), Beside.ToCompactString()))
            {
                return Finish();
            }
            BeginShot(TEXT("HostSees-MoveMarker.png"), [this](FString& Facts) { return ShowsMoveMarker(Facts); },
                LDDemoCombatScenario::CombatProbeSkillSeconds);
            MoveClickAt = Now;
            MoveClick = EClickStep::Shot;
            return true;
        }
        if (MoveClick == EClickStep::Shot)
        {
            if (ShotPending()) { return true; }
            PC.Navigation->DevSetPointer(TOptional<FVector2D>());
            if (!ShotSaved(TEXT("the ground marker of its right click"))) { return Finish(); }
            MoveClick = EClickStep::Next;
            return true;
        }
        if (Now - MoveClickAt > LDDemoCombatScenario::CombatProbeStepSeconds)
        {
            return Fail(TEXT("Pointer"), TEXT("The Host did not walk back to its duel site after the ground click: ") + PC.Navigation->Status);
        }
        if (PC.Navigation->HasMoveOrder()) { return true; }
        if (FVector::Dist2D(Body.GetNavAgentLocation(), Site) <= LDDemoCombatScenario::CombatProbeArrivalCm)
        {
            MoveClick = EClickStep::Done;
            return true;
        }
        PC.Navigation->Navigate(Site);
        return true;
    }

    // Legal open ground a short walk from Site, outside the shop's reach, on screen above the HUD's bottom bar.
    bool PickBeside(const ALDPlayerController& PC, const FVector& Site, FVector& Out, FVector2D& Screen) const
    {
        const ULDMapRegistry* Registry = PC.GetWorld()->GetSubsystem<ULDMapRegistry>();
        FCollisionShape Capsule;
        FString Problem;
        int32 Width = 0, Height = 0;
        PC.GetViewportSize(Width, Height);
        if (!Registry || !ULDMapRegistry::ContentCapsule(*Catalog, Capsule, Problem) || Width <= 0 || Height <= 0) { return false; }
        const FName Surface = PC.Navigation->GroundSurfaceHint();
        FLDLegalGround From;
        ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
        if (!Registry->QueryGround(Registry->Revision(), Site, Capsule, Surface, From, Failure, Problem)) { return false; }
        for (int32 Bearing = 0; Bearing < 8; ++Bearing)
        {
            const FVector Point = Site + FVector(1, 0, 0).RotateAngleAxis(45.0 * Bearing, FVector::UpVector)
                * LDDemoCombatScenario::CombatProbeBesideCm;
            FLDLegalGround To;
            FLDCompletePath Route;
            bool bWithin = true;
            if (Registry->QueryGround(Registry->Revision(), Point, Capsule, Surface, To, Failure, Problem)
                && Registry->QueryShopReach(Registry->Revision(), Registry->NearestShop(Point), Point, bWithin, Failure, Problem) && !bWithin
                && Registry->QueryCompletePath(Registry->Revision(), From, To, Capsule, Route, Failure, Problem)
                && Route.LengthCm <= 1.3 * LDDemoCombatScenario::CombatProbeBesideCm
                && PC.ProjectWorldLocationToScreen(To.SurfaceCm, Screen)
                && Screen.X > .1 * Width && Screen.X < .9 * Width && Screen.Y > .1 * Height && Screen.Y < .7 * Height
                && !UnitNearScreen(PC, Screen))
            {
                Out = To.SurfaceCm;
                return true;
            }
        }
        return false;
    }

    // A ground click must not become an attack: no observed unit is drawn near that pointer.
    static bool UnitNearScreen(const ALDPlayerController& PC, const FVector2D& Screen)
    {
        const FLDWorldView* Frame = LDDemoCombatScenario::CombatProbeFrame(PC);
        const double HalfHeight = GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
        if (!Frame) { return false; }
        for (const FLDObservedUnitView& Unit : Frame->Units)
        {
            FVector2D At;
            if (PC.ProjectWorldLocationToScreen(FVector(Unit.FeetCm) + FVector(0, 0, HalfHeight), At)
                && FVector2D::Distance(At, Screen) < 120.0)
            {
                return true;
            }
        }
        return false;
    }

    // Host: the first kill order is the right click on Client1's drawn body: its hover ring and attack cursor, then the
    // attack marker closing on it, are captured on the way.
    bool TickKillClick(ALDPlayerController& PC, const FLDObservedUnitView& Victim)
    {
        FVector2D Screen;
        FVector Presented;
        if (!PC.LocalView() || !PC.LocalView()->ReadPresentedLocation(Victim.Handle,Victim.ObservationEpoch,Presented)
            || !PC.ProjectWorldLocationToScreen(Presented, Screen))
        {
            return Fail(TEXT("Pointer"), TEXT("Client1's observed body does not project into the Host's view"));
        }
        const FGuid Handle = Victim.Handle;
        if (KillClick == EClickStep::Aim)
        {
            PC.Navigation->DevSetPointer(Screen);
            BeginShot(TEXT("HostSees-Hover.png"), [this, Handle](FString& Facts) { return ShowsPointer(Handle, true, Facts); },
                LDDemoCombatScenario::CombatProbeSkillSeconds);
            KillClick = EClickStep::Shot;
            return true;
        }
        if (ShotPending()) { return true; }
        if (KillClick == EClickStep::Shot)
        {
            if (!ShotSaved(TEXT("the hover ring and attack cursor on Client1 under its pointer"))) { return Finish(); }
            PC.Navigation->DevSetPointer(Screen);
            const uint32 Before = PC.Navigation->LocalIdentity().Order;
            PC.Navigation->ClickMove();
            if (!Check(PC.Navigation->LocalIdentity().Order != Before, TEXT("Pointer"),
                TEXT("The right click on Client1's drawn body sends the Host's attack order")))
            {
                return Finish();
            }
            KillOrder = PC.Navigation->LocalIdentity();
            ++AttackOrders;
            BeginShot(TEXT("HostSees-AttackMarker.png"), [this, Handle](FString& Facts) { return ShowsPointer(Handle, false, Facts); },
                LDDemoCombatScenario::CombatProbeSkillSeconds);
            KillClick = EClickStep::Next;
            return true;
        }
        PC.Navigation->DevSetPointer(TOptional<FVector2D>());
        if (!ShotSaved(TEXT("the attack marker closing on the right-clicked Client1"))) { return Finish(); }
        KillClick = EClickStep::Done;
        SetStage(EStage::KillWait);
        return true;
    }

    // The Host's current frame shows the hover ring under Handle with the plain arrow (hovering never changes the
    // pointer), or the attack marker.
    bool ShowsPointer(const FGuid& Handle, bool bHover, FString& Facts) const
    {
        const ALDPlayerController* PC = Controller.Get();
        const ALDPointerFeedback* Feedback = PC ? PC->Navigation->PointerFeedback() : nullptr;
        FGuid Hovered;
        uint32 Epoch = 0;
        const bool bShown = Feedback && (bHover
            ? PC->Navigation->ReadHovered(Hovered, Epoch) && Hovered == Handle && Feedback->ShowsHover()
                && PC->CurrentMouseCursor == EMouseCursor::Default
            : Feedback->ShowsAttack());
        if (!bShown) { return false; }
        Facts = FString::Printf(TEXT("%s cursor=%d"), bHover ? TEXT("hover_ring") : TEXT("attack_marker"), int32(PC->CurrentMouseCursor.GetValue()));
        return true;
    }

    bool ShowsMoveMarker(FString& Facts) const
    {
        const ALDPlayerController* PC = Controller.Get();
        const ALDPointerFeedback* Feedback = PC ? PC->Navigation->PointerFeedback() : nullptr;
        if (!Feedback || !Feedback->ShowsMove()) { return false; }
        Facts = TEXT("move_marker");
        return true;
    }

    // The first right-click walk of this life: its order time and starting point.
    void StartFeel(const ALDPlayerController& PC)
    {
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        Feel.SentAt = FPlatformTime::Seconds();
        Feel.From = Body ? Body->GetActorLocation() : FVector::ZeroVector;
        Feel.MaxSpeed = Body ? Body->GetCharacterMovement()->GetMaxSpeed() : 0.0;
        EnsureFeelTicking();
    }

    // The per-frame sampler.
    void EnsureFeelTicking()
    {
        if (!bFeelTicking)
        {
            bFeelTicking = true;
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(AsShared(), &FLDDemoCombatProbe::SampleFeel), 0.f);
        }
    }

    bool SampleFeel(float)
    {
        const ALDPlayerController* PC = Controller.Get();
        if (Written || !PC)
        {
            bFeelTicking = false;
            return false;
        }
        const double Now = FPlatformTime::Seconds();
        const double FrameMs = Feel.LastFrameAt >= 0 ? (Now - Feel.LastFrameAt) * 1000.0 : 0.0;
        const double SettleFrameMs = 1000.0 * GetDefault<ALDCharacter>()->GetCharacterMovement()->MaxSimulationTimeStep;
        Feel.SteadySince = Feel.SteadySince < 0 || FrameMs > SettleFrameMs ? Now : Feel.SteadySince;
        if (Feel.SentAt >= 0 && Feel.FirstMoveAt < 0)
        {
            ++Feel.ClickFrames;
            Feel.ClickLongestFrameMs = FMath::Max(Feel.ClickLongestFrameMs, FrameMs);
        }
        Feel.FirstFrameAt = Feel.FirstFrameAt < 0 ? Now : Feel.FirstFrameAt;
        Feel.LastFrameAt = Now;
        ++Feel.Frames;
        if (const ALDCharacter* Body = Cast<ALDCharacter>(PC->GetPawn()))
        {
            const FVector At = Body->GetActorLocation();
            const double Speed = Body->GetVelocity().Size2D();
            const double Moved = LDDemoCombatScenario::CombatProbeFeelMovedCm;
            if (Feel.SentAt >= 0 && Feel.FirstMoveAt < 0 && FVector::Dist2D(At, Feel.From) > Moved) { Feel.FirstMoveAt = Now; }
            if (Feel.FirstMoveAt >= 0 && Feel.FullSpeedAt < 0 && Speed >= .95 * Feel.MaxSpeed) { Feel.FullSpeedAt = Now; }
            if (Feel.StopAt >= 0 && Feel.StoppedAt < 0 && Speed < LDDemoCombatScenario::CombatProbeFeelStillCmPerSec)
            {
                Feel.StoppedAt = Now;
                Feel.StopDistanceCm = FVector::Dist2D(At, Feel.StopFrom);
            }
            if (Feel.RestartAt >= 0 && Feel.RestartMoveAt < 0 && FVector::Dist2D(At, Feel.RestartFrom) > Moved) { Feel.RestartMoveAt = Now; }
        }
        if (Feel.CancelAt >= 0 && Feel.CancelDoneAt < 0)
        {
            const FLDOrderReceipt& Latest = PC->Navigation->LatestReceipt();
            if (Latest.Identity == StopOrder && Latest.Status == ELDOrderStatus::Cancelled) { Feel.CancelDoneAt = Now; }
        }
        return true;
    }

    // The feel measurements as report fields, with the movement parameters they ran under; one Feel check that they
    // were all taken.
    TSharedRef<FJsonObject> CheckFeel()
    {
        const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
        const auto Ms = [](double From, double To) { return From >= 0 && To >= 0 ? FMath::RoundToDouble((To - From) * 1000.0) : -1.0; };
        const ALDPlayerController* PC = Controller.Get();
        const UCharacterMovementComponent* Movement = GetDefault<ALDCharacter>()->GetCharacterMovement();
        Out->SetNumberField(TEXT("click_to_move_ms"), Ms(Feel.SentAt, Feel.FirstMoveAt));
        Out->SetNumberField(TEXT("click_frames"), Feel.ClickFrames);
        Out->SetNumberField(TEXT("click_longest_frame_ms"), FMath::RoundToDouble(Feel.ClickLongestFrameMs));
        Out->SetNumberField(TEXT("spawn_to_click_ms"), Ms(Feel.SpawnAt, Feel.SentAt));
        Out->SetNumberField(TEXT("click_to_activated_receipt_ms"),
            PC && PC->Navigation->RouteAcceptanceMs.Num() ? FMath::RoundToDouble(PC->Navigation->RouteAcceptanceMs[0]) : -1.0);
        Out->SetNumberField(TEXT("ramp_to_full_speed_ms"), Ms(Feel.FirstMoveAt, Feel.FullSpeedAt));
        Out->SetNumberField(TEXT("frame_ms"), Feel.Frames > 1 ? (Feel.LastFrameAt - Feel.FirstFrameAt) * 1000.0 / double(Feel.Frames - 1) : -1.0);
        Out->SetNumberField(TEXT("max_walk_speed"), Feel.MaxSpeed);
        Out->SetNumberField(TEXT("max_acceleration"), Movement->MaxAcceleration);
        Out->SetNumberField(TEXT("braking_deceleration_walking"), Movement->BrakingDecelerationWalking);
        Out->SetNumberField(TEXT("ground_friction"), Movement->GroundFriction);
        Out->SetNumberField(TEXT("braking_friction_factor"), Movement->BrakingFrictionFactor);
        Out->SetBoolField(TEXT("separate_braking_friction"), Movement->bUseSeparateBrakingFriction);
        Out->SetStringField(TEXT("facing"), TEXT("instant (ALDCharacter::SetFacing sets the steering direction's yaw)"));
        bool bTaken = Feel.FirstMoveAt >= 0 && Feel.FullSpeedAt >= 0;
        if (IsHost())
        {
            Out->SetNumberField(TEXT("stop_from_speed"), Feel.StopSpeed);
            Out->SetNumberField(TEXT("stop_to_still_ms"), Ms(Feel.StopAt, Feel.StoppedAt));
            Out->SetNumberField(TEXT("stop_distance_cm"), Feel.StopDistanceCm);
            Out->SetNumberField(TEXT("restart_click_to_move_ms"), Ms(Feel.RestartAt, Feel.RestartMoveAt));
            bTaken &= Feel.StoppedAt >= 0 && Feel.RestartMoveAt >= 0;
        }
        else
        {
            Out->SetNumberField(TEXT("stop_to_stopped_receipt_ms"), Ms(Feel.CancelAt, Feel.CancelDoneAt));
            bTaken &= Feel.CancelDoneAt >= 0;
        }
        FString Text;
        FJsonSerializer::Serialize(Out, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
        Check(bTaken, TEXT("Feel"), TEXT("The first right-click walk, the S and the next walk are timed on this connection's own body"), Text);
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_FEEL role=%s %s"), *Role, *Text);
        return Out;
    }

    // Opens the one capture: every frame until Seconds pass, a read-back is requested while the moment shows.
    void BeginShot(const TCHAR* File, TFunction<bool(FString&)> Shows, double Seconds)
    {
        Shot = MakeShared<FShot>();
        Shot->File = File;
        Shot->Shows = MoveTemp(Shows);
        Shot->Deadline = FPlatformTime::Seconds() + Seconds;
        if (!ShotHandle.IsValid())
        {
            ShotHandle = UGameViewportClient::OnScreenshotCaptured().AddSP(AsShared(), &FLDDemoCombatProbe::OnShot);
        }
        if (!bShotTicking)
        {
            bShotTicking = true;
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(AsShared(), &FLDDemoCombatProbe::TickShot), 0.f);
        }
    }

    bool TickShot(float)
    {
        const bool bOpen = !Written && ShotPending();
        if (bOpen && FPlatformTime::Seconds() <= Shot->Deadline)
        {
            FString Facts;
            if (!FScreenshotRequest::IsScreenshotRequested() && Shot->Shows(Facts))
            {
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(Output, Shot->File), true, false);
            }
            return true;
        }
        if (bOpen)
        {
            Shot->Problem = FString::Printf(TEXT("no rendered frame showed the moment before the deadline (%d read-backs)"), Shot->ReadBacks);
        }
        // An unanswered request must not be written by the engine once nothing checks its frame.
        if (FScreenshotRequest::IsScreenshotRequested()) { FScreenshotRequest::Reset(); }
        UGameViewportClient::OnScreenshotCaptured().Remove(ShotHandle);
        ShotHandle.Reset();
        bShotTicking = false;
        return false;
    }

    // The engine's read-back of the Host's main view with its UI, on the frame it rendered.
    void OnShot(int32 Width, int32 Height, const TArray<FColor>& Colors)
    {
        if (!ShotPending()) { return; }
        ++Shot->ReadBacks;
        FString Facts;
        // The moment ended before this frame: TickShot asks for the next frame that still shows it.
        if (!Shot->Shows(Facts)) { return; }
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Colors.GetData(), Colors.Num()), Png);
        const FString Path = FPaths::Combine(Output, Shot->File);
        if (Png.IsEmpty() || !FFileHelper::SaveArrayToFile(Png, *Path))
        {
            Shot->Problem = TEXT("the rendered frame could not be written to ") + Path;
            return;
        }
        Shot->bSaved = true;
        Shot->Facts = FString::Printf(TEXT("%dx%d %s"), Width, Height, *Facts);
        const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("file"), Shot->File);
        Row->SetNumberField(TEXT("width"), Width);
        Row->SetNumberField(TEXT("height"), Height);
        Row->SetStringField(TEXT("shows"), Facts);
        Screenshots.Add(MakeShared<FJsonValueObject>(Row));
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_COMBAT_SCREENSHOT %s %s"), *Path, *Shot->Facts);
    }

    bool ShotPending() const
    {
        return Shot && !Shot->bSaved && Shot->Problem.IsEmpty();
    }

    // Records the finished capture as a Render check and closes it.
    bool ShotSaved(const TCHAR* Moment)
    {
        const bool bSaved = Shot && Shot->bSaved;
        Check(bSaved, TEXT("Render"), FString::Printf(TEXT("The Host's rendered main view is captured while it shows %s"), Moment),
            Shot ? (bSaved ? Shot->File + TEXT(" ") + Shot->Facts : Shot->Problem) : FString(TEXT("no capture was opened")));
        Shot.Reset();
        return bSaved;
    }
    // The ordinary right-click lock from beyond the reach-carded range: the chase stops within it and the hit lands
    // from farther than the base basic range allows.
    bool TickReach(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        const FLDWorldView* Frame = LDDemoCombatScenario::CombatProbeFrame(PC);
        const FLDObservedUnitView* Target = Frame ? LDDemoCombatScenario::CombatProbeContender(*Frame, ClientId) : nullptr;
        const ALDCharacter* Other = ClientBody(Mode);
        const FLDBodyCombatState* Victim = Other ? Mode.Combat->StateFor(Other) : nullptr;
        if (!Target || !Victim)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeSkillSeconds) { return true; }
            return Fail(TEXT("Skills"), TEXT("Client1 left the Host's sight before the reach check"));
        }
        HitBeforeMs = Victim->Cues.LastHitAtMs;
        PC.Navigation->AttackUnit(Target->Handle, Target->ObservationEpoch);
        ReachOrder = PC.Navigation->LocalIdentity();
        // Every swing of this lock is a chance to capture the own body's strike.
        BeginShot(TEXT("HostSees-OwnStrike.png"), [this](FString& Facts) { return ShowsOwn(ELDObservedActionCue::Attack, .17f, .32f, Facts); },
            LDDemoCombatScenario::CombatProbeStepSeconds);
        SetStage(EStage::ReachHit);
        return true;
    }

    bool TickReachHit(double Now, ALDPlayerController& PC, ALDGameMode& Mode)
    {
        if (!bReachMeasured)
        {
            const ALDCharacter* Own = Cast<ALDCharacter>(PC.GetPawn());
            const ALDCharacter* Other = ClientBody(Mode);
            const FLDBodyCombatState* Victim = Other ? Mode.Combat->StateFor(Other) : nullptr;
            const FLDOrderReceipt& Latest = PC.Navigation->LatestReceipt();
            if (Latest.Identity == ReachOrder && (Latest.Status == ELDOrderStatus::Rejected || Latest.Status == ELDOrderStatus::Cancelled
                || Latest.Status == ELDOrderStatus::Completed))
            {
                return Fail(TEXT("Skills"), TEXT("The reach check's attack lock ended before its hit: ") + PC.Navigation->Status);
            }
            if (!Own || !Victim || Victim->Cues.LastHitAtMs <= HitBeforeMs)
            {
                if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
                return Fail(TEXT("Skills"), TEXT("The reach check's basic attack never hit Client1: ") + PC.Navigation->Status);
            }
            ReachHitCm = FVector::Dist2D(ALDCombatDirector::FootOf(*Own), ALDCombatDirector::FootOf(*Other));
            const FLDDemoContentDefinitions& Defs = Catalog->Get();
            const double Base = LDDemoCombatScenario::CombatProbeRational(Defs.Parameters.AttackRangeCm.Base);
            const FLDDemoAttributeDefinition* Reach = nullptr;
            FString ReachError;
            double Bonus = 0;
            if (Catalog->FindAttribute(LDDemoCombatScenario::CombatProbeSkillPlan(true)[0].ToString(), Reach, ReachError) && Reach)
            {
                for (const FLDDemoStatBonus& Stat : Reach->Stats)
                {
                    Bonus += Stat.Stat == ELDDemoBonusStat::AttackRangeCmFlat ? LDDemoCombatScenario::CombatProbeRational(Stat.Cm) : 0.0;
                }
            }
            Check(Bonus > 0 && FMath::IsNearlyEqual(double(PC.CombatView.AttackRangeCm), Base + Bonus, 0.01)
                && ReachHitCm > Base && ReachHitCm <= Base + Bonus + 1.0, TEXT("Skills"),
                TEXT("The reach card lands a basic hit from beyond the base basic range in a real chase"),
                FString::Printf(TEXT("hit_gap_cm=%.1f base_cm=%.0f bonus_cm=%.0f projected_cm=%.0f %s"), ReachHitCm, Base, Bonus,
                    PC.CombatView.AttackRangeCm, *ReachError));
            if (!Passed) { return Finish(); }
            bReachMeasured = true;
        }
        // The lock keeps swinging until a strike is captured.
        if (ShotPending()) { return true; }
        PC.StopCombatAndOrders();
        if (!ShotSaved(TEXT("its own body's basic strike")) || !Mark(TEXT("HostSkillsDone.json"))) { return Finish(); }
        SetStage(EStage::Wrap);
        return true;
    }

    bool TickWrap(const ALDGameState& Public, const ALDPlayerController& PC)
    {
        if (!Exists(TEXT("Client1.json"))) { return true; }
        const FLDPublicMatchView& Match = Public.PublicMatch;
        Check(Match.Generation == PC.OwnerState.Match
            && (Match.Phase == ELDMatchPhase::Development || Match.Phase == ELDMatchPhase::CaptureOpen), TEXT("Protection"),
            TEXT("The match stayed open through the whole scenario: no capture result, fault or reset"));
        return Finish();
    }

    // After the reports each process exits on the runner's signal: the client on FinishClients, the Host on Finish.
    bool TickAfterReport() const
    {
        if (!Exists(IsHost() ? TEXT("Finish") : TEXT("FinishClients"))) { return true; }
        FPlatformMisc::RequestExitWithStatus(false, 0);
        return false;
    }

    bool Finish()
    {
        if (Written) { return true; }
        const TSharedPtr<FJsonObject> FeelFields = Passed ? CheckFeel().ToSharedPtr() : TSharedPtr<FJsonObject>();
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), Passed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetBoolField(TEXT("dev_probe"), true);
        Report->SetBoolField(TEXT("normal_start"), true);
        Report->SetStringField(TEXT("scope"), IsHost()
            ? TEXT("DEV-only owned-connection combat/orders scenario, target and killer role: a participating Listen Host that is attacked, audits the hits, kills with a right-click order and audits the respawn; then, through its DEV growth fixture's preset offers and XP grants, learns attr_reach and active_binding_snare by ordinary choices, roots Client1, observes its shield, is hit by Client1's arc bolt and lands a reach-carded basic hit. It renders its main view and keeps captures of the observed root, shield and chalk flight for presentation review (not compared by pixels). Not camps, AI, capture or full-match acceptance.")
            : TEXT("DEV-only owned-connection combat/orders scenario, attacker and victim role: attack-move, S, death, the random protected respawn and an empty Q through the owning connection; then learns active_guard_pulse and active_arc_bolt from fixture-preset offers by ordinary choices, shields itself and dispels the Host's root, and throws the arc bolt at the Host. Not camps, AI, capture or full-match acceptance."));
        Report->SetBoolField(TEXT("growth_fixture"), LDDevGrowthFixture::IsSwitchPresent());
        Report->SetNumberField(TEXT("skills_learned"), SkillsLearned);
        Report->SetStringField(TEXT("catalog_sha256"), CatalogDigest);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetNumberField(TEXT("stage"), int32(Stage));
        Report->SetArrayField(TEXT("checks"), Checks);
        if (FeelFields) { Report->SetObjectField(TEXT("feel"), FeelFields); }
        if (const ALDPlayerController* PC = Controller.Get())
        {
            Report->SetNumberField(TEXT("generation"), PC->OwnerState.Match);
            Report->SetStringField(TEXT("entity"), LDDemoCombatScenario::CombatProbeGuid(PC->OwnerState.ContenderId));
            Report->SetNumberField(TEXT("life"), PC->OwnerState.Life);
        }
        if (IsHost())
        {
            Report->SetStringField(TEXT("respawn_anchor"), RespawnAnchor);
            Report->SetNumberField(TEXT("respawn_at_ms"), double(RespawnAtMs));
            Report->SetNumberField(TEXT("protected_until_ms"), double(ProtectedUntilMs));
            Report->SetNumberField(TEXT("attack_orders"), AttackOrders);
            Report->SetNumberField(TEXT("skill_gap_cm"), SkillGapCm);
            Report->SetNumberField(TEXT("projectile_samples"), ProjectileSamples);
            Report->SetArrayField(TEXT("screenshots"), Screenshots);
            Report->SetNumberField(TEXT("reach_hit_cm"), ReachHitCm);
        }
        else
        {
            Report->SetNumberField(TEXT("spent_receipt_phase"), int32(SpentReceipt.Phase));
            Report->SetNumberField(TEXT("spent_receipt_reject"), int32(SpentReceipt.Reject));
            Report->SetBoolField(TEXT("spent_protection_removed"), SpentReceipt.bProtectionRemoved);
            Report->SetNumberField(TEXT("root_until_ms"), double(RootUntilMs));
        }
        Written = Save(Report, Role + TEXT(".json"));
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_COMBAT_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
        if (!Passed || !Written)
        {
            FPlatformMisc::RequestExitWithStatus(false, 2);
            return false;
        }
        return true;
    }
};

void LDDemoTests::StartCombatProbe(ALDPlayerController& Controller)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoCombatProbe")) || !Controller.IsLocalController()
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap)
    {
        return;
    }
    const TSharedRef<FLDDemoCombatProbe> Probe = MakeShared<FLDDemoCombatProbe>();
    int32 Timeout = 600;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoCombatTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoCombatOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoCombatRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1")) || Timeout < 300 || Timeout > 1500
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_COMBAT_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    Probe->Controller = &Controller;
    Probe->World = Controller.GetWorld();
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .1f);
}
#endif
