#include "UI/LDCursor.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

namespace
{
// Slate draws a software cursor centred on the pointer, so the widget is twice the arrow's reach and the tip sits at
// its centre. Arrow outline in local units from the tip (1080p reference; Slate applies the DPI and UI scale).
constexpr float CursorHalf=32.f;
const FVector2f CursorShape[]={FVector2f(0.f,0.f),FVector2f(0.f,26.f),FVector2f(8.f,17.3f),FVector2f(20.f,17.7f)};
const FLinearColor CursorFace(1.f,1.f,1.f);
const FLinearColor CursorEdge(.02f,.02f,.03f);
const FLinearColor CursorAttackGlow(1.f,.12f,.08f);
const FLinearColor CursorCastGlow(.25f,.6f,1.f);

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
        // The local units are scaled to the drawn size, so the arrow follows the software cursor scale.
        const float Unit=float(Geometry.GetLocalSize().X)/(2.f*CursorHalf);
        // The outline closes on a copy of its first corner: adding an element of the array to itself is refused.
        TArray<FVector2f> Loop;Loop.Reserve(UE_ARRAY_COUNT(CursorShape)+1);
        for(const FVector2f& Corner:CursorShape){Loop.Add((Corner+FVector2f(CursorHalf,CursorHalf))*Unit);}
        const FVector2f Start=Loop[0];
        Loop.Add(Start);
        const FPaintGeometry Paint=Geometry.ToPaintGeometry();
        // The glow: wide, faint strokes under the face, narrowing and brightening toward the edge.
        if(Glow.A>0.f)
        {
            const float Widths[]={10.f,6.5f,3.5f};const float Alphas[]={.18f,.35f,.75f};
            for(int32 Pass=0;Pass<3;++Pass){FSlateDrawElement::MakeLines(Elements,Layer,Paint,Loop,ESlateDrawEffect::None,Glow.CopyWithNewOpacity(Alphas[Pass]),true,Widths[Pass]*Unit);}
        }
        // The face: two triangles of the notched outline (tip-bottom-notch and tip-notch-right).
        if(FSlateApplication::IsInitialized())
        {
            const FSlateResourceHandle White=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
            const FSlateRenderTransform& ToWindow=Geometry.GetAccumulatedRenderTransform();
            TArray<FSlateVertex> Verts;
            for(int32 Corner=0;Corner<4;++Corner)
            {Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(ToWindow,Loop[Corner],FVector2f(0.f,0.f),CursorFace.ToFColor(true)));}
            const TArray<SlateIndex> Indices={0,1,2,0,2,3};
            FSlateDrawElement::MakeCustomVerts(Elements,Layer+1,White,Verts,Indices,nullptr,0,0);
        }
        FSlateDrawElement::MakeLines(Elements,Layer+2,Paint,Loop,ESlateDrawEffect::None,CursorEdge,true,1.8f*Unit);
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
