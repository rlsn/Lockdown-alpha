#pragma once

#include "CoreMinimal.h"
#include "Vision/LDViewTypes.h"
#include "Math/Vector.h"

namespace LDHitFeel { struct FTuning; }

// The designed effects of the combat presentation slice: the three basic swings and their hit tiers, the water line
// (active_hose_down), the shock chain (active_zap), the conduct reaction and the electric ground field. Sources:
// SourceData/VFX/Slice (rendered by SourceData/VFX/Blender/slice_*.py, listed with SHA-256 in vfx_slice_assets.json),
// imported by Scripts\ImportDemoArt.ps1 -Mode ImportVFX (LDDemoVfxImport) into Folder with the importer-built Material.
// Every slice texture packs RGB = glow (0 ink, 0.6 element body, 1 white-hot core) and A = coverage; a flipbook is a
// 4 x 4 atlas whose frame i sits at column i % 4, row i / 4.
namespace LDSliceVfx
{
    constexpr const TCHAR* Folder = TEXT("/Game/Lockdown/Art/VFX/Slice");
    // Translucent unlit two-sided: emissive = Ink where the glow is 0, the Glow body where it is 0.6 and Core where it
    // is 1; opacity = coverage x Fade x the U window x the dissolve erosion. The parameters below drive it.
    constexpr const TCHAR* Material = TEXT("/Game/Lockdown/Art/VFX/Slice/M_LDSlice");
    constexpr const TCHAR* MaskParameter = TEXT("Mask");
    constexpr const TCHAR* NoiseParameter = TEXT("Noise");
    constexpr const TCHAR* GlowParameter = TEXT("Glow");
    constexpr const TCHAR* InkParameter = TEXT("Ink");
    constexpr const TCHAR* CoreParameter = TEXT("Core");
    constexpr const TCHAR* FadeParameter = TEXT("Fade");
    // (columns, rows, frame, U repeats) of the atlas.
    constexpr const TCHAR* FlipParameter = TEXT("Flip");
    // (U window start, U window end, dissolve 0..1, U scroll) over the surface's first UV channel.
    constexpr const TCHAR* ShapeParameter = TEXT("Shape");
    enum class ETex : uint8
    {
        SlashEdge, DissolveNoise, HitSkill, WaterJet, WaterSplash, WetDecal, ChalkLane,
        // The generated effects' elements (vfx_slice_assets.json, slice_chain_candidates.py), used by the generated Niagara
        // materials and their ground layers.
        ChainAProfile, ChainAEnergyNoise, ChainASparkStreak, ChainAFlash, ChainAGroundGlow, ChainBScorchDash, Count
    };
    enum class EMesh : uint8 { Slash1, Slash2, Slash3, Count };
    // The manifest id ("fb_hit_basic", "SM_LD_SlashCombo1") and the package (Folder/T_fb_hit_basic, Folder/SM_LD_SlashCombo1).
    LOCKDOWN_API const TCHAR* TexId(ETex Tex);
    LOCKDOWN_API const TCHAR* MeshId(EMesh Mesh);
    LOCKDOWN_API FString TexturePackage(ETex Tex);
    LOCKDOWN_API FString MeshPackage(EMesh Mesh);
    LOCKDOWN_API bool IsFlipbook(ETex Tex);
    // The authored frame rate of a flipbook (16 frames each); 0 for a still texture.
    LOCKDOWN_API float FlipbookFps(ETex Tex);
    // The slash meshes lie in the swing plane with their origin at the swing centre, forward +X; combo stage 1 sweeps
    // right to left with its head on the left of forward, stage 2 the reverse, stage 3 is the wide finisher. Their imported
    // head points (UV U = 0 on the outer edge), which the importer checks after every import, in Unreal coordinates.
    LOCKDOWN_API FVector SlashHead(EMesh Mesh);
    // Niagara (LDDemoVfxNiagara, built by the VFX import commandlet). Every asset under GeneratedFolder is owned by the
    // generator, rebuilt by each import and never edited by hand.
    constexpr const TCHAR* GeneratedFolder = TEXT("/Game/Lockdown/Art/VFX/Niagara/Generated");
    // The generated systems (GeneratedFolder/NS_LD<name>); every one takes the user parameters below.
    //   Chain        one hop of a chain: Start the hand or the last body struck, End the body struck
    //   HitBasic     a basic (combo 1-2) hit at End struck from Start's side; HitFinisher the combo finisher's
    //   Swing1..3    a basic swing: Start the swing centre at the chest, End a point ahead along the facing
    //   Field        an electric field on the ground: Start its centre on the floor, Width its radius, Intensity its
    //                strength (2.2 full), Duration its life (the ground state's: effectively unbounded; conduct's splash)
    //   Tell1..3     a basic swing's anticipation on the floor: Start the feet, End ahead, Duration the windup
    enum class ESystem : uint8 { Chain, HitBasic, HitFinisher, Swing1, Swing2, Swing3, Field, Tell1, Tell2, Tell3, Count };
    LOCKDOWN_API const TCHAR* SystemName(ESystem System);
    LOCKDOWN_API FString SystemPackage(ESystem System);
    // The shocked-body rim glow (LDHitVfx::FGhosts::bShell): an additive fresnel shell pushed 1.2 cm out along the normals.
    constexpr const TCHAR* BodyGlowMaterial = TEXT("/Game/Lockdown/Art/VFX/Niagara/Generated/M_LDChainBodyGlow");
    LOCKDOWN_API FString GeneratedPackage(const TCHAR* Name);
    constexpr const TCHAR* ChainStart = TEXT("User.Start");
    constexpr const TCHAR* ChainEnd = TEXT("User.End");
    constexpr const TCHAR* ChainTint = TEXT("User.Tint");
    constexpr const TCHAR* ChainWidth = TEXT("User.Width");
    constexpr const TCHAR* ChainIntensity = TEXT("User.Intensity");
    constexpr const TCHAR* ChainDuration = TEXT("User.Duration");
    // 1 for the local player's own or focused effect, 0 for others' (60 % wide, half as bright, fewer parts).
    constexpr const TCHAR* ChainFocus = TEXT("User.Focus");

