#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LDMinimap.generated.h"
UCLASS()
class LOCKDOWN_API ULDMinimap : public UUserWidget
{
    GENERATED_BODY()
public:
    // Same operation is used by pointer capture and deterministic native probes.
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
    FVector2D Fraction(const FGeometry& Geometry,const FPointerEvent& Event) const;
};
