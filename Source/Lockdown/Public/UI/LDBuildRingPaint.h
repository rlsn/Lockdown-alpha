#pragma once
#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Rendering/DrawElements.h"

class UTexture2D;

// The build ring's paint primitives: everything is drawn in the ring widget's own paint pass in local Slate units, with no
// material assets. Every primitive takes the next layer, so overlapping parts (a hovered tile over its neighbours, the link
// stroke under the tiles) always stack in the order they are painted.
namespace LDRingPaint
{
    struct FPainter
    {
        FSlateWindowElementList& Elements;
        const FGeometry& Geometry;
        int32 Layer;
        float Opacity=1.f;

        FPainter(FSlateWindowElementList& InElements,const FGeometry& InGeometry,int32 InLayer):Elements(InElements),Geometry(InGeometry),Layer(InLayer){}
        int32 Next(){return ++Layer;}
        FLinearColor Fade(const FLinearColor& Color) const {return Color.CopyWithNewOpacity(Color.A*Opacity);}

        // A convex polygon (no repeated last point) in one colour, or with a colour per point.
        LOCKDOWN_API void Fill(const TArray<FVector2D>& Points,const FLinearColor& Color);
        LOCKDOWN_API void FillShaded(const TArray<FVector2D>& Points,TFunctionRef<FLinearColor(FVector2D)> Color);
        // The band between two outlines of the same point count (an annular sector, a ring), coloured per point.
        LOCKDOWN_API void Band(const TArray<FVector2D>& Inner,const TArray<FVector2D>& Outer,TFunctionRef<FLinearColor(FVector2D)> Color);
        // A polyline of Width; EndColor (alpha >= 0) fades the colour along it.
        LOCKDOWN_API void Stroke(const TArray<FVector2D>& Path,float Width,const FLinearColor& Color,bool bClosed=false,
            const FLinearColor& EndColor=FLinearColor(0,0,0,-1));
        LOCKDOWN_API void Dashed(const TArray<FVector2D>& Path,float Width,float Dash,float Gap,const FLinearColor& Color,bool bClosed=false);
        // A texture filling Box (cropped to cover), clipped to the convex Clip outline when given.
        LOCKDOWN_API void Image(UTexture2D* Texture,const FBox2D& Box,const FLinearColor& Tint=FLinearColor::White,const TArray<FVector2D>* Clip=nullptr);
        // Many straight segments of Width in one batch (dial ticks): Ends holds the two ends of each, Color(index) its ink.
        LOCKDOWN_API void Segments(const TArray<FVector2D>& Ends,float Width,TFunctionRef<FLinearColor(int32)> Color);
        // Text with its top-left at Align (0..1) of its measured box placed at At; returns the measured size.
        LOCKDOWN_API FVector2D Text(const FString& Value,FVector2D At,const FSlateFontInfo& Font,const FLinearColor& Color,
            FVector2D Align=FVector2D::ZeroVector);
    };

    LOCKDOWN_API FVector2D Measure(const FString& Value,const FSlateFontInfo& Font);
    // Greedy line breaking for CJK and Latin text inside Width; at most MaxLines, the last one ellipsised.
    LOCKDOWN_API TArray<FString> Wrap(const FString& Value,const FSlateFontInfo& Font,float Width,int32 MaxLines);
    // Outline builders (local units).
    LOCKDOWN_API TArray<FVector2D> Arc(FVector2D Centre,float Radius,float FromDegrees,float ToDegrees,int32 Segments=0);
    // The point at Radius and Degrees (clockwise from straight up) around Centre.
    LOCKDOWN_API FVector2D Polar(FVector2D Centre,float Radius,float Degrees);
    LOCKDOWN_API bool Inside(const TArray<FVector2D>& Convex,FVector2D Point);
    LOCKDOWN_API FLinearColor Mix(const FLinearColor& A,const FLinearColor& B,float T);
}
