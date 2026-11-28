#include "Character/LDHitVfx.h"
#include "Character/LDHitFeel.h"
#include "Combat/LDCombatCues.h"
#include "Combat/LDCombatElements.h"
#include "Development/LDDemoContentIds.h"
#include "UI/LDBroadcastStyle.h"

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

const TCHAR* LDSliceVfx::TexId(ETex Tex)
{
    switch (Tex)
    {
    case ETex::SlashEdge: return TEXT("tex_slash_edge");
    case ETex::DissolveNoise: return TEXT("tex_dissolve_noise");
    case ETex::HitSkill: return TEXT("fb_hit_skill");
    case ETex::ChainAProfile: return TEXT("chainA_profile");
    case ETex::ChainAEnergyNoise: return TEXT("chainA_energy_noise");
    case ETex::ChainASparkStreak: return TEXT("chainA_spark_streak");
    case ETex::ChainAFlash: return TEXT("chainA_flash");
    case ETex::ChainAGroundGlow: return TEXT("chainA_ground_glow");
    case ETex::ChainBScorchDash: return TEXT("chainB_scorch_dash");
    case ETex::FieldWeb: return TEXT("field_web");
    case ETex::FieldEnergyNoise: return TEXT("field_energy_noise");
    case ETex::WaterFlow: return TEXT("water_flow");
    case ETex::WaterDrop: return TEXT("water_drop");
    case ETex::WaterWet: return TEXT("water_wet");
    case ETex::SoftPuff: return TEXT("soft_puff");
    case ETex::FireTongue: return TEXT("fire_tongue");
    case ETex::FireChar: return TEXT("fire_char");
    case ETex::IceShard: return TEXT("ice_shard");
    case ETex::IceFrost: return TEXT("ice_frost");
    case ETex::RotMurk: return TEXT("rot_murk");
    case ETex::RotBubble: return TEXT("rot_bubble");
    default: return TEXT("");
    }
}

const TCHAR* LDSliceVfx::MeshId(EMesh Mesh)
{
    switch (Mesh)
    {
    case EMesh::Slash1: return TEXT("SM_LD_SlashCombo1");
    case EMesh::Slash2: return TEXT("SM_LD_SlashCombo2");
    case EMesh::Slash3: return TEXT("SM_LD_SlashCombo3");
    case EMesh::PropTakeout: return TEXT("SM_LD_PropTakeout");
    case EMesh::PropSlipper: return TEXT("SM_LD_PropSlipper");
    case EMesh::PropBag: return TEXT("SM_LD_PropBag");
    case EMesh::PropBrick: return TEXT("SM_LD_PropBrick");
    case EMesh::PropFlyer: return TEXT("SM_LD_PropFlyer");
    case EMesh::PropTape: return TEXT("SM_LD_PropTape");
    default: return TEXT("");
    }
}

FString LDSliceVfx::TexturePackage(ETex Tex) { return FString(Folder) / TEXT("T_") + TexId(Tex); }
FString LDSliceVfx::MeshPackage(EMesh Mesh) { return FString(Folder) / MeshId(Mesh); }

bool LDSliceVfx::IsFlipbook(ETex Tex) { return FlipbookFps(Tex) > 0.f; }

float LDSliceVfx::FlipbookFps(ETex Tex)
{
    // vfx_slice_assets.json flip.fps of each flipbook.
    switch (Tex)
    {
    case ETex::HitSkill: return 28.f;
    default: return 0.f;
    }
}

FVector LDSliceVfx::SlashHead(EMesh Mesh)
{
    // The handoff's Blender head points (+Y = left of forward) with Y mirrored: the FBX import turns the right-handed
    // source into Unreal's left-handed space, where -Y is left of forward.
    switch (Mesh)
    {
    case EMesh::Slash1: return FVector(59.1, -45.7, 0.);
    case EMesh::Slash2: return FVector(59.3, 54.7, 0.);
    case EMesh::Slash3: return FVector(13.4, -103.4, 0.);
    default: return FVector::ZeroVector;
    }
}

namespace LDSliceVfxDetail
{
    using namespace LDSliceVfx;

    FLinearColor Hdr(const FLinearColor& Colour, float Glow) { return FLinearColor(Colour.R * Glow, Colour.G * Glow, Colour.B * Glow, 1.f); }
    float EaseOut(float T) { return 1.f - FMath::Pow(1.f - FMath::Clamp(T, 0.f, 1.f), 3.f); }

    // Ink edges carry the contrast on the bright warm map; the core is white-hot; the body is the element ink, or for no
    // element white, with the ball orange only on the local player's own strikes.
    const FLinearColor InkEdge = LDBroadcast::Ink;
    FLinearColor BodyGlow(const FLook& Look) { return Look.bOwn ? Hdr(LDBroadcast::Orange, 2.f * Look.Glow) : Hdr(LDBroadcast::White, 1.25f * Look.Glow); }
    FLinearColor CoreGlow(const FLook& Look) { return Hdr(LDBroadcast::White, (Look.bOwn ? 3.f : 2.2f) * Look.Glow); }

