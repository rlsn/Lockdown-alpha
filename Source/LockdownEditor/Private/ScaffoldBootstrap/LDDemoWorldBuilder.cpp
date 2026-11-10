#include "LDDemoWorldBuilder.h"
#include "LDEditorLog.h"

#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDRegionNavBounds.h"
#include "Navigation/LDDemoNavigationSystem.h"
#include "AI/NavigationSystemConfig.h"
#include "ActorEditorUtils.h"
#include "AssetCompilingManager.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/Level.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Model.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "PackageTools.h"
#include "UObject/StrongObjectPtr.h"
#endif

namespace
{
const TCHAR* GeometryStamp = TEXT("LDDemoWorldSHA256");
const TCHAR* GeometryRoot = TEXT("/Game/Lockdown/Generated/World/");

bool Fail(FString& Error, const FString& Detail)
{
    Error = Detail;
    return false;
}

bool CollisionChannelsMatch(const UStaticMeshComponent& Component, bool Floor, bool Sight)
{
    return Component.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics
        && Component.GetCollisionObjectType() == ECC_WorldStatic
        && Component.GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block
        && Component.GetCollisionResponseToChannel(ECC_GameTraceChannel1) == (Floor ? ECR_Block : ECR_Ignore)
        && Component.GetCollisionResponseToChannel(ECC_GameTraceChannel2) == (Sight ? ECR_Block : ECR_Ignore);
}

bool ConvexShapeMatches(const UBodySetup& Body, const TArray<FVector>& Expected)
{
    if (Body.CollisionTraceFlag != CTF_UseSimpleAsComplex || Body.AggGeom.ConvexElems.Num() != 1) { return false; }
    const FKConvexElem& Convex = Body.AggGeom.ConvexElems[0];
    if (!Convex.GetTransform().Equals(FTransform::Identity, 0.) || Convex.VertexData.Num() != Expected.Num()) { return false; }
    for (int32 I = 0; I < Expected.Num(); ++I)
    {
        if (Convex.VertexData[I] != Expected[I]) { return false; }
    }
    return true;
}

FString MeshPath(const FString& Id) { return FString(GeometryRoot) + TEXT("SM_Demo_") + Id; }
const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");

double RationalCm(const FLDDemoRational& V) { return double(V.Numerator) / double(V.Denominator); }

// The footprint ring of a cylinder around its own centre: the regular LDDemoWorld::CylinderSides-gon inscribed in its
// radius, so the physical trunk never reaches past the circle every rule tests against.
bool CylinderRing(const FLDDemoWorldBlocker& Blocker, TArray<FVector>& Out, FString& Error)
{
    if (Blocker.RadiusCm.Denominator <= 0 || Blocker.RadiusCm.Numerator <= 0) { return Fail(Error, TEXT("Invalid typed blocker cylinder.")); }
    const double Radius = RationalCm(Blocker.RadiusCm);
    for (int32 I = 0; I < LDDemoWorld::CylinderSides; ++I)
    {
        const double Angle = 2 * UE_DOUBLE_PI * I / LDDemoWorld::CylinderSides;
        Out.Add(FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0));
    }
    return true;
}

// #97: trees of one size share one trunk mesh, placed at each tree's centre, instead of one mesh asset per tree.
FString TrunkId(const FLDDemoWorldBlocker& Blocker)
{
    const auto Key = [](const FLDDemoRational& V)
    {
        return V.Denominator == 1 ? FString::Printf(TEXT("%lld"), V.Numerator) : FString::Printf(TEXT("%lld_%lld"), V.Numerator, V.Denominator);
    };
    return TEXT("trunk_r") + Key(Blocker.RadiusCm) + TEXT("_h") + Key(Blocker.HeightCm);
}

bool TrunkVertices(const FLDDemoWorldBlocker& Blocker, TArray<FVector>& Out, FString& Error)
{
    if (Blocker.HeightCm.Denominator <= 0 || Blocker.HeightCm.Numerator <= 0) { return Fail(Error, TEXT("Invalid typed blocker geometry.")); }
    if (!CylinderRing(Blocker, Out, Error)) { return false; }
    const int32 Ring = Out.Num();
    for (int32 I = 0; I < Ring; ++I) { Out.Add(Out[I] + FVector(0, 0, RationalCm(Blocker.HeightCm))); }
    return true;
}

// Prism vertices in world space: the footprint ring at the ground, then the same ring at the blocker's height. A
// cylinder's ring is its CylinderRing around its centre.
bool BlockerVertices(const FLDDemoWorldBlocker& Blocker, TArray<FVector>& Out, FString& Error)
{
    if (Blocker.HeightCm.Denominator <= 0 || Blocker.HeightCm.Numerator <= 0)
    {
        return Fail(Error, TEXT("Invalid typed blocker geometry."));
    }
    const double Height = double(Blocker.HeightCm.Numerator) / double(Blocker.HeightCm.Denominator);
    if (Blocker.Shape == ELDDemoWorldBlockerShape::Polygon)
    {
        if (Blocker.PolygonCm.Num() < 3) { return Fail(Error, TEXT("Invalid typed blocker polygon.")); }
        for (const auto& Source : Blocker.PolygonCm)
        {
            FVector Point;
            if (!Source.ToVector(Point, Error)) { return false; }
            Out.Add(Point);
        }
    }
    else
    {
        FVector Centre;
        if (!Blocker.CenterCm.ToVector(Centre, Error) || !CylinderRing(Blocker, Out, Error)) { return false; }
        for (FVector& Point : Out) { Point += Centre; }
    }
    const int32 Ring = Out.Num();
    for (int32 I = 0; I < Ring; ++I) { Out.Add(Out[I] + FVector(0, 0, Height)); }
    return true;
}

// #97 terrain prism: the footprint ring at Base, then the authored top ring (flat for a plateau, one sloped plane for
// a ramp). A ramp's low edge lies on its base, so those base points coincide with their top points.
struct FTerrainPiece
{
    FString Id;
    TArray<FVector> Prism;
};

bool TerrainRing(const TArray<FLDDemoWorldPoint>& Polygon, double Base, TArray<FVector>& Out, FString& Error)
{
    TArray<FVector> Top;
    for (const auto& Source : Polygon)
    {
        FVector Point;
        if (!Source.ToVector(Point, Error)) { return false; }
        Top.Add(Point);
    }
    for (const FVector& Point : Top) { Out.Add(FVector(Point.X, Point.Y, Base)); }
    Out.Append(Top);
    return true;
}

bool TerrainPieces(const FLDDemoWorldGeometry& Geometry, TArray<FTerrainPiece>& Out, FString& Error)
{
    if (Geometry.LevelStepCm.Denominator <= 0 || Geometry.LevelStepCm.Numerator <= 0) { return Fail(Error, TEXT("Invalid typed level step.")); }
    const double Step = double(Geometry.LevelStepCm.Numerator) / double(Geometry.LevelStepCm.Denominator);
    for (const auto& Plateau : Geometry.Plateaus)
    {
        FTerrainPiece& Piece = Out.Emplace_GetRef();
        Piece.Id = Plateau.Id;
        if (!TerrainRing(Plateau.PolygonCm, 0, Piece.Prism, Error)) { return false; }
    }
    for (const auto& Ramp : Geometry.Ramps)
    {
        FTerrainPiece& Piece = Out.Emplace_GetRef();
        Piece.Id = Ramp.Id;
        if (!TerrainRing(Ramp.PolygonCm, Ramp.LowerLevel * Step, Piece.Prism, Error)) { return false; }
    }
    return true;
}

// Convex collision of a prism without base points that coincide with their top points.
TArray<FVector> ConvexPoints(const TArray<FVector>& Prism)
{
    const int32 Ring = Prism.Num() / 2;
    TArray<FVector> Out;
    for (int32 I = 0; I < Ring; ++I) { if (Prism[I] != Prism[I + Ring]) { Out.Add(Prism[I]); } }
    for (int32 I = Ring; I < Prism.Num(); ++I) { Out.Add(Prism[I]); }
    return Out;
}

// #97: a cliff is closed by an invisible guard, not by its height. A guard is a thin box on every stretch of a terrain
// edge whose outer side is lower flat ground: plateau edges not met by a ramp or by ground as high, and both sides of
// every ramp. Guards stop bodies and navigation only; sight, floor and pointer traces pass through, so high ground
// still sees low ground across a cliff.
const FName CliffGuardTag(TEXT("LDDemoCliffGuard"));
constexpr double GuardSampleCm = 5, GuardThicknessCm = 2;

struct FCliffGuard
{
    FString Id;
    FVector2D From = FVector2D::ZeroVector, To = FVector2D::ZeroVector;
};

// At Point on an edge, Inner is the tier just inside (see LDDemoWorld::FTerrain), and the edge is open to a fall when
// the tier just outside is flat ground below Ceiling, inside the field. Outward is the half-thickness edge normal.
bool Exposed(const LDDemoWorld::FTerrain& Levels, const FVector& Min, const FVector& Max, const FVector2D& Point,
    const FVector2D& Outward, int32 Inner, int32 Ceiling)
{
    const FVector2D Outside = Point + Outward;
    if (Outside.X <= Min.X || Outside.X >= Max.X || Outside.Y <= Min.Y || Outside.Y >= Max.Y) { return false; }
    const int32 Below = Levels.Tier(Outside);
    return Levels.Tier(Point - Outward) == Inner && Below % 2 == 0 && Below < Ceiling;
}

FVector2D Outward(const FVector2D& A, const FVector2D& B) { return FVector2D(B.Y - A.Y, A.X - B.X).GetSafeNormal() * (GuardThicknessCm * .5); }

// Each run of open GuardSampleCm samples along A->B becomes one guard spanning the run.
void EdgeGuards(const LDDemoWorld::FTerrain& Levels, const FVector& Min, const FVector& Max, const FString& Owner, int32 Edge,
    const FVector2D& A, const FVector2D& B, int32 Inner, int32 Ceiling, TArray<FCliffGuard>& Out)
{
    const FVector2D Normal = Outward(A, B);
    const int32 Samples = FMath::Max(1, FMath::CeilToInt32(FVector2D::Distance(A, B) / GuardSampleCm));
    int32 Start = INDEX_NONE, Run = 0;
    for (int32 I = 0; I <= Samples; ++I)
    {
        const bool Open = I < Samples && Exposed(Levels, Min, Max, FMath::Lerp(A, B, (I + .5) / Samples), Normal, Inner, Ceiling);
        if (Open && Start == INDEX_NONE) { Start = I; }
        if (!Open && Start != INDEX_NONE)
        {
            Out.Add({FString::Printf(TEXT("cliff_%s_%d_%d"), *Owner, Edge, Run++),
                FMath::Lerp(A, B, double(Start) / Samples), FMath::Lerp(A, B, double(I) / Samples)});
            Start = INDEX_NONE;
        }
    }
}

bool Ring2D(const TArray<FLDDemoWorldPoint>& Polygon, TArray<FVector2D>& Out, FString& Error)
{
    for (const auto& Source : Polygon)
    {
        FVector Point;
        if (!Source.ToVector(Point, Error)) { return false; }
        Out.Add(FVector2D(Point));
    }
    return true;
}

