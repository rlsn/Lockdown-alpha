#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB92EconomyProbe.generated.h"
class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB92EconomyProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB92EconomyProbe();
    virtual void Tick(float Delta) override;
private:
    bool Done=false;
    int32 Failures=0;
    TArray<TSharedPtr<FJsonValue>> Checks;
    void Check(bool Passed,const FString& Name);
};
