#include "Tests/LDDemoCampProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDWorldViewRelay.h"
#include "World/LDCampDirector.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "UObject/Package.h"

// Owned-connection camp checks (App Spec §5.2/§10, WORLD_CAMPS_AND_VISION §8.1): no first strike, whole-camp
// retaliation, last-hit reward once per life, clear-then-refresh timing, leash return with the public cue and an
// unrewarded home reset. Bookkeeping invariants stay in Lockdown.Rules.DemoCamp.*. Named, not anonymous: unity
// builds merge files.
namespace LDDemoCampScenario
{
const TCHAR* const CampRoles[] = {TEXT("Host"), TEXT("Client1"), TEXT("Client2")};
// Basin points of the #97 layout, checked offline on the approved world: an open, reachable melee approach beside
// Low camp l09 and High camp h05 (no other camp of the same creature within the target search), and a flee goal on
// open basin ground 14.7 m from h05, beyond the High leash and away from the Boss patrol and the flagpole.
const FVector CampLowApproachCm(1452, -2386, 0);
const FVector CampHighApproachCm(1723, 818, 0);
const FVector CampFleeGoalCm(2100, -850, 0);
constexpr double CampApproachArrivalCm = 150.0;
constexpr double CampTargetSearchCm = 600.0;
// Local placement offsets reach 100.344 cm from a member home.
constexpr double CampHomePlacementCm = 101.0;
const FName CampLowId(TEXT("l09"));
const FName CampHighId(TEXT("h05"));

// The approved reward of clearing one low camp: every member's stage-0 XP (the first lives spawn before T=0).
bool CampLowClearReward(int64& OutXP)
{
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FString Error;
    const FLDDemoMonsterDefinition* Monster = nullptr;
    if (!LDDemoEntry::LoadRuntimeContent(Content, Error) || !Content || !Content->FindMonster(TEXT("monster_scavenger"), Monster, Error)
        || !Monster) { return false; }
    OutXP = Monster->RewardXP * Monster->CountPerCamp;
    return OutXP > 0;
}

// The owner's current authorized frame, or null while it is unavailable or stale.
const FLDWorldView* CampCurrentFrame(const ALDPlayerController& Owner)
{
    const ALDWorldViewRelay* Relay = Owner.LocalView();
    if (!Relay) { return nullptr; }
    const FLDObservedFrameRead Read = Relay->ReadObservedFrame();
    return Read.Status == ELDObservedViewStatus::Current ? Read.Frame : nullptr;
}

const FLDObservedUnitView* NearestCreature(const FLDWorldView& Frame, ELDDemoMonsterId Creature, const FVector& Near)
{
    const FLDObservedUnitView* Best = nullptr;
    double BestDistance = FMath::Square(CampTargetSearchCm);
    for (const FLDObservedUnitView& Unit : Frame.Units)
    {
        const double Distance = FVector::DistSquared2D(FVector(Unit.FeetCm), Near);
        if (Unit.Kind == ELDObservedUnitKind::CampMember && Unit.CreatureId == uint8(Creature) && Distance <= BestDistance)
        {
            Best = &Unit;
            BestDistance = Distance;
        }
    }
    return Best;
}

class FLDDemoCampProbe : public TSharedFromThis<FLDDemoCampProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Controller;
    TWeakObjectPtr<UWorld> World;
    FString Output, Role, Error, CatalogDigest;
    double Deadline = 0, StageAt = 0, NextCommandAt = 0, NextReadyAt = 0, BlockedSince = 0, NextOrderAt = 0, WatchSince = 0;
    int32 Stage = 0, NavigateAttempts = 0;
    uint32 LobbyGeneration = 0;
    bool Passed = true, Written = false, bJoined = false, bLobbyConfigured = false;
    TArray<TSharedPtr<FJsonValue>> Checks;
    // Owning-client state.
    int64 StartXP = -1;
    bool bArrived = false, bIdleConfirmed = false, bAttacking = false, bTookDamage = false, bRewarded = false;
    bool bSawReturningCue = false, bCueShapeValid = true;
    TSet<FGuid> SeenScavengers;
    // Client2's high-camp route; Client2 is also the rendered owner that keeps the presentation screenshots.
    enum class EHighStep : uint8 { Approach, Attack, Flee, FollowHome, ToLowCamp, Done };
    EHighStep HighStep = EHighStep::Approach;
    FString PendingImage;
    double ImageAt = 0, LowShotSince = 0;
    bool bGuardianShot = false;
    TArray<TSharedPtr<FJsonValue>> Images;
    // Listen Host state.
    FGuid Client1, Client2;
    bool bCampsReady = false, bLowIdle = false, bLowEngaged = false, bLowCleared = false, bLowRefreshed = false, bHighReset = false;
    int64 LowDueMs = 0;

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

