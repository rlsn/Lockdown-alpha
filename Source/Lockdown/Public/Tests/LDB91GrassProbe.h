#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB91GrassProbe.generated.h"
class ALDB91Controller;
class ALDWorldObjects;
class ALDWorldObject;
class ALDItem;
class ALDTacticalField;
class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB91GrassProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB91GrassProbe();
    virtual void Tick(float Delta) override;
private:
    ALDB91Controller* A=nullptr;
    ALDB91Controller* B=nullptr;
    ALDWorldObjects* Objects=nullptr;
    ALDWorldObject* Grass=nullptr;
    TWeakObjectPtr<ALDItem> TorchA,TorchB;
    TWeakObjectPtr<AActor> Wall;
    TWeakObjectPtr<ALDTacticalField> Detector;
    FGuid SeenBody;
    uint8 Phase=0;
    int32 Failures=0;
    double Started=0,At=0;
    TArray<TSharedPtr<FJsonValue>> Checks,Manifest;
    void Check(bool Good,const FString& Text);
    void Place(ALDB91Controller* PC,FVector Point);
    void Task(uint8 Value);
    void Save(bool Complete);
};
