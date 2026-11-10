#include "UI/LDHUDModel.h"
#include "Core/LDLog.h"
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentIds.h"
#include "Development/LDDemoCardText.h"
#include "Development/LDProgressionRules.h"
#include "Development/LDSkillGrowth.h"
#include "Combat/LDAttributeProjection.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"

bool LDHUD::FFrameDiagnostics::AddFrame(double Seconds)
{
    if (!FMath::IsFinite(Seconds) || Seconds<=0) { return false; }
    Elapsed+=Seconds;
    Peak=FMath::Max(Peak,Seconds);
    ++Count;
    if (HitchSeconds>0 && Seconds>=HitchSeconds) { ++HitchCount; }
    if (Elapsed<WindowSeconds) { return false; }
    FPS=Count/Elapsed;
    FrameMs=1000*Elapsed/Count;
    PeakMs=1000*Peak;
    Frames=Count;
    Hitches=HitchCount;
    Elapsed=Peak=0;
    Count=HitchCount=0;
    return true;
}
namespace
{
// A missing display text is a content error; report each id once instead of every frame.
void HudReportMissingText(const FString& Id)
{
    static TSet<FString> Reported;
    bool bAlready=false;Reported.Add(Id,&bAlready);
    if(!bAlready){UE_LOG(LogLDUI,Error,TEXT("LD_HUD_DISPLAY_TEXT_MISSING %s: the approved DemoContent display text is not registered; showing the stable id."),*Id);}
}
const TCHAR* HudStatusContentId(uint8 Id)
{
    switch(static_cast<ELDDemoStatusId>(Id))
    {
    case ELDDemoStatusId::Root: return TEXT("root");
    case ELDDemoStatusId::Shield: return TEXT("shield");
    case ELDDemoStatusId::CampReturning: return TEXT("camp_returning");
    case ELDDemoStatusId::RespawnProtection: return TEXT("respawn_protection");
    case ELDDemoStatusId::Slow: return TEXT("slow");
    case ELDDemoStatusId::StatBoost: return TEXT("stat_boost");
    case ELDDemoStatusId::Exposed: return TEXT("exposed");
    case ELDDemoStatusId::Taunt: return TEXT("taunt");
    case ELDDemoStatusId::Disarm: return TEXT("disarm");
    case ELDDemoStatusId::Stun: return TEXT("stun");
    case ELDDemoStatusId::Empowered: return TEXT("empowered");
    case ELDDemoStatusId::Knockback: return TEXT("knockback");
    case ELDDemoStatusId::Silence: return TEXT("silence");
    case ELDDemoStatusId::Burn: return TEXT("burn");
    case ELDDemoStatusId::AttackSlowed: return TEXT("attack_slowed");
    default: return nullptr;
    }
}
int64 HudWholePoints(int64 P2,bool bRoundUp)
{
    return bRoundUp&&P2>0?(P2+99)/100:P2>=0?P2/100:-((-P2)/100);
}
}

FString LDHUD::RegenText(const FLDOwnerState& Own,const FLDCombatOwnerView* Combat,bool bHealth)
{
    if(!Combat||Own.Match==0||Own.Life==0||Own.LifePhase!=ELDLifePhase::Alive
        ||Combat->Generation!=Own.Match||Combat->Life!=Own.Life||!Combat->bHasBody||Combat->HPP2<=0
        ||(bHealth?Combat->MaxHPP2:Combat->MaxMPP2)<=0||(!bHealth&&Combat->MPP2<0)){return FString();}
    const double Rate=bHealth?Combat->HPRegenPerSec:Combat->MPRegenPerSec;
    return FMath::IsFinite(Rate)&&Rate>=0?FString::Printf(TEXT("+%.1f/s"),Rate):FString();
}

bool LDHUD::ContentText(const FLDDemoContentDefinitions* Content,const FString& ContentId,bool bDescription,FString& Out)
{
    Out=ContentId;
    if(!Content||ContentId.IsEmpty()){return false;}
    const FLDDemoDisplayDefinition* Display=Content->Display.FindByPredicate([&ContentId](const FLDDemoDisplayDefinition& D){return D.Id==ContentId;});
    if(!Display||Display->Namespace.IsEmpty()){HudReportMissingText(ContentId);return false;}
    const FString& Key=bDescription?Display->DescriptionKey:Display->NameKey;
    const FStringTableConstPtr Table=FStringTableRegistry::Get().FindStringTable(FName(*Display->Namespace));
    FString Text;
    if(Key.IsEmpty()||!Table.IsValid()||!Table->GetSourceString(FTextKey(Key),Text)||Text.IsEmpty()){HudReportMissingText(ContentId);return false;}
    Out=Text;
    return true;
}
FString LDHUD::ContentName(const FLDDemoContentDefinitions* Content,const FString& ContentId)
{
    FString Name;ContentText(Content,ContentId,false,Name);return Name;
}
FString LDHUD::ContentIconId(const FLDDemoContentDefinitions* Content,const FString& ContentId)
{
    const FLDDemoDisplayDefinition* Display=Content&&!ContentId.IsEmpty()
        ?Content->Display.FindByPredicate([&ContentId](const FLDDemoDisplayDefinition& D){return D.Id==ContentId;}):nullptr;
    return Display?Display->IconId:FString();
}
FString LDHUD::StatusName(const FLDDemoContentDefinitions* Content,uint8 StatusId)
{
    const TCHAR* Id=HudStatusContentId(StatusId);
    return Id?ContentName(Content,Id):FString::Printf(TEXT("未知状态 #%d"),StatusId);
}
FString LDHUD::CreatureName(const FLDDemoContentDefinitions* Content,uint8 CreatureId)
{
    if(Content){if(const auto* Monster=Content->Monsters.FindByPredicate([CreatureId](const FLDDemoMonsterDefinition& M){return M.NativeId==CreatureId;})){return ContentName(Content,Monster->Id);}}
    return FString::Printf(TEXT("未知野怪 #%d"),CreatureId);
}
FString LDHUD::NextStep(const FLDPublicMatchView& Public,const FGuid& Self,const FLDProgressionView* Progression,const FString& OwnerName)
{
    if(Public.Phase!=ELDMatchPhase::Rest&&Public.Phase!=ELDMatchPhase::Development&&Public.Phase!=ELDMatchPhase::CaptureOpen){return FString();}
    // #198: a rest's locked steps (draw .. settlement, the return countdown) refuse every build command, so nothing is hinted.
    if(Public.Phase==ELDMatchPhase::Rest&&!LDMatchTimeline::IsRestBuildOpen(Public.Timeline)){return FString();}
    const bool bGrowth=Progression&&Progression->bValid&&Progression->Generation==Public.Generation;
    if(bGrowth&&Progression->bHasActiveOffer)
    {return FString::Printf(TEXT("下一步：点%s选一个技能（%s）"),LDTerms::Choice,*LDTerms::OffersQueued(Progression->QueuedActiveOpportunities));}
    if(bGrowth&&Progression->UnspentPoints>0){return FString::Printf(TEXT("下一步：点%s加点（%s）"),LDTerms::Choice,*LDTerms::UnspentPoints(Progression->UnspentPoints));}
    if(bGrowth&&!Progression->PendingBlueprints.IsEmpty()&&Progression->Actives.Num()<3)
    {return FString::Printf(TEXT("下一步：点%s把%s学到空槽"),LDTerms::Choice,LDTerms::Blueprint);}
    // In a rest only growth can be acted on.
    if(Public.Phase==ELDMatchPhase::Rest){return FString();}
    if(Public.Phase==ELDMatchPhase::CaptureOpen)
    {
        if(Public.CircleState==ELDCircleState::Solo&&Self.IsValid()&&Public.Occupier==Self){return FString(TEXT("稳住：你"))+LDTerms::Holding;}
        if(Public.GraffitiOwner.IsValid()&&Self.IsValid()&&Public.GraffitiOwner==Self){return LDTerms::GuardOwn;}
        if(Public.GraffitiOwner.IsValid()){return LDTerms::Besiege(OwnerName.IsEmpty()?FString(TEXT("未列名参赛者")):OwnerName);}
        return FString(TEXT("下一步："))+LDTerms::CaptureOpened+LDTerms::CaptureGo;
    }
    if(bGrowth&&Progression->Level<=1){return FString::Printf(TEXT("下一步：去打附近的野怪涨%s（右键攻击）"),LDTerms::Experience);}
    return FString();
}
FString LDHUD::AbilityName(const FLDDemoContentDefinitions* Content,uint8 AbilityId)
{
    if(Content){if(const auto* Ability=Content->Abilities.FindByPredicate([AbilityId](const FLDDemoAbilityDefinition& A){return A.NativeId==AbilityId;})){return ContentName(Content,Ability->Id);}}
    return FString::Printf(TEXT("未知技能 #%d"),AbilityId);
}
FString LDHUD::RosterName(const FLDLobbyView& Lobby,const FGuid& ContenderId)
{
    const FLDLobbyMember* Member=ContenderId.IsValid()?Lobby.Members.FindByPredicate([&ContenderId](const FLDLobbyMember& M){return M.ContenderId==ContenderId;}):nullptr;
    return Member?Member->Name:FString();
}
FString LDHUD::ObservedContenderName(const FLDLobbyView& Lobby,const FGuid& ContenderId,bool bInRest)
{
    const FString Name=RosterName(Lobby,ContenderId);
    return Name.IsEmpty()&&bInRest&&ContenderId.IsValid()?FString(LDTerms::TrainingDummy):Name;
}
FLinearColor LDHUD::PaletteColor(uint8 Presentation)
{
    return ULDHumanoidVisualComponent::ContenderTint(Presentation);
}

LDHUD::FResourceView LDHUD::Resource(const FString& Label,double Current,double Max,bool bRoundUp)
{
    FResourceView View;
    if(!FMath::IsFinite(Current)||!FMath::IsFinite(Max)||Max<=0||Current<0)
    {View.Text=Label+TEXT(" 同步中");return View;}
    const double Shown=bRoundUp?FMath::CeilToDouble(Current):FMath::FloorToDouble(Current);
    const double ShownMax=bRoundUp?FMath::CeilToDouble(Max):FMath::FloorToDouble(Max);
    View.bKnown=true;View.Fraction=float(FMath::Clamp(Current/Max,0.,1.));
    View.Text=FString::Printf(TEXT("%s %.0f/%.0f"),*Label,Shown,ShownMax);
    return View;
}
LDHUD::FResourceView LDHUD::ResourceP2(const FString& Label,int64 CurrentP2,int64 MaxP2,bool bRoundUp)
{
    FResourceView View;
    if(MaxP2<=0||CurrentP2<0){View.Text=Label+TEXT(" 同步中");return View;}
    View.bKnown=true;View.Fraction=float(FMath::Clamp(double(CurrentP2)/double(MaxP2),0.,1.));
    View.Text=FString::Printf(TEXT("%s %lld/%lld"),*Label,HudWholePoints(CurrentP2,bRoundUp),HudWholePoints(MaxP2,bRoundUp));
    return View;
}
LDHUD::FLevelView LDHUD::Level(int32 InLevel,int32 MaxLevel,int64 CumulativeXp,int64 LevelThresholdXp,bool bHasNext,int64 NextThresholdXp)
{
    FLevelView View;
    if(InLevel<1||MaxLevel<1||InLevel>MaxLevel||CumulativeXp<LevelThresholdXp)
    {View.Level=TEXT("等级 —");View.Experience=FString(LDTerms::Experience)+TEXT("数据异常");return View;}
    View.Level=FString::Printf(TEXT("等级 %d"),InLevel);
    if(!bHasNext||InLevel==MaxLevel)
    {View.bMax=true;View.Fraction=1.f;View.Experience=FString(LDTerms::Experience)+TEXT(" MAX");return View;}
    const int64 Span=NextThresholdXp-LevelThresholdXp;
    if(Span<=0){View.Experience=FString(LDTerms::Experience)+TEXT("数据异常");return View;}
    const int64 Progress=FMath::Clamp<int64>(CumulativeXp-LevelThresholdXp,0,Span);
    View.Fraction=float(double(Progress)/double(Span));
    View.Experience=FString::Printf(TEXT("%s %lld/%lld"),LDTerms::Experience,Progress,Span);
    return View;
}

