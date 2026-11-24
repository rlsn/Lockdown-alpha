#include "UI/LDWorldLabels.h"
#include "UI/LDWorkshop.h"
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
#include "Navigation/LDNavigationComponent.h"
#include "World/LDTrainingRoomDisplay.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "UI/LDUIFonts.h"
#include "UI/LDBroadcastPaint.h"
#include "UI/LDUIIcons.h"
#include "Engine/Texture2D.h"

namespace
{
// Anchor height above the presented body centre, clear of the head and its status markers. The proxy draws the
// high-tier camp body larger, so its label sits higher; the own body's bars sit at a contender label's height.
double WorldLabelHeadroomCm(const LDWorldLabels::FLabel& Label)
{return Label.bContender?120.:Label.CreatureId==uint8(ELDDemoMonsterId::MonsterDean)?175.
    :(Label.CreatureId==uint8(ELDDemoMonsterId::MonsterAdvisor)||Label.CreatureId>=uint8(ELDDemoMonsterId::MonsterAuditor))?160.
    :(Label.CreatureId==uint8(ELDDemoMonsterId::MonsterGuardian)||Label.CreatureId==uint8(ELDDemoMonsterId::MonsterAgent)||Label.CreatureId==uint8(ELDDemoMonsterId::MonsterGuard))?150.:120.;}
constexpr double OwnBarsHeadroomCm=120.;
// #328 §1.1: LoL-style overhead bar dimensions & palette at 1080p reference.
constexpr float WorldLabelCullMargin=180.f;
constexpr float ContenderBarWidth=108.f,ContenderBarHeight=12.f,ContenderMomentumHeight=4.f;
constexpr float CampLowHighWidth=74.f,CampLowHighHeight=8.f;
constexpr float CampEliteWidth=104.f,CampEliteHeight=10.f;
constexpr float CampBossWidth=156.f,CampBossHeight=14.f;
constexpr float WorldBadgeSide=22.f,WorldBadgeGap=4.f,WorldRowGap=2.f;
constexpr float WorldCCBarHeight=4.f,WorldElemDiscSide=16.f,WorldElemDiscGap=2.f;
constexpr float OwnStatusSide=22.f,OwnStatusGap=3.f;constexpr int32 OwnStatusPerRow=6;

const FSlateBrush* WorldDiscBrush(){static const FSlateRoundedBoxBrush Disc(FLinearColor::White);return &Disc;}

FVector2f WorldBarDimensions(const LDWorldLabels::FLabel& Label)
{
    if(Label.bContender){return FVector2f(ContenderBarWidth,ContenderBarHeight);}
    if(Label.CampTier==int8(ELDDemoCampTier::Boss)||Label.CreatureId==uint8(ELDDemoMonsterId::MonsterDean))
    {return FVector2f(CampBossWidth,CampBossHeight);}
    if(Label.CampTier==int8(ELDDemoCampTier::Elite)||Label.CampTier==int8(ELDDemoCampTier::Ancient)
        ||Label.CreatureId==uint8(ELDDemoMonsterId::MonsterAdvisor)||Label.CreatureId>=uint8(ELDDemoMonsterId::MonsterAuditor))
    {return FVector2f(CampEliteWidth,CampEliteHeight);}
    return FVector2f(CampLowHighWidth,CampLowHighHeight);
}

const FLinearColor WorldOuterRim=FLinearColor(FColor(0x0A,0x0C,0x10)).CopyWithNewOpacity(.95f);
const FLinearColor WorldMajorTickInk=FLinearColor(FColor(0x08,0x0A,0x0D));
// On air: the own bar is the ball orange (me), another contender's white, the current target's live red.
const FLinearColor WorldOwnHPFill=FLinearColor(FColor(0xFF,0x6B,0x1A));
const FLinearColor WorldOtherHPFill=FLinearColor(FColor(0xF2,0xF2,0xF2));
const FLinearColor WorldOwnHPColorblind=FLinearColor(FColor(0x38,0xBD,0xF8));
const FLinearColor WorldEnemyHPFill=FLinearColor(FColor(0xE0,0x31,0x31));
const FLinearColor WorldEnemyHPColorblind=FLinearColor(FColor(0xF9,0x73,0x16));
const FLinearColor WorldCampHPFill=FLinearColor(FColor(0xB8,0x3B,0x3B));
const FLinearColor WorldBossFrameInk=FLinearColor(FColor(0xD9,0x77,0x06));
const FLinearColor WorldMomentumFill=FLinearColor(FColor(0xF5,0x9E,0x0B));
const FLinearColor WorldMomentumOverdrive=FLinearColor(FColor(0xFA,0xCC,0x15));
const FLinearColor WorldShieldFill=FLinearColor(FColor(0xE2,0xE8,0xF0));
const FLinearColor WorldShieldRim=FLinearColor(FColor(0x7D,0xD3,0xFC));
const FLinearColor WorldHealFlashInk=FLinearColor(FColor(0x86,0xEF,0xAC));
const FLinearColor WorldCCBarFill=FLinearColor(FColor(0xF8,0xFA,0xFC));

bool WorldIsHardCC(uint8 StatusId,const TCHAR*& OutShortName)
{
    switch(static_cast<ELDDemoStatusId>(StatusId))
    {
    case ELDDemoStatusId::Stun:    OutShortName=TEXT("眩晕"); return true;
    case ELDDemoStatusId::Root:    OutShortName=TEXT("定身"); return true;
    case ELDDemoStatusId::Taunt:   OutShortName=TEXT("嘲讽"); return true;
    case ELDDemoStatusId::Silence: OutShortName=TEXT("沉默"); return true;
    case ELDDemoStatusId::Disarm:  OutShortName=TEXT("缴械"); return true;
    default: OutShortName=nullptr; return false;
    }
}

bool WorldIsElementDisc(uint8 StatusId,const TCHAR*& OutGlyph,FLinearColor& OutColor)
{
    switch(static_cast<ELDDemoStatusId>(StatusId))
    {
    case ELDDemoStatusId::Wet:         OutGlyph=TEXT("湿"); OutColor=FLinearColor(FColor(0x38,0xBD,0xF8)); return true;
    case ELDDemoStatusId::Electrified: OutGlyph=TEXT("电"); OutColor=FLinearColor(FColor(0xC0,0x84,0xFC)); return true;
    case ELDDemoStatusId::Burn:        OutGlyph=TEXT("烫"); OutColor=FLinearColor(FColor(0xFB,0x92,0x3C)); return true;
    case ELDDemoStatusId::Chilled:     OutGlyph=TEXT("寒"); OutColor=FLinearColor(FColor(0x67,0xE8,0xF9)); return true;
    case ELDDemoStatusId::Rotten:      OutGlyph=TEXT("馊"); OutColor=FLinearColor(FColor(0x84,0xCC,0x16)); return true;
    default: OutGlyph=nullptr; return false;
    }
}
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
    if((Unit.HitFlags&LDObservation::ShieldBroken)!=0&&Unit.RecentHitId.IsValid()){Label.BreakHitId=Unit.RecentHitId;}
    // The same parts as the target card's status line (LDHUD::ObservedCard), so both read alike.
    for(const FLDObservedStatus& Status:Unit.Statuses)
    {
        if(Status.StatusId==uint8(ELDDemoStatusId::Shield)){Label.bShield=true;}
        FString Part=LDHUD::StatusName(Content,Status.StatusId);
        if(Status.Stacks>1){Part+=FString::Printf(TEXT(" ×%d"),Status.Stacks);}
        if(Status.ExpiresAtServerMs>0){Part+=TEXT(" ")+LDHUD::TenthsUp(int64(FMath::RoundToDouble(double(Status.ExpiresAtServerMs)-SampledAtServerSec*1000.)));}
        Label.Status+=(Label.Status.IsEmpty()?TEXT(""):TEXT(" · "))+Part;
        LDWorldLabels::FLabel::FStatusPart& Shown=Label.StatusParts.Add_GetRef({MoveTemp(Part),Status.StatusId});
        Shown.IconId=LDHUD::StatusIconId(Content,Status.StatusId);
        Shown.Glyph=LDHUD::IconGlyph(LDHUD::StatusName(Content,Status.StatusId));
        Shown.bPositive=LDHUD::StatusIsPositive(Status.StatusId);
        Shown.Stacks=Status.Stacks;
        if(LDHUD::StatusIsHardControl(Status.StatusId)&&Status.ExpiresAtServerMs>0)
        {
            const int64 Left=int64(FMath::RoundToDouble(double(Status.ExpiresAtServerMs)-SampledAtServerSec*1000.));
            Label.ControlRemainingMs=FMath::Max(Label.ControlRemainingMs,Left);
        }
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

void LDWorldLabels::BuildHealthTicks(int64 MaxHPP2,int64 CurrentHPP2,int64 ShieldP2,float BarWidth,TArray<FBarTick>& OutTicks)
{
    OutTicks.Reset();
    if(MaxHPP2<=0||!(BarWidth>4.f)){return;}
    const int64 SpanP2=FMath::Max(MaxHPP2,FMath::Max<int64>(0,CurrentHPP2)+FMath::Max<int64>(0,ShieldP2));
    const double SpanHP=double(SpanP2)/100.;
    if(SpanHP<=100.){return;}
    // #328 §1.2: 100 HP minor ticks below 3500 HP span, switching to 250 HP at >= 3500 HP to avoid moire; 1000 HP major ticks.
    const int32 MinorStepHP=SpanHP>=3500.?250:100;
    for(int32 HP=MinorStepHP;double(HP)<SpanHP-.5;HP+=MinorStepHP)
    {
        const float X=float(double(HP)/SpanHP)*BarWidth;
        if(X<=1.f||X>=BarWidth-1.f){continue;}
        OutTicks.Add({X,(HP%1000)==0});
    }
}

void LDWorldLabels::AdvanceBarTrail(FBarTrailState& State,int64 CurrentHPP2,int64 MaxHPP2,int64 CurrentShieldP2,
    uint8 HitFlags,const FGuid& HitId,double NowSec)
{
    CurrentHPP2=FMath::Max<int64>(0,CurrentHPP2);
    CurrentShieldP2=FMath::Max<int64>(0,CurrentShieldP2);
    if(!State.bPrimed||MaxHPP2<=0||NowSec<State.LastUpdateSec)
    {
        State=FBarTrailState();
        State.bPrimed=MaxHPP2>0;
        State.PreviousHPP2=CurrentHPP2;
        State.TrailHPP2=double(CurrentHPP2);
        State.PreviousShieldP2=CurrentShieldP2;
        State.LastUpdateSec=NowSec;
        State.LastHitId=HitId;
        return;
    }
    const double DeltaSec=FMath::Clamp(NowSec-State.LastUpdateSec,0.,0.25);
    State.LastUpdateSec=NowSec;
    const bool bNewHit=HitId.IsValid()&&HitId!=State.LastHitId;
    if(bNewHit){State.LastHitId=HitId;}

    if(CurrentHPP2<State.PreviousHPP2)
    {
        const int64 DropP2=State.PreviousHPP2-CurrentHPP2;
        State.TrailHPP2=FMath::Max(State.TrailHPP2,double(State.PreviousHPP2));
        State.LastDamagedAtSec=NowSec;
        if(DropP2*100>=MaxHPP2*18||(HitFlags&LDObservation::Critical)!=0)
        {
            State.LastShakeAtSec=NowSec;
        }
    }
    else if(CurrentHPP2>State.PreviousHPP2)
    {
        if(double(CurrentHPP2)>State.TrailHPP2){State.TrailHPP2=double(CurrentHPP2);}
        State.HealFromHPP2=State.PreviousHPP2;
        State.HealToHPP2=CurrentHPP2;
        State.LastHealedAtSec=NowSec;
    }
    if(bNewHit&&(HitFlags&LDObservation::Critical)!=0)
    {
        State.LastShakeAtSec=NowSec;
    }
    if((State.PreviousShieldP2>0&&CurrentShieldP2==0&&CurrentHPP2<State.PreviousHPP2)
        ||(bNewHit&&(HitFlags&LDObservation::ShieldBroken)!=0))
    {
        State.LastShieldBreakAtSec=NowSec;
        const double Span=double(FMath::Max(MaxHPP2,CurrentHPP2+State.PreviousShieldP2));
        State.ShieldBreakFraction=Span>0.?float(FMath::Clamp(double(CurrentHPP2+.5*State.PreviousShieldP2)/Span,0.05,0.98)):.85f;
    }
    const double HeldUntil=State.LastDamagedAtSec+0.12;
    const double DecaySec=FMath::Clamp(FMath::Min(DeltaSec,NowSec-HeldUntil),0.,0.25);
    if(State.TrailHPP2>double(CurrentHPP2)&&DecaySec>0.)
    {
        State.TrailHPP2=FMath::Max(double(CurrentHPP2),State.TrailHPP2-2.4*double(MaxHPP2)*DecaySec);
    }
    else if(State.TrailHPP2<double(CurrentHPP2))
    {
        State.TrailHPP2=double(CurrentHPP2);
    }
    State.PreviousHPP2=CurrentHPP2;
    State.PreviousShieldP2=CurrentShieldP2;
}

bool LDWorldLabels::ShouldRefreshOcclusion(double LastCheckedSec,double NowSec,double IntervalSec)
{
    if(!FMath::IsFinite(LastCheckedSec)||!FMath::IsFinite(NowSec)){return false;}
    return LastCheckedSec<0.||NowSec<LastCheckedSec||(NowSec-LastCheckedSec)>=FMath::Max(0.01,IntervalSec);
}

bool LDWorldLabels::ComputeSilhouetteBounds(const FVector2f& HeadPx,const FVector2f& FootPx,float HalfWidthPx,FSlateRect& OutRect)
{
    if(!FMath::IsFinite(HeadPx.X)||!FMath::IsFinite(HeadPx.Y)||!FMath::IsFinite(FootPx.X)||!FMath::IsFinite(FootPx.Y)
        ||!FMath::IsFinite(HalfWidthPx)){return false;}
    const float Top=FMath::Min(HeadPx.Y,FootPx.Y);
    const float Bottom=FMath::Max(HeadPx.Y,FootPx.Y);
    const float Height=Bottom-Top;
    if(Height<6.f||Height>900.f){return false;}
    const float CentreX=(HeadPx.X+FootPx.X)*.5f;
    const float SafeHalfW=FMath::Clamp(HalfWidthPx,FMath::Max(6.f,Height*.18f),Height*.55f);
    OutRect=FSlateRect(CentreX-SafeHalfW,Top,CentreX+SafeHalfW,Bottom);
    return true;
}

bool LDWorldLabels::IsChestOccludedByStaticWorld(const UWorld& World,const FVector& CameraLocation,
    const FVector& PresentedFeet,const AActor* IgnoredBody)
{
    FCollisionQueryParams Params(SCENE_QUERY_STAT(LDWorldLabelsOcclusion),false);
    Params.AddIgnoredActor(World.GetDefaultBrush());
    if(IsValid(IgnoredBody)){Params.AddIgnoredActor(IgnoredBody);}
    const FVector Chest=PresentedFeet+FVector(0.,0.,90.);
    for(int32 Attempt=0;Attempt<16;++Attempt)
    {
        FHitResult Hit;
        if(!World.LineTraceSingleByChannel(Hit,CameraLocation,Chest,ECC_Visibility,Params)||!Hit.bBlockingHit)
        {
            return false;
        }
        const AActor* HitActor=Hit.GetActor();
        const UPrimitiveComponent* Comp=Hit.GetComponent();
        if(!Comp||Comp->Mobility==EComponentMobility::Movable||Cast<APawn>(HitActor))
        {
            if(Comp){Params.AddIgnoredComponent(Comp);}
            if(HitActor&&Cast<APawn>(HitActor)){Params.AddIgnoredActor(HitActor);}
            else if(!Comp&&HitActor){Params.AddIgnoredActor(HitActor);}
            if(!Comp&&!HitActor){return false;}
            continue;
        }
        return true;
    }
    return false;
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
    UWorld* World=GetWorld();
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    const ALDGameState* Public=World&&!World->bIsTearingDown?World->GetGameState<ALDGameState>():nullptr;
    FSlateRenderer* Renderer=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetRenderer():nullptr;
    if(!bDisplayContentTried||!PC||!Public||!Renderer||(PC->HUDUI&&PC->HUDUI->IsFrozenForTeardown()))
    {
        UnitTrails.Reset();
        UnitOcclusions.Reset();
        return Base;
    }
    if(PC->HUDUI){PC->HUDUI->PaintWorldPreviews(G,E,Base+1);}
    const double UiNow=FPlatformTime::Seconds();
    const FLDOwnerState& Own=PC->OwnerState;
    const ALDWorldViewRelay* Relay=Own.LifePhase==ELDLifePhase::Alive?PC->LocalView():nullptr;
    const FLDObservedFrameRead Read=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
    const FLDWorldView* Frame=Read.Frame&&Read.Status==ELDObservedViewStatus::Current&&Read.Frame->MatchGeneration==Own.Match&&Read.Frame->ObserverLife==Own.Life?Read.Frame:nullptr;
    const LDWorldLabels::FOwnBars SelfBars=LDWorldLabels::OwnBars(Own,Public->PublicMatch.Generation,PC->CombatView);
    const ALDCharacter* OwnBody=SelfBars.bVisible?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    const FString OwnNotice=PC->HUDUI?PC->HUDUI->GetOwnStatusNotice():FString();
    const ALDCharacter* NoticeBody=!OwnBody&&!OwnNotice.IsEmpty()&&Own.LifePhase==ELDLifePhase::Alive?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    int32 ViewW=0,ViewH=0;PC->GetViewportSize(ViewW,ViewH);
    if((!Frame&&!IsValid(OwnBody)&&!IsValid(NoticeBody))||ViewW<=0||ViewH<=0)
    {
        UnitTrails.Reset();
        UnitOcclusions.Reset();
        if(PC->HUDUI){PC->HUDUI->ReportOwnStatusRects({});}
        return Base+1;
    }
    const FVector2f Size=G.GetLocalSize();const float ToLocalX=Size.X/float(ViewW),ToLocalY=Size.Y/float(ViewH);
    auto Culled=[&Size](float X,float Y){return X<-WorldLabelCullMargin||X>Size.X+WorldLabelCullMargin||Y<0.f||Y>Size.Y+WorldLabelCullMargin;};
    const float TextScale=PC->Controls?PC->Controls->TextScale:1.f;
    const bool bColorblind=PC->Controls&&PC->Controls->bColorblindMode;
    const FSlateFontInfo ContenderFont=WorldLabelFont(TEXT("Bold"),LDHUDStyle::TypeText,TextScale),CampFont=WorldLabelFont(TEXT("Regular"),LDHUDStyle::TypeText,TextScale),StatusFont=WorldLabelFont(TEXT("Regular"),LDHUDStyle::TypeSmall,TextScale);
    const FSlateFontInfo LevelFont=WorldLabelFont(TEXT("Bold"),LDHUDStyle::TypeCaption,TextScale,1);
    const FSlateFontInfo TinyBoldFont=WorldLabelFont(TEXT("Bold"),LDHUDStyle::TypeTiny,TextScale,1);
    const float BadgeSide=FMath::RoundToFloat(WorldBadgeSide*TextScale),BadgeRim=BadgeSide+3.f;
    const TSharedRef<FSlateFontMeasure> FontMeasure=Renderer->GetFontMeasureService();
    const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const FLDDemoContentDefinitions* Content=DisplayContent?&DisplayContent->Get():nullptr;

    auto Box=[&](int32 Layer,const FVector2f& At,const FVector2f& Extent,const FLinearColor& Tint)
    {FSlateDrawElement::MakeBox(E,Base+Layer,G.ToPaintGeometry(Extent,FSlateLayoutTransform(At)),White,ESlateDrawEffect::None,Tint);};
    auto Disc=[&](int32 Layer,const FVector2f& At,float Diameter,const FLinearColor& Tint)
    {FSlateDrawElement::MakeBox(E,Base+Layer,G.ToPaintGeometry(FVector2f(Diameter,Diameter),FSlateLayoutTransform(At-FVector2f(Diameter*.5f,Diameter*.5f))),WorldDiscBrush(),ESlateDrawEffect::None,Tint);};
    auto Diamond=[&](int32 Layer,const FVector2f& At,float Edge,const FLinearColor& Tint)
    {FSlateDrawElement::MakeRotatedBox(E,Base+Layer,G.ToPaintGeometry(FVector2f(Edge,Edge),FSlateLayoutTransform(At-FVector2f(Edge*.5f,Edge*.5f))),White,ESlateDrawEffect::None,UE_PI*.25f,TOptional<FVector2f>(),FSlateDrawElement::RelativeToElement,Tint);};
    auto DrawSegmentedBar=[&](float BarLeft,float BarTop,float BarW,float BarH,int64 CurrentHPP2,int64 MaxHPP2,int64 ShieldP2,
        const FLinearColor& HPFill,const LDWorldLabels::FBarTrailState& Trail,bool bHasMomentum,float MomentumFrac,bool bMomentumOverdrive,
        const FLinearColor* AccentFrame)
    {
        const float ShakeAge=float(UiNow-Trail.LastShakeAtSec);
        const float ShakeOffset=(ShakeAge>=0.f&&ShakeAge<.14f)?3.5f*(1.f-ShakeAge/.14f)*FMath::Sin(ShakeAge*95.f):0.f;
        const bool bRimFlash=ShakeAge>=0.f&&ShakeAge<.09f;
        const float X0=BarLeft+ShakeOffset;
        const float TotalInnerH=BarH+(bHasMomentum?ContenderMomentumHeight+1.f:0.f);
        if(AccentFrame)
        {
            Box(1,FVector2f(X0-2.5f,BarTop-2.5f),FVector2f(BarW+5.f,TotalInnerH+5.f),*AccentFrame);
        }
        Box(1,FVector2f(X0-1.5f,BarTop-1.5f),FVector2f(BarW+3.f,TotalInnerH+3.f),bRimFlash?FLinearColor::White:WorldOuterRim);
        Box(2,FVector2f(X0,BarTop),FVector2f(BarW,BarH),LDHUDStyle::BarTrack);

        const int64 SafeCur=FMath::Max<int64>(0,CurrentHPP2);
        const int64 SafeShield=FMath::Max<int64>(0,ShieldP2);
        const int64 SpanP2=FMath::Max<int64>(FMath::Max<int64>(1,MaxHPP2),SafeCur+SafeShield);
        const float HPFrac=FMath::Clamp(float(double(SafeCur)/double(SpanP2)),0.f,1.f);
        const float TrailFrac=FMath::Clamp(float(Trail.TrailHPP2/double(SpanP2)),HPFrac,1.f);
        const float ShieldFrac=FMath::Clamp(float(double(SafeShield)/double(SpanP2)),0.f,1.f-HPFrac);

        if(TrailFrac>HPFrac+.002f)
        {
            Box(3,FVector2f(X0+BarW*HPFrac,BarTop),FVector2f(BarW*(TrailFrac-HPFrac),BarH),FLinearColor::White);
        }
        if(HPFrac>0.f)
        {
            Box(3,FVector2f(X0,BarTop),FVector2f(BarW*HPFrac,BarH),HPFill);
        }
        const float HealAge=float(UiNow-Trail.LastHealedAtSec);
        if(HealAge>=0.f&&HealAge<.18f&&Trail.HealToHPP2>Trail.HealFromHPP2)
        {
            const float H0=FMath::Clamp(float(double(Trail.HealFromHPP2)/double(SpanP2)),0.f,HPFrac);
            const float H1=FMath::Clamp(float(double(Trail.HealToHPP2)/double(SpanP2)),H0,HPFrac);
            if(H1>H0+.002f)
            {
                Box(4,FVector2f(X0+BarW*H0,BarTop),FVector2f(BarW*(H1-H0),BarH),WorldHealFlashInk.CopyWithNewOpacity(1.f-HealAge/.18f));
            }
        }
        if(ShieldFrac>.002f)
        {
            const float SX=X0+BarW*HPFrac,SW=BarW*ShieldFrac;
            Box(3,FVector2f(SX,BarTop),FVector2f(SW,BarH),WorldShieldRim);
            if(SW>2.f&&BarH>2.f)
            {
                Box(3,FVector2f(SX+1.f,BarTop+1.f),FVector2f(SW-2.f,BarH-2.f),WorldShieldFill);
            }
        }
        TArray<LDWorldLabels::FBarTick> Ticks;
        LDWorldLabels::BuildHealthTicks(MaxHPP2,SafeCur,SafeShield,BarW,Ticks);
        for(const LDWorldLabels::FBarTick& Tick:Ticks)
        {
            if(Tick.bMajor)
            {
                Box(4,FVector2f(X0+Tick.X-1.5f,BarTop),FVector2f(.5f,BarH),FLinearColor(1.f,1.f,1.f,.22f));
                Box(4,FVector2f(X0+Tick.X-1.f,BarTop),FVector2f(2.f,BarH),WorldMajorTickInk);
                Box(4,FVector2f(X0+Tick.X+1.f,BarTop),FVector2f(.5f,BarH),FLinearColor(1.f,1.f,1.f,.22f));
            }
            else
            {
                Box(4,FVector2f(X0+Tick.X-.5f,BarTop),FVector2f(1.f,BarH*.5f),FLinearColor(0.f,0.f,0.f,.55f));
            }
        }
        const float BreakAge=float(UiNow-Trail.LastShieldBreakAtSec);
        if(BreakAge>=0.f&&BreakAge<.24f)
        {
            const float P=BreakAge/.24f;
            const FVector2f Origin(X0+BarW*FMath::Clamp(Trail.ShieldBreakFraction,.1f,.95f),BarTop+BarH*.5f);
            for(int32 Shard=0;Shard<6;++Shard)
            {
                const float Ang=float(Shard)*(UE_TWO_PI/6.f)+.35f;
                const FVector2f Offset(FMath::Cos(Ang)*(4.f+18.f*P),FMath::Sin(Ang)*(3.f+14.f*P));
                Diamond(5,Origin+Offset,4.8f*(1.f-.55f*P),WorldShieldRim.CopyWithNewOpacity(1.f-P));
            }
        }
        if(bHasMomentum)
        {
            const float MomTop=BarTop+BarH+1.f;
            Box(2,FVector2f(X0,MomTop),FVector2f(BarW,ContenderMomentumHeight),LDHUDStyle::BarTrack);
            if(MomentumFrac>0.f)
            {
                Box(3,FVector2f(X0,MomTop),FVector2f(BarW*FMath::Clamp(MomentumFrac,0.f,1.f),ContenderMomentumHeight),
                    bMomentumOverdrive?WorldMomentumOverdrive:WorldMomentumFill);
            }
        }
    };

    auto RowLeft=[&Size](float CentreX,float Width)
    {
        constexpr float Edge=4.f;
        return Width+Edge*2.f>=Size.X?Edge:FMath::Clamp(CentreX-Width*.5f,Edge,Size.X-Width-Edge);
    };
    auto Line=[&](const FString& Text,const FSlateFontInfo& Font,const FLinearColor& Ink,float CentreX,float& Bottom)
    {
        const FVector2f Extent=FontMeasure->Measure(Text,Font);const FVector2f At(RowLeft(CentreX,Extent.X),Bottom-Extent.Y);
        FSlateDrawElement::MakeText(E,Base+5,G.ToPaintGeometry(Extent,FSlateLayoutTransform(At)),Text,Font,ESlateDrawEffect::None,Ink);
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
            const auto* Display=Content->FindDisplay(Id);
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
                WorldLabelFont(TEXT("Bold"),Speech ? 20 : LDHUDStyle::TypeText,TextScale),
                FLinearColor(1.f,.72f,.28f),X,Bottom);
        }
    }
    TArray<FSlateRect> Placed;
    TArray<FSlateRect> OwnMarkRects;
    const TArray<FSlateRect> HudReserved=PC->HUDUI?PC->HUDUI->WorldLabelReservedAreas(G):TArray<FSlateRect>();
    int64 NowServerMs=0;
    const bool bHasServerMs=PC->ReadDemoAuthorityTimeMs(NowServerMs);
    const double FrameServerSec=Frame?Frame->SampledAtServerSec:(bHasServerMs?double(NowServerMs)/1000.:0.);

