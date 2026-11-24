#include "UI/LDUIRoot.h"
#include "Core/LDLog.h"
#include "Framework/LDCaptureTypes.h"
#include "Framework/LDBuildBlueprintComponent.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDGameState.h"
#include "Framework/LDPlayerController.h"
#include "Development/LDDemoContentCatalog.h"
#include "Navigation/LDNavigationComponent.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDChainCodex.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IPAddress.h"
#include "Misc/ConfigCacheIni.h"
#include "SocketSubsystem.h"
#include "UI/LDHUD.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDBroadcastStyle.h"
#include "UI/LDBroadcastPaint.h"
#include "UI/LDResultBuild.h"
#include "UI/LDTerms.h"
#include "UI/LDUIBackgrounds.h"
#include "UI/LDWidgets.h"
#include "UI/LDWorldFeedback.h"
#include "UI/LDWorldLabels.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UI/LDTween.h"
#include "UI/LDUIPaint.h"
#include "UI/LDUIShapes.h"
#include "Engine/Engine.h"
#include "Engine/PendingNetGame.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"

namespace
{
    constexpr int32 MainPanelCount=2;

    int32 MainPanelIndex(ELDUIMainPanel Panel)
    {
        const int32 Index=int32(Panel)-int32(ELDUIMainPanel::Choices);
        return Index>=0&&Index<MainPanelCount?Index:INDEX_NONE;
    }

    bool PanelOpen(const ULDPanel* Panel){return Panel&&Panel->IsPanelOpen();}

    // The configured default port ([URL] Port or -Port=) that a listen Host binds.
    bool ConfiguredListenPort(int32& Out)
    {
        Out=FURL::UrlConfig.DefaultPort;
        return Out>=1&&Out<=65535;
    }

    bool ParsePort(const FString& Text,int32& Out)
    {
        Out=0;
        if(Text.IsEmpty()||Text.Len()>5){return false;}
        for(const TCHAR Character:Text){if(Character<TEXT('0')||Character>TEXT('9')){return false;}}
        Out=FCString::Atoi(*Text);
        return Out>=1&&Out<=65535;
    }

    // PublicMatch is the single phase source; DemoLobby.Phase only mirrors it for the roster.
    const FLDPublicMatchView* CurrentMatch(const ALDGameState* Public)
    {
        return Public&&Public->PublicMatch.Generation?&Public->PublicMatch:nullptr;
    }

    FString MatchPhaseText(const FLDPublicMatchView* Match)
    {
        if(!Match){return TEXT("正在同步房间状态…");}
        switch(Match->Phase)
        {
        case ELDMatchPhase::Lobby: return TEXT("房间等待开始");
        case ELDMatchPhase::Starting: return TEXT("准备开局…");
        case ELDMatchPhase::Rest: return LDTerms::RestPhase;
        case ELDMatchPhase::Development: return LDTerms::Development;
        case ELDMatchPhase::CaptureOpen: return LDTerms::CaptureOpened;
        case ELDMatchPhase::Results: return TEXT("对局结束");
        case ELDMatchPhase::Resetting: return TEXT("正在返回房间…");
        case ELDMatchPhase::Closing: return TEXT("房间正在关闭…");
        case ELDMatchPhase::Faulted: return TEXT("房间因权威错误已停止，请离开房间并返回主菜单");
        }
        return TEXT("未知阶段，请离开房间并返回主菜单");
    }

    // Empty lines collapse, so a cleared message never keeps its space or reads as stale text.
    void SetLine(UTextBlock* Text,const FString& Value)
    {
        if(!Text){return;}
        if(!Text->GetText().ToString().Equals(Value,ESearchCase::CaseSensitive)){Text->SetText(FText::FromString(Value));}
        Text->SetVisibility(Value.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    }
    using FRootPainter=TFunction<void(const LDUIPaint::FContext&)>;

    ULDPaintLayer* PaintLayer(UWidgetTree* Tree,FRootPainter Painter)
    {
        auto* Layer=Tree->ConstructWidget<ULDPaintLayer>();Layer->Painter=MoveTemp(Painter);
        return Layer;
    }

    // Child inset over a painted surface that fills the overlay.
    UOverlay* Painted(UWidgetTree* Tree,UWidget* Child,const FMargin& Inset,FRootPainter Painter)
    {
        auto* Surface=Tree->ConstructWidget<UOverlay>();
        UOverlaySlot* LayerSlot=Surface->AddChildToOverlay(PaintLayer(Tree,MoveTemp(Painter)));
        LayerSlot->SetHorizontalAlignment(HAlign_Fill);LayerSlot->SetVerticalAlignment(VAlign_Fill);
        UOverlaySlot* ChildSlot=Surface->AddChildToOverlay(Child);
        ChildSlot->SetHorizontalAlignment(HAlign_Fill);ChildSlot->SetVerticalAlignment(VAlign_Fill);ChildSlot->SetPadding(Inset);
        return Surface;
    }
}

TSharedRef<SWidget> ULDUIRoot::RebuildWidget()
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(PC&&WidgetTree&&!ConfirmLayer)
    {
        SetIsFocusable(true);
        auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();
        WidgetTree->RootWidget=Canvas;
        ConfirmRows.SetNum(int32(ELDRootConfirm::LeaveRoom)+1);
        ConfirmCancels.SetNum(int32(ELDRootConfirm::LeaveRoom)+1);
        const bool bFrontend=PC->IsFrontend();
        if(bFrontend){BuildFrontend(*PC,*Canvas);}else{BuildWorld(*Canvas);}
        BuildConfirm(*Canvas,bFrontend);
    }
    return Super::RebuildWidget();
}