// Calls Visit(Owner, Edge, A, B, Inner, Ceiling) for every plateau edge and ramp side that may face a fall.
template <typename F>
bool ForEachTerrainEdge(const FLDDemoWorldGeometry& Geometry, F&& Visit, FString& Error)
{
    for (const auto& Plateau : Geometry.Plateaus)
    {
        TArray<FVector2D> Points;
        if (!Ring2D(Plateau.PolygonCm, Points, Error)) { return false; }
        for (int32 I = 0; I < Points.Num(); ++I) { Visit(Plateau.Id, I, Points[I], Points[(I + 1) % Points.Num()], 2 * Plateau.Level, 2 * Plateau.Level); }
    }
    for (const auto& Ramp : Geometry.Ramps)
    {
        TArray<FVector2D> Points;
        if (!Ring2D(Ramp.PolygonCm, Points, Error) || Points.Num() != 4) { return Fail(Error, TEXT("Invalid typed ramp quad: ") + Ramp.Id); }
        for (const int32 I : {1, 3}) { Visit(Ramp.Id, I, Points[I], Points[(I + 1) % 4], 2 * Ramp.LowerLevel + 1, 2 * Ramp.LowerLevel + 2); }
    }
    return true;
}

bool CliffGuards(const FLDDemoWorldGeometry& Geometry, TArray<FCliffGuard>& Out, FString& Error)
{
    LDDemoWorld::FTerrain Levels;
    FVector Min, Max;
    if (!Levels.Build(Geometry, Error) || !Geometry.Surfaces[0].MinCm.ToVector(Min, Error) || !Geometry.Surfaces[0].MaxCm.ToVector(Max, Error)) { return false; }
    return ForEachTerrainEdge(Geometry, [&](const FString& Owner, int32 Edge, const FVector2D& A, const FVector2D& B, int32 Inner, int32 Ceiling)
    {
        EdgeGuards(Levels, Min, Max, Owner, Edge, A, B, Inner, Ceiling, Out);
    }, Error);
}

// Guards rise from the ground past a full capsule standing on the highest level.
double GuardHeight(const FLDDemoWorldGeometry& Geometry, const FCollisionShape& Capsule)
{
    return (LDDemoWorld::MaxLevel + 1) * RationalCm(Geometry.LevelStepCm) + 2 * Capsule.GetCapsuleHalfHeight();
}

FTransform GuardTransform(const FCliffGuard& Guard, double Height)
{
    const FVector2D Mid = (Guard.From + Guard.To) * .5, Along = Guard.To - Guard.From;
    return FTransform(FRotator(0, FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X)), 0), FVector(Mid, Height * .5),
        FVector(Along.Size(), GuardThicknessCm, Height) / 100.);
}

// Every face of a generated convex prism faces away from its interior, as Unreal reads a triangle's front
// (Cross(P2 - P0, P1 - P0)): tops face the sky, cliff and wall faces face out, so nothing renders inside out.
bool FacesOutward(const UStaticMesh& Mesh)
{
    const FMeshDescription* Description = Mesh.GetMeshDescription(0);
    if (!Description || Description->Triangles().Num() == 0) { return false; }
    FStaticMeshConstAttributes Attributes(*Description);
    const auto Positions = Attributes.GetVertexPositions();
    FVector3f Inside = FVector3f::ZeroVector;
    for (const FVertexID Vertex : Description->Vertices().GetElementIDs()) { Inside += Positions[Vertex]; }
    Inside /= float(Description->Vertices().Num());
    for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
    {
        const TArrayView<const FVertexID> Corner = Description->GetTriangleVertices(Triangle);
        const FVector3f A = Positions[Corner[0]], B = Positions[Corner[1]], C = Positions[Corner[2]];
        if (FVector3f::DotProduct(FVector3f::CrossProduct(C - A, B - A), (A + B + C) / 3.f - Inside) <= 0) { return false; }
    }
    return true;
}

bool GuardChannelsMatch(const UStaticMeshComponent& Component)
{
    if (Component.GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics || Component.GetCollisionObjectType() != ECC_WorldStatic
        || !Component.CanEverAffectNavigation()) { return false; }
    for (const ECollisionChannel Channel : {ECC_WorldStatic, ECC_WorldDynamic, ECC_Pawn, ECC_Visibility, ECC_Camera, ECC_PhysicsBody,
        ECC_Vehicle, ECC_Destructible, ECC_GameTraceChannel1, ECC_GameTraceChannel2})
    {
        if (Component.GetCollisionResponseToChannel(Channel) != (Channel == ECC_Pawn ? ECR_Block : ECR_Ignore)) { return false; }
    }
    return true;
}

// One generated prism mesh: Points is a base ring followed by its top ring, Convex the exact collision hull. Terrain
// uses hard edges so each face keeps its own normal and the hill surface tells tops and ramps from cliff faces.
UStaticMesh* BuildPrism(const FString& Id, const TArray<FVector>& Points, const TArray<FVector>& ConvexHull, bool bHardEdges,
    const FString& Checksum, FString& Error)
{
    const FString PackagePath = MeshPath(Id), Name = FPackageName::GetShortName(PackagePath);
    UStaticMesh* Mesh = nullptr;
    if (FPackageName::DoesPackageExist(PackagePath))
    {
        Mesh = LoadObject<UStaticMesh>(nullptr, *(PackagePath + TEXT(".") + Name));
        if (!Mesh) { Fail(Error, TEXT("Existing generated geometry cannot be loaded: ") + PackagePath); return nullptr; }
    }
    else { Mesh = NewObject<UStaticMesh>(CreatePackage(*PackagePath), *Name, RF_Public | RF_Standalone); }
    Mesh->GetOutermost()->FullyLoad();
    Mesh->SetNumSourceModels(1);
    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description);
    Attributes.Register();
    auto Positions = Attributes.GetVertexPositions();
    auto UVs = Attributes.GetVertexInstanceUVs();
    UVs.SetNumChannels(1);
    const FPolygonGroupID Group = Description.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Stone");
    TArray<FVertexID> Vertices;
    for (const FVector& Point : Points)
    {
        const FVertexID Vertex = Description.CreateVertex();
        Positions[Vertex] = FVector3f(Point);
        Vertices.Add(Vertex);
    }
    const auto Triangle = [&](int32 A, int32 B, int32 C)
    {
        if (FVector::CrossProduct(Points[B] - Points[A], Points[C] - Points[A]).SizeSquared() <= UE_KINDA_SMALL_NUMBER) { return; }
        TArray<FVertexInstanceID> Instances;
        for (int32 Index : {A, B, C})
        {
            const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertices[Index]);
            UVs.Set(Instance, 0, FVector2f(Points[Index].X / 100., Points[Index].Y / 100.));
            Instances.Add(Instance);
        }
        Description.CreatePolygon(Group, Instances);
    };
    // Rings run counterclockwise in plan. Unreal takes a triangle's front side from Cross(P2 - P0, P1 - P0), so each
    // face lists its corners clockwise as seen from outside: the top faces up, the base down and every side outward.
    const int32 Ring = Points.Num() / 2;
    for (int32 I = 1; I + 1 < Ring; ++I)
    {
        Triangle(0, I, I + 1);
        Triangle(Ring, Ring + I + 1, Ring + I);
    }
    for (int32 I = 0; I < Ring; ++I)
    {
        const int32 Next = (I + 1) % Ring;
        Triangle(I, Next + Ring, Next);
        Triangle(I, I + Ring, Next + Ring);
    }
    if (bHardEdges)
    {
        auto Hardness = Attributes.GetEdgeHardnesses();
        for (const FEdgeID Edge : Description.Edges().GetElementIDs()) { Hardness[Edge] = true; }
    }
    auto& Settings = Mesh->GetSourceModel(0).BuildSettings;
    Settings.bRecomputeNormals = true;
    Settings.bRecomputeTangents = true;
    Mesh->CreateMeshDescription(0, MoveTemp(Description));
    Mesh->CommitMeshDescription(0);
    Mesh->GetStaticMaterials().Reset();
    Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Stone")));
    Mesh->CreateBodySetup();
    UBodySetup* Body = Mesh->GetBodySetup();
    Body->InvalidatePhysicsData();
    Body->AggGeom.EmptyElements();
    FKConvexElem Convex;
    Convex.VertexData = ConvexHull;
    Convex.UpdateElemBox();
    Body->AggGeom.ConvexElems.Add(MoveTemp(Convex));
    Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
    Mesh->Build(false);
    Body->CreatePhysicsMeshes();
    Mesh->CreateNavCollision(true);
    Mesh->GetOutermost()->GetMetaData().SetValue(Mesh, GeometryStamp, *Checksum);
    return LDDemoWorldBuilder::SaveAsset(*Mesh, PackagePath, Error) ? Mesh : nullptr;
}

// A polygon blocker's own world-space mesh, or the trunk mesh its tree size shares (built once, kept in Trunks).
UStaticMesh* BuildBlocker(const FLDDemoWorldBlocker& Blocker, const FString& Checksum, TMap<FString, UStaticMesh*>& Trunks, FString& Error)
{
    TArray<FVector> Points;
    if (Blocker.Shape == ELDDemoWorldBlockerShape::Cylinder)
    {
        const FString Id = TrunkId(Blocker);
        if (UStaticMesh* const* Found = Trunks.Find(Id)) { return *Found; }
        if (!TrunkVertices(Blocker, Points, Error)) { return nullptr; }
        UStaticMesh* Mesh = BuildPrism(Id, Points, Points, false, Checksum, Error);
        if (Mesh) { Trunks.Add(Id, Mesh); }
        return Mesh;
    }
    if (!BlockerVertices(Blocker, Points, Error)) { return nullptr; }
    return BuildPrism(Blocker.Id, Points, Points, false, Checksum, Error);
}

