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
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Tests/LDCombatNetworkProbe.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Tests/LDObjectiveNetworkProbe.h"
#include "Tests/LDSoakProbe.h"

namespace
{
    bool LDNetworkProbeEnabled()
    {return FParse::Param(FCommandLine::Get(),TEXT("LDViewProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDCombatNetProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDObjectiveNetProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDSoakProbe"));}
}

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
    if(GetNetMode()==NM_Client)
    {
    if (!IsFrontend() && FParse::Param(FCommandLine::Get(),TEXT("LDViewProbe")))
    { FActorSpawnParameters Params; Params.Owner=this; GetWorld()->SpawnActor<ALDViewNetworkProbe>(ALDViewNetworkProbe::StaticClass(),FTransform::Identity,Params); }
    if(!IsFrontend()&&FParse::Param(FCommandLine::Get(),TEXT("LDCombatNetProbe")))
    {FActorSpawnParameters Params;Params.Owner=this;GetWorld()->SpawnActor<ALDCombatNetworkProbe>(ALDCombatNetworkProbe::StaticClass(),FTransform::Identity,Params);}
    if(!IsFrontend()&&FParse::Param(FCommandLine::Get(),TEXT("LDObjectiveNetProbe")))
    {FActorSpawnParameters Params;Params.Owner=this;GetWorld()->SpawnActor<ALDObjectiveNetworkProbe>(ALDObjectiveNetworkProbe::StaticClass(),FTransform::Identity,Params);}
    if(!IsFrontend()&&FParse::Param(FCommandLine::Get(),TEXT("LDSoakProbe")))
    {FActorSpawnParameters Params;Params.Owner=this;GetWorld()->SpawnActor<ALDSoakProbe>(ALDSoakProbe::StaticClass(),FTransform::Identity,Params);}
    }
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
    Input->BindAction(Action(TEXT("Attack")),ETriggerEvent::Started,this,&ALDPlayerController::Attack);
    Input->BindAction(Action(TEXT("Assassinate")),ETriggerEvent::Started,this,&ALDPlayerController::Assassinate);
    Input->BindAction(Action(TEXT("Dash")),ETriggerEvent::Started,this,&ALDPlayerController::Dash);
    Input->BindAction(Action(TEXT("Interact")),ETriggerEvent::Started,this,&ALDPlayerController::InteractStarted);
    Input->BindAction(Action(TEXT("Interact")),ETriggerEvent::Completed,this,&ALDPlayerController::InteractStopped);
    Input->BindAction(Action(TEXT("Interact")),ETriggerEvent::Canceled,this,&ALDPlayerController::InteractStopped);
}

void ALDPlayerController::OnRep_OwnerState()
{
    if(OwnerState.Match<LatestOwner.Match || (OwnerState.Match==LatestOwner.Match&&OwnerState.Revision<LatestOwner.Revision)){OwnerState=LatestOwner;return;}
    LatestOwner=OwnerState;
}
void ALDPlayerController::ClientLifeReceipt_Implementation(const FLDOwnerState& State)
{
    if(State.Match<OwnerState.Match || (State.Match==OwnerState.Match&&State.Revision<OwnerState.Revision)){return;}
    OwnerState=State;OnRep_OwnerState();
}
void ALDPlayerController::Attack(){CombatAction(0);}
void ALDPlayerController::Assassinate(){CombatAction(1);}
void ALDPlayerController::Dash(){CombatAction(2);}
void ALDPlayerController::CombatAction(uint8 Action)
{
    ALDCharacter* Body=Cast<ALDCharacter>(GetPawn());
    if(bControlsSuppressed||!OwnerState.bActive||!Body){return;}
    FVector Direction=Body->GetActorForwardVector();
    if(Action==2){const FVector Move=Body->GetLastMovementInputVector();if(!Move.IsNearlyZero()){Direction=Move.GetSafeNormal2D();}}
    IntentFeedback=TEXT("Action requested; awaiting authority.");
    ServerCombatAction(OwnerState.Match,OwnerState.Life,++NextRequest,Action,Direction);
}
void ALDPlayerController::ServerCombatAction_Implementation(uint32 Match,uint32 Life,uint32 Request,uint8 Action,FVector_NetQuantizeNormal Direction)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* State=Mode?Mode->StateFor(this):nullptr;
    if(!State||Match!=State->Match||Life!=State->Life||!Request||Action>2||Direction.ContainsNaN()||FMath::Abs(Direction.Z)>.001||Direction.SizeSquared()<.9||Direction.SizeSquared()>1.1)
    {ClientIntentResult(Request,TEXT("Invalid action or stale life."));return;}
    if(const FString* Cached=CombatReceipts.Find(Request)){ClientIntentResult(Request,*Cached);return;}
    if(Mode->IsFinished()){ClientIntentResult(Request,TEXT("Demo finished."));return;}
    if(Request<LastCombatRequest){ClientIntentResult(Request,TEXT("Expired action request."));return;}
    const double Now=GetWorld()->GetTimeSeconds();
    CombatTokens=FMath::Min(3.,CombatTokens+FMath::Max(0.,Now-LastCombatAt)*12);LastCombatAt=Now;
    if(CombatTokens<1){ClientIntentResult(Request,TEXT("Action rate limited."));return;}
    --CombatTokens;LastCombatRequest=Request;LastCombatResult=TEXT("Action queued.");
    if(CombatReceipts.Num()>=32){uint32 Oldest=MAX_uint32;for(const auto& Pair:CombatReceipts){Oldest=FMath::Min(Oldest,Pair.Key);}CombatReceipts.Remove(Oldest);}
    CombatReceipts.Add(Request,LastCombatResult);
    if(Mode->Combat){Mode->Combat->Queue(this,Match,Life,Request,Action,Direction);}
    else{CompleteCombatRequest(Request,TEXT("Combat unavailable."));}
}
void ALDPlayerController::CompleteCombatRequest(uint32 Request,const FString& Result)
{CombatReceipts.Add(Request,Result);if(Request==LastCombatRequest){LastCombatResult=Result;}ClientIntentResult(Request,Result);}

