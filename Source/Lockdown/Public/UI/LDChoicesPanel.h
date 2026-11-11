#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "UI/LDHUDModel.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchTimeline.h"
#include "LDChoicesPanel.generated.h"
class UTextBlock;
class URichTextBlock;
class UBorder;
class UImage;
class USizeBox;
class UVerticalBox;
class UHorizontalBox;
class UWrapBox;
struct FLDDemoContentDefinitions;

// A bounded task workspace and contextual NPC service panel.
// All mutations retain the existing build commands, revision checks, confirmations and authority receipts.
UCLASS()
class LOCKDOWN_API ULDChoicesPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    // Growth's owning-connection entry: returns the allocated request id, or 0 with a local refusal reason.
    TFunction<uint64(const FLDBuildCommand&,ELDGrowthReason&)> SubmitBuild;
    // The id the next build submit will receive (0 while one is in flight); a Listen Host answers inside the call.
    TFunction<uint64()> PeekRequestId;
    // Result notices for a request that ends while the panel is closed (the HUD shows them as a toast).
    TFunction<void(const FString&)> Notify;
    TFunction<void()> ResetTraining;
    TFunction<void()> ReadyRest;
    // Stats: the owner's level and stats, so every card shows its numbers for now (#147). Stage (the public match's build
    // stage, LDProgression::StageForPublic) and the life phase judge what the server would refuse, for the disabled reasons.
    void Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,LDProgression::EMatchStage Stage,
        const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats);
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDBuildReceiptQueryResult& Result);
    void PresentAttributes(const FLDCombatOwnerView* Combat);
    void PresentRestActions(bool bRest,bool bCanReset,bool bCanReady,const FString& ReadyLabel);
    // Local view context only; the owning authority still admits every service command.
    void SetServiceContext(ELDRestService Service);
    bool OpenServicePanel();
    // 0: 技能与奖励 (offers, blueprints, the opening base pick and elite component choices); 1: 加点.
    void ShowTab(int32 Tab);
    // An offer card click: chooses that active with bLearnIfPossible.
    bool ChooseOffer(int32 Index);
    // Shows an offer in the inspection column without choosing it (automation and review captures).
    bool PreviewOffer(int32 Index);
    // A blueprint row's "学到 Q/W/E": learns the blueprint at Index of the shown list into the empty slot Key (0 = Q).
    bool LearnBlueprint(int32 Index,uint8 Key);
    // Stages one point more (+1) or less (-1) on the stat at Index of the catalog; local until ConfirmPoints.
    bool StagePoint(int32 Index,int32 Delta);
    // Sends the staged points as one AllocatePoints.
    bool ConfirmPoints();
    // #198 rest services. BuyNpcOffer and Respec send at once; AskReplace (the blueprint at Index of the shown list into the
    // occupied slot Key) and AskForget (the active in slot Key) show the confirm line, which ConfirmRestAction sends and
    // CancelRestAction (or Back, or closing the panel) withdraws. Each refuses locally with the reason the panel shows.
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
    int32 StagedTotal() const;
    virtual bool IsPanelOpen() const override {return bOpen||bServicePanelOpen;}
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
    bool HasPendingRequest() const {return PendingRequest!=0;}
    // The HUD opened this panel for the opening offer (D10): it folds back to the 构筑 button after a few seconds
    // unless the player engages with it (LDHUD::FChoicesFold).
    void MarkOpenedAutomatically();
    // Called on every HUD refresh with whether the own body was hit since the last one; folds an automatic opening when due.
    void UpdateFold(bool bOwnHit);
    // A card being read, staged points, a rest confirm line or a request in flight.
    bool IsEngaged() const {return DetailCard!=INDEX_NONE||StagedTotal()>0||PendingRequest!=0||HasChildConfirmation()
        ||bFillGapOpen||bDismantleOpen||(RestTrainingButton&&RestTrainingButton->HasKeyboardFocus())||(RestReadyButton&&RestReadyButton->HasKeyboardFocus());}
    // The offer card whose inspection is showing, or INDEX_NONE.
    int32 GetDetailCard() const {return DetailCard;}
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float Delta) override;
    virtual UWidget* FocusScope() const override;
    virtual UWidget* HitArea() const override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FLDDemoOpponentProbe;
