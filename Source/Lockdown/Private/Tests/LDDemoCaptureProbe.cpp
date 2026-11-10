#include "Tests/LDDemoCaptureProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDProgressionRules.h"
#include "Navigation/LDNavigationComponent.h"
#include "Navigation/LDPathSteering.h"
#include "Region/LDMapRegistry.h"
#include "Tests/LDScenarioWalk.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDWorldViewRelay.h"
#include "World/LDCampDirector.h"
#include "World/LDCampLifecycle.h"
#include "World/LDCaptureMarker.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// Host stays at its spawn and Client1 sprays. Client1 and Client2 wait inside the closed zone through fields 1-3, where the
// patrolling Boss never reaches; at the open Client2 walks out so Client1 sprays alone. Client2's contest pauses Client1's
// spray progress and Client1 resumes it until his graffiti lands. The owner then walks away, which pauses his guard
// (2026-11-07 user decision: it counts only while he stands alone in the zone); Client2 repaints alone, keeping it frozen,
// and walks out before his spray lands, losing his progress, while the guard stays paused. The owner walks back in and the
// guard resumes from its kept value; Client2 joins him (the guard freezes again) and the owner walks out under Client2,
// which resets the frozen guard to zero. Client2 walks out before repainting, the empty zone keeps the guard paused at zero,
// and the owner walks back in and stands alone in the zone for the whole guard to win. After the result the Host returns
// the same connections to a new Lobby generation and starts a second match.
enum class ELDCaptureProbeStage : uint8 { Join, Lobby, Starting, Match, Results, SecondLobby, SecondMatch, Done };
// #198: every match opens in the rest R0, whose private rest body is each contender's life 1; the R0 return retires it without a
// death and places the first field life, life 2.
constexpr uint32 LDCaptureProbeFirstFieldLife = 2;
// D26 (#79, #97): the Boss patrols a loop around the flagpole, so every point just outside the zone enters its acquire band
// at some moment of each lap, and a body standing still there is chased down. A body that has to wait outside the zone
// therefore stands on a ring this far outside the edge, opposite the Boss it observes, and follows it around in hops no
// wider than the hop angle; a hop's straight chord stays (radius + outside) * cos(hop / 2) from the centre, outside the
// zone. Fixture placement for this scenario only, never a product value.
constexpr double LDCaptureStationOutsideCm = 100.;
constexpr double LDCaptureStationHopDeg = 40.;
// A stationed body re-centres once the Boss has moved this far from straight across; the two clients keep this spacing
// to either side so they never contend for one ring point.
constexpr double LDCaptureStationSlackDeg = 20.;
constexpr double LDCaptureStationSpacingDeg = 25.;

// The public spray and guard sequence every role must observe in the first match.
enum class ELDCaptureProbeSeen : uint8
{
    None, Solo, Paused, Resumed, Sprayed, OwnerAway, Repainting, RepaintLeft, OwnerBack, GuardFrozen, GuardReset, ResetPaused,
    OwnerReturned
};

