#include "Art/LDArtTestCharacter.h"
#include "Art/LDBasketballPreview.h"
#include "LDEditorLog.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Camera/CameraComponent.h"
#include "Character/LDCameraSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "NavigationSystem.h"
#include "InputCoreTypes.h"
#include "EngineUtils.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"

ALDArtTestCharacter::ALDArtTestCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    AutoPossessPlayer = EAutoReceiveInput::Player0;
    GetCapsuleComponent()->InitCapsuleSize(28.f,90.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);
    bUseControllerRotationYaw = false;
    auto* Movement = GetCharacterMovement();
    Movement->NavAgentProps.AgentRadius = 28.f;
    Movement->NavAgentProps.AgentHeight = 180.f;
    Movement->bOrientRotationToMovement = true;
    Movement->RotationRate = FRotator(0,720,0);
    Movement->MaxWalkSpeed = AnimationRunSpeed;
    Movement->MaxAcceleration = 12000.f;
    Movement->BrakingDecelerationWalking = 9000.f;
    Movement->GroundFriction = 10.f;
    Movement->SetWalkableFloorAngle(40.f);
    Movement->MaxStepHeight = 35.f;
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewCameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("ArtCameraArm"));
    PreviewCameraArm->SetupAttachment(GetRootComponent());
    PreviewCameraArm->SetUsingAbsoluteRotation(true);
    PreviewCameraArm->SetRelativeRotation(FRotator(LDCamera::Pitch,LDCamera::Yaw,0));
    PreviewCameraArm->TargetArmLength = LDCamera::MinArmLength;
    PreviewCameraArm->bDoCollisionTest = false;
    PreviewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ArtCamera"));
    PreviewCamera->SetupAttachment(PreviewCameraArm);
    PreviewCamera->FieldOfView = LDCamera::FieldOfView;
    PreviewCamera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
    PreviewCamera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
    PreviewCamera->PostProcessSettings.AutoExposureMinBrightness = 0;
    PreviewCamera->PostProcessSettings.AutoExposureMaxBrightness = 0;
    PreviewCamera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    PreviewCamera->PostProcessSettings.MotionBlurAmount = 0;
}

void ALDArtTestCharacter::UpdatePreviewMesh()
{
    if (!PreviewMesh) { return; }
    auto* Visual = GetMesh();
    Visual->SetSkeletalMeshAsset(PreviewMesh);
    const FBoxSphereBounds Bounds = PreviewMesh->GetBounds();
    const float Height = Bounds.BoxExtent.Z*2.f;
    if (Height <= UE_SMALL_NUMBER || !FMath::IsFinite(PreviewHeightCm) || PreviewHeightCm <= 0) { return; }
    const float Scale = PreviewHeightCm/Height;
    Visual->SetRelativeScale3D(FVector(Scale));
    Visual->SetRelativeRotation(MeshOrientation);
    Visual->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-(Bounds.Origin.Z-Bounds.BoxExtent.Z)*Scale));
}

void ALDArtTestCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    UpdatePreviewMesh();
}

void ALDArtTestCharacter::BeginPlay()
{
    Super::BeginPlay();
    UpdatePreviewMesh();
    GetCharacterMovement()->MaxWalkSpeed = AnimationRunSpeed;
    if (!PreviewMesh || !IdleAnimation || !RunAnimation
        || IdleAnimation->GetSkeleton() != PreviewMesh->GetSkeleton()
        || RunAnimation->GetSkeleton() != PreviewMesh->GetSkeleton())
    {
        UE_LOG(LogLDEditor,Error,TEXT("LD_ART_TEST_INVALID_ASSETS: assign a mesh and Idle/Run animations retargeted to its skeleton."));
        GetCharacterMovement()->DisableMovement();
        return;
    }
    if (!MotionSet)
    {
        MotionSet=LoadObject<ULDBasketballMotionSet>(nullptr,TEXT("/Game/Lockdown/Art_Test/Characters/Kobe/Basketball/DA_KobeBasketball.DA_KobeBasketball"));
    }
    if (!BasketballMesh || !MotionSet || !MotionSet->Dribble || !MotionSet->Idle || !MotionSet->Handling || !MotionSet->Carry || MotionSet->Passes.IsEmpty())
    {
        UE_LOG(LogLDEditor,Error,TEXT("LD_BASKETBALL_MISSING_MOTION_SET"));
        GetCharacterMovement()->DisableMovement();
        return;
    }
    // UE 5.8 queues index work for the next editor tick. Keep a valid idle pose
    // while it loads; waiting synchronously on a not-yet-started task asserts.
    MotionLoadStarted=GetWorld()->GetTimeSeconds();
    GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    GetMesh()->PlayAnimation(IdleAnimation,true);
    GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    for (auto* Database:{MotionSet->Dribble.Get(),MotionSet->Idle.Get(),MotionSet->Handling.Get(),MotionSet->Carry.Get()})
    {
        ULDBasketballMotionSet::DatabasePoseCount(Database);
    }
    Basketball=GetWorld()->SpawnActor<ALDBasketballPreviewBall>();
    Basketball->Initialize(this);
}

