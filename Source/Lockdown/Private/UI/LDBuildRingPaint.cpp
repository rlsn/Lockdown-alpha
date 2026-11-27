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
// A textured primitive's vertices and indices: one scratch pair that keeps its capacity (primitives are built and drawn
// one after another on the game thread; MakeCustomVerts copies what it is given).
TArray<FSlateVertex>& RingScratchVertices(){static TArray<FSlateVertex> Scratch;return Scratch;}
TArray<SlateIndex>& RingScratchIndices(){static TArray<SlateIndex> Scratch;return Scratch;}
// The largest waiting mesh sent so far, so a painter's first primitive reserves once.
int32 RingWaitingVertices=0,RingWaitingIndices=0;
using FRingIds=TArray<int32,TInlineAllocator<128>>;
struct FRingMesh
{
    LDRingPaint::FPainter& Painter;
    UTexture2D* Texture=nullptr;
    TArray<FSlateVertex>& Vertices;
    TArray<SlateIndex>& Indices;
    const FSlateRenderTransform* Transform=nullptr;
    // Untextured: appended to the painter's waiting mesh. Textured: sent on its own, after what waits.
    explicit FRingMesh(LDRingPaint::FPainter& InPainter,UTexture2D* InTexture=nullptr):Painter(InPainter),Texture(InTexture)
        ,Vertices(InTexture?RingScratchVertices():InPainter.Waiting),Indices(InTexture?RingScratchIndices():InPainter.WaitingIndices)
        ,Transform(&InPainter.Geometry.GetAccumulatedRenderTransform())
    {
        if(Texture){Painter.Flush();Vertices.Reset();Indices.Reset();}
        else if(Vertices.Max()==0){Vertices.Reserve(RingWaitingVertices);Indices.Reserve(RingWaitingIndices);}
    }
    int32 Add(FVector2D At,const FLinearColor& Color,FVector2D UV=FVector2D(.5,.5))
    {
        Vertices.Add(FSlateVertex::Make(*Transform,FVector2f(At),FVector2f(UV),RingSrgb(Color)));
        return Vertices.Num()-1;
    }
    void Tri(int32 A,int32 B,int32 C){Indices.Add(SlateIndex(A));Indices.Add(SlateIndex(B));Indices.Add(SlateIndex(C));}
    void Quad(int32 A,int32 B,int32 C,int32 D){Tri(A,B,C);Tri(A,C,D);}
    // The primitive takes the next layer either way.
    void Draw()
    {
        const int32 Layer=Painter.Next();
        if(!Texture){if(Painter.WaitingLayer==INDEX_NONE){Painter.WaitingLayer=Layer;}return;}
        if(Indices.IsEmpty()){return;}
        FSlateDrawElement::MakeCustomVerts(Painter.Elements,Layer,RingHandle(Texture),Vertices,Indices,nullptr,0,0);
    }
};
FVector2D RingCentre(const TArray<FVector2D>& Points)
{
    FVector2D Sum=FVector2D::ZeroVector;
    for(const FVector2D& Point:Points){Sum+=Point;}
    return Points.IsEmpty()?Sum:Sum/double(Points.Num());
}
float RingPixel(const FGeometry& Geometry){return 1.f/FMath::Max(.1f,Geometry.Scale);}
// What a wrapped text depends on: the same text, face, width and line limit break the same way.
struct FRingWrapKey
{
    FString Value;
    FSlateFontInfo Font;
    float Width=0.f;
    int32 MaxLines=0;
    bool operator==(const FRingWrapKey& Other) const{return Width==Other.Width&&MaxLines==Other.MaxLines&&Value.Equals(Other.Value,ESearchCase::CaseSensitive)&&Font==Other.Font;}
    friend uint32 GetTypeHash(const FRingWrapKey& Key){return HashCombine(HashCombine(GetTypeHash(Key.Value),GetTypeHash(Key.Font)),HashCombine(GetTypeHash(Key.Width),GetTypeHash(Key.MaxLines)));}
};
constexpr int32 RingWrapMemory=512;
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
FVector2D LDRingPaint::Measure(const FString& Value,const FSlateFontInfo& Font)
{
    if(Value.IsEmpty()||!FSlateApplication::IsInitialized()){return FVector2D::ZeroVector;}
    const TSharedRef<FSlateFontMeasure> Service=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    return FVector2D(Service->Measure(Value,Font,1.f));
}
TArray<FString> LDRingPaint::Wrap(const FString& Value,const FSlateFontInfo& Font,float Width,int32 MaxLines)
{
    // Breaking measures the line once per token; the same text is asked for again every painted frame, so the last
    // results are kept (all dropped when the memory fills).
    static TMap<FRingWrapKey,TArray<FString>> Memory;
    FRingWrapKey Key{Value,Font,Width,MaxLines};
    if(const TArray<FString>* Known=Memory.Find(Key)){return *Known;}
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
    if(Memory.Num()>=RingWrapMemory){Memory.Reset();}
    Memory.Add(MoveTemp(Key),Lines);
    return Lines;
}

