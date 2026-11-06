#include "Region/LDMapRegistry.h"
#include "Core/LDLog.h"
#include "Region/LDMapDefinition.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/LDDemoEntryProbe.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavigationData.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "GameFramework/PlayerStart.h"

namespace
{
bool Reject(ELDWorldQueryFailure Code, const TCHAR* Detail, ELDWorldQueryFailure& Failure, FString& Error)
{
    Failure = Code;
    Error = Detail;
    return false;
}

// Legal standing ground at Foot (the capsule bottom) on the registered terrain: the authored terrain height and slope at
// that XY, a physical floor piece agreeing with them right under the foot, and a capsule clear of static geometry.
// Walls and trees are tested with the whole capsule; other terrain pieces with the capsule lifted by the body's step
// height, so the foot of a ramp or a plateau's rim beside the floor piece is not taken for an obstacle while a cliff
// face still is.
bool Ground(UWorld& World, const FLDDemoWorldGeometry& Geometry, const LDDemoWorld::FTerrain& Terrain,
    const FLDWorldRevision& Revision, const TArray<UPrimitiveComponent*>& Floors, const FCollisionShape& RequiredCapsule,
    const FVector& Foot, const FCollisionShape& Capsule, FName SurfaceHint, FLDLegalGround& Out,
    ELDWorldQueryFailure& Failure, FString& Error)
{
    if (SurfaceHint.IsNone() || SurfaceHint != FName(*Geometry.Surfaces[0].Id))
    {
        return Reject(ELDWorldQueryFailure::InvalidSurface, TEXT("InvalidSurface: unknown or missing registered surface."), Failure, Error);
    }
    if (!Capsule.IsCapsule() || Capsule.GetCapsuleRadius() != RequiredCapsule.GetCapsuleRadius()
        || Capsule.GetCapsuleHalfHeight() != RequiredCapsule.GetCapsuleHalfHeight())
    {
        return Reject(ELDWorldQueryFailure::InvalidCapsule, TEXT("InvalidCapsule: body must match the approved native navigation agent."), Failure, Error);
    }
    FVector Min, Max;
    if (Foot.ContainsNaN() || !Geometry.Surfaces[0].MinCm.ToVector(Min, Error) || !Geometry.Surfaces[0].MaxCm.ToVector(Max, Error))
    {
        return Reject(ELDWorldQueryFailure::InvalidPosition, TEXT("InvalidPosition: nonfinite world coordinate."), Failure, Error);
    }
    const double Radius = Capsule.GetCapsuleRadius(), Height = Capsule.GetCapsuleHalfHeight();
    if (Foot.X - Radius < Min.X || Foot.X + Radius > Max.X || Foot.Y - Radius < Min.Y || Foot.Y + Radius > Max.Y)
    {
        return Reject(ELDWorldQueryFailure::Blocked, TEXT("Blocked: body footprint leaves the registered ground."), Failure, Error);
    }
    double Expected = 0;
    FVector Normal = FVector::UpVector;
    Terrain.Ground(FVector2D(Foot.X, Foot.Y), Expected, Normal);
    // On a slope the capsule's lowest point rests above the plane by Radius * (1 / cos - 1).
    const double Lift = Radius * (1.0 / Normal.Z - 1.0);
    const double Gap = UCharacterMovementComponent::MAX_FLOOR_DIST + UE_KINDA_SMALL_NUMBER;
    FHitResult Hit;
    const bool NearFloor = Foot.Z >= Expected - Gap && Foot.Z <= Expected + Lift + Gap;
    const bool HitFloor = NearFloor && World.LineTraceSingleByChannel(Hit,
        Foot + FVector(0, 0, Gap), Foot - FVector(0, 0, Lift + 2 * Gap), ECC_GameTraceChannel1);
    if (!NearFloor || !HitFloor || FVector::DotProduct(Hit.ImpactNormal, Normal) < 1.0 - 1e-4
        || FMath::Abs(Hit.ImpactPoint.Z - Expected) > Gap)
    {
        const FString Detail = FString::Printf(TEXT("NotGrounded: no matching local support; foot=%s terrain=%.3f hit=%d normalZ=%.6f impactZ=%.6f hitActor=%s. No downward fallback."),
            *Foot.ToString(), Expected, HitFloor, Hit.ImpactNormal.Z, Hit.ImpactPoint.Z, *GetNameSafe(Hit.GetActor()));
        return Reject(ELDWorldQueryFailure::NotGrounded, *Detail, Failure, Error);
    }
    UPrimitiveComponent* Support = Hit.GetComponent();
    if (!Floors.Contains(Support))
    {
        return Reject(ELDWorldQueryFailure::WrongSurface, TEXT("WrongSurface: the physical support differs from the supplied surface."), Failure, Error);
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoGroundCapsule), false);
    for (const UPrimitiveComponent* Floor : Floors) { Query.AddIgnoredActor(Floor->GetOwner()); }
    if (World.OverlapAnyTestByObjectType(Foot + FVector(0, 0, Height), FQuat::Identity,
        FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllStaticObjects), Capsule, Query))
    {
        return Reject(ELDWorldQueryFailure::Blocked, TEXT("Blocked: native capsule intersects static geometry."), Failure, Error);
    }
    const double Step = GetDefault<ALDCharacter>()->GetCharacterMovement()->MaxStepHeight;
    for (UPrimitiveComponent* Floor : Floors)
    {
        if (Floor != Support && Floor->OverlapComponent(Foot + FVector(0, 0, Height + Step), FQuat::Identity, Capsule))
        {
            return Reject(ELDWorldQueryFailure::Blocked, TEXT("Blocked: native capsule meets a cliff or higher terrain."), Failure, Error);
        }
    }
    FLDLegalGround Candidate;
    Candidate.World = Revision;
    Candidate.FootCm = Foot;
    Candidate.SurfaceCm = Hit.ImpactPoint;
    Candidate.SurfaceId = SurfaceHint;
    Candidate.Floor = Support;
    Out = MoveTemp(Candidate);
    Failure = ELDWorldQueryFailure::None;
    Error.Reset();
    return true;
}

