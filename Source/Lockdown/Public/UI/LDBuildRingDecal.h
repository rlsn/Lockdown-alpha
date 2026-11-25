#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Actor.h"
#include "UI/LDBuildRingModel.h"
#include "LDBuildRingDecal.generated.h"

class UWidgetComponent;

// What the ground radar shows: the five stats against the keystone line, and (closed) the capacity ticks and the medal dots.
struct FLDBuildRadarState
{
    TArray<int32> Points;
    int32 Keystone=0;
    bool bClosed=false;
    int32 Installed=0;
    int32 Capacity=0;
    int32 MaxCapacity=0;
    // The socketed hosts' element colours at their ring angles (degrees), for the closed dots.
    TArray<TPair<float,FLinearColor>> Medals;
    float Opacity=1.f;
};

// The radar drawn into the ground decal's render target: the widget's own 640 x 640 canvas, the radar's full radius at 200 px.
UCLASS()
class LOCKDOWN_API ULDBuildRadarWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    static constexpr float Canvas=640.f;
    // Widget px per ring design px (the radar's design radius reaches 200 widget px).
    static constexpr float PerDesign=200.f/LDBuildRing::RadarRadius;
    FLDBuildRadarState State;
    // The radius (design px) of Points at a stat axis: a small hub, then the keystone at 18 of 22 points to the rim.
    static float AxisRadius(int32 Points,int32 Keystone);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
};

// The build ring's five-stat radar lying on the ground under the own character (approved design: a ground decal the character
// stands on). A presentation-only actor: a world-space widget plane a few millimetres above the feet, translucent and unlit,
// with no collision, navigation, shadow or replication; the character's body hides it where it stands. The ring widget owns,
// moves and removes it.
UCLASS(NotPlaceable,Transient)
class LOCKDOWN_API ALDBuildRadarDecal : public AActor
{
    GENERATED_BODY()
public:
    ALDBuildRadarDecal();
    // Lays the radar at Feet facing up, its top towards the camera's forward (CameraYaw), the radar radius RadiusCm.
    void Show(const FLDBuildRadarState& State,const FVector& Feet,float CameraYaw,float RadiusCm);
    void Hide();
    // The world point of a ring position (design px from the centre, degrees clockwise from the radar's top).
    FVector WorldAt(float DesignRadius,float Degrees) const;
    bool IsShown() const {return bShown;}
    UPROPERTY() TObjectPtr<UWidgetComponent> Plane;
private:
    float RadiusCm=60.f;
    float Yaw=0.f;
    FVector Feet=FVector::ZeroVector;
    bool bShown=false;
};
