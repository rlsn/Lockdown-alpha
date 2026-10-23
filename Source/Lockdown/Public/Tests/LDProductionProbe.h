#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDProductionProbe.generated.h"
UCLASS()
class LOCKDOWN_API ALDProductionProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDProductionProbe();
    virtual void Tick(float Delta) override;
private:
    bool Done=false;
};
