#include "Character/LDHitVfx.h"
#include "Character/LDHitFeel.h"
#include "Combat/LDCombatCues.h"
#include "Combat/LDCombatElements.h"
#include "Development/LDDemoContentIds.h"

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

    // Live and saved tuning: sizes, durations (with their delays) and glow scale together; no size or time shows nothing.
    void Tune(FRecipe& Recipe, const FLayerTuning& Tuning)
    {
        if (!FMath::IsFinite(Tuning.Size) || !FMath::IsFinite(Tuning.Seconds) || !FMath::IsFinite(Tuning.Glow)
            || Tuning.Size <= 0.f || Tuning.Seconds <= 0.f || Tuning.Glow <= 0.f)
        {
            Recipe.Layers.Reset();
            return;
        }
        for (FLayer& Layer : Recipe.Layers)
        {
            Layer.SizeCm *= Tuning.Size;
            Layer.Seconds *= Tuning.Seconds;
            Layer.DelaySeconds *= Tuning.Seconds;
            Layer.SpinDegPerSec /= Tuning.Seconds;
            Layer.Glow *= Tuning.Glow;
        }
    }

    float StartOf(const TCHAR* ConsoleName, float Fallback)
    {
        for (const LDHitFeel::FField& Field : LDHitFeel::Fields())
        {
            if (FCString::Strcmp(Field.ConsoleName, ConsoleName) == 0) { return Field.Default; }
        }
        return Fallback;
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

LDHitVfx::FLayerTuning LDHitVfx::TuningOf(const LDHitFeel::FTuning& Feel, float AssetSize, float AssetSeconds, float AssetGlow)
{
    using namespace LDHitVfxDetail;
    FLayerTuning Out;
    // ImpactSeconds is in seconds; the field starts in milliseconds.
    const float SizeStart = StartOf(TEXT("ld.Feel.ImpactScale"), 1.f);
    const float SecondsStart = StartOf(TEXT("ld.Feel.ImpactMs"), 1000.f) / 1000.f;
    const float GlowStart = StartOf(TEXT("ld.Feel.ImpactGlow"), 1.f);
    Out.Size = AssetSize * (SizeStart > 0.f ? Feel.ImpactScale / SizeStart : 0.f);
    Out.Seconds = AssetSeconds * (SecondsStart > 0.f ? Feel.ImpactSeconds / SecondsStart : 0.f);
    Out.Glow = AssetGlow * (GlowStart > 0.f ? Feel.ImpactGlow / GlowStart : 0.f);
    return Out;
}

LDHitVfx::FRecipe LDHitVfx::ForHit(ELDObservedHitLook Look, uint8 HitFlags, float EffectScale, const FLayerTuning& Tuning)
{
    using namespace LDHitVfxDetail;
    FRecipe Recipe;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f) { return Recipe; }
    const bool bCritical = (HitFlags & LDObservation::Critical) != 0;
    const bool bBroken = (HitFlags & LDObservation::ShieldBroken) != 0;
    const bool bShield = bBroken || (HitFlags & LDObservation::ShieldAbsorbed) != 0;
    const bool bOverdrive = (HitFlags & LDObservation::Overdriven) != 0;
    const FLinearColor OverdriveGlow = OverdriveTint() * 3.2f, OverdriveInk = OverdriveTint() * .5f;

    // The flash star: an overdrive's large magenta cross, a shield's small cyan star, a critical gold cross, a magic
    // violet cross, else the look's star.
    if (bOverdrive)
    {
        FLayer Cross = Make(EMask::Cross, EPlace::Sprite, 220.f, .3f, OverdriveGlow, OverdriveInk, .45f, 1.f, .14f);
        Cross.SpinDegPerSec = 140.f;
        Recipe.Layers.Add(Cross);
    }
    else if (bShield) { Recipe.Layers.Add(Make(EMask::Star, EPlace::Sprite, 80.f, .16f, CyanGlow, CyanInk, .6f, 1.f)); }
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
    else if (!bOverdrive && (Look == ELDObservedHitLook::None || Look == ELDObservedHitLook::Electric))
    {
        Recipe.Layers.Add(Make(EMask::Ring, EPlace::Ground, 170.f, .32f, WarmGlow * .8f, WarmInk, .35f, 1.f, .1f));
    }

    // Overdrive owns its wide ring and a spinning sigil under the struck body.
    if (bOverdrive)
    {
        Recipe.Layers.Add(Make(EMask::Ring, EPlace::Ground, 340.f, .42f, OverdriveGlow, OverdriveInk, .25f, 1.f, .08f));
        FLayer Sigil = Make(EMask::Sigil, EPlace::Ground, 230.f, .55f, OverdriveGlow * .8f, OverdriveInk, .8f, 1.05f, .12f);
        Sigil.SpinDegPerSec = -120.f;
        Recipe.Layers.Add(Sigil);
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
    Tune(Recipe, Tuning);
    return Recipe;
}

LDHitVfx::FRecipe LDHitVfx::ForKill(float EffectScale, const FLayerTuning& Tuning)
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
    Tune(Recipe, Tuning);
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

float LDHitVfx::SpriteCompensation(float DistanceCm, float FovDegrees)
{
    if (!FMath::IsFinite(DistanceCm) || !FMath::IsFinite(FovDegrees) || DistanceCm <= 0.f || FovDegrees <= 0.f || FovDegrees >= 170.f)
    { return 1.f; }
    const float ViewCm = 2.f * DistanceCm * FMath::Tan(FMath::DegreesToRadians(FovDegrees) * .5f);
    return FMath::Max(1.f, MinSpritePx / 1080.f * ViewCm / ReferenceSpriteCm);
}

LDHitVfx::FScreenFlash LDHitVfx::ScreenFlashOf(uint8 DealtFlags, bool bReduced)
{
    FScreenFlash Flash;
    if (bReduced) { return Flash; }
    if (DealtFlags & LDCombatCues::DealtLethal) { Flash = {FLinearColor(1.f, .12f, .06f, .55f), .24f, 3.5f}; }
    else if (DealtFlags & LDCombatCues::DealtOverdrive) { Flash = {FLinearColor(1.f, .2f, .8f, .5f), .2f, 3.f}; }
    else if (DealtFlags & LDObservation::Critical) { Flash = {FLinearColor(1.f, .62f, .12f, .45f), .12f, 2.5f}; }
    else if (DealtFlags & LDObservation::ShieldBroken) { Flash = {FLinearColor(.1f, .8f, 1.f, .4f), .14f, 1.5f}; }
    return Flash;
}

float LDHitVfx::ScreenFlashAlpha(const FScreenFlash& Flash, float AgeSeconds)
{
    if (Flash.Seconds <= 0.f || !FMath::IsFinite(AgeSeconds) || AgeSeconds < 0.f || AgeSeconds >= Flash.Seconds) { return 0.f; }
    return Flash.Colour.A * FMath::Square(1.f - AgeSeconds / Flash.Seconds);
}

LDHitVfx::FScreenFlash LDHitVfx::HurtFlashOf(bool bReduced)
{
    FScreenFlash Flash;
    if (!bReduced) { Flash = {FLinearColor(.85f, .04f, .03f, .22f), .2f, 0.f}; }
    return Flash;
}

namespace LDHitVfxDetail
{
    // A tint as particle colour: HDR enough to bloom while keeping its hue.
    FLinearColor Hot(const FLinearColor& Tint, float Glow, float Alpha = 1.f)
    {
        return FLinearColor(Tint.R * Glow, Tint.G * Glow, Tint.B * Glow, Alpha);
    }

    const FLinearColor DustGrey(.16f, .14f, .12f, .5f);
}

LDHitVfx::FBurst LDHitVfx::BurstOf(EBurst Kind, const FLinearColor& Tint, float EffectScale)
{
    using namespace LDHitVfxDetail;
    FBurst B;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f) { return B; }
    B.SparkColour = Hot(Tint, 3.f);
    B.CoreColour = Hot(Tint, 2.2f, .8f);
    B.SmokeColour = DustGrey;
    B.ShardColour = FLinearColor(.12f, .1f, .08f, 1.f);
    switch (Kind)
    {
    case EBurst::Strike: B.Sparks = 14; B.Smoke = 3; B.Speed = 1.1f; break;
    case EBurst::Muzzle:
        B.Sparks = 6; B.Smoke = 7; B.Speed = .7f; B.Scale = .8f; B.Life = .8f;
        B.SmokeColour = FLinearColor(Tint.R * .5f + .3f, Tint.G * .5f + .3f, Tint.B * .5f + .3f, .45f);
        break;
    case EBurst::Flame:
        B.Sparks = 22; B.Smoke = 7; B.Speed = .55f; B.Scale = 1.3f; B.Life = 1.7f; B.Drag = 2.2f; B.Gravity = FVector(0, 0, 320);
        B.SmokeColour = FLinearColor(.4f, .08f, .015f, .6f);
        break;
    case EBurst::Splash:
        B.Sparks = 24; B.Smoke = 4; B.Speed = .95f; B.Life = 1.2f; B.Drag = .4f; B.Gravity = FVector(0, 0, -1900);
        B.SmokeColour = FLinearColor(.5f, .7f, .85f, .35f);
        break;
    case EBurst::Zap:
        B.Sparks = 22; B.Smoke = 2; B.Speed = 1.9f; B.Scale = .8f; B.Life = .45f; B.Drag = 3.f; B.Gravity = FVector::ZeroVector;
        B.CoreColour = Hot(Tint, 1.4f, .45f);
        B.SmokeColour = FLinearColor(Tint.R * .5f, Tint.G * .5f, Tint.B * .3f, .45f);
        break;
    case EBurst::Frost:
        B.Sparks = 14; B.Smoke = 7; B.Speed = .5f; B.Life = 1.5f; B.Drag = 2.5f; B.Gravity = FVector(0, 0, -200);
        B.SmokeColour = FLinearColor(.75f, .88f, 1.f, .4f);
        break;
    case EBurst::Rot:
        B.Sparks = 12; B.Smoke = 7; B.Speed = .45f; B.Life = 1.6f; B.Drag = 2.f; B.Gravity = FVector(0, 0, -350);
        B.SmokeColour = FLinearColor(.22f, .3f, .08f, .5f);
        break;
    case EBurst::Gust:
        B.Sparks = 10; B.Smoke = 11; B.Speed = 1.4f; B.Scale = 1.3f; B.Life = 1.2f; B.Drag = 1.2f; B.Gravity = FVector::ZeroVector;
        B.SmokeColour = FLinearColor(.6f, .64f, .66f, .4f);
        break;
    case EBurst::Arcane:
        B.Sparks = 18; B.Smoke = 5; B.Speed = .5f; B.Scale = 1.2f; B.Life = 1.6f; B.Drag = 2.5f; B.Gravity = FVector(0, 0, 200);
        B.SmokeColour = Hot(Tint, .55f, .55f);
        break;
    case EBurst::Dash:
        B.Sparks = 6; B.Smoke = 10; B.Speed = .8f; B.Scale = 1.2f; B.Life = 1.1f;
        B.SmokeColour = FLinearColor(.22f, .19f, .15f, .55f);
        break;
    case EBurst::Aura:
        B.Sparks = 20; B.Smoke = 6; B.Speed = .6f; B.Scale = 1.3f; B.Life = 1.3f; B.Drag = 2.f; B.Gravity = FVector(0, 0, 420);
        B.SmokeColour = Hot(Tint, .6f, .55f);
        break;
    case EBurst::Slam:
        B.Sparks = 12; B.Smoke = 12; B.Shards = 10; B.Speed = 1.f; B.Scale = 1.5f; B.Life = 1.2f; B.Gravity = FVector(0, 0, -1400);
        B.SmokeColour = FLinearColor(.24f, .21f, .17f, .6f);
        B.ShardColour = FLinearColor(.2f, .17f, .13f, 1.f);
        break;
    case EBurst::Overdrive:
        B.Sparks = 34; B.Smoke = 10; B.Shards = 8; B.Speed = 1.6f; B.Scale = 1.8f; B.Life = 1.3f; B.Gravity = FVector(0, 0, -600);
        B.SparkColour = Hot(OverdriveTint(), 3.2f);
        B.CoreColour = Hot(OverdriveTint(), 2.4f, .7f);
        B.SmokeColour = FLinearColor(.35f, .04f, .25f, .6f);
        B.ShardColour = FLinearColor(.3f, .05f, .22f, 1.f);
        break;
    case EBurst::Kill:
        B.Sparks = 28; B.Smoke = 14; B.Shards = 14; B.Speed = 1.4f; B.Scale = 1.7f; B.Life = 1.3f; B.Gravity = FVector(0, 0, -1200);
        B.SmokeColour = FLinearColor(.1f, .07f, .06f, .65f);
        break;
    default: return FBurst();
    }
    const float Thin = FMath::Clamp(EffectScale, 0.f, 1.f);
    B.Sparks = FMath::RoundToInt(float(B.Sparks) * Thin);
    B.Smoke = FMath::RoundToInt(float(B.Smoke) * Thin);
    B.Shards = FMath::RoundToInt(float(B.Shards) * Thin);
    return B;
}

