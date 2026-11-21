#include "UI/LDHUD.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDResultBuild.h"
#include "UI/LDRecapChain.h"
#include "UI/LDChainCodex.h"
#include "Combat/LDChainRules.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTween.h"
#include "UI/LDTerms.h"

#include "UI/LDChoicesPanel.h"
#include "UI/LDComponentStorage.h"
#include "UI/LDWorkshop.h"
#include "UI/LDWorkshopModel.h"
#include "Scalability.h"
#include "Engine/Texture2D.h"
#include "UI/LDStatBar.h"
#include "UI/LDDevTestPanel.h"
#include "UI/LDMinimap.h"
#include "UI/LDWidgets.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDUIRoot.h"
#include "UI/LDUIIcons.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDBuildBlueprintComponent.h"
#include "Framework/LDGameState.h"
#include "Framework/LDDemoEntry.h"
#include "World/LDPKRingMarker.h"
#include "World/LDSignupMarker.h"
#include "World/LDShowdownRingMarker.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Character/LDHitFeel.h"
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
#include "Components/ScaleBoxSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"

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
// The result table's column heads as small line glyphs (report p.21): kills a crosshair, deaths a tombstone, assists two
// people, final level a double chevron, guard a shield, sprays a can, the winner a crown. Drawn centred in the layer.
enum class EHudResultGlyph : uint8 { Kills=1, Deaths, Assists, Level, Guard, Spray, Winner };
void HudResultGlyph(const LDUIPaint::FContext& P,EHudResultGlyph Glyph,const FLinearColor& Ink)
{
    const FVector2D C=FVector2D(P.Geometry.GetLocalSize())*.5f;
    const auto At=[&C](float X,float Y){return C+FVector2D(X,Y)*1.5f;};
    const auto Path=[&At](std::initializer_list<FVector2D> Points){TArray<FVector2D> Out;for(const FVector2D& Point:Points){Out.Add(At(float(Point.X),float(Point.Y)));}return Out;};
    const float W=2.4f;
    switch(Glyph)
    {
    case EHudResultGlyph::Kills:
        P.Arc(P.Layer,C,10.5f,1.f,Ink,W);
        P.Line(P.Layer,At(-11,0),At(-4,0),Ink,W);P.Line(P.Layer,At(4,0),At(11,0),Ink,W);
        P.Line(P.Layer,At(0,-11),At(0,-4),Ink,W);P.Line(P.Layer,At(0,4),At(0,11),Ink,W);
        P.Fill(P.Layer,FBox2D(At(-1.5f,-1.5f),At(1.5f,1.5f)),Ink,1.5f);
        break;
    case EHudResultGlyph::Deaths:
        P.Stroke(P.Layer,Path({{-7,9},{-7,-3},{-5,-7},{-2,-9},{2,-9},{5,-7},{7,-3},{7,9}}),Ink,W);
        P.Line(P.Layer,At(-10,9),At(10,9),Ink,W);
        P.Line(P.Layer,At(0,-5),At(0,4),Ink,W);P.Line(P.Layer,At(-3,-2),At(3,-2),Ink,W);
        break;
    case EHudResultGlyph::Assists:
        P.Arc(P.Layer,At(-4,-4),4.5f,1.f,Ink,W);P.Arc(P.Layer,At(5,-5),4.5f,1.f,Ink,W);
        P.Stroke(P.Layer,Path({{-10,9},{-9,4},{-6,1.5f},{-2,1.5f},{1,4},{2,9}}),Ink,W);
        P.Stroke(P.Layer,Path({{4,1},{8,1},{10,4},{11,9}}),Ink,W);
        break;
    case EHudResultGlyph::Level:
        P.Stroke(P.Layer,Path({{-7,2},{0,-5},{7,2}}),Ink,W+.4f);
        P.Stroke(P.Layer,Path({{-7,9},{0,2},{7,9}}),Ink,W+.4f);
        P.Line(P.Layer,At(-7,-10),At(7,-10),Ink,W);
        break;
    case EHudResultGlyph::Guard:
        P.Stroke(P.Layer,Path({{0,-10},{8,-6},{8,1},{0,10},{-8,1},{-8,-6},{0,-10}}),Ink,W);
        P.Line(P.Layer,At(0,-6),At(0,6),Ink.CopyWithNewOpacity(Ink.A*.6f),W);
        break;
    case EHudResultGlyph::Spray:
        P.Frame(P.Layer,LDUIShapes::EShape::Rounded,FBox2D(At(-7,-3),At(3,10)),Ink,W,2.f);
        P.Fill(P.Layer,FBox2D(At(-4,-8),At(0,-3)),Ink,1.f);
        for(const FVector2D& Dot:{FVector2D(5,-9),FVector2D(8,-6),FVector2D(6,-3.5f),FVector2D(10,-10),FVector2D(10,-3)})
        {P.Fill(P.Layer,FBox2D(At(float(Dot.X)-1.f,float(Dot.Y)-1.f),At(float(Dot.X)+1.f,float(Dot.Y)+1.f)),Ink,1.f);}
        break;
    case EHudResultGlyph::Winner:
        P.Stroke(P.Layer,Path({{-9,6},{-9,-4},{-4.5f,1},{0,-8},{4.5f,1},{9,-4},{9,6},{-9,6}}),Ink,W);
        break;
    }
}
// The band's slots in LDHUD::EActionSlot order (Q W E | dash), which is also the order they are shown in.
const FName HudActionBindings[]={TEXT("Skill1"),TEXT("Skill2"),TEXT("SkillE"),TEXT("Dash")};
const ELDCombatEntry HudActionEntries[]={ELDCombatEntry::Q,ELDCombatEntry::W,ELDCombatEntry::E,ELDCombatEntry::Dash};
const FName HudActionNames[]={TEXT("HUDActionQ"),TEXT("HUDActionW"),TEXT("HUDActionE"),TEXT("HUDActionDash")};
constexpr int32 HudActionCount=UE_ARRAY_COUNT(HudActionEntries);
// Status control bits that forbid each entry kind: character actives cast, the dash displaces.
const uint8 HudActionControl[]={uint8(ELDCombatControlBlock::Cast),uint8(ELDCombatControlBlock::Cast),
    uint8(ELDCombatControlBlock::Cast),uint8(ELDCombatControlBlock::Displacement)};
static_assert(UE_ARRAY_COUNT(HudActionBindings)==HudActionCount && UE_ARRAY_COUNT(HudActionNames)==HudActionCount
    && UE_ARRAY_COUNT(HudActionControl)==HudActionCount,"Every HUD action entry has a binding, a name and a control bit.");
