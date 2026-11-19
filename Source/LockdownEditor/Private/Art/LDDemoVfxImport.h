#pragma once
#include "CoreMinimal.h"

// The hit-effect masks of SourceData\VFX\vfx_textures.json (rendered for this project by
// SourceData\VFX\Blender\hit_textures.py), each one effect texture at LDHitVfx::TexturePackage(id), and the three
// importer-built materials LDHitVfx::LayerMaterial, ShellMaterial and GhostMaterial. Their graphs are code: an import rebuilds
// a material whose graph differs from the importer's, so edits made to the material assets are replaced; tint,
// brightness and fade are runtime parameters.
namespace LDDemoVfxImport
{
    // Imports every listed mask whose texture differs from its source, rebuilds any material whose graph differs and saves it.
    bool Import(FString& Error);
    // Checks the manifest, hashes, the generator script and unlisted files, that every texture exists with effect
    // settings and was imported from the current source bytes, and that all three materials hold the importer's graph.
    bool Validate(FString& Error);
}
