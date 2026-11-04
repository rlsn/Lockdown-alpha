#include "UI/LDHUD.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDAttributesPanel.h"
#include "UI/LDChoicesPanel.h"
#include "UI/LDShopPanel.h"
#include "UI/LDDevTestPanel.h"
#include "UI/LDMinimap.h"
#include "UI/LDWidgets.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDUIRoot.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDDemoEntry.h"
#include "Character/LDCharacter.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Components/CapsuleComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "Vision/LDWorldViewRelay.h"
#include "Audio/LDAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
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
void HudSetColor(UTextBlock* Text,const FLinearColor& Color)
{
    if(Text&&Text->GetColorAndOpacity().GetSpecifiedColor()!=Color){Text->SetColorAndOpacity(FSlateColor(Color));}
}
// Bold where the text leads its group; a dark outline where it sits on bright or changing fills.
void HudFace(UTextBlock* Text,bool bBold,int32 Outline)
{
    auto Font=Text->GetFont();
    if(bBold){Font.TypefaceFontName=FName(TEXT("Bold"));}
    if(Outline>0){Font.OutlineSettings.OutlineSize=Outline;Font.OutlineSettings.OutlineColor=LDHUDStyle::Rim;}
    Text->SetFont(Font);
}
// Rounded face that takes its fill from the border's brush colour; the edge fades with that colour and the widget opacity.
FSlateRoundedBoxBrush HudRound(float Radius,const FLinearColor& Edge,float EdgeWidth)
{
    FSlateRoundedBoxBrush Brush(FLinearColor::White,Radius,Edge,EdgeWidth);Brush.OutlineSettings.bUseBrushTransparency=true;return Brush;
}
void HudBarStyle(UProgressBar* Bar,const FLinearColor& Fill)
{
    FProgressBarStyle Style;
    Style.SetBackgroundImage(FSlateRoundedBoxBrush(LDHUDStyle::BarTrack,4.f,LDHUDStyle::PanelRim,1.f)).SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,4.f)).SetMarqueeImage(FSlateNoResource());
    Bar->SetWidgetStyle(Style);Bar->SetFillColorAndOpacity(Fill);
}
void HudTint(ULDCommandButton* Button,const FLinearColor& Color)
{
    if(!Button){return;}
    auto Style=Button->GetStyle();
    if(Style.Normal.TintColor==FSlateColor(Color)){return;}
    Style.Normal.TintColor=FSlateColor(Color);Style.Hovered.TintColor=FSlateColor(Color*1.6f);Button->SetStyle(Style);
}
const FName HudActionBindings[]={TEXT("Skill1"),TEXT("Skill2"),TEXT("Item1"),TEXT("Item2"),TEXT("Dash")};
const ELDCombatEntry HudActionEntries[]={ELDCombatEntry::Q,ELDCombatEntry::W,ELDCombatEntry::Item1,ELDCombatEntry::Item2,ELDCombatEntry::Dash};
const FName HudActionNames[]={TEXT("HUDActionQ"),TEXT("HUDActionW"),TEXT("HUDActionItem1"),TEXT("HUDActionItem2"),TEXT("HUDActionDash")};
constexpr int32 HudActionCount=UE_ARRAY_COUNT(HudActionEntries);
// Status control bits that forbid each entry kind: character actives cast, equipment actives use gear, the dash displaces.
const uint8 HudActionControl[]={uint8(ELDCombatControlBlock::Cast),uint8(ELDCombatControlBlock::Cast),uint8(ELDCombatControlBlock::Gear),
    uint8(ELDCombatControlBlock::Gear),uint8(ELDCombatControlBlock::Displacement)};
static_assert(UE_ARRAY_COUNT(HudActionBindings)==HudActionCount && UE_ARRAY_COUNT(HudActionNames)==HudActionCount
    && UE_ARRAY_COUNT(HudActionControl)==HudActionCount,"Every HUD action entry has a binding, a name and a control bit.");
// Band geometry in Slate units at the 1080p reference (720p draws the same layout at ~0.667). Equal side blocks keep the
// centre block, resources over the 2+2+1 action entries, on the screen centre directly under the character.
constexpr float HudSideWidth=300.f;
constexpr float HudBandPadX=14.f;
constexpr float HudBlockGap=16.f;
constexpr float HudSlotWidth=124.f;
constexpr float HudSlotHeight=106.f;
constexpr float HudSlotGap=8.f;
constexpr float HudGroupGap=24.f;
constexpr float HudCentreWidth=5*HudSlotWidth+2*HudSlotGap+2*HudGroupGap;
constexpr double HudNoticeSeconds=2.;
constexpr double HudPulseSeconds=6.;
const FLinearColor HudButtonRest(.045f,.12f,.15f);
const FLinearColor HudReadyEdge(FColor(0x7F,0xE0,0xD0));
const FLinearColor HudManaText(FColor(0x8F,0xB0,0xFF));
const FLinearColor HudControlEdge(FColor(0xFF,0x6A,0x3D));
const FLinearColor HudPromptFill(FColor(0xFF,0xC8,0x3D));
const FLinearColor HudPromptHover(FColor(0xFF,0xD8,0x66));
const FLinearColor HudPromptPress(FColor(0xE6,0xAE,0x2A));
const FLinearColor HudPromptInk(FColor(0x1A,0x13,0x00));
struct FHudLookStyle
{
    FLinearColor Fill;
    FLinearColor Edge;
    float EdgeWidth=1.f;
    FLinearColor Caption;
    FLinearColor Name;
};
// Ready, armed and controlled slots get a crisp 2u edge; slot text carries a 1u outline because the fills are mid-tones.
FHudLookStyle HudLookStyle(LDHUD::EActionLook Look)
{
    switch(Look)
    {
    case LDHUD::EActionLook::Ready: return {LDHUDStyle::SlotReady,HudReadyEdge,2.f,HudReadyEdge,LDHUDStyle::TextPrimary};
    case LDHUD::EActionLook::Armed: return {LDHUDStyle::SlotArmed,LDHUDStyle::TextAccent,2.f,LDHUDStyle::TextAccent,LDHUDStyle::TextPrimary};
    // Cooling keeps the ready fill, so the part the mask has already uncovered shows how far the slot has come back.
    case LDHUD::EActionLook::Cooling: return {LDHUDStyle::SlotReady,FLinearColor(.10f,.26f,.28f),1.f,LDHUDStyle::TextSecondary,LDHUDStyle::TextPrimary};
    case LDHUD::EActionLook::NoMana: return {LDHUDStyle::SlotNoMana,FLinearColor(.18f,.28f,.62f),1.f,HudManaText,HudManaText};
    case LDHUD::EActionLook::Controlled: return {LDHUDStyle::SlotControlled,HudControlEdge,2.f,HudControlEdge,LDHUDStyle::TextPrimary};
    default: return {LDHUDStyle::SlotBlocked,FLinearColor(.10f,.13f,.16f),1.f,LDHUDStyle::TextSecondary,LDHUDStyle::TextSecondary};
    }
}
// Camp tiers are named after their approved creature, never a guessed or hidden attacker.
void HudCampNames(const FLDDemoContentDefinitions* Content,FString& Low,FString& High)
{
    Low=TEXT("低档野怪");High=TEXT("高档野怪");
    if(Content){for(const FLDDemoMonsterDefinition& Monster:Content->Monsters){(Monster.Tier==ELDDemoCampTier::Low?Low:High)=LDHUD::ContentName(Content,Monster.Id);}}
}
}

