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
class USizeBox;
class UVerticalBox;
class UHorizontalBox;
class UWrapBox;
struct FLDDemoContentDefinitions;

// The 构筑 tray behind the HUD's bottom-right 构筑 button, and the NPC service window of a rest.
// The tray is a stack of card rows right above the button, one row per pending decision (the active offer nearest the
// button, then the opening base pick, the elite component choices and the blueprints that have an empty Q/W/E slot). A card
// shows the icon and the name only; hovering it shows its full rules in the HUD's slot popup (Hovered) and a click takes it.
// Component rows switch to icon-only tiles when there are too many component cards. Nothing scrolls and nothing explains:
// results and refusals go to the HUD's toast (Notify). All mutations retain the existing build commands, revision checks,
// confirmations and authority receipts.
UCLASS()
class LOCKDOWN_API ULDChoicesPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    // Growth's owning-connection entry: returns the allocated request id, or 0 with a local refusal reason.
    TFunction<uint64(const FLDBuildCommand&,ELDGrowthReason&)> SubmitBuild;
    // The id the next build submit will receive (0 while one is in flight); a Listen Host answers inside the call.
    TFunction<uint64()> PeekRequestId;
    // Every result and refusal line (the HUD shows it as a toast).
    TFunction<void(const FString&)> Notify;
    // Stats: the owner's level and stats, so every card shows its numbers for now (#147). Stage (the public match's build
    // stage, LDProgression::StageForPublic) and the life phase judge what the server would refuse, for the disabled reasons.
    // Keys: the key the HUD shows for each Q/W/E slot; every line that names a slot uses it.
    void Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,LDProgression::EMatchStage Stage,
        const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats,const LDHUD::FSlotKeys& Keys);
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDBuildReceiptQueryResult& Result);
    // Local view context only; the owning authority still admits every service command.
    void SetServiceContext(ELDRestService Service);
    bool OpenServicePanel();
    // The tray's distance from the screen's bottom-right corner (the HUD keeps it right above the 构筑 button).
    void SetTrayInset(float Right,float Bottom);
    // An offer card click: chooses that active with bLearnIfPossible.
    bool ChooseOffer(int32 Index);
    bool SkipOffer();
    // Independent final cards; several legal sources open a source picker before the choice commits.
    bool ChooseFinalMod(int32 Index);
    bool ChooseFinalModSource(int32 Index);
    bool IsShowingFinalMod() const {return ShownFinalMod.IsValid();}
    // Keeps an offer card's popup showing without a pointer (automation and review captures); INDEX_NONE releases it.
    bool PreviewOffer(int32 Index);
    // A blueprint card click: learns the blueprint at Index of the shown list into the lowest empty Q/W/E slot.
    bool LearnBlueprintCard(int32 Index);
    // A trainer blueprint row's "学到 Q/W/E": learns the blueprint at Index of the shown list into the empty slot Key (0 = Q).
    bool LearnBlueprint(int32 Index,uint8 Key);
    // #198 rest services. BuyNpcOffer and Respec send at once; AskReplace (the blueprint at Index of the shown list into the
    // occupied slot Key) and AskForget (the active in slot Key) show the confirm line, which ConfirmRestAction sends and
    // CancelRestAction (or Back, or closing the panel) withdraws. Each refuses locally with the reason it toasts.
    bool BuyNpcOffer();
    bool Respec();
    bool AskReplace(int32 Index,uint8 Key);
    bool AskForget(uint8 Key);
    // #198 G rest material services. ToggleFillGap opens (or closes) the gap target list; PickGapTarget shows that target's
    // fillers and PickGapFiller the confirm line ("花 4 点：为 X 补 Y"). ToggleDismantle opens the list of owned common
    // components; PickDismantle selects or deselects one, and the approved count selected shows the confirm line naming what
    // is removed and which installs go back to the inventory. ConfirmRestAction sends either.
    bool ToggleFillGap();
    bool PickGapTarget(int32 Index);
    bool PickGapFiller(int32 Index);
    bool ToggleDismantle();
    bool PickDismantle(int32 Index);
    // #198 G: a pending elite camp component choice (ChoiceIndex in the view's oldest-first order): take the option at
    // OptionIndex, or, once every option is owned, the empty-pool points.
    bool PickComponentChoice(int32 ChoiceIndex,int32 OptionIndex);
    bool TakeComponentChoicePoints(int32 ChoiceIndex);
    bool ConfirmRestAction();
    void CancelRestAction();
    // The confirm line is a child confirmation for Back routing; it captures no input, so play goes on meanwhile.
    virtual bool HasChildConfirmation() const override {return RestConfirm.Kind!=ELDBuildCommandKind::None;}
    virtual bool CloseChildConfirmation() override;
    virtual bool ChildConfirmationYields() const override {return true;}
    // #198 B: an opening base card click: takes the option at Index of the shown list; a plain base asks to install it on
    // 本体 in the same commit, B18 goes to the storage, where its drop on a skill asks for the element (#227).
    bool PickOpeningBase(int32 Index);
    virtual bool IsPanelOpen() const override {return bOpen||bServicePanelOpen;}
    // The 构筑 tray (not the NPC service window) is open.
    bool IsTrayOpen() const {return bOpen;}
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
    bool HasPendingRequest() const {return PendingRequest!=0;}
    // The tray has at least one card (LDHUD::GrowthPending over the same view).
    bool HasCards() const;
    // The card of the open tray under Pointer (absolute), else the pinned offer card, with its popup lines; or null.
    const UWidget* Hovered(FVector2D Pointer,LDHUD::FActionDetailView& Out,FName* OutComponent=nullptr) const;
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
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FLDDemoOpponentProbe;
#endif
    // One tray card: the icon (the name's first character without its texture) over the name, or the icon alone as a grid
    // tile. Detail is its popup.
    struct FTrayCard
    {
        FName Component;
        TObjectPtr<USizeBox> Frame;
        TObjectPtr<ULDCommandButton> Button;
        TObjectPtr<USizeBox> IconFrame;
        TObjectPtr<UImage> Picture;
        TObjectPtr<UTextBlock> Glyph;
        TObjectPtr<UTextBlock> Name;
        // What the face shows (the definition and the outcome it was rendered for), so it is rendered again only on a change.
        FString ShownKey;
        FString AppliedIcon;
        // Whether the picture was applied for an enabled card (-1: not yet).
        int32 AppliedEnabled=-1;
        bool bTile=false;
        LDHUD::FActionDetailView Detail;
    };
    // One pending elite component choice: its option cards and the empty-pool points card.
    struct FChoiceRow
    {
        TObjectPtr<UWrapBox> Row;
        TArray<FTrayCard> Options;
        FTrayCard Points;
    };
    struct FBlueprintRow
    {
        TObjectPtr<UWidget> Row;
        TObjectPtr<UImage> Picture;
        TObjectPtr<UTextBlock> Name,Summary,Blocked;
        TArray<TObjectPtr<ULDCommandButton>> Learn;
        // #198 rest only: "替换 Q/W/E" for each occupied slot.
        TArray<TObjectPtr<ULDCommandButton>> Replace;
    };
    // A replace, forget (#198 G: fill gap or dismantle) waiting on the confirm line, with the terms it was asked under.
    struct FRestConfirm
    {
        ELDBuildCommandKind Kind=ELDBuildCommandKind::None;
        FName Blueprint;
        uint8 Slot=0;
        // The active the slot held when asked; another one there by confirm time withdraws the line instead.
        FName Active;
        // FillGap: the gap target and the base it receives; Dismantle: the surrendered components.
        FName GapTarget;
        FName GapFiller;
        TArray<FName> Surrendered;
    };
    // The tray: its frame (placed over the whole canvas) and the stack of rows.
    UPROPERTY() TObjectPtr<UBorder> TrayFrame;
    UPROPERTY() TObjectPtr<UVerticalBox> Tray;
    UPROPERTY() TObjectPtr<UWidget> OfferGroup;
    UPROPERTY() TObjectPtr<UTextBlock> OfferNumber;
    UPROPERTY() TObjectPtr<UWrapBox> OfferRow;
    UPROPERTY() TObjectPtr<UWidget> FinalModGroup;
    UPROPERTY() TObjectPtr<UWrapBox> FinalModRow;
    UPROPERTY() TObjectPtr<UWrapBox> FinalSourceRow;
    UPROPERTY() TObjectPtr<UWrapBox> BaseRow;
    UPROPERTY() TObjectPtr<UVerticalBox> ChoiceRowList;
    UPROPERTY() TObjectPtr<UWrapBox> BlueprintCardRow;
    TArray<FTrayCard> OfferCards;
    TArray<FTrayCard> FinalCards;
    TArray<FTrayCard> FinalSourceCards;
    FTrayCard SkipCard;
    TArray<FTrayCard> BaseCards;
    TArray<FChoiceRow> ChoiceRows;
    TArray<FTrayCard> BlueprintCards;
    TArray<FBlueprintRow> BlueprintRows;
    ELDRestService ServiceContext=ELDRestService::None;
    UPROPERTY() TObjectPtr<UBorder> ServicePanel;
    UPROPERTY() TObjectPtr<UVerticalBox> ServiceBody;
    // The trainer's blueprint rows (learn or replace).
    UPROPERTY() TObjectPtr<UWidget> BlueprintSection;
    UPROPERTY() TObjectPtr<UVerticalBox> BlueprintList;
    // #198 rest services section.
    UPROPERTY() TObjectPtr<UWidget> RestSection;
    UPROPERTY() TObjectPtr<ULDCommandButton> NpcButton;
    UPROPERTY() TObjectPtr<UTextBlock> NpcBlocked;
    UPROPERTY() TObjectPtr<ULDCommandButton> RespecButton;
    UPROPERTY() TObjectPtr<UTextBlock> RespecBlocked;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> ForgetButtons;
    UPROPERTY() TObjectPtr<UTextBlock> ForgetBlocked;
    // #198 G FillGap and Dismantle: their service buttons, the target / filler pickers and the dismantle selection.
    UPROPERTY() TObjectPtr<ULDCommandButton> FillGapButton;
    UPROPERTY() TObjectPtr<UTextBlock> FillGapBlocked;
    UPROPERTY() TObjectPtr<UWrapBox> GapTargetLine;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> GapTargetButtons;
    UPROPERTY() TObjectPtr<UWrapBox> GapFillerLine;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> GapFillerButtons;
    UPROPERTY() TObjectPtr<ULDCommandButton> DismantleButton;
    UPROPERTY() TObjectPtr<UTextBlock> DismantleBlocked;
    UPROPERTY() TObjectPtr<UWidget> DismantleSection;
    UPROPERTY() TObjectPtr<UTextBlock> DismantleHeading;
    UPROPERTY() TObjectPtr<UVerticalBox> DismantleList;
    UPROPERTY() TArray<TObjectPtr<UHorizontalBox>> DismantleLines;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> DismantleButtons;
    UPROPERTY() TObjectPtr<UWidget> RestConfirmLine;
    UPROPERTY() TObjectPtr<UTextBlock> RestConfirmText;
    UPROPERTY() TObjectPtr<ULDHoldButton> RestConfirmButton;
    FRestConfirm RestConfirm;
    // Every widget built after RebuildWidget, kept alive with the tree.
    UPROPERTY() TArray<TObjectPtr<UWidget>> RowWidgets;
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
    TArray<FLDFinalModSourceView> ShownFinalSources;
    // The blueprints the trainer rows and the tray cards show, in the view's order.
    TArray<FName> ShownBlueprints;
    // #198 B: the opening base options the cards show, in the view's order.
    TArray<FName> ShownBaseOptions;
    // #198 G: the pending component choices the rows show (ids, oldest first) and the rest material services of the latest
    // view: the open pickers, the picked gap target and the components picked for a dismantle.
    TArray<FGuid> ShownChoices;
    LDHUD::FMaterialServicesView Materials;
    bool bFillGapOpen=false;
    FName GapTarget;
    bool bDismantleOpen=false;
    TArray<FName> DismantlePicked;
    // The owner's level, stats or catalog changed: every shown card face is rendered again.
    bool bCardsStale=true;
    // PreviewOffer's pinned card, whose popup shows while no card is hovered.
    int32 PinnedPreview=INDEX_NONE;
    uint64 PendingRequest=0;
    ELDBuildCommandKind PendingKind=ELDBuildCommandKind::None;
    uint32 Answers=0;
    bool bOpen=false;
    bool bServicePanelOpen=false;
    LDHUD::FChoicesFold Fold;
    double UiNow() const;
    bool Usable() const;
    // The lowest empty Q/W/E slot of the latest view, or 3 when all are full.
    uint8 FirstEmptySlot() const;
    // Builds one tray card whose click runs Click; the caller adds its Frame to a row.
    FTrayCard MakeCard(FName ButtonName,TFunction<void()> Click);
    // The face of Card needs rendering for Key (another definition or outcome, or new numbers).
    bool CardStale(const FTrayCard& Card,const FString& Key) const;
    // Fills a card's name, icon and popup for Key.
    void ShowFace(FTrayCard& Card,const FString& Key,const FString& Name,const FString& IconId,const FString& Glyph,
        const LDHUD::FActionDetailView& Detail);
    // Shows the card as a named card or an icon tile (bTile), enabled or greyed out.
    void ShowLook(FTrayCard& Card,bool bTile,bool bEnabled);
    FTrayCard& BaseCardAt(int32 Index);
    FChoiceRow& ChoiceRowAt(int32 Index);
    FTrayCard& BlueprintCardAt(int32 Index);
    FBlueprintRow& BlueprintRowAt(int32 Index);
    void RefreshOffer();
    void RefreshFinalMod();
    bool SubmitFinalMod(FName Option,const FLDFinalModSourceView& Source);
    void RefreshOpeningBase(bool bTiles);
    void RefreshComponentChoices(bool bTiles);
    void RefreshBlueprintCards();
    void RefreshBlueprints();
    void RefreshRest();
    // Why a rest service is refused now: the gate's reason, then the local checks (price, unspent points, an outstanding
    // offer, nothing allocated); empty when it may be sent.
    FString NpcRefusal() const;
    FString RespecRefusal() const;
    FString ReplaceRefusal() const;
    FString ForgetRefusal() const;
    // The confirm line's terms still hold in the latest view (the blueprint is pending, the slot holds the same active).
    bool RestConfirmHolds() const;
    // Why the confirm line's command would be refused now (the service's own refusal); empty when it may be sent.
    FString RestConfirmRefusal() const;
    ULDCommandButton* GapTargetButtonAt(int32 Index);
    ULDCommandButton* GapFillerButtonAt(int32 Index);
    ULDCommandButton* DismantleButtonAt(int32 Index);
    // The FillGap / Dismantle pickers and their confirm line still match the latest material services.
    void RefreshMaterials();
    void CloseMaterialPickers();
    void RefreshAll();
    bool Send(FLDBuildCommand Command);
    void ShowFinal(const FLDBuildReceipt& Receipt);
    void FinishRequest();
    // Toasts a result or refusal line (nothing for an empty one).
    void Tell(const FString& Text);
    // The latest result line, toasted when the request finishes.
    FString ResultLine;
};
