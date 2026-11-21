#pragma once
#include "Development/LDDemoContentDefinitions.h"

inline FLDDemoVisionRangeParameters LDVisionTestRange()
{
    FLDDemoVisionRangeParameters Range;
    Range.NeutralHeightCm = {35,1}; Range.FullHeightCm = {120,1};
    Range.UphillRadiusBp = 7500; Range.DownhillRadiusBp = 12500;
    return Range;
}
