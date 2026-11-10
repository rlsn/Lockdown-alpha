#pragma once

#include "CoreMinimal.h"

// Reservations describe the wire format, not an approved or enabled content set.
// Retired identities must never be reused: skill 0..61 and item 0..98, 107, 113..114.
enum class ELDDemoCharacterId : uint8 { CharacterDemo=1 };
// #198: abilities are single-level actives 92-103 and 114-118; 0-91, the passives 104-113 and 120-125 and the cower
// active 119 are retired. ActiveSockShot (116) is reserved for its #198 rework and has no content projection yet.
enum class ELDDemoAbilityId : uint8
{
    ActivePantsDown=92, ActiveCrotchGrab=93, ActiveSnarkBolt=94, ActiveSeethe=95, ActiveBailOut=96, ActiveElbow=97, ActiveNotMad=98, ActiveStareDown=99, ActiveQuickMeeting=100, ActiveAccountBanned=101, ActiveUnplug=102, ActiveTripleCombo=103,
    ActiveOpenMic=114, ActiveCalculusDrop=115, ActiveSockShot=116, ActiveFlyerTrap=117, ActiveApologySlide=118
};
// #198: the five point-bought stats share the former attribute-card number space; 0-26 are retired.
enum class ELDDemoStatId : uint8
{
    StatPhysique=27, StatThickSkin=28, StatStrength=29, StatBrains=30, StatAgility=31
};
enum class ELDDemoMonsterId : uint8 { MonsterScavenger=1, MonsterGuardian=2, MonsterDean=3, MonsterAdvisor=4 };
// Public statuses travel in a 32-bit observation mask, so status ids stay below 32 (#112 C2 uses 20, #147 v7 25..26).
// Retired status identities must never be reused: 7..8, 11, 15..19, 21..24, 28..29 (native_ids.json retired_ranges).
enum class ELDDemoStatusId : uint8 { Root=1, Shield=2, CampReturning=3, RespawnProtection=4,
    Slow=5, StatBoost=6, Exposed=9, Taunt=10,
    Disarm=12, Stun=13, Empowered=14,
    Knockback=20, Silence=25, Burn=26, AttackSlowed=27 };
// #147 build v7 keyword primitives (docs/systems/COMBAT_AND_GROWTH.md); #198 keeps the passive-form operations for base
// components. Retired operation identities must never be reused: 6, 9..11, 16, 19, 23, 25, 27..30, 32..34, 38..39,
// 41..49, 51, 53, 55..66, 68, 72..76, 83..85, 89 (native_ids.json).
enum class ELDDemoEffectOp : uint8
{
    Damage=1, Shield=2, DispelRoot=3, Root=4, StatBonus=5,
    Lifesteal=7, Dash=8,
    Slow=12, TimedStatBonus=13, SelfArea=14, LowHpStatBonus=15, ControlDamageAmp=17,
    Taunt=18, Disarm=20, Stun=21, EmpowerBasic=22, Thorns=24,
    CooldownRefund=26, TriggeredShield=31,
    NthBasicBonus=35, ManaOnHit=36, DispelSlow=37,
    Knockback=40, Zone=50, ZoneSlow=52, ZoneDamage=54,
    ShieldBreakRetort=67,
    Silence=69, Breach=70, Burn=71, ZoneBurn=77,
    ChannelCone=78, GroundImpact=79, AttackSlow=80, ContactTrap=81, DashContact=82,
    PrimaryGuard=86, DashContactDamage=87, DashContactRefund=88
};
