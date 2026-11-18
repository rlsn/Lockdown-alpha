#include "Character/LDHitVfx.h"
#include "Combat/LDCombatCues.h"

namespace LDHitVfxDetail
{
    using namespace LDHitVfx;
    // Linear colours. Glow stays under ~4 so bloom keeps the hue on bright floors; Ink carries the saturated body colour.
    const FLinearColor WarmGlow(3.2f, 2.2f, 1.1f), WarmInk(.5f, .3f, .1f);
    const FLinearColor GoldGlow(3.6f, 1.7f, .25f), GoldInk(.7f, .35f, .03f);
    const FLinearColor CyanGlow(.2f, 1.8f, 3.5f), CyanInk(0.f, .35f, .7f);
    const FLinearColor FireGlow(2.2f, .38f, .04f), BurnInk(.85f, .1f, .01f);
    const FLinearColor WaterGlow(.25f, 1.4f, 3.5f), WaterInk(.02f, .3f, .75f);
    const FLinearColor BoltGlow(3.f, 3.f, 1.2f), BoltInk(.6f, .6f, .15f);
    const FLinearColor MagicGlow(2.2f, .6f, 3.8f), MagicInk(.6f, .1f, 1.f);
    const FLinearColor KillGlow(5.f, .7f, .25f), CrackInk(.012f, .008f, .006f), CrackGlow(3.5f, .8f, .12f);

    FLayer Make(EMask Mask, EPlace Place, float SizeCm, float Seconds, const FLinearColor& Glow, const FLinearColor& Ink,
        float FromScale = 1.f, float ToScale = 1.f, float PeakAt = .2f)
    {
        FLayer Layer;
        Layer.Mask = Mask;
        Layer.Place = Place;
        Layer.SizeCm = SizeCm;
        Layer.Seconds = Seconds;
        Layer.Glow = Glow;
        Layer.Ink = Ink;
        Layer.FromScale = FromScale;
        Layer.ToScale = ToScale;
        Layer.PeakAt = PeakAt;
        return Layer;
    }

    FLinearColor LookGlow(ELDObservedHitLook Look)
    {
        switch (Look)
        {
        case ELDObservedHitLook::Magic: return MagicGlow;
        case ELDObservedHitLook::Fire: return FireGlow;
        case ELDObservedHitLook::Water: return WaterGlow;
        case ELDObservedHitLook::Electric: return BoltGlow;
        default: return WarmGlow;
        }
    }

    // Reduced effects: the optional layers go and the rest keeps its shape at a lower brightness.
    void Thin(FRecipe& Recipe, float EffectScale)
    {
        const float Thinness = FMath::Clamp(EffectScale, 0.f, 1.f);
        const float Dim = .55f + .45f * Thinness;
        Recipe.Layers.RemoveAll([Thinness](const FLayer& Layer) { return Layer.bOptional && Thinness < 1.f; });
        for (FLayer& Layer : Recipe.Layers)
        {
            Layer.Glow *= Dim;
            Layer.Ink *= Dim;
        }
    }
}

const TCHAR* LDHitVfx::MaskId(EMask Mask)
{
    switch (Mask)
    {
    case EMask::Star: return TEXT("hit_star");
    case EMask::Cross: return TEXT("hit_cross");
    case EMask::Ring: return TEXT("hit_ring");
    case EMask::Ripple: return TEXT("hit_ripple");
    case EMask::Burn: return TEXT("hit_burn");
    case EMask::Sigil: return TEXT("hit_sigil");
    case EMask::Crack: return TEXT("hit_crack");
    case EMask::Bolt: return TEXT("hit_bolt");
    case EMask::Hex: return TEXT("hit_hex");
    case EMask::ScreenEdge: return TEXT("screen_edge");
    default: return TEXT("");
    }
}

FString LDHitVfx::TexturePackage(const TCHAR* Id)
{
    return FString(Folder) / TEXT("T_") + Id;
}

