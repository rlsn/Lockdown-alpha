#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LDMinimap.generated.h"
struct FLDMinimapStatic;
UCLASS()
class LOCKDOWN_API ULDMinimap : public UUserWidget
{
    GENERATED_BODY()
public:
    // Same operation is used by pointer capture and deterministic native probes. A pan is a camera operation and
    // stays available while dead; a ground move needs CanWorldCommand. True only when the pan or order was issued.
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
    // Public static map prepared once per registered world revision and widget size; it never holds observations.
    mutable TSharedPtr<FLDMinimapStatic> StaticMap;
    FVector2D Fraction(const FGeometry& Geometry,const FPointerEvent& Event) const;
};
