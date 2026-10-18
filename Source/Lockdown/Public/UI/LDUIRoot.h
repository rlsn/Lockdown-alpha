#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LDUIRoot.generated.h"

class UBorder;
class UTextBlock;
class UEditableTextBox;
UCLASS()
class LOCKDOWN_API ULDUIRoot : public UUserWidget
{
    GENERATED_BODY()
public:
    void ToggleMenu();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    bool bMenuOpen = false;
    UPROPERTY() TObjectPtr<UBorder> MenuPanel;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UEditableTextBox> Address;
    UFUNCTION() void Host();
    UFUNCTION() void Join();
    UFUNCTION() void Resume();
    UFUNCTION() void Leave();
    UFUNCTION() void Exit();
};
