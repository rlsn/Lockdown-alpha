#include "Character/LDBasketballAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

struct FLDBasketballAnimProxy : FAnimInstanceProxy
{
    FAnimNode_SequenceEvaluator_Standalone Base,Previous,Action;
    FAnimNode_TwoWayBlend Locomotion,Full;
    FAnimNode_LayeredBoneBlend Upper,Arm;
    FName LastArm;
    explicit FLDBasketballAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
    {
        Locomotion.A.SetLinkNode(&Previous);Locomotion.B.SetLinkNode(&Base);
        Full.A.SetLinkNode(&Locomotion);Full.B.SetLinkNode(&Action);
        Upper.BasePose.SetLinkNode(&Full);Upper.AddPose();Upper.BlendPoses[0].SetLinkNode(&Action);
        FBranchFilter Torso;Torso.BoneName=TEXT("spine_01");Torso.BlendDepth=1;
        Upper.LayerSetup[0].BranchFilters.Add(Torso);Upper.bMeshSpaceRotationBlend=true;
        Arm.BasePose.SetLinkNode(&Upper);Arm.AddPose();Arm.BlendPoses[0].SetLinkNode(&Action);
        FBranchFilter Hand;Hand.BoneName=TEXT("clavicle_r");Hand.BlendDepth=0;
        Arm.LayerSetup[0].BranchFilters.Add(Hand);Arm.bMeshSpaceRotationBlend=true;
        Base.SetTeleportToExplicitTime(true);Previous.SetTeleportToExplicitTime(true);Action.SetTeleportToExplicitTime(true);
    }
    virtual FAnimNode_Base* GetCustomRootNode() override { return &Arm; }
    virtual void GetCustomNodes(TArray<FAnimNode_Base*>& Nodes) override
    {Nodes.Append({&Base,&Previous,&Action,&Locomotion,&Full,&Upper,&Arm});}
    virtual void PreUpdate(UAnimInstance* Instance,float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,Delta);
        const auto* View=CastChecked<ULDBasketballAnimInstance>(Instance);
        Base.SetSequence(View->BaseSequence);Base.SetExplicitTime(View->BaseTime);
        Previous.SetSequence(View->PreviousSequence?View->PreviousSequence:View->BaseSequence);
        Previous.SetExplicitTime(View->PreviousSequence?View->PreviousTime:View->BaseTime);
        Action.SetSequence(View->ActionSequence?View->ActionSequence:View->BaseSequence);Action.SetExplicitTime(View->ActionTime);
        Locomotion.Alpha=View->BaseBlend;Full.Alpha=View->FullWeight;
        Upper.BlendWeights[0]=View->UpperWeight;Arm.BlendWeights[0]=View->ArmWeight;
        const FName Bone=View->bLeft?TEXT("clavicle_l"):TEXT("clavicle_r");
        if(Bone!=LastArm){Arm.LayerSetup[0].BranchFilters[0].BoneName=Bone;Arm.InvalidatePerBoneBlendWeights();LastArm=Bone;}
    }
};

FAnimInstanceProxy* ULDBasketballAnimInstance::CreateAnimInstanceProxy(){return new FLDBasketballAnimProxy(this);}
void ULDBasketballAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy){delete Proxy;}

void ULDBasketballAnimInstance::Bind(ULDBasketballMotionSet* InMotion,UStaticMeshComponent* InBall)
{
    Motion=InMotion;Ball=InBall;SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
    if(Motion&&!Motion->Idle.IsEmpty()){BaseSequence=Motion->Idle[0].Animation;}
}

