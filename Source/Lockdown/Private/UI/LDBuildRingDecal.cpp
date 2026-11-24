#include "UI/LDBuildRingDecal.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/WidgetComponent.h"
#include "UI/LDBuildRingPaint.h"
#include "UI/LDBroadcastStyle.h"

namespace
{
// The radar sits this far over the floor: above it for the depth test, under the character's feet.
constexpr float RadarLiftCm=1.5f;
}

float ULDBuildRadarWidget::AxisRadius(int32 Points,int32 Keystone)
{
    // The mock's scale: 26 px of hub, 3 px a point, the keystone (18) at 80 px and the rim at 22 points.
    const float Max=Keystone>0?float(Keystone)*22.f/18.f:22.f;
    return 26.f+FMath::Min(float(FMath::Max(0,Points)),Max)*(66.f/Max);
}
TSharedRef<SWidget> ULDBuildRadarWidget::RebuildWidget()
{
    if(!WidgetTree->RootWidget){WidgetTree->RootWidget=WidgetTree->ConstructWidget<UCanvasPanel>();}
    return Super::RebuildWidget();
}
int32 ULDBuildRadarWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    Layer=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
    const int32 Count=State.Points.Num();
    if(Count<3||State.Opacity<=0.f){return Layer;}
    LDRingPaint::FPainter P(Elements,Geometry,Layer);
    P.Opacity=State.Opacity;
    const FVector2D Size=Geometry.GetLocalSize();
    const float K=float(Size.X)/Canvas*PerDesign;
    const FVector2D C=Size*.5;
    const auto At=[&](float Design,int32 Axis){return LDRingPaint::Polar(C,Design*K,Axis*360.f/Count);};
    const auto Ring=[&](TFunctionRef<float(int32)> Radius){TArray<FVector2D> Out;for(int32 Axis=0;Axis<Count;++Axis){Out.Add(At(Radius(Axis),Axis));}return Out;};
    const float Rim=AxisRadius(1000,State.Keystone);
    const float Dim=State.bClosed?.55f:1.f;
    // The plate: the dark pentagon to the rim, its grid and the keystone line.
    P.Fill(Ring([&](int32){return Rim;}),FLinearColor(0.f,0.f,0.f,.42f*Dim));
    for(const float Grid:{AxisRadius(State.Keystone/3,State.Keystone),AxisRadius(State.Keystone*2/3,State.Keystone)})
    {P.Stroke(Ring([&](int32){return Grid;}),1.4f*K,FLinearColor(1.f,1.f,1.f,.22f*Dim),true);}
    P.Stroke(Ring([&](int32){return Rim;}),1.8f*K,FLinearColor(1.f,1.f,1.f,.42f*Dim),true);
    if(State.Keystone>0)
    {
        P.Dashed(Ring([&](int32){return AxisRadius(State.Keystone,State.Keystone);}),2.2f*K,5.f*K,5.f*K,LDBroadcast::Gold.CopyWithNewOpacity(.95f*Dim),true);
    }
    for(int32 Axis=0;Axis<Count;++Axis)
    {
        const FLinearColor Ink=State.Inks.IsValidIndex(Axis)?State.Inks[Axis]:FLinearColor::White;
        P.Stroke({C,At(Rim,Axis)},1.6f*K,Ink.CopyWithNewOpacity(.55f*Dim));
    }
    // The shape of the build: a gold-to-pink fill with a black keyline and a gold edge.
    const TArray<FVector2D> Shape=Ring([&](int32 Axis){return AxisRadius(State.Points[Axis],State.Keystone);});
    const float Reach=Rim*K;
    P.FillShaded(Shape,[&](FVector2D Point)
    {
        const float T=FMath::Clamp(float(FVector2D::Distance(Point,C))/Reach,0.f,1.f);
        const FLinearColor Inner=LDBroadcast::Gold.CopyWithNewOpacity(.22f),Mid=LDBroadcast::Gold.CopyWithNewOpacity(.5f),Outer=LDRingPaint::Hex(0xFF4D8D,.62f);
        const FLinearColor Out=T<.55f?LDRingPaint::Mix(Inner,Mid,T/.55f):LDRingPaint::Mix(Mid,Outer,(T-.55f)/.45f);
        return Out.CopyWithNewOpacity(Out.A*Dim);
    });
    P.Stroke(Shape,6.f*K,LDRingPaint::Hex(0x04060B,.9f*Dim),true);
    P.Stroke(Shape,3.f*K,LDRingPaint::Hex(0xFFE27A,Dim),true);
    for(int32 Axis=0;Axis<Count;++Axis)
    {
        const FVector2D Dot=Shape[Axis];
        const FLinearColor Ink=State.Inks.IsValidIndex(Axis)?State.Inks[Axis]:FLinearColor::White;
        P.Fill(LDRingPaint::Octagon(FBox2D(Dot-FVector2D(7.,7.)*K,Dot+FVector2D(7.,7.)*K),.29f),LDRingPaint::Hex(0x04060B));
        P.Fill(LDRingPaint::Octagon(FBox2D(Dot-FVector2D(5.,5.)*K,Dot+FVector2D(5.,5.)*K),.29f),Ink);
    }
    if(State.bClosed)
    {
        // The capacity ticks: installed gold, open white, locked faint, 300 degrees around the feet.
        const int32 Ticks=FMath::Max(State.MaxCapacity,State.Capacity);
        for(int32 Tick=0;Tick<Ticks;++Tick)
        {
            const float A=Ticks>1?-150.f+Tick*(300.f/(Ticks-1)):0.f;
            const FLinearColor Ink=Tick<State.Installed?LDBroadcast::Gold:Tick<State.Capacity?FLinearColor(1.f,1.f,1.f,.45f):FLinearColor(1.f,1.f,1.f,.14f);
            P.Stroke(LDRingPaint::Arc(C,116.f*K,A-9.f,A+9.f,6),5.f*K,Ink);
        }
        for(const TPair<float,FLinearColor>& Medal:State.Medals)
        {
            const FVector2D Dot=LDRingPaint::Polar(C,134.f*K,Medal.Key*.6f);
            const float H=9.f*K;
            const TArray<FVector2D> Diamond={Dot+FVector2D(0,-H),Dot+FVector2D(H,0),Dot+FVector2D(0,H),Dot+FVector2D(-H,0)};
            P.Fill({Dot+FVector2D(0,-H-2.5f*K),Dot+FVector2D(H+2.5f*K,0),Dot+FVector2D(0,H+2.5f*K),Dot+FVector2D(-H-2.5f*K,0)},LDRingPaint::Hex(0x04060B));
            P.Fill(Diamond,Medal.Value);
        }
    }
    return P.Layer;
}