void ALDArtTestCharacter::SetPreviewAttackSpeed(float Rate)
{
    if (FMath::IsFinite(Rate)) { PreviewAttacksPerSecond=FMath::Clamp(Rate,.2f,5.f); }
}

void ALDArtTestCharacter::SetPreviewMoveSpeed(float Speed)
{
    if (!FMath::IsFinite(Speed) || Speed<=0) { return; }
    AnimationRunSpeed=FMath::Clamp(Speed,100.f,1500.f);
    if (DashTime<0) { GetCharacterMovement()->MaxWalkSpeed=AnimationRunSpeed; }
}

FVector ALDArtTestCharacter::GetBasketballHand(bool Left) const
{
    return GetMesh()->GetSocketLocation(Left ? TEXT("BasketballSocketL"):TEXT("BasketballSocket"));
}

FVector ALDArtTestCharacter::KeepBallOutsideHands(FVector Position) const
{
    // Palm normals calibrated against the actual bind-pose skin surface, not
    // the wrist joint. A disk contact constraint keeps a 12 cm sphere outside it.
    for (bool Left:{false,true})
    {
        const FVector LocalNormal=Left ? FVector(-.006196f,-.094325f,-.995522f):FVector(-.975772f,-.109624f,-.189348f);
        const FVector Normal=GetMesh()->GetSocketQuaternion(Left ? TEXT("hand_l"):TEXT("hand_r")).RotateVector(LocalNormal).GetSafeNormal();
        const FVector Palm=GetBasketballHand(Left)-Normal*12.f;
        const FVector Delta=Position-Palm;
        const float Depth=FVector::DotProduct(Delta,Normal);
        const float Lateral=(Delta-Normal*Depth).Size();
        if (Depth>-14.f && Depth<12.f && Lateral<17.f)
        {
            const float Edge=FMath::Max(0.f,Lateral-5.f);
            const float Clearance=FMath::Sqrt(FMath::Max(0.f,144.f-Edge*Edge));
            if (Depth<Clearance) { Position+=Normal*(Clearance-Depth); }
        }
    }
    // Finger capsules cover the remaining skin thickness during blended poses.
    // Resolve toward the palm underside, never through the back of the hand.
    for (int32 Pass=0;Pass<2;++Pass)
    {
        for (bool Left:{false,true})
        {
            const TCHAR* Side=Left ? TEXT("l"):TEXT("r");
            const FVector LocalNormal=Left ? FVector(-.006196f,-.094325f,-.995522f):FVector(-.975772f,-.109624f,-.189348f);
            const FVector Normal=GetMesh()->GetSocketQuaternion(FName(*FString::Printf(TEXT("hand_%s"),Side))).RotateVector(LocalNormal).GetSafeNormal();
            for (const TCHAR* Finger:{TEXT("thumb"),TEXT("index"),TEXT("middle"),TEXT("ring"),TEXT("pinky")})
            {
                for (int32 Joint=1;Joint<3;++Joint)
                {
                    const FVector A=GetMesh()->GetSocketLocation(FName(*FString::Printf(TEXT("%s_0%d_%s"),Finger,Joint,Side)));
                    const FVector B=GetMesh()->GetSocketLocation(FName(*FString::Printf(TEXT("%s_0%d_%s"),Finger,Joint+1,Side)));
                    const FVector Closest=FMath::ClosestPointOnSegment(Position,A,B);
                    const FVector D=Position-Closest;
                    constexpr float Clearance=13.2f;
                    if (D.SizeSquared()<FMath::Square(Clearance))
                    {
                        const float Along=FVector::DotProduct(D,Normal);
                        const float Push=-Along+FMath::Sqrt(FMath::Max(0.f,Along*Along+Clearance*Clearance-D.SizeSquared()));
                        Position+=Normal*Push;
                    }
                }
            }
        }
    }
    return Position;
}

