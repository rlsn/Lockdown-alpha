#include "UI/LDUIPaint.h"
#include "Algo/Reverse.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "UI/LDHUDStyle.h"

namespace
{
    constexpr int32 PaintArcPoints=9;
    constexpr int32 PaintEdgePoints=4;

    FColor PaintSrgb(const FLinearColor& Color){return Color.ToFColor(true);}
    FLinearColor PaintLerp(const FLinearColor& A,const FLinearColor& B,float T)
    {
        // Like the design source, gradients blend the encoded (sRGB) channels.
        const FColor X=PaintSrgb(A),Y=PaintSrgb(B);
        const auto Mix=[T](uint8 P,uint8 Q){return uint8(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(float(P),float(Q),T)),0,255));};
        FLinearColor Out=FLinearColor::FromSRGBColor(FColor(Mix(X.R,Y.R),Mix(X.G,Y.G),Mix(X.B,Y.B),255));
        Out.A=FMath::Lerp(A.A,B.A,T);
        return Out;
    }
    FSlateResourceHandle PaintHandle(UTexture2D* Texture)
    {
        if(!FSlateApplication::IsInitialized()){return FSlateResourceHandle();}
        FSlateRenderer* Renderer=FSlateApplication::Get().GetRenderer();
        if(!Renderer){return FSlateResourceHandle();}
        if(!Texture){return Renderer->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")));}
        // One brush per texture for the life of the process: the handle refers to it.
        static TMap<TWeakObjectPtr<UTexture2D>,TSharedPtr<FSlateBrush>> Brushes;
        TSharedPtr<FSlateBrush>& Brush=Brushes.FindOrAdd(Texture);
        if(!Brush.IsValid())
        {
            Brush=MakeShared<FSlateBrush>();Brush->SetResourceObject(Texture);Brush->DrawAs=ESlateBrushDrawType::Image;
            Brush->ImageSize=FVector2D(Texture->GetSizeX(),Texture->GetSizeY());
        }
        return Renderer->GetResourceHandle(*Brush);
    }
    // Inserts Count points evenly on every edge, so two outlines of the same shape always pair up point for point.
    TArray<FVector2D> PaintSubdivide(const TArray<FVector2D>& Corners,int32 Count)
    {
        TArray<FVector2D> Out;
        for(int32 Index=0;Index<Corners.Num();++Index)
        {
            const FVector2D From=Corners[Index],To=Corners[(Index+1)%Corners.Num()];
            for(int32 Step=0;Step<Count;++Step){Out.Add(FMath::Lerp(From,To,double(Step)/double(Count)));}
        }
        return Out;
    }
    FVector2D PaintCentre(const TArray<FVector2D>& Points)
    {
        FVector2D Sum=FVector2D::ZeroVector;
        for(const FVector2D& Point:Points){Sum+=Point;}
        return Points.IsEmpty()?Sum:Sum/double(Points.Num());
    }
    // The outward normal at each point of a convex outline.
    TArray<FVector2D> PaintNormals(const TArray<FVector2D>& Points)
    {
        TArray<FVector2D> Out;Out.SetNum(Points.Num());
        const FVector2D Centre=PaintCentre(Points);
        for(int32 Index=0;Index<Points.Num();++Index)
        {
            const FVector2D Prev=Points[(Index+Points.Num()-1)%Points.Num()],Next=Points[(Index+1)%Points.Num()];
            const FVector2D Tangent=(Next-Prev).GetSafeNormal();
            FVector2D Normal(Tangent.Y,-Tangent.X);
            if(FVector2D::DotProduct(Normal,Points[Index]-Centre)<0.){Normal=-Normal;}
            Out[Index]=Normal.IsNearlyZero()?(Points[Index]-Centre).GetSafeNormal():Normal;
        }
        return Out;
    }
    struct FPaintMesh
    {
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;
        const FSlateRenderTransform* Transform=nullptr;
        int32 Add(FVector2D At,const FLinearColor& Color,FVector2D UV=FVector2D(.5,.5))
        {
            Vertices.Add(FSlateVertex::Make(*Transform,FVector2f(At),FVector2f(UV),PaintSrgb(Color)));
            return Vertices.Num()-1;
        }
        void Tri(int32 A,int32 B,int32 C){Indices.Add(SlateIndex(A));Indices.Add(SlateIndex(B));Indices.Add(SlateIndex(C));}
        void Quad(int32 A,int32 B,int32 C,int32 D){Tri(A,B,C);Tri(A,C,D);}
        void Draw(FSlateWindowElementList& Elements,int32 Layer,const FSlateResourceHandle& Handle)
        {
            if(Indices.IsEmpty()){return;}
            FSlateDrawElement::MakeCustomVerts(Elements,Layer,Handle,Vertices,Indices,nullptr,0,0);
        }
    };
    // One local unit is Geometry.Scale pixels; the feather is a pixel wide.
    float PaintPixel(const FGeometry& Geometry){return 1.f/FMath::Max(.1f,Geometry.Scale);}

    // A fan from the centre to the outline, with a feathered rim; colour and UV come per point.
    void PaintFan(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const TArray<FVector2D>& Points,
        TFunctionRef<FLinearColor(FVector2D)> Color,TFunctionRef<FVector2D(FVector2D)> UV,const FSlateResourceHandle& Handle)
    {
        if(Points.Num()<3){return;}
        FPaintMesh Mesh;Mesh.Transform=&Geometry.GetAccumulatedRenderTransform();
        const FVector2D Centre=PaintCentre(Points);
        const int32 Hub=Mesh.Add(Centre,Color(Centre),UV(Centre));
        const TArray<FVector2D> Normals=PaintNormals(Points);
        const float Pixel=PaintPixel(Geometry);
        const int32 N=Points.Num();
        TArray<int32> Inner,Outer;
        for(int32 Index=0;Index<N;++Index)
        {
            const FVector2D At=Points[Index]-Normals[Index]*(Pixel*.5f);
            Inner.Add(Mesh.Add(At,Color(Points[Index]),UV(At)));
            const FVector2D Out=Points[Index]+Normals[Index]*(Pixel*.5f);
            Outer.Add(Mesh.Add(Out,Color(Points[Index]).CopyWithNewOpacity(0.f),UV(Out)));
        }
        for(int32 Index=0;Index<N;++Index)
        {
            const int32 Next=(Index+1)%N;
            Mesh.Tri(Hub,Inner[Index],Inner[Next]);
            Mesh.Quad(Inner[Index],Outer[Index],Outer[Next],Inner[Next]);
        }
        Mesh.Draw(Elements,Layer,Handle);
    }
    // The band between two outlines that pair up point for point, coloured per outline.
    void PaintBand(FPaintMesh& Mesh,const TArray<FVector2D>& A,const FLinearColor& ColorA,const TArray<FVector2D>& B,const FLinearColor& ColorB)
    {
        const int32 N=FMath::Min(A.Num(),B.Num());
        TArray<int32> First,Second;
        for(int32 Index=0;Index<N;++Index){First.Add(Mesh.Add(A[Index],ColorA));Second.Add(Mesh.Add(B[Index],ColorB));}
        for(int32 Index=0;Index<N;++Index)
        {
            const int32 Next=(Index+1)%N;
            Mesh.Quad(First[Index],Second[Index],Second[Next],First[Next]);
        }
    }
    float PaintFillT(const LDUIPaint::FFill& Fill,const FBox2D& Box,FVector2D At)
    {
        const FVector2D Centre=Box.GetCenter(),Size=Box.GetSize();
        if(Fill.bConic)
        {
            const FVector2D D=At-Centre;
            const float Degrees=FMath::RadiansToDegrees(FMath::Atan2(float(D.X),float(-D.Y)));
            return FMath::Fmod(Degrees-Fill.Angle+720.f,360.f)/360.f;
        }
        const float Radians=FMath::DegreesToRadians(Fill.Angle);
        const FVector2D Direction(FMath::Sin(Radians),-FMath::Cos(Radians));
        const float Length=FMath::Abs(float(Size.X)*FMath::Sin(Radians))+FMath::Abs(float(Size.Y)*FMath::Cos(Radians));
        return Length>0.f?.5f+float(FVector2D::DotProduct(At-Centre,Direction))/Length:.5f;
    }
}

