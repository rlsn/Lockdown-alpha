#pragma once

#include "CoreMinimal.h"

// Reservations describe the wire format, not an approved or enabled content set.
// Existing skill 0..61 and item 0..98 identities must never be reused.
enum class ELDDemoCharacterId : uint8 { CharacterDemo=1 };
enum class ELDDemoAbilityId : uint8
{
    ActiveArcBolt=62, ActiveGuardPulse=63, ActiveBindingSnare=64,
    PassiveBattleRhythm=65, PassiveSecondWind=66, PassivePrecision=67,
    ActiveSlipperThrow=68, ActiveFartDash=69, ActiveComboFlurry=70, ActiveSlickMoves=71,
    PassiveCornered=72, PassiveLurking=73, PassivePublicShame=74, PassiveEagleEye=75,
    ActiveMonkeyPeach=76, ActiveThousandYear=77, ActiveLunge=78, PassiveIronCrotch=79,
    ActiveBodySlam=80, ActiveComeHere=81,
    ActivePorcelainFlop=82, ActiveExposePost=83, ActiveBarrageRant=84, ActiveCancelBlast=85, ActivePieChart=86, ActiveTableFlip=87, PassiveDoxMaster=88, PassiveThickSkin=89, PassiveMentalGymnastics=90, PassiveKeyboardWarrior=91
};
enum class ELDDemoAttributeId : uint8
{
    AttrMight=1, AttrFocus=2, AttrVitality=3, AttrResolve=4, AttrReach=5,
    AttrMightHp=6, AttrMightHaste=7, AttrFocusAs=8, AttrFocusDefense=9, AttrReachMs=10, AttrRoamHybrid=11,
    AttrWildGlassCannon=12, AttrWildJuggernaut=13, AttrWildBrawler=14
};
enum class ELDDemoEquipmentId : uint8
{
    ItemIronPlate=99, ItemEdgeBlade=100, ItemRunicFocus=101,
    ItemSiphonEmblem=102, ItemPhaseAnchor=103, ItemArcaneBattery=104, ItemEchoMaul=105,
    ItemSlingshot=106, ItemPaperBox=107, ItemNailUniform=108,
    ItemMegaphone=109, ItemDossier=110, ItemPrivacyScreen=111, ItemSkateboard=112, ItemDecoyWig=113, ItemEnergyDrink=114
};
enum class ELDDemoMonsterId : uint8 { MonsterScavenger=1, MonsterGuardian=2 };
// Public statuses travel in a 32-bit observation mask, so status ids stay below 32 (#112 C2 uses 20..24).
enum class ELDDemoStatusId : uint8 { Root=1, Shield=2, CampReturning=3, RespawnProtection=4,
    Slow=5, StatBoost=6, Enraged=7, AmbushReady=8, Exposed=9, Taunt=10, Stealth=11,
    Disarm=12, Stun=13, Empowered=14,
    Knockback=20, HealCut=22, DamageGuard=23, Vulnerable=24 };
enum class ELDDemoEffectOp : uint8
{
    Damage=1, Shield=2, DispelRoot=3, Root=4, StatBonus=5,
    OutOfCombatRegen=6, Lifesteal=7, Dash=8, RestoreMana=9, Cleave=10, DashBoost=11,
    Slow=12, TimedStatBonus=13, SelfArea=14, LowHpStatBonus=15, AmbushDamage=16, ControlDamageAmp=17,
    Taunt=18, Stealth=19, Disarm=20, Stun=21, EmpowerBasic=22, Jump=23, Thorns=24, Backstab=25,
    CooldownRefund=26, AbilityCrit=27, ResistShred=28, Momentum=29, TauntRetaliate=30, TriggeredShield=31,
    BasicStacks=32, LongReach=33, CritDamageReduction=34, NthBasicBonus=35, ManaOnHit=36, DispelSlow=37,
    PathSlow=38, Knockback=40, HealCut=41, Dot=42, MobilityPunish=43, Volley=44, RefreshDebuffs=45, OnHit=46, DebuffAmp=47, ShieldShatter=48, DebuffHeal=49, Zone=50, ZoneShield=51, ZoneSlow=52, ZoneRoot=53, ZoneDamage=54, DamageGuard=55, ShieldBurst=56, Vulnerable=57, Decoy=58, LeapBack=59, CooldownRefresh=60, BreachStacks=61, MobilityShield=62, ShieldPulse=63, BasicRider=64, CastFrenzy=65, AfflictionAmp=66, ShieldBreakRetort=67, HitHaste=68
};
