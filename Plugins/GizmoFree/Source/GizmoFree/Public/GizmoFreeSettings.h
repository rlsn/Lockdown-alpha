#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GizmoFreeSettings.generated.h"

UCLASS(Config=EditorPerProjectUserSettings, meta=(DisplayName="GizmoFree"))
class GIZMOFREE_API UGizmoFreeSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

    UPROPERTY(Config, EditAnywhere, Category="General", meta=(DisplayName="Enable Alt-drag transforms"))
    bool bEnabled = true;

    UPROPERTY(Config, EditAnywhere, Category="Sensitivity", meta=(ClampMin="0.01", ClampMax="10.0"))
    float MoveSensitivity = 0.7f;

    UPROPERTY(Config, EditAnywhere, Category="Sensitivity", meta=(ClampMin="0.01", ClampMax="10.0", Units="deg", ToolTip="Degrees per mouse pixel."))
    float RotationSensitivity = 0.3f;

    UPROPERTY(Config, EditAnywhere, Category="Sensitivity", meta=(ClampMin="0.0001", ClampMax="0.1", ToolTip="Exponential uniform scale per mouse pixel."))
    float ScaleSensitivity = 0.005f;
};