void ULDUIRoot::BuildFrontend(ALDPlayerController& PC,UCanvasPanel& Canvas)
{
    const ULDStartPageDraft* Draft=StartDraft();
    // The title screen is this root over its picture; the frontend map itself renders nothing.
    FrontendBackground=AddBackground(Canvas,LDUIBackgrounds::TitleAsset);
    if(FrontendBackground){FrontendBackground->SetRenderTransformPivot(FVector2D(.5f,.5f));}
    // On air: the picture darkened to ink, heavier on the left where the title and the menu stand, then a slow neutral haze
    // along the ground (interface motion only, see TickFrontend).
    const auto Cover=[&Canvas](UWidget* Layer,const FMargin& Offsets)
    {
        UCanvasPanelSlot* LayerSlot=Canvas.AddChildToCanvas(Layer);LayerSlot->SetAnchors(FAnchors(0,0,1,1));LayerSlot->SetOffsets(Offsets);
        LayerSlot->SetZOrder(-1);
    };
    Cover(PaintLayer(WidgetTree,[](const LDUIPaint::FContext& Paint)
    {
        const FBox2D Whole(FVector2D::ZeroVector,FVector2D(Paint.Geometry.GetLocalSize()));
        Paint.Shape(Paint.Layer,LDUIPaint::EShape::Rounded,Whole,0.f,LDUIPaint::FFill::Solid(LDBroadcast::Ink.CopyWithNewOpacity(.42f)));
        Paint.Shape(Paint.Layer+1,LDUIPaint::EShape::Rounded,Whole,0.f,
            LDUIPaint::FFill::Linear(90.f,{{0.f,LDBroadcast::Ink.CopyWithNewOpacity(.82f)},{.45f,LDBroadcast::Ink.CopyWithNewOpacity(.42f)},{1.f,LDBroadcast::Ink.CopyWithNewOpacity(.12f)}}));
    }),FMargin(0));
    FrontendFog=PaintLayer(WidgetTree,[this](const LDUIPaint::FContext& Paint)
    {
        const FVector2D Size(Paint.Geometry.GetLocalSize());
        const float Breath=.75f+.25f*LDTween::PingPong(FPlatformTime::Seconds()-FrontendShownAt,9.);
        Paint.Shape(Paint.Layer,LDUIPaint::EShape::Rounded,FBox2D(FVector2D(0,Size.Y*.6),Size),0.f,
            LDUIPaint::FFill::Linear(180.f,{{0.f,LDBroadcast::Ink.CopyWithNewOpacity(0.f)},{1.f,LDBroadcast::Ink.CopyWithNewOpacity(.55f)}}),Breath);
    });
    Cover(FrontendFog,FMargin(-60,0,-60,0));
    UVerticalBox* Content=nullptr;
    FrontendPage=MakePage(Canvas,560,Content);
    FrontendPanel=Cast<UBorder>(FrontendPage->GetContent());
    FrontendShownAt=FPlatformTime::Seconds();
    auto Add=[](UVerticalBox* Box,UWidget* Child,float Gap){Box->AddChildToVerticalBox(Child)->SetPadding(FMargin(0,Gap));};
    const auto Whole=[](const LDUIPaint::FContext& Paint){return FBox2D(FVector2D::ZeroVector,FVector2D(Paint.Geometry.GetLocalSize()));};
    // The left column (title and menu), anchored to the left edge at the 1080p reference; it takes pointer input only on
    // its buttons.
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Column->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    {
        UCanvasPanelSlot* ColumnSlot=Canvas.AddChildToCanvas(Column);ColumnSlot->SetAnchors(FAnchors(0,.5f));ColumnSlot->SetAlignment(FVector2D(0,.5f));
        ColumnSlot->SetPosition(FVector2D(96.f,-30.f));ColumnSlot->SetAutoSize(true);
    }
    {
        // LIVE · the title's lead, then the winner title in white heavy with its first character on an orange block.
        auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();
        UTextBlock* Live=LDWidgets::Text(WidgetTree,TEXT("LIVE"),Scaled(22),NAME_None,LDUIFonts::ERole::Figure);Live->SetColorAndOpacity(LDBroadcast::White);
        Head->AddChildToHorizontalBox(Painted(WidgetTree,Live,FMargin(18,4,16,4),[Whole](const LDUIPaint::FContext& Paint)
        {LDBroadcastPaint::FillSlab(Paint,Paint.Layer,Whole(Paint),LDBroadcast::LiveRed);}))->SetVerticalAlignment(VAlign_Center);
        FString Lead=LDTerms::Title;Lead.RemoveFromEnd(LDTerms::Winner);
        UTextBlock* LeadText=LDWidgets::Text(WidgetTree,Lead,Scaled(26),NAME_None,LDUIFonts::ERole::Strong);LeadText->SetColorAndOpacity(LDBroadcast::White);
        Head->AddChildToHorizontalBox(LeadText)->SetPadding(FMargin(20,0,0,0));
        if(auto* LeadSlot=Cast<UHorizontalBoxSlot>(LeadText->Slot)){LeadSlot->SetVerticalAlignment(VAlign_Center);}
        Column->AddChildToVerticalBox(Head);
        auto* Title=WidgetTree->ConstructWidget<UHorizontalBox>();
        const FString Winner=LDTerms::Winner;
        const auto Heavy=[this](const FString& Value,const FLinearColor& Ink)
        {
            UTextBlock* Text=LDWidgets::Text(WidgetTree,Value,Scaled(132),NAME_None,LDUIFonts::ERole::Title);
            FSlateFontInfo Font=Text->GetFont();Font.SkewAmount=0.f;Text->SetFont(Font);Text->SetColorAndOpacity(Ink);Text->SetAutoWrapText(false);
            return Text;
        };
        Title->AddChildToHorizontalBox(Painted(WidgetTree,Heavy(Winner.Left(1),LDBroadcast::Ink),FMargin(14,0,22,0),[Whole](const LDUIPaint::FContext& Paint)
        {LDBroadcastPaint::FillSlab(Paint,Paint.Layer,Whole(Paint),LDBroadcast::Orange);}));
        Title->AddChildToHorizontalBox(Heavy(Winner.Mid(1),LDBroadcast::White))->SetPadding(FMargin(8,0,0,0));
        Column->AddChildToVerticalBox(Title)->SetPadding(FMargin(0,10,0,0));
        FrontendLogo=Column;
    }
    // The menu column under the title: 开始 is the one white slab with its orange arrow; the rest are plain rows with an
    // orange tick that turn into the white slab under the pointer.
    MainMenu=WidgetTree->ConstructWidget<UVerticalBox>();
    Column->AddChildToVerticalBox(MainMenu)->SetPadding(FMargin(0,40,0,0));
    const auto Row=[this,Whole](ULDCommandButton* Button,bool bPrimary)
    {
        // The ghost look paints no face of its own: the row's slab below is the whole surface.
        LDWidgets::SetLook(Button,LDUIPaint::EButton::Ghost);
        auto* Text=CastChecked<UTextBlock>(Button->GetContent());
        Text->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Strong,Scaled(bPrimary?30:26)));Text->SetJustification(ETextJustify::Left);
        FButtonStyle Style=Button->GetStyle();
        Style.Normal=FSlateNoResource();Style.Hovered=FSlateNoResource();Style.Pressed=FSlateNoResource();Style.Disabled=FSlateNoResource();
        Style.NormalPadding=Style.PressedPadding=FMargin(0);Button->SetStyle(Style);
        TWeakObjectPtr<ULDCommandButton> Weak(Button);
        auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
        Text->RemoveFromParent();
        UHorizontalBoxSlot* TextSlot=Line->AddChildToHorizontalBox(Text);TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TextSlot->SetVerticalAlignment(VAlign_Center);
        auto* Surface=Painted(WidgetTree,Line,FMargin(bPrimary?28:24,6,24,6),[Weak,bPrimary,Whole](const LDUIPaint::FContext& Paint)
        {
            const bool bLit=bPrimary||(Weak.IsValid()&&Weak->IsHovered());
            if(bPrimary)
            {
                // The primary row: a white slab with the orange arrow at its right end.
                const FBox2D Box=Whole(Paint);
                LDBroadcastPaint::FillSlab(Paint,Paint.Layer,Box,LDBroadcast::White);
                const FVector2D C(Box.Max.X-40.,Box.GetCenter().Y);
                Paint.Polygon(Paint.Layer+1,{C+FVector2D(-6.,-9.),C+FVector2D(8.,0.),C+FVector2D(-6.,9.)},LDUIPaint::FFill::Solid(LDBroadcast::Orange));
                return;
            }
            if(bLit){LDBroadcastPaint::FillSlab(Paint,Paint.Layer,Whole(Paint),LDBroadcast::Raised);}
            LDBroadcastPaint::Block(Paint,Paint.Layer+1,FBox2D(FVector2D(0.,10.),FVector2D(4.,Paint.Geometry.GetLocalSize().Y-10.)),LDBroadcast::Orange);
        });
        auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(420.f);Box->SetHeightOverride(bPrimary?66.f:54.f);Box->SetContent(Surface);
        Button->SetContent(Box);
        // The caption is ink on the white slab and white on the plain rows; only the primary row carries the arrow.
        Text->SetColorAndOpacity(bPrimary?FSlateColor(LDBroadcast::Ink):FSlateColor(LDBroadcast::White));
        return Button;
    };
    const auto Stack=[this](UWidget* Child,float Below)
    {UVerticalBoxSlot* SignSlot=MainMenu->AddChildToVerticalBox(Child);SignSlot->SetHorizontalAlignment(HAlign_Left);SignSlot->SetPadding(FMargin(0,0,0,Below));};
    StartButton=MakeButton(TEXT("开始"),TEXT("Start"),[this](){ShowStartPage(true);});
    Stack(Row(StartButton,true),10);
    if(const auto* Instance=GetGameInstance<ULDGameInstance>();Instance&&Instance->CanReconnect())
    {
        ULDCommandButton* Reconnect=MakeButton(TEXT("重新连接本局"),TEXT("Reconnect"),[this]()
        {
            if(auto* Owner=Cast<ALDPlayerController>(GetOwningPlayer()))
            {if(auto* State=GetGameInstance<ULDGameInstance>()){State->ReconnectRoom(*Owner);}}
        });
        Stack(Row(Reconnect,false),4);
    }
    Stack(Row(MakeButton(LDTerms::HowTo,TEXT("HowTo"),[this](){OpenHowTo();}),false),4);
    Stack(Row(MakeButton(LDTerms::ChainCodex,TEXT("ChainCodex"),[this](){OpenCodex();}),false),4);
    Stack(Row(MakeButton(TEXT("设置"),TEXT("Settings"),[this](){ToggleMainPanel(ELDUIMainPanel::Settings);}),false),4);
    Stack(Row(MakeButton(TEXT("退出"),TEXT("Exit"),[this](){OpenConfirm(ELDRootConfirm::Exit);}),false),0);
    {
        // Spec §6.5: the codex page lists every chain key found on this computer, newest first, with its longest length.
        UVerticalBox* Codex=nullptr;
        CodexPage=MakePage(Canvas,720,Codex);
        CodexPage->SetVisibility(ESlateVisibility::Collapsed);
        Add(Codex,Label(LDTerms::ChainCodex,LDHUDStyle::TypePage,LDHUDStyle::MenuTitle),6);
        CodexCount=Label(FString(),LDHUDStyle::TypeText,LDHUDStyle::MenuMuted);
        Add(Codex,CodexCount,2);
        CodexRows=WidgetTree->ConstructWidget<UVerticalBox>();
        Add(Codex,CodexRows,6);
        CodexBack=MakeButton(TEXT("返回"),TEXT("ChainCodexBack"),[this](){CloseCodex();});
        Add(Codex,CodexBack,8);
    }
    {
        // 怎么玩 (on air): one of six short cards on the right of the screen, clear of the title and the menu column; an ink
        // square with a 2 px white rule, the card's index as a big figure, its title and one sentence. Only the actionable
        // control (下一张) is orange; 上一张 and 关闭 are panel blocks with a white rule.
        UVerticalBox* HowTo=nullptr;
        HowToPage=MakePage(Canvas,620,HowTo);
        HowToPage->SetVisibility(ESlateVisibility::Collapsed);
        if(UCanvasPanelSlot* PageSlot=Cast<UCanvasPanelSlot>(HowToPage->Slot))
        {PageSlot->SetAnchors(FAnchors(1.f,.5f));PageSlot->SetAlignment(FVector2D(1.f,.5f));PageSlot->SetPosition(FVector2D(-96.f,-20.f));}
        if(UBorder* Face=Cast<UBorder>(HowToPage->GetContent()))
        {
            Face->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,0.f,LDBroadcast::White,LDBroadcast::RuleWidth));
            Face->SetBrushColor(LDBroadcast::Ink.CopyWithNewOpacity(.94f));Face->SetPadding(FMargin(32,26));
        }
        auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();
        HowToCount=LDWidgets::Text(WidgetTree,FString(),Scaled(54),NAME_None,LDUIFonts::ERole::Figure);HowToCount->SetColorAndOpacity(LDBroadcast::White);
        HowToCount->SetAutoWrapText(false);
        Head->AddChildToHorizontalBox(HowToCount)->SetVerticalAlignment(VAlign_Bottom);
        UTextBlock* Tag=Label(LDTerms::HowTo,LDHUDStyle::TypeLead,LDBroadcast::Grey);
        auto* TagSlot=Head->AddChildToHorizontalBox(Tag);TagSlot->SetVerticalAlignment(VAlign_Bottom);TagSlot->SetPadding(FMargin(16,0,0,10));
        Add(HowTo,Head,0);
        HowToTitle=Label(FString(),LDHUDStyle::TypePage,LDBroadcast::White);
        HowToTitle->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Strong,Scaled(LDHUDStyle::TypeBanner)));
        Add(HowTo,HowToTitle,10);
        HowToBody=Label(FString(),LDHUDStyle::TypeText,LDBroadcast::Grey);
        HowToBody->SetAutoWrapText(true);
        Add(HowTo,HowToBody,6);
        const auto Flat=[this,Whole](ULDCommandButton* Button,bool bOrange)
        {
            UTextBlock* Caption=Cast<UTextBlock>(Button->GetContent());
            FButtonStyle Style=Button->GetStyle();
            Style.Normal=FSlateColorBrush(FLinearColor::Transparent);Style.Hovered=Style.Normal;Style.Pressed=Style.Normal;Style.Disabled=Style.Normal;
            Style.NormalPadding=Style.PressedPadding=FMargin(0);Button->SetStyle(Style);
            if(!Caption){return Button;}
            Caption->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Strong,Scaled(LDHUDStyle::TypeText)));
            Caption->SetColorAndOpacity(bOrange?LDBroadcast::Ink:LDBroadcast::White);
            Caption->RemoveFromParent();
            TWeakObjectPtr<ULDCommandButton> Weak(Button);
            Button->SetContent(Painted(WidgetTree,Caption,FMargin(26,8,26,8),[Weak,bOrange,Whole](const LDUIPaint::FContext& Paint)
            {
                const FBox2D Box=Whole(Paint);
                const bool bHot=Weak.IsValid()&&Weak->IsHovered();
                if(bOrange){LDBroadcastPaint::FillSlab(Paint,Paint.Layer,Box,bHot?LDBroadcast::Gold:LDBroadcast::Orange);return;}
                LDBroadcastPaint::FillSlab(Paint,Paint.Layer,Box,LDBroadcast::White);
                const FBox2D Inner(Box.Min+FVector2D(LDBroadcast::RuleWidth,LDBroadcast::RuleWidth),Box.Max-FVector2D(LDBroadcast::RuleWidth,LDBroadcast::RuleWidth));
                LDBroadcastPaint::FillSlab(Paint,Paint.Layer+1,Inner,bHot?LDBroadcast::Raised:LDBroadcast::Panel);
            }));
            return Button;
        };
        auto* Pager=WidgetTree->ConstructWidget<UHorizontalBox>();
        Pager->AddChildToHorizontalBox(Flat(MakeButton(TEXT("上一张"),TEXT("HowToPrevious"),[this](){ShowHowTo(HowToIndex-1);}),false));
        Pager->AddChildToHorizontalBox(Flat(MakeButton(TEXT("下一张"),TEXT("HowToNext"),[this](){ShowHowTo(HowToIndex+1);}),true))->SetPadding(FMargin(12,0,0,0));
        Pager->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        HowToBack=Flat(MakeButton(TEXT("关闭"),TEXT("HowToBack"),[this](){CloseHowTo();}),false);
        Pager->AddChildToHorizontalBox(HowToBack);
        Add(HowTo,Pager,18);
    }
    // The first main menu on this computer opens 怎么玩 by itself.
    if(PC.IsFrontend()&&GConfig)
    {
        bool bSeen=false;
        GConfig->GetBool(TEXT("Lockdown.HowTo"),TEXT("bSeen"),bSeen,GGameUserSettingsIni);
        if(!bSeen){OpenHowTo();}
    }

    StartPage=WidgetTree->ConstructWidget<UVerticalBox>();
    Add(Content,StartPage,0);
    Add(StartPage,Label(TEXT("开始"),LDHUDStyle::TypePage,LDHUDStyle::MenuTitle),6);
    Add(StartPage,Label(TEXT("名称"),LDHUDStyle::TypeText,LDHUDStyle::MenuMuted),2);
    NameBox=MakeTextBox(FString::Printf(TEXT("输入你的名称（必填，最多 %d 个字符）"),ULDControlSettings::MaxPlayerNameChars),TEXT("PlayerName"));
    if(PC.Controls){NameBox->SetText(FText::FromString(PC.Controls->PlayerName));}
    Add(StartPage,NameBox,4);
    // Two cards side by side, 我开房 and 我加入, each with its one primary button; while its attempt runs a ring turns on the card,
    // and its result or failure stays on the same card with the typed values.
    const auto Card=[this](const TCHAR* Title,UWidget* Help,UVerticalBox*& Body)
    {
        Body=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();
        UTextBlock* Heading=Label(Title,LDHUDStyle::TypeBanner,LDHUDStyle::UIText);Heading->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Title,Heading->GetFont().Size));
        Head->AddChildToHorizontalBox(Heading)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        if(Help){Head->AddChildToHorizontalBox(Help)->SetVerticalAlignment(VAlign_Center);}
        Body->AddChildToVerticalBox(Head)->SetPadding(FMargin(0,0,0,10));
        return Painted(WidgetTree,Body,FMargin(24,20),[](const LDUIPaint::FContext& Paint)
        {Paint.Panel(Paint.Layer,FBox2D(FVector2D::ZeroVector,FVector2D(Paint.Geometry.GetLocalSize())));});
    };
    const auto CardStatus=[this](UVerticalBox* Body,TObjectPtr<UWidget>& Ring,TObjectPtr<UTextBlock>& Notice)
    {
        auto* Ringed=WidgetTree->ConstructWidget<USizeBox>();Ringed->SetWidthOverride(26);Ringed->SetHeightOverride(26);
        Ringed->SetContent(PaintLayer(WidgetTree,[](const LDUIPaint::FContext& Paint)
        {
            const FVector2D Size(Paint.Geometry.GetLocalSize());const float Radius=float(FMath::Min(Size.X,Size.Y))*.5f-2.f;
            Paint.Arc(Paint.Layer,Size*.5,Radius,1.f,FLinearColor(1,1,1,.14f),3.f);
            Paint.Arc(Paint.Layer+1,Size*.5,Radius,.3f,LDHUDStyle::UIAction,3.f,float(FMath::Fmod(FPlatformTime::Seconds()*1.2,1.)));
        }));
        Ringed->SetVisibility(ESlateVisibility::Collapsed);Ring=Ringed;
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
        UHorizontalBoxSlot* RingSlot=Row->AddChildToHorizontalBox(Ringed);RingSlot->SetVerticalAlignment(VAlign_Center);RingSlot->SetPadding(FMargin(0,0,10,0));
        Notice=Label(FString(),LDHUDStyle::TypeText);
        UHorizontalBoxSlot* NoticeSlot=Row->AddChildToHorizontalBox(Notice);NoticeSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));NoticeSlot->SetVerticalAlignment(VAlign_Center);
        Body->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,10,0,0));
    };
    const auto Push=[this](UVerticalBox* Body){Body->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));};
    auto* Cards=WidgetTree->ConstructWidget<UHorizontalBox>();
    Add(StartPage,Cards,8);
    int32 ListenPort=0;
    bListenPortValid=ConfiguredListenPort(ListenPort);
    {
        // The firewall and port-forwarding note sits behind a "?" on the host card.
        auto* Help=WidgetTree->ConstructWidget<USizeBox>();Help->SetWidthOverride(28);Help->SetHeightOverride(28);
        UTextBlock* Mark=Label(TEXT("?"),LDHUDStyle::TypeLead,LDHUDStyle::UIAction);Mark->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Strong,Mark->GetFont().Size));
        Mark->SetJustification(ETextJustify::Center);
        Help->SetContent(Painted(WidgetTree,Mark,FMargin(0,1,0,0),[](const LDUIPaint::FContext& Paint)
        {
            const FBox2D Disc(FVector2D::ZeroVector,FVector2D(Paint.Geometry.GetLocalSize()));
            Paint.Shape(Paint.Layer,LDUIPaint::EShape::Circle,Disc,0.f,LDUIPaint::FFill::Solid(FLinearColor(0,0,0,.35f)));
            Paint.Edge(Paint.Layer+1,LDUIPaint::EShape::Circle,Disc,0.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.8f),1.5f);
        }));
        Help->SetToolTipText(FText::FromString(TEXT("其他玩家用你的 IP 地址和这个端口加入；跨网络需自行开放防火墙并设置端口映射。")));
        Help->SetVisibility(ESlateVisibility::Visible);
        UVerticalBox* Body=nullptr;
        Cards->AddChildToHorizontalBox(Card(TEXT("我开房"),Help,Body))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Add(Body,Label(bListenPortValid?FString::Printf(TEXT("本机监听端口：%d"),ListenPort)
            :FString(TEXT("本机监听端口未配置（[URL] Port 无效），无法创建房间。")),LDHUDStyle::TypeLead),2);
        Push(Body);
        HostButton=MakeButton(TEXT("创建房间"),TEXT("Host"),[this](){Host();});
        LDWidgets::SetLook(HostButton,LDUIPaint::EButton::Primary);
        Add(Body,HostButton,6);
        CardStatus(Body,HostRing,HostFeedback);
    }
    {
        UVerticalBox* Body=nullptr;
        UHorizontalBoxSlot* JoinSlot=Cards->AddChildToHorizontalBox(Card(TEXT("我加入"),nullptr,Body));
        JoinSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));JoinSlot->SetPadding(FMargin(16,0,0,0));
        Add(Body,Label(TEXT("房主 IP 与端口"),LDHUDStyle::TypeText,LDHUDStyle::MenuMuted),2);
        auto* EndpointRow=WidgetTree->ConstructWidget<UHorizontalBox>();
        Add(Body,EndpointRow,4);
        AddressBox=MakeTextBox(TEXT("房主 IP / IP:端口（可直接粘贴）"),TEXT("JoinAddress"));
        LastAddressInput=Draft?Draft->Address:FString();
        AddressBox->SetText(FText::FromString(LastAddressInput));
        AddressBox->OnTextChanged.AddDynamic(this,&ULDUIRoot::AddressTextChanged);
        AddressBox->OnTextCommitted.AddDynamic(this,&ULDUIRoot::AddressTextCommitted);
        EndpointRow->AddChildToHorizontalBox(AddressBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        PortBox=MakeTextBox(TEXT("端口"),TEXT("JoinPort"));
        PortBox->SetText(FText::FromString(Draft&&!Draft->Port.IsEmpty()?Draft->Port:bListenPortValid?FString::FromInt(ListenPort):FString()));
        auto* PortWidth=WidgetTree->ConstructWidget<USizeBox>();
        PortWidth->SetWidthOverride(130);
        PortWidth->SetContent(PortBox);
        EndpointRow->AddChildToHorizontalBox(PortWidth)->SetPadding(FMargin(8,0,0,0));
        Push(Body);
        auto* JoinRow=WidgetTree->ConstructWidget<UHorizontalBox>();
        Add(Body,JoinRow,6);
        JoinButton=MakeButton(TEXT("加入房间"),TEXT("Join"),[this](){Join();});
        LDWidgets::SetLook(JoinButton,LDUIPaint::EButton::Primary);
        JoinRow->AddChildToHorizontalBox(JoinButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        CancelJoinButton=MakeButton(TEXT("取消连接"),TEXT("CancelJoin"),[this](){CancelJoin();});
        UHorizontalBoxSlot* CancelSlot=JoinRow->AddChildToHorizontalBox(CancelJoinButton);
        CancelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        CancelSlot->SetPadding(FMargin(8,0,0,0));
        CardStatus(Body,JoinRing,JoinFeedback);
    }

    Feedback=Label(FString(),LDHUDStyle::TypeLabel);
    Add(Content,Feedback,6);
    SettingsStatus=Label(FString(),LDHUDStyle::TypeBody,LDHUDStyle::MenuWarning);
    Add(Content,SettingsStatus,4);
    StartBackButton=MakeButton(TEXT("返回"),TEXT("StartBack"),[this](){ShowStartPage(false);});
    Add(Content,StartBackButton,6);
    FString Version,VersionError;
    UTextBlock* VersionLine=LDWidgets::Text(WidgetTree,LDDemoEntry::ReadProductVersion(Version,VersionError)?LDDemoEntry::TitleVersion(Version):TEXT("版本读取失败：")+VersionError,
        Scaled(14),NAME_None,LDUIFonts::ERole::Figure);
    VersionLine->SetColorAndOpacity(LDBroadcast::Grey);VersionLine->SetAutoWrapText(false);
    // The slang note stays on the front page (#111) so the title's slang is never read as anything else: the bottom ticker
    // of the broadcast, its "黑话" tag on an orange block, the version at its right end.
    UTextBlock* SlangNote=Label(LDTerms::SlangNote,15,LDBroadcast::White);SlangNote->SetAutoWrapText(false);
    {
        auto* Ticker=WidgetTree->ConstructWidget<UHorizontalBox>();
        UTextBlock* Tag=LDWidgets::Text(WidgetTree,TEXT("黑话"),Scaled(18),NAME_None,LDUIFonts::ERole::Strong);Tag->SetColorAndOpacity(LDBroadcast::Ink);
        Ticker->AddChildToHorizontalBox(Painted(WidgetTree,Tag,FMargin(40,10,40,10),[Whole](const LDUIPaint::FContext& Paint)
        {LDBroadcastPaint::FillSlab(Paint,Paint.Layer,Whole(Paint),LDBroadcast::Orange,true,false);}));
        auto* NoteSlot=Ticker->AddChildToHorizontalBox(SlangNote);NoteSlot->SetVerticalAlignment(VAlign_Center);NoteSlot->SetPadding(FMargin(24,0,0,0));
        NoteSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* VersionSlot=Ticker->AddChildToHorizontalBox(VersionLine);VersionSlot->SetVerticalAlignment(VAlign_Center);VersionSlot->SetPadding(FMargin(0,0,36,0));
        UOverlay* Band=Painted(WidgetTree,Ticker,FMargin(0),[Whole](const LDUIPaint::FContext& Paint)
        {LDBroadcastPaint::Block(Paint,Paint.Layer,Whole(Paint),LDBroadcast::Ink.CopyWithNewOpacity(.92f));});
        Band->SetVisibility(ESlateVisibility::HitTestInvisible);
        UCanvasPanelSlot* BandSlot=Canvas.AddChildToCanvas(Band);BandSlot->SetAnchors(FAnchors(0,1,1,1));BandSlot->SetAlignment(FVector2D(0,1));
        BandSlot->SetOffsets(FMargin(0,0,0,0));BandSlot->SetAutoSize(true);
        FrontendCorners.Add(Band);
    }
    bMenuOpen=true;
    SetFrontendPage(Draft&&Draft->bStartPage);
}

