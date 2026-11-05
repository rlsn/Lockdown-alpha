#include "Framework/LDPlayerController.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Character/LDModelPicking.h"
#include "Navigation/LDNavigationComponent.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDHUD.h"
#include "UI/LDUIRoot.h"
#include "Vision/LDWorldViewRelay.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Components/CapsuleComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/App.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace LDPlayerInputDetail
{
    // PublicMatch is the single generation and phase source; DemoLobby only mirrors it for the roster.
    bool InRunningMatch(const ALDPlayerController& PC)
    {
        const auto* Public=PC.GetWorld()->GetGameState<ALDGameState>();
        return Public && Public->PublicMatch.Generation!=0 && Public->PublicMatch.Generation==PC.OwnerState.Match
            && (Public->PublicMatch.Phase==ELDMatchPhase::Development || Public->PublicMatch.Phase==ELDMatchPhase::CaptureOpen);
    }

    bool PhysicalDown(const ALDPlayerController* PC,FKey Key)
    {
#if PLATFORM_WINDOWS
        if (!FApp::IsUnattended())
        {
            const uint32* Code=nullptr;
            const uint32* Character=nullptr;
            FInputKeyManager::Get().GetCodesFromKey(Key,Code,Character);
            if (Code) { return (::GetAsyncKeyState(int32(*Code))&0x8000)!=0; }
        }
#endif
        return PC->IsInputKeyDown(Key);
    }
}

FKey ALDPlayerController::BindingKey(FName Action) const
{ return Controls?Controls->Key(Action):FKey(); }

FString ALDPlayerController::BindingLabel(FName Action) const
{ return Controls?Controls->Label(Action):Action.ToString(); }

FString ALDPlayerController::HeldInputLabel() const
{ return BindingLabel(TEXT("Stop"))+TEXT(" stops"); }

FString ALDPlayerController::ControlSummary() const
{
    auto Key=[this](const TCHAR* Action) { return BindingLabel(FName(Action)); };
    return Key(TEXT("Select"))+TEXT(" inspect / ")+Key(TEXT("Navigate"))+TEXT(" move / ")+Key(TEXT("Stop"))+TEXT(" stop\n")
        +Key(TEXT("PanUp"))+TEXT(" / ")+Key(TEXT("PanDown"))+TEXT(" / ")+Key(TEXT("PanLeft"))+TEXT(" / ")+Key(TEXT("PanRight"))+TEXT(" camera / ")
        +Key(TEXT("Recenter"))+TEXT(" hold recenter / ")+Key(TEXT("Follow"))+TEXT(" follow / ")
        +Key(TEXT("Back"))+TEXT(" settings / Back");
}

FVector2D ALDPlayerController::CameraPanAxis() const
{
    return FVector2D((IsInputKeyDown(BindingKey(TEXT("PanRight")))?1:0)-(IsInputKeyDown(BindingKey(TEXT("PanLeft")))?1:0),
        (IsInputKeyDown(BindingKey(TEXT("PanUp")))?1:0)-(IsInputKeyDown(BindingKey(TEXT("PanDown")))?1:0));
}

bool ALDPlayerController::IsWorldLifeValid() const
{
    return LDPlayerInputDetail::InRunningMatch(*this) && OwnerState.LifePhase==ELDLifePhase::Alive
        && OwnerState.BodyRef.IsValid() && GetPawn();
}

bool ALDPlayerController::IsWorldInputSuppressed() const
{ return bControlsSuppressed || bFreshInputRequired || IsWorldInputCaptured(); }

bool ALDPlayerController::IsWorldInputCaptured() const
{
    const ELDUIInputLayer Layer=RootUI?RootUI->TopInputLayer():ELDUIInputLayer::None;
    return Layer==ELDUIInputLayer::TextEditing || Layer==ELDUIInputLayer::ChildConfirmation;
}

bool ALDPlayerController::IsAwaitingRespawn() const
{ return LDPlayerInputDetail::InRunningMatch(*this) && OwnerState.LifePhase==ELDLifePhase::Dead; }

bool ALDPlayerController::CanCameraCommand() const
{ return !IsWorldInputSuppressed() && (IsWorldLifeValid() || IsAwaitingRespawn()); }

