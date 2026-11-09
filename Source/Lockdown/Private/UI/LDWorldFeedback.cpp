#include "UI/LDWorldFeedback.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
#include "UI/LDControlSettings.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDCaptureTypes.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDWorldViewRelay.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
// Whole points from hundredths, rounded half-up; the sign belongs to the event kind.
int64 WorldFeedbackPoints(int64 AbsP2){return (AbsP2+50)/100;}
// Regeneration stays quiet: a gain reads as a heal only above 25 points per second of authority time (at least 0.1 s)
// plus one point, clear of the fastest contender regeneration.
double WorldFeedbackRegenToleranceP2(double Seconds){return 100.+2500.*FMath::Max(.1,Seconds);}
// Camp members never regenerate: any gain above one point is a heal (a camp that returned home refills).
constexpr int64 WorldFeedbackCampToleranceP2=100;
const FLDCombatStatusView* WorldFeedbackShieldStatus(const FLDCombatOwnerView& Combat)
{return Combat.Statuses.FindByPredicate([](const FLDCombatStatusView& Status){return Status.StatusId==uint8(ELDDemoStatusId::Shield);});}
void WorldFeedbackAdd(TArray<LDWorldFeedback::FEvent>& Out,LDWorldFeedback::EKind Kind,int64 AbsP2)
{
    const int64 Points=WorldFeedbackPoints(AbsP2);
    if(Points<=0){return;}
    LDWorldFeedback::FEvent& Event=Out.AddDefaulted_GetRef();Event.Kind=Kind;Event.Amount=Points;
}
}

void LDWorldFeedback::FTracker::Reset(uint32 InMatch)
{
    *this=FTracker();
    Match=InMatch;
}

void LDWorldFeedback::FTracker::Interrupt()
{
    bBody=false;
    bFrame=false;
    Units.Reset();
}

void LDWorldFeedback::FTracker::Observe(const FSample& Sample,TArray<FEvent>& Out)
{
    if(Sample.Match==0){return;}
    // A new match primes every baseline again; nothing from the previous one is compared or replayed.
    if(Sample.Match!=Match){Reset(Sample.Match);}
    ObserveBody(Sample,Out);
    ObserveGrowth(Sample,Out);
    ObserveKills(Sample,Out);
    ObserveUnits(Sample,Out);
}

void LDWorldFeedback::FTracker::ObserveBody(const FSample& Sample,TArray<FEvent>& Out)
{
    const FLDCombatOwnerView* Combat=Sample.OwnCombat;
    // Only the body of this match and life; anything else leaves the baseline alone until the current view arrives.
    if(!Combat||!Combat->bHasBody||Sample.Life==0||Combat->Generation!=Sample.Match||Combat->Life!=Sample.Life){return;}
    const bool bNewLife=!bBody||BodyLife!=Combat->Life;
    if(!bNewLife&&Combat->Revision<=BodyRevision){return;}
    const FLDCombatStatusView* Shield=WorldFeedbackShieldStatus(*Combat);
    // A changed maximum rescales the points proportionally (level, build): never a hit or a heal.
    if(!bNewLife&&Combat->MaxHPP2==MaxHPP2&&Sample.LifePhase==ELDLifePhase::Alive)
    {
        const int64 DeltaHP=Combat->HPP2-HPP2,DeltaShield=Combat->ShieldP2-ShieldP2;
        if(DeltaHP<0){WorldFeedbackAdd(Out,EKind::DamageTaken,-DeltaHP);}
        else if(double(DeltaHP)>WorldFeedbackRegenToleranceP2(double(Combat->AsOfMs-BodyAsOfMs)/1000.)){WorldFeedbackAdd(Out,EKind::Heal,DeltaHP);}
        if(DeltaShield>0){WorldFeedbackAdd(Out,EKind::ShieldGain,DeltaShield);}
        // Absorbed while the shield is still listed, with lost points, or gone before its listed end; a shield that
        // merely reached its end is silent.
        else if(DeltaShield<0&&(Shield||DeltaHP<0||ShieldEndsAtMs>Combat->AsOfMs)){WorldFeedbackAdd(Out,EKind::ShieldAbsorbed,-DeltaShield);}
    }
    bBody=true;BodyLife=Combat->Life;BodyRevision=Combat->Revision;BodyAsOfMs=Combat->AsOfMs;
    HPP2=Combat->HPP2;MaxHPP2=Combat->MaxHPP2;ShieldP2=Combat->ShieldP2;ShieldEndsAtMs=Shield?Shield->ExpiresAtMs:0;
}

void LDWorldFeedback::FTracker::ObserveGrowth(const FSample& Sample,TArray<FEvent>& Out)
{
    // A missing, invalid or foreign view drops its baseline, so the next valid view of this match only primes again.
    const FLDProgressionView* Progression=Sample.Progression;
    const bool bProgress=Progression&&Progression->bValid&&Progression->Generation==Sample.Match;
    if(bProgress&&bGrowth)
    {
        // Every approved reward grants XP (also past the last level). Each growth publication is its own sample, so
        // separate last hits stay separate rewards.
        if(Progression->CumulativeXP>XP)
        {
            FEvent& XPEvent=Out.AddDefaulted_GetRef();XPEvent.Kind=EKind::Experience;XPEvent.Amount=Progression->CumulativeXP-XP;
        }
        // Several levels at once are one level-up naming the level reached and every three-choice earned with it.
        if(Progression->Level>Level)
        {
            FEvent& LevelEvent=Out.AddDefaulted_GetRef();LevelEvent.Kind=EKind::LevelUp;LevelEvent.Level=Progression->Level;
            LevelEvent.Amount=FMath::Max(0,int32(Progression->EarnedThrough)-int32(Earned));LevelEvent.bChoiceEarned=LevelEvent.Amount>0;
        }
    }
    bGrowth=bProgress;
    if(bProgress){Level=Progression->Level;Earned=Progression->EarnedThrough;XP=Progression->CumulativeXP;}
}

