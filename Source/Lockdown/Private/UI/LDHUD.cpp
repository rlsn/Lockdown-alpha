#include "UI/LDHUD.h"
#include "UI/LDHUDModel.h"
#include "UI/LDAttributesPanel.h"
#include "UI/LDChoicesPanel.h"
#include "UI/LDShopPanel.h"
#include "UI/LDMinimap.h"
#include "UI/LDWidgets.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDInventoryPanel.h"
#include "UI/LDUIRoot.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDDemoEntry.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "Vision/LDWorldViewRelay.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/World.h"

namespace
{
void HudSetText(UTextBlock* Text,const FString& Value)
{
    if(Text&&!Text->GetText().ToString().Equals(Value,ESearchCase::CaseSensitive)){Text->SetText(FText::FromString(Value));}
}
void HudSetShown(UWidget* Widget,bool bShown,ESlateVisibility Shown)
{
    if(!Widget){return;}
    const ESlateVisibility Next=bShown?Shown:ESlateVisibility::Collapsed;
    if(Widget->GetVisibility()!=Next){Widget->SetVisibility(Next);}
}
void HudTint(ULDCommandButton* Button,const FLinearColor& Color)
{
    if(!Button){return;}
    auto Style=Button->GetStyle();
    if(Style.Normal.TintColor==FSlateColor(Color)){return;}
    Style.Normal.TintColor=FSlateColor(Color);Style.Hovered.TintColor=FSlateColor(Color*1.6f);Button->SetStyle(Style);
}
const FName HudActionBindings[]={TEXT("Skill1"),TEXT("Skill2"),TEXT("Item1"),TEXT("Item2")};
const ELDCombatEntry HudActionEntries[]={ELDCombatEntry::Q,ELDCombatEntry::W,ELDCombatEntry::Item1,ELDCombatEntry::Item2};
const FName HudActionNames[]={TEXT("HUDActionQ"),TEXT("HUDActionW"),TEXT("HUDActionItem1"),TEXT("HUDActionItem2")};
// Status control bits that forbid each entry kind: character actives cast, equipment actives use gear.
const uint8 HudActionControl[]={uint8(ELDCombatControlBlock::Cast),uint8(ELDCombatControlBlock::Cast),uint8(ELDCombatControlBlock::Gear),uint8(ELDCombatControlBlock::Gear)};
}

