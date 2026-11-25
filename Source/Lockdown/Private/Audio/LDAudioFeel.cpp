#include "Audio/LDAudioFeel.h"
#include "Audio/LDAudioSubsystem.h"
#include "Character/LDHitFeel.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{
    ULDAudioSubsystem* AudioOf(const UObject& Context)
    {
        const UWorld* World = Context.GetWorld();
        UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
        return Instance ? Instance->GetSubsystem<ULDAudioSubsystem>() : nullptr;
    }

    void Body(ULDAudioSubsystem& Audio, ELDAudioCue Cue, bool bOwnBody, const FVector& At, float Volume = 1.f, float Pitch = 1.f)
    {
        if (bOwnBody) { Audio.Play(Cue, Volume, Pitch); }
        else { Audio.PlayAt(Cue, At, Volume * LDAudioFeel::OtherGain, Pitch); }
    }
}

float LDAudioFeel::SwingPreRollSeconds(float WindupSeconds)
{
    return FMath::Clamp(.2f * WindupSeconds, 0.f, .04f);
}

void LDAudioFeel::Swing(const UObject& BodyContext, bool bOwnBody, uint8 ComboStep, const FVector& At)
{
    if (ULDAudioSubsystem* Audio = AudioOf(BodyContext))
    {
        // Steps 1 and 2 alternate a little in pitch so a combo reads as two different blows, not one repeated sample.
        const bool bFinisher = ComboStep >= 3;
        Body(*Audio, bFinisher ? ELDAudioCue::SwingFinisher : ELDAudioCue::SwingLight, bOwnBody, At,
            LDHitFeel::Read().SwingVolume, bFinisher ? 1.f : ComboStep == 2 ? .94f : 1.f);
    }
}

UAudioComponent* LDAudioFeel::Charge(const UObject& BodyContext, bool bOwnBody, const FVector& At)
{
    ULDAudioSubsystem* Audio = AudioOf(BodyContext);
    return Audio ? Audio->Start(ELDAudioCue::CastCharge, !bOwnBody, At, bOwnBody ? 1.f : OtherGain) : nullptr;
}

void LDAudioFeel::CutCharge(UAudioComponent* Charge)
{
    if (IsValid(Charge) && Charge->IsPlaying()) { Charge->FadeOut(ChargeCutSeconds, 0.f); }
}

void LDAudioFeel::Release(const UObject& BodyContext, bool bOwnBody, uint8 Ability, bool bOverdrive, const FVector& At)
{
    if (ULDAudioSubsystem* Audio = AudioOf(BodyContext))
    {
        Audio->PlaySkillRelease(Ability, !bOwnBody, At, bOwnBody ? 1.f : OtherGain);
        if (bOverdrive) { Body(*Audio, ELDAudioCue::SwingFinisher, bOwnBody, At, .8f, .8f); }
    }
}

void LDAudioFeel::Impact(const UObject& BodyContext, ELDObservedHitLook Look, uint8 HitFlags, bool bFinisher, const FVector& At)
{
    ULDAudioSubsystem* Audio = AudioOf(BodyContext);
    if (!Audio) { return; }
    const ELDAudioCue Cue = (HitFlags & LDObservation::Dodged) ? ELDAudioCue::SelfPerfectDodge
        : (HitFlags & LDObservation::Parried) ? ELDAudioCue::SelfParry
        : (HitFlags & LDObservation::ShieldBroken) ? ELDAudioCue::SelfShieldBreak
        : (HitFlags & LDObservation::ShieldAbsorbed) ? ELDAudioCue::SelfShieldHit
        : Look == ELDObservedHitLook::Electric ? ELDAudioCue::HitElectric
        : Look == ELDObservedHitLook::Fire ? ELDAudioCue::HitFire
        : Look == ELDObservedHitLook::Water ? ELDAudioCue::HitWater
        : Look == ELDObservedHitLook::Magic ? ELDAudioCue::HitMagic
        : bFinisher ? ELDAudioCue::HitFinisher : ELDAudioCue::HitBasic;
    Audio->PlayAt(Cue, At, OtherGain * ((HitFlags & LDObservation::Critical) ? 1.25f : 1.f));
}

void LDAudioFeel::Confirm(const UObject& Context, uint8 HitFlags, float Volume)
{
    ULDAudioSubsystem* Audio = AudioOf(Context);
    if (!Audio) { return; }
    // The impact body under every own landed hit; its accent names the tier (an ordinary hit, a critical hit, a kill).
    Audio->Play(ELDAudioCue::SelfDealtImpact, Volume);
    switch (LDHitFeel::TierOf(HitFlags))
    {
    case LDHitFeel::ETier::Kill: Audio->Play(ELDAudioCue::SelfDealtKill, Volume); break;
    case LDHitFeel::ETier::Critical: Audio->Play(ELDAudioCue::SelfDealtCrit, Volume); break;
    case LDHitFeel::ETier::ShieldBreak: Audio->Play(ELDAudioCue::SelfShieldBreak, Volume); break;
    default: Audio->Play(ELDAudioCue::SelfDealtAccent, Volume); break;
    }
}