void ULDUIRoot::BuildWorld(UCanvasPanel& Canvas)
{
    if(ULDStartPageDraft* Draft=StartDraft())
    {
        // Reaching a room world ends the frontend attempt; a later return starts at the main menu.
        Draft->Attempt=ELDFrontendAttempt::None;
        Draft->Endpoint.Reset();
        Draft->Notice.Reset();
        Draft->bStartPage=false;
        if(const auto* Instance=GetGameInstance<ULDGameInstance>()){Draft->SeenConnection=Instance->ConnectionMessage;}
    }
    auto Add=[](UVerticalBox* Box,UWidget* Child,float Gap){Box->AddChildToVerticalBox(Child)->SetPadding(FMargin(0,Gap));};
    // Overhead unit names: under every root page and the HUD's higher viewport layer; never hit-tested or focused.
    WorldLabels=CreateWidget<ULDWorldLabels>(GetOwningPlayer());
    if(WorldLabels)
    {
        UCanvasPanelSlot* LabelSlot=Canvas.AddChildToCanvas(WorldLabels);
        LabelSlot->SetAnchors(FAnchors(0,0,1,1));
        LabelSlot->SetOffsets(FMargin(0));
        LabelSlot->SetZOrder(-1);
        WorldLabels->SetVisibility(ESlateVisibility::Collapsed);
    }
    else{UE_LOG(LogLDUI,Error,TEXT("LD_WORLD_LABELS_CREATION_FAILED Overhead unit names are unavailable in this world."));}
    // Floating feedback over the bodies: above the names in the same bottom layer, never hit-tested or focused.
    WorldFeedback=CreateWidget<ULDWorldFeedback>(GetOwningPlayer());
    if(WorldFeedback)
    {
        UCanvasPanelSlot* FeedbackSlot=Canvas.AddChildToCanvas(WorldFeedback);
        FeedbackSlot->SetAnchors(FAnchors(0,0,1,1));
        FeedbackSlot->SetOffsets(FMargin(0));
        FeedbackSlot->SetZOrder(-1);
        WorldFeedback->SetVisibility(ESlateVisibility::Collapsed);
    }
    else{UE_LOG(LogLDUI,Error,TEXT("LD_WORLD_FEEDBACK_CREATION_FAILED Floating numbers, rewards and level-up and kill confirmations are unavailable in this world."));}
    Status=Label(FString(),LDHUDStyle::TypeLead);
    HeaderPanel=LDWidgets::Panel(WidgetTree,Status,FMargin(20,12));
    UCanvasPanelSlot* HeaderSlot=Canvas.AddChildToCanvas(HeaderPanel);
    HeaderSlot->SetAnchors(FAnchors(0,0,1,0));
    HeaderSlot->SetOffsets(FMargin(18,18,18,84));
    HeaderPanel->SetVisibility(ESlateVisibility::Collapsed);

    UVerticalBox* Lobby=nullptr;
    LobbyBackground=AddBackground(Canvas,LDUIBackgrounds::LobbyAsset);
    if(LobbyBackground){LobbyBackground->SetVisibility(ESlateVisibility::Collapsed);}
    LobbyPage=MakePage(Canvas,860,Lobby);
    LobbyPage->SetVisibility(ESlateVisibility::Collapsed);
    Add(Lobby,Label(TEXT("房间"),LDHUDStyle::TypePage,LDHUDStyle::MenuTitle),6);
    Add(Lobby,Label(TEXT("房主免准备；所有客机准备后，由房主直接开始对局。"),LDHUDStyle::TypeText,LDHUDStyle::MenuMuted),2);
    RoomAddressRow=WidgetTree->ConstructWidget<UHorizontalBox>();
    Add(Lobby,RoomAddressRow,4);
    RoomAddressLabel=Label(FString(),LDHUDStyle::TypeBody,LDHUDStyle::MenuSection);
    RoomAddressLabel->SetAutoWrapText(false);
    UHorizontalBoxSlot* AddrTextSlot=RoomAddressRow->AddChildToHorizontalBox(RoomAddressLabel);
    AddrTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    AddrTextSlot->SetVerticalAlignment(VAlign_Center);
    CopyAddressButton=MakeButton(LDTerms::CopyRoomAddress,TEXT("CopyRoomAddress"),[this](){CopyRoomAddress();},LDHUDStyle::TypeBody);
    RoomAddressRow->AddChildToHorizontalBox(CopyAddressButton)->SetPadding(FMargin(8,0,0,0));
    RoomAddressRow->SetVisibility(ESlateVisibility::Collapsed);
    RosterRows=WidgetTree->ConstructWidget<UVerticalBox>();
    Add(Lobby,RosterRows,8);
    AIButtonRow=WidgetTree->ConstructWidget<UHorizontalBox>();
    Add(Lobby,AIButtonRow,6);
    auto* AICaption=Label(TEXT("AI 对手"),LDHUDStyle::TypeLead,LDHUDStyle::MenuSection);AICaption->SetAutoWrapText(false);
    AIButtonRow->AddChildToHorizontalBox(AICaption)->SetVerticalAlignment(VAlign_Center);
    for(int32 Count=0;Count<=LDMatchLifecycle::MaxOpponents;++Count)
    {
        ULDCommandButton* Choice=MakeButton(FString::FromInt(Count),FName(*FString::Printf(TEXT("AICount%d"),Count)),
            [this,Count](){if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer())){PC->SetDemoAICount(uint8(Count));}});
        UHorizontalBoxSlot* ChoiceSlot=AIButtonRow->AddChildToHorizontalBox(Choice);
        ChoiceSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ChoiceSlot->SetPadding(FMargin(8,0,0,0));
        AIButtons.Add(Choice);
    }
    AIButtonRow->SetVisibility(ESlateVisibility::Collapsed);
    AIReadOnly=Label(FString(),LDHUDStyle::TypeLead);
    Add(Lobby,AIReadOnly,6);
    LobbyDevMarker=Label(FString(),LDHUDStyle::TypeText,LDHUDStyle::MenuWarning);
    Add(Lobby,LobbyDevMarker,4);
    auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();
    Add(Lobby,Actions,8);
    ReadyButton=MakeButton(TEXT("准备"),TEXT("Ready"),[this](){Ready();});
    Actions->AddChildToHorizontalBox(ReadyButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    StartMatchButton=MakeButton(TEXT("开始对局"),TEXT("StartMatch"),[this](){StartMatch();});
    // Once everyone is ready the host's 开始对局 turns primary and breathes a yellow halo (RefreshLobby).
    StartMatchGlow=PaintLayer(WidgetTree,[](const LDUIPaint::FContext& Paint)
    {
        const float Pulse=.25f+.35f*LDTween::PingPong(FPlatformTime::Seconds(),1.6);
        Paint.Halo(Paint.Layer,LDUIPaint::EShape::Rounded,FBox2D(FVector2D::ZeroVector,FVector2D(Paint.Geometry.GetLocalSize())),
            LDHUDStyle::RadiusButton,LDHUDStyle::UIAction.CopyWithNewOpacity(Pulse),14.f);
    });
    StartMatchGlow->SetVisibility(ESlateVisibility::Collapsed);
    auto* StartWrap=WidgetTree->ConstructWidget<UOverlay>();
    for(UWidget* Layer:{StartMatchGlow.Get(),static_cast<UWidget*>(StartMatchButton.Get())})
    {UOverlaySlot* LayerSlot=StartWrap->AddChildToOverlay(Layer);LayerSlot->SetHorizontalAlignment(HAlign_Fill);LayerSlot->SetVerticalAlignment(VAlign_Fill);}
    UHorizontalBoxSlot* StartSlot=Actions->AddChildToHorizontalBox(StartWrap);
    StartSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    StartSlot->SetPadding(FMargin(8,0,0,0));
    StartMatchButton->SetVisibility(ESlateVisibility::Collapsed);
    LobbyBlocker=Label(FString(),LDHUDStyle::TypeText,LDHUDStyle::MenuWarning);
    Add(Lobby,LobbyBlocker,4);
    LobbyFeedback=Label(FString(),LDHUDStyle::TypeText);
    Add(Lobby,LobbyFeedback,4);
    Add(Lobby,Label(TEXT("最多 8 名参赛者（含房主），其中 AI 最多 7 个；空位不会自动补满。真人进出会清除准备，调整 AI 数量保留真人准备。"),LDHUDStyle::TypeBody,LDHUDStyle::MenuMuted),6);
    // #198 §10: the own last build blueprint and the read-only import view, outside any match.
    Add(Lobby,MakeButton(LDTerms::BuildBlueprint,TEXT("LobbyBuildBlueprint"),[this]()
    {
        if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer())){if(PC->BuildBlueprints){PC->BuildBlueprints->ToggleViewer();}}
    }),8);
    Add(Lobby,MakeButton(TEXT("设置"),TEXT("LobbySettings"),[this](){ToggleMainPanel(ELDUIMainPanel::Settings);}),8);
    Add(Lobby,MakeButton(TEXT("离开房间并返回主菜单"),TEXT("LeaveRoom"),[this](){OpenConfirm(ELDRootConfirm::LeaveRoom);}),8);

    UVerticalBox* Menu=nullptr;
    MenuPage=MakePage(Canvas,520,Menu);
    MenuPage->SetVisibility(ESlateVisibility::Collapsed);
    Add(Menu,Label(TEXT("菜单"),LDHUDStyle::TypePage,LDHUDStyle::MenuTitle),6);
    MenuPause=Label(TEXT("对局不会暂停"),LDHUDStyle::TypeLabel);
    Add(Menu,MenuPause,2);
    MenuPhase=Label(FString(),LDHUDStyle::TypeText,LDHUDStyle::MenuMuted);
    Add(Menu,MenuPhase,2);
    Add(Menu,MakeButton(TEXT("继续"),TEXT("Resume"),[this](){Resume();}),8);
    Add(Menu,MakeButton(TEXT("设置"),TEXT("MatchSettings"),[this](){ToggleMainPanel(ELDUIMainPanel::Settings);}),8);
    Add(Menu,MakeButton(TEXT("离开房间并返回主菜单"),TEXT("LeaveMatch"),[this](){OpenConfirm(ELDRootConfirm::LeaveRoom);}),8);
    MenuFeedback=Label(FString(),LDHUDStyle::TypeBody,LDHUDStyle::MenuMuted);
    Add(Menu,MenuFeedback,4);
}

