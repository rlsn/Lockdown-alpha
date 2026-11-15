#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LDSceneAuthoringLibrary.generated.h"

UCLASS()
class LOCKDOWNEDITOR_API ULDSceneAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Lockdown|Scene Authoring")
    static bool BakeNavigation(UWorld* World, FString& Error);
    UFUNCTION(BlueprintCallable, Category="Lockdown|Scene Authoring")
    static bool ValidateGround(UWorld* World, FString& Error);
    // Python retains errors on failed validation rather than losing an out parameter through bool success conversion.
    UFUNCTION(BlueprintCallable, Category="Lockdown|Scene Authoring")
    static FString GroundValidationError(UWorld* World);
};
