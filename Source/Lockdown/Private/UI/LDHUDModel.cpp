#include "UI/LDHUDModel.h"
#include "Core/LDLog.h"
#include "UI/LDTerms.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentIds.h"
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
    case ELDDemoStatusId::Enraged: return TEXT("enraged");
    case ELDDemoStatusId::AmbushReady: return TEXT("ambush_ready");
    case ELDDemoStatusId::Exposed: return TEXT("exposed");
    case ELDDemoStatusId::Taunt: return TEXT("taunt");
    case ELDDemoStatusId::Disarm: return TEXT("disarm");
    case ELDDemoStatusId::Stun: return TEXT("stun");
    case ELDDemoStatusId::Empowered: return TEXT("empowered");
    case ELDDemoStatusId::Knockback: return TEXT("knockback");
    case ELDDemoStatusId::HealCut: return TEXT("heal_cut");
    case ELDDemoStatusId::DamageGuard: return TEXT("damage_guard");
    case ELDDemoStatusId::Vulnerable: return TEXT("vulnerable");
    case ELDDemoStatusId::Silence: return TEXT("silence");
    case ELDDemoStatusId::Burn: return TEXT("burn");
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
FString LDHUD::EquipmentName(const FLDDemoContentDefinitions* Content,uint8 EquipmentId)
{
    if(Content){if(const auto* Item=Content->Equipment.FindByPredicate([EquipmentId](const FLDDemoEquipmentDefinition& E){return E.NativeId==EquipmentId;})){return ContentName(Content,Item->Id);}}
    return FString::Printf(TEXT("未知装备 #%d"),EquipmentId);
}
FString LDHUD::NextStep(const FLDPublicMatchView& Public,const FGuid& Self,const FLDProgressionView* Progression,const FLDEquipmentView* Equipment,const FLDDemoContentDefinitions* Content,
    const FString& OwnerName)
{
    if(Public.Phase!=ELDMatchPhase::Countdown&&Public.Phase!=ELDMatchPhase::Development&&Public.Phase!=ELDMatchPhase::CaptureOpen){return FString();}
    const bool bGrowth=Progression&&Progression->bValid&&Progression->Generation==Public.Generation;
    if(bGrowth&&Progression->PendingChoiceCount>0){return FString::Printf(TEXT("下一步：%s ×%d（点右下角的%s按钮）"),LDTerms::Choice,int32(Progression->PendingChoiceCount),LDTerms::Choice);}
    // #153: before T=0 only the opening choice can be acted on.
    if(Public.Phase==ELDMatchPhase::Countdown){return FString();}
    if(Public.Phase==ELDMatchPhase::CaptureOpen)
    {
        if(Public.CircleState==ELDCircleState::Solo&&Self.IsValid()&&Public.Occupier==Self){return FString(TEXT("稳住：你"))+LDTerms::Holding;}
        if(Public.GraffitiOwner.IsValid()&&Self.IsValid()&&Public.GraffitiOwner==Self){return LDTerms::GuardOwn;}
        if(Public.GraffitiOwner.IsValid()){return LDTerms::Besiege(OwnerName.IsEmpty()?FString(TEXT("未列名参赛者")):OwnerName);}
        return FString(TEXT("下一步："))+LDTerms::CaptureOpened+LDTerms::CaptureGo;
    }
    const bool bWallet=Equipment&&Equipment->bValid&&Equipment->Generation==Public.Generation;
    if(bWallet&&Content&&Equipment->Slots.ContainsByPredicate([](const FLDEquipmentSlotView& Slot){return !Slot.bOccupied;}))
    {
        const FLDDemoEquipmentDefinition* Best=nullptr;
        for(const FLDDemoEquipmentDefinition& Item:Content->Equipment)
        {if(Item.BuyPrice>0&&Item.BuyPrice<=Equipment->Gold&&(!Best||Item.BuyPrice>Best->BuyPrice)){Best=&Item;}}
        if(Best){return FString(TEXT("下一步："))+LDTerms::Gold+TEXT("够买「")+ContentName(Content,Best->Id)+TEXT("」了，去")+LDTerms::Shop;}
    }
    if(bGrowth&&Progression->Level<=1){return FString::Printf(TEXT("下一步：去打附近的野怪搞%s、涨%s（右键攻击）"),LDTerms::Gold,LDTerms::Experience);}
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
FString LDHUD::Gold(int64 Gold)
{
    return FString::Printf(TEXT("%s %lld"),LDTerms::Gold,Gold);
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
    case ELDMatchPhase::Countdown: return LDTerms::OpeningCountdown;
    case ELDMatchPhase::Development: return FString(LDTerms::Development)+TEXT(" · ")+LDTerms::CapturePoint+TEXT("尚未开放");
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
int32 LDHUD::ReplacePickSlot(FName Action,ELDCombatEntry AtKey1,ELDCombatEntry AtKey2)
{
    const ELDCombatEntry Fired=Action==TEXT("Skill1")?AtKey1:Action==TEXT("Skill2")?AtKey2:ELDCombatEntry::None;
    return Fired==ELDCombatEntry::Q?0:Fired==ELDCombatEntry::W?1:INDEX_NONE;
}
FString LDHUD::MatchTimer(const FLDPublicMatchView& Public,bool bClock,int64 NowMs)
{
    if(!bClock||Public.StartedAtMs<=0){return FString();}
    const int64 Seconds=NowMs>Public.StartedAtMs?(NowMs-Public.StartedAtMs)/1000:0;
    return FString::Printf(TEXT("%lld:%02lld"),Seconds/60,Seconds%60);
}
FString LDHUD::OpeningCountdown(const FLDPublicMatchView& Public,bool bClock,int64 NowMs)
{
    if(!bClock||Public.StartedAtMs<=0){return FString();}
    if(Public.Phase==ELDMatchPhase::Countdown)
    {
        // The authority's T=0 boundary may land a frame after the local clock reaches it: hold "1" until it does.
        const int64 Seconds=NowMs<Public.StartedAtMs?(Public.StartedAtMs-NowMs+999)/1000:1;
        return FString::Printf(TEXT("%lld"),Seconds);
    }
    if(Public.Phase==ELDMatchPhase::Development&&NowMs>=Public.StartedAtMs&&NowMs-Public.StartedAtMs<CountdownGoShowMs)
    { return LDTerms::CountdownGo; }
    return FString();
}
LDHUD::FCircleView LDHUD::Circle(const FLDPublicMatchView& Public,const FString& SprayerName,const FString& OwnerName,bool bClock,int64 NowMs,
    int64 SprayMs,int64 GuardMs)
{
    FCircleView View;
    switch(Public.Phase)
    {
    case ELDMatchPhase::Development:
        View.Line=bClock&&Public.OpenAtMs>0?FString(LDTerms::Development)+TEXT(" · ")+LDTerms::CapturePoint+TEXT(" ")+Clock(Public.OpenAtMs-NowMs)+TEXT(" 后开放")
            :FString(LDTerms::Development)+TEXT(" · ")+LDTerms::CapturePoint+TEXT("尚未开放");
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
FString LDHUD::AirdropLine(const FLDAirdropView& Drop,bool bClock,int64 NowMs)
{
    switch(Drop.Phase)
    {
    case ELDAirdropPhase::Announced:
        return bClock?LDTerms::AirdropCountdown(Clock(Drop.LandAtMs-NowMs)):LDTerms::AirdropSoon();
    case ELDAirdropPhase::Landed: return LDTerms::AirdropLanded;
    default: return FString();
    }
}
FString LDHUD::BossLine(const FLDBossView& Boss,const FString& BossName,bool bClock,int64 NowMs)
{
    // Presentation only: how early the countdown shows.
    constexpr int64 CountdownMs=15000;
    const FString Name=BossName.IsEmpty()?FString(LDTerms::Boss):BossName;
    switch(Boss.Phase)
    {
    case ELDBossPhase::Pending:
        if(!bClock||Boss.AppearAtMs-NowMs>CountdownMs){return FString();}
        return Boss.AppearAtMs>NowMs?LDTerms::BossCountdown(Name,Clock(Boss.AppearAtMs-NowMs)):LDTerms::BossSoon(Name);
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
                const FKillFeedLineView Line=KillFeedLine(*Notice,In.Self,Name(Notice->Victim),Name(Notice->Killer),In.LowCampName,In.HighCampName,In.BossName,Assisters);
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
    if(Self.IsValid()&&Notice.Leader==Self){return LDTerms::LeaderSelf(Notice.BountyGold);}
    return LDTerms::LeaderNamed(LeaderName.IsEmpty()?FString(TEXT("未列名参赛者")):LeaderName,Notice.BountyGold);
}
FString LDHUD::KillLine(const FLDKillNotice& Notice,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName,const FString& BossName,const TArray<FString>& AssisterNames)
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
    case ELDKillCause::Boss: return LDTerms::KilledByLine(Victim,BossName);
    case ELDKillCause::Unowned: return Victim+TEXT(" ")+LDTerms::Dead+TEXT("（无参赛者击杀）");
    default: return Victim+TEXT(" ")+LDTerms::Dead+TEXT("（死因未知）");
    }
}
LDHUD::FKillFeedLineView LDHUD::KillFeedLine(const FLDKillNotice& Notice,const FGuid& Self,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName,const FString& BossName,const TArray<FString>& AssisterNames)
{
    FKillFeedLineView Line;
    const bool bVictim=Self.IsValid()&&Notice.Victim==Self;
    const bool bKiller=Self.IsValid()&&Notice.Cause==ELDKillCause::Contender&&Notice.Killer==Self;
    TArray<FString> Assisters=AssisterNames;
    bool bAssister=false;
    for(int32 Index=0;Index<Assisters.Num()&&Index<Notice.Assisters.Num();++Index)
    {if(Self.IsValid()&&Notice.Assisters[Index]==Self){Assisters[Index]=TEXT("你");bAssister=true;}}
    Line.bSelf=bVictim||bKiller||bAssister;
    Line.Text=KillLine(Notice,bVictim?FString(TEXT("你")):VictimName,bKiller?FString(TEXT("你")):KillerName,LowCampName,HighCampName,BossName,Assisters);
    return Line;
}
FString LDHUD::DeathCause(const FLDKillFeedView& Feed,uint32 Generation,const FGuid& Self,int64 NotBeforeMs,const FLDLobbyView& Lobby,const FString& LowCampName,const FString& HighCampName,const FString& BossName)
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

bool LDHUD::IsEquipmentSlot(EActionSlot Slot) { return Slot==EActionSlot::Equipment1||Slot==EActionSlot::Equipment2; }
LDHUD::FActionView LDHUD::Action(const FActionInput& Input,const FActionContext& Context)
{
    FActionView View;
    View.Slot=Input.Slot;View.Key=Context.KeyLabel;
    const bool bLearned=Input.Slot==EActionSlot::Q||Input.Slot==EActionSlot::W;
    const bool bDash=Input.Slot==EActionSlot::Dash;
    const bool bEquipment=IsEquipmentSlot(Input.Slot);
    View.Kind=bLearned?TEXT("角色主动"):bDash?TEXT("通用冲刺"):TEXT("装备");
    View.Name=!Input.bHasSource?(bLearned?FString(TEXT("空槽")):bDash?FString(LDTerms::Dash)+TEXT("不可用"):FString(TEXT("无装备")))
        :bLearned&&Input.Rank>0?FString::Printf(TEXT("%s %d级"),*Input.Name,Input.Rank):Input.Name;
    // Only a current own context may send; a predicted execution failure never suppresses the intent itself. An equipment
    // slot is never activated.
    View.bCanSubmitIntent=!bEquipment&&Context.bOwnContext&&Input.bProjected&&Context.bPlaying&&Context.bAlive&&!Context.bInputCaptured;
    FString State;
    auto Block=[&View,&State](uint32 Flag,const FString& Text){View.ExecutionBlocks|=Flag;if(State.IsEmpty()){State=Text;}};
    if(!Context.bOwnContext||!Input.bProjected){Block(ActionUnsynced,TEXT("未同步"));}
    if(!Context.bPlaying){Block(ActionNotPlaying,TEXT("对局未进行"));}
    else if(!Context.bAlive){Block(ActionDead,LDTerms::Dead);}
    if(Input.bProjected&&!Input.bHasSource){Block(ActionEmpty,bLearned?TEXT("空槽"):TEXT("无装备"));}
    else if(Input.bProjected&&bEquipment){Block(ActionPassive,TEXT("已装备"));}
    const bool bActive=Input.bProjected&&Input.bHasSource&&!bEquipment;
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
    if(bLearned&&Input.bHasSource&&Input.Rank>0){View.Rank=FString::FromInt(Input.Rank);}
    return View;
}
LDHUD::FActionDetailView LDHUD::ActionDetail(const FActionView& View,const FActionInput& Input,const FActionContext& Context,const FLDDemoContentDefinitions* Content)
{
    FActionDetailView Detail;
    Detail.Name=View.Name;
    Detail.Summary=View.Key.IsEmpty()?FString::Printf(TEXT("%s · %s"),*View.Kind,*View.State)
        :FString::Printf(TEXT("%s [%s] · %s"),*View.Kind,*View.Key,*View.State);
    const bool bLearned=Input.Slot==EActionSlot::Q||Input.Slot==EActionSlot::W;
    const bool bDash=Input.Slot==EActionSlot::Dash;
    TArray<FString> Lines;
    const FString Id=Input.DefinitionId.ToString();
    if(Input.bHasSource&&!bDash&&!Input.DefinitionId.IsNone())
    {
        FString Description;if(ContentText(Content,Id,true,Description)&&!Description.IsEmpty()){Lines.Add(Description);Lines.Add(FString());}
    }
    if(bDash&&Input.bHasSource)
    {Lines.Add(FString::Printf(TEXT("朝方向%s出 %dcm，碰到阻挡就停下；%s时不能%s；释放后的冷却不因换装改变"),LDTerms::Dash,Input.RangeCm,LDTerms::Root,LDTerms::Dash));}
    else if(bLearned&&Input.bHasSource)
    {
        if(const FLDDemoAbilityDefinition* Ability=Content?Content->Abilities.FindByPredicate([&Id](const FLDDemoAbilityDefinition& D){return D.Id==Id;}):nullptr)
        {
            Lines.Add(TargetLine(Ability->Target));
            if(const FLDDemoRankDefinition* Rank=Ability->Ranks.FindByPredicate([&Input](const FLDDemoRankDefinition& R){return R.Rank==Input.Rank;}))
            {Lines.Append(RankLines(*Rank,true));}
            else{Lines.Add(FString::Printf(TEXT("缺少 %d 级定义"),Input.Rank));}
        }
        else if(Content){Lines.Add(TEXT("内容目录中找不到该技能"));}
    }
    else if(Input.bHasSource)
    {
        if(const FLDDemoEquipmentDefinition* Item=Content?Content->Equipment.FindByPredicate([&Id](const FLDDemoEquipmentDefinition& D){return D.Id==Id;}):nullptr)
        {
            if(Input.bUpgraded){Detail.Name+=TEXT("（已进阶）");}
            for(const FLDDemoStatBonus& Stat:LDDemoEquipment::EffectiveStats(*Item,Input.bUpgraded)){Lines.Add(StatLine(Stat));}
            if(const TArray<FLDDemoEffectDefinition>* Passive=LDDemoEquipment::EffectivePassiveEffects(*Item,Input.bUpgraded))
            {for(const FLDDemoEffectDefinition& Effect:*Passive){Lines.Add(TEXT("被动：")+EffectLine(Effect));}}
        }
        else if(Content){Lines.Add(TEXT("内容目录中找不到该装备"));}
    }
    if(Context.bInputCaptured){Lines.Add(TEXT("当前输入暂不可用：界面占用或按键尚未松开"));}
    Detail.Body=FString::Join(Lines,TEXT("\n"));
    return Detail;
}
LDHUD::EActionLook LDHUD::ActionLook(const FActionView& View,bool bArmed)
{
    const uint32 Blocks=View.ExecutionBlocks;
    if(Blocks&(ActionUnsynced|ActionNotPlaying|ActionDead)){return EActionLook::Blocked;}
    if(Blocks&(ActionEmpty|ActionPassive)){return EActionLook::Empty;}
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
    Card.bEquipment=bContender;
    if(bContender)
    {
        auto Slot=[Content](uint8 Id){return Id?EquipmentName(Content,Id):FString(TEXT("空槽"));};
        Card.Equipment=FString::Printf(TEXT("装备：%s · %s"),*Slot(Unit.Equipment.Slots[0]),*Slot(Unit.Equipment.Slots[1]));
    }
    Card.bActives=bContender&&(Unit.AuthorizedFieldMask&LDObservation::Actives)!=0;
    const int32 MaxRank=Content&&Content->Progression.MaxSkillRank>0?Content->Progression.MaxSkillRank:int32(LDObservation::MaxActiveRank);
    for(int32 Index=0;Card.bActives&&Index<2;++Index)
    {
        FObservedCard::FActiveChip& Chip=Card.Actives[Index];
        Chip.Key=Index==0?TEXT("Q"):TEXT("W");
        const uint8 Id=Unit.Actives.Abilities[Index];
        Chip.bEmpty=Id==0;
        if(Chip.bEmpty){Chip.Name=TEXT("空槽");continue;}
        Chip.Name=AbilityName(Content,Id);
        Chip.bMaxRank=int32(Unit.Actives.Ranks[Index])>=MaxRank;
        Chip.Rank=FString::Printf(TEXT("Lv%d"),int32(Unit.Actives.Ranks[Index]));
        if(Chip.bMaxRank){Chip.Rank+=TEXT(" ★满级");}
    }
    return Card;
}
FString LDHUD::ObservedUnit(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec)
{
    const FObservedCard Card=ObservedCard(Unit,ContenderName,Content,SampledAtServerSec);
    FString Text=Card.Title+TEXT("\n")+Card.Health.Text;
    if(Card.bMana){Text+=TEXT(" · ")+Card.Mana.Text;}
    Text+=TEXT("\n")+Card.Statuses;
    if(Card.bEquipment){Text+=TEXT("\n")+Card.Equipment;}
    if(Card.bActives)
    {
        Text+=TEXT("\n技能：");
        for(int32 Index=0;Index<2;++Index)
        {
            const FObservedCard::FActiveChip& Chip=Card.Actives[Index];
            Text+=(Index?TEXT(" · "):TEXT(""))+Chip.Key+TEXT(" ")+Chip.Name+(Chip.bEmpty?FString():TEXT(" ")+Chip.Rank);
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
FString LDHUD::PenaltyLine(const FLDDemoStatBonus& Stat)
{
    // A wild card's explicit deduction: the same stat line with the sign turned.
    FString Line=StatLine(Stat);
    const int32 Plus=Line.Find(TEXT(" +"));
    if(Plus!=INDEX_NONE){Line[Plus+1]=TEXT('-');}
    return Line;
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
    case ELDDemoEffectOp::OutOfCombatRegen:
    {
        FString Text=TEXT("脱战 ")+HudSeconds(Effect.DelayMs)+TEXT(" 后每秒回复 ")+HudPercent(Effect.MaxHPPerSecBp)+TEXT(" ")+LDTerms::MaxHealth();
        if(Effect.MultiplierBp>0){Text+=TEXT("；受到技能伤害后 ")+HudSeconds(Effect.DurationMs)+TEXT(" 内")+LDTerms::HealthRegen()+TEXT("与")+LDTerms::Mana+TEXT("回复变为 ")+HudPercent(Effect.MultiplierBp);}
        if(Effect.RatioBp>0){Text+=TEXT("；战斗中保留 ")+HudPercent(Effect.RatioBp);}
        return Text;
    }
    case ELDDemoEffectOp::Lifesteal: return FString(LDTerms::BasicAttack)+TEXT("打掉的")+LDTerms::Health+TEXT("的 ")+HudPercent(Effect.RatioBp)+TEXT(" 回复自身");
    case ELDDemoEffectOp::Dash: return TEXT("朝方向冲刺 ")+HudRational(Effect.DistanceCm)+TEXT("cm（")+HudSeconds(Effect.DurationMs)+TEXT("）");
    case ELDDemoEffectOp::DashBoost:
    {
        FString Text=FString(LDTerms::Dash)+TEXT("的冷却变为 ")+HudPercent(Effect.CooldownRatioBp)+TEXT("，")+LDTerms::Dash+TEXT("的距离 +")+HudPercent(Effect.DistanceBonusBp);
        if(Effect.SkillDistanceBonusBp>0||Effect.SkillCooldownRatioBp>0)
        {Text+=TEXT("；带冲刺或跳跃的技能距离 +")+HudPercent(Effect.SkillDistanceBonusBp)+TEXT("，冷却变为 ")+HudPercent(Effect.SkillCooldownRatioBp);}
        return Text;
    }
    case ELDDemoEffectOp::Cleave: return TEXT("横扫：主目标周围 ")+HudRational(Effect.RadiusCm)+TEXT("cm 内造成主击 ")+HudPercent(Effect.RatioBp)+TEXT(" 的")+LDTerms::PhysicalDamage+TEXT("副击");
    case ELDDemoEffectOp::Slow:
    {
        FString Text=FString(LDTerms::Slow)+TEXT("：")+LDTerms::MoveSpeed+TEXT(" -")+HudPercent(Effect.RatioBp)+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);
        if(Effect.VampRatioBp>0){Text+=TEXT("；期间对其造成的")+FString(LDTerms::BasicAttack)+TEXT("与技能伤害吸血 ")+HudPercent(Effect.VampRatioBp);}
        return Text;
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
        if(Effect.VampRatioBp>0){Text+=TEXT("，全能吸血 ")+HudPercent(Effect.VampRatioBp);}
        return FString(LDTerms::Health)+TEXT("低于 ")+HudPercent(Effect.RatioBp)+TEXT(" 时：")+Text;
    }
    case ELDDemoEffectOp::AmbushDamage:
    {
        FString Text=TEXT("脱战 ")+HudSeconds(Effect.DelayMs)+TEXT(" 后，下一次")+LDTerms::BasicAttack+TEXT("或伤害技能额外造成")+HudDamageTerm(Effect)+TEXT(" ")+HudScaling(Effect);
        if(Effect.AmountMs>0){Text+=TEXT("；每次冲刺或跳跃使准备时间 -")+HudSeconds(Effect.AmountMs);}
        if(Effect.RatioBp>0){Text+=TEXT("；首击使目标")+FString(LDTerms::Armor)+TEXT("与")+LDTerms::MagicResist+TEXT(" -")+HudPercent(Effect.RatioBp)+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);}
        return Text;
    }
    case ELDDemoEffectOp::ControlDamageAmp: return TEXT("对处于任何控制或")+FString(LDTerms::Slow)+TEXT("下的敌人，伤害 +")+HudPercent(Effect.RatioBp);
    case ELDDemoEffectOp::Taunt: return FString(LDTerms::Taunt)+TEXT(" ")+HudSeconds(Effect.DurationMs)+TEXT("：自身周围 ")+HudRational(Effect.RadiusCm)+TEXT("cm 内的敌人");
    case ELDDemoEffectOp::Disarm: return FString(LDTerms::Disarm)+TEXT(" ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::Stun: return FString(LDTerms::Stun)+TEXT(" ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::Backstab:
        return TEXT("背后出手：伤害 +")+HudPercent(Effect.RatioBp)+TEXT("，")+LDTerms::Stun+TEXT(" ")+HudSeconds(Effect.DurationMs)
            +(Effect.bImpaired?FString(TEXT("；目标处于")+FString(LDTerms::Root)+TEXT("、")+LDTerms::Slow+TEXT("或")+LDTerms::Taunt+TEXT("时也按背后结算")):FString());
    case ELDDemoEffectOp::EmpowerBasic:
        return HudSeconds(Effect.DurationMs)+TEXT(" 内下一次")+LDTerms::BasicAttack+TEXT("的主击额外造成 ")+HudScaling(Effect);
    case ELDDemoEffectOp::Jump: return TEXT("跳向指定地点（")+HudSeconds(Effect.DurationMs)+TEXT(" 后落地）");
    case ELDDemoEffectOp::Thorns:
    {
        FString Text=TEXT("被 ")+HudRational(Effect.RadiusCm)+TEXT("cm 内敌人的")+LDTerms::BasicAttack+(Effect.bAbilityTrigger?TEXT("或技能打中时"):TEXT("打中时"))
            +TEXT("，对方受")+HudDamageTerm(Effect)+TEXT(" ")+HudPoints(Effect.BaseP2);
        if(Effect.ArmorRatioBp>0){Text+=TEXT(" + ")+HudPercent(Effect.ArmorRatioBp)+LDTerms::Armor;}
        if(Effect.MaxHPRatioBp>0){Text+=TEXT(" + ")+HudPercent(Effect.MaxHPRatioBp)+LDTerms::MaxHealth();}
        if(Effect.CriticalMultiplierBp>10000){Text+=TEXT("，被")+FString(LDTerms::Crit)+TEXT("时变为 ")+HudPercent(Effect.CriticalMultiplierBp);}
        Text+=TEXT("；同一人 ")+HudSeconds(Effect.IntervalMs)+TEXT(" 内最多一次");
        if(Effect.bRearExempt){Text+=TEXT("，背后来袭不触发");}
        if(Effect.RatioBp>0){Text+=TEXT("；反伤使对方")+FString(LDTerms::AttackSpeed)+TEXT("与")+LDTerms::MoveSpeed+TEXT(" -")+HudPercent(Effect.RatioBp)+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);}
        return Text;
    }
    case ELDDemoEffectOp::CooldownRefund:
    {
        static const TCHAR* Triggers[]={TEXT(""),TEXT("施放后"),TEXT("强化普攻命中后"),TEXT("持续期间每次普攻命中，"),TEXT("普攻暴击时"),TEXT("解除了控制或减速时"),TEXT(""),TEXT(""),TEXT(""),TEXT("")};
        static const TCHAR* Entries[]={TEXT(""),TEXT("普攻节拍"),TEXT("本技能"),TEXT("其他主动技能"),TEXT("Q/W")};
        const FString Amount=Effect.AmountMs>0?TEXT("剩余冷却 -")+HudSeconds(Effect.AmountMs):(Effect.RatioBp>=10000?FString(TEXT("立即就绪")):TEXT("返还 ")+HudPercent(Effect.RatioBp)+TEXT(" 冷却"));
        return FString(Triggers[uint8(Effect.Trigger)%10])+Entries[uint8(Effect.Entries)%5]+Amount;
    }
    case ELDDemoEffectOp::AbilityCrit:
        return Effect.CritMode==ELDDemoCritMode::Impaired
            ? FString(TEXT("目标处于任何控制或破防时，本次必定技能")+FString(LDTerms::Crit))
            : TEXT("指定技能以 ")+HudPercent(Effect.RatioBp)+TEXT(" 的")+LDTerms::CritChance()+TEXT("进行技能")+LDTerms::Crit;
    case ELDDemoEffectOp::ResistShred:
        return TEXT("命中后目标")+FString(Effect.ArmorRatioBp>0?LDTerms::Armor:LDTerms::MagicResist)+TEXT(" -")+HudPercent(FMath::Max(Effect.ArmorRatioBp,Effect.MagicResistRatioBp))+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::Momentum:
        return TEXT("之后 ")+HudSeconds(Effect.DurationMs)+TEXT(" 内每移动 100cm 积攒 ")+HudPoints(Effect.BaseP2)+TEXT(" 点（最多 ")+HudPoints(Effect.CapP2)+TEXT("），并入下一次")+LDTerms::BasicAttack+TEXT("的主击");
    case ELDDemoEffectOp::TauntRetaliate:
        return FString(LDTerms::Taunt)+TEXT("结束时，周围 ")+HudRational(Effect.RadiusCm)+TEXT("cm 内受")+HudDamageTerm(Effect)+TEXT(" ")+HudPoints(Effect.BaseP2)+TEXT(" + 每次挨打 ")+HudPoints(Effect.PerCountP2)
            +TEXT("（最多 ")+FString::FromInt(Effect.Count)+TEXT(" 次）+ ")+HudPercent(Effect.ArmorRatioBp)+LDTerms::Armor;
    case ELDDemoEffectOp::TriggeredShield:
        return (Effect.Trigger==ELDDemoEffectTrigger::LowHP
                ? FString(LDTerms::Health)+TEXT("跌破 ")+HudPercent(Effect.RatioBp)+TEXT(" 时（内置冷却 ")+HudSeconds(Effect.IntervalMs)+TEXT("）获得")
                : FString(TEXT("落地每命中 1 个敌人获得")))
            +LDTerms::Shield+TEXT(" ")+HudScaling(Effect)+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::BasicStacks:
    {
        FString Text;for(const FLDDemoStatBonus& Stat:Effect.Stats){Text+=(Text.IsEmpty()?TEXT(""):TEXT("，"))+StatLine(Stat);}
        Text=TEXT("每次")+FString(LDTerms::BasicAttack)+TEXT("获得 ")+HudSeconds(Effect.DurationMs)+TEXT(" ")+Text+TEXT("，最多 ")+FString::FromInt(Effect.Count)+TEXT(" 层");
        if(Effect.RatioBp>0){Text+=TEXT("；满层时")+FString(LDTerms::BasicAttack)+TEXT("前摇缩短 ")+HudPercent(Effect.RatioBp);}
        return Text;
    }
    case ELDDemoEffectOp::LongReach:
    {
        FString Text=TEXT("与目标每远 100cm，伤害 +")+HudPercent(Effect.RatioBp)+TEXT("（最多 ")+HudPercent(Effect.CapBp)+TEXT("）");
        if(Effect.DistanceCm.Numerator>0){Text+=TEXT("；方向投射与指向技能射程 +")+HudRational(Effect.DistanceCm)+TEXT("cm");}
        return Text;
    }
    case ELDDemoEffectOp::CritDamageReduction: return TEXT("受到的")+FString(LDTerms::Crit)+TEXT("伤害 -")+HudPercent(Effect.RatioBp);
    case ELDDemoEffectOp::NthBasicBonus:
        return TEXT("每第 ")+FString::FromInt(Effect.Count)+TEXT(" 次")+LDTerms::BasicAttack+TEXT("额外造成 ")+HudPoints(Effect.BaseP2)+TEXT(" ")+HudDamageTerm(Effect);
    case ELDDemoEffectOp::ManaOnHit:
        return (Effect.Trigger==ELDDemoEffectTrigger::Control?FString(TEXT("亲自施加控制或")+FString(LDTerms::Slow)+TEXT("时")):FString(TEXT("技能命中时")))
            +TEXT("回复 ")+HudPoints(Effect.BaseP2)+TEXT(" ")+LDTerms::Mana+TEXT("（每次施放一次）");
    case ELDDemoEffectOp::DispelSlow: return FString(TEXT("解除自身全部"))+LDTerms::Slow;
    case ELDDemoEffectOp::PathSlow:
        return TEXT("冲刺路径上的敌人")+FString(LDTerms::Slow)+TEXT(" ")+HudPercent(Effect.RatioBp)+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);
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
    case ELDDemoTargetMode::DirectionDash: return TEXT("方向冲刺");
    case ELDDemoTargetMode::GroundPoint: return TEXT("指定地点 · 最远 ")+HudRational(Target.RangeCm)+TEXT("cm");
    default: return TEXT("未知目标方式");
    }
}
TArray<FString> LDHUD::RankLines(const FLDDemoRankDefinition& Rank,bool bActive)
{
    TArray<FString> Lines;
    for(const FLDDemoEffectDefinition& Effect:Rank.Effects){Lines.Add(EffectLine(Effect));}
    if(bActive){Lines.Add(FString(LDTerms::Mana)+TEXT(" ")+HudPoints(Rank.ManaCostP2)+TEXT(" · 冷却 ")+HudSeconds(Rank.CooldownMs));}
    return Lines;
}
LDHUD::FChoiceCardView LDHUD::ChoiceCard(const FLDChoiceOption& Option,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    FChoiceCardView Card;
    const FString Id=Option.DefinitionId.ToString();
    Card.Name=ContentName(Content,Id);
    FString Description;if(ContentText(Content,Id,true,Description)){Card.Description=Description;}
    if(const FLDDemoDisplayDefinition* Display=Content?Content->Display.FindByPredicate([&Id](const FLDDemoDisplayDefinition& D){return D.Id==Id;}):nullptr)
    {Card.IconId=Display->IconId;}
    Card.Glyph=Card.Name.IsEmpty()?FString(TEXT("?")):Card.Name.Left(1);
    switch(Option.Kind)
    {
    case ELDChoiceOptionKind::LearnActive: Card.Kind=TEXT("新主动技能");Card.Icon=EChoiceIcon::Active;break;
    case ELDChoiceOptionKind::UpgradeActive: Card.Kind=TEXT("主动技能升级");Card.Icon=EChoiceIcon::Active;break;
    case ELDChoiceOptionKind::LearnPassive: Card.Kind=TEXT("新被动技能");Card.Icon=EChoiceIcon::Passive;break;
    case ELDChoiceOptionKind::UpgradePassive: Card.Kind=TEXT("被动技能升级");Card.Icon=EChoiceIcon::Passive;break;
    case ELDChoiceOptionKind::Attribute: Card.Kind=TEXT("属性提升");Card.Icon=EChoiceIcon::Attribute;break;
    default: Card.Kind=TEXT("未知选项");break;
    }
    if(Option.Kind==ELDChoiceOptionKind::Attribute)
    {
        const FLDAttributeCardView* Owned=Progression.AttributeCards.FindByPredicate([&Option](const FLDAttributeCardView& C){return C.DefinitionId==Option.DefinitionId;});
        Card.RankText=FString::Printf(TEXT("已选 %d 次"),Owned?int32(Owned->Count):0);
        Card.Summary=Card.RankText;Card.Detail=Card.Kind+TEXT(" · ")+Card.RankText;
        if(const FLDDemoAttributeDefinition* Definition=Content?Content->Attributes.FindByPredicate([&Id](const FLDDemoAttributeDefinition& D){return D.Id==Id;}):nullptr)
        {for(const FLDDemoStatBonus& Stat:Definition->Stats){Card.Lines.Add(StatLine(Stat));}for(const FLDDemoStatBonus& Penalty:Definition->Penalties){Card.Lines.Add(PenaltyLine(Penalty));}}
        return Card;
    }
    Card.RankText=Option.FromRank>0?FString::Printf(TEXT("%d → %d 级"),Option.FromRank,Option.ToRank):FString::Printf(TEXT("%d 级"),Option.ToRank);
    if(const FLDDemoAbilityDefinition* Ability=Content?Content->Abilities.FindByPredicate([&Id](const FLDDemoAbilityDefinition& D){return D.Id==Id;}):nullptr)
    {
        const bool bActive=Ability->Kind==ELDDemoAbilityKind::Active;
        if(bActive){Card.Lines.Add(TargetLine(Ability->Target));}
        if(const FLDDemoRankDefinition* Rank=Ability->Ranks.FindByPredicate([&Option](const FLDDemoRankDefinition& R){return R.Rank==Option.ToRank;}))
        {Card.Lines.Append(RankLines(*Rank,bActive));}
        else{Card.Lines.Add(FString::Printf(TEXT("缺少 %d 级定义"),Option.ToRank));}
    }
    else if(Content){Card.Lines.Add(TEXT("内容目录中找不到该技能"));}
    if(Option.Kind==ELDChoiceOptionKind::LearnActive)
    {
        int32 Filled=0;for(const FLDLearnedSkillView& Skill:Progression.Skills){if(Skill.bActive){++Filled;}}
        Card.bNeedsReplacement=Filled>=2;
    }
    Card.Summary=Option.FromRank>0?FString::Printf(TEXT("%d→%d"),Option.FromRank,Option.ToRank):FString(TEXT("新"));
    Card.Detail=Card.Kind+TEXT(" · ")+Card.RankText;
    return Card;
}
TArray<FString> LDHUD::UpgradeLines(const FLDDemoEquipmentDefinition& Item)
{
    TArray<FString> Lines;
    for(const FLDDemoStatBonus& Stat:Item.UpgradedStats){Lines.Add(StatLine(Stat));}
    if(const TArray<FLDDemoEffectDefinition>* Passive=LDDemoEquipment::EffectivePassiveEffects(Item,true))
    {for(const FLDDemoEffectDefinition& Effect:*Passive){Lines.Add(TEXT("被动：")+EffectLine(Effect));}}
    return Lines;
}
TArray<FLDShopItemView> LDHUD::CatalogItems(const FLDDemoContentDefinitions* Content)
{
    TArray<FLDShopItemView> Items;
    if(!Content){return Items;}
    for(const FLDDemoEquipmentDefinition& Definition:Content->Equipment)
    {
        FLDShopItemView& Row=Items.AddDefaulted_GetRef();
        Row.DefinitionId=FName(*Definition.Id);Row.BuyPrice=Definition.BuyPrice;Row.SellGold=Definition.SellGold;
    }
    return Items;
}
int32 LDHUD::NextPreorder(const TArray<FName>& Queue,const TArray<FLDShopItemView>& Listing,int64 Gold,int32 FreeSlots,const TArray<FName>& Upgradable,int64 UpgradePrice)
{
    for(int32 Index=0;Index<Queue.Num();++Index)
    {
        const FName Wanted=Queue[Index];
        if(Upgradable.Contains(Wanted))
        {
            if(UpgradePrice>0&&UpgradePrice<=Gold){return Index;}
            continue;
        }
        if(FreeSlots<=0){continue;}
        const FLDShopItemView* Row=Listing.FindByPredicate([Wanted](const FLDShopItemView& Item){return Item.DefinitionId==Wanted;});
        if(Row&&Row->BuyPrice>0&&Row->BuyPrice<=Gold){return Index;}
    }
    return INDEX_NONE;
}
LDHUD::FShopItemView LDHUD::ShopItem(const FLDShopItemView& Item,int64 Gold,const FLDDemoContentDefinitions* Content)
{
    FShopItemView View;
    const FString Id=Item.DefinitionId.ToString();
    View.Name=ContentName(Content,Id);
    View.Price=FString::Printf(TEXT("买价 %lld · 卖价 %lld"),Item.BuyPrice,Item.SellGold);
    View.bAffordable=Gold>=Item.BuyPrice;
    if(const FLDDemoEquipmentDefinition* Definition=Content?Content->Equipment.FindByPredicate([&Id](const FLDDemoEquipmentDefinition& D){return D.Id==Id;}):nullptr)
    {
        for(const FLDDemoStatBonus& Stat:Definition->Stats){View.Lines.Add(StatLine(Stat));}
        if(Definition->HasPassive){for(const FLDDemoEffectDefinition& Effect:Definition->Passive.Effects){View.Lines.Add(TEXT("被动：")+EffectLine(Effect));}}
    }
    else if(Content){View.Lines.Add(TEXT("内容目录中找不到该装备"));}
    return View;
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
    View.Breakdown=TEXT("基础 ")+Value(Row.Base)+TEXT(" · 等级 ")+Value(Row.Level)+TEXT(" · 三选一 ")+Value(Row.Choices)+TEXT(" · 被动 ")+Value(Row.Passives)
        +TEXT(" · 装备 ")+Value(Row.Equipment);
    if(Row.PercentBonus.Denominator>0&&Row.PercentBonus.Numerator!=0)
    {View.Breakdown+=TEXT(" · 百分比 +")+HudDecimal(100.*double(Row.PercentBonus.Numerator)/double(Row.PercentBonus.Denominator))+TEXT("%");}
    if(Row.Penalty.Denominator>0&&Row.Penalty.Numerator!=0){View.Breakdown+=TEXT(" · 代价 -")+Value(Row.Penalty);}
    if(Row.PenaltyPercent.Denominator>0&&Row.PenaltyPercent.Numerator!=0)
    {View.Breakdown+=TEXT(" · 代价百分比 -")+HudDecimal(100.*double(Row.PenaltyPercent.Numerator)/double(Row.PenaltyPercent.Denominator))+TEXT("%");}
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
    case ELDGrowthReason::Dead: return FString(LDTerms::Dead)+TEXT("，不能交易");
    case ELDGrowthReason::MatchNotStarted: return LDTerms::NotStartedYet;
    case ELDGrowthReason::RequestConflict: return TEXT("请求冲突，未执行");
    case ELDGrowthReason::OutOfOrder: return TEXT("请求顺序错误，未执行");
    case ELDGrowthReason::ReceiptExpired: return TEXT("回执已过期，请以当前状态为准");
    case ELDGrowthReason::RequestInFlight: return TEXT("上一个请求仍在处理中");
    case ELDGrowthReason::NoOffer: return TEXT("当前没有待选奖励");
    case ELDGrowthReason::UnknownOffer: return TEXT("这组奖励已失效");
    case ELDGrowthReason::InvalidOption: return TEXT("该选项无效");
    case ELDGrowthReason::OfferAlreadyCommitted: return TEXT("这次奖励已经选过了");
    case ELDGrowthReason::OfferGuardStale: return TEXT("内容已更新，请重新确认");
    case ELDGrowthReason::BuildStale: return TEXT("构筑已变化，请重新确认");
    case ELDGrowthReason::ReplacementRequired: return TEXT("主动技能槽已满，需要选择替换哪个技能");
    case ELDGrowthReason::ReplacementMismatch: return TEXT("要替换的技能已变化，请重新选择");
    case ELDGrowthReason::ReplacementNotAllowed: return TEXT("该选项不能替换技能");
    case ELDGrowthReason::NoReroll: return TEXT("没有免费重抽次数");
    case ELDGrowthReason::OfferAlreadyRerolled: return TEXT("这一组已经重抽过一次");
    case ELDGrowthReason::NoShopSession: return FString(LDTerms::Shop)+TEXT("尚未打开");
    case ELDGrowthReason::QuoteStale: return TEXT("报价已更新，请重新确认");
    case ELDGrowthReason::PriceChanged: return TEXT("价格已变化，请重新确认");
    case ELDGrowthReason::UnknownItem: return TEXT("未知商品");
    case ELDGrowthReason::ItemNotOwned: return TEXT("你没有这件装备");
    case ELDGrowthReason::ItemStale: return TEXT("装备已变化，请重新确认");
    case ELDGrowthReason::EquipmentFull: return TEXT("装备槽已满，请先卖出一件");
    case ELDGrowthReason::InsufficientGold: return LDTerms::NotEnoughGold();
    case ELDGrowthReason::InventoryStale: return TEXT("装备已变化，请重新确认");
    case ELDGrowthReason::AccountStale: return FString(LDTerms::Gold)+TEXT("已变化，请重新确认");
    case ELDGrowthReason::UnknownShop: return FString(TEXT("未知的"))+LDTerms::Shop;
    case ELDGrowthReason::StaleWorld: return TEXT("世界版本不一致");
    case ELDGrowthReason::OutsideRange: case ELDGrowthReason::InvalidApproachRegion: return FString(TEXT("不在"))+LDTerms::Shop+TEXT("交互范围内");
    case ELDGrowthReason::NotGrounded: return TEXT("需要站在地面上");
    case ELDGrowthReason::InvalidGround: case ELDGrowthReason::WrongSurface: return TEXT("当前位置不能交易");
    case ELDGrowthReason::DataNotReady: return TEXT("数据尚未就绪");
    case ELDGrowthReason::StaleContext: return TEXT("状态已变化，请重新确认");
    case ELDGrowthReason::UpgradeUsed: return TEXT("本局的装备进阶已经用过了");
    case ELDGrowthReason::UpgradeUnavailable: return TEXT("这件装备暂时不能进阶");
    case ELDGrowthReason::AlreadyUpgraded: return TEXT("这件装备已经进阶");
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
    Input.RangeCm=Entry.RangeCm;
    Input.DefinitionId=Entry.DefinitionId;
    Input.Rank=Entry.Rank;Input.ManaCostP2=Entry.ManaCostP2;
    Input.ReadyAtMs=Entry.ReadyAtMs;Input.CooldownMs=Entry.CooldownMs;
    Input.ServerBlock=CombatRejectText(Entry.Unavailable);
    return Input;
}
LDHUD::FActionInput LDHUD::EquipmentInput(const FLDEquipmentSlotView& Held,EActionSlot Slot,const FLDDemoContentDefinitions* Content)
{
    FActionInput Input;
    Input.Slot=Slot;Input.bProjected=true;
    Input.bHasSource=Held.bOccupied&&!Held.DefinitionId.IsNone();
    if(Input.bHasSource)
    {
        Input.DefinitionId=Held.DefinitionId;
        Input.Name=ContentName(Content,Held.DefinitionId.ToString());
        Input.bUpgraded=Held.bUpgraded;
    }
    return Input;
}
namespace
{
// Combat lists each equipment passive once by its family; the player sees the equipment that grants it.
FString HudPassiveSource(const FLDDemoContentDefinitions* Content,const FName& Passive)
{
    const FString Id=Passive.ToString();
    const FLDDemoEquipmentDefinition* Item=Content?Content->Equipment.FindByPredicate([&Id](const FLDDemoEquipmentDefinition& D){return D.HasPassive&&(D.Id==Id||D.Passive.Family==Id);}):nullptr;
    return Item?Item->Id:Id;
}
}
FString LDHUD::SelfStatusLine(const FLDCombatOwnerView& Combat,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* Content)
{
    TArray<FString> Parts;
    for(const FLDCombatStatusView& Status:Combat.Statuses)
    {
        if(Status.bIsPassive||Status.StatusId==uint8(ELDDemoStatusId::RespawnProtection)){continue;}
        FString Part=StatusName(Content,Status.StatusId);
        if(Status.AmountP2>0){Part+=FString::Printf(TEXT(" %lld"),HudWholePoints(Status.AmountP2,true));}
        if(Status.ExpiresAtMs>0&&bClock){Part+=TEXT(" ")+TenthsUp(Status.ExpiresAtMs-NowMs);}
        Parts.Add(MoveTemp(Part));
    }
    return FString::Join(Parts,TEXT(" · "));
}
FString LDHUD::PassiveLine(const FLDProgressionView* Progression,const FLDCombatOwnerView* Combat,const FLDDemoContentDefinitions* Content)
{
    TArray<FString> Parts;
    if(!Progression){Parts.Add(TEXT("角色被动同步中"));}
    else{for(const FLDLearnedSkillView& Skill:Progression->Skills){if(!Skill.bActive){Parts.Add(FString::Printf(TEXT("%s %d级"),*ContentName(Content,Skill.DefinitionId.ToString()),int32(Skill.Rank)));}}}
    if(!Combat){Parts.Add(TEXT("装备被动同步中"));}
    else
    {
        TArray<FName> Seen;
        for(const FName& Passive:Combat->EquipmentPassives)
        {
            if(Seen.Contains(Passive)){continue;}
            Seen.Add(Passive);Parts.Add(ContentName(Content,HudPassiveSource(Content,Passive)));
        }
    }
    return Parts.IsEmpty()?FString(TEXT("被动：无")):TEXT("被动：")+FString::Join(Parts,TEXT("、"));
}
FString LDHUD::EntryLabel(ELDCombatEntry Entry)
{
    switch(Entry)
    {
    case ELDCombatEntry::Basic: return LDTerms::BasicAttack;
    case ELDCombatEntry::Q: return TEXT("角色技能 1");
    case ELDCombatEntry::W: return TEXT("角色技能 2");
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
    const bool bSlot=Receipt.Entry==ELDCombatEntry::Q||Receipt.Entry==ELDCombatEntry::W||Receipt.Entry==ELDCombatEntry::Dash;
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