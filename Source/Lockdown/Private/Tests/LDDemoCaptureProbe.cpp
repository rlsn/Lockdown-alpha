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
#include "Region/LDMapRegistry.h"
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

// Host stays at its spawn and Client1 sprays. Client2's contest pauses Client1's spray progress and Client1 resumes
// it until his graffiti lands. The owner then walks away, which pauses his guard (2026-11-07 user decision: it counts
// only while he stands alone in the zone); Client2 repaints alone, keeping it frozen, and walks out before his spray
// lands, losing his progress, while the guard stays paused. The owner walks back in and the guard resumes from its kept
// value; Client2 joins him (the guard freezes again) and the owner walks out under Client2, which resets the frozen
// guard to zero. Client2 walks out before repainting, the empty zone keeps the guard paused at zero, and the owner
// walks back in and stands alone in the zone for the whole guard to win. After the result the Host returns the same
// connections to a new Lobby generation and starts a second match.
enum class ELDCaptureProbeStage : uint8 { Join, Lobby, Starting, Match, Results, SecondLobby, SecondMatch, Done };

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
    int64 SprayMs = 0, GuardMs = 0, DevelopmentMs = 0;
    FVector Center = FVector::ZeroVector, Staging = FVector::ZeroVector, Inside = FVector::ZeroVector;
    FName Surface;
    double RadiusCm = 0;
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

    bool Tick();
    bool CheckSecondMatch(ALDPlayerController& PC, ALDGameMode* Mode, const FLDLobbyView& Lobby, const FLDPublicMatchView& View,
        const FLDProgressionView& Progress, const FLDEquipmentView& Equipment);
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
        DevelopmentMs = Content->Get().Parameters.DevelopmentMs;
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
        if (View.Phase != ELDMatchPhase::Development || View.Generation != Lobby.Generation || !Body
            || PC->OwnerState.LifePhase != ELDLifePhase::Alive || !PC->CanWorldCommand()) { return true; }
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
        Check(View.Generation == FirstGeneration && View.OpenAtMs == View.StartedAtMs + DevelopmentMs
            && View.CircleState == ELDCircleState::NotOpen && !View.Occupier.IsValid() && !View.GraffitiOwner.IsValid(),
            TEXT("C04"), TEXT("Normal start opens the approved development phase with a closed, bare flagpole"));
        FVector Direction = Body->GetActorLocation() - Center;
        Direction.Z = 0;
        Direction = Direction.GetSafeNormal();
        Check(!Direction.IsNearlyZero(), TEXT("C05"), TEXT("Spawned body is outside the circle centre"));
        Check(FreshMarker(*Public, *World, View.Generation), TEXT("C13"),
            TEXT("Every role shows one flat flagpole boundary at the approved radius around the anchor, without collision"));
        Staging = Center + Direction * (RadiusCm + 300.);
        Inside = Center + Direction * (RadiusCm - 250.);
        // The second match proves freshness against these first-match identities.
        FirstBody = PC->OwnerState.BodyRef;
        if (Mode) { CampMembers(*Mode, FirstCampMembers); }
        if (!Passed) { return Finish(); }
        if (Role == TEXT("Client1")) { Nav->NavigateGround(Center, Surface); }
        else if (Role == TEXT("Client2")) { Nav->NavigateGround(Staging, Surface); }
        Stage = ELDCaptureProbeStage::Match;
        return true;
    }
    case ELDCaptureProbeStage::Match:
    {
        int64 NowMs = 0;
        PC->ReadDemoAuthorityTimeMs(NowMs);
        if (Role == TEXT("Client2") && View.Phase == ELDMatchPhase::CaptureOpen)
        {
            // Contest early in the short spray, walk out to let it resume, then repaint alone and give up before it lands.
            if (ContestStep == 0 && Seeing(ELDCaptureProbeSeen::Solo) && !Nav->HasMoveOrder() && NowMs - FirstSoloSinceMs >= 300)
            {
                Nav->NavigateGround(Inside, Surface);
                ContestStep = 1;
            }
            else if (After(ELDCaptureProbeSeen::Paused, 1, 1.5)) { Nav->NavigateGround(Staging, Surface); ContestStep = 3; }
            else if (After(ELDCaptureProbeSeen::OwnerAway, 3, 1.)) { Nav->NavigateGround(Inside, Surface); ContestStep = 5; }
            else if (After(ELDCaptureProbeSeen::Repainting, 5, 1.)) { Nav->NavigateGround(Staging, Surface); ContestStep = 7; }
            // Join the owner once he is back in the zone, then give up the repaint the owner's exit hands over.
            else if (After(ELDCaptureProbeSeen::OwnerBack, 7, 1.)) { Nav->NavigateGround(Inside, Surface); ContestStep = 9; }
            else if (After(ELDCaptureProbeSeen::GuardReset, 9, 1.)) { Nav->NavigateGround(Staging, Surface); ContestStep = 11; }
        }
        else if (Role == TEXT("Client1") && View.Phase == ELDMatchPhase::CaptureOpen)
        {
            // The owner walks away once his graffiti stands, which pauses his guard. He comes back after the abandoned
            // repaint, resuming it, and walks out again under the challenger, which resets his frozen guard. Once the
            // challenger has left he stands alone in the zone for the whole guard.
            if (After(ELDCaptureProbeSeen::Sprayed, 0, 1.5)) { Nav->NavigateGround(Staging, Surface); ContestStep = 2; }
            else if (After(ELDCaptureProbeSeen::RepaintLeft, 2, 1.)) { Nav->NavigateGround(Inside, Surface); ContestStep = 4; }
            else if (After(ELDCaptureProbeSeen::GuardFrozen, 4, 1.5)) { Nav->NavigateGround(Staging, Surface); ContestStep = 6; }
            else if (After(ELDCaptureProbeSeen::ResetPaused, 6, 1.)) { Nav->NavigateGround(Inside, Surface); ContestStep = 8; }
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
        // The first frame on which the owner's own views all describe the new match's living first body.
        const FLDProgressionView* Progress = PC->ReadOwnProgression();
        const FLDEquipmentView* Equipment = PC->ReadOwnEquipment();
        const FLDCombatOwnerView& Own = PC->CombatView;
        if (View.Phase != ELDMatchPhase::Development || !Cast<ALDCharacter>(PC->GetPawn())
            || PC->OwnerState.Match != SecondGeneration || PC->OwnerState.LifePhase != ELDLifePhase::Alive
            || !PC->OwnerState.BodyRef.IsValid() || !Progress || !Equipment
            || Own.Generation != SecondGeneration || Own.Life != PC->OwnerState.Life || !Own.bHasBody) { return true; }
        return CheckSecondMatch(*PC, Mode, Lobby, View, *Progress, *Equipment);
    }
    default:
        return true;
    }
}