void ULDUIRoot::BuildConfirm(UCanvasPanel& Canvas,bool bFrontend)
{
    auto* Dialog=WidgetTree->ConstructWidget<UVerticalBox>();
    ConfirmText=Label(FString(),LDHUDStyle::TypeHeading);
    Dialog->AddChildToVerticalBox(ConfirmText)->SetPadding(FMargin(0,0,0,16));
    auto AddChoice=[this,Dialog](ELDRootConfirm Kind,const TCHAR* Accept,FName AcceptName,TFunction<void()> Action,FName CancelName)
    {
        auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
        Line->AddChildToHorizontalBox(MakeButton(Accept,AcceptName,MoveTemp(Action)))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ULDCommandButton* Cancel=MakeButton(TEXT("取消"),CancelName,[this](){CloseConfirm();});
        UHorizontalBoxSlot* CancelSlot=Line->AddChildToHorizontalBox(Cancel);
        CancelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        CancelSlot->SetPadding(FMargin(12,0,0,0));
        Dialog->AddChildToVerticalBox(Line);
        ConfirmRows[int32(Kind)]=Line;
        ConfirmCancels[int32(Kind)]=Cancel;
    };
    if(bFrontend){AddChoice(ELDRootConfirm::Exit,TEXT("退出"),TEXT("ConfirmExit"),[this](){Exit();},TEXT("CancelExit"));}
    else
    {
        AddChoice(ELDRootConfirm::LeaveRoom,TEXT("确认离开"),TEXT("ConfirmLeave"),[this](){LeaveRoom();},TEXT("CancelLeave"));
    }
    auto* DialogWidth=WidgetTree->ConstructWidget<USizeBox>();
    DialogWidth->SetWidthOverride(560);
    DialogWidth->SetContent(LDWidgets::Panel(WidgetTree,Dialog,FMargin(28,24)));
    // Full-viewport modal: it disables the page beneath it and PointerOverUI reports every world click as UI.
    ConfirmLayer=WidgetTree->ConstructWidget<UBorder>();
    ConfirmLayer->SetBrushColor(LDHUDStyle::ModalScrim);
    ConfirmLayer->SetHorizontalAlignment(HAlign_Center);
    ConfirmLayer->SetVerticalAlignment(VAlign_Center);
    ConfirmLayer->SetContent(DialogWidth);
    UCanvasPanelSlot* LayerSlot=Canvas.AddChildToCanvas(ConfirmLayer);
    LayerSlot->SetAnchors(FAnchors(0,0,1,1));
    LayerSlot->SetOffsets(FMargin(0));
    LayerSlot->SetZOrder(10);
    ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
}

int32 ULDUIRoot::Scaled(int32 Size) const{return FMath::Max(1,FMath::RoundToInt(Size*AppliedTextScale));}

UTextBlock* ULDUIRoot::Label(const FString& Value,int32 Size,const FLinearColor& Color)
{
    UTextBlock* Text=LDWidgets::Text(WidgetTree,Value,Scaled(Size));
    Text->SetColorAndOpacity(FSlateColor(Color));
    return Text;
}

ULDCommandButton* ULDUIRoot::MakeButton(const FString& Text,FName Name,TFunction<void()> Action,int32 Size)
{
    ULDCommandButton* Button=LDWidgets::Button(WidgetTree,Text,MoveTemp(Action),false,Name);
    auto* Caption=CastChecked<UTextBlock>(Button->GetContent());
    Caption->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Body,Scaled(Size)));
    Caption->SetJustification(ETextJustify::Center);
    CastChecked<UButtonSlot>(Caption->Slot)->SetHorizontalAlignment(HAlign_Fill);
    // 12 px vertical padding keeps every root button at least 44 px tall (VISION_AND_UI readability).
    FButtonStyle Style=Button->GetStyle();
    Style.NormalPadding=Style.PressedPadding=FMargin(16,12);
    Button->SetStyle(Style);
    return Button;
}

UEditableTextBox* ULDUIRoot::MakeTextBox(const FString& Hint,FName Name)
{
    auto* Box=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),Name);
    Box->SetHintText(FText::FromString(Hint));
    FEditableTextBoxStyle Style=Box->GetWidgetStyle();
    Style.TextStyle.SetFont(LDUIFonts::Font(LDUIFonts::ERole::Body,Scaled(LDHUDStyle::TypeLead)));
    Style.Padding=FMargin(12,11);
    Box->SetWidgetStyle(Style);
    Box->SetForegroundColor(LDHUDStyle::InputText);
    // Enter ends editing but keeps keyboard focus inside the root, so Esc and Tab keep reaching it.
    Box->SetClearKeyboardFocusOnCommit(false);
    Box->OnTextCommitted.AddDynamic(this,&ULDUIRoot::TextCommitted);
    return Box;
}

UWidget* ULDUIRoot::AddBackground(UCanvasPanel& Canvas,const TCHAR* AssetPath)
{
    UTexture2D* Texture=LoadObject<UTexture2D>(nullptr,AssetPath);
    if(!Texture)
    {
        UE_LOG(LogLDUI,Error,TEXT("LD_UI_BACKGROUND_MISSING %s is not imported; run ImportDemoArt.ps1 -Mode ImportUI."),AssetPath);
        return nullptr;
    }
    auto* Picture=WidgetTree->ConstructWidget<UImage>();
    Picture->SetBrushFromTexture(Texture,true);
    // Scale to fill: the picture covers any aspect ratio, cropping the overflow around its centre instead of stretching.
    auto* Fill=WidgetTree->ConstructWidget<UScaleBox>();
    Fill->SetStretch(EStretch::ScaleToFill);
    Fill->SetContent(Picture);
    Fill->SetVisibility(ESlateVisibility::HitTestInvisible);
    UCanvasPanelSlot* FillSlot=Canvas.AddChildToCanvas(Fill);
    FillSlot->SetAnchors(FAnchors(0,0,1,1));
    FillSlot->SetOffsets(FMargin(0));
    // Under the overhead labels and feedback (-1) and every page.
    FillSlot->SetZOrder(-2);
    return Fill;
}

USizeBox* ULDUIRoot::MakePage(UCanvasPanel& Canvas,float Width,UVerticalBox*& Content)
{
    Content=WidgetTree->ConstructWidget<UVerticalBox>();
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();
    Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll);
    Scroll->AddChild(Content);
    auto* Page=WidgetTree->ConstructWidget<USizeBox>();
    Page->SetWidthOverride(Width);
    Page->SetContent(LDWidgets::Panel(WidgetTree,Scroll,FMargin(28,24)));
    // Auto height, capped to the viewport in NativeTick: long content scrolls instead of clipping at 1280x720.
    UCanvasPanelSlot* PageSlot=Canvas.AddChildToCanvas(Page);
    PageSlot->SetAnchors(FAnchors(.5f,.5f));
    PageSlot->SetAlignment(FVector2D(.5f,.5f));
    PageSlot->SetAutoSize(true);
    Pages.Add(Page);
    return Page;
}

void ULDUIRoot::ApplyTextScale(float Scale)
{
    if(Scale<=0||FMath::IsNearlyEqual(Scale,AppliedTextScale)){return;}
    const float Ratio=Scale/AppliedTextScale;
    WidgetTree->ForEachWidget([Ratio](UWidget* Widget)
    {
        if(auto* Text=Cast<UTextBlock>(Widget))
        {
            FSlateFontInfo Font=Text->GetFont();
            Font.Size=FMath::RoundToFloat(Font.Size*Ratio);
            Text->SetFont(Font);
        }
        else if(auto* Box=Cast<UEditableTextBox>(Widget))
        {
            FEditableTextBoxStyle Style=Box->GetWidgetStyle();
            Style.TextStyle.Font.Size=FMath::RoundToFloat(Style.TextStyle.Font.Size*Ratio);
            Box->SetWidgetStyle(Style);
        }
    });
    AppliedTextScale=Scale;
}

ULDStartPageDraft* ULDUIRoot::StartDraft() const
{
    const UGameInstance* Instance=GetGameInstance();
    return Instance?Instance->GetSubsystem<ULDStartPageDraft>():nullptr;
}

FString ULDUIRoot::LocalSettingsProblem(const ALDPlayerController& PC) const
{
    const auto* Instance=GetGameInstance<ULDGameInstance>();
    if(!Instance){return FString();}
    const FLDDisplaySettingsResult& Display=Instance->GetDisplaySettingsResult();
    if(Display.Status==ELDDisplaySettingsStatus::PersistenceFailed
        ||Display.Status==ELDDisplaySettingsStatus::ApplyFailed)
    {return TEXT("显示设置：")+Display.Message;}
    if(!PC.Controls||PC.Controls->NeedsRecovery()){return TEXT("本地设置：")+Instance->GetLocalSettingsMessage();}
    return FString();
}

void ULDUIRoot::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC||!ConfirmLayer){return;}
    ApplyTextScale(PC->Controls?PC->Controls->TextScale:1.f);
    const float Height=FMath::Max(240.f,float(Geometry.GetLocalSize().Y)-48.f);
    if(!FMath::IsNearlyEqual(Height,AppliedPageHeight))
    {
        AppliedPageHeight=Height;
        for(USizeBox* Page:Pages){Page->SetMaxDesiredHeight(Height);}
    }
    if(PC->IsFrontend()){TickFrontend(*PC);}else{TickWorld(*PC);}
}

void ULDUIRoot::TickFrontend(ALDPlayerController& PC)
{
    // The title picture pushes in slowly and back, the low fog drifts and the logo's neon buzzes now and then (interface
    // motion only; nothing renders behind it).
    const double Age=FPlatformTime::Seconds()-FrontendShownAt;
    if(FrontendBackground){LDTween::SetScale(FrontendBackground,1.f+.04f*LDTween::PingPong(Age,40.));}
    if(FrontendFog){LDTween::SetTranslation(FrontendFog,FVector2D(40.*FMath::Sin(2.*PI*Age/26.),0.));}
    const double Buzz=FMath::Fmod(Age,7.3);
    const float Neon=Buzz>6.9&&Buzz<6.98?.35f:Buzz>7.06&&Buzz<7.13?.6f:1.f;
    for(UWidget* Glow:FrontendNeon){if(Glow->GetRenderOpacity()!=Neon){Glow->SetRenderOpacity(Neon);}}
    // Settings is the frontend's main panel: the page yields to it and comes back unchanged when it closes.
    const bool bSettings=PanelOpen(PC.ControlsUI.Get());
    if(bSettings&&!bFrontendHidden){DismissMenu();}
    else if(!bSettings&&bFrontendHidden){RestoreFrontendPage(PC);}
    ULDStartPageDraft* Draft=StartDraft();
    const auto* Instance=GetGameInstance<ULDGameInstance>();
    if(Draft&&Instance)
    {
        if(!Instance->ConnectionMessage.Equals(Draft->SeenConnection,ESearchCase::CaseSensitive))
        {
            Draft->SeenConnection=Instance->ConnectionMessage;
            if(!Draft->SeenConnection.IsEmpty())
            {
                // The session domain's text is shown as-is; only the context of the local attempt is added.
                const TCHAR* Prefix=Draft->Attempt==ELDFrontendAttempt::Join?TEXT("连接失败：")
                    :Draft->Attempt==ELDFrontendAttempt::Host?TEXT("无法创建房间："):TEXT("已返回主菜单：");
                Draft->Notice=Prefix+Draft->SeenConnection;
                Draft->NoticeFor=Draft->Attempt;
                Draft->Attempt=ELDFrontendAttempt::None;
                Draft->Endpoint.Reset();
            }
        }
        else if(Draft->Attempt!=ELDFrontendAttempt::None&&!TravelInFlight())
        {
            Draft->Notice=Draft->Attempt==ELDFrontendAttempt::Join?TEXT("连接已结束，但没有收到结果或失败原因，请重试。")
                :TEXT("没有进入房间，也没有收到失败原因，请重试。");
            Draft->NoticeFor=Draft->Attempt;
            Draft->Attempt=ELDFrontendAttempt::None;
            Draft->Endpoint.Reset();
        }
        if(Draft->Attempt!=AppliedAttempt){UpdateStartControls();}
    }
    // A host or join notice stays on its own card, under the typed values; others show under the page.
    const FString Notice=Draft?Draft->Notice:FString();
    const ELDFrontendAttempt NoticeCard=Draft&&bStartPage?Draft->NoticeFor:ELDFrontendAttempt::None;
    SetLine(Feedback,NoticeCard==ELDFrontendAttempt::None?Notice:FString());
    SetLine(HostFeedback,NoticeCard==ELDFrontendAttempt::Host?Notice:FString());
    SetLine(JoinFeedback,NoticeCard==ELDFrontendAttempt::Join?Notice:FString());
    const FSlateColor CardInk(Draft&&Draft->Attempt!=ELDFrontendAttempt::None?LDHUDStyle::MenuMuted:FLinearColor(.9f,.94f,.96f));
    for(UTextBlock* CardLine:{HostFeedback.Get(),JoinFeedback.Get()}){if(CardLine->GetColorAndOpacity()!=CardInk){CardLine->SetColorAndOpacity(CardInk);}}
    SetLine(SettingsStatus,LocalSettingsProblem(PC));
}