void LDRingPaint::FPainter::Flush()
{
    if(WaitingLayer!=INDEX_NONE&&!WaitingIndices.IsEmpty())
    {
        FSlateDrawElement::MakeCustomVerts(Elements,WaitingLayer,RingHandle(nullptr),Waiting,WaitingIndices,nullptr,0,0);
        RingWaitingVertices=FMath::Max(RingWaitingVertices,Waiting.Num());RingWaitingIndices=FMath::Max(RingWaitingIndices,WaitingIndices.Num());
    }
    Waiting.Reset();WaitingIndices.Reset();WaitingLayer=INDEX_NONE;
}
void LDRingPaint::FPainter::FillShaded(const TArray<FVector2D>& Points,TFunctionRef<FLinearColor(FVector2D)> Color)
{
    if(Points.Num()<3||Opacity<=0.f){return;}
    FRingMesh Mesh(*this);
    const FVector2D Centre=RingCentre(Points);
    const int32 Hub=Mesh.Add(Centre,Fade(Color(Centre)));
    const float Pixel=RingPixel(Geometry);
    FRingIds Inner,Outer;
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
    Mesh.Draw();
}
void LDRingPaint::FPainter::Fill(const TArray<FVector2D>& Points,const FLinearColor& Color)
{
    FillShaded(Points,[&Color](FVector2D){return Color;});
}
void LDRingPaint::FPainter::Band(const TArray<FVector2D>& Inner,const TArray<FVector2D>& Outer,TFunctionRef<FLinearColor(FVector2D)> Color)
{
    const int32 N=FMath::Min(Inner.Num(),Outer.Num());
    if(N<2||Opacity<=0.f){return;}
    FRingMesh Mesh(*this);
    FRingIds A,B;
    for(int32 Index=0;Index<N;++Index){A.Add(Mesh.Add(Inner[Index],Fade(Color(Inner[Index]))));B.Add(Mesh.Add(Outer[Index],Fade(Color(Outer[Index]))));}
    for(int32 Index=0;Index+1<N;++Index){Mesh.Quad(A[Index],B[Index],B[Index+1],A[Index+1]);}
    Mesh.Draw();
}
void LDRingPaint::FPainter::Stroke(const TArray<FVector2D>& Path,float Width,const FLinearColor& Color,bool bClosed,const FLinearColor& EndColor)
{
    const int32 N=Path.Num();
    if(N<2||Opacity<=0.f||Width<=0.f){return;}
    FRingMesh Mesh(*this);
    const float Pixel=RingPixel(Geometry);
    const float Half=FMath::Max(Width,Pixel)*.5f;
    const int32 Count=bClosed?N+1:N;
    // Total length for the fade.
    float Total=0.f;for(int32 Index=1;Index<Count;++Index){Total+=float(FVector2D::Distance(Path[(Index-1)%N],Path[Index%N]));}
    float Travel=0.f;
    FRingIds L0,L1,R0,R1;
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
    Mesh.Draw();
}
void LDRingPaint::FPainter::Dashed(const TArray<FVector2D>& Path,float Width,float Dash,float Gap,const FLinearColor& Color,bool bClosed)
{
    const int32 N=Path.Num();
    if(N<2||Dash<=0.f){return;}
    FRingMesh Mesh(*this);
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
    Mesh.Draw();
}
void LDRingPaint::FPainter::Image(UTexture2D* Texture,const FBox2D& Box,const FLinearColor& Tint,const TArray<FVector2D>* Clip)
{
    if(!Texture||Opacity<=0.f){return;}
    FRingMesh Mesh(*this,Texture);
    const FVector2D Centre=Box.GetCenter(),Size=Box.GetSize();
    // Cover: the texture's shorter side spans the box.
    const float Aspect=float(Texture->GetSizeX())/FMath::Max(1.f,float(Texture->GetSizeY()));
    const float BoxAspect=float(Size.X/FMath::Max(1e-3,Size.Y));
    FVector2D Span(1.,1.);
    if(Aspect>BoxAspect){Span.X=BoxAspect/Aspect;}else{Span.Y=Aspect/BoxAspect;}
    const auto UV=[&](FVector2D P){const FVector2D D=P-Centre;return FVector2D(.5+D.X/Size.X*Span.X,.5+D.Y/Size.Y*Span.Y);};
    const FLinearColor C=Fade(Tint);
    if(Clip&&Clip->Num()>=3)
    {
        const FVector2D Hub=RingCentre(*Clip);
        const int32 H=Mesh.Add(Hub,C,UV(Hub));
        FRingIds Ring;for(const FVector2D& P:*Clip){Ring.Add(Mesh.Add(P,C,UV(P)));}
        for(int32 Index=0;Index<Ring.Num();++Index){Mesh.Tri(H,Ring[Index],Ring[(Index+1)%Ring.Num()]);}
    }
    else
    {
        const FVector2D P[4]={Box.Min,FVector2D(Box.Max.X,Box.Min.Y),Box.Max,FVector2D(Box.Min.X,Box.Max.Y)};
        Mesh.Quad(Mesh.Add(P[0],C,UV(P[0])),Mesh.Add(P[1],C,UV(P[1])),Mesh.Add(P[2],C,UV(P[2])),Mesh.Add(P[3],C,UV(P[3])));
    }
    Mesh.Draw();
}
void LDRingPaint::FPainter::Segments(const TArray<FVector2D>& Ends,float Width,TFunctionRef<FLinearColor(int32)> Color)
{
    if(Ends.Num()<2||Opacity<=0.f||Width<=0.f){return;}
    FRingMesh Mesh(*this);
    const float Half=FMath::Max(Width,RingPixel(Geometry))*.5f;
    for(int32 Index=0;Index+1<Ends.Num();Index+=2)
    {
        const FVector2D A=Ends[Index],B=Ends[Index+1];
        const FVector2D Dir=(B-A).GetSafeNormal();
        if(Dir.IsNearlyZero()){continue;}
        const FVector2D Normal(-Dir.Y,Dir.X);
        const FLinearColor C=Fade(Color(Index/2));
        Mesh.Quad(Mesh.Add(A+Normal*Half,C),Mesh.Add(B+Normal*Half,C),Mesh.Add(B-Normal*Half,C),Mesh.Add(A-Normal*Half,C));
    }
    Mesh.Draw();
}
FVector2D LDRingPaint::FPainter::Text(const FString& Value,FVector2D At,const FSlateFontInfo& Font,const FLinearColor& Color,FVector2D Align)
{
    if(Value.IsEmpty()||Opacity<=0.f){return FVector2D::ZeroVector;}
    Flush();
    const FVector2D Size=Measure(Value,Font);
    const FVector2D TopLeft=At-Size*Align;
    FSlateDrawElement::MakeText(Elements,Next(),Geometry.ToPaintGeometry(FVector2f(Size),FSlateLayoutTransform(FVector2f(TopLeft))),Value,Font,
        ESlateDrawEffect::None,Fade(Color));
    return Size;
}
