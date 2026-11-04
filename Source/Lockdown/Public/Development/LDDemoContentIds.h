#pragma once

#include "CoreMinimal.h"

// Reservations describe the wire format, not an approved or enabled content set.
// Existing skill 0..61 and item 0..98 identities must never be reused.
enum class ELDDemoCharacterId : uint8 { CharacterDemo=1 };
enum class ELDDemoAbilityId : uint8
{
    ActiveArcBolt=62, ActiveGuardPulse=63, ActiveBindingSnare=64,
    PassiveBattleRhythm=65, PassiveSecondWind=66, PassivePrecision=67
};
enum class ELDDemoAttributeId : uint8
{
    AttrMight=1, AttrFocus=2, AttrVitality=3, AttrResolve=4, AttrReach=5
};
enum class ELDDemoEquipmentId : uint8
{
    ItemIronPlate=99, ItemEdgeBlade=100, ItemRunicFocus=101,
    ItemSiphonEmblem=102, ItemPhaseAnchor=103, ItemArcaneBattery=104, ItemEchoMaul=105
};
enum class ELDDemoMonsterId : uint8 { MonsterScavenger=1, MonsterGuardian=2 };
enum class ELDDemoStatusId : uint8 { Root=1, Shield=2, CampReturning=3, RespawnProtection=4 };
enum class ELDDemoEffectOp : uint8
{
    Damage=1, Shield=2, DispelRoot=3, Root=4, StatBonus=5,
    OutOfCombatRegen=6, Lifesteal=7, Dash=8, RestoreMana=9, Cleave=10, DashBoost=11
};
