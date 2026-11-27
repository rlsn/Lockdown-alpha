#pragma once

#include "CoreMinimal.h"

class UWidget;
class UWidgetTree;
struct FLDDemoContentDefinitions;
struct FLDResultBuild;

namespace LDResultBuild
{
    // A compact read-only rule board, or an explicit error when the frozen summary cannot be displayed.
    LOCKDOWN_API UWidget* Make(UWidgetTree* Tree, const FLDResultBuild& Build, const FLDDemoContentDefinitions* Content,
        float Width, float Height, float TextScale);
}
