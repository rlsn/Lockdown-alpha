#include "UI/LDHUD.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
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
#include "Character/LDCharacterMovementComponent.h"
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
#include "Components/ScaleBox.h"
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
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"

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
// The band's slots in LDHUD::EActionSlot order. The two equipment slots show the held items: no key, no combat entry.
const FName HudActionBindings[]={TEXT("Skill1"),TEXT("Skill2"),NAME_None,NAME_None,TEXT("Dash")};
const ELDCombatEntry HudActionEntries[]={ELDCombatEntry::Q,ELDCombatEntry::W,ELDCombatEntry::None,ELDCombatEntry::None,ELDCombatEntry::Dash};
const FName HudActionNames[]={TEXT("HUDActionQ"),TEXT("HUDActionW"),TEXT("HUDEquipment1"),TEXT("HUDEquipment2"),TEXT("HUDActionDash")};
constexpr int32 HudActionCount=UE_ARRAY_COUNT(HudActionEntries);
// Status control bits that forbid each entry kind: character actives cast, the dash displaces.
const uint8 HudActionControl[]={uint8(ELDCombatControlBlock::Cast),uint8(ELDCombatControlBlock::Cast),0,0,uint8(ELDCombatControlBlock::Displacement)};
static_assert(UE_ARRAY_COUNT(HudActionBindings)==HudActionCount && UE_ARRAY_COUNT(HudActionNames)==HudActionCount
    && UE_ARRAY_COUNT(HudActionControl)==HudActionCount,"Every HUD action entry has a binding, a name and a control bit.");
