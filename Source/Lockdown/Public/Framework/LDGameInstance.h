#pragma once
#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "LDGameInstance.generated.h"

UCLASS()
class LOCKDOWN_API ULDGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    FString ConnectionMessage;
    virtual void Init() override;
    virtual void Shutdown() override;
private:
    FDelegateHandle NetworkHandle, TravelHandle;
    void NetworkFailed(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
    void TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Error);
    void ReturnAfterFailure(UWorld* World, const FString& Error);
};
