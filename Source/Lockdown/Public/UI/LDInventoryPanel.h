#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDInventoryPanel.generated.h"
class UTextBlock;class UEditableTextBox;class UUniformGridPanel;class ULDCommandButton;class UBorder;
UCLASS()
class LOCKDOWN_API ULDInventoryPanel:public UUserWidget
{
    GENERATED_BODY()
public:
    void Open();void Close();bool IsOpen() const{return bOpen;}
    void Refresh();void CancelDrag();bool IsDragging() const{return bPointerDown;}
    void Select(FGuid Id);void Use();void AskDrop();void ConfirmDrop();
    FString DetailText() const;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& E) override;
    virtual FReply NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E) override;
    virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& E) override;
    virtual void NativeOnFocusLost(const FFocusEvent& E) override;
    virtual FReply NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E) override;
private:
    friend class ALDInventoryProbe;
    UPROPERTY() TObjectPtr<UTextBlock> Heading;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Detail;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UTextBlock> ConfirmText;
    UPROPERTY() TObjectPtr<UEditableTextBox> Search;
    UPROPERTY() TObjectPtr<UUniformGridPanel> Grid;
    UPROPERTY() TObjectPtr<UBorder> ConfirmPanel;
    UPROPERTY() TObjectPtr<UBorder> MainPanel;
    UPROPERTY() TObjectPtr<ULDCommandButton> UseButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> DropButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> ConfirmButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> SortButton;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> Cells;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> Quick;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> Assignments;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> Organizations;
    UFUNCTION() void SearchChanged(const FText& Value);
    void Filter(uint8 Kind);
    int32 HitCell(FVector2D Position) const;
    bool bOpen=false,bConfirm=false,bPointerDown=false,bDragging=false,bDirty=true;
    uint8 FilterKind=0,Columns=0;
    FGuid Selected,Dragged,DropItem;
    uint32 DropInventoryRevision=0,DropItemRevision=0,SeenInventory=0,SeenOwner=0,SeenLife=0;
    FVector2D DragStart;
    float AppliedScale=1;
};
