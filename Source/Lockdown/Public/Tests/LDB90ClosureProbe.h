#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB90ClosureProbe.generated.h"
class FJsonObject;
struct FLDB90ResetEvidence
{
    int32 Round=0,Failures=0;
    uint32 PreviousGeneration=0;
    FString Bindings,Geometry;
    TArray<TWeakObjectPtr<AActor>> OldActors;
    TArray<TSharedPtr<class FJsonValue>> Rows;
};

UCLASS()
class LOCKDOWN_API ALDB90ClosureProbe : public AActor
{
    GENERATED_BODY()
public:
    ALDB90ClosureProbe();
    virtual void Tick(float Delta) override;
    static bool LegacyCompleted(UWorld* World);
private:
    bool Ran=false;
    void Run();
};
