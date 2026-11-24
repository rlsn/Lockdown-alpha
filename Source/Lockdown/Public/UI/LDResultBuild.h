#pragma once

#include "CoreMinimal.h"

class UWidget;
class UWidgetTree;
struct FLDDemoContentDefinitions;
struct FLDResultBuild;
struct FLDResultRow;
struct FLDResultView;

namespace LDResultBuild
{
    enum class EReportTab : uint8 { Build, Timeline, Comparison };
    UWidget* MakeReport(UWidgetTree* Tree,const FLDResultView& Result,const FLDResultRow& Row,
        const FLDDemoContentDefinitions* Content,EReportTab Tab,float Width,float TextScale);
    // A compact read-only rule board, or an explicit error when the frozen summary cannot be displayed.
    LOCKDOWN_API UWidget* Make(UWidgetTree* Tree, const FLDResultBuild& Build, const FLDDemoContentDefinitions* Content,
        float Width, float Height, float TextScale);
}