void ULDUIRoot::TickWorld(ALDPlayerController& PC)
{
    const ALDGameState* Public=GetWorld()->GetGameState<ALDGameState>();
    const FLDPublicMatchView* Match=CurrentMatch(Public);
    const bool bLobbyNow=Match&&Match->Phase==ELDMatchPhase::Lobby;
    const bool bResultsNow=Match&&Match->Phase==ELDMatchPhase::Results;
    const bool bPlaying=Match&&(Match->Phase==ELDMatchPhase::Rest||Match->Phase==ELDMatchPhase::Development
        ||Match->Phase==ELDMatchPhase::CaptureOpen);
    const bool bSettings=PanelOpen(PC.ControlsUI.Get());
    if(bLobbyNow!=bInLobby)
    {
        // A confirmation belongs to the screen that opened it; the lobby and the match never share one.
        CloseConfirm();
        bInLobby=bLobbyNow;
        bRosterBuilt=false;
        if(bInLobby&&bMenuOpen){bMenuOpen=false;MenuPage->SetVisibility(ESlateVisibility::Collapsed);}
        LobbyPage->SetVisibility(bInLobby?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        if(LobbyBackground){LobbyBackground->SetVisibility(bInLobby?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
        if(!bSettings){PC.SetMenuOpen(bInLobby);}
    }
    if(bResultsNow!=bInResults)
    {
        bInResults=bResultsNow;
        // The HUD result screen owns the centre; Back may reopen the menu.
        if(bInResults&&bMenuOpen){CloseMenu();}
    }
    // Closing Settings hands keyboard focus back to the root's own full page, as it was before Settings opened.
    LobbyPage->SetVisibility(bInLobby&&!bSettings?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(bSettingsWasOpen&&!bSettings&&(bInLobby||bMenuOpen)){PC.SetMenuOpen(true);}
    bSettingsWasOpen=bSettings;
    if(WorldLabels||WorldFeedback)
    {
        // Same condition that shows the in-match HUD (ULDHUD::RefreshReadModel): this owner's running match, no menu or
        // Settings over it. Both world layers themselves draw only the owner's current authorized frame and own facts.
        const bool bLabels=PC.HUDUI&&bPlaying&&PC.OwnerState.Match==Match->Generation&&!bMenuOpen&&!bSettings;
        const ESlateVisibility Labels=bLabels?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed;
        if(WorldLabels&&WorldLabels->GetVisibility()!=Labels){WorldLabels->SetVisibility(Labels);}
        if(WorldFeedback&&WorldFeedback->GetVisibility()!=Labels){WorldFeedback->SetVisibility(Labels);}
    }
    const bool bHeader=!bInLobby&&!bInResults&&!PC.Navigation->Enabled();
    HeaderPanel->SetVisibility(bHeader?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(bHeader){SetLine(Status,TEXT("对局阶段：")+MatchPhaseText(Match));}
    if(!bInLobby){CachedRoomEndpoint.Reset();CachedRoomGeneration=0;CachedRoomPort=-1;}
    if(bInLobby){RefreshLobby(PC,*Public);}
    if(!bMenuOpen){return;}
    SetLine(MenuPause,bPlaying?FString(TEXT("对局不会暂停")):FString());
    SetLine(MenuPhase,TEXT("对局阶段：")+MatchPhaseText(Match));
    FString Notes=PC.IntentFeedback;
    const FString Problem=LocalSettingsProblem(PC);
    if(!Problem.IsEmpty()){Notes+=(Notes.IsEmpty()?FString():FString(TEXT("\n")))+Problem;}
    SetLine(MenuFeedback,Notes);
}

void ULDUIRoot::RefreshLobby(const ALDPlayerController& PC,const ALDGameState& Public)
{
    const FLDLobbyView& Lobby=Public.DemoLobby;
    const FLDOwnerState& Mine=PC.OwnerState;
    // Rows belong to the generation PublicMatch reports; until both match the roster is syncing, never a stale room.
    const bool bCurrent=Lobby.Generation==Public.PublicMatch.Generation;
    const bool bOwnerCurrent=bCurrent&&Mine.Match==Lobby.Generation;
    const bool bHost=bOwnerCurrent&&Mine.bLobbyHost;
    if(!bCurrent)
    {
        if(bRosterBuilt||RosterRows->GetChildrenCount()==0)
        {
            bRosterBuilt=false;
            RosterRows->ClearChildren();
            RosterRows->AddChildToVerticalBox(Label(TEXT("正在同步房间名单…"),LDHUDStyle::TypeLead,LDHUDStyle::MenuMuted));
        }
    }
    else if(!bRosterBuilt||Lobby.Generation!=SeenLobbyGeneration||Lobby.RosterRevision!=SeenLobbyRoster
        ||Lobby.PublicRevision!=SeenLobbyPublic||Mine.ContenderId!=SeenSelf||bHost!=bSeenHost
        ||Public.MatchResult.ResultId!=SeenLobbyResult||Public.MatchResult.ResultSerial!=SeenLobbyResultSerial)
    {
        bRosterBuilt=true;
        SeenLobbyGeneration=Lobby.Generation;
        SeenLobbyRoster=Lobby.RosterRevision;
        SeenLobbyPublic=Lobby.PublicRevision;
        SeenLobbyResult=Public.MatchResult.ResultId;
        SeenLobbyResultSerial=Public.MatchResult.ResultSerial;
        SeenSelf=Mine.ContenderId;
        bSeenHost=bHost;
        RebuildRoster(Public,Mine.ContenderId);
        for(int32 Count=0;Count<AIButtons.Num();++Count)
        {
            SetLine(CastChecked<UTextBlock>(AIButtons[Count]->GetContent()),
                Count==Lobby.AICount?FString::Printf(TEXT("● %d"),Count):FString::FromInt(Count));
            AIButtons[Count]->SetIsEnabled(Lobby.Members.Num()-Lobby.AICount+Count<=LDMatchLifecycle::MaxContenders);
        }
    }
    if(RoomAddressRow&&RoomAddressLabel)
    {
        RoomAddressRow->SetVisibility(bHost?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        if(bHost)
        {
            const UWorld* World=GetWorld();
            const int32 LivePort=(World&&World->URL.Port>0&&World->URL.Port<=65535)?World->URL.Port:0;
            if(CachedRoomEndpoint.IsEmpty()||CachedRoomGeneration!=Lobby.Generation||CachedRoomPort!=LivePort)
            {
                CachedRoomEndpoint=ResolveRoomEndpoint();
                CachedRoomGeneration=Lobby.Generation;
                CachedRoomPort=LivePort;
            }
            SetLine(RoomAddressLabel,FString::Printf(TEXT("房间地址：%s"),*CachedRoomEndpoint));
        }
        else if(!CachedRoomEndpoint.IsEmpty())
        {
            CachedRoomEndpoint.Reset();CachedRoomGeneration=0;CachedRoomPort=-1;
        }
    }
    AIButtonRow->SetVisibility(bHost?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    SetLine(AIReadOnly,bHost||!bCurrent?FString():FString::Printf(TEXT("AI 对手：%d"),int32(Lobby.AICount)));
    // A DEV test room says so before anyone starts: its results are fixture evidence, never a cheat-free match.
    SetLine(LobbyDevMarker,!bCurrent||(!Lobby.DevTestSession&&!Lobby.DevContentReload)?FString()
        :FString(Lobby.DevTestSession?TEXT("测试模式房间：对局中可用“测试”面板改自己的状态"):TEXT("开发房间"))
        +(Lobby.DevContentReload?TEXT(" · 内容热重载：只接受房主与 AI"):TEXT(""))+TEXT("；结果不算无作弊对局。"));
    StartMatchButton->SetVisibility(bHost?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(UWidget* StartWrap=StartMatchButton->GetParent())
    {
        StartWrap->SetVisibility(bHost?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        if(auto* StartSlot=Cast<UHorizontalBoxSlot>(StartWrap->Slot)){StartSlot->SetPadding(FMargin(bHost?0.f:8.f,0,0,0));}
    }
    StartMatchButton->SetIsEnabled(bCurrent&&Lobby.CanStart);
    const bool bCanStart=bHost&&bCurrent&&Lobby.CanStart;
    const LDUIPaint::EButton StartLook=bCanStart?LDUIPaint::EButton::Primary:LDUIPaint::EButton::Normal;
    if(StartMatchButton->Look!=StartLook){LDWidgets::SetLook(StartMatchButton,StartLook);}
    StartMatchGlow->SetVisibility(bCanStart?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    // #337 §4: hide ReadyButton for the Host; only guests see Ready / Cancel Ready.
    ReadyButton->SetVisibility(!bHost?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    ReadyButton->SetIsEnabled(bOwnerCurrent&&!bHost);
    if(UTextBlock* ReadyCap=Cast<UTextBlock>(ReadyButton->GetContent()))
    {
        SetLine(ReadyCap,Mine.bReady?TEXT("取消准备"):TEXT("准备"));
    }
    int32 GuestTotal=0,GuestReady=0;
    for(const FLDLobbyMember& Member:Lobby.Members)
    {
        if(Member.Kind==ELDContenderKind::Human&&!Member.Host)
        {
            ++GuestTotal;
            if(Member.Ready){++GuestReady;}
        }
    }
    FString BlockerLine;
    if(bCurrent&&!Lobby.CanStart)
    {
        if(bHost&&GuestTotal>0&&GuestReady<GuestTotal)
        {
            BlockerLine=FString::Printf(TEXT("等待其他玩家准备 (%d/%d)"),GuestReady,GuestTotal);
        }
        else if(!Lobby.StartBlocker.IsEmpty())
        {
            BlockerLine=TEXT("暂不能开始：")+Lobby.StartBlocker;
        }
    }
    SetLine(LobbyBlocker,BlockerLine);
    SetLine(LobbyFeedback,!PC.IntentFeedback.IsEmpty()?PC.IntentFeedback:CopiedAddressFeedback);
}

const FLDDemoContentDefinitions* ULDUIRoot::ResultDefinitions()
{
    if(!bResultContentTried)
    {
        bResultContentTried=true;
        FString Error;
        if(!LDDemoEntry::LoadRuntimeContent(ResultContent,Error)||!ResultContent)
        {
            ResultContent.Reset();
            UE_LOG(LogLDUI,Warning,TEXT("LD_RESULT_BUILD_CONTENT_UNAVAILABLE %s"),*Error);
        }
    }
    return ResultContent?&ResultContent->Get():nullptr;
}

void ULDUIRoot::RebuildRoster(const ALDGameState& Public,const FGuid& Self)
{
    // One seat card per roster seat on its seat colour: a crown on the host's, a robot on each AI's, the name, 真人/AI and 房主, and a
    // 已准备 stamp across the corner; empty seats are dashed outlines. The own seat is edged in action yellow.
    RosterRows->ClearChildren();
    // Keep three columns when the scroll bar takes width; wrapping here creates a two-/three-row layout feedback loop.
    auto* Seats=WidgetTree->ConstructWidget<UUniformGridPanel>();Seats->SetSlotPadding(FMargin(5));
    RosterRows->AddChildToVerticalBox(Seats);
    constexpr int32 SeatCount=LDMatchLifecycle::MaxContenders;
    const bool bHasPrevious=Public.DemoLobby.PreviousResultId.IsValid();
    const bool bPreviousReady=bHasPrevious&&Public.MatchResult.ResultId==Public.DemoLobby.PreviousResultId
        &&Public.MatchResult.Generation>0&&Public.MatchResult.Generation<Public.DemoLobby.Generation;
    int32 SeatIndex=0;
    const auto Seat=[this,Seats,bHasPrevious,&SeatIndex](UWidget* Card)
    {
        auto* Place=WidgetTree->ConstructWidget<USizeBox>();Place->SetMaxDesiredWidth(258.f);Place->SetHeightOverride(bHasPrevious?174.f:116.f);Place->SetContent(Card);
        auto* SeatSlot=Seats->AddChildToUniformGrid(Place,SeatIndex/3,SeatIndex%3);
        SeatSlot->SetHorizontalAlignment(HAlign_Fill);SeatSlot->SetVerticalAlignment(VAlign_Fill);
        ++SeatIndex;
    };
    int32 Shown=0;
    for(const FLDLobbyMember& Member:Public.DemoLobby.Members)
    {
        const bool bMine=Self.IsValid()&&Member.ContenderId==Self;
        const bool bHuman=Member.Kind==ELDContenderKind::Human;
        const FLinearColor Identity=LDHUD::PaletteColor(uint8(Shown));
        auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();
        if(Member.Host||!bHuman)
        {
            const bool bCrown=Member.Host;
            auto* Icon=WidgetTree->ConstructWidget<USizeBox>();Icon->SetWidthOverride(22.f);Icon->SetHeightOverride(22.f);
            Icon->SetContent(PaintLayer(WidgetTree,[bCrown](const LDUIPaint::FContext& Paint)
            {
                if(bCrown)
                {
                    Paint.Stroke(Paint.Layer,{FVector2D(3,17),FVector2D(3,7),FVector2D(7.5,12),FVector2D(11,4),FVector2D(14.5,12),
                        FVector2D(19,7),FVector2D(19,17),FVector2D(3,17)},LDHUDStyle::UIAction,2.f);
                    return;
                }
                Paint.Fill(Paint.Layer,FBox2D(FVector2D(4,8),FVector2D(18,20)),LDHUDStyle::UIText,3.f);
                Paint.Fill(Paint.Layer+1,FBox2D(FVector2D(7,12),FVector2D(9.5,14.5)),LDUIPaint::Hex(0x15130A),1.f);
                Paint.Fill(Paint.Layer+1,FBox2D(FVector2D(12.5,12),FVector2D(15,14.5)),LDUIPaint::Hex(0x15130A),1.f);
                Paint.Line(Paint.Layer,FVector2D(11,8),FVector2D(11,4),LDHUDStyle::UIText,1.5f);
                Paint.Fill(Paint.Layer,FBox2D(FVector2D(9.5,1.5),FVector2D(12.5,4.5)),LDHUDStyle::UIText,1.5f);
            }));
            UHorizontalBoxSlot* IconSlot=Head->AddChildToHorizontalBox(Icon);IconSlot->SetVerticalAlignment(VAlign_Center);IconSlot->SetPadding(FMargin(0,0,8,0));
        }
        UTextBlock* Name=Label(Member.Name+(bMine?TEXT("（你）"):TEXT("")),LDHUDStyle::TypeLead,LDHUDStyle::UIText);
        Name->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Title,Name->GetFont().Size));Name->SetAutoWrapText(false);
        Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        Name->SetClipping(EWidgetClipping::ClipToBounds);
        UHorizontalBoxSlot* NameSlot=Head->AddChildToHorizontalBox(Name);NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));NameSlot->SetVerticalAlignment(VAlign_Center);
        Box->AddChildToVerticalBox(Head);
        auto* Tags=WidgetTree->ConstructWidget<UHorizontalBox>();
        Tags->AddChildToHorizontalBox(Label(bHuman?TEXT("真人"):TEXT("AI"),LDHUDStyle::TypeSmall,LDHUDStyle::TextSecondary));
        if(Member.Host){Tags->AddChildToHorizontalBox(Label(TEXT("房主"),LDHUDStyle::TypeSmall,LDHUDStyle::UIAction))->SetPadding(FMargin(10,0,0,0));}
        if(bHuman&&!Member.Ready){Tags->AddChildToHorizontalBox(Label(TEXT("未准备"),LDHUDStyle::TypeSmall,LDHUDStyle::MenuMuted))->SetPadding(FMargin(10,0,0,0));}
        for(UWidget* Tag:Tags->GetAllChildren()){Cast<UTextBlock>(Tag)->SetAutoWrapText(false);}
        Box->AddChildToVerticalBox(Tags)->SetPadding(FMargin(0,8,0,0));
        if(bHasPrevious)
        {
            const FLDResultRow* Previous=LDCapture::PreviousLobbyResult(Public.MatchResult,Public.DemoLobby,Member);
            UWidget* Build=Previous?LDResultBuild::Make(WidgetTree,Previous->Build,ResultDefinitions(),218.f,66.f,AppliedTextScale)
                :Label(bPreviousReady?TEXT("暂无上局构筑"):TEXT("上局构筑同步中…"),LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary);
            Box->AddChildToVerticalBox(Build)->SetPadding(FMargin(0,6,0,0));
        }
        UOverlay* Card=Painted(WidgetTree,Box,FMargin(20,12),[Identity,bMine](const LDUIPaint::FContext& Paint)
        {
            const FBox2D Whole(FVector2D::ZeroVector,FVector2D(Paint.Geometry.GetLocalSize()));
            Paint.Shape(Paint.Layer,LDUIPaint::EShape::Rounded,Whole,LDHUDStyle::RadiusPanel,LDUIPaint::FFill::Linear(180.f,
                {{0.f,(LDHUDStyle::UIPanel+(Identity-LDHUDStyle::UIPanel)*.42f).CopyWithNewOpacity(.94f)},{1.f,LDHUDStyle::UIPanel}}));
            Paint.Shape(Paint.Layer+1,LDUIPaint::EShape::Rounded,FBox2D(Whole.Min,FVector2D(6.,Whole.Max.Y)),3.f,LDUIPaint::FFill::Solid(Identity));
            Paint.Edge(Paint.Layer+2,LDUIPaint::EShape::Rounded,Whole,LDHUDStyle::RadiusPanel,
                bMine?LDHUDStyle::UIAction:FLinearColor(1,1,1,.12f),bMine?2.f:1.f);
        });
        Cast<UOverlaySlot>(Box->Slot)->SetVerticalAlignment(VAlign_Center);
        if(bHuman&&Member.Ready)
        {
            UTextBlock* StampText=Label(TEXT("已准备"),LDHUDStyle::TypeSmall,LDHUDStyle::UILegal);
            StampText->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Title,StampText->GetFont().Size));StampText->SetAutoWrapText(false);
            UOverlay* Stamp=Painted(WidgetTree,StampText,FMargin(10,3),[](const LDUIPaint::FContext& Paint)
            {
                const FBox2D Whole(FVector2D::ZeroVector,FVector2D(Paint.Geometry.GetLocalSize()));
                Paint.Shape(Paint.Layer,LDUIPaint::EShape::Rounded,Whole,3.f,LDUIPaint::FFill::Solid(LDHUDStyle::UILegal.CopyWithNewOpacity(.12f)));
                Paint.Edge(Paint.Layer+1,LDUIPaint::EShape::Rounded,Whole,3.f,LDHUDStyle::UILegal,2.f);
            });
            Stamp->SetRenderTransformAngle(-8.f);Stamp->SetVisibility(ESlateVisibility::HitTestInvisible);
            UOverlaySlot* StampSlot=Card->AddChildToOverlay(Stamp);
            StampSlot->SetHorizontalAlignment(HAlign_Right);StampSlot->SetVerticalAlignment(VAlign_Top);StampSlot->SetPadding(FMargin(0,10,10,0));
        }
        Seat(Card);
        ++Shown;
    }
    for(;Shown<SeatCount;++Shown)
    {
        UTextBlock* Empty=Label(TEXT("空位"),LDHUDStyle::TypeSmall,LDHUDStyle::TextSecondary);Empty->SetJustification(ETextJustify::Center);
        UOverlay* Card=Painted(WidgetTree,Empty,FMargin(0),[](const LDUIPaint::FContext& Paint)
        {
            Paint.DashedEdge(Paint.Layer,LDUIPaint::EShape::Rounded,FBox2D(FVector2D::ZeroVector,FVector2D(Paint.Geometry.GetLocalSize())),
                LDHUDStyle::RadiusPanel,FLinearColor(1,1,1,.28f),1.5f,8.f,6.f);
        });
        Cast<UOverlaySlot>(Empty->Slot)->SetVerticalAlignment(VAlign_Center);
        Seat(Card);
    }
    // Rebuilding during a tick can otherwise paint the grid before its first layout prepass.
    Seats->ForceLayoutPrepass();
}

void ULDUIRoot::SetFrontendPage(bool bStart)
{
    bStartPage=bStart;
    // The main menu stands on the ground without a panel; the start page is a centred panel and hides the logo.
    if(UCanvasPanelSlot* PageSlot=FrontendPage?Cast<UCanvasPanelSlot>(FrontendPage->Slot):nullptr)
    {
        PageSlot->SetAnchors(bStart?FAnchors(.5f,.5f):FAnchors(.5f,1.f));PageSlot->SetAlignment(bStart?FVector2D(.5f,.5f):FVector2D(.5f,1.f));
        PageSlot->SetPosition(bStart?FVector2D::ZeroVector:FVector2D(0,-50));
        FrontendPage->SetWidthOverride(bStart?940.f:560.f);
    }
    if(FrontendPanel){FrontendPanel->SetBrushColor(bStart?LDHUDStyle::PanelFill:FLinearColor::Transparent);}
    if(FrontendLogo){FrontendLogo->SetVisibility(bStart?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);}
    MainMenu->SetVisibility(bStart?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
    StartPage->SetVisibility(bStart?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(ULDStartPageDraft* Draft=StartDraft()){Draft->bStartPage=bStart;}
    UpdateStartControls();
}

void ULDUIRoot::ShowStartPage(bool bShow)
{
    ULDStartPageDraft* Draft=StartDraft();
    if(!MainMenu||bShow==bStartPage||(Draft&&Draft->Attempt!=ELDFrontendAttempt::None)){return;}
    if(Draft){Draft->Notice.Reset();Draft->NoticeFor=ELDFrontendAttempt::None;}
    SetFrontendPage(bShow);
    if(!bShow){StartButton->SetKeyboardFocus();return;}
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(NameBox->GetText().IsEmpty()&&PC&&PC->Controls){NameBox->SetText(FText::FromString(PC->Controls->PlayerName));}
    if(NameBox->GetText().IsEmpty()){NameBox->SetKeyboardFocus();}else{HostButton->SetKeyboardFocus();}
}

void ULDUIRoot::OpenCodex()
{
    if(!CodexPage||!FrontendPage||bCodexOpen){return;}
    TArray<FLDChainCodexEntry> Entries=LDChainCodex::Load();
    Entries.Sort([](const FLDChainCodexEntry& A,const FLDChainCodexEntry& B){return A.FirstSeenUnix>B.FirstSeenUnix;});
    CodexRows->ClearChildren();
    CodexCount->SetText(FText::FromString(LDTerms::ChainCodexCount(Entries.Num())));
    // The saved key keeps the canonical step ids; each row names them like the new-chain line does.
    const FLDDemoContentDefinitions* Content=ResultDefinitions();
    for(const FLDChainCodexEntry& Entry:Entries)
    {
        TArray<FString> Parts;
        Entry.Key.ParseIntoArray(Parts,TEXT(">"));
        TArray<FName> Steps;
        for(const FString& Part:Parts){Steps.Add(FName(*Part));}
        UTextBlock* Row=Label(FString::Printf(TEXT("%s    %s"),*LDHUD::ChainKeyText(Content,Steps),*LDTerms::ChainLongest(Entry.LongestSteps)),
            LDHUDStyle::TypeText,LDHUDStyle::UIText);
        CodexRows->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,3));
    }
    bCodexOpen=true;
    FrontendPage->SetVisibility(ESlateVisibility::Collapsed);
    if(FrontendLogo){FrontendLogo->SetVisibility(ESlateVisibility::Collapsed);}
    CodexPage->SetVisibility(ESlateVisibility::Visible);
    CodexBack->SetKeyboardFocus();
}

void ULDUIRoot::ShowHowTo(int32 Index)
{
    if(!HowToPage){return;}
    HowToIndex=FMath::Clamp(Index,0,LDTerms::HowToCards-1);
    HowToCount->SetText(FText::FromString(FString::Printf(TEXT("%02d/%02d"),HowToIndex+1,LDTerms::HowToCards)));
    HowToTitle->SetText(FText::FromString(LDTerms::HowToTitle(HowToIndex)));
    HowToBody->SetText(FText::FromString(LDTerms::HowToBody(HowToIndex)));
}

void ULDUIRoot::OpenHowTo()
{
    if(!HowToPage||!FrontendPage||bHowToOpen||bCodexOpen){return;}
    ShowHowTo(0);
    bHowToOpen=true;
    // The card stands on the right; the title and the menu column stay in view on the left.
    FrontendPage->SetVisibility(ESlateVisibility::Collapsed);
    HowToPage->SetVisibility(ESlateVisibility::Visible);
    HowToBack->SetKeyboardFocus();
}

bool ULDUIRoot::CloseHowTo()
{
    if(!bHowToOpen){return false;}
    bHowToOpen=false;
    if(GConfig)
    {
        GConfig->SetBool(TEXT("Lockdown.HowTo"),TEXT("bSeen"),true,GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);
    }
    HowToPage->SetVisibility(ESlateVisibility::Collapsed);
    FrontendPage->SetVisibility(ESlateVisibility::Visible);
    if(FrontendLogo&&!bStartPage){FrontendLogo->SetVisibility(ESlateVisibility::SelfHitTestInvisible);}
    if(StartButton){StartButton->SetKeyboardFocus();}
    return true;
}

bool ULDUIRoot::CloseCodex()
{
    if(!bCodexOpen){return false;}
    bCodexOpen=false;
    CodexPage->SetVisibility(ESlateVisibility::Collapsed);
    FrontendPage->SetVisibility(ESlateVisibility::Visible);
    if(FrontendLogo&&!bStartPage){FrontendLogo->SetVisibility(ESlateVisibility::SelfHitTestInvisible);}
    if(StartButton){StartButton->SetKeyboardFocus();}
    return true;
}

bool ULDUIRoot::CloseStartPage()
{
    if(!bStartPage||bFrontendHidden){return false;}
    const ULDStartPageDraft* Draft=StartDraft();
    const ELDFrontendAttempt Attempt=Draft?Draft->Attempt:ELDFrontendAttempt::None;
    // One Back step: a join in flight is cancelled first; a host travel already handed to OpenLevel only consumes Back.
    if(Attempt==ELDFrontendAttempt::Join){CancelJoin();}
    else if(Attempt==ELDFrontendAttempt::None){ShowStartPage(false);}
    return true;
}

void ULDUIRoot::UpdateStartControls()
{
    if(!HostButton){return;}
    const ULDStartPageDraft* Draft=StartDraft();
    AppliedAttempt=Draft?Draft->Attempt:ELDFrontendAttempt::None;
    const bool bBusy=AppliedAttempt!=ELDFrontendAttempt::None;
    const bool bJoining=AppliedAttempt==ELDFrontendAttempt::Join;
    HostButton->SetIsEnabled(!bBusy&&bListenPortValid);
    JoinButton->SetIsEnabled(!bBusy);
    if(!bJoining&&CancelJoinButton->HasKeyboardFocus()){JoinButton->SetKeyboardFocus();}
    CancelJoinButton->SetVisibility(bJoining?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    // The ring turns on the card whose attempt runs.
    HostRing->SetVisibility(AppliedAttempt==ELDFrontendAttempt::Host?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    JoinRing->SetVisibility(bJoining?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    StartBackButton->SetVisibility(bStartPage&&!bBusy?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    // The typed values stay readable while connecting but cannot change under the attempt.
    for(UEditableTextBox* Box:{NameBox.Get(),AddressBox.Get(),PortBox.Get()}){Box->SetIsReadOnly(bBusy);}
}

void ULDUIRoot::RestoreFrontendPage(ALDPlayerController& PC)
{
    bFrontendHidden=false;
    bMenuOpen=true;
    FrontendPage->SetVisibility(ESlateVisibility::Visible);
    for(UWidget* Corner:FrontendCorners){Corner->SetVisibility(ESlateVisibility::HitTestInvisible);}
    if(FrontendLogo&&!bStartPage){FrontendLogo->SetVisibility(ESlateVisibility::SelfHitTestInvisible);}
    PC.SetMenuOpen(true);
}

bool ULDUIRoot::ApplyPlayerName(ALDPlayerController& PC,FString& Name)
{
    ULDStartPageDraft* Draft=StartDraft();
    auto* Instance=GetGameInstance<ULDGameInstance>();
    FString Error;
    Name=NameBox->GetText().ToString();
    if(!ULDControlSettings::ValidatePlayerName(Name,Error)){Draft->Notice=Error;return false;}
    if(!PC.Controls)
    {
        const FString Message=Instance->GetLocalSettingsMessage();
        Draft->Notice=TEXT("本地设置不可用，无法保存名称：")+(Message.IsEmpty()?FString(TEXT("没有已加载的本地设置。")):Message);
        return false;
    }
    if(!PC.Controls->SetAppliedPlayerName(Name,Error)){Draft->Notice=Error;return false;}
    Instance->RequestLocalSettingsFlush();
    // Travel with exactly the applied value that the settings file will hold.
    Name=PC.Controls->PlayerName;
    return true;
}

bool ULDUIRoot::TravelInFlight() const
{
    const FWorldContext* Context=GEngine?GEngine->GetWorldContextFromWorld(GetWorld()):nullptr;
    return Context&&(Context->PendingNetGame!=nullptr||!Context->TravelURL.IsEmpty());
}

void ULDUIRoot::Host()
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    auto* Instance=GetGameInstance<ULDGameInstance>();
    ULDStartPageDraft* Draft=StartDraft();
    if(!PC||!Instance||!Draft||Draft->Attempt!=ELDFrontendAttempt::None||!NameBox){return;}
    Draft->NoticeFor=ELDFrontendAttempt::Host;
    FString Name;
    if(!ApplyPlayerName(*PC,Name)){return;}
    // Keep the typed join endpoint too: UE reloads the frontend map when a host travel fails.
    Draft->Address=AddressBox->GetText().ToString().TrimStartAndEnd();
    Draft->Port=PortBox->GetText().ToString().TrimStartAndEnd();
    if(!bListenPortValid){Draft->Notice=TEXT("本机监听端口未配置（[URL] Port 无效），无法创建房间。");return;}
    FString Error;
    if(!LDDemoEntry::VerifyRuntimeFiles(Error)||!LDDemoEntry::VerifyWorldPackages(Error))
    {
        Instance->ConnectionMessage=Error;
        Draft->SeenConnection=Error;
        Draft->Notice=TEXT("无法创建房间：")+Error;
        return;
    }
    Instance->ConnectionMessage.Reset();
    Draft->SeenConnection.Reset();
    Draft->Attempt=ELDFrontendAttempt::Host;
    Draft->Endpoint.Reset();
    Draft->Notice=TEXT("正在创建房间…");
    UpdateStartControls();
    // The engine replaces ?Name= with the online subsystem's nickname; the chosen name travels as ?LDName=.
    UGameplayStatics::OpenLevel(this,LDDemoEntry::WorldMap,true,FString::Printf(TEXT("listen?LDName=%s"),*Name));
}

void ULDUIRoot::Join()
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    auto* Instance=GetGameInstance<ULDGameInstance>();
    ULDStartPageDraft* Draft=StartDraft();
    if(!PC||!Instance||!Draft||Draft->Attempt!=ELDFrontendAttempt::None||!NameBox){return;}
    Draft->NoticeFor=ELDFrontendAttempt::Join;
    FString Name;
    if(!ApplyPlayerName(*PC,Name)){return;}
    FString HostName=AddressBox->GetText().ToString().TrimStartAndEnd();
    FString PortText=PortBox->GetText().ToString().TrimStartAndEnd();
    FString SplitHost,SplitPort;
    if(LDDemoEntry::SplitPastedEndpoint(HostName,SplitHost,SplitPort))
    {
        HostName=SplitHost;
        PortText=SplitPort;
        bSplittingAddress=true;
        LastAddressInput=HostName;
        AddressBox->SetText(FText::FromString(HostName));
        PortBox->SetText(FText::FromString(PortText));
        bSplittingAddress=false;
    }
    Draft->Address=HostName;
    Draft->Port=PortText;
    int32 Port=0;
    if(HostName.IsEmpty()){Draft->Notice=TEXT("请输入房主 IP 或主机名。");return;}
    if(HostName.Contains(TEXT(":"))){Draft->Notice=TEXT("IP 栏只填写地址，端口请填在右侧端口栏。");return;}
    if(!ParsePort(PortText,Port)){Draft->Notice=TEXT("端口必须是 1–65535 的整数。");return;}
    const FString Address=FString::Printf(TEXT("%s:%d"),*HostName,Port);
    FString Error,Digest,Version;
    if(!LDDemoEntry::ValidateAddress(Address,Error)||!LDDemoEntry::ReadProductVersion(Version,Error)
        ||!LDDemoEntry::VerifyRuntimeFiles(Error,&Digest)||!LDDemoEntry::VerifyWorldPackages(Error))
    {
        Instance->ConnectionMessage=Error;
        Draft->SeenConnection=Error;
        Draft->Notice=TEXT("无法加入：")+Error;
        return;
    }
    Instance->ConnectionMessage.Reset();
    Draft->SeenConnection.Reset();
    Draft->Attempt=ELDFrontendAttempt::Join;
    Draft->Endpoint=Address;
    Draft->Notice=FString::Printf(TEXT("正在连接 %s…"),*Address);
    UpdateStartControls();
    CancelJoinButton->SetKeyboardFocus();
    PC->ClientTravel(Address+FString::Printf(TEXT("?LDWire=%u?LDProduct=%s?LDCatalog=%s?LDName=%s"),
        LDDemoEntry::WireProtocol,*Version,*Digest,*Name),TRAVEL_Absolute);
}

void ULDUIRoot::CancelJoin()
{
    ULDStartPageDraft* Draft=StartDraft();
    if(!Draft||Draft->Attempt!=ELDFrontendAttempt::Join||!GEngine){return;}
    if(FWorldContext* Context=GEngine->GetWorldContextFromWorld(GetWorld()))
    {
        // A client travel the engine has not started yet would still connect after CancelPending; drop only this attempt's URL.
        if(!Draft->Endpoint.IsEmpty()&&Context->TravelURL.StartsWith(Draft->Endpoint)){Context->TravelURL.Reset();}
        GEngine->CancelPending(GetWorld());
    }
    Draft->Attempt=ELDFrontendAttempt::None;
    Draft->Endpoint.Reset();
    Draft->NoticeFor=ELDFrontendAttempt::Join;
    Draft->Notice=TEXT("已取消连接");
    UpdateStartControls();
}

void ULDUIRoot::AddressTextChanged(const FText& Text)
{
    if(bSplittingAddress||!AddressBox||!PortBox){return;}
    const FString Current=Text.ToString();
    const FString Previous=LastAddressInput;
    LastAddressInput=Current;
    // Manual typing inserts at most one character per event: wait for commit/blur or Join instead of splitting on the first digit.
    if(LDDemoEntry::IsSingleCharTypingEdit(Previous,Current)){return;}
    FString SplitHost,SplitPort;
    if(LDDemoEntry::SplitPastedEndpoint(Current,SplitHost,SplitPort))
    {
        bSplittingAddress=true;
        LastAddressInput=SplitHost;
        AddressBox->SetText(FText::FromString(SplitHost));
        PortBox->SetText(FText::FromString(SplitPort));
        if(ULDStartPageDraft* Draft=StartDraft()){Draft->Address=SplitHost;Draft->Port=SplitPort;}
        bSplittingAddress=false;
    }
}

void ULDUIRoot::AddressTextCommitted(const FText& Text,ETextCommit::Type Method)
{
    if(bSplittingAddress||!AddressBox||!PortBox){return;}
    if(Method==ETextCommit::OnCleared){LastAddressInput=Text.ToString();return;}
    FString SplitHost,SplitPort;
    if(LDDemoEntry::SplitPastedEndpoint(Text.ToString(),SplitHost,SplitPort))
    {
        bSplittingAddress=true;
        LastAddressInput=SplitHost;
        AddressBox->SetText(FText::FromString(SplitHost));
        PortBox->SetText(FText::FromString(SplitPort));
        if(ULDStartPageDraft* Draft=StartDraft()){Draft->Address=SplitHost;Draft->Port=SplitPort;}
        bSplittingAddress=false;
    }
    else
    {
        LastAddressInput=Text.ToString();
    }
}

FString ULDUIRoot::ResolveRoomEndpoint() const
{
    int32 Port=7777;
    if(const UWorld* World=GetWorld();World&&World->URL.Port>0&&World->URL.Port<=65535)
    {
        Port=World->URL.Port;
    }
    else if(GConfig)
    {
        GConfig->GetInt(TEXT("URL"),TEXT("Port"),Port,GEngineIni);
    }
    FString ChosenIP=TEXT("127.0.0.1");
    if(ISocketSubsystem* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
    {
        TArray<TSharedPtr<FInternetAddr>> Addresses;
        if(Sockets->GetLocalAdapterAddresses(Addresses))
        {
            int32 BestRank=0;
            for(const TSharedPtr<FInternetAddr>& Addr:Addresses)
            {
                if(!Addr.IsValid()){continue;}
                const FString Text=Addr->ToString(false);
                if(Text.IsEmpty()||Text.Contains(TEXT(":"))||Text==TEXT("0.0.0.0")||Text==TEXT("127.0.0.1")||Text.StartsWith(TEXT("169.254.")))
                {continue;}
                int32 Rank=1;
                if(Text.StartsWith(TEXT("192.168."))||Text.StartsWith(TEXT("10."))){Rank=3;}
                else if(Text.StartsWith(TEXT("172."))){Rank=2;}
                if(Rank>BestRank){BestRank=Rank;ChosenIP=Text;}
            }
        }
    }
    return FString::Printf(TEXT("%s:%d"),*ChosenIP,Port);
}

void ULDUIRoot::CopyRoomAddress()
{
    if(CachedRoomEndpoint.IsEmpty()){CachedRoomEndpoint=ResolveRoomEndpoint();}
    FPlatformApplicationMisc::ClipboardCopy(*CachedRoomEndpoint);
    CopiedAddressFeedback=FString::Printf(TEXT("已复制 %s（发给好友粘贴到加入栏即可）"),*CachedRoomEndpoint);
    if(LobbyFeedback){LobbyFeedback->SetText(FText::FromString(CopiedAddressFeedback));LobbyFeedback->SetVisibility(ESlateVisibility::SelfHitTestInvisible);}
}

void ULDUIRoot::TextCommitted(const FText&,ETextCommit::Type Method)
{
    if(Method==ETextCommit::OnEnter){SetKeyboardFocus();}
}

UUserWidget* ULDUIRoot::TextEditingOwner() const
{
    if(!FSlateApplication::IsInitialized()){return nullptr;}
    static const FName EditableText(TEXT("SEditableText"));
    static const FName MultiLineText(TEXT("SMultiLineEditableText"));
    const TSharedPtr<SWidget> Focused=FSlateApplication::Get().GetKeyboardFocusedWidget();
    if(!Focused.IsValid()||(Focused->GetType()!=EditableText&&Focused->GetType()!=MultiLineText)){return nullptr;}
    TArray<UUserWidget*,TInlineAllocator<8>> Candidates;
    Candidates.Add(const_cast<ULDUIRoot*>(this));
    if(const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer()))
    {
        Candidates.Add(PC->ControlsUI.Get());
        Candidates.Add(PC->HUDUI.Get());
    }
    for(const TWeakObjectPtr<ULDPanel>& Panel:MainPanels){Candidates.Add(Panel.Get());}
    for(UUserWidget* Candidate:Candidates)
    {
        if(!Candidate){continue;}
        const TSharedPtr<SWidget> Root=Candidate->GetCachedWidget();
        for(TSharedPtr<SWidget> Ancestor=Focused;Root.IsValid()&&Ancestor.IsValid();Ancestor=Ancestor->GetParentWidget())
        {
            if(Ancestor==Root){return Candidate;}
        }
    }
    return nullptr;
}

bool ULDUIRoot::EndTextEditing()
{
    UUserWidget* Editor=TextEditingOwner();
    if(!Editor){return false;}
    // Focus returns to the owning panel so its own keys keep working; nothing typed is submitted or discarded.
    if(Editor!=this&&Editor->IsFocusable()){Editor->SetKeyboardFocus();}else{SetKeyboardFocus();}
    return true;
}

ULDPanel* ULDUIRoot::OpenMainPanel() const
{
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC){return nullptr;}
    if(PanelOpen(PC->ControlsUI.Get())){return PC->ControlsUI.Get();}
    for(const TWeakObjectPtr<ULDPanel>& Panel:MainPanels){if(PanelOpen(Panel.Get())){return Panel.Get();}}
    return nullptr;
}

ELDUIMainPanel ULDUIRoot::ActiveMainPanel() const
{
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC){return ELDUIMainPanel::None;}
    if(PanelOpen(PC->ControlsUI.Get())){return ELDUIMainPanel::Settings;}
    for(int32 Index=0;Index<MainPanelCount;++Index)
    {
        if(PanelOpen(MainPanels[Index].Get())){return static_cast<ELDUIMainPanel>(int32(ELDUIMainPanel::Choices)+Index);}
    }
    // The frontend main menu and the lobby are base pages; the Start page and the in-match menu are closable panels.
    if(PC->IsFrontend()){return bStartPage&&!bFrontendHidden?ELDUIMainPanel::Menu:ELDUIMainPanel::None;}
    return bMenuOpen?ELDUIMainPanel::Menu:ELDUIMainPanel::None;
}

ELDUIInputLayer ULDUIRoot::TopInputLayer() const
{
    if(TextEditingOwner()){return ELDUIInputLayer::TextEditing;}
    const ULDPanel* Open=OpenMainPanel();
    if(Confirm!=ELDRootConfirm::None||(Open&&Open->HasChildConfirmation())){return ELDUIInputLayer::ChildConfirmation;}
    return Open||ActiveMainPanel()!=ELDUIMainPanel::None?ELDUIInputLayer::MainPanel:ELDUIInputLayer::None;
}

ULDPanel* ULDUIRoot::YieldingConfirmation() const
{
    if(TextEditingOwner()||Confirm!=ELDRootConfirm::None){return nullptr;}
    ULDPanel* Open=OpenMainPanel();
    return Open&&Open->HasChildConfirmation()&&Open->ChildConfirmationYields()?Open:nullptr;
}

bool ULDUIRoot::CloseTopModal()
{
    if(EndTextEditing()){return true;}
    if(Confirm!=ELDRootConfirm::None){CloseConfirm();return true;}
    ULDPanel* Open=OpenMainPanel();
    if(!Open||!Open->HasChildConfirmation()){return false;}
    // A prompt that must be answered explicitly (keep or revert a display trial) still consumes Back.
    Open->CloseChildConfirmation();
    return true;
}

bool ULDUIRoot::CloseTopPanel()
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC){return false;}
    if(ULDPanel* Open=OpenMainPanel())
    {
        Open->ClosePanel();
        return true;
    }
    if(PC->IsFrontend()){return CloseHowTo()||CloseCodex()||CloseStartPage();}
    if(bMenuOpen){CloseMenu();return true;}
    return false;
}

void ULDUIRoot::ToggleMainPanel(ELDUIMainPanel Panel)
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    // The root's own confirmation is modal: Back answers it before any panel may switch.
    if(!PC||!ConfirmLayer||Panel==ELDUIMainPanel::None||Confirm!=ELDRootConfirm::None){return;}
    ULDControlsPanel* SettingsPanel=PC->ControlsUI.Get();
    if(Panel==ELDUIMainPanel::Settings)
    {
        const bool bOpening=!PanelOpen(SettingsPanel);
        if(bOpening&&!CloseMainPanelsExcept(Panel)){return;}
        // Opening a main panel clears the local armed preview only; accepted orders keep running (MOVEMENT_INPUT_AND_CAMERA input ownership).
        if(bOpening){PC->CancelArmedCombat();}
        PC->ToggleControlsPanel();
        return;
    }
    if(PC->IsFrontend())
    {
        // The main menu is the frontend's base page: Menu only closes Settings or the Start page and never quits.
        if(Panel!=ELDUIMainPanel::Menu){return;}
        if(PanelOpen(SettingsPanel)){SettingsPanel->ClosePanel();}else if(!CloseHowTo()&&!CloseCodex()){CloseStartPage();}
        return;
    }
    if(bInLobby){return;}
    if(Panel==ELDUIMainPanel::Menu)
    {
        if(bMenuOpen){CloseMenu();}
        else if(CloseMainPanelsExcept(Panel)){PC->CancelArmedCombat();OpenMenu();}
        return;
    }
    const int32 Index=MainPanelIndex(Panel);
    ULDPanel* Target=Index==INDEX_NONE?nullptr:MainPanels[Index].Get();
    if(!Target)
    {
        UE_LOG(LogLDUI,Warning,TEXT("LD_UI_MAIN_PANEL_UNREGISTERED %d"),int32(Panel));
        PC->PostIntentNotice(TEXT("该面板在当前版本尚未接入。"));
        return;
    }
    if(Target->IsPanelOpen()){Target->ClosePanel();}
    else if(CloseMainPanelsExcept(Panel)){PC->CancelArmedCombat();Target->OpenPanel();}
}

bool ULDUIRoot::ShowMainPanel(ELDUIMainPanel Panel)
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    const int32 Index=MainPanelIndex(Panel);
    ULDPanel* Target=Index==INDEX_NONE?nullptr:MainPanels[Index].Get();
    if(!PC||!Target||PC->IsFrontend()||bInLobby||Confirm!=ELDRootConfirm::None){return false;}
    if(Target->IsPanelOpen()){return true;}
    // A late event never replaces the menu or Settings the player opened afterwards.
    const ELDUIMainPanel Active=ActiveMainPanel();
    if(Active==ELDUIMainPanel::Menu||Active==ELDUIMainPanel::Settings){return false;}
    if(!CloseMainPanelsExcept(Panel)){return false;}
    PC->CancelArmedCombat();
    Target->OpenPanel();
    return Target->IsPanelOpen();
}

bool ULDUIRoot::CloseMainPanelsExcept(ELDUIMainPanel Keep)
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC){return false;}
    // A panel that keeps itself open (its own confirmation, an unapplied draft) blocks the switch.
    if(Keep!=ELDUIMainPanel::Settings&&PanelOpen(PC->ControlsUI.Get())&&!PC->ControlsUI->ClosePanel()){return false;}
    for(int32 Index=0;Index<MainPanelCount;++Index)
    {
        ULDPanel* Other=MainPanels[Index].Get();
        if(Index!=MainPanelIndex(Keep)&&PanelOpen(Other)&&!Other->ClosePanel()){return false;}
    }
    if(!PC->IsFrontend()&&Keep!=ELDUIMainPanel::Menu&&bMenuOpen)
    {
        // ToggleControlsPanel dismisses the menu itself and takes over SetMenuOpen; other panels never suppress the world.
        if(Keep==ELDUIMainPanel::Settings){DismissMenu();}else{CloseMenu();}
    }
    return true;
}

