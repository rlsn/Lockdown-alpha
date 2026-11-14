#include "Framework/LDPlayerController.h"
#include "Core/LDLog.h"
#include "Audio/LDAudioSubsystem.h"
#include "Audio/LDMusicSelection.h"
#include "Framework/LDBuildBlueprintComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDPlaytestTrace.h"
#include "Character/LDCharacter.h"
#include "Character/LDCameraSettings.h"
#include "Navigation/LDNavigationComponent.h"
#include "Tests/LDDemoEntryProbe.h"
#include "Tests/LDDemoVisionProbe.h"
#include "Tests/LDDemoGrowthProbe.h"
#include "Tests/LDDemoCombatProbe.h"
#include "Tests/LDDemoCampProbe.h"
#include "Tests/LDDemoCaptureProbe.h"
#include "Tests/LDDemoOpponentProbe.h"
#include "Tests/LDDemoSessionFaultProbe.h"
#include "Tests/LDBuildEvaluationProbe.h"
#include "UI/LDUIRoot.h"
#include "UI/LDControlSettings.h"
#include "Vision/LDWorldViewRelay.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

ALDPlayerController::ALDPlayerController()
{
    Navigation=CreateDefaultSubobject<ULDNavigationComponent>(TEXT("NavigationCommands"));
    BuildBlueprints=CreateDefaultSubobject<ULDBuildBlueprintComponent>(TEXT("BuildBlueprints"));
    bAutoManageActiveCameraTarget=false;
}

// New world commands need both halves (LDPlayerInput.cpp): a valid life in the running match and unsuppressed input.
bool ALDPlayerController::CanWorldCommand() const
{ return IsWorldLifeValid() && !IsWorldInputSuppressed(); }

bool ALDPlayerController::IsFrontend() const
{ return UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName())==LDDemoEntry::FrontendMap; }

bool ALDPlayerController::ReadDemoAuthorityTimeMs(int64& OutNowMs) const
{
    OutNowMs=0;
    const auto* Public=GetWorld()->GetGameState<ALDGameState>();
    if (!Public || !Public->DemoClock.IsValid() || Public->DemoClock.Generation!=OwnerState.Match) { return false; }
    OutNowMs=Public->DemoClock.NowMs;
    return true;
}

void ALDPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if(bParkedForReconnect){return;}
    InitializeLocalUI();
#if WITH_DEV_AUTOMATION_TESTS
    LDDemoTests::StartWorldRoomProbe(*this);
    LDDemoTests::StartCoreProbe(*this);
    LDDemoTests::StartVisionProbe(*this);
    LDDemoTests::StartGrowthProbe(*this);
    LDDemoTests::StartCombatProbe(*this);
    LDDemoTests::StartCampProbe(*this);
    LDDemoTests::StartCaptureProbe(*this);
    LDDemoTests::StartOpponentProbe(*this);
    LDDemoTests::StartSessionFaultProbe(*this);
    LDDemoTests::StartBuildEvaluation(*this);
#endif
}

void ALDPlayerController::PawnLeavingGame()
{
    if(auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();Mode&&Mode->CanParkDisconnectedMember(*this))
    {
        FString Error;
        if(Mode->ParkDisconnectedMember(*this,Error)){return;}
        Mode->ReportAuthorityFault(Mode->GetMatchContext(),Error);
    }
    Super::PawnLeavingGame();
}

void ALDPlayerController::InitializeLocalUI()
{
    if (!IsLocalController() || !GetLocalPlayer() || RootUI) { return; }
    auto* Instance=CastChecked<ULDGameInstance>(GetGameInstance());
    // A room this client already leaves for a refusal or a closed room gets no room UI: building it would mark that
    // reason as already seen, and the frontend would show none.
    if (Instance->IsLeavingWorld(GetWorld())) { return; }
    Controls=Instance->GetLocalControlSettings();
    bShowMouseCursor=true;
    const TSubclassOf<ULDUIRoot> RootClass=LoadClass<ULDUIRoot>(nullptr,TEXT("/Game/Lockdown/Generated/UI/WBP_UIRoot.WBP_UIRoot_C"));
    checkf(RootClass,TEXT("Missing UI root; run asset bootstrap"));
    RootUI=CreateWidget<ULDUIRoot>(this,RootClass);
    if (!RootUI)
    {
        UE_LOG(LogLDUI,Error,TEXT("LD_DEMO_UI_INITIALIZATION_FAILED Native UI root could not be created."));
        return;
    }
    RootUI->AddToViewport();
    SetMenuOpen(IsFrontend());
    RebuildV05Mappings();
}

void ALDPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    RebuildV05Mappings();
}

void ALDPlayerController::OnRep_OwnerState()
{
    if (OwnerState.Match<LatestOwner.Match || (OwnerState.Match==LatestOwner.Match
        && (OwnerState.Revision<LatestOwner.Revision || OwnerState.Life<LatestOwner.Life)))
    {
        OwnerState=LatestOwner;
        return;
    }
    const bool NewLife=IsLocalController() && (OwnerState.Match!=LatestOwner.Match
        || OwnerState.Life!=LatestOwner.Life || OwnerState.BodyRef!=LatestOwner.BodyRef
        || (LatestOwner.LifePhase==ELDLifePhase::Alive && OwnerState.LifePhase!=ELDLifePhase::Alive));
    LatestOwner=OwnerState;
    if (NewLife)
    {
        Inspected.Invalidate();
        InspectedEpoch=0;
        ClearControls(true);
    }
}

void ALDPlayerController::ClientLifeReceipt_Implementation(const FLDOwnerState& State)
{
    if (State.Match<OwnerState.Match || (State.Match==OwnerState.Match
        && (State.Revision<OwnerState.Revision || State.Life<OwnerState.Life))) { return; }
    OwnerState=State;
    OnRep_OwnerState();
}

void ALDPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ALDPlayerController,OwnerState);
    DOREPLIFETIME_CONDITION(ALDPlayerController,CombatView,COND_OwnerOnly);
}

ALDWorldViewRelay* ALDPlayerController::LocalView() const
{
    for (TActorIterator<ALDWorldViewRelay> It(GetWorld());It;++It)
    { if (It->GetOwner()==this) { return *It; } }
    return nullptr;
}

void ALDPlayerController::ClientIntentResult_Implementation(uint32 Request,const FString& Reason)
{
    if (Request)
    {
        if (SeenIntentResults.ContainsByPredicate([&](const auto& Result) { return Result.Key==Request && Result.Value==Reason; })) { return; }
        if (SeenIntentResults.Num()>=128) { SeenIntentResults.RemoveAt(0); }
        SeenIntentResults.Emplace(Request,Reason);
    }
    if (IntentFeedbackSerial==MAX_uint32)
    {
        UE_LOG(LogLDUI,Error,TEXT("LD_INTENT_FEEDBACK_EXHAUSTED"));
        return;
    }
    ++IntentFeedbackSerial;
    IntentFeedback=Reason;
}

void ALDPlayerController::ClearControls(bool LifeChanged)
{
    if (LifeChanged) { Navigation->ResetLife(); }
    else { Navigation->Stop(); }
    Navigation->Recenter(false);
    if (!HasAuthority())
    {
        if (auto* Body=Cast<ALDCharacter>(GetPawn()))
        {
            Body->ConsumeMovementInputVector();
            auto* Movement=Body->GetCharacterMovement();
            if (Movement->IsFalling()) { Movement->Velocity.X=Movement->Velocity.Y=0; }
            else { Movement->StopMovementImmediately(); }
        }
    }
}

void ALDPlayerController::FlushPressedKeys()
{
    Super::FlushPressedKeys();
    // The engine also flushes when a panel or Settings takes keyboard focus inside this window: that only ends local
    // gestures. Losing OS focus is Navigation's single Stop (UI 4, 7).
    ReleaseLocalInput();
}

void ALDPlayerController::SetMenuOpen(bool bOpen)
{
    // The menu and Settings suppress new world commands only: an accepted order keeps its heartbeat and the body
    // keeps following its path underneath (UI 4, 7).
    bControlsSuppressed=bOpen;
    ReleaseLocalInput();
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    if (bOpen && RootUI) { Mode.SetWidgetToFocus(RootUI->TakeWidget()); }
    SetInputMode(Mode);
}