TSharedRef<SWidget> ULDHUD::RebuildWidget()
{
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if(!Band)
    {
        auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
        auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Canvas;
        auto Text=[&](const FString& S,int32 Size){return LDWidgets::Text(WidgetTree,S,Size);};
        auto Panel=[&](UWidget* Child,bool bHit)
        {
            auto* Border=LDWidgets::Panel(WidgetTree,Child,FMargin(12,8));
            if(bHit){HitPanels.Add(Border);}else{Border->SetVisibility(ESlateVisibility::HitTestInvisible);}
            return Border;
        };
        auto Place=[&](UWidget* Widget,FAnchors Anchors,FVector2D Alignment,FVector2D Position)
        {auto* S=Canvas->AddChildToCanvas(Widget);S->SetAnchors(Anchors);S->SetAlignment(Alignment);S->SetPosition(Position);S->SetAutoSize(true);};
        auto Bar=[&](FLinearColor Fill,UProgressBar*& OutBar,UTextBlock*& OutText,float Width,float Height=24.f)
        {
            auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(Width);Box->SetHeightOverride(Height);
            auto* Stack=WidgetTree->ConstructWidget<UOverlay>();Box->SetContent(Stack);
            OutBar=WidgetTree->ConstructWidget<UProgressBar>();OutBar->SetFillColorAndOpacity(Fill);OutBar->SetPercent(0);
            auto* BarSlot=Stack->AddChildToOverlay(OutBar);BarSlot->SetHorizontalAlignment(HAlign_Fill);BarSlot->SetVerticalAlignment(VAlign_Fill);
            OutText=Text(TEXT(""),14);OutText->SetAutoWrapText(false);OutText->SetJustification(ETextJustify::Center);
            OutText->SetShadowOffset(FVector2D(1,1));OutText->SetShadowColorAndOpacity(FLinearColor(0,0,0,.85f));
            auto* TextSlot=Stack->AddChildToOverlay(OutText);TextSlot->SetHorizontalAlignment(HAlign_Center);TextSlot->SetVerticalAlignment(VAlign_Center);
            return Box;
        };
        auto Portraits=[&](float Size,int32 GlyphSize,UBorder*& OutFrame,UTextBlock*& OutGlyph)
        {
            OutGlyph=Text(TEXT(""),GlyphSize);OutGlyph->SetAutoWrapText(false);OutGlyph->SetJustification(ETextJustify::Center);
            OutFrame=WidgetTree->ConstructWidget<UBorder>();OutFrame->SetHorizontalAlignment(HAlign_Center);OutFrame->SetVerticalAlignment(VAlign_Center);OutFrame->SetContent(OutGlyph);
            auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(Size);Box->SetHeightOverride(Size);Box->SetContent(OutFrame);
            return Box;
        };

        // Top-left: the current authorized target only. It disappears with the observation and never greys into history.
        auto* TargetBox=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* TargetHead=WidgetTree->ConstructWidget<UHorizontalBox>();TargetBox->AddChildToVerticalBox(TargetHead);
        UBorder* TP=nullptr;UTextBlock* TG=nullptr;TargetHead->AddChildToHorizontalBox(Portraits(44,18,TP,TG))->SetPadding(FMargin(0,0,10,0));TargetPortrait=TP;TargetGlyph=TG;
        TargetTitle=Text(TEXT(""),18);auto* TitleSlot=TargetHead->AddChildToHorizontalBox(TargetTitle);TitleSlot->SetVerticalAlignment(VAlign_Center);TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        UProgressBar* B=nullptr;UTextBlock* T=nullptr;
        TargetBox->AddChildToVerticalBox(Bar(FLinearColor(.72f,.2f,.17f),B,T,306))->SetPadding(FMargin(0,8,0,2));TargetHealthBar=B;TargetHealth=T;
        TargetManaRow=Bar(FLinearColor(.2f,.36f,.78f),B,T,306);TargetBox->AddChildToVerticalBox(TargetManaRow)->SetPadding(FMargin(0,2));TargetManaBar=B;TargetMana=T;
        TargetStatuses=Text(TEXT(""),14);TargetBox->AddChildToVerticalBox(TargetStatuses)->SetPadding(FMargin(0,4,0,0));
        TargetEquipment=Text(TEXT(""),14);TargetBox->AddChildToVerticalBox(TargetEquipment)->SetPadding(FMargin(0,2,0,0));
        auto* TargetWidth=WidgetTree->ConstructWidget<USizeBox>();TargetWidth->SetWidthOverride(330);TargetWidth->SetContent(TargetBox);
        TargetCard=Panel(TargetWidth,true);Place(TargetCard,FAnchors(0,0),FVector2D(0,0),FVector2D(16,16));TargetCard->SetVisibility(ESlateVisibility::Collapsed);

        // Top-centre: the public capture state, the public kill feed, then the own death banner. None takes pointer input.
        auto* CircleBox=WidgetTree->ConstructWidget<UVerticalBox>();
        PhaseText=Text(TEXT(""),18);PhaseText->SetAutoWrapText(false);PhaseText->SetJustification(ETextJustify::Center);
        CircleBox->AddChildToVerticalBox(PhaseText)->SetHorizontalAlignment(HAlign_Center);
        CircleProgress=WidgetTree->ConstructWidget<UProgressBar>();CircleProgress->SetFillColorAndOpacity(FLinearColor(1,.78f,.25f));CircleProgress->SetPercent(0);
        auto* CircleBarSize=WidgetTree->ConstructWidget<USizeBox>();CircleBarSize->SetWidthOverride(360);CircleBarSize->SetHeightOverride(8);CircleBarSize->SetContent(CircleProgress);
        CircleBox->AddChildToVerticalBox(CircleBarSize)->SetPadding(FMargin(0,6,0,0));
        PhaseBanner=Panel(CircleBox,false);Place(PhaseBanner,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,16));
        KillFeedText=Text(TEXT(""),15);KillFeedText->SetJustification(ETextJustify::Center);
        KillFeedPanel=Panel(KillFeedText,false);Place(KillFeedPanel,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,92));KillFeedPanel->SetVisibility(ESlateVisibility::Collapsed);
        auto* DeathBox=WidgetTree->ConstructWidget<UVerticalBox>();
        DeathTitle=Text(TEXT(""),34);DeathTitle->SetJustification(ETextJustify::Center);DeathTitle->SetColorAndOpacity(FLinearColor(1,.55f,.45f));
        DeathCountdown=Text(TEXT(""),22);DeathCountdown->SetJustification(ETextJustify::Center);
        DeathNote=Text(TEXT(""),14);DeathNote->SetJustification(ETextJustify::Center);
        for(UTextBlock* Line:{DeathTitle.Get(),DeathCountdown.Get(),DeathNote.Get()}){DeathBox->AddChildToVerticalBox(Line)->SetHorizontalAlignment(HAlign_Center);}
        DeathBanner=Panel(DeathBox,false);Place(DeathBanner,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,200));DeathBanner->SetVisibility(ESlateVisibility::Collapsed);

        // Centre: the one frozen result; it replaces the gameplay HUD instead of overlaying it.
        auto* ResultBox=WidgetTree->ConstructWidget<UVerticalBox>();
        ResultTitle=Text(TEXT(""),28);ResultTitle->SetJustification(ETextJustify::Center);ResultBox->AddChildToVerticalBox(ResultTitle)->SetHorizontalAlignment(HAlign_Center);
        ResultSubtitle=Text(TEXT(""),16);ResultSubtitle->SetJustification(ETextJustify::Center);ResultBox->AddChildToVerticalBox(ResultSubtitle)->SetPadding(FMargin(0,6,0,12));
        ResultRows=WidgetTree->ConstructWidget<UVerticalBox>();ResultBox->AddChildToVerticalBox(ResultRows);
        auto* ResultActions=WidgetTree->ConstructWidget<UHorizontalBox>();ResultBox->AddChildToVerticalBox(ResultActions)->SetPadding(FMargin(0,16,0,0));
        ReturnButton=LDWidgets::Button(WidgetTree,TEXT("带房间返回 Lobby"),[PC](){PC->ReturnDemoToLobby();},false,TEXT("ResultReturnToLobby"));
        ResultActions->AddChildToHorizontalBox(ReturnButton)->SetPadding(FMargin(0,0,12,0));
        ResultActions->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("离开房间"),[PC](){if(PC->RootUI){PC->RootUI->LeaveRoom();}},false,TEXT("ResultLeaveRoom")));
        auto* ResultWidth=WidgetTree->ConstructWidget<USizeBox>();ResultWidth->SetWidthOverride(640);ResultWidth->SetContent(ResultBox);
        ResultPanel=Panel(ResultWidth,true);Place(ResultPanel,FAnchors(.5f,.5f),FVector2D(.5f,.5f),FVector2D(0,0));ResultPanel->SetVisibility(ESlateVisibility::Collapsed);

        // Top-right: minimap, sight state and the settings entry.
        auto* MapBox=WidgetTree->ConstructWidget<UVerticalBox>();
        Minimap=CreateWidget<ULDMinimap>(PC);MapFrame=WidgetTree->ConstructWidget<USizeBox>();MapFrame->SetWidthOverride(240);MapFrame->SetHeightOverride(240);MapFrame->SetContent(Minimap);
        MapBox->AddChildToVerticalBox(MapFrame);
        SightText=Text(TEXT(""),13);MapBox->AddChildToVerticalBox(SightText)->SetPadding(FMargin(0,4));
        MapBox->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,TEXT("设置"),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Settings);}},true,TEXT("HUDSettings")));
        MapColumn=Panel(MapBox,true);Place(MapColumn,FAnchors(1,0),FVector2D(1,0),FVector2D(-16,16));

        // Bottom band: own identity, resources, the 2+2 action entries and utilities.
        auto* Bottom=WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* SelfHead=WidgetTree->ConstructWidget<UHorizontalBox>();
        UBorder* SP=nullptr;UTextBlock* SG=nullptr;SelfHead->AddChildToHorizontalBox(Portraits(64,26,SP,SG))->SetPadding(FMargin(0,0,10,0));Portrait=SP;PortraitGlyph=SG;
        auto* NameBox=WidgetTree->ConstructWidget<UVerticalBox>();
        SelfName=Text(TEXT(""),18);SelfName->SetAutoWrapText(false);NameBox->AddChildToVerticalBox(SelfName);
        LifeText=Text(TEXT(""),14);NameBox->AddChildToVerticalBox(LifeText)->SetPadding(FMargin(0,4,0,0));
        // Temporary statuses and passives are separate lines, so a control status is never lost among passives.
        SelfStatusText=Text(TEXT(""),13);SelfStatusText->SetColorAndOpacity(FLinearColor(1,.7f,.45f));NameBox->AddChildToVerticalBox(SelfStatusText)->SetPadding(FMargin(0,2,0,0));
        PassiveText=Text(TEXT(""),12);PassiveText->SetColorAndOpacity(FLinearColor(.72f,.8f,.86f));NameBox->AddChildToVerticalBox(PassiveText)->SetPadding(FMargin(0,2,0,0));
        SelfHead->AddChildToHorizontalBox(NameBox)->SetVerticalAlignment(VAlign_Center);
        auto* SelfWidth=WidgetTree->ConstructWidget<USizeBox>();SelfWidth->SetWidthOverride(250);SelfWidth->SetContent(SelfHead);
        Bottom->AddChildToHorizontalBox(SelfWidth)->SetPadding(FMargin(0,0,14,0));
        auto* Stats=WidgetTree->ConstructWidget<UVerticalBox>();
        Stats->AddChildToVerticalBox(Bar(FLinearColor(.72f,.2f,.17f),B,T,280));HealthBar=B;HealthText=T;
        Stats->AddChildToVerticalBox(Bar(FLinearColor(.2f,.36f,.78f),B,T,280))->SetPadding(FMargin(0,4));ManaBar=B;ManaText=T;
        Stats->AddChildToVerticalBox(Bar(FLinearColor(.62f,.52f,.16f),B,T,280,20));LevelBar=B;LevelText=T;
        GoldText=Text(TEXT(""),15);Stats->AddChildToVerticalBox(GoldText)->SetPadding(FMargin(0,4,0,0));
        Bottom->AddChildToHorizontalBox(Stats)->SetPadding(FMargin(0,0,14,0));
        auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();
        for(int32 Index=0;Index<4;++Index)
        {
            FLDHUDActionSlot& Entry=ActionSlots.AddDefaulted_GetRef();
            auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();
            Entry.Key=Text(TEXT(""),13);Entry.Key->SetAutoWrapText(false);Entry.Key->SetColorAndOpacity(FLinearColor(1,.82f,.4f));Lines->AddChildToVerticalBox(Entry.Key);
            Entry.Name=Text(TEXT(""),13);Lines->AddChildToVerticalBox(Entry.Name)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            Entry.State=Text(TEXT(""),12);Lines->AddChildToVerticalBox(Entry.State);
            Entry.Cooldown=WidgetTree->ConstructWidget<UProgressBar>();Entry.Cooldown->SetFillColorAndOpacity(FLinearColor(.85f,.85f,.9f,.8f));Entry.Cooldown->SetPercent(0);
            auto* SweepBox=WidgetTree->ConstructWidget<USizeBox>();SweepBox->SetHeightOverride(5);SweepBox->SetContent(Entry.Cooldown);Lines->AddChildToVerticalBox(SweepBox)->SetPadding(FMargin(0,3,0,0));
            Entry.Frame=WidgetTree->ConstructWidget<UBorder>();Entry.Frame->SetPadding(FMargin(6,4));Entry.Frame->SetContent(Lines);
            // The icon sends exactly what its hotkey sends; Combat decides Self casts and the armed target forms.
            const ELDCombatEntry Activated=HudActionEntries[Index];
            Entry.Button=LDWidgets::Button(WidgetTree,TEXT(""),[PC,Activated](){PC->ActivateCombatEntry(Activated);},true,HudActionNames[Index]);
            auto ButtonStyle=Entry.Button->GetStyle();ButtonStyle.NormalPadding=FMargin(0);ButtonStyle.PressedPadding=FMargin(0);Entry.Button->SetStyle(ButtonStyle);
            Entry.Button->SetContent(Entry.Frame);
            if(UButtonSlot* FrameSlot=Cast<UButtonSlot>(Entry.Frame->Slot)){FrameSlot->SetHorizontalAlignment(HAlign_Fill);FrameSlot->SetVerticalAlignment(VAlign_Fill);}
            auto* SlotSize=WidgetTree->ConstructWidget<USizeBox>();SlotSize->SetWidthOverride(92);SlotSize->SetHeightOverride(92);SlotSize->SetContent(Entry.Button);
            // Character actives (Q/W) and equipment actives (1/2) are separate groups, never one merged bar.
            Actions->AddChildToHorizontalBox(SlotSize)->SetPadding(FMargin(Index==2?18:0,0,6,0));
        }
        Bottom->AddChildToHorizontalBox(Actions)->SetPadding(FMargin(0,0,8,0));
        auto* Utility=WidgetTree->ConstructWidget<UVerticalBox>();
        // Own panels open while dead as well (UI 3.2); the root keeps one main panel at a time.
        auto* PanelRow=WidgetTree->ConstructWidget<UHorizontalBox>();Utility->AddChildToVerticalBox(PanelRow)->SetPadding(FMargin(0,0,0,6));
        auto* Pick=LDWidgets::Button(WidgetTree,TEXT("待选"),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Choices);}},true,TEXT("HUDChoices"));
        auto* Sheet=LDWidgets::Button(WidgetTree,TEXT("属性"),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Attributes);}},true,TEXT("HUDAttributes"));
        Commands.Add(TEXT("Choices"),Pick);Commands.Add(TEXT("Attributes"),Sheet);
        PanelRow->AddChildToHorizontalBox(Pick)->SetPadding(FMargin(0,0,6,0));PanelRow->AddChildToHorizontalBox(Sheet);
        auto* Stop=LDWidgets::Button(WidgetTree,TEXT("停止"),[PC](){PC->StopCombatAndOrders();},true,TEXT("HUDStop"));
        Commands.Add(TEXT("Stop"),Stop);Utility->AddChildToVerticalBox(Stop);
        CameraText=Text(TEXT(""),13);Utility->AddChildToVerticalBox(CameraText)->SetPadding(FMargin(0,6,0,0));
        auto* UtilityWidth=WidgetTree->ConstructWidget<USizeBox>();UtilityWidth->SetWidthOverride(196);UtilityWidth->SetContent(Utility);
        Bottom->AddChildToHorizontalBox(UtilityWidth);
        Band=Panel(Bottom,true);Place(Band,FAnchors(.5f,1),FVector2D(.5f,1),FVector2D(0,-12));

        Toast=Text(TEXT(""),16);ToastPanel=Panel(Toast,false);Place(ToastPanel,FAnchors(.5f,1),FVector2D(.5f,1),FVector2D(0,-150));ToastPanel->SetVisibility(ESlateVisibility::Collapsed);
        ArmedText=Text(TEXT(""),15);ArmedText->SetColorAndOpacity(FLinearColor(1,.85f,.45f));
        ArmedPanel=Panel(ArmedText,false);Place(ArmedPanel,FAnchors(.5f,1),FVector2D(.5f,1),FVector2D(0,-196));ArmedPanel->SetVisibility(ESlateVisibility::Collapsed);

        // Normal main panels fill the canvas with a hit-transparent frame, so the world beside them keeps its input.
        AttributesPanel=CreateWidget<ULDAttributesPanel>(PC);ChoicesPanel=CreateWidget<ULDChoicesPanel>(PC);ShopPanel=CreateWidget<ULDShopPanel>(PC);
        const TPair<ELDUIMainPanel,ULDPanel*> Docked[]={{ELDUIMainPanel::Attributes,AttributesPanel.Get()},{ELDUIMainPanel::Choices,ChoicesPanel.Get()},{ELDUIMainPanel::Shop,ShopPanel.Get()}};
        for(const TPair<ELDUIMainPanel,ULDPanel*>& Entry:Docked)
        {
            if(!Entry.Value){UE_LOG(LogTemp,Error,TEXT("LD_HUD_PANEL_CREATION_FAILED %d"),int32(Entry.Key));continue;}
            auto* PanelSlot=Canvas->AddChildToCanvas(Entry.Value);PanelSlot->SetAnchors(FAnchors(0,0,1,1));PanelSlot->SetOffsets(FMargin(0));PanelSlot->SetZOrder(10);
            if(PC->RootUI){PC->RootUI->RegisterMainPanel(Entry.Key,Entry.Value);}
        }
        if(ShopPanel){ShopPanel->Notify=[this](const FString& Notice){ShowToast(Notice);};}
        if(ChoicesPanel){ChoicesPanel->Notify=[this](const FString& Notice){ShowToast(Notice);};}
        // Growth's owning-connection entries; the panels only build commands from the views they showed.
        const TWeakObjectPtr<ALDPlayerController> WeakPC(PC);
        if(ChoicesPanel)
        {
            ChoicesPanel->SubmitChoice=[WeakPC](const FLDChoiceCommand& Command,ELDGrowthReason& Reason)->uint64
            {if(ALDPlayerController* Owner=WeakPC.Get()){return Owner->SubmitChoice(Command,Reason);}Reason=ELDGrowthReason::DataNotReady;return 0;};
            ChoicesPanel->PeekRequestId=[WeakPC]()->uint64{const ALDPlayerController* Owner=WeakPC.Get();return Owner?Owner->PeekGrowthRequestId(ELDGrowthFamily::Choice):0;};
        }
        if(ShopPanel)
        {
            ShopPanel->SubmitShopCommand=[WeakPC](const FLDShopCommand& Command,ELDGrowthReason& Reason)->uint64
            {if(ALDPlayerController* Owner=WeakPC.Get()){return Owner->SubmitShopCommand(Command,Reason);}Reason=ELDGrowthReason::DataNotReady;return 0;};
            ShopPanel->PeekRequestId=[WeakPC]()->uint64{const ALDPlayerController* Owner=WeakPC.Get();return Owner?Owner->PeekGrowthRequestId(ELDGrowthFamily::Shop):0;};
        }
        PC->OnGrowthSubmit.AddUObject(this,&ULDHUD::HandleGrowthSubmit);
        PC->OnChoiceReceipt.AddUObject(this,&ULDHUD::HandleChoiceReceipt);
        PC->OnShopReceipt.AddUObject(this,&ULDHUD::HandleShopReceipt);
        PC->OnShopOpenResult.AddUObject(this,&ULDHUD::HandleShopOpen);
    }
    return Super::RebuildWidget();
}
bool ULDHUD::PointerOverUI() const
{
    if(!IsVisible()||!FSlateApplication::IsInitialized()){return false;}return IsUIAt(FSlateApplication::Get().GetCursorPos());
}
bool ULDHUD::IsUIAt(FVector2D P) const
{
    if(Minimap&&Minimap->IsDragging()){return true;}
    for(const ULDPanel* Open:MainPanels()){if(Open&&Open->IsPanelHit(P)){return true;}}
    for(const auto& Panel:HitPanels){if(Panel&&Panel->IsVisible()&&Panel->GetCachedGeometry().IsUnderLocation(P)){return true;}}return false;
}
TArray<ULDPanel*,TInlineAllocator<3>> ULDHUD::MainPanels() const
{
    TArray<ULDPanel*,TInlineAllocator<3>> Result;
    for(ULDPanel* Panel:{static_cast<ULDPanel*>(AttributesPanel.Get()),static_cast<ULDPanel*>(ChoicesPanel.Get()),static_cast<ULDPanel*>(ShopPanel.Get())}){if(Panel){Result.Add(Panel);}}
    return Result;
}
FReply ULDHUD::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{return IsUIAt(E.GetScreenSpacePosition())?FReply::Handled():Super::NativeOnMouseButtonDown(G,E);}
FReply ULDHUD::NativeOnMouseWheel(const FGeometry& G,const FPointerEvent& E)
{return IsUIAt(E.GetScreenSpacePosition())?FReply::Handled():Super::NativeOnMouseWheel(G,E);}
const FLDDemoContentDefinitions* ULDHUD::DisplayDefinitions()
{
    if(!bDisplayContentTried)
    {
        bDisplayContentTried=true;FString Error;
        if(!LDDemoEntry::LoadRuntimeContent(DisplayContent,Error)||!DisplayContent){DisplayContent.Reset();UE_LOG(LogTemp,Error,TEXT("LD_HUD_CONTENT_UNAVAILABLE %s"),*Error);}
    }
    return DisplayContent?&DisplayContent->Get():nullptr;
}
void ULDHUD::RefreshReadModel()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* GS=GetWorld()->GetGameState<ALDGameState>();if(!GS||!Band){return;}
    const FLDOwnerState& O=PC->OwnerState;const FLDLobbyView& Lobby=GS->DemoLobby;const FLDPublicMatchView& Public=GS->PublicMatch;
    // PublicMatch is the single public phase source; a projection from another generation is never this match.
    const bool bOwnMatch=Public.Generation!=0&&Public.Generation==O.Match;
    const bool Playing=bOwnMatch&&(Public.Phase==ELDMatchPhase::Development||Public.Phase==ELDMatchPhase::CaptureOpen);
    const bool bResults=bOwnMatch&&Public.Phase==ELDMatchPhase::Results;
    // Full-viewport menus draw below this widget, so the HUD steps aside while they are open.
    const bool Overlay=(PC->ControlsUI&&PC->ControlsUI->IsOpen())||(PC->InventoryUI&&PC->InventoryUI->IsOpen())||(PC->RootUI&&PC->RootUI->IsMenuOpen());
    const bool bShow=Playing&&!Overlay;
    if(!Playing)
    {
        // The result replaces gameplay UI: no main panel outlives play (UI 3.1); closing never undoes submitted work.
        for(ULDPanel* Open:MainPanels()){if(Open->IsPanelOpen()){Open->ClosePanel();}}
    }
    HudSetShown(Band,bShow,ESlateVisibility::Visible);HudSetShown(MapColumn,bShow,ESlateVisibility::Visible);HudSetShown(PhaseBanner,bShow,ESlateVisibility::HitTestInvisible);
    HudSetShown(ResultPanel,bResults&&!Overlay,ESlateVisibility::Visible);
    if(bResults&&!Overlay){RefreshResult(*PC,*GS);}
    if(!bShow)
    {
        for(UWidget* Hidden:{static_cast<UWidget*>(TargetCard.Get()),static_cast<UWidget*>(DeathBanner.Get()),static_cast<UWidget*>(ToastPanel.Get()),static_cast<UWidget*>(KillFeedPanel.Get()),static_cast<UWidget*>(ArmedPanel.Get())}){HudSetShown(Hidden,false,ESlateVisibility::Visible);}
        return;
    }

    const float TextScale=PC->Controls?PC->Controls->TextScale:1;
    if(AppliedScale!=TextScale)
    {
        const float Previous=AppliedScale>0?AppliedScale:1;AppliedScale=TextScale;TArray<UWidget*> All;WidgetTree->GetAllWidgets(All);
        for(auto* W:All){if(auto* Label=Cast<UTextBlock>(W)){auto Font=Label->GetFont();Font.Size=FMath::RoundToInt(Font.Size*TextScale/Previous);Label->SetFont(Font);}}
    }
    // The docked panels own separate widget trees and follow the same preference themselves.
    for(ULDPanel* Docked:MainPanels()){Docked->ApplyTextScale(TextScale);}
    if(MapAspect<=0)
    {
        // The frame follows the registered ground aspect so the minimap never stretches world distances.
        const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();const FLDDemoWorldGeometry* Geometry=Registry&&Registry->Enabled()?Registry->GetGeometry():nullptr;
        FVector Min,Max;FString Error;
        if(Geometry&&Geometry->Surfaces.Num()==1&&Geometry->Surfaces[0].MinCm.ToVector(Min,Error)&&Geometry->Surfaces[0].MaxCm.ToVector(Max,Error)&&Max.X>Min.X&&Max.Y>Min.Y)
        {
            MapAspect=float((Max.Y-Min.Y)/(Max.X-Min.X));const float Width=MapAspect>1?240.f/MapAspect:240.f;
            MapFrame->SetWidthOverride(Width);MapFrame->SetHeightOverride(Width*MapAspect);
        }
    }
    const FLDDemoContentDefinitions* Content=DisplayDefinitions();
    int64 NowMs=0;const bool bClock=PC->ReadDemoAuthorityTimeMs(NowMs);
    const bool bAlive=O.LifePhase==ELDLifePhase::Alive;

    const LDHUD::FCircleView Circle=LDHUD::Circle(Public,LDHUD::RosterName(Lobby,Public.Occupier),bClock,NowMs);
    HudSetText(PhaseText,Circle.Line);CircleProgress->SetPercent(Circle.Progress);
    HudSetShown(CircleProgress->GetParent(),Circle.bProgress,ESlateVisibility::HitTestInvisible);
    RefreshKillFeed(*GS,Content,bClock,NowMs);
    const LDHUD::FDeathView Death=LDHUD::Death(O,bClock,NowMs);
    HudSetShown(DeathBanner,Death.bVisible,ESlateVisibility::HitTestInvisible);
    if(Death.bVisible){HudSetText(DeathTitle,Death.Title);HudSetText(DeathCountdown,Death.Countdown);HudSetText(DeathNote,Death.Note);}

    const FString Name=LDHUD::RosterName(Lobby,O.ContenderId);
    HudSetText(SelfName,Name.IsEmpty()?FString(TEXT("名单同步中")):Name);
    HudSetText(PortraitGlyph,Name.IsEmpty()?FString(TEXT("?")):Name.Left(1));
    if(const auto* Body=Cast<ALDCharacter>(PC->GetPawn())){Portrait->SetBrushColor(LDHUD::PaletteColor(Body->PresentationKey));}
    HudSetText(LifeText,LDHUD::LifeStatus(O,bClock,NowMs));
    // Owner projections of this match and life only; anything older reads as syncing, never as zero.
    const FLDCombatOwnerView* OwnCombat=bOwnMatch&&O.Life!=0&&PC->CombatView.Generation==O.Match&&PC->CombatView.Life==O.Life?&PC->CombatView:nullptr;
    const FLDProgressionView* Progression=PC->ReadOwnProgression();
    const FLDEquipmentView* Equipment=PC->ReadOwnEquipment();
    LDHUD::FResourceView Health,Mana;double ShieldPoints=0;
    if(OwnCombat&&OwnCombat->bHasBody)
    {
        Health=LDHUD::ResourceP2(TEXT("面子"),OwnCombat->HPP2,OwnCombat->MaxHPP2,true);
        Mana=LDHUD::ResourceP2(TEXT("胆量"),OwnCombat->MPP2,OwnCombat->MaxMPP2,false);
        ShieldPoints=double(OwnCombat->ShieldP2)/100.;
    }
    else
    {
        Health=LDHUD::Resource(TEXT("面子"),O.Hp,O.MaxHp,true);
        Mana=LDHUD::Resource(TEXT("胆量"),O.Mp,O.MaxMp,false);
        ShieldPoints=O.Barrier;
    }
    FString HealthLine=Health.Text;
    if(Health.bKnown&&ShieldPoints>0){HealthLine+=FString::Printf(TEXT(" · 淡定 %.0f"),FMath::CeilToDouble(ShieldPoints));}
    HealthBar->SetPercent(Health.Fraction);HudSetText(HealthText,HealthLine);
    ManaBar->SetPercent(Mana.Fraction);HudSetText(ManaText,Mana.Text);
    LDHUD::FLevelView Level;
    if(Progression)
    {Level=LDHUD::Level(Progression->Level,Progression->MaxLevel,Progression->CumulativeXP,Progression->LevelThresholdXP,Progression->bHasNextThreshold,Progression->NextThresholdXP);}
    else{Level.Level=TEXT("等级 —");Level.Experience=TEXT("成长数据同步中");}
    LevelBar->SetPercent(Level.Fraction);HudSetText(LevelText,Level.Level+TEXT(" · ")+Level.Experience);
    HudSetText(GoldText,Equipment?LDHUD::Gold(Equipment->Gold):FString(TEXT("零花钱 同步中")));
    HudSetText(SelfStatusText,OwnCombat?LDHUD::SelfStatusLine(*OwnCombat,bClock,NowMs,Content):FString());
    HudSetShown(SelfStatusText,!SelfStatusText->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    HudSetText(PassiveText,LDHUD::PassiveLine(Progression,OwnCombat,Content));

    // A child confirmation owns input: an armed preview behind it is cancelled locally, never kept for a later click.
    const ELDUIInputLayer Layer=PC->RootUI?PC->RootUI->TopInputLayer():ELDUIInputLayer::None;
    if(Layer==ELDUIInputLayer::ChildConfirmation){PC->CancelArmedCombat();}
    if(AttributesPanel){AttributesPanel->Present(OwnCombat,Progression,bClock,NowMs,Content);}
    if(ChoicesPanel){ChoicesPanel->Present(Progression?*Progression:FLDProgressionView(),O.Match,O.Life,O.LifePhase,Content);}
    if(ShopPanel)
    {
        const FLDShopView* ShopView=PC->ReadOwnShop();
        if(bOpenedShop)
        {
            // Until the next growth publication arrives, the Opened result's own quote stands in for the lagging projection.
            const bool bAhead=OpenedShop.Generation==O.Match&&OpenedShop.Life==O.Life&&PC->OwnerState.Growth.PublishRevision==OpenedShopPublish;
            if(!bAhead){bOpenedShop=false;}
            else if(!ShopView||ShopView->SessionId!=OpenedShop.View.SessionId){ShopView=&OpenedShop.View;}
        }
        ShopPanel->Present(ShopView,Equipment,Progression!=nullptr,Progression?Progression->BuildRevision:0,O.Match,O.Life,O.LifePhase,bClock,NowMs,Content);
    }
    if(ULDCommandButton* Pick=Commands.FindRef(TEXT("Choices")))
    {
        HudSetText(CastChecked<UTextBlock>(Pick->GetContent()),Progression?FString::Printf(TEXT("待选 %d"),int32(Progression->PendingChoiceCount)):FString(TEXT("待选 —")));
        HudTint(Pick,Progression&&Progression->PendingChoiceCount>0?FLinearColor(.36f,.27f,.06f):FLinearColor(.045f,.12f,.15f));
    }
    if(ULDCommandButton* Sheet=Commands.FindRef(TEXT("Attributes"))){HudSetText(CastChecked<UTextBlock>(Sheet->GetContent()),TEXT("属性 [")+PC->BindingLabel(TEXT("Attributes"))+TEXT("]"));}

    ELDCombatEntry ArmedEntry=ELDCombatEntry::None;const bool bArmed=PC->ReadArmedCombatEntry(ArmedEntry);
    FString ArmedName;
    uint8 ControlBits=0;
    if(OwnCombat){for(const FLDCombatStatusView& Status:OwnCombat->Statuses){ControlBits|=Status.ControlBlocks;}}
    ActionTips.SetNum(ActionSlots.Num());
    for(int32 Index=0;Index<ActionSlots.Num();++Index)
    {
        const LDHUD::EActionSlot SlotKind=static_cast<LDHUD::EActionSlot>(Index);
        const ELDCombatEntry Wanted=HudActionEntries[Index];
        const FLDCombatEntryView* Source=OwnCombat?OwnCombat->Entries.FindByPredicate([Wanted](const FLDCombatEntryView& Candidate){return Candidate.Entry==Wanted;}):nullptr;
        LDHUD::FActionInput Input;Input.Slot=SlotKind;
        if(Source){Input=LDHUD::ActionInput(*Source,SlotKind,Content);Input.bControlled=(ControlBits&HudActionControl[Index])!=0;}
        LDHUD::FActionContext Context;Context.KeyLabel=PC->BindingLabel(HudActionBindings[Index]);Context.bOwnContext=bOwnMatch&&O.Life!=0;
        Context.bPlaying=Playing;Context.bAlive=bAlive;Context.bClock=bClock;Context.NowMs=NowMs;
        Context.bInputCaptured=Layer==ELDUIInputLayer::ChildConfirmation||Layer==ELDUIInputLayer::TextEditing;
        Context.bManaKnown=OwnCombat&&OwnCombat->bHasBody&&OwnCombat->MaxMPP2>0;Context.ManaP2=OwnCombat?OwnCombat->MPP2:0;
        const LDHUD::FActionView View=LDHUD::Action(Input,Context);
        FLDHUDActionSlot& Entry=ActionSlots[Index];
        HudSetText(Entry.Key,View.Key+TEXT(" · ")+View.Kind);HudSetText(Entry.Name,View.Name);HudSetText(Entry.State,View.State);
        Entry.Cooldown->SetPercent(View.CooldownFraction);
        const bool bThisArmed=bArmed&&ArmedEntry==Wanted;
        if(bThisArmed){ArmedName=View.Name;}
        Entry.Frame->SetBrushColor(bThisArmed?FLinearColor(.36f,.27f,.06f,.98f):View.ExecutionBlocks?FLinearColor(.05f,.06f,.08f,.96f):FLinearColor(.06f,.22f,.24f,.98f));
        Entry.Button->SetIsEnabled(View.bCanSubmitIntent);
        if(ActionTips[Index]!=View.Tooltip){ActionTips[Index]=View.Tooltip;Entry.Button->SetToolTipText(FText::FromString(View.Tooltip));}
    }
    FString ArmedLine;
    if(bArmed){ArmedLine=(ArmedName.IsEmpty()?LDHUD::EntryLabel(ArmedEntry):ArmedName)+TEXT(" 准备中：左键确认目标 · 右键或 ")+PC->BindingLabel(TEXT("Back"))+TEXT(" 取消");}
    else if(PC->IsAttackMoveArmed()){ArmedLine=TEXT("攻击移动：左键选择地点或目标 · 右键或 ")+PC->BindingLabel(TEXT("Back"))+TEXT(" 取消");}
    HudSetText(ArmedText,ArmedLine);HudSetShown(ArmedPanel,!ArmedLine.IsEmpty(),ESlateVisibility::HitTestInvisible);
    if(PC->CombatReceiptSerial!=SeenCombatReceipt)
    {
        SeenCombatReceipt=PC->CombatReceiptSerial;
        FLDCombatActionReceipt Receipt=PC->LatestCombatReceipt;
        if(Receipt.Generation==O.Match&&Receipt.Life==O.Life)
        {
            // Protection is lost once per life revision; later receipts of the same request repeat the flag.
            if(Receipt.bProtectionRemoved){if(ToldProtectionLifeRevision==Receipt.LifeRevision){Receipt.bProtectionRemoved=false;}else{ToldProtectionLifeRevision=Receipt.LifeRevision;}}
            const FString Notice=LDHUD::CombatReceiptNotice(Receipt);
            if(!Notice.IsEmpty()){ShowToast(Notice);}
        }
    }
    for(auto& Pair:Commands){Pair.Value->SetIsEnabled(Pair.Key!=TEXT("Stop")||bAlive);}
    if(ULDCommandButton* Stop=Commands.FindRef(TEXT("Stop"))){HudSetText(CastChecked<UTextBlock>(Stop->GetContent()),TEXT("停止 [")+PC->BindingLabel(TEXT("Stop"))+TEXT("]"));}
    HudSetText(CameraText,PC->Navigation->bFollowCamera?FString(TEXT("镜头：跟随")):TEXT("镜头：自由 · ")+PC->BindingLabel(TEXT("Follow"))+TEXT(" 跟随"));

    const ALDWorldViewRelay* Relay=PC->LocalView();
    const FLDObservedFrameRead Frame=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
    const bool bSightCurrent=Frame.Frame&&Frame.Status==ELDObservedViewStatus::Current;
    HudSetText(SightText,!bAlive?FString(TEXT("社死：仅显示静态地图与公开信息"))
        :bSightCurrent?FString::Printf(TEXT("视野：当前可见 %d 个单位"),Frame.Frame->Units.Num())
        :Frame.Status==ELDObservedViewStatus::Stale?FString(TEXT("视野：已过期，等待刷新")):FString(TEXT("视野：同步中")));

    FLDObservedUnitView Subject;const bool bSelected=PC->ReadInspected(Subject);
    HudSetShown(TargetCard,bSelected,ESlateVisibility::Visible);
    if(bSelected)
    {
        const FString SubjectName=Subject.Kind==ELDObservedUnitKind::Contender?LDHUD::RosterName(Lobby,Subject.ContenderId):FString();
        const LDHUD::FObservedCard Card=LDHUD::ObservedCard(Subject,SubjectName,Content,Frame.Frame?Frame.Frame->SampledAtServerSec:0.);
        HudSetText(TargetTitle,Card.Title);HudSetText(TargetGlyph,Card.Title.Left(1));
        TargetPortrait->SetBrushColor(Subject.Kind==ELDObservedUnitKind::Contender?LDHUD::PaletteColor(Subject.Presentation):FLinearColor(.45f,.32f,.12f));
        TargetHealthBar->SetPercent(Card.Health.Fraction);HudSetText(TargetHealth,Card.Health.Text);
        HudSetShown(TargetManaRow,Card.bMana,ESlateVisibility::SelfHitTestInvisible);
        if(Card.bMana){TargetManaBar->SetPercent(Card.Mana.Fraction);HudSetText(TargetMana,Card.Mana.Text);}
        HudSetText(TargetStatuses,Card.Statuses);
        HudSetShown(TargetEquipment,Card.bEquipment,ESlateVisibility::HitTestInvisible);
        if(Card.bEquipment){HudSetText(TargetEquipment,Card.Equipment);}
    }

    // Toast fade is local presentation time; it is never compared with an authority deadline.
    const double UiNow=GetWorld()->GetRealTimeSeconds();
    if(SeenIntent!=PC->IntentFeedback||SeenFeedback!=PC->FeedbackSerial()){SeenIntent=PC->IntentFeedback;SeenFeedback=PC->FeedbackSerial();ShowToast(PC->IntentFeedback);}
    HudSetText(Toast,UiNow-FeedbackAt<5?LastFeedback:FString());
    HudSetShown(ToastPanel,!Toast->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
}
void ULDHUD::ShowToast(const FString& Text)
{
    LastFeedback=Text;FeedbackAt=GetWorld()?GetWorld()->GetRealTimeSeconds():0;
}
void ULDHUD::HandleGrowthSubmit(const FLDSubmitResult& Result)
{
    if(Result.Family==ELDGrowthFamily::Choice&&ChoicesPanel){ChoicesPanel->HandleSubmit(Result);}
    else if(Result.Family==ELDGrowthFamily::Shop&&ShopPanel){ShopPanel->HandleSubmit(Result);}
}
void ULDHUD::HandleChoiceReceipt(const FLDChoiceReceiptQueryResult& Result){if(ChoicesPanel){ChoicesPanel->HandleReceipt(Result);}}
void ULDHUD::HandleShopReceipt(const FLDShopReceiptQueryResult& Result){if(ShopPanel){ShopPanel->HandleReceipt(Result);}}
void ULDHUD::HandleShopOpen(const FLDShopOpenResult& Result)
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());if(!PC){return;}
    FName ShopId;uint32 Life=0;
    // Only the player's current shop interaction may open the page: a late, replaced or foreign result never pops it.
    if(!PC->ReadShopInteraction(ShopId,Life)||Result.Generation!=PC->OwnerState.Match||Result.Life!=Life||Result.ShopId!=ShopId){return;}
    switch(Result.OpenStatus)
    {
    case ELDShopOpenStatus::Pending: ShowToast(TEXT("小卖部：等待开店结果…"));return;
    case ELDShopOpenStatus::Rejected:
    {
        const FString Why=LDHUD::GrowthReasonText(LDGrowthWire::Reason(Result.Reason));
        ShowToast(TEXT("小卖部没有打开：")+(Why.IsEmpty()?FString(TEXT("原因未知")):Why));return;
    }
    case ELDShopOpenStatus::Opened:
        if(!Result.bHasView||!Result.View.SessionId.IsValid())
        {
            UE_LOG(LogTemp,Error,TEXT("LD_HUD_SHOP_OPENED_WITHOUT_VIEW Shop %s opened without a session quote."),*Result.ShopId.ToString());
            ShowToast(TEXT("小卖部结果缺少报价，没有打开"));return;
        }
        OpenedShop=Result;OpenedShopPublish=PC->OwnerState.Growth.PublishRevision;bOpenedShop=true;
        if(!PC->RootUI||!PC->RootUI->ShowMainPanel(ELDUIMainPanel::Shop)){ShowToast(TEXT("小卖部已开门：关闭当前界面后再次右键小卖部即可打开"));}
        return;
    default:
        UE_LOG(LogTemp,Error,TEXT("LD_HUD_SHOP_OPEN_STATUS_INVALID Shop %s returned open status %d."),*Result.ShopId.ToString(),int32(Result.OpenStatus));
        return;
    }
}
void ULDHUD::RefreshKillFeed(const ALDGameState& Public,const FLDDemoContentDefinitions* Content,bool bClock,int64 NowMs)
{
    const FLDKillFeedView& Feed=Public.KillFeed;const double UiNow=GetWorld()->GetRealTimeSeconds();
    if(Feed.Generation!=KillGeneration){KillGeneration=Feed.Generation;KillSerial=0;KillLines.Reset();}
    if(Feed.Generation&&Feed.Generation==Public.PublicMatch.Generation)
    {
        // Camp causes name the approved creature of that tier; never a guessed or hidden attacker.
        FString Low=TEXT("低档野怪"),High=TEXT("高档野怪");
        if(Content){for(const FLDDemoMonsterDefinition& Monster:Content->Monsters){(Monster.Tier==ELDDemoCampTier::Low?Low:High)=LDHUD::ContentName(Content,Monster.Id);}}
        TArray<const FLDKillNotice*> Fresh;
        for(const FLDKillNotice& Notice:Feed.Notices){if(Notice.Serial>KillSerial){Fresh.Add(&Notice);}}
        Fresh.Sort([](const FLDKillNotice& A,const FLDKillNotice& B){return A.Serial<B.Serial;});
        for(const FLDKillNotice* Notice:Fresh)
        {
            KillSerial=Notice->Serial;
            // Notices already old when first seen (e.g. a late join) are marked seen without a banner.
            if(bClock&&NowMs-Notice->OccurredAtMs>6000){continue;}
            KillLines.Emplace(UiNow,LDHUD::KillLine(*Notice,LDHUD::RosterName(Public.DemoLobby,Notice->Victim),LDHUD::RosterName(Public.DemoLobby,Notice->Killer),Low,High));
        }
    }
    KillLines.RemoveAll([UiNow](const TPair<double,FString>& Line){return UiNow-Line.Key>=6;});
    while(KillLines.Num()>4){KillLines.RemoveAt(0);}
    FString Text;
    for(const TPair<double,FString>& Line:KillLines){Text+=(Text.IsEmpty()?TEXT(""):TEXT("\n"))+Line.Value;}
    HudSetText(KillFeedText,Text);HudSetShown(KillFeedPanel,!Text.IsEmpty(),ESlateVisibility::HitTestInvisible);
}
void ULDHUD::RefreshResult(ALDPlayerController& PC,const ALDGameState& Public)
{
    const FLDResultView& Result=Public.MatchResult;
    const bool bReady=Result.Generation==Public.PublicMatch.Generation&&Result.ResultId.IsValid()&&Result.ResultId==Public.PublicMatch.ResultId;
    ReturnButton->SetVisibility(PC.OwnerState.bLobbyHost?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ReturnButton->SetIsEnabled(bReady);
    if(!bReady)
    {
        // Without the frozen result there is nothing to show yet: no zero-filled table.
        HudSetText(ResultTitle,TEXT("对局结束"));HudSetText(ResultSubtitle,TEXT("结算同步中…"));
        if(ShownResultId.IsValid()){ResultRows->ClearChildren();ShownResultId.Invalidate();ShownResultSerial=0;}
        return;
    }
    if(ShownResultId==Result.ResultId&&ShownResultSerial==Result.ResultSerial){return;}
    ShownResultId=Result.ResultId;ShownResultSerial=Result.ResultSerial;
    const FLDResultRow* WinnerRow=Result.Winner.IsValid()?Result.Rows.FindByPredicate([&Result](const FLDResultRow& Row){return Row.ContenderId==Result.Winner;}):nullptr;
    HudSetText(ResultTitle,LDHUD::ResultHeadline(Result,WinnerRow?WinnerRow->Name:FString()));
    const int64 Seconds=FMath::Max<int64>(0,Result.DurationMs)/1000;
    HudSetText(ResultSubtitle,FString::Printf(TEXT("本局用时 %lld:%02lld · 统计只作展示，不决定胜负"),Seconds/60,Seconds%60));
    ResultRows->ClearChildren();
    const int32 Size=FMath::RoundToInt(16*(AppliedScale>0?AppliedScale:1.f));
    auto Cell=[this,Size](const FString& Value,float Width)
    {
        UTextBlock* Label=LDWidgets::Text(WidgetTree,Value,Size);Label->SetAutoWrapText(false);
        auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(Width);Box->SetContent(Label);return Box;
    };
    auto Row=[&](const FString& Name,const FString& Kills,const FString& Deaths,const FString& Level,const FString& Mark)
    {
        auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
        Line->AddChildToHorizontalBox(Cell(Name,280));Line->AddChildToHorizontalBox(Cell(Kills,80));Line->AddChildToHorizontalBox(Cell(Deaths,80));
        Line->AddChildToHorizontalBox(Cell(Level,100));Line->AddChildToHorizontalBox(Cell(Mark,80));
        ResultRows->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,4));
    };
    Row(TEXT("名称"),TEXT("击杀"),TEXT("社死"),TEXT("最终等级"),FString());
    for(const FLDResultRow& Entry:Result.Rows)
    {
        const LDHUD::FResultRowView View=LDHUD::ResultRow(Entry,Result.Winner,PC.OwnerState.ContenderId);
        Row(View.Name,View.Kills,View.Deaths,View.Level,View.bWinner?FString(TEXT("阴人王")):FString());
    }
}
int32 ULDHUD::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(!PC->CanWorldCommand()||!Band){return Base;}
    auto Lines=[&](TArray<FVector2D> Points,FLinearColor Color,float Thickness=2.f){FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Thickness);};
    auto Project=[&](FVector P,FVector2D& Out){FVector2D Screen;if(!PC->ProjectWorldLocationToScreen(P,Screen)){return false;}int32 W=0,H=0;PC->GetViewportSize(W,H);Out=Screen*G.GetLocalSize()/FVector2D(FMath::Max(1,W),FMath::Max(1,H));return true;};
    FLDObservedUnitView Subject;
    if(PC->ReadInspected(Subject))
    {
        FVector2D P;
        if(Project(FVector(Subject.FeetCm)+FVector(0,0,GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()),P))
        {Lines({P+FVector2D(-14,12),P+FVector2D(-20,12),P+FVector2D(-20,-30),P+FVector2D(20,-30),P+FVector2D(20,12),P+FVector2D(14,12)},FLinearColor(1,.78f,.3f));}
    }
    // Armed preview (CONTROLS 3): the own range ring around the feet, and for direction forms the direction from the
    // feet toward the cursor. Local presentation only; the server re-checks range, direction and target on confirm.
    ELDCombatEntry Armed=ELDCombatEntry::None;
    const bool bArmedEntry=PC->ReadArmedCombatEntry(Armed);
    const auto* Self=Cast<ALDCharacter>(PC->GetPawn());
    if(Self&&(bArmedEntry||PC->IsAttackMoveArmed()))
    {
        float RangeCm=0;bool bDirection=false;
        if(bArmedEntry)
        {
            const FLDCombatEntryView* Source=PC->CombatView.Entries.FindByPredicate([Armed](const FLDCombatEntryView& Candidate){return Candidate.Entry==Armed;});
            RangeCm=Source?float(Source->RangeCm):0.f;
            bDirection=Source&&(Source->TargetMode==uint8(ELDDemoTargetMode::DirectionProjectile)||Source->TargetMode==uint8(ELDDemoTargetMode::DirectionDash));
        }
        else{RangeCm=PC->CombatView.AttackRangeCm;}
        const FVector Feet=Self->GetActorLocation()-FVector(0,0,Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const FLinearColor Preview(1,.85f,.45f,.9f);
        if(RangeCm>0)
        {
            TArray<FVector2D> Ring;
            for(int32 Step=0;Step<=48;++Step)
            {
                const float Angle=2.f*PI*Step/48.f;FVector2D Point;
                if(!Project(Feet+FVector(FMath::Cos(Angle)*RangeCm,FMath::Sin(Angle)*RangeCm,0),Point)){Ring.Reset();break;}
                Ring.Add(Point);
            }
            if(Ring.Num()>1){Lines(Ring,Preview,1.5f);}
        }
        FVector Origin,Direction;
        if(bDirection&&PC->DeprojectMousePositionToWorld(Origin,Direction)&&FMath::Abs(Direction.Z)>KINDA_SMALL_NUMBER)
        {
            // The cursor ray meets the feet plane; only its planar direction is previewed, at the entry's range when known.
            const double Along=(Feet.Z-Origin.Z)/Direction.Z;
            FVector Planar=Origin+Direction*Along-Feet;Planar.Z=0;
            if(Along>0&&!Planar.IsNearlyZero())
            {
                const FVector End=Feet+(RangeCm>0?Planar.GetSafeNormal()*RangeCm:Planar);
                FVector2D From,To;
                if(Project(Feet,From)&&Project(End,To)){Lines({From,To},Preview,3.f);}
            }
        }
    }
    return Base+1;
}
