#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LDHUD.generated.h"
class UBorder;
class UCanvasPanelSlot;
class UTextBlock;
class UProgressBar;
class ULDCommandButton;
class ULDMinimap;
UCLASS()
class LOCKDOWN_API ULDHUD : public UUserWidget
{
    GENERATED_BODY()
public:
    void RefreshReadModel();
    bool PointerOverUI() const;
    FString InspectionText() const;
    FString CommandText() const;
    FString TerminalText() const;
    bool CommandLayoutFits() const;
    UPROPERTY() TObjectPtr<ULDMinimap> Minimap;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override;
private:
    friend class ALDEconomyUIProbe;
    UPROPERTY() TObjectPtr<class USizeBox> MapFrame;
    UPROPERTY() TObjectPtr<class USizeBox> InspectFrame;
    UPROPERTY() TObjectPtr<UBorder> Band;
    UPROPERTY() TObjectPtr<UBorder> Task;
    UPROPERTY() TObjectPtr<UBorder> Objective;
    UPROPERTY() TObjectPtr<UBorder> ResultPanel;
    UPROPERTY() TObjectPtr<UBorder> ToastPanel;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> BandSlot;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> FeedbackSlot;
    UPROPERTY() TObjectPtr<UTextBlock> Inspector;
    UPROPERTY() TObjectPtr<UTextBlock> Resources;
    UPROPERTY() TObjectPtr<UTextBlock> ObjectiveText;
    UPROPERTY() TObjectPtr<UTextBlock> TaskText;
    UPROPERTY() TObjectPtr<UTextBlock> SourceText;
    UPROPERTY() TObjectPtr<UTextBlock> ActionText;
    UPROPERTY() TObjectPtr<UTextBlock> Warning;
    UPROPERTY() TObjectPtr<UTextBlock> Toast;
    UPROPERTY() TObjectPtr<UTextBlock> ResultText;
    UPROPERTY() TObjectPtr<UProgressBar> Progress;
    UPROPERTY() TObjectPtr<ULDCommandButton> ContextButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> VendorButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> TradeButton;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> Skills;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> Items;
    UPROPERTY() TMap<FName,TObjectPtr<ULDCommandButton>> Commands;
    UPROPERTY() TArray<TObjectPtr<UBorder>> HitPanels;
    double FeedbackAt=0;
    FString LastFeedback;
    uint32 SeenResult=0;
    uint32 SeenFeedback=0;
    float AppliedScale=0;
    bool IsUIAt(FVector2D Absolute) const;
};
