#pragma once

#include "CoreMinimal.h"

// Reservations describe the wire format, not an approved or enabled content set.
// Retired identities must never be reused: skill 0..61 and item 0..98, 107, 113..114.
enum class ELDDemoCharacterId : uint8 { CharacterDemo=1 };
// #198: abilities are single-level actives 92-103, 114-118 and 126-130 (issue #287 P4 adds N1-N3 as 131-133; 134 stays reserved for N4); 0-91, the passives 104-113 and 120-125 and the
// cower active 119 are retired.
enum class ELDDemoAbilityId : uint8
{
    ActivePantsDown=92, ActiveCrotchGrab=93, ActiveSnarkBolt=94, ActiveSeethe=95, ActiveBailOut=96, ActiveElbow=97, ActiveNotMad=98, ActiveStareDown=99, ActiveQuickMeeting=100, ActiveAccountBanned=101, ActiveUnplug=102, ActiveTripleCombo=103,
    ActiveOpenMic=114, ActiveCalculusDrop=115, ActiveSockShot=116, ActiveFlyerTrap=117, ActiveApologySlide=118,
    ActiveHoseDown=126, ActiveZap=127, ActiveChargedRam=128, ActiveBrace=129, ActiveDirtyPuddle=130,
    ActiveColaShake=131, ActiveDeskFan=132, ActiveStaleTakeout=133, ActiveSchoolBag=134
};
// #198: the five point-bought stats share the former attribute-card number space; 0-26 are retired.
enum class ELDDemoStatId : uint8
{
    StatPhysique=27, StatThickSkin=28, StatStrength=29, StatBrains=30, StatAgility=31
};
enum class ELDDemoFinalModId : uint8
{
    FinalHitAndRun=25, FinalClearOut=26, FinalKillSeconds=27, FinalUsurp=28,
    FinalCarryFlag=29, FinalCage=30, FinalPrivatePole=31, FinalDoubleYellow=32
};
// Issue #287 spec §7 build cards: A01-A19 take 200-218, K01-K25 219-243, D01-D12 244-255 and Y01-Y29 256-284 (P2, P4 and the
// #292 P5 cards); the element medals 285-289. The #198 components 0-199 and the single-element cards 214-218, 237-242 and 255
// are retired and never reused.
enum class ELDDemoCardId : uint16
{
    CardThirdBreath=200, CardPunchThink=201, CardSolidPunch=202, CardThirdShove=203, CardLongArm=204, CardBothHands=205,
    CardWideSwing=206, CardFistSkill=207, CardQuickHands=208, CardSlowCraft=209, CardScholarPunch=210, CardHeatedUp=211,
    CardMuscleMemory=212, CardBreachFist=213, CardNextUp=219, CardPayBack=220, CardQuickTakedown=221, CardSetupLine=222, CardPinAmp=223,
    CardEncore=224, CardCrowdHit=225, CardAgainNow=226, CardBigOne=227, CardLongReach=228, CardHoldLonger=229,
    CardBloodPrice=230, CardLendFist=231, CardHeldBreath=232, CardHurtHeal=233, CardHeldShot=234, CardPractice=235,
    CardComboChain=236, CardBlastReload=243, CardBumpReset=244, CardBumpAgain=245, CardDashPunch=246, CardDodgeStrike=247,
    CardDoubleDash=248, CardPassThrough=249, CardHeadDown=250, CardPaidDash=251, CardSlippery=252, CardIceTrail=253,
    CardMoveTurf=254, CardSpikyHide=256, CardTakedown=257, CardHangOn=258, CardReactionRefund=259,
    CardWaveAfter=260, CardShellBurst=261, CardHardSkin=262, CardBallKick=263, CardSkillCrit=264, CardSkillDrain=265,
    CardShortBreath=266, CardDoubleStall=267, CardThickHide=268, CardOverfed=269, CardBerserk=270, CardQuickBrain=271,
    CardPunchHeal=272, CardRunHit=273, CardGrowFat=274, CardKillStreak=275, CardToughSkin=276, CardChargeUp=277,
    CardTurfGrowth=278, CardTally=279, CardLiveWire=280, CardLongSoak=281, CardLouderBlast=282, CardEndlessBurn=283,
    CardBurnGuard=284, MedalFire=285, MedalWater=286, MedalElectric=287, MedalChill=288, MedalRot=289
};
enum class ELDDemoMonsterId : uint8 { MonsterScavenger=1, MonsterGuardian=2, MonsterDean=3, MonsterAdvisor=4, MonsterPromoter=5, MonsterCourier=6, MonsterAgent=7, MonsterGuard=8, MonsterAuditor=9, MonsterClient=10, MonsterDirector=11, MonsterFinalClient=12, MonsterProperty=13 };
// Public status ids stay below 64 (the observation uniqueness check is a 64-bit mask; #112 C2 uses 20, #147 v7 25..26,
// #198 elements 30..32, issue #287 P4 chill and rot 36..37).
// Retired status identities must never be reused: 7..8, 11, 15..19, 21..24, 28..29, 33 (native_ids.json retired_ranges).
enum class ELDDemoStatusId : uint8 { Root=1, Shield=2, CampReturning=3, RespawnProtection=4,
    Slow=5, StatBoost=6, Exposed=9, Taunt=10,
    Disarm=12, Stun=13, Empowered=14,
    Knockback=20, Silence=25, Burn=26, AttackSlowed=27,
    Wet=30, Electrified=31, Braced=32, CampEnraged=34, CampGuarding=35, Chilled=36, Rotten=37 };
