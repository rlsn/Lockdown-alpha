#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDCombatProbe.generated.h"
class ALDCharacter;
UCLASS()
class ALDCombatProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDCombatProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    UPROPERTY() TObjectPtr<ALDCharacter> A;
    UPROPERTY() TObjectPtr<ALDCharacter> B;
    int32 Phase=0,Failures=0;
    double At=0;
    FVector Start;
    bool bDashStarted=false;
    TArray<FString> Checks;
    void Reset();
    void Rules();
    void Check(bool Pass,const FString& Text);
    void DashPhase(int32 Next,FVector Point);
    void Finish();
};
