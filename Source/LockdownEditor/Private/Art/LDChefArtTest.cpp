#include "Art/LDChefArtTest.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "Camera/CameraComponent.h"
#include "Character/LDCameraSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "NavigationSystem.h"
#include "ChaosCloth/ChaosClothingSimulationInteractor.h"
#include "ClothingAssetBase.h"
#include "UObject/ConstructorHelpers.h"
#include "LDEditorLog.h"

struct FLDChefAnimProxy : FAnimInstanceProxy
{
    FAnimNode_SequenceEvaluator_Standalone Idle,Run,Slash;
    FAnimNode_TwoWayBlend Move;
    FAnimNode_LayeredBoneBlend Attack;
    explicit FLDChefAnimProxy(UAnimInstance* Instance):FAnimInstanceProxy(Instance)
    {
        Move.A.SetLinkNode(&Idle);Move.B.SetLinkNode(&Run);
        Attack.BasePose.SetLinkNode(&Move);Attack.AddPose();Attack.BlendPoses[0].SetLinkNode(&Slash);
        FBranchFilter Filter;Filter.BoneName=TEXT("spine_01");Filter.BlendDepth=2;Attack.LayerSetup[0].BranchFilters.Add(Filter);Attack.bMeshSpaceRotationBlend=true;
    }
    virtual FAnimNode_Base* GetCustomRootNode() override {return &Attack;}
    virtual void GetCustomNodes(TArray<FAnimNode_Base*>& Nodes) override {Nodes.Append({&Idle,&Run,&Slash,&Move,&Attack});}
    virtual void PreUpdate(UAnimInstance* Instance,float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,Delta);
        auto* H=Cast<ALDChefArtTestCharacter>(Instance->TryGetPawnOwner());if(!H)return;
        Idle.SetSequence(H->IdleAnimation);Idle.SetExplicitTime(FMath::Fmod(H->PoseClock,2.f));
        Run.SetSequence(H->RunAnimation);Run.SetExplicitTime(FMath::Fmod(H->RunClock,.8f));
        Slash.SetSequence(H->SlashAnimation);Slash.SetExplicitTime(FMath::Clamp(H->ActionPhase,0.f,1.f)*1.2f);
        Move.Alpha=H->RunWeight;Attack.BlendWeights[0]=H->ActionWeight;
    }
};
FAnimInstanceProxy* ULDChefPreviewAnimInstance::CreateAnimInstanceProxy(){return new FLDChefAnimProxy(this);}
void ULDChefPreviewAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy){delete Proxy;}

