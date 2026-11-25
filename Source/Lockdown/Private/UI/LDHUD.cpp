#include "UI/LDHUD.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDResultBuild.h"
#include "UI/LDRecapChain.h"
#include "UI/LDChainCodex.h"
#include "Combat/LDChainRules.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDBroadcastStyle.h"
#include "UI/LDBroadcastPaint.h"
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
#include "Framework/LDGameInstance.h"
#include "Framework/LDBuildBlueprintComponent.h"
#include "Framework/LDGameState.h"
#include "Framework/LDDemoEntry.h"
#include "PK/LDPKRules.h"
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
#include "Navigation/LDOrderRules.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "Vision/LDWorldViewRelay.h"
#include "Audio/LDAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
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
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
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
// On-air geometry in Slate units at the 1080p reference (720p draws the same layout at ~0.667): a safe margin round the
// screen, the top-left score bug, the top-centre target card, the top-right feed and the bottom strip centred under the
// character. Sizes follow LDBroadcastStyle's type scale.
constexpr float HudSafeSide=28.f;
constexpr float HudSafeTop=22.f;
constexpr float HudSafeBottom=20.f;
// A press on a slot becomes a drag once the pointer moves this far (screen units).
constexpr float HudDragStartPx=8.f;
// The score bug: its top row height, the clock block's width, the modifier badges, the beat bar and the seat plates.
constexpr float HudBugRowH=46.f;
constexpr float HudBugClockW=112.f;
constexpr float HudBugBadge=34.f;
constexpr float HudBugWidth=8*34.f+7*6.f;
constexpr float HudBugBeatH=6.f;
constexpr float HudBugPlateW=34.f;
constexpr float HudBugPlateGap=6.f;
constexpr float HudBugPlateH=32.f;
// The target card: the plate, the name's cap, the bar and the Q/W/E and status tiles.
constexpr float HudTargetPlateW=60.f;
constexpr float HudTargetPlateH=52.f;
constexpr float HudTargetNameMax=230.f;
constexpr float HudTargetBarW=170.f;
constexpr float HudTargetBarH=16.f;
constexpr float HudTargetActiveSide=30.f;
constexpr float HudTargetStatusSide=26.f;
// The rest actions and the spawn choice stand under the target card; the toast under them.
constexpr float HudTopCentreY=HudSafeTop+HudTargetPlateH+14.f;
constexpr float HudToastY=HudTopCentreY+64.f;
// Mock 5's result page: the whole width, the gap between its cards and the winner's rule board inside its card.
constexpr float HudResultWidth=1640.f;
constexpr float HudResultGap=20.f;
constexpr float HudResultBoardWidth=600.f;
constexpr float HudMapSize=232.f;
constexpr float HudFeedWidth=520.f;
constexpr float HudFeedRowH=34.f;
constexpr float HudFeedGap=6.f;
// The strip: the seat block, its columns and gaps, the health figure, the square slots over their key bands.
constexpr float HudStripH=106.f;
constexpr float HudStripPanelTop=12.f;
constexpr float HudStripPanelBottom=96.f;
constexpr float HudStripPadR=18.f;
constexpr float HudSeatW=128.f;
constexpr float HudSeatGap=20.f;
constexpr float HudLevelW=86.f;
constexpr float HudColGap=18.f;
constexpr float HudHealthW=330.f;
constexpr float HudHealthFigure=60.f;
constexpr float HudHealthBarH=12.f;
constexpr float HudMomentumBarW=104.f;
constexpr float HudMomentumBarH=8.f;
constexpr float HudMomentumTextW=40.f;
constexpr float HudSlotSide=84.f;
constexpr float HudKeyBandH=18.f;
constexpr float HudSlotTop=0.f;
constexpr float HudSlotGap=6.f;
constexpr float HudGroupGap=16.f;
constexpr float HudDetailWidth=300.f;
// The corner build entry and the loot queue above it.
constexpr float HudCornerH=46.f;
constexpr float HudLootBottom=HudSafeBottom+HudCornerH+20.f;
// The PK side panel wraps at this width: the longest fixed line (the PK time limit) stays on one line at 100% text.
constexpr float HudPKWrap=440.f;
const FLinearColor HudPromptFill(FColor(0xFF,0xC8,0x3D));
struct FHudLookStyle
{
    FLinearColor Fill;
    FLinearColor Edge;
    float EdgeWidth=1.f;
    // The key band under the face and its key ink.
    FLinearColor Band;
    FLinearColor KeyInk;
};
// The 2 px rule round the face names the state: white when it can act (its key band orange), orange while armed, live red
// while controlled, grey while cooling or short of momentum, faint when blocked or empty.
FHudLookStyle HudLookStyle(LDHUD::EActionLook Look)
{
    const FLinearColor Face=LDBroadcast::Ink.CopyWithNewOpacity(.92f);
    const FLinearColor DarkBand=LDBroadcast::Ink.CopyWithNewOpacity(.92f);
    switch(Look)
    {
    case LDHUD::EActionLook::Ready: return {Face,LDBroadcast::White,LDBroadcast::RuleWidth,LDBroadcast::Orange,LDBroadcast::Ink};
    case LDHUD::EActionLook::Armed: return {Face,LDBroadcast::Orange,3.f,LDBroadcast::White,LDBroadcast::Ink};
    case LDHUD::EActionLook::Cooling: return {Face,LDBroadcast::Mute,LDBroadcast::RuleWidth,DarkBand,LDBroadcast::White};
    case LDHUD::EActionLook::MomentumShort: return {Face,LDBroadcast::Mute,LDBroadcast::RuleWidth,DarkBand,LDBroadcast::Orange};
    case LDHUD::EActionLook::Controlled: return {Face,LDBroadcast::LiveRed,3.f,LDBroadcast::LiveRed,LDBroadcast::White};
    default: return {LDBroadcast::Ink.CopyWithNewOpacity(.8f),LDBroadcast::White.CopyWithNewOpacity(.28f),LDBroadcast::RuleWidth,DarkBand,LDBroadcast::Grey};
    }
}
// Icon square colours under a missing picture: actives raised grey, the innate dash panel grey and an empty slot ink; the
// square carries the first character when a texture is missing.
enum class EHudIconKind : uint8 { Active, Dash, Empty };
FLinearColor HudIconBase(EHudIconKind Kind)
{
    switch(Kind)
    {
    case EHudIconKind::Active: return LDBroadcast::Raised;
    case EHudIconKind::Dash: return LDBroadcast::Panel;
    default: return LDBroadcast::Ink;
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
        auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
        if(!PC){return Super::RebuildWidget();}
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
        NetSpikeText=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeCaption,TEXT("HUDNetSpikeText"));
        NetSpikeText->SetAutoWrapText(false);
        NetSpikeText->SetJustification(ETextJustify::Right);
        NetSpikeText->SetColorAndOpacity(LDHUDStyle::TextWarning);
        NetSpikePanel=Panel(NetSpikeText,false);
        NetSpikePanel->SetBrush(HudRound(6.f,LDHUDStyle::TextWarning,1.5f));
        NetSpikePanel->SetBrushColor(FLinearColor(.16f,.11f,.02f,.92f));
        NetSpikePanel->SetPadding(FMargin(10,4));
        NetSpikePanel->SetVisibility(ESlateVisibility::Collapsed);
        auto Portraits=[&](float Size,int32 GlyphSize,UBorder*& OutFrame,UTextBlock*& OutGlyph,FName Name=NAME_None)
        {
            // The glyph sits on any of the eight identity tints, bright ones included; an outline keeps it readable on each.
            OutGlyph=OneLine(TEXT(""),GlyphSize,0);OutGlyph->SetJustification(ETextJustify::Center);HudFace(OutGlyph,true,2);
            OutFrame=WidgetTree->ConstructWidget<UBorder>();OutFrame->SetBrush(FSlateColorBrush(FLinearColor::White));
            OutFrame->SetHorizontalAlignment(HAlign_Center);OutFrame->SetVerticalAlignment(VAlign_Center);OutFrame->SetContent(OutGlyph);
            auto* Box=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),Name);Box->SetWidthOverride(Size);Box->SetHeightOverride(Size);Box->SetContent(OutFrame);
            return Box;
        };

        // ---- On-air chrome helpers (LDBroadcastStyle): figures in the condensed italic face, labels in Source Han Bold or
        // Heavy, and a paint layer under content that fills the whole stack (slabs, plates and bars are painted, never
        // rounded panels). Sizes are 1080p pixels.
        auto Figure=[&](const FString& S,float Px,const FLinearColor& Ink)
        {
            UTextBlock* Label=LDWidgets::Text(WidgetTree,S,int32(LDBroadcastPaint::PointsOf(Px)),NAME_None,LDUIFonts::ERole::Figure);
            Label->SetAutoWrapText(false);Label->SetColorAndOpacity(Ink);return Label;
        };
        auto Words=[&](const FString& S,float Px,const FLinearColor& Ink,bool bHeavy=false)
        {
            UTextBlock* Label=LDWidgets::Text(WidgetTree,S,int32(LDBroadcastPaint::PointsOf(Px)),NAME_None,LDUIFonts::ERole::Strong);
            if(bHeavy){FSlateFontInfo Font=Label->GetFont();Font.TypefaceFontName=FName(TEXT("Heavy"));Label->SetFont(Font);}
            Label->SetAutoWrapText(false);Label->SetColorAndOpacity(Ink);return Label;
        };
        auto Painted=[&](UWidget* Content,TFunction<void(const LDUIPaint::FContext&)> Painter,FMargin Pad=FMargin(0))->UOverlay*
        {
            auto* Stack=WidgetTree->ConstructWidget<UOverlay>();Stack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            auto* Layer=WidgetTree->ConstructWidget<ULDPaintLayer>();Layer->Painter=MoveTemp(Painter);Layer->SetVisibility(ESlateVisibility::HitTestInvisible);
            auto* LayerSlot=Stack->AddChildToOverlay(Layer);LayerSlot->SetHorizontalAlignment(HAlign_Fill);LayerSlot->SetVerticalAlignment(VAlign_Fill);
            if(Content)
            {
                auto* ContentSlot=Stack->AddChildToOverlay(Content);ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Fill);
                ContentSlot->SetPadding(Pad);
            }
            return Stack;
        };
        auto Whole=[](const LDUIPaint::FContext& P){return FBox2D(FVector2D::ZeroVector,FVector2D(P.Geometry.GetLocalSize()));};
        auto Slabbed=[&](UWidget* Content,TFunction<FLinearColor()> Ink,FMargin Pad)
        {
            return Painted(Content,[Ink=MoveTemp(Ink),Whole](const LDUIPaint::FContext& P){LDBroadcastPaint::FillSlab(P,P.Layer,Whole(P),Ink());},Pad);
        };
        auto Fixed=[&](UWidget* Content,float W,float H)
        {
            auto* Box=WidgetTree->ConstructWidget<USizeBox>();if(W>0.f){Box->SetWidthOverride(W);}if(H>0.f){Box->SetHeightOverride(H);}
            Box->SetContent(Content);return Box;
        };

        // Top centre: the current authorized target only, as one lower third: seat plate, name, public level, the health
        // figure over a segmented bar (public momentum under it), then its public statuses and Q/W/E as square tiles. It
        // disappears with the observation (a 2.5 s lost-vision snapshot aside) and never greys into history.
        {
            auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();Head->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            // The plate: a contender's seat ink and jersey number; a camp's tint block with its first character.
            TargetGlyph=Figure(TEXT(""),HudTargetPlateH*.62f,LDBroadcast::Ink);TargetGlyph->SetJustification(ETextJustify::Center);
            {FSlateFontInfo Font=LDUIFonts::Font(LDUIFonts::ERole::Title,LDBroadcastPaint::PointsOf(HudTargetPlateH*.5f));Font.SkewAmount=0.f;TargetGlyph->SetFont(Font);}
            UOverlay* Plate=Painted(TargetGlyph,[this,Whole](const LDUIPaint::FContext& P)
            {
                const FBox2D Box=Whole(P);
                if(TargetPaint.bContender){LDBroadcastPaint::SeatPlate(P,P.Layer,Box,TargetPaint.Seat);}
                else{LDBroadcastPaint::FillSlab(P,P.Layer,Box,TargetPaint.CreatureInk);}
            });
            if(UOverlaySlot* GlyphSlot=Cast<UOverlaySlot>(TargetGlyph->Slot)){GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);}
            TargetPortrait=Fixed(Plate,HudTargetPlateW,HudTargetPlateH);
            Head->AddChildToHorizontalBox(TargetPortrait)->SetVerticalAlignment(VAlign_Center);
            auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
            TargetTitle=Words(TEXT(""),LDBroadcast::TypeLabel,LDBroadcast::White,true);TargetTitle->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            Line->AddChildToHorizontalBox(Fixed(TargetTitle,0.f,0.f))->SetVerticalAlignment(VAlign_Center);
            if(USizeBox* TitleWidth=Cast<USizeBox>(TargetTitle->GetParent())){TitleWidth->SetMaxDesiredWidth(HudTargetNameMax);}
            TargetLevelText=Figure(TEXT(""),LDBroadcast::TypeBody,LDBroadcast::Grey);
            Line->AddChildToHorizontalBox(TargetLevelText)->SetPadding(FMargin(12,2,0,0));
            if(auto* LevelSlot=Cast<UHorizontalBoxSlot>(TargetLevelText->Slot)){LevelSlot->SetVerticalAlignment(VAlign_Center);}
            TargetHealth=Figure(TEXT(""),LDBroadcast::TypeHeadline,LDBroadcast::LiveRed);
            Line->AddChildToHorizontalBox(TargetHealth)->SetPadding(FMargin(18,0,0,0));
            if(auto* HealthSlot=Cast<UHorizontalBoxSlot>(TargetHealth->Slot)){HealthSlot->SetVerticalAlignment(VAlign_Center);}
            UOverlay* Bars=Painted(nullptr,[this,Whole](const LDUIPaint::FContext& P)
            {
                const FBox2D Box=Whole(P);
                const double Now=FPlatformTime::Seconds();
                LDBroadcastPaint::FBar HealthBar;HealthBar.Fraction=TargetPaint.bHealth?TargetPaint.Health:0.f;
                HealthBar.Trail=TargetPaint.bHealth?TargetTrail.Advance(TargetPaint.Health,Now):-1.f;HealthBar.Fill=TargetPaint.HealthInk;
                const float BarH=TargetPaint.bMomentum?Box.GetSize().Y*.62f:Box.GetSize().Y;
                LDBroadcastPaint::Bar(P,P.Layer,FBox2D(Box.Min,FVector2D(Box.Max.X,Box.Min.Y+BarH)),HealthBar);
                if(TargetPaint.bMomentum)
                {
                    LDBroadcastPaint::FBar Momentum;Momentum.Fraction=TargetPaint.Momentum;Momentum.Notch=TargetPaint.MomentumNotch;
                    const float Pulse=TargetPaint.bCharging?.55f+.45f*FMath::Abs(FMath::Sin(float(Now)*9.f)):1.f;
                    Momentum.Fill=TargetPaint.bMomentumFull?LDBroadcast::Gold.CopyWithNewOpacity(Pulse):LDHUDStyle::Momentum;
                    LDBroadcastPaint::Bar(P,P.Layer+5,FBox2D(FVector2D(Box.Min.X,Box.Max.Y-Box.GetSize().Y*.26f),Box.Max),Momentum);
                }
            });
            TargetBarSpot=Fixed(Bars,HudTargetBarW,HudTargetBarH);
            Line->AddChildToHorizontalBox(TargetBarSpot)->SetPadding(FMargin(14,0,4,0));
            if(auto* BarSlot=Cast<UHorizontalBoxSlot>(TargetBarSpot->Slot)){BarSlot->SetVerticalAlignment(VAlign_Center);}
            UOverlay* Slab=Slabbed(Line,[](){return LDBroadcast::Ink.CopyWithNewOpacity(.9f);},FMargin(22,0,18,0));
            Head->AddChildToHorizontalBox(Fixed(Slab,0.f,HudTargetPlateH))->SetPadding(FMargin(-6,0,0,0));
            // Public statuses: square tiles, a green or red rule naming help or harm; stacks over 1 in the corner.
            TargetStatusRow=WidgetTree->ConstructWidget<UWrapBox>();TargetStatusRow->SetInnerSlotPadding(FVector2D(3,3));
            TargetStatusRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            Head->AddChildToHorizontalBox(TargetStatusRow)->SetPadding(FMargin(8,0,0,0));
            if(auto* StatusSlot=Cast<UHorizontalBoxSlot>(TargetStatusRow->Slot)){StatusSlot->SetVerticalAlignment(VAlign_Center);}
            // A visible contender's Q/W/E as dark square tiles; no cooldown is ever drawn. The key and name are the tooltip.
            auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Row->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            for(int32 Index=0;Index<3;++Index)
            {
                UTextBlock* Name=Words(TEXT(""),LDBroadcast::TypeSmall,LDBroadcast::White);Name->SetJustification(ETextJustify::Center);
                auto* Tile=WidgetTree->ConstructWidget<UBorder>();Tile->SetBrush(HudRound(0.f,LDBroadcast::Mute,1.f));Tile->SetBrushColor(LDBroadcast::Ink.CopyWithNewOpacity(.9f));
                Tile->SetPadding(FMargin(0));Tile->SetHorizontalAlignment(HAlign_Center);Tile->SetVerticalAlignment(VAlign_Center);Tile->SetContent(Name);
                UImage* Icon=WidgetTree->ConstructWidget<UImage>();Icon->SetVisibility(ESlateVisibility::Collapsed);
                auto* Face=WidgetTree->ConstructWidget<UOverlay>();
                for(UWidget* Layer:{static_cast<UWidget*>(Tile),static_cast<UWidget*>(Icon)})
                {auto* LayerSlot=Face->AddChildToOverlay(Layer);LayerSlot->SetHorizontalAlignment(HAlign_Fill);LayerSlot->SetVerticalAlignment(VAlign_Fill);LayerSlot->SetPadding(FMargin(Layer==Icon?1.f:0.f));}
                auto* Side=Fixed(Face,HudTargetActiveSide,HudTargetActiveSide);Side->SetVisibility(ESlateVisibility::Visible);
                Row->AddChildToHorizontalBox(Side)->SetPadding(FMargin(Index?3.f:0.f,0,0,0));
                TargetActiveTiles.Add(Tile);TargetActiveNames.Add(Name);TargetActiveIcons.Add(Icon);
            }
            TargetActivesRow=Row;Head->AddChildToHorizontalBox(Row)->SetPadding(FMargin(10,0,0,0));
            if(auto* ActivesSlot=Cast<UHorizontalBoxSlot>(Row->Slot)){ActivesSlot->SetVerticalAlignment(VAlign_Center);}
            TargetCard=WidgetTree->ConstructWidget<UBorder>();TargetCard->SetBrush(FSlateNoResource());TargetCard->SetPadding(FMargin(0));
            TargetCard->SetContent(Head);TargetCard->SetVisibility(ESlateVisibility::Collapsed);
            // It keeps its hover details but is no click area: a world click or an armed skill's confirmation passes through it.
            Place(TargetCard,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,HudSafeTop));
        }

        // Top-left score bug (on air): LIVE · the beat's label · its clock · the day's modifiers, then the eight-beat match bar
        // and the eight seat plates (public facts only), then the flagpole line. It takes no pointer input except the
        // modifier badges' tooltips.
        auto* CircleBox=WidgetTree->ConstructWidget<UVerticalBox>();CircleBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        // A DEV test room is marked on every member's screen for the whole match (#78).
        DevMarkerText=OneLine(TEXT(""),LDHUDStyle::TypeTiny,0);DevMarkerText->SetJustification(ETextJustify::Right);DevMarkerText->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        DevMarkerText->SetVisibility(ESlateVisibility::Collapsed);
        {
            auto* Top=WidgetTree->ConstructWidget<UHorizontalBox>();Top->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            UTextBlock* Live=Figure(TEXT("LIVE"),LDBroadcast::TypeLabel,LDBroadcast::White);
            Top->AddChildToHorizontalBox(Fixed(Slabbed(Live,[](){return LDBroadcast::LiveRed;},FMargin(14,0,12,0)),0.f,HudBugRowH));
            if(UOverlaySlot* LiveSlot=Cast<UOverlaySlot>(Live->Slot)){LiveSlot->SetVerticalAlignment(VAlign_Center);LiveSlot->SetHorizontalAlignment(HAlign_Center);}
            ScoreLabelText=Words(TEXT(""),LDBroadcast::TypeLabel,LDBroadcast::White,true);
            UOverlay* LabelSlab=Slabbed(ScoreLabelText,[](){return LDBroadcast::Ink.CopyWithNewOpacity(.92f);},FMargin(18,0,18,0));
            if(UOverlaySlot* LabelSlot=Cast<UOverlaySlot>(ScoreLabelText->Slot)){LabelSlot->SetVerticalAlignment(VAlign_Center);}
            Top->AddChildToHorizontalBox(Fixed(LabelSlab,0.f,HudBugRowH))->SetPadding(FMargin(-4,0,0,0));
            ScoreClockText=Figure(TEXT(""),LDBroadcast::TypeTitle,LDBroadcast::Ink);ScoreClockText->SetJustification(ETextJustify::Center);
            UOverlay* ClockSlab=Slabbed(ScoreClockText,[this](){return ShownBug.bUrgent?LDBroadcast::LiveRed:LDBroadcast::White;},FMargin(16,0,16,0));
            if(UOverlaySlot* ClockSlot=Cast<UOverlaySlot>(ScoreClockText->Slot)){ClockSlot->SetVerticalAlignment(VAlign_Center);ClockSlot->SetHorizontalAlignment(HAlign_Center);}
            Top->AddChildToHorizontalBox(Fixed(ClockSlab,HudBugClockW,HudBugRowH))->SetPadding(FMargin(-4,0,0,0));
            // P6 spec §11.1: one square badge per daily modifier beside the clock; its name and numbers are the tooltip.
            for(int32 Index=0;Index<4;++Index)
            {
                UTextBlock* Glyph=Words(TEXT(""),LDBroadcast::TypeBody,LDBroadcast::White,true);Glyph->SetJustification(ETextJustify::Center);
                auto* Badge=WidgetTree->ConstructWidget<UBorder>();Badge->SetBrush(HudRound(0.f,LDBroadcast::Rule,LDBroadcast::RuleWidth));
                Badge->SetBrushColor(LDBroadcast::Ink.CopyWithNewOpacity(.85f));Badge->SetHorizontalAlignment(HAlign_Center);Badge->SetVerticalAlignment(VAlign_Center);
                Badge->SetPadding(FMargin(0));Badge->SetContent(Glyph);Badge->SetVisibility(ESlateVisibility::Collapsed);
                auto* BadgeAt=Top->AddChildToHorizontalBox(Fixed(Badge,HudBugBadge,HudBugBadge));BadgeAt->SetVerticalAlignment(VAlign_Center);
                BadgeAt->SetPadding(FMargin(Index?4.f:10.f,0,0,0));
                DailyBadges.Add(Badge);DailyBadgeGlyphs.Add(Glyph);
            }
            CircleBox->AddChildToVerticalBox(Top);
            // The eight beats: past white, now orange (a rest beat is narrower than a field beat), ahead dim.
            ScoreBeatSpot=Fixed(Painted(nullptr,[this,Whole](const LDUIPaint::FContext& P)
            {
                if(!ShownBug.bKnown){return;}
                const FBox2D Box=Whole(P);
                const float Gap=4.f,Unit=(float(Box.GetSize().X)-Gap*(LDHUD::ScoreBugBeats-1))/(4.f*1.f+4.f*1.6f);
                float X=float(Box.Min.X);
                for(int32 Beat=0;Beat<LDHUD::ScoreBugBeats;++Beat)
                {
                    const bool bRestBeat=Beat%2==0&&Beat<LDHUD::ScoreBugBeats-1;
                    const float W=Unit*(bRestBeat?1.f:1.6f);
                    const FLinearColor Ink=Beat<ShownBug.Beat?LDBroadcast::White:Beat==ShownBug.Beat?LDBroadcast::Orange:LDBroadcast::White.CopyWithNewOpacity(.25f);
                    LDBroadcastPaint::FillSlab(P,P.Layer,FBox2D(FVector2D(X,Box.Min.Y),FVector2D(X+W,Box.Max.Y)),Ink);
                    X+=W+Gap;
                }
            }),HudBugWidth,HudBugBeatH);
            CircleBox->AddChildToVerticalBox(ScoreBeatSpot)->SetPadding(FMargin(4,6,0,0));
            // The seats: a plate each in the seat ink with its number; the own seat underlined orange, the graffiti owner
            // crowned gold, the 上热搜 leader marked live red, a fresh public KO struck through.
            ScorePlateSpot=Fixed(Painted(nullptr,[this,Whole](const LDUIPaint::FContext& P)
            {
                const FBox2D Box=Whole(P);
                const float H=float(Box.GetSize().Y)-6.f,W=HudBugPlateW;
                for(int32 Index=0;Index<ShownPlates.Num();++Index)
                {
                    const LDHUD::FSeatPlateView& Seat=ShownPlates[Index];
                    const float X=float(Box.Min.X)+float(Index)*(W+HudBugPlateGap);
                    const FBox2D Plate(FVector2D(X,Box.Min.Y),FVector2D(X+W,Box.Min.Y+H));
                    const LDUIPaint::FContext Faded=P.WithOpacity(Seat.bRecentKO?.45f:1.f);
                    LDBroadcastPaint::SeatPlate(Faded,P.Layer,Plate,Seat.Seat);
                    if(Seat.bRecentKO){LDBroadcastPaint::Rule(P,P.Layer+3,FVector2D(Plate.Min.X+3.f,Plate.Max.Y-3.f),FVector2D(Plate.Max.X-3.f,Plate.Min.Y+3.f),LDBroadcast::LiveRed,3.f);}
                    if(Seat.bSelf){LDBroadcastPaint::FillSlab(P,P.Layer+2,FBox2D(FVector2D(X,Plate.Max.Y+2.f),FVector2D(X+W,Plate.Max.Y+5.f)),LDBroadcast::Orange);}
                    if(Seat.bGraffiti){LDBroadcastPaint::Block(P,P.Layer+2,FBox2D(FVector2D(Plate.Max.X-7.f,Plate.Min.Y-3.f),FVector2D(Plate.Max.X+1.f,Plate.Min.Y+5.f)),LDBroadcast::Gold);}
                    if(Seat.bLeader){LDBroadcastPaint::Block(P,P.Layer+2,FBox2D(FVector2D(Plate.Min.X-1.f,Plate.Min.Y-3.f),FVector2D(Plate.Min.X+7.f,Plate.Min.Y+5.f)),LDBroadcast::LiveRed);}
                }
            }),HudBugWidth,HudBugPlateH);
            ScorePlateSpot->SetVisibility(ESlateVisibility::Visible);
            CircleBox->AddChildToVerticalBox(ScorePlateSpot)->SetPadding(FMargin(4,6,0,0));
        }
        // #198: the rest step or the final field's closure under the bug, with its detail line (PK sign-up and lock).
        TimelineText=Words(TEXT(""),LDBroadcast::TypeBody,LDBroadcast::White,true);
        TimelineText->SetVisibility(ESlateVisibility::Collapsed);
        CircleBox->AddChildToVerticalBox(TimelineText)->SetPadding(FMargin(6,8,0,0));
        // The detail is information, not an action: grey, one line, shrunk to the bug's width rather than wrapped.
        TimelineDetailText=Words(TEXT(""),LDBroadcast::TypeBody,LDBroadcast::White);TimelineDetailText->SetAutoWrapText(false);
        TimelineDetailText->SetVisibility(ESlateVisibility::Collapsed);
        {
            UScaleBox* DetailFit=LDWidgets::Fit(WidgetTree,TimelineDetailText);
            if(auto* FitSlot=Cast<UScaleBoxSlot>(DetailFit->GetContentSlot())){FitSlot->SetHorizontalAlignment(HAlign_Left);}
            auto* DetailWidth=WidgetTree->ConstructWidget<USizeBox>();DetailWidth->SetWidthOverride(HudBugWidth+60.f);DetailWidth->SetContent(DetailFit);
            DetailWidth->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            CircleBox->AddChildToVerticalBox(DetailWidth)->SetPadding(FMargin(6,2,0,0));
        }
        {
            // The flagpole: its state over a gold progress bar, then the graffiti owner and the guard state.
            PhaseText=Words(TEXT(""),LDBroadcast::TypeBody,LDBroadcast::White,true);
            CircleBox->AddChildToVerticalBox(PhaseText)->SetPadding(FMargin(6,8,0,0));
            CircleSpot=Fixed(Painted(nullptr,[this,Whole](const LDUIPaint::FContext& P)
            {
                LDBroadcastPaint::FBar Bar;Bar.Fraction=CircleFraction;Bar.Fill=LDBroadcast::Gold;Bar.Track=LDBroadcast::Ink.CopyWithNewOpacity(.75f);
                LDBroadcastPaint::Bar(P,P.Layer,Whole(P),Bar);
            }),HudBugWidth,6.f);
            CircleBox->AddChildToVerticalBox(CircleSpot)->SetPadding(FMargin(6,4,0,0));
            auto* Foot=WidgetTree->ConstructWidget<UHorizontalBox>();
            PhaseOwnerText=Words(TEXT(""),LDBroadcast::TypeSmall,LDBroadcast::Grey);PhaseOwnerText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            PhaseAsideText=Words(TEXT(""),LDBroadcast::TypeSmall,LDBroadcast::Grey);PhaseAsideText->SetJustification(ETextJustify::Right);PhaseAsideText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            Foot->AddChildToHorizontalBox(PhaseOwnerText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            auto* AsideSlot=Foot->AddChildToHorizontalBox(PhaseAsideText);AsideSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));AsideSlot->SetPadding(FMargin(12,0,0,0));
            Foot->SetVisibility(ESlateVisibility::Collapsed);PhaseFootRow=Foot;
            CircleBox->AddChildToVerticalBox(Fixed(Foot,HudBugWidth,0.f))->SetPadding(FMargin(6,4,0,0));
        }
        PhaseBanner=WidgetTree->ConstructWidget<UBorder>();PhaseBanner->SetBrush(FSlateNoResource());PhaseBanner->SetPadding(FMargin(0));PhaseBanner->SetContent(CircleBox);
        Place(PhaseBanner,FAnchors(0,0),FVector2D(0,0),FVector2D(HudSafeSide,HudSafeTop));
        {
            // The rest actions sit in their own hit panel right under the click-through banner, shown only in a rest (the
            // training reset and the ready button); the stack grows downward from the top
            // edge, so a taller banner never covers it.
            auto* RestLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            TrainingResetButton=LDWidgets::Button(WidgetTree,LDTerms::TrainingReset,[this](){SubmitTrainingReset();},true,TEXT("HUDTrainingReset"));
            RestLine->AddChildToHorizontalBox(TrainingResetButton)->SetVerticalAlignment(VAlign_Center);
            RestReadyButton=LDWidgets::Button(WidgetTree,TEXT("准备完毕"),[this](){SubmitRestReady();},true,TEXT("HUDRestReady"));
            // On air: parallelogram blocks. 准备完毕 is the orange actionable block (a panel with a white rule once ready, when
            // it cancels); 重置训练 is a panel block with a white rule. The buttons keep flat transparent brushes of their own, so
            // no rounded face is painted under the slabs.
            for(ULDCommandButton* Action:{TrainingResetButton.Get(),RestReadyButton.Get()})
            {
                UTextBlock* Caption=Cast<UTextBlock>(Action->GetContent());
                if(!Caption){continue;}
                Caption->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Strong,LDBroadcastPaint::PointsOf(LDBroadcast::TypeLabel)));
                auto Style=Action->GetStyle();
                Style.Normal=FSlateColorBrush(FLinearColor::Transparent);Style.Hovered=Style.Normal;Style.Pressed=Style.Normal;Style.Disabled=Style.Normal;
                Style.NormalPadding=FMargin(0);Style.PressedPadding=FMargin(0);Action->SetStyle(Style);
                const bool bReady=Action==RestReadyButton;
                if(bReady){RestReadyCaption=Caption;}
                Caption->RemoveFromParent();
                TWeakObjectPtr<ULDCommandButton> Weak(Action);
                Action->SetContent(Painted(Caption,[this,Weak,bReady,Whole](const LDUIPaint::FContext& P)
                {
                    const FBox2D Box=Whole(P);
                    const bool bHot=Weak.IsValid()&&Weak->IsHovered()&&Weak->GetIsEnabled();
                    const bool bOrange=bReady&&!bRestReadyShown;
                    if(!bOrange){LDBroadcastPaint::FillSlab(P,P.Layer,Box,LDBroadcast::White);}
                    const FBox2D Inner(Box.Min+FVector2D(LDBroadcast::RuleWidth,LDBroadcast::RuleWidth),Box.Max-FVector2D(LDBroadcast::RuleWidth,LDBroadcast::RuleWidth));
                    const FLinearColor Fill=bOrange?(bHot?LDBroadcast::Gold:LDBroadcast::Orange):(bHot?LDBroadcast::Raised:LDBroadcast::Panel);
                    LDBroadcastPaint::FillSlab(P,P.Layer+1,bOrange?Box:Inner,Weak.IsValid()&&!Weak->GetIsEnabled()?Fill.CopyWithNewOpacity(.5f):Fill);
                },FMargin(30,10,30,10)));
                Caption->SetColorAndOpacity(bReady?LDBroadcast::Ink:LDBroadcast::White);
            }
            RestLine->AddChildToHorizontalBox(RestReadyButton)->SetPadding(FMargin(20,0,0,0));
            RestAnswerText=OneLine(TEXT(""),LDHUDStyle::TypeSmall,420);RestAnswerText->SetColorAndOpacity(LDHUDStyle::TextSecondary);RestAnswerText->SetVisibility(ESlateVisibility::Collapsed);
            auto* ResultSlot=RestLine->AddChildToHorizontalBox(RestAnswerText);ResultSlot->SetVerticalAlignment(VAlign_Center);ResultSlot->SetPadding(FMargin(10,0,0,0));
            // On air the rest actions stand bare under the target card, without a rounded panel behind them.
            RestActionsPanel=Panel(RestLine,true);RestActionsPanel->SetBrush(FSlateNoResource());RestActionsPanel->SetVisibility(ESlateVisibility::Collapsed);
            {
                // PK actions and public sides stay off the arena centre. No scroll view: when every side, bet and award row
                // shows at a large text scale the panel scales down to its height cap.
                auto* PKBox=WidgetTree->ConstructWidget<UVerticalBox>();
                auto Centred=[PKBox](UWidget* Child,float Top){auto* S=PKBox->AddChildToVerticalBox(Child);S->SetHorizontalAlignment(HAlign_Center);S->SetPadding(FMargin(0,Top,0,0));};
                PKTitleText=OneLine(TEXT(""),LDHUDStyle::TypeLabel,0);PKTitleText->SetJustification(ETextJustify::Center);HudFace(PKTitleText,true,0);Centred(PKTitleText,0);
                PKCountdownText=OneLine(TEXT(""),LDHUDStyle::TypeLead,HudPKWrap);PKCountdownText->SetJustification(ETextJustify::Center);HudFace(PKCountdownText,true,2);
                PKCountdownText->SetColorAndOpacity(LDHUDStyle::TextWarning);Centred(PKCountdownText,4);
                PKRingText=OneLine(TEXT(""),LDHUDStyle::TypeSmall,HudPKWrap);PKRingText->SetJustification(ETextJustify::Center);PKRingText->SetColorAndOpacity(FSlateColor(ALDPKRingMarker::SafeColor()));Centred(PKRingText,2);
                for(int32 Index=0;Index<LDPKRules::SpawnAnchors;++Index)
                {
                    UTextBlock* SideText=OneLine(TEXT(""),LDHUDStyle::TypeBody,HudPKWrap);SideText->SetJustification(ETextJustify::Center);SideText->SetVisibility(ESlateVisibility::Collapsed);
                    Centred(SideText,Index?2.f:6.f);PKSideTexts.Add(SideText);
                }
                PKRoleText=OneLine(TEXT(""),LDHUDStyle::TypeSmall,HudPKWrap);PKRoleText->SetJustification(ETextJustify::Center);PKRoleText->SetColorAndOpacity(LDHUDStyle::TextAccent);Centred(PKRoleText,6);
                for(int32 Index=0;Index<LDPKRules::SpawnAnchors;++Index)
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
                for(int32 Index=0;Index<LDPKRules::SpawnAnchors;++Index)
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
            auto* RestSlot=TopCentre->AddChildToVerticalBox(RestActionsPanel);RestSlot->SetHorizontalAlignment(HAlign_Center);RestSlot->SetPadding(FMargin(0,6,0,0));
            {
                // P6 spec §10.4: the death screen's spawn choice, one button per candidate and the seconds left.
                auto* ChoiceLine=WidgetTree->ConstructWidget<UHorizontalBox>();
                auto* ChoiceTitle=OneLine(LDTerms::SpawnChoice,LDHUDStyle::TypeLabel,0);HudFace(ChoiceTitle,true,0);
                ChoiceLine->AddChildToHorizontalBox(ChoiceTitle)->SetVerticalAlignment(VAlign_Center);
                for(int32 Index=0;Index<LDPacing::MaxSpawnChoices;++Index)
                {
                    ULDCommandButton* Choice=LDWidgets::Button(WidgetTree,LDTerms::SpawnCandidate(Index),[this,Index](){SelectSpawnCandidate(Index);},true,
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
            // Under the target card: the rest actions and the spawn choice.
            Place(TopCentre,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,HudTopCentreY));
            // Above the workshop (its rest actions stay usable in the full view), below the tray and the storage.
            TopCentreSlot=Cast<UCanvasPanelSlot>(TopCentre->Slot);if(TopCentreSlot){TopCentreSlot->SetZOrder(9);}
            {
                // #331: compact top PK matchup + betting bar shown over the open Workshop during Draw / Betting.
                auto* CompactLine=WidgetTree->ConstructWidget<UHorizontalBox>();
                PKCompactTitle=OneLine(TEXT(""),LDHUDStyle::TypeSmall,0);HudFace(PKCompactTitle,true,1);PKCompactTitle->SetColorAndOpacity(LDHUDStyle::TextAccent);
                CompactLine->AddChildToHorizontalBox(PKCompactTitle)->SetVerticalAlignment(VAlign_Center);
                PKCompactBetButtons.Reset();
                for(int32 Index=0;Index<LDPKRules::SpawnAnchors;++Index)
                {
                    ULDCommandButton* Bet=LDWidgets::Button(WidgetTree,TEXT(""),[this,Index](){SubmitPKBet(Index);},true,FName(*FString::Printf(TEXT("HUDPKCompactBet%d"),Index+1)));
                    Bet->SetVisibility(ESlateVisibility::Collapsed);
                    CompactLine->AddChildToHorizontalBox(Bet)->SetPadding(FMargin(8,0,0,0));
                    PKCompactBetButtons.Add(Bet);
                }
                PKCompactBar=Panel(CompactLine,true);PKCompactBar->SetVisibility(ESlateVisibility::Collapsed);
                Place(PKCompactBar,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,66));
                if(auto* CompactSlot=Cast<UCanvasPanelSlot>(PKCompactBar->Slot)){CompactSlot->SetZOrder(12);}
            }
        }
        auto* DeathBox=WidgetTree->ConstructWidget<UVerticalBox>();
        DeathTitle=OneLine(TEXT(""),LDHUDStyle::TypeDisplay,0);HudFace(DeathTitle,true,0);DeathTitle->SetColorAndOpacity(LDHUDStyle::TextDanger);
        DeathCauseText=OneLine(TEXT(""),LDHUDStyle::TypeLead,520);DeathCountdown=OneLine(TEXT(""),LDHUDStyle::TypeSection,0);
        DeathNote=OneLine(TEXT(""),LDHUDStyle::TypeSmall,520);DeathNote->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        for(UTextBlock* DeathLine:{DeathTitle.Get(),DeathCauseText.Get(),DeathCountdown.Get(),DeathNote.Get()})
        {DeathLine->SetJustification(ETextJustify::Center);DeathBox->AddChildToVerticalBox(DeathLine)->SetHorizontalAlignment(HAlign_Center);}
        // The recap lines shrink together to the banner width instead of wrapping or scrolling, with a collapse toggle (#331).
        {
            auto* HeadLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            DeathRecapHeading=OneLine(TEXT(""),15,0);DeathRecapHeading->SetJustification(ETextJustify::Center);
            HeadLine->AddChildToHorizontalBox(DeathRecapHeading)->SetVerticalAlignment(VAlign_Center);
            DeathRecapToggle=LDWidgets::Button(WidgetTree,TEXT("收起 ▲"),[this]()
            {
                bDeathRecapCollapsed=!bDeathRecapCollapsed;
                MarkReadModelDirty();
            },true,TEXT("HUDDeathRecapToggle"));
            DeathRecapToggle->SetVisibility(ESlateVisibility::Collapsed);
            HeadLine->AddChildToHorizontalBox(DeathRecapToggle)->SetPadding(FMargin(10,0,0,0));
            DeathBox->AddChildToVerticalBox(HeadLine)->SetHorizontalAlignment(HAlign_Center);

            auto* RecapBox=WidgetTree->ConstructWidget<UVerticalBox>();
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
            DeathRecapContainer=RecapWidth;
            auto* RecapSlot=DeathBox->AddChildToVerticalBox(RecapWidth);RecapSlot->SetHorizontalAlignment(HAlign_Center);RecapSlot->SetPadding(FMargin(0,6,0,0));
        }
        DeathWorkshopButton=LDWidgets::Button(WidgetTree,LDTerms::DeathWorkshopPrompt(TEXT("B"),FString()),[this](){ToggleWorkshop();},true,TEXT("HUDDeathWorkshopButton"));
        LDWidgets::SetLook(DeathWorkshopButton,LDUIPaint::EButton::Primary);
        DeathWorkshopButton->SetVisibility(ESlateVisibility::Collapsed);
        auto* DeathBtnSlot=DeathBox->AddChildToVerticalBox(DeathWorkshopButton);DeathBtnSlot->SetHorizontalAlignment(HAlign_Center);DeathBtnSlot->SetPadding(FMargin(0,10,0,0));
        DeathBanner=Panel(DeathBox,false);DeathBanner->SetVisibility(ESlateVisibility::Collapsed);
        Place(DeathBanner,FAnchors(.5f,0),FVector2D(.5f,0),FVector2D(0,176));
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
        // Mock 5 (#331): the contender table on the left (click any row to switch), the selected contender's rule board on the right.
        {
            auto* Body=WidgetTree->ConstructWidget<UHorizontalBox>();ResultBox->AddChildToVerticalBox(Body);
            auto* LeftCol=WidgetTree->ConstructWidget<UVerticalBox>();
            ResultRows=WidgetTree->ConstructWidget<UVerticalBox>();
            LeftCol->AddChildToVerticalBox(ResultRows);
            auto* RowsSlot=Body->AddChildToHorizontalBox(LeftCol);RowsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));RowsSlot->SetPadding(FMargin(0,0,HudResultGap,0));
            auto* BoardBox=WidgetTree->ConstructWidget<UVerticalBox>();
            auto* BoardHead=WidgetTree->ConstructWidget<UHorizontalBox>();
            ResultBoardTitle=OneLine(TEXT("赢家规则板"),LDHUDStyle::TypeHeading,0);HudFace(ResultBoardTitle,true,0);
            auto* BoardTitleSlot=BoardHead->AddChildToHorizontalBox(ResultBoardTitle);BoardTitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));BoardTitleSlot->SetVerticalAlignment(VAlign_Center);
            ResultInspectSelfButton=LDWidgets::Button(WidgetTree,LDTerms::ResultInspectSelf,[this,PC](){SelectResultContender(PC->OwnerState.ContenderId);},false,TEXT("ResultInspectSelf"));
            BoardHead->AddChildToHorizontalBox(ResultInspectSelfButton)->SetVerticalAlignment(VAlign_Center);
            BoardBox->AddChildToVerticalBox(BoardHead)->SetPadding(FMargin(0,0,0,12));
            auto* ReportTabs=WidgetTree->ConstructWidget<UHorizontalBox>();
            const TCHAR* Labels[]={TEXT("构筑与加点"),TEXT("成长与卡牌"),TEXT("全员战斗数据")};
            for(uint8 Index=0;Index<UE_ARRAY_COUNT(Labels);++Index)
            {
                auto* Tab=LDWidgets::Button(WidgetTree,Labels[Index],[this,PC,Index]()
                {
                    ResultReportTab=Index;
                    if(const auto* GS=GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr)
                    {
                        RefreshResultInspectedBuild(GS->MatchResult,PC->OwnerState.ContenderId);
                    }
                },false,FName(*FString::Printf(TEXT("ResultReportTab%u"),uint32(Index))));
                ReportTabs->AddChildToHorizontalBox(Tab)->SetPadding(FMargin(0,0,8,0));
            }
            BoardBox->AddChildToVerticalBox(ReportTabs)->SetPadding(FMargin(0,0,0,8));
            ResultWinnerBuild=WidgetTree->ConstructWidget<USizeBox>();BoardBox->AddChildToVerticalBox(ResultWinnerBuild);
            ResultWinnerBuild->SetVisibility(ESlateVisibility::Collapsed);
            auto* Board=WidgetTree->ConstructWidget<UBorder>();Board->SetBrush(HudRound(6.f,LDHUDStyle::PanelRim,1.f));
            Board->SetBrushColor(LDHUDStyle::UIPanel.CopyWithNewOpacity(.82f));Board->SetPadding(FMargin(24,20));Board->SetContent(BoardBox);
            auto* BoardWidth=WidgetTree->ConstructWidget<USizeBox>();BoardWidth->SetWidthOverride(HudResultBoardWidth+48.f);BoardWidth->SetContent(Board);
            Body->AddChildToHorizontalBox(BoardWidth)->SetVerticalAlignment(VAlign_Fill);
        }
        // The own damage per source in one card (#331): icon + text + colored share bar per source on one line (never a scroll view).
        ResultDamageCells.Reset();ResultDamageSlots.Reset();ResultDamageIcons.Reset();ResultDamageGlyphs.Reset();ResultDamageBars.Reset();
        {
            auto* DamageBox=WidgetTree->ConstructWidget<UVerticalBox>();
            ResultDamageHeading=Text(TEXT(""),LDHUDStyle::TypeHeading);HudFace(ResultDamageHeading,true,0);
            DamageBox->AddChildToVerticalBox(ResultDamageHeading)->SetPadding(FMargin(0,0,0,10));
            auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();DamageBox->AddChildToVerticalBox(Line);
            const float CellW=(HudResultWidth-48.f)/LDHUD::DamageSummarySourcesMax-12.f;
            for(int32 Index=0;Index<LDHUD::DamageSummarySourcesMax;++Index)
            {
                auto* CellStack=WidgetTree->ConstructWidget<UVerticalBox>();
                auto* TopRow=WidgetTree->ConstructWidget<UHorizontalBox>();
                auto* IconFrame=WidgetTree->ConstructWidget<UBorder>();IconFrame->SetBrush(HudRound(4.f,LDHUDStyle::PanelRim,1.f));
                IconFrame->SetBrushColor(FLinearColor(.04f,.06f,.09f,.95f));IconFrame->SetPadding(FMargin(0));
                auto* IconOverlay=WidgetTree->ConstructWidget<UOverlay>();
                UImage* Pic=WidgetTree->ConstructWidget<UImage>();Pic->SetVisibility(ESlateVisibility::Collapsed);
                auto* PicSlot=IconOverlay->AddChildToOverlay(Pic);PicSlot->SetHorizontalAlignment(HAlign_Fill);PicSlot->SetVerticalAlignment(VAlign_Fill);
                UTextBlock* Glyph=OneLine(TEXT(""),12,0);HudFace(Glyph,true,1);Glyph->SetJustification(ETextJustify::Center);
                auto* GlyphSlot=IconOverlay->AddChildToOverlay(Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
                IconFrame->SetContent(IconOverlay);
                auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetWidthOverride(22);IconSize->SetHeightOverride(22);IconSize->SetContent(IconFrame);
                TopRow->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);

                UTextBlock* Cell=OneLine(TEXT(""),14,0);Cell->SetColorAndOpacity(LDHUDStyle::UIText);
                auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);
                Fit->SetContent(Cell);
                if(auto* FitSlot=Cast<UScaleBoxSlot>(Fit->GetContentSlot())){FitSlot->SetHorizontalAlignment(HAlign_Left);}
                auto* TextBox=WidgetTree->ConstructWidget<USizeBox>();TextBox->SetWidthOverride(FMath::Max(40.f,CellW-28.f));TextBox->SetContent(Fit);
                auto* TextSlot=TopRow->AddChildToHorizontalBox(TextBox);TextSlot->SetPadding(FMargin(6,0,0,0));TextSlot->SetVerticalAlignment(VAlign_Center);
                CellStack->AddChildToVerticalBox(TopRow);

                UProgressBar* ShareBar=WidgetTree->ConstructWidget<UProgressBar>();HudBarStyle(ShareBar,LDHUDStyle::UIAction);ShareBar->SetPercent(0.f);
                auto* BarBox=WidgetTree->ConstructWidget<USizeBox>();BarBox->SetHeightOverride(6.f);BarBox->SetContent(ShareBar);
                CellStack->AddChildToVerticalBox(BarBox)->SetPadding(FMargin(0,5,0,0));

                auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(CellW);Box->SetContent(CellStack);
                Box->SetVisibility(ESlateVisibility::Collapsed);
                Line->AddChildToHorizontalBox(Box)->SetPadding(FMargin(0,0,12,0));
                ResultDamageSlots.Add(Box);ResultDamageIcons.Add(Pic);ResultDamageGlyphs.Add(Glyph);ResultDamageCells.Add(Cell);ResultDamageBars.Add(ShareBar);
            }
            auto* DamageCard=WidgetTree->ConstructWidget<UBorder>();DamageCard->SetBrush(HudRound(6.f,LDHUDStyle::PanelRim,1.f));
            DamageCard->SetBrushColor(LDHUDStyle::UIPanel.CopyWithNewOpacity(.82f));DamageCard->SetPadding(FMargin(24,18));DamageCard->SetContent(DamageBox);
            ResultBox->AddChildToVerticalBox(DamageCard)->SetPadding(FMargin(0,HudResultGap,0,0));
        }
        // Spec §12 结算“本局最离谱”: one line per contender (at most eight), shrunk to fit, never scrolled.
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
            // #328 §4.4: one row per contender in the hold-Tab tactical scoreboard (portrait | name+crown | level | graffiti bar | K/D/A | Q/W/E icons | final mod).
            TacticalScoreRows.Reset();ScoreRows.Reset();ScoreCells.Reset();
            auto* HeaderLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            const TCHAR* HeaderCols[6]={TEXT("参赛者"),TEXT("等级"),TEXT("涂鸦守点"),TEXT("K / D / A"),TEXT("目击技能"),TEXT("终局改装")};
            const float HeaderWidths[6]={204.f,56.f,146.f,96.f,92.f,124.f};
            for(int32 Col=0;Col<6;++Col)
            {
                UTextBlock* Cell=OneLine(HeaderCols[Col],LDHUDStyle::TypeCaption,0);
                Cell->SetColorAndOpacity(LDHUDStyle::TextSecondary);
                auto* CellBox=WidgetTree->ConstructWidget<USizeBox>();CellBox->SetWidthOverride(HeaderWidths[Col]);CellBox->SetContent(Cell);
                HeaderLine->AddChildToHorizontalBox(CellBox)->SetPadding(FMargin(Col?8:0,0,0,0));
            }
            ScoreBox->AddChildToVerticalBox(HeaderLine)->SetPadding(FMargin(0,0,0,4));

            for(int32 RowIdx=0;RowIdx<LDCapture::MaxScoreRows;++RowIdx)
            {
                FScoreRowWidgets RowW;
                auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
                auto* WhoLine=WidgetTree->ConstructWidget<UHorizontalBox>();
                UBorder* PFrame=nullptr;UTextBlock* PGlyph=nullptr;
                auto* PBox=Portraits(24.f,LDHUDStyle::TypeCaption,PFrame,PGlyph);
                RowW.Portrait=PFrame;RowW.PortraitGlyph=PGlyph;
                WhoLine->AddChildToHorizontalBox(PBox)->SetVerticalAlignment(VAlign_Center);
                RowW.Name=OneLine(TEXT(""),LDHUDStyle::TypeBody,0);RowW.Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
                auto* NameSlot=WhoLine->AddChildToHorizontalBox(RowW.Name);NameSlot->SetPadding(FMargin(8,0,0,0));NameSlot->SetVerticalAlignment(VAlign_Center);
                NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                auto* WhoWidth=WidgetTree->ConstructWidget<USizeBox>();WhoWidth->SetWidthOverride(HeaderWidths[0]);WhoWidth->SetContent(WhoLine);
                Line->AddChildToHorizontalBox(WhoWidth)->SetVerticalAlignment(VAlign_Center);

                RowW.Level=OneLine(TEXT(""),LDHUDStyle::TypeBody,0);HudFace(RowW.Level,true,0);
                auto* LvWidth=WidgetTree->ConstructWidget<USizeBox>();LvWidth->SetWidthOverride(HeaderWidths[1]);LvWidth->SetContent(RowW.Level);
                auto* LvSlot=Line->AddChildToHorizontalBox(LvWidth);LvSlot->SetPadding(FMargin(8,0,0,0));LvSlot->SetVerticalAlignment(VAlign_Center);

                auto* GrafCol=WidgetTree->ConstructWidget<UVerticalBox>();
                RowW.GraffitiText=OneLine(TEXT(""),LDHUDStyle::TypeTiny,0);RowW.GraffitiText->SetColorAndOpacity(LDHUDStyle::TextSecondary);
                GrafCol->AddChildToVerticalBox(RowW.GraffitiText);
                RowW.GraffitiBar=WidgetTree->ConstructWidget<UProgressBar>();HudBarStyle(RowW.GraffitiBar,LDHUDStyle::UIAction);RowW.GraffitiBar->SetPercent(0.f);
                auto* GrafBarBox=WidgetTree->ConstructWidget<USizeBox>();GrafBarBox->SetHeightOverride(5.f);GrafBarBox->SetContent(RowW.GraffitiBar);
                GrafCol->AddChildToVerticalBox(GrafBarBox)->SetPadding(FMargin(0,2,0,0));
                auto* GrafWidth=WidgetTree->ConstructWidget<USizeBox>();GrafWidth->SetWidthOverride(HeaderWidths[2]);GrafWidth->SetContent(GrafCol);
                auto* GrafSlot=Line->AddChildToHorizontalBox(GrafWidth);GrafSlot->SetPadding(FMargin(8,0,0,0));GrafSlot->SetVerticalAlignment(VAlign_Center);

                RowW.KDA=OneLine(TEXT(""),LDHUDStyle::TypeBody,0);HudFace(RowW.KDA,true,0);
                auto* KdaWidth=WidgetTree->ConstructWidget<USizeBox>();KdaWidth->SetWidthOverride(HeaderWidths[3]);KdaWidth->SetContent(RowW.KDA);
                auto* KdaSlot=Line->AddChildToHorizontalBox(KdaWidth);KdaSlot->SetPadding(FMargin(8,0,0,0));KdaSlot->SetVerticalAlignment(VAlign_Center);

                auto* SkillRow=WidgetTree->ConstructWidget<UHorizontalBox>();
                for(int32 S=0;S<3;++S)
                {
                    auto* SStack=WidgetTree->ConstructWidget<UOverlay>();
                    RowW.SkillIcons[S]=WidgetTree->ConstructWidget<UImage>();RowW.SkillIcons[S]->SetVisibility(ESlateVisibility::Collapsed);
                    auto* SPic=SStack->AddChildToOverlay(RowW.SkillIcons[S]);SPic->SetHorizontalAlignment(HAlign_Fill);SPic->SetVerticalAlignment(VAlign_Fill);
                    RowW.SkillGlyphs[S]=OneLine(TEXT("?"),LDHUDStyle::TypeTiny,0);RowW.SkillGlyphs[S]->SetJustification(ETextJustify::Center);
                    RowW.SkillGlyphs[S]->SetColorAndOpacity(LDHUDStyle::TextSecondary);
                    auto* SGlyph=SStack->AddChildToOverlay(RowW.SkillGlyphs[S]);SGlyph->SetHorizontalAlignment(HAlign_Center);SGlyph->SetVerticalAlignment(VAlign_Center);
                    RowW.SkillFrames[S]=WidgetTree->ConstructWidget<UBorder>();
                    RowW.SkillFrames[S]->SetBrush(HudRound(4.f,LDHUDStyle::PanelRim,1.f));
                    RowW.SkillFrames[S]->SetBrushColor(FLinearColor(.02f,.03f,.05f,.85f));
                    RowW.SkillFrames[S]->SetPadding(FMargin(0));RowW.SkillFrames[S]->SetContent(SStack);
                    auto* SSize=WidgetTree->ConstructWidget<USizeBox>();SSize->SetWidthOverride(24.f);SSize->SetHeightOverride(24.f);SSize->SetContent(RowW.SkillFrames[S]);
                    SkillRow->AddChildToHorizontalBox(SSize)->SetPadding(FMargin(S?4:0,0,0,0));
                }
                auto* SkillWidth=WidgetTree->ConstructWidget<USizeBox>();SkillWidth->SetWidthOverride(HeaderWidths[4]);SkillWidth->SetContent(SkillRow);
                auto* SkillSlot=Line->AddChildToHorizontalBox(SkillWidth);SkillSlot->SetPadding(FMargin(8,0,0,0));SkillSlot->SetVerticalAlignment(VAlign_Center);

                RowW.FinalMod=OneLine(TEXT("—"),LDHUDStyle::TypeCaption,0);RowW.FinalMod->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
                RowW.FinalMod->SetColorAndOpacity(LDHUDStyle::UIEvent);
                auto* FinWidth=WidgetTree->ConstructWidget<USizeBox>();FinWidth->SetWidthOverride(HeaderWidths[5]);FinWidth->SetContent(RowW.FinalMod);
                auto* FinSlot=Line->AddChildToHorizontalBox(FinWidth);FinSlot->SetPadding(FMargin(8,0,0,0));FinSlot->SetVerticalAlignment(VAlign_Center);

                auto* RowCard=WidgetTree->ConstructWidget<UBorder>();
                RowCard->SetBrush(HudRound(5.f,FLinearColor(1,1,1,.08f),1.f));
                RowCard->SetBrushColor(FLinearColor(.02f,.035f,.055f,.75f));
                RowCard->SetPadding(FMargin(8,5));RowCard->SetContent(Line);
                RowW.Row=RowCard;
                ScoreBox->AddChildToVerticalBox(RowCard)->SetPadding(FMargin(0,2));
                ScoreRows.Add(RowCard);
                TacticalScoreRows.Add(MoveTemp(RowW));
            }
            ScorePanel=Panel(ScoreBox,false);Place(ScorePanel,FAnchors(.5f,.5f),FVector2D(.5f,.5f),FVector2D(0,-60));ScorePanel->SetVisibility(ESlateVisibility::Collapsed);
        }

        // Bottom-left corner: the minimap on an ink square with a 2 px white rule, the elapsed match time over its frame and
        // nothing else; the map's own markers carry the public events (2026-11-12 user decision: no text lines above it).
        auto* MapBox=WidgetTree->ConstructWidget<UVerticalBox>();
        // A square frame: the minimap fits either the field or the rest room (ULDMinimap), each with one uniform scale.
        Minimap=CreateWidget<ULDMinimap>(PC);MapFrame=WidgetTree->ConstructWidget<USizeBox>();MapFrame->SetWidthOverride(HudMapSize);MapFrame->SetHeightOverride(HudMapSize);MapFrame->SetContent(Minimap);
        auto* MapStack=WidgetTree->ConstructWidget<UVerticalBox>();
        MatchTimerText=Figure(TEXT(""),LDBroadcast::TypeBody,LDBroadcast::Grey);
        MatchTimerText->SetVisibility(ESlateVisibility::Collapsed);
        MapStack->AddChildToVerticalBox(MatchTimerText)->SetHorizontalAlignment(HAlign_Left);
        auto* MapRim=WidgetTree->ConstructWidget<UBorder>();MapRim->SetBrush(HudRound(0.f,LDBroadcast::Rule,LDBroadcast::RuleWidth));
        MapRim->SetBrushColor(LDBroadcast::Ink.CopyWithNewOpacity(.92f));MapRim->SetPadding(FMargin(LDBroadcast::RuleWidth));MapRim->SetContent(MapFrame);
        auto* FrameSlot=MapStack->AddChildToVerticalBox(MapRim);FrameSlot->SetHorizontalAlignment(HAlign_Left);FrameSlot->SetPadding(FMargin(0,2,0,0));
        MapBox->AddChildToVerticalBox(MapStack)->SetHorizontalAlignment(HAlign_Left);
        MapColumn=WidgetTree->ConstructWidget<UBorder>();MapColumn->SetBrush(FSlateNoResource());MapColumn->SetPadding(FMargin(0));MapColumn->SetContent(MapBox);
        HitPanels.Add(MapColumn);
        Place(MapColumn,FAnchors(0,1),FVector2D(0,1),FVector2D(HudSafeSide,-HudSafeBottom));
        // Top-right corner: the room's event feed (LDHUD::FEventFeed) painted as lower thirds, newest at the bottom. It takes
        // no pointer input; the DEV marker and the network diagnostics stand under it.
        EventFeedSpot=WidgetTree->ConstructWidget<USizeBox>();EventFeedSpot->SetWidthOverride(HudFeedWidth);EventFeedSpot->SetHeightOverride(0.f);
        EventFeedSpot->SetContent(Painted(nullptr,[this](const LDUIPaint::FContext& P){PaintFeed(P);}));
        EventFeedPanel=WidgetTree->ConstructWidget<UBorder>();EventFeedPanel->SetBrush(FSlateNoResource());EventFeedPanel->SetPadding(FMargin(0));
        EventFeedPanel->SetContent(EventFeedSpot);EventFeedPanel->SetVisibility(ESlateVisibility::Collapsed);
        auto* TopRight=WidgetTree->ConstructWidget<UVerticalBox>();TopRight->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        TopRight->AddChildToVerticalBox(EventFeedPanel)->SetHorizontalAlignment(HAlign_Right);
        TopRight->AddChildToVerticalBox(DevMarkerText)->SetHorizontalAlignment(HAlign_Right);
        TopRight->AddChildToVerticalBox(NetSpikePanel)->SetHorizontalAlignment(HAlign_Right);
        TopRight->AddChildToVerticalBox(DiagnosticsPanel)->SetHorizontalAlignment(HAlign_Right);
        // The PK side panel follows the corner stack, so a longer feed never covers it.
        TopRight->AddChildToVerticalBox(PKPanelBorder)->SetHorizontalAlignment(HAlign_Right);
        for(UWidget* Below:{static_cast<UWidget*>(DevMarkerText.Get()),static_cast<UWidget*>(NetSpikePanel.Get()),static_cast<UWidget*>(DiagnosticsPanel.Get()),static_cast<UWidget*>(PKPanelBorder.Get())})
        {if(auto* BelowSlot=Cast<UVerticalBoxSlot>(Below->Slot)){BelowSlot->SetPadding(FMargin(0,6,0,0));}}
        Place(TopRight,FAnchors(1,0),FVector2D(1,0),FVector2D(-HudSafeSide,HudSafeTop));
        // Settings open from the Back menu (its 设置 entry); the HUD has no separate settings button.

        // Bottom lower-third strip, centred on the screen: the seat block (own jersey number), level and experience, the health
        // figure over its segmented bar, momentum with the overdrive notch, then Q/W/E | D over their key bands. Weight follows
        // importance: the health figure and the slots lead; the level, numbers and keys stay small. The five stats do not live
        // here (the build ring allocates on its radar; the corner entry shows the unspent points).
        auto* StripRow=WidgetTree->ConstructWidget<UHorizontalBox>();StripRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        // The seat block: painted by PaintStrip; it takes hover so its tooltip names the own roster name.
        Portrait=WidgetTree->ConstructWidget<UBorder>();Portrait->SetBrush(FSlateNoResource());Portrait->SetPadding(FMargin(0));
        Portrait->SetVisibility(ESlateVisibility::Visible);
        BodySlot=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HUDActionBody"));BodySlot->SetWidthOverride(HudSeatW);BodySlot->SetHeightOverride(HudStripH);
        BodySlot->SetContent(Portrait);
        StripRow->AddChildToHorizontalBox(BodySlot)->SetVerticalAlignment(VAlign_Top);
        {
            // Level and experience: a small "LV" over nothing louder than the level figure; the bar's tooltip has the numbers.
            auto* LevelCol=WidgetTree->ConstructWidget<UVerticalBox>();
            auto* LevelLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            UTextBlock* LV=Figure(TEXT("LV"),LDBroadcast::TypeSmall,LDBroadcast::Orange);
            LevelLine->AddChildToHorizontalBox(LV)->SetVerticalAlignment(VAlign_Bottom);
            if(auto* LVSlot=Cast<UHorizontalBoxSlot>(LV->Slot)){LVSlot->SetPadding(FMargin(0,0,6,6));}
            LevelBadgeText=Figure(TEXT(""),LDBroadcast::TypeTitle+4.f,LDBroadcast::White);
            LevelLine->AddChildToHorizontalBox(LevelBadgeText)->SetVerticalAlignment(VAlign_Bottom);
            LevelCol->AddChildToVerticalBox(LevelLine);
            LevelBar=WidgetTree->ConstructWidget<UProgressBar>();
            {
                FProgressBarStyle Style;Style.SetBackgroundImage(FSlateColorBrush(LDBroadcast::White.CopyWithNewOpacity(.18f)))
                    .SetFillImage(FSlateColorBrush(FLinearColor::White)).SetMarqueeImage(FSlateNoResource());
                LevelBar->SetWidgetStyle(Style);LevelBar->SetFillColorAndOpacity(LDBroadcast::Orange);LevelBar->SetPercent(0);
            }
            LevelCol->AddChildToVerticalBox(Fixed(LevelBar,HudLevelW-10.f,6.f))->SetPadding(FMargin(0,2,0,0));
            StripRow->AddChildToHorizontalBox(Fixed(LevelCol,HudLevelW,0.f))->SetPadding(FMargin(HudSeatGap,0,0,0));
            if(auto* LevelSlot=Cast<UHorizontalBoxSlot>(LevelCol->GetParent()->Slot)){LevelSlot->SetVerticalAlignment(VAlign_Center);}
        }
        {
            // Health: the big figure (the strip's loudest element) with its maximum, momentum and regen to its right, the
            // segmented bar under both.
            auto* HealthCol=WidgetTree->ConstructWidget<UVerticalBox>();
            auto* Numbers=WidgetTree->ConstructWidget<UHorizontalBox>();
            HealthText=Figure(TEXT(""),HudHealthFigure,LDBroadcast::White);
            Numbers->AddChildToHorizontalBox(HealthText)->SetVerticalAlignment(VAlign_Bottom);
            HealthMaxText=Figure(TEXT(""),LDBroadcast::TypeLabel,LDBroadcast::Grey);
            auto* MaxSlot=Numbers->AddChildToHorizontalBox(HealthMaxText);MaxSlot->SetVerticalAlignment(VAlign_Bottom);MaxSlot->SetPadding(FMargin(4,0,0,6));
            Numbers->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            auto* Side=WidgetTree->ConstructWidget<UVerticalBox>();
            HealthRegenText=Figure(TEXT(""),LDBroadcast::TypeSmall,LDBroadcast::Grey);HealthRegenText->SetJustification(ETextJustify::Right);
            Side->AddChildToVerticalBox(HealthRegenText)->SetHorizontalAlignment(HAlign_Right);
            auto* MomentumLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            UTextBlock* Jin=Words(LDTerms::Momentum,LDBroadcast::TypeSmall,LDBroadcast::Orange,true);
            MomentumLine->AddChildToHorizontalBox(Jin)->SetVerticalAlignment(VAlign_Center);
            MomentumBarSpot=Fixed(Painted(nullptr,[this,Whole](const LDUIPaint::FContext& P)
            {
                LDBroadcastPaint::FBar Bar;Bar.Fraction=StripPaint.bMomentum?StripPaint.Momentum:0.f;
                Bar.Fill=StripPaint.bOverdrive?LDBroadcast::Gold:LDBroadcast::Orange;Bar.Track=LDBroadcast::White.CopyWithNewOpacity(.14f);
                Bar.Notch=StripPaint.bMomentum?StripPaint.Notch:-1.f;Bar.NotchInk=StripPaint.bOverdrive?LDBroadcast::Gold:LDBroadcast::White;
                LDBroadcastPaint::Bar(P,P.Layer,Whole(P),Bar);
            }),HudMomentumBarW,HudMomentumBarH);
            auto* BarSlot=MomentumLine->AddChildToHorizontalBox(MomentumBarSpot);BarSlot->SetVerticalAlignment(VAlign_Center);BarSlot->SetPadding(FMargin(6,0,6,0));
            MomentumText=Figure(TEXT(""),LDBroadcast::TypeSmall,LDBroadcast::White);MomentumText->SetJustification(ETextJustify::Right);
            MomentumLine->AddChildToHorizontalBox(Fixed(MomentumText,HudMomentumTextW,0.f))->SetVerticalAlignment(VAlign_Center);
            Side->AddChildToVerticalBox(MomentumLine)->SetPadding(FMargin(0,4,0,0));
            auto* SideSlot=Numbers->AddChildToHorizontalBox(Side);SideSlot->SetVerticalAlignment(VAlign_Bottom);SideSlot->SetPadding(FMargin(8,0,0,10));
            // A larger text scale shrinks the row to the column instead of pushing into the slots (no overflow, no scroll).
            HealthCol->AddChildToVerticalBox(LDWidgets::Fit(WidgetTree,Numbers));
            HealthBarSpot=Fixed(Painted(nullptr,[this,Whole](const LDUIPaint::FContext& P)
            {
                LDBroadcastPaint::FBar Bar;Bar.bLean=true;
                Bar.Fraction=StripPaint.bHealth?StripPaint.Health:0.f;Bar.Shield=StripPaint.bHealth?StripPaint.Shield:0.f;
                Bar.Trail=StripPaint.bHealth?StripTrail.Advance(StripPaint.Health,FPlatformTime::Seconds()):-1.f;
                Bar.Fill=StripPaint.HealthInk;Bar.Track=LDBroadcast::White.CopyWithNewOpacity(.14f);Bar.Segment=StripPaint.HealthSegment;
                LDBroadcastPaint::Bar(P,P.Layer,Whole(P),Bar);
            }),HudHealthW,HudHealthBarH);
            HealthCol->AddChildToVerticalBox(HealthBarSpot)->SetPadding(FMargin(0,0,0,0));
            StripRow->AddChildToHorizontalBox(Fixed(HealthCol,HudHealthW,0.f))->SetPadding(FMargin(HudColGap,0,0,0));
            if(auto* HealthSlot=Cast<UHorizontalBoxSlot>(HealthCol->GetParent()->Slot)){HealthSlot->SetVerticalAlignment(VAlign_Center);}
        }
        for(int32 Index=0;Index<HudActionCount;++Index)
        {
            FLDHUDActionSlot& Entry=ActionSlots.AddDefaulted_GetRef();
            // The face: the icon on a square with a 2 px rule naming the state, the cooldown mask and its remaining time over it,
            // the element chip (top-left), the momentum cost (bottom-right) and the rank ticks (bottom-left); the key rides on
            // the band under the face.
            auto* Face=WidgetTree->ConstructWidget<UOverlay>();Face->SetClipping(EWidgetClipping::ClipToBounds);
            auto FillLayer=[](UOverlaySlot* Layer){Layer->SetHorizontalAlignment(HAlign_Fill);Layer->SetVerticalAlignment(VAlign_Fill);};
            Entry.Glyph=Words(TEXT(""),LDBroadcast::TypeTitle,LDBroadcast::White,true);Entry.Glyph->SetJustification(ETextJustify::Center);
            Entry.Icon=WidgetTree->ConstructWidget<UBorder>();Entry.Icon->SetBrush(HudRound(0.f,FLinearColor::Transparent,0.f));
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
            FProgressBarStyle MaskStyle;MaskStyle.SetBackgroundImage(FSlateNoResource()).SetFillImage(FSlateColorBrush(FLinearColor::White)).SetMarqueeImage(FSlateNoResource());
            Entry.Cooldown->SetWidgetStyle(MaskStyle);Entry.Cooldown->SetBarFillType(EProgressBarFillType::TopToBottom);Entry.Cooldown->SetFillColorAndOpacity(LDBroadcast::Ink.CopyWithNewOpacity(.72f));
            Entry.Cooldown->SetPercent(0);Entry.Cooldown->SetVisibility(ESlateVisibility::Collapsed);FillLayer(Face->AddChildToOverlay(Entry.Cooldown));
            Entry.Countdown=Figure(TEXT(""),LDBroadcast::TypeTitle+8.f,LDBroadcast::White);Entry.Countdown->SetJustification(ETextJustify::Center);
            Entry.Countdown->SetVisibility(ESlateVisibility::Collapsed);
            auto* CountdownLayer=Face->AddChildToOverlay(Entry.Countdown);CountdownLayer->SetHorizontalAlignment(HAlign_Center);CountdownLayer->SetVerticalAlignment(VAlign_Center);
            auto Chip=[&](UTextBlock*& OutText,const FLinearColor& Ink,const FLinearColor& Fill,bool bFigure)
            {
                OutText=bFigure?Figure(TEXT(""),LDBroadcast::TypeSmall,Ink):Words(TEXT(""),LDBroadcast::TypeSmall,Ink,true);
                OutText->SetJustification(ETextJustify::Center);
                auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrush(FSlateColorBrush(FLinearColor::White));Border->SetBrushColor(Fill);
                Border->SetPadding(FMargin(5,0));Border->SetContent(OutText);Border->SetVisibility(ESlateVisibility::Collapsed);
                return Border;
            };
            UTextBlock* ChipText=nullptr;
            Entry.CostChip=Chip(ChipText,LDBroadcast::Orange,LDBroadcast::Ink.CopyWithNewOpacity(.9f),true);Entry.Cost=ChipText;
            auto* CostLayer=Face->AddChildToOverlay(Entry.CostChip);CostLayer->SetHorizontalAlignment(HAlign_Right);CostLayer->SetVerticalAlignment(VAlign_Bottom);CostLayer->SetPadding(FMargin(0,0,2,2));
            UTextBlock* ControlText=nullptr;
            Entry.ControlChip=Chip(ControlText,LDBroadcast::White,LDBroadcast::LiveRed,false);Entry.ControlBadge=ControlText;
            auto* ControlLayer=Face->AddChildToOverlay(Entry.ControlChip);ControlLayer->SetHorizontalAlignment(HAlign_Center);ControlLayer->SetVerticalAlignment(VAlign_Center);
            UTextBlock* ElementText=nullptr;
            Entry.ElementChip=Chip(ElementText,LDBroadcast::Ink,LDBroadcast::White,false);Entry.ElementGlyph=ElementText;Entry.ElementChip->SetPadding(FMargin(4,0));
            auto* ElementLayer=Face->AddChildToOverlay(Entry.ElementChip);ElementLayer->SetHorizontalAlignment(HAlign_Left);ElementLayer->SetVerticalAlignment(VAlign_Top);ElementLayer->SetPadding(FMargin(2,2,0,0));
            // The rank ticks on the lower edge and the missing momentum over a short face.
            Entry.Ranks=Figure(TEXT(""),LDBroadcast::TypeSmall,LDBroadcast::Orange);
            auto* RanksLayer=Face->AddChildToOverlay(Entry.Ranks);RanksLayer->SetHorizontalAlignment(HAlign_Left);RanksLayer->SetVerticalAlignment(VAlign_Bottom);RanksLayer->SetPadding(FMargin(5,0,0,1));
            Entry.Gap=Figure(TEXT(""),LDBroadcast::TypeLabel,LDBroadcast::Orange);Entry.Gap->SetVisibility(ESlateVisibility::Collapsed);
            auto* GapLayer=Face->AddChildToOverlay(Entry.Gap);GapLayer->SetHorizontalAlignment(HAlign_Center);GapLayer->SetVerticalAlignment(VAlign_Center);
            // The overdrive hold fills the face from the bottom in gold while the key is held on a full bar.
            Entry.Hold=WidgetTree->ConstructWidget<UProgressBar>();
            {
                FProgressBarStyle HoldStyle;HoldStyle.SetBackgroundImage(FSlateNoResource()).SetFillImage(FSlateColorBrush(FLinearColor::White)).SetMarqueeImage(FSlateNoResource());
                Entry.Hold->SetWidgetStyle(HoldStyle);Entry.Hold->SetBarFillType(EProgressBarFillType::BottomToTop);
                Entry.Hold->SetFillColorAndOpacity(LDBroadcast::Gold.CopyWithNewOpacity(.45f));Entry.Hold->SetPercent(0);Entry.Hold->SetVisibility(ESlateVisibility::Collapsed);
            }
            FillLayer(Face->AddChildToOverlay(Entry.Hold));
            Entry.Frame=WidgetTree->ConstructWidget<UBorder>();Entry.Frame->SetPadding(FMargin(LDBroadcast::RuleWidth));
            Entry.Frame->SetHorizontalAlignment(HAlign_Fill);Entry.Frame->SetVerticalAlignment(VAlign_Fill);Entry.Frame->SetContent(Face);
            // The ready flash covers the whole face; neither layer takes input, the button under them does.
            auto* Stack=WidgetTree->ConstructWidget<UOverlay>();
            FillLayer(Stack->AddChildToOverlay(Entry.Frame));
            Entry.Flash=WidgetTree->ConstructWidget<UBorder>();Entry.Flash->SetVisibility(ESlateVisibility::Collapsed);FillLayer(Stack->AddChildToOverlay(Entry.Flash));
            // The key band under the face.
            Entry.Key=Figure(TEXT(""),LDBroadcast::TypeSmall,LDBroadcast::Ink);Entry.Key->SetJustification(ETextJustify::Center);Entry.Key->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            Entry.KeyBand=WidgetTree->ConstructWidget<UBorder>();Entry.KeyBand->SetBrush(FSlateColorBrush(FLinearColor::White));Entry.KeyBand->SetBrushColor(LDBroadcast::Orange);
            Entry.KeyBand->SetPadding(FMargin(2,0));Entry.KeyBand->SetHorizontalAlignment(HAlign_Center);Entry.KeyBand->SetVerticalAlignment(VAlign_Center);Entry.KeyBand->SetContent(Entry.Key);
            auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();
            Column->AddChildToVerticalBox(Fixed(Stack,HudSlotSide,HudSlotSide));
            Column->AddChildToVerticalBox(Fixed(Entry.KeyBand,HudSlotSide,HudKeyBandH))->SetPadding(FMargin(0,2,0,0));
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
            Entry.Button->SetContent(Column);
            if(UButtonSlot* ColumnSlot=Cast<UButtonSlot>(Column->Slot)){ColumnSlot->SetPadding(FMargin(0));ColumnSlot->SetHorizontalAlignment(HAlign_Fill);ColumnSlot->SetVerticalAlignment(VAlign_Fill);}
            Entry.Box=WidgetTree->ConstructWidget<USizeBox>();Entry.Box->SetWidthOverride(HudSlotSide);Entry.Box->SetHeightOverride(HudSlotSide+2.f+HudKeyBandH);
            Entry.Box->SetRenderTransformPivot(FVector2D(.5f,.4f));
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
                {
                    // A small orange square on the corner: its own flat brushes, so no rounded button face is painted.
                    FButtonStyle PlusStyle=Entry.Plus->GetStyle();
                    PlusStyle.Normal=FSlateColorBrush(LDBroadcast::Orange);PlusStyle.Hovered=FSlateColorBrush(LDBroadcast::Gold);
                    PlusStyle.Pressed=FSlateColorBrush(LDBroadcast::Gold);PlusStyle.Disabled=FSlateColorBrush(LDBroadcast::Mute);
                    PlusStyle.NormalPadding=FMargin(10,0);PlusStyle.PressedPadding=FMargin(10,0);Entry.Plus->SetStyle(PlusStyle);
                    if(UTextBlock* PlusText=Cast<UTextBlock>(Entry.Plus->GetContent()))
                    {PlusText->SetFont(LDBroadcastPaint::FigureFont(LDBroadcast::TypeSmall));PlusText->SetColorAndOpacity(LDBroadcast::Ink);}
                }
                Entry.Plus->SetVisibility(ESlateVisibility::Collapsed);
                auto* PlusLayer=WithPlus->AddChildToOverlay(Entry.Plus);PlusLayer->SetHorizontalAlignment(HAlign_Right);PlusLayer->SetVerticalAlignment(VAlign_Top);
                // A tab standing on the face's top edge, above the icon: it never covers the picture.
                PlusLayer->SetHorizontalAlignment(HAlign_Center);PlusLayer->SetPadding(FMargin(0,-20,0,0));
                Entry.Box->SetContent(WithPlus);
            }
            else{Entry.Box->SetContent(Entry.Button);}
        }
        // Character actives (Q/W/E) and the innate dash (D) are separate groups after the resources.
        for(int32 Index=0;Index<HudActionCount;++Index)
        {
            auto* SlotAt=StripRow->AddChildToHorizontalBox(ActionSlots[Index].Box);
            SlotAt->SetPadding(FMargin(Index==0?HudColGap:Index==3?HudGroupGap:HudSlotGap,HudSlotTop,0,0));SlotAt->SetVerticalAlignment(VAlign_Top);
        }
        Band=WidgetTree->ConstructWidget<UBorder>();Band->SetBrush(FSlateNoResource());Band->SetPadding(FMargin(0));
        Band->SetContent(Painted(StripRow,[this](const LDUIPaint::FContext& P){PaintStrip(P);},FMargin(0,0,HudStripPadR,0)));
        HitPanels.Add(Band);

        // Above the strip: the own stat attribute view (a flyout the Attributes key opens) and the life line. The stack grows
        // upward from the strip, so a larger text scale never overlaps it.
        auto* BandStack=WidgetTree->ConstructWidget<UVerticalBox>();BandStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        StatBar=CreateWidget<ULDStatBar>(PC);
        if(StatBar)
        {
            auto* Flyout=WidgetTree->ConstructWidget<UBorder>();Flyout->SetBrush(HudRound(0.f,LDBroadcast::Rule,LDBroadcast::RuleWidth));
            Flyout->SetBrushColor(LDBroadcast::Ink.CopyWithNewOpacity(.92f));Flyout->SetPadding(FMargin(12,8));Flyout->SetContent(StatBar);
            Flyout->SetVisibility(ESlateVisibility::Collapsed);StatFlyout=Flyout;HitPanels.Add(Flyout);
            BandStack->AddChildToVerticalBox(Flyout)->SetPadding(FMargin(HudSeatW,0,0,8));
            if(auto* FlyoutSlot=Cast<UVerticalBoxSlot>(Flyout->Slot)){FlyoutSlot->SetHorizontalAlignment(HAlign_Left);}
        }
        else{UE_LOG(LogLDUI,Error,TEXT("LD_HUD_STAT_BAR_CREATION_FAILED"));}
        LifeText=Words(TEXT(""),LDBroadcast::TypeSmall,LDBroadcast::White,true);LifeText->SetVisibility(ESlateVisibility::Collapsed);
        BandStack->AddChildToVerticalBox(LifeText)->SetPadding(FMargin(6,0,0,6));
        if(auto* LifeSlot=Cast<UVerticalBoxSlot>(LifeText->Slot)){LifeSlot->SetHorizontalAlignment(HAlign_Left);}
        BandStack->AddChildToVerticalBox(Band)->SetHorizontalAlignment(HAlign_Center);
        Place(BandStack,FAnchors(.5f,1),FVector2D(.5f,1),FVector2D(0,-HudSafeBottom));
        // The toast (Channel C): one line on an ink slab with an orange lead block, wiping in under the top-centre stack.
        Toast=Words(TEXT(""),LDBroadcast::TypeBody,LDBroadcast::White,true);Toast->SetJustification(ETextJustify::Left);Toast->SetWrapTextAt(760.f);
        {
            UOverlay* ToastSlab=Painted(Toast,[Whole](const LDUIPaint::FContext& P)
            {
                const FBox2D Box=Whole(P);
                LDBroadcastPaint::FillSlab(P,P.Layer,Box,LDBroadcast::Ink.CopyWithNewOpacity(.9f));
                LDBroadcastPaint::FillSlab(P,P.Layer+1,FBox2D(Box.Min,FVector2D(Box.Min.X+10.f,Box.Max.Y)),LDBroadcast::Orange);
            },FMargin(28,8,22,8));
            ToastPanel=WidgetTree->ConstructWidget<UBorder>();ToastPanel->SetBrush(FSlateNoResource());ToastPanel->SetPadding(FMargin(0));ToastPanel->SetContent(ToastSlab);
        }
        {
            // The on-air moments over everything but the open panels; never hit-tested.
            auto* Moments=WidgetTree->ConstructWidget<ULDPaintLayer>();Moments->Painter=[this](const LDUIPaint::FContext& P){PaintMoments(P);};
            Moments->SetVisibility(ESlateVisibility::HitTestInvisible);
            UCanvasPanelSlot* MomentSlot=Canvas->AddChildToCanvas(Moments);MomentSlot->SetAnchors(FAnchors(0,0,1,1));MomentSlot->SetOffsets(FMargin(0));
            MomentSlot->SetZOrder(13);
        }
        ToastPanel->SetVisibility(ESlateVisibility::Collapsed);
        ToastSlot=Canvas->AddChildToCanvas(ToastPanel);ToastSlot->SetAnchors(FAnchors(.5f,0));ToastSlot->SetAlignment(FVector2D(.5f,0));ToastSlot->SetAutoSize(true);
        ToastSlot->SetZOrder(14);
        ToastSlot->SetPosition(FVector2D(0,HudToastY));
        {
            // The hovered slot's description (#328 §3 live skill tooltip + Alt/C expandable formula box).
            auto* DetailBox=WidgetTree->ConstructWidget<UVerticalBox>();
            auto FixedWrap=[&](int32 Size){UTextBlock* Line=Text(TEXT(""),Size);Line->SetAutoWrapText(false);Line->SetWrapTextAt(HudDetailWidth-28.f);return Line;};
            ActionDetailName=FixedWrap(LDHUDStyle::TypeLabel);DetailBox->AddChildToVerticalBox(ActionDetailName);
            ActionDetailSummary=FixedWrap(LDHUDStyle::TypeCaption);ActionDetailSummary->SetColorAndOpacity(LDHUDStyle::TextAccent);DetailBox->AddChildToVerticalBox(ActionDetailSummary)->SetPadding(FMargin(0,2,0,0));
            ActionDetailBody=FixedWrap(LDHUDStyle::TypeBody);DetailBox->AddChildToVerticalBox(ActionDetailBody)->SetPadding(FMargin(0,8,0,0));

            ActionFormulaText=FixedWrap(LDHUDStyle::TypeTiny);
            ActionFormulaText->SetColorAndOpacity(FLinearColor(.86f,.91f,.96f));
            ActionFormulaPanel=WidgetTree->ConstructWidget<UBorder>();
            ActionFormulaPanel->SetBrush(HudRound(0.f,FLinearColor(1.f,1.f,1.f,.14f),1.f));
            ActionFormulaPanel->SetBrushColor(LDBroadcast::Panel.CopyWithNewOpacity(.96f));
            ActionFormulaPanel->SetPadding(FMargin(10,8));
            ActionFormulaPanel->SetContent(ActionFormulaText);
            ActionFormulaPanel->SetVisibility(ESlateVisibility::Collapsed);
            DetailBox->AddChildToVerticalBox(ActionFormulaPanel)->SetPadding(FMargin(0,8,0,0));

            ActionDetailFooter=FixedWrap(LDHUDStyle::TypeMicro);
            ActionDetailFooter->SetColorAndOpacity(LDHUDStyle::TextSecondary);
            ActionDetailFooter->SetVisibility(ESlateVisibility::Collapsed);
            DetailBox->AddChildToVerticalBox(ActionDetailFooter)->SetPadding(FMargin(0,6,0,0));

            auto* DetailPanel=WidgetTree->ConstructWidget<UBorder>();DetailPanel->SetBrush(HudRound(0.f,LDBroadcast::White.CopyWithNewOpacity(.35f),1.f));
            DetailPanel->SetBrushColor(LDBroadcast::Ink.CopyWithNewOpacity(.95f));DetailPanel->SetPadding(FMargin(14,12));DetailPanel->SetContent(DetailBox);
            auto* DetailWidth=WidgetTree->ConstructWidget<USizeBox>();DetailWidth->SetWidthOverride(HudDetailWidth);DetailWidth->SetContent(DetailPanel);
            DetailWidth->SetVisibility(ESlateVisibility::Collapsed);
            ActionDetailSlot=Canvas->AddChildToCanvas(DetailWidth);ActionDetailSlot->SetAutoSize(true);ActionDetailSlot->SetAlignment(FVector2D(.5f,1));ActionDetailSlot->SetZOrder(20);
        }
        {
            // #328 §4.2 Channel A: 0.9s lightweight reject micro-tip above the refused action slot or cursor.
            SlotRejectText=Words(TEXT(""),LDBroadcast::TypeSmall,LDBroadcast::White,true);
            SlotRejectPanel=WidgetTree->ConstructWidget<UBorder>();
            SlotRejectPanel->SetBrush(FSlateColorBrush(FLinearColor::White));SlotRejectPanel->SetBrushColor(LDBroadcast::LiveRed);
            SlotRejectPanel->SetPadding(FMargin(10,3));SlotRejectPanel->SetContent(SlotRejectText);
            SlotRejectPanel->SetVisibility(ESlateVisibility::Collapsed);
            SlotRejectSlot=Canvas->AddChildToCanvas(SlotRejectPanel);SlotRejectSlot->SetAutoSize(true);
            SlotRejectSlot->SetAlignment(FVector2D(.5f,1.f));SlotRejectSlot->SetZOrder(22);
        }
        {
            // The rank-up page (#287 spec §12 + #331 3-rank tree preview): two branch cards ([1]/[2]), with Rank 3 sub-nodes
            // beneath 2A and 2B during 1->2 selection so players can hover 3A1/3A2/3B1/3B2 before committing.
            auto* PageBox=WidgetTree->ConstructWidget<UVerticalBox>();
            RankTitle=Text(TEXT(""),LDHUDStyle::TypeBanner);HudFace(RankTitle,true,0);RankTitle->SetJustification(ETextJustify::Center);
            PageBox->AddChildToVerticalBox(RankTitle)->SetHorizontalAlignment(HAlign_Center);
            RankHint=OneLine(TEXT("按 [1] / [2] 选择分支 · 悬停下方三级节点预览终极形态 · [Esc] 关闭"),LDHUDStyle::TypeCaption,0);
            RankHint->SetColorAndOpacity(LDHUDStyle::TextSecondary);RankHint->SetJustification(ETextJustify::Center);
            PageBox->AddChildToVerticalBox(RankHint)->SetPadding(FMargin(0,4,0,0));
            auto* CardRow=WidgetTree->ConstructWidget<UHorizontalBox>();
            RankSubRows.Reset();RankSubChips.Reset();RankSubNames.Reset();RankSubIcons.Reset();
            for(int32 Card=0;Card<2;++Card)
            {
                auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();
                auto* CardBox=WidgetTree->ConstructWidget<UVerticalBox>();
                auto* TopKey=OneLine(FString::Printf(TEXT("[%d]"),Card+1),LDHUDStyle::TypeCaption,0);
                HudFace(TopKey,true,1);TopKey->SetColorAndOpacity(LDHUDStyle::TextAccent);
                CardBox->AddChildToVerticalBox(TopKey)->SetHorizontalAlignment(HAlign_Right);
                auto* Icon=WidgetTree->ConstructWidget<UImage>();
                auto* IconBox=WidgetTree->ConstructWidget<USizeBox>();IconBox->SetWidthOverride(68);IconBox->SetHeightOverride(68);IconBox->SetContent(Icon);
                CardBox->AddChildToVerticalBox(IconBox)->SetHorizontalAlignment(HAlign_Center);
                UTextBlock* Name=Text(TEXT(""),LDHUDStyle::TypeLabel);HudFace(Name,true,0);Name->SetJustification(ETextJustify::Center);
                CardBox->AddChildToVerticalBox(Name)->SetPadding(FMargin(0,6,0,0));
                UTextBlock* Line=Text(TEXT(""),LDHUDStyle::TypeSmall);Line->SetAutoWrapText(true);Line->SetJustification(ETextJustify::Center);
                CardBox->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,6,0,0));
                auto* CardPanel=LDWidgets::Panel(WidgetTree,CardBox,FMargin(14,10));CardPanel->SetBrushColor(FLinearColor(.04f,.06f,.09f,.96f));
                auto* CardSize=WidgetTree->ConstructWidget<USizeBox>();CardSize->SetWidthOverride(260);CardSize->SetHeightOverride(228);CardSize->SetContent(CardPanel);
                ULDCommandButton* Button=LDWidgets::Button(WidgetTree,TEXT(""),[this,Card](){SubmitRankUp(Card);},true,FName(*FString::Printf(TEXT("HUDRankCard%d"),Card)));
                Button->SetContent(CardSize);
                Column->AddChildToVerticalBox(Button);

                auto* SubWrap=WidgetTree->ConstructWidget<UVerticalBox>();
                auto* Connector=OneLine(TEXT("▼ 后续三级终极分化（悬停看完整数值）"),11,0);
                Connector->SetColorAndOpacity(LDHUDStyle::TextSecondary);Connector->SetJustification(ETextJustify::Center);
                SubWrap->AddChildToVerticalBox(Connector)->SetPadding(FMargin(0,6,0,4));
                auto* SubRow=WidgetTree->ConstructWidget<UHorizontalBox>();
                for(int32 Sub=0;Sub<2;++Sub)
                {
                    auto* ChipLine=WidgetTree->ConstructWidget<UHorizontalBox>();
                    UImage* SubIcon=WidgetTree->ConstructWidget<UImage>();
                    auto* SubIconBox=WidgetTree->ConstructWidget<USizeBox>();SubIconBox->SetWidthOverride(22);SubIconBox->SetHeightOverride(22);SubIconBox->SetContent(SubIcon);
                    ChipLine->AddChildToHorizontalBox(SubIconBox)->SetVerticalAlignment(VAlign_Center);
                    UTextBlock* SubName=OneLine(TEXT(""),12,0);SubName->SetColorAndOpacity(LDHUDStyle::TextPrimary);
                    auto* SubNameSlot=ChipLine->AddChildToHorizontalBox(SubName);SubNameSlot->SetPadding(FMargin(5,0,0,0));SubNameSlot->SetVerticalAlignment(VAlign_Center);
                    auto* Chip=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*FString::Printf(TEXT("HUDRankSubChip_%d_%d"),Card,Sub)));
                    Chip->SetBrush(HudRound(5.f,LDHUDStyle::PanelRim,1.f));Chip->SetBrushColor(FLinearColor(.03f,.045f,.07f,.92f));
                    Chip->SetPadding(FMargin(6,5));Chip->SetContent(ChipLine);
                    auto* ChipSize=WidgetTree->ConstructWidget<USizeBox>();ChipSize->SetWidthOverride(126);ChipSize->SetContent(Chip);
                    SubRow->AddChildToHorizontalBox(ChipSize)->SetPadding(FMargin(Sub==0?0:8,0,0,0));
                    RankSubChips.Add(Chip);RankSubNames.Add(SubName);RankSubIcons.Add(SubIcon);
                }
                SubWrap->AddChildToVerticalBox(SubRow);
                SubWrap->SetVisibility(ESlateVisibility::Collapsed);
                Column->AddChildToVerticalBox(SubWrap);
                RankSubRows.Add(SubWrap);

                CardRow->AddChildToHorizontalBox(Column)->SetPadding(FMargin(Card==0?0:24,0,0,0));
                RankCards.Add(Button);RankCardNames.Add(Name);RankCardLines.Add(Line);RankCardIcons.Add(Icon);
            }
            PageBox->AddChildToVerticalBox(CardRow)->SetPadding(FMargin(0,14,0,0));
            ULDCommandButton* Close=LDWidgets::Button(WidgetTree,LDTerms::Close,[this](){RankSlot=INDEX_NONE;MarkReadModelDirty();},true,TEXT("HUDRankClose"));
            PageBox->AddChildToVerticalBox(Close)->SetPadding(FMargin(0,14,0,0));
            RankPage=LDWidgets::Panel(WidgetTree,PageBox,FMargin(24,18));RankPage->SetBrushColor(FLinearColor(.02f,.03f,.05f,.97f));
            RankPage->SetVisibility(ESlateVisibility::Collapsed);
            HitPanels.Add(RankPage);
            auto* PageSlot=Canvas->AddChildToCanvas(RankPage);PageSlot->SetAnchors(FAnchors(.5f,.5f));PageSlot->SetAlignment(FVector2D(.5f,.5f));
            PageSlot->SetAutoSize(true);PageSlot->SetZOrder(25);
        }

        // Bottom-right corner: the attribute view and, in a DEV test room, the test panel as key plates. The build entry is the
        // ring's own closed state beside the strip (ULDWorkshop); the HUD draws no second one.
        auto* CornerRow=WidgetTree->ConstructWidget<UHorizontalBox>();
        {
            // The attribute view (ULDStatBar's grid, a flyout over the strip) and, in a DEV test room only, the test panel.
            auto KeyPlate=[&](const FString& Word,FName Name,TFunction<void()> Callback)
            {
                ULDCommandButton* Button=LDWidgets::Button(WidgetTree,TEXT(""),MoveTemp(Callback),true,Name);
                auto Style=Button->GetStyle();Style.Normal=FSlateNoResource();Style.Hovered=FSlateNoResource();Style.Pressed=FSlateNoResource();
                Style.Disabled=FSlateNoResource();Style.NormalPadding=FMargin(0);Style.PressedPadding=FMargin(0);Button->SetStyle(Style);
                UTextBlock* Text=Words(Word,LDBroadcast::TypeSmall,LDBroadcast::White,true);
                TWeakObjectPtr<ULDCommandButton> Weak(Button);
                UOverlay* Plate=Slabbed(Text,[Weak](){return Weak.IsValid()&&Weak->IsHovered()?LDBroadcast::Raised:LDBroadcast::Ink.CopyWithNewOpacity(.85f);},FMargin(14,0,14,0));
                if(UOverlaySlot* TextSlot=Cast<UOverlaySlot>(Text->Slot)){TextSlot->SetVerticalAlignment(VAlign_Center);}
                Button->SetContent(Fixed(Plate,0.f,HudCornerH*.78f));
                CornerRow->AddChildToHorizontalBox(Button)->SetPadding(FMargin(0,0,6,0));
                if(auto* PlateSlot=Cast<UHorizontalBoxSlot>(Button->Slot)){PlateSlot->SetVerticalAlignment(VAlign_Bottom);}
                return Button;
            };
            ULDCommandButton* Sheet=KeyPlate(TEXT("属性"),TEXT("HUDAttributes"),[this](){ToggleAttributeView();});
            ULDCommandButton* Dev=KeyPlate(TEXT("测试"),TEXT("HUDDevTest"),[PC](){if(PC->RootUI){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::DevTest);}});
            Dev->SetVisibility(ESlateVisibility::Collapsed);
            Commands.Add(TEXT("Attributes"),Sheet);Commands.Add(TEXT("DevTest"),Dev);
        }
        PromptFrame=WidgetTree->ConstructWidget<UBorder>();PromptFrame->SetBrush(FSlateNoResource());PromptFrame->SetPadding(FMargin(0));PromptFrame->SetContent(CornerRow);
        HitPanels.Add(PromptFrame);
        PromptSlot=Canvas->AddChildToCanvas(PromptFrame);PromptSlot->SetAnchors(FAnchors(1,1));PromptSlot->SetAlignment(FVector2D(1,1));PromptSlot->SetAutoSize(true);
        PromptSlot->SetPosition(FVector2D(-HudSafeSide,-HudSafeBottom));
        {
            // #328 §4.2 Channel B: right-side loot/growth queue (max 3 rows, 3.5s independent fade), each a lower third with
            // its kind's ink block (skill, build card, element stone, chain).
            LootQueueBox=WidgetTree->ConstructWidget<UVerticalBox>();
            LootQueueBox->SetVisibility(ESlateVisibility::Collapsed);
            LootQueueCards.Reset();LootQueueDots.Reset();LootQueueTexts.Reset();
            for(int32 Row=0;Row<3;++Row)
            {
                auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
                auto* Dot=WidgetTree->ConstructWidget<UBorder>();Dot->SetBrush(FSlateColorBrush(FLinearColor::White));Dot->SetBrushColor(LDBroadcast::Orange);
                Line->AddChildToHorizontalBox(Fixed(Dot,8.f,0.f))->SetVerticalAlignment(VAlign_Fill);
                UTextBlock* Txt=Words(TEXT(""),LDBroadcast::TypeSmall,LDBroadcast::White,true);Txt->SetWrapTextAt(420.f);
                auto* TxtSlot=Line->AddChildToHorizontalBox(Txt);TxtSlot->SetPadding(FMargin(12,5,14,5));TxtSlot->SetVerticalAlignment(VAlign_Center);
                auto* Card=WidgetTree->ConstructWidget<UBorder>();Card->SetBrush(FSlateColorBrush(FLinearColor::White));
                Card->SetBrushColor(LDBroadcast::Ink.CopyWithNewOpacity(.9f));
                Card->SetPadding(FMargin(0));Card->SetContent(Line);Card->SetVisibility(ESlateVisibility::Collapsed);
                LootQueueBox->AddChildToVerticalBox(Card)->SetPadding(FMargin(0,4,0,0));
                if(auto* VSlot=Cast<UVerticalBoxSlot>(Card->Slot)){VSlot->SetHorizontalAlignment(HAlign_Right);}
                LootQueueCards.Add(Card);LootQueueDots.Add(Dot);LootQueueTexts.Add(Txt);
            }
            LootQueueSlot=Canvas->AddChildToCanvas(LootQueueBox);
            LootQueueSlot->SetAnchors(FAnchors(1,1));LootQueueSlot->SetAlignment(FVector2D(1,1));
            LootQueueSlot->SetAutoSize(true);LootQueueSlot->SetZOrder(15);
            LootQueueSlot->SetPosition(FVector2D(-HudSafeSide,-HudLootBottom));
        }
        {
            // The dragged slot's ghost follows the pointer; it takes no input.
            DragGhostGlyph=Words(TEXT(""),LDBroadcast::TypeTitle,LDBroadcast::White,true);DragGhostGlyph->SetJustification(ETextJustify::Center);
            DragGhost=WidgetTree->ConstructWidget<UBorder>();DragGhost->SetBrush(HudRound(0.f,LDBroadcast::Orange,LDBroadcast::RuleWidth));
            DragGhost->SetHorizontalAlignment(HAlign_Fill);DragGhost->SetVerticalAlignment(VAlign_Fill);
            {
                auto* GhostFace=WidgetTree->ConstructWidget<UOverlay>();
                DragGhostPicture=WidgetTree->ConstructWidget<UImage>();DragGhostPicture->SetVisibility(ESlateVisibility::Collapsed);
                auto* PictureLayer=GhostFace->AddChildToOverlay(DragGhostPicture);PictureLayer->SetHorizontalAlignment(HAlign_Fill);PictureLayer->SetVerticalAlignment(VAlign_Fill);
                auto* GlyphLayer=GhostFace->AddChildToOverlay(DragGhostGlyph);GlyphLayer->SetHorizontalAlignment(HAlign_Center);GlyphLayer->SetVerticalAlignment(VAlign_Center);
                DragGhost->SetContent(GhostFace);
            }
            auto* GhostBox=Fixed(DragGhost,HudSlotSide,HudSlotSide);
            GhostBox->SetRenderOpacity(.8f);GhostBox->SetVisibility(ESlateVisibility::Collapsed);
            DragGhostSlot=Canvas->AddChildToCanvas(GhostBox);DragGhostSlot->SetAutoSize(true);DragGhostSlot->SetAlignment(FVector2D(.5f,.5f));DragGhostSlot->SetZOrder(30);
        }

        // The build ring (ULDWorkshop) draws every build decision; the 构筑 panel and the card storage below hold its commands.
        Workshop=CreateWidget<ULDWorkshop>(PC);
        if(Workshop)
        {
            auto* WorkshopSlot=Canvas->AddChildToCanvas(Workshop);WorkshopSlot->SetAnchors(FAnchors(0,0,1,1));WorkshopSlot->SetOffsets(FMargin(0));WorkshopSlot->SetZOrder(8);
            Workshop->RequestClose=[this]()
            {
                auto* Owner=Cast<ALDPlayerController>(GetOwningPlayer());
                if(Owner&&Owner->RootUI&&ChoicesPanel&&ChoicesPanel->IsTrayOpen()){Owner->RootUI->ToggleMainPanel(ELDUIMainPanel::Choices);}
            };
            Workshop->RequestOpen=[this](){if(ChoicesPanel&&!ChoicesPanel->IsTrayOpen()){ToggleWorkshop();}};
            Workshop->Notify=[this](const FString& Notice){ShowToast(Notice);};
            // The ring's ground darkening lies under the band, the minimap and the rest of the HUD chrome.
            WorkshopGround=CreateWidget<ULDBuildRingGround>(PC);
            if(WorkshopGround)
            {
                WorkshopGround->Ring=Workshop;
                auto* GroundSlot=Canvas->AddChildToCanvas(WorkshopGround);GroundSlot->SetAnchors(FAnchors(0,0,1,1));GroundSlot->SetOffsets(FMargin(0));GroundSlot->SetZOrder(-1);
            }
        }
        else{UE_LOG(LogLDUI,Error,TEXT("LD_HUD_WORKSHOP_CREATION_FAILED"));}
        // The main panels the root opens one at a time. The 构筑 panel draws nothing (the ring shows its decisions), so only the
        // DEV panel joins the canvas, filling it with a hit-transparent frame so the world beside it keeps its input.
        ChoicesPanel=CreateWidget<ULDChoicesPanel>(PC);
        DevTestPanel=CreateWidget<ULDDevTestPanel>(PC);
        const TPair<ELDUIMainPanel,ULDPanel*> Docked[]={{ELDUIMainPanel::Choices,ChoicesPanel.Get()},{ELDUIMainPanel::DevTest,DevTestPanel.Get()}};
        for(const TPair<ELDUIMainPanel,ULDPanel*>& Entry:Docked)
        {
            if(!Entry.Value){UE_LOG(LogLDUI,Error,TEXT("LD_HUD_PANEL_CREATION_FAILED %d"),int32(Entry.Key));continue;}
            if(Entry.Value==DevTestPanel)
            {auto* PanelSlot=Canvas->AddChildToCanvas(Entry.Value);PanelSlot->SetAnchors(FAnchors(0,0,1,1));PanelSlot->SetOffsets(FMargin(0));PanelSlot->SetZOrder(10);}
            if(PC->RootUI){PC->RootUI->RegisterMainPanel(Entry.Key,Entry.Value);}
        }
        // #227: the card storage's commands (installs, moves, replaces); the ring draws the library and the installed cards.
        Storage=NewObject<ULDComponentStorage>(this);
        Storage->Notify=[this](const FString& Notice){ShowToast(Notice);};
        if(ChoicesPanel){ChoicesPanel->Notify=[this](const FString& Notice){ShowToast(Notice);};}
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
    return Areas;
}
bool ULDHUD::IsUIAt(FVector2D P) const
{
    if(Minimap&&Minimap->IsDragging()){return true;}
    if(Workshop&&Workshop->IsHit(P)){return true;}
    if(DeathBanner&&DeathBanner->IsVisible())
    {
        if(DeathWorkshopButton&&DeathWorkshopButton->IsVisible()&&DeathWorkshopButton->GetCachedGeometry().IsUnderLocation(P)){return true;}
        if(DeathRecapToggle&&DeathRecapToggle->IsVisible()&&DeathRecapToggle->GetCachedGeometry().IsUnderLocation(P)){return true;}
    }
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
    // The build ring opens whenever the own match's HUD shows: the hand and the build are there to tend, not only offers.
    const bool bOpen=ChoicesPanel->IsTrayOpen();
    const bool bReady=Workshop&&Workshop->IsVisible();
    if(bOpen||bReady){PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Choices);}
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
void ULDHUD::FreezeForWorldTeardown()
{
    bFrozenForTeardown=true;
    bPKThrowArmed=false;
    bDragging=false;
    DragFrom=INDEX_NONE;
    if(Minimap){Minimap->FreezeForWorldTeardown();Minimap->SetVisibility(ESlateVisibility::Collapsed);}
    for(ULDPanel* Open:MainPanels()){if(Open&&Open->IsPanelOpen()){Open->ClosePanel();}}
    SetVisibility(ESlateVisibility::Collapsed);
}

