#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDAcquisitionProbe.generated.h"
UCLASS()
class ALDAcquisitionProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDAcquisitionProbe();
    virtual void Tick(float Delta) override;
private:
    bool Done=false;
};
