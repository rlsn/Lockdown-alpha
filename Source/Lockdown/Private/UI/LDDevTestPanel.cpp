#include "UI/LDDevTestPanel.h"
#include "UI/LDHUDModel.h"
#include "UI/LDTerms.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDGrowthProtocol.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

namespace
{
const TCHAR* const DevPanelSyncing=TEXT("同步中");
const FLinearColor DevPanelBright(.9f,.94f,.96f),DevPanelSecondary(.74f,.81f,.85f),DevPanelDim(.55f,.63f,.68f),
    DevPanelHeading(1,.82f,.4f),DevPanelWarning(1.f,.70f,.45f),DevPanelCard(.045f,.075f,.105f,1);
void DevPanelSetText(UTextBlock* Label,const FString& Value)
{
    if(Label&&!Label->GetText().ToString().Equals(Value,ESearchCase::CaseSensitive)){Label->SetText(FText::FromString(Value));}
}
void DevPanelShow(UWidget* Target,bool bShow,ESlateVisibility Shown)
{
    const ESlateVisibility Wanted=bShow?Shown:ESlateVisibility::Collapsed;
    if(Target&&Target->GetVisibility()!=Wanted){Target->SetVisibility(Wanted);}
}
const TCHAR* DevPanelUnit(const LDDevTest::FStatField& Field)
{
    if(Field.Decimals==0){return TEXT("厘米");}
    if(Field.Stat==uint8(ELDDemoBonusStat::CritChanceBonusRatio)){return TEXT("百分点");}
    if(Field.Stat==uint8(ELDDemoBonusStat::AttackSpeedBonusRatio)||Field.Stat==uint8(ELDDemoBonusStat::MoveSpeedBonusRatio)){return TEXT("% 基础值");}
    return TEXT("点");
}
}

TArray<LDDevPanel::FLearnRow> LDDevPanel::LearnRows(const FLDDemoContentDefinitions& Content,const FLDProgressionView* Progression)
{
    TArray<FLearnRow> Rows;
    for(const FLDDemoAbilityDefinition& Ability:Content.Abilities)
    {
        FLearnRow& Row=Rows.AddDefaulted_GetRef();
        Row.Id=FName(*Ability.Id);Row.Name=LDHUD::ContentName(&Content,Ability.Id);
        Row.bActiveSkill=Ability.Kind==ELDDemoAbilityKind::Active;Row.Kind=Row.bActiveSkill?TEXT("主动技能"):TEXT("被动技能");
        if(Progression&&Progression->bValid)
        {
            for(const FLDLearnedSkillView& Skill:Progression->Skills)
            {
                if(Skill.DefinitionId.ToString().Equals(Ability.Id,ESearchCase::IgnoreCase)){Row.Held=Skill.Rank;}
            }
        }
    }
    for(const FLDDemoAttributeDefinition& Attribute:Content.Attributes)
    {
        FLearnRow& Row=Rows.AddDefaulted_GetRef();
        Row.Id=FName(*Attribute.Id);Row.Name=LDHUD::ContentName(&Content,Attribute.Id);Row.Kind=TEXT("属性卡");
        if(Progression&&Progression->bValid)
        {
            for(const FLDAttributeCardView& Card:Progression->AttributeCards)
            {
                if(Card.DefinitionId.ToString().Equals(Attribute.Id,ESearchCase::IgnoreCase)){Row.Held=Card.Count;}
            }
        }
    }
    return Rows;
}

bool LDDevPanel::BothActiveSlotsHeld(const FLDProgressionView* Progression)
{
    if(!Progression||!Progression->bValid){return false;}
    bool bSlots[2]={false,false};
    for(const FLDLearnedSkillView& Skill:Progression->Skills)
    {
        if(Skill.bActive&&Skill.Slot<2){bSlots[Skill.Slot]=true;}
    }
    return bSlots[0]&&bSlots[1];
}

FString LDDevPanel::HeldText(const FLearnRow& Row)
{
    if(Row.Held<=0){return TEXT("未拥有");}
    return Row.Kind==TEXT("属性卡")?FString::Printf(TEXT("已有 %d 张"),Row.Held):FString::Printf(TEXT("%d 级"),Row.Held);
}

