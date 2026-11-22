#include "Region/LDMapRegistry.h"
#include "World/LDNativeGround.h"
#include "World/LDSceneTags.h"
#include "World/LDSpawnPlacement.h"
#include "Core/LDLog.h"
#include "Region/LDMapDefinition.h"
#include "Art/LDStaticScene.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Probes/LDDemoEntryProbe.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/LDPathSteering.h"
#include "NavigationData.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Brush.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "GameFramework/PlayerStart.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Misc/Crc.h"

namespace
{
struct FGroundFloor
{
    TWeakObjectPtr<UPrimitiveComponent> Component;
    FBox Bounds;
};

bool Reject(ELDWorldQueryFailure Code, const TCHAR* Detail, ELDWorldQueryFailure& Failure, FString& Error)
{
    Failure = Code;
    Error = Detail;
    return false;
}

bool HasActorChainTag(const UPrimitiveComponent& Part, FName Tag)
{
    if (Part.ComponentHasTag(Tag)) { return true; }
    for (const AActor* Actor = Part.GetOwner(); Actor; Actor = Actor->GetAttachParentActor())
    {
        if (Actor->ActorHasTag(Tag)) { return true; }
    }
    return false;
}

bool IsExplicitBlockerComponent(const UPrimitiveComponent& Part, FName PrimarySurfaceTag)
{
    static const FName IndependentRampTag(TEXT("LDIndependentRamp"));
    if (Part.ComponentHasTag(LDStaticScene::CollisionTag)) { return true; }
    for (const AActor* Actor = Part.GetOwner(); Actor; Actor = Actor->GetAttachParentActor())
    {
        if (LDStaticScene::Value(*Actor, TEXT("LDSceneZone:")).Equals(TEXT("blocker"), ESearchCase::IgnoreCase))
        {
            return true;
        }
        if (Actor->ActorHasTag(LDDemoWorld::StaticGeometryTag))
        {
            const bool bTerrain = (!PrimarySurfaceTag.IsNone() && Actor->ActorHasTag(PrimarySurfaceTag))
                || Actor->ActorHasTag(LDDemoWorld::TerrainTag)
                || Actor->ActorHasTag(IndependentRampTag);
            if (!bTerrain) { return true; }
        }
    }
    return false;
}

bool BoxFaceHasWalkableSlope(const FTransform& ElemWorld, const FVector& HalfExtent, double WalkableFloorZ)
{
    if (HalfExtent.X <= 0. || HalfExtent.Y <= 0. || HalfExtent.Z <= 0.) { return false; }
    const FVector Axes[3] = {
        ElemWorld.TransformVector(FVector(HalfExtent.X, 0., 0.)),
        ElemWorld.TransformVector(FVector(0., HalfExtent.Y, 0.)),
        ElemWorld.TransformVector(FVector(0., 0., HalfExtent.Z))
    };
    const double Rise = FMath::Abs(Axes[0].Z) + FMath::Abs(Axes[1].Z) + FMath::Abs(Axes[2].Z);
    if (Rise * 2. <= 4.) { return false; }
    for (int32 Face = 0; Face < 3; ++Face)
    {
        const FVector Raw = FVector::CrossProduct(Axes[(Face + 1) % 3], Axes[(Face + 2) % 3]);
        const FVector Normal = Raw.GetSafeNormal();
        for (const double Sign : {1., -1.})
        {
            const double NZ = Normal.Z * Sign;
            if (NZ >= WalkableFloorZ - 1e-4 && NZ < .9999) { return true; }
        }
    }
    return false;
}

bool HasWalkableSlopeFace(const UPrimitiveComponent& Part, double WalkableFloorZ)
{
    const FTransform& CompTransform = Part.GetComponentTransform();
    if (const auto* BoxComp = Cast<UBoxComponent>(&Part))
    {
        return BoxFaceHasWalkableSlope(CompTransform, BoxComp->GetUnscaledBoxExtent(), WalkableFloorZ);
    }
    const auto* MeshComp = Cast<UStaticMeshComponent>(&Part);
    const UBodySetup* Body = MeshComp && MeshComp->GetStaticMesh() ? MeshComp->GetStaticMesh()->GetBodySetup() : nullptr;
    if (!Body) { return false; }
    for (const FKBoxElem& Box : Body->AggGeom.BoxElems)
    {
        if (BoxFaceHasWalkableSlope(Box.GetTransform() * CompTransform, FVector(Box.X * .5, Box.Y * .5, Box.Z * .5), WalkableFloorZ))
        {
            return true;
        }
    }
    for (const FKConvexElem& Convex : Body->AggGeom.ConvexElems)
    {
        if (Convex.VertexData.Num() < 4 || Convex.VertexData.Num() > 64) { continue; }
        const FTransform ElemWorld = Convex.GetTransform() * CompTransform;
        TArray<FVector, TInlineAllocator<32>> Pts;
        FVector Inside = FVector::ZeroVector;
        for (const FVector& V : Convex.VertexData)
        {
            const FVector W = ElemWorld.TransformPosition(V);
            if (!Pts.ContainsByPredicate([&W](const FVector& E) { return FVector::DistSquared(E, W) <= .25; }))
            {
                Pts.Add(W);
                Inside += W;
            }
        }
        if (Pts.Num() < 4) { continue; }
        Inside /= double(Pts.Num());
        for (int32 I = 0; I < Pts.Num(); ++I)
        {
            for (int32 J = I + 1; J < Pts.Num(); ++J)
            {
                for (int32 K = J + 1; K < Pts.Num(); ++K)
                {
                    FVector N = FVector::CrossProduct(Pts[J] - Pts[I], Pts[K] - Pts[I]).GetSafeNormal();
                    if (N.IsNearlyZero()) { continue; }
                    if (FVector::DotProduct(N, Pts[I] - Inside) < 0.) { N = -N; }
                    if (N.Z < WalkableFloorZ - 1e-4 || N.Z >= .9999) { continue; }
                    bool bOuter = true;
                    double MinZ = Pts[I].Z, MaxZ = Pts[I].Z;
                    for (const FVector& P : Pts)
                    {
                        const double Signed = FVector::DotProduct(N, P - Pts[I]);
                        if (Signed > 1.5) { bOuter = false; break; }
                        if (Signed >= -1.5) { MinZ = FMath::Min(MinZ, P.Z); MaxZ = FMath::Max(MaxZ, P.Z); }
                    }
                    if (bOuter && MaxZ - MinZ > 4.) { return true; }
                }
            }
        }
    }
    return false;
}

// Legal standing ground at Foot (the capsule bottom) uses the current native support and capsule clearance.
// Walls and trees are tested with the whole capsule; other terrain pieces with the capsule lifted by the body's step
// height, so the foot of a ramp or a plateau's rim beside the floor piece is not taken for an obstacle while a cliff
// face still is.
bool Ground(UWorld& World, const FLDDemoWorldGeometry& Geometry, const LDDemoWorld::FTerrain& Terrain,
    const FLDWorldRevision& Revision, const TArray<FGroundFloor>& Floors, const FCollisionQueryParams& Query,
    double Step, const FCollisionShape& RequiredCapsule,
    const FVector& Foot, const FCollisionShape& Capsule, FName SurfaceHint, FLDLegalGround& Out,
    ELDWorldQueryFailure& Failure, FString& Error, bool bPrefilter = true)
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
    // A capsule at a ramp seam can rest on the slope beside its foot, even while the centre's terrain is flat.
    // Start above the steepest permitted slope's capsule lift; the bottom of the sweep stays one native gap below Foot.
    const double MinSupportNormalZ = GetDefault<ALDCharacter>()->GetCharacterMovement()->GetWalkableFloorZ();
    const double Lift = Radius * (1.0 / MinSupportNormalZ - 1.0);
    const double Gap = UCharacterMovementComponent::MAX_FLOOR_DIST + UE_KINDA_SMALL_NUMBER;
    FHitResult Hit;
    const bool HitFloor = Terrain.SweepScene(
        Foot + FVector(0, 0, Height + Lift + Gap), Foot + FVector(0, 0, Height - Gap),
        Capsule, Hit);
    if (Hit.bStartPenetrating)
    {
        const FString Detail = FString::Printf(TEXT("Blocked: native capsule starts inside static collision; foot=%s actor=%s."),
            *Foot.ToString(), *GetNameSafe(Hit.GetActor()));
        return Reject(ELDWorldQueryFailure::Blocked, *Detail, Failure, Error);
    }
    if (!HitFloor || Hit.bStartPenetrating || Hit.ImpactNormal.Z < MinSupportNormalZ - 1e-4)
    {
        const FString Detail = FString::Printf(TEXT("NotGrounded: no walkable local native support; foot=%s hit=%d penetrating=%d normalZ=%.6f hitActor=%s. No downward fallback."),
            *Foot.ToString(), HitFloor, Hit.bStartPenetrating, Hit.ImpactNormal.Z, *GetNameSafe(Hit.GetActor()));
        return Reject(ELDWorldQueryFailure::NotGrounded, *Detail, Failure, Error);
    }
    UPrimitiveComponent* Support = Hit.GetComponent();
    if (!Floors.ContainsByPredicate([Support](const FGroundFloor& Floor) { return Floor.Component.Get() == Support; }))
    {
        return Reject(ELDWorldQueryFailure::WrongSurface, TEXT("WrongSurface: the physical support differs from the supplied surface."), Failure, Error);
    }
    if (LDNativeGround::Overlaps(World, Foot + FVector(0, 0, Height), Capsule, Query))
    {
        return Reject(ELDWorldQueryFailure::Blocked, TEXT("Blocked: native capsule intersects static geometry."), Failure, Error);
    }
    const FVector Center = Foot + FVector(0, 0, Height + Step);
    const FVector Extent(Radius, Radius, Height);
    const FBox CapsuleBounds(Center - Extent, Center + Extent);
    for (const FGroundFloor& Piece : Floors)
    {
        UPrimitiveComponent* Floor = Piece.Component.Get();
        if (Floor && Floor != Support && (!bPrefilter || Piece.Bounds.Intersect(CapsuleBounds))
            && Floor->OverlapComponent(Center, FQuat::Identity, Capsule))
        {
            return Reject(ELDWorldQueryFailure::Blocked, TEXT("Blocked: native capsule meets a cliff or higher terrain."), Failure, Error);
        }
    }
    FLDLegalGround Candidate;
    Candidate.World = Revision;
    Candidate.FootCm = Foot;
    // The sweep proved support at this foot. A world-height ray would instead select an overhead mesh
    // (for example the flag at capture), giving navigation an endpoint on its roof rather than this floor.
    FHitResult CentreHit;
    const FVector Top = Foot + FVector(0, 0, Lift + Gap);
    const FVector Bottom = Foot - FVector(0, 0, Gap);
    const double SurfaceZ = Terrain.TraceScene(Top, Bottom, CentreHit)
        && CentreHit.ImpactNormal.Z >= MinSupportNormalZ - 1e-4
        ? CentreHit.ImpactPoint.Z
        : Hit.ImpactPoint.Z - (Hit.ImpactNormal.X * (Foot.X - Hit.ImpactPoint.X)
            + Hit.ImpactNormal.Y * (Foot.Y - Hit.ImpactPoint.Y)) / Hit.ImpactNormal.Z;
    Candidate.SurfaceCm = FVector(Foot.X, Foot.Y, SurfaceZ);
    Candidate.SupportOriginCm = Hit.Location;
    Candidate.SurfaceId = SurfaceHint;
    Candidate.Floor = Support;
    Out = MoveTemp(Candidate);
    Failure = ELDWorldQueryFailure::None;
    Error.Reset();
    return true;
}

}

