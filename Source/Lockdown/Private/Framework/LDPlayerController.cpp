#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "UI/LDUIRoot.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Tests/LDViewNetworkProbe.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool ALDPlayerController::IsFrontend() const { return GetWorld()->GetMapName().Contains(TEXT("L_Frontend")); }

void ALDPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) { return; }
    bShowMouseCursor = true;
    if (ULocalPlayer* Local = GetLocalPlayer())
    {
        UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Lockdown/Generated/Input/IMC_Gameplay.IMC_Gameplay"));
        if (ensure(Context)) { Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()->AddMappingContext(Context, 0); }
    }
    const TSubclassOf<ULDUIRoot> RootClass = LoadClass<ULDUIRoot>(nullptr,TEXT("/Game/Lockdown/Generated/UI/WBP_UIRoot.WBP_UIRoot_C"));
    checkf(RootClass,TEXT("Missing UI root; run asset bootstrap"));
    RootUI = CreateWidget<ULDUIRoot>(this, RootClass);
    RootUI->AddToViewport();
    SetMenuOpen(IsFrontend());
#if !UE_BUILD_SHIPPING
    if (!IsFrontend() && FParse::Param(FCommandLine::Get(),TEXT("LDViewProbe")))
    { FActorSpawnParameters Params; Params.Owner=this; GetWorld()->SpawnActor<ALDViewNetworkProbe>(ALDViewNetworkProbe::StaticClass(),FTransform::Identity,Params); }
#endif
}

void ALDPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(InputComponent);
    auto Action = [](const TCHAR* Name)
    {
        return LoadObject<UInputAction>(nullptr, *(FString(TEXT("/Game/Lockdown/Generated/Input/IA_")) + Name + TEXT(".IA_") + Name));
    };
    Input->BindAction(Action(TEXT("Move")), ETriggerEvent::Triggered, this, &ALDPlayerController::Move);
    Input->BindAction(Action(TEXT("Sprint")), ETriggerEvent::Started, this, &ALDPlayerController::SprintStarted);
    Input->BindAction(Action(TEXT("Sprint")), ETriggerEvent::Completed, this, &ALDPlayerController::SprintStopped);
    Input->BindAction(Action(TEXT("Sprint")), ETriggerEvent::Canceled, this, &ALDPlayerController::SprintStopped);
    Input->BindAction(Action(TEXT("Zoom")), ETriggerEvent::Triggered, this, &ALDPlayerController::Zoom);
    Input->BindAction(Action(TEXT("Menu")), ETriggerEvent::Started, this, &ALDPlayerController::ToggleMenu);
    Input->BindAction(Action(TEXT("Help")), ETriggerEvent::Started, this, &ALDPlayerController::ToggleMenu);
    Input->BindAction(Action(TEXT("Ward")), ETriggerEvent::Started, this, &ALDPlayerController::Ward);
}

void ALDPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ALDPlayerController,OwnerState); }
ALDWorldViewRelay* ALDPlayerController::LocalView() const
{
    for (TActorIterator<ALDWorldViewRelay> It(GetWorld());It;++It) { if (It->GetOwner()==this) { return *It; } }
    return nullptr;
}
void ALDPlayerController::Ward()
{
    if (bControlsSuppressed) { return; }
    FHitResult Hit;
    if (GetHitResultUnderCursor(ECC_GameTraceChannel1,false,Hit)) { ServerPlaceWard(OwnerState.Match,OwnerState.Life,++NextRequest,Hit.ImpactPoint); }
}
void ALDPlayerController::ServerPlaceWard_Implementation(uint32 Match,uint32 Life,uint32 Request,FVector_NetQuantize10 Position)
{
    const double Now=GetWorld()->GetTimeSeconds();
    if (Now-LastWardAt<.25) { return; }
    LastWardAt=Now;
    ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    FLDOwnerState* State=Mode ? Mode->StateFor(this) : nullptr;
    if (!State || Match!=State->Match || Life!=State->Life || !Request)
    { ClientIntentResult(Request,TEXT("Stale session or life.")); return; }
    if (Request==LastWardRequest) { ClientIntentResult(Request,LastWardResult); return; }
    if (Request<LastWardRequest) { ClientIntentResult(Request,TEXT("Expired request.")); return; }
    LastWardRequest=Request;
    LastWardResult=TEXT("Sight system unavailable.");
    if (Mode->Vision) { Mode->Vision->PlaceWard(this,Position,LastWardResult); }
    ClientIntentResult(Request,LastWardResult);
}
void ALDPlayerController::ClientIntentResult_Implementation(uint32 Request,const FString& Reason) { IntentFeedback=Reason; }
void ALDPlayerController::ClientProbePhase_Implementation(uint8 Phase)
{
#if !UE_BUILD_SHIPPING
    if (Phase<=9 && FParse::Param(FCommandLine::Get(),TEXT("LDViewProbe"))) { ProbePhase=Phase; }
#endif
}
void ALDPlayerController::ServerProbeHello_Implementation(uint8 ProbeIdentity)
{
#if !UE_BUILD_SHIPPING
    const ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (Mode && Mode->bScaffoldProfile && !ProbeRole && ProbeIdentity>=1 && ProbeIdentity<=2 && FParse::Param(FCommandLine::Get(),TEXT("LDViewProbe"))) { ProbeRole=ProbeIdentity; }
#endif
}
void ALDPlayerController::TogglePublicSensorFixture()
{
#if !UE_BUILD_SHIPPING
    ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (HasAuthority() && IsLocalController() && Mode && Mode->bScaffoldProfile && Mode->Vision)
    { Mode->Vision->SetPublicSensor(!Mode->Vision->IsPublicSensorActive()); }
#endif
}

