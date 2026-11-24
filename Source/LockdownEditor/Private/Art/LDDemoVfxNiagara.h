#pragma once
#include "CoreMinimal.h"

// Niagara systems of the combat slice, built from code in the VFX import commandlet through the engine's external edit
// utilities (UNiagaraExternalEditUtilities): engine template emitters, user parameters, module inputs linked to them,
// renderer properties and the importer-built particle material, then compiled and saved. A rebuild of the same code
// replaces the asset, so edits made to it in the Niagara editor are replaced; tune through DA_CombatVfx and the user
// parameters instead.
namespace LDDemoVfxNiagara
{
    // Builds and saves every slice Niagara system (the Chain template, LDSliceVfx::ChainSystem).
    bool Build(FString& Error);
    // Checks that every slice Niagara system exists, holds its user parameters and compiled without errors.
    bool Validate(FString& Error);
}
