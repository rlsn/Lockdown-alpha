#pragma once
#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"
#include "UI/LDUIShapes.h"

class UTexture2D;

// The interface's painted surfaces at the design system's values: gradient fills, frames, soft glows and drop shadows,
// icons clipped to the component shapes. Everything is drawn into a widget's paint pass (no material assets): rounded
// rectangles with an axis gradient use Slate's own gradient element; other shapes and diagonal or conic gradients use
// vertex-coloured triangle fans with a one-pixel feathered edge. Gradients interpolate in sRGB like the design source.
namespace LDUIPaint
{
    using LDUIShapes::EShape;

    // A colour from a design hex value (sRGB) and opacity.
    inline FLinearColor Hex(uint32 Rgb,float Alpha=1.f)
    {
        return FLinearColor(FColor(uint8((Rgb>>16)&0xFF),uint8((Rgb>>8)&0xFF),uint8(Rgb&0xFF),255)).CopyWithNewOpacity(Alpha);
    }

    struct FStop
    {
        float At=0.f;
        FLinearColor Color=FLinearColor::Transparent;
    };
    // A gradient like the design's linear-gradient (Angle in degrees: 180 runs top to bottom, 90 left to right) or, with
    // bConic, a conic sweep around the centre starting at Angle.
    struct FFill
    {
        TArray<FStop> Stops;
        float Angle=180.f;
        bool bConic=false;
        bool IsEmpty() const {return Stops.IsEmpty();}
        static FFill Solid(const FLinearColor& Color){FFill Out;Out.Stops={{0.f,Color},{1.f,Color}};return Out;}
        static FFill Linear(float Angle,std::initializer_list<FStop> Stops){FFill Out;Out.Angle=Angle;Out.Stops=Stops;return Out;}
        LOCKDOWN_API FLinearColor At(float T) const;
    };

    // A shape's closed outline without the repeated last point; every outline of a shape has the same number of points,
    // so an outline and its grown copy pair up point for point.
    LOCKDOWN_API TArray<FVector2D> Ring(EShape Shape,const FBox2D& Box,float Radius);
    LOCKDOWN_API FBox2D Grow(const FBox2D& Box,float By);

    // The shape filled with Fill (multiplied by Opacity).
    LOCKDOWN_API void Shape(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,float Radius,
        const FFill& Fill,float Opacity=1.f);
    // Any convex polygon (local points, without the repeated last point) filled with Fill.
    LOCKDOWN_API void Polygon(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const TArray<FVector2D>& Points,const FFill& Fill,
        float Opacity=1.f);
    // A border of Width inside the shape's edge.
    LOCKDOWN_API void Edge(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,float Radius,
        const FLinearColor& Color,float Width);
    // A soft halo around the shape: Color at the edge fading out over Reach (a glow), or, with an Offset and a dark colour,
    // a drop shadow.
    LOCKDOWN_API void Halo(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,float Radius,
        const FLinearColor& Color,float Reach,FVector2D Offset=FVector2D::ZeroVector);
    // An inner glow: Color at the edge fading towards the inside over Reach.
    LOCKDOWN_API void InnerHalo(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,float Radius,
        const FLinearColor& Color,float Reach);
    // Texture covering the shape (cropped to cover, like object-fit: cover), clipped to it.
    LOCKDOWN_API void Picture(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,float Radius,
        UTexture2D* Texture,const FLinearColor& Tint=FLinearColor::White);
    // A dashed outline along the shape.
    LOCKDOWN_API void DashedEdge(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,float Radius,
        const FLinearColor& Color,float Width=2.f,float Dash=6.f,float Gap=5.f);
    // A straight line with feathered sides; EndColor (when given) fades it along its length.
    LOCKDOWN_API void Line(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,FVector2D From,FVector2D To,const FLinearColor& Color,
        float Width,const FLinearColor& EndColor=FLinearColor(0,0,0,-1));

    // ---- the design system's surfaces ----

    // A raised panel: the vertical surface gradient, a one-pixel highlight on the top, the hairline border, the drop shadow
    // and the yellow corner fold top-left with a thin tick bottom-right (bFolds).
    LOCKDOWN_API void Panel(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,bool bFolds=true,float Opacity=1.f);
    // Card: a choice card's raised surface (.ocard); CardActive / CardBuild / CardStone provide #328 §2 3-way silhouettes.
    enum class EButton : uint8 { Normal, Primary, Event, Plus, Key, Tag, TagOn, Ghost, Card, CardActive, CardBuild, CardStone };
    struct FButtonState
    {
        bool bHovered=false;
        bool bPressed=false;
        bool bEnabled=true;
        FLinearColor Tint=FLinearColor::Transparent;
        // The opacity inherited from the widget tree (render opacity and fading parents).
        float Fade=1.f;
    };
    LOCKDOWN_API void Button(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,EButton Look,const FButtonState& State);
    // A dark round-ended chip (the momentum cost and cooldown pills).
    LOCKDOWN_API void Pill(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,const FLinearColor& Border=FLinearColor(1,1,1,.075f),
        const FLinearColor& Fill=FLinearColor(0,0,0,.42f));
    // A card tile: the type's shape (LDUIShapes::KindShape) with its gradient frame, the icon face inset by the frame, and
    // the drop shadow.
    enum class ETileState : uint8 { Normal, Hovered, Selected, Legal, Replace, Rejected, Dim, Hole };
    LOCKDOWN_API void Tile(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,uint8 Kind,
        ETileState State,UTexture2D* Icon,float Opacity=1.f,double Time=0.);
    // A skill head: the gold (or, for the body and dash, silver) gradient frame with its shadow; the icon face inside.
    LOCKDOWN_API void SkillArt(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,bool bSilver,bool bEmpty,
        UTexture2D* Icon,const FLinearColor& Glow=FLinearColor::Transparent,float Opacity=1.f);
    // An element disc (fire, water, electric) behind its one-character label.
    LOCKDOWN_API void ElementDisc(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,const FLinearColor& Color,float Opacity=1.f);

