#pragma once
#include "CoreMinimal.h"

// Presentation design system of the "on air" direction: the match is framed as a street-ball broadcast. Black and white
// carry everything; one ball orange marks "me" and "can act now"; live red marks danger only. Shapes are 12 degree
// parallelogram bars and square blocks with 2 px white rules; no rounded corners, soft shadows or glow on HUD chrome.
// The build ring (ULDWorkshop) keeps its approved street-paint card materials and may use Gold for legendary, keystone and
// selection highlights; every other interface uses these tokens. Combat colours that already have meaning in LDHUDStyle
// (own/enemy health, shield, momentum) stay there.
// Local presentation only; no gameplay value or rule lives here. Sizes are Slate units at the 1080p reference.
namespace LDBroadcast
{
    // Surfaces
    inline const FLinearColor Ink = FLinearColor(FColor(0x0A, 0x0B, 0x0D));
    inline const FLinearColor Panel = FLinearColor(FColor(0x17, 0x19, 0x1E));
    inline const FLinearColor Raised = FLinearColor(FColor(0x22, 0x25, 0x2C));
    inline const FLinearColor Rule = FLinearColor(FColor(0xFF, 0xFF, 0xFF));
    // Text
    inline const FLinearColor White = FLinearColor(FColor(0xFF, 0xFF, 0xFF));
    inline const FLinearColor Grey = FLinearColor(FColor(0x9A, 0xA0, 0xA8));
    inline const FLinearColor Mute = FLinearColor(FColor(0x5C, 0x61, 0x6A));
    // Accents: Orange is the only highlight, kept for what can be pressed now (a rank-up "+", points to spend, a waiting choice, the primary button); LiveRed is low health and danger;
    // Gold is legendary, overdrive and a maxed keystone.
    inline const FLinearColor Orange = FLinearColor(FColor(0xFF, 0x6B, 0x1A));
    inline const FLinearColor LiveRed = FLinearColor(FColor(0xE8, 0x00, 0x2E));
    inline const FLinearColor Gold = FLinearColor(FColor(0xFF, 0xD2, 0x3D));

    // Elements (ELDCombatElement order: none, fire, water, shock, chill, rot). Shared by HUD chips, the build ring's
    // medal sockets and the combat VFX tint, so one element reads as one colour everywhere.
    inline const FLinearColor Fire = FLinearColor(FColor(0xFF, 0x6A, 0x3D));
    inline const FLinearColor Water = FLinearColor(FColor(0x42, 0xB4, 0xFF));
    inline const FLinearColor Shock = FLinearColor(FColor(0xFF, 0xD2, 0x3D));
    inline const FLinearColor Chill = FLinearColor(FColor(0xA6, 0xEC, 0xFF));
    inline const FLinearColor Rot = FLinearColor(FColor(0xA4, 0xD2, 0x4A));

    // Rarity is shown by construction (band, stripe, materials), never by rim colour alone; these are the band inks.
    inline const FLinearColor Common = FLinearColor(FColor(0xC9, 0xCE, 0xD6));
    inline const FLinearColor Rare = FLinearColor(FColor(0x4F, 0xB0, 0xFF));
    inline const FLinearColor Legendary = FLinearColor(FColor(0xFF, 0xB8, 0x3D));

    // Seat ink: the jersey tint of a roster seat (ULDHumanoidVisualComponent::ContenderTint), lifted for UI legibility,
    // so a number plate, the kill feed, the scoreboard and the minimap name a seat with the colour its body wears.
    LOCKDOWN_API FLinearColor SeatInk(uint8 Slot);
    // Dark or white figure ink that reads on a seat plate.
    LOCKDOWN_API FLinearColor SeatFigure(uint8 Slot);

    // Type scale (1080p px). Body text never goes below Small; at 720p nothing renders below 12 px.
    inline constexpr float TypeSmall = 18.f;
    inline constexpr float TypeBody = 22.f;
    inline constexpr float TypeLabel = 26.f;
    inline constexpr float TypeTitle = 36.f;
    inline constexpr float TypeHeadline = 48.f;
    inline constexpr float TypeHero = 72.f;
    inline constexpr float TypeDisplay = 96.f;

    // Geometry
    inline constexpr float SkewDegrees = 12.f;
    inline constexpr float RuleWidth = 2.f;
    // Safe area: 5 % of the viewport on every edge.
    inline constexpr float SafeAreaFraction = .05f;

    // Motion (seconds). Slides wipe in fast-out slow-stop; numbers roll; a hard cut is two frames. No bounce on HUD chrome.
    inline constexpr float SlideIn = .20f;
    inline constexpr float NumberRoll = .30f;
    inline constexpr float HardCut = 2.f / 60.f;
}