// Band geometry in Slate units at the 1080p reference (720p draws the same layout at ~0.667). The band is placed so its
// centre block, the 2+2+1 action entries over the resources, sits on the screen centre directly under the character.
// #97: the whole band draws at HudBandScale of these sizes (user: "shrink them to about 75%").
constexpr float HudBandScale=.75f;
// A press on a slot becomes a drag once the pointer moves this far (screen units).
constexpr float HudDragStartPx=8.f;
constexpr float HudSideWidth=300.f;
// The held toast of the #97 replacement pick.
const TCHAR* const LDHUDReplacePrompt=TEXT("选择要替换的技能");
// The own status grid under the 属性 button: LDHUD::SelfIconColumns squares fill the side column.
constexpr float HudSelfIconSize=44.f;
constexpr float HudSelfIconGap=5.f;
static_assert(LDHUD::SelfIconColumns*HudSelfIconSize+(LDHUD::SelfIconColumns-1)*HudSelfIconGap<=HudSideWidth,"The status grid fits the side column.");
constexpr float HudBandPadX=14.f;
constexpr float HudBlockGap=16.f;
// Square slots: the icon fills the face inside a 3u state ring; the key, rank and cost ride on its corners.
constexpr float HudSlotWidth=96.f;
constexpr float HudSlotHeight=96.f;
constexpr float HudSlotGap=8.f;
constexpr float HudGroupGap=24.f;
constexpr float HudCentreWidth=5*HudSlotWidth+2*HudSlotGap+2*HudGroupGap;
constexpr double HudNoticeSeconds=2.;
constexpr float HudDetailWidth=300.f;
constexpr double HudPulseSeconds=6.;
const FLinearColor HudReadyEdge(FColor(0x7F,0xE0,0xD0));
const FLinearColor HudManaText(FColor(0x8F,0xB0,0xFF));
const FLinearColor HudControlEdge(FColor(0xFF,0x6A,0x3D));
const FLinearColor HudPromptFill(FColor(0xFF,0xC8,0x3D));
const FLinearColor HudPromptHover(FColor(0xFF,0xD8,0x66));
const FLinearColor HudPromptPress(FColor(0xE6,0xAE,0x2A));
const FLinearColor HudPromptInk(FColor(0x1A,0x13,0x00));
const FLinearColor HudPromptIdle(.16f,.17f,.19f);
const FLinearColor HudPromptIdleInk(.52f,.55f,.58f);
// The corner buttons (blind box, shop) share one shape, size and colour scheme: lit is the gold fill, greyed out is the
// idle grey (disabled), and the shop entry out of range keeps the shared button face (still clickable).
constexpr float HudCornerRadius=6.f;
FButtonStyle HudCornerStyle(const FButtonStyle& Shared,bool bLit)
{
    FButtonStyle Style=Shared;
    if(bLit)
    {
        Style.Normal=FSlateRoundedBoxBrush(HudPromptFill,HudCornerRadius);Style.Hovered=FSlateRoundedBoxBrush(HudPromptHover,HudCornerRadius);
        Style.Pressed=FSlateRoundedBoxBrush(HudPromptPress,HudCornerRadius);
    }
    Style.Disabled=FSlateRoundedBoxBrush(HudPromptIdle,HudCornerRadius);
    Style.NormalPadding=FMargin(16,10);Style.PressedPadding=FMargin(16,10);
    return Style;
}
struct FHudLookStyle
{
    FLinearColor Fill;
    FLinearColor Edge;
    float EdgeWidth=1.f;
};
// The ring round the icon names the state: ready, armed and controlled slots get a crisp edge, the rest a quiet one.
FHudLookStyle HudLookStyle(LDHUD::EActionLook Look)
{
    switch(Look)
    {
    case LDHUD::EActionLook::Ready: return {LDHUDStyle::SlotReady,HudReadyEdge,2.f};
    case LDHUD::EActionLook::Armed: return {LDHUDStyle::SlotArmed,LDHUDStyle::TextAccent,3.f};
    case LDHUD::EActionLook::Cooling: return {LDHUDStyle::SlotReady,FLinearColor(.10f,.26f,.28f),1.f};
    case LDHUD::EActionLook::NoMana: return {LDHUDStyle::SlotNoMana,FLinearColor(.18f,.28f,.62f),2.f};
    case LDHUD::EActionLook::Controlled: return {LDHUDStyle::SlotControlled,HudControlEdge,3.f};
    default: return {LDHUDStyle::SlotBlocked,FLinearColor(.10f,.13f,.16f),1.f};
    }
}
// Placeholder icon colours, shared with the shop and blind-box icons: actives orange, a source without an active blue;
// the innate dash is slate and an empty slot dark grey.
enum class EHudIconKind : uint8 { Active, Passive, Dash, Empty };
FLinearColor HudIconBase(EHudIconKind Kind)
{
    switch(Kind)
    {
    case EHudIconKind::Active: return FLinearColor(.62f,.22f,.04f);
    case EHudIconKind::Passive: return FLinearColor(.08f,.28f,.52f);
    case EHudIconKind::Dash: return FLinearColor(.20f,.26f,.32f);
    default: return FLinearColor(.12f,.13f,.15f);
    }
}
// The icon also carries the state: missing mana turns it blue, control and blocks drain its colour; the cooldown mask
// darkens a cooling icon on its own.
FLinearColor HudIconColor(EHudIconKind Kind,LDHUD::EActionLook Look)
{
    const FLinearColor Base=HudIconBase(Kind);
    switch(Look)
    {
    case LDHUD::EActionLook::NoMana: return FMath::Lerp(Base,FLinearColor(.10f,.20f,.75f),.7f);
    case LDHUD::EActionLook::Controlled: return Base.Desaturate(.6f)*.6f;
    case LDHUD::EActionLook::Blocked: return Base.Desaturate(1.f)*.45f;
    default: return Base;
    }
}
// Camp tiers are named after their approved creature, never a guessed or hidden attacker.
void HudCampNames(const FLDDemoContentDefinitions* Content,FString& Low,FString& High,FString& Boss)
{
    Low=LDTerms::CampLow;High=LDTerms::CampHigh;Boss=LDTerms::Boss;
    if(Content){for(const FLDDemoMonsterDefinition& Monster:Content->Monsters)
    {(Monster.Tier==ELDDemoCampTier::Low?Low:Monster.Tier==ELDDemoCampTier::High?High:Boss)=LDHUD::ContentName(Content,Monster.Id);}}
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
        DiagnosticsText=LDWidgets::Text(WidgetTree,TEXT(""),14,TEXT("NetworkDiagnosticsText"));
        DiagnosticsText->SetAutoWrapText(false);
        DiagnosticsText->SetJustification(ETextJustify::Right);
        DiagnosticsPanel=Panel(DiagnosticsText,false);
        Place(DiagnosticsPanel,FAnchors(1,0),FVector2D(1,0),FVector2D(-16,16));
        DiagnosticsPanel->SetVisibility(ESlateVisibility::Collapsed);
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
                TextSlot->SetHorizontalAlignment(HAlign_Fill);TextSlot->SetPadding(FMargin(80,0));
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
        {
            // Two Q/W tiles of a visible contender: the name on top, the rank (with the Lv3 mark) below; no cooldown is ever drawn.
            auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
            for(int32 Index=0;Index<2;++Index)
            {
                auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();
                UTextBlock* Name=OneLine(TEXT(""),14,0);HudFace(Name,true,1);Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Lines->AddChildToVerticalBox(Name);
                UTextBlock* Rank=OneLine(TEXT(""),13,0);HudFace(Rank,false,1);Rank->SetColorAndOpacity(LDHUDStyle::TextSecondary);Lines->AddChildToVerticalBox(Rank);
                auto* Tile=WidgetTree->ConstructWidget<UBorder>();Tile->SetBrush(HudRound(6.f,LDHUDStyle::Rim,1.f));Tile->SetBrushColor(FLinearColor(.02f,.03f,.05f,.9f));
                Tile->SetPadding(FMargin(8,3));Tile->SetContent(Lines);Tile->SetClipping(EWidgetClipping::ClipToBounds);
                auto* TileSlot=Row->AddChildToHorizontalBox(Tile);TileSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TileSlot->SetPadding(FMargin(Index?3.f:0.f,0,Index?0.f:3.f,0));
                TargetActiveTiles.Add(Tile);TargetActiveNames.Add(Name);TargetActiveRanks.Add(Rank);
            }
            TargetActivesRow=Row;TargetBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,4,0,0));
        }
        auto* TargetWidth=WidgetTree->ConstructWidget<USizeBox>();TargetWidth->SetWidthOverride(330);TargetWidth->SetContent(TargetBox);
        TargetCard=Panel(TargetWidth,true);TargetCard->SetVisibility(ESlateVisibility::Collapsed);

        // Top-left: the target card alone; the event feed sits above the minimap.
        Place(TargetCard,FAnchors(0,0),FVector2D(0,0),FVector2D(16,16));

        // Top-centre: the public capture state, then the own death banner. None takes pointer input.
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
        // D26: the public airdrop line, in the airdrop marker's orange.
        AirdropText=OneLine(TEXT(""),15,520);AirdropText->SetJustification(ETextJustify::Center);
        AirdropText->SetColorAndOpacity(FSlateColor(ALDAirdropMarker::Color()));AirdropText->SetVisibility(ESlateVisibility::Collapsed);
        CircleBox->AddChildToVerticalBox(AirdropText)->SetHorizontalAlignment(HAlign_Center);
        // D27: the public 上热搜 line, in the minimap circle's red.
        LeaderText=OneLine(TEXT(""),15,520);LeaderText->SetJustification(ETextJustify::Center);
        LeaderText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f,.32f,.28f)));LeaderText->SetVisibility(ESlateVisibility::Collapsed);
        CircleBox->AddChildToVerticalBox(LeaderText)->SetHorizontalAlignment(HAlign_Center);
        // D26: the public Boss line, in the minimap waypoints' gold.
        BossText=OneLine(TEXT(""),15,520);BossText->SetJustification(ETextJustify::Center);
        BossText->SetColorAndOpacity(FSlateColor(FLinearColor(.98f,.76f,.2f)));BossText->SetVisibility(ESlateVisibility::Collapsed);
        CircleBox->AddChildToVerticalBox(BossText)->SetHorizontalAlignment(HAlign_Center);
        // The own next step under the public line, in the accent colour so it reads as advice rather than match state.
        NextStepText=OneLine(TEXT(""),15,520);NextStepText->SetJustification(ETextJustify::Center);NextStepText->SetColorAndOpacity(LDHUDStyle::TextAccent);
        CircleBox->AddChildToVerticalBox(NextStepText)->SetHorizontalAlignment(HAlign_Center);
        PhaseBanner=Panel(CircleBox,false);Place(PhaseBanner,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,16));
        auto* DeathBox=WidgetTree->ConstructWidget<UVerticalBox>();
        DeathTitle=OneLine(TEXT(""),34,0);HudFace(DeathTitle,true,0);DeathTitle->SetColorAndOpacity(LDHUDStyle::TextDanger);
        DeathCauseText=OneLine(TEXT(""),18,520);DeathCountdown=OneLine(TEXT(""),22,0);
        DeathNote=OneLine(TEXT(""),14,520);DeathNote->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        for(UTextBlock* DeathLine:{DeathTitle.Get(),DeathCauseText.Get(),DeathCountdown.Get(),DeathNote.Get()})
        {DeathLine->SetJustification(ETextJustify::Center);DeathBox->AddChildToVerticalBox(DeathLine)->SetHorizontalAlignment(HAlign_Center);}
        DeathBanner=Panel(DeathBox,false);Place(DeathBanner,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,200));DeathBanner->SetVisibility(ESlateVisibility::Collapsed);
        // #153: the opening countdown, large in the screen centre; it never takes pointer input.
        OpeningCountdownText=LDWidgets::Text(WidgetTree,TEXT(""),96,TEXT("OpeningCountdownText"));OpeningCountdownText->SetAutoWrapText(false);
        OpeningCountdownText->SetJustification(ETextJustify::Center);HudFace(OpeningCountdownText,true,4);
        OpeningCountdownText->SetColorAndOpacity(LDHUDStyle::TextWarning);
        Place(OpeningCountdownText,FAnchors(.5f,.5f),FVector2D(.5f,.5f),FVector2D(0,-120));OpeningCountdownText->SetVisibility(ESlateVisibility::Collapsed);

        // Centre: the one frozen result; it replaces the gameplay HUD instead of overlaying it.
        auto* ResultBox=WidgetTree->ConstructWidget<UVerticalBox>();
        ResultTitle=Text(TEXT(""),28);ResultTitle->SetJustification(ETextJustify::Center);ResultBox->AddChildToVerticalBox(ResultTitle)->SetHorizontalAlignment(HAlign_Center);
        ResultSubtitle=Text(TEXT(""),16);ResultSubtitle->SetJustification(ETextJustify::Center);ResultBox->AddChildToVerticalBox(ResultSubtitle)->SetPadding(FMargin(0,6,0,12));
        ResultRows=WidgetTree->ConstructWidget<UVerticalBox>();ResultBox->AddChildToVerticalBox(ResultRows);
        auto* ResultActions=WidgetTree->ConstructWidget<UHorizontalBox>();ResultBox->AddChildToVerticalBox(ResultActions)->SetPadding(FMargin(0,16,0,0));
        ReturnButton=LDWidgets::Button(WidgetTree,TEXT("带房间返回 Lobby"),[PC](){PC->ReturnDemoToLobby();},false,TEXT("ResultReturnToLobby"));
        ResultActions->AddChildToHorizontalBox(ReturnButton)->SetPadding(FMargin(0,0,12,0));
        ResultActions->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("离开房间"),[PC](){if(PC->RootUI){PC->RootUI->LeaveRoom();}},false,TEXT("ResultLeaveRoom")));
        auto* ResultWidth=WidgetTree->ConstructWidget<USizeBox>();ResultWidth->SetWidthOverride(720);ResultWidth->SetContent(ResultBox);
        ResultPanel=Panel(ResultWidth,true);Place(ResultPanel,FAnchors(.5f,.5f),FVector2D(.5f,.5f),FVector2D(0,0));ResultPanel->SetVisibility(ESlateVisibility::Collapsed);

        // Centre: the hold-Tab scoreboard, read-only and click-through, over the running match.
        {
            auto* ScoreBox=WidgetTree->ConstructWidget<UVerticalBox>();
            UTextBlock* ScoreTitle=OneLine(LDTerms::Scoreboard,20,0);HudFace(ScoreTitle,true,0);
            ScoreBox->AddChildToVerticalBox(ScoreTitle)->SetPadding(FMargin(0,0,0,8));
            const float Widths[4]={240,96,96,72};
            for(int32 RowIdx=0;RowIdx<=LDCapture::MaxScoreRows;++RowIdx)
            {
                auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
                for(int32 Col=0;Col<4;++Col)
                {
                    UTextBlock* Cell=OneLine(TEXT(""),16,0);
                    if(RowIdx==0){Cell->SetColorAndOpacity(LDHUDStyle::TextSecondary);}
                    auto* CellBox=WidgetTree->ConstructWidget<USizeBox>();CellBox->SetWidthOverride(Widths[Col]);CellBox->SetContent(Cell);
                    Line->AddChildToHorizontalBox(CellBox);
                    if(RowIdx>0){ScoreCells.Add(Cell);}
                    else{HudSetText(Cell,Col==0?FString(TEXT("玩家")):Col==1?FString(LDTerms::ResultKills):Col==2?FString(LDTerms::ResultDeaths):FString(LDTerms::ResultAssists));}
                }
                ScoreBox->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,3));
                if(RowIdx>0){ScoreRows.Add(Line);}
            }
            ScorePanel=Panel(ScoreBox,false);Place(ScorePanel,FAnchors(.5f,.5f),FVector2D(.5f,.5f),FVector2D(0,-60));ScorePanel->SetVisibility(ESlateVisibility::Collapsed);
        }

        // Bottom-left, left of the bottom band: the sight state (only when it is not the normal current view) over the
        // minimap, which sits in the corner. #97: no standing camera-mode line.
        auto* MapBox=WidgetTree->ConstructWidget<UVerticalBox>();
        SightText=Text(TEXT(""),14);SightText->SetColorAndOpacity(LDHUDStyle::TextWarning);MapBox->AddChildToVerticalBox(SightText)->SetPadding(FMargin(0,0,0,4));
        Minimap=CreateWidget<ULDMinimap>(PC);MapFrame=WidgetTree->ConstructWidget<USizeBox>();MapFrame->SetWidthOverride(240);MapFrame->SetHeightOverride(240);MapFrame->SetContent(Minimap);
        // The match timer sits centred over the map frame itself, so a wide sight line above never shifts it.
        auto* MapStack=WidgetTree->ConstructWidget<UVerticalBox>();
        MatchTimerText=OneLine(TEXT(""),16,0);HudFace(MatchTimerText,true,1);MatchTimerText->SetJustification(ETextJustify::Center);
        MatchTimerText->SetVisibility(ESlateVisibility::Collapsed);
        MapStack->AddChildToVerticalBox(MatchTimerText)->SetHorizontalAlignment(HAlign_Center);
        auto* FrameSlot=MapStack->AddChildToVerticalBox(MapFrame);FrameSlot->SetHorizontalAlignment(HAlign_Left);FrameSlot->SetPadding(FMargin(0,2,0,0));
        MapBox->AddChildToVerticalBox(MapStack)->SetHorizontalAlignment(HAlign_Left);
        MapColumn=Panel(MapBox,true);
        // Bottom-left, right above the minimap column: the room's event feed (LDHUD::FEventFeed). The newest line is at the
        // bottom, next to the map, and older lines are pushed up; the stack grows upward from the column, so the map never
        // moves. The feed takes no pointer input.
        auto* FeedBox=WidgetTree->ConstructWidget<UVerticalBox>();
        for(int32 Row=0;Row<LDHUD::FEventFeed::MaxLines;++Row)
        {
            UTextBlock* FeedLine=OneLine(TEXT(""),15,440);FeedLine->SetVisibility(ESlateVisibility::Collapsed);HudFace(FeedLine,false,1);
            EventFeedRows.Add(FeedLine);FeedBox->AddChildToVerticalBox(FeedLine)->SetPadding(FMargin(0,1));
        }
        EventFeedPanel=Panel(FeedBox,false);EventFeedPanel->SetBrushColor(FLinearColor(.02f,.03f,.05f,.55f));EventFeedPanel->SetVisibility(ESlateVisibility::Collapsed);
        auto* BottomLeft=WidgetTree->ConstructWidget<UVerticalBox>();BottomLeft->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        BottomLeft->AddChildToVerticalBox(EventFeedPanel)->SetHorizontalAlignment(HAlign_Left);
        // #97: the map column keeps its own width; a wide feed line above must not stretch the minimap frame.
        auto* MapSlot=BottomLeft->AddChildToVerticalBox(MapColumn);MapSlot->SetPadding(FMargin(0,8,0,0));MapSlot->SetHorizontalAlignment(HAlign_Left);
        Place(BottomLeft,FAnchors(0,1),FVector2D(0,1),FVector2D(16,-16));
        // Settings open from the Back menu (its 设置 entry); the HUD has no separate settings button.

        // Bottom band: own identity | resources over the 2+2+1 action entries. The band stack is shifted so the centre block
        // stays on the screen centre, directly under the character, where the eyes are during a fight.
        auto* Bottom=WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* SelfBox=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* SelfHead=WidgetTree->ConstructWidget<UHorizontalBox>();SelfBox->AddChildToVerticalBox(SelfHead);
        UBorder* SP=nullptr;UTextBlock* SG=nullptr;
        auto* PortraitStack=WidgetTree->ConstructWidget<UOverlay>();PortraitStack->AddChildToOverlay(Portraits(44,20,SP,SG));Portrait=SP;PortraitGlyph=SG;
        // The level rides on the portrait corner; the experience bar over the actions carries the numbers as its tooltip.
        LevelBadgeText=OneLine(TEXT(""),13,0);HudFace(LevelBadgeText,true,0);LevelBadgeText->SetColorAndOpacity(LDHUDStyle::TextAccent);
        auto* LevelBadge=WidgetTree->ConstructWidget<UBorder>();LevelBadge->SetBrush(HudRound(4.f,LDHUDStyle::TextAccent,1.f));LevelBadge->SetBrushColor(FLinearColor(.02f,.03f,.05f,.96f));
        LevelBadge->SetPadding(FMargin(5,0));LevelBadge->SetContent(LevelBadgeText);LevelBadge->SetVisibility(ESlateVisibility::HitTestInvisible);LevelBadge->SetRenderTranslation(FVector2D(6,6));
        auto* BadgeSlot=PortraitStack->AddChildToOverlay(LevelBadge);BadgeSlot->SetHorizontalAlignment(HAlign_Right);BadgeSlot->SetVerticalAlignment(VAlign_Bottom);
        auto* PortraitSlot=SelfHead->AddChildToHorizontalBox(PortraitStack);PortraitSlot->SetPadding(FMargin(0,0,14,0));PortraitSlot->SetVerticalAlignment(VAlign_Top);
        auto* NameBox=WidgetTree->ConstructWidget<UVerticalBox>();
        // A long roster name ends in an ellipsis inside the column; hovering it shows the whole name (UI 3.3).
        SelfName=OneLine(TEXT(""),17,0);HudFace(SelfName,true,0);SelfName->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);SelfName->SetVisibility(ESlateVisibility::Visible);NameBox->AddChildToVerticalBox(SelfName);
        auto* NameSlot=SelfHead->AddChildToHorizontalBox(NameBox);NameSlot->SetVerticalAlignment(VAlign_Center);NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        // The own attribute sheet opens from the identity block that shows the level; own panels open while dead as well
        // (UI 3.2) and the root keeps one main panel at a time. The DEV test entry shows only in a DEV test room (#78).
        auto* PanelColumn=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Sheet=LDWidgets::Button(WidgetTree,TEXT("属性"),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Attributes);}},true,TEXT("HUDAttributes"));
        auto* Dev=LDWidgets::Button(WidgetTree,TEXT("测试"),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::DevTest);}},true,TEXT("HUDDevTest"));
        Dev->SetVisibility(ESlateVisibility::Collapsed);
        Commands.Add(TEXT("Attributes"),Sheet);Commands.Add(TEXT("DevTest"),Dev);
        PanelColumn->AddChildToVerticalBox(Sheet)->SetHorizontalAlignment(HAlign_Right);
        PanelColumn->AddChildToVerticalBox(Dev)->SetPadding(FMargin(0,4,0,0));
        auto* ColumnSlot=SelfHead->AddChildToHorizontalBox(PanelColumn);ColumnSlot->SetVerticalAlignment(VAlign_Center);ColumnSlot->SetPadding(FMargin(8,0,0,0));
        // The life line only when it says more than "alive"; it wraps inside the fixed column.
        LifeText=Text(TEXT(""),14);SelfBox->AddChildToVerticalBox(LifeText)->SetPadding(FMargin(0,8,0,0));
        // The own status grid: temporary statuses first (control before the rest), then passives, one placeholder square
        // each (the name's first character; hover names it in full). A full grid ends in "…": the attribute panel lists all.
        {
            SelfIconSyncing=Text(TEXT("状态同步中"),14);SelfIconSyncing->SetColorAndOpacity(LDHUDStyle::TextSecondary);
            SelfIconSyncing->SetVisibility(ESlateVisibility::Collapsed);SelfBox->AddChildToVerticalBox(SelfIconSyncing)->SetPadding(FMargin(0,6,0,0));
            for(int32 Row=0;Row<LDHUD::SelfIconRows;++Row)
            {
                auto* RowBox=WidgetTree->ConstructWidget<UHorizontalBox>();RowBox->SetVisibility(ESlateVisibility::Collapsed);
                for(int32 Col=0;Col<LDHUD::SelfIconColumns;++Col)
                {
                    UTextBlock* Glyph=OneLine(TEXT(""),18,0);HudFace(Glyph,true,1);Glyph->SetJustification(ETextJustify::Center);
                    UTextBlock* Stacks=OneLine(TEXT(""),11,0);HudFace(Stacks,true,1);
                    auto* Face=WidgetTree->ConstructWidget<UOverlay>();
                    auto* GlyphSlot=Face->AddChildToOverlay(Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
                    auto* StackSlot=Face->AddChildToOverlay(Stacks);StackSlot->SetHorizontalAlignment(HAlign_Right);StackSlot->SetVerticalAlignment(VAlign_Bottom);
                    auto* Cell=WidgetTree->ConstructWidget<UBorder>();Cell->SetBrush(HudRound(5.f,LDHUDStyle::Rim,1.f));
                    Cell->SetBrushColor(FLinearColor(.02f,.03f,.05f,.9f));Cell->SetPadding(FMargin(2));Cell->SetContent(Face);
                    Cell->SetVisibility(ESlateVisibility::Collapsed);
                    auto* CellBox=WidgetTree->ConstructWidget<USizeBox>();CellBox->SetWidthOverride(HudSelfIconSize);CellBox->SetHeightOverride(HudSelfIconSize);CellBox->SetContent(Cell);
                    RowBox->AddChildToHorizontalBox(CellBox)->SetPadding(FMargin(Col?HudSelfIconGap:0.f,0,0,0));
                    SelfIconCells.Add(Cell);SelfIconGlyphs.Add(Glyph);SelfIconStacks.Add(Stacks);SelfIconKinds.Add(-1);SelfIconRing.Add(-1.f);
                }
                SelfIconRowBoxes.Add(RowBox);SelfBox->AddChildToVerticalBox(RowBox)->SetPadding(FMargin(0,Row?HudSelfIconGap:6.f,0,0));
            }
            SelfIconMore=OneLine(TEXT("…"),16,0);HudFace(SelfIconMore,true,0);SelfIconMore->SetColorAndOpacity(LDHUDStyle::TextSecondary);
            SelfIconMore->SetToolTipText(FText::FromString(TEXT("更多状态与被动见属性面板")));SelfIconMore->SetVisibility(ESlateVisibility::Collapsed);
            SelfBox->AddChildToVerticalBox(SelfIconMore)->SetPadding(FMargin(0,2,0,0));
        }
        auto* SelfWidth=WidgetTree->ConstructWidget<USizeBox>();SelfWidth->SetWidthOverride(HudSideWidth);SelfWidth->SetClipping(EWidgetClipping::ClipToBounds);SelfWidth->SetContent(SelfBox);
        Bottom->AddChildToHorizontalBox(SelfWidth)->SetVerticalAlignment(VAlign_Fill);
        {
            auto* Rule=WidgetTree->ConstructWidget<UBorder>();Rule->SetBrushColor(LDHUDStyle::PanelRim);Rule->SetVisibility(ESlateVisibility::HitTestInvisible);
            auto* RuleBox=WidgetTree->ConstructWidget<USizeBox>();RuleBox->SetWidthOverride(1);RuleBox->SetContent(Rule);
            auto* RuleSlot=Bottom->AddChildToHorizontalBox(RuleBox);RuleSlot->SetPadding(FMargin(HudBlockGap*.5f,4));RuleSlot->SetVerticalAlignment(VAlign_Fill);
        }
        auto* Centre=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();
        for(int32 Index=0;Index<HudActionCount;++Index)
        {
            FLDHUDActionSlot& Entry=ActionSlots.AddDefaulted_GetRef();
            // The face: the icon filling the state ring, the cooldown mask and its remaining time over it, and the key
            // (top-left), the rank (bottom-left) and the mana cost (bottom-right) on its corners. Corner text carries a 2u
            // outline because it sits on the icon colours.
            auto* Face=WidgetTree->ConstructWidget<UOverlay>();Face->SetClipping(EWidgetClipping::ClipToBounds);
            auto FillLayer=[](UOverlaySlot* Layer){Layer->SetHorizontalAlignment(HAlign_Fill);Layer->SetVerticalAlignment(VAlign_Fill);};
            Entry.Glyph=OneLine(TEXT(""),30,0);HudFace(Entry.Glyph,true,2);Entry.Glyph->SetJustification(ETextJustify::Center);
            Entry.Icon=WidgetTree->ConstructWidget<UBorder>();Entry.Icon->SetBrush(HudRound(6.f,FLinearColor(1,1,1,.18f),1.f));
            Entry.Icon->SetHorizontalAlignment(HAlign_Center);Entry.Icon->SetVerticalAlignment(VAlign_Center);Entry.Icon->SetContent(Entry.Glyph);
            FillLayer(Face->AddChildToOverlay(Entry.Icon));
            Entry.Cooldown=WidgetTree->ConstructWidget<UProgressBar>();
            FProgressBarStyle MaskStyle;MaskStyle.SetBackgroundImage(FSlateNoResource()).SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,6.f)).SetMarqueeImage(FSlateNoResource());
            Entry.Cooldown->SetWidgetStyle(MaskStyle);Entry.Cooldown->SetBarFillType(EProgressBarFillType::TopToBottom);Entry.Cooldown->SetFillColorAndOpacity(LDHUDStyle::SlotCooldownMask);
            Entry.Cooldown->SetPercent(0);Entry.Cooldown->SetVisibility(ESlateVisibility::Collapsed);FillLayer(Face->AddChildToOverlay(Entry.Cooldown));
            Entry.Countdown=OneLine(TEXT(""),26,0);HudFace(Entry.Countdown,true,2);Entry.Countdown->SetJustification(ETextJustify::Center);Entry.Countdown->SetColorAndOpacity(FLinearColor::White);
            Entry.Countdown->SetVisibility(ESlateVisibility::Collapsed);
            auto* CountdownLayer=Face->AddChildToOverlay(Entry.Countdown);CountdownLayer->SetHorizontalAlignment(HAlign_Center);CountdownLayer->SetVerticalAlignment(VAlign_Center);
            Entry.Key=OneLine(TEXT(""),16,0);HudFace(Entry.Key,true,2);Entry.Key->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Entry.Key->SetColorAndOpacity(LDHUDStyle::TextAccent);
            auto* KeyLayer=Face->AddChildToOverlay(Entry.Key);KeyLayer->SetHorizontalAlignment(HAlign_Left);KeyLayer->SetVerticalAlignment(VAlign_Top);KeyLayer->SetPadding(FMargin(5,2,5,0));
            auto Chip=[&](UTextBlock*& OutText,const FLinearColor& Ink,const FLinearColor& Edge,const FLinearColor& Fill)
            {
                OutText=OneLine(TEXT(""),13,0);HudFace(OutText,true,0);OutText->SetColorAndOpacity(Ink);
                auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrush(HudRound(4.f,Edge,1.f));Border->SetBrushColor(Fill);
                Border->SetPadding(FMargin(5,0));Border->SetContent(OutText);Border->SetVisibility(ESlateVisibility::Collapsed);
                return Border;
            };
            UTextBlock* ChipText=nullptr;
            Entry.RankChip=Chip(ChipText,LDHUDStyle::TextAccent,LDHUDStyle::TextAccent,FLinearColor(.02f,.03f,.05f,.92f));Entry.Rank=ChipText;
            auto* RankLayer=Face->AddChildToOverlay(Entry.RankChip);RankLayer->SetHorizontalAlignment(HAlign_Left);RankLayer->SetVerticalAlignment(VAlign_Bottom);RankLayer->SetPadding(FMargin(3,0,0,3));
            Entry.CostChip=Chip(ChipText,HudManaText,FLinearColor(.18f,.28f,.62f),FLinearColor(.015f,.03f,.09f,.92f));Entry.Cost=ChipText;
            auto* CostLayer=Face->AddChildToOverlay(Entry.CostChip);CostLayer->SetHorizontalAlignment(HAlign_Right);CostLayer->SetVerticalAlignment(VAlign_Bottom);CostLayer->SetPadding(FMargin(0,0,3,3));
            // A pre-sold item (marked from the item menu): a yellow disc with an "S" in the top-right corner.
            UTextBlock* MarkText=OneLine(TEXT("S"),13,0);HudFace(MarkText,true,0);MarkText->SetJustification(ETextJustify::Center);MarkText->SetColorAndOpacity(FLinearColor(.1f,.07f,0.f));
            auto* MarkDisc=WidgetTree->ConstructWidget<UBorder>();MarkDisc->SetBrush(HudRound(11.f,FLinearColor(.35f,.24f,0.f),1.f));MarkDisc->SetBrushColor(FLinearColor(1.f,.8f,.1f));
            MarkDisc->SetPadding(FMargin(0));MarkDisc->SetHorizontalAlignment(HAlign_Center);MarkDisc->SetVerticalAlignment(VAlign_Center);MarkDisc->SetContent(MarkText);
            Entry.PresellMark=WidgetTree->ConstructWidget<USizeBox>();Entry.PresellMark->SetWidthOverride(22);Entry.PresellMark->SetHeightOverride(22);Entry.PresellMark->SetContent(MarkDisc);
            Entry.PresellMark->SetVisibility(ESlateVisibility::Collapsed);
            auto* MarkLayer=Face->AddChildToOverlay(Entry.PresellMark);MarkLayer->SetHorizontalAlignment(HAlign_Right);MarkLayer->SetVerticalAlignment(VAlign_Top);MarkLayer->SetPadding(FMargin(0,3,3,0));
            Entry.Frame=WidgetTree->ConstructWidget<UBorder>();Entry.Frame->SetPadding(FMargin(3));
            Entry.Frame->SetHorizontalAlignment(HAlign_Fill);Entry.Frame->SetVerticalAlignment(VAlign_Fill);Entry.Frame->SetContent(Face);
            // The ready flash covers the whole slot; neither layer takes input, the button under them does.
            auto* Stack=WidgetTree->ConstructWidget<UOverlay>();
            FillLayer(Stack->AddChildToOverlay(Entry.Frame));
            Entry.Flash=WidgetTree->ConstructWidget<UBorder>();Entry.Flash->SetVisibility(ESlateVisibility::Collapsed);FillLayer(Stack->AddChildToOverlay(Entry.Flash));
            // The icon sends exactly what its hotkey sends; Combat decides Self casts and the armed target forms.
            const ELDCombatEntry Activated=HudActionEntries[Index];
            // A Q/W slot fires the entry the local slot order puts there; during the replacement pick it is the choice of
            // the skill to forget instead (#97); a press that became a drag fires nothing.
            Entry.Button=LDWidgets::Button(WidgetTree,TEXT(""),[this,PC,Activated,Index]()
            {
                if(bDragging&&DragFrom==Index){return;}
                const ELDCombatEntry Fired=Index<2?PC->SkillAtKey[Index]:Activated;
                if(Index<2&&ChoicesPanel&&ChoicesPanel->HasChildConfirmation()){ChoicesPanel->ChooseReplacement(Fired==ELDCombatEntry::W?1:0);return;}
                if(Fired!=ELDCombatEntry::None){PC->ActivateCombatEntry(Fired);}
            },true,HudActionNames[Index]);
            // The face draws every state itself, so the button keeps no chrome of its own.
            auto ButtonStyle=Entry.Button->GetStyle();ButtonStyle.Normal=FSlateNoResource();ButtonStyle.Hovered=FSlateNoResource();ButtonStyle.Pressed=FSlateNoResource();ButtonStyle.Disabled=FSlateNoResource();
            ButtonStyle.NormalPadding=FMargin(0);ButtonStyle.PressedPadding=FMargin(0);Entry.Button->SetStyle(ButtonStyle);
            Entry.Button->SetContent(Stack);
            if(UButtonSlot* StackSlot=Cast<UButtonSlot>(Stack->Slot)){StackSlot->SetPadding(FMargin(0));StackSlot->SetHorizontalAlignment(HAlign_Fill);StackSlot->SetVerticalAlignment(VAlign_Fill);}
            Entry.Box=WidgetTree->ConstructWidget<USizeBox>();Entry.Box->SetWidthOverride(HudSlotWidth);Entry.Box->SetHeightOverride(HudSlotHeight);Entry.Box->SetContent(Entry.Button);
            // Character actives (Q/W), the two equipment slots and the innate dash (D) are separate groups, never one merged bar.
            Actions->AddChildToHorizontalBox(Entry.Box)->SetPadding(FMargin(Index==0?0.f:Index==2||Index==4?HudGroupGap:HudSlotGap,0,0,0));
        }
        // #97: the action entries on top, right under the blind-box button and the three-choice panel; health, mana and the
        // experience strip below them.
        Centre->AddChildToVerticalBox(Actions);
        Centre->AddChildToVerticalBox(Bar(LDHUDStyle::OwnHealth,B,T,HudCentreWidth,24,&HealthRegenText))->SetPadding(FMargin(0,8,0,0));HealthBar=B;HealthText=T;
        Centre->AddChildToVerticalBox(Bar(LDHUDStyle::Mana,B,T,HudCentreWidth,22,&ManaRegenText))->SetPadding(FMargin(0,3,0,0));ManaBar=B;ManaText=T;
        LevelBar=WidgetTree->ConstructWidget<UProgressBar>();HudBarStyle(LevelBar,LDHUDStyle::Experience);LevelBar->SetPercent(0);
        auto* LevelStrip=WidgetTree->ConstructWidget<USizeBox>();LevelStrip->SetHeightOverride(8);LevelStrip->SetContent(LevelBar);
        Centre->AddChildToVerticalBox(LevelStrip)->SetPadding(FMargin(0,4,0,0));
        auto* CentreWidth=WidgetTree->ConstructWidget<USizeBox>();CentreWidth->SetWidthOverride(HudCentreWidth);CentreWidth->SetContent(Centre);
        Bottom->AddChildToHorizontalBox(CentreWidth)->SetVerticalAlignment(VAlign_Bottom);
        Band=LDWidgets::Panel(WidgetTree,Bottom,FMargin(HudBandPadX,10));HitPanels.Add(Band);

        // Above the band: the toast, the only line that says what went wrong (a refused request such as 目标不可用); an armed
        // entry or attack move is shown by its slot ring and the world preview alone. The stack grows upward from the band,
        // so a taller band (text scale) never overlaps it. The band has no right block, so the stack is shifted left by half
        // the identity block: the centre block stays on the screen centre, under the character, and the toast is padded
        // back over it.
        const float HudLeftReach=HudSideWidth+HudBlockGap+1;
        auto* BandStack=WidgetTree->ConstructWidget<UVerticalBox>();BandStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        BandStack->AddChildToVerticalBox(Band)->SetHorizontalAlignment(HAlign_Center);
        // The scale box lays the band out at HudBandScale, so its hit area, popups and the stack above it follow the drawn size.
        auto* BandScale=WidgetTree->ConstructWidget<UScaleBox>();BandScale->SetStretch(EStretch::UserSpecified);BandScale->SetUserSpecifiedScale(HudBandScale);
        BandScale->SetVisibility(ESlateVisibility::SelfHitTestInvisible);BandScale->SetContent(BandStack);
        Place(BandScale,FAnchors(.5f,1),FVector2D(.5f,1),FVector2D(-HudLeftReach*.5f*HudBandScale,-12));
        // The toast is centred on the screen centre by its own canvas slot, 8 units above the band (RefreshReadModel follows
        // the band's height), so its width never shifts it off centre; a translucent face keeps the world visible behind it.
        Toast=OneLine(TEXT(""),16,640);Toast->SetJustification(ETextJustify::Center);
        ToastPanel=Panel(Toast,false);ToastPanel->SetBrushColor(LDHUDStyle::PanelFill.CopyWithNewOpacity(.72f));ToastPanel->SetVisibility(ESlateVisibility::Collapsed);
        ToastSlot=Canvas->AddChildToCanvas(ToastPanel);ToastSlot->SetAnchors(FAnchors(.5f,1));ToastSlot->SetAlignment(FVector2D(.5f,1));ToastSlot->SetAutoSize(true);
        ToastSlot->SetPosition(FVector2D(0,-12-8));
        {
            // The hovered slot's description: the shop and blind-box popup's look, wrapped at a fixed width, centred above
            // the slot (RefreshReadModel places it each frame) and taking no pointer input.
            auto* DetailBox=WidgetTree->ConstructWidget<UVerticalBox>();
            auto FixedWrap=[&](int32 Size){UTextBlock* Line=Text(TEXT(""),Size);Line->SetAutoWrapText(false);Line->SetWrapTextAt(HudDetailWidth-28.f);return Line;};
            ActionDetailName=FixedWrap(17);DetailBox->AddChildToVerticalBox(ActionDetailName);
            ActionDetailSummary=FixedWrap(13);ActionDetailSummary->SetColorAndOpacity(LDHUDStyle::TextAccent);DetailBox->AddChildToVerticalBox(ActionDetailSummary)->SetPadding(FMargin(0,2,0,0));
            ActionDetailBody=FixedWrap(14);DetailBox->AddChildToVerticalBox(ActionDetailBody)->SetPadding(FMargin(0,8,0,0));
            auto* DetailPanel=LDWidgets::Panel(WidgetTree,DetailBox,FMargin(14,12));DetailPanel->SetBrushColor(FLinearColor(.02f,.035f,.055f,.94f));
            auto* DetailWidth=WidgetTree->ConstructWidget<USizeBox>();DetailWidth->SetWidthOverride(HudDetailWidth);DetailWidth->SetContent(DetailPanel);
            DetailWidth->SetVisibility(ESlateVisibility::Collapsed);
            ActionDetailSlot=Canvas->AddChildToCanvas(DetailWidth);ActionDetailSlot->SetAutoSize(true);ActionDetailSlot->SetAlignment(FVector2D(.5f,1));ActionDetailSlot->SetZOrder(20);
        }
        {
            // A right click on an own equipment slot opens this menu above it; its one command marks the item for selling
            // (sold at once at the shop, else on reaching it) or takes the mark back. Any press elsewhere closes it.
            ItemMenuButton=LDWidgets::Button(WidgetTree,TEXT("卖出"),[this](){ChooseItemMenu();},true,TEXT("HUDItemMenuSell"));
            ItemMenu=LDWidgets::Panel(WidgetTree,ItemMenuButton,FMargin(6));ItemMenu->SetBrushColor(FLinearColor(.02f,.035f,.055f,.96f));
            ItemMenu->SetVisibility(ESlateVisibility::Collapsed);HitPanels.Add(ItemMenu);
            ItemMenuSlot=Canvas->AddChildToCanvas(ItemMenu);ItemMenuSlot->SetAutoSize(true);ItemMenuSlot->SetAlignment(FVector2D(.5f,1));ItemMenuSlot->SetZOrder(21);
        }

        // The blind-box button sits right above the band's left block (#97) and the shop entry in the bottom-right corner.
        // Each toggles its own panel, which docks right above it; neither has a hotkey. The blind-box button is the Choices
        // panel's only entry besides the opening's automatic open: greyed out and disabled while no choice is pending, and it
        // pulses when the count rises. The shop entry shows the own gold and pre-order count; buying and upgrading still
        // need the shop itself, and pre-orders and pre-sells (marked from the equipment slots' right-click menu) trade
        // themselves there (ULDShopPanel).
        auto* Prompt=LDWidgets::Button(WidgetTree,TEXT(""),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Choices);}},true,TEXT("HUDChoicesPrompt"));
        PromptButton=Prompt;
        PromptText=CastChecked<UTextBlock>(Prompt->GetContent());HudFace(PromptText,true,0);PromptText->SetColorAndOpacity(HudPromptInk);
        // The blind-box button is always the lit corner style; disabled draws it greyed out.
        Prompt->SetStyle(HudCornerStyle(Prompt->GetStyle(),true));
        PendingPrompt=WidgetTree->ConstructWidget<UBorder>();PendingPrompt->SetBrushColor(FLinearColor::Transparent);PendingPrompt->SetPadding(FMargin(0));PendingPrompt->SetContent(Prompt);
        PendingPrompt->SetRenderTransformPivot(FVector2D(.5f,.5f));
        ShopButton=LDWidgets::Button(WidgetTree,TEXT(""),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Shop);}},true,TEXT("HUDShop"));
        ShopButtonText=CastChecked<UTextBlock>(ShopButton->GetContent());HudFace(ShopButtonText,true,0);ShopButtonText->SetColorAndOpacity(LDHUDStyle::GoldGain);
        ShopSharedStyle=ShopButton->GetStyle();ShopButton->SetStyle(HudCornerStyle(ShopSharedStyle,false));
        for(UTextBlock* Caption:{PromptText.Get(),ShopButtonText.Get()})
        {
            Caption->SetJustification(ETextJustify::Center);
            if(UButtonSlot* CaptionSlot=Cast<UButtonSlot>(Caption->Slot)){CaptionSlot->SetPadding(FMargin(0));CaptionSlot->SetHorizontalAlignment(HAlign_Center);CaptionSlot->SetVerticalAlignment(VAlign_Center);}
        }
        auto* ShopFrame=Panel(ShopButton,true);ShopFrame->SetPadding(FMargin(4));
        Place(ShopFrame,FAnchors(1,1),FVector2D(1,1),FVector2D(-16,-16));
        ShopEntry=ShopFrame;
        // RefreshReadModel keeps the blind-box button at the left edge of the band, right above its left block.
        PromptFrame=Panel(PendingPrompt,true);PromptFrame->SetPadding(FMargin(4));
        PromptSlot=Canvas->AddChildToCanvas(PromptFrame);PromptSlot->SetAnchors(FAnchors(.5f,1));PromptSlot->SetAlignment(FVector2D(0,1));PromptSlot->SetAutoSize(true);
        {
            // The dragged slot's ghost follows the pointer; it takes no input.
            DragGhostGlyph=OneLine(TEXT(""),26,0);HudFace(DragGhostGlyph,true,2);DragGhostGlyph->SetJustification(ETextJustify::Center);
            DragGhost=WidgetTree->ConstructWidget<UBorder>();DragGhost->SetBrush(HudRound(6.f,LDHUDStyle::TextAccent,2.f));
            DragGhost->SetHorizontalAlignment(HAlign_Center);DragGhost->SetVerticalAlignment(VAlign_Center);DragGhost->SetContent(DragGhostGlyph);
            auto* GhostBox=WidgetTree->ConstructWidget<USizeBox>();GhostBox->SetWidthOverride(HudSlotWidth*HudBandScale);GhostBox->SetHeightOverride(HudSlotHeight*HudBandScale);
            GhostBox->SetContent(DragGhost);GhostBox->SetRenderOpacity(.8f);GhostBox->SetVisibility(ESlateVisibility::Collapsed);
            DragGhostSlot=Canvas->AddChildToCanvas(GhostBox);DragGhostSlot->SetAutoSize(true);DragGhostSlot->SetAlignment(FVector2D(.5f,.5f));DragGhostSlot->SetZOrder(30);
        }

        // Normal main panels fill the canvas with a hit-transparent frame, so the world beside them keeps its input.
        AttributesPanel=CreateWidget<ULDAttributesPanel>(PC);ChoicesPanel=CreateWidget<ULDChoicesPanel>(PC);ShopPanel=CreateWidget<ULDShopPanel>(PC);
        DevTestPanel=CreateWidget<ULDDevTestPanel>(PC);
        const TPair<ELDUIMainPanel,ULDPanel*> Docked[]={{ELDUIMainPanel::Attributes,AttributesPanel.Get()},{ELDUIMainPanel::Choices,ChoicesPanel.Get()},
            {ELDUIMainPanel::Shop,ShopPanel.Get()},{ELDUIMainPanel::DevTest,DevTestPanel.Get()}};
        for(const TPair<ELDUIMainPanel,ULDPanel*>& Entry:Docked)
        {
            if(!Entry.Value){UE_LOG(LogLDUI,Error,TEXT("LD_HUD_PANEL_CREATION_FAILED %d"),int32(Entry.Key));continue;}
            auto* PanelSlot=Canvas->AddChildToCanvas(Entry.Value);PanelSlot->SetAnchors(FAnchors(0,0,1,1));PanelSlot->SetOffsets(FMargin(0));PanelSlot->SetZOrder(10);
            if(PC->RootUI){PC->RootUI->RegisterMainPanel(Entry.Key,Entry.Value);}
        }
        if(ShopPanel)
        {
            ShopPanel->Notify=[this](const FString& Notice){ShowToast(Notice);};
            ShopPanel->PlayCue=[this](ELDAudioCue Cue){PlayAudioCues(TArray<ELDAudioCue>{Cue});};
        }
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
FReply ULDHUD::NativeOnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    const FVector2D At=E.GetScreenSpacePosition();
    // Any press outside the open item menu closes it and still goes where it was aimed.
    if(ItemMenuIndex!=INDEX_NONE&&!(ItemMenu&&ItemMenu->GetCachedGeometry().IsUnderLocation(At))){CloseItemMenu();}
    // A left press on a filled Q/W or equipment slot may become a drag (UpdateDrag); the press still reaches the button.
    // The replacement pick takes Q/W clicks as its choice, so nothing drags meanwhile.
    if(E.GetEffectingButton()==EKeys::LeftMouseButton&&Band&&Band->IsVisible()&&!(ChoicesPanel&&ChoicesPanel->HasChildConfirmation()))
    {
        DragFrom=INDEX_NONE;bDragging=false;
        for(int32 Index=0;Index<FMath::Min(4,ActionSlots.Num());++Index)
        {
            const FLDHUDActionSlot& Entry=ActionSlots[Index];
            if(Entry.bHasSource&&Entry.Box&&Entry.Box->GetCachedGeometry().IsUnderLocation(At)){DragFrom=Index;DragStart=At;break;}
        }
    }
    // A right click on an own equipment slot opens its menu; seen before the slot button (which answers only the left
    // button), so the world never gets this right click.
    if(E.GetEffectingButton()==EKeys::RightMouseButton&&Band&&Band->IsVisible())
    {
        for(int32 Index=0;Index<ActionSlots.Num();++Index)
        {
            const FLDHUDActionSlot& Entry=ActionSlots[Index];
            if(Entry.ItemInstance.IsValid()&&Entry.Box&&Entry.Box->GetCachedGeometry().IsUnderLocation(At)){OpenItemMenu(Index);return FReply::Handled();}
        }
    }
    return Super::NativeOnPreviewMouseButtonDown(G,E);
}
void ULDHUD::UpdateDrag()
{
    if(DragFrom==INDEX_NONE||!FSlateApplication::IsInitialized()){return;}
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    UWidget* Ghost=DragGhostSlot?DragGhostSlot->Content.Get():nullptr;
    const FVector2D At=FSlateApplication::Get().GetCursorPos();
    const bool bHeld=FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
    if(!PC||!ActionSlots.IsValidIndex(DragFrom)||!Band||!Band->IsVisible())
    {
        DragFrom=INDEX_NONE;bDragging=false;if(Ghost){Ghost->SetVisibility(ESlateVisibility::Collapsed);}return;
    }
    if(!bDragging&&bHeld&&FVector2D::Distance(At,DragStart)>=HudDragStartPx)
    {
        bDragging=true;CloseItemMenu();
        const FLDHUDActionSlot& From=ActionSlots[DragFrom];
        if(DragGhost){DragGhost->SetBrushColor(From.Icon->GetBrushColor());}
        HudSetText(DragGhostGlyph,From.Glyph->GetText().ToString());
        if(Ghost){Ghost->SetVisibility(ESlateVisibility::HitTestInvisible);}
    }
    if(bDragging&&Ghost)
    {
        const FVector2D Place=GetCachedGeometry().AbsoluteToLocal(At);
        if(!DragGhostSlot->GetPosition().Equals(Place,.5f)){DragGhostSlot->SetPosition(Place);}
    }
    if(bHeld){return;}
    if(bDragging)
    {
        // Dropped on the other slot of the same group (skills with skills, equipment with equipment): the two trade places.
        for(int32 To=0;To<FMath::Min(4,ActionSlots.Num());++To)
        {
            if(To==DragFrom||(To<2)!=(DragFrom<2)||!ActionSlots[To].Box->GetCachedGeometry().IsUnderLocation(At)){continue;}
            if(To<2){Swap(PC->SkillAtKey[0],PC->SkillAtKey[1]);}else{Swap(PC->EquipmentAtPosition[0],PC->EquipmentAtPosition[1]);}
            for(FLDHUDActionSlot& Changed:ActionSlots){Changed.AppliedLook=-1;Changed.bCooling=false;}
            break;
        }
    }
    DragFrom=INDEX_NONE;bDragging=false;
    if(Ghost){Ghost->SetVisibility(ESlateVisibility::Collapsed);}
}
void ULDHUD::OpenItemMenu(int32 SlotIndex)
{
    if(!ItemMenu||!ActionSlots.IsValidIndex(SlotIndex)||!ActionSlots[SlotIndex].ItemInstance.IsValid()){return;}
    ItemMenuIndex=SlotIndex;ItemMenuInstance=ActionSlots[SlotIndex].ItemInstance;
    ItemMenu->SetVisibility(ESlateVisibility::Visible);PlaceItemMenu();
}
void ULDHUD::PlaceItemMenu()
{
    if(ItemMenuIndex==INDEX_NONE||!ItemMenuSlot||!ItemMenuButton){return;}
    HudSetText(CastChecked<UTextBlock>(ItemMenuButton->GetContent()),ShopPanel&&ShopPanel->IsPresell(ItemMenuInstance)?TEXT("取消预售"):TEXT("卖出"));
    // Centred 8 units above the slot, read from last frame's layout, pushed sideways only to stay on screen.
    const FGeometry& Area=GetCachedGeometry();const FGeometry& Over=ActionSlots[ItemMenuIndex].Box->GetCachedGeometry();
    const FVector2D Top=Area.AbsoluteToLocal(Over.GetAbsolutePosition());
    const FVector2D Size=Area.AbsoluteToLocal(Over.GetAbsolutePosition()+Over.GetAbsoluteSize())-Top;
    const float Half=float(ItemMenu->GetDesiredSize().X)*.5f+8.f;const float Wide=float(Area.GetLocalSize().X);
    const float X=Wide>2*Half?FMath::Clamp(float(Top.X+Size.X*.5),Half,Wide-Half):float(Top.X+Size.X*.5);
    const FVector2D Place(FMath::RoundToFloat(X),FMath::RoundToFloat(float(Top.Y)-8.f));
    if(!ItemMenuSlot->GetPosition().Equals(Place,.5f)){ItemMenuSlot->SetPosition(Place);}
}
void ULDHUD::CloseItemMenu()
{
    ItemMenuIndex=INDEX_NONE;ItemMenuInstance.Invalidate();
    if(ItemMenu){ItemMenu->SetVisibility(ESlateVisibility::Collapsed);}
}
void ULDHUD::ChooseItemMenu()
{
    const FGuid Chosen=ItemMenuInstance;CloseItemMenu();
    if(!ShopPanel){UE_LOG(LogLDUI,Error,TEXT("LD_HUD_ITEM_MENU_NO_SHOP The item menu has no shop panel to mark the sale with."));return;}
    if(Chosen.IsValid()){ShopPanel->TogglePresell(Chosen);}
}
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
        if(!LDDemoEntry::LoadRuntimeContent(DisplayContent,Error)||!DisplayContent){DisplayContent.Reset();UE_LOG(LogLDUI,Error,TEXT("LD_HUD_CONTENT_UNAVAILABLE %s"),*Error);}
    }
    return DisplayContent?&DisplayContent->Get():nullptr;
}
void ULDHUD::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    UpdateDrag();
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if (!PC || !DiagnosticsPanel) { return; }
    const bool Network=PC->Controls && PC->Controls->bShowNetworkDiagnostics;
    const bool FPS=PC->Controls && PC->Controls->bShowFPS;
    HudSetShown(DiagnosticsPanel,Network||FPS,ESlateVisibility::HitTestInvisible);
    if (!Network && !FPS)
    {
        DiagnosticsTickAt=-1; FrameDiagnostics={}; DiagnosticsSampleAt=0;
        DiagnosticsBody.Reset(); DiagnosticsCorrections=0;
        return;
    }
    const double Now=FPlatformTime::Seconds();
    const double Previous=DiagnosticsTickAt;
    DiagnosticsTickAt=Now;
    if (Previous<0 || !FrameDiagnostics.AddFrame(Now-Previous)) { return; }
    FString Text;
    if (FPS)
    {
        Text=FString::Printf(TEXT("FPS %.0f · 帧耗时 %.1f ms · 峰值 %.1f ms"),FrameDiagnostics.FPS,FrameDiagnostics.FrameMs,FrameDiagnostics.PeakMs);
    }
    if (Network)
    {
        if (!Text.IsEmpty()) { Text+=TEXT("\n"); }
        const auto* Driver=GetWorld()->GetNetDriver();
        const auto* Connection=Driver?Driver->ServerConnection.Get():nullptr;
        if (GetWorld()->GetNetMode()==NM_ListenServer) { Text+=TEXT("本机房主 · 无远端往返延迟"); }
        else if (!Connection) { Text+=TEXT("网络：未连接"); }
        else if (Connection->GetLastRecvAckTime()<=0) { Text+=TEXT("网络：等待延迟样本"); }
        else
        {
            const double Ping=Connection->AvgLag>0 ? Connection->AvgLag : Connection->RawPingInSeconds;
            Text+=FString::Printf(TEXT("延迟 RTT %.0f ms · 抖动 %.1f ms\n丢包 收 %.1f%% / 发 %.1f%% · 未收包 %.1f s"),
                Ping*1000,Connection->GetAverageJitterInMS(),
                100*Connection->GetInLossPercentage().GetLossPercentage(),100*Connection->GetOutLossPercentage().GetLossPercentage(),
                FMath::Max(0.,Driver->GetElapsedTime()-Connection->LastReceiveTime));
        }
        auto* Body=Cast<ALDCharacter>(PC->GetPawn());
        const auto* Movement=Body?Cast<ULDCharacterMovementComponent>(Body->GetCharacterMovement()):nullptr;
        if (Movement && !PC->HasAuthority())
        {
            const bool SameBody=DiagnosticsBody.Get()==Body && DiagnosticsSampleAt>0;
            const double Rate=SameBody ? double(Movement->CorrectionCount-DiagnosticsCorrections)/(Now-DiagnosticsSampleAt) : 0.;
            Text+=FString::Printf(TEXT("\n移动校正 %.1f 次/s · 最近 %.1f cm"),Rate,Movement->LastCorrectionCm);
            DiagnosticsBody=Body; DiagnosticsCorrections=Movement->CorrectionCount;
        }
        else { DiagnosticsBody.Reset(); DiagnosticsCorrections=0; }
    }
    // Hidden network rows keep no correction baseline, so showing them again starts a fresh rate window.
    else { DiagnosticsBody.Reset(); DiagnosticsCorrections=0; }
    DiagnosticsSampleAt=Now;
    HudSetText(DiagnosticsText,Text);
}

