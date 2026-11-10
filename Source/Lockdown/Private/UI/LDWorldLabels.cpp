#include "UI/LDWorldLabels.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDControlSettings.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDWorldViewRelay.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
// Anchor height above the presented body centre, clear of the head and its status markers. The proxy draws the
// high-tier camp body larger, so its label sits higher; the own body's bars sit at a contender label's height.
double WorldLabelHeadroomCm(const LDWorldLabels::FLabel& Label)
{return Label.bContender?120.:Label.CreatureId==uint8(ELDDemoMonsterId::MonsterDean)?175.
    :Label.CreatureId==uint8(ELDDemoMonsterId::MonsterAdvisor)?160.
    :Label.CreatureId==uint8(ELDDemoMonsterId::MonsterGuardian)?150.:120.;}
constexpr double OwnBarsHeadroomCm=120.;
// Anchors further off-screen are skipped; the margin keeps a label whose anchor sits just past an edge.
constexpr float WorldLabelCullMargin=160.f;
// Local units at the 1080p reference; every bar, badge and pip also has a one-unit rim.
constexpr float WorldBarHeight=6.f,WorldShieldLine=2.f,WorldBadgeSide=20.f,WorldBadgeGap=3.f,WorldRowGap=2.f;
constexpr float OwnBarWidth=84.f,OwnHealthHeight=8.f,OwnManaHeight=4.f;
// Contenders sit between the two camp tiers; the high-tier camp body is drawn larger, so its bar is wider.
float WorldBarWidth(const LDWorldLabels::FLabel& Label)
{return Label.bContender?72.f:Label.CreatureId==uint8(ELDDemoMonsterId::MonsterDean)?110.f
    :Label.CreatureId==uint8(ELDDemoMonsterId::MonsterAdvisor)?96.f
    :Label.CreatureId==uint8(ELDDemoMonsterId::MonsterGuardian)?84.f:64.f;}
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
    default: return WorldLabelStatusInk;
    }
}
// The UI's default CJK-capable font at the local text scale, outlined so a label reads over any ground or body.
FSlateFontInfo WorldLabelFont(const FName Typeface,int32 Size,float TextScale,int32 OutlineSize=2)
{
    FFontOutlineSettings Outline;Outline.OutlineSize=OutlineSize;Outline.OutlineColor=LDHUDStyle::Rim;
    return FCoreStyle::GetDefaultFontStyle(Typeface,float(FMath::Max(1,FMath::RoundToInt(Size*TextScale))),Outline);
}
}

