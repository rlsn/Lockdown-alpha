#pragma once
#include "CoreMinimal.h"
#include "Vision/LDViewTypes.h"

class UAudioComponent;
class UObject;

// Body sounds on the presentation hooks the body effects already use (AUDIO_AND_FEEL §2 sync table). The caller is the
// body presentation (ULDHumanoidVisualComponent) of the own body or of a sight-authorised proxy, so a sound never reveals
// a body the local player cannot see. The own body plays 2D at full level; another body plays at its location, quieter.
// Presentation only: nothing here reads or changes gameplay state or timing.
namespace LDAudioFeel
{
    // Others' sounds relative to the own body's: blows and states quieter, but what threatens the local player (another's
    // skill windup and release, a reaction) stays near full so a danger is never downgraded just for being someone else's.
    constexpr float OtherGain = .7f;
    constexpr float OtherDangerGain = .9f;
    constexpr float OtherStateGain = .55f;
    // A swing's whoosh starts this long before its release so its peak lands on the release frame; never before 80 % of
    // the windup, so a predicted swing held at 75 % (LDHitFeel::SwingHoldFraction) stays silent until confirmed.
    LOCKDOWN_API float SwingPreRollSeconds(float WindupSeconds);
    // A cast this long or longer charges audibly over its windup.
    constexpr float ChargeMinSeconds = .12f;
    // The charge fades out over this long at the release.
    constexpr float ChargeCutSeconds = .03f;

    // A basic swing at its release (combo step 1-3; 3 is the finisher).
    LOCKDOWN_API void Swing(const UObject& Body, bool bOwnBody, uint8 ComboStep, const FVector& At);
    // The start of a cast windup long enough to charge; the returned sound is cut at the release (null = none plays).
    LOCKDOWN_API UAudioComponent* Charge(const UObject& Body, bool bOwnBody, const FVector& At);
    LOCKDOWN_API void CutCharge(UAudioComponent* Charge);
    // A skill (ELDDemoAbilityId) released.
    LOCKDOWN_API void Release(const UObject& Body, bool bOwnBody, uint8 Ability, const FVector& At);
    // A resolved hit on another visible body (the own body's hurt sounds from the owner tracker): its look, its outcome
    // flags (LDObservation::EHitFlags), whether the strike it belongs to is a combo finisher, and the skill (ELDDemoAbilityId)
    // whose visible strike the effects matched it to (0 = none): a skill with its own hit sound plays that.
    LOCKDOWN_API void Impact(const UObject& Body, ELDObservedHitLook Look, uint8 HitFlags, bool bFinisher, uint8 Ability, const FVector& At);
    // An element reaction (ELDCombatReaction) on a visible body, at its own time.
    LOCKDOWN_API void Reaction(const UObject& Body, bool bOwnBody, uint8 Reaction, const FVector& At);
    // An element state starting on a visible body: Index in burn, wet, electrified, chilled, rotten order.
    LOCKDOWN_API void StateOnset(const UObject& Body, bool bOwnBody, int32 Index, const FVector& At);
    // Another visible body knocked out, at its fall (the own death sounds from the owner tracker).
    LOCKDOWN_API void Fall(const UObject& Body, const FVector& At);
    // The owner's confirmation of its own landed hit (the receipt), by tier, at the trims of the effects tab.
    LOCKDOWN_API void Confirm(const UObject& Context, uint8 HitFlags, float Volume);
}
