#pragma once
#include "CoreMinimal.h"
#include "GenericPlatform/ICursor.h"
class UGameViewportClient;

// The game's own pointer: software cursor widgets on the game viewport, so no platform cursor (cross, hand, text beam,
// resize arrows) ever shows over the game or its UI. Every cursor type draws the same plain arrow (white face, dark
// outline, the tip on the hotspot) except the two aiming types below, which add a coloured glow round its edge.
namespace LDCursor
{
    // A confirmed attack-move (A) waits for its left click: red glow.
    inline constexpr EMouseCursor::Type AttackAim=EMouseCursor::Crosshairs;
    // A confirmed active skill or equipment active waits for its target: blue glow.
    inline constexpr EMouseCursor::Type CastAim=EMouseCursor::EyeDropper;
    // Maps every cursor type of this viewport to the arrows; call once per game viewport.
    LOCKDOWN_API void Install(UGameViewportClient& Viewport);
}
