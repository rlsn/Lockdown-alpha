#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "UI/LDHUDModel.h"
#include "Development/LDGrowthProtocol.h"
#include "LDChoicesPanel.generated.h"
class UTextBlock;
class URichTextBlock;
class UBorder;
class USizeBox;
class UCanvasPanel;
class UCanvasPanelSlot;
struct FLDDemoContentDefinitions;

// Three-card level-up choice (APP_SPEC 4.2, UI 3.2): a normal main panel over one fixed server offer.
// Compact cards show a placeholder icon, the name and one kind/rank line; hovering or focusing a card pops its full
// description above it. Clicking a card submits it at once; the command carries stable OfferId/OptionId and versions,
// never a row index. A learn-active card with both Q/W filled starts the replacement pick instead (#97): the HUD's Q/W
// slots glow red and clicking one submits the replacement of that slot; no slot is picked by default, another card
// restarts from that card and Back cancels.
UCLASS()
class LOCKDOWN_API ULDChoicesPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    // Growth's owning-connection entry: returns the allocated request id, or 0 with a local refusal reason.
    TFunction<uint64(const FLDChoiceCommand&,ELDGrowthReason&)> SubmitChoice;
    // The id the next choice submit will receive (0 while one is in flight); a Listen Host answers inside the call.
    TFunction<uint64()> PeekRequestId;
    // Result notices for a request that ends while the panel is closed (the HUD shows them as a toast).
    TFunction<void(const FString&)> Notify;
    // Stats: the owner's level and stats, so every card shows its numbers for now (#147).
    void Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase Phase,const FLDDemoContentDefinitions* Content,
        const LDHUD::FCardStats& Stats);
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDChoiceReceiptQueryResult& Result);
    // A card click: submits that card, or starts the replacement pick when it needs one.
    bool ChooseCard(int32 Index);
    // Shows a card's detail popup without choosing it, as hovering does (automation and review captures).
    bool PreviewChoice(int32 Index);
    // A HUD Q/W slot click during the replacement pick: forgets that slot's skill and submits the replacement at once.
    bool ChooseReplacement(uint8 SkillSlot);
    // The replacement pick is waiting and this skill slot (0 Q, 1 W) holds an active that a click would replace.
    bool IsReplacementTarget(uint8 SkillSlot) const;
    // Submits the chosen card (or the chosen replacement); a card click and a slot click both end here.
    void ConfirmChoice();
    // 112 §6: spends one free reroll on the shown offer (one per offer).
    bool Reroll();
    virtual bool IsPanelOpen() const override {return bOpen;}
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
    virtual bool HasChildConfirmation() const override {return bReplacing;}
    virtual bool CloseChildConfirmation() override;
    // The replacement pick never holds the player still: it waits through play (Q/W keys cast as usual) for a HUD Q/W
    // slot click, Back, another card or the panel closing; the HUD holds its prompt toast meanwhile.
    virtual bool ChildConfirmationYields() const override {return bReplacing;}
    bool HasPendingRequest() const {return PendingRequest!=0;}
    // The HUD opened this panel for the opening offer (D10): it folds back to the pending pill after a few seconds unless
    // the player engages with it (LDHUD::FChoicesFold).
    void MarkOpenedAutomatically();
    // Called on every HUD refresh with whether the own body was hit since the last one; folds the open panel when due.
    void UpdateFold(bool bOwnHit);
    // A card being read (hovered, focused or previewed), a chosen card, the replacement confirmation or a request in flight.
    bool IsEngaged() const {return DetailCard!=INDEX_NONE||Selected!=INDEX_NONE||bReplacing||PendingRequest!=0;}
    // The card whose detail popup is showing, or INDEX_NONE.
    int32 GetDetailCard() const {return DetailCard;}
    // Distance of the panel's bottom edge from the screen bottom and of its left edge from the screen left: the HUD keeps
    // it in the blind-box button's row, right of the button (#97).
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
        TObjectPtr<USizeBox> IconSize;
        TObjectPtr<UBorder> IconShape;
        TObjectPtr<UTextBlock> Glyph,Name,Summary;
        // The rule sentence on the card face (#147; user 2026-11-07: shown on the card in full, keywords bold).
        TObjectPtr<URichTextBlock> Sentence;
    };
    TArray<FCardWidgets> Cards;
    // #97: the free reroll is a fourth card of the same shape.
    FCardWidgets RerollCard;
    TArray<LDHUD::FChoiceCardView> CardViews;
    UPROPERTY() TObjectPtr<UBorder> MainPanel;
    UPROPERTY() TObjectPtr<UBorder> DockFrame;
    float BottomDock=96.f,DockLeft=16.f;
    // The detail popup lives on its own full-panel canvas layer, so its size never moves the card column.
    UPROPERTY() TObjectPtr<UCanvasPanel> DetailLayer;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> DetailSlot;
    UPROPERTY() TObjectPtr<UBorder> Detail;
    UPROPERTY() TObjectPtr<UTextBlock> DetailName;
    UPROPERTY() TObjectPtr<UTextBlock> DetailSummary;
    UPROPERTY() TObjectPtr<URichTextBlock> DetailBody;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<ULDCommandButton> RerollButton;
    UPROPERTY() TArray<TObjectPtr<UWidget>> CardWidgets;
    FLDProgressionView Latest;
    uint32 LatestMatch=0,LatestLife=0;
    ELDLifePhase LatestPhase=ELDLifePhase::Unspawned;
    const FLDDemoContentDefinitions* LatestContent=nullptr;
    LDHUD::FCardStats LatestStats;
    FGuid ShownOffer;
    uint64 ShownOfferRevision=0;
    int32 Selected=INDEX_NONE;
    // PreviewChoice's pinned card, shown when no card is hovered or focused; DetailCard is the one on screen.
    int32 PinnedPreview=INDEX_NONE;
    int32 DetailCard=INDEX_NONE;
    bool bDetailStale=true;
    int32 ReplaceSlot=INDEX_NONE;
    FName ReplaceSkill;
    uint8 ReplaceRank=0;
    uint64 ReplaceBuildRevision=0;
    uint64 PendingRequest=0;
    uint32 Answers=0;
    FString PendingName;
    // The last request's result while the panel was open; toasted if the panel then closes itself (nothing left to pick).
    FString ResultNotice;
    bool bPendingReroll=false;
    bool bOpen=false;
    bool bReplacing=false;
    LDHUD::FChoicesFold Fold;
    double UiNow() const;
    void Send(const FLDChoiceCommand& Command);
    void ShowFinal(const FLDChoiceReceipt& Receipt);
    void FinishRequest();
    void RefreshSelection();
    void ResetReplacement();
    void UpdateDetail();
};