FLinearColor LDUIPaint::FFill::At(float T) const
{
    if(Stops.IsEmpty()){return FLinearColor::Transparent;}
    T=FMath::Clamp(T,0.f,1.f);
    if(T<=Stops[0].At){return Stops[0].Color;}
    for(int32 Index=1;Index<Stops.Num();++Index)
    {
        if(T<=Stops[Index].At)
        {
            const float Span=Stops[Index].At-Stops[Index-1].At;
            return PaintLerp(Stops[Index-1].Color,Stops[Index].Color,Span>0.f?(T-Stops[Index-1].At)/Span:1.f);
        }
    }
    return Stops.Last().Color;
}

FBox2D LDUIPaint::Grow(const FBox2D& Box,float By){return FBox2D(Box.Min-FVector2D(By,By),Box.Max+FVector2D(By,By));}

TArray<FVector2D> LDUIPaint::Ring(EShape Shape,const FBox2D& Box,float Radius)
{
    TArray<FVector2D> Points;
    const FVector2D Min=Box.Min,Max=Box.Max,Centre=Box.GetCenter(),Half=Box.GetExtent();
    if(Shape==EShape::Circle)
    {
        constexpr int32 Segments=48;
        for(int32 Index=0;Index<Segments;++Index)
        {
            const float Angle=2.f*PI*float(Index)/float(Segments)-PI*.5f;
            Points.Add(Centre+FVector2D(FMath::Cos(Angle)*Half.X,FMath::Sin(Angle)*Half.Y));
        }
        return Points;
    }
    if(Shape==EShape::Octagon)
    {
        const double C=FMath::Min(Box.GetSize().X,Box.GetSize().Y)*.22;
        return PaintSubdivide({FVector2D(Min.X+C,Min.Y),FVector2D(Max.X-C,Min.Y),FVector2D(Max.X,Min.Y+C),FVector2D(Max.X,Max.Y-C),FVector2D(Max.X-C,Max.Y),
            FVector2D(Min.X+C,Max.Y),FVector2D(Min.X,Max.Y-C),FVector2D(Min.X,Min.Y+C)},PaintEdgePoints);
    }
    const double R=FMath::Clamp(double(Radius),0.,FMath::Min(Half.X,Half.Y));
    const FVector2D Corners[]={FVector2D(Max.X-R,Min.Y+R),FVector2D(Max.X-R,Max.Y-R),FVector2D(Min.X+R,Max.Y-R),FVector2D(Min.X+R,Min.Y+R)};
    for(int32 Corner=0;Corner<4;++Corner)
    {
        const float Start=-PI*.5f+PI*.5f*float(Corner);
        for(int32 Step=0;Step<PaintArcPoints;++Step)
        {
            const float Angle=Start+PI*.5f*float(Step)/float(PaintArcPoints-1);
            Points.Add(Corners[Corner]+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*R);
        }
        // The straight edge to the next corner, so a long side still carries its gradient.
        const FVector2D From=Points.Last();
        const int32 NextCorner=(Corner+1)%4;
        const float NextAngle=-PI*.5f+PI*.5f*float(NextCorner);
        const FVector2D To=Corners[NextCorner]+FVector2D(FMath::Cos(NextAngle),FMath::Sin(NextAngle))*R;
        for(int32 Step=1;Step<PaintEdgePoints;++Step){Points.Add(FMath::Lerp(From,To,double(Step)/double(PaintEdgePoints)));}
    }
    return Points;
}

