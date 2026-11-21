#pragma once

#include "CoreMinimal.h"
#include "Framework/LDPacingTypes.h"

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
inline constexpr TCHAR Momentum[]=TEXT("劲");
inline constexpr TCHAR Overdrive[]=TEXT("爆劲");
inline constexpr TCHAR Combo[]=TEXT("连击");
inline constexpr TCHAR SkillPoint[]=TEXT("技能点");
inline constexpr TCHAR PerfectDodge[]=TEXT("完美闪避");
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
inline FString HealthRegen(){return FString(Health)+TEXT("回复");}
inline FString MomentumRegen(){return FString(TEXT("交战"))+Momentum+TEXT("回复");}
inline FString CritChance(){return FString(Crit)+TEXT("率");}
inline FString MomentumShort(){return FString(Momentum)+TEXT("不足");}

// 动作与地点
inline constexpr TCHAR BasicAttack[]=TEXT("普攻");
inline constexpr TCHAR Dash[]=TEXT("冲刺");
inline constexpr TCHAR CapturePoint[]=TEXT("旗杆");
inline constexpr TCHAR CampLow[]=TEXT("小野");
inline constexpr TCHAR CampHigh[]=TEXT("中野");
inline constexpr TCHAR CampElite[]=TEXT("大野");

// 地图事件（#79）
inline constexpr TCHAR Dorm[]=TEXT("躺平角");
inline FString DormRegenActive(){return FString(Dorm)+TEXT("回血中");}
inline FString InDorm(){return FString(TEXT("在"))+Dorm+TEXT(" · 脱战后快速回复");}
inline constexpr TCHAR Airdrop[]=TEXT("天降外卖");
inline constexpr TCHAR AirdropLanded[]=TEXT("外卖到了 · 先到先得");
inline FString AirdropExpired(){return FString(Airdrop)+TEXT("没人领，消失了");}
inline FString AirdropOwnClaim(){return FString(TEXT("你抢到了"))+Airdrop+TEXT("！经验和一份构筑奖励到手");}
inline FString AirdropTaken(){return FString(Airdrop)+TEXT("被人抢走了");}
inline constexpr TCHAR Boss[]=TEXT("画饼老板");
// 击败画饼老板的最后一击得一份构筑奖励
inline constexpr TCHAR BossReward[]=TEXT("拿到一份构筑奖励");
inline FString BossAppeared(const FString& Name){return Name+TEXT(" 出现了 · 小地图金色巡逻点");}
inline FString BossLeft(const FString& Name){return Name+TEXT(" 没人打倒，离场了");}
inline FString BossDownSelf(const FString& Name){return FString(TEXT("你击败了"))+Name+TEXT("，")+BossReward+TEXT("！");}
inline FString BossDownBy(const FString& Killer,const FString& Name){return Killer+TEXT(" 击败了")+Name;}
inline FString BossDown(const FString& Name){return Name+TEXT(" 倒下了");}
// 营地标记：每个野外段各标一个高档营地和一个精英营地，首个清掉的人领奖
inline constexpr TCHAR CampMarkHigh[]=TEXT("高档营地标记：首个清掉的人得一份主动图纸");
inline constexpr TCHAR CampMarkElite[]=TEXT("精英营地标记：首个清掉的人得一组牌三选一");
inline FString CampMarkTaken(bool bHigh){return FString(bHigh?TEXT("高档营地标记"):TEXT("精英营地标记"))+TEXT("被人领走了");}
inline FString CampMarkOwnClaim(bool bHigh){return bHigh?FString(TEXT("你领到了高档营地标记：一份主动图纸")):FString(TEXT("你领到了精英营地标记：一组牌三选一"));}
inline FString CampMarkExpired(){return FString(TEXT("本段营地标记没人领，失效了"));}
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
inline constexpr TCHAR Choice[]=TEXT("构筑");
inline FString LevelUp(int32 Level){return FString::Printf(TEXT("涨粉了！Lv%d"),Level);}
inline constexpr TCHAR Blueprint[]=TEXT("图纸");
inline constexpr TCHAR StatPoints[]=TEXT("属性点");
inline FString OfferChance(int32 Count){return Count>1?FString::Printf(TEXT("%d 次选技能机会"),Count):FString(TEXT("一次选技能机会"));}
// 构筑牌（APP_SPEC §4）：形式（技能、冲刺、本体）× 类型（触发、改造、转化、成长、元素），另有元素勋章；三选一拿到的牌进
// 牌库，拖到对应的列上安装。
inline constexpr TCHAR Card[]=TEXT("牌");
inline constexpr TCHAR TakeToStorage[]=TEXT("放进牌库");
inline constexpr TCHAR PickElement[]=TEXT("选哪种元素：");
inline constexpr TCHAR CancelBinding[]=TEXT("取消");
inline constexpr TCHAR TargetDash[]=TEXT("冲刺(D)");
inline constexpr TCHAR WorkshopPreview[]=TEXT("数值变化");
inline constexpr TCHAR WorkshopPreviewPartial[]=TEXT("数值变化 · 部分");
inline constexpr TCHAR WorkshopPreviewSnapshot[]=TEXT("当前快照 · 未列出的效果以规则说明和实际结算为准");
inline constexpr TCHAR WorkshopPreviewSync[]=TEXT("正在同步构筑与战斗数据");
inline constexpr TCHAR WorkshopPreviewWaiting[]=TEXT("等待服务器回执");
inline constexpr TCHAR WorkshopPreviewChoice[]=TEXT("选择绑定后显示变化");
inline constexpr TCHAR WorkshopPreviewUnavailable[]=TEXT("暂时无法计算精确变化");
inline constexpr TCHAR WorkshopPreviewConditional[]=TEXT("其他效果见规则说明与实际结算");
inline constexpr TCHAR WorkshopPreviewNoChange[]=TEXT("当前没有数值变化");
inline constexpr TCHAR WorkshopKnockback[]=TEXT("击退");
inline constexpr TCHAR WorkshopDuration[]=TEXT("基础时长");
inline constexpr TCHAR WorkshopArea[]=TEXT("区域");
inline constexpr TCHAR WorkshopChain[]=TEXT("连锁");
inline constexpr TCHAR WorkshopEffect[]=TEXT("效果");
inline constexpr TCHAR WorkshopDistance[]=TEXT("距离");
inline constexpr TCHAR WorkshopRadius[]=TEXT("半径");
inline constexpr TCHAR WorkshopCount[]=TEXT("次数");
inline constexpr TCHAR WorkshopBase[]=TEXT("基础值");
inline constexpr TCHAR WorkshopDamageScale[]=TEXT("伤害倍率");
inline constexpr TCHAR WorkshopCooldown[]=TEXT("冷却");
inline constexpr TCHAR WorkshopRange[]=TEXT("射程");
inline constexpr TCHAR WorkshopProjectileRadius[]=TEXT("投射物半径");
inline constexpr TCHAR WorkshopLineHalfWidth[]=TEXT("判定半宽");
inline FString InstalledOn(const FString& Target){return FString::Printf(TEXT("（%s）"),*Target);}
// Spec §8.1 element names.
inline constexpr TCHAR ElementFire[]=TEXT("烫");
inline constexpr TCHAR ElementWater[]=TEXT("湿");
inline constexpr TCHAR ElementElectric[]=TEXT("麻");
inline constexpr TCHAR ElementChill[]=TEXT("冻");
inline constexpr TCHAR ElementRot[]=TEXT("馊");
inline FString CardTaken(const FString& Name){return FString::Printf(TEXT("已拿到牌：%s"),*Name);}
inline FString CardInstalled(const FString& Name){return FString::Printf(TEXT("已装上：%s"),*Name);}
inline FString CardStored(const FString& Name,const FString& Why)
{return Why.IsEmpty()?CardTaken(Name):FString::Printf(TEXT("已拿到牌：%s，先放牌库（%s）"),*Name,*Why);}
inline FString CardRemoved(const FString& Name){return FString::Printf(TEXT("已拆下：%s"),*Name);}
inline constexpr TCHAR CardsSkipped[]=TEXT("已跳过这组牌");
inline FString CardsRerolled(int32 Left){return FString::Printf(TEXT("已重抽，还剩 %d 次"),Left);}
// 升级横幅的第二行：这次升级得到的属性点、主动机会与牌三选一。
inline FString LevelUpGains(int64 Points,int32 Offers,int32 CardOffers)
{
    TArray<FString> Parts;
    if(Points>0){Parts.Add(FString::Printf(TEXT("%lld %s"),Points,StatPoints));}
    if(Offers>0){Parts.Add(OfferChance(Offers));}
    if(CardOffers>0){Parts.Add(CardOffers>1?FString::Printf(TEXT("%d 次选牌"),CardOffers):FString(TEXT("一次选牌")));}
    return Parts.IsEmpty()?FString():TEXT("获得 ")+FString::Join(Parts,TEXT(" · "));
}
inline FString LearnInto(const FString& Key){return FString(TEXT("学到 "))+Key;}
inline FString LearnedInto(const FString& Name,const FString& Key){return FString::Printf(TEXT("已学到 %s：%s"),*Key,*Name);}
inline FString StoredAsBlueprint(const FString& Name,const FString& Why)
{return Why.IsEmpty()?FString::Printf(TEXT("已存为%s：%s"),Blueprint,*Name):FString::Printf(TEXT("已存为%s：%s（%s）"),Blueprint,*Name,*Why);}
// 不能学习、加点或使用休息服务的原因（学习、加点与装牌随时可用，休息服务仍要活着、在配装窗口）。
inline constexpr TCHAR DeadNoLearn[]=TEXT("阵亡中");
inline constexpr TCHAR SlotsFull[]=TEXT("Q/W/E 已满");
inline constexpr TCHAR ReplaceAtRest[]=TEXT("休息时或在躺平角脱战后才能替换");
inline FString UnspentPoints(int64 Count){return FString::Printf(TEXT("未分配点数 %lld"),Count);}
// 牌库（HUD 右侧的牌堆）与展开后的安装列：Q/W/E、冲刺(D) 与本体（含普攻）各一列；元素勋章装在技能或本体的元素槽里。
inline constexpr TCHAR Storage[]=TEXT("牌库");
// 技能列与本体列的元素槽：空槽写“元素槽”，装上的勋章占这一格。
inline constexpr TCHAR MedalSlot[]=TEXT("元素槽");
inline constexpr TCHAR StorageEmpty[]=TEXT("牌库为空");
inline constexpr TCHAR StorageNoMatches[]=TEXT("没有符合的牌");
inline constexpr TCHAR StorageNoRoom[]=TEXT("牌库空间不足，暂时无法显示");
inline constexpr TCHAR StoragePreviousPage[]=TEXT("上一页");
inline constexpr TCHAR StorageNextPage[]=TEXT("下一页");
inline constexpr TCHAR PendingNoRoom[]=TEXT("待处理区空间不足，暂时无法完整显示");
inline constexpr TCHAR ColumnBody[]=TEXT("本体");
inline constexpr TCHAR ColumnEmptySlot[]=TEXT("空技能槽");
inline constexpr TCHAR DropHere[]=TEXT("放这里");
inline FString StorageFresh(int32 Count){return FString::Printf(TEXT("新 %d"),Count);}
inline FString CopiesBadge(int32 Copies){return FString::Printf(TEXT("×%d"),Copies);}
inline FString CannotGoThere(const FString& Name,const FString& Where){return FString::Printf(TEXT("%s不能装在%s"),*Name,*Where);}
// 牌的悬停弹层：库里的牌现在能装到哪些列，没有就直说。
inline FString CanGoOn(const FString& Columns){return FString::Printf(TEXT("可装：%s"),*Columns);}
inline constexpr TCHAR NoPlaceNow[]=TEXT("现在没有可装的位置");
// 每张装上的牌占一格容量（元素勋章不占）；等级决定格数。
inline constexpr TCHAR WorkshopFullReason[]=TEXT("工坊已满");
inline FString WorkshopFullHint(int32 Capacity){return FString::Printf(TEXT("工坊已满 %d/%d，拖到一张已装的牌上替换它"),Capacity,Capacity);}
inline FString WorkshopFullNoPlace(int32 Capacity){return FString::Printf(TEXT("工坊已满 %d/%d，现在没有可替换的位置"),Capacity,Capacity);}
inline constexpr TCHAR NoSkillPointReason[]=TEXT("没有技能点");
inline constexpr TCHAR SkillRank[]=TEXT("等级");
inline constexpr TCHAR Close[]=TEXT("关闭");
inline constexpr TCHAR BranchUnavailableReason[]=TEXT("不能升到这个分支");
inline constexpr TCHAR ReplaceAvailable[]=TEXT("拖到可替换的已装牌上");
// 拖动牌时跟随的一行预览：放下会装到哪里、替换谁；联动行列出词汇相通的技能和牌。
inline FString DropInstallsOn(const FString& Place){return FString::Printf(TEXT("装到 %s"),*Place);}
inline FString DropReplaces(const FString& Replaced,const FString& Place){return FString::Printf(TEXT("替换 %s · 装到 %s"),*Replaced,*Place);}
inline constexpr TCHAR DropNowhere[]=TEXT("不能装在这里");
// 工坊台（构筑的全屏视图）：规则板、容量条、牌库分组、角色栏、待处理与信息坞。
inline constexpr TCHAR WorkshopCapacityTitle[]=TEXT("工坊容量");
inline FString WorkshopUnlockAt(int32 Level){return FString::Printf(TEXT("Lv %d 解锁下一格"),Level);}
inline constexpr TCHAR WorkshopPending[]=TEXT("待处理");
inline constexpr TCHAR WorkshopReactions[]=TEXT("元素反应");
inline constexpr TCHAR WorkshopPointsLabel[]=TEXT("点可分配");
inline constexpr TCHAR WorkshopInstalledOn[]=TEXT("装在");
inline constexpr TCHAR WorkshopGoesOn[]=TEXT("可装");
inline constexpr TCHAR WorkshopLinks[]=TEXT("联动");
inline constexpr TCHAR WorkshopNotes[]=TEXT("注意");
inline constexpr TCHAR WorkshopLocked[]=TEXT("构筑暂不可调整");
inline constexpr TCHAR WorkshopClose[]=TEXT("Esc");
inline FString NeedsLearned(const FString& Names){return FString::Printf(TEXT("需要已学：%s"),*Names);}
// 十种元素反应（APP_SPEC §4.4），按 ELDCombatReaction 编号（6、9、12 已退役，留空）。
inline constexpr TCHAR ReactionFog[]=TEXT("起雾");
inline constexpr TCHAR ReactionConduct[]=TEXT("导电");
inline constexpr TCHAR ReactionOverload[]=TEXT("过载");
inline constexpr TCHAR ReactionMelt[]=TEXT("融化");
inline constexpr TCHAR ReactionMethane[]=TEXT("沼气爆");
inline constexpr TCHAR ReactionFreeze[]=TEXT("冻住");
inline constexpr TCHAR ReactionFerment[]=TEXT("发酵");
inline constexpr TCHAR ReactionShatter[]=TEXT("碎冰");
inline constexpr TCHAR ReactionCorrode[]=TEXT("腐蚀");
inline constexpr TCHAR ReactionFrostbite[]=TEXT("冻疮");
inline constexpr TCHAR ReactionFogRule[]=TEXT("烫 + 湿：对目标造成伤害并立即结算剩余灼烧，原地起雾，雾里的无元素敌人被弄湿。");
inline constexpr TCHAR ReactionConductRule[]=TEXT("湿 + 麻：目标受较高伤害并减速，附近其他湿的敌人也挨一下。");
inline constexpr TCHAR ReactionOverloadRule[]=TEXT("烫 + 麻：目标受较高伤害并破防，附近其他敌人也挨一下。");
inline constexpr TCHAR ReactionMeltRule[]=TEXT("烫 + 冻：目标受双倍伤害，脚下留水洼。");
inline constexpr TCHAR ReactionMethaneRule[]=TEXT("烫 + 馊：目标受较高伤害并重新烧起来，附近其他敌人也挨一下。");
inline constexpr TCHAR ReactionFreezeRule[]=TEXT("湿 + 冻：目标受伤害并被冻住，短时间定身。");
inline constexpr TCHAR ReactionFermentRule[]=TEXT("湿 + 馊：目标受伤害，挂上更久、更狠的馊。");
inline constexpr TCHAR ReactionShatterRule[]=TEXT("麻 + 冻：目标受伤害，下一次受到的技能伤害大幅提高。");
inline constexpr TCHAR ReactionCorrodeRule[]=TEXT("麻 + 馊：目标受伤害，被叠满更久的破防。");
inline constexpr TCHAR ReactionFrostbiteRule[]=TEXT("冻 + 馊：目标受伤害，攻速大幅降低。");
// Indexed by the reaction number; retired numbers stay empty.
inline constexpr const TCHAR* ReactionNames[]={TEXT(""),ReactionFog,ReactionConduct,ReactionOverload,ReactionMelt,ReactionMethane,
    TEXT(""),ReactionFreeze,ReactionFerment,TEXT(""),ReactionShatter,ReactionCorrode,TEXT(""),ReactionFrostbite};