class FLDDemoCaptureProbe : public TSharedFromThis<FLDDemoCaptureProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output, Role, Error;
    double Deadline = 0, StageAt = 0, ReadyAt = 0;
    ELDCaptureProbeStage Stage = ELDCaptureProbeStage::Join;
    ELDCaptureProbeSeen Seen = ELDCaptureProbeSeen::None;
    int32 ContestStep = 0;
    bool Passed = true, Written = false, ReadySent = false, NamesChecked = false, StartSent = false, ReturnSent = false;
    bool SecondStartSent = false;
    bool PauseShown = false, Resumed = false, Sprayed = false, OwnerAway = false, RepaintPaused = false, RepaintLeft = false;
    bool OwnerBack = false, GuardFrozen = false, GuardReset = false, ResetPaused = false, OwnerReturned = false;
    uint32 FirstGeneration = 0, SecondGeneration = 0;
    FGuid CapturerId, ContesterId;
    FLDCombatBodyRef FirstBody;
    TSet<FGuid> FirstCampMembers;
    int64 FirstSoloSinceMs = -1, FirstPausedMs = 0, ResumedDueMs = 0, SprayedAtMs = -1;
    int64 AwayKeptMs = 0, RepaintSinceMs = 0, RepaintLeftAtMs = 0, ResumedGuardDueMs = 0;
    int64 FrozenKeptMs = 0, ResetAtMs = 0, ResetLeftAtMs = 0, RestartedGuardDueMs = 0;
    int32 MarkerObservations = 0, MarkerMismatches = 0;
    int64 SprayMs = 0, GuardMs = 0, FieldSegmentMs = 0, Rest0Ms = 0, ProtectionMs = 0;
    // The second match's first field life as first seen: its R0 return protection, checked once it has ended.
    int64 SecondProtectedUntilMs = -1;
    FVector Center = FVector::ZeroVector, Inside = FVector::ZeroVector;
    FName Surface;
    double RadiusCm = 0;
    // D26: the Boss's native patrol corridor, the creature id it is observed under, and this role's waiting ring state.
    TArray<FLDCompletePath> BossPatrol;
    uint8 BossCreature = 0;
    double HomeAngle = 0, StationAngle = 0, StationOffset = 0, GoalAngle = 0, BossAngle = 0, BossSeenAt = -1, NextStationOrderAt = 0;
    double NextRecoverAt = 0;
    bool bStationing = false, bLeftForSpray = false;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();

    // Stable ids let the runner require each role's checks, so a skipped stage cannot pass silently.
    bool Check(bool Value, const TCHAR* Id, const TCHAR* Name)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("id"), Id);
        Row->SetStringField(TEXT("name"), Name);
        Row->SetBoolField(TEXT("passed"), Value);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        Passed &= Value;
        if (!Value && Error.IsEmpty()) { Error = FString(Id) + TEXT(": ") + Name; }
        return Value;
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
        Error = TEXT("Could not persist capture probe marker: ") + Name;
        Passed = false;
        return false;
    }

    bool AllMarkers(const TCHAR* Suffix) const
    {
        for (const TCHAR* Name : {TEXT("Host"), TEXT("Client1"), TEXT("Client2")})
        {
            if (!FPaths::FileExists(FPaths::Combine(Output, FString(Name) + Suffix))) { return false; }
        }
        return true;
    }

    // The runner sends each role's chosen name as ?LDName=; the roster and the results must show exactly these,
    // never the online subsystem's generated nickname. One inner space checks that it survives the URL.
    static const TCHAR* ChosenName(const FString& Which)
    {
        if (Which == TEXT("Host")) { return TEXT("抢圈 房主"); }
        if (Which == TEXT("Client1")) { return TEXT("抢圈甲"); }
        return TEXT("抢圈乙");
    }

    static bool NamedOnce(const TArray<FLDLobbyMember>& Members, const TCHAR* Name)
    {
        return Members.FilterByPredicate([Name](const FLDLobbyMember& Row)
        { return Row.Kind == ELDContenderKind::Human && Row.Name.Equals(Name, ESearchCase::CaseSensitive); }).Num() == 1;
    }

    bool Finish()
    {
        auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), Passed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetStringField(TEXT("scope"), TEXT("DEV-only normal start through providers, real owned spray, contest pause and resume, graffiti, an owner walking away that pauses his guard, a repaint that keeps the guard frozen and is abandoned (progress lost, guard still paused), the owner's return resuming it, a reset under a challenger, guard win with the owner alone in the zone as the unique result, same-world return to a new Lobby generation and a second normal match that starts clean. Not 3+3, AI, controls or an owner's death."));
        Report->SetObjectField(TEXT("summary"), Summary);
        Report->SetArrayField(TEXT("checks"), Checks);
        Written = Save(Report, Role + TEXT(".json"));
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_CAPTURE_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
        if (!Passed || !Written) { FPlatformMisc::RequestExitWithStatus(false, 2); return false; }
        return true;
    }

    // A ready raced by a roster change is refused as stale; resend until the published roster shows it.
    bool EnsureReady(ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        const FLDLobbyMember* Self = Lobby.Members.FindByPredicate([&PC](const FLDLobbyMember& Row)
        { return Row.ContenderId == PC.OwnerState.ContenderId; });
        if (Self && Self->Ready) { return true; }
        if (!ReadySent || FPlatformTime::Seconds() - ReadyAt >= 3.)
        {
            PC.SetDemoReady(true);
            ReadySent = true;
            ReadyAt = FPlatformTime::Seconds();
        }
        return false;
    }

    static bool AllHumansReady(const FLDLobbyView& Lobby)
    {
        return Lobby.Members.Num() == 3 && !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Row)
        { return Row.Kind == ELDContenderKind::Human && !Row.Ready; });
    }

    static void CampMembers(const ALDGameMode& Mode, TSet<FGuid>& Out)
    {
        Out.Reset();
        const ALDCampDirector* Camps = Mode.Camps.Get();
        const FLDCampMatchState* State = Camps ? &Camps->GetState() : nullptr;
        if (!State || !State->IsActive()) { return; }
        for (int32 Camp = 0; Camp < State->NumCamps(); ++Camp)
        {
            for (const FLDCampMemberState& Member : State->FindCamp(Camp)->Members) { Out.Add(Member.EntityId); }
        }
    }

    void Observe(const ALDGameState& Public)
    {
        const FLDPublicMatchView& View = Public.PublicMatch;
        if (View.Phase != ELDMatchPhase::CaptureOpen) { return; }
        if (View.Generation == FirstGeneration) { Follow(View); }
        // The visible boundary and the flagpole graffiti must show exactly the public state this role has accepted.
        const ALDCaptureMarker* Marker = Public.GetCaptureMarker();
        ++MarkerObservations;
        MarkerMismatches += !Marker || Marker->GetGeneration() != View.Generation || Marker->GetShownState() != View.CircleState
            || Marker->IsGraffitiShown() != View.GraffitiOwner.IsValid()
            || (View.GraffitiOwner.IsValid() && Marker->GetGraffitiSlot() != View.GraffitiPresentation) ? 1 : 0;
    }

    // Advances through the expected public sequence. Every state lasts at least 1 s by choreography, and
    // ServerNowMs is the authority time of the publication that changed the zone.
    void Follow(const FLDPublicMatchView& View)
    {
        const bool CapturerSolo = View.CircleState == ELDCircleState::Solo && View.Occupier == CapturerId;
        const bool CapturerPaused = View.CircleState == ELDCircleState::Contested && View.Occupier == CapturerId;
        const bool FullSpray = View.DueAtMs - View.SoloSinceMs == SprayMs;
        switch (Seen)
        {
        case ELDCaptureProbeSeen::None:
            if (!CapturerSolo || View.GraffitiOwner.IsValid()) { return; }
            FirstSoloSinceMs = View.SoloSinceMs;
            Seen = ELDCaptureProbeSeen::Solo;
            return;
        case ELDCaptureProbeSeen::Solo:
            if (!CapturerPaused) { return; }
            FirstPausedMs = View.PausedProgressMs;
            PauseShown = FirstPausedMs > 0 && FirstPausedMs < SprayMs && FirstPausedMs <= View.ServerNowMs - FirstSoloSinceMs
                && !View.SoloSinceMs && !View.DueAtMs && !View.GraffitiOwner.IsValid();
            Seen = ELDCaptureProbeSeen::Paused;
            return;
        case ELDCaptureProbeSeen::Paused:
            if (!CapturerSolo) { return; }
            // A restart would publish a spray younger than one step; the resumed one already holds the paused progress.
            Resumed = View.ServerNowMs - View.SoloSinceMs >= FirstPausedMs && View.SoloSinceMs > FirstSoloSinceMs && FullSpray;
            ResumedDueMs = View.DueAtMs;
            Seen = ELDCaptureProbeSeen::Resumed;
            return;
        case ELDCaptureProbeSeen::Resumed:
            if (View.GraffitiOwner != CapturerId) { return; }
            // A fresh guard starts at the spray instant, so its win instant is exactly one guard later.
            SprayedAtMs = View.GuardDueAtMs - GuardMs;
            Sprayed = View.CircleState == ELDCircleState::Guarded && SprayedAtMs == ResumedDueMs && !View.Occupier.IsValid()
                && View.GuardKeptMs == 0;
            Seen = ELDCaptureProbeSeen::Sprayed;
            return;
        case ELDCaptureProbeSeen::Sprayed:
            if (View.CircleState != ELDCircleState::Empty || View.GraffitiOwner != CapturerId) { return; }
            // The owner away from the empty zone pauses his guard at the progress it had when he left.
            AwayKeptMs = View.GuardKeptMs;
            OwnerAway = !View.GuardDueAtMs && AwayKeptMs > 0 && AwayKeptMs < GuardMs && SprayedAtMs + AwayKeptMs <= View.ServerNowMs;
            Seen = ELDCaptureProbeSeen::OwnerAway;
            return;
        case ELDCaptureProbeSeen::OwnerAway:
            if (View.CircleState != ELDCircleState::Solo || View.Occupier != ContesterId) { return; }
            RepaintSinceMs = View.SoloSinceMs;
            // Nothing accrued while the zone was empty; the sole challenger keeps the kept value frozen.
            RepaintPaused = View.GraffitiOwner == CapturerId && !View.GuardDueAtMs && View.GuardKeptMs == AwayKeptMs
                && RepaintSinceMs > SprayedAtMs + AwayKeptMs && FullSpray;
            Seen = ELDCaptureProbeSeen::Repainting;
            return;
        case ELDCaptureProbeSeen::Repainting:
            if (View.CircleState != ELDCircleState::Empty || View.GraffitiOwner != CapturerId) { return; }
            // The challenger walked out before his spray landed; with the owner still away the guard stays paused.
            RepaintLeftAtMs = View.ServerNowMs;
            RepaintLeft = !View.GuardDueAtMs && View.GuardKeptMs == AwayKeptMs && RepaintLeftAtMs > RepaintSinceMs;
            Seen = ELDCaptureProbeSeen::RepaintLeft;
            return;
        case ELDCaptureProbeSeen::RepaintLeft:
        {
            if (View.CircleState != ELDCircleState::Guarded || View.GraffitiOwner != CapturerId) { return; }
            ResumedGuardDueMs = View.GuardDueAtMs;
            // The owner back alone resumed the guard from its kept value at his own entry, after the challenger left.
            const int64 ResumeAtMs = ResumedGuardDueMs - GuardMs + AwayKeptMs;
            OwnerBack = !View.GuardKeptMs && ResumedGuardDueMs > 0 && ResumeAtMs > RepaintLeftAtMs && ResumeAtMs <= View.ServerNowMs;
            Seen = ELDCaptureProbeSeen::OwnerBack;
            return;
        }
        case ELDCaptureProbeSeen::OwnerBack:
        {
            if (View.CircleState != ELDCircleState::Contested || View.GraffitiOwner != CapturerId) { return; }
            FrozenKeptMs = View.GuardKeptMs;
            // A challenger beside the owner froze the running guard at his entry; nobody holds spray progress.
            const int64 FrozenAtMs = ResumedGuardDueMs - GuardMs + FrozenKeptMs;
            GuardFrozen = !View.GuardDueAtMs && !View.Occupier.IsValid() && FrozenKeptMs > AwayKeptMs && FrozenKeptMs < GuardMs
                && FrozenAtMs <= View.ServerNowMs;
            Seen = ELDCaptureProbeSeen::GuardFrozen;
            return;
        }
        case ELDCaptureProbeSeen::GuardFrozen:
            if (View.CircleState != ELDCircleState::Solo || View.Occupier != ContesterId) { return; }
            // The owner walked out under the challenger: the graffiti stays, the frozen guard is back at zero.
            ResetAtMs = View.SoloSinceMs;
            GuardReset = View.GraffitiOwner == CapturerId && !View.GuardDueAtMs && !View.GuardKeptMs && FullSpray
                && ResetAtMs <= View.ServerNowMs;
            Seen = ELDCaptureProbeSeen::GuardReset;
            return;
        case ELDCaptureProbeSeen::GuardReset:
            if (View.CircleState != ELDCircleState::Empty || View.GraffitiOwner != CapturerId) { return; }
            // The challenger walked out before his repaint landed; the owner is away, so the reset guard stays at zero.
            ResetLeftAtMs = View.ServerNowMs;
            ResetPaused = !View.GuardDueAtMs && !View.GuardKeptMs && ResetLeftAtMs > ResetAtMs;
            Seen = ELDCaptureProbeSeen::ResetPaused;
            return;
        case ELDCaptureProbeSeen::ResetPaused:
        {
            if (View.CircleState != ELDCircleState::Guarded || View.GraffitiOwner != CapturerId) { return; }
            RestartedGuardDueMs = View.GuardDueAtMs;
            // The guard counts from zero only once the owner stands alone in the zone again.
            const int64 RestartAtMs = RestartedGuardDueMs - GuardMs;
            OwnerReturned = !View.GuardKeptMs && RestartedGuardDueMs > 0 && RestartAtMs > ResetLeftAtMs
                && RestartAtMs <= View.ServerNowMs;
            Seen = ELDCaptureProbeSeen::OwnerReturned;
            return;
        }
        default:
            return;
        }
    }

    bool Seeing(ELDCaptureProbeSeen Value) const { return uint8(Seen) >= uint8(Value); }

    // Starts the role's next move once its own public view reached Value and, after that, Delay seconds passed.
    bool After(ELDCaptureProbeSeen Value, int32 WaitStep, double Delay)
    {
        if (ContestStep == WaitStep && Seeing(Value))
        {
            StageAt = FPlatformTime::Seconds();
            ContestStep = WaitStep + 1;
        }
        return ContestStep == WaitStep + 1 && FPlatformTime::Seconds() - StageAt >= Delay;
    }

    static int32 MarkerActors(UWorld& World)
    {
        int32 Count = 0;
        for (TActorIterator<ALDCaptureMarker> It(&World); It; ++It) { ++Count; }
        return Count;
    }

    // One flat, collision-free ring for this generation at the approved radius around the registered anchor.
    bool FreshMarker(const ALDGameState& Public, UWorld& World, uint32 Generation) const
    {
        const ALDCaptureMarker* Marker = Public.GetCaptureMarker();
        return Marker && MarkerActors(World) == 1 && Marker->GetGeneration() == Generation && Marker->IsPresentationOnly()
            && FVector::Dist2D(Marker->GetCenter(), Center) <= 1. && FMath::IsNearlyEqual(Marker->GetRadius(), RadiusCm, 1e-6)
            && Marker->GetSegmentCount() > 0 && Marker->GetShownState() == ELDCircleState::NotOpen;
    }

    double AngleOf(const FVector& Point) const { return FMath::Atan2(Point.Y - Center.Y, Point.X - Center.X); }

    FVector RingPoint(double Angle) const
    { return Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.) * (RadiusCm + LDCaptureStationOutsideCm); }

    // Planar distance from Point to the Boss's native patrol corridor.
    double CorridorDistance(const FVector& Point) const
    {
        double Best = DBL_MAX;
        const FVector Flat(Point.X, Point.Y, 0.);
        for (const FLDCompletePath& Path : BossPatrol)
        {
            for (int32 I = 1; I < Path.Points.Num(); ++I)
            {
                Best = FMath::Min(Best, double(FMath::PointDistToSegment(Flat, FVector(Path.Points[I - 1].X, Path.Points[I - 1].Y, 0.),
                    FVector(Path.Points[I].X, Path.Points[I].Y, 0.))));
            }
        }
        return Best;
    }

    // The Boss as this owner's own authorized view shows it; unseen (out of sight, or down) keeps the last sighting.
    void ObserveBoss(const ALDPlayerController& PC)
    {
        const ALDWorldViewRelay* Relay = PC.LocalView();
        if (!Relay) { return; }
        const FLDObservedFrameRead Read = Relay->ReadObservedFrame();
        if (Read.Status != ELDObservedViewStatus::Current || !Read.Frame) { return; }
        for (const FLDObservedUnitView& Unit : Read.Frame->Units)
        {
            if (Unit.Kind == ELDObservedUnitKind::CampMember && Unit.CreatureId == BossCreature)
            {
                BossAngle = AngleOf(FVector(Unit.FeetCm));
                BossSeenAt = FPlatformTime::Seconds();
                return;
            }
        }
    }

    bool BossFresh() const { return BossSeenAt >= 0 && FPlatformTime::Seconds() - BossSeenAt <= 1.; }

    // Leaves the zone (or stays out of it) for the ring point straight across from the observed Boss.
    void BeginStation(ULDNavigationComponent& Nav)
    {
        StationAngle = (BossFresh() ? BossAngle + UE_DOUBLE_PI : HomeAngle) + StationOffset;
        GoalAngle = StationAngle;
        bStationing = true;
        NextStationOrderAt = FPlatformTime::Seconds() + .5;
        Nav.NavigateGround(RingPoint(StationAngle), Surface);
    }

    void WalkIn(ULDNavigationComponent& Nav, const FVector& Goal)
    {
        bStationing = false;
        Nav.NavigateGround(Goal, Surface);
    }

    // A rest return may set a body down anywhere within its search radius of the foot it left (and a death would respawn
    // it far away): it walks back to its waiting point inside the zone, at most one order every two seconds.
    void ReturnTo(const ALDPlayerController& PC, ULDNavigationComponent& Nav, const FVector& Goal)
    {
        const APawn* Body = PC.GetPawn();
        const double Now = FPlatformTime::Seconds();
        if (!Body || PC.OwnerState.LifePhase != ELDLifePhase::Alive || !PC.CanWorldCommand() || Nav.HasMoveOrder()
            || Now < NextRecoverAt || FVector::Dist2D(Body->GetActorLocation(), Goal) <= 100.) { return; }
        NextRecoverAt = Now + 2.;
        Nav.NavigateGround(Goal, Surface);
    }

    // Keeps a waiting body on the ring opposite the Boss: from inside the zone straight to its ring point, from just
    // outside radially onto the ring, then along the ring in bounded hops once the Boss has moved on. A walk still heading
    // more than a quarter turn away from where the Boss now leaves room is replaced.
    void KeepStation(const ALDPlayerController& PC, ULDNavigationComponent& Nav)
    {
        const APawn* Body = PC.GetPawn();
        if (!bStationing || !Body || PC.OwnerState.LifePhase != ELDLifePhase::Alive || !PC.CanWorldCommand()) { return; }
        if (BossFresh()) { StationAngle = BossAngle + UE_DOUBLE_PI + StationOffset; }
        const double Now = FPlatformTime::Seconds();
        if (Now < NextStationOrderAt || (Nav.HasMoveOrder()
            && FMath::Abs(FMath::FindDeltaAngleRadians(GoalAngle, StationAngle)) <= UE_DOUBLE_HALF_PI)) { return; }
        const FVector Foot = Body->GetActorLocation();
        const double Out = FVector::Dist2D(Foot, Center), Along = AngleOf(Foot);
        const double Behind = FMath::FindDeltaAngleRadians(Along, StationAngle);
        if (Out <= RadiusCm) { GoalAngle = StationAngle; }
        else if (Out < RadiusCm + LDCaptureStationOutsideCm / 2) { GoalAngle = Along; }
        else if (FMath::Abs(Behind) > FMath::DegreesToRadians(LDCaptureStationSlackDeg))
        {
            const double Hop = FMath::DegreesToRadians(LDCaptureStationHopDeg);
            GoalAngle = Along + FMath::Clamp(Behind, -Hop, Hop);
        }
        else { return; }
        NextStationOrderAt = Now + .5;
        Nav.NavigateGround(RingPoint(GoalAngle), Surface);
    }

    bool Tick();
    bool CheckSecondMatch(ALDPlayerController& PC, ALDGameMode* Mode, const FLDLobbyView& Lobby, const FLDPublicMatchView& View,
        const FLDProgressionView& Progress);
};

