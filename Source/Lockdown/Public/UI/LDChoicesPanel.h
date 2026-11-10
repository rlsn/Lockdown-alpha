#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "UI/LDHUDModel.h"
#include "Development/LDGrowthProtocol.h"
#include "LDChoicesPanel.generated.h"
class UTextBlock;
class URichTextBlock;
class UBorder;
class UImage;
class USizeBox;
class UCanvasPanel;
class UCanvasPanelSlot;
class UVerticalBox;
class UHorizontalBox;
struct FLDDemoContentDefinitions;

// #198 growth panel (APP_SPEC 4.2, UI 3.2), opened by the 开盲盒 button: the current active offer (up to three cards, a click
// is "学这个": an ActiveChoice that learns into the lowest empty Q/W/E slot when allowed, otherwise keeps it as a blueprint),
// the pending blueprints with a "学到 Q/W/E" button per empty slot, and the unspent points with one row per stat whose +/−
// only stage locally until one confirm sends AllocatePoints. #198 B adds the opening base pick (cards of the current options;
// a click is an OpeningBase that installs it when allowed) and the owned components (已启用, or a 启用 button for a first-time
// InstallComponent); B18 first asks for a learned active and an element. Every command carries the ids and versions of the view it was
// built from; nothing shown changes before the server's receipt and the next owner view. Buttons the server would refuse
// are disabled with the reason (LDHUD::BuildGate over LDProgression::StageForPublic, LDHUD::ComponentRow); the server still
// decides each command. In a rest it also shows the rest services: the NPC's active offer, the respec, "替换 Q/W/E" on each
// blueprint row for an occupied slot and "遗忘 Q/W/E" per learned active; replace and forget first show a confirm line
// naming the active that will be forgotten.
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
    // Stats: the owner's level and stats, so every card shows its numbers for now (#147). Stage (the public match's build
    // stage, LDProgression::StageForPublic), BuildSafeAtMs (the freshest own publication of Combat's safe window), bClock
    // and NowMs judge what the server would refuse, for the disabled reasons.
    void Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,LDProgression::EMatchStage Stage,int64 BuildSafeAtMs,
        bool bClock,int64 NowMs,const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats);
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDBuildReceiptQueryResult& Result);
    // An offer card click: chooses that active with bLearnIfPossible.
    bool ChooseOffer(int32 Index);
    // Shows an offer card's detail popup without choosing it, as hovering does (automation and review captures).
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
    bool ConfirmRestAction();
    void CancelRestAction();
    // The confirm line is a child confirmation for Back routing; it captures no input, so play goes on meanwhile.
    virtual bool HasChildConfirmation() const override {return RestConfirm.Kind!=ELDBuildCommandKind::None;}
    virtual bool CloseChildConfirmation() override;
    virtual bool ChildConfirmationYields() const override {return true;}
    // #198 B: an opening base card click: takes the option at Index of the shown list with bInstallIfPossible. A binding
    // component (B18) with a bindable learned active first asks for that active and an element (PickBindActive/Element).
    bool PickOpeningBase(int32 Index);
    // A component row's 启用: a first-time InstallComponent of the owned component at Index of the shown list (binding first
    // as above).
    bool EnableComponent(int32 Index);
    // The binding step of an opening pick or install: the bindable learned active at Index (Q/W/E order), then the element,
    // which sends the command.
    bool PickBindActive(int32 Index);
    bool PickBindElement(ELDDemoElement Element);
    void CancelBinding();
    int32 StagedTotal() const;
    virtual bool IsPanelOpen() const override {return bOpen;}
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
    bool HasPendingRequest() const {return PendingRequest!=0;}
    // The HUD opened this panel for the opening offer (D10): it folds back to the 开盲盒 button after a few seconds
    // unless the player engages with it (LDHUD::FChoicesFold).
    void MarkOpenedAutomatically();
    // Called on every HUD refresh with whether the own body was hit since the last one; folds the open panel when due.
    void UpdateFold(bool bOwnHit);
    // A card being read (hovered, focused or previewed), staged points, a binding being picked, a rest confirm line or a
    // request in flight.
    bool IsEngaged() const {return DetailCard!=INDEX_NONE||StagedTotal()>0||!BindingComponent.IsNone()||PendingRequest!=0||HasChildConfirmation();}
    // The offer card whose detail popup is showing, or INDEX_NONE.
    int32 GetDetailCard() const {return DetailCard;}
    // Distance of the panel's bottom edge from the screen bottom and of its left edge from the screen left: the HUD keeps
    // it in the 开盲盒 button's row, right of the button (#97).
    void SetBottomDock(float Bottom,float Left);
    // Distance of the panel's top edge from the screen bottom (last layout), so the HUD can stack the toast above it.
    float PanelTop() const;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float Delta) override;
    virtual UWidget* FocusScope() const override;
    virtual UWidget* HitArea() const override;
