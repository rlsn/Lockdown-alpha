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
#include "Combat/LDDamageRecap.h"
#include "Combat/LDChainRules.h"
#include "Combat/LDCombatElements.h"
#include "PK/LDPKRules.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"

bool LDHUD::FReadModelKey::SameInputs(const FReadModelKey& Other) const
{
    return Views==Other.Views&&Local==Other.Local&&CombatReceipts==Other.CombatReceipts&&Feedback==Other.Feedback
        &&Frame==Other.Frame&&Interaction==Other.Interaction&&Pointer==Other.Pointer&&Viewport==Other.Viewport
        &&TextScale==Other.TextScale&&Flags==Other.Flags;
}
int64 LDHUD::ReadModelClock(double RealSeconds,bool bTimed)
{
    return bTimed?int64(FMath::FloorToDouble(RealSeconds*ReadModelClockHz)):int64(-1);
}
bool LDHUD::ReadModelRingMoves(int64 StartedAtMs,int64 EndsAtMs)
{
    return EndsAtMs>StartedAtMs&&EndsAtMs-StartedAtMs<ReadModelSmoothRingMs;
}
bool LDHUD::FReadModelGate::ShouldRebuild(const FReadModelKey& Key) const
{
    return !bHas||bLive||bSettle||Key.Clock!=Last.Clock||!Key.SameInputs(Last);
}
void LDHUD::FReadModelGate::Rebuilt(const FReadModelKey& Key,bool bBuiltLive)
{
    // A clock tick alone moves no layout; any other change is built once more after Slate laid out this build.
    bSettle=!bHas||!Key.SameInputs(Last);
    Last=Key;bHas=true;bLive=bBuiltLive;
}

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
    case ELDDemoStatusId::CampEnraged: return TEXT("camp_enraged");
    case ELDDemoStatusId::CampGuarding: return TEXT("camp_guarding");
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
    case ELDDemoStatusId::Wet: return TEXT("wet");
    case ELDDemoStatusId::Electrified: return TEXT("electrified");
    case ELDDemoStatusId::Braced: return TEXT("braced");
    case ELDDemoStatusId::Chilled: return TEXT("chilled");
    case ELDDemoStatusId::Rotten: return TEXT("rotten");
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
        ||!bHealth||Combat->MaxHPP2<=0){return FString();}
    // Momentum has no steady rate (combat gain, idle drift and hit income): only health shows one.
    const double Rate=Combat->HPRegenPerSec;
    return FMath::IsFinite(Rate)&&Rate>=0?FString::Printf(TEXT("+%.1f/s"),Rate):FString();
}

FString LDHUD::RankDots(uint8 Rank)
{
    FString Out;
    for(uint8 Index=1;Index<=3;++Index){Out+=Index<=Rank?TEXT("●"):TEXT("○");}
    return Out;
}

TArray<ELDSkillBranch> LDHUD::NextBranches(ELDSkillBranch Current)
{
    using B=ELDSkillBranch;
    switch(Current)
    {
    case B::None: return {B::A,B::B};
    case B::A: return {B::A1,B::A2};
    case B::B: return {B::B1,B::B2};
    default: return {};
    }
}

