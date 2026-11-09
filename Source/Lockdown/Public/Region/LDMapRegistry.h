#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CollisionShape.h"
#include "World/LDWorldQueryTypes.h"
#include "Region/LDMapGeometry.h"
#include "LDMapRegistry.generated.h"
class ULDMapDefinition;
class UPrimitiveComponent;
class FJsonObject;
class FLDDemoContentSnapshot;
struct FLDDemoWorldGeometry;
struct FLDMatchContext;
struct FLDSpawnRequestKey;
class FLDGameplayWritePermit;
class FLDSpawnSelection;
class FLDSpawnReservation;

struct FLDMapBinding
{
    FName Id,Surface;
    FVector Position=FVector::ZeroVector;
    TWeakObjectPtr<UPrimitiveComponent> Floor;
    TArray<FVector> HubPath;
};

UCLASS()
class LOCKDOWN_API ULDMapRegistry : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void OnWorldBeginPlay(UWorld& World) override;
    bool Enabled() const { return Definition.IsValid() && Geometry.IsValid() && WorldRevision.IsValid(); }
    uint32 Generation=0;
    bool Rebuild(FString& Error);
    bool Ensure(FString& Error);
    const FLDMapBinding* Resolve(FName Id,uint32 ExpectedGeneration) const;
    const TArray<FLDMapBinding>& Sites() const { return Bindings; }
    const TArray<FLDMapBinding>& ServiceSeats() const { return ServiceBindings; }
    FString CanonicalBindings() const;
    TSharedRef<FJsonObject> Report() const;
    const FLDWorldRevision& Revision() const { return WorldRevision; }
    const FLDDemoWorldGeometry* GetGeometry() const { return Geometry.Get(); }
    // #97: plateaus, ramps and the ground they rise from; one registered surface made of several floor pieces.
    const LDDemoWorld::FTerrain& GetTerrain() const { return Terrain; }
    // Whether a native floor is one of the registered terrain pieces (ground, plateau or ramp).
    bool IsTerrainFloor(const UPrimitiveComponent* Floor) const;
    static bool ContentCapsule(const FLDDemoContentSnapshot& Content, FCollisionShape& Out, FString& Error);
    bool QueryGround(const FLDWorldRevision& Expected, const FVector& FootCm, const FCollisionShape& Capsule,
        FName SurfaceHint, FLDLegalGround& Out, ELDWorldQueryFailure& Failure, FString& Error) const;
    // The caller resolves a living body first; this cache is geometry only, not a body/life authority.
    bool QueryBodyGround(const FLDMatchContext& Match, const FLDCombatBodyRef& Body, uint64 ResetEpoch,
        const FLDDemoContentSnapshot& BodyContent,
        const FVector& FootCm, FName SurfaceHint, FLDLegalGround& Out, ELDWorldQueryFailure& Failure, FString& Error) const;
#if WITH_DEV_AUTOMATION_TESTS
    bool QueryGroundReference(const FVector& FootCm, const FCollisionShape& Capsule, FName SurfaceHint,
        FLDLegalGround& Out, ELDWorldQueryFailure& Failure, FString& Error) const;
    uint64 GroundQueryCountForTests() const;
#endif
    bool QueryCompletePath(const FLDWorldRevision& Expected, const FLDLegalGround& From, const FLDLegalGround& To,
        const FCollisionShape& Capsule, FLDCompletePath& Out, ELDWorldQueryFailure& Failure, FString& Error) const;
    bool BeginSpawnMatch(const FLDMatchContext& Match, FString& Error);
    bool DrawSpawnAnchor(const FLDSpawnRequestKey& Request, FRandomStream& Random,
        const FLDGameplayWritePermit& Permit, TSharedPtr<const FLDSpawnSelection>& Out, FString& Error);
    bool ReserveSpawnPlacement(const TSharedPtr<const FLDSpawnSelection>& Selection, const FCollisionShape& Capsule,
        const FLDGameplayWritePermit& Permit, TSharedPtr<const FLDSpawnReservation>& Out, FString& Error);
    bool ValidateSpawnReservation(const TSharedPtr<const FLDSpawnReservation>& Reservation,
        const FLDGameplayWritePermit& Permit, const AActor* PreparedBody, FString& Error) const;
    bool ReleaseSpawnReservation(TSharedPtr<const FLDSpawnReservation>& Reservation, FString& Error);
    bool FinalizeSpawnPlacement(TSharedPtr<const FLDSpawnReservation>& Reservation,
        const FLDGameplayWritePermit& Permit, FString& Error);
private:
    struct FSpawnState;
    TSharedPtr<FSpawnState> SpawnState;
    struct FGroundState;
    TSharedPtr<FGroundState> GroundState;
    TWeakObjectPtr<const ULDMapDefinition> Definition;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TSharedPtr<const FLDDemoWorldGeometry> Geometry;
    FLDWorldRevision WorldRevision;
    TWeakObjectPtr<UPrimitiveComponent> GroundComponent;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> TerrainFloors;
    LDDemoWorld::FTerrain Terrain;
    TArray<FLDMapBinding> Bindings,ServiceBindings;
};

namespace LDMapFitting
{
    LOCKDOWN_API TArray<FBox> StructuralWalls(UWorld* World);
    LOCKDOWN_API bool RoadOverlaps(const FTransform& Tile,const FBox& Wall,double Padding=0);
}
