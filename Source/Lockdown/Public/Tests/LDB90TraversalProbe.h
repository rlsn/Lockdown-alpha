#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB90TraversalProbe.generated.h"

class ALDCharacter;
class ALDB90PreviewController;
class ULDMapDefinition;
class FJsonObject;
class FJsonValue;

// Standalone spatial fixture. Never instantiated by a competitive rules profile.
UCLASS()
class LOCKDOWN_API ALDB90TraversalProbe : public AActor
{
    GENERATED_BODY()
public:
    ALDB90TraversalProbe();
    virtual void Tick(float DeltaSeconds) override;
private:
    struct FTrip { FName From, To; FString Kind; };
    struct FBoundary { FString Id; FVector Position, Direction; };
    UPROPERTY() TObjectPtr<ALDCharacter> Body;
    UPROPERTY() TObjectPtr<ALDB90PreviewController> Controller;
    const ULDMapDefinition* Definition=nullptr; // Owned by the persistent map anchor.
    TArray<FTrip> Trips;
    TArray<FBoundary> Boundaries;
    TArray<TSharedPtr<FJsonValue>> Results, BoundaryResults;
    TSharedPtr<FJsonObject> BoundaryResult;
    TSharedPtr<FJsonObject> NavigationContract;
    FString Output, Lane, Filter, Trace, BoundaryTrace;
    int32 Phase=0, TripIndex=0, BoundaryIndex=0, Failures=0, Samples=0, WaterSamples=0;
    uint32 StartReplans=0;
    double PhaseAt=0, WalkAt=0, Length=0, PlannedLength=0, PeakSpeed=0, FallingSeconds=0;
    double EmptySeconds=0, MaxEmptySeconds=0, EmptyDistance=0, NextSample=0, WallStart=0;
    double MinTick=DBL_MAX, MaxTick=0;
    FVector Last=FVector::ZeroVector, Start=FVector::ZeroVector, EmptyStart=FVector::ZeroVector, LongestEmptyStart=FVector::ZeroVector, LongestEmptyEnd=FVector::ZeroVector;
    bool BoundarySafe=true, Finished=false;
    bool Initialize();
    bool Place(const FVector& Point, bool Project);
    void BeginTrip();
    void EndTrip(bool Passed, const FString& Reason);
    void SampleTravel(float DeltaSeconds, bool Force=false);
    void BeginBoundary();
    bool SafeEndpoint() const;
    double SweptPush(const FVector& Direction, double Distance);
    void RecordBoundary(const TCHAR* Action);
    void Export(bool Complete);
    void Finish();
};
