#pragma once

#include "CoreMinimal.h"
#include "Vision/LDViewTypes.h"

namespace LDHitFeel { struct FTuning; }

// The shaped hit layers over the Niagara burst (LDImpactFx): a flash star facing the camera, a ground mark under the
// struck body and the per-look signature (fire burn ring, water ripples, electric strike, magic sigil, shield shell),
// plus the kill crack where a visible body falls and the own screen edge flash. Each tier and look has its own shape,
// not only its own colour. Local presentation only: layers are spawned for hits this client already sees and never
// touch collision, navigation, sight or authority.
// Masks: SourceData/VFX (rendered by SourceData/VFX/Blender/hit_textures.py) imported by
// Scripts\ImportDemoArt.ps1 -Mode ImportVFX (LDDemoVfxImport) into Folder, with the importer-built materials.
namespace LDHitVfx
{
    constexpr const TCHAR* Folder = TEXT("/Game/Lockdown/Art/VFX/Hit");
    // Translucent unlit: emissive = Glow x mask.r + Ink, opacity = mask.a x Fade.
    constexpr const TCHAR* LayerMaterial = TEXT("/Game/Lockdown/Art/VFX/Hit/M_LDHitLayer");
    // Additive: Glow x Fade x (hex lattice + fresnel rim).
    constexpr const TCHAR* ShellMaterial = TEXT("/Game/Lockdown/Art/VFX/Hit/M_LDHitShell");
    // Translucent fresnel ghost for skeletal afterimages: emissive = Glow x (rim + 0.3), opacity = Fade x (0.25 + 0.75 rim).
    constexpr const TCHAR* GhostMaterial = TEXT("/Game/Lockdown/Art/VFX/Hit/M_LDHitGhost");
    constexpr const TCHAR* MaskParameter = TEXT("Mask");
    constexpr const TCHAR* InkParameter = TEXT("Ink");
    constexpr const TCHAR* GlowParameter = TEXT("Glow");
    constexpr const TCHAR* FadeParameter = TEXT("Fade");

    enum class EMask : uint8 { Star, Cross, Ring, Ripple, Burn, Sigil, Crack, Bolt, Hex, ScreenEdge, Count };
    // The manifest id of a mask ("hit_star") and its texture package (Folder/T_hit_star).
    LOCKDOWN_API const TCHAR* MaskId(EMask Mask);
    LOCKDOWN_API FString TexturePackage(const TCHAR* Id);

    // How a layer is placed: a sprite turned to the camera at the chest (Bolt stands upright), a flat mark on the
    // ground under the body, or a sphere shell around the chest.
    enum class EPlace : uint8 { Sprite, Upright, Ground, Shell };
    struct FLayer
    {
        EMask Mask = EMask::Star;
        EPlace Place = EPlace::Sprite;
        float SizeCm = 0;            // the full width at its largest; a sprite is also held to MinSpritePx on screen
        float FromScale = 1, ToScale = 1;
        float DelaySeconds = 0, Seconds = 0;
        float PeakAt = .2f;          // share of Seconds at full brightness before it fades
        float SpinDegPerSec = 0;
        FLinearColor Glow = FLinearColor::Black, Ink = FLinearColor::Black;
        bool bOptional = false;      // dropped with reduced effects: a repeat or a halo, never the tier's or look's shape
    };
    // The heaviest mix (shield absorbed, critical, overdrive, electric) needs seven.
    constexpr int32 MaxLayers = 8;
    struct FRecipe
    {
        TArray<FLayer, TFixedAllocator<MaxLayers>> Layers;
    };
    // Size, duration and brightness multipliers of every layer: the catalog's saved values (DA_CombatVfx Layer*) times
    // the live impact controls relative to their starting values (ld.Feel.ImpactScale / ImpactMs / ImpactGlow). An
    // impact size or duration of zero turns the layers off, as it turns the Niagara burst off.
    struct FLayerTuning
    {
        float Size = 1, Seconds = 1, Glow = 1;
    };
    LOCKDOWN_API FLayerTuning TuningOf(const LDHitFeel::FTuning& Feel, float AssetSize, float AssetSeconds, float AssetGlow);
    // One observed hit (LDObservation::EHitFlags): ordinary star and ring; critical cross star and a double gold ring;
    // a shield's cyan hex shell (a broken one bursts outwards over a wide ring); then the look's signature. An overdrive
    // hit replaces the star with a large magenta cross and adds its own wide ring and spinning ground sigil.
    // Reduced effects drop the optional layers and dim the rest; no layer that tells the tier or the look is removed.
    LOCKDOWN_API FRecipe ForHit(ELDObservedHitLook Look, uint8 HitFlags, float EffectScale, const FLayerTuning& Tuning = FLayerTuning());
    // A visible body's fall: a glowing ground crack that stays two seconds and a double shock ring.
    LOCKDOWN_API FRecipe ForKill(float EffectScale, const FLayerTuning& Tuning = FLayerTuning());
    // 0 before DelaySeconds and from Seconds on; a quick rise to 1 at PeakAt, then an ease-out fade.
    LOCKDOWN_API float Brightness(const FLayer& Layer, float AgeSeconds);
    LOCKDOWN_API float ScaleAt(const FLayer& Layer, float AgeSeconds);