FString LDHUD::TenthsUp(int64 RemainingMs)
{
    const int64 Tenths=RemainingMs>0?(RemainingMs+99)/100:0;
    return FString::Printf(TEXT("%lld.%llds"),Tenths/10,Tenths%10);
}
FString LDHUD::CooldownText(int64 RemainingMs)
{
    if(RemainingMs<=0){return FString();}
    // A remainder that rounds up to 10.0 already reads in whole seconds.
    const int64 Tenths=(RemainingMs+99)/100;
    return Tenths>=100?FString::Printf(TEXT("%lld"),(RemainingMs+999)/1000):FString::Printf(TEXT("%lld.%lld"),Tenths/10,Tenths%10);
}
FString LDHUD::LifeStatus(const FLDOwnerState& Own,bool bClock,int64 NowMs)
{
    switch(Own.LifePhase)
    {
    case ELDLifePhase::Alive:
        if(Own.ProtectedUntilMs<=0){return TEXT("存活");}
        if(!bClock){return FString(LDTerms::RespawnProtection)+TEXT(" · 时间同步中");}
        return Own.ProtectedUntilMs>NowMs?FString(LDTerms::RespawnProtection)+TEXT(" ")+TenthsUp(Own.ProtectedUntilMs-NowMs)+TEXT(" · ")+LDTerms::ProtectionRule:FString(TEXT("存活"));
    case ELDLifePhase::Dead:
        if(Own.RespawnAtMs<=0){return LDTerms::Dead;}
        if(!bClock){return FString(LDTerms::Dead)+TEXT(" · ")+LDTerms::RespawnWait+TEXT("（时间同步中）");}
        return FString(LDTerms::Dead)+TEXT(" · ")+LDTerms::RespawnWait+TEXT(" ")+TenthsUp(Own.RespawnAtMs-NowMs);
    case ELDLifePhase::Unspawned: return TEXT("等待出生");
    case ELDLifePhase::Left: return TEXT("已离开本局");
    default: return TEXT("生命状态未知");
    }
}
LDHUD::FDeathView LDHUD::Death(const FLDOwnerState& Own,bool bClock,int64 NowMs)
{
    FDeathView View;
    if(Own.LifePhase!=ELDLifePhase::Dead){return View;}
    View.bVisible=true;View.Title=LDTerms::DeathTitle;
    const FString Wait(LDTerms::RespawnWait);
    View.Countdown=Own.RespawnAtMs<=0?Wait+TEXT(" · 等待复活时间"):!bClock?Wait+TEXT(" · 时间同步中"):Wait+TEXT(" ")+TenthsUp(Own.RespawnAtMs-NowMs);
    View.Note=LDTerms::DeathNote();
    return View;
}
FString LDHUD::PhaseLine(ELDMatchPhase Phase)
{
    switch(Phase)
    {
    case ELDMatchPhase::Lobby: return TEXT("房间等待中");
    case ELDMatchPhase::Starting: return TEXT("对局即将开始");
    case ELDMatchPhase::Rest: return LDTerms::RestPhase;
    case ELDMatchPhase::Development: return FString(LDTerms::Development)+TEXT(" · ")+LDTerms::CaptureAfterLastRest;
    case ELDMatchPhase::CaptureOpen: return FString(LDTerms::CaptureOpened)+TEXT(" · ")+LDTerms::CaptureRule;
    case ELDMatchPhase::Results: return TEXT("对局结束");
    case ELDMatchPhase::Resetting: return TEXT("正在返回房间");
    case ELDMatchPhase::Closing: return TEXT("房间正在关闭");
    case ELDMatchPhase::Faulted: return TEXT("对局故障：权威服务已中止");
    default: return TEXT("对局阶段未知");
    }
}
FString LDHUD::Clock(int64 RemainingMs)
{
    const int64 Seconds=RemainingMs>0?(RemainingMs+999)/1000:0;
    return FString::Printf(TEXT("%lld:%02lld"),Seconds/60,Seconds%60);
}
FString LDHUD::MatchTimer(const FLDPublicMatchView& Public,bool bClock,int64 NowMs)
{
    if(!bClock||Public.StartedAtMs<=0){return FString();}
    const int64 Seconds=NowMs>Public.StartedAtMs?(NowMs-Public.StartedAtMs)/1000:0;
    return FString::Printf(TEXT("%lld:%02lld"),Seconds/60,Seconds%60);
}
FString LDHUD::RestReturnCountdown(const FLDPublicMatchView& Public,bool bClock,int64 NowMs)
{
    const FLDTimelineView& Timeline=Public.Timeline;
    if(!bClock||Public.Generation==0||Timeline.Generation!=Public.Generation){return FString();}
    if(Public.Phase==ELDMatchPhase::Rest&&Timeline.Stage==ELDMatchStage::Rest&&Timeline.Step==ELDRestStep::Return)
    {
        // The authority's field boundary may land a frame after the local clock reaches it: hold "1" until it does.
        const int64 Seconds=NowMs<Timeline.StepEndsAtMs?(Timeline.StepEndsAtMs-NowMs+999)/1000:1;
        return FString::Printf(TEXT("%lld"),Seconds);
    }
    const bool bField=(Public.Phase==ELDMatchPhase::Development||Public.Phase==ELDMatchPhase::CaptureOpen)
        &&(Timeline.Stage==ELDMatchStage::Field||Timeline.Stage==ELDMatchStage::FinalField);
    if(bField&&NowMs>=Timeline.FieldStartedAtMs&&NowMs-Timeline.FieldStartedAtMs<CountdownGoShowMs){return LDTerms::CountdownGo;}
    return FString();
}
FString LDHUD::RestStepText(ELDRestStep Step)
{
    switch(Step)
    {
    case ELDRestStep::Build: return LDTerms::RestBuild;
    case ELDRestStep::Draw: return LDTerms::RestDraw;
    case ELDRestStep::Betting: return LDTerms::RestBetting;
    case ELDRestStep::PKReady: return LDTerms::RestPKReady;
    case ELDRestStep::PK: return LDTerms::RestPK;
    case ELDRestStep::Settlement: return LDTerms::RestSettlement;
    case ELDRestStep::Payout: return LDTerms::RestPayout;
    case ELDRestStep::Return: return LDTerms::RestReturn;
    default: return FString();
    }
}
LDHUD::FTimelineView LDHUD::TimelineBanner(const FLDPublicMatchView& Public,const FGuid& Self,bool bClock,int64 NowMs)
{
    FTimelineView View;
    const FLDTimelineView& Timeline=Public.Timeline;
    if(Public.Generation==0||Timeline.Generation!=Public.Generation){return View;}
    // Whole seconds rounded up, so a running step never reads 0 while it lasts.
    const auto Seconds=[](int64 Ms){return Ms>0?(Ms+999)/1000:int64(0);};
    switch(Timeline.Stage)
    {
    case ELDMatchStage::Field:
    {
        const FString Left=bClock?Clock(Timeline.FieldEndT-LDMatchTimeline::FieldNowFromView(Timeline,NowMs)):FString(TEXT("同步中"));
        View.Headline=LDTerms::FieldSegmentLine(int32(Timeline.FieldSegment)+1,int32(FLDTimelineParameters::TimedSegments),Left);
        return View;
    }
    case ELDMatchStage::FinalField:
        View.Headline=LDTerms::FinalField;
        return View;
    case ELDMatchStage::Rest:
        break;
    default:
        return View;
    }
    View.Headline=LDTerms::RestLabel(Timeline.RestIndex)+TEXT(" · ")+RestStepText(Timeline.Step)+TEXT(" · ")
        +(!bClock?FString(TEXT("时间同步中"))
        :FString::Printf(TEXT("%s %lld 秒"),Timeline.Step==ELDRestStep::PK?TEXT("最长剩"):TEXT("剩"),Seconds(Timeline.StepEndsAtMs-NowMs)));
    TArray<FString> Detail;
    if(Timeline.Step==ELDRestStep::Build)
    {
        View.bSignedUp=Self.IsValid()&&Timeline.Signups.Contains(Self);
        View.SignupAction=View.bSignedUp?FString(LDTerms::SignupWithdraw):FString(LDTerms::SignupJoin);
        if(!bClock){Detail.Add(TEXT("PK 报名时间同步中"));}
        else if(NowMs<Timeline.SignupOpensAtMs){Detail.Add(LDTerms::SignupOpensIn(Seconds(Timeline.SignupOpensAtMs-NowMs)));}
        else if(NowMs<=Timeline.StepEndsAtMs){View.bSignupOpen=true;Detail.Add(LDTerms::SignupOpenFor(Seconds(Timeline.StepEndsAtMs-NowMs)));}
        else{Detail.Add(TEXT("PK 报名已截止"));}
        if(View.bSignupOpen||!Timeline.Signups.IsEmpty())
        {
            Detail.Add(LDTerms::SignedUpCount(Timeline.Signups.Num()));
            Detail.Add(View.bSignedUp?FString(LDTerms::SignedUpSelf):FString(LDTerms::NotSignedUpSelf));
        }
    }
    else
    {
        Detail.Add(Timeline.bPKSkipped?FString(LDTerms::PKSkipped):LDTerms::PKEntrants(Timeline.LockedEntrants));
        if(!LDMatchTimeline::IsRestBuildOpen(Timeline)){Detail.Add(LDTerms::BuildLockedNow);}
    }
    View.Detail=FString::Join(Detail,TEXT(" · "));
    return View;
}
FString LDHUD::RestSignupText(ELDRestSignupResult Result)
{
    switch(Result)
    {
    case ELDRestSignupResult::Joined: return TEXT("已报名 PK");
    case ELDRestSignupResult::Withdrawn: return TEXT("已取消 PK 报名");
    case ELDRestSignupResult::WindowClosed: return TEXT("PK 报名没有开放，本次没有改动");
    case ELDRestSignupResult::NotContender: return TEXT("不是本局参赛者，不能报名 PK");
    case ELDRestSignupResult::StaleMatch: return TEXT("已不是当前对局，没有报名");
    case ELDRestSignupResult::NoPKProvider: return LDTerms::NoPKProvider;
    case ELDRestSignupResult::NotInRest: return TEXT("现在不在休息中，不能报名 PK");
    default: return FString::Printf(TEXT("PK 报名结果未知 #%d"),int32(Result));
    }
}
FString LDHUD::TrainingResetText(ELDTrainingResetResult Result)
{
    switch(Result)
    {
    case ELDTrainingResetResult::Queued: return TEXT("训练已重置：你和假人回满，冷却就绪");
    case ELDTrainingResetResult::NotInRest: return TEXT("现在不在休息中，不能重置训练");
    case ELDTrainingResetResult::NotContender: return TEXT("不是本局参赛者，不能重置训练");
    case ELDTrainingResetResult::StaleMatch: return TEXT("已不是当前对局，没有重置训练");
    case ELDTrainingResetResult::NoDummy: return FString(TEXT("没有"))+LDTerms::TrainingDummy+TEXT("（休息位被占）");
    default: return FString::Printf(TEXT("重置训练结果未知 #%d"),int32(Result));
    }
}
LDHUD::FCircleView LDHUD::Circle(const FLDPublicMatchView& Public,const FString& SprayerName,const FString& OwnerName,bool bClock,int64 NowMs,
    int64 SprayMs,int64 GuardMs)
{
    FCircleView View;
    switch(Public.Phase)
    {
    case ELDMatchPhase::Development:
        // #198: the opening is scheduled only when the final field begins (OpenAtMs is unscheduled until then).
        View.Line=FString(LDTerms::Development)+TEXT(" · ")+LDTerms::CaptureAfterLastRest;
        return View;
    case ELDMatchPhase::CaptureOpen:
    {
        const FString Sprayer=SprayerName.IsEmpty()?FString(TEXT("未列名参赛者")):SprayerName;
        // The graffiti and its guard are public in every open state; the guard runs only while its owner is alone in
        // the zone: anyone else in the zone freezes it and an empty zone pauses it.
        FString Guard;
        float GuardProgress=0.f;
        bool bGuardProgress=false;
        if(Public.GraffitiOwner.IsValid())
        {
            Guard=LDTerms::GraffitiOf(OwnerName.IsEmpty()?FString(TEXT("未列名参赛者")):OwnerName);
            if(Public.CircleState!=ELDCircleState::Guarded)
            {
                Guard+=FString(TEXT(" · "))+(Public.CircleState==ELDCircleState::Empty?LDTerms::GuardAway:LDTerms::GuardPaused);
                if(GuardMs>0)
                {
                    Guard+=FString(TEXT("（"))+LDTerms::GuardLeft+TEXT(" ")+TenthsUp(GuardMs-Public.GuardKeptMs)+TEXT("）");
                    bGuardProgress=true;GuardProgress=float(FMath::Clamp(double(Public.GuardKeptMs)/double(GuardMs),0.,1.));
                }
            }
            else if(!bClock){Guard+=TEXT(" · 守护同步中");}
            else
            {
                Guard+=FString(TEXT(" · "))+LDTerms::GuardLeft+TEXT(" ")+TenthsUp(Public.GuardDueAtMs-NowMs);
                if(GuardMs>0){bGuardProgress=true;GuardProgress=float(FMath::Clamp(1.-double(Public.GuardDueAtMs-NowMs)/double(GuardMs),0.,1.));}
            }
        }
        const auto WithGuard=[&Guard](const FString& Spray){return Guard.IsEmpty()?Spray:Spray.IsEmpty()?Guard:Spray+TEXT(" · ")+Guard;};
        switch(Public.CircleState)
        {
        case ELDCircleState::Empty:
            View.Line=Guard.IsEmpty()?FString(LDTerms::CircleEmpty):Guard;
            View.bProgress=bGuardProgress;View.Progress=GuardProgress;
            return View;
        case ELDCircleState::Guarded:
            View.Line=Guard;
            View.bProgress=bGuardProgress;View.Progress=GuardProgress;
            return View;
        case ELDCircleState::Contested:
        {
            // A contest pauses the holder's spray progress instead of clearing it; without a holder nothing is kept.
            if(!Public.Occupier.IsValid()||Public.PausedProgressMs<=0)
            {
                View.Line=WithGuard(FString(LDTerms::CircleContested)+TEXT(" · ")+LDTerms::NoProgressKept);
                View.bProgress=bGuardProgress;View.Progress=GuardProgress;
                return View;
            }
            const FString Kept=FString::Printf(TEXT("%.1f"),double(Public.PausedProgressMs)/1000.);
            if(SprayMs<=0){View.Line=WithGuard(FString(LDTerms::CircleContested)+TEXT(" · ")+LDTerms::ProgressKept(Sprayer)+TEXT("（")+Kept+TEXT(" s）"));return View;}
            View.bProgress=true;View.Progress=float(FMath::Clamp(double(Public.PausedProgressMs)/double(SprayMs),0.,1.));
            View.Line=WithGuard(FString(LDTerms::CircleContested)+TEXT(" · ")+LDTerms::ProgressKept(Sprayer)+TEXT(" ")+Kept+FString::Printf(TEXT("/%.1f s"),double(SprayMs)/1000.));
            return View;
        }
        case ELDCircleState::Solo:
        {
            const int64 Span=Public.DueAtMs-Public.SoloSinceMs;
            if(!bClock||Span<=0){View.Line=WithGuard(Sprayer+TEXT(" ")+LDTerms::Holding+TEXT(" · 进度同步中"));return View;}
            View.bProgress=true;View.Progress=float(FMath::Clamp(double(NowMs-Public.SoloSinceMs)/double(Span),0.,1.));
            View.Line=WithGuard(Sprayer+TEXT(" ")+LDTerms::Holding+TEXT(" · ")+LDTerms::HoldLeft+TEXT(" ")+TenthsUp(Public.DueAtMs-NowMs));
            return View;
        }
        default: View.Line=FString(LDTerms::CapturePoint)+TEXT("状态同步中");return View;
        }
    }
    default:
        View.Line=PhaseLine(Public.Phase);
        return View;
    }
}
FString LDHUD::AirdropLine(const FLDAirdropView& Drop,bool bClock,int64 FieldNowMs)
{
    switch(Drop.Phase)
    {
    case ELDAirdropPhase::Announced:
        return bClock?LDTerms::AirdropCountdown(Clock(Drop.LandAtMs-FieldNowMs)):LDTerms::AirdropSoon();
    case ELDAirdropPhase::Landed: return LDTerms::AirdropLanded;
    default: return FString();
    }
}
FString LDHUD::BossLine(const FLDBossView& Boss,const FString& BossName,bool bClock,int64 FieldNowMs)
{
    // Presentation only: how early the countdown shows.
    constexpr int64 CountdownMs=15000;
    const FString Name=BossName.IsEmpty()?FString(LDTerms::Boss):BossName;
    switch(Boss.Phase)
    {
    case ELDBossPhase::Pending:
        if(!bClock||Boss.AppearAtMs-FieldNowMs>CountdownMs){return FString();}
        return Boss.AppearAtMs>FieldNowMs?LDTerms::BossCountdown(Name,Clock(Boss.AppearAtMs-FieldNowMs)):LDTerms::BossSoon(Name);
    case ELDBossPhase::Alive: return LDTerms::BossPatrol(Name);
    default: return FString();
    }
}
FString LDHUD::BossDownLine(const FLDBossView& Boss,const FGuid& Self,const FString& KillerName,const FString& BossName)
{
    const FString Name=BossName.IsEmpty()?FString(LDTerms::Boss):BossName;
    if(Self.IsValid()&&Boss.KilledBy==Self){return LDTerms::BossDownSelf(Name);}
    if(!Boss.KilledBy.IsValid()){return LDTerms::BossDown(Name);}
    return LDTerms::BossDownBy(KillerName.IsEmpty()?FString(TEXT("未列名参赛者")):KillerName,Name);
}
void LDHUD::FEventFeed::Observe(const FEventFeedInput& In)
{
    const double Now=In.LocalSeconds;
    const uint32 Match=In.Public?In.Public->Generation:0;
    // A new match (or none) starts an empty feed whose first observation only records the state.
    if(Match!=Generation){Generation=Match;bPrimed=false;KillSerial=0;Shown.Reset();}
    auto Add=[this,Now](const FString& Text,bool bSelf,uint64 ClaimSerial=0)
    {
        if(Text.IsEmpty()){return;}
        FEventLine& Line=Shown.AddDefaulted_GetRef();Line.Text=Text;Line.bSelf=bSelf;Line.At=Now;Line.AirdropClaimSerial=ClaimSerial;
    };
    auto Name=[&In](const FGuid& Id){return In.Lobby?RosterName(*In.Lobby,Id):FString();};
    if(Match!=0)
    {
        const FLDPublicMatchView& Public=*In.Public;
        if(In.Kills&&In.Kills->Generation==Match)
        {
            TArray<const FLDKillNotice*> Fresh;
            for(const FLDKillNotice& Notice:In.Kills->Notices){if(Notice.Serial>KillSerial){Fresh.Add(&Notice);}}
            Fresh.Sort([](const FLDKillNotice& A,const FLDKillNotice& B){return A.Serial<B.Serial;});
            for(const FLDKillNotice* Notice:Fresh)
            {
                KillSerial=Notice->Serial;
                // Notices already old when first seen (e.g. a late join) are marked seen without a line.
                if(In.bClock&&In.NowMs-Notice->OccurredAtMs>StaleKillMs){continue;}
                TArray<FString> Assisters;
                for(const FGuid& Assister:Notice->Assisters){Assisters.Add(Name(Assister));}
                const FKillFeedLineView Line=KillFeedLine(*Notice,In.Self,Name(Notice->Victim),Name(Notice->Killer),In.LowCampName,In.HighCampName,In.EliteCampName,In.BossName,Assisters);
                Add(Line.Text,Line.bSelf);
            }
        }
        if(bPrimed)
        {
            if(Public.Phase!=Phase&&Public.Phase==ELDMatchPhase::CaptureOpen){Add(LDTerms::CaptureOpened,false);}
            if(Public.Airdrop.Serial!=AirdropSerial||Public.Airdrop.Phase!=AirdropPhase)
            {
                if(Public.Airdrop.Phase==ELDAirdropPhase::Landed){Add(LDTerms::AirdropLanded,false);}
                // Labelled below from the own claim serial; the public view never says who took it.
                else if(Public.Airdrop.Phase==ELDAirdropPhase::Claimed){Add(LDTerms::AirdropTaken(),false,Public.Airdrop.Serial);}
            }
            if(Public.Boss.Phase!=BossPhase)
            {
                const FString Boss=In.BossName.IsEmpty()?FString(LDTerms::Boss):In.BossName;
                if(Public.Boss.Phase==ELDBossPhase::Alive){Add(LDTerms::BossAppeared(Boss),false);}
                else if(Public.Boss.Phase==ELDBossPhase::Down)
                {Add(BossDownLine(Public.Boss,In.Self,Name(Public.Boss.KilledBy),Boss),In.Self.IsValid()&&Public.Boss.KilledBy==In.Self);}
            }
            if(Public.Leader.Leader.IsValid()&&Public.Leader.Leader!=Leader)
            {Add(LeaderLine(Public.Leader,In.Self,Name(Public.Leader.Leader)),In.Self.IsValid()&&Public.Leader.Leader==In.Self);}
            if(Public.GraffitiOwner.IsValid()&&Public.GraffitiOwner!=GraffitiOwner)
            {
                const bool bOwn=In.Self.IsValid()&&Public.GraffitiOwner==In.Self;
                const FString Owner=bOwn?FString(TEXT("你")):Name(Public.GraffitiOwner);
                Add(LDTerms::GraffitiSprayed(Owner.IsEmpty()?FString(TEXT("未列名参赛者")):Owner),bOwn);
            }
            // A kept guard only drops to zero when its owner leaves the zone under a challenger; a guard that was
            // running cannot pause or freeze at zero for the same owner, since the spray that started it needs the zone alone.
            else if(Public.GraffitiOwner.IsValid()&&!Public.GuardDueAtMs&&!Public.GuardKeptMs&&(GuardKeptMs>0||GuardDueAtMs))
            {Add(LDTerms::GuardReset,In.Self.IsValid()&&Public.GraffitiOwner==In.Self);}
        }
        bPrimed=true;Phase=Public.Phase;AirdropSerial=Public.Airdrop.Serial;AirdropPhase=Public.Airdrop.Phase;BossPhase=Public.Boss.Phase;
        Leader=Public.Leader.Leader;GraffitiOwner=Public.GraffitiOwner;GuardKeptMs=Public.GuardKeptMs;GuardDueAtMs=Public.GuardDueAtMs;
    }
    for(FEventLine& Line:Shown)
    {
        if(Line.AirdropClaimSerial==0){continue;}
        const bool bOwn=In.OwnAirdropClaimSerial==Line.AirdropClaimSerial;
        Line.Text=bOwn?LDTerms::AirdropOwnClaim():LDTerms::AirdropTaken();Line.bSelf=bOwn;
    }
    Shown.RemoveAll([Now](const FEventLine& Line){return Now-Line.At>=LineSeconds;});
    if(Shown.Num()>MaxLines){Shown.RemoveAt(0,Shown.Num()-MaxLines);}
}
FString LDHUD::LeaderLine(const FLDLeaderView& Notice,const FGuid& Self,const FString& LeaderName)
{
    if(!Notice.Leader.IsValid()){return FString();}
    if(Self.IsValid()&&Notice.Leader==Self){return LDTerms::LeaderSelf();}
    return LDTerms::LeaderNamed(LeaderName.IsEmpty()?FString(TEXT("未列名参赛者")):LeaderName);
}
FString LDHUD::KillLine(const FLDKillNotice& Notice,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName,const FString& EliteCampName,const FString& BossName,const TArray<FString>& AssisterNames)
{
    const FString Victim=VictimName.IsEmpty()?FString(TEXT("未列名参赛者")):VictimName;
    switch(Notice.Cause)
    {
    case ELDKillCause::Contender:
    {
        TArray<FString> Assisters;
        for(const FString& Name:AssisterNames){Assisters.Add(Name.IsEmpty()?FString(TEXT("未列名参赛者")):Name);}
        return LDTerms::KillLine(KillerName.IsEmpty()?FString(TEXT("未列名参赛者")):KillerName,Victim)+LDTerms::AssistSuffix(Assisters);
    }
    case ELDKillCause::CampLow: return LDTerms::KilledByLine(Victim,LowCampName);
    case ELDKillCause::CampHigh: return LDTerms::KilledByLine(Victim,HighCampName);
    case ELDKillCause::CampElite: return LDTerms::KilledByLine(Victim,EliteCampName);
    case ELDKillCause::Boss: return LDTerms::KilledByLine(Victim,BossName);
    case ELDKillCause::Unowned: return Victim+TEXT(" ")+LDTerms::Dead+TEXT("（无参赛者击杀）");
    default: return Victim+TEXT(" ")+LDTerms::Dead+TEXT("（死因未知）");
    }
}
LDHUD::FKillFeedLineView LDHUD::KillFeedLine(const FLDKillNotice& Notice,const FGuid& Self,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName,const FString& EliteCampName,const FString& BossName,const TArray<FString>& AssisterNames)
{
    FKillFeedLineView Line;
    const bool bVictim=Self.IsValid()&&Notice.Victim==Self;
    const bool bKiller=Self.IsValid()&&Notice.Cause==ELDKillCause::Contender&&Notice.Killer==Self;
    TArray<FString> Assisters=AssisterNames;
    bool bAssister=false;
    for(int32 Index=0;Index<Assisters.Num()&&Index<Notice.Assisters.Num();++Index)
    {if(Self.IsValid()&&Notice.Assisters[Index]==Self){Assisters[Index]=TEXT("你");bAssister=true;}}
    Line.bSelf=bVictim||bKiller||bAssister;
    Line.Text=KillLine(Notice,bVictim?FString(TEXT("你")):VictimName,bKiller?FString(TEXT("你")):KillerName,LowCampName,HighCampName,EliteCampName,BossName,Assisters);
    return Line;
}
FString LDHUD::DeathCause(const FLDKillFeedView& Feed,uint32 Generation,const FGuid& Self,int64 NotBeforeMs,const FLDLobbyView& Lobby,const FString& LowCampName,const FString& HighCampName,const FString& EliteCampName,const FString& BossName)
{
    if(Generation==0||Feed.Generation!=Generation||!Self.IsValid()){return FString();}
    const FLDKillNotice* Latest=nullptr;
    for(const FLDKillNotice& Notice:Feed.Notices)
    {if(Notice.Victim==Self&&Notice.OccurredAtMs>=NotBeforeMs&&(!Latest||Notice.Serial>Latest->Serial)){Latest=&Notice;}}
    if(!Latest){return FString();}
    switch(Latest->Cause)
    {
    case ELDKillCause::Contender:
    {
        const FString Killer=RosterName(Lobby,Latest->Killer);
        return LDTerms::KilledBy(Killer.IsEmpty()?FString(TEXT("未列名参赛者")):Killer);
    }
    case ELDKillCause::CampLow: return LDTerms::KilledBy(LowCampName);
    case ELDKillCause::CampHigh: return LDTerms::KilledBy(HighCampName);
    case ELDKillCause::CampElite: return LDTerms::KilledBy(EliteCampName);
    case ELDKillCause::Boss: return LDTerms::KilledBy(BossName);
    case ELDKillCause::Unowned: return TEXT("无参赛者击杀");
    default: return TEXT("死因未知");
    }
}
FString LDHUD::ResultHeadline(const FLDResultView& Result,const FString& WinnerName)
{
    switch(Result.Reason)
    {
    case ELDMatchEndReason::GuardSucceeded:
        return Result.Winner.IsValid()?FString(TEXT("守护成功 · "))+LDTerms::Crowned(WinnerName.IsEmpty()?FString(TEXT("未列名参赛者")):WinnerName):FString(LDTerms::NoWinner);
    case ELDMatchEndReason::HostEnded: return FString(TEXT("房主结束了本局 · "))+LDTerms::NoCapture;
    default: return TEXT("对局结束 · 原因未知");
    }
}
TArray<LDHUD::FScoreLine> LDHUD::Scoreboard(const FLDKillFeedView& Feed,uint32 Generation,const FLDLobbyView& Lobby,const FGuid& Self)
{
    TArray<FScoreLine> Lines;
    if(!Generation||Feed.Generation!=Generation){return Lines;}
    for(const FLDScoreRow& Row:Feed.Scores)
    {
        FScoreLine& Line=Lines.AddDefaulted_GetRef();
        const FLDLobbyMember* Member=Lobby.Members.FindByPredicate([&Row](const FLDLobbyMember& M){return M.ContenderId==Row.ContenderId;});
        Line.Name=!Member?FString(TEXT("（已离开）")):Member->Name.IsEmpty()?FString(TEXT("未列名参赛者")):Member->Name;
        if(Member&&Member->Kind==ELDContenderKind::OpponentAI){Line.Name+=TEXT("（AI）");}
        Line.bSelf=Self.IsValid()&&Row.ContenderId==Self;
        if(Line.bSelf){Line.Name+=TEXT("（你）");}
        Line.Kills=Row.Kills;Line.Deaths=Row.Deaths;Line.Assists=Row.Assists;
    }
    Lines.StableSort([](const FScoreLine& A,const FScoreLine& B){return A.Kills!=B.Kills?A.Kills>B.Kills:A.Deaths<B.Deaths;});
    return Lines;
}
LDHUD::FResultRowView LDHUD::ResultRow(const FLDResultRow& Row,const FGuid& Winner,const FGuid& Self)
{
    FResultRowView View;
    View.Name=Row.Name.IsEmpty()?FString(TEXT("未列名参赛者")):Row.Name;
    if(Row.Kind==ELDContenderKind::OpponentAI){View.Name+=TEXT("（AI）");}
    if(Self.IsValid()&&Row.ContenderId==Self){View.Name+=TEXT("（你）");}
    if(Row.bLeft){View.Name+=TEXT("（已离开）");}
    View.bWinner=Winner.IsValid()&&Row.ContenderId==Winner;
    View.Kills=FString::Printf(TEXT("%llu"),Row.Kills);
    View.Deaths=FString::Printf(TEXT("%llu"),Row.Deaths);
    View.Assists=FString::Printf(TEXT("%llu"),Row.Assists);
    View.Level=FString::Printf(TEXT("%d"),Row.FinalLevel);
    return View;
}

