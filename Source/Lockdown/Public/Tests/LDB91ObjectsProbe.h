#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB91ObjectsProbe.generated.h"
class ALDB91Controller;
class ALDWorldObjects;
class ALDWorldObject;
class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB91ObjectsProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB91ObjectsProbe();
    virtual void Tick(float Delta) override;
private:
    ALDB91Controller* A=nullptr;
    ALDB91Controller* B=nullptr;
    ALDB91Controller* Winner=nullptr;
    ALDWorldObjects* Objects=nullptr;
    ALDWorldObject* Target=nullptr;
    uint8 Phase=0;
    int32 Failures=0;
    double At=0,Started=0;
    TArray<TSharedPtr<FJsonValue>> Checks,Manifest;
    TArray<FGuid> StoredIds;
    TArray<FGuid> InitialIds;
    uint32 BeforeRevision=0;
    int32 BeforeCount=0;
    void Check(bool Good,const FString& Text);
    void Place(ALDB91Controller* PC,FVector Point);
    void Task(uint8 Value,bool Both=true);
    void Save(bool Complete);
};
