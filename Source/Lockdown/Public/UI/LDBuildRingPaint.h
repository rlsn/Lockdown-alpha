#pragma once
#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Rendering/DrawElements.h"

class UTexture2D;

// The build ring's paint primitives: everything is drawn in the ring widget's own paint pass in local Slate units, with no
// material assets. Every primitive takes the next layer, so overlapping faces (the fanned hand, lifted cards) always stack in
// the order they are painted.
namespace LDRingPaint
{
    struct FPainter
    {
        FSlateWindowElementList& Elements;
        const FGeometry& Geometry;
        int32 Layer;
        float Opacity=1.f;
        // A transform applied to every point and text drawn while set (a fanned card's rotation).
        FTransform2D Xform;
        bool bXform=false;

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
        // A texture filling Box (cropped to cover), clipped to the convex Clip outline when given, rotated by Degrees.
        LOCKDOWN_API void Image(UTexture2D* Texture,const FBox2D& Box,const FLinearColor& Tint=FLinearColor::White,const TArray<FVector2D>* Clip=nullptr,
            float Degrees=0.f);
        // Many small squares in one batch (halftone dots), each Size wide, coloured by Color(at).
        LOCKDOWN_API void Dots(const TArray<FVector2D>& Centres,float Size,TFunctionRef<FLinearColor(FVector2D)> Color);
        // Text with its top-left at Align (0..1) of its measured box placed at At; Skew slants it (degrees, italic).
        LOCKDOWN_API FVector2D Text(const FString& Value,FVector2D At,const FSlateFontInfo& Font,const FLinearColor& Color,
            FVector2D Align=FVector2D::ZeroVector,float SkewDegrees=0.f);
    };

    LOCKDOWN_API FVector2D Measure(const FString& Value,const FSlateFontInfo& Font);
    // Greedy line breaking for CJK and Latin text inside Width; at most MaxLines, the last one ellipsised.
    LOCKDOWN_API TArray<FString> Wrap(const FString& Value,const FSlateFontInfo& Font,float Width,int32 MaxLines);
    // Outline builders (local units).
    LOCKDOWN_API TArray<FVector2D> Rounded(const FBox2D& Box,float Radius,int32 Segments=4);
    LOCKDOWN_API TArray<FVector2D> Chamfer(const FBox2D& Box,float Cut);
    // Two opposite corners (top-left, bottom-right) cut by Cut: the skill host's frame.
    LOCKDOWN_API TArray<FVector2D> HostCut(const FBox2D& Box,float Cut);
    LOCKDOWN_API TArray<FVector2D> Octagon(const FBox2D& Box,float CutFraction=.3f);
    // A parallelogram slanted by SkewDegrees (its top edge shifted right).
    LOCKDOWN_API TArray<FVector2D> Slant(const FBox2D& Box,float Shift);
    LOCKDOWN_API TArray<FVector2D> Arc(FVector2D Centre,float Radius,float FromDegrees,float ToDegrees,int32 Segments=0);
    // The point at Radius and Degrees (clockwise from straight up) around Centre.
    LOCKDOWN_API FVector2D Polar(FVector2D Centre,float Radius,float Degrees);
    // A path displaced along its normals by a deterministic noise of Amplitude (the spray-painted edge).
    LOCKDOWN_API TArray<FVector2D> Jitter(const TArray<FVector2D>& Path,float Amplitude,uint32 Seed,float Step);
    // Sutherland-Hodgman: the part of convex Subject inside convex Clip.
    LOCKDOWN_API TArray<FVector2D> ClipConvex(const TArray<FVector2D>& Subject,const TArray<FVector2D>& Clip);
    LOCKDOWN_API bool Inside(const TArray<FVector2D>& Convex,FVector2D Point);
    LOCKDOWN_API FLinearColor Mix(const FLinearColor& A,const FLinearColor& B,float T);
    inline FLinearColor Hex(uint32 Rgb,float Alpha=1.f)
    {
        return FLinearColor(FColor(uint8((Rgb>>16)&0xFF),uint8((Rgb>>8)&0xFF),uint8(Rgb&0xFF),255)).CopyWithNewOpacity(Alpha);
    }
}