TSharedRef<SWidget> ULDHUD::RebuildWidget()
{
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if(!Band)
    {
        auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
        auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Canvas;
        auto Text=[&](const FString& S,int32 Size){return LDWidgets::Text(WidgetTree,S,Size);};
        // A text in a panel that sizes to its content has no stable width to wrap at: it stays on one line, or wraps at a fixed
        // width, so an empty or collapsed frame can never shrink the next text into a one-glyph column.
        auto OneLine=[&](const FString& S,int32 Size,float WrapAt){UTextBlock* Label=Text(S,Size);Label->SetAutoWrapText(false);if(WrapAt>0){Label->SetWrapTextAt(WrapAt);}return Label;};
        auto Panel=[&](UWidget* Child,bool bHit)
        {
            auto* Border=LDWidgets::Panel(WidgetTree,Child,FMargin(12,8));
            if(bHit){HitPanels.Add(Border);}else{Border->SetVisibility(ESlateVisibility::HitTestInvisible);}
            return Border;
        };
        auto Place=[&](UWidget* Widget,FAnchors Anchors,FVector2D Alignment,FVector2D Position)
        {auto* S=Canvas->AddChildToCanvas(Widget);S->SetAnchors(Anchors);S->SetAlignment(Alignment);S->SetPosition(Position);S->SetAutoSize(true);};
        auto Bar=[&](FLinearColor Fill,UProgressBar*& OutBar,UTextBlock*& OutText,float Width,float Height,TObjectPtr<UTextBlock>* OutRegen=nullptr)
        {
            auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(Width);Box->SetHeightOverride(Height);
            auto* Stack=WidgetTree->ConstructWidget<UOverlay>();Box->SetContent(Stack);
            OutBar=WidgetTree->ConstructWidget<UProgressBar>();HudBarStyle(OutBar,Fill);OutBar->SetPercent(0);
            auto* BarSlot=Stack->AddChildToOverlay(OutBar);BarSlot->SetHorizontalAlignment(HAlign_Fill);BarSlot->SetVerticalAlignment(VAlign_Fill);
            // Values cross bright fills and the dark track alike: bold white with a dark outline reads on both.
            OutText=OneLine(TEXT(""),15,0);OutText->SetJustification(ETextJustify::Center);HudFace(OutText,true,2);
            auto* TextSlot=Stack->AddChildToOverlay(OutText);TextSlot->SetHorizontalAlignment(HAlign_Center);TextSlot->SetVerticalAlignment(VAlign_Center);
            if(OutRegen)
            {
                auto* Regen=OneLine(TEXT(""),14,0);HudFace(Regen,true,2);
                auto* RegenSlot=Stack->AddChildToOverlay(Regen);RegenSlot->SetHorizontalAlignment(HAlign_Right);
                RegenSlot->SetVerticalAlignment(VAlign_Center);RegenSlot->SetPadding(FMargin(0,0,10,0));
                *OutRegen=Regen;
                // Reserve equal space on both sides so the current/max value stays centred and cannot overlap regen.
                TextSlot->SetHorizontalAlignment(HAlign_Fill);TextSlot->SetPadding(FMargin(150,0));
            }
            return Box;
        };
        auto Portraits=[&](float Size,int32 GlyphSize,UBorder*& OutFrame,UTextBlock*& OutGlyph)
        {
            // The glyph sits on any of the six identity tints, bright ones included; an outline keeps it readable on each.
            OutGlyph=OneLine(TEXT(""),GlyphSize,0);OutGlyph->SetJustification(ETextJustify::Center);HudFace(OutGlyph,true,2);
            OutFrame=WidgetTree->ConstructWidget<UBorder>();OutFrame->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,8.f,LDHUDStyle::Rim,1.f));
            OutFrame->SetHorizontalAlignment(HAlign_Center);OutFrame->SetVerticalAlignment(VAlign_Center);OutFrame->SetContent(OutGlyph);
            auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(Size);Box->SetHeightOverride(Size);Box->SetContent(OutFrame);
            return Box;
        };

        // Top-left: the current authorized target only. It disappears with the observation and never greys into history.
        auto* TargetBox=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* TargetHead=WidgetTree->ConstructWidget<UHorizontalBox>();TargetBox->AddChildToVerticalBox(TargetHead);
        UBorder* TP=nullptr;UTextBlock* TG=nullptr;TargetHead->AddChildToHorizontalBox(Portraits(44,18,TP,TG))->SetPadding(FMargin(0,0,10,0));TargetPortrait=TP;TargetGlyph=TG;
        TargetTitle=Text(TEXT(""),18);HudFace(TargetTitle,true,0);auto* TitleSlot=TargetHead->AddChildToHorizontalBox(TargetTitle);TitleSlot->SetVerticalAlignment(VAlign_Center);TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        UProgressBar* B=nullptr;UTextBlock* T=nullptr;
        TargetBox->AddChildToVerticalBox(Bar(LDHUDStyle::EnemyHealth,B,T,306,24))->SetPadding(FMargin(0,8,0,2));TargetHealthBar=B;TargetHealth=T;
        TargetManaRow=Bar(LDHUDStyle::Mana,B,T,306,22);TargetBox->AddChildToVerticalBox(TargetManaRow)->SetPadding(FMargin(0,2));TargetManaBar=B;TargetMana=T;
        TargetStatuses=Text(TEXT(""),14);TargetBox->AddChildToVerticalBox(TargetStatuses)->SetPadding(FMargin(0,4,0,0));
        TargetEquipment=Text(TEXT(""),14);TargetEquipment->SetColorAndOpacity(LDHUDStyle::TextSecondary);TargetBox->AddChildToVerticalBox(TargetEquipment)->SetPadding(FMargin(0,2,0,0));
        auto* TargetWidth=WidgetTree->ConstructWidget<USizeBox>();TargetWidth->SetWidthOverride(330);TargetWidth->SetContent(TargetBox);
        TargetCard=Panel(TargetWidth,true);Place(TargetCard,FAnchors(0,0),FVector2D(0,0),FVector2D(16,16));TargetCard->SetVisibility(ESlateVisibility::Collapsed);

        // Top-centre: the public capture state, the public kill feed, then the own death banner. None takes pointer input.
        auto* CircleBox=WidgetTree->ConstructWidget<UVerticalBox>();
        // A DEV test room is marked on every member's screen for the whole match (#78).
        DevMarkerText=OneLine(TEXT(""),15,0);DevMarkerText->SetJustification(ETextJustify::Center);DevMarkerText->SetColorAndOpacity(LDHUDStyle::TextWarning);
        DevMarkerText->SetVisibility(ESlateVisibility::Collapsed);
        CircleBox->AddChildToVerticalBox(DevMarkerText)->SetHorizontalAlignment(HAlign_Center);
        PhaseText=OneLine(TEXT(""),18,0);PhaseText->SetJustification(ETextJustify::Center);
        CircleBox->AddChildToVerticalBox(PhaseText)->SetHorizontalAlignment(HAlign_Center);
        CircleProgress=WidgetTree->ConstructWidget<UProgressBar>();HudBarStyle(CircleProgress,FLinearColor(1,.78f,.25f));CircleProgress->SetPercent(0);
        auto* CircleBarSize=WidgetTree->ConstructWidget<USizeBox>();CircleBarSize->SetWidthOverride(360);CircleBarSize->SetHeightOverride(8);CircleBarSize->SetContent(CircleProgress);
        CircleBox->AddChildToVerticalBox(CircleBarSize)->SetPadding(FMargin(0,6,0,0));
        // The own next step under the public line, in the accent colour so it reads as advice rather than match state.
        NextStepText=OneLine(TEXT(""),15,520);NextStepText->SetJustification(ETextJustify::Center);NextStepText->SetColorAndOpacity(LDHUDStyle::TextAccent);
        CircleBox->AddChildToVerticalBox(NextStepText)->SetHorizontalAlignment(HAlign_Center);
        PhaseBanner=Panel(CircleBox,false);Place(PhaseBanner,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,16));
        auto* FeedBox=WidgetTree->ConstructWidget<UVerticalBox>();
        for(int32 Row=0;Row<4;++Row)
        {
            UTextBlock* FeedLine=OneLine(TEXT(""),15,520);FeedLine->SetJustification(ETextJustify::Center);FeedLine->SetVisibility(ESlateVisibility::Collapsed);
            KillFeedRows.Add(FeedLine);FeedBox->AddChildToVerticalBox(FeedLine)->SetHorizontalAlignment(HAlign_Center);
        }
        KillFeedPanel=Panel(FeedBox,false);Place(KillFeedPanel,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,92));KillFeedPanel->SetVisibility(ESlateVisibility::Collapsed);
        auto* DeathBox=WidgetTree->ConstructWidget<UVerticalBox>();
        DeathTitle=OneLine(TEXT(""),34,0);HudFace(DeathTitle,true,0);DeathTitle->SetColorAndOpacity(LDHUDStyle::TextDanger);
        DeathCauseText=OneLine(TEXT(""),18,520);DeathCountdown=OneLine(TEXT(""),22,0);
        DeathNote=OneLine(TEXT(""),14,520);DeathNote->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        for(UTextBlock* DeathLine:{DeathTitle.Get(),DeathCauseText.Get(),DeathCountdown.Get(),DeathNote.Get()})
        {DeathLine->SetJustification(ETextJustify::Center);DeathBox->AddChildToVerticalBox(DeathLine)->SetHorizontalAlignment(HAlign_Center);}
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

        // Top-right: minimap, sight state (only when it is not the normal current view) and the settings entry.
        auto* MapBox=WidgetTree->ConstructWidget<UVerticalBox>();
        Minimap=CreateWidget<ULDMinimap>(PC);MapFrame=WidgetTree->ConstructWidget<USizeBox>();MapFrame->SetWidthOverride(240);MapFrame->SetHeightOverride(240);MapFrame->SetContent(Minimap);
        MapBox->AddChildToVerticalBox(MapFrame);
        SightText=Text(TEXT(""),14);SightText->SetColorAndOpacity(LDHUDStyle::TextWarning);MapBox->AddChildToVerticalBox(SightText)->SetPadding(FMargin(0,4));
        MapBox->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,TEXT("设置"),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Settings);}},true,TEXT("HUDSettings")));
        MapColumn=Panel(MapBox,true);Place(MapColumn,FAnchors(1,0),FVector2D(1,0),FVector2D(-16,16));

        // Bottom band: own identity | resources over the 2+2 action entries | utilities. Equal side blocks keep the centre
        // block on the screen centre, directly under the character, where the eyes are during a fight.
        auto* Bottom=WidgetTree->ConstructWidget<UHorizontalBox>();
        auto Divider=[&]()
        {
            auto* Rule=WidgetTree->ConstructWidget<UBorder>();Rule->SetBrushColor(LDHUDStyle::PanelRim);Rule->SetVisibility(ESlateVisibility::HitTestInvisible);
            auto* RuleBox=WidgetTree->ConstructWidget<USizeBox>();RuleBox->SetWidthOverride(1);RuleBox->SetContent(Rule);
            auto* RuleSlot=Bottom->AddChildToHorizontalBox(RuleBox);RuleSlot->SetPadding(FMargin(HudBlockGap*.5f,4));RuleSlot->SetVerticalAlignment(VAlign_Fill);
        };
        auto* SelfBox=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* SelfHead=WidgetTree->ConstructWidget<UHorizontalBox>();SelfBox->AddChildToVerticalBox(SelfHead);
        UBorder* SP=nullptr;UTextBlock* SG=nullptr;
        auto* PortraitStack=WidgetTree->ConstructWidget<UOverlay>();PortraitStack->AddChildToOverlay(Portraits(44,20,SP,SG));Portrait=SP;PortraitGlyph=SG;
        // The level rides on the portrait corner; the experience numbers sit under the name.
        LevelBadgeText=OneLine(TEXT(""),13,0);HudFace(LevelBadgeText,true,0);LevelBadgeText->SetColorAndOpacity(LDHUDStyle::TextAccent);
        auto* LevelBadge=WidgetTree->ConstructWidget<UBorder>();LevelBadge->SetBrush(HudRound(4.f,LDHUDStyle::TextAccent,1.f));LevelBadge->SetBrushColor(FLinearColor(.02f,.03f,.05f,.96f));
        LevelBadge->SetPadding(FMargin(5,0));LevelBadge->SetContent(LevelBadgeText);LevelBadge->SetVisibility(ESlateVisibility::HitTestInvisible);LevelBadge->SetRenderTranslation(FVector2D(6,6));
        auto* BadgeSlot=PortraitStack->AddChildToOverlay(LevelBadge);BadgeSlot->SetHorizontalAlignment(HAlign_Right);BadgeSlot->SetVerticalAlignment(VAlign_Bottom);
        auto* PortraitSlot=SelfHead->AddChildToHorizontalBox(PortraitStack);PortraitSlot->SetPadding(FMargin(0,0,14,0));PortraitSlot->SetVerticalAlignment(VAlign_Top);
        auto* NameBox=WidgetTree->ConstructWidget<UVerticalBox>();
        // A long roster name ends in an ellipsis inside the column; hovering it shows the whole name (UI 3.3).
        SelfName=OneLine(TEXT(""),17,0);HudFace(SelfName,true,0);SelfName->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);SelfName->SetVisibility(ESlateVisibility::Visible);NameBox->AddChildToVerticalBox(SelfName);
        LevelText=OneLine(TEXT(""),14,0);LevelText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);LevelText->SetColorAndOpacity(LDHUDStyle::TextSecondary);NameBox->AddChildToVerticalBox(LevelText)->SetPadding(FMargin(0,2,0,0));
        GoldText=OneLine(TEXT(""),15,0);GoldText->SetColorAndOpacity(LDHUDStyle::GoldGain);NameBox->AddChildToVerticalBox(GoldText)->SetPadding(FMargin(0,2,0,0));
        auto* NameSlot=SelfHead->AddChildToHorizontalBox(NameBox);NameSlot->SetVerticalAlignment(VAlign_Center);NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        // Temporary statuses and passives are separate lines, so a control status is never lost among passives. Lines that
        // would only state a plain value (alive, no passive) are left out; all of them wrap inside the fixed column.
        LifeText=Text(TEXT(""),14);SelfBox->AddChildToVerticalBox(LifeText)->SetPadding(FMargin(0,8,0,0));
        SelfStatusText=Text(TEXT(""),14);SelfStatusText->SetColorAndOpacity(LDHUDStyle::TextWarning);SelfBox->AddChildToVerticalBox(SelfStatusText)->SetPadding(FMargin(0,4,0,0));
        PassiveText=Text(TEXT(""),14);PassiveText->SetColorAndOpacity(LDHUDStyle::TextSecondary);SelfBox->AddChildToVerticalBox(PassiveText)->SetPadding(FMargin(0,4,0,0));
        auto* SelfWidth=WidgetTree->ConstructWidget<USizeBox>();SelfWidth->SetWidthOverride(HudSideWidth);SelfWidth->SetClipping(EWidgetClipping::ClipToBounds);SelfWidth->SetContent(SelfBox);
        Bottom->AddChildToHorizontalBox(SelfWidth)->SetVerticalAlignment(VAlign_Fill);
        Divider();
        auto* Centre=WidgetTree->ConstructWidget<UVerticalBox>();
        Centre->AddChildToVerticalBox(Bar(LDHUDStyle::OwnHealth,B,T,HudCentreWidth,24,&HealthRegenText));HealthBar=B;HealthText=T;
        Centre->AddChildToVerticalBox(Bar(LDHUDStyle::Mana,B,T,HudCentreWidth,22,&ManaRegenText))->SetPadding(FMargin(0,3,0,0));ManaBar=B;ManaText=T;
        LevelBar=WidgetTree->ConstructWidget<UProgressBar>();HudBarStyle(LevelBar,LDHUDStyle::Experience);LevelBar->SetPercent(0);
        auto* LevelStrip=WidgetTree->ConstructWidget<USizeBox>();LevelStrip->SetHeightOverride(8);LevelStrip->SetContent(LevelBar);
        Centre->AddChildToVerticalBox(LevelStrip)->SetPadding(FMargin(0,4,0,0));
        auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();
        for(int32 Index=0;Index<HudActionCount;++Index)
        {
            FLDHUDActionSlot& Entry=ActionSlots.AddDefaulted_GetRef();
            auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();
            // Every line stays inside the slot: the key ends in an ellipsis, the name wraps and ends in an ellipsis on its last
            // visible line, and the tooltip keeps the full text. Slot text carries a 1u outline over the mid-tone fills.
            Lines->SetClipping(EWidgetClipping::ClipToBounds);
            auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();Lines->AddChildToVerticalBox(Head);
            Entry.Key=OneLine(TEXT(""),16,0);HudFace(Entry.Key,true,1);Entry.Key->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Entry.Key->SetColorAndOpacity(LDHUDStyle::TextAccent);
            auto* KeySlot=Head->AddChildToHorizontalBox(Entry.Key);KeySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));KeySlot->SetVerticalAlignment(VAlign_Center);
            Entry.Cost=OneLine(TEXT(""),13,0);Entry.Cost->SetColorAndOpacity(HudManaText);
            Entry.CostChip=WidgetTree->ConstructWidget<UBorder>();Entry.CostChip->SetBrush(HudRound(4.f,FLinearColor(.18f,.28f,.62f),1.f));Entry.CostChip->SetBrushColor(FLinearColor(.015f,.03f,.09f,.92f));
            Entry.CostChip->SetPadding(FMargin(5,0));Entry.CostChip->SetContent(Entry.Cost);Entry.CostChip->SetVisibility(ESlateVisibility::Collapsed);
            auto* CostSlot=Head->AddChildToHorizontalBox(Entry.CostChip);CostSlot->SetVerticalAlignment(VAlign_Center);CostSlot->SetPadding(FMargin(4,0,0,0));
            Entry.Name=Text(TEXT(""),15);HudFace(Entry.Name,false,1);Entry.Name->SetTextOverflowPolicy(ETextOverflowPolicy::MultilineEllipsis);Entry.Name->SetClipping(EWidgetClipping::ClipToBounds);
            auto* NameLine=Lines->AddChildToVerticalBox(Entry.Name);NameLine->SetSize(FSlateChildSize(ESlateSizeRule::Fill));NameLine->SetPadding(FMargin(0,2,0,0));
            Entry.State=Text(TEXT(""),13);HudFace(Entry.State,false,1);Lines->AddChildToVerticalBox(Entry.State);
            Entry.Frame=WidgetTree->ConstructWidget<UBorder>();Entry.Frame->SetPadding(FMargin(8,4));Entry.Frame->SetContent(Lines);
            // Over the face, in order: the cooldown mask shrinking with the remainder, the remaining time, the control badge and
            // the ready flash. None takes input; the button under them does.
            auto* Stack=WidgetTree->ConstructWidget<UOverlay>();
            auto FillLayer=[](UOverlaySlot* Layer){Layer->SetHorizontalAlignment(HAlign_Fill);Layer->SetVerticalAlignment(VAlign_Fill);};
            FillLayer(Stack->AddChildToOverlay(Entry.Frame));
            Entry.Cooldown=WidgetTree->ConstructWidget<UProgressBar>();
            FProgressBarStyle MaskStyle;MaskStyle.SetBackgroundImage(FSlateNoResource()).SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,6.f)).SetMarqueeImage(FSlateNoResource());
            Entry.Cooldown->SetWidgetStyle(MaskStyle);Entry.Cooldown->SetBarFillType(EProgressBarFillType::TopToBottom);Entry.Cooldown->SetFillColorAndOpacity(LDHUDStyle::SlotCooldownMask);
            Entry.Cooldown->SetPercent(0);Entry.Cooldown->SetVisibility(ESlateVisibility::Collapsed);FillLayer(Stack->AddChildToOverlay(Entry.Cooldown));
            Entry.Countdown=OneLine(TEXT(""),26,0);HudFace(Entry.Countdown,true,2);Entry.Countdown->SetJustification(ETextJustify::Center);Entry.Countdown->SetColorAndOpacity(FLinearColor::White);
            Entry.Countdown->SetVisibility(ESlateVisibility::Collapsed);
            auto* CountdownLayer=Stack->AddChildToOverlay(Entry.Countdown);CountdownLayer->SetHorizontalAlignment(HAlign_Center);CountdownLayer->SetVerticalAlignment(VAlign_Center);
            auto* BadgeText=OneLine(TEXT("控"),13,0);HudFace(BadgeText,true,0);BadgeText->SetColorAndOpacity(HudPromptInk);
            Entry.Badge=WidgetTree->ConstructWidget<UBorder>();Entry.Badge->SetBrush(HudRound(4.f,HudControlEdge,1.f));Entry.Badge->SetBrushColor(HudControlEdge);
            Entry.Badge->SetPadding(FMargin(5,0));Entry.Badge->SetContent(BadgeText);Entry.Badge->SetVisibility(ESlateVisibility::Collapsed);
            auto* BadgeLayer=Stack->AddChildToOverlay(Entry.Badge);BadgeLayer->SetHorizontalAlignment(HAlign_Right);BadgeLayer->SetVerticalAlignment(VAlign_Top);BadgeLayer->SetPadding(FMargin(0,5,8,0));
            Entry.Flash=WidgetTree->ConstructWidget<UBorder>();Entry.Flash->SetVisibility(ESlateVisibility::Collapsed);FillLayer(Stack->AddChildToOverlay(Entry.Flash));
            // The icon sends exactly what its hotkey sends; Combat decides Self casts and the armed target forms.
            const ELDCombatEntry Activated=HudActionEntries[Index];
            Entry.Button=LDWidgets::Button(WidgetTree,TEXT(""),[PC,Activated](){PC->ActivateCombatEntry(Activated);},true,HudActionNames[Index]);
            // The face draws every state itself, so the button keeps no chrome of its own.
            auto ButtonStyle=Entry.Button->GetStyle();ButtonStyle.Normal=FSlateNoResource();ButtonStyle.Hovered=FSlateNoResource();ButtonStyle.Pressed=FSlateNoResource();ButtonStyle.Disabled=FSlateNoResource();
            ButtonStyle.NormalPadding=FMargin(0);ButtonStyle.PressedPadding=FMargin(0);Entry.Button->SetStyle(ButtonStyle);
            Entry.Button->SetContent(Stack);
            if(UButtonSlot* StackSlot=Cast<UButtonSlot>(Stack->Slot)){StackSlot->SetPadding(FMargin(0));StackSlot->SetHorizontalAlignment(HAlign_Fill);StackSlot->SetVerticalAlignment(VAlign_Fill);}
            // 124 leaves a 108-unit text box (frame padding 8+8): five size-15 CJK glyphs per line, also with the rounded glyph
            // advances of the 720p UI scale; 106 holds the key row, two name lines and the state line.
            Entry.Box=WidgetTree->ConstructWidget<USizeBox>();Entry.Box->SetWidthOverride(HudSlotWidth);Entry.Box->SetHeightOverride(HudSlotHeight);Entry.Box->SetContent(Entry.Button);
            // Character actives (Q/W), equipment actives (1/2) and the innate dash (D) are separate groups, never one merged bar.
            Actions->AddChildToHorizontalBox(Entry.Box)->SetPadding(FMargin(Index==0?0.f:Index==2||Index==4?HudGroupGap:HudSlotGap,0,0,0));
        }
        Centre->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,8,0,0));
        auto* CentreWidth=WidgetTree->ConstructWidget<USizeBox>();CentreWidth->SetWidthOverride(HudCentreWidth);CentreWidth->SetContent(Centre);
        Bottom->AddChildToHorizontalBox(CentreWidth)->SetVerticalAlignment(VAlign_Bottom);
        Divider();
        auto* Utility=WidgetTree->ConstructWidget<UVerticalBox>();
        // Own panels open while dead as well (UI 3.2); the root keeps one main panel at a time.
        auto* PanelRow=WidgetTree->ConstructWidget<UHorizontalBox>();Utility->AddChildToVerticalBox(PanelRow)->SetPadding(FMargin(0,0,0,6));
        auto* Pick=LDWidgets::Button(WidgetTree,TEXT("待选"),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Choices);}},true,TEXT("HUDChoices"));
        auto* Sheet=LDWidgets::Button(WidgetTree,TEXT("属性"),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Attributes);}},true,TEXT("HUDAttributes"));
        auto* Dev=LDWidgets::Button(WidgetTree,TEXT("测试"),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::DevTest);}},true,TEXT("HUDDevTest"));
        Dev->SetVisibility(ESlateVisibility::Collapsed);
        Commands.Add(TEXT("Choices"),Pick);Commands.Add(TEXT("Attributes"),Sheet);Commands.Add(TEXT("DevTest"),Dev);
        PanelRow->AddChildToHorizontalBox(Pick)->SetPadding(FMargin(0,0,6,0));PanelRow->AddChildToHorizontalBox(Sheet);
        PanelRow->AddChildToHorizontalBox(Dev)->SetPadding(FMargin(6,0,0,0));
        auto* Stop=LDWidgets::Button(WidgetTree,TEXT("停止"),[PC](){PC->StopCombatAndOrders();},true,TEXT("HUDStop"));
        Commands.Add(TEXT("Stop"),Stop);Utility->AddChildToVerticalBox(Stop)->SetHorizontalAlignment(HAlign_Left);
        CameraText=Text(TEXT(""),14);CameraText->SetColorAndOpacity(LDHUDStyle::TextSecondary);Utility->AddChildToVerticalBox(CameraText)->SetPadding(FMargin(0,6,0,0));
        auto* UtilityWidth=WidgetTree->ConstructWidget<USizeBox>();UtilityWidth->SetWidthOverride(HudSideWidth);UtilityWidth->SetContent(Utility);
        Bottom->AddChildToHorizontalBox(UtilityWidth)->SetVerticalAlignment(VAlign_Fill);
        Band=LDWidgets::Panel(WidgetTree,Bottom,FMargin(HudBandPadX,10));HitPanels.Add(Band);

        // Above the band, bottom-up: the pending-choice pill over the Q/W group, the toast, then the armed prompt. The stack
        // grows upward from the band, so a taller band (text scale) never overlaps them; only the pill takes input.
        auto* BandStack=WidgetTree->ConstructWidget<UVerticalBox>();BandStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        ArmedText=OneLine(TEXT(""),16,640);ArmedText->SetColorAndOpacity(LDHUDStyle::TextAccent);
        ArmedPanel=Panel(ArmedText,false);ArmedPanel->SetVisibility(ESlateVisibility::Collapsed);
        auto* ArmedSlot=BandStack->AddChildToVerticalBox(ArmedPanel);ArmedSlot->SetHorizontalAlignment(HAlign_Center);ArmedSlot->SetPadding(FMargin(0,0,0,8));
        Toast=OneLine(TEXT(""),16,640);ToastPanel=Panel(Toast,false);ToastPanel->SetVisibility(ESlateVisibility::Collapsed);
        auto* ToastSlot=BandStack->AddChildToVerticalBox(ToastPanel);ToastSlot->SetHorizontalAlignment(HAlign_Center);ToastSlot->SetPadding(FMargin(0,0,0,8));
        // The pill opens the same Choices panel as 待选 and has no hotkey of its own; only its own rect is a hit panel.
        auto* Prompt=LDWidgets::Button(WidgetTree,TEXT(""),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Choices);}},true,TEXT("HUDChoicesPrompt"));
        PromptText=CastChecked<UTextBlock>(Prompt->GetContent());HudFace(PromptText,true,0);PromptText->SetColorAndOpacity(HudPromptInk);
        if(UButtonSlot* PromptSlot=Cast<UButtonSlot>(PromptText->Slot)){PromptSlot->SetPadding(FMargin(0));}
        auto PromptStyle=Prompt->GetStyle();PromptStyle.Normal=FSlateRoundedBoxBrush(HudPromptFill);PromptStyle.Hovered=FSlateRoundedBoxBrush(HudPromptHover);PromptStyle.Pressed=FSlateRoundedBoxBrush(HudPromptPress);
        PromptStyle.NormalPadding=FMargin(16,5);PromptStyle.PressedPadding=FMargin(16,5);Prompt->SetStyle(PromptStyle);
        PendingPrompt=WidgetTree->ConstructWidget<UBorder>();PendingPrompt->SetBrushColor(FLinearColor::Transparent);PendingPrompt->SetPadding(FMargin(0));PendingPrompt->SetContent(Prompt);
        PendingPrompt->SetVisibility(ESlateVisibility::Collapsed);HitPanels.Add(PendingPrompt);
        auto* PendingSlot=BandStack->AddChildToVerticalBox(PendingPrompt);PendingSlot->SetHorizontalAlignment(HAlign_Left);PendingSlot->SetPadding(FMargin(HudBandPadX+HudSideWidth+HudBlockGap+1,0,0,6));
        BandStack->AddChildToVerticalBox(Band)->SetHorizontalAlignment(HAlign_Center);
        Place(BandStack,FAnchors(.5f,1),FVector2D(.5f,1),FVector2D(0,-12));

        // Normal main panels fill the canvas with a hit-transparent frame, so the world beside them keeps its input.
        AttributesPanel=CreateWidget<ULDAttributesPanel>(PC);ChoicesPanel=CreateWidget<ULDChoicesPanel>(PC);ShopPanel=CreateWidget<ULDShopPanel>(PC);
        DevTestPanel=CreateWidget<ULDDevTestPanel>(PC);
        const TPair<ELDUIMainPanel,ULDPanel*> Docked[]={{ELDUIMainPanel::Attributes,AttributesPanel.Get()},{ELDUIMainPanel::Choices,ChoicesPanel.Get()},
            {ELDUIMainPanel::Shop,ShopPanel.Get()},{ELDUIMainPanel::DevTest,DevTestPanel.Get()}};
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
        if(DevTestPanel)
        {
            // The controller's single DEV test entry; the authority admits and commits every command.
            DevTestPanel->Submit=[WeakPC](ELDDevTestOp Op,int64 Amount,ELDGrowthReason& Reason,FName Definition,uint8 SlotIndex,uint8 Stat)->uint64
            {if(ALDPlayerController* Owner=WeakPC.Get()){return Owner->SubmitDevTest(Op,Amount,Reason,Definition,SlotIndex,Stat);}Reason=ELDGrowthReason::DataNotReady;return 0;};
            PC->OnDevTestResult.AddUObject(this,&ULDHUD::HandleDevTestResult);
        }
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
TArray<ULDPanel*,TInlineAllocator<4>> ULDHUD::MainPanels() const
{
    TArray<ULDPanel*,TInlineAllocator<4>> Result;
    for(ULDPanel* Panel:{static_cast<ULDPanel*>(AttributesPanel.Get()),static_cast<ULDPanel*>(ChoicesPanel.Get()),static_cast<ULDPanel*>(ShopPanel.Get()),
        static_cast<ULDPanel*>(DevTestPanel.Get())}){if(Panel){Result.Add(Panel);}}
    return Result;
}
void ULDHUD::HandleDevTestResult(const FLDDevTestResult& Result)
{
    if(DevTestPanel){DevTestPanel->HandleResult(Result);}
}
FReply ULDHUD::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{return IsUIAt(E.GetScreenSpacePosition())?FReply::Handled():Super::NativeOnMouseButtonDown(G,E);}
FReply ULDHUD::NativeOnMouseWheel(const FGeometry& G,const FPointerEvent& E)
{return IsUIAt(E.GetScreenSpacePosition())?FReply::Handled():Super::NativeOnMouseWheel(G,E);}
const FLDDemoContentDefinitions* ULDHUD::DisplayDefinitions()
{
#if WITH_DEV_AUTOMATION_TESTS
    // Under DEV content hot reload each match pins its own snapshot; the labels follow it instead of the first one.
    if(bDisplayContentTried&&LDDemoEntry::IsDevContentReload()){bDisplayContentTried=false;}
#endif
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
    // Cues follow every sample, including while a menu covers the HUD or the result is shown.
    ObserveAudio(*PC,*GS);
    int64 NowMs=0;const bool bClock=PC->ReadDemoAuthorityTimeMs(NowMs);
    const bool bAlive=O.LifePhase==ELDLifePhase::Alive;
    // Local presentation time for fades, flashes and pulses; it is never compared with an authority deadline.
    const double UiNow=GetWorld()->GetRealTimeSeconds();
    if(bOwnMatch&&bAlive&&O.Life!=0&&bClock&&(LifeSeenMatch!=O.Match||LifeSeenLife!=O.Life)){LifeSeenMatch=O.Match;LifeSeenLife=O.Life;LifeSeenAliveMs=NowMs;}
    // Full-viewport menus draw below this widget, so the HUD steps aside while they are open.
    const bool Overlay=(PC->ControlsUI&&PC->ControlsUI->IsOpen())||(PC->RootUI&&PC->RootUI->IsMenuOpen());
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
        for(UWidget* Hidden:{static_cast<UWidget*>(TargetCard.Get()),static_cast<UWidget*>(DeathBanner.Get()),static_cast<UWidget*>(ToastPanel.Get()),static_cast<UWidget*>(KillFeedPanel.Get()),
            static_cast<UWidget*>(ArmedPanel.Get()),static_cast<UWidget*>(PendingPrompt.Get())}){HudSetShown(Hidden,false,ESlateVisibility::Visible);}
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

    const LDHUD::FCircleView Circle=LDHUD::Circle(Public,LDHUD::RosterName(Lobby,Public.Occupier),bClock,NowMs,Content?Content->Parameters.CaptureHoldMs:0);
    HudSetText(PhaseText,Circle.Line);CircleProgress->SetPercent(Circle.Progress);
    HudSetText(DevMarkerText,!Lobby.DevTestSession&&!Lobby.DevContentReload?FString()
        :FString(Lobby.DevTestSession?TEXT("测试模式"):TEXT("开发房间"))+(Lobby.DevContentReload?TEXT(" · 内容热重载"):TEXT("")));
    HudSetShown(DevMarkerText,Lobby.DevTestSession||Lobby.DevContentReload,ESlateVisibility::HitTestInvisible);
    HudSetShown(CircleProgress->GetParent(),Circle.bProgress,ESlateVisibility::HitTestInvisible);
    HudSetText(NextStepText,LDHUD::NextStep(Public,O.ContenderId,PC->ReadOwnProgression(),PC->ReadOwnEquipment(),Content));
    HudSetShown(NextStepText,!NextStepText->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    RefreshKillFeed(*GS,O.ContenderId,Content,bClock,NowMs);
    const LDHUD::FDeathView Death=LDHUD::Death(O,bClock,NowMs);
    HudSetShown(DeathBanner,Death.bVisible,ESlateVisibility::HitTestInvisible);
    if(Death.bVisible)
    {
        HudSetText(DeathTitle,Death.Title);HudSetText(DeathCountdown,Death.Countdown);HudSetText(DeathNote,Death.Note);
        FString Low,High;HudCampNames(Content,Low,High);
        const int64 NotBeforeMs=LifeSeenMatch==O.Match&&LifeSeenLife==O.Life?LifeSeenAliveMs:0;
        const FString Cause=LDHUD::DeathCause(GS->KillFeed,Public.Generation,O.ContenderId,NotBeforeMs,Lobby,Low,High);
        HudSetText(DeathCauseText,Cause);HudSetShown(DeathCauseText,!Cause.IsEmpty(),ESlateVisibility::HitTestInvisible);
    }

    const FString Name=LDHUD::RosterName(Lobby,O.ContenderId);
    HudSetText(SelfName,Name.IsEmpty()?FString(TEXT("名单同步中")):Name);
    if(!SelfName->GetToolTipText().ToString().Equals(SelfName->GetText().ToString(),ESearchCase::CaseSensitive)){SelfName->SetToolTipText(SelfName->GetText());}
    HudSetText(PortraitGlyph,Name.IsEmpty()?FString(TEXT("?")):Name.Left(1));
    if(const auto* Body=Cast<ALDCharacter>(PC->GetPawn()))
    {const FLinearColor Identity=LDHUD::PaletteColor(Body->PresentationKey);if(Portrait->GetBrushColor()!=Identity){Portrait->SetBrushColor(Identity);}}
    // Plain alive needs no line; protection, death and the other phases keep theirs.
    const bool bPlainAlive=bAlive&&(O.ProtectedUntilMs<=0||(bClock&&O.ProtectedUntilMs<=NowMs));
    HudSetText(LifeText,LDHUD::LifeStatus(O,bClock,NowMs));HudSetShown(LifeText,!bPlainAlive,ESlateVisibility::HitTestInvisible);
    HudSetColor(LifeText,bAlive?LDHUDStyle::Shield:O.LifePhase==ELDLifePhase::Dead?LDHUDStyle::TextDanger:LDHUDStyle::TextSecondary);
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
    HudSetText(HealthRegenText,LDHUD::RegenText(O,OwnCombat,true));
    HudSetText(ManaRegenText,LDHUD::RegenText(O,OwnCombat,false));
    LDHUD::FLevelView Level;
    if(Progression)
    {Level=LDHUD::Level(Progression->Level,Progression->MaxLevel,Progression->CumulativeXP,Progression->LevelThresholdXP,Progression->bHasNextThreshold,Progression->NextThresholdXP);}
    else{Level.Level=TEXT("等级 —");Level.Experience=TEXT("成长数据同步中");}
    LevelBar->SetPercent(Level.Fraction);HudSetText(LevelText,Level.Level+TEXT(" · ")+Level.Experience);
    const bool bLevelKnown=Progression&&Progression->Level>=1&&Progression->Level<=Progression->MaxLevel;
    HudSetText(LevelBadgeText,bLevelKnown?FString::FromInt(Progression->Level):FString(TEXT("—")));
    HudSetText(GoldText,Equipment?LDHUD::Gold(Equipment->Gold):FString(TEXT("零花钱 同步中")));
    HudSetText(SelfStatusText,OwnCombat?LDHUD::SelfStatusLine(*OwnCombat,bClock,NowMs,Content):FString());
    HudSetShown(SelfStatusText,!SelfStatusText->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    // A missing projection still reads as syncing; known projections without any passive leave the line out.
    bool bPassive=!Progression||!OwnCombat||!OwnCombat->EquipmentPassives.IsEmpty();
    if(Progression){for(const FLDLearnedSkillView& Skill:Progression->Skills){bPassive=bPassive||!Skill.bActive;}}
    HudSetText(PassiveText,LDHUD::PassiveLine(Progression,OwnCombat,Content));HudSetShown(PassiveText,bPassive,ESlateVisibility::HitTestInvisible);

    // New world commands follow Combat's input half (menu/Settings, child confirmation or text edit, held keys);
    // Combat itself refuses and cancels any armed preview while input is captured.
    const bool bInputSuppressed=PC->IsWorldInputSuppressed();
    if(AttributesPanel){AttributesPanel->Present(OwnCombat,Progression,bClock,NowMs,Content);}
    if(ChoicesPanel){ChoicesPanel->Present(Progression?*Progression:FLDProgressionView(),O.Match,O.Life,O.LifePhase,Content);}
    // D10: the opening three-choice opens its panel once per match, when its offer first reaches this owner and nothing
    // else is open; it never opens later over the player's own UI or armed preview, and nothing is picked for the player.
    // Growth may publish the match's progression before the opening offer, so the one chance waits for the offer itself.
    // The root may still be leaving the lobby on the match's first frames, so a refused open is retried for a short
    // while; the player's own panel or captured input ends the chance at once.
    if(AutoChoicesMatch!=O.Match&&AutoChoicesPending!=O.Match){AutoChoicesPending=O.Match;AutoChoicesSince=-1;}
    if(ChoicesPanel&&PC->RootUI&&Progression&&Progression->Generation==O.Match&&AutoChoicesMatch!=O.Match
        &&(Progression->bHasOffer||Progression->ConsumedThrough>0))
    {
        const bool bOpening=Progression->bHasOffer&&Progression->ConsumedThrough==0&&Progression->Offer.OpportunityOrdinal==1;
        const bool bPlayerUI=PC->RootUI->ActiveMainPanel()!=ELDUIMainPanel::None||PC->IsWorldInputCaptured();
        const double UiTime=GetWorld()->GetRealTimeSeconds();
        if(!bOpening||bPlayerUI){AutoChoicesMatch=O.Match;}
        else if(PC->RootUI->ShowMainPanel(ELDUIMainPanel::Choices)){AutoChoicesMatch=O.Match;ChoicesPanel->MarkOpenedAutomatically();}
        else if(AutoChoicesSince<0){AutoChoicesSince=UiTime;}
        else if(UiTime-AutoChoicesSince>5.)
        {
            AutoChoicesMatch=O.Match;
            UE_LOG(LogTemp,Warning,TEXT("LD_HUD_OPENING_CHOICE_NOT_SHOWN The opening three-choice panel could not open within 5 s."));
        }
    }
    if(ChoicesPanel)
    {
        // The panel docks over the right side of the battlefield: a hit on the own body, or the opening's automatic
        // open running its few seconds unattended, folds it back to the pending pill (LDHUD::FChoicesFold).
        LDWorldFeedback::FSample Body;Body.Match=O.Match;Body.Life=O.Life;Body.LifePhase=O.LifePhase;Body.Self=O.ContenderId;Body.OwnCombat=OwnCombat;
        TArray<LDWorldFeedback::FEvent> BodyEvents;OwnHitWatch.Observe(Body,BodyEvents);
        const bool bOwnHit=BodyEvents.ContainsByPredicate([](const LDWorldFeedback::FEvent& Event)
        {return Event.bOwn&&(Event.Kind==LDWorldFeedback::EKind::DamageTaken||Event.Kind==LDWorldFeedback::EKind::ShieldAbsorbed);});
        ChoicesPanel->UpdateFold(bOwnHit);
    }
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
    if(DevTestPanel)
    {
        // Only a DEV test room offers the panel; the Host alone sees the match-wide actions, which the server checks again.
        if(!Lobby.DevTestSession&&DevTestPanel->IsPanelOpen()){DevTestPanel->ClosePanel();}
        if(Lobby.DevTestSession)
        {
            DevTestPanel->Present(Progression,Equipment,OwnCombat,Content,PC->IsLocalController()&&PC->HasAuthority());
        }
    }
    if(ULDCommandButton* Dev=Commands.FindRef(TEXT("DevTest"))){HudSetShown(Dev,Lobby.DevTestSession,ESlateVisibility::Visible);}
    const int32 Pending=Progression?int32(Progression->PendingChoiceCount):0;
    if(ULDCommandButton* Pick=Commands.FindRef(TEXT("Choices")))
    {
        // Zero pending choices need neither a count nor a tint; any pending choice is counted and tinted.
        HudSetText(CastChecked<UTextBlock>(Pick->GetContent()),!Progression?FString(TEXT("待选 —")):Pending>0?FString::Printf(TEXT("待选 %d"),Pending):FString(TEXT("待选")));
        HudTint(Pick,Pending>0?FLinearColor(.36f,.27f,.06f):HudButtonRest);
    }
    if(ULDCommandButton* Sheet=Commands.FindRef(TEXT("Attributes"))){HudSetText(CastChecked<UTextBlock>(Sheet->GetContent()),TEXT("属性 [")+PC->BindingLabel(TEXT("Attributes"))+TEXT("]"));}
    // The pill over Q/W exists only with a pending choice; after the count rises it pulses at ~1 Hz for a few seconds.
    if(Pending>PromptCount){PromptPulseAt=UiNow;}
    PromptCount=Pending;
    HudSetShown(PendingPrompt,Pending>0,ESlateVisibility::SelfHitTestInvisible);
    if(Pending>0)
    {
        HudSetText(PromptText,FString::Printf(TEXT("可选成长 ×%d · 点击选择"),Pending));
        const double PulseAge=UiNow-PromptPulseAt;
        const float Wave=PromptPulseAt>=0&&PulseAge<HudPulseSeconds?.5f-.5f*FMath::Cos(2.f*UE_PI*float(PulseAge)):0.f;
        if(AppliedPromptWave!=Wave){AppliedPromptWave=Wave;const float Scale=1.f+.05f*Wave;PendingPrompt->SetRenderScale(FVector2D(Scale,Scale));PendingPrompt->SetRenderOpacity(1.f-.25f*Wave);}
    }

    ELDCombatEntry ArmedEntry=ELDCombatEntry::None;const bool bArmed=PC->ReadArmedCombatEntry(ArmedEntry);
    if(PC->CombatReceipts.Serial()!=SeenCombatReceipt)
    {
        TArray<FLDCombatActionReceipt> Arrived;
        if(const uint32 Lost=PC->CombatReceipts.ReadSince(SeenCombatReceipt,Arrived)){UE_LOG(LogTemp,Warning,TEXT("LD_HUD_COMBAT_RECEIPTS_MISSED %u"),Lost);}
        // Every receipt since the last frame presents once; the single toast line joins their distinct notices in order.
        const FString Notice=LDHUD::CombatReceiptsNotice(Arrived,O.Match,O.Life,ToldProtectionLifeRevision);
        if(!Notice.IsEmpty()){ShowToast(Notice);}
        for(const FLDCombatActionReceipt& Receipt:Arrived)
        {
            if(Receipt.Generation!=O.Match||Receipt.Life!=O.Life){continue;}
            // The pressed slot repeats the reason where the player is looking; the toast keeps the full notice.
            const FString SlotNotice=LDHUD::CombatReceiptSlotNotice(Receipt);
            for(int32 Pressed=0;Pressed<ActionSlots.Num()&&!SlotNotice.IsEmpty();++Pressed)
            {if(HudActionEntries[Pressed]==Receipt.Entry){ActionSlots[Pressed].Notice=SlotNotice;ActionSlots[Pressed].NoticeAt=UiNow;}}
        }
    }
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
        Context.bInputCaptured=bInputSuppressed;
        Context.bManaKnown=OwnCombat&&OwnCombat->bHasBody&&OwnCombat->MaxMPP2>0;Context.ManaP2=OwnCombat?OwnCombat->MPP2:0;
        const LDHUD::FActionView View=LDHUD::Action(Input,Context);
        FLDHUDActionSlot& Entry=ActionSlots[Index];
        const bool bThisArmed=bArmed&&ArmedEntry==Wanted;
        if(bThisArmed){ArmedName=View.Name;}
        const LDHUD::FActionLookView Look=LDHUD::ActionLook(View,bThisArmed);
        const bool bCooling=View.CooldownRemainingMs>0;
        // A cooldown that runs out on a usable slot within the same life flashes that slot once.
        if(Entry.bCooling&&!bCooling&&Entry.CoolingMatch==O.Match&&Entry.CoolingLife==O.Life&&Look.Look==LDHUD::EActionLook::Ready){Entry.FlashAt=UiNow;}
        Entry.bCooling=bCooling;Entry.CoolingMatch=O.Match;Entry.CoolingLife=O.Life;
        const FHudLookStyle Style=HudLookStyle(Look.Look);
        const bool bHover=Entry.Button->IsHovered();
        const int32 LookKey=int32(Look.Look)*2+(bHover?1:0);
        if(Entry.AppliedLook!=LookKey)
        {
            // Hovering brightens the edge; the fill keeps naming the state.
            Entry.AppliedLook=LookKey;
            const FLinearColor Edge=bHover?Style.Edge+(FLinearColor::White-Style.Edge)*.4f:Style.Edge;
            Entry.Frame->SetBrush(HudRound(6.f,Edge,bHover?FMath::Max(Style.EdgeWidth,2.f):Style.EdgeWidth));Entry.Frame->SetBrushColor(Style.Fill);
        }
        const bool bNotice=!Entry.Notice.IsEmpty()&&UiNow-Entry.NoticeAt<HudNoticeSeconds;
        HudSetText(Entry.Key,View.Key);HudSetText(Entry.Name,View.Name);HudSetColor(Entry.Name,Style.Name);
        HudSetText(Entry.State,bNotice?Entry.Notice:Look.Caption);HudSetColor(Entry.State,bNotice?LDHUDStyle::TextDanger:Style.Caption);
        HudSetText(Entry.Cost,View.Cost);HudSetShown(Entry.CostChip,!View.Cost.IsEmpty()&&Look.Look!=LDHUD::EActionLook::Controlled,ESlateVisibility::HitTestInvisible);
        HudSetShown(Entry.Badge,Look.Look==LDHUD::EActionLook::Controlled,ESlateVisibility::HitTestInvisible);
        Entry.Cooldown->SetPercent(View.CooldownFraction);HudSetShown(Entry.Cooldown,bCooling,ESlateVisibility::HitTestInvisible);
        HudSetText(Entry.Countdown,View.Countdown);HudSetShown(Entry.Countdown,bCooling,ESlateVisibility::HitTestInvisible);
        // An empty slot or a source without an active fades as a whole; the ready flash pops the slot and fades out.
        const float Opacity=Look.Look==LDHUD::EActionLook::Empty?.45f:1.f;
        if(Entry.AppliedOpacity!=Opacity){Entry.AppliedOpacity=Opacity;Entry.Box->SetRenderOpacity(Opacity);}
        const double FlashAge=Entry.FlashAt>=0?UiNow-Entry.FlashAt:LDHUDStyle::ReadyFlashSeconds;
        const bool bFlash=FlashAge<LDHUDStyle::ReadyFlashSeconds;
        const float FlashPhase=bFlash?float(FlashAge/LDHUDStyle::ReadyFlashSeconds):1.f;
        HudSetShown(Entry.Flash,bFlash,ESlateVisibility::HitTestInvisible);
        if(bFlash)
        {
            const float Fade=FMath::Square(1.f-FlashPhase);
            Entry.Flash->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,6.f,LDHUDStyle::SlotReadyFlash.CopyWithNewOpacity(Fade),3.f));
            Entry.Flash->SetBrushColor(LDHUDStyle::SlotReadyFlash.CopyWithNewOpacity(.35f*Fade));
        }
        else{Entry.FlashAt=-1;}
        const float Pop=bFlash?1.f+.08f*FMath::Sin(UE_PI*FlashPhase):1.f;
        if(Entry.AppliedPop!=Pop){Entry.AppliedPop=Pop;Entry.Box->SetRenderScale(FVector2D(Pop,Pop));}
        Entry.Button->SetIsEnabled(View.bCanSubmitIntent);
        if(ActionTips[Index]!=View.Tooltip){ActionTips[Index]=View.Tooltip;Entry.Button->SetToolTipText(FText::FromString(View.Tooltip));}
    }
    FString ArmedLine;
    if(bArmed){ArmedLine=(ArmedName.IsEmpty()?LDHUD::EntryLabel(ArmedEntry):ArmedName)+TEXT(" 准备中：左键确认目标 · 右键或 ")+PC->BindingLabel(TEXT("Back"))+TEXT(" 取消");}
    else if(PC->IsAttackMoveArmed()){ArmedLine=TEXT("攻击移动：左键选择地点或目标 · 右键或 ")+PC->BindingLabel(TEXT("Back"))+TEXT(" 取消");}
    HudSetText(ArmedText,ArmedLine);HudSetShown(ArmedPanel,!ArmedLine.IsEmpty(),ESlateVisibility::HitTestInvisible);
    // Stop is a new world command (same halves as S); the panel buttons open own panels and stay available while dead.
    for(auto& Pair:Commands){Pair.Value->SetIsEnabled(Pair.Key!=TEXT("Stop")||PC->CanWorldCommand());}
    if(ULDCommandButton* Stop=Commands.FindRef(TEXT("Stop"))){HudSetText(CastChecked<UTextBlock>(Stop->GetContent()),TEXT("停止 [")+PC->BindingLabel(TEXT("Stop"))+TEXT("]"));}
    HudSetText(CameraText,PC->Navigation->bFollowCamera?FString(TEXT("镜头：跟随")):TEXT("镜头：自由 · ")+PC->BindingLabel(TEXT("Follow"))+TEXT(" 跟随"));

    const ALDWorldViewRelay* Relay=PC->LocalView();
    const FLDObservedFrameRead Frame=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
    const bool bSightCurrent=Frame.Frame&&Frame.Status==ELDObservedViewStatus::Current;
    // The map already shows a normal current view; only a syncing, stale or dead view needs words under it.
    HudSetText(SightText,!bAlive?FString(TEXT("社死：仅显示静态地图与公开信息"))
        :bSightCurrent?FString::Printf(TEXT("视野：当前可见 %d 个单位"),Frame.Frame->Units.Num())
        :Frame.Status==ELDObservedViewStatus::Stale?FString(TEXT("视野：已过期，等待刷新")):FString(TEXT("视野：同步中")));
    HudSetShown(SightText,!bAlive||!bSightCurrent,ESlateVisibility::HitTestInvisible);

    FLDObservedUnitView Subject;const bool bSelected=PC->ReadInspected(Subject);
    HudSetShown(TargetCard,bSelected,ESlateVisibility::Visible);
    if(bSelected)
    {
        const FString SubjectName=Subject.Kind==ELDObservedUnitKind::Contender?LDHUD::RosterName(Lobby,Subject.ContenderId):FString();
        const LDHUD::FObservedCard Card=LDHUD::ObservedCard(Subject,SubjectName,Content,Frame.Frame?Frame.Frame->SampledAtServerSec:0.);
        HudSetText(TargetTitle,Card.Title);HudSetText(TargetGlyph,Card.Title.Left(1));
        TargetPortrait->SetBrushColor(Subject.Kind==ELDObservedUnitKind::Contender?LDHUD::PaletteColor(Subject.Presentation):ULDHumanoidVisualComponent::MonsterTint(Subject.CreatureId));
        // The bar speaks the shared language: other contenders red, camp members amber (the own body would read green).
        const bool bContender=Subject.Kind==ELDObservedUnitKind::Contender;
        const FLinearColor HealthFill=!bContender?LDHUDStyle::CampHealth:Subject.ContenderId==O.ContenderId?LDHUDStyle::OwnHealth:LDHUDStyle::EnemyHealth;
        if(TargetHealthBar->GetFillColorAndOpacity()!=HealthFill){TargetHealthBar->SetFillColorAndOpacity(HealthFill);}
        TargetHealthBar->SetPercent(Card.Health.Fraction);HudSetText(TargetHealth,Card.Health.Text);
        HudSetShown(TargetManaRow,Card.bMana,ESlateVisibility::SelfHitTestInvisible);
        if(Card.bMana){TargetManaBar->SetPercent(Card.Mana.Fraction);HudSetText(TargetMana,Card.Mana.Text);}
        HudSetText(TargetStatuses,Card.Statuses);
        HudSetShown(TargetEquipment,Card.bEquipment,ESlateVisibility::HitTestInvisible);
        if(Card.bEquipment){HudSetText(TargetEquipment,Card.Equipment);}
    }

    // The toast fades on local presentation time.
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
    if(const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer())){TArray<ELDAudioCue> Cues;AudioCues.ObserveShopSubmit(Result,PC->OwnerState.Match,Cues);PlayAudioCues(Cues);}
}
void ULDHUD::HandleChoiceReceipt(const FLDChoiceReceiptQueryResult& Result){if(ChoicesPanel){ChoicesPanel->HandleReceipt(Result);}}
void ULDHUD::HandleShopReceipt(const FLDShopReceiptQueryResult& Result)
{
    if(ShopPanel){ShopPanel->HandleReceipt(Result);}
    if(const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer())){TArray<ELDAudioCue> Cues;AudioCues.ObserveShopReceipt(Result,PC->OwnerState.Match,Cues);PlayAudioCues(Cues);}
}
void ULDHUD::ObserveAudio(ALDPlayerController& PC,const ALDGameState& Public)
{
    // Only room-public DTOs and the player's own projection/receipts: no observed unit, actor or other owner state.
    const FLDOwnerState& O=PC.OwnerState;
    FLDAudioFrame Frame;
    Frame.Public=&Public.PublicMatch;Frame.KillFeed=&Public.KillFeed;Frame.Result=&Public.MatchResult;
    Frame.Self=O.ContenderId;Frame.OwnMatch=O.Match;Frame.OwnLife=O.Life;Frame.OwnLifePhase=O.LifePhase;
    Frame.OwnCombat=&PC.CombatView;Frame.OwnProgression=PC.ReadOwnProgression();
    Frame.bClock=PC.ReadDemoAuthorityTimeMs(Frame.NowMs);Frame.LocalSeconds=GetWorld()->GetRealTimeSeconds();
    TArray<ELDAudioCue> Cues;
    AudioCues.Observe(Frame,Cues);
    // A receipt that arrived before this HUD existed is not replayed.
    if(!bAudioPrimed){bAudioPrimed=true;AudioCombatReceipt=PC.CombatReceipts.Serial();}
    // Every receipt since the last frame is observed, not only the newest: an earlier request's release still sounds.
    if(PC.CombatReceipts.Serial()!=AudioCombatReceipt)
    {
        TArray<FLDCombatActionReceipt> Arrived;
        if(const uint32 Lost=PC.CombatReceipts.ReadSince(AudioCombatReceipt,Arrived)){UE_LOG(LogTemp,Warning,TEXT("LD_AUDIO_COMBAT_RECEIPTS_MISSED %u"),Lost);}
        for(const FLDCombatActionReceipt& Receipt:Arrived){AudioCues.ObserveCombatReceipt(Receipt,O.Match,O.Life,Cues);}
    }
    PlayAudioCues(Cues);
}
void ULDHUD::PlayAudioCues(const TArray<ELDAudioCue>& Cues)
{
    if(Cues.IsEmpty()){return;}
    UGameInstance* Instance=GetGameInstance();ULDAudioSubsystem* Audio=Instance?Instance->GetSubsystem<ULDAudioSubsystem>():nullptr;
    if(!Audio){UE_LOG(LogTemp,Warning,TEXT("LD_AUDIO_SUBSYSTEM_UNAVAILABLE %d cue(s) not played."),Cues.Num());return;}
    for(const ELDAudioCue Cue:Cues){Audio->Play(Cue);}
}
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
void ULDHUD::RefreshKillFeed(const ALDGameState& Public,const FGuid& Self,const FLDDemoContentDefinitions* Content,bool bClock,int64 NowMs)
{
    const FLDKillFeedView& Feed=Public.KillFeed;const double UiNow=GetWorld()->GetRealTimeSeconds();
    if(Feed.Generation!=KillGeneration){KillGeneration=Feed.Generation;KillSerial=0;KillLines.Reset();}
    if(Feed.Generation&&Feed.Generation==Public.PublicMatch.Generation)
    {
        FString Low,High;HudCampNames(Content,Low,High);
        TArray<const FLDKillNotice*> Fresh;
        for(const FLDKillNotice& Notice:Feed.Notices){if(Notice.Serial>KillSerial){Fresh.Add(&Notice);}}
        Fresh.Sort([](const FLDKillNotice& A,const FLDKillNotice& B){return A.Serial<B.Serial;});
        for(const FLDKillNotice* Notice:Fresh)
        {
            KillSerial=Notice->Serial;
            // Notices already old when first seen (e.g. a late join) are marked seen without a banner.
            if(bClock&&NowMs-Notice->OccurredAtMs>6000){continue;}
            const LDHUD::FKillFeedLineView Shown=LDHUD::KillFeedLine(*Notice,Self,LDHUD::RosterName(Public.DemoLobby,Notice->Victim),LDHUD::RosterName(Public.DemoLobby,Notice->Killer),Low,High);
            FLDHUDKillLine& Line=KillLines.AddDefaulted_GetRef();Line.At=UiNow;Line.Text=Shown.Text;Line.bSelf=Shown.bSelf;
        }
    }
    KillLines.RemoveAll([UiNow](const FLDHUDKillLine& Line){return UiNow-Line.At>=6;});
    while(KillLines.Num()>KillFeedRows.Num()){KillLines.RemoveAt(0);}
    // One row per notice, so the lines about the player (said with 你) can stand out from the rest of the room.
    for(int32 Row=0;Row<KillFeedRows.Num();++Row)
    {
        UTextBlock* FeedLine=KillFeedRows[Row];const bool bLine=KillLines.IsValidIndex(Row);
        HudSetText(FeedLine,bLine?KillLines[Row].Text:FString());HudSetColor(FeedLine,bLine&&KillLines[Row].bSelf?LDHUDStyle::TextAccent:LDHUDStyle::TextPrimary);
        HudSetShown(FeedLine,bLine,ESlateVisibility::HitTestInvisible);
    }
    HudSetShown(KillFeedPanel,!KillLines.IsEmpty(),ESlateVisibility::HitTestInvisible);
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
    auto RangeRing=[&](const FVector& Feet,float RangeCm,const FLinearColor& Color,float Thickness)
    {
        TArray<FVector2D> Ring;
        for(int32 Step=0;Step<=64;++Step)
        {
            const float Angle=2.f*PI*Step/64.f;FVector2D Point;
            if(!Project(Feet+FVector(FMath::Cos(Angle)*RangeCm,FMath::Sin(Angle)*RangeCm,0),Point)){return;}
            Ring.Add(Point);
        }
        Lines(Ring,Color,Thickness);
    };
    const FLDOwnerState& Own=PC->OwnerState;
    const bool bOwnCombat=Self&&Own.Life!=0&&PC->CombatView.bHasBody&&PC->CombatView.Generation==Own.Match&&PC->CombatView.Life==Own.Life;
    if(bOwnCombat&&!bArmedEntry&&!PC->IsAttackMoveArmed()&&PC->CombatView.AttackRangeCm>0)
    {
        // A faint standing ring of the own 弹脑瓜崩 reach (the projected attack range, so range growth shows at once).
        const FVector Feet=Self->GetActorLocation()-FVector(0,0,Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        RangeRing(Feet,PC->CombatView.AttackRangeCm,FLinearColor(1.f,1.f,1.f,.22f),1.2f);
    }
    if(Self&&(bArmedEntry||PC->IsAttackMoveArmed()))
    {
        float RangeCm=0;bool bDirection=false,bUnitTarget=false,bPoint=false;
        if(bArmedEntry)
        {
            const FLDCombatEntryView* Source=PC->CombatView.Entries.FindByPredicate([Armed](const FLDCombatEntryView& Candidate){return Candidate.Entry==Armed;});
            RangeCm=Source?float(Source->RangeCm):0.f;
            bDirection=Source&&(Source->TargetMode==uint8(ELDDemoTargetMode::DirectionProjectile)||Source->TargetMode==uint8(ELDDemoTargetMode::DirectionDash));
            bUnitTarget=Source&&Source->TargetMode==uint8(ELDDemoTargetMode::Unit);
            // A ground point previews where the jump would land: the pointer, clamped to the reach.
            bPoint=Source&&Source->TargetMode==uint8(ELDDemoTargetMode::GroundPoint);bDirection|=bPoint;
        }
        else{RangeCm=PC->CombatView.AttackRangeCm;bUnitTarget=true;}
        const FVector Feet=Self->GetActorLocation()-FVector(0,0,Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        FLinearColor Preview(1,.85f,.45f,.9f);
        if(bUnitTarget&&RangeCm>0)
        {
            // The unit the pointer would pick (the same selection as a click), from the current authorized frame only:
            // green when its feet are within the server's foot-to-foot range, red otherwise.
            FGuid Handle;uint32 Epoch=0;FLDObservedUnitView Hovered;
            const ALDWorldViewRelay* Relay=PC->LocalView();
            if(PC->ObservedUnderPointer(Handle,Epoch)&&Relay&&Relay->ReadUnit(Handle,Epoch,Hovered))
            {
                const bool bInRange=FVector::Dist2D(Feet,FVector(Hovered.FeetCm))<=RangeCm;
                Preview=bInRange?FLinearColor(.35f,1.f,.45f,.95f):FLinearColor(1.f,.35f,.3f,.95f);
                FVector Presented;FVector2D Mark;
                if(Relay->ReadPresentedLocation(Handle,Epoch,Presented)&&Project(Presented,Mark))
                {Lines({Mark+FVector2D(-26,-34),Mark+FVector2D(26,-34),Mark+FVector2D(26,34),Mark+FVector2D(-26,34),Mark+FVector2D(-26,-34)},Preview,2.5f);}
            }
        }
        if(RangeCm>0){RangeRing(Feet,RangeCm,Preview,1.5f);}
        FVector Origin,Direction;
        if(bDirection&&PC->DeprojectMousePositionToWorld(Origin,Direction)&&FMath::Abs(Direction.Z)>KINDA_SMALL_NUMBER)
        {
            // The cursor ray meets the feet plane; only its planar direction is previewed, at the entry's range when known.
            const double Along=(Feet.Z-Origin.Z)/Direction.Z;
            FVector Planar=Origin+Direction*Along-Feet;Planar.Z=0;
            if(Along>0&&!Planar.IsNearlyZero())
            {
                const FVector End=Feet+(RangeCm>0?Planar.GetSafeNormal()*(bPoint?FMath::Min(double(RangeCm),Planar.Size()):double(RangeCm)):Planar);
                FVector2D From,To;
                if(Project(Feet,From)&&Project(End,To)){Lines({From,To},Preview,3.f);}
            }
        }
    }
    return Base+1;
}