    EMesh SlashOf(uint8 Step) { return Step >= 3 ? EMesh::Slash3 : Step == 2 ? EMesh::Slash2 : EMesh::Slash1; }
    // The finisher's crescent is far wider than the first two cuts: drawn at three quarters of their scale so the struck
    // body stays visible behind its impact star.
    float SlashScale(uint8 Step) { return Step >= 3 ? 105.f : 140.f; }

    FVector Planar(const FVector& Dir)
    {
        const FVector Flat(Dir.X, Dir.Y, 0.);
        return Flat.IsNearlyZero() ? FVector::ForwardVector : Flat.GetSafeNormal();
    }

    FLayer Burst(ETex Tex, const FVector& At, const FVector& Dir, float SizeCm, float Seconds, const FLook& Look)
    {
        FLayer Layer;
        Layer.Tex = Tex;
        Layer.Place = EPlace::Facing;
        Layer.At = At;
        Layer.Dir = Dir.IsNearlyZero() ? FVector::ForwardVector : Dir.GetSafeNormal();
        Layer.SizeCm = SizeCm * Look.Size;
        // The hit sheets put the hit point at U 0.3 of the frame; the spark is centred.
        Layer.Pivot = .3f;
        Layer.Seconds = Seconds * Look.Seconds;
        Layer.FadeInSeconds = 0.f;
        Layer.FadeOutSeconds = .04f;
        Layer.Ink = InkEdge;
        Layer.bTop = true;
        return Layer;
    }

    // Own strikes at full size; others' at 80 % size and 70 % strength; reduced effects drop the optional layers and dim.
    void Finish(FRecipe& Recipe, const FLook& Look)
    {
        const float Thin = FMath::Clamp(Look.EffectScale, 0.f, 1.f);
        if (!FMath::IsFinite(Look.EffectScale) || Thin <= 0.f || !FMath::IsFinite(Look.Size) || !FMath::IsFinite(Look.Seconds)
            || !FMath::IsFinite(Look.Glow) || Look.Size <= 0.f || Look.Seconds <= 0.f || Look.Glow <= 0.f)
        {
            Recipe.Layers.Reset();
            return;
        }
        Recipe.Layers.RemoveAll([Thin](const FLayer& Layer) { return Layer.bOptional && Thin < 1.f; });
        for (FLayer& Layer : Recipe.Layers)
        {
            Layer.Alpha *= (.55f + .45f * Thin) * (Look.bFocus ? 1.f : .7f);
            if (!Look.bFocus && Layer.Place != EPlace::GroundSlash && Layer.Place != EPlace::GroundStrip) { Layer.SizeCm *= .8f; }
        }
    }
}

bool LDSliceVfx::ShowsHit(ELDObservedHitLook Look, uint8 HitFlags)
{
    return (Look == ELDObservedHitLook::None || Look == ELDObservedHitLook::Water || Look == ELDObservedHitLook::Electric || Look == ELDObservedHitLook::Fire)
        && (HitFlags & LDObservation::HitFlagsMask) == 0;
}

LDSliceVfx::FState LDSliceVfx::Evaluate(const FLayer& Layer, float AgeSeconds)
{
    using namespace LDSliceVfxDetail;
    FState State;
    const float T = AgeSeconds - Layer.DelaySeconds;
    if (!FMath::IsFinite(T) || T < 0.f || Layer.Seconds <= 0.f || (!Layer.bLoop && T >= Layer.Seconds)) { return State; }
    const float In = Layer.FadeInSeconds > 0.f ? FMath::Clamp(T / Layer.FadeInSeconds, 0.f, 1.f) : 1.f;
    const float Out = Layer.bLoop || Layer.FadeOutSeconds <= 0.f ? 1.f : FMath::Clamp((Layer.Seconds - T) / Layer.FadeOutSeconds, 0.f, 1.f);
    State.Fade = FMath::Clamp(Layer.Alpha, 0.f, 1.f) * In * Out;
    if (const float Fps = FlipbookFps(Layer.Tex); Fps > 0.f)
    {
        const int32 Frame = FMath::FloorToInt(T * Fps);
        State.Frame = float(Layer.bLoop ? Frame % 16 : FMath::Min(Frame, 15));
    }
    else if (Layer.Variants > 1)
    {
        // A jumpy order (3 is coprime with 8), so consecutive flickers never look like a slide.
        State.Frame = float((FMath::FloorToInt(T * FMath::Max(Layer.VariantHz, 1.f)) * 3) % Layer.Variants);
    }
    const float Reveal = Layer.RevealSeconds > 0.f ? EaseOut(T / Layer.RevealSeconds) : 1.f;
    if (Layer.bRevealFromTail) { State.WindowLo = 1.f - Reveal * 1.05f; }
    else if (Layer.RevealSeconds > 0.f) { State.WindowHi = Reveal * 1.05f; }
    State.Dissolve = FMath::Max(Layer.ChalkErode, Layer.ErodeFrom >= 0.f && Layer.ErodeSeconds > 0.f
        ? FMath::Clamp((T - Layer.ErodeFrom) / Layer.ErodeSeconds, 0.f, 1.f) : 0.f);
    State.Scroll = Layer.ScrollPerSecond * T;
    State.Scale = Layer.GrowFrom >= 1.f ? 1.f : FMath::Lerp(Layer.GrowFrom, 1.f, EaseOut(T / FMath::Max(Layer.Seconds * .25f, .01f)));
    State.bVisible = State.Fade > .002f && State.Dissolve < .999f;
    return State;
}