void ULDHUD::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    UWorld* World=GetWorld();
    const auto* GI=World?World->GetGameInstance<ULDGameInstance>():nullptr;
    if(bFrozenForTeardown||!World||World->bIsTearingDown||(GI&&GI->IsLeavingWorld(World))){return;}
    UpdateDrag();
    // The stat bar usually sits in a closed flyout, where Slate does not tick it: its queued points still leave on time.
    if(StatBar){StatBar->Pump();}
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if (!PC || !DiagnosticsPanel) { return; }
    const double Now=FPlatformTime::Seconds();
    // #337 §3.3: automatic compact yellow network spike warning on remote clients (RTT >= 180 ms, Loss >= 5%, or Corrections >= 3/s for >= 2s; hides after 5s calm).
    if (NetSpikePanel)
    {
        if (World->GetNetMode()==NM_Client)
        {
            const auto* Driver=World->GetNetDriver();
            const auto* Connection=Driver?Driver->ServerConnection.Get():nullptr;
            if (Connection && Connection->GetLastRecvAckTime()>0 && NetSpikeTickAt>0 && Now-NetSpikeTickAt>=.25)
            {
                const double Elapsed=Now-NetSpikeTickAt;
                const double RttMs=(Connection->AvgLag>0 ? Connection->AvgLag : Connection->RawPingInSeconds)*1000.;
                const double LossPct=100.*FMath::Max(Connection->GetInLossPercentage().GetLossPercentage(),Connection->GetOutLossPercentage().GetLossPercentage());
                auto* Body=Cast<ALDCharacter>(PC->GetPawn());
                const auto* Movement=Body?Cast<ULDCharacterMovementComponent>(Body->GetCharacterMovement()):nullptr;
                const double CorrRate=(Movement && NetSpikeBody.Get()==Body) ? double(Movement->CorrectionCount-NetSpikeCorrections)/Elapsed : 0.;
                NetSpikeBody=Body;
                NetSpikeCorrections=Movement?Movement->CorrectionCount:0;
                NetSpikeTickAt=Now;
                const bool bBad=RttMs>=180. || LossPct>=5. || CorrRate>=3.;
                if (bBad)
                {
                    NetSpikeGoodSince=-1.;
                    if (NetSpikeBadSince<0.) { NetSpikeBadSince=Now; }
                    if (Now-NetSpikeBadSince>=2.)
                    {
                        bNetSpikeShown=true;
                        HudSetText(NetSpikeText,FString::Printf(TEXT("⚠ 网络波动 · %.0f ms · 丢包 %.0f%%"),RttMs,LossPct));
                        if (!bNetSpikeLogged)
                        {
                            bNetSpikeLogged=true;
                            UE_LOG(LogLDMatch,Warning,TEXT("LD_NET_SPIKE rtt_ms=%.0f loss_pct=%.1f corr_per_s=%.1f"),RttMs,LossPct,CorrRate);
                        }
                    }
                }
                else
                {
                    NetSpikeBadSince=-1.;
                    if (bNetSpikeShown)
                    {
                        if (NetSpikeGoodSince<0.) { NetSpikeGoodSince=Now; }
                        else if (Now-NetSpikeGoodSince>=5.) { bNetSpikeShown=false; bNetSpikeLogged=false; }
                    }
                }
            }
            else if (NetSpikeTickAt<=0.)
            {
                NetSpikeTickAt=Now;
                auto* Body=Cast<ALDCharacter>(PC->GetPawn());
                const auto* Movement=Body?Cast<ULDCharacterMovementComponent>(Body->GetCharacterMovement()):nullptr;
                NetSpikeBody=Body;
                NetSpikeCorrections=Movement?Movement->CorrectionCount:0;
            }
            HudSetShown(NetSpikePanel,bNetSpikeShown,ESlateVisibility::HitTestInvisible);
        }
        else
        {
            bNetSpikeShown=false;bNetSpikeLogged=false;
            HudSetShown(NetSpikePanel,false,ESlateVisibility::HitTestInvisible);
        }
    }
    const bool Network=PC->Controls && PC->Controls->bShowNetworkDiagnostics;
    const bool FPS=PC->Controls && PC->Controls->bShowFPS;
    HudSetShown(DiagnosticsPanel,Network||FPS,ESlateVisibility::HitTestInvisible);
    if (!Network && !FPS)
    {
        DiagnosticsTickAt=-1; FrameDiagnostics={}; DiagnosticsSampleAt=0;
        DiagnosticsBody.Reset(); DiagnosticsCorrections=0;
        return;
    }
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
    UWorld* World=GetWorld();
    auto* GI=World?World->GetGameInstance<ULDGameInstance>():nullptr;
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    const auto* GS=World?World->GetGameState<ALDGameState>():nullptr;
    if(bFrozenForTeardown||!World||World->bIsTearingDown||(GI&&GI->IsLeavingWorld(World))||!PC||!GS||!Band){return;}
    if(GI){GI->NoteCrashMatchState(GS->PublicMatch.Generation,uint8(GS->PublicMatch.Phase),uint8(GS->PublicMatch.Timeline.Step));}
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
    UWorld* World=GetWorld();
    const auto* GI=World?World->GetGameInstance<ULDGameInstance>():nullptr;
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    const auto* GS=World?World->GetGameState<ALDGameState>():nullptr;
    if(bFrozenForTeardown||!World||World->bIsTearingDown||(GI&&GI->IsLeavingWorld(World))||!PC||!GS||!Band){return;}
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
        const bool bAltKey=PC.IsInputKeyDown(EKeys::LeftAlt)||PC.IsInputKeyDown(EKeys::RightAlt);
        Put(Held(TEXT("Details")),1);Put(Held(TEXT("Scoreboard")),1);Put(bAltKey,1);
        ELDCombatEntry Armed=ELDCombatEntry::None;Put(PC.ReadArmedCombatEntry(Armed),1);Put(uint64(Armed),8);
        for(const ELDCombatEntry Entry:PC.SkillAtKey){Put(uint64(Entry),8);}
        Put(PC.BuildBlueprints&&PC.BuildBlueprints->IsViewerOpen(),1);
        Put(ChoicesPanel&&ChoicesPanel->IsPanelOpen(),1);Put(ChoicesPanel&&ChoicesPanel->IsTrayOpen(),1);
        Put(Workshop&&Workshop->IsOpen(),1);Put(bDragging,1);
        Put(PC.Controls&&PC.Controls->bColorblindMode,1);
        int64 NowMs=0;Put(PC.ReadDemoAuthorityTimeMs(NowMs),1);
        // Owner statistics that arrive by RPC (the low bits of their serials are enough to notice a new one).
        Put(PC.BuildStats.Serial,3);Put(PC.TrainingCombo.Serial,4);
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
    const bool bAutoWorkshopPhase=LDWorkshop::FullScreen(bRestPhase,Public.Timeline.Step,bDeathWait);
    const bool bWorkshopOpen=bShow&&ChoicesPanel&&ChoicesPanel->IsTrayOpen();
    // The build ring grows around the character: the strip, the minimap and the score bug stay in play while it is open.
    HudSetShown(Band,bShow,ESlateVisibility::Visible);
    HudSetShown(PromptFrame,bShow&&!bWorkshopOpen,ESlateVisibility::Visible);HudSetShown(MapColumn,bShow,ESlateVisibility::Visible);
    HudSetShown(PhaseBanner,bShow,ESlateVisibility::SelfHitTestInvisible);
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
        if(Workshop){Workshop->Present(ULDWorkshop::FInput());}
        if(Storage){Storage->Present(nullptr,O.Match,LDProgression::EMatchStage::Closed,O.LifePhase,nullptr,false);}
        for(UWidget* Hidden:{static_cast<UWidget*>(TargetCard.Get()),static_cast<UWidget*>(DeathBanner.Get()),static_cast<UWidget*>(ToastPanel.Get()),static_cast<UWidget*>(EventFeedPanel.Get()),
            static_cast<UWidget*>(ReturnCountdownText.Get()),static_cast<UWidget*>(RestActionsPanel.Get()),static_cast<UWidget*>(PKPanelBorder.Get()),static_cast<UWidget*>(PKCompactBar.Get()),
            static_cast<UWidget*>(SpawnChoicePanel.Get()),static_cast<UWidget*>(StatFlyout.Get()),static_cast<UWidget*>(SlotRejectPanel.Get()),static_cast<UWidget*>(LootQueueBox.Get()),
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
    LDWorldFeedback::FSample OwnSample;OwnSample.Match=O.Match;OwnSample.Life=O.Life;OwnSample.LifePhase=O.LifePhase;OwnSample.Self=O.ContenderId;OwnSample.OwnCombat=OwnCombat;
    // The same tracker tells the own kills and level-ups for the on-air moments (PaintMoments).
    OwnSample.Progression=PC->ReadOwnProgression();OwnSample.KillFeed=&GS->KillFeed;OwnSample.PublicGeneration=Public.Generation;
    OwnSample.bClock=bClock;OwnSample.NowMs=NowMs;
    TArray<LDWorldFeedback::FEvent> BodyEvents;OwnHitWatch.Observe(OwnSample,BodyEvents);
    for(const LDWorldFeedback::FEvent& Event:BodyEvents)
    {
        if(Event.Kind==LDWorldFeedback::EKind::Kill)
        {
            KillMomentAt=FPlatformTime::Seconds();KillMomentVictimSeat=INDEX_NONE;
            // The victim's seat from the newest public notice naming the own kill.
            const FLDKillNotice* Newest=nullptr;
            for(const FLDKillNotice& Notice:GS->KillFeed.Notices){if(Notice.Killer==O.ContenderId&&(!Newest||Notice.Serial>Newest->Serial)){Newest=&Notice;}}
            if(Newest){KillMomentVictimSeat=LDHUD::SeatOf(Lobby,Newest->Victim);}
        }
        else if(Event.Kind==LDWorldFeedback::EKind::LevelUp){LevelFlipAt=FPlatformTime::Seconds();LevelFlipLevel=Event.Level;LevelFlipPoints=Event.Amount;}
    }
    const bool bOwnHit=BodyEvents.ContainsByPredicate([](const LDWorldFeedback::FEvent& Event)
    {return Event.bOwn&&(Event.Kind==LDWorldFeedback::EKind::DamageTaken||Event.Kind==LDWorldFeedback::EKind::ShieldAbsorbed);});
    const bool bRestPlanning=Public.Phase==ELDMatchPhase::Rest&&Public.Timeline.Step==ELDRestStep::Build;
    HudSetText(PhaseText,Circle.Head);
    const bool bPhaseLine=!bChoicesRestOpen&&(Public.Phase!=ELDMatchPhase::Development||Public.Showdown.bStarted)&&Public.Phase!=ELDMatchPhase::Rest;
    HudSetShown(PhaseText,bPhaseLine,ESlateVisibility::HitTestInvisible);
    HudSetText(PhaseOwnerText,Circle.Owner);HudSetText(PhaseAsideText,Circle.Aside);
    HudSetShown(PhaseFootRow,bPhaseLine&&!(Circle.Owner.IsEmpty()&&Circle.Aside.IsEmpty()),ESlateVisibility::HitTestInvisible);
    CircleFraction=Circle.Progress;
    HudSetText(DevMarkerText,!Lobby.DevTestSession&&!Lobby.DevContentReload?FString()
        :FString(Lobby.DevTestSession?TEXT("测试模式"):TEXT("开发房间"))+(Lobby.DevContentReload?TEXT(" · 内容热重载"):TEXT("")));
    HudSetShown(DevMarkerText,Lobby.DevTestSession||Lobby.DevContentReload,ESlateVisibility::HitTestInvisible);
    HudSetShown(CircleSpot,Circle.bProgress,ESlateVisibility::HitTestInvisible);
    {
        const LDHUD::FTimelineView Timeline=LDHUD::TimelineBanner(Public,O.ContenderId,bClock,NowMs);
        // The score bug carries the beat and its clock; under it only the rest's step (or the closed final field) remains.
        ShownBug=LDHUD::ScoreBug(Public,bClock,NowMs);
        HudSetText(ScoreLabelText,ShownBug.bKnown?ShownBug.Label:FString(TEXT("同步中")));
        HudSetText(ScoreClockText,ShownBug.Clock.IsEmpty()?FString(TEXT("—")):ShownBug.Clock);
        ShownPlates=LDHUD::SeatPlates(Lobby,Public,GS->KillFeed,O.ContenderId,bClock,NowMs);
        bReadModelLive|=ShownPlates.ContainsByPredicate([](const LDHUD::FSeatPlateView& Seat){return Seat.bRecentKO;});
        const FString StepLine=Public.Timeline.Stage==ELDMatchStage::Rest
            ?LDHUD::RestStepText(Public.Timeline.Step)+(Public.Timeline.bCountdownPaused?TEXT(" · 计时已暂停"):TEXT(""))
            :Public.Timeline.Stage==ELDMatchStage::FinalField&&!Public.Showdown.bStarted?Timeline.Headline:FString();
        HudSetText(TimelineText,StepLine);HudSetShown(TimelineText,!StepLine.IsEmpty(),ESlateVisibility::HitTestInvisible);
        {
            // The detail states facts only: the sign-up instruction (stand in the disk) is left to the disk itself.
            FString Detail=Timeline.Detail;
            Detail.ReplaceInline(*(FString(TEXT(" · "))+LDTerms::NotSignedUpSelf),TEXT(""));
            HudSetText(TimelineDetailText,Detail);
        }
        HudSetShown(TimelineDetailText,LDHUD::TimelineDetailVisible(Public,Timeline,bChoicesRestOpen),ESlateVisibility::HitTestInvisible);
        // The reset exists only in a rest's open steps (build and settlement/payout); leaving them drops a wait the server will still answer.
        const bool bRestActions=bRestPlanning||(Public.Phase==ELDMatchPhase::Rest&&(Public.Timeline.Step==ELDRestStep::Settlement||Public.Timeline.Step==ELDRestStep::Payout));
        if(!bRestActions){bTrainingResetInFlight=false;bRestReadyInFlight=false;}
        HudSetShown(RestActionsPanel,bRestActions,ESlateVisibility::Visible);
        const FString ReadyLabel=bRestReadyInFlight?TEXT("提交中…"):O.bRestReady?FString(LDTerms::RestReadyCancel):FString(LDTerms::RestReadyCommit);
        if(bRestActions)
        {
            if(TrainingResetButton->GetIsEnabled()==bTrainingResetInFlight){TrainingResetButton->SetIsEnabled(!bTrainingResetInFlight);}
            // Orange while it can mark the player ready; a white-ruled panel once ready (the press then cancels).
            HudSetText(RestReadyCaption,ReadyLabel);
            RestReadyButton->SetIsEnabled(O.bCanRestReady&&!bRestReadyInFlight);
            bRestReadyShown=O.bRestReady;
            HudSetColor(RestReadyCaption,O.bRestReady?LDBroadcast::White:LDBroadcast::Ink);
            HudSetShown(RestReadyButton,bRestPlanning,ESlateVisibility::Visible);
            const bool bThisRest=RestAnswerMatch==Public.Generation&&RestAnswerRest==Public.Timeline.RestIndex;
            HudSetText(RestAnswerText,bThisRest?RestAnswer:FString());
            HudSetShown(RestAnswerText,bThisRest&&!RestAnswer.IsEmpty(),ESlateVisibility::HitTestInvisible);
        }
        SyncSignupMarker(Public,O.Match,bRestActions&&Timeline.bSignupStep,Timeline.bSignupOpen,Timeline.bSignedUp);
        // A finished training combo on the dummy: each source and its damage, largest first, beside the rest actions.
        // Live per-hit updates (!bFinished) drive ULDWorldLabels' overhead dummy board without spamming HUD toasts (#331).
        if(const FLDTrainingComboView& Combo=PC->TrainingCombo;Combo.Match==O.Match&&Combo.Serial!=SeenTrainingCombo)
        {
            SeenTrainingCombo=Combo.Serial;
            if(Combo.bFinished)
            {
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
            HudSetText(Cast<UTextBlock>(Button->GetContent()),
                FString::Printf(TEXT("[%d] %s"),Index+1,*LDTerms::SpawnCandidate(Index)));
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
                HudSetShown(DailyBadges[Index]->GetParent(),bShown,ESlateVisibility::Visible);
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
    HudSetShown(DeathBanner,Death.bVisible&&!(bWorkshopOpen),ESlateVisibility::SelfHitTestInvisible);
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
        if(DeathRecapToggle)
        {
            HudSetShown(DeathRecapToggle,Recap.bAvailable,ESlateVisibility::Visible);
            HudSetText(Cast<UTextBlock>(DeathRecapToggle->GetContent()),bDeathRecapCollapsed?TEXT("展开 ▼"):TEXT("收起 ▲"));
        }
        HudSetShown(DeathRecapContainer,Recap.bAvailable&&!bDeathRecapCollapsed,ESlateVisibility::HitTestInvisible);
        // Each row is its icon chain (who triggered → what it consumed → what it produced), rebuilt only when it changes;
        // new text takes the current text scale, later scale changes rescale it with the rest of the HUD.
        const float Scale=AppliedScale>0?AppliedScale:1.f;
        for(int32 Row=0;Row<DeathRecapRows.Num();++Row)
        {
            const bool bRow=Recap.bAvailable&&!bDeathRecapCollapsed&&Recap.Chains.IsValidIndex(Row);
            const LDHUD::FRecapChain Chain=bRow?Recap.Chains[Row]:LDHUD::FRecapChain();
            if(DeathRecapShown.IsValidIndex(Row)&&!(DeathRecapShown[Row]==Chain))
            {
                DeathRecapShown[Row]=Chain;
                LDRecapChain::Fill(WidgetTree,DeathRecapRows[Row],Chain,FMath::RoundToFloat(20.f*Scale),FMath::RoundToInt(14.f*Scale));
            }
            HudSetShown(DeathRecapRows[Row],bRow,ESlateVisibility::HitTestInvisible);
        }
        if(DeathWorkshopButton)
        {
            const bool bCanOpenBuild=bDeathWait&&!bRestPhase&&!bWorkshopOpen;
            HudSetShown(DeathWorkshopButton,bCanOpenBuild,ESlateVisibility::Visible);
            if(bCanOpenBuild)
            {
                const FString KeyName=PC->BindingLabel(TEXT("Workshop"));
                const FString LeftSec=(bClock&&O.RespawnAtMs>NowMs)
                    ?FString::Printf(TEXT("（%.1fs 后复活）"),double(O.RespawnAtMs-NowMs)/1000.)
                    :FString();
                HudSetText(Cast<UTextBlock>(DeathWorkshopButton->GetContent()),
                    FString::Printf(TEXT("按 [%s] 趁复活调整构筑%s"),*KeyName,*LeftSec));
            }
        }
    }

    const FString Name=LDHUD::RosterName(Lobby,O.ContenderId);
    // The seat block shows only the jersey number; its tooltip names the own roster name.
    {
        const FString Tip=Name.IsEmpty()?FString(TEXT("名单同步中")):Name;
        if(!Portrait->GetToolTipText().ToString().Equals(Tip,ESearchCase::CaseSensitive)){Portrait->SetToolTipText(FText::FromString(Tip));}
    }
    StripPaint.Seat=LDHUD::SeatOf(Lobby,O.ContenderId);
    if(const auto* Body=Cast<ALDCharacter>(PC->GetPawn()))
    {
        const FLinearColor Identity=LDHUD::PaletteColor(Body->PresentationKey);
        if(Portrait->GetBrushColor()!=Identity){Portrait->SetBrushColor(Identity);}
        if(StripPaint.Seat==INDEX_NONE){StripPaint.Seat=Body->PresentationKey;}
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
    const bool bColorblind=PC->Controls&&PC->Controls->bColorblindMode;
    {
        // The health figure leads the strip; its bar is white, live red (colour blind: gold) once low, and cut every 250 HP
        // (1000 for a tall pool) so its blocks can be counted. A shield runs on after the fill and is named after the regen.
        const int64 MaxP2=OwnCombat&&OwnCombat->bHasBody?OwnCombat->MaxHPP2:int64(FMath::RoundToDouble(O.MaxHp*100.));
        StripPaint.bHealth=Health.bKnown;StripPaint.Health=Health.Fraction;
        StripPaint.Shield=Health.bKnown&&MaxP2>0?float(FMath::Clamp(ShieldPoints*100./double(MaxP2),0.,1.)):0.f;
        const bool bLow=Health.bKnown&&Health.Fraction<=.3f;
        StripPaint.HealthInk=bLow?(bColorblind?LDBroadcast::Gold:LDBroadcast::LiveRed):LDBroadcast::White;
        const double Step=MaxP2>0&&MaxP2<=600000?25000.:100000.;
        StripPaint.HealthSegment=MaxP2>0?float(FMath::Min(1.,Step/double(MaxP2))):0.f;
        HudSetText(HealthText,Health.bKnown?Health.Now:FString(TEXT("—")));
        HudSetText(HealthMaxText,Health.bKnown?TEXT("/")+Health.Max:FString(TEXT("同步中")));
        HudSetColor(HealthText,bLow?StripPaint.HealthInk:LDBroadcast::White);
        if(HealthText->GetToolTipText().ToString()!=Health.Text){HealthText->SetToolTipText(FText::FromString(Health.Text));}
    }
    // A bar at the own overdrive threshold (OVERDRIVE.Threshold, or Y11's lower one) glows overdrive gold and says so; the bar
    // has no rate text (momentum is no steady regeneration).
    const bool bFullBar=Momentum.bKnown&&OwnCombat&&OwnCombat->OverdriveThresholdP2>0&&OwnCombat->MomentumP2>=OwnCombat->OverdriveThresholdP2;
    StripPaint.bMomentum=Momentum.bKnown;StripPaint.Momentum=Momentum.Fraction;StripPaint.bOverdrive=bFullBar;
    StripPaint.Notch=OwnCombat&&OwnCombat->MaxMomentumP2>0&&OwnCombat->OverdriveThresholdP2>0
        ?float(FMath::Clamp(double(OwnCombat->OverdriveThresholdP2)/double(OwnCombat->MaxMomentumP2),0.,1.)):-1.f;
    HudSetText(MomentumText,Momentum.bKnown?Momentum.Now:FString(TEXT("—")));
    HudSetColor(MomentumText,bFullBar?LDBroadcast::Gold:LDBroadcast::White);
    {
        const FString MomentumTip=bFullBar?Momentum.Text+TEXT(" · ")+LDTerms::Overdrive:Momentum.Text;
        if(MomentumText->GetToolTipText().ToString()!=MomentumTip){MomentumText->SetToolTipText(FText::FromString(MomentumTip));}
    }
    {
        FString Aside=LDHUD::RegenText(O,OwnCombat,true);
        if(Health.bKnown&&ShieldPoints>0){Aside=FString::Printf(TEXT("%s %.0f"),LDTerms::Shield,FMath::CeilToDouble(ShieldPoints))+(Aside.IsEmpty()?FString():TEXT("  ")+Aside);}
        HudSetText(HealthRegenText,Aside);
    }
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
                PushLootNotice(LDHUD::NewChainNotice(DisplayDefinitions(),End.Key),3);
            }
        }
        if(bChanged&&!LDChainCodex::Save(Codex)){UE_LOG(LogLDUI,Warning,TEXT("LD_UI_CHAIN_CODEX_SAVE_FAILED entries=%d"),Codex.Num());}
    }

    // #328 §4.2 Channel B: observe newly gained blueprints and non-medal build cards into the loot queue (medals use MedalDropWatch below).
    if(Progression&&Progression->bValid&&Progression->Generation==O.Match)
    {
        if(LootSeenMatch!=O.Match)
        {
            LootSeenMatch=O.Match;
            LootKnownBlueprints.Reset();
            LootKnownCardSerials.Reset();
            for(const FName Bp:Progression->PendingBlueprints){LootKnownBlueprints.Add(Bp);}
            for(const FLDCardInstanceView& C:Progression->Cards){LootKnownCardSerials.Add(C.Serial);}
        }
        else
        {
            for(const FName Bp:Progression->PendingBlueprints)
            {
                if(!LootKnownBlueprints.Contains(Bp))
                {
                    LootKnownBlueprints.Add(Bp);
                    PushLootNotice(FString::Printf(TEXT("获得技能图纸 · %s"),*LDHUD::ContentName(Content,Bp.ToString())),0);
                }
            }
            for(const FLDCardInstanceView& C:Progression->Cards)
            {
                if(!LootKnownCardSerials.Contains(C.Serial))
                {
                    LootKnownCardSerials.Add(C.Serial);
                    const bool bStone=(Content&&LDProgression::IsMedal(*Content,C.DefinitionId))||C.DefinitionId.ToString().StartsWith(TEXT("medal_"));
                    if(!bStone)
                    {
                        PushLootNotice(FString::Printf(TEXT("获得构筑卡牌 · %s"),*LDHUD::ContentName(Content,C.DefinitionId.ToString())),1);
                    }
                }
            }
        }
    }

    // New world commands follow Combat's input half (menu/Settings, child confirmation or text edit, held keys);
    // Combat itself refuses and cancels any armed preview while input is captured.
    const bool bInputSuppressed=PC->IsWorldInputSuppressed();
    const FKey DetailsKey=PC->BindingKey(TEXT("Details"));
    const bool bDetailsHeld=bOwnMatch&&Playing&&DetailsKey.IsValid()&&PC->IsInputKeyDown(DetailsKey)&&!PC->IsWorldInputCaptured();
    if(StatBar){StatBar->SetDetailsHeld(bDetailsHeld);}

    const LDProgression::EMatchStage BuildStage=bOwnMatch?LDProgression::StageForPublic(Public.Phase,Public.Timeline):LDProgression::EMatchStage::Closed;
    if(ChoicesPanel)
    {
        // #147: the cards show this level's numbers; while no projection is published (dead) the last stats stay in use.
        if(OwnCombat&&!OwnCombat->Attributes.IsEmpty()){ChoiceStats=LDHUD::CardStatsOf(Progression?Progression->Level:1,OwnCombat->Attributes);}
        else if(Progression){ChoiceStats.Level=FMath::Max<uint8>(Progression->Level,1);}
        // Holding Alt spells out the scaling on every card face, the workshop's as well as the skill bar's.
        ChoiceStats.bShowScalingFormula=FSlateApplication::IsInitialized()&&FSlateApplication::Get().GetModifierKeys().IsAltDown();
        // The 构筑 panel draws nothing (the build ring shows its decisions); its command guards follow the view every frame.
        ChoicesPanel->Present(Progression?*Progression:FLDProgressionView(),O.Match,O.Life,O.LifePhase,BuildStage,
            Content,HudSlotKeys(*PC),bOwnMatch&&OwnCombat&&OwnCombat->bDormRegenActive);
    }
    if(StatBar)
    {
        StatBar->Present(Progression,O.Match,O.Life,BuildStage,O.LifePhase,Content,OwnCombat?OwnCombat->Attributes:TArray<FLDCombatAttributeRowView>(),
            bOwnMatch&&Progression!=nullptr,DisplayContent.Get(),OwnCombat);
    }
    if(Storage){Storage->Present(Progression,O.Match,BuildStage,O.LifePhase,Content,bOwnMatch&&Progression!=nullptr);}
    if(Workshop)
    {
        // The build ring: open while the 构筑 tray is open; otherwise its closed state under the character and by the band.
        ULDWorkshop::FInput In;
        In.Mode=bWorkshopOpen?ULDWorkshop::EMode::Open:ULDWorkshop::EMode::Closed;
        In.bShown=bOwnMatch&&Progression!=nullptr;
        In.Progression=Progression;In.Match=O.Match;In.Content=Content;In.ContentSnapshot=DisplayContent.Get();In.Combat=OwnCombat;
        In.Stage=BuildStage;In.Stats=ChoiceStats;In.Keys=HudSlotKeys(*PC);
        In.DashKey=PC->BindingLabel(TEXT("Dash"));In.OpenKey=PC->BindingLabel(TEXT("Workshop"));
        In.Attributes=OwnCombat?OwnCombat->Attributes:TArray<FLDCombatAttributeRowView>();
        In.bRest=bRestPhase;In.bDead=bDeathWait;
        const auto RingRect=[](const UWidget* Widget){return Widget&&Widget->IsVisible()?Widget->GetCachedGeometry().GetLayoutBoundingRect():FSlateRect();};
        // The top HUD block (the banner, and in a rest its actions): the ring's dim and shop wing keep clear of it.
        In.Band=RingRect(Band);In.Map=RingRect(MapColumn);In.Banner=RingRect(TopCentreSlot?TopCentreSlot->Content.Get():PhaseBanner.Get());
        // The Q, W, E and D faces: a linked skill's face takes the ring's link rule.
        for(const FLDHUDActionSlot& Entry:ActionSlots){In.Skills.Add(RingRect(Entry.Icon.Get()));}
        In.Toast=RingRect(ToastPanel);
        if(RestActionsPanel&&RestActionsPanel->IsVisible()&&TopCentreSlot&&TopCentreSlot->Content){In.Actions=TopCentreSlot->Content->GetDesiredSize();}
        Workshop->Present(In,&GetCachedGeometry());
    }
    if(ChoicesPanel&&PC->RootUI&&bOwnMatch)
    {
        // #331: field death never auto-opens the full-screen Workshop (so SpawnChoice + minimap stay unobstructed); if the
        // player manually opens Workshop during death, respawn closes it immediately. Entering R1-R3 Build auto-opens the
        // full-screen Workshop with Rest Shop side-by-side.
        const bool bFieldDead=bDeathWait&&!bRestPhase;
        if(bFieldDead&&ChoicesPanel->IsTrayOpen()){bWorkshopOpenedDuringDeath=true;}
        if(!ChoicesPanel->IsTrayOpen()){bWorkshopAutoOpened=false;bWorkshopOpenedDuringDeath=false;}
        const bool bRespawnedFromDeath=bWorkshopSeenDead&&!bFieldDead;
        if((bRespawnedFromDeath&&(bWorkshopAutoOpened||bWorkshopOpenedDuringDeath))
            ||(bWasAutoWorkshopPhase&&!bAutoWorkshopPhase&&bWorkshopAutoOpened))
        {
            if(ChoicesPanel->IsTrayOpen()&&!ChoicesPanel->HasPendingRequest()&&!(Storage&&Storage->HasPendingRequest()))
            {
                PC->RootUI->ToggleMainPanel(ELDUIMainPanel::Choices);
                bWorkshopAutoOpened=false;
                bWorkshopOpenedDuringDeath=false;
            }
        }
        if(bRestPlanning&&!bWorkshopSeenRestBuild&&!ChoicesPanel->IsPanelOpen())
        {
            AutoOpenSince=GetWorld()->GetRealTimeSeconds();
        }
        bWorkshopSeenDead=bFieldDead;
        bWorkshopSeenRestBuild=bRestPlanning;
        bWasAutoWorkshopPhase=bAutoWorkshopPhase;
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
        // #198 G: field rewards and element medal drops enter Channel B loot queue (#328 §4.2) so they never clobber broadcasts.
        for(const FLDFieldRewardView& Reward:FieldRewardWatch.Observe(Progression,O.Match))
        {
            const FString Line=LDHUD::FieldRewardNotice(Reward,Content);
            if(!Line.IsEmpty())
            {
                const uint8 Kind=Reward.Result==ELDFieldRewardResult::Blueprint?0:1;
                PushLootNotice(Line,Kind);
            }
        }
        for(const FString& MedalLine:MedalDropWatch.Observe(Progression,O.Match,Content))
        {
            if(!MedalLine.IsEmpty()){PushLootNotice(TEXT("◆ ")+MedalLine,2);}
        }
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
                AutoOpenSince=-1;bWorkshopAutoOpened=true;if(!bAutoWorkshopPhase){ChoicesPanel->MarkOpenedAutomatically();}
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
    const float BandTop=HudSafeBottom+float(Band->GetDesiredSize().Y);
    if(TopCentreSlot)
    {
        // The open build ring keeps the rest actions in the bottom-right corner beside the HUD band, under its shop.
        // Only the rest's actions leave the top centre (they would meet the ring's top arc); the banner alone stays.
        const bool bCorner=bWorkshopOpen&&Workshop&&RestActionsPanel&&RestActionsPanel->IsVisible();
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
        const FAnchors Anchor=bCorner?FAnchors(1,1):FAnchors(.5f,0);const FVector2D Align=bCorner?FVector2D(1,1):FVector2D(.5f,0);
        const FVector2D Where=bCorner?Workshop->ActionsAnchor()-GetCachedGeometry().GetLocalSize():FVector2D(0,HudTopCentreY);
        if(!(TopCentreSlot->GetAnchors()==Anchor)){TopCentreSlot->SetAnchors(Anchor);}
        if(!TopCentreSlot->GetAlignment().Equals(Align)){TopCentreSlot->SetAlignment(Align);}
        if(!TopCentreSlot->GetPosition().Equals(Where,.5f)){TopCentreSlot->SetPosition(Where);}
    }
    if(bWorkshopOpen)
    {
        // Covered by the workshop: the corner widgets it replaces.
        HudSetShown(EventFeedPanel,false,ESlateVisibility::HitTestInvisible);HudSetShown(TargetCard,false,ESlateVisibility::Visible);
        HudSetShown(DeathBanner,false,ESlateVisibility::HitTestInvisible);
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
    if(ULDCommandButton* Sheet=Commands.FindRef(TEXT("Attributes")))
    {
        // The entry names its key; the flyout over the strip shows the own final attributes while it is open.
        const FString Tip=FString(StatBar&&StatBar->ShowsAttributes()?TEXT("收起属性"):TEXT("属性"))+TEXT(" [")+PC->BindingLabel(TEXT("Attributes"))+TEXT("]");
        if(Sheet->GetToolTipText().ToString()!=Tip){Sheet->SetToolTipText(FText::FromString(Tip));}
    }
    HudSetShown(StatFlyout,StatBar&&StatBar->ShowsAttributes()&&Band->IsVisible(),ESlateVisibility::Visible);

    ELDCombatEntry ArmedEntry=ELDCombatEntry::None;const bool bArmed=PC->ReadArmedCombatEntry(ArmedEntry);
    // An armed aim cancelled since the last sample flashes its slot once.
    const ELDCombatEntry CancelledEntry=PC->GetArmedCancelSerial()!=SeenArmedCancel?PC->GetArmedCancelEntry():ELDCombatEntry::None;
    SeenArmedCancel=PC->GetArmedCancelSerial();
    if(PC->CombatReceipts.Serial()!=SeenCombatReceipt)
    {
        TArray<FLDCombatActionReceipt> Arrived;
        if(const uint32 Lost=PC->CombatReceipts.ReadSince(SeenCombatReceipt,Arrived)){UE_LOG(LogLDUI,Warning,TEXT("LD_HUD_COMBAT_RECEIPTS_MISSED %u"),Lost);}
        bool bHandledAsSlotTip=false;
        for(const FLDCombatActionReceipt& Receipt:Arrived)
        {
            if(Receipt.Generation!=O.Match||Receipt.Life!=O.Life){continue;}
            const FString SlotReason=LDHUD::CombatReceiptSlotNotice(Receipt);
            if(!SlotReason.IsEmpty())
            {
                int32 MatchedSlot=INDEX_NONE;
                for(int32 Pressed=0;Pressed<ActionSlots.Num();++Pressed)
                {
                    if((Pressed<3?PC->SkillAtKey[Pressed]:HudActionEntries[Pressed])==Receipt.Entry)
                    {
                        ActionSlots[Pressed].RejectedAt=UiNow;
                        MatchedSlot=Pressed;
                    }
                }
                PushSlotRejectTip(SlotReason,MatchedSlot);
                bHandledAsSlotTip=true;
            }
            if(Receipt.bProtectionRemoved&&ToldProtectionLifeRevision!=Receipt.LifeRevision)
            {
                ToldProtectionLifeRevision=Receipt.LifeRevision;
                ShowToast(LDTerms::ProtectionRemoved());
            }
        }
        if(!bHandledAsSlotTip)
        {
            const FString Notice=LDHUD::CombatReceiptsNotice(Arrived,O.Match,O.Life,ToldProtectionLifeRevision);
            if(!Notice.IsEmpty()){ShowToast(Notice);}
        }
    }
    uint8 ControlBits=0;
    if(OwnCombat){for(const FLDCombatStatusView& Status:OwnCombat->Statuses){ControlBits|=Status.ControlBlocks;}}
    const bool bAltHeld=PC->IsInputKeyDown(EKeys::LeftAlt)||PC->IsInputKeyDown(EKeys::RightAlt);
    const bool bExpandFormula=(bAltHeld||bDetailsHeld)&&!PC->IsWorldInputCaptured();
    // The slot under the pointer (by its laid-out box, so a disabled slot explains itself as well) gets the description.
    const FVector2D Pointer=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetCursorPos():FVector2D(-1,-1);
    int32 DetailIndex=INDEX_NONE;LDHUD::FActionDetailView DetailView;
    LDHUD::FLiveSkillTooltip LiveSkillDetail;bool bHasLiveSkillDetail=false;
    for(int32 Index=0;Index<ActionSlots.Num();++Index)
    {
        const LDHUD::EActionSlot SlotKind=static_cast<LDHUD::EActionSlot>(Index);
        // Q/W/E positions show the entries the local slot order puts there (#97 drag to swap).
        const ELDCombatEntry Wanted=Index<3?PC->SkillAtKey[Index]:HudActionEntries[Index];
        LDHUD::FActionInput Input;Input.Slot=SlotKind;
        if(const FLDCombatEntryView* Source=OwnCombat?OwnCombat->Entries.FindByPredicate([Wanted](const FLDCombatEntryView& Candidate){return Candidate.Entry==Wanted;}):nullptr)
        {
            Input=LDHUD::ActionInput(*Source,SlotKind,Content);
            uint8 SlotControlMask=HudActionControl[Index];
            if(Index<3&&Content&&!Input.DefinitionId.IsNone())
            {
                if(const FLDDemoAbilityDefinition* Ability=Content->Abilities.FindByPredicate([&Input](const FLDDemoAbilityDefinition& Candidate)
                    {return FName(*Candidate.Id)==Input.DefinitionId;}))
                {
                    const FLDDemoAbilityBranch* Branched=Ability->FindBranch(Input.Branch);
                    const FLDDemoAbilityValues& Values=Branched?Branched->Values:Ability->Values;
                    if(Values.Effects.ContainsByPredicate([](const FLDDemoEffectDefinition& Eff){return Eff.GetOp()==ELDDemoEffectOp::Dash;}))
                    {
                        SlotControlMask|=uint8(ELDCombatControlBlock::Displacement);
                    }
                }
            }
            Input.bControlled=(ControlBits&SlotControlMask)!=0;
            if(Input.bControlled&&OwnCombat)
            {
                Input.ControlLabel=LDHUD::ControlStatusBadge(OwnCombat->Statuses,SlotControlMask);
                if(Input.ControlLabel.IsEmpty()&&(SlotControlMask&uint8(ELDCombatControlBlock::Displacement))&&OwnCombat->bRooted)
                {
                    Input.ControlLabel=TEXT("定身");
                }
            }
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
            const FLinearColor Edge=bRejected?LDBroadcast::LiveRed:bHover?Style.Edge+(FLinearColor::White-Style.Edge)*.4f:Style.Edge;
            const float EdgeWidth=bRejected?3.f:bHover?FMath::Max(Style.EdgeWidth,2.f):Style.EdgeWidth;
            Entry.Frame->SetBrush(HudRound(0.f,Edge,EdgeWidth));Entry.Frame->SetBrushColor(Style.Fill);
            Entry.Icon->SetBrushColor(HudIconColor(IconKind,Look));
            HudSetColor(Entry.Glyph,Look==LDHUD::EActionLook::Blocked||Look==LDHUD::EActionLook::Empty?LDBroadcast::Grey:LDBroadcast::White);
            Entry.KeyBand->SetBrushColor(bRejected?LDBroadcast::LiveRed:Style.Band);
            HudSetColor(Entry.Key,bRejected?LDBroadcast::White:Style.KeyInk);
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
        {
            // The element the bound active applies, in its element ink.
            const uint8 Element=Index<3&&Input.bHasSource?LDHUD::SkillElement(Content,Input.DefinitionId,Input.Branch):0;
            const FLinearColor Inks[6]={LDBroadcast::White,LDBroadcast::Fire,LDBroadcast::Water,LDBroadcast::Shock,LDBroadcast::Chill,LDBroadcast::Rot};
            HudSetShown(Entry.ElementChip,Element>0&&Element<6,ESlateVisibility::HitTestInvisible);
            if(Element>0&&Element<6)
            {
                HudSetText(Entry.ElementGlyph,LDHUD::ElementGlyph(Element));
                if(Entry.ElementChip->GetBrushColor()!=Inks[Element]){Entry.ElementChip->SetBrushColor(Inks[Element]);}
            }
        }
        HudSetText(Entry.ControlBadge,View.LockBadge);HudSetShown(Entry.ControlChip,!View.LockBadge.IsEmpty(),ESlateVisibility::HitTestInvisible);
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
        const float Opacity=Look==LDHUD::EActionLook::Empty?.75f:1.f;
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
            Entry.Flash->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,0.f,Tint.CopyWithNewOpacity(Fade),3.f));
            Entry.Flash->SetBrushColor(Tint.CopyWithNewOpacity(.35f*Fade));
        }
        else if(bRankGlow)
        {
            const float Wave=.35f+.65f*LDTween::PingPong(UiNow,1.2);
            Entry.Flash->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,0.f,LDBroadcast::Orange.CopyWithNewOpacity(Wave),3.f));
            Entry.Flash->SetBrushColor(LDBroadcast::Orange.CopyWithNewOpacity(.12f*Wave));
            bReadModelLive=true;
        }
        const float Pop=bFlash?1.f+(LDHUDStyle::ReadyPopScale-1.f)*LDTween::PingPong(FlashAge,LDHUDStyle::ReadyFlashSeconds):1.f;
        LDTween::SetScale(Entry.Box,Pop);
        Entry.Button->SetIsEnabled(View.bCanSubmitIntent);
        if(DetailIndex==INDEX_NONE&&Entry.Box->GetCachedGeometry().IsUnderLocation(Pointer))
        {
            DetailIndex=Index;
            int32 PracticeStacks=0;
            if(PC->BuildStats.Match==O.Match&&Progression)
            {
                for(const FLDCardInstanceView& C:Progression->Cards)
                {
                    if(C.IsInstalled()&&C.DefinitionId==TEXT("card_practice")
                        &&(C.Target.Kind==ELDCardTargetKind::Body||(C.Target.Kind==ELDCardTargetKind::Skill&&C.Target.Active==Input.DefinitionId)))
                    {
                        for(const FLDCardTriggerRow& Tr:PC->BuildStats.Triggers)
                        {
                            if(LDHUD::CardDefinition(Tr.Card)==TEXT("card_practice")){PracticeStacks=FMath::Max(PracticeStacks,Tr.Count);}
                        }
                    }
                }
            }
            LiveSkillDetail=LDHUD::EvaluateLiveSkillTooltip(View,Input,Context,Progression,OwnCombat,Content,bExpandFormula,PracticeStacks);
            DetailView=LiveSkillDetail.Detail;
            bHasLiveSkillDetail=LiveSkillDetail.bHasFormula;
        }
    }
    // The basic attack's combo pips: the current step lit (the third gold) until COMBO.ResetSec after its start.
    {
        int32 Step=0;
        if(OwnCombat&&Content&&bClock&&OwnCombat->ComboStep>0&&NowMs-OwnCombat->ComboStartMs<=Content->Parameters.Combo.ResetMs)
        {Step=OwnCombat->ComboStep;bReadModelLive=true;}
        StripPaint.Combo=Step;
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
    const bool bTrayOpen=ChoicesPanel&&ChoicesPanel->IsTrayOpen();
    if((PC->RootUI&&PC->RootUI->ActiveMainPanel()!=ELDUIMainPanel::None&&!(bTrayOpen&&PC->RootUI->ActiveMainPanel()==ELDUIMainPanel::Choices))
        ||(PC->BuildBlueprints&&PC->BuildBlueprints->IsViewerOpen())){DetailOver=nullptr;DetailMark.Reset();}
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
            const bool bSkillPopup=DetailIndex!=INDEX_NONE&&bHasLiveSkillDetail;
            if(ActionDetailBody)
            {
                const FLinearColor BodyTint=!bSkillPopup?LDHUDStyle::TextPrimary
                    :LiveSkillDetail.PrimaryTone==1?FLinearColor(FColor(0xFF,0x9F,0x43))
                    :LiveSkillDetail.PrimaryTone==2?FLinearColor(FColor(0x38,0xBD,0xF8))
                    :LiveSkillDetail.PrimaryTone==4?FLinearColor(FColor(0x4A,0xDE,0x80)):LDHUDStyle::TextPrimary;
                ActionDetailBody->SetColorAndOpacity(BodyTint);
            }
            if(ActionFormulaPanel&&ActionFormulaText)
            {
                const bool bShowFormula=bSkillPopup&&bExpandFormula&&!LiveSkillDetail.FormulaLines.IsEmpty();
                HudSetShown(ActionFormulaPanel,bShowFormula,ESlateVisibility::HitTestInvisible);
                if(bShowFormula){HudSetText(ActionFormulaText,FString::Join(LiveSkillDetail.FormulaLines,TEXT("\n")));}
            }
            if(ActionDetailFooter)
            {
                HudSetShown(ActionDetailFooter,bSkillPopup&&!LiveSkillDetail.FooterHint.IsEmpty(),ESlateVisibility::HitTestInvisible);
                if(bSkillPopup){HudSetText(ActionDetailFooter,LiveSkillDetail.FooterHint);}
            }
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

    // Update authorized tactical sighting ledger for the hold-Tab scoreboard (#328 §4.4).
    if(ScoreSightingsMatch!=O.Match)
    {
        ScoreSightingsMatch=O.Match;
        ScoreSightings.Reset();
    }
    if(Frame.Frame&&Frame.Status==ELDObservedViewStatus::Current&&Frame.Frame->MatchGeneration==O.Match)
    {
        for(const FLDObservedUnitView& SeenUnit:Frame.Frame->Units)
        {
            if(SeenUnit.Kind!=ELDObservedUnitKind::Contender||!SeenUnit.ContenderId.IsValid()){continue;}
            LDHUD::FTacticalSighting& Entry=ScoreSightings.FindOrAdd(SeenUnit.ContenderId);
            Entry.Presentation=SeenUnit.Presentation;
            if(SeenUnit.Level>0){Entry.LastSeenLevel=SeenUnit.Level;}
            if((SeenUnit.AuthorizedFieldMask&LDObservation::Actives)!=0)
            {
                for(int32 S=0;S<3;++S)
                {
                    if(SeenUnit.Actives.Abilities[S]!=0){Entry.RevealedActives[S]=SeenUnit.Actives.Abilities[S];}
                }
            }
        }
    }
    if(Public.GraffitiOwner.IsValid())
    {
        LDHUD::FTacticalSighting& OwnerSight=ScoreSightings.FindOrAdd(Public.GraffitiOwner);
        OwnerSight.Presentation=Public.GraffitiPresentation;
        const int64 GuardTarget=Public.Showdown.bStarted&&Public.Showdown.GuardMs>0
            ?Public.Showdown.GuardMs:(Content&&Content->Parameters.CaptureGuardMs>0?Content->Parameters.CaptureGuardMs:20000);
        if(Public.CircleState==ELDCircleState::Guarded&&bClock&&Public.GuardDueAtMs>0)
        {
            OwnerSight.KnownGuardMs=FMath::Clamp<int64>(GuardTarget-FMath::Max<int64>(0,Public.GuardDueAtMs-NowMs),0,GuardTarget);
        }
        else
        {
            OwnerSight.KnownGuardMs=FMath::Max<int64>(OwnerSight.KnownGuardMs,FMath::Max<int64>(Public.GuardKeptMs,Public.OwnerBankedGuardMs));
        }
    }

    // #328 §4.3: TargetCard live view + 2.5s 55%-alpha lost-vision snapshot + seamless re-sight resume.
    if(LastTargetMatch!=O.Match)
    {
        LastTargetMatch=O.Match;
        bHasTargetSnapshot=false;
        LastTargetHandle.Invalidate();
        LastTargetContenderId.Invalidate();
    }
    FLDObservedUnitView Subject;bool bSelected=PC->ReadInspected(Subject);
    if(!bSelected&&bHasTargetSnapshot&&LastTargetContenderId.IsValid()&&(UiNow-LastTargetSeenAtSec)<2.5
        &&Frame.Frame&&Frame.Status==ELDObservedViewStatus::Current&&Frame.Frame->MatchGeneration==O.Match)
    {
        for(const FLDObservedUnitView& Candidate:Frame.Frame->Units)
        {
            if(Candidate.Kind==ELDObservedUnitKind::Contender&&Candidate.ContenderId==LastTargetContenderId)
            {
                PC->Inspect(Candidate.Handle,Candidate.ObservationEpoch);
                bSelected=PC->ReadInspected(Subject);
                break;
            }
        }
    }
    if(bSelected)
    {
        const FString SubjectName=Subject.Kind==ELDObservedUnitKind::Contender?LDHUD::ObservedContenderName(Lobby,Subject.ContenderId,Public.Phase==ELDMatchPhase::Rest):FString();
        LastTargetCard=LDHUD::ObservedCard(Subject,SubjectName,Content,Frame.Frame?Frame.Frame->SampledAtServerSec:0.);
        LastTargetRelation=Subject.Kind==ELDObservedUnitKind::Contender?LDHUD::PKRelationText(Public,O.ContenderId,Subject.ContenderId):FString();
        LastTargetHandle=Subject.Handle;
        LastTargetContenderId=Subject.Kind==ELDObservedUnitKind::Contender?Subject.ContenderId:FGuid();
        LastTargetCreatureId=Subject.CreatureId;
        LastTargetPresentation=Subject.Presentation;
        bLastTargetContender=Subject.Kind==ELDObservedUnitKind::Contender;
        LastTargetSeenAtSec=UiNow;
        bHasTargetSnapshot=true;
    }
    const bool bLostVisionGhost=!bSelected&&bHasTargetSnapshot&&(UiNow-LastTargetSeenAtSec)<2.5;
    if(!bSelected&&!bLostVisionGhost){bHasTargetSnapshot=false;}
    const bool bShowTarget=(bSelected||bLostVisionGhost)&&!bWorkshopOpen;
    HudSetShown(TargetCard,bShowTarget,ESlateVisibility::Visible);
    bReadModelLive|=bLostVisionGhost;
    if(bShowTarget)
    {
        TargetCard->SetRenderOpacity(bLostVisionGhost?.55f:1.f);
        const LDHUD::FObservedCard& Card=LastTargetCard;
        // The name (with the PK relation and the lost-vision mark), the public level and the health figure; the plate, bar
        // and momentum are painted from TargetPaint.
        FString NameLine=LastTargetRelation.IsEmpty()?Card.Name:Card.Name+TEXT(" · ")+LastTargetRelation;
        if(bLostVisionGhost){NameLine+=TEXT(" · 视野丢失");}
        HudSetText(TargetTitle,NameLine);
        if(TargetTitle->GetToolTipText().ToString()!=Card.Title){TargetTitle->SetToolTipText(FText::FromString(Card.Title));}
        HudSetText(TargetLevelText,bLastTargetContender&&Card.Level>0?FString::Printf(TEXT("LV%d"),Card.Level):FString());
        HudSetShown(TargetLevelText,bLastTargetContender&&Card.Level>0,ESlateVisibility::HitTestInvisible);
        HudSetText(TargetGlyph,bLastTargetContender?FString():Card.Name.Left(1));
        if(TargetTrailHandle!=LastTargetHandle){TargetTrailHandle=LastTargetHandle;TargetTrail.Reset();}
        TargetPaint.bContender=bLastTargetContender;
        TargetPaint.Seat=bLastTargetContender?LDHUD::SeatOf(Lobby,LastTargetContenderId):INDEX_NONE;
        if(bLastTargetContender&&TargetPaint.Seat==INDEX_NONE){TargetPaint.Seat=LastTargetPresentation;}
        TargetPaint.CreatureInk=ULDHumanoidVisualComponent::MonsterTint(LastTargetCreatureId);
        // Own: white; another contender: live red (colour blind: the enemy orange); a camp: the camp health ink.
        const bool bSelfTarget=bLastTargetContender&&LastTargetContenderId==O.ContenderId;
        TargetPaint.HealthInk=!bLastTargetContender?LDHUDStyle::CampHealth:bSelfTarget?LDBroadcast::White
            :bColorblind?LDHUDStyle::EnemyHealthColorblind:LDBroadcast::LiveRed;
        TargetPaint.bHealth=Card.Health.bKnown;TargetPaint.Health=Card.Health.Fraction;
        HudSetText(TargetHealth,Card.Health.bKnown?Card.Health.Now:FString(TEXT("—")));
        HudSetColor(TargetHealth,TargetPaint.HealthInk);
        if(TargetHealth->GetToolTipText().ToString()!=Card.Health.Text){TargetHealth->SetToolTipText(FText::FromString(Card.Health.Text));}
        TargetPaint.bMomentum=Card.bMomentum;TargetPaint.Momentum=Card.Momentum.Fraction;TargetPaint.bMomentumFull=Card.bMomentumFull;
        TargetPaint.bCharging=!bLostVisionGhost&&Card.bCharging;
        TargetPaint.MomentumNotch=Content&&Content->Parameters.Momentum.MaxP2>0
            ?float(FMath::Clamp(double(Content->Parameters.Overdrive.ThresholdP2)/double(Content->Parameters.Momentum.MaxP2),0.,1.)):-1.f;
        if(Card.bMomentum&&TargetBarSpot->GetToolTipText().ToString()!=Card.Momentum.Text){TargetBarSpot->SetToolTipText(FText::FromString(Card.Momentum.Text));}
        bReadModelLive|=TargetPaint.bCharging;
        // The status squares: grown on demand, the unused ones collapsed.
        while(TargetStatusTiles.Num()<Card.StatusChips.Num())
        {
            UTextBlock* Glyph=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeSmall);Glyph->SetAutoWrapText(false);HudFace(Glyph,true,0);Glyph->SetJustification(ETextJustify::Center);
            auto* Rim=WidgetTree->ConstructWidget<UBorder>();Rim->SetBrush(HudRound(0.f,LDHUDStyle::Rim,LDBroadcast::RuleWidth));
            Rim->SetPadding(FMargin(0));Rim->SetHorizontalAlignment(HAlign_Center);Rim->SetVerticalAlignment(VAlign_Center);Rim->SetContent(Glyph);
            UImage* Icon=WidgetTree->ConstructWidget<UImage>();Icon->SetVisibility(ESlateVisibility::Collapsed);
            UTextBlock* Stack=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeTiny);Stack->SetAutoWrapText(false);HudFace(Stack,true,0);
            auto* Face=WidgetTree->ConstructWidget<UOverlay>();
            for(UWidget* Layer:{static_cast<UWidget*>(Rim),static_cast<UWidget*>(Icon)})
            {auto* LayerSlot=Face->AddChildToOverlay(Layer);LayerSlot->SetHorizontalAlignment(HAlign_Fill);LayerSlot->SetVerticalAlignment(VAlign_Fill);}
            {auto* StackSlot=Face->AddChildToOverlay(Stack);StackSlot->SetHorizontalAlignment(HAlign_Right);StackSlot->SetVerticalAlignment(VAlign_Bottom);}
            auto* Side=WidgetTree->ConstructWidget<USizeBox>();Side->SetWidthOverride(HudTargetStatusSide);Side->SetHeightOverride(HudTargetStatusSide);Side->SetContent(Face);
            Side->SetVisibility(ESlateVisibility::Visible);
            TargetStatusRow->AddChildToWrapBox(Side);
            TargetStatusTiles.Add(Side);TargetStatusRims.Add(Rim);TargetStatusIcons.Add(Icon);TargetStatusGlyphs.Add(Glyph);TargetStatusStacks.Add(Stack);
        }
        HudSetShown(TargetStatusRow,!Card.StatusChips.IsEmpty(),ESlateVisibility::SelfHitTestInvisible);
        for(int32 Index=0;Index<TargetStatusTiles.Num();++Index)
        {
            const bool bUsed=Card.StatusChips.IsValidIndex(Index);
            HudSetShown(TargetStatusTiles[Index],bUsed,ESlateVisibility::Visible);
            if(!bUsed){continue;}
            const LDHUD::FObservedCard::FStatusChip& Chip=Card.StatusChips[Index];
            if(!TargetStatusTiles[Index]->GetToolTipText().ToString().Equals(Chip.Tip,ESearchCase::CaseSensitive))
            {TargetStatusTiles[Index]->SetToolTipText(FText::FromString(Chip.Tip));}
            const FLinearColor Edge=Chip.bPositive?(bColorblind?FLinearColor(.25f,.68f,1.f):LDHUDStyle::StatusPositive)
                :(bColorblind?FLinearColor(1.f,.52f,.10f):LDHUDStyle::StatusNegative);
            if(TargetStatusRims[Index]->Background.OutlineSettings.Color!=FSlateColor(Edge))
            {TargetStatusRims[Index]->SetBrush(HudRound(0.f,Edge,LDBroadcast::RuleWidth));TargetStatusRims[Index]->SetBrushColor(LDBroadcast::Ink.CopyWithNewOpacity(.9f));}
            const bool bPicture=LDUIIcons::Apply(TargetStatusIcons[Index],Chip.IconId,0.f);
            HudSetText(TargetStatusGlyphs[Index],bPicture?FString():Chip.Glyph);
            TargetStatusGlyphs[Index]->SetColorAndOpacity(FSlateColor(Edge));
            HudSetText(TargetStatusStacks[Index],Chip.Stacks>1?FString::FromInt(Chip.Stacks):FString());
        }
        HudSetShown(TargetActivesRow,Card.bActives,ESlateVisibility::SelfHitTestInvisible);
        for(int32 Index=0;Card.bActives&&Index<TargetActiveTiles.Num();++Index)
        {
            const LDHUD::FObservedCard::FActiveChip& Chip=Card.Actives[Index];
            const FString Tip=Chip.Key+TEXT(" · ")+Chip.Name;
            UWidget* Tile=TargetActiveTiles[Index]->GetParent()?TargetActiveTiles[Index]->GetParent()->GetParent():nullptr;
            if(Tile&&!Tile->GetToolTipText().ToString().Equals(Tip,ESearchCase::CaseSensitive)){Tile->SetToolTipText(FText::FromString(Tip));}
            const bool bPicture=LDUIIcons::Apply(TargetActiveIcons[Index],Chip.IconId,0.f);
            HudSetText(TargetActiveNames[Index],Chip.bEmpty||bPicture?FString():Chip.Name.Left(1));
            TargetActiveTiles[Index]->SetBrushColor(LDBroadcast::Ink.CopyWithNewOpacity(Chip.bEmpty?.45f:.9f));
        }
    }

    // Route controller intent notices: local target/pointer/order rejects go to Channel A (0.9s cursor micro-tip) so they
    // never overwrite global broadcasts (#328 §4.2).
    if(SeenIntent!=PC->IntentFeedback||SeenFeedback!=PC->FeedbackSerial())
    {
        SeenIntent=PC->IntentFeedback;SeenFeedback=PC->FeedbackSerial();
        if(PC->IntentFeedback.Contains(TEXT("指针"))||PC->IntentFeedback.Contains(TEXT("无法到达"))
            ||PC->IntentFeedback.Contains(TEXT("不能下达"))||PC->IntentFeedback.Contains(TEXT("目标")))
        {
            PushSlotRejectTip(PC->IntentFeedback,INDEX_NONE);
        }
        else
        {
            ShowToast(PC->IntentFeedback);
        }
    }
    // Channel A: 0.9s slot/cursor reject micro-tip.
    if(SlotRejectPanel&&SlotRejectSlot)
    {
        const double RejectAge=UiNow-SlotRejectAt;
        const bool bShowReject=SlotRejectAt>0.&&RejectAge<0.9;
        HudSetShown(SlotRejectPanel,bShowReject,ESlateVisibility::HitTestInvisible);
        bReadModelLive|=bShowReject;
        if(bShowReject)
        {
            SlotRejectPanel->SetRenderOpacity(RejectAge>0.65?float(1.-(RejectAge-0.65)/0.25):1.f);
            const FGeometry& Area=GetCachedGeometry();
            FVector2D Pos(Area.GetLocalSize().X*.5,Area.GetLocalSize().Y-BandTop-28.);
            if(ActionSlots.IsValidIndex(SlotRejectIndex)&&ActionSlots[SlotRejectIndex].Box&&ActionSlots[SlotRejectIndex].Box->IsVisible())
            {
                const FGeometry& BoxG=ActionSlots[SlotRejectIndex].Box->GetCachedGeometry();
                const FVector2D TopLeft=Area.AbsoluteToLocal(BoxG.GetAbsolutePosition());
                const FVector2D Ext=Area.AbsoluteToLocal(BoxG.GetAbsolutePosition()+BoxG.GetAbsoluteSize())-TopLeft;
                Pos=FVector2D(TopLeft.X+Ext.X*.5,TopLeft.Y-10.);
            }
            else if(SlotRejectCursor.X>=0.&&SlotRejectCursor.Y>=0.)
            {
                Pos=Area.AbsoluteToLocal(SlotRejectCursor)-FVector2D(0.,28.);
            }
            Pos.X=FMath::Clamp(Pos.X,90.,FMath::Max(90.,Area.GetLocalSize().X-90.));
            Pos.Y=FMath::Clamp(Pos.Y,36.,FMath::Max(36.,Area.GetLocalSize().Y-24.));
            SlotRejectSlot->SetPosition(Pos);
        }
    }
    // Channel B: right-side vertical loot/growth queue (max 3 rows, 3.5s independent fade).
    if(LootQueueBox)
    {
        LootNotices.RemoveAll([UiNow](const FLootNoticeEntry& Item){return (UiNow-Item.At)>=3.5;});
        const bool bShowLoot=!LootNotices.IsEmpty()&&!bWorkshopOpen;
        HudSetShown(LootQueueBox,bShowLoot,ESlateVisibility::HitTestInvisible);
        bReadModelLive|=bShowLoot;
        for(int32 Row=0;Row<LootQueueCards.Num();++Row)
        {
            const bool bRow=bShowLoot&&LootNotices.IsValidIndex(Row);
            HudSetShown(LootQueueCards[Row],bRow,ESlateVisibility::HitTestInvisible);
            if(!bRow){continue;}
            const FLootNoticeEntry& Entry=LootNotices[Row];
            const double Age=UiNow-Entry.At;
            const float Alpha=Age>2.8?float(1.-(Age-2.8)/0.7):1.f;
            LootQueueCards[Row]->SetRenderOpacity(FMath::Clamp(Alpha,0.f,1.f));
            // The kind's ink block leads the slab: a skill orange, a build card the rare blue, an element stone its green, a
            // chain white.
            const FLinearColor KindInk=Entry.Kind==0?LDBroadcast::Orange:Entry.Kind==1?LDBroadcast::Rare:Entry.Kind==2?LDBroadcast::Rot:LDBroadcast::White;
            if(LootQueueDots[Row]->GetBrushColor()!=KindInk){LootQueueDots[Row]->SetBrushColor(KindInk);}
            // Wipes in from the right over the first fifth of a second.
            const float Wipe=1.f-LDTween::Progress(Age,LDBroadcast::SlideIn,LDTween::ECurve::EaseOut);
            LootQueueCards[Row]->SetRenderTranslation(FVector2D(60.f*Wipe,0.f));
            HudSetText(LootQueueTexts[Row],Entry.Text);
        }
    }
    // Channel C: top-center broadcast banner (below TopCentre, never fighting Channel A or B).
    HudSetText(Toast,UiNow-FeedbackAt<FeedbackDuration?LastFeedback:FString());
    HudSetShown(ToastPanel,!Toast->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    {
        // The toast wipes in from the left over LDBroadcast::SlideIn, then holds; a repeat of the same line does not wipe again.
        const float Wipe=1.f-LDTween::Progress(UiNow-FeedbackAt,LDBroadcast::SlideIn,LDTween::ECurve::EaseOut);
        ToastPanel->SetRenderTranslation(FVector2D(-48.f*Wipe,0.f));ToastPanel->SetRenderOpacity(1.f-.8f*Wipe);
    }
    bReadModelLive|=!Toast->GetText().IsEmpty()||AutoOpenSince>=0||(Workshop&&(Workshop->IsOpening()||(Workshop->IsOpen()&&bRestPhase
        &&LDHUD::ReadModelRingMoves(Public.Timeline.StepStartedAtMs,Public.Timeline.StepEndsAtMs))));
    if(ToastSlot)
    {
        const FAnchors Anchor(.5f,0.f);const FVector2D Align(.5f,0.f);
        // Stacked under the top-centre block (the target card, then the rest actions, spawn choice and PK lines): the toast
        // never draws over them, whatever is shown (last frame's layout; the block does not depend on the toast).
        float Top=HudTopCentreY;
        if(const UWidget* Block=TopCentreSlot?TopCentreSlot->Content.Get():nullptr;Block&&Block->IsVisible())
        {
            const FGeometry& Over=Block->GetCachedGeometry();
            const float Bottom=float(GetCachedGeometry().AbsoluteToLocal(Over.LocalToAbsolute(FVector2D(Over.GetLocalSize()))).Y);
            if(Over.GetLocalSize().Y>1.f){Top=FMath::Max(Top,Bottom+8.f);}
        }
        const FVector2D TopPos(0.f,FMath::RoundToFloat(Top));
        if(!(ToastSlot->GetAnchors()==Anchor)){ToastSlot->SetAnchors(Anchor);}
        if(!ToastSlot->GetAlignment().Equals(Align)){ToastSlot->SetAlignment(Align);}
        if(!ToastSlot->GetPosition().Equals(TopPos,.5f)){ToastSlot->SetPosition(TopPos);}
    }
}
void ULDHUD::OpenRankUp(int32 ProgressionSlot)
{
    RankSlot=(RankSlot==ProgressionSlot)?INDEX_NONE:ProgressionSlot;
    MarkReadModelDirty();
}