LDWorldLabels::FLabel LDWorldLabels::Build(const FLDObservedUnitView& Unit,const FString& RosterName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec)
{
    FLabel Label;
    Label.bContender=Unit.Kind==ELDObservedUnitKind::Contender;
    if(Label.bContender){Label.Name=RosterName;Label.Presentation=Unit.Presentation;Label.Level=Unit.Level;}
    else{Label.Name=LDHUD::CreatureName(Content,Unit.CreatureId);Label.CreatureId=Unit.CreatureId;}
    // As on the target card (LDHUD::ResourceP2), health without a known maximum or with a malformed current is unknown:
    // no bar rather than an empty one.
    Label.bHealth=Unit.Health.MaxP2>0&&Unit.Health.CurrentP2>=0;
    if(Label.bHealth){Label.HealthFraction=float(FMath::Clamp(double(Unit.Health.CurrentP2)/double(Unit.Health.MaxP2),0.,1.));}
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
    Bars.bMana=Combat.MaxMPP2>0&&Combat.MPP2>=0;
    if(Bars.bMana){Bars.Mana=float(FMath::Clamp(double(Combat.MPP2)/double(Combat.MaxMPP2),0.,1.));}
    return Bars;
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
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());const UWorld* World=GetWorld();
    const ALDGameState* Public=World?World->GetGameState<ALDGameState>():nullptr;
    FSlateRenderer* Renderer=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetRenderer():nullptr;
    // Nothing is drawn before the display content was tried, so a first frame never flashes fallback ids.
    if(!bDisplayContentTried||!PC||!Public||!Renderer){return Base;}
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
    int32 ViewW=0,ViewH=0;PC->GetViewportSize(ViewW,ViewH);
    if((!Frame&&!IsValid(OwnBody))||ViewW<=0||ViewH<=0){return Base;}
    // Viewport pixels to local units: the layer fills the viewport, so this also removes the DPI scale.
    const FVector2f Size=G.GetLocalSize();const float ToLocalX=Size.X/float(ViewW),ToLocalY=Size.Y/float(ViewH);
    auto Culled=[&Size](float X,float Y){return X<-WorldLabelCullMargin||X>Size.X+WorldLabelCullMargin||Y<0.f||Y>Size.Y+WorldLabelCullMargin;};
    const float TextScale=PC->Controls?PC->Controls->TextScale:1.f;
    const FSlateFontInfo ContenderFont=WorldLabelFont(TEXT("Bold"),16,TextScale),CampFont=WorldLabelFont(TEXT("Regular"),16,TextScale),StatusFont=WorldLabelFont(TEXT("Regular"),14,TextScale);
    // The level badge grows with the text preference, so its number always fits.
    const FSlateFontInfo LevelFont=WorldLabelFont(TEXT("Bold"),13,TextScale,1);
    const float BadgeSide=FMath::RoundToFloat(WorldBadgeSide*TextScale),BadgeRim=BadgeSide+2.f;
    const TSharedRef<FSlateFontMeasure> FontMeasure=Renderer->GetFontMeasureService();
    const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const FLDDemoContentDefinitions* Content=DisplayContent?&DisplayContent->Get():nullptr;
    // Layers: rims, then tracks and badge tints, then fills, then text.
    auto Box=[&](int32 Layer,const FVector2f& At,const FVector2f& Extent,const FLinearColor& Tint)
    {FSlateDrawElement::MakeBox(E,Base+Layer,G.ToPaintGeometry(Extent,FSlateLayoutTransform(At)),White,ESlateDrawEffect::None,Tint);};
    // One centred line whose bottom edge rests on Bottom; Bottom then moves up to the line's top edge.
    auto Line=[&](const FString& Text,const FSlateFontInfo& Font,const FLinearColor& Ink,float CentreX,float& Bottom)
    {
        const FVector2f Extent=FontMeasure->Measure(Text,Font);const FVector2f At(CentreX-Extent.X*.5f,Bottom-Extent.Y);
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
    FVector2D OwnScreen;
    if(IsValid(OwnBody)&&PC->ProjectWorldLocationToScreen(OwnBody->PresentedLocation()+FVector(0,0,OwnBarsHeadroomCm),OwnScreen))
    {
        const float OwnX=float(OwnScreen.X)*ToLocalX,OwnBottom=float(OwnScreen.Y)*ToLocalY;
        if(!Culled(OwnX,OwnBottom))
        {
            // Bottom-up from the anchor: mana under health (the shield after its fill) in one rim.
            const float RimW=OwnBarWidth+2.f,RimH=OwnHealthHeight+2.f+(SelfBars.bMana?OwnManaHeight+1.f:0.f);
            const float RimLeft=OwnX-RimW*.5f,RimTop=OwnBottom-RimH,BarX=RimLeft+1.f,HealthTop=RimTop+1.f;
            Box(1,FVector2f(RimLeft,RimTop),FVector2f(RimW,RimH),LDHUDStyle::Rim);
            Box(2,FVector2f(BarX,HealthTop),FVector2f(OwnBarWidth,OwnHealthHeight),LDHUDStyle::BarTrack);
            if(SelfBars.Health>0.f){Box(3,FVector2f(BarX,HealthTop),FVector2f(OwnBarWidth*SelfBars.Health,OwnHealthHeight),LDHUDStyle::OwnHealth);}
            if(SelfBars.Shield>0.f){Box(3,FVector2f(BarX+OwnBarWidth*SelfBars.Health,HealthTop),FVector2f(OwnBarWidth*SelfBars.Shield,OwnHealthHeight),LDHUDStyle::Shield);}
            if(SelfBars.bMana)
            {
                const float ManaTop=HealthTop+OwnHealthHeight+1.f;
                Box(2,FVector2f(BarX,ManaTop),FVector2f(OwnBarWidth,OwnManaHeight),LDHUDStyle::BarTrack);
                if(SelfBars.Mana>0.f){Box(3,FVector2f(BarX,ManaTop),FVector2f(OwnBarWidth*SelfBars.Mana,OwnManaHeight),LDHUDStyle::Mana);}
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
            // Placed before any label, so a unit's label moves above the own bars instead of covering them.
            Placed.Add(FSlateRect(ReachLeft,ReachTop,ReachRight,OwnBottom));
        }
    }
    // Labels are measured first and placed lowest-on-screen first; a label that would overlap one already placed (the own
    // bars come first) moves up above it, so units standing together keep readable labels instead of a garbled stack.
    struct FPending
    {
        LDWorldLabels::FLabel Label;
        float CentreX=0.f,Bottom=0.f,LeftReach=0.f,RightReach=0.f,Height=0.f,Row=0.f;
    };
    TArray<FPending> Pending;
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
            const LDWorldLabels::FLabel& Label=Entry.Label;
            FVector2D Screen;
            if((Label.Name.IsEmpty()&&Label.Status.IsEmpty()&&!Label.bHealth)||!PC->ProjectWorldLocationToScreen(Centre+FVector(0,0,WorldLabelHeadroomCm(Label)),Screen)){continue;}
            Entry.CentreX=float(Screen.X)*ToLocalX;Entry.Bottom=float(Screen.Y)*ToLocalY;
            if(Culled(Entry.CentreX,Entry.Bottom)){continue;}
            // The bar row: the bar centred on the unit, a contender's level badge left of it and centred on it vertically.
            if(Label.bHealth)
            {
                const float BarRim=WorldBarWidth(Label)+2.f;const bool bBadge=Label.bContender&&Label.Level>0;
                Entry.Row=FMath::Max(WorldBarHeight+(Label.bShield?WorldShieldLine:0.f)+2.f,bBadge?BadgeRim:0.f);
                Entry.LeftReach=BarRim*.5f+(bBadge?WorldBadgeGap+BadgeRim:0.f);Entry.RightReach=BarRim*.5f;
            }
            float Lines=0.f;
            if(!Label.Name.IsEmpty())
            {
                const FVector2f Name=FontMeasure->Measure(Label.Name,Label.bContender?ContenderFont:CampFont);
                Entry.LeftReach=FMath::Max(Entry.LeftReach,Name.X*.5f);Entry.RightReach=FMath::Max(Entry.RightReach,Name.X*.5f);Lines+=Name.Y;
            }
            if(!Label.Status.IsEmpty())
            {
                const FVector2f Status=FontMeasure->Measure(Label.Status,StatusFont);
                Entry.LeftReach=FMath::Max(Entry.LeftReach,Status.X*.5f);Entry.RightReach=FMath::Max(Entry.RightReach,Status.X*.5f);Lines+=Status.Y;
            }
            Entry.Height=Entry.Row+(Entry.Row>0.f&&Lines>0.f?WorldRowGap:0.f)+Lines;
            Pending.Add(MoveTemp(Entry));
        }
    }
    Pending.Sort([](const FPending& A,const FPending& B){return A.Bottom>B.Bottom;});
    for(FPending& Entry:Pending)
    {
        float Bottom=Entry.Bottom;
        const float Left=Entry.CentreX-Entry.LeftReach,Right=Entry.CentreX+Entry.RightReach;
        for(bool bMoved=true;bMoved;)
        {
            bMoved=false;
            for(const FSlateRect& Other:Placed)
            {
                if(Left<Other.Right&&Right>Other.Left&&Bottom-Entry.Height<Other.Bottom&&Bottom>Other.Top){Bottom=Other.Top-2.f;bMoved=true;}
            }
        }
        Placed.Add(FSlateRect(Left,Bottom-Entry.Height,Right,Bottom));
        const LDWorldLabels::FLabel& Label=Entry.Label;
        const float CentreX=Entry.CentreX;
        if(Label.bHealth)
        {
            const float BarWidth=WorldBarWidth(Label),BarRimHeight=WorldBarHeight+(Label.bShield?WorldShieldLine:0.f)+2.f;
            const float Mid=Bottom-Entry.Row*.5f,BarLeft=CentreX-(BarWidth+2.f)*.5f,BarTop=Mid-BarRimHeight*.5f;
            Box(1,FVector2f(BarLeft,BarTop),FVector2f(BarWidth+2.f,BarRimHeight),LDHUDStyle::Rim);
            float TrackTop=BarTop+1.f;
            // The shield is a line across the top of the bar; the status line below names it too.
            if(Label.bShield){Box(3,FVector2f(BarLeft+1.f,TrackTop),FVector2f(BarWidth,WorldShieldLine),LDHUDStyle::Shield);TrackTop+=WorldShieldLine;}
            Box(2,FVector2f(BarLeft+1.f,TrackTop),FVector2f(BarWidth,WorldBarHeight),LDHUDStyle::BarTrack);
            if(Label.HealthFraction>0.f)
            {Box(3,FVector2f(BarLeft+1.f,TrackTop),FVector2f(BarWidth*Label.HealthFraction,WorldBarHeight),Label.bContender?LDHUDStyle::EnemyHealth:LDHUDStyle::CampHealth);}
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
        if(!Label.Name.IsEmpty()){Line(Label.Name,Label.bContender?ContenderFont:CampFont,Label.bContender?WorldLabelContenderInk:WorldLabelCampInk,CentreX,Bottom);}
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
            float X=CentreX-Width*.5f;const float Top=Bottom-Height;
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
