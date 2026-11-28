#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatControlRules.h"

// Batch 01 isolated combat prototypes (docs/systems/COMBAT_AND_GROWTH.md#隔离原型): server command-line switches only, never
// SourceData or the content hash. Without a switch every rule runs unchanged.
struct FLDProtoSettings
{
    // C-10 "急眼了": -LDProtoAgency [-LDProtoAgencyCut=Immediate|NextControl] [-LDProtoAgencyWindow=<s>]
    // [-LDProtoAgencyThreshold=<s>] [-LDProtoAgencyGuard=<s>] [-LDProtoAgencyCooldown=<s>] [-LDProtoAgencyRollFrom=<n>].
    bool bAgency = false;
    FLDAgencyParams Agency;
    // C-14 -LDProtoPotDelay=<s>: the ground-impact delay of S08's 2B forms (2B, 3B1, 3B2); negative keeps the content value.
    int64 PotDelayMs = -1;

    bool Any() const { return bAgency || PotDelayMs >= 0; }
};

namespace LDProto
{
    // Reads the switches of CommandLine; false with Error on a malformed or out-of-range value.
    LOCKDOWN_API bool Parse(const TCHAR* CommandLine, FLDProtoSettings& Out, FString& Error);
    // The process's switches, read once. A malformed value logs LD_PROTO_INVALID and enables no prototype; enabled ones log
    // one LD_PROTO line for sample grouping.
    LOCKDOWN_API const FLDProtoSettings& Settings();
    // "flags=... params=..." of the LD_PROTO line.
    LOCKDOWN_API FString Describe(const FLDProtoSettings& Settings);
    // S08 (active_quick_meeting), whose 2B forms the pot-delay prototype retimes.
    LOCKDOWN_API FName PotSkillId();
}
