#include "UI/LDHUDModel.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentIds.h"
#include "Combat/LDAttributeProjection.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"

namespace
{
// A missing display text is a content error; report each id once instead of every frame.
void HudReportMissingText(const FString& Id)
{
    static TSet<FString> Reported;
    bool bAlready=false;Reported.Add(Id,&bAlready);
    if(!bAlready){UE_LOG(LogTemp,Error,TEXT("LD_HUD_DISPLAY_TEXT_MISSING %s: the approved DemoContent display text is not registered; showing the stable id."),*Id);}
}
const TCHAR* HudStatusContentId(uint8 Id)
{
    switch(static_cast<ELDDemoStatusId>(Id))
    {
    case ELDDemoStatusId::Root: return TEXT("root");
    case ELDDemoStatusId::Shield: return TEXT("shield");
    case ELDDemoStatusId::CampReturning: return TEXT("camp_returning");
    case ELDDemoStatusId::RespawnProtection: return TEXT("respawn_protection");
    default: return nullptr;
    }
}
int64 HudWholePoints(int64 P2,bool bRoundUp)
{
    return bRoundUp&&P2>0?(P2+99)/100:P2>=0?P2/100:-((-P2)/100);
}
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
    {View.Level=TEXT("等级 —");View.Experience=TEXT("经验数据异常");return View;}
    View.Level=FString::Printf(TEXT("等级 %d"),InLevel);
    if(!bHasNext||InLevel==MaxLevel)
    {View.bMax=true;View.Fraction=1.f;View.Experience=TEXT("经验 MAX");return View;}
    const int64 Span=NextThresholdXp-LevelThresholdXp;
    if(Span<=0){View.Experience=TEXT("经验数据异常");return View;}
    const int64 Progress=FMath::Clamp<int64>(CumulativeXp-LevelThresholdXp,0,Span);
    View.Fraction=float(double(Progress)/double(Span));
    View.Experience=FString::Printf(TEXT("经验 %lld/%lld"),Progress,Span);
    return View;
}
FString LDHUD::Gold(int64 Gold)
{
    return FString::Printf(TEXT("零花钱 %lld"),Gold);
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
        if(!bClock){return TEXT("复活保护 · 时间同步中");}
        return Own.ProtectedUntilMs>NowMs?TEXT("复活保护 ")+TenthsUp(Own.ProtectedUntilMs-NowMs)+TEXT(" · 发起进攻即解除，失败也解除"):FString(TEXT("存活"));
    case ELDLifePhase::Dead:
        if(Own.RespawnAtMs<=0){return TEXT("社死");}
        if(!bClock){return TEXT("社死 · 回宿舍冷静（时间同步中）");}
        return TEXT("社死 · 回宿舍冷静 ")+TenthsUp(Own.RespawnAtMs-NowMs);
    case ELDLifePhase::Unspawned: return TEXT("等待出生");
    case ELDLifePhase::Left: return TEXT("已离开本局");
    default: return TEXT("生命状态未知");
    }
}
LDHUD::FDeathView LDHUD::Death(const FLDOwnerState& Own,bool bClock,int64 NowMs)
{
    FDeathView View;
    if(Own.LifePhase!=ELDLifePhase::Dead){return View;}
    View.bVisible=true;View.Title=TEXT("社死");
    View.Countdown=Own.RespawnAtMs<=0?FString(TEXT("回宿舍冷静 · 等待复活时间"))
        :!bClock?FString(TEXT("回宿舍冷静 · 时间同步中")):TEXT("回宿舍冷静 ")+TenthsUp(Own.RespawnAtMs-NowMs);
    View.Note=TEXT("等级、构筑与零花钱全部保留 · 选卡不影响复活");
    return View;
}
FString LDHUD::PhaseLine(ELDMatchPhase Phase)
{
    switch(Phase)
    {
    case ELDMatchPhase::Lobby: return TEXT("房间等待中");
    case ELDMatchPhase::Starting: return TEXT("对局即将开始");
    case ELDMatchPhase::Development: return TEXT("发育期 · 升旗台尚未开放");
    case ELDMatchPhase::CaptureOpen: return TEXT("升旗台已开放 · 连续独占即可加冕");
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
LDHUD::FCircleView LDHUD::Circle(const FLDPublicMatchView& Public,const FString& OccupierName,bool bClock,int64 NowMs)
{
    FCircleView View;
    switch(Public.Phase)
    {
    case ELDMatchPhase::Development:
        View.Line=bClock&&Public.OpenAtMs>0?TEXT("发育期 · 升旗台 ")+Clock(Public.OpenAtMs-NowMs)+TEXT(" 后开放"):FString(TEXT("发育期 · 升旗台尚未开放"));
        return View;
    case ELDMatchPhase::CaptureOpen:
        switch(Public.CircleState)
        {
        case ELDCircleState::Empty: View.Line=TEXT("升旗台已开放 · 无人占领");return View;
        case ELDCircleState::Contested: View.Line=TEXT("升旗台争夺中 · 占领进度清零");return View;
        case ELDCircleState::Solo:
        {
            const FString Name=OccupierName.IsEmpty()?FString(TEXT("未列名参赛者")):OccupierName;
            const int64 Span=Public.DueAtMs-Public.SoloSinceMs;
            if(!bClock||Span<=0){View.Line=Name+TEXT(" 正在独占升旗台 · 进度同步中");return View;}
            View.bProgress=true;View.Progress=float(FMath::Clamp(double(NowMs-Public.SoloSinceMs)/double(Span),0.,1.));
            View.Line=Name+TEXT(" 正在独占升旗台 · 剩余 ")+TenthsUp(Public.DueAtMs-NowMs);
            return View;
        }
        default: View.Line=TEXT("升旗台状态同步中");return View;
        }
    default:
        View.Line=PhaseLine(Public.Phase);
        return View;
    }
}
FString LDHUD::KillLine(const FLDKillNotice& Notice,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName)
{
    const FString Victim=VictimName.IsEmpty()?FString(TEXT("未列名参赛者")):VictimName;
    switch(Notice.Cause)
    {
    case ELDKillCause::Contender: return (KillerName.IsEmpty()?FString(TEXT("未列名参赛者")):KillerName)+TEXT(" 让 ")+Victim+TEXT(" 社死了");
    case ELDKillCause::CampLow: return Victim+TEXT(" 被")+LowCampName+TEXT("送去社死");
    case ELDKillCause::CampHigh: return Victim+TEXT(" 被")+HighCampName+TEXT("送去社死");
    case ELDKillCause::Unowned: return Victim+TEXT(" 社死了（无参赛者击杀）");
    default: return Victim+TEXT(" 社死了（死因未知）");
    }
}
LDHUD::FKillFeedLineView LDHUD::KillFeedLine(const FLDKillNotice& Notice,const FGuid& Self,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName)
{
    FKillFeedLineView Line;
    const bool bVictim=Self.IsValid()&&Notice.Victim==Self;
    const bool bKiller=Self.IsValid()&&Notice.Cause==ELDKillCause::Contender&&Notice.Killer==Self;
    Line.bSelf=bVictim||bKiller;
    Line.Text=KillLine(Notice,bVictim?FString(TEXT("你")):VictimName,bKiller?FString(TEXT("你")):KillerName,LowCampName,HighCampName);
    return Line;
}
FString LDHUD::DeathCause(const FLDKillFeedView& Feed,uint32 Generation,const FGuid& Self,int64 NotBeforeMs,const FLDLobbyView& Lobby,const FString& LowCampName,const FString& HighCampName)
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
        return TEXT("被 ")+(Killer.IsEmpty()?FString(TEXT("未列名参赛者")):Killer)+TEXT(" 送去社死");
    }
    case ELDKillCause::CampLow: return TEXT("被")+LowCampName+TEXT("送去社死");
    case ELDKillCause::CampHigh: return TEXT("被")+HighCampName+TEXT("送去社死");
    case ELDKillCause::Unowned: return TEXT("无参赛者击杀");
    default: return TEXT("死因未知");
    }
}
FString LDHUD::ResultHeadline(const FLDResultView& Result,const FString& WinnerName)
{
    switch(Result.Reason)
    {
    case ELDMatchEndReason::CaptureCompleted:
        return Result.Winner.IsValid()?(WinnerName.IsEmpty()?FString(TEXT("未列名参赛者")):WinnerName)+TEXT(" 连续独占升旗台，加冕阴人王！"):FString(TEXT("对局结束 · 无胜者"));
    case ELDMatchEndReason::HostEnded: return TEXT("房主结束了本局 · 无胜者");
    default: return TEXT("对局结束 · 原因未知");
    }
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
    View.Level=FString::Printf(TEXT("%d"),Row.FinalLevel);
    return View;
}

