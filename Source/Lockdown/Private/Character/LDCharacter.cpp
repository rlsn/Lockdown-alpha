#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Core/LDPrototypeTuning.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapRegistry.h"
#include "Combat/LDCombatDirector.h"
#include "Region/LDRegionDirector.h"

ALDCharacter::ALDCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<ULDCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    bOnlyRelevantToOwner = true;
    bAlwaysRelevant = false;
    bUseControllerRotationYaw = false;
    GetCapsuleComponent()->InitCapsuleSize(28,90);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->MaxWalkSpeed = 180;
    Movement->MaxAcceleration = 1800;
    Movement->BrakingDecelerationWalking = 1800;
    Movement->MaxStepHeight = 35;
    Movement->SetWalkableFloorAngle(40);
    Movement->bOrientRotationToMovement = false;
    Movement->bRunPhysicsWithNoController = true;
    Movement->NavAgentProps.bCanJump = false;
    Body = CreateDefaultSubobject<ULDHumanoidVisualComponent>(TEXT("SharedBody"));
    Body->SetupAttachment(GetRootComponent());
    PortableGlow=CreateDefaultSubobject<UPointLightComponent>(TEXT("OwnerCosmeticToolLight"));PortableGlow->SetupAttachment(GetRootComponent());PortableGlow->SetMobility(EComponentMobility::Movable);PortableGlow->SetCastShadows(false);PortableGlow->SetIntensity(1500);PortableGlow->SetLightColor(FLinearColor(1.f,.65f,.25f));PortableGlow->SetVisibility(false);
    CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    CameraArm->SetupAttachment(GetRootComponent());
    CameraArm->SetUsingAbsoluteRotation(true);
    CameraArm->SetRelativeRotation(FRotator(-60,-45,0));
    CameraArm->TargetArmLength = 1400;
    CameraArm->TargetOffset = FVector(0,0,20);
    CameraArm->bDoCollisionTest = false;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraArm);
    Camera->FieldOfView = 55;
    Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
    Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
    Camera->PostProcessSettings.AutoExposureMinBrightness = 0;
    Camera->PostProcessSettings.AutoExposureMaxBrightness = 0;
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Camera->PostProcessSettings.MotionBlurAmount = 0;
}

void ALDCharacter::SetPortableLight(uint8 Mode)
{PortableGlow->SetAttenuationRadius(Mode==2?800:600);PortableGlow->SetVisibility(Mode==1||Mode==2);}

bool ALDCharacter::ConfigureDemoMovement(const FLDDemoContentSnapshot& Content, FString& Error)
{
    FCollisionShape Capsule;
    if (!ULDMapRegistry::ContentCapsule(Content, Capsule, Error)) { return false; }
    const auto& Speed = Content.Get().Parameters.MoveSpeedCmPerSec.Base;
    if (Speed.Denominator <= 0)
    {
        Error = TEXT("Demo movement speed has a nonpositive denominator.");
        return false;
    }
    const double NativeSpeed = double(Speed.Numerator) / double(Speed.Denominator);
    if (!FMath::IsFinite(NativeSpeed) || NativeSpeed <= 0 || NativeSpeed > MAX_flt)
    {
        Error = TEXT("Demo movement speed is outside the native movement range.");
        return false;
    }
    auto* Movement = CastChecked<ULDCharacterMovementComponent>(GetCharacterMovement());
    GetCapsuleComponent()->SetCapsuleSize(Capsule.GetCapsuleRadius(), Capsule.GetCapsuleHalfHeight());
    Movement->NavAgentProps.AgentRadius = Capsule.GetCapsuleRadius();
    Movement->NavAgentProps.AgentHeight = Capsule.GetCapsuleHalfHeight() * 2;
    Movement->MaxWalkSpeed = float(NativeSpeed);
    Movement->SprintSpeed = float(NativeSpeed);
    Movement->bWantsSprint = false;
    Error.Reset();
    return true;
}

