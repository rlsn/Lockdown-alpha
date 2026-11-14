#pragma once
#include "CoreMinimal.h"
#include "Components/Button.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Audio/LDAudioTypes.h"
#include "UI/LDTween.h"
#include "UI/LDUIFonts.h"
#include "UI/LDUIPaint.h"
#include "LDWidgets.generated.h"
class UWidgetTree;
class UTextBlock;class UBorder;class URichTextBlock;class UScaleBox;
UCLASS()
class LOCKDOWN_API ULDCommandButton : public UButton
{
    GENERATED_BODY()
public:
    TFunction<void()> Callback;
    TFunction<void()> PressCallback,ReleaseCallback;
    UFUNCTION() void Execute(){if(Callback){Callback();}}
    UFUNCTION() void Press(){if(PressCallback){PressCallback();}}
    UFUNCTION() void Release(){if(ReleaseCallback){ReleaseCallback();}}
    // LDWidgets buttons answer hover and press with a small scale (LDHUDStyle motion) and the interface hover and click cues.
    // The motion is presentation only: the command runs on the click itself, never after an animation.
    void EnableFeel();
    UFUNCTION() void FeelHovered();
    UFUNCTION() void FeelUnhovered();
    UFUNCTION() void FeelPressed();
    UFUNCTION() void FeelReleased();
    UFUNCTION() void FeelClicked(){PlayCue(ELDAudioCue::UIClick);}
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    // The painted face under the caption (LDUIPaint::Button). A button given its own style brushes keeps them instead.
    LDUIPaint::EButton Look=LDUIPaint::EButton::Normal;
    // A card's state colour (transparent while idle).
    FLinearColor Tint=FLinearColor::Transparent;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    void PlayCue(ELDAudioCue Cue) const;
    void Animate(float Target,double Duration);
    void StopTicker(FTSTicker::FDelegateHandle& Handle);
    LDTween::FTrack Scale{1.f,1.f};
    FTSTicker::FDelegateHandle FeelTicker;
};
// A layer that paints through a callback in its own geometry (a canvas fills it); placed under a panel's widgets, it draws
// their surfaces, frames and tiles with LDUIPaint. It takes no input.
UCLASS()
class LOCKDOWN_API ULDPaintLayer : public UWidget
{
    GENERATED_BODY()
public:
    TFunction<void(FSlateWindowElementList&,int32,const FGeometry&)> Painter;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    TSharedPtr<SWidget> Layer;
};
// Irreversible confirmations (D5): pressing starts a fill across the face, releasing early cancels, and a full fill after
// LDHUDStyle::MotionHoldConfirm runs Callback once. Enter and Space hold the focused button the same way.
UCLASS()
class LOCKDOWN_API ULDHoldButton : public ULDCommandButton
{
    GENERATED_BODY()
public:
    // 0 while idle, rising to 1 while held; ULDPanel paints it over the face.
    float HoldProgress() const;
    UFUNCTION() void HoldPressed();
    UFUNCTION() void HoldReleased();
    UFUNCTION() void InstantClicked();
    // A confirmation whose current action is reversible commits on a plain click instead.
    void SetHoldRequired(bool bRequired);
    bool IsHoldRequired() const {return bHoldRequired;}
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
private:
    void Repaint() const;
    bool IsShownAndEnabled() const;
    double HoldStartedAt=-1.;
    bool bHoldRequired=true;
    FTSTicker::FDelegateHandle HoldTicker;
};
UCLASS()
class LOCKDOWN_API ULDWorldButton : public ULDCommandButton
{
    GENERATED_BODY()
public:
    ULDWorldButton(){InitIsFocusable(false);}
};
// Shared major-panel focus scope. World buttons deliberately remain unfocusable.
// The stack hooks let ULDUIRoot route Back over panels it does not own (UI_AND_SETTINGS 4):
// a child confirmation closes before its main panel, and closing never submits or rolls back domain work.
UCLASS()
class LOCKDOWN_API ULDPanel : public UUserWidget
{
    GENERATED_BODY()
public:
    bool MovePanelFocus(bool Backward=false);
    TArray<UWidget*> PanelFocusTargets() const;
    virtual bool IsPanelOpen() const {return false;}
    virtual void OpenPanel() {}
    // Returns false when the panel keeps itself open (e.g. it showed an unapplied-draft confirmation instead).
    virtual bool ClosePanel() {return false;}
    virtual bool HasChildConfirmation() const {return false;}
    virtual bool CloseChildConfirmation() {return false;}
    // A child confirmation that waits without capturing input: play goes on meanwhile and Back still closes it (after an
    // armed skill) (ULDUIRoot::YieldingConfirmation).
    virtual bool ChildConfirmationYields() const {return false;}
    // Normal panels take pointer input only over their own area, so world clicks beside them still reach the world.
    bool IsPanelHit(FVector2D Absolute) const;
    // Local text-size preference (UI 3.3): rescales the built tree once per change; text built later uses ScaleNewText.
    void ApplyTextScale(float Scale);
protected:
    virtual UWidget* FocusScope() const;
    virtual UWidget* HitArea() const {return FocusScope();}
    void ScaleNewText(UTextBlock* Label) const;
    float AppliedTextScale=1.f;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E) override;
    virtual int32 NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const override;
};
namespace LDWidgets
{
    // Withdraws every hold in progress in Tree, so a closed or left page needs a fresh full hold.
    LOCKDOWN_API void ReleaseHolds(UWidgetTree* Tree);
    // The painted face a command button wears (the dark raised face by default) and the caption colour that goes with it.
    LOCKDOWN_API void SetLook(ULDCommandButton* Button,LDUIPaint::EButton Look);
    LOCKDOWN_API UBorder* Panel(UWidgetTree* Tree,UWidget* Child,FMargin Padding=FMargin(20));
    // Role picks the interface face (LDUIFonts); every runtime text takes its font through a role.
    LOCKDOWN_API UTextBlock* Text(UWidgetTree* Tree,const FString& Value,int32 Size=16,FName Name=NAME_None,LDUIFonts::ERole Role=LDUIFonts::ERole::Body);
    // Name gives automation a stable handle that survives copy changes; NAME_None keeps a generated name.
    LOCKDOWN_API ULDCommandButton* Button(UWidgetTree* Tree,const FString& Value,TFunction<void()> Callback,bool World=false,FName Name=NAME_None);
    // The same face as Button, committing only after a full hold (ULDHoldButton).
    LOCKDOWN_API ULDHoldButton* HoldButton(UWidgetTree* Tree,const FString& Value,TFunction<void()> Callback,FName Name=NAME_None);
    // Wrapping rich text in the Text look whose <b>…</> runs are bold and accented (#147 card keywords).
    LOCKDOWN_API URichTextBlock* RichText(UWidgetTree* Tree,int32 Size=16,FName Name=NAME_None);
    // Rebuilds Rich's two styles at Size (the text-size preference).
    LOCKDOWN_API void SetRichTextSize(URichTextBlock* Rich,int32 Size);
    LOCKDOWN_API int32 RichTextSize(const URichTextBlock* Rich);
    // A clipped box that scales its content down (never up, never below 90%) to fit; RefitScale applies it each tick.
    LOCKDOWN_API UScaleBox* Fit(UWidgetTree* Tree,UWidget* Content);
    LOCKDOWN_API void RefitScale(UScaleBox* Fit);
    // Plays an interface cue through the game instance's audio subsystem (quietly nothing before it is ready).
    LOCKDOWN_API void PlayCue(const UObject* Context,ELDAudioCue Cue);
}