// The server slot (0..2) of a Q/W/E combat entry, or -1.
int32 HudServerSlot(ELDCombatEntry Entry)
{
    return Entry==ELDCombatEntry::Q?0:Entry==ELDCombatEntry::W?1:Entry==ELDCombatEntry::E?2:-1;
}
// The key each server slot is fired with, as its HUD slot shows it (the local slot order puts the slot under a skill key).
LDHUD::FSlotKeys HudSlotKeys(const ALDPlayerController& PC)
{
    LDHUD::FSlotKeys Keys;
    for(int32 Index=0;Index<3;++Index)
    {
        const int32 Slot=HudServerSlot(PC.SkillAtKey[Index]);
        if(Slot>=0){Keys.Keys[Slot]=PC.BindingLabel(HudActionBindings[Index]);}
    }
    return Keys;
}
// Band geometry in Slate units at the 1080p reference (720p draws the same layout at ~0.667). The band is placed so its
// centre block, the body and 3+1 action entries over the resources, sits on the screen centre directly under the character.
// The whole band draws at HudBandScale of these sizes, so a skill slot is 84 units at the 1080p reference.
constexpr float HudBandScale=.875f;
// A press on a slot becomes a drag once the pointer moves this far (screen units).
constexpr float HudDragStartPx=8.f;
// The band is centred on the screen as a whole; its left block (portrait, name, stats) is kept narrow so it stays compact.
constexpr float HudSideWidth=264.f;
constexpr float HudBandPadX=10.f;
constexpr float HudBlockGap=12.f;
// Mock 1 corners at the 1080p reference: the target card (250 wide with its panel padding), the top-centre panel (532 wide),
// the minimap (248 square in an 8-unit panel) and the 构筑 button (244 by 52).
constexpr float HudTargetInner=226.f;
constexpr float HudTargetPortrait=32.f;
constexpr float HudTargetHealthHeight=16.f;
constexpr float HudTargetMomentumHeight=8.f;
constexpr float HudTargetActiveSide=34.f;
// Mock 5's result page: the whole width, the gap between its cards and the winner's rule board inside its card.
constexpr float HudResultWidth=1640.f;
constexpr float HudResultGap=20.f;
constexpr float HudResultBoardWidth=600.f;
constexpr float HudTopInner=508.f;
constexpr float HudMapSize=248.f;
constexpr float HudPromptWidth=244.f;
constexpr float HudPromptHeight=52.f;
constexpr float HudFeedWidth=420.f;
// Square slots: the icon fills the face inside a 3u state ring; the key and the cost ride on its corners.
constexpr float HudSlotWidth=96.f;
constexpr float HudSlotHeight=96.f;
constexpr float HudBodySlotWidth=76.f;
constexpr float HudSlotGap=8.f;
constexpr float HudGroupGap=24.f;
constexpr float HudCentreWidth=HudBodySlotWidth+HudSlotGap+4*HudSlotWidth+2*HudSlotGap+HudGroupGap;
constexpr float HudDetailWidth=300.f;
// The 构筑 button's distance from the screen's bottom-right corner.
constexpr float HudCornerInset=16.f;
// The PK side panel wraps at this width: the longest fixed line (the PK time limit) stays on one line at 100% text.
constexpr float HudPKWrap=440.f;
const FLinearColor HudReadyEdge(FColor(0x7F,0xE0,0xD0));
const FLinearColor HudMomentumText(FColor(0xFF,0xB0,0x6A));
const FLinearColor HudControlEdge(FColor(0xFF,0x6A,0x3D));
const FLinearColor HudPromptFill(FColor(0xFF,0xC8,0x3D));
const FLinearColor HudPromptHover(FColor(0xFF,0xD8,0x66));
const FLinearColor HudPromptPress(FColor(0xE6,0xAE,0x2A));
const FLinearColor HudPromptInk(FColor(0x1A,0x13,0x00));
const FLinearColor HudPromptIdle(.16f,.17f,.19f);
const FLinearColor HudPromptIdleInk(.52f,.55f,.58f);
// The blind-box button's shape, size and colour scheme: lit is the gold fill, greyed out is the idle grey (disabled).
constexpr float HudCornerRadius=6.f;
FButtonStyle HudCornerStyle(const FButtonStyle& Shared)
{
    FButtonStyle Style=Shared;
    Style.Normal=FSlateRoundedBoxBrush(HudPromptFill,HudCornerRadius);Style.Hovered=FSlateRoundedBoxBrush(HudPromptHover,HudCornerRadius);
    Style.Pressed=FSlateRoundedBoxBrush(HudPromptPress,HudCornerRadius);
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
    case LDHUD::EActionLook::MomentumShort: return {LDHUDStyle::SlotMomentumShort,FLinearColor(.18f,.28f,.62f),2.f};
    case LDHUD::EActionLook::Controlled: return {LDHUDStyle::SlotControlled,HudControlEdge,3.f};
    default: return {LDHUDStyle::SlotBlocked,FLinearColor(.10f,.13f,.16f),1.f};
    }
}
// Icon square colours: actives orange, the innate dash slate and an empty slot dark grey; the square shows round the
// picture's corners and carries the first character when a texture is missing.
enum class EHudIconKind : uint8 { Active, Dash, Empty };
FLinearColor HudIconBase(EHudIconKind Kind)
{
    switch(Kind)
    {
    case EHudIconKind::Active: return FLinearColor(.62f,.22f,.04f);
    case EHudIconKind::Dash: return FLinearColor(.20f,.26f,.32f);
    default: return FLinearColor(.12f,.13f,.15f);
    }
}
// The icon also carries the state: missing momentum turns it ember-dark, control and blocks drain its colour; the cooldown mask
// darkens a cooling icon on its own.
FLinearColor HudIconColor(EHudIconKind Kind,LDHUD::EActionLook Look)
{
    const FLinearColor Base=HudIconBase(Kind);
    switch(Look)
    {
    case LDHUD::EActionLook::MomentumShort: return FMath::Lerp(Base,FLinearColor(.16f,.07f,.02f),.7f);
    case LDHUD::EActionLook::Controlled: return Base.Desaturate(.6f)*.6f;
    case LDHUD::EActionLook::Blocked: return Base.Desaturate(1.f)*.45f;
    default: return Base;
    }
}
// The same state on the icon picture: missing momentum dims it to embers, control and blocks grey it out.
enum class EHudIconTint : uint8 { Normal, MomentumShort, Controlled, Blocked };
EHudIconTint HudIconTintOf(LDHUD::EActionLook Look)
{
    switch(Look)
    {
    case LDHUD::EActionLook::MomentumShort: return EHudIconTint::MomentumShort;
    case LDHUD::EActionLook::Controlled: return EHudIconTint::Controlled;
    case LDHUD::EActionLook::Blocked: case LDHUD::EActionLook::Empty: return EHudIconTint::Blocked;
    default: return EHudIconTint::Normal;
    }
}
FLinearColor HudIconTint(EHudIconTint Tint)
{
    switch(Tint)
    {
    case EHudIconTint::MomentumShort: return FLinearColor(.55f,.38f,.28f);
    case EHudIconTint::Controlled: return FLinearColor(.55f,.55f,.55f);
    case EHudIconTint::Blocked: return FLinearColor(.4f,.4f,.4f);
    default: return FLinearColor::White;
    }
}
// Camp tiers are named after their approved creature, never a guessed or hidden attacker.
void HudCampNames(const FLDDemoContentDefinitions* Content,FString& Low,FString& High,FString& Elite,FString& Boss)
{
    Low=LDTerms::CampLow;High=LDTerms::CampHigh;Elite=LDTerms::CampElite;Boss=LDTerms::Boss;
    if(Content){Boss=LDHUD::ContentName(Content,Content->Boss.Id);}
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
        DiagnosticsText=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeSmall,TEXT("NetworkDiagnosticsText"));
        DiagnosticsText->SetAutoWrapText(false);
        DiagnosticsText->SetJustification(ETextJustify::Right);
        DiagnosticsPanel=Panel(DiagnosticsText,false);
        DiagnosticsPanel->SetVisibility(ESlateVisibility::Collapsed);
        auto Bar=[&](FLinearColor Fill,UProgressBar*& OutBar,UTextBlock*& OutText,float Width,float Height,TObjectPtr<UTextBlock>* OutRegen=nullptr)
        {
            auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(Width);Box->SetHeightOverride(Height);
            auto* Stack=WidgetTree->ConstructWidget<UOverlay>();Box->SetContent(Stack);
            OutBar=WidgetTree->ConstructWidget<UProgressBar>();HudBarStyle(OutBar,Fill);OutBar->SetPercent(0);
            auto* BarSlot=Stack->AddChildToOverlay(OutBar);BarSlot->SetHorizontalAlignment(HAlign_Fill);BarSlot->SetVerticalAlignment(VAlign_Fill);
            // Values cross bright fills and the dark track alike: bold white with a dark outline reads on both.
            OutText=OneLine(TEXT(""),LDHUDStyle::TypeBody,0);OutText->SetJustification(ETextJustify::Center);HudFace(OutText,true,2);
            auto* TextSlot=Stack->AddChildToOverlay(OutText);TextSlot->SetHorizontalAlignment(HAlign_Center);TextSlot->SetVerticalAlignment(VAlign_Center);
            if(OutRegen)
            {
                auto* Regen=OneLine(TEXT(""),LDHUDStyle::TypeSmall,0);HudFace(Regen,true,2);
                auto* RegenSlot=Stack->AddChildToOverlay(Regen);RegenSlot->SetHorizontalAlignment(HAlign_Right);
                RegenSlot->SetVerticalAlignment(VAlign_Center);RegenSlot->SetPadding(FMargin(0,0,10,0));
                *OutRegen=Regen;
                // Reserve equal space on both sides so the current/max value stays centred and cannot overlap regen.
                TextSlot->SetHorizontalAlignment(HAlign_Fill);TextSlot->SetPadding(FMargin(80,0));
            }
            return Box;
        };
        auto Portraits=[&](float Size,int32 GlyphSize,UBorder*& OutFrame,UTextBlock*& OutGlyph,FName Name=NAME_None)
        {
            // The glyph sits on any of the six identity tints, bright ones included; an outline keeps it readable on each.
            OutGlyph=OneLine(TEXT(""),GlyphSize,0);OutGlyph->SetJustification(ETextJustify::Center);HudFace(OutGlyph,true,2);
            OutFrame=WidgetTree->ConstructWidget<UBorder>();OutFrame->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,8.f,LDHUDStyle::Rim,1.f));
            OutFrame->SetHorizontalAlignment(HAlign_Center);OutFrame->SetVerticalAlignment(VAlign_Center);OutFrame->SetContent(OutGlyph);
            auto* Box=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),Name);Box->SetWidthOverride(Size);Box->SetHeightOverride(Size);Box->SetContent(OutFrame);
            return Box;
        };

        // Top-left: the current authorized target only. It disappears with the observation and never greys into history.
        auto* TargetBox=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* TargetHead=WidgetTree->ConstructWidget<UHorizontalBox>();TargetBox->AddChildToVerticalBox(TargetHead);
        UBorder* TP=nullptr;UTextBlock* TG=nullptr;TargetHead->AddChildToHorizontalBox(Portraits(HudTargetPortrait,LDHUDStyle::TypeBody,TP,TG))->SetPadding(FMargin(0,0,8,0));TargetPortrait=TP;TargetGlyph=TG;
        TargetTitle=OneLine(TEXT(""),LDHUDStyle::TypeBody,0);HudFace(TargetTitle,true,0);TargetTitle->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        auto* TitleSlot=TargetHead->AddChildToHorizontalBox(TargetTitle);TitleSlot->SetVerticalAlignment(VAlign_Center);TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        UProgressBar* B=nullptr;UTextBlock* T=nullptr;
        // Mock 1: a 16-unit health bar over a thin momentum bar; their values ride inside at caption size.
        TargetBox->AddChildToVerticalBox(Bar(LDHUDStyle::EnemyHealth,B,T,HudTargetInner,HudTargetHealthHeight))->SetPadding(FMargin(0,6,0,2));TargetHealthBar=B;TargetHealth=T;
        TargetMomentumRow=Bar(LDHUDStyle::Momentum,B,T,HudTargetInner,HudTargetMomentumHeight);TargetBox->AddChildToVerticalBox(TargetMomentumRow)->SetPadding(FMargin(0,4,0,2));TargetMomentumBar=B;TargetMomentum=T;
        for(UTextBlock* Value:{TargetHealth.Get(),TargetMomentum.Get()}){FSlateFontInfo Font=Value->GetFont();Font.Size=LDHUDStyle::TypeTiny;Value->SetFont(Font);}
        // The 8-unit momentum bar is too thin for its value; the value is the bar's tooltip instead.
        TargetMomentum->SetVisibility(ESlateVisibility::Collapsed);TargetMomentumRow->SetVisibility(ESlateVisibility::Visible);
        TargetStatuses=OneLine(TEXT(""),LDHUDStyle::TypeSmall,HudTargetInner);TargetBox->AddChildToVerticalBox(TargetStatuses)->SetPadding(FMargin(0,4,0,0));
        {
            // Mock 1: three 34-unit Q/W/E icon tiles of a visible contender, 8 apart; no cooldown is ever drawn. The key and
            // name are the tile's tooltip; a missing icon shows the name's first character.
            auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Row->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            for(int32 Index=0;Index<3;++Index)
            {
                UTextBlock* Name=OneLine(TEXT(""),LDHUDStyle::TypeBody,0);HudFace(Name,true,1);Name->SetJustification(ETextJustify::Center);
                auto* Tile=WidgetTree->ConstructWidget<UBorder>();Tile->SetBrush(HudRound(6.f,LDHUDStyle::Rim,1.f));Tile->SetBrushColor(FLinearColor(.02f,.03f,.05f,.9f));
                Tile->SetPadding(FMargin(0));Tile->SetHorizontalAlignment(HAlign_Center);Tile->SetVerticalAlignment(VAlign_Center);Tile->SetContent(Name);
                UImage* Icon=WidgetTree->ConstructWidget<UImage>();Icon->SetVisibility(ESlateVisibility::Collapsed);
                auto* Face=WidgetTree->ConstructWidget<UOverlay>();
                for(UWidget* Layer:{static_cast<UWidget*>(Tile),static_cast<UWidget*>(Icon)})
                {auto* LayerSlot=Face->AddChildToOverlay(Layer);LayerSlot->SetHorizontalAlignment(HAlign_Fill);LayerSlot->SetVerticalAlignment(VAlign_Fill);}
                auto* Side=WidgetTree->ConstructWidget<USizeBox>();Side->SetWidthOverride(HudTargetActiveSide);Side->SetHeightOverride(HudTargetActiveSide);Side->SetContent(Face);
                Side->SetVisibility(ESlateVisibility::Visible);
                Row->AddChildToHorizontalBox(Side)->SetPadding(FMargin(Index?8.f:0.f,0,0,0));
                TargetActiveTiles.Add(Tile);TargetActiveNames.Add(Name);TargetActiveIcons.Add(Icon);
            }
            TargetActivesRow=Row;TargetBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,10,0,0));
        }
        auto* TargetWidth=WidgetTree->ConstructWidget<USizeBox>();TargetWidth->SetWidthOverride(HudTargetInner);TargetWidth->SetContent(TargetBox);
        TargetCard=Panel(TargetWidth,true);TargetCard->SetVisibility(ESlateVisibility::Collapsed);
        // It keeps its hover details but is no click area: a world click or an armed skill's confirmation passes through it.
        HitPanels.Remove(TargetCard);

        // Top-left: the target card alone; the event feed sits in the top-right corner.
        Place(TargetCard,FAnchors(0,0),FVector2D(0,0),FVector2D(HudCornerInset,20));

        // Top-centre: the public capture state, then the own death banner. None takes pointer input.
        auto* CircleBox=WidgetTree->ConstructWidget<UVerticalBox>();
        // A DEV test room is marked on every member's screen for the whole match (#78).
        DevMarkerText=OneLine(TEXT(""),LDHUDStyle::TypeTiny,0);DevMarkerText->SetJustification(ETextJustify::Right);DevMarkerText->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        DevMarkerText->SetVisibility(ESlateVisibility::Collapsed);
        // #198: the timeline (rest step or field segment) above the capture line, with its detail line.
        TimelineText=OneLine(TEXT(""),LDHUDStyle::TypeLead,HudTopInner);TimelineText->SetJustification(ETextJustify::Center);HudFace(TimelineText,true,0);
        TimelineText->SetVisibility(ESlateVisibility::Collapsed);
        CircleBox->AddChildToVerticalBox(TimelineText)->SetHorizontalAlignment(HAlign_Center);
        TimelineDetailText=OneLine(TEXT(""),LDHUDStyle::TypeBody,HudTopInner);TimelineDetailText->SetJustification(ETextJustify::Center);TimelineDetailText->SetColorAndOpacity(LDHUDStyle::TextAccent);
        TimelineDetailText->SetVisibility(ESlateVisibility::Collapsed);
        CircleBox->AddChildToVerticalBox(TimelineDetailText)->SetHorizontalAlignment(HAlign_Center);
        PhaseText=OneLine(TEXT(""),LDHUDStyle::TypeLead,HudTopInner);PhaseText->SetJustification(ETextJustify::Left);HudFace(PhaseText,true,0);
        CircleBox->AddChildToVerticalBox(PhaseText)->SetHorizontalAlignment(HAlign_Fill);
        CircleProgress=WidgetTree->ConstructWidget<UProgressBar>();HudBarStyle(CircleProgress,FLinearColor(1,.78f,.25f));CircleProgress->SetPercent(0);
        auto* CircleBarSize=WidgetTree->ConstructWidget<USizeBox>();CircleBarSize->SetWidthOverride(HudTopInner);CircleBarSize->SetHeightOverride(8);CircleBarSize->SetContent(CircleProgress);
        CircleBox->AddChildToVerticalBox(CircleBarSize)->SetPadding(FMargin(0,6,0,0));
        {
            // Mock 1: the graffiti owner on the left under the bar and the guard state on the right, in caption grey.
            auto* Foot=WidgetTree->ConstructWidget<UHorizontalBox>();
            PhaseOwnerText=OneLine(TEXT(""),LDHUDStyle::TypeSmall,0);PhaseOwnerText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            PhaseAsideText=OneLine(TEXT(""),LDHUDStyle::TypeSmall,0);PhaseAsideText->SetJustification(ETextJustify::Right);PhaseAsideText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            for(UTextBlock* Side:{PhaseOwnerText.Get(),PhaseAsideText.Get()}){Side->SetColorAndOpacity(LDHUDStyle::TextSecondary);}
            Foot->AddChildToHorizontalBox(PhaseOwnerText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            auto* AsideSlot=Foot->AddChildToHorizontalBox(PhaseAsideText);AsideSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));AsideSlot->SetPadding(FMargin(12,0,0,0));
            Foot->SetVisibility(ESlateVisibility::Collapsed);PhaseFootRow=Foot;
            CircleBox->AddChildToVerticalBox(Foot)->SetPadding(FMargin(0,8,0,0));
        }
        // Mock 1: one fixed-width panel at the top centre, the gold progress bar running its full width.
        auto* CircleWidth=WidgetTree->ConstructWidget<USizeBox>();CircleWidth->SetWidthOverride(HudTopInner);CircleWidth->SetContent(CircleBox);
        PhaseBanner=Panel(CircleWidth,false);
        {
            // The rest actions sit in their own hit panel right under the click-through banner, shown only in a rest (the
            // training reset and the ready button); the stack grows downward from the top
            // edge, so a taller banner never covers it.
            auto* RestLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            TrainingResetButton=LDWidgets::Button(WidgetTree,LDTerms::TrainingReset,[this](){SubmitTrainingReset();},true,TEXT("HUDTrainingReset"));
            RestLine->AddChildToHorizontalBox(TrainingResetButton)->SetVerticalAlignment(VAlign_Center);
            RestReadyButton=LDWidgets::Button(WidgetTree,TEXT("准备完毕"),[this](){SubmitRestReady();},true,TEXT("HUDRestReady"));
            // .btn / .btn.pri: the rest's main action is the yellow one.
            LDWidgets::SetLook(RestReadyButton,LDUIPaint::EButton::Primary);
            for(ULDCommandButton* Action:{TrainingResetButton.Get(),RestReadyButton.Get()})
            {
                if(UTextBlock* Caption=Cast<UTextBlock>(Action->GetContent()))
                {FSlateFontInfo Font=LDUIFonts::Font(LDUIFonts::ERole::Strong,19);Font.LetterSpacing=80;Caption->SetFont(Font);}
                auto Style=Action->GetStyle();Style.NormalPadding=FMargin(26,13);Style.PressedPadding=FMargin(26,13);Action->SetStyle(Style);
            }
            RestLine->AddChildToHorizontalBox(RestReadyButton)->SetPadding(FMargin(20,0,0,0));
            RestAnswerText=OneLine(TEXT(""),LDHUDStyle::TypeSmall,420);RestAnswerText->SetColorAndOpacity(LDHUDStyle::TextSecondary);RestAnswerText->SetVisibility(ESlateVisibility::Collapsed);
            auto* ResultSlot=RestLine->AddChildToHorizontalBox(RestAnswerText);ResultSlot->SetVerticalAlignment(VAlign_Center);ResultSlot->SetPadding(FMargin(10,0,0,0));
            RestActionsPanel=Panel(RestLine,true);RestActionsPanel->SetVisibility(ESlateVisibility::Collapsed);
            {
                // PK actions and public sides stay off the arena centre. No scroll view: when every side, bet and award row
                // shows at a large text scale the panel scales down to its height cap.
                auto* PKBox=WidgetTree->ConstructWidget<UVerticalBox>();
                auto Centred=[PKBox](UWidget* Child,float Top){auto* S=PKBox->AddChildToVerticalBox(Child);S->SetHorizontalAlignment(HAlign_Center);S->SetPadding(FMargin(0,Top,0,0));};
                PKTitleText=OneLine(TEXT(""),LDHUDStyle::TypeLabel,0);PKTitleText->SetJustification(ETextJustify::Center);HudFace(PKTitleText,true,0);Centred(PKTitleText,0);
                PKCountdownText=OneLine(TEXT(""),LDHUDStyle::TypeLead,HudPKWrap);PKCountdownText->SetJustification(ETextJustify::Center);HudFace(PKCountdownText,true,2);
                PKCountdownText->SetColorAndOpacity(LDHUDStyle::TextWarning);Centred(PKCountdownText,4);
                PKRingText=OneLine(TEXT(""),LDHUDStyle::TypeSmall,HudPKWrap);PKRingText->SetJustification(ETextJustify::Center);PKRingText->SetColorAndOpacity(FSlateColor(ALDPKRingMarker::SafeColor()));Centred(PKRingText,2);
                for(int32 Index=0;Index<6;++Index)
                {
                    UTextBlock* SideText=OneLine(TEXT(""),LDHUDStyle::TypeBody,HudPKWrap);SideText->SetJustification(ETextJustify::Center);SideText->SetVisibility(ESlateVisibility::Collapsed);
                    Centred(SideText,Index?2.f:6.f);PKSideTexts.Add(SideText);
                }
                PKRoleText=OneLine(TEXT(""),LDHUDStyle::TypeSmall,HudPKWrap);PKRoleText->SetJustification(ETextJustify::Center);PKRoleText->SetColorAndOpacity(LDHUDStyle::TextAccent);Centred(PKRoleText,6);
                for(int32 Index=0;Index<6;++Index)
                {
                    ULDCommandButton* Bet=LDWidgets::Button(WidgetTree,TEXT(""),[this,Index](){SubmitPKBet(Index);},true,FName(*FString::Printf(TEXT("HUDPKBet%d"),Index+1)));
                    Bet->SetVisibility(ESlateVisibility::Collapsed);Centred(Bet,4);PKBetButtons.Add(Bet);
                }
                PKThrowButton=LDWidgets::Button(WidgetTree,LDTerms::PKThrow,[this](){TogglePKThrowAim();},true,TEXT("HUDPKThrow"));
                PKThrowButton->SetVisibility(ESlateVisibility::Collapsed);Centred(PKThrowButton,6);
                PKThrowButton->SetToolTipText(FText::FromString(LDTerms::PKThrowEffect));
                PKResultLine=OneLine(TEXT(""),LDHUDStyle::TypeHeading,HudPKWrap);PKResultLine->SetJustification(ETextJustify::Center);HudFace(PKResultLine,true,0);
                PKResultLine->SetColorAndOpacity(LDHUDStyle::Celebration);Centred(PKResultLine,6);
                PKAwardsTitle=OneLine(LDTerms::PKAwardsTitle,LDHUDStyle::TypeSmall,0);PKAwardsTitle->SetColorAndOpacity(LDHUDStyle::TextSecondary);Centred(PKAwardsTitle,6);
                for(int32 Index=0;Index<6;++Index)
                {
                    UTextBlock* AwardText=OneLine(TEXT(""),LDHUDStyle::TypeSmall,HudPKWrap);AwardText->SetJustification(ETextJustify::Center);AwardText->SetVisibility(ESlateVisibility::Collapsed);
                    Centred(AwardText,2);PKAwardTexts.Add(AwardText);
                }
                PKAnswerText=OneLine(TEXT(""),LDHUDStyle::TypeSmall,HudPKWrap);PKAnswerText->SetJustification(ETextJustify::Center);PKAnswerText->SetColorAndOpacity(LDHUDStyle::TextSecondary);Centred(PKAnswerText,6);
                for(UWidget* Hidden:{static_cast<UWidget*>(PKTitleText.Get()),static_cast<UWidget*>(PKCountdownText.Get()),static_cast<UWidget*>(PKRingText.Get()),
                    static_cast<UWidget*>(PKRoleText.Get()),static_cast<UWidget*>(PKResultLine.Get()),static_cast<UWidget*>(PKAwardsTitle.Get()),
                    static_cast<UWidget*>(PKAnswerText.Get())}){Hidden->SetVisibility(ESlateVisibility::Collapsed);}
                auto* PKFit=WidgetTree->ConstructWidget<UScaleBox>();PKFit->SetStretch(EStretch::ScaleToFit);PKFit->SetStretchDirection(EStretchDirection::DownOnly);
                PKFit->SetContent(PKBox);
                auto* PKWidth=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HUDPKSidePanel"));PKWidth->SetWidthOverride(HudPKWrap+24.f);PKWidth->SetMaxDesiredHeight(600);PKWidth->SetContent(PKFit);
                PKPanelBorder=Panel(PKWidth,true);PKPanelBorder->SetVisibility(ESlateVisibility::Collapsed);
            }
            auto* TopCentre=WidgetTree->ConstructWidget<UVerticalBox>();TopCentre->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            TopCentre->AddChildToVerticalBox(PhaseBanner)->SetHorizontalAlignment(HAlign_Center);
            auto* RestSlot=TopCentre->AddChildToVerticalBox(RestActionsPanel);RestSlot->SetHorizontalAlignment(HAlign_Center);RestSlot->SetPadding(FMargin(0,6,0,0));
            {
                // P6 spec §10.4: the death screen's spawn choice, one button per candidate and the seconds left.
                auto* ChoiceLine=WidgetTree->ConstructWidget<UHorizontalBox>();
                auto* ChoiceTitle=OneLine(LDTerms::SpawnChoice,LDHUDStyle::TypeLabel,0);HudFace(ChoiceTitle,true,0);
                ChoiceLine->AddChildToHorizontalBox(ChoiceTitle)->SetVerticalAlignment(VAlign_Center);
                for(int32 Index=0;Index<LDPacing::MaxSpawnChoices;++Index)
                {
                    ULDCommandButton* Choice=LDWidgets::Button(WidgetTree,LDTerms::SpawnCandidate(Index),[this,Index](){SubmitSpawnChoice(Index);},true,
                        FName(*FString::Printf(TEXT("HUDSpawnChoice%d"),Index+1)));
                    Choice->SetVisibility(ESlateVisibility::Collapsed);
                    ChoiceLine->AddChildToHorizontalBox(Choice)->SetPadding(FMargin(10,0,0,0));
                    SpawnChoiceButtons.Add(Choice);
                }
                SpawnChoiceCountdown=OneLine(TEXT(""),LDHUDStyle::TypeLead,0);HudFace(SpawnChoiceCountdown,true,2);SpawnChoiceCountdown->SetColorAndOpacity(LDHUDStyle::TextWarning);
                auto* CountSlot=ChoiceLine->AddChildToHorizontalBox(SpawnChoiceCountdown);CountSlot->SetVerticalAlignment(VAlign_Center);CountSlot->SetPadding(FMargin(12,0,0,0));
                SpawnChoicePanel=Panel(ChoiceLine,true);SpawnChoicePanel->SetVisibility(ESlateVisibility::Collapsed);
                auto* ChoiceSlot=TopCentre->AddChildToVerticalBox(SpawnChoicePanel);ChoiceSlot->SetHorizontalAlignment(HAlign_Center);ChoiceSlot->SetPadding(FMargin(0,6,0,0));
            }
            Place(TopCentre,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,22));
            // Above the workshop (its rest actions stay usable in the full view), below the tray and the storage.
            TopCentreSlot=Cast<UCanvasPanelSlot>(TopCentre->Slot);if(TopCentreSlot){TopCentreSlot->SetZOrder(9);}
        }
        auto* DeathBox=WidgetTree->ConstructWidget<UVerticalBox>();
        DeathTitle=OneLine(TEXT(""),LDHUDStyle::TypeDisplay,0);HudFace(DeathTitle,true,0);DeathTitle->SetColorAndOpacity(LDHUDStyle::TextDanger);
        DeathCauseText=OneLine(TEXT(""),LDHUDStyle::TypeLead,520);DeathCountdown=OneLine(TEXT(""),LDHUDStyle::TypeSection,0);
        DeathNote=OneLine(TEXT(""),LDHUDStyle::TypeSmall,520);DeathNote->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        for(UTextBlock* DeathLine:{DeathTitle.Get(),DeathCauseText.Get(),DeathCountdown.Get(),DeathNote.Get()})
        {DeathLine->SetJustification(ETextJustify::Center);DeathBox->AddChildToVerticalBox(DeathLine)->SetHorizontalAlignment(HAlign_Center);}
        // The recap lines shrink together to the banner width instead of wrapping or scrolling.
        {
            auto* RecapBox=WidgetTree->ConstructWidget<UVerticalBox>();
            DeathRecapHeading=OneLine(TEXT(""),15,0);DeathRecapHeading->SetJustification(ETextJustify::Center);
            RecapBox->AddChildToVerticalBox(DeathRecapHeading)->SetHorizontalAlignment(HAlign_Center);
            DeathRecapRows.Reset();DeathRecapShown.Reset();
            for(int32 Row=0;Row<LDHUD::DeathRecapAttackersMax;++Row)
            {
                auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();Line->SetVisibility(ESlateVisibility::Collapsed);
                UVerticalBoxSlot* LineSlot=RecapBox->AddChildToVerticalBox(Line);LineSlot->SetHorizontalAlignment(HAlign_Center);LineSlot->SetPadding(FMargin(0,2));
                DeathRecapRows.Add(Line);DeathRecapShown.AddDefaulted();
            }
            auto* RecapFit=WidgetTree->ConstructWidget<UScaleBox>();RecapFit->SetStretch(EStretch::ScaleToFit);RecapFit->SetStretchDirection(EStretchDirection::DownOnly);
            RecapFit->SetContent(RecapBox);
            auto* RecapWidth=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HUDDeathRecap"));RecapWidth->SetMaxDesiredWidth(640);RecapWidth->SetContent(RecapFit);
            auto* RecapSlot=DeathBox->AddChildToVerticalBox(RecapWidth);RecapSlot->SetHorizontalAlignment(HAlign_Center);RecapSlot->SetPadding(FMargin(0,8,0,0));
        }
        DeathBanner=Panel(DeathBox,false);Place(DeathBanner,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,200));DeathBanner->SetVisibility(ESlateVisibility::Collapsed);
        // #198: the rest return countdown, large in the screen centre; it never takes pointer input.
        ReturnCountdownText=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeGiant,TEXT("ReturnCountdownText"));ReturnCountdownText->SetAutoWrapText(false);
        ReturnCountdownText->SetJustification(ETextJustify::Center);HudFace(ReturnCountdownText,true,4);
        ReturnCountdownText->SetColorAndOpacity(LDHUDStyle::TextWarning);
        Place(ReturnCountdownText,FAnchors(.5f,.5f),FVector2D(.5f,.5f),FVector2D(0,-120));ReturnCountdownText->SetVisibility(ESlateVisibility::Collapsed);
        // The return countdown shows over the workshop.
        if(auto* CountdownSlot=Cast<UCanvasPanelSlot>(ReturnCountdownText->Slot)){CountdownSlot->SetZOrder(11);}

        // Centre: the one frozen result; it replaces the gameplay HUD instead of overlaying it.
        auto* ResultBox=WidgetTree->ConstructWidget<UVerticalBox>();
        ResultTitle=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeBanner,NAME_None,LDUIFonts::ERole::Title);
        // The verdict is written like the logo: action yellow over an event-pink offset, sprayed on a sheared pink band.
        ResultTitle->SetColorAndOpacity(LDHUDStyle::UIAction);ResultTitle->SetShadowOffset(FVector2D(4.f,4.f));ResultTitle->SetShadowColorAndOpacity(LDHUDStyle::UIEvent);ResultTitle->SetJustification(ETextJustify::Left);
        {
            auto* Verdict=WidgetTree->ConstructWidget<UBorder>();Verdict->SetBrush(HudRound(6.f,LDHUDStyle::UIEvent.CopyWithNewOpacity(.55f),2.f));
            Verdict->SetBrushColor(LDHUDStyle::UIEvent.CopyWithNewOpacity(.16f));Verdict->SetPadding(FMargin(36,8));Verdict->SetContent(ResultTitle);
            Verdict->SetVisibility(ESlateVisibility::HitTestInvisible);ResultWinnerBand=Verdict;
            ResultBox->AddChildToVerticalBox(Verdict)->SetHorizontalAlignment(HAlign_Left);
        }
        ResultSubtitle=Text(TEXT(""),LDHUDStyle::TypeText);ResultSubtitle->SetJustification(ETextJustify::Left);ResultSubtitle->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        ResultBox->AddChildToVerticalBox(ResultSubtitle)->SetPadding(FMargin(0,6,0,18));
        // Mock 5: the contender table on the left, the winner's rule board in its own card on the right.
        {
            auto* Body=WidgetTree->ConstructWidget<UHorizontalBox>();ResultBox->AddChildToVerticalBox(Body);
            ResultRows=WidgetTree->ConstructWidget<UVerticalBox>();
            auto* RowsSlot=Body->AddChildToHorizontalBox(ResultRows);RowsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));RowsSlot->SetPadding(FMargin(0,0,HudResultGap,0));
            auto* BoardBox=WidgetTree->ConstructWidget<UVerticalBox>();
            UTextBlock* BoardTitle=OneLine(TEXT("赢家规则板"),LDHUDStyle::TypeHeading,0);HudFace(BoardTitle,true,0);
            BoardBox->AddChildToVerticalBox(BoardTitle)->SetPadding(FMargin(0,0,0,14));
            ResultWinnerBuild=WidgetTree->ConstructWidget<USizeBox>();BoardBox->AddChildToVerticalBox(ResultWinnerBuild);
            ResultWinnerBuild->SetVisibility(ESlateVisibility::Collapsed);
            auto* Board=WidgetTree->ConstructWidget<UBorder>();Board->SetBrush(HudRound(6.f,LDHUDStyle::PanelRim,1.f));
            Board->SetBrushColor(LDHUDStyle::UIPanel.CopyWithNewOpacity(.82f));Board->SetPadding(FMargin(24,20));Board->SetContent(BoardBox);
            auto* BoardWidth=WidgetTree->ConstructWidget<USizeBox>();BoardWidth->SetWidthOverride(HudResultBoardWidth+48.f);BoardWidth->SetContent(Board);
            Body->AddChildToHorizontalBox(BoardWidth)->SetVerticalAlignment(VAlign_Fill);
        }
        // The own damage per source in one card: a heading, then DamageSummarySourcesMax cells on one line.
        ResultDamageCells.Reset();
        {
            auto* DamageBox=WidgetTree->ConstructWidget<UVerticalBox>();
            ResultDamageHeading=Text(TEXT(""),LDHUDStyle::TypeHeading);HudFace(ResultDamageHeading,true,0);
            DamageBox->AddChildToVerticalBox(ResultDamageHeading)->SetPadding(FMargin(0,0,0,10));
            auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();DamageBox->AddChildToVerticalBox(Line);
            for(int32 Index=0;Index<LDHUD::DamageSummarySourcesMax;++Index)
            {
                UTextBlock* Cell=OneLine(TEXT(""),15,0);Cell->SetColorAndOpacity(LDHUDStyle::UIText);
                auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);
                Fit->SetContent(Cell);
                if(auto* FitSlot=Cast<UScaleBoxSlot>(Fit->GetContentSlot())){FitSlot->SetHorizontalAlignment(HAlign_Left);}
                auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride((HudResultWidth-48.f)/LDHUD::DamageSummarySourcesMax-12.f);Box->SetContent(Fit);
                Line->AddChildToHorizontalBox(Box)->SetPadding(FMargin(0,0,12,0));ResultDamageCells.Add(Cell);
            }
            auto* DamageCard=WidgetTree->ConstructWidget<UBorder>();DamageCard->SetBrush(HudRound(6.f,LDHUDStyle::PanelRim,1.f));
            DamageCard->SetBrushColor(LDHUDStyle::UIPanel.CopyWithNewOpacity(.82f));DamageCard->SetPadding(FMargin(24,18));DamageCard->SetContent(DamageBox);
            ResultBox->AddChildToVerticalBox(DamageCard)->SetPadding(FMargin(0,HudResultGap,0,0));
        }
        // Spec §12 结算“本局最离谱”: one line per contender (at most six), shrunk to fit, never scrolled.
        ResultHighlightCells.Reset();
        {
            auto* HighlightBox=WidgetTree->ConstructWidget<UVerticalBox>();
            UTextBlock* HighlightHeading=Text(LDTerms::ResultHighlights,LDHUDStyle::TypeHeading);HudFace(HighlightHeading,true,0);
            HighlightBox->AddChildToVerticalBox(HighlightHeading)->SetPadding(FMargin(0,0,0,8));
            for(int32 Index=0;Index<LDHUD::ResultHighlightsMax;++Index)
            {
                UTextBlock* Cell=OneLine(TEXT(""),14,0);Cell->SetColorAndOpacity(LDHUDStyle::UIText);
                auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);
                Fit->SetContent(Cell);
                if(auto* FitSlot=Cast<UScaleBoxSlot>(Fit->GetContentSlot())){FitSlot->SetHorizontalAlignment(HAlign_Left);}
                auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(HudResultWidth-48.f);Box->SetContent(Fit);
                HighlightBox->AddChildToVerticalBox(Box)->SetPadding(FMargin(0,2,0,0));ResultHighlightCells.Add(Cell);
            }
            auto* HighlightCard=WidgetTree->ConstructWidget<UBorder>();HighlightCard->SetBrush(HudRound(6.f,LDHUDStyle::PanelRim,1.f));
            HighlightCard->SetBrushColor(LDHUDStyle::UIPanel.CopyWithNewOpacity(.82f));HighlightCard->SetPadding(FMargin(24,14));HighlightCard->SetContent(HighlightBox);
            HighlightCard->SetVisibility(ESlateVisibility::Collapsed);
            ResultBox->AddChildToVerticalBox(HighlightCard)->SetPadding(FMargin(0,HudResultGap,0,0));
            ResultHighlightsCard=HighlightCard;
        }
        // Actions sit bottom-right: the blueprint viewer, leave, and the host's return to Lobby as the one primary button.
        auto* ResultActions=WidgetTree->ConstructWidget<UHorizontalBox>();
        ResultBox->AddChildToVerticalBox(ResultActions)->SetPadding(FMargin(0,HudResultGap,0,0));
        if(auto* ActionsSlot=Cast<UVerticalBoxSlot>(ResultActions->Slot)){ActionsSlot->SetHorizontalAlignment(HAlign_Right);}
        ResultActionsBox=ResultActions;
        // #198 §10: the own build blueprint of this match, read-only.
        ResultActions->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,LDTerms::BuildBlueprint,[PC](){if(PC->BuildBlueprints){PC->BuildBlueprints->ToggleViewer();}},false,TEXT("ResultBuildBlueprint")))->SetPadding(FMargin(0,0,12,0));
        LeaveRoomButton=LDWidgets::Button(WidgetTree,TEXT("离开房间并返回主菜单"),[PC](){if(PC->RootUI){PC->RootUI->LeaveRoom();}},false,TEXT("ResultLeaveRoom"));
        ResultActions->AddChildToHorizontalBox(LeaveRoomButton)->SetPadding(FMargin(0,0,12,0));
        ReturnButton=LDWidgets::Button(WidgetTree,TEXT("带房间返回 Lobby"),[PC](){PC->ReturnDemoToLobby();},false,TEXT("ResultReturnToLobby"));
        LDWidgets::SetLook(ReturnButton,LDUIPaint::EButton::Primary);
        ResultActions->AddChildToHorizontalBox(ReturnButton);
        auto* ResultWidth=WidgetTree->ConstructWidget<USizeBox>();ResultWidth->SetWidthOverride(HudResultWidth);ResultWidth->SetContent(ResultBox);
        ResultPanel=Panel(ResultWidth,true);Place(ResultPanel,FAnchors(.5f,.5f),FVector2D(.5f,.5f),FVector2D(0,0));ResultPanel->SetVisibility(ESlateVisibility::Collapsed);

        // Centre: the hold-Tab scoreboard, read-only and click-through, over the running match.
        {
            auto* ScoreBox=WidgetTree->ConstructWidget<UVerticalBox>();
            UTextBlock* ScoreTitle=OneLine(LDTerms::Scoreboard,LDHUDStyle::TypeHeading,0);HudFace(ScoreTitle,true,0);
            ScoreBox->AddChildToVerticalBox(ScoreTitle)->SetPadding(FMargin(0,0,0,8));
            const float Widths[4]={240,96,96,72};
            for(int32 RowIdx=0;RowIdx<=LDCapture::MaxScoreRows;++RowIdx)
            {
                auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
                for(int32 Col=0;Col<4;++Col)
                {
                    UTextBlock* Cell=OneLine(TEXT(""),LDHUDStyle::TypeText,0);
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

        // Bottom-left corner: the minimap in an 8-unit panel with the match timer centred over the map frame and nothing else;
        // the map's own markers carry the public events (2026-11-12 user decision: no text lines above the minimap).
        auto* MapBox=WidgetTree->ConstructWidget<UVerticalBox>();
        // A square frame: the minimap fits either the field or the rest room (ULDMinimap), each with one uniform scale.
        Minimap=CreateWidget<ULDMinimap>(PC);MapFrame=WidgetTree->ConstructWidget<USizeBox>();MapFrame->SetWidthOverride(HudMapSize);MapFrame->SetHeightOverride(HudMapSize);MapFrame->SetContent(Minimap);
        // The match timer sits centred over the map frame itself, so a wide sight line above never shifts it.
        auto* MapStack=WidgetTree->ConstructWidget<UVerticalBox>();
        MatchTimerText=OneLine(TEXT(""),LDHUDStyle::TypeSection,0);HudFace(MatchTimerText,true,2);MatchTimerText->SetJustification(ETextJustify::Center);
        MatchTimerText->SetVisibility(ESlateVisibility::Collapsed);
        {
            // P6 spec §11.1: the day's modifiers as glyph badges beside the timer; hovering one shows its name and numbers.
            auto* TimerRow=WidgetTree->ConstructWidget<UHorizontalBox>();TimerRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            TimerRow->AddChildToHorizontalBox(MatchTimerText)->SetVerticalAlignment(VAlign_Center);
            for(int32 Index=0;Index<4;++Index)
            {
                UTextBlock* Glyph=OneLine(TEXT(""),LDHUDStyle::TypeSmall,0);HudFace(Glyph,true,0);Glyph->SetJustification(ETextJustify::Center);
                auto* Badge=WidgetTree->ConstructWidget<UBorder>();Badge->SetBrush(HudRound(4.f,LDHUDStyle::TextAccent,1.f));Badge->SetBrushColor(FLinearColor(.02f,.03f,.05f,.9f));
                Badge->SetPadding(FMargin(5,1));Badge->SetContent(Glyph);Badge->SetVisibility(ESlateVisibility::Collapsed);
                auto* BadgeAt=TimerRow->AddChildToHorizontalBox(Badge);BadgeAt->SetVerticalAlignment(VAlign_Center);BadgeAt->SetPadding(FMargin(6,0,0,0));
                DailyBadges.Add(Badge);DailyBadgeGlyphs.Add(Glyph);
            }
            MapStack->AddChildToVerticalBox(TimerRow)->SetHorizontalAlignment(HAlign_Center);
        }
        auto* FrameSlot=MapStack->AddChildToVerticalBox(MapFrame);FrameSlot->SetHorizontalAlignment(HAlign_Left);FrameSlot->SetPadding(FMargin(0,4,0,0));
        MapBox->AddChildToVerticalBox(MapStack)->SetHorizontalAlignment(HAlign_Left);
        MapColumn=Panel(MapBox,true);MapColumn->SetPadding(FMargin(8));
        Place(MapColumn,FAnchors(0,1),FVector2D(0,1),FVector2D(HudCornerInset,-HudCornerInset));
        // Top-right corner: the room's event feed (LDHUD::FEventFeed), one line a row with the newest at the bottom. It takes no
        // pointer input; the DEV marker and the network diagnostics stand under it.
        auto* FeedBox=WidgetTree->ConstructWidget<UVerticalBox>();
        for(int32 Row=0;Row<LDHUD::FEventFeed::MaxLines;++Row)
        {
            UTextBlock* FeedLine=OneLine(TEXT(""),LDHUDStyle::TypeSmall,0);FeedLine->SetVisibility(ESlateVisibility::Collapsed);HudFace(FeedLine,false,1);
            FeedLine->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);FeedLine->SetJustification(ETextJustify::Right);
            EventFeedRows.Add(FeedLine);FeedBox->AddChildToVerticalBox(FeedLine)->SetPadding(FMargin(0,1));
        }
        EventFeedPanel=Panel(FeedBox,false);EventFeedPanel->SetBrushColor(FLinearColor(.02f,.03f,.05f,.55f));EventFeedPanel->SetVisibility(ESlateVisibility::Collapsed);
        auto* FeedWidth=WidgetTree->ConstructWidget<USizeBox>();FeedWidth->SetMaxDesiredWidth(HudFeedWidth);FeedWidth->SetContent(EventFeedPanel);
        FeedWidth->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* TopRight=WidgetTree->ConstructWidget<UVerticalBox>();TopRight->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        TopRight->AddChildToVerticalBox(FeedWidth)->SetHorizontalAlignment(HAlign_Right);
        TopRight->AddChildToVerticalBox(DevMarkerText)->SetHorizontalAlignment(HAlign_Right);
        TopRight->AddChildToVerticalBox(DiagnosticsPanel)->SetHorizontalAlignment(HAlign_Right);
        // The PK side panel follows the corner stack, so a longer feed never covers it.
        TopRight->AddChildToVerticalBox(PKPanelBorder)->SetHorizontalAlignment(HAlign_Right);
        for(UWidget* Below:{static_cast<UWidget*>(DevMarkerText.Get()),static_cast<UWidget*>(DiagnosticsPanel.Get()),static_cast<UWidget*>(PKPanelBorder.Get())})
        {if(auto* BelowSlot=Cast<UVerticalBoxSlot>(Below->Slot)){BelowSlot->SetPadding(FMargin(0,6,0,0));}}
        Place(TopRight,FAnchors(1,0),FVector2D(1,0),FVector2D(-HudCornerInset,22));
        // Settings open from the Back menu (its 设置 entry); the HUD has no separate settings button.

        // Bottom band: own identity | body and 3+1 action entries over the resources, centred on the screen as a whole.
        auto* Bottom=WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* SelfBox=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* SelfHead=WidgetTree->ConstructWidget<UHorizontalBox>();SelfBox->AddChildToVerticalBox(SelfHead);
        UBorder* SP=nullptr;UTextBlock* SG=nullptr;
        auto* PortraitStack=WidgetTree->ConstructWidget<UOverlay>();PortraitStack->AddChildToOverlay(Portraits(52,LDHUDStyle::TypeHeading,SP,SG));Portrait=SP;PortraitGlyph=SG;
        // The level rides on the portrait corner; the experience bar over the actions carries the numbers as its tooltip.
        LevelBadgeText=OneLine(TEXT(""),LDHUDStyle::TypeCaption,0);HudFace(LevelBadgeText,true,0);LevelBadgeText->SetColorAndOpacity(LDHUDStyle::TextAccent);
        auto* LevelBadge=WidgetTree->ConstructWidget<UBorder>();LevelBadge->SetBrush(HudRound(4.f,LDHUDStyle::TextAccent,1.f));LevelBadge->SetBrushColor(FLinearColor(.02f,.03f,.05f,.96f));
        LevelBadge->SetPadding(FMargin(5,0));LevelBadge->SetContent(LevelBadgeText);LevelBadge->SetVisibility(ESlateVisibility::HitTestInvisible);LevelBadge->SetRenderTranslation(FVector2D(6,12));
        auto* BadgeSlot=PortraitStack->AddChildToOverlay(LevelBadge);BadgeSlot->SetHorizontalAlignment(HAlign_Right);BadgeSlot->SetVerticalAlignment(VAlign_Bottom);
        // Unspent points in a yellow circle on the opposite corner; the stat icons below spend them.
        PointsBadgeText=OneLine(TEXT(""),LDHUDStyle::TypeCaption,0);HudFace(PointsBadgeText,true,0);PointsBadgeText->SetColorAndOpacity(HudPromptInk);
        PointsBadgeText->SetJustification(ETextJustify::Center);
        PointsBadge=WidgetTree->ConstructWidget<UBorder>();PointsBadge->SetBrush(HudRound(11.f,LDHUDStyle::Rim,1.f));PointsBadge->SetBrushColor(HudPromptFill);
        PointsBadge->SetPadding(FMargin(6,1));PointsBadge->SetHorizontalAlignment(HAlign_Center);PointsBadge->SetContent(PointsBadgeText);
        PointsBadge->SetVisibility(ESlateVisibility::Collapsed);PointsBadge->SetRenderTranslation(FVector2D(-6,12));

        auto* PointsSlot=PortraitStack->AddChildToOverlay(PointsBadge);PointsSlot->SetHorizontalAlignment(HAlign_Left);PointsSlot->SetVerticalAlignment(VAlign_Bottom);
        // Padded in from the block's clipped left edge so the points badge hanging off the portrait's corner stays whole.
        auto* PortraitSlot=SelfHead->AddChildToHorizontalBox(PortraitStack);PortraitSlot->SetPadding(FMargin(8,0,10,0));PortraitSlot->SetVerticalAlignment(VAlign_Top);
        auto* NameBox=WidgetTree->ConstructWidget<UVerticalBox>();
        // A long roster name ends in an ellipsis inside the column; hovering it shows the whole name (VISION_AND_UI readability).
        SelfName=OneLine(TEXT(""),LDHUDStyle::TypeLabel,0);HudFace(SelfName,true,0);SelfName->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);SelfName->SetVisibility(ESlateVisibility::Visible);NameBox->AddChildToVerticalBox(SelfName);
        auto* NameSlot=SelfHead->AddChildToHorizontalBox(NameBox);NameSlot->SetVerticalAlignment(VAlign_Center);NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        // The 属性 entry turns the stat icons below into the attribute view and back (alive or dead, never a panel). The DEV
        // test entry shows only in a DEV test room (#78).
        auto* PanelColumn=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Sheet=LDWidgets::Button(WidgetTree,TEXT("属性"),[this](){ToggleAttributeView();},true,TEXT("HUDAttributes"));
        auto* Dev=LDWidgets::Button(WidgetTree,TEXT("测试"),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::DevTest);}},true,TEXT("HUDDevTest"));
        Dev->SetVisibility(ESlateVisibility::Collapsed);
        Commands.Add(TEXT("Attributes"),Sheet);Commands.Add(TEXT("DevTest"),Dev);
        PanelColumn->AddChildToVerticalBox(Sheet)->SetHorizontalAlignment(HAlign_Right);
        PanelColumn->AddChildToVerticalBox(Dev)->SetPadding(FMargin(0,4,0,0));
        auto* ColumnSlot=SelfHead->AddChildToHorizontalBox(PanelColumn);ColumnSlot->SetVerticalAlignment(VAlign_Center);ColumnSlot->SetPadding(FMargin(8,0,0,0));
        // The life line only when it says more than "alive"; it wraps inside the fixed column.
        LifeText=Text(TEXT(""),LDHUDStyle::TypeSmall);SelfBox->AddChildToVerticalBox(LifeText)->SetPadding(FMargin(0,8,0,0));
        // The own point-bought stats: one icon each, a "+" over the hovered one spends a point at once.
        StatBar=CreateWidget<ULDStatBar>(PC);
        if(StatBar){SelfBox->AddChildToVerticalBox(StatBar)->SetPadding(FMargin(0,4,0,0));}
        else{UE_LOG(LogLDUI,Error,TEXT("LD_HUD_STAT_BAR_CREATION_FAILED"));}
        auto* SelfWidth=WidgetTree->ConstructWidget<USizeBox>();SelfWidth->SetWidthOverride(HudSideWidth);SelfWidth->SetClipping(EWidgetClipping::ClipToBounds);SelfWidth->SetContent(SelfBox);
        Bottom->AddChildToHorizontalBox(SelfWidth)->SetVerticalAlignment(VAlign_Fill);
        {
            auto* Rule=WidgetTree->ConstructWidget<UBorder>();Rule->SetBrushColor(LDHUDStyle::PanelRim);Rule->SetVisibility(ESlateVisibility::HitTestInvisible);
            auto* RuleBox=WidgetTree->ConstructWidget<USizeBox>();RuleBox->SetWidthOverride(1);RuleBox->SetContent(Rule);
            auto* RuleSlot=Bottom->AddChildToHorizontalBox(RuleBox);RuleSlot->SetPadding(FMargin(HudBlockGap*.5f,4));RuleSlot->SetVerticalAlignment(VAlign_Fill);
        }
        auto* Centre=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();
        UBorder* BodyFrame=nullptr;UTextBlock* BodyGlyph=nullptr;
        BodySlot=Portraits(HudBodySlotWidth,LDHUDStyle::TypePage,BodyFrame,BodyGlyph,TEXT("HUDActionBody"));
        BodyPortrait=BodyFrame;BodyPortraitGlyph=BodyGlyph;
        BodySlot->SetVisibility(ESlateVisibility::HitTestInvisible);
        {
            // The basic attack's combo: three pips under the body slot.
            auto* BodyColumn=WidgetTree->ConstructWidget<UVerticalBox>();
            BodyColumn->AddChildToVerticalBox(BodySlot);
            auto* Pips=WidgetTree->ConstructWidget<UHorizontalBox>();Pips->SetVisibility(ESlateVisibility::HitTestInvisible);
            for(int32 Pip=0;Pip<3;++Pip)
            {
                auto* Face=WidgetTree->ConstructWidget<UBorder>();Face->SetBrush(HudRound(3.f,LDHUDStyle::Rim,1.f));Face->SetBrushColor(LDHUDStyle::ComboPipDim);
                auto* PipBox=WidgetTree->ConstructWidget<USizeBox>();PipBox->SetWidthOverride(14);PipBox->SetHeightOverride(6);PipBox->SetContent(Face);
                Pips->AddChildToHorizontalBox(PipBox)->SetPadding(FMargin(Pip==0?0:4,0,0,0));
                ComboPips.Add(Face);
            }
            BodyColumn->AddChildToVerticalBox(Pips)->SetHorizontalAlignment(HAlign_Center);
            Actions->AddChildToHorizontalBox(BodyColumn)->SetVerticalAlignment(VAlign_Top);
        }
        for(int32 Index=0;Index<HudActionCount;++Index)
        {
            FLDHUDActionSlot& Entry=ActionSlots.AddDefaulted_GetRef();
            // The face: the icon filling the state ring, the cooldown mask and its remaining time over it, and the key
            // (top-left) and the momentum cost (bottom-right) on its corners. Corner text carries a 2u outline because it sits on
            // the icon colours.
            auto* Face=WidgetTree->ConstructWidget<UOverlay>();Face->SetClipping(EWidgetClipping::ClipToBounds);
            auto FillLayer=[](UOverlaySlot* Layer){Layer->SetHorizontalAlignment(HAlign_Fill);Layer->SetVerticalAlignment(VAlign_Fill);};
            Entry.Glyph=OneLine(TEXT(""),LDHUDStyle::TypePage,0);HudFace(Entry.Glyph,true,2);Entry.Glyph->SetJustification(ETextJustify::Center);
            Entry.Icon=WidgetTree->ConstructWidget<UBorder>();Entry.Icon->SetBrush(HudRound(6.f,FLinearColor(1,1,1,.18f),1.f));
            Entry.Icon->SetHorizontalAlignment(HAlign_Fill);Entry.Icon->SetVerticalAlignment(VAlign_Fill);Entry.Icon->SetPadding(FMargin(0));
            {
                auto* IconFace=WidgetTree->ConstructWidget<UOverlay>();
                Entry.Picture=WidgetTree->ConstructWidget<UImage>();Entry.Picture->SetVisibility(ESlateVisibility::Collapsed);
                FillLayer(IconFace->AddChildToOverlay(Entry.Picture));
                auto* GlyphLayer=IconFace->AddChildToOverlay(Entry.Glyph);GlyphLayer->SetHorizontalAlignment(HAlign_Center);GlyphLayer->SetVerticalAlignment(VAlign_Center);
                Entry.Icon->SetContent(IconFace);
            }
            FillLayer(Face->AddChildToOverlay(Entry.Icon));
            Entry.Cooldown=WidgetTree->ConstructWidget<UProgressBar>();
            FProgressBarStyle MaskStyle;MaskStyle.SetBackgroundImage(FSlateNoResource()).SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,6.f)).SetMarqueeImage(FSlateNoResource());
            Entry.Cooldown->SetWidgetStyle(MaskStyle);Entry.Cooldown->SetBarFillType(EProgressBarFillType::TopToBottom);Entry.Cooldown->SetFillColorAndOpacity(LDHUDStyle::SlotCooldownMask);
            Entry.Cooldown->SetPercent(0);Entry.Cooldown->SetVisibility(ESlateVisibility::Collapsed);FillLayer(Face->AddChildToOverlay(Entry.Cooldown));
            Entry.Countdown=OneLine(TEXT(""),LDHUDStyle::TypeSubBanner,0);HudFace(Entry.Countdown,true,2);Entry.Countdown->SetJustification(ETextJustify::Center);Entry.Countdown->SetColorAndOpacity(FLinearColor::White);
            Entry.Countdown->SetVisibility(ESlateVisibility::Collapsed);
            auto* CountdownLayer=Face->AddChildToOverlay(Entry.Countdown);CountdownLayer->SetHorizontalAlignment(HAlign_Center);CountdownLayer->SetVerticalAlignment(VAlign_Center);
            Entry.Key=OneLine(TEXT(""),LDHUDStyle::TypeText,0);HudFace(Entry.Key,true,2);Entry.Key->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Entry.Key->SetColorAndOpacity(LDHUDStyle::TextAccent);
            auto* KeyLayer=Face->AddChildToOverlay(Entry.Key);KeyLayer->SetHorizontalAlignment(HAlign_Left);KeyLayer->SetVerticalAlignment(VAlign_Top);KeyLayer->SetPadding(FMargin(5,2,5,0));
            auto Chip=[&](UTextBlock*& OutText,const FLinearColor& Ink,const FLinearColor& Edge,const FLinearColor& Fill)
            {
                OutText=OneLine(TEXT(""),LDHUDStyle::TypeCaption,0);HudFace(OutText,true,0);OutText->SetColorAndOpacity(Ink);
                auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrush(HudRound(4.f,Edge,1.f));Border->SetBrushColor(Fill);
                Border->SetPadding(FMargin(5,0));Border->SetContent(OutText);Border->SetVisibility(ESlateVisibility::Collapsed);
                return Border;
            };
            UTextBlock* ChipText=nullptr;
            Entry.CostChip=Chip(ChipText,HudMomentumText,FLinearColor(.62f,.24f,.05f),FLinearColor(.09f,.03f,.01f,.92f));Entry.Cost=ChipText;
            auto* CostLayer=Face->AddChildToOverlay(Entry.CostChip);CostLayer->SetHorizontalAlignment(HAlign_Right);CostLayer->SetVerticalAlignment(VAlign_Bottom);CostLayer->SetPadding(FMargin(0,0,3,3));
            // The rank dots on the lower edge and the missing momentum over a short face.
            Entry.Ranks=OneLine(TEXT(""),LDHUDStyle::TypeCaption,0);HudFace(Entry.Ranks,true,2);Entry.Ranks->SetColorAndOpacity(LDHUDStyle::TextAccent);
            auto* RanksLayer=Face->AddChildToOverlay(Entry.Ranks);RanksLayer->SetHorizontalAlignment(HAlign_Left);RanksLayer->SetVerticalAlignment(VAlign_Bottom);RanksLayer->SetPadding(FMargin(5,0,0,2));
            Entry.Gap=OneLine(TEXT(""),LDHUDStyle::TypeSubBanner,0);HudFace(Entry.Gap,true,2);Entry.Gap->SetColorAndOpacity(HudMomentumText);Entry.Gap->SetVisibility(ESlateVisibility::Collapsed);
            auto* GapLayer=Face->AddChildToOverlay(Entry.Gap);GapLayer->SetHorizontalAlignment(HAlign_Center);GapLayer->SetVerticalAlignment(VAlign_Center);
            // The overdrive hold fills the face from the bottom in gold while the key is held on a full bar.
            Entry.Hold=WidgetTree->ConstructWidget<UProgressBar>();
            {
                FProgressBarStyle HoldStyle;HoldStyle.SetBackgroundImage(FSlateNoResource()).SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,6.f)).SetMarqueeImage(FSlateNoResource());
                Entry.Hold->SetWidgetStyle(HoldStyle);Entry.Hold->SetBarFillType(EProgressBarFillType::BottomToTop);
                Entry.Hold->SetFillColorAndOpacity(LDHUDStyle::Overdrive.CopyWithNewOpacity(.45f));Entry.Hold->SetPercent(0);Entry.Hold->SetVisibility(ESlateVisibility::Collapsed);
            }
            FillLayer(Face->AddChildToOverlay(Entry.Hold));
            Entry.Frame=WidgetTree->ConstructWidget<UBorder>();Entry.Frame->SetPadding(FMargin(3));
            Entry.Frame->SetHorizontalAlignment(HAlign_Fill);Entry.Frame->SetVerticalAlignment(VAlign_Fill);Entry.Frame->SetContent(Face);
            // The ready flash covers the whole slot; neither layer takes input, the button under them does.
            auto* Stack=WidgetTree->ConstructWidget<UOverlay>();
            FillLayer(Stack->AddChildToOverlay(Entry.Frame));
            Entry.Flash=WidgetTree->ConstructWidget<UBorder>();Entry.Flash->SetVisibility(ESlateVisibility::Collapsed);FillLayer(Stack->AddChildToOverlay(Entry.Flash));
            // The icon sends exactly what its hotkey sends; Combat decides Self casts and the armed target forms.
            const ELDCombatEntry Activated=HudActionEntries[Index];
            // A Q/W/E slot fires the entry the local slot order puts there; a press that became a drag fires nothing.
            Entry.Button=LDWidgets::Button(WidgetTree,TEXT(""),[this,PC,Activated,Index]()
            {
                if(bDragging&&DragFrom==Index){return;}
                const ELDCombatEntry Fired=Index<3?PC->SkillAtKey[Index]:Activated;
                if(Fired!=ELDCombatEntry::None){PC->ActivateCombatEntry(Fired);}
            },true,HudActionNames[Index]);
            // The face draws every state itself, so the button keeps no chrome of its own.
            auto ButtonStyle=Entry.Button->GetStyle();ButtonStyle.Normal=FSlateNoResource();ButtonStyle.Hovered=FSlateNoResource();ButtonStyle.Pressed=FSlateNoResource();ButtonStyle.Disabled=FSlateNoResource();
            ButtonStyle.NormalPadding=FMargin(0);ButtonStyle.PressedPadding=FMargin(0);Entry.Button->SetStyle(ButtonStyle);
            Entry.Button->SetContent(Stack);
            if(UButtonSlot* StackSlot=Cast<UButtonSlot>(Stack->Slot)){StackSlot->SetPadding(FMargin(0));StackSlot->SetHorizontalAlignment(HAlign_Fill);StackSlot->SetVerticalAlignment(VAlign_Fill);}
            Entry.Box=WidgetTree->ConstructWidget<USizeBox>();Entry.Box->SetWidthOverride(HudSlotWidth);Entry.Box->SetHeightOverride(HudSlotHeight);
            // A skill point can raise this Q/W/E: the "+" on its upper-right corner opens the rank-up page.
            if(Index<3)
            {
                auto* WithPlus=WidgetTree->ConstructWidget<UOverlay>();
                auto* ButtonLayer=WithPlus->AddChildToOverlay(Entry.Button);ButtonLayer->SetHorizontalAlignment(HAlign_Fill);ButtonLayer->SetVerticalAlignment(VAlign_Fill);
                Entry.Plus=LDWidgets::Button(WidgetTree,TEXT("+"),[this,PC,Index]()
                {
                    const ELDCombatEntry Shown=PC->SkillAtKey[Index];
                    OpenRankUp(Shown==ELDCombatEntry::Q?0:Shown==ELDCombatEntry::W?1:Shown==ELDCombatEntry::E?2:INDEX_NONE);
                },true,FName(*FString::Printf(TEXT("HUDRankUp%d"),Index)));
                Entry.Plus->SetVisibility(ESlateVisibility::Collapsed);
                auto* PlusLayer=WithPlus->AddChildToOverlay(Entry.Plus);PlusLayer->SetHorizontalAlignment(HAlign_Right);PlusLayer->SetVerticalAlignment(VAlign_Top);
                Entry.Box->SetContent(WithPlus);
            }
            else{Entry.Box->SetContent(Entry.Button);}
        }
        // Character actives (Q/W/E) and the innate dash (D) are separate groups, after the body.
        for(int32 Index=0;Index<HudActionCount;++Index)
        {
            Actions->AddChildToHorizontalBox(ActionSlots[Index].Box)
                ->SetPadding(FMargin(Index==3?HudGroupGap:HudSlotGap,0,0,0));
        }
        // #97: the action entries on top, right under the blind-box button and the growth panel; health, momentum and the
        // experience strip below them.
        Centre->AddChildToVerticalBox(Actions);
        Centre->AddChildToVerticalBox(Bar(LDHUDStyle::OwnHealth,B,T,HudCentreWidth,30,&HealthRegenText))->SetPadding(FMargin(0,8,0,0));HealthBar=B;HealthText=T;
        {
            // The momentum bar: 0..100 with a tick every quarter (three thin marks over the track).
            UWidget* MomentumRow=Bar(LDHUDStyle::Momentum,B,T,HudCentreWidth,28);MomentumBar=B;MomentumText=T;
            auto* Ticked=WidgetTree->ConstructWidget<UOverlay>();
            auto* RowSlot=Ticked->AddChildToOverlay(MomentumRow);RowSlot->SetHorizontalAlignment(HAlign_Fill);RowSlot->SetVerticalAlignment(VAlign_Fill);
            auto* Ticks=WidgetTree->ConstructWidget<UHorizontalBox>();Ticks->SetVisibility(ESlateVisibility::HitTestInvisible);
            for(int32 Quarter=0;Quarter<4;++Quarter)
            {
                auto* Gap=WidgetTree->ConstructWidget<USpacer>();
                Ticks->AddChildToHorizontalBox(Gap)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                if(Quarter==3){break;}
                auto* Tick=WidgetTree->ConstructWidget<UBorder>();Tick->SetBrushColor(LDHUDStyle::Rim.CopyWithNewOpacity(.55f));
                auto* TickBox=WidgetTree->ConstructWidget<USizeBox>();TickBox->SetWidthOverride(2);TickBox->SetContent(Tick);
                Ticks->AddChildToHorizontalBox(TickBox)->SetVerticalAlignment(VAlign_Fill);
            }
            auto* TickSlot=Ticked->AddChildToOverlay(Ticks);TickSlot->SetHorizontalAlignment(HAlign_Fill);TickSlot->SetVerticalAlignment(VAlign_Fill);
            Centre->AddChildToVerticalBox(Ticked)->SetPadding(FMargin(0,3,0,0));
        }
        LevelBar=WidgetTree->ConstructWidget<UProgressBar>();HudBarStyle(LevelBar,LDHUDStyle::Experience);LevelBar->SetPercent(0);
        auto* LevelStrip=WidgetTree->ConstructWidget<USizeBox>();LevelStrip->SetHeightOverride(8);LevelStrip->SetContent(LevelBar);
        Centre->AddChildToVerticalBox(LevelStrip)->SetPadding(FMargin(0,4,0,0));
        auto* CentreWidth=WidgetTree->ConstructWidget<USizeBox>();CentreWidth->SetWidthOverride(HudCentreWidth);CentreWidth->SetContent(Centre);
        Bottom->AddChildToHorizontalBox(CentreWidth)->SetVerticalAlignment(VAlign_Bottom);
        Band=LDWidgets::Panel(WidgetTree,Bottom,FMargin(HudBandPadX,10));HitPanels.Add(Band);

        // Above the band: the toast, the only line that says what went wrong (a refused request such as 目标不可用); an armed
        // entry or attack move is shown by its slot ring and the world preview alone. The stack grows upward from the band,
        // so a taller band (text scale) never overlaps it. The whole band is centred on the screen.
        auto* BandStack=WidgetTree->ConstructWidget<UVerticalBox>();BandStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        BandStack->AddChildToVerticalBox(Band)->SetHorizontalAlignment(HAlign_Center);
        // The scale box lays the band out at HudBandScale, so its hit area, popups and the stack above it follow the drawn size.
        auto* BandScale=WidgetTree->ConstructWidget<UScaleBox>();BandScale->SetStretch(EStretch::UserSpecified);BandScale->SetUserSpecifiedScale(HudBandScale);
        BandScale->SetVisibility(ESlateVisibility::SelfHitTestInvisible);BandScale->SetContent(BandStack);
        Place(BandScale,FAnchors(.5f,1),FVector2D(.5f,1),FVector2D(0,-12));
        // The toast is centred on the screen centre by its own canvas slot, 8 units above the band (RefreshReadModel follows
        // the band's height), so its width never shifts it off centre; a translucent face keeps the world visible behind it.
        Toast=OneLine(TEXT(""),LDHUDStyle::TypeText,640);Toast->SetJustification(ETextJustify::Center);
        ToastPanel=Panel(Toast,false);ToastPanel->SetBrushColor(LDHUDStyle::PanelFill.CopyWithNewOpacity(.72f));ToastPanel->SetVisibility(ESlateVisibility::Collapsed);
        ToastSlot=Canvas->AddChildToCanvas(ToastPanel);ToastSlot->SetAnchors(FAnchors(.5f,1));ToastSlot->SetAlignment(FVector2D(.5f,1));ToastSlot->SetAutoSize(true);
        ToastSlot->SetZOrder(14);
        ToastSlot->SetPosition(FVector2D(0,-12-8));
        {
            // The hovered slot's description: the blind-box popup's look, wrapped at a fixed width, centred above
            // the slot (RefreshReadModel places it each frame) and taking no pointer input.
            auto* DetailBox=WidgetTree->ConstructWidget<UVerticalBox>();
            auto FixedWrap=[&](int32 Size){UTextBlock* Line=Text(TEXT(""),Size);Line->SetAutoWrapText(false);Line->SetWrapTextAt(HudDetailWidth-28.f);return Line;};
            ActionDetailName=FixedWrap(LDHUDStyle::TypeLabel);DetailBox->AddChildToVerticalBox(ActionDetailName);
            ActionDetailSummary=FixedWrap(LDHUDStyle::TypeCaption);ActionDetailSummary->SetColorAndOpacity(LDHUDStyle::TextAccent);DetailBox->AddChildToVerticalBox(ActionDetailSummary)->SetPadding(FMargin(0,2,0,0));
            ActionDetailBody=FixedWrap(LDHUDStyle::TypeSmall);DetailBox->AddChildToVerticalBox(ActionDetailBody)->SetPadding(FMargin(0,8,0,0));
            auto* DetailPanel=LDWidgets::Panel(WidgetTree,DetailBox,FMargin(14,12));DetailPanel->SetBrushColor(FLinearColor(.02f,.035f,.055f,.94f));
            auto* DetailWidth=WidgetTree->ConstructWidget<USizeBox>();DetailWidth->SetWidthOverride(HudDetailWidth);DetailWidth->SetContent(DetailPanel);
            DetailWidth->SetVisibility(ESlateVisibility::Collapsed);
            ActionDetailSlot=Canvas->AddChildToCanvas(DetailWidth);ActionDetailSlot->SetAutoSize(true);ActionDetailSlot->SetAlignment(FVector2D(.5f,1));ActionDetailSlot->SetZOrder(20);
        }
        {
            // The rank-up page: the skill's name, two big branch cards (icon, name, one-line change; hover for the numbers)
            // and a close button. A card click submits RankUpSkill.
            auto* PageBox=WidgetTree->ConstructWidget<UVerticalBox>();
            RankTitle=Text(TEXT(""),LDHUDStyle::TypeBanner);HudFace(RankTitle,true,0);RankTitle->SetJustification(ETextJustify::Center);
            PageBox->AddChildToVerticalBox(RankTitle)->SetHorizontalAlignment(HAlign_Center);
            auto* CardRow=WidgetTree->ConstructWidget<UHorizontalBox>();
            for(int32 Card=0;Card<2;++Card)
            {
                auto* CardBox=WidgetTree->ConstructWidget<UVerticalBox>();
                auto* Icon=WidgetTree->ConstructWidget<UImage>();
                auto* IconBox=WidgetTree->ConstructWidget<USizeBox>();IconBox->SetWidthOverride(72);IconBox->SetHeightOverride(72);IconBox->SetContent(Icon);
                CardBox->AddChildToVerticalBox(IconBox)->SetHorizontalAlignment(HAlign_Center);
                UTextBlock* Name=Text(TEXT(""),LDHUDStyle::TypeLabel);HudFace(Name,true,0);Name->SetJustification(ETextJustify::Center);
                CardBox->AddChildToVerticalBox(Name)->SetPadding(FMargin(0,8,0,0));
                UTextBlock* Line=Text(TEXT(""),LDHUDStyle::TypeSmall);Line->SetAutoWrapText(true);Line->SetJustification(ETextJustify::Center);
                CardBox->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,6,0,0));
                auto* CardPanel=LDWidgets::Panel(WidgetTree,CardBox,FMargin(14,12));CardPanel->SetBrushColor(FLinearColor(.04f,.06f,.09f,.96f));
                auto* CardSize=WidgetTree->ConstructWidget<USizeBox>();CardSize->SetWidthOverride(240);CardSize->SetHeightOverride(220);CardSize->SetContent(CardPanel);
                ULDCommandButton* Button=LDWidgets::Button(WidgetTree,TEXT(""),[this,Card](){SubmitRankUp(Card);},true,FName(*FString::Printf(TEXT("HUDRankCard%d"),Card)));
                Button->SetContent(CardSize);
                CardRow->AddChildToHorizontalBox(Button)->SetPadding(FMargin(Card==0?0:24,0,0,0));
                RankCards.Add(Button);RankCardNames.Add(Name);RankCardLines.Add(Line);RankCardIcons.Add(Icon);
            }
            PageBox->AddChildToVerticalBox(CardRow)->SetPadding(FMargin(0,16,0,0));
            ULDCommandButton* Close=LDWidgets::Button(WidgetTree,LDTerms::Close,[this](){RankSlot=INDEX_NONE;MarkReadModelDirty();},true,TEXT("HUDRankClose"));
            PageBox->AddChildToVerticalBox(Close)->SetPadding(FMargin(0,16,0,0));
            RankPage=LDWidgets::Panel(WidgetTree,PageBox,FMargin(24,18));RankPage->SetBrushColor(FLinearColor(.02f,.03f,.05f,.97f));
            RankPage->SetVisibility(ESlateVisibility::Collapsed);
            auto* PageSlot=Canvas->AddChildToCanvas(RankPage);PageSlot->SetAnchors(FAnchors(.5f,.5f));PageSlot->SetAlignment(FVector2D(.5f,.5f));
            PageSlot->SetAutoSize(true);PageSlot->SetZOrder(25);
        }

        // Bottom-right corner: the 构筑 button, which toggles the card tray right above it. It says 构筑 and nothing else; it
        // is lit while the tray has cards (and pulses when new ones arrive), greyed out and unclickable otherwise.
        auto* Prompt=LDWidgets::Button(WidgetTree,LDTerms::Choice,[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Choices);}},true,TEXT("HUDChoicesPrompt"));
        PromptButton=Prompt;
        PromptText=CastChecked<UTextBlock>(Prompt->GetContent());HudFace(PromptText,true,0);PromptText->SetColorAndOpacity(HudPromptInk);
        // The corner style stays consistent with the action band.
        Prompt->SetStyle(HudCornerStyle(Prompt->GetStyle()));
        PendingPrompt=WidgetTree->ConstructWidget<UBorder>();PendingPrompt->SetBrushColor(FLinearColor::Transparent);PendingPrompt->SetPadding(FMargin(0));PendingPrompt->SetContent(Prompt);
        PendingPrompt->SetRenderTransformPivot(FVector2D(.5f,.5f));
        PromptText->SetJustification(ETextJustify::Center);
        {FSlateFontInfo Font=PromptText->GetFont();Font.Size=22;PromptText->SetFont(Font);}
        if(UButtonSlot* CaptionSlot=Cast<UButtonSlot>(PromptText->Slot)){CaptionSlot->SetPadding(FMargin(0));CaptionSlot->SetHorizontalAlignment(HAlign_Center);CaptionSlot->SetVerticalAlignment(VAlign_Center);}
        // Mock 1: a 244 by 52 gold button in the corner, its frame included.
        auto* PromptSize=WidgetTree->ConstructWidget<USizeBox>();PromptSize->SetWidthOverride(HudPromptWidth-8.f);PromptSize->SetHeightOverride(HudPromptHeight-8.f);
        PromptSize->SetContent(PendingPrompt);
        PromptFrame=Panel(PromptSize,true);PromptFrame->SetPadding(FMargin(4));
        PromptSlot=Canvas->AddChildToCanvas(PromptFrame);PromptSlot->SetAnchors(FAnchors(1,1));PromptSlot->SetAlignment(FVector2D(1,1));PromptSlot->SetAutoSize(true);
        PromptSlot->SetPosition(FVector2D(-HudCornerInset,-HudCornerInset));
        {
            // One count per kind of waiting decision on the 构筑 button's corner, each in its kind's colour and cut.
            const FLinearColor Fills[3]={FLinearColor(.24f,.48f,.95f),FLinearColor(.86f,.62f,.16f),LDHUDStyle::UIEvent};
            const float Radii[3]={3.f,3.f,9.f};
            const FMargin Pads[3]={FMargin(5,0),FMargin(4,2),FMargin(6,0)};
            for(int32 Kind=0;Kind<3;++Kind)
            {
                UTextBlock* KindText=OneLine(TEXT(""),LDHUDStyle::TypeTiny,0);KindText->SetColorAndOpacity(FLinearColor::White);HudFace(KindText,true,0);
                UBorder* Badge=WidgetTree->ConstructWidget<UBorder>();Badge->SetBrush(HudRound(Radii[Kind],LDHUDStyle::UIBase,1.f));Badge->SetBrushColor(Fills[Kind]);
                Badge->SetPadding(Pads[Kind]);Badge->SetContent(KindText);Badge->SetVisibility(ESlateVisibility::Collapsed);
                UCanvasPanelSlot* KindSlot=Canvas->AddChildToCanvas(Badge);KindSlot->SetAnchors(FAnchors(1,1));KindSlot->SetAlignment(FVector2D(.5f,.5f));
                KindSlot->SetAutoSize(true);KindSlot->SetZOrder(1);
                PromptBadges.Add(Badge);PromptBadgeTexts.Add(KindText);PromptBadgeSlots.Add(KindSlot);
            }
        }
        {
            // The dragged slot's ghost follows the pointer; it takes no input.
            DragGhostGlyph=OneLine(TEXT(""),LDHUDStyle::TypeSubBanner,0);HudFace(DragGhostGlyph,true,2);DragGhostGlyph->SetJustification(ETextJustify::Center);
            DragGhost=WidgetTree->ConstructWidget<UBorder>();DragGhost->SetBrush(HudRound(6.f,LDHUDStyle::TextAccent,2.f));
            DragGhost->SetHorizontalAlignment(HAlign_Fill);DragGhost->SetVerticalAlignment(VAlign_Fill);
            {
                auto* GhostFace=WidgetTree->ConstructWidget<UOverlay>();
                DragGhostPicture=WidgetTree->ConstructWidget<UImage>();DragGhostPicture->SetVisibility(ESlateVisibility::Collapsed);
                auto* PictureLayer=GhostFace->AddChildToOverlay(DragGhostPicture);PictureLayer->SetHorizontalAlignment(HAlign_Fill);PictureLayer->SetVerticalAlignment(VAlign_Fill);
                auto* GlyphLayer=GhostFace->AddChildToOverlay(DragGhostGlyph);GlyphLayer->SetHorizontalAlignment(HAlign_Center);GlyphLayer->SetVerticalAlignment(VAlign_Center);
                DragGhost->SetContent(GhostFace);
            }
            auto* GhostBox=WidgetTree->ConstructWidget<USizeBox>();GhostBox->SetWidthOverride(HudSlotWidth*HudBandScale);GhostBox->SetHeightOverride(HudSlotHeight*HudBandScale);
            GhostBox->SetContent(DragGhost);GhostBox->SetRenderOpacity(.8f);GhostBox->SetVisibility(ESlateVisibility::Collapsed);
            DragGhostSlot=Canvas->AddChildToCanvas(GhostBox);DragGhostSlot->SetAutoSize(true);DragGhostSlot->SetAlignment(FVector2D(.5f,.5f));DragGhostSlot->SetZOrder(30);
        }

        // The workshop lies under the tray and the storage, which it lays out (ULDWorkshop).
        Workshop=CreateWidget<ULDWorkshop>(PC);
        if(Workshop)
        {
            auto* WorkshopSlot=Canvas->AddChildToCanvas(Workshop);WorkshopSlot->SetAnchors(FAnchors(0,0,1,1));WorkshopSlot->SetOffsets(FMargin(0));WorkshopSlot->SetZOrder(8);
            Workshop->RequestClose=[this]()
            {
                auto* Owner=Cast<ALDPlayerController>(GetOwningPlayer());
                if(Owner&&Owner->RootUI&&ChoicesPanel&&ChoicesPanel->IsTrayOpen()){Owner->RootUI->ToggleMainPanel(ELDUIMainPanel::Choices);}
            };
        }
        else{UE_LOG(LogLDUI,Error,TEXT("LD_HUD_WORKSHOP_CREATION_FAILED"));}
        // Normal main panels fill the canvas with a hit-transparent frame, so the world beside them keeps its input.
        ChoicesPanel=CreateWidget<ULDChoicesPanel>(PC);
        DevTestPanel=CreateWidget<ULDDevTestPanel>(PC);
        const TPair<ELDUIMainPanel,ULDPanel*> Docked[]={{ELDUIMainPanel::Choices,ChoicesPanel.Get()},{ELDUIMainPanel::DevTest,DevTestPanel.Get()}};
        for(const TPair<ELDUIMainPanel,ULDPanel*>& Entry:Docked)
        {
            if(!Entry.Value){UE_LOG(LogLDUI,Error,TEXT("LD_HUD_PANEL_CREATION_FAILED %d"),int32(Entry.Key));continue;}
            auto* PanelSlot=Canvas->AddChildToCanvas(Entry.Value);PanelSlot->SetAnchors(FAnchors(0,0,1,1));PanelSlot->SetOffsets(FMargin(0));PanelSlot->SetZOrder(10);
            if(PC->RootUI){PC->RootUI->RegisterMainPanel(Entry.Key,Entry.Value);}
        }
        // #227: the storage fills the canvas with a hit-transparent frame above the band and below the main panels' popups.
        Storage=CreateWidget<ULDComponentStorage>(PC);
        if(Storage)
        {
            auto* StorageSlot=Canvas->AddChildToCanvas(Storage);StorageSlot->SetAnchors(FAnchors(0,0,1,1));StorageSlot->SetOffsets(FMargin(0));StorageSlot->SetZOrder(12);
            Storage->Notify=[this](const FString& Notice){ShowToast(Notice);};
        }
        else{UE_LOG(LogLDUI,Error,TEXT("LD_HUD_STORAGE_CREATION_FAILED"));}
        if(ChoicesPanel)
        {
            ChoicesPanel->Notify=[this](const FString& Notice){ShowToast(Notice);};
        }
        // Growth's owning-connection entries; the panels only build commands from the views they showed.
        const TWeakObjectPtr<ALDPlayerController> WeakPC(PC);
        if(ChoicesPanel)
        {
            ChoicesPanel->SubmitBuild=[WeakPC](const FLDBuildCommand& Command,ELDGrowthReason& Reason)->uint64
            {if(ALDPlayerController* Owner=WeakPC.Get()){return Owner->SubmitBuild(Command,Reason);}Reason=ELDGrowthReason::DataNotReady;return 0;};
            ChoicesPanel->PeekRequestId=[WeakPC]()->uint64{const ALDPlayerController* Owner=WeakPC.Get();return Owner?Owner->PeekGrowthRequestId(ELDGrowthFamily::Build):0;};
        }
        if(Storage)
        {
            Storage->SubmitBuild=[WeakPC](const FLDBuildCommand& Command,ELDGrowthReason& Reason)->uint64
            {if(ALDPlayerController* Owner=WeakPC.Get()){return Owner->SubmitBuild(Command,Reason);}Reason=ELDGrowthReason::DataNotReady;return 0;};
            Storage->PeekRequestId=[WeakPC]()->uint64{const ALDPlayerController* Owner=WeakPC.Get();return Owner?Owner->PeekGrowthRequestId(ELDGrowthFamily::Build):0;};
        }
        if(StatBar)
        {
            StatBar->Notify=[this](const FString& Notice){ShowToast(Notice);};
            StatBar->SubmitBuild=[WeakPC](const FLDBuildCommand& Command,ELDGrowthReason& Reason)->uint64
            {if(ALDPlayerController* Owner=WeakPC.Get()){return Owner->SubmitBuild(Command,Reason);}Reason=ELDGrowthReason::DataNotReady;return 0;};
            StatBar->PeekRequestId=[WeakPC]()->uint64{const ALDPlayerController* Owner=WeakPC.Get();return Owner?Owner->PeekGrowthRequestId(ELDGrowthFamily::Build):0;};
        }
        if(Workshop){Workshop->Storage=Storage;Workshop->StatBar=StatBar;Workshop->Choices=ChoicesPanel;}
        if(ChoicesPanel)
        {
            const TWeakObjectPtr<ULDWorkshop> WeakWorkshop(Workshop.Get());
            ChoicesPanel->FinalModApplied=[WeakWorkshop](const FLDBuildReceipt& Receipt,const FLDFinalModSelection& Selection)
            {if(ULDWorkshop* Current=WeakWorkshop.Get()){Current->FinalModApplied(Receipt,Selection);}};
        }
        if(StatBar)
        {
            const TWeakObjectPtr<ULDWorkshop> WeakWorkshop(Workshop.Get());
            StatBar->AllocationApplied=[WeakWorkshop](const FLDBuildReceipt& Receipt)
            {if(ULDWorkshop* Current=WeakWorkshop.Get()){Current->AllocationApplied(Receipt);}};
        }
        PC->OnGrowthSubmit.AddUObject(this,&ULDHUD::HandleGrowthSubmit);
        PC->OnBuildReceipt.AddUObject(this,&ULDHUD::HandleBuildReceipt);
        PC->OnTrainingReset.AddUObject(this,&ULDHUD::HandleTrainingReset);
        PC->OnRestReady.AddUObject(this,&ULDHUD::HandleRestReady);
        PC->OnPKBet.AddUObject(this,&ULDHUD::HandlePKBet);
        PC->OnPKThrow.AddUObject(this,&ULDHUD::HandlePKThrow);
        PC->OnSpawnChoice.AddUObject(this,&ULDHUD::HandleSpawnChoice);
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
TArray<FSlateRect> ULDHUD::WorldLabelReservedAreas(const FGeometry& PaintGeometry) const
{
    TArray<FSlateRect> Areas;
    const auto Add=[&](const UWidget* Widget)
    {
        if(!Widget||!Widget->IsVisible()){return;}
        const FGeometry& Geometry=Widget->GetCachedGeometry();
        const FVector2D TopLeft=PaintGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D::ZeroVector));
        const FVector2D BottomRight=PaintGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize()));
        Areas.Add(FSlateRect(TopLeft.X,TopLeft.Y,BottomRight.X,BottomRight.Y));
    };
    Add(Band);
    Add(PromptFrame);
    Add(TargetCard);
    Add(MapColumn);
    Add(PhaseBanner);
    Add(RestActionsPanel);
    Add(PKPanelBorder);
    Add(SpawnChoicePanel);
    Add(DeathBanner);
    Add(EventFeedPanel);
    Add(ToastPanel);
    if(Storage){for(const UWidget* Shown:Storage->ReservedWidgets()){Add(Shown);}}
    return Areas;
}
bool ULDHUD::IsUIAt(FVector2D P) const
{
    if(Minimap&&Minimap->IsDragging()){return true;}
    if(Storage&&Storage->IsHit(P)){return true;}
    if(Workshop&&Workshop->IsHit(P)){return true;}
    if(ChoicesPanel&&ChoicesPanel->IsQuickHit(P)){return true;}
    for(const ULDPanel* Open:MainPanels()){if(Open&&Open->IsPanelHit(P)){return true;}}
    for(const auto& Panel:HitPanels){if(Panel&&Panel->IsVisible()&&Panel->GetCachedGeometry().IsUnderLocation(P)){return true;}}return false;
}
TArray<ULDPanel*,TInlineAllocator<4>> ULDHUD::MainPanels() const
{
    TArray<ULDPanel*,TInlineAllocator<4>> Result;
    for(ULDPanel* Panel:{static_cast<ULDPanel*>(ChoicesPanel.Get()),static_cast<ULDPanel*>(DevTestPanel.Get())}){if(Panel){Result.Add(Panel);}}
    return Result;
}
void ULDHUD::ToggleAttributeView()
{
    MarkReadModelDirty();
    if(StatBar){StatBar->ToggleView();}
}
void ULDHUD::ToggleWorkshop()
{
    MarkReadModelDirty();
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC||!PC->RootUI||!ChoicesPanel){return;}
    const bool bOpen=ChoicesPanel->IsTrayOpen();
    const bool bPromptReady=PromptButton&&PromptButton->GetIsEnabled()&&PromptFrame&&PromptFrame->IsVisible();
    if(bOpen||bPromptReady){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Choices);}
}
void ULDHUD::HandleDevTestResult(const FLDDevTestResult& Result)
{
    MarkReadModelDirty();
    if(DevTestPanel){DevTestPanel->HandleResult(Result);}
}
FReply ULDHUD::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{return IsUIAt(E.GetScreenSpacePosition())?FReply::Handled():Super::NativeOnMouseButtonDown(G,E);}
FReply ULDHUD::NativeOnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    const FVector2D At=E.GetScreenSpacePosition();
    // A left press on a filled Q/W/E slot may become a drag (UpdateDrag); the press still reaches the button.
    if(E.GetEffectingButton()==EKeys::LeftMouseButton&&Band&&Band->IsVisible())
    {
        DragFrom=INDEX_NONE;bDragging=false;
        for(int32 Index=0;Index<FMath::Min(3,ActionSlots.Num());++Index)
        {
            const FLDHUDActionSlot& Entry=ActionSlots[Index];
            if(Entry.bHasSource&&Entry.Box&&Entry.Box->GetCachedGeometry().IsUnderLocation(At)){DragFrom=Index;DragStart=At;break;}
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
        bDragging=true;
        const FLDHUDActionSlot& From=ActionSlots[DragFrom];
        if(DragGhost){DragGhost->SetBrushColor(From.Icon->GetBrushColor());}
        HudSetText(DragGhostGlyph,From.Glyph->GetText().ToString());
        const bool bPicture=From.Picture->GetVisibility()!=ESlateVisibility::Collapsed;
        if(bPicture){DragGhostPicture->SetBrush(From.Picture->GetBrush());}
        HudSetShown(DragGhostPicture,bPicture,ESlateVisibility::HitTestInvisible);
        HudSetShown(DragGhostGlyph,!bPicture,ESlateVisibility::HitTestInvisible);
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
        // Dropped on another Q/W/E slot: the two trade places.
        for(int32 To=0;To<FMath::Min(3,ActionSlots.Num());++To)
        {
            if(To==DragFrom||!ActionSlots[To].Box->GetCachedGeometry().IsUnderLocation(At)){continue;}
            Swap(PC->SkillAtKey[DragFrom],PC->SkillAtKey[To]);
            for(FLDHUDActionSlot& Changed:ActionSlots){Changed.AppliedLook=-1;Changed.bCooling=false;}
            break;
        }
    }
    DragFrom=INDEX_NONE;bDragging=false;
    if(Ghost){Ghost->SetVisibility(ESlateVisibility::Collapsed);}
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

void ULDHUD::SampleReadModel()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* GS=GetWorld()->GetGameState<ALDGameState>();if(!GS||!Band){return;}
    // Cues follow every sample, including while a menu covers the HUD or the result is shown.
    ObserveAudio(*PC,*GS);
    LDHUD::FReadModelKey Key;
    {TRACE_CPUPROFILER_EVENT_SCOPE(LDHUDReadModelKey);Key=ReadModelKey(*PC,*GS);}
    if(!ReadModelGate.ShouldRebuild(Key)){return;}
    TRACE_CPUPROFILER_EVENT_SCOPE(LDHUDRebuildReadModel);
    RebuildReadModel(*PC,*GS);
    ReadModelGate.Rebuilt(Key,bReadModelLive);
}
void ULDHUD::RefreshReadModel()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* GS=GetWorld()->GetGameState<ALDGameState>();if(!GS||!Band){return;}
    ObserveAudio(*PC,*GS);
    RebuildReadModel(*PC,*GS);
}
LDHUD::FReadModelKey ULDHUD::ReadModelKey(ALDPlayerController& PC,const ALDGameState& GS)
{
    LDHUD::FReadModelKey Key;
    {
        bool bChanged=false;
        const auto Compared=[&bChanged](auto& Kept,const auto& Now)
        {
            if(!std::decay_t<decltype(Now)>::StaticStruct()->CompareScriptStruct(&Kept,&Now,PPF_None)){Kept=Now;bChanged=true;}
        };
        const auto Same=[&bChanged](auto& Kept,const auto& Now){if(!(Kept==Now)){Kept=Now;bChanged=true;}};
        FReadModelSeen& Seen=ReadModelSeen;
        Compared(Seen.Owner,PC.OwnerState);
        // The public match and the lobby advance their revision with every publication (PublishPublicMatch, PublishLobby).
        Same(Seen.PublicGeneration,GS.PublicMatch.Generation);Same(Seen.PublicRevision,GS.PublicMatch.PublicRevision);
        Same(Seen.LobbyRevision,GS.DemoLobby.PublicRevision);Same(Seen.RosterRevision,GS.DemoLobby.RosterRevision);
        Compared(Seen.Kills,GS.KillFeed);Compared(Seen.Result,GS.MatchResult);Compared(Seen.Recap,PC.DeathRecap);
        Compared(Seen.Summary,PC.DamageSummary);
        // Each accepted reaction-assist prompt advances the controller's serial (ClientReactionAssist).
        Same(Seen.ReactionAssistSerial,PC.ReactionAssistSerial);
        // Combat replaces its owner view only with a new revision, generation, life or body (ALDCombatDirector::PublishOwnerViews).
        Same(Seen.CombatGeneration,PC.CombatView.Generation);Same(Seen.CombatLife,PC.CombatView.Life);
        Same(Seen.CombatRevision,PC.CombatView.Revision);Same(Seen.bCombatBody,PC.CombatView.bHasBody);
        const auto* Body=Cast<ALDCharacter>(PC.GetPawn());
        Same(Seen.Body,TWeakObjectPtr<const AActor>(Body));Same(Seen.Presentation,Body?int32(Body->PresentationKey):-1);
        Same(Seen.Content,static_cast<const void*>(DisplayContent.Get()));
        if(bChanged){++ReadModelViews;}
    }
    Key.Views=ReadModelViews;Key.Local=ReadModelLocal;
    Key.CombatReceipts=PC.CombatReceipts.Serial();Key.Feedback=PC.FeedbackSerial();
    {
        // The target card follows the inspected unit in each new observed frame.
        FLDObservedUnitView Subject;
        if(PC.ReadInspected(Subject))
        {
            const ALDWorldViewRelay* Relay=PC.LocalView();
            Key.Frame=HashCombineFast(GetTypeHash(Subject.Handle),Subject.ObservationEpoch)|(uint64(Relay?Relay->ReadObservedFrame().LocalSerial:0)+1)<<32;
        }
    }
    // Hover and popups change only with the pointer over the HUD or an own status mark over the own bars; moving it over
    // the world is not an input.
    const bool bSlate=FSlateApplication::IsInitialized();
    const FVector2D At=bSlate?FSlateApplication::Get().GetCursorPos():FVector2D(-1,-1);
    if(bSlate&&(PointerOverUI()||OwnStatusHitRects.ContainsByPredicate([&At](const FSlateRect& Mark){return Mark.ContainsPoint(At);})))
    {
        Key.Interaction=FSlateApplication::Get().GetLastUserInteractionTime();Key.Pointer=At;
    }
    else{Key.Pointer=FVector2D(-1,-1);}
    Key.Viewport=GetCachedGeometry().GetLocalSize();
    Key.TextScale=PC.Controls?PC.Controls->TextScale:1.f;
    {
        uint64 Flags=0;int32 Shift=0;
        const auto Put=[&Flags,&Shift](uint64 Value,int32 Bits){Flags|=(Value&((uint64(1)<<Bits)-1))<<Shift;Shift+=Bits;};
        Put(PC.ControlsUI&&PC.ControlsUI->IsOpen(),1);Put(PC.RootUI&&PC.RootUI->IsMenuOpen(),1);
        Put(PC.RootUI?uint64(PC.RootUI->ActiveMainPanel()):0,8);
        Put(PC.IsWorldInputSuppressed(),1);Put(PC.IsWorldInputCaptured(),1);
        const auto Held=[&PC](const TCHAR* Action){const FKey Bound=PC.BindingKey(Action);return Bound.IsValid()&&PC.IsInputKeyDown(Bound);};
        Put(Held(TEXT("Details")),1);Put(Held(TEXT("Scoreboard")),1);
        ELDCombatEntry Armed=ELDCombatEntry::None;Put(PC.ReadArmedCombatEntry(Armed),1);Put(uint64(Armed),8);
        for(const ELDCombatEntry Entry:PC.SkillAtKey){Put(uint64(Entry),8);}
        Put(PC.BuildBlueprints&&PC.BuildBlueprints->IsViewerOpen(),1);
        Put(ChoicesPanel&&ChoicesPanel->IsPanelOpen(),1);Put(ChoicesPanel&&ChoicesPanel->IsTrayOpen(),1);
        Put(Workshop&&Workshop->IsOpen(),1);Put(Storage&&Storage->IsDragging(),1);Put(bDragging,1);
        int64 NowMs=0;Put(PC.ReadDemoAuthorityTimeMs(NowMs),1);
        // Owner statistics that arrive by RPC (the low bits of their serials are enough to notice a new one).
        Put(PC.BuildStats.Serial,4);Put(PC.TrainingCombo.Serial,4);
        check(Shift<=64);
        Key.Flags=Flags;
    }
    {
        // Clock texts show while the HUD is up in play; other covered states hold no countdown.
        const FLDPublicMatchView& Public=GS.PublicMatch;
        const bool bPlaying=Public.Generation!=0&&Public.Generation==PC.OwnerState.Match
            &&(Public.Phase==ELDMatchPhase::Rest||Public.Phase==ELDMatchPhase::Development||Public.Phase==ELDMatchPhase::CaptureOpen);
        const bool bCovered=(PC.ControlsUI&&PC.ControlsUI->IsOpen())||(PC.RootUI&&PC.RootUI->IsMenuOpen());
        Key.Clock=LDHUD::ReadModelClock(GetWorld()->GetRealTimeSeconds(),bPlaying&&!bCovered);
    }
    return Key;
}
void ULDHUD::RebuildReadModel(ALDPlayerController& PlayerController,const ALDGameState& GameState)
{
    ALDPlayerController* PC=&PlayerController;const ALDGameState* GS=&GameState;
    bReadModelLive=false;
    const FLDOwnerState& O=PC->OwnerState;const FLDLobbyView& Lobby=GS->DemoLobby;const FLDPublicMatchView& Public=GS->PublicMatch;
    // PublicMatch is the single public phase source; a projection from another generation is never this match.
    const bool bOwnMatch=Public.Generation!=0&&Public.Generation==O.Match;
    const bool Playing=bOwnMatch&&(Public.Phase==ELDMatchPhase::Rest||Public.Phase==ELDMatchPhase::Development||Public.Phase==ELDMatchPhase::CaptureOpen);
    const bool bResults=bOwnMatch&&Public.Phase==ELDMatchPhase::Results;
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
        // The result replaces gameplay UI: no main panel outlives play (VISION_AND_UI navigation); closing never undoes submitted work.
        for(ULDPanel* Open:MainPanels()){if(Open->IsPanelOpen()){Open->ClosePanel();}}
    }
    if(bOwnMatch&&PC->SlotOrderMatch!=O.Match){PC->ResetSlotOrder(O.Match);}
    // The workshop is the open 构筑 tray's full view; it hides the band (the skill keys still work) and, full screen, the
    // corners it covers. Full screen in a rest's build, payout and return steps and in the own death wait.
    const LDHUD::FDeathView Death=LDHUD::Death(O,bClock,NowMs,Public.Showdown.bStarted&&O.RespawnAtMs<=0,&PC->CombatView);
    const bool bDeathWait=bOwnMatch&&Death.bWaiting;
    const bool bRestPhase=bOwnMatch&&Public.Phase==ELDMatchPhase::Rest;
    const bool bWorkshopFull=LDWorkshop::FullScreen(bRestPhase,Public.Timeline.Step,bDeathWait);
    const bool bWorkshopOpen=bShow&&ChoicesPanel&&ChoicesPanel->IsTrayOpen();
    HudSetShown(Band,bShow&&!bWorkshopOpen,ESlateVisibility::Visible);
    HudSetShown(PromptFrame,bShow&&!bWorkshopOpen,ESlateVisibility::Visible);HudSetShown(MapColumn,bShow&&!bWorkshopOpen,ESlateVisibility::Visible);
    HudSetShown(PhaseBanner,bShow&&!(bWorkshopOpen&&bWorkshopFull),ESlateVisibility::HitTestInvisible);
    HudSetShown(ResultPanel,bResults&&!Overlay,ESlateVisibility::Visible);
    if(bResults&&!Overlay){RefreshResult(*PC,*GS);}
    // The arena rings are world presentation: they follow the PK even while a menu covers the HUD.
    SyncPKRing(Public,bOwnMatch,bClock,NowMs);
    // The rings are world presentation: they follow the match even while a menu covers the HUD.
    SyncShowdownRing(Public,bOwnMatch,bClock,NowMs);
    SyncObjectiveRing(Public,bOwnMatch);
    if(!bShow)
    {
        // A covered HUD keeps no armed throw: the next world click must not throw blind.
        bPKThrowArmed=false;
        if(StatBar){StatBar->SetDetailsHeld(false);}
        if(Storage){Storage->SetDetailsHeld(false);}
        if(Workshop){Workshop->Present(ULDWorkshop::FInput());}
        if(Storage){Storage->SetWorkshop(ULDComponentStorage::FWorkshopLayout());}
        if(Storage){Storage->Present(nullptr,O.Match,LDProgression::EMatchStage::Closed,O.LifePhase,nullptr,ChoiceStats,ULDComponentStorage::FAnchors(),false);}
        for(UWidget* Hidden:{static_cast<UWidget*>(TargetCard.Get()),static_cast<UWidget*>(DeathBanner.Get()),static_cast<UWidget*>(ToastPanel.Get()),static_cast<UWidget*>(EventFeedPanel.Get()),
            static_cast<UWidget*>(ReturnCountdownText.Get()),static_cast<UWidget*>(RestActionsPanel.Get()),static_cast<UWidget*>(PKPanelBorder.Get()),static_cast<UWidget*>(SpawnChoicePanel.Get()),
            static_cast<UWidget*>(ScorePanel.Get()),ActionDetailSlot?ActionDetailSlot->Content.Get():nullptr}){HudSetShown(Hidden,false,ESlateVisibility::Visible);}
        // The result enters row by row until AnimateResult settles it, and the showdown ring shrinks: both move with time.
        bReadModelLive=(bResults&&!Overlay&&ResultShownAt>=0.)||ShowdownRing.IsValid();
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
    if(Workshop){Workshop->ApplyTextScale(TextScale);}
    if(Storage){Storage->ApplyTextScale(TextScale);}
    const FLDDemoContentDefinitions* Content=DisplayDefinitions();

    // The final field names the graffiti owner with his public final mod.
    const FString OwnerName=LDTerms::HolderWithMod(LDHUD::RosterName(Lobby,Public.GraffitiOwner),
        Public.Showdown.bStarted?LDHUD::PublicFinalMod(Public,Public.GraffitiOwner,Content):FString());
    const LDHUD::FCircleView Circle=LDHUD::Circle(Public,LDHUD::RosterName(Lobby,Public.Occupier),OwnerName,bClock,NowMs,
        Public.Showdown.bStarted?Public.Showdown.SprayMs:Content?Content->Parameters.CaptureSprayMs:0,
        Public.Showdown.bStarted?Public.Showdown.GuardMs:Content?Content->Parameters.CaptureGuardMs:0);
    // The rest services panel replaces the rest's top lines; the 构筑 tray in the corner does not.
    const bool bChoicesRestOpen=Public.Phase==ELDMatchPhase::Rest&&ChoicesPanel&&ChoicesPanel->IsPanelOpen()&&!ChoicesPanel->IsTrayOpen();
    const bool bFieldPhase=Public.Phase==ELDMatchPhase::Development||Public.Phase==ELDMatchPhase::CaptureOpen;
    const FLDCombatOwnerView* OwnCombat=bOwnMatch&&O.Life!=0&&PC->CombatView.Generation==O.Match&&PC->CombatView.Life==O.Life?&PC->CombatView:nullptr;
    const bool bEngaged=OwnCombat&&bClock&&NowMs<OwnCombat->BuildSafeAtMs;
    LDWorldFeedback::FSample OwnSample;OwnSample.Match=O.Match;OwnSample.Life=O.Life;OwnSample.LifePhase=O.LifePhase;OwnSample.Self=O.ContenderId;OwnSample.OwnCombat=OwnCombat;
    TArray<LDWorldFeedback::FEvent> BodyEvents;OwnHitWatch.Observe(OwnSample,BodyEvents);
    const bool bOwnHit=BodyEvents.ContainsByPredicate([](const LDWorldFeedback::FEvent& Event)
    {return Event.bOwn&&(Event.Kind==LDWorldFeedback::EKind::DamageTaken||Event.Kind==LDWorldFeedback::EKind::ShieldAbsorbed);});
    const bool bRestPlanning=Public.Phase==ELDMatchPhase::Rest&&Public.Timeline.Step==ELDRestStep::Build;
    HudSetText(PhaseText,Circle.Head);
    const bool bPhaseLine=!bChoicesRestOpen&&(Public.Phase!=ELDMatchPhase::Development||Public.Showdown.bStarted)&&Public.Phase!=ELDMatchPhase::Rest;
    HudSetShown(PhaseText,bPhaseLine,ESlateVisibility::HitTestInvisible);
    HudSetText(PhaseOwnerText,Circle.Owner);HudSetText(PhaseAsideText,Circle.Aside);
    HudSetShown(PhaseFootRow,bPhaseLine&&!(Circle.Owner.IsEmpty()&&Circle.Aside.IsEmpty()),ESlateVisibility::HitTestInvisible);
    CircleProgress->SetPercent(Circle.Progress);
    HudSetText(DevMarkerText,!Lobby.DevTestSession&&!Lobby.DevContentReload?FString()
        :FString(Lobby.DevTestSession?TEXT("测试模式"):TEXT("开发房间"))+(Lobby.DevContentReload?TEXT(" · 内容热重载"):TEXT("")));
    HudSetShown(DevMarkerText,Lobby.DevTestSession||Lobby.DevContentReload,ESlateVisibility::HitTestInvisible);
    HudSetShown(CircleProgress->GetParent(),Circle.bProgress,ESlateVisibility::HitTestInvisible);
    {
        const LDHUD::FTimelineView Timeline=LDHUD::TimelineBanner(Public,O.ContenderId,bClock,NowMs);
        HudSetText(TimelineText,Timeline.Headline);HudSetShown(TimelineText,!Timeline.Headline.IsEmpty(),ESlateVisibility::HitTestInvisible);
        HudSetText(TimelineDetailText,Timeline.Detail);
        HudSetShown(TimelineDetailText,LDHUD::TimelineDetailVisible(Public,Timeline,bChoicesRestOpen),ESlateVisibility::HitTestInvisible);
        // The reset exists only in a rest's open steps (build and payout); leaving them drops a wait the server will still answer.
        const bool bRestActions=bRestPlanning||(Public.Phase==ELDMatchPhase::Rest&&Public.Timeline.Step==ELDRestStep::Payout);
        if(!bRestActions){bTrainingResetInFlight=false;bRestReadyInFlight=false;}
        HudSetShown(RestActionsPanel,bRestActions,ESlateVisibility::Visible);
        const FString ReadyLabel=O.bRestReady?TEXT("已准备"):bRestReadyInFlight?TEXT("提交中…"):TEXT("准备完毕");
        if(bRestActions)
        {
            if(TrainingResetButton->GetIsEnabled()==bTrainingResetInFlight){TrainingResetButton->SetIsEnabled(!bTrainingResetInFlight);}
            HudSetText(Cast<UTextBlock>(RestReadyButton->GetContent()),ReadyLabel);
            RestReadyButton->SetIsEnabled(O.bCanRestReady&&!O.bRestReady&&!bRestReadyInFlight);
            HudSetShown(RestReadyButton,bRestPlanning,ESlateVisibility::Visible);
            const bool bThisRest=RestAnswerMatch==Public.Generation&&RestAnswerRest==Public.Timeline.RestIndex;
            HudSetText(RestAnswerText,bThisRest?RestAnswer:FString());
            HudSetShown(RestAnswerText,bThisRest&&!RestAnswer.IsEmpty(),ESlateVisibility::HitTestInvisible);
        }
        SyncSignupMarker(Public,O.Match,bRestActions&&Timeline.bSignupStep,Timeline.bSignupOpen,Timeline.bSignedUp);
        // A finished training combo on the dummy: each source and its damage, largest first, beside the rest actions.
        if(const FLDTrainingComboView& Combo=PC->TrainingCombo;Combo.Match==O.Match&&Combo.Serial!=SeenTrainingCombo)
        {
            SeenTrainingCombo=Combo.Serial;
            int64 TotalP2=0;
            TArray<FString> Parts;
            for(const FLDDamageLine& Line:Combo.Lines)
            {
                TotalP2+=Line.DamageP2;
                if(Parts.Num()<5){Parts.Add(FString::Printf(TEXT("%s %lld"),*LDHUD::DamageSourceName(Content,Line.Source),Line.DamageP2/100));}
            }
            if(TotalP2>0){ShowRestAnswer(FString::Printf(TEXT("%lld：%s"),TotalP2/100,*FString::Join(Parts,TEXT(" · "))));}
        }
    }
    RefreshPK(*PC,Public,Lobby,bClock,NowMs);
    {
        // P6: the death screen's spawn choice (own owner projection only).
        const LDHUD::FSpawnChoiceView Choice=bOwnMatch?LDHUD::SpawnChoice(O,bClock,NowMs):LDHUD::FSpawnChoiceView();
        if(!Choice.bVisible){bSpawnChoiceInFlight=false;}
        HudSetShown(SpawnChoicePanel,Choice.bVisible,ESlateVisibility::Visible);
        for(int32 Index=0;Index<SpawnChoiceButtons.Num();++Index)
        {
            ULDCommandButton* Button=SpawnChoiceButtons[Index];
            const bool bShown=Choice.bVisible&&Index<Choice.Count;
            HudSetShown(Button,bShown,ESlateVisibility::Visible);
            if(!bShown){continue;}
            const FLinearColor Tint=Index==Choice.Chosen?HudPromptFill:FLinearColor::White;
            if(Button->GetBackgroundColor()!=Tint){Button->SetBackgroundColor(Tint);}
            if(Button->GetIsEnabled()==bSpawnChoiceInFlight){Button->SetIsEnabled(!bSpawnChoiceInFlight);}
        }
        HudSetText(SpawnChoiceCountdown,Choice.Countdown);
        // The day's modifiers beside the match timer.
        const bool bDaily=bOwnMatch&&Public.Phase!=ELDMatchPhase::Lobby;
        const TArray<ELDDailyModifier> Daily=bDaily?Public.DailyModifiers:TArray<ELDDailyModifier>();
        if(Daily!=ShownDaily)
        {
            ShownDaily=Daily;
            for(int32 Index=0;Index<DailyBadges.Num();++Index)
            {
                const bool bShown=Daily.IsValidIndex(Index);
                HudSetShown(DailyBadges[Index],bShown,ESlateVisibility::Visible);
                if(!bShown){continue;}
                HudSetText(DailyBadgeGlyphs[Index],LDTerms::DailyGlyph(Daily[Index]));
                const FString Tip=FString(LDTerms::DailyName(Daily[Index]))+(Content?TEXT("\n")+LDHUD::DailyDescription(Daily[Index],Content->Parameters):FString());
                DailyBadges[Index]->SetToolTipText(FText::FromString(Tip));
            }
        }
        bReadModelLive|=Choice.bVisible;
    }
    RefreshEventFeed(*GS,O,Content,bClock,NowMs);
    RefreshScoreboard(*GS,O);
    HudSetText(ReturnCountdownText,LDHUD::RestReturnCountdown(Public,bClock,NowMs));
    HudSetShown(ReturnCountdownText,!ReturnCountdownText->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    HudSetText(MatchTimerText,LDHUD::MatchTimer(Public,bClock,NowMs));
    HudSetShown(MatchTimerText,!MatchTimerText->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    HudSetShown(DeathBanner,Death.bVisible,ESlateVisibility::HitTestInvisible);
    // Moving with time: the arena ring, the flagpole bar and the death countdown with its ring.
    bReadModelLive|=ShowdownRing.IsValid()||PKRing.IsValid()||Circle.bProgress||Death.bVisible;
    if(Death.bVisible)
    {
        HudSetText(DeathTitle,Death.Title);HudSetText(DeathCountdown,Death.Countdown);HudSetText(DeathNote,Death.Note);
        FString Low,High,Elite,Dean;HudCampNames(Content,Low,High,Elite,Dean);
        const int64 NotBeforeMs=LifeSeenMatch==O.Match&&LifeSeenLife==O.Life?LifeSeenAliveMs:0;
        const FString Cause=LDHUD::DeathCause(GS->KillFeed,Public.Generation,O.ContenderId,NotBeforeMs,Lobby,Low,High,Elite,Dean);
        HudSetText(DeathCauseText,Cause);HudSetShown(DeathCauseText,!Cause.IsEmpty(),ESlateVisibility::HitTestInvisible);
        // Until the recap of exactly this life arrives there is no recap section at all.
        const LDHUD::FDamageListView Recap=LDHUD::DeathRecap(PC->DeathRecap,O,Lobby,Content);
        HudSetText(DeathRecapHeading,Recap.Heading);HudSetShown(DeathRecapHeading,Recap.bAvailable,ESlateVisibility::HitTestInvisible);
        // Each row is its icon chain (who triggered → what it consumed → what it produced), rebuilt only when it changes;
        // new text takes the current text scale, later scale changes rescale it with the rest of the HUD.
        const float Scale=AppliedScale>0?AppliedScale:1.f;
        for(int32 Row=0;Row<DeathRecapRows.Num();++Row)
        {
            const bool bRow=Recap.bAvailable&&Recap.Chains.IsValidIndex(Row);
            const LDHUD::FRecapChain Chain=bRow?Recap.Chains[Row]:LDHUD::FRecapChain();
            if(DeathRecapShown.IsValidIndex(Row)&&!(DeathRecapShown[Row]==Chain))
            {
                DeathRecapShown[Row]=Chain;
                LDRecapChain::Fill(WidgetTree,DeathRecapRows[Row],Chain,FMath::RoundToFloat(20.f*Scale),FMath::RoundToInt(14.f*Scale));
            }
            HudSetShown(DeathRecapRows[Row],bRow,ESlateVisibility::HitTestInvisible);
        }
    }

    const FString Name=LDHUD::RosterName(Lobby,O.ContenderId);
    HudSetText(SelfName,Name.IsEmpty()?FString(TEXT("名单同步中")):Name);
    if(!SelfName->GetToolTipText().ToString().Equals(SelfName->GetText().ToString(),ESearchCase::CaseSensitive)){SelfName->SetToolTipText(SelfName->GetText());}
    const FString IdentityGlyph=Name.IsEmpty()?FString(TEXT("?")):Name.Left(1);
    HudSetText(PortraitGlyph,IdentityGlyph);HudSetText(BodyPortraitGlyph,IdentityGlyph);
    if(const auto* Body=Cast<ALDCharacter>(PC->GetPawn()))
    {
        const FLinearColor Identity=LDHUD::PaletteColor(Body->PresentationKey);
        if(Portrait->GetBrushColor()!=Identity){Portrait->SetBrushColor(Identity);}
        if(BodyPortrait->GetBrushColor()!=Identity){BodyPortrait->SetBrushColor(Identity);}
    }
    // Plain alive needs no line; protection, death and the other phases keep theirs.
    const bool bPlainAlive=bAlive&&(O.ProtectedUntilMs<=0||(bClock&&O.ProtectedUntilMs<=NowMs));
    HudSetText(LifeText,LDHUD::LifeStatus(O,bClock,NowMs));HudSetShown(LifeText,!bPlainAlive,ESlateVisibility::HitTestInvisible);
    // The protection countdown shows tenths.
    bReadModelLive|=!bPlainAlive;
    HudSetColor(LifeText,bAlive?LDHUDStyle::Shield:O.LifePhase==ELDLifePhase::Dead?LDHUDStyle::TextDanger:LDHUDStyle::TextSecondary);
    // Owner projections of this match and life only; anything older reads as syncing, never as zero.
    const FLDProgressionView* Progression=PC->ReadOwnProgression();
    LDHUD::FResourceView Health,Momentum;double ShieldPoints=0;
    if(OwnCombat&&OwnCombat->bHasBody)
    {
        Health=LDHUD::ResourceP2(LDTerms::Health,OwnCombat->HPP2,OwnCombat->MaxHPP2,true);
        Momentum=LDHUD::ResourceP2(LDTerms::Momentum,OwnCombat->MomentumP2,OwnCombat->MaxMomentumP2,false);
        ShieldPoints=double(OwnCombat->ShieldP2)/100.;
    }
    else
    {
        Health=LDHUD::Resource(LDTerms::Health,O.Hp,O.MaxHp,true);
        Momentum=LDHUD::ResourceP2(LDTerms::Momentum,0,0,false);
        ShieldPoints=O.Barrier;
    }
    FString HealthLine=Health.Text;
    if(Health.bKnown&&ShieldPoints>0){HealthLine+=FString::Printf(TEXT(" · %s %.0f"),LDTerms::Shield,FMath::CeilToDouble(ShieldPoints));}
    HealthBar->SetPercent(Health.Fraction);HudSetText(HealthText,HealthLine);
    // A bar at the own overdrive threshold (OVERDRIVE.Threshold, or Y11's lower one) glows overdrive gold and says so; the bar
    // has no rate text (momentum is no steady regeneration).
    const bool bFullBar=Momentum.bKnown&&OwnCombat&&OwnCombat->OverdriveThresholdP2>0&&OwnCombat->MomentumP2>=OwnCombat->OverdriveThresholdP2;
    MomentumBar->SetPercent(Momentum.Fraction);MomentumBar->SetFillColorAndOpacity(bFullBar?LDHUDStyle::Overdrive:LDHUDStyle::Momentum);
    HudSetText(MomentumText,bFullBar?Momentum.Text+TEXT(" · ")+LDTerms::Overdrive:Momentum.Text);
    HudSetText(HealthRegenText,LDHUD::RegenText(O,OwnCombat,true));
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
        const LDHUD::FSelfIconsView Icons=LDHUD::SelfIcons(OwnCombat,bClock,NowMs,Content);
        SelfIconViews=Icons.Items;
        // The own statuses are painted over the own overhead bars (ULDWorldLabels); the HUD keeps their textures alive and
        // their remaining-time spans. Hidden with the band while the workshop covers it.
        OwnStatusMarks.Reset();
        OwnStatusTextures.SetNum(Icons.Items.Num());
        OwnStatusBrushes.SetNum(Icons.Items.Num());
        TArray<FName> LiveRings;
        for(int32 Index=0;Index<Icons.Items.Num();++Index)
        {
            const LDHUD::FSelfIconView& Icon=Icons.Items[Index];
            UTexture2D* Picture=Icon.IconId.IsEmpty()?nullptr:LDUIIcons::Find(Icon.IconId);
            if(OwnStatusTextures[Index]!=Picture)
            {
                OwnStatusTextures[Index]=Picture;
                FSlateBrush& Brush=OwnStatusBrushes[Index];Brush=FSlateBrush();
                if(Picture){Brush.SetResourceObject(Picture);Brush.ImageSize=FVector2D(64,64);Brush.DrawAs=ESlateBrushDrawType::Image;}
            }
            FLDOwnStatusMark& Mark=OwnStatusMarks.AddDefaulted_GetRef();
            Mark.Glyph=Icon.Glyph;Mark.bPositive=Icon.bPositive;Mark.Stacks=Icon.Stacks;Mark.ViewIndex=Index;
            // The remaining-time bar needs the matching authority clock; without it the mark shows no remaining time.
            if(Icon.ExpiresAtMs>0&&bClock){Mark.Remaining=SelfIconRings.Fraction(Icon.Key,Icon.ExpiresAtMs,NowMs);LiveRings.Add(Icon.Key);}
        }
        SelfIconRings.Retain(LiveRings);
        bReadModelLive|=!LiveRings.IsEmpty();
        const bool bShown=Band&&Band->IsVisible();
        if(!bShown){OwnStatusMarks.Reset();}
        OwnStatusNotice=LDHUD::SelfIconsNotice(Icons,bShown);
    }
    {
        // Unspent points ride on the portrait; the stat icons under the name spend them.
        const int64 Unspent=Progression&&Progression->bValid&&Progression->Generation==O.Match?FMath::Max<int64>(0,Progression->UnspentPoints):0;
        HudSetText(PointsBadgeText,FString::Printf(TEXT("%lld"),Unspent));
        HudSetShown(PointsBadge,Unspent>0,ESlateVisibility::Visible);
    }
    // Spec §6.5: every chain end of the own view reaches the local codex once; a key it had not seen shows one line.
    if(OwnCombat&&!OwnCombat->Chains.IsEmpty())
    {
        if(ChainMatch!=O.Match){ChainMatch=O.Match;ChainSerial=0;}
        TArray<FLDChainCodexEntry> Codex;
        bool bLoaded=false,bChanged=false;
        for(const FLDChainEndView& End:OwnCombat->Chains)
        {
            if(End.Serial<=ChainSerial){continue;}
            ChainSerial=End.Serial;
            if(!bLoaded){Codex=LDChainCodex::Load();bLoaded=true;}
            bChanged=true;
            if(LDChainCodex::Record(Codex,LDChain::Text(End.Key,TEXT(">")),End.Steps,FDateTime::UtcNow().ToUnixTimestamp()))
            {
                ShowToast(LDHUD::NewChainNotice(DisplayDefinitions(),End.Key));
            }
        }
        if(bChanged&&!LDChainCodex::Save(Codex)){UE_LOG(LogLDUI,Warning,TEXT("LD_UI_CHAIN_CODEX_SAVE_FAILED entries=%d"),Codex.Num());}
    }

    // New world commands follow Combat's input half (menu/Settings, child confirmation or text edit, held keys);
    // Combat itself refuses and cancels any armed preview while input is captured.
    const bool bInputSuppressed=PC->IsWorldInputSuppressed();
    const FKey DetailsKey=PC->BindingKey(TEXT("Details"));
    const bool bDetailsHeld=bOwnMatch&&Playing&&DetailsKey.IsValid()&&PC->IsInputKeyDown(DetailsKey)&&!PC->IsWorldInputCaptured();
    if(StatBar){StatBar->SetDetailsHeld(bDetailsHeld);}
    if(Storage){Storage->SetDetailsHeld(bDetailsHeld);}

    if(ChoicesPanel)
    {
        // #147: the cards show this level's numbers; while no projection is published (dead) the last stats stay in use.
        if(OwnCombat&&!OwnCombat->Attributes.IsEmpty()){ChoiceStats=LDHUD::CardStatsOf(Progression?Progression->Level:1,OwnCombat->Attributes);}
        else if(Progression){ChoiceStats.Level=FMath::Max<uint8>(Progression->Level,1);}
        // Holding Alt spells out the scaling on every card face, the workshop's as well as the skill bar's.
        ChoiceStats.bShowScalingFormula=FSlateApplication::IsInitialized()&&FSlateApplication::Get().GetModifierKeys().IsAltDown();
        // #198: the build stage of the public match (the same rule the server and the AI read), never the phase alone.
        const LDProgression::EMatchStage Stage=bOwnMatch?LDProgression::StageForPublic(Public.Phase,Public.Timeline):LDProgression::EMatchStage::Closed;
        // The workshop's places from its last layout; in the field with the workshop closed the pending cards stand as quick
        // picks (icons only inside the engagement window, LDCombat's BuildSafeAtMs).
        if(Workshop){ChoicesPanel->SetWorkshopPlaces(Workshop->Places());}
        ChoicesPanel->SetQuickPick(bOwnMatch&&bFieldPhase&&!bDeathWait,bEngaged);
        if(Storage){Storage->SetEngaged(bEngaged);}
        ChoicesPanel->Present(Progression?*Progression:FLDProgressionView(),O.Match,O.Life,O.LifePhase,Stage,Content,ChoiceStats,HudSlotKeys(*PC),
            OwnCombat&&OwnCombat->bDormRegenActive);
        if(Storage){Storage->SetWarnings(ChoicesPanel->ConfirmWarnings());}
    }
    if(StatBar)
    {
        const LDProgression::EMatchStage Stage=bOwnMatch?LDProgression::StageForPublic(Public.Phase,Public.Timeline):LDProgression::EMatchStage::Closed;
        StatBar->Present(Progression,O.Match,O.Life,Stage,O.LifePhase,Content,OwnCombat?OwnCombat->Attributes:TArray<FLDCombatAttributeRowView>(),
            bOwnMatch&&Progression!=nullptr);
    }
    if(Storage)
    {
        // The storage columns follow the actual body and action slots, including the local Q/W/E slot order.
        ULDComponentStorage::FAnchors Anchors;
        const auto RectOf=[](const UWidget* Widget){return Widget&&Widget->IsVisible()?Widget->GetCachedGeometry().GetLayoutBoundingRect():FSlateRect();};
        Anchors.Band=RectOf(Band);
        Anchors.Corner=RectOf(PromptFrame);
        Anchors.bValid=Anchors.Band.Right>Anchors.Band.Left;
        Anchors.Columns[int32(LDHUD::EStorageColumn::Body)]=RectOf(BodySlot);
        for(int32 Index=0;Index<FMath::Min(3,ActionSlots.Num());++Index)
        {
            const int32 ServerSlot=HudServerSlot(PC->SkillAtKey[Index]);
            if(ServerSlot>=0){Anchors.Columns[int32(LDHUD::SlotColumn(uint8(ServerSlot)))]=RectOf(ActionSlots[Index].Box);}
        }
        if(ActionSlots.Num()>3){Anchors.Columns[int32(LDHUD::EStorageColumn::Dash)]=RectOf(ActionSlots[3].Box);}
        const LDProgression::EMatchStage Stage=bOwnMatch?LDProgression::StageForPublic(Public.Phase,Public.Timeline):LDProgression::EMatchStage::Closed;
        if(Workshop)
        {
            ULDWorkshop::FInput In;
            In.Mode=!bWorkshopOpen?ULDWorkshop::EMode::Closed:bWorkshopFull?ULDWorkshop::EMode::Full:ULDWorkshop::EMode::Field;
            In.Progression=Progression;In.Match=O.Match;In.Content=Content;In.ContentSnapshot=DisplayContent.Get();In.Combat=OwnCombat;
            In.Stage=Stage;In.bFinalField=Public.Timeline.Stage==ELDMatchStage::FinalField;
            In.Stats=ChoiceStats;In.Keys=HudSlotKeys(*PC);
            In.DashKey=PC->BindingLabel(TEXT("Dash"));
            In.HolderName=Name;In.HolderColor=Portrait?Portrait->GetBrushColor():FLinearColor::Gray;In.Level=Level;
            In.Attributes=OwnCombat?OwnCombat->Attributes:TArray<FLDCombatAttributeRowView>();
            In.bRest=bRestPhase;In.RestIndex=Public.Timeline.RestIndex;In.Step=Public.Timeline.Step;
            In.StepStartedAtMs=Public.Timeline.StepStartedAtMs;In.StepEndsAtMs=Public.Timeline.StepEndsAtMs;In.bClock=bClock;In.NowMs=NowMs;
            In.bDead=bDeathWait&&!bRestPhase;In.DeathTitle=Death.Title;In.DeathCountdown=Death.Countdown;
            In.DeathClock=Death.Clock;In.DeathProgress=Death.Progress;
            if(In.bDead){In.DeathRecap=LDHUD::DeathRecap(PC->DeathRecap,O,Lobby,Content);}
            In.TrayDesired=ChoicesPanel?ChoicesPanel->TrayDesiredSize():FVector2D::ZeroVector;
            const LDHUD::FGrowthPending Waiting=LDHUD::GrowthPending(Progression,O.Match,Content);
            In.Pending=(Waiting.bOffer?1:0)+Waiting.Blueprints+(Waiting.bCardOffer?1:0)+(Waiting.bFinalMod?1:0);
            In.bLowQuality=Scalability::GetQualityLevels().PostProcessQuality<=0;
            In.bStage=ChoicesPanel&&ChoicesPanel->IsStageShown();In.bFinalStage=ChoicesPanel&&ChoicesPanel->IsFinalStage();
            In.bServices=ChoicesPanel&&ChoicesPanel->IsServiceShown();
            In.bShop=ChoicesPanel&&ChoicesPanel->IsShopShown();
            In.bCharacterServices=In.bServices;
            Workshop->Present(In);
            Storage->SetWorkshop(Workshop->StorageLayout());
        }
        {
            // What each installed card did so far: its damage, else how often it triggered (the snapshot of the last rest).
            TMap<FName,FString> Did;
            const FLDBuildStatsView& BuildStats=PC->BuildStats;
            if(BuildStats.Match==O.Match)
            {
                for(const FLDCardTriggerRow& Row:BuildStats.Triggers){Did.Add(LDHUD::CardDefinition(Row.Card),FString::Printf(TEXT("×%d"),Row.Count));}
                for(const FLDDamageLine& Line:BuildStats.Lines)
                {
                    if(Line.DamageP2>=100&&Line.Source.DefinitionId.ToString().StartsWith(TEXT("card_")))
                    {Did.Add(Line.Source.DefinitionId,FString::Printf(TEXT("%lld"),Line.DamageP2/100));}
                }
            }
            Storage->SetContributions(BuildStats.Match==O.Match?BuildStats.Serial:0,Did);
        }
        Storage->Present(Progression,O.Match,Stage,O.LifePhase,Content,ChoiceStats,Anchors,bOwnMatch&&Progression!=nullptr);
    }
    if(ChoicesPanel&&PC->RootUI&&bOwnMatch)
    {
        // The own death wait outside a rest opens the workshop by itself (once per death); a rest never does, the 构筑 entry
        // lights instead. Leaving that full-screen moment closes a workshop that opened by itself again unless a request is
        // still on its way. One the player opened stays, at the field size.
        const bool bNewDeath=bDeathWait&&!bRestPhase&&!bWorkshopSeenDead;
        if(bNewDeath){AutoOpenSince=GetWorld()->GetRealTimeSeconds();}
        if(!ChoicesPanel->IsTrayOpen()){bWorkshopAutoOpened=false;}
        if(bWorkshopWasFull&&!bWorkshopFull&&bWorkshopAutoOpened&&ChoicesPanel->IsTrayOpen()&&!ChoicesPanel->HasPendingRequest()&&!(Storage&&Storage->HasPendingRequest()))
        {PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Choices);bWorkshopAutoOpened=false;}
        bWorkshopSeenDead=bDeathWait&&!bRestPhase;bWorkshopWasFull=bWorkshopFull;
    }
    // D10/APP_SPEC §4.2: the growth panel opens by itself only for the match's opening offer, and only when nothing else is
    // open; later offers and new points light the 构筑 entry instead. It never opens over the player's own UI
    // or armed preview, and nothing is chosen for the player. The root may still be leaving the lobby on the match's first
    // frames, so a refused open is retried for a short while; the player's own panel or captured input ends the chance at once.
    if(ChoicesPanel&&PC->RootUI&&Progression)
    {
        const bool bNewAutoOpen=GrowthAutoOpen.Observe(*Progression,O.Match);
        if(bNewAutoOpen&&!bFieldPhase){AutoOpenSince=GetWorld()->GetRealTimeSeconds();}
    }
    {
        // #198 G: a field reward (天降外卖, 画饼老板, a high or elite camp marker) tells what it gave once, as a toast.
        TArray<FString> Lines;
        for(const FLDFieldRewardView& Reward:FieldRewardWatch.Observe(Progression,O.Match))
        {
            const FString Line=LDHUD::FieldRewardNotice(Reward,Content);
            if(!Line.IsEmpty()){Lines.Add(Line);}
        }
        // An element medal that dropped (a level-up, a camp, an event or the Boss) is named once too.
        Lines.Append(MedalDropWatch.Observe(Progression,O.Match,Content));
        if(!Lines.IsEmpty()){ShowToast(FString::Join(Lines,TEXT("\n")));}
    }
    if(ChoicesPanel&&PC->RootUI&&AutoOpenSince>=0)
    {
        if(bFieldPhase&&!bDeathWait){AutoOpenSince=-1;}
        else
        {
            const bool bPlayerUI=PC->RootUI->ActiveMainPanel()!=ELDUIMainPanel::None||PC->IsWorldInputCaptured();
            const double UiTime=GetWorld()->GetRealTimeSeconds();
            if(ChoicesPanel->IsPanelOpen()||bPlayerUI){AutoOpenSince=-1;}
            else if(PC->RootUI->ShowMainPanel(ELDUIMainPanel::Choices))
            {
                // Only a field opening folds by itself; the full workshop stays until its moment ends or the player closes it.
                AutoOpenSince=-1;bWorkshopAutoOpened=true;if(!bWorkshopFull){ChoicesPanel->MarkOpenedAutomatically();}
            }
            else if(UiTime-AutoOpenSince>5.)
            {
                AutoOpenSince=-1;
                UE_LOG(LogLDUI,Warning,TEXT("LD_HUD_GROWTH_AUTO_OPEN_FAILED The growth panel could not open within 5 s."));
            }
        }
    }
    if(ChoicesPanel)
    {
        // #222: only an automatic opening folds on an own hit; a panel the player opened stays through combat.
        ChoicesPanel->UpdateFold(bOwnHit);
    }
    if(bOwnHit&&Workshop){Workshop->InterruptFinalFeedback();}
    // The workspace entry stays above the band's left block; workspace layout is viewport-bounded.
    const float BandTop=12.f+float(Band->GetDesiredSize().Y)*HudBandScale;
    // The tray stands right above the 构筑 button in the corner, or in the workshop's pending area.
    if(ChoicesPanel&&Workshop&&Workshop->IsOpen()){const FVector2D Inset=Workshop->TrayInset();ChoicesPanel->SetTrayInset(Inset.X,Inset.Y,Workshop->TrayWrapWidth(),Workshop->TrayHeightLimit());}
    else if(ChoicesPanel&&PromptFrame)
    {
        // The storage stack stands between the button and the tray.
        const float Stack=Storage?Storage->StackHeight():0.f;
        ChoicesPanel->SetTrayInset(HudCornerInset,HudCornerInset+FMath::RoundToFloat(float(PromptFrame->GetDesiredSize().Y)+(Stack>0.f?Stack+12.f:0.f))+8.f);
    }
    if(TopCentreSlot)
    {
        // The full workshop keeps the rest actions in its top-right corner, beside its own close button.
        const bool bCorner=bWorkshopOpen&&bWorkshopFull&&Workshop;
        // In the full workshop the actions stand bare on the top bar, as buttons of their own; elsewhere they sit on a panel.
        if(RestActionsPanel)
        {
            if(!bRestPanelBrushKept){RestPanelBrush=RestActionsPanel->Background;bRestPanelBrushKept=true;}
            const bool bBare=RestActionsPanel->Background.DrawAs==ESlateBrushDrawType::NoDrawType;
            if(bBare!=bCorner)
            {
                if(bCorner){RestActionsPanel->SetBrush(FSlateNoResource());}else{RestActionsPanel->SetBrush(RestPanelBrush);}
                RestActionsPanel->SetPadding(bCorner?FMargin(0):FMargin(12,8));
            }
        }
        const FAnchors Anchor=bCorner?FAnchors(1,0):FAnchors(.5f,0);const FVector2D Align=bCorner?FVector2D(1,0):FVector2D(.5f,0);
        const FVector2D Where=bCorner?FVector2D(float(Workshop->ActionsAnchor().X)-float(GetCachedGeometry().GetLocalSize().X),Workshop->ActionsAnchor().Y):FVector2D(0,22);
        if(!(TopCentreSlot->GetAnchors()==Anchor)){TopCentreSlot->SetAnchors(Anchor);}
        if(!TopCentreSlot->GetAlignment().Equals(Align)){TopCentreSlot->SetAlignment(Align);}
        if(!TopCentreSlot->GetPosition().Equals(Where,.5f)){TopCentreSlot->SetPosition(Where);}
    }
    if(bWorkshopOpen)
    {
        // Covered by the workshop: the corner widgets it replaces.
        HudSetShown(EventFeedPanel,false,ESlateVisibility::HitTestInvisible);HudSetShown(TargetCard,false,ESlateVisibility::Visible);
        if(bWorkshopFull){HudSetShown(DeathBanner,false,ESlateVisibility::HitTestInvisible);}
    }
    if(DevTestPanel)
    {
        // Only a DEV test room offers the panel; the Host alone sees the match-wide actions, which the server checks again.
        if(!Lobby.DevTestSession&&DevTestPanel->IsPanelOpen()){DevTestPanel->ClosePanel();}
        if(Lobby.DevTestSession)
        {
            DevTestPanel->Present(Progression,OwnCombat,Content,PC->IsLocalController()&&PC->HasAuthority());
        }
    }
    if(ULDCommandButton* Dev=Commands.FindRef(TEXT("DevTest"))){HudSetShown(Dev,Lobby.DevTestSession,ESlateVisibility::Visible);}
    const LDHUD::FGrowthPending Pending=LDHUD::GrowthPending(Progression,O.Match,Content);
    // Lit only while the tray has cards; with nothing to choose the button is greyed out and the tray folds away (a request
    // still in flight keeps it until its answer).
    const bool bPromptLit=bOwnMatch&&Pending.Any();
    // The entry names the view it switches to.
    if(ULDCommandButton* Sheet=Commands.FindRef(TEXT("Attributes")))
    {
        const TCHAR* Next=StatBar&&StatBar->ShowsAttributes()?TEXT("加点"):TEXT("属性");
        HudSetText(CastChecked<UTextBlock>(Sheet->GetContent()),FString(Next)+TEXT(" [")+PC->BindingLabel(TEXT("Attributes"))+TEXT("]"));
    }
    // New opportunities or component choices pulse the workspace entry briefly; owned components wait in the storage and
    // unspent points on the portrait badge.
    if(Pending.Offers>PromptOffers||Pending.CardOffers>PromptCardOffers){PromptPulseAt=UiNow;}
    PromptOffers=Pending.Offers;PromptCardOffers=Pending.CardOffers;
    HudSetColor(PromptText,bPromptLit?HudPromptInk:HudPromptIdleInk);
    {
        // Skills (the active offer and the blueprints), build cards, then the final mod; shown badges line up leftward.
        const int32 Waiting[3]={(Pending.bOffer?1:0)+Pending.Blueprints,Pending.bCardOffer?1:0,Pending.bFinalMod?1:0};
        float Offset=0.f;
        for(int32 Kind=0;Kind<PromptBadges.Num()&&Kind<3;++Kind)
        {
            const bool bShown=Waiting[Kind]>0&&PromptFrame->IsVisible();
            HudSetText(PromptBadgeTexts[Kind],FString::FromInt(Waiting[Kind]));
            HudSetShown(PromptBadges[Kind],bShown,ESlateVisibility::HitTestInvisible);
            if(!bShown){continue;}
            const FVector2D Corner(-HudCornerInset-4.f-Offset,-HudCornerInset-float(PromptFrame->GetDesiredSize().Y)+4.f);
            if(PromptBadgeSlots[Kind]&&!PromptBadgeSlots[Kind]->GetPosition().Equals(Corner,.5f)){PromptBadgeSlots[Kind]->SetPosition(Corner);}
            Offset+=float(PromptBadges[Kind]->GetDesiredSize().X)+3.f;
        }
    }
    // The button always opens the workshop; it is lit while something is pending.
    if(PromptButton->GetIsEnabled()!=bOwnMatch){PromptButton->SetIsEnabled(bOwnMatch);}
    {
        const double PulseAge=UiNow-PromptPulseAt;
        const float Wave=Pending.Any()&&!bEngaged&&PromptPulseAt>=0&&PulseAge<LDHUDStyle::PulseSeconds?LDTween::PingPong(PulseAge,1.):0.f;
        bReadModelLive|=PromptPulseAt>=0&&PulseAge<LDHUDStyle::PulseSeconds;
        LDTween::SetScale(PendingPrompt,1.f+.05f*Wave);LDTween::SetOpacity(PendingPrompt,1.f-.25f*Wave);
    }

    ELDCombatEntry ArmedEntry=ELDCombatEntry::None;const bool bArmed=PC->ReadArmedCombatEntry(ArmedEntry);
    // An armed aim cancelled since the last sample flashes its slot once.
    const ELDCombatEntry CancelledEntry=PC->GetArmedCancelSerial()!=SeenArmedCancel?PC->GetArmedCancelEntry():ELDCombatEntry::None;
    SeenArmedCancel=PC->GetArmedCancelSerial();
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
            {if((Pressed<3?PC->SkillAtKey[Pressed]:HudActionEntries[Pressed])==Receipt.Entry){ActionSlots[Pressed].RejectedAt=UiNow;}}
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
        // Q/W/E positions show the entries the local slot order puts there (#97 drag to swap).
        const ELDCombatEntry Wanted=Index<3?PC->SkillAtKey[Index]:HudActionEntries[Index];
        LDHUD::FActionInput Input;Input.Slot=SlotKind;
        if(const FLDCombatEntryView* Source=OwnCombat?OwnCombat->Entries.FindByPredicate([Wanted](const FLDCombatEntryView& Candidate){return Candidate.Entry==Wanted;}):nullptr)
        {
            Input=LDHUD::ActionInput(*Source,SlotKind,Content);Input.bControlled=(ControlBits&HudActionControl[Index])!=0;
        }
        LDHUD::FActionContext Context;Context.KeyLabel=PC->BindingLabel(HudActionBindings[Index]);
        Context.bOwnContext=bOwnMatch&&O.Life!=0;
        Context.bPlaying=Playing;Context.bAlive=bAlive;Context.bClock=bClock;Context.NowMs=NowMs;
        Context.bInputCaptured=bInputSuppressed;
        Context.bMomentumKnown=OwnCombat&&OwnCombat->bHasBody&&OwnCombat->MaxMomentumP2>0;Context.MomentumP2=OwnCombat?OwnCombat->MomentumP2:0;
        Context.Card=LDHUD::CardStatsOf(Progression?Progression->Level:1,OwnCombat?OwnCombat->Attributes:TArray<FLDCombatAttributeRowView>());
        // Holding Alt spells out each number's scaling (base + AD/AP/HP shares) instead of its current total.
        Context.Card.bShowScalingFormula=FSlateApplication::IsInitialized()&&FSlateApplication::Get().GetModifierKeys().IsAltDown();
        const LDHUD::FActionView View=LDHUD::Action(Input,Context);
        FLDHUDActionSlot& Entry=ActionSlots[Index];
        const LDHUD::EActionLook Look=LDHUD::ActionLook(View,bArmed&&ArmedEntry==Wanted);
        const bool bCooling=View.CooldownRemainingMs>0;
        // A cooldown that runs out on a usable slot within the same life flashes that slot once.
        if(Entry.bCooling&&!bCooling&&Entry.CoolingMatch==O.Match&&Entry.CoolingLife==O.Life&&Look==LDHUD::EActionLook::Ready){Entry.FlashAt=UiNow;}
        Entry.bCooling=bCooling;Entry.CoolingMatch=O.Match;Entry.CoolingLife=O.Life;
        if(CancelledEntry!=ELDCombatEntry::None&&CancelledEntry==Wanted){Entry.CancelledAt=UiNow;}
        const FHudLookStyle Style=HudLookStyle(Look);
        const EHudIconKind IconKind=(View.ExecutionBlocks&LDHUD::ActionEmpty)?EHudIconKind::Empty
            :SlotKind==LDHUD::EActionSlot::Dash?EHudIconKind::Dash:EHudIconKind::Active;
        const bool bHover=Entry.Button->IsHovered();
        const bool bRejected=Entry.RejectedAt>=0&&UiNow-Entry.RejectedAt<LDHUDStyle::NoticeSeconds;
        // The cooldown mask and its tenths, the ready flash and the red ring move with time.
        bReadModelLive|=bCooling||bRejected||Entry.FlashAt>=0||Entry.CancelledAt>=0;
        Entry.bHasSource=Index<3&&Input.bHasSource;
        const int32 LookKey=((int32(Look)*4+int32(IconKind))*2+(bHover?1:0))*2+(bRejected?1:0);
        if(Entry.AppliedLook!=LookKey)
        {
            // Hovering brightens the edge and a refused press turns the ring red; the fill and the icon keep naming the state.
            Entry.AppliedLook=LookKey;
            const FLinearColor Edge=bRejected?LDHUDStyle::TextDanger:bHover?Style.Edge+(FLinearColor::White-Style.Edge)*.4f:Style.Edge;
            const float EdgeWidth=bRejected?3.f:bHover?FMath::Max(Style.EdgeWidth,2.f):Style.EdgeWidth;
            Entry.Frame->SetBrush(HudRound(8.f,Edge,EdgeWidth));Entry.Frame->SetBrushColor(bRejected?FMath::Lerp(Style.Fill,LDHUDStyle::TextDanger,.5f):Style.Fill);
            Entry.Icon->SetBrushColor(HudIconColor(IconKind,Look));
            HudSetColor(Entry.Glyph,Look==LDHUD::EActionLook::Blocked||Look==LDHUD::EActionLook::Empty?LDHUDStyle::TextSecondary:LDHUDStyle::TextPrimary);
        }
        HudSetText(Entry.Key,View.Key);HudSetText(Entry.Glyph,View.Glyph);
        {
            const int32 Tint=int32(HudIconTintOf(Look));
            if(Entry.AppliedIcon!=View.IconId||Entry.AppliedTint!=Tint)
            {
                Entry.AppliedIcon=View.IconId;Entry.AppliedTint=Tint;
                const bool bPicture=LDUIIcons::Apply(Entry.Picture,View.IconId,6.f,HudIconTint(EHudIconTint(Tint)));
                HudSetShown(Entry.Glyph,!bPicture,ESlateVisibility::HitTestInvisible);
            }
        }
        HudSetText(Entry.Cost,View.Cost);HudSetShown(Entry.CostChip,!View.Cost.IsEmpty(),ESlateVisibility::HitTestInvisible);
        // Skill ranks: the dots, the "+" while a point can raise this skill, the momentum gap and the overdrive hold.
        if(Index<3)
        {
            const int32 ProgressionSlot=Wanted==ELDCombatEntry::Q?0:Wanted==ELDCombatEntry::W?1:Wanted==ELDCombatEntry::E?2:INDEX_NONE;
            const FLDActiveSlotView* Learned=Progression&&ProgressionSlot!=INDEX_NONE
                ?Progression->Actives.FindByPredicate([ProgressionSlot](const FLDActiveSlotView& Row){return Row.Slot==ProgressionSlot;}):nullptr;
            const uint8 Rank=Learned?LDSkillBranch::RankOf(Learned->Branch):0;
            HudSetText(Entry.Ranks,Learned?LDHUD::RankDots(Rank):FString());
            HudSetShown(Entry.Ranks,Learned!=nullptr,ESlateVisibility::HitTestInvisible);
            HudSetShown(Entry.Plus,Learned&&Rank<3&&Progression->SkillPoints>0&&bAlive,ESlateVisibility::Visible);
        }
        HudSetText(Entry.Gap,View.Gap);HudSetShown(Entry.Gap,!View.Gap.IsEmpty()&&!bCooling,ESlateVisibility::HitTestInvisible);
        {
            const float Held=Index<3?PC->OverdriveHoldProgress(Wanted):0.f;
            Entry.Hold->SetPercent(Held);HudSetShown(Entry.Hold,Held>0.f,ESlateVisibility::HitTestInvisible);
            bReadModelLive|=Held>0.f;
        }
        Entry.Cooldown->SetPercent(View.CooldownFraction);HudSetShown(Entry.Cooldown,bCooling,ESlateVisibility::HitTestInvisible);
        HudSetText(Entry.Countdown,View.Countdown);HudSetShown(Entry.Countdown,bCooling,ESlateVisibility::HitTestInvisible);
        // An empty slot fades as a whole; the ready flash pops the slot and fades out.
        const float Opacity=Look==LDHUD::EActionLook::Empty?.45f:1.f;
        if(Entry.AppliedOpacity!=Opacity){Entry.AppliedOpacity=Opacity;Entry.Box->SetRenderOpacity(Opacity);}
        const double FlashAge=Entry.FlashAt>=0?UiNow-Entry.FlashAt:LDHUDStyle::ReadyFlashSeconds;
        const bool bFlash=FlashAge<LDHUDStyle::ReadyFlashSeconds;
        if(!bFlash){Entry.FlashAt=-1;}
        // A cancelled aim flashes red and wins over the ready flash; while a skill point can raise this skill and nothing
        // else flashes, the slot's edge keeps glowing until the point is spent.
        const double CancelAge=Entry.CancelledAt>=0?UiNow-Entry.CancelledAt:LDHUDStyle::ReadyFlashSeconds;
        const bool bCancelFlash=CancelAge<LDHUDStyle::ReadyFlashSeconds;
        if(!bCancelFlash){Entry.CancelledAt=-1;}
        const bool bRankGlow=!bFlash&&!bCancelFlash&&Entry.Plus&&Entry.Plus->IsVisible();
        HudSetShown(Entry.Flash,bFlash||bCancelFlash||bRankGlow,ESlateVisibility::HitTestInvisible);
        if(bCancelFlash||bFlash)
        {
            const FLinearColor Tint=bCancelFlash?LDHUDStyle::EnemyHealth:LDHUDStyle::SlotReadyFlash;
            const float Fade=1.f-LDTween::Progress(bCancelFlash?CancelAge:FlashAge,LDHUDStyle::ReadyFlashSeconds,LDTween::ECurve::EaseOut);
            Entry.Flash->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,6.f,Tint.CopyWithNewOpacity(Fade),3.f));
            Entry.Flash->SetBrushColor(Tint.CopyWithNewOpacity(.35f*Fade));
        }
        else if(bRankGlow)
        {
            const float Wave=.35f+.65f*LDTween::PingPong(UiNow,1.2);
            Entry.Flash->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,6.f,LDHUDStyle::TextAccent.CopyWithNewOpacity(Wave),3.f));
            Entry.Flash->SetBrushColor(LDHUDStyle::TextAccent.CopyWithNewOpacity(.12f*Wave));
            bReadModelLive=true;
        }
        const float Pop=bFlash?1.f+(LDHUDStyle::ReadyPopScale-1.f)*LDTween::PingPong(FlashAge,LDHUDStyle::ReadyFlashSeconds):1.f;
        LDTween::SetScale(Entry.Box,Pop);
        Entry.Button->SetIsEnabled(View.bCanSubmitIntent);
        if(DetailIndex==INDEX_NONE&&Entry.Box->GetCachedGeometry().IsUnderLocation(Pointer)){DetailIndex=Index;DetailView=LDHUD::ActionDetail(View,Input,Context,Content);}
    }
    // The basic attack's combo pips: the current step lit (the third gold) until COMBO.ResetSec after its start.
    {
        int32 Step=0;
        if(OwnCombat&&Content&&bClock&&OwnCombat->ComboStep>0&&NowMs-OwnCombat->ComboStartMs<=Content->Parameters.Combo.ResetMs)
        {Step=OwnCombat->ComboStep;bReadModelLive=true;}
        for(int32 Pip=0;Pip<ComboPips.Num();++Pip)
        {
            const bool bLit=Pip+1==Step;
            ComboPips[Pip]->SetBrushColor(!bLit?LDHUDStyle::ComboPipDim:Pip==2?LDHUDStyle::Overdrive:LDHUDStyle::ComboPip);
        }
    }
    RefreshRankUp(Progression,Content,LDHUD::CardStatsOf(Progression?Progression->Level:1,OwnCombat?OwnCombat->Attributes:TArray<FLDCombatAttributeRowView>()));
    // Without a slot under the pointer, a hovered own status mark over the own bars (LDHUD::SelfIconDetail) or stat icon uses
    // the same popup. The world layer reports the marks' last painted rects.
    const UWidget* DetailOver=DetailIndex!=INDEX_NONE?ActionSlots[DetailIndex].Box.Get():nullptr;
    TOptional<FSlateRect> DetailMark;
    if(!DetailOver)
    {
        for(int32 Index=0;Index<FMath::Min(OwnStatusHitRects.Num(),OwnStatusMarks.Num());++Index)
        {
            const int32 View=OwnStatusMarks[Index].ViewIndex;
            if(SelfIconViews.IsValidIndex(View)&&OwnStatusHitRects[Index].ContainsPoint(Pointer))
            {DetailMark=OwnStatusHitRects[Index];DetailView=LDHUD::SelfIconDetail(SelfIconViews[View],Content);break;}
        }
    }
    // The open workshop covers the band: neither its slots, its status marks nor its stat icons pop up.
    if(Workshop&&Workshop->IsOpen()){DetailIndex=INDEX_NONE;DetailOver=nullptr;DetailMark.Reset();}
    // A hovered stat icon: its name, points and per-point text, above its "+".
    else if(!DetailOver&&!DetailMark&&StatBar){DetailOver=StatBar->Hovered(DetailView);}
    // A card of the open 构筑 tray: its full rules (the card shows only the icon and the name).
    const bool bTrayOpen=ChoicesPanel&&(ChoicesPanel->IsTrayOpen()||ChoicesPanel->IsQuickShown());
    if(!DetailOver&&!DetailMark&&bTrayOpen)
    {
        FName SeenComponent;
        DetailOver=ChoicesPanel->Hovered(Pointer,DetailView,&SeenComponent);
        if(Storage&&!SeenComponent.IsNone()){Storage->MarkSeen(SeenComponent);}
        // The workshop's dock shows the card instead of a popup.
        if(Workshop&&Workshop->IsOpen()){DetailOver=nullptr;}
    }
    if((PC->RootUI&&PC->RootUI->ActiveMainPanel()!=ELDUIMainPanel::None&&!(bTrayOpen&&PC->RootUI->ActiveMainPanel()==ELDUIMainPanel::Choices))
        ||(PC->BuildBlueprints&&PC->BuildBlueprints->IsViewerOpen())||(Storage&&Storage->IsDragging())){DetailOver=nullptr;DetailMark.Reset();}
    if(UWidget* Popup=ActionDetailSlot?ActionDetailSlot->Content:nullptr)
    {
        const bool bDetail=DetailOver!=nullptr||DetailMark.IsSet();
        HudSetShown(Popup,bDetail,ESlateVisibility::HitTestInvisible);
        // A mark's popup follows the own body, which moves every frame.
        bReadModelLive|=DetailMark.IsSet();
        if(bDetail)
        {
            HudSetText(ActionDetailName,DetailView.Name);HudSetText(ActionDetailSummary,DetailView.Summary);
            HudSetText(ActionDetailBody,DetailView.Body);HudSetShown(ActionDetailBody,!DetailView.Body.IsEmpty(),ESlateVisibility::HitTestInvisible);
            // Centred 8 units above the slot, icon or mark, read from last frame's layout; neither depends on the popup, so
            // this never feeds back. The popup is only pushed sideways to stay on screen.
            const FGeometry& Area=GetCachedGeometry();
            FVector2D OverTopLeft,OverBottomRight;
            if(DetailOver){const FGeometry& Over=DetailOver->GetCachedGeometry();OverTopLeft=FVector2D(Over.GetAbsolutePosition());OverBottomRight=OverTopLeft+FVector2D(Over.GetAbsoluteSize());}
            else{OverTopLeft=FVector2D(DetailMark->Left,DetailMark->Top);OverBottomRight=FVector2D(DetailMark->Right,DetailMark->Bottom);}
            const FVector2D Top=Area.AbsoluteToLocal(OverTopLeft);
            const FVector2D Size=Area.AbsoluteToLocal(OverBottomRight)-Top;
            const float Half=HudDetailWidth*.5f+8.f;const float Wide=float(Area.GetLocalSize().X);
            const float X=Wide>2*Half?FMath::Clamp(float(Top.X+Size.X*.5),Half,Wide-Half):float(Top.X+Size.X*.5);
            const FVector2D Place(FMath::RoundToFloat(X),FMath::RoundToFloat(float(Top.Y)-8.f));
            if(!ActionDetailSlot->GetPosition().Equals(Place,.5f)){ActionDetailSlot->SetPosition(Place);}
        }
    }

    const ALDWorldViewRelay* Relay=PC->LocalView();
    const FLDObservedFrameRead Frame=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();

    FLDObservedUnitView Subject;const bool bSelected=PC->ReadInspected(Subject);
    HudSetShown(TargetCard,bSelected,ESlateVisibility::Visible);
    if(bSelected)
    {
        const FString SubjectName=Subject.Kind==ELDObservedUnitKind::Contender?LDHUD::ObservedContenderName(Lobby,Subject.ContenderId,Public.Phase==ELDMatchPhase::Rest):FString();
        const LDHUD::FObservedCard Card=LDHUD::ObservedCard(Subject,SubjectName,Content,Frame.Frame?Frame.Frame->SampledAtServerSec:0.);
        const FString Relation=Subject.Kind==ELDObservedUnitKind::Contender?LDHUD::PKRelationText(Public,O.ContenderId,Subject.ContenderId):FString();
        HudSetText(TargetTitle,Relation.IsEmpty()?Card.Title:Card.Title+TEXT(" · ")+Relation);HudSetText(TargetGlyph,Card.Title.Left(1));
        TargetPortrait->SetBrushColor(Subject.Kind==ELDObservedUnitKind::Contender?LDHUD::PaletteColor(Subject.Presentation):ULDHumanoidVisualComponent::MonsterTint(Subject.CreatureId));
        // The bar speaks the shared language: other contenders red, camp members amber (the own body would read green).
        const bool bContender=Subject.Kind==ELDObservedUnitKind::Contender;
        const FLinearColor HealthFill=!bContender?LDHUDStyle::CampHealth:Subject.ContenderId==O.ContenderId?LDHUDStyle::OwnHealth:LDHUDStyle::EnemyHealth;
        if(TargetHealthBar->GetFillColorAndOpacity()!=HealthFill){TargetHealthBar->SetFillColorAndOpacity(HealthFill);}
        TargetHealthBar->SetPercent(Card.Health.Fraction);HudSetText(TargetHealth,Card.Health.Text);
        HudSetShown(TargetMomentumRow,Card.bMomentum,ESlateVisibility::Visible);
        if(Card.bMomentum&&!TargetMomentum->GetText().ToString().Equals(Card.Momentum.Text,ESearchCase::CaseSensitive))
        {TargetMomentumBar->SetPercent(Card.Momentum.Fraction);HudSetText(TargetMomentum,Card.Momentum.Text);TargetMomentumRow->SetToolTipText(FText::FromString(Card.Momentum.Text));}
        else if(Card.bMomentum){TargetMomentumBar->SetPercent(Card.Momentum.Fraction);}
        // A full bar glows gold (a possible overdrive); a held charge pulses it.
        if(Card.bMomentum)
        {
            const float Pulse=Card.bCharging?.55f+.45f*FMath::Abs(FMath::Sin(float(FPlatformTime::Seconds())*9.f)):1.f;
            TargetMomentumBar->SetFillColorAndOpacity(Card.bMomentumFull?LDHUDStyle::Overdrive.CopyWithNewOpacity(Pulse):LDHUDStyle::Momentum);
            bReadModelLive|=Card.bCharging;
        }
        HudSetText(TargetStatuses,Card.Statuses);
        HudSetShown(TargetActivesRow,Card.bActives,ESlateVisibility::SelfHitTestInvisible);
        for(int32 Index=0;Card.bActives&&Index<TargetActiveTiles.Num();++Index)
        {
            const LDHUD::FObservedCard::FActiveChip& Chip=Card.Actives[Index];
            const FString Tip=Chip.Key+TEXT(" · ")+Chip.Name;
            UWidget* Tile=TargetActiveTiles[Index]->GetParent()?TargetActiveTiles[Index]->GetParent()->GetParent():nullptr;
            if(Tile&&!Tile->GetToolTipText().ToString().Equals(Tip,ESearchCase::CaseSensitive)){Tile->SetToolTipText(FText::FromString(Tip));}
            // The first character is only the fallback of a chip without a picture; never drawn under its icon.
            const bool bPicture=LDUIIcons::Apply(TargetActiveIcons[Index],Chip.IconId,6.f);
            HudSetText(TargetActiveNames[Index],Chip.bEmpty||bPicture?FString():Chip.Name.Left(1));
            TargetActiveTiles[Index]->SetBrushColor(FLinearColor(.02f,.03f,.05f,Chip.bEmpty?.45f:.9f));
        }
    }

    // The toast fades on local presentation time.
    if(SeenIntent!=PC->IntentFeedback||SeenFeedback!=PC->FeedbackSerial()){SeenIntent=PC->IntentFeedback;SeenFeedback=PC->FeedbackSerial();ShowToast(PC->IntentFeedback);}
    HudSetText(Toast,UiNow-FeedbackAt<FeedbackDuration?LastFeedback:FString());
    HudSetShown(ToastPanel,!Toast->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    // The toast leaves on time; the opening workshop fades in with Present, and the full one paints its rest step ring from
    // the clock (a long step's ring moves smoothly enough on the clock bucket); a refused automatic opening retries.
    bReadModelLive|=!Toast->GetText().IsEmpty()||AutoOpenSince>=0||(Workshop&&(Workshop->IsOpening()||(Workshop->GetMode()==ULDWorkshop::EMode::Full&&bRestPhase
        &&LDHUD::ReadModelRingMoves(Public.Timeline.StepStartedAtMs,Public.Timeline.StepEndsAtMs))));
    if(ToastSlot)
    {
        // Above the band; over the full workshop at the top centre; above the bottom workshop's top edge.
        const bool bTop=Workshop&&Workshop->GetMode()==ULDWorkshop::EMode::Full;
        const bool bField=Workshop&&Workshop->GetMode()==ULDWorkshop::EMode::Field;
        const FAnchors Anchor=bTop?FAnchors(.5f,0):FAnchors(.5f,1);const FVector2D Align=bTop?FVector2D(.5f,0):FVector2D(.5f,1);
        const float Height=float(GetCachedGeometry().GetLocalSize().Y);
        const FVector2D Above=bTop?FVector2D(0,36):FVector2D(0,-FMath::RoundToFloat(bField?Height*.46f+8.f:BandTop+8.f));
        if(!(ToastSlot->GetAnchors()==Anchor)){ToastSlot->SetAnchors(Anchor);}
        if(!ToastSlot->GetAlignment().Equals(Align)){ToastSlot->SetAlignment(Align);}
        if(!ToastSlot->GetPosition().Equals(Above,.5f)){ToastSlot->SetPosition(Above);}
    }
}
void ULDHUD::OpenRankUp(int32 ProgressionSlot)
{
    RankSlot=ProgressionSlot;
    MarkReadModelDirty();
}

