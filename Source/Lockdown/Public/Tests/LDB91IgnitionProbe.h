#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB91IgnitionProbe.generated.h"
class ALDB91Controller;
class ALDWorldObjects;
class ALDWorldObject;
class ALDItem;
class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB91IgnitionProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB91IgnitionProbe();
    virtual void Tick(float Delta) override;
private:
    ALDB91Controller* A=nullptr;
    ALDB91Controller* B=nullptr;
    ALDB91Controller* Winner=nullptr;
    ALDWorldObjects* Objects=nullptr;
    ALDWorldObject* Tree=nullptr;
    ALDWorldObject* NextTree=nullptr;
    TWeakObjectPtr<ALDItem> TorchA,TorchB,Oil;
    uint8 Phase=0;
    int32 Failures=0;
    double Started=0,At=0,ReturnAt=0;
    int64 LifeCooldown=0;
    bool SawHold=false,CapRejected=false,FilledLease=false;
    FVector Aim=FVector(80000,-80000,400);
    TArray<TSharedPtr<FJsonValue>> Checks;
    void Check(bool Good,const FString& Text);
    void Place(ALDB91Controller* PC,FVector Point);
    void Task(uint8 Value);
    void Save(bool Complete);
    bool Fill(int32 Target,int64 Stamp,int64 Duration);
};
