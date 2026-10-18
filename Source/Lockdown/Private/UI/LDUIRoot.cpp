#include "UI/LDUIRoot.h"
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
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Internationalization/Regex.h"
#include "Styling/CoreStyle.h"
#include "Vision/LDWorldViewRelay.h"
#include "Rendering/DrawElements.h"

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
        Header->SetBrushColor(FLinearColor(.025f,.045f,.045f,.92f));
        Header->SetPadding(FMargin(20,12));
        Status = Text(TEXT("LOCKDOWN  |  Unreal migration demo"), 18);
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
        Row(Text(TEXT("Private vision checkpoint\nExplore cover, raised paths and shared sight."), 16));
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
            Button(TEXT("Host movement demo"))->OnClicked.AddDynamic(this, &ULDUIRoot::Host);
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
            if (GetOwningPlayer()->HasAuthority()) { Button(TEXT("Fixture: toggle public sensor"))->OnClicked.AddDynamic(this, &ULDUIRoot::ToggleSensor); }
        }
        Row(Text(TEXT("WASD move  ·  Shift sprint  ·  Mouse aim\nF place ward within 96 cm  ·  Wheel zoom\nEscape / H menu"), 15));
        Feedback = Text(TEXT(""), 14);
        Row(Feedback);
        Button(TEXT("Exit"))->OnClicked.AddDynamic(this, &ULDUIRoot::Exit);
        bMenuOpen = bFrontend;
        MenuPanel->SetVisibility(bFrontend ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
    return Super::RebuildWidget();
}

void ULDUIRoot::NativeTick(const FGeometry& Geometry, float Delta)
{
    Super::NativeTick(Geometry, Delta);
    if (const ULDGameInstance* Instance = GetGameInstance<ULDGameInstance>())
    {
        if (!Instance->ConnectionMessage.IsEmpty()) { Feedback->SetText(FText::FromString(Instance->ConnectionMessage)); }
    }
    if (const ALDCharacter* Body = Cast<ALDCharacter>(GetOwningPlayerPawn()))
    {
        const ALDPlayerController* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
        const ALDWorldViewRelay* Relay=PC->LocalView();
        const FLDWorldView View=Relay ? Relay->LastView() : FLDWorldView();
        Status->SetText(FText::FromString(FString::Printf(TEXT("LOCKDOWN  |  Unreal migration demo  |  Private vision\nWASD move  ·  Shift sprint  ·  Mouse aim  ·  F ward  ·  Wheel zoom  ·  Esc menu\nSpeed %.0f cm/s  |  Ward charges %d  |  Sight %s\nSources: body %d · private wards %d · shared tower %d  |  Map: light=visible, dim=explored, black=unseen\n%s"),
            Body->GetVelocity().Size2D(),PC->OwnerState.WardCharges,Relay && !Relay->IsStale() ? TEXT("fresh") : TEXT("STALE"),View.BodySources,View.WardSources,View.PublicSources,
            PC->IntentFeedback.IsEmpty() ? TEXT("F: one private ward, within 96 cm; lasts 60 seconds.") : *PC->IntentFeedback)));
        if (!PC->IntentFeedback.IsEmpty()) { Feedback->SetText(FText::FromString(PC->IntentFeedback)); }
    }
}

int32 ULDUIRoot::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    const int32 Result=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
    const ALDPlayerController* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    const ALDWorldViewRelay* Relay=PC ? PC->LocalView() : nullptr;
    if (!Relay || PC->IsFrontend()) { return Result; }
    const FLDWorldView& View=Relay->LastView();
    const FVector2D Origin(Geometry.GetLocalSize().X-270,180);
    auto Box=[&](FVector2D At,FVector2D Size,FLinearColor Color)
    { FSlateDrawElement::MakeBox(Elements,Result+1,Geometry.ToPaintGeometry(Size,FSlateLayoutTransform(At)),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,Color); };
    for (int32 I=0;I<LDVision::Cells;++I)
    {
        const bool Current=!Relay->IsStale() && LDVision::Bit(View.Current,I);
        Box(Origin+FVector2D((I%60)*4,(39-I/60)*4),FVector2D(4),Current ? FLinearColor(.42f,.55f,.5f,1) : (LDVision::Bit(View.Explored,I) ? FLinearColor(.12f,.17f,.16f,1) : FLinearColor(.008f,.012f,.012f,1)));
    }
    for (const auto& Entity : View.Entities)
    { Box(Origin+FVector2D((Entity.Position.X+2400)/20,(1600-Entity.Position.Y)/20)-FVector2D(2),FVector2D(4),FLinearColor(.9f,.9f,.7f)); }
    if (const APawn* Pawn=PC->GetPawn()) { const FVector P=Pawn->GetActorLocation(); Box(Origin+FVector2D((P.X+2400)/20,(1600-P.Y)/20)-FVector2D(3),FVector2D(6),FLinearColor::White); }
    return Result+1;
}
void ULDUIRoot::ToggleSensor() { CastChecked<ALDPlayerController>(GetOwningPlayer())->TogglePublicSensorFixture(); }

void ULDUIRoot::ToggleMenu()
{
    if (CastChecked<ALDPlayerController>(GetOwningPlayer())->IsFrontend()) { return; }
    bMenuOpen = !bMenuOpen;
    MenuPanel->SetVisibility(bMenuOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    CastChecked<ALDPlayerController>(GetOwningPlayer())->SetMenuOpen(bMenuOpen);
}
FReply ULDUIRoot::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape) { ToggleMenu(); return FReply::Handled(); }
    return Super::NativeOnKeyDown(Geometry, Event);
}
void ULDUIRoot::Host()
{
    GetGameInstance<ULDGameInstance>()->ConnectionMessage.Reset();
    UGameplayStatics::OpenLevel(this, TEXT("/Game/Lockdown/Generated/Maps/L_MigrationSandbox"), true, TEXT("listen"));
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