void LDUIPaint::Polygon(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const TArray<FVector2D>& Points,const FFill& Fill,float Opacity)
{
    if(Fill.IsEmpty()||Points.Num()<3||Opacity<=0.f){return;}
    FBox2D Bounds(ForceInit);for(const FVector2D& Point:Points){Bounds+=Point;}
    PaintFan(Elements,Layer,Geometry,Points,[&](FVector2D At){FLinearColor C=Fill.At(PaintFillT(Fill,Bounds,At));C.A*=Opacity;return C;},
        [](FVector2D){return FVector2D(.5,.5);},PaintHandle(nullptr));
}

void LDUIPaint::Shape(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,float Radius,const FFill& Fill,float Opacity)
{
    if(Fill.IsEmpty()||Opacity<=0.f||Box.GetSize().X<=0.||Box.GetSize().Y<=0.){return;}
    const float Angle=FMath::Fmod(Fill.Angle+360.f,360.f);
    const bool bAxis=!Fill.bConic&&(FMath::IsNearlyEqual(Angle,180.f)||FMath::IsNearlyEqual(Angle,90.f)||FMath::IsNearlyEqual(Angle,0.f)||FMath::IsNearlyEqual(Angle,270.f));
    if(Shape==EShape::Rounded&&bAxis)
    {
        // Slate's own gradient: exact stops and antialiased rounded corners.
        const bool bAcross=FMath::IsNearlyEqual(Angle,90.f)||FMath::IsNearlyEqual(Angle,270.f);
        const bool bReverse=FMath::IsNearlyEqual(Angle,0.f)||FMath::IsNearlyEqual(Angle,270.f);
        const FVector2D Size=Box.GetSize();
        TArray<FSlateGradientStop> Stops;
        for(const FStop& Stop:Fill.Stops)
        {
            const double At=bReverse?1.-Stop.At:Stop.At;
            FLinearColor Color=Stop.Color;Color.A*=Opacity;
            Stops.Add(FSlateGradientStop(bAcross?FVector2D(Size.X*At,0.):FVector2D(0.,Size.Y*At),Color));
        }
        if(bReverse){Algo::Reverse(Stops);}
        const float R=FMath::Clamp(Radius,0.f,float(FMath::Min(Size.X,Size.Y))*.5f);
        FSlateDrawElement::MakeGradient(Elements,Layer,Geometry.ToPaintGeometry(Size,FSlateLayoutTransform(Box.Min)),Stops,
            bAcross?Orient_Vertical:Orient_Horizontal,ESlateDrawEffect::None,FVector4f(R,R,R,R));
        return;
    }
    PaintFan(Elements,Layer,Geometry,Ring(Shape,Box,Radius),[&](FVector2D At){FLinearColor C=Fill.At(PaintFillT(Fill,Box,At));C.A*=Opacity;return C;},
        [](FVector2D){return FVector2D(.5,.5);},PaintHandle(nullptr));
}

