#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDDemoEntry.h"
#include "Character/LDCharacter.h"
#include "Navigation/LDNavigationComponent.h"
#include "Tests/LDDemoEntryProbe.h"
#include "Tests/LDDemoVisionProbe.h"
#include "Tests/LDDemoGrowthProbe.h"
#include "Tests/LDDemoCombatProbe.h"
#include "Tests/LDDemoCampProbe.h"
#include "UI/LDUIRoot.h"
#include "UI/LDInventoryPanel.h"
#include "Vision/LDWorldViewRelay.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

ALDPlayerController::ALDPlayerController()
{ Navigation=CreateDefaultSubobject<ULDNavigationComponent>(TEXT("NavigationCommands")); }

bool ALDPlayerController::CanWorldCommand() const
{
    const auto* Public=GetWorld()->GetGameState<ALDGameState>();
    return !bControlsSuppressed && !bFreshInputRequired && GetPawn() && Public
        && Public->DemoLobby.Generation==OwnerState.Match && OwnerState.LifePhase==ELDLifePhase::Alive
        && OwnerState.BodyRef.IsValid() && (Public->DemoLobby.Phase==ELDMatchPhase::Development
            || Public->DemoLobby.Phase==ELDMatchPhase::CaptureOpen);
}

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
    InitializeLocalUI();
    // Growth/Shop: shop arrival requests the shop session (LDPlayerGrowth.cpp).
    BindGrowthClient();
#if WITH_DEV_AUTOMATION_TESTS
    LDDemoTests::StartWorldRoomProbe(*this);
    LDDemoTests::StartCoreProbe(*this);
    LDDemoTests::StartVisionProbe(*this);
    LDDemoTests::StartGrowthProbe(*this);
    LDDemoTests::StartCombatProbe(*this);
    LDDemoTests::StartCampProbe(*this);
#endif
}

void ALDPlayerController::InitializeLocalUI()
{
    if (!IsLocalController() || !GetLocalPlayer() || RootUI) { return; }
    Controls=CastChecked<ULDGameInstance>(GetGameInstance())->GetLocalControlSettings();
    bShowMouseCursor=true;
    const TSubclassOf<ULDUIRoot> RootClass=LoadClass<ULDUIRoot>(nullptr,TEXT("/Game/Lockdown/Generated/UI/WBP_UIRoot.WBP_UIRoot_C"));
    checkf(RootClass,TEXT("Missing UI root; run asset bootstrap"));
    RootUI=CreateWidget<ULDUIRoot>(this,RootClass);
    if (!RootUI)
    {
        UE_LOG(LogTemp,Error,TEXT("LD_DEMO_UI_INITIALIZATION_FAILED Native UI root could not be created."));
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
        UE_LOG(LogTemp,Error,TEXT("LD_INTENT_FEEDBACK_EXHAUSTED"));
        return;
    }
    ++IntentFeedbackSerial;
    IntentFeedback=Reason;
}

void ALDPlayerController::ClearControls(bool LifeChanged)
{
    if (InventoryUI) { InventoryUI->CancelDrag(); }
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
    bFreshInputRequired=true;
    ClearControls();
}

void ALDPlayerController::SetMenuOpen(bool bOpen)
{
    bFreshInputRequired=true;
    if (bControlsSuppressed!=bOpen)
    {
        bControlsSuppressed=bOpen;
        SetIgnoreMoveInput(bOpen);
    }
    if (bOpen) { ClearControls(); }
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    if (bOpen && RootUI) { Mode.SetWidgetToFocus(RootUI->TakeWidget()); }
    SetInputMode(Mode);
}

void ALDPlayerController::PlayerTick(float Delta)
{
    Super::PlayerTick(Delta);
    InitializeLocalUI();
    UpdateV05Inputs(Delta);
    if (!GetLocalPlayer() || IsFrontend()) { return; }
    if (auto* Active=Cast<ALDCharacter>(GetPawn()))
    {
        LastBodyPosition=Active->GetActorLocation();
        if (WaitingCamera)
        {
            SetViewTarget(Active);
            WaitingCamera->Destroy();
            WaitingCamera=nullptr;
        }
    }
    else if (OwnerState.LifePhase!=ELDLifePhase::Alive && !WaitingCamera)
    {
        WaitingCamera=GetWorld()->SpawnActor<ACameraActor>();
        if (!WaitingCamera)
        {
            UE_LOG(LogTemp,Error,TEXT("LD_DEMO_CAMERA_CREATION_FAILED"));
            return;
        }
        const FRotator Rotation(-60,-45,0);
        WaitingCamera->SetActorLocation(LastBodyPosition+FVector(0,0,20)-Rotation.Vector()*1400);
        WaitingCamera->SetActorRotation(Rotation);
        auto* Camera=WaitingCamera->GetCameraComponent();
        Camera->FieldOfView=55;
        Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness=true;
        Camera->PostProcessSettings.AutoExposureMinBrightness=0;
        Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness=true;
        Camera->PostProcessSettings.AutoExposureMaxBrightness=0;
        Camera->PostProcessSettings.bOverride_MotionBlurAmount=true;
        Camera->PostProcessSettings.MotionBlurAmount=0;
        SetViewTarget(WaitingCamera);
    }
}
