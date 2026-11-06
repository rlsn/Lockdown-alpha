#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "LDShopPanel.generated.h"
class UTextBlock;
class UBorder;
class UVerticalBox;
class UUniformGridPanel;
class UCanvasPanel;
class UCanvasPanelSlot;
class USizeBox;
struct FLDDemoContentDefinitions;

// The shop (APP_SPEC 5, UI 3.2/3.3): a normal main panel, not a full-viewport modal. It opens from the HUD's shop entry
// anywhere and from a left click on the shop model. With a session of this life it lists the server quote;
// without one it lists the approved catalog for browsing and pre-ordering only. It formats the quote and the own
// equipment view and submits Buy/Sell/Upgrade intents that carry the quote and revisions it showed; range, eligibility,
// prices and cooldowns stay with the server. Selling needs the child confirmation. A new match closes the panel; taking
// damage never does, and death only blocks trading.
//
// Pre-orders: up to LDHUD::MaxShopPreorders items, kept for the match (death keeps them). While the own body stands in
// the shop's range (a local trigger; the server re-checks reach), the HUD requests a session for them and the panel buys
// the next one LDHUD::NextPreorder picks from the session quote, one request at a time. A bought definition leaves the
// queue on its Applied receipt, whether it was bought automatically or by hand; a refused automatic buy waits for a new
// gold, inventory or quote revision, or a short retry delay, before it tries again.
UCLASS()
class LOCKDOWN_API ULDShopPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    // The panel docks in the bottom-right corner at this width, clear of the screen centre, so the world beside it keeps its input.
    static constexpr float DockWidth=440.f;
    // Distance of the panel's bottom edge from the screen bottom: the HUD keeps it right above its shop entry.
    void SetBottomDock(float Bottom);
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
    // bNearShop: the own body stands in the shop's range by the local check (ULDNavigationComponent::OwnBodyAtShop).
    void Present(const FLDShopView* View,const FLDEquipmentView* EquipView,bool bBuildKnown,uint64 BuildRevision,uint32 Match,uint32 Life,ELDLifePhase Phase,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* ContentDefs,bool bNearShop);
    // The right click on an icon: buys it right away in the shop's range with this life's quote, else pre-orders it;
    // false (with feedback) when nothing was sent or queued.
    bool QuickBuy(int32 Index);
    bool AddPreorder(FName DefinitionId);
    bool RemovePreorder(int32 Index);
    const TArray<FName>& Preorders() const {return PreorderQueue;}
    // This life has no session yet while the page is open or a pre-order could be bought now: the HUD requests one
    // while at the shop.
    bool WantsSession() const;
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDShopReceiptQueryResult& Result);
    // Opens the sell child confirmation for that equipment slot.
    bool RequestSell(uint8 SlotIndex);
    // 112 §7: opens the same child confirmation for upgrading that slot's item (one upgrade per match).
    bool RequestUpgrade(uint8 SlotIndex);
    void ConfirmSell();
    bool HasPendingRequest() const {return PendingRequest!=0;}
    virtual bool IsPanelOpen() const override {return bOpen;}
    // Opened by the HUD's shop entry or a left click on the shop model; never takes keyboard focus.
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
    virtual bool HasChildConfirmation() const override {return bSelling;}
    virtual bool CloseChildConfirmation() override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float Delta) override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual UWidget* FocusScope() const override;
    virtual UWidget* HitArea() const override;
