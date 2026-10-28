#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB91SaplingsProbe.generated.h"
class ALDB91Controller;class ALDWorldObjects;class ALDWorldObject;class ALDItem;class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB91SaplingsProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB91SaplingsProbe();
    virtual void Tick(float Delta) override;
private:
    ALDB91Controller* A=nullptr;ALDB91Controller* B=nullptr;ALDB91Controller* Winner=nullptr;
    ALDWorldObjects* Objects=nullptr;ALDWorldObject* Target=nullptr;
    TArray<TWeakObjectPtr<ALDItem>> Staged;
    TArray<TSharedPtr<FJsonValue>> Checks,Manifest;
    uint8 Phase=0;int Failures=0;double Started=0,At=0,Returned=0;
    void Check(bool Good,const FString& Text);
    void Near(int Index,bool Both=true);
    void Task(uint8 Value);
    void Save(bool Complete);
};
