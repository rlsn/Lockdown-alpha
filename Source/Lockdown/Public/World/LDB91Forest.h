#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB91Forest.generated.h"
class UInstancedStaticMeshComponent;
UCLASS()
class LOCKDOWN_API ALDB91Forest:public AActor
{
    GENERATED_BODY()
public:
    ALDB91Forest();
    static bool Ensure(UWorld* World,FString& Error);
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Trunks;
private:
    bool Initialize(FString& Error);
};
