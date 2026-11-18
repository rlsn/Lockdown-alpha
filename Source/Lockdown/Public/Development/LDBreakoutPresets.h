#pragma once

#include "CoreMinimal.h"
#include "Development/LDProgressionRules.h"

// The P2 breakout samples (spec §7.6): fixed builds that show a card combination turning into something new. They are not game
// content: the DEV panel configures one in a click (LDDevGrowthFixture) and the AI's breakout goal (spec §13) scores a
// sample's cards higher. Every id names approved content; LDBreakoutPresets::Build checks it against the catalog.
struct FLDBreakoutSkill
{
    const TCHAR* Ability = nullptr;
    // The deepest branch path taken (its rank follows: None = 1, A/B = 2, A1..B2 = 3).
    ELDSkillBranch Branch = ELDSkillBranch::None;
};

struct FLDBreakoutCard
{
    const TCHAR* Card = nullptr;
    ELDCardTargetKind Target = ELDCardTargetKind::None;
    // Skill: the index into the preset's skills; ignored otherwise.
    int32 Skill = INDEX_NONE;
};

struct FLDBreakoutPreset
{
    const TCHAR* Id = nullptr;
    // DEV panel label.
    const TCHAR* Label = nullptr;
    TArray<FLDBreakoutSkill> Skills;
    TArray<FLDBreakoutCard> Cards;
    // Allocated attribute points on one stat (None for none).
    const TCHAR* Stat = nullptr;
    int32 StatPoints = 0;
    // A DEV cooldown haste term (P2), 0 for none.
    int64 CooldownHasteP2 = 0;
    // Spec §13: the AI draws its breakout goal among the P2 samples only.
    bool bAIGoal = true;
};

namespace LDBreakoutPresets
{
    // The spec §7.6 samples in spec order: the nine P2 ones, then the P4 minefield.
    LOCKDOWN_API const TArray<FLDBreakoutPreset>& All();
    LOCKDOWN_API const FLDBreakoutPreset* Find(FName Id);
    // Whether Card belongs to Preset (any copy).
    LOCKDOWN_API bool HasCard(const FLDBreakoutPreset& Preset, FName Card);
    // The preset as a max-level build from the match start: its skills learned in Q/W/E order with their branches (DEV extra
    // skill points cover ranks beyond the level's), its stat points allocated, every card received and installed in order.
    // OutXP is the cumulative XP of that level. False with Error when the catalog does not hold a named id or the result is
    // not a valid build.
    LOCKDOWN_API bool Build(const FLDDemoContentDefinitions& Content, const FLDBreakoutPreset& Preset, FLDGrowthBuild& Out,
        int64& OutXP, FString& Error);
}
