#include "UI/LDWorldLabels.h"
#include "UI/LDTerms.h"
#include "Core/LDLog.h"
#include "UI/LDHUD.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDControlSettings.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameState.h"
#include "Framework/LDPlayerController.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDWorldViewRelay.h"
#include "World/LDTrainingRoomDisplay.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "UI/LDUIFonts.h"

namespace
{
// Anchor height above the presented body centre, clear of the head and its status markers. The proxy draws the
// high-tier camp body larger, so its label sits higher; the own body's bars sit at a contender label's height.
double WorldLabelHeadroomCm(const LDWorldLabels::FLabel& Label)
{return Label.bContender?120.:Label.CreatureId==uint8(ELDDemoMonsterId::MonsterDean)?175.
    :(Label.CreatureId==uint8(ELDDemoMonsterId::MonsterAdvisor)||Label.CreatureId>=uint8(ELDDemoMonsterId::MonsterAuditor))?160.
    :(Label.CreatureId==uint8(ELDDemoMonsterId::MonsterGuardian)||Label.CreatureId==uint8(ELDDemoMonsterId::MonsterAgent)||Label.CreatureId==uint8(ELDDemoMonsterId::MonsterGuard))?150.:120.;}
constexpr double OwnBarsHeadroomCm=120.;
// Anchors further off-screen are skipped; the margin keeps a label whose anchor sits just past an edge.
constexpr float WorldLabelCullMargin=160.f;
// Local units at the 1080p reference; every bar, badge and pip also has a one-unit rim.
constexpr float WorldBarHeight=9.f,WorldBadgeSide=20.f,WorldBadgeGap=3.f,WorldRowGap=2.f;
constexpr float OwnBarWidth=84.f,OwnHealthHeight=8.f,OwnMomentumHeight=4.f;
// Own status icons over the own bars: squares at the reference text scale, the gap between them and how many fill a row.
constexpr float OwnStatusSide=22.f,OwnStatusGap=3.f;constexpr int32 OwnStatusPerRow=6;
// Contenders sit between the two camp tiers; the high-tier camp body is drawn larger, so its bar is wider.
float WorldBarWidth(const LDWorldLabels::FLabel& Label)
{return Label.bContender?96.f:Label.CreatureId==uint8(ELDDemoMonsterId::MonsterDean)?120.f
    :(Label.CreatureId==uint8(ELDDemoMonsterId::MonsterAdvisor)||Label.CreatureId>=uint8(ELDDemoMonsterId::MonsterAuditor))?96.f
    :(Label.CreatureId==uint8(ELDDemoMonsterId::MonsterGuardian)||Label.CreatureId==uint8(ELDDemoMonsterId::MonsterAgent)||Label.CreatureId==uint8(ELDDemoMonsterId::MonsterGuard))?84.f:64.f;}
const FLinearColor WorldLabelContenderInk(.96f,.97f,1.f);
const FLinearColor WorldLabelCampInk(1.f,.8f,.42f);
const FLinearColor WorldLabelStatusInk(.8f,.93f,1.f);
// Status text shares the body markers' tints (Combat's LDHumanoidVisualComponent): root magenta, shield cyan, respawn
// protection white-gold; other statuses keep the plain status ink.
FLinearColor WorldStatusInk(uint8 StatusId)
{
    switch(static_cast<ELDDemoStatusId>(StatusId))
    {
    case ELDDemoStatusId::Root: return FLinearColor(1.f,.02f,.58f);
    case ELDDemoStatusId::Shield: return LDHUDStyle::Shield;
    case ELDDemoStatusId::RespawnProtection: return FLinearColor(1.f,.92f,.62f);
    case ELDDemoStatusId::Disarm: return FLinearColor(1.f,.55f,.1f);
    case ELDDemoStatusId::Stun: return FLinearColor(1.f,.95f,.1f);
    // #147: silence violet, burn ember orange.
    case ELDDemoStatusId::Silence: return FLinearColor(.62f,.48f,1.f);
    case ELDDemoStatusId::Burn: return FLinearColor(1.f,.36f,.12f);
    case ELDDemoStatusId::AttackSlowed: return FLinearColor(.65f,.4f,.9f);
    // Spec §8.1 element colours: wet blue, shock violet, chill cyan, rot green; the S16 stance steel.
    case ELDDemoStatusId::Wet: return FLinearColor(.3f,.6f,1.f);
    case ELDDemoStatusId::Electrified: return FLinearColor(.72f,.42f,1.f);
    case ELDDemoStatusId::Chilled: return FLinearColor(.35f,.95f,1.f);
    case ELDDemoStatusId::Rotten: return FLinearColor(.45f,.85f,.2f);
    case ELDDemoStatusId::Braced: return FLinearColor(.7f,.8f,.95f);
    case ELDDemoStatusId::CampEnraged: return FLinearColor(1.f,.3f,.08f);
    case ELDDemoStatusId::CampGuarding: return FLinearColor(.55f,.8f,1.f);
    default: return WorldLabelStatusInk;
    }
}
// The interface font at the local text scale, outlined so a label reads over any ground or body.
FSlateFontInfo WorldLabelFont(const FName Typeface,int32 Size,float TextScale,int32 OutlineSize=2)
{
    FFontOutlineSettings Outline;Outline.OutlineSize=OutlineSize;Outline.OutlineColor=LDHUDStyle::Rim;
    return LDUIFonts::Font(Typeface==FName(TEXT("Bold"))?LDUIFonts::ERole::Strong:LDUIFonts::ERole::Body,float(FMath::Max(1,FMath::RoundToInt(Size*TextScale))),Outline);
}
}

