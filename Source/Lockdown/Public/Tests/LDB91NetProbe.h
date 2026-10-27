#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB91NetProbe.generated.h"
class FJsonValue;
class ALDB91Controller;
class ALDEnvironmentDirector;
class ALDEnvironmentDoor;
UCLASS()
class LOCKDOWN_API ALDB91NetProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB91NetProbe();
    virtual void Tick(float Delta) override;
private:
    ALDB91Controller* A=nullptr;
    ALDB91Controller* B=nullptr;
    ALDEnvironmentDirector* Environment=nullptr;
    ALDEnvironmentDoor* Door=nullptr;
    int32 Phase=0,Failures=0;
    double At=0,StartWall=0;
    uint32 PriorRevision=0;
    bool Through=false;
    TArray<TSharedPtr<FJsonValue>> Checks;
    void Check(bool Good,const FString& Name);
    void Place(ALDB91Controller* PC,FVector Offset);
    void Task(uint8 Value);
    void Save(bool Complete);
};
