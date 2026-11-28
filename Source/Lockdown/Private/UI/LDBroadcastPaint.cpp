#include "UI/LDBroadcastPaint.h"
#include "UI/LDBroadcastStyle.h"
#include "UI/LDUIFonts.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

float LDBroadcastPaint::Lean(float Height)
{
    return Height*FMath::Tan(FMath::DegreesToRadians(LDBroadcast::SkewDegrees));
}
TArray<FVector2D> LDBroadcastPaint::Slab(const FBox2D& Box,bool bFlatLeft,bool bFlatRight)
{
    const double Half=Lean(float(Box.Max.Y-Box.Min.Y))*.5;
    const double TopShift=Half,BottomShift=-Half;
    const FVector2D TopLeft(Box.Min.X+(bFlatLeft?0.:TopShift),Box.Min.Y);
    const FVector2D TopRight(Box.Max.X+(bFlatRight?0.:TopShift),Box.Min.Y);
    const FVector2D BottomRight(Box.Max.X+(bFlatRight?0.:BottomShift),Box.Max.Y);
    const FVector2D BottomLeft(Box.Min.X+(bFlatLeft?0.:BottomShift),Box.Max.Y);
    return {TopLeft,TopRight,BottomRight,BottomLeft};
}
void LDBroadcastPaint::FillSlab(const LDUIPaint::FContext& P,int32 Layer,const FBox2D& Box,const FLinearColor& Color,bool bFlatLeft,bool bFlatRight)
{
    if(Color.A<=0.f||Box.Max.X<=Box.Min.X||Box.Max.Y<=Box.Min.Y){return;}
    P.Polygon(Layer,Slab(Box,bFlatLeft,bFlatRight),LDUIPaint::FFill::Solid(Color));
}
void LDBroadcastPaint::Block(const LDUIPaint::FContext& P,int32 Layer,const FBox2D& Box,const FLinearColor& Color)
{
    if(Color.A<=0.f||Box.Max.X<=Box.Min.X||Box.Max.Y<=Box.Min.Y){return;}
    FSlateDrawElement::MakeBox(P.Elements,Layer,P.Geometry.ToPaintGeometry(FVector2f(Box.GetSize()),FSlateLayoutTransform(FVector2f(Box.Min))),
        FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,P.Fade(Color));
}
void LDBroadcastPaint::Rule(const LDUIPaint::FContext& P,int32 Layer,FVector2D From,FVector2D To,const FLinearColor& Color,float Width)
{
    if(FMath::IsNearlyEqual(From.Y,To.Y))
    {Block(P,Layer,FBox2D(FVector2D(FMath::Min(From.X,To.X),From.Y-Width*.5f),FVector2D(FMath::Max(From.X,To.X),From.Y+Width*.5f)),Color);return;}
    if(FMath::IsNearlyEqual(From.X,To.X))
    {Block(P,Layer,FBox2D(FVector2D(From.X-Width*.5f,FMath::Min(From.Y,To.Y)),FVector2D(From.X+Width*.5f,FMath::Max(From.Y,To.Y))),Color);return;}
    P.Line(Layer,From,To,Color,Width);
}
void LDBroadcastPaint::Frame(const LDUIPaint::FContext& P,int32 Layer,const FBox2D& Box,const FLinearColor& Color,float Width)
{
    const FVector2D A=Box.Min,B=Box.Max;
    Block(P,Layer,FBox2D(A,FVector2D(B.X,A.Y+Width)),Color);
    Block(P,Layer,FBox2D(FVector2D(A.X,B.Y-Width),B),Color);
    Block(P,Layer,FBox2D(FVector2D(A.X,A.Y+Width),FVector2D(A.X+Width,B.Y-Width)),Color);
    Block(P,Layer,FBox2D(FVector2D(B.X-Width,A.Y+Width),FVector2D(B.X,B.Y-Width)),Color);
}
float LDBroadcastPaint::PointsOf(float Px)
{
    return FMath::Max(1.f,FMath::RoundToFloat(Px*.75f));
}
FSlateFontInfo LDBroadcastPaint::FigureFont(float Px,float TextScale)
{
    return LDUIFonts::Font(LDUIFonts::ERole::Figure,PointsOf(Px*TextScale));
}
FSlateFontInfo LDBroadcastPaint::LabelFont(float Px,bool bHeavy,float TextScale)
{
    FSlateFontInfo Font=LDUIFonts::Font(LDUIFonts::ERole::Strong,PointsOf(Px*TextScale));
    if(bHeavy){Font.TypefaceFontName=FName(TEXT("Heavy"));}
    return Font;
}
FVector2D LDBroadcastPaint::Measure(const FString& Text,const FSlateFontInfo& Font)
{
    if(Text.IsEmpty()||!FSlateApplication::IsInitialized()||!FSlateApplication::Get().GetRenderer()){return FVector2D::ZeroVector;}
    return FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text,Font));
}
FBox2D LDBroadcastPaint::Text(const LDUIPaint::FContext& P,int32 Layer,const FString& Text,const FSlateFontInfo& Font,FVector2D At,
    const FLinearColor& Color,FVector2D Align)
{
    const FVector2D Size=Measure(Text,Font);
    const FVector2D TopLeft(FMath::RoundToDouble(At.X-Size.X*Align.X),FMath::RoundToDouble(At.Y-Size.Y*Align.Y));
    if(!Text.IsEmpty()&&Color.A>0.f)
    {
        FSlateDrawElement::MakeText(P.Elements,Layer,P.Geometry.ToPaintGeometry(FVector2f(Size),FSlateLayoutTransform(FVector2f(TopLeft))),
            Text,Font,ESlateDrawEffect::None,P.Fade(Color));
    }
    return FBox2D(TopLeft,TopLeft+Size);
}
void LDBroadcastPaint::SeatPlate(const LDUIPaint::FContext& P,int32 Layer,const FBox2D& Box,int32 Seat,float TextScale,float FigurePx)
{
    const bool bKnown=Seat>=0&&Seat<=255;
    const FLinearColor Ink=bKnown?LDBroadcast::SeatInk(uint8(Seat)):LDBroadcast::Raised;
    const FLinearColor Figure=bKnown?LDBroadcast::SeatFigure(uint8(Seat)):LDBroadcast::White;
    FillSlab(P,Layer,Box,Ink);
    const float Height=float(Box.Max.Y-Box.Min.Y);
    const float Px=FigurePx>0.f?FigurePx:Height*.82f;
    Text(P,Layer+1,bKnown?FString::FromInt(Seat+1):FString(TEXT("?")),FigureFont(Px,TextScale),Box.GetCenter()+FVector2D(0.,Height*.02),Figure,FVector2D(.5,.5));
}
float LDBroadcastPaint::BarX(const FBox2D& Box,float Fraction)
{
    return float(Box.Min.X+(Box.Max.X-Box.Min.X)*FMath::Clamp(Fraction,0.f,1.f));
}
void LDBroadcastPaint::Bar(const LDUIPaint::FContext& P,int32 Layer,const FBox2D& Box,const FBar& In)
{
    const auto Run=[&](float From,float To,const FLinearColor& Color,int32 At)
    {
        From=FMath::Clamp(From,0.f,1.f);To=FMath::Clamp(To,0.f,1.f);
        if(To<=From+.0005f){return;}
        const FBox2D Part(FVector2D(BarX(Box,From),Box.Min.Y),FVector2D(BarX(Box,To),Box.Max.Y));
        if(In.bLean){FillSlab(P,At,Part,Color);}else{Block(P,At,Part,Color);}
    };
    Run(0.f,1.f,In.Track,Layer);
    if(In.Trail>In.Fraction){Run(In.Fraction,In.Trail,LDBroadcast::White.CopyWithNewOpacity(.85f),Layer+1);}
    Run(0.f,In.Fraction,In.Fill,Layer+2);
    if(In.Shield>0.f){Run(In.Fraction,In.Fraction+In.Shield,In.ShieldInk,Layer+2);}
    if(In.Segment>.01f)
    {
        // Cuts in the track colour: the bar reads as countable blocks of health.
        const float Height=float(Box.Max.Y-Box.Min.Y);
        const float Shift=In.bLean?Lean(Height)*.5f:0.f;
        for(float Cut=In.Segment;Cut<.999f;Cut+=In.Segment)
        {
            const float X=BarX(Box,Cut);
            P.Line(Layer+3,FVector2D(X+Shift,Box.Min.Y),FVector2D(X-Shift,Box.Max.Y),LDBroadcast::Ink.CopyWithNewOpacity(.9f),2.f);
        }
    }
}