void ULDUIRoot::RegisterMainPanel(ELDUIMainPanel Panel,ULDPanel* Widget)
{
    const int32 Index=MainPanelIndex(Panel);
    if(Index==INDEX_NONE)
    {
        UE_LOG(LogLDUI,Error,TEXT("LD_UI_MAIN_PANEL_REGISTRATION_REJECTED %d: only Choices and DevTest are registered panels."),int32(Panel));
        return;
    }
    MainPanels[Index]=Widget;
}

void ULDUIRoot::OpenMenu()
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC||!MenuPage){return;}
    bMenuOpen=true;
    MenuPage->SetVisibility(ESlateVisibility::Visible);
    PC->SetMenuOpen(true);
}

void ULDUIRoot::CloseMenu()
{
    if(!MenuPage){return;}
    if(Confirm==ELDRootConfirm::LeaveRoom){CloseConfirm();}
    bMenuOpen=false;
    MenuPage->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer())){PC->SetMenuOpen(false);}
}

void ULDUIRoot::OpenConfirm(ELDRootConfirm Kind)
{
    const int32 Index=int32(Kind);
    if(Confirm!=ELDRootConfirm::None||!ConfirmRows.IsValidIndex(Index)||!ConfirmRows[Index]){return;}
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    // The listen Host's departure ends the whole room for every player (APP_SPEC 9).
    const bool bHost=(PC&&PC->OwnerState.bLobbyHost)||GetWorld()->GetNetMode()==NM_ListenServer;
    const FLDPublicMatchView* Match=CurrentMatch(GetWorld()->GetGameState<ALDGameState>());
    const bool bLobbyPhase=Match&&Match->Phase==ELDMatchPhase::Lobby;
    const bool bResultsPhase=Match&&Match->Phase==ELDMatchPhase::Results;
    FString Message;
    switch(Kind)
    {
    case ELDRootConfirm::Exit: Message=TEXT("确认退出游戏？");break;
    default:
        Message=bHost?TEXT("你是房主：离开将结束整个房间，所有玩家返回主菜单。")
            :bLobbyPhase?TEXT("离开后将返回主菜单；房间继续运行，其他成员仍留在 Lobby。")
            :bResultsPhase?TEXT("离开后将返回主菜单，不能重新加入本局；结算已完成，你的记录会保留。")
            :TEXT("离开后将返回主菜单，不能重新加入本局；你的离局记录会保留，其他玩家可以继续本局。");
        break;
    }
    ConfirmText->SetText(FText::FromString(Message));
    for(int32 Row=0;Row<ConfirmRows.Num();++Row)
    {
        if(ConfirmRows[Row]){ConfirmRows[Row]->SetVisibility(Row==Index?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);}
    }
    Confirm=Kind;
    ConfirmLayer->SetVisibility(ESlateVisibility::Visible);
    for(USizeBox* Page:{FrontendPage.Get(),LobbyPage.Get(),MenuPage.Get()}){if(Page){Page->SetIsEnabled(false);}}
    ConfirmCancels[Index]->SetKeyboardFocus();
}

