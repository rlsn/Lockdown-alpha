#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "UI/LDHUDModel.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchTimeline.h"
#include "LDChoicesPanel.generated.h"
class UTextBlock;
class UBorder;
class UImage;
class UTexture2D;
class USizeBox;
class UVerticalBox;
class UHorizontalBox;
class UWrapBox;
class UCanvasPanel;
class UCanvasPanelSlot;
struct FLDDemoContentDefinitions;

// The 构筑 tray behind the HUD's bottom-right 构筑 button: open, it is the build workshop (ULDWorkshop lays it out), with the
// pending decisions stacked in the workshop's pending area, the active offer and the final mod on a centre stage of big
// cards, and a rest's NPC services on the workshop objects they act on (APP_SPEC §7 构筑). In the field, closed, the same
// cards show as quick picks above the button. One row per pending decision (the active offer nearest the bottom, then the
// card offer with its locks, reroll and skip, and the blueprints that have an empty Q/W/E slot). A card shows the icon and
// the name only; hovering it shows its full rules in the workshop's dock or the HUD's slot popup (Hovered); a click takes it.
// Nothing scrolls and nothing explains:
// results and refusals go to the HUD's toast (Notify). All mutations retain the existing build commands, revision checks,
// confirmations and authority receipts.
class UScaleBox;
UCLASS()
class LOCKDOWN_API ULDChoicesPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    // Growth's owning-connection entry: returns the allocated request id, or 0 with a local refusal reason.
    TFunction<uint64(const FLDBuildCommand&,ELDGrowthReason&)> SubmitBuild;
    // The id the next build submit will receive (0 while one is in flight); a Listen Host answers inside the call.
    TFunction<uint64()> PeekRequestId;
    TFunction<void(const FLDBuildReceipt&,const FLDFinalModSelection&)> FinalModApplied;
    // Every result and refusal line (the HUD shows it as a toast).
    TFunction<void(const FString&)> Notify;
    // Stats: the owner's level and stats, so every card shows its numbers for now (#147). Stage (the public match's build
    // stage, LDProgression::StageForPublic) and the life phase judge what the server would refuse, for the disabled reasons.
    // Keys: the key the HUD shows for each Q/W/E slot; every line that names a slot uses it.
    void Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,LDProgression::EMatchStage Stage,
        const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats,const LDHUD::FSlotKeys& Keys,bool bDormRested=false);
    void UpdateInputs(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,LDProgression::EMatchStage Stage,
        const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats,const LDHUD::FSlotKeys& Keys,bool bDormRested);
    struct FWorkshopPlaces;
    struct FDrawerState
    {
        bool bStage=false;
        bool bLibraryOnly=false;
        bool bFinalStage=false;
        bool bShop=false;
        bool bServices=false;
        bool bCharacterServices=false;
    };
    FDrawerState DrawerState(bool bWorkshopOpen) const;
    void PresentWithBounds(const FWorkshopPlaces& InPlaces,FVector2D Inset,float Width=0.f,float Height=0.f);
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDBuildReceiptQueryResult& Result);
    // In a rest's build window the open workshop shows the rest rebuilds (respec, replace, forget) on the objects they act on;
    // the owning authority still admits every command.
    bool IsServiceShown() const;
    bool IsBlueprintSwapShown() const;
    // Absolute anchors computed from the current workshop layout.
    struct FWorkshopPlaces
    {
        bool bActive=false;
        FSlateRect Stage;
        FSlateRect Dock;
        FSlateRect Heads[LDHUD::StorageColumnCount];
        FSlateRect HeadIcons[LDHUD::StorageColumnCount];
        FSlateRect CharacterFoot;
        FSlateRect Points;
        FSlateRect PendingTop;
        FSlateRect Shop;
        FSlateRect DrawerHeader;
    };
    // The centre stage shows the active offer (and then the final mod) as big cards while the full workshop is open, until
    // the player steps back to the board for that offer.
    bool IsStageShown() const {return bStageShown;}
    bool IsFinalStage() const {return bStageShown&&bStageFinal;}
    bool LeaveStage();
    // Field quick picks: with the tray closed, the pending cards stand above the 构筑 button; in an engagement window they
    // shrink to icons and show no description.
    void SetQuickPick(bool bQuick,bool bCompact);
    bool IsQuickShown() const {return bQuickShown;}
    bool IsQuickHit(FVector2D Absolute) const;
    // The components a confirm line would take off or send back (lit orange on the board before the hold).
    TArray<FName> ConfirmWarnings() const;
    // The tray's distance from the screen's bottom-right corner (the HUD keeps it right above the 构筑 button), and the width
    // its rows wrap at (0: four cards); in the workshop the rows wrap inside the pending area.
    void SetTrayInset(float Right,float Bottom,float Width=0.f,float Height=0.f);
    // An offer card click: chooses that active with bLearnIfPossible.
    bool ChooseOffer(int32 Index);
    bool SkipOffer();
    // Alt+1/2/3 hotkey: takes candidate Index of the currently active 3-choice offer (#331).
    bool PickByHotkey(int32 Index);
    // Universal final cards: a tray click commits at once (ChooseFinalMod); on the stage a click only selects
    // (SelectFinalMod) and the hold of GrowthFinalLock commits (LockFinalMod).
    bool ChooseFinalMod(int32 Index);
    bool SelectFinalMod(int32 Index);
    bool ReopenFinalStage(int32 Index);
    bool LockFinalMod();
    bool IsShowingFinalMod() const {return ShownFinalMod.IsValid();}
    // Keeps an offer card's popup showing without a pointer (automation and review captures); INDEX_NONE releases it.
    bool PreviewOffer(int32 Index);
    // A blueprint card click: learns the blueprint at Index of the shown list into the lowest empty Q/W/E slot.
    bool LearnBlueprintCard(int32 Index);
    // A trainer blueprint row's "学到 Q/W/E": learns the blueprint at Index of the shown list into the empty slot Key (0 = Q).
    bool LearnBlueprint(int32 Index,uint8 Key);
    // #198 rest services. Respec sends at once; AskReplace (the blueprint at Index of the shown list into the
    // occupied slot Key) and AskForget (the active in slot Key) show the confirm line, which ConfirmRestAction sends and
    // CancelRestAction (or Back, or closing the panel) withdraws. Each refuses locally with the reason it toasts.
    bool Respec();
    bool AskReplace(int32 Index,uint8 Key);
    bool AskForget(uint8 Key);
    // The trainer's right click on the board's skill head in Column (LDHUD::SlotColumn): asks to forget that slot's active.
    // False (the click is not taken) without the trainer or on the body and dash columns.
    bool ForgetAtColumn(int32 Column);
    // The card offer (spec §6.4): a card click takes it (installed on its form's place in the same commit when one is free,
    // LDProgression::DefaultCardTarget), its lock carries it into the next offer (again: unlocks), 重抽 redraws the offer
    // keeping the lock, 跳过 lets it go.
    bool PickCardOffer(int32 Index);
    bool RerollCardOffer();
    bool SkipCardOffer();
    bool ConfirmRestAction();
    void CancelRestAction();
    // Spec §3.13 rest shop: the 商店 toggle (a stocked shop in a rest) opens the shelf over the board. Buying a slot, the skill
    // point and a refresh send at once, paid in unspent stat points. Each refuses locally with the reason it toasts.
    bool ToggleShop();
    bool IsShopShown() const;
    bool BuyShopSlot(int32 Index);
    bool BuyShopSkillPoint();
    bool RefreshShopShelf();
    // The confirm line is a child confirmation for Back routing; it captures no input, so play goes on meanwhile.
    virtual bool HasChildConfirmation() const override {return RestConfirm.Kind!=ELDBuildCommandKind::None;}
    virtual bool CloseChildConfirmation() override;
    virtual bool ChildConfirmationYields() const override {return true;}
    virtual bool IsPanelOpen() const override {return bOpen;}
    // The 构筑 tray (the workshop) is open.
    bool IsTrayOpen() const {return bOpen;}
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
    bool HasPendingRequest() const {return PendingRequest!=0;}
    // The build ring (ULDWorkshop) draws every decision itself: the tray keeps its rules, guards, requests and receipts and
    // shows nothing of its own.
    void SetHeadless(bool bInHeadless){bHeadless=bInHeadless;}
    // The open rest confirm line (替换 / 遗忘), with the reason it would be refused now; empty without one.
    FString ConfirmLine() const;
    // The tray has at least one card (LDHUD::GrowthPending over the same view).
    bool HasCards() const;
    // The card of the open tray under Pointer (absolute), else the pinned offer card, with its popup lines; or null.
    const UWidget* Hovered(FVector2D Pointer,LDHUD::FActionDetailView& Out,FName* OutComponent=nullptr,FString* OutIcon=nullptr) const;
    // The tray's laid-out size, so the workshop can give its rows the room they take in the pending area.
    // The HUD opened the tray for the opening offer (D10): it folds back to the 构筑 button after a few seconds unless the
    // player engages with it (LDHUD::FChoicesFold).
    void MarkOpenedAutomatically();
    // Called on every HUD refresh with whether the own body was hit since the last one; folds an automatic opening when due.
    void UpdateFold(bool bOwnHit);
    // A card under the pointer, a rest confirm line, an open picker or a request in flight.
    bool IsEngaged() const;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual UWidget* FocusScope() const override;
    virtual UWidget* HitArea() const override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual int32 NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FLDDemoOpponentProbe;
    friend class FLDHUDCardFacesTest;
