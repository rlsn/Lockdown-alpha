#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vision/LDViewTypes.h"
#include "LDResetProbe.generated.h"

struct FLDResetEvidence
{
    int32 Round=0,Failures=0;
    uint32 PreviousGeneration=0;
    FLDWorldView PreviousView;
    FLDOwnerState PreviousOwner;
    TArray<TWeakObjectPtr<AActor>> OldActors;
    TArray<FString> Checks;
    TArray<double> MemoryMiB;
};

UCLASS()
class ALDResetProbe : public AActor
{
    GENERATED_BODY()
public:
    ALDResetProbe();
    virtual void Tick(float Delta) override;
private:
    void Run();
};