void ULDUIRoot::CloseConfirm()
{
    if(Confirm==ELDRootConfirm::None){return;}
    Confirm=ELDRootConfirm::None;
    ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
    for(USizeBox* Page:{FrontendPage.Get(),LobbyPage.Get(),MenuPage.Get()}){if(Page){Page->SetIsEnabled(true);}}
    SetKeyboardFocus();
}

bool ULDUIRoot::PointerOverUI() const
{
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(PC&&PC->Navigation->Enabled()&&PC->HUDUI&&PC->HUDUI->PointerOverUI()){return true;}
    if(!FSlateApplication::IsInitialized()){return false;}
    if(ConfirmLayer&&ConfirmLayer->IsVisible()){return true;}
    const FVector2D Pointer=FSlateApplication::Get().GetCursorPos();
    const UWidget* Surfaces[]={HeaderPanel.Get(),FrontendPage.Get(),LobbyPage.Get(),MenuPage.Get()};
    for(const UWidget* Surface:Surfaces)
    {
        if(Surface&&Surface->IsVisible()&&Surface->GetCachedGeometry().IsUnderLocation(Pointer)){return true;}
    }
    return false;
}

void ULDUIRoot::ToggleMenu()
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC){return;}
    if(PC->IsFrontend()&&bFrontendHidden)
    {
        if(!PanelOpen(PC->ControlsUI.Get())){RestoreFrontendPage(*PC);}
        return;
    }
    ToggleMainPanel(ELDUIMainPanel::Menu);
}

