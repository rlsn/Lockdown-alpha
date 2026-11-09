#include "Art/LDBasketballPreview.h"
#include "Art/LDArtTestCharacter.h"
#include "LDEditorLog.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/UnrealType.h"

struct FLDBasketballAnimProxy : public FAnimInstanceProxy
{
    FAnimNode_MotionMatching Motion;
    FAnimNode_PoseSearchHistoryCollector History;
    FAnimNode_SequenceEvaluator_Standalone ActionPose;
    FAnimNode_TwoWayBlend FullBody;
    FAnimNode_LayeredBoneBlend UpperBody;
    FAnimNode_LayeredBoneBlend PassArm;
    FAnimNode_ConvertLocalToComponentSpace ToComponent;
    FAnimNode_ModifyBone Aim;
    FAnimNode_ConvertComponentToLocalSpace ToLocal;
    FName LastArmFilter;
    explicit FLDBasketballAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
    {
        Motion.SetMaxActiveBlends(4);
        auto SetFloat=[&](const TCHAR* Name,float Value)
        {
            FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(),Name)->SetPropertyValue_InContainer(&Motion,Value);
        };
        SetFloat(TEXT("BlendTime"),.10f);
        SetFloat(TEXT("SearchThrottleTime"),.045f);
        SetFloat(TEXT("PoseReselectHistory"),.3f);
        auto* Rate=FindFProperty<FStructProperty>(FAnimNode_MotionMatching::StaticStruct(),TEXT("PlayRate"));
        *Rate->ContainerPtrToValuePtr<FFloatInterval>(&Motion)=FFloatInterval(.75f,1.25f);
        FullBody.A.SetLinkNode(&Motion);
        FullBody.B.SetLinkNode(&ActionPose);
        UpperBody.BasePose.SetLinkNode(&FullBody);
        UpperBody.AddPose();
        UpperBody.BlendPoses[0].SetLinkNode(&ActionPose);
        FBranchFilter Filter; Filter.BoneName=TEXT("spine_01"); Filter.BlendDepth=1;
        UpperBody.LayerSetup[0].BranchFilters.Add(Filter);
        UpperBody.bMeshSpaceRotationBlend=true;
        PassArm.BasePose.SetLinkNode(&UpperBody);
        PassArm.AddPose(); PassArm.BlendPoses[0].SetLinkNode(&ActionPose);
        FBranchFilter ArmFilter; ArmFilter.BoneName=TEXT("clavicle_r"); ArmFilter.BlendDepth=0;
        PassArm.LayerSetup[0].BranchFilters.Add(ArmFilter); PassArm.bMeshSpaceRotationBlend=true;
        ToComponent.LocalPose.SetLinkNode(&PassArm);
        Aim.ComponentPose.SetLinkNode(&ToComponent);
        Aim.BoneToModify.BoneName=TEXT("spine_01");
        Aim.RotationMode=BMM_Additive; Aim.RotationSpace=BCS_ComponentSpace; Aim.Alpha=1;
        ToLocal.ComponentPose.SetLinkNode(&Aim);
        History.Source.SetLinkNode(&ToLocal);
        History.bGenerateTrajectory=true;
        History.bStoreScales=true;
        History.PoseCount=12;
        History.SamplingInterval=.04f;
        History.PredictionSamplingInterval=.2f;
        for (const TCHAR* Name:{TEXT("pelvis"),TEXT("foot_l"),TEXT("foot_r"),TEXT("hand_l"),TEXT("hand_r")})
        {
            FBoneReference Bone; Bone.BoneName=Name; History.CollectedBones.Add(Bone);
        }
    }
    virtual FAnimNode_Base* GetCustomRootNode() override { return &History; }
    virtual void GetCustomNodes(TArray<FAnimNode_Base*>& Nodes) override
    {
        Nodes.Append({&Motion,&History,&ActionPose,&FullBody,&UpperBody,&PassArm,&ToComponent,&Aim,&ToLocal});
    }
    virtual void PreUpdate(UAnimInstance* Instance,float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,Delta);
        const auto* Hero=Cast<ALDArtTestCharacter>(Instance->TryGetPawnOwner());
        if (!Hero || !Hero->MotionSet) { return; }
        auto* Database=Hero->bMoving ? Hero->MotionSet->Dribble.Get():
            ((Hero->bHandlingVariation && Hero->Action==ELDBasketballAction::None && !Hero->bCarryPose) ? Hero->MotionSet->Handling.Get():Hero->MotionSet->Idle.Get());
        auto* Rate=FindFProperty<FStructProperty>(FAnimNode_MotionMatching::StaticStruct(),TEXT("PlayRate"));
        *Rate->ContainerPtrToValuePtr<FFloatInterval>(&Motion)=FFloatInterval(Hero->LocomotionPlayRate,Hero->LocomotionPlayRate);
        Motion.SetDatabaseToSearch(Database,EPoseSearchInterruptMode::InterruptOnDatabaseChange);
        // Search on a changed/invalid bank, then preserve the chosen loop's phase.
        // Re-searching a single run every few frames caused rapid foot and prop jumps.
        const bool ChangedBank=Motion.GetMotionMatchingState().SearchResult.SelectedDatabase!=Database;
        FindFProperty<FBoolProperty>(FAnimNode_MotionMatching::StaticStruct(),TEXT("bShouldSearch"))->SetPropertyValue_InContainer(&Motion,ChangedBank);
        ActionPose.SetSequence(Hero->ActiveClip.Animation ? Hero->ActiveClip.Animation.Get():Hero->IdleAnimation.Get());
        ActionPose.SetExplicitTime(Hero->GetActionSampleTime());
        const bool Upper=Hero->Action==ELDBasketballAction::Pass || Hero->Action==ELDBasketballAction::Catch || Hero->Action==ELDBasketballAction::Crossover;
        // Preserve the pack's chest/shoulder action at restrained amplitude,
        // while retaining the throwing arm and the locomotion leg phase.
        const FName Arm=Hero->bLeftHand ? FName(TEXT("clavicle_l")):FName(TEXT("clavicle_r"));
        if (Arm!=LastArmFilter)
        {
            PassArm.LayerSetup[0].BranchFilters[0].BoneName=Arm;
            PassArm.InvalidatePerBoneBlendWeights(); LastArmFilter=Arm;
        }
        FullBody.Alpha=Upper ? 0.f:Hero->ActionWeight;
        const float TorsoWeight=Hero->Action==ELDBasketballAction::Pass ? .42f:
            (Hero->Action==ELDBasketballAction::Catch ? .65f:1.f);
        UpperBody.BlendWeights[0]=Upper ? Hero->ActionWeight*TorsoWeight:0.f;
        PassArm.BlendWeights[0]=Hero->Action==ELDBasketballAction::Pass ? Hero->ActionWeight:0.f;
        Aim.Rotation=FRotator(0,Hero->UpperBodyAimYaw,0);
    }
    virtual void PostUpdate(UAnimInstance* Instance) const override
    {
        FAnimInstanceProxy::PostUpdate(Instance);
        if (auto* Hero=Cast<ALDArtTestCharacter>(Instance->TryGetPawnOwner()))
        {
            const auto& Result=Motion.GetMotionMatchingState().SearchResult;
            if (Result.SelectedAnim)
            {
                const FString Name=Result.SelectedAnim->GetName();
                if (Hero->MatchedAnimation!=Name) { ++Hero->MatchChanges; Hero->MatchedAnimation=Name; }
                Hero->MatchedTime=Motion.GetCurrentAssetTime();
                Hero->MatchCost=Result.SearchCost;
            }
        }
    }
};