void LDUIPaint::Edge(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,float Radius,const FLinearColor& Color,float Width)
{
    if(Width<=0.f||Color.A<=0.f){return;}
    const float Pixel=PaintPixel(Geometry);
    const TArray<FVector2D> Outside=Ring(Shape,Grow(Box,Pixel*.5f),Radius+Pixel*.5f);
    const TArray<FVector2D> Outer=Ring(Shape,Box,Radius);
    const TArray<FVector2D> Inner=Ring(Shape,Grow(Box,-Width),FMath::Max(0.f,Radius-Width));
    const TArray<FVector2D> Inside=Ring(Shape,Grow(Box,-Width-Pixel*.5f),FMath::Max(0.f,Radius-Width-Pixel*.5f));
    FPaintMesh Mesh;Mesh.Transform=&Geometry.GetAccumulatedRenderTransform();
    PaintBand(Mesh,Outside,Color.CopyWithNewOpacity(0.f),Outer,Color);
    PaintBand(Mesh,Outer,Color,Inner,Color);
    PaintBand(Mesh,Inner,Color,Inside,Color.CopyWithNewOpacity(0.f));
    Mesh.Draw(Elements,Layer,PaintHandle(nullptr));
}

void LDUIPaint::Halo(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,float Radius,const FLinearColor& Color,float Reach,FVector2D Offset)
{
    if(Reach<=0.f||Color.A<=0.f){return;}
    const FBox2D Core(Box.Min+Offset,Box.Max+Offset);
    // A blur's falloff in four rings; the core under the shape is filled so a translucent surface still sits on its shadow.
    constexpr float Steps[]={0.f,.22f,.5f,1.f};
    constexpr float Alphas[]={1.f,.55f,.18f,0.f};
    FPaintMesh Mesh;Mesh.Transform=&Geometry.GetAccumulatedRenderTransform();
    TArray<TArray<FVector2D>> Rings;
    for(const float Step:Steps){Rings.Add(Ring(Shape,Grow(Core,Reach*Step),Radius+Reach*Step));}
    for(int32 Index=0;Index+1<Rings.Num();++Index)
    {PaintBand(Mesh,Rings[Index],Color.CopyWithNewOpacity(Color.A*Alphas[Index]),Rings[Index+1],Color.CopyWithNewOpacity(Color.A*Alphas[Index+1]));}
    const int32 Hub=Mesh.Add(Core.GetCenter(),Color);
    TArray<int32> Rim;for(const FVector2D& Point:Rings[0]){Rim.Add(Mesh.Add(Point,Color));}
    for(int32 Index=0;Index<Rim.Num();++Index){Mesh.Tri(Hub,Rim[Index],Rim[(Index+1)%Rim.Num()]);}
    Mesh.Draw(Elements,Layer,PaintHandle(nullptr));
}

void LDUIPaint::InnerHalo(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,float Radius,const FLinearColor& Color,float Reach)
{
    if(Reach<=0.f||Color.A<=0.f){return;}
    const float Most=float(FMath::Min(Box.GetSize().X,Box.GetSize().Y))*.5f-1.f;
    const float Depth=FMath::Min(Reach,Most);
    const TArray<FVector2D> Mid=Ring(Shape,Grow(Box,-Depth*.4f),FMath::Max(0.f,Radius-Depth*.4f));
    FPaintMesh Mesh;Mesh.Transform=&Geometry.GetAccumulatedRenderTransform();
    PaintBand(Mesh,Ring(Shape,Box,Radius),Color,Mid,Color.CopyWithNewOpacity(Color.A*.35f));
    PaintBand(Mesh,Mid,Color.CopyWithNewOpacity(Color.A*.35f),Ring(Shape,Grow(Box,-Depth),FMath::Max(0.f,Radius-Depth)),Color.CopyWithNewOpacity(0.f));
    Mesh.Draw(Elements,Layer,PaintHandle(nullptr));
}

void LDUIPaint::Picture(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,float Radius,UTexture2D* Texture,const FLinearColor& Tint)
{
    if(!Texture||Tint.A<=0.f){return;}
    const FVector2D Size=Box.GetSize();
    if(Size.X<=0.||Size.Y<=0.){return;}
    // Cover: the texture's shorter side fills the box, the longer one is cropped evenly.
    const double Aspect=double(Texture->GetSizeX())/FMath::Max(1.,double(Texture->GetSizeY()));
    const double BoxAspect=Size.X/Size.Y;
    FVector2D Span(1,1);
    if(Aspect>BoxAspect){Span.X=BoxAspect/Aspect;}else{Span.Y=Aspect/BoxAspect;}
    const FVector2D Start=(FVector2D(1,1)-Span)*.5;
    PaintFan(Elements,Layer,Geometry,Ring(Shape,Box,Radius),[&](FVector2D){return Tint;},
        [&](FVector2D At){const FVector2D Unit=(At-Box.Min)/Size;return Start+Unit*Span;},PaintHandle(Texture));
}

