#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB91ToolsProbe.generated.h"
class ALDB91Controller;
class ALDWorldObjects;
class ALDWorldObject;
class ALDItem;
class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB91ToolsProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB91ToolsProbe();
    virtual void Tick(float Delta) override;
private:
    ALDB91Controller* A=nullptr;
    ALDB91Controller* B=nullptr;
    ALDWorldObjects* Objects=nullptr;
    ALDWorldObject* Torch=nullptr;
    ALDItem* Item=nullptr;
    uint8 Phase=0;
    int32 Failures=0;
    double Started=0,At=0,ReturnAt=0;
    bool Seeded=false;
    TArray<TSharedPtr<FJsonValue>> Checks,Fuel;
    void Check(bool Good,const FString& Text);
    void Record(const FString& Tag);
    void Place(ALDB91Controller* PC,FVector Point);
    void Task(uint8 Value);
    void Save(bool Complete);
};
