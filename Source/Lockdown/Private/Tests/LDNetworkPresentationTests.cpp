#include "Character/LDCameraSettings.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
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
