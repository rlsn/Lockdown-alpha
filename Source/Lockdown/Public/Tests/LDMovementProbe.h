#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDMovementProbe.generated.h"
class ALDCharacter;

// Explicit, non-shipping standalone verification scenario; never spawned by a client request.
UCLASS()
class LOCKDOWN_API ALDMovementProbe : public AActor
{
    GENERATED_BODY()
public:
    ALDMovementProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    UPROPERTY() TObjectPtr<ALDCharacter> TestBody;
    int32 Phase = 0, Failures = 0;
    float PhaseTime = 0;
    double StartedAt = 0;
    FVector StartLocation;
    TArray<FString> Results;
    TMap<TWeakObjectPtr<ALDCharacter>, FVector> ResidentStarts;
    void StartPhase(int32 NewPhase, FVector Location, bool bSprint);
    void Check(bool bPass, const FString& Description);
    void Finish();
};