LDWorldLabels::FLabel LDWorldLabels::Build(const FLDObservedUnitView& Unit,const FString& RosterName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec)
{
    FLabel Label;
    Label.bContender=Unit.Kind==ELDObservedUnitKind::Contender;
    if(Label.bContender){Label.Name=RosterName;Label.Presentation=Unit.Presentation;Label.Level=Unit.Level;}
    else
    {
        // P6 spec §11.2/§11.3: the dealer goes by its own name; an affixed camp member wears its affix glyph and name.
        Label.Name=Unit.bPeddler?FString(LDTerms::Peddler):LDHUD::CreatureName(Content,Unit.CreatureId);
        if(!Unit.bPeddler&&Unit.CampAffix!=0)
        {
            const ELDCampAffix Affix=ELDCampAffix(Unit.CampAffix);
            Label.Name=FString::Printf(TEXT("%s%s · %s"),LDTerms::AffixGlyph(Affix),LDTerms::AffixName(Affix),*Label.Name);
        }
        Label.CreatureId=Unit.CreatureId;
        const FLDDemoMonsterDefinition* Monster=Content&&!Unit.bPeddler?Content->Monsters.FindByPredicate([&Unit](const FLDDemoMonsterDefinition& M)
            {return M.NativeId==Unit.CreatureId;}):nullptr;
        if(Monster){Label.CampTier=int8(Monster->Tier);}
    }
    // As on the target card (LDHUD::ResourceP2), health without a known maximum or with a malformed current is unknown:
    // no bar rather than an empty one.
    Label.bHealth=Unit.Health.MaxP2>0&&Unit.Health.CurrentP2>=0;
    Label.bCharging=Label.bContender&&Unit.bCharging;
    if(Label.bHealth)
    {
        Label.HealthFraction=float(FMath::Clamp(double(Unit.Health.CurrentP2)/double(Unit.Health.MaxP2),0.,1.));
        Label.MaxHealth=float(double(Unit.Health.MaxP2)/100.);
        Label.ShieldFraction=float(Unit.ShieldBar)/float(LDObservation::ShieldBarFull);
    }
    Label.Handle=Unit.Handle;
    // The same parts as the target card's status line (LDHUD::ObservedCard), so both read alike.
    for(const FLDObservedStatus& Status:Unit.Statuses)
    {
        if(Status.StatusId==uint8(ELDDemoStatusId::Shield)){Label.bShield=true;}
        FString Part=LDHUD::StatusName(Content,Status.StatusId);
        if(Status.Stacks>1){Part+=FString::Printf(TEXT(" ×%d"),Status.Stacks);}
        if(Status.ExpiresAtServerMs>0){Part+=TEXT(" ")+LDHUD::TenthsUp(int64(FMath::RoundToDouble(double(Status.ExpiresAtServerMs)-SampledAtServerSec*1000.)));}
        Label.Status+=(Label.Status.IsEmpty()?TEXT(""):TEXT(" · "))+Part;
        Label.StatusParts.Add({MoveTemp(Part),Status.StatusId});
    }
    return Label;
}

LDWorldLabels::FOwnBars LDWorldLabels::OwnBars(const FLDOwnerState& Own,uint32 PublicGeneration,const FLDCombatOwnerView& Combat)
{
    FOwnBars Bars;
    // The HUD band's gate (ULDHUD::RefreshReadModel): the own combat projection of the current public match and this
    // life, alive with a body. Unknown or malformed health shows no bars rather than empty ones.
    if(PublicGeneration==0||PublicGeneration!=Own.Match||Own.Life==0||Own.LifePhase!=ELDLifePhase::Alive||Combat.Generation!=Own.Match
        ||Combat.Life!=Own.Life||!Combat.bHasBody||Combat.MaxHPP2<=0||Combat.HPP2<0){return Bars;}
    Bars.bVisible=true;
    const double MaxHealth=double(Combat.MaxHPP2);
    double Health=FMath::Clamp(double(Combat.HPP2)/MaxHealth,0.,1.),Shield=FMath::Max(double(Combat.ShieldP2),0.)/MaxHealth;
    if(Health+Shield>1.){const double Total=Health+Shield;Health/=Total;Shield/=Total;}
    Bars.Health=float(Health);Bars.Shield=float(Shield);
    Bars.bMomentum=Combat.MaxMomentumP2>0&&Combat.MomentumP2>=0;
    if(Bars.bMomentum){Bars.Momentum=float(FMath::Clamp(double(Combat.MomentumP2)/double(Combat.MaxMomentumP2),0.,1.));}
    return Bars;
}

TArray<FSlateRect> LDWorldLabels::OwnStatusIconRects(int32 Count,float CentreX,float Bottom,float Side,float Gap,int32 PerRow)
{
    TArray<FSlateRect> Rects;
    if(Count<=0||PerRow<=0||Side<=0.f){return Rects;}
    Rects.Reserve(Count);
    for(int32 Index=0;Index<Count;++Index)
    {
        const int32 Row=Index/PerRow,Col=Index%PerRow;
        const int32 InRow=FMath::Min(PerRow,Count-Row*PerRow);
        const float RowWidth=InRow*Side+(InRow-1)*Gap;
        const float Left=CentreX-RowWidth*.5f+Col*(Side+Gap);
        const float RowBottom=Bottom-Row*(Side+Gap);
        Rects.Add(FSlateRect(Left,RowBottom-Side,Left+Side,RowBottom));
    }
    return Rects;
}

void ULDWorldLabels::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);
    if(bDisplayContentTried){return;}
    // Missing content stays visible: names then read as the HUD reports them (未知野怪 #n, stable status ids).
    bDisplayContentTried=true;FString Error;
    if(!LDDemoEntry::LoadRuntimeContent(DisplayContent,Error)||!DisplayContent){DisplayContent.Reset();UE_LOG(LogLDUI,Error,TEXT("LD_WORLD_LABELS_CONTENT_UNAVAILABLE %s"),*Error);}
}

