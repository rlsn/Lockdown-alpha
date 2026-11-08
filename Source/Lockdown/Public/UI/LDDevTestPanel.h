#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "Development/LDDevTestProtocol.h"
#include "LDDevTestPanel.generated.h"
class UBorder;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
struct FLDCombatOwnerView;
struct FLDDemoContentDefinitions;
struct FLDEquipmentView;
struct FLDProgressionView;

// Pure formatting of the DEV test panel (#78): rows from the approved runtime catalog and the own projections only.
// Nothing here decides a result; every action is one DEV test command the authority admits or refuses.
namespace LDDevPanel
{
    // One catalog entry the panel can take as the current choice: a skill (active or passive) or an attribute card.
    struct FLearnRow
    {
        FName Id;
        FString Name;
        FString Kind;
        bool bActiveSkill=false;
        // The learned rank of a skill (0 = not learned) or how many of a card are held.
        int32 Held=0;
    };
    // Catalog order: skills, then attribute cards.
    LOCKDOWN_API TArray<FLearnRow> LearnRows(const FLDDemoContentDefinitions& Content,const FLDProgressionView* Progression);
    // Q/W/E all hold an active, so a new active needs a confirmed replacement; the server checks it again.
    LOCKDOWN_API bool BothActiveSlotsHeld(const FLDProgressionView* Progression);
    LOCKDOWN_API FString HeldText(const FLearnRow& Row);
    struct FItemRow
    {
        FName Id;
        FString Name;
        int64 Price=0;
        FString Traits;
    };
    LOCKDOWN_API TArray<FItemRow> ItemRows(const FLDDemoContentDefinitions& Content);
    // Level, XP, gold and unused choice opportunities; an unprojected view reads as syncing, never as 0.
    LOCKDOWN_API FString StatusLine(const FLDProgressionView* Progression,const FLDEquipmentView* Equipment);
    // The stat's name in player terms with its entry unit.
    LOCKDOWN_API FString StatLabel(const LDDevTest::FStatField& Field);
    // The committed final value with its sources (the test layer included) from the owner projection only.
    LOCKDOWN_API FString AttributeLine(const FLDCombatOwnerView* Combat,uint8 Attribute);
    // The equipment slot (1 or 2) as the own view shows it.
    LOCKDOWN_API FString SlotLine(const FLDEquipmentView* Equipment,const FLDDemoContentDefinitions* Content,uint8 SlotNumber);
    // The authority's answer in one line: admission refusal, applied, or refused with its reason.
    LOCKDOWN_API FString ResultLine(const FLDDevTestResult& Result);
}

// 测试模式 panel (#78): a normal main panel shown only in a DEV test session. It formats the own projections and the
// approved catalog and sends DEV test commands through the controller's single owning-connection entry; the server
// admits, validates and commits each one exactly like the console commands. Match-wide actions show only to the Host.
UCLASS()
class LOCKDOWN_API ULDDevTestPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    // The controller's SubmitDevTest: the request id, or 0 with the local refusal reason.
    TFunction<uint64(ELDDevTestOp,int64,ELDGrowthReason&,FName,uint8,uint8)> Submit;
    // Called on every HUD refresh with the own projections (nullptr when not projected) and the approved catalog.
    void Present(const FLDProgressionView* Progression,const FLDEquipmentView* Equipment,const FLDCombatOwnerView* Combat,
        const FLDDemoContentDefinitions* Content,bool bHost);
    void HandleResult(const FLDDevTestResult& Result);
    // Sends one command and shows the local answer; the panel's buttons and automation both use it.
    bool Send(ELDDevTestOp Op,int64 Amount=0,FName Definition=NAME_None,uint8 SlotIndex=0,uint8 Stat=0);
    // Reads an amount or a stat value field and sends it; a malformed entry is refused locally and sends nothing.
    bool SendAmount(ELDDevTestOp Op,const FString& Text);
    bool SendStat(int32 FieldIndex,const FString& Text);
    const FString& FeedbackText() const {return Feedback;}
    virtual bool IsPanelOpen() const override {return bOpen;}
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual UWidget* FocusScope() const override;
    virtual UWidget* HitArea() const override;
private:
    UPROPERTY() TObjectPtr<UBorder> MainPanel;
    UPROPERTY() TObjectPtr<UTextBlock> StatusText;
    UPROPERTY() TObjectPtr<UTextBlock> FeedbackLine;
    UPROPERTY() TObjectPtr<UEditableTextBox> XPBox;
    UPROPERTY() TObjectPtr<UEditableTextBox> GoldBox;
    UPROPERTY() TArray<TObjectPtr<UEditableTextBox>> StatBoxes;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> StatLines;
    UPROPERTY() TObjectPtr<UTextBlock> SlotLines[2];
    UPROPERTY() TObjectPtr<UTextBlock> LearnHint;
    UPROPERTY() TObjectPtr<UVerticalBox> LearnList;
    UPROPERTY() TObjectPtr<UVerticalBox> ItemList;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> ItemNames;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> ItemPrices;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> LearnNames;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> LearnHeld;
    UPROPERTY() TArray<TObjectPtr<UWidget>> LearnReplace;
    UPROPERTY() TObjectPtr<UWidget> HostSection;
    TArray<LDDevPanel::FItemRow> ShownItems;
    TArray<LDDevPanel::FLearnRow> ShownLearn;
    bool bCatalogBuilt=false;
    FString Feedback;
    bool bOpen=false;
    void BuildCatalog(const FLDDemoContentDefinitions& Content);
    void ShowFeedback(const FString& Text);
};