void ALDPlayerController::UpdateCameraManager(float DeltaSeconds)
{
    bool bDevArtCapture=false;
#if WITH_DEV_AUTOMATION_TESTS
    bDevArtCapture=DevArtCaptureCamera.IsValid();
#endif
    if (GetLocalPlayer())
    {
        if (!IsFrontend()) { UpdateLocalViewTarget(); }
        if (auto* Active=Cast<ALDCharacter>(GetPawn())) { Active->UpdateOwnerPresentation(DeltaSeconds); }
        if (!bDevArtCapture) { Navigation->UpdateLocalCamera(DeltaSeconds); }
    }
    Super::UpdateCameraManager(DeltaSeconds);
    if (!bDevArtCapture && GetLocalPlayer() && !IsFrontend() && PlayerCameraManager)
    {
        LastCameraPose=FTransform(PlayerCameraManager->GetCameraRotation(),PlayerCameraManager->GetCameraLocation());
        LastCameraFOV=PlayerCameraManager->GetFOVAngle();
        bHasCameraPose=true;
    }
}

void ALDPlayerController::PlayerTick(float Delta)
{
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(LDPlayerTick_Engine);
        Super::PlayerTick(Delta);
    }
    InitializeLocalUI();
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(LDPlayerTick_Inputs);
        UpdateV05Inputs(Delta);
    }
    UpdateMusic();
    if (!GetLocalPlayer() || IsFrontend()) { return; }
    TRACE_CPUPROFILER_EVENT_SCOPE(LDPlayerTick_PlaytestTrace);
    Trace().Tick(*this);
}

void ALDPlayerController::UpdateMusic()
{
    if (!GetLocalPlayer()) { return; }
    UGameInstance* Instance = GetGameInstance();
    auto* AudioOutput = Instance ? Instance->GetSubsystem<ULDAudioSubsystem>() : nullptr;
    if (!AudioOutput) { return; }
    FLDMusicInput Input;
    Input.bFrontend = IsFrontend();
    if (const auto* Public = GetWorld()->GetGameState<ALDGameState>()) { Input.Public = &Public->PublicMatch; }
    Input.bClock = ReadDemoAuthorityTimeMs(Input.NowMs);
    AudioOutput->SetMusic(LDAudio::SelectMusic(Input));
}

#if WITH_DEV_AUTOMATION_TESTS
void ALDPlayerController::SetDevArtCaptureCamera(ACameraActor* Camera)
{ DevArtCaptureCamera=Camera; }
#endif

void ALDPlayerController::UpdateLocalViewTarget()
{
#if WITH_DEV_AUTOMATION_TESTS
    if (auto* CaptureCamera=DevArtCaptureCamera.Get())
    {
        if (GetViewTarget()!=CaptureCamera) { SetViewTarget(CaptureCamera); }
        return;
    }
#endif
    if (auto* Active=Cast<ALDCharacter>(GetPawn()))
    {
        // Possession and the owner life receipt travel independently. Always bind the actual local body before
        // evaluating the camera, even if a late engine restart/reset previously selected the controller.
        if (GetViewTarget()!=Active) { SetViewTarget(Active); }
        if (IsValid(WaitingCamera))
        {
            WaitingCamera->Destroy();
            WaitingCamera=nullptr;
        }
        return;
    }
    // A missing replicated pawn does not mean the owner life receipt has caught up. Keep the last rendered pose
    // through this gap instead of exposing the controller's origin or a newly spawned pawn's default arm position.
    if (!IsValid(WaitingCamera))
    {
        WaitingCamera=GetWorld()->SpawnActor<ACameraActor>();
        if (!WaitingCamera)
        {
            UE_LOG(LogLDUI,Error,TEXT("LD_DEMO_CAMERA_CREATION_FAILED"));
            return;
        }
        const float ArmLength=Controls?Controls->Zoom:LDCamera::DefaultArmLength;
        WaitingCamera->SetActorTransform(bHasCameraPose ? LastCameraPose
            : ALDCharacter::DefaultArmCameraPose(FVector::ZeroVector,ArmLength));
        auto* Camera=WaitingCamera->GetCameraComponent();
        Camera->FieldOfView=bHasCameraPose ? LastCameraFOV : LDCamera::FieldOfView;
        Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness=true;
        Camera->PostProcessSettings.AutoExposureMinBrightness=0;
        Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness=true;
        Camera->PostProcessSettings.AutoExposureMaxBrightness=0;
        Camera->PostProcessSettings.bOverride_MotionBlurAmount=true;
        Camera->PostProcessSettings.MotionBlurAmount=0;
    }
    if (GetViewTarget()!=WaitingCamera) { SetViewTarget(WaitingCamera); }
}
