#pragma once

#include "CoreMinimal.h"

// Reservations describe the wire format, not an approved or enabled content set.
// Retired identities must never be reused: skill 0..61 and item 0..98, 107, 113..114.
enum class ELDDemoCharacterId : uint8 { CharacterDemo=1 };
// #198: abilities are single-level actives 92-103, 114-118 and 126-130; 0-91, the passives 104-113 and 120-125 and the
// cower active 119 are retired.
enum class ELDDemoAbilityId : uint8
{
    ActivePantsDown=92, ActiveCrotchGrab=93, ActiveSnarkBolt=94, ActiveSeethe=95, ActiveBailOut=96, ActiveElbow=97, ActiveNotMad=98, ActiveStareDown=99, ActiveQuickMeeting=100, ActiveAccountBanned=101, ActiveUnplug=102, ActiveTripleCombo=103,
    ActiveOpenMic=114, ActiveCalculusDrop=115, ActiveSockShot=116, ActiveFlyerTrap=117, ActiveApologySlide=118,
    ActiveHoseDown=126, ActiveZap=127, ActiveChargedRam=128, ActiveBrace=129, ActiveDirtyPuddle=130
};
// #198: the five point-bought stats share the former attribute-card number space; 0-26 are retired.
enum class ELDDemoStatId : uint8
{
    StatPhysique=27, StatThickSkin=28, StatStrength=29, StatBrains=30, StatAgility=31
};
// #198 B: workshop materials. Base components B01-B18 are 1-18; mods M01-M12 take 19-30 and connectors C01-C12 31-42 (#198 F).
enum class ELDDemoComponentId : uint8
{
    ComponentFistFire=1, ComponentTabLedger=2, ComponentSpikyHide=3, ComponentPunchHeal=4, ComponentDashBump=5,
    ComponentThirdPunch=6, ComponentKickDown=7, ComponentBreachFist=8, ComponentHardSkin=9, ComponentFistWater=10,
    ComponentFistShock=11, ComponentDashPunch=12, ComponentCatchBreath=13, ComponentLongArm=14, ComponentSeeingRed=15,
    ComponentThickHide=16, ComponentHeldShot=17, ComponentDirtyTrick=18,
    ComponentCrowdHit=19, ComponentSettleLater=20, ComponentLongKick=21, ComponentHoldDown=22, ComponentReachOut=23, ComponentAgainNow=24,
    ComponentOneSlam=25, ComponentRamThrough=26, ComponentQuickHop=27, ComponentBigTab=28, ComponentPickYou=29, ComponentOweIt=30,
    ComponentFireSkin=31, ComponentBallKick=32, ComponentBumpAgain=33, ComponentRollBump=34, ComponentPinPunch=35, ComponentAfterPin=36,
    ComponentControlMana=37, ComponentBreathStrike=38, ComponentBlastChase=39, ComponentShellBurst=40, ComponentHurtHeal=41, ComponentDirtyFist=42
};
enum class ELDDemoMonsterId : uint8 { MonsterScavenger=1, MonsterGuardian=2, MonsterDean=3, MonsterAdvisor=4 };
// Public status ids stay below 64 (the observation uniqueness check is a 64-bit mask; #112 C2 uses 20, #147 v7 25..26,
// #198 elements 30..33).
// Retired status identities must never be reused: 7..8, 11, 15..19, 21..24, 28..29 (native_ids.json retired_ranges).
enum class ELDDemoStatusId : uint8 { Root=1, Shield=2, CampReturning=3, RespawnProtection=4,
    Slow=5, StatBoost=6, Exposed=9, Taunt=10,
    Disarm=12, Stun=13, Empowered=14,
    Knockback=20, Silence=25, Burn=26, AttackSlowed=27,
    Wet=30, Electrified=31, Braced=32, Soured=33 };
// #147 build v7 keyword primitives (docs/systems/COMBAT_AND_GROWTH.md); #198 keeps the passive-form operations for base
// components. Retired operation identities must never be reused: 6, 9..11, 16, 19, 23, 25, 27..30, 32..34, 38..39,
// 41..49, 51, 53, 55..66, 68, 72..76, 83..85, 89 (native_ids.json). #198 B owns 100..119 for component-only operations;
// 90..96 are the #198 elements, reactions and the S09/S10/S13/S16/S20/S21 actives; #198 F owns 120..149 (mods 120..136,
// connectors 137..147; C03 reuses DashContactRefund).
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
    PrimaryGuard=86, DashContactDamage=87, DashContactRefund=88,
    Wet=90, Electrify=91, ChainBounce=92, Brace=93, ZoneWet=94, ZoneHaste=95, Grievous=96,
    Ledger=100, BasicElement=101, BasicDamageScale=102, IdleReadyAmp=103, ActiveElement=104,
    SpreadUnit=120, SpreadProjectile=121, SpreadLedger=122, SpreadCounter=123, DelayLedger=124, DelayCounter=125,
    KnockbackScale=126, ControlScale=127, RangeBonus=128, CounterRangeBonus=129, CooldownCost=130, CompressCone=131,
    CompressZone=132, DashScale=133, LedgerVariant=134, MarkedCounter=135, DamageDefer=136, FireShield=137,
    KnockbackImpact=138, ContactKnockback=139, ControlMark=140, Aftershock=141, ControlMana=142, HealCounter=143,
    ReactionEmpower=144, ShieldBurst=145, SkillLifesteal=146, ExtraBase=147, CompressChain=148
};