void ALDArtTestCharacter::BeginClip(const FLDBasketballActionClip& Clip, ELDBasketballAction Kind, float Duration)
{
    ActiveClip=Clip;
    Action=Kind;
    ActionDuration=FMath::Max(Duration,.05f);
    AttackTime=0;
    bLeftHand=Clip.bLeftHand;
}

bool ALDArtTestCharacter::BeginBallAction(FVector AimPoint, bool Skill)
{
    if (!bMotionReady || !Basketball || !MotionSet || AimPoint.ContainsNaN() || DashTime>=0
        || Basketball->State!=ELDBasketballPreviewState::Dribble
        || GetWorld()->GetTimeSeconds()<NextAttackAt) { return false; }
    const auto& Clips=Skill ? (GetVelocity().Size2D()>60 ? MotionSet->MovingShots:MotionSet->StandingShots):MotionSet->Passes;
    if (Clips.IsEmpty()) { return false; }
    const bool Left=GetMesh()->GetAnimInstance()->GetCurveValue(TEXT("BallLeft"))>.5f;
    TArray<int32> Choices;
    for (int32 I=0;I<Clips.Num();++I) { if (Clips[I].bLeftHand==Left) { Choices.Add(I); } }
    const int32 Index=Choices.IsEmpty() ? 0:Choices[Variation++%Choices.Num()];
    AttackCycle=Skill ? 1.15f:1.f/FMath::Clamp(PreviewAttacksPerSecond,.2f,5.f);
    BallReleaseTime=AttackCycle*(Skill ? .44f:.12f);
    BeginClip(Clips[Index],Skill ? ELDBasketballAction::Shot:ELDBasketballAction::Pass,AttackCycle*(Skill ? .8f:.56f));
    AttackYaw=(AimPoint-GetActorLocation()).Rotation().Yaw;
    if (!Basketball->ThrowAt(AimPoint)) { Action=ELDBasketballAction::None; AttackTime=-1; return false; }
    NextAttackAt=GetWorld()->GetTimeSeconds()+AttackCycle;
    if (Skill) { ++SkillCount; }
    return true;
}

bool ALDArtTestCharacter::ThrowBasketball(FVector AimPoint) { return BeginBallAction(AimPoint,false); }
bool ALDArtTestCharacter::ShootBasketball(FVector AimPoint) { return BeginBallAction(AimPoint,true); }

bool ALDArtTestCharacter::DashBasketball(FVector Direction)
{
    if (!bMotionReady || Direction.ContainsNaN() || !MotionSet || MotionSet->Crossovers.IsEmpty() || !Basketball || DashTime>=0
        || Basketball->State!=ELDBasketballPreviewState::Dribble
        || GetWorld()->GetTimeSeconds()<NextDashAt) { return false; }
    Direction.Z=0;
    if (!Direction.Normalize()) { Direction=GetActorForwardVector(); }
    DashDirection=Direction;
    const auto& Clips=MotionSet->Crossovers;
    BeginClip(Clips[0],ELDBasketballAction::Crossover,DashDuration);
    DashTime=0;
    DashSpeed=FMath::Max(900.f,AnimationRunSpeed*1.8f);
    NextDashAt=GetWorld()->GetTimeSeconds()+.85f;
    ++DashCount;
    SetActorRotation(Direction.Rotation());
    if (Controller) { Controller->StopMovement(); }
    GetCharacterMovement()->GroundFriction=0;
    GetCharacterMovement()->BrakingDecelerationWalking=0;
    return true;
}

void ALDArtTestCharacter::ReceiveBasketball()
{
    // Resume the current dribble phase. Compressing a separate catching clip
    // into a few frames created an unwanted arm/torso flash on every return.
}

float ALDArtTestCharacter::GetActionSampleTime() const
{
    if (!ActiveClip.Animation) { return 0; }
    const float T=FMath::Clamp(AttackTime,0.f,ActionDuration);
    if (Action==ELDBasketballAction::Pass || Action==ELDBasketballAction::Shot)
    {
        if (T<=BallReleaseTime) { return FMath::Lerp(ActiveClip.Start,ActiveClip.Release,T/FMath::Max(BallReleaseTime,.001f)); }
        return FMath::Lerp(ActiveClip.Release,ActiveClip.End,(T-BallReleaseTime)/FMath::Max(ActionDuration-BallReleaseTime,.001f));
    }
    return FMath::Lerp(ActiveClip.Start,ActiveClip.End,T/FMath::Max(ActionDuration,.001f));
}