    // A paint layer's inherited opacity applies to every primitive, including custom vertices and Slate lines.
    struct FContext
    {
        FSlateWindowElementList& Elements;
        int32 Layer;
        const FGeometry& Geometry;
        float Opacity;

        FContext(FSlateWindowElementList& InElements,int32 InLayer,const FGeometry& InGeometry,float InOpacity=1.f)
            :Elements(InElements),Layer(InLayer),Geometry(InGeometry),Opacity(FMath::Clamp(InOpacity,0.f,1.f)){}
        FContext WithOpacity(float InOpacity) const {return FContext(Elements,Layer,Geometry,Opacity*InOpacity);}
        FLinearColor Fade(const FLinearColor& Color) const {return Color.CopyWithNewOpacity(Color.A*Opacity);}

        void Shape(int32 At,EShape Form,const FBox2D& Box,float Radius,const FFill& Fill,float Alpha=1.f) const
        {LDUIPaint::Shape(Elements,At,Geometry,Form,Box,Radius,Fill,Opacity*Alpha);}
        void Polygon(int32 At,const TArray<FVector2D>& Points,const FFill& Fill,float Alpha=1.f) const
        {LDUIPaint::Polygon(Elements,At,Geometry,Points,Fill,Opacity*Alpha);}
        void Edge(int32 At,EShape Form,const FBox2D& Box,float Radius,const FLinearColor& Color,float Width) const
        {LDUIPaint::Edge(Elements,At,Geometry,Form,Box,Radius,Fade(Color),Width);}
        void Halo(int32 At,EShape Form,const FBox2D& Box,float Radius,const FLinearColor& Color,float Reach,FVector2D Offset=FVector2D::ZeroVector) const
        {LDUIPaint::Halo(Elements,At,Geometry,Form,Box,Radius,Fade(Color),Reach,Offset);}
        void InnerHalo(int32 At,EShape Form,const FBox2D& Box,float Radius,const FLinearColor& Color,float Reach) const
        {LDUIPaint::InnerHalo(Elements,At,Geometry,Form,Box,Radius,Fade(Color),Reach);}
        void DashedEdge(int32 At,EShape Form,const FBox2D& Box,float Radius,const FLinearColor& Color,float Width=2.f,float Dash=6.f,float Gap=5.f) const
        {LDUIPaint::DashedEdge(Elements,At,Geometry,Form,Box,Radius,Fade(Color),Width,Dash,Gap);}
        void Line(int32 At,FVector2D From,FVector2D To,const FLinearColor& Color,float Width,const FLinearColor& EndColor=FLinearColor(0,0,0,-1)) const
        {LDUIPaint::Line(Elements,At,Geometry,From,To,Fade(Color),Width,EndColor.A<0.f?EndColor:Fade(EndColor));}
        void Panel(int32 At,const FBox2D& Box,bool bFolds=true,float Alpha=1.f) const
        {LDUIPaint::Panel(Elements,At,Geometry,Box,bFolds,Opacity*Alpha);}
        void Pill(int32 At,const FBox2D& Box,const FLinearColor& Border=FLinearColor(1,1,1,.075f),const FLinearColor& Fill=FLinearColor(0,0,0,.42f)) const
        {LDUIPaint::Pill(Elements,At,Geometry,Box,Fade(Border),Fade(Fill));}
        void Tile(int32 At,const FBox2D& Box,uint8 Kind,ETileState State,UTexture2D* Icon,float Alpha=1.f,double Time=0.) const
        {LDUIPaint::Tile(Elements,At,Geometry,Box,Kind,State,Icon,Opacity*Alpha,Time);}
        void SkillArt(int32 At,const FBox2D& Box,bool bSilver,bool bEmpty,UTexture2D* Icon,const FLinearColor& Glow=FLinearColor::Transparent) const
        {LDUIPaint::SkillArt(Elements,At,Geometry,Box,bSilver,bEmpty,Icon,Glow,Opacity);}
        void ElementDisc(int32 At,const FBox2D& Box,const FLinearColor& Color,float Alpha=1.f) const
        {LDUIPaint::ElementDisc(Elements,At,Geometry,Box,Color,Opacity*Alpha);}
        void Stroke(int32 At,const TArray<FVector2D>& Points,const FLinearColor& Color,float Thickness) const
        {LDUIShapes::Stroke(Elements,At,Geometry,Points,Fade(Color),Thickness);}
        void Arc(int32 At,FVector2D Center,float Radius,float Fraction,const FLinearColor& Color,float Thickness,float Start=0.f) const
        {LDUIShapes::Arc(Elements,At,Geometry,Center,Radius,Fraction,Fade(Color),Thickness,Start);}
        void Fill(int32 At,const FBox2D& Box,const FLinearColor& Color,float Radius) const
        {LDUIShapes::Fill(Elements,At,Geometry,Box,Fade(Color),Radius);}
        void Frame(int32 At,EShape Form,const FBox2D& Box,const FLinearColor& Color,float Thickness,float Radius=8.f) const
        {LDUIShapes::Frame(Elements,At,Geometry,Form,Box,Fade(Color),Thickness,Radius);}
        void Glow(int32 At,EShape Form,const FBox2D& Box,const FLinearColor& Color,float Reach=6.f,float Radius=8.f) const
        {LDUIShapes::Glow(Elements,At,Geometry,Form,Box,Fade(Color),Reach,Radius);}
    };
}
