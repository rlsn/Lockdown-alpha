#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "UI/LDControlSettings.h"
#include "LDControlsPanel.generated.h"
class UTextBlock;
class UVerticalBox;
class ULDGameInstance;
UCLASS()
class LOCKDOWN_API ULDControlsPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    void Open();
    void Close();
    void ForceClose();
    bool IsOpen() const {return bOpen;}
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
private:
    bool bOpen=false;
    float AppliedTextScale=1;
    FName Capture;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UTextBlock> Preferences;
    UPROPERTY() TObjectPtr<UVerticalBox> Rows;
    UPROPERTY() TObjectPtr<UVerticalBox> DisplayRows;
    UPROPERTY() TObjectPtr<UVerticalBox> TrialRows;
    UPROPERTY() TObjectPtr<UVerticalBox> RecoveryRows;
    UPROPERTY() TObjectPtr<UVerticalBox> CloseRows;
    UPROPERTY() TObjectPtr<UTextBlock> DisplayFeedback;
    TWeakObjectPtr<ULDGameInstance> SettingsInstance;
    FLDDisplaySettings DisplayDraft;
    TArray<FIntPoint> Resolutions;
    uint64 TrialId = 0;
    bool bDisplayAvailable = false;
    bool bDisplayDraftDirty = false;
    bool bCloseAfterConfirm = false;
    ELDDisplaySettingsStatus SeenDisplayStatus = ELDDisplaySettingsStatus::Unavailable;
    void Populate();
    UTextBlock* ScaledText(const FString& Text, int32 Size=16);
    ULDCommandButton* ScaledButton(const FString& Text, TFunction<void()> Action);
    void PopulateDisplay();
    void RefreshDisplayDraft();
    void ChangePreference(TFunction<void(ULDControlSettings&)> Change);
    void ShowRecoveryConfirmation();
    void ShowCloseConfirmation();
    void ApplyDisplay();
    void ConfirmDisplay(uint64 Id);
    void CancelDisplay(uint64 Id);
    FReply CaptureKey(FKey Key);
};
