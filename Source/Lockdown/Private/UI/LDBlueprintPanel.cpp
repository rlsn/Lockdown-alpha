#include "UI/LDBlueprintPanel.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"
#include "UI/LDHUD.h"
#include "UI/LDChoicesPanel.h"
#include "Core/LDLog.h"
#include "Development/LDBuildBlueprintStore.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDBuildBlueprintComponent.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDPlayerController.h"
#include "UI/LDControlSettings.h"
#include "Misc/Paths.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/Image.h"
#include "Components/WrapBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

static_assert(UE_ARRAY_COUNT(LDTerms::BlueprintFactLabels)==int32(ELDBlueprintFact::Count),"One label per blueprint fact.");

namespace
{
constexpr int32 BlueprintListLimit=50;
const FLinearColor BlueprintBright(.9f,.94f,.96f),BlueprintDim(.55f,.63f,.68f);

void BlueprintText(UTextBlock* Label,const FString& Value)
{
    if(Label&&!Label->GetText().ToString().Equals(Value,ESearchCase::CaseSensitive)){Label->SetText(FText::FromString(Value));}
}
void BlueprintShow(UWidget* Target,bool bShow,ESlateVisibility Shown=ESlateVisibility::Visible)
{
    const ESlateVisibility Wanted=bShow?Shown:ESlateVisibility::Collapsed;
    if(Target&&Target->GetVisibility()!=Wanted){Target->SetVisibility(Wanted);}
}
// Authority ms against the match start (T=0); the opening countdown reads as before the start.
FString BlueprintClock(int64 Ms,int64 StartMs)
{
    if(Ms<StartMs){return TEXT("开局前");}
    const int64 Seconds=(Ms-StartMs)/1000;
    return FString::Printf(TEXT("%lld:%02lld"),Seconds/60,Seconds%60);
}
FString BlueprintPoints(int64 P2){return FString::Printf(TEXT("%lld"),(P2+50)/100);}
FString BlueprintName(const FLDDemoContentDefinitions* Definitions,const FString& Id)
{
    // ContentText leaves the stable id when the local catalog has no display row, so a stale id stays visible.
    return Definitions?LDHUD::ContentName(Definitions,Id):Id;
}
void BlueprintInstall(const FLDBlueprintComponent& Component,const FLDDemoContentDefinitions* Definitions,
    FString& Where,TArray<FString>& Parameters)
{
    Where=Component.Host.IsEmpty()?FString(TEXT("角色")):BlueprintName(Definitions,Component.Host);
    for(const FLDBlueprintParameter& Parameter:Component.Parameters)
    {
        if(Parameter.Key==TEXT("target_kind"))
        {
            if(Parameter.Value==TEXT("dash")){Where=TEXT("冲刺（D）");}
            else if(Component.Host.IsEmpty()&&Parameter.Value==TEXT("active")){Where=TEXT("未记录的技能");}
            else if(Component.Host.IsEmpty()&&Parameter.Value==TEXT("component")){Where=TEXT("未记录的构件");}
            continue;
        }
        if(Parameter.Key==TEXT("element"))
        {
            const ELDDemoElement Element=Parameter.Value==TEXT("fire")?ELDDemoElement::Fire:Parameter.Value==TEXT("water")
                ?ELDDemoElement::Water:Parameter.Value==TEXT("electric")?ELDDemoElement::Electric:ELDDemoElement::None;
            Parameters.Add(TEXT("绑定元素：")+(Element==ELDDemoElement::None?Parameter.Value:LDHUD::ElementName(Element)));
            continue;
        }
        if(Parameter.Key==TEXT("bound_component"))
        {
            Parameters.Add(TEXT("复用构件：")+BlueprintName(Definitions,Parameter.Value));
            continue;
        }
        Parameters.Add(Parameter.Key+TEXT("=")+Parameter.Value);
    }
}
FString BlueprintStructure(const FLDBlueprintStructure& Structure,const FLDDemoContentDefinitions* Definitions)
{
    TArray<FString> Slots;
    for(const TCHAR* Slot:{TEXT("Q"),TEXT("W"),TEXT("E")})
    {
        const FLDBlueprintActive* Active=Structure.Actives.FindByPredicate([Slot](const FLDBlueprintActive& Row){return Row.Slot==Slot;});
        Slots.Add(FString::Printf(TEXT("%s %s"),Slot,Active?*BlueprintName(Definitions,Active->Ability):TEXT("—")));
    }
    TArray<FString> Stats;
    for(const FLDBlueprintStat& Stat:Structure.Stats){Stats.Add(FString::Printf(TEXT("%s %d"),*BlueprintName(Definitions,Stat.Stat),Stat.Points));}
    FString Components;
    if(!Structure.bComponentsCollected){Components=LDTerms::NotCollected;}
    else if(Structure.Components.IsEmpty()){Components=TEXT("无");}
    else
    {
        TArray<FString> Rows;
        for(const FLDBlueprintComponent& Component:Structure.Components)
        {
            TArray<FString> Parameters;
            FString Where;
            BlueprintInstall(Component,Definitions,Where,Parameters);
            FString Row=BlueprintName(Definitions,Component.Component);
            Row+=TEXT(" → ")+Where;
            if(!Parameters.IsEmpty()){Row+=TEXT("（")+FString::Join(Parameters,TEXT("，"))+TEXT("）");}
            Rows.Add(MoveTemp(Row));
        }
        Components=FString::Join(Rows,TEXT(" · "));
    }
    return FString::Printf(TEXT("  主动：%s\n  %s：%s\n  构件：%s"),*FString::Join(Slots,TEXT(" · ")),LDTerms::StatPoints,
        Stats.IsEmpty()?TEXT("无"):*FString::Join(Stats,TEXT(" · ")),*Components);
}
FString BlueprintFacts(const FLDBlueprintVersion& Version)
{
    TArray<FString> Collected,Missing;
    for(int32 Index=0;Index<int32(ELDBlueprintFact::Count);++Index)
    {
        const FLDBlueprintFactValue& Fact=Version.Facts[Index];
        const ELDBlueprintFact Kind=ELDBlueprintFact(Index);
        if(!Fact.bCollected){Missing.Add(LDTerms::BlueprintFactLabels[Index]);continue;}
        const bool bCount=Kind==ELDBlueprintFact::BasicAttacks||Kind==ELDBlueprintFact::AbilityCasts||Kind==ELDBlueprintFact::Triggers
            ||Kind==ELDBlueprintFact::Rejections;
        Collected.Add(FString::Printf(TEXT("%s %s"),LDTerms::BlueprintFactLabels[Index],
            bCount?*FString::Printf(TEXT("%lld"),Fact.Value):*BlueprintPoints(Fact.Value)));
    }
    FString Out=FString::Printf(TEXT("  %s"),*FString::Join(Collected,TEXT(" · ")));
    if(!Missing.IsEmpty()){Out+=FString::Printf(TEXT("\n  %s：%s"),LDTerms::NotCollected,*FString::Join(Missing,TEXT("、")));}
    return Out;
}
FString BlueprintOutcome(const FLDBlueprintRecord& Record)
{
    const FString Ending=Record.EndReason==TEXT("host_ended")?FString(TEXT("房主结束了本局")):FString(TEXT("涂鸦守护结束"));
    if(Record.Outcome==TEXT("won")){return Ending+TEXT(" · 你是")+LDTerms::Winner;}
    if(Record.Outcome==TEXT("lost")){return Ending+TEXT(" · 别人守住了涂鸦");}
    return Ending+TEXT(" · ")+LDTerms::NoCapture;
}
}