bool FLDDemoCaptureProbe::CheckSecondMatch(ALDPlayerController& PC, ALDGameMode* Mode, const FLDLobbyView& Lobby,
    const FLDPublicMatchView& View, const FLDProgressionView& Progress, const FLDEquipmentView& Equipment)
{
    const FLDDemoContentDefinitions& Defs = Content->Get();
    const FLDOwnerState& Self = PC.OwnerState;
    const FLDCombatOwnerView& Own = PC.CombatView;
    if (Mode)
    {
        Check(SecondStartSent && Lobby.Phase == ELDMatchPhase::Development,
            TEXT("R01"), TEXT("The Host's normal start opened the second match in the new generation without aborting"));
    }
    Check(Self.Match == SecondGeneration && Self.Life == 1 && Progress.Life == Self.Life
        && FirstBody.IsValid() && Self.BodyRef != FirstBody,
        TEXT("R02"), TEXT("The first Alive frame is the new generation's first life in a fresh body"));
    uint8 StartLevel = 0;
    const bool bStartLevel = LDProgression::LevelForXP(Defs.Progression, Defs.Progression.XPStart, StartLevel) == ELDGrowthReason::None;
    Check(bStartLevel && Progress.Level == StartLevel && Progress.CumulativeXP == Defs.Progression.XPStart
        && Equipment.Gold == Defs.Parameters.StartingGold,
        TEXT("R03"), TEXT("Level, XP and gold are the content's opening values (start level, XP.Start, SHOP.StartingGold)"));
    bool bSlotsEmpty = Equipment.Slots.Num() == Defs.Progression.EquipmentSlots;
    for (const FLDEquipmentSlotView& Slot : Equipment.Slots) { bSlotsEmpty &= !Slot.bOccupied; }
    // Only the innate D dash, which every contender holds, may name a source at a fresh start.
    const bool bNoEntries = !Own.Entries.ContainsByPredicate([](const FLDCombatEntryView& Entry)
    { return Entry.Entry != ELDCombatEntry::Dash && !Entry.DefinitionId.IsNone(); });
    Check(Progress.Skills.IsEmpty() && Progress.AttributeCards.IsEmpty() && Progress.EarnedThrough == 1
        && Progress.ConsumedThrough == 0 && Progress.PendingChoiceCount == 1 && Progress.bHasOffer && Progress.Offer.OpportunityOrdinal == 1
        && bSlotsEmpty && bNoEntries && Own.EquipmentPassives.IsEmpty() && PC.ReadOwnShop() == nullptr,
        TEXT("R04"), TEXT("No learned skill, attribute card, equipment or shop session; only this match's unconsumed opening choice"));
    Check(Self.ProtectedUntilMs == 0 && Own.Statuses.IsEmpty() && Own.ShieldP2 == 0 && !Own.bRooted
        && Own.MaxHPP2 > 0 && Own.HPP2 == Own.MaxHPP2,
        TEXT("R05"), TEXT("The initial body has full HP and no shield, root, protection or other status"));
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
            && State->NumCamps() == 5 && Roster->Camps.Num() == State->NumCamps() && Defs.Camps.Num() == State->NumCamps();
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
                && Match.OpenAtMs == Match.StartedAtMs + DevelopmentMs;
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
    int32 Timeout = 480;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoCaptureTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoCaptureOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoCaptureRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1") && Probe->Role != TEXT("Client2"))
        || Timeout < 120 || Timeout > 900
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