// Effect operations of the actives, their skill branches (170..195), the build cards (196..232; issue #287 spec §7) and
// the P4 element, ground-state and N1-N3 operations (233..241) and the #292 P5 card operations (242..255; the uint8
// operation space is now full).
// Retired operation identities must never be reused (native_ids.json retired_ranges): 5..6, 9..11, 15..17, 19, 23,
// 25..30, 32..36, 38..39, 41..49, 51, 53, 55..68, 72..76, 83..89, 96, 100, 102, 104, 120..129, 131..137, 139..145, 147..151,
// 154, 159, 161, 165..169, 195.
enum class ELDDemoEffectOp : uint8
{
    Damage=1, Shield=2, DispelRoot=3, Root=4, Lifesteal=7, Dash=8, Slow=12, TimedStatBonus=13, SelfArea=14, Taunt=18,
    Disarm=20, Stun=21, EmpowerBasic=22, Thorns=24, TriggeredShield=31, DispelSlow=37, Knockback=40, Zone=50, ZoneSlow=52,
    ZoneDamage=54, Silence=69, Breach=70, Burn=71, ZoneBurn=77, ChannelCone=78, GroundImpact=79, AttackSlow=80,
    ContactTrap=81, DashContact=82, Wet=90, Electrify=91, ChainBounce=92, Brace=93, ZoneWet=94, ZoneHaste=95,
    BasicElement=101, IdleReadyAmp=103, CooldownCost=130, KnockbackImpact=138, SkillLifesteal=146, ProjectileReturn=152,
    ProjectilePierce=153, EchoImpact=155, HitHasteOther=156, SetupMark=157, ReactionHaste=158, ZoneTether=160,
    FinalKnockback=162, EmpowerReach=163, Pull=164, Vulnerable=170, NextBasicAmp=171, MomentumGain=172, MomentumSteal=173,
    FollowUp=174, Recast=175, DashBack=176, SelfBuff=177, BurnBurst=178, Pulse=179, ZoneGuard=180, ZoneMomentum=181,
    Overcharge=182, FollowBasic=183, ShieldBurstBreak=184, BasicRider=185, DelayedBurst=186, ShieldReflect=187,
    ShieldEndHeal=188, ShieldHitBonus=189, BraceStun=190, BraceHeal=191, BraceStack=192, BraceShove=193, BraceShield=194,
    ComboMomentum=196, BasicCooldownCut=197, ComboSkillHit=198, ComboKnockback=199, ComboDouble=200,
    OvercapExtraHit=201, BasicApRider=202, FocusAttackSpeed=203, SkillHitMomentum=204, AutoRecast=205,
    SkillDamageFactor=206, HpCost=207, SkillAdRider=208, SkillGrowth=209, SkillElement=210, DashContactReset=211,
    DashSplit=212, DashPhase=213, DashContactElement=214, TakedownReset=215, LastStand=216, ReactionMomentum=217,
    ReactionElement=218, SkillCrit=219, LifestealSkills=220, ArmorToHp=221, OverhealShield=222, MissingHpAttackSpeed=223,
    HasteMomentum=224, HealGrowth=225, KillGrowth=226, ControlGrowth=227, IdleCharge=228, ElementAura=229,
    ElementDuration=230, ReactionFactor=231, BurnCap=232,
    Rot=233, Chill=234, GroundState=236, Fan=237, DashIce=238, GroundRelocate=239, DoubleStall=240,
    GroundGrowth=241, Bag=242,
    BasicProjectile=243, ComboSweep=244, ComboCast=245, SpeedFactor=246, HitGrowth=247, ControlAmp=248, Reshape=249,
    ShieldSpend=250, RecentCastAmp=251, DodgeFreeCast=252, DashRam=253, OverdriveShort=254, Tally=255
};
