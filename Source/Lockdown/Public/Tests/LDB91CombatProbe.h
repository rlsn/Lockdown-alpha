#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB91CombatProbe.generated.h"
class ALDB91Controller;class ALDWorldObject;class ALDCharacter;class ALDItem;class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB91CombatProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB91CombatProbe();
    virtual void Tick(float Delta) override;
private:
    ALDB91Controller *A=nullptr,*B=nullptr;
    FVector Arena;
    uint8 Phase=0;
    double Started=0,At=0,MP=0;
    int Failures=0;
    int64 Lock=0,GuardLock=0;
    bool Stunned=false;
    FGuid OldBody;
    ALDWorldObject* Grass=nullptr;
    TWeakObjectPtr<AActor> Wall;
    TArray<TSharedPtr<FJsonValue>> Checks;
    void Check(bool Good,const FString& Text);
    void Task(uint8 Value);
    void Place(ALDB91Controller* PC,FVector Floor);
    void Save(bool Complete);
};
