#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "UI/LDControlSettings.h"
#include "Styling/SlateTypes.h"
#include "LDControlsPanel.generated.h"
class UBorder;
class UTextBlock;
class UVerticalBox;
class USlider;
class ULDGameInstance;
// The settings page (UI_AND_SETTINGS 8). One category page shows at a time behind named tabs; every row shows its current
// value. Local preferences apply and save at once; VSync and quality stay a display draft until Apply.
UCLASS()
class LOCKDOWN_API ULDControlsPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    // Pages in tab order. Each keeps its own scroll position; switching pages keeps the display draft.
    enum class ECategory : uint8 {Display,Audio,Controls,Camera,Interface};
    static constexpr int32 CategoryCount=5;
    void Open();
    // True only when the panel actually closed; a capture, child confirmation or unapplied draft is handled instead.
    bool Close();
    void ForceClose();
    bool IsOpen() const {return bOpen;}
    ECategory GetCategory() const {return Category;}
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
    ECategory Category=ECategory::Display;
    // The page whose defaults the open reset confirmation restores; switching pages dismisses that confirmation.
    ECategory ResetCategory=ECategory::Display;
    FName Capture;
    // The binding row that last ended a capture keeps its result line (changed, unchanged or refused) until the next capture.
    FName LastCapture;
    FString CaptureResult;
    bool bCaptureProblem=false;
    EChildConfirmation Child=EChildConfirmation::None;
    // A control to focus once a newly shown confirmation has been arranged (after the frame that showed it).
    FName PendingFocus;
    uint64 PendingFocusFrame=0;
    // A binding row whose result line just changed; it is scrolled fully into view again once that line has been laid out.
    UPROPERTY() TObjectPtr<UWidget> PendingRevealRow;
    uint64 PendingRevealFrame=0;
    // The footer line keeps a problem until it is answered; a plain result gives way to the page hint on a page switch.
    bool bFeedbackProblem=false;
    // The footer problem came from the display page (a failed display reset); the apply bar's next success answers it.
    bool bFeedbackFromDisplay=false;
    // Shared button face of the native widgets; chosen tabs, options and capturing keys are drawn from it.
    FButtonStyle RestStyle;
    UPROPERTY() TArray<TObjectPtr<UWidget>> Pages;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UTextBlock> MatchNote;
    UPROPERTY() TObjectPtr<UWidget> ConfirmCard;
    UPROPERTY() TObjectPtr<UVerticalBox> ResetRows;
    UPROPERTY() TObjectPtr<UTextBlock> ResetPrompt;
    UPROPERTY() TObjectPtr<UVerticalBox> RecoveryRows;
    UPROPERTY() TObjectPtr<UVerticalBox> CloseRows;
    UPROPERTY() TObjectPtr<UTextBlock> CloseDraftPrompt;
    UPROPERTY() TObjectPtr<UVerticalBox> DisplayRows;
    UPROPERTY() TObjectPtr<UTextBlock> DisplayUnavailable;
    UPROPERTY() TObjectPtr<UBorder> DisplayBar;
    UPROPERTY() TObjectPtr<UTextBlock> DisplayFeedback;
    UPROPERTY() TObjectPtr<UTextBlock> VSyncNote;
    UPROPERTY() TObjectPtr<UTextBlock> QualityNote;
    UPROPERTY() TObjectPtr<USlider> VolumeSlider;
    UPROPERTY() TObjectPtr<UTextBlock> VolumeValue;
    UPROPERTY() TObjectPtr<USlider> MusicSlider;
    UPROPERTY() TObjectPtr<UTextBlock> MusicValue;
    UPROPERTY() TObjectPtr<UTextBlock> ZoomValue;
    UPROPERTY() TObjectPtr<UTextBlock> FollowNote;
    // Built once in ULDControlSettings::Definitions() order, so a rebind keeps focus and every name stays stable.
    UPROPERTY() TArray<TObjectPtr<UButton>> BindingKeys;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> BindingNotes;
    TWeakObjectPtr<ULDGameInstance> SettingsInstance;
    FLDDisplaySettings DisplayDraft;
    // Display values the engine verifiably took: read while no apply had failed, or the last successful apply or retry.
    // The draft is compared with these, never with settings fields that a failed apply has already written.
    FLDDisplaySettings AppliedDisplay;
    bool bAppliedDisplayKnown = false;
    bool bDisplayAvailable = false;
    // The draft differs from the verified display values, or a failed apply is still unresolved; Apply, Revert and the
    // close prompt follow it.
    bool bDisplayDraftDirty = false;
    // The instance refused this exact draft; applying it again would only repeat the failure, so Apply waits for an edit.
    bool bDisplayDraftRejected = false;
    // The latest display result worth showing in the apply bar; empty while the bar describes the draft itself.
    FString DisplayMessage;
    bool bDisplayMessageProblem = false;
    // Mouse and controller captures are tracked apart: grabbing the handle with the mouse ends a controller capture first.
    bool bVolumeMouseCapture=false;
    bool bVolumeControllerCapture=false;
    // USlider::SetValue broadcasts OnValueChanged; syncing the slider from the applied value must not apply it again.
    bool bSyncingVolume=false;
    // A volume change waits for the instance's debounced flush; its real result is shown once the flush has run.
    bool bVolumeSavePending=false;
    // The instance's debounced save (volume, zoom or follow) as last seen, so its end is noticed whoever requested it.
    bool bSeenFlushPending=false;
    // The save actions the footer currently offers; any change in the settings state that alters them refreshes it.
    bool bShownRetrySave=false;
    bool bShownRecover=false;
    ELDDisplaySettingsStatus SeenDisplayStatus = ELDDisplaySettingsStatus::Unavailable;
    void ShowCategory(ECategory Next);
    // True when Point (screen space) is over a shown category tab; a tab click during a key capture switches pages instead.
    bool IsCategoryTabAt(const FVector2D& Point) const;
    void Populate();
    void PopulatePreferences();
    void PopulateBindings();
    void PopulateDisplay();
    void PopulateFooter();
    void FooterActions(bool& bRetrySave, bool& bRecover) const;
    void SetFeedback(const FString& Message, bool bProblem);
    void ShowPageHint();
    void RefreshDisplayDraft();
    void EditDisplayDraft();
    // A failed apply wrote its request into the settings fields without the engine taking it; until an apply or a retry
    // confirms the live values again, the display page keeps an unapplied change.
    bool DisplayApplyUnresolved() const;
    // After a retry (here or elsewhere) confirms the live values, they become the verified values the draft is compared with.
    void AdoptVerifiedDisplay();
    void StepQuality(int32 Direction);
    void SetDisplayMessage(const FLDDisplaySettingsResult& Result);
    void ChangePreference(TFunction<void(ULDControlSettings&)> Change);
    void ShowChild(EChildConfirmation Which);
    void AskReset();
    void ShowRecoveryConfirmation();
    void BeginCapture(FName Action);
    void EndCapture();
    // True when the draft was applied (saved, or only its save failed); a refusal is shown and disables Apply until an edit.
    bool ApplyDisplay(FString& Message);
    void ApplyDisplay();
    // A refusal keeps the close prompt open with the failure and focuses "discard and return".
    void ApplyDisplayAndReturn();
    // Restore defaults (UI_AND_SETTINGS 8) for the page that asked: a preference group applies and saves at once; the
    // display page makes the product defaults the draft and applies it through the normal display apply.
    void ConfirmResetDefaults();
    void RestoreDisplayDefaults(ULDGameInstance& Instance);
    FReply CaptureKey(FKey Key);
    void PreviewMasterVolume();
    void EndVolumeCapture(bool& bCaptured);
    UFUNCTION() void HandleMasterVolumeChanged(float Value);
    // The music itself is the preview: the slider changes the playing track's level, so no click sounds.
    UFUNCTION() void HandleMusicVolumeChanged(float Value);
    UFUNCTION() void HandleVolumeMouseCaptureBegin();
    UFUNCTION() void HandleVolumeMouseCaptureEnd();
    UFUNCTION() void HandleVolumeControllerCaptureBegin();
    UFUNCTION() void HandleVolumeControllerCaptureEnd();
};
