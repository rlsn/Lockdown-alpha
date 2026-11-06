#include "Tests/LDDemoVisionProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDControlRules.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Development/LDDevGrowthFixture.h"
#include "Development/LDGrowthProtocol.h"
#include "Economy/LDEconomyDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapGeometry.h"
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

// Carries the topology-only assertions of the retired LDViewNetworkProbe (#1, #9, #15, #23, #28 of the observation
// closure plan) plus the Listen Host presentation gate, then the paper box stealth segment (#ST, #STH): no frame,
// handle, proxy or Listen Host body presents a stealthed contender beyond its reveal radius, a lost lock is cancelled,
// rediscovery issues a fresh handle and each reveal trigger brings the body back. Codec, ordering, handles and sight
// stay in Lockdown.Rules.DemoVision.*; this scenario does not repeat them. Named, not anonymous: unity builds merge files.
namespace LDDemoVisionScenario
{
const TCHAR* const VisionRoles[] = {TEXT("Host"), TEXT("Client1"), TEXT("Client2")};
// All three bodies walk to the public capture anchor; a crowded arrival may stop short of its exact point.
constexpr double ArrivalCm = 150.0;

// Stealth segment (#ST, CONTENT_AND_BALANCE 15.4): the three bodies stand on an equilateral triangle of this side beside
// the shop, so every body is in sight of the other two and far outside the box's reveal radius.
constexpr double StealthGapCm = 600.0;
// A stealthed body walks up to this distance of its near observer, inside the reveal radius.
constexpr double StealthApproachCm = 150.0;
// A frame's feet are sampled on the authority while this process reads its own body now: one body radius of slack.
constexpr double StealthSlackCm = 40.0;
constexpr double StealthSiteCm = 80.0;
constexpr int32 StealthWalkAttempts = 4;
// Bounded waits in seconds: a walk, a shop step or a fixture grant; one trial after its stealth began; the second
// stealth of the same box waits out the approved cooldown.
constexpr double StealthStepSeconds = 20.0;
constexpr double StealthTrialSeconds = 8.0;
constexpr double StealthCooldownSeconds = 75.0;

inline FName StealthItem() { return FName(TEXT("item_paper_box")); }
inline FName StealthTaunt() { return FName(TEXT("active_come_here")); }

// How a stealthed body is made visible again before its expiry (15.4): its own accepted attack, its own accepted
// ability, a hit it takes or a control that lands on it.
enum class EStealthTrigger : uint8 { Attack, Cast, Hurt, Control };

struct FLDStealthTrial
{
    const TCHAR* Stealther;
    EStealthTrigger Trigger;
    // The observer the stealthed body walks up to (and who acts for Hurt/Control); null when it stays on its site.
    const TCHAR* Near;
    // Near holds an attack order on the body when it hides: lost sight must cancel the lock and the chase.
    bool bChase;
    const TCHAR* Name;
};

// Client1 and Client2 are restricted owning clients; the Host is the participating Listen Host. Every role is also an
// observer of each trial it does not hide in. The fourth trial reuses Client1's box after its cooldown.
const FLDStealthTrial StealthTrials[] = {
    {TEXT("Client1"), EStealthTrigger::Attack, TEXT("Client2"), true, TEXT("attack")},
    {TEXT("Client2"), EStealthTrigger::Cast, nullptr, false, TEXT("cast")},
    {TEXT("Host"), EStealthTrigger::Hurt, TEXT("Client1"), false, TEXT("hurt")},
    {TEXT("Client1"), EStealthTrigger::Control, TEXT("Client2"), false, TEXT("control")},
};

double VisionRational(const FLDDemoRational& Value)
{
    return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : -1.0;
}

const FLDObservedUnitView* ContenderUnit(const FLDWorldView& Frame, const FGuid& Contender)
{
    return Frame.Units.FindByPredicate([&Contender](const FLDObservedUnitView& Unit)
        { return Unit.Kind == ELDObservedUnitKind::Contender && Unit.ContenderId == Contender; });
}

bool HasStatus(const TArray<FLDObservedStatus>& Statuses, ELDDemoStatusId Id)
{
    return Statuses.ContainsByPredicate([Id](const FLDObservedStatus& Status) { return Status.StatusId == uint8(Id); });
}

// Per-trial runtime state of one role; reset before every trial.
struct FLDStealthRun
{
    // One accepted observation frame as this observer received it (the probe hooks every accepted frame).
    struct FFrameRow
    {
        int64 SampleMs = 0;
        bool bPresent = false, bPreHandle = false, bStealthStatus = false;
        double Distance = -1;
        FGuid Handle;
        uint32 Epoch = 0;
    };
    double At = 0, ActAt = 0;
    int32 WalkAttempts = 0, HomeSeen = -1, Cursor = 0;
    bool bHomeReady = false;
    FGuid PreHandle;
    uint32 PreEpoch = 0;
    FLDMoveIdentity Chase;
    bool bChaseSent = false, bChaseCancelled = false, bChaseResumed = false;
    bool bHidden = false, bConcealed = false, bFound = false, bActed = false, bStopped = false;
    bool bEarly = false, bLate = false, bTaunted = false, bHurt = false, bProxyChecked = false;
    int64 HiddenMs = 0, ExpiresMs = 0, RevealMs = 0, TargetHp = 0, OwnHp = 0;
    TArray<FFrameRow> Rows;
    FString Leaks;
    // The authority end of the stealth as the Host saw it at each rendered tick: the last authority time it was still
    // stealthed and the first at which it no longer was (same step granularity as the observation samples).
    bool bEnded = false;
    int64 LastStealthMs = 0, EndedMs = 0;
    // Host only: the authority watch and its native-body presentation audit at every game tick.
    bool bOracleSeen = false, bOracleEnded = false, bEndedMarked = false;
    int64 OracleLastMs = 0, OracleEndMs = 0;
    int32 HostTicks = 0;
    FString HostDrawn;
};

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
    enum class EStall : uint8 { Waiting, Lossy, Recovering, Done };

