#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"

struct FLDDemoContentParameters;

// #147 build v7 keywords (docs/systems/COMBAT_AND_GROWTH.md, "构筑 v7 战斗原语"): the shared breach and burn records of
// one target life and the lifesteal cap. Pure rules over primitive values; the director applies them to its body records.

// The approved keyword terms (COMBAT.Breach*, COMBAT.Burn*, COMBAT.LifestealCap).
struct LOCKDOWN_API FLDKeywordTerms
{
    int64 BreachPerStackP2 = 0;
    int32 BreachMaxStacks = 0;
    int64 BreachMs = 0;
    int64 BurnPerSecP2 = 0;
    int64 BurnAPRatioBp = 0;
    int64 BurnTickMs = 0;
    int64 BurnMs = 0;
    int32 BurnMaxStacks = 0;
    int64 LifestealCapBp = 0;
    // The burn's fixed part uses the same segmented skill growth as learned abilities.
    int64 SkillGrowthPerLevelBp = 0;
    int64 SkillGrowthPerLevelAfter10Bp = 0;

    static FLDKeywordTerms From(const FLDDemoContentParameters& Parameters);
};

// The one breach of a target life (D12): every source adds stacks to it, each stack takes BreachPerStack off armor and
// magic resist, and a new stack restarts the whole group. Breach layers stay when their source leaves (they are no damage).
struct FLDCombatSharedBreach
{
    int32 Stacks = 0;
    int64 ExpiresAtMs = 0;
};

// One burn layer: who applied it (damage, kill and assist credit) and the applier's level and AP snapshotted when applied;
// its damage per second is BurnLayerPerSecondP2(Level, SourceAPP2) (v7 4.2: 3 x G(L) + 3% AP).
struct FLDCombatBurnLayer
{
    FGuid SourceEntity;
    FLDCombatBodyRef SourceBody;
    FGuid CreditOwner;
    FLDCombatSourceRef Source;
    int32 Level = 1;
    int64 SourceAPP2 = 0;
};

// The one burn of a target life (D11): its layers share one timer that a new layer restarts; at the cap a new layer
// replaces the oldest one. Ticks fall every BurnTickMs from the first layer's application, the last one at ExpiresAtMs.
struct FLDCombatBurn
{
    TArray<FLDCombatBurnLayer> Layers;   // oldest first
    int64 ExpiresAtMs = 0;
    int64 NextTickAtMs = 0;
};


// One tick's damage of every layer of one applier.
struct FLDBurnTickShare
{
    FGuid SourceEntity;
    FLDCombatBodyRef SourceBody;
    FGuid CreditOwner;
    FLDCombatSourceRef Source;
    int64 DamageP2 = 0;
};

namespace LDCombatKeywords
{
    LOCKDOWN_API int32 BreachStacks(const FLDCombatSharedBreach& Breach, int64 AtMs);
    // Adds Stacks up to the cap and restarts the whole group (a full group only restarts); a lapsed group starts over.
    // False for a non-positive count or unusable terms (nothing changes then).
    LOCKDOWN_API bool ApplyBreach(FLDCombatSharedBreach& Breach, int32 Stacks, int64 AtMs, const FLDKeywordTerms& Terms);
    // Armor and magic resist the active stacks take away.
    LOCKDOWN_API int64 BreachFlatP2(const FLDCombatSharedBreach& Breach, int64 AtMs, const FLDKeywordTerms& Terms);
    // The resistance a hit meets: ResistanceP2 less the active stacks' flat cut, never below zero.
    LOCKDOWN_API int64 BreachedResistanceP2(int64 ResistanceP2, const FLDCombatSharedBreach& Breach, int64 AtMs,
        const FLDKeywordTerms& Terms);
    // Clears a lapsed group (half-open at ExpiresAtMs); true when it ended.
    LOCKDOWN_API bool ExpireBreach(FLDCombatSharedBreach& Breach, int64 AtMs);

    // Per-second damage of one layer: G(L) x BurnPerSec + BurnAPRatio x AP, floored to P2 (G(L) of LDSkillGrowth with
    // SKILL.GrowthPerLevel; skill amounts already arrive grown through FLDDemoContentSnapshot::FindValues).
    LOCKDOWN_API int64 BurnLayerPerSecondP2(int32 Level, int64 SourceAPP2, const FLDKeywordTerms& Terms);
    LOCKDOWN_API bool BurnActive(const FLDCombatBurn& Burn, int64 AtMs);
    // Adds Count copies of Layer: below the cap they join, at the cap each replaces the oldest; the shared timer
    // restarts. A lapsed or empty burn starts again with its first tick one tick from AtMs. False for bad input.
    LOCKDOWN_API bool ApplyBurn(FLDCombatBurn& Burn, const FLDCombatBurnLayer& Layer, int32 Count, int64 AtMs,
        const FLDKeywordTerms& Terms);
    // One tick: per applier (first-layer order), the summed floor(PerSecond x Tick / 1000) of its layers, each at its snapshot.
    LOCKDOWN_API void BurnTickShares(const FLDCombatBurn& Burn, const FLDKeywordTerms& Terms, TArray<FLDBurnTickShare>& Out);
    // Clears a burn whose last tick has passed; true when it ended.
    LOCKDOWN_API bool ExpireBurn(FLDCombatBurn& Burn, int64 AtMs);
    // A leaving applier's layers end with it (POLICY.SourceLeave); returns the removed count.
    LOCKDOWN_API int32 RemoveBurnSource(FLDCombatBurn& Burn, const FGuid& Source);

    // Earliest fraction (0..1) of From->To at which a moving point comes within Reach of Centre (planar); false when never.
    LOCKDOWN_API bool SegmentEntry(const FVector& From, const FVector& To, const FVector& Centre, double Reach, double& OutFraction);

    // D22: every lifesteal of one hit adds up, at most COMBAT.LifestealCap.
    LOCKDOWN_API int64 CapLifestealBp(int64 SumBp, const FLDKeywordTerms& Terms);
    // triggered_shield burst: the breaker takes RatioBp of the granted shield; another nearby enemy takes half.
    LOCKDOWN_API int64 ShieldBurstShareP2(int64 GrantedP2, int64 RatioBp, bool bNearbyEnemy);
}