void LDWorldFeedback::FTracker::ObserveKills(const FSample& Sample,TArray<FEvent>& Out)
{
    const FLDKillFeedView* Feed=Sample.KillFeed;
    if(!Feed||Feed->Generation==0||Feed->Generation!=Sample.PublicGeneration||Feed->Generation!=Sample.Match){return;}
    if(!bKills)
    {
        // The first feed of this match primes: notices already listed are never replayed.
        bKills=true;
        for(const FLDKillNotice& Notice:Feed->Notices){KillSerial=FMath::Max(KillSerial,Notice.Serial);}
        return;
    }
    // Without the own identity nothing can be attributed yet; the notices wait instead of being guessed.
    if(!Sample.Self.IsValid()){return;}
    TArray<const FLDKillNotice*> Fresh;
    for(const FLDKillNotice& Notice:Feed->Notices){if(Notice.Serial>KillSerial){Fresh.Add(&Notice);}}
    Fresh.Sort([](const FLDKillNotice& First,const FLDKillNotice& Second){return First.Serial<Second.Serial;});
    for(const FLDKillNotice* Notice:Fresh)
    {
        KillSerial=Notice->Serial;
        if(Sample.bClock&&Sample.NowMs-Notice->OccurredAtMs>KillNoticeFreshMs){continue;}
        if(Notice->Killer!=Sample.Self||Notice->Victim==Sample.Self){continue;}
        FEvent& Event=Out.AddDefaulted_GetRef();Event.Kind=EKind::Kill;Event.Amount=1;
        Event.Name=Sample.RosterName?Sample.RosterName(Notice->Victim):FString();
    }
}

void LDWorldFeedback::FTracker::ObserveUnits(const FSample& Sample,TArray<FEvent>& Out)
{
    const FLDWorldView* Frame=Sample.Frame;
    // Only the current frame of this match and living observer; any gap forgets every unit (no LastSeen).
    if(!Frame||Sample.Life==0||Sample.LifePhase!=ELDLifePhase::Alive||Frame->MatchGeneration!=Sample.Match||Frame->ObserverLife!=Sample.Life)
    {bFrame=false;Units.Reset();return;}
    const bool bPrime=!bFrame||Frame->Generation!=FrameGeneration;
    if(!bPrime&&Frame->Revision<=FrameRevision){return;}
    // Only units listed in consecutive frames under the same handle and epoch compare; a unit missing from a frame is
    // forgotten, so returning to sight (always under a new handle) only primes.
    TArray<FUnit> Seen;
    for(const FLDObservedUnitView& Unit:Frame->Units)
    {
        if((Unit.AuthorizedFieldMask&LDObservation::Health)==0||Unit.Health.MaxP2<=0){continue;}
        FUnit Next;
        Next.Handle=Unit.Handle;Next.Epoch=Unit.ObservationEpoch;Next.bCamp=Unit.Kind==ELDObservedUnitKind::CampMember;
        Next.HPP2=Unit.Health.CurrentP2;Next.MaxP2=Unit.Health.MaxP2;Next.SampledAtSec=Frame->SampledAtServerSec;
        Next.Cue=uint8(Unit.ActionCue);Next.CueAgeMs=Unit.ActionCueAgeMs;Next.Ability=Unit.ActionAbility;
        const FUnit* Last=bPrime?nullptr:Units.FindByPredicate([&Next](const FUnit& Known){return Known.Handle==Next.Handle&&Known.Epoch==Next.Epoch;});
        // A cast with a public ability shows its name once: when the cue turns to Cast, restarts (its age falls) or names
        // another ability. A unit first seen mid-cast only primes, like its health.
        const bool bCast=Next.Cue==uint8(ELDObservedActionCue::Cast)&&Next.Ability!=0;
        if(Last&&bCast&&(Last->Cue!=Next.Cue||Next.CueAgeMs<Last->CueAgeMs||Last->Ability!=Next.Ability))
        {
            FEvent& Event=Out.AddDefaulted_GetRef();
            Event.Kind=EKind::UnitCast;Event.Amount=0;Event.bOwn=false;Event.Handle=Next.Handle;Event.Epoch=Next.Epoch;
            Event.SampledAtServerSec=Next.SampledAtSec;Event.Name=Sample.CastName?Sample.CastName(Next.Ability):FString();
        }
        if(Last&&Last->MaxP2==Next.MaxP2&&Last->bCamp==Next.bCamp)
        {
            const int64 Delta=Next.HPP2-Last->HPP2;
            const double Tolerance=Next.bCamp?double(WorldFeedbackCampToleranceP2):WorldFeedbackRegenToleranceP2(Next.SampledAtSec-Last->SampledAtSec);
            const int64 Points=WorldFeedbackPoints(FMath::Abs(Delta));
            if(Points>0&&(Delta<0||double(Delta)>Tolerance))
            {
                FEvent& Event=Out.AddDefaulted_GetRef();
                Event.Kind=Delta<0?EKind::UnitDamage:EKind::UnitHeal;Event.Amount=Points;
                Event.bOwn=false;Event.Handle=Next.Handle;Event.Epoch=Next.Epoch;Event.SampledAtServerSec=Next.SampledAtSec;
            }
        }
        Seen.Add(Next);
    }
    Units=MoveTemp(Seen);
    bFrame=true;FrameGeneration=Frame->Generation;FrameRevision=Frame->Revision;
}