FAnimInstanceProxy* ULDBasketballPreviewAnimInstance::CreateAnimInstanceProxy() { return new FLDBasketballAnimProxy(this); }
void ULDBasketballPreviewAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }

ALDBasketballPreviewBall::ALDBasketballPreviewBall()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    BallMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Basketball"));
    SetRootComponent(BallMesh);
    BallMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BallMesh->SetCanEverAffectNavigation(false);
}

void ALDBasketballPreviewBall::Initialize(ALDArtTestCharacter* InHero)
{
    Hero=InHero; SetOwner(Hero);
    BallMesh->SetStaticMesh(Hero->BasketballMesh);
    SetActorScale3D(FVector(.24f));
    AddTickPrerequisiteComponent(Hero->GetMesh());
    SetActorLocation(HandPosition());
}

FVector ALDBasketballPreviewBall::HandPosition() const { return Hero->GetBasketballHand(bThrowLeft); }

float ALDBasketballPreviewBall::GroundHeight(const FVector& Point) const
{
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BasketballFloor),false,Hero); Query.AddIgnoredActor(this);
    const FVector Top(Point.X,Point.Y,Hero->GetActorLocation().Z+250);
    return GetWorld()->LineTraceSingleByChannel(Hit,Top,Top-FVector(0,0,1500),ECC_GameTraceChannel1,Query)
        ? Hit.ImpactPoint.Z+12:Hero->GetActorLocation().Z-78;
}