TArray<LDDevPanel::FItemRow> LDDevPanel::ItemRows(const FLDDemoContentDefinitions& Content)
{
    TArray<FItemRow> Rows;
    for(const FLDDemoEquipmentDefinition& Item:Content.Equipment)
    {
        FItemRow& Row=Rows.AddDefaulted_GetRef();
        Row.Id=FName(*Item.Id);Row.Name=LDHUD::ContentName(&Content,Item.Id);Row.Price=Item.BuyPrice;
        Row.Traits=Item.HasActive&&Item.HasPassive?TEXT("主动 + 被动"):Item.HasActive?TEXT("主动"):Item.HasPassive?TEXT("被动"):TEXT("属性");
    }
    return Rows;
}

FString LDDevPanel::StatusLine(const FLDProgressionView* Progression,const FLDEquipmentView* Equipment)
{
    const FString Level=Progression&&Progression->bValid?FString::Printf(TEXT("等级 %d · %s %lld · 未用选择机会 %d"),
        int32(Progression->Level),LDTerms::Experience,Progression->CumulativeXP,int32(Progression->PendingChoiceCount)):FString(TEXT("等级 同步中"));
    const FString Gold=Equipment&&Equipment->bValid?LDHUD::Gold(Equipment->Gold):FString(LDTerms::Gold)+TEXT(" 同步中");
    return Level+TEXT(" · ")+Gold;
}

FString LDDevPanel::StatLabel(const LDDevTest::FStatField& Field)
{
    FLDCombatAttributeRowView Probe;Probe.Attribute=Field.Attribute;
    return FString::Printf(TEXT("%s（%s）"),*LDHUD::AttributeRow(Probe).Name,DevPanelUnit(Field));
}

FString LDDevPanel::AttributeLine(const FLDCombatOwnerView* Combat,uint8 Attribute)
{
    const FLDCombatAttributeRowView* Row=Combat?Combat->Attributes.FindByPredicate([Attribute](const FLDCombatAttributeRowView& R){return R.Attribute==Attribute;}):nullptr;
    if(!Row){return DevPanelSyncing;}
    const LDHUD::FAttributeRowView Shown=LDHUD::AttributeRow(*Row);
    return Shown.Breakdown.IsEmpty()?TEXT("当前 ")+Shown.Final:FString::Printf(TEXT("当前 %s（%s）"),*Shown.Final,*Shown.Breakdown);
}

FString LDDevPanel::SlotLine(const FLDEquipmentView* Equipment,const FLDDemoContentDefinitions* Content,uint8 SlotNumber)
{
    const FString Label=FString::Printf(TEXT("装备 %d："),int32(SlotNumber));
    if(!Equipment||!Equipment->bValid){return Label+DevPanelSyncing;}
    const FLDEquipmentSlotView* Slot=Equipment->Slots.FindByPredicate([SlotNumber](const FLDEquipmentSlotView& S){return S.SlotIndex+1==SlotNumber;});
    if(!Slot){return Label+DevPanelSyncing;}
    return Label+(Slot->bOccupied?LDHUD::ContentName(Content,Slot->DefinitionId.ToString()):FString(TEXT("空")));
}

FString LDDevPanel::ResultLine(const FLDDevTestResult& Result)
{
    const ELDDevTestOp Op=Result.bHasReceipt?Result.Receipt.Op:ELDDevTestOp::None;
    if(Result.Status==ELDAdmissionStatus::AdmissionRejected)
    {
        return FString::Printf(TEXT("请求被拒绝（%s）%s"),LDGrowthWire::ReasonName(LDGrowthWire::Reason(Result.Reason)),
            Result.Detail.IsEmpty()?TEXT(""):*(TEXT("：")+Result.Detail));
    }
    if(Result.Status!=ELDAdmissionStatus::Final||!Result.bHasReceipt){return FString::Printf(TEXT("已受理，等待提交（请求 %llu）"),Result.RequestId);}
    if(Result.Receipt.Header.Outcome==ELDReceiptOutcome::Applied){return FString::Printf(TEXT("%s 已生效"),LDDevTest::OpLabel(Op));}
    return FString::Printf(TEXT("%s 被拒绝（%s）%s"),LDDevTest::OpLabel(Op),
        LDGrowthWire::ReasonName(LDGrowthWire::Reason(Result.Receipt.Header.Reason)),
        Result.Receipt.Detail.IsEmpty()?TEXT(""):*(TEXT("：")+Result.Receipt.Detail));
}

