#include "Character/LDCharacter.h"
#include "Core/LDLog.h"
#include "Character/LDCameraSettings.h"
#include "Character/LDBodyPuppet.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Character/LDHitFeel.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Character/LDImpactFx.h"
#include "Audio/LDAudioSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerState.h"
#include "Components/CapsuleComponent.h"
#include "Net/UnrealNetwork.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameState.h"
#include "UI/LDHUDModel.h"
#include "UI/LDControlSettings.h"
#include "Framework/LDDemoEntry.h"
#include "Combat/LDCombatCues.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Region/LDMapRegistry.h"
#include "Navigation/LDNavigationComponent.h"

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
    PredictWindupMs=Content->Get().Parameters.BasicAttackWindupMs;
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
    const bool bCurrentView=PresentationOwnerView(View,PC->OwnerState,PC->ReadDemoAuthorityTimeMs(NowMs));
    if (bCurrentView)
    {
        Selected=LDCombatCues::Select(View.Cues,NowMs);
        for (const FLDCombatStatusView& Status : View.Statuses)
        {
            if (!Status.bIsPassive && (Status.ExpiresAtMs==0 || NowMs<Status.ExpiresAtMs))
            { Statuses.Add({Status.StatusId,1,Status.ExpiresAtMs}); }
        }
        if (NowMs<PC->OwnerState.ProtectedUntilMs)
        { Statuses.Add({uint8(ELDDemoStatusId::RespawnProtection),1,PC->OwnerState.ProtectedUntilMs}); }
    }
    const bool bCue=Selected.Cue!=ELDObservedActionCue::None;
    const int64 ImpactAtMs=View.Cues.bActing && Selected.AtMs==View.Cues.ActingStartedAtMs ? View.Cues.ActingReleaseAtMs
        : Selected.AtMs==View.Cues.ReleasedStartedAtMs ? View.Cues.ReleasedAtMs : 0;
    // The own swing prediction (LDHitFeel::AdvanceSwing): once the standing attack order is in reach and the cadence is
    // due, the swing starts now and holds short of its impact until the authority's start confirms it, else rewinds.
    if (SwingLife!=ShownLife) { Swing={}; SwingLife=ShownLife; bShownPredicted=false; }
    const FLDCombatCueFacts& Facts=View.Cues;
    if (bCurrentView && Facts.bActingBasic && Facts.ActingReleaseAtMs>Facts.ActingStartedAtMs)
    { PredictWindupMs=Facts.ActingReleaseAtMs-Facts.ActingStartedAtMs; }
    else if (bCurrentView && Facts.bReleasedBasic && Facts.ReleasedAtMs>Facts.ReleasedStartedAtMs)
    { PredictWindupMs=Facts.ReleasedAtMs-Facts.ReleasedStartedAtMs; }
    const LDHitFeel::FTuning Feel=LDHitFeel::Read();
    LDHitFeel::FSwingFacts SwingFacts;
    SwingFacts.bEnabled=Feel.bPredictSwing && PredictWindupMs>0;
    SwingFacts.bCurrent=bCurrentView;
    SwingFacts.NowMs=NowMs;
    SwingFacts.NextAttackAtMs=View.NextAttackAtMs;
    SwingFacts.LastBasicStartMs=FMath::Max(Facts.bActingBasic ? Facts.ActingStartedAtMs : 0, Facts.bReleasedBasic ? Facts.ReleasedStartedAtMs : 0);
    SwingFacts.bBusy=(Facts.bActing && !Facts.bActingBasic) || NowMs<Facts.DashUntilMs;
    for (const FLDCombatStatusView& Status : View.Statuses)
    {
        if ((Status.ExpiresAtMs==0 || NowMs<Status.ExpiresAtMs) && (Status.ControlBlocks & uint8(ELDCombatControlBlock::Basic)))
        { SwingFacts.bBlocked=true; }
    }
    SwingFacts.bEngaged=bCurrentView && PC->Navigation && PC->Navigation->ReadBasicEngagement(PresentedLocation(),View.AttackRangeCm);
    SwingFacts.GraceMs=Feel.PredictGraceMs+(PC->PlayerState ? int64(PC->PlayerState->GetPingInMilliseconds()) : 0);
    const LDHitFeel::ESwingStep Step=LDHitFeel::AdvanceSwing(Swing,SwingFacts);
    if (Step!=LDHitFeel::ESwingStep::None)
    {
        UE_LOG(LogLDCombat,Log,TEXT("LD_SWING_PREDICT step=%s key=%lld started=%lld now=%lld grace=%lld"),
            LDHitFeel::SwingStepName(Step),Swing.KeyAtMs,Swing.StartedAtMs,NowMs,SwingFacts.GraceMs);
    }
    if (Step==LDHitFeel::ESwingStep::Withdrawn) { Body->Retract(); }
    const bool bPredicted=Swing.bActive && (!bCue || (Selected.Cue==ELDObservedActionCue::Attack && Selected.AtMs<Swing.KeyAtMs));
    Body->SetHold(bPredicted ? LDHitFeel::SwingHoldSeconds(PredictWindupMs) : -1.f);
    if (bPredicted)
    {
        Body->SetCue(PresentationCue(ELDObservedActionCue::Attack,Statuses),float(NowMs-Swing.StartedAtMs)/1000.f,
            float(PredictWindupMs)/1000.f,PresentationKnockback(Statuses));
    }
    else
    {
        // The swing the prediction showed is now the authority's: take its start, earlier or later, not the jitter filter.
        const bool bHandover=bShownPredicted && Selected.Cue==ELDObservedActionCue::Attack;
        Body->SetCue(PresentationCue(Selected.Cue,Statuses),bCue ? float(NowMs-Selected.AtMs)/1000.f : -1.f,
            bCue ? float(FMath::Max<int64>(0,ImpactAtMs-Selected.AtMs))/1000.f : -1.f,PresentationKnockback(Statuses),bHandover);
    }
    bShownPredicted=bPredicted;
    // The shown cast's own skill look (LDHitVfx::SkillLookOf): the authority's cue facts name the acting or released
    // learned ability, so a release snapshot that already cleared the action slot keeps it.
    uint8 CastAbility=0;
    if (bCue && Selected.Cue==ELDObservedActionCue::Cast)
    {
        if (const LDHitVfx::FSkillLook* Skill=LDHitVfx::SkillLookOf(Facts.CastDefinitionId)) { CastAbility=Skill->Ability; }
    }
    Body->SetCastAbility(CastAbility);
    Body->SetCharging(bCurrentView && View.ChargingEntry!=ELDCombatEntry::None);
    Body->SetOverdriveCast(bCue && Selected.Cue==ELDObservedActionCue::Cast && Facts.bOverdriveCast);
    if (bCurrentView && View.Cues.LastHitAtMs>0 && NowMs>=View.Cues.LastHitAtMs)
    {
        const bool bFresh=Body->SetHitReaction(float(NowMs-View.Cues.LastHitAtMs)/1000.f,
            static_cast<ELDObservedHitLook>(View.Cues.LastHitLook),View.Cues.LastHitFlags,View.Cues.LastHitId,View.Cues.LastHitAtMs);
        // Taking a hit tints the screen edge faintly red; a stronger flash of the own hit still playing wins.
        ULDImpactFxSubsystem* Hurt=GetWorld()?GetWorld()->GetSubsystem<ULDImpactFxSubsystem>():nullptr;
        FLinearColor Playing;
        if (bFresh && Hurt && NowMs-View.Cues.LastHitAtMs<200 && Hurt->ScreenFlashAlpha(Playing)<=0.f)
        { Hurt->FlashScreen(LDHitVfx::HurtFlashOf(PC->Controls && PC->Controls->bReduceCombatEffects)); }
    }
    // The own hit confirmation (LDHitFeel): each fresh receipt of a hit this body landed holds its striking pose, flares
    // its hand and kicks its camera along its facing. Hits it takes never move this camera or its pose.
    const bool bReduced=PC->Controls && PC->Controls->bReduceCombatEffects;
    if (bCurrentView)
    {
        if (LDHitFeel::TakeFreshReceipt(ConfirmedAtMs,View.Cues.LastDealtAtMs,NowMs))
        {
            const LDHitFeel::FConfirm Confirm=LDHitFeel::ConfirmOf(LDHitFeel::Read(),View.Cues.LastDealtFlags);
            // The server time only judges the receipt; the layers start now, together with the confirmation sounds.
            Body->SetConfirm(0.f,Confirm.StopSeconds,Confirm.SparkScale);
            KickAge=0.f;
            KickCm=Confirm.KickCm*(bReduced ? .45f : 1.f);
            // A kill, critical hit or broken shield flashes the screen edge and fringes the lens (off when reduced).
            if (ULDImpactFxSubsystem* Impacts=GetWorld()?GetWorld()->GetSubsystem<ULDImpactFxSubsystem>():nullptr)
            { Impacts->FlashScreen(LDHitVfx::ScreenFlashOf(View.Cues.LastDealtFlags,bReduced)); }
            UGameInstance* Instance=GetGameInstance();
            ULDAudioSubsystem* Audio=Instance?Instance->GetSubsystem<ULDAudioSubsystem>():nullptr;
            if (Audio)
            {
                Audio->Play(ELDAudioCue::SelfDealtImpact,Feel.ConfirmVolume);
                Audio->Play(ELDAudioCue::SelfDealtAccent,Feel.ConfirmVolume);
            }
            else { UE_LOG(LogLDContent,Warning,TEXT("LD_AUDIO_SUBSYSTEM_UNAVAILABLE confirmed hit layers not played.")); }
        }
    }
    Body->Animate(GetVelocity(),Delta);
    KickAge+=Delta;
    const float Kick=PC->Controls && PC->Controls->bCameraShake ? LDBodyMotion::KickOffset(KickAge,KickCm,LDHitFeel::Read().KickSeconds) : 0.f;
    Camera->SetRelativeLocation(CameraArm->GetComponentRotation().UnrotateVector(GetActorForwardVector().GetSafeNormal2D()*Kick));
    const ULDImpactFxSubsystem* Impacts=GetWorld()?GetWorld()->GetSubsystem<ULDImpactFxSubsystem>():nullptr;
    const float Fringe=Impacts && !bReduced ? Impacts->ScreenFringe() : 0.f;
    Camera->PostProcessSettings.bOverride_SceneFringeIntensity=Fringe>0.f;
    Camera->PostProcessSettings.SceneFringeIntensity=Fringe;
}

bool ALDCharacter::PresentationOwnerView(const FLDCombatOwnerView& View, const FLDOwnerState& Owner, bool bHasClock)
{
    return bHasClock && View.bHasBody && View.Generation==Owner.Match && View.Life==Owner.Life;
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
    uint8 Code=Cue==ELDObservedActionCue::Attack ? 1 : Cue==ELDObservedActionCue::Cast ? 2 : 0;
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

bool ALDCharacter::PresentationKnockback(const TArray<FLDObservedStatus>& Statuses)
{
    return Statuses.ContainsByPredicate([](const FLDObservedStatus& Status)
        { return Status.StatusId == uint8(ELDDemoStatusId::Knockback); });
}

void ALDCharacter::SetFacing(float Yaw) { if (FMath::IsFinite(Yaw)) { SetActorRotation(FRotator(0, FRotator::NormalizeAxis(Yaw), 0)); } }
void ALDCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
    Camera->GetCameraView(DeltaTime,OutResult);
    // The fixed, collision-free arm's tick may precede movement. Evaluate its pose at camera time, not last tick.
    FVector Origin=CameraArm->GetComponentLocation();
    float ArmLength=CameraArm->TargetArmLength;
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
