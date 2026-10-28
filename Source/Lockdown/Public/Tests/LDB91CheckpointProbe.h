#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB91CheckpointProbe.generated.h"
class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB91CheckpointProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB91CheckpointProbe();
    virtual void Tick(float Delta) override;
private:
    bool Done=false;
    int32 Failures=0;
    TArray<TSharedPtr<FJsonValue>> Checks;
    void Check(bool Passed,const FString& Name);
};