void ALDArtTestCharacter::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!Basketball || !MotionSet || bMotionFailed) { return; }
    if (!bMotionReady)
    {
        bool Ready=true;
        for (auto* Database:{MotionSet->Dribble.Get(),MotionSet->Idle.Get(),MotionSet->Handling.Get(),MotionSet->Carry.Get()})
        {
            Ready &= ULDBasketballMotionSet::DatabasePoseCount(Database)>0;
        }
        if (!Ready)
        {
            GetCharacterMovement()->StopMovementImmediately();
            if (GetWorld()->GetTimeSeconds()-MotionLoadStarted>30)
            {
                bMotionFailed=true;
                GetCharacterMovement()->DisableMovement();
                UE_LOG(LogLDEditor,Error,TEXT("LD_BASKETBALL_INVALID_SEARCH_INDEX: basketball search data did not become ready."));
            }
            return;
        }
        GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        GetMesh()->SetAnimInstanceClass(ULDBasketballPreviewAnimInstance::StaticClass());
        GetMesh()->GetAnimInstance()->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
        bMotionReady=true;
    }
    auto* Movement=GetCharacterMovement();
    const float Speed=GetVelocity().Size2D();
    const FVector Acceleration=(GetVelocity()-PreviousClothVelocity)/FMath::Max(Delta,.001f);
    PreviousClothVelocity=GetVelocity();
    const FTransform MeshTransform=GetMesh()->GetComponentTransform();
    const FVector ClothTarget=MeshTransform.InverseTransformVectorNoScale(-Acceleration*.0003f-GetVelocity()*.002f).GetClampedToMaxSize(3.5f);
    float ClothRemaining=FMath::Min(Delta,.1f);
    while (ClothRemaining>UE_SMALL_NUMBER)
    {
        const float Step=FMath::Min(ClothRemaining,1.f/120.f);
        ClothOffsetVelocity+=((ClothTarget-ClothOffset)*90.f-ClothOffsetVelocity*17.f)*Step;
        ClothOffset+=ClothOffsetVelocity*Step; ClothRemaining-=Step;
    }
    GetMesh()->SetVectorParameterValueOnMaterials(TEXT("ClothInertia"),ClothOffset);
    GetMesh()->SetScalarParameterValueOnMaterials(TEXT("ClothFlutter"),FMath::Clamp(Speed/500.f,.10f,1.8f));
    if (DashTime>=0)
    {
        DashTime+=Delta;
        if (DashTime<DashDuration)
        {
            const float U=DashTime/DashDuration;
            Movement->MaxWalkSpeed=DashSpeed;
            Movement->Velocity=DashDirection*DashSpeed*(.72f+.28f*FMath::Sin(PI*U));
        }
        else
        {
            DashTime=-1;
            Movement->GroundFriction=10;
            Movement->BrakingDecelerationWalking=9000;
            Movement->MaxWalkSpeed=AnimationRunSpeed;
            Movement->Velocity=DashDirection*FMath::Min(Speed,AnimationRunSpeed);
        }
    }
    bMoving=Speed>20 || !Movement->GetCurrentAcceleration().IsNearlyZero();
    LocomotionPlayRate=bMoving ? FMath::Clamp(Speed/501.54f,.45f,3.f):1.f;
    IdleTime=Speed<12 ? IdleTime+Delta:0;
    bHandlingVariation=IdleTime>4 && FMath::Fmod(IdleTime,10.f)>4 && FMath::Fmod(IdleTime,10.f)<7.5f;
    bCarryPose=Basketball->State==ELDBasketballPreviewState::Outbound || Basketball->State==ELDBasketballPreviewState::ToBounce;
    if (AttackTime>=0)
    {
        AttackTime+=Delta;
        const float BlendIn=Action==ELDBasketballAction::Pass ? FMath::Min(.035f,ActionDuration*.13f):FMath::Min(.09f,ActionDuration*.2f);
        const float BlendOut=FMath::Min(.14f,ActionDuration*.28f);
        ActionWeight=FMath::Min(FMath::SmoothStep(0.f,BlendIn,AttackTime),FMath::SmoothStep(0.f,BlendOut,ActionDuration-AttackTime));
        if (AttackTime>=ActionDuration) { if (Action==ELDBasketballAction::Turn) { SetActorRotation(FRotator(0,TurnStartYaw+TurnYawDelta,0)); } AttackTime=-1; ActionWeight=0; Action=ELDBasketballAction::None; }
    }
    const bool Aiming=Action==ELDBasketballAction::Pass || Action==ELDBasketballAction::Shot;
    Movement->bOrientRotationToMovement=(Speed>5 || !Aiming) && Action!=ELDBasketballAction::Turn;
    if (Aiming && Speed<5) { SetActorRotation(FMath::RInterpTo(GetActorRotation(),FRotator(0,AttackYaw,0),Delta,24)); }
    const float Aim=Aiming ? FMath::Clamp(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,AttackYaw),-30.f,30.f):0;
    UpperBodyAimYaw=FMath::FInterpTo(UpperBodyAimYaw,Aim,Delta,24);
    // Start/stop stay in pose search; never compress a jump-stop into a shuffle.
    FVector Desired=GetLastMovementInputVector();
    if (Desired.IsNearlyZero()) { Desired=Movement->GetCurrentAcceleration(); }
    Desired.Z=0;
    const float DirectionDelta=Desired.IsNearlyZero() ? 0:FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,Desired.Rotation().Yaw);
    if (Action==ELDBasketballAction::None && Speed>80 && FMath::Abs(DirectionDelta)>105
        && GetWorld()->GetTimeSeconds()>NextTurnAt && !MotionSet->Turns.IsEmpty()
        && Basketball->State==ELDBasketballPreviewState::Dribble)
    {
        const bool LeftTurn=DirectionDelta<0;
        const auto& Clip=MotionSet->Turns[LeftTurn && MotionSet->Turns.Num()>1 ? 1:0];
        TurnStartYaw=GetActorRotation().Yaw; TurnYawDelta=DirectionDelta;
        BeginClip(Clip,ELDBasketballAction::Turn,FMath::Clamp(FMath::Abs(DirectionDelta)/300.f,.38f,.60f));
        NextTurnAt=GetWorld()->GetTimeSeconds()+.6f; ++TurnCount;
        Movement->bOrientRotationToMovement=false;
    }
    if (Action==ELDBasketballAction::Turn)
    {
        const float U=FMath::Clamp(AttackTime/ActionDuration,0.f,1.f);
        SetActorRotation(FRotator(0,TurnStartYaw+TurnYawDelta*FMath::SmoothStep(0.f,1.f,U),0));
    }
    // Original spin assets rotate the pelvis as well as having root motion.
    // Compensate authored facing on the visual mesh so it does not add a second spin.
    float SourceFacing=0;
    if (Action==ELDBasketballAction::Turn && ActiveClip.Animation)
    {
        SourceFacing=ActiveClip.Animation->EvaluateCurveData(TEXT("TurnFacing"),FAnimExtractContext(GetActionSampleTime(),false),true);
        SourceFacing=FMath::UnwindDegrees(SourceFacing)*ActionWeight;
    }
    GetMesh()->SetRelativeRotation(FRotator(MeshOrientation.Pitch,MeshOrientation.Yaw-SourceFacing,MeshOrientation.Roll));

}

