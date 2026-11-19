#include "Art/LDStaticScene.h"
#include "World/LDLayoutPoint.h"
#include "Region/LDMapGeometry.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Subsystems/EditorActorSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCampAuthoringTest,"Lockdown.Rules.StaticScene.AuthoredCampInstances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCampAuthoringTest::RunTest(const FString& Parameters)
{
    auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);
    ON_SCOPE_EXIT { UEditorLoadingAndSavingUtils::NewBlankMap(false); };
    auto* Floor=World->SpawnActor<AStaticMeshActor>();
    Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    Floor->SetActorScale3D(FVector(100,100,1));
    Floor->SetActorLocation(FVector(0,0,-50));
    FLDDemoWorldGeometry Base, Out;
    Base.Surfaces.AddDefaulted_GetRef().Id=TEXT("demo_ground");
    auto& Template=Base.Anchors.AddDefaulted_GetRef();
    Template.Id=TEXT("l22"); Template.Kind=ELDDemoWorldAnchorKind::Camp; Template.SurfaceId=TEXT("demo_ground");
    Template.Members.AddDefaulted_GetRef().Id=TEXT("l22_0");
    auto* Camp=World->SpawnActor<ALDLayoutPoint>();
    Camp->Tags={TEXT("LDSceneAnchor:l23"),TEXT("LDScenePrototypeAnchor:l22")};
    auto* Member=World->SpawnActor<ALDLayoutPoint>();
    Member->Tags={TEXT("LDSceneMember:l23_0"),TEXT("LDSceneMemberSlot:0")};
    Member->AttachToActor(Camp,FAttachmentTransformRules::KeepWorldTransform);
    FString Error;
    TestTrue(TEXT("A deleted original and independent saved replacement project"),LDStaticScene::ProjectRuntimeGeometry(*World,Base,Out,Error));
    TestEqual(TEXT("No deleted original is resurrected"),Out.Anchors.Num(),1);
    if (Out.Anchors.IsEmpty()) { return false; }
    TestEqual(TEXT("Saved independent id is retained"),Out.Anchors[0].Id,FString(TEXT("l23")));
    TestEqual(TEXT("Approved prototype is explicit"),Out.Anchors[0].PrototypeId,FString(TEXT("l22")));
    auto* ArchivedCamp=World->SpawnActor<ALDLayoutPoint>();
    ArchivedCamp->Tags=Camp->Tags;
    ArchivedCamp->bIsEditorOnlyActor=true;
    auto* ArchivedRamp=World->SpawnActor<AStaticMeshActor>();
    ArchivedRamp->Tags={TEXT("archived_ramp"),LDDemoWorld::StaticGeometryTag};
    ArchivedRamp->GetStaticMeshComponent()->SetStaticMesh(Floor->GetStaticMeshComponent()->GetStaticMesh());
    ArchivedRamp->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ArchivedRamp->bIsEditorOnlyActor=true;
    Base.Ramps.AddDefaulted_GetRef().Id=TEXT("archived_ramp");
    TestTrue(TEXT("Editor reference camps cannot duplicate live semantic identities"),LDStaticScene::ProjectRuntimeGeometry(*World,Base,Out,Error));
    TestEqual(TEXT("Editor reference terrain cannot retain source-only runtime ramps"),Out.Ramps.Num(),0);
    Member->AttachToActor(ArchivedCamp,FAttachmentTransformRules::KeepWorldTransform);
    TestFalse(TEXT("A live home under a stripped reference parent is diagnosed before cook"),LDStaticScene::ProjectRuntimeGeometry(*World,Base,Out,Error));
    TestTrue(TEXT("The broken attachment is identified"),Error.Contains(TEXT("editor-only attachment ancestor")));
    Member->AttachToActor(Camp,FAttachmentTransformRules::KeepWorldTransform);
    auto* Editor=GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
    auto* Copy=Cast<ALDLayoutPoint>(Editor->DuplicateActor(Camp,World,FVector(500,0,0)));
    if (!TestNotNull(TEXT("An editor duplicate is available"),Copy)) { return false; }
    TestTrue(TEXT("Duplication assigns a distinct saved camp id"),LDStaticScene::Value(*Copy,TEXT("LDSceneAnchor:"))!=TEXT("l23"));
    auto* CopyMember=World->SpawnActor<ALDLayoutPoint>();
    CopyMember->Tags=Member->Tags;
    CopyMember->AttachToActor(Copy,FAttachmentTransformRules::KeepWorldTransform);
    TestTrue(TEXT("Child slots follow the duplicated semantic parent"),LDStaticScene::ProjectRuntimeGeometry(*World,Base,Out,Error));
    TestEqual(TEXT("Both instances are real geometry anchors"),Out.Anchors.Num(),2);
    CopyMember->Destroy();
    TestFalse(TEXT("An incomplete copied camp is diagnosed, not silently omitted"),LDStaticScene::ProjectRuntimeGeometry(*World,Base,Out,Error));
    Copy->Destroy();
    Camp->bUseAuthoredCampMembers=true;
    auto& Other=Base.Anchors.AddDefaulted_GetRef();
    Other.Id=TEXT("h1");Other.Kind=ELDDemoWorldAnchorKind::Camp;Other.SurfaceId=TEXT("demo_ground");
    Other.Members.AddDefaulted_GetRef().Id=TEXT("h1_0");
    auto* Extra=World->SpawnActor<ALDLayoutPoint>();
    Extra->Tags={TEXT("LDSceneMember:l23_0"),TEXT("LDSceneMemberSlot:1")};
    Extra->CampMemberPrototypeId=TEXT("h1");
    Extra->SetActorLocation(FVector(123,456,0));
    Extra->AttachToActor(Camp,FAttachmentTransformRules::KeepWorldTransform);
    TestTrue(TEXT("An authored group retains an extra mixed-type slot despite a copied old member id"),LDStaticScene::ProjectRuntimeGeometry(*World,Base,Out,Error));
    if (TestEqual(TEXT("One shared centre"),Out.Anchors.Num(),1) && TestEqual(TEXT("Actual authored member count"),Out.Anchors[0].Members.Num(),2))
    {
        FVector Saved;
        TestEqual(TEXT("Member type is independently approved"),Out.Anchors[0].Members[1].PrototypeId,FString(TEXT("h1")));
        TestTrue(TEXT("Exact home survives grouping"),Out.Anchors[0].Members[1].PositionCm.ToVector(Saved,Error) && Saved==Extra->GetActorLocation());
    }
    Extra->Tags={TEXT("LDSceneMember:l23_1"),TEXT("LDSceneMemberSlot:0")};
    TestFalse(TEXT("Repeated authored slots are refused"),LDStaticScene::ProjectRuntimeGeometry(*World,Base,Out,Error));
    return !HasAnyErrors();
}