ALDBuildRadarDecal::ALDBuildRadarDecal()
{
    PrimaryActorTick.bCanEverTick=false;
    bReplicates=false;
    SetCanBeDamaged(false);
    Plane=CreateDefaultSubobject<UWidgetComponent>(TEXT("Plane"));
    RootComponent=Plane;
    Plane->SetWidgetSpace(EWidgetSpace::World);
    Plane->SetWidgetClass(ULDBuildRadarWidget::StaticClass());
    Plane->SetDrawSize(FVector2D(ULDBuildRadarWidget::Canvas,ULDBuildRadarWidget::Canvas));
    Plane->SetPivot(FVector2D(.5f,.5f));
    Plane->SetBlendMode(EWidgetBlendMode::Transparent);
    Plane->SetTwoSided(true);
    Plane->SetDrawAtDesiredSize(false);
    Plane->SetTickWhenOffscreen(true);
    Plane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Plane->SetCollisionResponseToAllChannels(ECR_Ignore);
    Plane->SetGenerateOverlapEvents(false);
    Plane->SetCanEverAffectNavigation(false);
    Plane->SetCastShadow(false);
    Plane->SetHiddenInGame(true);
}
void ALDBuildRadarDecal::Show(const FLDBuildRadarState& State,const FVector& InFeet,float CameraYaw,float InRadiusCm)
{
    if(!Plane){return;}
    if(ULDBuildRadarWidget* Widget=Cast<ULDBuildRadarWidget>(Plane->GetUserWidgetObject()))
    {
        Widget->State=State;
    }
    Feet=InFeet;Yaw=CameraYaw;RadiusCm=FMath::Max(5.f,InRadiusCm);
    // Widget px -> cm: the radar's 200 px reach RadiusCm.
    const float Scale=RadiusCm/200.f;
    const FVector Forward=FRotator(0.f,Yaw,0.f).Vector();
    // The plane's normal points up and its widget top towards the camera's forward: the radar reads upright on screen.
    const FRotator Facing=FRotationMatrix::MakeFromXZ(FVector::UpVector,Forward).Rotator();
    Plane->SetWorldLocationAndRotation(Feet+FVector(0.f,0.f,RadarLiftCm),Facing);
    Plane->SetWorldScale3D(FVector(1.f,Scale,Scale));
    Plane->SetHiddenInGame(false);
    Plane->RequestRenderUpdate();
    bShown=true;
}
void ALDBuildRadarDecal::Hide()
{
    if(Plane&&bShown){Plane->SetHiddenInGame(true);}
    bShown=false;
}
FVector ALDBuildRadarDecal::WorldAt(float DesignRadius,float Degrees) const
{
    const FVector Forward=FRotator(0.f,Yaw,0.f).Vector();
    const FVector Right=FRotator(0.f,Yaw+90.f,0.f).Vector();
    const float Cm=DesignRadius*ULDBuildRadarWidget::PerDesign*RadiusCm/200.f;
    const float T=FMath::DegreesToRadians(Degrees);
    return Feet+FVector(0.f,0.f,RadarLiftCm)+(Right*FMath::Sin(T)+Forward*FMath::Cos(T))*Cm;
}