ALDArtTestController::ALDArtTestController()
{
    OverridePlayerInputClass = UPlayerInput::StaticClass();
    bShowMouseCursor = true;
    DefaultMouseCursor = EMouseCursor::Default;
}

void ALDArtTestController::BeginPlay()
{
    Super::BeginPlay();
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
}

void ALDArtTestController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    auto* PreviewBody = Cast<ALDArtTestCharacter>(GetPawn());
    if (!PreviewBody) { return; }
    if (WasInputKeyJustPressed(EKeys::RightMouseButton)) { MoveToCursor(); }
    if (IsInputKeyDown(EKeys::LeftMouseButton)) { AttackCursor(false); }
    if (IsInputKeyDown(EKeys::F)) { AttackCursor(true); }
    if (WasInputKeyJustPressed(EKeys::Q)) { AttackCursor(true,true); }
    if (WasInputKeyJustPressed(EKeys::LeftShift))
    {
        FVector Direction=PreviewBody->GetLastMovementInputVector();
        if (Direction.IsNearlyZero()) { Direction=PreviewBody->GetVelocity(); }
        PreviewBody->DashBasketball(Direction);
    }
    if (WasInputKeyJustPressed(EKeys::One)) { PreviewBody->SetPreviewAttackSpeed(1); }
    if (WasInputKeyJustPressed(EKeys::Two)) { PreviewBody->SetPreviewAttackSpeed(2); }
    if (WasInputKeyJustPressed(EKeys::Three)) { PreviewBody->SetPreviewAttackSpeed(3); }
    if (WasInputKeyJustPressed(EKeys::SpaceBar)) { StopPreviewMovement(); }
    if (WasInputKeyJustPressed(EKeys::R))
    {
        for (TActorIterator<ALDArtTestMonster> It(GetWorld());It;++It) { It->ResetTarget(); }
    }
    const float Forward = float(IsInputKeyDown(EKeys::W))-float(IsInputKeyDown(EKeys::S));
    const float Right = float(IsInputKeyDown(EKeys::D))-float(IsInputKeyDown(EKeys::A));
    if ((Forward != 0 || Right != 0) && PreviewBody->DashTime<0)
    {
        StopMovement();
        const FRotator ViewYaw(0,LDCamera::Yaw,0);
        PreviewBody->AddMovementInput(ViewYaw.Vector(),Forward);
        PreviewBody->AddMovementInput(FRotationMatrix(ViewYaw).GetUnitAxis(EAxis::Y),Right);
    }
}

