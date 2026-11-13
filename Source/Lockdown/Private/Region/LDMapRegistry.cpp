#include "Region/LDMapRegistry.h"
#include "World/LDNativeGround.h"
#include "Core/LDLog.h"
#include "Region/LDMapDefinition.h"
#include "Art/LDStaticScene.h"
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
#include "Engine/StaticMesh.h"
#include "Engine/Brush.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "GameFramework/PlayerStart.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "PhysicsEngine/BodyInstance.h"
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
    double SurfaceZ; FVector SurfaceNormal;
    Terrain.Ground(FVector2D(Foot), SurfaceZ, SurfaceNormal);
    Candidate.SurfaceCm = FVector(Foot.X, Foot.Y, FMath::IsFinite(SurfaceZ) ? SurfaceZ : Hit.ImpactPoint.Z);
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
    FCollisionQueryParams Query{SCENE_QUERY_STAT(DemoGroundCapsule), false};
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
    int32 AnchorCount = 0;
    const ULDMapDefinition* CandidateDefinition = nullptr;
    UPrimitiveComponent* Floor = nullptr;
    TArray<UPrimitiveComponent*> Floors;
    TArray<const APlayerStart*> Starts;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if (const auto* Anchor = Cast<ALDMapAnchor>(*It)) { ++AnchorCount; CandidateDefinition = Anchor->Definition; }
        if (const auto* Start = Cast<APlayerStart>(*It)) { Starts.Add(Start); }
        if (*It == GetWorld()->GetDefaultBrush()) { continue; }
        TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
        for (auto* Part : Parts)
        {
            if (LDNativeGround::IsSupport(*Part))
            {
                Floors.Add(Part);
                if (!Floor) { Floor = Part; }
            }
        }
    }
    TSharedPtr<const FLDDemoContentSnapshot> CandidateContent;
    TSharedPtr<const FLDDemoWorldGeometry> CandidateGeometry;
    if (AnchorCount != 1 || !CandidateDefinition || !Floor
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
    CandidateGround->Floors.Reserve(Floors.Num());
    FBox NativeBounds(ForceInit);
    for (UPrimitiveComponent* Piece : Floors)
    {
        const FBodyInstance* Physics = Piece->GetBodyInstance();
        if (!Physics || !Physics->IsValidBodyInstance())
        {
            Error = TEXT("Demo terrain requires initialized native collision bodies.");
            return false;
        }
        // Use collision bounds, not render bounds; padding only admits extra narrow-phase candidates.
        CandidateGround->Floors.Add({Piece, Physics->GetBodyBounds().ExpandBy(UCharacterMovementComponent::MAX_FLOOR_DIST)});
        NativeBounds += Physics->GetBodyBounds();
        CandidateGround->Query.AddIgnoredComponent(Piece);
    }
    CandidateTerrain.BindScene(*GetWorld(), NativeBounds);
    // World revisions must change with saved native collision, including ordinary untagged meshes.
    TArray<FString> SceneRows;
    for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
    {
        TInlineComponentArray<UStaticMeshComponent*> Parts(*It);
        for (const auto* Part : Parts)
        {
            if (!Part->IsQueryCollisionEnabled()) { continue; }
            SceneRows.Add((It->GetName() + Part->GetName() + GetPathNameSafe(Part->GetStaticMesh())
                + Part->GetComponentTransform().ToString() + FString::FromInt(int32(Part->GetCollisionEnabled()))
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
        // Layout markers keep their XY; terrain edits move their runtime standing height without writing the map.
        FVector NativeNormal;
        CandidateTerrain.Ground(FVector2D(Binding.Position), Binding.Position.Z, NativeNormal);
        if (!FMath::IsFinite(Binding.Position.Z))
        { Error = TEXT("NotGrounded: no native ground under layout point ") + Anchor.Id; return false; }
        FLDLegalGround Legal;
        ELDWorldQueryFailure Failure;
        if (!Ground(*GetWorld(), *CandidateGeometry, CandidateTerrain, CandidateRevision, CandidateGround->Floors,
            CandidateGround->Query, CandidateGround->Step, Capsule, Binding.Position,
            Capsule, Binding.Surface, Legal, Failure, Error))
        { Error = TEXT("Layout point ") + Anchor.Id + TEXT(": ") + Error; return false; }
        Binding.Floor = Legal.Floor;
        if (Anchor.Kind == ELDDemoWorldAnchorKind::Spawn)
        {
            const auto* Found = Starts.FindByPredicate([&](const APlayerStart* Start) { return Start->PlayerStartTag == Binding.Id; });
            if (!Found || SpawnIds.Contains(Binding.Id)
                || !FVector2D((*Found)->GetActorLocation()).Equals(FVector2D(Binding.Position), UE_KINDA_SMALL_NUMBER))
            {
                Error = TEXT("Saved player start identity/position differs from the approved anchor: ") + Anchor.Id;
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
            CandidateTerrain.Ground(FVector2D(Position), Position.Z, NativeNormal);
            if (!FMath::IsFinite(Position.Z)) { Error = TEXT("NotGrounded: no ground under camp home ") + Member.Id; return false; }
            Member.PositionCm.Z.Numerator = FMath::RoundToInt64(Position.Z * 1000.);
            Member.PositionCm.Z.Denominator = 1000;
        }
        Sites.Add(MoveTemp(Binding));
    }
    if (SpawnIds.Num() != Starts.Num())
    {
        Error = TEXT("Every saved player start must be one approved spawn anchor.");
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
    // A bounded geometry cache cannot retain every retired body in a long-running world.
    if (Cache.Bodies.Num() >= 64) { Cache.Bodies.Reset(); }
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
    return Enabled() && Terrain.TraceScene(Start, End, Hit);
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
