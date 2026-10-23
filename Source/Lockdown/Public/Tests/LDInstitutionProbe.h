#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "LDInstitutionProbe.generated.h"
UCLASS()
class LOCKDOWN_API ALDInstitutionProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDInstitutionProbe();
    virtual void Tick(float Delta) override;
private:
    void Authority();void Geography(float Delta);void Check(bool Good,const FString& Message);void Finish(const FString& Scope);
    bool Started=false,Done=false;
    int32 Failures=0,RouteIndex=-1,Waypoint=0;
    TArray<TSharedPtr<FJsonValue>> Checks,Travel;
    double Began=0,Distance=0,Exposed=0,Sampled=0;
    FVector Previous;
    TArray<int32> RouteIds;
};