inline constexpr const TCHAR* ReactionRules[]={TEXT(""),ReactionFogRule,ReactionConductRule,ReactionOverloadRule,ReactionMeltRule,
    ReactionMethaneRule,TEXT(""),ReactionFreezeRule,ReactionFermentRule,TEXT(""),ReactionShatterRule,ReactionCorrodeRule,TEXT(""),
    ReactionFrostbiteRule};
// The reaction term of an ELDCombatReaction number (empty for none or unknown).
inline FString ReactionTerm(uint8 Reaction){return Reaction<UE_ARRAY_COUNT(ReactionNames)?FString(ReactionNames[Reaction]):FString();}
inline FString ReactionExplanation(const FString& Name)
{
    for(int32 Index=1;Index<UE_ARRAY_COUNT(ReactionNames);++Index){if(!Name.IsEmpty()&&Name==ReactionNames[Index]){return ReactionRules[Index];}}
    return FString();
}
// 地面状态（APP_SPEC §4.4），按 ELDGroundStateKind 编号 1..5。
inline constexpr const TCHAR* GroundNames[]={TEXT(""),TEXT("水洼"),TEXT("冰面"),TEXT("火场"),TEXT("毒雾"),TEXT("电场")};
// 构筑时刻：主动三选一页与终局改装页。
inline constexpr TCHAR OfferStageTitle[]=TEXT("选一个技能");
inline FString OfferChanceOf(int32 Ordinal,int32 Earned){return FString::Printf(TEXT("机会 %d / %d"),Ordinal,Earned);}
inline constexpr TCHAR OfferLinks[]=TEXT("可接");
inline FString OfferLinksLine(const FString& Names){return FString::Printf(TEXT("可接：%s"),*Names);}
inline FString OfferLinksUnavailable(const FString& Reason){return FString::Printf(TEXT("可接预览不可用：%s"),*Reason);}
// 跳过主动候选或牌三选一补的属性点。
inline FString OfferSkipPoints(int64 Points){return FString::Printf(TEXT("+%lld 属性点"),Points);}
// Spec §3.13 休息商店：价格是属性点。
inline FString PointsGained(int64 Points){return FString::Printf(TEXT("+%lld 属性点"),Points);}
inline const TCHAR* const ShopRefreshed=TEXT("货架已刷新");
inline const TCHAR* const ShopTitle=TEXT("商店");
inline const TCHAR* const ChainCodex=TEXT("连锁图鉴");
inline FString ChainCodexCount(int32 Count){return FString::Printf(TEXT("已发现 %d 种连锁"),Count);}
inline FString ChainLongest(int32 Steps){return FString::Printf(TEXT("最长 %d 步"),Steps);}
// #305: the float over the own body when a counted chain of its ends.
inline FString ChainEndFloat(int32 Steps){return FString::Printf(TEXT("连锁 ×%d！"),Steps);}
inline FString ShopToggle(int64 Points){return FString::Printf(TEXT("商店 · 点 %lld"),Points);}
inline FString ShopRefresh(int32 Price){return Price>0?FString::Printf(TEXT("刷新 %d点"),Price):FString(TEXT("刷新 免费"));}
inline FString ShopSkillPoint(int32 Price,int32 Bought,int32 Max){return FString::Printf(TEXT("技能点 %d点  %d/%d"),Price,Bought,Max);}
inline FString ShopPrice(int32 Price){return FString::Printf(TEXT("%d点"),Price);}
inline const TCHAR* const ShopSoldMark=TEXT("已售");
inline const TCHAR* const PointGlyph=TEXT("点");
inline const TCHAR* const ResultHighlights=TEXT("本局最离谱");
inline const TCHAR* const BestHit=TEXT("最高一下");
inline const TCHAR* const SkillPointBought=TEXT("买到 1 个技能点");
inline constexpr TCHAR OfferSkip[]=TEXT("不选");
inline constexpr TCHAR OfferWaitingName[]=TEXT("选技能机会");
inline constexpr TCHAR OfferWaiting[]=TEXT("现在没有合格的技能候选，这次机会保留，有合格候选时再出现。");
inline FString ReactsWith(const FString& Key,const FString& Name,const FString& Pair,const FString& Reaction){return FString::Printf(TEXT("与 %s %s：%s %s"),*Key,*Name,*Pair,*Reaction);}
inline constexpr TCHAR FinalStageTitle[]=TEXT("终局改装");
inline constexpr TCHAR FinalOnce[]=TEXT("一局一次");
inline constexpr TCHAR FinalLock[]=TEXT("按住选定改装");
inline constexpr TCHAR FinalTakenNote[]=TEXT("改装只选一次；技能和牌仍可随时调整。");
inline constexpr TCHAR FinalTaken[]=TEXT("已选定");
inline constexpr TCHAR ForgetShort[]=TEXT("遗忘");
inline FString WorksWith(const FString& Names){return FString::Printf(TEXT("联动：%s"),*Names);}
inline FString WorkshopCaption(int32 Installed,int32 Capacity){return FString::Printf(TEXT("已装 %d/%d"),Installed,Capacity);}
// 牌的形式与类型（APP_SPEC §4）。
inline const TCHAR* CardFormName(uint8 Form)
{return Form==1?TEXT("技能"):Form==2?TEXT("冲刺"):Form==3?TEXT("本体"):Form==4?TEXT("勋章"):TEXT("");}
inline const TCHAR* CardTypeName(uint8 Type)
{return Type==0?TEXT("触发"):Type==1?TEXT("改造"):Type==2?TEXT("转化"):Type==3?TEXT("成长"):Type==4?TEXT("元素"):TEXT("");}
// 牌三选一（APP_SPEC §4）：开局 R0 一组，之后每升一级一组，野外奖励也给一组；可重抽、跳过。
inline constexpr TCHAR CardStageTitle[]=TEXT("选一张牌");
inline FString CardOfferOf(int32 Ordinal,int32 Queued){return Queued>1?FString::Printf(TEXT("还有 %d 组"),Queued-1):FString();}
inline FString CardReroll(int32 Left){return FString::Printf(TEXT("重抽 %d"),Left);}
inline constexpr TCHAR CardSkip[]=TEXT("跳过");
// 主菜单“怎么玩”：六张短卡，每张一个标题一句话。
inline constexpr TCHAR HowTo[]=TEXT("怎么玩");
inline constexpr int32 HowToCards=6;
inline const TCHAR* HowToTitle(int32 Index)
{
    static const TCHAR* const Titles[HowToCards]={TEXT("右键走，右键打"),TEXT("放技能要劲"),TEXT("升级做三件事"),TEXT("休息时配装"),TEXT("两种元素一碰"),TEXT("最后抢旗杆")};
    return Index>=0&&Index<HowToCards?Titles[Index]:TEXT("");
}
inline const TCHAR* HowToBody(int32 Index)
{
    static const TCHAR* const Bodies[HowToCards]={
        TEXT("右键地面移动，按住就跟着鼠标走；右键敌人或野怪就一直普攻。"),
        TEXT("技能键加左键确认。打人、连击、躲招都回劲；劲够多时长按技能键是更强的爆劲。"),
        TEXT("加属性点，选一张牌，技能格发光时点它升技能。"),
        TEXT("每段野外之后回休息室：逛商店、装牌、打假人试招；站进报名圈参加 PK。"),
        TEXT("目标身上挂着一种元素时，再打上另一种就会反应。"),
        TEXT("终局站进旗杆圈喷漆，喷完守住就赢。")};
    return Index>=0&&Index<HowToCards?Bodies[Index]:TEXT("");
}
inline FString PointsAdded(int64 Spent,int64 Left){return FString::Printf(TEXT("已加 %lld 点，还剩 %lld 点"),Spent,Left);}

