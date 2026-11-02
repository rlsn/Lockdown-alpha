#include "Tests/LDDemoCombatProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapGeometry.h"
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

// Owned-connection combat and orders scenario (COMBAT_AND_ATTRIBUTES, CONTROLS_AND_TARGETING). Client1 acts only
// through its own connection and the calls its hotkeys make; the participating Listen Host stands still while it is
// attacked, audits those hits from its authority and then kills Client1 with an ordinary right-click order. The probe
// never calls a driver entry, an _Implementation or a Combat write. Named, not anonymous: unity builds merge files.
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
}

class FLDDemoCombatProbe : public TSharedFromThis<FLDDemoCombatProbe>
{
public:
    enum class EStage : uint8
    {
        Join, Lobby, Spawned, ToSite,
        // Client1
        AttackMove, Engaged, Stopping, AwaitDeath, AwaitRespawn, Sighting, Spend, Settle,
        // Host
        Struck, Quiet, Kill, KillWait, Respawn, Removal, Wrap
    };

    TWeakObjectPtr<ALDPlayerController> Controller;
    TWeakObjectPtr<UWorld> World;
    FString Output, Role, Error, CatalogDigest;
    double Deadline = 0, StageAt = 0, NextCommandAt = 0, NextReadyAt = 0, BlockedSince = 0;
    EStage Stage = EStage::Join;
    bool Passed = true, Written = false, bJoined = false, bLobbyConfigured = false, bStartIssued = false, bSitesReady = false;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TArray<TSharedPtr<FJsonValue>> Checks;

    // Deterministic public duel sites beside the shop, outside its reach and the capture circle.
    FVector HostSite = FVector::ZeroVector, ClientSite = FVector::ZeroVector;
    double RespawnSeconds = 0;
    FGuid HostId, ClientId;
    int32 NavigateAttempts = 0, AttackOrders = 0;

    // Client1: the attack-move, its stop and the spent protection.
    FLDMoveIdentity AttackMoveOrder, StopOrder;
    int64 HostHpAtOrder = 0, ProtectedUntilSeenMs = 0;
    bool bEngagedSeen = false, bHitSeen = false;
    uint64 LifeRevisionBefore = 0;
    uint32 ReceiptSerialBefore = 0;
    FLDCombatActionReceipt SpentReceipt;

    // Host: the hits it took, its kill and the audited respawn.
    int64 HpAtStopP2 = -1, BasicIntervalMs = 0, RespawnAtMs = 0, ProtectedUntilMs = 0;
    double StopSampledAt = 0;
    FLDMoveIdentity KillOrder;
    FString RespawnAnchor;

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
        case EStage::Lobby: return TickLobby(Now, *PC, Lobby);
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
        case EStage::Struck: return TickStruck(Now, *PC, *Mode);
        case EStage::Quiet: return TickQuiet(Now, *PC, *Mode);
        case EStage::Kill: return TickKill(Now, *PC);
        case EStage::KillWait: return TickKillWait(Now, *PC, *Mode, *Public);
        case EStage::Respawn: return TickRespawn(Now, *Mode, *Registry);
        case EStage::Removal: return TickRemoval(Now, *Mode);
        case EStage::Wrap: return TickWrap(Lobby);
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
    bool TickLobby(double Now, ALDPlayerController& PC, const FLDLobbyView& Lobby)
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
        if (!ResolveRoster(PC, Lobby) || !PrepareSites(Registry)) { return Finish(); }
        Check(PC.OwnerState.Life == 1 && PC.OwnerState.ProtectedUntilMs == 0, TEXT("Start"),
            TEXT("The first body is an unprotected initial life"));
        if (!Passed) { return Finish(); }
        PC.Navigation->Navigate(IsHost() ? HostSite : ClientSite);
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

