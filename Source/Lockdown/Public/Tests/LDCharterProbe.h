#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDCharterProbe.generated.h"
UCLASS()
class LOCKDOWN_API ALDCharterProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDCharterProbe();
    virtual void Tick(float Delta) override;
private:
    bool Done=false;
};
