#include "LDSceneAuthoringLibrary.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "World/LDSpawnPlacement.h"
#include "World/LDCampLifecycle.h"
#include "World/LDCampMember.h"
#include "World/LDLayoutPoint.h"
#include "GameFramework/PlayerStart.h"
#include "Art/LDStaticScene.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/CapsuleComponent.h"
#include "Tests/LDNativeMotionProbe.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCampPhysicalPlacement,"Lockdown.Rules.StaticScene.CampPhysicalPlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCampPhysicalPlacement::RunTest(const FString& Parameters)
{
    auto* World=UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(LDDemoEntry::WorldMap,FPackageName::GetMapPackageExtension()));
    if (!TestNotNull(TEXT("Authored map"),World)) { return false; }
    FString Error;
    auto* Registry=World->GetSubsystem<ULDMapRegistry>();
    if (!TestTrue(TEXT("Scene registry: ")+Error,Registry && Registry->Rebuild(Error))) { return false; }
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FCollisionShape Capsule;
    FLDCampRoster Roster;
    if (!LDDemoEntry::LoadRuntimeContent(Content,Error) || !ULDMapRegistry::ContentCapsule(*Content,Capsule,Error)
        || !LDCamp::BuildRoster(Content->Get(),*Registry->GetGeometry(),Roster,Error)) { AddError(Error); return false; }
    if (!TestTrue(TEXT("Every authored field spawn has legal local candidates and a complete capture path: ")+Error,
        Registry->ValidateFieldSpawnPaths(false,Error))) { return false; }
    {
        AActor* SpawnPoint=nullptr;
        for (TActorIterator<APlayerStart> It(World);It;++It)
        { if (It->PlayerStartTag==FName(*Content->Get().World.SpawnIds[0])) { SpawnPoint=*It;break; } }
        if (!TestNotNull(TEXT("Semantic field spawn for isolated unreachable-floor regression"),SpawnPoint)) { return false; }
        const FTransform Saved=SpawnPoint->GetActorTransform();
        auto* Platform=World->SpawnActor<AStaticMeshActor>();
        ON_SCOPE_EXIT
        {
            SpawnPoint->SetActorTransform(Saved);Platform->Destroy();
            FString RestoreError;
            if (!Registry->Rebuild(RestoreError)) { AddError(TEXT("Restore field spawn floor regression: ")+RestoreError); }
        };
        auto* Part=Platform->GetStaticMeshComponent();
        Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Part->SetCollisionProfileName(TEXT("BlockAll"));
        Platform->SetActorScale3D(FVector(4,4,.5));
        const FVector High=Saved.GetLocation()+FVector(0,0,5000);
        Platform->SetActorLocation(High-FVector(0,0,25));SpawnPoint->SetActorLocation(High);
        if (!TestTrue(TEXT("The disconnected upper platform has genuine native support: ")+Error,Registry->Rebuild(Error))) { return false; }
        TestFalse(TEXT("Field admission refuses a standing spawn on a disconnected upper floor"),Registry->ValidateFieldSpawnPaths(false,Error));
        TestTrue(TEXT("The rejection identifies navigation rather than removing the spawn check"),Error.Contains(TEXT("导航不可用")));
    }
    auto* Prepared=World->SpawnActorDeferred<ALDCampMemberCharacter>(ALDCampMemberCharacter::StaticClass(),FTransform::Identity);
    if (!TestTrue(TEXT("Production monster prepares its approved capsule before FinishSpawning"),
        Prepared && Prepared->PrepareMonster(*Content,Content->Get().Monsters[0],Error))) { return false; }
    TestEqual(TEXT("Deferred production capsule already has the navigation radius"),
        Prepared->GetCapsuleComponent()->GetScaledCapsuleRadius(),Capsule.GetCapsuleRadius());
    Prepared->Destroy();
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true).CreateFXSystem(false);
    auto* Physics=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(Physics);
    ON_SCOPE_EXIT { Physics->EndPlay(EEndPlayReason::Destroyed); Physics->DestroyWorld(false); GEngine->DestroyWorldContext(Physics); };
    Physics->InitializeActorsForPlay(FURL(),false); Physics->BeginPlay();
    TMap<const UPrimitiveComponent*,UPrimitiveComponent*> Supports;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        TInlineComponentArray<UStaticMeshComponent*> Parts(*It);
        for (auto* Source : Parts)
        {
            if (!Source->GetStaticMesh() || !Source->IsQueryCollisionEnabled() || Source->GetCollisionObjectType()!=ECC_WorldStatic
                || Source->GetCollisionResponseToChannel(ECC_Pawn)!=ECR_Block) { continue; }
            auto* Actor=Physics->SpawnActor<AActor>();
            auto* Copy=NewObject<UStaticMeshComponent>(Actor);
            Actor->SetRootComponent(Copy); Actor->AddInstanceComponent(Copy);
            Copy->SetStaticMesh(Source->GetStaticMesh()); Copy->SetMobility(EComponentMobility::Static);
            Copy->SetCollisionProfileName(TEXT("BlockAll")); Copy->SetWorldTransform(Source->GetComponentTransform());
            Copy->RegisterComponentWithWorld(Physics); Supports.Add(Source,Copy);
        }
    }
    TOptional<FVector> LocalHome;
    FString LocalHomeId;
    for (const auto& Camp : Roster.Camps)
    {
        FLDCampDecisionInput Input;
        Input.Match={1,FGuid(1,2,3,4)}; Input.World=Registry->Revision();
        Input.Self={{ELDVictimKind::CampMember,FGuid(1,1,1,1),1},FGuid(2,2,2,2)};
        Input.NowSeconds=1; Input.FootCm=Camp.AnchorCm; Input.CampAnchorFootCm=Camp.AnchorCm;
        Input.Grounded=true; Input.SurfaceId=Camp.SurfaceId; Input.MemberHome={Input.World,Camp.Members[0].HomeCm,Camp.SurfaceId};
        Input.MonsterId=Camp.Members[0].MonsterId;
        Input.bObservationComplete=true;
        FLDCampDecision Decision;
        TestTrue(TEXT("Every saved instance has an active approved AI definition: ")+Camp.CampId.ToString(),
            LDCampDecision::Evaluate(*Content,Camp.DefinitionId,Input,{},{},Decision,Error));
        for (const auto& Member : Camp.Members)
        {
            FLDLegalGround Ground;
            ELDWorldQueryFailure Failure;
            if (!Registry->QueryGround(Registry->Revision(),Member.HomeCm,Capsule,Camp.SurfaceId,Ground,Failure,Error))
            { AddWarning(Camp.CampId.ToString()+TEXT(" exact home not clear: ")+Error); continue; }
            const FVector Origin=LDSpawnPlacement::StandingOrigin(Ground);
            if (!LocalHome.IsSet()) { LocalHome=Member.HomeCm;LocalHomeId=Member.MemberId.ToString(); }
            Ground.Floor=Supports.FindRef(Ground.Floor.Get());
            if (!TestNotNull(TEXT("Native support was cloned"),Ground.Floor.Get())) { return false; }
            FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Body=Physics->SpawnActor<ALDNativeMotionProbeCharacter>(Origin,FRotator::ZeroRotator,Spawn);
            ON_SCOPE_EXIT { if (Body) { Body->Destroy(); } };
            auto* Move=Body->GetCharacterMovement();
            Move->SetMovementMode(MOVE_Walking);
            Move->FindFloor(Body->GetActorLocation(),Move->CurrentFloor,false,nullptr);
            Move->SetBaseFromFloor(Move->CurrentFloor);
            const FVector Delta=Body->GetActorLocation()-Origin;
            TestTrue(FString::Printf(TEXT("%s delta=(%.9f,%.9f,%.9f) gap=%.9f floor=%s expected=%s"),*Member.MemberId.ToString(),
                Delta.X,Delta.Y,Delta.Z,Move->CurrentFloor.GetDistanceToFloor(),
                *GetPathNameSafe(Move->CurrentFloor.HitResult.GetComponent()),*GetPathNameSafe(Ground.Floor.Get())),
                Move->CurrentFloor.IsWalkableFloor() && Move->CurrentFloor.HitResult.GetComponent()==Ground.Floor.Get()
                && Body->GetActorLocation().Equals(Origin,.000001));
        }
    }
    ALDLayoutPoint* HomeActor=nullptr;
    for (TActorIterator<ALDLayoutPoint> It(World); It; ++It)
    { if (LDStaticScene::Value(**It,TEXT("LDSceneMember:"))==LocalHomeId) { HomeActor=*It;break; } }
    if (!TestTrue(TEXT("A saved camp home is available for the overhead regression"),HomeActor && LocalHome.IsSet())) { return false; }
    const FTransform Saved=HomeActor->GetActorTransform();
    ON_SCOPE_EXIT { HomeActor->SetActorTransform(Saved); };
    HomeActor->SetActorLocation(LocalHome.GetValue()+FVector(0,0,100));
    auto* Roof=World->SpawnActor<AStaticMeshActor>();
    ON_SCOPE_EXIT { Roof->Destroy(); };
    Roof->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Roof->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    Roof->SetActorScale3D(FVector(10,10,.2));Roof->SetActorLocation(LocalHome.GetValue()+FVector(0,0,500));
    if (!TestTrue(TEXT("Camp home below a new overhead roof still binds: ")+Error,Registry->Rebuild(Error))) { return false; }
    const FLDDemoWorldMemberHome* Projected=nullptr;
    for (const auto& Anchor : Registry->GetGeometry()->Anchors)
    { for (const auto& Member : Anchor.Members) { if (Member.Id==LocalHomeId) { Projected=&Member; } } }
    FVector Grounded;
    TestTrue(TEXT("A lowered local floor wins over an overhead roof without changing the saved marker"),
        Projected && Projected->PositionCm.ToVector(Grounded,Error) && FMath::Abs(Grounded.Z-LocalHome->Z)<.01);
    return !HasAnyErrors();
}
