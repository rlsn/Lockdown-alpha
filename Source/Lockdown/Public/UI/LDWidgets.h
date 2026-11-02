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
protected:
    virtual UWidget* FocusScope() const;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E) override;
    virtual int32 NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const override;
};
namespace LDWidgets
{
    LOCKDOWN_API UBorder* Panel(UWidgetTree* Tree,UWidget* Child,FMargin Padding=FMargin(20));
    LOCKDOWN_API UTextBlock* Text(UWidgetTree* Tree,const FString& Value,int32 Size=16);
    // Name gives automation a stable handle that survives copy changes; NAME_None keeps a generated name.
    LOCKDOWN_API ULDCommandButton* Button(UWidgetTree* Tree,const FString& Value,TFunction<void()> Callback,bool World=false,FName Name=NAME_None);
}
