#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "LDShopPanel.generated.h"
class UTextBlock;
class UBorder;
class UVerticalBox;
struct FLDDemoContentDefinitions;

// 小卖部 (APP_SPEC 5, UI 3.2/3.3): a normal main panel over the current Opened shop session, not a full-viewport modal.
// It only formats the server quote and the own equipment view, and submits Buy/Sell intents that carry the quote and
// revisions it showed; range, eligibility, prices and cooldowns stay with the server. Selling needs the child
// confirmation. Death and the end of the session, life or match close the panel; taking damage never does.
UCLASS()
class LOCKDOWN_API ULDShopPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    // Growth's owning-connection entry: returns the allocated request id, or 0 with a local refusal reason.
    TFunction<uint64(const FLDShopCommand&,ELDGrowthReason&)> SubmitShopCommand;
    // The id the next shop submit will get, or 0 when one is already in flight. A Listen Host answers inside
    // SubmitShopCommand, so the panel records the request under this id before calling it.
    TFunction<uint64()> PeekRequestId;
    // Short player notices the HUD shows as a toast (e.g. the panel closed because the session ended).
    TFunction<void(const FString&)> Notify;
    // Called on every HUD refresh. View is the current Opened session quote (nullptr when there is none);
    // EquipView is the own two-slot view (nullptr when not projected); BuildRevision is the owner's current build revision
    // (bBuildKnown=false when the progression view is not projected).
    void Present(const FLDShopView* View,const FLDEquipmentView* EquipView,bool bBuildKnown,uint64 BuildRevision,uint32 Match,uint32 Life,ELDLifePhase Phase,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* ContentDefs);
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDShopReceiptQueryResult& Result);
    // Preview only, never submits.
    bool SelectItem(int32 Index);
    // Submits Buy for the selected item at the price the panel shows.
    void Buy();
    // Opens the sell child confirmation for that equipment slot.
    bool RequestSell(uint8 SlotIndex);
    void ConfirmSell();
    bool HasPendingRequest() const {return PendingRequest!=0;}
    virtual bool IsPanelOpen() const override {return bOpen;}
    // Called by the HUD only after an authoritative Opened result; never takes keyboard focus.
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
    virtual bool HasChildConfirmation() const override {return bSelling;}
    virtual bool CloseChildConfirmation() override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual UWidget* FocusScope() const override;
    virtual UWidget* HitArea() const override;
private:
    UPROPERTY() TObjectPtr<UBorder> MainPanel;
    UPROPERTY() TObjectPtr<UBorder> SellPanel;
    UPROPERTY() TObjectPtr<UTextBlock> GoldLine;
    UPROPERTY() TObjectPtr<UTextBlock> EquipNames[2];
    UPROPERTY() TObjectPtr<ULDCommandButton> SellButtons[2];
    UPROPERTY() TObjectPtr<UVerticalBox> ItemList;
    // Stable ShopItem<N> buttons reused by position; the pool only grows, so names, hover and focus survive updates.
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> ItemButtons;
    UPROPERTY() TObjectPtr<UTextBlock> DetailName;
    UPROPERTY() TObjectPtr<UTextBlock> DetailPrice;
    UPROPERTY() TObjectPtr<UTextBlock> DetailLines;
    UPROPERTY() TObjectPtr<UTextBlock> DetailHint;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<ULDCommandButton> BuyButton;
    UPROPERTY() TObjectPtr<UTextBlock> SellPrompt;
    UPROPERTY() TObjectPtr<ULDCommandButton> SellConfirmButton;
    FLDShopView Latest;
    FLDEquipmentView LatestEquipment;
    bool bLatestView=false;
    bool bLatestBuildKnown=false;
    uint64 LatestBuildRevision=0;
    uint32 LatestMatch=0,LatestLife=0;
    ELDLifePhase LatestPhase=ELDLifePhase::Unspawned;
    bool bLatestClock=false;
    int64 LatestNowMs=0;
    const FLDDemoContentDefinitions* LatestContent=nullptr;
    bool bPresented=false;
    TArray<FName> ShownItems;
    FName SelectedItem;
    int32 Selected=INDEX_NONE;
    bool bOpen=false;
    bool bSelling=false;
    FGuid SellInstance;
    uint64 SellItemRevision=0;
    int64 SellQuotePrice=0;
    FString SellName;
    // Cleared exactly when the controller ends the request, so the panel never unlocks a second submit early.
    uint64 PendingRequest=0;
    ELDShopCommandKind PendingKind=ELDShopCommandKind::None;
    FString PendingName;
    int64 PendingPrice=0;
    // Counts accepted answers for the pending request; tells Send whether one already arrived inside the submit call.
    uint32 AnswerCount=0;
    void RefreshShop();
    void SyncItemButtons();
    bool EquipmentKnown() const;
    const FLDShopSellView* SellableFor(uint8 SlotIdx) const;
    FString CommandBlock() const;
    bool FillCommand(ELDShopCommandKind CommandKind,FLDShopCommand& Command);
    bool Send(const FLDShopCommand& Command,FString ItemName);
    void AnswerReceipt(const FLDShopReceiptQueryResult& Result);
    FString AppliedNotice(const FLDShopReceipt& Receipt) const;
    void ClearPending();
    void FinishRequest(const FString& Notice);
    void NotifyPlayer(const FString& Notice) const;
    void ResetSell();
};
