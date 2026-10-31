#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB92RoutinesProbe.generated.h"
class ALDB92Controller;
class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB92RoutinesProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB92RoutinesProbe();
    virtual void Tick(float Delta) override;
private:
    int32 Phase=0,Failures=0,Seconds=900,FleeBefore=0,AttackTrials=0;
    double Started=0,Wall=0,At=0,NextSnapshot=0,RootDistance=0;
    FVector RootPosition;
    FName Subject;
    TWeakObjectPtr<ALDB92Controller> Observer;
    TArray<TSharedPtr<FJsonValue>> Checks,Samples;
    void Check(bool Good,const FString& Name);
    bool PlaceObserver(FVector Floor);
    void Finish();
};