LDHUD::FActionView LDHUD::Action(const FActionInput& Input,const FActionContext& Context)
{
    FActionView View;
    View.Slot=Input.Slot;View.Key=Context.KeyLabel;
    const bool bLearned=Input.Slot==EActionSlot::Q||Input.Slot==EActionSlot::W||Input.Slot==EActionSlot::E;
    View.Kind=bLearned?TEXT("角色主动"):TEXT("通用冲刺");
    View.Name=!Input.bHasSource?(bLearned?FString(TEXT("空槽")):FString(LDTerms::Dash)+TEXT("不可用")):Input.Name;
    // Only a current own context may send; a predicted execution failure never suppresses the intent itself.
    View.bCanSubmitIntent=Context.bOwnContext&&Input.bProjected&&Context.bPlaying&&Context.bAlive&&!Context.bInputCaptured;
    FString State;
    auto Block=[&View,&State](uint32 Flag,const FString& Text){View.ExecutionBlocks|=Flag;if(State.IsEmpty()){State=Text;}};
    if(!Context.bOwnContext||!Input.bProjected){Block(ActionUnsynced,TEXT("未同步"));}
    if(!Context.bPlaying){Block(ActionNotPlaying,TEXT("对局未进行"));}
    else if(!Context.bAlive){Block(ActionDead,LDTerms::Dead);}
    if(Input.bProjected&&!Input.bHasSource){Block(ActionEmpty,bLearned?TEXT("空槽"):FString(LDTerms::Dash)+TEXT("不可用"));}
    const bool bActive=Input.bProjected&&Input.bHasSource;
    if(bActive&&Input.ReadyAtMs>0)
    {
        if(!Context.bClock){Block(ActionUnsynced,TEXT("冷却时间同步中"));}
        else if(Input.ReadyAtMs>Context.NowMs)
        {
            View.CooldownRemainingMs=Input.ReadyAtMs-Context.NowMs;
            View.CooldownFraction=Input.CooldownMs>0?float(FMath::Clamp(double(View.CooldownRemainingMs)/double(Input.CooldownMs),0.,1.)):1.f;
            Block(ActionCooldown,TEXT("冷却 ")+TenthsUp(View.CooldownRemainingMs));
        }
    }
    if(bActive&&Input.bControlled){Block(ActionControlled,TEXT("受控制无法使用"));}
    if(bActive&&Context.bManaKnown&&Input.ManaCostP2>Context.ManaP2){Block(ActionMana,LDTerms::NotEnoughMana());}
    if(Input.bProjected&&!Input.ServerBlock.IsEmpty()){Block(ActionServer,Input.ServerBlock);}
    View.State=State.IsEmpty()?FString(TEXT("就绪")):State;
    View.Countdown=CooldownText(View.CooldownRemainingMs);
    if(bActive&&Input.ManaCostP2>0){View.Cost=FString::Printf(TEXT("%lld"),HudWholePoints(Input.ManaCostP2,false));}
    if(Input.bHasSource&&!Input.Name.IsEmpty()){View.Glyph=Input.Name.Left(1);}
    if(Input.bHasSource){View.IconId=Input.IconId;}
    return View;
}
LDHUD::FActionDetailView LDHUD::ActionDetail(const FActionView& View,const FActionInput& Input,const FActionContext& Context,const FLDDemoContentDefinitions* Content)
{
    FActionDetailView Detail;
    Detail.Name=View.Name;
    Detail.Summary=View.Key.IsEmpty()?FString::Printf(TEXT("%s · %s"),*View.Kind,*View.State)
        :FString::Printf(TEXT("%s [%s] · %s"),*View.Kind,*View.Key,*View.State);
    const bool bLearned=Input.Slot==EActionSlot::Q||Input.Slot==EActionSlot::W||Input.Slot==EActionSlot::E;
    const bool bDash=Input.Slot==EActionSlot::Dash;
    TArray<FString> Lines;
    if(bDash&&Input.bHasSource)
    {Lines.Add(FString::Printf(TEXT("朝方向%s出 %dcm，碰到阻挡就停下；%s时不能%s"),LDTerms::Dash,Input.RangeCm,LDTerms::Root,LDTerms::Dash));}
    else if(bLearned&&Input.bHasSource)
    {
        // #147/#198: the same card as the growth panel, with this level's numbers and stats.
        const FActiveCardView Card=ActiveCard(Input.DefinitionId,Content,Context.Card);
        if(Card.bKnown)
        {
            Lines.Add(Card.SentencePlain);
            Lines.Add(FString());
            Lines.Add(Card.Target);
            Lines.Add(Card.Cost);
        }
        else if(Content){Lines.Add(Card.SentencePlain);}
    }
    if(Context.bInputCaptured){Lines.Add(TEXT("当前输入暂不可用：界面占用或按键尚未松开"));}
    Detail.Body=FString::Join(Lines,TEXT("\n"));
    return Detail;
}
LDHUD::EActionLook LDHUD::ActionLook(const FActionView& View,bool bArmed)
{
    const uint32 Blocks=View.ExecutionBlocks;
    if(Blocks&(ActionUnsynced|ActionNotPlaying|ActionDead)){return EActionLook::Blocked;}
    if(Blocks&ActionEmpty){return EActionLook::Empty;}
    if(bArmed){return EActionLook::Armed;}
    if(Blocks&ActionControlled){return EActionLook::Controlled;}
    if(Blocks&ActionCooldown){return EActionLook::Cooling;}
    if(Blocks&ActionMana){return EActionLook::NoMana;}
    if(Blocks&ActionServer){return EActionLook::Blocked;}
    return EActionLook::Ready;
}

