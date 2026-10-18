#include "Core/LDPrototypeTuning.h"

bool ULDPrototypeTuning::Validate(FString& Error) const
{
    if (SchemaVersion != 1 || BaselineRevision.IsEmpty())
    {
        Error = TEXT("Missing or unsupported migration tuning schema/revision");
        return false;
    }
    const float RequiredPositive[] = { WalkSpeed, SprintSpeed, CapsuleRadius, CapsuleHalfHeight, DashDistance,
        DashSeconds, DashCooldownSeconds, BasicReach, BasicWindupSeconds, BasicCadenceSeconds,
        AssassinationReach, AssassinationWarningSeconds, AssassinationCooldownSeconds, ProtectionSeconds,
        BodySight, WardSight, TowerSight, HeartSight, CellSize, WardLifetimeSeconds, WardPlacementRange,
        InteractionRange, SealPickupRange, ContestRange, WorkSeconds, WorkCooldownSeconds, ClaimSeconds };
    for (const float Value : RequiredPositive)
    {
        if (!FMath::IsFinite(Value) || Value <= 0)
        {
            Error = TEXT("A required tuning distance or duration is non-finite or non-positive");
            return false;
        }
    }
    if (MaxHp <= 0 || ResidentHp <= 0 || BasicDamage <= 0 || AssassinationDamage <= 0 || WardHp <= 0
        || WardCap <= 0 || CourierGold <= 0 || CourierXp <= 0 || CapsuleHalfHeight < CapsuleRadius
        || SprintSpeed < WalkSpeed || BasicCadenceSeconds < BasicWindupSeconds || ContestRange < InteractionRange)
    {
        Error = TEXT("Invalid tuning counts or inconsistent movement/combat/interaction bounds");
        return false;
    }
    Error.Reset();
    return true;
}
