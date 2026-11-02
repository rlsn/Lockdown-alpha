#include "UI/LDChoicesPanel.h"
#include "UI/LDHUDModel.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
const FLinearColor ChoicesIdle(.045f,.12f,.15f),ChoicesSelected(.26f,.2f,.05f);
void ChoicesTint(ULDCommandButton* Button,const FLinearColor& Color)
{
    auto Style=Button->GetStyle();Style.Normal.TintColor=FSlateColor(Color);Style.Hovered.TintColor=FSlateColor(Color*1.6f);Button->SetStyle(Style);
}
void ChoicesText(UTextBlock* Text,const FString& Value)
{
    if(Text&&!Text->GetText().ToString().Equals(Value,ESearchCase::CaseSensitive)){Text->SetText(FText::FromString(Value));}
}
}

TSharedRef<SWidget> ULDChoicesPanel::RebuildWidget()
{
    if(!MainPanel)
    {
        auto Text=[this](const FString& Value,int32 Size){return LDWidgets::Text(WidgetTree,Value,Size);};
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Top=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Top);
        Header=Text(TEXT(""),20);Top->AddChildToHorizontalBox(Header)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Top->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("关闭"),[this](){ClosePanel();},false,TEXT("ChoicesClose")));
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,12));
        for(int32 Index=0;Index<3;++Index)
        {
            FCardWidgets& Card=Cards.AddDefaulted_GetRef();
            auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();
            Card.Kind=Text(TEXT(""),13);Card.Kind->SetColorAndOpacity(FLinearColor(1,.82f,.4f));Lines->AddChildToVerticalBox(Card.Kind);
            Card.Name=Text(TEXT(""),20);Lines->AddChildToVerticalBox(Card.Name)->SetPadding(FMargin(0,4,0,0));
            Card.Rank=Text(TEXT(""),14);Lines->AddChildToVerticalBox(Card.Rank);
            Card.Body=Text(TEXT(""),14);Lines->AddChildToVerticalBox(Card.Body)->SetPadding(FMargin(0,8,0,0));
            Card.Button=LDWidgets::Button(WidgetTree,TEXT(""),[this,Index](){PreviewChoice(Index);},false,FName(*FString::Printf(TEXT("ChoiceCard%d"),Index)));
            Card.Button->SetContent(Lines);
            auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(286);Size->SetMinDesiredHeight(240);Size->SetContent(Card.Button);
            Row->AddChildToHorizontalBox(Size)->SetPadding(FMargin(0,0,Index<2?10:0,0));
            CardWidgets.Add(Card.Button);CardWidgets.Add(Card.Kind);CardWidgets.Add(Card.Name);CardWidgets.Add(Card.Rank);CardWidgets.Add(Card.Body);
        }
        auto* Bottom=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Bottom);
        Feedback=Text(TEXT(""),15);Bottom->AddChildToHorizontalBox(Feedback)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ConfirmButton=LDWidgets::Button(WidgetTree,TEXT("确认本次选择"),[this](){ConfirmChoice();},false,TEXT("ChoicesConfirm"));
        Bottom->AddChildToHorizontalBox(ConfirmButton);
        auto* Replace=WidgetTree->ConstructWidget<UVerticalBox>();
        ReplaceText=Text(TEXT(""),16);Replace->AddChildToVerticalBox(ReplaceText);
        auto* Slots=WidgetTree->ConstructWidget<UHorizontalBox>();Replace->AddChildToVerticalBox(Slots)->SetPadding(FMargin(0,8));
        for(uint8 SkillSlot=0;SkillSlot<2;++SkillSlot)
        {
            ReplaceSlotButtons[SkillSlot]=LDWidgets::Button(WidgetTree,TEXT(""),[this,SkillSlot](){PreviewReplacement(SkillSlot);},false,SkillSlot?FName(TEXT("ReplaceSlotW")):FName(TEXT("ReplaceSlotQ")));
            Slots->AddChildToHorizontalBox(ReplaceSlotButtons[SkillSlot])->SetPadding(FMargin(0,0,10,0));
        }
        Replace->AddChildToVerticalBox(Text(TEXT("被替换的技能及其等级会被遗忘，再学从 1 级开始，旧投入不返还；冷却照常计时。"),14));
        auto* ReplaceActions=WidgetTree->ConstructWidget<UHorizontalBox>();Replace->AddChildToVerticalBox(ReplaceActions)->SetPadding(FMargin(0,10,0,0));
        ReplaceConfirmButton=LDWidgets::Button(WidgetTree,TEXT("确认替换并遗忘"),[this](){ConfirmChoice();},false,TEXT("ReplaceConfirm"));
        ReplaceActions->AddChildToHorizontalBox(ReplaceConfirmButton)->SetPadding(FMargin(0,0,10,0));
        ReplaceActions->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("返回三卡"),[this](){CloseChildConfirmation();},false,TEXT("ReplaceBack")));
        ReplacePanel=LDWidgets::Panel(WidgetTree,Replace,FMargin(16));ReplacePanel->SetBrushColor(FLinearColor(.06f,.05f,.03f,.99f));ReplacePanel->SetVisibility(ESlateVisibility::Collapsed);
        Root->AddChildToVerticalBox(ReplacePanel)->SetPadding(FMargin(0,12,0,0));
        MainPanel=LDWidgets::Panel(WidgetTree,Root,FMargin(20));
        auto* Width=WidgetTree->ConstructWidget<USizeBox>();Width->SetWidthOverride(920);Width->SetContent(MainPanel);
        auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor::Transparent);Frame->SetHorizontalAlignment(HAlign_Center);Frame->SetVerticalAlignment(VAlign_Top);
        Frame->SetPadding(FMargin(0,150,0,0));Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Frame->SetContent(Width);
        WidgetTree->RootWidget=Frame;
        SetVisibility(ESlateVisibility::Collapsed);
    }
    return Super::RebuildWidget();
}
UWidget* ULDChoicesPanel::FocusScope() const{return bReplacing?static_cast<UWidget*>(ReplacePanel.Get()):static_cast<UWidget*>(MainPanel.Get());}
UWidget* ULDChoicesPanel::HitArea() const{return MainPanel;}
void ULDChoicesPanel::OpenPanel()
{
    bOpen=true;SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if(!PendingRequest){ChoicesText(Feedback,TEXT("先点选一张卡，再确认。对局不会暂停。"));}
}
bool ULDChoicesPanel::ClosePanel()
{
    // Closing only hides the panel: the opportunity, the offer and any submitted request keep their real state.
    ResetReplacement();bOpen=false;SetVisibility(ESlateVisibility::Collapsed);return true;
}
bool ULDChoicesPanel::CloseChildConfirmation()
{
    if(!bReplacing){return false;}
    ResetReplacement();ChoicesText(Feedback,TEXT("已返回三卡，没有消耗本次选择。"));return true;
}
void ULDChoicesPanel::ResetReplacement()
{
    bReplacing=false;ReplaceSlot=INDEX_NONE;ReplaceSkill=NAME_None;ReplaceRank=0;ReplaceBuildRevision=0;
    if(ReplacePanel){ReplacePanel->SetVisibility(ESlateVisibility::Collapsed);}
}
void ULDChoicesPanel::Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase Phase,const FLDDemoContentDefinitions* Content)
{
    const FGuid KeepOffer=ShownOffer;
    const FGuid KeepOption=Selected!=INDEX_NONE&&Latest.Offer.Options.IsValidIndex(Selected)?Latest.Offer.Options[Selected].OptionId:FGuid();
    Latest=View;LatestMatch=Match;LatestLife=Life;LatestPhase=Phase;LatestContent=Content;
    if(!MainPanel){return;}
    const bool bUsable=View.bValid&&View.Generation==Match&&View.Life==Life;
    const bool bOffer=bUsable&&View.bHasOffer&&View.Offer.Options.Num()==3;
    ChoicesText(Header,!bUsable?FString(TEXT("升级奖励 · 成长数据同步中")):FString::Printf(TEXT("升级奖励 · 待选 %d 次%s"),View.PendingChoiceCount,
        Phase==ELDLifePhase::Dead?TEXT(" · 社死中也可确认，只更新构筑"):TEXT("")));
    if(!bOffer||View.Offer.OfferId!=ShownOffer||View.Offer.OfferRevision!=ShownOfferRevision)
    {
        // A new offer replaces the cards. A revised offer of the same opportunity refreshes its guarded versions and keeps
        // the same option selected, so a stale confirmation is confirmed again on the same card (UI 3.2).
        ShownOffer=bOffer?View.Offer.OfferId:FGuid();ShownOfferRevision=bOffer?View.Offer.OfferRevision:0;Selected=INDEX_NONE;ResetReplacement();
        if(bOffer&&View.Offer.OfferId==KeepOffer&&KeepOption.IsValid())
        {Selected=View.Offer.Options.IndexOfByPredicate([&KeepOption](const FLDChoiceOption& Option){return Option.OptionId==KeepOption;});}
        for(int32 Index=0;Index<Cards.Num();++Index)
        {
            FCardWidgets& Card=Cards[Index];
            Card.Button->SetVisibility(bOffer?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
            if(!bOffer){continue;}
            const LDHUD::FChoiceCardView Shown=LDHUD::ChoiceCard(View.Offer.Options[Index],View,Content);
            ChoicesText(Card.Kind,Shown.Kind+(Shown.bNeedsReplacement?TEXT(" · 需替换"):TEXT("")));ChoicesText(Card.Name,Shown.Name);ChoicesText(Card.Rank,Shown.RankText);
            FString Body=Shown.Description;for(const FString& Line:Shown.Lines){Body+=(Body.IsEmpty()?TEXT(""):TEXT("\n"))+Line;}
            ChoicesText(Card.Body,Body);
        }
        if(!bOffer&&!PendingRequest){ChoicesText(Feedback,bUsable?TEXT("暂无待选奖励。升级后会获得新的选择机会。"):TEXT("等待成长数据同步。"));}
    }
    else if(bReplacing&&ReplaceBuildRevision!=0&&View.BuildRevision!=ReplaceBuildRevision)
    {
        // The skills shown for replacement changed: the confirmation is withdrawn, never redirected to another slot.
        ResetReplacement();ChoicesText(Feedback,TEXT("技能已变化，替换确认已撤销，请重新选择。"));
    }
    RefreshSelection();
}
void ULDChoicesPanel::RefreshSelection()
{
    for(int32 Index=0;Index<Cards.Num();++Index){ChoicesTint(Cards[Index].Button,Index==Selected?ChoicesSelected:ChoicesIdle);}
    const bool bReady=Selected!=INDEX_NONE&&!PendingRequest&&ShownOffer.IsValid()&&LDGrowthWire::IsChoicePhase(LatestPhase);
    ConfirmButton->SetIsEnabled(bReady&&!bReplacing);
    ReplaceConfirmButton->SetIsEnabled(bReady&&bReplacing&&ReplaceSlot!=INDEX_NONE);
    for(uint8 SkillSlot=0;SkillSlot<2;++SkillSlot){ChoicesTint(ReplaceSlotButtons[SkillSlot],int32(SkillSlot)==ReplaceSlot?ChoicesSelected:ChoicesIdle);}
}
bool ULDChoicesPanel::PreviewChoice(int32 Index)
{
    if(!Latest.bHasOffer||!Latest.Offer.Options.IsValidIndex(Index)||Latest.Offer.OfferId!=ShownOffer){return false;}
    Selected=Index;ResetReplacement();
    const LDHUD::FChoiceCardView Shown=LDHUD::ChoiceCard(Latest.Offer.Options[Index],Latest,LatestContent);
    if(!PendingRequest){ChoicesText(Feedback,TEXT("当前选择：")+Shown.Name+TEXT(" · ")+Shown.RankText);}
    RefreshSelection();return true;
}
bool ULDChoicesPanel::PreviewReplacement(uint8 SkillSlot)
{
    if(!bReplacing||SkillSlot>1){return false;}
    const FLDLearnedSkillView* Skill=Latest.Skills.FindByPredicate([SkillSlot](const FLDLearnedSkillView& S){return S.bActive&&S.Slot==SkillSlot;});
    if(!Skill){ChoicesText(Feedback,TEXT("该技能槽为空，无需替换；请返回三卡重新确认。"));return false;}
    ReplaceSlot=SkillSlot;ReplaceSkill=Skill->DefinitionId;ReplaceRank=Skill->Rank;
    ChoicesText(Feedback,FString::Printf(TEXT("将遗忘 %s：%s %d 级"),SkillSlot?TEXT("W"):TEXT("Q"),*LDHUD::ContentName(LatestContent,Skill->DefinitionId.ToString()),Skill->Rank));
    RefreshSelection();return true;
}
void ULDChoicesPanel::ConfirmChoice()
{
    if(PendingRequest){ChoicesText(Feedback,TEXT("上一次选择仍在处理中。"));return;}
    if(Selected==INDEX_NONE||!Latest.bHasOffer||!Latest.Offer.Options.IsValidIndex(Selected)||Latest.Offer.OfferId!=ShownOffer){ChoicesText(Feedback,TEXT("请先点选一张卡。"));return;}
    if(!LDGrowthWire::IsChoicePhase(LatestPhase)){ChoicesText(Feedback,TEXT("当前状态不能确认选择。"));return;}
    const FLDChoiceOption& Option=Latest.Offer.Options[Selected];
    FLDChoiceCommand Command;
    Command.Generation=LatestMatch;Command.Life=LatestLife;Command.ExpectedLifePhase=LatestPhase;
    Command.OfferId=Latest.Offer.OfferId;Command.OfferRevision=Latest.Offer.OfferRevision;Command.OptionId=Option.OptionId;
    Command.ExpectedBuildRevision=Latest.BuildRevision;Command.CatalogDigest=Latest.CatalogDigest;
    const LDHUD::FChoiceCardView Shown=LDHUD::ChoiceCard(Option,Latest,LatestContent);
    if(Shown.bNeedsReplacement)
    {
        if(!bReplacing)
        {
            // Child confirmation: both current actives, no default slot.
            bReplacing=true;ReplaceBuildRevision=Latest.BuildRevision;ReplacePanel->SetVisibility(ESlateVisibility::Visible);
            for(uint8 SkillSlot=0;SkillSlot<2;++SkillSlot)
            {
                const FLDLearnedSkillView* Skill=Latest.Skills.FindByPredicate([SkillSlot](const FLDLearnedSkillView& S){return S.bActive&&S.Slot==SkillSlot;});
                const FString Label=FString::Printf(TEXT("%s：%s"),SkillSlot?TEXT("W"):TEXT("Q"),Skill?*FString::Printf(TEXT("%s %d 级"),*LDHUD::ContentName(LatestContent,Skill->DefinitionId.ToString()),Skill->Rank):TEXT("空"));
                ChoicesText(CastChecked<UTextBlock>(ReplaceSlotButtons[SkillSlot]->GetContent()),Label);
            }
            ChoicesText(ReplaceText,TEXT("学习「")+Shown.Name+TEXT("」需要遗忘一个主动技能。选择要遗忘的技能："));
            ChoicesText(Feedback,TEXT("请选择要替换的技能槽。"));RefreshSelection();
            return;
        }
        if(ReplaceSlot==INDEX_NONE||!FLDDefinitionWire::Encode(ReplaceSkill,Command.ExpectedOldSkillId))
        {ChoicesText(Feedback,TEXT("请选择要替换的技能槽。"));return;}
        Command.bReplace=true;Command.ReplaceSlot=uint8(ReplaceSlot);Command.ExpectedOldRank=ReplaceRank;Command.bReplaceConfirmed=true;
    }
    PendingName=Shown.Name;
    Send(Command);
}
void ULDChoicesPanel::Send(const FLDChoiceCommand& Command)
{
    if(!SubmitChoice)
    {
        UE_LOG(LogTemp,Error,TEXT("LD_CHOICES_SUBMIT_UNAVAILABLE The growth choice entry is not connected to this panel."));
        ChoicesText(Feedback,TEXT("选择入口不可用，本次没有提交。"));return;
    }
    // A Listen Host answers inside the submit call, so the pending entry is keyed to the id it will receive first.
    const uint64 Expected=PeekRequestId?PeekRequestId():0;
    const uint32 Before=Answers;PendingRequest=Expected;
    ELDGrowthReason Reason=ELDGrowthReason::None;
    const uint64 Request=SubmitChoice(Command,Reason);
    if(!Request)
    {
        if(PendingRequest==Expected){PendingRequest=0;}
        const FString Why=LDHUD::GrowthReasonText(Reason);
        ChoicesText(Feedback,TEXT("没有提交：")+(Why.IsEmpty()?FString(TEXT("原因未知")):Why));RefreshSelection();return;
    }
    if(Answers==Before){PendingRequest=Request;ChoicesText(Feedback,TEXT("已提交，等待服务器确认…"));}
    RefreshSelection();
}
void ULDChoicesPanel::ShowFinal(const FLDChoiceReceipt& Receipt)
{
    if(Receipt.Header.Outcome==ELDReceiptOutcome::Applied)
    {
        ChoicesText(Feedback,TEXT("已确认：")+PendingName+(Receipt.bReplaced?TEXT("（已遗忘被替换的技能）"):TEXT("")));
        Selected=INDEX_NONE;ResetReplacement();
        return;
    }
    // A rejected confirmation keeps the selected card: after the view refreshes it is confirmed again explicitly.
    ChoicesText(Feedback,TEXT("没有生效：")+LDHUD::GrowthReasonText(LDGrowthWire::Reason(Receipt.Header.Reason)));
}
// The pending entry clears exactly when the controller releases its in-flight request: a final submit answer, an
// unreserved rejection, or a final/expired receipt. Other answers only explain the wait.
void ULDChoicesPanel::HandleSubmit(const FLDSubmitResult& Result)
{
    if(Result.Family!=ELDGrowthFamily::Choice||!PendingRequest||Result.RequestId!=PendingRequest||Result.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Status)
    {
    case ELDAdmissionStatus::AcceptedPending: ChoicesText(Feedback,TEXT("已受理，等待结算…"));return;
    case ELDAdmissionStatus::AdmissionRejected:
        ChoicesText(Feedback,TEXT("没有受理：")+LDHUD::GrowthReasonText(LDGrowthWire::Reason(Result.Reason)));
        if(Result.bSequenceReserved){return;}
        break;
    case ELDAdmissionStatus::Final:
        if(Result.bHasChoiceReceipt){ShowFinal(Result.ChoiceReceipt);}else{ChoicesText(Feedback,TEXT("请求已结束，请以当前状态为准。"));}
        break;
    default: ChoicesText(Feedback,TEXT("无法识别服务器的答复，请以当前状态为准。"));return;
    }
    PendingRequest=0;RefreshSelection();
}
void ULDChoicesPanel::HandleReceipt(const FLDChoiceReceiptQueryResult& Result)
{
    if(Result.Query.Family!=ELDGrowthFamily::Choice||!PendingRequest||Result.Query.RequestId!=PendingRequest||Result.Query.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Lookup)
    {
    case ELDReceiptLookup::Pending: ChoicesText(Feedback,TEXT("已受理，等待结算…"));return;
    case ELDReceiptLookup::Final:
        if(Result.bHasReceipt){ShowFinal(Result.Receipt);}else{ChoicesText(Feedback,TEXT("回执缺少结果，请以当前状态为准。"));}
        break;
    case ELDReceiptLookup::ReceiptExpired: ChoicesText(Feedback,TEXT("回执已过期，请以当前状态为准。"));break;
    case ELDReceiptLookup::NotFound: ChoicesText(Feedback,TEXT("服务器没有找到这次请求，请以当前状态为准。"));return;
    default: ChoicesText(Feedback,TEXT("回执查询无效，请以当前状态为准。"));return;
    }
    PendingRequest=0;RefreshSelection();
}