void ALDPlayerController::ReleaseLocalInput()
{
    bFreshInputRequired=true;
    CancelArmedCombat();
    QueuedCombat={};
    if (Navigation) { Navigation->Recenter(false); }
}

void ALDPlayerController::RebuildV05Mappings()
{
    if (!IsLocalController() || !Controls || (!Navigation->Enabled() && !IsFrontend())) { return; }
    auto* Local=GetLocalPlayer();
    auto* Input=Cast<UEnhancedInputComponent>(InputComponent);
    if (!Local || !Input) { return; }
    auto* Subsystem=Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
    if (!Subsystem)
    {
        UE_LOG(LogTemp,Error,TEXT("LD_DEMO_INPUT_CONTEXT_MISSING"));
        return;
    }
    for (uint32 Handle : V05BindingHandles) { Input->RemoveBindingByHandle(Handle); }
    V05BindingHandles.Reset();
    if (NavigationInputContext) { Subsystem->RemoveMappingContext(NavigationInputContext); }
    // A rebuilt mapping clears latches and waits for release; it never stops an accepted order (UI 7).
    ReleaseLocalInput();
    NavigationInputContext=NewObject<UInputMappingContext>(this);
    for (const auto& Binding : Controls->Definitions())
    {
        auto* Action=NewObject<UInputAction>(NavigationInputContext);
        Action->ValueType=Binding.Axis?EInputActionValueType::Axis1D:EInputActionValueType::Boolean;
        NavigationInputContext->MapKey(Action,Controls->Key(Binding.Action));
        if (Binding.Axis)
        { V05BindingHandles.Add(Input->BindAction(Action,ETriggerEvent::Triggered,this,&ALDPlayerController::V05Zoom).GetHandle()); }
        else
        {
            V05BindingHandles.Add(Input->BindAction(Action,ETriggerEvent::Started,this,&ALDPlayerController::V05Pressed,Binding.Action).GetHandle());
            V05BindingHandles.Add(Input->BindAction(Action,ETriggerEvent::Completed,this,&ALDPlayerController::V05Released,Binding.Action).GetHandle());
            V05BindingHandles.Add(Input->BindAction(Action,ETriggerEvent::Canceled,this,&ALDPlayerController::V05Released,Binding.Action).GetHandle());
        }
    }
    FModifyContextOptions Options;
    Options.bIgnoreAllPressedKeysUntilRelease=true;
    Subsystem->AddMappingContext(NavigationInputContext,10,Options);
    bNavigationContextActive=true;
    Navigation->bEdgePan=Controls->bEdgePan;
}

void ALDPlayerController::UpdateV05Inputs(float Delta)
{
    if (!IsLocalController() || !GetLocalPlayer() || (!Navigation->Enabled() && !IsFrontend())) { return; }
    if (!Controls) { Controls=CastChecked<ULDGameInstance>(GetGameInstance())->GetLocalControlSettings(); }
    if (!Controls) { IntentFeedback=CastChecked<ULDGameInstance>(GetGameInstance())->GetLocalSettingsMessage(); return; }
    if (Navigation->Enabled() && !IsFrontend() && !HUDUI)
    {
        HUDUI=CreateWidget<ULDHUD>(this);
        if (!HUDUI) { UE_LOG(LogTemp,Error,TEXT("LD_DEMO_HUD_CREATION_FAILED")); return; }
        HUDUI->AddToViewport(30);
    }
    if (!bNavigationContextActive) { RebuildV05Mappings(); Navigation->bFollowCamera=Controls->bStartFollowing; }
    if (bFreshInputRequired)
    {
        bool Down=LDPlayerInputDetail::PhysicalDown(this,EKeys::LeftMouseButton)||LDPlayerInputDetail::PhysicalDown(this,EKeys::RightMouseButton);
        for (const auto& Binding : Controls->Definitions()) { Down|=LDPlayerInputDetail::PhysicalDown(this,Controls->Key(Binding.Action)); }
        if (!Down) { bFreshInputRequired=false; }
    }
    // A child confirmation or text edit that opens over an armed preview cancels it before the next frame (UI 4).
    if (IsWorldInputCaptured()) { CancelArmedCombat(); }
    FlushQueuedCombat();
    if (GetPawn()!=PreferencesPawn.Get())
    {
        PreferencesPawn=GetPawn();
        if (auto* Body=Cast<ALDCharacter>(GetPawn())) { Body->CameraArm->TargetArmLength=Controls->Zoom; }
    }
    FLDObservedUnitView Selected;
    if (Inspected.IsValid() && !ReadInspected(Selected))
    {
        Inspected.Invalidate();
        InspectedEpoch=0;
        PostIntentNotice(TEXT("目标已不可见"));
    }
    if (HUDUI) { HUDUI->RefreshReadModel(); }
}

