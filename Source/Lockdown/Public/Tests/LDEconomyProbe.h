#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDEconomyProbe.generated.h"
UCLASS()
class LOCKDOWN_API ALDEconomyProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDEconomyProbe();
    virtual void Tick(float Delta) override;
private:
    bool Done=false;
};
