#include "Character/LDModelPicking.h"
#include "Art/LDArtCatalog.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

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

    // Culled skinned picking: a 10 x 10 quad sheet bent by two bones, one of them scaled and sheared, with a
    // material per half, a top row whose weights do not sum to 1, a mixed-weight triangle and one no bone moves.
    using namespace LDModelPicking;
    TArray<FVector3f> Rest;
    TArray<uint32> InfluenceStart, Sheet;
    TArray<FInfluence> Influences;
    TArray<int32> Materials;
    for (int32 Row=0; Row<=10; ++Row)
    {
        for (int32 Column=0; Column<=10; ++Column)
        {
            Rest.Add(FVector3f(Column*10.f,0.f,Row*10.f));
            InfluenceStart.Add(Influences.Num());
            const float Bend=Row/10.f, Total=Row==10 ? .8f : 1.f;
            Influences.Add({0,(1.f-Bend)*Total});
            Influences.Add({1,Bend*Total});
        }
    }
    Rest.Add(FVector3f(500.f,0.f,500.f));
    InfluenceStart.Add(Influences.Num());
    InfluenceStart.Add(Influences.Num());
    for (int32 Row=0; Row<10; ++Row)
    {
        for (int32 Column=0; Column<10; ++Column)
        {
            const uint32 Corner=Row*11+Column;
            Sheet.Append({Corner,Corner+1,Corner+12,Corner,Corner+12,Corner+11});
            Materials.Append({Column<5 ? 0 : 1,Column<5 ? 0 : 1});
        }
    }
    Sheet.Append({0,1,121});
    Materials.Add(0);
    Sheet.Append({121,121,121});
    Materials.Add(0);
    const TSharedRef<const FSkinnedModel> Model=BuildSkinnedModel(Rest,InfluenceStart,Influences,Sheet,Materials);
    TestTrue(TEXT("The sheet splits into several leaves and drops only the fully unweighted triangle"),
        Model->Leaves.Num()>=4 && Model->DroppedTriangles==1 && Model->RequiredBones==2);
    FMatrix Twist=FScaleMatrix(FVector(2.,.5,1.))*FRotationTranslationMatrix(FRotator(30,45,10),FVector(5,-20,40));
    Twist.M[1][0]+=.7;
    FSkinnedPose Pose;
    Pose.RefToLocal={FMatrix44f(FTranslationMatrix(FVector(0,3,0)))};
    TestFalse(TEXT("A pose without every model bone has no bounds"),PoseBounds(*Model,Pose));
    Pose.RefToLocal.Add(FMatrix44f(Twist));
    TArray<FVector3f> Posed;
    for (int32 Vertex=0; Vertex<Rest.Num(); ++Vertex)
    {
        FVector3f Skinned(0,0,0);
        for (uint32 Index=InfluenceStart[Vertex]; Index<InfluenceStart[Vertex+1]; ++Index)
        {
            Skinned+=FVector3f(Pose.RefToLocal[Influences[Index].Bone].TransformPosition(Rest[Vertex]))*Influences[Index].Weight;
        }
        Posed.Add(Skinned);
    }
    bool bSkinned=PoseBounds(*Model,Pose), bInside=true;
    for (int32 Leaf=0; Leaf<Model->Leaves.Num(); ++Leaf)
    {
        const TConstArrayView<FVector3f> Skinned=SkinLeaf(*Model,Pose,Leaf);
        bSkinned&=Skinned.Num()==Model->Leaves[Leaf].NumVertices;
        for (int32 Vertex=0; bSkinned && Vertex<Skinned.Num(); ++Vertex)
        {
            bSkinned&=Skinned[Vertex]==Posed[Model->Source[Model->Leaves[Leaf].FirstVertex+Vertex]];
            bInside&=Pose.LeafBounds[Leaf].IsInsideOrOn(Skinned[Vertex]);
        }
    }
    TestTrue(TEXT("Each leaf skins exactly like the whole mesh"),bSkinned);
    TestTrue(TEXT("Posed leaf bounds contain their skinned surface under scale, shear and missing weight"),bInside);
    const FVector MixedCentre((Posed[0]+Posed[1]+Posed[121])/3.f);
    const FVector MixedStart=MixedCentre+FVector(0,0,50), MixedEnd=MixedCentre-FVector(0,0,50);
    double MixedExpected=1., MixedCulled=1.;
    const TArray<uint32> MixedTriangle={0,1,121};
    const TArray<int32> MixedMaterials={0};
    const TSharedRef<const FSkinnedModel> MixedModel=BuildSkinnedModel(Rest,InfluenceStart,Influences,MixedTriangle,MixedMaterials);
    PoseBounds(*MixedModel,Pose);
    TestTrue(TEXT("A triangle with an unweighted vertex retains its selectable surface"),
        LDModelPicking::Trace(Posed,MixedTriangle,MixedStart,MixedEnd,MixedExpected)
        && TraceSkinned(*MixedModel,Pose,MixedStart,MixedEnd,[](int32) { return true; },MixedCulled)
        && MixedExpected==MixedCulled);
    FBox3f PosedBox(Posed.GetData(),Posed.Num()-1);
    const FVector Camera(-150,-400,250);
    int32 Hits=0;
    bool bSame=true;
    for (const bool bHideHalf:{false,true})
    {
        TArray<uint32> ShownSheet;
        for (int32 Triangle=0; Triangle<Materials.Num(); ++Triangle)
        {
            if (!bHideHalf || Materials[Triangle]==0) { ShownSheet.Append(&Sheet[Triangle*3],3); }
        }
        for (int32 X=0; X<=12; ++X)
        {
            for (int32 Z=0; Z<=12; ++Z)
            {
                const FVector Target(PosedBox.Min+(PosedBox.Max-PosedBox.Min)*FVector3f(X/12.f,.5f,Z/12.f));
                const FVector Through=Camera+(Target-Camera)*2.;
                double Expected=1., Culled=1.;
                const bool bExpected=LDModelPicking::Trace(Posed,ShownSheet,Camera,Through,Expected);
                PoseBounds(*Model,Pose);
                const bool bCulled=TraceSkinned(*Model,Pose,Camera,Through,[bHideHalf](int32 Material) { return !bHideHalf || Material==0; },Culled);
                bSame&=bExpected==bCulled && (!bExpected || Expected==Culled);
                Hits+=bExpected;
            }
        }
    }
    TestTrue(TEXT("The culled trace selects exactly what tracing the whole shown mesh selects"),bSame && Hits>=20);
    PoseBounds(*Model,Pose);
    const FVector Beside(PosedBox.Max.X+1000.,PosedBox.GetCenter().Y,PosedBox.GetCenter().Z);
    TestTrue(TEXT("A ray past every leaf bound skins nothing"),
        !TraceSkinned(*Model,Pose,Beside+FVector(0,0,1000),Beside-FVector(0,0,1000),[](int32) { return true; },Far)
        && Pose.Vertices.IsEmpty());
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
            const FTransform& Transform=Visual->Mesh->GetComponentTransform();
            double Fraction=1.;
            // A fresh pose skins only the leaves a ray reaches; a ray down through the body stops at its head.
            Visual->PickingFrame=MAX_uint64;
            Visual->TracePresentedModel(Centre+FVector(0,0,1000),Centre-FVector(0,0,1000),Fraction);
            const TSharedPtr<const LDModelPicking::FSkinnedModel> Model=Visual->PickingModel;
            LDModelPicking::FSkinnedPose& Pose=Visual->PickingPose;
            if (!TestTrue(TEXT("The real posed mesh has a picking model: ")+Look.Id,Model.IsValid() && !Model->Leaves.IsEmpty())) { return false; }
            TestEqual(TEXT("Every enabled triangle of the real mesh is selectable: ")+Look.Id,Model->DroppedTriangles,0);
            TestTrue(TEXT("A ray through the body skins a small part of it: ")+Look.Id,Pose.Vertices.Num()*10<Model->Rest.Num());
            // The renderer's own CPU skinning of the whole mesh in the same pose, and its shown triangles.
            const int32 LODIndex=Visual->Mesh->GetPredictedLODLevel();
            const FSkeletalMeshLODRenderData& LOD=Visual->Mesh->GetSkeletalMeshRenderData()->LODRenderData[LODIndex];
            TArray<FMatrix44f> RefToLocal;
            Visual->Mesh->CacheRefToLocalMatrices(RefToLocal);
            TArray<FVector3f> Engine;
            USkinnedMeshComponent::ComputeSkinnedPositions(Visual->Mesh,Engine,RefToLocal,LOD,*Visual->Mesh->GetSkinWeightBuffer(LODIndex));
            TArray<uint32> Indices;
            for (const FSkelMeshRenderSection& Section:LOD.RenderSections)
            {
                if (Section.bDisabled || !Visual->Mesh->IsMaterialSectionShown(Section.MaterialIndex,LODIndex)) { continue; }
                for (uint32 Index=Section.BaseIndex; Index<Section.BaseIndex+Section.NumTriangles*3; ++Index)
                {
                    Indices.Add(LOD.MultiSizeIndexContainer.GetIndexBuffer()->Get(Index));
                }
            }
            bool bSame=true, bInside=true;
            FBox Reach(ForceInit);
            for (int32 Leaf=0; Leaf<Model->Leaves.Num(); ++Leaf)
            {
                const TConstArrayView<FVector3f> Skinned=LDModelPicking::SkinLeaf(*Model,Pose,Leaf);
                const LDModelPicking::FSkinnedLeaf& Patch=Model->Leaves[Leaf];
                for (int32 Vertex=0; Vertex<Patch.NumVertices; ++Vertex)
                {
                    const FVector3f& Expected=Engine[Model->Source[Patch.FirstVertex+Vertex]];
                    bSame&=Skinned.IsValidIndex(Vertex) && Skinned[Vertex].Equals(Expected,.01f);
                    bInside&=Pose.LeafBounds[Leaf].IsInsideOrOn(Expected);
                }
                Reach+=FBox(Pose.LeafBounds[Leaf]).TransformBy(Transform);
            }
            TestTrue(TEXT("Leaves skin the real mesh as the renderer does: ")+Look.Id,bSame);
            TestTrue(TEXT("Posed leaf bounds contain the real skinned mesh: ")+Look.Id,bInside);
            const FVector Beside(Reach.Max.X+10.,Centre.Y,Centre.Z);
            Visual->PickingFrame=MAX_uint64;
            TestTrue(TEXT("A near miss inside the broad bounds skins nothing: ")+Look.Id,
                FMath::LineBoxIntersection(Visual->Mesh->Bounds.GetBox().ExpandBy(Look.HeightCm),Beside+FVector(0,0,1000),
                    Beside-FVector(0,0,1000),FVector(0,0,-2000))
                && !Visual->TracePresentedModel(Beside+FVector(0,0,1000),Beside-FVector(0,0,1000),Fraction)
                && Visual->PickingPose.Vertices.IsEmpty());
            uint32 Top=Indices[0],Bottom=Top,Side=Top;
            for (const uint32 Index:Indices)
            {
                if (Engine[Index].Z>Engine[Top].Z) { Top=Index; }
                if (Engine[Index].Z<Engine[Bottom].Z) { Bottom=Index; }
                if (Engine[Index].Y>Engine[Side].Y) { Side=Index; }
            }
            const FVector Camera=Centre+FVector(1000,-1000,2000);
            for (const uint32 Index:{Top,Bottom,Side})
            {
                const FVector Surface=Transform.TransformPosition(FVector(Engine[Index]));
                const FVector End=Surface+(Surface-Camera).GetSafeNormal()*20.;
                double Expected=1.;
                LDModelPicking::Trace(Engine,Indices,Transform.InverseTransformPosition(Camera),Transform.InverseTransformPosition(End),Expected);
                Visual->PickingFrame=MAX_uint64;
                TestTrue(TEXT("Head, feet and outstretched extremities select the nearest posed surface: ")+Look.Id,
                    Visual->TracePresentedModel(Camera,End,Fraction) && FMath::IsNearlyEqual(Fraction,Expected,1e-6));
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
