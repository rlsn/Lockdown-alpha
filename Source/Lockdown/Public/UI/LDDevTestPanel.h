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
struct FLDProgressionView;

// Pure formatting of the DEV test panel (#78): rows from the approved runtime catalog and the own projections only.
// Nothing here decides a result; every action is one DEV test command the authority admits or refuses.
namespace LDDevPanel
{
    // What the own build holds of one catalog active (#198).
    enum class EActiveHeld : uint8 { None, Blueprint, Learned };
    struct FActiveRow
    {
        FName Id;
        FString Name;
        EActiveHeld Held=EActiveHeld::None;
        // Learned: its slot, 0 = Q, 1 = W, 2 = E.
        uint8 Slot=0;
    };
    // Every active of the catalog in catalog order, with what the own progression holds of it.
    LOCKDOWN_API TArray<FActiveRow> ActiveRows(const FLDDemoContentDefinitions& Content,const FLDProgressionView* Progression);
    // Q/W/E all hold an active, so a DEV learn names the slot whose active it forgets; the server checks it again.
    LOCKDOWN_API bool AllActiveSlotsHeld(const FLDProgressionView* Progression);
    LOCKDOWN_API FString HeldText(const FActiveRow& Row);
    // Level, XP, unspent points and queued active opportunities; an unprojected view reads as syncing, never as 0.
    LOCKDOWN_API FString StatusLine(const FLDProgressionView* Progression);
    // The stat's name in player terms with its entry unit.
    LOCKDOWN_API FString StatLabel(const LDDevTest::FStatField& Field);
    // The committed final value with its sources (the test layer included) from the owner projection only.
    LOCKDOWN_API FString AttributeLine(const FLDCombatOwnerView* Combat,uint8 Attribute);
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
    void Present(const FLDProgressionView* Progression,const FLDCombatOwnerView* Combat,const FLDDemoContentDefinitions* Content,bool bHost);
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
    UPROPERTY() TArray<TObjectPtr<UEditableTextBox>> StatBoxes;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> StatLines;
    UPROPERTY() TObjectPtr<UTextBlock> LearnHint;
    UPROPERTY() TObjectPtr<UVerticalBox> LearnList;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> LearnNames;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> LearnHeld;
    UPROPERTY() TArray<TObjectPtr<UWidget>> LearnGrant;
    UPROPERTY() TArray<TObjectPtr<UWidget>> LearnNow;
    UPROPERTY() TArray<TObjectPtr<UWidget>> LearnReplace;
    UPROPERTY() TObjectPtr<UVerticalBox> PointList;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> PointNames;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> PointHeld;
    UPROPERTY() TObjectPtr<UWidget> HostSection;
    TArray<LDDevPanel::FActiveRow> ShownActives;
    TArray<FName> ShownStats;
    int64 ShownUnspent=0;
    bool bCatalogBuilt=false;
    FString Feedback;
    bool bOpen=false;
    void BuildCatalog(const FLDDemoContentDefinitions& Content);
    void ShowFeedback(const FString& Text);
};
