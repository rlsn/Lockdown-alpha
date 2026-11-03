#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Net/UnrealNetwork.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDDemoEntry.h"
#include "Combat/LDCombatCues.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
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
    OnRep_Presentation();
}

void ALDCharacter::Tick(float Delta)
{
    Super::Tick(Delta);
    const auto* PC=Cast<ALDPlayerController>(GetController());
    const bool OwnBody=PC && PC->GetLocalPlayer() && PC->GetPawn()==this
        && PC->OwnerState.LifePhase==ELDLifePhase::Alive && PC->OwnerState.BodyRef.IsValid();
    Body->SetVisibility(OwnBody,true);
    if (!OwnBody) { return; }
    // The own body shows the cue and markers others see, from its own Combat projection at the public clock.
    const FLDCombatOwnerView& View=PC->CombatView;
    int64 NowMs=0;
    ELDObservedActionCue Cue=ELDObservedActionCue::None;
    TArray<FLDObservedStatus> Statuses;
    if (PC->ReadDemoAuthorityTimeMs(NowMs) && View.bHasBody && View.Generation==PC->OwnerState.Match
        && View.Life==PC->OwnerState.Life)
    {
        Cue=LDCombatCues::Select(View.Cues,NowMs).Cue;
        for (const FLDCombatStatusView& Status : View.Statuses)
        {
            if (!Status.bIsPassive && (Status.ExpiresAtMs==0 || NowMs<Status.ExpiresAtMs))
            { Statuses.Add({Status.StatusId,1,Status.ExpiresAtMs}); }
        }
        if (NowMs<PC->OwnerState.ProtectedUntilMs)
        { Statuses.Add({uint8(ELDDemoStatusId::RespawnProtection),1,PC->OwnerState.ProtectedUntilMs}); }
    }
    Body->SetCue(PresentationCue(Cue,Statuses));
    Body->Animate(GetVelocity(),Delta);
}

uint8 ALDCharacter::PresentationCue(ELDObservedActionCue Cue, const TArray<FLDObservedStatus>& Statuses)
{
    uint8 Code=Cue==ELDObservedActionCue::Attack ? 1 : Cue==ELDObservedActionCue::Cast ? 2 : Cue==ELDObservedActionCue::Hit ? 4 : 0;
    for (const FLDObservedStatus& Status : Statuses)
    {
        switch (static_cast<ELDDemoStatusId>(Status.StatusId))
        {
        case ELDDemoStatusId::Root: Code|=16; break;
        case ELDDemoStatusId::CampReturning: Code|=32; break;
        case ELDDemoStatusId::Shield: Code|=64; break;
        case ELDDemoStatusId::RespawnProtection: Code|=128; break;
        default: break;
        }
    }
    return Code;
}

void ALDCharacter::SetFacing(float Yaw) { if (FMath::IsFinite(Yaw)) { SetActorRotation(FRotator(0, FRotator::NormalizeAxis(Yaw), 0)); } }
void ALDCharacter::Zoom(float Axis) { CameraArm->TargetArmLength = ZoomedArmLength(CameraArm->TargetArmLength, Axis); }
float ALDCharacter::ZoomedArmLength(float ArmLength, float Axis) { return FMath::Clamp(ArmLength - Axis * 80, 1100.0f, 1700.0f); }
FTransform ALDCharacter::DefaultArmCameraPose(const FVector& Origin, float ArmLength)
{
    // The arm uses an absolute rotation and a world-space target offset, so the default subobject is the whole geometry.
    const USpringArmComponent* Arm = GetDefault<ALDCharacter>()->CameraArm;
    const FRotator Rotation = Arm->GetRelativeRotation();
    return FTransform(Rotation, Origin + Arm->TargetOffset - Rotation.Vector() * ArmLength);
}
void ALDCharacter::OnRep_Presentation() { Body->ApplyPalette(PresentationKey); }
void ALDCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ALDCharacter, PresentationKey);
}