void LDUIPaint::Line(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,FVector2D From,FVector2D To,const FLinearColor& Color,float Width,const FLinearColor& EndColor)
{
    const FVector2D Along=(To-From).GetSafeNormal();
    if(Along.IsNearlyZero()){return;}
    const FLinearColor End=EndColor.A<0.f?Color:EndColor;
    const FVector2D Side(-Along.Y,Along.X);
    const float Pixel=PaintPixel(Geometry);
    const float Half=Width*.5f;
    FPaintMesh Mesh;Mesh.Transform=&Geometry.GetAccumulatedRenderTransform();
    const int32 A0=Mesh.Add(From-Side*(Half+Pixel*.5f),Color.CopyWithNewOpacity(0.f)),A1=Mesh.Add(From-Side*Half,Color),A2=Mesh.Add(From+Side*Half,Color),
        A3=Mesh.Add(From+Side*(Half+Pixel*.5f),Color.CopyWithNewOpacity(0.f));
    const int32 B0=Mesh.Add(To-Side*(Half+Pixel*.5f),End.CopyWithNewOpacity(0.f)),B1=Mesh.Add(To-Side*Half,End),B2=Mesh.Add(To+Side*Half,End),
        B3=Mesh.Add(To+Side*(Half+Pixel*.5f),End.CopyWithNewOpacity(0.f));
    Mesh.Quad(A0,B0,B1,A1);Mesh.Quad(A1,B1,B2,A2);Mesh.Quad(A2,B2,B3,A3);
    Mesh.Draw(Elements,Layer,PaintHandle(nullptr));
}

void LDUIPaint::DashedEdge(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,EShape Shape,const FBox2D& Box,float Radius,const FLinearColor& Color,float Width,float Dash,float Gap)
{
    TArray<FVector2D> Points=Ring(Shape,Grow(Box,-Width*.5f),FMath::Max(0.f,Radius-Width*.5f));
    if(Points.Num()<2){return;}
    const FVector2D First=Points[0];
    Points.Add(First);
    float Carry=0.f;bool bOn=true;
    for(int32 Index=0;Index+1<Points.Num();++Index)
    {
        FVector2D From=Points[Index];const FVector2D To=Points[Index+1];
        float Left=float(FVector2D::Distance(From,To));
        const FVector2D Step=(To-From).GetSafeNormal();
        while(Left>KINDA_SMALL_NUMBER)
        {
            const float Need=(bOn?Dash:Gap)-Carry;
            const float Take=FMath::Min(Need,Left);
            const FVector2D Next=From+Step*Take;
            if(bOn){Line(Elements,Layer,Geometry,From,Next,Color,Width);}
            From=Next;Left-=Take;Carry+=Take;
            if(Carry>=(bOn?Dash:Gap)-KINDA_SMALL_NUMBER){Carry=0.f;bOn=!bOn;}
        }
    }
}

void LDUIPaint::Panel(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,bool bFolds,float Opacity)
{
    // .panel: the surface gradient, the hairline, the top highlight, a deep soft shadow and the two corner marks.
    Halo(Elements,Layer,Geometry,EShape::Rounded,Box,6.f,FLinearColor(0,0,0,.5f*Opacity),46.f,FVector2D(0,22));
    Shape(Elements,Layer+1,Geometry,EShape::Rounded,Box,6.f,FFill::Linear(180.f,{{0.f,Hex(0x1A2131,.93f)},{1.f,Hex(0x0D111B,.95f)}}),Opacity);
    Edge(Elements,Layer+1,Geometry,EShape::Rounded,Box,6.f,FLinearColor(1,1,1,.075f*Opacity),1.f);
    Line(Elements,Layer+1,Geometry,FVector2D(Box.Min.X+6.f,Box.Min.Y+1.5f),FVector2D(Box.Max.X-6.f,Box.Min.Y+1.5f),FLinearColor(1,1,1,.09f*Opacity),1.f);
    if(!bFolds){return;}
    const FLinearColor Accent=LDHUDStyle::UIAction.CopyWithNewOpacity(.9f*Opacity);
    const FVector2D Corner=Box.Min+FVector2D(.5f,.5f);
    TArray<FVector2D> Fold{FVector2D(Corner.X,Corner.Y+24.f)};
    for(int32 Step=0;Step<=6;++Step)
    {
        const float Angle=PI+PI*.5f*float(Step)/6.f;
        Fold.Add(FVector2D(Corner.X+5.f,Corner.Y+5.f)+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*5.f);
    }
    Fold.Add(FVector2D(Corner.X+24.f,Corner.Y));
    LDUIShapes::Stroke(Elements,Layer+2,Geometry,Fold,Accent,3.f);
    const FVector2D Far=Box.Max-FVector2D(1.f,1.f);
    TArray<FVector2D> Tick{FVector2D(Far.X,Far.Y-17.f)};
    for(int32 Step=0;Step<=6;++Step)
    {
        const float Angle=PI*.5f*float(Step)/6.f;
        Tick.Add(FVector2D(Far.X-5.f,Far.Y-5.f)+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*5.f);
    }
    Tick.Add(FVector2D(Far.X-17.f,Far.Y));
    LDUIShapes::Stroke(Elements,Layer+2,Geometry,Tick,FLinearColor(1,1,1,.14f*Opacity),2.f);
}