    TWeakObjectPtr<ALDPlayerController> Controller;
    TWeakObjectPtr<UWorld> World;
    FString Output, Role, Error, CatalogDigest, PendingImage, SpawnViolation;
    double Deadline = 0, StageAt = 0, NextCommandAt = 0, NextReadyAt = 0, BlockedSince = 0, SightDeadline = 0, ImageAt = 0, DisconnectAt = 0;
    int32 Stage = 0, NavigateAttempts = 0, SpawnFramesChecked = 0, PreStallProxies = 0;
    bool Passed = true, Written = false, bJoined = false, bLobbyConfigured = false, bStallAnnounced = false;
    bool bControlObserved = false, bControlViolated = false, bEmulating = false, DisconnectVerified = false;
    EStall Stall = EStall::Waiting;
    FLDCombatBodyRef OwnBody;
    TArray<FLDSeenUnit> PreStall;
    TSet<FGuid> RemoteContenders;
    TArray<TWeakObjectPtr<APawn>> SpawnedPawns;
    TArray<TSharedPtr<FJsonValue>> Checks, Images;
    FDelegateHandle SpawnHandle, PostTickHandle;
#if DO_ENABLE_NET_TEST
    FPacketSimulationSettings SavedEmulation;
#endif
    // ---- Stealth segment (#ST/#STH) ----
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    enum class EPhase : uint8 { Home, Pre, Run, Done };
    int32 StealthStep = 0, Trial = 0;
    EPhase Phase = EPhase::Home;
    double StealthAt = 0;
    bool bOffersStaged = false, bSitesReady = false, bIdMarked = false, bGranted = false, bShopSent = false, bBuySent = false;
    int32 ShopAttempts = 0;
    uint64 ChoiceRequest = 0;
    TArray<uint64> GrantTickets;
    FGuid Ids[3];
    FVector Sites[3];
    FName ShopId;
    double RevealCm = 0;
    int64 StealthDurationMs = 0;
    LDDemoVisionScenario::FLDStealthRun Run;
    TArray<TSharedPtr<FJsonValue>> StealthRows;
    FDelegateHandle TrialTickHandle;
    TWeakObjectPtr<ALDWorldViewRelay> HookedRelay;

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
            }
            Probe->SpawnedPawns.Reset();
        });
    }

    void StopWatching()
    {
        UnhookTrial();
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

    // #15H (WORLD_CAMPS_AND_VISION §5.4, CT-A17): the Listen Host shows other units only through its own projection.
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
        Check(Read.Frame && Read.Frame->Units.IsEmpty() && bDark, TEXT("#23"), TEXT("The timed-out view keeps no unit and no current coverage"));
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
        if (Lobby.Phase == ELDMatchPhase::Starting) { return true; }
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
            if (!Mark(TEXT("LobbyConfigured.json"))) { return Finish(); }
            bLobbyConfigured = true;
        }
        if (!Exists(TEXT("LobbyConfigured.json"))) { return true; }
        if (!Owner.OwnerState.bReady && Now >= NextReadyAt) { Owner.SetDemoReady(true); NextReadyAt = Now + 1; }
        if (Role != TEXT("Host")) { return true; }
        const bool bAllReady = Lobby.Members.Num() == 3
            && !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Member) { return !Member.Ready; });
        if (!bAllReady) { BlockedSince = 0; return true; }
        if (!bOffersStaged)
        {
            // Both owning clients' opening offer carries the taunt the stealth segment needs. The growth domain does not
            // exist before Start, so the Host's DEV growth fixture stages it in the Lobby.
            FString StageError;
            bool bStaged = LDDevGrowthFixture::IsSwitchPresent();
            int32 Staged = 0;
            for (const FLDLobbyMember& Member : Lobby.Members)
            {
                if (Member.Kind != ELDContenderKind::Human || Member.Host) { continue; }
                bStaged = bStaged && LDDevGrowthFixture::StageOffer(Owner.GetWorld(), Member.ContenderId,
                    {StealthTaunt(), FName(TEXT("attr_might")), FName(TEXT("attr_focus"))}, StageError);
                ++Staged;
            }
            if (!Check(bStaged && Staged == 2, TEXT("#ST"), TEXT("The Host's DEV growth fixture (-LDDevGrowthFixture) stages both owning clients' opening offer with active_come_here in the Lobby")
                + (StageError.IsEmpty() ? FString() : TEXT(": ") + StageError)))
            {
                return Finish();
            }
            bOffersStaged = true;
        }
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

    bool TickBody(double Now, ALDPlayerController& Owner, const FLDMapBinding& Goal)
    {
        if (!Cast<ALDCharacter>(Owner.GetPawn()) || Owner.OwnerState.LifePhase != ELDLifePhase::Alive
            || !Owner.OwnerState.BodyRef.IsValid() || !Owner.CanWorldCommand()) { return true; }
        OwnBody = Owner.OwnerState.BodyRef;
        Owner.Navigation->Navigate(Goal.Position);
        NavigateAttempts = 1;
        StageAt = Now;
        Stage = 3;
        return true;
    }

    // Each owner walks its own body to the public capture anchor with an ordinary ground order.
    bool TickArrive(double Now, ALDPlayerController& Owner, const FLDMapBinding& Goal)
    {
        const ALDCharacter* Body = Cast<ALDCharacter>(Owner.GetPawn());
        if (!Body || Owner.OwnerState.BodyRef != OwnBody)
        { return Fail(TEXT("#1"), TEXT("The owned body changed identity before observation was established")); }
        if (Owner.Navigation->HasMoveOrder()) { return true; }
        if (FVector::Dist2D(Body->GetNavAgentLocation(), Goal.Position) <= ArrivalCm)
        {
            if (!Mark(Role + TEXT("Arrived.json"))) { return Finish(); }
            Stage = 4;
            return true;
        }
        if (Now - StageAt < 1) { return true; }
        if (NavigateAttempts >= 3)
        { return Fail(TEXT("#1"), TEXT("The owned body could not reach the public capture anchor: ") + Owner.IntentFeedback); }
        Owner.Navigation->Navigate(Goal.Position);
        ++NavigateAttempts;
        StageAt = Now;
        return true;
    }

    bool TickSight(double Now, UWorld& InWorld, ALDPlayerController& Owner)
    {
        if (!AllMarkers(TEXT("Arrived.json"))) { return true; }
        if (SightDeadline == 0) { SightDeadline = Now + 20; }
        const ALDWorldViewRelay* Relay = nullptr;
        const FLDWorldView* Frame = CurrentFrame(Owner, Relay);
        if (!Frame || !Relay || OtherContenders(*Frame, Owner.OwnerState.ContenderId) < 2)
        {
            if (Now < SightDeadline) { return true; }
            return Fail(TEXT("#1"), Frame
                ? TEXT("At the shared capture anchor the current frame still lacks the other two contenders")
                : TEXT("No current authorized frame reached this owner (observation wiring pending)"));
        }
        if (Role == TEXT("Host"))
        {
            const ALDGameMode* Mode = InWorld.GetAuthGameMode<ALDGameMode>();
            if (!Mode) { return Fail(TEXT("#15H"), TEXT("The Listen Host has no authoritative game mode")); }
            CheckHostPresentation(InWorld, *Mode, Owner, *Relay, *Frame);
            if (!Passed) { return Finish(); }
            PendingImage = FPaths::Combine(Output, TEXT("HostSighted.png"));
            ImageAt = Now;
            FScreenshotRequest::RequestScreenshot(PendingImage, true, false);
            Stage = 5;
            return true;
        }
        CheckRestrictedRegistry(InWorld, Owner, *Relay, *Frame);
        if (!Passed || !Mark(Role + TEXT("Sighted.json"))) { return Finish(); }
        Stage = 6;
        return true;
    }

    bool TickImage(double Now)
    {
        if (IFileManager::Get().FileSize(*PendingImage) <= 0)
        {
            if (Now - ImageAt < 10) { return true; }
            return Fail(TEXT("#1"), TEXT("The participating Listen Host rendered no screenshot of its main view"));
        }
        Images.Add(MakeShared<FJsonValueString>(FPaths::GetCleanFilename(PendingImage)));
        Check(true, TEXT("#1"), TEXT("The participating Listen Host renders its main view (screenshot kept for presentation review)"));
        if (!Mark(TEXT("HostSighted.json"))) { return Finish(); }
        Stage = 6;
        return true;
    }

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
            bControlObserved = true;
            const ALDWorldViewRelay* Relay = nullptr;
            if (!CurrentFrame(Owner, Relay) || !Relay || Relay->ProxyCount() == 0) { bControlViolated = true; }
        }
        if (!Exists(TEXT("Client1Recovered.json"))) { return true; }
        Check(bControlObserved && !bControlViolated, TEXT("#23"),
            TEXT("Another owner's current view and proxies are untouched while one connection loses its frames"));
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

    // ---- Stealth segment (#ST): every role buys a paper box through its own connection; four trials hide one body each. ----

    int32 SelfIndex() const
    {
        for (int32 Index = 0; Index < 3; ++Index) { if (Role == VisionRoles[Index]) { return Index; } }
        return INDEX_NONE;
    }

    static int32 IndexOf(const TCHAR* Name)
    {
        for (int32 Index = 0; Index < 3; ++Index) { if (FCString::Strcmp(VisionRoles[Index], Name) == 0) { return Index; } }
        return INDEX_NONE;
    }

    TSharedPtr<FJsonObject> Load(const FString& Name) const
    {
        FString Json;
        TSharedPtr<FJsonObject> Value;
        if (!FFileHelper::LoadFileToString(Json, *FPaths::Combine(Output, Name))
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Value)) { return nullptr; }
        return Value;
    }

    // Trial marker, e.g. S2HiddenClient2.json.
    FString TM(const TCHAR* What, const FString& Who = FString()) const { return FString::Printf(TEXT("S%d%s%s.json"), Trial + 1, What, *Who); }

    bool AllTrialMarkers(const TCHAR* What, const TCHAR* Except = nullptr) const
    {
        for (const TCHAR* Name : VisionRoles)
        {
            if ((!Except || FCString::Strcmp(Name, Except) != 0) && !Exists(TM(What, Name))) { return false; }
        }
        return true;
    }

    bool CurrentCombatView(const ALDPlayerController& Owner) const
    {
        return Owner.CombatView.Generation == Owner.OwnerState.Match && Owner.CombatView.Life == Owner.OwnerState.Life;
    }

    const FLDCombatEntryView* EntryRow(const ALDPlayerController& Owner, FName Definition) const
    {
        return CurrentCombatView(Owner) ? Owner.CombatView.Entries.FindByPredicate([&Definition](const FLDCombatEntryView& Entry)
            { return Entry.DefinitionId == Definition && Entry.bHasActive; }) : nullptr;
    }

    // The held active is off cooldown on the authority clock and affordable. Unavailable is display-only and is not
    // republished when a cooldown merely runs out, so it is not read here; the server still judges the activation.
    bool EntryReady(const ALDPlayerController& Owner, FName Definition) const
    {
        const FLDCombatEntryView* Row = EntryRow(Owner, Definition);
        int64 NowMs = 0;
        return Row && Owner.ReadDemoAuthorityTimeMs(NowMs) && Row->ReadyAtMs <= NowMs && Owner.CombatView.MPP2 >= Row->ManaCostP2;
    }

    const FLDCombatStatusView* OwnStatus(const ALDPlayerController& Owner, ELDDemoStatusId Id) const
    {
        return CurrentCombatView(Owner) ? Owner.CombatView.Statuses.FindByPredicate([Id](const FLDCombatStatusView& Status)
            { return Status.StatusId == uint8(Id); }) : nullptr;
    }

    bool StealthCheck(bool bValue, const TCHAR* Id, const FString& Name)
    {
        return Check(bValue, Id, FString::Printf(TEXT("[S%d %s] "), Trial + 1, StealthTrials[Trial].Name) + Name);
    }

    // Host, Client1 and Client2 derive the same triangle beside the shop from public anchors and approved radii.
    bool PrepareStealthSites(const ULDMapRegistry& Registry)
    {
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        ShopId = Defs.World.ShopIds.IsEmpty() ? FName() : FName(*Defs.World.ShopIds[0]);
        const FLDMapBinding* ShopAnchor = Registry.Resolve(ShopId, Registry.Generation);
        const FLDMapBinding* CaptureAnchor = Registry.Resolve(FName(*Defs.World.CaptureId), Registry.Generation);
        const double ShopReach = VisionRational(Defs.Parameters.ShopInteractRadiusCm);
        const double CaptureReach = VisionRational(Defs.Parameters.CaptureRadiusCm);
        const double BodyRadius = VisionRational(Defs.Parameters.CollisionRadiusCm);
        const double SightReach = VisionRational(Defs.Parameters.VisionRadiusCm);
        const FLDDemoEquipmentDefinition* Box = Defs.Equipment.FindByPredicate([](const FLDDemoEquipmentDefinition& Item)
            { return FName(*Item.Id) == StealthItem(); });
        const FLDDemoEffectDefinition* Effect = Box && Box->HasActive ? Box->Active.Rank.Effects.FindByPredicate(
            [](const FLDDemoEffectDefinition& Candidate) { return Candidate.Opcode == uint8(ELDDemoEffectOp::Stealth); }) : nullptr;
        RevealCm = Effect ? VisionRational(Effect->RadiusCm) : -1;
        StealthDurationMs = Effect ? Effect->DurationMs : 0;
        if (!ShopAnchor || !CaptureAnchor || ShopReach <= 0 || CaptureReach <= 0 || BodyRadius <= 0 || RevealCm <= 0
            || StealthDurationMs <= 0 || StealthGapCm >= SightReach - 2.0 * BodyRadius
            || StealthGapCm * 0.8 <= RevealCm + StealthApproachCm + StealthSlackCm)
        {
            return Check(false, TEXT("#ST"), TEXT("The public anchors, the approved radii and the paper box's stealth admit an in-sight triangle beyond its reveal radius"));
        }
        const FVector Outward = (ShopAnchor->Position - CaptureAnchor->Position).GetSafeNormal2D();
        const FVector Across(Outward.Y, -Outward.X, 0.0);
        const FVector Base = ShopAnchor->Position + Across * (2.0 * ShopReach);
        FCollisionShape Capsule;
        FString SiteError;
        if (Outward.IsNearlyZero() || !ULDMapRegistry::ContentCapsule(*Catalog, Capsule, SiteError))
        {
            return Check(false, TEXT("#ST"), TEXT("The stealth sites have a body capsule and a direction from the shop: ") + SiteError);
        }
        const auto Legal = [&](const FVector& Site, FLDLegalGround& LegalSite)
        {
            ELDWorldQueryFailure SiteFailure = ELDWorldQueryFailure::None;
            bool bWithin = true;
            return Registry.QueryGround(Registry.Revision(), Site, Capsule, ShopAnchor->Surface, LegalSite, SiteFailure, SiteError)
                && Registry.QueryShopReach(Registry.Revision(), ShopId, Site, bWithin, SiteFailure, SiteError) && !bWithin
                && FVector::Dist2D(Site, CaptureAnchor->Position) > CaptureReach + 2.0 * BodyRadius;
        };
        // Twelve fixed bearings; the first whose three corners are legal ground joined pairwise by near-straight complete
        // routes (open ground) wins. Sight itself is proven at run time from each owner's frame.
        for (int32 Bearing = 0; Bearing < 12 && !bSitesReady; ++Bearing)
        {
            const FVector Direction = Across.RotateAngleAxis(30.0 * Bearing, FVector::UpVector);
            const FVector Candidate[3] = {Base, Base + Direction * StealthGapCm,
                Base + Direction.RotateAngleAxis(60.0, FVector::UpVector) * StealthGapCm};
            FLDLegalGround Ground[3];
            bool bOpen = Legal(Candidate[0], Ground[0]) && Legal(Candidate[1], Ground[1]) && Legal(Candidate[2], Ground[2]);
            for (int32 From = 0; bOpen && From < 3; ++From)
            {
                const int32 To = (From + 1) % 3;
                FLDCompletePath Route;
                ELDWorldQueryFailure RouteFailure = ELDWorldQueryFailure::None;
                bOpen = Registry.QueryCompletePath(Registry.Revision(), Ground[From], Ground[To], Capsule, Route, RouteFailure, SiteError)
                    && Route.LengthCm <= 1.15 * StealthGapCm;
            }
            if (bOpen)
            {
                for (int32 Index = 0; Index < 3; ++Index) { Sites[Index] = Candidate[Index]; }
                bSitesReady = true;
            }
        }
        return Check(bSitesReady, TEXT("#ST"), FString::Printf(
            TEXT("Three legal stealth sites %.0f cm apart beside the shop, outside its reach and the capture circle, joined by open routes (reveal radius %.0f cm, stealth %lld ms)%s"),
            StealthGapCm, RevealCm, StealthDurationMs, *(bSitesReady ? FString() : TEXT(": ") + SiteError)));
    }

    // A ground walk to Site with bounded retries; bArrived once the body stands within StealthSiteCm of it.
    bool StealthWalk(double Now, ALDPlayerController& Owner, const FVector& Site, bool& bArrived)
    {
        bArrived = false;
        const ALDCharacter* Body = Cast<ALDCharacter>(Owner.GetPawn());
        if (Owner.Navigation->HasMoveOrder() || (Run.WalkAttempts > 0 && Now - Run.ActAt < 1.0)) { return true; }
        if (Body && FVector::Dist2D(Body->GetNavAgentLocation(), Site) <= StealthSiteCm) { bArrived = true; return true; }
        if (Run.WalkAttempts >= StealthWalkAttempts)
        { return Fail(TEXT("#ST"), TEXT("The owned body could not reach its stealth site: ") + Owner.Navigation->Status); }
        Owner.Navigation->Navigate(Site);
        ++Run.WalkAttempts;
        Run.ActAt = Now;
        return true;
    }

    bool StepTimedOut(double Now, double Seconds, const FString& Why)
    {
        if (Now - StealthAt <= Seconds) { return false; }
        Fail(TEXT("#ST"), Why);
        return true;
    }

    void NextStealthStep(double Now)
    {
        ++StealthStep;
        StealthAt = Now;
    }

    bool TickStealth(double Now, UWorld& InWorld, ALDPlayerController& Owner, const ULDMapRegistry& Registry)
    {
        if (StealthAt == 0) { StealthAt = Now; }
        if (Owner.OwnerState.LifePhase != ELDLifePhase::Alive || !Cast<ALDCharacter>(Owner.GetPawn()))
        { return Fail(TEXT("#ST"), TEXT("The owned body left its life during the stealth segment")); }
        switch (StealthStep)
        {
        case 0: return TickStealthSetup(Now, Owner, Registry);
        case 1: return TickStealthFund(Now, InWorld);
        case 2: return TickStealthShop(Now, Owner);
        case 3: return TickStealthBuy(Now, Owner);
        default: return TickTrial(Now, InWorld, Owner);
        }
    }

    // Every role publishes its contender id; each owning client learns the staged taunt through the ordinary choice.
    bool TickStealthSetup(double Now, ALDPlayerController& Owner, const ULDMapRegistry& Registry)
    {
        if (!Catalog)
        {
            FString LoadError;
            if (!LDDemoEntry::LoadRuntimeContent(Catalog, LoadError) || !Catalog)
            { return Fail(TEXT("#ST"), TEXT("The approved Demo content is unavailable: ") + LoadError); }
        }
        if (!bSitesReady && !PrepareStealthSites(Registry)) { return Finish(); }
        if (!bIdMarked)
        {
            auto Value = MakeShared<FJsonObject>();
            Value->SetStringField(TEXT("contender"), Owner.OwnerState.ContenderId.ToString(EGuidFormats::DigitsWithHyphensLower));
            if (!Owner.OwnerState.ContenderId.IsValid() || !Save(Value, Role + TEXT("Id.json")))
            { return Fail(TEXT("#ST"), TEXT("This owner could not publish its contender id to the scenario")); }
            bIdMarked = true;
        }
        if (Role != TEXT("Host"))
        {
            const FLDProgressionView* View = Owner.ReadOwnProgression();
            const bool bLearned = View && View->Skills.ContainsByPredicate([](const FLDLearnedSkillView& Row) { return Row.DefinitionId == StealthTaunt(); });
            if (!bLearned)
            {
                if (ChoiceRequest == 0)
                {
                    const FLDChoiceOption* Card = View && View->bHasOffer && View->bCanChoose
                        ? View->Offer.Options.FindByPredicate([](const FLDChoiceOption& Option) { return Option.DefinitionId == StealthTaunt(); })
                        : nullptr;
                    if (!Card)
                    {
                        return StepTimedOut(Now, StealthStepSeconds, TEXT("No choosable opening offer carries the staged active_come_here")) ? false : true;
                    }
                    FLDChoiceCommand Command;
                    Command.OfferId = View->Offer.OfferId;
                    Command.OfferRevision = View->Offer.OfferRevision;
                    Command.OptionId = Card->OptionId;
                    Command.ExpectedBuildRevision = View->BuildRevision;
                    Command.CatalogDigest = View->CatalogDigest;
                    ELDGrowthReason LocalReason = ELDGrowthReason::None;
                    ChoiceRequest = Owner.SubmitChoice(Command, LocalReason);
                    if (!Check(ChoiceRequest != 0 && LocalReason == ELDGrowthReason::None, TEXT("#ST"),
                        TEXT("This owning client submits the staged active_come_here as its ordinary opening choice"))) { return Finish(); }
                }
                return StepTimedOut(Now, StealthStepSeconds, TEXT("The chosen active_come_here never reached this owner's build")) ? false : true;
            }
        }
        for (int32 Index = 0; Index < 3; ++Index)
        {
            if (Ids[Index].IsValid()) { continue; }
            const TSharedPtr<FJsonObject> Marker = Load(FString(VisionRoles[Index]) + TEXT("Id.json"));
            FString Text;
            if (!Marker || !Marker->TryGetStringField(TEXT("contender"), Text) || !FGuid::Parse(Text, Ids[Index]))
            {
                Ids[Index].Invalidate();
                return StepTimedOut(Now, StealthStepSeconds, TEXT("Not every role published its contender id")) ? false : true;
            }
        }
        if (!Check(Ids[SelfIndex()] == Owner.OwnerState.ContenderId && Ids[0] != Ids[1] && Ids[1] != Ids[2] && Ids[0] != Ids[2], TEXT("#ST"),
            TEXT("The three roles are three distinct contenders of this match"))) { return Finish(); }
        NextStealthStep(Now);
        return true;
    }

    // Host: the DEV growth fixture grants each contender exactly the gold it lacks for one paper box, on its authority.
    bool TickStealthFund(double Now, UWorld& InWorld)
    {
        if (Role == TEXT("Host") && !Exists(TEXT("StealthFunded.json")))
        {
            ALDGameMode* Mode = InWorld.GetAuthGameMode<ALDGameMode>();
            const FLDDemoEquipmentDefinition* Box = Catalog->Get().Equipment.FindByPredicate([](const FLDDemoEquipmentDefinition& Item)
                { return FName(*Item.Id) == StealthItem(); });
            if (!Mode || !Mode->Economy || !Box) { return Fail(TEXT("#ST"), TEXT("The Listen Host has the Economy owner and the approved paper box")); }
            if (!bGranted)
            {
                int64 Needed[3] = {0, 0, 0};
                for (int32 Index = 0; Index < 3; ++Index)
                {
                    const ALDPlayerController* Contender = Mode->FindContenderController(Ids[Index]);
                    const FLDEquipmentView* Equipment = Contender ? Contender->ReadOwnEquipment() : nullptr;
                    if (!Equipment) { return StepTimedOut(Now, StealthStepSeconds, TEXT("A contender's equipment view is unreadable on the Host")) ? false : true; }
                    Needed[Index] = FMath::Max<int64>(0, Box->BuyPrice - Equipment->Gold);
                }
                for (int32 Index = 0; Index < 3; ++Index)
                {
                    if (Needed[Index] == 0) { continue; }
                    FString GrantError;
                    const uint64 Ticket = Mode->Economy->DevGrantReward(Ids[Index], 0, Needed[Index], GrantError);
                    if (!Check(Ticket != 0, TEXT("#ST"), FString::Printf(TEXT("The DEV growth fixture queues the %lld gold %s lacks for the paper box%s"),
                        Needed[Index], VisionRoles[Index], *(GrantError.IsEmpty() ? FString() : TEXT(": ") + GrantError)))) { return Finish(); }
                    GrantTickets.Add(Ticket);
                }
                bGranted = true;
                return true;
            }
            for (const uint64 Ticket : GrantTickets)
            {
                FString Detail;
                const ELDDevGrantStatus Status = Mode->Economy->DevGrantStatus(Ticket, Detail);
                if (Status == ELDDevGrantStatus::Pending) { return StepTimedOut(Now, StealthStepSeconds, TEXT("A DEV gold grant stayed pending")) ? false : true; }
                if (!Check(Status == ELDDevGrantStatus::Granted, TEXT("#ST"), TEXT("Each DEV gold grant commits through the reward composite: ") + Detail)) { return Finish(); }
            }
            if (!Mark(TEXT("StealthFunded.json"))) { return Finish(); }
        }
        if (!Exists(TEXT("StealthFunded.json"))) { return StepTimedOut(Now, StealthStepSeconds, TEXT("The Host never funded the paper boxes")) ? false : true; }
        NextStealthStep(Now);
        return true;
    }

    // Each owner walks to the public shop; its own arrival handler opens the session (no probe call opens it).
    bool TickStealthShop(double Now, ALDPlayerController& Owner)
    {
        if (Owner.ReadOwnShop()) { NextStealthStep(Now); return true; }
        if (!bShopSent || (Now - StealthAt > StealthStepSeconds && ShopAttempts < 2))
        {
            Owner.Navigation->ApproachShop(ShopId);
            bShopSent = true;
            ++ShopAttempts;
            StealthAt = Now;
            return true;
        }
        return StepTimedOut(Now, StealthStepSeconds, TEXT("The shop approach opened no shop session: ") + Owner.Navigation->Status) ? false : true;
    }

    // One ordinary Buy of the paper box on the open quote; the box's active must then appear on the owner's 1/2 entry.
    bool TickStealthBuy(double Now, ALDPlayerController& Owner)
    {
        if (EntryRow(Owner, StealthItem()))
        {
            Check(true, TEXT("#ST"), TEXT("This owner bought item_paper_box through its own connection and holds its active on a gear entry"));
            NextStealthStep(Now);
            Phase = EPhase::Home;
            Run = LDDemoVisionScenario::FLDStealthRun();
            Run.At = Now;
            return true;
        }
        if (!bBuySent)
        {
            const FLDShopView* ShopView = Owner.ReadOwnShop();
            const FLDEquipmentView* Equipment = Owner.ReadOwnEquipment();
            const FLDProgressionView* Progression = Owner.ReadOwnProgression();
            const FLDShopItemView* Offered = ShopView ? ShopView->Items.FindByPredicate([](const FLDShopItemView& Item)
                { return Item.DefinitionId == StealthItem(); }) : nullptr;
            FLDShopCommand Command;
            if (!ShopView || !Equipment || !Progression || !Offered || Offered->BuyPrice <= 0 || Equipment->Gold < Offered->BuyPrice
                || !FLDDefinitionWire::Encode(ShopView->WorldMapId, Command.WorldMapId)
                || !FLDDefinitionWire::Encode(Offered->DefinitionId, Command.DefinitionId))
            {
                return StepTimedOut(Now, StealthStepSeconds, TEXT("The open quote never offered an affordable paper box")) ? false : true;
            }
            Command.Kind = ELDShopCommandKind::Buy;
            Command.SessionId = ShopView->SessionId;
            Command.QuoteRevision = ShopView->QuoteRevision;
            Command.CatalogDigest = ShopView->CatalogDigest;
            Command.WorldLayoutRevision = ShopView->WorldLayoutRevision;
            Command.WorldGeometryRevision = ShopView->WorldGeometryRevision;
            Command.ExpectedAccountRevision = Equipment->AccountRevision;
            Command.ExpectedInventoryRevision = Equipment->InventoryRevision;
            Command.ExpectedBuildRevision = Progression->BuildRevision;
            Command.BuyPrice = Offered->BuyPrice;
            ELDGrowthReason LocalReason = ELDGrowthReason::None;
            const uint64 Request = Owner.SubmitShopCommand(Command, LocalReason);
            if (!Check(Request != 0 && LocalReason == ELDGrowthReason::None, TEXT("#ST"), TEXT("The paper box Buy goes through SubmitShopCommand")))
            { return Finish(); }
            bBuySent = true;
            StealthAt = Now;
            return true;
        }
        return StepTimedOut(Now, StealthStepSeconds, TEXT("The paper box purchase never reached this owner's gear entries")) ? false : true;
    }

    void BeginPhase(EPhase Next, double Now)
    {
        Phase = Next;
        Run.At = Now;
        Run.WalkAttempts = 0;
        Run.ActAt = 0;
    }

    // Installs this trial's watches: an observer audits every frame its relay accepts; the Host also follows the
    // hider's authority stealth at every game tick and, while it observes, whether its own world draws the hider's body.
    void HookTrial(ALDPlayerController& Owner, const FGuid& Hidden, bool bHider)
    {
        UnhookTrial();
        TWeakPtr<FLDDemoVisionProbe> Weak = AsShared();
        if (!bHider)
        {
            if (ALDWorldViewRelay* Relay = Owner.LocalView())
            {
                HookedRelay = Relay;
                Relay->DevOnAcceptedFrame = [Weak, Hidden](const FLDWorldView& Frame)
                {
                    if (const TSharedPtr<FLDDemoVisionProbe> Probe = Weak.Pin()) { Probe->RecordFrame(Frame, Hidden); }
                };
            }
        }
        if (Role == TEXT("Host"))
        {
            TrialTickHandle = FWorldDelegates::OnWorldPostActorTick.AddLambda([Weak, Hidden, bHider](UWorld* Ticked, ELevelTick, float)
            {
                const TSharedPtr<FLDDemoVisionProbe> Probe = Weak.Pin();
                if (Probe && Ticked && Ticked == Probe->World.Get()) { Probe->WatchAuthority(*Ticked, Hidden, bHider); }
            });
        }
    }

    void UnhookTrial()
    {
        if (ALDWorldViewRelay* Relay = HookedRelay.Get()) { Relay->DevOnAcceptedFrame = nullptr; }
        HookedRelay.Reset();
        if (TrialTickHandle.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(TrialTickHandle); }
        TrialTickHandle.Reset();
    }

    void RecordFrame(const FLDWorldView& Frame, const FGuid& Hidden)
    {
        const ALDPlayerController* Owner = Controller.Get();
        const APawn* Own = Owner ? Owner->GetPawn() : nullptr;
        LDDemoVisionScenario::FLDStealthRun::FFrameRow Row;
        Row.SampleMs = int64(FMath::RoundToDouble(Frame.SampledAtServerSec * 1000.0));
        if (const FLDObservedUnitView* Unit = ContenderUnit(Frame, Hidden))
        {
            Row.bPresent = true;
            Row.Distance = Own ? FVector::Dist2D(FVector(Unit->FeetCm), Own->GetNavAgentLocation()) : -1;
            Row.bPreHandle = Unit->Handle == Run.PreHandle;
            Row.bStealthStatus = HasStatus(Unit->Statuses, ELDDemoStatusId::Stealth);
            Row.Handle = Unit->Handle;
            Row.Epoch = Unit->ObservationEpoch;
        }
        Run.Rows.Add(Row);
    }

    // Host: the test's timing reference for the stealth end, read from the authority it hosts (never shown to a player).
    void WatchAuthority(UWorld& InWorld, const FGuid& Hidden, bool bHider)
    {
        const ALDGameMode* Mode = InWorld.GetAuthGameMode<ALDGameMode>();
        const ALDPlayerController* HiderPC = Mode ? Mode->FindContenderController(Hidden) : nullptr;
        const ALDCharacter* Body = HiderPC ? Cast<ALDCharacter>(HiderPC->GetPawn()) : nullptr;
        const FLDBodyCombatState* State = Body && Mode->Combat ? Mode->Combat->StateFor(Body) : nullptr;
        if (!State) { return; }
        const int64 NowMs = Mode->GetAuthorityNowMs();
        if (LDControl::IsStealthed(State->Stealth, NowMs))
        {
            Run.bOracleSeen = true;
            Run.OracleLastMs = NowMs;
        }
        else if (Run.bOracleSeen && !Run.bOracleEnded)
        {
            Run.bOracleEnded = true;
            Run.OracleEndMs = NowMs;
        }
        if (bHider) { return; }
        ++Run.HostTicks;
        FString Part;
        if (HasPresentation(*Body, Part) && Run.HostDrawn.Len() < 500) { Run.HostDrawn += Part + TEXT(" "); }
    }

    // Host: publishes the authority stealth end once it is known, for every observer's classification of its frames.
    bool TickOracle()
    {
        if (Role != TEXT("Host") || Run.bEndedMarked || !Run.bOracleEnded) { return true; }
        auto Value = MakeShared<FJsonObject>();
        Value->SetNumberField(TEXT("last_stealth_ms"), double(Run.OracleLastMs));
        Value->SetNumberField(TEXT("ended_ms"), double(Run.OracleEndMs));
        if (!Save(Value, TM(TEXT("Ended")))) { return Fail(TEXT("#ST"), TEXT("The Host could not publish the stealth end")); }
        Run.bEndedMarked = true;
        return true;
    }

    bool LoadEnded()
    {
        if (Run.bEnded) { return true; }
        const TSharedPtr<FJsonObject> Marker = Load(TM(TEXT("Ended")));
        double Last = 0, Ended = 0;
        if (!Marker || !Marker->TryGetNumberField(TEXT("last_stealth_ms"), Last) || !Marker->TryGetNumberField(TEXT("ended_ms"), Ended)) { return false; }
        Run.bEnded = true;
        Run.LastStealthMs = int64(Last);
        Run.EndedMs = int64(Ended);
        return true;
    }

    bool TickTrial(double Now, UWorld& InWorld, ALDPlayerController& Owner)
    {
        const LDDemoVisionScenario::FLDStealthTrial& T = StealthTrials[Trial];
        const bool bHider = Role == T.Stealther;
        if (Phase == EPhase::Run && !TickOracle()) { return false; }
        switch (Phase)
        {
        case EPhase::Home: return TickTrialHome(Now, Owner, T, bHider);
        case EPhase::Pre: return TickTrialPre(Now, Owner, T, bHider);
        case EPhase::Run: return bHider ? TickHider(Now, Owner, T) : TickWatcher(Now, InWorld, Owner, T);
        default: break;
        }
        if (!AllTrialMarkers(TEXT("Done")))
        {
            if (Now - Run.At <= StealthStepSeconds) { return true; }
            return Fail(TEXT("#ST"), FString::Printf(TEXT("[S%d] Not every role finished the trial"), Trial + 1));
        }
        if (++Trial >= int32(UE_ARRAY_COUNT(StealthTrials)))
        {
            Stage = 8;
            return true;
        }
        Run = LDDemoVisionScenario::FLDStealthRun();
        BeginPhase(EPhase::Home, Now);
        return true;
    }

    // Everyone returns to its corner; the hider's box and any trigger skill must be ready; each sees the other two.
    bool TickTrialHome(double Now, ALDPlayerController& Owner, const LDDemoVisionScenario::FLDStealthTrial& T, bool bHider)
    {
        const bool bNear = T.Near && Role == T.Near;
        bool bArrived = false;
        if (!StealthWalk(Now, Owner, Sites[SelfIndex()], bArrived)) { return false; }
        if (bArrived && !Exists(TM(TEXT("Home"), Role)))
        {
            const ALDWorldViewRelay* Relay = nullptr;
            const FLDWorldView* Frame = CurrentFrame(Owner, Relay);
            int32 Seen = 0;
            for (int32 Index = 0; Frame && Index < 3; ++Index)
            {
                Seen += Index != SelfIndex() && ContenderUnit(*Frame, Ids[Index]) ? 1 : 0;
            }
            const bool bNeedsTaunt = (bHider && T.Trigger == EStealthTrigger::Cast) || (bNear && T.Trigger == EStealthTrigger::Control);
            Run.HomeSeen = Seen;
            Run.bHomeReady = (!bHider || EntryReady(Owner, StealthItem())) && (!bNeedsTaunt || EntryReady(Owner, StealthTaunt()));
            if (Seen == 2 && Run.bHomeReady && !Mark(TM(TEXT("Home"), Role))) { return Finish(); }
        }
        if (AllTrialMarkers(TEXT("Home"))) { BeginPhase(EPhase::Pre, Now); return true; }
        if (Now - Run.At <= (Trial == 3 ? StealthCooldownSeconds : StealthStepSeconds)) { return true; }
        return Fail(TEXT("#ST"), FString::Printf(TEXT("[S%d] Not every role stood ready in sight on its stealth site (this role arrived=%d sees=%d ready=%d)"),
            Trial + 1, int32(bArrived), Run.HomeSeen, int32(Run.bHomeReady)));
    }

    // Observers record the hider's current handle; the near one of the chase trial first holds an admitted attack order
    // on it. The hider then opens its box through its 1/2 hotkey entry.
    bool TickTrialPre(double Now, ALDPlayerController& Owner, const LDDemoVisionScenario::FLDStealthTrial& T, bool bHider)
    {
        const FGuid Hidden = Ids[IndexOf(T.Stealther)];
        if (!bHider)
        {
            if (!Run.PreHandle.IsValid())
            {
                const ALDWorldViewRelay* Relay = nullptr;
                const FLDWorldView* Frame = CurrentFrame(Owner, Relay);
                const FLDObservedUnitView* Unit = Frame ? ContenderUnit(*Frame, Hidden) : nullptr;
                if (!Unit)
                {
                    if (Now - Run.At <= 2.0) { return true; }
                    return Fail(TEXT("#ST"), FString::Printf(TEXT("[S%d] This observer lost the hider before it hid"), Trial + 1));
                }
                Run.PreHandle = Unit->Handle;
                Run.PreEpoch = Unit->ObservationEpoch;
                if (T.bChase && Role == T.Near)
                {
                    Owner.Navigation->AttackUnit(Unit->Handle, Unit->ObservationEpoch);
                    Run.Chase = Owner.Navigation->LocalIdentity();
                    Run.bChaseSent = true;
                }
            }
            if (Run.bChaseSent)
            {
                // The hider may hide only once the server admitted the lock, so its loss is a cancellation, not a refusal.
                const FLDOrderReceipt& Latest = Owner.Navigation->LatestReceipt();
                const bool bOwn = Latest.Identity == Run.Chase;
                if (!bOwn || (Latest.Status != ELDOrderStatus::Activated && Latest.Status != ELDOrderStatus::Progress))
                {
                    if (bOwn && (Latest.Status == ELDOrderStatus::Rejected || Latest.Status == ELDOrderStatus::Cancelled
                        || Latest.Status == ELDOrderStatus::Completed))
                    {
                        return Fail(TEXT("#ST"), TEXT("[S1] The attack order on the visible hider ended before it hid: ") + Owner.Navigation->Status);
                    }
                    if (Now - Run.At <= 3.0) { return true; }
                    return Fail(TEXT("#ST"), TEXT("[S1] The attack order on the visible hider was never activated"));
                }
            }
            HookTrial(Owner, Hidden, false);
            if (!Mark(TM(TEXT("Pre"), Role))) { return Finish(); }
            BeginPhase(EPhase::Run, Now);
            return true;
        }
        if (!AllTrialMarkers(TEXT("Pre"), T.Stealther))
        {
            if (Now - Run.At <= StealthStepSeconds) { return true; }
            return Fail(TEXT("#ST"), FString::Printf(TEXT("[S%d] The observers never recorded the hider"), Trial + 1));
        }
        const FLDCombatEntryView* Box = EntryRow(Owner, StealthItem());
        if (!Box) { return Fail(TEXT("#ST"), FString::Printf(TEXT("[S%d] The hider no longer holds its paper box"), Trial + 1)); }
        HookTrial(Owner, Hidden, true);
        Run.OwnHp = Owner.CombatView.HPP2;
        Owner.ActivateCombatEntry(Box->Entry);
        BeginPhase(EPhase::Run, Now);
        return true;
    }

    bool FinishTrial(double Now, const FString& Kind, const TSharedRef<FJsonObject>& Row)
    {
        UnhookTrial();
        Row->SetNumberField(TEXT("trial"), Trial + 1);
        Row->SetStringField(TEXT("trigger"), StealthTrials[Trial].Name);
        Row->SetStringField(TEXT("stealther"), StealthTrials[Trial].Stealther);
        Row->SetStringField(TEXT("role_kind"), Kind);
        Row->SetNumberField(TEXT("hidden_ms"), double(Run.HiddenMs));
        Row->SetNumberField(TEXT("expires_ms"), double(Run.ExpiresMs));
        Row->SetNumberField(TEXT("last_stealth_ms"), double(Run.LastStealthMs));
        Row->SetNumberField(TEXT("ended_ms"), double(Run.EndedMs));
        Row->SetStringField(TEXT("leaks"), Run.Leaks);
        StealthRows.Add(MakeShared<FJsonValueObject>(Row));
        if (!Passed || !Mark(TM(TEXT("Done"), Role))) { return Finish(); }
        BeginPhase(EPhase::Done, Now);
        return true;
    }

    // The hider: its own view shows the stealth, it acts on its trigger and its own view shows the stealth end early.
    bool TickHider(double Now, ALDPlayerController& Owner, const LDDemoVisionScenario::FLDStealthTrial& T)
    {
        const FLDCombatStatusView* Stealth = OwnStatus(Owner, ELDDemoStatusId::Stealth);
        const FGuid NearId = T.Near ? Ids[IndexOf(T.Near)] : FGuid();
        const ALDWorldViewRelay* Relay = nullptr;
        const FLDWorldView* Frame = CurrentFrame(Owner, Relay);
        const FLDObservedUnitView* NearUnit = Frame && T.Near ? ContenderUnit(*Frame, NearId) : nullptr;
        if (!Run.bHidden)
        {
            if (!Stealth)
            {
                if (Now - Run.At <= 3.0) { return true; }
                return Fail(TEXT("#ST"), FString::Printf(TEXT("[S%d] The paper box's active put no stealth status on this owner's view"), Trial + 1));
            }
            Run.bHidden = true;
            Run.ExpiresMs = Stealth->ExpiresAtMs;
            // The release sets the expiry exactly one approved duration after the authority time the stealth began.
            Run.HiddenMs = Stealth->ExpiresAtMs - StealthDurationMs;
            auto Value = MakeShared<FJsonObject>();
            Value->SetNumberField(TEXT("hidden_ms"), double(Run.HiddenMs));
            Value->SetNumberField(TEXT("expires_ms"), double(Run.ExpiresMs));
            if (!Save(Value, TM(TEXT("Hidden")))) { return Fail(TEXT("#ST"), TEXT("The hider could not announce its stealth")); }
            if (T.Near)
            {
                const ALDCharacter* Body = Cast<ALDCharacter>(Owner.GetPawn());
                if (!NearUnit || !Body) { return Fail(TEXT("#ST"), FString::Printf(TEXT("[S%d] The hider does not see its near observer"), Trial + 1)); }
                const FVector NearFeet = NearUnit->FeetCm;
                const FVector Goal = NearFeet + (Body->GetNavAgentLocation() - NearFeet).GetSafeNormal2D() * StealthApproachCm;
                Owner.Navigation->Navigate(FVector(Goal.X, Goal.Y, NearFeet.Z));
            }
            return true;
        }
        if (Stealth && Stealth->ExpiresAtMs != Run.ExpiresMs)
        { return Fail(TEXT("#ST"), FString::Printf(TEXT("[S%d] The stealth was replaced while it ran"), Trial + 1)); }
        Run.bTaunted |= OwnStatus(Owner, ELDDemoStatusId::Taunt) != nullptr;
        Run.bHurt |= CurrentCombatView(Owner) && Owner.CombatView.HPP2 < Run.OwnHp;
        if (CurrentCombatView(Owner) && !Stealth && !Run.bEarly && !Run.bLate)
        {
            // The first own view without the stealth was composed no earlier than its end.
            Run.RevealMs = Owner.CombatView.AsOfMs;
            Run.bEarly = Owner.CombatView.AsOfMs < Run.ExpiresMs;
            Run.bLate = !Run.bEarly;
        }
        if (T.Trigger == EStealthTrigger::Attack)
        {
            if (!Run.bActed && Exists(TM(TEXT("Found"))))
            {
                if (!NearUnit) { return Fail(TEXT("#ST"), TEXT("[S1] The hider lost its near observer before attacking")); }
                if (!Mark(TM(TEXT("Reveal")))) { return Finish(); }
                Run.TargetHp = NearUnit->Health.CurrentP2;
                Owner.Navigation->AttackUnit(NearUnit->Handle, NearUnit->ObservationEpoch);
                Run.bActed = true;
                Run.ActAt = Now;
            }
            if (Run.bActed && !Run.bStopped && ((NearUnit && NearUnit->Health.CurrentP2 < Run.TargetHp) || Now - Run.ActAt > 3.0))
            {
                Owner.StopCombatAndOrders();
                Run.bStopped = true;
            }
        }
        else if (T.Trigger == EStealthTrigger::Cast && !Run.bActed && AllTrialMarkers(TEXT("Concealed"), T.Stealther))
        {
            const FLDCombatEntryView* Taunt = EntryRow(Owner, StealthTaunt());
            if (!Taunt) { return Fail(TEXT("#ST"), TEXT("[S2] The hider holds no active_come_here to cast")); }
            if (!Mark(TM(TEXT("Reveal")))) { return Finish(); }
            Owner.ActivateCombatEntry(Taunt->Entry);
            Run.bActed = true;
        }
        const bool bTriggerDone = T.Trigger == EStealthTrigger::Attack ? Run.bStopped
            : T.Trigger == EStealthTrigger::Cast ? Run.bActed
            : T.Trigger == EStealthTrigger::Hurt ? Run.bHurt : Run.bTaunted;
        // The Host also owes every observer the authority end of this stealth before it leaves the trial.
        if ((Run.bEarly || Run.bLate) && bTriggerDone && (Role != TEXT("Host") || Run.bEndedMarked))
        {
            StealthCheck(Run.bEarly, TEXT("#ST"), FString::Printf(
                TEXT("The hider's own view shows its stealth ended at its %s before the expiry (ended by %lld ms, expiry %lld ms)"),
                T.Name, Run.RevealMs, Run.ExpiresMs));
            if (T.Trigger == EStealthTrigger::Hurt) { StealthCheck(Run.bHurt, TEXT("#ST"), TEXT("The stealthed body took the near observer's hit")); }
            if (T.Trigger == EStealthTrigger::Control) { StealthCheck(Run.bTaunted, TEXT("#ST"), TEXT("The near observer's taunt landed on the stealthed body as a control")); }
            auto Row = MakeShared<FJsonObject>();
            Row->SetNumberField(TEXT("revealed_ms"), double(Run.RevealMs));
            return FinishTrial(Now, TEXT("stealther"), Row);
        }
        if (Now - Run.At <= StealthTrialSeconds) { return true; }
        return Fail(TEXT("#ST"), FString::Printf(TEXT("[S%d] The %s trial did not complete for the hider (early=%d late=%d acted=%d stopped=%d hurt=%d taunted=%d)"),
            Trial + 1, T.Name, int32(Run.bEarly), int32(Run.bLate), int32(Run.bActed), int32(Run.bStopped), int32(Run.bHurt), int32(Run.bTaunted)));
    }

    // The relay and world state behind this observer's presentation: the old handle never resolves or keeps a proxy
    // again once a frame sampled after the stealth began is current, and a restricted client never holds another body.
    void AuditPresentation(UWorld& InWorld, const ALDPlayerController& Owner, const ALDWorldViewRelay& Relay)
    {
        const auto Leak = [this](const FString& Text) { if (Run.Leaks.Len() < 2000) { Run.Leaks += Text + TEXT("; "); } };
        FLDObservedUnitView Ignored;
        if (Relay.ReadUnit(Run.PreHandle, Run.PreEpoch, Ignored)) { Leak(TEXT("the old handle still resolves")); }
        for (TActorIterator<ALDObservedEntityProxy> It(&InWorld); It; ++It)
        {
            if (It->ObservedHandle == Run.PreHandle) { Leak(TEXT("a proxy still presents the old handle")); }
        }
        if (Role != TEXT("Host"))
        {
            int32 Pawns = 0;
            for (TActorIterator<APawn> It(&InWorld); It; ++It) { Pawns += *It != Owner.GetPawn() ? 1 : 0; }
            if (Pawns != 0) { Leak(TEXT("a foreign body was replicated to this restricted client")); }
        }
    }

    // An observer. Live: every accepted frame is recorded as it arrives; concealment is announced and the near observer
    // acts once it rediscovers the body inside the radius. Final, once the Host published the authority end: frames
    // sampled while stealthed must never show it beyond the radius or under its old handle; frames sampled at or after
    // the end must show it again (far observers) before it would have expired.
    bool TickWatcher(double Now, UWorld& InWorld, ALDPlayerController& Owner, const LDDemoVisionScenario::FLDStealthTrial& T)
    {
        using FFrameRow = LDDemoVisionScenario::FLDStealthRun::FFrameRow;
        const bool bNear = T.Near && Role == T.Near;
        if (!Run.bHidden)
        {
            const TSharedPtr<FJsonObject> Marker = Load(TM(TEXT("Hidden")));
            double HiddenMs = 0, ExpiresMs = 0;
            if (!Marker || !Marker->TryGetNumberField(TEXT("hidden_ms"), HiddenMs) || !Marker->TryGetNumberField(TEXT("expires_ms"), ExpiresMs))
            {
                if (Now - Run.At <= 5.0) { return true; }
                return Fail(TEXT("#ST"), FString::Printf(TEXT("[S%d] The hider never announced its stealth"), Trial + 1));
            }
            Run.bHidden = true;
            Run.HiddenMs = int64(HiddenMs);
            Run.ExpiresMs = int64(ExpiresMs);
            Run.At = Now;
        }
        LoadEnded();
        const double Limit = RevealCm + StealthSlackCm;
        // Losing sight cancels the admitted lock; nothing may resume it until the hider's stealth ended.
        if (Run.bChaseSent && !Run.bEnded)
        {
            const FLDOrderReceipt& Latest = Owner.Navigation->LatestReceipt();
            if (!Run.bChaseCancelled)
            {
                Run.bChaseCancelled = Latest.Identity == Run.Chase && Latest.Status == ELDOrderStatus::Cancelled
                    && Latest.Failure == ELDOrderFailure::TargetUnavailable && !Owner.Navigation->HasMoveOrder();
            }
            else { Run.bChaseResumed |= Owner.Navigation->HasMoveOrder() || Latest.Identity != Run.Chase; }
        }
        const ALDWorldViewRelay* Relay = nullptr;
        const FLDWorldView* Frame = CurrentFrame(Owner, Relay);
        if (Frame && Relay && int64(FMath::RoundToDouble(Frame->SampledAtServerSec * 1000.0)) >= Run.HiddenMs)
        {
            AuditPresentation(InWorld, Owner, *Relay);
            if (!Run.bProxyChecked && !Run.bEnded && !ContenderUnit(*Frame, Ids[IndexOf(T.Stealther)]))
            {
                Run.bProxyChecked = true;
                FString Why;
                StealthCheck(ProxiesMatchFrame(InWorld, *Relay, *Frame, Why), TEXT("#ST"),
                    TEXT("Once the body hid, this owner's proxies match its current frame, which no longer lists the body (the minimap and floating numbers read only this frame)")
                    + (Why.IsEmpty() ? FString() : TEXT(": ") + Why));
            }
        }
        // Live pass over newly accepted frames, provisionally hidden until the Host published the end.
        for (; Run.Cursor < Run.Rows.Num(); ++Run.Cursor)
        {
            const FFrameRow& Row = Run.Rows[Run.Cursor];
            if (Row.SampleMs < Run.HiddenMs || Row.SampleMs >= Run.ExpiresMs || (Run.bEnded && Row.SampleMs > Run.LastStealthMs)) { continue; }
            if (!Row.bPresent && !Run.bConcealed)
            {
                Run.bConcealed = true;
                if (!Passed || !Mark(TM(TEXT("Concealed"), Role))) { return Finish(); }
            }
            if (bNear && Row.bPresent && Run.bConcealed && !Run.bFound && Row.Distance >= 0 && Row.Distance <= Limit)
            {
                Run.bFound = true;
                if (!Mark(TM(TEXT("Found")))) { return Finish(); }
                if (T.Trigger == EStealthTrigger::Hurt)
                {
                    if (!Mark(TM(TEXT("Reveal")))) { return Finish(); }
                    const FLDObservedUnitView* Unit = Frame ? ContenderUnit(*Frame, Ids[IndexOf(T.Stealther)]) : nullptr;
                    Run.TargetHp = Unit ? Unit->Health.CurrentP2 : MAX_int64;
                    Owner.Navigation->AttackUnit(Row.Handle, Row.Epoch);
                    Run.bActed = true;
                    Run.ActAt = Now;
                }
                else if (T.Trigger == EStealthTrigger::Control)
                {
                    const FLDCombatEntryView* Taunt = EntryRow(Owner, StealthTaunt());
                    if (!Taunt) { return Fail(TEXT("#ST"), TEXT("[S4] The near observer holds no active_come_here")); }
                    if (!Mark(TM(TEXT("Reveal")))) { return Finish(); }
                    Owner.ActivateCombatEntry(Taunt->Entry);
                    Run.bActed = true;
                }
            }
        }
        if (bNear && Run.bActed && !Run.bStopped && T.Trigger == EStealthTrigger::Hurt)
        {
            const FLDObservedUnitView* Unit = Frame ? ContenderUnit(*Frame, Ids[IndexOf(T.Stealther)]) : nullptr;
            if ((Unit && Unit->Health.CurrentP2 < Run.TargetHp) || Now - Run.ActAt > 3.0)
            {
                Owner.StopCombatAndOrders();
                Run.bStopped = true;
            }
        }
        const FFrameRow* Regained = nullptr;
        if (Run.bEnded && !bNear)
        {
            Regained = Run.Rows.FindByPredicate([this](const FFrameRow& Row) { return Row.SampleMs >= Run.EndedMs && Row.bPresent; });
        }
        const bool bNearDone = Run.bFound && (T.Trigger != EStealthTrigger::Hurt || Run.bStopped) && (T.Trigger != EStealthTrigger::Control || Run.bActed);
        const bool bHostDone = Role != TEXT("Host") || Run.bEndedMarked;
        if (!(Run.bEnded && bHostDone && (bNear ? bNearDone : Regained != nullptr)))
        {
            if (Now - Run.At <= StealthTrialSeconds) { return true; }
            return Fail(TEXT("#ST"), FString::Printf(TEXT("[S%d] The %s trial did not complete for this %s observer (concealed=%d found=%d ended=%d regained=%d chase_cancelled=%d frames=%d)"),
                Trial + 1, T.Name, bNear ? TEXT("near") : TEXT("far"), int32(Run.bConcealed), int32(Run.bFound), int32(Run.bEnded),
                int32(Regained != nullptr), int32(Run.bChaseCancelled), Run.Rows.Num()));
        }
        // Final classification of every accepted frame by its authority sample time.
        int32 HiddenFrames = 0, Unsettled = 0;
        bool bConcealedFinal = false;
        const FFrameRow* Found = nullptr;
        for (const FFrameRow& Row : Run.Rows)
        {
            if (Row.SampleMs < Run.HiddenMs) { continue; }
            if (Row.bPresent && Row.bPreHandle && Run.Leaks.Len() < 2000) { Run.Leaks += TEXT("the old handle came back; "); }
            if (Row.SampleMs > Run.LastStealthMs)
            {
                // Between the last tick still stealthed and the first one no longer, a sample is neither proof nor leak.
                Unsettled += Row.SampleMs < Run.EndedMs ? 1 : 0;
                continue;
            }
            ++HiddenFrames;
            bConcealedFinal |= !Row.bPresent;
            if (!Row.bPresent) { continue; }
            if ((Row.Distance < 0 || Row.Distance > Limit) && Run.Leaks.Len() < 2000) { Run.Leaks += FString::Printf(TEXT("presented at %.0f cm while stealthed; "), Row.Distance); }
            if (Row.bStealthStatus && Run.Leaks.Len() < 2000) { Run.Leaks += TEXT("its owner-only stealth status reached this observer; "); }
            if (!Found && bConcealedFinal) { Found = &Row; }
        }
        StealthCheck(Run.Leaks.IsEmpty() && HiddenFrames > 0 && bConcealedFinal, TEXT("#ST"), FString::Printf(
            TEXT("Every frame this observer accepted while the body was stealthed (%d; %d inside the authority end's tick left unclassified) hid it beyond the reveal radius and never under its old handle; the old handle never resolved or kept a proxy%s"),
            HiddenFrames, Unsettled, *(Run.Leaks.IsEmpty() ? FString() : TEXT(": ") + Run.Leaks)));
        auto Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("frames_while_hidden"), HiddenFrames);
        Row->SetNumberField(TEXT("frames_unclassified"), Unsettled);
        if (bNear)
        {
            StealthCheck(Found && Found->Handle != Run.PreHandle && !Found->bStealthStatus, TEXT("#ST"), FString::Printf(
                TEXT("The body, still stealthed, is rediscovered inside the reveal radius (%.0f cm) under a fresh handle, without its stealth status"),
                Found ? Found->Distance : -1.0));
            Row->SetNumberField(TEXT("rediscovered_cm"), Found ? Found->Distance : -1.0);
            if (T.bChase)
            {
                StealthCheck(Run.bChaseCancelled && !Run.bChaseResumed, TEXT("#ST"),
                    TEXT("Losing sight cancelled this observer's admitted attack lock and chase (TargetUnavailable), and nothing resumed them until the stealth ended"));
                Row->SetBoolField(TEXT("chase_cancelled"), Run.bChaseCancelled && !Run.bChaseResumed);
            }
        }
        else
        {
            StealthCheck(Run.EndedMs < Run.ExpiresMs && Regained->Handle != Run.PreHandle && Regained->SampleMs < Run.ExpiresMs
                && Regained->Distance > Limit && !Regained->bStealthStatus, TEXT("#ST"), FString::Printf(
                TEXT("After the %s ended the stealth early (authority %lld ms < expiry %lld ms), the first frame showing the body again (sample %lld ms, %.0f cm away) uses a fresh handle"),
                T.Name, Run.EndedMs, Run.ExpiresMs, Regained->SampleMs, Regained->Distance));
            Row->SetNumberField(TEXT("regained_sample_ms"), double(Regained->SampleMs));
            Row->SetNumberField(TEXT("regained_cm"), Regained->Distance);
        }
        if (Role == TEXT("Host"))
        {
            StealthCheck(Run.HostTicks > 0 && Run.HostDrawn.IsEmpty(), TEXT("#STH"), FString::Printf(
                TEXT("The Listen Host's world drew the stealthed remote body at none of its %d game ticks of the trial%s"),
                Run.HostTicks, *(Run.HostDrawn.IsEmpty() ? FString() : TEXT(": ") + Run.HostDrawn)));
            Row->SetNumberField(TEXT("host_ticks"), Run.HostTicks);
        }
        return FinishTrial(Now, bNear ? TEXT("near") : TEXT("far"), Row);
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
        Report->SetStringField(TEXT("scope"), TEXT("DEV-only owned-connection observation scenario: a rendered, participating Listen Host and two owning clients started through the normal lobby. Carries checks #1, #9, #15, #23 and #28 of the retired view network probe plus Listen Host presentation (#15H), then the paper box stealth segment (#ST, #STH): every role buys the box with DEV-fixture gold through its own connection and four trials hide one body each, revealed by its attack, its cast, a hit it takes and a taunt. Not combat balance, death or termination, AI or full-match acceptance."));
        Report->SetBoolField(TEXT("growth_fixture"), LDDevGrowthFixture::IsSwitchPresent());
        Report->SetArrayField(TEXT("stealth"), StealthRows);
        Report->SetStringField(TEXT("catalog_sha256"), CatalogDigest);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetNumberField(TEXT("stage"), Stage);
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
            if (Exists(TEXT("FinishClients"))) { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
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
            Result->SetStringField(TEXT("scope"), TEXT("After both owning clients disconnect, the Listen Host's own current view and proxies no longer present them."));
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
        case 2: return TickBody(Now, *Owner, *Goal);
        case 3: return TickArrive(Now, *Owner, *Goal);
        case 4: return TickSight(Now, *Current, *Owner);
        case 5: return TickImage(Now);
        case 6: return TickStall(Now, *Current, *Owner);
        case 7: return TickStealth(Now, *Current, *Owner, *Registry);
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
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .1f);
}
#endif
