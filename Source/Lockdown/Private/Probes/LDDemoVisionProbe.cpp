#include "Probes/LDDemoVisionProbe.h"
#include "Probes/LDScenarioWalk.h"
#include "World/LDSpawnPlacement.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/SpringArmComponent.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDCombatDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Navigation/LDPathSteering.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "AIController.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/LightComponentBase.h"
#include "Components/PrimitiveComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "UObject/CoreNet.h"

struct FLDDemoVisionReactionAccess
{
    static void Queue(ALDCombatDirector& Combat, const FLDCombatBodyRef& Setter,
        const FLDCombatBodyRef& Trigger, const FLDCombatBodyRef& Target)
    {
        const auto Hit = [&Target](const FLDCombatBodyRef& Source)
        {
            FLDCombatHit Result;
            Result.HitId = Result.EffectId = FGuid::NewGuid();
            Result.Target = Target;
            Result.SourceBody = Source;
            Result.SourceEntity = Result.CreditOwner = Source.Victim.EntityId;
            Result.Source.Kind = ELDCombatSourceKind::Basic;
            Result.HitClass = ELDCombatHitClass::Ability;
            Result.DamageType = ELDDemoDamageType::Magical;
            return Result;
        };
        FLDCombatHit Strike = Hit(Setter);
        Strike.BaseP2 = 100;
        Combat.PendingHits.Add(MoveTemp(Strike));
        FLDCombatHit Wet = Hit(Setter);
        Wet.NativeElements = LDCombatElements::Bit(ELDCombatElement::Water);
        FLDCombatHit Electric = Hit(Trigger);
        Electric.NativeElements = LDCombatElements::Bit(ELDCombatElement::Shock);
        Combat.PendingHits.Add(MoveTemp(Wet));
        Combat.PendingHits.Add(MoveTemp(Electric));
    }
};

// Spec §8.3 ice inertia over a real owning connection: the Host's own body owns an ice field placed through the native
// ground path at the walker's foot, so the walker stands on an enemy's ice; long enough for one bounded walk across it.
static bool ReactionOnly() { return FParse::Param(FCommandLine::Get(), TEXT("LDDemoVisionReactionOnly")); }
static bool IceOnly() { return FParse::Param(FCommandLine::Get(), TEXT("LDDemoVisionIceOnly")); }
static bool HeadlessOnly() { return ReactionOnly() || IceOnly(); }

struct FLDDemoVisionIceAccess
{
    static bool Spawn(ALDCombatDirector& Combat, const FLDCombatBodyRef& Owner, const FGuid& OwnerEntity, const FVector& Foot,
        int64 NowMs, FString& Error)
    {
        FLDElementApplier Applier;
        Applier.SourceEntity = Applier.CreditOwner = OwnerEntity;
        Applier.SourceBody = Owner;
        Applier.Level = 1;
        return Combat.SpawnGround(ELDGroundStateKind::Ice, Applier, Foot, NowMs, Error, IceRadiusCm, IceLifeMs);
    }
    static constexpr double IceRadiusCm = 600.;
    static constexpr int64 IceLifeMs = 20000;
};

// Carries the topology-only assertions of the retired LDViewNetworkProbe (#1, #9, #15, #23, #28 of the observation
// closure plan) plus the Listen Host presentation gate. Codec, ordering, handles and sight stay in
// Lockdown.Rules.DemoVision.*; this scenario does not repeat them. Named, not anonymous: unity builds merge files.
namespace LDDemoVisionScenario
{
const TCHAR* const VisionRoles[] = {TEXT("Host"), TEXT("Client1"), TEXT("Client2")};
// Each owner has a separate legal arrival disk near the public capture anchor.
constexpr double ArrivalCm = 150.0;
// One waypoint's bound, repeated orders included.
constexpr double WalkWaypointSeconds = 30.0;

struct FLDSeenUnit
{
    FGuid Handle;
    uint32 Epoch = 0;
};

// True when any drawing or sounding part of Actor, or of an actor attached to it, can present in this process.
bool HasPresentation(const AActor& Actor, FString& Part)
{
    if (Actor.IsHidden()) { return false; }
    TInlineComponentArray<USceneComponent*> Components(&Actor, true);
    for (const USceneComponent* Component : Components)
    {
        if (!Component) { continue; }
        const bool bDraws = Component->IsA<UPrimitiveComponent>() || Component->IsA<ULightComponentBase>() || Component->IsA<UDecalComponent>();
        const UAudioComponent* Audio = Cast<UAudioComponent>(Component);
        if ((bDraws && Component->IsVisible()) || (Audio && Audio->IsPlaying()))
        {
            Part = Actor.GetName() + TEXT(".") + Component->GetName();
            return true;
        }
    }
    TArray<AActor*> Attached;
    Actor.GetAttachedActors(Attached);
    for (const AActor* Child : Attached)
    {
        if (Child && HasPresentation(*Child, Part)) { return true; }
    }
    return false;
}

// The owner's current authorized frame, or null while it is unavailable or stale.
const FLDWorldView* CurrentFrame(const ALDPlayerController& Controller, const ALDWorldViewRelay*& Relay)
{
    Relay = Controller.LocalView();
    if (!Relay) { return nullptr; }
    const FLDObservedFrameRead Read = Relay->ReadObservedFrame();
    return Read.Status == ELDObservedViewStatus::Current ? Read.Frame : nullptr;
}

int32 OtherContenders(const FLDWorldView& Frame, const FGuid& Self)
{
    int32 Count = 0;
    for (const FLDObservedUnitView& Unit : Frame.Units)
    {
        Count += Unit.Kind == ELDObservedUnitKind::Contender && Unit.ContenderId != Self ? 1 : 0;
    }
    return Count;
}

int32 CountProxies(const UWorld& World)
{
    int32 Count = 0;
    for (TActorIterator<ALDObservedEntityProxy> It(&World); It; ++It) { ++Count; }
    return Count;
}

// Every proxy in this world presents a unit of the owner's current frame, and every unit has exactly one proxy.
bool ProxiesMatchFrame(const UWorld& World, const ALDWorldViewRelay& Relay, const FLDWorldView& Frame, FString& Why)
{
    for (TActorIterator<ALDObservedEntityProxy> It(&World); It; ++It)
    {
        const ALDObservedEntityProxy* Proxy = *It;
        if (!Frame.Units.ContainsByPredicate([Proxy](const FLDObservedUnitView& Unit)
            { return Unit.Handle == Proxy->ObservedHandle && Unit.ObservationEpoch == Proxy->ObservedEpoch; }))
        {
            Why = TEXT("a proxy presents no unit of the current frame");
            return false;
        }
    }
    const int32 Proxies = CountProxies(World);
    if (Proxies != Relay.ProxyCount() || Proxies != Frame.Units.Num())
    {
        Why = FString::Printf(TEXT("%d proxies, %d relay proxies, %d units"), Proxies, Relay.ProxyCount(), Frame.Units.Num());
        return false;
    }
    Why.Reset();
    return true;
}

class FLDDemoVisionProbe : public TSharedFromThis<FLDDemoVisionProbe>
{
public:
    enum class EStall : uint8 { Waiting, Lossy, MovingWithoutFrames, Recovering, Done };

    TWeakObjectPtr<ALDPlayerController> Controller;
    TWeakObjectPtr<UWorld> World;
    FString Output, Role, Error, CatalogDigest, PendingImage, SpawnViolation, SpawnVisibilityViolation;
    double Deadline = 0, StageAt = 0, NextCommandAt = 0, NextReadyAt = 0, BlockedSince = 0, SightDeadline = 0, ImageAt = 0, DisconnectAt = 0;
    double ReactionQueuedAt = 0, ReactionAt = 0;
    // Ice scenario (spec §8.3): the Host's queue, the walker's start and the server corrections around the walk.
    bool bIceReady = false, bIceQueued = false, bIceServerScaled = false, bIceWalking = false;
    double IceSeenAt = 0, IceWalkAt = 0, IceTopSpeedAt = 0;
    uint32 IceCorrectionBaseline = 0;
    double IceTopSpeed = 0;
    FVector IceGoal = FVector::ZeroVector;
    int32 Stage = 0, WalkWaypoint = 0, SpawnFramesChecked = 0, SpawnVisibilityFramesChecked = 0, PreStallProxies = 0;
    bool Passed = true, Written = false, bJoined = false, bLobbyConfigured = false, bStallAnnounced = false;
    bool bControlObserved = false, bControlViolated = false, bEmulating = false, DisconnectVerified = false;
    bool bReactionReady = false, bReactionQueued = false, bReactionLeaveRequested = false;
    uint64 ReactionBaseline = 0;
    uint32 HitReceiptBaseline = 0;
    EStall Stall = EStall::Waiting;
    FLDCombatBodyRef OwnBody;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TArray<LDScenarioWalk::FKeepout> WalkKeepouts;
    TArray<FVector> WalkWaypoints;
    TArray<FVector> ArrivalGoals;
    int32 FixtureRoute = 0;
    int32 NativeQueries = 0;
    // The other two roles' arrival goals are keepouts of this role's route, added once; a waypoint whose order ended
    // short (another walker stood on it) is ordered again within WalkWaypointSeconds.
    bool bGoalKeepouts = false;
    double WalkOrderedAt = 0;
    int32 WalkReorders = 0;
    double MaxRouteSeconds = 0;
    double MaxRouteTickSeconds = 0, MaxQuerySeconds = 0, RouteStarted = 0;
    bool bPlanning = false;
    LDScenarioWalk::FPlanner RoutePlanner;