void ALDPlayerController::ToggleControlsPanel()
{
    if (ControlsUI && ControlsUI->IsOpen()) { ControlsUI->Close(); return; }
    if (RootUI) { RootUI->DismissMenu(); }
    if (!Controls) { Controls=CastChecked<ULDGameInstance>(GetGameInstance())->GetLocalControlSettings(); }
    if (!Controls) { IntentFeedback=CastChecked<ULDGameInstance>(GetGameInstance())->GetLocalSettingsMessage(); return; }
    if (!ControlsUI)
    {
        ControlsUI=CreateWidget<ULDControlsPanel>(this);
        if (!ControlsUI) { UE_LOG(LogTemp,Error,TEXT("LD_DEMO_CONTROLS_CREATION_FAILED")); return; }
        ControlsUI->AddToViewport(25);
    }
    ControlsUI->Open();
}

void ALDPlayerController::V05Back()
{
    // UI_AND_SETTINGS 4: one layer per press — editing/child confirmation, then a local Armed preview,
    // then a normal main panel, then the menu. Cancelling Armed never stops an accepted order.
    if (!RootUI) { return; }
    if (RootUI->CloseTopModal()) { return; }
    if (CancelArmedCombat()) { return; }
    if (RootUI->CloseTopPanel()) { return; }
    if (!bFreshInputRequired) { RootUI->ToggleMainPanel(ELDUIMainPanel::Menu); }
}

void ALDPlayerController::V05Pressed(FName Action)
{
    if (Action==TEXT("Back")) { V05Back(); return; }
    if (bControlsSuppressed || bFreshInputRequired) { return; }
    // Own panels also open while dead (UI_AND_SETTINGS 3.2); the root keeps one main panel at a time.
    if (Action==TEXT("Attributes")) { if (RootUI) { RootUI->ToggleMainPanel(ELDUIMainPanel::Attributes); } return; }
    // A child confirmation or a text edit owns every other key and click until it closes (UI 4).
    if (IsWorldInputCaptured()) { return; }
    if (Action==TEXT("Stop")) { StopCombatAndOrders(); return; }
    if (Action==TEXT("Recenter") || Action==TEXT("Follow"))
    {
        if (!CanCameraCommand()) { return; }
        // The static death camera has no living body to centre on or follow, and never another player's (CONTROLS 3.3).
        if (IsAwaitingRespawn())
        {
            PostIntentNotice(TEXT("无存活角色"));
            return;
        }
        if (Action==TEXT("Recenter")) { Navigation->Recenter(true); return; }
        Navigation->ToggleFollow();
        Controls->SetAppliedFollowing(Navigation->bFollowCamera);
        CastChecked<ULDGameInstance>(GetGameInstance())->RequestLocalSettingsFlush();
        return;
    }
    if (!CanWorldCommand()) { return; }
    if (Action==TEXT("Select"))
    {
        if (RootUI && RootUI->PointerOverUI()) { return; }
        if (ConfirmArmedCombat()) { return; }
        FGuid Handle;
        uint32 ObservationEpoch=0;
        ObservedUnderPointer(Handle,ObservationEpoch);
        Inspect(Handle,ObservationEpoch);
    }
    else if (Action==TEXT("Navigate"))
    {
        // A right click while something is armed only cancels it; the next right click is an ordinary order.
        if (RootUI && RootUI->PointerOverUI()) { return; }
        if (CancelArmedCombat()) { return; }
        Navigation->ClickMove();
    }
    else if (Action==TEXT("AttackMove")) { ArmAttackMove(); }
    else if (Action==TEXT("Skill1")) { ActivateCombatEntry(ELDCombatEntry::Q); }
    else if (Action==TEXT("Skill2")) { ActivateCombatEntry(ELDCombatEntry::W); }
    else if (Action==TEXT("Item1")) { ActivateCombatEntry(ELDCombatEntry::Item1); }
    else if (Action==TEXT("Item2")) { ActivateCombatEntry(ELDCombatEntry::Item2); }
    else if (Action==TEXT("Dash")) { ActivateCombatEntry(ELDCombatEntry::Dash); }
}

