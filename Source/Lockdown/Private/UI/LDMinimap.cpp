#include "UI/LDMinimap.h"
#include "UI/LDHUDModel.h"
#include "Framework/LDPlayerController.h"
#include "Vision/LDWorldViewRelay.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "Core/LDGroundQueries.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Engine/World.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"

namespace
{
// The minimap shares the registered ground bounds that observation coverage indexes.
bool MinimapGroundBounds(const UWorld* World,FBox2D& Out)
{
    const auto* Registry=World?World->GetSubsystem<ULDMapRegistry>():nullptr;
    const FLDDemoWorldGeometry* Geometry=Registry&&Registry->Enabled()?Registry->GetGeometry():nullptr;
    FVector Min,Max;FString Error;
    if(!Geometry||Geometry->Surfaces.Num()!=1||!Geometry->Surfaces[0].MinCm.ToVector(Min,Error)||!Geometry->Surfaces[0].MaxCm.ToVector(Max,Error)||Max.X<=Min.X||Max.Y<=Min.Y){return false;}
    Out=FBox2D(FVector2D(Min.X,Min.Y),FVector2D(Max.X,Max.Y));
    return true;
}
}
TSharedRef<SWidget> ULDMinimap::RebuildWidget()
{if(!WidgetTree->RootWidget){auto* Background=WidgetTree->ConstructWidget<UBorder>();Background->SetBrushColor(FLinearColor(.02f,.03f,.05f));WidgetTree->RootWidget=Background;}return Super::RebuildWidget();}

FVector2D ULDMinimap::Fraction(const FGeometry& G,const FPointerEvent& E) const
{return G.AbsoluteToLocal(E.GetScreenSpacePosition())/G.GetLocalSize();}
bool ULDMinimap::Gesture(FVector2D P,bool Move)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(!PC->CanWorldCommand()){return false;}
    FBox2D Grid;if(!MinimapGroundBounds(GetWorld(),Grid)){PC->IntentFeedback=TEXT("Map unavailable outside the registered Demo world");return true;}
    const FVector XY=LDHUD::MapWorld(P,Grid);FVector Surface;
    if(Move){if(!LDGround::MapSurface(GetWorld(),XY,Surface)){PC->IntentFeedback=TEXT("No walkable route");return true;}PC->Navigation->Navigate(Surface);}
    else{PC->Navigation->PanTo(XY);}
    return true;
}
FReply ULDMinimap::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetEffectingButton()==EKeys::LeftMouseButton){bDragging=Gesture(Fraction(G,E),false);return FReply::Handled().CaptureMouse(TakeWidget());}
    if(E.GetEffectingButton()==EKeys::RightMouseButton){Gesture(Fraction(G,E),true);return FReply::Handled();}return FReply::Handled();
}
FReply ULDMinimap::NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E)
{if(bDragging){Gesture(Fraction(G,E),false);}return FReply::Handled();}
FReply ULDMinimap::NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)
{if(E.GetEffectingButton()==EKeys::LeftMouseButton){bDragging=false;return FReply::Handled().ReleaseMouseCapture();}return FReply::Handled();}
void ULDMinimap::NativeOnMouseCaptureLost(const FCaptureLostEvent& E){bDragging=false;Super::NativeOnMouseCaptureLost(E);}
void ULDMinimap::NativeTick(const FGeometry& G,float D)
{Super::NativeTick(G,D);if(!CastChecked<ALDPlayerController>(GetOwningPlayer())->CanWorldCommand()){bDragging=false;}}
int32 ULDMinimap::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    FBox2D Grid;if(!MinimapGroundBounds(GetWorld(),Grid)){return Base;}
    const FVector2D Size=G.GetLocalSize();
    auto Box=[&](FVector2D P,FVector2D Extent,FLinearColor Color){FSlateDrawElement::MakeBox(E,Base+1,G.ToPaintGeometry(Extent,FSlateLayoutTransform(P)),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,Color);};
    auto Line=[&](TArray<FVector2D> Points,FLinearColor Color,float Width=1.f){FSlateDrawElement::MakeLines(E,Base+2,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width);};
    Box(FVector2D::ZeroVector,Size,FLinearColor(.025f,.035f,.055f));
    const auto* R=PC->LocalView();const FLDObservedFrameRead Frame=R?R->ReadObservedFrame():FLDObservedFrameRead();
    if(Frame.Frame&&Frame.Status==ELDObservedViewStatus::Current&&Frame.Frame->Footprint.SourceStatus==ELDObservationSource::ActiveBody)
    {
        // Current authorized coverage only: no explored or remembered layer exists. Rows merge into runs.
        const int32 Side=LDObservation::CoverageSide;const FVector2D Cell=Size/double(Side);const auto& Bits=Frame.Frame->Footprint.CurrentBits;
        for(int32 Y=0;Y<Side;++Y)
        {
            for(int32 X=0;X<Side;)
            {
                if(!LDObservation::CoverageBit(Bits,Y*Side+X)){++X;continue;}
                const int32 Start=X;while(X<Side&&LDObservation::CoverageBit(Bits,Y*Side+X)){++X;}
                Box(FVector2D(Start*Cell.X,(Side-1-Y)*Cell.Y),FVector2D((X-Start)*Cell.X,Cell.Y),FLinearColor(.26f,.42f,.40f));
            }
        }
        for(const FLDObservedUnitView& Unit:Frame.Frame->Units)
        {Box(LDHUD::MapPoint(FVector(Unit.FeetCm),Grid)*Size-FVector2D(2),FVector2D(4),Unit.Kind==ELDObservedUnitKind::Contender?FLinearColor(1,.35f,.25f):FLinearColor(.9f,.8f,.3f));}
    }
    if(PC->GetPawn()){Box(LDHUD::MapPoint(PC->GetPawn()->GetActorLocation(),Grid)*Size-FVector2D(3),FVector2D(6),FLinearColor(.3f,1,.8f));}
    // Actual camera's ground footprint, without querying any world actor.
    int32 W=0,H=0;PC->GetViewportSize(W,H);TArray<FVector2D> Footprint;
    for(FVector2D Pixel:{FVector2D(0,0),FVector2D(W,0),FVector2D(W,H),FVector2D(0,H)})
    {FVector Origin,Direction;if(PC->DeprojectScreenPositionToWorld(float(Pixel.X),float(Pixel.Y),Origin,Direction)&&Direction.Z<-.001){const FVector P=Origin+Direction*(-Origin.Z/Direction.Z);const FVector2D Fraction=LDHUD::MapPoint(P,Grid);Footprint.Add(FVector2D(FMath::Clamp(Fraction.X,0.,1.),FMath::Clamp(Fraction.Y,0.,1.))*Size);}}
    if(Footprint.Num()==4){const FVector2D First=Footprint[0];Footprint.Add(First);Line(Footprint,FLinearColor(1,1,1,.8f),1.5f);}return Base+3;
}
