#include "Character/LDCameraSettings.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Camera/CameraActor.h"
#include "Framework/LDPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Navigation/LDOrderRules.h"
#include "UI/LDHUDModel.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFixedCameraTest,"Lockdown.Rules.DemoControlFeel.FixedCamera",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDFixedCameraTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Camera fixture world"),World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Body=World->SpawnActor<ALDCharacter>();
    if (!TestNotNull(TEXT("Native camera body"),Body)) { return false; }
    const FVector Centre(1200,-700,90);
    Body->CameraArm->SetWorldLocation(Centre);
    for (const float Yaw : {0.f,90.f,-90.f,180.f,-45.f,45.f})
    {
        Body->SetActorLocation(FVector(Yaw*2,400,90));
        Body->SetFacing(Yaw);
        TestTrue(TEXT("A panned camera cannot orbit or translate with a turning/moving capsule"),
            Body->CameraArm->GetComponentLocation().Equals(Centre,.001));
        FMinimalViewInfo View;
        Body->CalcCamera(.016f,View);
        TestTrue(TEXT("Camera evaluates the current fixed arm, without waiting for its component tick"),
            View.Location.Equals(ALDCharacter::DefaultArmCameraPose(Centre,Body->CameraArm->TargetArmLength).GetLocation(),.001)
            && View.Rotation.Equals(FRotator(LDCamera::Pitch,LDCamera::Yaw,0),.001));
    }
    Body->CameraArm->TargetArmLength=1300;
    Body->CameraArm->SetWorldLocation(Centre+FVector(100,50,0));
    FMinimalViewInfo View;
    Body->CalcCamera(.016f,View);
    TestTrue(TEXT("Pan and zoom are visible in the same camera evaluation"),
        View.Location.Equals(ALDCharacter::DefaultArmCameraPose(Centre+FVector(100,50,0),1300).GetLocation(),.001));
    TestTrue(TEXT("Normal camera input keeps its frame duration"),FMath::IsNearlyEqual(LDCamera::PanDelta(.016f),.016f));
    TestTrue(TEXT("A 1300 ms stall cannot apply 1300 ms of stale camera input"),
        FMath::IsNearlyEqual(LDCamera::PanDelta(1.3f),.05f));
    TestTrue(TEXT("Invalid camera input duration cannot move the camera"),
        LDCamera::PanDelta(-1)==0 && LDCamera::PanDelta(std::numeric_limits<float>::infinity())==0);

    auto* PC=World->SpawnActor<ALDPlayerController>();
    if (PC) { PC->SpawnPlayerCameraManager(); }
    if (!TestNotNull(TEXT("Camera owner"),PC) || !TestNotNull(TEXT("Native camera manager"),PC->PlayerCameraManager.Get())) { return false; }
    PC->SetPawn(Body);
    PC->SetViewTarget(PC);
    PC->UpdateLocalViewTarget();
    TestTrue(TEXT("A late engine view-target reset is repaired before camera evaluation"),PC->GetViewTarget()==Body);
    PC->LastCameraPose=FTransform(View.Rotation,View.Location);
    PC->LastCameraFOV=View.FOV;
    PC->bHasCameraPose=true;
    PC->OwnerState.LifePhase=ELDLifePhase::Alive;
    PC->SetPawn(nullptr);
    PC->SetViewTarget(PC);
    PC->UpdateLocalViewTarget();
    ACameraActor* Waiting=PC->WaitingCamera;
    TestTrue(TEXT("Missing pawn with a delayed Alive receipt preserves the last rendered camera, not the origin"),
        Waiting && PC->GetViewTarget()==Waiting && Waiting->GetActorTransform().Equals(PC->LastCameraPose,.001)
        && FMath::IsNearlyEqual(Waiting->GetCameraComponent()->FieldOfView,View.FOV));
    PC->OwnerState.LifePhase=ELDLifePhase::Dead;
    PC->UpdateLocalViewTarget();
    TestTrue(TEXT("The death receipt cannot recreate or jump the waiting camera"),PC->WaitingCamera==Waiting);
    PC->SetViewTarget(PC);
    PC->UpdateLocalViewTarget();
    TestTrue(TEXT("A late reset during the pawn gap cannot replace the waiting view"),PC->GetViewTarget()==Waiting);
    auto* Respawned=World->SpawnActor<ALDCharacter>();
    PC->SetPawn(Respawned);
    PC->UpdateLocalViewTarget();
    TestTrue(TEXT("Replicated respawn rebinds the actual body even before its life receipt"),
        PC->GetViewTarget()==Respawned && !PC->WaitingCamera && !IsValid(Waiting));

    PC->SetPawn(nullptr);
    PC->UpdateLocalViewTarget();
    ACameraActor* CaptureWaiting=PC->WaitingCamera;
    if (!TestNotNull(TEXT("Waiting camera before DEV art capture"),CaptureWaiting)) { return false; }
    auto* Capture=World->SpawnActor<ACameraActor>();
    if (!TestNotNull(TEXT("DEV art capture camera"),Capture)) { return false; }
    PC->SetDevArtCaptureCamera(Capture);
    PC->SetViewTarget(PC);
    bool bCaptureRetained=true;
    for (int32 I=0;I<4;++I)
    {
        PC->UpdateLocalViewTarget();
        bCaptureRetained&=PC->GetViewTarget()==Capture && PC->WaitingCamera==CaptureWaiting;
    }
    TestTrue(TEXT("DEV art capture retains its camera across repeated pawnless evaluations"),bCaptureRetained);
    PC->SetPawn(Respawned);
    PC->SetViewTarget(PC);
    PC->UpdateLocalViewTarget();
    TestTrue(TEXT("DEV art capture retains its camera when a pawn arrives"),PC->GetViewTarget()==Capture);
    PC->SetDevArtCaptureCamera(nullptr);
    PC->UpdateLocalViewTarget();
    TestTrue(TEXT("Releasing DEV art capture restores the body camera"),
        PC->GetViewTarget()==Respawned && !PC->WaitingCamera && !IsValid(CaptureWaiting));
    PC->SetPawn(nullptr);
    PC->UpdateLocalViewTarget();
    ACameraActor* RestoredWaiting=PC->WaitingCamera;
    PC->SetDevArtCaptureCamera(Capture);
    Capture->Destroy();
    PC->UpdateLocalViewTarget();
    TestTrue(TEXT("A destroyed DEV capture camera automatically restores the waiting camera"),
        RestoredWaiting && PC->GetViewTarget()==RestoredWaiting);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDOwnerSmoothingTest,"Lockdown.Rules.DemoControlFeel.OwnerCorrectionSmoothing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDOwnerSmoothingTest::RunTest(const FString& Parameters)
{
    FLDOwnerCorrectionSmoothing Smooth;
    const FVector Before(100,20,90), Corrected(92,20,90);
    Smooth.Reconcile(Before,Corrected,256,384);
    TestTrue(TEXT("A correction keeps the displayed model and camera continuous"),(Corrected+Smooth.Offset).Equals(Before));
    const double Original=Smooth.Offset.Size();
    Smooth.Advance(.016f,.1f);
    TestTrue(TEXT("Only the display offset decays; the corrected capsule is never rewritten"),
        Smooth.Offset.Size()>0 && Smooth.Offset.Size()<Original && Corrected==FVector(92,20,90));
    const FVector Presented=Corrected+Smooth.Offset;
    Smooth.Reconcile(Presented,Corrected+FVector(3,0,0),256,384);
    TestTrue(TEXT("Successive corrections preserve the already displayed position"),
        (Corrected+FVector(3,0,0)+Smooth.Offset).Equals(Presented));
    for (int32 I=0;I<120;++I) { Smooth.Advance(1.f/60,.1f); }
    TestTrue(TEXT("A stopped model converges exactly to authority"),Smooth.Offset.IsZero());
    Smooth.Reconcile(FVector(300,0,0),FVector::ZeroVector,256,384);
    TestTrue(TEXT("Medium offsets are bounded by native smoothing distance"),FMath::IsNearlyEqual(Smooth.Offset.Size(),256.));
    Smooth.Reconcile(FVector(385,0,0),FVector::ZeroVector,256,384);
    TestTrue(TEXT("A teleport snaps rather than dragging an old life across the map"),Smooth.Offset.IsZero());
    FLDOwnerCorrectionSmoothing One,Many;
    One.Offset=Many.Offset=FVector(100,0,0);
    One.Advance(.1f,.1f);
    for (int32 I=0;I<10;++I) { Many.Advance(.01f,.1f); }
    TestTrue(TEXT("Smoothing is frame-rate independent"),One.Offset.Equals(Many.Offset,.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDOwnerWalkLeadTest,"Lockdown.Rules.DemoControlFeel.OwnerWalkLead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDOwnerWalkLeadTest::RunTest(const FString& Parameters)
{
    using namespace LDOwnerPrediction;
    const double Speed=350;
    const double Bound=Speed*WalkLeadSeconds+WalkSlackCm;
    TestTrue(TEXT("Two bodies at the same point agree"),AcceptsWalkError(FVector::ZeroVector,Speed));
    TestTrue(TEXT("One 60 Hz frame of walking lead is no correction"),AcceptsWalkError(FVector(Speed/60,0,0),Speed));
    TestTrue(TEXT("A lead exactly at the bound is still accepted, in any direction"),
        AcceptsWalkError(FVector(0,-Bound,0),Speed) && AcceptsWalkError(FVector(Bound,0,0)*.999,Speed));
    TestFalse(TEXT("A lead past the bound is corrected"),AcceptsWalkError(FVector(Bound+.01,0,0),Speed));
    TestFalse(TEXT("A whole round trip of lead at 200 ms is corrected"),AcceptsWalkError(FVector(Speed*.2,0,0),Speed));
    TestTrue(TEXT("A rooted body (no speed) keeps only the fixed slack"),
        AcceptsWalkError(FVector(WalkSlackCm,0,0),0) && !AcceptsWalkError(FVector(WalkSlackCm+.01,0,0),0));
    TestFalse(TEXT("Unknown errors and speeds are never accepted"),
        AcceptsWalkError(FVector(std::numeric_limits<double>::quiet_NaN(),0,0),Speed)
        || AcceptsWalkError(FVector::ZeroVector,std::numeric_limits<double>::infinity())
        || AcceptsWalkError(FVector::ZeroVector,-1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPredictedMoveIdentityTest,"Lockdown.Rules.DemoControlFeel.PredictedMoveIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPredictedMoveIdentityTest::RunTest(const FString& Parameters)
{
    using LDOrderRules::PredictedMoveIdentity;
    const FLDMoveIdentity None;
    const FLDMoveIdentity Old{3,2,4,1}, Early{3,2,5,1}, Queued{3,2,6,1}, Pending{3,2,7,1};
    TestTrue(TEXT("A Shift step sent while an early ground walk runs keeps tagging moves with the walked order"),
        PredictedMoveIdentity(Queued,None,Early,false,None)==Early && PredictedMoveIdentity(Queued,Old,Early,false,None)==Early);
    TestTrue(TEXT("The walked early order wins over the older accepted order it replaces"),
        PredictedMoveIdentity(Early,Old,Early,false,None)==Early);
    TestTrue(TEXT("Without an early walk the accepted order is predicted under a pending replacement"),
        PredictedMoveIdentity(Pending,Old,None,false,None)==Old);
    const FLDMoveIdentity Stopped{3,2,8,2};
    TestTrue(TEXT("A Stop and its newer epoch predict neither the accepted nor an early order"),
        PredictedMoveIdentity(Stopped,Old,None,true,None)==Stopped && PredictedMoveIdentity(Stopped,Old,Early,false,None)==Stopped);
    const FLDMoveIdentity Refused=PredictedMoveIdentity(Early,None,None,false,Early);
    TestTrue(TEXT("An early walk refused before it ran no longer matches its own saved moves"),Refused!=Early && Refused.Order==0);
    TestTrue(TEXT("The refused walk keeps the match, life and epoch the server checks"),
        Refused.Match==Early.Match && Refused.Life==Early.Life && Refused.Epoch==Early.Epoch);
    TestTrue(TEXT("A newer order after the refusal is predicted as itself"),PredictedMoveIdentity(Queued,None,None,false,Early)==Queued);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFrameDiagnosticsTest,"Lockdown.Rules.HUDFrameDiagnostics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDFrameDiagnosticsTest::RunTest(const FString& Parameters)
{
    LDHUD::FFrameDiagnostics Frames;
    TestFalse(TEXT("Unknown FPS is not fabricated from invalid data"),Frames.AddFrame(0)
        || Frames.AddFrame(-1) || Frames.AddFrame(std::numeric_limits<double>::quiet_NaN()));
    bool Published=false;
    for (int32 I=0;I<60;++I) { Published|=Frames.AddFrame(1./60); }
    TestTrue(TEXT("Real frame durations produce 60 FPS and 16.7 ms"),Published
        && FMath::IsNearlyEqual(Frames.FPS,60.,.01) && FMath::IsNearlyEqual(Frames.FrameMs,1000./60,.01));
    Frames={};
    for (int32 I=0;I<20;++I) { Frames.AddFrame(.01); }
    TestTrue(TEXT("A long frame is included rather than silently clamped"),Frames.AddFrame(.3)
        && FMath::IsNearlyEqual(Frames.FPS,42.) && FMath::IsNearlyEqual(Frames.PeakMs,300.));
    return true;
}
#endif