AStaticMeshActor* Place(UWorld& World, const FString& Id, UStaticMesh* Mesh, UMaterialInterface* Material,
    const FVector& Position, const FVector& Scale, bool Floor, bool Movement, bool Sight, FString& Error)
{
    if (!Mesh || !Material) { Fail(Error, TEXT("Required shared mesh/material is absent: ") + Id); return nullptr; }
    auto* Actor = World.SpawnActor<AStaticMeshActor>(Position, FRotator::ZeroRotator);
    if (!Actor) { Fail(Error, TEXT("Static geometry actor creation failed: ") + Id); return nullptr; }
    Actor->SetActorLabel(Id);
    Actor->Tags.Add(FName(*Id));
    Actor->Tags.Add(LDDemoWorld::StaticGeometryTag);
    Actor->SetActorScale3D(Scale);
    auto* Component = Actor->GetStaticMeshComponent();
    Component->SetStaticMesh(Mesh);
    Component->SetMaterial(0, Material);
    Component->SetMobility(EComponentMobility::Static);
    Component->SetCollisionProfileName(TEXT("BlockAll"));
    Component->SetCollisionEnabled(Movement ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    Component->SetCollisionObjectType(ECC_WorldStatic);
    Component->SetCollisionResponseToChannel(ECC_Pawn, Movement ? ECR_Block : ECR_Ignore);
    Component->SetCollisionResponseToChannel(ECC_GameTraceChannel1, Floor ? ECR_Block : ECR_Ignore);
    Component->SetCollisionResponseToChannel(ECC_GameTraceChannel2, Sight ? ECR_Block : ECR_Ignore);
    Component->SetCanEverAffectNavigation(Movement);
    // #97: bodies have no step-up onto generated geometry; they climb only what is walkable floor (ramps).
    Component->CanCharacterStepUpOn = ECB_No;
    return Actor;
}

// Presentation of the generated world: engine-only materials and lighting, rebuilt from this code on every generation.
// None of it collides, affects navigation or sight, or is read by any rule.
const TCHAR* FieldMaterialPath = TEXT("/Game/Lockdown/Generated/Materials/M_DemoField");
const TCHAR* WallMaterialPath = TEXT("/Game/Lockdown/Generated/Materials/M_DemoWall");
const TCHAR* ApronMaterialPath = TEXT("/Game/Lockdown/Generated/Materials/M_DemoApron");
const TCHAR* TreeMaterialPath = TEXT("/Game/Lockdown/Generated/Materials/M_DemoTree");
const TCHAR* HillMaterialPath = TEXT("/Game/Lockdown/Generated/Materials/M_DemoHill");
const TCHAR* FenceMaterialPath = TEXT("/Game/Lockdown/Generated/Materials/M_DemoFence");
// Paving reaches this far past the field on every side, so props and buildings outside the walls stand on ground.
constexpr double ApronMarginCm = 4000.;
const TCHAR* ApronTag = TEXT("LDDemoApron");

FString ObjectPath(const TCHAR* PackagePath) { return FString(PackagePath) + TEXT(".") + FPackageName::GetShortName(PackagePath); }

UMaterial* NewGeneratedMaterial(const TCHAR* PackagePath, FString& Error)
{
    UMaterial* Material = nullptr;
    if (FPackageName::DoesPackageExist(PackagePath))
    {
        Material = LoadObject<UMaterial>(nullptr, *ObjectPath(PackagePath));
        if (!Material) { Fail(Error, FString(TEXT("Existing generated material cannot be loaded: ")) + PackagePath); return nullptr; }
        Material->GetOutermost()->FullyLoad();
        Material->GetExpressionCollection().Empty();
        UMaterialEditorOnlyData* Inputs = Material->GetEditorOnlyData();
        Inputs->BaseColor = FColorMaterialInput();
        Inputs->Roughness = FScalarMaterialInput();
        Inputs->Specular = FScalarMaterialInput();
    }
    else { Material = NewObject<UMaterial>(CreatePackage(PackagePath), *FPackageName::GetShortName(PackagePath), RF_Public | RF_Standalone); }
    return Material;
}

template <typename T>
T* AddNode(UMaterial& Material)
{
    T* Node = NewObject<T>(&Material);
    Material.GetExpressionCollection().AddExpression(Node);
    return Node;
}

UMaterialExpression* ColorNode(UMaterial& Material, const FLinearColor& Value)
{
    auto* Node = AddNode<UMaterialExpressionConstant3Vector>(Material);
    Node->Constant = Value;
    return Node;
}

UMaterialExpression* ScalarNode(UMaterial& Material, float Value)
{
    auto* Node = AddNode<UMaterialExpressionConstant>(Material);
    Node->R = Value;
    return Node;
}

UMaterialExpression* MaskNode(UMaterial& Material, UMaterialExpression* Input, bool R, bool G, bool B)
{
    auto* Node = AddNode<UMaterialExpressionComponentMask>(Material);
    Node->Input.Connect(0, Input);
    Node->R = R;
    Node->G = G;
    Node->B = B;
    Node->A = false;
    return Node;
}

// Below when Value < Threshold, otherwise Above.
UMaterialExpression* ChooseNode(UMaterial& Material, UMaterialExpression* Value, float Threshold, UMaterialExpression* Below,
    UMaterialExpression* Above)
{
    auto* Node = AddNode<UMaterialExpressionIf>(Material);
    Node->A.Connect(0, Value);
    Node->ConstB = Threshold;
    Node->ALessThanB.Connect(0, Below);
    Node->AEqualsB.Connect(0, Above);
    Node->AGreaterThanB.Connect(0, Above);
    return Node;
}

UMaterialExpression* MinNode(UMaterial& Material, UMaterialExpression* A, UMaterialExpression* B)
{
    auto* Node = AddNode<UMaterialExpressionMin>(Material);
    Node->A.Connect(0, A);
    Node->B.Connect(0, B);
    return Node;
}

UMaterialExpression* OneMinusNode(UMaterial& Material, UMaterialExpression* Input)
{
    auto* Node = AddNode<UMaterialExpressionOneMinus>(Material);
    Node->Input.Connect(0, Input);
    return Node;
}

UMaterial* FinishMaterial(UMaterial& Material, UMaterialExpression* BaseColor, float Roughness, float Specular,
    const TCHAR* PackagePath, FString& Error)
{
    UMaterialEditorOnlyData* Inputs = Material.GetEditorOnlyData();
    Inputs->BaseColor.Connect(0, BaseColor);
    Inputs->Roughness.Connect(0, ScalarNode(Material, Roughness));
    Inputs->Specular.Connect(0, ScalarNode(Material, Specular));
    Material.PreEditChange(nullptr);
    Material.PostEditChange();
    return LDDemoWorldBuilder::SaveAsset(Material, PackagePath, Error) ? &Material : nullptr;
}

// The school playground: two-tone mown turf with soft stripe edges and low-frequency patches, inside a red running
// track with a white inner line, laid along the ground's outline by the top face's 0..1 texture coordinates.
UMaterial* BuildFieldMaterial(FString& Error)
{
    UMaterial* Material = NewGeneratedMaterial(FieldMaterialPath, Error);
    if (!Material) { return nullptr; }
    auto* UV = AddNode<UMaterialExpressionTextureCoordinate>(*Material);
    UMaterialExpression* U = MaskNode(*Material, UV, true, false, false);
    UMaterialExpression* V = MaskNode(*Material, UV, false, true, false);
    UMaterialExpression* Edge = MinNode(*Material, MinNode(*Material, U, OneMinusNode(*Material, U)),
        MinNode(*Material, V, OneMinusNode(*Material, V)));
    // Nine stripe pairs; each stripe eases into the next over about a metre.
    auto* Bands = AddNode<UMaterialExpressionMultiply>(*Material);
    Bands->A.Connect(0, U);
    Bands->ConstB = 9.f;
    auto* Phase = AddNode<UMaterialExpressionFrac>(*Material);
    Phase->Input.Connect(0, Bands);
    auto* Centred = AddNode<UMaterialExpressionSubtract>(*Material);
    Centred->A.Connect(0, Phase);
    Centred->ConstB = .5f;
    auto* Offset = AddNode<UMaterialExpressionAbs>(*Material);
    Offset->Input.Connect(0, Centred);
    auto* Ease = AddNode<UMaterialExpressionSmoothStep>(*Material);
    Ease->Value.Connect(0, Offset);
    Ease->ConstMin = .21f;
    Ease->ConstMax = .29f;
    auto* Stripes = AddNode<UMaterialExpressionLinearInterpolate>(*Material);
    Stripes->A.Connect(0, ColorNode(*Material, FLinearColor(.085f, .27f, .06f)));
    Stripes->B.Connect(0, ColorNode(*Material, FLinearColor(.06f, .21f, .045f)));
    Stripes->Alpha.Connect(0, Ease);
    // Patches a few metres across keep the turf from reading as flat paint.
    auto* Patches = AddNode<UMaterialExpressionNoise>(*Material);
    Patches->Position.Connect(0, AddNode<UMaterialExpressionWorldPosition>(*Material));
    Patches->Scale = .0015f;
    Patches->NoiseFunction = NOISEFUNCTION_GradientALU;
    Patches->Quality = 1;
    Patches->Levels = 3;
    Patches->bTurbulence = false;
    Patches->OutputMin = .72f;
    Patches->OutputMax = 1.18f;
    auto* Turf = AddNode<UMaterialExpressionMultiply>(*Material);
    Turf->A.Connect(0, Stripes);
    Turf->B.Connect(0, Patches);
    UMaterialExpression* Inside = ChooseNode(*Material, Edge, .066f, ColorNode(*Material, FLinearColor(.82f, .82f, .78f)), Turf);
    UMaterialExpression* Ground = ChooseNode(*Material, Edge, .06f, ColorNode(*Material, FLinearColor(.47f, .13f, .08f)), Inside);
    return FinishMaterial(*Material, Ground, .92f, .3f, FieldMaterialPath, Error);
}

// School walls: cream plaster above a red skirting band at the foot.
UMaterial* BuildWallMaterial(FString& Error)
{
    UMaterial* Material = NewGeneratedMaterial(WallMaterialPath, Error);
    if (!Material) { return nullptr; }
    UMaterialExpression* Height = MaskNode(*Material, AddNode<UMaterialExpressionWorldPosition>(*Material), false, false, true);
    UMaterialExpression* Wall = ChooseNode(*Material, Height, 45.f, ColorNode(*Material, FLinearColor(.42f, .1f, .07f)),
        ColorNode(*Material, FLinearColor(.72f, .66f, .55f)));
    return FinishMaterial(*Material, Wall, .85f, .35f, WallMaterialPath, Error);
}

// Trees: a dark bark foot under a mottled leafy green, so a trunk reads apart from the cream walls.
UMaterial* BuildTreeMaterial(FString& Error)
{
    UMaterial* Material = NewGeneratedMaterial(TreeMaterialPath, Error);
    if (!Material) { return nullptr; }
    auto* Mottle = AddNode<UMaterialExpressionNoise>(*Material);
    Mottle->Position.Connect(0, AddNode<UMaterialExpressionWorldPosition>(*Material));
    Mottle->Scale = .02f;
    Mottle->NoiseFunction = NOISEFUNCTION_GradientALU;
    Mottle->Quality = 1;
    Mottle->Levels = 2;
    Mottle->bTurbulence = false;
    Mottle->OutputMin = .7f;
    Mottle->OutputMax = 1.15f;
    auto* Leaves = AddNode<UMaterialExpressionMultiply>(*Material);
    Leaves->A.Connect(0, ColorNode(*Material, FLinearColor(.07f, .2f, .05f)));
    Leaves->B.Connect(0, Mottle);
    UMaterialExpression* Height = MaskNode(*Material, AddNode<UMaterialExpressionWorldPosition>(*Material), false, false, true);
    UMaterialExpression* Tree = ChooseNode(*Material, Height, 60.f, ColorNode(*Material, FLinearColor(.16f, .09f, .045f)), Leaves);
    return FinishMaterial(*Material, Tree, .9f, .25f, TreeMaterialPath, Error);
}

// Fences: painted timber posts every metre and two rails, the gaps clipped away so the low fence reads as see-through,
// which is what the rules make it: it stops bodies but not sight.
UMaterial* BuildFenceMaterial(FString& Error)
{
    UMaterial* Material = NewGeneratedMaterial(FenceMaterialPath, Error);
    if (!Material) { return nullptr; }
    Material->BlendMode = BLEND_Masked;
    Material->TwoSided = true;
    UMaterialExpression* Position = AddNode<UMaterialExpressionWorldPosition>(*Material);
    auto* Along = AddNode<UMaterialExpressionAdd>(*Material);
    Along->A.Connect(0, MaskNode(*Material, Position, true, false, false));
    Along->B.Connect(0, MaskNode(*Material, Position, false, true, false));
    auto* Posts = AddNode<UMaterialExpressionMultiply>(*Material);
    Posts->A.Connect(0, Along);
    Posts->ConstB = 1.f / 100.f;
    auto* PostPhase = AddNode<UMaterialExpressionFrac>(*Material);
    PostPhase->Input.Connect(0, Posts);
    auto* Rails = AddNode<UMaterialExpressionMultiply>(*Material);
    Rails->A.Connect(0, MaskNode(*Material, Position, false, false, true));
    Rails->ConstB = 1.f / 50.f;
    auto* RailPhase = AddNode<UMaterialExpressionFrac>(*Material);
    RailPhase->Input.Connect(0, Rails);
    auto* Solid = AddNode<UMaterialExpressionAdd>(*Material);
    Solid->A.Connect(0, ChooseNode(*Material, PostPhase, .1f, ScalarNode(*Material, 1.f), ScalarNode(*Material, 0.f)));
    Solid->B.Connect(0, ChooseNode(*Material, RailPhase, .76f, ScalarNode(*Material, 0.f), ScalarNode(*Material, 1.f)));
    Material->GetEditorOnlyData()->OpacityMask.Connect(0, Solid);
    return FinishMaterial(*Material, ColorNode(*Material, FLinearColor(.36f, .22f, .11f)), .8f, .3f, FenceMaterialPath, Error);
}

// A blocker's generated surface: trunks are trees, sight blockers walls, and every non-sight polygon a fence.
const TCHAR* BlockerMaterialPath(const FLDDemoWorldBlocker& Blocker)
{
    if (Blocker.Shape == ELDDemoWorldBlockerShape::Cylinder) { return TreeMaterialPath; }
    return Blocker.BlocksSight ? WallMaterialPath : FenceMaterialPath;
}

// Raised ground: mottled grass on plateau tops, ramps and cliff faces alike (#97: the darker rock faces now take the
// surrounding grass; lighting alone sets a cliff apart from the top).
UMaterial* BuildHillMaterial(FString& Error)
{
    UMaterial* Material = NewGeneratedMaterial(HillMaterialPath, Error);
    if (!Material) { return nullptr; }
    auto* Mottle = AddNode<UMaterialExpressionNoise>(*Material);
    Mottle->Position.Connect(0, AddNode<UMaterialExpressionWorldPosition>(*Material));
    Mottle->Scale = .006f;
    Mottle->NoiseFunction = NOISEFUNCTION_GradientALU;
    Mottle->Quality = 1;
    Mottle->Levels = 3;
    Mottle->bTurbulence = false;
    Mottle->OutputMin = .78f;
    Mottle->OutputMax = 1.15f;
    auto* Grass = AddNode<UMaterialExpressionMultiply>(*Material);
    Grass->A.Connect(0, ColorNode(*Material, FLinearColor(.11f, .29f, .07f)));
    Grass->B.Connect(0, Mottle);
    return FinishMaterial(*Material, Grass, .92f, .3f, HillMaterialPath, Error);
}

// Campus paving beyond the walls: warm grey two-metre slabs with darker joints and faint weathering, darker than the
// turf so the field stays the focus.
UMaterial* BuildApronMaterial(FString& Error)
{
    UMaterial* Material = NewGeneratedMaterial(ApronMaterialPath, Error);
    if (!Material) { return nullptr; }
    auto* Slabs = AddNode<UMaterialExpressionMultiply>(*Material);
    Slabs->A.Connect(0, MaskNode(*Material, AddNode<UMaterialExpressionWorldPosition>(*Material), true, true, false));
    Slabs->ConstB = 1.f / 200.f;
    auto* Phase = AddNode<UMaterialExpressionFrac>(*Material);
    Phase->Input.Connect(0, Slabs);
    UMaterialExpression* Joint = MinNode(*Material, MaskNode(*Material, Phase, true, false, false),
        MaskNode(*Material, Phase, false, true, false));
    auto* Weather = AddNode<UMaterialExpressionNoise>(*Material);
    Weather->Position.Connect(0, AddNode<UMaterialExpressionWorldPosition>(*Material));
    Weather->Scale = .002f;
    Weather->NoiseFunction = NOISEFUNCTION_GradientALU;
    Weather->Quality = 1;
    Weather->Levels = 3;
    Weather->bTurbulence = false;
    Weather->OutputMin = .85f;
    Weather->OutputMax = 1.08f;
    auto* Paving = AddNode<UMaterialExpressionMultiply>(*Material);
    Paving->A.Connect(0, ColorNode(*Material, FLinearColor(.24f, .23f, .21f)));
    Paving->B.Connect(0, Weather);
    UMaterialExpression* Surface = ChooseNode(*Material, Joint, .025f, ColorNode(*Material, FLinearColor(.15f, .145f, .135f)), Paving);
    return FinishMaterial(*Material, Surface, .95f, .3f, ApronMaterialPath, Error);
}

FVector ApronLocation(const FVector& Min, const FVector& Max, double Depth) { return (Min + Max) * .5 - FVector(0, 0, Depth * .5 + 1.); }

FVector ApronScale(const FVector& Min, const FVector& Max, double Depth)
{
    return FVector(Max.X - Min.X + 2. * ApronMarginCm, Max.Y - Min.Y + 2. * ApronMarginCm, Depth) / 100.;
}

// Visual paving under and around the field, 1 cm below the turf: no collision, navigation, shadow or rule identity.
bool PlaceApron(UWorld& World, UStaticMesh* Mesh, UMaterialInterface* Material, const FVector& Min, const FVector& Max,
    double Depth, FString& Error)
{
    if (!Mesh || !Material) { return Fail(Error, TEXT("The paving apron needs the shared cube and its generated material.")); }
    auto* Actor = World.SpawnActor<AStaticMeshActor>(ApronLocation(Min, Max, Depth), FRotator::ZeroRotator);
    if (!Actor) { return Fail(Error, TEXT("Cannot create the paving apron.")); }
    Actor->SetActorLabel(TEXT("DemoApron"));
    Actor->Tags.Add(ApronTag);
    Actor->SetActorScale3D(ApronScale(Min, Max, Depth));
    Actor->SetActorEnableCollision(false);
    auto* Component = Actor->GetStaticMeshComponent();
    Component->SetStaticMesh(Mesh);
    Component->SetMaterial(0, Material);
    Component->SetMobility(EComponentMobility::Static);
    Component->SetCollisionProfileName(TEXT("NoCollision"));
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCanEverAffectNavigation(false);
    Component->SetCastShadow(false);
    return true;
}

// A clear afternoon: one warm, fairly low sun casts clear shadows and drives the physical sky; the sky light captures
// that sky as softer ambient light, a light height fog adds depth at the far edges, and an unbound post-process grades.
bool BuildEnvironment(UWorld& World, FString& Error)
{
    auto* Sun = World.SpawnActor<ADirectionalLight>(FVector(0, 0, 3000), FRotator(-45, -35, 0));
    auto* Sky = World.SpawnActor<ASkyAtmosphere>();
    auto* Ambient = World.SpawnActor<ASkyLight>();
    auto* Fog = World.SpawnActor<AExponentialHeightFog>();
    auto* Post = World.SpawnActor<APostProcessVolume>();
    if (!Sun || !Sky || !Ambient || !Ambient->GetLightComponent() || !Fog || !Fog->GetComponent() || !Post)
    {
        return Fail(Error, TEXT("Cannot create the Demo world environment."));
    }
    auto* Light = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetIntensity(10.f);
    Light->SetUseTemperature(true);
    Light->SetTemperature(5600.f);
    Light->SetCastShadows(true);
    Light->SetAtmosphereSunLight(true);
    // Short screen-space contact shadows ground the bodies on the turf.
    Light->ContactShadowLength = .03f;
    Sun->SetActorLabel(TEXT("DemoSun"));
    Sky->SetActorLabel(TEXT("DemoSkyAtmosphere"));
    USkyLightComponent* SkyLight = Ambient->GetLightComponent();
    SkyLight->SetMobility(EComponentMobility::Movable);
    SkyLight->SourceType = SLS_CapturedScene;
    SkyLight->SetRealTimeCapture(true);
    SkyLight->SetIntensity(.6f);
    Ambient->SetActorLabel(TEXT("DemoSkyLight"));
    UExponentialHeightFogComponent* Haze = Fog->GetComponent();
    Haze->SetFogDensity(.01f);
    Haze->SetFogHeightFalloff(.2f);
    Haze->SetStartDistance(3000.f);
    Haze->SetFogMaxOpacity(.6f);
    Fog->SetActorLabel(TEXT("DemoHeightFog"));
    Post->bUnbound = true;
    FPostProcessSettings& Grade = Post->Settings;
    // The engine's default exposure bias (+1) and the project's flattened local exposure wash the bright field out.
    Grade.bOverride_AutoExposureBias = true;
    Grade.AutoExposureBias = -.8f;
    Grade.bOverride_LocalExposureHighlightContrastScale = true;
    Grade.LocalExposureHighlightContrastScale = 1.f;
    Grade.bOverride_LocalExposureShadowContrastScale = true;
    Grade.LocalExposureShadowContrastScale = 1.f;
    Grade.bOverride_BloomIntensity = true;
    Grade.BloomIntensity = .4f;
    Grade.bOverride_ColorSaturation = true;
    Grade.ColorSaturation = FVector4(1.15, 1.15, 1.15, 1.);
    Grade.bOverride_ColorContrast = true;
    Grade.ColorContrast = FVector4(1.1, 1.1, 1.1, 1.);
    Grade.bOverride_VignetteIntensity = true;
    Grade.VignetteIntensity = .3f;
    Post->SetActorLabel(TEXT("DemoPostProcess"));
    return true;
}

bool NavigationBounds(const FLDDemoWorldGeometry& Geometry, const FCollisionShape& Capsule, FBox& Out, FString& Error)
{
    FVector Min, Max;
    if (!Geometry.Surfaces[0].MinCm.ToVector(Min, Error) || !Geometry.Surfaces[0].MaxCm.ToVector(Max, Error)) { return false; }
    Min.Z -= Capsule.GetCapsuleHalfHeight();
    FBox Bounds(Min, Max);
    for (const auto& Blocker : Geometry.Blockers)
    {
        TArray<FVector> Points;
        if (!BlockerVertices(Blocker, Points, Error)) { return false; }
        for (const FVector& Point : Points) { Bounds += Point; }
    }
    TArray<FTerrainPiece> Terrain;
    if (!TerrainPieces(Geometry, Terrain, Error)) { return false; }
    for (const auto& Piece : Terrain) { for (const FVector& Point : Piece.Prism) { Bounds += Point; } }
    Out = Bounds.ExpandBy(FVector(Capsule.GetCapsuleRadius(), Capsule.GetCapsuleRadius(), Capsule.GetCapsuleHalfHeight()));
    return true;
}

bool BuildNavigation(UWorld& World, const FLDDemoWorldGeometry& Geometry, const FCollisionShape& Capsule, FString& Error)
{
    FBox Envelope;
    if (!NavigationBounds(Geometry, Capsule, Envelope, Error)) { return false; }
    const FVector Size = Envelope.GetSize();
    auto* Bounds = World.SpawnActor<ALDRegionNavBounds>(Envelope.GetCenter(), FRotator::ZeroRotator);
    if (!Bounds) { return Fail(Error, TEXT("Demo navigation bounds creation failed.")); }
    Bounds->Configure(Size);
    Bounds->SetActorLabel(TEXT("DemoNavigationBounds"));
    Bounds->Tags.Add(TEXT("demo_navigation_bounds"));
    Bounds->Brush = NewObject<UModel>(Bounds, NAME_None, RF_Transactional);
    Bounds->Brush->Initialize(Bounds, false);
    Bounds->GetBrushComponent()->Brush = Bounds->Brush;
    auto* Builder = NewObject<UCubeBuilder>(Bounds, NAME_None, RF_Transactional);
    Bounds->BrushBuilder = Builder;
    Builder->X = Size.X; Builder->Y = Size.Y; Builder->Z = Size.Z;
    if (!Builder->Build(&World, Bounds)) { return Fail(Error, TEXT("Demo navigation brush generation failed.")); }
    Bounds->SetActorLocation(Envelope.GetCenter());
    Bounds->Brush->BuildBound();
    Bounds->GetBrushComponent()->BuildSimpleBrushCollision();
    Bounds->GetBrushComponent()->UpdateBounds();
    Bounds->PostEditChange();
    Bounds->PostEditMove(true);
    Bounds->GetBrushComponent()->SetCanEverAffectNavigation(false);
    auto* Nav = Cast<ULDDemoNavigationSystem>(World.GetNavigationSystem());
    if (!Nav || !Nav->HasApprovedAgent()) { return Fail(Error, TEXT("Demo navigation system/approved agent is absent.")); }
    const FNavDataConfig& Config = Nav->GetDefaultSupportedAgentConfig();
    auto* Recast = Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));
    if (!Recast)
    {
        Recast = Cast<ARecastNavMesh>(Nav->CreateNavigationDataInstanceInLevel(Config, World.PersistentLevel));
        if (Recast) { Nav->RequestRegistrationDeferred(*Recast); }
    }
    if (!Recast) { return Fail(Error, TEXT("Demo requires native Recast navigation data.")); }
    if (Recast->GetConfig().AgentRadius != Capsule.GetCapsuleRadius()
        || Recast->GetConfig().AgentHeight != Capsule.GetCapsuleHalfHeight() * 2)
    {
        return Fail(Error, FString::Printf(TEXT("Demo navigation agent differs: registered=%s radius=%.3f height=%.3f; supported=%s radius=%.3f height=%.3f; required radius=%.3f height=%.3f."),
            *Recast->GetConfig().Name.ToString(), Recast->GetConfig().AgentRadius, Recast->GetConfig().AgentHeight,
            *Config.Name.ToString(), Config.AgentRadius, Config.AgentHeight, Capsule.GetCapsuleRadius(), Capsule.GetCapsuleHalfHeight() * 2));
    }
    Recast->TileSizeUU = 1024;
    for (uint8 I = 0; I < uint8(ENavigationDataResolution::MAX); ++I)
    {
        Recast->SetCellSize(ENavigationDataResolution(I), Config.AgentRadius / 4);
        Recast->SetCellHeight(ENavigationDataResolution(I), 5);
        Recast->SetAgentMaxStepHeight(ENavigationDataResolution(I), 35);
    }
    Recast->PostEditChange();
    FlushAsyncLoading();
    FAssetCompilingManager::Get().FinishAllCompilation();
    Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    Nav->OnNavigationBoundsUpdated(Bounds);
    Nav->Build();
    Recast->EnsureBuildCompletion();
    return (Recast->IsRegistered() && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) == Recast
        && Nav->IsNavigationBuilt(World.GetWorldSettings())) || Fail(Error, TEXT("Demo navigation build/agent registration did not finish successfully."));
}
}