bool LDHitVfx::HitBurstOf(ELDObservedHitLook Look, EBurst& Out, FLinearColor& Tint)
{
    switch (Look)
    {
    case ELDObservedHitLook::Fire: Out = EBurst::Flame; Tint = FLinearColor(1.f, .32f, .05f); return true;
    case ELDObservedHitLook::Water: Out = EBurst::Splash; Tint = FLinearColor(.25f, .7f, 1.f); return true;
    case ELDObservedHitLook::Electric: Out = EBurst::Zap; Tint = FLinearColor(1.f, .9f, .3f); return true;
    case ELDObservedHitLook::Magic: Out = EBurst::Arcane; Tint = FLinearColor(.7f, .3f, 1.f); return true;
    default: return false;
    }
}

TConstArrayView<LDHitVfx::FSkillLook> LDHitVfx::SkillLooks()
{
    using A = ELDDemoAbilityId;
    static const FSkillLook Table[] = {
        {uint8(A::ActiveElbow), TEXT("active_elbow"), FLinearColor(1.f, .55f, .2f), EBurst::Strike},
        {uint8(A::ActivePantsDown), TEXT("active_pants_down"), FLinearColor(1.f, .45f, .65f), EBurst::Strike},
        {uint8(A::ActiveCrotchGrab), TEXT("active_crotch_grab"), FLinearColor(1.f, .85f, .25f), EBurst::Strike},
        {uint8(A::ActiveAccountBanned), TEXT("active_account_banned"), FLinearColor(1.f, .2f, .2f), EBurst::Strike},
        {uint8(A::ActiveUnplug), TEXT("active_unplug"), FLinearColor(.55f, .9f, 1.f), EBurst::Zap},
        {uint8(A::ActiveSnarkBolt), TEXT("active_snark_bolt"), FLinearColor(.95f, .95f, .9f), EBurst::Muzzle},
        {uint8(A::ActiveSeethe), TEXT("active_seethe"), FLinearColor(1.f, .3f, .05f), EBurst::Flame},
        {uint8(A::ActiveQuickMeeting), TEXT("active_quick_meeting"), FLinearColor(1.f, .4f, .08f), EBurst::Flame},
        {uint8(A::ActiveBailOut), TEXT("active_bail_out"), FLinearColor(.6f, .85f, 1.f), EBurst::Dash},
        {uint8(A::ActiveApologySlide), TEXT("active_apology_slide"), FLinearColor(1.f, .75f, .45f), EBurst::Dash},
        {uint8(A::ActiveNotMad), TEXT("active_not_mad"), FLinearColor(.2f, .85f, 1.f), EBurst::Aura},
        {uint8(A::ActiveStareDown), TEXT("active_stare_down"), FLinearColor(1.f, .15f, .1f), EBurst::Aura},
        {uint8(A::ActiveTripleCombo), TEXT("active_triple_combo"), FLinearColor(1.f, .7f, .1f), EBurst::Aura},
        {uint8(A::ActiveCalculusDrop), TEXT("active_calculus_drop"), FLinearColor(.95f, .95f, .85f), EBurst::Slam},
        {uint8(A::ActiveFlyerTrap), TEXT("active_flyer_trap"), FLinearColor(1.f, .62f, .2f), EBurst::Muzzle},
        {uint8(A::ActiveOpenMic), TEXT("active_open_mic"), FLinearColor(1.f, .4f, .8f), EBurst::Gust},
        {uint8(A::ActiveHoseDown), TEXT("active_hose_down"), FLinearColor(.2f, .65f, 1.f), EBurst::Splash},
        {uint8(A::ActiveZap), TEXT("active_zap"), FLinearColor(1.f, .9f, .3f), EBurst::Zap},
        {uint8(A::ActiveChargedRam), TEXT("active_charged_ram"), FLinearColor(1.f, .85f, .2f), EBurst::Zap},
        {uint8(A::ActiveBrace), TEXT("active_brace"), FLinearColor(.75f, .8f, .9f), EBurst::Aura},
        {uint8(A::ActiveDirtyPuddle), TEXT("active_dirty_puddle"), FLinearColor(.35f, .55f, .7f), EBurst::Splash},
        {uint8(A::ActiveSockShot), TEXT("active_sock_shot"), FLinearColor(.55f, .85f, .25f), EBurst::Rot},
        {uint8(A::ActiveColaShake), TEXT("active_cola_shake"), FLinearColor(.65f, .9f, 1.f), EBurst::Frost},
        {uint8(A::ActiveDeskFan), TEXT("active_desk_fan"), FLinearColor(.8f, .9f, .95f), EBurst::Gust},
        {uint8(A::ActiveStaleTakeout), TEXT("active_stale_takeout"), FLinearColor(.5f, .7f, .15f), EBurst::Rot},
        {uint8(A::ActiveSchoolBag), TEXT("active_school_bag"), FLinearColor(.7f, .45f, .2f), EBurst::Slam},
    };
    return Table;
}

