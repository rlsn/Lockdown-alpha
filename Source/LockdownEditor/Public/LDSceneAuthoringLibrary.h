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
    UFUNCTION(BlueprintCallable, Category="Lockdown|Scene Authoring")
    static FString NavigationBakeError(UWorld* World);
    // Read-only diagnostics over the current saved scene projection and native collision/navigation.
    UFUNCTION(BlueprintCallable, Category="Lockdown|Scene Authoring")
    static FString GameplayPlacementAudit(UWorld* World, bool bIncludePaths = true);
    // Read-only sight/coverage diagnostics at authored locations and the current local player.
    UFUNCTION(BlueprintCallable, Category="Lockdown|Scene Authoring")
    static FString GameplayVisibilityAudit(UWorld* World, bool bSampleNavigation = false);
    // Native collision envelopes, narrow passages, and deterministic complete-path samples.
    UFUNCTION(BlueprintCallable, Category="Lockdown|Scene Authoring")
    static FString GameplayTraversalAudit(UWorld* World, int32 PathPairCount = 200);
};
