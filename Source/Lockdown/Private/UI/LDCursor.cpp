#include "UI/LDCursor.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

namespace
{
// Slate draws a software cursor centred on the pointer, so the widget is twice the arrow's reach and the tip sits at
// its centre. Arrow outline in local units from the tip (1080p reference; Slate applies the DPI and UI scale), drawn at
// 1.5 times the original 26-unit arrow.
constexpr float CursorScale=1.5f;
constexpr float CursorHalf=56.f;
const FVector2f CursorShape[]={FVector2f(0.f,0.f),FVector2f(0.f,26.f),FVector2f(8.f,17.3f),FVector2f(20.f,17.7f)};
// Bands outside the face, in local units: the dark outline, then the glow fading from its inner edge to nothing.
constexpr float CursorEdgeWidth=1.6f*CursorScale,CursorGlowWidth=6.f*CursorScale;
const FLinearColor CursorFace(1.f,1.f,1.f);
const FLinearColor CursorEdge(.02f,.02f,.03f);
const FLinearColor CursorAttackGlow(1.f,.12f,.08f);
const FLinearColor CursorCastGlow(.25f,.6f,1.f);

// A band round the closed outline Corners, from Inner to Outer units outside it, coloured InnerInk at its inner edge and
// OuterInk at its outer edge. Convex corners are rounded with an arc and the notch is mitred, so the band has the same
// width all round, corners included, and never overlaps itself (one mesh, so no doubled alpha at a corner).
void CursorBand(FSlateWindowElementList& Elements,int32 Layer,const FSlateResourceHandle& White,const FSlateRenderTransform& ToWindow,
    const TArray<FVector2f>& Corners,float Inner,float Outer,const FLinearColor& InnerInk,const FLinearColor& OuterInk)
{
    const int32 Count=Corners.Num();
    if(Count<3){return;}
    FVector2f Centre(0.f,0.f);float Area=0.f;
    for(int32 Index=0;Index<Count;++Index){Centre+=Corners[Index];Area+=FVector2f::CrossProduct(Corners[Index],Corners[(Index+1)%Count]);}
    Centre/=float(Count);
    // The outward normal of the edge from corner Index to the next one.
    auto Normal=[&](int32 Index)
    {
        const FVector2f From=Corners[Index],To=Corners[(Index+1)%Count];const FVector2f Along=(To-From).GetSafeNormal();
        FVector2f Out(Along.Y,-Along.X);
        if(FVector2f::DotProduct(Out,(From+To)*.5f-Centre)<0.f){Out=-Out;}
        return Out;
    };
    // Each corner's outward directions (scaled so a mitre keeps the band's width), in outline order.
    TArray<TPair<int32,FVector2f>> Rays;
    for(int32 Index=0;Index<Count;++Index)
    {
        const FVector2f Before=Normal((Index+Count-1)%Count),After=Normal(Index);
        const FVector2f In=Corners[Index]-Corners[(Index+Count-1)%Count],Out=Corners[(Index+1)%Count]-Corners[Index];
        if(FVector2f::CrossProduct(In,Out)*Area>=0.f)
        {
            const float Turn=FMath::Atan2(FVector2f::CrossProduct(Before,After),FVector2f::DotProduct(Before,After));
            const int32 Steps=FMath::Max(2,FMath::CeilToInt(FMath::Abs(Turn)/(UE_PI/12.f)));
            const float Start=FMath::Atan2(Before.Y,Before.X);
            for(int32 Step=0;Step<=Steps;++Step)
            {const float Angle=Start+Turn*float(Step)/float(Steps);Rays.Emplace(Index,FVector2f(FMath::Cos(Angle),FMath::Sin(Angle)));}
        }
        else
        {
            const FVector2f Mitre=(Before+After).GetSafeNormal();
            Rays.Emplace(Index,Mitre/FMath::Max(FVector2f::DotProduct(Mitre,Before),.25f));
        }
    }
    const FColor InnerColour=InnerInk.ToFColor(true),OuterColour=OuterInk.ToFColor(true);
    TArray<FSlateVertex> Verts;Verts.Reserve(Rays.Num()*2);
    for(const TPair<int32,FVector2f>& Ray:Rays)
    {
        const FVector2f At=Corners[Ray.Key];
        Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(ToWindow,At+Ray.Value*Inner,FVector2f(0.f,0.f),InnerColour));
        Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(ToWindow,At+Ray.Value*Outer,FVector2f(0.f,0.f),OuterColour));
    }
    TArray<SlateIndex> Indices;Indices.Reserve(Rays.Num()*6);
    for(int32 Ray=0;Ray<Rays.Num();++Ray)
    {
        const SlateIndex A=SlateIndex(Ray*2),B=SlateIndex(((Ray+1)%Rays.Num())*2);
        Indices.Append({A,SlateIndex(A+1),SlateIndex(B+1),A,SlateIndex(B+1),B});
    }
    FSlateDrawElement::MakeCustomVerts(Elements,Layer,White,Verts,Indices,nullptr,0,0);
}