const LDHitVfx::FSkillLook* LDHitVfx::SkillLookOf(uint8 Ability)
{
    for (const FSkillLook& Look : SkillLooks()) { if (Look.Ability == Ability) { return &Look; } }
    return nullptr;
}

const LDHitVfx::FSkillLook* LDHitVfx::SkillLookOf(FName Id)
{
    if (Id.IsNone()) { return nullptr; }
    for (const FSkillLook& Look : SkillLooks()) { if (Id == FName(Look.Id)) { return &Look; } }
    return nullptr;
}

LDHitVfx::FZone LDHitVfx::ZoneOf(ELDObservedProjectileLook Look, float RadiusCm, float EffectScale)
{
    using namespace LDHitVfxDetail;
    FZone Z;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f || !FMath::IsFinite(RadiusCm) || RadiusCm <= 0.f) { return Z; }
    // Density follows the covered area so a wide zone is as busy as a small one.
    const float Area = FMath::Square(FMath::Clamp(RadiusCm, 40.f, 900.f) / 150.f);
    float Density = 0.f;
    switch (Look)
    {
    case ELDObservedProjectileLook::AreaFire:
        Density = 80.f; Z.Lifetime = .9f; Z.Scale = 2.2f; Z.Velocity = FVector(0, 0, 170); Z.Colour = Hot(FLinearColor(1.f, .2f, .02f), 2.2f);
        break;
    case ELDObservedProjectileLook::AreaWater:
        Density = 50.f; Z.Lifetime = 1.1f; Z.Scale = 1.8f; Z.Velocity = FVector(0, 0, 55); Z.Colour = Hot(FLinearColor(.08f, .45f, 1.f), 2.f);
        break;
    case ELDObservedProjectileLook::AreaElectric:
        Density = 120.f; Z.Lifetime = .22f; Z.Scale = 1.5f; Z.Velocity = FVector(0, 0, 30); Z.Colour = Hot(FLinearColor(1.f, .75f, .08f), 2.8f);
        break;
    case ELDObservedProjectileLook::AreaMagic:
        Density = 55.f; Z.Lifetime = 1.4f; Z.Scale = 1.8f; Z.Velocity = FVector(0, 0, 90); Z.Colour = Hot(FLinearColor(.55f, .12f, 1.f), 2.2f);
        break;
    case ELDObservedProjectileLook::AreaPlain:
        Density = 35.f; Z.Lifetime = 1.f; Z.Scale = 1.8f; Z.Velocity = FVector(0, 0, 40); Z.Colour = Hot(FLinearColor(1.f, .45f, .1f), 1.6f);
        break;
    case ELDObservedProjectileLook::SpeechCone:
        Density = 60.f; Z.Lifetime = .5f; Z.Scale = 1.f; Z.Velocity = FVector(RadiusCm * 1.6f, 0, 20); Z.Colour = Hot(FLinearColor(1.f, .4f, .8f), 2.2f);
        break;
    case ELDObservedProjectileLook::Wind:
        Density = 45.f; Z.Lifetime = .6f; Z.Scale = 1.1f; Z.Velocity = FVector(520, 0, 10); Z.Colour = Hot(FLinearColor(.8f, .9f, .95f), 1.4f);
        break;
    case ELDObservedProjectileLook::FlyerArmed:
        Density = 12.f; Z.Lifetime = .5f; Z.Scale = .7f; Z.Velocity = FVector(0, 0, 60); Z.Colour = Hot(FLinearColor(1.f, .6f, .2f), 2.f);
        break;
    default: return Z;
    }
    Z.bShow = true;
    Z.Rate = Density * Area * FMath::Clamp(EffectScale, 0.f, 1.f);
    return Z;
}