bool LDDemoWorldBuilder::SaveAsset(UObject& Asset, const FString& PackagePath, FString& Error)
{
    const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true))
    {
        return Fail(Error, TEXT("Cannot create generated asset directory: ") + File);
    }
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(Asset.GetOutermost(), &Asset, *File, Args)
        || Fail(Error, TEXT("Generated asset could not be saved: ") + File);
}

bool LDDemoWorldBuilder::GenerateFrontend(UWorld*& Out, FString& Error)
{
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!World || !World->GetWorldSettings()) { return Fail(Error, TEXT("Cannot create the frontend editor world.")); }
    World->GetWorldSettings()->DefaultGameMode = ALDGameMode::StaticClass();
    if (!ValidateFrontend(*World, Error)) { return false; }
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, LDDemoEntry::FrontendMap))
    {
        return Fail(Error, TEXT("The blank frontend map could not be saved."));
    }
    Out = World;
    Error.Reset();
    return true;
}

bool LDDemoWorldBuilder::ValidateFrontend(const UWorld& World, FString& Error)
{
    if (!World.PersistentLevel) { return Fail(Error, TEXT("The frontend map has no persistent level.")); }
    for (AActor* Actor : World.PersistentLevel->Actors)
    {
        // Transient actors (the engine's default physics volume) are never saved with the map.
        if (!Actor || Actor->HasAnyFlags(RF_Transient) || Actor->IsA<AWorldSettings>() || FActorEditorUtils::IsABuilderBrush(Actor)) { continue; }
        return Fail(Error, FString::Printf(TEXT("The frontend map holds the scene actor %s (%s); the title screen shows only its UI and ")
            TEXT("background image. Run BootstrapAssets.ps1 -Mode RegenerateFrontend."), *Actor->GetName(), *Actor->GetClass()->GetName()));
    }
    Error.Reset();
    return true;
}