int32 ULDWorldLabels::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());UWorld* World=GetWorld();
    const ALDGameState* Public=World?World->GetGameState<ALDGameState>():nullptr;
    FSlateRenderer* Renderer=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetRenderer():nullptr;
    // Nothing is drawn before the display content was tried, so a first frame never flashes fallback ids.
    if(!bDisplayContentTried||!PC||!Public||!Renderer){return Base;}
    // A bar out of sight for a while forgets its trail.
    {const double TrailNow=FPlatformTime::Seconds();for(auto It=Trails.CreateIterator();It;++It){if(TrailNow-It.Value().SeenAt>2.){It.RemoveCurrent();}}}
    if(PC->HUDUI){PC->HUDUI->PaintWorldPreviews(G,E,Base+1);}
    const FLDOwnerState& Own=PC->OwnerState;
    // Only the current authorized frame of this match and life, read on every paint: a dead, stale or foreign frame
    // labels nothing, and no unit outlives the frame that lists it (no LastSeen).
    const ALDWorldViewRelay* Relay=Own.LifePhase==ELDLifePhase::Alive?PC->LocalView():nullptr;
    const FLDObservedFrameRead Read=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
    const FLDWorldView* Frame=Read.Frame&&Read.Status==ELDObservedViewStatus::Current&&Read.Frame->MatchGeneration==Own.Match&&Read.Frame->ObserverLife==Own.Life?Read.Frame:nullptr;
    // The own bars read only this controller's own projections of the current match and life, anchored on its own pawn,
    // so they show even while no other unit is in sight.
    const LDWorldLabels::FOwnBars SelfBars=LDWorldLabels::OwnBars(Own,Public->PublicMatch.Generation,PC->CombatView);
    const ALDCharacter* OwnBody=SelfBars.bVisible?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    // Without the own combat projection there are no bars, yet the HUD's syncing line still sits over the own pawn.
    const FString OwnNotice=PC->HUDUI?PC->HUDUI->GetOwnStatusNotice():FString();
    const ALDCharacter* NoticeBody=!OwnBody&&!OwnNotice.IsEmpty()&&Own.LifePhase==ELDLifePhase::Alive?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    int32 ViewW=0,ViewH=0;PC->GetViewportSize(ViewW,ViewH);
    if((!Frame&&!IsValid(OwnBody)&&!IsValid(NoticeBody))||ViewW<=0||ViewH<=0){if(PC->HUDUI){PC->HUDUI->ReportOwnStatusRects({});}return Base+1;}
    // Viewport pixels to local units: the layer fills the viewport, so this also removes the DPI scale.
    const FVector2f Size=G.GetLocalSize();const float ToLocalX=Size.X/float(ViewW),ToLocalY=Size.Y/float(ViewH);
    auto Culled=[&Size](float X,float Y){return X<-WorldLabelCullMargin||X>Size.X+WorldLabelCullMargin||Y<0.f||Y>Size.Y+WorldLabelCullMargin;};
    const float TextScale=PC->Controls?PC->Controls->TextScale:1.f;
    const FSlateFontInfo ContenderFont=WorldLabelFont(TEXT("Bold"),LDHUDStyle::TypeText,TextScale),CampFont=WorldLabelFont(TEXT("Regular"),LDHUDStyle::TypeText,TextScale),StatusFont=WorldLabelFont(TEXT("Regular"),LDHUDStyle::TypeSmall,TextScale);
    // The level badge grows with the text preference, so its number always fits.
    const FSlateFontInfo LevelFont=WorldLabelFont(TEXT("Bold"),LDHUDStyle::TypeCaption,TextScale,1);
    const float BadgeSide=FMath::RoundToFloat(WorldBadgeSide*TextScale),BadgeRim=BadgeSide+2.f;
    const TSharedRef<FSlateFontMeasure> FontMeasure=Renderer->GetFontMeasureService();
    const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const FLDDemoContentDefinitions* Content=DisplayContent?&DisplayContent->Get():nullptr;
    // Layers: rims, then tracks and badge tints, then fills, then text.
    auto Box=[&](int32 Layer,const FVector2f& At,const FVector2f& Extent,const FLinearColor& Tint)
    {FSlateDrawElement::MakeBox(E,Base+Layer,G.ToPaintGeometry(Extent,FSlateLayoutTransform(At)),White,ESlateDrawEffect::None,Tint);};
    // A text row centred on its unit but kept whole inside the viewport's left and right edges.
    auto RowLeft=[&Size](float CentreX,float Width)
    {
        constexpr float Edge=4.f;
        return Width+Edge*2.f>=Size.X?Edge:FMath::Clamp(CentreX-Width*.5f,Edge,Size.X-Width-Edge);
    };
    // One centred line whose bottom edge rests on Bottom; Bottom then moves up to the line's top edge.
    auto Line=[&](const FString& Text,const FSlateFontInfo& Font,const FLinearColor& Ink,float CentreX,float& Bottom)
    {
        const FVector2f Extent=FontMeasure->Measure(Text,Font);const FVector2f At(RowLeft(CentreX,Extent.X),Bottom-Extent.Y);
        FSlateDrawElement::MakeText(E,Base+4,G.ToPaintGeometry(Extent,FSlateLayoutTransform(At)),Text,Font,ESlateDrawEffect::None,Ink);
        Bottom=At.Y;
    };
    if (Frame && Content)
    {
        for (const auto& Effect : Frame->Projectiles)
        {
            const TCHAR* Id=Effect.Look==ELDObservedProjectileLook::SpeechCone ? TEXT("active_open_mic")
                : Effect.Look==ELDObservedProjectileLook::BookShadow ? TEXT("active_calculus_drop")
                : Effect.Look==ELDObservedProjectileLook::FlyerArmed || Effect.Look==ELDObservedProjectileLook::FlyerArming
                    ? TEXT("active_flyer_trap") : nullptr;
            if (!Id) { continue; }
            const auto* Display=Content->Display.FindByPredicate([Id](const auto& Row) { return Row.Id==Id; });
            if (!Display) { continue; }
            FVector Point=FVector(Effect.PositionCm)+FVector(0,0,35);
            const bool Speech=Effect.Look==ELDObservedProjectileLook::SpeechCone;
            if (Speech)
            {
                const double Angle=double(Effect.Heading)/65536.*2.*PI;
                Point+=FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Effect.RadiusCm*.5;
                Point.Z+=70;
            }
            FVector2D Screen;
            if (!PC->ProjectWorldLocationToScreen(Point,Screen)) { continue; }
            const float X=float(Screen.X)*ToLocalX;
            float Bottom=float(Screen.Y)*ToLocalY;
            if (Culled(X,Bottom)) { continue; }
            Line(Speech ? Display->NameText.Right(2)+TEXT("！") : Display->NameText,
                WorldLabelFont(TEXT("Bold"),Speech ? 24 : 15,TextScale),
                FLinearColor(1.f,.72f,.28f),X,Bottom);
        }
    }
    TArray<FSlateRect> Placed;
    TArray<FSlateRect> OwnMarkRects;
    const FSlateFontInfo StackFont=WorldLabelFont(TEXT("Bold"),LDHUDStyle::TypeTiny,TextScale,1);
    const TArray<FSlateRect> HudReserved=PC->HUDUI?PC->HUDUI->WorldLabelReservedAreas(G):TArray<FSlateRect>();
    FVector2D OwnScreen;
    if(IsValid(OwnBody)&&PC->ProjectWorldLocationToScreen(OwnBody->PresentedLocation()+FVector(0,0,OwnBarsHeadroomCm),OwnScreen))
    {
        const float OwnX=float(OwnScreen.X)*ToLocalX,OwnBottom=float(OwnScreen.Y)*ToLocalY;
        if(!Culled(OwnX,OwnBottom))
        {
            // Bottom-up from the anchor: momentum under health (the shield after its fill) in one rim.
            const float RimW=OwnBarWidth+2.f,RimH=OwnHealthHeight+2.f+(SelfBars.bMomentum?OwnMomentumHeight+1.f:0.f);
            const float RimLeft=OwnX-RimW*.5f,RimTop=OwnBottom-RimH,BarX=RimLeft+1.f,HealthTop=RimTop+1.f;
            Box(1,FVector2f(RimLeft,RimTop),FVector2f(RimW,RimH),LDHUDStyle::Rim);
            Box(2,FVector2f(BarX,HealthTop),FVector2f(OwnBarWidth,OwnHealthHeight),LDHUDStyle::BarTrack);
            if(SelfBars.Health>0.f){Box(3,FVector2f(BarX,HealthTop),FVector2f(OwnBarWidth*SelfBars.Health,OwnHealthHeight),LDHUDStyle::OwnHealth);}
            if(SelfBars.Shield>0.f){Box(3,FVector2f(BarX+OwnBarWidth*SelfBars.Health,HealthTop),FVector2f(OwnBarWidth*SelfBars.Shield,OwnHealthHeight),LDHUDStyle::Shield);}
            if(SelfBars.bMomentum)
            {
                const float MomentumTop=HealthTop+OwnHealthHeight+1.f;
                Box(2,FVector2f(BarX,MomentumTop),FVector2f(OwnBarWidth,OwnMomentumHeight),LDHUDStyle::BarTrack);
                if(SelfBars.Momentum>0.f){Box(3,FVector2f(BarX,MomentumTop),FVector2f(OwnBarWidth*SelfBars.Momentum,OwnMomentumHeight),SelfBars.Momentum>=1.f?LDHUDStyle::Overdrive:LDHUDStyle::Momentum);}
            }
            // The own level badge left of the bars, like every contender's: the own progression of this match, in the own
            // identity tint. Without a known level there is no badge rather than a guessed number.
            const FLDProgressionView* Progression=PC->ReadOwnProgression();
            const ALDCharacter* OwnCharacter=Cast<ALDCharacter>(OwnBody);
            const bool bOwnBadge=Progression&&Progression->Level>=1&&Progression->Level<=Progression->MaxLevel&&OwnCharacter;
            float ReachLeft=RimLeft;
            if(bOwnBadge)
            {
                const float BadgeLeft=RimLeft-WorldBadgeGap-BadgeRim,BadgeTop=RimTop+RimH*.5f-BadgeRim*.5f;
                Box(1,FVector2f(BadgeLeft,BadgeTop),FVector2f(BadgeRim,BadgeRim),LDHUDStyle::Rim);
                Box(2,FVector2f(BadgeLeft+1.f,BadgeTop+1.f),FVector2f(BadgeSide,BadgeSide),LDHUD::PaletteColor(OwnCharacter->PresentationKey));
                const FString LevelText=FString::FromInt(Progression->Level);const FVector2f LevelExtent=FontMeasure->Measure(LevelText,LevelFont);
                const FVector2f LevelAt(BadgeLeft+BadgeRim*.5f-LevelExtent.X*.5f,BadgeTop+BadgeRim*.5f-LevelExtent.Y*.5f);
                FSlateDrawElement::MakeText(E,Base+4,G.ToPaintGeometry(LevelExtent,FSlateLayoutTransform(LevelAt)),LevelText,LevelFont,ESlateDrawEffect::None,FLinearColor::White);
                ReachLeft=BadgeLeft;
            }
            // The own public roster name over the bars, as every other unit's name sits over its bar; an unlisted name is
            // left out rather than invented.
            float ReachTop=bOwnBadge?FMath::Min(RimTop,RimTop+RimH*.5f-BadgeRim*.5f):RimTop;
            float ReachRight=RimLeft+RimW;
            const FString OwnName=LDHUD::RosterName(Public->DemoLobby,Own.ContenderId);
            if(!OwnName.IsEmpty())
            {
                const FVector2f NameExtent=FontMeasure->Measure(OwnName,ContenderFont);
                float NameBottom=ReachTop-WorldRowGap;
                Line(OwnName,ContenderFont,WorldLabelContenderInk,OwnX,NameBottom);
                ReachTop=NameBottom;ReachLeft=FMath::Min(ReachLeft,OwnX-NameExtent.X*.5f);ReachRight=FMath::Max(ReachRight,OwnX+NameExtent.X*.5f);
            }
            // The own statuses as icons above the name, LoL-style: rows of OwnStatusPerRow squares centred over the bars,
            // later rows stacking upward. Each square is its picture (or the name's first character) on a dark tile with a
            // green (helps) or red (hurts) rim, the stacks over 1 in the bottom-right corner, and a timed status's remaining
            // time as a shrinking line along its bottom edge. Hovering one opens the HUD's slot popup.
            const TArray<FLDOwnStatusMark>* Marks=PC->HUDUI?&PC->HUDUI->GetOwnStatusMarks():nullptr;
            if(Marks&&!Marks->IsEmpty())
            {
                const float Side=FMath::RoundToFloat(OwnStatusSide*TextScale);
                const TArray<FSlateRect> Squares=LDWorldLabels::OwnStatusIconRects(Marks->Num(),OwnX,ReachTop-WorldRowGap-1.f,Side,OwnStatusGap,OwnStatusPerRow);
                for(int32 Index=0;Index<Squares.Num();++Index)
                {
                    const FLDOwnStatusMark& Mark=(*Marks)[Index];const FSlateRect& Square=Squares[Index];
                    const FLinearColor Edge=Mark.bPositive?LDHUDStyle::StatusPositive:LDHUDStyle::StatusNegative;
                    const FVector2f At(Square.Left,Square.Top);
                    Box(1,At-FVector2f(1.f,1.f),FVector2f(Side+2.f,Side+2.f),Edge);
                    Box(2,At,FVector2f(Side,Side),FLinearColor(.02f,.03f,.05f,.92f));
                    if(const FSlateBrush* Picture=PC->HUDUI->OwnStatusBrush(Mark.ViewIndex))
                    {FSlateDrawElement::MakeBox(E,Base+3,G.ToPaintGeometry(FVector2f(Side,Side),FSlateLayoutTransform(At)),Picture,ESlateDrawEffect::None,FLinearColor::White);}
                    else
                    {
                        const FVector2f GlyphExtent=FontMeasure->Measure(Mark.Glyph,LevelFont);
                        const FVector2f GlyphAt(At.X+Side*.5f-GlyphExtent.X*.5f,At.Y+Side*.5f-GlyphExtent.Y*.5f);
                        FSlateDrawElement::MakeText(E,Base+4,G.ToPaintGeometry(GlyphExtent,FSlateLayoutTransform(GlyphAt)),Mark.Glyph,LevelFont,ESlateDrawEffect::None,Edge);
                    }
                    if(Mark.Stacks>1)
                    {
                        const FString StackText=FString::FromInt(Mark.Stacks);const FVector2f StackExtent=FontMeasure->Measure(StackText,StackFont);
                        const FVector2f StackAt(At.X+Side-StackExtent.X+1.f,At.Y+Side-StackExtent.Y+1.f);
                        FSlateDrawElement::MakeText(E,Base+4,G.ToPaintGeometry(StackExtent,FSlateLayoutTransform(StackAt)),StackText,StackFont,ESlateDrawEffect::None,FLinearColor::White);
                    }
                    if(Mark.Remaining>=0.f)
                    {
                        Box(3,FVector2f(At.X,At.Y+Side-2.f),FVector2f(Side,2.f),FLinearColor(0.f,0.f,0.f,.7f));
                        if(Mark.Remaining>0.f){Box(4,FVector2f(At.X,At.Y+Side-2.f),FVector2f(Side*FMath::Clamp(Mark.Remaining,0.f,1.f),2.f),Edge);}
                    }
                    OwnMarkRects.Add(FSlateRect(FVector2D(G.LocalToAbsolute(FVector2f(Square.Left,Square.Top))),FVector2D(G.LocalToAbsolute(FVector2f(Square.Right,Square.Bottom)))));
                    ReachTop=FMath::Min(ReachTop,Square.Top-1.f);ReachLeft=FMath::Min(ReachLeft,Square.Left-1.f);ReachRight=FMath::Max(ReachRight,Square.Right+1.f);
                }
            }
            else if(!OwnNotice.IsEmpty())
            {
                // The own projection is missing: one small grey line in place of the squares, so no status never reads as none.
                // Painted only; it reports no hover rect.
                const float NoticeHalf=FontMeasure->Measure(OwnNotice,StatusFont).X*.5f;
                float NoticeBottom=ReachTop-WorldRowGap;
                Line(OwnNotice,StatusFont,LDHUDStyle::TextSecondary,OwnX,NoticeBottom);
                ReachTop=NoticeBottom;ReachLeft=FMath::Min(ReachLeft,OwnX-NoticeHalf);ReachRight=FMath::Max(ReachRight,OwnX+NoticeHalf);
            }
            // Placed before any label, so a unit's label moves above the own bars instead of covering them.
            Placed.Add(FSlateRect(ReachLeft,ReachTop,ReachRight,OwnBottom));
        }
    }
    else if(IsValid(NoticeBody)&&PC->ProjectWorldLocationToScreen(NoticeBody->PresentedLocation()+FVector(0,0,OwnBarsHeadroomCm),OwnScreen))
    {
        // No bars without the projection: the syncing line alone rests on the bars' anchor. Painted only, never hit-tested.
        const float OwnX=float(OwnScreen.X)*ToLocalX,OwnBottom=float(OwnScreen.Y)*ToLocalY;
        if(!Culled(OwnX,OwnBottom))
        {
            const float NoticeHalf=FontMeasure->Measure(OwnNotice,StatusFont).X*.5f;
            float NoticeTop=OwnBottom;
            Line(OwnNotice,StatusFont,LDHUDStyle::TextSecondary,OwnX,NoticeTop);
            Placed.Add(FSlateRect(OwnX-NoticeHalf,NoticeTop,OwnX+NoticeHalf,OwnBottom));
        }
    }
    if(PC->HUDUI){PC->HUDUI->ReportOwnStatusRects(MoveTemp(OwnMarkRects));}
    // Labels are measured first and placed lowest-on-screen first; a label that would overlap one already placed (the own
    // bars come first) moves up above it, so units standing together keep readable labels instead of a garbled stack.
    struct FPending
    {
        LDWorldLabels::FLabel Label;
        // LeftReach/RightReach: the bar row around the unit; TextWidth: the widest text row, drawn kept inside the viewport.
        float CentreX=0.f,Bottom=0.f,LeftReach=0.f,RightReach=0.f,TextWidth=0.f,Height=0.f,Row=0.f;
        bool bPublicService=false;
    };
    TArray<FPending> Pending;
    if(Public->PublicMatch.Generation==Own.Match&&Public->PublicMatch.Phase==ELDMatchPhase::Rest)
    {
        const auto PublicLabel=[&](const FVector& Position,const FString& Name)
        {
            FVector2D Screen;
            if(!PC->ProjectWorldLocationToScreen(Position,Screen)){return;}
            FPending Entry;Entry.Label.Name=Name;Entry.bPublicService=true;
            Entry.CentreX=float(Screen.X)*ToLocalX;Entry.Bottom=float(Screen.Y)*ToLocalY;
            if(Culled(Entry.CentreX,Entry.Bottom)){return;}
            const FVector2f Extent=FontMeasure->Measure(Name,CampFont);
            Entry.TextWidth=Extent.X;Entry.Height=Extent.Y;
            Pending.Add(MoveTemp(Entry));
        };
        for(TActorIterator<ALDTrainingRoomDisplay> It(World);It;++It)
        {PublicLabel(It->GetActorLocation()+FVector(7500,950,40),TEXT("训练区 · 共享假人"));}
    }
    if(Frame)
    {
        for(const FLDObservedUnitView& Unit:Frame->Units)
        {
            // Only a unit whose proxy is presented right now has an anchor: the interpolated pose the player sees.
            FVector Centre;
            if(!Relay->ReadPresentedLocation(Unit.Handle,Unit.ObservationEpoch,Centre)){continue;}
            const bool bContender=Unit.Kind==ELDObservedUnitKind::Contender;
            FPending Entry;
            Entry.Label=LDWorldLabels::Build(Unit,bContender?LDHUD::ObservedContenderName(Public->DemoLobby,Unit.ContenderId,
                Public->PublicMatch.Phase==ELDMatchPhase::Rest):FString(),Content,Frame->SampledAtServerSec);
            if(bContender)
            {
                const FString Relation=LDHUD::PKRelationText(Public->PublicMatch,Own.ContenderId,Unit.ContenderId);
                if(!Entry.Label.Name.IsEmpty()&&!Relation.IsEmpty()){Entry.Label.Name+=TEXT(" · ")+Relation;}
                // Nobody can be attacked in a rest outside the PK itself, and a PK teammate never can; the shared training
                // dummies stay attackable.
                const ELDRestStep Step=Public->PublicMatch.Timeline.Step;
                const bool bFighting=Step==ELDRestStep::PKReady||Step==ELDRestStep::PK;
                const bool bDummy=Entry.Label.Name.StartsWith(LDTerms::TrainingDummy);
                Entry.Label.bPeaceful=!bDummy&&((Public->PublicMatch.Phase==ELDMatchPhase::Rest&&!bFighting)||Relation==LDTerms::PKTeammate);
            }
            const LDWorldLabels::FLabel& Label=Entry.Label;
            FVector2D Screen;
            if((Label.Name.IsEmpty()&&Label.Status.IsEmpty()&&!Label.bHealth)||!PC->ProjectWorldLocationToScreen(Centre+FVector(0,0,WorldLabelHeadroomCm(Label)),Screen)){continue;}
            Entry.CentreX=float(Screen.X)*ToLocalX;Entry.Bottom=float(Screen.Y)*ToLocalY;
            if(Culled(Entry.CentreX,Entry.Bottom)){continue;}
            // The bar row: the bar centred on the unit, a contender's level badge left of it and centred on it vertically.
            if(Label.bHealth)
            {
                const float BarRim=WorldBarWidth(Label)+2.f;const bool bBadge=Label.bContender&&Label.Level>0;
                Entry.Row=FMath::Max(WorldBarHeight+2.f,bBadge?BadgeRim:0.f);
                Entry.LeftReach=BarRim*.5f+(bBadge?WorldBadgeGap+BadgeRim:0.f);Entry.RightReach=BarRim*.5f;
            }
            float Lines=0.f;
            if(!Label.Name.IsEmpty())
            {
                const FVector2f Name=FontMeasure->Measure(Label.Name,Label.bContender?ContenderFont:CampFont);
                Entry.TextWidth=FMath::Max(Entry.TextWidth,Name.X);Lines+=Name.Y;
            }
            if(!Label.Status.IsEmpty())
            {
                const FVector2f Status=FontMeasure->Measure(Label.Status,StatusFont);
                Entry.TextWidth=FMath::Max(Entry.TextWidth,Status.X);Lines+=Status.Y;
            }
            Entry.Height=Entry.Row+(Entry.Row>0.f&&Lines>0.f?WorldRowGap:0.f)+Lines;
            Pending.Add(MoveTemp(Entry));
        }
    }
    Pending.Sort([](const FPending& A,const FPending& B){return A.Bottom>B.Bottom;});
    for(FPending& Entry:Pending)
    {
        float Bottom=Entry.Bottom;
        // The space the label really draws in: its bar row around the unit and its text rows where RowLeft puts them.
        const bool bBar=Entry.LeftReach>0.f||Entry.RightReach>0.f;
        const float TextLeft=RowLeft(Entry.CentreX,Entry.TextWidth);
        const float Left=bBar?FMath::Min(Entry.CentreX-Entry.LeftReach,TextLeft):TextLeft;
        const float Right=bBar?FMath::Max(Entry.CentreX+Entry.RightReach,TextLeft+Entry.TextWidth):TextLeft+Entry.TextWidth;
        for(bool bMoved=true;bMoved;)
        {
            bMoved=false;
            for(const FSlateRect& Other:HudReserved)
            {
                if(Left<Other.Right&&Right>Other.Left&&Bottom-Entry.Height<Other.Bottom&&Bottom>Other.Top)
                {Bottom=Other.Top-2.f;bMoved=true;}
            }
            for(const FSlateRect& Other:Placed)
            {
                if(Left<Other.Right&&Right>Other.Left&&Bottom-Entry.Height<Other.Bottom&&Bottom>Other.Top){Bottom=Other.Top-2.f;bMoved=true;}
            }
        }
        if(Entry.bPublicService&&Bottom<Entry.Height)
        {
            Bottom=Entry.Bottom+Entry.Height+2.f;
            for(bool bMoved=true;bMoved;)
            {
                bMoved=false;
                const auto MoveBelow=[&](const TArray<FSlateRect>& Areas)
                {
                    for(const FSlateRect& Other:Areas)
                    {
                        if(Left<Other.Right&&Right>Other.Left&&Bottom-Entry.Height<Other.Bottom&&Bottom>Other.Top)
                        {Bottom=Other.Bottom+Entry.Height+2.f;bMoved=true;}
                    }
                };
                MoveBelow(HudReserved);MoveBelow(Placed);
            }
            if(Bottom>Size.Y){continue;}
        }
        if(Bottom<Entry.Height){continue;}
        Placed.Add(FSlateRect(Left,Bottom-Entry.Height,Right,Bottom));
        const LDWorldLabels::FLabel& Label=Entry.Label;
        const float CentreX=Entry.CentreX;
        if(Label.bHealth)
        {
            const float BarWidth=WorldBarWidth(Label),BarRimHeight=WorldBarHeight+2.f;
            // The bar's memory for this visibility interval: the damage trail, a heavy hit's shake and a shield's break.
            const double PaintNow=FPlatformTime::Seconds();
            float TrailShown=Label.HealthFraction,Shake=0.f,Burst=-1.f;
            if(Label.Handle.IsValid())
            {
                ULDWorldLabels::FTrail& Trail=Trails.FindOrAdd(Label.Handle);
                if(Trail.SeenAt<=0.||Label.HealthFraction>=Trail.Shown){Trail.Shown=Label.HealthFraction;Trail.HeldUntil=PaintNow+.15;}
                else if(PaintNow>Trail.HeldUntil)
                {Trail.Shown=FMath::Max(Label.HealthFraction,Trail.Shown-float(FMath::Min(PaintNow-Trail.SeenAt,.1))*1.6f);}
                // A single drop of more than 18% of the maximum shakes the bar; a shield taken to nothing bursts.
                if(Trail.LastHealth>=0.f&&Trail.LastHealth-Label.HealthFraction>.18f){Trail.ShakeUntil=PaintNow+.25;}
                if(Trail.LastShield>0.f&&Label.ShieldFraction<=0.f&&Label.HealthFraction>0.f){Trail.BrokeAt=PaintNow;}
                Trail.LastHealth=Label.HealthFraction;Trail.LastShield=Label.ShieldFraction;
                Trail.SeenAt=PaintNow;
                TrailShown=Trail.Shown;
                if(PaintNow<Trail.ShakeUntil){Shake=2.5f*FMath::Sin(float(PaintNow*90.))*float((Trail.ShakeUntil-PaintNow)/.25);}
                if(Trail.BrokeAt>=0.&&PaintNow-Trail.BrokeAt<.35){Burst=float((PaintNow-Trail.BrokeAt)/.35);}
            }
            const float Mid=Bottom-Entry.Row*.5f,BarLeft=CentreX-(BarWidth+2.f)*.5f+Shake,BarTop=Mid-BarRimHeight*.5f;
            // An overdrive charge: a pulsing gold halo around the bar, visible to every observer of the unit.
            if(Label.bCharging)
            {
                const float Pulse=.55f+.45f*FMath::Abs(FMath::Sin(float(FPlatformTime::Seconds())*9.f));
                Box(0,FVector2f(BarLeft-3.f,BarTop-3.f),FVector2f(BarWidth+8.f,BarRimHeight+6.f),LDHUDStyle::Overdrive.CopyWithNewOpacity(Pulse));
            }
            // A camp member's bar sits in a frame of its tier colour (the name carries the same colour).
            if(Label.CampTier>=0){Box(1,FVector2f(BarLeft-1.f,BarTop-1.f),FVector2f(BarWidth+4.f,BarRimHeight+2.f),LDHUDStyle::CampTierInk(uint8(Label.CampTier)));}
            Box(1,FVector2f(BarLeft,BarTop),FVector2f(BarWidth+2.f,BarRimHeight),LDHUDStyle::Rim);
            const float TrackTop=BarTop+1.f;
            Box(2,FVector2f(BarLeft+1.f,TrackTop),FVector2f(BarWidth,WorldBarHeight),LDHUDStyle::BarTrack);
            // Health, then the shield in proportion to the maximum HP right after it; when both exceed the bar they share it.
            const float Fill=Label.HealthFraction+Label.ShieldFraction,Squeeze=Fill>1.f?1.f/Fill:1.f;
            const float HealthW=BarWidth*Label.HealthFraction*Squeeze,ShieldW=BarWidth*Label.ShieldFraction*Squeeze;
            // The damage trail: what was just lost stays bright for a moment, then closes on the current fill.
            if(TrailShown>Label.HealthFraction)
            {Box(3,FVector2f(BarLeft+1.f+HealthW,TrackTop),FVector2f(FMath::Min(BarWidth*(TrailShown-Label.HealthFraction)*Squeeze,BarWidth-HealthW),WorldBarHeight),FLinearColor(1.f,.96f,.86f,.9f));}
            if(Label.HealthFraction>0.f)
            {Box(3,FVector2f(BarLeft+1.f,TrackTop),FVector2f(HealthW,WorldBarHeight),!Label.bContender?LDHUDStyle::CampHealth:Label.bPeaceful?LDHUDStyle::PeacefulHealth:LDHUDStyle::EnemyHealth);}
            if(ShieldW>0.f){Box(3,FVector2f(BarLeft+1.f+HealthW,TrackTop),FVector2f(ShieldW,WorldBarHeight),LDHUDStyle::Shield);}
            // A broken shield bursts outward in its colour for a moment.
            if(Burst>=0.f)
            {
                const float Grow=4.f+10.f*Burst;
                Box(4,FVector2f(BarLeft-Grow*.5f,BarTop-Grow*.25f),FVector2f(BarWidth+2.f+Grow,BarRimHeight+Grow*.5f),LDHUDStyle::Shield.CopyWithNewOpacity(.7f*(1.f-Burst)));
            }
            // Ticks show how tanky the unit is: a thin half tick every 100 HP (when they fit), a full one every 1000 HP.
            if(Label.MaxHealth>0.f)
            {
                const float PerHundred=BarWidth*Squeeze*100.f/Label.MaxHealth;
                const FLinearColor Tick(0.f,0.f,0.f,.55f);
                for(int32 Hundred=1;Hundred*100.f<Label.MaxHealth;++Hundred)
                {
                    const bool bThousand=Hundred%10==0;
                    if(!bThousand&&PerHundred<3.f){continue;}
                    const float X=BarLeft+1.f+PerHundred*float(Hundred);
                    const float TickHeight=bThousand?WorldBarHeight:WorldBarHeight*.5f;
                    Box(4,FVector2f(X-(bThousand?.75f:.5f),TrackTop),FVector2f(bThousand?1.5f:1.f,TickHeight),Tick);
                }
            }
            if(Label.bContender&&Label.Level>0)
            {
                // The badge carries the identity tint: the name itself identifies, the colour is only an extra cue.
                const float BadgeLeft=BarLeft-WorldBadgeGap-BadgeRim,BadgeTop=Mid-BadgeRim*.5f;
                Box(1,FVector2f(BadgeLeft,BadgeTop),FVector2f(BadgeRim,BadgeRim),LDHUDStyle::Rim);
                Box(2,FVector2f(BadgeLeft+1.f,BadgeTop+1.f),FVector2f(BadgeSide,BadgeSide),LDHUD::PaletteColor(Label.Presentation));
                const FString LevelText=FString::FromInt(Label.Level);const FVector2f LevelExtent=FontMeasure->Measure(LevelText,LevelFont);
                const FVector2f LevelAt(BadgeLeft+BadgeRim*.5f-LevelExtent.X*.5f,Mid-LevelExtent.Y*.5f);
                FSlateDrawElement::MakeText(E,Base+4,G.ToPaintGeometry(LevelExtent,FSlateLayoutTransform(LevelAt)),LevelText,LevelFont,ESlateDrawEffect::None,FLinearColor::White);
            }
            Bottom-=Entry.Row+WorldRowGap;
        }
        if(!Label.Name.IsEmpty())
        {
            const FLinearColor Ink=Label.bContender?WorldLabelContenderInk:Label.CampTier>=0?LDHUDStyle::CampTierInk(uint8(Label.CampTier)):WorldLabelCampInk;
            Line(Label.Name,Label.bContender?ContenderFont:CampFont,Ink,CentreX,Bottom);
        }
        if(!Label.StatusParts.IsEmpty())
        {
            // Each status in the colour of its body marker (Combat's root, shield and protection tints); the text names it.
            const FString Separator=TEXT(" · ");const float SeparatorWidth=FontMeasure->Measure(Separator,StatusFont).X;
            float Width=0.f,Height=0.f;
            for(int32 Index=0;Index<Label.StatusParts.Num();++Index)
            {
                const FVector2f Extent=FontMeasure->Measure(Label.StatusParts[Index].Text,StatusFont);
                Width+=Extent.X+(Index>0?SeparatorWidth:0.f);Height=FMath::Max(Height,Extent.Y);
            }
            float X=RowLeft(CentreX,Width);const float Top=Bottom-Height;
            auto Draw=[&](const FString& Text,const FLinearColor& Ink)
            {
                const FVector2f Extent=FontMeasure->Measure(Text,StatusFont);
                FSlateDrawElement::MakeText(E,Base+4,G.ToPaintGeometry(Extent,FSlateLayoutTransform(FVector2f(X,Top))),Text,StatusFont,ESlateDrawEffect::None,Ink);
                X+=Extent.X;
            };
            for(int32 Index=0;Index<Label.StatusParts.Num();++Index)
            {
                if(Index>0){Draw(Separator,WorldLabelStatusInk);}
                Draw(Label.StatusParts[Index].Text,WorldStatusInk(Label.StatusParts[Index].StatusId));
            }
            Bottom=Top;
        }
    }
    return Base+4;
}
