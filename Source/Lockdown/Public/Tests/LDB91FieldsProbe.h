#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB91FieldsProbe.generated.h"
class ALDB91Controller;
class ALDWorldObjects;
class ALDItem;
class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB91FieldsProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB91FieldsProbe();
    virtual void Tick(float Delta) override;
private:
    ALDB91Controller* A=nullptr;
    ALDB91Controller* B=nullptr;
    ALDWorldObjects* Objects=nullptr;
    TWeakObjectPtr<ALDItem> Water,Oil,Second;
    uint8 Phase=0;
    int32 Failures=0;
    double Started=0,At=0;
    uint32 ReplayRevision=0;
    FVector Aim=FVector::ZeroVector;
    TArray<TSharedPtr<FJsonValue>> Checks;
    void Check(bool Good,const FString& Text);
    void Place(ALDB91Controller* PC,FVector Point);
    void Task(uint8 Value);
    void Save(bool Complete);
};
