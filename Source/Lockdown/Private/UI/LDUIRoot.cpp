#include "UI/LDUIRoot.h"
#include "Development/LDV08Loadout.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameInstance.h"
#include "Character/LDCharacter.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/ScrollBox.h"
#include "Combat/LDTacticalCatalog.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Internationalization/Regex.h"
#include "Styling/CoreStyle.h"
#include "Vision/LDWorldViewRelay.h"
#include "Rendering/DrawElements.h"
#include "GameFramework/GameStateBase.h"
#include "Framework/LDGameMode.h"
#include "Objectives/LDObjectiveRules.h"
#include "Region/LDRegionDefinition.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "UI/LDDevelopmentPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Navigation/LDNavigationComponent.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDHUD.h"
#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"

bool ULDUIRoot::PointerOverUI() const
{
    if(const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->Navigation->Enabled()&&PC->HUDUI&&PC->HUDUI->PointerOverUI()){return true;}
    if(!FSlateApplication::IsInitialized()){return false;}
    const FVector2D PointerPosition=FSlateApplication::Get().GetCursorPos();
    for(const UWidget* W:{static_cast<UWidget*>(HeaderPanel.Get()),static_cast<UWidget*>(ActionPanel.Get()),static_cast<UWidget*>(MenuPanel.Get()),static_cast<UWidget*>(LobbyPanel.Get()),static_cast<UWidget*>(FixturePanel.Get())})
    {if(W&&W->IsVisible()&&W->GetCachedGeometry().IsUnderLocation(PointerPosition)){return true;}}
    if(const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->Navigation->Enabled()){return false;}
    const FVector2D Local=GetCachedGeometry().AbsoluteToLocal(PointerPosition);const FVector2D MapOrigin(GetCachedGeometry().GetLocalSize().X-270,180);
    return Local.X>=MapOrigin.X&&Local.X<=MapOrigin.X+240&&Local.Y>=MapOrigin.Y&&Local.Y<=MapOrigin.Y+160;
}