TSharedRef<SWidget> ULDDevTestPanel::RebuildWidget()
{
    if(!MainPanel)
    {
        auto MakeText=[this](const FString& Value,int32 FontSize,const FLinearColor& TextColor,bool bWrap)
        {
            UTextBlock* Label=LDWidgets::Text(WidgetTree,Value,FontSize);Label->SetColorAndOpacity(TextColor);Label->SetAutoWrapText(bWrap);return Label;
        };
        auto MakeBox=[this](const FString& Hint,FName Name)
        {
            auto* Box=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),Name);
            Box->SetHintText(FText::FromString(Hint));
            FEditableTextBoxStyle Style=Box->GetWidgetStyle();Style.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular",15));Style.Padding=FMargin(8,5);
            Box->SetWidgetStyle(Style);Box->SetForegroundColor(FLinearColor(.04f,.07f,.07f));Box->SetClearKeyboardFocusOnCommit(false);
            auto* Width=WidgetTree->ConstructWidget<USizeBox>();Width->SetWidthOverride(110);Width->SetContent(Box);
            return TPair<UEditableTextBox*,UWidget*>(Box,Width);
        };
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Top=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Top);
        Top->AddChildToHorizontalBox(MakeText(TEXT("测试模式"),20,DevPanelWarning,false))->SetVerticalAlignment(VAlign_Center);
        UHorizontalBoxSlot* NoteSlot=Top->AddChildToHorizontalBox(MakeText(TEXT("只改自己；每项由服务器按真实规则提交"),13,DevPanelDim,false));
        NoteSlot->SetPadding(FMargin(12,0,0,0));NoteSlot->SetVerticalAlignment(VAlign_Center);NoteSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Top->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("关闭"),[this](){ClosePanel();},false,TEXT("DevTestClose")));
        StatusText=MakeText(DevPanelSyncing,15,DevPanelBright,true);
        Root->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0,8,0,0));
        FeedbackLine=MakeText(TEXT(""),14,DevPanelSecondary,true);
        Root->AddChildToVerticalBox(FeedbackLine)->SetPadding(FMargin(0,4,0,0));

        auto* Body=WidgetTree->ConstructWidget<UVerticalBox>();
        auto Heading=[&](const FString& Title){Body->AddChildToVerticalBox(MakeText(Title,16,DevPanelHeading,false))->SetPadding(FMargin(0,10,0,4));};
        auto Row=[&](){auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();Body->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,0,0,4));return Line;};
        auto AddButton=[&](UHorizontalBox* Line,const FString& Label,TFunction<void()> Callback,FName Name)
        {Line->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,Label,MoveTemp(Callback),false,Name))->SetPadding(FMargin(6,0,0,0));};

        Heading(TEXT("资源与重复试验"));
        {
            UHorizontalBox* Line=Row();
            const auto XP=MakeBox(LDTerms::Experience,TEXT("DevTestXPAmount"));XPBox=XP.Key;Line->AddChildToHorizontalBox(XP.Value)->SetVerticalAlignment(VAlign_Center);
            AddButton(Line,LDDevTest::OpLabel(ELDDevTestOp::GrantExperience),[this](){SendAmount(ELDDevTestOp::GrantExperience,XPBox->GetText().ToString());},TEXT("DevTestXP"));
            const auto Gold=MakeBox(LDTerms::Gold,TEXT("DevTestGoldAmount"));GoldBox=Gold.Key;
            Line->AddChildToHorizontalBox(Gold.Value)->SetPadding(FMargin(16,0,0,0));
            AddButton(Line,LDDevTest::OpLabel(ELDDevTestOp::GrantGold),[this](){SendAmount(ELDDevTestOp::GrantGold,GoldBox->GetText().ToString());},TEXT("DevTestGold"));
        }
        {
            UHorizontalBox* Line=Row();
            AddButton(Line,LDDevTest::OpLabel(ELDDevTestOp::RefillResources),[this](){Send(ELDDevTestOp::RefillResources);},TEXT("DevTestRefill"));
            AddButton(Line,TEXT("重置冷却"),[this](){Send(ELDDevTestOp::ResetCooldowns);},TEXT("DevTestCooldowns"));
            AddButton(Line,TEXT("重置到开局"),[this](){Send(ELDDevTestOp::ResetToMatchStart);},TEXT("DevTestReset"));
        }

        Heading(TEXT("测试属性（叠加在构筑之上，0 去掉该项）"));
        StatBoxes.Reset();StatLines.Reset();
        const TConstArrayView<LDDevTest::FStatField> Fields=LDDevTest::StatFields();
        for(int32 Index=0;Index<Fields.Num();++Index)
        {
            const LDDevTest::FStatField& Field=Fields[Index];
            auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();
            auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();Lines->AddChildToVerticalBox(Head);
            Head->AddChildToHorizontalBox(MakeText(LDDevPanel::StatLabel(Field),15,DevPanelBright,false))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            const auto Box=MakeBox(Field.Decimals?TEXT("两位小数"):TEXT("整数"),FName(*FString::Printf(TEXT("DevTestAttrValue%s"),Field.ConsoleName)));
            Head->AddChildToHorizontalBox(Box.Value)->SetVerticalAlignment(VAlign_Center);
            Head->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("设置"),[this,Index](){SendStat(Index,StatBoxes[Index]->GetText().ToString());},
                false,FName(*FString::Printf(TEXT("DevTestAttr%s"),Field.ConsoleName))))->SetPadding(FMargin(6,0,0,0));
            UTextBlock* Current=MakeText(DevPanelSyncing,13,DevPanelSecondary,true);
            Lines->AddChildToVerticalBox(Current)->SetPadding(FMargin(0,2,0,0));
            UBorder* Card=LDWidgets::Panel(WidgetTree,Lines,FMargin(10,6));Card->SetBrushColor(DevPanelCard);
            Body->AddChildToVerticalBox(Card)->SetPadding(FMargin(0,0,0,4));
            StatBoxes.Add(Box.Key);StatLines.Add(Current);
        }
        {
            UHorizontalBox* Line=Row();
            AddButton(Line,TEXT("清除全部测试属性"),[this](){Send(ELDDevTestOp::ClearAttributes);},TEXT("DevTestClearAttr"));
        }

        Heading(FString(TEXT("装备（按目录价格，不用去"))+LDTerms::Shop+TEXT("）"));
        for(uint8 SlotNumber=1;SlotNumber<=2;++SlotNumber)
        {
            UHorizontalBox* Line=Row();
            SlotLines[SlotNumber-1]=MakeText(DevPanelSyncing,15,DevPanelBright,false);
            Line->AddChildToHorizontalBox(SlotLines[SlotNumber-1])->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            AddButton(Line,TEXT("出售"),[this,SlotNumber](){Send(ELDDevTestOp::SellItem,0,NAME_None,SlotNumber);},FName(*FString::Printf(TEXT("DevTestSell%d"),int32(SlotNumber))));
        }
        ItemList=WidgetTree->ConstructWidget<UVerticalBox>();Body->AddChildToVerticalBox(ItemList);

        Heading(TEXT("技能与属性卡（作为当前这次三选一的结果）"));
        LearnHint=MakeText(TEXT(""),13,DevPanelDim,true);Body->AddChildToVerticalBox(LearnHint)->SetPadding(FMargin(0,0,0,4));
        LearnList=WidgetTree->ConstructWidget<UVerticalBox>();Body->AddChildToVerticalBox(LearnList);

        auto* Host=WidgetTree->ConstructWidget<UVerticalBox>();
        Host->AddChildToVerticalBox(MakeText(TEXT("整局（只限房主）"),16,DevPanelHeading,false))->SetPadding(FMargin(0,10,0,4));
        auto* HostLine=WidgetTree->ConstructWidget<UHorizontalBox>();Host->AddChildToVerticalBox(HostLine);
        HostLine->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("刷新已清空营地"),[this](){Send(ELDDevTestOp::RefreshCamps);},false,TEXT("DevTestRefreshCamps")));
        HostLine->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("提前开圈"),[this](){Send(ELDDevTestOp::OpenCapture);},false,TEXT("DevTestOpenCapture")))->SetPadding(FMargin(6,0,0,0));
        HostSection=Host;Body->AddChildToVerticalBox(Host);Host->SetVisibility(ESlateVisibility::Collapsed);

        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Scroll->SetClipping(EWidgetClipping::ClipToBounds);Scroll->AddChild(Body);
        auto* HeightBox=WidgetTree->ConstructWidget<USizeBox>();HeightBox->SetMaxDesiredHeight(560);HeightBox->SetContent(Scroll);
        Root->AddChildToVerticalBox(HeightBox)->SetPadding(FMargin(0,8,0,0));
        Root->AddChildToVerticalBox(MakeText(TEXT("测试结果不算无作弊对局；数值只用于试验，不是批准的平衡值。"),13,DevPanelDim,true))->SetPadding(FMargin(0,8,0,0));
        MainPanel=LDWidgets::Panel(WidgetTree,Root,FMargin(20));
        auto* WidthBox=WidgetTree->ConstructWidget<USizeBox>();WidthBox->SetWidthOverride(760);WidthBox->SetContent(MainPanel);
        auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor::Transparent);Frame->SetHorizontalAlignment(HAlign_Center);Frame->SetVerticalAlignment(VAlign_Top);
        Frame->SetPadding(FMargin(0,110,0,0));Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Frame->SetContent(WidthBox);
        WidgetTree->RootWidget=Frame;
        SetVisibility(bOpen?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    }
    return Super::RebuildWidget();
}

