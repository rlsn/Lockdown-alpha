#pragma once

#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDGrowthProtocol.h"

// #198 F workshop rules shared by growth (install legality, build validation) and Combat's projection: the stored shape of
// an installed mod or connector. Pure functions over the approved catalog row; whether the target is still learned or
// installed is the install command's check (a target lost later only makes the attachment inert).
namespace LDWorkshop
{
    // Whether Id is listed in Ids (case-sensitive catalog ids).
    LOCKDOWN_API bool Lists(const TArray<FString>& Ids, FName Id);
    // Whether an ability may host a mod or connector: exact authored targets, plus an ability of an authored target's
    // delivery mode that the component actually changes (a mod's compile gate, else every effect of a listed same-mode host).
    LOCKDOWN_API bool TargetsActive(const FLDDemoContentDefinitions& Content, const FLDDemoComponentDefinition& Definition, FName ActiveId);
    // The first effect of Op the component carries, or null.
    LOCKDOWN_API const FLDDemoEffectDefinition* FindOp(const FLDDemoComponentDefinition& Definition, ELDDemoEffectOp Op);
    // The stored shape of an installed mod or connector: a target kind its definition accepts (Active of an accepted target
    // mode, Component listed in its targets, Dash when it takes the innate D, None for a self-installed mod), no active/element binding, and C12's
    // repeated base exactly when the definition takes one (one of its ExtraBases). False for a base component.
    LOCKDOWN_API bool TargetShape(const FLDDemoContentDefinitions& Content, const FLDDemoComponentDefinition& Definition,
        ELDComponentTargetKind Kind, FName TargetId, FName BoundActive, ELDDemoElement Element, FName BoundComponent);
    // Whether two components conflict on the same target (spec §4.4): distinct components of one form.
    LOCKDOWN_API bool Conflicts(const FLDDemoComponentDefinition& A, const FLDDemoComponentDefinition& B);
}
