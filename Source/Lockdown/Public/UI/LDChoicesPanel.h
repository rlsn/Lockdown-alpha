#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "Development/LDGrowthProtocol.h"
#include "LDChoicesPanel.generated.h"
class UTextBlock;
class UBorder;
class UVerticalBox;
struct FLDDemoContentDefinitions;

// Three-card level-up choice (APP_SPEC 4.2, UI 3.2): a normal main panel over one fixed server offer.
// Cards only preview; the command carries stable OfferId/OptionId and versions, never a row index. A learn-active card
// with both Q/W filled opens the replacement child confirmation, which never picks a slot by default.
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
    void Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase Phase,const FLDDemoContentDefinitions* Content);
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDChoiceReceiptQueryResult& Result);
    bool PreviewChoice(int32 Index);
    bool PreviewReplacement(uint8 SkillSlot);
    void ConfirmChoice();
    virtual bool IsPanelOpen() const override {return bOpen;}
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
    virtual bool HasChildConfirmation() const override {return bReplacing;}
    virtual bool CloseChildConfirmation() override;
    bool HasPendingRequest() const {return PendingRequest!=0;}
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual UWidget* FocusScope() const override;
    virtual UWidget* HitArea() const override;
private:
    struct FCardWidgets
    {
        TObjectPtr<ULDCommandButton> Button;
        TObjectPtr<UTextBlock> Kind,Name,Rank,Body;
    };
    TArray<FCardWidgets> Cards;
    UPROPERTY() TObjectPtr<UBorder> MainPanel;
    UPROPERTY() TObjectPtr<UBorder> ReplacePanel;
    UPROPERTY() TObjectPtr<class UScrollBox> CardScroll;
    UPROPERTY() TObjectPtr<UTextBlock> Header;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UTextBlock> ReplaceText;
    UPROPERTY() TObjectPtr<ULDCommandButton> ConfirmButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> ReplaceSlotButtons[2];
    UPROPERTY() TObjectPtr<ULDCommandButton> ReplaceConfirmButton;
    UPROPERTY() TArray<TObjectPtr<UWidget>> CardWidgets;
    FLDProgressionView Latest;
    uint32 LatestMatch=0,LatestLife=0;
    ELDLifePhase LatestPhase=ELDLifePhase::Unspawned;
    const FLDDemoContentDefinitions* LatestContent=nullptr;
    FGuid ShownOffer;
    uint64 ShownOfferRevision=0;
    int32 Selected=INDEX_NONE;
    int32 ReplaceSlot=INDEX_NONE;
    FName ReplaceSkill;
    uint8 ReplaceRank=0;
    uint64 ReplaceBuildRevision=0;
    uint64 PendingRequest=0;
    uint32 Answers=0;
    FString PendingName;
    bool bOpen=false;
    bool bReplacing=false;
    void Send(const FLDChoiceCommand& Command);
    void ShowFinal(const FLDChoiceReceipt& Receipt);
    void FinishRequest();
    void RefreshSelection();
    void ResetReplacement();
};
