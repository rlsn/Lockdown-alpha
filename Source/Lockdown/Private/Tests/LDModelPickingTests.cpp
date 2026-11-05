#include "Character/LDModelPicking.h"
#include "Art/LDArtCatalog.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDModelPickingTest, "Lockdown.Rules.DemoControlFeel.ModelPicking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDModelPickingTest::RunTest(const FString& Parameters)
{
    const TArray<FVector3f> Vertices = {
        {0,-10,0}, {0,10,0}, {0,0,20},
        {10,-10,0}, {10,10,0}, {10,0,20}
    };
    const TArray<uint32> Triangles = {0,1,2,3,4,5};
    double Fraction = 0.;
    TestTrue(TEXT("A ray through the visible surface selects the front triangle"),
        LDModelPicking::Trace(Vertices,Triangles,FVector(-100,0,10),FVector(100,0,10),Fraction)
        && FMath::IsNearlyEqual(Fraction,.5));
    TestTrue(TEXT("Triangle edges are selectable, including head and feet"),
        LDModelPicking::Trace(Vertices,Triangles,FVector(-100,0,20),FVector(100,0,20),Fraction)
        && LDModelPicking::Trace(Vertices,Triangles,FVector(-100,10,0),FVector(100,10,0),Fraction));
    TestFalse(TEXT("Empty space inside the model bounds is not the model"),
        LDModelPicking::Trace(Vertices,Triangles,FVector(-100,9,19),FVector(100,9,19),Fraction));
    TestTrue(TEXT("Back-facing surfaces remain selectable"),
        LDModelPicking::Trace(Vertices,Triangles,FVector(100,0,10),FVector(-100,0,10),Fraction)
        && FMath::IsNearlyEqual(Fraction,.45));
    TestFalse(TEXT("A surface behind the camera or beyond the ray is not selected"),
        LDModelPicking::Trace(Vertices,Triangles,FVector(-100,0,10),FVector(-10,0,10),Fraction));
    TestFalse(TEXT("A degenerate ray selects nothing"),
        LDModelPicking::Trace(Vertices,Triangles,FVector::ZeroVector,FVector::ZeroVector,Fraction));
    TestFalse(TEXT("No geometry selects nothing"),
        LDModelPicking::Trace(TArray<FVector3f>(),TArray<uint32>(),FVector(-100,0,10),FVector(100,0,10),Fraction));
    const TArray<uint32> Invalid={0,1,99,0,0,0};
    TestFalse(TEXT("Invalid and collapsed triangles select nothing"),
        LDModelPicking::Trace(Vertices,Invalid,FVector(-100,0,10),FVector(100,0,10),Fraction));
    const FTransform Display(FRotator(0,45,0),FVector(400,-200,90),FVector(1.7));
    const FVector Start = Display.TransformPosition(FVector(-100,0,10));
    const FVector End = Display.TransformPosition(FVector(100,0,10));
    TestTrue(TEXT("The displayed transform, not the latest authority location, controls selection"),
        LDModelPicking::Trace(Vertices,Triangles,Display.InverseTransformPosition(Start),Display.InverseTransformPosition(End),Fraction)
        && FMath::IsNearlyEqual(Fraction,.5));

    // Lenient volume: an upright body 0..180 cm tall with a 50 cm footprint, seen from 2000 cm away along +X.
    const FVector Foot(0,0,0), Top(0,0,180), Eye(-2000,0,90);
    const double Px=FMath::DegreesToRadians(.05);
    double Angle=1., Along=0.;
    TestTrue(TEXT("A ray through the pick volume is inside it"),
        LDModelPicking::NearVolume(Eye,FVector(1,0,0),1e6,Foot,Top,50.,0.,Angle,Along) && Angle==0. && FMath::IsNearlyEqual(Along,2000.,1.));
    TestTrue(TEXT("A ray just past the footprint radius is within the screen tolerance"),
        LDModelPicking::NearVolume(Eye,FVector(2000,51,0),1e6,Foot,Top,50.,Px,Angle,Along) && Angle>0. && Angle<=Px);
    TestFalse(TEXT("A ray beyond the screen tolerance is not selected"),
        LDModelPicking::NearVolume(Eye,FVector(2000,60,0),1e6,Foot,Top,50.,Px,Angle,Along));
    TestFalse(TEXT("A ray above the head plus tolerance is not selected"),
        LDModelPicking::NearVolume(Eye,FVector(2000,0,200),1e6,Foot,Top,50.,Px,Angle,Along));
    TestFalse(TEXT("A sight blocker before the volume stops the lenient pick"),
        LDModelPicking::NearVolume(Eye,FVector(1,0,0),1500.,Foot,Top,50.,Px,Angle,Along));
    TestFalse(TEXT("A volume just behind a sight blocker cannot pass the angular tolerance"),
        LDModelPicking::NearVolume(Eye,FVector(1,0,0),1940.,Foot,Top,50.,
            FMath::DegreesToRadians(.5),Angle,Along));
    TestFalse(TEXT("A volume behind the ray start is not selected"),
        LDModelPicking::NearVolume(FVector(2000,0,90),FVector(1,0,0),1e6,Foot,Top,50.,Px,Angle,Along));
    TestFalse(TEXT("A degenerate ray or tolerance selects nothing"),
        LDModelPicking::NearVolume(Eye,FVector::ZeroVector,1e6,Foot,Top,50.,Px,Angle,Along)
        || LDModelPicking::NearVolume(Eye,FVector(1,0,0),1e6,Foot,Top,50.,-1.,Angle,Along)
        || LDModelPicking::NearVolume(Eye,FVector(1,0,0),1e6,Foot,Top,-1.,Px,Angle,Along));
    double Near=0., Far=0.;
    LDModelPicking::NearVolume(Eye,FVector(2000,55,0),1e6,Foot,Top,50.,Px,Near,Along);
    LDModelPicking::NearVolume(Eye,FVector(2000,58,0),1e6,Foot,Top,50.,Px,Far,Along);
    TestTrue(TEXT("The volume closer to the pointer scores the smaller angle"),Near<Far);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPresentedModelPickingTest, "Lockdown.Rules.DemoControlFeel.PresentedModelPicking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDPresentedModelPickingTest::RunTest(const FString& Parameters)
{
    const auto* Catalog=LoadObject<ULDArtCatalog>(nullptr,ULDArtCatalog::AssetPath);
    FLDArtResolved Resolved;
    FString Error;
    if (!TestNotNull(TEXT("Real imported Demo art"),Catalog)
        || !TestTrue(TEXT("All meshes retain CPU picking data: ")+Error,Catalog->Resolve(Resolved,Error))) { return false; }
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Presented model fixture world"),World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    for (const FLDArtCharacter& Look:Catalog->Characters)
    {
        AActor* Actor=World->SpawnActor<AActor>();
        auto* Visual=NewObject<ULDHumanoidVisualComponent>(Actor);
        Actor->SetRootComponent(Visual);
        Visual->RegisterComponent();
        Visual->ApplyLook(&Look,FLinearColor::Blue,Look.Id);
        Actor->SetActorTransform(FTransform(FRotator(0,37,0),FVector(450,-230,90),FVector(1.2)));
        for (const uint8 Cue:{uint8(0),uint8(1),uint8(2)})
        {
            Visual->SetCue(Cue,.12f);
            Visual->Animate(FVector::ZeroVector,.016f);
            Visual->Mesh->RefreshBoneTransforms();
            const FVector Centre=Visual->Mesh->Bounds.Origin;
            double Fraction=1.;
            // Prime the real CPU skinning cache, then sample the extremities of the actual evaluated mesh.
            Visual->TracePresentedModel(Centre+FVector(0,0,1000),Centre-FVector(0,0,1000),Fraction);
            if (!TestTrue(TEXT("The real posed mesh has indexed triangles: ")+Look.Id,
                !Visual->PickingVertices.IsEmpty() && !Visual->PickingIndices.IsEmpty())) { return false; }
            uint32 Top=Visual->PickingIndices[0],Bottom=Top,Side=Top;
            for (const uint32 Index:Visual->PickingIndices)
            {
                if (Visual->PickingVertices[Index].Z>Visual->PickingVertices[Top].Z) { Top=Index; }
                if (Visual->PickingVertices[Index].Z<Visual->PickingVertices[Bottom].Z) { Bottom=Index; }
                if (Visual->PickingVertices[Index].Y>Visual->PickingVertices[Side].Y) { Side=Index; }
            }
            const FVector Camera=Centre+FVector(1000,-1000,2000);
            for (const uint32 Index:{Top,Bottom,Side})
            {
                const FVector Surface=Visual->Mesh->GetComponentTransform().TransformPosition(FVector(Visual->PickingVertices[Index]));
                TestTrue(TEXT("Head, feet and outstretched extremities select the posed surface: ")+Look.Id,
                    Visual->TracePresentedModel(Camera,Surface+(Surface-Camera).GetSafeNormal()*20.,Fraction));
            }
            FVector VolumeFoot, VolumeTop;
            double Radius=0.;
            TestTrue(TEXT("The shown model has a pick volume as tall as its displayed height: ")+Look.Id,
                Visual->PresentedPickVolume(VolumeFoot,VolumeTop,Radius) && Radius>0.
                && FMath::IsNearlyEqual(VolumeTop.Z-VolumeFoot.Z,double(Look.HeightCm)*1.2,1.)
                && FVector::Dist2D(VolumeFoot,Visual->GetComponentLocation())<=1.);
            Visual->SetVisibility(false,true);
            TestFalse(TEXT("An invisible model has no pick volume"),Visual->PresentedPickVolume(VolumeFoot,VolumeTop,Radius));
            TestFalse(TEXT("An invisible model cannot be selected"),Visual->TracePresentedModel(Centre+FVector(0,0,1000),Centre-FVector(0,0,1000),Fraction));
            Visual->SetVisibility(true,true);
            Actor->SetActorHiddenInGame(true);
            TestFalse(TEXT("An actor hidden by its lifecycle cannot block another model"),
                Visual->TracePresentedModel(Centre+FVector(0,0,1000),Centre-FVector(0,0,1000),Fraction));
            Actor->SetActorHiddenInGame(false);
        }
        Visual->PlayFall();
        double Fraction=1.;
        const FVector Centre=Visual->Mesh->Bounds.Origin;
        TestFalse(TEXT("A fallen model cannot be selected"),Visual->TracePresentedModel(Centre+FVector(0,0,1000),Centre-FVector(0,0,1000),Fraction));
        FVector Foot, Top;
        double Radius=0.;
        TestFalse(TEXT("A fallen model has no pick volume"),Visual->PresentedPickVolume(Foot,Top,Radius));
        Actor->Destroy();
    }
    return true;
}
#endif