    LDScenarioWalk::EPlan PlanWalk(const ULDMapRegistry& Registry, const FVector& Foot, const FVector& Goal,
        TArray<FVector>& Route, FString& Why)
    {
        const double Began = FPlatformTime::Seconds();
        if (!bPlanning)
        {
            RouteStarted = Began;
            Route.Reset();
            if (!LDScenarioWalk::Begin(RoutePlanner, Registry, *Catalog, Foot, Goal, WalkKeepouts, ArrivalCm, Why, &MaxQuerySeconds, &NativeQueries))
            { return LDScenarioWalk::EPlan::Failed; }
            bPlanning = true;
        }
        const auto Result = LDScenarioWalk::Step(RoutePlanner, Registry, *Catalog, Route, Why, &MaxQuerySeconds, &NativeQueries);
        const double Seconds = FPlatformTime::Seconds() - Began;
        MaxRouteTickSeconds = FMath::Max(MaxRouteTickSeconds, Seconds);
        UE_LOG(LogLDTest, Display, TEXT("LD_VISION_FIXTURE_WORK role=%s validation=%d tick_seconds=%.6f max_query_seconds=%.6f"),
            *Role, FixtureRoute, Seconds, MaxQuerySeconds);
        if (Result != LDScenarioWalk::EPlan::Working)
        {
            bPlanning = false;
            const double WallSeconds = FPlatformTime::Seconds() - RouteStarted;
            MaxRouteSeconds = FMath::Max(MaxRouteSeconds, WallSeconds);
            UE_LOG(LogLDTest, Display, TEXT("LD_VISION_FIXTURE_ROUTE role=%s validation=%d wall_seconds=%.6f result=%u from=%s goal=%s"),
                *Role, FixtureRoute, WallSeconds, uint32(Result), *Foot.ToString(), *Goal.ToString());
        }
        return Result;
    }
    TArray<FLDSeenUnit> PreStall;
    TArray<uint8> StalledGround;
    FVector BeforeGroundMove = FVector::ZeroVector;
    FVector WalkStart = FVector::ZeroVector;
    bool bExplicitLeaveSent=false;
    double ExplicitLeaveSentAt=0;
    TSet<FGuid> RemoteContenders;
    TArray<TWeakObjectPtr<APawn>> SpawnedPawns;
    TArray<TSharedPtr<FJsonValue>> Checks, Images;
    FDelegateHandle SpawnHandle, PostTickHandle;
#if DO_ENABLE_NET_TEST
    FPacketSimulationSettings SavedEmulation;
#endif

    ~FLDDemoVisionProbe() { StopWatching(); }