bool ALDPlayerController::ChooseContext(uint8& Target,FGuid& Observed) const
{
    const APawn* Body=GetPawn();if(!Body){return false;}const FVector P=Body->GetActorLocation();
    if(!OwnerState.SealId.IsValid())
    {
        double Distance=80*80+1.;FGuid Closest;
        if(const auto* Relay=LocalView()){for(const auto& E:Relay->LastView().Entities)
        {const double D=FVector::DistSquared2D(P,E.Position);if(E.Kind==ELDObservedKind::Seal&&(D<Distance||(D==Distance&&E.Id.ToString()<Closest.ToString()))){Distance=D;Closest=E.Id;}}}
        if(Closest.IsValid()){Target=LDObjective::Seal;Observed=Closest;return true;}
    }
    Observed.Invalidate();
    if(FVector::DistSquared2D(P,LDObjective::Position(LDObjective::Heart))<=120*120){Target=LDObjective::Heart;return true;}
    const double Guild=FVector::DistSquared2D(P,LDObjective::Position(LDObjective::Guild)),Station=FVector::DistSquared2D(P,LDObjective::Position(LDObjective::Station));
    if(FMath::Min(Guild,Station)<=120*120){Target=Guild<=Station?LDObjective::Guild:LDObjective::Station;return true;}
    return false;
}
FString ALDPlayerController::ContextPrompt() const
{
    const auto* GS=GetWorld()->GetGameState<ALDGameState>();
    if(GS&&GS->EndReason){return GS->EndReason==2?TEXT("No claim. Demo finished."):(OwnerState.bWinner?TEXT("Heart claimed. You won."):TEXT("Heart claimed by another contender."));}
    if(!OwnerState.bActive){return TEXT("Waiting: owned ward and shared sight only.");}
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    if(Now<OwnerState.ProtectedUntil){return TEXT("Return protection: gameplay actions locked.");}
    uint8 Target=0;FGuid Observed;if(!ChooseContext(Target,Observed)){return TEXT("Approach Guild [G], West station [W] or Heart [H]. E interacts within 120 cm.");}
    if(Target==LDObjective::Seal){return TEXT("E: pick up visible Concord Seal within 80 cm.");}
    if(Target==LDObjective::Heart)
    {return GS&&LDRules::HasHeartAccess(OwnerState.SealId.IsValid(),GS->DefensiveFlags)?TEXT("Hold E: claim Heart for 10 seconds. Other unprotected contenders contest within 240 cm."):TEXT("Heart locked: carry a Seal or clear all four defences through the labelled fixture.");}
    if(Target==LDObjective::Guild)
    {
        if(OwnerState.ContractStage==2){return TEXT("Hold E: deliver courier package for 3 seconds (+25 gold, +15 XP).");}
        if(OwnerState.ContractStage==1){return TEXT("Courier active: collect at West station [W].");}
        if(Now<OwnerState.WorkReadyAt){return FString::Printf(TEXT("Courier acceptance ready in %.1fs."),OwnerState.WorkReadyAt-Now);}
        return TEXT("E: accept courier contract. Then hold E at West station [W] to collect.");
    }
    return OwnerState.ContractStage==1?TEXT("Hold E: collect courier package for 3 seconds."):TEXT("West station: accept a courier contract at Guild [G] first.");
}
void ALDPlayerController::InteractStarted()
{
    if(bControlsSuppressed||!OwnerState.bActive){return;}uint8 Target=0;FGuid Observed;
    if(!ChooseContext(Target,Observed)){IntentFeedback=ContextPrompt();return;}
    bInteractHeld=true;ContextToken=++NextRequest;HoldSequence=0;LastHoldSent=-1000;
    ServerContextRequest(OwnerState.Match,OwnerState.Life,ContextToken,Target,Observed);
}
void ALDPlayerController::InteractStopped()
{
    if(bInteractHeld){ServerContextRelease(OwnerState.Match,OwnerState.Life,ContextToken);}bInteractHeld=false;
}
void ALDPlayerController::ServerContextRequest_Implementation(uint32 Match,uint32 Life,uint32 Request,uint8 Target,FGuid Observed)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode?Mode->StateFor(this):nullptr;
    if(!O||O->Match!=Match||O->Life!=Life||!Request||Target<1||Target>4||(Target==4)!=Observed.IsValid())
    {ClientIntentResult(Request,TEXT("Invalid interaction or stale life."));return;}
    if(const auto* Receipt=ContextReceipts.Find(Request)){ClientTransactionResult(OwnerState,Request,*Receipt);return;}
    if(Mode->IsFinished()){ClientIntentResult(Request,TEXT("Demo finished."));return;}
    if(Request<LastContextRequest){ClientIntentResult(Request,TEXT("Expired interaction request."));return;}
    const double Now=GetWorld()->GetTimeSeconds();ContextTokens=FMath::Min(2.,ContextTokens+FMath::Max(0.,Now-ContextRateAt)*6);ContextRateAt=Now;
    if(ContextTokens<1){ClientIntentResult(Request,TEXT("Interaction rate limited."));return;}
    --ContextTokens;LastContextRequest=Request;
    if(ContextReceipts.Num()>=32){uint32 Oldest=MAX_uint32;for(const auto& Pair:ContextReceipts){Oldest=FMath::Min(Oldest,Pair.Key);}ContextReceipts.Remove(Oldest);}
    ContextReceipts.Add(Request,TEXT("Interaction queued."));
    if(Mode->Objectives){Mode->Objectives->Queue(this,Match,Life,Request,Target,Observed);}else{CompleteContextRequest(Request,TEXT("Interactions unavailable."));}
}
void ALDPlayerController::ServerContextHold_Implementation(uint32 Match,uint32 Life,uint32 Token,uint32 Sequence,bool Moving)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode?Mode->StateFor(this):nullptr;const double Now=GetWorld()->GetTimeSeconds();
    if(!O||O->Match!=Match||O->Life!=Life||Now-LastHoldReceived<.04){return;}LastHoldReceived=Now;
    if(Mode->Objectives&&!Mode->IsFinished()){Mode->Objectives->RefreshHold(this,Life,Token,Sequence,Moving,Now);}
}
void ALDPlayerController::ServerContextRelease_Implementation(uint32 Match,uint32 Life,uint32 Token)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode?Mode->StateFor(this):nullptr;
    if(O&&O->Match==Match&&O->Life==Life&&Mode->Objectives){Mode->Objectives->Release(this,Life,Token);}
}
void ALDPlayerController::CompleteContextRequest(uint32 Request,const FString& Result)
{
    ContextReceipts.Add(Request,Result);ClientTransactionResult(OwnerState,Request,Result);
}
void ALDPlayerController::ClientTransactionResult_Implementation(const FLDOwnerState& State,uint32 Request,const FString& Result)
{ClientLifeReceipt_Implementation(State);ClientIntentResult_Implementation(Request,Result);}

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
void ALDPlayerController::ClientIntentResult_Implementation(uint32 Request,const FString& Reason)
{
    IntentFeedback=Reason;
#if !UE_BUILD_SHIPPING
    if(double* Sent=ProbeRequests.Find(Request)){ProbeAckMs.Add((FPlatformTime::Seconds()-*Sent)*1000);ProbeRequests.Remove(Request);}
#endif
}
void ALDPlayerController::ClientProbePhase_Implementation(uint8 Phase)
{
#if !UE_BUILD_SHIPPING
    if (Phase<=16 && LDNetworkProbeEnabled()) { ProbePhase=Phase; }
#endif
}
void ALDPlayerController::ServerProbeHello_Implementation(uint8 ProbeIdentity)
{
#if !UE_BUILD_SHIPPING
    const ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (Mode && Mode->bScaffoldProfile && !ProbeRole && ProbeIdentity>=1 && ProbeIdentity<=2 && LDNetworkProbeEnabled()) { ProbeRole=ProbeIdentity; }
#endif
}
void ALDPlayerController::ServerProbePing_Implementation(double Sent)
{
#if !UE_BUILD_SHIPPING
    if(FMath::IsFinite(Sent)&&LDNetworkProbeEnabled()){ClientProbePong(Sent);}
#endif
}
void ALDPlayerController::ClientProbePong_Implementation(double Sent)
{
#if !UE_BUILD_SHIPPING
    if(FMath::IsFinite(Sent)&&LDNetworkProbeEnabled()){ProbeRtts.Add((FPlatformTime::Seconds()-Sent)*1000);}
#endif
}
void ALDPlayerController::TogglePublicSensorFixture()
{
#if !UE_BUILD_SHIPPING
    ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (HasAuthority() && IsLocalController() && Mode && Mode->bScaffoldProfile && Mode->Vision&&!Mode->IsFinished())
    { Mode->Vision->SetPublicSensor(!Mode->Vision->IsPublicSensorActive()); }
#endif
}
void ALDPlayerController::ToggleDefenceFixture(uint8 Index)
{
#if !UE_BUILD_SHIPPING
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* GS=GetWorld()->GetGameState<ALDGameState>();
    if(HasAuthority()&&IsLocalController()&&Mode&&Mode->Objectives&&Index<4){Mode->Objectives->SetDefencesFixture(GS->DefensiveFlags^(1<<Index));}
#endif
}
void ALDPlayerController::SeedSealFixture()
{
#if !UE_BUILD_SHIPPING
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(HasAuthority()&&IsLocalController()&&Mode&&Mode->Objectives)
    {IntentFeedback=Mode->Objectives->SeedFixtureSeal()?TEXT("Fixture: one dropped Seal seeded at Guild. Pickup/drop/claim are real."):TEXT("Fixture seed unavailable: already seeded or demo finished.");}
#endif
}

