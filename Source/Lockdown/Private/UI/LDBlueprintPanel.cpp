#include "UI/LDBlueprintPanel.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
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
            for(const FLDBlueprintParameter& Parameter:Component.Parameters){Parameters.Add(Parameter.Key+TEXT("=")+Parameter.Value);}
            FString Row=BlueprintName(Definitions,Component.Component);
            if(!Component.Host.IsEmpty()){Row+=TEXT(" @")+Component.Host;}
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
        UTextBlock* Title=MakeText(LDTerms::BuildBlueprint,20,BlueprintBright);Title->SetAutoWrapText(false);
        Top->AddChildToHorizontalBox(Title)->SetVerticalAlignment(VAlign_Center);
        UHorizontalBoxSlot* NoteSlot=Top->AddChildToHorizontalBox(MakeText(LDTerms::BlueprintReadOnly,13,BlueprintDim));
        NoteSlot->SetPadding(FMargin(12,0,12,0));NoteSlot->SetVerticalAlignment(VAlign_Center);NoteSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Top->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("关闭"),[this](){ClosePanel();},false,TEXT("BlueprintClose")));
        SourceText=MakeText(TEXT(""),15,BlueprintBright);Root->AddChildToVerticalBox(SourceText)->SetPadding(FMargin(0,10,0,0));
        StatusText=MakeText(TEXT(""),14,LDHUDStyle::TextSecondary);Root->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0,4,0,0));
        NoticeText=MakeText(TEXT(""),14,LDHUDStyle::TextWarning);Root->AddChildToVerticalBox(NoticeText)->SetPadding(FMargin(0,4,0,0));
        NoticeText->SetVisibility(ESlateVisibility::Collapsed);
        auto* Body=WidgetTree->ConstructWidget<UVerticalBox>();
        BodyText=MakeText(TEXT(""),14,BlueprintBright);Body->AddChildToVerticalBox(BodyText);
        FileList=WidgetTree->ConstructWidget<UVerticalBox>();Body->AddChildToVerticalBox(FileList);FileList->SetVisibility(ESlateVisibility::Collapsed);
        Scroll=WidgetTree->ConstructWidget<UScrollBox>();Scroll->SetClipping(EWidgetClipping::ClipToBounds);Scroll->AddChild(Body);
        auto* HeightBox=WidgetTree->ConstructWidget<USizeBox>();HeightBox->SetMaxDesiredHeight(560);HeightBox->SetContent(Scroll);
        Root->AddChildToVerticalBox(HeightBox)->SetPadding(FMargin(0,12,0,0));
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
        MainPanel=LDWidgets::Panel(WidgetTree,Root,FMargin(20));
        auto* WidthBox=WidgetTree->ConstructWidget<USizeBox>();WidthBox->SetWidthOverride(820);WidthBox->SetContent(MainPanel);
        auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor::Transparent);Frame->SetHorizontalAlignment(HAlign_Center);Frame->SetVerticalAlignment(VAlign_Center);
        Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Frame->SetContent(WidthBox);
        WidgetTree->RootWidget=Frame;
        SetVisibility(bOpen?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    }
    return Super::RebuildWidget();
}
UWidget* ULDBlueprintPanel::FocusScope() const{return MainPanel;}
UWidget* ULDBlueprintPanel::HitArea() const{return MainPanel;}
void ULDBlueprintPanel::OpenPanel()
{
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
    BlueprintText(SourceText,TEXT("本局（只给你自己看）"));
    BlueprintShow(BodyText,true,ESlateVisibility::HitTestInvisible);
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
        BlueprintText(BodyText,Describe(Record,Local));
        break;
    }
    }
    BlueprintText(NoticeText,FString::Join(Notices,TEXT("\n")));BlueprintShow(NoticeText,!Notices.IsEmpty(),ESlateVisibility::HitTestInvisible);
}
void ULDBlueprintPanel::ShowList()
{
    if(!MainPanel){return;}
    Mode=EMode::List;
    BlueprintShow(BackButton,true);BlueprintShow(ExportButton,false);BlueprintShow(ImportButton,false);
    BlueprintShow(BodyText,false);BlueprintShow(NoticeText,false);
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
    FileList->ClearChildren();
    BlueprintShow(FileList,false);BlueprintShow(BackButton,true);BlueprintShow(ExportButton,false);BlueprintShow(ImportButton,true);
    BlueprintShow(BodyText,true,ESlateVisibility::HitTestInvisible);
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
        BlueprintText(BodyText,Describe(Record,Local));
    }
    BlueprintText(NoticeText,FString::Join(Notices,TEXT("\n")));BlueprintShow(NoticeText,!Notices.IsEmpty(),ESlateVisibility::HitTestInvisible);
    Scroll->ScrollToStart();
}
void ULDBlueprintPanel::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(bOpen&&Mode==EMode::Match){RefreshMatch();}
    const ALDPlayerController* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(PC&&PC->Controls){ApplyTextScale(PC->Controls->TextScale);}
}