#endif
    // One tray card: the icon (the name's first character without its texture) over the name, or the icon alone as a grid
    // tile. Detail is its popup.
    struct FTrayCard
    {
        FName Component;
        TObjectPtr<USizeBox> Frame;
        TObjectPtr<ULDCommandButton> Button;
        TObjectPtr<UBorder> Shape;
        TObjectPtr<USizeBox> IconFrame;
        TObjectPtr<UImage> Picture;
        TObjectPtr<UTextBlock> Glyph;
        TObjectPtr<UTextBlock> Name;
        FString FullName;
        TObjectPtr<UWidget> HotkeyChip;
        TObjectPtr<UTextBlock> HotkeyText;
        TObjectPtr<UWidget> KindBanner;
        TObjectPtr<UTextBlock> KindBannerText;
        TObjectPtr<UTextBlock> Benefit;
        bool bElementStone=false;
        // What the face shows (the definition and the outcome it was rendered for), so it is rendered again only on a change.
        FString ShownKey;
        FString AppliedIcon;
        // Whether the picture was applied for an enabled card (-1: not yet).
        int32 AppliedEnabled=-1;
        bool bTile=false;
        LDHUD::FActionDetailView Detail;
        bool bBig=false;
    };
    struct FOfferGhost
    {
        FSlateRect Frame,Icon,NameBounds;
        TWeakObjectPtr<UTexture2D> Texture;
        FString Name,Glyph;
        float NameSize=0.f;
    };
    struct FOfferMotion
    {
        LDHUD::FOfferMotionTicket Ticket;
        FOfferGhost Chosen;
        TArray<FOfferGhost> Others;
        ELDBuildChoiceResult Result=ELDBuildChoiceResult::None;
        uint8 Slot=0;
        double StartedAt=-1.;
    };
    enum class ETrayGroup : uint8 { Blueprint,CardOffer,Offer,FinalMod };
    struct FTrayUnit
    {
        ETrayGroup Group=ETrayGroup::Blueprint;
        int32 First=0;
        int32 Count=0;
    };
    // The trainer's buttons of one pending blueprint, standing on the Q/W/E column heads: "学到" on an empty slot, "替换" on
    // an occupied one (#198 rest only); only the selected blueprint's show.
    struct FBlueprintRow
    {
        TObjectPtr<ULDCommandButton> Pick;
        TArray<TObjectPtr<ULDCommandButton>> Learn;
        TArray<TObjectPtr<ULDCommandButton>> Replace;
    };
    // A replace or forget waiting on the confirm line, with the terms it was asked under.
    struct FRestConfirm
    {
        ELDBuildCommandKind Kind=ELDBuildCommandKind::None;
        FName Blueprint;
        uint8 Slot=0;
        // The active the slot held when asked; another one there by confirm time withdraws the line instead.
        FName Active;
    };
    // The tray: its frame (placed over the whole canvas) and the stack of rows.
    UPROPERTY() TObjectPtr<UBorder> TrayFrame;
    UPROPERTY() TObjectPtr<UVerticalBox> Tray;
    UPROPERTY() TObjectPtr<UHorizontalBox> TrayPager;
    UPROPERTY() TObjectPtr<ULDCommandButton> TrayPrevious;
    UPROPERTY() TObjectPtr<ULDCommandButton> TrayNext;
    UPROPERTY() TObjectPtr<UTextBlock> TrayPageText;
    TArray<FTrayUnit> TrayUnits;
    TArray<FIntPoint> TrayPages;
    int32 TrayPage=0;

    float TrayAvailableWidth=0.f,TrayAvailableHeight=0.f;
    bool bTrayOverflow=false,bTrayOverflowReported=false;
    bool bCompactTray=false,bIconOnlyTray=false;
    UPROPERTY() TObjectPtr<UWidget> OfferGroup;
    UPROPERTY() TObjectPtr<UTextBlock> OfferNumber;
    UPROPERTY() TObjectPtr<UWrapBox> OfferRow;
    UPROPERTY() TObjectPtr<UWidget> FinalModGroup;
    UPROPERTY() TObjectPtr<UWrapBox> FinalModRow;
    // The card offer row: a cell per card (the card over its lock) and the reroll/skip column.
    UPROPERTY() TObjectPtr<UWrapBox> CardRow;
    UPROPERTY() TArray<TObjectPtr<UWidget>> CardCells;
    UPROPERTY() TObjectPtr<UWidget> CardActions;
    UPROPERTY() TObjectPtr<ULDCommandButton> CardRerollButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> CardSkipButton;
    // Every tray row (they all wrap at one width).
    UPROPERTY() TArray<TObjectPtr<UWrapBox>> TrayRows;
    UPROPERTY() TObjectPtr<UWrapBox> BlueprintCardRow;
    TArray<FTrayCard> OfferCards;
    TArray<FTrayCard> FinalCards;
    FTrayCard SkipCard;
    TArray<FTrayCard> CardCards;
    TArray<FTrayCard> BlueprintCards;
    TArray<FBlueprintRow> BlueprintRows;
    // The centre stage: its title, the chance line, the cards moved in from their tray rows, the final sources, the final
    // description (effect, cost, notes) with the lock, and the way back to the board.
    UPROPERTY() TObjectPtr<UCanvasPanel> Layer;
    UPROPERTY() TObjectPtr<UWidget> StageBox;
    UPROPERTY() TObjectPtr<UTextBlock> StageTitle;
    UPROPERTY() TObjectPtr<UTextBlock> StageSub;
    UPROPERTY() TObjectPtr<UHorizontalBox> StageCards;
    UPROPERTY() TObjectPtr<ULDCommandButton> StageBack;
    UPROPERTY() TObjectPtr<UWidget> FinalDesc;
    // The selected card's effect, cost and notes lines (each a label chip and its text), the whole description when the
    // catalog has no split for it, its numbers, and the sources of a picked column holding several.
    UPROPERTY() TObjectPtr<UTextBlock> FinalDescText;
    UPROPERTY() TObjectPtr<UTextBlock> FinalNumbersText;
    UPROPERTY() TObjectPtr<ULDHoldButton> FinalLockButton;
    UPROPERTY() TObjectPtr<UTextBlock> FinalLockNote;
    // The services on the workshop: the trainer's blueprint picks and head buttons, the merchant's card, and the dock box for
    // the confirm line.
    UPROPERTY() TObjectPtr<UWidget> RespecBox;
    UPROPERTY() TObjectPtr<UWrapBox> TrainerPicks;
    UPROPERTY() TObjectPtr<UWidget> ServiceDock;
    UPROPERTY() TObjectPtr<UScaleBox> ServiceFit;
    UPROPERTY() TObjectPtr<UVerticalBox> ServiceDockBody;
    // #198 rest services section.
    UPROPERTY() TObjectPtr<UWidget> RestSection;
    UPROPERTY() TObjectPtr<ULDCommandButton> RespecButton;
    UPROPERTY() TObjectPtr<UTextBlock> RespecBlocked;
    UPROPERTY() TObjectPtr<UTextBlock> ForgetBlocked;
    UPROPERTY() TObjectPtr<UWidget> RestConfirmLine;
    UPROPERTY() TObjectPtr<UTextBlock> RestConfirmText;
    UPROPERTY() TObjectPtr<ULDHoldButton> RestConfirmButton;
    FRestConfirm RestConfirm;
    UPROPERTY() TObjectPtr<UWidget> DrawerTabs;
    UPROPERTY() TObjectPtr<ULDCommandButton> ShopToggleButton;
    UPROPERTY() TObjectPtr<UWidget> ShopPanel;
    UPROPERTY() TObjectPtr<UTextBlock> ShopPoints;
    UPROPERTY() TObjectPtr<UTextBlock> ShopBlockedText;
    UPROPERTY() TObjectPtr<ULDCommandButton> ShopRefreshButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> ShopPointButton;
    UPROPERTY() TObjectPtr<class UUniformGridPanel> ShopSlotRow;
    UPROPERTY() TArray<TObjectPtr<UWidget>> ShopCells;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> ShopPrices;
    TArray<FTrayCard> ShopCards;
    bool bShopOpen=true;
    bool bLibraryOnly=false;
    uint32 AutoShopStock=0;
    uint32 AutoShopMatch=0;
    bool IsShopOffered() const;
    void RefreshShopPanel();
    bool ShopCommand(ELDBuildCommandKind Kind,FLDBuildCommand& Out);
    // Every widget built after RebuildWidget, kept alive with the tree.
    UPROPERTY() TArray<TObjectPtr<UWidget>> RowWidgets;
    TMap<UWidget*,UCanvasPanelSlot*> LayerSlots;
    FWorkshopPlaces Places;
    bool bStageShown=false;
    bool bStageFinal=false;
    FGuid StageLeftFor;
    FGuid StageShownFor;
    double StageOpenedAt=-1.;
    FOfferMotion OfferMotion;
    FGuid ConfirmedOffer;
    uint32 ConfirmedMatch=0,ConfirmedLife=0;
    int32 TrainerBlueprint=0;
    bool bQuick=false;
    bool bQuickCompact=false;
    bool bQuickShown=false;
    FLDProgressionView Latest;
    uint32 LatestMatch=0,LatestLife=0;
    ELDLifePhase LatestLifePhase=ELDLifePhase::Unspawned;
    LDProgression::EMatchStage LatestStage=LDProgression::EMatchStage::Closed;
    const FLDDemoContentDefinitions* LatestContent=nullptr;
    LDHUD::FCardStats LatestStats;
    LDHUD::FSlotKeys SlotKeys;
    LDHUD::FBuildGateView Gate;
    FGuid ShownOffer;
    uint64 ShownOfferRevision=0;
    FGuid ShownFinalMod;
    FName PendingFinalOption;
    FLDFinalModSelection SubmittedFinal;
    bool bFinalFeedbackAllowed=false;
    // The blueprints the trainer rows and the tray cards show, in the view's order.
    TArray<FName> ShownBlueprints;
    // The card offer the row shows (id, revision and cards).
    FGuid ShownCardOffer;
    uint64 ShownCardRevision=0;
    TArray<FName> ShownCardOptions;
    // The owner's level, stats or catalog changed: every shown card face is rendered again.
    bool bCardsStale=true;
    // PreviewOffer's pinned card, whose popup shows while no card is hovered.
    int32 PinnedPreview=INDEX_NONE;
    bool bPinnedOfferExplicit=false;
    uint64 PendingRequest=0;
    ELDBuildCommandKind PendingKind=ELDBuildCommandKind::None;
    uint32 Answers=0;
    bool bOpen=false;
    bool bHeadless=false;
    LDHUD::FChoicesFold Fold;
    double UiNow() const;
    bool Usable() const;
    // The lowest empty Q/W/E slot of the latest view, or 3 when all are full.
    uint8 FirstEmptySlot() const;
    // Builds one tray card whose click runs Click; the caller adds its Frame to a row.
    // What a tray card stands for, so each kind reads apart at a glance: a skill (blue frame, square-cut icon), a build card
    // (gold frame, taller like a playing card) or anything else (the plain frame).
    enum class ETrayLook : uint8 { Plain, Skill, Card };
    FTrayCard MakeCard(FName ButtonName,TFunction<void()> Click,ETrayLook Look=ETrayLook::Plain,bool bHold=false);
    // The face of Card needs rendering for Key (another definition or outcome, or new numbers).
    bool CardStale(const FTrayCard& Card,const FString& Key) const;
    // Fills a card's name, icon and popup for Key.
    void ShowFace(FTrayCard& Card,const FString& Key,const FString& Name,const FString& IconId,const FString& Glyph,
        const LDHUD::FActionDetailView& Detail,const FString& Rule=FString());
    // Shows the card as a named card or an icon tile (bTile), enabled or greyed out.
    void ShowLook(FTrayCard& Card,bool bTile,bool bEnabled,bool bVisible=true);
    FTrayCard& BlueprintCardAt(int32 Index);
    FBlueprintRow& BlueprintRowAt(int32 Index);
    void RefreshOffer();
    void RefreshFinalMod();
    bool SubmitFinalMod(FName Option);
    void RefreshCardOffer(bool bTiles);
    // The guards every card offer command carries (the shown offer's id and revision), or false with the toast.
    bool CardOfferCommand(ELDBuildCommandKind Kind,FLDBuildCommand& Out);
    void RefreshBlueprintCards();
    void RefreshBlueprints();
    void RefreshRest();
    void UpdateStage();
    bool ConfirmedOfferStillShown() const;
    bool HasAvailableOffer() const;
    bool HasAvailableFinal() const;
    bool OfferMotionActive() const;
    void CaptureOfferMotion(int32 Index);
    void PaintOfferMotion(const LDUIPaint::FContext& Paint) const;
    void PlanTrayPages(bool bTiles);
    bool TrayShows(ETrayGroup Group,int32 Index=0) const;
    void RefreshStage();
    void LayoutLayer();
    void PlaceOnLayer(UWidget* Widget,const FSlateRect& Absolute,FVector2D Alignment,bool bFill=false);
    void ShowBig(FTrayCard& Card,bool bBig,float Width,float Icon);
    // Why a rest service is refused now: the gate's reason, then the local checks (price, unspent points, an outstanding
    // offer, nothing allocated); empty when it may be sent.
    FString RespecRefusal() const;
    FString ReplaceRefusal() const;
    FString ForgetRefusal() const;
    // The confirm line's terms still hold in the latest view (the blueprint is pending, the slot holds the same active).
    bool RestConfirmHolds() const;
    // Why the confirm line's command would be refused now (the service's own refusal); empty when it may be sent.
    FString RestConfirmRefusal() const;
    void RefreshAll();
    bool Send(FLDBuildCommand Command);
    void ShowFinal(const FLDBuildReceipt& Receipt);
    void FinishRequest();
    // Toasts a result or refusal line (nothing for an empty one).
    void Tell(const FString& Text);
    // The latest result line, toasted when the request finishes.
    FString ResultLine;
};
