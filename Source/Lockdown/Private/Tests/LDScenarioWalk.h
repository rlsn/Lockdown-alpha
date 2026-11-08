#pragma once

#include "CoreMinimal.h"
#include "World/LDWorldQueryTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
class ULDMapRegistry;
class FLDDemoContentSnapshot;
struct FLDDemoWorldGeometry;

namespace LDScenarioWalk
{
struct FKeepout { FVector Centre; double Radius = 0; };
using FGroundQuery = TFunctionRef<bool(const FVector&, FLDLegalGround&, FString&)>;
using FPathQuery = TFunctionRef<bool(const FLDLegalGround&, const FLDLegalGround&, FLDCompletePath&, FString&)>;
enum class EPlan { Working, Complete, Failed };
class FPlanner
{
public:
    void Begin(const FLDLegalGround& From, const FLDLegalGround& Goal, const TArray<FKeepout>& Keepouts, double Arrival);
    EPlan Step(FGroundQuery Ground, FPathQuery Path, TArray<FVector>& Out, FString& Error, double BudgetSeconds = .005);
private:
    struct FVisit
    {
        FLDLegalGround Ground;
        TArray<FVector> Waypoints;
        double Cost = 0, Estimate = 0;
        bool Expanded = false;
    };
    TArray<FVisit> Visits;
    FVisit Current;
    FLDLegalGround Goal;
    TArray<FKeepout> Zones;
    TArray<int32> PendingZones;
    TArray<FVector> ClearTail;
    double Arrival = 0, ClearCost = DBL_MAX;
    int32 Expanded = 0, Pending = 0, Candidate = 0;
    bool bExpanding = false;
};

bool Keepouts(const FLDDemoWorldGeometry& Geometry, const FLDDemoContentSnapshot& Content,
    double BodyRadius, double Arrival, TArray<FKeepout>& Out, FString& Error);
void PatrolKeepouts(const TArray<FLDCompletePath>& Paths, double Radius, TArray<FKeepout>& Out);
bool PatrolPaths(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content,
    TArray<FLDCompletePath>& Out, FString& Error);
int32 RouteKeepout(const TArray<FVector>& Path, const TArray<FKeepout>& Keepouts);
bool Plan(const FLDLegalGround& Start, const FLDLegalGround& Goal, const TArray<FKeepout>& Keepouts,
    double Arrival, FGroundQuery Ground, FPathQuery Path, TArray<FVector>& Waypoints, FString& Error);
bool Plan(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content, const FVector& Foot,
    const FVector& Goal, const TArray<FKeepout>& Keepouts, double Arrival, TArray<FVector>& Waypoints, FString& Error);
bool Begin(FPlanner& Planner, const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content,
    const FVector& Foot, const FVector& Goal, const TArray<FKeepout>& Keepouts, double Arrival, FString& Error,
    double* MaxQuerySeconds = nullptr, int32* QueryCount = nullptr);
EPlan Step(FPlanner& Planner, const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content,
    TArray<FVector>& Waypoints, FString& Error, double* MaxQuerySeconds = nullptr, int32* QueryCount = nullptr);
bool VisionGoals(const FVector& Capture, double BodyRadius, double Arrival, double SightRadius,
    TArray<FVector>& Out, FString& Error);
bool VisionTargets(const FVector& Capture, double BodyRadius, double Arrival, double SightRadius,
    const TArray<FKeepout>& Keepouts, TFunctionRef<bool(TArray<FVector>&)> Validate,
    TArray<FVector>& Out, FString& Error);
}
#endif
