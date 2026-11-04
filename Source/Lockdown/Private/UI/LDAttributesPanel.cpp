#include "UI/LDAttributesPanel.h"
#include "UI/LDHUDModel.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDGrowthProtocol.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
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
constexpr int32 AttrPanelRowCount=int32(ELDProjectedAttribute::Count);
// Approved meanings (APP_SPEC 3.2) in the player terms of APP_SPEC 1.2, in ELDProjectedAttribute order.
const TCHAR* const AttrPanelNotes[]=
{
    TEXT("存活资源的上限；面子归零即社死。"),
    TEXT("技能资源的上限。"),
    TEXT("决定弹脑瓜崩及具有手劲加成的技能效果。"),
    TEXT("决定具有缺德值加成的技能效果；不会让弹脑瓜崩变成社死伤害。"),
    TEXT("影响承受的皮肉伤。"),
    TEXT("影响承受的社死伤害。"),
    TEXT("影响弹脑瓜崩的频率。"),
    TEXT("影响行走与追击速度。"),
    TEXT("影响弹脑瓜崩的有效距离，不等同于技能的施法距离。"),
    TEXT("面子的自然恢复。"),
    TEXT("胆量的自然恢复。"),
    TEXT("影响允许正中要害的攻击；不是所有技能都能正中要害。"),
    TEXT("影响适用主动技能的再次使用时间。")
};
static_assert(int32(UE_ARRAY_COUNT(AttrPanelNotes))==AttrPanelRowCount,"One approved note per projected attribute.");
const TCHAR* const AttrPanelSyncing=TEXT("同步中");
const TCHAR* const AttrPanelEmpty=TEXT("暂无");
const FLinearColor AttrPanelBright(.9f,.94f,.96f),AttrPanelSecondary(.74f,.81f,.85f),AttrPanelDim(.55f,.63f,.68f),
    AttrPanelHeading(1,.82f,.4f),AttrPanelCardColor(.045f,.075f,.105f,1);
void AttrPanelText(UTextBlock* Label,const FString& Value)
{
    if(Label&&!Label->GetText().ToString().Equals(Value,ESearchCase::CaseSensitive)){Label->SetText(FText::FromString(Value));}
}
void AttrPanelShow(UWidget* Target,bool bShow)
{
    const ESlateVisibility Wanted=bShow?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed;
    if(Target&&Target->GetVisibility()!=Wanted){Target->SetVisibility(Wanted);}
}
FString AttrPanelLines(const TArray<FString>& Lines)
{
    return Lines.IsEmpty()?FString(AttrPanelEmpty):FString::Join(Lines,TEXT("\n"));
}
// Learned passives from the growth projection, each with only its learned rank's effects.
FString AttrPanelLearned(const FLDCombatOwnerView* Combat,const FLDProgressionView* Progression,const FLDDemoContentDefinitions* Definitions)
{
    // A progression of another match generation than the current combat projection is stale, not empty.
    if(!Progression||!Progression->bValid||(Combat&&Progression->Generation!=Combat->Generation)){return AttrPanelSyncing;}
    TArray<FString> Lines;
    for(const FLDLearnedSkillView& Skill:Progression->Skills)
    {
        if(Skill.bActive){continue;}
        const FString Id=Skill.DefinitionId.ToString();
        Lines.Add(FString::Printf(TEXT("%s %d 级"),*LDHUD::ContentName(Definitions,Id),int32(Skill.Rank)));
        const FLDDemoAbilityDefinition* Ability=Definitions?Definitions->Abilities.FindByPredicate([&Id](const FLDDemoAbilityDefinition& D){return D.Id==Id;}):nullptr;
        if(!Ability){if(Definitions){Lines.Add(TEXT("  内容目录中找不到该技能"));}continue;}
        const FLDDemoRankDefinition* Rank=Ability->Ranks.FindByPredicate([&Skill](const FLDDemoRankDefinition& R){return R.Rank==int32(Skill.Rank);});
        if(!Rank){Lines.Add(FString::Printf(TEXT("  缺少 %d 级定义"),int32(Skill.Rank)));continue;}
        for(const FString& Line:LDHUD::RankLines(*Rank,false)){Lines.Add(TEXT("  ")+Line);}
    }
    return AttrPanelLines(Lines);
}
// Combat applies each equipment passive family once. An entry is matched to an item id first and then to a passive
// family; when neither exists the stable id stays visible as a content error instead of a guessed name.
FString AttrPanelGear(const FLDCombatOwnerView* Combat,const FLDDemoContentDefinitions* Definitions)
{
    if(!Combat){return AttrPanelSyncing;}
    TArray<FString> Lines;
    TArray<FName> Seen;
    for(const FName& Passive:Combat->EquipmentPassives)
    {
        if(Seen.Contains(Passive)){continue;}
        Seen.Add(Passive);
        const FString Id=Passive.ToString();
        const FLDDemoEquipmentDefinition* Item=nullptr;
        if(Definitions)
        {
            Item=Definitions->Equipment.FindByPredicate([&Id](const FLDDemoEquipmentDefinition& D){return D.HasPassive&&D.Id==Id;});
            if(!Item){Item=Definitions->Equipment.FindByPredicate([&Id](const FLDDemoEquipmentDefinition& D){return D.HasPassive&&D.Passive.Family==Id;});}
        }
        Lines.Add(LDHUD::ContentName(Definitions,Item?Item->Id:Id));
        if(!Item){if(Definitions){Lines.Add(TEXT("  内容目录中找不到该装备被动"));}continue;}
        for(const FLDDemoEffectDefinition& Effect:Item->Passive.Effects){Lines.Add(TEXT("  ")+LDHUD::EffectLine(Effect));}
    }
    return AttrPanelLines(Lines);
}
FString AttrPanelStatuses(const FLDCombatOwnerView* Combat,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* Definitions)
{
    if(!Combat){return AttrPanelSyncing;}
    TArray<FString> Lines;
    for(const FLDCombatStatusView& Status:Combat->Statuses)
    {
        FString Line=LDHUD::StatusName(Definitions,Status.StatusId);
        // Shield points round up so a live shield never reads 0; a remainder needs the matching authority clock.
        if(Status.AmountP2>0){Line+=FString::Printf(TEXT(" · 淡定 %lld"),Status.AmountP2/100+(Status.AmountP2%100!=0?1:0));}
        if(Status.ExpiresAtMs>0&&bClock){Line+=TEXT(" · 剩余 ")+LDHUD::TenthsUp(Status.ExpiresAtMs-NowMs);}
        Lines.Add(MoveTemp(Line));
    }
    if(Combat->bOutOfCombatRegenActive){Lines.Add(TEXT("脱战回复生效中"));}
    // D21 (#79): the dorm's fast recovery, or the reminder that it starts once out of combat.
    if(Combat->bDormRegenActive){Lines.Add(TEXT("宿舍快回生效中"));}
    else if(Combat->bInDorm){Lines.Add(TEXT("在宿舍 · 脱战后快速回复"));}
    return AttrPanelLines(Lines);
}
}