FString ULDBlueprintPanel::Describe(const FLDBlueprintRecord& Record,const FLDDemoContentDefinitions* Definitions)
{
    TArray<FString> Lines;
    const int64 Seconds=FMath::Max<int64>(0,Record.DurationMs)/1000;
    Lines.Add(FString::Printf(TEXT("结果：%s · 用时 %lld:%02lld · 最终 Lv%u · %s %lld · %s %lld · %s %lld"),*BlueprintOutcome(Record),
        Seconds/60,Seconds%60,uint32(Record.FinalLevel),LDTerms::ResultKills,Record.Kills,LDTerms::ResultDeaths,Record.Deaths,
        LDTerms::ResultAssists,Record.Assists));
    Lines.Add(FString::Printf(TEXT("规则：产品 %s · 协议 %d · 目录 %s…"),*Record.ProductVersion,Record.WireProtocol,*Record.CatalogRoot.Left(12)));
    Lines.Add(FString());
    Lines.Add(FString::Printf(TEXT("最终构筑（修订 %llu）"),Record.FinalRevision));
    Lines.Add(BlueprintStructure(Record.FinalBuild,Definitions));
    Lines.Add(FString());
    Lines.Add(TEXT("每个版本只记它生效期间真实发生的结果；伤害每次命中只计一次，回复、护盾吸收与减免分开列。"));
    for(int32 Index=0;Index<Record.Versions.Num();++Index)
    {
        const FLDBlueprintVersion& Version=Record.Versions[Index];
        FString Head=FString::Printf(TEXT("版本 %d/%d · 修订 %llu–%llu · %s – %s · Lv%u–%u"),Index+1,Record.Versions.Num(),
            Version.FirstRevision,Version.LastRevision,*BlueprintClock(Version.FromMs,Record.StartedAtMs),
            *BlueprintClock(Version.ToMs,Record.StartedAtMs),uint32(Version.LevelFrom),uint32(Version.LevelTo));
        if(Version.bDevLayer){Head+=TEXT(" · DEV 测试层");}
        Lines.Add(FString());
        Lines.Add(MoveTemp(Head));
        Lines.Add(BlueprintStructure(Version.Structure,Definitions));
        Lines.Add(BlueprintFacts(Version));
    }
    return FString::Join(Lines,TEXT("\n"));
}

