#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "LDAttributesPanel.generated.h"
class UBorder;
class UTextBlock;
struct FLDCombatOwnerView;
struct FLDProgressionView;
struct FLDDemoContentDefinitions;

// Own attribute panel (APP_SPEC 3.2, UI 3.2 attributes page): a normal main panel over the Combat owner projection.
// The projection is computed once natively; this panel only formats it and never recomputes a value, and an unknown
// or unsynced value shows the syncing text, never 0. Rows are built once, so owner refreshes keep scroll and focus.
UCLASS()
class LOCKDOWN_API ULDAttributesPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    // Called on every HUD refresh. Combat is the owner projection only when it belongs to the current match and life
    // (nullptr otherwise); Progression supplies the allocated stat points (nullptr when not projected).
    void Present(const FLDCombatOwnerView* Combat,const FLDProgressionView* Progression,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* Definitions);
    virtual bool IsPanelOpen() const override;
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual UWidget* FocusScope() const override;
    virtual UWidget* HitArea() const override;
private:
    UPROPERTY() TObjectPtr<UBorder> MainPanel;
    // Indexed by ELDProjectedAttribute, never by the position of a published row.
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> RowFinals;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> RowBreakdowns;
    UPROPERTY() TObjectPtr<UTextBlock> StatPointsText;
    UPROPERTY() TObjectPtr<UTextBlock> StatusText;
    bool bOpen=false;
};