struct ULDMapRegistry::FGroundState
{
    FCollisionShape Capsule;
    double Step = 0;
    TArray<FGroundFloor> Floors;
    FBox FloorBounds = FBox(ForceInit);
    FCollisionQueryParams Query{SCENE_QUERY_STAT(DemoGroundCapsule), false};
    FCollisionQueryParams FloorQuery{SCENE_QUERY_STAT(DemoWalkableFloors), false};
    struct FBodyResult
    {
        FLDCombatBodyRef Body;
        uint64 ResetEpoch = 0;
        FVector Foot;
        FName Surface;
        FLDLegalGround Ground;
        ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
        FString Error;
        bool bLegal = false;
    };
    FLDMatchContext Match;
    FCollisionShape BodyCapsule;
    TMap<FGuid, FBodyResult> Bodies;
#if WITH_DEV_AUTOMATION_TESTS
    uint64 Queries = 0;
#endif
};

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
        LDNativeGround::RetireLegacyCliffGuards(W);
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
    GroundState.Reset();
    WorldRevision = {}; Generation = 0; Bindings.Reset(); ServiceBindings.Reset();
    if (!GetWorld() || (GetWorld()->IsGameWorld()
        && UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap))
    {
        Error = TEXT("Map registry is available only for the Demo world or editor asset validation.");
        return false;
    }
    LDSceneTags::ApplyRuntimePolicy(*GetWorld());
    int32 AnchorCount = 0;
    const ULDMapDefinition* CandidateDefinition = nullptr;
    TArray<UPrimitiveComponent*> AllSupport;
    TArray<const APlayerStart*> Starts;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if (const auto* Anchor = Cast<ALDMapAnchor>(*It)) { ++AnchorCount; CandidateDefinition = Anchor->Definition; }
        if (const auto* Start = Cast<APlayerStart>(*It); Start && !Start->IsEditorOnly()) { Starts.Add(Start); }
        if (*It == GetWorld()->GetDefaultBrush()) { continue; }
        TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
        for (auto* Part : Parts)
        {
            if (LDNativeGround::IsSupport(*Part))
            {
                AllSupport.Add(Part);
            }
        }
    }
    TSharedPtr<const FLDDemoContentSnapshot> CandidateContent;
    TSharedPtr<const FLDDemoWorldGeometry> CandidateGeometry;
    if (AnchorCount != 1 || !CandidateDefinition || AllSupport.IsEmpty()
        || CandidateDefinition->GetPathName() != FString(LDDemoEntry::WorldDefinition) + TEXT(".DA_DemoWorld")
        || Starts.IsEmpty())
    {
        Error = TEXT("Demo world requires its unique native definition, walkable ground collision and player starts.");
        return false;
    }
    if (!LDDemoEntry::LoadRuntimeContent(CandidateContent, Error)
        || !CandidateDefinition->Read(*CandidateContent, CandidateGeometry, Error)) { return false; }
    auto Current = MakeShared<FLDDemoWorldGeometry>();
    if (!LDStaticScene::ProjectRuntimeGeometry(*GetWorld(), *CandidateGeometry, *Current, Error)) { return false; }
    CandidateGeometry = Current;
    LDDemoWorld::FTerrain CandidateTerrain;
    if (!CandidateTerrain.Build(*CandidateGeometry, Error)) { return false; }
    FCollisionShape Capsule;
    if (!ContentCapsule(*CandidateContent, Capsule, Error)) { return false; }
    auto CandidateGround = MakeShared<FGroundState>();
    CandidateGround->Capsule = Capsule;
    CandidateGround->Step = GetDefault<ALDCharacter>()->GetCharacterMovement()->MaxStepHeight;
    FBox NativeBounds(ForceInit);
    for (UPrimitiveComponent* Piece : AllSupport)
    {
        const FBodyInstance* Physics = Piece->GetBodyInstance();
        if (!Physics || !Physics->IsValidBodyInstance())
        {
            Error = TEXT("Demo terrain requires initialized native collision bodies.");
            return false;
        }
        NativeBounds += Physics->GetBodyBounds();
    }
    // Separate genuine walkable terrain/floor pieces from blockers, walls, trees and unreachable elevated rooftops.
    const FName PrimarySurfaceTag = !CandidateGeometry->Surfaces.IsEmpty() ? FName(*CandidateGeometry->Surfaces[0].Id) : NAME_None;
    static const FName IndependentRampTag(TEXT("LDIndependentRamp"));
    const double WalkableFloorZ = GetDefault<ALDCharacter>()->GetCharacterMovement()->GetWalkableFloorZ();
    TArray<UPrimitiveComponent*> NonBlockers;
    FCollisionQueryParams NonBlockerQuery = LDNativeGround::Query(*GetWorld());
    for (UPrimitiveComponent* Piece : AllSupport)
    {
        if (IsExplicitBlockerComponent(*Piece, PrimarySurfaceTag))
        {
            NonBlockerQuery.AddIgnoredComponent(Piece);
        }
        else
        {
            NonBlockers.Add(Piece);
        }
    }
    TSet<UPrimitiveComponent*> AdmittedSet;
    TArray<UPrimitiveComponent*> Floors;
    const auto AdmitFloor = [&](UPrimitiveComponent* Piece)
    {
        if (Piece && !AdmittedSet.Contains(Piece))
        {
            AdmittedSet.Add(Piece);
            Floors.Add(Piece);
        }
    };
    for (UPrimitiveComponent* Piece : NonBlockers)
    {
        if ((!PrimarySurfaceTag.IsNone() && HasActorChainTag(*Piece, PrimarySurfaceTag))
            || HasActorChainTag(*Piece, LDDemoWorld::TerrainTag)
            || HasActorChainTag(*Piece, IndependentRampTag))
        {
            AdmitFloor(Piece);
        }
    }
    const double HalfHeight = Capsule.GetCapsuleHalfHeight();
    const double Lift = Capsule.GetCapsuleRadius() * (1.0 / WalkableFloorZ - 1.0);
    const double Gap = UCharacterMovementComponent::MAX_FLOOR_DIST + UE_KINDA_SMALL_NUMBER;
    const auto AdmitPointSupport = [&](const FVector& Pos)
    {
        FHitResult LocalHit;
        if (LDNativeGround::Sweep(*GetWorld(), LocalHit,
            Pos + FVector(0., 0., HalfHeight + Lift + Gap),
            Pos + FVector(0., 0., HalfHeight - Gap),
            Capsule, NonBlockerQuery)
            && !LocalHit.bStartPenetrating
            && LocalHit.ImpactNormal.Z >= WalkableFloorZ - 1e-4
            && NonBlockers.Contains(LocalHit.GetComponent()))
        {
            AdmitFloor(LocalHit.GetComponent());
            return;
        }
        FHitResult TopHit;
        if (LDNativeGround::Trace(*GetWorld(), TopHit,
            FVector(Pos.X, Pos.Y, NativeBounds.Max.Z + 1.),
            FVector(Pos.X, Pos.Y, NativeBounds.Min.Z - 1.),
            NonBlockerQuery)
            && !TopHit.bStartPenetrating
            && TopHit.ImpactNormal.Z >= WalkableFloorZ - 1e-4
            && NonBlockers.Contains(TopHit.GetComponent()))
        {
            AdmitFloor(TopHit.GetComponent());
        }
    };
    TArray<FVector> ReferenceSeeds;
    for (const auto& Anchor : Current->Anchors)
    {
        FVector AnchorPos;
        if (!Anchor.PositionCm.ToVector(AnchorPos, Error)) { return false; }
        AdmitPointSupport(AnchorPos);
        if (Anchor.Kind == ELDDemoWorldAnchorKind::Capture
            || Anchor.Kind == ELDDemoWorldAnchorKind::RestService
            || Anchor.Kind == ELDDemoWorldAnchorKind::FinalCapture
            || (ReferenceSeeds.IsEmpty() && Anchor.Kind == ELDDemoWorldAnchorKind::Spawn))
        {
            ReferenceSeeds.Add(AnchorPos);
        }
        for (const auto& Member : Anchor.Members)
        {
            FVector MemberPos;
            if (Member.PositionCm.ToVector(MemberPos, Error)) { AdmitPointSupport(MemberPos); }
        }
    }
    if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
    {
        if (const ANavigationData* Data = Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
        {
            TArray<FVector> SeedNavOrigins;
            for (const FVector& SeedPos : ReferenceSeeds)
            {
                FVector Probe = SeedPos;
                FHitResult SeedHit;
                if (LDNativeGround::Trace(*GetWorld(), SeedHit,
                    FVector(SeedPos.X, SeedPos.Y, NativeBounds.Max.Z + 1.),
                    FVector(SeedPos.X, SeedPos.Y, NativeBounds.Min.Z - 1.),
                    NonBlockerQuery) && !SeedHit.bStartPenetrating)
                {
                    Probe = SeedHit.ImpactPoint;
                }
                FNavLocation SeedNav;
                if (Nav->ProjectPointToNavigation(Probe, SeedNav,
                    FVector(Capsule.GetCapsuleRadius() * 2., Capsule.GetCapsuleRadius() * 2., HalfHeight * 2.), Data))
                {
                    SeedNavOrigins.Add(SeedNav.Location);
                }
            }
            if (!SeedNavOrigins.IsEmpty())
            {
                const FCollisionQueryParams CompTraceParams(SCENE_QUERY_STAT(DemoFloorNavCheck), false);
                const double NavSnapZ = CandidateGround->Step + 25.;
                for (UPrimitiveComponent* Piece : NonBlockers)
                {
                    if (AdmittedSet.Contains(Piece)) { continue; }
                    const FBox CBounds = Piece->GetBodyInstance()->GetBodyBounds();
                    const FVector Size = CBounds.GetSize();
                    const FVector2D Samples[5] = {
                        FVector2D(CBounds.GetCenter()),
                        FVector2D(CBounds.Min.X + Size.X * .25, CBounds.Min.Y + Size.Y * .25),
                        FVector2D(CBounds.Min.X + Size.X * .75, CBounds.Min.Y + Size.Y * .25),
                        FVector2D(CBounds.Min.X + Size.X * .25, CBounds.Min.Y + Size.Y * .75),
                        FVector2D(CBounds.Min.X + Size.X * .75, CBounds.Min.Y + Size.Y * .75)
                    };
                    bool bNavReachable = false;
                    for (const FVector2D& SampleXY : Samples)
                    {
                        FHitResult PieceHit;
                        if (!Piece->LineTraceComponent(PieceHit,
                            FVector(SampleXY.X, SampleXY.Y, CBounds.Max.Z + 1.),
                            FVector(SampleXY.X, SampleXY.Y, CBounds.Min.Z - 1.),
                            CompTraceParams)
                            || PieceHit.bStartPenetrating
                            || PieceHit.ImpactNormal.Z < WalkableFloorZ - 1e-4)
                        {
                            continue;
                        }
                        FNavLocation PieceNav;
                        if (!Nav->ProjectPointToNavigation(PieceHit.ImpactPoint, PieceNav,
                            FVector(Capsule.GetCapsuleRadius(), Capsule.GetCapsuleRadius(), NavSnapZ), Data)
                            || FMath::Abs(PieceNav.Location.Z - PieceHit.ImpactPoint.Z) > NavSnapZ)
                        {
                            continue;
                        }
                        for (const FVector& Origin : SeedNavOrigins)
                        {
                            FPathFindingQuery RouteQuery(this, *Data, Origin, PieceNav.Location);
                            RouteQuery.SetAllowPartialPaths(false);
                            const FPathFindingResult Route = Nav->FindPathSync(Data->GetConfig(), RouteQuery);
                            if (Route.IsSuccessful() && Route.Path.IsValid() && !Route.Path->IsPartial())
                            {
                                bNavReachable = true;
                                break;
                            }
                        }
                        if (bNavReachable) { break; }
                    }
                    if (bNavReachable) { AdmitFloor(Piece); }
                }
            }
        }
    }
    if (Floors.IsEmpty() && !NonBlockers.IsEmpty())
    {
        double LowestTopZ = DBL_MAX;
        for (UPrimitiveComponent* Piece : NonBlockers)
        {
            LowestTopZ = FMath::Min(LowestTopZ, Piece->GetBodyInstance()->GetBodyBounds().Max.Z);
        }
        for (UPrimitiveComponent* Piece : NonBlockers)
        {
            if (Piece->GetBodyInstance()->GetBodyBounds().Max.Z <= LowestTopZ + CandidateGround->Step + 2.)
            {
                AdmitFloor(Piece);
            }
        }
    }
    const double XYReach = Capsule.GetCapsuleRadius() + 4.;
    bool bExpanded = true;
    while (bExpanded)
    {
        bExpanded = false;
        for (UPrimitiveComponent* Piece : NonBlockers)
        {
            if (AdmittedSet.Contains(Piece)) { continue; }
            const FBox CBounds = Piece->GetBodyInstance()->GetBodyBounds();
            const FBox2D CXY(FVector2D(CBounds.Min.X - XYReach, CBounds.Min.Y - XYReach),
                FVector2D(CBounds.Max.X + XYReach, CBounds.Max.Y + XYReach));
            for (UPrimitiveComponent* Existing : Floors)
            {
                const FBox FBounds = Existing->GetBodyInstance()->GetBodyBounds();
                if (!CXY.Intersect(FBox2D(FVector2D(FBounds.Min.X, FBounds.Min.Y), FVector2D(FBounds.Max.X, FBounds.Max.Y))))
                {
                    continue;
                }
                const bool bStepReachable = CBounds.Max.Z <= FBounds.Max.Z + CandidateGround->Step + 2.;
                const bool bSlopeReachable = !bStepReachable
                    && CBounds.Min.Z <= FBounds.Max.Z + CandidateGround->Step + 2.
                    && HasWalkableSlopeFace(*Piece, WalkableFloorZ);
                if (bStepReachable || bSlopeReachable)
                {
                    AdmitFloor(Piece);
                    bExpanded = true;
                    break;
                }
            }
        }
    }
    UPrimitiveComponent* Floor = Floors.IsEmpty() ? nullptr : Floors[0];
    if (!Floor)
    {
        Error = TEXT("Demo world requires its unique native definition, walkable ground collision and player starts.");
        return false;
    }
    CandidateGround->Floors.Reserve(Floors.Num());
    CandidateGround->FloorQuery = LDNativeGround::Query(*GetWorld());
    for (UPrimitiveComponent* Piece : AllSupport)
    {
        if (!AdmittedSet.Contains(Piece))
        {
            CandidateGround->FloorQuery.AddIgnoredComponent(Piece);
        }
    }
    for (UPrimitiveComponent* Piece : Floors)
    {
        const FBodyInstance* Physics = Piece->GetBodyInstance();
        // Use collision bounds, not render bounds; padding only admits extra narrow-phase candidates.
        CandidateGround->Floors.Add({Piece, Physics->GetBodyBounds().ExpandBy(UCharacterMovementComponent::MAX_FLOOR_DIST)});
        CandidateGround->FloorBounds += Physics->GetBodyBounds();
        CandidateGround->Query.AddIgnoredComponent(Piece);
    }
    CandidateTerrain.BindScene(*GetWorld(), NativeBounds);
    // World revisions must change with saved native collision, including ordinary untagged meshes.
    TArray<FString> SceneRows;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if (*It == GetWorld()->GetDefaultBrush()) { continue; }
        TArray<FString> Tags;
        for (FName Tag : It->Tags) { Tags.Add(Tag.ToString()); }
        Tags.Sort();
        const FString SemanticTags = FString::Join(Tags, TEXT("|"));
        TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
        for (auto* Part : Parts)
        {
            if (!Part->IsQueryCollisionEnabled() || Part->GetCollisionObjectType() != ECC_WorldStatic) { continue; }
            FString Shape;
            if (auto* Setup = Part->GetBodySetup())
            {
                FKAggregateGeom::StaticStruct()->ExportText(Shape, &Setup->AggGeom, nullptr, nullptr, PPF_None, nullptr);
                Shape += Setup->BodySetupGuid.ToString() + FString::FromInt(int32(Setup->CollisionTraceFlag));
            }
            const auto* Mesh = Cast<UStaticMeshComponent>(Part);
            const auto* Physics = Part->GetBodyInstance();
            const FBox Bounds = Physics && Physics->IsValidBodyInstance() ? Physics->GetBodyBounds() : Part->Bounds.GetBox();
            SceneRows.Add((Part->GetPathName(GetWorld()) + Part->GetClass()->GetPathName()
                + (Mesh ? GetPathNameSafe(Mesh->GetStaticMesh()) : FString()) + SemanticTags
                + FString::FromInt(LDSceneTags::NoCollision(*Part)) + FString::FromInt(LDSceneTags::NoViewBlock(*Part))
                + Shape + Bounds.ToString()
                + Part->GetComponentTransform().ToString() + FString::FromInt(int32(Part->GetCollisionEnabled()))
                + FString::FromInt(int32(Part->GetCollisionObjectType()))
                + FString::FromInt(Part->GetCollisionResponseToChannel(ECC_Pawn))
                + FString::FromInt(Part->GetCollisionResponseToChannel(ECC_GameTraceChannel1))
                + FString::FromInt(Part->GetCollisionResponseToChannel(ECC_GameTraceChannel2))).ToLower());
        }
    }
    SceneRows.Sort();
    for (const auto& Anchor : Current->Anchors)
    {
        FVector Position;
        if (!Anchor.PositionCm.ToVector(Position, Error)) { return false; }
        SceneRows.Add(Anchor.Id.ToLower() + TEXT("|") + Position.ToString());
    }
    Current->GeometryRevision = int32(FCrc::StrCrc32(*FString::Join(SceneRows, TEXT("\n"))) & MAX_int32);
    if (!Current->GeometryRevision) { Current->GeometryRevision = 1; }
    const FLDWorldRevision CandidateRevision{FName(*CandidateGeometry->MapId),
        uint32(CandidateGeometry->LayoutRevision), uint32(CandidateGeometry->GeometryRevision)};
    TArray<FLDMapBinding> Sites;
    TSet<FName> SpawnIds;
    for (auto& Anchor : Current->Anchors)
    {
        FLDMapBinding Binding;
        Binding.Id = FName(*Anchor.Id);
        Binding.Surface = FName(*Anchor.SurfaceId);
        if (!Anchor.PositionCm.ToVector(Binding.Position, Error)) { return false; }
        // Prefer the authored marker's local support: an overhead prop is not a reason to move a valid
        // ground-level site onto its roof. If terrain editing removed that support, reproject and validate it.
        FVector NativeNormal;
        FLDLegalGround Legal;
        ELDWorldQueryFailure Failure;
        const auto LocalGround = [&](const FVector& Foot, FLDLegalGround& Out)
        {
            return Ground(*GetWorld(), *CandidateGeometry, CandidateTerrain, CandidateRevision, CandidateGround->Floors,
                CandidateGround->Query, CandidateGround->Step, Capsule, Foot, Capsule, Binding.Surface, Out, Failure, Error);
        };
        if (!LocalGround(Binding.Position, Legal))
        {
            FHitResult FloorHit;
            if (LDNativeGround::Trace(*GetWorld(), FloorHit,
                FVector(Binding.Position.X, Binding.Position.Y, CandidateGround->FloorBounds.Max.Z + 1.),
                FVector(Binding.Position.X, Binding.Position.Y, CandidateGround->FloorBounds.Min.Z - 1.),
                CandidateGround->FloorQuery))
            {
                Binding.Position.Z = FloorHit.ImpactPoint.Z;
            }
            else
            {
                CandidateTerrain.Ground(FVector2D(Binding.Position), Binding.Position.Z, NativeNormal);
            }
            if (!FMath::IsFinite(Binding.Position.Z))
            { Error = TEXT("NotGrounded: no native ground under layout point ") + Anchor.Id; return false; }
            if (!LocalGround(Binding.Position, Legal))
            { Error = TEXT("Layout point ") + Anchor.Id + TEXT(": ") + Error; return false; }
        }
        Binding.Position.Z = Legal.SurfaceCm.Z;
        Binding.Floor = Legal.Floor;
        if (Anchor.Kind == ELDDemoWorldAnchorKind::Spawn)
        {
            const auto* Found = Starts.FindByPredicate([&](const APlayerStart* Start) { return Start->PlayerStartTag == Binding.Id; });
            if (!Found || SpawnIds.Contains(Binding.Id)
                || !FVector2D((*Found)->GetActorLocation()).Equals(FVector2D(Binding.Position), .001))
            {
                Error = TEXT("Saved player start does not match its projected field anchor: ") + Anchor.Id;
                return false;
            }
            SpawnIds.Add(Binding.Id);
        }
        Anchor.PositionCm.Z.Numerator = FMath::RoundToInt64(Binding.Position.Z * 1000.);
        Anchor.PositionCm.Z.Denominator = 1000;
        for (auto& Member : Anchor.Members)
        {
            FVector Position;
            if (!Member.PositionCm.ToVector(Position, Error)) { return false; }
            // Like other authored points, prefer the saved local floor beneath a roof/bridge.
            // A failed exact home can still use the ordinary local spawn offsets at runtime.
            FLDLegalGround MemberGround;
            if (LocalGround(Position,MemberGround)) { Position.Z=MemberGround.SurfaceCm.Z; }
            else
            {
                // Terrain may have been lowered beneath the saved marker. Search down from its old
                // level before trying a world-height ray, which could choose an unrelated roof above it.
                FHitResult Below;
                if (CandidateTerrain.TraceScene(Position+FVector(0,0,CandidateGround->Step),
                    FVector(Position.X,Position.Y,NativeBounds.Min.Z-1),Below)
                    && LocalGround(Below.ImpactPoint,MemberGround)) { Position.Z=MemberGround.SurfaceCm.Z; }
                else { CandidateTerrain.Ground(FVector2D(Position), Position.Z, NativeNormal); }
            }
            if (!FMath::IsFinite(Position.Z)) { Error = TEXT("NotGrounded: no ground under camp home ") + Member.Id; return false; }
            Member.PositionCm.Z.Numerator = FMath::RoundToInt64(Position.Z * 1000.);
            Member.PositionCm.Z.Denominator = 1000;
        }
        Sites.Add(MoveTemp(Binding));
    }
    if (SpawnIds.Num() != Starts.Num())
    {
        Error = TEXT("Every playable PlayerStart must bind to one field spawn anchor.");
        return false;
    }
    Definition = CandidateDefinition;
    Content = MoveTemp(CandidateContent);
    Geometry = MoveTemp(CandidateGeometry);
    GroundComponent = Floor;
    GroundState = MoveTemp(CandidateGround);
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
bool ULDMapRegistry::SharedSignupCentre(FVector& OutCentre) const
{
    FVector Sum=FVector::ZeroVector;
    FName Surface;
    for(int32 Slot=0;Slot<LDDemoWorld::PKSignups;++Slot)
    {
        const FLDMapBinding* Seat=Resolve(FName(*LDDemoWorld::PKSignupId(Slot)),Generation);
        if(!Seat||Seat->Surface.IsNone()||Seat->Position.ContainsNaN()
            ||(Slot>0&&Seat->Surface!=Surface)){return false;}
        Surface=Seat->Surface;
        Sum+=Seat->Position;
    }
    OutCentre=Sum/double(LDDemoWorld::PKSignups);
    return !OutCentre.ContainsNaN();
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
    TRACE_CPUPROFILER_EVENT_SCOPE(LDQueryGround);
    if (!Enabled() || !Content || !GroundComponent.IsValid() || !GroundState)
    {
        return Reject(ELDWorldQueryFailure::MissingWorld, TEXT("MissingWorld: Demo ground bindings are unavailable."), Failure, Error);
    }
    if (Expected != WorldRevision)
    {
        return Reject(ELDWorldQueryFailure::StaleWorld, TEXT("StaleWorld: world geometry revision changed."), Failure, Error);
    }
#if WITH_DEV_AUTOMATION_TESTS
    ++GroundState->Queries;
#endif
    return Ground(*GetWorld(), *Geometry, Terrain, WorldRevision, GroundState->Floors, GroundState->Query,
        GroundState->Step, GroundState->Capsule, FootCm, Capsule, SurfaceHint, Out, Failure, Error);
}