    // The importer-built unit quad: 100 x 100 cm in XY facing +Z, U = (X + 50) / 100, V = (Y + 50) / 100.
    constexpr const TCHAR* QuadMesh = TEXT("/Game/Lockdown/Art/VFX/Slice/SM_LDSliceQuad");

    // How a layer stands in the world. Every placement keeps the texture's +U along Dir.
    enum class EPlace : uint8
    {
        Facing,       // a quad turned to the camera at At, its +U along Dir as seen on screen; Pivot = the hit point's U
        Flat,         // a quad lying on the ground under At
        Strip,        // a quad from At to End, turned about that axis towards the camera
        GroundStrip,  // a quad from At to End lying on the ground
        Slash,        // a slash mesh with its swing centre at At
        GroundSlash,  // a slash mesh laid on the ground under At
    };
    struct FLayer
    {
        ETex Tex = ETex::HitSkill;
        EMesh Mesh = EMesh::Count;   // Slash and GroundSlash only
        EPlace Place = EPlace::Facing;
        FVector At = FVector::ZeroVector, End = FVector::ZeroVector, Dir = FVector::ForwardVector;
        float SizeCm = 100;          // quad width, strip width, or a slash mesh's scale x 100
        float Pivot = .5f;
        float DelaySeconds = 0, Seconds = 0;
        float FadeInSeconds = .02f, FadeOutSeconds = .08f;
        float Alpha = 1;
        FLinearColor Glow = FLinearColor::White, Ink = FLinearColor::Black, Core = FLinearColor::White;
        // The visible U window grows from [RevealLo, RevealLo] to [RevealLo, 1] (or, reversed, from the tail) over
        // RevealSeconds; ErodeFrom (seconds) starts the dissolve that takes ErodeSeconds; ChalkErode is a constant grain.
        float RevealSeconds = 0;
        bool bRevealFromTail = false;
        float ErodeFrom = -1, ErodeSeconds = .1f, ChalkErode = 0;
        float TileCm = 0;            // strips: one texture repeat per TileCm of length; 0 stretches it once
        float ScrollPerSecond = 0;
        int32 Variants = 1;          // strips: stacked rows picked at VariantHz
        float VariantHz = 0;
        float GrowFrom = 1;          // size scale at the start, eased to 1 over the first quarter
        bool bLoop = false;
        bool bOptional = false;      // dropped with reduced effects
        bool bTop = false;           // drawn over the other layers
        bool bTelegraph = false;     // an anticipation layer, cleared when its windup is withdrawn or cancelled

    };
    constexpr int32 MaxLayers = 12;
    struct FRecipe
    {
        TArray<FLayer, TFixedAllocator<MaxLayers>> Layers;
    };
    // What one layer shows AgeSeconds after its recipe started.
    struct FState
    {
        bool bVisible = false;
        float Fade = 0, Frame = 0, WindowLo = -1, WindowHi = 2, Dissolve = 0, Scroll = 0, Scale = 1;
    };
    LOCKDOWN_API FState Evaluate(const FLayer& Layer, float AgeSeconds);
    LOCKDOWN_API bool IsOver(const FLayer& Layer, float AgeSeconds);