TArray<UPrimitiveComponent*> Pieces(const TArray<TWeakObjectPtr<UPrimitiveComponent>>& Floors)
{
    TArray<UPrimitiveComponent*> Out;
    for (const auto& Floor : Floors) { if (Floor.IsValid()) { Out.Add(Floor.Get()); } }
    return Out;
}
}

bool ULDMapRegistry::ShouldCreateSubsystem(UObject* Outer) const
{
    const auto* World = Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World && (!World->IsGameWorld()
        || UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == LDDemoEntry::WorldMap);
}

void ULDMapRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
#if WITH_DEV_AUTOMATION_TESTS
    LDDemoTests::ObserveMapRegistry(*GetWorld(), false);
#endif
}

void ULDMapRegistry::OnWorldBeginPlay(UWorld& W)
{
    Super::OnWorldBeginPlay(W);
    if (UWorld::RemovePIEPrefix(W.GetOutermost()->GetName()) == LDDemoEntry::WorldMap)
    {
        FString Error;
        if (!Rebuild(Error)) { UE_LOG(LogLDWorld, Error, TEXT("LD_DEMO_WORLD_BINDING_REJECTED %s"), *Error); }
#if WITH_DEV_AUTOMATION_TESTS
        if (Enabled()) { LDDemoTests::ObserveMapRegistry(W, true); }
#endif
    }
}
bool ULDMapRegistry::Ensure(FString& Error)
{
    // Every approved anchor is bound once; the count follows the world payload instead of a fixed number.
    if (Enabled() && Bindings.Num() == Geometry->Anchors.Num()) { Error.Reset(); return true; }
    return Rebuild(Error);
}