TSharedRef<SWidget> ULDAttributesPanel::RebuildWidget()
{
    if(!MainPanel)
    {
        auto MakeText=[this](const FString& Value,int32 FontSize,const FLinearColor& TextColor,bool bWrap)
        {
            UTextBlock* Label=LDWidgets::Text(WidgetTree,Value,FontSize);Label->SetColorAndOpacity(TextColor);Label->SetAutoWrapText(bWrap);return Label;
        };
        auto MakeCard=[this](UWidget* Inner)
        {
            UBorder* Card=LDWidgets::Panel(WidgetTree,Inner,FMargin(12,8));Card->SetBrushColor(AttrPanelCardColor);Card->SetVisibility(ESlateVisibility::HitTestInvisible);return Card;
        };
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Top=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Top);
        Top->AddChildToHorizontalBox(MakeText(TEXT("属性"),20,AttrPanelBright,false))->SetVerticalAlignment(VAlign_Center);
        UHorizontalBoxSlot* HintSlot=Top->AddChildToHorizontalBox(MakeText(TEXT("[P]"),14,AttrPanelDim,false));
        HintSlot->SetPadding(FMargin(10,0,0,0));HintSlot->SetVerticalAlignment(VAlign_Center);HintSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Top->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("关闭"),[this](){ClosePanel();},false,TEXT("AttributesClose")));
        auto* Body=WidgetTree->ConstructWidget<UVerticalBox>();
        RowFinals.Reset();RowBreakdowns.Reset();
        for(int32 RowIndex=0;RowIndex<AttrPanelRowCount;++RowIndex)
        {
            // The name comes from the same formatter as the values, so the player term table keeps one source.
            FLDCombatAttributeRowView Probe;Probe.Attribute=uint8(RowIndex);
            auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();
            auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();Lines->AddChildToVerticalBox(Head);
            UTextBlock* NameText=MakeText(LDHUD::AttributeRow(Probe).Name,17,AttrPanelBright,true);NameText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold",17));
            Head->AddChildToHorizontalBox(NameText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            UTextBlock* FinalText=MakeText(AttrPanelSyncing,17,AttrPanelBright,false);
            UHorizontalBoxSlot* FinalSlot=Head->AddChildToHorizontalBox(FinalText);
            FinalSlot->SetPadding(FMargin(12,0,0,0));FinalSlot->SetHorizontalAlignment(HAlign_Right);FinalSlot->SetVerticalAlignment(VAlign_Center);
            UTextBlock* BreakdownText=MakeText(TEXT(""),13,AttrPanelSecondary,true);BreakdownText->SetVisibility(ESlateVisibility::Collapsed);
            Lines->AddChildToVerticalBox(BreakdownText)->SetPadding(FMargin(0,2,0,0));
            Lines->AddChildToVerticalBox(MakeText(AttrPanelNotes[RowIndex],13,AttrPanelDim,true))->SetPadding(FMargin(0,2,0,0));
            Body->AddChildToVerticalBox(MakeCard(Lines))->SetPadding(FMargin(0,0,0,6));
            RowFinals.Add(FinalText);RowBreakdowns.Add(BreakdownText);
        }
        auto AddSection=[&](const TCHAR* Title)
        {
            Body->AddChildToVerticalBox(MakeText(Title,16,AttrPanelHeading,false))->SetPadding(FMargin(0,10,0,4));
            UTextBlock* Section=MakeText(AttrPanelSyncing,14,AttrPanelBright,true);
            Body->AddChildToVerticalBox(MakeCard(Section));
            return Section;
        };
        LearnedPassiveText=AddSection(TEXT("角色被动"));
        EquipmentPassiveText=AddSection(TEXT("装备被动"));
        StatusText=AddSection(TEXT("临时状态"));
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Scroll->SetClipping(EWidgetClipping::ClipToBounds);Scroll->AddChild(Body);
        auto* HeightBox=WidgetTree->ConstructWidget<USizeBox>();HeightBox->SetMaxDesiredHeight(620);HeightBox->SetContent(Scroll);
        Root->AddChildToVerticalBox(HeightBox)->SetPadding(FMargin(0,12,0,0));
        Root->AddChildToVerticalBox(MakeText(TEXT("数值由战斗属性投影统一计算，界面只显示，不另行重算。"),13,AttrPanelDim,true))->SetPadding(FMargin(0,10,0,0));
        MainPanel=LDWidgets::Panel(WidgetTree,Root,FMargin(20));
        auto* WidthBox=WidgetTree->ConstructWidget<USizeBox>();WidthBox->SetWidthOverride(720);WidthBox->SetContent(MainPanel);
        auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor::Transparent);Frame->SetHorizontalAlignment(HAlign_Center);Frame->SetVerticalAlignment(VAlign_Top);
        Frame->SetPadding(FMargin(0,140,0,0));Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Frame->SetContent(WidthBox);
        WidgetTree->RootWidget=Frame;
        SetVisibility(bOpen?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    }
    return Super::RebuildWidget();
}
UWidget* ULDAttributesPanel::FocusScope() const{return MainPanel;}
UWidget* ULDAttributesPanel::HitArea() const{return MainPanel;}
bool ULDAttributesPanel::IsPanelOpen() const{return bOpen;}
void ULDAttributesPanel::OpenPanel()
{
    // Opening never takes keyboard focus: world input and any armed action stay where they were.
    bOpen=true;SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}
