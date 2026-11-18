#pragma once

#include "CoreMinimal.h"
#include "Vision/LDViewTypes.h"

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
    constexpr int32 MaxLayers = 6;
    struct FRecipe
    {
        TArray<FLayer, TFixedAllocator<MaxLayers>> Layers;
    };
    // One observed hit (LDObservation::EHitFlags): ordinary star and ring; critical cross star and a double gold ring;
    // a shield's cyan hex shell (a broken one bursts outwards over a wide ring); then the look's signature.
    // Reduced effects drop the optional layers and dim the rest; no layer that tells the tier or the look is removed.
    LOCKDOWN_API FRecipe ForHit(ELDObservedHitLook Look, uint8 HitFlags, float EffectScale);
    // A visible body's fall: a glowing ground crack that stays two seconds and a double shock ring.
    LOCKDOWN_API FRecipe ForKill(float EffectScale);
    // 0 before DelaySeconds and from Seconds on; a quick rise to 1 at PeakAt, then an ease-out fade.
    LOCKDOWN_API float Brightness(const FLayer& Layer, float AgeSeconds);
    LOCKDOWN_API float ScaleAt(const FLayer& Layer, float AgeSeconds);

    // The flash star keeps at least MinSpritePx of a 1080-line view whatever the camera distance.
    constexpr float MinSpritePx = 60.f;
    LOCKDOWN_API float SpriteCm(float SizeCm, float DistanceCm, float FovDegrees);

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
}