LDHitVfx::FTrail LDHitVfx::TrailOf(ELDObservedProjectileLook Look, float EffectScale)
{
    using namespace LDHitVfxDetail;
    FTrail T;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f) { return T; }
    switch (Look)
    {
    case ELDObservedProjectileLook::Chalk:
    case ELDObservedProjectileLook::ChalkReturn:
        T = {true, 18.f, .22f, 120.f, Hot(FLinearColor(.95f, .95f, .9f), 2.f, .8f)};
        break;
    case ELDObservedProjectileLook::Sock:
    case ELDObservedProjectileLook::SockReturn:
        T = {true, 22.f, .3f, 120.f, Hot(FLinearColor(.55f, .85f, .25f), 2.f, .8f)};
        break;
    default: return T;
    }
    T.WidthCm *= .6f + .4f * FMath::Clamp(EffectScale, 0.f, 1.f);
    return T;
}

bool LDHitVfx::AppearBurstOf(ELDObservedProjectileLook Look, EBurst& Out, FLinearColor& Tint)
{
    switch (Look)
    {
    case ELDObservedProjectileLook::Chalk: Out = EBurst::Muzzle; Tint = FLinearColor(.95f, .95f, .9f); return true;
    case ELDObservedProjectileLook::Sock: Out = EBurst::Muzzle; Tint = FLinearColor(.55f, .85f, .25f); return true;
    case ELDObservedProjectileLook::AreaFire: Out = EBurst::Flame; Tint = FLinearColor(1.f, .3f, .05f); return true;
    case ELDObservedProjectileLook::AreaWater: Out = EBurst::Splash; Tint = FLinearColor(.25f, .7f, 1.f); return true;
    case ELDObservedProjectileLook::AreaElectric: Out = EBurst::Zap; Tint = FLinearColor(1.f, .9f, .3f); return true;
    case ELDObservedProjectileLook::AreaMagic: Out = EBurst::Arcane; Tint = FLinearColor(.7f, .3f, 1.f); return true;
    case ELDObservedProjectileLook::AreaPlain: Out = EBurst::Slam; Tint = FLinearColor(1.f, .6f, .25f); return true;
    case ELDObservedProjectileLook::Bag:
    case ELDObservedProjectileLook::BagWall: Out = EBurst::Slam; Tint = FLinearColor(.7f, .45f, .2f); return true;
    case ELDObservedProjectileLook::FlyerArmed: Out = EBurst::Muzzle; Tint = FLinearColor(1.f, .62f, .2f); return true;
    default: return false;
    }
}