void LDUIPaint::Button(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,EButton Look,const FButtonState& State)
{
    const float Opacity=State.bEnabled?1.f:.42f;
    const bool bHot=State.bEnabled&&State.bHovered;
    const bool bDown=State.bEnabled&&State.bPressed;
    const FLinearColor Accent=LDHUDStyle::UIAction;
    switch(Look)
    {
    case EButton::Primary:
    case EButton::Plus:
    {
        const float Radius=5.f;
        const float GlowReach=Look==EButton::Plus?14.f:26.f;
        const float GlowAlpha=(Look==EButton::Plus?.4f:.35f)*(bHot?1.6f:1.f)*(bDown?.6f:1.f);
        if(Look==EButton::Primary){Halo(Elements,Layer,Geometry,EShape::Rounded,Box,Radius,FLinearColor(0,0,0,.5f*Opacity),18.f,FVector2D(0,8));}
        Halo(Elements,Layer,Geometry,EShape::Rounded,Box,Radius,Accent.CopyWithNewOpacity(GlowAlpha*Opacity),GlowReach);
        const float Mid=Look==EButton::Plus?.5f:.45f;
        const FFill Fill=FFill::Linear(180.f,{{0.f,Hex(bHot?0xFFF6C4:0xFFF0A0)},{Mid,bDown?Hex(0xF2C928):Accent},{1.f,Hex(0xE9B91C)}});
        Shape(Elements,Layer+1,Geometry,EShape::Rounded,Box,Radius,Fill,Opacity);
        Line(Elements,Layer+1,Geometry,FVector2D(Box.Min.X+Radius,Box.Min.Y+1.f),FVector2D(Box.Max.X-Radius,Box.Min.Y+1.f),FLinearColor(1,1,1,.7f*Opacity),1.f);
        return;
    }
    case EButton::Event:
    {
        Halo(Elements,Layer,Geometry,EShape::Rounded,Box,5.f,LDHUDStyle::UIEvent.CopyWithNewOpacity(.45f*(bHot?1.5f:1.f)*Opacity),30.f);
        Shape(Elements,Layer+1,Geometry,EShape::Rounded,Box,5.f,FFill::Linear(180.f,{{0.f,Hex(0xFF7AA6)},{.5f,LDHUDStyle::UIEvent},{1.f,Hex(0xC61B57)}}),Opacity);
        Line(Elements,Layer+1,Geometry,FVector2D(Box.Min.X+5.f,Box.Min.Y+1.f),FVector2D(Box.Max.X-5.f,Box.Min.Y+1.f),FLinearColor(1,1,1,.5f*Opacity),1.f);
        return;
    }
    case EButton::Key:
    {
        Shape(Elements,Layer,Geometry,EShape::Rounded,Box,4.f,FFill::Solid(FLinearColor(0,0,0,.35f)),Opacity);
        Edge(Elements,Layer,Geometry,EShape::Rounded,Box,4.f,FLinearColor(1,1,1,(bHot?.6f:.3f)*Opacity),1.f);
        return;
    }
    case EButton::Tag:
    case EButton::TagOn:
    {
        if(Look==EButton::TagOn){Shape(Elements,Layer,Geometry,EShape::Rounded,Box,3.f,FFill::Solid(Accent),Opacity);return;}
        Shape(Elements,Layer,Geometry,EShape::Rounded,Box,3.f,FFill::Solid(FLinearColor(1,1,1,bHot?.11f:.055f)),Opacity);
        Edge(Elements,Layer,Geometry,EShape::Rounded,Box,3.f,bHot?Accent.CopyWithNewOpacity(.7f*Opacity):FLinearColor(1,1,1,.075f*Opacity),1.f);
        return;
    }
    case EButton::Card:
    {
        // .ocard: the deep vertical surface with its hairline, top highlight and long shadow; hovering lifts its edge, a
        // chosen card wears its colour on the edge with a glow.
        const bool bChosen=State.Tint.A>0.f;
        const FLinearColor Mark=bChosen?State.Tint:Accent;
        Halo(Elements,Layer,Geometry,EShape::Rounded,Box,8.f,FLinearColor(0,0,0,.55f*Opacity),30.f,FVector2D(0,14));
        if(bChosen||bHot){Halo(Elements,Layer,Geometry,EShape::Rounded,Box,8.f,Mark.CopyWithNewOpacity((bChosen?.34f:.18f)*Opacity),bChosen?28.f:18.f);}
        Shape(Elements,Layer+1,Geometry,EShape::Rounded,Box,8.f,FFill::Linear(180.f,{{0.f,Hex(bHot?0x222B41:0x1B2336)},{1.f,Hex(0x0D111B)}}),Opacity);
        Edge(Elements,Layer+1,Geometry,EShape::Rounded,Box,8.f,bChosen?Mark.CopyWithNewOpacity(Opacity):bHot?Accent.CopyWithNewOpacity(.6f*Opacity):FLinearColor(1,1,1,.14f*Opacity),bChosen?2.f:1.f);
        Line(Elements,Layer+1,Geometry,FVector2D(Box.Min.X+8.f,Box.Min.Y+1.5f),FVector2D(Box.Max.X-8.f,Box.Min.Y+1.5f),FLinearColor(1,1,1,.1f*Opacity),1.f);
        return;
    }
    case EButton::Ghost:
    {
        if(bHot){Shape(Elements,Layer,Geometry,EShape::Rounded,Box,5.f,FFill::Solid(FLinearColor(1,1,1,.06f)),Opacity);}
        return;
    }
    default:
    {
        // .btn: the dark raised face with its hairline and top highlight; hovering lifts the face and rims it yellow.
        Halo(Elements,Layer,Geometry,EShape::Rounded,Box,5.f,FLinearColor(0,0,0,.45f*Opacity),18.f,FVector2D(0,8));
        if(bHot){Halo(Elements,Layer,Geometry,EShape::Rounded,Box,5.f,Accent.CopyWithNewOpacity(.16f),16.f);}
        const FFill Fill=bDown?FFill::Linear(180.f,{{0.f,Hex(0x161C2A)},{1.f,Hex(0x1E2638)}})
            :FFill::Linear(180.f,{{0.f,Hex(bHot?0x2C3750:0x232C40)},{1.f,Hex(bHot?0x1B2232:0x161C2A)}});
        Shape(Elements,Layer+1,Geometry,EShape::Rounded,Box,5.f,Fill,Opacity);
        Edge(Elements,Layer+1,Geometry,EShape::Rounded,Box,5.f,bHot?Accent.CopyWithNewOpacity(.75f*Opacity):FLinearColor(1,1,1,.14f*Opacity),1.f);
        if(!bDown){Line(Elements,Layer+1,Geometry,FVector2D(Box.Min.X+5.f,Box.Min.Y+1.5f),FVector2D(Box.Max.X-5.f,Box.Min.Y+1.5f),FLinearColor(1,1,1,.1f*Opacity),1.f);}
        return;
    }
    }
}

