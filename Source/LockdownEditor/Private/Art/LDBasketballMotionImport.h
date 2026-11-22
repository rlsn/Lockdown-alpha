#pragma once
#include "CoreMinimal.h"
class USkeletalMesh;
class ULDBasketballMotionSet;

namespace LDBasketballMotionImport
{
    // Rebuilds finished animation assets from the retained, calibrated local pose records. No preview game classes.
    bool Import(USkeletalMesh& Body,const FString& Source,const FString& Folder,const FString& AssetName,
        TArray<UObject*>& Assets,FString& Error);
    bool ValidateSource(const ULDBasketballMotionSet& Motion,const FString& Source,FString& Error);
}