    bool Check(bool bValue, const TCHAR* Id, const FString& Name)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("id"), Id);
        Row->SetStringField(TEXT("name"), Name);
        Row->SetBoolField(TEXT("passed"), bValue);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        Passed &= bValue;
        if (!bValue && Error.IsEmpty()) { Error = FString(Id) + TEXT(" ") + Name; }
        return bValue;
    }

    bool Fail(const TCHAR* Id, const FString& Why)
    {
        Check(false, Id, Why);
        return Finish();
    }

    bool Save(const TSharedRef<FJsonObject>& Value, const FString& Name) const
    {
        FString Json;
        const FString File = FPaths::Combine(Output, Name);
        return FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *(File + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*File, *(File + TEXT(".tmp")), true, true);
    }

    bool Mark(const FString& Name)
    {
        auto Value = MakeShared<FJsonObject>();
        Value->SetBoolField(TEXT("ready"), true);
        if (Save(Value, Name)) { return true; }
        Check(false, TEXT("#1"), TEXT("The isolated scenario could not persist its marker ") + Name);
        return false;
    }

    bool Exists(const FString& Name) const { return FPaths::FileExists(FPaths::Combine(Output, Name)); }

    bool AllMarkers(const TCHAR* Suffix) const
    {
        for (const TCHAR* Name : VisionRoles)
        {
            if (!Exists(FString(Name) + Suffix)) { return false; }
        }
        return true;
    }

    bool ReadReactionContender(const FString& Participant, FGuid& Contender, const TCHAR* Suffix = TEXT("ReactionReady.json")) const
    {
        FString Json, Id;
        TSharedPtr<FJsonObject> Value;
        return FFileHelper::LoadFileToString(Json, *FPaths::Combine(Output, Participant + Suffix))
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Value) && Value.IsValid()
            && Value->TryGetStringField(TEXT("contender"), Id) && FGuid::Parse(Id, Contender) && Contender.IsValid();
    }

    // Listen Host only: every body other than the Host's own must draw nothing by the end of its spawn frame,
    // before that frame renders and before any observation sample could hide it.
    void WatchSpawns(UWorld& InWorld)
    {
        TWeakPtr<FLDDemoVisionProbe> Weak = AsShared();
        SpawnHandle = InWorld.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([Weak](AActor* Actor)
        {
            const TSharedPtr<FLDDemoVisionProbe> Probe = Weak.Pin();
            if (APawn* Pawn = Cast<APawn>(Actor); Probe && Pawn) { Probe->SpawnedPawns.Emplace(Pawn); }
        }));
        PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddLambda([Weak](UWorld* Ticked, ELevelTick, float)
        {
            const TSharedPtr<FLDDemoVisionProbe> Probe = Weak.Pin();
            if (!Probe || Ticked != Probe->World.Get()) { return; }
            for (const TWeakObjectPtr<APawn>& Spawned : Probe->SpawnedPawns)
            {
                const APawn* Pawn = Spawned.Get();
                if (!Pawn || Pawn->IsLocallyControlled()) { continue; }
                FString Part;
                if (HasPresentation(*Pawn, Part)) { Probe->SpawnViolation += Part + TEXT(" "); }
                ++Probe->SpawnFramesChecked;
                if (const auto* Body = Cast<ALDCharacter>(Pawn); Body && Body->Body)
                {
                    TArray<USceneComponent*> Descendants;
                    Body->Body->GetChildrenComponents(true, Descendants);
                    Descendants.Add(Body->Body);
                    for (const USceneComponent* Component : Descendants)
                    {
                        if (Component && Component->GetVisibleFlag())
                        {
                            Probe->SpawnVisibilityViolation += Pawn->GetName() + TEXT(".") + Component->GetName() + TEXT(" ");
                            break;
                        }
                    }
                    ++Probe->SpawnVisibilityFramesChecked;
                }
            }
            Probe->SpawnedPawns.Reset();
        });
    }

    void StopWatching()
    {
        if (UWorld* Watched = World.Get(); Watched && SpawnHandle.IsValid()) { Watched->RemoveOnActorSpawnedHandler(SpawnHandle); }
        if (PostTickHandle.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle); }
        SpawnHandle.Reset();
        PostTickHandle.Reset();
    }

    // Client1 only: the engine's network emulation drops every packet this client receives, so its relay sees a
    // real gap while the server keeps publishing. No observation code path is altered for the test.
    bool BeginFrameLoss(UWorld& InWorld)
    {
#if DO_ENABLE_NET_TEST
        UNetDriver* Driver = InWorld.GetNetDriver();
        if (!Check(Driver && Driver->ServerConnection, TEXT("#23"), TEXT("Fixture: this client has its owning server connection")))
        { return false; }
        SavedEmulation = Driver->PacketSimulationSettings;
        FPacketSimulationSettings Loss = SavedEmulation;
        Loss.PktIncomingLoss = 100;
        Driver->SetPacketSimulationSettings(Loss);
        bEmulating = true;
        return Check(Driver->ServerConnection->PacketSimulationSettings.PktIncomingLoss == 100, TEXT("#23"),
            TEXT("Fixture: only this client drops every incoming packet (engine network emulation)"));
#else
        return Check(false, TEXT("#23"), TEXT("Fixture: engine network emulation is unavailable in this build"));
#endif
    }

    void EndFrameLoss()
    {
#if DO_ENABLE_NET_TEST
        if (!bEmulating) { return; }
        if (UWorld* Emulated = World.Get())
        {
            if (UNetDriver* Driver = Emulated->GetNetDriver()) { Driver->SetPacketSimulationSettings(SavedEmulation); }
        }
        bEmulating = false;
#endif
    }

    // #15: an owning client's world holds only its own body, state, controller and relay; other units are proxies.
    void CheckRestrictedRegistry(const UWorld& InWorld, const ALDPlayerController& Owner, const ALDWorldViewRelay& Relay, const FLDWorldView& Frame)
    {
        int32 Pawns = 0, States = 0, Controllers = 0, Relays = 0, Directors = 0, Brains = 0;
        bool bOwnOnly = true;
        for (TActorIterator<APawn> It(&InWorld); It; ++It) { ++Pawns; bOwnOnly &= *It == Owner.GetPawn(); }
        for (TActorIterator<APlayerState> It(&InWorld); It; ++It) { ++States; bOwnOnly &= *It == Owner.PlayerState; }
        for (TActorIterator<APlayerController> It(&InWorld); It; ++It) { ++Controllers; bOwnOnly &= *It == &Owner; }
        for (TActorIterator<ALDWorldViewRelay> It(&InWorld); It; ++It) { ++Relays; bOwnOnly &= It->GetOwner() == &Owner; }
        for (TActorIterator<ALDVisionDirector> It(&InWorld); It; ++It) { ++Directors; }
        for (TActorIterator<AAIController> It(&InWorld); It; ++It) { ++Brains; }
        const AGameStateBase* Public = InWorld.GetGameState();
        Check(Pawns == 1 && States == 1 && Controllers == 1 && Relays == 1 && Directors == 0 && Brains == 0 && bOwnOnly
            && Public && Public->PlayerArray.Num() == 1, TEXT("#15"), FString::Printf(
            TEXT("Restricted registry holds only this owner's body, state, controller and relay (pawns %d, states %d, controllers %d, relays %d, directors %d, AI %d)"),
            Pawns, States, Controllers, Relays, Directors, Brains));
        FString Why;
        Check(ProxiesMatchFrame(InWorld, Relay, Frame, Why), TEXT("#15"),
            TEXT("Other units exist here only as proxies of this owner's current frame") + (Why.IsEmpty() ? FString() : TEXT(": ") + Why));
    }

    // #15H (VISION_AND_UI field permissions and Listen Host, CT-A17): the Listen Host shows other units only through its own projection.
    void CheckHostPresentation(const UWorld& InWorld, const ALDGameMode& Mode, const ALDPlayerController& Local,
        const ALDWorldViewRelay& Relay, const FLDWorldView& Frame)
    {
        int32 Others = 0, Hidden = 0;
        FString Shown;
        for (TActorIterator<APawn> It(&InWorld); It; ++It)
        {
            if (*It == Local.GetPawn()) { continue; }
            ++Others;
            FString Part;
            if (HasPresentation(**It, Part)) { Shown += Part + TEXT(" "); } else { ++Hidden; }
        }
        int32 Contenders = 0, Colliding = 0;
        const FLDMatchContext& Match = Mode.GetMatchContext();
        for (FConstPlayerControllerIterator It = InWorld.GetPlayerControllerIterator(); It; ++It)
        {
            ALDPlayerController* Other = Cast<ALDPlayerController>(It->Get());
            if (!Other || Other == &Local) { continue; }
            const FGuid Contender = Mode.ContenderId(Other);
            FLDContenderLifeRead Life;
            FString Why;
            ALDCharacter* Body = nullptr;
            if (!Contender.IsValid() || !Mode.ReadContenderLife(Match, Contender, Life, Why) || Life.Phase != ELDLifePhase::Alive
                || Mode.ResolveBody(Match, Life.Body, Body) != ELDCommitReject::None || !Body) { continue; }
            ++Contenders;
            RemoteContenders.Add(Contender);
            Colliding += Body->GetActorEnableCollision() && Body->GetCapsuleComponent()->IsCollisionEnabled() ? 1 : 0;
        }
        Check(Others >= 2 && Hidden == Others, TEXT("#15H"), FString::Printf(
            TEXT("The Listen Host draws no canonical body but its own (%d of %d hidden)%s"), Hidden, Others,
            *(Shown.IsEmpty() ? FString() : TEXT("; drawn: ") + Shown)));
        Check(Contenders == 2 && Colliding == 2, TEXT("#15H"),
            TEXT("Both remote contenders keep living, colliding authoritative bodies while hidden from the Host view"));
        Check(SpawnFramesChecked >= 2 && SpawnViolation.IsEmpty(), TEXT("#15H"), FString::Printf(
            TEXT("No other body drew at the end of its spawn frame (%d checked)%s"), SpawnFramesChecked,
            *(SpawnViolation.IsEmpty() ? FString() : TEXT("; drawn: ") + SpawnViolation)));
        Check(SpawnVisibilityFramesChecked >= 2 && SpawnVisibilityViolation.IsEmpty(), TEXT("#15H"), FString::Printf(
            TEXT("Every other body's complete visual subtree starts hidden (%d checked)%s"), SpawnVisibilityFramesChecked,
            *(SpawnVisibilityViolation.IsEmpty() ? FString() : TEXT("; visible: ") + SpawnVisibilityViolation)));
        int32 Drawn = 0;
        for (TActorIterator<ALDObservedEntityProxy> It(&InWorld); It; ++It)
        {
            FString Part;
            Drawn += HasPresentation(**It, Part) ? 1 : 0;
        }
        FString Why;
        Check(ProxiesMatchFrame(InWorld, Relay, Frame, Why) && Drawn == Frame.Units.Num(), TEXT("#15H"),
            TEXT("The Host shows each other unit once, as a drawn proxy of its own current frame") + (Why.IsEmpty() ? FString() : TEXT(": ") + Why));
        int32 Copies = 0, Silent = 0;
        for (TActorIterator<ALDWorldViewRelay> It(&InWorld); It; ++It)
        {
            if (It->GetOwner() == &Local) { continue; }
            ++Copies;
            Silent += It->ProxyCount() == 0 && It->ReadObservedFrame().Status == ELDObservedViewStatus::Unavailable ? 1 : 0;
        }
        Check(Copies == 2 && Silent == 2, TEXT("#15H"), TEXT("Server copies of the remote owners' relays present and hold nothing on the Host"));
    }

    // #23: a real 250 ms gap destroys every proxy and leaves no unit, coverage or LastSeen handle behind.
    void CheckTimeoutClear(const UWorld& InWorld, const ALDWorldViewRelay& Relay, const FLDObservedFrameRead& Read)
    {
        Check(Relay.ProxyCount() == 0 && CountProxies(InWorld) == 0, TEXT("#23"), FString::Printf(
            TEXT("All %d presented proxies are destroyed once 250 ms pass without a full frame"), PreStallProxies));
        bool bDark = true;
        if (Read.Frame) { for (const uint8 Byte : Read.Frame->Footprint.CurrentBits) { bDark &= Byte == 0; } }
        Check(Read.Frame && Read.Frame->Units.IsEmpty() && bDark, TEXT("#23"),
            TEXT("The timed-out authority view keeps no unit or authorized coverage"));
        bool bNoLastSeen = true, bTimedOut = true;
        FLDObservedUnitView Ignored;
        for (const FLDSeenUnit& Seen : PreStall)
        {
            bNoLastSeen &= !Relay.ReadUnit(Seen.Handle, Seen.Epoch, Ignored);
            bTimedOut &= Relay.RecentClears().ContainsByPredicate([&Seen](const FLDObservedClear& Clear)
            { return Clear.Handle == Seen.Handle && Clear.ObservationEpoch == Seen.Epoch && Clear.Reason == ELDObservedClearReason::Timeout; });
        }
        Check(bNoLastSeen, TEXT("#23"), TEXT("No previously visible handle still resolves after the timeout (no LastSeen)"));
        Check(bTimedOut, TEXT("#23"), TEXT("Every previously visible unit was cleared by this owner's timeout, not by a server concealment"));
    }

    bool TickJoin(const UWorld& InWorld, ALDPlayerController& Owner, const FLDLobbyView& Lobby, const UNetDriver* Driver)
    {
        if (!Owner.IsLocalController() || !Owner.GetLocalPlayer() || !Lobby.Generation) { return true; }
        if (!bJoined)
        {
            if (!Mark(Role + TEXT("Joined.json")) || (Role == TEXT("Host") && !Mark(TEXT("HostReady.json")))) { return Finish(); }
            bJoined = true;
        }
        if (Role == TEXT("Host"))
        {
            if (!AllMarkers(TEXT("Joined.json")) || Lobby.Members.Num() != 3 || !Driver || Driver->ClientConnections.Num() != 2) { return true; }
            Check(InWorld.GetNetMode() == NM_ListenServer && Owner.HasAuthority(), TEXT("#1"),
                TEXT("A participating Listen Host with its own local player serves two real owning connections and a three-human roster"));
        }
        else
        {
            Check(InWorld.GetNetMode() == NM_Client && !Owner.HasAuthority() && Owner.GetNetConnection() != nullptr, TEXT("#1"),
                TEXT("This contender plays through a real owning remote connection"));
        }
        if (!Passed) { return Finish(); }
        Stage = 1;
        return true;
    }

    // The normal lobby path: no AI seats, every human ready, then the Host's ordinary Start request.
    bool TickLobby(double Now, ALDPlayerController& Owner, const FLDLobbyView& Lobby)
    {
        if (Lobby.Phase == ELDMatchPhase::Development || Lobby.Phase == ELDMatchPhase::CaptureOpen) { Stage = 2; return true; }
        if (Lobby.Phase == ELDMatchPhase::Starting || Lobby.Phase == ELDMatchPhase::Rest) { return true; }
        if (Lobby.Phase != ELDMatchPhase::Lobby)
        {
            return Fail(TEXT("#1"), TEXT("The room left the lobby without opening play: ")
                + StaticEnum<ELDMatchPhase>()->GetNameStringByValue(int64(Lobby.Phase)));
        }
        if (Role == TEXT("Host") && !bLobbyConfigured)
        {
            if (Lobby.AICount != 0)
            {
                if (Now >= NextCommandAt) { Owner.SetDemoAICount(0); NextCommandAt = Now + 1; }
                return true;
            }
            // The match's daily modifiers are fixed to two that touch neither sight nor bodies (a shop sale and the
            // monster XP scale): a drawn blackout shortens the fixture's sight radius and a typhoon pushes the three
            // contenders off their mutually visible targets.
            ALDGameMode* Mode = Owner.GetWorld() ? Owner.GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
            if (!Mode) { return Fail(TEXT("#1"), TEXT("The Listen Host has no authority game mode to fix the daily modifiers")); }
            Mode->DevDailyPreset = {ELDDailyModifier::Sale, ELDDailyModifier::Exam};
            if (!Mark(TEXT("LobbyConfigured.json"))) { return Finish(); }
            bLobbyConfigured = true;
        }
        if (!Exists(TEXT("LobbyConfigured.json"))) { return true; }
        if (!Owner.OwnerState.bReady && Now >= NextReadyAt) { Owner.SetDemoReady(true); NextReadyAt = Now + 1; }
        if (Role != TEXT("Host")) { return true; }
        const bool bAllReady = Lobby.Members.Num() == 3
            && !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Member) { return !Member.Ready; });
        if (!bAllReady) { BlockedSince = 0; return true; }
        if (Lobby.CanStart)
        {
            BlockedSince = 0;
            if (Now >= NextCommandAt) { Owner.StartDemoMatch(); NextCommandAt = Now + 2; }
            return true;
        }
        if (BlockedSince == 0) { BlockedSince = Now; }
        if (Now - BlockedSince <= 20) { return true; }
        return Fail(TEXT("#1"), TEXT("The normal lobby Start stays refused with every human ready: ") + Lobby.StartBlocker);
    }

    bool TickBody(double Now, ALDPlayerController& Owner, const ULDMapRegistry& Registry, const FLDMapBinding& Capture)
    {
        const auto* Body = Cast<ALDCharacter>(Owner.GetPawn());
        if (!Body || Owner.OwnerState.LifePhase != ELDLifePhase::Alive
            || !Owner.OwnerState.BodyRef.IsValid() || !Owner.CanWorldCommand()) { return true; }
        const bool bPeaceful=FParse::Param(FCommandLine::Get(),TEXT("LDDemoVisionPeaceful"));
        if(bPeaceful&&!Owner.bDevPeaceful)
        {
            const auto* Public=Owner.GetWorld()->GetGameState<ALDGameState>();
            if(!Public||!Public->DemoLobby.DevTestSession)
            {if(Role==TEXT("Host")){Owner.ServerEnableDevTest(Owner.OwnerState.Match);}return true;}
            ELDGrowthReason Reason;
            Owner.SubmitDevTest(ELDDevTestOp::SetPeaceful,1,Reason);
            return true;
        }
        FString Why;
        const auto* Geometry = Registry.GetGeometry();
        FCollisionShape Capsule;
        if (!Catalog)
        {
            if (!Geometry || !LDDemoEntry::LoadRuntimeContent(Catalog, Why)
                || !ULDMapRegistry::ContentCapsule(*Catalog, Capsule, Why)
                || !LDScenarioWalk::Keepouts(*Geometry, *Catalog, Capsule.GetCapsuleRadius(),
                    bPeaceful ? LDPathSteering::WaypointToleranceCm : ArrivalCm, WalkKeepouts, Why, !bPeaceful))
            { return Fail(TEXT("#1"), TEXT("The approved Vision arrival fixture could not be prepared: ") + Why); }
            // The peaceful fixture still has physical camp bodies. Only the Boss aggro corridor is exempt.
            if (!bPeaceful)
            {
                TArray<FLDCompletePath> Patrol;
                if (!LDScenarioWalk::PatrolPaths(Registry, *Catalog, Patrol, Why)) { return Fail(TEXT("#1"), Why); }
                const auto& Aggro = Catalog->Get().Boss.AggroRadiusCm;
                const double CorridorRadius = double(Aggro.Numerator) / Aggro.Denominator
                    + Capsule.GetCapsuleRadius() + ArrivalCm + LDPathSteering::WaypointToleranceCm;
                LDScenarioWalk::PatrolKeepouts(Patrol, CorridorRadius, WalkKeepouts);
            }
            FLDSightGeometry FixtureSight;
            if (!FixtureSight.Build(*Geometry, Registry.Revision(), Catalog->Get().Parameters.VisionRadiusCm,Catalog->Get().Parameters.VisionRange, Why,Owner.GetWorld()))
            { return Fail(TEXT("#1"), Why); }
            FLDLegalGround HubGround;ELDWorldQueryFailure HubFailure;
            if(!Registry.QueryGround(Registry.Revision(),Capture.Position,Capsule,Capture.Surface,HubGround,HubFailure,Why))
            {return Fail(TEXT("#1"),Why);}
            const auto Validate = [&](TArray<FVector>& Goals)
            {
                for (FVector& Goal : Goals)
                {
                    FVector Normal;
                    Registry.GetTerrain().Ground(FVector2D(Goal), Goal.Z, Normal);
                    FLDLegalGround Legal;
                    ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
                    const double Began = FPlatformTime::Seconds();
                    ++NativeQueries;
                    const bool bLegal = Registry.QueryGround(Registry.Revision(), Goal, Capsule, Capture.Surface, Legal, Failure, Why);
                    MaxQuerySeconds = FMath::Max(MaxQuerySeconds, FPlatformTime::Seconds() - Began);
                    if (!bLegal) { return false; }
                    FLDCompletePath Path;
                    if(!Registry.QueryCompletePath(Registry.Revision(),HubGround,Legal,Capsule,Path,Failure,Why)){return false;}
                    Goal=LDSpawnPlacement::StandingOrigin(Legal)-FVector(0,0,Capsule.GetCapsuleHalfHeight());
                }
                for (int32 A = 0; A < Goals.Num(); ++A)
                { for (int32 B = 0; B < Goals.Num(); ++B)
                    { if (A != B && FixtureSight.Trace(Goals[A], Goals[B]) != ELDSightResult::Visible) { return false; } } }
                return true;
            };
            if (!LDScenarioWalk::VisionTargets(Capture.Position, Capsule.GetCapsuleRadius(), ArrivalCm, FixtureSight.Radius(),
                WalkKeepouts, Validate, ArrivalGoals, Why)) { return Fail(TEXT("#1"), Why); }
            Check(true, TEXT("#1"), TEXT("Three native legal-ground targets are mutually visible outside the complete public Boss patrol corridor"));
        }
        const int32 RoleIndex = Role == TEXT("Host") ? 0 : Role == TEXT("Client1") ? 1 : Role == TEXT("Client2") ? 2 : INDEX_NONE;
        if (RoleIndex == INDEX_NONE) { return Fail(TEXT("#1"), TEXT("The Vision fixture role is unknown")); }
        if (!bGoalKeepouts)
        {
            // Bodies block each other and a route is planned once: a role that arrives first must not stand on another's way.
            bGoalKeepouts = true;
            const double Radius = 2 * Body->GetCapsuleComponent()->GetScaledCapsuleRadius() + LDPathSteering::WaypointToleranceCm;
            for (int32 Other = 0; Other < ArrivalGoals.Num(); ++Other)
            {
                if (Other != RoleIndex) { WalkKeepouts.Add({ArrivalGoals[Other], Radius}); }
            }
        }
        const auto Result = PlanWalk(Registry, Body->GetNavAgentLocation(), ArrivalGoals[RoleIndex], WalkWaypoints, Why);
        if (Result == LDScenarioWalk::EPlan::Failed)
        { return Fail(TEXT("#1"), TEXT("The owner's bounded Vision fixture route is unavailable: ") + Why); }
        if (Result == LDScenarioWalk::EPlan::Working) { return true; }
        FixtureRoute = 1;
        Check(true, TEXT("#1"), TEXT("This owner's route and every detour pass native legal-ground and complete-path validation"));
        OwnBody = Owner.OwnerState.BodyRef;
        WalkStart = Body->GetNavAgentLocation();
        WalkWaypoint = 0;
        Owner.Navigation->Navigate(WalkWaypoints[WalkWaypoint]);
        StageAt = Now;
        Stage = 3;
        return true;
    }

    bool TickArrive(double Now, ALDPlayerController& Owner)
    {
        const ALDCharacter* Body = Cast<ALDCharacter>(Owner.GetPawn());
        if (!Body || Owner.OwnerState.BodyRef != OwnBody)
        { return Fail(TEXT("#1"), TEXT("The owned body changed identity before observation was established")); }
        if (Owner.Navigation->HasMoveOrder()) { return true; }
        const double Acceptance = WalkWaypoint == WalkWaypoints.Num() - 1 ? ArrivalCm
            : Body->GetCapsuleComponent()->GetScaledCapsuleRadius() + LDPathSteering::WaypointToleranceCm;
        if (FVector::Dist2D(Body->GetNavAgentLocation(), WalkWaypoints[WalkWaypoint]) <= Acceptance)
        {
            if (++WalkWaypoint < WalkWaypoints.Num())
            {
                Owner.Navigation->Navigate(WalkWaypoints[WalkWaypoint]);
                StageAt = Now;
                return true;
            }
            if (!Mark(Role + TEXT("Arrived.json"))) { return Finish(); }
            Stage = 4;
            return true;
        }
        if (Now - FMath::Max(StageAt, WalkOrderedAt) < 1) { return true; }
        if (Now - StageAt < WalkWaypointSeconds)
        {
            // Two roles from one dorm share their first waypoints: the order of the one behind ends short of a point the
            // other still stands on. The same waypoint is ordered again until its bounded time is up.
            WalkOrderedAt = Now;
            ++WalkReorders;
            Owner.Navigation->Navigate(WalkWaypoints[WalkWaypoint]);
            return true;
        }
        return Fail(TEXT("#1"), FString::Printf(TEXT("The bounded Vision fixture route stopped before waypoint %d/%d after %d repeated order(s): "),
            WalkWaypoint + 1, WalkWaypoints.Num(), WalkReorders) + Owner.IntentFeedback);
    }

    bool TickSight(double Now, UWorld& InWorld, ALDPlayerController& Owner)
    {
        if (!AllMarkers(TEXT("Arrived.json"))) { return true; }
        if (SightDeadline == 0) { SightDeadline = Now + 20; }
        const bool bReactionOnly = HeadlessOnly();
        const ALDWorldViewRelay* Relay = nullptr;
        const FLDWorldView* Frame = CurrentFrame(Owner, Relay);
        if (!Frame || !Relay || (!bReactionOnly && OtherContenders(*Frame, Owner.OwnerState.ContenderId) < 2))
        {
            if (Now < SightDeadline) { return true; }
            return Fail(TEXT("#1"), Frame
                ? TEXT("At the shared capture anchor the current frame still lacks the other two contenders")
                : TEXT("No current authorized frame reached this owner (observation wiring pending)"));
        }
        if (Role == TEXT("Host") && !bReactionOnly)
        {
            const ALDGameMode* Mode = InWorld.GetAuthGameMode<ALDGameMode>();
            if (!Mode) { return Fail(TEXT("#15H"), TEXT("The Listen Host has no authoritative game mode")); }
            CheckHostPresentation(InWorld, *Mode, Owner, *Relay, *Frame);
            if (!Passed) { return Finish(); }
        }
        else if (Role != TEXT("Host")) { CheckRestrictedRegistry(InWorld, Owner, *Relay, *Frame); }
        FNetBitWriter Writer(nullptr,1<<18);
        FLDWorldView WireFrame=*Frame;
        bool bEncoded=true;
        WireFrame.NetSerialize(Writer,nullptr,bEncoded);
        Check(bEncoded && !Writer.IsError(),TEXT("#23"),TEXT("The received authorized frame re-encodes without error"));
        Check(Writer.GetNumBytes()<1100,TEXT("#23"),FString::Printf(
            TEXT("The shared-anchor observation payload fits below 1100 bytes (%lld bytes)"),Writer.GetNumBytes()));
        if (!Passed) { return Finish(); }
        if (!bReactionOnly && !FParse::Param(FCommandLine::Get(),TEXT("LDDemoVisionNoScreenshots")) && (Role==TEXT("Host") || FParse::Param(FCommandLine::Get(),TEXT("LDDemoVisionRendered"))))
        {
            PendingImage=FPaths::Combine(Output,Role+TEXT("Sighted.png"));
            ImageAt=Now;
            FScreenshotRequest::RequestScreenshot(PendingImage,true,false);
            Stage=5;
            return true;
        }
        if (!Passed || !Mark(Role + TEXT("Sighted.json"))) { return Finish(); }
        Stage = 6;
        return true;
    }

    bool TickImage(double Now)
    {
        if (IFileManager::Get().FileSize(*PendingImage) <= 0)
        {
            if (Now - ImageAt < 10) { return true; }
            return Fail(TEXT("#1"), TEXT("The rendered participant left no screenshot of its main view"));
        }
        Images.Add(MakeShared<FJsonValueString>(FPaths::GetCleanFilename(PendingImage)));
        Check(true, TEXT("#1"), TEXT("The participant renders its main view (screenshot kept for presentation review)"));
        if (!Mark(Role+TEXT("Sighted.json"))) { return Finish(); }
        Stage = 6;
        return true;
    }

    bool TickReaction(double Now, UWorld& InWorld, ALDPlayerController& Owner, const FLDLobbyView& Lobby)
    {
        if (!AllMarkers(TEXT("Sighted.json"))) { return true; }
        if (!bReactionReady)
        {
            const ALDWorldViewRelay* Relay = nullptr;
            const FLDWorldView* Frame = CurrentFrame(Owner, Relay);
            if (!Frame || !Relay) { return true; }
            if (Role != TEXT("Host") && OtherContenders(*Frame, Owner.OwnerState.ContenderId) >= 2
                && !Mark(Role + TEXT("ReactionSight.json"))) { return Finish(); }
            ReactionBaseline = Owner.ReactionAssistSerial;
            HitReceiptBaseline = Relay->ImmediateHitCount();
            if (!Owner.OwnerState.ContenderId.IsValid())
            { return Fail(TEXT("#Reaction"), TEXT("The owning connection has no contender identity")); }
            auto Ready = MakeShared<FJsonObject>();
            Ready->SetStringField(TEXT("contender"), Owner.OwnerState.ContenderId.ToString());
            if (!Save(Ready, Role + TEXT("ReactionReady.json")))
            { return Fail(TEXT("#Reaction"), TEXT("Could not persist the owning connection's contender identity")); }
            bReactionReady = true;
        }
        if (Role == TEXT("Host") && !bReactionQueued)
        {
            if (!AllMarkers(TEXT("ReactionReady.json"))) { return true; }
            const FString Chosen = Exists(TEXT("Client1ReactionSight.json")) ? TEXT("Client1")
                : Exists(TEXT("Client2ReactionSight.json")) ? TEXT("Client2") : FString();
            if (Chosen.IsEmpty())
            { return Fail(TEXT("#Reaction"), TEXT("Neither remote setter currently observes both the target and the trigger")); }
            if (!Exists(Chosen + TEXT("ReactionChosen.json")) && !Mark(Chosen + TEXT("ReactionChosen.json"))) { return Finish(); }
        }
        const FString SetterRole = Exists(TEXT("Client1ReactionChosen.json")) ? TEXT("Client1")
            : Exists(TEXT("Client2ReactionChosen.json")) ? TEXT("Client2") : FString();
        if (SetterRole.IsEmpty()) { return true; }
        FGuid HostId, SetterId, TriggerId;
        const FString TriggerRole = SetterRole == TEXT("Client1") ? TEXT("Client2") : TEXT("Client1");
        if (!ReadReactionContender(TEXT("Host"), HostId) || !ReadReactionContender(SetterRole, SetterId)
            || !ReadReactionContender(TriggerRole, TriggerId)
            || HostId == SetterId || HostId == TriggerId || SetterId == TriggerId
            || !Lobby.Members.ContainsByPredicate([SetterId](const FLDLobbyMember& Member) { return Member.ContenderId == SetterId; })
            || !Lobby.Members.ContainsByPredicate([TriggerId](const FLDLobbyMember& Member) { return Member.ContenderId == TriggerId; })
            || (Role == TEXT("Host") && Owner.OwnerState.ContenderId != HostId)
            || (Role == SetterRole && Owner.OwnerState.ContenderId != SetterId)
            || (Role == TriggerRole && Owner.OwnerState.ContenderId != TriggerId))
        { return Fail(TEXT("#Reaction"), TEXT("The owning connections do not identify three distinct roster participants")); }
        if (Role == TEXT("Host") && !bReactionQueued)
        {
            ALDGameMode* Mode = InWorld.GetAuthGameMode<ALDGameMode>();
            ALDPlayerController* SetterController = Mode ? Mode->FindContenderController(SetterId) : nullptr;
            ALDPlayerController* TriggerController = Mode ? Mode->FindContenderController(TriggerId) : nullptr;
            if (!Mode || !Mode->Combat || !SetterController || !TriggerController
                || !SetterController->OwnerState.BodyRef.IsValid() || !TriggerController->OwnerState.BodyRef.IsValid()
                || !Owner.OwnerState.BodyRef.IsValid())
            { return Fail(TEXT("#Reaction"), TEXT("The authority has no live bodies for the three test participants")); }
            if (Mode->IsContenderProtected(Owner.OwnerState.ContenderId)) { return true; }
            RemoteContenders.Add(SetterId);
            RemoteContenders.Add(TriggerId);
            FLDDemoVisionReactionAccess::Queue(*Mode->Combat, SetterController->OwnerState.BodyRef,
                TriggerController->OwnerState.BodyRef, Owner.OwnerState.BodyRef);
            if (!Mark(TEXT("ReactionQueued.json"))) { return Finish(); }
            bReactionQueued = true;
        }
        if (!Exists(TEXT("ReactionQueued.json"))) { return true; }
        const bool bRecipient = Role == SetterRole;
        if (ReactionQueuedAt == 0) { ReactionQueuedAt = Now; }
        if (bRecipient && !Exists(TEXT("ReactionReceived.json")) && Owner.ReactionAssistSerial > ReactionBaseline)
        {
            if (!Mark(TEXT("ReactionReceived.json"))) { return Finish(); }
        }
        if (!Exists(TEXT("ReactionReceived.json")))
        {
            if (Now - ReactionQueuedAt < 10) { return true; }
            return Fail(TEXT("#Reaction"), TEXT("The wet setter did not receive a reaction prompt"));
        }
        if (ReactionAt == 0) { ReactionAt = Now; }
        if (Now - ReactionAt < 2) { return true; }
        const FLDReactionAssistView* Received = Owner.ReactionAssists.IsEmpty() ? nullptr : &Owner.ReactionAssists.Last();
        const bool bCorrect = bRecipient
            ? Owner.ReactionAssistSerial == ReactionBaseline + 1 && Received
                && Received->Match == Owner.OwnerState.Match && Received->Life == Owner.OwnerState.Life
                && Received->Trigger == TriggerId && Received->Consumed == uint8(ELDCombatElement::Water)
                && Received->Reaction == uint8(ELDCombatReaction::Conduct)
            : Owner.ReactionAssistSerial == ReactionBaseline;
        if (!Check(bCorrect, TEXT("#Reaction"), bRecipient
            ? TEXT("The wet setter alone receives the named conduct prompt over its owning connection")
            : TEXT("A non-setter receives no reaction prompt"))) { return Finish(); }
        const ALDWorldViewRelay* Relay = nullptr;
        const FLDWorldView* Frame = CurrentFrame(Owner, Relay);
        const bool bHitReceiptCorrect = Frame && Relay
            && Relay->ImmediateHitCount() == HitReceiptBaseline + (bRecipient ? 1u : 0u);
        if (!Check(bHitReceiptCorrect, TEXT("#HitReceipt"), bRecipient
            ? TEXT("The visible hit reaches only its owning connection immediately")
            : TEXT("A different owner receives no immediate hit receipt"))) { return Finish(); }
        if (!Mark(Role + TEXT("ReactionDone.json"))) { return Finish(); }
        Stage = 7;
        return true;
    }

    static ULDCharacterMovementComponent* MovementOf(const APawn* Pawn)
    {
        const ALDCharacter* Body = Cast<ALDCharacter>(Pawn);
        return Body ? Cast<ULDCharacterMovementComponent>(Body->GetCharacterMovement()) : nullptr;
    }

    // Spec §8.3 ice inertia over a real owning connection. Client1 is the walker; the Host places an ice field its own body
    // owns at Client1's foot, then watches the authoritative acceleration of Client1's server body and its corrections;
    // Client1 checks its owner view and prediction scale, then walks across the ice and times its start.
    bool TickIce(double Now, UWorld& InWorld, ALDPlayerController& Owner, const FLDLobbyView& Lobby)
    {
        if (!AllMarkers(TEXT("Sighted.json"))) { return true; }
        TSharedPtr<const FLDDemoContentSnapshot> Content;
        FString ContentError;
        if (!LDDemoEntry::LoadRuntimeContent(Content, ContentError) || !Content)
        { return Fail(TEXT("#Ice"), TEXT("No approved content: ") + ContentError); }
        const float Scale = float(Content->Get().Parameters.Ground.IceAccelScaleBp) / 10000.f;
        if (!(Scale > 0.f && Scale < 1.f)) { return Fail(TEXT("#Ice"), TEXT("GROUND.IceAccelScale is not a slowdown")); }
        if (!bIceReady)
        {
            if (!Owner.OwnerState.ContenderId.IsValid()) { return Fail(TEXT("#Ice"), TEXT("The owning connection has no contender identity")); }
            auto Ready = MakeShared<FJsonObject>();
            Ready->SetStringField(TEXT("contender"), Owner.OwnerState.ContenderId.ToString());
            if (!Save(Ready, Role + TEXT("IceReady.json"))) { return Fail(TEXT("#Ice"), TEXT("Could not persist the contender identity")); }
            bIceReady = true;
            if (IceSeenAt == 0) { IceSeenAt = Now; }
        }
        // Client2 only stands by; it names itself so the Host can check its leave later (#9).
        if (Role == TEXT("Client2"))
        {
            if (!Mark(Role + TEXT("IceDone.json"))) { return Finish(); }
            Stage = 7;
            return true;
        }
        if (Role == TEXT("Host"))
        {
            FGuid WalkerId, BystanderId;
            if (!AllMarkers(TEXT("IceReady.json"))) { return true; }
            if (!ReadReactionContender(TEXT("Client1"), WalkerId, TEXT("IceReady.json"))
                || !ReadReactionContender(TEXT("Client2"), BystanderId, TEXT("IceReady.json"))
                || !Lobby.Members.ContainsByPredicate([WalkerId](const FLDLobbyMember& Member) { return Member.ContenderId == WalkerId; }))
            { return Fail(TEXT("#Ice"), TEXT("Client1 is not a roster participant")); }
            if (RemoteContenders.IsEmpty()) { RemoteContenders.Add(WalkerId); RemoteContenders.Add(BystanderId); }
            ALDGameMode* Mode = InWorld.GetAuthGameMode<ALDGameMode>();
            ALDPlayerController* Walker = Mode ? Mode->FindContenderController(WalkerId) : nullptr;
            ULDCharacterMovementComponent* ServerMove = Walker ? MovementOf(Walker->GetPawn()) : nullptr;
            const ALDCharacter* WalkerBody = Walker ? Cast<ALDCharacter>(Walker->GetPawn()) : nullptr;
            if (!Mode || !Mode->Combat || !ServerMove || !WalkerBody || !Owner.OwnerState.BodyRef.IsValid())
            { return Fail(TEXT("#Ice"), TEXT("The authority has no live bodies for the Host and the walker")); }
            if (!bIceQueued)
            {
                if (Mode->IsContenderProtected(WalkerId)) { return true; }
                FString SpawnError;
                if (!FLDDemoVisionIceAccess::Spawn(*Mode->Combat, Owner.OwnerState.BodyRef, Owner.OwnerState.ContenderId,
                    ALDCombatDirector::FootOf(*WalkerBody), Mode->GetAuthorityNowMs(), SpawnError))
                { return Fail(TEXT("#Ice"), TEXT("The native ground path refused the ice field: ") + SpawnError); }
                IceCorrectionBaseline = ServerMove->ServerCorrectionCount;
                if (!Mark(TEXT("IceQueued.json"))) { return Finish(); }
                bIceQueued = true;
                IceWalkAt = Now;
            }
            // The server body of the walker accelerates and brakes at the ice scale while it stands on it.
            const float ServerAccel = ServerMove->GetMaxAcceleration();
            bIceServerScaled |= FMath::IsNearlyEqual(ServerMove->AccelScale(), Scale, .001f)
                && FMath::IsNearlyEqual(ServerAccel, ServerMove->MaxAcceleration * Scale, 1.f);
            if (!Exists(TEXT("IceWalked.json")))
            {
                if (Now - IceWalkAt < 30) { return true; }
                return Fail(TEXT("#Ice"), TEXT("The walker did not finish its walk on the ice"));
            }
            const uint32 Corrections = ServerMove->ServerCorrectionCount - IceCorrectionBaseline;
            if (!Check(bIceServerScaled, TEXT("#Ice"), FString::Printf(
                TEXT("The walker's server body accelerates at the authoritative ice scale %.2f on the Host's ice"), Scale))) { return Finish(); }
            if (!Check(Corrections <= IceCorrectionAllowance, TEXT("#Ice"), FString::Printf(
                TEXT("The walk across the ice stays predicted: %u server corrections (allowance %u)"), Corrections, IceCorrectionAllowance)))
            { return Finish(); }
            if (!Mark(Role + TEXT("IceDone.json"))) { return Finish(); }
            Stage = 7;
            return true;
        }
        // Client1: the owning client's prediction reads the same scale from its owner view, then walks across the ice.
        if (!Exists(TEXT("IceQueued.json"))) { return true; }
        ULDCharacterMovementComponent* Move = MovementOf(Owner.GetPawn());
        if (!Move || !Owner.Navigation) { return Fail(TEXT("#Ice"), TEXT("The walker has no predicted movement")); }
        if (!bIceWalking)
        {
            if (!FMath::IsNearlyEqual(Move->AccelScale(), Scale, .001f))
            {
                if (Now - IceSeenAt < 15) { return true; }
                return Fail(TEXT("#Ice"), FString::Printf(TEXT("The owner view never published the ice scale (%.3f, expected %.3f)"),
                    Move->AccelScale(), Scale));
            }
            if (!Check(FMath::IsNearlyEqual(Move->GetMaxAcceleration(), Move->MaxAcceleration * Scale, 1.f), TEXT("#Ice"),
                TEXT("The owning client's prediction accelerates at the same authoritative ice scale"))) { return Finish(); }
            const FVector Foot = Owner.GetPawn()->GetActorLocation();
            FVector Forward = Owner.GetPawn()->GetActorForwardVector().GetSafeNormal2D();
            if (Forward.IsNearlyZero()) { Forward = FVector::ForwardVector; }
            IceGoal = Foot + Forward * IceWalkCm;
            Owner.Navigation->Navigate(IceGoal);
            bIceWalking = true;
            IceWalkAt = Now;
            return true;
        }
        const double Speed = Owner.GetPawn()->GetVelocity().Size2D();
        IceTopSpeed = FMath::Max(IceTopSpeed, Speed);
        if (IceTopSpeedAt == 0 && Speed >= Move->GetMaxSpeed() * .9) { IceTopSpeedAt = Now; }
        if (FVector::Dist2D(Owner.GetPawn()->GetActorLocation(), IceGoal) > 60.)
        {
            if (Now - IceWalkAt < 10) { return true; }
            return Fail(TEXT("#Ice"), TEXT("The walker did not arrive across the ice: ") + Owner.IntentFeedback);
        }
        // Full speed at the ice scale takes MaxSpeed / (MaxAcceleration * scale); off ice it is effectively instant.
        const double Expected = Move->GetMaxSpeed() / FMath::Max(Move->MaxAcceleration * Scale, 1.f) * .9;
        const double Took = IceTopSpeedAt > 0 ? IceTopSpeedAt - IceWalkAt : -1;
        auto Walked = MakeShared<FJsonObject>();
        Walked->SetNumberField(TEXT("scale"), Scale);
        Walked->SetNumberField(TEXT("seconds_to_top_speed"), Took);
        Walked->SetNumberField(TEXT("expected_seconds"), Expected);
        Walked->SetNumberField(TEXT("top_speed"), IceTopSpeed);
        // The ramp is reported, not gated: at the probe's 0.1 s sampling it only bounds the scaled start from below.
        if (!Save(Walked, TEXT("IceWalked.json"))) { return Fail(TEXT("#Ice"), TEXT("Could not persist the walk")); }
        if (!Check(true, TEXT("#Ice"), FString::Printf(TEXT("The walker crosses the ice (top speed after %.2f s; scaled ramp about %.2f s)"),
            Took, Expected))) { return Finish(); }
        if (!Mark(Role + TEXT("IceDone.json"))) { return Finish(); }
        Stage = 7;
        return true;
    }
    static constexpr double IceWalkCm = 300.;
    static constexpr uint32 IceCorrectionAllowance = 2;

    // Client1 loses every incoming packet for one bounded gap; the Host and Client2 watch their own views meanwhile.
    bool TickStall(double Now, UWorld& InWorld, ALDPlayerController& Owner)
    {
        if (Role == TEXT("Client1")) { return TickFrameLoss(Now, InWorld, Owner); }
        if (Role == TEXT("Host") && !bStallAnnounced)
        {
            if (!AllMarkers(TEXT("Sighted.json"))) { return true; }
            if (!Mark(TEXT("StallBegin.json"))) { return Finish(); }
            bStallAnnounced = true;
        }
        if (Exists(TEXT("Client1Stalling.json")) && !Exists(TEXT("Client1Recovered.json")))
        {
            const ALDWorldViewRelay* Relay = nullptr;
            const FLDWorldView* Frame=CurrentFrame(Owner,Relay);
            if (Frame && Relay && Relay->ProxyCount()>0) { bControlObserved=true; }
            else if (FParse::Param(FCommandLine::Get(),TEXT("LDDemoVisionNetEmulated")) && Relay)
            {
                // Ambient lag/loss can legitimately expire the control connection too. It must fail closed,
                // not retain units, and must not inherit Client1's injected total-loss setting.
                const FLDObservedFrameRead Read=Relay->ReadObservedFrame();
                if (Read.Status!=ELDObservedViewStatus::Stale || Relay->ProxyCount()!=0
                    || !Read.Frame || !Read.Frame->Units.IsEmpty() || !Read.Frame->Footprint.CurrentBits.IsEmpty())
                { bControlViolated=true; }
#if DO_ENABLE_NET_TEST
                const UNetDriver* Driver=InWorld.GetNetDriver();
                if (!Driver || Driver->PacketSimulationSettings.PktIncomingLoss==100
                    || (Driver->ServerConnection && Driver->ServerConnection->PacketSimulationSettings.PktIncomingLoss==100))
                { bControlViolated=true; }
#endif
            }
            else { bControlViolated=true; }
        }
        if (!Exists(TEXT("Client1Recovered.json"))) { return true; }
        Check(bControlObserved && !bControlViolated, TEXT("#23"),
            TEXT("Another owner receives its own view during the isolated loss; any ambient timeout clears rather than retains units"));
        if (!Passed || !Mark(Role + TEXT("Control.json"))) { return Finish(); }
        Stage = 7;
        return true;
    }

    bool TickFrameLoss(double Now, UWorld& InWorld, ALDPlayerController& Owner)
    {
        const ALDWorldViewRelay* Relay = nullptr;
        if (Stall == EStall::Waiting)
        {
            if (!Exists(TEXT("StallBegin.json"))) { return true; }
            const FLDWorldView* Frame = CurrentFrame(Owner, Relay);
            if (!Frame || !Relay || Frame->Units.IsEmpty())
            { return Fail(TEXT("#23"), TEXT("No presented unit existed when the frame loss began")); }
            for (const FLDObservedUnitView& Unit : Frame->Units)
            {
                FLDSeenUnit Seen;
                Seen.Handle = Unit.Handle;
                Seen.Epoch = Unit.ObservationEpoch;
                PreStall.Add(Seen);
            }
            PreStallProxies = Relay->ProxyCount();
            if (!BeginFrameLoss(InWorld) || !Mark(TEXT("Client1Stalling.json"))) { return Finish(); }
            StageAt = Now;
            Stall = EStall::Lossy;
            return true;
        }
        if (Stall == EStall::Lossy)
        {
            Relay = Owner.LocalView();
            const FLDObservedFrameRead Read = Relay ? Relay->ReadObservedFrame() : FLDObservedFrameRead();
            if (Read.Status != ELDObservedViewStatus::Stale)
            {
                if (Now - StageAt < 2) { return true; }
                return Fail(TEXT("#23"), TEXT("Two seconds without any received frame did not time the view out"));
            }
            CheckTimeoutClear(InWorld, *Relay, Read);
            const ALDCharacter* Body=Cast<ALDCharacter>(Owner.GetPawn());
            const FTransform Expected=Body ? ALDCharacter::DefaultArmCameraPose(
                Body->CameraArm->GetComponentLocation(),Body->CameraArm->TargetArmLength) : FTransform::Identity;
            Check(Body && Owner.GetViewTarget()==Body && Owner.PlayerCameraManager
                && Owner.PlayerCameraManager->GetCameraLocation().Equals(Expected.GetLocation(),10.),
                TEXT("#23"),TEXT("Loss of observation packets never moves the owning camera to a controller or foreign view"));
            uint64 LocalSerial=0;
            const auto* Ground=Relay->ReadGroundFog(LocalSerial);
            if (!Check(Body && Ground && Ground->ContainsByPredicate([](uint8 Byte){return Byte!=0;}),
                TEXT("#23"),TEXT("The local terrain stays visible while all server observation is stale"))) { return Finish(); }
            StalledGround = *Ground;
            BeforeGroundMove = Body->GetNavAgentLocation();
            // Walk back toward a waypoint already checked by the fixture. The owning client predicts its normal
            // ground navigation while every response from the server is being dropped.
            const FVector* WalkBack = nullptr;
            for (int32 Index = WalkWaypoints.Num()-2; Index >= 0; --Index)
            {
                if (FVector::Dist2D(BeforeGroundMove, WalkWaypoints[Index]) > 240.)
                { WalkBack = &WalkWaypoints[Index]; break; }
            }
            if (!WalkBack && FVector::Dist2D(BeforeGroundMove, WalkStart) > 240.) { WalkBack = &WalkStart; }
            if (!WalkBack) { return Fail(TEXT("#23"),TEXT("The fixture route lacks a return waypoint for the loss check")); }
            Owner.Navigation->Navigate(*WalkBack);
            StageAt = Now;
            Stall = EStall::MovingWithoutFrames;
            return true;
        }
        if (Stall == EStall::MovingWithoutFrames)
        {
            Relay = Owner.LocalView();
            uint64 LocalSerial=0;
            const auto* Ground=Relay ? Relay->ReadGroundFog(LocalSerial) : nullptr;
            const auto* Body=Cast<ALDCharacter>(Owner.GetPawn());
            const double Distance=Body ? FVector::Dist2D(BeforeGroundMove,Body->GetNavAgentLocation()) : 0.;
            if (!Relay || Relay->ReadObservedFrame().Status != ELDObservedViewStatus::Stale || Relay->ProxyCount() != 0)
            { return Fail(TEXT("#23"), TEXT("Local movement during packet loss granted an observed unit")); }
            if (Distance < 120. || !Ground || *Ground == StalledGround
                || !Ground->ContainsByPredicate([](uint8 Byte){return Byte!=0;}))
            {
                if (Now-StageAt < 3.) { return true; }
                return Fail(TEXT("#23"),FString::Printf(
                    TEXT("The local terrain mask did not follow the body while packets were blocked (body moved %.1f cm)"), Distance));
            }
            Check(true,TEXT("#23"),FString::Printf(
                TEXT("Terrain coverage follows %.1f cm of predicted local movement with every incoming packet still dropped"),Distance));
            Check(true,TEXT("#23"),TEXT("Local movement during packet loss grants no observed unit"));
            EndFrameLoss();
            if (!Passed) { return Finish(); }
            StageAt = Now;
            Stall = EStall::Recovering;
            return true;
        }
        // Recovery itself is Lockdown.Rules.DemoVision.Ordering (#24); here it only gates the remaining steps.
        if (!CurrentFrame(Owner, Relay))
        {
            if (Now - StageAt < 5) { return true; }
            return Fail(TEXT("#23"), TEXT("The view did not become current again after frames resumed"));
        }
        if (!Mark(TEXT("Client1Recovered.json"))) { return Finish(); }
        Stall = EStall::Done;
        Stage = 7;
        return true;
    }

    bool TickReport()
    {
        for (const TCHAR* Marker : {TEXT("HostControl.json"), TEXT("Client1Recovered.json"), TEXT("Client2Control.json")})
        {
            if (!Exists(Marker)) { return true; }
        }
        return Finish();
    }

    bool Finish()
    {
        if (Written) { return true; }
        EndFrameLoss();
        StopWatching();
        auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), Passed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetBoolField(TEXT("dev_probe"), true);
        Report->SetBoolField(TEXT("normal_start"), true);
        Report->SetBoolField(TEXT("peaceful_fixture"),FParse::Param(FCommandLine::Get(),TEXT("LDDemoVisionPeaceful")));
        Report->SetBoolField(TEXT("screenshots_requested"),!FParse::Param(FCommandLine::Get(),TEXT("LDDemoVisionNoScreenshots")));
        Report->SetStringField(TEXT("scope"), IceOnly()
            ? TEXT("DEV-only owned-connection ice scenario: three normal-start participants; the Host's body owns an ice field placed through the native ground path under Client1, which walks across it over its real owning connection. Checks the authoritative acceleration scale on the server, the same scale in the owning client's prediction, the slower start and the server corrections of that walk; not balance, other ground states or full-match acceptance.")
            : ReactionOnly()
            ? TEXT("DEV-only owned-connection reaction scenario: three normal-start participants, with fixture hits queued into the native Effects phase. Checks the setter's reaction and visible-hit RPCs and non-recipient isolation; not player input, hidden sight cases, combat balance or full-match acceptance.")
            : TEXT("DEV-only owned-connection observation scenario: a rendered, participating Listen Host and two owning clients started through the normal lobby. Carries checks #1, #9, #15, #23 and #28 of the retired view network probe plus Listen Host presentation (#15H). Not combat balance, death or termination, AI or full-match acceptance."));
        Report->SetStringField(TEXT("catalog_sha256"), CatalogDigest);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetNumberField(TEXT("stage"), Stage);
        Report->SetNumberField(TEXT("validated_routes"), FixtureRoute);
        Report->SetNumberField(TEXT("max_route_seconds"), MaxRouteSeconds);
        Report->SetNumberField(TEXT("max_route_tick_seconds"), MaxRouteTickSeconds);
        Report->SetNumberField(TEXT("max_native_query_seconds"), MaxQuerySeconds);
        Report->SetNumberField(TEXT("native_fixture_queries"), NativeQueries);
        Report->SetArrayField(TEXT("checks"), Checks);
        Report->SetArrayField(TEXT("screenshots"), Images);
        if (const ALDPlayerController* Owner = Controller.Get())
        {
            Report->SetNumberField(TEXT("generation"), Owner->OwnerState.Match);
            Report->SetNumberField(TEXT("life"), Owner->OwnerState.Life);
        }
        Written = Save(Report, Role + TEXT(".json"));
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_VISION_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
        if (!Passed || !Written) { FPlatformMisc::RequestExitWithStatus(false, 2); return false; }
        return true;
    }

    // After the reports: owning clients leave on the runner's signal; the Host then checks #9 and waits to be released.
    bool TickAfterReport(double Now)
    {
        if (Role != TEXT("Host"))
        {
            if (Exists(TEXT("FinishClients")))
            {
                // Every mode leaves through the formal room exit: ending the process is an unexpected disconnect, whose
                // contender the authority keeps for reconnection (and so still presents).
                if (!bReactionLeaveRequested)
                {
                    ALDPlayerController* Owner = Controller.Get();
                    if (!Owner)
                    {
                        UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_VISION_LEAVE_FAILED no owning controller"));
                        FPlatformMisc::RequestExitWithStatus(false, 2);
                        return false;
                    }
                    Owner->LeaveDemoRoom();
                    bReactionLeaveRequested = true;
                    DisconnectAt = Now;
                }
                const UWorld* LeavingWorld = World.Get();
                if (!Controller.IsValid() || !LeavingWorld || !LeavingWorld->GetNetDriver())
                { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
                if (Now - DisconnectAt >= 20)
                {
                    UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_VISION_LEAVE_FAILED owning client did not leave the room"));
                    FPlatformMisc::RequestExitWithStatus(false, 2);
                    return false;
                }
            }
            return true;
        }
        UWorld* Hosted = World.Get();
        const UNetDriver* Driver = Hosted ? Hosted->GetNetDriver() : nullptr;
        if (!DisconnectVerified && Hosted && Driver && Driver->ClientConnections.IsEmpty() && Exists(TEXT("FinishClients")))
        {
            if (DisconnectAt == 0) { DisconnectAt = Now; }
            const ALDPlayerController* Owner = Controller.Get();
            const ALDWorldViewRelay* Relay = nullptr;
            const FLDWorldView* Frame = Owner ? CurrentFrame(*Owner, Relay) : nullptr;
            FString Why;
            bool bClean = Frame && Relay && RemoteContenders.Num() == 2;
            if (!bClean) { Why = TEXT("the Host has no current frame or never resolved both remote contenders"); }
            else if (Frame->Units.ContainsByPredicate([this](const FLDObservedUnitView& Unit)
                { return Unit.Kind == ELDObservedUnitKind::Contender && RemoteContenders.Contains(Unit.ContenderId); }))
            {
                bClean = false;
                Why = TEXT("a departed contender is still presented");
            }
            else { bClean = ProxiesMatchFrame(*Hosted, *Relay, *Frame, Why); }
            if (!bClean && Now - DisconnectAt < 3) { return true; }
            auto Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("id"), TEXT("#9"));
            Result->SetBoolField(TEXT("passed"), bClean);
            Result->SetNumberField(TEXT("departed_contenders"), RemoteContenders.Num());
            Result->SetNumberField(TEXT("remaining_units"), Frame ? Frame->Units.Num() : -1);
            Result->SetNumberField(TEXT("remaining_proxies"), CountProxies(*Hosted));
            Result->SetStringField(TEXT("detail"), Why);
            Result->SetStringField(TEXT("scope"), HeadlessOnly()
                ? TEXT("After both owning clients explicitly leave, the Listen Host's own current view and proxies no longer present them.")
                : TEXT("After both owning clients disconnect, the Listen Host's own current view and proxies no longer present them."));
            if (!Save(Result, TEXT("Disconnects.json")) || !bClean)
            {
                UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_VISION_DISCONNECT_FAILED %s"), *Why);
                FPlatformMisc::RequestExitWithStatus(false, 2);
                return false;
            }
            DisconnectVerified = true;
        }
        if (Exists(TEXT("Finish"))) { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
        return true;
    }

    bool Tick()
    {
        const double Now = FPlatformTime::Seconds();
        if (Written) { return TickAfterReport(Now); }
        if (Now >= Deadline)
        { return Fail(TEXT("#28"), FString::Printf(TEXT("The scenario did not finish within its bounded deadline (stage %d)"), Stage)); }
        ALDPlayerController* Owner = Controller.Get();
        UWorld* Current = World.Get();
        if (!Owner || !Current) { return Fail(TEXT("#9"), TEXT("The owning controller or its world ended before the scenario finished")); }
        const ALDGameState* Public = Current->GetGameState<ALDGameState>();
        const ULDMapRegistry* Registry = Current->GetSubsystem<ULDMapRegistry>();
        if (!Public || !Registry || !Registry->Enabled()) { return true; }
        if (CatalogDigest.IsEmpty())
        {
            FString Why;
            if (!LDDemoEntry::VerifyRuntimeFiles(Why, &CatalogDigest))
            { return Fail(TEXT("#1"), TEXT("The approved runtime files are unavailable: ") + Why); }
        }
        const FLDMapBinding* Goal = Registry->Resolve(TEXT("capture"), Registry->Generation);
        if (!Goal) { return Fail(TEXT("#1"), TEXT("The registered world has no public capture anchor")); }
        const UNetDriver* Driver = Current->GetNetDriver();
        if (Stage >= 1)
        {
            const bool bConnected = Role == TEXT("Host")
                ? Driver && Driver->ClientConnections.Num() == 2
                : Driver && Driver->ServerConnection != nullptr;
            if (!bConnected) { return Fail(TEXT("#9"), TEXT("An owning connection dropped before the scenario finished")); }
        }
        const FLDLobbyView& Lobby = Public->DemoLobby;
        switch (Stage)
        {
        case 0: return TickJoin(*Current, *Owner, Lobby, Driver);
        case 1: return TickLobby(Now, *Owner, Lobby);
        case 2: return TickBody(Now, *Owner, *Registry, *Goal);
        case 3: return TickArrive(Now, *Owner);
        case 4: return TickSight(Now, *Current, *Owner);
        case 5: return TickImage(Now);
        case 6: return IceOnly() ? TickIce(Now, *Current, *Owner, Lobby)
            : ReactionOnly() ? TickReaction(Now, *Current, *Owner, Lobby) : TickStall(Now, *Current, *Owner);
        case 7:
            if (IceOnly()) { return AllMarkers(TEXT("IceDone.json")) ? Finish() : true; }
            if (ReactionOnly())
            { return AllMarkers(TEXT("ReactionDone.json")) ? Finish() : true; }
            return TickReport();
        default: return TickReport();
        }
    }
};
}

void LDDemoTests::StartVisionProbe(ALDPlayerController& Controller)
{
    using LDDemoVisionScenario::FLDDemoVisionProbe;
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoVisionProbe")) || !Controller.IsLocalController()
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap) { return; }
    const TSharedRef<FLDDemoVisionProbe> Probe = MakeShared<FLDDemoVisionProbe>();
    int32 Timeout = 300;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoVisionTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoVisionOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoVisionRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1") && Probe->Role != TEXT("Client2"))
        || Timeout < 120 || Timeout > 600
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_VISION_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    Probe->Controller = &Controller;
    Probe->World = Controller.GetWorld();
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    if (Probe->Role == TEXT("Host")) { Probe->WatchSpawns(*Controller.GetWorld()); }
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe, Next = 0.0](float) mutable
    {
        const double Now = FPlatformTime::Seconds();
        if (Probe->Stage != 2 && Now < Next) { return true; }
        Next = Now + .1;
        return Probe->Tick();
    }));
}
#endif