FString LDWorldFeedback::Text(const FEvent& Event)
{
    switch(Event.Kind)
    {
    case EKind::DamageTaken: case EKind::UnitDamage: return FString::Printf(TEXT("-%lld"),Event.Amount);
    case EKind::Heal: case EKind::UnitHeal: return FString::Printf(TEXT("+%lld"),Event.Amount);
    case EKind::ShieldGain: return FString::Printf(TEXT("+%lld %s"),Event.Amount,LDTerms::Shield);
    case EKind::ShieldAbsorbed: return FString::Printf(TEXT("-%lld %s"),Event.Amount,LDTerms::Shield);
    case EKind::Experience: return FString::Printf(TEXT("+%lld %s"),Event.Amount,LDTerms::Experience);
    case EKind::LevelUp: return LDTerms::LevelUp(Event.Level);
    case EKind::Kill: return LDTerms::KillFloat(Event.Name.IsEmpty()?FString(LDTerms::UnknownVictim):Event.Name);
    case EKind::UnitCast: return Event.Name.IsEmpty()?FString(TEXT("施法！")):Event.Name+TEXT("！");
    }
    return FString();
}

FString LDWorldFeedback::Subline(const FEvent& Event)
{
    if(Event.Kind!=EKind::LevelUp||!Event.bChoiceEarned){return FString();}
    return LDTerms::ChoicesEarned(Event.Amount);
}

LDWorldFeedback::EUnitHold LDWorldFeedback::UnitHold(bool bPresented,bool bPresentationClock,double PresentationAtServerSec,double SampledAtServerSec,double HeldSeconds)
{
    if(!bPresented){return EUnitHold::Drop;}
    // Both times are on the authority sample scale; the hold limit is local presentation time.
    return !bPresentationClock||PresentationAtServerSec>=SampledAtServerSec||HeldSeconds>=UnitHoldMaxSeconds?EUnitHold::Release:EUnitHold::Hold;
}

namespace
{
constexpr double WorldFeedbackMergeSeconds=.15;
constexpr double WorldFeedbackPopSeconds=.12;
// A number's lane stays taken while it is still near the start of its rise.
constexpr double WorldFeedbackLaneSeconds=.5;
// Ticks stop while the root collapses the layer (menu, Settings); a longer pause than this re-primes body and units.
constexpr double WorldFeedbackResumeSeconds=1.;
constexpr double WorldFeedbackBannerAppearSeconds=.1;
constexpr double WorldFeedbackBannerPopSeconds=.2;
constexpr double WorldFeedbackBannerFadeSeconds=.6;
constexpr double WorldFeedbackKillTintSeconds=.5;
constexpr int32 WorldFeedbackMaxFloaters=32;
constexpr int32 WorldFeedbackMaxBanners=3;
constexpr int32 WorldFeedbackLanes=5;
constexpr int32 WorldFeedbackRewardRows=6;
// Heights above the body centre (cm): the own bars and readiness pips rest on +120 (LDWorldLabels) in a block 23 units
// tall, and so do a unit's label rows; rewards sit beside the own body, a unit's numbers at its chest, banners above.
constexpr double WorldFeedbackBarsCm=120.;
constexpr double WorldFeedbackRewardCm=60.;
constexpr double WorldFeedbackUnitNumberCm=40.;
constexpr double WorldFeedbackBannerCm=230.;
// Slate units at the 1080p reference; text-relative distances also scale with the text preference.
constexpr float WorldFeedbackOwnBarsHeight=23.f;
constexpr float WorldFeedbackClearGap=4.f;
constexpr float WorldFeedbackRise=44.f;
constexpr float WorldFeedbackRewardRise=26.f;
constexpr float WorldFeedbackLaneGap=28.f;
constexpr float WorldFeedbackLaneStep=72.f;
constexpr float WorldFeedbackRewardGap=44.f;
// A unit's numbers start right or left of its body, the near edge this far from its centre, then step outward.
constexpr float WorldFeedbackUnitSideGap=34.f;
constexpr float WorldFeedbackUnitStep=46.f;
constexpr int32 WorldFeedbackUnitSteps=3;
// The banner stack starts above the highest point a rising number reaches.
constexpr float WorldFeedbackBannerClearance=80.f;
constexpr float WorldFeedbackBannerGap=6.f;
constexpr float WorldFeedbackCullMargin=160.f;
// Level-up effect around the own feet: two expanding rings and rising light streaks.
constexpr double WorldFeedbackRingSeconds=.9;
constexpr double WorldFeedbackRingDelay=.25;
constexpr double WorldFeedbackRingFromCm=40.;
constexpr double WorldFeedbackRingToCm=170.;
constexpr int32 WorldFeedbackRingPoints=32;
constexpr int32 WorldFeedbackRings=2;
constexpr double WorldFeedbackStreakSeconds=1.2;
constexpr double WorldFeedbackStreakTopCm=220.;
constexpr double WorldFeedbackStreakLengthCm=90.;
constexpr double WorldFeedbackStreakRadiusCm=55.;
constexpr int32 WorldFeedbackStreaks=6;
constexpr double WorldFeedbackEffectSeconds=1.2;
constexpr float WorldFeedbackEffectThickness=3.f;

struct FWorldFeedbackLook
{
    FLinearColor Ink;
    int32 FontSize;
};
FWorldFeedbackLook WorldFeedbackLookOf(LDWorldFeedback::EKind Kind)
{
    switch(Kind)
    {
    case LDWorldFeedback::EKind::DamageTaken: return {LDHUDStyle::DamageTaken,LDHUDStyle::TypeTitle};
    case LDWorldFeedback::EKind::UnitDamage: return {LDHUDStyle::DamageDealt,LDHUDStyle::TypeLabel};
    case LDWorldFeedback::EKind::Heal: case LDWorldFeedback::EKind::UnitHeal: return {LDHUDStyle::Heal,LDHUDStyle::TypeLabel};
    case LDWorldFeedback::EKind::ShieldGain: return {LDHUDStyle::ShieldGain,LDHUDStyle::TypeLabel};
    case LDWorldFeedback::EKind::ShieldAbsorbed: return {LDHUDStyle::Shield,LDHUDStyle::TypeLabel};
    case LDWorldFeedback::EKind::Experience: return {LDHUDStyle::ExperienceGain,LDHUDStyle::TypeBody};
    case LDWorldFeedback::EKind::UnitCast: return {FLinearColor(.85f,.55f,1.f),LDHUDStyle::TypeTitle};
    default: return {LDHUDStyle::Celebration,LDHUDStyle::TypeBanner};
    }
}
bool WorldFeedbackIsReward(LDWorldFeedback::EKind Kind){return Kind==LDWorldFeedback::EKind::Experience;}
bool WorldFeedbackSameAnchor(const LDWorldFeedback::FEvent& First,const LDWorldFeedback::FEvent& Second)
{return First.bOwn==Second.bOwn&&(First.bOwn||(First.Handle==Second.Handle&&First.Epoch==Second.Epoch));}
float WorldFeedbackEaseOut(float Progress)
{
    const float Rest=1.f-FMath::Clamp(Progress,0.f,1.f);
    return 1.f-Rest*Rest*Rest;
}
// Pop-in scale in tenths, so a growing line rasterizes only a few glyph sizes.
float WorldFeedbackPop(double Age,double Seconds,float From)
{
    if(Age<0.||Age>=Seconds){return 1.f;}
    return FMath::RoundToFloat(FMath::Lerp(From*10.f,10.f,float(Age/Seconds)))/10.f;
}
// The UI's default CJK-capable bold font at the local text scale. The outline fades with the line: Slate draws a text
// outline in its own colour, not the tint's opacity.
FSlateFontInfo WorldFeedbackFont(int32 Size,float TextScale,int32 OutlineSize,float Alpha)
{
    FFontOutlineSettings Outline;Outline.OutlineSize=OutlineSize;Outline.OutlineColor=LDHUDStyle::Rim;Outline.OutlineColor.A*=Alpha;
    return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),float(FMath::Max(1,FMath::RoundToInt(float(Size)*TextScale))),Outline);
}
}

