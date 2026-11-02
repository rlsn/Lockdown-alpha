#include "UI/LDUIRoot.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDDemoEntry.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/ScrollBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Internationalization/Regex.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"
#include "Framework/LDGameMode.h"
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
    for(const UWidget* W:{static_cast<UWidget*>(HeaderPanel.Get()),static_cast<UWidget*>(MenuPanel.Get()),static_cast<UWidget*>(LobbyPanel.Get())})
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
        auto* MenuScroll = WidgetTree->ConstructWidget<UScrollBox>();
        MenuScroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll);
        MenuScroll->AddChild(Menu);
        MenuPanel->SetContent(MenuScroll);
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
        Row(Text(TEXT("Fight, grow your build, and hold the capture point."), 16));
        FString Version, VersionError;
        Row(Text(LDDemoEntry::ReadProductVersion(Version, VersionError) ? TEXT("Version ") + Version : VersionError, 14));
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
        }
        MenuHelp=Text(TEXT("Listen Host with up to two human guests and three independent AI opponents. Direct IP connections require a reachable Host."),15); Row(MenuHelp);
        Feedback = Text(TEXT(""), 14);
        Row(Feedback);
        Row(LDWidgets::Button(WidgetTree, TEXT("Settings"),
            [this]() { CastChecked<ALDPlayerController>(GetOwningPlayer())->ToggleControlsPanel(); }));
        Button(TEXT("Exit"))->OnClicked.AddDynamic(this, &ULDUIRoot::Exit);
        bMenuOpen = bFrontend;
        MenuPanel->SetVisibility(bFrontend ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        if (!bFrontend)
        {
            LobbyPanel=WidgetTree->ConstructWidget<UBorder>();
            LobbyPanel->SetBrushColor(FLinearColor(.025f,.045f,.045f,.99f)); LobbyPanel->SetPadding(FMargin(24));
            auto* Choices=WidgetTree->ConstructWidget<UVerticalBox>(); LobbyPanel->SetContent(Choices);
            auto* LobbyCanvasSlot=Canvas->AddChildToCanvas(LobbyPanel); LobbyCanvasSlot->SetAnchors(FAnchors(.5f,.5f)); LobbyCanvasSlot->SetAlignment(FVector2D(.5)); LobbyCanvasSlot->SetSize(FVector2D(720,660));
            Choices->AddChildToVerticalBox(Text(TEXT("DEMO LOBBY"),26));
            LobbyInfo=Text(TEXT("Waiting for the authoritative room roster..."),17); Choices->AddChildToVerticalBox(LobbyInfo);
            auto Choice=[&](const TCHAR* Label)
            {
                auto* Control=WidgetTree->ConstructWidget<UButton>();
                auto Style=Control->GetStyle(); Style.Normal.TintColor=FSlateColor(FLinearColor(.055f,.12f,.12f));
                Style.Hovered.TintColor=FSlateColor(FLinearColor(.1f,.24f,.23f)); Style.Pressed.TintColor=FSlateColor(FLinearColor(.035f,.08f,.08f));
                Style.Disabled.TintColor=FSlateColor(FLinearColor(.04f,.07f,.07f)); Style.NormalPadding=FMargin(12,8); Style.PressedPadding=FMargin(12,8); Control->SetStyle(Style);
                auto* Caption=Text(Label,17); Caption->SetAutoWrapText(false); Caption->SetJustification(ETextJustify::Center); Control->SetContent(Caption);
                Choices->AddChildToVerticalBox(Control)->SetPadding(FMargin(0,8)); return Control;
            };
            auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
            AIControls=WidgetTree->ConstructWidget<UVerticalBox>();Choices->AddChildToVerticalBox(AIControls);
            for(uint8 Count=0;Count<=3;++Count)
            {
                AIControls->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,
                    FString::Printf(TEXT("Set AI opponents: %u"), Count), [PC,Count](){PC->SetDemoAICount(Count);}));
            }
            Choice(TEXT("Toggle Ready"))->OnClicked.AddDynamic(this,&ULDUIRoot::Ready);
            LobbyStart=Choice(TEXT("Start match (host)")); LobbyStart->OnClicked.AddDynamic(this,&ULDUIRoot::StartMatch);
            Choices->AddChildToVerticalBox(Text(TEXT("Up to three humans, including the Host. Empty slots stay empty. Roster changes clear human readiness."),14));
            LobbyPanel->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    return Super::RebuildWidget();
}

