#include "Character/LDBasketballAnimInstance.h"
#include "Character/LDActionMotion.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

struct FLDBasketballActionEvaluator : FAnimNode_SequenceEvaluator_Standalone
{
    bool bNormalizeTurn = false;

    virtual void Evaluate_AnyThread(FPoseContext& Output) override
    {
        FAnimNode_SequenceEvaluator_Standalone::Evaluate_AnyThread(Output);
        if (!bNormalizeTurn || Output.Pose.GetNumBones() == 0) { return; }

        // The authored spin includes nearly a full revolution. Normalize it in the sampled
        // pose before blending, so only the character's presentation yaw turns the body.
        const float AuthoredYaw = Output.Curve.Get(TEXT("TurnFacing"));
        const FQuat Correction(FVector::UpVector, FMath::DegreesToRadians(-AuthoredYaw));
        FTransform& Root = Output.Pose[FCompactPoseBoneIndex(0)];
        Root.SetRotation((Correction * Root.GetRotation()).GetNormalized());

        // Ball curves are expressed in the same component frame as the authored turn.
        const FVector BallPosition(Output.Curve.Get(TEXT("BallX")), Output.Curve.Get(TEXT("BallY")), 0);
        const FVector NormalizedBall = Correction.RotateVector(BallPosition);
        Output.Curve.Set(TEXT("BallX"), NormalizedBall.X);
        Output.Curve.Set(TEXT("BallY"), NormalizedBall.Y);
    }
};

struct FLDBasketballAnimProxy : FAnimInstanceProxy
{
    FAnimNode_SequenceEvaluator_Standalone Base,Previous;
    FLDBasketballActionEvaluator Action;
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
        Action.bNormalizeTurn=View->bNormalizeTurn;
        Locomotion.Alpha=View->BaseBlend;Full.Alpha=View->FullWeight;
        Upper.BlendWeights[0]=View->UpperWeight;Arm.BlendWeights[0]=View->ArmWeight;
        const FName Bone=View->bLeft?TEXT("clavicle_l"):TEXT("clavicle_r");
        if(Bone!=LastArm){Arm.LayerSetup[0].BranchFilters[0].BoneName=Bone;Arm.InvalidatePerBoneBlendWeights();LastArm=Bone;}
    }
};

FAnimInstanceProxy* ULDBasketballAnimInstance::CreateAnimInstanceProxy(){return new FLDBasketballAnimProxy(this);}
void ULDBasketballAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy){delete Proxy;}

void ULDBasketballAnimInstance::Bind(ULDBasketballMotionSet* InMotion,UStaticMeshComponent* InBall,const FVector& InMeshForward)
{
    Motion=InMotion;Ball=InBall;MeshForward=InMeshForward;SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
    if(Motion&&!Motion->Idle.IsEmpty()){BaseSequence=Motion->Idle[0].Animation;}
    if (Motion && !Motion->Run.IsEmpty())
    {
        const UAnimSequence* Run=Motion->Run[0].Animation;
        const int32 Frames=Run->GetNumberOfSampledKeys();
        RunBounceHeight=MAX_flt;
        for (int32 Frame=0; Frame<Frames; ++Frame)
        {
            RunBounceHeight=FMath::Min(RunBounceHeight,
                Run->EvaluateCurveData(TEXT("BallZ"),FAnimExtractContext(Run->GetTimeAtFrame(Frame),false),false));
        }
        if (Frames==0) { RunBounceHeight=0; }
    }
    if (auto* Mesh=GetSkelMeshComponent(); Mesh && !BallPoseHandle.IsValid())
    {
        BallPoseHandle=Mesh->RegisterOnBoneTransformsFinalizedDelegate(
            FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this,&ULDBasketballAnimInstance::UpdateBallFromFinalPose));
    }
}