void ULDWorldFeedback::ClearShown()
{
    Held.Reset();
    Floaters.Reset();
    Banners.Reset();
    LevelUpAt=-1.;
}

void ULDWorldFeedback::Present(const LDWorldFeedback::FEvent& Event,double Now)
{
    using LDWorldFeedback::EKind;
    if(Event.Kind==EKind::LevelUp||Event.Kind==EKind::Kill)
    {
        if(Event.Kind==EKind::LevelUp){LevelUpAt=Now;}
        LDWorldFeedback::FBanner& Banner=Banners.AddDefaulted_GetRef();
        Banner.Kind=Event.Kind;Banner.Text=LDWorldFeedback::Text(Event);Banner.Subline=LDWorldFeedback::Subline(Event);Banner.BornAt=Now;
        while(Banners.Num()>WorldFeedbackMaxBanners){Banners.RemoveAt(0);}
        return;
    }
    // Damage and heal numbers landing together read as one: a same-kind event at the same anchor within the merge window
    // adds up and pops the number again without restarting its rise. Each reward publication keeps its own lines.
    const bool bReward=WorldFeedbackIsReward(Event.Kind);
    LDWorldFeedback::FFloater* Same=bReward?nullptr:Floaters.FindByPredicate([&Event,Now](const LDWorldFeedback::FFloater& Shown)
    {return Shown.Event.Kind==Event.Kind&&WorldFeedbackSameAnchor(Shown.Event,Event)&&Now-Shown.BornAt<=WorldFeedbackMergeSeconds;});
    if(Same)
    {
        Same->Event.Amount+=Event.Amount;Same->Text=LDWorldFeedback::Text(Same->Event);Same->PopAt=Now;
        return;
    }
    int32 Lane=0;
    if(bReward)
    {
        // A reward line starts below every reward line still shown, which rise away from it, so lines never overlap.
        for(const LDWorldFeedback::FFloater& Shown:Floaters)
        {if(WorldFeedbackIsReward(Shown.Event.Kind)&&Now-Shown.BornAt<LDHUDStyle::FloatSeconds){Lane=FMath::Max(Lane,Shown.Lane+1);}}
        if(Lane>=WorldFeedbackRewardRows){Lane=0;}
    }
    else if(!Event.bOwn)
    {
        // Consecutive numbers at one unit alternate right and left of its body; on that side each takes the nearest step
        // not held by a number still near its start.
        const LDWorldFeedback::FFloater* Latest=nullptr;
        for(const LDWorldFeedback::FFloater& Shown:Floaters)
        {
            if(WorldFeedbackSameAnchor(Shown.Event,Event)&&Now-Shown.BornAt<LDHUDStyle::FloatSeconds&&(!Latest||Shown.BornAt>=Latest->BornAt))
            {Latest=&Shown;}
        }
        const int32 Side=Latest?1-Latest->Lane%2:0;
        int32 Step=0;
        for(;Step<WorldFeedbackUnitSteps;++Step)
        {
            const int32 Candidate=Step*2+Side;
            const bool bTaken=Floaters.ContainsByPredicate([&Event,Now,Candidate](const LDWorldFeedback::FFloater& Shown)
            {return Shown.Lane==Candidate&&WorldFeedbackSameAnchor(Shown.Event,Event)&&Now-Shown.BornAt<WorldFeedbackLaneSeconds;});
            if(!bTaken){break;}
        }
        Lane=(Step==WorldFeedbackUnitSteps?0:Step)*2+Side;
    }
    else
    {
        // Own numbers take the first lane not held by a number still near its start.
        for(;Lane<WorldFeedbackLanes;++Lane)
        {
            const bool bTaken=Floaters.ContainsByPredicate([&Event,Now,Lane](const LDWorldFeedback::FFloater& Shown)
            {return Shown.Lane==Lane&&!WorldFeedbackIsReward(Shown.Event.Kind)&&WorldFeedbackSameAnchor(Shown.Event,Event)&&Now-Shown.BornAt<WorldFeedbackLaneSeconds;});
            if(!bTaken){break;}
        }
        if(Lane==WorldFeedbackLanes){Lane=0;}
    }
    LDWorldFeedback::FFloater& Floater=Floaters.AddDefaulted_GetRef();
    Floater.Event=Event;Floater.Text=LDWorldFeedback::Text(Event);Floater.BornAt=Now;Floater.PopAt=Now;Floater.Lane=Lane;
#if WITH_DEV_AUTOMATION_TESTS
    if(bReward){DevRewards.Add(Floater.Text);}
#endif
    while(Floaters.Num()>WorldFeedbackMaxFloaters){Floaters.RemoveAt(0);}
}