LDHitVfx::FRecipe LDHitVfx::ForHit(ELDObservedHitLook Look, uint8 HitFlags, float EffectScale)
{
    using namespace LDHitVfxDetail;
    FRecipe Recipe;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f) { return Recipe; }
    const bool bCritical = (HitFlags & LDObservation::Critical) != 0;
    const bool bBroken = (HitFlags & LDObservation::ShieldBroken) != 0;
    const bool bShield = bBroken || (HitFlags & LDObservation::ShieldAbsorbed) != 0;

    // The flash star: a shield's small cyan star, a critical gold cross, a magic violet cross, else the look's star.
    if (bShield) { Recipe.Layers.Add(Make(EMask::Star, EPlace::Sprite, 80.f, .16f, CyanGlow, CyanInk, .6f, 1.f)); }
    else if (bCritical)
    {
        FLayer Cross = Make(EMask::Cross, EPlace::Sprite, 190.f, .26f, GoldGlow, GoldInk, .5f, 1.f, .16f);
        Cross.SpinDegPerSec = 90.f;
        Recipe.Layers.Add(Cross);
    }
    else if (Look == ELDObservedHitLook::Magic) { Recipe.Layers.Add(Make(EMask::Cross, EPlace::Sprite, 120.f, .22f, MagicGlow, MagicInk, .55f, 1.f)); }
    else
    {
        const float Size = Look == ELDObservedHitLook::Water ? 80.f : 120.f;
        Recipe.Layers.Add(Make(EMask::Star, EPlace::Sprite, Size, .18f, LookGlow(Look), WarmInk, .55f, 1.f));
    }

    // The shield shell shows on the hit; a broken one bursts outwards over a wide cyan ring.
    if (bShield)
    {
        Recipe.Layers.Add(Make(EMask::Hex, EPlace::Shell, 150.f, bBroken ? .32f : .26f, CyanGlow, FLinearColor::Black,
            bBroken ? 1.f : .96f, bBroken ? 1.55f : 1.04f, .12f));
    }

    // The ground: a broken shield's wide ring, a critical double gold ring, else a warm ring unless the look owns the ground.
    if (bBroken) { Recipe.Layers.Add(Make(EMask::Ring, EPlace::Ground, 300.f, .4f, CyanGlow, CyanInk, .3f, 1.f, .1f)); }
    else if (bCritical)
    {
        Recipe.Layers.Add(Make(EMask::Ring, EPlace::Ground, 210.f, .36f, GoldGlow, GoldInk, .3f, 1.f, .1f));
        FLayer Echo = Make(EMask::Ring, EPlace::Ground, 270.f, .36f, GoldGlow * .6f, GoldInk, .3f, 1.f, .1f);
        Echo.DelaySeconds = .07f;
        Echo.bOptional = true;
        Recipe.Layers.Add(Echo);
    }
    else if (Look == ELDObservedHitLook::None || Look == ELDObservedHitLook::Electric)
    {
        Recipe.Layers.Add(Make(EMask::Ring, EPlace::Ground, 170.f, .32f, WarmGlow * .8f, WarmInk, .35f, 1.f, .1f));
    }

    // The look's signature.
    switch (Look)
    {
    case ELDObservedHitLook::Fire:
        Recipe.Layers.Add(Make(EMask::Burn, EPlace::Ground, 165.f, .6f, FireGlow, BurnInk, .6f, 1.f, .15f));
        break;
    case ELDObservedHitLook::Water:
        Recipe.Layers.Add(Make(EMask::Ripple, EPlace::Ground, 230.f, .6f, WaterGlow, WaterInk, .3f, 1.f, .12f));
        break;
    case ELDObservedHitLook::Electric:
        Recipe.Layers.Add(Make(EMask::Bolt, EPlace::Upright, 110.f, .18f, BoltGlow, BoltInk, 1.f, 1.f, .1f));
        break;
    case ELDObservedHitLook::Magic:
    {
        FLayer Sigil = Make(EMask::Sigil, EPlace::Ground, 210.f, .65f, MagicGlow, MagicInk, .7f, 1.f, .15f);
        Sigil.SpinDegPerSec = 60.f;
        Recipe.Layers.Add(Sigil);
        break;
    }
    default: break;
    }
    Thin(Recipe, EffectScale);
    return Recipe;
}