TSharedRef<SWidget> ULDUIRoot::RebuildWidget()
{
    if (!Status)
    {
        SetIsFocusable(true);
        const bool bFrontend = CastChecked<ALDPlayerController>(GetOwningPlayer())->IsFrontend();
        const auto* Authority=GetWorld()->GetAuthGameMode<ALDGameMode>();
        const bool bFixtureUI=GetOwningPlayer()->HasAuthority()&&Authority&&!Authority->IsDevelopmentProfile();
        UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
        WidgetTree->RootWidget = Canvas;
        auto Text = [this](const FString& Value, int32 Size)
        {
            UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
            Label->SetText(FText::FromString(Value));
            Label->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFontStyle("Regular", Size)));
            Label->SetColorAndOpacity(FLinearColor(.88f,.9f,.82f));
            Label->SetAutoWrapText(true);
            Label->SetVisibility(ESlateVisibility::HitTestInvisible);
            return Label;
        };
        UBorder* Header = WidgetTree->ConstructWidget<UBorder>();
        HeaderPanel=Header;
        Header->SetBrushColor(FLinearColor(.025f,.045f,.045f,.92f));
        Header->SetPadding(FMargin(20,12));
        Status = Text(TEXT("PROJECT LOCKDOWN"), 18);
        Header->SetContent(Status);
        UCanvasPanelSlot* HeaderSlot = Canvas->AddChildToCanvas(Header);
        HeaderSlot->SetAnchors(FAnchors(0,0,1,0));
        HeaderSlot->SetOffsets(FMargin(18,18,18,150));

        MenuPanel = WidgetTree->ConstructWidget<UBorder>();
        MenuPanel->SetBrushColor(FLinearColor(.025f,.045f,.045f,.98f));
        MenuPanel->SetPadding(FMargin(28));
        UVerticalBox* Menu = WidgetTree->ConstructWidget<UVerticalBox>();
        MenuPanel->SetContent(Menu);
        UCanvasPanelSlot* MenuSlot = Canvas->AddChildToCanvas(MenuPanel);
        MenuSlot->SetAnchors(FAnchors(.5f,.5f));
        MenuSlot->SetAlignment(FVector2D(.5,.5));
        MenuSlot->SetSize(FVector2D(560,620));
        auto Row = [Menu](UWidget* Widget)
        {
            UVerticalBoxSlot* Slot = Menu->AddChildToVerticalBox(Widget);
            Slot->SetPadding(FMargin(0,7));
        };
        Row(Text(TEXT("PROJECT LOCKDOWN"), 28));
        Row(Text(TEXT("Work, survive, recover a Seal and claim the Heart."), 16));
        auto Button = [this, &Text, &Row](const TCHAR* Label)
        {
            UButton* Result = WidgetTree->ConstructWidget<UButton>();
            FButtonStyle Style = Result->GetStyle();
            Style.Normal.TintColor = FSlateColor(FLinearColor(.055f,.12f,.12f));
            Style.Hovered.TintColor = FSlateColor(FLinearColor(.1f,.24f,.23f));
            Style.Pressed.TintColor = FSlateColor(FLinearColor(.035f,.08f,.08f));
            Style.NormalPadding = FMargin(16,10);
            Style.PressedPadding = FMargin(16,10);
            Result->SetStyle(Style);
            UTextBlock* Caption = Text(Label, 19);
            Caption->SetAutoWrapText(false);
            Result->SetContent(Caption);
            Row(Result);
            return Result;
        };
        if (bFrontend)
        {
            Button(TEXT("Host private lobby"))->OnClicked.AddDynamic(this, &ULDUIRoot::Host);
            Address = WidgetTree->ConstructWidget<UEditableTextBox>();
            Address->SetText(FText::FromString(TEXT("127.0.0.1:7777")));
            Address->SetHintText(FText::FromString(TEXT("Host address:port")));
            FEditableTextBoxStyle AddressStyle = Address->GetWidgetStyle();
            AddressStyle.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular",18));
            Address->SetWidgetStyle(AddressStyle);
            Address->SetForegroundColor(FLinearColor(.04f,.07f,.07f));
            Row(Address);
            Button(TEXT("Join address"))->OnClicked.AddDynamic(this, &ULDUIRoot::Join);
        }
        else
        {
            Button(TEXT("Resume"))->OnClicked.AddDynamic(this, &ULDUIRoot::Resume);
            Button(TEXT("Return to menu"))->OnClicked.AddDynamic(this, &ULDUIRoot::Leave);
            if (bFixtureUI) { Button(TEXT("Authority fixtures (setup only)"))->OnClicked.AddDynamic(this, &ULDUIRoot::Fixtures); }
        }
        MenuHelp=Text(TEXT("WASD move · Shift sprint · Mouse aim\nClick attack · Q assassination · Space dash\nE interact / hold · F ward · Wheel zoom\nEscape / H menu"),15); Row(MenuHelp);
        Feedback = Text(TEXT(""), 14);
        Row(Feedback);
        Button(TEXT("Exit"))->OnClicked.AddDynamic(this, &ULDUIRoot::Exit);
        bMenuOpen = bFrontend;
        MenuPanel->SetVisibility(bFrontend ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        if (!bFrontend)
        {
            LobbyPanel=WidgetTree->ConstructWidget<UBorder>();
            LobbyPanel->SetBrushColor(FLinearColor(.025f,.045f,.045f,.99f)); LobbyPanel->SetPadding(FMargin(24));
            auto* Choices=WidgetTree->ConstructWidget<UVerticalBox>(); LobbyPanel->SetContent(Choices);
            auto* LobbyCanvasSlot=Canvas->AddChildToCanvas(LobbyPanel); LobbyCanvasSlot->SetAnchors(FAnchors(.5f,.5f)); LobbyCanvasSlot->SetAlignment(FVector2D(.5)); LobbyCanvasSlot->SetSize(FVector2D(720,660));
            Choices->AddChildToVerticalBox(Text(TEXT("CHOOSE YOUR CHARACTER"),26));
            LobbyInfo=Text(TEXT("Choose a starting class, then Ready."),17); Choices->AddChildToVerticalBox(LobbyInfo);
            auto Choice=[&](const TCHAR* Label)
            {
                auto* Control=WidgetTree->ConstructWidget<UButton>();
                auto Style=Control->GetStyle(); Style.Normal.TintColor=FSlateColor(FLinearColor(.055f,.12f,.12f));
                Style.Hovered.TintColor=FSlateColor(FLinearColor(.1f,.24f,.23f)); Style.Pressed.TintColor=FSlateColor(FLinearColor(.035f,.08f,.08f));
                Style.Disabled.TintColor=FSlateColor(FLinearColor(.04f,.07f,.07f)); Style.NormalPadding=FMargin(12,8); Style.PressedPadding=FMargin(12,8); Control->SetStyle(Style);
                auto* Caption=Text(Label,17); Caption->SetAutoWrapText(false); Caption->SetJustification(ETextJustify::Center); Control->SetContent(Caption);
                Choices->AddChildToVerticalBox(Control)->SetPadding(FMargin(0,8)); return Control;
            };
            auto* StarterScroll=WidgetTree->ConstructWidget<UScrollBox>();auto* StarterSize=WidgetTree->ConstructWidget<USizeBox>();StarterSize->SetHeightOverride(300);StarterSize->SetContent(StarterScroll);Choices->AddChildToVerticalBox(StarterSize);
            auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
            for(uint8 N=0;N<3;++N){StarterScroll->AddChild(LDWidgets::Button(WidgetTree,FString(LDDevelopment::ClassName(N))+TEXT(" · retained economic skills"),[PC,N](){PC->ChooseLobbyClass(N);}));}
            NewStarters=WidgetTree->ConstructWidget<UVerticalBox>();StarterScroll->AddChild(NewStarters);
            if(const auto* C=ULDTacticalCatalog::Load(PC->OwnerState.Build.RulesVersion)){for(uint8 N=0;N<C->Starters.Num();++N){const auto& K=C->Starters[N];if(K.Skills.Num()!=2){NewStarters->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,K.Name+TEXT(" · unavailable until Frostline is implemented"),14));continue;}NewStarters->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,K.Name+TEXT(" · ")+LDDevelopment::SkillName(K.Skills[0])+TEXT(" + ")+LDDevelopment::SkillName(K.Skills[1]),[this,N](){CastChecked<ALDPlayerController>(GetOwningPlayer())->ChooseLobbyClass(N+3);}));}}
            Choice(TEXT("Toggle Ready"))->OnClicked.AddDynamic(this,&ULDUIRoot::Ready);
            LobbyStart=Choice(TEXT("Start match (host)")); LobbyStart->OnClicked.AddDynamic(this,&ULDUIRoot::StartMatch);
            Choices->AddChildToVerticalBox(Text(TEXT("1–4 humans · One is practice · Roster locks on start\nEach profile uses its own action bindings · presets grant no gear or gold."),14));
            LobbyPanel->SetVisibility(ESlateVisibility::Collapsed);
        }
        if(!bFrontend)
        {
            UBorder* ObjectivePanel=WidgetTree->ConstructWidget<UBorder>();ObjectivePanel->SetBrushColor(FLinearColor(.025f,.045f,.045f,.94f));ObjectivePanel->SetPadding(FMargin(16,10));
            ActionPanel=ObjectivePanel;
            UVerticalBox* Information=WidgetTree->ConstructWidget<UVerticalBox>();ObjectivePanel->SetContent(Information);
            ObjectiveInfo=Text(TEXT("E interacts with nearby public sites."),16);Information->AddChildToVerticalBox(ObjectiveInfo);
            ChannelProgress=WidgetTree->ConstructWidget<UProgressBar>();ChannelProgress->SetFillColorAndOpacity(FLinearColor(.75f,.65f,.22f));
            auto* ProgressSlot=Information->AddChildToVerticalBox(ChannelProgress);ProgressSlot->SetPadding(FMargin(0,6));
            ControlsInfo=Text(TEXT("WASD · Shift · Mouse aim · Click attack · Q assassination · Space dash · E hold · F ward · H / Esc help"),14);
            Information->AddChildToVerticalBox(ControlsInfo);
            SkillBar=Text(TEXT(""),16);Information->AddChildToVerticalBox(SkillBar);
            ObjectiveInfo->SetWrapTextAt(928);ControlsInfo->SetWrapTextAt(928);SkillBar->SetWrapTextAt(928);
            auto* ObjectiveSize=WidgetTree->ConstructWidget<USizeBox>();ObjectiveSize->SetWidthOverride(960);ObjectiveSize->SetContent(ObjectivePanel);
            auto* ObjectiveSlot=Canvas->AddChildToCanvas(ObjectiveSize);ObjectiveSlot->SetAnchors(FAnchors(0,1));ObjectiveSlot->SetAlignment(FVector2D(0,1));ObjectiveSlot->SetPosition(FVector2D(18,-18));ObjectiveSlot->SetAutoSize(true);
            // HUD panels do not capture clicks intended for the game world.
            ObjectivePanel->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        if(!bFrontend&&bFixtureUI)
        {
            FixturePanel=WidgetTree->ConstructWidget<UBorder>();FixturePanel->SetBrushColor(FLinearColor(.025f,.045f,.045f,.99f));FixturePanel->SetPadding(FMargin(28));
            UVerticalBox* Items=WidgetTree->ConstructWidget<UVerticalBox>();FixturePanel->SetContent(Items);
            auto* FixtureSlot=Canvas->AddChildToCanvas(FixturePanel);FixtureSlot->SetAnchors(FAnchors(.5f,.5f));FixtureSlot->SetAlignment(FVector2D(.5));FixtureSlot->SetSize(FVector2D(560,650));
            Items->AddChildToVerticalBox(Text(TEXT("AUTHORITY FIXTURES"),24));
            Items->AddChildToVerticalBox(Text(TEXT("Setup only. Tower combat and Seal crafting are deferred. Pickup, ownership, theft and claims are real."),15));
            auto FixtureButton=[&](const TCHAR* Caption,UTextBlock** OutLabel=nullptr)
            {
                auto* Control=WidgetTree->ConstructWidget<UButton>();auto Style=Control->GetStyle();Style.Normal.TintColor=FSlateColor(FLinearColor(.055f,.12f,.12f));Style.Hovered.TintColor=FSlateColor(FLinearColor(.1f,.24f,.23f));Style.NormalPadding=FMargin(12,9);Control->SetStyle(Style);
                auto* Label=Text(Caption,17);Control->SetContent(Label);Items->AddChildToVerticalBox(Control)->SetPadding(FMargin(0,6));if(OutLabel){*OutLabel=Label;}return Control;
            };
            FixtureButton(TEXT("Toggle shared public sensor"))->OnClicked.AddDynamic(this,&ULDUIRoot::ToggleSensor);
            UTextBlock* Label=nullptr;
            FixtureButton(TEXT("West defence"),&Label)->OnClicked.AddDynamic(this,&ULDUIRoot::DefenceWest);DefenceLabels.Add(Label);
            FixtureButton(TEXT("East defence"),&Label)->OnClicked.AddDynamic(this,&ULDUIRoot::DefenceEast);DefenceLabels.Add(Label);
            FixtureButton(TEXT("South defence"),&Label)->OnClicked.AddDynamic(this,&ULDUIRoot::DefenceSouth);DefenceLabels.Add(Label);
            FixtureButton(TEXT("Central defence"),&Label)->OnClicked.AddDynamic(this,&ULDUIRoot::DefenceCentral);DefenceLabels.Add(Label);
            FixtureButton(TEXT("Seed one dropped Seal at Guild"))->OnClicked.AddDynamic(this,&ULDUIRoot::SeedSeal);
            FixtureButton(TEXT("Back"))->OnClicked.AddDynamic(this,&ULDUIRoot::FixtureBack);
            FixturePanel->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    return Super::RebuildWidget();
}

void ULDUIRoot::NativeTick(const FGeometry& Geometry, float Delta)
{
    Super::NativeTick(Geometry, Delta);
    if(auto* InputOwner=Cast<ALDPlayerController>(GetOwningPlayer());InputOwner&&(InputOwner->IsFrontend()||InputOwner->Navigation->Enabled()))
    {MenuHelp->SetText(FText::FromString(InputOwner->ControlSummary()));}
    if (const ULDGameInstance* Instance = GetGameInstance<ULDGameInstance>())
    {
        if (!Instance->ConnectionMessage.IsEmpty()) { Feedback->SetText(FText::FromString(Instance->ConnectionMessage)); }
    }
    if (const ALDPlayerController* PC = Cast<ALDPlayerController>(GetOwningPlayer());PC&&!PC->IsFrontend())
    {
        const ALDCharacter* Body=Cast<ALDCharacter>(PC->GetPawn());
        const auto& O=PC->OwnerState;
        const auto* GS=GetWorld()->GetGameState<ALDGameState>();
        const bool Lobby=GS&&GS->bDevelopmentProfile&&!GS->bMatchStarted;
        if (Lobby!=bInLobby && LobbyPanel)
        {
            bInLobby=Lobby;
            LobbyPanel->SetVisibility(Lobby?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
            HeaderPanel->SetVisibility(Lobby?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
            if (ActionPanel) { ActionPanel->SetVisibility(Lobby?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible); }
            if (Lobby) { MenuPanel->SetVisibility(ESlateVisibility::Collapsed); bMenuOpen=false; }
            const_cast<ALDPlayerController*>(PC)->SetMenuOpen(Lobby);
        }
        if (Lobby && LobbyInfo)
        {
            const auto& B=O.Build;
            if(NewStarters){NewStarters->SetVisibility(O.Build.RulesVersion>=7?ESlateVisibility::Visible:ESlateVisibility::Collapsed);if(SeenStarterRules!=O.Build.RulesVersion){SeenStarterRules=O.Build.RulesVersion;NewStarters->ClearChildren();if(const auto* C=ULDTacticalCatalog::Load(O.Build.RulesVersion)){for(uint8 N=0;N<C->Starters.Num();++N){const auto& K=C->Starters[N];if(K.Skills.Num()!=2){NewStarters->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,K.Name+TEXT(" · unavailable until Frostline is implemented"),14));continue;}NewStarters->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,K.Name+TEXT(" · ")+LDDevelopment::SkillName(K.Skills[0])+TEXT(" + ")+LDDevelopment::SkillName(K.Skills[1]),[this,N](){CastChecked<ALDPlayerController>(GetOwningPlayer())->ChooseLobbyClass(N+3);}));}}}}

            LobbyInfo->SetText(FText::FromString(FString::Printf(TEXT("Your class: %s · %s\nSTR %d  DEX %d  CON %d  INT %d  WIS %d  CHA %d\nReady %d / %d · Your choice is private\n%s"),
                LDDevelopment::PresetName(B),O.bReady?TEXT("READY"):TEXT("Not ready"),B.Attributes[0],B.Attributes[1],B.Attributes[2],B.Attributes[3],B.Attributes[4],B.Attributes[5],GS->ReadyCount,GS->LobbyCount,*PC->IntentFeedback)));
            LobbyStart->SetIsEnabled(O.bLobbyHost&&GS->ReadyCount==GS->LobbyCount&&GS->LobbyCount>0);
            LobbyStart->SetVisibility(O.bLobbyHost?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        }
        if(!Lobby)
        {
            const bool Overlay=PC->DevelopmentUI&&PC->DevelopmentUI->IsOpen();HeaderPanel->SetVisibility(Overlay?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
            if(ActionPanel){ActionPanel->SetVisibility(Overlay?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}
        }
        if(PC->Navigation->Enabled())
        {
            HeaderPanel->SetVisibility(ESlateVisibility::Collapsed);if(ActionPanel){ActionPanel->SetVisibility(ESlateVisibility::Collapsed);}
            if(!PC->IntentFeedback.IsEmpty()){Feedback->SetText(FText::FromString(PC->IntentFeedback));}
            return;
        }
        const double Now=GetWorld()->GetGameState()?GetWorld()->GetGameState()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
        const ALDWorldViewRelay* Relay=PC->LocalView();
        const FLDWorldView View=Relay ? Relay->LastView() : FLDWorldView();
        FString Life=O.bActive?FString::Printf(TEXT("HP %.2f/%.0f · Speed %.0f · Ward %d"),O.Hp,O.MaxHp,Body?Body->GetVelocity().Size2D():0,O.WardCharges)
            :(GS&&GS->EndReason?FString::Printf(TEXT("DEAD · Lost %lld gold · Demo finished"),O.LastLostGold):FString::Printf(TEXT("DEAD · Lost %lld gold · Return in %.1fs · Owned wards and shared sight only"),O.LastLostGold,FMath::Max(0.,O.ReturnAt-Now)));
        FString Danger=Now<O.WarningUntil?TEXT("DANGER: INCOMING CAST · Turn, escape, Step or deal damage!")
            :(Now<O.ProtectedUntil?FString::Printf(TEXT("Return protection %.1fs · Gameplay actions locked"),O.ProtectedUntil-Now)
                :(Now<O.DamageUntil?FString::Printf(TEXT("HIT · Anonymous incoming sector %d/8 (world +X=0, toward +Y)"),O.IncomingSector):PC->IntentFeedback));
        if(Now<O.AlertUntil){Danger+=FString::Printf(TEXT(" · Purse Alert %.1fs"),O.AlertUntil-Now);}
        Status->SetColorAndOpacity(Now<O.WarningUntil?FLinearColor(1,.35f,.2f):FLinearColor(.88f,.9f,.82f));
        FString Public=TEXT("Heart unavailable");
        if(GS)
        {
            const TCHAR* Route=GS->bUIControlProfile?TEXT("Public Charter"):GS->ClaimRoute==LDObjective::Assault?TEXT("Assault"):(GS->ClaimRoute==LDObjective::SealRoute?TEXT("Seal"):TEXT("Mixed"));
            Public=GS->EndReason?(GS->EndReason==2?TEXT("No claim · Demo finished"):FString::Printf(TEXT("Heart claimed · Route %s · Demo finished"),GS->WinningRoute==5?TEXT("Keys"):GS->WinningRoute==6?TEXT("Heartbreaker"):GS->WinningRoute==2?TEXT("Siege"):GS->bUIControlProfile?TEXT("Public Charter"):GS->WinningRoute==LDObjective::SealRoute?TEXT("Seal"):TEXT("Assault")))
                :(GS->bClaimActive?FString::Printf(TEXT("Heart %s · %s · %.1f / 10s"),GS->bContested?TEXT("CONTESTED"):TEXT("claim active"),Route,GS->ClaimProgress)
                    :FString::Printf(TEXT("Heart %s · Defences W:%d E:%d S:%d C:%d · Time left %.0fs"),GS->DefensiveFlags?TEXT("locked"):TEXT("ordinary access open"),!!(GS->DefensiveFlags&1),!!(GS->DefensiveFlags&2),!!(GS->DefensiveFlags&4),!!(GS->DefensiveFlags&8),FMath::Max(0.,GS->MatchDeadline-Now)));
            if(GS->SealNoticeSerial){Public+=FString::Printf(TEXT(" · Seal %s: %s"),GS->SealNoticeKind==1?TEXT("fixture seed"):GS->SealNoticeKind==2?TEXT("dropped"):TEXT("district"),*LDRegion::DistrictName(GetWorld(),GS->SealNoticeDistrict));}
            const TCHAR* Names[]={TEXT("West"),TEXT("East"),TEXT("South"),TEXT("Central")};
            for(int32 I=0;I<DefenceLabels.Num();++I){DefenceLabels[I]->SetText(FText::FromString(FString::Printf(TEXT("%s defence: %s (fixture toggle)"),Names[I],GS->DefensiveFlags&(1<<I)?TEXT("ACTIVE"):TEXT("CLEARED"))));}
        }
        Status->SetText(FText::FromString(FString::Printf(TEXT("LOCKDOWN · MIGRATION DEMO · %s\nGold %lld · XP %lld (level %d) · Seal %s · Sight %s: body %d / ward %d / shared %d\nAttack %.1fs · Q %.1fs · Dash %.1fs · %s\n%s"),
            *Life,O.Gold,O.Xp,O.bDevelopmentProfile?O.Build.Level:LDRules::LevelFromXp(O.Xp),O.SealId.IsValid()?TEXT("carried"):TEXT("none"),Relay&&!Relay->IsStale()?TEXT("fresh"):TEXT("STALE"),View.BodySources,View.WardSources,View.PublicSources,
            FMath::Max(0.,O.BasicReadyAt-Now),FMath::Max(0.,O.AssassinReadyAt-Now),FMath::Max(0.,O.DashReadyAt-Now),*Public,*Danger)));
        if (O.bDevelopmentProfile && !Lobby)
        {
            if(LDRegion::Definition(GetWorld())&&GS&&!GS->bClaimActive&&!GS->EndReason){Public=FString::Printf(TEXT("Heart locked · Conquest deferred · %.0fs · Watch %s"),FMath::Max(0.,GS->MatchDeadline-Now),GS->bPublicSensorActive?TEXT("ON"):*FString::Printf(TEXT("OFF %.0fs"),FMath::Max(0.,GS->PublicSensorOfflineUntil-Now)));}
            const auto& B=O.Build;
            Status->SetText(FText::FromString(FString::Printf(TEXT("LOCKDOWN · %s · %s\n%s · Level %d · XP %lld / 420 · Gold %lld · Attribute points %d · Skill points %d\nSTR %d  DEX %d  CON %d  INT %d  WIS %d  CHA %d · Basic %.2f damage\n%s · %s"),
                PC->Navigation->Enabled()?TEXT("v0.5 INPUT CHECKPOINT"):TEXT("v0.4 DEVELOPMENT"),*Life,LDDevelopment::PresetName(B),B.Level,O.Xp,O.Gold,B.AttributePoints,B.SkillPoints,B.Attributes[0],B.Attributes[1],B.Attributes[2],B.Attributes[3],B.Attributes[4],B.Attributes[5],LDDevelopment::BasicDamage(B),*Public,*Danger)));
        }
        if (O.bDevelopmentProfile)
        {
            const FText Controls=FText::FromString(PC->Navigation->Enabled()?PC->ControlSummary():FString(TEXT("WASD · Shift · Click attack · 1–4 skills (hold utility) · Q/Space equipped aliases · P build/books · E work/loot · F ward · H help")));
            if (ControlsInfo) { ControlsInfo->SetText(Controls); } MenuHelp->SetText(Controls);
            FString Slots;
            for(int32 I=0;I<LDDevelopment::SlotCount;++I)
            {
                const ELDSkill Skill=O.Build.Slots[I];const double Left=O.Build.Cooldowns[uint8(Skill)]-Now;
                const FString Rank=Skill==ELDSkill::None?TEXT(""):FString::Printf(TEXT(" R%d"),LDDevelopment::Rank(O.Build,Skill));
                const FString Ready=Skill==ELDSkill::None?TEXT(""):(Left>0?FString::Printf(TEXT(" %.1fs"),Left):(uint8(Skill)>=4?TEXT(" hold"):TEXT(" press")));
                const FString SlotKey=PC->Navigation->Enabled()?PC->BindingLabel(FName(*FString::Printf(TEXT("Skill%d"),I+1))):FString::FromInt(I+1);
                Slots+=FString::Printf(TEXT("[%s] %s%s%s   "),*SlotKey,LDDevelopment::SkillName(Skill),*Rank,*Ready);
            }
            if(SkillBar){SkillBar->SetText(FText::FromString(Slots));}
        }
        if(ObjectiveInfo)
        {
            FString Stage=O.ContractStage==1?TEXT("Collect at West station [W]"):O.ContractStage==2?TEXT("Deliver at Guild [G]"):TEXT("No active contract");
            if(const auto* D=LDRegion::Definition(GetWorld())){if(O.ContractStage==1){const auto* N=D->Node(O.ContractDestination);Stage=FString::Printf(TEXT("Collect at %s"),N?*N->Name:TEXT("assigned town"));}Stage+=FString::Printf(TEXT(" · Paid %d/8"),O.PaidJobs);}
            FString Progress=O.ChannelKind?FString::Printf(TEXT("%s %.1f / %.0fs confirmed · Release or move to cancel; only hard control / displacement cancels a Heart claim."),O.ChannelKind==1?TEXT("Collecting"):O.ChannelKind==2?TEXT("Delivering"):O.ChannelKind==4?TEXT("Fabricating"):O.ChannelKind==5?TEXT("Activating / key pickup"):O.ChannelKind==6?TEXT("Recovering"):TEXT("Claiming"),O.ChannelProgress,(O.ChannelDuration>0?O.ChannelDuration:LDObjective::Duration(O.ChannelKind))):TEXT("Hold progress resets on interruption.");
            if(GS&&GS->bContested&&O.ChannelKind==3){Progress=TEXT("CONTESTED · Claim progress reset to zero.");}
            FString Craft=O.bDevelopmentProfile&&LDRegion::Definition(GetWorld())?FString::Printf(TEXT("\nGuild: E courier · Shift+E Seal (4s; 4 credits, 100 gold, Witness) · %s"),O.FabricationReceipt.IsValid()?TEXT("craft used"):O.SealId.IsValid()?TEXT("slot full"):LDObjective::CanFabricate(O.Witness.IsValid(),O.Commissions,O.Gold,false,false)?TEXT("ready"):TEXT("requirements missing")):TEXT("");
            if(PC->Navigation->Enabled()){Progress.ReplaceInline(TEXT("S stops"),*(TEXT("Release ")+PC->BindingLabel(TEXT("Context"))));Craft.ReplaceInline(TEXT("Shift+E Seal"),TEXT("Character panel: Arm Seal"));Craft.ReplaceInline(TEXT("E courier"),*(PC->BindingLabel(TEXT("Context"))+TEXT(" courier")));}
            ObjectiveInfo->SetText(FText::FromString(FString::Printf(TEXT("%s\nCourier: %s · Credits %d · Witness: %s%s\n%s"),*PC->ContextPrompt(),*Stage,O.Commissions,O.Witness.IsValid()?TEXT("recorded"):TEXT("none"),*Craft,*Progress)));
            if(GS&&GS->bUIControlProfile){ObjectiveInfo->SetText(FText::FromString(PC->ContextPrompt()+TEXT("\n")+Stage+TEXT("\n")+Progress));}
            if(O.ActiveSkill!=ELDSkill::None&&(O.ActionCue&15)==7)
            {Progress=FString::Printf(TEXT("Holding %s · %.1fs remaining · Release, move or damage cancels."),LDDevelopment::SkillName(O.ActiveSkill),FMath::Max(0.,O.SkillEndsAt-Now));ObjectiveInfo->SetText(FText::FromString(Progress));}
            ChannelProgress->SetPercent((O.ActionCue&15)==7?float(FMath::Clamp(1.-(O.SkillEndsAt-Now)/LDDevelopment::ChannelSeconds(O.ActiveSkill),0.,1.)):(O.ChannelKind?FMath::Clamp(O.ChannelProgress/float((O.ChannelDuration>0?O.ChannelDuration:LDObjective::Duration(O.ChannelKind))),0.f,1.f):0));
        }
        if (!PC->IntentFeedback.IsEmpty()) { Feedback->SetText(FText::FromString(PC->IntentFeedback)); }
    }
}

int32 ULDUIRoot::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    const int32 Result=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
    const ALDPlayerController* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(PC&&PC->Navigation->Enabled()){return Result;}
    const ALDWorldViewRelay* Relay=PC ? PC->LocalView() : nullptr;
    if (!Relay || PC->IsFrontend() || bInLobby || (PC->DevelopmentUI&&PC->DevelopmentUI->IsOpen())) { return Result; }
    const FLDWorldView& View=Relay->LastView();
    const FVector2D Origin(Geometry.GetLocalSize().X-270,180);
    auto MapPoint=[&](const FVector& P){return Origin+FVector2D((P.X-View.Grid.Min.X)/View.Grid.CellSize*4,(40-(P.Y-View.Grid.Min.Y)/View.Grid.CellSize)*4);};
    auto Box=[&](FVector2D At,FVector2D Size,FLinearColor Color)
    { FSlateDrawElement::MakeBox(Elements,Result+1,Geometry.ToPaintGeometry(Size,FSlateLayoutTransform(At)),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,Color); };
    for (int32 I=0;I<LDVision::Cells;++I)
    {
        const bool Current=!Relay->IsStale() && LDVision::Bit(View.Current,I);
        Box(Origin+FVector2D((I%60)*4,(39-I/60)*4),FVector2D(4),Current ? FLinearColor(.42f,.55f,.5f,1) : (LDVision::Bit(View.Explored,I) ? FLinearColor(.12f,.17f,.16f,1) : FLinearColor(.008f,.012f,.012f,1)));
    }
    for (const auto& Entity : View.Entities)
    { Box(MapPoint(Entity.Position)-FVector2D(2),FVector2D(4),Entity.Kind==ELDObservedKind::Seal?FLinearColor(1,.6f,.1f):FLinearColor(.9f,.9f,.7f)); }
    for(uint8 Site:{uint8(1),uint8(2),uint8(3),uint8(6),uint8(7),uint8(8)})
    {if(Site>3&&!LDRegion::Definition(GetWorld())){continue;}const FVector P=LDRegion::Site(GetWorld(),Site);const FVector2D At=MapPoint(P);Box(At-FVector2D(3),FVector2D(6),FLinearColor(.25f,.7f,.9f));FSlateDrawElement::MakeText(Elements,Result+2,Geometry.ToPaintGeometry(FVector2D(30,16),FSlateLayoutTransform(At+FVector2D(4,-8))),FString(Site==1?TEXT("G"):Site==2?TEXT("W"):Site==3?TEXT("H"):Site==6?TEXT("E"):Site==7?TEXT("T"):TEXT("X")),FCoreStyle::GetDefaultFontStyle("Bold",12),ESlateDrawEffect::None,FLinearColor::White);}
    if (const APawn* Pawn=PC->GetPawn()) { const FVector P=Pawn->GetActorLocation(); Box(MapPoint(P)-FVector2D(3),FVector2D(6),FLinearColor::White); }
    return Result+2;
}
void ULDUIRoot::ToggleSensor() { CastChecked<ALDPlayerController>(GetOwningPlayer())->TogglePublicSensorFixture(); }
void ULDUIRoot::Fixtures(){if(FixturePanel){MenuPanel->SetVisibility(ESlateVisibility::Collapsed);FixturePanel->SetVisibility(ESlateVisibility::Visible);}}
void ULDUIRoot::FixtureBack(){if(FixturePanel){FixturePanel->SetVisibility(ESlateVisibility::Collapsed);MenuPanel->SetVisibility(ESlateVisibility::Visible);}}
void ULDUIRoot::SeedSeal(){CastChecked<ALDPlayerController>(GetOwningPlayer())->SeedSealFixture();}
void ULDUIRoot::DefenceWest(){CastChecked<ALDPlayerController>(GetOwningPlayer())->ToggleDefenceFixture(0);}
void ULDUIRoot::DefenceEast(){CastChecked<ALDPlayerController>(GetOwningPlayer())->ToggleDefenceFixture(1);}
void ULDUIRoot::DefenceSouth(){CastChecked<ALDPlayerController>(GetOwningPlayer())->ToggleDefenceFixture(2);}
void ULDUIRoot::DefenceCentral(){CastChecked<ALDPlayerController>(GetOwningPlayer())->ToggleDefenceFixture(3);}

void ULDUIRoot::ToggleMenu()
{
    if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->InventoryUI&&PC->InventoryUI->IsOpen()){PC->InventoryUI->Close();return;}
    if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->ControlsUI&&PC->ControlsUI->IsOpen()){PC->ControlsUI->Close();return;}
    if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->DevelopmentUI&&PC->DevelopmentUI->IsOpen()){PC->DevelopmentUI->Close();return;}
    if (bInLobby) { return; }
    if (CastChecked<ALDPlayerController>(GetOwningPlayer())->IsFrontend()) { return; }
    if(FixturePanel&&FixturePanel->GetVisibility()==ESlateVisibility::Visible){FixtureBack();return;}
    bMenuOpen = !bMenuOpen;
    MenuPanel->SetVisibility(bMenuOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    CastChecked<ALDPlayerController>(GetOwningPlayer())->SetMenuOpen(bMenuOpen);
}
FReply ULDUIRoot::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape)
    {if(!Event.IsRepeat()){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(PC->Navigation->Enabled()||PC->IsFrontend()){PC->V05Back();}else{ToggleMenu();}}return FReply::Handled();}
    return Super::NativeOnKeyDown(Geometry, Event);
}
void ULDUIRoot::DismissMenu(){if(!CastChecked<ALDPlayerController>(GetOwningPlayer())->IsFrontend()){bMenuOpen=false;MenuPanel->SetVisibility(ESlateVisibility::Collapsed);if(FixturePanel){FixturePanel->SetVisibility(ESlateVisibility::Collapsed);}}}
void ULDUIRoot::Host()
{
    GetGameInstance<ULDGameInstance>()->ConnectionMessage.Reset();
    UGameplayStatics::OpenLevel(this, TEXT("/Game/Lockdown/Generated/Maps/L_DevelopmentRegion"), true, TEXT("listen?Profile=V05"));
}
void ULDUIRoot::Join()
{
    GetGameInstance<ULDGameInstance>()->ConnectionMessage.Reset();
    const FString Destination = Address->GetText().ToString().TrimStartAndEnd();
    const FRegexPattern Pattern(TEXT("^[A-Za-z0-9.-]+(:[0-9]{1,5})?$"));
    FRegexMatcher Matcher(Pattern, Destination);
    if (!Matcher.FindNext()) { Feedback->SetText(FText::FromString(TEXT("Enter a host name or IPv4 address, optionally followed by :port."))); return; }
    FString HostName, Port;
    if (Destination.Split(TEXT(":"), &HostName, &Port) && (FCString::Atoi(*Port) < 1 || FCString::Atoi(*Port) > 65535))
    {
        Feedback->SetText(FText::FromString(TEXT("Port must be between 1 and 65535.")));
        return;
    }
    Feedback->SetText(FText::FromString(TEXT("Connecting...")));
    GetOwningPlayer()->ClientTravel(Destination, TRAVEL_Absolute);
}
void ULDUIRoot::Resume() { if (bMenuOpen) { ToggleMenu(); } }
void ULDUIRoot::Leave() { UGameplayStatics::OpenLevel(this, TEXT("/Game/Lockdown/Generated/Maps/L_Frontend")); }
void ULDUIRoot::Exit() { UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false); }
void ULDUIRoot::Laborer() { CastChecked<ALDPlayerController>(GetOwningPlayer())->ChooseLobbyClass(0); }
void ULDUIRoot::Rogue() { CastChecked<ALDPlayerController>(GetOwningPlayer())->ChooseLobbyClass(1); }
void ULDUIRoot::Scholar() { CastChecked<ALDPlayerController>(GetOwningPlayer())->ChooseLobbyClass(2); }
void ULDUIRoot::Ready() { CastChecked<ALDPlayerController>(GetOwningPlayer())->ToggleLobbyReady(); }
void ULDUIRoot::StartMatch() { CastChecked<ALDPlayerController>(GetOwningPlayer())->StartLobby(); }
