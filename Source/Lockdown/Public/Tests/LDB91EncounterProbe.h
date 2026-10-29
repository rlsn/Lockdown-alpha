#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB91EncounterProbe.generated.h"
class ALDB91Controller;class ALDWorldObjects;class ALDWorldObject;class ALDItem;class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB91EncounterProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB91EncounterProbe();
    virtual void Tick(float Delta) override;
private:
    ALDB91Controller *A=nullptr,*B=nullptr;
    ALDWorldObjects* Objects=nullptr;
    ALDWorldObject *Storage=nullptr,*Sapling=nullptr,*Grass=nullptr,*Anchor=nullptr;
    ALDItem *Knife=nullptr,*Torch=nullptr,*Rope=nullptr;
    FVector Base=FVector::ZeroVector,Last=FVector::ZeroVector;
    uint8 Phase=0;int Failures=0,Channels=0;
    double Started=0,At=0,Distance=0,Falling=0;
    bool SawChannel=false;
    TArray<TSharedPtr<FJsonValue>> Checks,RouteRows;
    int RouteIndex=0;
    FString Trace=TEXT("phase,time_s,x_cm,y_cm,z_cm\n");
    void Check(bool Good,const FString& Text);
    void Task(uint8 Value,FVector Point=FVector::ZeroVector);
    void Save(bool Complete);
};