bool LDHitVfx::EndBurstOf(ELDObservedProjectileLook Look, EBurst& Out, FLinearColor& Tint)
{
    switch (Look)
    {
    case ELDObservedProjectileLook::Chalk:
    case ELDObservedProjectileLook::ChalkReturn: Out = EBurst::Strike; Tint = FLinearColor(.95f, .95f, .9f); return true;
    case ELDObservedProjectileLook::Sock:
    case ELDObservedProjectileLook::SockReturn: Out = EBurst::Rot; Tint = FLinearColor(.55f, .85f, .25f); return true;
    case ELDObservedProjectileLook::BookShadow: Out = EBurst::Slam; Tint = FLinearColor(.95f, .95f, .85f); return true;
    default: return false;
    }
}

bool LDHitVfx::IsDashing(float Speed2D, bool bKnockback, bool bFalling)
{
    return FMath::IsFinite(Speed2D) && Speed2D >= DashSpeedCmPerSec && !bKnockback && !bFalling;
}

LDHitVfx::FTrail LDHitVfx::DashTrailOf(const FLinearColor& Tint, float EffectScale)
{
    FTrail T;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f) { return T; }
    // Saturated and nearly opaque, so the streak reads on the bright yard as well as on dark floors.
    const FLinearColor Deep(FMath::Pow(Tint.R, 1.6f), FMath::Pow(Tint.G, 1.6f), FMath::Pow(Tint.B, 1.6f));
    T = {true, 70.f * (.6f + .4f * FMath::Clamp(EffectScale, 0.f, 1.f)), .3f, 140.f, LDHitVfxDetail::Hot(Deep, 1.6f, .9f)};
    return T;
}

