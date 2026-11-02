#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "LDInventoryPanel.generated.h"
class UTextBlock;class UBorder;
// Read-only equipment page over the owner Growth view (gold and the two real equipment slots). The shop page that
// replaces it is owned by the UI module; this panel never writes gameplay state.
UCLASS()
class LOCKDOWN_API ULDInventoryPanel:public ULDPanel
{
    GENERATED_BODY()
public:
    void Open();void Close();bool IsOpen() const{return bOpen;}
    void Refresh();void CancelDrag();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual UWidget* FocusScope() const override;
    virtual FReply NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E) override;
private:
    UPROPERTY() TObjectPtr<UTextBlock> Heading;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Slots;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UBorder> MainPanel;
    bool bOpen=false;
};