LDHUD::FBranchCardView LDHUD::BranchCard(const FLDDemoContentDefinitions* Content,FName AbilityId,ELDSkillBranch Path,const FCardStats& Stats)
{
    FBranchCardView Out;
    Out.Path=Path;
    if(!Content){return Out;}
    const FString Id=AbilityId.ToString();
    const FLDDemoAbilityDefinition* Ability=Content->Abilities.FindByPredicate([&Id](const FLDDemoAbilityDefinition& Row){return Row.Id==Id;});
    const FLDDemoAbilityBranch* Branch=Ability?Ability->FindBranch(Path):nullptr;
    if(!Branch){return Out;}
    const FString StepId=LDSkillBranch::DisplayId(Id,Path);
    const FLDDemoDisplayDefinition* Step=Content->Display.FindByPredicate([&StepId](const FLDDemoDisplayDefinition& Row){return Row.Id==StepId;});
    const FLDDemoDisplayDefinition* Whole=Content->Display.FindByPredicate([&Id](const FLDDemoDisplayDefinition& Row){return Row.Id==Id;});
    if(!Step||!Whole){return Out;}
    const uint8 Rank=LDSkillBranch::RankOf(Path);
    FString Name;
    Out.Name=ContentText(Content,StepId,false,Name)?Name:Step->NameText;
    const FCardText Line=CardText(Step->DescriptionText,Branch->Values,Stats,Content->Parameters.SkillRanks,Content->Parameters.SkillGrowthPerLevelBp,Rank);
    const FCardText Body=CardText(Whole->DescriptionText,Branch->Values,Stats,Content->Parameters.SkillRanks,Content->Parameters.SkillGrowthPerLevelBp,Rank);
    if(!Line.bComplete){return Out;}
    Out.Line=Line.Plain;
    Out.Detail=FString::Printf(TEXT("%s\n%s\n%s"),*Line.Plain,*Body.Plain,*CostLine(Branch->Values));
    Out.IconId=Whole->IconId;
    Out.bKnown=true;
    return Out;
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
    // A description that lists several variants (one clause per host kind) reads as one clause per line. A final mod keeps
    // its text whole: FinalModParts lays its effect, cost and notes out as three labelled rows.
    if(bDescription&&!ContentId.StartsWith(TEXT("final_")))
    {
        int32 Clauses=0;for(const TCHAR C:Text){Clauses+=C==TEXT('；');}
        if(Clauses>=2){Text.ReplaceInline(TEXT("；"),TEXT("；\n"));}
    }
    Out=Text;
    return true;
}
bool LDHUD::FinalModParts(const FLDDemoContentDefinitions* Content,FName FinalMod,FString& OutEffect,FString& OutCost,FString& OutNotes)
{
    OutEffect.Reset();OutCost.Reset();OutNotes.Reset();
    if(!Content||FinalMod.IsNone()){return false;}
    const FString Id=FinalMod.ToString();
    const FLDDemoDisplayDefinition* Display=Content->Display.FindByPredicate([&Id](const FLDDemoDisplayDefinition& D){return D.Id==Id;});
    if(!Display||Display->Namespace.IsEmpty()||Display->EffectKey.IsEmpty()){return false;}
    const FStringTableConstPtr Table=FStringTableRegistry::Get().FindStringTable(FName(*Display->Namespace));
    if(!Table.IsValid()){HudReportMissingText(Id);return false;}
    FString* Outs[3]={&OutEffect,&OutCost,&OutNotes};
    const FString* Keys[3]={&Display->EffectKey,&Display->CostKey,&Display->NotesKey};
    for(int32 Part=0;Part<3;++Part)
    {
        if(Keys[Part]->IsEmpty()||!Table->GetSourceString(FTextKey(**Keys[Part]),*Outs[Part])||Outs[Part]->IsEmpty())
        {OutEffect.Reset();OutCost.Reset();OutNotes.Reset();HudReportMissingText(Id);return false;}
    }
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
FString LDHUD::AbilityName(const FLDDemoContentDefinitions* Content,uint8 AbilityId)
{
    if(Content){if(const auto* Ability=Content->Abilities.FindByPredicate([AbilityId](const FLDDemoAbilityDefinition& A){return A.NativeId==AbilityId;})){return ContentName(Content,Ability->Id);}}
    return FString::Printf(TEXT("未知技能 #%d"),AbilityId);
}
FString LDHUD::RosterName(const FLDLobbyView& Lobby,const FGuid& ContenderId)
{
    const FLDLobbyMember* Member=ContenderId.IsValid()?Lobby.Members.FindByPredicate([&ContenderId](const FLDLobbyMember& M){return M.ContenderId==ContenderId;}):nullptr;
    if(!Member||Member->Name.IsEmpty()){return FString();}
    int32 Matches=0,Ordinal=0;
    for(const FLDLobbyMember& Candidate:Lobby.Members)
    {
        if(Candidate.Name!=Member->Name){continue;}
        ++Matches;
        if(&Candidate==Member){Ordinal=Matches;}
    }
    return Matches>1?FString::Printf(TEXT("%s %d"),*Member->Name,Ordinal):Member->Name;
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
LDHUD::FDeathView LDHUD::Death(const FLDOwnerState& Own,bool bClock,int64 NowMs,bool bFinalEliminated,const FLDCombatOwnerView* Combat)
{
    FDeathView View;
    if(Own.LifePhase!=ELDLifePhase::Dead){return View;}
    View.bVisible=true;View.Title=LDTerms::DeathTitle;
    if(bFinalEliminated)
    {
        View.Countdown=TEXT("终局已出局");
        View.Note=TEXT("本局不再复活");
        return View;
    }
    const FString Wait(LDTerms::RespawnWait);
    View.Countdown=Own.RespawnAtMs<=0?Wait+TEXT(" · 等待复活时间"):!bClock?Wait+TEXT(" · 时间同步中"):Wait+TEXT(" ")+TenthsUp(Own.RespawnAtMs-NowMs);
    View.bWaiting=Own.RespawnAtMs>0;
    if(View.bWaiting&&bClock)
    {
        const int64 Remaining=FMath::Max<int64>(0,Own.RespawnAtMs-NowMs);
        View.Clock=Clock(Remaining);
        if(Combat&&Own.Match!=0&&Own.Life!=0&&Combat->Generation==Own.Match&&Combat->Life==Own.Life&&!Combat->bHasBody
            &&Combat->RespawnDurationMs>0&&Combat->RespawnDurationMs<=Own.RespawnAtMs)
        {
            View.bProgressKnown=true;
            View.Progress=float(FMath::Clamp(double(Remaining)/double(Combat->RespawnDurationMs),0.,1.));
        }
    }
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
    if(Timeline.bCountdownPaused){NowMs=FMath::Min(NowMs,Timeline.CountdownPausedAtMs);}
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
    if(Timeline.bCountdownPaused){NowMs=FMath::Min(NowMs,Timeline.CountdownPausedAtMs);}
    if(Public.Generation==0||Timeline.Generation!=Public.Generation){return View;}
    // Whole seconds rounded up, so a running step never reads 0 while it lasts.
    const auto Seconds=[](int64 Ms){return Ms>0?(Ms+999)/1000:int64(0);};
    switch(Timeline.Stage)
    {
    case ELDMatchStage::Field:
    {
        const FString Left=bClock?Clock(Timeline.FieldEndT-LDMatchTimeline::FieldNowFromView(Timeline,NowMs)):FString(TEXT("同步中"));
        View.Headline=LDTerms::FieldSegmentLine(int32(Timeline.FieldSegment)+1,int32(FLDTimelineParameters::TimedSegments),Left);
        if(Timeline.bCountdownPaused){View.Headline+=TEXT(" · 计时已暂停");}
        return View;
    }
    case ELDMatchStage::FinalField:
        if(!Public.Showdown.bStarted){View.Headline=LDTerms::FinalField;return View;}
        View.Headline=FString(TEXT("终局争旗 · "))+(bClock?Clock(Public.Showdown.EndsAtMs-NowMs):FString(TEXT("时间同步中")));
        if(Timeline.bCountdownPaused){View.Headline+=TEXT(" · 计时已暂停");}
        if(Public.Showdown.bStarted&&bClock)
        {
            if(NowMs>=Public.Showdown.EndsAtMs-10000){View.Detail=TEXT("最后十秒");}
            else if(NowMs>=Public.Showdown.ClosureAtMs){View.Detail=TEXT("封街收缩中 · 圈外持续受伤");}
            else{View.Detail=FString::Printf(TEXT("封街倒计时 %lld 秒"),Seconds(Public.Showdown.ClosureAtMs-NowMs));}
            if(NowMs>=Public.Showdown.RespawnsCloseAtMs){View.Detail+=TEXT(" · 不再接受新复活");}
        }
        return View;
    case ELDMatchStage::Rest:
        break;
    default:
        return View;
    }
    View.Headline=LDTerms::RestLabel(Timeline.RestIndex)+TEXT(" · ")+RestStepText(Timeline.Step)+TEXT(" · ")
        +(!bClock?FString(TEXT("时间同步中"))
        :FString::Printf(TEXT("%s %lld 秒"),Timeline.Step==ELDRestStep::PK?TEXT("最长剩"):TEXT("剩"),Seconds(Timeline.StepEndsAtMs-NowMs)));
    if(Timeline.RestIndex==FLDTimelineParameters::TimedSegments)
    {
        View.Headline=FString(TEXT("最终休息 · "))+RestStepText(Timeline.Step)+TEXT(" · ")
            +(bClock?Clock(Timeline.StepEndsAtMs-NowMs):FString(TEXT("时间同步中")));
        return View;
    }
    if(Timeline.bCountdownPaused){View.Headline+=TEXT(" · 计时已暂停");}
    TArray<FString> Detail;
    if(Timeline.Step==ELDRestStep::Build)
    {
        View.bSignupStep=true;
        View.bSignedUp=Self.IsValid()&&Timeline.Signups.Contains(Self);
        if(!bClock){Detail.Add(TEXT("PK 报名时间同步中"));}
        else if(NowMs<Timeline.SignupOpensAtMs)
        {
            Detail.Add(LDTerms::SignupOpensIn(Seconds(Timeline.SignupOpensAtMs-NowMs)));
        }
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
        // #222: builds stay open through the PK steps, so no 配装已锁定 line; only the NPC services wait for the payout.
        Detail.Add(Timeline.bPKSkipped?FString(LDTerms::PKSkipped):LDTerms::PKEntrants(Timeline.LockedEntrants));
    }
    View.Detail=FString::Join(Detail,TEXT(" · "));
    return View;
}
bool LDHUD::TimelineDetailVisible(const FLDPublicMatchView& Public,const FTimelineView& Timeline,bool bChoicesRestOpen)
{
    const bool bFieldPhase=Public.Phase==ELDMatchPhase::Development||Public.Phase==ELDMatchPhase::CaptureOpen;
    const bool bShowdown=bFieldPhase&&Public.Showdown.bStarted&&Public.Timeline.Stage==ELDMatchStage::FinalField;
    const bool bRestPlanning=Public.Phase==ELDMatchPhase::Rest&&Public.Timeline.Step==ELDRestStep::Build;
    return (!bFieldPhase||bShowdown)&&!bChoicesRestOpen&&!Timeline.Detail.IsEmpty()
        &&(Public.Phase!=ELDMatchPhase::Rest||bRestPlanning);
}
FString LDHUD::RestReadyText(ELDRestReadyResult Result)
{
    switch(Result)
    {
    case ELDRestReadyResult::Accepted: return TEXT("已准备。");
    case ELDRestReadyResult::NotContender: return TEXT("准备失败：你不是本局参赛者。");
    case ELDRestReadyResult::StaleMatch: return TEXT("准备失败：对局已变化，请等待同步。");
    case ELDRestReadyResult::StaleWindow: return TEXT("准备失败：本次配装窗口已结束。");
    case ELDRestReadyResult::NotOpen: return TEXT("准备失败：当前不能提交准备。");
    default: return TEXT("准备失败：服务器没有返回有效结果。");
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
FString LDHUD::PKFormatText(ELDPKFormat Format,uint8 SideCount,int32 Entrants)
{
    switch(Format)
    {
    case ELDPKFormat::Duel: return LDTerms::PKDuel;
    case ELDPKFormat::FreeForAll: return LDTerms::PKFreeForAll;
    case ELDPKFormat::Teams:
        return SideCount>=2&&Entrants>=int32(SideCount)&&Entrants%int32(SideCount)==0?LDTerms::PKTeams(Entrants/int32(SideCount)):FString();
    default: return FString();
    }
}
FString LDHUD::PKRelationText(const FLDPublicMatchView& Public,const FGuid& Self,const FGuid& Target)
{
    if(!Self.IsValid()||!Target.IsValid()||!HasPK(Public)||Public.PK.Format!=ELDPKFormat::Teams){return FString();}
    const FLDPKEntrantView* TargetEntry=Public.PK.FindEntrant(Target);
    if(!TargetEntry||TargetEntry->Side==0||TargetEntry->Side>Public.PK.SideCount){return FString();}
    const FLDPKEntrantView* SelfEntry=Public.PK.FindEntrant(Self);
    if(!SelfEntry||IsSpectatingPK(Public,Self)){return LDTerms::PKSide(TargetEntry->Side);}
    if(Self==Target){return FString(LDTerms::PKOwnSide);}
    return SelfEntry->Side==TargetEntry->Side?FString(LDTerms::PKTeammate):FString(LDTerms::PKEnemy);
}
bool LDHUD::HasPK(const FLDPublicMatchView& Public)
{
    return Public.Generation!=0&&Public.Phase==ELDMatchPhase::Rest&&Public.Timeline.Generation==Public.Generation
        &&Public.Timeline.Stage==ELDMatchStage::Rest&&Public.PK.Serial!=0&&Public.PK.Generation==Public.Generation;
}
bool LDHUD::IsSpectatingPK(const FLDPublicMatchView& Public,const FGuid& Self)
{
    const ELDRestStep Step=Public.Timeline.Step;
    if(!Self.IsValid()||!HasPK(Public)||(Step!=ELDRestStep::PKReady&&Step!=ELDRestStep::PK)||Public.PK.Outcome==ELDPKOutcome::NoContest){return false;}
    // Like ALDPKDirector::CanSpectate: everyone but a living entrant watches the arena.
    const FLDPKEntrantView* Own=Public.PK.FindEntrant(Self);
    return !Own||!Own->bAlive;
}
FString LDHUD::PKSideNames(const FLDPKView& PK,uint8 Side,const FLDLobbyView& Lobby)
{
    TArray<FString> Names;
    for(const FLDPKEntrantView& Entrant:PK.Entrants)
    {
        if(Entrant.Side!=Side){continue;}
        const FString Name=RosterName(Lobby,Entrant.Contender);
        Names.Add(Name.IsEmpty()?FString(LDTerms::PKUnlisted):Name);
    }
    return FString::Join(Names,TEXT("、"));
}
FString LDHUD::PKBetText(ELDPKBetResult Result)
{
    switch(Result)
    {
    case ELDPKBetResult::Placed: return TEXT("已押注（押注结束前可以改）");
    case ELDPKBetResult::NotSpectator: return TEXT("你不是本场观众，不能押注");
    case ELDPKBetResult::WindowClosed: return TEXT("现在不是押注时间，本次没有改动");
    case ELDPKBetResult::UnknownSide: return TEXT("没有这一方，本次没有押注");
    case ELDPKBetResult::StaleMatch: return TEXT("已不是当前对局或这场 PK，没有押注");
    case ELDPKBetResult::NotContender: return TEXT("不是本局参赛者，不能押注");
    case ELDPKBetResult::NoPK: return TEXT("本次休息没有 PK，不能押注");
    default: return FString::Printf(TEXT("押注结果未知 #%d"),int32(Result));
    }
}
FString LDHUD::PKThrowText(ELDPKThrowResult Result)
{
    switch(Result)
    {
    case ELDPKThrowResult::Thrown: return TEXT("已投掷：落点已标出，稍后落地");
    case ELDPKThrowResult::NotSpectator: return TEXT("你不是本场观众，不能投掷");
    case ELDPKThrowResult::NotRunning: return TEXT("PK 没在进行，不能投掷");
    case ELDPKThrowResult::CoolingDown: return TEXT("投掷还在冷却，这次没扔出去");
    case ELDPKThrowResult::OutsideArena: return TEXT("落点要在竞技场里，这次没扔出去");
    case ELDPKThrowResult::OutOfRange: return TEXT("落点超出投掷距离，这次没扔出去");
    case ELDPKThrowResult::NotGround: return TEXT("落点不是能落地的地面，这次没扔出去");
    case ELDPKThrowResult::StaleMatch: return TEXT("已不是当前对局或这场 PK，没有投掷");
    case ELDPKThrowResult::NotContender: return TEXT("不是本局参赛者，不能投掷");
    case ELDPKThrowResult::NoPK: return TEXT("本次休息没有 PK，不能投掷");
    default: return FString::Printf(TEXT("投掷结果未知 #%d"),int32(Result));
    }
}
FString LDHUD::PKThrowCooldown(int64 ReadyAtMs,bool bClock,int64 NowMs)
{
    if(ReadyAtMs<=0){return FString();}
    if(!bClock){return LDTerms::PKThrowCoolingSync;}
    return ReadyAtMs>NowMs?LDTerms::PKThrowCooling(CooldownText(ReadyAtMs-NowMs)):FString();
}
FString LDHUD::PKResultText(const FLDPKView& PK,const FLDLobbyView& Lobby)
{
    switch(PK.Outcome)
    {
    case ELDPKOutcome::Won:
    {
        const FString Names=PKSideNames(PK,PK.WinnerSide,Lobby);
        return LDTerms::PKWinners(Names.IsEmpty()?LDTerms::PKSide(PK.WinnerSide):Names);
    }
    case ELDPKOutcome::Draw: return LDTerms::PKDraw;
    case ELDPKOutcome::NoContest: return LDTerms::PKNoContest;
    default: return FString();
    }
}
TArray<LDHUD::FPKAwardRow> LDHUD::PKAwardRows(const FLDPKView& PK,const FLDLobbyView& Lobby,const FGuid& Self)
{
    TArray<FPKAwardRow> Rows;
    for(const FLDPKAwardView& Award:PK.Awards)
    {
        const int64 Total=int64(Award.Participation)+int64(Award.Winner)+int64(Award.Bet);
        if(Total<=0){continue;}
        TArray<FString> Parts;
        if(Award.Participation>0){Parts.Add(FString::Printf(TEXT("%s +%d"),LDTerms::PKAwardParticipation,int32(Award.Participation)));}
        if(Award.Winner>0){Parts.Add(FString::Printf(TEXT("%s +%d"),LDTerms::PKAwardWinner,int32(Award.Winner)));}
        if(Award.Bet>0){Parts.Add(FString::Printf(TEXT("%s +%d"),LDTerms::PKAwardBet,int32(Award.Bet)));}
        Parts.Add(LDTerms::PKAwardTotal(Total));
        FPKAwardRow& Row=Rows.AddDefaulted_GetRef();
        const FString Name=RosterName(Lobby,Award.Contender);
        Row.Name=Name.IsEmpty()?FString(LDTerms::PKUnlisted):Name;
        Row.Points=FString::Join(Parts,TEXT(" · "));
        Row.Total=Total;
        Row.bSelf=Self.IsValid()&&Award.Contender==Self;
    }
    return Rows;
}
int64 LDHUD::PKRingElapsedMs(const FLDPKView& PK,bool bClock,int64 NowMs)
{
    return bClock&&PK.FightStartedAtMs>0&&NowMs>PK.FightStartedAtMs?NowMs-PK.FightStartedAtMs:0;
}
LDHUD::FPKPanelView LDHUD::PKPanel(const FLDPublicMatchView& Public,const FLDLobbyView& Lobby,const FPKPanelInput& In)
{
    FPKPanelView View;
    const FLDPKView& PK=Public.PK;
    const int64 TimelineNow=Public.Timeline.bCountdownPaused?FMath::Min(In.NowMs,Public.Timeline.CountdownPausedAtMs):In.NowMs;
    const ELDRestStep Step=Public.Timeline.Step;
    if(!HasPK(Public)||Step<ELDRestStep::Draw||Step>ELDRestStep::Payout){return View;}
    View.bVisible=true;
    const int32 Entrants=PK.Entrants.Num();
    const FString Format=PKFormatText(PK.Format,PK.SideCount,Entrants);
    View.Title=LDTerms::PKTitle(PK.Serial,Format.IsEmpty()?FString(LDTerms::PKFormatPending):Format,Entrants);
    const bool bDecided=PK.Outcome==ELDPKOutcome::Won||PK.Outcome==ELDPKOutcome::Draw||PK.Outcome==ELDPKOutcome::NoContest;
    const bool bRunning=PK.Outcome==ELDPKOutcome::Running;
    // Whole seconds rounded up, like the timeline banner.
    const auto Seconds=[](int64 Ms){return Ms>0?(Ms+999)/1000:int64(0);};
    if(Step==ELDRestStep::PKReady||(Step==ELDRestStep::PK&&!bDecided))
    {
        if(!In.bClock){View.Countdown=LDTerms::PKClockSync;}
        // The authority starts the fight; a local clock that reaches the end first holds 1.
        else if(Step==ELDRestStep::PKReady){View.Countdown=LDTerms::PKStartsIn(FMath::Max<int64>(1,Seconds(Public.Timeline.StepEndsAtMs-TimelineNow)));}
        else{View.Countdown=LDTerms::PKTimeLeft(Seconds(Public.Timeline.StepEndsAtMs-TimelineNow));}
    }
    if(Step==ELDRestStep::PK&&bRunning&&In.Params&&In.bClock&&PK.FightStartedAtMs>0)
    {
        const FLDPKParameters& Params=*In.Params;
        const int64 Elapsed=PKRingElapsedMs(PK,In.bClock,In.NowMs);
        const auto Meters=[](double Cm){return FString::Printf(TEXT("%.1f"),Cm/100.);};
        View.Ring=LDTerms::PKRingNow(Meters(LDPKRules::RingRadiusCm(Params,Elapsed)));
        double NextCm=0;int64 NextAtMs=0;
        if(LDPKRules::RingPreview(Params,Elapsed,NextCm,NextAtMs))
        {
            View.Ring+=TEXT(" · ")+(Elapsed<Params.ShrinkStartMs?LDTerms::PKRingSoon(Meters(NextCm),Seconds(Params.ShrinkStartMs-Elapsed))
                :LDTerms::PKRingShrinking(Meters(NextCm)));
        }
    }
    const bool bFought=PK.FightStartedAtMs>0;
    const FLDPKEntrantView* Own=In.Self.IsValid()?PK.FindEntrant(In.Self):nullptr;
    const FLDPKBetView* OwnBet=In.Self.IsValid()?PK.Bets.FindByPredicate([&In](const FLDPKBetView& Bet){return Bet.Spectator==In.Self;}):nullptr;
    View.OwnBet=OwnBet?OwnBet->Side:0;
    for(int32 Side=1;Side<=int32(PK.SideCount);++Side)
    {
        FPKSideView& Row=View.Sides.AddDefaulted_GetRef();
        Row.Side=uint8(Side);
        TArray<FString> Members;
        bool bAny=false,bAnyIn=false;
        for(const FLDPKEntrantView& Entrant:PK.Entrants)
        {
            if(Entrant.Side!=Side){continue;}
            bAny=true;
            const FString Listed=RosterName(Lobby,Entrant.Contender);
            FString Name=Listed.IsEmpty()?FString(LDTerms::PKUnlisted):Listed;
            if(Entrant.bForfeited){Name+=FString(TEXT("（"))+LDTerms::PKForfeit+TEXT("）");}
            else if(bFought&&!Entrant.bAlive){Name+=FString(TEXT("（"))+LDTerms::PKOut+TEXT("）");}
            else{bAnyIn=true;}
            Members.Add(Name);
            if(Own&&Entrant.Contender==In.Self){Row.bOwnSide=true;}
        }
        int32 Bets=0;
        for(const FLDPKBetView& Bet:PK.Bets){if(Bet.Side==Side){++Bets;}}
        const FString Names=PKSideNames(PK,uint8(Side),Lobby);
        Row.BetAction=LDTerms::PKBetOn(Names.IsEmpty()?LDTerms::PKSide(Side):Names);
        Row.bOwnBet=View.OwnBet==Side;
        Row.bWinner=PK.Outcome==ELDPKOutcome::Won&&PK.WinnerSide==Side;
        Row.bOut=bAny&&!bAnyIn;
        FString Line=FString::Join(Members,TEXT("、"));
        if(PK.Format==ELDPKFormat::Teams){Line=LDTerms::PKSide(Side)+TEXT("：")+Line;}
        if(Bets>0){Line+=TEXT(" · ")+LDTerms::PKBetCount(Bets);}
        if(Row.bOwnSide){Line+=TEXT(" · ")+FString(LDTerms::PKOwnSide);}
        if(Row.bOwnBet){Line+=TEXT(" · ")+FString(LDTerms::PKOwnBet);}
        Row.Line=Line;
    }
    const bool bBettor=!Own&&In.Self.IsValid()&&PK.IsBettor(In.Self);
    View.bSpectating=IsSpectatingPK(Public,In.Self);
    if(Own)
    {
        const bool bOut=Own->bForfeited||(bFought&&!Own->bAlive);
        if(Step<=ELDRestStep::Betting){View.Role=LDTerms::PKRoleEntrant;}
        else if(Step<=ELDRestStep::PK&&!bDecided){View.Role=bOut?LDTerms::PKRoleOut:LDTerms::PKRoleFighter;}
    }
    else if(bBettor&&Step==ELDRestStep::Betting){View.Role=LDTerms::PKRoleBet;}
    else if(bBettor&&Step==ELDRestStep::PK&&bRunning){View.Role=LDTerms::PKRoleThrow;}
    else if(Step==ELDRestStep::PKReady||(Step==ELDRestStep::PK&&!bDecided)){View.Role=LDTerms::PKRoleWatch;}
    View.bCanBet=bBettor&&Step==ELDRestStep::Betting;
    View.bCanThrow=bBettor&&Step==ELDRestStep::PK&&bRunning;
    if(View.bCanThrow)
    {
        const FString Cooling=PKThrowCooldown(In.ThrowReadyAtMs,In.bClock,In.NowMs);
        View.bThrowReady=Cooling.IsEmpty()&&!In.bThrowInFlight;
        View.ThrowAction=In.bThrowInFlight?FString(LDTerms::PKThrowSending):!Cooling.IsEmpty()?Cooling
            :In.bThrowArmed?FString(LDTerms::PKThrowAim):FString(LDTerms::PKThrow);
    }
    if((Step==ELDRestStep::Settlement||Step==ELDRestStep::Payout)&&bDecided)
    {
        View.bResult=true;
        View.Result=PKResultText(PK,Lobby);
        View.bOwnWin=Own&&PK.Outcome==ELDPKOutcome::Won&&Own->Side==PK.WinnerSide;
        View.Awards=PKAwardRows(PK,Lobby,In.Self);
    }
    return View;
}
LDHUD::FCircleView LDHUD::Circle(const FLDPublicMatchView& Public,const FString& SprayerName,const FString& OwnerName,bool bClock,int64 NowMs,
    int64 SprayMs,int64 GuardMs)
{
    FCircleView View;
    switch(Public.Phase)
    {
    case ELDMatchPhase::Development:
        // #198: the opening is scheduled only when the final field begins (OpenAtMs is unscheduled until then).
        View.Line=Public.Showdown.bStarted?FString(TEXT("旗杆解锁 · "))+(!bClock?FString(TEXT("同步中")):TenthsUp(Public.OpenAtMs-NowMs))
            :FString(LDTerms::Development)+TEXT(" · ")+LDTerms::CaptureAfterLastRest;
        View.Head=View.Line;
        return View;
    case ELDMatchPhase::CaptureOpen:
    {
        const FString Sprayer=SprayerName.IsEmpty()?FString(TEXT("未列名参赛者")):SprayerName;
        // The graffiti and its guard are public in every open state; the guard runs only while its owner is alone in
        // the zone: anyone else in the zone freezes it and an empty zone pauses it.
        FString GuardState;
        float GuardProgress=0.f;
        bool bGuardProgress=false;
        if(Public.GraffitiOwner.IsValid())
        {
            View.Owner=LDTerms::GraffitiOf(OwnerName.IsEmpty()?FString(TEXT("未列名参赛者")):OwnerName);
            if(Public.CircleState!=ELDCircleState::Guarded)
            {
                GuardState=Public.CircleState==ELDCircleState::Empty?LDTerms::GuardAway:LDTerms::GuardPaused;
                if(GuardMs>0)
                {
                    GuardState+=FString(TEXT("（"))+LDTerms::GuardLeft+TEXT(" ")+TenthsUp(GuardMs-Public.GuardKeptMs)+TEXT("）");
                    bGuardProgress=true;GuardProgress=float(FMath::Clamp(double(Public.GuardKeptMs)/double(GuardMs),0.,1.));
                }
            }
            else if(!bClock){GuardState=TEXT("守护同步中");}
            else
            {
                GuardState=FString(LDTerms::GuardLeft)+TEXT(" ")+TenthsUp(Public.GuardDueAtMs-NowMs);
                if(GuardMs>0){bGuardProgress=true;GuardProgress=float(FMath::Clamp(1.-double(Public.GuardDueAtMs-NowMs)/double(GuardMs),0.,1.));}
            }
        }
        const FString Guard=View.Owner.IsEmpty()?FString():GuardState.IsEmpty()?View.Owner:View.Owner+TEXT(" · ")+GuardState;
        // Line keeps the spray part first and the whole guard after it; the panel lifts the spray part (or, without one,
        // the guard state) into Head and puts the owner and the remaining guard state under the bar.
        const auto Finish=[&View,&Guard,&GuardState](const FString& Spray)
        {
            View.Line=Guard.IsEmpty()?Spray:Spray.IsEmpty()?Guard:Spray+TEXT(" · ")+Guard;
            if(Spray.IsEmpty()){View.Head=GuardState;}else{View.Head=Spray;View.Aside=GuardState;}
            if(View.Head.IsEmpty()){View.Head=View.Owner;View.Owner.Reset();}
        };
        switch(Public.CircleState)
        {
        case ELDCircleState::Empty:
            Finish(Guard.IsEmpty()?FString(LDTerms::CircleEmpty):FString());
            View.bProgress=bGuardProgress;View.Progress=GuardProgress;
            return View;
        case ELDCircleState::Guarded:
            Finish(FString());
            View.bProgress=bGuardProgress;View.Progress=GuardProgress;
            return View;
        case ELDCircleState::Contested:
        {
            // A contest pauses the holder's spray progress instead of clearing it; without a holder nothing is kept.
            if(!Public.Occupier.IsValid()||Public.PausedProgressMs<=0)
            {
                Finish(FString(LDTerms::CircleContested)+TEXT(" · ")+LDTerms::NoProgressKept);
                View.bProgress=bGuardProgress;View.Progress=GuardProgress;
                return View;
            }
            const FString Kept=FString::Printf(TEXT("%.1f"),double(Public.PausedProgressMs)/1000.);
            if(SprayMs<=0){Finish(FString(LDTerms::CircleContested)+TEXT(" · ")+LDTerms::ProgressKept(Sprayer)+TEXT("（")+Kept+TEXT(" s）"));return View;}
            View.bProgress=true;View.Progress=float(FMath::Clamp(double(Public.PausedProgressMs)/double(SprayMs),0.,1.));
            Finish(FString(LDTerms::CircleContested)+TEXT(" · ")+LDTerms::ProgressKept(Sprayer)+TEXT(" ")+Kept+FString::Printf(TEXT("/%.1f s"),double(SprayMs)/1000.));
            return View;
        }
        case ELDCircleState::Solo:
        {
            const int64 Span=Public.DueAtMs-Public.SoloSinceMs;
            if(!bClock||Span<=0){Finish(Sprayer+TEXT(" ")+LDTerms::Holding+TEXT(" · 进度同步中"));return View;}
            View.bProgress=true;View.Progress=float(FMath::Clamp(double(NowMs-Public.SoloSinceMs)/double(Span),0.,1.));
            Finish(Sprayer+TEXT(" ")+LDTerms::Holding+TEXT(" · ")+LDTerms::HoldLeft+TEXT(" ")+TenthsUp(Public.DueAtMs-NowMs));
            return View;
        }
        default: View.Owner.Reset();View.Line=FString(LDTerms::CapturePoint)+TEXT("状态同步中");View.Head=View.Line;return View;
        }
    }
    default:
        View.Line=PhaseLine(Public.Phase);View.Head=View.Line;
        return View;
    }
}
FLinearColor LDHUD::MarkerColor()
{
    return FLinearColor(.74f,.48f,1.f);
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
    Prompt.Reset();
    auto Add=[this,Now](const FString& Text,bool bSelf,uint64 ClaimSerial=0,ELDFieldMarkerKind MarkerKind=ELDFieldMarkerKind::None,uint8 MarkerSegment=0)
    {
        if(Text.IsEmpty()){return;}
        FEventLine& Line=Shown.AddDefaulted_GetRef();Line.Text=Text;Line.bSelf=bSelf;Line.At=Now;Line.AirdropClaimSerial=ClaimSerial;
        Line.MarkerKind=MarkerKind;Line.MarkerSegment=MarkerSegment;
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
        if(In.OwnReactionAssists)
        {
            for(const FLDReactionAssistView& Assist:*In.OwnReactionAssists)
            {
                if(Assist.Serial<=ReactionAssistSerial){continue;}
                // A prompt can overtake the match or own life it belongs to, or arrive before the clock that dates it is known:
                // it waits, unconsumed, until they are. The serial is client-local and never resets, so each prompt is told
                // at most once by this feed, however late its first observation (the HUD lives as long as its controller).
                if(!In.bClock||Assist.Match>Match||(Assist.Match==Match&&(In.OwnLife==0||Assist.Life>In.OwnLife))){break;}
                ReactionAssistSerial=Assist.Serial;
                if(Assist.Match!=Match||Assist.Life!=In.OwnLife||In.NowMs-Assist.AtMs>StaleKillMs){continue;}
                Prompt=ReactionAssistText(In.Content,Assist,Name(Assist.Trigger));
                Add(Prompt,true);
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
                else if(Public.Airdrop.Phase==ELDAirdropPhase::Expired){Add(LDTerms::AirdropExpired(),false);}
            }
            if(Public.Boss.Phase!=BossPhase)
            {
                const FString Boss=In.BossName.IsEmpty()?FString(LDTerms::Boss):In.BossName;
                if(Public.Boss.Phase==ELDBossPhase::Alive){Add(LDTerms::BossAppeared(Boss),false);}
                else if(Public.Boss.Phase==ELDBossPhase::Down)
                {Add(BossDownLine(Public.Boss,In.Self,Name(Public.Boss.KilledBy),Boss),In.Self.IsValid()&&Public.Boss.KilledBy==In.Self);}
                else if(Public.Boss.Phase==ELDBossPhase::Left){Add(LDTerms::BossLeft(Boss),false);}
            }
            // #198 outer §5 camp markers: a new segment's reveal, then each marker's claim or expiry once.
            {
                bool bExpired=false;
                for(const FLDFieldMarkerView& Marker:Public.Markers)
                {
                    const int32 Slot=Marker.Kind==ELDFieldMarkerKind::High?0:Marker.Kind==ELDFieldMarkerKind::Elite?1:INDEX_NONE;
                    if(Slot==INDEX_NONE){continue;}
                    const ELDFieldMarkerPhase Before=Marker.Segment==MarkerSegment?MarkerPhases[Slot]:ELDFieldMarkerPhase::None;
                    if(Marker.Phase==Before){continue;}
                    const bool bHigh=Slot==0;
                    if(Marker.Phase==ELDFieldMarkerPhase::Revealed){Add(FString(bHigh?LDTerms::CampMarkHigh:LDTerms::CampMarkElite),false);}
                    // Labelled below from the own field rewards; the public view never says who took it.
                    else if(Marker.Phase==ELDFieldMarkerPhase::Claimed){Add(LDTerms::CampMarkTaken(bHigh),false,0,Marker.Kind,Marker.Segment);}
                    else if(Marker.Phase==ELDFieldMarkerPhase::Expired){bExpired=true;}
                }
                if(bExpired){Add(LDTerms::CampMarkExpired(),false);}
            }
            // Each newly named trending contender is announced once, the rumor day's runner-up included.
            for(const FGuid& Named:{Public.Leader.Leader,Public.Leader.RunnerUp})
            {
                if(Named.IsValid()&&Named!=Leader&&Named!=RunnerUp)
                {Add(LeaderLine(Named,In.Self,Name(Named)),In.Self.IsValid()&&Named==In.Self);}
            }
            if(Public.GraffitiOwner.IsValid()&&Public.GraffitiOwner!=GraffitiOwner)
            {
                const bool bOwn=In.Self.IsValid()&&Public.GraffitiOwner==In.Self;
                const FString Owner=bOwn?FString(TEXT("你")):Name(Public.GraffitiOwner);
                Add(LDTerms::GraffitiSprayed(Owner.IsEmpty()?FString(TEXT("未列名参赛者")):Owner),bOwn);
            }
        }
        bPrimed=true;Phase=Public.Phase;AirdropSerial=Public.Airdrop.Serial;AirdropPhase=Public.Airdrop.Phase;BossPhase=Public.Boss.Phase;
        MarkerSegment=Public.Markers.IsEmpty()?0:Public.Markers[0].Segment;MarkerPhases[0]=MarkerPhases[1]=ELDFieldMarkerPhase::None;
        for(const FLDFieldMarkerView& Marker:Public.Markers)
        {
            if(Marker.Kind==ELDFieldMarkerKind::High){MarkerPhases[0]=Marker.Phase;}
            else if(Marker.Kind==ELDFieldMarkerKind::Elite){MarkerPhases[1]=Marker.Phase;}
        }
        Leader=Public.Leader.Leader;RunnerUp=Public.Leader.RunnerUp;GraffitiOwner=Public.GraffitiOwner;GuardKeptMs=Public.GuardKeptMs;GuardDueAtMs=Public.GuardDueAtMs;
    }
    for(FEventLine& Line:Shown)
    {
        if(Line.MarkerKind!=ELDFieldMarkerKind::None)
        {
            const FName Event=Line.MarkerKind==ELDFieldMarkerKind::High?FName(TEXT("camp_marker_high")):FName(TEXT("camp_marker_elite"));
            const bool bOwn=In.OwnFieldRewards&&In.OwnFieldRewards->ContainsByPredicate([&Line,Event](const FLDFieldRewardView& Reward)
                {return Reward.Event==Event&&Reward.Serial==Line.MarkerSegment;});
            const bool bHigh=Line.MarkerKind==ELDFieldMarkerKind::High;
            Line.Text=bOwn?LDTerms::CampMarkOwnClaim(bHigh):LDTerms::CampMarkTaken(bHigh);Line.bSelf=bOwn;
            continue;
        }
        if(Line.AirdropClaimSerial==0){continue;}
        const bool bOwn=In.OwnAirdropClaimSerial==Line.AirdropClaimSerial;
        Line.Text=bOwn?LDTerms::AirdropOwnClaim():LDTerms::AirdropTaken();Line.bSelf=bOwn;
    }
    Shown.RemoveAll([Now](const FEventLine& Line){return Now-Line.At>=LineSeconds;});
    if(Shown.Num()>MaxLines){Shown.RemoveAt(0,Shown.Num()-MaxLines);}
}
FString LDHUD::LeaderLine(const FGuid& Trending,const FGuid& Self,const FString& LeaderName)
{
    if(!Trending.IsValid()){return FString();}
    if(Self.IsValid()&&Trending==Self){return LDTerms::LeaderSelf();}
    return LDTerms::LeaderNamed(LeaderName.IsEmpty()?FString(TEXT("未列名参赛者")):LeaderName);
}
FString LDHUD::KillLine(const FLDKillNotice& Notice,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName,const FString& EliteCampName,const FString& BossName,const TArray<FString>& AssisterNames)
{
    const FString Victim=VictimName.IsEmpty()?FString(TEXT("未列名参赛者")):VictimName;
    if(!Notice.CreatureName.IsEmpty()){return LDTerms::KilledByLine(Victim,Notice.CreatureName);}
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
    case ELDKillCause::CampAncient: return LDTerms::KilledByLine(Victim,TEXT("远古野怪"));
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
    if(!Latest->CreatureName.IsEmpty()){return LDTerms::KilledBy(Latest->CreatureName);}
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
    case ELDKillCause::CampAncient: return LDTerms::KilledBy(TEXT("远古野怪"));
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
    case ELDMatchEndReason::ObjectiveTimeout:
        return Result.Winner.IsValid()?FString(TEXT("终局时间到 · 目标贡献获胜 · "))+WinnerName:TEXT("终局时间到 · 无合格的唯一胜者");
    case ELDMatchEndReason::NoChampion: return TEXT("终局结束 · 没有存活的合格冠军");
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
namespace
{
FString HudDamageEntry(const FString& Name,int64 DamageP2)
{
    return FString::Printf(TEXT("%s %lld"),*Name,HudWholePoints(DamageP2,true));
}
// ELDCombatElement (fire, water, shock, chill, rot) as the status it leaves; ELDCombatReaction as its term.
const TCHAR* HudElementStatusId(uint8 Element)
{
    switch(Element)
    {
    case 1: return TEXT("burn");
    case 2: return TEXT("wet");
    case 3: return TEXT("electrified");
    case 4: return TEXT("chilled");
    case 5: return TEXT("rotten");
    default: return nullptr;
    }
}
FString HudElementState(const FLDDemoContentDefinitions* Content,uint8 Element)
{
    const TCHAR* Id=HudElementStatusId(Element);
    return Id?LDHUD::ContentName(Content,Id):FString();
}
FString HudReactionTerm(uint8 Reaction)
{
    return LDTerms::ReactionTerm(Reaction);
}
FString HudElementStateId(uint8 Element)
{
    const TCHAR* Id=HudElementStatusId(Element);
    return Id?FString(Id):FString();
}
// The icon of a damage source: its content display row's icon, the dash icon, or none for a plain basic attack.
FString HudDamageSourceIcon(const FLDDemoContentDefinitions* Content,const FLDCombatSourceRef& Source)
{
    if(Source.Kind==ELDCombatSourceKind::Innate){return LDUIIcons::Dash;}
    if(Source.DefinitionId.IsNone()){return FString();}
    switch(Source.Kind)
    {
    case ELDCombatSourceKind::Basic: case ELDCombatSourceKind::Learned: case ELDCombatSourceKind::Card: case ELDCombatSourceKind::FinalMod:
        return LDHUD::ContentIconId(Content,Source.DefinitionId.ToString());
    default: return FString();
    }
}
LDHUD::FRecapNode HudRecapNode(LDHUD::ERecapNode Kind,LDHUD::ERecapLink Link,const FString& Text,const FString& IconId=FString(),const FString& Name=FString())
{
    LDHUD::FRecapNode Node;Node.Kind=Kind;Node.Link=Link;Node.Text=Text;Node.IconId=IconId;Node.Name=Name.IsEmpty()?Text:Name;
    Node.Glyph=LDHUD::IconGlyph(Node.Name);
    return Node;
}
}
FString LDHUD::ReactionChainText(const FLDDemoContentDefinitions* Content,const FLDDamageLine& Line,const FGuid& Self,const FString& SetterName)
{
    const FString Source=DamageSourceName(Content,Line.Source);
    if(Line.Reaction==0){return Source;}
    // An Overload splash consumed nothing on this body.
    if(Line.Consumed==0){return FString::Printf(TEXT("%s → %s"),*Source,*HudReactionTerm(Line.Reaction));}
    FString Owner;
    if(Line.Setter.IsValid()&&Self.IsValid()&&Line.Setter==Self){Owner=TEXT("你的");}
    else if(Line.Setter.IsValid()&&Line.Setter!=Line.Contender){Owner=(SetterName.IsEmpty()?FString(TEXT("未列名参赛者")):SetterName)+TEXT("的");}
    return FString::Printf(TEXT("%s → %s%s → %s"),*Source,*Owner,*HudElementState(Content,Line.Consumed),*HudReactionTerm(Line.Reaction));
}
void LDHUD::ReactionChainNodes(const FLDDemoContentDefinitions* Content,const FLDDamageLine& Line,const FGuid& Self,const FString& SetterName,TArray<FRecapNode>& Out)
{
    const FString Source=DamageSourceName(Content,Line.Source);
    Out.Add(HudRecapNode(ERecapNode::Source,ERecapLink::None,FString(),HudDamageSourceIcon(Content,Line.Source),Source));
    if(Line.Reaction==0){return;}
    const FString Reaction=HudReactionTerm(Line.Reaction);
    if(Line.Consumed!=0)
    {
        FString Setter;
        if(Line.Setter.IsValid()&&Self.IsValid()&&Line.Setter==Self){Setter=TEXT("你");}
        else if(Line.Setter.IsValid()&&Line.Setter!=Line.Contender){Setter=SetterName.IsEmpty()?FString(TEXT("未列名参赛者")):SetterName;}
        if(!Setter.IsEmpty()){Out.Add(HudRecapNode(ERecapNode::Setter,ERecapLink::Arrow,Setter));}
        const FString State=HudElementState(Content,Line.Consumed);
        Out.Add(HudRecapNode(ERecapNode::State,Setter.IsEmpty()?ERecapLink::Arrow:ERecapLink::None,FString(),
            ContentIconId(Content,HudElementStateId(Line.Consumed)),State));
    }
    Out.Add(HudRecapNode(ERecapNode::Reaction,ERecapLink::Arrow,Reaction,FString(),Reaction+TEXT("：")+LDTerms::ReactionExplanation(Reaction)));
}
FString LDHUD::ReactionAssistText(const FLDDemoContentDefinitions* Content,const FLDReactionAssistView& View,const FString& TriggerName)
{
    // A trigger the setter did not see stays unnamed.
    if(!View.Trigger.IsValid()){return FString::Printf(TEXT("你的%s被引爆（%s）"),*HudElementState(Content,View.Consumed),*HudReactionTerm(View.Reaction));}
    return FString::Printf(TEXT("你的%s被%s引爆（%s）"),*HudElementState(Content,View.Consumed),
        TriggerName.IsEmpty()?TEXT("未列名参赛者"):*TriggerName,*HudReactionTerm(View.Reaction));
}
FString LDHUD::DamageSourceName(const FLDDemoContentDefinitions* Content,const FLDCombatSourceRef& Source)
{
    switch(Source.Kind)
    {
    case ELDCombatSourceKind::Basic: return Source.DefinitionId.IsNone()?FString(LDTerms::BasicAttack):ContentName(Content,Source.DefinitionId.ToString())+TEXT("技能");
    case ELDCombatSourceKind::Innate: return LDTerms::Dash;
    case ELDCombatSourceKind::Learned: case ELDCombatSourceKind::Card: case ELDCombatSourceKind::FinalMod:
        return Source.DefinitionId.IsNone()?FString(TEXT("其他来源")):ContentName(Content,Source.DefinitionId.ToString());
    default: return TEXT("其他来源");
    }
}
LDHUD::FDamageListView LDHUD::DeathRecap(const FLDDeathRecapView& Recap,const FLDOwnerState& Own,const FLDLobbyView& Lobby,const FLDDemoContentDefinitions* Content)
{
    FDamageListView View;
    if(Own.LifePhase!=ELDLifePhase::Dead||Own.Match==0||Recap.Match!=Own.Match||Recap.Life!=Own.Life||Recap.WindowMs<=0){return View;}
    View.bAvailable=true;
    const FString Window=Recap.WindowMs%1000==0?FString::Printf(TEXT("%lld"),Recap.WindowMs/1000):FString::Printf(TEXT("%.1f"),double(Recap.WindowMs)/1000.);
    if(Recap.Lines.IsEmpty())
    {
        View.Heading=FString::Printf(TEXT("近 %s 秒没有受到其他单位的伤害"),*Window);
        return View;
    }
    // The lines arrive grouped by attacker; each run of one attacker is one row.
    struct FAttacker{int32 First=0,Num=0;int64 TotalP2=0;};
    TArray<FAttacker> Attackers;
    int64 AllP2=0;
    for(int32 Index=0;Index<Recap.Lines.Num();++Index)
    {
        const FLDDamageLine& Line=Recap.Lines[Index];
        if(Attackers.IsEmpty()||!Recap.Lines[Attackers.Last().First].SameAttacker(Line)){Attackers.Add({Index,0,0});}
        FAttacker& Attacker=Attackers.Last();
        ++Attacker.Num;Attacker.TotalP2+=Line.DamageP2;AllP2+=Line.DamageP2;
    }
    View.Heading=FString::Printf(TEXT("近 %s 秒承受伤害 %lld"),*Window,HudWholePoints(AllP2,true));
    const int32 Shown=Attackers.Num()>DeathRecapAttackersMax?DeathRecapAttackersMax-1:Attackers.Num();
    for(int32 Index=0;Index<Shown;++Index)
    {
        const FAttacker& Attacker=Attackers[Index];
        const FLDDamageLine& Head=Recap.Lines[Attacker.First];
        FString Name=Head.Contender.IsValid()?RosterName(Lobby,Head.Contender):!Head.MonsterId.IsNone()?ContentName(Content,Head.MonsterId.ToString()):FString(TEXT("其他来源"));
        if(Name.IsEmpty()){Name=TEXT("未列名参赛者");}
        TArray<FString> Parts;Parts.Add(HudDamageEntry(Name,Attacker.TotalP2));
        TArray<FRecapNode>& Nodes=View.Chains.AddDefaulted_GetRef().Nodes;
        Nodes.Add(HudRecapNode(ERecapNode::Attacker,ERecapLink::None,Name));
        Nodes.Add(HudRecapNode(ERecapNode::Amount,ERecapLink::None,FString::Printf(TEXT("%lld"),HudWholePoints(Attacker.TotalP2,true))));
        const int32 Sources=Attacker.Num>DeathRecapSourcesMax?DeathRecapSourcesMax-1:Attacker.Num;
        int64 RestP2=0;
        for(int32 Offset=0;Offset<Attacker.Num;++Offset)
        {
            const FLDDamageLine& Line=Recap.Lines[Attacker.First+Offset];
            if(Offset<Sources)
            {
                const FString SetterName=Line.Setter.IsValid()?RosterName(Lobby,Line.Setter):FString();
                Parts.Add(HudDamageEntry(ReactionChainText(Content,Line,Own.ContenderId,SetterName),Line.DamageP2));
                const int32 Start=Nodes.Num();
                ReactionChainNodes(Content,Line,Own.ContenderId,SetterName,Nodes);
                Nodes[Start].Link=ERecapLink::Gap;
                Nodes.Add(HudRecapNode(ERecapNode::Amount,ERecapLink::None,FString::Printf(TEXT("%lld"),HudWholePoints(Line.DamageP2,true))));
            }
            else{RestP2+=Line.DamageP2;}
        }
        if(RestP2>0)
        {
            Parts.Add(HudDamageEntry(TEXT("其他"),RestP2));
            Nodes.Add(HudRecapNode(ERecapNode::Rest,ERecapLink::Gap,HudDamageEntry(TEXT("其他"),RestP2)));
        }
        const bool bKiller=Head.Contender.IsValid()?Head.Contender==Recap.Killer
            :!Head.MonsterId.IsNone()&&Head.MonsterId==Recap.KillerMonsterId;
        const FString Tag=bKiller?FString(TEXT("击杀")):Head.Contender.IsValid()&&Recap.Assisters.Contains(Head.Contender)?FString(LDTerms::Assist):FString();
        if(!Tag.IsEmpty()){Parts.Add(Tag);Nodes.Add(HudRecapNode(ERecapNode::Tag,ERecapLink::Gap,Tag));}
        View.Rows.Add(FString::Join(Parts,TEXT(" · ")));
    }
    if(Shown<Attackers.Num())
    {
        int64 RestP2=0;
        for(int32 Index=Shown;Index<Attackers.Num();++Index){RestP2+=Attackers[Index].TotalP2;}
        View.Rows.Add(HudDamageEntry(FString::Printf(TEXT("其他 %d 个攻击者"),Attackers.Num()-Shown),RestP2));
        View.Chains.AddDefaulted_GetRef().Nodes.Add(HudRecapNode(ERecapNode::Rest,ERecapLink::None,View.Rows.Last()));
    }
    return View;
}
LDHUD::FDamageListView LDHUD::DamageSummary(const FLDDamageSummaryView& Summary,const FLDResultView& Result,const FLDDemoContentDefinitions* Content)
{
    FDamageListView View;
    if(!Result.ResultId.IsValid()||Summary.ResultId!=Result.ResultId||Summary.Match!=Result.Generation){return View;}
    View.bAvailable=true;
    if(Summary.Lines.IsEmpty()){View.Heading=TEXT("你本局没有造成伤害");return View;}
    int64 AllP2=0,RestP2=0;
    const int32 Shown=Summary.Lines.Num()>DamageSummarySourcesMax?DamageSummarySourcesMax-1:Summary.Lines.Num();
    for(int32 Index=0;Index<Summary.Lines.Num();++Index)
    {
        const FLDDamageLine& Line=Summary.Lines[Index];
        AllP2+=Line.DamageP2;
        if(Index<Shown){View.Rows.Add(HudDamageEntry(DamageSourceName(Content,Line.Source),Line.DamageP2));}else{RestP2+=Line.DamageP2;}
    }
    if(RestP2>0){View.Rows.Add(HudDamageEntry(TEXT("其他"),RestP2));}
    View.Heading=FString::Printf(TEXT("你造成的伤害 %lld"),HudWholePoints(AllP2,true));
    return View;
}

FString LDHUD::ChainStepName(const FLDDemoContentDefinitions* Content,FName Step)
{
    const FString Id=Step.ToString();
    if(Step==LDChain::KillStep()){return TEXT("击杀");}
    if(Id==TEXT("basic")){return LDTerms::BasicAttack;}
    if(Id.StartsWith(TEXT("reaction_")))
    {
        for(uint8 Index=1;Index<LDCombatReactionCount;++Index)
        {
            if(LDTerms::ReactionTerm(Index).IsEmpty()){continue;}
            if(LDChain::ReactionStep(LDCombatElements::ReactionName(ELDCombatReaction(Index)))==Step){return LDTerms::ReactionTerm(Index);}
        }
        return Id;
    }
    if(Id.StartsWith(TEXT("ground_")))
    {
        for(uint8 Index=1;Index<UE_ARRAY_COUNT(LDTerms::GroundNames);++Index)
        {
            if(LDChain::GroundStep(LDCombatElements::GroundName(ELDGroundStateKind(Index)))==Step){return LDTerms::GroundNames[Index];}
        }
        return Id;
    }
    return ContentName(Content,Id);
}
FString LDHUD::ChainKeyText(const FLDDemoContentDefinitions* Content,const TArray<FName>& Key)
{
    return FString::JoinBy(Key,TEXT(" → "),[Content](FName Step){return ChainStepName(Content,Step);});
}
FString LDHUD::NewChainNotice(const FLDDemoContentDefinitions* Content,const TArray<FName>& Key)
{
    return TEXT("新连锁：")+ChainKeyText(Content,Key);
}
LDHUD::FResultHighlightsView LDHUD::ResultHighlights(const FLDDamageSummaryView& Summary,const FLDResultView& Result,const FGuid& Self,
    const FLDDemoContentDefinitions* Content)
{
    FResultHighlightsView View;
    if(!Result.ResultId.IsValid()||Summary.ResultId!=Result.ResultId||Summary.Match!=Result.Generation){return View;}
    View.bAvailable=true;
    for(const FLDResultRow& Row:Result.Rows)
    {
        if(View.Rows.Num()>=ResultHighlightsMax){break;}
        const FLDResultHighlight* Highlight=Summary.Highlights.FindByPredicate([&Row](const FLDResultHighlight& Each){return Each.Contender==Row.ContenderId;});
        FResultHighlightRow& Out=View.Rows.AddDefaulted_GetRef();
        Out.Name=Row.Name;
        Out.bOwn=Row.ContenderId==Self;
        Out.Chain=Highlight&&Highlight->LongestSteps>0
            ?FString::Printf(TEXT("%s（%d 步）"),*ChainKeyText(Content,Highlight->LongestKey),Highlight->LongestSteps):FString(TEXT("—"));
        if(Highlight&&Highlight->BestDamageP2>0)
        {
            const FString Source=ChainStepName(Content,Highlight->BestSource);
            const int64 Points=HudWholePoints(Highlight->BestDamageP2,true);
            const int64 Percent=Highlight->BestTargetMaxHpP2>0?(Highlight->BestDamageP2*100+Highlight->BestTargetMaxHpP2/2)/Highlight->BestTargetMaxHpP2:0;
            Out.Best=FString::Printf(TEXT("%s %lld（%lld%%）"),*Source,Points,Percent);
        }
        else{Out.Best=TEXT("—");}
    }
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
    if(bActive&&Context.bMomentumKnown&&Input.MomentumCostP2>Context.MomentumP2)
    {
        Block(ActionMomentum,LDTerms::MomentumShort());
        // The gap rounds up, so a slot never reads one point closer than it is.
        View.Gap=FString::Printf(TEXT("-%lld"),(Input.MomentumCostP2-FMath::Max<int64>(Context.MomentumP2,0)+99)/100);
    }
    if(Input.bProjected&&!Input.ServerBlock.IsEmpty()){Block(ActionServer,Input.ServerBlock);}
    View.State=State.IsEmpty()?FString(TEXT("就绪")):State;
    View.Countdown=CooldownText(View.CooldownRemainingMs);
    if(bActive&&Input.MomentumCostP2>0){View.Cost=FString::Printf(TEXT("%lld"),HudWholePoints(Input.MomentumCostP2,false));}
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
    {Lines.Add(FString::Printf(TEXT("朝方向%s出 %d 厘米，碰到阻挡就停下；%s时不能%s"),LDTerms::Dash,Input.RangeCm,LDTerms::Root,LDTerms::Dash));}
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
    if(Blocks&ActionMomentum){return EActionLook::MomentumShort;}
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
    Card.bMomentum=(Unit.AuthorizedFieldMask&LDObservation::Momentum)!=0;
    if(Card.bMomentum)
    {
        // The public bar is a 0..255 fraction: the card shows its share, and a bar at the public overdrive threshold (a possible
        // overdrive) glows.
        Card.Momentum.bKnown=true;
        Card.Momentum.Fraction=float(Unit.MomentumBar)/float(LDObservation::MomentumBarFull);
        Card.Momentum.Text=FString::Printf(TEXT("%s %d%%"),LDTerms::Momentum,FMath::RoundToInt(Card.Momentum.Fraction*100.f));
        const int64 MaxP2=Content?Content->Parameters.Momentum.MaxP2:0;
        const int64 ReadyBar=MaxP2>0?FMath::DivideAndRoundUp<int64>(Content->Parameters.Overdrive.ThresholdP2*LDObservation::MomentumBarFull,MaxP2)
            :int64(LDObservation::MomentumBarFull);
        Card.bMomentumFull=int64(Unit.MomentumBar)>=ReadyBar;
        Card.bCharging=Unit.bCharging;
    }
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
        if(!Chip.bEmpty&&Content)
        {
            if(const auto* Ability=Content->Abilities.FindByPredicate([Id](const FLDDemoAbilityDefinition& A){return A.NativeId==Id;}))
            {Chip.IconId=ContentIconId(Content,Ability->Id);}
        }
    }
    return Card;
}
FString LDHUD::ObservedUnit(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec)
{
    const FObservedCard Card=ObservedCard(Unit,ContenderName,Content,SampledAtServerSec);
    FString Text=Card.Title+TEXT("\n")+Card.Health.Text;
    if(Card.bMomentum){Text+=TEXT(" · ")+Card.Momentum.Text;}
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
    case ELDDemoBonusStat::MomentumRegenFlat: return LDTerms::MomentumRegen();
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
    if(Effect.ArmorRatioBp>0){Text+=TEXT(" + ")+HudPercent(Effect.ArmorRatioBp)+LDTerms::Armor;}
    return Text;
}
}
FString LDHUD::StatLine(const FLDDemoStatBonus& Stat)
{
    const FString Name=HudBonusStatName(Stat.Stat);
    if(Name.IsEmpty()){return FString::Printf(TEXT("未知属性 #%d"),int32(Stat.Stat));}
    switch(Stat.Stat)
    {
    case ELDDemoBonusStat::AttackRangeCmFlat: return Name+TEXT(" +")+HudRational(Stat.Cm)+TEXT(" 厘米");
    case ELDDemoBonusStat::HPRegenFlat: case ELDDemoBonusStat::MomentumRegenFlat: return Name+TEXT(" +")+HudRational(Stat.Cm)+TEXT("/s");
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
    case ELDDemoEffectOp::Lifesteal: return FString(LDTerms::BasicAttack)+TEXT("打掉的")+LDTerms::Health+TEXT("的 ")+HudPercent(Effect.RatioBp)+TEXT(" 回复自身");
    case ELDDemoEffectOp::Dash: return TEXT("朝方向冲刺 ")+HudRational(Effect.DistanceCm)+TEXT(" 厘米（")+HudSeconds(Effect.DurationMs)+TEXT("）");
    case ELDDemoEffectOp::Slow:
    {
        return FString(LDTerms::Slow)+TEXT("：")+LDTerms::MoveSpeed+TEXT(" -")+HudPercent(Effect.RatioBp)+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);
    }
    case ELDDemoEffectOp::TimedStatBonus:
    {
        FString Text;for(const FLDDemoStatBonus& Stat:Effect.Stats){Text+=(Text.IsEmpty()?TEXT(""):TEXT("，"))+StatLine(Stat);}
        return Text+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);
    }
    case ELDDemoEffectOp::SelfArea: return TEXT("范围：出手位置周围 ")+HudRational(Effect.RadiusCm)+TEXT(" 厘米内的敌人");
    case ELDDemoEffectOp::Taunt: return FString(LDTerms::Taunt)+TEXT(" ")+HudSeconds(Effect.DurationMs)+TEXT("：自身周围 ")+HudRational(Effect.RadiusCm)+TEXT(" 厘米内的敌人");
    case ELDDemoEffectOp::Disarm: return FString(LDTerms::Disarm)+TEXT(" ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::Stun: return FString(LDTerms::Stun)+TEXT(" ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::EmpowerBasic:
        return HudSeconds(Effect.DurationMs)+TEXT(" 内下一次")+LDTerms::BasicAttack+TEXT("的主击额外造成 ")+HudScaling(Effect);
    case ELDDemoEffectOp::Thorns:
    {
        FString Text=TEXT("被 ")+HudRational(Effect.RadiusCm)+TEXT(" 厘米内敌人的")+LDTerms::BasicAttack+TEXT("打中时")
            +TEXT("，对方受")+HudDamageTerm(Effect)+TEXT(" ")+HudPoints(Effect.BaseP2);
        if(Effect.ArmorRatioBp>0){Text+=TEXT(" + ")+HudPercent(Effect.ArmorRatioBp)+LDTerms::Armor;}
        if(Effect.MaxHPRatioBp>0){Text+=TEXT(" + ")+HudPercent(Effect.MaxHPRatioBp)+LDTerms::MaxHealth();}
        if(Effect.CriticalMultiplierBp>10000){Text+=TEXT("，被")+FString(LDTerms::Crit)+TEXT("时变为 ")+HudPercent(Effect.CriticalMultiplierBp);}
        Text+=TEXT("；同一人 ")+HudSeconds(Effect.IntervalMs)+TEXT(" 内最多一次");
        if(Effect.SecondaryRatioBp>0){Text+=TEXT("；反伤使对方")+FString(LDTerms::MoveSpeed)+TEXT(" -")+HudPercent(Effect.SecondaryRatioBp)+TEXT("，持续 ")+HudSeconds(Effect.SecondaryDurationMs);}
        return Text;
    }
    case ELDDemoEffectOp::TriggeredShield:
        return TEXT("受到伤害时（内置冷却 ")+HudSeconds(Effect.IntervalMs)+TEXT("）获得")
            +LDTerms::Shield+TEXT(" ")+HudScaling(Effect)+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::DispelSlow: return FString(TEXT("解除自身全部"))+LDTerms::Slow;
    default: return FString::Printf(TEXT("未知效果 #%d"),Effect.Opcode);
    }
}
FString LDHUD::TargetLine(const FLDDemoTargetDefinition& Target)
{
    switch(Target.Mode)
    {
    case ELDDemoTargetMode::Self: return TEXT("对自身释放");
    case ELDDemoTargetMode::Unit: return TEXT("指向单位 · 射程 ")+HudRational(Target.RangeCm)+TEXT(" 厘米");
    case ELDDemoTargetMode::DirectionProjectile: return TEXT("方向弹道 · 射程 ")+HudRational(Target.RangeCm)+TEXT(" 厘米");
    case ELDDemoTargetMode::DirectionCone: return TEXT("方向扇形 · 射程 ")+HudRational(Target.RangeCm)+TEXT(" 厘米 · ")+HudRational(Target.AngleDegrees)+TEXT("°");
    case ELDDemoTargetMode::DirectionLine:
    {
        FLDDemoRational Width=Target.LineHalfWidthCm;
        Width.Numerator*=2;
        return TEXT("方向直线 · 长 ")+HudRational(Target.RangeCm)+TEXT(" 厘米 · 宽 ")+HudRational(Width)+TEXT(" 厘米");
    }
    case ELDDemoTargetMode::DirectionDash: return TEXT("方向冲刺");
    case ELDDemoTargetMode::GroundPoint: return TEXT("指定地点 · 最远 ")+HudRational(Target.RangeCm)+TEXT(" 厘米");
    default: return TEXT("未知目标方式");
    }
}
FString LDHUD::CostLine(const FLDDemoAbilityValues& Values)
{
    return FString(LDTerms::Momentum)+TEXT(" ")+HudPoints(Values.MomentumCostP2)+TEXT(" · 冷却 ")+HudSeconds(Values.CooldownMs);
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
    return Stat==ELDDemoBonusStat::AttackRangeCmFlat||Stat==ELDDemoBonusStat::HPRegenFlat||Stat==ELDDemoBonusStat::MomentumRegenFlat;
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
        if(Stats.bShowScalingFormula&&(Effect->ADRatioBp>0||Effect->APRatioBp>0||Effect->MaxHPRatioBp>0||Effect->ArmorRatioBp>0))
        {
            Out=HudScaling(*Effect);
            if(Token.Field==EField::Burst){Out=TEXT("(")+Out+TEXT(") × ")+HudPercent(Effect->SecondaryRatioBp);}
            return true;
        }
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
    case EField::Multiplier: Out=HudDecimal(double(Effect->MultiplierBp)/100.);return true;
    case EField::CostRatio: Out=HudDecimal(double(Effect->CostRatioBp)/100.);return true;
    case EField::Angle: Out=HudRational(Effect->AngleDegrees);return true;
    // A09: the attack-speed step in attacks per second.
    case EField::Step: Out=HudRational(Effect->StepRate);return true;
    case EField::Threshold: Out=HudRational(Effect->ExtraDistanceCm);return true;
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
    const FLDDemoSkillRankParameters& Ranks,int64 GrowthPerLevelBp,uint8 Rank)
{
    FLDDemoAbilityValues Grown;
    if(!LDSkillGrowth::GrowValues(Values,Ranks,GrowthPerLevelBp,Stats.Level,Rank,Grown))
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
    const FCardText Sentence=CardText(Display->DescriptionText,Ability->Values,Stats,Content->Parameters.SkillRanks,
        Content->Parameters.SkillGrowthPerLevelBp,1);
    Card.Sentence=Sentence.Rich;Card.SentencePlain=Sentence.Plain;
    Card.Keywords=KeywordLines(Sentence.Keywords);
    Card.Target=TargetLine(Ability->Target);
    Card.Cost=CostLine(Ability->Values);
    Card.bKnown=Sentence.bComplete;
    return Card;
}
LDHUD::FFinalModCardView LDHUD::FinalModCard(FName DefinitionId,const FLDDemoContentDefinitions* Content,const FCardStats& Stats)
{
    FFinalModCardView View;
    const FString Id=DefinitionId.ToString();
    View.Name=ContentName(Content,Id);View.Glyph=IconGlyph(View.Name);View.Detail.Name=View.Name;
    const auto* Card=Content?Content->FinalMods.FindByPredicate([&Id](const FLDDemoFinalModDefinition& Row){return Row.Id==Id;}):nullptr;
    const auto* Display=Content?Content->Display.FindByPredicate([&Id](const FLDDemoDisplayDefinition& Row){return Row.Id==Id;}):nullptr;
    if(!Card||!Display){View.Detail.Body=TEXT("终局改装数据未就绪。");return View;}
    View.IconId=Display->IconId;View.Detail.Summary=TEXT("终局改装 · 一局一次");
    const auto Decimal=[](int64 Value,int64 Divisor){return HudDecimal(double(Value)/double(Divisor));};
    const auto Sec=[&Decimal](int64 Value){return Decimal(Value,1000)+TEXT("s");};
    const auto Percent=[&Decimal](int64 Value){return Decimal(Value,100)+TEXT("%");};
    const auto Cm=[](const FLDDemoRational& Value){return HudDecimal(double(Value.Numerator)/double(Value.Denominator))+TEXT(" 厘米");};
    int64 Base=0;
    if(!LDSkillGrowth::GrowP2(Card->BaseP2,LDSkillGrowth::FactorBp(Content->Parameters.SkillGrowthPerLevelBp,Stats.Level),Base))
    {
        View.Detail.Body=TEXT("终局改装成长数值无效。");
        HudReportMissingText(Id+TEXT(".growth"));return View;
    }
    FString Damage=Decimal(Base,100);
    if(Card->ADRatioBp>0){Damage+=TEXT(" + ")+Decimal(Card->ADRatioBp,10000)+TEXT(" AD");}
    if(Card->APRatioBp>0){Damage+=TEXT(" + ")+Decimal(Card->APRatioBp,10000)+TEXT(" AP");}
    FString Numbers;
    switch(Card->GetNativeId())
    {
    case ELDDemoFinalModId::FinalLandlord:
        Numbers=FString::Printf(TEXT("每 %s 首次普攻触发；延迟 %s，半径 %s。\n魔法伤害 %s；持旗普攻主击伤害为 %s。"),
            *Sec(Card->IntervalMs),*Sec(Card->DelayMs),*Cm(Card->RadiusCm),*Damage,*Percent(Card->DamageScaleBp));break;
    case ELDDemoFinalModId::FinalBreach:
        Numbers=FString::Printf(TEXT("首次圈外准备 %s，再次 %s；入圈窗口 %s，外推 %s。\n代价：D 基础冷却增加 %s。"),
            *Sec(Card->OutsideMs),*Sec(Card->RearmOutsideMs),*Sec(Card->WindowMs),*Cm(Card->DistanceCm),*Sec(Card->CooldownCostMs));break;
    case ELDDemoFinalModId::FinalComeback:
        Numbers=FString::Printf(TEXT("入圈窗口 %s；半径 %s，持续 %s，每 %s 一跳。\n每跳魔法伤害 %s，减速 %s 持续 %s；间隔 %s。\n代价：触发那次直接伤害为 %s。"),
            *Sec(Card->WindowMs),*Cm(Card->RadiusCm),*Sec(Card->DurationMs),*Sec(Card->PulseIntervalMs),
            *Damage,*Percent(Card->SlowBp),*Sec(Card->SlowMs),*Sec(Card->IntervalMs),*Percent(Card->DamageScaleBp));break;
    case ELDDemoFinalModId::FinalOpenMic:
        Numbers=FString::Printf(TEXT("%s 内两次独立命中；半径 %s，持续 %s，每 %s 一跳。\n每跳魔法伤害 %s，减速 %s 持续 %s。\n代价：声浪期间本人移速降低 %s。"),
            *Sec(Card->WindowMs),*Cm(Card->RadiusCm),*Sec(Card->DurationMs),*Sec(Card->PulseIntervalMs),
            *Damage,*Percent(Card->SlowBp),*Sec(Card->SlowMs),*Percent(Card->MoveSpeedCostBp));break;
    case ELDDemoFinalModId::FinalBowling:
        Numbers=FString::Printf(TEXT("首次撞人：魔法伤害 %s，沿碰撞方向再推 %s。\n代价：绑定来源原推移距离为 %s。"),
            *Damage,*Cm(Card->DistanceCm),*Percent(Card->DistanceScaleBp));break;
    case ELDDemoFinalModId::FinalForcedExit:
        Numbers=FString::Printf(TEXT("实际硬控结束 %s 后，仍在圈内则外推 %s；每目标间隔 %s。\n代价：绑定来源原硬控时长为 %s，再参与控制递减。"),
            *Sec(Card->DelayMs),*Cm(Card->DistanceCm),*Sec(Card->IntervalMs),*Percent(Card->ControlScaleBp));break;
    case ELDDemoFinalModId::FinalTraffic:
        Numbers=FString::Printf(TEXT("真实冲刺路径宽 %s，持续 %s；减速 %s 持续 %s。\n代价：绑定来源基础冷却增加 %s。"),
            *Cm(Card->RadiusCm),*Sec(Card->DurationMs),*Percent(Card->SlowBp),*Sec(Card->SlowMs),*Sec(Card->CooldownCostMs));break;
    case ELDDemoFinalModId::FinalSplit:
        Numbers=FString::Printf(TEXT("中心两侧各 %s；子区半径为 %s，单跳固定基础项为 %s。\n代价：持续时间减少 %s；AD/AP 系数不变。"),
            *Cm(Card->OffsetCm),*Percent(Card->RadiusScaleBp),*Percent(Card->DamageScaleBp),*Sec(Card->DurationCostMs));break;
    default:
        View.Detail.Body=TEXT("终局改装没有可用的表现定义。");HudReportMissingText(Id+TEXT(".final"));return View;
    }
    View.Detail.Body=Display->DescriptionText+TEXT("\n\n")+Numbers;
    View.bKnown=true;return View;
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
bool LDHUD::AppliedStatFeedback(const FLDBuildReceipt& Receipt,uint32 Match,uint32 Life,uint64 Request,int32 StatCount)
{
    return Match!=0&&Receipt.Header.Generation==Match&&Receipt.Header.OriginalLife==Life
        &&Request!=0&&Receipt.Header.RequestId==Request
        &&Receipt.Header.Family==ELDGrowthFamily::Build&&Receipt.Header.Outcome==ELDReceiptOutcome::Applied
        &&Receipt.Kind==ELDBuildCommandKind::AllocatePoints&&Receipt.Header.Revisions.bHasBuild
        &&Receipt.Header.Revisions.BuildRevision!=0&&StatCount>0&&Receipt.PointsSpent.Num()==StatCount
        &&Receipt.PointsSpent.ContainsByPredicate([](uint8 Count){return Count>0;});
}
bool LDHUD::AppliedFinalFeedback(const FLDBuildReceipt& Receipt,uint32 Match,uint32 Life,uint64 Request,FName Definition)
{
    return Match!=0&&Receipt.Header.Generation==Match&&Receipt.Header.OriginalLife==Life
        &&Request!=0&&Receipt.Header.RequestId==Request&&!Definition.IsNone()&&Receipt.DefinitionId==Definition
        &&Receipt.Header.Family==ELDGrowthFamily::Build&&Receipt.Header.Outcome==ELDReceiptOutcome::Applied
        &&Receipt.Kind==ELDBuildCommandKind::FinalMod&&Receipt.Header.Revisions.bHasBuild
        &&Receipt.Header.Revisions.BuildRevision!=0;
}
bool LDHUD::FinalStampReady(const FLDProgressionView& View,uint64 Revision,const FLDFinalModSelection& Selection,bool bBoardVisible)
{
    return bBoardVisible&&View.bValid&&Revision!=0&&View.BuildRevision>=Revision
        &&!Selection.IsEmpty()&&View.FinalMod==Selection;
}
TArray<LDHUD::FStatEffectView> LDHUD::StatEffects(const FLDDemoStatDefinition& Stat,const TArray<FLDCombatAttributeRowView>& Attributes)
{
    // The projected attribute each per-point bonus adds to (ratios add to their attribute's percent term).
    const auto Target=[](ELDDemoBonusStat Bonus)
    {
        switch(Bonus)
        {
        case ELDDemoBonusStat::ADFlat: case ELDDemoBonusStat::ADBonusRatio: return ELDProjectedAttribute::AD;
        case ELDDemoBonusStat::APFlat: case ELDDemoBonusStat::APBonusRatio: return ELDProjectedAttribute::AP;
        case ELDDemoBonusStat::MaxHPFlat: return ELDProjectedAttribute::MaxHP;

        case ELDDemoBonusStat::ArmorFlat: return ELDProjectedAttribute::Armor;
        case ELDDemoBonusStat::MagicResistFlat: return ELDProjectedAttribute::MagicResist;
        case ELDDemoBonusStat::CooldownHasteFlat: return ELDProjectedAttribute::CooldownHaste;
        case ELDDemoBonusStat::AttackRangeCmFlat: return ELDProjectedAttribute::AttackRange;
        case ELDDemoBonusStat::AttackSpeedBonusRatio: return ELDProjectedAttribute::AttackSpeed;
        case ELDDemoBonusStat::MoveSpeedBonusRatio: return ELDProjectedAttribute::MoveSpeed;
        case ELDDemoBonusStat::CritChanceBonusRatio: return ELDProjectedAttribute::CritChance;
        case ELDDemoBonusStat::HPRegenFlat: return ELDProjectedAttribute::HPRegen;
        case ELDDemoBonusStat::MomentumRegenFlat: return ELDProjectedAttribute::MomentumRegen;
        }
        return ELDProjectedAttribute::Count;
    };
    TArray<FStatEffectView> Effects;
    for(const FLDDemoStatBonus& Bonus:Stat.PerPoint)
    {
        const uint8 Attribute=uint8(Target(Bonus.Stat));
        const FLDCombatAttributeRowView* Row=Attributes.FindByPredicate([Attribute](const FLDCombatAttributeRowView& R){return R.Attribute==Attribute;});
        FLDCombatAttributeRowView Named;Named.Attribute=Attribute;
        FStatEffectView& Effect=Effects.AddDefaulted_GetRef();
        Effect.Attribute=AttributeRow(Row?*Row:Named).Name;
        Effect.Current=Row?AttributeRow(*Row).Final:FString(TEXT("?"));
        const TCHAR* Unit=Bonus.Stat==ELDDemoBonusStat::AttackRangeCmFlat?TEXT(" 厘米"):Bonus.Stat==ELDDemoBonusStat::HPRegenFlat||Bonus.Stat==ELDDemoBonusStat::MomentumRegenFlat?TEXT("/s"):TEXT("");
        Effect.PerPoint=TEXT("+")+(CardRationalStat(Bonus.Stat)?HudRational(Bonus.Cm)+Unit:CardRatioStat(Bonus.Stat)?HudPercent(Bonus.Amount):HudPoints(Bonus.Amount));
    }
    return Effects;
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
FString LDHUD::FSlotKeys::Of(uint8 Slot) const
{
    return Slot<3&&!Keys[Slot].IsEmpty()?Keys[Slot]:SlotKey(Slot);
}
FString LDHUD::BuildBlockedText(ELDGrowthReason Reason)
{
    switch(Reason)
    {
    case ELDGrowthReason::None: return FString();
    case ELDGrowthReason::Dead: return LDTerms::DeadNoLearn;
    case ELDGrowthReason::NoEmptySlot: return LDTerms::SlotsFull;
    case ELDGrowthReason::SlotOccupied: return LDTerms::ReplaceAtRest;
    default: return GrowthReasonText(Reason);
    }
}
LDHUD::FBuildGateView LDHUD::BuildGate(LDProgression::EMatchStage Stage,ELDLifePhase LifePhase)
{
    LDProgression::FBuildContext Context;
    Context.Stage=Stage;
    Context.LifePhase=LifePhase;
    const auto Gate=[&](ELDGrowthReason Reason,bool& bAllowed,FString& Blocked)
    {
        bAllowed=Reason==ELDGrowthReason::None;
        Blocked=BuildBlockedText(Reason);
    };
    FBuildGateView View;
    Gate(LDProgression::CommandGate(ELDBuildCommandKind::ActiveChoice,Context),View.bCanChoose,View.ChooseBlocked);
    Gate(LDProgression::CommandGate(ELDBuildCommandKind::LearnBlueprint,Context),View.bCanLearn,View.LearnBlocked);
    Gate(LDProgression::CommandGate(ELDBuildCommandKind::AllocatePoints,Context),View.bCanAllocate,View.AllocateBlocked);
    // A card install, move or uninstall is admitted exactly as growth admits it (LDProgression::InstallGate).
    Gate(LDProgression::InstallGate(Context),View.bCanInstall,View.InstallBlocked);
    Gate(LDProgression::CommandGate(ELDBuildCommandKind::ShopBuy,Context),View.bCanShop,View.ShopBlocked);
    Gate(LDProgression::CommandGate(ELDBuildCommandKind::Respec,Context),View.bCanRespec,View.RespecBlocked);
    Gate(LDProgression::CommandGate(ELDBuildCommandKind::ReplaceActive,Context),View.bCanReplace,View.ReplaceBlocked);
    Gate(LDProgression::CommandGate(ELDBuildCommandKind::FinalMod,Context),View.bCanFinalMod,View.FinalModBlocked);
    Gate(LDProgression::CommandGate(ELDBuildCommandKind::ForgetActive,Context),View.bCanForget,View.ForgetBlocked);
    Gate(LDProgression::CommandGate(ELDBuildCommandKind::PickCard,Context),View.bCanCardOffer,View.CardOfferBlocked);
    View.bInRest=Stage==LDProgression::EMatchStage::Rest||Stage==LDProgression::EMatchStage::RestLocked;
    return View;
}
FString LDHUD::BuildReceiptText(const FLDBuildReceipt& Receipt,const FLDDemoContentDefinitions* Content,const FSlotKeys& Keys)
{
    if(Receipt.Header.Outcome!=ELDReceiptOutcome::Applied)
    {
        return TEXT("没有生效：")+BuildBlockedText(LDGrowthWire::Reason(Receipt.Header.Reason));
    }
    const FString Missing(TEXT("回执缺少结果，请以当前状态为准。"));
    switch(Receipt.Kind)
    {
    case ELDBuildCommandKind::SkipActiveOffer:
        return Receipt.PointsDelta>0?LDTerms::PointsGained(Receipt.PointsDelta):Missing;
    case ELDBuildCommandKind::ShopBuy:
    {
        if(Receipt.DefinitionId.IsNone()){return LDTerms::SkillPointBought;}
        const FString Name=ContentName(Content,Receipt.DefinitionId.ToString());
        if(Receipt.ChoiceResult==ELDBuildChoiceResult::Learned){return LDTerms::LearnedInto(Name,Keys.Of(Receipt.LearnedSlot));}
        if(Receipt.ChoiceResult==ELDBuildChoiceResult::StoredAsBlueprint){return LDTerms::StoredAsBlueprint(Name,BuildBlockedText(ELDGrowthReason::NoEmptySlot));}
        return LDTerms::CardStored(Name,FString());
    }
    case ELDBuildCommandKind::ShopRefresh:
        return LDTerms::ShopRefreshed;
    case ELDBuildCommandKind::FinalMod:
        return !Receipt.DefinitionId.IsNone()
            ?FString::Printf(TEXT("终局改装：%s 已生效。"),*ContentName(Content,Receipt.DefinitionId.ToString())):Missing;
    case ELDBuildCommandKind::ActiveChoice:
    case ELDBuildCommandKind::LearnBlueprint:
    {
        const FString Name=ContentName(Content,Receipt.DefinitionId.ToString());
        if(Receipt.ChoiceResult==ELDBuildChoiceResult::Learned){return LDTerms::LearnedInto(Name,Keys.Of(Receipt.LearnedSlot));}
        if(Receipt.ChoiceResult==ELDBuildChoiceResult::StoredAsBlueprint)
        {return LDTerms::StoredAsBlueprint(Name,BuildBlockedText(LDGrowthWire::Reason(Receipt.StoredReason)));}
        return Missing;
    }
    case ELDBuildCommandKind::AllocatePoints:
    {
        int64 Spent=0;for(const uint8 Count:Receipt.PointsSpent){Spent+=Count;}
        return LDTerms::PointsAdded(Spent,Receipt.UnspentPointsAfter);
    }
    case ELDBuildCommandKind::Respec:
        return Receipt.PointsRefunded>0?LDTerms::Respecced(Receipt.PointsRefunded,Receipt.UnspentPointsAfter):Missing;
    case ELDBuildCommandKind::ReplaceActive:
        if(Receipt.ChoiceResult!=ELDBuildChoiceResult::Learned||Receipt.DefinitionId.IsNone()||Receipt.ForgottenId.IsNone()){return Missing;}
        return LDTerms::Replaced(ContentName(Content,Receipt.DefinitionId.ToString()),Keys.Of(Receipt.LearnedSlot),ContentName(Content,Receipt.ForgottenId.ToString()));
    case ELDBuildCommandKind::ForgetActive:
        return Receipt.ForgottenId.IsNone()?Missing:LDTerms::Forgotten(ContentName(Content,Receipt.ForgottenId.ToString()));
    case ELDBuildCommandKind::PickCard:
    {
        if(Receipt.CardId.IsNone()){return Missing;}
        const FString Name=ContentName(Content,Receipt.CardId.ToString());
        if(Receipt.bCardInstalled){return LDTerms::CardInstalled(Name);}
        return LDTerms::CardStored(Name,BuildBlockedText(LDGrowthWire::Reason(Receipt.InstallStoredReason)));
    }
    case ELDBuildCommandKind::InstallCard:
        if(Receipt.CardId.IsNone()||!Receipt.bCardInstalled){return Missing;}
        return LDTerms::CardInstalled(ContentName(Content,Receipt.CardId.ToString()));
    case ELDBuildCommandKind::UninstallCard:
        if(Receipt.CardId.IsNone()||Receipt.bCardInstalled){return Missing;}
        return LDTerms::CardRemoved(ContentName(Content,Receipt.CardId.ToString()));
    case ELDBuildCommandKind::SkipCards: return Receipt.PointsDelta>0?LDTerms::PointsGained(Receipt.PointsDelta):FString(LDTerms::CardsSkipped);
    case ELDBuildCommandKind::RerollCards: return LDTerms::CardsRerolled(Receipt.CardRerollsAfter);
    default: return Missing;
    }
}
FString LDHUD::ElementName(ELDDemoElement Element)
{
    switch(Element)
    {
    case ELDDemoElement::Fire: return LDTerms::ElementFire;
    case ELDDemoElement::Water: return LDTerms::ElementWater;
    case ELDDemoElement::Electric: return LDTerms::ElementElectric;
    case ELDDemoElement::Chill: return LDTerms::ElementChill;
    case ELDDemoElement::Rot: return LDTerms::ElementRot;
    default: return FString::Printf(TEXT("未知元素 #%d"),int32(Element));
    }
}
FName LDHUD::CardKey(FName Definition,uint16 Serial)
{
    return FName(Definition,int32(Serial));
}
FName LDHUD::CardDefinition(FName Key)
{
    return FName(Key,0);
}
uint16 LDHUD::CardSerial(FName Key)
{
    return uint16(FMath::Clamp(Key.GetNumber(),0,int32(MAX_uint16)));
}
const FLDCardInstanceView* LDHUD::FindCard(const FLDProgressionView& Progression,FName Key)
{
    const uint16 Serial=CardSerial(Key);
    return Serial==0?nullptr:Progression.Cards.FindByPredicate([Serial](const FLDCardInstanceView& Card){return Card.Serial==Serial;});
}
const FLDDemoCardDefinition* LDHUD::CardDefinitionOf(const FLDDemoContentDefinitions* Content,FName KeyOrId)
{
    if(!Content||KeyOrId.IsNone()){return nullptr;}
    const FString Id=CardDefinition(KeyOrId).ToString();
    return Content->Cards.FindByPredicate([&Id](const FLDDemoCardDefinition& Row){return Row.Id==Id;});
}
int32 LDHUD::OwnedCopies(const FLDProgressionView& Progression,FName Definition)
{
    const FName Id=CardDefinition(Definition);
    return Progression.Cards.FilterByPredicate([Id](const FLDCardInstanceView& Card){return Card.DefinitionId==Id;}).Num();
}
FString LDHUD::CardTargetText(const FLDCardInstanceView& Card,const FLDDemoContentDefinitions* Content)
{
    if(!Card.IsInstalled()){return FString();}
    TArray<FString> Parts;
    if(Card.Target.Kind==ELDCardTargetKind::Skill&&!Card.Target.Active.IsNone()){Parts.Add(ContentName(Content,Card.Target.Active.ToString()));}
    if(Card.Target.Element!=ELDDemoElement::None){Parts.Add(ElementName(Card.Target.Element));}
    return Parts.IsEmpty()?FString():LDTerms::InstalledOn(FString::Join(Parts,TEXT(" · ")));
}
namespace
{
// A card's rich text (keywords in <b>…</>, escaped markup) as the plain text of the HUD popup.
FString HudRichToPlain(const FString& Rich)
{
    return Rich.Replace(TEXT("<b>"),TEXT("")).Replace(TEXT("</>"),TEXT("")).Replace(TEXT("&lt;"),TEXT("<")).Replace(TEXT("&gt;"),TEXT(">"))
        .Replace(TEXT("&amp;"),TEXT("&"));
}
FString HudChoiceBody(const FString& Sentence,const TArray<FString>& Keywords,const FString& Outcome)
{
    TArray<FString> Lines;
    Lines.Add(Sentence);
    if(!Keywords.IsEmpty()){Lines.Add(FString());for(const FString& Keyword:Keywords){Lines.Add(HudRichToPlain(Keyword));}}
    if(!Outcome.IsEmpty()){Lines.Add(FString());Lines.Add(Outcome);}
    return FString::Join(Lines,TEXT("\n"));
}
}
LDHUD::FActionDetailView LDHUD::ActiveChoiceDetail(const FActiveCardView& Card,bool bBlueprint,const FString& Outcome)
{
    FActionDetailView Detail;
    Detail.Name=Card.Name;
    const TCHAR* Kind=bBlueprint?LDTerms::Blueprint:TEXT("技能");
    Detail.Summary=Card.bKnown?FString::Printf(TEXT("%s · %s · %s"),Kind,*Card.Target,*Card.Cost):FString(Kind);
    Detail.Body=HudChoiceBody(Card.SentencePlain,Card.Keywords,Outcome);
    return Detail;
}
LDHUD::FActionDetailView LDHUD::CardChoiceDetail(const FCardFaceView& Card,const FString& Outcome)
{
    FActionDetailView Detail;
    Detail.Name=Card.Name;
    Detail.Summary=Card.KindLine.IsEmpty()?FString(LDTerms::Card):Card.KindLine;
    Detail.Body=HudChoiceBody(Card.SentencePlain,Card.Keywords,Outcome);
    return Detail;
}
LDHUD::FCardFaceView LDHUD::CardFace(FName DefinitionId,const FLDDemoContentDefinitions* Content,const FCardStats& Stats)
{
    FCardFaceView Card;
    Card.DefinitionId=CardDefinition(DefinitionId);
    const FString Id=Card.DefinitionId.ToString();
    Card.Name=ContentName(Content,Id);
    Card.Glyph=IconGlyph(Card.Name);
    const FLDDemoDisplayDefinition* Display=Content?Content->Display.FindByPredicate([&Id](const FLDDemoDisplayDefinition& D){return D.Id==Id;}):nullptr;
    const FLDDemoCardDefinition* Definition=CardDefinitionOf(Content,Card.DefinitionId);
    if(Display){Card.IconId=Display->IconId;}
    if(!Definition||!Display)
    {
        Card.SentencePlain=Card.Sentence=Content?FString(TEXT("内容目录中找不到这张牌")):FString(TEXT("内容数据同步中"));
        return Card;
    }
    Card.Form=Definition->Form;
    Card.Type=Definition->Type;
    Card.bChooseElement=!Definition->ChooseElements.IsEmpty();
    Card.KindLine=FString::Printf(TEXT("%s · %s"),LDTerms::CardFormName(uint8(Definition->Form)),LDTerms::CardTypeName(uint8(Definition->Type)));
    // A card's fixed amounts grow with the holder's level like an active's (LDSkillGrowth), so its face renders the same way
    // against a value set that holds only its effects.
    FLDDemoAbilityValues Values;Values.Effects=Definition->Effects;
    const FCardText Sentence=CardText(Display->DescriptionText,Values,Stats,Content->Parameters.SkillRanks,
        Content->Parameters.SkillGrowthPerLevelBp,1);
    Card.Sentence=Sentence.Rich;Card.SentencePlain=Sentence.Plain;
    Card.Keywords=KeywordLines(Sentence.Keywords);
    Card.bKnown=Sentence.bComplete;
    return Card;
}
namespace
{
    // Installed cards that take workshop capacity (an element medal does not).
    int32 HudInstalledCards(const FLDProgressionView& Progression,const FLDDemoContentDefinitions& Content)
    {
        return Progression.Cards.FilterByPredicate([&Content](const FLDCardInstanceView& Card)
            {return Card.IsInstalled()&&!LDProgression::IsMedal(Content,Card.DefinitionId);}).Num();
    }
    // A card's place as growth names it (LDProgression::FLDCardTarget) from its view.
    FLDCardTarget HudTarget(ELDCardTargetKind Kind,FName Active,ELDDemoElement Element)
    {
        FLDCardTarget Target;Target.Kind=Kind;Target.Active=Active;Target.Element=Element;
        return Target;
    }
}
LDHUD::EStorageColumn LDHUD::BoardColumn(int32 Position)
{
    static const EStorageColumn Order[StorageColumnCount]={EStorageColumn::SlotQ,EStorageColumn::SlotW,
        EStorageColumn::SlotE,EStorageColumn::Dash,EStorageColumn::Body};
    return Order[FMath::Clamp(Position,0,StorageColumnCount-1)];
}
int32 LDHUD::BoardPosition(EStorageColumn Column)
{
    for(int32 Position=0;Position<StorageColumnCount;++Position){if(BoardColumn(Position)==Column){return Position;}}
    return 0;
}
FString LDHUD::ColumnLabel(EStorageColumn Column)
{
    switch(Column)
    {
    case EStorageColumn::SlotQ: return TEXT("Q");
    case EStorageColumn::SlotW: return TEXT("W");
    case EStorageColumn::SlotE: return TEXT("E");
    case EStorageColumn::Dash: return TEXT("D");
    default: return LDTerms::ColumnBody;
    }
}
bool LDHUD::CardColumn(const FLDProgressionView& Progression,ELDCardTargetKind Kind,FName Active,EStorageColumn& OutColumn)
{
    switch(Kind)
    {
    case ELDCardTargetKind::Dash: OutColumn=EStorageColumn::Dash;return true;
    case ELDCardTargetKind::Body: OutColumn=EStorageColumn::Body;return true;
    case ELDCardTargetKind::Skill:
        for(const FLDActiveSlotView& Held:Progression.Actives)
        {if(Held.DefinitionId==Active&&Held.Slot<3){OutColumn=SlotColumn(Held.Slot);return true;}}
        return false;
    default: return false;
    }
}
LDHUD::EStorageColumn LDHUD::SlotColumn(uint8 Slot)
{
    return Slot==0?EStorageColumn::SlotQ:Slot==1?EStorageColumn::SlotW:Slot==2?EStorageColumn::SlotE:EStorageColumn::Body;
}
bool LDHUD::FinalSourceColumn(const FLDProgressionView& Progression,ELDFinalModTargetKind Kind,FName TargetId,EStorageColumn& OutColumn)
{
    if(!Progression.bValid){return false;}
    switch(Kind)
    {
    case ELDFinalModTargetKind::None: OutColumn=EStorageColumn::Body;return true;
    case ELDFinalModTargetKind::Dash: OutColumn=EStorageColumn::Dash;return true;
    case ELDFinalModTargetKind::Active:
        for(const FLDActiveSlotView& Active:Progression.Actives)
        {if(Active.DefinitionId==TargetId&&Active.Slot<3){OutColumn=SlotColumn(Active.Slot);return true;}}
        return false;
    default: return false;
    }
}
bool LDHUD::FinalModColumn(const FLDProgressionView& Progression,EStorageColumn& OutColumn)
{
    const FLDFinalModSelection& Final=Progression.FinalMod;
    if(!Progression.bValid||Final.IsEmpty()||!Progression.FinalModSources.ContainsByPredicate([&Final](const FLDFinalModSourceView& Source)
        {return Source.Option==Final.DefinitionId&&Source.TargetKind==Final.TargetKind&&Source.TargetId==Final.TargetId;})){return false;}
    return FinalSourceColumn(Progression,Final.TargetKind,Final.TargetId,OutColumn);
}
LDHUD::FStorageView LDHUD::CardStorage(const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content,
    const TSet<FName>* Seen)
{
    FStorageView View;
    View.Columns.SetNum(StorageColumnCount);
    for(int32 Index=0;Index<StorageColumnCount;++Index)
    {
        FStorageColumnView& Column=View.Columns[Index];
        Column.Column=static_cast<EStorageColumn>(Index);
        // A slot column is never named by a key (the player's bindings and slot order move the keys): its place name is
        // its active's, which the storage looks up only when a notice names it.
        if(Index>=1&&Index<=3){Column.Active=ActiveInSlot(Progression,uint8(Index-1));}
        Column.Title=Column.Column==EStorageColumn::Body?FString(LDTerms::ColumnBody):Column.Column==EStorageColumn::Dash?FString(LDTerms::Dash)
            :Column.Active.IsNone()?FString(LDTerms::ColumnEmptySlot):FString();
    }
    if(!Progression.bValid){return View;}
    View.Capacity=Progression.CardCapacity;
    // Copies of one definition in one place share a face: the library groups by definition, a column by definition and element.
    for(const FLDCardInstanceView& Owned:Progression.Cards)
    {
        const FName Key=CardKey(Owned.DefinitionId,Owned.Serial);
        if(!Owned.IsInstalled())
        {
            FStoredView* Group=View.Stored.FindByPredicate([&Owned](const FStoredView& Row){return CardDefinition(Row.Card)==Owned.DefinitionId;});
            if(!Group){Group=&View.Stored.AddDefaulted_GetRef();Group->Card=Key;Group->Copies=0;}
            Group->Keys.Add(Key);
            Group->Copies=Group->Keys.Num();
            if(!Seen||!Seen->Contains(Key)){Group->bFresh=true;}
            continue;
        }
        if(!Content||!LDProgression::IsMedal(*Content,Owned.DefinitionId)){++View.Installed;}
        EStorageColumn Column=EStorageColumn::Body;
        if(!CardColumn(Progression,Owned.Target.Kind,Owned.Target.Active,Column)){continue;}
        const FString Note=Owned.Target.Element==ELDDemoElement::None?FString():ElementName(Owned.Target.Element);
        TArray<FEquippedView>& Entries=View.Columns[int32(Column)].Entries;
        FEquippedView* Group=Entries.FindByPredicate([&Owned,&Note](const FEquippedView& Row)
            {return CardDefinition(Row.Card)==Owned.DefinitionId&&Row.Note==Note;});
        if(!Group){Group=&Entries.AddDefaulted_GetRef();Group->Card=Key;Group->Note=Note;}
        Group->Keys.Add(Key);
        Group->Copies=Group->Keys.Num();
    }
    for(const FStoredView& Row:View.Stored){View.Fresh+=Row.bFresh?1:0;}
    return View;
}
TArray<LDHUD::FCardDrop> LDHUD::CardDrops(FName Key,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    TArray<FCardDrop> Out;
    const FLDDemoCardDefinition* Definition=CardDefinitionOf(Content,Key);
    const FLDCardInstanceView* Owned=FindCard(Progression,Key);
    // The server refuses a new install past the capacity (WorkshopFull); a move frees its own slot first; a medal takes none.
    const bool bMedal=Definition&&Definition->Form==ELDCardForm::Medal;
    if(!Definition||!Owned||!Progression.bValid||(!bMedal&&!Owned->IsInstalled()&&WorkshopFull(Progression,Content))){return Out;}
    const FLDGrowthBuild Build=LDProgression::BuildFromView(*Content,Progression);
    const ELDDemoElement Element=Definition->ChooseElements.IsEmpty()?ELDDemoElement::None:Definition->ChooseElements[0];
    const auto Try=[&](ELDCardTargetKind Kind,FName Active)
    {
        // Growth's own target rule decides the place (any listed element stands for Y25's choice).
        if(LDProgression::CardTargetReason(*Content,Build,Owned->DefinitionId,HudTarget(Kind,Active,Element))!=ELDGrowthReason::None){return;}
        FCardDrop Drop;Drop.Kind=Kind;Drop.TargetId=Active;Drop.Elements=Definition->ChooseElements;
        if(!CardColumn(Progression,Kind,Active,Drop.Column)){return;}
        // Moving a card onto the place it already holds is no drop.
        if(Owned->IsInstalled()&&Owned->Target.Kind==Kind&&Owned->Target.Active==Active&&Definition->ChooseElements.IsEmpty()){return;}
        Out.Add(MoveTemp(Drop));
    };
    switch(Definition->Form)
    {
    case ELDCardForm::Dash: Try(ELDCardTargetKind::Dash,NAME_None);break;
    case ELDCardForm::Body: Try(ELDCardTargetKind::Body,NAME_None);break;
    case ELDCardForm::Skill:
    case ELDCardForm::Medal:
        // A medal goes into the element slot of a learned skill or of the body (it swaps out the one there).
        for(uint8 Slot=0;Slot<3;++Slot)
        {
            const FName Held=ActiveInSlot(Progression,Slot);
            if(!Held.IsNone()){Try(ELDCardTargetKind::Skill,Held);}
        }
        if(bMedal){Try(ELDCardTargetKind::Body,NAME_None);}
        break;
    default: break;
    }
    return Out;
}
const LDHUD::FCardDrop* LDHUD::FindDrop(const TArray<FCardDrop>& Drops,EStorageColumn Column,FName Host)
{
    if(!Host.IsNone())
    {
        if(const FCardDrop* OnHost=Drops.FindByPredicate([Column,Host](const FCardDrop& D){return D.Column==Column&&D.Host==Host;})){return OnHost;}
    }
    return Drops.FindByPredicate([Column](const FCardDrop& D){return D.Column==Column&&D.Host.IsNone();});
}
TArray<LDHUD::FCardDrop> LDHUD::ReplaceDrops(FName Key,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    TArray<FCardDrop> Out;
    const FLDCardInstanceView* Self=FindCard(Progression,Key);
    if(!Self||Self->IsInstalled()||!CardDefinitionOf(Content,Key)||!WorkshopFull(Progression,Content)
        ||CardDefinitionOf(Content,Key)->Form==ELDCardForm::Medal){return Out;}
    // The same card with one slot free: every place it takes stands on the tile of each installed card in that column.
    FLDProgressionView Freed=Progression;
    for(const FLDCardInstanceView& Replaced:Progression.Cards)
    {
        if(!Replaced.IsInstalled()){continue;}
        for(FLDCardInstanceView& Row:Freed.Cards){if(Row.Serial==Replaced.Serial){Row.Target=FLDCardTargetView();}}
        EStorageColumn Column=EStorageColumn::Body;
        if(CardColumn(Progression,Replaced.Target.Kind,Replaced.Target.Active,Column))
        {
            for(const FCardDrop& Landing:CardDrops(Key,Freed,Content))
            {
                if(Landing.Column!=Column){continue;}
                FCardDrop Drop=Landing;Drop.Host=CardKey(Replaced.DefinitionId,Replaced.Serial);Drop.Replacing=Drop.Host;
                Out.Add(MoveTemp(Drop));
                break;
            }
        }
        for(FLDCardInstanceView& Row:Freed.Cards){if(Row.Serial==Replaced.Serial){Row.Target=Replaced.Target;}}
    }
    return Out;
}
FString LDHUD::DropPreviewText(FName Key,const FCardDrop& Drop,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    FString Place=ColumnLabel(Drop.Column);
    if(Drop.Kind==ELDCardTargetKind::Skill&&!Drop.TargetId.IsNone()){Place=ContentName(Content,Drop.TargetId.ToString());}
    if(Drop.Replacing.IsNone()){return LDTerms::DropInstallsOn(Place);}
    return LDTerms::DropReplaces(ContentName(Content,CardDefinition(Drop.Replacing).ToString()),Place);
}
LDHUD::FCardConnections LDHUD::CardConnections(FName Key,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    FCardConnections Out;
    const FLDDemoCardDefinition* Definition=CardDefinitionOf(Content,Key);
    if(!Definition||!Content||Definition->VocabMask==0){return Out;}
    const FName Self=CardDefinition(Key);
    for(const FLDActiveSlotView& Active:Progression.Actives)
    {
        const FString Id=Active.DefinitionId.ToString();
        const FLDDemoAbilityDefinition* Ability=Content->Abilities.FindByPredicate([&Id](const FLDDemoAbilityDefinition& Row){return Row.Id==Id;});
        if(Ability&&(Ability->VocabMask&Definition->VocabMask)!=0){Out.Actives.AddUnique(Active.DefinitionId);}
    }
    for(const FLDCardInstanceView& Owned:Progression.Cards)
    {
        if(Owned.DefinitionId==Self){continue;}
        const FLDDemoCardDefinition* Other=CardDefinitionOf(Content,Owned.DefinitionId);
        if(Other&&(Other->VocabMask&Definition->VocabMask)!=0){Out.Cards.AddUnique(Owned.DefinitionId);}
    }
    return Out;
}
FString LDHUD::ConnectionsText(FName Key,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    const FCardConnections Connections=CardConnections(Key,Progression,Content);
    TArray<FString> Links;
    for(const FName Id:Connections.Actives){Links.AddUnique(ContentName(Content,Id.ToString()));}
    for(const FName Id:Connections.Cards){Links.AddUnique(ContentName(Content,Id.ToString()));}
    return Links.IsEmpty()?FString():LDTerms::WorksWith(FString::Join(Links,TEXT("、")));
}
FString LDHUD::DropColumnsText(const TArray<FCardDrop>& Drops)
{
    TArray<FString> Columns;
    for(int32 Position=0;Position<StorageColumnCount;++Position)
    {
        const EStorageColumn Column=BoardColumn(Position);
        if(Drops.ContainsByPredicate([Column](const FCardDrop& D){return D.Column==Column;})){Columns.Add(ColumnLabel(Column));}
    }
    return Columns.IsEmpty()?FString(LDTerms::NoPlaceNow):LDTerms::CanGoOn(FString::Join(Columns,TEXT("、")));
}
bool LDHUD::WorkshopFull(const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    if(!Content||!Progression.bValid){return false;}
    return HudInstalledCards(Progression,*Content)>=Progression.CardCapacity;
}
FString LDHUD::StoredPlaceText(FName Key,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    if(WorkshopFull(Progression,Content))
    {
        return ReplaceDrops(Key,Progression,Content).IsEmpty()?LDTerms::WorkshopFullNoPlace(Progression.CardCapacity)
            :LDTerms::WorkshopFullHint(Progression.CardCapacity);
    }
    return DropColumnsText(CardDrops(Key,Progression,Content));
}
LDHUD::FGrowthPending LDHUD::GrowthPending(const FLDProgressionView* Progression,uint32 Match,const FLDDemoContentDefinitions* Content)
{
    FGrowthPending Pending;
    if(!Progression||!Progression->bValid||Match==0||Progression->Generation!=Match){return Pending;}
    Pending.bOffer=Progression->bHasActiveOffer&&!Progression->ActiveOffer.Options.IsEmpty();
    Pending.Offers=Progression->QueuedActiveOpportunities;
    Pending.Blueprints=Progression->Actives.Num()<3?Progression->PendingBlueprints.Num():0;
    Pending.bCardOffer=Progression->bHasCardOffer&&!Progression->CardOffer.Options.IsEmpty();
    Pending.CardOffers=Progression->QueuedCardOffers;
    Pending.bFinalMod=Progression->bFinalModPending&&!Progression->FinalModOptions.IsEmpty();
    return Pending;
}
FString LDHUD::OfferQueueDots(uint8 Queued)
{
    if(Queued==0){return FString();}
    FString Dots=TEXT("●");
    for(int32 Index=1;Index<FMath::Min<int32>(Queued,8);++Index){Dots+=TEXT(" ○");}
    return Dots;
}
bool LDHUD::FOfferMotionTicket::Matches(const FLDBuildReceipt& Receipt,uint32 CurrentMatch,uint32 CurrentLife) const
{
    const bool bIdentity=Match!=0&&Request!=0&&Offer.IsValid()&&CurrentMatch==Match&&CurrentLife==Life
        &&Receipt.Header.Generation==Match&&Receipt.Header.OriginalLife==Life&&Receipt.Header.RequestId==Request
        &&Receipt.Header.Family==ELDGrowthFamily::Build&&Receipt.Header.Outcome==ELDReceiptOutcome::Applied
        &&Receipt.Kind==Kind&&Receipt.OfferId==Offer;
    if(!bIdentity){return false;}
    if(Kind==ELDBuildCommandKind::SkipActiveOffer){return Option.IsNone()&&Receipt.PointsDelta>0;}
    return Kind==ELDBuildCommandKind::ActiveChoice&&!Option.IsNone()&&Receipt.DefinitionId==Option
        &&(Receipt.ChoiceResult==ELDBuildChoiceResult::StoredAsBlueprint
            ||(Receipt.ChoiceResult==ELDBuildChoiceResult::Learned&&Receipt.LearnedSlot<3));
}
FString LDHUD::FieldRewardEventName(FName Event)
{
    if(Event==FName(TEXT("airdrop"))){return LDTerms::Airdrop;}
    if(Event==FName(TEXT("boss"))){return LDTerms::Boss;}
    if(Event==FName(TEXT("camp_marker_high"))){return LDTerms::FieldRewardHighCamp;}
    if(Event==FName(TEXT("camp_marker_elite"))){return LDTerms::FieldRewardEliteCamp;}
    return FString::Printf(TEXT("未知事件 %s"),*Event.ToString());
}
FString LDHUD::FieldRewardNotice(const FLDFieldRewardView& Reward,const FLDDemoContentDefinitions* Content)
{
    TArray<FString> Parts;
    if(Reward.XP>0){Parts.Add(FString::Printf(TEXT("+%lld %s"),Reward.XP,LDTerms::Experience));}
    const FString Missing(TEXT("奖励记录缺少内容，请以当前状态为准"));
    const FString Name=Reward.Definition.IsNone()?FString():ContentName(Content,Reward.Definition.ToString());
    switch(Reward.Result)
    {
    case ELDFieldRewardResult::None: break;
    case ELDFieldRewardResult::Blueprint: Parts.Add(Name.IsEmpty()?Missing:LDTerms::FieldRewardBlueprint(Name)); break;
    case ELDFieldRewardResult::CardOffer: Parts.Add(LDTerms::FieldRewardCards()); break;
    case ELDFieldRewardResult::BlueprintHeld: Parts.Add(LDTerms::FieldRewardBlueprintHeld()); break;
    default: Parts.Add(FString::Printf(TEXT("未知奖励 #%d"),int32(Reward.Result))); break;
    }
    if(Parts.IsEmpty()){return FString();}
    return FieldRewardEventName(Reward.Event)+TEXT("：")+FString::Join(Parts,TEXT("，"));
}
TArray<FLDFieldRewardView> LDHUD::NewFieldRewards(const FLDProgressionView& Progression,int32 Known)
{
    TArray<FLDFieldRewardView> Out;
    for(int32 Index=FMath::Max(0,Known);Index<Progression.FieldRewards.Num();++Index){Out.Add(Progression.FieldRewards[Index]);}
    return Out;
}
TArray<FLDFieldRewardView> LDHUD::FFieldRewardWatch::Observe(const FLDProgressionView* Progression,uint32 Match)
{
    if(!Progression||!Progression->bValid||Match==0||Progression->Generation!=Match){return TArray<FLDFieldRewardView>();}
    // The first view of a match primes: rewards it already lists were told before (or before a reconnect) and never replay.
    if(SeenMatch!=Match){SeenMatch=Match;Known=Progression->FieldRewards.Num();return TArray<FLDFieldRewardView>();}
    TArray<FLDFieldRewardView> Out=NewFieldRewards(*Progression,Known);
    Known=Progression->FieldRewards.Num();
    return Out;
}
LDHUD::FAttributeRowView LDHUD::AttributeRow(const FLDCombatAttributeRowView& Row)
{
    static const FString Names[]={LDTerms::MaxHealth(),LDTerms::AttackDamage,LDTerms::AbilityPower,LDTerms::Armor,LDTerms::MagicResist,
        LDTerms::AttackSpeed,LDTerms::MoveSpeed,TEXT("攻击距离"),LDTerms::HealthRegen(),LDTerms::MomentumRegen(),LDTerms::CritChance(),LDTerms::Haste};
    static const TCHAR* Units[]={TEXT(""),TEXT(""),TEXT(""),TEXT(""),TEXT(""),TEXT(" 次/分钟"),TEXT(""),TEXT(" 厘米"),TEXT("/s"),TEXT("/s"),TEXT(""),TEXT("")};
    static const TCHAR* Glyphs[]={TEXT("血"),TEXT("攻"),TEXT("法"),TEXT("甲"),TEXT("抗"),TEXT("速"),TEXT("移"),TEXT("距"),TEXT("回"),TEXT("劲"),TEXT("暴"),TEXT("急")};
    static_assert(UE_ARRAY_COUNT(Names)==int32(ELDProjectedAttribute::Count)&&UE_ARRAY_COUNT(Units)==int32(ELDProjectedAttribute::Count)
        &&UE_ARRAY_COUNT(Glyphs)==int32(ELDProjectedAttribute::Count),"One name, unit and glyph per projected attribute.");
    FAttributeRowView View;
    if(Row.Attribute>=UE_ARRAY_COUNT(Names)){View.Name=FString::Printf(TEXT("未知属性 #%d"),Row.Attribute);View.Glyph=TEXT("?");View.Value=View.Final=TEXT("—");return View;}
    View.Name=Names[Row.Attribute];View.Glyph=Glyphs[Row.Attribute];View.bClamped=Row.bClamped;
    const ELDProjectedAttribute Attribute=static_cast<ELDProjectedAttribute>(Row.Attribute);
    const bool bRatio=Attribute==ELDProjectedAttribute::CritChance;
    // Attack speed reads as whole basic attacks per minute (2026-11-13 user feedback, #85); the rule value stays attacks per second.
    const bool bPerMinute=Attribute==ELDProjectedAttribute::AttackSpeed;
    auto Value=[bRatio,bPerMinute](const FLDDemoRational& V)
    {
        if(V.Denominator<=0){return FString(TEXT("?"));}
        const double D=double(V.Numerator)/double(V.Denominator);
        if(bPerMinute){return FString::FromInt(FMath::RoundToInt(D*60.));}
        return bRatio?HudDecimal(D*100.)+TEXT("%"):HudDecimal(D);
    };
    View.Value=Value(Row.Final);
    View.Final=View.Value+Units[Row.Attribute];
    if(Attribute==ELDProjectedAttribute::CooldownHaste&&Row.Final.Denominator>0)
    {
        // Haste reads as the cooldown reduction it gives, FORMULA.CooldownMs: 1-100/(100+H); the popup keeps the haste points.
        const double H=FMath::Max(0.,double(Row.Final.Numerator)/double(Row.Final.Denominator));
        View.Value=FString::FromInt(FMath::RoundToInt(100.*H/(100.+H)))+TEXT("%");
        View.Final=TEXT("冷却缩减 ")+View.Value+TEXT("（")+LDTerms::Haste+TEXT(" ")+HudDecimal(H)+TEXT("）");
    }
    if(Row.bClamped){View.Final+=TEXT("（已达上下限）");}
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
TArray<uint8> LDHUD::AttributeViewOrder()
{
    using A=ELDProjectedAttribute;
    return {uint8(A::AD),uint8(A::AP),uint8(A::Armor),uint8(A::MagicResist),uint8(A::AttackSpeed),uint8(A::MoveSpeed),
        uint8(A::AttackRange),uint8(A::CritChance),uint8(A::CooldownHaste)};
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
    case ELDGrowthReason::NoEmptySlot: return LDTerms::SlotsFull;
    case ELDGrowthReason::SlotOccupied: return FString(TEXT("这个技能槽已有技能，"))+LDTerms::ReplaceAtRest;
    case ELDGrowthReason::NoBlueprint: return FString(TEXT("没有这份"))+LDTerms::Blueprint;
    case ELDGrowthReason::NotEnoughPoints: return TEXT("未分配点数不够");
    case ELDGrowthReason::NoActiveOffer: return TEXT("当前没有可选的技能");
    case ELDGrowthReason::PhaseClosed: return TEXT("现在不是成长时段");
    case ELDGrowthReason::ReplacementNotAllowed: return TEXT("休息时才能替换或遗忘技能");
    case ELDGrowthReason::BuildLocked: return TEXT("PK 与返回倒计时期间不能逛商店、洗点或换技能");
    case ELDGrowthReason::OfferOutstanding: return TEXT("还有没选的技能，先选完");
    case ELDGrowthReason::NothingToRespec: return TEXT("还没加过点，不用洗");
    case ELDGrowthReason::SlotEmpty: return TEXT("这个技能槽是空的");
    case ELDGrowthReason::NotRestWindow: return TEXT("休息时才能逛商店或洗点");
    case ELDGrowthReason::NotOwned: return TEXT("你没有这张牌");
    case ELDGrowthReason::AlreadyInstalled: return TEXT("这张牌已经装上了");
    case ELDGrowthReason::NotInstalled: return TEXT("这张牌还没有装上");
    case ELDGrowthReason::InvalidTarget: return TEXT("这张牌不能装在这里");
    case ELDGrowthReason::WorkshopFull: return LDTerms::WorkshopFullReason;
    case ELDGrowthReason::NoCardOffer: return TEXT("这组牌已经失效");
    case ELDGrowthReason::NoReroll: return TEXT("没有重抽次数了");
    case ELDGrowthReason::NotInOffer: return TEXT("这张牌不在这组里");
    case ELDGrowthReason::ShopSoldOut: return TEXT("这一格已经卖掉了");
    case ELDGrowthReason::ShopStale: return TEXT("货架已经换过了");
    case ELDGrowthReason::SkillPointLimit: return TEXT("本局的技能点已经买满了");
    case ELDGrowthReason::NoShop: return TEXT("商店还没开张");
    case ELDGrowthReason::NoSkillPoint: return LDTerms::NoSkillPointReason;
    case ELDGrowthReason::BranchUnavailable: return LDTerms::BranchUnavailableReason;
    case ELDGrowthReason::DataNotReady: return TEXT("数据尚未就绪");
    case ELDGrowthReason::StaleContext: return TEXT("状态已变化，请重新确认");
    case ELDGrowthReason::NumericUnderflow: return TEXT("数值越界，本次没有提交");
    case ELDGrowthReason::NumericDomain: return TEXT("数值非法，本次没有提交");
    case ELDGrowthReason::ArithmeticOverflow: return TEXT("数值溢出，本次没有提交");
    case ELDGrowthReason::DataInvalid: return TEXT("数据无效，本次没有提交");
    case ELDGrowthReason::PrepareUnavailable: return TEXT("暂时无法提交，请稍后重试");
    case ELDGrowthReason::AuthorityFault: return TEXT("服务器故障，本次没有提交");
    case ELDGrowthReason::CapacityExceeded: return TEXT("服务器容量已满");
    case ELDGrowthReason::FieldRewardDuplicate: return TEXT("这份野外奖励已经领过了");
    case ELDGrowthReason::FinalLocked: return TEXT("终局改装已选定");
    case ELDGrowthReason::NoFinalMod: return TEXT("没有可选的终局改装");
    case ELDGrowthReason::FinalModUnavailable: return TEXT("这项终局改装当前没有合法来源");
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
    case ELDCombatReject::MomentumShort: return LDTerms::MomentumShort();
    case ELDCombatReject::ControlBlocked: return TEXT("受控制无法使用");
    case ELDCombatReject::TargetUnavailable: return TEXT("目标不可用");
    case ELDCombatReject::InvalidDirection: return TEXT("方向无效");
    case ELDCombatReject::OutOfRange: return TEXT("超出射程");
    case ELDCombatReject::Blocked: return TEXT("被地形阻挡");
    case ELDCombatReject::Dead: return LDTerms::Dead;
    case ELDCombatReject::MatchClosed: return TEXT("对局已结束");
    case ELDCombatReject::AuthorityDataUnavailable: return TEXT("服务器数据不可用");
    case ELDCombatReject::CapacityUnavailable: return TEXT("服务器容量不足");
    case ELDCombatReject::HealthShort: return TEXT("生命不够");
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
    Input.MomentumCostP2=Entry.MomentumCostP2;
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
    case ELDDemoStatusId::Braced:
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
    case ELDDemoStatusId::Wet:
    case ELDDemoStatusId::Electrified:
    case ELDDemoStatusId::Chilled:
    case ELDDemoStatusId::Rotten:
        return false;
    }
    return false;
}
FString LDHUD::SelfIconsNotice(const FSelfIconsView& View,bool bShown)
{
    return bShown&&View.bSyncing?FString(TEXT("状态同步中")):FString();
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
            View.Items.Add(MoveTemp(Icon));
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
        const FString Rate=TEXT("每秒恢复 ")+HudPercent(HPBp)+TEXT(" 生命上限");
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
    bOpen=true;bAutomatic=bAutomaticOpen;OpenedAt=Now;
}
void LDHUD::FChoicesFold::Closed()
{
    bOpen=false;bAutomatic=false;
}
bool LDHUD::FChoicesFold::ShouldFold(double Now,bool bEngaged,bool bOwnHit)
{
    // #222: a panel the player opened (or engaged with) stays until the player closes it, in combat and in a PK too.
    if(!bOpen||!bAutomatic){return false;}
    // Engaging with the automatic opening makes it the player's own.
    if(bEngaged){bAutomatic=false;return false;}
    return bOwnHit||Now-OpenedAt>=AutoOpenSeconds;
}
bool LDHUD::FGrowthAutoOpen::Observe(const FLDProgressionView& View,uint32 Match)
{
    if(!View.bValid||Match==0||View.Generation!=Match){return false;}
    if(SeenMatch!=Match){SeenMatch=Match;bOpeningShown=false;bOpeningCardsShown=false;}
    const bool bOpeningOffer=!bOpeningShown&&View.bHasActiveOffer&&View.ActiveOffer.OfferId.IsValid()&&View.ActiveOffer.OpportunityOrdinal==1;
    const bool bOpeningCards=!bOpeningCardsShown&&View.bHasCardOffer&&View.CardOffer.Source==ELDCardOfferSource::Opening
        &&!View.CardOffer.Options.IsEmpty();
    if(bOpeningOffer){bOpeningShown=true;}
    if(bOpeningCards){bOpeningCardsShown=true;}
    return bOpeningOffer||bOpeningCards;
}

FString LDHUD::DailyDescription(ELDDailyModifier Modifier,const FLDDemoContentParameters& P)
{
    const auto& D=P.Daily;
    const auto Sec=[](int64 Ms){return FString::Printf(TEXT("%g"),double(Ms)/1000.);};
    const auto Pct=[](int64 Bp){return FString::Printf(TEXT("%g%%"),double(Bp)/100.);};
    const auto Cm=[](const FLDDemoRational& R){return R.Denominator>0?double(R.Numerator)/double(R.Denominator):0.;};
    switch(Modifier)
    {
    case ELDDailyModifier::Rain: return FString::Printf(TEXT("每 %s 秒野外随机 %d 处出水洼（%s 秒）；湿持续 +%s 秒"),*Sec(D.RainIntervalMs),D.RainPuddles,*Sec(D.RainPuddleMs),*Sec(D.RainWetBonusMs));
    case ELDDailyModifier::Cold: return FString::Printf(TEXT("水洼生成 %s 秒后变冰面；冻持续 +%s 秒"),*Sec(D.ColdFreezeMs),*Sec(D.ColdChillBonusMs));
    case ELDDailyModifier::Blackout: return FString::Printf(TEXT("视野半径 -%s；麻的额外伤害 ×%g"),*Pct(D.BlackoutVisionCutBp),double(D.BlackoutShockScaleBp)/10000.);
    case ELDDailyModifier::Typhoon: return FString::Printf(TEXT("每 %s 秒全图向同一方向吹 %g 米（人和地面状态都吹）"),*Sec(D.TyphoonIntervalMs),Cm(D.TyphoonPushCm)/100.);
    case ELDDailyModifier::Heat: return FString::Printf(TEXT("灼烧每跳 +%s；起雾不生成雾区"),*Pct(D.HeatBurnTickScaleBp-10000));
    case ELDDailyModifier::Sale: return FString::Printf(TEXT("商店价格 ×%g（向上取整）"),double(D.SalePriceScaleBp)/10000.);
    case ELDDailyModifier::Delivery: return FString::Printf(TEXT("外卖 %d 份，每份 %s 秒后消失"),D.DeliveryLandMs.Num(),*Sec(D.DeliveryExpireMs));
    case ELDDailyModifier::Patrol: return FString::Printf(TEXT("画饼老板第 %s 秒就出场"),*Sec(D.PatrolBossAppearMs));
    case ELDDailyModifier::Exam: return FString::Printf(TEXT("野怪经验 +%s；复活等待 +%s 秒"),*Pct(D.ExamMonsterXPScaleBp-10000),*Sec(D.ExamRespawnBonusMs));
    case ELDDailyModifier::Gym: return FString::Printf(TEXT("冲刺冷却 -%s"),*Pct(D.GymDashCooldownCutBp));
    case ELDDailyModifier::Overtime: return TEXT("所有营地出生都带“加班”词条");
    case ELDDailyModifier::Rumor: return FString::Printf(TEXT("上热搜每次公开前 %d 名"),D.RumorLeaders);
    default: return FString();
    }
}

FString LDHUD::AffixDescription(ELDCampAffix Affix,const FLDDemoContentParameters& P)
{
    const auto& A=P.Affix;
    const auto Pct=[](int64 Bp){return FString::Printf(TEXT("%g%%"),double(Bp)/100.);};
    switch(Affix)
    {
    case ELDCampAffix::Overtime: return FString::Printf(TEXT("攻速 +%s"),*Pct(A.OvertimeAttackSpeedBp));
    case ELDCampAffix::Teambuild: return FString::Printf(TEXT("经验 +%s"),*Pct(A.TeambuildXPScaleBp-10000));
    case ELDCampAffix::Fridge: return FString::Printf(TEXT("攻击挂冻，脚下常驻冰面 · 清营得一组牌%d选一"),A.FridgeCardOffer);
    case ELDCampAffix::Bonus: return FString::Printf(TEXT("生命 +%s · 清营得一组牌%d选一"),*Pct(A.BonusHPBp),A.BonusCardOffer);
    case ELDCampAffix::Layoff: return FString::Printf(TEXT("同营倒下一个，其余攻击 +%s（可叠）"),*Pct(A.LayoffAttackBp));
    case ELDCampAffix::Audit: return FString::Printf(TEXT("技能预警缩短 %s · 清营得一个技能点（每局最多 %d 个）"),*Pct(A.AuditTelegraphCutBp),A.AuditSkillPointsMax);
    default: return FString();
    }
}

LDHUD::FSpawnChoiceView LDHUD::SpawnChoice(const FLDOwnerState& Own,bool bClock,int64 NowMs)
{
    FSpawnChoiceView View;
    const FLDOwnerPacingView& Pacing=Own.Pacing;
    if(Own.LifePhase!=ELDLifePhase::Dead||Pacing.SpawnCandidates.IsEmpty()||Pacing.SpawnLife!=Own.Life
        ||(bClock&&NowMs>Pacing.SpawnChoiceEndsAtMs)){return View;}
    View.bVisible=true;
    View.Count=Pacing.SpawnCandidates.Num();
    View.Chosen=Pacing.SpawnChosen;
    View.Countdown=bClock?FString::Printf(TEXT("%lld"),FMath::Max<int64>(1,(Pacing.SpawnChoiceEndsAtMs-NowMs+999)/1000)):FString();
    return View;
}

FString LDHUD::SpawnChoiceText(ELDSpawnChoiceResult Result)
{
    switch(Result)
    {
    case ELDSpawnChoiceResult::Chosen: return TEXT("出生点已选");
    case ELDSpawnChoiceResult::NoChoice: return TEXT("选择已结束");
    case ELDSpawnChoiceResult::OutOfRange: return TEXT("没有这个出生点");
    case ELDSpawnChoiceResult::StaleMatch: case ELDSpawnChoiceResult::StaleLife: return TEXT("这次复活已经过去");
    default: return TEXT("不能选择出生点");
    }
}
