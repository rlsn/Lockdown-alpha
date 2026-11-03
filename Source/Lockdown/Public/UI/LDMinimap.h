#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/LDControlSettings.h"
#include "LDMinimap.generated.h"
struct FLDMinimapStatic;

// Minimap placement. Every orientation is a rotation plus one uniform scale of the true top view, never a reflection: UE
// axes are left-handed, so seen from above the right-hand side of a ground direction is its yaw +90°. A view draws its
// Forward straight up and its Right (Forward turned +90° yaw) to the right.
namespace LDMinimap
{
    // Frame units kept clear inside every frame edge, so a glyph on the fitted content is not cut off.
    constexpr double EdgeMargin=8.;
    // 固定正北 draws world -Y up: the editor top view, +X right and +Y down.
    constexpr double NorthYawDeg=-90.;
    struct FView
    {
        // Effective orientation: Facing without a living body is the Camera view.
        ELDMinimapOrientation Mode=ELDMinimapOrientation::North;
        // World point drawn at the frame centre: the centre of the fitted box.
        FVector2D Centre=FVector2D::ZeroVector;
        // Ground unit vectors drawn straight up and to the right.
        FVector2D Forward=FVector2D(0.,-1.);
        FVector2D Right=FVector2D(1.,0.);
        FVector2D FrameSize=FVector2D::ZeroVector;
        // Frame units per world cm along both frame axes; zero for an invalid view.
        double Scale=0.;
        bool IsValid() const {return Scale>0.&&FrameSize.X>0.&&FrameSize.Y>0.;}
    };
    // Fit is the world box the frame shows. Camera: the camera's ground forward is up and the rotated box's bounds fit the
    // frame. Facing: the living body's facing is up and the box's circumscribed circle fits, so turning never rescales;
    // without a body it is the Camera view. North: world -Y is up whatever the camera or body. Fitting keeps EdgeMargin
    // clear. An empty box or frame, or a non-finite yaw in use, gives an invalid view.
    // A positive FitRadius makes both turning views fit the circle of that radius about the box centre instead, so a
    // square ground turned 45° does not shrink the drawn content; ground beyond the circle may then be cut off at the
    // frame edge. North always fits the box.
    LOCKDOWN_API FView MakeView(ELDMinimapOrientation Mode,const FBox2D& Fit,const FVector2D& FrameSize,double CameraYawDeg,bool bHasBody,double BodyYawDeg,double FitRadius=0.);
    // Frame point (local units, y down) of a world point; only X and Y are read.
    LOCKDOWN_API FVector2D ToFrame(const FView& View,const FVector& World);
    // Ground point (Z = 0) under a frame point: the exact inverse of ToFrame, unclamped.
    LOCKDOWN_API FVector ToWorld(const FView& View,const FVector2D& Local);
    // A gesture's ground point: X and Y clamped inside the registered ground (half-open at the far edges), Z = 0. The
    // map grants no height and no target; a move is still validated against the ground surface below.
    LOCKDOWN_API FVector ClampToGround(const FVector& World,const FBox2D& Ground);
}

UCLASS()
class LOCKDOWN_API ULDMinimap : public UUserWidget
{
    GENERATED_BODY()
public:
    // Same operation is used by pointer capture and deterministic native probes. A pan is a camera operation and
    // stays available while dead; a ground move needs CanWorldCommand. True only when the pan or order was issued.
    // Fraction is of the widget area and goes through the same view as the painted map, then the ground clamp.
    bool Gesture(FVector2D Fraction,bool Move);
    bool IsDragging() const {return bDragging;}
    virtual void NativeTick(const FGeometry& Geometry,float Delta) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
private:
    bool bDragging=false;
    // Owner match/life at drag start: a new life ends the drag so its camera is not overridden by an old gesture.
    uint32 DragMatch=0,DragLife=0;
    // Local size of the last tick or pointer event; a gesture's view is built for it.
    FVector2D FrameSize=FVector2D::ZeroVector;
    // Public static map resolved once per registered world revision and laid out once per view scale; it never holds
    // observations.
    mutable TSharedPtr<FLDMinimapStatic> StaticMap;
    FVector2D Fraction(const FGeometry& Geometry,const FPointerEvent& Event) const;
};