bool FLDDemoCaptureProbe::Tick()
{
    if (Written)
    {
        if (Role != TEXT("Host") && FPaths::FileExists(FPaths::Combine(Output, TEXT("FinishClients"))))
        { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
        if (Role == TEXT("Host") && FPaths::FileExists(FPaths::Combine(Output, TEXT("Finish"))))
        { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
        return true;
    }
    if (FPlatformTime::Seconds() >= Deadline)
    {
        Check(false, TEXT("T01"), TEXT("Capture scenario completed within its bounded deadline"));
        Error = FString::Printf(TEXT("Capture probe timed out at stage %u, contest step %d, public sequence %u."),
            uint8(Stage), ContestStep, uint8(Seen));
        return Finish();
    }
    auto* PC = Controller.Get();
    auto* World = PC ? PC->GetWorld() : nullptr;
    auto* Public = World ? World->GetGameState<ALDGameState>() : nullptr;
    auto* Registry = World ? World->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!PC || !World || !Public || !Registry || !Registry->Enabled()) { return true; }
    if (!Content)
    {
        const FLDMapBinding* Capture = nullptr;
        if (LDDemoEntry::LoadRuntimeContent(Content, Error))
        { Capture = Registry->Resolve(FName(*Content->Get().World.CaptureId), Registry->Generation); }
        const auto& Radius = Content ? Content->Get().Parameters.CaptureRadiusCm : FLDDemoRational();
        if (!Capture || Radius.Denominator <= 0)
        {
            Check(false, TEXT("P01"), TEXT("Approved content and the registered capture anchor resolve"));
            return Finish();
        }
        Center = Capture->Position;
        Surface = Capture->Surface;
        RadiusCm = double(Radius.Numerator) / double(Radius.Denominator);
        SprayMs = Content->Get().Parameters.CaptureSprayMs;
        GuardMs = Content->Get().Parameters.CaptureGuardMs;
        FieldSegmentMs = Content->Get().Parameters.FieldSegmentMs;
        // No sign-up ever happens here, so R0 skips its PK: the build step, then the return countdown.
        Rest0Ms = Content->Get().Parameters.RestOpeningBuildMs + Content->Get().Parameters.RestReturnMs;
        ProtectionMs = Content->Get().Parameters.ProtectionMs;
        // D26: the Boss's public native patrol corridor fixes where bodies may wait. The centre, where the owner sprays and
        // guards, and the contester's inside point must both lie outside the Boss's acquire band, or this choreography
        // cannot exist on the approved map. The inside point keeps two body radii and the contact tolerance from the
        // centre, so neither client ever pushes the other off its point, in the direction farthest from the corridor.
        const FLDDemoContentDefinitions& Defs = Content->Get();
        const FLDDemoMonsterDefinition* BossBody = Defs.Monsters.FindByPredicate([&Defs](const FLDDemoMonsterDefinition& Row)
        { return Row.Tier == ELDDemoCampTier::Boss && Row.Id == Defs.Boss.Id; });
        FString Why;
        FCollisionShape Capsule;
        bool bFixture = BossBody && Defs.Boss.AggroRadiusCm.Denominator > 0
            && ULDMapRegistry::ContentCapsule(*Content, Capsule, Why)
            && LDScenarioWalk::PatrolPaths(*Registry, *Content, BossPatrol, Why)
            && !BossPatrol.ContainsByPredicate([](const FLDCompletePath& Path) { return Path.Points.Num() < 2; });
        if (bFixture)
        {
            BossCreature = BossBody->NativeId;
            const double Band = double(Defs.Boss.AggroRadiusCm.Numerator) / double(Defs.Boss.AggroRadiusCm.Denominator)
                + LDPathSteering::WaypointToleranceCm;
            const double Apart = 2 * (Capsule.GetCapsuleRadius() + LDPathSteering::WaypointToleranceCm);
            double Clearest = -1;
            for (int32 Step = 0; Step < 16; ++Step)
            {
                const double Angle = Step * UE_DOUBLE_TWO_PI / 16;
                const FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.) * Apart;
                const double Clear = CorridorDistance(Point);
                if (Clear > Clearest) { Clearest = Clear; Inside = Point; }
            }
            bFixture = Apart > 0 && Apart + Capsule.GetCapsuleRadius() < RadiusCm
                && CorridorDistance(Center) > Band && Clearest > Band
                && (RadiusCm + LDCaptureStationOutsideCm) * FMath::Cos(FMath::DegreesToRadians(LDCaptureStationHopDeg) / 2)
                    > RadiusCm + LDPathSteering::WaypointToleranceCm;
        }
        if (!bFixture)
        {
            Check(false, TEXT("P01"), TEXT("The flagpole centre and the contester's inside point lie outside the Boss's acquire band around its native patrol corridor"));
            if (!Why.IsEmpty()) { Error += TEXT(" ") + Why; }
            return Finish();
        }
        StationOffset = FMath::DegreesToRadians(Role == TEXT("Client1") ? LDCaptureStationSpacingDeg : -LDCaptureStationSpacingDeg);
    }
    const FLDLobbyView& Lobby = Public->DemoLobby;
    const FLDPublicMatchView& View = Public->PublicMatch;
    Observe(*Public);
    auto* Nav = PC->Navigation.Get();
    // Only the Listen Host has the authority; clients read their own owner views only.
    ALDGameMode* Mode = World->GetAuthGameMode<ALDGameMode>();
    switch (Stage)
    {
    case ELDCaptureProbeStage::Join:
        if (!PC->IsLocalController() || !Lobby.Generation) { return true; }
        Check(Role == TEXT("Host") ? World->GetNetMode() == NM_ListenServer
            : World->GetNetMode() == NM_Client && PC->GetNetConnection() && !PC->HasAuthority(),
            TEXT("C01"), TEXT("Each role is a real Listen Host or owning client connection"));
        FirstGeneration = Lobby.Generation;
        if (!Mark(Role + TEXT("Joined.json"))) { return Finish(); }
        Stage = ELDCaptureProbeStage::Lobby;
        return true;
    case ELDCaptureProbeStage::Lobby:
    {
        if (!AllMarkers(TEXT("Joined.json")) || Lobby.Members.Num() != 3) { return true; }
        if (!NamesChecked)
        {
            const FLDLobbyMember* Self = Lobby.Members.FindByPredicate([PC](const FLDLobbyMember& Row)
            { return Row.ContenderId == PC->OwnerState.ContenderId; });
            Check(Self && Self->Name.Equals(ChosenName(Role), ESearchCase::CaseSensitive) && NamedOnce(Lobby.Members, ChosenName(TEXT("Host")))
                && NamedOnce(Lobby.Members, ChosenName(TEXT("Client1"))) && NamedOnce(Lobby.Members, ChosenName(TEXT("Client2"))),
                TEXT("C16"), TEXT("The roster shows each role's frontend name exactly, never an online subsystem nickname"));
            NamesChecked = true;
            if (!Passed) { return Finish(); }
        }
        if (!EnsureReady(*PC, Lobby)) { return true; }
        if (Role != TEXT("Host")) { Stage = ELDCaptureProbeStage::Starting; return true; }
        if (!AllHumansReady(Lobby) || StartSent) { return true; }
        if (!Check(Lobby.CanStart && Lobby.StartBlocker.IsEmpty(), TEXT("C02"), TEXT("Every provider is wired: the ready room may start normally")))
        {
            Error = TEXT("Normal start is still blocked: ") + Lobby.StartBlocker;
            return Finish();
        }
        PC->StartDemoMatch();
        StartSent = true;
        Stage = ELDCaptureProbeStage::Starting;
        return true;
    }
    case ELDCaptureProbeStage::Starting:
    {
        const auto* Body = Cast<ALDCharacter>(PC->GetPawn());
        // The match opens in the rest R0 (life 1 in a private rest space); the R0 return places the first field life. The
        // public phase may reach Development a frame before this owner's projection leaves its rest life.
        if (View.Phase != ELDMatchPhase::Development || View.Generation != Lobby.Generation || !Body
            || PC->OwnerState.LifePhase != ELDLifePhase::Alive || PC->OwnerState.Life < LDCaptureProbeFirstFieldLife
            || !PC->CanWorldCommand()) { return true; }
        // A freshly spawned body may still be settling; a ground order sent before it stands fails its local plan.
        if (!Body->GetCharacterMovement() || !Body->GetCharacterMovement()->IsMovingOnGround()) { return true; }
        const FLDLobbyMember* Capturer = Lobby.Members.FindByPredicate([](const FLDLobbyMember& Row)
        { return Row.Name.Equals(ChosenName(TEXT("Client1")), ESearchCase::CaseSensitive); });
        CapturerId = Capturer ? Capturer->ContenderId : FGuid();
        const FLDLobbyMember* Contester = Lobby.Members.FindByPredicate([](const FLDLobbyMember& Row)
        { return Row.Name.Equals(ChosenName(TEXT("Client2")), ESearchCase::CaseSensitive); });
        ContesterId = Contester ? Contester->ContenderId : FGuid();
        Check(Capturer && CapturerId.IsValid() && Contester && ContesterId.IsValid() && ContesterId != CapturerId
            && (Role != TEXT("Client1") || CapturerId == PC->OwnerState.ContenderId)
            && (Role != TEXT("Client2") || ContesterId == PC->OwnerState.ContenderId),
            TEXT("C03"), TEXT("The capturing and the contesting contenders are on the locked roster"));
        // #198: the flagpole exists only in the final field, so it is not scheduled (MAX) while field 1 runs from T=0 right
        // after the R0 return.
        const FLDTimelineView& Line = View.Timeline;
        Check(View.Generation == FirstGeneration && View.OpenAtMs == MAX_int64 && Line.Stage == ELDMatchStage::Field
            && Line.FieldSegment == 0 && Line.FieldBaseMs == 0 && Line.FieldEndT == FieldSegmentMs
            && Line.FieldStartedAtMs == View.StartedAtMs + Rest0Ms
            && View.CircleState == ELDCircleState::NotOpen && !View.Occupier.IsValid() && !View.GraffitiOwner.IsValid(),
            TEXT("C04"), TEXT("Normal start runs the opening rest R0, then field 1 from T=0 with a closed, bare and unscheduled flagpole"));
        FVector Direction = Body->GetActorLocation() - Center;
        Direction.Z = 0;
        Direction = Direction.GetSafeNormal();
        Check(!Direction.IsNearlyZero(), TEXT("C05"), TEXT("Spawned body is outside the circle centre"));
        Check(FreshMarker(*Public, *World, View.Generation), TEXT("C13"),
            TEXT("Every role shows one flat flagpole boundary at the approved radius around the anchor, without collision"));
        // A body that waits outside the zone with the Boss unseen (down) takes the side it came from.
        HomeAngle = FMath::Atan2(Direction.Y, Direction.X);
        // The second match proves freshness against these first-match identities.
        FirstBody = PC->OwnerState.BodyRef;
        if (Mode) { CampMembers(*Mode, FirstCampMembers); }
        if (!Passed) { return Finish(); }
        // Both clients wait inside the closed zone, clear of the Boss, until the final field opens it; each rest returns a
        // body near the field foot it left. The walk in ends long before the Boss first appears.
        if (Role == TEXT("Client1")) { Nav->NavigateGround(Center, Surface); }
        else if (Role == TEXT("Client2")) { Nav->NavigateGround(Inside, Surface); }
        Stage = ELDCaptureProbeStage::Match;
        return true;
    }
    case ELDCaptureProbeStage::Match:
    {
        // #198: the flagpole opens only when the final field starts, after the three timed field segments and the rests
        // R1..R3. This normal-start scenario runs without the DEV growth fixture, so it has no DEV OpenCapture shortcut and
        // honestly waits for that boundary (the runner's timeout must cover it). Each rest moves every body to its private
        // rest space and back near the field foot it left, so both clients return inside the zone, each with the shared
        // return protection. D26: whenever a client has to be outside the zone it waits on the ring opposite the Boss.
        int64 NowMs = 0;
        PC->ReadDemoAuthorityTimeMs(NowMs);
        if (Role != TEXT("Host") && View.Phase == ELDMatchPhase::Development && View.Timeline.Stage == ELDMatchStage::Field)
        { ReturnTo(*PC, *Nav, Role == TEXT("Client1") ? Center : Inside); }
        if (Role != TEXT("Host") && View.Phase == ELDMatchPhase::CaptureOpen) { ObserveBoss(*PC); }
        if (Role == TEXT("Client2") && View.Phase == ELDMatchPhase::CaptureOpen)
        {
            // Leave the zone at the open so the owner sprays alone; contest early in the short spray, walk out to let it
            // resume, then repaint alone and give up before it lands.
            if (ContestStep == 0 && !bLeftForSpray) { BeginStation(*Nav); bLeftForSpray = true; }
            else if (ContestStep == 0 && Seeing(ELDCaptureProbeSeen::Solo) && NowMs - FirstSoloSinceMs >= 300)
            {
                WalkIn(*Nav, Inside);
                ContestStep = 1;
            }
            else if (After(ELDCaptureProbeSeen::Paused, 1, 1.5)) { BeginStation(*Nav); ContestStep = 3; }
            else if (After(ELDCaptureProbeSeen::OwnerAway, 3, 1.)) { WalkIn(*Nav, Inside); ContestStep = 5; }
            else if (After(ELDCaptureProbeSeen::Repainting, 5, 1.)) { BeginStation(*Nav); ContestStep = 7; }
            // Join the owner once he is back in the zone, then give up the repaint the owner's exit hands over.
            else if (After(ELDCaptureProbeSeen::OwnerBack, 7, 1.)) { WalkIn(*Nav, Inside); ContestStep = 9; }
            else if (After(ELDCaptureProbeSeen::GuardReset, 9, 1.)) { BeginStation(*Nav); ContestStep = 11; }
            KeepStation(*PC, *Nav);
        }
        else if (Role == TEXT("Client1") && View.Phase == ELDMatchPhase::CaptureOpen)
        {
            // The owner walks away once his graffiti stands, which pauses his guard. He comes back after the abandoned
            // repaint, resuming it, and walks out again under the challenger, which resets his frozen guard. Once the
            // challenger has left he stands alone in the zone for the whole guard. Until he first sprays alone he keeps to
            // the centre, should the R3 return have set him down away from it.
            if (ContestStep == 0 && !Seeing(ELDCaptureProbeSeen::Solo)) { ReturnTo(*PC, *Nav, Center); }
            else if (After(ELDCaptureProbeSeen::Sprayed, 0, 1.5)) { BeginStation(*Nav); ContestStep = 2; }
            else if (After(ELDCaptureProbeSeen::RepaintLeft, 2, 1.)) { WalkIn(*Nav, Center); ContestStep = 4; }
            else if (After(ELDCaptureProbeSeen::GuardFrozen, 4, 1.5)) { BeginStation(*Nav); ContestStep = 6; }
            else if (After(ELDCaptureProbeSeen::ResetPaused, 6, 1.)) { WalkIn(*Nav, Center); ContestStep = 8; }
            KeepStation(*PC, *Nav);
        }
        if (View.Phase != ELDMatchPhase::Results) { return true; }
        const FLDResultView& Result = Public->MatchResult;
        if (Result.Generation != View.Generation || Result.ResultId != View.ResultId) { return true; }
        Check(Result.Reason == ELDMatchEndReason::GuardSucceeded && Result.Winner == CapturerId,
            TEXT("C06"), TEXT("A guarded graffiti locks its owner as the unique winner, ended by a guard success"));
        Check(Seeing(ELDCaptureProbeSeen::OwnerBack) && PauseShown && Resumed && Sprayed,
            TEXT("C07"), TEXT("A real contest paused the sprayer's progress; he resumed it as the sole sprayer and his graffiti landed"));
        Check(Seeing(ELDCaptureProbeSeen::OwnerBack) && OwnerAway && RepaintPaused && RepaintLeft && OwnerBack,
            TEXT("C18"), TEXT("The owner leaving the empty zone paused his guard at its value; a challenger entering alone kept it frozen there and, abandoning his repaint, lost its progress while the guard stayed paused; the owner back alone resumed it from the kept value"));
        Check(Seeing(ELDCaptureProbeSeen::OwnerReturned) && GuardFrozen && GuardReset && ResetPaused && OwnerReturned,
            TEXT("C19"), TEXT("A challenger beside the returned owner froze the guard; the owner walking out under him reset it to zero with the graffiti kept; the empty zone held it at zero and it counted again only once the owner stood alone in the zone"));
        Check(Result.EndedAtMs == RestartedGuardDueMs && View.GuardDueAtMs == RestartedGuardDueMs && View.GraffitiOwner == CapturerId
            && View.CircleState == ELDCircleState::Guarded && Result.EndedAtMs - SprayedAtMs > GuardMs
            && Result.EndedAtMs - ResetLeftAtMs > GuardMs && ResumedDueMs - SprayedAtMs == 0
            && Result.DurationMs == Result.EndedAtMs - Result.StartedAtMs,
            TEXT("C08"), TEXT("The spray took its approved duration and the owner won after standing alone in the zone for the whole approved guard after the reset, timed by the server"));
        Check(Result.Rows.Num() == 3 && !Result.Rows.ContainsByPredicate([](const FLDResultRow& Row)
            { return Row.FinalLevel < 1 || Row.bLeft; }),
            TEXT("C09"), TEXT("The frozen result lists every contender with a committed final level"));
        bool bRowNames = Result.Rows.Num() == 3;
        for (const FLDResultRow& Row : Result.Rows)
        {
            const FLDLobbyMember* Listed = Lobby.Members.FindByPredicate([&Row](const FLDLobbyMember& Member)
            { return Member.ContenderId == Row.ContenderId; });
            bRowNames &= Listed && Row.Name.Equals(Listed->Name, ESearchCase::CaseSensitive);
        }
        for (const TCHAR* Which : {TEXT("Host"), TEXT("Client1"), TEXT("Client2")})
        {
            bRowNames &= Result.Rows.ContainsByPredicate([Which](const FLDResultRow& Row)
            { return Row.Name.Equals(ChosenName(Which), ESearchCase::CaseSensitive); });
        }
        Check(bRowNames, TEXT("C17"), TEXT("The frozen result rows carry each contender's frontend name, the same as the roster"));
        Summary->SetStringField(TEXT("name"), ChosenName(Role));
        const ALDCaptureMarker* Marker = Public->GetCaptureMarker();
        Check(Marker && Marker->GetGeneration() == View.Generation && Marker->GetShownState() == View.CircleState
            && MarkerObservations > 0 && MarkerMismatches == 0,
            TEXT("C14"), TEXT("The boundary and the flagpole graffiti mirrored every observed public state through the locked result"));
        Summary->SetNumberField(TEXT("marker_observations"), MarkerObservations);
        Summary->SetNumberField(TEXT("first_solo_since_ms"), double(FirstSoloSinceMs));
        Summary->SetNumberField(TEXT("first_paused_ms"), double(FirstPausedMs));
        Summary->SetNumberField(TEXT("sprayed_at_ms"), double(SprayedAtMs));
        Summary->SetNumberField(TEXT("away_kept_guard_ms"), double(AwayKeptMs));
        Summary->SetNumberField(TEXT("repaint_since_ms"), double(RepaintSinceMs));
        Summary->SetNumberField(TEXT("repaint_left_at_ms"), double(RepaintLeftAtMs));
        Summary->SetNumberField(TEXT("resumed_guard_due_ms"), double(ResumedGuardDueMs));
        Summary->SetNumberField(TEXT("frozen_guard_kept_ms"), double(FrozenKeptMs));
        Summary->SetNumberField(TEXT("guard_reset_at_ms"), double(ResetAtMs));
        Summary->SetNumberField(TEXT("reset_left_at_ms"), double(ResetLeftAtMs));
        Summary->SetNumberField(TEXT("restarted_guard_due_ms"), double(RestartedGuardDueMs));
        Summary->SetNumberField(TEXT("ended_at_ms"), double(Result.EndedAtMs));
        Summary->SetNumberField(TEXT("epoch"), double(View.CaptureEpoch));
        Summary->SetStringField(TEXT("winner"), Result.Winner.ToString(EGuidFormats::Digits));
        if (Mode)
        {
            Check(!Mode->IsGameplayWriteOpen(Mode->GetMatchContext()) && Mode->ReadPublicMatch().ResultId == Result.ResultId,
                TEXT("C10"), TEXT("The authority gate is closed by the same result it published"));
        }
        if (!Passed || !Mark(Role + TEXT("Results.json"))) { return Finish(); }
        Stage = ELDCaptureProbeStage::Results;
        return true;
    }
    case ELDCaptureProbeStage::Results:
        if (Role == TEXT("Host") && !ReturnSent && AllMarkers(TEXT("Results.json")))
        {
            PC->ReturnDemoToLobby();
            ReturnSent = true;
        }
        if (Lobby.Generation == FirstGeneration || Lobby.Phase != ELDMatchPhase::Lobby
            || View.Generation != Lobby.Generation || View.Phase != ELDMatchPhase::Lobby || PC->GetPawn()) { return true; }
        Check(Lobby.Members.Num() == 3 && !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Row) { return Row.Ready; }),
            TEXT("C11"), TEXT("The same connections return to a new, unready Lobby generation"));
        Check(View.CircleState == ELDCircleState::NotOpen && !View.ResultId.IsValid() && !View.StartedAtMs && !View.GraffitiOwner.IsValid(),
            TEXT("C12"), TEXT("The new Lobby carries no spray state, graffiti or result"));
        Check(!Public->GetCaptureMarker() && MarkerActors(*World) == 0,
            TEXT("C15"), TEXT("Returning to the Lobby removes the flagpole boundary"));
        SecondGeneration = Lobby.Generation;
        Summary->SetNumberField(TEXT("next_generation"), double(SecondGeneration));
        if (!Passed || !Mark(Role + TEXT("Lobby2.json"))) { return Finish(); }
        ReadySent = false;
        Stage = ELDCaptureProbeStage::SecondLobby;
        return true;
    case ELDCaptureProbeStage::SecondLobby:
    case ELDCaptureProbeStage::SecondMatch:
    {
        // An aborted start would retire the room into yet another generation.
        if (Lobby.Generation != SecondGeneration || (View.Generation && View.Generation != SecondGeneration))
        {
            Check(false, Mode ? TEXT("R01") : TEXT("R02"), TEXT("The second match runs in the new generation without an aborted start"));
            Error = FString::Printf(TEXT("The room left generation %u for %u before the second match was checked."),
                SecondGeneration, Lobby.Generation);
            return Finish();
        }
        if (Stage == ELDCaptureProbeStage::SecondLobby)
        {
            if (View.Generation == SecondGeneration && View.Phase == ELDMatchPhase::Development)
            {
                Stage = ELDCaptureProbeStage::SecondMatch;
                return true;
            }
            // Every role first records the new, unready Lobby (C11) before anyone readies for the second match;
            // otherwise a faster role's Ready reaches a slower role before its own C11 observation.
            if (Lobby.Phase != ELDMatchPhase::Lobby || Lobby.Members.Num() != 3 || !AllMarkers(TEXT("Lobby2.json"))
                || !EnsureReady(*PC, Lobby)) { return true; }
            if (!Mode || SecondStartSent || !AllHumansReady(Lobby)) { return true; }
            if (!Lobby.CanStart || !Lobby.StartBlocker.IsEmpty())
            {
                Check(false, TEXT("R01"), TEXT("The ready second Lobby starts the second match normally"));
                Error = TEXT("The second normal start is blocked: ") + Lobby.StartBlocker;
                return Finish();
            }
            PC->StartDemoMatch();
            SecondStartSent = true;
            return true;
        }
        // The first frame on which the owner's own views all describe the new match's living first field body, once its
        // R0 return protection has ended.
        const FLDProgressionView* Progress = PC->ReadOwnProgression();
        const FLDCombatOwnerView& Own = PC->CombatView;
        if (View.Phase != ELDMatchPhase::Development || !Cast<ALDCharacter>(PC->GetPawn())
            || PC->OwnerState.Match != SecondGeneration || PC->OwnerState.LifePhase != ELDLifePhase::Alive
            || PC->OwnerState.Life < LDCaptureProbeFirstFieldLife
            || !PC->OwnerState.BodyRef.IsValid() || !Progress
            || Own.Generation != SecondGeneration || Own.Life != PC->OwnerState.Life || !Own.bHasBody) { return true; }
        if (SecondProtectedUntilMs < 0) { SecondProtectedUntilMs = PC->OwnerState.ProtectedUntilMs; }
        if (PC->OwnerState.ProtectedUntilMs != 0) { return true; }
        return CheckSecondMatch(*PC, Mode, Lobby, View, *Progress);
    }
    default:
        return true;
    }
}

