#include "UI/LDBroadcastStyle.h"
#include "Character/LDHumanoidVisualComponent.h"

FLinearColor LDBroadcast::SeatInk(uint8 Slot)
{
    // Jersey tints are dark linear values tuned for cloth under scene light; lift value and keep hue for flat UI.
    FLinearColor Hsv = ULDHumanoidVisualComponent::ContenderTint(Slot).LinearRGBToHSV();
    Hsv.B = FMath::Max(Hsv.B, .92f);
    Hsv.G = FMath::Min(Hsv.G, .85f);
    return Hsv.HSVToLinearRGB();
}

FLinearColor LDBroadcast::SeatFigure(uint8 Slot)
{
    return SeatInk(Slot).GetLuminance() > .35f ? Ink : White;
}