bool ULDMapRegistry::QueryBodyGround(const FLDMatchContext& Match, const FLDCombatBodyRef& Body, uint64 ResetEpoch,
    const FLDDemoContentSnapshot& BodyContent,
    const FVector& FootCm, FName SurfaceHint, FLDLegalGround& Out, ELDWorldQueryFailure& Failure, FString& Error) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(LDQueryBodyGround);
    if (!Enabled() || !GroundComponent.IsValid() || !GroundState)
    {
        return Reject(ELDWorldQueryFailure::MissingWorld, TEXT("MissingWorld: Demo ground bindings are unavailable."), Failure, Error);
    }
    auto& Cache = *GroundState;
    if (Cache.Match != Match)
    {
        Cache.Bodies.Reset();
        if (!ContentCapsule(BodyContent, Cache.BodyCapsule, Error))
        {
            Failure = ELDWorldQueryFailure::InvalidCapsule;
            return false;
        }
        Cache.Match = Match;
    }
    if (const auto* Previous = Cache.Bodies.Find(Body.Victim.EntityId);
        Previous && Previous->Body == Body && Previous->ResetEpoch == ResetEpoch
        && Previous->Foot == FootCm && Previous->Surface == SurfaceHint)
    {
        if (Previous->bLegal) { Out = Previous->Ground; }
        Failure = Previous->Failure;
        Error = Previous->Error;
        return Previous->bLegal;
    }
    // A bounded geometry cache cannot retain every retired body in a long-running world. The bound sits well above the
    // living bodies of one field (about 80 camp members, the Boss, eight contenders and their training dummies): a bound
    // below that count empties the cache every step and most standing bodies sweep the ground again.
    if (Cache.Bodies.Num() >= 256) { Cache.Bodies.Reset(); }
    FGroundState::FBodyResult Result;
    Result.Body = Body;
    Result.ResetEpoch = ResetEpoch;
    Result.Foot = FootCm;
    Result.Surface = SurfaceHint;
    Result.bLegal = QueryGround(WorldRevision, FootCm, Cache.BodyCapsule, SurfaceHint, Result.Ground, Result.Failure, Result.Error);
    if (Result.bLegal) { Out = Result.Ground; }
    Failure = Result.Failure;
    Error = Result.Error;
    const bool bLegal = Result.bLegal;
    Cache.Bodies.Add(Body.Victim.EntityId, MoveTemp(Result));
    return bLegal;
}