void LDWorldFeedback::MakeSample(const ALDPlayerController& PC,const ALDGameState& Public,const FLDDemoContentDefinitions* Content,FSample& Out)
{
    const FLDOwnerState& Own=PC.OwnerState;
    const ALDWorldViewRelay* Relay=Own.LifePhase==ELDLifePhase::Alive?PC.LocalView():nullptr;
    const FLDObservedFrameRead Read=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
    const FLDCombatOwnerView& Combat=PC.CombatView;
    Out=FSample();
    Out.Match=Own.Match;Out.Life=Own.Life;Out.LifePhase=Own.LifePhase;Out.Self=Own.ContenderId;
    Out.OwnCombat=Own.Life!=0&&Combat.bHasBody&&Combat.Generation==Own.Match&&Combat.Life==Own.Life?&Combat:nullptr;
    Out.Progression=PC.ReadOwnProgression();
    Out.KillFeed=&Public.KillFeed;Out.PublicGeneration=Public.PublicMatch.Generation;
    Out.bClock=PC.ReadDemoAuthorityTimeMs(Out.NowMs);
    Out.Frame=Read.Frame&&Read.Status==ELDObservedViewStatus::Current&&Read.Frame->MatchGeneration==Own.Match&&Read.Frame->ObserverLife==Own.Life?Read.Frame:nullptr;
    const ALDGameState* PublicState=&Public;
    Out.RosterName=[PublicState](const FGuid& Contender){return LDHUD::RosterName(PublicState->DemoLobby,Contender);};
    Out.CastName=[Content](uint8 Ability){return LDHUD::AbilityName(Content,Ability);};
}

void ULDWorldFeedback::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);
    if(!bDisplayContentTried)
    {
        // Missing content stays visible: a cast then reads by its stable id (未知技能 #n).
        bDisplayContentTried=true;FString Error;
        if(!LDDemoEntry::LoadRuntimeContent(DisplayContent,Error)||!DisplayContent){DisplayContent.Reset();UE_LOG(LogLDUI,Error,TEXT("LD_WORLD_FEEDBACK_CONTENT_UNAVAILABLE %s"),*Error);}
    }
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());const UWorld* World=GetWorld();
    const ALDGameState* Public=World?World->GetGameState<ALDGameState>():nullptr;
    if(!PC||!Public){return;}
    // Local presentation time only; it is never compared with an authority deadline.
    const double Now=World->GetRealTimeSeconds();
    if(LastTickAt>=0.&&Now-LastTickAt>WorldFeedbackResumeSeconds){Tracker.Interrupt();Held.Reset();}
    LastTickAt=Now;
    const FLDOwnerState& Own=PC->OwnerState;
    if(Own.Match==0||Own.Match!=Public->PublicMatch.Generation){ClearShown();return;}
#if WITH_DEV_AUTOMATION_TESTS
    if(Own.Match!=DevRewardsMatch){DevRewardsMatch=Own.Match;DevRewards.Reset();}