// 休息服务（#198，APP_SPEC §4.4/§5）：只在休息的配装窗口（配装与派奖）可用，服务器裁决每一条命令。
inline constexpr TCHAR RestServices[]=TEXT("休息服务");
inline constexpr TCHAR Respec[]=TEXT("洗点");
inline FString ReplaceInto(const FString& Key){return FString(TEXT("替换 "))+Key;}
inline FString ForgetSlot(const FString& Key,const FString& Name){return FString::Printf(TEXT("遗忘 %s：%s"),*Key,*Name);}
inline FString ReplaceConfirm(const FString& NewName,const FString& Key,const FString& OldName)
{return FString::Printf(TEXT("用 %s 替换 %s：%s？%s 会被遗忘，不退还"),*NewName,*Key,*OldName,*OldName);}
inline FString ForgetConfirm(const FString& Key,const FString& Name)
{return FString::Printf(TEXT("遗忘 %s：%s？不退还，之后还能再抽到"),*Key,*Name);}
inline constexpr TCHAR ConfirmAction[]=TEXT("确认");
inline constexpr TCHAR CancelAction[]=TEXT("取消");
inline FString Respecced(int64 Refunded,int64 Left){return FString::Printf(TEXT("已洗点：返还 %lld 点，现有 %lld 点未分配"),Refunded,Left);}
inline FString Replaced(const FString& NewName,const FString& Key,const FString& OldName)
{return FString::Printf(TEXT("已在 %s 用 %s 替换 %s"),*Key,*NewName,*OldName);}
inline FString Forgotten(const FString& Name){return FString::Printf(TEXT("已遗忘：%s（之后还能再抽到）"),*Name);}
// 野外奖励（天降外卖、画饼老板、高档/精英营地标记）的提示。
inline constexpr TCHAR FieldRewardHighCamp[]=TEXT("高档营地标记");
inline constexpr TCHAR FieldRewardEliteCamp[]=TEXT("精英营地标记");
inline FString FieldRewardBlueprint(const FString& Name){return FString::Printf(TEXT("获得%s「%s」"),Blueprint,*Name);}
inline FString FieldRewardCards(){return FString(TEXT("一组牌三选一已就绪，打开"))+Choice+TEXT("选择");}
inline FString FieldRewardBlueprintHeld(){return FString(Blueprint)+TEXT("池已空，奖励保留，之后有能给的技能时补发");}