bool ALDBasketballPreviewBall::ThrowAt(const FVector& Point)
{
    if (!Hero || State!=ELDBasketballPreviewState::Dribble) { return false; }
    Cycle=Hero->AttackCycle; ReleaseDelay=Hero->BallReleaseTime;
    bSkill=Hero->Action==ELDBasketballAction::Shot; bThrowLeft=Hero->bLeftHand;
    State=ELDBasketballPreviewState::Gather;
    Goal=Point; GatherStart=GetActorLocation(); Age=0; FlightTime=0;
    ++ThrowCount;
    UE_LOG(LogLDEditor,Display,TEXT("LD_BASKETBALL_THROW id=%d moving=%d skill=%d cycle=%.3f release=%.3f"),ThrowCount,Hero->GetVelocity().Size2D()>5,bSkill,Cycle,ReleaseDelay);
    return true;
}

void ALDBasketballPreviewBall::StartBounce()
{
    State=ELDBasketballPreviewState::ToBounce;
    Start=GetActorLocation();
    LastImpactPoint=Start;
    ImpactHeroPosition=Hero->GetActorLocation();
    BouncePoint=(Start+ImpactHeroPosition)*.5f;
    BouncePoint.Z=GroundHeight(BouncePoint);
    LastBouncePoint=BouncePoint;
    FlightTime=0;
    FlightDuration=Cycle*(bSkill ? .18f:.23f);
}

void ALDBasketballPreviewBall::UpdateDribble(float Delta,const FVector& Hand)
{
    const UAnimInstance* Anim=Hero->GetMesh()->GetAnimInstance();
    if (!Anim) { return; }
    const FVector Local(Anim->GetCurveValue(TEXT("BallX")),Anim->GetCurveValue(TEXT("BallY")),Anim->GetCurveValue(TEXT("BallZ")));
    FVector Desired=Local.IsNearlyZero() ? Hand:Hero->GetMesh()->GetComponentTransform().TransformPosition(Local);
    const float Contact=FMath::Clamp(Anim->GetCurveValue(TEXT("BallContact")),0.f,1.f);
    const bool Left=Anim->GetCurveValue(TEXT("BallLeft"))>.5f;
    Desired=FMath::Lerp(Desired,Hero->GetBasketballHand(Left),Contact);
    Desired.Z=FMath::Max(Desired.Z,GroundHeight(Desired));
    if (CatchBlend<1)
    {
        CatchBlend=FMath::Min(1.f,CatchBlend+Delta/.12f);
        Desired+=CatchOffset*(1-FMath::SmoothStep(0.f,1.f,CatchBlend));
    }
    SetActorLocation(Hero->KeepBallOutsideHands(Desired));
    AddActorLocalRotation(FRotator(Delta*340,Delta*40,0));
}

void ALDBasketballPreviewBall::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!IsValid(Hero)) { Destroy(); return; }
    // A short attack can cross several flight phases in one rendered frame.
    // Consume the elapsed time in bounded steps so a hitch cannot add a frame
    // to every phase and silently reduce the preview's attack-speed cadence.
    if (State==ELDBasketballPreviewState::Dribble) { UpdateDribble(Delta,HandPosition()); return; }
    float Remaining=Delta;
    while (Remaining>UE_SMALL_NUMBER && State!=ELDBasketballPreviewState::Dribble)
    {
        const float Step=FMath::Min(Remaining,1.f/240.f);
        AdvanceFlight(Step);
        Remaining-=Step;
    }
    if (Remaining>UE_SMALL_NUMBER) { UpdateDribble(Remaining,HandPosition()); }
}