bool ULDHUD::CloseRankUp()
{
    if(RankSlot==INDEX_NONE){return false;}
    RankSlot=INDEX_NONE;
    MarkReadModelDirty();
    return true;
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
    const uint8 NextRank=LDSkillBranch::RankOf(RankOptions[0]);
    HudSetText(RankTitle,FString::Printf(TEXT("%s · %s %u"),*SkillName,LDTerms::SkillRank,uint32(NextRank)));
    if(RankHint)
    {
        HudSetText(RankHint,NextRank==2
            ?FString(TEXT("按 [1] / [2] 选择二级分化 · 悬停下方三级节点预览终极形态 · [Esc] 关闭"))
            :FString(TEXT("按 [1] / [2] 选择三级终极形态 · [Esc] 关闭")));
    }
    for(int32 Card=0;Card<RankCards.Num();++Card)
    {
        const LDHUD::FBranchCardView View=LDHUD::BranchCard(Content,RankAbility,RankOptions[Card],Stats);
        HudSetText(RankCardNames[Card],View.Name);
        HudSetText(RankCardLines[Card],View.Line);
        RankCards[Card]->SetToolTipText(FText::FromString(View.Detail));
        RankCards[Card]->SetIsEnabled(View.bKnown);
        LDUIIcons::Apply(RankCardIcons[Card],View.IconId,8.f,FLinearColor::White);

        const TArray<ELDSkillBranch> SubBranches=LDHUD::NextBranches(RankOptions[Card]);
        const bool bShowSub=SubBranches.Num()==2&&RankSubRows.IsValidIndex(Card);
        if(RankSubRows.IsValidIndex(Card)){HudSetShown(RankSubRows[Card],bShowSub,ESlateVisibility::SelfHitTestInvisible);}
        for(int32 Sub=0;bShowSub&&Sub<2;++Sub)
        {
            const int32 ChipIdx=Card*2+Sub;
            if(!RankSubChips.IsValidIndex(ChipIdx)){continue;}
            const LDHUD::FBranchCardView SubView=LDHUD::BranchCard(Content,RankAbility,SubBranches[Sub],Stats);
            const TCHAR* Code=(SubBranches[Sub]==ELDSkillBranch::A1)?TEXT("3A1")
                :(SubBranches[Sub]==ELDSkillBranch::A2)?TEXT("3A2")
                :(SubBranches[Sub]==ELDSkillBranch::B1)?TEXT("3B1"):TEXT("3B2");
            HudSetText(RankSubNames[ChipIdx],FString::Printf(TEXT("%s · %s"),Code,*SubView.Name));
            LDUIIcons::Apply(RankSubIcons[ChipIdx],SubView.IconId,4.f,FLinearColor::White);
            RankSubChips[ChipIdx]->SetToolTipText(FText::FromString(SubView.Detail));
        }
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

void ULDHUD::PushSlotRejectTip(const FString& Text,int32 ActionSlotIndex)
{
    if(Text.IsEmpty()){return;}
    MarkReadModelDirty();
    SlotRejectAt=GetWorld()?GetWorld()->GetRealTimeSeconds():0.;
    SlotRejectIndex=ActionSlotIndex;
    SlotRejectCursor=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetCursorPos():FVector2D(-1.,-1.);
    HudSetText(SlotRejectText,Text);
}

void ULDHUD::PushLootNotice(const FString& Text,uint8 Kind)
{
    if(Text.IsEmpty()){return;}
    MarkReadModelDirty();
    const double Now=GetWorld()?GetWorld()->GetRealTimeSeconds():0.;
    while(LootNotices.Num()>=3){LootNotices.RemoveAt(0);}
    LootNotices.Add({Text,Kind,Now});
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
    if(!PC->OwnerState.bCanRestReady)
    {ShowRestAnswer(TEXT("当前不能提交准备，未改变准备状态。"));return;}
    bRestReadyInFlight=true;
    PC->SubmitRestReady(!PC->OwnerState.bRestReady);
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
bool ULDHUD::SelectSpawnCandidate(int32 Index)
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    const auto* GS=GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr;
    if(!PC||!GS||GS->PublicMatch.Generation==0||GS->PublicMatch.Generation!=PC->OwnerState.Match){return false;}
    int64 NowMs=0;const bool bClock=PC->ReadDemoAuthorityTimeMs(NowMs);
    const LDHUD::FSpawnChoiceView Choice=LDHUD::SpawnChoice(PC->OwnerState,bClock,NowMs);
    if(!Choice.bVisible||Index<0||Index>=Choice.Count||!PC->OwnerState.Pacing.SpawnCandidates.IsValidIndex(Index)){return false;}
    SubmitSpawnChoice(Index);
    if(PC->Navigation&&PC->OwnerState.Pacing.SpawnCandidateCm.IsValidIndex(Index))
    {
        PC->Navigation->PanTo(PC->OwnerState.Pacing.SpawnCandidateCm[Index]);
    }
    return true;
}

bool ULDHUD::HandleContextualKeyPress(const FKey& Key,bool bAlt,bool bCtrl,bool bShift)
{
    auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC||!IsVisible()){return false;}
    if(PC->ControlsUI&&PC->ControlsUI->IsOpen()){return false;}
    if(PC->RootUI&&PC->RootUI->IsMenuOpen()){return false;}

    // 1) Active skill rank-up modal: [1] / [2] pick branch A or B immediately.
    if(RankSlot!=INDEX_NONE&&!bAlt&&!bCtrl)
    {
        if(Key==EKeys::One){SubmitRankUp(0);return true;}
        if(Key==EKeys::Two){SubmitRankUp(1);return true;}
    }

    // 2) Ctrl + Q / W / E (or bound Skill1 / Skill2 / SkillE): open/toggle RankPage for that skill when a skill point is available.
    if(bCtrl&&!bAlt&&!bShift)
    {
        const FName SkillActions[3]={TEXT("Skill1"),TEXT("Skill2"),TEXT("SkillE")};
        const FKey FallbackKeys[3]={EKeys::Q,EKeys::W,EKeys::E};
        for(int32 VisualIdx=0;VisualIdx<3;++VisualIdx)
        {
            if(Key==PC->BindingKey(SkillActions[VisualIdx])||Key==FallbackKeys[VisualIdx])
            {
                const ELDCombatEntry Shown=PC->SkillAtKey[VisualIdx];
                const int32 ProgSlot=Shown==ELDCombatEntry::Q?0:Shown==ELDCombatEntry::W?1:Shown==ELDCombatEntry::E?2:INDEX_NONE;
                const FLDProgressionView* Prog=PC->ReadOwnProgression();
                const FLDActiveSlotView* Learned=Prog&&ProgSlot!=INDEX_NONE
                    ?Prog->Actives.FindByPredicate([ProgSlot](const FLDActiveSlotView& Row){return Row.Slot==ProgSlot;}):nullptr;
                if(Learned&&LDSkillBranch::RankOf(Learned->Branch)<3&&Prog->SkillPoints>0&&PC->OwnerState.LifePhase==ELDLifePhase::Alive)
                {
                    OpenRankUp(ProgSlot);
                    return true;
                }
                return false;
            }
        }
    }

    // 3) Alt + 1 / 2 / 3: 3-choice offer quick-pick (works both expanded and combat-folded).
    if(bAlt&&!bCtrl&&Workshop&&Workshop->HandleKey(Key))
    {
        MarkReadModelDirty();
        return true;
    }

    // 4) Active SpawnChoice during field death wait: [1] / [2] / [3] selects spawn candidate + pans camera preview.
    if(!bAlt&&!bCtrl&&!bShift)
    {
        const int32 SpawnIdx=Key==EKeys::One?0:Key==EKeys::Two?1:Key==EKeys::Three?2:INDEX_NONE;
        if(SpawnIdx!=INDEX_NONE&&SelectSpawnCandidate(SpawnIdx))
        {
            return true;
        }
    }
    // 5) The build ring: [1] / [2] / [3] take an option of the offer on top, open or closed.
    if(!bCtrl&&!bShift&&Workshop&&Workshop->HandleKey(Key))
    {
        MarkReadModelDirty();
        return true;
    }
    return false;
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
        &&LDDemoEntry::IsWorldMap(World);
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
        &&LDDemoEntry::IsWorldMap(World);
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
    const bool bShowCompact=View.bVisible&&bWorkspaceOpen
        &&(Public.Timeline.Step==ELDRestStep::Draw||Public.Timeline.Step==ELDRestStep::Betting);
    if(PKCompactBar)
    {
        HudSetShown(PKCompactBar,bShowCompact,ESlateVisibility::Visible);
        if(bShowCompact)
        {
            TArray<FString> SideParts;
            for(const LDHUD::FPKSideView& Side:View.Sides){SideParts.Add(Side.Line);}
            const FString Summary=FString::Printf(TEXT("%s%s%s"),*View.Title,
                View.Countdown.IsEmpty()?TEXT(""):*FString::Printf(TEXT(" · %s"),*View.Countdown),
                SideParts.IsEmpty()?TEXT(""):*FString::Printf(TEXT(" · %s"),*FString::Join(SideParts,TEXT(" vs "))));
            HudSetText(PKCompactTitle,Summary);
            for(int32 Index=0;Index<PKCompactBetButtons.Num();++Index)
            {
                ULDCommandButton* Bet=PKCompactBetButtons[Index];
                const bool bShown=View.bCanBet&&View.Sides.IsValidIndex(Index);
                HudSetShown(Bet,bShown,ESlateVisibility::Visible);
                if(!bShown){continue;}
                const LDHUD::FPKSideView& Side=View.Sides[Index];
                HudSetText(CastChecked<UTextBlock>(Bet->GetContent()),Side.bOwnBet?Side.BetAction+TEXT(" · ")+LDTerms::PKOwnBet:Side.BetAction);
                const FLinearColor Tint=Side.bOwnBet?HudPromptFill:FLinearColor::White;
                if(Bet->GetBackgroundColor()!=Tint){Bet->SetBackgroundColor(Tint);}
                if(Bet->GetIsEnabled()==bPKBetInFlight){Bet->SetIsEnabled(!bPKBetInFlight);}
            }
        }
    }
    HudSetShown(PKPanelBorder,View.bVisible&&!bWorkspaceOpen,ESlateVisibility::Visible);
    // The PK countdowns, the ring line and the throw cooldown move with time.
    bReadModelLive|=View.bVisible;
    if(!View.bVisible||bWorkspaceOpen){return;}
    const auto Line=[](UTextBlock* Text,const FString& Value){HudSetText(Text,Value);HudSetShown(Text,!Value.IsEmpty(),ESlateVisibility::HitTestInvisible);};
    Line(PKTitleText,View.Title);
    Line(PKCountdownText,View.Countdown);
    HudSetColor(PKCountdownText,View.bMatchupReveal?LDHUDStyle::Celebration:LDHUDStyle::TextAccent);
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
        &&LDDemoEntry::IsWorldMap(World);
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
    for(const ELDAudioCue Cue:Cues){Audio->Play(Cue);}
}
void ULDHUD::RefreshScoreboard(const ALDGameState& Public,const FLDOwnerState& Own)
{
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(!PC){return;}
    const FKey Key=PC->BindingKey(TEXT("Scoreboard"));
    const bool bHeld=Key.IsValid()&&PC->IsInputKeyDown(Key)&&!PC->IsWorldInputCaptured();
    HudSetShown(ScorePanel,bHeld,ESlateVisibility::HitTestInvisible);
    if(!bHeld){return;}
    int64 NowMs=0;
    const bool bClock=PC->ReadDemoAuthorityTimeMs(NowMs);
    const TArray<LDHUD::FTacticalScoreRow> Rows=LDHUD::BuildTacticalScoreboard(Public.PublicMatch,Public.KillFeed,
        Public.DemoLobby,Own.ContenderId,ScoreSightings,PC->ReadOwnProgression(),DisplayDefinitions(),bClock,NowMs);
    bReadModelLive|=Public.PublicMatch.GraffitiOwner.IsValid()&&Public.PublicMatch.CircleState==ELDCircleState::Guarded;
    for(int32 RowIdx=0;RowIdx<TacticalScoreRows.Num();++RowIdx)
    {
        FScoreRowWidgets& W=TacticalScoreRows[RowIdx];
        const bool bLine=Rows.IsValidIndex(RowIdx);
        HudSetShown(W.Row,bLine||(RowIdx==0&&Rows.IsEmpty()),ESlateVisibility::HitTestInvisible);
        if(!bLine)
        {
            if(RowIdx==0){HudSetText(W.Name,TEXT("战术数据同步中…"));HudSetText(W.Level,FString());HudSetText(W.KDA,FString());HudSetText(W.FinalMod,FString());}
            continue;
        }
        const LDHUD::FTacticalScoreRow& R=Rows[RowIdx];
        if(UBorder* Card=Cast<UBorder>(W.Row.Get()))
        {
            Card->SetBrush(HudRound(5.f,R.bSelf?LDHUDStyle::UIAction:FLinearColor(1,1,1,.08f),R.bSelf?1.6f:1.f));
            Card->SetBrushColor(R.bSelf?FLinearColor(.06f,.13f,.18f,.88f):FLinearColor(.02f,.035f,.055f,.75f));
        }
        // The seat plate: the seat ink and its jersey number, as on every other seat mark.
        if(W.Portrait){W.Portrait->SetBrushColor(LDBroadcast::SeatInk(R.Presentation));}
        HudSetText(W.PortraitGlyph,FString::FromInt(int32(R.Presentation)+1));HudSetColor(W.PortraitGlyph,LDBroadcast::SeatFigure(R.Presentation));
        HudSetText(W.Name,R.bLeaderCrown?(R.Name+TEXT(" ♛热搜")):R.Name);
        W.Name->SetColorAndOpacity(R.bLeaderCrown?LDHUDStyle::UIAction:R.bSelf?LDHUDStyle::TextAccent:LDHUDStyle::TextPrimary);
        HudSetText(W.Level,R.LevelText);
        W.Level->SetColorAndOpacity(R.bSelf?LDHUDStyle::TextAccent:LDHUDStyle::TextPrimary);
        if(W.GraffitiBar){W.GraffitiBar->SetPercent(R.GraffitiFraction);}
        HudSetText(W.GraffitiText,R.GraffitiText);
        HudSetText(W.KDA,FString::Printf(TEXT("%llu / %llu / %llu"),R.Kills,R.Deaths,R.Assists));
        W.KDA->SetColorAndOpacity(R.bSelf?LDHUDStyle::TextAccent:LDHUDStyle::TextPrimary);
        for(int32 S=0;S<3;++S)
        {
            const bool bKnown=!R.ActiveIcons[S].IsEmpty()||!R.ActiveNames[S].IsEmpty();
            const bool bPic=bKnown&&LDUIIcons::Apply(W.SkillIcons[S],R.ActiveIcons[S],4.f);
            HudSetShown(W.SkillIcons[S],bPic,ESlateVisibility::HitTestInvisible);
            HudSetText(W.SkillGlyphs[S],bPic?FString():(bKnown?R.ActiveNames[S].Left(1):FString(TEXT("?"))));
            HudSetShown(W.SkillGlyphs[S],!bPic,ESlateVisibility::HitTestInvisible);
            if(W.SkillFrames[S])
            {
                W.SkillFrames[S]->SetBrush(HudRound(4.f,bKnown?LDHUDStyle::RewardActiveRim.CopyWithNewOpacity(.75f):LDHUDStyle::PanelRim,1.f));
            }
        }
        HudSetText(W.FinalMod,R.FinalModText.IsEmpty()?FString(TEXT("—")):R.FinalModText);
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
    // The rows are painted (PaintFeed); the spot's height follows the shown lines so the world labels keep clear of them.
    const float TextScale=AppliedScale>0.f?AppliedScale:1.f;
    const float Height=Lines.IsEmpty()?0.f:float(Lines.Num())*(HudFeedRowH*TextScale+HudFeedGap);
    if(EventFeedSpot&&!FMath::IsNearlyEqual(EventFeedSpot->GetHeightOverride(),Height)){EventFeedSpot->SetHeightOverride(Height);}
    HudSetShown(EventFeedPanel,!Lines.IsEmpty(),ESlateVisibility::HitTestInvisible);
    // Each line fades out and leaves on time.
    bReadModelLive|=!Lines.IsEmpty();
}
void ULDHUD::SelectResultContender(const FGuid& ContenderId)
{
    if(!ContenderId.IsValid()||InspectedResultContender==ContenderId){return;}
    InspectedResultContender=ContenderId;
    MarkReadModelDirty();
    if(const auto* GS=GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr)
    {
        if(const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer()))
        {
            RefreshResultInspectedBuild(GS->MatchResult,PC->OwnerState.ContenderId);
        }
    }
}

void ULDHUD::RefreshResultInspectedBuild(const FLDResultView& Result,const FGuid& OwnContenderId)
{
    const FLDResultRow* Chosen=InspectedResultContender.IsValid()
        ?Result.Rows.FindByPredicate([this](const FLDResultRow& Row){return Row.ContenderId==InspectedResultContender;}):nullptr;
    if(!Chosen&&Result.Winner.IsValid())
    {
        Chosen=Result.Rows.FindByPredicate([&Result](const FLDResultRow& Row){return Row.ContenderId==Result.Winner;});
    }
    if(!Chosen&&!Result.Rows.IsEmpty()){Chosen=&Result.Rows[0];}
    if(Chosen){InspectedResultContender=Chosen->ContenderId;}

    if(ResultBoardTitle)
    {
        const bool bIsWinner=Chosen&&Result.Winner.IsValid()&&Chosen->ContenderId==Result.Winner;
        const bool bIsSelf=Chosen&&OwnContenderId.IsValid()&&Chosen->ContenderId==OwnContenderId;
        const FString Suffix=bIsWinner?TEXT(" ★"):bIsSelf?TEXT("（你）"):TEXT("");
        HudSetText(ResultBoardTitle,Chosen?FString::Printf(TEXT("【%s%s】的规则板"),*Chosen->Name,*Suffix):FString(TEXT("终局规则板")));
    }
    if(ResultInspectSelfButton)
    {
        const bool bHasSelf=OwnContenderId.IsValid()&&Result.Rows.ContainsByPredicate([&OwnContenderId](const FLDResultRow& R){return R.ContenderId==OwnContenderId;});
        HudSetShown(ResultInspectSelfButton,bHasSelf,ESlateVisibility::Visible);
        if(bHasSelf)
        {
            ResultInspectSelfButton->SetIsEnabled(!Chosen||Chosen->ContenderId!=OwnContenderId);
        }
    }
    ResultWinnerBuild->ClearChildren();
    ResultWinnerBuild->SetVisibility(Chosen?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(Chosen)
    {
        ResultWinnerBuild->SetContent(LDResultBuild::MakeReport(WidgetTree,Result,*Chosen,DisplayDefinitions(),
            static_cast<LDResultBuild::EReportTab>(ResultReportTab),HudResultBoardWidth,
            AppliedScale>0?AppliedScale:1.f));
    }
    for(int32 Idx=0;Idx<ResultRowCards.Num()&&Idx<ResultRowContenders.Num();++Idx)
    {
        UBorder* Card=ResultRowCards[Idx];
        if(!Card){continue;}
        const bool bSelected=Chosen&&ResultRowContenders[Idx]==Chosen->ContenderId;
        const bool bSelf=OwnContenderId.IsValid()&&ResultRowContenders[Idx]==OwnContenderId;
        const FLinearColor Rim=bSelected?LDHUDStyle::TextAccent:bSelf?LDHUDStyle::UIAction:LDHUDStyle::PanelRim;
        Card->SetBrush(HudRound(6.f,Rim,(bSelected||bSelf)?2.f:1.f));
        Card->SetBrushColor(bSelected?FLinearColor(.06f,.13f,.18f,.94f):bSelf?LDHUDStyle::UIRaised:LDHUDStyle::UIPanel.CopyWithNewOpacity(.82f));
    }
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
        if(ShownResultId.IsValid()){ResultRows->ClearChildren();ResultEntries.Reset();ResultRowCards.Reset();ResultRowContenders.Reset();InspectedResultContender.Invalidate();ShownResultId.Invalidate();ShownResultSerial=0;}
        ResultShownAt=-1.;
        ResultReportTab=0;
        for(UWidget* Widget:{ResultWinnerBand.Get(),ResultActionsBox.Get()}){LDTween::SetOpacity(Widget,1.f);LDTween::SetScale(Widget,1.f);LDTween::SetTranslation(Widget,FVector2D::ZeroVector);}
        HudSetShown(ResultDamageHeading,false,ESlateVisibility::HitTestInvisible);
        for(UWidget* SlotWidget:ResultDamageSlots){HudSetShown(SlotWidget,false,ESlateVisibility::HitTestInvisible);}
        HudSetShown(ResultHighlightsCard,false,ESlateVisibility::HitTestInvisible);
        return;
    }
    AnimateResult();
    // The own damage arrives on the owner channel apart from the public result, so it is read every refresh; without
    // the summary of exactly this result the section says so instead of showing zeros.
    {
        const FLDDemoContentDefinitions* Content=DisplayDefinitions();
        const LDHUD::FDamageListView Damage=LDHUD::DamageSummary(PC.DamageSummary,Result,Content);
        HudSetText(ResultDamageHeading,Damage.bAvailable?Damage.Heading:FString(TEXT("你的伤害统计不可用")));
        HudSetShown(ResultDamageHeading,true,ESlateVisibility::HitTestInvisible);
        for(int32 Index=0;Index<ResultDamageCells.Num();++Index)
        {
            const bool bCell=Damage.bAvailable&&Damage.Rows.IsValidIndex(Index);
            HudSetText(ResultDamageCells[Index],bCell?Damage.Rows[Index]:FString());
            if(ResultDamageSlots.IsValidIndex(Index)){HudSetShown(ResultDamageSlots[Index],bCell,ESlateVisibility::HitTestInvisible);}
            if(bCell&&Damage.Shares.IsValidIndex(Index))
            {
                const LDHUD::FDamageShareRow& Share=Damage.Shares[Index];
                if(ResultDamageIcons.IsValidIndex(Index)&&ResultDamageGlyphs.IsValidIndex(Index))
                {
                    const bool bPic=!Share.IconId.IsEmpty()&&LDUIIcons::Apply(ResultDamageIcons[Index],Share.IconId,3.f);
                    HudSetShown(ResultDamageIcons[Index],bPic,ESlateVisibility::HitTestInvisible);
                    HudSetShown(ResultDamageGlyphs[Index],!bPic,ESlateVisibility::HitTestInvisible);
                    HudSetText(ResultDamageGlyphs[Index],LDHUD::IconGlyph(Share.Name));
                }
                if(ResultDamageBars.IsValidIndex(Index))
                {
                    const FLinearColor BarTint=Share.Kind==0?LDHUDStyle::UIAction
                        :(Share.Kind==1||Share.Kind==2)?LDHUDStyle::TextAccent
                        :Share.Kind==3?FLinearColor(.68f,.44f,1.f,1.f)
                        :FLinearColor(1.f,.46f,.36f,1.f);
                    ResultDamageBars[Index]->SetFillColorAndOpacity(BarTint);
                    ResultDamageBars[Index]->SetPercent(Share.Share);
                }
            }
        }
        const LDHUD::FResultHighlightsView Highlights=LDHUD::ResultHighlights(PC.DamageSummary,Result,PC.OwnerState.ContenderId,Content);
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
    InspectedResultContender=Result.Winner.IsValid()?Result.Winner:(Result.Rows.Num()>0?Result.Rows[0].ContenderId:FGuid());
    const FLDResultRow* WinnerRow=Result.Winner.IsValid()?Result.Rows.FindByPredicate([&Result](const FLDResultRow& Row){return Row.ContenderId==Result.Winner;}):nullptr;
    HudSetText(ResultTitle,LDHUD::ResultHeadline(Result,WinnerRow?WinnerRow->Name:FString()));
    const int64 Seconds=FMath::Max<int64>(0,Result.DurationMs)/1000;
    HudSetText(ResultSubtitle,FString::Printf(TEXT("本局用时 %lld:%02lld · 击杀不决定胜负"),Seconds/60,Seconds%60));
    ResultRows->ClearChildren();ResultEntries.Reset();ResultRowCards.Reset();ResultRowContenders.Reset();
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
    // Every contender row is clickable (#331) to inspect that contender's full build on the right-side rule board.
    auto Row=[&](const TArray<FString>& Values,const FLinearColor* Seat,bool bSelf,bool bWinner,const FGuid& RowId,int32 RowIndex)
    {
        static constexpr float Widths[]={190,90,90,60,90,100,60,70};
        auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
        {
            UTextBlock* Star=LDWidgets::Text(WidgetTree,bWinner?TEXT("★"):TEXT(""),LDHUDStyle::TypeHeading,NAME_None,LDUIFonts::ERole::Strong);
            Star->SetAutoWrapText(false);Star->SetColorAndOpacity(LDHUDStyle::UIAction);Star->SetJustification(ETextJustify::Center);
            auto* StarBox=WidgetTree->ConstructWidget<USizeBox>();StarBox->SetWidthOverride(36.f);StarBox->SetContent(Star);
            Line->AddChildToHorizontalBox(StarBox)->SetVerticalAlignment(VAlign_Center);
            auto* RowPortrait=WidgetTree->ConstructWidget<UBorder>();RowPortrait->SetBrush(HudRound(4.f,FLinearColor::Transparent,0.f));
            RowPortrait->SetBrushColor(Seat?*Seat:FLinearColor::Transparent);RowPortrait->SetHorizontalAlignment(HAlign_Center);RowPortrait->SetVerticalAlignment(VAlign_Center);
            if(Seat&&Values.Num()>0){UTextBlock* Glyph=LDWidgets::Text(WidgetTree,LDHUD::IconGlyph(Values[0]),Size+2,NAME_None,LDUIFonts::ERole::Strong);Glyph->SetAutoWrapText(false);Glyph->SetColorAndOpacity(LDHUDStyle::UIText);RowPortrait->SetContent(Glyph);}
            auto* PortraitBox=WidgetTree->ConstructWidget<USizeBox>();PortraitBox->SetWidthOverride(44.f);PortraitBox->SetHeightOverride(Seat?44.f:8.f);
            PortraitBox->SetContent(RowPortrait);
            Line->AddChildToHorizontalBox(PortraitBox)->SetPadding(FMargin(0,0,14,0));
            if(auto* PortraitSlot=Cast<UHorizontalBoxSlot>(PortraitBox->Slot)){PortraitSlot->SetVerticalAlignment(VAlign_Center);}
        }
        const FLinearColor Ink=!Seat?LDHUDStyle::TextSecondary:bSelf?LDHUDStyle::UIText:LDHUDStyle::UIText.CopyWithNewOpacity(.86f);
        for(int32 Column=0;Column<Values.Num()&&Column<UE_ARRAY_COUNT(Widths);++Column)
        {
            const bool bMark=Column==UE_ARRAY_COUNT(Widths)-1;
            if(!Seat&&Column>0)
            {
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
            Card->SetPadding(FMargin(0,6,10,6));Card->SetContent(Line);
            ResultRowCards.Add(Card);
            ResultRowContenders.Add(RowId);
            ULDCommandButton* RowButton=LDWidgets::Button(WidgetTree,TEXT(""),[this,RowId](){SelectResultContender(RowId);},true,
                FName(*FString::Printf(TEXT("ResultContenderRow%d"),RowIndex+1)));
            auto RowBtnStyle=RowButton->GetStyle();RowBtnStyle.Normal=FSlateNoResource();RowBtnStyle.Hovered=FSlateNoResource();RowBtnStyle.Pressed=FSlateNoResource();
            RowBtnStyle.NormalPadding=FMargin(0);RowBtnStyle.PressedPadding=FMargin(0);RowButton->SetStyle(RowBtnStyle);
            RowButton->SetContent(Card);
            if(UButtonSlot* BtnSlot=Cast<UButtonSlot>(Card->Slot)){BtnSlot->SetPadding(FMargin(0));BtnSlot->SetHorizontalAlignment(HAlign_Fill);BtnSlot->SetVerticalAlignment(VAlign_Fill);}
            Entry=RowButton;
        }
        ResultRows->AddChildToVerticalBox(Entry)->SetPadding(FMargin(0,3));
        ResultEntries.Add(Entry);
    };
    Row({TEXT("名字"),LDTerms::ResultKills,LDTerms::ResultDeaths,LDTerms::ResultAssists,TEXT("最终等级"),TEXT("守护贡献"),TEXT("喷漆"),FString()},nullptr,false,false,FGuid(),INDEX_NONE);
    // Rows follow the locked start roster, whose position is each contender's seat colour.
    for(int32 Index=0;Index<Result.Rows.Num();++Index)
    {
        const FLDResultRow& Entry=Result.Rows[Index];
        const LDHUD::FResultRowView View=LDHUD::ResultRow(Entry,Result.Winner,PC.OwnerState.ContenderId);
        const FLinearColor Seat=LDHUD::PaletteColor(uint8(Index));
        Row({View.Name,View.Kills,View.Deaths,View.Assists,View.Level,FString::Printf(TEXT("%.1f s"),double(Entry.BankedGuardMs)/1000.),
            FString::Printf(TEXT("%u"),Entry.CompletedSprays),View.bWinner?FString(LDTerms::Winner):FString()},
            &Seat,PC.OwnerState.ContenderId.IsValid()&&Entry.ContenderId==PC.OwnerState.ContenderId,View.bWinner,Entry.ContenderId,Index);
    }
    RefreshResultInspectedBuild(Result,PC.OwnerState.ContenderId);
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
void ULDHUD::PaintStrip(const LDUIPaint::FContext& P) const
{
    // The dark strip behind the columns and slots, then the seat block leaning over its left end with the own jersey number
    // and the basic attack's combo ticks under it (the current step white, the third gold).
    const FVector2D Size(P.Geometry.GetLocalSize());
    LDBroadcastPaint::FillSlab(P,P.Layer,FBox2D(FVector2D(HudSeatW-30.f,HudStripPanelTop),FVector2D(Size.X,HudStripPanelBottom)),LDBroadcast::Ink.CopyWithNewOpacity(.9f));
    const FBox2D Seat(FVector2D(0.f,0.f),FVector2D(HudSeatW,HudStripPanelBottom));
    const bool bSeat=StripPaint.Seat>=0&&StripPaint.Seat<=255;
    const FLinearColor Ink=bSeat?LDBroadcast::SeatInk(uint8(StripPaint.Seat)):LDBroadcast::Raised;
    const FLinearColor Figure=bSeat?LDBroadcast::SeatFigure(uint8(StripPaint.Seat)):LDBroadcast::White;
    LDBroadcastPaint::FillSlab(P,P.Layer+1,Seat,Ink);
    LDBroadcastPaint::Text(P,P.Layer+2,bSeat?FString::FromInt(StripPaint.Seat+1):FString(TEXT("?")),LDBroadcastPaint::FigureFont(84.f),
        FVector2D(HudSeatW*.5f,HudStripPanelBottom*.46f),Figure,FVector2D(.5,.5));
    constexpr float TickW=20.f,TickH=6.f,TickGap=6.f;
    const float Left=HudSeatW*.5f-(3.f*TickW+2.f*TickGap)*.5f;
    for(int32 Tick=0;Tick<3;++Tick)
    {
        const bool bLit=StripPaint.Combo==Tick+1;
        const FLinearColor TickInk=!bLit?Figure.CopyWithNewOpacity(.3f):Tick==2?LDBroadcast::Gold:Figure;
        const float X=Left+float(Tick)*(TickW+TickGap);
        LDBroadcastPaint::FillSlab(P,P.Layer+2,FBox2D(FVector2D(X,HudStripPanelBottom-16.f),FVector2D(X+TickW,HudStripPanelBottom-16.f+TickH)),TickInk);
    }
}
void ULDHUD::PaintFeed(const LDUIPaint::FContext& P) const
{
    // Each line is a lower third right-aligned in the corner: a kill as killer plate + name · cause · victim plate + name, any
    // other line as one slab; a line about the player leads with an orange block. Rows wipe in from the right and fade out
    // over their last half second (local real time; nothing here decides what is told).
    const UWorld* World=GetWorld();
    if(!World){return;}
    const double Now=World->GetRealTimeSeconds();
    const float Scale=AppliedScale>0.f?AppliedScale:1.f;
    const float RowH=HudFeedRowH*Scale;
    const float Width=float(P.Geometry.GetLocalSize().X);
    const FSlateFontInfo NameFont=LDBroadcastPaint::LabelFont(LDBroadcast::TypeBody-2.f,false,Scale);
    const TArray<LDHUD::FEventLine>& Lines=EventFeed.Lines();
    for(int32 Row=0;Row<Lines.Num();++Row)
    {
        const LDHUD::FEventLine& Line=Lines[Row];
        const double Age=Now-Line.At;
        const float In=LDTween::Progress(Age,LDBroadcast::SlideIn,LDTween::ECurve::EaseOut);
        const float Out=1.f-LDTween::Progress(Age-(LDHUD::FEventFeed::LineSeconds-LDHUDStyle::FeedFadeSeconds),LDHUDStyle::FeedFadeSeconds,LDTween::ECurve::EaseIn);
        const LDUIPaint::FContext R=P.WithOpacity(FMath::Clamp(Out,0.f,1.f)*(.2f+.8f*In));
        const float Top=float(Row)*(RowH+HudFeedGap);
        const float Shift=(1.f-In)*60.f;
        const float Right=Width-6.f+Shift;
        const float Mid=Top+RowH*.5f;
        const float Pad=12.f*Scale,PlateW=RowH*.95f,PlateH=RowH-8.f*Scale;
        if(Line.bKill)
        {
            const bool bContender=Line.KillerSeat!=INDEX_NONE||Line.Cause==ELDKillCause::Contender;
            const FString Assists=Line.Assists>0?FString::Printf(TEXT(" +%d"),Line.Assists):FString();
            const FString KillerName=Line.KillerName+Assists;
            const FVector2D KillerSize=LDBroadcastPaint::Measure(KillerName,NameFont);
            const FVector2D VictimSize=LDBroadcastPaint::Measure(Line.VictimName,NameFont);
            const float CauseW=RowH*1.1f;
            const bool bKiller=!Line.KillerName.IsEmpty();
            const float KillerW=bKiller?(bContender?PlateW+6.f:0.f)+float(KillerSize.X)+Pad:0.f;
            const float VictimW=PlateW+6.f+float(VictimSize.X)+Pad;
            const float Total=Pad+KillerW+CauseW+Pad*.5f+VictimW;
            float X=Right-Total;
            LDBroadcastPaint::FillSlab(R,R.Layer,FBox2D(FVector2D(X,Top),FVector2D(Right,Top+RowH)),LDBroadcast::Ink.CopyWithNewOpacity(.88f));
            if(Line.bSelf){LDBroadcastPaint::FillSlab(R,R.Layer+1,FBox2D(FVector2D(X,Top),FVector2D(X+7.f,Top+RowH)),LDBroadcast::Orange);}
            X+=Pad;
            if(bKiller)
            {
                if(bContender)
                {
                    LDBroadcastPaint::SeatPlate(R,R.Layer+1,FBox2D(FVector2D(X,Mid-PlateH*.5f),FVector2D(X+PlateW,Mid+PlateH*.5f)),Line.KillerSeat,1.f,PlateH*.86f);
                    X+=PlateW+6.f;
                }
                LDBroadcastPaint::Text(R,R.Layer+2,KillerName,NameFont,FVector2D(X,Mid),LDBroadcast::White,FVector2D(0.,.5));
                X+=float(KillerSize.X)+Pad;
            }
            // The cause cell: a contender's kill in orange, a camp's or an unowned death in grey, both a right-pointing wedge.
            const FBox2D Cause(FVector2D(X,Top+3.f),FVector2D(X+CauseW,Top+RowH-3.f));
            LDBroadcastPaint::FillSlab(R,R.Layer+1,Cause,bContender?LDBroadcast::Orange:LDBroadcast::Mute);
            const FVector2D C=Cause.GetCenter();const float A=float(Cause.GetSize().Y)*.26f;
            R.Polygon(R.Layer+2,{C+FVector2D(-A*.7f,-A),C+FVector2D(A*.9f,0.),C+FVector2D(-A*.7f,A)},LDUIPaint::FFill::Solid(bContender?LDBroadcast::Ink:LDBroadcast::White));
            X+=CauseW+Pad*.5f;
            const LDUIPaint::FContext Victim=R.WithOpacity(.85f);
            LDBroadcastPaint::SeatPlate(Victim,R.Layer+1,FBox2D(FVector2D(X,Mid-PlateH*.5f),FVector2D(X+PlateW,Mid+PlateH*.5f)),Line.VictimSeat,1.f,PlateH*.86f);
            LDBroadcastPaint::Rule(R,R.Layer+4,FVector2D(X+3.f,Mid+PlateH*.5f-3.f),FVector2D(X+PlateW-3.f,Mid-PlateH*.5f+3.f),LDBroadcast::LiveRed,3.f);
            X+=PlateW+6.f;
            LDBroadcastPaint::Text(R,R.Layer+2,Line.VictimName,NameFont,FVector2D(X,Mid),LDBroadcast::Grey,FVector2D(0.,.5));
            continue;
        }
        // Any other line: one slab, shrunk to fit the corner rather than wrapped.
        FSlateFontInfo Font=NameFont;
        FVector2D TextSize=LDBroadcastPaint::Measure(Line.Text,Font);
        const float MaxText=Width-Pad*3.f-12.f;
        if(TextSize.X>MaxText&&TextSize.X>0.)
        {
            Font.Size=FMath::Max(10.f,FMath::FloorToFloat(float(Font.Size)*MaxText/float(TextSize.X)));
            TextSize=LDBroadcastPaint::Measure(Line.Text,Font);
        }
        const float Left=Right-float(TextSize.X)-Pad*2.f-(Line.bSelf?8.f:0.f);
        LDBroadcastPaint::FillSlab(R,R.Layer,FBox2D(FVector2D(Left,Top),FVector2D(Right,Top+RowH)),LDBroadcast::Ink.CopyWithNewOpacity(.82f));
        if(Line.bSelf){LDBroadcastPaint::FillSlab(R,R.Layer+1,FBox2D(FVector2D(Left,Top),FVector2D(Left+7.f,Top+RowH)),LDBroadcast::Orange);}
        LDBroadcastPaint::Text(R,R.Layer+2,Line.Text,Font,FVector2D(Left+Pad+(Line.bSelf?8.f:0.f),Mid),LDBroadcast::White,FVector2D(0.,.5));
    }
}
void ULDHUD::PaintMoments(const LDUIPaint::FContext& P) const
{
    const double Now=FPlatformTime::Seconds();
    const FVector2D Size(P.Geometry.GetLocalSize());
    const bool bShown=Band&&Band->IsVisible();
    // Own kill: 0-150 ms letterbox bars and an orange skew wipe in, a two-frame hard cut to K.O. with the own plate beating
    // the victim's, everything gone by KillMomentSeconds (so the screen centre is never covered for longer).
    const double KillAge=MomentFreezeAge>=0.f&&KillMomentAt>=0.?double(MomentFreezeAge):Now-KillMomentAt;
    if(bShown&&KillMomentAt>=0.&&KillAge>=0.&&KillAge<KillMomentSeconds+.15)
    {
        const float In=LDTween::Progress(KillAge,.15,LDTween::ECurve::EaseOut);
        const float Out=KillAge>KillMomentSeconds?1.f-float((KillAge-KillMomentSeconds)/.15):1.f;
        const float Bar=float(Size.Y)*.075f*In*Out;
        LDBroadcastPaint::Block(P,P.Layer,FBox2D(FVector2D(0.,0.),FVector2D(Size.X,Bar)),LDBroadcast::Ink);
        LDBroadcastPaint::Block(P,P.Layer,FBox2D(FVector2D(0.,Size.Y-Bar),Size),LDBroadcast::Ink);
        if(KillAge<KillMomentSeconds)
        {
            // The wipe: an orange slab and a white rule sweeping in from the left over the left third.
            const float Sweep=LDTween::Progress(KillAge,LDBroadcast::SlideIn,LDTween::ECurve::EaseOut);
            const float H=float(Size.Y);
            const float Right=float(Size.X)*(.08f+.22f*Sweep);
            const float Lean=LDBroadcastPaint::Lean(H);
            const LDUIPaint::FContext Wipe=P.WithOpacity(.88f);
            Wipe.Polygon(P.Layer+1,{FVector2D(Right-Size.X*.22f+Lean*.5f,0.),FVector2D(Right+Lean*.5f,0.),FVector2D(Right-Lean*.5f,H),FVector2D(Right-Size.X*.22f-Lean*.5f,H)},
                LDUIPaint::FFill::Solid(LDBroadcast::Orange));
            Wipe.Polygon(P.Layer+1,{FVector2D(Right+18.f+Lean*.5f,0.),FVector2D(Right+34.f+Lean*.5f,0.),FVector2D(Right+34.f-Lean*.5f,H),FVector2D(Right+18.f-Lean*.5f,H)},
                LDUIPaint::FFill::Solid(LDBroadcast::White));
            // The hard cut: K.O. appears whole after two frames and leaves whole.
            if(KillAge>=LDBroadcast::HardCut)
            {
                const FSlateFontInfo KO=LDBroadcastPaint::FigureFont(220.f);
                const FVector2D At(Size.X*.16f,Size.Y*.40f);
                LDBroadcastPaint::Text(P,P.Layer+2,TEXT("K.O."),KO,At+FVector2D(6.,6.),LDBroadcast::Ink,FVector2D(0.,.5));
                const FBox2D Word=LDBroadcastPaint::Text(P,P.Layer+3,TEXT("K.O."),KO,At,LDBroadcast::White,FVector2D(0.,.5));
                // The lower third under it: own plate ▸ the victim's plate struck through.
                const float PlateH=46.f,PlateW=56.f;
                const FVector2D Left(Word.Min.X+20.,Word.Max.Y-14.);
                LDBroadcastPaint::FillSlab(P,P.Layer+2,FBox2D(Left,Left+FVector2D(PlateW*2.+120.,PlateH+16.)),LDBroadcast::Ink.CopyWithNewOpacity(.92f));
                LDBroadcastPaint::SeatPlate(P,P.Layer+3,FBox2D(Left+FVector2D(18.,8.),Left+FVector2D(18.+PlateW,8.+PlateH)),StripPaint.Seat);
                const FVector2D C=Left+FVector2D(18.+PlateW+30.,8.+PlateH*.5);
                P.Polygon(P.Layer+3,{C+FVector2D(-7.,-10.),C+FVector2D(9.,0.),C+FVector2D(-7.,10.)},LDUIPaint::FFill::Solid(LDBroadcast::Orange));
                const FBox2D Victim(Left+FVector2D(18.+PlateW+62.,8.),Left+FVector2D(18.+PlateW*2.+62.,8.+PlateH));
                LDBroadcastPaint::SeatPlate(P.WithOpacity(.85f),P.Layer+3,Victim,KillMomentVictimSeat);
                LDBroadcastPaint::Rule(P,P.Layer+5,FVector2D(Victim.Min.X+4.,Victim.Max.Y-4.),FVector2D(Victim.Max.X-4.,Victim.Min.Y+4.),LDBroadcast::LiveRed,4.f);
            }
        }
    }
    // Level-up: an orange card flips open over the level column (scale Y 0->1 in 120 ms), holds, then flips shut.
    const double FlipAge=MomentFreezeAge>=0.f&&LevelFlipAt>=0.?double(MomentFreezeAge):Now-LevelFlipAt;
    if(bShown&&LevelFlipAt>=0.&&FlipAge>=0.&&FlipAge<LevelFlipSeconds&&LevelBadgeText)
    {
        const FGeometry& Level=LevelBadgeText->GetCachedGeometry();
        const FVector2D TopLeft=P.Geometry.AbsoluteToLocal(Level.LocalToAbsolute(FVector2D::ZeroVector));
        const FVector2D Extent=P.Geometry.AbsoluteToLocal(Level.LocalToAbsolute(FVector2D(Level.GetLocalSize())))-TopLeft;
        const float Open=FMath::Min(LDTween::Progress(FlipAge,.12,LDTween::ECurve::EaseOut),1.f-LDTween::Progress(FlipAge-(LevelFlipSeconds-.12),.12,LDTween::ECurve::EaseIn));
        const FVector2D Centre=TopLeft+Extent*.5;
        const FVector2D Half(70.,FMath::Max(2.,32.*Open));
        const FBox2D Card(Centre-Half-FVector2D(14.,0.),Centre+Half-FVector2D(14.,0.));
        LDBroadcastPaint::FillSlab(P,P.Layer+6,Card,LDBroadcast::Orange);
        if(Open>.6f)
        {
            LDBroadcastPaint::Text(P,P.Layer+7,FString::Printf(TEXT("LV %d"),LevelFlipLevel),LDBroadcastPaint::FigureFont(40.f),Card.GetCenter(),LDBroadcast::Ink,FVector2D(.5,.5));
            if(LevelFlipPoints>0)
            {
                // Above the card, never over the health figure beside it.
                const FVector2D ChipAt(Card.Min.X,Card.Min.Y-Card.GetSize().Y-6.);
                const FString Gain=FString::Printf(TEXT("+%lld 点"),LevelFlipPoints);
                const FSlateFontInfo Font=LDBroadcastPaint::LabelFont(LDBroadcast::TypeBody,true);
                const FVector2D GainSize=LDBroadcastPaint::Measure(Gain,Font);
                LDBroadcastPaint::FillSlab(P,P.Layer+6,FBox2D(ChipAt,ChipAt+FVector2D(GainSize.X+28.,Card.GetSize().Y)),LDBroadcast::White);
                LDBroadcastPaint::Text(P,P.Layer+7,Gain,Font,ChipAt+FVector2D(14.,Card.GetSize().Y*.5),LDBroadcast::Ink,FVector2D(0.,.5));
            }
        }
    }
}
int32 ULDHUD::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);
    return Base+1;
}

void ULDHUD::PaintWorldPreviews(const FGeometry& G,FSlateWindowElementList& E,int32 Layer) const
{
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(bFrozenForTeardown||!PC||!Band){return;}
    const FVector2D LocalSize=G.GetLocalSize();
    if(LocalSize.X<=1.||LocalSize.Y<=1.){return;}
    int32 ViewW=0,ViewH=0;PC->GetViewportSize(ViewW,ViewH);
    const FVector2D ViewScale=LocalSize/FVector2D(FMath::Max(1,ViewW),FMath::Max(1,ViewH));
    const FVector2D ClipMin(-8.,-8.),ClipMax(LocalSize.X+8.,LocalSize.Y+8.);
    auto Lines=[&](const TArray<FVector2D>& Points,FLinearColor Color,float Thickness=2.f)
    {
        if(Points.Num()>=2){FSlateDrawElement::MakeLines(E,Layer,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Thickness);}
    };
    auto Project=[&](const FVector& P,FVector2D& Out)
    {
        FVector2D Screen;
        if(!PC->ProjectWorldLocationToScreen(P,Screen)){return false;}
        Out=Screen*ViewScale;
        return !Out.ContainsNaN();
    };
    // #335 §2.2: step along world segments every 50 cm and clip each projected sub-segment against the viewport so
    // aiming toward screen edges or past the camera near plane never drops the visible in-screen half of the corridor.
    auto DrawWorldSegment=[&](const FVector& WorldA,const FVector& WorldB,const FLinearColor& Color,float Thickness)
    {
        const double TotalCm=FVector::Dist(WorldA,WorldB);
        const int32 Steps=FMath::Clamp(FMath::CeilToInt32(TotalCm/50.),1,64);
        TArray<FVector2D> Run;
        Run.Reserve(Steps+2);
        for(int32 Step=0;Step<Steps;++Step)
        {
            const FVector SubA=FMath::Lerp(WorldA,WorldB,double(Step)/double(Steps));
            const FVector SubB=FMath::Lerp(WorldA,WorldB,double(Step+1)/double(Steps));
            FVector2D SA,SB;
            if(!Project(SubA,SA)||!Project(SubB,SB))
            {
                if(Run.Num()>=2){Lines(Run,Color,Thickness);}
                Run.Reset();
                continue;
            }
            if(!LDOrderRules::ClipViewportSegment(SA,SB,ClipMin,ClipMax))
            {
                if(Run.Num()>=2){Lines(Run,Color,Thickness);}
                Run.Reset();
                continue;
            }
            if(Run.IsEmpty()||FVector2D::DistSquared(Run.Last(),SA)>1.5)
            {
                if(Run.Num()>=2){Lines(Run,Color,Thickness);}
                Run.Reset();
                Run.Add(SA);
            }
            Run.Add(SB);
        }
        if(Run.Num()>=2){Lines(Run,Color,Thickness);}
    };
    auto RangeRing=[&](const FVector& Centre,float RadiusCm,const FLinearColor& Color,float Thickness)
    {
        if(!(RadiusCm>1.f)){return;}
        const int32 Segs=FMath::Clamp(FMath::CeilToInt32((2.*UE_DOUBLE_PI*double(RadiusCm))/50.),24,96);
        TArray<FVector2D> Run;
        Run.Reserve(Segs+2);
        for(int32 Step=0;Step<Segs;++Step)
        {
            const double A0=UE_DOUBLE_TWO_PI*double(Step)/double(Segs);
            const double A1=UE_DOUBLE_TWO_PI*double(Step+1)/double(Segs);
            FVector2D SA,SB;
            if(!Project(Centre+FVector(FMath::Cos(A0)*RadiusCm,FMath::Sin(A0)*RadiusCm,4.f),SA)
                ||!Project(Centre+FVector(FMath::Cos(A1)*RadiusCm,FMath::Sin(A1)*RadiusCm,4.f),SB)
                ||!LDOrderRules::ClipViewportSegment(SA,SB,ClipMin,ClipMax))
            {
                if(Run.Num()>=2){Lines(Run,Color,Thickness);}
                Run.Reset();
                continue;
            }
            if(Run.IsEmpty()||FVector2D::DistSquared(Run.Last(),SA)>1.5)
            {
                if(Run.Num()>=2){Lines(Run,Color,Thickness);}
                Run.Reset();
                Run.Add(SA);
            }
            Run.Add(SB);
        }
        if(Run.Num()>=2){Lines(Run,Color,Thickness);}
    };
    auto RationalCm=[](const FLDDemoRational& R)->double
    {
        return R.Denominator>0?double(R.Denominator?double(R.Numerator)/double(R.Denominator):0.):0.;
    };
    // #331: PK spectator throw aim draws a 120cm orange-yellow dashed circle + center reticle at the cursor's ground hit.
    if(bPKThrowArmed&&!PC->IsWorldInputSuppressed()&&PC->Navigation)
    {
        FHitResult ThrowGround;
        if(PC->Navigation->GroundUnderPointer(ThrowGround))
        {
            const float ThrowRadiusCm=bPKParamsValid&&PKParams.ThrowRadiusCm>0.f?float(PKParams.ThrowRadiusCm):120.f;
            const FLinearColor ThrowColor(1.f,.72f,.18f,.92f);
            constexpr int32 DashCount=24;
            for(int32 Dash=0;Dash<DashCount;++Dash)
            {
                const float A0=2.f*PI*(float(Dash)/float(DashCount));
                const float A1=2.f*PI*((float(Dash)+.62f)/float(DashCount));
                DrawWorldSegment(ThrowGround.ImpactPoint+FVector(FMath::Cos(A0)*ThrowRadiusCm,FMath::Sin(A0)*ThrowRadiusCm,4.f),
                    ThrowGround.ImpactPoint+FVector(FMath::Cos(A1)*ThrowRadiusCm,FMath::Sin(A1)*ThrowRadiusCm,4.f),ThrowColor,2.5f);
            }
            DrawWorldSegment(ThrowGround.ImpactPoint+FVector(-18.f,0,4.f),ThrowGround.ImpactPoint+FVector(18.f,0,4.f),ThrowColor,2.f);
            DrawWorldSegment(ThrowGround.ImpactPoint+FVector(0,-18.f,4.f),ThrowGround.ImpactPoint+FVector(0,18.f,4.f),ThrowColor,2.f);
        }
    }
    if(!PC->CanWorldCommand()){return;}
    ELDCombatEntry Armed=ELDCombatEntry::None;
    const bool bArmedEntry=PC->ReadArmedCombatEntry(Armed)&&PC->ShouldShowArmedPreview();
    const auto* Self=Cast<ALDCharacter>(PC->GetPawn());
    const FLDOwnerState& Own=PC->OwnerState;
    const bool bOwnCombat=Self&&Own.Life!=0&&PC->CombatView.bHasBody&&PC->CombatView.Generation==Own.Match&&PC->CombatView.Life==Own.Life;
    if(bOwnCombat&&!bArmedEntry&&!PC->IsAttackMoveArmed()&&PC->CombatView.AttackRangeCm>0)
    {
        const FVector Feet=Self->PresentedLocation()-FVector(0,0,Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        RangeRing(Feet,PC->CombatView.AttackRangeCm,FLinearColor(1.f,1.f,1.f,.22f),1.2f);
    }
    if(Self&&(bArmedEntry||PC->IsAttackMoveArmed()))
    {
        float RangeCm=0.f,ConeDegrees=0.f,HalfWidthCm=0.f;
        bool bDirection=false,bUnitTarget=false,bPoint=false,bCorridor=false;
        ELDDemoSkillElement SkillElem=ELDDemoSkillElement::None;
        TArray<float,TInlineAllocator<4>> EndpointRadiiCm;
        const FLDCombatEntryView* Source=nullptr;
        if(bArmedEntry)
        {
            Source=PC->CombatView.Entries.FindByPredicate([Armed](const FLDCombatEntryView& Candidate){return Candidate.Entry==Armed;});
            RangeCm=Source?float(Source->RangeCm):0.f;
            const ELDDemoTargetMode TargetMode=Source?ELDDemoTargetMode(Source->TargetMode):ELDDemoTargetMode::Self;
            bDirection=Source&&(TargetMode==ELDDemoTargetMode::DirectionProjectile||TargetMode==ELDDemoTargetMode::DirectionDash
                ||TargetMode==ELDDemoTargetMode::DirectionCone||TargetMode==ELDDemoTargetMode::DirectionLine);
            bCorridor=Source&&(TargetMode==ELDDemoTargetMode::DirectionProjectile||TargetMode==ELDDemoTargetMode::DirectionDash
                ||TargetMode==ELDDemoTargetMode::DirectionLine);
            bUnitTarget=Source&&TargetMode==ELDDemoTargetMode::Unit;
            bPoint=Source&&TargetMode==ELDDemoTargetMode::GroundPoint;
            bDirection|=bPoint;
            if(Source&&DisplayContent)
            {
                const FLDDemoContentDefinitions& Content=DisplayContent->Get();
                const FLDDemoAbilityDefinition* Ability=Content.Abilities.FindByPredicate([Source](const auto& Row)
                {return Row.Id==Source->DefinitionId.ToString();});
                if(Ability)
                {
                    ELDSkillBranch ActivePath=ELDSkillBranch::None;
                    for(const FLDActiveSlotView& Learned:Own.Growth.Progression.Actives)
                    {
                        if(Learned.DefinitionId==Source->DefinitionId){ActivePath=Learned.Branch;break;}
                    }
                    const FLDDemoAbilityBranch* Branch=ActivePath!=ELDSkillBranch::None
                        ?Ability->Branches.FindByPredicate([ActivePath](const FLDDemoAbilityBranch& B){return B.Path==ActivePath;}):nullptr;
                    const FLDDemoTargetDefinition& TargetDef=Branch?Branch->Target:Ability->Target;
                    SkillElem=(Branch&&Branch->Element!=ELDDemoSkillElement::None)?Branch->Element:Ability->Element;
                    if(SkillElem==ELDDemoSkillElement::None)
                    {
                        for(const FLDCardInstanceView& Inst:Own.Growth.Progression.Cards)
                        {
                            if(!Inst.IsInstalled()||Inst.Target.Kind!=ELDCardTargetKind::Skill||Inst.Target.Active!=Source->DefinitionId){continue;}
                            const FLDDemoCardDefinition* MedalDef=Content.Cards.FindByPredicate([&Inst](const FLDDemoCardDefinition& C)
                                {return FName(*C.Id)==Inst.DefinitionId;});
                            if(!MedalDef){continue;}
                            for(const FLDDemoEffectDefinition& Ef:MedalDef->Effects)
                            {
                                if(Ef.GetOp()!=ELDDemoEffectOp::SkillElement){continue;}
                                switch(Ef.Element)
                                {
                                case ELDDemoElement::Fire: SkillElem=ELDDemoSkillElement::Fire; break;
                                case ELDDemoElement::Water: SkillElem=ELDDemoSkillElement::Water; break;
                                case ELDDemoElement::Electric: SkillElem=ELDDemoSkillElement::Shock; break;
                                case ELDDemoElement::Chill: SkillElem=ELDDemoSkillElement::Chill; break;
                                case ELDDemoElement::Rot: SkillElem=ELDDemoSkillElement::Rot; break;
                                default: break;
                                }
                                if(SkillElem!=ELDDemoSkillElement::None){break;}
                            }
                            if(SkillElem!=ELDDemoSkillElement::None){break;}
                        }
                    }
                    if(TargetMode==ELDDemoTargetMode::DirectionCone)
                    {
                        ConeDegrees=float(RationalCm(TargetDef.AngleDegrees));
                    }
                    else if(TargetMode==ELDDemoTargetMode::DirectionProjectile)
                    {
                        HalfWidthCm=float(RationalCm(TargetDef.ProjectileRadiusCm));
                        if(HalfWidthCm<=0.f){HalfWidthCm=36.f;}
                    }
                    else if(TargetMode==ELDDemoTargetMode::DirectionLine)
                    {
                        HalfWidthCm=float(RationalCm(TargetDef.LineHalfWidthCm));
                        if(HalfWidthCm<=0.f){HalfWidthCm=45.f;}
                    }
                    else if(TargetMode==ELDDemoTargetMode::DirectionDash)
                    {
                        HalfWidthCm=float(Self->GetCapsuleComponent()->GetScaledCapsuleRadius());
                    }
                    const TArray<FLDDemoEffectDefinition>& Effects=Branch?Branch->Values.Effects:Ability->Values.Effects;
                    auto PushRadius=[&EndpointRadiiCm](double R)
                    {
                        if(R>8.)
                        {
                            const float Rf=float(R);
                            if(!EndpointRadiiCm.ContainsByPredicate([Rf](float Existing){return FMath::Abs(Existing-Rf)<2.f;}))
                            {
                                EndpointRadiiCm.Add(Rf);
                            }
                        }
                    };
                    for(const FLDDemoEffectDefinition& Ef:Effects)
                    {
                        if(Ef.GroundKind!=0)
                        {
                            const FLDDemoGroundParameters& GP=Content.Parameters.Ground;
                            double BaseR=0.;
                            switch(Ef.GroundKind)
                            {
                            case 1: BaseR=RationalCm(GP.PuddleRadiusCm); break;
                            case 2: BaseR=RationalCm(GP.IceRadiusCm); break;
                            case 3: BaseR=RationalCm(GP.FireRadiusCm); break;
                            case 4: BaseR=RationalCm(GP.MiasmaRadiusCm); break;
                            case 5: BaseR=RationalCm(GP.FieldRadiusCm); break;
                            default: break;
                            }
                            if(Ef.RatioBp>0){BaseR=BaseR*double(Ef.RatioBp)/10000.;}
                            PushRadius(BaseR);
                        }
                        const double DirectR=RationalCm(Ef.RadiusCm);
                        if(DirectR>0.&&(bPoint||TargetMode==ELDDemoTargetMode::DirectionProjectile||TargetMode==ELDDemoTargetMode::DirectionDash))
                        {
                            PushRadius(DirectR);
                        }
                    }
                    EndpointRadiiCm.Sort();
                }
            }
            if(bCorridor&&HalfWidthCm<=0.f){HalfWidthCm=32.f;}
        }
        else{RangeCm=PC->CombatView.AttackRangeCm;bUnitTarget=true;}
        const FVector AuthorityFeet=Self->GetActorLocation()-FVector(0,0,Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const FVector Feet=Self->PresentedLocation()-FVector(0,0,Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        FLinearColor Preview=.9f*FLinearColor(.36f,.84f,.98f,1.f);
        switch(SkillElem)
        {
        case ELDDemoSkillElement::Rot:   Preview=FLinearColor(122.f/255.f,217.f/255.f,58.f/255.f,.82f); break;
        case ELDDemoSkillElement::Fire:  Preview=FLinearColor(1.f,.45f,.16f,.85f); break;
        case ELDDemoSkillElement::Water: Preview=FLinearColor(.24f,.68f,1.f,.85f); break;
        case ELDDemoSkillElement::Shock: Preview=FLinearColor(1.f,.88f,.24f,.88f); break;
        case ELDDemoSkillElement::Chill: Preview=FLinearColor(.52f,.88f,1.f,.85f); break;
        default: Preview=bArmedEntry?FLinearColor(.36f,.84f,.98f,.85f):FLinearColor(1.f,.85f,.45f,.9f); break;
        }
        if(bUnitTarget&&RangeCm>0)
        {
            FGuid Handle;uint32 Epoch=0;FLDObservedUnitView Hovered;
            const ALDWorldViewRelay* Relay=PC->LocalView();
            if(PC->ObservedUnderPointer(Handle,Epoch)&&Relay&&Relay->ReadUnit(Handle,Epoch,Hovered))
            {
                const bool bInRange=FVector::Dist2D(AuthorityFeet,FVector(Hovered.FeetCm))<=RangeCm;
                Preview=bInRange?FLinearColor(.35f,1.f,.45f,.95f):FLinearColor(1.f,.35f,.3f,.95f);
            }
        }
        if(RangeCm>0){RangeRing(Feet,RangeCm,Preview.CopyWithNewOpacity(.55f),1.5f);}
        FVector AimWorldPoint=FVector::ZeroVector;
        if(bDirection&&PC->Navigation&&PC->Navigation->AimPointAtElevation(AuthorityFeet.Z,AimWorldPoint))
        {
            const FVector Dir=LDOrderRules::ResolveElevationAimDirection(AuthorityFeet,AimWorldPoint,Self->GetActorForwardVector());
            const double AimDist=FVector::Dist2D(AimWorldPoint,AuthorityFeet);
            const double ReachCm=RangeCm>0.f?(bPoint?FMath::Min(double(RangeCm),AimDist):double(RangeCm)):AimDist;
            const FVector Start=Feet+FVector(0,0,4.f);
            const FVector End=Start+Dir*ReachCm;
            if(bCorridor&&HalfWidthCm>0.f&&ReachCm>20.)
            {
                const FVector Right(-Dir.Y,Dir.X,0.);
                const double HeadBack=FMath::Min(52.,ReachCm*.28);
                const FVector Neck=End-Dir*HeadBack;
                DrawWorldSegment(Start+Right*HalfWidthCm,Neck+Right*HalfWidthCm,Preview,2.2f);
                DrawWorldSegment(Start-Right*HalfWidthCm,Neck-Right*HalfWidthCm,Preview,2.2f);
                DrawWorldSegment(Start,End,Preview.CopyWithNewOpacity(Preview.A*.42f),1.4f);
                const double Wing=HalfWidthCm*1.28;
                DrawWorldSegment(Neck+Right*Wing,End,Preview,2.6f);
                DrawWorldSegment(Neck-Right*Wing,End,Preview,2.6f);
                DrawWorldSegment(Neck+Right*HalfWidthCm,Neck+Right*Wing,Preview,2.f);
                DrawWorldSegment(Neck-Right*HalfWidthCm,Neck-Right*Wing,Preview,2.f);
            }
            else if(!bPoint)
            {
                DrawWorldSegment(Start,End,Preview,2.6f);
            }
            if(ConeDegrees>0.f&&RangeCm>0.f)
            {
                const double Heading=FMath::Atan2(Dir.Y,Dir.X);
                const double Half=FMath::DegreesToRadians(double(ConeDegrees)*.5);
                DrawWorldSegment(Start,Start+FVector(FMath::Cos(Heading-Half)*RangeCm,FMath::Sin(Heading-Half)*RangeCm,0.),Preview,2.2f);
                DrawWorldSegment(Start,Start+FVector(FMath::Cos(Heading+Half)*RangeCm,FMath::Sin(Heading+Half)*RangeCm,0.),Preview,2.2f);
                for(int32 Step=0;Step<24;++Step)
                {
                    const double A0=Heading-Half+2.*Half*double(Step)/24.;
                    const double A1=Heading-Half+2.*Half*double(Step+1)/24.;
                    DrawWorldSegment(Start+FVector(FMath::Cos(A0)*RangeCm,FMath::Sin(A0)*RangeCm,0.),
                        Start+FVector(FMath::Cos(A1)*RangeCm,FMath::Sin(A1)*RangeCm,0.),Preview,2.2f);
                }
            }
            for(int32 RIdx=0;RIdx<EndpointRadiiCm.Num();++RIdx)
            {
                const bool bInnerSweetSpot=EndpointRadiiCm.Num()>1&&RIdx==0;
                const FLinearColor RingTint=bInnerSweetSpot
                    ?FLinearColor(FMath::Min(1.f,Preview.R+.18f),FMath::Min(1.f,Preview.G+.18f),FMath::Min(1.f,Preview.B+.12f),.95f)
                    :Preview;
                RangeRing(End-FVector(0,0,4.f),EndpointRadiiCm[RIdx],RingTint,bInnerSweetSpot?2.4f:2.0f);
            }
        }
    }
}
