#pragma once
#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UI/LDHUDStyle.h"

class UWidget;

// Procedural interface shapes drawn in a widget's NativePaint: the component frames (a rounded square for base rules, a
// chamfered octagon for skill mods, a circle for link rules), dashed empty places, rings and link lines. Points are in the
// painting widget's local space.
namespace LDUIShapes
{
    enum class EShape : uint8 { Rounded, Octagon, Circle, Diamond, Hexagon };

    inline float Chamfer(const FBox2D& Box){return FMath::Min(Box.GetSize().X,Box.GetSize().Y)*.24f;}
    inline TArray<FVector2D> Outline(EShape Shape,const FBox2D& Box,float Radius=8.f)
    {
        TArray<FVector2D> Points;
        const FVector2D Min=Box.Min,Max=Box.Max,Center=Box.GetCenter(),Half=Box.GetExtent();
        if(Shape==EShape::Circle)
        {
            constexpr int32 Segments=40;
            for(int32 Index=0;Index<=Segments;++Index)
            {
                const float Angle=2.f*PI*float(Index)/float(Segments);
                Points.Add(Center+FVector2D(FMath::Cos(Angle)*Half.X,FMath::Sin(Angle)*Half.Y));
            }
            return Points;
        }
        if(Shape==EShape::Octagon)
        {
            const float C=Chamfer(Box);
            Points={FVector2D(Min.X+C,Min.Y),FVector2D(Max.X-C,Min.Y),FVector2D(Max.X,Min.Y+C),FVector2D(Max.X,Max.Y-C),FVector2D(Max.X-C,Max.Y),
                FVector2D(Min.X+C,Max.Y),FVector2D(Min.X,Max.Y-C),FVector2D(Min.X,Min.Y+C),FVector2D(Min.X+C,Min.Y)};
            return Points;
        }
        if(Shape==EShape::Diamond)
        {
            Points={FVector2D(Center.X,Min.Y),FVector2D(Max.X,Center.Y),FVector2D(Center.X,Max.Y),FVector2D(Min.X,Center.Y),FVector2D(Center.X,Min.Y)};
            return Points;
        }
        if(Shape==EShape::Hexagon)
        {
            const float C=float(Half.X)*.5f;
            Points={FVector2D(Min.X+C,Min.Y),FVector2D(Max.X-C,Min.Y),FVector2D(Max.X,Center.Y),FVector2D(Max.X-C,Max.Y),FVector2D(Min.X+C,Max.Y),
                FVector2D(Min.X,Center.Y),FVector2D(Min.X+C,Min.Y)};
            return Points;
        }
        const float R=FMath::Min(Radius,FMath::Min(Half.X,Half.Y));
        const FVector2D Corners[]={FVector2D(Max.X-R,Min.Y+R),FVector2D(Max.X-R,Max.Y-R),FVector2D(Min.X+R,Max.Y-R),FVector2D(Min.X+R,Min.Y+R)};
        constexpr int32 Arc=5;
        for(int32 Corner=0;Corner<4;++Corner)
        {
            const float Start=-PI*.5f+PI*.5f*float(Corner);
            for(int32 Step=0;Step<=Arc;++Step)
            {
                const float Angle=Start+PI*.5f*float(Step)/float(Arc);
                Points.Add(Corners[Corner]+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*R);
            }
        }
        // Copy the first point before adding it: Add may grow the array and invalidate a reference into it.
        const FVector2D First=Points[0];
        Points.Add(First);
        return Points;
    }
    inline void Stroke(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const TArray<FVector2D>& Points,const FLinearColor& Color,float Thickness)
    {
        if(Points.Num()<2){return;}
        FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Thickness);
    }
    inline void Frame(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,const FLinearColor& Color,float Thickness,float Radius=8.f)
    {
        Stroke(Elements,Layer,Geometry,Outline(Shape,Box,Radius),Color,Thickness);
    }
    // A soft halo behind a frame (selection, legal place, scarce glow): a few widening translucent strokes.
    inline void Glow(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,const FLinearColor& Color,float Reach=6.f,float Radius=8.f)
    {
        for(int32 Ring=3;Ring>=1;--Ring)
        {
            const float Grow=Reach*float(Ring)/3.f;
            const FBox2D Wider(Box.Min-FVector2D(Grow,Grow),Box.Max+FVector2D(Grow,Grow));
            Stroke(Elements,Layer,Geometry,Outline(Shape,Wider,Radius+Grow),Color.CopyWithNewOpacity(Color.A*.22f*float(4-Ring)/3.f),2.f);
        }
    }
    // A dashed outline of Box (empty places and the next capacity cell).
    inline void Dashed(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,const FLinearColor& Color,float Dash=6.f,float Gap=4.f,float Thickness=1.5f)
    {
        const FVector2D Corners[]={Box.Min,FVector2D(Box.Max.X,Box.Min.Y),Box.Max,FVector2D(Box.Min.X,Box.Max.Y)};
        for(int32 Edge=0;Edge<4;++Edge)
        {
            const FVector2D From=Corners[Edge],To=Corners[(Edge+1)%4];
            const float Length=float(FVector2D::Distance(From,To));
            if(Length<=0.f){continue;}
            const FVector2D Step=(To-From)/Length;
            for(float At=0.f;At<Length;At+=Dash+Gap){Stroke(Elements,Layer,Geometry,{From+Step*At,From+Step*FMath::Min(At+Dash,Length)},Color,Thickness);}
        }
    }
    // An arc of a ring around Center from the twelve o'clock position, Fraction of a full turn clockwise (Start offsets it).
    inline void Arc(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,FVector2D Center,float Radius,float Fraction,const FLinearColor& Color,float Thickness,float Start=0.f)
    {
        const int32 Segments=FMath::Max(2,FMath::CeilToInt(48.f*FMath::Clamp(Fraction,0.f,1.f)));
        TArray<FVector2D> Points;
        for(int32 Index=0;Index<=Segments;++Index)
        {
            const float Angle=(Start+Fraction*float(Index)/float(Segments))*2.f*PI-PI*.5f;
            Points.Add(Center+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*Radius);
        }
        Stroke(Elements,Layer,Geometry,Points,Color,Thickness);
    }
    inline void Fill(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,const FLinearColor& Color,float Radius)
    {
        const FSlateRoundedBoxBrush Brush(Color,Radius);
        FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(Box.GetSize(),FSlateLayoutTransform(Box.Min)),&Brush);
    }
    // A card type's colour and shape (ELDCardType order: trigger, modify, convert, grow, element).
    inline FLinearColor KindColor(uint8 Type)
    {
        return Type==0?LDHUDStyle::CardTrigger:Type==1?LDHUDStyle::CardModify:Type==2?LDHUDStyle::CardConvert
            :Type==3?LDHUDStyle::CardGrow:LDHUDStyle::CardElement;
    }
    inline EShape KindShape(uint8 Type)
    {
        return Type==0?EShape::Rounded:Type==1?EShape::Octagon:Type==2?EShape::Diamond:Type==3?EShape::Hexagon:EShape::Circle;
    }
}