void ULDBasketballAnimInstance::Prepare(double Clock,float Speed,float FacingYaw,LDBodyMotion::EAction Action,float Age,
    float Impact,double Identity,UAnimSequence* Override,float OverrideTime,bool bDash,bool bShowBall)
{
    if(!Motion){return;}
    const float Delta=LastClock<0?0:FMath::Clamp(float(Clock-LastClock),0.f,.25f);LastClock=Clock;
    // A released ball can finish its local follow-through after the authority cue ends. A cancelled windup cannot
    // invent a release; prediction's existing retract pose remains in control until it reaches rest.
    if(Action==LDBodyMotion::EAction::None&&LastAction!=LDBodyMotion::EAction::None&&bWasReleased
        &&CurrentAge+Delta<CurrentDuration&&!Override&&!bDash)
    {Action=LastAction;Age=CurrentAge+Delta;Impact=CurrentImpact;Identity=LastActionIdentity;}
    IdleAge=Speed<15?IdleAge+Delta:0;
    const auto& IdleBank=IdleAge>4&&FMath::Fmod(IdleAge,10.f)>4&&FMath::Fmod(IdleAge,10.f)<7.5f?Motion->Handling:Motion->Idle;
    const int32 IdleChoice=&IdleBank==&Motion->Handling?int32(IdleAge/10.f)%IdleBank.Num():
        (GetCurveValue(TEXT("BallLeft"))>.5f&&IdleBank.Num()>1?1:0);
    const auto& Loop=Speed>=15?Motion->Run[0]:IdleBank[IdleChoice];
    const float Rate=Speed>=15?FMath::Clamp(Speed/Motion->RunSpeedCmPerSec,.45f,3.f):1.f;
    if(BaseSequence!=Loop.Animation)
    {PreviousSequence=BaseSequence;PreviousTime=BaseTime;BaseSequence=Loop.Animation;BaseTime=0;BaseBlend=PreviousSequence?0:1;}
    const auto Advance=[](UAnimSequence* Sequence,float Time,float Dt){return Sequence&&Sequence->GetPlayLength()>0?FMath::Fmod(Time+Dt,Sequence->GetPlayLength()):0;};
    BaseTime=Advance(BaseSequence,BaseTime,Delta*Rate);PreviousTime=Advance(PreviousSequence,PreviousTime,Delta*Rate);
    BaseBlend=FMath::Min(1.f,BaseBlend+Delta/.12f);
    FullWeight=UpperWeight=ArmWeight=0;TurnFacing=0;ActionSequence=nullptr;
    bBallVisible=bShowBall&&!Override;bThrowing=false;
    const float TurnDelta=bHasFacing?FMath::FindDeltaAngleDegrees(LastFacing,FacingYaw):0;
    LastFacing=FacingYaw;bHasFacing=true;
    if(Override)
    {ActionSequence=Override;ActionTime=OverrideTime;FullWeight=1;TurnAt=-1;}
    else if(bDash)
    {
        Active=Motion->Crossovers[0];ActionSequence=Active.Animation;
        ActionTime=FMath::Lerp(Active.Start,Active.End,FMath::Clamp(Age/.45f,0.f,1.f));UpperWeight=1;TurnAt=-1;
    }
    else if(Action!=LDBodyMotion::EAction::None)
    {
        if(Action!=LastAction||FMath::Abs(Identity-LastActionIdentity)>LDBodyMotion::SameInstanceSeconds)
        {
            const auto& Choices=Action==LDBodyMotion::EAction::Attack?Motion->Passes:(Speed>60?Motion->MovingShots:Motion->StandingShots);
            // Follow the current dribbling hand; alternate only between clips with the same hand.
            const bool Left=GetCurveValue(TEXT("BallLeft"))>.5f;TArray<int32,TInlineAllocator<8>> Candidates;
            for(int32 I=0;I<Choices.Num();++I){if(Choices[I].bLeftHand==Left){Candidates.Add(I);}}
            Active=Choices[Candidates.IsEmpty()?0:Candidates[Variation++%Candidates.Num()]];
            bWasReleased=false;LastActionIdentity=Identity;TurnAt=-1;
        }
        CurrentAge=Age;CurrentImpact=FMath::Max(Impact,.001f);CurrentDuration=LDBasketballMotion::Duration(Active,CurrentImpact);
        ActionSequence=Active.Animation;ActionTime=LDBasketballMotion::SampleTime(Active,Age,CurrentImpact);bLeft=Active.bLeftHand;
        const float BlendIn=FMath::Min(.035f,CurrentImpact*.25f),BlendOut=FMath::Min(.14f,CurrentDuration*.28f);
        ActionWeight=FMath::Min(FMath::SmoothStep(0.f,BlendIn,FMath::Max(Age,0.f)),FMath::SmoothStep(0.f,BlendOut,CurrentDuration-Age));
        if(Action==LDBodyMotion::EAction::Attack){UpperWeight=ActionWeight*.42f;ArmWeight=ActionWeight;}
        else if(Speed>60){UpperWeight=ActionWeight;}else{FullWeight=ActionWeight;}
        bThrowing=Age>=0&&Age<CurrentDuration;
    }
    else
    {
        bWasReleased=false;
        if(bDash)
        {
            Active=Motion->Crossovers[0];ActionSequence=Active.Animation;
            ActionTime=FMath::Lerp(Active.Start,Active.End,FMath::Fmod(float(Clock),.45f)/.45f);UpperWeight=1;
        }
        else
        {
            if(Speed>80&&FMath::Abs(TurnDelta)>105&&Clock>NextTurnAt)
            {
                Active=Motion->Turns[TurnDelta<0&&Motion->Turns.Num()>1?1:0];
                TurnAt=Clock;TurnDuration=FMath::Clamp(FMath::Abs(TurnDelta)/300.f,.38f,.60f);NextTurnAt=Clock+.6;++Turns;
            }
            if(TurnAt>=0&&Clock-TurnAt<TurnDuration)
            {
                const float AgeTurn=float(Clock-TurnAt);ActionSequence=Active.Animation;
                ActionTime=FMath::Lerp(Active.Start,Active.End,AgeTurn/TurnDuration);
                FullWeight=FMath::Min(FMath::SmoothStep(0.f,.07f,AgeTurn),FMath::SmoothStep(0.f,.1f,TurnDuration-AgeTurn));
                TurnFacing=FMath::UnwindDegrees(Active.Animation->EvaluateCurveData(TEXT("TurnFacing"),FAnimExtractContext(ActionTime,false),true))*FullWeight;
            }
            else{TurnAt=-1;}
        }
    }
    LastAction=Action;
    if(Ball){Ball->SetHiddenInGame(!bBallVisible);}
}

