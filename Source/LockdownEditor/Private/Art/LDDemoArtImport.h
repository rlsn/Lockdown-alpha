#pragma once
#include "CoreMinimal.h"

namespace LDDemoArtImport
{
    // Imports every CC0 source of SourceData\Art\art_assets.json into /Game/Lockdown/Art and saves the typed catalog.
    bool Import(FString& Error);
    // Checks licenses, hashes and unlisted files, that every asset was imported from the current source bytes, that the
    // saved catalog equals the manifest and resolves, and that every decoration respects its D02 placement zone.
    bool Validate(FString& Error);
}
