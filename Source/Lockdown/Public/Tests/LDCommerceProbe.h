#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDCommerceProbe.generated.h"
UCLASS()
class LOCKDOWN_API ALDCommerceProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDCommerceProbe();
    virtual void Tick(float Delta) override;
private:
    bool Done=false;
};