bool LDSliceVfx::IsOver(const FLayer& Layer, float AgeSeconds)
{
    return !Layer.bLoop && (!FMath::IsFinite(AgeSeconds) || AgeSeconds >= Layer.DelaySeconds + Layer.Seconds);
}

FLinearColor LDSliceVfx::StrikeTint(const FLook& Look)
{
    return Look.bOwn ? FLinearColor(1.f, .64f, .36f) : FLinearColor(1.f, .94f, .84f);
}

LDSliceVfx::ESystem LDSliceVfx::ReactionSystemOf(uint8 Reaction)
{
    switch (static_cast<ELDCombatReaction>(Reaction))
    {
    case ELDCombatReaction::Fog: return ESystem::FogReact;
    case ELDCombatReaction::Freeze: return ESystem::FreezeReact;
    case ELDCombatReaction::Overload: return ESystem::OverloadReact;
    case ELDCombatReaction::Melt: return ESystem::MeltReact;
    case ELDCombatReaction::Methane: return ESystem::MethaneReact;
    case ELDCombatReaction::Ferment: return ESystem::FermentReact;
    case ELDCombatReaction::Shatter: return ESystem::ShatterReact;
    case ELDCombatReaction::Corrode: return ESystem::CorrodeReact;
    case ELDCombatReaction::Frostbite: return ESystem::FrostbiteReact;
    default: return ESystem::Count;
    }
}

LDSliceVfx::ESystem LDSliceVfx::EffectSystemOf(uint8 ProjectileLook, FLinearColor& OutTint)
{
    OutTint = FLinearColor::White;
    switch (static_cast<ELDObservedProjectileLook>(ProjectileLook))
    {
    case ELDObservedProjectileLook::Chalk:
    case ELDObservedProjectileLook::ChalkReturn: return ESystem::FlightChalk;
    case ELDObservedProjectileLook::Sock:
    case ELDObservedProjectileLook::SockReturn: return ESystem::FlightSock;
    case ELDObservedProjectileLook::FistWind: return ESystem::FlightFist;
    case ELDObservedProjectileLook::Wind: return ESystem::WindZone;
    case ELDObservedProjectileLook::AreaPlain: OutTint = FLinearColor(1.f, .78f, .5f); return ESystem::AreaPlain;
    case ELDObservedProjectileLook::AreaMagic: OutTint = FLinearColor(.78f, .58f, 1.f); return ESystem::AreaPlain;
    // An elemental area is that element's floor: the same surface a ground state of the element shows.
    case ELDObservedProjectileLook::AreaFire: OutTint = ElementInk(uint8(ELDCombatElement::Fire)); return ESystem::FireGround;
    case ELDObservedProjectileLook::AreaWater: OutTint = ElementInk(uint8(ELDCombatElement::Water)); return ESystem::PuddleGround;
    case ELDObservedProjectileLook::AreaElectric: OutTint = ElementInk(uint8(ELDCombatElement::Shock)); return ESystem::Field;
    default: return ESystem::Count;
    }
}

const TCHAR* LDSliceVfx::PackPath(EPack Pack)
{
    switch (Pack)
    {
    case EPack::DustRing: return TEXT("/Game/ThirdParty/CCBY/PotaVFX_Smoke/VFX/System/SmokeRing/NS_SmokeRing.NS_SmokeRing");
    case EPack::FireBlast: return TEXT("/Game/ThirdParty/CCBY/MixedVFX/Particles/Explosions/NS_ExplosionGroundSmall.NS_ExplosionGroundSmall");
    case EPack::LightningHit: return TEXT("/Game/ThirdParty/CCBY/MixedVFX/Particles/Projectiles/Hits/NS_LightningBolt_Hit.NS_LightningBolt_Hit");
    default: return TEXT("");
    }
}