LDHUD::FActionView LDHUD::Action(const FActionInput& Input,const FActionContext& Context)
{
    FActionView View;
    View.Slot=Input.Slot;View.Key=Context.KeyLabel;
    const bool bLearned=Input.Slot==EActionSlot::Q||Input.Slot==EActionSlot::W;
    View.Kind=bLearned?TEXT("角色主动"):TEXT("装备主动");
    View.Name=!Input.bHasSource?FString(bLearned?TEXT("空槽"):TEXT("无装备"))
        :bLearned&&Input.Rank>0?FString::Printf(TEXT("%s %d级"),*Input.Name,Input.Rank):Input.Name;
    // Only a current own context may send; a predicted execution failure never suppresses the intent itself.
    View.bCanSubmitIntent=Context.bOwnContext&&Input.bProjected&&Context.bPlaying&&Context.bAlive&&!Context.bInputCaptured;
    FString State;
    auto Block=[&View,&State](uint32 Flag,const FString& Text){View.ExecutionBlocks|=Flag;if(State.IsEmpty()){State=Text;}};
    if(!Context.bOwnContext||!Input.bProjected){Block(ActionUnsynced,TEXT("未同步"));}
    if(!Context.bPlaying){Block(ActionNotPlaying,TEXT("对局未进行"));}
    else if(!Context.bAlive){Block(ActionDead,TEXT("社死中"));}
    if(Input.bProjected&&!Input.bHasSource){Block(ActionEmpty,bLearned?TEXT("空槽"):TEXT("无装备"));}
    else if(Input.bProjected&&!Input.bHasActive){Block(ActionNoActive,TEXT("无主动效果"));}
    if(Input.bProjected&&Input.bHasSource&&Input.bHasActive&&Input.ReadyAtMs>0)
    {
        if(!Context.bClock){Block(ActionUnsynced,TEXT("冷却时间同步中"));}
        else if(Input.ReadyAtMs>Context.NowMs)
        {
            View.CooldownRemainingMs=Input.ReadyAtMs-Context.NowMs;
            View.CooldownFraction=Input.CooldownMs>0?float(FMath::Clamp(double(View.CooldownRemainingMs)/double(Input.CooldownMs),0.,1.)):1.f;
            Block(ActionCooldown,TEXT("冷却 ")+TenthsUp(View.CooldownRemainingMs));
        }
    }
    if(Input.bProjected&&Input.bHasActive&&Input.bControlled){Block(ActionControlled,TEXT("受控制无法使用"));}
    if(Input.bProjected&&Input.bHasActive&&Context.bManaKnown&&Input.ManaCostP2>Context.ManaP2){Block(ActionMana,TEXT("胆量不足"));}
    if(Input.bProjected&&!Input.ServerBlock.IsEmpty()){Block(ActionServer,Input.ServerBlock);}
    View.State=State.IsEmpty()?FString(TEXT("就绪")):State;
    View.Countdown=CooldownText(View.CooldownRemainingMs);
    if(Input.bProjected&&Input.bHasSource&&Input.bHasActive&&Input.ManaCostP2>0){View.Cost=FString::Printf(TEXT("胆量 %lld"),HudWholePoints(Input.ManaCostP2,false));}
    View.Tooltip=FString::Printf(TEXT("%s [%s]：%s\n%s"),*View.Kind,*View.Key,*View.Name,*View.State);
    if(Input.bProjected&&Input.bHasActive&&Input.ManaCostP2>0){View.Tooltip+=FString::Printf(TEXT("\n胆量消耗 %lld"),HudWholePoints(Input.ManaCostP2,false));}
    if(!bLearned&&Input.bHasSource){View.Tooltip+=TEXT("\n装备主动没有角色等级；两件同款各自独立冷却");}
    if(Context.bInputCaptured){View.Tooltip+=TEXT("\n当前输入暂不可用：界面占用或按键尚未松开");}
    return View;
}
LDHUD::FActionLookView LDHUD::ActionLook(const FActionView& View,bool bArmed)
{
    FActionLookView Look;
    const uint32 Blocks=View.ExecutionBlocks;
    auto Set=[&Look](EActionLook Kind,const FString& Caption){Look.Look=Kind;Look.Caption=Caption;return Look;};
    if(Blocks&(ActionUnsynced|ActionNotPlaying|ActionDead)){return Set(EActionLook::Blocked,View.State);}
    // An empty slot is already named by its name line; a source without an active says so.
    if(Blocks&ActionEmpty){return Set(EActionLook::Empty,FString());}
    if(Blocks&ActionNoActive){return Set(EActionLook::Empty,View.State);}
    if(bArmed){return Set(EActionLook::Armed,TEXT("选择目标"));}
    if(Blocks&ActionControlled){return Set(EActionLook::Controlled,TEXT("受控"));}
    if(Blocks&ActionCooldown){return Set(EActionLook::Cooling,TEXT("冷却中"));}
    if(Blocks&ActionMana){return Set(EActionLook::NoMana,TEXT("胆量不足"));}
    if(Blocks&ActionServer){return Set(EActionLook::Blocked,View.State);}
    return Set(EActionLook::Ready,TEXT("就绪"));
}

