#include "UI/LDBuildRingPaint.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
FColor RingSrgb(const FLinearColor& Color){return Color.ToFColor(true);}
FSlateResourceHandle RingHandle(UTexture2D* Texture)
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
struct FRingMesh
{
    TArray<FSlateVertex> Vertices;
    TArray<SlateIndex> Indices;
    const FSlateRenderTransform* Transform=nullptr;
    const FTransform2D* Local=nullptr;
    int32 Add(FVector2D At,const FLinearColor& Color,FVector2D UV=FVector2D(.5,.5))
    {
        const FVector2D P=Local?Local->TransformPoint(At):At;
        Vertices.Add(FSlateVertex::Make(*Transform,FVector2f(P),FVector2f(UV),RingSrgb(Color)));
        return Vertices.Num()-1;
    }
    void Tri(int32 A,int32 B,int32 C){Indices.Add(SlateIndex(A));Indices.Add(SlateIndex(B));Indices.Add(SlateIndex(C));}
    void Quad(int32 A,int32 B,int32 C,int32 D){Tri(A,B,C);Tri(A,C,D);}
    void Draw(FSlateWindowElementList& Elements,int32 Layer,UTexture2D* Texture=nullptr)
    {
        if(Indices.IsEmpty()){return;}
        FSlateDrawElement::MakeCustomVerts(Elements,Layer,RingHandle(Texture),Vertices,Indices,nullptr,0,0);
    }
};
FVector2D RingCentre(const TArray<FVector2D>& Points)
{
    FVector2D Sum=FVector2D::ZeroVector;
    for(const FVector2D& Point:Points){Sum+=Point;}
    return Points.IsEmpty()?Sum:Sum/double(Points.Num());
}
float RingPixel(const FGeometry& Geometry){return 1.f/FMath::Max(.1f,Geometry.Scale);}
// A deterministic value noise in -1..1.
float RingNoise(uint32 Seed,float X)
{
    const auto Hash=[Seed](int32 I){uint32 H=uint32(I)*2654435761u^Seed*2246822519u;H^=H>>15;H*=2246822519u;H^=H>>13;return float(H&0xFFFF)/32767.5f-1.f;};
    const int32 I=FMath::FloorToInt(X);const float F=X-I;const float S=F*F*(3.f-2.f*F);
    return FMath::Lerp(Hash(I),Hash(I+1),S);
}
}

