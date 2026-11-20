#pragma once

#include "CoreMinimal.h"

class FLDDemoContentSnapshot;
class ULDMapDefinition;
class UWorld;
struct FLDDemoWorldGeometry;

namespace LDDemoWorldBuilder
{
// Scoped layout-point synchronization: adds the approved new camp member homes and the definition's layout points the
// saved level lacks (at their definition position), and removes saved anchor points the definition no longer declares
// (optional mini flag and locker points the author added are kept). Preserves every other object; never
// regenerates collision or navigation.
bool RefreshLayoutPoints(UWorld& World,const FLDDemoWorldGeometry& Previous,ULDMapDefinition& Definition,const FLDDemoContentSnapshot& Content,FString& Error);
bool Refresh(UWorld& World, ULDMapDefinition& Definition, const FLDDemoContentSnapshot& Content, FString& Error);
bool Validate(UWorld& World, const ULDMapDefinition& Definition, const FLDDemoContentSnapshot& Content,
    const FString& ReportDirectory, FString& Error);
bool SaveAsset(UObject& Asset, const FString& PackagePath, FString& Error);
// The title screen's map: a blank level with only its world settings. The title is the UI root over its background
// image (LDUIBackgrounds), so the map renders nothing.
bool GenerateFrontend(UWorld*& Out, FString& Error);
// Refuses any actor beyond the world settings and the editor's builder brush: no geometry, light, volume or start.
bool ValidateFrontend(const UWorld& World, FString& Error);
}