#endif
    // Shown feedback belongs to one match and life; a new one starts clean.
    if(Own.Match!=ShownMatch||Own.Life!=ShownLife){ShownMatch=Own.Match;ShownLife=Own.Life;ClearShown();}
    const ALDWorldViewRelay* Relay=Own.LifePhase==ELDLifePhase::Alive?PC->LocalView():nullptr;
    LDWorldFeedback::FSample Sample;
    LDWorldFeedback::MakeSample(*PC,*Public,DisplayContent?&DisplayContent->Get():nullptr,Sample);
    const FLDWorldView* Frame=Sample.Frame;
    TArray<LDWorldFeedback::FEvent> Events;
    Tracker.Observe(Sample,Events);
    for(const LDWorldFeedback::FEvent& Event:Events)
    {
        if(Event.bOwn){Present(Event,Now);continue;}
        LDWorldFeedback::FHeldNumber& Waiting=Held.AddDefaulted_GetRef();Waiting.Event=Event;Waiting.QueuedAt=Now;
    }
    while(Held.Num()>WorldFeedbackMaxFloaters){Held.RemoveAt(0);}
    // A unit's number shows when the relay poses the frame that produced it, together with the delayed hit pose.
    double PresentationAt=0.;
    const bool bPresentationClock=Relay&&Relay->ReadPresentationTime(PresentationAt);
    for(int32 Index=0;Index<Held.Num();)
    {
        const LDWorldFeedback::FHeldNumber& Waiting=Held[Index];
        FVector Presented;
        const bool bPresented=Frame&&Relay->ReadPresentedLocation(Waiting.Event.Handle,Waiting.Event.Epoch,Presented);
        const LDWorldFeedback::EUnitHold Hold=LDWorldFeedback::UnitHold(bPresented,bPresentationClock,PresentationAt,Waiting.Event.SampledAtServerSec,Now-Waiting.QueuedAt);
        if(Hold==LDWorldFeedback::EUnitHold::Hold){++Index;continue;}
        if(Hold==LDWorldFeedback::EUnitHold::Release){Present(Waiting.Event,Now);}
        Held.RemoveAt(Index);
    }
    // A unit's numbers vanish with its presentation (no LastSeen); every other line ends with its own local timeline.
    Floaters.RemoveAll([Now,Frame,Relay](const LDWorldFeedback::FFloater& Shown)
    {
        FVector Presented;
        return Now-Shown.BornAt>=LDHUDStyle::FloatSeconds
            ||(!Shown.Event.bOwn&&(!Frame||!Relay->ReadPresentedLocation(Shown.Event.Handle,Shown.Event.Epoch,Presented)));
    });
    Banners.RemoveAll([Now](const LDWorldFeedback::FBanner& Shown){return Now-Shown.BornAt>=LDHUDStyle::BannerSeconds;});
    if(LevelUpAt>=0.&&Now-LevelUpAt>=WorldFeedbackEffectSeconds){LevelUpAt=-1.;}
}