void ALDPlayerController::Move(const FInputActionValue& Value)
{
    if (bControlsSuppressed || !OwnerState.bActive) { return; }
    if (APawn* ControlledPawn = GetPawn())
    {
        const FVector2D Axis = Value.Get<FVector2D>().GetClampedToMaxSize(1);
        const FRotator CameraYaw(0,-45,0);
        const FVector Direction = CameraYaw.RotateVector(FVector(Axis.Y,Axis.X,0));
        ControlledPawn->AddMovementInput(Direction.GetSafeNormal(), float(Direction.Size()));
    }
}

void ALDPlayerController::SprintStarted() { if (!bControlsSuppressed) { if (ALDCharacter* Body = Cast<ALDCharacter>(GetPawn())) { Body->SetSprint(true); } } }
void ALDPlayerController::SprintStopped() { if (ALDCharacter* Body = Cast<ALDCharacter>(GetPawn())) { Body->SetSprint(false); } }
void ALDPlayerController::Zoom(const FInputActionValue& Value) { if (!bControlsSuppressed) { if (ALDCharacter* Body = Cast<ALDCharacter>(GetPawn())) { Body->Zoom(Value.Get<float>()); } } }
void ALDPlayerController::ToggleMenu() { if (RootUI && !IsFrontend()) { RootUI->ToggleMenu(); } }

void ALDPlayerController::ClearControls()
{
    SprintStopped();
    if (ALDCharacter* Body = Cast<ALDCharacter>(GetPawn()))
    {
        Body->ConsumeMovementInputVector();
        Body->GetCharacterMovement()->StopMovementImmediately();
    }
}

void ALDPlayerController::FlushPressedKeys() { Super::FlushPressedKeys(); ClearControls(); }

void ALDPlayerController::SetMenuOpen(bool bOpen)
{
    if (bControlsSuppressed != bOpen)
    {
        bControlsSuppressed = bOpen;
        SetIgnoreMoveInput(bOpen);
    }
    ClearControls();
    if (bOpen)
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        if (RootUI) { Mode.SetWidgetToFocus(RootUI->TakeWidget()); }
        SetInputMode(Mode);
    }
    else
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(Mode);
    }
}

void ALDPlayerController::PlayerTick(float Delta)
{
    Super::PlayerTick(Delta);
    if (!IsLocalController() || bControlsSuppressed || !OwnerState.bActive) { return; }
    ALDCharacter* Body = Cast<ALDCharacter>(GetPawn());
    if (!Body) { return; }
    FHitResult Hit;
    if (GetHitResultUnderCursor(ECC_GameTraceChannel1, false, Hit) && Hit.ImpactNormal.Z >= FMath::Cos(FMath::DegreesToRadians(40.0)))
    {
        FVector Direction = Hit.ImpactPoint - Body->GetActorLocation();
        Direction.Z = 0;
        if (!Direction.IsNearlyZero() && !Direction.ContainsNaN())
        {
            const float Yaw = Direction.Rotation().Yaw;
            Body->SetFacing(Yaw);
            AimSendTime += Delta;
            if (AimSendTime >= .1f) { AimSendTime = 0; ServerSetAim(OwnerState.Match,OwnerState.Life,Yaw); }
        }
    }
}

void ALDPlayerController::ServerSetAim_Implementation(uint32 Match,uint32 Life,float Yaw)
{
    const double Now = GetWorld()->GetTimeSeconds();
    ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    const FLDOwnerState* State=Mode ? Mode->StateFor(this) : nullptr;
    if (!State || !State->bActive || Match!=State->Match || Life!=State->Life || !FMath::IsFinite(Yaw) || Now - LastAcceptedAimTime < .04) { return; }
    LastAcceptedAimTime = Now;
    if (ALDCharacter* Body = Cast<ALDCharacter>(GetPawn())) { Body->SetFacing(Yaw); }
}