TConstArrayView<const TCHAR*> LDSliceVfx::PackMutedEmitters(EPack Pack)
{
    // The explosion's translucent red shock sphere: several of them overlap into pink bubbles along a flame front.
    static const TCHAR* const FireBlast[] = {TEXT("Empty")};
    if (Pack == EPack::FireBlast) { return FireBlast; }
    return {};
}

LDSliceVfx::FSkillSystems LDSliceVfx::SkillSystemsOf(uint8 Ability)
{
    FSkillSystems Out;
    switch (static_cast<ELDDemoAbilityId>(Ability))
    {
    case ELDDemoAbilityId::ActiveElbow: Out.Release = ESystem::ElbowRelease; Out.Hit = ESystem::ElbowHit; Out.HitDust = .32f; break;
    case ELDDemoAbilityId::ActivePantsDown: Out.Release = ESystem::PantsRelease; Out.Hit = ESystem::PantsHit; Out.HitDust = .22f; break;
    case ELDDemoAbilityId::ActiveCrotchGrab: Out.Release = ESystem::GrabRelease; Out.Hit = ESystem::GrabHit; Out.HitDust = .22f; break;
    case ELDDemoAbilityId::ActiveBailOut: Out.Release = ESystem::BailRelease; break;
    case ELDDemoAbilityId::ActiveChargedRam: Out.Release = ESystem::RamRelease; Out.Hit = ESystem::RamHit; Out.HitDust = .5f; break;
    case ELDDemoAbilityId::ActiveUnplug: Out.Release = ESystem::UnplugRelease; Out.Hit = ESystem::UnplugHit; break;
    case ELDDemoAbilityId::ActiveNotMad: Out.Release = ESystem::NotMadRelease; break;
    case ELDDemoAbilityId::ActiveStareDown: Out.Release = ESystem::StareDownRelease; Out.Aura = ESystem::StareDownAura; break;
    case ELDDemoAbilityId::ActiveBrace: Out.Release = ESystem::BraceRelease; break;
    case ELDDemoAbilityId::ActiveTripleCombo: Out.Release = ESystem::TripleRelease; break;
    case ELDDemoAbilityId::ActiveOpenMic: Out.Release = ESystem::OpenMicRelease; Out.Hit = ESystem::OpenMicHit; break;
    case ELDDemoAbilityId::ActiveFlurryRush: Out.Release = ESystem::FlurryRelease; Out.Aura = ESystem::FlurryAura; break;
    default: break;
    }
    return Out;
}

FLinearColor LDSliceVfx::ElementInk(uint8 Element)
{
    // Their own values; today they equal the broadcast inks they started from (#FF6A3D, #42B4FF, #FFD23D, #A6ECFF, #A4D24A).
    switch (static_cast<ELDCombatElement>(Element))
    {
    case ELDCombatElement::Fire: return FLinearColor(FColor(0xFF, 0x6A, 0x3D));
    case ELDCombatElement::Water: return FLinearColor(FColor(0x42, 0xB4, 0xFF));
    case ELDCombatElement::Shock: return FLinearColor(FColor(0xFF, 0xD2, 0x3D));
    case ELDCombatElement::Chill: return FLinearColor(FColor(0xA6, 0xEC, 0xFF));
    case ELDCombatElement::Rot: return FLinearColor(FColor(0xA4, 0xD2, 0x4A));
    default: return FLinearColor(1.f, .85f, .35f);
    }
}

LDSliceVfx::FRecipe LDSliceVfx::ZapRelease(const FVector& Hand, const FLook& Look)
{
    using namespace LDSliceVfxDetail;
    FRecipe Recipe;
    // A small charged warm glow at the hand that lasts until the link leaves it; no star.
    FLayer Glow = Burst(ETex::ChainAFlash, Hand, FVector::ForwardVector, 40.f, .3f, Look);
    Glow.Pivot = .5f;
    Glow.Glow = Hdr(FLinearColor(1.f, .78f, .36f), 1.4f * Look.Glow);
    Glow.Core = Hdr(FLinearColor(1.f, .9f, .67f), 2.f * Look.Glow);
    Glow.Ink = Glow.Glow;
    Recipe.Layers.Add(Glow);
    Finish(Recipe, Look);
    return Recipe;
}

