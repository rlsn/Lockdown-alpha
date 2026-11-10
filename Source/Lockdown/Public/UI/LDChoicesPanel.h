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
// only stage locally until one confirm sends AllocatePoints. Every command carries the ids and versions of the view it was
// built from; nothing shown changes before the server's receipt and the next owner view. Buttons the server would refuse
// are disabled with the reason (LDHUD::BuildGate); the server still decides each command.
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
    // Stats: the owner's level and stats, so every card shows its numbers for now (#147). Phase, BuildSafeAtMs (the freshest
    // own publication of Combat's safe window), bClock and NowMs judge what the server would refuse, for the disabled reasons.
    void Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,ELDMatchPhase Phase,int64 BuildSafeAtMs,
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
    // A card being read (hovered, focused or previewed), staged points or a request in flight.
    bool IsEngaged() const {return DetailCard!=INDEX_NONE||StagedTotal()>0||PendingRequest!=0;}
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
    };
    struct FStatRow
    {
        TObjectPtr<UWidget> Row;
        TObjectPtr<UTextBlock> Name,PerPoint,Points;
        TObjectPtr<ULDCommandButton> Minus,Plus;
    };
    TArray<FCardWidgets> Cards;
    TArray<LDHUD::FActiveCardView> CardViews;
    TArray<FBlueprintRow> BlueprintRows;
    TArray<FStatRow> StatRows;
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
    ELDMatchPhase LatestPhase=ELDMatchPhase::Faulted;
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
    // Local staging of the point allocation, one count per stat in the stats' design order; never sent until ConfirmPoints.
    TArray<int32> Staged;
    uint32 StagedMatch=0;
    // PreviewOffer's pinned card, shown when no card is hovered or focused; DetailCard is the one on screen.
    int32 PinnedPreview=INDEX_NONE;
    int32 DetailCard=INDEX_NONE;
    bool bDetailStale=true;
    // The owner's level, stats or catalog changed: the shown cards' numbers are redrawn.
    bool bCardsStale=true;
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
    bool Send(FLDBuildCommand Command);
    void ShowFinal(const FLDBuildReceipt& Receipt);
    void FinishRequest();
    void UpdateDetail();
    void SetFeedback(const FString& Text);
    bool HasAnythingToDo() const;
};
