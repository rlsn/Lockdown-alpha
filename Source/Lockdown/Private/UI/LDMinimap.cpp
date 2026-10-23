#include "UI/LDMinimap.h"
#include "UI/LDHUDModel.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Vision/LDWorldViewRelay.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDRegionDefinition.h"
#include "Core/LDGroundQueries.h"
#include "Economy/LDProductionDirector.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Engine/World.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"

TSharedRef<SWidget> ULDMinimap::RebuildWidget()
{if(!WidgetTree->RootWidget){auto* Background=WidgetTree->ConstructWidget<UBorder>();Background->SetBrushColor(FLinearColor(.02f,.03f,.05f));WidgetTree->RootWidget=Background;}return Super::RebuildWidget();}

FVector2D ULDMinimap::Fraction(const FGeometry& G,const FPointerEvent& E) const
{return G.AbsoluteToLocal(E.GetScreenSpacePosition())/G.GetLocalSize();}
bool ULDMinimap::Gesture(FVector2D P,bool Move)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(!PC->CanWorldCommand()){return false;}
    if(Move&&PC->HasArmedAction()){PC->CancelArmed();return true;}
    const auto* Definition=LDRegion::Definition(GetWorld());const FLDVisionGrid Grid=Definition?Definition->Grid:FLDVisionGrid();
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
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* R=PC->LocalView();if(!R){return Base;}
    const auto& V=R->LastView();const auto* Definition=LDRegion::Definition(GetWorld());const FLDVisionGrid Grid=Definition?Definition->Grid:V.Grid;
    const FVector2D Size=G.GetLocalSize(),Cell=Size/FVector2D(60,40);
    auto Box=[&](FVector2D P,FVector2D Extent,FLinearColor Color){FSlateDrawElement::MakeBox(E,Base+1,G.ToPaintGeometry(Extent,FSlateLayoutTransform(P)),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,Color);};
    auto Line=[&](TArray<FVector2D> Points,FLinearColor Color,float Width=1.f){FSlateDrawElement::MakeLines(E,Base+2,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width);};
    for(int32 I=0;I<LDVision::Cells;++I)
    {
        const bool Current=!R->IsStale()&&LDVision::Bit(V.Current,I),Known=LDVision::Bit(V.Explored,I);const FVector2D At((I%60)*Cell.X,(39-I/60)*Cell.Y);
        Box(At,Cell,Current?FLinearColor(.26f,.42f,.40f):Known?FLinearColor(.10f,.16f,.20f):FLinearColor(.025f,.035f,.055f));
        if(Known&&!Current&&I%3==0){Line({At,At+Cell*.7f},FLinearColor(.25f,.29f,.34f));}
        if(!Known&&I%4==0){Box(At+Cell*.45f,FVector2D(1),FLinearColor(.17f,.18f,.23f));}
    }
    if(!R->IsStale()){for(const auto& Record:V.Entities)
    {
        const FVector2D At=LDHUD::MapPoint(Record.Position,Grid)*Size;
        if(Record.Kind==ELDObservedKind::Ward){const float Radius=Record.Presentation==1?4.f:2.f;Line({At+FVector2D(0,-Radius),At+FVector2D(Radius,0),At+FVector2D(0,Radius),At+FVector2D(-Radius,0),At+FVector2D(0,-Radius)},FLinearColor(.4f,1,.8f));}
        else{Box(At-FVector2D(2),FVector2D(4),Record.Kind==ELDObservedKind::Seal?FLinearColor(1,.76f,.2f):Record.Kind==ELDObservedKind::Sentinel?FLinearColor(1,.35f,.25f):FLinearColor(.9f,.9f,.75f));}
    }}
    const auto* State=GetWorld()->GetGameState<ALDGameState>();
    for(uint8 Site:{uint8(1),uint8(2),uint8(3),uint8(6),uint8(7),uint8(8)})
    {
        if(Site>3&&!Definition){continue;}const FVector2D At=LDHUD::MapPoint(LDRegion::Site(GetWorld(),Site),Grid)*Size;
        const FString Label=Site==1?TEXT("G"):Site==2?TEXT("W"):Site==3?TEXT("H"):Site==6?TEXT("E"):Site==7?(State&&State->bPublicSensorActive?TEXT("T+"):TEXT("T−")):TEXT("X");
        Box(At-FVector2D(2),FVector2D(5),FLinearColor(.3f,.8f,1));FSlateDrawElement::MakeText(E,Base+3,G.ToPaintGeometry(FVector2D(24,14),FSlateLayoutTransform(At+FVector2D(3,-7))),Label,FCoreStyle::GetDefaultFontStyle("Bold",12),ESlateDrawEffect::None,FLinearColor::White);
    }
    if(State&&State->bUIControlProfile)
    {for(uint8 Site=0;Site<5;++Site){const FVector2D At=LDHUD::MapPoint(ALDProductionDirector::Position(GetWorld(),Site),Grid)*Size;const FString Label=Site<2?FString::Printf(TEXT("C%d:%d"),Site+1,Site?State->OutcropTwo:State->OutcropOne):TEXT("WS");Line({At+FVector2D(0,-4),At+FVector2D(4,0),At+FVector2D(0,4),At+FVector2D(-4,0),At+FVector2D(0,-4)},FLinearColor(.7f,.5f,1));FSlateDrawElement::MakeText(E,Base+3,G.ToPaintGeometry(FVector2D(40,14),FSlateLayoutTransform(At+FVector2D(3,7))),Label,FCoreStyle::GetDefaultFontStyle("Bold",10),ESlateDrawEffect::None,FLinearColor::White);}}
    if(PC->GetPawn()){Box(LDHUD::MapPoint(PC->GetPawn()->GetActorLocation(),Grid)*Size-FVector2D(3),FVector2D(6),FLinearColor(.3f,1,.8f));}
    // Actual camera's ground footprint, without querying any world actor.
    int32 W=0,H=0;PC->GetViewportSize(W,H);TArray<FVector2D> Footprint;
    for(FVector2D Pixel:{FVector2D(0,0),FVector2D(W,0),FVector2D(W,H),FVector2D(0,H)})
    {FVector Origin,Direction;if(PC->DeprojectScreenPositionToWorld(float(Pixel.X),float(Pixel.Y),Origin,Direction)&&Direction.Z<-.001){const FVector P=Origin+Direction*(-Origin.Z/Direction.Z);const FVector2D Fraction=LDHUD::MapPoint(P,Grid);Footprint.Add(FVector2D(FMath::Clamp(Fraction.X,0.,1.),FMath::Clamp(Fraction.Y,0.,1.))*Size);}}
    if(Footprint.Num()==4){const FVector2D First=Footprint[0];Footprint.Add(First);Line(Footprint,FLinearColor(1,1,1,.8f),1.5f);}return Base+3;
}