LDSliceVfx::FRecipe LDSliceVfx::Impact(EStrike Strike, const FVector& Chest, const FVector& Feet, const FVector& Dir, const FVector& From,
    bool bChained, const FLook& Look)
{
    using namespace LDSliceVfxDetail;
    FRecipe Recipe;
    const FVector Along = Dir.IsNearlyZero() ? FVector::ForwardVector : Dir.GetSafeNormal();
    switch (Strike)
    {
    // The basic and finisher hits are the Niagara Hit (ULDImpactFxSubsystem::Hit), the finisher's contact light its own.
    case EStrike::Basic: case EStrike::Finisher: break;
    case EStrike::Skill:
    {
        FLayer Hit = Burst(ETex::HitSkill, Chest, Along, 200.f, .57f, Look);
        Hit.Glow = BodyGlow(Look);
        Hit.Core = CoreGlow(Look);
        Recipe.Layers.Add(Hit);
        break;
    }
    // A water hit is the Niagara WaterHit (ULDImpactFxSubsystem::WaterHit).
    case EStrike::Hose: break;
    case EStrike::Zap:
    {
        // The link itself, its contact burst, body arcs and sparks are the Niagara Chain (ULDImpactFxSubsystem::Chain);
        // here a warm contact glow on the ground for a quarter second over a faint scorch flash along the hop. A bounce
        // target's is the same at its smaller link scale (Look.Size).
        const FVector Hop = !From.ContainsNaN() && FVector::DistSquared(From, Chest) > 100.f ? Planar(Chest - From) : Planar(Along);
        FLayer Glow;
        Glow.Tex = ETex::ChainAGroundGlow;
        Glow.Place = EPlace::Flat;
        Glow.At = Feet;
        Glow.Dir = Hop;
        Glow.SizeCm = (bChained ? 130.f : 160.f) * Look.Size;
        Glow.Seconds = .26f * Look.Seconds;
        Glow.FadeInSeconds = 0.f;
        Glow.FadeOutSeconds = .16f;
        Glow.Alpha = Look.bFocus ? 1.f : .65f;
        Glow.Glow = Hdr(FLinearColor(1.f, .78f, .36f), 2.f);
        Glow.Core = Hdr(FLinearColor(1.f, .9f, .67f), 2.4f);
        Glow.Ink = Glow.Glow;
        Glow.bOptional = true;
        Recipe.Layers.Add(Glow);
        FLayer Scorch;
        Scorch.Tex = ETex::ChainBScorchDash;
        Scorch.Place = EPlace::Flat;
        Scorch.At = Feet;
        Scorch.Dir = Hop;
        Scorch.SizeCm = (bChained ? 80.f : 100.f) * Look.Size;
        Scorch.Seconds = .55f * Look.Seconds;
        Scorch.FadeInSeconds = 0.f;
        Scorch.FadeOutSeconds = .4f;
        Scorch.Alpha = .4f;
        Scorch.Glow = Hdr(FLinearColor(1.f, .62f, .2f), 1.6f);
        Scorch.Core = Scorch.Glow;
        Scorch.Ink = FLinearColor(.06f, .03f, .012f);
        Scorch.bOptional = true;
        Recipe.Layers.Add(Scorch);
        break;
    }
    default: break;
    }
    Finish(Recipe, Look);
    return Recipe;
}

FString LDSliceVfx::GeneratedPackage(const TCHAR* Name) { return FString(GeneratedFolder) / Name; }