bool LDDemoWorldBuilder::Generate(ULDMapDefinition& Definition, const FLDDemoContentSnapshot& Content, UWorld*& Out, FString& Error)
{
    TSharedPtr<const FLDDemoWorldGeometry> Geometry;
    FCollisionShape Capsule;
    if (!Definition.Read(Content, Geometry, Error) || !ULDMapRegistry::ContentCapsule(Content, Capsule, Error)) { return false; }
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!World || !World->GetWorldSettings()) { return Fail(Error, TEXT("Cannot create the Demo editor world.")); }
    World->GetWorldSettings()->DefaultGameMode = ALDGameMode::StaticClass();
    auto* NavigationConfig = World->GetWorldSettings()->GetNavigationSystemConfig();
    if (!NavigationConfig) { return Fail(Error, TEXT("The Demo world has no navigation configuration object.")); }
    NavigationConfig->NavigationSystemClass = FSoftClassPath(ULDDemoNavigationSystem::StaticClass());
    NavigationConfig->DefaultAgentName = TEXT("DemoBody");
    NavigationConfig->SupportedAgentsMask.Empty();
    NavigationConfig->SupportedAgentsMask.Set(0);
    NavigationConfig->SupportedAgentsMask.MarkInitialized();
    FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorMode, NavigationConfig, true, true);
    auto* Anchor = World->SpawnActor<ALDMapAnchor>();
    if (!Anchor) { return Fail(Error, TEXT("Cannot create the sole Demo map anchor.")); }
    Anchor->Definition = &Definition;
    Anchor->SetActorLabel(TEXT("DemoWorldDefinition"));
    auto* Cube = LoadObject<UStaticMesh>(nullptr, CubePath);
    UMaterial* GroundMaterial = BuildFieldMaterial(Error);
    if (!GroundMaterial) { return false; }
    UMaterial* StoneMaterial = BuildWallMaterial(Error);
    if (!StoneMaterial) { return false; }
    UMaterial* PavingMaterial = BuildApronMaterial(Error);
    if (!PavingMaterial) { return false; }
    UMaterial* TreeMaterial = BuildTreeMaterial(Error);
    if (!TreeMaterial) { return false; }
    UMaterial* HillMaterial = BuildHillMaterial(Error);
    if (!HillMaterial) { return false; }
    UMaterial* FenceMaterial = BuildFenceMaterial(Error);
    if (!FenceMaterial) { return false; }
    FVector Min, Max;
    if (!Geometry->Surfaces[0].MinCm.ToVector(Min, Error) || !Geometry->Surfaces[0].MaxCm.ToVector(Max, Error)) { return false; }
    const double Depth = Capsule.GetCapsuleHalfHeight();
    if (!Place(*World, Geometry->Surfaces[0].Id, Cube, GroundMaterial, (Min + Max) * .5 - FVector(0, 0, Depth * .5),
        FVector(Max.X - Min.X, Max.Y - Min.Y, Depth) / 100., true, true, false, Error)) { return false; }
    if (!PlaceApron(*World, Cube, PavingMaterial, Min, Max, Depth, Error)) { return false; }
    TArray<FTerrainPiece> Terrain;
    if (!TerrainPieces(*Geometry, Terrain, Error)) { return false; }
    for (const auto& Piece : Terrain)
    {
        UStaticMesh* Mesh = BuildPrism(Piece.Id, Piece.Prism, ConvexPoints(Piece.Prism), true, Definition.Checksum, Error);
        AStaticMeshActor* Actor = Place(*World, Piece.Id, Mesh, HillMaterial, FVector::ZeroVector, FVector::OneVector,
            true, true, false, Error);
        if (!Actor) { return false; }
        Actor->Tags.Add(LDDemoWorld::TerrainTag);
    }
    TMap<FString, UStaticMesh*> Trunks;
    for (const auto& Blocker : Geometry->Blockers)
    {
        UStaticMesh* Mesh = BuildBlocker(Blocker, Definition.Checksum, Trunks, Error);
        if (!Mesh) { return false; }
        FVector Position = FVector::ZeroVector;
        if (Blocker.Shape == ELDDemoWorldBlockerShape::Cylinder && !Blocker.CenterCm.ToVector(Position, Error)) { return false; }
        const TCHAR* Path = BlockerMaterialPath(Blocker);
        UMaterial* Surface = Path == TreeMaterialPath ? TreeMaterial : Path == FenceMaterialPath ? FenceMaterial : StoneMaterial;
        if (!Place(*World, Blocker.Id, Mesh, Surface, Position, FVector::OneVector,
            false, Blocker.BlocksMovement, Blocker.BlocksSight, Error)) { return false; }
    }
    TArray<FCliffGuard> Guards;
    if (!CliffGuards(*Geometry, Guards, Error)) { return false; }
    const double GuardTop = GuardHeight(*Geometry, Capsule);
    for (const auto& Guard : Guards)
    {
        AStaticMeshActor* Actor = Place(*World, Guard.Id, Cube, StoneMaterial, FVector::ZeroVector, FVector::OneVector,
            false, true, false, Error);
        if (!Actor) { return false; }
        Actor->SetActorTransform(GuardTransform(Guard, GuardTop));
        Actor->Tags.Add(CliffGuardTag);
        auto* Component = Actor->GetStaticMeshComponent();
        Component->SetCollisionResponseToAllChannels(ECR_Ignore);
        Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        Component->SetVisibility(false);
        Component->SetHiddenInGame(true);
        Component->SetCastShadow(false);
    }
    for (const auto& Source : Geometry->Anchors)
    {
        FVector Foot;
        if (!Source.PositionCm.ToVector(Foot, Error)) { return false; }
        if (Source.Kind == ELDDemoWorldAnchorKind::Spawn)
        {
            auto* Start = World->SpawnActor<APlayerStart>(Foot + FVector(0, 0, Capsule.GetCapsuleHalfHeight()), FRotator::ZeroRotator);
            if (!Start) { return Fail(Error, TEXT("Cannot create Demo player start: ") + Source.Id); }
            Start->PlayerStartTag = FName(*Source.Id);
            Start->SetActorLabel(Source.Id);
            Start->SetActorEnableCollision(false);
        }
    }
    if (!BuildEnvironment(*World, Error)) { return false; }
    if (!BuildNavigation(*World, *Geometry, Capsule, Error)) { return false; }
    World->GetOutermost()->GetMetaData().SetValue(World, GeometryStamp, *Definition.Checksum);
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, LDDemoEntry::WorldMap))
    {
        return Fail(Error, TEXT("The generated Demo map could not be saved."));
    }
    Out = World;
    Error.Reset();
    return true;
}