    // Distance compensation of the flash sprites: one factor for every sprite, set so the ordinary star
    // (ReferenceSpriteCm, the full-size flash before its pop animation) keeps at least MinSpritePx of a 1080-line view.
    // Every sprite is multiplied by the same factor after its recipe size and animation, so a critical cross stays 1.5x
    // an ordinary star and a water star stays smaller at any distance. Never below 1.
    constexpr float MinSpritePx = 60.f;
    constexpr float ReferenceSpriteCm = 120.f;
    LOCKDOWN_API float SpriteCompensation(float DistanceCm, float FovDegrees);

    // The own screen edge flash and lens fringe for a receipt (LDCombatCues::DealtLethal over the observed flags):
    // a kill red, a critical hit gold, a broken shield cyan; an ordinary hit none. Reduced effects turn it off.
    struct FScreenFlash
    {
        FLinearColor Colour = FLinearColor::Transparent;
        float Seconds = 0;
        float Fringe = 0;            // SceneFringeIntensity at the flash's start
    };
    LOCKDOWN_API FScreenFlash ScreenFlashOf(uint8 DealtFlags, bool bReduced);
    LOCKDOWN_API float ScreenFlashAlpha(const FScreenFlash& Flash, float AgeSeconds);
    // The own body taking a hit: a short, faint red edge that never replaces a stronger flash still playing.
    LOCKDOWN_API FScreenFlash HurtFlashOf(bool bReduced);
    // A busy fight never strobes the whole screen: within ScreenFlashGapSeconds of the last flash's start only a stronger
    // flash (a kill over a critical hit) replaces it; anything else is skipped.
    constexpr float ScreenFlashGapSeconds = .6f;
    LOCKDOWN_API bool ScreenFlashReplaces(const FScreenFlash& Last, float LastAgeSeconds, const FScreenFlash& Next);

    // Particle kit (Niagara NS_LDBurst, its user parameters): one burst of velocity-stretched sparks with a settable
    // gravity and drag (rising embers, falling droplets, hanging motes, fast arcs), soft smoke, a core flash and mesh
    // shards. Every skill release, element hit, landing, muzzle, dash and fall picks a preset tinted by its source.
    enum class EBurst : uint8 { Strike, Muzzle, Flame, Splash, Zap, Frost, Rot, Gust, Arcane, Dash, Aura, Slam, Kill, Overdrive, Count };
    struct FBurst
    {
        int32 Sparks = 0, Smoke = 0, Shards = 0;
        FLinearColor SparkColour = FLinearColor::Black, SmokeColour = FLinearColor::Black;
        FLinearColor CoreColour = FLinearColor::Transparent, ShardColour = FLinearColor::Black;
        float Speed = 1, Scale = 1, Life = 1, Drag = .75f;
        FVector Gravity = FVector(0, 0, -980);
        bool IsEmpty() const { return Sparks + Smoke + Shards <= 0 && CoreColour.A <= 0.f; }
    };
    // Reduced effects keep every preset and its colours but spawn fewer particles (x0.45).
    LOCKDOWN_API FBurst BurstOf(EBurst Kind, const FLinearColor& Tint, float EffectScale);
    // The extra particles an element hit throws over the impact burst: fire embers, water droplets, electric arcs,
    // magic motes; none for an ordinary look.
    LOCKDOWN_API bool HitBurstOf(ELDObservedHitLook Look, EBurst& Out, FLinearColor& Tint);

    // Each active skill's own look: its tint (cast charge, release and dash trail) and its release burst. The table
    // covers every ELDDemoAbilityId (Lockdown.Rules.DemoBodyMotion.HitLayers checks 26 distinct entries).
    struct FSkillLook
    {
        uint8 Ability = 0;           // ELDDemoAbilityId
        const TCHAR* Id = TEXT("");  // the content id ("active_elbow")
        FLinearColor Tint = FLinearColor::White;
        EBurst Release = EBurst::Strike;
    };
    LOCKDOWN_API TConstArrayView<FSkillLook> SkillLooks();
    LOCKDOWN_API const FSkillLook* SkillLookOf(uint8 Ability);
    LOCKDOWN_API const FSkillLook* SkillLookOf(FName Id);