const TCHAR* LDSliceVfx::SystemName(ESystem System)
{
    switch (System)
    {
    case ESystem::Chain: return TEXT("NS_LDChain");
    case ESystem::HitBasic: return TEXT("NS_LDHitBasic");
    case ESystem::HitFinisher: return TEXT("NS_LDHitFinisher");
    case ESystem::Swing1: return TEXT("NS_LDSwing1");
    case ESystem::Swing2: return TEXT("NS_LDSwing2");
    case ESystem::Swing3: return TEXT("NS_LDSwing3");
    case ESystem::Field: return TEXT("NS_LDField");
    case ESystem::Tell1: return TEXT("NS_LDTell1");
    case ESystem::Tell2: return TEXT("NS_LDTell2");
    case ESystem::Tell3: return TEXT("NS_LDTell3");
    case ESystem::HoseTell: return TEXT("NS_LDHoseTell");
    case ESystem::HoseJet: return TEXT("NS_LDHoseJet");
    case ESystem::WaterHit: return TEXT("NS_LDWaterHit");
    case ESystem::ZapCharge: return TEXT("NS_LDZapCharge");
    case ESystem::SeetheTell: return TEXT("NS_LDSeetheTell");
    case ESystem::SeetheBurst: return TEXT("NS_LDSeetheBurst");
    case ESystem::FireHit: return TEXT("NS_LDFireHit");
    case ESystem::ColaTell: return TEXT("NS_LDColaTell");
    case ESystem::ColaBlast: return TEXT("NS_LDColaBlast");
    case ESystem::ChillHit: return TEXT("NS_LDChillHit");
    case ESystem::BurnState: return TEXT("NS_LDBurnState");
    case ESystem::WetState: return TEXT("NS_LDWetState");
    case ESystem::ShockState: return TEXT("NS_LDShockState");
    case ESystem::ChillState: return TEXT("NS_LDChillState");
    case ESystem::RotState: return TEXT("NS_LDRotState");
    case ESystem::PuddleGround: return TEXT("NS_LDPuddleGround");
    case ESystem::IceGround: return TEXT("NS_LDIceGround");
    case ESystem::FireGround: return TEXT("NS_LDFireGround");
    case ESystem::MiasmaGround: return TEXT("NS_LDMiasmaGround");
    case ESystem::Knockout: return TEXT("NS_LDKnockout");
    case ESystem::CastAim: return TEXT("NS_LDCastAim");
    case ESystem::CastBrace: return TEXT("NS_LDCastBrace");
    case ESystem::ElbowRelease: return TEXT("NS_LDElbowRelease");
    case ESystem::ElbowHit: return TEXT("NS_LDElbowHit");
    case ESystem::PantsRelease: return TEXT("NS_LDPantsRelease");
    case ESystem::PantsHit: return TEXT("NS_LDPantsHit");
    case ESystem::GrabRelease: return TEXT("NS_LDGrabRelease");
    case ESystem::GrabHit: return TEXT("NS_LDGrabHit");
    case ESystem::BailRelease: return TEXT("NS_LDBailRelease");
    case ESystem::RamRelease: return TEXT("NS_LDRamRelease");
    case ESystem::RamHit: return TEXT("NS_LDRamHit");
    case ESystem::ShieldState: return TEXT("NS_LDShieldState");
    case ESystem::BracedState: return TEXT("NS_LDBracedState");
    case ESystem::EmpoweredState: return TEXT("NS_LDEmpoweredState");
    case ESystem::StareDownAura: return TEXT("NS_LDStareDownAura");
    case ESystem::FlurryAura: return TEXT("NS_LDFlurryAura");
    case ESystem::UnplugRelease: return TEXT("NS_LDUnplugRelease");
    case ESystem::UnplugHit: return TEXT("NS_LDUnplugHit");
    case ESystem::NotMadRelease: return TEXT("NS_LDNotMadRelease");
    case ESystem::StareDownRelease: return TEXT("NS_LDStareDownRelease");
    case ESystem::BraceRelease: return TEXT("NS_LDBraceRelease");
    case ESystem::BraceBurst: return TEXT("NS_LDBraceBurst");
    case ESystem::TripleRelease: return TEXT("NS_LDTripleRelease");
    case ESystem::OpenMicRelease: return TEXT("NS_LDOpenMicRelease");
    case ESystem::OpenMicHit: return TEXT("NS_LDOpenMicHit");
    case ESystem::FlurryRelease: return TEXT("NS_LDFlurryRelease");
    case ESystem::FogReact: return TEXT("NS_LDFogReact");
    case ESystem::FreezeReact: return TEXT("NS_LDFreezeReact");
    case ESystem::OverloadReact: return TEXT("NS_LDOverloadReact");
    case ESystem::MeltReact: return TEXT("NS_LDMeltReact");
    case ESystem::MethaneReact: return TEXT("NS_LDMethaneReact");
    case ESystem::FermentReact: return TEXT("NS_LDFermentReact");
    case ESystem::ShatterReact: return TEXT("NS_LDShatterReact");
    case ESystem::CorrodeReact: return TEXT("NS_LDCorrodeReact");
    case ESystem::FrostbiteReact: return TEXT("NS_LDFrostbiteReact");
    case ESystem::FlightChalk: return TEXT("NS_LDFlightChalk");
    case ESystem::FlightSock: return TEXT("NS_LDFlightSock");
    case ESystem::FlightFist: return TEXT("NS_LDFlightFist");
    case ESystem::WindZone: return TEXT("NS_LDWindZone");
    case ESystem::AreaPlain: return TEXT("NS_LDAreaPlain");
    case ESystem::ShardBurst: return TEXT("NS_LDShardBurst");
    case ESystem::RootState: return TEXT("NS_LDRootState");
    case ESystem::ProtectedState: return TEXT("NS_LDProtectedState");
    case ESystem::SlowState: return TEXT("NS_LDSlowState");
    case ESystem::AttackSlowState: return TEXT("NS_LDAttackSlowState");
    case ESystem::StunState: return TEXT("NS_LDStunState");
    case ESystem::DisarmState: return TEXT("NS_LDDisarmState");
    case ESystem::SilenceState: return TEXT("NS_LDSilenceState");
    case ESystem::TauntState: return TEXT("NS_LDTauntState");
    case ESystem::ExposedState: return TEXT("NS_LDExposedState");
    case ESystem::DashWake: return TEXT("NS_LDDashWake");
    case ESystem::LevelUp: return TEXT("NS_LDLevelUp");
    case ESystem::OwnMark: return TEXT("NS_LDOwnMark");
    case ESystem::RingBand: return TEXT("NS_LDRingBand");
    case ESystem::RingDanger: return TEXT("NS_LDRingDanger");
    case ESystem::RingNext: return TEXT("NS_LDRingNext");
    case ESystem::CritBurst: return TEXT("NS_LDCritBurst");
    case ESystem::ReturnState: return TEXT("NS_LDReturnState");
    default: return TEXT("");
    }
}