bool ULDMapRegistry::ContentCapsule(const FLDDemoContentSnapshot& Content, FCollisionShape& Out, FString& Error)
{
    const auto& Radius = Content.Get().Parameters.CollisionRadiusCm;
    const double NativeRadius = double(Radius.Numerator) / double(Radius.Denominator);
    const double HalfHeight = GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    if (Radius.Denominator <= 0 || !FMath::IsFinite(NativeRadius) || NativeRadius <= 0 || NativeRadius > HalfHeight)
    {
        Error = TEXT("Approved collision radius does not fit the retained native body height.");
        return false;
    }
    Out = FCollisionShape::MakeCapsule(float(NativeRadius), float(HalfHeight));
    Error.Reset();
    return true;
}

bool ULDMapRegistry::Rebuild(FString& Error)
{
    Definition.Reset(); Content.Reset(); Geometry.Reset(); GroundComponent.Reset(); TerrainFloors.Reset(); Terrain = {};
    WorldRevision = {}; Generation = 0; Bindings.Reset(); ServiceBindings.Reset();
    if (!GetWorld() || (GetWorld()->IsGameWorld()
        && UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap))
    {
        Error = TEXT("Map registry is available only for the Demo world or editor asset validation.");
        return false;
    }
    int32 AnchorCount = 0, FloorCount = 0;
    const ULDMapDefinition* CandidateDefinition = nullptr;
    UPrimitiveComponent* Floor = nullptr;
    TArray<UPrimitiveComponent*> Floors;
    TArray<const APlayerStart*> Starts;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if (const auto* Anchor = Cast<ALDMapAnchor>(*It)) { ++AnchorCount; CandidateDefinition = Anchor->Definition; }
        if (const auto* Start = Cast<APlayerStart>(*It)) { Starts.Add(Start); }
        if (auto* Mesh = Cast<AStaticMeshActor>(*It); Mesh && Mesh->Tags.Contains(TEXT("demo_ground")))
        {
            ++FloorCount;
            Floor = Mesh->GetStaticMeshComponent();
            Floors.Add(Floor);
        }
        if (auto* Mesh = Cast<AStaticMeshActor>(*It); Mesh && Mesh->Tags.Contains(LDDemoWorld::TerrainTag))
        {
            Floors.Add(Mesh->GetStaticMeshComponent());
        }
    }
    TSharedPtr<const FLDDemoContentSnapshot> CandidateContent;
    TSharedPtr<const FLDDemoWorldGeometry> CandidateGeometry;
    if (AnchorCount != 1 || FloorCount != 1 || !CandidateDefinition || !Floor
        || CandidateDefinition->GetPathName() != FString(LDDemoEntry::WorldDefinition) + TEXT(".DA_DemoWorld")
        || Starts.Num() != 10)
    {
        Error = TEXT("Demo world requires its unique native definition, ground and ten player starts.");
        return false;
    }
    if (!LDDemoEntry::LoadRuntimeContent(CandidateContent, Error)
        || !CandidateDefinition->Read(*CandidateContent, CandidateGeometry, Error)) { return false; }
    LDDemoWorld::FTerrain CandidateTerrain;
    if (!CandidateTerrain.Build(*CandidateGeometry, Error)) { return false; }
    if (Floors.Num() != 1 + CandidateGeometry->Plateaus.Num() + CandidateGeometry->Ramps.Num())
    {
        Error = TEXT("Demo world requires one saved floor piece per registered plateau and ramp.");
        return false;
    }
    FCollisionShape Capsule;
    if (!ContentCapsule(*CandidateContent, Capsule, Error)) { return false; }
    const FLDWorldRevision CandidateRevision{FName(*CandidateGeometry->MapId),
        uint32(CandidateGeometry->LayoutRevision), uint32(CandidateGeometry->GeometryRevision)};
    TArray<FLDMapBinding> Sites;
    TSet<FName> SpawnIds;
    for (const auto& Anchor : CandidateGeometry->Anchors)
    {
        FLDMapBinding Binding;
        Binding.Id = FName(*Anchor.Id);
        Binding.Surface = FName(*Anchor.SurfaceId);
        if (!Anchor.PositionCm.ToVector(Binding.Position, Error)) { return false; }
        if (Anchor.Kind != ELDDemoWorldAnchorKind::Shop)
        {
            FLDLegalGround Legal;
            ELDWorldQueryFailure Failure;
            if (!Ground(*GetWorld(), *CandidateGeometry, CandidateTerrain, CandidateRevision, Floors, Capsule, Binding.Position,
                Capsule, Binding.Surface, Legal, Failure, Error)) { return false; }
            Binding.Floor = Legal.Floor;
        }
        else
        {
            // A shop anchor stands in its stall; its floor is the terrain piece under the stall.
            // The stall itself may answer the floor channel, so only the floor pieces are traced.
            const FVector Above = Binding.Position + FVector(0, 0, 50), Below = Binding.Position - FVector(0, 0, 50);
            for (UPrimitiveComponent* Piece : Floors)
            {
                FHitResult Hit;
                if (Piece->LineTraceComponent(Hit, Above, Below, FCollisionQueryParams(SCENE_QUERY_STAT(DemoShopFloor), false))
                    && FMath::Abs(Hit.ImpactPoint.Z - Binding.Position.Z) <= UE_KINDA_SMALL_NUMBER) { Binding.Floor = Piece; }
            }
            if (!Binding.Floor.IsValid())
            {
                Error = TEXT("Shop anchor has no registered floor beneath it: ") + Anchor.Id;
                return false;
            }
        }
        if (Anchor.Kind == ELDDemoWorldAnchorKind::Spawn)
        {
            const auto* Found = Starts.FindByPredicate([&](const APlayerStart* Start) { return Start->PlayerStartTag == Binding.Id; });
            if (!Found || SpawnIds.Contains(Binding.Id)
                || !(*Found)->GetActorLocation().Equals(Binding.Position + FVector(0, 0, Capsule.GetCapsuleHalfHeight()), UE_KINDA_SMALL_NUMBER))
            {
                Error = TEXT("Saved player start identity/position differs from the approved anchor: ") + Anchor.Id;
                return false;
            }
            SpawnIds.Add(Binding.Id);
        }
        Sites.Add(MoveTemp(Binding));
    }
    Definition = CandidateDefinition;
    Content = MoveTemp(CandidateContent);
    Geometry = MoveTemp(CandidateGeometry);
    GroundComponent = Floor;
    TerrainFloors.Reset();
    for (UPrimitiveComponent* Piece : Floors) { TerrainFloors.Add(Piece); }
    Terrain = MoveTemp(CandidateTerrain);
    WorldRevision = CandidateRevision;
    Generation = WorldRevision.GeometryRevision;
    Bindings = MoveTemp(Sites);
    Error.Reset();
    return true;
}
const FLDMapBinding* ULDMapRegistry::Resolve(FName Id,uint32 ExpectedGeneration) const
{
    if(!Enabled()||ExpectedGeneration!=Generation) { return nullptr; }
    return Bindings.FindByPredicate([&](const auto& B){return B.Id==Id&&B.Floor.IsValid();});
}
FString ULDMapRegistry::CanonicalBindings() const
{
    TArray<FString> Lines;
    for(const auto* Rows:{&Bindings,&ServiceBindings}) { for(const auto& B:*Rows)
    { Lines.Add(B.Id.ToString()+TEXT("|")+B.Position.ToString()+TEXT("|")+B.Surface.ToString()); } }
    Lines.Sort(); return FString::Join(Lines,TEXT("\n"));
}
TSharedRef<FJsonObject> ULDMapRegistry::Report() const
{
    auto R=MakeShared<FJsonObject>(); R->SetNumberField(TEXT("generation"),Generation);
    R->SetNumberField(TEXT("site_count"),Bindings.Num()); R->SetNumberField(TEXT("service_seats"),ServiceBindings.Num());
    R->SetBoolField(TEXT("ready"), Enabled());
    R->SetStringField(TEXT("payload_sha256"),Definition.IsValid()?Definition->Checksum:TEXT(""));
    R->SetStringField(TEXT("scope"),TEXT("Demo static world/ground bindings; no gameplay, observation or transaction authority"));
    TArray<TSharedPtr<FJsonValue>> Points,Services;
    for(const auto& B:Bindings)
    { auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"),B.Id.ToString()); J->SetStringField(TEXT("native_cm"),B.Position.ToString());
      J->SetStringField(TEXT("floor_proxy"),B.Surface.ToString()); J->SetBoolField(TEXT("floor_valid"),B.Floor.IsValid()); Points.Add(MakeShared<FJsonValueObject>(J)); }
    R->SetArrayField(TEXT("sites"),Points); R->SetArrayField(TEXT("services"),Services); return R;
}