#if WITH_DEV_AUTOMATION_TESTS
uint64 ULDMapRegistry::GroundQueryCountForTests() const
{
    return GroundState ? GroundState->Queries : 0;
}

bool ULDMapRegistry::QueryGroundReference(const FVector& FootCm, const FCollisionShape& Capsule, FName SurfaceHint,
    FLDLegalGround& Out, ELDWorldQueryFailure& Failure, FString& Error) const
{
    if (!Enabled() || !GroundState)
    {
        return Reject(ELDWorldQueryFailure::MissingWorld, TEXT("MissingWorld: Demo ground bindings are unavailable."), Failure, Error);
    }
    return Ground(*GetWorld(), *Geometry, Terrain, WorldRevision, GroundState->Floors, GroundState->Query,
        GroundState->Step, GroundState->Capsule, FootCm, Capsule, SurfaceHint, Out, Failure, Error, false);
}
#endif

bool ULDMapRegistry::IsTerrainFloor(const UPrimitiveComponent* Floor) const
{
    return Floor && TerrainFloors.ContainsByPredicate([Floor](const auto& Piece) { return Piece.Get() == Floor; });
}

bool ULDMapRegistry::TraceGround(const FVector& Start, const FVector& End, FHitResult& Hit) const
{
    if (!Enabled() || !GroundState || !GetWorld()) { return false; }
    FCollisionQueryParams Params = GroundState->FloorQuery;
    for (int32 Pass = 0; Pass < 8; ++Pass)
    {
        if (!LDNativeGround::Trace(*GetWorld(), Hit, Start, End, Params)) { return false; }
        if (IsTerrainFloor(Hit.GetComponent())) { return true; }
        Params.AddIgnoredComponent(Hit.GetComponent());
    }
    return false;
}

