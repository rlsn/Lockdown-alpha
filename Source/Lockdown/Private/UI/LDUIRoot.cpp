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
#include "GameFramework/GameStateBase.h"
#include "Framework/LDGameMode.h"
#include "Objectives/LDObjectiveRules.h"
#include "Components/ProgressBar.h"

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
        Row(Text(TEXT("Unreal migration demo\nWork, survive, recover a Seal and claim the Heart."), 16));
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
            Button(TEXT("Host gameplay demo"))->OnClicked.AddDynamic(this, &ULDUIRoot::Host);
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
            if (GetOwningPlayer()->HasAuthority()) { Button(TEXT("Authority fixtures (setup only)"))->OnClicked.AddDynamic(this, &ULDUIRoot::Fixtures); }
        }
        Row(Text(TEXT("WASD move · Shift sprint · Mouse aim\nClick attack · Q assassination · Space dash\nE interact / hold · F ward · Wheel zoom\nEscape / H menu"), 15));
        Feedback = Text(TEXT(""), 14);
        Row(Feedback);
        Button(TEXT("Exit"))->OnClicked.AddDynamic(this, &ULDUIRoot::Exit);
        bMenuOpen = bFrontend;
        MenuPanel->SetVisibility(bFrontend ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        if(!bFrontend)
        {
            UBorder* ObjectivePanel=WidgetTree->ConstructWidget<UBorder>();ObjectivePanel->SetBrushColor(FLinearColor(.025f,.045f,.045f,.94f));ObjectivePanel->SetPadding(FMargin(16,10));
            UVerticalBox* Information=WidgetTree->ConstructWidget<UVerticalBox>();ObjectivePanel->SetContent(Information);
            ObjectiveInfo=Text(TEXT("E interacts with nearby public sites."),16);Information->AddChildToVerticalBox(ObjectiveInfo);
            ChannelProgress=WidgetTree->ConstructWidget<UProgressBar>();ChannelProgress->SetFillColorAndOpacity(FLinearColor(.75f,.65f,.22f));
            auto* ProgressSlot=Information->AddChildToVerticalBox(ChannelProgress);ProgressSlot->SetPadding(FMargin(0,6));
            Information->AddChildToVerticalBox(Text(TEXT("WASD · Shift · Mouse aim · Click attack · Q assassination · Space dash · E hold · F ward · H / Esc help"),14));
            auto* ObjectiveSlot=Canvas->AddChildToCanvas(ObjectivePanel);ObjectiveSlot->SetAnchors(FAnchors(0,1));ObjectiveSlot->SetAlignment(FVector2D(0,1));ObjectiveSlot->SetPosition(FVector2D(18,-18));ObjectiveSlot->SetSize(FVector2D(960,150));
            // HUD panels do not capture clicks intended for the game world.
            ObjectivePanel->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        if(!bFrontend&&GetOwningPlayer()->HasAuthority())
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
    if (const ULDGameInstance* Instance = GetGameInstance<ULDGameInstance>())
    {
        if (!Instance->ConnectionMessage.IsEmpty()) { Feedback->SetText(FText::FromString(Instance->ConnectionMessage)); }
    }
    if (const ALDPlayerController* PC = Cast<ALDPlayerController>(GetOwningPlayer());PC&&!PC->IsFrontend())
    {
        const ALDCharacter* Body=Cast<ALDCharacter>(PC->GetPawn());
        const auto& O=PC->OwnerState;
        const auto* GS=GetWorld()->GetGameState<ALDGameState>();
        const double Now=GetWorld()->GetGameState()?GetWorld()->GetGameState()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
        const ALDWorldViewRelay* Relay=PC->LocalView();
        const FLDWorldView View=Relay ? Relay->LastView() : FLDWorldView();
        FString Life=O.bActive?FString::Printf(TEXT("HP %d/100 · Speed %.0f · Ward %d"),O.Hp,Body?Body->GetVelocity().Size2D():0,O.WardCharges)
            :(GS&&GS->EndReason?FString::Printf(TEXT("DEAD · Lost %lld gold · Demo finished"),O.LastLostGold):FString::Printf(TEXT("DEAD · Lost %lld gold · Return in %.1fs · Owned wards and shared sight only"),O.LastLostGold,FMath::Max(0.,O.ReturnAt-Now)));
        FString Danger=Now<O.WarningUntil?TEXT("DANGER: ASSASSINATION · Turn, escape, dash or deal damage!")
            :(Now<O.ProtectedUntil?FString::Printf(TEXT("Return protection %.1fs · Gameplay actions locked"),O.ProtectedUntil-Now)
                :(Now<O.DamageUntil?FString::Printf(TEXT("HIT · Anonymous incoming sector %d/8 (world +X=0, toward +Y)"),O.IncomingSector):PC->IntentFeedback));
        Status->SetColorAndOpacity(Now<O.WarningUntil?FLinearColor(1,.35f,.2f):FLinearColor(.88f,.9f,.82f));
        FString Public=TEXT("Heart unavailable");
        if(GS)
        {
            const TCHAR* Route=GS->ClaimRoute==LDObjective::Assault?TEXT("Assault"):(GS->ClaimRoute==LDObjective::SealRoute?TEXT("Seal"):TEXT("Mixed"));
            Public=GS->EndReason?(GS->EndReason==2?TEXT("No claim · Demo finished"):FString::Printf(TEXT("Heart claimed · Route %s · Demo finished"),GS->WinningRoute==LDObjective::SealRoute?TEXT("Seal"):TEXT("Assault")))
                :(GS->bClaimActive?FString::Printf(TEXT("Heart %s · %s · %.1f / 10s"),GS->bContested?TEXT("CONTESTED"):TEXT("claim active"),Route,GS->ClaimProgress)
                    :FString::Printf(TEXT("Heart %s · Defences W:%d E:%d S:%d C:%d · Time left %.0fs"),GS->DefensiveFlags?TEXT("locked"):TEXT("ordinary access open"),!!(GS->DefensiveFlags&1),!!(GS->DefensiveFlags&2),!!(GS->DefensiveFlags&4),!!(GS->DefensiveFlags&8),FMath::Max(0.,GS->MatchDeadline-Now)));
            if(GS->SealNoticeSerial){Public+=FString::Printf(TEXT(" · Seal %s: %s"),GS->SealNoticeKind==1?TEXT("fixture seed"):GS->SealNoticeKind==2?TEXT("dropped"):TEXT("district"),LDObjective::DistrictName(GS->SealNoticeDistrict));}
            const TCHAR* Names[]={TEXT("West"),TEXT("East"),TEXT("South"),TEXT("Central")};
            for(int32 I=0;I<DefenceLabels.Num();++I){DefenceLabels[I]->SetText(FText::FromString(FString::Printf(TEXT("%s defence: %s (fixture toggle)"),Names[I],GS->DefensiveFlags&(1<<I)?TEXT("ACTIVE"):TEXT("CLEARED"))));}
        }
        Status->SetText(FText::FromString(FString::Printf(TEXT("LOCKDOWN · MIGRATION DEMO · %s\nGold %lld · XP %lld (level %d) · Seal %s · Sight %s: body %d / ward %d / shared %d\nAttack %.1fs · Q %.1fs · Dash %.1fs · %s\n%s"),
            *Life,O.Gold,O.Xp,LDRules::LevelFromXp(O.Xp),O.SealId.IsValid()?TEXT("carried"):TEXT("none"),Relay&&!Relay->IsStale()?TEXT("fresh"):TEXT("STALE"),View.BodySources,View.WardSources,View.PublicSources,
            FMath::Max(0.,O.BasicReadyAt-Now),FMath::Max(0.,O.AssassinReadyAt-Now),FMath::Max(0.,O.DashReadyAt-Now),*Public,*Danger)));
        if(ObjectiveInfo)
        {
            const TCHAR* Stage=O.ContractStage==1?TEXT("Collect at West station [W]"):O.ContractStage==2?TEXT("Deliver at Guild [G]"):TEXT("No active contract");
            FString Progress=O.ChannelKind?FString::Printf(TEXT("%s %.1f / %.0fs confirmed · Release E, move or take damage to cancel."),O.ChannelKind==1?TEXT("Collecting"):O.ChannelKind==2?TEXT("Delivering"):TEXT("Claiming"),O.ChannelProgress,O.ChannelKind==3?10.:3.):TEXT("Hold progress resets on interruption.");
            if(GS&&GS->bContested&&O.ChannelKind==3){Progress=TEXT("CONTESTED · Claim progress reset to zero.");}
            ObjectiveInfo->SetText(FText::FromString(FString::Printf(TEXT("%s\nCourier: %s · Commissions %d\n%s"),*PC->ContextPrompt(),Stage,O.Commissions,*Progress)));
            ChannelProgress->SetPercent(O.ChannelKind?FMath::Clamp(O.ChannelProgress/(O.ChannelKind==3?10.f:3.f),0.f,1.f):0);
        }
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
    { Box(Origin+FVector2D((Entity.Position.X+2400)/20,(1600-Entity.Position.Y)/20)-FVector2D(2),FVector2D(4),Entity.Kind==ELDObservedKind::Seal?FLinearColor(1,.6f,.1f):FLinearColor(.9f,.9f,.7f)); }
    for(uint8 Site=1;Site<=3;++Site)
    {const FVector P=LDObjective::Position(Site);const FVector2D At=Origin+FVector2D((P.X+2400)/20,(1600-P.Y)/20);Box(At-FVector2D(3),FVector2D(6),FLinearColor(.25f,.7f,.9f));FSlateDrawElement::MakeText(Elements,Result+2,Geometry.ToPaintGeometry(FVector2D(30,16),FSlateLayoutTransform(At+FVector2D(4,-8))),FString(Site==1?TEXT("G"):Site==2?TEXT("W"):TEXT("H")),FCoreStyle::GetDefaultFontStyle("Bold",12),ESlateDrawEffect::None,FLinearColor::White);}
    if (const APawn* Pawn=PC->GetPawn()) { const FVector P=Pawn->GetActorLocation(); Box(Origin+FVector2D((P.X+2400)/20,(1600-P.Y)/20)-FVector2D(3),FVector2D(6),FLinearColor::White); }
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
    if (CastChecked<ALDPlayerController>(GetOwningPlayer())->IsFrontend()) { return; }
    if(FixturePanel&&FixturePanel->GetVisibility()==ESlateVisibility::Visible){FixtureBack();return;}
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
