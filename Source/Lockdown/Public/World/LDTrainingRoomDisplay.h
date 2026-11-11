#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDTrainingRoomDisplay.generated.h"

UCLASS()
class LOCKDOWN_API ALDTrainingRoomDisplay : public AActor
{
    GENERATED_BODY()
public:
    ALDTrainingRoomDisplay();
private:
    UPROPERTY() TObjectPtr<USceneComponent> Root;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> ZoneLines;
};
