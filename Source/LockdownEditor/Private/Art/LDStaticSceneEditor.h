#pragma once
#include "CoreMinimal.h"
class UWorld;
struct FLDDemoWorldGeometry;
namespace LDStaticSceneEditor
{
    bool RefreshIndex(UWorld& World, FString& Error);
    bool NormalizeCopies(UWorld& World, const FLDDemoWorldGeometry& Base, FString& Error);
    bool Export(const FString& Filename, FString& Error);
}