void ALDBasketballPreviewBall::AdvanceFlight(float Delta)
{
    const FVector Hand=HandPosition();
    Age+=Delta;
    if (State==ELDBasketballPreviewState::Gather)
    {
        const float U=FMath::Clamp(Age/FMath::Max(ReleaseDelay*.8f,.001f),0.f,1.f);
        SetActorLocation(Hero->KeepBallOutsideHands(FMath::Lerp(GatherStart,Hand,FMath::SmoothStep(0.f,1.f,U))));
        if (Age>=ReleaseDelay)
        {
            Start=Hand;
            Goal=Start+(Goal-Start).GetClampedToMaxSize(1200);
            FlightDuration=Cycle*(bSkill ? .22f:.28f);
            FlightTime=Age-ReleaseDelay;
            State=ELDBasketballPreviewState::Outbound;
            LastReleaseDelay=Age;
        }
    }
    else if (State==ELDBasketballPreviewState::Outbound)
    {
        FlightTime+=Delta;
        const float U=FMath::Clamp(FlightTime/FlightDuration,0.f,1.f);
        const FVector Next=FMath::Lerp(Start,Goal,U)+FVector(0,0,(bSkill ? 210.f:22.f)*FMath::Sin(PI*U));
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BasketballHit),false,Hero); Query.AddIgnoredActor(this);
        FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_WorldStatic);
        if (GetWorld()->SweepSingleByObjectType(Hit,GetActorLocation(),Next,FQuat::Identity,Objects,FCollisionShape::MakeSphere(12),Query))
        {
            SetActorLocation(Hit.Location);
            if (auto* Target=Cast<ALDArtTestMonster>(Hit.GetActor()))
            {
                const float Applied=UGameplayStatics::ApplyDamage(Target,bSkill ? 50:25,Hero->GetController(),this,nullptr);
                if (Applied>0) { ++HitCount; }
            }
            StartBounce();
        }
        else { SetActorLocation(Next); if (U>=1) { StartBounce(); } }
    }
    else if (State==ELDBasketballPreviewState::ToBounce)
    {
        FlightTime+=Delta;
        const float U=FMath::Clamp(FlightTime/FlightDuration,0.f,1.f);
        FVector Next=FMath::Lerp(Start,BouncePoint,U);
        Next.Z=FMath::Lerp(Start.Z,BouncePoint.Z,U*U);
        SetActorLocation(Next);
        if (U>=1)
        {
            ++BounceCount; ReturnStart=BouncePoint; FlightTime=FMath::Max(0.f,FlightTime-FlightDuration);
            FlightDuration=Cycle*(bSkill ? .12f:.25f);
            State=ELDBasketballPreviewState::Returning;
        }
    }
    else
    {
        FlightTime+=Delta;
        const float U=FMath::Clamp(FlightTime/FlightDuration,0.f,1.f);
        FVector Next=FMath::Lerp(ReturnStart,Hand,U);
        Next.Z=FMath::Lerp(ReturnStart.Z,Hand.Z,2*U-U*U);
        SetActorLocation(Hero->KeepBallOutsideHands(Next));
        if (U>=1)
        {
            State=ELDBasketballPreviewState::Dribble; ++ReturnCount; LastCycleDuration=Age;
            Hero->ReceiveBasketball();
            const auto* Anim=Hero->GetMesh()->GetAnimInstance();
            const FVector Local(Anim->GetCurveValue(TEXT("BallX")),Anim->GetCurveValue(TEXT("BallY")),Anim->GetCurveValue(TEXT("BallZ")));
            CatchOffset=Hand-Hero->GetMesh()->GetComponentTransform().TransformPosition(Local); CatchBlend=0;
            UE_LOG(LogLDEditor,Display,TEXT("LD_BASKETBALL_RETURN id=%d hits=%d bounce=%d duration=%.3f"),ThrowCount,HitCount,BounceCount,Age);
        }
    }
    AddActorLocalRotation(FRotator(Delta*720,Delta*90,0));
}