LDHitVfx::FGhosts LDHitVfx::GhostsOf(const FLinearColor& Tint, float EffectScale)
{
    FGhosts G;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f) { return G; }
    G.bShow = true;
    G.IntervalSeconds = EffectScale < 1.f ? .09f : .045f;
    G.Seconds = .3f;
    // Saturated like the dash trail, so the copies read on the bright yard.
    G.Glow = LDHitVfxDetail::Hot(FLinearColor(FMath::Pow(Tint.R, 2.f), FMath::Pow(Tint.G, 2.f), FMath::Pow(Tint.B, 2.f)), 1.4f);
    return G;
}

float LDHitVfx::GhostFade(const FGhosts& Ghosts, float AgeSeconds)
{
    if (!Ghosts.bShow || Ghosts.Seconds <= 0.f || !FMath::IsFinite(AgeSeconds) || AgeSeconds < 0.f || AgeSeconds >= Ghosts.Seconds) { return 0.f; }
    return .9f * FMath::Square(1.f - AgeSeconds / Ghosts.Seconds);
}

FLinearColor LDHitVfx::OverdriveTint()
{
    return FLinearColor(1.f, .18f, .75f);
}

LDHitVfx::FZone LDHitVfx::ChargeZoneOf(float Progress, float EffectScale)
{
    FZone Z;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f || !FMath::IsFinite(Progress)) { return Z; }
    const float P = FMath::Clamp(Progress, 0.f, 1.f);
    Z.bShow = true;
    Z.Rate = (40.f + 90.f * P) * FMath::Clamp(EffectScale, 0.f, 1.f);
    Z.Lifetime = .45f;
    Z.Scale = 1.f + .6f * P;
    Z.Velocity = FVector(0, 0, 260.f + 220.f * P);
    Z.Colour = LDHitVfxDetail::Hot(OverdriveTint(), 1.6f + 1.6f * P);
    return Z;
}

