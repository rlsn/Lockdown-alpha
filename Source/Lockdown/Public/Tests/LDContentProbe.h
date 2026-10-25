#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDContentProbe.generated.h"
UCLASS()
class ALDContentProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDContentProbe();
    virtual void Tick(float Delta) override;
private:
    bool Done=false;
};