bool ULDMapRegistry::ResolveWalkableGround(const FVector2D& Point, FVector& OutSurface, FVector* OutNormal) const
{
    if (!Enabled() || !GroundState || !Geometry || Geometry->Surfaces.IsEmpty()
        || !FMath::IsFinite(Point.X) || !FMath::IsFinite(Point.Y))
    {
        return false;
    }
    FHitResult Hit;
    if (GroundState->FloorBounds.IsValid && TraceGround(
        FVector(Point.X, Point.Y, GroundState->FloorBounds.Max.Z + 1.),
        FVector(Point.X, Point.Y, GroundState->FloorBounds.Min.Z - 1.), Hit))
    {
        OutSurface = Hit.ImpactPoint;
        if (OutNormal) { *OutNormal = Hit.ImpactNormal; }
        return true;
    }
    FVector Min, Max;
    FString Error;
    if (!Geometry->Surfaces[0].MinCm.ToVector(Min, Error) || !Geometry->Surfaces[0].MaxCm.ToVector(Max, Error))
    {
        return false;
    }
    if (Point.X < Min.X - 1. || Point.X > Max.X + 1. || Point.Y < Min.Y - 1. || Point.Y > Max.Y + 1.)
    {
        return false;
    }
    OutSurface = FVector(FMath::Clamp(Point.X, Min.X, Max.X), FMath::Clamp(Point.Y, Min.Y, Max.Y),
        GroundState->FloorBounds.IsValid ? GroundState->FloorBounds.Min.Z : Min.Z);
    if (OutNormal) { *OutNormal = FVector::UpVector; }
    return true;
}

