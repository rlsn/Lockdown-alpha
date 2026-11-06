#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Audio/LDAudioTypes.h"
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
// prices and cooldowns stay with the server. Nothing asks for a confirmation. A new match closes the panel; taking
// damage never does, and death only blocks trading.
//
// Buying an item the own equipment already holds upgrades that copy instead (the server's once-per-match Upgrade at its
// quoted price), so its shown price is the upgrade price; an owned item that cannot be upgraded any more buys a second copy.
//
// Pre-orders: up to LDHUD::MaxShopPreorders items, kept for the match (death keeps them). Pre-sells: own equipment
// instances marked from the HUD's equipment slots (TogglePresell), kept until sold, unmarked or no longer held. While the
// own body stands in the shop's range (a local trigger; the server re-checks reach), the HUD requests a session for them
// and the panel sells every marked instance first, then buys or upgrades the next pre-order LDHUD::NextPreorder picks
// from the session quote, one request at a time. A bought or upgraded definition leaves the pre-orders on its Applied
// receipt, whether it was sent automatically or by hand; a refused automatic request waits for a new gold, inventory or
// quote revision, or a short retry delay, before it tries again.
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
    // The HUD plays the panel's local cues: ShopMarked when a pre-order or pre-sell is added, ShopUnmarked when one is
    // taken back. Trade results keep their own receipt cues.
    TFunction<void(ELDAudioCue)> PlayCue;
    // Called on every HUD refresh. View is the current Opened session quote (nullptr when there is none);
    // EquipView is the own two-slot view (nullptr when not projected); BuildRevision is the owner's current build revision
    // (bBuildKnown=false when the progression view is not projected).
    // bNearShop: the own body stands in the shop's range by the local check (ULDNavigationComponent::OwnBodyAtShop).
    void Present(const FLDShopView* View,const FLDEquipmentView* EquipView,bool bBuildKnown,uint64 BuildRevision,uint32 Match,uint32 Life,ELDLifePhase Phase,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* ContentDefs,bool bNearShop);
    // The right click on an icon: buys it (or upgrades the owned copy) right away in the shop's range with this life's
    // quote, else pre-orders it; false (with feedback) when nothing was sent or queued.
    bool QuickBuy(int32 Index);
    bool AddPreorder(FName DefinitionId);
    bool RemovePreorder(int32 Index);
    const TArray<FName>& Preorders() const {return PreorderQueue;}
    // Marks a held equipment instance for selling, or unmarks it. In the shop's range with this life's quote the mark
    // sells right away, without a confirmation; anywhere else it sells itself on reaching the shop. False (with a notice)
    // when the instance is not held.
    bool TogglePresell(const FGuid& Instance);
    bool IsPresell(const FGuid& Instance) const {return PresellQueue.Contains(Instance);}
    // This life has no session yet while the page is open, an item is marked for selling or a pre-order could be bought
    // now: the HUD requests one while at the shop.
    bool WantsSession() const;
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDShopReceiptQueryResult& Result);
    bool HasPendingRequest() const {return PendingRequest!=0;}
    virtual bool IsPanelOpen() const override {return bOpen;}
    // Opened by the HUD's shop entry or a left click on the shop model; never takes keyboard focus.
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
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
    UPROPERTY() TObjectPtr<UTextBlock> GoldLine;
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
    FLDShopView Latest;
    FLDEquipmentView LatestEquipment;
    bool bLatestView=false;
    bool bLatestNear=false;
    // What the list shows: the session quote of this life, else the approved catalog (browse and pre-order only).
    TArray<FLDShopItemView> Listing;
    TArray<FName> PreorderQueue;
    // Own equipment instances marked for selling, in marking order.
    TArray<FGuid> PresellQueue;
    // The definition of the pending Buy or Upgrade, the instance of the pending Sell, and whether a pre-order or pre-sell
    // sent it automatically.
    FName PendingDefinition;
    FGuid PendingInstance;
    bool bPendingAuto=false;
    // A refused automatic request waits for one of these revisions to move, or for the retry time.
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
    // The quote's row for a held instance; nullptr without this life's quote or when the quote does not list it there.
    const FLDShopSellView* SellRowFor(const FGuid& Instance) const;
    // The quote's row of a held, unmarked copy of that definition whose upgrade is offered now; nullptr when none.
    const FLDShopSellView* UpgradeRowFor(FName DefinitionId) const;
    // Buying that definition would upgrade a held copy: by the quote with a session, else by the own equipment view and
    // the approved content (upgrade unspent, copy not upgraded, item upgradable, copy not marked for selling).
    bool OwnedUpgradable(FName DefinitionId) const;
    // The upgrade price: the quote's with a session, else the approved one.
    int64 UpgradeQuote() const;
    // What buying that row costs now: the upgrade price for an owned upgradable item, else its buy price.
    int64 PriceFor(const FLDShopItemView& Row) const;
    TArray<FName> UpgradableIds() const;
    void PrunePresells();
    FString CommandBlock() const;
    bool FillCommand(ELDShopCommandKind CommandKind,FLDShopCommand& Command);
    bool BuyItem(FName DefinitionId,bool bAuto);
    bool UpgradeItem(const FLDShopSellView& Row,bool bAuto);
    bool SellItem(const FLDShopSellView& Row,bool bAuto);
    // Sends the next pre-sell, else the next pre-order; true when a request was sent.
    bool TryAutoTrade();
    void BlockAutoTrade();
    int32 FreeSlots() const;
    bool HasSession() const;
    void RefreshPreorders();
    bool Send(const FLDShopCommand& Command,FString ItemName);
    void AnswerReceipt(const FLDShopReceiptQueryResult& Result);
    FString AppliedNotice(const FLDShopReceipt& Receipt) const;
    void ClearPending();
    void FinishRequest(const FString& Notice);
    void NotifyPlayer(const FString& Notice) const;
    void Sound(ELDAudioCue Cue) const;
};
