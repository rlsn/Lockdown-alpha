#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Core/LDPrototypeTuning.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Framework/LDPlayerController.h"

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

void ALDCharacter::BeginPlay()
{
    Super::BeginPlay();
    Tuning = LoadObject<ULDPrototypeTuning>(nullptr, TEXT("/Game/Lockdown/Generated/Data/DA_PrototypeTuning.DA_PrototypeTuning"));
    FString Error;
    if (!Tuning || !Tuning->Validate(Error)) { UE_LOG(LogTemp, Fatal, TEXT("Invalid prototype tuning: %s"), *Error); return; }
    GetCapsuleComponent()->SetCapsuleSize(Tuning->CapsuleRadius, Tuning->CapsuleHalfHeight);
    GetCharacterMovement()->MaxWalkSpeed = Tuning->WalkSpeed;
    CastChecked<ULDCharacterMovementComponent>(GetCharacterMovement())->SprintSpeed = Tuning->SprintSpeed;
    if (HasAuthority()) { PresentationKey = uint8(FMath::RandRange(0,3)); }
    OnRep_Presentation();
}

void ALDCharacter::Tick(float Delta)
{
    Super::Tick(Delta);
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
            if (Nav->GetRandomReachablePointInRadius(GetActorLocation(), 650, Destination)) { AI->MoveToLocation(Destination.Location, 35); }
        }
    }
}