    const FLinearColor OwnSilhouetteTint=FLinearColor(FColor(0x7D,0xD3,0xFC));
    const FLinearColor EnemySilhouetteTint=FLinearColor(FColor(0xF8,0x71,0x71));
    const FLinearColor HoverHighlightTint=FLinearColor(FColor(0xFF,0x4D,0x4F)).CopyWithNewOpacity(.92f);
    const bool bHasCamera=PC->PlayerCameraManager!=nullptr;
    const FVector CamLocation=bHasCamera?PC->PlayerCameraManager->GetCameraLocation():FVector::ZeroVector;
    const FVector CamRight=bHasCamera?FRotationMatrix(PC->PlayerCameraManager->GetCameraRotation()).GetScaledAxis(EAxis::Y):FVector(1.,0.,0.);
    auto SampleChestOccluded=[&](LDWorldLabels::FOcclusionSample& Sample,const FVector& PresentedFeet)->bool
    {
        if(!bHasCamera){return false;}
        if(LDWorldLabels::ShouldRefreshOcclusion(Sample.LastCheckedSec,UiNow))
        {
            Sample.LastCheckedSec=UiNow;
            Sample.bOccluded=LDWorldLabels::IsChestOccludedByStaticWorld(*World,CamLocation,PresentedFeet,OwnBody);
        }
        return Sample.bOccluded;
    };
    auto ProjectUnitCapsule=[&](const FVector& PresentedFeet,double HeightCm,double RadiusCm,FSlateRect& OutRect)->bool
    {
        FVector2D FootScreen,HeadScreen,SideScreen;
        if(!PC->ProjectWorldLocationToScreen(PresentedFeet,FootScreen)
            ||!PC->ProjectWorldLocationToScreen(PresentedFeet+FVector(0.,0.,HeightCm),HeadScreen)){return false;}
        const FVector2f FootPx(float(FootScreen.X)*ToLocalX,float(FootScreen.Y)*ToLocalY);
        const FVector2f HeadPx(float(HeadScreen.X)*ToLocalX,float(HeadScreen.Y)*ToLocalY);
        float HalfWidthPx=(FootPx.Y-HeadPx.Y)*.26f;
        if(PC->ProjectWorldLocationToScreen(PresentedFeet+FVector(0.,0.,HeightCm*.5)+CamRight*RadiusCm,SideScreen))
        {
            HalfWidthPx=FMath::Abs(float(SideScreen.X)*ToLocalX-(FootPx.X+HeadPx.X)*.5f);
        }
        if(!LDWorldLabels::ComputeSilhouetteBounds(HeadPx,FootPx,HalfWidthPx,OutRect)){return false;}
        return !Culled((OutRect.Left+OutRect.Right)*.5f,OutRect.Bottom);
    };
    auto DrawCapsuleSilhouette=[&](const FSlateRect& Bounds,const FLinearColor& BaseTint)
    {
        const float W=Bounds.Right-Bounds.Left,H=Bounds.Bottom-Bounds.Top;
        const float Cx=(Bounds.Left+Bounds.Right)*.5f;
        const FLinearColor Fill=BaseTint.CopyWithNewOpacity(.25f);
        const FLinearColor Rim=BaseTint.CopyWithNewOpacity(.42f);
        const float HeadDiam=FMath::Clamp(W*.62f,8.f,H*.30f);
        Disc(1,FVector2f(Cx,Bounds.Top+HeadDiam*.5f),HeadDiam,Fill);
        const float TorsoTop=Bounds.Top+HeadDiam*.88f;
        const float TorsoH=FMath::Max(4.f,Bounds.Bottom-TorsoTop);
        const float R=FMath::Min(W*.5f,TorsoH*.45f);
        TArray<FVector2f> Poly;Poly.Reserve(26);
        for(int32 Step=0;Step<=12;++Step)
        {
            const float Ang=UE_PI+float(Step)*(UE_PI/12.f);
            Poly.Add(FVector2f(Cx+FMath::Cos(Ang)*R,TorsoTop+R+FMath::Sin(Ang)*R));
        }
        for(int32 Step=0;Step<=12;++Step)
        {
            const float Ang=float(Step)*(UE_PI/12.f);
            Poly.Add(FVector2f(Cx+FMath::Cos(Ang)*(R*.82f),Bounds.Bottom-R*.82f+FMath::Sin(Ang)*(R*.82f)));
        }
        const int32 TopRow=FMath::FloorToInt(TorsoTop),BotRow=FMath::CeilToInt(Bounds.Bottom);
        for(int32 Row=TopRow;Row<BotRow;++Row)
        {
            const float Mid=float(Row)+.5f;
            float MinX=1e6f,MaxX=-1e6f;int32 Crossings=0;
            for(int32 I=0,J=Poly.Num()-1;I<Poly.Num();J=I++)
            {
                if((Poly[J].Y>Mid)!=(Poly[I].Y>Mid))
                {
                    const float X=Poly[J].X+(Mid-Poly[J].Y)*(Poly[I].X-Poly[J].X)/(Poly[I].Y-Poly[J].Y);
                    MinX=FMath::Min(MinX,X);MaxX=FMath::Max(MaxX,X);++Crossings;
                }
            }
            if(Crossings>=2&&MaxX>MinX){Box(1,FVector2f(MinX,float(Row)),FVector2f(MaxX-MinX,1.f),Fill);}
        }
        const FVector2f First=Poly[0];Poly.Add(First);
        FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),Poly,ESlateDrawEffect::None,Rim,true,1.5f);
    };
    auto DrawHoverOutline=[&](const FSlateRect& Bounds)
    {
        const FSlateRect Pad(Bounds.Left-3.f,Bounds.Top-3.f,Bounds.Right+3.f,Bounds.Bottom+3.f);
        const float W=Pad.Right-Pad.Left,H=Pad.Bottom-Pad.Top;
        const float Arm=FMath::Clamp(FMath::Min(W,H)*.28f,6.f,16.f);
        const auto CornerBracket=[&](const FVector2f& Corner,float SX,float SY)
        {
            FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),
                {Corner+FVector2f(SX*Arm,0.f),Corner,Corner+FVector2f(0.f,SY*Arm)},
                ESlateDrawEffect::None,HoverHighlightTint,true,1.5f);
        };
        CornerBracket(FVector2f(Pad.Left,Pad.Top),1.f,1.f);
        CornerBracket(FVector2f(Pad.Right,Pad.Top),-1.f,1.f);
        CornerBracket(FVector2f(Pad.Right,Pad.Bottom),-1.f,-1.f);
        CornerBracket(FVector2f(Pad.Left,Pad.Bottom),1.f,-1.f);
    };
    FGuid HoveredHandle;uint32 HoveredEpoch=0;
    const bool bHasHovered=PC->Navigation&&PC->Navigation->ReadHovered(HoveredHandle,HoveredEpoch);
    // On air, a contender's overhead label is its seat plate and bar; its name shows only while it is the current target or
    // under the pointer.
    FLDObservedUnitView Inspected;
    const bool bHasInspected=PC->ReadInspected(Inspected);
    // The seat plate left of a bar (seat ink, jersey number), as tall as the bar block plus a little; returns its left edge.
    auto DrawSeatPlate=[&](float BarLeft,float BarTop,float BarH,int32 Seat)
    {
        const float H=FMath::RoundToFloat(FMath::Max(BarH+8.f,18.f*TextScale)),W=FMath::RoundToFloat(H*1.15f);
        const float Right=BarLeft-3.f;
        const FBox2D Plate(FVector2D(Right-W,BarTop+BarH*.5f-H*.5f),FVector2D(Right,BarTop+BarH*.5f+H*.5f));
        LDBroadcastPaint::SeatPlate(LDUIPaint::FContext(E,Base+2,G),Base+2,Plate,Seat,1.f,H*.9f);
        return float(Plate.Min.X)-LDBroadcastPaint::Lean(H)*.5f;
    };

    // Own body segmented LoL bar (108x12 + 108x4 momentum + 22x22 hex level badge).
    if(OwnTrailMatch!=Own.Match||OwnTrailLife!=Own.Life)
    {
        OwnTrail=LDWorldLabels::FBarTrailState();
        OwnOcclusion=LDWorldLabels::FOcclusionSample();
        OwnTrailMatch=Own.Match;
        OwnTrailLife=Own.Life;
    }
    if(IsValid(OwnBody))
    {
        const FVector OwnFeet=OwnBody->PresentedLocation()-FVector(0.,0.,88.);
        FSlateRect OwnBounds;
        if(SampleChestOccluded(OwnOcclusion,OwnFeet)&&ProjectUnitCapsule(OwnFeet,176.,38.,OwnBounds))
        {
            DrawCapsuleSilhouette(OwnBounds,OwnSilhouetteTint);
        }
    }
    FVector2D OwnScreen;
    // The open build ring grows around the own body: its overhead plate steps aside so the element ring stays readable.
    const bool bBuildRingOpen=PC->HUDUI&&PC->HUDUI->Workshop&&PC->HUDUI->Workshop->IsOpen();
    if(!bBuildRingOpen&&IsValid(OwnBody)&&PC->ProjectWorldLocationToScreen(OwnBody->PresentedLocation()+FVector(0,0,OwnBarsHeadroomCm),OwnScreen))
    {
        const float OwnX=float(OwnScreen.X)*ToLocalX,OwnBottom=float(OwnScreen.Y)*ToLocalY;
        if(!Culled(OwnX,OwnBottom))
        {
            const FLDCombatOwnerView& Combat=PC->CombatView;
            LDWorldLabels::AdvanceBarTrail(OwnTrail,Combat.HPP2,Combat.MaxHPP2,Combat.ShieldP2,0,FGuid(),UiNow);
            const float TotalInnerH=ContenderBarHeight+(SelfBars.bMomentum?ContenderMomentumHeight+1.f:0.f);
            const float BarLeft=OwnX-ContenderBarWidth*.5f,BarTop=OwnBottom-TotalInnerH-1.5f;
            const int64 OverdriveThresh=Combat.OverdriveThresholdP2>0?Combat.OverdriveThresholdP2
                :(Content?Content->Parameters.Overdrive.ThresholdP2:6000);
            const bool bOwnOverdrive=SelfBars.bMomentum&&Combat.MomentumP2>=OverdriveThresh;
            // On air: the own bar is the ball orange (colour blind: the own blue), behind the own seat plate; no name.
            DrawSegmentedBar(BarLeft,BarTop,ContenderBarWidth,ContenderBarHeight,Combat.HPP2,Combat.MaxHPP2,Combat.ShieldP2,
                bColorblind?WorldOwnHPColorblind:WorldOwnHPFill,OwnTrail,SelfBars.bMomentum,SelfBars.Momentum,bOwnOverdrive,nullptr);

            const ALDCharacter* OwnCharacter=Cast<ALDCharacter>(OwnBody);
            float ReachLeft=BarLeft-2.f,ReachRight=BarLeft+ContenderBarWidth+2.f;
            float ReachTop=BarTop-2.f;
            if(OwnCharacter)
            {
                int32 Seat=LDHUD::SeatOf(Public->DemoLobby,Own.ContenderId);
                if(Seat==INDEX_NONE){Seat=OwnCharacter->PresentationKey;}
                const float PlateLeft=DrawSeatPlate(BarLeft,BarTop,TotalInnerH,Seat);
                ReachLeft=FMath::Min(ReachLeft,PlateLeft);
                ReachTop=FMath::Min(ReachTop,BarTop-3.f);
            }
            float BestCCFrac=0.f;FString BestCCName;
            TArray<TPair<FString,FLinearColor>,TInlineAllocator<4>> OwnElems;
            for(const FLDCombatStatusView& St:Combat.Statuses)
            {
                const TCHAR* CCName=nullptr;
                if(WorldIsHardCC(St.StatusId,CCName)&&bHasServerMs&&St.ExpiresAtMs>NowServerMs)
                {
                    const float RemSec=float(St.ExpiresAtMs-NowServerMs)/1000.f;
                    const float Frac=FMath::Clamp(RemSec/2.5f,.08f,1.f);
                    if(Frac>BestCCFrac){BestCCFrac=Frac;BestCCName=CCName;}
                }
                const TCHAR* ElemGlyph=nullptr;FLinearColor ElemCol;
                if(OwnElems.Num()<4&&WorldIsElementDisc(St.StatusId,ElemGlyph,ElemCol))
                {
                    OwnElems.Add({FString(ElemGlyph),ElemCol});
                }
            }
            if(!OwnElems.IsEmpty())
            {
                float ElemRight=BarLeft+ContenderBarWidth;
                const float ElemCy=ReachTop-WorldElemDiscSide*.5f-2.f;
                for(int32 EI=OwnElems.Num()-1;EI>=0;--EI)
                {
                    const FVector2f CPos(ElemRight-WorldElemDiscSide*.5f,ElemCy);
                    Disc(2,CPos,WorldElemDiscSide+2.f,WorldOuterRim);
                    Disc(3,CPos,WorldElemDiscSide,OwnElems[EI].Value);
                    const FVector2f GE=FontMeasure->Measure(OwnElems[EI].Key,TinyBoldFont);
                    FSlateDrawElement::MakeText(E,Base+5,G.ToPaintGeometry(GE,FSlateLayoutTransform(CPos-GE*.5f)),OwnElems[EI].Key,TinyBoldFont,ESlateDrawEffect::None,WorldOuterRim);
                    ElemRight-=WorldElemDiscSide+WorldElemDiscGap;
                }
                ReachTop=FMath::Min(ReachTop,ElemCy-WorldElemDiscSide*.5f-2.f);
            }
            if(BestCCFrac>0.f)
            {
                const float CCTop=ReachTop-WorldCCBarHeight-2.f;
                Box(2,FVector2f(BarLeft-1.f,CCTop-1.f),FVector2f(ContenderBarWidth+2.f,WorldCCBarHeight+2.f),WorldOuterRim);
                Box(3,FVector2f(BarLeft,CCTop),FVector2f(ContenderBarWidth*BestCCFrac,WorldCCBarHeight),WorldCCBarFill);
                float CCNameBottom=CCTop-1.f;
                const FVector2f CCE=FontMeasure->Measure(BestCCName,TinyBoldFont);
                FSlateDrawElement::MakeText(E,Base+5,G.ToPaintGeometry(CCE,FSlateLayoutTransform(FVector2f(BarLeft,CCNameBottom-CCE.Y))),BestCCName,TinyBoldFont,ESlateDrawEffect::None,WorldCCBarFill);
                ReachTop=CCNameBottom-CCE.Y;
            }
            const TArray<FLDOwnStatusMark>* Marks=PC->HUDUI?&PC->HUDUI->GetOwnStatusMarks():nullptr;
            if(Marks&&!Marks->IsEmpty())
            {
                const float Side=FMath::RoundToFloat(OwnStatusSide*TextScale);
                const TArray<FSlateRect> Squares=LDWorldLabels::OwnStatusIconRects(Marks->Num(),OwnX,ReachTop-WorldRowGap-1.f,Side,OwnStatusGap,OwnStatusPerRow);
                for(int32 Index=0;Index<Squares.Num();++Index)
                {
                    const FLDOwnStatusMark& Mark=(*Marks)[Index];const FSlateRect& Square=Squares[Index];
                    const FLinearColor Edge=Mark.bPositive?(bColorblind?FLinearColor(.25f,.68f,1.f):LDHUDStyle::StatusPositive)
                        :(bColorblind?FLinearColor(1.f,.52f,.10f):LDHUDStyle::StatusNegative);
                    const FVector2f At(Square.Left,Square.Top);
                    Box(1,At-FVector2f(1.f,1.f),FVector2f(Side+2.f,Side+2.f),Edge);
                    Box(2,At,FVector2f(Side,Side),FLinearColor(.02f,.03f,.05f,.92f));
                    if(const FSlateBrush* Picture=PC->HUDUI->OwnStatusBrush(Mark.ViewIndex))
                    {FSlateDrawElement::MakeBox(E,Base+3,G.ToPaintGeometry(FVector2f(Side,Side),FSlateLayoutTransform(At)),Picture,ESlateDrawEffect::None,FLinearColor::White);}
                    else
                    {
                        const FVector2f GlyphExtent=FontMeasure->Measure(Mark.Glyph,LevelFont);
                        const FVector2f GlyphAt(At.X+Side*.5f-GlyphExtent.X*.5f,At.Y+Side*.5f-GlyphExtent.Y*.5f);
                        FSlateDrawElement::MakeText(E,Base+5,G.ToPaintGeometry(GlyphExtent,FSlateLayoutTransform(GlyphAt)),Mark.Glyph,LevelFont,ESlateDrawEffect::None,Edge);
                    }
                    if(Mark.Stacks>1)
                    {
                        const FString StackText=FString::FromInt(Mark.Stacks);const FVector2f StackExtent=FontMeasure->Measure(StackText,TinyBoldFont);
                        const FVector2f StackAt(At.X+Side-StackExtent.X+1.f,At.Y+Side-StackExtent.Y+1.f);
                        FSlateDrawElement::MakeText(E,Base+5,G.ToPaintGeometry(StackExtent,FSlateLayoutTransform(StackAt)),StackText,TinyBoldFont,ESlateDrawEffect::None,FLinearColor::White);
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
                const float NoticeHalf=FontMeasure->Measure(OwnNotice,StatusFont).X*.5f;
                float NoticeBottom=ReachTop-WorldRowGap;
                Line(OwnNotice,StatusFont,LDHUDStyle::TextSecondary,OwnX,NoticeBottom);
                ReachTop=NoticeBottom;ReachLeft=FMath::Min(ReachLeft,OwnX-NoticeHalf);ReachRight=FMath::Max(ReachRight,OwnX+NoticeHalf);
            }
            Placed.Add(FSlateRect(ReachLeft,ReachTop,ReachRight,OwnBottom));
        }
    }
    else if(IsValid(NoticeBody)&&PC->ProjectWorldLocationToScreen(NoticeBody->PresentedLocation()+FVector(0,0,OwnBarsHeadroomCm),OwnScreen))
    {
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

    // Prune unit damage-trail and occlusion caches strictly to handles visible in this exact frame (no cross-visibility memory).
    TSet<FGuid> SeenHandles;
    if(Frame){for(const FLDObservedUnitView& Unit:Frame->Units){SeenHandles.Add(Unit.Handle);}}
    for(auto It=UnitTrails.CreateIterator();It;++It){if(!SeenHandles.Contains(It.Key())){It.RemoveCurrent();}}
    for(auto It=UnitOcclusions.CreateIterator();It;++It){if(!SeenHandles.Contains(It.Key())){It.RemoveCurrent();}}

    struct FPending
    {
        LDWorldLabels::FLabel Label;
        int64 CurrentHPP2=0,MaxHPP2=0,ShieldP2=0;
        bool bHasMomentum=false,bMomentumOverdrive=false;
        float MomentumFrac=0.f;
        float CCFrac=0.f;
        FString CCName;
        TArray<TPair<FString,FLinearColor>,TInlineAllocator<4>> ElemDiscs;
        LDWorldLabels::FBarTrailState TrailSnapshot;
        float CentreX=0.f,Bottom=0.f,LeftReach=0.f,RightReach=0.f,TextWidth=0.f,Height=0.f,Row=0.f;
        bool bPublicService=false;
        // The current target (inspected) or the unit under the pointer: its name shows and a contender's bar turns live red.
        bool bFocused=false;
        FString DummyBoard;
        FLinearColor DummyBoardInk=FLinearColor::White;
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
        const int64 OverdriveThreshP2=Content?Content->Parameters.Overdrive.ThresholdP2:6000;
        const int64 MaxMomP2=Content&&Content->Parameters.Momentum.MaxP2>0?Content->Parameters.Momentum.MaxP2:10000;
        const uint8 OverdriveBarCut=uint8(FMath::Clamp(FMath::RoundToInt32(double(OverdriveThreshP2)/double(MaxMomP2)*double(LDObservation::MomentumBarFull)),1,255));
        for(const FLDObservedUnitView& Unit:Frame->Units)
        {
            FVector Centre;
            if(!Relay->ReadPresentedLocation(Unit.Handle,Unit.ObservationEpoch,Centre)){continue;}
            const bool bContender=Unit.Kind==ELDObservedUnitKind::Contender;
            FPending Entry;
            Entry.Label=LDWorldLabels::Build(Unit,bContender?LDHUD::ObservedContenderName(Public->DemoLobby,Unit.ContenderId,
                Public->PublicMatch.Phase==ELDMatchPhase::Rest):FString(),Content,Frame->SampledAtServerSec);
            Entry.CurrentHPP2=Unit.Health.CurrentP2;
            Entry.MaxHPP2=Unit.Health.MaxP2;
            Entry.ShieldP2=Entry.MaxHPP2>0?FMath::RoundToInt64(double(Entry.Label.ShieldFraction)*double(Entry.MaxHPP2)):0;
            if(bContender&&(Unit.AuthorizedFieldMask&LDObservation::Momentum)!=0)
            {
                Entry.bHasMomentum=true;
                Entry.MomentumFrac=float(Unit.MomentumBar)/float(LDObservation::MomentumBarFull);
                Entry.bMomentumOverdrive=Unit.bCharging||Unit.MomentumBar>=OverdriveBarCut;
            }
            for(const FLDObservedStatus& St:Unit.Statuses)
            {
                const TCHAR* CCName=nullptr;
                if(WorldIsHardCC(St.StatusId,CCName)&&St.ExpiresAtServerMs>0)
                {
                    const double RemSec=(double(St.ExpiresAtServerMs)-FrameServerSec*1000.)/1000.;
                    if(RemSec>0.)
                    {
                        const float Frac=float(FMath::Clamp(RemSec/2.5,.08,1.));
                        if(Frac>Entry.CCFrac){Entry.CCFrac=Frac;Entry.CCName=CCName;}
                    }
                }
                const TCHAR* ElemGlyph=nullptr;FLinearColor ElemCol;
                if(Entry.ElemDiscs.Num()<4&&WorldIsElementDisc(St.StatusId,ElemGlyph,ElemCol))
                {
                    Entry.ElemDiscs.Add({FString(ElemGlyph),ElemCol});
                }
            }
            if(Entry.Label.bHealth)
            {
                LDWorldLabels::FBarTrailState& Trail=UnitTrails.FindOrAdd(Unit.Handle);
                LDWorldLabels::AdvanceBarTrail(Trail,Entry.CurrentHPP2,Entry.MaxHPP2,Entry.ShieldP2,
                    Unit.RecentHitAgePlusOneMs!=0?Unit.HitFlags:0,Unit.RecentHitId,UiNow);
                Entry.TrailSnapshot=Trail;
            }
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
                if(bDummy&&Public->PublicMatch.Phase==ELDMatchPhase::Rest)
                {
                    const FLDTrainingComboView& Combo=PC->TrainingCombo;
                    const bool bThisDummy=Combo.Match==Own.Match&&(!Combo.DummyId.IsValid()||Combo.DummyId==Unit.ContenderId);
                    if(bThisDummy&&(Combo.TotalP2>0||Combo.PreviousTotalP2>0))
                    {
                        const int64 Total=(Combo.TotalP2+99)/100;
                        const double Sec=Combo.ElapsedMs>0?FMath::Max(1.,double(Combo.ElapsedMs)/1000.):1.;
                        const int64 Dps=Combo.TotalP2>0?FMath::RoundToInt64(double(Total)/Sec):0;
                        FString Board=FString::Printf(TEXT("本轮总伤 %lld · 实时秒伤 %lld/s"),Total,Dps);
                        Entry.DummyBoardInk=LDHUDStyle::UIAction;
                        if(Combo.PreviousTotalP2>0&&Combo.TotalP2>0)
                        {
                            const double Pct=(double(Combo.TotalP2-Combo.PreviousTotalP2)/double(Combo.PreviousTotalP2))*100.;
                            if(Pct>=.05)
                            {
                                Board+=FString::Printf(TEXT(" · 较上轮 +%.1f%% ▲"),Pct);
                                Entry.DummyBoardInk=bColorblind?FLinearColor(.30f,.75f,1.f):LDHUDStyle::UILegal;
                            }
                            else if(Pct<=-.05)
                            {
                                Board+=FString::Printf(TEXT(" · 较上轮 %.1f%% ▼"),Pct);
                                Entry.DummyBoardInk=LDHUDStyle::UIReplace;
                            }
                            else{Board+=TEXT(" · 较上轮 ±0.0%");}
                        }
                        else if(Combo.PreviousTotalP2>0&&Combo.TotalP2==0)
                        {
                            Board+=FString::Printf(TEXT(" · 上轮基准 %lld"),(Combo.PreviousTotalP2+99)/100);
                            Entry.DummyBoardInk=LDHUDStyle::TextSecondary;
                        }
                        Entry.DummyBoard=MoveTemp(Board);
                    }
                }
            }
            Entry.bFocused=(bHasHovered&&Unit.Handle==HoveredHandle&&Unit.ObservationEpoch==HoveredEpoch)||(bHasInspected&&Unit.Handle==Inspected.Handle);
            // A camp member keeps its tier in its bar's size and accent; the Boss keeps its name.
            const bool bBossLabel=Entry.Label.CampTier==int8(ELDDemoCampTier::Boss)||Entry.Label.CreatureId==uint8(ELDDemoMonsterId::MonsterDean);
            if(!Entry.bFocused&&!bBossLabel&&!Unit.bPeddler&&!Entry.Label.Name.StartsWith(LDTerms::TrainingDummy)){Entry.Label.Name.Reset();}
            const LDWorldLabels::FLabel& Label=Entry.Label;
            {
                const FVector UnitFeet=Centre-FVector(0.,0.,88.);
                const double UnitHeightCm=bContender?176.:WorldLabelHeadroomCm(Label)+48.;
                const double UnitRadiusCm=bContender?38.:44.;
                if(bContender&&!Label.bPeaceful)
                {
                    LDWorldLabels::FOcclusionSample& Occ=UnitOcclusions.FindOrAdd(Unit.Handle);
                    FSlateRect SilBounds;
                    if(SampleChestOccluded(Occ,UnitFeet)&&ProjectUnitCapsule(UnitFeet,UnitHeightCm,UnitRadiusCm,SilBounds))
                    {
                        DrawCapsuleSilhouette(SilBounds,EnemySilhouetteTint);
                    }
                }
                if(!Label.bPeaceful&&bHasHovered&&Unit.Handle==HoveredHandle&&Unit.ObservationEpoch==HoveredEpoch)
                {
                    FSlateRect HoverBounds;
                    if(ProjectUnitCapsule(UnitFeet,UnitHeightCm,UnitRadiusCm,HoverBounds))
                    {
                        DrawHoverOutline(HoverBounds);
                    }
                }
            }
            FVector2D Screen;
            if((Label.Name.IsEmpty()&&Label.Status.IsEmpty()&&!Label.bHealth)||!PC->ProjectWorldLocationToScreen(Centre+FVector(0,0,WorldLabelHeadroomCm(Label)),Screen)){continue;}
            Entry.CentreX=float(Screen.X)*ToLocalX;Entry.Bottom=float(Screen.Y)*ToLocalY;
            if(Culled(Entry.CentreX,Entry.Bottom)){continue;}
            if(Label.bHealth)
            {
                const FVector2f Dim=WorldBarDimensions(Label);
                const float BarRimW=Dim.X+3.f;
                const float TotalBarH=Dim.Y+(Entry.bHasMomentum?ContenderMomentumHeight+1.f:0.f)+3.f;
                const bool bBadge=Label.bContender;
                const float ExtraTop=(Entry.CCFrac>0.f?WorldCCBarHeight+14.f:0.f)+(!Entry.ElemDiscs.IsEmpty()?WorldElemDiscSide+3.f:0.f);
                Entry.Row=FMath::Max(TotalBarH,bBadge?BadgeRim:0.f)+ExtraTop;
                Entry.LeftReach=BarRimW*.5f+(bBadge?WorldBadgeGap+BadgeRim:0.f);
                Entry.RightReach=BarRimW*.5f;
            }
            float Lines=0.f;
            if(!Label.Name.IsEmpty())
            {
                const FVector2f Name=FontMeasure->Measure(Label.Name,Label.bContender?ContenderFont:CampFont);
                Entry.TextWidth=FMath::Max(Entry.TextWidth,Name.X);Lines+=Name.Y;
            }
            if(!Label.StatusParts.IsEmpty())
            {
                // The status squares' rows (as painted below).
                const float Side=FMath::RoundToFloat(OwnStatusSide*.8f*TextScale);const int32 Count=Label.StatusParts.Num();
                const int32 Rows=(Count+OwnStatusPerRow-1)/OwnStatusPerRow;
                Entry.TextWidth=FMath::Max(Entry.TextWidth,float(FMath::Min(Count,OwnStatusPerRow))*(Side+OwnStatusGap));
                Lines+=float(Rows)*(Side+OwnStatusGap)+2.f;
            }
            if(!Entry.DummyBoard.IsEmpty())
            {
                const FVector2f BoardExt=FontMeasure->Measure(Entry.DummyBoard,ContenderFont)+FVector2f(16.f,8.f);
                Entry.TextWidth=FMath::Max(Entry.TextWidth,BoardExt.X);Lines+=BoardExt.Y+4.f;
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
            const FVector2f Dim=WorldBarDimensions(Label);
            const float TotalInnerH=Dim.Y+(Entry.bHasMomentum?ContenderMomentumHeight+1.f:0.f);
            const float BarLeft=CentreX-Dim.X*.5f;
            const float BarTop=Bottom-TotalInnerH-1.5f;
            if(Label.bCharging)
            {
                const float Pulse=.55f+.45f*FMath::Abs(FMath::Sin(float(UiNow)*9.f));
                Box(0,FVector2f(BarLeft-4.f,BarTop-4.f),FVector2f(Dim.X+8.f,TotalInnerH+8.f),LDHUDStyle::Overdrive.CopyWithNewOpacity(Pulse));
            }
            const bool bBoss=Label.CampTier==int8(ELDDemoCampTier::Boss)||Label.CreatureId==uint8(ELDDemoMonsterId::MonsterDean);
            const FLinearColor TierAccent=bBoss?WorldBossFrameInk:(Label.CampTier>=0?LDHUDStyle::CampTierInk(uint8(Label.CampTier)):FLinearColor::Transparent);
            const FLinearColor HPFill=!Label.bContender?WorldCampHPFill:Label.bPeaceful?LDHUDStyle::PeacefulHealth
                :!Entry.bFocused?WorldOtherHPFill:(bColorblind?WorldEnemyHPColorblind:WorldEnemyHPFill);
            DrawSegmentedBar(BarLeft,BarTop,Dim.X,Dim.Y,Entry.CurrentHPP2,Entry.MaxHPP2,Entry.ShieldP2,
                HPFill,Entry.TrailSnapshot,Entry.bHasMomentum,Entry.MomentumFrac,Entry.bMomentumOverdrive,
                (bBoss||Label.CampTier>=0)?&TierAccent:nullptr);

            float UpperY=BarTop-2.f;
            // A training dummy is nobody's seat: it keeps its name and bar only.
            if(Label.bContender&&!Label.Name.StartsWith(LDTerms::TrainingDummy)){DrawSeatPlate(BarLeft,BarTop,TotalInnerH,Label.Presentation);UpperY=FMath::Min(UpperY,BarTop-3.f);}
            if(!Entry.ElemDiscs.IsEmpty())
            {
                float ElemRight=BarLeft+Dim.X;
                const float ElemCy=BarTop-WorldElemDiscSide*.5f-3.f;
                for(int32 EI=Entry.ElemDiscs.Num()-1;EI>=0;--EI)
                {
                    const FVector2f CPos(ElemRight-WorldElemDiscSide*.5f,ElemCy);
                    Disc(2,CPos,WorldElemDiscSide+2.f,WorldOuterRim);
                    Disc(3,CPos,WorldElemDiscSide,Entry.ElemDiscs[EI].Value);
                    const FVector2f GE=FontMeasure->Measure(Entry.ElemDiscs[EI].Key,TinyBoldFont);
                    FSlateDrawElement::MakeText(E,Base+5,G.ToPaintGeometry(GE,FSlateLayoutTransform(CPos-GE*.5f)),
                        Entry.ElemDiscs[EI].Key,TinyBoldFont,ESlateDrawEffect::None,WorldOuterRim);
                    ElemRight-=WorldElemDiscSide+WorldElemDiscGap;
                }
                UpperY=FMath::Min(UpperY,ElemCy-WorldElemDiscSide*.5f-2.f);
            }
            if(Entry.CCFrac>0.f)
            {
                const float CCTop=UpperY-WorldCCBarHeight-1.f;
                Box(2,FVector2f(BarLeft-1.f,CCTop-1.f),FVector2f(Dim.X+2.f,WorldCCBarHeight+2.f),WorldOuterRim);
                Box(3,FVector2f(BarLeft,CCTop),FVector2f(Dim.X*Entry.CCFrac,WorldCCBarHeight),WorldCCBarFill);
                const FVector2f CCE=FontMeasure->Measure(Entry.CCName,TinyBoldFont);
                FSlateDrawElement::MakeText(E,Base+5,G.ToPaintGeometry(CCE,FSlateLayoutTransform(FVector2f(BarLeft,CCTop-CCE.Y-1.f))),
                    Entry.CCName,TinyBoldFont,ESlateDrawEffect::None,WorldCCBarFill);
                UpperY=CCTop-CCE.Y-2.f;
            }
            Bottom=UpperY-WorldRowGap;
        }
        if(!Label.Name.IsEmpty())
        {
            const FLinearColor Ink=Label.bContender?WorldLabelContenderInk:Label.CampTier>=0?LDHUDStyle::CampTierInk(uint8(Label.CampTier)):WorldLabelCampInk;
            Line(Label.Name,Label.bContender?ContenderFont:CampFont,Ink,CentreX,Bottom);
        }
        if(!Label.StatusParts.IsEmpty())
        {
            // Each status as a small square above the name: its picture (or its first character) on a dark tile with a green
            // or red rim (honoring colorblind mode), its stacks over 1 in the corner.
            const float Side=FMath::RoundToFloat(OwnStatusSide*.8f*TextScale);
            const TArray<FSlateRect> Squares=LDWorldLabels::OwnStatusIconRects(Label.StatusParts.Num(),CentreX,Bottom-1.f,Side,OwnStatusGap,OwnStatusPerRow);
            float Top=Bottom;
            for(int32 Index=0;Index<Squares.Num();++Index)
            {
                const LDWorldLabels::FLabel::FStatusPart& Part=Label.StatusParts[Index];const FSlateRect& Square=Squares[Index];
                const FLinearColor Edge=Part.bPositive?(bColorblind?FLinearColor(.25f,.68f,1.f):LDHUDStyle::StatusPositive)
                    :(bColorblind?FLinearColor(1.f,.52f,.10f):LDHUDStyle::StatusNegative);
                const FVector2f At(Square.Left,Square.Top);
                Box(1,At-FVector2f(1.f,1.f),FVector2f(Side+2.f,Side+2.f),Edge);
                Box(2,At,FVector2f(Side,Side),FLinearColor(.02f,.03f,.05f,.92f));
                const FSlateBrush* Picture=nullptr;
                if(!Part.IconId.IsEmpty())
                {
                    if(FSlateBrush* Known=StatusBrushes.Find(Part.IconId)){Picture=Known;}
                    else if(UTexture2D* Texture=LDUIIcons::Find(Part.IconId))
                    {
                        StatusTextures.Add(Part.IconId,Texture);
                        FSlateBrush& Brush=StatusBrushes.Add(Part.IconId);
                        Brush.SetResourceObject(Texture);Brush.ImageSize=FVector2D(64,64);Brush.DrawAs=ESlateBrushDrawType::Image;
                        Picture=&Brush;
                    }
                }
                if(Picture)
                {FSlateDrawElement::MakeBox(E,Base+3,G.ToPaintGeometry(FVector2f(Side,Side),FSlateLayoutTransform(At)),Picture,ESlateDrawEffect::None,FLinearColor::White);}
                else
                {
                    const FVector2f GlyphExtent=FontMeasure->Measure(Part.Glyph,StatusFont);
                    const FVector2f GlyphAt(At.X+Side*.5f-GlyphExtent.X*.5f,At.Y+Side*.5f-GlyphExtent.Y*.5f);
                    FSlateDrawElement::MakeText(E,Base+5,G.ToPaintGeometry(GlyphExtent,FSlateLayoutTransform(GlyphAt)),Part.Glyph,StatusFont,ESlateDrawEffect::None,WorldStatusInk(Part.StatusId));
                }
                if(Part.Stacks>1)
                {
                    const FString StackText=FString::FromInt(Part.Stacks);const FVector2f StackExtent=FontMeasure->Measure(StackText,TinyBoldFont);
                    const FVector2f StackAt(At.X+Side-StackExtent.X+1.f,At.Y+Side-StackExtent.Y+1.f);
                    FSlateDrawElement::MakeText(E,Base+5,G.ToPaintGeometry(StackExtent,FSlateLayoutTransform(StackAt)),StackText,TinyBoldFont,ESlateDrawEffect::None,FLinearColor::White);
                }
                Top=FMath::Min(Top,Square.Top-1.f);
            }
            Bottom=Top;
        }
        if(!Entry.DummyBoard.IsEmpty())
        {
            const FVector2f TextExt=FontMeasure->Measure(Entry.DummyBoard,ContenderFont);
            const FVector2f PlateExt=TextExt+FVector2f(16.f,8.f);
            const float PlateLeft=RowLeft(CentreX,PlateExt.X);
            const float PlateTop=Bottom-PlateExt.Y-3.f;
            Box(1,FVector2f(PlateLeft-1.f,PlateTop-1.f),PlateExt+FVector2f(2.f,2.f),Entry.DummyBoardInk.CopyWithNewOpacity(.85f));
            Box(2,FVector2f(PlateLeft,PlateTop),PlateExt,FLinearColor(.02f,.035f,.055f,.92f));
            FSlateDrawElement::MakeText(E,Base+5,G.ToPaintGeometry(TextExt,FSlateLayoutTransform(FVector2f(PlateLeft+8.f,PlateTop+4.f))),
                Entry.DummyBoard,ContenderFont,ESlateDrawEffect::None,Entry.DummyBoardInk);
            Bottom=PlateTop-1.f;
        }
    }
    return Base+5;
}
