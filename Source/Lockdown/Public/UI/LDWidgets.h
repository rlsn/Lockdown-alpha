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
class UTextBlock;class UBorder;class UScaleBox;
UCLASS()
class LOCKDOWN_API ULDCommandButton : public UButton
{
    GENERATED_BODY()
public:
    TFunction<void()> Callback;
    TFunction<void()> PressCallback,ReleaseCallback;
    // A click runs Callback; the click that ends a hold which already repeated runs nothing more.
    UFUNCTION() void Execute(){if(bRepeated){bRepeated=false;return;}if(Callback){Callback();}}
    UFUNCTION() void Press(){if(PressCallback){PressCallback();}}
    UFUNCTION() void Release(){if(ReleaseCallback){ReleaseCallback();}}
    // LDWidgets buttons answer hover and press with a small scale (LDHUDStyle motion) and the interface hover and click cues.
    // The motion is presentation only: the command runs on the click itself, never after an animation.
    void EnableFeel();
    // Hold to repeat: after a short delay the button runs Again at a steady interval while it stays pressed and enabled, and
    // stops for this hold once Again returns false. A short click still runs Callback once.
    void EnableRepeat(TFunction<bool()> Again);
    UFUNCTION() void RepeatPressed();
    UFUNCTION() void RepeatReleased();
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
    TFunction<bool()> RepeatCallback;
    FTSTicker::FDelegateHandle RepeatTicker;
    double RepeatNextAt=0.;
    bool bRepeated=false;
};
// A layer that paints through a callback in its own geometry (a canvas fills it); placed under a panel's widgets, it draws
// their surfaces, frames and tiles with LDUIPaint. It takes no input.
UCLASS()
class LOCKDOWN_API ULDPaintLayer : public UWidget
{
    GENERATED_BODY()
public:
    TFunction<void(const LDUIPaint::FContext&)> Painter;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    TSharedPtr<SWidget> Layer;
};
UCLASS()
class LOCKDOWN_API ULDWorldButton : public ULDCommandButton
{
    GENERATED_BODY()
public:
    ULDWorldButton(){InitIsFocusable(false);}
};
// Shared major-panel focus scope. World buttons deliberately remain unfocusable.
// The stack hooks let ULDUIRoot route Back over panels it does not own (MOVEMENT_INPUT_AND_CAMERA input ownership):
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
    // Local text-size preference (VISION_AND_UI readability): rescales the built tree once per change; text built later uses ScaleNewText.
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
    // The painted face a command button wears (the dark raised face by default) and the caption colour that goes with it.
    LOCKDOWN_API void SetLook(ULDCommandButton* Button,LDUIPaint::EButton Look);
    LOCKDOWN_API UBorder* Panel(UWidgetTree* Tree,UWidget* Child,FMargin Padding=FMargin(20));
    // Role picks the interface face (LDUIFonts); every runtime text takes its font through a role.
    LOCKDOWN_API UTextBlock* Text(UWidgetTree* Tree,const FString& Value,int32 Size=16,FName Name=NAME_None,LDUIFonts::ERole Role=LDUIFonts::ERole::Body);
    // Name gives automation a stable handle that survives copy changes; NAME_None keeps a generated name.
    LOCKDOWN_API ULDCommandButton* Button(UWidgetTree* Tree,const FString& Value,TFunction<void()> Callback,bool World=false,FName Name=NAME_None);
    // Native ScaleToFit normalizes measurement before scaling; never enlarge the content.
    LOCKDOWN_API UScaleBox* Fit(UWidgetTree* Tree,UWidget* Content);
    // Plays an interface cue through the game instance's audio subsystem (quietly nothing before it is ready).
    LOCKDOWN_API void PlayCue(const UObject* Context,ELDAudioCue Cue);
}