void ULDHUD::RefreshRankUp(const FLDProgressionView* Progression,const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats)
{
    const FLDActiveSlotView* Learned=Progression&&RankSlot!=INDEX_NONE
        ?Progression->Actives.FindByPredicate([this](const FLDActiveSlotView& Row){return Row.Slot==RankSlot;}):nullptr;
    RankOptions=Learned&&Progression->SkillPoints>0?LDHUD::NextBranches(Learned->Branch):TArray<ELDSkillBranch>();
    // A page without a legal next rank (no point left, a forgotten skill, rank 3) closes itself.
    if(RankOptions.Num()!=RankCards.Num()){RankSlot=INDEX_NONE;RankOptions.Reset();}
    HudSetShown(RankPage,RankSlot!=INDEX_NONE,ESlateVisibility::Visible);
    if(RankSlot==INDEX_NONE){return;}
    RankAbility=Learned->DefinitionId;
    FString SkillName;
    LDHUD::ContentText(Content,RankAbility.ToString(),false,SkillName);
    HudSetText(RankTitle,FString::Printf(TEXT("%s · %s %u"),*SkillName,LDTerms::SkillRank,uint32(LDSkillBranch::RankOf(RankOptions[0]))));
    for(int32 Card=0;Card<RankCards.Num();++Card)
    {
        const LDHUD::FBranchCardView View=LDHUD::BranchCard(Content,RankAbility,RankOptions[Card],Stats);
        HudSetText(RankCardNames[Card],View.Name);
        HudSetText(RankCardLines[Card],View.Line);
        RankCards[Card]->SetToolTipText(FText::FromString(View.Detail));
        RankCards[Card]->SetIsEnabled(View.bKnown);
        LDUIIcons::Apply(RankCardIcons[Card],View.IconId,8.f,FLinearColor::White);
    }
}