LDHitVfx::FRecipe LDHitVfx::ForKill(float EffectScale)
{
    using namespace LDHitVfxDetail;
    FRecipe Recipe;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f) { return Recipe; }
    Recipe.Layers.Add(Make(EMask::Crack, EPlace::Ground, 260.f, 2.f, CrackGlow, CrackInk, .85f, 1.f, .05f));
    Recipe.Layers.Add(Make(EMask::Ring, EPlace::Ground, 260.f, .4f, KillGlow, WarmInk, .25f, 1.f, .08f));
    FLayer Echo = Make(EMask::Ring, EPlace::Ground, 360.f, .45f, KillGlow * .6f, WarmInk, .25f, 1.f, .08f);
    Echo.DelaySeconds = .09f;
    Echo.bOptional = true;
    Recipe.Layers.Add(Echo);
    Thin(Recipe, EffectScale);
    return Recipe;
}

float LDHitVfx::Brightness(const FLayer& Layer, float AgeSeconds)
{
    const float Age = AgeSeconds - Layer.DelaySeconds;
    if (!FMath::IsFinite(Age) || Age < 0.f || Layer.Seconds <= 0.f || Age >= Layer.Seconds) { return 0.f; }
    const float Progress = Age / Layer.Seconds;
    const float Peak = FMath::Clamp(Layer.PeakAt, .01f, .99f);
    if (Progress < Peak) { return FMath::Sqrt(Progress / Peak); }
    return FMath::Pow(1.f - (Progress - Peak) / (1.f - Peak), 1.6f);
}

float LDHitVfx::ScaleAt(const FLayer& Layer, float AgeSeconds)
{
    const float Progress = Layer.Seconds > 0.f ? FMath::Clamp((AgeSeconds - Layer.DelaySeconds) / Layer.Seconds, 0.f, 1.f) : 1.f;
    return FMath::Lerp(Layer.FromScale, Layer.ToScale, 1.f - FMath::Pow(1.f - Progress, 3.f));
}

float LDHitVfx::SpriteCm(float SizeCm, float DistanceCm, float FovDegrees)
{
    if (!FMath::IsFinite(DistanceCm) || !FMath::IsFinite(FovDegrees) || DistanceCm <= 0.f || FovDegrees <= 0.f || FovDegrees >= 170.f)
    { return SizeCm; }
    const float ViewCm = 2.f * DistanceCm * FMath::Tan(FMath::DegreesToRadians(FovDegrees) * .5f);
    return FMath::Max(SizeCm, MinSpritePx / 1080.f * ViewCm);
}

LDHitVfx::FScreenFlash LDHitVfx::ScreenFlashOf(uint8 DealtFlags, bool bReduced)
{
    FScreenFlash Flash;
    if (bReduced) { return Flash; }
    if (DealtFlags & LDCombatCues::DealtLethal) { Flash = {FLinearColor(1.f, .12f, .06f, .55f), .24f, 3.5f}; }
    else if (DealtFlags & LDObservation::Critical) { Flash = {FLinearColor(1.f, .62f, .12f, .45f), .12f, 2.5f}; }
    else if (DealtFlags & LDObservation::ShieldBroken) { Flash = {FLinearColor(.1f, .8f, 1.f, .4f), .14f, 1.5f}; }
    return Flash;
}

float LDHitVfx::ScreenFlashAlpha(const FScreenFlash& Flash, float AgeSeconds)
{
    if (Flash.Seconds <= 0.f || !FMath::IsFinite(AgeSeconds) || AgeSeconds < 0.f || AgeSeconds >= Flash.Seconds) { return 0.f; }
    return Flash.Colour.A * FMath::Square(1.f - AgeSeconds / Flash.Seconds);
}