UWidget* ULDDevTestPanel::FocusScope() const{return MainPanel;}
UWidget* ULDDevTestPanel::HitArea() const{return MainPanel;}

void ULDDevTestPanel::OpenPanel()
{
    // Like the other own panels, opening never takes keyboard focus; a text field takes it only when clicked.
    bOpen=true;SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

bool ULDDevTestPanel::ClosePanel()
{
    bOpen=false;SetVisibility(ESlateVisibility::Collapsed);return true;
}

void ULDDevTestPanel::BuildCatalog(const FLDDemoContentDefinitions& Content)
{
    // Rows are built once from the catalog this process plays. A hot reload changes numbers only, never the ids, so
    // later presents refresh the texts in place and every stable button name keeps naming one widget.
    if(bCatalogBuilt||!ItemList||!LearnList){return;}
    bCatalogBuilt=true;
    auto MakeText=[this](const FString& Value,int32 FontSize,const FLinearColor& TextColor)
    {
        UTextBlock* Label=LDWidgets::Text(WidgetTree,Value,FontSize);Label->SetColorAndOpacity(TextColor);ScaleNewText(Label);return Label;
    };
    auto MakeButton=[this](const FString& Label,TFunction<void()> Callback,const FString& Name)
    {
        ULDCommandButton* Button=LDWidgets::Button(WidgetTree,Label,MoveTemp(Callback),false,FName(*Name));
        ScaleNewText(Cast<UTextBlock>(Button->GetContent()));return Button;
    };
    ShownItems=LDDevPanel::ItemRows(Content);
    for(int32 Index=0;Index<ShownItems.Num();++Index)
    {
        auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();ItemList->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,0,0,3));
        UTextBlock* Name=MakeText(TEXT(""),15,DevPanelBright);Line->AddChildToHorizontalBox(Name)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        UTextBlock* Price=MakeText(TEXT(""),13,DevPanelSecondary);Line->AddChildToHorizontalBox(Price)->SetVerticalAlignment(VAlign_Center);
        const FName Id=ShownItems[Index].Id;
        Line->AddChildToHorizontalBox(MakeButton(TEXT("购买"),[this,Id](){Send(ELDDevTestOp::BuyItem,0,Id);},FString::Printf(TEXT("DevTestBuy%d"),Index)))
            ->SetPadding(FMargin(8,0,0,0));
        ItemNames.Add(Name);ItemPrices.Add(Price);
    }
    ShownLearn=LDDevPanel::LearnRows(Content,nullptr);
    for(int32 Index=0;Index<ShownLearn.Num();++Index)
    {
        auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();LearnList->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,0,0,3));
        UTextBlock* Name=MakeText(TEXT(""),15,DevPanelBright);Line->AddChildToHorizontalBox(Name)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Line->AddChildToHorizontalBox(MakeText(ShownLearn[Index].Kind,13,DevPanelSecondary))->SetVerticalAlignment(VAlign_Center);
        UTextBlock* Held=MakeText(TEXT(""),13,DevPanelDim);
        UHorizontalBoxSlot* HeldSlot=Line->AddChildToHorizontalBox(Held);HeldSlot->SetPadding(FMargin(10,0,0,0));HeldSlot->SetVerticalAlignment(VAlign_Center);
        const FName Id=ShownLearn[Index].Id;
        Line->AddChildToHorizontalBox(MakeButton(TEXT("学习"),[this,Id](){Send(ELDDevTestOp::LearnOption,0,Id);},FString::Printf(TEXT("DevTestLearn%d"),Index)))
            ->SetPadding(FMargin(8,0,0,0));
        // Replacing is a confirmed choice of its own; it is offered only while both active slots are held.
        auto* Replace=WidgetTree->ConstructWidget<UHorizontalBox>();
        Replace->AddChildToHorizontalBox(MakeButton(TEXT("替换 Q"),[this,Id](){Send(ELDDevTestOp::LearnOption,0,Id,1);},FString::Printf(TEXT("DevTestLearnQ%d"),Index)))
            ->SetPadding(FMargin(6,0,0,0));
        Replace->AddChildToHorizontalBox(MakeButton(TEXT("替换 W"),[this,Id](){Send(ELDDevTestOp::LearnOption,0,Id,2);},FString::Printf(TEXT("DevTestLearnW%d"),Index)))
            ->SetPadding(FMargin(6,0,0,0));
        Replace->SetVisibility(ESlateVisibility::Collapsed);
        Line->AddChildToHorizontalBox(Replace);
        LearnNames.Add(Name);LearnHeld.Add(Held);LearnReplace.Add(Replace);
    }
}