// 阶段与旗杆（喷漆守护终局）
// 对局流程（#198）：开局休息 R0，之后三段野外各接一次休息 R1–R3，最后一段野外开放旗杆。
// 休息的返回倒计时在屏幕中央整秒倒数，每段野外开始时显示“开淫！”。
inline constexpr TCHAR CountdownGo[]=TEXT("开淫！");
inline constexpr TCHAR NotStartedYet[]=TEXT("还没开始呢，急什么");
inline constexpr TCHAR RestPhase[]=TEXT("课间休息，抓紧配装");
inline FString RestLabel(int32 Index){return FString::Printf(TEXT("休息 R%d"),Index);}
// 休息的各步（配装的最后一段兼作 PK 报名；派奖时可以再调整配装）。
inline constexpr TCHAR RestBuild[]=TEXT("配装与训练");
inline constexpr TCHAR RestDraw[]=TEXT("PK 公布赛制与分队");
inline constexpr TCHAR RestBetting[]=TEXT("押注");
inline constexpr TCHAR RestPKReady[]=TEXT("PK 准备");
inline constexpr TCHAR RestPK[]=TEXT("PK 中");
inline constexpr TCHAR RestSettlement[]=TEXT("PK 结算");
inline constexpr TCHAR RestPayout[]=TEXT("派奖 · 可再配装");
inline constexpr TCHAR RestReturn[]=TEXT("返回野外");
inline FString SignupOpensIn(int64 Seconds){return FString::Printf(TEXT("PK 报名 %lld 秒后开放"),Seconds);}
inline FString SignupOpenFor(int64 Seconds){return FString::Printf(TEXT("PK 报名中 · 剩 %lld 秒"),Seconds);}
inline FString SignedUpCount(int32 Count){return FString::Printf(TEXT("已报名 %d 人"),Count);}
// 报名窗内站在公共报名圈里的玩家在截止时锁定参赛。
inline constexpr TCHAR SignedUpSelf[]=TEXT("你在报名圈里，已报名");
inline constexpr TCHAR NotSignedUpSelf[]=TEXT("站进报名圈即报名");
inline FString PKEntrants(int32 Count){return FString::Printf(TEXT("PK %d 人参赛"),Count);}
inline constexpr TCHAR PKSkipped[]=TEXT("本次休息不举行 PK");
// PK 与观众（#198 D，APP_SPEC §5）：抽签公布赛制与分队；没参赛的观众押一方会赢（免费、一人一票、押注结束前可改）；
// PK 中观众可以往竞技场地面投掷干扰；出局的参赛者只能观战。数值来自内容参数，这里不写数字。
inline constexpr TCHAR PKDuel[]=TEXT("单挑");
inline constexpr TCHAR PKFreeForAll[]=TEXT("混战");
inline FString PKTeams(int32 PerSide){return FString::Printf(TEXT("%dv%d"),PerSide,PerSide);}
inline FString PKTitle(uint32 Serial,const FString& Format,int32 Entrants)
{return FString::Printf(TEXT("第 %u 场 PK · %s · %d 人"),Serial,*Format,Entrants);}
inline FString PKSide(int32 Side){return FString::Printf(TEXT("第 %d 方"),Side);}
inline constexpr TCHAR PKFormatPending[]=TEXT("赛制公布中");
inline constexpr TCHAR PKClockSync[]=TEXT("PK 时间同步中");
inline constexpr TCHAR PKUnlisted[]=TEXT("未列名参赛者");
inline constexpr TCHAR PKOut[]=TEXT("出局");
inline constexpr TCHAR PKForfeit[]=TEXT("离场");
inline FString PKBetCount(int32 Count){return FString::Printf(TEXT("%d 人押"),Count);}
inline constexpr TCHAR PKOwnSide[]=TEXT("你在这方");
inline constexpr TCHAR PKTeammate[]=TEXT("队友");
inline constexpr TCHAR PKEnemy[]=TEXT("敌方");
inline constexpr TCHAR PKOwnBet[]=TEXT("你押了这方");
inline FString PKStartsIn(int64 Seconds){return FString::Printf(TEXT("%lld 秒后开打 · 准备中不能移动和攻击"),Seconds);}
inline FString PKTimeLeft(int64 Seconds){return FString::Printf(TEXT("最长还剩 %lld 秒"),Seconds);}
inline FString PKRingNow(const FString& Meters){return FString::Printf(TEXT("安全圈 %s 米"),*Meters);}
inline FString PKRingSoon(const FString& Meters,int64 Seconds){return FString::Printf(TEXT("%lld 秒后开始缩到 %s 米"),Seconds,*Meters);}
inline FString PKRingShrinking(const FString& Meters){return FString::Printf(TEXT("正在缩到 %s 米"),*Meters);}
inline constexpr TCHAR PKRoleFighter[]=TEXT("你在场上：活到最后就赢，圈外会持续掉血");
inline constexpr TCHAR PKRoleEntrant[]=TEXT("你已参赛：等观众押注后入场");
inline constexpr TCHAR PKRoleOut[]=TEXT("你已出局，只能观战");
inline constexpr TCHAR PKRoleBet[]=TEXT("押一方会赢：免费，一人一票，押注结束前可以改");
inline constexpr TCHAR PKRoleThrow[]=TEXT("观战中");
inline constexpr TCHAR PKRoleWatch[]=TEXT("观战中");
inline FString PKBetOn(const FString& Names){return FString(TEXT("押 "))+Names;}
inline constexpr TCHAR PKThrow[]=TEXT("投掷");
// The throw button's hover line: what a hit does, nothing about how to press.
inline constexpr TCHAR PKThrowEffect[]=TEXT("砸中：减速并暴露位置");
inline constexpr TCHAR PKThrowAim[]=TEXT("点竞技场地面投掷 · 右键取消");
inline constexpr TCHAR PKThrowSending[]=TEXT("投掷中…");
inline FString PKThrowCooling(const FString& Left){return FString::Printf(TEXT("投掷冷却 %s 秒"),*Left);}
inline constexpr TCHAR PKThrowCoolingSync[]=TEXT("投掷冷却时间同步中");
inline constexpr TCHAR PKThrowNoGround[]=TEXT("这里没有可投掷的地面");
inline FString PKWinners(const FString& Names){return FString(TEXT("PK 胜方："))+Names;}
inline constexpr TCHAR PKWonSelf[]=TEXT("你赢了这场 PK！");
inline constexpr TCHAR PKDraw[]=TEXT("PK 平局：没有分出唯一胜方");
inline constexpr TCHAR PKNoContest[]=TEXT("本场 PK 无效：竞技场没能布置，不发奖励");
inline constexpr TCHAR PKAwardsTitle[]=TEXT("PK 奖励（属性点）");
inline constexpr TCHAR PKAwardsNone[]=TEXT("本场没有人得到奖励");
inline constexpr TCHAR PKAwardParticipation[]=TEXT("参与");
inline constexpr TCHAR PKAwardWinner[]=TEXT("胜者");
inline constexpr TCHAR PKAwardBet[]=TEXT("竞猜");
inline FString PKAwardTotal(int64 Points){return FString::Printf(TEXT("共 %lld 点"),Points);}
inline constexpr TCHAR PKSpectating[]=TEXT("观战镜头：竞技场");
// 公共休息室中的训练；显示范围由获准观察帧裁定，重置结果由服务器回执确认。
inline constexpr TCHAR TrainingReset[]=TEXT("重置训练");
inline constexpr TCHAR TrainingDummy[]=TEXT("训练假人");
inline FString FieldSegmentLine(int32 Segment,int32 Segments,const FString& Clock)
{return FString::Printf(TEXT("野外 %d/%d · 下次休息 %s"),Segment,Segments,*Clock);}
inline constexpr TCHAR FinalField[]=TEXT("最终野外 · 旗杆开放");
// P6 对局节奏与地图变量（APP_SPEC §5–§7）：今日热点、营地词条、中途目标、藏起来的东西与复活选点。
// 热点和词条在格子里只显示一个字的图标与名称，完整效果放悬停说明（数值由 LDHUD::DailyDescription 从内容参数拼出）。
inline const TCHAR* DailyName(ELDDailyModifier Modifier)
{
    switch(Modifier)
    {
    case ELDDailyModifier::Rain: return TEXT("暴雨");
    case ELDDailyModifier::Cold: return TEXT("寒潮");
    case ELDDailyModifier::Blackout: return TEXT("停电");
    case ELDDailyModifier::Typhoon: return TEXT("台风");
    case ELDDailyModifier::Heat: return TEXT("高温");
    case ELDDailyModifier::Sale: return TEXT("大促");
    case ELDDailyModifier::Delivery: return TEXT("外卖节");
    case ELDDailyModifier::Patrol: return TEXT("城管巡逻");
    case ELDDailyModifier::Exam: return TEXT("考试周");
    case ELDDailyModifier::Gym: return TEXT("健身日");
    case ELDDailyModifier::Overtime: return TEXT("加班");
    case ELDDailyModifier::Rumor: return TEXT("谣言");
    default: return TEXT("");
    }
}
// 热点图标：一个字的徽章（形状与颜色由小地图绘制，不只靠颜色区分）。
inline const TCHAR* DailyGlyph(ELDDailyModifier Modifier)
{
    switch(Modifier)
    {
    case ELDDailyModifier::Rain: return TEXT("雨");
    case ELDDailyModifier::Cold: return TEXT("寒");
    case ELDDailyModifier::Blackout: return TEXT("暗");
    case ELDDailyModifier::Typhoon: return TEXT("风");
    case ELDDailyModifier::Heat: return TEXT("热");
    case ELDDailyModifier::Sale: return TEXT("促");
    case ELDDailyModifier::Delivery: return TEXT("餐");
    case ELDDailyModifier::Patrol: return TEXT("管");
    case ELDDailyModifier::Exam: return TEXT("考");
    case ELDDailyModifier::Gym: return TEXT("健");
    case ELDDailyModifier::Overtime: return TEXT("班");
    case ELDDailyModifier::Rumor: return TEXT("谣");
    default: return TEXT("");
    }
}
inline const TCHAR* AffixName(ELDCampAffix Affix)
{
    switch(Affix)
    {
    case ELDCampAffix::Overtime: return TEXT("加班");
    case ELDCampAffix::Teambuild: return TEXT("团建");
    case ELDCampAffix::Fridge: return TEXT("冰柜");
    case ELDCampAffix::Bonus: return TEXT("年终奖");
    case ELDCampAffix::Layoff: return TEXT("裁员");
    case ELDCampAffix::Audit: return TEXT("突击检查");
    default: return TEXT("");
    }
}
inline const TCHAR* AffixGlyph(ELDCampAffix Affix)
{
    switch(Affix)
    {
    case ELDCampAffix::Overtime: return TEXT("班");
    case ELDCampAffix::Teambuild: return TEXT("团");
    case ELDCampAffix::Fridge: return TEXT("冰");
    case ELDCampAffix::Bonus: return TEXT("奖");
    case ELDCampAffix::Layoff: return TEXT("裁");
    case ELDCampAffix::Audit: return TEXT("查");
    default: return TEXT("");
    }
}
inline constexpr TCHAR MiniFlag[]=TEXT("喷漆小旗");
inline constexpr TCHAR Arena[]=TEXT("擂台");
inline constexpr TCHAR Locker[]=TEXT("快递柜");
inline constexpr TCHAR Peddler[]=TEXT("二手贩子");
inline constexpr TCHAR SpawnChoice[]=TEXT("选出生点");
inline FString SpawnCandidate(int32 Index){return FString::Printf(TEXT("出生点 %d"),Index+1);}

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
inline FString GraffitiOf(const FString& Owner){return FString(TEXT("旗杆上是 "))+Owner+TEXT(" 的涂鸦");}
inline FString GraffitiSprayed(const FString& Owner){return Owner+TEXT(" 在旗杆上喷上了涂鸦");}
inline constexpr TCHAR GuardLeft[]=TEXT("再守");
// Final mods in the showdown (content §4.7).
inline constexpr TCHAR GraffitiFades[]=TEXT("涂鸦褪色还剩");
inline constexpr TCHAR GraffitiFaded[]=TEXT("涂鸦褪色了，旗杆空出来了");
inline FString HolderWithMod(const FString& Holder,const FString& Mod){return Mod.IsEmpty()?Holder:Holder+TEXT("（")+Mod+TEXT("）");}
inline FString PoleCarried(const FString& Owner){return Owner+TEXT(" 扛着旗杆跑了");}
inline FString CageUp(const FString& Owner){return Owner+TEXT(" 喷上涂鸦，铁笼升起：圈内出不去、圈外进不来");}
inline constexpr TCHAR CageDown[]=TEXT("铁笼落下了");
inline FString Usurped(const FString& Owner){return Owner+TEXT(" 篡位，旗杆上的涂鸦归他了");}
inline FString PrivatePoleSprayed(const FString& Holder){return Holder+TEXT(" 喷上了私人旗杆，进去冻结他的守护");}
inline constexpr TCHAR GuardPaused[]=TEXT("有人进圈，守护冻结");
inline constexpr TCHAR GuardAway[]=TEXT("守擂者不在圈内，守护暂停");
inline constexpr TCHAR GuardReset[]=TEXT("守擂者离圈，守护清零");
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
inline FString SprayCountWin(const FString& WinnerName){return WinnerName+TEXT(" 喷够了次数，恭喜成为今晚的")+Winner+TEXT("！");}
inline FString ClearOutWin(const FString& WinnerName){return WinnerName+TEXT(" 清场了，恭喜成为今晚的")+Winner+TEXT("！");}
inline constexpr TCHAR NoCapture[]=TEXT("没人守住涂鸦");
inline constexpr TCHAR Scoreboard[]=TEXT("排行榜");
inline constexpr TCHAR ResultKills[]=TEXT("淫了几个");
inline constexpr TCHAR ResultDeaths[]=TEXT("寄了几次");
inline constexpr TCHAR ResultAssists[]=TEXT("助攻");

