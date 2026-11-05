#pragma once

#include "CoreMinimal.h"

class FLDDemoContentSnapshot;
class ULDMapDefinition;
class UWorld;

namespace LDDemoWorldBuilder
{
bool Generate(ULDMapDefinition& Definition, const FLDDemoContentSnapshot& Content, UWorld*& Out, FString& Error);
bool Validate(UWorld& World, const ULDMapDefinition& Definition, const FLDDemoContentSnapshot& Content,
    const FString& ReportDirectory, FString& Error);
bool SaveAsset(UObject& Asset, const FString& PackagePath, FString& Error);
// The title screen's map: a blank level with only its world settings. The title is the UI root over its background
// image (LDUIBackgrounds), so the map renders nothing.
bool GenerateFrontend(UWorld*& Out, FString& Error);
// Refuses any actor beyond the world settings and the editor's builder brush: no geometry, light, volume or start.
bool ValidateFrontend(const UWorld& World, FString& Error);
}