FLinearColor LDRingPaint::Mix(const FLinearColor& A,const FLinearColor& B,float T)
{
    const FColor X=RingSrgb(A),Y=RingSrgb(B);
    const auto M=[T](uint8 P,uint8 Q){return uint8(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(float(P),float(Q),T)),0,255));};
    FLinearColor Out=FLinearColor::FromSRGBColor(FColor(M(X.R,Y.R),M(X.G,Y.G),M(X.B,Y.B),255));
    Out.A=FMath::Lerp(A.A,B.A,T);
    return Out;
}
FVector2D LDRingPaint::Polar(FVector2D Centre,float Radius,float Degrees)
{
    const float T=FMath::DegreesToRadians(Degrees);
    return FVector2D(Centre.X+Radius*FMath::Sin(T),Centre.Y-Radius*FMath::Cos(T));
}
TArray<FVector2D> LDRingPaint::Arc(FVector2D Centre,float Radius,float FromDegrees,float ToDegrees,int32 Segments)
{
    const int32 N=Segments>0?Segments:FMath::Clamp(FMath::CeilToInt(FMath::Abs(ToDegrees-FromDegrees)/2.f),2,180);
    TArray<FVector2D> Out;Out.Reserve(N+1);
    for(int32 Index=0;Index<=N;++Index){Out.Add(Polar(Centre,Radius,FMath::Lerp(FromDegrees,ToDegrees,float(Index)/N)));}
    return Out;
}
TArray<FVector2D> LDRingPaint::Rounded(const FBox2D& Box,float Radius,int32 Segments)
{
    TArray<FVector2D> Out;
    const float R=FMath::Clamp(Radius,0.f,float(FMath::Min(Box.GetSize().X,Box.GetSize().Y))*.5f);
    const FVector2D Centres[4]={FVector2D(Box.Max.X-R,Box.Min.Y+R),FVector2D(Box.Max.X-R,Box.Max.Y-R),FVector2D(Box.Min.X+R,Box.Max.Y-R),FVector2D(Box.Min.X+R,Box.Min.Y+R)};
    for(int32 Corner=0;Corner<4;++Corner)
    {
        for(int32 Step=0;Step<=Segments;++Step)
        {
            const float A=FMath::DegreesToRadians(-90.f+Corner*90.f+90.f*Step/Segments);
            Out.Add(Centres[Corner]+FVector2D(FMath::Cos(A),FMath::Sin(A))*R);
        }
    }
    return Out;
}
TArray<FVector2D> LDRingPaint::Chamfer(const FBox2D& Box,float Cut)
{
    const FVector2D A=Box.Min,B=Box.Max;
    return {FVector2D(A.X+Cut,A.Y),FVector2D(B.X-Cut,A.Y),FVector2D(B.X,A.Y+Cut),FVector2D(B.X,B.Y-Cut),FVector2D(B.X-Cut,B.Y),
        FVector2D(A.X+Cut,B.Y),FVector2D(A.X,B.Y-Cut),FVector2D(A.X,A.Y+Cut)};
}
TArray<FVector2D> LDRingPaint::HostCut(const FBox2D& Box,float Cut)
{
    const FVector2D A=Box.Min,B=Box.Max;
    return {FVector2D(A.X+Cut,A.Y),FVector2D(B.X,A.Y),FVector2D(B.X,B.Y-Cut),FVector2D(B.X-Cut,B.Y),FVector2D(A.X,B.Y),FVector2D(A.X,A.Y+Cut)};
}
TArray<FVector2D> LDRingPaint::Octagon(const FBox2D& Box,float CutFraction)
{
    return Chamfer(Box,float(FMath::Min(Box.GetSize().X,Box.GetSize().Y))*CutFraction);
}
TArray<FVector2D> LDRingPaint::Slant(const FBox2D& Box,float Shift)
{
    return {FVector2D(Box.Min.X+Shift,Box.Min.Y),FVector2D(Box.Max.X,Box.Min.Y),FVector2D(Box.Max.X-Shift,Box.Max.Y),FVector2D(Box.Min.X,Box.Max.Y)};
}
TArray<FVector2D> LDRingPaint::ClipConvex(const TArray<FVector2D>& Subject,const TArray<FVector2D>& Clip)
{
    TArray<FVector2D> Out=Subject;
    if(Clip.Num()<3){return Out;}
    // The clip's winding decides which side is inside.
    float Area=0.f;for(int32 Index=0;Index<Clip.Num();++Index){Area+=float(FVector2D::CrossProduct(Clip[Index],Clip[(Index+1)%Clip.Num()]));}
    const float Side=Area>=0.f?1.f:-1.f;
    for(int32 Edge=0;Edge<Clip.Num()&&!Out.IsEmpty();++Edge)
    {
        const FVector2D A=Clip[Edge],B=Clip[(Edge+1)%Clip.Num()];
        const auto In=[&](FVector2D P){return float(FVector2D::CrossProduct(B-A,P-A))*Side>=0.f;};
        const auto Cross=[&](FVector2D P,FVector2D Q)
        {
            const float D1=float(FVector2D::CrossProduct(B-A,P-A)),D2=float(FVector2D::CrossProduct(B-A,Q-A));
            return P+(Q-P)*(D1/(D1-D2));
        };
        TArray<FVector2D> Input=MoveTemp(Out);Out.Reset();
        for(int32 Index=0;Index<Input.Num();++Index)
        {
            const FVector2D P=Input[Index],Q=Input[(Index+1)%Input.Num()];
            const bool bP=In(P),bQ=In(Q);
            if(bP){Out.Add(P);}
            if(bP!=bQ){Out.Add(Cross(P,Q));}
        }
    }
    return Out;
}
bool LDRingPaint::Inside(const TArray<FVector2D>& Convex,FVector2D Point)
{
    if(Convex.Num()<3){return false;}
    float Sign=0.f;
    for(int32 Index=0;Index<Convex.Num();++Index)
    {
        const FVector2D A=Convex[Index],B=Convex[(Index+1)%Convex.Num()];
        const float Cross=float(FVector2D::CrossProduct(B-A,Point-A));
        if(FMath::Abs(Cross)<1e-4f){continue;}
        if(Sign==0.f){Sign=FMath::Sign(Cross);}
        else if(FMath::Sign(Cross)!=Sign){return false;}
    }
    return true;
}
TArray<FVector2D> LDRingPaint::Jitter(const TArray<FVector2D>& Path,float Amplitude,uint32 Seed,float Step)
{
    TArray<FVector2D> Out;
    float Travel=0.f;
    for(int32 Index=0;Index+1<Path.Num();++Index)
    {
        const FVector2D A=Path[Index],B=Path[Index+1];
        const float Length=float(FVector2D::Distance(A,B));
        const int32 Pieces=FMath::Max(1,FMath::CeilToInt(Length/FMath::Max(1.f,Step)));
        const FVector2D Dir=(B-A).GetSafeNormal(),Normal(-Dir.Y,Dir.X);
        for(int32 Piece=0;Piece<Pieces;++Piece)
        {
            const float T=float(Piece)/Pieces;
            const float At=Travel+Length*T;
            Out.Add(FMath::Lerp(A,B,T)+Normal*(Amplitude*(RingNoise(Seed,At/(Step*2.2f))*.75f+RingNoise(Seed+7,At/(Step*.7f))*.25f)));
        }
        Travel+=Length;
    }
    if(!Path.IsEmpty()){Out.Add(Path.Last());}
    return Out;
}
FVector2D LDRingPaint::Measure(const FString& Value,const FSlateFontInfo& Font)
{
    if(Value.IsEmpty()||!FSlateApplication::IsInitialized()){return FVector2D::ZeroVector;}
    const TSharedRef<FSlateFontMeasure> Service=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    return FVector2D(Service->Measure(Value,Font,1.f));
}
TArray<FString> LDRingPaint::Wrap(const FString& Value,const FSlateFontInfo& Font,float Width,int32 MaxLines)
{
    TArray<FString> Lines;
    FString Line;
    const auto IsWide=[](TCHAR C){return C>=0x2E80;};
    int32 Index=0;
    while(Index<Value.Len())
    {
        // A token: one wide character, or a run of narrow ones up to a space.
        FString Token;
        if(IsWide(Value[Index])){Token.AppendChar(Value[Index++]);}
        else{while(Index<Value.Len()&&!IsWide(Value[Index])){const TCHAR C=Value[Index++];Token.AppendChar(C);if(C==TEXT(' ')){break;}}}
        if(Token==TEXT("\n")){Lines.Add(Line);Line.Reset();continue;}
        const FString Next=Line+Token;
        if(!Line.IsEmpty()&&Measure(Next,Font).X>Width)
        {
            // Closing punctuation never starts a line.
            const bool bClosing=Token.Len()==1&&FString(TEXT("，。、；：！？）」』%")).Contains(Token);
            if(bClosing){Line=Next;continue;}
            Lines.Add(Line.TrimEnd());Line=Token.TrimStart();
        }
        else{Line=Next;}
    }
    if(!Line.IsEmpty()){Lines.Add(Line);}
    if(MaxLines>0&&Lines.Num()>MaxLines)
    {
        Lines.SetNum(MaxLines);
        FString& Last=Lines.Last();
        while(!Last.IsEmpty()&&Measure(Last+TEXT("…"),Font).X>Width){Last.LeftChopInline(1);}
        Last+=TEXT("…");
    }
    return Lines;
}