// 构筑蓝图（#198 §10）：局末本人只读记录，只保存配方，不授予能力；与学习用的“图纸”不是一回事。
inline constexpr TCHAR BuildBlueprint[]=TEXT("构筑蓝图");
inline constexpr TCHAR BlueprintReadOnly[]=TEXT("只读记录 · 只保存配方，不授予能力，也不会恢复遗忘的技能");
inline constexpr TCHAR BlueprintExport[]=TEXT("导出文件");
inline constexpr TCHAR BlueprintOpenFolder[]=TEXT("打开所在文件夹");
inline constexpr TCHAR BlueprintImport[]=TEXT("导入查看");

// 交互丝滑度提示与关键石名称（#331）
inline FString DeathWorkshopPrompt(const FString& Key,const FString& Countdown)
{return Countdown.IsEmpty()?FString::Printf(TEXT("按 [%s] 趁复活调整构筑"),*Key):FString::Printf(TEXT("按 [%s] 趁复活调整构筑（%s 后复活）"),*Key,*Countdown);}
inline constexpr TCHAR InstalledCardHint[]=TEXT("右键：卸下 · 拖拽：换槽");
inline constexpr TCHAR ReplacePickPrompt[]=TEXT("点击要替换的旧牌（Esc 取消）");
inline constexpr TCHAR NoCompatibleSkillHost[]=TEXT("⚠ 当前已学技能暂无可装槽（换技能后可用）");
inline const TCHAR* KeystoneName(FName StatId)
{
    if(StatId==TEXT("stat_physique")){return TEXT("回血成盾");}
    if(StatId==TEXT("stat_thick_skin")){return TEXT("硬骨头");}
    if(StatId==TEXT("stat_strength")){return TEXT("破竹势");}
    if(StatId==TEXT("stat_brains")){return TEXT("省流大师");}
    if(StatId==TEXT("stat_agility")){return TEXT("顺步追击");}
    return TEXT("关键石");
}
inline FString ResultRuleBoardTitle(const FString& Name){return Name.IsEmpty()?FString(TEXT("赢家规则板")):FString::Printf(TEXT("【%s】的规则板"),*Name);}
inline constexpr TCHAR ResultViewSelf[]=TEXT("查看自己");
inline constexpr TCHAR ResultInspectSelf[]=TEXT("查看自己");
inline constexpr TCHAR ResultClickSwitchHint[]=TEXT("点击左侧任意参赛者行可切换查看其完整终局构筑");
inline constexpr TCHAR CopyRoomAddress[]=TEXT("复制房间地址");
inline constexpr TCHAR RestReadyCommit[]=TEXT("准备完毕");
inline constexpr TCHAR RestReadyCancel[]=TEXT("已准备（点击取消）");
inline constexpr TCHAR BlueprintBack[]=TEXT("返回本局");
inline constexpr TCHAR NotCollected[]=TEXT("未采集");
// 按构筑版本汇总的事实，顺序与 ELDBlueprintFact 一致（LDBlueprintPanel.cpp 静态核对数量）。
inline constexpr const TCHAR* BlueprintFactLabels[]={
    TEXT("造成伤害"),TEXT("其中对玩家"),TEXT("承受伤害"),TEXT("自身护盾吸收"),TEXT("生命损失"),TEXT("一次性回复"),TEXT("获得护盾"),
    TEXT("普攻"),TEXT("施法"),TEXT("自然回复"),TEXT("减免"),TEXT("触发"),TEXT("拒绝"),
};