LDHUD::FObservedCard LDHUD::ObservedCard(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec)
{
    FObservedCard Card;
    const bool bContender=Unit.Kind==ELDObservedUnitKind::Contender;
    if(bContender){Card.Title=FString::Printf(TEXT("%s · 等级 %d"),ContenderName.IsEmpty()?TEXT("未列名参赛者"):*ContenderName,Unit.Level);}
    else{Card.Title=CreatureName(Content,Unit.CreatureId);}
    Card.Health=ResourceP2(LDTerms::Health,Unit.Health.CurrentP2,Unit.Health.MaxP2,true);
    Card.bMana=(Unit.AuthorizedFieldMask&LDObservation::Mana)!=0;
    if(Card.bMana){Card.Mana=ResourceP2(LDTerms::Mana,Unit.Mana.CurrentP2,Unit.Mana.MaxP2,false);}
    FString Statuses;
    for(const FLDObservedStatus& Status:Unit.Statuses)
    {
        Statuses+=(Statuses.IsEmpty()?TEXT(""):TEXT(" · "))+StatusName(Content,Status.StatusId);
        if(Status.Stacks>1){Statuses+=FString::Printf(TEXT(" ×%d"),Status.Stacks);}
        if(Status.ExpiresAtServerMs>0){Statuses+=TEXT(" ")+TenthsUp(int64(FMath::RoundToDouble(double(Status.ExpiresAtServerMs)-SampledAtServerSec*1000.)));}
    }
    Card.Statuses=TEXT("状态：")+(Statuses.IsEmpty()?FString(TEXT("无公开状态")):Statuses);
    Card.bActives=bContender&&(Unit.AuthorizedFieldMask&LDObservation::Actives)!=0;
    for(int32 Index=0;Card.bActives&&Index<3;++Index)
    {
        FObservedCard::FActiveChip& Chip=Card.Actives[Index];
        Chip.Key=SlotKey(uint8(Index));
        const uint8 Id=Unit.Actives.Abilities[Index];
        Chip.bEmpty=Id==0;
        Chip.Name=Chip.bEmpty?FString(TEXT("空槽")):AbilityName(Content,Id);
    }
    return Card;
}
FString LDHUD::ObservedUnit(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec)
{
    const FObservedCard Card=ObservedCard(Unit,ContenderName,Content,SampledAtServerSec);
    FString Text=Card.Title+TEXT("\n")+Card.Health.Text;
    if(Card.bMana){Text+=TEXT(" · ")+Card.Mana.Text;}
    Text+=TEXT("\n")+Card.Statuses;
    if(Card.bActives)
    {
        Text+=TEXT("\n技能：");
        for(int32 Index=0;Index<3;++Index)
        {
            const FObservedCard::FActiveChip& Chip=Card.Actives[Index];
            Text+=(Index?TEXT(" · "):TEXT(""))+Chip.Key+TEXT(" ")+Chip.Name;
        }
    }
    return Text;
}

