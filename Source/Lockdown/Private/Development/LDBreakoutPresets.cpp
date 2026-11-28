#include "Development/LDBreakoutPresets.h"

#include "Development/LDDemoContentDefinitions.h"

namespace
{
using ET = ELDCardTargetKind;
using EB = ELDSkillBranch;

TArray<FLDBreakoutPreset> MakeBreakoutPresets()
{
    TArray<FLDBreakoutPreset> Out;
    // 技能永动机: S06 2B/3B1, S10 2B/3B1, S18 2A/3A2, haste 100; K01 on each skill, K06, Y16.
    Out.Add({TEXT("breakout_skill_engine"), TEXT("技能永动机"),
        {{TEXT("active_snark_bolt"), EB::B1}, {TEXT("active_zap"), EB::B1}, {TEXT("active_calculus_drop"), EB::A2}},
        {{TEXT("card_next_up"), ET::Skill, 0}, {TEXT("card_next_up"), ET::Skill, 1}, {TEXT("card_next_up"), ET::Skill, 2},
            {TEXT("card_encore"), ET::Skill, 2}, {TEXT("card_quick_brain"), ET::Body}},
        nullptr, 0, 10000});
    Out.Last().Allocation = {{TEXT("stat_brains"), 18}, {TEXT("stat_agility"), 18}, {TEXT("stat_physique"), 4}, {TEXT("stat_thick_skin"), 4}, {TEXT("stat_strength"), 4}};
    // 攻速拉满: S17 2A/3A1; A12, A09, A03 (on the body), K01.
    Out.Add({TEXT("breakout_attack_speed"), TEXT("攻速拉满"),
        {{TEXT("active_triple_combo"), EB::A1}},
        {{TEXT("card_heated_up"), ET::Body}, {TEXT("card_quick_hands"), ET::Body}, {TEXT("card_solid_punch"), ET::Body},
            {TEXT("card_next_up"), ET::Skill, 0}}});
    Out.Last().Allocation = {{TEXT("stat_strength"), 18}, {TEXT("stat_agility"), 18}, {TEXT("stat_physique"), 4}, {TEXT("stat_thick_skin"), 4}, {TEXT("stat_brains"), 4}};
    // 吸血永生: S07 2B/3B2, S08, S13; K12 x2, Y10, Y14, Y19, Y17.
    Out.Add({TEXT("breakout_lifesteal"), TEXT("吸血永生"),
        {{TEXT("active_seethe"), EB::B2}, {TEXT("active_quick_meeting")}, {TEXT("active_charged_ram")}},
        {{TEXT("card_blood_price"), ET::Skill, 0}, {TEXT("card_blood_price"), ET::Skill, 1}, {TEXT("card_skill_drain"), ET::Body},
            {TEXT("card_overfed"), ET::Body}, {TEXT("card_grow_fat"), ET::Body}, {TEXT("card_punch_heal"), ET::Body}}});
    Out.Last().Allocation = {{TEXT("stat_strength"), 18}, {TEXT("stat_brains"), 18}, {TEXT("stat_physique"), 4}, {TEXT("stat_thick_skin"), 4}, {TEXT("stat_agility"), 4}};
    // 攒个大的: S18 2B/3B1; Y22, Y09, K16, K09; strength 18.
    Out.Add({TEXT("breakout_big_one"), TEXT("攒个大的"),
        {{TEXT("active_calculus_drop"), EB::B1}},
        {{TEXT("card_charge_up"), ET::Body}, {TEXT("card_skill_crit"), ET::Body}, {TEXT("card_held_shot"), ET::Skill, 0},
            {TEXT("card_big_one"), ET::Skill, 0}},
        TEXT("stat_strength"), 18});
    Out.Last().Allocation = {{TEXT("stat_agility"), 18}, {TEXT("stat_physique"), 6}, {TEXT("stat_thick_skin"), 6}};
    // 刺猬肉山: S14 2A/3A1, S15 2A/3A1, S16; Y13, Y01 x2, Y21; thick skin 18.
    Out.Add({TEXT("breakout_hedgehog"), TEXT("刺猬肉山"),
        {{TEXT("active_not_mad"), EB::A1}, {TEXT("active_stare_down"), EB::A1}, {TEXT("active_brace")}},
        {{TEXT("card_thick_hide"), ET::Body}, {TEXT("card_spiky_hide"), ET::Body}, {TEXT("card_spiky_hide"), ET::Body},
            {TEXT("card_tough_skin"), ET::Body}},
        TEXT("stat_thick_skin"), 18});
    Out.Last().Allocation = {{TEXT("stat_strength"), 18}, {TEXT("stat_brains"), 12}};
    // 元素永动: S07, S09, S10; Y05, Y04, Y27, K25.
    Out.Add({TEXT("breakout_element_loop"), TEXT("元素永动"),
        {{TEXT("active_seethe")}, {TEXT("active_hose_down")}, {TEXT("active_zap")}},
        {{TEXT("card_wave_after"), ET::Body}, {TEXT("card_reaction_refund"), ET::Body}, {TEXT("card_louder_blast"), ET::Body},
            {TEXT("card_blast_reload"), ET::Skill, 0}}});
    Out.Last().Allocation = {{TEXT("stat_physique"), 18}, {TEXT("stat_brains"), 18}, {TEXT("stat_thick_skin"), 6}, {TEXT("stat_agility"), 6}};
    // 冲刺鬼影: S11 2B/3B1; D01, D06, D03.
    Out.Add({TEXT("breakout_dash_ghost"), TEXT("冲刺鬼影"),
        {{TEXT("active_bail_out"), EB::B1}},
        {{TEXT("card_bump_reset"), ET::Dash}, {TEXT("card_pass_through"), ET::Dash},
            {TEXT("card_dash_punch"), ET::Dash}}});
    Out.Last().Allocation = {{TEXT("stat_thick_skin"), 18}, {TEXT("stat_agility"), 18}, {TEXT("stat_physique"), 4}, {TEXT("stat_strength"), 4}, {TEXT("stat_brains"), 4}};
    // 人头滚雪球: S03, S02; Y02, Y20.
    Out.Add({TEXT("breakout_snowball"), TEXT("人头滚雪球"),
        {{TEXT("active_crotch_grab")}, {TEXT("active_pants_down")}},
        {{TEXT("card_takedown"), ET::Body}, {TEXT("card_kill_streak"), ET::Body}}});
    Out.Last().Allocation = {{TEXT("stat_strength"), 18}, {TEXT("stat_brains"), 18}, {TEXT("stat_physique"), 4}, {TEXT("stat_thick_skin"), 4}, {TEXT("stat_agility"), 4}};
    // 残血狂暴: S17, S05; Y15, Y03, K12, Y17, card_desperado.
    Out.Add({TEXT("breakout_berserk"), TEXT("残血狂暴"),
        {{TEXT("active_triple_combo")}, {TEXT("active_unplug")}},
        {{TEXT("card_berserk"), ET::Body}, {TEXT("card_hang_on"), ET::Body}, {TEXT("card_blood_price"), ET::Skill, 0},
            {TEXT("card_punch_heal"), ET::Body}, {TEXT("card_desperado"), ET::Body}}});
    Out.Last().Allocation = {{TEXT("stat_strength"), 18}, {TEXT("stat_agility"), 18}, {TEXT("stat_physique"), 4}, {TEXT("stat_thick_skin"), 4}, {TEXT("stat_brains"), 4}};
    // 地雷阵 (P4): S08 2A, S19 2A, active_cordon_tape; Y12, Y23, D11, D10. A DEV sample only, never an AI goal.
    FLDBreakoutPreset Minefield{TEXT("breakout_minefield"), TEXT("地雷阵"),
        {{TEXT("active_quick_meeting"), EB::A}, {TEXT("active_flyer_trap"), EB::A}, {TEXT("active_cordon_tape")}},
        {{TEXT("card_double_stall"), ET::Body}, {TEXT("card_turf_growth"), ET::Body}, {TEXT("card_move_turf"), ET::Dash},
            {TEXT("card_ice_trail"), ET::Dash}}};
    Minefield.bAIGoal = false;
    Minefield.Allocation = {{TEXT("stat_physique"), 18}, {TEXT("stat_brains"), 18}, {TEXT("stat_thick_skin"), 6}, {TEXT("stat_agility"), 6}};
    Out.Add(MoveTemp(Minefield));
    // B01 X1 (evaluation only, never an AI goal): S02 2B 抽裤腰带, the 620 cm line grab, with the two control skill cards
    // (card_pin_amp, card_hold_longer) on it. A test of how much the far root depends on aiming ahead of the rival.
    FLDBreakoutPreset LongGrab{TEXT("breakout_long_grab"), TEXT("抽裤腰带（评估）"),
        {{TEXT("active_pants_down"), EB::B}},
        {{TEXT("card_pin_amp"), ET::Skill, 0}, {TEXT("card_hold_longer"), ET::Skill, 0}}};
    LongGrab.bAIGoal = false;
    LongGrab.Allocation = {{TEXT("stat_strength"), 18}, {TEXT("stat_agility"), 18}, {TEXT("stat_physique"), 4},
        {TEXT("stat_thick_skin"), 4}, {TEXT("stat_brains"), 4}};
    Out.Add(MoveTemp(LongGrab));
    return Out;
}
}