void ULDUIRoot::DismissMenu()
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC){return;}
    if(PC->IsFrontend())
    {
        // Settings replaces the frontend page; TickFrontend hands the same page back once Settings closes.
        if(FrontendPage&&!bFrontendHidden)
        {
            CloseConfirm();
            bFrontendHidden=true;
            bMenuOpen=false;
            FrontendPage->SetVisibility(ESlateVisibility::Collapsed);
            for(UWidget* Corner:FrontendCorners){Corner->SetVisibility(ESlateVisibility::Collapsed);}
            if(FrontendLogo){FrontendLogo->SetVisibility(ESlateVisibility::Collapsed);}
        }
        return;
    }
    if(Confirm==ELDRootConfirm::LeaveRoom){CloseConfirm();}
    if(bMenuOpen&&MenuPage){bMenuOpen=false;MenuPage->SetVisibility(ESlateVisibility::Collapsed);}
}

FReply ULDUIRoot::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()!=EKeys::Escape){return Super::NativeOnKeyDown(Geometry,Event);}
    if(!Event.IsRepeat())
    {
        if(TextEditingOwner()==this){EndTextEditing();}
        else if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer()))
        {
            PC->V05Back();
        }
    }
    return FReply::Handled();
}

FReply ULDUIRoot::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()==EKeys::Enter && Event.IsControlDown())
    {
        if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer()); PC && !Event.IsRepeat() && PC->ToggleDevTestMenu())
        {return FReply::Handled();}
        if(Event.IsRepeat()) {return FReply::Handled();}
    }
    if(!Event.IsRepeat() && !TextEditingOwner() && ActiveMainPanel()!=ELDUIMainPanel::Settings && Confirm==ELDRootConfirm::None)
    {
        if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
            PC && PC->HandleContextualKeyPress(Event.GetKey(), Event.IsAltDown(), Event.IsControlDown(), Event.IsShiftDown()))
        {
            return FReply::Handled();
        }
    }
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}

void ULDUIRoot::LeaveRoom()
{
    CloseConfirm();
    if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&!PC->IsFrontend()&&PC->OwnerState.Match)
    {PC->LeaveDemoRoom();}
    else{UGameplayStatics::OpenLevel(this,LDDemoEntry::FrontendMap);}
}

void ULDUIRoot::Leave(){LeaveRoom();}
void ULDUIRoot::Exit(){UKismetSystemLibrary::QuitGame(this,GetOwningPlayer(),EQuitPreference::Quit,false);}
void ULDUIRoot::Resume(){if(bMenuOpen){CloseMenu();}}
void ULDUIRoot::Ready(){if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer())){PC->SetDemoReady(!PC->OwnerState.bReady);}}
void ULDUIRoot::StartMatch(){if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer())){PC->StartDemoMatch();}}
