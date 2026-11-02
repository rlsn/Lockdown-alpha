#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Net/UnrealNetwork.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapRegistry.h"

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
    Movement->MaxAcceleration = 1800;
    Movement->BrakingDecelerationWalking = 1800;
    Movement->MaxStepHeight = 35;
    Movement->SetWalkableFloorAngle(40);
    Movement->bOrientRotationToMovement = false;
    Movement->bRunPhysicsWithNoController = true;
    Movement->NavAgentProps.bCanJump = false;
    Body = CreateDefaultSubobject<ULDHumanoidVisualComponent>(TEXT("SharedBody"));
    Body->SetupAttachment(GetRootComponent());
    Body->SetVisibility(false,true);
    // The Demo presents bodies only through SharedBody; the inherited, asset-less character mesh never renders.
    GetMesh()->SetHiddenInGame(true);
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
    Error.Reset();
    return true;
}

void ALDCharacter::BeginPlay()
{
    Super::BeginPlay();
    FString Error;
    if (UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap)
    {
        UE_LOG(LogTemp,Error,TEXT("LD_DEMO_BODY_CONFIGURATION_REJECTED Body creation is outside the registered Demo world."));
        Destroy();
        return;
    }
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if (!LDDemoEntry::LoadRuntimeContent(Content,Error) || !ConfigureDemoMovement(*Content,Error))
    {
        UE_LOG(LogTemp,Error,TEXT("LD_DEMO_BODY_CONFIGURATION_REJECTED %s"),*Error);
        Destroy();
        return;
    }
    if (HasAuthority()) { GetCharacterMovement()->SetComponentTickEnabled(false); }
    if (HasAuthority()) { PresentationKey = uint8(FMath::RandRange(0,3)); }
    OnRep_Presentation();
}

void ALDCharacter::Tick(float Delta)
{
    Super::Tick(Delta);
    const auto* PC=Cast<ALDPlayerController>(GetController());
    const bool OwnBody=PC && PC->GetLocalPlayer() && PC->GetPawn()==this
        && PC->OwnerState.LifePhase==ELDLifePhase::Alive && PC->OwnerState.BodyRef.IsValid();
    Body->SetVisibility(OwnBody,true);
    if (OwnBody) { Body->Animate(GetVelocity(),Delta); }
}

void ALDCharacter::SetFacing(float Yaw) { if (FMath::IsFinite(Yaw)) { SetActorRotation(FRotator(0, FRotator::NormalizeAxis(Yaw), 0)); } }
void ALDCharacter::Zoom(float Axis) { CameraArm->TargetArmLength = FMath::Clamp(CameraArm->TargetArmLength - Axis * 80, 1100.0f, 1700.0f); }
void ALDCharacter::OnRep_Presentation() { Body->ApplyPalette(PresentationKey); }
void ALDCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ALDCharacter, PresentationKey);
}
