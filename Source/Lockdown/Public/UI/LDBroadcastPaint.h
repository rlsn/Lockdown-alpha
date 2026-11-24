#pragma once
#include "CoreMinimal.h"
#include "UI/LDUIPaint.h"

// The on-air chrome's painted shapes (LDBroadcastStyle tokens): 12 degree parallelogram slabs, square blocks, 2 px rules,
// seat plates (seat ink + jersey number), segmented bars with an overdrive notch, and text in the figure face. Everything
// paints into a widget's own geometry (ULDPaintLayer); local presentation only, no gameplay value lives here.
namespace LDBroadcastPaint
{
    // The horizontal run of the broadcast lean over Height.
    LOCKDOWN_API float Lean(float Height);
    // The corners of Box leaning right by the broadcast skew: its top edge shifted right by half the lean, its bottom edge
    // left by half. A flat side stays vertical (the slab then grows or shrinks on the other side only).
    LOCKDOWN_API TArray<FVector2D> Slab(const FBox2D& Box,bool bFlatLeft=false,bool bFlatRight=false);
    LOCKDOWN_API void FillSlab(const LDUIPaint::FContext& P,int32 Layer,const FBox2D& Box,const FLinearColor& Color,bool bFlatLeft=false,bool bFlatRight=false);
    // An axis-aligned block.
    LOCKDOWN_API void Block(const LDUIPaint::FContext& P,int32 Layer,const FBox2D& Box,const FLinearColor& Color);
    // A crisp straight rule (default 2 px).
    LOCKDOWN_API void Rule(const LDUIPaint::FContext& P,int32 Layer,FVector2D From,FVector2D To,const FLinearColor& Color,float Width=2.f);
    // A square frame of Width inside Box.
    LOCKDOWN_API void Frame(const LDUIPaint::FContext& P,int32 Layer,const FBox2D& Box,const FLinearColor& Color,float Width=2.f);

    // Fonts at a 1080p pixel size (the type scale), times the local text scale. Figure: condensed heavy italic numbers.
    LOCKDOWN_API FSlateFontInfo FigureFont(float Px,float TextScale=1.f);
    LOCKDOWN_API FSlateFontInfo LabelFont(float Px,bool bHeavy,float TextScale=1.f);
    // The Slate font size of a 1080p pixel size (Slate sizes are points at 96 DPI).
    LOCKDOWN_API float PointsOf(float Px);
    LOCKDOWN_API FVector2D Measure(const FString& Text,const FSlateFontInfo& Font);
    // Paints Text so that Align (0..1 per axis) of its box lies at At; returns the painted box.
    LOCKDOWN_API FBox2D Text(const LDUIPaint::FContext& P,int32 Layer,const FString& Text,const FSlateFontInfo& Font,FVector2D At,
        const FLinearColor& Color,FVector2D Align=FVector2D::ZeroVector);

    // A seat plate: a slab in the seat's ink with its jersey number (Seat + 1) in the figure ink that reads on it. A seat
    // outside the roster paints a neutral plate with "?".
    LOCKDOWN_API void SeatPlate(const LDUIPaint::FContext& P,int32 Layer,const FBox2D& Box,int32 Seat,float TextScale=1.f,float FigurePx=0.f);

    // A bar: a dark track, a white damage trail, the fill, an optional shield run after the fill, thin cuts every Segment
    // (a fraction of the track; 0 = none) and a notch at Notch (0..1, <0 = none) standing above and below the track.
    struct FBar
    {
        float Fraction=0.f;
        float Trail=-1.f;
        float Shield=0.f;
        FLinearColor Fill=FLinearColor::White;
        FLinearColor Track=FLinearColor(0.f,0.f,0.f,.6f);
        FLinearColor ShieldInk=FLinearColor(FColor(0xA6,0xEC,0xFF));
        float Segment=0.f;
        float Notch=-1.f;
        FLinearColor NotchInk=FLinearColor::White;
        bool bLean=true;
    };
    LOCKDOWN_API void Bar(const LDUIPaint::FContext& P,int32 Layer,const FBox2D& Box,const FBar& Bar);
    // The fraction of Box's width at which Fraction of a leaning bar's fill ends (for anchoring marks on it).
    LOCKDOWN_API float BarX(const FBox2D& Box,float Fraction);
}