bool ULDAttributesPanel::ClosePanel()
{
    bOpen=false;SetVisibility(ESlateVisibility::Collapsed);return true;
}
void ULDAttributesPanel::Present(const FLDCombatOwnerView* Combat,const FLDProgressionView* Progression,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* Definitions)
{
    if(!MainPanel){return;}
    // Published rows are keyed by their Attribute id; the first row of an id wins and an absent id stays syncing.
    const FLDCombatAttributeRowView* ByAttribute[AttrPanelRowCount]={};
    if(Combat)
    {
        for(const FLDCombatAttributeRowView& Row:Combat->Attributes)
        {
            if(Row.Attribute<AttrPanelRowCount&&!ByAttribute[Row.Attribute]){ByAttribute[Row.Attribute]=&Row;}
        }
    }
    for(int32 RowIndex=0;RowIndex<AttrPanelRowCount&&RowIndex<RowFinals.Num()&&RowIndex<RowBreakdowns.Num();++RowIndex)
    {
        const FLDCombatAttributeRowView* Row=ByAttribute[RowIndex];
        const LDHUD::FAttributeRowView Shown=Row?LDHUD::AttributeRow(*Row):LDHUD::FAttributeRowView();
        AttrPanelText(RowFinals[RowIndex].Get(),Row?Shown.Final:FString(AttrPanelSyncing));
        AttrPanelText(RowBreakdowns[RowIndex].Get(),Shown.Breakdown);
        AttrPanelShow(RowBreakdowns[RowIndex].Get(),!Shown.Breakdown.IsEmpty());
    }
    AttrPanelText(LearnedPassiveText,AttrPanelLearned(Combat,Progression,Definitions));
    AttrPanelText(EquipmentPassiveText,AttrPanelGear(Combat,Definitions));
    AttrPanelText(StatusText,AttrPanelStatuses(Combat,bClock,NowMs,Definitions));
}
