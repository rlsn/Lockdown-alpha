#pragma once
#include "CoreMinimal.h"
#include "Components/Button.h"
#include "Blueprint/UserWidget.h"
#include "LDWidgets.generated.h"
class UWidgetTree;
class UTextBlock;class UBorder;
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
    // A child confirmation that gives way instead of capturing input: Back still closes it first, but any other world
    // input cancels it and then acts as usual (ULDUIRoot::YieldingConfirmation).
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
    LOCKDOWN_API UBorder* Panel(UWidgetTree* Tree,UWidget* Child,FMargin Padding=FMargin(20));
    LOCKDOWN_API UTextBlock* Text(UWidgetTree* Tree,const FString& Value,int32 Size=16,FName Name=NAME_None);
    // Name gives automation a stable handle that survives copy changes; NAME_None keeps a generated name.
    LOCKDOWN_API ULDCommandButton* Button(UWidgetTree* Tree,const FString& Value,TFunction<void()> Callback,bool World=false,FName Name=NAME_None);
}