LDHitVfx::FRecipe LDHitVfx::ForChargeReady(float EffectScale, const FLayerTuning& Tuning)
{
    using namespace LDHitVfxDetail;
    FRecipe Recipe;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f) { return Recipe; }
    const FLinearColor Glow = OverdriveTint() * 2.6f, Ink = OverdriveTint() * .4f;
    FLayer Sigil = Make(EMask::Sigil, EPlace::Ground, 160.f, .5f, Glow, Ink, .6f, 1.f, .2f);
    Sigil.SpinDegPerSec = 180.f;
    Recipe.Layers.Add(Sigil);
    Recipe.Layers.Add(Make(EMask::Cross, EPlace::Sprite, 110.f, .2f, Glow, Ink, .5f, 1.f, .2f));
    FLayer Ring = Make(EMask::Ring, EPlace::Ground, 200.f, .3f, Glow, Ink, .3f, 1.f, .1f);
    Ring.bOptional = true;
    Recipe.Layers.Add(Ring);
    Thin(Recipe, EffectScale);
    Tune(Recipe, Tuning);
    return Recipe;
}

LDHitVfx::FRecipe LDHitVfx::ForOverdriveRelease(float EffectScale, const FLayerTuning& Tuning)
{
    using namespace LDHitVfxDetail;
    FRecipe Recipe;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f) { return Recipe; }
    const FLinearColor Glow = OverdriveTint() * 3.4f, Ink = OverdriveTint() * .5f;
    FLayer Cross = Make(EMask::Cross, EPlace::Sprite, 240.f, .28f, Glow, Ink, .4f, 1.f, .12f);
    Cross.SpinDegPerSec = -160.f;
    Recipe.Layers.Add(Cross);
    Recipe.Layers.Add(Make(EMask::Ring, EPlace::Ground, 380.f, .45f, Glow, Ink, .2f, 1.f, .08f));
    FLayer Echo = Make(EMask::Ring, EPlace::Ground, 480.f, .5f, Glow * .6f, Ink, .2f, 1.f, .08f);
    Echo.DelaySeconds = .08f;
    Echo.bOptional = true;
    Recipe.Layers.Add(Echo);
    FLayer Sigil = Make(EMask::Sigil, EPlace::Ground, 260.f, .6f, Glow * .8f, Ink, .7f, 1.1f, .12f);
    Sigil.SpinDegPerSec = 90.f;
    Recipe.Layers.Add(Sigil);
    Recipe.Layers.Add(Make(EMask::Crack, EPlace::Ground, 200.f, .9f, Glow, FLinearColor(.02f, .005f, .015f), .8f, 1.f, .05f));
    Thin(Recipe, EffectScale);
    Tune(Recipe, Tuning);
    return Recipe;
}

FLinearColor LDHitVfx::ElementTint(uint8 Element)
{
    switch (static_cast<ELDCombatElement>(Element))
    {
    case ELDCombatElement::Fire: return FLinearColor(1.f, .45f, .12f);
    case ELDCombatElement::Water: return FLinearColor(.3f, .65f, 1.f);
    case ELDCombatElement::Shock: return FLinearColor(.75f, .45f, 1.f);
    case ELDCombatElement::Chill: return FLinearColor(.45f, .95f, 1.f);
    case ELDCombatElement::Rot: return FLinearColor(.55f, .9f, .25f);
    default: return FLinearColor(1.f, .85f, .35f);
    }
}