void ALDPlayerController::Move(const FInputActionValue& Value)
{
    if (bControlsSuppressed || !OwnerState.bActive || (GetWorld()->GetGameState<ALDGameState>()&&GetWorld()->GetGameState<ALDGameState>()->EndReason)) { return; }
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
    InteractStopped();
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
    if(IsLocalController()&&bInteractHeld&&GetWorld()->GetTimeSeconds()-LastHoldSent>=.1)
    {
        LastHoldSent=GetWorld()->GetTimeSeconds();const APawn* Body=GetPawn();
        ServerContextHold(OwnerState.Match,OwnerState.Life,ContextToken,++HoldSequence,Body&&!Body->GetLastMovementInputVector().IsNearlyZero());
    }
    const auto* PublicState=GetWorld()->GetGameState<ALDGameState>();const bool bFinished=PublicState&&PublicState->EndReason;
    if(bFinished)
    {if(IsLocalController()){bInteractHeld=false;if(auto* Body=Cast<ALDCharacter>(GetPawn())){Body->GetCharacterMovement()->DisableMovement();}}}
    if(IsLocalController()&&!IsFrontend())
    {
        if(ALDCharacter* Active=Cast<ALDCharacter>(GetPawn()))
        {
            LastBodyPosition=Active->GetActorLocation();
            if(WaitingCamera){SetViewTarget(Active);WaitingCamera->Destroy();WaitingCamera=nullptr;}
            auto* Move=CastChecked<ULDCharacterMovementComponent>(Active->GetCharacterMovement());
            Move->bCombatWalkOnly=(OwnerState.ActionCue&15)==2||(OwnerState.ActionCue&15)==3;
            Move->bCanWalkOffLedges=(OwnerState.ActionCue&15)!=4;
        }
        else if(!OwnerState.bActive&&!WaitingCamera)
        {
            WaitingCamera=GetWorld()->SpawnActor<ACameraActor>();
            const FRotator Rotation(-60,-45,0);
            WaitingCamera->SetActorLocation(LastBodyPosition+FVector(0,0,20)-Rotation.Vector()*1400);
            WaitingCamera->SetActorRotation(Rotation);
            auto* Camera=WaitingCamera->GetCameraComponent();Camera->FieldOfView=55;
            Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness=true;Camera->PostProcessSettings.AutoExposureMinBrightness=0;
            Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness=true;Camera->PostProcessSettings.AutoExposureMaxBrightness=0;
            Camera->PostProcessSettings.bOverride_MotionBlurAmount=true;Camera->PostProcessSettings.MotionBlurAmount=0;
            SetViewTarget(WaitingCamera);
        }
    }
    if (bFinished || !IsLocalController() || bControlsSuppressed || !OwnerState.bActive) { return; }
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