void ULDHUD::SubmitRankUp(int32 Card)
{
    ALDPlayerController* PC=GetOwningPlayer<ALDPlayerController>();
    const FLDProgressionView* Latest=PC?PC->ReadOwnProgression():nullptr;
    if(!PC||!Latest||RankSlot==INDEX_NONE||!RankOptions.IsValidIndex(Card)){return;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::RankUpSkill;
    Command.Slot=uint8(RankSlot);
    Command.Branch=RankOptions[Card];
    Command.ExpectedBuildRevision=Latest->BuildRevision;
    Command.CatalogDigest=Latest->CatalogDigest;
    ELDGrowthReason Reason=ELDGrowthReason::None;
    if(PC->SubmitBuild(Command,Reason)==0){ShowToast(LDHUD::GrowthReasonText(Reason));return;}
    RankSlot=INDEX_NONE;
    MarkReadModelDirty();
}

void ULDHUD::ShowToast(const FString& Text)
{
    MarkReadModelDirty();
    if(Text.IsEmpty()){return;}
    const double Now=GetWorld()?GetWorld()->GetRealTimeSeconds():0;
    if(Text==LastFeedbackBase&&Now-FeedbackAt<=LDHUDStyle::NoticeSeconds)
    {
        LastFeedback=Text+FString::Printf(TEXT("（重复 %d 次）"),++FeedbackRepeatCount);
        return;
    }
    LastFeedbackBase=Text;LastFeedback=Text;FeedbackAt=Now;FeedbackRepeatCount=0;
    FeedbackDuration=Text==TEXT("对局开始。")?LDHUDStyle::NoticeSeconds:5.;
}
// The panel, the storage and the stat bar each keep their own request; each answer reaches the one that sent it.
void ULDHUD::HandleGrowthSubmit(const FLDSubmitResult& Result)
{
    MarkReadModelDirty();
    if(Result.Family!=ELDGrowthFamily::Build){return;}
    if(ChoicesPanel){ChoicesPanel->HandleSubmit(Result);}
    if(Storage){Storage->HandleSubmit(Result);}
    if(StatBar){StatBar->HandleSubmit(Result);}
}
void ULDHUD::HandleBuildReceipt(const FLDBuildReceiptQueryResult& Result)
{
    MarkReadModelDirty();
    if(ChoicesPanel){ChoicesPanel->HandleReceipt(Result);}
    if(Storage){Storage->HandleReceipt(Result);}
    if(StatBar){StatBar->HandleReceipt(Result);}
}
bool ULDHUD::CancelStorage(){MarkReadModelDirty();return Storage&&Storage->CancelInteraction();}
void ULDHUD::SubmitTrainingReset()
{
    MarkReadModelDirty();
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    const auto* GS=GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr;
    if(!PC||!GS||bTrainingResetInFlight||GS->PublicMatch.Generation==0||GS->PublicMatch.Generation!=PC->OwnerState.Match
        ||GS->PublicMatch.Phase!=ELDMatchPhase::Rest){return;}
    bTrainingResetInFlight=true;
    PC->SubmitTrainingReset();
}
void ULDHUD::SubmitRestReady()
{
    MarkReadModelDirty();
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC||bRestReadyInFlight){return;}
    if(!PC->OwnerState.bCanRestReady||PC->OwnerState.bRestReady)
    {ShowRestAnswer(TEXT("当前不能提交准备，未改变准备状态。"));return;}
    bRestReadyInFlight=true;PC->SubmitRestReady(true);
}
void ULDHUD::HandleRestReady(ELDRestReadyResult Result)
{
    MarkReadModelDirty();
    bRestReadyInFlight=false;
    ShowRestAnswer(LDHUD::RestReadyText(Result));
}
void ULDHUD::HandleTrainingReset(ELDTrainingResetResult Result)
{
    MarkReadModelDirty();
    bTrainingResetInFlight=false;
    UE_LOG(LogLDUI,Log,TEXT("LD_HUD_TRAINING_RESET_RESULT %d"),int32(Result));
    ShowRestAnswer(LDHUD::TrainingResetText(Result));
}
void ULDHUD::ShowRestAnswer(const FString& Text)
{
    MarkReadModelDirty();
    const auto* GS=GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr;
    RestAnswer=Text;
    RestAnswerMatch=GS?GS->PublicMatch.Generation:0;
    RestAnswerRest=GS?GS->PublicMatch.Timeline.RestIndex:0;
    ShowToast(Text);
}
void ULDHUD::SubmitSpawnChoice(int32 Index)
{
    MarkReadModelDirty();
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC||bSpawnChoiceInFlight||!PC->OwnerState.Pacing.SpawnCandidates.IsValidIndex(Index)){return;}
    bSpawnChoiceInFlight=true;
    PC->SubmitSpawnChoice(uint8(Index));
}
void ULDHUD::HandleSpawnChoice(ELDSpawnChoiceResult Result)
{
    MarkReadModelDirty();
    bSpawnChoiceInFlight=false;
    UE_LOG(LogLDUI,Log,TEXT("LD_HUD_SPAWN_CHOICE_RESULT %d"),int32(Result));
    if(Result!=ELDSpawnChoiceResult::Chosen){ShowToast(LDHUD::SpawnChoiceText(Result));}
}
bool ULDHUD::KeepRing(TWeakObjectPtr<ALDShowdownRingMarker>& Weak,bool bShow,const FLDPublicMatchView& Public,uint32 Key,const FVector& Centre,const FLinearColor& Tint)
{
    UWorld* World=GetWorld();
    ALDShowdownRingMarker* Ring=Weak.Get();
    if(Ring&&(!bShow||Ring->GetGeneration()!=Public.Generation||Ring->GetKey()!=Key||!Ring->GetCentre().Equals(Centre,1.)))
    {
        Ring->Destroy();
        Weak.Reset();
        Ring=nullptr;
    }
    if(!bShow||!World){return false;}
    if(Ring){return true;}
    // A failed build is reported once per ring, never retried every frame.
    if(RingFailedMatch==Public.Generation&&RingFailedKey==Key){return false;}
    FString Error;
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags|=RF_Transient;
    Ring=World->SpawnActor<ALDShowdownRingMarker>(Spawn);
    if(!Ring||!Ring->Build(Centre,Public.Generation,Key,Tint,Error))
    {
        if(Ring){Ring->Destroy();}
        if(Error.IsEmpty()){Error=TEXT("The ring marker actor could not be spawned.");}
        RingFailedMatch=Public.Generation;RingFailedKey=Key;
        UE_LOG(LogLDUI,Error,TEXT("LD_RING_MARKER_UNAVAILABLE generation=%u key=%u %s"),Public.Generation,Key,*Error);
        return false;
    }
    Weak=Ring;
    return true;
}
void ULDHUD::SyncShowdownRing(const FLDPublicMatchView& Public,bool bOwnMatch,bool bClock,int64 NowMs)
{
    UWorld* World=GetWorld();
    const FLDDemoContentDefinitions* Content=DisplayDefinitions();
    // Demo world presentation of the own match's final street closure; fixture worlds draw none.
    const bool bShow=bOwnMatch&&Content&&World&&!World->bIsTearingDown&&Public.Showdown.bStarted
        &&(Public.Phase==ELDMatchPhase::Development||Public.Phase==ELDMatchPhase::CaptureOpen)
        &&UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==LDDemoEntry::WorldMap;
    if(!KeepRing(ShowdownRing,bShow,Public,MAX_uint32,Public.Showdown.CentreCm,ALDShowdownRingMarker::SafeColor())){return;}
    const int64 At=bClock?NowMs:Public.ServerNowMs;
    const bool bPreview=At>=Public.Showdown.ClosureAtMs-Content->Parameters.ShowdownRingPreviewMs;
    ShowdownRing->Show(LDShowdown::StreetRadiusCm(Public.Showdown,At),bPreview,Public.Showdown.FinalRadiusCm);
}
void ULDHUD::SyncObjectiveRing(const FLDPublicMatchView& Public,bool bOwnMatch)
{
    UWorld* World=GetWorld();
    const FLDObjectiveView& Objective=Public.Objective;
    const bool bLive=Objective.Phase==ELDObjectivePhase::Announced||Objective.Phase==ELDObjectivePhase::Open;
    const bool bShow=bOwnMatch&&bLive&&World&&!World->bIsTearingDown&&Objective.RadiusCm>0.f
        &&(Public.Phase==ELDMatchPhase::Development||Public.Phase==ELDMatchPhase::CaptureOpen)
        &&UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==LDDemoEntry::WorldMap;
    if(!KeepRing(ObjectiveRing,bShow,Public,uint32(Objective.Serial),Objective.CentreCm,ALDShowdownRingMarker::PreviewColor())){return;}
    // Announced: only the preview of the circle; open: the circle itself.
    const bool bOpen=Objective.Phase==ELDObjectivePhase::Open;
    ObjectiveRing->Show(bOpen?Objective.RadiusCm:0.,!bOpen,Objective.RadiusCm);
}
void ULDHUD::SyncSignupMarker(const FLDPublicMatchView& Public,uint32 OwnMatch,bool bShow,bool bWindowOpen,bool bSignedUp)
{
    ALDSignupMarker* Marker=SignupMarker.Get();
    const auto* Body=Cast<ALDCharacter>(GetOwningPlayerPawn());
    const int32 OwnSlot=Body?int32(Body->PresentationKey):INDEX_NONE;
    UWorld* World=GetWorld();
    if(!bShow||!World||!Body||Public.Generation==0||Public.Generation!=OwnMatch)
    {
        if(Marker){Marker->Destroy();SignupMarker.Reset();}
        return;
    }
    if(Marker&&(Marker->GetGeneration()!=Public.Generation||Marker->GetSlot()!=OwnSlot)){Marker->Destroy();SignupMarker.Reset();Marker=nullptr;}
    if(!Marker)
    {
        // A failed build is reported once per match, never retried every frame or replaced by a guessed disk.
        if(SignupMarkerFailedMatch==Public.Generation){return;}
        const ULDMapRegistry* Registry=World->GetSubsystem<ULDMapRegistry>();
        FVector Centre=FVector::ZeroVector;
        const bool bDisk=Registry&&OwnSlot>=0&&OwnSlot<LDDemoWorld::PKSignups&&Registry->SharedSignupCentre(Centre);
        TSharedPtr<const FLDDemoContentSnapshot> Content;
        FString Error;
        const bool bContent=LDDemoEntry::LoadRuntimeContent(Content,Error)&&Content;
        const FLDDemoRational Radius=bContent?Content->Get().Parameters.AirdropPickupRadiusCm:FLDDemoRational();
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Marker=bDisk&&bContent&&Radius.Denominator>0&&Radius.Numerator>0?World->SpawnActor<ALDSignupMarker>(Spawn):nullptr;
        if(!Marker||!Marker->Build(Centre,double(Radius.Numerator)/double(Radius.Denominator),Public.Generation,OwnSlot,Error))
        {
            if(Marker){Marker->Destroy();}
            SignupMarkerFailedMatch=Public.Generation;
            UE_LOG(LogLDUI,Error,TEXT("LD_HUD_SIGNUP_DISK_UNAVAILABLE slot=%d %s"),OwnSlot,
                Error.IsEmpty()?TEXT("the shared sign-up disk is not in the registered world or content"):*Error);
            return;
        }
        SignupMarker=Marker;
    }
    Marker->Show(bWindowOpen,bSignedUp);
}
LDHUD::FPKPanelInput ULDHUD::PKInput(const ALDPlayerController& PC,bool bClock,int64 NowMs)
{
    LDHUD::FPKPanelInput Input;
    Input.Self=PC.OwnerState.ContenderId;Input.bClock=bClock;Input.NowMs=NowMs;
    Input.ThrowReadyAtMs=PKThrowReadyAtMs;Input.bThrowArmed=bPKThrowArmed;Input.bThrowInFlight=bPKThrowInFlight;
    Input.Params=PKParameters();
    return Input;
}
void ULDHUD::RefreshPK(ALDPlayerController& PC,const FLDPublicMatchView& Public,const FLDLobbyView& Lobby,bool bClock,int64 NowMs)
{
    // Every local PK state belongs to one PK; another match or PK starts clean.
    if(PKStateMatch!=Public.Generation||PKStateSerial!=Public.PK.Serial)
    {
        PKStateMatch=Public.Generation;PKStateSerial=Public.PK.Serial;
        PKAnswer.Reset();PKThrowReadyAtMs=0;bPKThrowArmed=false;bPKBetInFlight=false;bPKThrowInFlight=false;
    }
    const bool bOwnMatch=Public.Generation!=0&&Public.Generation==PC.OwnerState.Match;
    const LDHUD::FPKPanelView View=bOwnMatch?LDHUD::PKPanel(Public,Lobby,PKInput(PC,bClock,NowMs)):LDHUD::FPKPanelView();
    // Each button exists only while it applies; leaving that drops a wait the server will still answer.
    if(!View.bCanBet){bPKBetInFlight=false;}
    if(!View.bCanThrow){bPKThrowArmed=false;bPKThrowInFlight=false;}
    const bool bWorkspaceOpen=ChoicesPanel&&ChoicesPanel->IsPanelOpen();
    if(bWorkspaceOpen){bPKThrowArmed=false;}
    HudSetShown(PKPanelBorder,View.bVisible&&!bWorkspaceOpen,ESlateVisibility::Visible);
    // The PK countdowns, the ring line and the throw cooldown move with time.
    bReadModelLive|=View.bVisible;
    if(!View.bVisible||bWorkspaceOpen){return;}
    const auto Line=[](UTextBlock* Text,const FString& Value){HudSetText(Text,Value);HudSetShown(Text,!Value.IsEmpty(),ESlateVisibility::HitTestInvisible);};
    Line(PKTitleText,View.Title);
    Line(PKCountdownText,View.Countdown);
    Line(PKRingText,View.Ring);
    for(int32 Index=0;Index<PKSideTexts.Num();++Index)
    {
        const LDHUD::FPKSideView* Side=View.Sides.IsValidIndex(Index)?&View.Sides[Index]:nullptr;
        Line(PKSideTexts[Index],Side?Side->Line:FString());
        if(Side)
        {
            HudSetColor(PKSideTexts[Index],Side->bWinner?LDHUDStyle::Celebration:Side->bOut?LDHUDStyle::TextSecondary
                :Side->bOwnSide?LDHUDStyle::StatusPositive:LDHUDStyle::TextPrimary);
        }
    }
    Line(PKRoleText,View.Role);
    for(int32 Index=0;Index<PKBetButtons.Num();++Index)
    {
        ULDCommandButton* Bet=PKBetButtons[Index];
        const bool bShown=View.bCanBet&&View.Sides.IsValidIndex(Index);
        HudSetShown(Bet,bShown,ESlateVisibility::Visible);
        if(!bShown){continue;}
        const LDHUD::FPKSideView& Side=View.Sides[Index];
        // The own current bet is named on its button and lit gold; any other side changes it until the step ends.
        HudSetText(CastChecked<UTextBlock>(Bet->GetContent()),Side.bOwnBet?Side.BetAction+TEXT(" · ")+LDTerms::PKOwnBet:Side.BetAction);
        const FLinearColor Tint=Side.bOwnBet?HudPromptFill:FLinearColor::White;
        if(Bet->GetBackgroundColor()!=Tint){Bet->SetBackgroundColor(Tint);}
        if(Bet->GetIsEnabled()==bPKBetInFlight){Bet->SetIsEnabled(!bPKBetInFlight);}
    }
    HudSetShown(PKThrowButton,View.bCanThrow,ESlateVisibility::Visible);
    if(View.bCanThrow)
    {
        HudSetText(CastChecked<UTextBlock>(PKThrowButton->GetContent()),View.ThrowAction);
        // While armed the button stays pressable so a second press cancels the aim.
        const bool bEnabled=View.bThrowReady||bPKThrowArmed;
        if(PKThrowButton->GetIsEnabled()!=bEnabled){PKThrowButton->SetIsEnabled(bEnabled);}
        const FLinearColor Tint=bPKThrowArmed?HudPromptFill:FLinearColor::White;
        if(PKThrowButton->GetBackgroundColor()!=Tint){PKThrowButton->SetBackgroundColor(Tint);}
    }
    Line(PKResultLine,View.bResult?(View.bOwnWin?View.Result+TEXT(" · ")+LDTerms::PKWonSelf:View.Result):FString());
    HudSetShown(PKAwardsTitle,View.bResult,ESlateVisibility::HitTestInvisible);
    HudSetText(PKAwardsTitle,View.bResult&&View.Awards.IsEmpty()?FString(LDTerms::PKAwardsNone):FString(LDTerms::PKAwardsTitle));
    for(int32 Index=0;Index<PKAwardTexts.Num();++Index)
    {
        const LDHUD::FPKAwardRow* Row=View.bResult&&View.Awards.IsValidIndex(Index)?&View.Awards[Index]:nullptr;
        Line(PKAwardTexts[Index],Row?Row->Name+TEXT("：")+Row->Points:FString());
        if(Row){HudSetColor(PKAwardTexts[Index],Row->bSelf?LDHUDStyle::TextAccent:LDHUDStyle::TextPrimary);}
    }

    Line(PKAnswerText,PKAnswer);
}
void ULDHUD::SubmitPKBet(int32 SideIndex)
{
    MarkReadModelDirty();
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    const auto* GS=GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr;
    if(!PC||!GS||bPKBetInFlight||GS->PublicMatch.Generation==0||GS->PublicMatch.Generation!=PC->OwnerState.Match){return;}
    int64 NowMs=0;const bool bClock=PC->ReadDemoAuthorityTimeMs(NowMs);
    // The button reads the same public view it shows; the server answers with the authoritative result.
    const LDHUD::FPKPanelView View=LDHUD::PKPanel(GS->PublicMatch,GS->DemoLobby,PKInput(*PC,bClock,NowMs));
    if(!View.bCanBet||!View.Sides.IsValidIndex(SideIndex)){return;}
    bPKBetInFlight=true;
    PC->SubmitPKBet(View.Sides[SideIndex].Side);
}
void ULDHUD::TogglePKThrowAim()
{
    MarkReadModelDirty();
    if(CancelPKThrowAim()){return;}
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    const auto* GS=GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr;
    if(!PC||!GS||bPKThrowInFlight||GS->PublicMatch.Generation==0||GS->PublicMatch.Generation!=PC->OwnerState.Match){return;}
    int64 NowMs=0;const bool bClock=PC->ReadDemoAuthorityTimeMs(NowMs);
    const LDHUD::FPKPanelView View=LDHUD::PKPanel(GS->PublicMatch,GS->DemoLobby,PKInput(*PC,bClock,NowMs));
    if(!View.bCanThrow||!View.bThrowReady){return;}
    bPKThrowArmed=true;
}
bool ULDHUD::CancelPKThrowAim()
{
    MarkReadModelDirty();
    if(!bPKThrowArmed){return false;}
    bPKThrowArmed=false;
    return true;
}
void ULDHUD::ThrowPKAtPointer()
{
    MarkReadModelDirty();
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    const auto* GS=GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr;
    if(!bPKThrowArmed||!PC||!GS){bPKThrowArmed=false;return;}
    int64 NowMs=0;const bool bClock=PC->ReadDemoAuthorityTimeMs(NowMs);
    const LDHUD::FPKPanelView View=GS->PublicMatch.Generation!=0&&GS->PublicMatch.Generation==PC->OwnerState.Match
        ?LDHUD::PKPanel(GS->PublicMatch,GS->DemoLobby,PKInput(*PC,bClock,NowMs)):LDHUD::FPKPanelView();
    if(!View.bCanThrow||!View.bThrowReady){bPKThrowArmed=false;return;}
    // The same cursor-ground trace a move order uses; a miss keeps the aim so the next click can try again.
    FHitResult Ground;
    if(!PC->Navigation||!PC->Navigation->GroundUnderPointer(Ground)){ShowPKAnswer(LDTerms::PKThrowNoGround);return;}
    bPKThrowArmed=false;
    bPKThrowInFlight=true;
    PC->SubmitPKThrow(FVector(Ground.ImpactPoint));
}
void ULDHUD::HandlePKBet(ELDPKBetResult Result)
{
    MarkReadModelDirty();
    bPKBetInFlight=false;
    UE_LOG(LogLDUI,Log,TEXT("LD_HUD_PK_BET_RESULT %d"),int32(Result));
    ShowPKAnswer(LDHUD::PKBetText(Result));
}
void ULDHUD::HandlePKThrow(ELDPKThrowResult Result,int64 ReadyAtMs)
{
    MarkReadModelDirty();
    bPKThrowInFlight=false;
    // Thrown and CoolingDown carry when the next throw is allowed; other answers leave the known cooldown as it is.
    if(ReadyAtMs>0){PKThrowReadyAtMs=ReadyAtMs;}
    UE_LOG(LogLDUI,Log,TEXT("LD_HUD_PK_THROW_RESULT %d ready_at=%lld"),int32(Result),ReadyAtMs);
    ShowPKAnswer(LDHUD::PKThrowText(Result));
}
void ULDHUD::ShowPKAnswer(const FString& Text)
{
    MarkReadModelDirty();
    PKAnswer=Text;
    ShowToast(Text);
}
const FLDPKParameters* ULDHUD::PKParameters()
{
    const FLDDemoContentDefinitions* Content=DisplayDefinitions();
    if(Content!=PKParamsSource)
    {
        PKParamsSource=Content;
        bPKParamsValid=false;
        FString Error;
        if(Content)
        {
            bPKParamsValid=FLDPKParameters::FromContent(Content->Parameters,PKParams,Error);
            if(!bPKParamsValid){UE_LOG(LogLDUI,Error,TEXT("LD_HUD_PK_PARAMETERS_INVALID %s"),*Error);}
        }
    }
    return bPKParamsValid?&PKParams:nullptr;
}
void ULDHUD::SyncPKRing(const FLDPublicMatchView& Public,bool bOwnMatch,bool bClock,int64 NowMs)
{
    UWorld* World=GetWorld();
    const FLDPKView& PK=Public.PK;
    const ELDRestStep Step=Public.Timeline.Step;
    const bool bFinal=Public.Showdown.bStarted&&(Public.Phase==ELDMatchPhase::Development||Public.Phase==ELDMatchPhase::CaptureOpen);
    const FVector Centre=bFinal?Public.Showdown.CentreCm:PK.ArenaCentreCm;
    const uint32 Serial=bFinal?MAX_uint32:PK.Serial;
    // Demo world presentation of the own match's arena during the PKReady and PK steps; fixture worlds draw none.
    const bool bShow=bOwnMatch&&World&&!World->bIsTearingDown
        &&(bFinal||(LDHUD::HasPK(Public)&&(Step==ELDRestStep::PKReady||Step==ELDRestStep::PK)&&PK.Outcome!=ELDPKOutcome::NoContest))
        &&UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==LDDemoEntry::WorldMap;
    ALDPKRingMarker* Ring=PKRing.Get();
    if(Ring&&(!bShow||Ring->GetGeneration()!=Public.Generation||Ring->GetSerial()!=Serial||!Ring->GetCentre().Equals(Centre,1.)))
    {
        Ring->Destroy();
        PKRing.Reset();
        Ring=nullptr;
    }
    const FLDPKParameters* Params=bShow?PKParameters():nullptr;
    if(!Params){return;}
    if(!Ring)
    {
        if(PKRingFailedMatch==Public.Generation&&PKRingFailedSerial==Serial){return;}
        FString Error;
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags|=RF_Transient;
        Ring=World->SpawnActor<ALDPKRingMarker>(Spawn);
        if(!Ring||!Ring->Build(Centre,Public.Generation,Serial,Error))
        {
            if(Ring){Ring->Destroy();}
            if(Error.IsEmpty()){Error=TEXT("The PK ring marker actor could not be spawned.");}
            PKRingFailedMatch=Public.Generation;PKRingFailedSerial=Serial;
            UE_LOG(LogLDUI,Error,TEXT("LD_PK_RING_MARKER_UNAVAILABLE generation=%u serial=%u %s"),Public.Generation,Serial,*Error);
            return;
        }
        PKRing=Ring;
    }
    if(bFinal)
    {
        const int64 At=bClock?NowMs:Public.ServerNowMs;
        const bool bPreview=At>=Public.Showdown.ClosureAtMs-Params->PreviewMs;
        Ring->Show(LDShowdown::StreetRadiusCm(Public.Showdown,At),bPreview,Public.Showdown.FinalRadiusCm,{},0.);
        return;
    }
    const int64 ElapsedMs=LDHUD::PKRingElapsedMs(PK,bClock,NowMs);
    double PreviewCm=0;int64 PreviewAtMs=0;
    const bool bPreview=LDPKRules::RingPreview(*Params,ElapsedMs,PreviewCm,PreviewAtMs);
    // Each telegraph stays until its landing instant; without the authority clock every listed throw is shown.
    TArray<FVector> Throws;
    if(Step==ELDRestStep::PK){for(const FLDPKThrowView& Throw:PK.Throws){if(!bClock||Throw.LandsAtMs>NowMs){Throws.Add(Throw.TargetCm);}}}
    Ring->Show(LDPKRules::RingRadiusCm(*Params,ElapsedMs),bPreview,PreviewCm,Throws,Params->ThrowRadiusCm);
}
void ULDHUD::NativeDestruct()
{
    if(ALDPKRingMarker* Ring=PKRing.Get()){Ring->Destroy();}
    PKRing.Reset();
    if(ALDSignupMarker* Marker=SignupMarker.Get()){Marker->Destroy();}
    SignupMarker.Reset();
    for(TWeakObjectPtr<ALDShowdownRingMarker>* Weak:{&ShowdownRing,&ObjectiveRing})
    {
        if(ALDShowdownRingMarker* Ring=Weak->Get()){Ring->Destroy();}
        Weak->Reset();
    }
    Super::NativeDestruct();
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
    for(const ELDAudioCue Cue:Cues){Audio->Play(Cue,Cue==ELDAudioCue::SelfAttack?LDHitFeel::Read().SwingVolume:1.f);}
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
        // Every final mod is public in the final field: it follows its holder's name.
        HudSetText(Cells[0],Public.PublicMatch.Showdown.bStarted
            ?LDTerms::HolderWithMod(Line.Name,LDHUD::PublicFinalMod(Public.PublicMatch,Line.ContenderId,DisplayDefinitions())):Line.Name);HudSetText(Cells[1],FString::Printf(TEXT("%llu"),Line.Kills));
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
    Input.OwnFieldRewards=Own.Match==Public.PublicMatch.Generation?&Own.Growth.Progression.FieldRewards:nullptr;
    if(const ALDPlayerController* Owner=Cast<ALDPlayerController>(GetOwningPlayer())){Input.OwnReactionAssists=&Owner->ReactionAssists;}
    Input.OwnLife=Own.Match==Public.PublicMatch.Generation?Own.Life:0;
    Input.Content=Content;
    Input.bClock=bClock;Input.NowMs=NowMs;Input.LocalSeconds=GetWorld()->GetRealTimeSeconds();
    HudCampNames(Content,Input.LowCampName,Input.HighCampName,Input.EliteCampName,Input.BossName);
    EventFeed.Observe(Input);
    if(!EventFeed.AssistPrompt().IsEmpty()){ShowToast(EventFeed.AssistPrompt());}
    const TArray<LDHUD::FEventLine>& Lines=EventFeed.Lines();
    // One row per line, so the lines about the player (said with 你) stand out; each fades out over its last half second.
    for(int32 Row=0;Row<EventFeedRows.Num();++Row)
    {
        UTextBlock* FeedLine=EventFeedRows[Row];const bool bLine=Lines.IsValidIndex(Row);
        HudSetText(FeedLine,bLine?Lines[Row].Text:FString());HudSetColor(FeedLine,bLine&&Lines[Row].bSelf?LDHUDStyle::TextAccent:LDHUDStyle::TextPrimary);
        HudSetShown(FeedLine,bLine,ESlateVisibility::HitTestInvisible);
        const float Opacity=bLine?1.f-LDTween::Progress(Input.LocalSeconds-Lines[Row].At-(LDHUD::FEventFeed::LineSeconds-LDHUDStyle::FeedFadeSeconds),LDHUDStyle::FeedFadeSeconds,LDTween::ECurve::EaseIn):1.f;
        LDTween::SetOpacity(FeedLine,Opacity);
    }
    HudSetShown(EventFeedPanel,!Lines.IsEmpty(),ESlateVisibility::HitTestInvisible);
    // Each line fades out and leaves on time.
    bReadModelLive|=!Lines.IsEmpty();
}
void ULDHUD::RefreshResult(ALDPlayerController& PC,const ALDGameState& Public)
{
    const FLDResultView& Result=Public.MatchResult;
    const bool bReady=Result.Generation==Public.PublicMatch.Generation&&Result.ResultId.IsValid()&&Result.ResultId==Public.PublicMatch.ResultId;
    ReturnButton->SetVisibility(PC.OwnerState.bLobbyHost?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ReturnButton->SetIsEnabled(bReady);
    HudSetText(Cast<UTextBlock>(LeaveRoomButton->GetContent()),
        PC.OwnerState.bLobbyHost?TEXT("关闭房间并返回主菜单"):TEXT("离开房间并返回主菜单"));
    if(!bReady)
    {
        // Without the frozen result there is nothing to show yet: no zero-filled table.
        HudSetText(ResultTitle,TEXT("对局结束"));HudSetText(ResultSubtitle,TEXT("结算同步中…"));
        ResultWinnerBuild->SetVisibility(ESlateVisibility::Collapsed);
        ResultWinnerBuild->ClearChildren();
        if(ShownResultId.IsValid()){ResultRows->ClearChildren();ResultEntries.Reset();ShownResultId.Invalidate();ShownResultSerial=0;}
        ResultShownAt=-1.;
        for(UWidget* Widget:{ResultWinnerBand.Get(),ResultActionsBox.Get()}){LDTween::SetOpacity(Widget,1.f);LDTween::SetScale(Widget,1.f);LDTween::SetTranslation(Widget,FVector2D::ZeroVector);}
        HudSetShown(ResultDamageHeading,false,ESlateVisibility::HitTestInvisible);
        for(UTextBlock* Cell:ResultDamageCells){HudSetShown(Cell,false,ESlateVisibility::HitTestInvisible);}
        HudSetShown(ResultHighlightsCard,false,ESlateVisibility::HitTestInvisible);
        return;
    }
    AnimateResult();
    // The own damage arrives on the owner channel apart from the public result, so it is read every refresh; without
    // the summary of exactly this result the section says so instead of showing zeros.
    {
        const LDHUD::FDamageListView Damage=LDHUD::DamageSummary(PC.DamageSummary,Result,DisplayDefinitions());
        HudSetText(ResultDamageHeading,Damage.bAvailable?Damage.Heading:FString(TEXT("你的伤害统计不可用")));
        HudSetShown(ResultDamageHeading,true,ESlateVisibility::HitTestInvisible);
        for(int32 Index=0;Index<ResultDamageCells.Num();++Index)
        {
            const bool bCell=Damage.bAvailable&&Damage.Rows.IsValidIndex(Index);
            HudSetText(ResultDamageCells[Index],bCell?Damage.Rows[Index]:FString());
            HudSetShown(ResultDamageCells[Index],bCell,ESlateVisibility::HitTestInvisible);
        }
        const LDHUD::FResultHighlightsView Highlights=LDHUD::ResultHighlights(PC.DamageSummary,Result,PC.OwnerState.ContenderId,DisplayDefinitions());
        HudSetShown(ResultHighlightsCard,Highlights.bAvailable,ESlateVisibility::HitTestInvisible);
        for(int32 Index=0;Index<ResultHighlightCells.Num();++Index)
        {
            const bool bRow=Highlights.bAvailable&&Highlights.Rows.IsValidIndex(Index);
            HudSetText(ResultHighlightCells[Index],bRow?FString::Printf(TEXT("%s    %s    %s %s"),*Highlights.Rows[Index].Name,
                *Highlights.Rows[Index].Chain,LDTerms::BestHit,*Highlights.Rows[Index].Best):FString());
            if(bRow){ResultHighlightCells[Index]->SetColorAndOpacity(Highlights.Rows[Index].bOwn?LDHUDStyle::TextAccent:LDHUDStyle::UIText);}
            HudSetShown(ResultHighlightCells[Index],bRow,ESlateVisibility::HitTestInvisible);
        }
    }
    if(ShownResultId==Result.ResultId&&ShownResultSerial==Result.ResultSerial){return;}
    ShownResultId=Result.ResultId;ShownResultSerial=Result.ResultSerial;
    const FLDResultRow* WinnerRow=Result.Winner.IsValid()?Result.Rows.FindByPredicate([&Result](const FLDResultRow& Row){return Row.ContenderId==Result.Winner;}):nullptr;
    HudSetText(ResultTitle,LDHUD::ResultHeadline(Result,WinnerRow?WinnerRow->Name:FString()));
    ResultWinnerBuild->ClearChildren();
    ResultWinnerBuild->SetVisibility(WinnerRow?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(WinnerRow)
    {
        ResultWinnerBuild->SetContent(LDResultBuild::Make(WidgetTree,WinnerRow->Build,DisplayDefinitions(),HudResultBoardWidth,184.f,
            AppliedScale>0?AppliedScale:1.f));
    }
    const int64 Seconds=FMath::Max<int64>(0,Result.DurationMs)/1000;
    HudSetText(ResultSubtitle,FString::Printf(TEXT("本局用时 %lld:%02lld · 击杀不决定胜负"),Seconds/60,Seconds%60));
    ResultRows->ClearChildren();ResultEntries.Reset();
    ResultShownAt=GetWorld()?GetWorld()->GetRealTimeSeconds():0.;
    const int32 Size=FMath::RoundToInt(16*(AppliedScale>0?AppliedScale:1.f));
    auto Cell=[this,Size](const FString& Value,float Width,const FLinearColor& Ink,LDUIFonts::ERole Role,bool bLeft)
    {
        UTextBlock* Label=LDWidgets::Text(WidgetTree,Value,Size,NAME_None,Role);Label->SetAutoWrapText(false);Label->SetColorAndOpacity(Ink);
        Label->SetJustification(bLeft?ETextJustify::Left:ETextJustify::Center);Label->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(Width);Box->SetClipping(EWidgetClipping::ClipToBounds);
        if(!Value.IsEmpty()){Box->SetToolTipText(FText::FromString(Value));}
        Box->SetContent(Label);return Box;
    };
    // Every row is a seat-coloured strip and a card; the own row is raised with a yellow rim, the winner's mark in yellow.
    auto Row=[&](const TArray<FString>& Values,const FLinearColor* Seat,bool bSelf,bool bWinner)
    {
        static constexpr float Widths[]={190,90,90,60,90,100,60,70};
        auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
        // Mock 5: the winner's star, then a seat-coloured portrait tile carrying the name's first glyph.
        {
            UTextBlock* Star=LDWidgets::Text(WidgetTree,bWinner?TEXT("★"):TEXT(""),LDHUDStyle::TypeHeading,NAME_None,LDUIFonts::ERole::Strong);
            Star->SetAutoWrapText(false);Star->SetColorAndOpacity(LDHUDStyle::UIAction);Star->SetJustification(ETextJustify::Center);
            auto* StarBox=WidgetTree->ConstructWidget<USizeBox>();StarBox->SetWidthOverride(36.f);StarBox->SetContent(Star);
            Line->AddChildToHorizontalBox(StarBox)->SetVerticalAlignment(VAlign_Center);
            auto* Portrait=WidgetTree->ConstructWidget<UBorder>();Portrait->SetBrush(HudRound(4.f,FLinearColor::Transparent,0.f));
            Portrait->SetBrushColor(Seat?*Seat:FLinearColor::Transparent);Portrait->SetHorizontalAlignment(HAlign_Center);Portrait->SetVerticalAlignment(VAlign_Center);
            if(Seat&&Values.Num()>0){UTextBlock* Glyph=LDWidgets::Text(WidgetTree,LDHUD::IconGlyph(Values[0]),Size+2,NAME_None,LDUIFonts::ERole::Strong);Glyph->SetAutoWrapText(false);Glyph->SetColorAndOpacity(LDHUDStyle::UIText);Portrait->SetContent(Glyph);}
            auto* PortraitBox=WidgetTree->ConstructWidget<USizeBox>();PortraitBox->SetWidthOverride(44.f);PortraitBox->SetHeightOverride(Seat?44.f:8.f);
            PortraitBox->SetContent(Portrait);
            Line->AddChildToHorizontalBox(PortraitBox)->SetPadding(FMargin(0,0,14,0));
            if(auto* PortraitSlot=Cast<UHorizontalBoxSlot>(PortraitBox->Slot)){PortraitSlot->SetVerticalAlignment(VAlign_Center);}
        }
        const FLinearColor Ink=!Seat?LDHUDStyle::TextSecondary:bSelf?LDHUDStyle::UIText:LDHUDStyle::UIText.CopyWithNewOpacity(.86f);
        for(int32 Column=0;Column<Values.Num()&&Column<UE_ARRAY_COUNT(Widths);++Column)
        {
            const bool bMark=Column==UE_ARRAY_COUNT(Widths)-1;
            if(!Seat&&Column>0)
            {
                // The head row draws a glyph per column; its name stays in the tooltip.
                auto* Glyph=WidgetTree->ConstructWidget<ULDPaintLayer>();
                const EHudResultGlyph Kind=static_cast<EHudResultGlyph>(Column);
                const FLinearColor GlyphInk=bMark?LDHUDStyle::UIAction:LDHUDStyle::TextSecondary;
                Glyph->Painter=[Kind,GlyphInk](const LDUIPaint::FContext& Paint){HudResultGlyph(Paint,Kind,GlyphInk);};
                auto* Head=WidgetTree->ConstructWidget<UVerticalBox>();
                auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(Widths[Column]);Box->SetHeightOverride(40.f);
                Box->SetToolTipText(FText::FromString(bMark?FString(LDTerms::Winner):Values[Column]));
                Box->SetContent(Glyph);Head->AddChildToVerticalBox(Box);
                Head->AddChildToVerticalBox(Cell(bMark?FString(LDTerms::Winner):Values[Column],Widths[Column],GlyphInk,LDUIFonts::ERole::Strong,false));
                Line->AddChildToHorizontalBox(Head)->SetVerticalAlignment(VAlign_Center);
                continue;
            }
            const LDUIFonts::ERole Role=!Seat?LDUIFonts::ERole::Strong:Column==0?LDUIFonts::ERole::Strong:LDUIFonts::ERole::Number;
            Line->AddChildToHorizontalBox(Cell(Values[Column],Widths[Column],bMark&&bWinner?LDHUDStyle::UIAction:Ink,Role,Column==0));
        }
        UWidget* Entry=Line;
        if(Seat)
        {
            auto* Card=WidgetTree->ConstructWidget<UBorder>();
            Card->SetBrush(HudRound(6.f,bSelf?LDHUDStyle::UIAction:LDHUDStyle::PanelRim,bSelf?2.f:1.f));
            Card->SetBrushColor(bSelf?LDHUDStyle::UIRaised:LDHUDStyle::UIPanel.CopyWithNewOpacity(.82f));
            Card->SetPadding(FMargin(0,6,10,6));Card->SetContent(Line);Entry=Card;
        }
        ResultRows->AddChildToVerticalBox(Entry)->SetPadding(FMargin(0,3));
        ResultEntries.Add(Entry);
    };
    Row({TEXT("名字"),LDTerms::ResultKills,LDTerms::ResultDeaths,LDTerms::ResultAssists,TEXT("最终等级"),TEXT("守护贡献"),TEXT("喷漆"),FString()},nullptr,false,false);
    // Rows follow the locked start roster, whose position is each contender's seat colour.
    for(int32 Index=0;Index<Result.Rows.Num();++Index)
    {
        const FLDResultRow& Entry=Result.Rows[Index];
        const LDHUD::FResultRowView View=LDHUD::ResultRow(Entry,Result.Winner,PC.OwnerState.ContenderId);
        const FLinearColor Seat=LDHUD::PaletteColor(uint8(Index));
        Row({View.Name,View.Kills,View.Deaths,View.Assists,View.Level,FString::Printf(TEXT("%.1f s"),double(Entry.BankedGuardMs)/1000.),
            FString::Printf(TEXT("%u"),Entry.CompletedSprays),View.bWinner?FString(LDTerms::Winner):FString()},
            &Seat,PC.OwnerState.ContenderId.IsValid()&&Entry.ContenderId==PC.OwnerState.ContenderId,View.bWinner);
    }
    AnimateResult();
}
void ULDHUD::AnimateResult()
{
    if(ResultShownAt<0.||!GetWorld()){return;}
    const double Age=GetWorld()->GetRealTimeSeconds()-ResultShownAt;
    // Winner first (stamped in), then the table row by row, then the buttons.
    const float Stamp=LDTween::Progress(Age,LDHUDStyle::MotionChoose,LDTween::ECurve::Back);
    LDTween::SetScale(ResultWinnerBand,FMath::Lerp(1.5f,1.f,Stamp));
    LDTween::SetOpacity(ResultWinnerBand,LDTween::Progress(Age,LDHUDStyle::MotionHover*2.,LDTween::ECurve::EaseOut));
    const double RowsAt=LDHUDStyle::MotionChoose+.05,RowStep=LDHUDStyle::MotionOfferStagger+.02,RowFor=LDHUDStyle::MotionInstallLand+.08;
    for(int32 Index=0;Index<ResultEntries.Num();++Index)
    {
        const float T=LDTween::Progress(Age-RowsAt-RowStep*Index,RowFor,LDTween::ECurve::EaseOut);
        LDTween::SetOpacity(ResultEntries[Index],T);LDTween::SetTranslation(ResultEntries[Index],FVector2D((1.f-T)*28.f,0.f));
    }
    const double ActionsAt=RowsAt+RowStep*ResultEntries.Num()+.1;
    const float Actions=LDTween::Progress(Age-ActionsAt,RowFor,LDTween::ECurve::EaseOut);
    LDTween::SetOpacity(ResultActionsBox,Actions);LDTween::SetTranslation(ResultActionsBox,FVector2D(0.f,(1.f-Actions)*16.f));
    if(Age>ActionsAt+RowFor){ResultShownAt=-1.;}
}
int32 ULDHUD::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);
    return Base+1;
}