void ALDPlayerController::V05Released(FName Action)
{ if (Action==TEXT("Recenter")) { Navigation->Recenter(false); } }

void ALDPlayerController::V05Zoom(const FInputActionValue& Value)
{
    if (!CanCameraCommand() || !Controls || (RootUI && RootUI->PointerOverUI())) { return; }
    if (auto* Body=Cast<ALDCharacter>(GetPawn()))
    {
        Body->Zoom(Value.Get<float>());
        RecordAppliedCameraZoom();
        return;
    }
    // The dead owner's static camera zooms the applied preference itself; the next body starts from it.
    FString Error;
    if (Controls->SetAppliedZoom(ALDCharacter::ZoomedArmLength(Controls->Zoom,Value.Get<float>()),Error))
    { CastChecked<ULDGameInstance>(GetGameInstance())->RequestLocalSettingsFlush(); }
    else { IntentFeedback=Error; }
}

void ALDPlayerController::RecordAppliedCameraZoom()
{
    const auto* Body=Cast<ALDCharacter>(GetPawn());
    if (!Body || !Controls) { return; }
    FString Error;
    if (Controls->SetAppliedZoom(Body->CameraArm->TargetArmLength,Error))
    { CastChecked<ULDGameInstance>(GetGameInstance())->RequestLocalSettingsFlush(); }
    else { IntentFeedback=Error; }
}

bool ALDPlayerController::ObservedUnderPointer(FGuid& OutHandle,uint32& OutEpoch) const
{
    OutHandle.Invalidate();
    OutEpoch=0;
    const auto* Relay=LocalView();
    FVector2D Pointer;
    if (!Relay || !Navigation || !Navigation->PointerPosition(Pointer)) { return false; }
    const auto Read=Relay->ReadObservedFrame();
    if (!Read.Frame || Read.Status!=ELDObservedViewStatus::Current
        || Read.Frame->MatchGeneration!=OwnerState.Match || Read.Frame->ObserverLife!=OwnerState.Life) { return false; }
    FVector RayStart, RayDirection;
    int32 Width=0, Height=0;
    GetViewportSize(Width,Height);
    const float Fov=PlayerCameraManager ? PlayerCameraManager->GetFOVAngle() : 0.f;
    if (Width<=0 || !(Fov>0.f && Fov<180.f)) { return false; }
    // Horizontal FOV over the viewport width: the angle LDModelPicking::PickTolerancePx pixels subtend at screen centre.
    const double Tolerance=FMath::Atan(2.*FMath::Tan(FMath::DegreesToRadians(Fov)*.5)*LDModelPicking::PickTolerancePx/Width);
    return DeprojectScreenPositionToWorld(float(Pointer.X),float(Pointer.Y),RayStart,RayDirection)
        && Relay->PickPresentedUnit(RayStart,RayDirection,Tolerance,OutHandle,OutEpoch);
}

void ALDPlayerController::Inspect(FGuid Handle,uint32 ObservationEpoch)
{
    Inspected=Handle;
    InspectedEpoch=ObservationEpoch;
    InspectedGeneration=OwnerState.Match;
    FLDObservedUnitView Unit;
    if (!ReadInspected(Unit)) { Inspected.Invalidate(); InspectedEpoch=0; }
}

bool ALDPlayerController::ReadInspected(FLDObservedUnitView& Out) const
{
    const auto* Relay=LocalView();
    if (!Inspected.IsValid() || !InspectedEpoch || InspectedGeneration!=OwnerState.Match || !Relay) { return false; }
    const auto Read=Relay->ReadObservedFrame();
    return Read.Frame && Read.Status==ELDObservedViewStatus::Current
        && Read.Frame->MatchGeneration==OwnerState.Match && Read.Frame->ObserverLife==OwnerState.Life
        && Relay->ReadUnit(Inspected,InspectedEpoch,Out);
}
