#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDInventoryPanel.generated.h"
class UTextBlock;class UEditableTextBox;class UUniformGridPanel;class ULDCommandButton;class UBorder;
UCLASS()
class LOCKDOWN_API ULDInventoryPanel:public ULDPanel
{
    GENERATED_BODY()
public:
    void Open();void Close();bool IsOpen() const{return bOpen;}
    void Refresh();void CancelDrag();bool IsDragging() const{return bPointerDown;}
    void Select(FGuid Id);void AskDrop();void ConfirmDrop();
    FString DetailText() const;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual UWidget* FocusScope() const override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& E) override;
    virtual FReply NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E) override;
    virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& E) override;
    virtual void NativeOnFocusLost(const FFocusEvent& E) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E) override;
    virtual FReply NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E) override;
private:
    UPROPERTY() TObjectPtr<class USizeBox> DetailWidth;
    UPROPERTY() TObjectPtr<UTextBlock> Heading;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Detail;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UTextBlock> ConfirmText;
    UPROPERTY() TObjectPtr<UEditableTextBox> Search;
    UPROPERTY() TObjectPtr<UUniformGridPanel> Grid;
    UPROPERTY() TObjectPtr<UBorder> ConfirmPanel;
    UPROPERTY() TObjectPtr<UBorder> MainPanel;
    UPROPERTY() TObjectPtr<ULDCommandButton> DropButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> ConfirmButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> SortButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> MoveButton;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> Cells;
    UFUNCTION() void SearchChanged(const FText& Value);
    int32 HitCell(FVector2D Position) const;
    bool bOpen=false,bConfirm=false,bPointerDown=false,bDragging=false,bDirty=true;
    uint8 Columns=0;
    FGuid Selected,Dragged,DropItem;
    uint32 DropInventoryRevision=0,DropItemRevision=0,SeenInventory=0,SeenLife=0;
    FVector2D DragStart;
    float AppliedScale=1;
};