void LDUIPaint::Pill(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,const FLinearColor& Border,const FLinearColor& Fill)
{
    const float Radius=float(Box.GetSize().Y)*.5f;
    Shape(Elements,Layer,Geometry,EShape::Rounded,Box,Radius,FFill::Solid(Fill));
    Edge(Elements,Layer,Geometry,EShape::Rounded,Box,Radius,Border,1.f);
}

float LDUIPaint::TileRadius(uint8 Kind,float Size){return Kind==2?Size*.5f:Kind==1?0.f:Size*.14f;}

void LDUIPaint::Tile(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,uint8 Kind,bool bScarce,ETileState State,UTexture2D* Icon,float Opacity,double Time)
{
    const float Size=float(FMath::Min(Box.GetSize().X,Box.GetSize().Y));
    if(Size<4.f||Opacity<=0.f){return;}
    const EShape Form=LDUIShapes::KindShape(Kind);
    const float Radius=TileRadius(Kind,Size);
    const FLinearColor KindColor=LDUIShapes::KindColor(Kind);
    if(State==ETileState::Hole)
    {
        Shape(Elements,Layer,Geometry,Form,Box,Radius,FFill::Solid(FLinearColor(1,1,1,.03f)),Opacity);
        DashedEdge(Elements,Layer,Geometry,Form,Box,Radius,FLinearColor(1,1,1,.22f*Opacity),2.f);
        Picture(Elements,Layer+1,Geometry,Form,Grow(Box,-3.f),FMath::Max(0.f,Radius-3.f),Icon,FLinearColor(.6f,.6f,.6f,.16f*Opacity));
        return;
    }
    const float Fade=State==ETileState::Dim?.28f*Opacity:Opacity;
    // filter: drop-shadow(0 4px 8px rgba(0,0,0,.55)), and the state or scarce glow.
    Halo(Elements,Layer,Geometry,Form,Box,Radius,FLinearColor(0,0,0,.5f*Fade),8.f,FVector2D(0,4));
    FLinearColor Glow=FLinearColor::Transparent;float Reach=0.f;
    switch(State)
    {
    case ETileState::Selected: Glow=LDHUDStyle::UIAction.CopyWithNewOpacity(.85f);Reach=14.f;break;
    case ETileState::Hovered: Glow=LDHUDStyle::UIAction.CopyWithNewOpacity(.45f);Reach=12.f;break;
    case ETileState::Legal: Glow=LDHUDStyle::UILegal.CopyWithNewOpacity(.7f);Reach=12.f;break;
    case ETileState::Replace: Glow=LDHUDStyle::UIReplace.CopyWithNewOpacity(.7f);Reach=10.f;break;
    case ETileState::Rejected: Glow=LDHUDStyle::UIReject.CopyWithNewOpacity(.8f);Reach=12.f;break;
    default: if(bScarce){Glow=Hex(0xFFD66A,.55f);Reach=10.f;}break;
    }
    if(Reach>0.f){Halo(Elements,Layer,Geometry,Form,Box,Radius,Glow.CopyWithNewOpacity(Glow.A*Fade),Reach);}
    // The frame: a solid state colour, the conic iridescence of a scarce component, or the kind's lit gradient.
    FFill Frame;
    if(State==ETileState::Selected||State==ETileState::Hovered){Frame=FFill::Solid(LDHUDStyle::UIAction);}
    else if(State==ETileState::Legal){Frame=FFill::Solid(LDHUDStyle::UILegal);}
    else if(State==ETileState::Replace){Frame=FFill::Solid(LDHUDStyle::UIReplace);}
    else if(State==ETileState::Rejected){Frame=FFill::Solid(LDHUDStyle::UIReject);}
    else if(bScarce)
    {
        Frame.bConic=true;Frame.Angle=210.f+float(FMath::Fmod(Time*40.,360.));
        Frame.Stops={{0.f,Hex(0xFFF3C4)},{.25f,Hex(0xFFC94D)},{.5f,Hex(0xFF8FD0)},{.75f,Hex(0x8FE9FF)},{1.f,Hex(0xFFF3C4)}};
    }
    else
    {
        const FLinearColor Dark=PaintLerp(KindColor,FLinearColor::Black,.55f);
        Frame=FFill::Linear(150.f,{{0.f,FLinearColor::White},{.3f,KindColor},{1.f,Dark}});
    }
    Shape(Elements,Layer+1,Geometry,Form,Box,Radius,Frame,Fade);
    const float Border=Size>=80.f?4.f:3.f;
    const FBox2D Face=Grow(Box,-Border);
    const float FaceSize=float(Face.GetSize().X);
    const float FaceRadius=Kind==2?FaceSize*.5f:Kind==1?0.f:FaceSize*.10f;
    Shape(Elements,Layer+2,Geometry,Form,Face,FaceRadius,FFill::Solid(Hex(0x0A1220)),Fade);
    const float IconAlpha=State==ETileState::Replace?.55f:1.f;
    const FLinearColor Tint=State==ETileState::Dim?FLinearColor(.55f,.55f,.6f,Fade):FLinearColor(1,1,1,Fade*IconAlpha);
    Picture(Elements,Layer+3,Geometry,Form,Face,FaceRadius,Icon,Tint);
}

