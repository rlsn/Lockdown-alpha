#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB92ServicesProbe.generated.h"
class ALDB92Controller;
class ALDWorldObject;
class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB92ServicesProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB92ServicesProbe();
    virtual void Tick(float Delta) override;
private:
    int32 Phase=0,Failures=0,City=1;
    double At=0,Started=0;
    TWeakObjectPtr<ALDB92Controller> A,B,Replacement;
    TWeakObjectPtr<class ALDCharacter> OriginalBody;
    TWeakObjectPtr<ALDWorldObject> Tree;
    FGuid Seat,BodyLife;
    int64 SavedGold=0;
    int32 DepositXp=0;
    double SurveyCooldown=0;
    TArray<FGuid> SavedItems;
    TArray<TSharedPtr<FJsonValue>> Checks;
    void Check(bool Good,const FString& Name);
    void Task(ALDB92Controller* PC,uint8 Task,FName POI);
    void Place(ALDB92Controller* PC,FVector Floor);
    void Save(bool Complete);
};