bool ULDMapRegistry::QueryGround(const FLDWorldRevision& Expected, const FVector& FootCm, const FCollisionShape& Capsule,
    FName SurfaceHint, FLDLegalGround& Out, ELDWorldQueryFailure& Failure, FString& Error) const
{
    if (!Enabled() || !Content || !GroundComponent.IsValid())
    {
        return Reject(ELDWorldQueryFailure::MissingWorld, TEXT("MissingWorld: Demo ground bindings are unavailable."), Failure, Error);
    }
    if (Expected != WorldRevision)
    {
        return Reject(ELDWorldQueryFailure::StaleWorld, TEXT("StaleWorld: world geometry revision changed."), Failure, Error);
    }
    FCollisionShape Required;
    if (!ContentCapsule(*Content, Required, Error))
    {
        Failure = ELDWorldQueryFailure::InvalidCapsule;
        return false;
    }
    return Ground(*GetWorld(), *Geometry, Terrain, WorldRevision, Pieces(TerrainFloors), Required, FootCm, Capsule, SurfaceHint, Out, Failure, Error);
}

bool ULDMapRegistry::IsTerrainFloor(const UPrimitiveComponent* Floor) const
{
    return Floor && TerrainFloors.ContainsByPredicate([Floor](const auto& Piece) { return Piece.Get() == Floor; });
}

