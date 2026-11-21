#pragma once
#include "CoreMinimal.h"
#include "GenericPlatform/ICursor.h"
class UGameViewportClient;

// The game's own pointer: software cursor widgets on the game viewport, so no platform cursor (cross, hand, text beam,
// resize arrows) ever shows over the game or its UI. Every cursor type draws the same plain arrow (white face, dark
// outline, the tip on the hotspot) except the two aiming types below, which add a coloured glow round its edge.
namespace LDCursor
{
    // Hovering an attackable enemy contender or camp monster within 12 px: crimson war-sword (#FF4757, #329 §4.3).
    inline constexpr EMouseCursor::Type EnemySword=EMouseCursor::CardinalCross;
    // A confirmed attack-move (A) waits for its left click: crimson-gold precision crosshair.
    inline constexpr EMouseCursor::Type AttackAim=EMouseCursor::Crosshairs;
    // A confirmed active skill or PK throw waits for its target: cyan-gold precision crosshair (#38BDF8, #329 §4.3).
    inline constexpr EMouseCursor::Type CastAim=EMouseCursor::EyeDropper;
    // Hovering unreachable ground: grey-red forbidden X mark (#329 §4.3).
    inline constexpr EMouseCursor::Type BlockedGround=EMouseCursor::SlashedCircle;
    // Maps every cursor type of this viewport to the tactical cursors; call once per game viewport.
    LOCKDOWN_API void Install(UGameViewportClient& Viewport);
}