void ALDCharacter::BeginPlay()
{
    Super::BeginPlay();
    FString Error;
    if (UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) == LDDemoEntry::WorldMap)
    {
        TSharedPtr<const FLDDemoContentSnapshot> Content;
        if (!LDDemoEntry::LoadRuntimeContent(Content, Error) || !ConfigureDemoMovement(*Content, Error))
        {
            UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BODY_CONFIGURATION_REJECTED %s"), *Error);
            Destroy();
            return;
        }
        if (HasAuthority()) { GetCharacterMovement()->SetComponentTickEnabled(false); }
    }
    else
    {
        Tuning = LoadObject<ULDPrototypeTuning>(nullptr, TEXT("/Game/Lockdown/Generated/Data/DA_PrototypeTuning.DA_PrototypeTuning"));
        if (!Tuning || !Tuning->Validate(Error)) { UE_LOG(LogTemp, Fatal, TEXT("Invalid prototype tuning: %s"), *Error); return; }
        GetCapsuleComponent()->SetCapsuleSize(Tuning->CapsuleRadius, Tuning->CapsuleHalfHeight);
        GetCharacterMovement()->MaxWalkSpeed = Tuning->WalkSpeed;
        CastChecked<ULDCharacterMovementComponent>(GetCharacterMovement())->SprintSpeed = Tuning->SprintSpeed;
    }
    if (HasAuthority()) { PresentationKey = uint8(FMath::RandRange(0,3)); }
    OnRep_Presentation();
    if(HasAuthority()){if(auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();Mode&&!Mode->IsDemoRoom()){if(Mode->Combat){Mode->Combat->Register(this);}}}
}

void ALDCharacter::Tick(float Delta)
{
    Super::Tick(Delta);
    if(!GetController()||!GetController()->IsLocalController()){SetPortableLight(0);}
    if(HasAuthority()){if(auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>()){if(Mode->Combat){Body->SetCue(Mode->Combat->CueFor(this));}}}
    else if(const auto* PC=Cast<ALDPlayerController>(GetController())){Body->SetCue(PC->OwnerState.ActionCue);}
    Body->Animate(GetVelocity(), Delta);
    if (const ALDPlayerController* PC=Cast<ALDPlayerController>(GetController()))
    { if (PC->IsLocalController()) { Body->SetVisibility(PC->OwnerState.bActive,true); } }
}

void ALDCharacter::SetSprint(bool bSprint) { CastChecked<ULDCharacterMovementComponent>(GetCharacterMovement())->bWantsSprint = bSprint; }
void ALDCharacter::SetFacing(float Yaw) { if (FMath::IsFinite(Yaw)) { SetActorRotation(FRotator(0, FRotator::NormalizeAxis(Yaw), 0)); } }
void ALDCharacter::Zoom(float Axis) { CameraArm->TargetArmLength = FMath::Clamp(CameraArm->TargetArmLength - Axis * 80, 1100.0f, 1700.0f); }
void ALDCharacter::OnRep_Presentation() { Body->ApplyPalette(PresentationKey); }
void ALDCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ALDCharacter, PresentationKey);
}

ALDResident::ALDResident(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    bReplicates = false;
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    AIControllerClass = AAIController::StaticClass();
}

void ALDResident::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!HasAuthority()) { return; }
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();if(Mode&&Mode->Region&&Mode->Region->UpdateResident(this)){return;}
    if (GetVelocity().Size2D() > 5) { SetFacing(GetVelocity().Rotation().Yaw); }
    NextDecision -= Delta;
    if (NextDecision > 0) { return; }
    NextDecision = FMath::FRandRange(3.0f,7.0f);
    if (AAIController* AI = Cast<AAIController>(GetController()))
    {
        AI->SetReplicates(false);
        if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
        {
            FNavLocation Destination;
            if (Nav->GetRandomReachablePointInRadius(Mode&&Mode->Region&&Mode->Combat->StateFor(this)?Mode->Combat->StateFor(this)->Home:GetActorLocation(),Mode&&Mode->Region?250:650,Destination)) { AI->MoveToLocation(Destination.Location, 35); }
        }
    }
}