#endif
    struct FCardWidgets
    {
        TObjectPtr<ULDCommandButton> Button;
        // The card's icon (LDUIIcons); without its texture, a plain square with the name's first character instead.
        TObjectPtr<UImage> Picture;
        TObjectPtr<USizeBox> IconSize;
        TObjectPtr<UBorder> IconShape;
        TObjectPtr<UTextBlock> Glyph,Name,Summary,Action;
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
    struct FStatRow
    {
        TObjectPtr<UWidget> Row;
        TObjectPtr<UTextBlock> Name,PerPoint,Points;
        TObjectPtr<ULDCommandButton> Minus,Plus;
    };
    // #198 B: one opening base option card (icon, name, the rendered sentence and what a click does).
    struct FBaseCard
    {
        TObjectPtr<UWidget> Frame;
        TObjectPtr<ULDCommandButton> Button;
        TObjectPtr<UImage> Picture;
        TObjectPtr<UTextBlock> Glyph,Name,Action;
        FString AppliedIcon;
        FName ShownId;
        bool bBinding=false;
    };
    // #198 G: one pending elite camp component choice: its title, one card per option and the empty-pool points button.
    struct FChoiceLine
    {
        TObjectPtr<UWidget> Row;
        TObjectPtr<UTextBlock> Title;
        TArray<FBaseCard> Cards;
        TObjectPtr<ULDCommandButton> Points;
    };
    TArray<FCardWidgets> Cards;
    TArray<LDHUD::FActiveCardView> CardViews;
    TArray<FBlueprintRow> BlueprintRows;
    TArray<FStatRow> StatRows;
    TArray<FBaseCard> BaseCards;
    TArray<FChoiceLine> ChoiceLines;
    UPROPERTY() TObjectPtr<UWidget> ComponentChoiceSection;
    UPROPERTY() TObjectPtr<UVerticalBox> ComponentChoiceList;
    UPROPERTY() TObjectPtr<UWidget> OpeningBaseSection;
    UPROPERTY() TObjectPtr<UVerticalBox> BaseCardList;
    UPROPERTY() TObjectPtr<UTextBlock> EmptyNote;
    UPROPERTY() TObjectPtr<UVerticalBox> PointsDock;
    UPROPERTY() TObjectPtr<UWidget> CurrentBuildSection;
    UPROPERTY() TObjectPtr<UTextBlock> AttributeSummary;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> EquippedNames;
    UPROPERTY() TArray<TObjectPtr<UImage>> EquippedIcons;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> TabButtons;
    int32 ActiveTab=0;
    ELDRestService ServiceContext=ELDRestService::None;
    UPROPERTY() TObjectPtr<UBorder> MainPanel;
    UPROPERTY() TObjectPtr<UBorder> ServicePanel;
    UPROPERTY() TObjectPtr<UVerticalBox> TaskContent;
    UPROPERTY() TObjectPtr<UVerticalBox> TaskBody;
    UPROPERTY() TObjectPtr<UVerticalBox> ServiceBody;
    UPROPERTY() TObjectPtr<ULDCommandButton> RestTrainingButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> RestReadyButton;
    UPROPERTY() TObjectPtr<UTextBlock> OfferHeading;
    UPROPERTY() TObjectPtr<UTextBlock> OfferNote;
    UPROPERTY() TObjectPtr<UWidget> OfferSection;
    UPROPERTY() TObjectPtr<UVerticalBox> CardRow;
    UPROPERTY() TObjectPtr<UWidget> BlueprintSection;
    UPROPERTY() TObjectPtr<UVerticalBox> BlueprintList;
    UPROPERTY() TObjectPtr<UWidget> PointsSection;
    UPROPERTY() TObjectPtr<UTextBlock> PointsHeading;
    UPROPERTY() TObjectPtr<UVerticalBox> StatList;
    UPROPERTY() TObjectPtr<ULDCommandButton> ConfirmButton;
    UPROPERTY() TObjectPtr<UTextBlock> PointsBlocked;
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
    UPROPERTY() TObjectPtr<ULDCommandButton> RestConfirmButton;
    FRestConfirm RestConfirm;
    // A bounded inspection column, separate from the independently scrolling inventory.
    UPROPERTY() TObjectPtr<UBorder> Detail;
    UPROPERTY() TObjectPtr<UTextBlock> DetailName;
    UPROPERTY() TObjectPtr<UTextBlock> DetailSummary;
    UPROPERTY() TObjectPtr<URichTextBlock> DetailBody;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    // Every widget built after RebuildWidget, kept alive with the tree.
    UPROPERTY() TArray<TObjectPtr<UWidget>> RowWidgets;
    FLDProgressionView Latest;
    uint32 LatestMatch=0,LatestLife=0;
    ELDLifePhase LatestLifePhase=ELDLifePhase::Unspawned;
    LDProgression::EMatchStage LatestStage=LDProgression::EMatchStage::Closed;
    const FLDDemoContentDefinitions* LatestContent=nullptr;
    LDHUD::FCardStats LatestStats;
    LDHUD::FBuildGateView Gate;
    FGuid ShownOffer;
    uint64 ShownOfferRevision=0;
    // The blueprints the rows show, in the view's order.
    TArray<FName> ShownBlueprints;
    // #198 B: the opening base options the cards show, in the view's order.
    TArray<FName> ShownBaseOptions;
    // #198 G: the pending component choices the lines show (ids, oldest first) and the rest material services of the latest
    // view: the open pickers, the picked gap target and the components picked for a dismantle.
    TArray<FGuid> ShownChoices;
    LDHUD::FMaterialServicesView Materials;
    bool bFillGapOpen=false;
    FName GapTarget;
    bool bDismantleOpen=false;
    TArray<FName> DismantlePicked;
    // Local staging of the point allocation, one count per stat in the stats' design order; never sent until ConfirmPoints.
    TArray<int32> Staged;
    uint32 StagedMatch=0;
    // PreviewOffer's pinned card, shown when no card is hovered or focused; DetailCard is the one on screen.
    int32 PinnedPreview=INDEX_NONE;
    int32 DetailCard=INDEX_NONE;
    // The owner's level, stats or catalog changed: the shown cards' numbers are redrawn.
    bool bCardsStale=true;
    // The same for the opening base and elite choice cards (cleared after each full refresh).
    bool bComponentTextStale=true;
    uint64 PendingRequest=0;
    ELDBuildCommandKind PendingKind=ELDBuildCommandKind::None;
    uint32 Answers=0;
    // The last request's result while the panel was open; toasted if the panel then closes itself (nothing left to do).
    FString ResultNotice;
    bool bOpen=false;
    bool bServicePanelOpen=false;
    LDHUD::FChoicesFold Fold;
    double UiNow() const;
    bool Usable() const;
    void BuildStatRows();
    FBlueprintRow& BlueprintRowAt(int32 Index);
    void RefreshOffer();
    void RefreshBlueprints();
    void RefreshPoints();
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
    FBaseCard& BaseCardAt(int32 Index);
    // Builds one component card face (icon, name, sentence, action) whose click runs Click; the caller places its Frame.
    void BuildComponentCard(FBaseCard& Card,FName ButtonName,TFunction<void()> Click);
    // Fills a card's face for Id (only when another id or new numbers are shown).
    void ShowComponentCard(FBaseCard& Card,FName Id);
    FChoiceLine& ChoiceLineAt(int32 Index);
    void RefreshComponentChoices();
    ULDCommandButton* GapTargetButtonAt(int32 Index);
    ULDCommandButton* GapFillerButtonAt(int32 Index);
    ULDCommandButton* DismantleButtonAt(int32 Index);
    // The FillGap / Dismantle pickers and their confirm line still match the latest material services.
    void RefreshMaterials();
    void CloseMaterialPickers();
    void RefreshOpeningBase();
    void RefreshAll();
    bool Send(FLDBuildCommand Command);
    void ShowFinal(const FLDBuildReceipt& Receipt);
    void FinishRequest();
    void UpdateDetail();
    void SetFeedback(const FString& Text);
};