bool ULDMapRegistry::QueryCompletePath(const FLDWorldRevision& Expected, const FLDLegalGround& From, const FLDLegalGround& To,
    const FCollisionShape& Capsule, FLDCompletePath& Out, ELDWorldQueryFailure& Failure, FString& Error) const
{
    if (From.World != Expected || To.World != Expected)
    {
        return Reject(ELDWorldQueryFailure::StaleWorld, TEXT("StaleWorld: path endpoints belong to another world revision."), Failure, Error);
    }
    FLDLegalGround CurrentFrom, CurrentTo;
    if (!QueryGround(Expected, From.FootCm, Capsule, From.SurfaceId, CurrentFrom, Failure, Error)
        || !QueryGround(Expected, To.FootCm, Capsule, To.SurfaceId, CurrentTo, Failure, Error)) { return false; }
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    const ANavigationData* Data = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
    if (!Data || Data->GetConfig().AgentRadius != Capsule.GetCapsuleRadius()
        || Data->GetConfig().AgentHeight != Capsule.GetCapsuleHalfHeight() * 2)
    {
        return Reject(ELDWorldQueryFailure::MissingNavigation, TEXT("MissingNavigation: navigation data does not match the native capsule."), Failure, Error);
    }
    if (CurrentFrom.SurfaceCm.Equals(CurrentTo.SurfaceCm, UE_KINDA_SMALL_NUMBER))
    {
        FLDCompletePath Stationary;
        Stationary.World = WorldRevision;
        Stationary.Points.Add(CurrentFrom.SurfaceCm);
        Out = MoveTemp(Stationary);
        Failure = ELDWorldQueryFailure::None;
        Error.Reset();
        return true;
    }

    FPathFindingQuery Query(this, *Data, CurrentFrom.SurfaceCm, CurrentTo.SurfaceCm);
    Query.SetAllowPartialPaths(false);
    const FPathFindingResult Result = Nav->FindPathSync(Data->GetConfig(), Query);
    if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial() || Result.Path->GetPathPoints().Num() < 2)
    {
        return Reject(ELDWorldQueryFailure::IncompletePath, TEXT("IncompletePath: no complete native route between legal endpoints."), Failure, Error);
    }
    FLDCompletePath Candidate;
    Candidate.World = WorldRevision;
    for (const FNavPathPoint& Point : Result.Path->GetPathPoints()) { Candidate.Points.Add(Point.Location); }
    Candidate.StartProjectionCm = Candidate.Points[0] - CurrentFrom.SurfaceCm;
    Candidate.EndProjectionCm = Candidate.Points.Last() - CurrentTo.SurfaceCm;
    if (Candidate.StartProjectionCm.SizeSquared2D() > UE_KINDA_SMALL_NUMBER)
    {
        const FString Detail = FString::Printf(TEXT("Unreachable: navigation adjusted the route start's XY position; start=%s navigation=%s."),
            *CurrentFrom.SurfaceCm.ToString(), *Candidate.Points[0].ToString());
        return Reject(ELDWorldQueryFailure::Unreachable, *Detail, Failure, Error);
    }
    if (Candidate.EndProjectionCm.SizeSquared2D() > UE_KINDA_SMALL_NUMBER)
    {
        const FString Detail = FString::Printf(TEXT("Unreachable: navigation adjusted the destination's XY position; goal=%s navigation=%s."),
            *CurrentTo.SurfaceCm.ToString(), *Candidate.Points.Last().ToString());
        return Reject(ELDWorldQueryFailure::Unreachable, *Detail, Failure, Error);
    }
    Candidate.LengthCm = Result.Path->GetLength();
    if (!FMath::IsFinite(Candidate.LengthCm) || Candidate.LengthCm < 0)
    {
        return Reject(ELDWorldQueryFailure::IncompletePath, TEXT("IncompletePath: native path length is invalid."), Failure, Error);
    }
    Out = MoveTemp(Candidate);
    Failure = ELDWorldQueryFailure::None;
    Error.Reset();
    return true;
}