TSharedRef<SWidget> ULDBlueprintPanel::RebuildWidget()
{
    if(!MainPanel)
    {
        auto MakeText=[this](const FString& Value,int32 Size,const FLinearColor& Color)
        {
            UTextBlock* Label=LDWidgets::Text(WidgetTree,Value,Size);Label->SetColorAndOpacity(Color);return Label;
        };
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Top=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Top);
        UTextBlock* Title=MakeText(TEXT("构筑蓝图 · 对局记录"),20,BlueprintBright);Title->SetAutoWrapText(false);
        Top->AddChildToHorizontalBox(Title)->SetVerticalAlignment(VAlign_Center);
        UHorizontalBoxSlot* NoteSlot=Top->AddChildToHorizontalBox(MakeText(TEXT("只读 · 不授予物品或技能"),13,BlueprintDim));
        NoteSlot->SetPadding(FMargin(12,0,12,0));NoteSlot->SetVerticalAlignment(VAlign_Center);NoteSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Top->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("关闭"),[this](){ClosePanel();},false,TEXT("BlueprintClose")));
        SourceText=MakeText(TEXT(""),15,BlueprintBright);Root->AddChildToVerticalBox(SourceText)->SetPadding(FMargin(0,10,0,0));
        StatusText=MakeText(TEXT(""),14,LDHUDStyle::TextSecondary);Root->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0,4,0,0));
        NoticeText=MakeText(TEXT(""),14,LDHUDStyle::TextWarning);Root->AddChildToVerticalBox(NoticeText)->SetPadding(FMargin(0,4,0,0));
        NoticeText->SetVisibility(ESlateVisibility::Collapsed);
        auto* Body=WidgetTree->ConstructWidget<UVerticalBox>();
        StructureList=WidgetTree->ConstructWidget<UVerticalBox>();Body->AddChildToVerticalBox(StructureList);
        PreviewText=MakeText(TEXT("悬停图标查看 · 点击固定详情"),14,LDHUDStyle::TextSecondary);
        PreviewText->SetAutoWrapText(false);PreviewText->SetWrapTextAt(250);
        HistoryButton=LDWidgets::Button(WidgetTree,TEXT("展开对局数据与版本记录"),[this]()
        {bHistory=!bHistory;BlueprintShow(BodyText,bHistory,ESlateVisibility::HitTestInvisible);
        BlueprintText(Cast<UTextBlock>(HistoryButton->GetContent()),bHistory?TEXT("收起对局数据与版本记录"):TEXT("展开对局数据与版本记录"));},false,TEXT("BlueprintHistory"));
        Body->AddChildToVerticalBox(HistoryButton)->SetPadding(FMargin(0,8,0,8));
        BodyText=MakeText(TEXT(""),14,BlueprintBright);BodyText->SetWrapTextAt(720);Body->AddChildToVerticalBox(BodyText);BlueprintShow(BodyText,false);
        FileList=WidgetTree->ConstructWidget<UVerticalBox>();Body->AddChildToVerticalBox(FileList);FileList->SetVisibility(ESlateVisibility::Collapsed);
        Scroll=WidgetTree->ConstructWidget<UScrollBox>();Scroll->SetClipping(EWidgetClipping::ClipToBounds);Scroll->AddChild(Body);
        auto* HeightBox=WidgetTree->ConstructWidget<USizeBox>();HeightBox->SetWidthOverride(750);HeightBox->SetHeightOverride(490);HeightBox->SetContent(Scroll);
        auto* PreviewScroll=WidgetTree->ConstructWidget<UScrollBox>();PreviewScroll->SetClipping(EWidgetClipping::ClipToBounds);PreviewScroll->AddChild(PreviewText);
        auto* PreviewSize=WidgetTree->ConstructWidget<USizeBox>();PreviewSize->SetWidthOverride(270);PreviewSize->SetHeightOverride(490);PreviewSize->SetContent(PreviewScroll);
        auto* Columns=WidgetTree->ConstructWidget<UHorizontalBox>();
        Columns->AddChildToHorizontalBox(HeightBox);
        Columns->AddChildToHorizontalBox(PreviewSize)->SetPadding(FMargin(20,0,0,0));
        Root->AddChildToVerticalBox(Columns)->SetPadding(FMargin(0,12,0,0));
        auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,12,0,0));
        ExportButton=LDWidgets::Button(WidgetTree,LDTerms::BlueprintExport,[this]()
        {
            if(ULDBuildBlueprintComponent* Owner=Channel()){SetAction(Owner->ExportCurrent());}
        },false,TEXT("BlueprintExport"));
        Actions->AddChildToHorizontalBox(ExportButton)->SetPadding(FMargin(0,0,10,0));
        Actions->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,LDTerms::BlueprintOpenFolder,[this]()
        {
            if(ULDBuildBlueprintComponent* Owner=Channel()){SetAction(Owner->OpenFolder());}
        },false,TEXT("BlueprintOpenFolder")))->SetPadding(FMargin(0,0,10,0));
        ImportButton=LDWidgets::Button(WidgetTree,LDTerms::BlueprintImport,[this](){ShowList();},false,TEXT("BlueprintImport"));
        Actions->AddChildToHorizontalBox(ImportButton)->SetPadding(FMargin(0,0,10,0));
        BackButton=LDWidgets::Button(WidgetTree,LDTerms::BlueprintBack,[this](){ShowMatch();},false,TEXT("BlueprintBack"));
        Actions->AddChildToHorizontalBox(BackButton);BackButton->SetVisibility(ESlateVisibility::Collapsed);
        ActionText=MakeText(TEXT(""),13,BlueprintDim);Root->AddChildToVerticalBox(ActionText)->SetPadding(FMargin(0,8,0,0));
        for(UTextBlock* Label:{SourceText.Get(),StatusText.Get(),NoticeText.Get(),ActionText.Get()})
        {Label->SetAutoWrapText(false);Label->SetWrapTextAt(1020);}
        MainPanel=LDWidgets::Panel(WidgetTree,Root,FMargin(20));MainPanel->SetBrushColor(LDHUDStyle::PanelFill.CopyWithNewOpacity(1.f));
        auto* WidthBox=WidgetTree->ConstructWidget<USizeBox>();WidthBox->SetWidthOverride(1080);WidthBox->SetContent(MainPanel);
        auto* Scale=WidgetTree->ConstructWidget<UScaleBox>();Scale->SetStretch(EStretch::ScaleToFit);Scale->SetStretchDirection(EStretchDirection::DownOnly);Scale->SetContent(WidthBox);
        auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor::Transparent);Frame->SetHorizontalAlignment(HAlign_Center);Frame->SetVerticalAlignment(VAlign_Center);
        Frame->SetPadding(FMargin(32,90,32,175));Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Frame->SetContent(Scale);
        WidgetTree->RootWidget=Frame;
        SetVisibility(bOpen?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    }
    return Super::RebuildWidget();
}
UWidget* ULDBlueprintPanel::FocusScope() const{return MainPanel;}
UWidget* ULDBlueprintPanel::HitArea() const{return MainPanel;}
void ULDBlueprintPanel::OpenPanel()
{
    if(const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->HUDUI)
    {
        if(PC->HUDUI->ChoicesPanel){PC->HUDUI->ChoicesPanel->ClosePanel();}
    }
    bOpen=true;SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    SetAction(FString());
    ShowMatch();
}
bool ULDBlueprintPanel::ClosePanel()
{
    bOpen=false;SetVisibility(ESlateVisibility::Collapsed);return true;
}
ULDBuildBlueprintComponent* ULDBlueprintPanel::Channel() const
{
    const ALDPlayerController* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    return PC?PC->BuildBlueprints.Get():nullptr;
}
const FLDDemoContentDefinitions* ULDBlueprintPanel::Definitions()
{
    if(!bContentTried)
    {
        bContentTried=true;FString Error;
        if(!LDDemoEntry::LoadRuntimeContent(Content,Error)||!Content){Content.Reset();UE_LOG(LogLDUI,Warning,TEXT("LD_BUILD_BLUEPRINT_CONTENT_UNAVAILABLE %s"),*Error);}
        if(!LDDemoEntry::ReadProductVersion(LocalProduct,Error)){LocalProduct.Reset();}
    }
    return Content?&Content->Get():nullptr;
}
void ULDBlueprintPanel::SetAction(const FString& Text)
{
    BlueprintText(ActionText,Text);BlueprintShow(ActionText,!Text.IsEmpty(),ESlateVisibility::HitTestInvisible);
}
void ULDBlueprintPanel::ShowRecord(const FLDBlueprintRecord& Record,const FLDDemoContentDefinitions* Local)
{
    StructureList->ClearChildren();TilePreviews.Reset();PinnedDetail.Reset();bHistory=false;
    BlueprintShow(StructureList,true);BlueprintShow(PreviewText,true);BlueprintShow(HistoryButton,true);
    BlueprintShow(BodyText,false);BlueprintText(BodyText,Describe(Record,Local));
    BlueprintText(Cast<UTextBlock>(HistoryButton->GetContent()),TEXT("展开对局数据与版本记录"));
    BlueprintText(PreviewText,TEXT("悬停图标查看 · 点击固定详情\n这是整套构筑的对局记录，不是待学主动图纸；导入只用于查看，不会改变本局。"));
    const ALDPlayerController* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    const FLDProgressionView* Own=PC?PC->ReadOwnProgression():nullptr;
    LDHUD::FCardStats CardStats;
    CardStats.Level=Record.FinalLevel;
    CardStats.bShowScalingFormula=true;
    auto* Note=LDWidgets::Text(WidgetTree,FString::Printf(TEXT("说明按本机目录与蓝图 Lv%u 显示；属性加成按实际属性计算。"),
        uint32(Record.FinalLevel)),12);
    Note->SetColorAndOpacity(LDHUDStyle::TextSecondary);ScaleNewText(Note);
    StructureList->AddChildToVerticalBox(Note)->SetPadding(FMargin(0,0,0,8));
    auto* Slots=WidgetTree->ConstructWidget<UHorizontalBox>();
    StructureList->AddChildToVerticalBox(Slots)->SetPadding(FMargin(0,0,0,12));
    auto Tile=[this,Local](const FString& Id,const FString& Caption,const FString& State,const FString& Detail,float Width)
    {
        auto* Face=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Picture=WidgetTree->ConstructWidget<UImage>();LDUIIcons::Apply(Picture,LDHUD::ContentIconId(Local,Id),0);
        auto* ImageSize=WidgetTree->ConstructWidget<USizeBox>();ImageSize->SetWidthOverride(44);ImageSize->SetHeightOverride(44);ImageSize->SetContent(Picture);
        Face->AddChildToVerticalBox(ImageSize)->SetHorizontalAlignment(HAlign_Center);
        auto* Name=LDWidgets::Text(WidgetTree,Caption,14);Name->SetAutoWrapText(false);Name->SetWrapTextAt(Width-24);
        Face->AddChildToVerticalBox(Name)->SetPadding(FMargin(0,6,0,4));ScaleNewText(Name);
        auto* Held=LDWidgets::Text(WidgetTree,State,12);Held->SetAutoWrapText(false);Held->SetWrapTextAt(Width-24);Held->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        Face->AddChildToVerticalBox(Held);ScaleNewText(Held);
        auto* Button=LDWidgets::Button(WidgetTree,TEXT(""),[this,Detail](){PinnedDetail=Detail;BlueprintText(PreviewText,Detail);});
        Button->SetContent(Face);TilePreviews.Add({Button,Detail});
        auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(Width);Size->SetContent(Button);return Size;
    };
    for(uint8 Key=0;Key<3;++Key)
    {
        const FString KeyName=LDHUD::SlotKey(Key);
        const FLDBlueprintActive* Active=Record.FinalBuild.Actives.FindByPredicate([&KeyName](const FLDBlueprintActive& Row){return Row.Slot==KeyName;});
        const FString Id=Active?Active->Ability:FString();
        const FString Name=Active?BlueprintName(Local,Id):FString(TEXT("空槽"));
        const bool bHeld=Own&&(LDHUD::ActiveInSlot(*Own,Key)==FName(*Id)||Own->PendingBlueprints.Contains(FName(*Id))
            ||Own->Actives.ContainsByPredicate([&Id](const FLDActiveSlotView& Row){return Row.DefinitionId==FName(*Id);}));
        FString Description;
        if(Active)
        {
            const LDHUD::FActiveCardView Card=LDHUD::ActiveCard(FName(*Id),Local,CardStats);
            Description=Card.SentencePlain;
            if(Card.bKnown){Description+=TEXT("\n")+Card.Target+TEXT("\n")+Card.Cost;}
        }
        Slots->AddChildToHorizontalBox(Tile(Id,KeyName+TEXT(" · ")+Name,!Active?TEXT("未配置"):!Own?TEXT("本局持有状态不可用"):bHeld?TEXT("本局已有"):TEXT("本局缺少"),
            Name+TEXT("\n")+Description,232))->SetPadding(FMargin(0,0,8,0));
    }
    TArray<FString> Stats;
    for(const FLDBlueprintStat& Stat:Record.FinalBuild.Stats){Stats.Add(BlueprintName(Local,Stat.Stat)+FString::Printf(TEXT(" %d 点"),Stat.Points));}
    auto* Points=LDWidgets::Text(WidgetTree,TEXT("属性分配：")+FString::Join(Stats,TEXT(" · ")),14);
    Points->SetAutoWrapText(false);Points->SetWrapTextAt(720);ScaleNewText(Points);StructureList->AddChildToVerticalBox(Points)->SetPadding(FMargin(0,0,0,12));
    auto* Components=WidgetTree->ConstructWidget<UWrapBox>();Components->SetExplicitWrapSize(true);Components->SetWrapSize(730);Components->SetInnerSlotPadding(FVector2D(8,8));
    StructureList->AddChildToVerticalBox(Components);
    if(!Record.FinalBuild.bComponentsCollected)
    {auto* Missing=LDWidgets::Text(WidgetTree,TEXT("构件：未采集，不能视为没有构件。"),14);ScaleNewText(Missing);StructureList->AddChildToVerticalBox(Missing);}
    for(const FLDBlueprintComponent& Component:Record.FinalBuild.Components)
    {
        const FString Name=BlueprintName(Local,Component.Component);
        const bool bHeld=Own&&LDHUD::OwnsComponent(*Own,FName(*Component.Component));
        const LDHUD::FComponentCardView Card=LDHUD::ComponentCard(FName(*Component.Component),Local,CardStats);
        FString Target;
        TArray<FString> Parameters;
        BlueprintInstall(Component,Local,Target,Parameters);
        Components->AddChildToWrapBox(Tile(Component.Component,Name,!Own?TEXT("本局持有状态不可用"):bHeld?TEXT("本局已有"):TEXT("本局缺少"),
            Name+TEXT("\n安装位置：")+Target+TEXT("\n")+Card.SentencePlain
                +(Parameters.IsEmpty()?FString():TEXT("\n")+FString::Join(Parameters,TEXT("\n"))),138));
    }
}
void ULDBlueprintPanel::ShowMatch()
{
    Mode=EMode::Match;
    if(FileList){FileList->ClearChildren();}
    BlueprintShow(FileList,false);BlueprintShow(BackButton,false);BlueprintShow(ImportButton,true);
    ShownState=MAX_uint8;
    RefreshMatch();
}
void ULDBlueprintPanel::RefreshMatch()
{
    if(!MainPanel||Mode!=EMode::Match){return;}
    const ULDBuildBlueprintComponent* Owner=Channel();
    const ELDBlueprintDelivery State=Owner?Owner->Delivery():ELDBlueprintDelivery::None;
    const FGuid Result=Owner?Owner->DeliveredResult():FGuid();
    const bool bSaved=Owner&&Owner->IsSaved();
    const FString Problem=Owner?Owner->Problem():FString();
    if(ShownState==uint8(State)&&ShownResult==Result&&bShownSaved==bSaved&&ShownProblem==Problem){return;}
    ShownState=uint8(State);ShownResult=Result;bShownSaved=bSaved;ShownProblem=Problem;
    TilePreviews.Reset();PinnedDetail.Reset();StructureList->ClearChildren();
    BlueprintText(SourceText,TEXT("本局（只给你自己看）"));
    BlueprintShow(BodyText,false);BlueprintShow(StructureList,false);BlueprintShow(PreviewText,false);BlueprintShow(HistoryButton,false);
    BlueprintShow(ExportButton,State==ELDBlueprintDelivery::Received);
    TArray<FString> Notices;
    switch(State)
    {
    case ELDBlueprintDelivery::None:
        BlueprintText(StatusText,TEXT("本局结束后，房主会把你自己的构筑蓝图单独发给你。"));
        BlueprintText(BodyText,FString());
        break;
    case ELDBlueprintDelivery::Receiving:
        BlueprintText(StatusText,TEXT("正在接收本局的构筑蓝图…"));
        BlueprintText(BodyText,FString());
        break;
    case ELDBlueprintDelivery::Failed:
        BlueprintText(StatusText,FString(TEXT("本局没有构筑蓝图："))+Problem);
        BlueprintText(BodyText,FString());
        break;
    case ELDBlueprintDelivery::Received:
    {
        const FLDBlueprintRecord& Record=Owner->Record();
        BlueprintText(StatusText,bSaved
            ?FString::Printf(TEXT("已自动保存：%s%s"),*LDBuildBlueprintStore::DisplayPath(Owner->SavedPath()),
                Problem.IsEmpty()?TEXT(""):*(FString(TEXT(" · "))+Problem))
            :FString(TEXT("自动保存失败："))+Problem);
        const FLDDemoContentDefinitions* Local=Definitions();
        LDBuildBlueprint::Notices(Record,Local,LocalProduct,Notices);
        ShowRecord(Record,Local);
        break;
    }
    }
    BlueprintText(NoticeText,FString::Join(Notices,TEXT("\n")));BlueprintShow(NoticeText,!Notices.IsEmpty(),ESlateVisibility::HitTestInvisible);
}
void ULDBlueprintPanel::ShowList()
{
    if(!MainPanel){return;}
    Mode=EMode::List;
    TilePreviews.Reset();PinnedDetail.Reset();StructureList->ClearChildren();
    BlueprintShow(BackButton,true);BlueprintShow(ExportButton,false);BlueprintShow(ImportButton,false);
    BlueprintShow(BodyText,false);BlueprintShow(NoticeText,false);
    BlueprintShow(StructureList,false);BlueprintShow(PreviewText,false);BlueprintShow(HistoryButton,false);
    const FString Directory=LDBuildBlueprintStore::UserDirectory();
    TArray<FString> Files;LDBuildBlueprintStore::ListViewable(Directory,Files);
    BlueprintText(SourceText,LDTerms::BlueprintImport);
    BlueprintText(StatusText,Files.IsEmpty()
        ?FString::Printf(TEXT("%s 里还没有蓝图文件。收到的文件放进 Imports 后再来。"),*LDBuildBlueprintStore::DisplayPath(Directory))
        :FString(TEXT("选一个文件只读查看；读取前会严格校验格式、版本和大小。")));
    FileList->ClearChildren();
    for(int32 Index=0;Index<Files.Num()&&Index<BlueprintListLimit;++Index)
    {
        const FString Path=Files[Index];
        ULDCommandButton* Row=LDWidgets::Button(WidgetTree,LDBuildBlueprintStore::DisplayPath(Path),[this,Path](){ShowImported(Path);});
        auto* Caption=CastChecked<UTextBlock>(Row->GetContent());Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(700);
        ScaleNewText(Cast<UTextBlock>(Row->GetContent()));
        FileList->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,0,0,6));
    }
    BlueprintShow(FileList,true);
    Scroll->ScrollToStart();
}
void ULDBlueprintPanel::ShowImported(const FString& Path)
{
    if(!MainPanel){return;}
    Mode=EMode::Imported;
    TilePreviews.Reset();PinnedDetail.Reset();StructureList->ClearChildren();
    FileList->ClearChildren();
    BlueprintShow(FileList,false);BlueprintShow(BackButton,true);BlueprintShow(ExportButton,false);BlueprintShow(ImportButton,true);
    BlueprintShow(BodyText,false);BlueprintShow(StructureList,false);BlueprintShow(PreviewText,false);BlueprintShow(HistoryButton,false);
    BlueprintText(SourceText,FString::Printf(TEXT("导入（只读）：%s"),*LDBuildBlueprintStore::DisplayPath(Path)));
    FLDBlueprintRecord Record;FString Detail;
    const ELDBlueprintParseError Error=LDBuildBlueprintStore::ReadFile(Path,Record,Detail);
    TArray<FString> Notices;
    if(Error!=ELDBlueprintParseError::None)
    {
        BlueprintText(StatusText,FString(TEXT("无法导入查看："))+LDBuildBlueprint::DescribeError(Error,Detail));
        BlueprintText(BodyText,FString());
        UE_LOG(LogLDUI,Log,TEXT("LD_BUILD_BLUEPRINT_IMPORT_REFUSED file=%s reason=%s"),*FPaths::GetCleanFilename(Path),*LDLog::Quote(Detail));
    }
    else
    {
        BlueprintText(StatusText,Record.Origin==LDBuildBlueprint::OriginExport?TEXT("导出的蓝图 · 只读查看，不会改变你的构筑或能力"):TEXT("自动保存的蓝图 · 只读查看，不会改变你的构筑或能力"));
        const FLDDemoContentDefinitions* Local=Definitions();
        LDBuildBlueprint::Notices(Record,Local,LocalProduct,Notices);
        ShowRecord(Record,Local);
    }
    BlueprintText(NoticeText,FString::Join(Notices,TEXT("\n")));BlueprintShow(NoticeText,!Notices.IsEmpty(),ESlateVisibility::HitTestInvisible);
    Scroll->ScrollToStart();
}
void ULDBlueprintPanel::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(bOpen&&Mode==EMode::Match){RefreshMatch();}
    if(bOpen&&!TilePreviews.IsEmpty())
    {
        FString Detail=PinnedDetail;
        for(const FTilePreview& Tile:TilePreviews)
        {if(Tile.Button&&(Tile.Button->IsHovered()||Tile.Button->HasKeyboardFocus())){Detail=Tile.Detail;break;}}
        BlueprintText(PreviewText,Detail.IsEmpty()?TEXT("悬停图标查看 · 点击固定详情\n这是整套构筑的只读记录，不是待学主动图纸；导入不会改变本局。"):Detail);
    }
    const ALDPlayerController* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(PC&&PC->Controls){ApplyTextScale(PC->Controls->TextScale);}
}