void ULDHUD::PaintWorldPreviews(const FGeometry& G,FSlateWindowElementList& E,int32 Layer) const
{
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC||!PC->CanWorldCommand()||!Band){return;}
    auto Lines=[&](TArray<FVector2D> Points,FLinearColor Color,float Thickness=2.f){FSlateDrawElement::MakeLines(E,Layer,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Thickness);};
    auto Project=[&](FVector P,FVector2D& Out){FVector2D Screen;if(!PC->ProjectWorldLocationToScreen(P,Screen)){return false;}int32 W=0,H=0;PC->GetViewportSize(W,H);Out=Screen*G.GetLocalSize()/FVector2D(FMath::Max(1,W),FMath::Max(1,H));return true;};
    // The inspected unit is marked only by the selection ring under its feet; the HUD draws no bracket over it.
    // Armed preview (MOVEMENT_INPUT_AND_CAMERA ability targets): the own range ring around the feet, and for direction forms the direction from the
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
        float RangeCm=0, ConeDegrees=0;bool bDirection=false,bUnitTarget=false,bPoint=false;
        if(bArmedEntry)
        {
            const FLDCombatEntryView* Source=PC->CombatView.Entries.FindByPredicate([Armed](const FLDCombatEntryView& Candidate){return Candidate.Entry==Armed;});
            RangeCm=Source?float(Source->RangeCm):0.f;
            bDirection=Source&&(Source->TargetMode==uint8(ELDDemoTargetMode::DirectionProjectile)
                ||Source->TargetMode==uint8(ELDDemoTargetMode::DirectionDash)||Source->TargetMode==uint8(ELDDemoTargetMode::DirectionCone)
                ||Source->TargetMode==uint8(ELDDemoTargetMode::DirectionLine));
            bUnitTarget=Source&&Source->TargetMode==uint8(ELDDemoTargetMode::Unit);
            // A ground point previews where the zone would sit: the pointer, clamped to the reach.
            bPoint=Source&&Source->TargetMode==uint8(ELDDemoTargetMode::GroundPoint);bDirection|=bPoint;
            if (Source && Source->TargetMode==uint8(ELDDemoTargetMode::DirectionCone) && DisplayContent)
            {
                const auto* Ability=DisplayContent->Get().Abilities.FindByPredicate([Source](const auto& Row)
                { return Row.Id==Source->DefinitionId.ToString(); });
                if (Ability && Ability->Target.AngleDegrees.Denominator>0)
                { ConeDegrees=float(double(Ability->Target.AngleDegrees.Numerator)/Ability->Target.AngleDegrees.Denominator); }
            }
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
                if (ConeDegrees>0 && RangeCm>0)
                {
                    TArray<FVector2D> Fan;
                    FVector2D Start;
                    if (Project(Feet,Start))
                    {
                        Fan.Add(Start);
                        const double Heading=FMath::Atan2(Planar.Y,Planar.X);
                        const double Half=FMath::DegreesToRadians(double(ConeDegrees)*.5);
                        for (int32 Step=0;Step<=24;++Step)
                        {
                            const double Angle=Heading-Half+2.*Half*Step/24.;
                            FVector2D Edge;
                            if (Project(Feet+FVector(FMath::Cos(Angle)*RangeCm,FMath::Sin(Angle)*RangeCm,0),Edge)) { Fan.Add(Edge); }
                        }
                        Fan.Add(Start);
                        Lines(Fan,Preview,2.f);
                    }
                }
            }
        }
    }
}