private:
    struct FCardWidgets
    {
        TObjectPtr<ULDCommandButton> Button;
        // The card's icon (LDUIIcons); without its texture, a plain square with the name's first character instead.
        TObjectPtr<UImage> Picture;
        TObjectPtr<USizeBox> IconSize;
        TObjectPtr<UBorder> IconShape;
        TObjectPtr<UTextBlock> Glyph,Name,Summary,Action;
        // The rule sentence on the card face (#147; user 2026-11-07: shown on the card in full, keywords bold).
        TObjectPtr<URichTextBlock> Sentence;
    };
    struct FBlueprintRow
    {
        TObjectPtr<UWidget> Row;
        TObjectPtr<UTextBlock> Name,Summary,Blocked;
        TArray<TObjectPtr<ULDCommandButton>> Learn;
        // #198 rest only: "替换 Q/W/E" for each occupied slot.
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
        TObjectPtr<URichTextBlock> Sentence;
        FString AppliedIcon;
        FName ShownId;
        bool bBinding=false;
    };
    // One owned component: its name and sentence, then 已启用 or a 启用 button with the reason it is disabled.
    struct FComponentRow
    {
        TObjectPtr<UWidget> Row;
        TObjectPtr<UTextBlock> Name,Summary,State,Blocked;
        TObjectPtr<ULDCommandButton> Enable;
        FName ShownId;
    };
    TArray<FCardWidgets> Cards;
    TArray<LDHUD::FActiveCardView> CardViews;
    TArray<FBlueprintRow> BlueprintRows;
    TArray<FStatRow> StatRows;
    TArray<FBaseCard> BaseCards;
    TArray<FComponentRow> ComponentRows;
    UPROPERTY() TObjectPtr<UWidget> OpeningBaseSection;
    UPROPERTY() TObjectPtr<UVerticalBox> BaseCardList;
    // Three cards per line; a line is added when a card beyond the last one is needed.
    UPROPERTY() TArray<TObjectPtr<UHorizontalBox>> BaseCardLines;
    UPROPERTY() TObjectPtr<UWidget> BindingSection;
    UPROPERTY() TObjectPtr<UTextBlock> BindingHeading;
    UPROPERTY() TObjectPtr<UWidget> BindElementLine;
    UPROPERTY() TObjectPtr<UTextBlock> BindingBlocked;
    // Q/W/E order of the bindable learned actives; then fire, water, electric.
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> BindActiveButtons;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> BindElementButtons;
    UPROPERTY() TObjectPtr<UWidget> ComponentSection;
    UPROPERTY() TObjectPtr<UVerticalBox> ComponentList;
    UPROPERTY() TObjectPtr<UBorder> MainPanel;
    UPROPERTY() TObjectPtr<UBorder> DockFrame;
    float BottomDock=96.f,DockLeft=16.f;
    UPROPERTY() TObjectPtr<UTextBlock> OfferHeading;
    UPROPERTY() TObjectPtr<UTextBlock> OfferNote;
    UPROPERTY() TObjectPtr<UWidget> OfferSection;
    UPROPERTY() TObjectPtr<UHorizontalBox> CardRow;
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
    UPROPERTY() TObjectPtr<UWidget> RestConfirmLine;
    UPROPERTY() TObjectPtr<UTextBlock> RestConfirmText;
    UPROPERTY() TObjectPtr<ULDCommandButton> RestConfirmButton;
    FRestConfirm RestConfirm;
    // The detail popup lives on its own full-panel canvas layer, so its size never moves the card column.
    UPROPERTY() TObjectPtr<UCanvasPanel> DetailLayer;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> DetailSlot;
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
    bool bLatestClock=false;
    int64 LatestNowMs=0;
    int64 LatestSafeAtMs=0;
    const FLDDemoContentDefinitions* LatestContent=nullptr;
    LDHUD::FCardStats LatestStats;
    LDHUD::FBuildGateView Gate;
    FGuid ShownOffer;
    uint64 ShownOfferRevision=0;
    // The blueprints the rows show, in the view's order.
    TArray<FName> ShownBlueprints;
    // #198 B: the opening base options and owned components the cards and rows show, in the view's order.
    TArray<FName> ShownBaseOptions;
    TArray<FName> ShownComponents;
    // A binding component (B18) waiting for its active and element: the command it will send and the choices so far.
    FName BindingComponent;
    ELDBuildCommandKind BindingKind=ELDBuildCommandKind::None;
    TArray<FName> BindingActives;
    FName BindingActive;
    // Local staging of the point allocation, one count per stat in the stats' design order; never sent until ConfirmPoints.
    TArray<int32> Staged;
    uint32 StagedMatch=0;
    // PreviewOffer's pinned card, shown when no card is hovered or focused; DetailCard is the one on screen.
    int32 PinnedPreview=INDEX_NONE;
    int32 DetailCard=INDEX_NONE;
    bool bDetailStale=true;
    // The owner's level, stats or catalog changed: the shown cards' numbers are redrawn.
    bool bCardsStale=true;
    // The same for the opening base cards and component rows (cleared after each full refresh).
    bool bComponentTextStale=true;
    uint64 PendingRequest=0;
    ELDBuildCommandKind PendingKind=ELDBuildCommandKind::None;
    uint32 Answers=0;
    // The last request's result while the panel was open; toasted if the panel then closes itself (nothing left to do).
    FString ResultNotice;
    bool bOpen=false;
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
    FBaseCard& BaseCardAt(int32 Index);
    FComponentRow& ComponentRowAt(int32 Index);
    void RefreshOpeningBase();
    void RefreshComponents();
    void RefreshBinding();
    void RefreshAll();
    // Starts an OpeningBase or InstallComponent of Component: straight away, or after the binding step for B18.
    bool BeginComponent(ELDBuildCommandKind Kind,FName Component);
    bool SendComponent(ELDBuildCommandKind Kind,FName Component,FName BoundActive,ELDDemoElement Element);
    bool Send(FLDBuildCommand Command);
    void ShowFinal(const FLDBuildReceipt& Receipt);
    void FinishRequest();
    void UpdateDetail();
    void SetFeedback(const FString& Text);
    bool HasAnythingToDo() const;
};