ALDArtTestMonster::ALDArtTestMonster()
{
    PrimaryActorTick.bCanEverTick=true;
    HitBody=CreateDefaultSubobject<UCapsuleComponent>(TEXT("TargetCollision"));
    SetRootComponent(HitBody);
    HitBody->InitCapsuleSize(60,115);
    HitBody->SetCollisionObjectType(ECC_Pawn);
    HitBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    HitBody->SetCollisionResponseToAllChannels(ECR_Ignore);
    HitBody->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    HitBody->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
    HitBody->SetCanEverAffectNavigation(false);
    Appearance=CreateDefaultSubobject<USceneComponent>(TEXT("GolemAppearance"));
    Appearance->SetupAttachment(HitBody);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone"));
    auto Part=[&](const TCHAR* Name,FVector Position,FVector Scale,bool Horn=false)
    {
        auto* Piece=CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Piece->SetupAttachment(Appearance);
        Piece->SetStaticMesh(Horn ? Cone.Object:Sphere.Object);
        Piece->SetRelativeLocation(Position);
        Piece->SetRelativeScale3D(Scale);
        Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Piece->SetCanEverAffectNavigation(false);
        return Piece;
    };
    Part(TEXT("StoneTorso"),FVector(0,0,0),FVector(.95f,1.1,1.2));
    Part(TEXT("Head"),FVector(4,0,70),FVector(.72f,.76f,.64f));
    Part(TEXT("Brow"),FVector(33,0,81),FVector(.2f,.7f,.15f));
    for (int32 Side: {-1,1})
    {
        const FString S=Side<0 ? TEXT("L"):TEXT("R");
        Part(*(TEXT("Shoulder")+S),FVector(0,Side*63,22),FVector(.58f,.55f,.55f));
        Part(*(TEXT("Arm")+S),FVector(3,Side*70,-21),FVector(.45f,.42f,.9f));
        Part(*(TEXT("Fist")+S),FVector(12,Side*72,-55),FVector(.52f,.48f,.48f));
        Part(*(TEXT("Leg")+S),FVector(0,Side*28,-67),FVector(.4f,.43f,.75f));
        Part(*(TEXT("Foot")+S),FVector(10,Side*28,-99),FVector(.60f,.46f,.29f));
        Part(*(TEXT("Eye")+S),FVector(36,Side*15,71),FVector(.08f,.17f,.14f));
        auto* Horn=Part(*(TEXT("Horn")+S),FVector(0,Side*26,112),FVector(.22f,.22f,.6f),true);
        Horn->SetRelativeRotation(FRotator(0,0,Side*25));
    }
}

void ALDArtTestMonster::BeginPlay()
{
    Super::BeginPlay();
    FHitResult Floor;
    const FVector Position=GetActorLocation();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ArtTargetFloor),false,this);
    if (GetWorld()->LineTraceSingleByChannel(Floor,Position+FVector(0,0,300),Position-FVector(0,0,2000),ECC_GameTraceChannel1,Query))
    {
        SetActorLocation(Floor.ImpactPoint+FVector(0,0,115));
    }
    auto* Stone=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Lockdown/Art_Test/Basketball/M_GolemStone.M_GolemStone"));
    auto* Eye=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Lockdown/Art_Test/Basketball/M_GolemEye.M_GolemEye"));
    auto* Dark=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Lockdown/Art_Test/Basketball/M_GolemDark.M_GolemDark"));
    TInlineComponentArray<UStaticMeshComponent*> Parts(this);
    for (auto* Part:Parts)
    {
        Part->SetMaterial(0,Part->GetName().StartsWith(TEXT("Eye")) ? Eye:(Part->GetName().StartsWith(TEXT("Horn")) ? Dark:Stone));
    }
    ResetTarget();
}