    // Both roles derive the same two sites from public anchors and approved radii, then prove them legal.
    bool PrepareSites(const ULDMapRegistry& Registry)
    {
        if (bSitesReady) { return true; }
        const FLDDemoContentDefinitions& Defs = Catalog->Get();
        const FLDMapBinding* ShopAnchor = Registry.Resolve(FName(*Defs.World.ShopId), Registry.Generation);
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
                && Registry.QueryShopReach(Registry.Revision(), Site, bWithin, SiteFailure, SiteError) && !bWithin
                && FVector::Dist2D(Site, CaptureAnchor->Position) > CaptureReach + 2.0 * BodyRadius;
        };
        FLDLegalGround HostGround;
        const bool bHostLegal = !Outward.IsNearlyZero() && ULDMapRegistry::ContentCapsule(*Catalog, Capsule, SiteError)
            && Legal(HostSite, HostGround);
        // Only Client1 walks to its own site: eight fixed bearings around the Host's site, the first with legal ground
        // and a near-straight complete route from the Host's site (open ground in sight) wins.
        bool bClientLegal = IsHost();
        for (int32 Bearing = 0; bHostLegal && !bClientLegal && Bearing < 8; ++Bearing)
        {
            ClientSite = HostSite + Across.RotateAngleAxis(45.0 * Bearing, FVector::UpVector) * Gap;
            FLDLegalGround ClientGround;
            FLDCompletePath Route;
            ELDWorldQueryFailure RouteFailure = ELDWorldQueryFailure::None;
            bClientLegal = Legal(ClientSite, ClientGround)
                && Registry.QueryCompletePath(Registry.Revision(), HostGround, ClientGround, Capsule, Route, RouteFailure, SiteError)
                && Route.LengthCm <= 1.2 * Gap;
        }
        bSitesReady = bHostLegal && bClientLegal;
        return Check(bSitesReady, TEXT("Start"),
            TEXT("The duel sites are legal ground outside the shop reach and the capture circle, joined by an open complete route"),
            FString::Printf(TEXT("host=%s client=%s %s"), *HostSite.ToCompactString(), *ClientSite.ToCompactString(), *SiteError));
    }

    bool TickToSite(double Now, ALDPlayerController& PC)
    {
        const ALDCharacter* Body = Cast<ALDCharacter>(PC.GetPawn());
        if (!Body || PC.OwnerState.LifePhase != ELDLifePhase::Alive || PC.OwnerState.Life != 1)
        {
            return Fail(TEXT("Start"), TEXT("The first life ended before the duel although nobody attacks yet"));
        }
        const FVector& Site = IsHost() ? HostSite : ClientSite;
        if (PC.Navigation->HasMoveOrder()) { return true; }
        if (FVector::Dist2D(Body->GetNavAgentLocation(), Site) <= LDDemoCombatScenario::CombatProbeArrivalCm)
        {
            if (!Mark(Role + TEXT("AtSite.json"))) { return Finish(); }
            SetStage(IsHost() ? EStage::Struck : EStage::AttackMove);
            return true;
        }
        if (Now - StageAt < 1.0) { return true; }
        if (NavigateAttempts >= LDDemoCombatScenario::CombatProbeNavigateAttempts)
        {
            return Fail(TEXT("Start"), TEXT("The owned body could not reach its public duel site: ") + PC.IntentFeedback);
        }
        PC.Navigation->Navigate(Site);
        ++NavigateAttempts;
        StageAt = Now;
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
        Check(PC.Navigation->Status == TEXT("Stopped"), TEXT("Stop"),
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
            Check(PC.OwnerState.RespawnAtMs > 0 && PC.OwnerState.ProtectedUntilMs == 0 && !PC.CanWorldCommand(), TEXT("Death"),
                TEXT("The owner projection reports its own death with a respawn instant and refuses world commands"),
                FString::Printf(TEXT("respawn_at_ms=%lld"), PC.OwnerState.RespawnAtMs));
            if (!Passed) { return Finish(); }
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
        ReceiptSerialBefore = PC.CombatReceiptSerial;
        // The Q hotkey's own entry: an empty slot still reaches the server as an activation attempt.
        PC.ActivateCombatEntry(ELDCombatEntry::Q);
        SetStage(EStage::Spend);
        return true;
    }

    bool TickSpend(double Now, ALDPlayerController& PC)
    {
        const FLDCombatActionReceipt& Seen = PC.LatestCombatReceipt;
        const bool bAnswered = PC.CombatReceiptSerial != ReceiptSerialBefore && Seen.Entry == ELDCombatEntry::Q && Seen.Life == 2;
        if (!bAnswered)
        {
            if (Now - StageAt <= LDDemoCombatScenario::CombatProbeStepSeconds) { return true; }
            return Fail(TEXT("Protection"), TEXT("The empty Q attempt received no receipt over the owning connection"));
        }
        if (SpentReceipt.Entry == ELDCombatEntry::None)
        {
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
        if (Own->Resources.HpP2 < Own->Resources.MaxHpP2)
        {
            Check(true, TEXT("AttackMove"), TEXT("The Host's authority body takes the remote attack-move's basic hits"),
                FString::Printf(TEXT("hp_p2=%lld/%lld"), Own->Resources.HpP2, Own->Resources.MaxHpP2));
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
        SetStage(EStage::Wrap);
        return true;
    }

    bool TickWrap(const FLDLobbyView& Lobby)
    {
        if (!Exists(TEXT("Client1.json"))) { return true; }
        Check(Lobby.Phase == ELDMatchPhase::Development || Lobby.Phase == ELDMatchPhase::CaptureOpen, TEXT("Protection"),
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
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), Passed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetBoolField(TEXT("dev_probe"), true);
        Report->SetBoolField(TEXT("normal_start"), true);
        Report->SetStringField(TEXT("scope"), IsHost()
            ? TEXT("DEV-only owned-connection combat/orders scenario, target and killer role: a participating Listen Host that is attacked, audits the hits, kills with a right-click order and audits the respawn. Not skills, gear actives, camps, AI, capture or full-match acceptance.")
            : TEXT("DEV-only owned-connection combat/orders scenario, attacker and victim role: attack-move, S, death, the random protected respawn and an empty Q through the owning connection. Not skills, gear actives, camps, AI, capture or full-match acceptance."));
        Report->SetStringField(TEXT("catalog_sha256"), CatalogDigest);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetNumberField(TEXT("stage"), int32(Stage));
        Report->SetArrayField(TEXT("checks"), Checks);
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
        }
        else
        {
            Report->SetNumberField(TEXT("spent_receipt_phase"), int32(SpentReceipt.Phase));
            Report->SetNumberField(TEXT("spent_receipt_reject"), int32(SpentReceipt.Reject));
            Report->SetBoolField(TEXT("spent_protection_removed"), SpentReceipt.bProtectionRemoved);
        }
        Written = Save(Report, Role + TEXT(".json"));
        UE_LOG(LogTemp, Display, TEXT("LD_DEMO_COMBAT_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
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
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_COMBAT_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    Probe->Controller = &Controller;
    Probe->World = Controller.GetWorld();
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .1f);
}
#endif