void LDRingPaint::FPainter::FillShaded(const TArray<FVector2D>& Points,TFunctionRef<FLinearColor(FVector2D)> Color)
{
    if(Points.Num()<3||Opacity<=0.f){return;}
    FRingMesh Mesh;Mesh.Transform=&Geometry.GetAccumulatedRenderTransform();Mesh.Local=bXform?&Xform:nullptr;
    const FVector2D Centre=RingCentre(Points);
    const int32 Hub=Mesh.Add(Centre,Fade(Color(Centre)));
    const float Pixel=RingPixel(Geometry);
    TArray<int32> Inner,Outer;
    const int32 N=Points.Num();
    for(int32 Index=0;Index<N;++Index)
    {
        const FVector2D Out=(Points[Index]-Centre).GetSafeNormal();
        const FLinearColor C=Fade(Color(Points[Index]));
        Inner.Add(Mesh.Add(Points[Index]-Out*(Pixel*.5f),C));
        Outer.Add(Mesh.Add(Points[Index]+Out*(Pixel*.5f),C.CopyWithNewOpacity(0.f)));
    }
    for(int32 Index=0;Index<N;++Index)
    {
        const int32 Next=(Index+1)%N;
        Mesh.Tri(Hub,Inner[Index],Inner[Next]);
        Mesh.Quad(Inner[Index],Outer[Index],Outer[Next],Inner[Next]);
    }
    Mesh.Draw(Elements,Next());
}
void LDRingPaint::FPainter::Fill(const TArray<FVector2D>& Points,const FLinearColor& Color)
{
    FillShaded(Points,[&Color](FVector2D){return Color;});
}
void LDRingPaint::FPainter::Band(const TArray<FVector2D>& Inner,const TArray<FVector2D>& Outer,TFunctionRef<FLinearColor(FVector2D)> Color)
{
    const int32 N=FMath::Min(Inner.Num(),Outer.Num());
    if(N<2||Opacity<=0.f){return;}
    FRingMesh Mesh;Mesh.Transform=&Geometry.GetAccumulatedRenderTransform();Mesh.Local=bXform?&Xform:nullptr;
    TArray<int32> A,B;
    for(int32 Index=0;Index<N;++Index){A.Add(Mesh.Add(Inner[Index],Fade(Color(Inner[Index]))));B.Add(Mesh.Add(Outer[Index],Fade(Color(Outer[Index]))));}
    for(int32 Index=0;Index+1<N;++Index){Mesh.Quad(A[Index],B[Index],B[Index+1],A[Index+1]);}
    Mesh.Draw(Elements,Next());
}
void LDRingPaint::FPainter::Stroke(const TArray<FVector2D>& Path,float Width,const FLinearColor& Color,bool bClosed,const FLinearColor& EndColor)
{
    const int32 N=Path.Num();
    if(N<2||Opacity<=0.f||Width<=0.f){return;}
    FRingMesh Mesh;Mesh.Transform=&Geometry.GetAccumulatedRenderTransform();Mesh.Local=bXform?&Xform:nullptr;
    const float Pixel=RingPixel(Geometry);
    const float Half=FMath::Max(Width,Pixel)*.5f;
    const int32 Count=bClosed?N+1:N;
    // Total length for the fade.
    float Total=0.f;for(int32 Index=1;Index<Count;++Index){Total+=float(FVector2D::Distance(Path[(Index-1)%N],Path[Index%N]));}
    float Travel=0.f;
    TArray<int32> L0,L1,R0,R1;
    for(int32 Index=0;Index<Count;++Index)
    {
        const FVector2D P=Path[Index%N];
        if(Index>0){Travel+=float(FVector2D::Distance(Path[(Index-1)%N],P));}
        const FVector2D Prev=Index>0?Path[(Index-1)%N]:(bClosed?Path[N-1]:P);
        const FVector2D NextP=Index+1<Count?Path[(Index+1)%N]:(bClosed?Path[1%N]:P);
        FVector2D Dir=(NextP-Prev).GetSafeNormal();
        if(Dir.IsNearlyZero()){Dir=FVector2D(1,0);}
        const FVector2D Normal(-Dir.Y,Dir.X);
        const FLinearColor C=Fade(EndColor.A>=0.f&&Total>0.f?Mix(Color,EndColor,Travel/Total):Color);
        L1.Add(Mesh.Add(P+Normal*(Half+Pixel*.5f),C.CopyWithNewOpacity(0.f)));
        L0.Add(Mesh.Add(P+Normal*FMath::Max(0.f,Half-Pixel*.5f),C));
        R0.Add(Mesh.Add(P-Normal*FMath::Max(0.f,Half-Pixel*.5f),C));
        R1.Add(Mesh.Add(P-Normal*(Half+Pixel*.5f),C.CopyWithNewOpacity(0.f)));
    }
    for(int32 Index=0;Index+1<Count;++Index)
    {
        Mesh.Quad(L1[Index],L0[Index],L0[Index+1],L1[Index+1]);
        Mesh.Quad(L0[Index],R0[Index],R0[Index+1],L0[Index+1]);
        Mesh.Quad(R0[Index],R1[Index],R1[Index+1],R0[Index+1]);
    }
    Mesh.Draw(Elements,Next());
}
void LDRingPaint::FPainter::Dashed(const TArray<FVector2D>& Path,float Width,float Dash,float Gap,const FLinearColor& Color,bool bClosed)
{
    const int32 N=Path.Num();
    if(N<2||Dash<=0.f){return;}
    FRingMesh Mesh;Mesh.Transform=&Geometry.GetAccumulatedRenderTransform();Mesh.Local=bXform?&Xform:nullptr;
    const FLinearColor C=Fade(Color);
    const float Half=FMath::Max(Width,RingPixel(Geometry))*.5f;
    float Phase=0.f;bool bOn=true;
    const int32 Count=bClosed?N:N-1;
    for(int32 Index=0;Index<Count;++Index)
    {
        const FVector2D A=Path[Index],B=Path[(Index+1)%N];
        const float Length=float(FVector2D::Distance(A,B));
        if(Length<=0.f){continue;}
        const FVector2D Dir=(B-A)/Length,Normal(-Dir.Y,Dir.X);
        float At=0.f;
        while(At<Length)
        {
            const float Span=(bOn?Dash:Gap)-Phase;
            const float End=FMath::Min(Length,At+Span);
            if(bOn)
            {
                const FVector2D P=A+Dir*At,Q=A+Dir*End;
                const int32 V0=Mesh.Add(P+Normal*Half,C),V1=Mesh.Add(Q+Normal*Half,C),V2=Mesh.Add(Q-Normal*Half,C),V3=Mesh.Add(P-Normal*Half,C);
                Mesh.Quad(V0,V1,V2,V3);
            }
            Phase+=End-At;At=End;
            if(Phase>=(bOn?Dash:Gap)-1e-3f){Phase=0.f;bOn=!bOn;}
        }
    }
    Mesh.Draw(Elements,Next());
}
void LDRingPaint::FPainter::Image(UTexture2D* Texture,const FBox2D& Box,const FLinearColor& Tint,const TArray<FVector2D>* Clip,float Degrees)
{
    if(!Texture||Opacity<=0.f){return;}
    FRingMesh Mesh;Mesh.Transform=&Geometry.GetAccumulatedRenderTransform();Mesh.Local=bXform?&Xform:nullptr;
    const FVector2D Centre=Box.GetCenter(),Size=Box.GetSize();
    // Cover: the texture's shorter side spans the box.
    const float Aspect=float(Texture->GetSizeX())/FMath::Max(1.f,float(Texture->GetSizeY()));
    const float BoxAspect=float(Size.X/FMath::Max(1e-3,Size.Y));
    FVector2D Span(1.,1.);
    if(Aspect>BoxAspect){Span.X=BoxAspect/Aspect;}else{Span.Y=Aspect/BoxAspect;}
    const float R=FMath::DegreesToRadians(Degrees),Cs=FMath::Cos(R),Sn=FMath::Sin(R);
    const auto UV=[&](FVector2D P)
    {
        const FVector2D D=P-Centre;
        const FVector2D L(D.X*Cs+D.Y*Sn,-D.X*Sn+D.Y*Cs);
        return FVector2D(.5+L.X/Size.X*Span.X,.5+L.Y/Size.Y*Span.Y);
    };
    const FLinearColor C=Fade(Tint);
    if(Clip&&Clip->Num()>=3)
    {
        const FVector2D Hub=RingCentre(*Clip);
        const int32 H=Mesh.Add(Hub,C,UV(Hub));
        TArray<int32> Ring;for(const FVector2D& P:*Clip){Ring.Add(Mesh.Add(P,C,UV(P)));}
        for(int32 Index=0;Index<Ring.Num();++Index){Mesh.Tri(H,Ring[Index],Ring[(Index+1)%Ring.Num()]);}
    }
    else
    {
        const auto Corner=[&](float X,float Y){const FVector2D L(X*Size.X*.5,Y*Size.Y*.5);return Centre+FVector2D(L.X*Cs-L.Y*Sn,L.X*Sn+L.Y*Cs);};
        const FVector2D P[4]={Corner(-1,-1),Corner(1,-1),Corner(1,1),Corner(-1,1)};
        const FVector2D T[4]={FVector2D(.5-Span.X*.5,.5-Span.Y*.5),FVector2D(.5+Span.X*.5,.5-Span.Y*.5),FVector2D(.5+Span.X*.5,.5+Span.Y*.5),FVector2D(.5-Span.X*.5,.5+Span.Y*.5)};
        Mesh.Quad(Mesh.Add(P[0],C,T[0]),Mesh.Add(P[1],C,T[1]),Mesh.Add(P[2],C,T[2]),Mesh.Add(P[3],C,T[3]));
    }
    Mesh.Draw(Elements,Next(),Texture);
}
void LDRingPaint::FPainter::Dots(const TArray<FVector2D>& Centres,float Size,TFunctionRef<FLinearColor(FVector2D)> Color)
{
    if(Centres.IsEmpty()||Opacity<=0.f){return;}
    FRingMesh Mesh;Mesh.Transform=&Geometry.GetAccumulatedRenderTransform();Mesh.Local=bXform?&Xform:nullptr;
    const float H=Size*.5f;
    for(const FVector2D& P:Centres)
    {
        const FLinearColor C=Fade(Color(P));
        if(C.A<=.003f){continue;}
        // A small octagon reads as a dot at this size.
        const float D=H*.42f;
        const int32 Hub=Mesh.Add(P,C);
        const FVector2D Ring[8]={P+FVector2D(-D,-H),P+FVector2D(D,-H),P+FVector2D(H,-D),P+FVector2D(H,D),P+FVector2D(D,H),P+FVector2D(-D,H),P+FVector2D(-H,D),P+FVector2D(-H,-D)};
        int32 First=INDEX_NONE,Prev=INDEX_NONE;
        for(int32 Index=0;Index<8;++Index)
        {
            const int32 V=Mesh.Add(Ring[Index],C);
            if(Prev!=INDEX_NONE){Mesh.Tri(Hub,Prev,V);}else{First=V;}
            Prev=V;
        }
        Mesh.Tri(Hub,Prev,First);
    }
    Mesh.Draw(Elements,Next());
}
FVector2D LDRingPaint::FPainter::Text(const FString& Value,FVector2D At,const FSlateFontInfo& Font,const FLinearColor& Color,FVector2D Align,float SkewDegrees)
{
    if(Value.IsEmpty()||Opacity<=0.f){return FVector2D::ZeroVector;}
    const FVector2D Size=Measure(Value,Font);
    const FVector2D TopLeft=At-Size*Align;
    if(bXform)
    {
        const FVector2D Origin=Xform.TransformPoint(TopLeft);
        double M00=1.,M01=0.,M10=0.,M11=1.;Xform.GetMatrix().GetMatrix(M00,M01,M10,M11);
        const FSlateRenderTransform Turn{FMatrix2x2f{float(M00),float(M01),float(M10),float(M11)}};
        const FGeometry Child=Geometry.MakeChild(Size,FSlateLayoutTransform(Origin),Turn,FVector2D::ZeroVector);
        FSlateFontInfo Slanted=Font;if(!FMath::IsNearlyZero(SkewDegrees)){Slanted.SkewAmount=FMath::Tan(FMath::DegreesToRadians(SkewDegrees));}
        FSlateDrawElement::MakeText(Elements,Next(),Child.ToPaintGeometry(),Value,Slanted,ESlateDrawEffect::None,Fade(Color));
    }
    else if(FMath::IsNearlyZero(SkewDegrees))
    {
        FSlateDrawElement::MakeText(Elements,Next(),Geometry.ToPaintGeometry(FVector2f(Size),FSlateLayoutTransform(FVector2f(TopLeft))),Value,Font,
            ESlateDrawEffect::None,Fade(Color));
    }
    else
    {
        FSlateFontInfo Slanted=Font;Slanted.SkewAmount=FMath::Tan(FMath::DegreesToRadians(SkewDegrees));
        FSlateDrawElement::MakeText(Elements,Next(),Geometry.ToPaintGeometry(FVector2f(Size),FSlateLayoutTransform(FVector2f(TopLeft))),Value,Slanted,
            ESlateDrawEffect::None,Fade(Color));
    }
    return Size;
}