namespace
{
// Two-decimal text without trailing zeros; formatting only, never fed back into a rule.
FString HudDecimal(double Value)
{
    FString Text=FString::Printf(TEXT("%.2f"),Value);
    while(Text.EndsWith(TEXT("0"))){Text.LeftChopInline(1);}
    if(Text.EndsWith(TEXT("."))){Text.LeftChopInline(1);}
    return Text==TEXT("-0")?FString(TEXT("0")):Text;
}
FString HudPoints(int64 P2){return HudDecimal(double(P2)/100.);}
FString HudPercent(int64 Bp){return HudDecimal(double(Bp)/100.)+TEXT("%");}
FString HudSeconds(int64 Ms){return HudDecimal(double(Ms)/1000.)+TEXT("s");}
FString HudRational(const FLDDemoRational& Value){return Value.Denominator>0?HudDecimal(double(Value.Numerator)/double(Value.Denominator)):FString(TEXT("?"));}
FString HudBonusStatName(ELDDemoBonusStat Stat)
{
    switch(Stat)
    {
    case ELDDemoBonusStat::ADFlat: return LDTerms::AttackDamage;
    case ELDDemoBonusStat::APFlat: return LDTerms::AbilityPower;
    case ELDDemoBonusStat::MaxHPFlat: return LDTerms::MaxHealth();
    case ELDDemoBonusStat::MaxMPFlat: return LDTerms::MaxMana();
    case ELDDemoBonusStat::ArmorFlat: return LDTerms::Armor;
    case ELDDemoBonusStat::MagicResistFlat: return LDTerms::MagicResist;
    case ELDDemoBonusStat::CooldownHasteFlat: return LDTerms::Haste;
    case ELDDemoBonusStat::AttackRangeCmFlat: return TEXT("攻击距离");
    case ELDDemoBonusStat::AttackSpeedBonusRatio: return LDTerms::AttackSpeed;
    case ELDDemoBonusStat::MoveSpeedBonusRatio: return LDTerms::MoveSpeed;
    case ELDDemoBonusStat::CritChanceBonusRatio: return LDTerms::CritChance();
    case ELDDemoBonusStat::ADBonusRatio: return LDTerms::AttackDamage;
    case ELDDemoBonusStat::HPRegenFlat: return LDTerms::HealthRegen();
    case ELDDemoBonusStat::APBonusRatio: return LDTerms::AbilityPower;
    case ELDDemoBonusStat::MPRegenFlat: return LDTerms::ManaRegen();
    default: return FString();
    }
}
FString HudDamageTerm(const FLDDemoEffectDefinition& Effect)
{
    return Effect.DamageType==ELDDemoDamageType::Magical?LDTerms::MagicalDamage:LDTerms::PhysicalDamage;
}
FString HudScaling(const FLDDemoEffectDefinition& Effect)
{
    FString Text=HudPoints(Effect.BaseP2);
    if(Effect.ADRatioBp>0){Text+=TEXT(" + ")+HudPercent(Effect.ADRatioBp)+LDTerms::AttackDamage;}
    if(Effect.APRatioBp>0){Text+=TEXT(" + ")+HudPercent(Effect.APRatioBp)+LDTerms::AbilityPower;}
    if(Effect.MaxHPRatioBp>0){Text+=TEXT(" + ")+HudPercent(Effect.MaxHPRatioBp)+TEXT("自身")+LDTerms::MaxHealth();}
    return Text;
}
}
FString LDHUD::StatLine(const FLDDemoStatBonus& Stat)
{
    const FString Name=HudBonusStatName(Stat.Stat);
    if(Name.IsEmpty()){return FString::Printf(TEXT("未知属性 #%d"),int32(Stat.Stat));}
    switch(Stat.Stat)
    {
    case ELDDemoBonusStat::AttackRangeCmFlat: return Name+TEXT(" +")+HudRational(Stat.Cm)+TEXT("cm");
    case ELDDemoBonusStat::HPRegenFlat: case ELDDemoBonusStat::MPRegenFlat: return Name+TEXT(" +")+HudRational(Stat.Cm)+TEXT("/s");
    case ELDDemoBonusStat::AttackSpeedBonusRatio: case ELDDemoBonusStat::MoveSpeedBonusRatio: case ELDDemoBonusStat::CritChanceBonusRatio:
    case ELDDemoBonusStat::ADBonusRatio: case ELDDemoBonusStat::APBonusRatio:
        return Name+TEXT(" +")+HudPercent(Stat.Amount);
    default: return Name+TEXT(" +")+HudPoints(Stat.Amount);
    }
}
FString LDHUD::EffectLine(const FLDDemoEffectDefinition& Effect)
{
    switch(Effect.GetOp())
    {
    case ELDDemoEffectOp::Damage: return HudDamageTerm(Effect)+TEXT(" ")+HudScaling(Effect);
    case ELDDemoEffectOp::Shield: return FString(TEXT("获得"))+LDTerms::Shield+TEXT(" ")+HudScaling(Effect)+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::DispelRoot: return FString(TEXT("解除自身全部"))+LDTerms::Root;
    case ELDDemoEffectOp::Root: return FString(LDTerms::Root)+TEXT(" ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::StatBonus:
    {
        FString Text;for(const FLDDemoStatBonus& Stat:Effect.Stats){Text+=(Text.IsEmpty()?TEXT(""):TEXT("，"))+StatLine(Stat);}
        return Text.IsEmpty()?FString(TEXT("无属性加成")):Text;
    }
    case ELDDemoEffectOp::Lifesteal: return FString(LDTerms::BasicAttack)+TEXT("打掉的")+LDTerms::Health+TEXT("的 ")+HudPercent(Effect.RatioBp)+TEXT(" 回复自身");
    case ELDDemoEffectOp::Dash: return TEXT("朝方向冲刺 ")+HudRational(Effect.DistanceCm)+TEXT("cm（")+HudSeconds(Effect.DurationMs)+TEXT("）");
    case ELDDemoEffectOp::Slow:
    {
        return FString(LDTerms::Slow)+TEXT("：")+LDTerms::MoveSpeed+TEXT(" -")+HudPercent(Effect.RatioBp)+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);
    }
    case ELDDemoEffectOp::TimedStatBonus:
    {
        FString Text;for(const FLDDemoStatBonus& Stat:Effect.Stats){Text+=(Text.IsEmpty()?TEXT(""):TEXT("，"))+StatLine(Stat);}
        return Text+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);
    }
    case ELDDemoEffectOp::SelfArea: return TEXT("范围：出手位置周围 ")+HudRational(Effect.RadiusCm)+TEXT("cm 内的敌人");
    case ELDDemoEffectOp::LowHpStatBonus:
    {
        FString Text;for(const FLDDemoStatBonus& Stat:Effect.Stats){Text+=(Text.IsEmpty()?TEXT(""):TEXT("，"))+StatLine(Stat);}
        return FString(LDTerms::Health)+TEXT("低于 ")+HudPercent(Effect.RatioBp)+TEXT(" 时：")+Text;
    }
    case ELDDemoEffectOp::ControlDamageAmp: return TEXT("对处于任何控制或")+FString(LDTerms::Slow)+TEXT("下的敌人，伤害 +")+HudPercent(Effect.RatioBp);
    case ELDDemoEffectOp::Taunt: return FString(LDTerms::Taunt)+TEXT(" ")+HudSeconds(Effect.DurationMs)+TEXT("：自身周围 ")+HudRational(Effect.RadiusCm)+TEXT("cm 内的敌人");
    case ELDDemoEffectOp::Disarm: return FString(LDTerms::Disarm)+TEXT(" ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::Stun: return FString(LDTerms::Stun)+TEXT(" ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::EmpowerBasic:
        return HudSeconds(Effect.DurationMs)+TEXT(" 内下一次")+LDTerms::BasicAttack+TEXT("的主击额外造成 ")+HudScaling(Effect);
    case ELDDemoEffectOp::Thorns:
    {
        FString Text=TEXT("被 ")+HudRational(Effect.RadiusCm)+TEXT("cm 内敌人的")+LDTerms::BasicAttack+TEXT("打中时")
            +TEXT("，对方受")+HudDamageTerm(Effect)+TEXT(" ")+HudPoints(Effect.BaseP2);
        if(Effect.ArmorRatioBp>0){Text+=TEXT(" + ")+HudPercent(Effect.ArmorRatioBp)+LDTerms::Armor;}
        if(Effect.MaxHPRatioBp>0){Text+=TEXT(" + ")+HudPercent(Effect.MaxHPRatioBp)+LDTerms::MaxHealth();}
        if(Effect.CriticalMultiplierBp>10000){Text+=TEXT("，被")+FString(LDTerms::Crit)+TEXT("时变为 ")+HudPercent(Effect.CriticalMultiplierBp);}
        Text+=TEXT("；同一人 ")+HudSeconds(Effect.IntervalMs)+TEXT(" 内最多一次");
        if(Effect.SecondaryRatioBp>0){Text+=TEXT("；反伤使对方")+FString(LDTerms::MoveSpeed)+TEXT(" -")+HudPercent(Effect.SecondaryRatioBp)+TEXT("，持续 ")+HudSeconds(Effect.SecondaryDurationMs);}
        return Text;
    }
    case ELDDemoEffectOp::CooldownRefund:
        return FString(TEXT("每次"))+LDTerms::BasicAttack+TEXT("命中，Q/W/E 剩余冷却 -")+HudSeconds(Effect.AmountMs);
    case ELDDemoEffectOp::TriggeredShield:
        return TEXT("受到伤害时（内置冷却 ")+HudSeconds(Effect.IntervalMs)+TEXT("）获得")
            +LDTerms::Shield+TEXT(" ")+HudScaling(Effect)+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::NthBasicBonus:
        return TEXT("每第 ")+FString::FromInt(Effect.Count)+TEXT(" 次")+LDTerms::BasicAttack+TEXT("额外造成 ")+HudPoints(Effect.BaseP2)+TEXT(" ")+HudDamageTerm(Effect);
    case ELDDemoEffectOp::ManaOnHit:
        return FString(TEXT("技能命中时"))+TEXT("回复 ")+HudPoints(Effect.BaseP2)+TEXT(" ")+LDTerms::Mana+TEXT("（每次施放一次）");
    case ELDDemoEffectOp::DispelSlow: return FString(TEXT("解除自身全部"))+LDTerms::Slow;
    case ELDDemoEffectOp::ShieldBreakRetort:
    {
        FString Text=FString(LDTerms::Shield)+TEXT("被打破时，对打破者造成该")+LDTerms::Shield+TEXT("获得量 ")+HudPercent(Effect.RatioBp)
            +TEXT(" 的")+LDTerms::MagicalDamage;
        if(Effect.SecondaryRatioBp>0){Text+=TEXT("并")+FString(LDTerms::Slow)+TEXT(" ")+HudPercent(Effect.SecondaryRatioBp)+TEXT("，持续 ")+HudSeconds(Effect.SecondaryDurationMs);}
        return Text+TEXT("；内置冷却 ")+HudSeconds(Effect.IntervalMs);
    }
    default: return FString::Printf(TEXT("未知效果 #%d"),Effect.Opcode);
    }
}
FString LDHUD::TargetLine(const FLDDemoTargetDefinition& Target)
{
    switch(Target.Mode)
    {
    case ELDDemoTargetMode::Self: return TEXT("对自身释放");
    case ELDDemoTargetMode::Unit: return TEXT("指向单位 · 射程 ")+HudRational(Target.RangeCm)+TEXT("cm");
    case ELDDemoTargetMode::DirectionProjectile: return TEXT("方向弹道 · 射程 ")+HudRational(Target.RangeCm)+TEXT("cm");
    case ELDDemoTargetMode::DirectionCone: return TEXT("方向扇形 · 射程 ")+HudRational(Target.RangeCm)+TEXT("cm · ")+HudRational(Target.AngleDegrees)+TEXT("°");
    case ELDDemoTargetMode::DirectionDash: return TEXT("方向冲刺");
    case ELDDemoTargetMode::GroundPoint: return TEXT("指定地点 · 最远 ")+HudRational(Target.RangeCm)+TEXT("cm");
    default: return TEXT("未知目标方式");
    }
}
FString LDHUD::CostLine(const FLDDemoAbilityValues& Values)
{
    return FString(LDTerms::Mana)+TEXT(" ")+HudPoints(Values.ManaCostP2)+TEXT(" · 冷却 ")+HudSeconds(Values.CooldownMs);
}
LDHUD::FCardStats LDHUD::CardStatsOf(uint8 Level,const TArray<FLDCombatAttributeRowView>& Attributes)
{
    FCardStats Stats;
    Stats.Level=Level>0?Level:1;
    const auto P2=[&Attributes](ELDProjectedAttribute Attribute)
    {
        const FLDCombatAttributeRowView* Row=Attributes.FindByPredicate([Attribute](const FLDCombatAttributeRowView& R){return R.Attribute==uint8(Attribute);});
        return Row&&Row->Final.Denominator>0&&Row->Final.Numerator>=0&&Row->Final.Numerator<=MAX_int64/100?Row->Final.Numerator*100/Row->Final.Denominator:int64(0);
    };
    Stats.ADP2=P2(ELDProjectedAttribute::AD);Stats.APP2=P2(ELDProjectedAttribute::AP);
    Stats.MaxHPP2=P2(ELDProjectedAttribute::MaxHP);Stats.ArmorP2=P2(ELDProjectedAttribute::Armor);
    return Stats;
}
namespace
{
bool CardRationalStat(ELDDemoBonusStat Stat)
{
    // The importer stores these exact fractions in Cm (unit cm, hp/s, mp/s); Amount stays 0.
    return Stat==ELDDemoBonusStat::AttackRangeCmFlat||Stat==ELDDemoBonusStat::HPRegenFlat||Stat==ELDDemoBonusStat::MPRegenFlat;
}
bool CardRatioStat(ELDDemoBonusStat Stat)
{
    return Stat==ELDDemoBonusStat::AttackSpeedBonusRatio||Stat==ELDDemoBonusStat::MoveSpeedBonusRatio||Stat==ELDDemoBonusStat::CritChanceBonusRatio
        ||Stat==ELDDemoBonusStat::ADBonusRatio||Stat==ELDDemoBonusStat::APBonusRatio;
}
// One placeholder's value as the card shows it; false when the value set has no such effect or stat.
bool CardValue(const LDDemoCardText::FToken& Token,const FLDDemoAbilityValues& Values,const LDHUD::FCardStats& Stats,FString& Out)
{
    using LDDemoCardText::EField;
    const FLDDemoEffectDefinition* Effect=LDDemoCardText::FindEffect(Values,Token);
    if(!Effect){return false;}
    const auto Term=[](int64 RatioBp,int64 StatP2){return RatioBp>0&&StatP2>0?StatP2/10000*RatioBp+StatP2%10000*RatioBp/10000:int64(0);};
    switch(Token.Field)
    {
    case EField::Amount: case EField::Burst:
    {
        int64 P2=Effect->BaseP2+Term(Effect->ADRatioBp,Stats.ADP2)+Term(Effect->APRatioBp,Stats.APP2)+Term(Effect->MaxHPRatioBp,Stats.MaxHPP2)
            +Term(Effect->ArmorRatioBp,Stats.ArmorP2);
        if(Token.Field==EField::Burst){P2=Term(Effect->SecondaryRatioBp,P2);}
        Out=FString::Printf(TEXT("%lld"),(P2+50)/100);return true;
    }
    case EField::BasicCount: Out=FString::FromInt(Effect->BasicCount);return true;
    case EField::Refund: Out=HudDecimal(double(Effect->AmountMs)/1000.);return true;
    case EField::Limit: Out=HudDecimal(double(Effect->LimitP2)/100.);return true;
    case EField::Duration: Out=HudDecimal(double(Effect->DurationMs)/1000.);return true;
    case EField::Ratio: Out=HudDecimal(double(Effect->RatioBp)/100.);return true;
    case EField::Count: Out=FString::FromInt(Effect->Count);return true;
    case EField::Radius: Out=HudRational(Effect->RadiusCm);return true;
    case EField::Distance: Out=HudRational(Effect->DistanceCm);return true;
    case EField::Interval: Out=HudDecimal(double(Effect->IntervalMs)/1000.);return true;
    case EField::Delay: Out=HudDecimal(double(Effect->DelayMs)/1000.);return true;
    case EField::SecondaryRatio: Out=HudDecimal(double(Effect->SecondaryRatioBp)/100.);return true;
    case EField::SecondaryDuration: Out=HudDecimal(double(Effect->SecondaryDurationMs)/1000.);return true;
    case EField::Stat:
    {
        const FLDDemoStatBonus* Stat=Effect->Stats.FindByPredicate([&Token](const FLDDemoStatBonus& S){return S.Stat==Token.Stat;});
        if(!Stat){return false;}
        if(CardRationalStat(Stat->Stat)){Out=HudRational(Stat->Cm);}
        else if(CardRatioStat(Stat->Stat)){Out=HudDecimal(double(Stat->Amount)/100.);}
        else{Out=FString::Printf(TEXT("%lld"),(Stat->Amount+50)/100);}
        return true;
    }
    default: return false;
    }
}
// A per-point bonus exactly as authored: points and percents keep their decimals (脸皮 +2.5, 暴击率 +0.4%).
bool CardPerPointValue(const LDDemoCardText::FToken& Token,const FLDDemoStatDefinition& Stat,FString& Out)
{
    const FLDDemoStatBonus* Bonus=LDDemoCardText::FindPerPoint(Stat,Token);
    if(!Bonus){return false;}
    Out=CardRationalStat(Bonus->Stat)?HudRational(Bonus->Cm):HudDecimal(double(Bonus->Amount)/100.);
    return true;
}
FString CardEscape(const FString& Text)
{
    return Text.Replace(TEXT("&"),TEXT("&amp;")).Replace(TEXT("<"),TEXT("&lt;")).Replace(TEXT(">"),TEXT("&gt;"));
}
// Fills Rich and Keywords from Plain: keywords in the plain text become bold, the longest glossary match winning at each position.
void CardMarkKeywords(LDHUD::FCardText& Text)
{
    for(int32 Index=0;Index<Text.Plain.Len();)
    {
        const LDTerms::FCardKeyword* Hit=nullptr;
        for(const LDTerms::FCardKeyword& Keyword:LDTerms::CardKeywords)
        {
            const int32 Len=FCString::Strlen(Keyword.Name);
            if(Text.Plain.Mid(Index,Len).Equals(Keyword.Name,ESearchCase::CaseSensitive)&&(!Hit||Len>FCString::Strlen(Hit->Name))){Hit=&Keyword;}
        }
        if(!Hit){Text.Rich+=CardEscape(Text.Plain.Mid(Index,1));++Index;continue;}
        Text.Rich+=FString(TEXT("<b>"))+Hit->Name+TEXT("</>");
        Text.Keywords.AddUnique(Hit->Name);
        Index+=FCString::Strlen(Hit->Name);
    }
    TArray<FString> Ordered;
    for(const LDTerms::FCardKeyword& Keyword:LDTerms::CardKeywords){if(Text.Keywords.Contains(Keyword.Name)){Ordered.Add(Keyword.Name);}}
    Text.Keywords=MoveTemp(Ordered);
}
template<typename TValueOf>
LDHUD::FCardText CardRender(const FString& Template,TValueOf&& ValueOf)
{
    LDHUD::FCardText Text;
    TArray<LDDemoCardText::FToken> Tokens;
    FString Error;
    if(!LDDemoCardText::Parse(Template,Tokens,Error)){Text.bComplete=false;Text.Plain=Text.Rich=TEXT("（卡面数据缺失）");return Text;}
    for(const LDDemoCardText::FToken& Token:Tokens)
    {
        if(!Token.bValue){Text.Plain+=Token.Literal;continue;}
        FString Value;
        if(!ValueOf(Token,Value)){Text.bComplete=false;Value=TEXT("?");}
        Text.Values.Add(Value);
        Text.Plain+=Value;
    }
    CardMarkKeywords(Text);
    return Text;
}
}
LDHUD::FCardText LDHUD::CardText(const FString& Template,const FLDDemoAbilityValues& Values,const FCardStats& Stats,
    int64 GrowthPerLevelBp,int64 GrowthPerLevelAfter10Bp)
{
    FLDDemoAbilityValues Grown;
    if(!LDSkillGrowth::GrowValues(Values,GrowthPerLevelBp,GrowthPerLevelAfter10Bp,Stats.Level,Grown))
    {
        FCardText Text;Text.bComplete=false;Text.Plain=Text.Rich=TEXT("（卡面数据缺失）");return Text;
    }
    return CardRender(Template,[&Grown,&Stats](const LDDemoCardText::FToken& Token,FString& Out){return !Token.bPerPoint&&CardValue(Token,Grown,Stats,Out);});
}
LDHUD::FCardText LDHUD::StatCardText(const FString& Template,const FLDDemoStatDefinition& Stat)
{
    return CardRender(Template,[&Stat](const LDDemoCardText::FToken& Token,FString& Out){return Token.bPerPoint&&CardPerPointValue(Token,Stat,Out);});
}
TArray<FString> LDHUD::KeywordLines(const TArray<FString>& Keywords)
{
    TArray<FString> Lines;
    for(const LDTerms::FCardKeyword& Keyword:LDTerms::CardKeywords)
    {
        if(Keywords.Contains(Keyword.Name)){Lines.Add(FString(TEXT("<b>"))+Keyword.Name+TEXT("</>：")+CardEscape(Keyword.Explain));}
    }
    return Lines;
}
LDHUD::FActiveCardView LDHUD::ActiveCard(FName DefinitionId,const FLDDemoContentDefinitions* Content,const FCardStats& Stats)
{
    FActiveCardView Card;
    Card.DefinitionId=DefinitionId;
    const FString Id=DefinitionId.ToString();
    Card.Name=ContentName(Content,Id);
    Card.Glyph=IconGlyph(Card.Name);
    const FLDDemoDisplayDefinition* Display=Content?Content->Display.FindByPredicate([&Id](const FLDDemoDisplayDefinition& D){return D.Id==Id;}):nullptr;
    const FLDDemoAbilityDefinition* Ability=Content?Content->Abilities.FindByPredicate([&Id](const FLDDemoAbilityDefinition& D){return D.Id==Id;}):nullptr;
    if(Display){Card.IconId=Display->IconId;}
    if(!Ability||!Display)
    {
        Card.SentencePlain=Card.Sentence=Content?FString(TEXT("内容目录中找不到该技能")):FString(TEXT("内容数据同步中"));
        return Card;
    }
    const FCardText Sentence=CardText(Display->DescriptionText,Ability->Values,Stats,Content->Parameters.SkillGrowthPerLevelBp,
        Content->Parameters.SkillGrowthPerLevelAfter10Bp);
    Card.Sentence=Sentence.Rich;Card.SentencePlain=Sentence.Plain;
    Card.Keywords=KeywordLines(Sentence.Keywords);
    Card.Target=TargetLine(Ability->Target);
    Card.Cost=CostLine(Ability->Values);
    Card.bKnown=Sentence.bComplete;
    return Card;
}
LDHUD::FStatRowView LDHUD::StatRow(const FLDDemoStatDefinition& Stat,int32 Points,const FLDDemoContentDefinitions* Content)
{
    FStatRowView Row;
    Row.StatId=FName(*Stat.Id);
    Row.Points=Points;
    Row.Name=ContentName(Content,Stat.Id);
    Row.Glyph=IconGlyph(Row.Name);
    Row.IconId=ContentIconId(Content,Stat.Id);
    const FLDDemoDisplayDefinition* Display=Content?Content->Display.FindByPredicate([&Stat](const FLDDemoDisplayDefinition& D){return D.Id==Stat.Id;}):nullptr;
    if(!Display){Row.PerPoint=Row.PerPointRich=TEXT("内容目录中找不到该属性");return Row;}
    const FCardText Text=StatCardText(Display->DescriptionText,Stat);
    Row.PerPoint=Text.Plain;Row.PerPointRich=Text.Rich;
    Row.bKnown=Text.bComplete;
    return Row;
}
int32 LDHUD::StatPointsOf(const FLDProgressionView& Progression,FName StatId)
{
    const FLDStatPointsView* Row=Progression.StatPoints.FindByPredicate([StatId](const FLDStatPointsView& S){return S.StatId==StatId;});
    return Row?Row->Points:0;
}
FName LDHUD::ActiveInSlot(const FLDProgressionView& Progression,uint8 Slot)
{
    const FLDActiveSlotView* Active=Progression.Actives.FindByPredicate([Slot](const FLDActiveSlotView& A){return A.Slot==Slot;});
    return Active?Active->DefinitionId:NAME_None;
}
FString LDHUD::SlotKey(uint8 Slot)
{
    return Slot==0?FString(TEXT("Q")):Slot==1?FString(TEXT("W")):Slot==2?FString(TEXT("E")):FString::Printf(TEXT("槽 %d"),int32(Slot));
}
FString LDHUD::BuildBlockedText(ELDGrowthReason Reason,int64 BuildSafeAtMs,bool bClock,int64 NowMs)
{
    switch(Reason)
    {
    case ELDGrowthReason::None: return FString();
    case ELDGrowthReason::NotSafe:
    {
        if(!bClock||BuildSafeAtMs<=NowMs){return LDTerms::NotSafe;}
        // Rounded up to the next tenth, so the wait never reads 0.0 while it lasts.
        const int64 Tenths=(BuildSafeAtMs-NowMs+99)/100;
        return LDTerms::NotSafeFor(FString::Printf(TEXT("%lld.%lld"),Tenths/10,Tenths%10));
    }
    case ELDGrowthReason::Dead: return LDTerms::DeadNoLearn;
    case ELDGrowthReason::NoEmptySlot: return LDTerms::SlotsFull;
    case ELDGrowthReason::SlotOccupied: return LDTerms::ReplaceAtRest;
    default: return GrowthReasonText(Reason);
    }
}
LDHUD::FBuildGateView LDHUD::BuildGate(LDProgression::EMatchStage Stage,ELDLifePhase LifePhase,int64 BuildSafeAtMs,bool bClock,int64 NowMs)
{
    LDProgression::FBuildContext Context;
    Context.Stage=Stage;
    Context.LifePhase=LifePhase;
    Context.bSafe=bClock&&BuildSafeAtMs<=NowMs;
    const auto Gate=[&](ELDBuildCommandKind Kind,bool& bAllowed,FString& Blocked)
    {
        const ELDGrowthReason Reason=LDProgression::CommandGate(Kind,Context);
        bAllowed=Reason==ELDGrowthReason::None;
        // An unknown safe window is no countdown: say it is syncing instead of naming a wait.
        Blocked=Reason==ELDGrowthReason::NotSafe&&!bClock?FString(TEXT("构筑安全窗同步中")):BuildBlockedText(Reason,BuildSafeAtMs,bClock,NowMs);
    };
    FBuildGateView View;
    Gate(ELDBuildCommandKind::ActiveChoice,View.bCanChoose,View.ChooseBlocked);
    Gate(ELDBuildCommandKind::LearnBlueprint,View.bCanLearn,View.LearnBlocked);
    Gate(ELDBuildCommandKind::AllocatePoints,View.bCanAllocate,View.AllocateBlocked);
    Gate(ELDBuildCommandKind::BuyNpcOffer,View.bCanBuyNpc,View.NpcBlocked);
    Gate(ELDBuildCommandKind::Respec,View.bCanRespec,View.RespecBlocked);
    Gate(ELDBuildCommandKind::ReplaceActive,View.bCanReplace,View.ReplaceBlocked);
    Gate(ELDBuildCommandKind::ForgetActive,View.bCanForget,View.ForgetBlocked);
    View.bInRest=Stage==LDProgression::EMatchStage::Rest||Stage==LDProgression::EMatchStage::RestLocked;
    return View;
}
FString LDHUD::BuildReceiptText(const FLDBuildReceipt& Receipt,const FLDDemoContentDefinitions* Content,int64 BuildSafeAtMs,bool bClock,int64 NowMs)
{
    if(Receipt.Header.Outcome!=ELDReceiptOutcome::Applied)
    {
        return TEXT("没有生效：")+BuildBlockedText(LDGrowthWire::Reason(Receipt.Header.Reason),BuildSafeAtMs,bClock,NowMs);
    }
    const FString Missing(TEXT("回执缺少结果，请以当前状态为准。"));
    switch(Receipt.Kind)
    {
    case ELDBuildCommandKind::ActiveChoice:
    case ELDBuildCommandKind::LearnBlueprint:
    {
        const FString Name=ContentName(Content,Receipt.DefinitionId.ToString());
        if(Receipt.ChoiceResult==ELDBuildChoiceResult::Learned){return LDTerms::LearnedInto(Name,SlotKey(Receipt.LearnedSlot));}
        if(Receipt.ChoiceResult==ELDBuildChoiceResult::StoredAsBlueprint)
        {return LDTerms::StoredAsBlueprint(Name,BuildBlockedText(LDGrowthWire::Reason(Receipt.StoredReason),BuildSafeAtMs,bClock,NowMs));}
        return Missing;
    }
    case ELDBuildCommandKind::AllocatePoints:
    {
        int64 Spent=0;for(const uint8 Count:Receipt.PointsSpent){Spent+=Count;}
        return LDTerms::PointsAdded(Spent,Receipt.UnspentPointsAfter);
    }
    case ELDBuildCommandKind::BuyNpcOffer:
        return Receipt.NpcPointsPaid>0?LDTerms::NpcBought(Receipt.NpcPointsPaid):Missing;
    case ELDBuildCommandKind::Respec:
        return Receipt.PointsRefunded>0?LDTerms::Respecced(Receipt.PointsRefunded,Receipt.UnspentPointsAfter):Missing;
    case ELDBuildCommandKind::ReplaceActive:
        if(Receipt.ChoiceResult!=ELDBuildChoiceResult::Learned||Receipt.DefinitionId.IsNone()||Receipt.ForgottenId.IsNone()){return Missing;}
        return LDTerms::Replaced(ContentName(Content,Receipt.DefinitionId.ToString()),SlotKey(Receipt.LearnedSlot),ContentName(Content,Receipt.ForgottenId.ToString()));
    case ELDBuildCommandKind::ForgetActive:
        return Receipt.ForgottenId.IsNone()?Missing:LDTerms::Forgotten(ContentName(Content,Receipt.ForgottenId.ToString()));
    default: return Missing;
    }
}
LDHUD::FGrowthPending LDHUD::GrowthPending(const FLDProgressionView* Progression,uint32 Match)
{
    FGrowthPending Pending;
    if(!Progression||!Progression->bValid||Match==0||Progression->Generation!=Match){return Pending;}
    Pending.Offers=Progression->QueuedActiveOpportunities;
    Pending.Points=FMath::Max<int64>(0,Progression->UnspentPoints);
    Pending.Blueprints=Progression->Actives.Num()<3?Progression->PendingBlueprints.Num():0;
    return Pending;
}
FString LDHUD::FGrowthPending::Prompt() const
{
    FString Text=LDTerms::Choice;
    if(Offers>0){Text+=FString::Printf(TEXT("+%d"),Offers);}
    if(Points>0){Text+=FString::Printf(TEXT(" · %lld点"),Points);}
    if(Offers<=0&&Points<=0&&Blueprints>0){Text+=FString::Printf(TEXT(" · %s %d"),LDTerms::Blueprint,Blueprints);}
    return Text;
}
FString LDHUD::GrowthPrompt(const FGrowthPending& Pending,bool bRestServices)
{
    return Pending.Any()||!bRestServices?Pending.Prompt():FString(LDTerms::Choice)+TEXT(" · ")+LDTerms::RestServices;
}
LDHUD::FAttributeRowView LDHUD::AttributeRow(const FLDCombatAttributeRowView& Row)
{
    static const FString Names[]={LDTerms::MaxHealth(),LDTerms::MaxMana(),LDTerms::AttackDamage,LDTerms::AbilityPower,LDTerms::Armor,LDTerms::MagicResist,
        LDTerms::AttackSpeed,LDTerms::MoveSpeed,TEXT("攻击距离"),LDTerms::HealthRegen(),LDTerms::ManaRegen(),LDTerms::CritChance(),LDTerms::Haste};
    static const TCHAR* Units[]={TEXT(""),TEXT(""),TEXT(""),TEXT(""),TEXT(""),TEXT(""),TEXT(" 次/秒"),TEXT(" cm/s"),TEXT(" cm"),TEXT("/s"),TEXT("/s"),TEXT(""),TEXT("")};
    FAttributeRowView View;
    if(Row.Attribute>=UE_ARRAY_COUNT(Names)){View.Name=FString::Printf(TEXT("未知属性 #%d"),Row.Attribute);View.Final=TEXT("—");return View;}
    View.Name=Names[Row.Attribute];View.bClamped=Row.bClamped;
    const bool bRatio=static_cast<ELDProjectedAttribute>(Row.Attribute)==ELDProjectedAttribute::CritChance;
    auto Value=[bRatio](const FLDDemoRational& V){if(V.Denominator<=0){return FString(TEXT("?"));}const double D=double(V.Numerator)/double(V.Denominator);return bRatio?HudDecimal(D*100.)+TEXT("%"):HudDecimal(D);};
    View.Final=Value(Row.Final)+Units[Row.Attribute]+(Row.bClamped?TEXT("（已达上下限）"):TEXT(""));
    View.Breakdown=TEXT("基础 ")+Value(Row.Base)+TEXT(" · 等级 ")+Value(Row.Level)+TEXT(" · 属性点 ")+Value(Row.Choices);
    if(Row.PercentBonus.Denominator>0&&Row.PercentBonus.Numerator!=0)
    {View.Breakdown+=TEXT(" · 百分比 +")+HudDecimal(100.*double(Row.PercentBonus.Numerator)/double(Row.PercentBonus.Denominator))+TEXT("%");}
    if(Row.Dev.Denominator>0&&Row.Dev.Numerator!=0){View.Breakdown+=TEXT(" · 测试 +")+Value(Row.Dev);}
    if(Row.DevPercent.Denominator>0&&Row.DevPercent.Numerator!=0)
    {View.Breakdown+=TEXT(" · 测试百分比 +")+HudDecimal(100.*double(Row.DevPercent.Numerator)/double(Row.DevPercent.Denominator))+TEXT("%");}
    if(Row.Temporary.Denominator>0&&Row.Temporary.Numerator!=0){View.Breakdown+=TEXT(" · 临时 +")+Value(Row.Temporary);}
    if(Row.TemporaryPercent.Denominator>0&&Row.TemporaryPercent.Numerator!=0)
    {
        const double Percent=100.*double(Row.TemporaryPercent.Numerator)/double(Row.TemporaryPercent.Denominator);
        View.Breakdown+=TEXT(" · 临时百分比 ")+FString(Percent>0?TEXT("+"):TEXT(""))+HudDecimal(Percent)+TEXT("%");
    }
    return View;
}
FString LDHUD::GrowthReasonText(ELDGrowthReason Reason)
{
    switch(Reason)
    {
    case ELDGrowthReason::None: return FString();
    case ELDGrowthReason::InvalidRequest: return TEXT("请求无效");
    case ELDGrowthReason::OldMatch: return TEXT("已不是当前对局");
    case ELDGrowthReason::OldLife: return TEXT("已不是当前生命");
    case ELDGrowthReason::LifePhaseChanged: return TEXT("生死状态已变化，请重新确认");
    case ELDGrowthReason::LeftMatch: return TEXT("你已离开本局");
    case ELDGrowthReason::MatchClosed: return TEXT("对局已结束");
    case ELDGrowthReason::NotContender: return TEXT("不是本局参赛者");
    case ELDGrowthReason::CatalogMismatch: return TEXT("内容版本不一致");
    case ELDGrowthReason::NotAuthority: return TEXT("服务器不可用");
    case ELDGrowthReason::Dead: return FString(LDTerms::Dead)+TEXT("，暂时不能操作");
    case ELDGrowthReason::RequestConflict: return TEXT("请求冲突，未执行");
    case ELDGrowthReason::OutOfOrder: return TEXT("请求顺序错误，未执行");
    case ELDGrowthReason::ReceiptExpired: return TEXT("回执已过期，请以当前状态为准");
    case ELDGrowthReason::RequestInFlight: return TEXT("上一个请求仍在处理中");
    case ELDGrowthReason::UnknownOffer: return TEXT("这组技能候选已失效");
    case ELDGrowthReason::InvalidOption: return TEXT("该选项无效");
    case ELDGrowthReason::OfferAlreadyCommitted: return TEXT("这组技能已经选过了");
    case ELDGrowthReason::OfferGuardStale: return TEXT("内容已更新，请重新确认");
    case ELDGrowthReason::BuildStale: return TEXT("构筑已变化，请重新确认");
    case ELDGrowthReason::NotSafe: return FString(LDTerms::NotSafe)+TEXT("，暂时不能学习或加点");
    case ELDGrowthReason::NoEmptySlot: return LDTerms::SlotsFull;
    case ELDGrowthReason::SlotOccupied: return FString(TEXT("这个技能槽已有技能，"))+LDTerms::ReplaceAtRest;
    case ELDGrowthReason::NoBlueprint: return FString(TEXT("没有这份"))+LDTerms::Blueprint;
    case ELDGrowthReason::NotEnoughPoints: return TEXT("未分配点数不够");
    case ELDGrowthReason::NoActiveOffer: return TEXT("当前没有可选的技能");
    case ELDGrowthReason::PhaseClosed: return TEXT("现在不是成长时段");
    case ELDGrowthReason::ReplacementNotAllowed: return TEXT("休息时才能替换或遗忘技能");
    case ELDGrowthReason::BuildLocked: return FString(LDTerms::BuildLockedNow)+TEXT("：PK 与返回倒计时期间不能改");
    case ELDGrowthReason::NpcPoolTooSmall: return TEXT("能抽的主动不足 3 个，NPC 没收点数");
    case ELDGrowthReason::OfferOutstanding: return TEXT("还有没选的技能，先选完");
    case ELDGrowthReason::NothingToRespec: return TEXT("还没加过点，不用洗");
    case ELDGrowthReason::SlotEmpty: return TEXT("这个技能槽是空的");
    case ELDGrowthReason::NotRestWindow: return TEXT("休息时才能找 NPC 或洗点");
    case ELDGrowthReason::DataNotReady: return TEXT("数据尚未就绪");
    case ELDGrowthReason::StaleContext: return TEXT("状态已变化，请重新确认");
    case ELDGrowthReason::NumericUnderflow: return TEXT("数值越界，本次没有提交");
    case ELDGrowthReason::NumericDomain: return TEXT("数值非法，本次没有提交");
    case ELDGrowthReason::ArithmeticOverflow: return TEXT("数值溢出，本次没有提交");
    case ELDGrowthReason::DataInvalid: return TEXT("数据无效，本次没有提交");
    case ELDGrowthReason::PrepareUnavailable: return TEXT("暂时无法提交，请稍后重试");
    case ELDGrowthReason::AuthorityFault: return TEXT("服务器故障，本次没有提交");
    case ELDGrowthReason::CapacityExceeded: return TEXT("服务器容量已满");
    default: return FString::Printf(TEXT("未知原因 #%d"),int32(Reason));
    }
}
FString LDHUD::CombatRejectText(ELDCombatReject Reject)
{
    switch(Reject)
    {
    case ELDCombatReject::None: return FString();
    case ELDCombatReject::InvalidRequest: return TEXT("请求无效");
    case ELDCombatReject::NotOwner: return TEXT("不是你的角色");
    case ELDCombatReject::StaleMatch: return TEXT("已不是当前对局");
    case ELDCombatReject::StaleLife: return TEXT("已不是当前生命");
    case ELDCombatReject::StaleRequest: return TEXT("请求已过期");
    case ELDCombatReject::RequestConflict: return TEXT("请求冲突");
    case ELDCombatReject::ReceiptExpired: return TEXT("回执已过期");
    case ELDCombatReject::Cancelled: return TEXT("已取消");
    case ELDCombatReject::StaleSource: return TEXT("技能已变化");
    case ELDCombatReject::SourceUnavailable: return TEXT("没有可用来源");
    case ELDCombatReject::Busy: return TEXT("正在执行其他动作");
    case ELDCombatReject::Cooldown: return TEXT("冷却中");
    case ELDCombatReject::AttackNotReady: return FString(LDTerms::BasicAttack)+TEXT("尚未就绪");
    case ELDCombatReject::InsufficientMana: return LDTerms::NotEnoughMana();
    case ELDCombatReject::ControlBlocked: return TEXT("受控制无法使用");
    case ELDCombatReject::TargetUnavailable: return TEXT("目标不可用");
    case ELDCombatReject::InvalidDirection: return TEXT("方向无效");
    case ELDCombatReject::OutOfRange: return TEXT("超出射程");
    case ELDCombatReject::Blocked: return TEXT("被地形阻挡");
    case ELDCombatReject::Dead: return LDTerms::Dead;
    case ELDCombatReject::MatchClosed: return TEXT("对局已结束");
    case ELDCombatReject::AuthorityDataUnavailable: return TEXT("服务器数据不可用");
    case ELDCombatReject::CapacityUnavailable: return TEXT("服务器容量不足");
    case ELDCombatReject::NotStarted: return LDTerms::NotStartedYet;
    default: return FString::Printf(TEXT("未知原因 #%d"),int32(Reject));
    }
}
LDHUD::FActionInput LDHUD::ActionInput(const FLDCombatEntryView& Entry,EActionSlot Slot,const FLDDemoContentDefinitions* Content)
{
    FActionInput Input;
    Input.Slot=Slot;Input.bProjected=true;
    Input.bHasSource=!Entry.DefinitionId.IsNone();
    // The innate dash is no content definition with display text; its slot names the action itself.
    if(Input.bHasSource){Input.Name=Slot==EActionSlot::Dash?FString(LDTerms::Dash):ContentName(Content,Entry.DefinitionId.ToString());}
    if(Input.bHasSource){Input.IconId=Slot==EActionSlot::Dash?FString(LDUIIcons::Dash):ContentIconId(Content,Entry.DefinitionId.ToString());}
    Input.RangeCm=Entry.RangeCm;
    Input.DefinitionId=Entry.DefinitionId;
    Input.ManaCostP2=Entry.ManaCostP2;
    Input.ReadyAtMs=Entry.ReadyAtMs;Input.CooldownMs=Entry.CooldownMs;
    Input.ServerBlock=CombatRejectText(Entry.Unavailable);
    return Input;
}
FString LDHUD::IconGlyph(const FString& Name)
{
    const FString Trimmed=Name.TrimStartAndEnd();
    return Trimmed.IsEmpty()?FString(TEXT("?")):Trimmed.Left(1).ToUpper();
}
bool LDHUD::StatusIsPositive(uint8 StatusId)
{
    switch(static_cast<ELDDemoStatusId>(StatusId))
    {
    case ELDDemoStatusId::Shield:
    case ELDDemoStatusId::StatBoost:
    case ELDDemoStatusId::Empowered:
    case ELDDemoStatusId::RespawnProtection:
    case ELDDemoStatusId::CampReturning:
        return true;
    case ELDDemoStatusId::Root:
    case ELDDemoStatusId::Slow:
    case ELDDemoStatusId::Exposed:
    case ELDDemoStatusId::Taunt:
    case ELDDemoStatusId::Disarm:
    case ELDDemoStatusId::Stun:
    case ELDDemoStatusId::Knockback:
    case ELDDemoStatusId::Silence:
    case ELDDemoStatusId::Burn:
    case ELDDemoStatusId::AttackSlowed:
        return false;
    }
    return false;
}
LDHUD::FSelfIconsView LDHUD::SelfIcons(const FLDCombatOwnerView* Combat,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* Content)
{
    FSelfIconsView View;
    View.bSyncing=!Combat;
    TArray<FSelfIconView> Control,Temporary;
    if(Combat)
    {
        for(const FLDCombatStatusView& Status:Combat->Statuses)
        {
            if(Status.bIsPassive||Status.StatusId==uint8(ELDDemoStatusId::RespawnProtection)){continue;}
            FSelfIconView Icon;
            const FString Name=StatusName(Content,Status.StatusId);
            Icon.Kind=Status.ControlBlocks!=0?ESelfIconKind::Control:ESelfIconKind::Temporary;
            Icon.bPositive=StatusIsPositive(Status.StatusId);
            Icon.Glyph=IconGlyph(Name);Icon.Tip=Name;Icon.Source=ESelfIconSource::Status;
            if(const TCHAR* Id=HudStatusContentId(Status.StatusId)){Icon.ContentId=Id;Icon.IconId=ContentIconId(Content,Icon.ContentId);}
            Icon.Key=FName(*FString::Printf(TEXT("Status.%d"),int32(Status.StatusId)));
            Icon.ExpiresAtMs=FMath::Max<int64>(0,Status.ExpiresAtMs);Icon.Stacks=Status.Stacks;
            if(Status.Stacks>1){Icon.Tip+=FString::Printf(TEXT(" ×%d"),int32(Status.Stacks));}
            if(Status.AmountP2>0){Icon.Tip+=FString::Printf(TEXT(" %lld"),HudWholePoints(Status.AmountP2,true));}
            if(Status.ExpiresAtMs>0&&bClock){Icon.Tip+=TEXT(" ")+TenthsUp(Status.ExpiresAtMs-NowMs);}
            (Icon.Kind==ESelfIconKind::Control?Control:Temporary).Add(MoveTemp(Icon));
        }
        // D21 recoveries are conditional states: shown while they heal, without a remaining-time ring.
        auto AddRecovery=[&Temporary](ESelfIconSource Source,const FString& Name,const TCHAR* Key,const TCHAR* IconId)
        {
            FSelfIconView Icon;Icon.Kind=ESelfIconKind::Temporary;Icon.Source=Source;Icon.IconId=IconId;
            Icon.Glyph=IconGlyph(Name);Icon.Tip=Name;Icon.Key=FName(Key);
            Temporary.Add(MoveTemp(Icon));
        };
        if(Combat->bOutOfCombatRegenActive){AddRecovery(ESelfIconSource::OutOfCombatRegen,LDTerms::OutOfCombatRegen,TEXT("Recovery.OutOfCombat"),LDUIIcons::OutOfCombatRegen);}
        if(Combat->bDormRegenActive){AddRecovery(ESelfIconSource::DormRegen,LDTerms::DormRegenActive(),TEXT("Recovery.Dorm"),LDUIIcons::DormRegen);}
    }
    for(TArray<FSelfIconView>* Group:{&Control,&Temporary})
    {
        for(FSelfIconView& Icon:*Group)
        {
            if(View.Items.Num()<SelfIconMax){View.Items.Add(MoveTemp(Icon));}else{++View.Overflow;}
        }
    }
    return View;
}
LDHUD::FActionDetailView LDHUD::SelfIconDetail(const FSelfIconView& Icon,const FLDDemoContentDefinitions* Content)
{
    FActionDetailView Detail;
    Detail.Name=Icon.Tip;
    TArray<FString> Lines;
    FString Description;
    if(!Icon.ContentId.IsEmpty()&&ContentText(Content,Icon.ContentId,true,Description)&&!Description.IsEmpty()){Lines.Add(Description);}
    switch(Icon.Source)
    {
    case ESelfIconSource::Status:
        Detail.Summary=FString(Icon.Kind==ESelfIconKind::Control?TEXT("控制状态"):TEXT("临时状态"))+(Icon.bPositive?TEXT(" · 正面"):TEXT(" · 负面"));
        break;
    case ESelfIconSource::OutOfCombatRegen:
    case ESelfIconSource::DormRegen:
    {
        Detail.Summary=TEXT("临时状态 · 正面");
        if(!Content){break;}
        const FLDDemoContentParameters& P=Content->Parameters;
        const bool bDorm=Icon.Source==ESelfIconSource::DormRegen;
        const int64 HPBp=bDorm?P.DormMaxHPRegenBp:P.OutOfCombatMaxHPRegenBp;
        const int64 MPBp=bDorm?P.DormMaxMPRegenBp:P.OutOfCombatMaxMPRegenBp;
        const FString Rate=HPBp==MPBp?TEXT("每秒恢复 ")+HudPercent(HPBp)+TEXT(" 生命上限和法力上限")
            :TEXT("每秒恢复 ")+HudPercent(HPBp)+TEXT(" 生命上限、")+HudPercent(MPBp)+TEXT(" 法力上限");
        Lines.Add(bDorm?FString(TEXT("站在"))+LDTerms::Dorm+TEXT("内且脱战 ")+HudSeconds(P.DormRegenDelayMs)+TEXT(" 后，")+Rate+TEXT("；与脱战回复叠加")
            :TEXT("脱战 ")+HudSeconds(P.OutOfCombatRegenDelayMs)+TEXT(" 后，")+Rate);
        Lines.Add(bDorm?FString(TEXT("出手、释放伤害技能、受到伤害或离开"))+LDTerms::Dorm+TEXT("立即停止")
            :FString(TEXT("出手、释放伤害技能或受到伤害立即停止")));
        break;
    }
    }
    Detail.Body=FString::Join(Lines,TEXT("\n"));
    return Detail;
}
float LDHUD::FStatusRings::Fraction(FName Key,int64 ExpiresAtMs,int64 NowMs)
{
    const int64 Remaining=FMath::Max<int64>(0,ExpiresAtMs-NowMs);
    FSpan& Span=Spans.FindOrAdd(Key);
    // A new or later end instant is a fresh application or a refresh: the ring restarts full from here. An earlier end
    // (a stronger source ran out) keeps the span, so the ring jumps down instead of refilling.
    if(ExpiresAtMs>Span.ExpiresAtMs){Span.SpanMs=Remaining;}
    Span.ExpiresAtMs=ExpiresAtMs;
    return Span.SpanMs>0?FMath::Clamp(float(double(Remaining)/double(Span.SpanMs)),0.f,1.f):0.f;
}
void LDHUD::FStatusRings::Retain(const TArray<FName>& Live)
{
    for(auto It=Spans.CreateIterator();It;++It){if(!Live.Contains(It.Key())){It.RemoveCurrent();}}
}
FString LDHUD::EntryLabel(ELDCombatEntry Entry)
{
    switch(Entry)
    {
    case ELDCombatEntry::Basic: return LDTerms::BasicAttack;
    case ELDCombatEntry::Q: return TEXT("角色技能 1");
    case ELDCombatEntry::W: return TEXT("角色技能 2");
    case ELDCombatEntry::E: return TEXT("角色技能 3");
    case ELDCombatEntry::Dash: return LDTerms::Dash;
    default: return TEXT("动作");
    }
}
FString LDHUD::CombatReceiptNotice(const FLDCombatActionReceipt& Receipt)
{
    TArray<FString> Parts;
    // Navigation-issued attacks report through their order; only the player's own direct request is explained here.
    if(Receipt.Phase==ELDCombatPhase::Rejected&&Receipt.Origin==ELDCombatRequestOrigin::Direct
        &&Receipt.Reject!=ELDCombatReject::None&&Receipt.Reject!=ELDCombatReject::Cancelled)
    {Parts.Add(EntryLabel(Receipt.Entry)+TEXT(" 未执行：")+CombatRejectText(Receipt.Reject));}
    if(Receipt.bProtectionRemoved){Parts.Add(LDTerms::ProtectionRemoved());}
    return FString::Join(Parts,TEXT(" · "));
}
FString LDHUD::CombatReceiptSlotNotice(const FLDCombatActionReceipt& Receipt)
{
    const bool bSlot=Receipt.Entry==ELDCombatEntry::Q||Receipt.Entry==ELDCombatEntry::W
        ||Receipt.Entry==ELDCombatEntry::E||Receipt.Entry==ELDCombatEntry::Dash;
    if(!bSlot||Receipt.Phase!=ELDCombatPhase::Rejected||Receipt.Origin!=ELDCombatRequestOrigin::Direct
        ||Receipt.Reject==ELDCombatReject::None||Receipt.Reject==ELDCombatReject::Cancelled){return FString();}
    return CombatRejectText(Receipt.Reject);
}
FString LDHUD::CombatReceiptsNotice(const TArray<FLDCombatActionReceipt>& Arrived,uint32 Match,uint32 Life,uint64& ToldProtectionLifeRevision)
{
    TArray<FString> Notices;
    for(FLDCombatActionReceipt Receipt:Arrived)
    {
        if(Receipt.Generation!=Match||Receipt.Life!=Life){continue;}
        // Later receipts of the same request repeat the flag.
        const bool bTellProtection=Receipt.bProtectionRemoved&&ToldProtectionLifeRevision!=Receipt.LifeRevision;
        if(bTellProtection){ToldProtectionLifeRevision=Receipt.LifeRevision;}
        Receipt.bProtectionRemoved=false;
        const FString Failure=CombatReceiptNotice(Receipt);
        if(!Failure.IsEmpty()){Notices.AddUnique(Failure);}
        if(bTellProtection){Notices.AddUnique(LDTerms::ProtectionRemoved());}
    }
    return FString::Join(Notices,TEXT(" · "));
}

