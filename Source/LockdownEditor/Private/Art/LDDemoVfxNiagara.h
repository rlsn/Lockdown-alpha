#pragma once
#include "CoreMinimal.h"

// Niagara systems of the combat slice, built from code in the VFX import commandlet through the engine's external edit
// utilities (UNiagaraExternalEditUtilities): engine template emitters, user parameters, module inputs linked to them,
// renderer properties and the importer-built particle material, then compiled and saved.
// GENERATOR-OWNED: everything under LDSliceVfx::GeneratedFolder (/Game/Lockdown/Art/VFX/Niagara/Generated) is rebuilt by
// every import and must never be edited by hand; the generator writes nothing outside it. Tune through the chain specs in
// LDDemoVfxNiagara.cpp, DA_CombatVfx and the user parameters instead.
namespace LDDemoVfxNiagara
{
    // Builds and saves every slice Niagara system: one Chain per look (NS_LDChain_V1/A/B/C) and the Capabilities check.
    bool Build(FString& Error);
    // Checks that every slice Niagara system exists, holds its user parameters and compiled without errors.
    bool Validate(FString& Error);
}
