#include "UI/LDHUDModel.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentIds.h"
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
    static const FLinearColor Palette[]={{.15f,.38f,.4f},{.35f,.23f,.15f},{.27f,.3f,.45f},{.35f,.37f,.21f}};
    return Palette[Presentation%4];
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
FString LDHUD::LifeStatus(const FLDOwnerState& Own,bool bClock,int64 NowMs)
{
    switch(Own.LifePhase)
    {
    case ELDLifePhase::Alive:
        if(Own.ProtectedUntilMs<=0){return TEXT("存活");}
        if(!bClock){return TEXT("复活保护 · 时间同步中");}
        return Own.ProtectedUntilMs>NowMs?TEXT("复活保护 ")+TenthsUp(Own.ProtectedUntilMs-NowMs)+TEXT(" · 发起进攻即解除"):FString(TEXT("存活"));
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
    View.Note=TEXT("等级、经验、技能、装备和零花钱全部保留；复活后随机回到出生点");
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
    View.Tooltip=FString::Printf(TEXT("%s [%s]：%s\n%s"),*View.Kind,*View.Key,*View.Name,*View.State);
    if(Input.bProjected&&Input.bHasActive&&Input.ManaCostP2>0){View.Tooltip+=FString::Printf(TEXT("\n胆量消耗 %lld"),HudWholePoints(Input.ManaCostP2,false));}
    if(!bLearned&&Input.bHasSource){View.Tooltip+=TEXT("\n装备主动没有角色等级；两件同款各自独立冷却");}
    if(Context.bInputCaptured){View.Tooltip+=TEXT("\n当前输入被界面占用");}
    return View;
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