void ULDDevTestPanel::Present(const FLDProgressionView* Progression,const FLDEquipmentView* Equipment,const FLDCombatOwnerView* Combat,
    const FLDDemoContentDefinitions* Content,bool bHost)
{
    if(!MainPanel){return;}
    if(Content){BuildCatalog(*Content);}
    if(Content)
    {
        const TArray<LDDevPanel::FItemRow> Items=LDDevPanel::ItemRows(*Content);
        for(int32 Index=0;Index<Items.Num()&&Index<ShownItems.Num()&&Index<ItemNames.Num();++Index)
        {
            if(Items[Index].Id!=ShownItems[Index].Id){continue;}
            DevPanelSetText(ItemNames[Index],Items[Index].Name);
            DevPanelSetText(ItemPrices[Index],FString::Printf(TEXT("%s · %s"),*Items[Index].Traits,*LDHUD::Gold(Items[Index].Price)));
        }
    }
    DevPanelSetText(StatusText,LDDevPanel::StatusLine(Progression,Equipment));
    const TConstArrayView<LDDevTest::FStatField> Fields=LDDevTest::StatFields();
    for(int32 Index=0;Index<Fields.Num()&&Index<StatLines.Num();++Index)
    {
        DevPanelSetText(StatLines[Index],LDDevPanel::AttributeLine(Combat,Fields[Index].Attribute));
    }
    for(uint8 SlotNumber=1;SlotNumber<=2;++SlotNumber){DevPanelSetText(SlotLines[SlotNumber-1],LDDevPanel::SlotLine(Equipment,Content,SlotNumber));}
    const bool bBothHeld=LDDevPanel::BothActiveSlotsHeld(Progression);
    const TArray<LDDevPanel::FLearnRow> Now=Content?LDDevPanel::LearnRows(*Content,Progression):TArray<LDDevPanel::FLearnRow>();
    for(int32 Index=0;Index<Now.Num()&&Index<ShownLearn.Num()&&Index<LearnHeld.Num()&&Index<LearnReplace.Num();++Index)
    {
        if(Now[Index].Id!=ShownLearn[Index].Id){continue;}
        DevPanelSetText(LearnNames[Index],Now[Index].Name);
        DevPanelSetText(LearnHeld[Index],Progression&&Progression->bValid?LDDevPanel::HeldText(Now[Index]):FString(DevPanelSyncing));
        DevPanelShow(LearnReplace[Index],bBothHeld&&Now[Index].bActiveSkill&&Now[Index].Held==0,ESlateVisibility::SelfHitTestInvisible);
    }
    const bool bChoice=Progression&&Progression->bValid&&Progression->PendingChoiceCount>0;
    DevPanelSetText(LearnHint,bChoice?FString(TEXT("学习受真实规则约束：等级上限、Q/W 两槽与选择数上限；Q/W 都满时用“替换”。"))
        :FString::Printf(TEXT("现在没有未用的选择机会：先加%s升级，再学习。"),LDTerms::Experience));
    DevPanelShow(HostSection,bHost,ESlateVisibility::SelfHitTestInvisible);
}

