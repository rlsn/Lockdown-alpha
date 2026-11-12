#include "Character/LDCharacter.h"
#include "Core/LDLog.h"
#include "Character/LDCameraSettings.h"
#include "Character/LDBodyPuppet.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Net/UnrealNetwork.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "UI/LDHUDModel.h"
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
    // #97: bodies block each other (contenders, camp members and the Boss; user: "right now characters walk through each
    // other without collision, add collision"). Only the dash passes through bodies (ULDCharacterMovementComponent).
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->MaxAcceleration = 8000;
    Movement->BrakingDecelerationWalking = 8000;
    // #97: no step-up anywhere (generated geometry refuses it); bodies climb only walkable floor. MaxStepHeight is
    // then only how far the floor is followed down, e.g. over a ramp's crest. A ramp keeps the flat-ground speed.
    Movement->MaxStepHeight = 35;
    Movement->bMaintainHorizontalGroundVelocity = true;
    Movement->SetWalkableFloorAngle(40);
    // #97: cliffs are not traversable in either direction; a body stops at a ledge instead of falling off it.
    Movement->bCanWalkOffLedges = false;
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
    CameraArm->SetUsingAbsoluteLocation(true);
    CameraArm->SetUsingAbsoluteRotation(true);
    CameraArm->SetRelativeRotation(FRotator(LDCamera::Pitch,LDCamera::Yaw,0));
    CameraArm->TargetArmLength = LDCamera::DefaultArmLength;
    CameraArm->TargetOffset = FVector(0,0,LDCamera::TargetOffsetZ);
    CameraArm->bDoCollisionTest = false;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraArm);
    Camera->FieldOfView = LDCamera::FieldOfView;
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
    // Constructor attachments may not yet be registered; hide the complete body before its first frame.
    Body->SetVisibility(false,true);
    FString Error;
    if (UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap)
    {
        UE_LOG(LogLDNav,Error,TEXT("LD_DEMO_BODY_CONFIGURATION_REJECTED Body creation is outside the registered Demo world."));
        Destroy();
        return;
    }
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if (!LDDemoEntry::LoadRuntimeContent(Content,Error) || !ConfigureDemoMovement(*Content,Error))
    {
        UE_LOG(LogLDNav,Error,TEXT("LD_DEMO_BODY_CONFIGURATION_REJECTED %s"),*Error);
        Destroy();
        return;
    }
    if (HasAuthority()) { GetCharacterMovement()->SetComponentTickEnabled(false); }
    OnRep_Presentation();
}

void ALDCharacter::Tick(float Delta)
{
    Super::Tick(Delta);
    auto* PC=Cast<ALDPlayerController>(GetController());
    const bool OwnBody=PC && PC->GetLocalPlayer() && PC->GetPawn()==this
        && PC->OwnerState.LifePhase==ELDLifePhase::Alive && PC->OwnerState.BodyRef.IsValid();
    // Only on a change: a propagated SetVisibility rebuilds the render state of every part under the body, even unchanged.
    if (Body->GetVisibleFlag()!=OwnBody) { Body->SetVisibility(OwnBody,true); }
    if (!OwnBody) { return; }
    ShownOwner=PC;
    ShownLife=PC->OwnerState.Life;
    // The own body shows the cue and markers others see, from its own Combat projection at the public clock.
    const FLDCombatOwnerView& View=PC->CombatView;
    int64 NowMs=0;
    LDCombatCues::FCue Selected;
    TArray<FLDObservedStatus> Statuses;
    if (PC->ReadDemoAuthorityTimeMs(NowMs) && View.bHasBody && View.Generation==PC->OwnerState.Match
        && View.Life==PC->OwnerState.Life)
    {
        Selected=LDCombatCues::Select(View.Cues,NowMs);
        if (View.Cues.LastHitAtMs>0 && NowMs>=View.Cues.LastHitAtMs)
        { Body->SetHitReaction(float(NowMs-View.Cues.LastHitAtMs)/1000.f); }
        for (const FLDCombatStatusView& Status : View.Statuses)
        {
            if (!Status.bIsPassive && (Status.ExpiresAtMs==0 || NowMs<Status.ExpiresAtMs))
            { Statuses.Add({Status.StatusId,1,Status.ExpiresAtMs}); }
        }
        if (NowMs<PC->OwnerState.ProtectedUntilMs)
        { Statuses.Add({uint8(ELDDemoStatusId::RespawnProtection),1,PC->OwnerState.ProtectedUntilMs}); }
    }
    const bool bCue=Selected.Cue!=ELDObservedActionCue::None;
    Body->SetCue(PresentationCue(Selected.Cue,Statuses),bCue ? float(NowMs-Selected.AtMs)/1000.f : -1.f);
    Body->Animate(GetVelocity(),Delta);
    // Each hit the own body takes shakes its camera briefly and only slightly; others' hits never move this camera.
    if (View.Generation==PC->OwnerState.Match && View.Life==PC->OwnerState.Life
        && View.Cues.LastHitAtMs>0 && NowMs>=View.Cues.LastHitAtMs
        && NowMs-View.Cues.LastHitAtMs<160 && View.Cues.LastHitAtMs!=ShakeHitAtMs)
    {
        ShakeHitAtMs=View.Cues.LastHitAtMs;
        ShakeAge=float(NowMs-View.Cues.LastHitAtMs)/1000.f;
    }
    ShakeAge+=Delta;
    constexpr float ShakeSeconds=.16f, ShakeCm=4.f;
    const float Amplitude=ShakeAge<ShakeSeconds ? ShakeCm*(1.f-ShakeAge/ShakeSeconds) : 0.f;
    Camera->SetRelativeLocation(FVector(0,Amplitude*FMath::Sin(190.f*ShakeAge),.7f*Amplitude*FMath::Sin(150.f*ShakeAge+1.f)));
}

void ALDCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    // The authority removes a dead contender's body at once; its owner sees it fall where it stood. A removal that is
    // not that life's death (a match end or a departure) leaves nothing once the owner's projection disagrees.
    const ALDPlayerController* ShownTo=ShownOwner.Get();
    if (Reason==EEndPlayReason::Destroyed && ShownTo && GetWorld() && !GetWorld()->bIsTearingDown)
    {
        ALDBodyPuppet::SpawnOwnFall(*Body,*ShownTo,ShownLife);
    }
    Super::EndPlay(Reason);
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
void ALDCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
    Camera->GetCameraView(DeltaTime,OutResult);
    // The fixed, collision-free arm's tick may precede movement. Evaluate its pose at camera time, not last tick.
    FVector Origin=CameraArm->GetComponentLocation();
    float ArmLength=CameraArm->TargetArmLength;
    // #198 PK spectators: while the own player watches a PK (LDHUD::IsSpectatingPK of the own match) the same fixed arm
    // looks at the public arena centre from its longest zoom instead of at this body; the authority grants the view.
    const auto* OwnPC=IsLocallyControlled()?Cast<ALDPlayerController>(GetController()):nullptr;
    const auto* Public=OwnPC&&GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr;
    if(Public&&Public->PublicMatch.Generation!=0&&Public->PublicMatch.Generation==OwnPC->OwnerState.Match
        &&LDHUD::IsSpectatingPK(Public->PublicMatch,OwnPC->OwnerState.ContenderId)&&!Public->PublicMatch.PK.ArenaCentreCm.ContainsNaN())
    {
        Origin=Public->PublicMatch.PK.ArenaCentreCm;
        ArmLength=LDCamera::MaxArmLength;
    }
    const FTransform Pose=DefaultArmCameraPose(Origin,ArmLength);
    OutResult.Location=Pose.TransformPosition(Camera->GetRelativeLocation());
    OutResult.Rotation=Pose.Rotator();
}
FVector ALDCharacter::PresentedLocation() const
{
    return GetActorLocation()+CastChecked<ULDCharacterMovementComponent>(GetCharacterMovement())->OwnerPresentationOffset();
}
void ALDCharacter::UpdateOwnerPresentation(float DeltaSeconds)
{
    CastChecked<ULDCharacterMovementComponent>(GetCharacterMovement())->AdvanceOwnerPresentation(DeltaSeconds);
    Body->SetWorldLocation(PresentedLocation());
}
void ALDCharacter::Zoom(float Axis) { CameraArm->TargetArmLength = ZoomedArmLength(CameraArm->TargetArmLength, Axis); }
float ALDCharacter::ZoomedArmLength(float ArmLength, float Axis) { return FMath::Clamp(ArmLength - Axis * LDCamera::ZoomStep, LDCamera::MinArmLength, LDCamera::MaxArmLength); }
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
