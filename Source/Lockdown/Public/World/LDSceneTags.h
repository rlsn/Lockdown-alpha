#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "LDSceneTags.generated.h"

class AActor;
class UPrimitiveComponent;
class UWorld;

// Retains the author's navigation setting while a saved NoCollision tag suppresses navigation export.
// The instance belongs to the component, survives editor reload/duplication, and is stripped from cooked content.
UCLASS()
class LOCKDOWN_API ULDSceneTagNavigationState final : public UAssetUserData
{
    GENERATED_BODY()
public:
    UPROPERTY() bool bWasNavigationRelevant = false;
    virtual bool IsEditorOnly() const override { return true; }
};

namespace LDSceneTags
{
    // FName comparisons are case-insensitive. Component tags apply to that component; actor tags also cover
    // attached model/collision actors, so a linked collision body cannot outlive the author's model policy.
    LOCKDOWN_API bool NoCollision(const UPrimitiveComponent& Component);
    LOCKDOWN_API bool NoViewBlock(const UPrimitiveComponent& Component);
    LOCKDOWN_API void ApplyRuntimePolicy(AActor& Actor);
    LOCKDOWN_API void ApplyRuntimePolicy(UWorld& World);
    LOCKDOWN_API void PrepareEditorNavigation(UWorld& World);
}
