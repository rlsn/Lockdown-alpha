#include "Combat/LDCombatPreInput.h"

bool LDCombatPreInput::IsDirectEntry(ELDCombatEntry Entry)
{
    return Entry == ELDCombatEntry::Q || Entry == ELDCombatEntry::W || Entry == ELDCombatEntry::Item1 || Entry == ELDCombatEntry::Item2;
}

bool LDCombatPreInput::IsWindingUp(const FLDCombatOwnerView& View, int64 NowMs)
{
    if (!View.bHasBody || View.ActionPhase != ELDCombatPhase::Accepted || !IsDirectEntry(View.ActionEntry)) { return false; }
    return NowMs <= 0 || NowMs < View.ActionReleaseAtMs;
}
