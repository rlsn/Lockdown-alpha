#pragma once

#include "CoreMinimal.h"

// 雾港世界观术语表（#111）：运行时界面里的世界观术语与带梗短句只在这里定义一次，界面与测试都引用它；
// 换梗只改这一处。技能、装备、野怪与状态的条目名仍由 SourceData\Demo\content.json 的 display 行提供；
// 下面“状态名”一节是系统文案需要直接引用的少数状态名，必须与目录显示名一致（Lockdown.Rules.HUDTermsGlossary 核对）。
// 通用系统文案（网络、设置、同步中等）不放在这里。
namespace LDTerms
{
// 门面
inline constexpr TCHAR Title[]=TEXT("谁是雾港最强淫人王");
inline constexpr TCHAR Tagline[]=TEXT("脆皮大学生无差别大乱斗 · 旗杆上插谁的名，谁就是淫人王");
inline constexpr TCHAR SlangNote[]=TEXT("雾港黑话：淫 = 把人揍趴下，跟色情没关系");

// 资源与属性
inline constexpr TCHAR Health[]=TEXT("精神状态");
inline constexpr TCHAR Mana[]=TEXT("电量");
inline constexpr TCHAR AttackDamage[]=TEXT("力大砖飞");
inline constexpr TCHAR AbilityPower[]=TEXT("阴阳怪气");
inline constexpr TCHAR Armor[]=TEXT("嘴硬");
inline constexpr TCHAR MagicResist[]=TEXT("心大");
inline constexpr TCHAR AttackSpeed[]=TEXT("手速");
inline constexpr TCHAR MoveSpeed[]=TEXT("润速");
inline constexpr TCHAR Crit[]=TEXT("暴击");
inline constexpr TCHAR Haste[]=TEXT("整活频率");
inline constexpr TCHAR PhysicalDamage[]=TEXT("物理超度");
inline constexpr TCHAR MagicalDamage[]=TEXT("精神攻击");
inline constexpr TCHAR Gold[]=TEXT("米");
inline constexpr TCHAR Experience[]=TEXT("流量");
inline FString MaxHealth(){return FString(Health)+TEXT("上限");}
inline FString MaxMana(){return FString(Mana)+TEXT("上限");}
inline FString HealthRegen(){return FString(Health)+TEXT("回复");}
inline FString ManaRegen(){return FString(Mana)+TEXT("回复");}
inline FString CritChance(){return FString(Crit)+TEXT("率");}
inline FString NotEnoughMana(){return FString(Mana)+TEXT("不足");}
inline FString NotEnoughGold(){return FString(Gold)+TEXT("不够");}

// 动作与地点
inline constexpr TCHAR BasicAttack[]=TEXT("大比兜");
inline constexpr TCHAR BasicAttackVerb[]=TEXT("抽大比兜");
inline constexpr TCHAR Dash[]=TEXT("润");
inline constexpr TCHAR Shop[]=TEXT("砍一刀小摊");
inline constexpr TCHAR CapturePoint[]=TEXT("旗杆");
inline constexpr TCHAR CampLow[]=TEXT("小野怪");
inline constexpr TCHAR CampHigh[]=TEXT("大野怪");

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
inline FString BossPatrol(const FString& Name){return Name+TEXT(" 巡逻中 · 打它会被它追着打");}
inline FString BossDownSelf(const FString& Name){return FString(TEXT("你击败了"))+Name+TEXT("，")+BossBuff+TEXT("！");}
inline FString BossDownBy(const FString& Killer,const FString& Name){return Killer+TEXT(" 击败了")+Name+TEXT("，")+BossBuff;}
inline FString BossDown(const FString& Name){return Name+TEXT(" 倒下了");}
inline constexpr TCHAR Leader[]=TEXT("上热搜");
inline FString LeaderBounty(int64 Amount){return FString::Printf(TEXT("赏金 +%lld %s"),Amount,Gold);}
inline FString LeaderNamed(const FString& Name,int64 Amount){return FString(Leader)+TEXT("：")+Name+TEXT(" · ")+LeaderBounty(Amount);}
inline FString LeaderSelf(int64 Amount){return FString(TEXT("你"))+Leader+TEXT("了！位置已在小地图公开 · ")+LeaderBounty(Amount);}

// 状态名：与目录 display 行同名
inline constexpr TCHAR Shield[]=TEXT("心理防线");
inline constexpr TCHAR Root[]=TEXT("提裤子");
inline constexpr TCHAR Slow[]=TEXT("腿软");
inline constexpr TCHAR Taunt[]=TEXT("瞅你咋地");
inline constexpr TCHAR Stealth[]=TEXT("查无此人");
inline constexpr TCHAR Disarm[]=TEXT("捂裆");
inline constexpr TCHAR Stun[]=TEXT("CPU 干烧了");
inline constexpr TCHAR RespawnProtection[]=TEXT("免死金牌");
inline FString ProtectionRemoved(){return FString(RespawnProtection)+TEXT("没了");}

// 生死与成长
inline constexpr TCHAR Dead[]=TEXT("寄了");
inline constexpr TCHAR DeathTitle[]=TEXT("你寄了");
inline constexpr TCHAR RespawnWait[]=TEXT("回出租屋 emo");
inline FString DeathNote(){return FString(TEXT("等级、构筑和"))+Gold+TEXT("都还在，别慌");}
inline constexpr TCHAR ProtectionRule[]=TEXT("一出手就没了，失手也没了");
inline constexpr TCHAR OutOfCombatRegen[]=TEXT("精神状态回暖中");
inline constexpr TCHAR Choice[]=TEXT("开盲盒");
inline FString LevelUp(int32 Level){return FString::Printf(TEXT("涨粉了！Lv%d"),Level);}
inline FString ChoicesEarned(int64 Count){return Count>1?FString::Printf(TEXT("获得 %lld 次%s机会"),Count,Choice):FString::Printf(TEXT("获得一次%s机会"),Choice);}

// 阶段与旗杆
inline constexpr TCHAR Development[]=TEXT("猥琐发育中，别浪");
inline constexpr TCHAR CaptureOpened[]=TEXT("旗杆开放了，冲！");
inline constexpr TCHAR CaptureRule[]=TEXT("一个人守住旗杆就能插旗");
inline constexpr TCHAR CircleEmpty[]=TEXT("旗杆空着，没人敢上？");
inline constexpr TCHAR CircleContested[]=TEXT("旗杆底下打成一锅粥");
inline constexpr TCHAR Holding[]=TEXT("正在旗杆底下装");
inline constexpr TCHAR HoldLeft[]=TEXT("剩");
inline FString ProgressKept(const FString& Holder){return Holder+TEXT(" 的进度先存着");}
inline constexpr TCHAR NoProgressKept[]=TEXT("谁也没存下进度");
inline constexpr TCHAR CaptureGo[]=TEXT("去中心插旗");

// 击杀与结算
inline constexpr TCHAR Winner[]=TEXT("淫人王");
inline constexpr TCHAR UnknownVictim[]=TEXT("对面");
inline FString KillLine(const FString& Killer,const FString& Victim){return Killer+TEXT(" 把 ")+Victim+TEXT(" 淫了");}
inline FString KilledByLine(const FString& Victim,const FString& By){return Victim+TEXT(" 被 ")+By+TEXT(" 淫了");}
inline FString KilledBy(const FString& By){return FString(TEXT("被 "))+By+TEXT(" 淫了");}
inline FString KillFloat(const FString& Victim){return FString(TEXT("淫了 "))+Victim+TEXT("！");}
inline FString Crowned(const FString& WinnerName){return WinnerName+TEXT(" 插旗成功，恭喜成为今晚的")+Winner+TEXT("！");}
inline constexpr TCHAR NoWinner[]=TEXT("无人插旗 · 全员摆烂");
inline constexpr TCHAR NoCapture[]=TEXT("无人插旗");
inline constexpr TCHAR ResultKills[]=TEXT("淫了几个");
inline constexpr TCHAR ResultDeaths[]=TEXT("寄了几次");
}
