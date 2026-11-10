#pragma once

#include "CoreMinimal.h"

// 运行时显示术语表（#111、#148）：界面里的系统术语与世界观短句只在这里定义一次，界面与测试都引用它。
// 名字保留雾港梗；属性、资源、伤害类型、状态、普攻与冲刺用通用游戏术语（#148）。技能、野怪与状态的条目名仍由 SourceData\Demo\content.json 的 display 行提供；
// 下面“状态名”一节是系统文案需要直接引用的少数状态名，必须与目录显示名一致（Lockdown.Rules.HUDTermsGlossary 核对）。
// 通用系统文案（网络、设置、同步中等）不放在这里。
namespace LDTerms
{
// 门面
inline constexpr TCHAR Title[]=TEXT("谁是雾港最强淫人王");
inline constexpr TCHAR Tagline[]=TEXT("脆皮大学生无差别大乱斗 · 旗杆上插谁的名，谁就是淫人王");
inline constexpr TCHAR SlangNote[]=TEXT("雾港黑话：淫 = 把人揍趴下，跟色情没关系");

// 资源与属性
inline constexpr TCHAR Health[]=TEXT("生命");
inline constexpr TCHAR Mana[]=TEXT("法力");
inline constexpr TCHAR AttackDamage[]=TEXT("攻击力");
inline constexpr TCHAR AbilityPower[]=TEXT("法术强度");
inline constexpr TCHAR Armor[]=TEXT("护甲");
inline constexpr TCHAR MagicResist[]=TEXT("魔抗");
inline constexpr TCHAR AttackSpeed[]=TEXT("攻速");
inline constexpr TCHAR MoveSpeed[]=TEXT("移速");
inline constexpr TCHAR Crit[]=TEXT("暴击");
inline constexpr TCHAR Haste[]=TEXT("技能急速");
inline constexpr TCHAR PhysicalDamage[]=TEXT("物理伤害");
inline constexpr TCHAR MagicalDamage[]=TEXT("魔法伤害");
inline constexpr TCHAR Experience[]=TEXT("流量");
inline FString MaxHealth(){return FString(Health)+TEXT("上限");}
inline FString MaxMana(){return FString(Mana)+TEXT("上限");}
inline FString HealthRegen(){return FString(Health)+TEXT("回复");}
inline FString ManaRegen(){return FString(Mana)+TEXT("回复");}
inline FString CritChance(){return FString(Crit)+TEXT("率");}
inline FString NotEnoughMana(){return FString(Mana)+TEXT("不足");}

// 动作与地点
inline constexpr TCHAR BasicAttack[]=TEXT("普攻");
inline constexpr TCHAR Dash[]=TEXT("冲刺");
inline constexpr TCHAR CapturePoint[]=TEXT("旗杆");
inline constexpr TCHAR CampLow[]=TEXT("小野怪");
inline constexpr TCHAR CampHigh[]=TEXT("大野怪");
inline constexpr TCHAR CampElite[]=TEXT("精英野怪");

// 地图事件（#79）
inline constexpr TCHAR Dorm[]=TEXT("躺平角");
inline FString DormRegenActive(){return FString(Dorm)+TEXT("回血中");}
inline FString InDorm(){return FString(TEXT("在"))+Dorm+TEXT(" · 脱战后快速回复");}
inline constexpr TCHAR Airdrop[]=TEXT("天降外卖");
inline FString AirdropCountdown(const FString& Clock){return FString(Airdrop)+TEXT(" ")+Clock+TEXT(" 后落地 · 小地图橙色标记");}
inline FString AirdropSoon(){return FString(Airdrop)+TEXT("即将落地 · 小地图橙色标记");}
inline constexpr TCHAR AirdropLanded[]=TEXT("外卖到了 · 先到先得");
inline FString AirdropOwnClaim(){return FString(TEXT("你抢到了"))+Airdrop+TEXT("！");}
inline FString AirdropTaken(){return FString(Airdrop)+TEXT("被人抢走了");}
inline constexpr TCHAR Boss[]=TEXT("画饼老板");
inline constexpr TCHAR BossBuff[]=TEXT("吃到大饼");
inline FString BossCountdown(const FString& Name,const FString& Clock){return Name+TEXT(" ")+Clock+TEXT(" 后出现 · 小地图金色巡逻点");}
inline FString BossSoon(const FString& Name){return Name+TEXT(" 即将出现");}
inline FString BossAppeared(const FString& Name){return Name+TEXT(" 出现了 · 小地图金色巡逻点");}
inline FString BossPatrol(const FString& Name){return Name+TEXT(" 巡逻中 · 打它会被它追着打");}
inline FString BossDownSelf(const FString& Name){return FString(TEXT("你击败了"))+Name+TEXT("，")+BossBuff+TEXT("！");}
inline FString BossDownBy(const FString& Killer,const FString& Name){return Killer+TEXT(" 击败了")+Name+TEXT("，")+BossBuff;}
inline FString BossDown(const FString& Name){return Name+TEXT(" 倒下了");}
inline constexpr TCHAR Leader[]=TEXT("上热搜");
inline FString LeaderNamed(const FString& Name){return FString(Leader)+TEXT("：")+Name;}
inline FString LeaderSelf(){return FString(TEXT("你"))+Leader+TEXT("了！位置已在小地图公开");}

// 状态名：与目录 display 行同名
inline constexpr TCHAR Shield[]=TEXT("护盾");
inline constexpr TCHAR Root[]=TEXT("定身");
inline constexpr TCHAR Slow[]=TEXT("减速");
inline constexpr TCHAR Taunt[]=TEXT("嘲讽");
inline constexpr TCHAR Disarm[]=TEXT("缴械");
inline constexpr TCHAR Stun[]=TEXT("眩晕");
inline constexpr TCHAR Silence[]=TEXT("沉默");
inline constexpr TCHAR Breach[]=TEXT("破防");
inline constexpr TCHAR Burn[]=TEXT("灼烧");
inline constexpr TCHAR RespawnProtection[]=TEXT("复活保护");
inline FString ProtectionRemoved(){return FString(RespawnProtection)+TEXT("没了");}

// 生死与成长
inline constexpr TCHAR Dead[]=TEXT("寄了");
inline constexpr TCHAR DeathTitle[]=TEXT("你寄了");
inline constexpr TCHAR RespawnWait[]=TEXT("回出租屋 emo");
inline FString DeathNote(){return TEXT("等级和构筑都还在，别慌");}
inline constexpr TCHAR ProtectionRule[]=TEXT("一出手就没了，失手也没了");
inline constexpr TCHAR OutOfCombatRegen[]=TEXT("脱战回复中");
// 成长面板的入口按钮（#198：主动三选一、待学图纸与五维加点，APP_SPEC §4.2）。
inline constexpr TCHAR Choice[]=TEXT("开盲盒");
inline FString LevelUp(int32 Level){return FString::Printf(TEXT("涨粉了！Lv%d"),Level);}
inline constexpr TCHAR Blueprint[]=TEXT("图纸");
inline constexpr TCHAR StatPoints[]=TEXT("属性点");
inline FString OfferChance(int32 Count){return Count>1?FString::Printf(TEXT("%d 次选技能机会"),Count):FString(TEXT("一次选技能机会"));}
// 升级横幅的第二行：这次升级得到的属性点与主动机会。
inline FString LevelUpGains(int64 Points,int32 Offers)
{
    TArray<FString> Parts;
    if(Points>0){Parts.Add(FString::Printf(TEXT("%lld %s"),Points,StatPoints));}
    if(Offers>0){Parts.Add(OfferChance(Offers));}
    return Parts.IsEmpty()?FString():TEXT("获得 ")+FString::Join(Parts,TEXT(" · "));
}
inline FString OffersQueued(int32 Count){return FString::Printf(TEXT("还有 %d 次选技能"),Count);}
inline constexpr TCHAR OfferWaitingEligible[]=TEXT("暂时没有能抽的新技能，机会先留着");
inline constexpr TCHAR LearnThis[]=TEXT("学这个");
inline FString LearnInto(const FString& Key){return FString(TEXT("学到 "))+Key;}
inline FString LearnedInto(const FString& Name,const FString& Key){return FString::Printf(TEXT("已学到 %s：%s"),*Key,*Name);}
inline FString StoredAsBlueprint(const FString& Name,const FString& Why)
{return Why.IsEmpty()?FString::Printf(TEXT("已存为%s：%s"),Blueprint,*Name):FString::Printf(TEXT("已存为%s：%s（%s）"),Blueprint,*Name,*Why);}
// 不能学习或加点的原因（构筑安全窗见 APP_SPEC §4.2）。
inline constexpr TCHAR NotSafe[]=TEXT("未脱战");
inline FString NotSafeFor(const FString& Seconds){return FString::Printf(TEXT("未脱战 %s 秒"),*Seconds);}
inline constexpr TCHAR DeadNoLearn[]=TEXT("阵亡中");
inline constexpr TCHAR SlotsFull[]=TEXT("Q/W/E 已满");
inline constexpr TCHAR ReplaceAtRest[]=TEXT("休息时才能替换");
inline FString UnspentPoints(int64 Count){return FString::Printf(TEXT("未分配点数 %lld"),Count);}
inline constexpr TCHAR ConfirmPoints[]=TEXT("确认加点");
inline FString PointsAdded(int64 Spent,int64 Left){return FString::Printf(TEXT("已加 %lld 点，还剩 %lld 点"),Spent,Left);}

// 休息服务（#198，APP_SPEC §4.4/§5）：只在休息的配装窗口（配装与派奖）可用，服务器裁决每一条命令。
inline constexpr TCHAR RestServices[]=TEXT("休息服务");
inline FString NpcOffer(int64 Points){return FString::Printf(TEXT("NPC：花 %lld 点抽 3 个主动"),Points);}
inline constexpr TCHAR Respec[]=TEXT("洗点");
inline FString ReplaceInto(const FString& Key){return FString(TEXT("替换 "))+Key;}
inline FString ForgetSlot(const FString& Key,const FString& Name){return FString::Printf(TEXT("遗忘 %s：%s"),*Key,*Name);}
inline FString ReplaceConfirm(const FString& NewName,const FString& Key,const FString& OldName)
{return FString::Printf(TEXT("用 %s 替换 %s：%s？%s 会被遗忘，不退还"),*NewName,*Key,*OldName,*OldName);}
inline FString ForgetConfirm(const FString& Key,const FString& Name)
{return FString::Printf(TEXT("遗忘 %s：%s？不退还，之后还能再抽到"),*Key,*Name);}
inline constexpr TCHAR ConfirmAction[]=TEXT("确认");
inline constexpr TCHAR CancelAction[]=TEXT("取消");
inline FString NpcBought(int64 Points){return FString::Printf(TEXT("已花 %lld 点，NPC 端来 3 个主动，选一个"),Points);}
inline FString Respecced(int64 Refunded,int64 Left){return FString::Printf(TEXT("已洗点：返还 %lld 点，现有 %lld 点未分配"),Refunded,Left);}
inline FString Replaced(const FString& NewName,const FString& Key,const FString& OldName)
{return FString::Printf(TEXT("已在 %s 用 %s 替换 %s"),*Key,*NewName,*OldName);}
inline FString Forgotten(const FString& Name){return FString::Printf(TEXT("已遗忘：%s（之后还能再抽到）"),*Name);}

// 阶段与旗杆（喷漆守护终局）
// 对局流程（#198）：开局休息 R0，之后三段野外各接一次休息 R1–R3，最后一段野外开放旗杆。
// 休息的返回倒计时在屏幕中央整秒倒数，每段野外开始时显示“开淫！”。
inline constexpr TCHAR CountdownGo[]=TEXT("开淫！");
inline constexpr TCHAR NotStartedYet[]=TEXT("还没开始呢，急什么");
inline constexpr TCHAR RestPhase[]=TEXT("课间休息，抓紧配装");
inline FString RestLabel(int32 Index){return FString::Printf(TEXT("休息 R%d"),Index);}
// 休息的各步（配装的最后一段兼作 PK 报名；派奖时可以再调整配装）。
inline constexpr TCHAR RestBuild[]=TEXT("配装与训练");
inline constexpr TCHAR RestDraw[]=TEXT("PK 抽签");
inline constexpr TCHAR RestBetting[]=TEXT("押注");
inline constexpr TCHAR RestPKReady[]=TEXT("PK 准备");
inline constexpr TCHAR RestPK[]=TEXT("PK 中");
inline constexpr TCHAR RestSettlement[]=TEXT("PK 结算");
inline constexpr TCHAR RestPayout[]=TEXT("派奖 · 可再配装");
inline constexpr TCHAR RestReturn[]=TEXT("返回野外");
inline constexpr TCHAR BuildLockedNow[]=TEXT("配装已锁定");
inline FString SignupOpensIn(int64 Seconds){return FString::Printf(TEXT("PK 报名 %lld 秒后开放"),Seconds);}
inline FString SignupOpenFor(int64 Seconds){return FString::Printf(TEXT("PK 报名中 · 剩 %lld 秒"),Seconds);}
inline FString SignedUpCount(int32 Count){return FString::Printf(TEXT("已报名 %d 人"),Count);}
inline constexpr TCHAR SignedUpSelf[]=TEXT("你已报名");
inline constexpr TCHAR NotSignedUpSelf[]=TEXT("你没报名");
inline FString PKEntrants(int32 Count){return FString::Printf(TEXT("PK %d 人参赛"),Count);}
inline constexpr TCHAR PKSkipped[]=TEXT("PK 跳过（报名不足 2 人）");
inline constexpr TCHAR SignupJoin[]=TEXT("报名 PK");
inline constexpr TCHAR SignupWithdraw[]=TEXT("取消报名");
inline constexpr TCHAR NoPKProvider[]=TEXT("PK 尚未开放，本次休息跳过 PK");
// 休息中的个人训练：只有本人看得见的训练假人，可随时重置（本人与假人回满、冷却就绪）。
inline constexpr TCHAR TrainingReset[]=TEXT("重置训练");
inline constexpr TCHAR TrainingDummy[]=TEXT("训练假人");
inline FString FieldSegmentLine(int32 Segment,int32 Segments,const FString& Clock)
{return FString::Printf(TEXT("野外 %d/%d · 下次休息 %s"),Segment,Segments,*Clock);}
inline constexpr TCHAR FinalField[]=TEXT("最终野外 · 旗杆开放");
inline constexpr TCHAR CaptureAfterLastRest[]=TEXT("旗杆在最后一次休息（R3）后开放");
inline constexpr TCHAR Development[]=TEXT("猥琐发育中，别浪");
inline constexpr TCHAR CaptureOpened[]=TEXT("旗杆开放了，冲！");
inline constexpr TCHAR CaptureRule[]=TEXT("一个人在旗杆下喷上涂鸦，再守住它就赢");
inline constexpr TCHAR CircleEmpty[]=TEXT("旗杆空着，没人敢上？");
inline constexpr TCHAR CircleContested[]=TEXT("旗杆底下打成一锅粥");
inline constexpr TCHAR Holding[]=TEXT("正在旗杆上喷漆");
inline constexpr TCHAR HoldLeft[]=TEXT("剩");
inline FString ProgressKept(const FString& Holder){return Holder+TEXT(" 的喷漆进度先存着");}
inline constexpr TCHAR NoProgressKept[]=TEXT("谁也没存下进度");
inline constexpr TCHAR CaptureGo[]=TEXT("去中心喷漆");
inline FString GraffitiOf(const FString& Owner){return FString(TEXT("旗杆上是 "))+Owner+TEXT(" 的涂鸦");}
inline FString GraffitiSprayed(const FString& Owner){return Owner+TEXT(" 在旗杆上喷上了涂鸦");}
inline constexpr TCHAR GuardLeft[]=TEXT("再守");
inline constexpr TCHAR GuardPaused[]=TEXT("有人进圈，守护冻结");
inline constexpr TCHAR GuardAway[]=TEXT("守擂者不在圈内，守护暂停");
inline constexpr TCHAR GuardReset[]=TEXT("守擂者离圈，守护清零");
inline constexpr TCHAR GuardOwn[]=TEXT("守住：站在旗杆圈里守护，离圈会暂停（寄了涂鸦就没了）");
inline FString Besiege(const FString& Owner){return FString(TEXT("下一步：去围攻 "))+Owner+TEXT("，或者去旗杆改漆");}

// 击杀与结算
inline constexpr TCHAR Winner[]=TEXT("淫人王");
inline constexpr TCHAR UnknownVictim[]=TEXT("对面");
inline FString KillLine(const FString& Killer,const FString& Victim){return Killer+TEXT(" 把 ")+Victim+TEXT(" 淫了");}
inline constexpr TCHAR Assist[]=TEXT("助攻");
inline FString AssistSuffix(const TArray<FString>& Assisters){return Assisters.IsEmpty()?FString():FString(TEXT("（"))+Assist+TEXT("：")+FString::Join(Assisters,TEXT("、"))+TEXT("）");}
inline FString KilledByLine(const FString& Victim,const FString& By){return Victim+TEXT(" 被 ")+By+TEXT(" 淫了");}
inline FString KilledBy(const FString& By){return FString(TEXT("被 "))+By+TEXT(" 淫了");}
inline FString KillFloat(const FString& Victim){return FString(TEXT("淫了 "))+Victim+TEXT("！");}
inline FString Crowned(const FString& WinnerName){return WinnerName+TEXT(" 的涂鸦守住了，恭喜成为今晚的")+Winner+TEXT("！");}
inline constexpr TCHAR NoWinner[]=TEXT("没人守住涂鸦 · 全员摆烂");
inline constexpr TCHAR NoCapture[]=TEXT("没人守住涂鸦");
inline constexpr TCHAR Scoreboard[]=TEXT("排行榜");
inline constexpr TCHAR ResultKills[]=TEXT("淫了几个");
inline constexpr TCHAR ResultDeaths[]=TEXT("寄了几次");
inline constexpr TCHAR ResultAssists[]=TEXT("助攻");

// 卡面关键词（#147，#198 删去横扫，见内容与数值 §4.5）：全局 12 个，卡面加粗，悬停卡片时在详情里逐个解释。卡面与关键词解释都不写喷漆或守护。
struct FCardKeyword { const TCHAR* Name; const TCHAR* Explain; };
inline constexpr FCardKeyword CardKeywords[]={
    {TEXT("减速"),TEXT("移速降低一定比例；同时有多个减速时只取最强的。减速不是控制")},
    {TEXT("定身"),TEXT("不能移动和冲刺，可以普攻和施法")},
    {TEXT("眩晕"),TEXT("什么都不能做")},
    {TEXT("沉默"),TEXT("不能施放技能，可以移动、普攻和冲刺")},
    {TEXT("缴械"),TEXT("不能普攻，可以移动和施法")},
    {TEXT("嘲讽"),TEXT("被迫攻击嘲讽者，不能施法")},
    {TEXT("击退"),TEXT("被推开一段距离，期间不能行动")},
    {TEXT("控制"),TEXT("处于定身、眩晕、沉默、缴械、嘲讽、击退中的任意一种。不含减速")},
    {TEXT("破防"),TEXT("每层护甲和魔抗 -3，最多 5 层；新的一层刷新整组时间")},
    {TEXT("灼烧"),TEXT("每层每秒造成魔法伤害，最多 5 层；新的一层刷新整组时间")},
    {TEXT("护盾"),TEXT("先于生命承受伤害，到期消失；身上只有一份，新的不弱于剩余量才替换")},
    {TEXT("吸血"),TEXT("按普攻实际造成的生命损失回复生命；多个吸血相加，最多 35%")},
};
}
