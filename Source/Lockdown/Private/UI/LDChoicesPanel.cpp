#include "UI/LDChoicesPanel.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDTerms.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDBuildBlueprintComponent.h"
#include "Engine/World.h"

void ULDChoicesPanel::Tell(const FString& Text)
{
    if(!Text.IsEmpty()&&Notify){Notify(Text);}
}
void ULDChoicesPanel::OpenPanel()
{
    if(const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->BuildBlueprints&&PC->BuildBlueprints->IsViewerOpen())
    {PC->BuildBlueprints->ToggleViewer();}
    bOpen=true;Fold.Opened(false,UiNow());
}
bool ULDChoicesPanel::ClosePanel()
{
    // Closing only closes the ring: the offer, the blueprints and any submitted request keep their state; an unconfirmed
    // replace or forget is withdrawn.
    RestConfirm=FRestConfirm();
    bOpen=false;bFinalFeedbackAllowed=false;
    if(!PendingRequest){OfferTicket=LDHUD::FOfferMotionTicket();}
    Fold.Closed();Refresh();return true;
}
FString ULDChoicesPanel::ConfirmLine() const
{
    if(RestConfirm.Kind==ELDBuildCommandKind::None){return FString();}
    const auto Name=[this](FName Id){return LDHUD::ContentName(LatestContent,Id.ToString());};
    FString Line=RestConfirm.Kind==ELDBuildCommandKind::ReplaceActive
        ?LDTerms::ReplaceConfirm(Name(RestConfirm.Blueprint),SlotKeys.Of(RestConfirm.Slot),Name(RestConfirm.Active))
        :LDTerms::ForgetConfirm(SlotKeys.Of(RestConfirm.Slot),Name(RestConfirm.Active));
    const FString Refusal=RestConfirmRefusal();
    if(!Refusal.IsEmpty()){Line+=TEXT("（")+Refusal+TEXT("）");}
    return Line;
}
bool ULDChoicesPanel::IsServiceShown() const
{
    // Only a rest's build windows (build and settlement/payout) offer the rest rebuilds; the locked steps and the return take them away.
    return bOpen&&LatestStage==LDProgression::EMatchStage::Rest;
}
double ULDChoicesPanel::UiNow() const{const UWorld* World=GetWorld();return World?World->GetRealTimeSeconds():0.;}
void ULDChoicesPanel::MarkOpenedAutomatically()
{
    if(!bOpen){return;}
    Fold.Opened(true,UiNow());
    UE_LOG(LogLDUI,Log,TEXT("LD_HUD_GROWTH_AUTO_OPEN The 构筑 tray opened for the opening offer or the opening base pick."));
}
void ULDChoicesPanel::UpdateFold(bool bOwnHit)
{
    if(bOwnHit){bFinalFeedbackAllowed=false;}
    // #222: only an automatic opening folds by itself; a ring the player opened stays, in combat and in a PK too.
    if(bOpen&&Fold.ShouldFold(UiNow(),IsEngaged(),bOwnHit))
    {
        UE_LOG(LogLDUI,Log,TEXT("LD_HUD_CHOICES_FOLDED %s"),bOwnHit?TEXT("own hit"):TEXT("automatic open timed out"));
        ClosePanel();
    }
}
bool ULDChoicesPanel::IsEngaged() const
{
    return PendingRequest!=0||HasChildConfirmation();
}
bool ULDChoicesPanel::Usable() const
{
    return Latest.bValid&&LatestMatch!=0&&Latest.Generation==LatestMatch&&Latest.Life==LatestLife;
}
uint8 ULDChoicesPanel::FirstEmptySlot() const
{
    for(uint8 Key=0;Key<3;++Key){if(LDHUD::ActiveInSlot(Latest,Key).IsNone()){return Key;}}
    return 3;
}
bool ULDChoicesPanel::ConfirmedOfferStillShown() const
{
    return ConfirmedOffer.IsValid()&&LatestMatch==ConfirmedMatch&&LatestLife==ConfirmedLife
        &&Latest.bHasActiveOffer&&Latest.ActiveOffer.OfferId==ConfirmedOffer;
}
bool ULDChoicesPanel::HasAvailableOffer() const
{
    return Usable()&&Latest.bHasActiveOffer&&!Latest.ActiveOffer.Options.IsEmpty()&&!ConfirmedOfferStillShown();
}
bool ULDChoicesPanel::HasAvailableFinal() const
{
    return Usable()&&!ConfirmedOfferStillShown()&&Latest.bFinalModPending
        &&(Latest.FinalModOptions.Num()==3||Latest.FinalModOptions.Num()==4);
}
void ULDChoicesPanel::TrackOffer(int32 Index)
{
    OfferTicket=LDHUD::FOfferMotionTicket();
    const bool bSkip=Index==INDEX_NONE;
    if(!bSkip&&!Latest.ActiveOffer.Options.IsValidIndex(Index)){return;}
    OfferTicket.Match=LatestMatch;OfferTicket.Life=LatestLife;
    OfferTicket.Offer=Latest.ActiveOffer.OfferId;
    OfferTicket.Kind=bSkip?ELDBuildCommandKind::SkipActiveOffer:ELDBuildCommandKind::ActiveChoice;
    if(!bSkip){OfferTicket.Option=Latest.ActiveOffer.Options[Index];}
}
void ULDChoicesPanel::Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,LDProgression::EMatchStage Stage,
    const FLDDemoContentDefinitions* Content,const LDHUD::FSlotKeys& Keys,bool bDormRested)
{
    if(OfferTicket.Match!=0&&(OfferTicket.Match!=Match||OfferTicket.Life!=Life)){OfferTicket=LDHUD::FOfferMotionTicket();}
    if(ConfirmedOffer.IsValid()&&(ConfirmedMatch!=Match||ConfirmedLife!=Life))
    {ConfirmedOffer.Invalidate();ConfirmedMatch=ConfirmedLife=0;}
    SlotKeys=Keys;
    Latest=View;LatestMatch=Match;LatestLife=Life;LatestStage=Stage;LatestContent=Content;
    Gate=Usable()?LDHUD::BuildGate(Stage,LifePhase,bDormRested):LDHUD::FBuildGateView();
    Refresh();
}
void ULDChoicesPanel::Refresh()
{
    // The ring shows exactly the current view: the guards every command carries follow it.
    ShownBlueprints=Usable()?Latest.PendingBlueprints:TArray<FName>();
    ShownOffer=HasAvailableOffer()?Latest.ActiveOffer.OfferId:FGuid();
    ShownFinalMod=HasAvailableFinal()?Latest.FinalModOfferId:FGuid();
    const bool bCards=Usable()&&Latest.bHasCardOffer&&!Latest.CardOffer.Options.IsEmpty();
    ShownCardOffer=bCards?Latest.CardOffer.OfferId:FGuid();ShownCardRevision=bCards?Latest.CardOffer.OfferRevision:0;
    ShownCardOptions=bCards?Latest.CardOffer.Options:TArray<FName>();
    // A confirm line whose terms changed (another slot content, a used blueprint, the rest over) is withdrawn, never resent.
    const bool bRest=Usable()&&(Gate.bInRest||Gate.bDormSwap);
    if(RestConfirm.Kind!=ELDBuildCommandKind::None&&(!bRest||!RestConfirmHolds()))
    {
        RestConfirm=FRestConfirm();
        if(bRest){Tell(TEXT("技能槽或图纸已变化，请重新选择。"));}
    }
}
bool ULDChoicesPanel::CardOfferCommand(ELDBuildCommandKind Kind,FLDBuildCommand& Out)
{
    if(!Usable()||!Latest.bHasCardOffer||Latest.CardOffer.OfferId!=ShownCardOffer){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanCardOffer){Tell(TEXT("没有提交：")+Gate.CardOfferBlocked);return false;}
    Out=FLDBuildCommand();
    Out.Kind=Kind;Out.OfferId=ShownCardOffer;Out.OfferRevision=ShownCardRevision;
    return true;
}
bool ULDChoicesPanel::PickCardOffer(int32 Index)
{
    FLDBuildCommand Command;
    if(!ShownCardOptions.IsValidIndex(Index)||!CardOfferCommand(ELDBuildCommandKind::PickCard,Command)){return false;}
    const FName Id=ShownCardOptions[Index];
    if(!FLDDefinitionWire::Encode(Id,Command.Option)){Tell(TEXT("没有提交：这张牌的标识无效。"));return false;}
    // Installed in the same commit on its form's place when there is one and a slot is free; the library keeps it otherwise.
    FLDCardTarget Target;
    if(LatestContent&&!LDHUD::WorkshopFull(Latest,LatestContent)
        &&LDProgression::DefaultCardTarget(*LatestContent,LDProgression::BuildFromView(*LatestContent,Latest),Id,Target))
    {
        Command.bInstallIfPossible=true;Command.CardTarget=Target.Kind;Command.Element=Target.Element;
        if(!Target.Active.IsNone()&&!FLDDefinitionWire::Encode(Target.Active,Command.TargetDefinition)){Tell(TEXT("没有提交：技能标识无效。"));return false;}
    }
    LDWidgets::PlayCue(this,ELDAudioCue::UIChoose);
    return Send(Command);
}
bool ULDChoicesPanel::RerollCardOffer()
{
    FLDBuildCommand Command;
    if(!CardOfferCommand(ELDBuildCommandKind::RerollCards,Command)){return false;}
    if(Latest.CardRerolls<=0){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::NoReroll));return false;}
    return Send(Command);
}
bool ULDChoicesPanel::SkipCardOffer()
{
    FLDBuildCommand Command;
    return CardOfferCommand(ELDBuildCommandKind::SkipCards,Command)&&Send(Command);
}
bool ULDChoicesPanel::ChooseOffer(int32 Index)
{
    if(!HasAvailableOffer()||!Latest.ActiveOffer.Options.IsValidIndex(Index)||Latest.ActiveOffer.OfferId!=ShownOffer){return false;}
    // One click is one choice: a click while a request is in flight never queues a second one.
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanChoose){Tell(TEXT("没有提交：")+Gate.ChooseBlocked);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::ActiveChoice;
    Command.OfferId=Latest.ActiveOffer.OfferId;Command.OfferRevision=Latest.ActiveOffer.OfferRevision;
    if(!FLDDefinitionWire::Encode(Latest.ActiveOffer.Options[Index],Command.Option)){Tell(TEXT("没有提交：这个技能的标识无效。"));return false;}
    // Always ask to learn now: the server learns it into the lowest empty slot when it may, otherwise keeps it as a blueprint.
    Command.bLearnIfPossible=true;Command.Slot=FLDBuildCommand::AnySlot;
    TrackOffer(Index);
    if(Send(Command)){return true;}
    OfferTicket=LDHUD::FOfferMotionTicket();return false;
}
bool ULDChoicesPanel::SkipOffer()
{
    if(!HasAvailableOffer()||Latest.ActiveOffer.OfferId!=ShownOffer||Latest.ActiveOffer.SkipPoints<=0){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanChoose){Tell(TEXT("没有提交：")+Gate.ChooseBlocked);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::SkipActiveOffer;
    Command.OfferId=Latest.ActiveOffer.OfferId;Command.OfferRevision=Latest.ActiveOffer.OfferRevision;
    TrackOffer(INDEX_NONE);
    if(Send(Command)){return true;}
    OfferTicket=LDHUD::FOfferMotionTicket();return false;
}
bool ULDChoicesPanel::ChooseFinalMod(int32 Index)
{
    if(!Usable()||!Latest.bFinalModPending||!Latest.FinalModOptions.IsValidIndex(Index)||Latest.FinalModOfferId!=ShownFinalMod){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanFinalMod){Tell(TEXT("没有提交：")+Gate.FinalModBlocked);return false;}
    const FName Option=Latest.FinalModOptions[Index];
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::FinalMod;Command.OfferId=ShownFinalMod;Command.Slot=FLDBuildCommand::AnySlot;
    if(!FLDDefinitionWire::Encode(Option,Command.Option)){Tell(TEXT("没有提交：终局改装标识无效。"));return false;}
    SubmittedFinal.DefinitionId=Option;
    bFinalFeedbackAllowed=bOpen;
    if(Send(Command)){return true;}
    SubmittedFinal=FLDFinalModSelection();bFinalFeedbackAllowed=false;return false;
}
bool ULDChoicesPanel::LearnBlueprint(int32 Index,uint8 Key)
{
    if(!Usable()||!ShownBlueprints.IsValidIndex(Index)||Key>2||!Latest.PendingBlueprints.Contains(ShownBlueprints[Index])){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!LDHUD::ActiveInSlot(Latest,Key).IsNone()){Tell(TEXT("没有提交：")+FString(LDTerms::ReplaceAtRest));return false;}
    if(!Gate.bCanLearn){Tell(TEXT("没有提交：")+Gate.LearnBlocked);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::LearnBlueprint;
    if(!FLDDefinitionWire::Encode(ShownBlueprints[Index],Command.Blueprint)){Tell(TEXT("没有提交：这份图纸的标识无效。"));return false;}
    Command.Slot=Key;
    return Send(Command);
}
bool ULDChoicesPanel::LearnBlueprintCard(int32 Index)
{
    const uint8 Key=FirstEmptySlot();
    if(Key>2){Tell(TEXT("没有提交：")+FString(LDTerms::SlotsFull));return false;}
    return LearnBlueprint(Index,Key);
}
FString ULDChoicesPanel::RespecRefusal() const
{
    if(!Gate.bCanRespec){return Gate.RespecBlocked;}
    int64 Allocated=0;for(const FLDStatPointsView& Row:Latest.StatPoints){Allocated+=FMath::Max(0,Row.Points);}
    return Allocated>0?FString():LDHUD::GrowthReasonText(ELDGrowthReason::NothingToRespec);
}
FString ULDChoicesPanel::ReplaceRefusal() const
{
    if(!Gate.bCanReplace){return Gate.ReplaceBlocked;}
    return Latest.bHasActiveOffer?LDHUD::GrowthReasonText(ELDGrowthReason::OfferOutstanding):FString();
}
FString ULDChoicesPanel::ForgetRefusal() const
{
    if(!Gate.bCanForget){return Gate.ForgetBlocked;}
    return Latest.bHasActiveOffer?LDHUD::GrowthReasonText(ELDGrowthReason::OfferOutstanding):FString();
}
bool ULDChoicesPanel::RestConfirmHolds() const
{
    if(RestConfirm.Kind==ELDBuildCommandKind::None||!Usable()){return false;}
    if(LDHUD::ActiveInSlot(Latest,RestConfirm.Slot)!=RestConfirm.Active){return false;}
    return RestConfirm.Kind!=ELDBuildCommandKind::ReplaceActive||Latest.PendingBlueprints.Contains(RestConfirm.Blueprint);
}
FString ULDChoicesPanel::RestConfirmRefusal() const
{
    switch(RestConfirm.Kind)
    {
    case ELDBuildCommandKind::ReplaceActive: return ReplaceRefusal();
    case ELDBuildCommandKind::ForgetActive: return ForgetRefusal();
    default: return LDHUD::GrowthReasonText(ELDGrowthReason::InvalidRequest);
    }
}
bool ULDChoicesPanel::Respec()
{
    if(!Usable()){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    const FString Refusal=RespecRefusal();
    if(!Refusal.IsEmpty()){Tell(TEXT("没有提交：")+Refusal);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::Respec;
    return Send(Command);
}
bool ULDChoicesPanel::AskReplace(int32 Index,uint8 Key)
{
    if(!Usable()||!ShownBlueprints.IsValidIndex(Index)||Key>2||!Latest.PendingBlueprints.Contains(ShownBlueprints[Index])){return false;}
    const FName Active=LDHUD::ActiveInSlot(Latest,Key);
    if(Active.IsNone()){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::SlotEmpty));return false;}
    const FString Refusal=ReplaceRefusal();
    if(!Refusal.IsEmpty()){Tell(TEXT("没有提交：")+Refusal);return false;}
    RestConfirm.Kind=ELDBuildCommandKind::ReplaceActive;RestConfirm.Blueprint=ShownBlueprints[Index];RestConfirm.Slot=Key;RestConfirm.Active=Active;
    Refresh();
    return true;
}
bool ULDChoicesPanel::AskForget(uint8 Key)
{
    if(!Usable()||Key>2){return false;}
    const FName Active=LDHUD::ActiveInSlot(Latest,Key);
    if(Active.IsNone()){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::SlotEmpty));return false;}
    const FString Refusal=ForgetRefusal();
    if(!Refusal.IsEmpty()){Tell(TEXT("没有提交：")+Refusal);return false;}
    RestConfirm=FRestConfirm();
    RestConfirm.Kind=ELDBuildCommandKind::ForgetActive;RestConfirm.Slot=Key;RestConfirm.Active=Active;
    Refresh();
    return true;
}
bool ULDChoicesPanel::ForgetAtColumn(int32 Column)
{
    if(!IsServiceShown()){return false;}
    for(uint8 Key=0;Key<3;++Key){if(int32(LDHUD::SlotColumn(Key))==Column){AskForget(Key);return true;}}
    return false;
}
bool ULDChoicesPanel::ConfirmRestAction()
{
    if(RestConfirm.Kind==ELDBuildCommandKind::None){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!RestConfirmHolds()){CancelRestAction();Tell(TEXT("技能槽或图纸已变化，请重新选择。"));return false;}
    const FString Refusal=RestConfirmRefusal();
    if(!Refusal.IsEmpty()){Tell(TEXT("没有提交：")+Refusal);return false;}
    FLDBuildCommand Command;
    Command.Kind=RestConfirm.Kind;
    Command.Slot=RestConfirm.Slot;
    if(RestConfirm.Kind==ELDBuildCommandKind::ReplaceActive&&!FLDDefinitionWire::Encode(RestConfirm.Blueprint,Command.Blueprint))
    {Tell(TEXT("没有提交：这份图纸的标识无效。"));return false;}
    // The line is consumed by the attempt; the receipt or the refusal reason takes its place.
    RestConfirm=FRestConfirm();
    return Send(Command);
}
void ULDChoicesPanel::CancelRestAction()
{
    RestConfirm=FRestConfirm();
}
bool ULDChoicesPanel::CloseChildConfirmation()
{
    if(RestConfirm.Kind==ELDBuildCommandKind::None){return false;}
    CancelRestAction();
    return true;
}
bool ULDChoicesPanel::IsShopOffered() const
{
    return bOpen&&Usable()&&LatestStage==LDProgression::EMatchStage::Rest&&Latest.Shop.bStocked;
}
bool ULDChoicesPanel::ShopCommand(ELDBuildCommandKind Kind,FLDBuildCommand& Out)
{
    if(!IsShopOffered()){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanShop){Tell(TEXT("没有提交：")+Gate.ShopBlocked);return false;}
    Out=FLDBuildCommand();Out.Kind=Kind;Out.OfferRevision=Latest.Shop.Revision;
    return true;
}
bool ULDChoicesPanel::BuyShopSlot(int32 Index)
{
    FLDBuildCommand Command;
    if(!Latest.Shop.Slots.IsValidIndex(Index)||!ShopCommand(ELDBuildCommandKind::ShopBuy,Command)){return false;}
    const FLDShopSlotView& Row=Latest.Shop.Slots[Index];
    if(Row.bSold){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::ShopSoldOut));return false;}
    if(Latest.UnspentPoints<Row.Price){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::NotEnoughPoints));return false;}
    Command.Slot=uint8(Index);
    LDWidgets::PlayCue(this,ELDAudioCue::UIChoose);
    return Send(Command);
}
bool ULDChoicesPanel::BuyShopSkillPoint()
{
    FLDBuildCommand Command;
    if(!ShopCommand(ELDBuildCommandKind::ShopBuy,Command)){return false;}
    const FLDShopView& Shop=Latest.Shop;
    if(Shop.SkillPointsBought>=Shop.SkillPointsMax){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::SkillPointLimit));return false;}
    if(Latest.UnspentPoints<Shop.SkillPointPrice){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::NotEnoughPoints));return false;}
    Command.Slot=FLDBuildCommand::ShopSkillPointSlot;
    return Send(Command);
}
bool ULDChoicesPanel::RefreshShopShelf()
{
    FLDBuildCommand Command;
    if(!ShopCommand(ELDBuildCommandKind::ShopRefresh,Command)){return false;}
    if(Latest.UnspentPoints<Latest.Shop.RefreshPrice){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::NotEnoughPoints));return false;}
    return Send(Command);
}
bool ULDChoicesPanel::Send(FLDBuildCommand Command)
{
    if(!SubmitBuild)
    {
        UE_LOG(LogLDUI,Error,TEXT("LD_GROWTH_SUBMIT_UNAVAILABLE The growth build entry is not connected to this panel."));
        Tell(TEXT("成长入口不可用，本次没有提交。"));return false;
    }
    Command.ExpectedBuildRevision=Latest.BuildRevision;Command.CatalogDigest=Latest.CatalogDigest;
    // A Listen Host answers inside the submit call, so the pending entry is keyed to the id it will receive first.
    const uint64 Expected=PeekRequestId?PeekRequestId():0;
    const bool bOfferChoice=(Command.Kind==ELDBuildCommandKind::ActiveChoice||Command.Kind==ELDBuildCommandKind::SkipActiveOffer)
        &&OfferTicket.Offer==Command.OfferId;
    if(bOfferChoice){OfferTicket.Request=Expected;}
    const uint32 Before=Answers;PendingRequest=Expected;ResultLine.Reset();
    ELDGrowthReason Reason=ELDGrowthReason::None;
    const uint64 Request=SubmitBuild(Command,Reason);
    if(!Request)
    {
        if(PendingRequest==Expected){PendingRequest=0;}
        const FString Why=LDHUD::GrowthReasonText(Reason);
        Tell(TEXT("没有提交：")+(Why.IsEmpty()?FString(TEXT("原因未知")):Why));
        Refresh();
        return false;
    }
    if(Answers==Before)
    {
        PendingRequest=Request;
        if(bOfferChoice){OfferTicket.Request=Request;}
    }
    Refresh();
    return true;
}
void ULDChoicesPanel::FinishRequest()
{
    PendingRequest=0;
    SubmittedFinal=FLDFinalModSelection();bFinalFeedbackAllowed=false;
    OfferTicket=LDHUD::FOfferMotionTicket();
    Refresh();
    // Every result reaches the player as a toast, whether the ring is open or not.
    Tell(ResultLine);ResultLine.Reset();
}
void ULDChoicesPanel::ShowFinal(const FLDBuildReceipt& Receipt)
{
    ResultLine=LDHUD::BuildReceiptText(Receipt,LatestContent,SlotKeys);
    // The moments the receipt confirms get their cue: an active chosen, the final mod locked, a refusal.
    if(Receipt.Header.Outcome==ELDReceiptOutcome::Applied)
    {
        if(Receipt.Kind==ELDBuildCommandKind::ActiveChoice||Receipt.Kind==ELDBuildCommandKind::SkipActiveOffer)
        {
            if(Receipt.Kind==ELDBuildCommandKind::ActiveChoice){LDWidgets::PlayCue(this,ELDAudioCue::UIChoose);}
            if(OfferTicket.Matches(Receipt,LatestMatch,LatestLife))
            {
                ConfirmedOffer=Receipt.OfferId;ConfirmedMatch=LatestMatch;ConfirmedLife=LatestLife;
            }
            else if(OfferTicket.Offer.IsValid())
            {UE_LOG(LogLDUI,Warning,TEXT("LD_UI_CHOICE_RECEIPT_MISMATCH Receipt does not match the submitted offer and life."));}
        }
        else if(Receipt.Kind==ELDBuildCommandKind::FinalMod)
        {
            if(LDHUD::AppliedFinalFeedback(Receipt,LatestMatch,LatestLife,PendingRequest,SubmittedFinal.DefinitionId))
            {
                LDWidgets::PlayCue(this,ELDAudioCue::UIFinalLock);
                if(bFinalFeedbackAllowed&&FinalModApplied){FinalModApplied(Receipt,SubmittedFinal);}
            }
            else{UE_LOG(LogLDUI,Warning,TEXT("LD_UI_FINAL_RECEIPT_MISMATCH Receipt does not match the submitted final mod and life."));}
        }
    }
    else{LDWidgets::PlayCue(this,ELDAudioCue::UIReject);}
}
// The pending entry clears exactly when the controller releases its in-flight request: a final submit answer, an
// unreserved rejection, or a final/expired receipt. Other answers only explain the wait.
void ULDChoicesPanel::HandleSubmit(const FLDSubmitResult& Result)
{
    if(Result.Family!=ELDGrowthFamily::Build||!PendingRequest||Result.RequestId!=PendingRequest||Result.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Status)
    {
    case ELDAdmissionStatus::AcceptedPending: return;
    case ELDAdmissionStatus::AdmissionRejected:
        ResultLine=TEXT("没有受理：")+LDHUD::GrowthReasonText(LDGrowthWire::Reason(Result.Reason));
        if(Result.bSequenceReserved){Tell(ResultLine);ResultLine.Reset();return;}
        break;
    case ELDAdmissionStatus::Final:
        if(Result.bHasBuildReceipt){ShowFinal(Result.BuildReceipt);}else{ResultLine=TEXT("请求已结束，请以当前状态为准。");}
        break;
    default: Tell(TEXT("无法识别服务器的答复，请以当前状态为准。"));return;
    }
    FinishRequest();
}
void ULDChoicesPanel::HandleReceipt(const FLDBuildReceiptQueryResult& Result)
{
    if(Result.Query.Family!=ELDGrowthFamily::Build||!PendingRequest||Result.Query.RequestId!=PendingRequest||Result.Query.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Lookup)
    {
    case ELDReceiptLookup::Pending: return;
    case ELDReceiptLookup::Final:
        if(Result.bHasReceipt){ShowFinal(Result.Receipt);}else{ResultLine=TEXT("回执缺少结果，请以当前状态为准。");}
        break;
    case ELDReceiptLookup::ReceiptExpired: ResultLine=TEXT("回执已过期，请以当前状态为准。");break;
    case ELDReceiptLookup::NotFound: Tell(TEXT("服务器没有找到这次请求，请以当前状态为准。"));return;
    default: Tell(TEXT("回执查询无效，请以当前状态为准。"));return;
    }
    FinishRequest();
}