void ULDBasketballAnimInstance::NativePostEvaluateAnimation()
{
    Super::NativePostEvaluateAnimation();
    auto* Mesh=GetSkelMeshComponent();
    if(!Motion||!Ball||!Mesh||!bBallVisible||!Mesh->IsVisible()||Mesh->bHiddenInGame||!Mesh->GetOwner()||Mesh->GetOwner()->IsHidden())
    {if(Ball){Ball->SetHiddenInGame(true);}return;}
    for(auto* Parent=Mesh->GetAttachParent();Parent;Parent=Parent->GetAttachParent())
    {if(!Parent->IsVisible()||Parent->bHiddenInGame){Ball->SetHiddenInGame(true);return;}}
    const auto Palm=[&](bool Left)
    {return Mesh->GetSocketTransform(Left?TEXT("hand_l"):TEXT("hand_r")).TransformPosition(Left?Motion->LeftPalm:Motion->RightPalm);};
    const bool Left=GetCurveValue(TEXT("BallLeft"))>.5f;
    const FVector Hand=Palm(bThrowing?bLeft:Left);
    const FVector Local(GetCurveValue(TEXT("BallX")),GetCurveValue(TEXT("BallY")),GetCurveValue(TEXT("BallZ")));
    FVector Desired=Local.IsNearlyZero()?Hand:Mesh->GetComponentTransform().TransformPosition(Local);
    Desired=FMath::Lerp(Desired,Palm(Left),FMath::Clamp(GetCurveValue(TEXT("BallContact")),0.f,1.f));
    if(bThrowing)
    {
        if(CurrentAge<CurrentImpact)
        {Desired=FMath::Lerp(Desired,Hand,FMath::SmoothStep(0.f,FMath::Max(CurrentImpact*.8f,.001f),CurrentAge));}
        else
        {
            if(!bWasReleased){ReleasePoint=Hand;ReleaseForward=Mesh->GetOwner()->GetActorForwardVector();bWasReleased=true;}
            const float U=FMath::Clamp((CurrentAge-CurrentImpact)/FMath::Max(CurrentDuration-CurrentImpact,.001f),0.f,1.f);
            // Display-only throw and return in the owner's public facing. No target lookup, collision, damage or RPC.
            const FVector Far=ReleasePoint+ReleaseForward*120.f;
            if(U<.45f){const float T=U/.45f;Desired=FMath::Lerp(ReleasePoint,Far,T)+FVector(0,0,40*FMath::Sin(PI*T));}
            else{const float T=(U-.45f)/.55f;Desired=FMath::Lerp(Far,Hand,FMath::SmoothStep(0.f,1.f,T))+FVector(0,0,-30*FMath::Sin(PI*T));}
        }
    }
    // Keep the ball above the current physical ground, including ramps, without adding any collision body.
    FHitResult Ground;FCollisionQueryParams Query(SCENE_QUERY_STAT(BasketballVisualGround),false,Mesh->GetOwner());
    if(Mesh->GetWorld()->LineTraceSingleByChannel(Ground,Desired+FVector(0,0,20),Desired-FVector(0,0,250),ECC_WorldStatic,Query))
    {Desired.Z=FMath::Max(Desired.Z,Ground.ImpactPoint.Z+Motion->BallRadiusCm);}
    // The old basketball system's calibrated palm disk and finger clearance. Sockets alone are inside the skin;
    // the ball centre must stay on the palm side even while the torso and throwing arm are blended separately.
    const float Radius=Motion->BallRadiusCm;
    for(bool Side:{false,true})
    {
        const FVector Normal=Mesh->GetSocketQuaternion(Side?TEXT("hand_l"):TEXT("hand_r")).RotateVector(Side?Motion->LeftPalmNormal:Motion->RightPalmNormal).GetSafeNormal();
        const FVector Centre=Palm(Side)-Normal*Radius,D=Desired-Centre;
        const float Depth=FVector::DotProduct(D,Normal),Lateral=(D-Normal*Depth).Size();
        if(Depth>-Radius-2&&Depth<Radius&&Lateral<Radius+5)
        {
            const float Edge=FMath::Max(0.f,Lateral-5),Clearance=FMath::Sqrt(FMath::Max(0.f,Radius*Radius-Edge*Edge));
            if(Depth<Clearance){Desired+=Normal*(Clearance-Depth);}
        }
    }
    for(int32 Pass=0;Pass<2;++Pass){for(bool Side:{false,true})
    {
        const TCHAR* S=Side?TEXT("l"):TEXT("r");
        const FVector Normal=Mesh->GetSocketQuaternion(Side?TEXT("hand_l"):TEXT("hand_r")).RotateVector(Side?Motion->LeftPalmNormal:Motion->RightPalmNormal).GetSafeNormal();
        for(const TCHAR* Finger:{TEXT("thumb"),TEXT("index"),TEXT("middle"),TEXT("ring"),TEXT("pinky")}){for(int32 Joint=1;Joint<3;++Joint)
        {
            const FVector A=Mesh->GetSocketLocation(FName(*FString::Printf(TEXT("%s_0%d_%s"),Finger,Joint,S)));
            const FVector B=Mesh->GetSocketLocation(FName(*FString::Printf(TEXT("%s_0%d_%s"),Finger,Joint+1,S)));
            const FVector D=Desired-FMath::ClosestPointOnSegment(Desired,A,B);const float Clearance=Radius+1.2f;
            if(D.SizeSquared()<Clearance*Clearance)
            {const float Along=FVector::DotProduct(D,Normal);Desired+=Normal*(-Along+FMath::Sqrt(FMath::Max(0.f,Along*Along+Clearance*Clearance-D.SizeSquared())));}
        }}
    }}
    Ball->SetWorldLocation(Desired);Ball->SetHiddenInGame(false);
    Ball->SetWorldRotation(FRotator(float(LastClock)*340,float(LastClock)*40,0));
}
