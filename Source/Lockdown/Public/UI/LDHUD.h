#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LDHUD.generated.h"
class UBorder;
class UCanvasPanelSlot;
class UTextBlock;
class ULDCommandButton;
class ULDMinimap;
class FLDDemoContentSnapshot;
struct FLDDemoContentDefinitions;
UCLASS()
class LOCKDOWN_API ULDHUD : public UUserWidget
{
    GENERATED_BODY()
public:
    void RefreshReadModel();
    bool PointerOverUI() const;
    FString InspectionText() const;
    bool CommandLayoutFits() const;
    UPROPERTY() TObjectPtr<ULDMinimap> Minimap;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override;
private:
    UPROPERTY() TObjectPtr<class USizeBox> MapFrame;
    UPROPERTY() TObjectPtr<class USizeBox> InspectFrame;
    UPROPERTY() TObjectPtr<UBorder> Band;
    UPROPERTY() TObjectPtr<UBorder> ToastPanel;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> BandSlot;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> FeedbackSlot;
    UPROPERTY() TObjectPtr<UTextBlock> Inspector;
    UPROPERTY() TObjectPtr<UTextBlock> Resources;
    UPROPERTY() TObjectPtr<UTextBlock> SourceText;
    UPROPERTY() TObjectPtr<UTextBlock> ActionText;
    UPROPERTY() TObjectPtr<UTextBlock> Toast;
    UPROPERTY() TMap<FName,TObjectPtr<ULDCommandButton>> Commands;
    UPROPERTY() TArray<TObjectPtr<UBorder>> HitPanels;
    double FeedbackAt=0;
    FString LastFeedback;
    uint32 SeenFeedback=0;
    float AppliedScale=0;
    // Approved public definitions used only to label observed units; loaded once per widget.
    TSharedPtr<const FLDDemoContentSnapshot> ObservedContent;
    bool bObservedContentTried=false;
    const FLDDemoContentDefinitions* ObservedDefinitions();
    bool IsUIAt(FVector2D Absolute) const;
};