ALDChefArtTestCharacter::ALDChefArtTestCharacter()
{
    PrimaryActorTick.bCanEverTick=true;AutoPossessPlayer=EAutoReceiveInput::Disabled;
    GetCapsuleComponent()->InitCapsuleSize(28,87.5f);GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);
    bUseControllerRotationYaw=false;
    auto* M=GetCharacterMovement();M->MaxWalkSpeed=420;M->MaxAcceleration=3000;M->BrakingDecelerationWalking=2400;M->RotationRate=FRotator(0,540,0);M->bOrientRotationToMovement=true;
    M->NavAgentProps.AgentRadius=28;M->NavAgentProps.AgentHeight=175;
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Body(TEXT("/Game/Lockdown/Art_Test/Characters/Chef/Meshes/SK_ChefNinja_Body"));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Cloth(TEXT("/Game/Lockdown/Art_Test/Characters/Chef/Meshes/SK_Chef_CapeClean"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Spoon(TEXT("/Game/Lockdown/Art_Test/Characters/Chef/Meshes/SM_Chef_IronLadle"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Idle(TEXT("/Game/Lockdown/Art_Test/Characters/Chef/Animations/AN_Chef_Idle"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Run(TEXT("/Game/Lockdown/Art_Test/Characters/Chef/Animations/AN_Chef_NinjaRun"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Cut(TEXT("/Game/Lockdown/Art_Test/Characters/Chef/Animations/AN_Chef_OneHandSlash"));
    IdleAnimation=Idle.Object;RunAnimation=Run.Object;SlashAnimation=Cut.Object;
    GetMesh()->SetSkeletalMeshAsset(Body.Object);GetMesh()->SetRelativeLocation(FVector(0,0,-87.5));GetMesh()->SetRelativeRotation(FRotator(0,-90,0));
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);GetMesh()->SetCanEverAffectNavigation(false);
    GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;GetMesh()->bEnableUpdateRateOptimizations=false;
    Cape=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Cape"));Cape->SetupAttachment(GetMesh());Cape->SetSkeletalMeshAsset(Cloth.Object);
    Cape->SetCollisionEnabled(ECollisionEnabled::NoCollision);Cape->SetCanEverAffectNavigation(false);Cape->bEnableUpdateRateOptimizations=false;Cape->bWaitForParallelClothTask=true;Cape->SetBoundsScale(2.5f);
    Cape->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Ladle=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IronLadle"));Ladle->SetupAttachment(GetMesh(),TEXT("Weapon_R"));Ladle->SetStaticMesh(Spoon.Object);Ladle->SetCollisionEnabled(ECollisionEnabled::NoCollision);Ladle->SetCanEverAffectNavigation(false);
    CameraArm=CreateDefaultSubobject<USpringArmComponent>(TEXT("ChefCameraArm"));CameraArm->SetupAttachment(GetRootComponent());CameraArm->SetUsingAbsoluteRotation(true);CameraArm->SetRelativeRotation(FRotator(LDCamera::Pitch,LDCamera::Yaw,0));CameraArm->TargetArmLength=LDCamera::MinArmLength;CameraArm->bDoCollisionTest=false;
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("ChefCamera"));Camera->SetupAttachment(CameraArm);Camera->FieldOfView=LDCamera::FieldOfView;
    Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness=true;Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness=true;Camera->PostProcessSettings.AutoExposureMinBrightness=0;Camera->PostProcessSettings.AutoExposureMaxBrightness=0;
    Camera->PostProcessSettings.bOverride_MotionBlurAmount=true;Camera->PostProcessSettings.MotionBlurAmount=0;
}
void ALDChefArtTestCharacter::BeginPlay()
{
    Super::BeginPlay();
    if(!GetMesh()->GetSkeletalMeshAsset()||!Cape->GetSkeletalMeshAsset()||!IdleAnimation||!RunAnimation||!SlashAnimation||!Ladle->GetStaticMesh()||!GetMesh()->DoesSocketExist(TEXT("Weapon_R")))
    {UE_LOG(LogLDEditor,Error,TEXT("LD_CHEF_MISSING_PREVIEW_ASSET"));GetCharacterMovement()->DisableMovement();return;}
    GetMesh()->SetAnimInstanceClass(ULDChefPreviewAnimInstance::StaticClass());Cape->SetAnimInstanceClass(ULDChefPreviewAnimInstance::StaticClass());
    Cape->AddTickPrerequisiteComponent(GetMesh());Ladle->AddTickPrerequisiteComponent(GetMesh());ResetCloth();
}
void ALDChefArtTestCharacter::SetPreviewAttackSpeed(float Rate){if(FMath::IsFinite(Rate))PreviewAttacksPerSecond=FMath::Clamp(Rate,.5f,3.f);}
void ALDChefArtTestCharacter::SetSprinting(bool Enabled){GetCharacterMovement()->MaxWalkSpeed=Enabled?620.f:420.f;}
void ALDChefArtTestCharacter::ResetCloth(){Cape->ForceClothNextUpdateTeleportAndReset();}
bool ALDChefArtTestCharacter::SlashAt(FVector Aim)
{
    if(ActionPhase>=0||Aim.ContainsNaN())return false;
    FVector Dir=Aim-GetActorLocation();Dir.Z=0;if(!Dir.IsNearlyZero())SetActorRotation(Dir.Rotation());
    ActionDuration=1.f/PreviewAttacksPerSecond;ActionPhase=0;bHitApplied=false;++SlashCount;
    UE_LOG(LogLDEditor,Display,TEXT("LD_CHEF_SLASH count=%d rate=%.1f"),SlashCount,PreviewAttacksPerSecond);return true;
}
void ALDChefArtTestCharacter::ApplyPreviewHit()
{
    // Match the ladle's forward swing arc, with world occlusion and one hit per target/cycle.
    for(TActorIterator<ALDArtTestMonster> It(GetWorld());It;++It)
    {
        FVector Delta=It->GetActorLocation()-GetActorLocation();Delta.Z=0;
        if(It->Health<=0||Delta.Size2D()>175||FVector::DotProduct(Delta.GetSafeNormal(),GetActorForwardVector())<.2f)continue;
        FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(ChefPreviewSlash),false,this);Params.AddIgnoredActor(*It);
        if(GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),It->GetActorLocation()+FVector(0,0,60),ECC_WorldStatic,Params))continue;
        if(UGameplayStatics::ApplyDamage(*It,15,GetController(),this,nullptr)>0)++HitCount;
    }
}
void ALDChefArtTestCharacter::Tick(float Delta)
{
    Super::Tick(Delta);const float Speed=GetVelocity().Size2D();ClothSpeed=Speed;PoseClock+=Delta;RunClock+=Delta*FMath::Clamp(Speed/300.f,.65f,2.f);
    RunWeight=FMath::FInterpTo(RunWeight,Speed>12?1.f:0.f,Delta,8.f);
    if(ActionPhase>=0)
    {
        ActionPhase+=Delta/ActionDuration;
        if(!bHitApplied&&ActionPhase>=.53f){ApplyPreviewHit();bHitApplied=true;}
        ActionWeight=FMath::Min(FMath::Clamp(ActionPhase/.12f,0.f,1.f),FMath::Clamp((1.f-ActionPhase)/.18f,0.f,1.f));
        if(ActionPhase>=1){ActionPhase=-1;ActionWeight=0;}
    }
    GetCharacterMovement()->bOrientRotationToMovement=ActionPhase<0;
    if(auto* Simulation=Cape->GetClothingSimulationInteractor())
    {
        for(const auto& Asset:Cape->GetSkeletalMeshAsset()->GetMeshClothingAssets())
        {
            if(auto* Cloth=Cast<UChaosClothingInteractor>(Simulation->GetClothingInteractor(Asset->GetFName())))
            {
                const float Amount=FMath::Clamp(Speed/420.f,0.f,1.5f);
                const FVector Wind=-GetVelocity()*.7f+FVector(0,0,Speed*.3f);
                Cloth->SetWind(FVector2D(.7f),FVector2D(.3f),1.225e-6f,Wind);Cloth->SetGravity(FMath::Lerp(.8f,.35f,FMath::Min(Amount,1.f)));
            }
        }
    }
}

bool ALDChefArtTestController::SwitchPreviewCharacter()
{
    if(!KobePawn)for(TActorIterator<ALDArtTestCharacter> It(GetWorld());It;++It){KobePawn=*It;break;}
    if(!ChefPawn)for(TActorIterator<ALDChefArtTestCharacter> It(GetWorld());It;++It){ChefPawn=*It;break;}
    if(!ChefPawn&&KobePawn)
    {
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        ChefPawn=GetWorld()->SpawnActor<ALDChefArtTestCharacter>(KobePawn->GetActorLocation()+FVector(0,220,0),KobePawn->GetActorRotation(),P);
    }
    APawn* Next=GetPawn()==ChefPawn?KobePawn.Get():ChefPawn.Get();if(!Next)return false;
    StopMovement();if(auto* Old=Cast<ACharacter>(GetPawn()))Old->GetCharacterMovement()->StopMovementImmediately();
    Possess(Next);SetViewTargetWithBlend(Next,.25f);++SwitchCount;
    if(Next==ChefPawn){ChefPawn->ResetCloth();ChefPawn->SetSprinting(false);}
    UE_LOG(LogLDEditor,Display,TEXT("LD_ART_SWITCH target=%s count=%d"),*Next->GetClass()->GetName(),SwitchCount);return true;
}
void ALDChefArtTestController::ChefAttack(bool Nearest)
{
    auto* H=Cast<ALDChefArtTestCharacter>(GetPawn());if(!H)return;
    FVector Aim=H->GetActorLocation()+H->GetActorForwardVector()*175;FHitResult Hit;
    if(Nearest){float Best=1500*1500;for(TActorIterator<ALDArtTestMonster> It(GetWorld());It;++It){float D=FVector::DistSquared(H->GetActorLocation(),It->GetActorLocation());if(It->Health>0&&D<Best){Best=D;Aim=It->GetActorLocation();}}}
    else if(GetHitResultUnderCursor(ECC_GameTraceChannel1,false,Hit))Aim=Hit.ImpactPoint;
    H->SlashAt(Aim);
}
void ALDChefArtTestController::PlayerTick(float Delta)
{
    Super::PlayerTick(Delta);
    if(WasInputKeyJustPressed(EKeys::Tab))SwitchPreviewCharacter();
    auto* H=Cast<ALDChefArtTestCharacter>(GetPawn());
    if(!H)return;

    auto Active=[this](const FKey& Key){return IsInputKeyDown(Key)||WasInputKeyJustPressed(Key);};
    if(Active(EKeys::LeftMouseButton))ChefAttack(false);if(Active(EKeys::F))ChefAttack(true);
    if(WasInputKeyJustPressed(EKeys::One))H->SetPreviewAttackSpeed(1);if(WasInputKeyJustPressed(EKeys::Two))H->SetPreviewAttackSpeed(2);if(WasInputKeyJustPressed(EKeys::Three))H->SetPreviewAttackSpeed(3);
    H->SetSprinting(Active(EKeys::LeftShift));
    if(WasInputKeyJustPressed(EKeys::SpaceBar)){StopMovement();H->GetCharacterMovement()->StopMovementImmediately();}
    if(WasInputKeyJustPressed(EKeys::R))for(TActorIterator<ALDArtTestMonster> It(GetWorld());It;++It)It->ResetTarget();
    if(WasInputKeyJustPressed(EKeys::RightMouseButton))
    {
        FHitResult Hit;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());FNavLocation Target;
        if(Nav&&GetHitResultUnderCursor(ECC_GameTraceChannel1,false,Hit)&&Nav->ProjectPointToNavigation(Hit.Location,Target,FVector(100,100,250)))UAIBlueprintHelperLibrary::SimpleMoveToLocation(this,Target.Location);
    }
    float F=float(Active(EKeys::W))-float(Active(EKeys::S)),R=float(Active(EKeys::D))-float(Active(EKeys::A));
    if(F!=0||R!=0){StopMovement();const FRotator Yaw(0,LDCamera::Yaw,0);H->AddMovementInput(Yaw.Vector(),F);H->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y),R);}
}
void ALDChefArtTestHUD::DrawHUD()
{
    auto* H=Cast<ALDChefArtTestCharacter>(GetOwningPawn());
    if(!H){Super::DrawHUD();if(Canvas)DrawText(TEXT("Tab 切换厨子 / 科比"),FLinearColor(.95f,.75f,.25f),32,145,GEngine->GetSmallFont(),1.1f);return;}
    AHUD::DrawHUD();if(!Canvas)return;
    DrawRect(FLinearColor(.012f,.02f,.035f,.86f),18,18,760,140);
    DrawText(TEXT("厨子 · 铁勺与披风测试    Tab 切换科比"),FLinearColor(.98f,.72f,.2f),32,28,GEngine->GetSmallFont(),1.3f);
    DrawText(TEXT("WASD / 右键 移动  |  Shift 加速忍者跑  |  空格停止"),FLinearColor::White,32,59,GEngine->GetSmallFont());
    DrawText(TEXT("按住左键 / F 连续挥砍  |  1 / 2 / 3 攻速  |  R 重置怪物"),FLinearColor::White,32,85,GEngine->GetSmallFont());
    DrawText(FString::Printf(TEXT("挥砍 %d 次 / 命中 %d 次  |  攻速 %.1f  |  移速 %.0f cm/s"),H->SlashCount,H->HitCount,H->PreviewAttacksPerSecond,H->GetVelocity().Size2D()),FLinearColor(.75f,.82f,.9f),32,115,GEngine->GetSmallFont());
}
ALDChefArtTestGameMode::ALDChefArtTestGameMode(){PlayerControllerClass=ALDChefArtTestController::StaticClass();HUDClass=ALDChefArtTestHUD::StaticClass();}