double ULDMapRegistry::WalkableVerticalSpanCm() const
{
    return Enabled() && GroundState && GroundState->FloorBounds.IsValid
        ? FMath::Max(0., GroundState->FloorBounds.Max.Z - GroundState->FloorBounds.Min.Z)
        : 0.;
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

    // A legal foot can be a few centimetres below Recast's voxel surface. A 3D nearest-point query
    // can then push its XY onto an adjacent steep face. Preserve XY when the native mesh has a layer
    // directly above this same supported foot; the physical standing checks remain unchanged.
    const double StepHeight=GetDefault<ALDCharacter>()->GetCharacterMovement()->MaxStepHeight;
    FVector NavFrom=CurrentFrom.SurfaceCm,NavTo=CurrentTo.SurfaceCm;
    LDPathSteering::VerticalNavPoint(*Data,CurrentFrom.SurfaceCm,StepHeight,NavFrom);
    LDPathSteering::VerticalNavPoint(*Data,CurrentTo.SurfaceCm,StepHeight,NavTo);
    FPathFindingQuery Query(this, *Data, NavFrom, NavTo);
    Query.SetAllowPartialPaths(false);
    const FPathFindingResult Result = Nav->FindPathSync(Data->GetConfig(), Query);
    if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial() || Result.Path->GetPathPoints().Num() < 2)
    {
        const FString Detail = FString::Printf(TEXT("IncompletePath: no complete native route; start=%s end=%s result=%d partial=%d search_limit=%d."),
            *CurrentFrom.SurfaceCm.ToString(), *CurrentTo.SurfaceCm.ToString(), int32(Result.Result), Result.IsPartial(),
            Result.Path.IsValid() && Result.Path->DidSearchReachedLimit());
        return Reject(ELDWorldQueryFailure::IncompletePath, *Detail, Failure, Error);
    }
    FLDCompletePath Candidate;
    Candidate.World = WorldRevision;
    for (const FNavPathPoint& Point : Result.Path->GetPathPoints()) { Candidate.Points.Add(Point.Location); }
    Candidate.StartProjectionCm = Candidate.Points[0] - CurrentFrom.SurfaceCm;
    Candidate.EndProjectionCm = Candidate.Points.Last() - CurrentTo.SurfaceCm;
    const auto ConnectEndpoint=[&](const FLDLegalGround& Ground,const FVector& NavPoint)
    {
        // Recast voxelizes its border. A physically clear short approach is ordinary CMC movement, not a teleport.
        const double Distance=FVector::Dist2D(Ground.SurfaceCm,NavPoint);
        if (Distance>Capsule.GetCapsuleRadius()
            || FMath::Abs(NavPoint.Z-Ground.SurfaceCm.Z)>GetDefault<ALDCharacter>()->GetCharacterMovement()->MaxStepHeight) { return false; }
        FLDLegalGround Bridge=Ground;
        const auto* Movement=GetDefault<ALDCharacter>()->GetCharacterMovement();
        const FCollisionQueryParams ApproachQuery=LDNativeGround::Query(*GetWorld());
        const int32 Samples=FMath::Max(1,FMath::CeilToInt(Distance/(Capsule.GetCapsuleRadius()*.25)));
        for (int32 I=1; I<=Samples; ++I)
        {
            FVector Point=FMath::Lerp(Ground.FootCm,FVector(NavPoint.X,NavPoint.Y,Ground.FootCm.Z),double(I)/Samples);
            // Follow the local walking surface. Holding the first height while XY moves downhill
            // incorrectly asks QueryGround to stand in midair; its no-fall rejection remains correct.
            Point.Z=Bridge.FootCm.Z;
            FHitResult Support;
            if (!LDNativeGround::Trace(*GetWorld(),Support,Point+FVector(0,0,Movement->MaxStepHeight),
                Point-FVector(0,0,Movement->MaxStepHeight),ApproachQuery)
                ||Support.bStartPenetrating||Support.ImpactNormal.Z<Movement->GetWalkableFloorZ())
            {Error=TEXT("native approach has no walkable support within one step");return false;}
            if (!QueryGround(Expected,Support.ImpactPoint,Capsule,Ground.SurfaceId,Bridge,Failure,Error)) { return false; }
        }
        FHitResult Hit;
        const bool bClear=!LDNativeGround::Sweep(*GetWorld(),Hit,LDSpawnPlacement::StandingOrigin(Ground),
            LDSpawnPlacement::StandingOrigin(Bridge),Capsule,LDNativeGround::Query(*GetWorld()));
        if (!bClear)
        { Error=FString::Printf(TEXT("native approach sweep: actor=%s normal=%s penetrating=%d"),
            *GetNameSafe(Hit.GetActor()),*Hit.ImpactNormal.ToString(),Hit.bStartPenetrating); }
        return bClear;
    };
    if (Candidate.StartProjectionCm.SizeSquared2D() > UE_KINDA_SMALL_NUMBER)
    {
        if (ConnectEndpoint(CurrentFrom,Candidate.Points[0]))
        { Candidate.Points.Insert(CurrentFrom.SurfaceCm,0); }
        else
        {
        const FString Detail = FString::Printf(TEXT("Unreachable: navigation adjusted the route start's XY position; start=%s navigation=%s approach=%s."),
            *CurrentFrom.SurfaceCm.ToString(), *Candidate.Points[0].ToString(), *Error);
        return Reject(ELDWorldQueryFailure::Unreachable, *Detail, Failure, Error);
        }
    }
    if (Candidate.EndProjectionCm.SizeSquared2D() > UE_KINDA_SMALL_NUMBER)
    {
        if (ConnectEndpoint(CurrentTo,Candidate.Points.Last()))
        { Candidate.Points.Add(CurrentTo.SurfaceCm); }
        else
        {
        const FString Detail = FString::Printf(TEXT("Unreachable: navigation adjusted the destination's XY position; goal=%s navigation=%s approach=%s."),
            *CurrentTo.SurfaceCm.ToString(), *Candidate.Points.Last().ToString(), *Error);
        return Reject(ELDWorldQueryFailure::Unreachable, *Detail, Failure, Error);
        }
    }
    Candidate.LengthCm = 0;
    for (int32 I=1; I<Candidate.Points.Num(); ++I) { Candidate.LengthCm+=FVector::Dist(Candidate.Points[I-1],Candidate.Points[I]); }
    if (!FMath::IsFinite(Candidate.LengthCm) || Candidate.LengthCm < 0)
    {
        return Reject(ELDWorldQueryFailure::IncompletePath, TEXT("IncompletePath: native path length is invalid."), Failure, Error);
    }
    Out = MoveTemp(Candidate);
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
