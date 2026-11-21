#pragma once
#include "CoreMinimal.h"

namespace LDDemoArtImport
{
    // Imports every glTF source of SourceData\Art\art_assets.json into /Game/Lockdown/Art, reads the authored bodies
    // kept as finished assets in their body folder and saves the typed catalog.
    // bPropsOnly: re-imports the props and the dressing props only; bodies, seats, tiers and the decorations read from the
    // saved Demo map stay as the catalog holds them (no placement check), for a new prop while the map is being re-authored.
    bool Import(FString& Error, bool bPropsOnly = false);
    // Only rebuilds the declared basketball body's presentation bank and catalog entry, preserving saved placements.
    bool ImportBasketball(FString& Error);
    // Checks licenses, hashes and unlisted files, that every asset was imported from the current source bytes, that the
    // saved catalog equals the manifest and resolves, and that every decoration respects its D02 placement zone.
    bool Validate(FString& Error);
}