void LDUIPaint::SkillArt(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,bool bSilver,bool bEmpty,UTexture2D* Icon,const FLinearColor& Glow)
{
    if(bEmpty)
    {
        Shape(Elements,Layer,Geometry,EShape::Rounded,Box,10.f,FFill::Solid(FLinearColor(1,1,1,.03f)));
        DashedEdge(Elements,Layer,Geometry,EShape::Rounded,Box,10.f,FLinearColor(1,1,1,.25f),2.f,7.f,5.f);
        return;
    }
    Halo(Elements,Layer,Geometry,EShape::Rounded,Box,10.f,FLinearColor(0,0,0,.6f),24.f,FVector2D(0,10));
    if(Glow.A>0.f){Halo(Elements,Layer,Geometry,EShape::Rounded,Box,10.f,Glow,18.f);}
    Halo(Elements,Layer,Geometry,EShape::Rounded,Box,10.f,FLinearColor(0,0,0,.6f),1.5f);
    const FFill Frame=bSilver?FFill::Linear(160.f,{{0.f,FLinearColor::White},{.3f,Hex(0x9AA6BD)},{1.f,Hex(0x39435A)}})
        :FFill::Linear(160.f,{{0.f,Hex(0xFFF7C9)},{.28f,LDHUDStyle::UIAction},{1.f,Hex(0x8A5A06)}});
    Shape(Elements,Layer+1,Geometry,EShape::Rounded,Box,10.f,Frame);
    const FBox2D Face=Grow(Box,-3.f);
    Shape(Elements,Layer+2,Geometry,EShape::Rounded,Face,7.f,FFill::Solid(Hex(0x0A1220)));
    Picture(Elements,Layer+3,Geometry,EShape::Rounded,Face,7.f,Icon);
}

void LDUIPaint::NewDot(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,FVector2D Center)
{
    const FBox2D Dot(Center-FVector2D(8,8),Center+FVector2D(8,8));
    Halo(Elements,Layer,Geometry,EShape::Circle,Dot,8.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.9f),10.f);
    Shape(Elements,Layer+1,Geometry,EShape::Circle,Dot,8.f,FFill::Solid(Hex(0x10141D)));
    Shape(Elements,Layer+2,Geometry,EShape::Circle,Grow(Dot,-2.f),6.f,FFill::Solid(LDHUDStyle::UIAction));
}

void LDUIPaint::ElementDisc(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,const FBox2D& Box,const FLinearColor& Color,float Opacity)
{
    const float Radius=float(Box.GetSize().X)*.5f;
    Shape(Elements,Layer,Geometry,EShape::Circle,Box,Radius,FFill::Solid(Hex(0x10141D)),Opacity);
    Shape(Elements,Layer+1,Geometry,EShape::Circle,Grow(Box,-2.f),Radius-2.f,FFill::Solid(Color),Opacity);
}