    bool Mark(const FString& Name, const TSharedRef<FJsonObject>& Value = MakeShared<FJsonObject>())
    {
        Value->SetBoolField(TEXT("ready"), true);
        if (Save(Value, Name)) { return true; }
        Check(false, TEXT("#C1"), TEXT("The isolated scenario could not persist its marker ") + Name);
        return false;
    }

    bool Exists(const FString& Name) const { return FPaths::FileExists(FPaths::Combine(Output, Name)); }

    TSharedPtr<FJsonObject> Load(const FString& Name) const
    {
        FString Text;
        TSharedPtr<FJsonObject> Value;
        if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Output, Name))
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Value)) { return nullptr; }
        return Value;
    }

    bool AllMarkers(const TCHAR* Suffix) const
    {
        for (const TCHAR* Name : CampRoles)
        {
            if (!Exists(FString(Name) + Suffix)) { return false; }
        }
        return true;
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
            Check(InWorld.GetNetMode() == NM_ListenServer && Owner.HasAuthority(), TEXT("#C1"),
                TEXT("A participating Listen Host with its own local player serves two real owning connections"));
        }
        else
        {
            Check(InWorld.GetNetMode() == NM_Client && !Owner.HasAuthority() && Owner.GetNetConnection() != nullptr, TEXT("#C1"),
                TEXT("This contender plays through a real owning remote connection"));
        }
        if (!Passed) { return Finish(); }
        Stage = 1;
        return true;
    }

    // The normal lobby path: no AI seats, every human ready, then the Host's ordinary Start request.
    bool TickLobby(double Now, ALDPlayerController& Owner, const FLDLobbyView& Lobby)
    {
        // An aborted start returns the room to a new Lobby generation; retrying would only hide the refusal.
        if (LobbyGeneration == 0) { LobbyGeneration = Lobby.Generation; }
        else if (Lobby.Generation != LobbyGeneration)
        {
            return Fail(TEXT("#C1"), FString::Printf(TEXT("The normal start aborted back to Lobby generation %u (see LD_MATCH_START_ABORTED in the Host log)"),
                Lobby.Generation));
        }
        if (Lobby.Phase == ELDMatchPhase::Development || Lobby.Phase == ELDMatchPhase::CaptureOpen) { Stage = 2; return true; }
        if (Lobby.Phase == ELDMatchPhase::Starting || Lobby.Phase == ELDMatchPhase::Countdown) { return true; }
        if (Lobby.Phase != ELDMatchPhase::Lobby)
        {
            return Fail(TEXT("#C1"), TEXT("The room left the lobby without opening play: ")
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
        return Fail(TEXT("#C1"), TEXT("The normal lobby Start stays refused with every human ready: ") + Lobby.StartBlocker);
    }

    // Every owner publishes its contender identity so the Host can correlate server-side camp aggro.
    bool TickPlaying(ALDPlayerController& Owner)
    {
        if (!Cast<ALDCharacter>(Owner.GetPawn()) || Owner.OwnerState.LifePhase != ELDLifePhase::Alive
            || !Owner.OwnerState.BodyRef.IsValid() || !Owner.CanWorldCommand()) { return true; }
        auto Identity = MakeShared<FJsonObject>();
        Identity->SetStringField(TEXT("contender"), Owner.OwnerState.ContenderId.ToString(EGuidFormats::DigitsWithHyphens));
        if (!Mark(Role + TEXT("Identity.json"), Identity)) { return Finish(); }
        Stage = 3;
        return true;
    }

    // ---- Owning clients ----

    bool Navigate(double Now, ALDPlayerController& Owner, const FVector& Goal, const TCHAR* Id)
    {
        const ALDCharacter* Body = Cast<ALDCharacter>(Owner.GetPawn());
        if (!Body) { return Fail(Id, TEXT("The owned body disappeared while walking to a camp")); }
        if (FVector::Dist2D(Body->GetNavAgentLocation(), Goal) <= CampApproachArrivalCm) { bArrived = true; return true; }
        if (Owner.Navigation->HasMoveOrder() || Now < NextOrderAt) { return true; }
        if (NavigateAttempts >= 4)
        { return Fail(Id, TEXT("The owned body could not reach its camp approach: ") + Owner.IntentFeedback); }
        Owner.Navigation->Navigate(Goal);
        ++NavigateAttempts;
        NextOrderAt = Now + 1;
        return true;
    }

    void SnapshotGrowth(const ALDPlayerController& Owner)
    {
        StartXP = Owner.OwnerState.Growth.Progression.CumulativeXP;
    }

    // A new destination supersedes the current move order at once; Navigate() then only retries or arrives.
    void IssueMove(double Now, ALDPlayerController& Owner, const FVector& Goal)
    {
        bArrived = false;
        NavigateAttempts = 1;
        NextOrderAt = Now + 1;
        Owner.Navigation->Navigate(Goal);
    }

    // True when the unit's presented proxy projects into the inner rendered view, so its world label is drawn.
    static bool CampOnScreen(const ALDPlayerController& Owner, const FLDObservedUnitView& Unit)
    {
        const ALDWorldViewRelay* Relay = Owner.LocalView();
        FVector Centre;
        FVector2D Screen;
        int32 Width = 0, Height = 0;
        Owner.GetViewportSize(Width, Height);
        return Relay && Width > 0 && Height > 0 && Relay->ReadPresentedLocation(Unit.Handle, Unit.ObservationEpoch, Centre)
            && Owner.ProjectWorldLocationToScreen(Centre, Screen)
            && Screen.X >= Width * .1 && Screen.X <= Width * .9 && Screen.Y >= Height * .2 && Screen.Y <= Height * .85;
    }

    // One screenshot at a time, written by the next rendered frame and kept for presentation review only.
    void RequestShot(const TCHAR* Name, double Now)
    {
        PendingImage = FPaths::Combine(Output, Name);
        ImageAt = Now;
        FScreenshotRequest::RequestScreenshot(PendingImage, true, false);
    }

    // 1 once the requested screenshot is on disk, 0 while no frame has written it yet, -1 once it is overdue.
    int32 PendingShot(double Now)
    {
        if (IFileManager::Get().FileSize(*PendingImage) > 0)
        {
            Images.Add(MakeShared<FJsonValueString>(FPaths::GetCleanFilename(PendingImage)));
            PendingImage.Reset();
            return 1;
        }
        return Now - ImageAt < 10 ? 0 : -1;
    }

    // Records the camp_returning cue shape of every visible camp member; returns the visible returning guardian.
    const FLDObservedUnitView* ScanReturningCue(const FLDWorldView& Frame)
    {
        const FLDObservedUnitView* Guardian = nullptr;
        for (const FLDObservedUnitView& Unit : Frame.Units)
        {
            if (Unit.Kind != ELDObservedUnitKind::CampMember) { continue; }
            for (const FLDObservedStatus& Status : Unit.Statuses)
            {
                if (Status.StatusId != uint8(ELDDemoStatusId::CampReturning)) { continue; }
                bCueShapeValid &= Status.Stacks == 1 && Status.ExpiresAtServerMs == 0;
                if (Unit.CreatureId == uint8(ELDDemoMonsterId::MonsterGuardian))
                {
                    bSawReturningCue = true;
                    Guardian = &Unit;
                }
            }
        }
        return Guardian;
    }

    bool TickLowCamp(double Now, ALDPlayerController& Owner)
    {
        if (!Exists(TEXT("CampsReady.json"))) { return true; }
        if (!bArrived)
        {
            if (!Navigate(Now, Owner, CampLowApproachCm, TEXT("#C3"))) { return false; }
            if (!bArrived) { return true; }
            SnapshotGrowth(Owner);
            if (!Mark(TEXT("Client1AtLow.json"))) { return Finish(); }
        }
        if (!bIdleConfirmed)
        {
            if (!Exists(TEXT("LowIdleChecked.json"))) { return true; }
            Check(Owner.OwnerState.Hp == Owner.OwnerState.MaxHp && Owner.OwnerState.MaxHp > 0, TEXT("#C3"),
                TEXT("Standing inside an untouched camp's perception does not draw a first strike"));
            bIdleConfirmed = true;
        }
        bTookDamage |= Owner.OwnerState.Hp < Owner.OwnerState.MaxHp;
        const FLDWorldView* Frame = CampCurrentFrame(Owner);
        if (Frame)
        {
            for (const FLDObservedUnitView& Unit : Frame->Units)
            {
                if (Unit.Kind == ELDObservedUnitKind::CampMember && Unit.CreatureId == uint8(ELDDemoMonsterId::MonsterScavenger)
                    && FVector::Dist2D(FVector(Unit.FeetCm), CampLowApproachCm) <= CampTargetSearchCm && !bRewarded)
                {
                    SeenScavengers.Add(Unit.Handle);
                }
            }
        }
        if (!Exists(TEXT("LowCleared.json")))
        {
            // Keep one attack order on the nearest visible scavenger; a dead target ends its order.
            if (Frame && Now >= NextOrderAt && !Owner.Navigation->HasMoveOrder())
            {
                if (const FLDObservedUnitView* Target = NearestCreature(*Frame, ELDDemoMonsterId::MonsterScavenger, CampLowApproachCm))
                {
                    Owner.Navigation->AttackUnit(Target->Handle, Target->ObservationEpoch);
                    bAttacking = true;
                    NextOrderAt = Now + .5;
                }
            }
            return true;
        }
        if (!bRewarded)
        {
            const int64 XP = Owner.OwnerState.Growth.Progression.CumulativeXP - StartXP;
            // The clear marker can arrive before the last reward's owner projection; allow it 5 s to replicate.
            if (WatchSince == 0) { WatchSince = Now; }
            int64 WantXP = 0;
            if (!CampLowClearReward(WantXP)) { return Fail(TEXT("#C5"), TEXT("The approved low-camp reward is unavailable")); }
            if (XP < WantXP && Now - WatchSince < 5) { return true; }
            Check(bAttacking && bTookDamage, TEXT("#C4"), TEXT("The attacked camp retaliated against this contender"));
            Check(XP == WantXP, TEXT("#C5"), FString::Printf(
                TEXT("The last hits on the low camp paid exactly its approved reward to this contender (XP +%lld of %lld)"),
                XP, WantXP));
            bRewarded = true;
            WatchSince = 0;
        }
        if (!Exists(TEXT("LowRefreshed.json"))) { return true; }
        if (WatchSince == 0) { WatchSince = Now; }
        int32 Fresh = 0;
        if (Frame)
        {
            for (const FLDObservedUnitView& Unit : Frame->Units)
            {
                Fresh += Unit.Kind == ELDObservedUnitKind::CampMember && Unit.CreatureId == uint8(ELDDemoMonsterId::MonsterScavenger)
                    && FVector::Dist2D(FVector(Unit.FeetCm), CampLowApproachCm) <= CampTargetSearchCm
                    && !SeenScavengers.Contains(Unit.Handle) ? 1 : 0;
            }
        }
        if (Fresh < 2 && Now - WatchSince < 5) { return true; }
        Check(Fresh == 2, TEXT("#C7"), FString::Printf(
            TEXT("The refreshed low camp presents two new member lives to this contender (%d new handles)"), Fresh));
        if (!Mark(TEXT("Client1Done.json"))) { return Finish(); }
        Stage = 4;
        return true;
    }

    // Client2 leashes h05's guardian and, as the only rendered owner, keeps two presentation screenshots (no pixel
    // checks): the returning guardian with its name and cue, then the refreshed low pair with their names. Adjacent low
    // and high camps stand about 3057 cm apart, wider than any frame of the fixed camera, so each tier has its own shot.
    bool TickHighCamp(double Now, ALDPlayerController& Owner)
    {
        if (!Exists(TEXT("CampsReady.json"))) { return true; }
        if (!FApp::CanEverRender()) { return Fail(TEXT("#C9"), TEXT("The screenshot owner runs without a renderer")); }
        if (!PendingImage.IsEmpty())
        {
            const int32 Shot = PendingShot(Now);
            if (Shot < 0) { return Fail(TEXT("#C9"), TEXT("The rendered owner wrote no screenshot within 10 s")); }
            if (Shot == 0) { return true; }
        }
        const FLDWorldView* Frame = CampCurrentFrame(Owner);
        const FLDObservedUnitView* Returning = Frame ? ScanReturningCue(*Frame) : nullptr;
        switch (HighStep)
        {
        case EHighStep::Approach:
            if (!Navigate(Now, Owner, CampHighApproachCm, TEXT("#C6"))) { return false; }
            if (!bArrived) { return true; }
            SnapshotGrowth(Owner);
            HighStep = EHighStep::Attack;
            return true;
        case EHighStep::Attack:
        {
            const FLDObservedUnitView* Guardian = Frame ? NearestCreature(*Frame, ELDDemoMonsterId::MonsterGuardian, CampHighApproachCm) : nullptr;
            if (Guardian && Guardian->Health.CurrentP2 < Guardian->Health.MaxP2)
            {
                // Break off after the first landed hit and run past the guardian's leash; the move supersedes the attack.
                IssueMove(Now, Owner, CampFleeGoalCm);
                HighStep = EHighStep::Flee;
            }
            else if (Guardian && Now >= NextOrderAt && !Owner.Navigation->HasMoveOrder())
            {
                Owner.Navigation->AttackUnit(Guardian->Handle, Guardian->ObservationEpoch);
                NextOrderAt = Now + .5;
            }
            return true;
        }
        case EHighStep::Flee:
            if (!Returning) { return Navigate(Now, Owner, CampFleeGoalCm, TEXT("#C6")); }
            // Walk back behind the returning guardian so it stays in the rendered view until its home reset.
            IssueMove(Now, Owner, CampHighApproachCm);
            HighStep = EHighStep::FollowHome;
            return true;
        case EHighStep::FollowHome:
        {
            if (!bArrived && !Navigate(Now, Owner, CampHighApproachCm, TEXT("#C9"))) { return false; }
            if (!bGuardianShot && Returning && CampOnScreen(Owner, *Returning))
            {
                RequestShot(TEXT("CampGuardianReturning.png"), Now);
                bGuardianShot = true;
                return true;
            }
            if (!Exists(TEXT("HighReset.json"))) { return true; }
            if (!bGuardianShot)
            { return Fail(TEXT("#C9"), TEXT("The returning guardian never stood in this owner's rendered view before its reset")); }
            const int64 XP = Owner.OwnerState.Growth.Progression.CumulativeXP - StartXP;
            Check(bSawReturningCue && bCueShapeValid, TEXT("#C6"),
                TEXT("The visible returning guardian carried only the single-stack untimed camp_returning cue"));
            Check(XP == 0, TEXT("#C6"), FString::Printf(
                TEXT("A leashed return and home reset paid nothing (XP +%lld)"), XP));
            IssueMove(Now, Owner, CampLowApproachCm);
            HighStep = EHighStep::ToLowCamp;
            return true;
        }
        case EHighStep::ToLowCamp:
        {
            if (!bArrived && !Navigate(Now, Owner, CampLowApproachCm, TEXT("#C9"))) { return false; }
            if (!bArrived || !Exists(TEXT("LowRefreshed.json"))) { return true; }
            if (LowShotSince == 0) { LowShotSince = Now; }
            int32 Shown = 0;
            if (Frame)
            {
                for (const FLDObservedUnitView& Unit : Frame->Units)
                {
                    Shown += Unit.Kind == ELDObservedUnitKind::CampMember && Unit.CreatureId == uint8(ELDDemoMonsterId::MonsterScavenger)
                        && FVector::Dist2D(FVector(Unit.FeetCm), CampLowApproachCm) <= CampTargetSearchCm && CampOnScreen(Owner, Unit) ? 1 : 0;
                }
            }
            if (Shown < 2)
            {
                if (Now - LowShotSince < 10) { return true; }
                return Fail(TEXT("#C9"), FString::Printf(
                    TEXT("The refreshed low pair never stood together in this owner's rendered view (%d shown)"), Shown));
            }
            RequestShot(TEXT("CampLowPair.png"), Now);
            HighStep = EHighStep::Done;
            return true;
        }
        case EHighStep::Done:
            Check(true, TEXT("#C9"),
                TEXT("The rendered owner kept CampGuardianReturning.png and CampLowPair.png for presentation review"));
            if (!Mark(TEXT("Client2Done.json"))) { return Finish(); }
            Stage = 4;
            return true;
        }
        return true;
    }

    // ---- Listen Host (authority reads only; it never drives camps) ----

    bool ReadIdentity(const TCHAR* Name, FGuid& Out) const
    {
        const TSharedPtr<FJsonObject> Value = Load(Name);
        FString Text;
        return Value && Value->TryGetStringField(TEXT("contender"), Text) && FGuid::Parse(Text, Out) && Out.IsValid();
    }

    bool MemberFoot(const ALDGameMode& Mode, const FLDCampMemberState& Member, FVector& Out) const
    {
        ALDCharacter* Body = nullptr;
        if (!Member.bAlive || Mode.ResolveBody(Mode.GetMatchContext(), Member.Body, Body) != ELDCommitReject::None || !Body) { return false; }
        Out = Body->GetCharacterMovement()->GetActorFeetLocation();
        return true;
    }

    bool CurrentBody(const ALDGameMode& Mode, const FGuid& Contender, FLDCombatBodyRef& Out) const
    {
        FLDContenderLifeRead Life;
        FString Why;
        if (!Mode.ReadContenderLife(Mode.GetMatchContext(), Contender, Life, Why) || Life.Phase != ELDLifePhase::Alive) { return false; }
        Out = Life.Body;
        return true;
    }

    bool TickHost(double Now, ALDGameMode& Mode)
    {
        const ALDCampDirector* Camps = Mode.Camps.Get();
        const FLDCampMatchState* State = Camps ? &Camps->GetState() : nullptr;
        const FLDCampRoster* Roster = State ? State->GetRoster() : nullptr;
        if (!State || !State->IsActive() || !Roster || !Mode.Combat)
        { return Fail(TEXT("#C2"), TEXT("A started match has no active camp director")); }
        const int32 Low = Roster->FindCamp(CampLowId), High = Roster->FindCamp(CampHighId);
        if (Low == INDEX_NONE || High == INDEX_NONE) { return Fail(TEXT("#C2"), TEXT("The approved roster lacks l09 or h05")); }
        if (!bCampsReady)
        {
            int32 Alive = 0, Placed = 0, Idle = 0;
            bool bFirstGeneration = State->NumCamps() == Roster->Camps.Num();
            for (int32 Camp = 0; Camp < State->NumCamps(); ++Camp)
            {
                const FLDCampGroupState& Group = *State->FindCamp(Camp);
                bFirstGeneration &= Group.Generation == 1;
                for (int32 Slot = 0; Slot < Group.Members.Num(); ++Slot)
                {
                    const FLDCampMemberState& Member = Group.Members[Slot];
                    FVector Foot;
                    Alive += Member.bAlive && Member.Life == 1 ? 1 : 0;
                    Idle += Member.Brain.Phase == ELDCampBrainPhase::Idle && Member.HostileSources.IsEmpty() ? 1 : 0;
                    Placed += MemberFoot(Mode, Member, Foot)
                        && FVector::Dist2D(Foot, Roster->Camps[Camp].Members[Slot].HomeCm) <= CampHomePlacementCm ? 1 : 0;
                }
            }
            int32 Members = 0;
            for (const auto& Camp : Roster->Camps) { Members += Camp.Members.Num(); }
            if (!Check(bFirstGeneration && Members > 0 && Alive == Members && Placed == Members && Idle == Members, TEXT("#C2"), FString::Printf(
                TEXT("Normal start spawned every approved camp once with idle first lives at their homes (alive %d, placed %d, idle %d of %d)"),
                Alive, Placed, Idle, Members))) { return Finish(); }
            if (!Mark(TEXT("CampsReady.json"))) { return Finish(); }
            bCampsReady = true;
        }
        // The two scenario tracks run concurrently, so neither may wait inside the other.
        if (!TickHostHigh(Mode, *State, *Roster, High)) { return false; }
        if ((!Client1.IsValid() && !ReadIdentity(TEXT("Client1Identity.json"), Client1))
            || (!Client2.IsValid() && !ReadIdentity(TEXT("Client2Identity.json"), Client2))) { return true; }
        if (!TickHostLow(Now, Mode, *State, *Roster, Low)) { return false; }
        if (bLowRefreshed && bHighReset)
        {
            if (!Mark(TEXT("HostDone.json"))) { return Finish(); }
            Stage = 4;
        }
        return true;
    }

    // The guardian's return is judged from its committed epochs, so a short Returning window cannot be missed.
    bool TickHostHigh(const ALDGameMode& Mode, const FLDCampMatchState& State, const FLDCampRoster& Roster, int32 High)
    {
        if (bHighReset) { return true; }
        const FLDCampMemberState& Guardian = State.FindCamp(High)->Members[0];
        if (!Guardian.bAlive || Guardian.Life != 1)
        { return Fail(TEXT("#C6"), TEXT("The leashed guardian must keep its first life through the whole return")); }
        if (Guardian.ReturnEpoch == 0 || Guardian.Brain.Phase != ELDCampBrainPhase::Idle || Guardian.ResetThroughEpoch != Guardian.ReturnEpoch)
        { return true; }
        FLDCombatBodyRead Read;
        FVector Foot;
        const bool bFull = Mode.Combat->ReadCombatBody(Guardian.Body, Read) && Read.HPP2 == Read.MaxHPP2 && Read.MaxHPP2 > 0;
        const bool bHome = MemberFoot(Mode, Guardian, Foot) && FVector::Dist2D(Foot, Roster.Camps[High].Members[0].HomeCm) <= CampHomePlacementCm;
        if (!Check(Guardian.ReturnEpoch == 1 && Guardian.HostileSources.IsEmpty() && bFull && bHome, TEXT("#C6"), FString::Printf(
            TEXT("The leashed guardian returned and reset once at home to full HP on the same life without aggro (epoch %llu, home %d, full %d)"),
            static_cast<unsigned long long>(Guardian.ReturnEpoch), bHome, bFull)))
        { return Finish(); }
        if (!Mark(TEXT("HighReset.json"))) { return Finish(); }
        bHighReset = true;
        return true;
    }

    bool TickHostLow(double Now, const ALDGameMode& Mode, const FLDCampMatchState& State, const FLDCampRoster& Roster, int32 Low)
    {
        const FLDCampGroupState& LowGroup = *State.FindCamp(Low);
        if (!bLowIdle)
        {
            if (!Exists(TEXT("Client1AtLow.json"))) { return true; }
            if (WatchSince == 0) { WatchSince = Now; }
            FLDCombatBodyRef Body;
            FLDCombatBodyRead Read;
            const bool bUntouched = CurrentBody(Mode, Client1, Body) && Mode.Combat->ReadCombatBody(Body, Read) && Read.HPP2 == Read.MaxHPP2
                && !LowGroup.Members.ContainsByPredicate([](const FLDCampMemberState& Member)
                    { return !Member.bAlive || Member.Brain.Phase != ELDCampBrainPhase::Idle || !Member.HostileSources.IsEmpty(); });
            if (!bUntouched) { return Fail(TEXT("#C3"), TEXT("An untouched low camp stays idle while a contender stands inside its perception")); }
            if (Now - WatchSince < 1.5) { return true; }
            Check(true, TEXT("#C3"), TEXT("An untouched low camp stays idle while a contender stands inside its perception"));
            if (!Mark(TEXT("LowIdleChecked.json"))) { return Finish(); }
            bLowIdle = true;
            WatchSince = 0;
        }
        if (!bLowEngaged && LowGroup.Members.ContainsByPredicate([](const FLDCampMemberState& Member)
            { return Member.bAlive && Member.Brain.Phase != ELDCampBrainPhase::Idle; }))
        {
            FLDCombatBodyRef Body;
            const bool bBody = CurrentBody(Mode, Client1, Body);
            int32 Engaged = 0, Living = 0;
            for (const FLDCampMemberState& Member : LowGroup.Members)
            {
                if (!Member.bAlive) { continue; }
                ++Living;
                Engaged += Member.Brain.Phase == ELDCampBrainPhase::Engaged && bBody && Member.HostileSources.Contains(Body) ? 1 : 0;
            }
            if (!Check(Living == 2 && Engaged == 2, TEXT("#C4"), FString::Printf(
                TEXT("The first confirmed hit engaged every living low-camp member against the attacker (%d of %d)"), Engaged, Living)))
            { return Finish(); }
            bLowEngaged = true;
        }
        if (bLowEngaged && !bLowCleared && LowGroup.bRespawnPending)
        {
            const bool bDead = !LowGroup.Members.ContainsByPredicate([](const FLDCampMemberState& Member) { return Member.bAlive || Member.Life != 1; });
            const int64 Delay = LowGroup.RespawnDueMs - LowGroup.ClearedAtMs;
            if (!Check(bDead && Delay == Roster.Camps[Low].RespawnMs, TEXT("#C5"), FString::Printf(
                TEXT("Clearing the low camp ends each first life once and schedules one refresh %lld ms after the last death"), Delay)))
            { return Finish(); }
            auto Due = MakeShared<FJsonObject>();
            Due->SetNumberField(TEXT("due_ms"), double(LowGroup.RespawnDueMs));
            if (!Mark(TEXT("LowCleared.json"), Due)) { return Finish(); }
            LowDueMs = LowGroup.RespawnDueMs;
            bLowCleared = true;
        }
        if (bLowCleared && !bLowRefreshed && Mode.GetAuthorityNowMs() >= LowDueMs && LowGroup.Generation == 2)
        {
            int32 Alive = 0, Placed = 0, Idle = 0;
            for (int32 Slot = 0; Slot < LowGroup.Members.Num(); ++Slot)
            {
                const FLDCampMemberState& Member = LowGroup.Members[Slot];
                FVector Foot;
                Alive += Member.bAlive && Member.Life == 2 ? 1 : 0;
                Idle += Member.Brain.Phase == ELDCampBrainPhase::Idle && Member.HostileSources.IsEmpty() ? 1 : 0;
                Placed += MemberFoot(Mode, Member, Foot) && FVector::Dist2D(Foot, Roster.Camps[Low].Members[Slot].HomeCm) <= CampHomePlacementCm ? 1 : 0;
            }
            if (!Check(Alive == 2 && Placed == 2 && Idle == 2, TEXT("#C7"), FString::Printf(
                TEXT("The due refresh re-spawned the whole low camp as second lives at home (alive %d, placed %d, idle %d)"), Alive, Placed, Idle)))
            { return Finish(); }
            if (!Mark(TEXT("LowRefreshed.json"))) { return Finish(); }
            bLowRefreshed = true;
        }
        return true;
    }

    bool TickReport()
    {
        for (const TCHAR* Marker : {TEXT("HostDone.json"), TEXT("Client1Done.json"), TEXT("Client2Done.json")})
        {
            if (!Exists(Marker)) { return true; }
        }
        return Finish();
    }

    bool Finish()
    {
        if (Written) { return true; }
        auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), Passed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetBoolField(TEXT("dev_probe"), true);
        Report->SetBoolField(TEXT("normal_start"), true);
        Report->SetStringField(TEXT("scope"), TEXT("DEV-only owned-connection camp scenario: a participating Listen Host and two owning clients started through the normal lobby. Covers no first strike, whole-camp retaliation, last-hit camp rewards, clear-then-refresh timing and a leashed return with an unrewarded home reset (#C1-#C7); the rendered Client2 keeps presentation screenshots without pixel comparison (#C9). Not AI, capture, full-match or balance acceptance."));
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
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_CAMP_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
        if (!Passed || !Written) { FPlatformMisc::RequestExitWithStatus(false, 2); return false; }
        return true;
    }

    bool TickAfterReport()
    {
        if (Role != TEXT("Host"))
        {
            if (Exists(TEXT("FinishClients"))) { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
            return true;
        }
        if (Exists(TEXT("Finish"))) { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
        return true;
    }

    bool Tick()
    {
        const double Now = FPlatformTime::Seconds();
        if (Written) { return TickAfterReport(); }
        if (Now >= Deadline)
        { return Fail(TEXT("#C8"), FString::Printf(TEXT("The camp scenario did not finish within its bounded deadline (stage %d)"), Stage)); }
        ALDPlayerController* Owner = Controller.Get();
        UWorld* Current = World.Get();
        if (!Owner || !Current) { return Fail(TEXT("#C1"), TEXT("The owning controller or its world ended before the scenario finished")); }
        const ALDGameState* Public = Current->GetGameState<ALDGameState>();
        const ULDMapRegistry* Registry = Current->GetSubsystem<ULDMapRegistry>();
        if (!Public || !Registry || !Registry->Enabled()) { return true; }
        if (CatalogDigest.IsEmpty())
        {
            FString Why;
            if (!LDDemoEntry::VerifyRuntimeFiles(Why, &CatalogDigest))
            { return Fail(TEXT("#C1"), TEXT("The approved runtime files are unavailable: ") + Why); }
        }
        const UNetDriver* Driver = Current->GetNetDriver();
        if (Stage >= 1)
        {
            const bool bConnected = Role == TEXT("Host")
                ? Driver && Driver->ClientConnections.Num() == 2
                : Driver && Driver->ServerConnection != nullptr;
            if (!bConnected) { return Fail(TEXT("#C1"), TEXT("An owning connection dropped before the scenario finished")); }
        }
        switch (Stage)
        {
        case 0: return TickJoin(*Current, *Owner, Public->DemoLobby, Driver);
        case 1: return TickLobby(Now, *Owner, Public->DemoLobby);
        case 2: return TickPlaying(*Owner);
        case 3:
        {
            if (Role == TEXT("Client1")) { return TickLowCamp(Now, *Owner); }
            if (Role == TEXT("Client2")) { return TickHighCamp(Now, *Owner); }
            ALDGameMode* Mode = Current->GetAuthGameMode<ALDGameMode>();
            return Mode ? TickHost(Now, *Mode) : Fail(TEXT("#C1"), TEXT("The Listen Host lost its authority game mode"));
        }
        default: return TickReport();
        }
    }
};
}

void LDDemoTests::StartCampProbe(ALDPlayerController& Controller)
{
    using LDDemoCampScenario::FLDDemoCampProbe;
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoCampProbe")) || !Controller.IsLocalController()
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap) { return; }
    const TSharedRef<FLDDemoCampProbe> Probe = MakeShared<FLDDemoCampProbe>();
    int32 Timeout = 300;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoCampTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoCampOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoCampRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1") && Probe->Role != TEXT("Client2"))
        || Timeout < 120 || Timeout > 900
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_CAMP_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    Probe->Controller = &Controller;
    Probe->World = Controller.GetWorld();
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .1f);
}
#endif
