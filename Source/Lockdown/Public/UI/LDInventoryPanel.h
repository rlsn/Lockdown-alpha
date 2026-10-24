#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "Inventory/LDInventoryTypes.h"
#include "Economy/LDCommerceDirector.h"
#include "Economy/LDProductionDirector.h"
#include "LDInventoryPanel.generated.h"
class UTextBlock;class UEditableTextBox;class UUniformGridPanel;class ULDCommandButton;class UBorder;
UCLASS()
class LOCKDOWN_API ULDInventoryPanel:public ULDPanel
{
    GENERATED_BODY()
public:
    void Open();void Close();bool IsOpen() const{return bOpen;}
    void ShowService(uint8 Tab);
    void RefreshServices();
    void BuildProduction(class UVerticalBox* All);void RefreshProduction();
    void ToggleProductionInput();void PreviewProduction(uint8 Recipe);void ArmProductionPreview();
    void BuildCommerce(class UVerticalBox* All);void RefreshCommerce();void CommerceAction(uint8 Command,ELDItemKind Kind);
    void BuildBank(class UVerticalBox* All);void RefreshBank();void ReviewBank(bool Deposit,bool Charter=false);
    void Refresh();void CancelDrag();bool IsDragging() const{return bPointerDown;}
    void Select(FGuid Id);void Use();void AskDrop();void ConfirmDrop();
    FString DetailText() const;
    void SelectOffer(ELDSkill Skill);void BuyOffer();
    FString VendorText() const;
    FString TradeText() const;
    void TradeSelected();void TradeGold(int64 Amount);void TradeReady();void TradeConfirm();

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
    friend class ALDEconomyUIProbe;
    friend class ALDCharterProbe;
    friend class ALDProductionProbe;
    friend class ALDInventoryProbe;
    friend class ALDVendorProbe;
    friend class ALDTradeProbe;
    uint8 ServiceTab=0;
    UPROPERTY() TArray<TObjectPtr<class UVerticalBox>> Services;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> ServiceTabs;
    UPROPERTY() TObjectPtr<class USizeBox> ServiceWidth;
    UPROPERTY() TObjectPtr<class UScrollBox> ServiceScroll;
    UPROPERTY() TObjectPtr<UTextBlock> ServiceHint;
    UPROPERTY() TObjectPtr<ULDCommandButton> CouponToggle;
    void BuildTradePanel(class UVerticalBox* All);
    void RefreshTradePanel();
    void BuildTradeReview(class UCanvasPanel* Canvas);
    void ShowTradeReview();
    UPROPERTY() TObjectPtr<UTextBlock> ProductionStatus;
    UPROPERTY() TObjectPtr<ULDCommandButton> ProductionInput;
    UPROPERTY() TObjectPtr<ULDCommandButton> ProductionConfirm;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> ProductionActions;
    TArray<FGuid> ProductionInputs;
    FLDProductionPlan DisplayedProduction;
    uint8 ProductionRecipe=1;
    bool bUseCoupon=true,bProductionPreview=false;
    UPROPERTY() TObjectPtr<class UVerticalBox> BankPanel;
    UPROPERTY() TObjectPtr<UTextBlock> BankStatus;
    UPROPERTY() TObjectPtr<UEditableTextBox> BankAmount;
    UPROPERTY() TObjectPtr<ULDCommandButton> BankOpenButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> BankConfirm;
    bool bBankReviewed=false,bBankDeposit=true,bCharterReview=false;
    int64 BankReviewedAmount=0;
    uint32 BankReviewedWallet=0,BankReviewedBalance=0;
    FGuid BankReviewedSession;
    UPROPERTY() TObjectPtr<class UVerticalBox> CommercePanel;
    UPROPERTY() TObjectPtr<UTextBlock> CommerceStatus;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> CommerceOpenButtons;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> CommerceLabels;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> CommerceBuyButtons;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> CommerceSellButtons;
    uint32 QuoteMatch=0;
    uint8 SelectedMarket=0;
    FLDCommerceView DisplayedCommerce;
    bool bTradeReview=false;
    uint32 ReviewedRevision=0;
    FGuid ReviewedSession;
    uint8 ReviewedStage=0;
    UPROPERTY() TObjectPtr<UTextBlock> ReviewHint;
    UPROPERTY() TObjectPtr<UBorder> TradeReview;
    UPROPERTY() TObjectPtr<UTextBlock> ReviewSummary;
    UPROPERTY() TObjectPtr<UTextBlock> ReviewOwn;
    UPROPERTY() TObjectPtr<UTextBlock> ReviewOther;
    UPROPERTY() TObjectPtr<ULDCommandButton> ReviewConfirm;
    UPROPERTY() TObjectPtr<ULDCommandButton> ReviewCancel;
    UPROPERTY() TObjectPtr<ULDCommandButton> ReviewBack;
    void SubmitTrade(uint8 Command,FGuid Item=FGuid(),int64 Amount=0);
    TArray<FGuid> DisplayedTradeItems;
    UPROPERTY() TObjectPtr<class UVerticalBox> TradePanel;
    UPROPERTY() TObjectPtr<UTextBlock> TradeStatus;
    UPROPERTY() TObjectPtr<UTextBlock> OwnTray;
    UPROPERTY() TObjectPtr<UTextBlock> OtherTray;
    UPROPERTY() TObjectPtr<UEditableTextBox> GoldInput;
    UPROPERTY() TObjectPtr<ULDCommandButton> AddTradeButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> GoldTradeButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> ReadyTradeButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> ConfirmTradeButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> AcceptTradeButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> CancelTradeButton;
    uint32 DisplayedTradeRevision=0;
    FString DisplayedTradeDigest;

    UPROPERTY() TObjectPtr<class UVerticalBox> CatalogPanel;
    UPROPERTY() TObjectPtr<UTextBlock> CatalogInfo;
    UPROPERTY() TObjectPtr<ULDCommandButton> VendorBuy;
    UPROPERTY() TObjectPtr<ULDCommandButton> AidButton;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> GearButtons;
    UPROPERTY() TObjectPtr<UTextBlock> GearPreview;
    UPROPERTY() TObjectPtr<ULDCommandButton> VendorOpen;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> Offers;
    ELDSkill SelectedOffer=ELDSkill::Spark;
    uint32 DisplayedCatalogRevision=0;
    int32 DisplayedPrice=0;
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
