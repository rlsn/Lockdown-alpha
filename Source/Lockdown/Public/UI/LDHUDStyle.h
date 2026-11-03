#pragma once
#include "CoreMinimal.h"

// Shared in-match presentation tokens, so the HUD band, overhead bars, world feedback and minimap keep one visual
// language: the own body reads green, other contenders red, camp members amber, mana blue and 淡定 the bright cyan of
// the body's shield marker.
// Local presentation only; no gameplay value or rule lives here. Sizes are Slate units at the 1080p reference.
namespace LDHUDStyle
{
    // Surfaces and text
    inline const FLinearColor PanelFill(.025f,.045f,.07f,.94f);
    inline const FLinearColor PanelRim(.42f,.55f,.62f,.45f);
    inline const FLinearColor TextPrimary(.94f,.96f,.98f);
    inline const FLinearColor TextSecondary(.70f,.78f,.84f);
    inline const FLinearColor TextAccent(1.f,.82f,.40f);
    inline const FLinearColor TextWarning(1.f,.70f,.45f);
    inline const FLinearColor TextDanger(1.f,.47f,.42f);
    // Outline behind world-anchored text and bars, so they read over any ground or body.
    inline const FLinearColor Rim(0.f,0.f,0.f,.85f);

    // Resources. Values are linear (Slate converts to sRGB), so saturated fills need low channels; white bar text with a
    // dark outline then keeps its contrast.
    inline const FLinearColor OwnHealth(.05f,.45f,.08f);
    inline const FLinearColor EnemyHealth(.70f,.07f,.05f);
    inline const FLinearColor CampHealth(.85f,.32f,.03f);
    inline const FLinearColor Mana(.05f,.22f,.85f);
    inline const FLinearColor Shield(0.f,.78f,1.f);
    inline const FLinearColor Experience(.70f,.45f,.05f);
    inline const FLinearColor BarTrack(.04f,.05f,.07f,.85f);

    // Floating feedback
    inline const FLinearColor DamageDealt(1.f,.93f,.80f);
    inline const FLinearColor DamageTaken(1.f,.36f,.30f);
    inline const FLinearColor Heal(.45f,.95f,.50f);
    inline const FLinearColor ShieldGain(.45f,.90f,1.f);
    inline const FLinearColor GoldGain(1.f,.84f,.30f);
    inline const FLinearColor ExperienceGain(.80f,.66f,1.f);
    inline const FLinearColor Celebration(1.f,.86f,.36f);

    // Action slot states: each limit has its own fill, and the slot text names it, so no state relies on colour alone.
    inline const FLinearColor SlotReady(.06f,.22f,.24f,.98f);
    inline const FLinearColor SlotArmed(.36f,.27f,.06f,.98f);
    inline const FLinearColor SlotBlocked(.05f,.06f,.08f,.96f);
    inline const FLinearColor SlotNoMana(.08f,.13f,.34f,.98f);
    inline const FLinearColor SlotControlled(.28f,.10f,.32f,.98f);
    inline const FLinearColor SlotCooldownMask(0.f,0.f,0.f,.62f);
    inline const FLinearColor SlotReadyFlash(1.f,.92f,.55f);

    // Type scale
    constexpr int32 TypeCaption=13;
    constexpr int32 TypeBody=15;
    constexpr int32 TypeLabel=17;
    constexpr int32 TypeTitle=21;
    constexpr int32 TypeBanner=32;
    constexpr int32 TypeHero=44;

    // Local presentation timings (real seconds; never compared with an authority deadline).
    constexpr double FloatSeconds=1.1;
    constexpr double BannerSeconds=2.4;
    constexpr double ReadyFlashSeconds=.45;
}