FString LDSliceVfx::SystemPackage(ESystem System) { return GeneratedPackage(SystemName(System)); }

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
    // Critical hits come often in a crit build: a faint gold edge without a lens fringe.
    else if (DealtFlags & LDObservation::Critical) { Flash = {FLinearColor(1.f, .62f, .12f, .25f), .12f, 0.f}; }
    else if (DealtFlags & LDObservation::ShieldBroken) { Flash = {FLinearColor(.1f, .8f, 1.f, .4f), .14f, 1.5f}; }
    return Flash;
}

float LDHitVfx::ScreenFlashAlpha(const FScreenFlash& Flash, float AgeSeconds)
{
    if (Flash.Seconds <= 0.f || !FMath::IsFinite(AgeSeconds) || AgeSeconds < 0.f || AgeSeconds >= Flash.Seconds) { return 0.f; }
    return Flash.Colour.A * FMath::Square(1.f - AgeSeconds / Flash.Seconds);
}

bool LDHitVfx::ScreenFlashReplaces(const FScreenFlash& Last, float LastAgeSeconds, const FScreenFlash& Next)
{
    if (Next.Seconds <= 0.f) { return false; }
    if (Last.Seconds <= 0.f || !FMath::IsFinite(LastAgeSeconds) || LastAgeSeconds < 0.f || LastAgeSeconds >= ScreenFlashGapSeconds) { return true; }
    return Next.Colour.A > Last.Colour.A;
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
    default: return FBurst();
    }
    const float Thin = FMath::Clamp(EffectScale, 0.f, 1.f);
    B.Sparks = FMath::RoundToInt(float(B.Sparks) * Thin);
    B.Smoke = FMath::RoundToInt(float(B.Smoke) * Thin);
    B.Shards = FMath::RoundToInt(float(B.Shards) * Thin);
    return B;
}