void ULDUIRoot::NativeTick(const FGeometry& Geometry, float Delta)
{
    Super::NativeTick(Geometry, Delta);
    if(auto* InputOwner=Cast<ALDPlayerController>(GetOwningPlayer());InputOwner&&!InputOwner->IsFrontend()&&InputOwner->Navigation->Enabled())
    {MenuHelp->SetText(FText::FromString(InputOwner->ControlSummary()));}
    if (const ULDGameInstance* Instance = GetGameInstance<ULDGameInstance>())
    {
        if (!Instance->ConnectionMessage.IsEmpty()) { Feedback->SetText(FText::FromString(Instance->ConnectionMessage)); }
        else if (Instance->GetDisplaySettingsResult().Status == ELDDisplaySettingsStatus::RestoreFailed
            || Instance->GetDisplaySettingsResult().Status == ELDDisplaySettingsStatus::PersistenceFailed
            || Instance->GetDisplaySettingsResult().Status == ELDDisplaySettingsStatus::ApplyFailed)
        { Feedback->SetText(FText::FromString(Instance->GetDisplaySettingsResult().Message)); }
        else if (const auto* PC = Cast<ALDPlayerController>(GetOwningPlayer()); PC && PC->Controls && PC->Controls->NeedsRecovery())
        { Feedback->SetText(FText::FromString(Instance->GetLocalSettingsMessage())); }
    }
    if (const ALDPlayerController* PC = Cast<ALDPlayerController>(GetOwningPlayer());PC&&!PC->IsFrontend())
    {
        const auto& O=PC->OwnerState;
        const auto* GS=GetWorld()->GetGameState<ALDGameState>();
        const bool Lobby=GS&&GS->DemoLobby.Generation&&GS->DemoLobby.Phase==ELDMatchPhase::Lobby;
        if (Lobby!=bInLobby && LobbyPanel)
        {
            bInLobby=Lobby;
            LobbyPanel->SetVisibility(Lobby?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
            HeaderPanel->SetVisibility(Lobby?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
            if (Lobby) { MenuPanel->SetVisibility(ESlateVisibility::Collapsed); bMenuOpen=false; }
            const_cast<ALDPlayerController*>(PC)->SetMenuOpen(Lobby);
        }
        if (Lobby && LobbyInfo)
        {
            FString Roster;
            for(const auto& Member:GS->DemoLobby.Members)
            {
                Roster+=Member.Name+(Member.Host?TEXT(" [Host]"):TEXT(""))
                    +(Member.Kind==ELDContenderKind::Human?(Member.Ready?TEXT(" - Human / Ready\n"):TEXT(" - Human / Not ready\n")):TEXT(" - AI\n"));
            }
            Roster+=TEXT("\n")+GS->DemoLobby.StartBlocker+TEXT("\n")+PC->IntentFeedback;
            LobbyInfo->SetText(FText::FromString(Roster));
            AIControls->SetIsEnabled(O.bLobbyHost);
            LobbyStart->SetIsEnabled(O.bLobbyHost&&GS->DemoLobby.CanStart);
            LobbyStart->SetVisibility(O.bLobbyHost?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
            return;
        }
        if(PC->Navigation->Enabled())
        {
            HeaderPanel->SetVisibility(ESlateVisibility::Collapsed);
            if(!PC->IntentFeedback.IsEmpty()){Feedback->SetText(FText::FromString(PC->IntentFeedback));}
            return;
        }
        if (!PC->IntentFeedback.IsEmpty()) { Feedback->SetText(FText::FromString(PC->IntentFeedback)); }
    }
}

void ULDUIRoot::ToggleMenu()
{
    if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->InventoryUI&&PC->InventoryUI->IsOpen()){PC->InventoryUI->Close();return;}
    if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->ControlsUI&&PC->ControlsUI->IsOpen()){PC->ControlsUI->Close();return;}
    if (bInLobby) { return; }
    if (CastChecked<ALDPlayerController>(GetOwningPlayer())->IsFrontend()) { return; }
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
void ULDUIRoot::DismissMenu(){if(!CastChecked<ALDPlayerController>(GetOwningPlayer())->IsFrontend()){bMenuOpen=false;MenuPanel->SetVisibility(ESlateVisibility::Collapsed);}}
void ULDUIRoot::Host()
{
    FString Error;
    if (!LDDemoEntry::VerifyRuntimeFiles(Error) || !LDDemoEntry::VerifyWorldPackages(Error))
    {
        GetGameInstance<ULDGameInstance>()->ConnectionMessage = Error;
        Feedback->SetText(FText::FromString(Error));
        return;
    }
    GetGameInstance<ULDGameInstance>()->ConnectionMessage.Reset();
    UGameplayStatics::OpenLevel(this, LDDemoEntry::WorldMap, true, TEXT("listen"));
}
void ULDUIRoot::Join()
{
    const FString Destination = Address->GetText().ToString().TrimStartAndEnd();
    FString Error, Digest, Version;
    if (!LDDemoEntry::ValidateAddress(Destination, Error) || !LDDemoEntry::ReadProductVersion(Version, Error)
        || !LDDemoEntry::VerifyRuntimeFiles(Error, &Digest) || !LDDemoEntry::VerifyWorldPackages(Error))
    {
        GetGameInstance<ULDGameInstance>()->ConnectionMessage = Error;
        Feedback->SetText(FText::FromString(Error));
        return;
    }
    GetGameInstance<ULDGameInstance>()->ConnectionMessage.Reset();
    Feedback->SetText(FText::FromString(TEXT("Connecting...")));
    GetOwningPlayer()->ClientTravel(Destination+FString::Printf(TEXT("?LDWire=%u?LDProduct=%s?LDCatalog=%s"),
        LDDemoEntry::WireProtocol,*Version,*Digest), TRAVEL_Absolute);
}
void ULDUIRoot::Resume() { if (bMenuOpen) { ToggleMenu(); } }
void ULDUIRoot::Leave() { UGameplayStatics::OpenLevel(this, TEXT("/Game/Lockdown/Generated/Maps/L_Frontend")); }
void ULDUIRoot::Exit() { UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false); }
void ULDUIRoot::Ready() { auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());PC->SetDemoReady(!PC->OwnerState.bReady); }
void ULDUIRoot::StartMatch() { CastChecked<ALDPlayerController>(GetOwningPlayer())->StartDemoMatch(); }