void ULDHUD::RefreshReadModel()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* GS=GetWorld()->GetGameState<ALDGameState>();if(!GS||!Band){return;}
    const FLDOwnerState& O=PC->OwnerState;const FLDLobbyView& Lobby=GS->DemoLobby;const FLDPublicMatchView& Public=GS->PublicMatch;
    // PublicMatch is the single public phase source; a projection from another generation is never this match.
    const bool bOwnMatch=Public.Generation!=0&&Public.Generation==O.Match;
    const bool Playing=bOwnMatch&&(Public.Phase==ELDMatchPhase::Countdown||Public.Phase==ELDMatchPhase::Development||Public.Phase==ELDMatchPhase::CaptureOpen);
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
    if(bOwnMatch&&PC->SlotOrderMatch!=O.Match){PC->ResetSlotOrder(O.Match);}
    HudSetShown(Band,bShow,ESlateVisibility::Visible);HudSetShown(ShopEntry,bShow,ESlateVisibility::Visible);HudSetShown(PromptFrame,bShow,ESlateVisibility::Visible);HudSetShown(MapColumn,bShow,ESlateVisibility::Visible);HudSetShown(PhaseBanner,bShow,ESlateVisibility::HitTestInvisible);
    HudSetShown(ResultPanel,bResults&&!Overlay,ESlateVisibility::Visible);
    if(bResults&&!Overlay){RefreshResult(*PC,*GS);}
    if(!bShow)
    {
        for(UWidget* Hidden:{static_cast<UWidget*>(TargetCard.Get()),static_cast<UWidget*>(DeathBanner.Get()),static_cast<UWidget*>(ToastPanel.Get()),static_cast<UWidget*>(EventFeedPanel.Get()),
            static_cast<UWidget*>(OpeningCountdownText.Get()),
            static_cast<UWidget*>(ScorePanel.Get()),ActionDetailSlot?ActionDetailSlot->Content.Get():nullptr}){HudSetShown(Hidden,false,ESlateVisibility::Visible);}
        CloseItemMenu();
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

    const FString OwnerName=LDHUD::RosterName(Lobby,Public.GraffitiOwner);
    const LDHUD::FCircleView Circle=LDHUD::Circle(Public,LDHUD::RosterName(Lobby,Public.Occupier),OwnerName,bClock,NowMs,
        Content?Content->Parameters.CaptureSprayMs:0,Content?Content->Parameters.CaptureGuardMs:0);
    HudSetText(PhaseText,Circle.Line);CircleProgress->SetPercent(Circle.Progress);
    HudSetText(DevMarkerText,!Lobby.DevTestSession&&!Lobby.DevContentReload?FString()
        :FString(Lobby.DevTestSession?TEXT("测试模式"):TEXT("开发房间"))+(Lobby.DevContentReload?TEXT(" · 内容热重载"):TEXT("")));
    HudSetShown(DevMarkerText,Lobby.DevTestSession||Lobby.DevContentReload,ESlateVisibility::HitTestInvisible);
    HudSetShown(CircleProgress->GetParent(),Circle.bProgress,ESlateVisibility::HitTestInvisible);
    HudSetText(AirdropText,LDHUD::AirdropLine(Public.Airdrop,bClock,NowMs));
    HudSetShown(AirdropText,!AirdropText->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    HudSetText(LeaderText,LDHUD::LeaderLine(Public.Leader,O.ContenderId,LDHUD::RosterName(Lobby,Public.Leader.Leader)));
    HudSetShown(LeaderText,!LeaderText->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    {
        FString Low,High,Dean;HudCampNames(Content,Low,High,Dean);
        HudSetText(BossText,LDHUD::BossLine(Public.Boss,Dean,bClock,NowMs));
        HudSetShown(BossText,!BossText->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    }
    HudSetText(NextStepText,LDHUD::NextStep(Public,O.ContenderId,PC->ReadOwnProgression(),PC->ReadOwnEquipment(),Content,OwnerName));
    HudSetShown(NextStepText,!NextStepText->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    RefreshEventFeed(*GS,O,Content,bClock,NowMs);
    RefreshScoreboard(*GS,O);
    HudSetText(OpeningCountdownText,LDHUD::OpeningCountdown(Public,bClock,NowMs));
    HudSetShown(OpeningCountdownText,!OpeningCountdownText->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    HudSetText(MatchTimerText,LDHUD::MatchTimer(Public,bClock,NowMs));
    HudSetShown(MatchTimerText,!MatchTimerText->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    const LDHUD::FDeathView Death=LDHUD::Death(O,bClock,NowMs);
    HudSetShown(DeathBanner,Death.bVisible,ESlateVisibility::HitTestInvisible);
    if(Death.bVisible)
    {
        HudSetText(DeathTitle,Death.Title);HudSetText(DeathCountdown,Death.Countdown);HudSetText(DeathNote,Death.Note);
        FString Low,High,Dean;HudCampNames(Content,Low,High,Dean);
        const int64 NotBeforeMs=LifeSeenMatch==O.Match&&LifeSeenLife==O.Life?LifeSeenAliveMs:0;
        const FString Cause=LDHUD::DeathCause(GS->KillFeed,Public.Generation,O.ContenderId,NotBeforeMs,Lobby,Low,High,Dean);
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
        Health=LDHUD::ResourceP2(LDTerms::Health,OwnCombat->HPP2,OwnCombat->MaxHPP2,true);
        Mana=LDHUD::ResourceP2(LDTerms::Mana,OwnCombat->MPP2,OwnCombat->MaxMPP2,false);
        ShieldPoints=double(OwnCombat->ShieldP2)/100.;
    }
    else
    {
        Health=LDHUD::Resource(LDTerms::Health,O.Hp,O.MaxHp,true);
        Mana=LDHUD::Resource(LDTerms::Mana,O.Mp,O.MaxMp,false);
        ShieldPoints=O.Barrier;
    }
    FString HealthLine=Health.Text;
    if(Health.bKnown&&ShieldPoints>0){HealthLine+=FString::Printf(TEXT(" · %s %.0f"),LDTerms::Shield,FMath::CeilToDouble(ShieldPoints));}
    HealthBar->SetPercent(Health.Fraction);HudSetText(HealthText,HealthLine);
    ManaBar->SetPercent(Mana.Fraction);HudSetText(ManaText,Mana.Text);
    HudSetText(HealthRegenText,LDHUD::RegenText(O,OwnCombat,true));
    HudSetText(ManaRegenText,LDHUD::RegenText(O,OwnCombat,false));
    LDHUD::FLevelView Level;
    if(Progression)
    {Level=LDHUD::Level(Progression->Level,Progression->MaxLevel,Progression->CumulativeXP,Progression->LevelThresholdXP,Progression->bHasNextThreshold,Progression->NextThresholdXP);}
    else{Level.Level=TEXT("等级 —");Level.Experience=TEXT("成长数据同步中");}
    LevelBar->SetPercent(Level.Fraction);
    {const FString Tip=Level.Level+TEXT(" · ")+Level.Experience;if(LevelTip!=Tip){LevelTip=Tip;LevelBar->SetToolTipText(FText::FromString(Tip));}}
    const bool bLevelKnown=Progression&&Progression->Level>=1&&Progression->Level<=Progression->MaxLevel;
    HudSetText(LevelBadgeText,bLevelKnown?FString::FromInt(Progression->Level):FString(TEXT("—")));
    {
        // A missing projection still reads as syncing, never as "no status".
        const LDHUD::FSelfIconsView Icons=LDHUD::SelfIcons(Progression,OwnCombat,bClock,NowMs,Content);
        SelfIconViews=Icons.Items;
        HudSetShown(SelfIconSyncing,Icons.bSyncing,ESlateVisibility::HitTestInvisible);
        TArray<FName> LiveRings;
        for(int32 Index=0;Index<SelfIconCells.Num();++Index)
        {
            const LDHUD::FSelfIconView* Icon=Icons.Items.IsValidIndex(Index)?&Icons.Items[Index]:nullptr;
            HudSetShown(SelfIconCells[Index],Icon!=nullptr,ESlateVisibility::Visible);
            SelfIconRing[Index]=-1.f;
            if(!Icon){continue;}
            HudSetText(SelfIconGlyphs[Index],Icon->Glyph);
            HudSetText(SelfIconStacks[Index],Icon->Stacks>1?FString::FromInt(Icon->Stacks):FString());
            if(SelfIconKinds[Index]!=int32(Icon->Kind))
            {
                // Control statuses are edged red, other temporary statuses amber, passives in the quiet secondary colour.
                SelfIconKinds[Index]=int32(Icon->Kind);
                const FLinearColor Edge=Icon->Kind==LDHUD::ESelfIconKind::Control?LDHUDStyle::TextDanger
                    :Icon->Kind==LDHUD::ESelfIconKind::Temporary?LDHUDStyle::TextWarning:LDHUDStyle::TextSecondary;
                SelfIconCells[Index]->SetBrush(HudRound(5.f,Edge,1.f));SelfIconCells[Index]->SetBrushColor(FLinearColor(.02f,.03f,.05f,.9f));
                HudSetColor(SelfIconGlyphs[Index],Edge);
            }
            // The ring needs the matching authority clock; without it the square shows no remaining time.
            if(Icon->ExpiresAtMs>0&&bClock){SelfIconRing[Index]=SelfIconRings.Fraction(Icon->Key,Icon->ExpiresAtMs,NowMs);LiveRings.Add(Icon->Key);}
        }
        SelfIconRings.Retain(LiveRings);
        for(int32 Row=0;Row<SelfIconRowBoxes.Num();++Row)
        {HudSetShown(SelfIconRowBoxes[Row],Icons.Items.Num()>Row*LDHUD::SelfIconColumns,ESlateVisibility::SelfHitTestInvisible);}
        HudSetShown(SelfIconMore,Icons.Overflow>0,ESlateVisibility::Visible);
    }

    // New world commands follow Combat's input half (menu/Settings, child confirmation or text edit, held keys);
    // Combat itself refuses and cancels any armed preview while input is captured.
    const bool bInputSuppressed=PC->IsWorldInputSuppressed();
    if(AttributesPanel){AttributesPanel->Present(OwnCombat,Progression,bClock,NowMs,Content);}
    if(ChoicesPanel)
    {
        // #147: the cards show this level's numbers; while no projection is published (dead) the last stats stay in use.
        if(OwnCombat&&!OwnCombat->Attributes.IsEmpty()){ChoiceStats=LDHUD::CardStatsOf(Progression?Progression->Level:1,OwnCombat->Attributes);}
        else if(Progression){ChoiceStats.Level=FMath::Max<uint8>(Progression->Level,1);}
        ChoicesPanel->Present(Progression?*Progression:FLDProgressionView(),O.Match,O.Life,O.LifePhase,Content,ChoiceStats);
    }
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
            UE_LOG(LogLDUI,Warning,TEXT("LD_HUD_OPENING_CHOICE_NOT_SHOWN The opening three-choice panel could not open within 5 s."));
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
        // Pre-orders buy themselves while the own body stands in the shop's range (the right-click check); the server
        // re-checks reach on every trade, so this only decides when to try.
        FName NearShop;
        const bool bNearShop=Playing&&bAlive&&PC->Navigation&&PC->Navigation->OwnBodyAtShop(NearShop);
        ShopPanel->Present(ShopView,Equipment,Progression!=nullptr,Progression?Progression->BuildRevision:0,O.Match,O.Life,O.LifePhase,Content,bNearShop);
        // A pre-order that could be bought needs this life's session: requested once, then again only every few seconds
        // while it still has none (a refused or lost open result).
        if(bNearShop&&ShopPanel->WantsSession()&&(PreorderOpenLife!=O.Life||UiNow-PreorderOpenAt>=3.))
        {
            PreorderOpenLife=O.Life;PreorderOpenAt=UiNow;
            ELDGrowthReason Reason=ELDGrowthReason::None;
            if(!PC->RequestShopSession(NearShop,O.Life,Reason))
            {UE_LOG(LogLDUI,Warning,TEXT("LD_HUD_PREORDER_SESSION_NOT_SENT %s"),*LDHUD::GrowthReasonText(Reason));}
        }
        // Lit while the own body stands in the shop's range, where buying, selling and pre-orders happen.
        const int8 ShopLit=bNearShop?1:0;
        if(AppliedShopLit!=ShopLit)
        {
            AppliedShopLit=ShopLit;
            ShopButton->SetStyle(HudCornerStyle(ShopSharedStyle,bNearShop));
            HudSetColor(ShopButtonText,bNearShop?HudPromptInk:LDHUDStyle::GoldGain);
        }
        HudSetText(ShopButtonText,(Equipment?FString::Printf(TEXT("%lld %s"),Equipment->Gold,LDTerms::Gold):FString(LDTerms::Gold)+TEXT(" 同步中"))
            +(ShopPanel->Preorders().IsEmpty()?FString():FString::Printf(TEXT(" · 预购 %d"),ShopPanel->Preorders().Num())));
    }
    if(ShopEntry)
    {
        // The shop panel docks in the bottom-right corner, right above the shop entry (whose height follows the text size).
        const float Dock=FMath::RoundToFloat(16.f+float(ShopEntry->GetDesiredSize().Y)+8.f);
        if(AppliedPanelDock!=Dock){AppliedPanelDock=Dock;if(ShopPanel){ShopPanel->SetBottomDock(Dock);}}
    }
    // #97: right above the band's left block, the blind-box button at its left edge and the three-choice panel in the same
    // row right of it, so a card and the Q/W slot it may replace are a short move apart. Sizes follow last frame's layout
    // (text size changes them); the root shows one main panel at a time.
    const float BandTop=12.f+float(Band->GetDesiredSize().Y)*HudBandScale;
    const float BandLeft=(-(HudSideWidth+HudBlockGap+1)*.5f-float(Band->GetDesiredSize().X)*.5f)*HudBandScale;
    const float PromptTop=BandTop+8.f+(PromptFrame?float(PromptFrame->GetDesiredSize().Y):0.f);
    if(PromptSlot)
    {
        const FVector2D Place(FMath::RoundToFloat(BandLeft),-FMath::RoundToFloat(BandTop+8.f));
        if(!PromptSlot->GetPosition().Equals(Place,.5f)){PromptSlot->SetPosition(Place);}
    }
    if(ChoicesPanel&&PromptFrame)
    {
        const float Left=FMath::RoundToFloat(float(GetCachedGeometry().GetLocalSize().X)*.5f+BandLeft+float(PromptFrame->GetDesiredSize().X)+8.f);
        const float Dock=FMath::RoundToFloat(BandTop+8.f);
        if(AppliedChoicesDock!=Dock||AppliedChoicesLeft!=Left){AppliedChoicesDock=Dock;AppliedChoicesLeft=Left;ChoicesPanel->SetBottomDock(Dock,Left);}
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
    if(ULDCommandButton* Sheet=Commands.FindRef(TEXT("Attributes"))){HudSetText(CastChecked<UTextBlock>(Sheet->GetContent()),TEXT("属性 [")+PC->BindingLabel(TEXT("Attributes"))+TEXT("]"));}
    // The blind-box button stays above the left block: greyed out and disabled without a pending choice (an open panel with nothing
    // left closes itself), lit with the count otherwise; after the count rises it pulses at ~1 Hz for a few seconds.
    if(Pending>PromptCount){PromptPulseAt=UiNow;}
    PromptCount=Pending;
    HudSetText(PromptText,FString::Printf(TEXT("%s+%d"),LDTerms::Choice,Pending));
    HudSetColor(PromptText,Pending>0?HudPromptInk:HudPromptIdleInk);
    if(PromptButton->GetIsEnabled()!=(Pending>0)){PromptButton->SetIsEnabled(Pending>0);}
    {
        const double PulseAge=UiNow-PromptPulseAt;
        const float Wave=Pending>0&&PromptPulseAt>=0&&PulseAge<HudPulseSeconds?.5f-.5f*FMath::Cos(2.f*UE_PI*float(PulseAge)):0.f;
        if(AppliedPromptWave!=Wave){AppliedPromptWave=Wave;const float Scale=1.f+.05f*Wave;PendingPrompt->SetRenderScale(FVector2D(Scale,Scale));PendingPrompt->SetRenderOpacity(1.f-.25f*Wave);}
    }

    ELDCombatEntry ArmedEntry=ELDCombatEntry::None;const bool bArmed=PC->ReadArmedCombatEntry(ArmedEntry);
    if(PC->CombatReceipts.Serial()!=SeenCombatReceipt)
    {
        TArray<FLDCombatActionReceipt> Arrived;
        if(const uint32 Lost=PC->CombatReceipts.ReadSince(SeenCombatReceipt,Arrived)){UE_LOG(LogLDUI,Warning,TEXT("LD_HUD_COMBAT_RECEIPTS_MISSED %u"),Lost);}
        // Every receipt since the last frame presents once; the single toast line joins their distinct notices in order.
        const FString Notice=LDHUD::CombatReceiptsNotice(Arrived,O.Match,O.Life,ToldProtectionLifeRevision);
        if(!Notice.IsEmpty()){ShowToast(Notice);}
        for(const FLDCombatActionReceipt& Receipt:Arrived)
        {
            if(Receipt.Generation!=O.Match||Receipt.Life!=O.Life){continue;}
            // The pressed slot's ring turns red for a moment where the player is looking; the toast says why.
            const bool bSlotRefused=!LDHUD::CombatReceiptSlotNotice(Receipt).IsEmpty();
            for(int32 Pressed=0;Pressed<ActionSlots.Num()&&bSlotRefused;++Pressed)
            {if((Pressed<2?PC->SkillAtKey[Pressed]:HudActionEntries[Pressed])==Receipt.Entry){ActionSlots[Pressed].RejectedAt=UiNow;}}
        }
    }
    uint8 ControlBits=0;
    if(OwnCombat){for(const FLDCombatStatusView& Status:OwnCombat->Statuses){ControlBits|=Status.ControlBlocks;}}
    // The slot under the pointer (by its laid-out box, so a disabled slot explains itself as well) gets the description.
    const FVector2D Pointer=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetCursorPos():FVector2D(-1,-1);
    int32 DetailIndex=INDEX_NONE;LDHUD::FActionDetailView DetailView;
    for(int32 Index=0;Index<ActionSlots.Num();++Index)
    {
        const LDHUD::EActionSlot SlotKind=static_cast<LDHUD::EActionSlot>(Index);
        // Q/W positions show the entries the local slot order puts there (#97 drag to swap).
        const ELDCombatEntry Wanted=Index<2?PC->SkillAtKey[Index]:HudActionEntries[Index];
        LDHUD::FActionInput Input;Input.Slot=SlotKind;
        FGuid HeldItem;
        if(LDHUD::IsEquipmentSlot(SlotKind))
        {
            const uint8 SlotIndex=PC->EquipmentAtPosition[SlotKind==LDHUD::EActionSlot::Equipment1?0:1];
            const FLDEquipmentSlotView* Held=Equipment?Equipment->Slots.FindByPredicate([SlotIndex](const FLDEquipmentSlotView& Row){return Row.SlotIndex==SlotIndex;}):nullptr;
            if(Held)
            {
                Input=LDHUD::EquipmentInput(*Held,SlotKind,Content);
                if(Held->bOccupied){HeldItem=Held->Instance;}
            }
        }
        else if(const FLDCombatEntryView* Source=OwnCombat?OwnCombat->Entries.FindByPredicate([Wanted](const FLDCombatEntryView& Candidate){return Candidate.Entry==Wanted;}):nullptr)
        {
            Input=LDHUD::ActionInput(*Source,SlotKind,Content);Input.bControlled=(ControlBits&HudActionControl[Index])!=0;
        }
        LDHUD::FActionContext Context;Context.KeyLabel=HudActionBindings[Index].IsNone()?FString():PC->BindingLabel(HudActionBindings[Index]);
        Context.bOwnContext=bOwnMatch&&O.Life!=0;
        Context.bPlaying=Playing;Context.bAlive=bAlive;Context.bClock=bClock;Context.NowMs=NowMs;
        Context.bInputCaptured=bInputSuppressed;
        Context.bManaKnown=OwnCombat&&OwnCombat->bHasBody&&OwnCombat->MaxMPP2>0;Context.ManaP2=OwnCombat?OwnCombat->MPP2:0;
        Context.Card=LDHUD::CardStatsOf(Progression?Progression->Level:1,OwnCombat?OwnCombat->Attributes:TArray<FLDCombatAttributeRowView>());
        const LDHUD::FActionView View=LDHUD::Action(Input,Context);
        FLDHUDActionSlot& Entry=ActionSlots[Index];
        const LDHUD::EActionLook Look=LDHUD::ActionLook(View,bArmed&&ArmedEntry==Wanted);
        const bool bCooling=View.CooldownRemainingMs>0;
        // A cooldown that runs out on a usable slot within the same life flashes that slot once.
        if(Entry.bCooling&&!bCooling&&Entry.CoolingMatch==O.Match&&Entry.CoolingLife==O.Life&&Look==LDHUD::EActionLook::Ready){Entry.FlashAt=UiNow;}
        Entry.bCooling=bCooling;Entry.CoolingMatch=O.Match;Entry.CoolingLife=O.Life;
        const FHudLookStyle Style=HudLookStyle(Look);
        const EHudIconKind IconKind=(View.ExecutionBlocks&LDHUD::ActionEmpty)?EHudIconKind::Empty:(View.ExecutionBlocks&LDHUD::ActionPassive)?EHudIconKind::Passive
            :SlotKind==LDHUD::EActionSlot::Dash?EHudIconKind::Dash:EHudIconKind::Active;
        const bool bHover=Entry.Button->IsHovered();
        const bool bRejected=Entry.RejectedAt>=0&&UiNow-Entry.RejectedAt<HudNoticeSeconds;
        // #97 replacement pick: a filled Q/W slot glows red on its edge; clicking it forgets that skill.
        const bool bReplacePick=Index<2&&ChoicesPanel&&ChoicesPanel->IsReplacementTarget(Wanted==ELDCombatEntry::W?1:0);
        Entry.bHasSource=Index<4&&Input.bHasSource;
        const int32 LookKey=(((int32(Look)*4+int32(IconKind))*2+(bHover?1:0))*2+(bRejected?1:0))*2+(bReplacePick?1:0);
        if(Entry.AppliedLook!=LookKey)
        {
            // Hovering brightens the edge and a refused press turns the ring red; the fill and the icon keep naming the state.
            Entry.AppliedLook=LookKey;
            const FLinearColor Edge=bRejected||bReplacePick?LDHUDStyle::TextDanger:bHover?Style.Edge+(FLinearColor::White-Style.Edge)*.4f:Style.Edge;
            const float EdgeWidth=bReplacePick?4.f:bRejected?3.f:bHover?FMath::Max(Style.EdgeWidth,2.f):Style.EdgeWidth;
            Entry.Frame->SetBrush(HudRound(8.f,Edge,EdgeWidth));Entry.Frame->SetBrushColor(bRejected?FMath::Lerp(Style.Fill,LDHUDStyle::TextDanger,.5f):Style.Fill);
            Entry.Icon->SetBrushColor(HudIconColor(IconKind,Look));
            HudSetColor(Entry.Glyph,Look==LDHUD::EActionLook::Blocked||Look==LDHUD::EActionLook::Empty?LDHUDStyle::TextSecondary:LDHUDStyle::TextPrimary);
        }
        HudSetText(Entry.Key,View.Key);HudSetText(Entry.Glyph,View.Glyph);
        HudSetText(Entry.Rank,View.Rank);HudSetShown(Entry.RankChip,!View.Rank.IsEmpty(),ESlateVisibility::HitTestInvisible);
        HudSetText(Entry.Cost,View.Cost);HudSetShown(Entry.CostChip,!View.Cost.IsEmpty(),ESlateVisibility::HitTestInvisible);
        Entry.ItemInstance=HeldItem;
        HudSetShown(Entry.PresellMark,HeldItem.IsValid()&&ShopPanel&&ShopPanel->IsPresell(HeldItem),ESlateVisibility::HitTestInvisible);
        Entry.Cooldown->SetPercent(View.CooldownFraction);HudSetShown(Entry.Cooldown,bCooling,ESlateVisibility::HitTestInvisible);
        HudSetText(Entry.Countdown,View.Countdown);HudSetShown(Entry.Countdown,bCooling,ESlateVisibility::HitTestInvisible);
        // An empty slot or an equipment slot fades as a whole; the ready flash pops the slot and fades out.
        const float Opacity=Look==LDHUD::EActionLook::Empty?.45f:1.f;
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
        Entry.Button->SetIsEnabled(View.bCanSubmitIntent||bReplacePick);
        // The open item menu replaces the description.
        if(DetailIndex==INDEX_NONE&&ItemMenuIndex==INDEX_NONE&&Entry.Box->GetCachedGeometry().IsUnderLocation(Pointer)){DetailIndex=Index;DetailView=LDHUD::ActionDetail(View,Input,Context,Content);}
    }
    // The item menu belongs to one held instance: it closes when that item leaves its slot.
    if(ItemMenuIndex!=INDEX_NONE)
    {
        if(!ActionSlots.IsValidIndex(ItemMenuIndex)||ActionSlots[ItemMenuIndex].ItemInstance!=ItemMenuInstance){CloseItemMenu();}else{PlaceItemMenu();}
    }
    // Without a slot under the pointer, a hovered status square (LDHUD::SelfIconDetail) uses the same popup.
    const UWidget* DetailOver=DetailIndex!=INDEX_NONE?ActionSlots[DetailIndex].Box.Get():nullptr;
    if(!DetailOver&&ItemMenuIndex==INDEX_NONE)
    {
        for(int32 Index=0;Index<FMath::Min(SelfIconCells.Num(),SelfIconViews.Num());++Index)
        {
            if(SelfIconCells[Index]->IsVisible()&&SelfIconCells[Index]->GetCachedGeometry().IsUnderLocation(Pointer))
            {DetailOver=SelfIconCells[Index];DetailView=LDHUD::SelfIconDetail(SelfIconViews[Index],
                LDHUD::CardStatsOf(Progression?Progression->Level:1,OwnCombat?OwnCombat->Attributes:TArray<FLDCombatAttributeRowView>()),Content);break;}
        }
    }
    if(UWidget* Popup=ActionDetailSlot?ActionDetailSlot->Content:nullptr)
    {
        HudSetShown(Popup,DetailOver!=nullptr,ESlateVisibility::HitTestInvisible);
        if(DetailOver)
        {
            HudSetText(ActionDetailName,DetailView.Name);HudSetText(ActionDetailSummary,DetailView.Summary);
            HudSetText(ActionDetailBody,DetailView.Body);HudSetShown(ActionDetailBody,!DetailView.Body.IsEmpty(),ESlateVisibility::HitTestInvisible);
            // Centred 8 units above the slot or square, read from last frame's layout; neither depends on the popup, so
            // this never feeds back. The popup is only pushed sideways to stay on screen.
            const FGeometry& Area=GetCachedGeometry();const FGeometry& Over=DetailOver->GetCachedGeometry();
            const FVector2D Top=Area.AbsoluteToLocal(Over.GetAbsolutePosition());
            const FVector2D Size=Area.AbsoluteToLocal(Over.GetAbsolutePosition()+Over.GetAbsoluteSize())-Top;
            const float Half=HudDetailWidth*.5f+8.f;const float Wide=float(Area.GetLocalSize().X);
            const float X=Wide>2*Half?FMath::Clamp(float(Top.X+Size.X*.5),Half,Wide-Half):float(Top.X+Size.X*.5);
            const FVector2D Place(FMath::RoundToFloat(X),FMath::RoundToFloat(float(Top.Y)-8.f));
            if(!ActionDetailSlot->GetPosition().Equals(Place,.5f)){ActionDetailSlot->SetPosition(Place);}
        }
    }

    const ALDWorldViewRelay* Relay=PC->LocalView();
    const FLDObservedFrameRead Frame=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
    const bool bSightCurrent=Frame.Frame&&Frame.Status==ELDObservedViewStatus::Current;
    // The map already shows a normal current view; only a syncing, stale or dead view needs words under it.
    HudSetText(SightText,!bAlive?FString(LDTerms::Dead)+TEXT("：仅显示静态地图与公开信息")
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
        HudSetShown(TargetActivesRow,Card.bActives,ESlateVisibility::HitTestInvisible);
        for(int32 Index=0;Card.bActives&&Index<TargetActiveTiles.Num();++Index)
        {
            const LDHUD::FObservedCard::FActiveChip& Chip=Card.Actives[Index];
            HudSetText(TargetActiveNames[Index],Chip.Key+TEXT(" · ")+Chip.Name);
            HudSetText(TargetActiveRanks[Index],Chip.bEmpty?FString(TEXT("—")):Chip.Rank);
            HudSetColor(TargetActiveRanks[Index],Chip.bMaxRank?LDHUDStyle::TextAccent:LDHUDStyle::TextSecondary);
            TargetActiveTiles[Index]->SetBrush(HudRound(6.f,Chip.bMaxRank?LDHUDStyle::TextAccent:LDHUDStyle::Rim,Chip.bMaxRank?2.f:1.f));
            TargetActiveTiles[Index]->SetBrushColor(FLinearColor(.02f,.03f,.05f,Chip.bEmpty?.55f:.9f));
        }
    }

    // The toast fades on local presentation time.
    if(SeenIntent!=PC->IntentFeedback||SeenFeedback!=PC->FeedbackSerial()){SeenIntent=PC->IntentFeedback;SeenFeedback=PC->FeedbackSerial();ShowToast(PC->IntentFeedback);}
    // The replacement pick holds its prompt until a slot is clicked or the pick ends; a notice that arrives meanwhile
    // (a refused cast, say) shows for its usual time and then the prompt returns.
    const bool bReplacePick=ChoicesPanel&&ChoicesPanel->HasChildConfirmation();
    if(bReplacePick&&!bReplacePromptShown){ReplacePromptSince=UiNow;}
    bReplacePromptShown=bReplacePick;
    const bool bFreshToast=UiNow-FeedbackAt<5&&(!bReplacePick||FeedbackAt>=ReplacePromptSince);
    HudSetText(Toast,bFreshToast?LastFeedback:bReplacePick?FString(LDHUDReplacePrompt):FString());
    HudSetShown(ToastPanel,!Toast->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    if(ToastSlot)
    {
        // Above the blind-box button, or above the three-choice panel while it is open (its replacement prompt reads there).
        const float Under=ChoicesPanel&&ChoicesPanel->IsPanelOpen()?ChoicesPanel->PanelTop():PromptTop;
        const FVector2D Above(0,-FMath::RoundToFloat(Under+8.f));
        if(!ToastSlot->GetPosition().Equals(Above,.5f)){ToastSlot->SetPosition(Above);}
    }
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
        if(const uint32 Lost=PC.CombatReceipts.ReadSince(AudioCombatReceipt,Arrived)){UE_LOG(LogLDUI,Warning,TEXT("LD_AUDIO_COMBAT_RECEIPTS_MISSED %u"),Lost);}
        for(const FLDCombatActionReceipt& Receipt:Arrived){AudioCues.ObserveCombatReceipt(Receipt,O.Match,O.Life,Cues);}
    }
    PlayAudioCues(Cues);
}
void ULDHUD::PlayAudioCues(const TArray<ELDAudioCue>& Cues)
{
    if(Cues.IsEmpty()){return;}
    UGameInstance* Instance=GetGameInstance();ULDAudioSubsystem* Audio=Instance?Instance->GetSubsystem<ULDAudioSubsystem>():nullptr;
    if(!Audio){UE_LOG(LogLDUI,Warning,TEXT("LD_AUDIO_SUBSYSTEM_UNAVAILABLE %d cue(s) not played."),Cues.Num());return;}
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
    case ELDShopOpenStatus::Pending: return;
    case ELDShopOpenStatus::Rejected:
    {
        const FString Why=LDHUD::GrowthReasonText(LDGrowthWire::Reason(Result.Reason));
        ShowToast(FString(LDTerms::Shop)+TEXT("没有打开：")+(Why.IsEmpty()?FString(TEXT("原因未知")):Why));return;
    }
    case ELDShopOpenStatus::Opened:
        if(!Result.bHasView||!Result.View.SessionId.IsValid())
        {
            UE_LOG(LogLDUI,Error,TEXT("LD_HUD_SHOP_OPENED_WITHOUT_VIEW Shop %s opened without a session quote."),*Result.ShopId.ToString());
            ShowToast(FString(LDTerms::Shop)+TEXT("结果缺少报价，没有打开"));return;
        }
        OpenedShop=Result;OpenedShopPublish=PC->OwnerState.Growth.PublishRevision;bOpenedShop=true;
        // Arrival only opens the session; the page is opened by a left click on the model or the shop button.
        return;
    default:
        UE_LOG(LogLDUI,Error,TEXT("LD_HUD_SHOP_OPEN_STATUS_INVALID Shop %s returned open status %d."),*Result.ShopId.ToString(),int32(Result.OpenStatus));
        return;
    }
}
void ULDHUD::RefreshScoreboard(const ALDGameState& Public,const FLDOwnerState& Own)
{
    // Shown only while the Scoreboard key is held; a text edit or a child confirmation keeps the key for itself.
    const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    const FKey Key=PC->BindingKey(TEXT("Scoreboard"));
    const bool bHeld=Key.IsValid()&&PC->IsInputKeyDown(Key)&&!PC->IsWorldInputCaptured();
    HudSetShown(ScorePanel,bHeld,ESlateVisibility::HitTestInvisible);
    if(!bHeld){return;}
    const TArray<LDHUD::FScoreLine> Lines=LDHUD::Scoreboard(Public.KillFeed,Own.Match,Public.DemoLobby,Own.ContenderId);
    for(int32 RowIdx=0;RowIdx<ScoreRows.Num();++RowIdx)
    {
        // Without this match's scores there is no zero-filled table, only the syncing line.
        const bool bLine=Lines.IsValidIndex(RowIdx);
        HudSetShown(ScoreRows[RowIdx],bLine||(RowIdx==0&&Lines.IsEmpty()),ESlateVisibility::HitTestInvisible);
        UTextBlock* Cells[4]={ScoreCells[RowIdx*4].Get(),ScoreCells[RowIdx*4+1].Get(),ScoreCells[RowIdx*4+2].Get(),ScoreCells[RowIdx*4+3].Get()};
        if(!bLine)
        {
            if(RowIdx==0){HudSetText(Cells[0],TEXT("统计同步中…"));for(int32 Col=1;Col<4;++Col){HudSetText(Cells[Col],FString());}}
            continue;
        }
        const LDHUD::FScoreLine& Line=Lines[RowIdx];
        HudSetText(Cells[0],Line.Name);HudSetText(Cells[1],FString::Printf(TEXT("%llu"),Line.Kills));
        HudSetText(Cells[2],FString::Printf(TEXT("%llu"),Line.Deaths));HudSetText(Cells[3],FString::Printf(TEXT("%llu"),Line.Assists));
        for(int32 Col=0;Col<4;++Col){Cells[Col]->SetColorAndOpacity(Line.bSelf?LDHUDStyle::TextAccent:LDHUDStyle::TextPrimary);}
    }
}
void ULDHUD::RefreshEventFeed(const ALDGameState& Public,const FLDOwnerState& Own,const FLDDemoContentDefinitions* Content,bool bClock,int64 NowMs)
{
    LDHUD::FEventFeedInput Input;
    Input.Public=&Public.PublicMatch;Input.Kills=&Public.KillFeed;Input.Lobby=&Public.DemoLobby;Input.Self=Own.ContenderId;
    // The own claim only counts within this match; a previous match's serial never names this drop.
    Input.OwnAirdropClaimSerial=Own.Match==Public.PublicMatch.Generation?Own.Growth.AirdropClaimedSerial:0;
    Input.bClock=bClock;Input.NowMs=NowMs;Input.LocalSeconds=GetWorld()->GetRealTimeSeconds();
    HudCampNames(Content,Input.LowCampName,Input.HighCampName,Input.BossName);
    EventFeed.Observe(Input);
    const TArray<LDHUD::FEventLine>& Lines=EventFeed.Lines();
    // One row per line, so the lines about the player (said with 你) stand out; each fades out over its last half second.
    for(int32 Row=0;Row<EventFeedRows.Num();++Row)
    {
        UTextBlock* FeedLine=EventFeedRows[Row];const bool bLine=Lines.IsValidIndex(Row);
        HudSetText(FeedLine,bLine?Lines[Row].Text:FString());HudSetColor(FeedLine,bLine&&Lines[Row].bSelf?LDHUDStyle::TextAccent:LDHUDStyle::TextPrimary);
        HudSetShown(FeedLine,bLine,ESlateVisibility::HitTestInvisible);
        const float Opacity=bLine?float(FMath::Clamp((LDHUD::FEventFeed::LineSeconds-(Input.LocalSeconds-Lines[Row].At))/.5,0.,1.)):1.f;
        if(FeedLine->GetRenderOpacity()!=Opacity){FeedLine->SetRenderOpacity(Opacity);}
    }
    HudSetShown(EventFeedPanel,!Lines.IsEmpty(),ESlateVisibility::HitTestInvisible);
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
    auto Row=[&](const FString& Name,const FString& Kills,const FString& Deaths,const FString& Assists,const FString& Level,const FString& Mark)
    {
        auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
        Line->AddChildToHorizontalBox(Cell(Name,280));Line->AddChildToHorizontalBox(Cell(Kills,90));Line->AddChildToHorizontalBox(Cell(Deaths,90));
        Line->AddChildToHorizontalBox(Cell(Assists,70));Line->AddChildToHorizontalBox(Cell(Level,100));Line->AddChildToHorizontalBox(Cell(Mark,80));
        ResultRows->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,4));
    };
    Row(TEXT("名字"),LDTerms::ResultKills,LDTerms::ResultDeaths,LDTerms::ResultAssists,TEXT("最终等级"),FString());
    for(const FLDResultRow& Entry:Result.Rows)
    {
        const LDHUD::FResultRowView View=LDHUD::ResultRow(Entry,Result.Winner,PC.OwnerState.ContenderId);
        Row(View.Name,View.Kills,View.Deaths,View.Assists,View.Level,View.bWinner?FString(LDTerms::Winner):FString());
    }
}
int32 ULDHUD::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    // Remaining-time rings of the timed own statuses: a dim full track and a bright arc that shrinks clockwise back to
    // twelve o'clock as the status runs out. Drawn whenever the band shows, also while world input is held.
    if(Band&&Band->IsVisible())
    {
        for(int32 Index=0;Index<SelfIconCells.Num();++Index)
        {
            const UBorder* Cell=SelfIconCells[Index];
            if(SelfIconRing[Index]<0.f||!Cell||!Cell->IsVisible()){continue;}
            const FGeometry& CellGeometry=Cell->GetCachedGeometry();
            const FVector2D TopLeft=G.AbsoluteToLocal(CellGeometry.LocalToAbsolute(FVector2D::ZeroVector));
            const FVector2D BottomRight=G.AbsoluteToLocal(CellGeometry.LocalToAbsolute(CellGeometry.GetLocalSize()));
            const FVector2D Centre=(TopLeft+BottomRight)*.5f;const float Radius=FMath::Min(BottomRight.X-TopLeft.X,BottomRight.Y-TopLeft.Y)*.5f-1.5f;
            if(Radius<=2.f){continue;}
            auto Arc=[&](float Fraction,const FLinearColor& Color,float Thickness)
            {
                const int32 Steps=FMath::Max(2,FMath::CeilToInt(48.f*Fraction));TArray<FVector2D> Points;Points.Reserve(Steps+1);
                for(int32 Step=0;Step<=Steps;++Step)
                {const float Angle=2.f*PI*Fraction*Step/Steps;Points.Add(Centre+FVector2D(FMath::Sin(Angle),-FMath::Cos(Angle))*Radius);}
                FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Thickness);
            };
            const FLinearColor Edge=SelfIconKinds[Index]==int32(LDHUD::ESelfIconKind::Control)?LDHUDStyle::TextDanger:LDHUDStyle::TextWarning;
            Arc(1.f,FLinearColor(1.f,1.f,1.f,.16f),3.f);
            if(SelfIconRing[Index]>0.f){Arc(SelfIconRing[Index],Edge,3.f);}
        }
    }
    if(!PC->CanWorldCommand()||!Band){return Base;}
    auto Lines=[&](TArray<FVector2D> Points,FLinearColor Color,float Thickness=2.f){FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Thickness);};
    auto Project=[&](FVector P,FVector2D& Out){FVector2D Screen;if(!PC->ProjectWorldLocationToScreen(P,Screen)){return false;}int32 W=0,H=0;PC->GetViewportSize(W,H);Out=Screen*G.GetLocalSize()/FVector2D(FMath::Max(1,W),FMath::Max(1,H));return true;};
    // The inspected unit is marked only by the selection ring under its feet; the HUD draws no bracket over it.
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
        // A faint standing ring of the own basic attack reach (the projected attack range, so range growth shows at once).
        const FVector Feet=Self->PresentedLocation()-FVector(0,0,Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
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
            // A ground point previews where the zone would sit: the pointer, clamped to the reach.
            bPoint=Source&&Source->TargetMode==uint8(ELDDemoTargetMode::GroundPoint);bDirection|=bPoint;
        }
        else{RangeCm=PC->CombatView.AttackRangeCm;bUnitTarget=true;}
        const FVector AuthorityFeet=Self->GetActorLocation()-FVector(0,0,Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const FVector Feet=Self->PresentedLocation()-FVector(0,0,Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        FLinearColor Preview(1,.85f,.45f,.9f);
        if(bUnitTarget&&RangeCm>0)
        {
            // The unit the pointer would pick (the same selection as a click), from the current authorized frame only, turns
            // the range ring green when its feet are within the server's foot-to-foot range and red otherwise; the unit
            // itself gets no frame (its hover ring already marks it).
            FGuid Handle;uint32 Epoch=0;FLDObservedUnitView Hovered;
            const ALDWorldViewRelay* Relay=PC->LocalView();
            if(PC->ObservedUnderPointer(Handle,Epoch)&&Relay&&Relay->ReadUnit(Handle,Epoch,Hovered))
            {
                const bool bInRange=FVector::Dist2D(AuthorityFeet,FVector(Hovered.FeetCm))<=RangeCm;
                Preview=bInRange?FLinearColor(.35f,1.f,.45f,.95f):FLinearColor(1.f,.35f,.3f,.95f);
            }
        }
        if(RangeCm>0){RangeRing(Feet,RangeCm,Preview,1.5f);}
        FHitResult GroundHit;
        if(bDirection&&PC->Navigation&&PC->Navigation->GroundUnderPointer(GroundHit))
        {
            // Aimed as ConfirmArmedCombat submits it (capsule feet to the pointer's ground hit), drawn from the shown feet;
            // only the planar direction is previewed, at the entry's range when known.
            FVector Planar=GroundHit.ImpactPoint-AuthorityFeet;Planar.Z=0;
            if(!Planar.IsNearlyZero())
            {
                const FVector End=Feet+(RangeCm>0?Planar.GetSafeNormal()*(bPoint?FMath::Min(double(RangeCm),Planar.Size()):double(RangeCm)):Planar);
                FVector2D From,To;
                if(Project(Feet,From)&&Project(End,To)){Lines({From,To},Preview,3.f);}
            }
        }
    }
    return Base+1;
}