LDHUD::FObservedCard LDHUD::ObservedCard(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec)
{
    FObservedCard Card;
    const bool bContender=Unit.Kind==ELDObservedUnitKind::Contender;
    if(bContender){Card.Title=FString::Printf(TEXT("%s · 等级 %d"),ContenderName.IsEmpty()?TEXT("未列名参赛者"):*ContenderName,Unit.Level);}
    else{Card.Title=CreatureName(Content,Unit.CreatureId);}
    Card.Health=ResourceP2(TEXT("面子"),Unit.Health.CurrentP2,Unit.Health.MaxP2,true);
    Card.bMana=(Unit.AuthorizedFieldMask&LDObservation::Mana)!=0;
    if(Card.bMana){Card.Mana=ResourceP2(TEXT("胆量"),Unit.Mana.CurrentP2,Unit.Mana.MaxP2,false);}
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
    return Card;
}
FString LDHUD::ObservedUnit(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec)
{
    const FObservedCard Card=ObservedCard(Unit,ContenderName,Content,SampledAtServerSec);
    FString Text=Card.Title+TEXT("\n")+Card.Health.Text;
    if(Card.bMana){Text+=TEXT(" · ")+Card.Mana.Text;}
    Text+=TEXT("\n")+Card.Statuses;
    if(Card.bEquipment){Text+=TEXT("\n")+Card.Equipment;}
    return Text;
}
FVector2D LDHUD::MapPoint(const FVector& P,const FBox2D& G)
{const FVector2D Span=G.GetSize();return FVector2D((P.X-G.Min.X)/Span.X,1-(P.Y-G.Min.Y)/Span.Y);}
FVector LDHUD::MapWorld(const FVector2D& P,const FBox2D& G)
{const FVector2D Span=G.GetSize();return FVector(G.Min.X+FMath::Clamp(P.X,0.,.999999)*Span.X,G.Min.Y+(1-FMath::Clamp(P.Y,.000001,1.))*Span.Y,0);}

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
const TCHAR* HudBonusStatName(ELDDemoBonusStat Stat)
{
    switch(Stat)
    {
    case ELDDemoBonusStat::ADFlat: return TEXT("手劲");
    case ELDDemoBonusStat::APFlat: return TEXT("缺德值");
    case ELDDemoBonusStat::MaxHPFlat: return TEXT("最大面子");
    case ELDDemoBonusStat::MaxMPFlat: return TEXT("最大胆量");
    case ELDDemoBonusStat::ArmorFlat: return TEXT("抗揍");
    case ELDDemoBonusStat::MagicResistFlat: return TEXT("脸皮");
    case ELDDemoBonusStat::CooldownHasteFlat: return TEXT("鬼点子");
    case ELDDemoBonusStat::AttackRangeCmFlat: return TEXT("攻击距离");
    case ELDDemoBonusStat::AttackSpeedBonusRatio: return TEXT("手速");
    case ELDDemoBonusStat::MoveSpeedBonusRatio: return TEXT("腿速");
    case ELDDemoBonusStat::CritChanceBonusRatio: return TEXT("正中要害率");
    default: return nullptr;
    }
}
FString HudScaling(const FLDDemoEffectDefinition& Effect)
{
    FString Text=HudPoints(Effect.BaseP2);
    if(Effect.ADRatioBp>0){Text+=TEXT(" + ")+HudPercent(Effect.ADRatioBp)+TEXT("手劲");}
    if(Effect.APRatioBp>0){Text+=TEXT(" + ")+HudPercent(Effect.APRatioBp)+TEXT("缺德值");}
    return Text;
}
}
FString LDHUD::StatLine(const FLDDemoStatBonus& Stat)
{
    const TCHAR* Name=HudBonusStatName(Stat.Stat);
    if(!Name){return FString::Printf(TEXT("未知属性 #%d"),int32(Stat.Stat));}
    switch(Stat.Stat)
    {
    case ELDDemoBonusStat::AttackRangeCmFlat: return FString(Name)+TEXT(" +")+HudRational(Stat.Cm)+TEXT("cm");
    case ELDDemoBonusStat::AttackSpeedBonusRatio: case ELDDemoBonusStat::MoveSpeedBonusRatio: case ELDDemoBonusStat::CritChanceBonusRatio:
        return FString(Name)+TEXT(" +")+HudPercent(Stat.Amount);
    default: return FString(Name)+TEXT(" +")+HudPoints(Stat.Amount);
    }
}
FString LDHUD::EffectLine(const FLDDemoEffectDefinition& Effect)
{
    switch(Effect.GetOp())
    {
    case ELDDemoEffectOp::Damage: return FString(Effect.DamageType==ELDDemoDamageType::Magical?TEXT("社死伤害 "):TEXT("皮肉伤 "))+HudScaling(Effect);
    case ELDDemoEffectOp::Shield: return TEXT("获得淡定 ")+HudScaling(Effect)+TEXT("，持续 ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::DispelRoot: return TEXT("移除自身全部定身");
    case ELDDemoEffectOp::Root: return TEXT("定身 ")+HudSeconds(Effect.DurationMs);
    case ELDDemoEffectOp::StatBonus:
    {
        FString Text;for(const FLDDemoStatBonus& Stat:Effect.Stats){Text+=(Text.IsEmpty()?TEXT(""):TEXT("，"))+StatLine(Stat);}
        return Text.IsEmpty()?FString(TEXT("无属性加成")):Text;
    }
    case ELDDemoEffectOp::OutOfCombatRegen: return TEXT("脱战 ")+HudSeconds(Effect.DelayMs)+TEXT(" 后面子回复 +")+HudRational(Effect.HPPerSec)+TEXT("/s");
    case ELDDemoEffectOp::Lifesteal: return TEXT("弹脑瓜崩造成的面子损失的 ")+HudPercent(Effect.RatioBp)+TEXT(" 回复自身");
    case ELDDemoEffectOp::Dash: return TEXT("朝方向冲刺 ")+HudRational(Effect.DistanceCm)+TEXT("cm（")+HudSeconds(Effect.DurationMs)+TEXT("）");
    case ELDDemoEffectOp::RestoreMana: return TEXT("回复 ")+HudPercent(Effect.RatioBp)+TEXT(" 最大胆量");
    case ELDDemoEffectOp::Cleave: return TEXT("横扫：主目标周围 ")+HudRational(Effect.RadiusCm)+TEXT("cm 内造成 ")+HudPercent(Effect.RatioBp)+TEXT(" 皮肉伤副击");
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
    default: return TEXT("未知目标方式");
    }
}
TArray<FString> LDHUD::RankLines(const FLDDemoRankDefinition& Rank,bool bActive)
{
    TArray<FString> Lines;
    for(const FLDDemoEffectDefinition& Effect:Rank.Effects){Lines.Add(EffectLine(Effect));}
    if(bActive){Lines.Add(TEXT("胆量 ")+HudPoints(Rank.ManaCostP2)+TEXT(" · 冷却 ")+HudSeconds(Rank.CooldownMs));}
    return Lines;
}
LDHUD::FChoiceCardView LDHUD::ChoiceCard(const FLDChoiceOption& Option,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    FChoiceCardView Card;
    const FString Id=Option.DefinitionId.ToString();
    Card.Name=ContentName(Content,Id);
    FString Description;if(ContentText(Content,Id,true,Description)){Card.Description=Description;}
    switch(Option.Kind)
    {
    case ELDChoiceOptionKind::LearnActive: Card.Kind=TEXT("新主动技能");break;
    case ELDChoiceOptionKind::UpgradeActive: Card.Kind=TEXT("主动技能升级");break;
    case ELDChoiceOptionKind::LearnPassive: Card.Kind=TEXT("新被动技能");break;
    case ELDChoiceOptionKind::UpgradePassive: Card.Kind=TEXT("被动技能升级");break;
    case ELDChoiceOptionKind::Attribute: Card.Kind=TEXT("属性提升");break;
    default: Card.Kind=TEXT("未知选项");break;
    }
    if(Option.Kind==ELDChoiceOptionKind::Attribute)
    {
        const FLDAttributeCardView* Owned=Progression.AttributeCards.FindByPredicate([&Option](const FLDAttributeCardView& C){return C.DefinitionId==Option.DefinitionId;});
        Card.RankText=FString::Printf(TEXT("已选 %d 次"),Owned?int32(Owned->Count):0);
        if(const FLDDemoAttributeDefinition* Definition=Content?Content->Attributes.FindByPredicate([&Id](const FLDDemoAttributeDefinition& D){return D.Id==Id;}):nullptr)
        {for(const FLDDemoStatBonus& Stat:Definition->Stats){Card.Lines.Add(StatLine(Stat));}}
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
    return Card;
}
LDHUD::FShopItemView LDHUD::ShopItem(const FLDShopItemView& Item,int64 Gold,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* Content)
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
        if(Definition->HasActive)
        {
            View.Lines.Add(TEXT("主动 [1/2]：")+TargetLine(Definition->Active.Target));
            for(const FString& Line:RankLines(Definition->Active.Rank,true)){View.Lines.Add(TEXT("  ")+Line);}
        }
        else{View.Lines.Add(TEXT("无主动效果"));}
    }
    else if(Content){View.Lines.Add(TEXT("内容目录中找不到该装备"));}
    if(Item.bInheritsCooldown&&Item.InheritedReadyAtMs>0)
    {View.Lines.Add(bClock&&Item.InheritedReadyAtMs>NowMs?TEXT("购买后继承未结束冷却 ")+TenthsUp(Item.InheritedReadyAtMs-NowMs):FString(TEXT("购买后继承此前的冷却记录")));}
    return View;
}
LDHUD::FAttributeRowView LDHUD::AttributeRow(const FLDCombatAttributeRowView& Row)
{
    static const TCHAR* Names[]={TEXT("最大面子"),TEXT("最大胆量"),TEXT("手劲"),TEXT("缺德值"),TEXT("抗揍"),TEXT("脸皮"),TEXT("手速"),TEXT("腿速"),
        TEXT("攻击距离"),TEXT("面子回复"),TEXT("胆量回复"),TEXT("正中要害率"),TEXT("鬼点子")};
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
    case ELDGrowthReason::Dead: return TEXT("社死中不能交易");
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
    case ELDGrowthReason::NoShopSession: return TEXT("小卖部尚未打开");
    case ELDGrowthReason::QuoteStale: return TEXT("报价已更新，请重新确认");
    case ELDGrowthReason::PriceChanged: return TEXT("价格已变化，请重新确认");
    case ELDGrowthReason::UnknownItem: return TEXT("未知商品");
    case ELDGrowthReason::ItemNotOwned: return TEXT("你没有这件装备");
    case ELDGrowthReason::ItemStale: return TEXT("装备已变化，请重新确认");
    case ELDGrowthReason::EquipmentFull: return TEXT("装备槽已满，请先卖出一件");
    case ELDGrowthReason::InsufficientGold: return TEXT("零花钱不足");
    case ELDGrowthReason::InventoryStale: return TEXT("装备已变化，请重新确认");
    case ELDGrowthReason::AccountStale: return TEXT("零花钱已变化，请重新确认");
    case ELDGrowthReason::UnknownShop: return TEXT("未知的小卖部");
    case ELDGrowthReason::StaleWorld: return TEXT("世界版本不一致");
    case ELDGrowthReason::OutsideRange: case ELDGrowthReason::InvalidApproachRegion: return TEXT("不在小卖部交互范围内");
    case ELDGrowthReason::NotGrounded: return TEXT("需要站在地面上");
    case ELDGrowthReason::InvalidGround: case ELDGrowthReason::WrongSurface: return TEXT("当前位置不能交易");
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
    case ELDCombatReject::StaleSource: return TEXT("技能或装备已变化");
    case ELDCombatReject::SourceUnavailable: return TEXT("没有可用来源");
    case ELDCombatReject::NoActiveEffect: return TEXT("无主动效果");
    case ELDCombatReject::Busy: return TEXT("正在执行其他动作");
    case ELDCombatReject::Cooldown: return TEXT("冷却中");
    case ELDCombatReject::AttackNotReady: return TEXT("弹脑瓜崩尚未就绪");
    case ELDCombatReject::InsufficientMana: return TEXT("胆量不足");
    case ELDCombatReject::ControlBlocked: return TEXT("受控制无法使用");
    case ELDCombatReject::TargetUnavailable: return TEXT("目标不可用");
    case ELDCombatReject::InvalidDirection: return TEXT("方向无效");
    case ELDCombatReject::OutOfRange: return TEXT("超出射程");
    case ELDCombatReject::Blocked: return TEXT("被地形阻挡");
    case ELDCombatReject::Dead: return TEXT("社死中");
    case ELDCombatReject::MatchClosed: return TEXT("对局已结束");
    case ELDCombatReject::AuthorityDataUnavailable: return TEXT("服务器数据不可用");
    case ELDCombatReject::CapacityUnavailable: return TEXT("服务器容量不足");
    default: return FString::Printf(TEXT("未知原因 #%d"),int32(Reject));
    }
}
LDHUD::FActionInput LDHUD::ActionInput(const FLDCombatEntryView& Entry,EActionSlot Slot,const FLDDemoContentDefinitions* Content)
{
    FActionInput Input;
    Input.Slot=Slot;Input.bProjected=true;
    Input.bHasSource=!Entry.DefinitionId.IsNone();
    if(Input.bHasSource){Input.Name=ContentName(Content,Entry.DefinitionId.ToString());}
    Input.Rank=Entry.Rank;Input.bHasActive=Entry.bHasActive;Input.ManaCostP2=Entry.ManaCostP2;
    Input.ReadyAtMs=Entry.ReadyAtMs;Input.CooldownMs=Entry.CooldownMs;
    Input.ServerBlock=CombatRejectText(Entry.Unavailable);
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
    case ELDCombatEntry::Basic: return TEXT("弹脑瓜崩");
    case ELDCombatEntry::Q: return TEXT("角色技能 1");
    case ELDCombatEntry::W: return TEXT("角色技能 2");
    case ELDCombatEntry::Item1: return TEXT("装备 1");
    case ELDCombatEntry::Item2: return TEXT("装备 2");
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
    if(Receipt.bProtectionRemoved){Parts.Add(TEXT("复活保护已解除"));}
    return FString::Join(Parts,TEXT(" · "));
}
FString LDHUD::CombatReceiptSlotNotice(const FLDCombatActionReceipt& Receipt)
{
    const bool bSlot=Receipt.Entry==ELDCombatEntry::Q||Receipt.Entry==ELDCombatEntry::W||Receipt.Entry==ELDCombatEntry::Item1||Receipt.Entry==ELDCombatEntry::Item2;
    if(!bSlot||Receipt.Phase!=ELDCombatPhase::Rejected||Receipt.Origin!=ELDCombatRequestOrigin::Direct
        ||Receipt.Reject==ELDCombatReject::None||Receipt.Reject==ELDCombatReject::Cancelled){return FString();}
    return CombatRejectText(Receipt.Reject);
}