class SLDCursorArrow final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SLDCursorArrow):_Glow(FLinearColor::Transparent){}
        // Transparent draws no glow.
        SLATE_ARGUMENT(FLinearColor,Glow)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args){Glow=Args._Glow;}
    virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D(2.f*CursorHalf,2.f*CursorHalf);}
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override
    {
        if(!FSlateApplication::IsInitialized()){return Layer;}
        // The local units are scaled to the drawn size, so the arrow follows the software cursor scale.
        const float Unit=float(Geometry.GetLocalSize().X)/(2.f*CursorHalf);
        TArray<FVector2f> Corners;Corners.Reserve(UE_ARRAY_COUNT(CursorShape));
        for(const FVector2f& Corner:CursorShape){Corners.Add((Corner*CursorScale+FVector2f(CursorHalf,CursorHalf))*Unit);}
        const FSlateResourceHandle White=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
        const FSlateRenderTransform& ToWindow=Geometry.GetAccumulatedRenderTransform();
        // The glow: outside the outline, fading out, round every corner.
        if(Glow.A>0.f)
        {CursorBand(Elements,Layer,White,ToWindow,Corners,CursorEdgeWidth*Unit,(CursorEdgeWidth+CursorGlowWidth)*Unit,Glow.CopyWithNewOpacity(.9f),Glow.CopyWithNewOpacity(0.f));}
        // The face: two triangles of the notched outline (tip-bottom-notch and tip-notch-right).
        TArray<FSlateVertex> Verts;
        for(const FVector2f& Corner:Corners){Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(ToWindow,Corner,FVector2f(0.f,0.f),CursorFace.ToFColor(true)));}
        const TArray<SlateIndex> Indices={0,1,2,0,2,3};
        FSlateDrawElement::MakeCustomVerts(Elements,Layer+1,White,Verts,Indices,nullptr,0,0);
        // The dark outline as a solid band, so its corners are as full as its sides.
        CursorBand(Elements,Layer+2,White,ToWindow,Corners,0.f,CursorEdgeWidth*Unit,CursorEdge,CursorEdge);
        return Layer+2;
    }
private:
    FLinearColor Glow=FLinearColor::Transparent;
};
}

void LDCursor::Install(UGameViewportClient& Viewport)
{
    Viewport.SetUseSoftwareCursorWidgets(true);
    const TSharedRef<SWidget> Plain=SNew(SLDCursorArrow);
    for(int32 Type=int32(EMouseCursor::Default);Type<int32(EMouseCursor::TotalCursorCount);++Type)
    {Viewport.SetSoftwareCursorWidget(static_cast<EMouseCursor::Type>(Type),Plain);}
    Viewport.SetSoftwareCursorWidget(AttackAim,TSharedPtr<SWidget>(SNew(SLDCursorArrow).Glow(CursorAttackGlow)));
    Viewport.SetSoftwareCursorWidget(CastAim,TSharedPtr<SWidget>(SNew(SLDCursorArrow).Glow(CursorCastGlow)));
}