    // Which visible strike a hit belongs to (ULDImpactFxSubsystem::NoteStrike / MatchHit); its tier picks the silhouette:
    // basic < finisher (the 3rd basic) < skill < reaction.
    enum class EStrike : uint8 { None, Basic, Finisher, Skill, Hose, Zap };
    // The hits the slice shows (Impact): a plain, water or electric hit without a critical, shield, overdrive, dodge or
    // parry outcome. Every other hit keeps its hit layers (LDHitVfx::ForHit).
    LOCKDOWN_API bool ShowsHit(ELDObservedHitLook Look, uint8 HitFlags);
    // bOwn: the strike is the local player's (white with the ball orange; others' strikes are white only). bFocus: the
    // local player strikes or is struck, shown full size; others' fights are smaller and fainter (80 % size, 70 %
    // strength) so they stay readable but quieter. Reduced effects (EffectScale < 1) drop optional layers and dim.
    struct FLook
    {
        bool bOwn = false;
        bool bFocus = false;
        float EffectScale = 1;
        // ld.Feel / DA_CombatVfx tuning of this kind of layer: size, duration and brightness multipliers.
        float Size = 1, Seconds = 1, Glow = 1;

    };
    // A basic swing's anticipation and arc are the Niagara Tell and Swing (ULDImpactFxSubsystem::SwingTell, Swing).
    // The ink of a physical strike's effects (swing, hit): the local player's own a soft orange, others' a pale ivory.
    LOCKDOWN_API FLinearColor StrikeTint(const FLook& Look);
    // A skill's chalk lane on the ground from the caster's feet along its facing (Length x Width), during the windup.
    LOCKDOWN_API FRecipe SkillTelegraph(const FVector& Feet, const FVector& Facing, float LengthCm, float WidthCm, float WindupSeconds, const FLook& Look);
    // active_hose_down at its release: the water line from the hand along the facing over its range, with a muzzle splash.
    LOCKDOWN_API FRecipe HoseRelease(const FVector& Hand, const FVector& Facing, float LengthCm, const FLook& Look);
    // active_zap at its release: the contact spark at the casting hand (the arcs follow the hits it lands).
    LOCKDOWN_API FRecipe ZapRelease(const FVector& Hand, const FLook& Look);
    // One landed hit of a strike on a visible body, its slice layers: the skill burst at the chest along Dir for a skill
    // (white, orange for the local player's, element ink for water), water's splash and wet ground, the zap's contact
    // light and scorch on the ground. The basic and finisher hits themselves are the Niagara Hit
    // (ULDImpactFxSubsystem::Hit), a zap's link the Niagara Chain.
    LOCKDOWN_API FRecipe Impact(EStrike Strike, const FVector& Chest, const FVector& Feet, const FVector& Dir, const FVector& From,
        bool bChained, const FLook& Look);
}

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
    // One observed hit (LDObservation::EHitFlags) the slice does not show (LDSliceVfx::ShowsHit: an unflagged plain, water
    // or electric hit is LDSliceVfx::Impact, so it has no layers here): critical cross star and a double gold ring;
    // a shield's cyan hex shell (a broken one bursts outwards over a wide ring); then the look's signature. An overdrive
    // hit replaces the star with a large magenta cross and adds its own wide ring and spinning ground sigil.
    // Reduced effects drop the optional layers and dim the rest; no layer that tells the tier or the look is removed.
    LOCKDOWN_API FRecipe ForHit(ELDObservedHitLook Look, uint8 HitFlags, float EffectScale, const FLayerTuning& Tuning = FLayerTuning());
    // A visible body's fall: a glowing ground crack that stays two seconds and a double shock ring.
    LOCKDOWN_API FRecipe ForKill(float EffectScale, const FLayerTuning& Tuning = FLayerTuning());
    // Ground dust shock-ring at dash launch and golden flare ring on PerfectDodge (#335 §3.1).
    LOCKDOWN_API FRecipe ForDashStart(const FLinearColor& Tint, float EffectScale, const FLayerTuning& Tuning = FLayerTuning());
    LOCKDOWN_API FRecipe ForPerfectDodge(float EffectScale, const FLayerTuning& Tuning = FLayerTuning());
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
        // An additive rim shell just outside the pose (LDSliceVfx::BodyGlowMaterial) instead of the afterimage material.
        bool bShell = false;
    };
    LOCKDOWN_API FGhosts GhostsOf(const FLinearColor& Tint, float EffectScale);
    LOCKDOWN_API float GhostFade(const FGhosts& Ghosts, float AgeSeconds);

    // An element reaction on a body other than conduct (LDSliceVfx::Conduct) (ELDCombatElement 1..6, the element that set
    // it off; spec 12 floats its name in the same colour): a cross flash and a ground ring in that colour plus the
    // element's particles. ElementTint is the broadcast element ink (LDBroadcast).
    LOCKDOWN_API FLinearColor ElementTint(uint8 Element);
    LOCKDOWN_API EBurst ElementBurst(uint8 Element);
    LOCKDOWN_API FRecipe ForReaction(uint8 Element, float EffectScale, const FLayerTuning& Tuning = FLayerTuning());
    // A public ground state (ELDGroundStateKind): looping particles over its footprint under its line pattern, and the
    // burst a freshly laid one appears with; the electric field has neither (it is the Niagara Field, ULDImpactFxSubsystem::ShowGroundField). The
    // pattern and the owner outline stay (never colour alone).
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