int32 ULDWorldFeedback::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);
    if(Floaters.IsEmpty()&&Banners.IsEmpty()&&LevelUpAt<0.){return Base;}
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());const UWorld* World=GetWorld();
    const ALDGameState* Public=World?World->GetGameState<ALDGameState>():nullptr;
    FSlateRenderer* Renderer=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetRenderer():nullptr;
    if(!PC||!Public||!Renderer){return Base;}
    const FLDOwnerState& Own=PC->OwnerState;
    // Only for the living owner of the running match and life that the shown feedback belongs to.
    if(Own.LifePhase!=ELDLifePhase::Alive||Own.Match==0||Own.Match!=Public->PublicMatch.Generation||Own.Match!=ShownMatch||Own.Life!=ShownLife){return Base;}
    int32 ViewW=0,ViewH=0;PC->GetViewportSize(ViewW,ViewH);
    if(ViewW<=0||ViewH<=0){return Base;}
    // Viewport pixels to local units: the layer fills the viewport, so this also removes the DPI scale.
    const FVector2f Size=G.GetLocalSize();const float ToLocalX=Size.X/float(ViewW),ToLocalY=Size.Y/float(ViewH);
    auto Project=[PC,ToLocalX,ToLocalY](const FVector& Point,FVector2f& Projected)
    {
        FVector2D Screen;
        if(!PC->ProjectWorldLocationToScreen(Point,Screen)){return false;}
        Projected=FVector2f(float(Screen.X)*ToLocalX,float(Screen.Y)*ToLocalY);
        return true;
    };
    auto OnScreen=[Size](const FVector2f& Spot)
    {
        return Spot.X>-WorldFeedbackCullMargin&&Spot.X<Size.X+WorldFeedbackCullMargin&&Spot.Y>-WorldFeedbackCullMargin&&Spot.Y<Size.Y+WorldFeedbackCullMargin;
    };
    const double Now=World->GetRealTimeSeconds();
    const float TextScale=PC->Controls?PC->Controls->TextScale:1.f;
    const TSharedRef<FSlateFontMeasure> FontMeasure=Renderer->GetFontMeasureService();
    // One line whose bottom edge rests on Anchor.Y; AlignX 0, .5 or 1 puts Anchor.X at its left edge, centre or right
    // edge, and Scale grows the line from that point. Returns the unscaled extent.
    auto Draw=[&](int32 Layer,const FString& Line,const FSlateFontInfo& Font,const FLinearColor& Ink,const FVector2f& Anchor,float AlignX,float Scale)
    {
        const FVector2f Extent=FontMeasure->Measure(Line,Font);
        const FVector2f Corner(Anchor.X-Extent.X*Scale*AlignX,Anchor.Y-Extent.Y*Scale);
        FSlateDrawElement::MakeText(E,Base+Layer,G.ToPaintGeometry(Extent,FSlateLayoutTransform(Scale,Corner)),Line,Font,ESlateDrawEffect::None,Ink);
        return Extent;
    };
    const ACharacter* Body=Cast<ACharacter>(PC->GetPawn());
    FVector2f NumberAt=FVector2f::ZeroVector,RewardAt=FVector2f::ZeroVector,BannerAt=FVector2f::ZeroVector;
    bool bNumbers=false,bRewards=false,bBanners=false;
    if(Body)
    {
        // Own numbers start just above the block of own bars and readiness pips, whose bottom rests on the +120 cm anchor,
        // so they clear it at every camera distance.
        const FVector Centre=Body->GetActorLocation();
        FVector2f BarsAt=FVector2f::ZeroVector;
        bNumbers=Project(Centre+FVector(0.,0.,WorldFeedbackBarsCm),BarsAt)&&OnScreen(BarsAt);
        if(bNumbers){NumberAt=FVector2f(BarsAt.X,BarsAt.Y-WorldFeedbackOwnBarsHeight-WorldFeedbackClearGap);}
        bRewards=Project(Centre+FVector(0.,0.,WorldFeedbackRewardCm),RewardAt)&&OnScreen(RewardAt);
        bBanners=bNumbers&&Project(Centre+FVector(0.,0.,WorldFeedbackBannerCm),BannerAt);
    }
    const ALDWorldViewRelay* Relay=PC->LocalView();
    const FLDObservedFrameRead Read=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
    const bool bUnits=Read.Frame&&Read.Status==ELDObservedViewStatus::Current&&Read.Frame->MatchGeneration==Own.Match&&Read.Frame->ObserverLife==Own.Life;

    // Level-up on the own body: two gold rings spread from the feet while light streaks rise around it.
    if(Body&&LevelUpAt>=0.)
    {
        const double Since=Now-LevelUpAt;
        const UCapsuleComponent* Capsule=Body->GetCapsuleComponent();
        const FVector Feet=Body->GetActorLocation()-FVector(0.,0.,Capsule?double(Capsule->GetScaledCapsuleHalfHeight()):0.);
        for(int32 Ring=0;Ring<WorldFeedbackRings;++Ring)
        {
            const double RingAge=Since-double(Ring)*WorldFeedbackRingDelay;
            if(RingAge<0.||RingAge>=WorldFeedbackRingSeconds){continue;}
            const float RingProgress=float(RingAge/WorldFeedbackRingSeconds);
            const double Radius=FMath::Lerp(WorldFeedbackRingFromCm,WorldFeedbackRingToCm,double(WorldFeedbackEaseOut(RingProgress)));
            TArray<FVector2f> Points;Points.Reserve(WorldFeedbackRingPoints+1);
            for(int32 Step=0;Step<=WorldFeedbackRingPoints;++Step)
            {
                const double Angle=UE_DOUBLE_TWO_PI*double(Step)/double(WorldFeedbackRingPoints);
                FVector2f Point;
                if(!Project(Feet+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0.),Point)){Points.Reset();break;}
                Points.Add(Point);
            }
            FLinearColor RingInk=LDHUDStyle::Celebration;RingInk.A*=.95f*(1.f-RingProgress);
            if(Points.Num()>1){FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),MoveTemp(Points),ESlateDrawEffect::None,RingInk,true,WorldFeedbackEffectThickness);}
        }
        if(Since<WorldFeedbackStreakSeconds)
        {
            const float StreakProgress=float(Since/WorldFeedbackStreakSeconds);
            const double Top=WorldFeedbackStreakTopCm*double(WorldFeedbackEaseOut(StreakProgress));
            const double Tail=FMath::Max(0.,Top-WorldFeedbackStreakLengthCm);
            FLinearColor StreakInk=FMath::Lerp(LDHUDStyle::Celebration,FLinearColor::White,.35f);StreakInk.A*=1.f-StreakProgress;
            for(int32 Index=0;Index<WorldFeedbackStreaks;++Index)
            {
                const double Angle=UE_DOUBLE_TWO_PI*(double(Index)+.5)/double(WorldFeedbackStreaks);
                const FVector StreakFoot=Feet+FVector(FMath::Cos(Angle)*WorldFeedbackStreakRadiusCm,FMath::Sin(Angle)*WorldFeedbackStreakRadiusCm,0.);
                FVector2f From,To;
                if(Project(StreakFoot+FVector(0.,0.,Tail),From)&&Project(StreakFoot+FVector(0.,0.,Top),To)&&!From.Equals(To))
                {FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),TArray<FVector2f>{From,To},ESlateDrawEffect::None,StreakInk,true,WorldFeedbackEffectThickness);}
            }
        }
    }

    // Floating numbers rise with an ease-out, pop in and fade over their last 40%. Own numbers start above the own bars;
    // a unit's start at its chest beside the body and rise into its head area, ending below its label rows; rewards
    // stack in rows beside the own body and drift up less.
    for(const LDWorldFeedback::FFloater& Floater:Floaters)
    {
        const double Age=Now-Floater.BornAt;
        if(Age<0.||Age>=LDHUDStyle::FloatSeconds){continue;}
        const bool bReward=WorldFeedbackIsReward(Floater.Event.Kind);
        FVector2f Anchor=bReward?RewardAt:NumberAt;
        // Highest screen point a unit number's top edge may reach: the bottom of the unit's label rows.
        FVector2f LabelAt=FVector2f::ZeroVector;
        if(Floater.Event.bOwn){if(!(bReward?bRewards:bNumbers)){continue;}}
        else
        {
            FVector Presented;
            if(!bUnits||!Relay->ReadPresentedLocation(Floater.Event.Handle,Floater.Event.Epoch,Presented)
                ||!Project(Presented+FVector(0.,0.,WorldFeedbackUnitNumberCm),Anchor)||!OnScreen(Anchor)
                ||!Project(Presented+FVector(0.,0.,WorldFeedbackBarsCm),LabelAt)){continue;}
        }
        const float Progress=float(Age/LDHUDStyle::FloatSeconds);
        const float Alpha=Progress<.6f?1.f:FMath::Clamp((1.f-Progress)/.4f,0.f,1.f);
        const FWorldFeedbackLook Look=WorldFeedbackLookOf(Floater.Event.Kind);
        const FSlateFontInfo Font=WorldFeedbackFont(Look.FontSize,TextScale,2,Alpha);
        FLinearColor Ink=Look.Ink;Ink.A*=Alpha;
        float AlignX=.5f;
        if(bReward)
        {
            AlignX=0.f;
            Anchor.X+=WorldFeedbackRewardGap*TextScale;
            Anchor.Y+=float(Floater.Lane)*FontMeasure->Measure(Floater.Text,Font).Y-WorldFeedbackRewardRise*TextScale*WorldFeedbackEaseOut(Progress);
        }
        else if(!Floater.Event.bOwn)
        {
            // Lane = step*2+side: the near edge sits right (side 0) or left (side 1) of the body, further out per step.
            const float Side=Floater.Lane%2==0?1.f:-1.f;
            Anchor.X+=Side*(WorldFeedbackUnitSideGap+float(Floater.Lane/2)*WorldFeedbackUnitStep*TextScale);
            AlignX=Side>0.f?0.f:1.f;
            const float Room=FMath::Max(0.f,Anchor.Y-LabelAt.Y-FontMeasure->Measure(Floater.Text,Font).Y-WorldFeedbackClearGap);
            Anchor.Y-=FMath::Min(WorldFeedbackRise*TextScale,Room)*WorldFeedbackEaseOut(Progress);
        }
        else
        {
            // Lane 0 is centred; later lanes alternate right and left with their near edge clear of a centred number.
            if(Floater.Lane>0)
            {
                const float Side=Floater.Lane%2==1?1.f:-1.f;
                Anchor.X+=Side*(WorldFeedbackLaneGap+float((Floater.Lane-1)/2)*WorldFeedbackLaneStep)*TextScale;
                AlignX=Side>0.f?0.f:1.f;
            }
            Anchor.Y-=WorldFeedbackRise*TextScale*WorldFeedbackEaseOut(Progress);
        }
        Draw(2,Floater.Text,Font,Ink,Anchor,AlignX,WorldFeedbackPop(Now-Floater.PopAt,WorldFeedbackPopSeconds,1.3f));
    }

    // Banners above the own body: level-ups stack over kill confirmations and the newest of a kind sits on top, so a
    // shown banner never moves when a later one of its kind appears. The stack starts above every rising number.
    if(bBanners&&!Banners.IsEmpty())
    {
        TArray<const LDWorldFeedback::FBanner*> Stack;
        for(const LDWorldFeedback::FBanner& Banner:Banners){Stack.Add(&Banner);}
        Stack.Sort([](const LDWorldFeedback::FBanner& First,const LDWorldFeedback::FBanner& Second)
        {
            const bool bFirstKill=First.Kind==LDWorldFeedback::EKind::Kill,bSecondKill=Second.Kind==LDWorldFeedback::EKind::Kill;
            return bFirstKill!=bSecondKill?!bFirstKill:First.BornAt>Second.BornAt;
        });
        float Bottom=FMath::Min(BannerAt.Y,NumberAt.Y-WorldFeedbackBannerClearance*TextScale);
        for(int32 Index=Stack.Num()-1;Index>=0;--Index)
        {
            const LDWorldFeedback::FBanner& Banner=*Stack[Index];
            const double Age=Now-Banner.BornAt;
            if(Age<0.||Age>=LDHUDStyle::BannerSeconds){continue;}
            const float Alpha=float(FMath::Min(FMath::Clamp(Age/WorldFeedbackBannerAppearSeconds,0.,1.),FMath::Clamp((LDHUDStyle::BannerSeconds-Age)/WorldFeedbackBannerFadeSeconds,0.,1.)));
            // A kill flashes from danger red into the celebration gold; a level-up is gold throughout.
            FLinearColor Ink=Banner.Kind==LDWorldFeedback::EKind::Kill
                ?FMath::Lerp(LDHUDStyle::TextDanger,LDHUDStyle::Celebration,float(FMath::Clamp(Age/WorldFeedbackKillTintSeconds,0.,1.))):LDHUDStyle::Celebration;
            Ink.A*=Alpha;
            if(!Banner.Subline.IsEmpty())
            {
                FLinearColor SublineInk=LDHUDStyle::TextPrimary;SublineInk.A*=Alpha;
                Bottom-=Draw(4,Banner.Subline,WorldFeedbackFont(LDHUDStyle::TypeLabel,TextScale,2,Alpha),SublineInk,FVector2f(BannerAt.X,Bottom),.5f,1.f).Y;
            }
            const FSlateFontInfo BannerFont=WorldFeedbackFont(LDHUDStyle::TypeBanner,TextScale,3,Alpha);
            Bottom-=Draw(4,Banner.Text,BannerFont,Ink,FVector2f(BannerAt.X,Bottom),.5f,WorldFeedbackPop(Age,WorldFeedbackBannerPopSeconds,1.4f)).Y+WorldFeedbackBannerGap*TextScale;
        }
    }
    // Text outlines draw one layer under their fill.
    return Base+5;
}
