#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "Components/Button.h"
#include "Development/LDCharacterBuild.h"
#include "LDDevelopmentPanel.generated.h"
class ULDDevelopmentPanel;
class UTextBlock;

UCLASS()
class LOCKDOWN_API ULDDevelopmentButton : public ULDCommandButton
{
    GENERATED_BODY()
public:
    ULDDevelopmentButton();
    UPROPERTY() TObjectPtr<ULDDevelopmentPanel> Panel;
    uint8 Operation=0,Index=0;
    UFUNCTION() void Route();
};

UCLASS()
class LOCKDOWN_API ULDDevelopmentPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    void Open();
    void Close();
    bool IsOpen() const {return bOpen;}
    void Choose(uint8 Operation,uint8 Index);
    // UI and deterministic UI probes use the same preview/arming functions.
    const FLDBuildPlan& Preview() const {return Draft;}
    void StartConfirm();
    void StopConfirm();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float Delta) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual void NativeOnFocusLost(const FFocusEvent& Event) override;
private:
    bool bOpen=false,bHolding=false;
    float AppliedTextScale=1;
    uint8 SelectedSlot=0,Armed=0;
    FGuid SelectedBook;
    FLDBuildPlan Draft;
    UPROPERTY() TObjectPtr<UTextBlock> Heading;
    UPROPERTY() TObjectPtr<UTextBlock> PreviewInfo;
    UPROPERTY() TObjectPtr<UTextBlock> HoldInfo;
    UPROPERTY() TObjectPtr<UTextBlock> BookInfo;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> AttributeLabels;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SkillLabels;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SlotLabels;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> BookLabels;
    UPROPERTY() TArray<TObjectPtr<ULDDevelopmentButton>> RankButtons;
    UPROPERTY() TArray<TObjectPtr<ULDDevelopmentButton>> EquipButtons;
    UPROPERTY() TArray<TObjectPtr<ULDDevelopmentButton>> ReadButtons;
    UPROPERTY() TArray<TObjectPtr<ULDDevelopmentButton>> BuyButtons;
    UPROPERTY() TArray<TObjectPtr<ULDDevelopmentButton>> SubclassButtons;
    UPROPERTY() TObjectPtr<ULDDevelopmentButton> ApplyButton;
    void ResetDraft();
};
