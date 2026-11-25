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

LDActionMotion::FBlend LDActionMotion::BlendOf(EFamily Family, bool bMoving)
{
    switch (Family)
    {
    // A jab stays light over the stance; the cross turns the chest further.
    case EFamily::Jab: return {0.f, .42f, 1.f};
    case EFamily::Cross: return {0.f, .7f, 1.f};
    case EFamily::OneHandThrow: return {0.f, 1.f, 1.f};
    case EFamily::Ready: return {0.f, .8f, .8f};
    // The finisher, set shots and slams shift the whole weight when standing.
    case EFamily::Finisher:
    case EFamily::TwoHandShot:
    case EFamily::HeavySlam: return bMoving ? FBlend{0.f, 1.f, 1.f} : FBlend{1.f, 0.f, 0.f};
    default: return {0.f, 1.f, 1.f};
    }
}