bool FLDDemoCaptureProbe::CheckSecondMatch(ALDPlayerController& PC, ALDGameMode* Mode, const FLDLobbyView& Lobby,
    const FLDPublicMatchView& View, const FLDProgressionView& Progress)
{
    const FLDDemoContentDefinitions& Defs = Content->Get();
    const FLDOwnerState& Self = PC.OwnerState;
    const FLDCombatOwnerView& Own = PC.CombatView;
    if (Mode)
    {
        Check(SecondStartSent && Lobby.Phase == ELDMatchPhase::Development,
            TEXT("R01"), TEXT("The Host's normal start opened the second match in the new generation without aborting"));
    }
    Check(Self.Match == SecondGeneration && Self.Life == LDCaptureProbeFirstFieldLife && Progress.Life == Self.Life
        && FirstBody.IsValid() && Self.BodyRef != FirstBody,
        TEXT("R02"), TEXT("The first Alive field frame is the new generation's first field life (after its R0 rest life) in a fresh body"));
    uint8 StartLevel = 0;
    const bool bStartLevel = LDProgression::LevelForXP(Defs.Progression, Defs.Progression.XPStart, StartLevel) == ELDGrowthReason::None;
    Check(bStartLevel && Progress.Level == StartLevel && Progress.CumulativeXP == Defs.Progression.XPStart,
        TEXT("R03"), TEXT("Level and XP are the content's opening values (start level, XP.Start)"));
    // Only the innate D dash, which every contender holds, may name a source at a fresh start.
    const bool bNoEntries = !Own.Entries.ContainsByPredicate([](const FLDCombatEntryView& Entry)
    { return Entry.Entry != ELDCombatEntry::Dash && !Entry.DefinitionId.IsNone(); });
    int64 OpeningPoints = 0;
    const bool bOpeningPoints = LDProgression::EarnedPoints(Defs, StartLevel, Defs.Progression.XPStart, 0, OpeningPoints) == ELDGrowthReason::None;
    Check(Progress.Actives.IsEmpty() && Progress.PendingBlueprints.IsEmpty() && bOpeningPoints && Progress.UnspentPoints == OpeningPoints
        && !Progress.StatPoints.ContainsByPredicate([](const FLDStatPointsView& Row) { return Row.Points != 0; })
        && Progress.ActiveOpportunitiesEarned == 1 && Progress.ActiveOpportunitiesConsumed == 0 && Progress.QueuedActiveOpportunities == 1
        && Progress.bHasActiveOffer && Progress.ActiveOffer.OpportunityOrdinal == 1 && bNoEntries,
        TEXT("R04"), TEXT("No learned active, blueprint or allocated point; only the opening points and this match's opening offer"));
    Check((SecondProtectedUntilMs == 0 || SecondProtectedUntilMs == View.Timeline.FieldStartedAtMs + ProtectionMs)
        && Self.ProtectedUntilMs == 0 && Own.Statuses.IsEmpty() && Own.ShieldP2 == 0 && !Own.bRooted
        && Own.MaxHPP2 > 0 && Own.HPP2 == Own.MaxHPP2,
        TEXT("R05"), TEXT("The first field body carries only the shared R0 return protection; once it ends the body has full HP and no shield, root, protection or other status"));
    const bool bTracksReady = !Own.Entries.ContainsByPredicate([&Own](const FLDCombatEntryView& Entry)
    {
        // The dash track is issued fresh for every match: ready, with no duration issued yet.
        return Entry.Entry == ELDCombatEntry::Dash ? Entry.CooldownMs != 0 || Entry.ReadyAtMs > Own.AsOfMs
            : Entry.IssuedCooldownId.IsValid() || Entry.ReadyAtMs > Own.AsOfMs;
    });
    Check(bTracksReady && Own.NextAttackAtMs <= Own.AsOfMs && Own.ActionEntry == ELDCombatEntry::None
        && Own.ActionPhase == ELDCombatPhase::Finished,
        TEXT("R06"), TEXT("Every cooldown source is ready with no issued cooldown track carried from the first match"));
    const auto* Public = PC.GetWorld()->GetGameState<ALDGameState>();
    Check(Public && FreshMarker(*Public, *PC.GetWorld(), SecondGeneration),
        TEXT("R10"), TEXT("The second match shows one fresh flagpole boundary for its own generation"));
    Summary->SetNumberField(TEXT("second_generation"), double(SecondGeneration));
    if (Mode)
    {
        // Host only, through the public camp, combat and core read APIs.
        const ALDCampDirector* Camps = Mode->Camps.Get();
        const FLDCampMatchState* State = Camps ? &Camps->GetState() : nullptr;
        const FLDCampRoster* Roster = State ? State->GetRoster() : nullptr;
        int32 Expected = 0, Fresh = 0;
        bool bCamps = State && State->IsActive() && State->GetMatch() == Mode->GetMatchContext() && Roster && Mode->Combat
            && State->NumCamps() > 0 && Roster->Camps.Num() == State->NumCamps() && Defs.Camps.Num() == State->NumCamps();
        for (int32 Camp = 0; bCamps && Camp < State->NumCamps(); ++Camp)
        {
            const FLDCampGroupState& Group = *State->FindCamp(Camp);
            Expected += Roster->Camps[Camp].Members.Num();
            bCamps &= Group.Generation == 1 && !Group.bRespawnPending && Group.ClearedAtMs == 0 && Group.RespawnDueMs == 0
                && Group.Members.Num() == Roster->Camps[Camp].Members.Num();
            for (const FLDCampMemberState& Member : Group.Members)
            {
                FLDCombatBodyRead Read;
                Fresh += Member.bAlive && Member.Life == 1 && !FirstCampMembers.Contains(Member.EntityId)
                    && Member.Brain.Phase == ELDCampBrainPhase::Idle && !Member.Brain.LostTargetSinceSeconds.IsSet()
                    && Member.HostileSources.IsEmpty() && Member.ReturnEpoch == 0 && Member.ResetThroughEpoch == 0
                    && Mode->Combat->ReadCombatBody(Member.Body, Read) && !Read.bPendingDeath && Read.MaxHPP2 > 0
                    && Read.HPP2 == Read.MaxHPP2 && Read.ShieldP2 == 0 && Read.RootedUntilMs == 0 ? 1 : 0;
            }
        }
        Check(bCamps && Expected > 0 && Fresh == Expected && FirstCampMembers.Num() == Expected,
            TEXT("R07"), TEXT("All five camps hold fresh first lives at full HP, idle, with no aggro or first-match identity"));
        Summary->SetNumberField(TEXT("second_fresh_camp_members"), Fresh);
        Summary->SetNumberField(TEXT("first_camp_members"), FirstCampMembers.Num());
        const FLDPublicMatchView Authority = Mode->ReadPublicMatch();
        auto Clean = [this](const FLDPublicMatchView& Match)
        {
            return Match.Generation == SecondGeneration && Match.Phase == ELDMatchPhase::Development
                && Match.CircleState == ELDCircleState::NotOpen && !Match.Occupier.IsValid() && !Match.SoloSinceMs && !Match.GraffitiOwner.IsValid()
                && !Match.DueAtMs && !Match.PausedProgressMs && !Match.CaptureEpoch && !Match.ResultId.IsValid()
                && Match.OpenAtMs == MAX_int64 && Match.Timeline.Stage == ELDMatchStage::Field && Match.Timeline.FieldSegment == 0
                && Match.Timeline.FieldStartedAtMs == Match.StartedAtMs + Rest0Ms;
        };
        Check(Clean(View) && Clean(Authority) && Public && Public->KillFeed.Generation == SecondGeneration
            && Public->KillFeed.Notices.IsEmpty()
            && !(Public->MatchResult.Generation == SecondGeneration && Public->MatchResult.ResultId.IsValid())
            && Mode->IsGameplayWriteOpen(Mode->GetMatchContext()),
            TEXT("R08"), TEXT("Capture restarts in Development with no holder, progress, contest, kill or result"));
        // A request stamped with the first match's generation would end the second match if it were accepted.
        const FLDLobbyView LobbyBefore = Mode->ReadLobby();
        const uint64 OwnerRevision = Self.Revision;
        FLDSessionRequest Stale;
        Stale.Key.Generation = FirstGeneration;
        Stale.Key.RequestSequence = 1;
        Stale.Command = ELDSessionCommand::EndMatch;
        Stale.ExpectedRosterRevision = LobbyBefore.RosterRevision;
        const FLDSessionReceipt Receipt = Mode->ProcessSessionCommand(&PC, Stale);
        const FLDPublicMatchView After = Mode->ReadPublicMatch();
        const FLDLobbyView LobbyAfter = Mode->ReadLobby();
        Check(Receipt.Outcome == ELDSessionOutcome::Rejected && Receipt.Reason == ELDSessionReject::StaleMatch
            && After.Generation == Authority.Generation && After.PublicRevision == Authority.PublicRevision
            && After.Phase == ELDMatchPhase::Development && After.CaptureEpoch == Authority.CaptureEpoch
            && LobbyAfter.RosterRevision == LobbyBefore.RosterRevision && LobbyAfter.PublicRevision == LobbyBefore.PublicRevision
            && PC.OwnerState.Revision == OwnerRevision && Mode->IsGameplayWriteOpen(Mode->GetMatchContext()),
            TEXT("R09"), TEXT("A first-match EndMatch request is rejected as StaleMatch and changes nothing"));
    }
    Stage = ELDCaptureProbeStage::Done;
    return Finish();
}

void LDDemoTests::StartCaptureProbe(ALDPlayerController& Controller)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoCaptureProbe")) || !Controller.IsLocalController()
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap) { return; }
    auto Probe = MakeShared<FLDDemoCaptureProbe>();
    // #198: without early readiness, the flagpole opens at R0 180+5 s + three fields 3x180 s
    // + R1..R3 3x(120+5) s = 1100 s after the first start. The second match is checked after its own R0.
    int32 Timeout = 1800;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoCaptureTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoCaptureOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoCaptureRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1") && Probe->Role != TEXT("Client2"))
        || Timeout < 120 || Timeout > 1800
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_CAPTURE_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    Probe->Controller = &Controller;
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .1f);
}
#endif