// 卡面关键词：卡面加粗，悬停卡片时在详情里逐个解释。卡面与关键词解释都不写喷漆或守护。
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
    {TEXT("冻"),TEXT("攻速和移速降低；遇烫融化，遇湿冻住")},
    {TEXT("馊"),TEXT("受到的治疗和护盾减少，并持续受到少量伤害")},
    {TEXT("水洼"),TEXT("站在上面的敌人会湿；遇麻变电场，遇冻变冰面，遇烫起雾")},
    {TEXT("冰面"),TEXT("站在上面的敌人起步和停下都变慢，被击退和冲刺更远；遇烫化成水洼")},
    {TEXT("火场"),TEXT("站在上面的敌人每秒多一层灼烧；遇湿熄灭起雾")},
    {TEXT("毒雾"),TEXT("站在里面的敌人会馊；遇烫整团爆炸")},
    {TEXT("电场"),TEXT("站在上面的敌人持续挨电并挂麻")},
    {ReactionFog,ReactionFogRule},
    {ReactionConduct,ReactionConductRule},
    {ReactionOverload,ReactionOverloadRule},
    {ReactionMelt,ReactionMeltRule},
    {ReactionMethane,ReactionMethaneRule},
    {ReactionFreeze,ReactionFreezeRule},
    {ReactionFerment,ReactionFermentRule},
    {ReactionShatter,ReactionShatterRule},
    {ReactionCorrode,ReactionCorrodeRule},
    {ReactionFrostbite,ReactionFrostbiteRule},
};
}
