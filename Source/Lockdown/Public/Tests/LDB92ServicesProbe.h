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
    int32 WorkStep=0,WorkXp=0;
    int64 WorkGold=0,WorkBGold=0;
    int32 WorkBXp=0;
    double SurveyCooldown=0;
    TArray<FGuid> SavedItems;
    TArray<TSharedPtr<FJsonValue>> Checks;
    void Check(bool Good,const FString& Name);
    void Task(ALDB92Controller* PC,uint8 Task,FName POI);
    void Place(ALDB92Controller* PC,FVector Floor);
    void Save(bool Complete);
    void TickCourier();
    void SaveCourier(bool Complete);
    FName CourierOrigin,CourierTarget;
    FGuid CourierToken;
    int32 CourierXp=0;
    void TickCharter();
    void SaveCharter(bool Complete);
    void CharterStage(const FString& Label);
    TArray<TSharedPtr<FJsonValue>> CharterStages;
    FGuid EarnedWitness,EarnedCharter;
    int32 InputStep=0;
    uint32 FixtureHold=0;
};