const TArray<FLDBreakoutPreset>& LDBreakoutPresets::All()
{
    static const TArray<FLDBreakoutPreset> Presets = MakeBreakoutPresets();
    return Presets;
}

const FLDBreakoutPreset* LDBreakoutPresets::Find(FName Id)
{
    return All().FindByPredicate([Id](const FLDBreakoutPreset& Preset) { return FName(Preset.Id) == Id; });
}

bool LDBreakoutPresets::HasCard(const FLDBreakoutPreset& Preset, FName Card)
{
    return Preset.Cards.ContainsByPredicate([Card](const FLDBreakoutCard& Row) { return FName(Row.Card) == Card; });
}

bool LDBreakoutPresets::Build(const FLDDemoContentDefinitions& Content, const FLDBreakoutPreset& Preset, FLDGrowthBuild& Out,
    int64& OutXP, FString& Error)
{
    Error.Reset();
    OutXP = 0;
    const int32 MaxLevel = Content.Progression.MaxLevel;
    if (MaxLevel < 1 || Content.Progression.XPThresholds.Num() < MaxLevel || Preset.Skills.Num() > 3)
    {
        Error = TEXT("The progression table or the preset is malformed.");
        return false;
    }
    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Level = uint8(MaxLevel);
    OutXP = Content.Progression.XPThresholds[MaxLevel - 1];
    for (int32 Index = 0; Index < Preset.Skills.Num(); ++Index)
    {
        const FName Id(Preset.Skills[Index].Ability);
        Build.Actives.Add({Id, uint8(Index), Preset.Skills[Index].Branch});
    }
    // Ranks beyond the level's skill points come from the DEV extra points.
    Build.DevSkillPoints = FMath::Max(0, -LDProgression::SkillPoints(Content, Build));
    if (Preset.Stat)
    {
        const int32 StatIndex = Content.StatDesignIndex(Preset.Stat);
        if (StatIndex == INDEX_NONE)
        {
            Error = FString::Printf(TEXT("'%s' is not a stat of the approved catalog."), Preset.Stat);
            return false;
        }
        Build.StatPoints[StatIndex] = Preset.StatPoints;
    }
    for (const FLDBreakoutStat& Row : Preset.Allocation)
    {
        const int32 StatIndex = Content.StatDesignIndex(Row.Stat);
        if (StatIndex == INDEX_NONE || Row.Points < 0)
        {
            Error = FString::Printf(TEXT("'%s' is not a stat of the approved catalog."), Row.Stat);
            return false;
        }
        Build.StatPoints[StatIndex] += Row.Points;
    }
    if (const ELDGrowthReason Reason = LDProgression::ValidateBuild(Content, Build); Reason != ELDGrowthReason::None)
    {
        Error = FString::Printf(TEXT("The preset skills do not form a valid build (%s)."), LDGrowthWire::ReasonName(Reason));
        return false;
    }
    for (const FLDBreakoutCard& Row : Preset.Cards)
    {
        uint16 Serial = 0;
        if (const ELDGrowthReason Reason = LDProgression::ReceiveCard(Content, Build, FName(Row.Card), 0, Serial); Reason != ELDGrowthReason::None)
        {
            Error = FString::Printf(TEXT("'%s' is not a card of the approved catalog (%s)."), Row.Card, LDGrowthWire::ReasonName(Reason));
            return false;
        }
        FLDCardTarget Target;
        Target.Kind = Row.Target;
        if (Row.Target == ELDCardTargetKind::Skill)
        {
            if (!Preset.Skills.IsValidIndex(Row.Skill))
            {
                Error = FString::Printf(TEXT("'%s' names no skill of the preset."), Row.Card);
                return false;
            }
            Target.Active = FName(Preset.Skills[Row.Skill].Ability);
        }
        FLDCardInstance* Card = Build.FindCard(Serial);
        if (const ELDGrowthReason Reason = LDProgression::CardTargetReason(Content, Build, FName(Row.Card), Target);
            !Card || Reason != ELDGrowthReason::None)
        {
            Error = FString::Printf(TEXT("'%s' cannot sit on that target (%s)."), Row.Card, LDGrowthWire::ReasonName(Reason));
            return false;
        }
        Card->Target = Target;
    }
    if (const ELDGrowthReason Reason = LDProgression::ValidateBuild(Content, Build); Reason != ELDGrowthReason::None)
    {
        Error = FString::Printf(TEXT("The preset is not a valid build (%s)."), LDGrowthWire::ReasonName(Reason));
        return false;
    }
    int64 Unspent = 0;
    if (const ELDGrowthReason Reason = LDProgression::UnspentPoints(Content, Build, OutXP, Unspent); Reason != ELDGrowthReason::None)
    {
        Error = FString::Printf(TEXT("The preset's attribute points exceed the level's (%s)."), LDGrowthWire::ReasonName(Reason));
        return false;
    }
    Out = MoveTemp(Build);
    return true;
}
