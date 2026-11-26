#include "Character/LDActionMotion.h"
#include "Development/LDDemoContentIds.h"

LDActionMotion::EFamily LDActionMotion::FamilyOf(bool bAttack, uint8 ComboStep, uint8 Ability)
{
    if (bAttack) { return ComboStep >= 3 ? EFamily::Finisher : ComboStep == 2 ? EFamily::Cross : EFamily::Jab; }
    switch (ELDDemoAbilityId(Ability))
    {
    // Thrown or aimed at one target.
    case ELDDemoAbilityId::ActiveSnarkBolt:
    case ELDDemoAbilityId::ActiveStaleTakeout:
    case ELDDemoAbilityId::ActiveZap:
    case ELDDemoAbilityId::ActiveElbow:
    case ELDDemoAbilityId::ActiveCordonTape:
    case ELDDemoAbilityId::ActiveFlyerTrap:
    case ELDDemoAbilityId::ActivePantsDown:
        return EFamily::OneHandThrow;
    // Slammed into the ground or driven with the whole body.
    case ELDDemoAbilityId::ActiveCalculusDrop:
    case ELDDemoAbilityId::ActiveSchoolBag:
    case ELDDemoAbilityId::ActiveChargedRam:
    case ELDDemoAbilityId::ActiveFlurryRush:
        return EFamily::HeavySlam;
    // Stances and buffs on the own body.
    case ELDDemoAbilityId::ActiveNotMad:
    case ELDDemoAbilityId::ActiveBrace:
    case ELDDemoAbilityId::ActiveUnplug:
    case ELDDemoAbilityId::ActiveTripleCombo:
    case ELDDemoAbilityId::ActiveStareDown:
    case ELDDemoAbilityId::ActiveBailOut:
        return EFamily::Ready;
    // Areas, voices, sprays and fields set with both hands.
    default:
        return EFamily::TwoHandShot;
    }
}

const TCHAR* LDActionMotion::FamilyName(EFamily Family)
{
    switch (Family)
    {
    case EFamily::Jab: return TEXT("Jab");
    case EFamily::Cross: return TEXT("Cross");
    case EFamily::Finisher: return TEXT("Finisher");
    case EFamily::OneHandThrow: return TEXT("OneHandThrow");
    case EFamily::TwoHandShot: return TEXT("TwoHandShot");
    case EFamily::HeavySlam: return TEXT("HeavySlam");
    case EFamily::Ready: return TEXT("Ready");
    default: return TEXT("Unknown");
    }
}

LDActionMotion::FShape LDActionMotion::ShapeOf(EFamily Family, float Age, float Impact)
{
    FShape Out;
    if (!FMath::IsFinite(Age) || !FMath::IsFinite(Impact) || Age < 0.f || Impact <= 0.f || Age > Impact + SettleSeconds) { return Out; }
    // Wind-up to 70 % of the windup, snap through at the release, settle back to rest.
    const float Wind = FMath::SmoothStep(0.f, .7f * Impact, Age) * (1.f - FMath::SmoothStep(.7f * Impact, Impact, Age));
    const float Snap = Age <= Impact ? FMath::SmoothStep(.7f * Impact, Impact, Age) : 1.f - FMath::SmoothStep(Impact, Impact + SettleSeconds, Age);
    switch (Family)
    {
    // The right jab loads the right shoulder back, then turns the chest left into the blow.
    case EFamily::Jab: Out.YawDeg = 10.f * Wind - 16.f * Snap; break;
    // The left cross loads the other way and turns further, stepping to the right side.
    case EFamily::Cross: Out.YawDeg = -16.f * Wind + 30.f * Snap; Out.SideCm = 14.f * Snap; Out.LungeScale = 1.3f; break;
    // The finisher sinks back through the windup and drives forward hardest.
    case EFamily::Finisher: Out.YawDeg = -6.f * Snap; Out.LungeScale = 1.8f; break;
    case EFamily::OneHandThrow: Out.YawDeg = 8.f * Wind - 18.f * Snap; break;
    case EFamily::HeavySlam: Out.LungeScale = 1.6f; break;
    default: break;
    }
    return Out;
}

LDActionMotion::FBlend LDActionMotion::BlendOf(EFamily Family, bool bMoving)
{
    switch (Family)
    {
    // A jab stays light over the stance; the cross turns the chest further.
    case EFamily::Jab: return {0.f, .42f, 1.f};
    case EFamily::Cross: return {0.f, .7f, 1.f};
    case EFamily::OneHandThrow: return {0.f, 1.f, 1.f};
    case EFamily::Ready: return bMoving ? FBlend{0.f, 1.f, 1.f} : FBlend{1.f, 0.f, 0.f};
    // The finisher, set shots and slams shift the whole weight when standing.
    case EFamily::Finisher:
    case EFamily::TwoHandShot:
    case EFamily::HeavySlam: return bMoving ? FBlend{0.f, 1.f, 1.f} : FBlend{1.f, 0.f, 0.f};
    default: return {0.f, 1.f, 1.f};
    }
}

const LDActionMotion::FOwnMotion* LDActionMotion::OwnMotionOf(uint8 Ability)
{
    // Strike frames measured as the striking hand's furthest reach from the chest (push and block: the held extension).
    using A = ELDDemoAbilityId;
    static const FOwnMotion Motions[] = {
        {uint8(A::ActiveTripleCombo), TEXT("QN_Punch_Jab"), 0.f, .2f, .6f, true},
        {uint8(A::ActiveBailOut), TEXT("QN_Punch_Cross"), 0.f, .27f, .7f, false},
        {uint8(A::ActiveFlurryRush), TEXT("QN_Punch_Cross"), 0.f, .27f, .7f, false},
        {uint8(A::ActiveElbow), TEXT("QN_Melee_Hook"), 0.f, .27f, .47f, false},
        {uint8(A::ActiveSnarkBolt), TEXT("QN_OverhandThrow"), 0.f, .37f, .9f, false},
        {uint8(A::ActiveOpenMic), TEXT("QN_Push_Loop"), .2f, .6f, 1.2f, false},
        {uint8(A::ActiveDeskFan), TEXT("QN_Push_Loop"), .2f, .6f, 1.2f, false},
        {uint8(A::ActiveChargedRam), TEXT("QN_Shield_Dash"), 0.f, .35f, .9f, false},
        {uint8(A::ActiveNotMad), TEXT("QN_Shield_OneShot"), 0.f, .6f, .83f, true},
        {uint8(A::ActiveBrace), TEXT("QN_Sword_Block"), 0.f, .27f, .9f, false},
        {uint8(A::ActiveUnplug), TEXT("QN_Sword_Block"), 0.f, .27f, .9f, false},
        {uint8(A::ActiveCrotchGrab), TEXT("QN_Slide_Start"), 0.f, .43f, .83f, false},
    };
    for (const FOwnMotion& Motion : Motions) { if (Motion.Ability == Ability) { return &Motion; } }
    return nullptr;
}
