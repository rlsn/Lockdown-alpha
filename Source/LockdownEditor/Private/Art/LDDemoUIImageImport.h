#pragma once
#include "CoreMinimal.h"

// The title and lobby background images of SourceData\UI\ui_images.json (supplied by the project owner, not part of the
// CC0 art packs of SourceData\Art). Each listed image becomes one UI texture at the path LDUIBackgrounds names.
namespace LDDemoUIImageImport
{
    // Imports every listed image over its texture asset and saves it.
    bool Import(FString& Error);
    // Checks the manifest, hashes and unlisted files, and that every texture exists with UI settings and was imported
    // from the current source bytes.
    bool Validate(FString& Error);
}
