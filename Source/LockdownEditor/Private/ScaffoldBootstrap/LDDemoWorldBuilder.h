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
}
