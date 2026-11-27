#pragma once
#include "CoreMinimal.h"

// Niagara systems of the combat slice, built from code in the VFX import commandlet through the engine's external edit
// utilities (UNiagaraExternalEditUtilities): engine template emitters, user parameters, module inputs linked to them,
// renderer properties and the generated materials, then compiled and saved.
// GENERATOR-OWNED: everything under LDSliceVfx::GeneratedFolder (/Game/Lockdown/Art/VFX/Niagara/Generated) is rebuilt by
// every import and must never be edited by hand; the generator writes nothing outside it. Tune through the part definitions in
// LDDemoVfxNiagara.cpp (ChainParts, HitParts, SwingParts, FieldParts) and the user parameters instead.
namespace LDDemoVfxNiagara
{
    // Builds and saves every generated system (LDSliceVfx::ESystem) with its materials, and deletes any other package left
    // in the generated folder.
    bool Build(FString& Error);
    // Checks that every slice Niagara system exists, holds its user parameters and compiled without errors.
    bool Validate(FString& Error);
}