bool ULDMapRegistry::FindPublicApproach(const FLDWorldRevision& Expected, const FLDLegalGround& From, FName PublicAnchor,
    const FCollisionShape& Capsule, FLDLegalGround& OutGround, FLDCompletePath& OutPath,
    ELDWorldQueryFailure& Failure, FString& Error) const
{
    FLDLegalGround Origin;
    if (From.World != Expected)
    {
        return Reject(ELDWorldQueryFailure::StaleWorld, TEXT("StaleWorld: approach origin belongs to another world."), Failure, Error);
    }
    if (!QueryGround(Expected, From.FootCm, Capsule, From.SurfaceId, Origin, Failure, Error)) { return false; }
    const FString Id = PublicAnchor.ToString();
    const auto* Target = Geometry->Anchors.FindByPredicate([&](const auto& A) { return A.Id == Id; });
    if (!Target || (Target->Kind != ELDDemoWorldAnchorKind::Shop && Target->Kind != ELDDemoWorldAnchorKind::Capture))
    {
        return Reject(ELDWorldQueryFailure::Unreachable, TEXT("Unreachable: public approach requires the registered shop or capture anchor."), Failure, Error);
    }
    const auto& Quantity = Target->Kind == ELDDemoWorldAnchorKind::Shop
        ? Content->Get().Parameters.ShopInteractRadiusCm : Content->Get().Parameters.CaptureRadiusCm;
    const double Range = double(Quantity.Numerator) / double(Quantity.Denominator);
    FVector Centre;
    if (!Target->PositionCm.ToVector(Centre, Error))
    {
        Failure = ELDWorldQueryFailure::InvalidPosition;
        return false;
    }
    const FName Surface(*Target->SurfaceId);
    if (FVector::DistSquared2D(Origin.FootCm, Centre) <= Range * Range)
    {
        FLDCompletePath Stationary;
        if (!QueryCompletePath(Expected, Origin, Origin, Capsule, Stationary, Failure, Error)) { return false; }
        OutGround = Origin;
        OutPath = MoveTemp(Stationary);
        return true;
    }
    FLDLegalGround BestGround;
    FLDCompletePath BestPath;
    bool Found = false;
    const FVector Direct = (Origin.FootCm - Centre).GetSafeNormal2D();
    for (int32 I = -1; I < 64; ++I)
    {
        const double Angle = double(I) * 2 * UE_PI / 64;
        const FVector Direction = I < 0 ? Direct : FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0);
        FVector Candidate = Centre + Direction * Range;
        FVector Normal;
        Terrain.Ground(FVector2D(Candidate.X, Candidate.Y), Candidate.Z, Normal);
        if (FVector::DistSquared2D(Candidate, Centre) > Range * Range) { continue; }
        FLDLegalGround CandidateGround;
        FLDCompletePath Path;
        if (!QueryGround(Expected, Candidate, Capsule, Surface, CandidateGround, Failure, Error)) { continue; }
        if (!QueryCompletePath(Expected, Origin, CandidateGround, Capsule, Path, Failure, Error))
        {
            if (Failure == ELDWorldQueryFailure::MissingNavigation) { return false; }
            continue;
        }
        if (!Found || Path.LengthCm < BestPath.LengthCm)
        {
            BestGround = CandidateGround;
            BestPath = MoveTemp(Path);
            Found = true;
        }
    }
    if (!Found)
    {
        return Reject(ELDWorldQueryFailure::Unreachable, TEXT("Unreachable: no complete path reaches the public anchor's legal range."), Failure, Error);
    }
    OutGround = BestGround;
    OutPath = MoveTemp(BestPath);
    Failure = ELDWorldQueryFailure::None;
    Error.Reset();
    return true;
}

