#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LDControlsPanel.generated.h"
class UTextBlock;
class UVerticalBox;
UCLASS()
class LOCKDOWN_API ULDControlsPanel : public UUserWidget
{
    GENERATED_BODY()
public:
    void Open();
    void Close();
    bool IsOpen() const {return bOpen;}
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
private:
    bool bOpen=false;
    FName Capture;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UTextBlock> Preferences;
    UPROPERTY() TObjectPtr<UVerticalBox> Rows;
    void Populate();
    void SavePreferences();
    FReply CaptureKey(FKey Key);
};