void LDHUD::FChoicesFold::Opened(bool bAutomaticOpen,double Now)
{
    // A player's own opening right after a hit folded the panel is respected: hits no longer fold it until it closes.
    bHitImmune=!bAutomaticOpen&&bFoldedByHit;
    bFoldedByHit=false;
    bOpen=true;bAutomatic=bAutomaticOpen;OpenedAt=Now;
}
void LDHUD::FChoicesFold::Closed()
{
    bOpen=false;bAutomatic=false;bHitImmune=false;
}
bool LDHUD::FChoicesFold::ShouldFold(double Now,bool bEngaged,bool bOwnHit)
{
    if(!bOpen){return false;}
    // Engaging with the automatic opening makes it the player's own: it no longer times out.
    if(bEngaged){bAutomatic=false;return false;}
    if(bOwnHit&&!bHitImmune){bFoldedByHit=true;return true;}
    return bAutomatic&&Now-OpenedAt>=AutoOpenSeconds;
}
bool LDHUD::FGrowthAutoOpen::Observe(const FLDProgressionView& View,uint32 Match)
{
    if(!View.bValid||Match==0||View.Generation!=Match){return false;}
    if(SeenMatch!=Match){SeenMatch=Match;SeenOffer.Invalidate();SeenUnspent=0;}
    const bool bNewOffer=View.bHasActiveOffer&&View.ActiveOffer.OfferId.IsValid()&&View.ActiveOffer.OfferId!=SeenOffer;
    const bool bMorePoints=View.UnspentPoints>SeenUnspent;
    if(View.bHasActiveOffer){SeenOffer=View.ActiveOffer.OfferId;}
    SeenUnspent=View.UnspentPoints;
    return bNewOffer||bMorePoints;
}