TConstArrayView<LDHitVfx::FSkillLook> LDHitVfx::SkillLooks()
{
    using A = ELDDemoAbilityId;
    static const FSkillLook Table[] = {
        {uint8(A::ActiveElbow), TEXT("active_elbow"), FLinearColor(1.f, .55f, .2f), EBurst::Strike},
        {uint8(A::ActivePantsDown), TEXT("active_pants_down"), FLinearColor(1.f, .45f, .65f), EBurst::Strike},
        {uint8(A::ActiveCrotchGrab), TEXT("active_crotch_grab"), FLinearColor(1.f, .85f, .25f), EBurst::Strike},
        {uint8(A::ActiveUnplug), TEXT("active_unplug"), FLinearColor(.55f, .92f, 1.f), EBurst::Aura},
        {uint8(A::ActiveSnarkBolt), TEXT("active_snark_bolt"), FLinearColor(.95f, .95f, .9f), EBurst::Muzzle},
        {uint8(A::ActiveSeethe), TEXT("active_seethe"), FLinearColor(1.f, .3f, .05f), EBurst::Flame},
        {uint8(A::ActiveQuickMeeting), TEXT("active_quick_meeting"), FLinearColor(1.f, .4f, .08f), EBurst::Flame},
        {uint8(A::ActiveBailOut), TEXT("active_bail_out"), FLinearColor(.6f, .85f, 1.f), EBurst::Dash},
        {uint8(A::ActiveNotMad), TEXT("active_not_mad"), FLinearColor(.2f, .85f, 1.f), EBurst::Aura},
        {uint8(A::ActiveStareDown), TEXT("active_stare_down"), FLinearColor(1.f, .15f, .1f), EBurst::Aura},
        {uint8(A::ActiveTripleCombo), TEXT("active_triple_combo"), FLinearColor(1.f, .7f, .1f), EBurst::Aura},
        {uint8(A::ActiveCalculusDrop), TEXT("active_calculus_drop"), FLinearColor(.95f, .95f, .85f), EBurst::Slam},
        {uint8(A::ActiveFlyerTrap), TEXT("active_flyer_trap"), FLinearColor(1.f, .62f, .2f), EBurst::Muzzle},
        {uint8(A::ActiveOpenMic), TEXT("active_open_mic"), FLinearColor(1.f, .45f, .75f), EBurst::Gust},
        {uint8(A::ActiveHoseDown), TEXT("active_hose_down"), FLinearColor(.2f, .65f, 1.f), EBurst::Splash},
        {uint8(A::ActiveZap), TEXT("active_zap"), FLinearColor(1.f, .9f, .3f), EBurst::Zap},
        {uint8(A::ActiveChargedRam), TEXT("active_charged_ram"), FLinearColor(.95f, .62f, .22f), EBurst::Slam},
        {uint8(A::ActiveBrace), TEXT("active_brace"), FLinearColor(.75f, .8f, .9f), EBurst::Aura},
        {uint8(A::ActiveColaShake), TEXT("active_cola_shake"), FLinearColor(.65f, .9f, 1.f), EBurst::Frost},
        {uint8(A::ActiveDeskFan), TEXT("active_desk_fan"), FLinearColor(.8f, .9f, .95f), EBurst::Gust},
        {uint8(A::ActiveStaleTakeout), TEXT("active_stale_takeout"), FLinearColor(.5f, .7f, .15f), EBurst::Rot},
        {uint8(A::ActiveSchoolBag), TEXT("active_school_bag"), FLinearColor(.7f, .45f, .2f), EBurst::Slam},
        {uint8(A::ActiveFlurryRush), TEXT("active_flurry_rush"), FLinearColor(1.f, .38f, .18f), EBurst::Gust},
        {uint8(A::ActiveCordonTape), TEXT("active_cordon_tape"), FLinearColor(1.f, .86f, .12f), EBurst::Zap},
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
    case ELDObservedProjectileLook::FistWind:
        T = {true, 34.f, .26f, 150.f, Hot(FLinearColor(1.f, .42f, .18f), 2.4f, .9f)};
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
    case ELDObservedProjectileLook::FistWind: Out = EBurst::Gust; Tint = FLinearColor(1.f, .42f, .18f); return true;
    case ELDObservedProjectileLook::SlipperDrop: Out = EBurst::Aura; Tint = FLinearColor(.25f, .95f, 1.f); return true;
    case ELDObservedProjectileLook::CordonTape: Out = EBurst::Zap; Tint = FLinearColor(1.f, .86f, .12f); return true;
    case ELDObservedProjectileLook::ShardBurst: Out = EBurst::Slam; Tint = FLinearColor(.2f, .92f, 1.f); return true;
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
    case ELDObservedProjectileLook::FistWind: Out = EBurst::Gust; Tint = FLinearColor(1.f, .42f, .18f); return true;
    case ELDObservedProjectileLook::SlipperDrop: Out = EBurst::Aura; Tint = FLinearColor(.25f, .95f, 1.f); return true;
    case ELDObservedProjectileLook::BookShadow: Out = EBurst::Slam; Tint = FLinearColor(.95f, .95f, .85f); return true;
    default: return false;
    }
}

bool LDHitVfx::IsDashing(float Speed2D, bool bKnockback, bool bFalling)
{
    return FMath::IsFinite(Speed2D) && Speed2D >= DashSpeedCmPerSec && !bKnockback && !bFalling;
}

LDHitVfx::FGhosts LDHitVfx::GhostsOf(const FLinearColor& Tint, float EffectScale)
{
    FGhosts G;
    if (!FMath::IsFinite(EffectScale) || EffectScale <= 0.f) { return G; }
    G.bShow = true;
    // #335 §3.1: 2 crisp translucent afterimages across an ~0.18s dash, fading linearly over 0.22s.
    G.IntervalSeconds = EffectScale < 1.f ? .16f : .075f;
    G.Seconds = .22f;
    const FLinearColor CyanWhite = FLinearColor::LerpUsingHSV(Tint, FLinearColor(.58f, .94f, 1.f), .35f);
    G.Glow = LDHitVfxDetail::Hot(FLinearColor(FMath::Pow(CyanWhite.R, 1.5f), FMath::Pow(CyanWhite.G, 1.5f), FMath::Pow(CyanWhite.B, 1.5f)), 1.65f);
    return G;
}

float LDHitVfx::GhostFade(const FGhosts& Ghosts, float AgeSeconds)
{
    if (!Ghosts.bShow || Ghosts.Seconds <= 0.f || !FMath::IsFinite(AgeSeconds) || AgeSeconds < 0.f || AgeSeconds >= Ghosts.Seconds) { return 0.f; }
    return .88f * (1.f - AgeSeconds / Ghosts.Seconds);
}

FLinearColor LDHitVfx::ElementTint(uint8 Element) { return LDSliceVfx::ElementInk(Element); }

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
    // The electric field is the designed looping crackle (the Niagara Field, ULDImpactFxSubsystem::ShowGroundField), not the kit particles.
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
    // The electric field fades in as the Niagara Field (ULDImpactFxSubsystem::ShowGroundField).
    default: return false;
    }
}