void ALDArtTestMonster::ResetTarget()
{
    Health=PreviewMaxHealth;
    DefeatTime=-10;
    HitTime=-10;
    Appearance->SetRelativeRotation(FRotator::ZeroRotator);
    Appearance->SetRelativeLocation(FVector::ZeroVector);
    Appearance->SetRelativeScale3D(FVector::OneVector);
    HitBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

float ALDArtTestMonster::TakeDamage(float Damage,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer)
{
    if (Health<=0 || Damage<=0) { return 0; }
    const float Applied=FMath::Min(Damage,Health);
    Health-=Applied;
    ++TotalHits;
    HitTime=GetWorld()->GetTimeSeconds();
    if (Health<=0)
    {
        ++DefeatCount;
        DefeatTime=HitTime;
        HitBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    UE_LOG(LogLDEditor,Display,TEXT("LD_BASKETBALL_HIT hp=%.0f hits=%d defeats=%d"),Health,TotalHits,DefeatCount);
    return Applied;
}

void ALDArtTestMonster::Tick(float Delta)
{
    Super::Tick(Delta);
    const float Now=GetWorld()->GetTimeSeconds();
    if (Health<=0)
    {
        const float T=Now-DefeatTime;
        Appearance->SetRelativeRotation(FRotator(FMath::Clamp(T*110.f,0.f,80.f),0,0));
        Appearance->SetRelativeLocation(FVector(0,0,-FMath::Clamp(T*100.f,0.f,65.f)));
        if (T>3.f) { ResetTarget(); }
    }
    else
    {
        const float Hit=FMath::Clamp(1.f-(Now-HitTime)/.35f,0.f,1.f);
        Appearance->SetRelativeRotation(FRotator(-Hit*14,0,0));
        Appearance->SetRelativeScale3D(FVector(1+Hit*.08f,1+Hit*.08f,1-Hit*.06f+FMath::Sin(Now*2)*.008f));
    }
}

void ALDBasketballPreviewHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) { return; }
    DrawRect(FLinearColor(.012f,.02f,.035f,.84f),18,18,670,113);
    DrawText(TEXT("篮球 · 攻击测试"),FLinearColor(.98f,.63f,.13f),32,28,GEngine->GetSmallFont(),1.35f);
    DrawText(TEXT("WASD 移动  |  按住左键 / F 连续普攻  |  右键移动"),FLinearColor::White,32,56,GEngine->GetSmallFont());
    DrawText(TEXT("Q 双手投篮  |  Shift 胯下运球冲刺  |  1 / 2 / 3 切换攻速"),FLinearColor(.68f,.77f,.85f),32,79,GEngine->GetSmallFont());
    auto* Hero=Cast<ALDArtTestCharacter>(GetOwningPawn());
    if (Hero && Hero->Basketball)
    {
        const auto* Ball=Hero->Basketball.Get();
        DrawText(FString::Printf(TEXT("攻速 %.1f 次/秒  |  R 重置怪物  |  空格停止  |  Motion Matching"),Hero->PreviewAttacksPerSecond),FLinearColor(.68f,.77f,.85f),32,102,GEngine->GetSmallFont());
        const TCHAR* States[]={TEXT("运球中"),TEXT("快速出手"),TEXT("篮球飞行中"),TEXT("中点落地"),TEXT("回收篮球")};
        DrawText(FString::Printf(TEXT("%s    投球 %d 次 / 命中 %d 次"),States[int32(Ball->State)],Ball->ThrowCount,Ball->HitCount),FLinearColor(.98f,.63f,.13f),32,Canvas->SizeY-45,GEngine->GetSmallFont(),1.15f);
    }
    for (TActorIterator<ALDArtTestMonster> It(GetWorld());It;++It)
    {
        FVector2D Point;
        if (!PlayerOwner->ProjectWorldLocationToScreen(It->GetActorLocation()+FVector(0,0,155),Point,true)) { continue; }
        DrawRect(FLinearColor(.02f,.03f,.04f,.9f),Point.X-64,Point.Y,128,12);
        DrawRect(FLinearColor(.91f,.22f,.10f),Point.X-62,Point.Y+2,124*FMath::Clamp(It->Health/FMath::Max(It->PreviewMaxHealth,1.f),0.f,1.f),8);
        DrawText(It->Health>0 ? FString::Printf(TEXT("测试石头怪  %.0f / %.0f"),It->Health,It->PreviewMaxHealth):TEXT("已击倒 · 即将恢复"),FLinearColor::White,Point.X-73,Point.Y-22,GEngine->GetSmallFont());
    }
}