void ULDDevTestPanel::ShowFeedback(const FString& Text)
{
    Feedback=Text;
    DevPanelSetText(FeedbackLine,Text);
}

bool ULDDevTestPanel::Send(ELDDevTestOp Op,int64 Amount,FName Definition,uint8 SlotIndex,uint8 Stat)
{
    if(!Submit){ShowFeedback(TEXT("测试命令通道不可用"));return false;}
    // A Listen Host answers inside Submit, so "sent" is shown first and the authority's answer replaces it.
    ShowFeedback(FString::Printf(TEXT("%s 已发送"),LDDevTest::OpLabel(Op)));
    ELDGrowthReason Reason=ELDGrowthReason::None;
    if(Submit(Op,Amount,Reason,Definition,SlotIndex,Stat)==0)
    {
        ShowFeedback(FString::Printf(TEXT("%s 没有发送（%s）"),LDDevTest::OpLabel(Op),LDGrowthWire::ReasonName(Reason)));
        return false;
    }
    return true;
}

bool ULDDevTestPanel::SendAmount(ELDDevTestOp Op,const FString& Text)
{
    int64 Amount=0;
    if(!LDDevTest::ParseAmount(Text,Amount))
    {
        ShowFeedback(FString::Printf(TEXT("%s：请输入正整数（最多 18 位）"),LDDevTest::OpLabel(Op)));
        return false;
    }
    return Send(Op,Amount);
}

bool ULDDevTestPanel::SendStat(int32 FieldIndex,const FString& Text)
{
    const TConstArrayView<LDDevTest::FStatField> Fields=LDDevTest::StatFields();
    if(!Fields.IsValidIndex(FieldIndex)){return false;}
    const LDDevTest::FStatField& Field=Fields[FieldIndex];
    int64 Amount=0;
    if(!LDDevTest::ParseScaled(Text,Field.Decimals,Amount))
    {
        ShowFeedback(FString::Printf(TEXT("%s：请输入不小于 0 的数%s"),*LDDevPanel::StatLabel(Field),Field.Decimals?TEXT("，最多两位小数"):TEXT("，只能是整数")));
        return false;
    }
    return Send(ELDDevTestOp::SetAttribute,Amount,NAME_None,0,Field.Stat);
}

void ULDDevTestPanel::HandleResult(const FLDDevTestResult& Result)
{
    ShowFeedback(LDDevPanel::ResultLine(Result));
}