    // A visible skill effect (ELDObservedProjectileLook): a ground zone's looping particles (NS_LDZone) sized to its
    // radius, a flight's ribbon trail (NS_LDSwingTrail), and the bursts it lands, launches and ends with.
    struct FZone
    {
        bool bShow = false;
        float Rate = 0, Lifetime = 0, Scale = 1;
        FVector Velocity = FVector::ZeroVector;  // along the effect's heading (+X) and up (+Z)
        FLinearColor Colour = FLinearColor::Black;
    };
    LOCKDOWN_API FZone ZoneOf(ELDObservedProjectileLook Look, float RadiusCm, float EffectScale);
    struct FTrail
    {
        bool bShow = false;
        float WidthCm = 0, Lifetime = 0, Rate = 0;
        FLinearColor Colour = FLinearColor::Black;
    };
    LOCKDOWN_API FTrail TrailOf(ELDObservedProjectileLook Look, float EffectScale);
    // The burst a visible effect shows when it first appears (a launch, a landing) and when it ends (a flight's hit,
    // a dropped book); false when the look has none.
    LOCKDOWN_API bool AppearBurstOf(ELDObservedProjectileLook Look, EBurst& Out, FLinearColor& Tint);
    LOCKDOWN_API bool EndBurstOf(ELDObservedProjectileLook Look, EBurst& Out, FLinearColor& Tint);

    // A body moving faster than any run (every dash, not a knock-back or a fall) streaks a trail from its chest and
    // kicks dust where it starts.
    constexpr float DashSpeedCmPerSec = 950.f;
    LOCKDOWN_API bool IsDashing(float Speed2D, bool bKnockback, bool bFalling);
    LOCKDOWN_API FTrail DashTrailOf(const FLinearColor& Tint, float EffectScale);
    // Afterimages of a dashing body: a posed translucent copy of its mesh left every IntervalSeconds, fading over
    // Seconds. Reduced effects leave half as many.
    struct FGhosts
    {
        bool bShow = false;
        float IntervalSeconds = 0, Seconds = 0;
        FLinearColor Glow = FLinearColor::Black;
    };
    LOCKDOWN_API FGhosts GhostsOf(const FLinearColor& Tint, float EffectScale);
    LOCKDOWN_API float GhostFade(const FGhosts& Ghosts, float AgeSeconds);

    // An element reaction on a body (ELDCombatElement 1..6, the element that set it off; spec 12 floats its name in the
    // same colour): a cross flash and a ground ring in that colour plus the element's particles.
    LOCKDOWN_API FLinearColor ElementTint(uint8 Element);
    LOCKDOWN_API EBurst ElementBurst(uint8 Element);
    LOCKDOWN_API FRecipe ForReaction(uint8 Element, float EffectScale, const FLayerTuning& Tuning = FLayerTuning());
    // A public ground state (ELDGroundStateKind): looping particles over its footprint under its line pattern, and the
    // burst a freshly laid one appears with. The pattern and the owner outline stay (never colour alone).
    LOCKDOWN_API FZone GroundZoneOf(uint8 Kind, float RadiusCm, float EffectScale);
    LOCKDOWN_API bool GroundAppearBurstOf(uint8 Kind, EBurst& Out, FLinearColor& Tint);

    // Overdrive (#305): one colour for its charge, release and hits.
    LOCKDOWN_API FLinearColor OverdriveTint();
    // The charge while the key is held with a full bar (public, FLDObservedUnitView::bCharging): an energy column at the
    // feet that grows with Progress (held / OVERDRIVE.HoldSec), and the ready flare once the hold completes.
    LOCKDOWN_API FZone ChargeZoneOf(float Progress, float EffectScale);
    LOCKDOWN_API FRecipe ForChargeReady(float EffectScale, const FLayerTuning& Tuning = FLayerTuning());
    // The release: the cast the authority admitted as an overdrive (public FLDObservedUnitView::bOverdriveCast) shows
    // this shockwave when its timeline reaches the release; a short press or a cancelled charge is an ordinary cast.
    LOCKDOWN_API FRecipe ForOverdriveRelease(float EffectScale, const FLayerTuning& Tuning = FLayerTuning());
}