TArray<FBox> LDMapFitting::StructuralWalls(UWorld* W)
{
    TArray<FBox> Boxes;
    for(TActorIterator<AStaticMeshActor> It(W);It;++It)
    { if(It->Tags.ContainsByPredicate([](FName T){return T.ToString().StartsWith(TEXT("building_"));})) { Boxes.Add(It->GetComponentsBoundingBox(true)); } }
    return Boxes;
}
bool LDMapFitting::RoadOverlaps(const FTransform& T,const FBox& Wall,double Padding)
{
    const FBox B=Wall.ExpandBy(Padding); const FVector C=T.GetLocation();
    const FVector X=T.TransformVector(FVector(50,0,0)),Y=T.TransformVector(FVector(0,50,0)),Z=T.TransformVector(FVector(0,0,50));
    const double Height=FMath::Abs(X.Z)+FMath::Abs(Y.Z)+FMath::Abs(Z.Z);
    if(C.Z+Height<=B.Min.Z||C.Z-Height>=B.Max.Z) { return false; }
    const FVector2D Centre(C),Delta=Centre-FVector2D(B.GetCenter()),Extent(B.GetExtent());
    for(FVector2D Axis:{FVector2D(1,0),FVector2D(0,1),FVector2D(X).GetSafeNormal(),FVector2D(Y).GetSafeNormal()})
    {
        if(Axis.IsNearlyZero()) { continue; }
        const double Radius=FMath::Abs(FVector2D::DotProduct(FVector2D(X),Axis))+FMath::Abs(FVector2D::DotProduct(FVector2D(Y),Axis))+FMath::Abs(FVector2D::DotProduct(FVector2D(Z),Axis));
        const double WallRadius=Extent.X*FMath::Abs(Axis.X)+Extent.Y*FMath::Abs(Axis.Y);
        if(FMath::Abs(FVector2D::DotProduct(Delta,Axis))>=Radius+WallRadius-.001) { return false; }
    }
    return true;
}
