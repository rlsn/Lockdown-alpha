#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "GameFramework/Actor.h"
#include "LDB92Routines.generated.h"
class ALDB92Roster;
class ALDCharacter;

// Server-only schedule state. It is never a client role/identity payload.
struct FLDB92Routine
{
    FName Source,From,To;
    FGuid Identity;
    TWeakObjectPtr<ALDCharacter> Body;
    TWeakObjectPtr<AAIController> Controller;
    bool Traveler=false,Returning=false,Dwelling=false,Fleeing=false,Dead=false;
    int32 Phase=0,Waypoint=0,Arrivals=0,Departures=0,Transitions=0,Trips=0,Flees=0,Resumes=0,Failures=0;
    double Remaining=0,Until=0,LastDanger=-1000,NextSense=0,RetryAt=0,Distance=0,MaxSpeed=0;
    FVector Previous=FVector::ZeroVector,Destination=FVector::ZeroVector,Post=FVector::ZeroVector;
    TArray<FVector> Activities,Route;
    TArray<double> Durations;
    FString Mode=TEXT("travel");
};
UCLASS()
class LOCKDOWN_API ALDB92RoutineController:public AAIController
{
    GENERATED_BODY()
public:
    ALDB92RoutineController();
};
UCLASS()
class LOCKDOWN_API ALDB92Routines:public AActor
{
    GENERATED_BODY()
public:
    ALDB92Routines();
    bool Initialize(ALDB92Roster* Roster,FString& Error);
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool SealedCargo(const ALDCharacter* Body) const;
    bool RoleDied(ALDCharacter* Body);
    const TArray<FLDB92Routine>& States() const {return Routines;}
    FString ExportJson() const;
    bool Audit(FString& Error) const;
    static bool StrategicRoute(const UWorld* World,FName From,FName To,TArray<FVector>& Points);
    uint64 PathQueries=0,PerceptionRays=0;
    double PathMilliseconds=0,MaxPathMilliseconds=0;
private:
    TWeakObjectPtr<ALDB92Roster> OwnerRoster;
    TArray<FLDB92Routine> Routines;
    double NextDangerSample=0;
    TArray<TWeakObjectPtr<ALDCharacter>> Danger;
    bool Ready=false;
    bool NativeGoal(FVector Requested,FVector& Goal) const;
    bool StartMove(FLDB92Routine& State,FVector Goal,double Now);
    void PlanActivity(FLDB92Routine& State,double Now);
    void Sense(FLDB92Routine& State,double Now);
};
