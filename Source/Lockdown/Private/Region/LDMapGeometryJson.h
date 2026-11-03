#pragma once

#include "Region/LDMapGeometry.h"
#include "../Development/LDDemoContentJson.h"

namespace LDDemoWorld
{
bool Project(const LDDemoContent::FValue& Value, const FLDDemoContentDefinitions& Content,
    FLDDemoWorldGeometry& Out, LDDemoContent::FCheck& Check);
}
