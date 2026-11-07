#include "Art/LDArtCatalog.h"
#include "Art/LDArtPlacement.h"
#include "Art/LDDecorationSet.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDDemoEntry.h"
#include "Region/LDMapDefinition.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    const TCHAR* const ArtRoot = TEXT("/Game/Lockdown/Art/");

    const ULDArtCatalog* ArtLoadCatalog(FAutomationTestBase& Test)
    {
        const auto* Catalog = LoadObject<ULDArtCatalog>(nullptr, ULDArtCatalog::AssetPath);
        Test.TestNotNull(TEXT("The Demo art catalog asset exists"), Catalog);
        return Catalog;
    }

    ULDArtCatalog* ArtCopy(const ULDArtCatalog& Source)
    {
        return DuplicateObject<ULDArtCatalog>(&Source, GetTransientPackage());
    }

    // A broken catalog must fail to resolve with an error that names what is wrong.
    void ArtExpectRejected(FAutomationTestBase& Test, const ULDArtCatalog& Catalog, const FString& What, const FString& Mention)
    {
        FLDArtResolved Resolved;
        FString Error;
        const bool bResolved = Catalog.Resolve(Resolved, Error);
        Test.TestFalse(What, bResolved);
        Test.TestTrue(What + TEXT(" is reported by name: ") + Error, !bResolved && Error.Contains(Mention));
    }

    void ArtExpectMisplaced(FAutomationTestBase& Test, const ULDArtCatalog& Catalog, const FString& What, const FString& Mention)
    {
        FString Error;
        const bool bPlaced = LDArt::CheckPlacementsAgainstDemoWorld(Catalog, Error);
        Test.TestFalse(What, bPlaced);
        Test.TestTrue(What + TEXT(" is reported by name: ") + Error, !bPlaced && Error.Contains(Mention));
    }

    // The spawn nearest the outer wall in the saved Demo world, and a spot on its wall walkway 150 cm from the wall at
    // the spawn's floor height: a wall-base prop there keeps to the wall but crowds that spawn's ring.
    bool ArtSpawnByWall(FAutomationTestBase& Test, FString& Id, FVector& Spot)
    {
        TSharedPtr<const FLDDemoContentSnapshot> Content;
        TSharedPtr<const FLDDemoWorldGeometry> World;
        FString Error;
        const auto* Definition = LoadObject<ULDMapDefinition>(nullptr, *(FString(LDDemoEntry::WorldDefinition) + TEXT(".DA_DemoWorld")));
        FVector Min = FVector::ZeroVector, Max = FVector::ZeroVector;
        const bool bRead = LDDemoEntry::LoadRuntimeContent(Content, Error) && Content
            && Definition && Definition->Read(*Content, World, Error) && World && World->Surfaces.Num() == 1
            && World->Surfaces[0].MinCm.ToVector(Min, Error) && World->Surfaces[0].MaxCm.ToVector(Max, Error);
        if (!Test.TestTrue(TEXT("The saved Demo world is readable: ") + Error, bRead)) { return false; }
        double Best = TNumericLimits<double>::Max();
        for (const FLDDemoWorldAnchor& Anchor : World->Anchors)
        {
            FVector Foot;
            if (Anchor.Kind != ELDDemoWorldAnchorKind::Spawn || !Anchor.PositionCm.ToVector(Foot, Error)) { continue; }
            const double Gaps[] = {Foot.X - Min.X, Max.X - Foot.X, Foot.Y - Min.Y, Max.Y - Foot.Y};
            for (int32 Side = 0; Side < 4; ++Side)
            {
                if (Gaps[Side] >= Best) { continue; }
                Best = Gaps[Side];
                Id = Anchor.Id;
                Spot = Foot;
                const double Wall = Side == 0 ? Min.X + 150 : Side == 1 ? Max.X - 150 : Side == 2 ? Min.Y + 150 : Max.Y - 150;
                (Side < 2 ? Spot.X : Spot.Y) = Wall;
            }
        }
        return Test.TestFalse(TEXT("The saved Demo world has a spawn"), Id.IsEmpty());
    }

    int32 ArtFindDecoration(const ULDArtCatalog& Catalog, ELDArtZone Zone)
    {
        return Catalog.Decorations.IndexOfByPredicate([Zone](const FLDArtDecoration& Decoration) { return Decoration.Zone == Zone; });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDArtCatalogTest, "Lockdown.Rules.ArtCatalog", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDArtCatalogTest::RunTest(const FString& Parameters)
{
    const ULDArtCatalog* Source = ArtLoadCatalog(*this);
    if (!Source || !TestTrue(TEXT("The catalog has at least two bodies and a prop to mutate"), Source->Characters.Num() >= 2 && !Source->Props.IsEmpty()))
    {
        return false;
    }
    FLDArtResolved Resolved;
    FString Error;
    TestTrue(TEXT("A complete copy resolves: ") + Error, ArtCopy(*Source)->Resolve(Resolved, Error));
    const int32 Attack = int32(ELDArtAnim::Attack), Hit = int32(ELDArtAnim::Hit), Idle = int32(ELDArtAnim::Idle);

    ULDArtCatalog* Copy = ArtCopy(*Source); Copy->Seats.Pop();
    ArtExpectRejected(*this, *Copy, TEXT("A missing seat look is an error"), TEXT("seat looks"));
    Copy = ArtCopy(*Source); Copy->Seats.Swap(0, 1);
    ArtExpectRejected(*this, *Copy, TEXT("Seat looks out of seat order are an error"), TEXT("Seat look 0"));
    Copy = ArtCopy(*Source); Copy->Seats[3].Character = TEXT("nobody");
    ArtExpectRejected(*this, *Copy, TEXT("A seat look naming no catalog character is an error"), TEXT("nobody"));
    Copy = ArtCopy(*Source); Copy->Monsters.Pop();
    ArtExpectRejected(*this, *Copy, TEXT("A missing monster look is an error"), TEXT("monster looks"));
    Copy = ArtCopy(*Source); Copy->Monsters[0].Character = Copy->Seats[0].Character;
    ArtExpectRejected(*this, *Copy, TEXT("A camp tier wearing a contender body is an error"), TEXT("also a contender body"));
    Copy = ArtCopy(*Source); Copy->Monsters[1].Character = Copy->Monsters[0].Character;
    ArtExpectRejected(*this, *Copy, TEXT("Two camp tiers sharing one body is an error"), TEXT("same body"));
    Copy = ArtCopy(*Source); Copy->Characters[1].Id = Copy->Characters[0].Id;
    ArtExpectRejected(*this, *Copy, TEXT("A duplicate character id is an error"), TEXT("unique lowercase id"));
    Copy = ArtCopy(*Source); Copy->Characters[0].Mesh = nullptr;
    ArtExpectRejected(*this, *Copy, TEXT("A body without mesh is an error"), TEXT("no skeletal mesh"));
    Copy = ArtCopy(*Source); Copy->Characters[0].HeightCm = 95.f;
    ArtExpectRejected(*this, *Copy, TEXT("A child-sized body is an error"), TEXT("adult"));
    Copy = ArtCopy(*Source); Copy->Characters[0].TintSlots = { TEXT("NoSuchSlot") };
    ArtExpectRejected(*this, *Copy, TEXT("A tint slot outside the mesh is an error"), TEXT("NoSuchSlot"));
    Copy = ArtCopy(*Source); Copy->Characters[0].HandBone = TEXT("NoSuchBone");
    ArtExpectRejected(*this, *Copy, TEXT("A hand bone outside the skeleton is an error"), TEXT("NoSuchBone"));
    Copy = ArtCopy(*Source); Copy->Characters[0].Animations.RemoveAt(Attack);
    ArtExpectRejected(*this, *Copy, TEXT("A missing Attack clip is an error"), TEXT("expected Attack"));
    Copy = ArtCopy(*Source); Copy->Characters[0].Animations.SetNum(int32(ELDArtAnim::Death));
    ArtExpectRejected(*this, *Copy, TEXT("A missing Death clip is an error"), TEXT("clips"));
    Copy = ArtCopy(*Source); Copy->Characters[0].Animations[Attack].Sequence = nullptr;
    ArtExpectRejected(*this, *Copy, TEXT("A clip without sequence is an error"), TEXT("Attack clip has no animation"));
    Copy = ArtCopy(*Source); Copy->Characters[0].Animations[Attack].ImpactSec = 0.f;
    ArtExpectRejected(*this, *Copy, TEXT("An attack without impact time is an error"), TEXT("impact"));
    Copy = ArtCopy(*Source); Copy->Characters[0].Animations[Hit].PlayRate = .1f;
    ArtExpectRejected(*this, *Copy, TEXT("A hit flinch longer than 0.3 s is an error"), TEXT("0.3"));
    Copy = ArtCopy(*Source); Copy->Characters[0].Animations[Idle].bLoop = false;
    ArtExpectRejected(*this, *Copy, TEXT("A non-looping Idle is an error"), TEXT("must loop"));
    Copy = ArtCopy(*Source); Copy->Characters[0].Animations[Idle].SourceClip.Reset();
    ArtExpectRejected(*this, *Copy, TEXT("A clip without provenance is an error"), TEXT("source clip"));
    if (Source->Characters[0].Mesh->GetSkeleton() != Source->Characters[1].Mesh->GetSkeleton())
    {
        Copy = ArtCopy(*Source); Copy->Characters[0].Animations[Idle].Sequence = Source->Characters[1].Animations[Idle].Sequence;
        ArtExpectRejected(*this, *Copy, TEXT("A clip of another skeleton is an error"), TEXT("mesh skeleton"));
    }
    Copy = ArtCopy(*Source); Copy->Props[0].Mesh = nullptr;
    ArtExpectRejected(*this, *Copy, TEXT("A prop without mesh is an error"), TEXT("no static mesh"));
    Copy = ArtCopy(*Source); Copy->Decorations[0].Prop = TEXT("nothing");
    ArtExpectRejected(*this, *Copy, TEXT("A decoration of an unknown prop is an error"), TEXT("unknown prop"));
    Copy = ArtCopy(*Source); Copy->Decorations[0].Transform.SetScale3D(FVector::ZeroVector);
    ArtExpectRejected(*this, *Copy, TEXT("A degenerate decoration transform is an error"), TEXT("unusable transform"));
    Copy = ArtCopy(*Source); Copy->Decorations[0].Zone = ELDArtZone::Count;
    ArtExpectRejected(*this, *Copy, TEXT("A decoration without zone is an error"), TEXT("zone"));
    Copy = ArtCopy(*Source); Copy->Decorations.Reset();
    ArtExpectRejected(*this, *Copy, TEXT("A catalog without decorations is an error"), TEXT("no decoration"));
    Copy = ArtCopy(*Source);
    FLDArtProp& Unplaced = Copy->Props.AddDefaulted_GetRef();
    Unplaced.Id = TEXT("unplaced_prop");
    Unplaced.Mesh = Source->Props[0].Mesh;
    ArtExpectRejected(*this, *Copy, TEXT("A prop that is never placed is an error"), TEXT("unplaced_prop"));
    // #97: a blocker's look names its block, only that zone names one, a block is drawn once, and the crate exists.
    const int32 Drawn = ArtFindDecoration(*Source, ELDArtZone::Blocker), Other = ArtFindDecoration(*Source, ELDArtZone::Outside);
    if (TestTrue(TEXT("The catalog draws a world block and places an outside prop"), Drawn != INDEX_NONE && Other != INDEX_NONE))
    {
        Copy = ArtCopy(*Source); Copy->Decorations[Drawn].Blocker.Reset();
        ArtExpectRejected(*this, *Copy, TEXT("A blocker-zone prop naming no block is an error"), TEXT("exactly when"));
        Copy = ArtCopy(*Source); Copy->Decorations[Other].Blocker = Source->Decorations[Drawn].Blocker;
        ArtExpectRejected(*this, *Copy, TEXT("An outside prop naming a block is an error"), TEXT("exactly when"));
        Copy = ArtCopy(*Source); Copy->Decorations.Add(Source->Decorations[Drawn]);
        ArtExpectRejected(*this, *Copy, TEXT("Two props drawing one block are an error"), TEXT("same world blocker"));
        Copy = ArtCopy(*Source); Copy->HiddenBlockers.Add(Source->Decorations[Drawn].Blocker);
        ArtExpectRejected(*this, *Copy, TEXT("Hiding a block a prop already draws is an error"), TEXT("already drawn"));
    }
    Copy = ArtCopy(*Source); Copy->AirdropProp = TEXT("nothing");
    ArtExpectRejected(*this, *Copy, TEXT("An airdrop crate that is no catalog prop is an error"), TEXT("airdrop crate"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDArtCatalogAssetTest, "Lockdown.Rules.ArtCatalogAsset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDArtCatalogAssetTest::RunTest(const FString& Parameters)
{
    const ULDArtCatalog* Catalog = ArtLoadCatalog(*this);
    if (!Catalog) { return false; }
    FLDArtResolved Art;
    FString Error;
    if (!TestTrue(TEXT("The imported Demo art catalog resolves: ") + Error, Catalog->Resolve(Art, Error))) { return false; }
    const FLDArtCharacter* Contender = Art.ForSeat(0);
    const FLDArtCharacter* Monitor = Art.ForMonster(uint8(ELDDemoMonsterId::MonsterScavenger));
    const FLDArtCharacter* Teacher = Art.ForMonster(uint8(ELDDemoMonsterId::MonsterGuardian));
    const FLDArtCharacter* Dean = Art.ForMonster(uint8(ELDDemoMonsterId::MonsterDean));
    TestTrue(TEXT("Every seat, both camp tiers, the Boss and nothing else resolve"), Contender && Monitor && Teacher && Dean
        && !Art.ForSeat(LDArt::SeatCount) && !Art.ForMonster(0) && !Art.ForMonster(4));
    TestTrue(TEXT("The teacher is the tallest camp body"), Contender && Monitor && Teacher && Teacher->HeightCm > Contender->HeightCm
        && Teacher->HeightCm > Monitor->HeightCm);
    TestTrue(TEXT("D26: the Boss is its own body, taller than every other"), Dean && Teacher && Dean != Teacher
        && Dean->HeightCm > Teacher->HeightCm);
    const float HalfHeight = GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    for (const FLDArtCharacter& Body : Catalog->Characters)
    {
        TestTrue(TEXT("Body asset lives in the art folder: ") + Body.Id, Body.Mesh->GetPathName().StartsWith(ArtRoot));
        // Display alignment with the body capsule: feet on the capsule bottom, the measured height as declared.
        const FBox Bounds = Body.Mesh->GetImportedBounds().GetBox().TransformBy(Body.MeshTransform);
        TestTrue(FString::Printf(TEXT("%s feet stand on the capsule bottom (%.1f cm)"), *Body.Id, Bounds.Min.Z),
            FMath::IsNearlyEqual(Bounds.Min.Z, -HalfHeight, 4.));
        TestTrue(FString::Printf(TEXT("%s displayed height %.1f cm matches its declared %.1f cm"), *Body.Id, Bounds.Max.Z - Bounds.Min.Z, Body.HeightCm),
            FMath::IsNearlyEqual(Bounds.Max.Z - Bounds.Min.Z, double(Body.HeightCm), Body.HeightCm * .03));
        for (const FSkeletalMaterial& Slot : Body.Mesh->GetMaterials())
        {
            FLinearColor Value;
            const UMaterial* Base = Slot.MaterialInterface ? Slot.MaterialInterface->GetMaterial() : nullptr;
            TestTrue(TEXT("Material and its master live in the art folder and expose the tint and flash: ") + Body.Id + TEXT(".") + Slot.MaterialSlotName.ToString(),
                Slot.MaterialInterface->GetPathName().StartsWith(ArtRoot) && Base && Base->GetPathName().StartsWith(ArtRoot)
                && Slot.MaterialInterface->GetVectorParameterValue(FHashedMaterialParameterInfo(ULDArtCatalog::TintParameter), Value)
                && Slot.MaterialInterface->GetVectorParameterValue(FHashedMaterialParameterInfo(ULDArtCatalog::FlashParameter), Value));
        }
        for (const FLDArtAnimation& Clip : Body.Animations)
        {
            TestTrue(TEXT("Clip lives in the art folder: ") + Body.Id + TEXT(" ") + LDArt::AnimId(Clip.State), Clip.Sequence->GetPathName().StartsWith(ArtRoot));
        }
    }
    if (Contender)
    {
        TestTrue(FString::Printf(TEXT("Contenders read as adults of the 180 cm capsule (%.1f cm)"), Contender->HeightCm),
            Contender->HeightCm >= 160.f && Contender->HeightCm <= 2.f * HalfHeight);
    }
    for (const FLDArtProp& Prop : Catalog->Props)
    {
        TestTrue(TEXT("Prop lives in the art folder: ") + Prop.Id, Prop.Mesh->GetPathName().StartsWith(ArtRoot));
        for (const FStaticMaterial& Slot : Prop.Mesh->GetStaticMaterials())
        {
            float Fade = -1.f;
            TestTrue(TEXT("Prop material lives in the art folder: ") + Prop.Id, Slot.MaterialInterface && Slot.MaterialInterface->GetPathName().StartsWith(ArtRoot));
            TestEqual(TEXT("Only fadeable prop materials expose the fade parameter: ") + Prop.Id, Slot.MaterialInterface
                && Slot.MaterialInterface->GetScalarParameterValue(FHashedMaterialParameterInfo(ULDArtCatalog::FadeParameter), Fade), Prop.bFadeable);
        }
    }
    TestTrue(TEXT("The capture flagpole is the fadeable occluder"), Catalog->Props.ContainsByPredicate([](const FLDArtProp& Prop) { return Prop.Id == TEXT("flagpole") && Prop.bFadeable; }));
    TestTrue(TEXT("Every decoration respects its D02 placement zone: ") + Error, LDArt::CheckPlacementsAgainstDemoWorld(*Catalog, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDArtDecorationTest, "Lockdown.Rules.ArtDecorations", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDArtDecorationTest::RunTest(const FString& Parameters)
{
    const ULDArtCatalog* Catalog = ArtLoadCatalog(*this);
    FLDArtResolved Art;
    FString Error;
    if (!Catalog || !TestTrue(TEXT("The Demo art catalog resolves"), Catalog->Resolve(Art, Error))) { return false; }

    // Placement zones against the saved Demo world.
    const int32 WallBase = ArtFindDecoration(*Catalog, ELDArtZone::WallBase), Outside = ArtFindDecoration(*Catalog, ELDArtZone::Outside);
    const int32 Capture = ArtFindDecoration(*Catalog, ELDArtZone::Capture), Shop = ArtFindDecoration(*Catalog, ELDArtZone::Shop);
    if (TestTrue(TEXT("The catalog has wall-base, outside, capture and shop decorations"), WallBase != INDEX_NONE && Outside != INDEX_NONE
        && Capture != INDEX_NONE && Shop != INDEX_NONE))
    {
        ULDArtCatalog* Copy = ArtCopy(*Catalog);
        Copy->Decorations[WallBase].Transform.SetLocation(FVector(0, -400, 0));
        ArtExpectMisplaced(*this, *Copy, TEXT("A low prop in the middle of the floor is rejected"), TEXT("outer wall"));
        Copy = ArtCopy(*Catalog);
        Copy->Decorations[WallBase].Transform.SetScale3D(Copy->Decorations[WallBase].Transform.GetScale3D() * FVector(1, 1, 20));
        ArtExpectMisplaced(*this, *Copy, TEXT("A wall-base prop taller than the sight sample is rejected"), TEXT("sight"));
        FString SpawnId;
        FVector Spot;
        if (ArtSpawnByWall(*this, SpawnId, Spot))
        {
            Copy = ArtCopy(*Catalog);
            Copy->Decorations[WallBase].Transform.SetLocation(Spot);
            ArtExpectMisplaced(*this, *Copy, TEXT("A wall-base prop beside a spawn point is rejected"), SpawnId);
        }
        Copy = ArtCopy(*Catalog);
        Copy->Decorations[WallBase].Transform.AddToTranslation(FVector(0, 0, -300));
        ArtExpectMisplaced(*this, *Copy, TEXT("A wall-base prop sunk below its level's floor is rejected"), TEXT("flat level"));
        Copy = ArtCopy(*Catalog);
        Copy->Decorations[Outside].Transform.SetLocation(FVector(4000, 4000, 0));
        ArtExpectMisplaced(*this, *Copy, TEXT("An outside prop moved over the floor is rejected"), TEXT("arena floor"));
        Copy = ArtCopy(*Catalog);
        Copy->Decorations[Capture].Transform.AddToTranslation(FVector(250, 0, 0));
        ArtExpectMisplaced(*this, *Copy, TEXT("An off-centre capture landmark is rejected"), TEXT("centred"));
        Copy = ArtCopy(*Catalog);
        Copy->Decorations[Shop].Transform.SetScale3D(Copy->Decorations[Shop].Transform.GetScale3D() * FVector(1, 1, 2));
        ArtExpectMisplaced(*this, *Copy, TEXT("A shop landmark tall enough to hide bodies is rejected"), TEXT("hide bodies"));
    }

    // #97: collision follows the art. A blocker's prop is its block's footprint and height; a tree's prop stands on it.
    const int32 Wall = Catalog->Decorations.IndexOfByPredicate([](const FLDArtDecoration& Decoration)
        { return Decoration.Zone == ELDArtZone::Blocker && !Decoration.Blocker.StartsWith(TEXT("tree_")); });
    const int32 Tree = Catalog->Decorations.IndexOfByPredicate([](const FLDArtDecoration& Decoration)
        { return Decoration.Zone == ELDArtZone::Blocker && Decoration.Blocker.StartsWith(TEXT("tree_")); });
    const int32 Ground = ArtFindDecoration(*Catalog, ELDArtZone::Ground);
    if (TestTrue(TEXT("The catalog draws a wall or fence, a tree, and ground cover"), Wall != INDEX_NONE && Tree != INDEX_NONE && Ground != INDEX_NONE))
    {
        TestTrue(TEXT("The saved art fits every block it draws: ") + Error, LDArt::CheckPlacementsAgainstDemoWorld(*Catalog, Error));
        ULDArtCatalog* Copy = ArtCopy(*Catalog);
        Copy->Decorations[Wall].Transform.AddToTranslation(FVector(50, 0, 0));
        ArtExpectMisplaced(*this, *Copy, TEXT("A block's prop shifted off its polygon is rejected"), TEXT("not the polygon"));
        Copy = ArtCopy(*Catalog);
        Copy->Decorations[Wall].Transform.SetScale3D(Copy->Decorations[Wall].Transform.GetScale3D() * FVector(1, 1, 1.2));
        ArtExpectMisplaced(*this, *Copy, TEXT("A block's prop taller than the block is rejected"), TEXT("spans"));
        Copy = ArtCopy(*Catalog);
        Copy->Decorations[Wall].Blocker = TEXT("no_such_block");
        ArtExpectMisplaced(*this, *Copy, TEXT("A prop drawing an unknown block is rejected"), TEXT("names no world blocker"));
        Copy = ArtCopy(*Catalog);
        Copy->Decorations[Tree].Transform.AddToTranslation(FVector(800, 0, 0));
        ArtExpectMisplaced(*this, *Copy, TEXT("A tree prop moved off its trunk is rejected"), TEXT("does not stand over its tree"));
        Copy = ArtCopy(*Catalog);
        {
            // Stretch the cover upward with its base kept on its floor, so only its height is wrong.
            FTransform& Placement = Copy->Decorations[Ground].Transform;
            const FLDArtProp* Cover = Catalog->Props.FindByPredicate([&](const FLDArtProp& Prop) { return Prop.Id == Catalog->Decorations[Ground].Prop; });
            const double Before = Cover->Mesh->GetBoundingBox().TransformBy(Placement).Min.Z;
            Placement.SetScale3D(Placement.GetScale3D() * FVector(1, 1, 20));
            Placement.AddToTranslation(FVector(0, 0, Before - Cover->Mesh->GetBoundingBox().TransformBy(Placement).Min.Z));
        }
        ArtExpectMisplaced(*this, *Copy, TEXT("Ground cover taller than the sight sample is rejected"), TEXT("sight"));
        FString SpawnId;
        FVector Spot;
        if (ArtSpawnByWall(*this, SpawnId, Spot))
        {
            Copy = ArtCopy(*Catalog);
            Copy->Decorations[Ground].Transform.SetLocation(Spot);
            ArtExpectMisplaced(*this, *Copy, TEXT("Ground cover beside a spawn point is rejected"), SpawnId);
        }
        Copy = ArtCopy(*Catalog);
        Copy->HiddenBlockers.Add(Catalog->Decorations[Wall].Blocker);
        Copy->Decorations.RemoveAt(Wall);
        ArtExpectMisplaced(*this, *Copy, TEXT("Hiding a block that stands on the floor is rejected"), TEXT("reaches over the arena floor"));
    }

    // Display only in a live world: nothing collides, overlaps, blocks a trace or reaches navigation.
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated decoration fixture world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Set = World->SpawnActor<ALDDecorationSet>();
    if (!TestNotNull(TEXT("Decoration set spawns"), Set) || !TestTrue(TEXT("Decoration set builds: ") + Error, Set->Build(Art, Error))) { return false; }
    TestFalse(TEXT("A built set refuses a second build"), Set->Build(Art, Error));
    TestEqual(TEXT("Every catalog placement is one instance"), Set->GetInstanceCount(), Catalog->Decorations.Num());
    TestTrue(TEXT("Every part is display only"), Set->IsPresentationOnly());
    for (const UInstancedStaticMeshComponent* Part : Set->GetParts())
    {
        const FLDArtProp* Prop = Catalog->Props.FindByPredicate([Part](const FLDArtProp& Candidate) { return Candidate.Mesh == Part->GetStaticMesh(); });
        TestTrue(TEXT("Exactly the fadeable props carry the fade tag on their own component"), Prop && Part->ComponentHasTag(ULDArtCatalog::FadeableTag) == Prop->bFadeable);
    }
    // A blocking control body proves the traces are live while decorations stay invisible to them.
    const FVector Probe(0, 0, 50);
    auto* Control = World->SpawnActor<AStaticMeshActor>(Probe, FRotator::ZeroRotator);
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Control && Cube)
    {
        Control->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Control->GetStaticMeshComponent()->SetStaticMesh(Cube);
        Control->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Control->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
    }
    FHitResult ControlHit;
    TestTrue(TEXT("The trace fixture hits a blocking control body"), Control && Cube
        && World->LineTraceSingleByChannel(ControlHit, Probe + FVector(0, 0, 500), Probe - FVector(0, 0, 500), ECC_GameTraceChannel2) && ControlHit.GetActor() == Control);
    int32 Traces = 0, Hits = 0;
    for (const UInstancedStaticMeshComponent* Part : Set->GetParts())
    {
        for (int32 Index = 0; Index < Part->GetInstanceCount(); ++Index)
        {
            FTransform Placement;
            Part->GetInstanceTransform(Index, Placement, true);
            const FBox Box = Part->GetStaticMesh()->GetBoundingBox().TransformBy(Placement);
            for (const ECollisionChannel Channel : { ECC_GameTraceChannel2, ECC_GameTraceChannel1, ECC_Pawn, ECC_Visibility, ECC_Camera, ECC_WorldStatic })
            {
                TArray<FHitResult> Found;
                World->LineTraceMultiByChannel(Found, Box.GetCenter() + FVector(0, 0, Box.GetExtent().Z + 100), Box.GetCenter() - FVector(0, 0, Box.GetExtent().Z + 100), Channel);
                ++Traces;
                Hits += Found.FilterByPredicate([Set](const FHitResult& Hit) { return Hit.GetActor() == Set; }).Num();
            }
            TArray<FOverlapResult> Overlaps;
            World->OverlapMultiByObjectType(Overlaps, Box.GetCenter(), FQuat::Identity, FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
                FCollisionShape::MakeBox(Box.GetExtent()));
            Hits += Overlaps.FilterByPredicate([Set](const FOverlapResult& Overlap) { return Overlap.GetActor() == Set; }).Num();
        }
    }
    TestTrue(FString::Printf(TEXT("%d traces and overlaps through decorations find none of them"), Traces), Traces > 0 && Hits == 0);
    return true;
}
#endif