bool LDDemoWorldBuilder::Validate(UWorld& World, const ULDMapDefinition& Definition, const FLDDemoContentSnapshot& Content,
    const FString& ReportDirectory, FString& Error)
{
    FlushAsyncLoading();
    FAssetCompilingManager::Get().FinishAllCompilation();
    TSharedPtr<const FLDDemoWorldGeometry> Geometry;
    FCollisionShape Capsule;
    if (!Definition.Read(Content, Geometry, Error) || !ULDMapRegistry::ContentCapsule(Content, Capsule, Error)) { return false; }
    auto Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Checks;
    bool Good = true;
    FString FirstFailure;
    const auto Check = [&](bool Passed, const FString& Name)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Name);
        Row->SetBoolField(TEXT("passed"), Passed);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        if (!Passed)
        {
            Good = false;
            if (FirstFailure.IsEmpty()) { FirstFailure = Name; }
            UE_LOG(LogLDEditor, Error, TEXT("LD_DEMO_WORLD_CHECK_FAILED %s"), *Name);
        }
        return Passed;
    };
    Check(World.GetWorldSettings() && World.GetWorldSettings()->DefaultGameMode == ALDGameMode::StaticClass(),
        TEXT("Saved map uses the unique native Demo GameMode"));
    Check(FString(World.GetOutermost()->GetMetaData().GetValue(&World, GeometryStamp)).Equals(Definition.Checksum, ESearchCase::CaseSensitive),
        TEXT("Saved map stamp identifies the verified world payload"));
    int32 Anchors = 0, BoundsCount = 0, NavCount = 0, StaticCount = 0, Steppable = 0;
    TSet<const UStaticMesh*> Faced;
    TArray<FString> InsideOut;
    int32 Suns = 0, Skies = 0, SkyLights = 0, Fogs = 0, Grades = 0, Aprons = 0;
    const AStaticMeshActor* Apron = nullptr;
    TMap<FString, const AStaticMeshActor*> Meshes;
    FBox ExpectedBounds;
    if (!NavigationBounds(*Geometry, Capsule, ExpectedBounds, Error)) { return false; }
    for (const AActor* Actor : World.PersistentLevel->Actors)
    {
        if (const auto* Sun = Cast<ADirectionalLight>(Actor))
        {
            const auto* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
            Suns += Light && Light->IsUsedAsAtmosphereSunLight() && Light->CastShadows ? 1 : 100;
        }
        if (Cast<ASkyAtmosphere>(Actor)) { ++Skies; }
        if (const auto* Ambient = Cast<ASkyLight>(Actor))
        {
            const auto* Light = Ambient->GetLightComponent();
            SkyLights += Light && Light->bRealTimeCapture && Light->Mobility == EComponentMobility::Movable ? 1 : 100;
        }
        if (Cast<AExponentialHeightFog>(Actor)) { ++Fogs; }
        if (const auto* Post = Cast<APostProcessVolume>(Actor)) { Grades += Post->bUnbound ? 1 : 100; }
        if (const auto* Anchor = Cast<ALDMapAnchor>(Actor))
        {
            ++Anchors;
            Check(Anchor->Definition == &Definition, TEXT("Sole map anchor binds DA_DemoWorld"));
        }
        if (const auto* Bounds = Cast<ANavMeshBoundsVolume>(Actor))
        {
            ++BoundsCount;
            const FBox Actual = Bounds->GetComponentsBoundingBox(true);
            Check(Bounds->IsA<ALDRegionNavBounds>() && Actual.Min.Equals(ExpectedBounds.Min, .1)
                && Actual.Max.Equals(ExpectedBounds.Max, .1),
                TEXT("Saved native/cooked navigation envelope matches geometry and capsule; actual=")
                    + Actual.ToString() + TEXT(" expected=") + ExpectedBounds.ToString());
        }
        if (const auto* Nav = Cast<ARecastNavMesh>(Actor))
        {
            ++NavCount;
            Check(Nav->AgentRadius == Capsule.GetCapsuleRadius() && Nav->GetConfig().AgentRadius == Capsule.GetCapsuleRadius()
                && Nav->AgentHeight == Capsule.GetCapsuleHalfHeight() * 2
                && Nav->GetConfig().AgentHeight == Capsule.GetCapsuleHalfHeight() * 2,
                TEXT("Saved Recast and registered agent use the actual approved capsule"));
        }
        if (const auto* Mesh = Cast<AStaticMeshActor>(Actor); Mesh && Mesh->Tags.Contains(ApronTag))
        {
            ++Aprons;
            Apron = Mesh;
        }
        if (const auto* Mesh = Cast<AStaticMeshActor>(Actor); Mesh && Mesh->Tags.Contains(LDDemoWorld::StaticGeometryTag))
        {
            ++StaticCount;
            Steppable += Mesh->GetStaticMeshComponent()->CanCharacterStepUpOn == ECB_No ? 0 : 1;
            const UStaticMesh* Shape = Mesh->GetStaticMeshComponent()->GetStaticMesh();
            if (Shape && Shape->GetPathName().StartsWith(GeometryRoot) && !Faced.Contains(Shape))
            {
                Faced.Add(Shape);
                if (!FacesOutward(*Shape)) { InsideOut.Add(Shape->GetName()); }
            }
            const FString Id = Mesh->Tags[0].ToString();
            Check(!Meshes.Contains(Id), TEXT("Unique static geometry identity: ") + Id);
            Meshes.Add(Id, Mesh);
        }
    }
    TArray<FTerrainPiece> Terrain;
    if (!TerrainPieces(*Geometry, Terrain, Error)) { return false; }
    TArray<FCliffGuard> Guards;
    if (!CliffGuards(*Geometry, Guards, Error)) { return false; }
    Check(Anchors == 1 && BoundsCount == 1 && NavCount == 1 && StaticCount == 1 + Terrain.Num() + Geometry->Blockers.Num() + Guards.Num(),
        TEXT("Exact native anchor, navigation and static-geometry counts"));
    Check(Steppable == 0, TEXT("No generated geometry lets a body step up onto it"));
    Check(!Faced.IsEmpty() && InsideOut.IsEmpty(),
        TEXT("Every generated terrain, wall, fence and trunk face points out of its prism: ") + FString::Join(InsideOut, TEXT(", ")));
    Check(Suns == 1 && Skies == 1 && SkyLights == 1 && Fogs == 1 && Grades == 1,
        TEXT("The playground is lit by one shadowing atmosphere sun, one sky, one real-time sky light, one height fog and one unbound grade"));
    const auto* Navigation = Cast<ULDDemoNavigationSystem>(World.GetNavigationSystem());
    Check(Navigation && Navigation->HasApprovedAgent()
        && World.GetWorldSettings()->GetNavigationSystemConfig()->NavigationSystemClass == FSoftClassPath(ULDDemoNavigationSystem::StaticClass()),
        TEXT("Saved navigation configuration recreates the same approved agent on load"));
    FVector Min, Max;
    if (!Geometry->Surfaces[0].MinCm.ToVector(Min, Error) || !Geometry->Surfaces[0].MaxCm.ToVector(Max, Error)) { return false; }
    const auto* FloorActor = Meshes.FindRef(Geometry->Surfaces[0].Id);
    if (Check(FloorActor != nullptr, TEXT("The registered ground is present")))
    {
        const auto* Component = FloorActor->GetStaticMeshComponent();
        const double Depth = Capsule.GetCapsuleHalfHeight();
        Check(Component->GetStaticMesh()
            && Component->GetStaticMesh()->GetPathName() == CubePath
            && FloorActor->GetActorLocation().Equals((Min + Max) * .5 - FVector(0, 0, Depth * .5), UE_KINDA_SMALL_NUMBER)
            && FloorActor->GetActorScale3D().Equals(FVector(Max.X - Min.X, Max.Y - Min.Y, Depth) / 100., UE_KINDA_SMALL_NUMBER)
            && FloorActor->GetActorRotation().IsNearlyZero()
            && CollisionChannelsMatch(*Component, true, false),
            TEXT("Ground transform, native support and sight classification match the source"));
        const UMaterialInterface* Field = Component->GetMaterial(0);
        Check(Field && Field->GetPathName() == ObjectPath(FieldMaterialPath), TEXT("The ground is the generated playground field"));
    }
    if (Check(Aprons == 1 && Apron != nullptr, TEXT("Exactly one paving apron surrounds the field")))
    {
        const auto* Component = Apron->GetStaticMeshComponent();
        const double Depth = Capsule.GetCapsuleHalfHeight();
        Check(Component->GetStaticMesh()
            && Component->GetStaticMesh()->GetPathName() == CubePath
            && Apron->GetActorLocation().Equals(ApronLocation(Min, Max, Depth), UE_KINDA_SMALL_NUMBER)
            && Apron->GetActorScale3D().Equals(ApronScale(Min, Max, Depth), UE_KINDA_SMALL_NUMBER)
            && Apron->GetActorRotation().IsNearlyZero()
            && !Apron->GetActorEnableCollision()
            && Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision
            && !Component->CanEverAffectNavigation() && !Component->CastShadow,
            TEXT("The paving apron sits 1 cm below the turf and 40 m past the field, without collision, navigation or shadow"));
        const UMaterialInterface* Paving = Component->GetMaterial(0);
        Check(Paving && Paving->GetPathName() == ObjectPath(ApronMaterialPath), TEXT("The apron is the generated campus paving"));
    }
    for (const auto& Piece : Terrain)
    {
        const auto* Actor = Meshes.FindRef(Piece.Id);
        if (!Check(Actor != nullptr, TEXT("Terrain piece exists: ") + Piece.Id)) { continue; }
        const auto* Component = Actor->GetStaticMeshComponent();
        const UStaticMesh* Mesh = Component->GetStaticMesh().Get();
        const auto* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
        Check(Actor->GetActorTransform().Equals(FTransform::Identity, UE_KINDA_SMALL_NUMBER) && Actor->Tags.Contains(LDDemoWorld::TerrainTag)
            && CollisionChannelsMatch(*Component, true, false),
            TEXT("Terrain piece is a registered floor that leaves sight to the level rule: ") + Piece.Id);
        const UMaterialInterface* Hill = Component->GetMaterial(0);
        Check(Hill && Hill->GetPathName() == ObjectPath(HillMaterialPath), TEXT("Terrain piece uses the generated hill surface: ") + Piece.Id);
        if (!Check(Mesh && Mesh->GetPathName() == MeshPath(Piece.Id) + TEXT(".") + FPackageName::GetShortName(MeshPath(Piece.Id))
            && Body && Body->CollisionTraceFlag == CTF_UseSimpleAsComplex && Body->AggGeom.ConvexElems.Num() == 1,
            TEXT("Terrain piece uses its exact convex mesh: ") + Piece.Id)) { continue; }
        Check(ConvexShapeMatches(*Body, ConvexPoints(Piece.Prism)),
            TEXT("Saved terrain collision vertices equal the authored prism: ") + Piece.Id);
    }
    for (const auto& Blocker : Geometry->Blockers)
    {
        const auto* Actor = Meshes.FindRef(Blocker.Id);
        if (!Check(Actor != nullptr, TEXT("Blocker exists: ") + Blocker.Id)) { continue; }
        const auto* Component = Actor->GetStaticMeshComponent();
        const UStaticMesh* Mesh = Component->GetStaticMesh().Get();
        const auto* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
        const bool bTrunk = Blocker.Shape == ELDDemoWorldBlockerShape::Cylinder;
        FVector Position = FVector::ZeroVector;
        if (bTrunk && !Blocker.CenterCm.ToVector(Position, Error)) { return false; }
        Check(Actor->GetActorTransform().Equals(FTransform(Position), UE_KINDA_SMALL_NUMBER)
            && CollisionChannelsMatch(*Component, false, Blocker.BlocksSight),
            TEXT("Blocker transform/movement/sight classification: ") + Blocker.Id);
        const UMaterialInterface* Wall = Component->GetMaterial(0);
        Check(Wall && Wall->GetPathName() == ObjectPath(BlockerMaterialPath(Blocker)), TEXT("Blocker uses its generated wall, fence or tree surface: ") + Blocker.Id);
        TArray<FVector> Vertices;
        if (!(bTrunk ? TrunkVertices(Blocker, Vertices, Error) : BlockerVertices(Blocker, Vertices, Error))) { return false; }
        const FString Expected = MeshPath(bTrunk ? TrunkId(Blocker) : Blocker.Id);
        if (!Check(Mesh && Mesh->GetPathName() == Expected + TEXT(".") + FPackageName::GetShortName(Expected)
            && Body && Body->CollisionTraceFlag == CTF_UseSimpleAsComplex && Body->AggGeom.ConvexElems.Num() == 1,
            TEXT("Blocker uses its exact convex mesh (trees: the shared trunk of their size), not an AABB substitute: ") + Blocker.Id)) { continue; }
        Check(ConvexShapeMatches(*Body, Vertices),
            TEXT("Saved collision vertices and local transform equal the authored body-space geometry: ") + Blocker.Id);
    }
    auto* Registry = World.GetSubsystem<ULDMapRegistry>();
    FString BindingError;
    const bool Bound = Registry && Registry->Rebuild(BindingError);
    Check(Bound, TEXT("Native world registry binds every approved anchor and one exact start per spawn: ") + BindingError);
    // Function, not layout: every authored anchor stands on legal ground, each kind of element does what its flags say
    // (walls and fences against movement and sight, trees, ramps, cliffs), and the registry refuses bad queries.
    // No check names a coordinate or a route of this particular map.
    int32 PlacementChecks = 0, RampChecks = 0;
    if (Bound)
    {
        const FLDWorldRevision Revision = Registry->Revision();
        ELDWorldQueryFailure Failure;
        const auto GroundAt = [&](const FVector& Foot, const FString& Id, FLDLegalGround& Legal)
        {
            FString Why;
            const bool Passed = Registry->QueryGround(Revision, Foot, Capsule, TEXT("demo_ground"), Legal, Failure, Why);
            return Check(Passed, TEXT("Local complete-capsule support: ") + Id + TEXT(" ") + Why);
        };
        const auto* RestService = Registry->Resolve(FName(*LDDemoWorld::RestServiceId(1)), Registry->Generation);
        FLDLegalGround RestServiceGround;
        const bool HasRestService = Check(RestService != nullptr, TEXT("The shared rest room has its reference service"))
            && GroundAt(RestService->Position, LDDemoWorld::RestServiceId(1), RestServiceGround);
        FVector Standing = FVector::ZeroVector;
        for (const auto& Anchor : Geometry->Anchors)
        {
            FVector Foot;
            if (!Anchor.PositionCm.ToVector(Foot, Error)) { return false; }
            FLDLegalGround Legal;
            if (GroundAt(Foot, Anchor.Id, Legal)) { Standing = Foot; }
            if (Anchor.Kind == ELDDemoWorldAnchorKind::Spawn || Anchor.Kind == ELDDemoWorldAnchorKind::RestSpawn)
            {
                for (const auto& Offset : Geometry->SpawnOffsetsCm)
                {
                    FVector Delta;
                    if (!Offset.ToVector(Delta, Error)) { return false; }
                    const bool Grounded = GroundAt(Foot + Delta, Anchor.Id + TEXT(" placement"), Legal);
                    if (Grounded && HasRestService && Anchor.Kind == ELDDemoWorldAnchorKind::RestSpawn)
                    {
                        FLDCompletePath Path;
                        FString Why;
                        const bool Reachable = Registry->QueryCompletePath(Revision, Legal, RestServiceGround, Capsule, Path, Failure, Why);
                        Check(Reachable, TEXT("A rest placement can reach the shared-room service: ") + Anchor.Id + TEXT(" ") + Why);
                    }
                    ++PlacementChecks;
                }
            }
            for (const auto& Home : Anchor.Members)
            {
                if (!Home.PositionCm.ToVector(Foot, Error)) { return false; }
                GroundAt(Foot, Home.Id, Legal);
            }
        }
        FLDLegalGround Result;
        FString Why;
        Check(!Registry->QueryGround(Revision, Standing, Capsule, NAME_None, Result, Failure, Why)
            && Failure == ELDWorldQueryFailure::InvalidSurface, TEXT("Missing surface identity is rejected"));
        FLDWorldRevision Stale = Revision;
        ++Stale.GeometryRevision;
        Check(!Registry->QueryGround(Stale, Standing, Capsule, TEXT("demo_ground"), Result, Failure, Why)
            && Failure == ELDWorldQueryFailure::StaleWorld, TEXT("Stale geometry identity is rejected"));
        Check(!Registry->QueryGround(Revision, Standing + FVector(0, 0, 100), Capsule, TEXT("demo_ground"), Result, Failure, Why)
            && Failure == ELDWorldQueryFailure::NotGrounded, TEXT("A point above legal ground is not projected onto the floor"));
        // #97: every ramp carries the capsule from the foot of its low edge, over its slope, onto the plateau above. The
        // ends stand a metre clear of the ramp so Recast's simplified seam polygons at its edges do not move them.
        const LDDemoWorld::FTerrain& Ground = Registry->GetTerrain();
        const auto Foot = [&](const FVector2D& Point)
        {
            double Z = 0;
            FVector Normal;
            Ground.Ground(Point, Z, Normal);
            return FVector(Point.X, Point.Y, Z);
        };
        for (const auto& Ramp : Geometry->Ramps)
        {
            FVector Corner[4];
            for (int32 I = 0; I < 4; ++I) { if (!Ramp.PolygonCm[I].ToVector(Corner[I], Error)) { return false; } }
            const FVector2D Low = FVector2D(Corner[0] + Corner[1]) * .5, High = FVector2D(Corner[2] + Corner[3]) * .5;
            const FVector2D Up = (High - Low).GetSafeNormal() * (Capsule.GetCapsuleRadius() + 100);
            const FVector2D Seam = Low - (High - Low).GetSafeNormal();
            const double Gradient = (Corner[2].Z - Corner[0].Z) / FVector2D::Distance(Low, High);
            FVector SeamFoot = Foot(Seam);
            SeamFoot.Z += Capsule.GetCapsuleRadius() * (FMath::Sqrt(1 + Gradient * Gradient) - 1) - Gradient
                + (UCharacterMovementComponent::MIN_FLOOR_DIST + UCharacterMovementComponent::MAX_FLOOR_DIST) * .5;
            FLDLegalGround SeamGround;
            GroundAt(SeamFoot, Ramp.Id + TEXT(" capsule support across low seam"), SeamGround);
            const FVector2D HighSeam = High - (High - Low).GetSafeNormal();
            FVector HighSeamFoot = Foot(HighSeam);
            HighSeamFoot.Z += Gradient
                + (UCharacterMovementComponent::MIN_FLOOR_DIST + UCharacterMovementComponent::MAX_FLOOR_DIST) * .5;
            FLDLegalGround HighSeamGround;
            GroundAt(HighSeamFoot, Ramp.Id + TEXT(" capsule support across high seam"), HighSeamGround);
            FLDLegalGround Bottom, Middle, Top;
            const bool Ends = GroundAt(Foot(Low - Up), Ramp.Id + TEXT(" foot"), Bottom)
                & GroundAt(Foot((Low + High) * .5), Ramp.Id + TEXT(" slope"), Middle)
                & GroundAt(Foot(High + Up), Ramp.Id + TEXT(" top"), Top);
            FLDCompletePath Path;
            Why.Reset();
            const bool Climbs = Ends && Registry->QueryCompletePath(Revision, Bottom, Top, Capsule, Path, Failure, Why);
            Check(Climbs, TEXT("A ramp is walkable from its foot to its top: ") + Ramp.Id + TEXT(" ") + Why);
            ++RampChecks;
        }
    }
    // #97: guards, not heights, close every cliff. Each derived guard is an invisible movement-only box on its edge, and
    // across every plateau edge or ramp side that faces lower ground, a capsule lifted clear of the higher side is
    // still stopped, so no cliff relies on its height or on step-up rules.
    const double GuardTop = GuardHeight(*Geometry, Capsule);
    TArray<UStaticMeshComponent*> GuardComponents;
    for (const auto& Guard : Guards)
    {
        const auto* Actor = Meshes.FindRef(Guard.Id);
        if (!Check(Actor != nullptr, TEXT("Cliff guard exists: ") + Guard.Id)) { continue; }
        UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
        Check(Component->GetStaticMesh() && Component->GetStaticMesh()->GetPathName() == CubePath
            && Actor->GetActorTransform().Equals(GuardTransform(Guard, GuardTop), 1e-3) && Actor->Tags.Contains(CliffGuardTag)
            && GuardChannelsMatch(*Component) && Component->bHiddenInGame && !Component->IsVisible() && !Component->CastShadow,
            TEXT("Cliff guard is an invisible box that stops only bodies and navigation: ") + Guard.Id);
        GuardComponents.Add(Component);
    }
    LDDemoWorld::FTerrain Levels;
    if (!Levels.Build(*Geometry, Error)) { return false; }
    int32 CliffChecks = 0;
    if (!ForEachTerrainEdge(*Geometry, [&](const FString& Owner, int32 Edge, const FVector2D& A, const FVector2D& B, int32 Inner, int32 Ceiling)
    {
        const FVector2D Mid = (A + B) * .5, Normal = Outward(A, B);
        if (!Exposed(Levels, Min, Max, Mid, Normal, Inner, Ceiling)) { return; }
        const FVector2D Out = Normal.GetSafeNormal() * (Capsule.GetCapsuleRadius() + 20);
        const double Lift = Ceiling / 2 * Levels.StepCm + Capsule.GetCapsuleHalfHeight() + 1;
        bool Stopped = false;
        FHitResult Hit;
        for (UStaticMeshComponent* Component : GuardComponents)
        {
            Stopped = Stopped || Component->SweepComponent(Hit, FVector(Mid + Out, Lift), FVector(Mid - Out, Lift), FQuat::Identity, Capsule);
        }
        Check(Stopped, FString::Printf(TEXT("A cliff guard stops the capsule lifted above the cliff: %s edge %d"), *Owner, Edge));
        ++CliffChecks;
    }, Error)) { return false; }
    // Every blocker, on a line through it across its thinnest side: its own collision stops the capsule, and it
    // spans the eye-height sight line exactly when it is declared to block sight (walls and trees do; fences stay under
    // the sight sample). Channel responses are checked per blocker above.
    const double Eye = double(Geometry->SightSampleHeightCm.Numerator) / double(Geometry->SightSampleHeightCm.Denominator);
    for (const auto& Blocker : Geometry->Blockers)
    {
        const auto* Actor = Meshes.FindRef(Blocker.Id);
        if (!Actor) { continue; }
        TArray<FVector2D> Footprint;
        if (!LDDemoWorld::Footprint(Blocker, Footprint, Error)) { return false; }
        FVector2D Centre = FVector2D::ZeroVector, Edge = FVector2D::ZeroVector;
        for (int32 I = 0; I < Footprint.Num(); ++I)
        {
            Centre += Footprint[I] / Footprint.Num();
            const FVector2D Side = Footprint[(I + 1) % Footprint.Num()] - Footprint[I];
            if (Side.SizeSquared() > Edge.SizeSquared()) { Edge = Side; }
        }
        const FVector2D Normal = FVector2D(-Edge.Y, Edge.X).GetSafeNormal();
        double Reach = 0;
        for (const FVector2D& Point : Footprint) { Reach = FMath::Max(Reach, FMath::Abs(FVector2D::DotProduct(Point - Centre, Normal))); }
        const FVector2D Across = Normal * (Reach + Capsule.GetCapsuleRadius() + 10);
        FVector Base;
        if (!(Blocker.Shape == ELDDemoWorldBlockerShape::Cylinder ? Blocker.CenterCm : Blocker.PolygonCm[0]).ToVector(Base, Error)) { return false; }
        const auto Line = [&](double Height, FVector& From, FVector& To)
        {
            From = FVector(Centre.X - Across.X, Centre.Y - Across.Y, Base.Z + Height);
            To = FVector(Centre.X + Across.X, Centre.Y + Across.Y, Base.Z + Height);
        };
        FVector From, To;
        FHitResult Hit;
        UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
        Line(Capsule.GetCapsuleHalfHeight() + 1, From, To);
        Check(Component->SweepComponent(Hit, From, To, FQuat::Identity, Capsule), TEXT("A blocker stops native capsule movement: ") + Blocker.Id);
        Line(Eye, From, To);
        Check(Component->LineTraceComponent(Hit, From, To, FCollisionQueryParams(SCENE_QUERY_STAT(DemoBlockerSight), false)) == Blocker.BlocksSight,
            TEXT("A blocker spans the eye-height sight line exactly when it blocks sight: ") + Blocker.Id);
    }
    Report->SetStringField(TEXT("scope"), TEXT("Approved static geometry, saved convex collision, navigation and ground bindings. Not gameplay, owned-network, camp/AI or balance acceptance."));
    Report->SetBoolField(TEXT("passed"), Good);
    Report->SetStringField(TEXT("world_sha256"), Definition.Checksum);
    Report->SetStringField(TEXT("catalog_sha256"), Content.Get().Identity.Root.ToHex());
    Report->SetNumberField(TEXT("spawn_placement_checks"), PlacementChecks);
    Report->SetNumberField(TEXT("ramp_checks"), RampChecks);
    Report->SetNumberField(TEXT("cliff_checks"), CliffChecks);
    Report->SetNumberField(TEXT("cliff_guards"), Guards.Num());
    Report->SetArrayField(TEXT("checks"), Checks);
    if (!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(ReportDirectory),
        FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        return Fail(Error, TEXT("World evidence must be written under this project's Artifacts directory."));
    }
    FString Json;
    if (!IFileManager::Get().MakeDirectory(*ReportDirectory, true)
        || !FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
        || !FFileHelper::SaveStringToFile(Json, *FPaths::Combine(ReportDirectory, TEXT("DemoWorld.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        return Fail(Error, TEXT("World validation evidence could not be persisted."));
    }
    if (!Good) { Error = FirstFailure; }
    return Good;
}

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
// Any generated convex prism of the current world: the collision tests take whichever exists, not a named piece of
// one particular layout.
UStaticMesh* GeneratedPrism()
{
    TArray<FAssetData> Assets;
    IAssetRegistry::GetChecked().ScanPathsSynchronous({FString(GeometryRoot).LeftChop(1)}, false);
    IAssetRegistry::GetChecked().GetAssetsByPath(FName(FString(GeometryRoot).LeftChop(1)), Assets, false);
    Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
    for (const FAssetData& Asset : Assets)
    {
        auto* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
        if (Mesh && Mesh->GetBodySetup() && Mesh->GetBodySetup()->AggGeom.ConvexElems.Num() == 1) { return Mesh; }
    }
    return nullptr;
}

class FCollisionPackageFixture
{
public:
    explicit FCollisionPackageFixture(FAutomationTestBase& InTest) : Test(InTest)
    {
        const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Mount = TEXT("/DemoCollision_") + Id + TEXT("/");
        Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts/DemoCollisionPackages/") + Id) + TEXT("/");
        PackagePath = Mount + TEXT("Fixture");
    }

    ~FCollisionPackageFixture()
    {
        Component = nullptr;
        if (UPackage* Package = FindPackage(nullptr, *PackagePath))
        {
            FText Error;
            if (!UPackageTools::UnloadPackages({Package}, Error, true)) { Test.AddError(TEXT("Cannot unload isolated collision package: ") + Error.ToString()); }
        }
        if (Mounted) { FPackageName::UnRegisterMountPoint(Mount, Directory); }
    }

    bool Create(UStaticMesh& Source, bool Floor, bool Sight)
    {
        if (!IFileManager::Get().MakeDirectory(*Directory, true)) { Test.AddError(TEXT("Cannot create isolated package directory.")); return false; }
        FPackageName::RegisterMountPoint(Mount, Directory);
        Mounted = true;
        UPackage* Package = CreatePackage(*PackagePath);
        auto* Mesh = Cast<UStaticMesh>(StaticDuplicateObject(&Source, Package, TEXT("Geometry")));
        if (!Mesh || Mesh->GetBodySetup() == Source.GetBodySetup())
        {
            Test.AddError(TEXT("Collision fixture must own an independent mesh and body setup."));
            return false;
        }
        Mesh->SetFlags(RF_Public | RF_Standalone);
        Component = NewObject<UStaticMeshComponent>(Package, TEXT("Collision"), RF_Public | RF_Standalone);
        Component->SetStaticMesh(Mesh);
        Component->SetCollisionProfileName(TEXT("BlockAll"));
        Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Component->SetCollisionObjectType(ECC_WorldStatic);
        Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        Component->SetCollisionResponseToChannel(ECC_GameTraceChannel1, Floor ? ECR_Block : ECR_Ignore);
        Component->SetCollisionResponseToChannel(ECC_GameTraceChannel2, Sight ? ECR_Block : ECR_Ignore);
        return RoundTrip();
    }

    bool RoundTrip()
    {
        FlushAsyncLoading();
        FAssetCompilingManager::Get().FinishAllCompilation();
        FString Error;
        if (!Component || !LDDemoWorldBuilder::SaveAsset(*Component, PackagePath, Error))
        {
            Test.AddError(TEXT("Isolated collision save failed: ") + Error);
            return false;
        }
        UPackage* Package = Component->GetOutermost();
        TWeakObjectPtr<UStaticMeshComponent> Previous(Component);
        Component = nullptr;
        FText UnloadError;
        if (!UPackageTools::UnloadPackages({Package}, UnloadError)
            || !Test.TestFalse(TEXT("Saved fixture is actually unloaded, not reused from memory"), Previous.IsValid()))
        {
            Test.AddError(TEXT("Isolated collision unload failed: ") + UnloadError.ToString());
            return false;
        }
        Component = LoadObject<UStaticMeshComponent>(nullptr, *(PackagePath + TEXT(".Collision")));
        FlushAsyncLoading();
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (!Component || !Component->GetStaticMesh()
            || Component->GetStaticMesh()->GetOutermost() != Component->GetOutermost())
        {
            Test.AddError(TEXT("Reload did not resolve the isolated component and its isolated mesh."));
            return false;
        }
        Test.AddInfo(TEXT("Isolated saved/reloaded collision evidence: ") + Directory);
        return true;
    }

    UStaticMeshComponent* Get() const { return Component; }

private:
    FAutomationTestBase& Test;
    FString Mount, Directory, PackagePath;
    UStaticMeshComponent* Component = nullptr;
    bool Mounted = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoSavedCollisionResponses, "Lockdown.Rules.DemoWorld.SavedCollisionResponses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoSavedCollisionResponses::RunTest(const FString& Parameters)
{
    for (bool Floor : {true, false})
    {
        TStrongObjectPtr<UStaticMesh> Source(Floor ? LoadObject<UStaticMesh>(nullptr, CubePath) : GeneratedPrism());
        if (!TestNotNull(TEXT("Baseline mesh exists for isolated saved collision tests"), Source.Get())) { return false; }
        FCollisionPackageFixture Fixture(*this);
        if (!Fixture.Create(*Source, Floor, !Floor)) { return false; }
        TestTrue(TEXT("Correct saved/reloaded collision passes the production predicate"), CollisionChannelsMatch(*Fixture.Get(), Floor, !Floor));
        Fixture.Get()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
        if (!Fixture.RoundTrip()) { return false; }
        TestTrue(TEXT("Pawn-only corruption persists while nav Vehicle response and trace classifications remain unchanged"),
            Fixture.Get()->GetCollisionObjectType() == ECC_WorldStatic
            && Fixture.Get()->GetCollisionResponseToChannel(ECC_Vehicle) == ECR_Block
            && Fixture.Get()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore
            && Fixture.Get()->GetCollisionResponseToChannel(ECC_GameTraceChannel1) == (Floor ? ECR_Block : ECR_Ignore)
            && Fixture.Get()->GetCollisionResponseToChannel(ECC_GameTraceChannel2) == (Floor ? ECR_Ignore : ECR_Block));
        TestFalse(TEXT("Production validation rejects the saved Pawn-response-only corruption on floor and blocker"),
            CollisionChannelsMatch(*Fixture.Get(), Floor, !Floor));
        Fixture.Get()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        Fixture.Get()->SetCollisionObjectType(ECC_WorldDynamic);
        if (!Fixture.RoundTrip()) { return false; }
        TestEqual(TEXT("Object-type-only corruption survives disk reload"), Fixture.Get()->GetCollisionObjectType(), ECC_WorldDynamic);
        TestFalse(TEXT("Production validation rejects an incorrect saved collision object type"),
            CollisionChannelsMatch(*Fixture.Get(), Floor, !Floor));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoSavedConvexTransform, "Lockdown.Rules.DemoWorld.SavedConvexTransform",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoSavedConvexTransform::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UStaticMesh> Source(GeneratedPrism());
    if (!TestNotNull(TEXT("Baseline exact polygon mesh exists"), Source.Get())
        || !Source->GetBodySetup() || Source->GetBodySetup()->AggGeom.ConvexElems.Num() != 1) { return false; }
    const TArray<FVector> Expected = Source->GetBodySetup()->AggGeom.ConvexElems[0].VertexData;
    FCollisionPackageFixture Fixture(*this);
    if (!Fixture.Create(*Source, false, true)) { return false; }
    TestTrue(TEXT("Correct saved/reloaded hull passes the same production predicate"),
        ConvexShapeMatches(*Fixture.Get()->GetStaticMesh()->GetBodySetup(), Expected));
    Fixture.Get()->GetStaticMesh()->GetBodySetup()->AggGeom.ConvexElems[0].SetTransform(FTransform(FVector(10, 0, 0)));
    if (!Fixture.RoundTrip()) { return false; }
    const UBodySetup* Body = Fixture.Get()->GetStaticMesh()->GetBodySetup();
    TestTrue(TEXT("Transform-only corruption leaves every raw vertex unchanged"), Body->AggGeom.ConvexElems[0].VertexData == Expected);
    TestTrue(TEXT("The independent saved convex transform survives disk reload"),
        Body->AggGeom.ConvexElems[0].GetTransform().GetTranslation() == FVector(10, 0, 0));
    TestFalse(TEXT("Production validation rejects shifted body-space collision despite identical raw vertices"),
        ConvexShapeMatches(*Body, Expected));
    TestTrue(TEXT("Production mesh was never mutated"), Source->GetBodySetup()->AggGeom.ConvexElems[0].GetTransform().Equals(FTransform::Identity, 0.)
        && Source->GetBodySetup()->AggGeom.ConvexElems[0].VertexData == Expected);
    return true;
}
#endif