private:
    UPROPERTY() TObjectPtr<UBorder> MainPanel;
    UPROPERTY() TObjectPtr<UBorder> DockFrame;
    float BottomDock=96.f;
    UPROPERTY() TObjectPtr<UBorder> SellPanel;
    UPROPERTY() TObjectPtr<UTextBlock> GoldLine;
    UPROPERTY() TObjectPtr<UTextBlock> EquipNames[2];
    UPROPERTY() TObjectPtr<ULDCommandButton> SellButtons[2];
    UPROPERTY() TObjectPtr<ULDCommandButton> UpgradeButtons[2];
    UPROPERTY() TObjectPtr<UUniformGridPanel> ItemGrid;
    // Stable ShopItem<N> icon buttons reused by position; the pool only grows, so names, hover and focus survive updates.
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> ItemButtons;
    UPROPERTY() TArray<TObjectPtr<USizeBox>> ItemCells;
    UPROPERTY() TArray<TObjectPtr<UBorder>> ItemShapes;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> ItemGlyphs;
    // Last applied icon look per button (active, affordable), so a refresh only restyles what changed.
    TArray<uint8> ItemLooks;
    // The hover description beside the panel: its own canvas layer over the whole panel.
    UPROPERTY() TObjectPtr<UCanvasPanel> DetailLayer;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> DetailSlot;
    UPROPERTY() TObjectPtr<UBorder> Detail;
    int32 DetailItem=INDEX_NONE;
    // The pre-order icon the description belongs to, or INDEX_NONE when it belongs to a grid icon.
    int32 DetailChip=INDEX_NONE;
    bool bDetailStale=true;
    UPROPERTY() TObjectPtr<UTextBlock> DetailName;
    UPROPERTY() TObjectPtr<UTextBlock> DetailPrice;
    UPROPERTY() TObjectPtr<UTextBlock> DetailLines;
    UPROPERTY() TObjectPtr<UTextBlock> DetailHint;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UTextBlock> PreorderLine;
    // One icon per queued pre-order (clicking it cancels it): lit with a gold rim while the gold covers its listed price,
    // greyed out otherwise.
    UPROPERTY() TObjectPtr<ULDCommandButton> PreorderChips[2];
    UPROPERTY() TObjectPtr<UBorder> PreorderShapes[2];
    UPROPERTY() TObjectPtr<UTextBlock> PreorderGlyphs[2];
    uint8 PreorderLooks[2]={0,0};
    UPROPERTY() TObjectPtr<UTextBlock> SellPrompt;
    UPROPERTY() TObjectPtr<ULDCommandButton> SellConfirmButton;
    FLDShopView Latest;
    FLDEquipmentView LatestEquipment;
    bool bLatestView=false;
    bool bLatestNear=false;
    // What the list shows: the session quote of this life, else the approved catalog (browse and pre-order only).
    TArray<FLDShopItemView> Listing;
    TArray<FName> PreorderQueue;
    // The definition of the pending Buy and whether a pre-order sent it automatically.
    FName PendingDefinition;
    bool bPendingAuto=false;
    // A refused automatic buy waits for one of these revisions to move, or for the retry time.
    bool bAutoBlocked=false;
    uint64 AutoBlockAccount=0,AutoBlockInventory=0,AutoBlockQuote=0;
    double AutoRetryAt=0;
    bool bLatestBuildKnown=false;
    uint64 LatestBuildRevision=0;
    uint32 LatestMatch=0,LatestLife=0;
    ELDLifePhase LatestPhase=ELDLifePhase::Unspawned;
    bool bLatestClock=false;
    int64 LatestNowMs=0;
    const FLDDemoContentDefinitions* LatestContent=nullptr;
    bool bPresented=false;
    TArray<FName> ShownItems;
    bool bOpen=false;
    bool bSelling=false;
    // The open child confirmation upgrades instead of selling.
    bool bUpgrading=false;
    int64 UpgradeQuotePrice=0;
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
    void UpdateDetail();
    void FillDetail(int32 Index,bool bQueuedIcon);
    bool EquipmentKnown() const;
    const FLDShopSellView* SellableFor(uint8 SlotIdx) const;
    FString CommandBlock() const;
    bool FillCommand(ELDShopCommandKind CommandKind,FLDShopCommand& Command);
    bool BuyItem(FName DefinitionId,bool bAuto);
    void TryAutoBuy();
    void BlockAutoBuy();
    int32 FreeSlots() const;
    bool HasSession() const;
    void RefreshPreorders();
    bool Send(const FLDShopCommand& Command,FString ItemName);
    void AnswerReceipt(const FLDShopReceiptQueryResult& Result);
    FString AppliedNotice(const FLDShopReceipt& Receipt) const;
    void ClearPending();
    void FinishRequest(const FString& Notice);
    void NotifyPlayer(const FString& Notice) const;
    void ResetSell();
};
