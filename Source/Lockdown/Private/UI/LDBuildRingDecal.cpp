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
    // A hub of 28% of the rim, then even steps: the keystone (18) at 87% and the rim at 22 points.
    const float Rim=LDBuildRing::RadarRadius,Hub=Rim*.28f;
    const float Max=Keystone>0?float(Keystone)*22.f/18.f:22.f;
    return Hub+FMath::Min(float(FMath::Max(0,Points)),Max)*((Rim-Hub)/Max);
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
    const FLinearColor White=LDBroadcast::White,Ink=LDBroadcast::Ink;
    // The plate (the broadcast instrument's face): an ink pentagon to the rim, its grid, the spokes and the keystone line.
    P.Fill(Ring([&](int32){return Rim;}),Ink.CopyWithNewOpacity(.35f*Dim));
    for(const float Grid:{AxisRadius(State.Keystone/3,State.Keystone),AxisRadius(State.Keystone*2/3,State.Keystone)})
    {P.Stroke(Ring([&](int32){return Grid;}),1.4f*K,White.CopyWithNewOpacity(.2f*Dim),true);}
    P.Stroke(Ring([&](int32){return Rim;}),2.f*K,White.CopyWithNewOpacity(.55f*Dim),true);
    if(State.Keystone>0)
    {
        P.Dashed(Ring([&](int32){return AxisRadius(State.Keystone,State.Keystone);}),1.6f*K,5.f*K,5.f*K,White.CopyWithNewOpacity(.45f*Dim),true);
    }
    for(int32 Axis=0;Axis<Count;++Axis){P.Stroke({C,At(Rim,Axis)},1.4f*K,White.CopyWithNewOpacity(.18f*Dim));}
    // The shape of the build: a white fill with a white edge.
    const TArray<FVector2D> Shape=Ring([&](int32 Axis){return AxisRadius(State.Points[Axis],State.Keystone);});
    P.Fill(Shape,White.CopyWithNewOpacity(.3f*Dim));
    P.Stroke(Shape,2.5f*K,White.CopyWithNewOpacity(Dim),true);
    if(State.bClosed)
    {
        // The capacity ticks: installed white, open faint, locked fainter, 300 degrees around the feet.
        const int32 Ticks=FMath::Max(State.MaxCapacity,State.Capacity);
        for(int32 Tick=0;Tick<Ticks;++Tick)
        {
            const float A=Ticks>1?-150.f+Tick*(300.f/(Ticks-1)):0.f;
            const FLinearColor Mark=Tick<State.Installed?White:Tick<State.Capacity?White.CopyWithNewOpacity(.4f):White.CopyWithNewOpacity(.12f);
            P.Stroke(LDRingPaint::Arc(C,106.f*K,A-9.f,A+9.f,6),5.f*K,Mark);
        }
        for(const TPair<float,FLinearColor>& Medal:State.Medals)
        {
            const FVector2D Dot=LDRingPaint::Polar(C,122.f*K,Medal.Key*.6f);
            const float H=9.f*K;
            P.Fill({Dot+FVector2D(0,-H-2.5f*K),Dot+FVector2D(H+2.5f*K,0),Dot+FVector2D(0,H+2.5f*K),Dot+FVector2D(-H-2.5f*K,0)},Ink);
            P.Fill({Dot+FVector2D(0,-H),Dot+FVector2D(H,0),Dot+FVector2D(0,H),Dot+FVector2D(-H,0)},Medal.Value);
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
    Plane->SetManuallyRedraw(true);
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
        if(!bShown||!(Widget->State==State)){Widget->State=State;Plane->RequestRenderUpdate();}
    }
    Feet=InFeet;Yaw=CameraYaw;RadiusCm=FMath::Max(5.f,InRadiusCm);
    // Widget px -> cm: the radar's 200 px reach RadiusCm.
    const float Scale=RadiusCm/200.f;
    const FVector Forward=FRotator(0.f,Yaw,0.f).Vector();
    // The plane's normal points up and its widget top towards the camera's forward: the radar reads upright on screen.
    const FRotator Facing=FRotationMatrix::MakeFromXZ(FVector::UpVector,Forward).Rotator();
    Plane->SetWorldLocationAndRotation(Feet+FVector(0.f,0.f,RadarLiftCm),Facing);
    Plane->SetWorldScale3D(FVector(1.f,Scale,Scale));
    if(!bShown){Plane->SetHiddenInGame(false);}
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