void ULDBasketballAnimInstance::NativeUninitializeAnimation()
{
    if (auto* Mesh=GetSkelMeshComponent(); Mesh && BallPoseHandle.IsValid())
    {
        Mesh->UnregisterOnBoneTransformsFinalizedDelegate(BallPoseHandle);
    }
    BallPoseHandle.Reset();
    Super::NativeUninitializeAnimation();
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
    FullWeight=UpperWeight=ArmWeight=0;bNormalizeTurn=false;ActionSequence=nullptr;
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
        const LDActionMotion::EFamily Family=LDActionMotion::FamilyOf(Action==LDBodyMotion::EAction::Attack,StyleStep,StyleAbility);
        if(Action!=LastAction||FMath::Abs(Identity-LastActionIdentity)>LDBodyMotion::SameInstanceSeconds)
        {
            using LDActionMotion::EFamily;
            const bool bMoving=Speed>60;
            // The family's bank: passes for jabs, crosses and throws; the jump shot for the finisher and slams; the set
            // shot for two-hand casts; the spin move for stances and buffs (a gesture readable from above). A moving body
            // shoots on the move.
            const bool bJump=Family==EFamily::Finisher||Family==EFamily::HeavySlam;
            const auto& Choices=Family==EFamily::Jab||Family==EFamily::Cross||Family==EFamily::OneHandThrow?Motion->Passes
                :Family==EFamily::Ready?Motion->Turns:bMoving?Motion->MovingShots:Motion->StandingShots;
            // The hand: the jab is right, the cross left, everything else follows the dribbling hand.
            const bool Left=Family==EFamily::Jab?false:Family==EFamily::Cross?true:GetCurveValue(TEXT("BallLeft"))>.5f;
            TArray<int32,TInlineAllocator<8>> Candidates;
            for(int32 I=0;I<Choices.Num();++I)
            {
                const bool bIsJump=Choices[I].Animation&&Choices[I].Animation->GetName().Contains(TEXT("Jump_Shot"));
                const bool bShape=&Choices!=&Motion->StandingShots||bIsJump==bJump;
                if(bShape&&(Choices[I].bLeftHand==Left||&Choices==&Motion->Turns)){Candidates.Add(I);}
            }
            Active=Choices[Candidates.IsEmpty()?0:Candidates[Variation++%Candidates.Num()]];
            bWasReleased=false;LastActionIdentity=Identity;TurnAt=-1;
        }
        CurrentAge=Age;CurrentImpact=FMath::Max(Impact,.001f);CurrentDuration=LDBasketballMotion::Duration(Active,CurrentImpact);
        ActionSequence=Active.Animation;ActionTime=LDBasketballMotion::SampleTime(Active,Age,CurrentImpact);bLeft=Active.bLeftHand;
        const float BlendIn=FMath::Min(.035f,CurrentImpact*.25f),BlendOut=FMath::Min(.14f,CurrentDuration*.28f);
        ActionWeight=FMath::Min(FMath::SmoothStep(0.f,BlendIn,FMath::Max(Age,0.f)),FMath::SmoothStep(0.f,BlendOut,CurrentDuration-Age));
        const LDActionMotion::FBlend Blend=LDActionMotion::BlendOf(Family,Speed>60);
        FullWeight=ActionWeight*Blend.Full;UpperWeight=ActionWeight*Blend.Upper;ArmWeight=ActionWeight*Blend.Arm;
        bThrowing=Age>=0&&Age<CurrentDuration;
    }
    else
    {
        bWasReleased=false;
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
            bNormalizeTurn=true;
        }
        else{TurnAt=-1;}
    }
    const auto* Run=Motion->Run[0].Animation.Get();
    const float RunWeight=(BaseSequence==Run ? BaseBlend : 0.f)+(PreviousSequence==Run ? 1.f-BaseBlend : 0.f);
    RunDribbleWeight=(!Override&&!bDash&&Action==LDBodyMotion::EAction::None) ? RunWeight*(1.f-FullWeight) : 0.f;
    LastAction=Action;
    if(Ball&&!bBallVisible){Ball->SetHiddenInGame(true);}
}

void ULDBasketballAnimInstance::UpdateBallFromFinalPose()
{
    auto* Mesh=GetSkelMeshComponent();
    if(!Motion||!Ball||!Mesh||!bBallVisible||!Mesh->IsVisible()||Mesh->bHiddenInGame||!Mesh->GetOwner()||Mesh->GetOwner()->IsHidden())
    {if(Ball){Ball->SetHiddenInGame(true);}return;}
    // The invisible collision capsule above the presentation is not a visibility gate.
    for(auto* Parent=Mesh->GetAttachParent();Parent;Parent=Parent->GetAttachParent())
    {
        if(!Parent->IsVisible()||Parent->bHiddenInGame){Ball->SetHiddenInGame(true);return;}
        if(Parent->IsA<ULDHumanoidVisualComponent>()){break;}
    }
    const auto Palm=[&](bool Left)
    {return Mesh->GetSocketTransform(Left?TEXT("hand_l"):TEXT("hand_r")).TransformPosition(Left?Motion->LeftPalm:Motion->RightPalm);};
    const bool Left=GetCurveValue(TEXT("BallLeft"))>.5f;
    const FVector Hand=Palm(bThrowing?bLeft:Left);
    const FVector Local(GetCurveValue(TEXT("BallX")),GetCurveValue(TEXT("BallY")),GetCurveValue(TEXT("BallZ")));
    const FTransform& MeshToWorld=Mesh->GetComponentTransform();
    const FVector CurvePosition=Local.IsNearlyZero()?Hand:MeshToWorld.TransformPosition(Local);
    const float Contact=FMath::Clamp(GetCurveValue(TEXT("BallContact")),0.f,1.f);
    FVector Desired=FMath::Lerp(CurvePosition,Palm(Left),Contact);
    const float Radius=Motion->BallRadiusCm;
    double RunFloorWeight=0, AuthoredBounceZ=0;
    if (RunDribbleWeight>0)
    {
        AuthoredBounceZ=MeshToWorld.TransformPosition(FVector(Local.X,Local.Y,RunBounceHeight)).Z;
        const double AboveBounce=FMath::Max(0.,CurvePosition.Z-AuthoredBounceZ);
        const double NearBounce=(1.-Contact)*(1.-FMath::SmoothStep(0.,3.*Radius,AboveBounce));
        // Let the released ball travel from the posed palm toward a stable, forward bounce.
        // The receiving palm takes over again as the authored height rises.
        const FVector Forward=MeshToWorld.TransformVectorNoScale(MeshForward).GetSafeNormal2D();
        const FVector Bounce=CurvePosition+Forward*Radius;
        const FVector RunPosition=FMath::Lerp(Hand,Bounce,NearBounce);
        Desired.X=FMath::Lerp(Desired.X,RunPosition.X,RunDribbleWeight);
        Desired.Y=FMath::Lerp(Desired.Y,RunPosition.Y,RunDribbleWeight);
        RunFloorWeight=NearBounce*RunDribbleWeight;
    }
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
    {
        const double Floor=Ground.ImpactPoint.Z+Radius;
        const double Correction=FMath::Max(0.,AuthoredBounceZ-Floor)*RunFloorWeight;
        Desired.Z=FMath::Max(Desired.Z-Correction,Floor);
    }
    // The old basketball system's calibrated palm disk and finger clearance. Sockets alone are inside the skin;
    // the ball centre must stay on the palm side even while the torso and throwing arm are blended separately.
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
