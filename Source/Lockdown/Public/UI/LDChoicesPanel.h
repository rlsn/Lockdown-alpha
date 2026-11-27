#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "UI/LDHUDModel.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchTimeline.h"
#include "LDChoicesPanel.generated.h"
struct FLDDemoContentDefinitions;

// The 构筑 command layer behind the build ring (ULDWorkshop draws every decision): the main panel the 构筑 key and the ring
// open and close (ULDUIRoot's Choices panel), and the owner of the offer, card offer, final mod, blueprint, rest service and
// rest shop commands. Every command carries the guards of the latest own view (offer ids and revisions, the build stage and
// life phase), one request is in flight at a time, and every result and refusal reaches the HUD's toast (Notify). It draws
// nothing and is never added to a canvas.
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
    // Called on every HUD refresh: the commands follow this view. Stage (the public match's build stage,
    // LDProgression::StageForPublic) and the life phase judge what the server would refuse. Keys: the key the HUD shows for
    // each Q/W/E slot; every line that names a slot uses it.
    void Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,LDProgression::EMatchStage Stage,
        const FLDDemoContentDefinitions* Content,const LDHUD::FSlotKeys& Keys,bool bDormRested);
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDBuildReceiptQueryResult& Result);
    // In a rest's build window the open ring offers the rest rebuilds (respec, replace, forget); the owning authority still
    // admits every command.
    bool IsServiceShown() const;
    // An offer option: chooses that active with bLearnIfPossible.
    bool ChooseOffer(int32 Index);
    bool SkipOffer();
    // A universal final option: commits at once.
    bool ChooseFinalMod(int32 Index);
    // Learns the pending blueprint at Index into the lowest empty Q/W/E slot.
    bool LearnBlueprintCard(int32 Index);
    // Learns the pending blueprint at Index into the empty slot Key (0 = Q).
    bool LearnBlueprint(int32 Index,uint8 Key);
    // #198 rest services. Respec sends at once; AskReplace (the blueprint at Index into the occupied slot Key) and AskForget
    // (the active in slot Key) open the confirm line, which ConfirmRestAction sends and CancelRestAction (or Back, or closing
    // the panel) withdraws. Each refuses locally with the reason it toasts.
    bool Respec();
    bool AskReplace(int32 Index,uint8 Key);
    bool AskForget(uint8 Key);
    // The trainer's right click on the skill host in Column (LDHUD::SlotColumn): asks to forget that slot's active. False (the
    // click is not taken) outside the rest services or on the body and dash columns.
    bool ForgetAtColumn(int32 Column);
    // The card offer (spec §6.4): a pick takes the card (installed on its form's place in the same commit when one is free,
    // LDProgression::DefaultCardTarget), reroll redraws the offer, skip lets it go.
    bool PickCardOffer(int32 Index);
    bool RerollCardOffer();
    bool SkipCardOffer();
    bool ConfirmRestAction();
    void CancelRestAction();
    // Spec §3.13 rest shop (a stocked shop in a rest while the ring is open): buying a slot, the skill point and a refresh send
    // at once, paid in unspent stat points. Each refuses locally with the reason it toasts.
    bool BuyShopSlot(int32 Index);
    bool BuyShopSkillPoint();
    bool BuyShopMedal();
    bool RefreshShopShelf();
    // The confirm line is a child confirmation for Back routing; it captures no input, so play goes on meanwhile.
    virtual bool HasChildConfirmation() const override {return RestConfirm.Kind!=ELDBuildCommandKind::None;}
    virtual bool CloseChildConfirmation() override;
    virtual bool ChildConfirmationYields() const override {return true;}
    virtual bool IsPanelOpen() const override {return bOpen;}
    // The 构筑 panel (the open build ring) is open.
    bool IsTrayOpen() const {return bOpen;}
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
    bool HasPendingRequest() const {return PendingRequest!=0;}
    // The open rest confirm line (替换 / 遗忘), with the reason it would be refused now; empty without one.
    FString ConfirmLine() const;
    // The HUD opened the ring for the opening offer (D10): it folds after a few seconds unless the player engages with it
    // (LDHUD::FChoicesFold).
    void MarkOpenedAutomatically();
    // Called on every HUD refresh with whether the own body was hit since the last one; folds an automatic opening when due.
    void UpdateFold(bool bOwnHit);
private:
    // A replace or forget waiting on the confirm line, with the terms it was asked under.
    struct FRestConfirm
    {
        ELDBuildCommandKind Kind=ELDBuildCommandKind::None;
        FName Blueprint;
        uint8 Slot=0;
        // The active the slot held when asked; another one there by confirm time withdraws the line instead.
        FName Active;
    };
    FRestConfirm RestConfirm;
    // The offer choice in flight, so its receipt can confirm the offer before the own view catches up.
    LDHUD::FOfferMotionTicket OfferTicket;
    FGuid ConfirmedOffer;
    uint32 ConfirmedMatch=0,ConfirmedLife=0;
    FLDProgressionView Latest;
    uint32 LatestMatch=0,LatestLife=0;
    LDProgression::EMatchStage LatestStage=LDProgression::EMatchStage::Closed;
    const FLDDemoContentDefinitions* LatestContent=nullptr;
    LDHUD::FSlotKeys SlotKeys;
    LDHUD::FBuildGateView Gate;
    // The decisions the commands act on, taken from the latest view.
    FGuid ShownOffer;
    FGuid ShownFinalMod;
    FLDFinalModSelection SubmittedFinal;
    bool bFinalFeedbackAllowed=false;
    TArray<FName> ShownBlueprints;
    FGuid ShownCardOffer;
    uint64 ShownCardRevision=0;
    TArray<FName> ShownCardOptions;
    uint64 PendingRequest=0;
    uint32 Answers=0;
    bool bOpen=false;
    LDHUD::FChoicesFold Fold;
    double UiNow() const;
    bool Usable() const;
    // A request in flight or a rest confirm line.
    bool IsEngaged() const;
    // The lowest empty Q/W/E slot of the latest view, or 3 when all are full.
    uint8 FirstEmptySlot() const;
    bool ConfirmedOfferStillShown() const;
    bool HasAvailableOffer() const;
    bool HasAvailableFinal() const;
    void TrackOffer(int32 Index);
    // The guards every card offer command carries (the shown offer's id and revision), or false with the toast.
    bool CardOfferCommand(ELDBuildCommandKind Kind,FLDBuildCommand& Out);
    bool IsShopOffered() const;
    bool ShopCommand(ELDBuildCommandKind Kind,FLDBuildCommand& Out);
    // Why a rest service is refused now: the gate's reason, then the local checks (unspent points, an outstanding offer,
    // nothing allocated); empty when it may be sent.
    FString RespecRefusal() const;
    FString ReplaceRefusal() const;
    FString ForgetRefusal() const;
    // The confirm line's terms still hold in the latest view (the blueprint is pending, the slot holds the same active).
    bool RestConfirmHolds() const;
    // Why the confirm line's command would be refused now (the service's own refusal); empty when it may be sent.
    FString RestConfirmRefusal() const;
    // Takes the decisions from the latest view and withdraws a confirm line whose terms changed.
    void Refresh();
    bool Send(FLDBuildCommand Command);
    void ShowFinal(const FLDBuildReceipt& Receipt);
    void FinishRequest();
    // Toasts a result or refusal line (nothing for an empty one).
    void Tell(const FString& Text);
    // The latest result line, toasted when the request finishes.
    FString ResultLine;
};
