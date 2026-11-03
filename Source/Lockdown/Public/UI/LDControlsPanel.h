#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "UI/LDControlSettings.h"
#include "LDControlsPanel.generated.h"
class UTextBlock;
class UVerticalBox;
class USlider;
class ULDGameInstance;
UCLASS()
class LOCKDOWN_API ULDControlsPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    void Open();
    // True only when the panel actually closed; a capture, trial, child confirmation or unapplied draft is handled instead.
    bool Close();
    void ForceClose();
    bool IsOpen() const {return bOpen;}
    virtual bool IsPanelOpen() const override {return bOpen;}
    virtual void OpenPanel() override {Open();}
    virtual bool ClosePanel() override {return Close();}
    virtual bool HasChildConfirmation() const override;
    virtual bool CloseChildConfirmation() override;
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
    enum class EChildConfirmation : uint8 {None,LocalReset,Recovery,CloseDraft};
    bool bOpen=false;
    float AppliedTextScale=1;
    FName Capture;
    EChildConfirmation Child=EChildConfirmation::None;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UTextBlock> Preferences;
    UPROPERTY() TObjectPtr<UVerticalBox> Rows;
    UPROPERTY() TObjectPtr<UVerticalBox> DisplayRows;
    UPROPERTY() TObjectPtr<UVerticalBox> TrialRows;
    UPROPERTY() TObjectPtr<UVerticalBox> ResetRows;
    UPROPERTY() TObjectPtr<UVerticalBox> RecoveryRows;
    UPROPERTY() TObjectPtr<UVerticalBox> CloseRows;
    UPROPERTY() TObjectPtr<UTextBlock> DisplayFeedback;
    UPROPERTY() TObjectPtr<USlider> VolumeSlider;
    UPROPERTY() TObjectPtr<UTextBlock> VolumeLabel;
    UPROPERTY() TObjectPtr<UTextBlock> PanSpeedLabel;
    UPROPERTY() TObjectPtr<UTextBlock> OrientationLabel;
    // Resting fill of an unchosen minimap orientation button, taken from the shared button style when it is built.
    FSlateColor ChoiceRestTint;
    TWeakObjectPtr<ULDGameInstance> SettingsInstance;
    FLDDisplaySettings DisplayDraft;
    TArray<FIntPoint> Resolutions;
    uint64 TrialId = 0;
    bool bDisplayAvailable = false;
    bool bDisplayDraftDirty = false;
    bool bCloseAfterConfirm = false;
    // Mouse and controller captures are tracked apart: grabbing the handle with the mouse ends a controller capture first.
    bool bVolumeMouseCapture=false;
    bool bVolumeControllerCapture=false;
    // USlider::SetValue broadcasts OnValueChanged; syncing the slider from the applied value must not apply it again.
    bool bSyncingVolume=false;
    // A volume change waits for the instance's debounced flush; its real result is shown once the flush has run.
    bool bVolumeSavePending=false;
    ELDDisplaySettingsStatus SeenDisplayStatus = ELDDisplaySettingsStatus::Unavailable;
    void Populate();
    void PopulatePreferences();
    void PopulateDisplay();
    void RefreshDisplayDraft();
    void ChangePreference(TFunction<void(ULDControlSettings&)> Change);
    void ShowChild(EChildConfirmation Which);
    void ShowRecoveryConfirmation();
    void EndCapture();
    void ApplyDisplay();
    void ConfirmDisplay(uint64 Id);
    void CancelDisplay(uint64 Id);
    // Restore defaults (UI_AND_SETTINGS 8): the local preferences apply at once, then the display draft becomes the product
    // defaults and goes through the normal display apply, so a mode change still needs the keep/revert trial.
    void ConfirmResetDefaults();
    void RestoreDisplayDefaults(ULDGameInstance& Instance);
    FReply CaptureKey(FKey Key);
    void PreviewMasterVolume();
    void EndVolumeCapture(bool& bCaptured);
    UFUNCTION() void HandleMasterVolumeChanged(float Value);
    UFUNCTION() void HandleVolumeMouseCaptureBegin();
    UFUNCTION() void HandleVolumeMouseCaptureEnd();
    UFUNCTION() void HandleVolumeControllerCaptureBegin();
    UFUNCTION() void HandleVolumeControllerCaptureEnd();
};