LDHitVfx::EBurst LDHitVfx::ElementBurst(uint8 Element)
{
    switch (static_cast<ELDCombatElement>(Element))
    {
    case ELDCombatElement::Fire: return EBurst::Flame;
    case ELDCombatElement::Water: return EBurst::Splash;
    case ELDCombatElement::Shock: return EBurst::Zap;
    case ELDCombatElement::Chill: return EBurst::Frost;
    case ELDCombatElement::Rot: return EBurst::Rot;
    default: return EBurst::Strike;
    }
}

LDHitVfx::FRecipe LDHitVfx::ForReaction(uint8 Element, float EffectScale, const FLayerTuning& Tuning)
{
    using namespace LDHitVfxDetail;
    FRecipe Recipe;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f) { return Recipe; }
    const FLinearColor Glow = ElementTint(Element) * 3.f, Ink = ElementTint(Element) * .5f;
    Recipe.Layers.Add(Make(EMask::Cross, EPlace::Sprite, 150.f, .24f, Glow, Ink, .5f, 1.f, .16f));
    Recipe.Layers.Add(Make(EMask::Ring, EPlace::Ground, 250.f, .4f, Glow, Ink, .3f, 1.f, .1f));
    FLayer Echo = Make(EMask::Ring, EPlace::Ground, 330.f, .42f, Glow * .6f, Ink, .3f, 1.f, .1f);
    Echo.DelaySeconds = .07f;
    Echo.bOptional = true;
    Recipe.Layers.Add(Echo);
    Thin(Recipe, EffectScale);
    Tune(Recipe, Tuning);
    return Recipe;
}

LDHitVfx::FZone LDHitVfx::GroundZoneOf(uint8 Kind, float RadiusCm, float EffectScale)
{
    using namespace LDHitVfxDetail;
    FZone Z;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f || !FMath::IsFinite(RadiusCm) || RadiusCm <= 0.f) { return Z; }
    const float Area = FMath::Square(FMath::Clamp(RadiusCm, 40.f, 900.f) / 150.f);
    float Density = 0.f;
    switch (static_cast<ELDGroundStateKind>(Kind))
    {
    case ELDGroundStateKind::Puddle:
        Density = 22.f; Z.Lifetime = 1.f; Z.Scale = 1.5f; Z.Velocity = FVector(0, 0, 40); Z.Colour = Hot(FLinearColor(.08f, .42f, 1.f), 2.f);
        break;
    case ELDGroundStateKind::Ice:
        Density = 16.f; Z.Lifetime = 1.2f; Z.Scale = 1.2f; Z.Velocity = FVector(0, 0, 14); Z.Colour = Hot(FLinearColor(.45f, .95f, 1.f), 2.4f);
        break;
    case ELDGroundStateKind::Fire:
        Density = 70.f; Z.Lifetime = .9f; Z.Scale = 2.2f; Z.Velocity = FVector(0, 0, 170); Z.Colour = Hot(FLinearColor(1.f, .2f, .02f), 2.2f);
        break;
    case ELDGroundStateKind::Miasma:
        Density = 32.f; Z.Lifetime = 1.6f; Z.Scale = 2.2f; Z.Velocity = FVector(0, 0, 55); Z.Colour = Hot(FLinearColor(.3f, .8f, .08f), 1.6f);
        break;
    case ELDGroundStateKind::Field:
        Density = 90.f; Z.Lifetime = .25f; Z.Scale = 1.5f; Z.Velocity = FVector(0, 0, 30); Z.Colour = Hot(FLinearColor(.6f, .25f, 1.f), 2.8f);
        break;
    default: return Z;
    }
    Z.bShow = true;
    Z.Rate = Density * Area * FMath::Clamp(EffectScale, 0.f, 1.f);
    return Z;
}

bool LDHitVfx::GroundAppearBurstOf(uint8 Kind, EBurst& Out, FLinearColor& Tint)
{
    switch (static_cast<ELDGroundStateKind>(Kind))
    {
    case ELDGroundStateKind::Puddle: Out = EBurst::Splash; Tint = FLinearColor(.08f, .42f, 1.f); return true;
    case ELDGroundStateKind::Ice: Out = EBurst::Frost; Tint = FLinearColor(.45f, .95f, 1.f); return true;
    case ELDGroundStateKind::Fire: Out = EBurst::Flame; Tint = FLinearColor(1.f, .3f, .04f); return true;
    case ELDGroundStateKind::Miasma: Out = EBurst::Rot; Tint = FLinearColor(.4f, .85f, .12f); return true;
    case ELDGroundStateKind::Field: Out = EBurst::Zap; Tint = FLinearColor(.66f, .3f, 1.f); return true;
    default: return false;
    }
}