void ALDArtTestController::MoveToCursor()
{
    auto* PreviewBody = Cast<ALDArtTestCharacter>(GetPawn());
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    FHitResult Hit;
    const bool bHitGround = GetHitResultUnderCursor(ECC_GameTraceChannel1,false,Hit);
    UE_LOG(LogLDEditor,Verbose,TEXT("LD_ART_CLICK: pawn=%d nav=%d ground=%d point=%s"),PreviewBody != nullptr,Nav != nullptr,bHitGround,*Hit.Location.ToString());
    if (!PreviewBody || !Nav || !bHitGround) { return; }
    const ANavigationData* Data = Nav->GetNavDataForProps(PreviewBody->GetNavAgentPropertiesRef(),PreviewBody->GetActorLocation());
    FNavLocation Target;
    if (Data && Nav->ProjectPointToNavigation(Hit.Location,Target,FVector(100,100,250),Data))
    {
        UE_LOG(LogLDEditor,Display,TEXT("LD_ART_CLICK_GOAL: %s"),*Target.Location.ToString());
        UAIBlueprintHelperLibrary::SimpleMoveToLocation(this,Target.Location);
    }
}

void ALDArtTestController::AttackCursor(bool bNearestTarget, bool bSkill)
{
    auto* Hero=Cast<ALDArtTestCharacter>(GetPawn());
    if (!Hero) { return; }
    FVector Aim=Hero->GetActorLocation()+Hero->GetActorForwardVector()*700;
    FHitResult Hit;
    if (bNearestTarget)
    {
        float Best=1500.f*1500.f;
        for (TActorIterator<ALDArtTestMonster> It(GetWorld());It;++It)
        {
            const float Distance=FVector::DistSquared(Hero->GetActorLocation(),It->GetActorLocation());
            if (It->Health>0 && Distance<Best) { Best=Distance; Aim=It->GetActorLocation()+FVector(0,0,15); }
        }
    }
    else if (GetHitResultUnderCursor(ECC_Visibility,false,Hit) && Cast<ALDArtTestMonster>(Hit.GetActor()))
    {
        Aim=Hit.GetActor()->GetActorLocation()+FVector(0,0,15);
    }
    else if (GetHitResultUnderCursor(ECC_GameTraceChannel1,false,Hit))
    {
        Aim=Hit.ImpactPoint+FVector(0,0,90);
    }
    if (bSkill) { Hero->ShootBasketball(Aim); } else { Hero->ThrowBasketball(Aim); }
}

void ALDArtTestController::StopPreviewMovement()
{
    StopMovement();
    if (auto* PreviewBody = Cast<ACharacter>(GetPawn())) { PreviewBody->GetCharacterMovement()->StopMovementImmediately(); }
}

ALDArtTestGameMode::ALDArtTestGameMode()
{
    DefaultPawnClass = nullptr;
    PlayerControllerClass = ALDArtTestController::StaticClass();
    HUDClass = ALDBasketballPreviewHUD::StaticClass();
}



