#include "Tests/LDDemoVisionProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Navigation/LDNavigationComponent.h"
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
// closure plan) plus the Listen Host presentation gate. Codec, ordering, handles and sight stay in
// Lockdown.Rules.DemoVision.*; this scenario does not repeat them. Named, not anonymous: unity builds merge files.
namespace LDDemoVisionScenario
{
const TCHAR* const VisionRoles[] = {TEXT("Host"), TEXT("Client1"), TEXT("Client2")};
// All three bodies walk to the public capture anchor; a crowded arrival may stop short of its exact point.
constexpr double ArrivalCm = 150.0;

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
        Report->SetStringField(TEXT("scope"), TEXT("DEV-only owned-connection observation scenario: a rendered, participating Listen Host and two owning clients started through the normal lobby. Carries checks #1, #9, #15, #23 and #28 of the retired view network probe plus Listen Host presentation (#15H). Not combat, death or termination, AI or full-match acceptance."));
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
        UE_LOG(LogTemp, Display, TEXT("LD_DEMO_VISION_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
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
                UE_LOG(LogTemp, Error, TEXT("LD_DEMO_VISION_DISCONNECT_FAILED %s"), *Why);
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
    int32 Timeout = 180;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoVisionTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoVisionOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoVisionRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1") && Probe->Role != TEXT("Client2"))
        || Timeout < 60 || Timeout > 300
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_VISION_PROBE Invalid isolated test arguments."));
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
