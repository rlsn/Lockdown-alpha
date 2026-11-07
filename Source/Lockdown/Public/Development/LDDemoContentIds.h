#pragma once

#include "CoreMinimal.h"

// Reservations describe the wire format, not an approved or enabled content set.
// Retired identities must never be reused: skill 0..61 and item 0..98, 107, 113..114.
enum class ELDDemoCharacterId : uint8 { CharacterDemo=1 };
// #147 v7: abilities 92-103 actives, 104-113 passives (0-91 retired); attributes 15-22 (0-14 retired).
enum class ELDDemoAbilityId : uint8
{
    ActivePantsDown=92, ActiveCrotchGrab=93, ActiveSnarkBolt=94, ActiveSeethe=95, ActiveBailOut=96, ActiveElbow=97, ActiveNotMad=98, ActiveStareDown=99, ActiveQuickMeeting=100, ActiveAccountBanned=101, ActiveUnplug=102, ActiveTripleCombo=103,
    PassivePressed=104, PassiveTilted=105, PassiveStoke=106, PassiveKickDown=107, PassiveThickFace=108, PassiveFreeloader=109, PassiveSpecialForces=110, PassiveMoralVictory=111, PassiveIcedAmericano=112, PassiveLuckyStar=113
};
enum class ELDDemoAttributeId : uint8
{
    AttrProtein=15, AttrFastHands=16, AttrAllNighter=17, AttrCanteen=18, AttrBigTalk=19, AttrMorningRush=20, AttrHandOut=21, AttrWildFrenzy=22
};
enum class ELDDemoEquipmentId : uint8
{
    ItemIronPlate=99, ItemEdgeBlade=100, ItemRunicFocus=101,
    ItemSiphonEmblem=102, ItemPhaseAnchor=103, ItemArcaneBattery=104, ItemEchoMaul=105,
    ItemSlingshot=106, ItemNailUniform=108,
    ItemMegaphone=109, ItemDossier=110, ItemPrivacyScreen=111, ItemSkateboard=112
};
enum class ELDDemoMonsterId : uint8 { MonsterScavenger=1, MonsterGuardian=2, MonsterDean=3 };
// Public statuses travel in a 32-bit observation mask, so status ids stay below 32 (#112 C2 uses 20..24, #147 v7 25..26).
enum class ELDDemoStatusId : uint8 { Root=1, Shield=2, CampReturning=3, RespawnProtection=4,
    Slow=5, StatBoost=6, Enraged=7, AmbushReady=8, Exposed=9, Taunt=10,
    Disarm=12, Stun=13, Empowered=14,
    Knockback=20, HealCut=22, DamageGuard=23, Vulnerable=24, Silence=25, Burn=26 };
// #147 build v7 adds the keyword primitives Silence..ZoneBurn (docs/systems/COMBAT_AND_GROWTH.md).
enum class ELDDemoEffectOp : uint8
{
    Damage=1, Shield=2, DispelRoot=3, Root=4, StatBonus=5,
    OutOfCombatRegen=6, Lifesteal=7, Dash=8, Cleave=10, DashBoost=11,
    Slow=12, TimedStatBonus=13, SelfArea=14, LowHpStatBonus=15, AmbushDamage=16, ControlDamageAmp=17,
    Taunt=18, Disarm=20, Stun=21, EmpowerBasic=22, Jump=23, Thorns=24, Backstab=25,
    CooldownRefund=26, AbilityCrit=27, ResistShred=28, Momentum=29, TauntRetaliate=30, TriggeredShield=31,
    BasicStacks=32, LongReach=33, CritDamageReduction=34, NthBasicBonus=35, ManaOnHit=36, DispelSlow=37,
    PathSlow=38, Knockback=40, Dot=42, MobilityPunish=43, Volley=44, RefreshDebuffs=45, OnHit=46, DebuffAmp=47, ShieldShatter=48, DebuffHeal=49, Zone=50, ZoneShield=51, ZoneSlow=52, ZoneRoot=53, ZoneDamage=54, DamageGuard=55, ShieldBurst=56, Vulnerable=57, BreachStacks=61, MobilityShield=62, ShieldPulse=63, BasicRider=64, CastFrenzy=65, AfflictionAmp=66, ShieldBreakRetort=67, HitHaste=68,
    Silence=69, Breach=70, Burn=71, Execute=72, Pierce=73, Echo=74, TauntShield=75, DispelDisarm=76, ZoneBurn=77
};
