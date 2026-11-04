#include "LDDemoContentJson.h"

namespace LDDemoContent
{
namespace
{
struct FNativeId
{
    const TCHAR* Category;
    const TCHAR* Id;
    uint8 Number;
};

#define DEMO_ID(Category, Name, Enum, Value) {TEXT(Category), TEXT(Name), uint8(Enum::Value)}
const FNativeId NativeIds[] =
{
    DEMO_ID("character", "character_demo", ELDDemoCharacterId, CharacterDemo),
    DEMO_ID("abilities", "active_arc_bolt", ELDDemoAbilityId, ActiveArcBolt),
    DEMO_ID("abilities", "active_guard_pulse", ELDDemoAbilityId, ActiveGuardPulse),
    DEMO_ID("abilities", "active_binding_snare", ELDDemoAbilityId, ActiveBindingSnare),
    DEMO_ID("abilities", "passive_battle_rhythm", ELDDemoAbilityId, PassiveBattleRhythm),
    DEMO_ID("abilities", "passive_second_wind", ELDDemoAbilityId, PassiveSecondWind),
    DEMO_ID("abilities", "passive_precision", ELDDemoAbilityId, PassivePrecision),
    DEMO_ID("attributes", "attr_might", ELDDemoAttributeId, AttrMight),
    DEMO_ID("attributes", "attr_focus", ELDDemoAttributeId, AttrFocus),
    DEMO_ID("attributes", "attr_vitality", ELDDemoAttributeId, AttrVitality),
    DEMO_ID("attributes", "attr_resolve", ELDDemoAttributeId, AttrResolve),
    DEMO_ID("attributes", "attr_reach", ELDDemoAttributeId, AttrReach),
    DEMO_ID("equipment", "item_iron_plate", ELDDemoEquipmentId, ItemIronPlate),
    DEMO_ID("equipment", "item_edge_blade", ELDDemoEquipmentId, ItemEdgeBlade),
    DEMO_ID("equipment", "item_runic_focus", ELDDemoEquipmentId, ItemRunicFocus),
    DEMO_ID("equipment", "item_siphon_emblem", ELDDemoEquipmentId, ItemSiphonEmblem),
    DEMO_ID("equipment", "item_phase_anchor", ELDDemoEquipmentId, ItemPhaseAnchor),
    DEMO_ID("equipment", "item_arcane_battery", ELDDemoEquipmentId, ItemArcaneBattery),
    DEMO_ID("equipment", "item_echo_maul", ELDDemoEquipmentId, ItemEchoMaul),
    DEMO_ID("monsters", "monster_scavenger", ELDDemoMonsterId, MonsterScavenger),
    DEMO_ID("monsters", "monster_guardian", ELDDemoMonsterId, MonsterGuardian),
    DEMO_ID("statuses", "root", ELDDemoStatusId, Root),
    DEMO_ID("statuses", "shield", ELDDemoStatusId, Shield),
    DEMO_ID("statuses", "camp_returning", ELDDemoStatusId, CampReturning),
    DEMO_ID("statuses", "respawn_protection", ELDDemoStatusId, RespawnProtection),
    DEMO_ID("effect_ops", "damage", ELDDemoEffectOp, Damage),
    DEMO_ID("effect_ops", "shield", ELDDemoEffectOp, Shield),
    DEMO_ID("effect_ops", "dispel_root", ELDDemoEffectOp, DispelRoot),
    DEMO_ID("effect_ops", "root", ELDDemoEffectOp, Root),
    DEMO_ID("effect_ops", "stat_bonus", ELDDemoEffectOp, StatBonus),
    DEMO_ID("effect_ops", "out_of_combat_regen", ELDDemoEffectOp, OutOfCombatRegen),
    DEMO_ID("effect_ops", "lifesteal", ELDDemoEffectOp, Lifesteal),
    DEMO_ID("effect_ops", "dash", ELDDemoEffectOp, Dash),
    DEMO_ID("effect_ops", "restore_mana", ELDDemoEffectOp, RestoreMana),
    DEMO_ID("effect_ops", "cleave", ELDDemoEffectOp, Cleave),
    DEMO_ID("effect_ops", "dash_boost", ELDDemoEffectOp, DashBoost),
};
#undef DEMO_ID

const FNativeId* Native(const TCHAR* Category, const FString& Id)
{
    for (const auto& Entry : NativeIds)
    {
        if (Id == Entry.Id && FCString::Strcmp(Category, Entry.Category) == 0)
        {
            return &Entry;
        }
    }
    return nullptr;
}

bool NativeIdentity(FFields& Fields, const TCHAR* Category, FString& Id, uint8& Number)
{
    int64 RawNumber;
    if (!Fields.Identifier(TEXT("id"), Id) || !Fields.Int(TEXT("native_id"), RawNumber, 1, 255))
    {
        return false;
    }
    const FNativeId* Entry = Native(Category, Id);
    if (!Fields.Check.Require(Entry && RawNumber == Entry->Number, Fields.Path, TEXT("unknown, retired or renumbered native ID")))
    {
        return false;
    }
    Number = static_cast<uint8>(RawNumber);
    return true;
}

bool NativeMap(const FValue& Value, FCheck& Check)
{
    FFields Root(Check, Value, TEXT("native_ids"));
    int64 Version;
    if (!Root.Int(TEXT("schema_version"), Version, 1, 1))
    {
        return false;
    }
    FFields Retired(Check, Root.Take(TEXT("retired_ranges")), TEXT("native_ids.retired_ranges"));
    for (const auto& Pair : {TPair<const TCHAR*, int64>(TEXT("abilities"), 61), {TEXT("equipment"), 98}})
    {
        const TArray<FValue>* Range;
        int64 First, Last;
        if (!Array(Retired.Take(Pair.Key), Range, Check, Retired.Path, 2, 2)
            || !Integer((*Range)[0], First, Check, Retired.Path, 0, 0)
            || !Integer((*Range)[1], Last, Check, Retired.Path, Pair.Value, Pair.Value))
        {
            return false;
        }
    }
    if (!Retired.Done())
    {
        return false;
    }
    for (const TCHAR* Category : {TEXT("character"), TEXT("abilities"), TEXT("attributes"),
        TEXT("equipment"), TEXT("monsters"), TEXT("statuses"), TEXT("effect_ops")})
    {
        FFields Group(Check, Root.Take(Category), Root.Path + TEXT(".") + Category);
        for (const auto& Entry : NativeIds)
        {
            if (FCString::Strcmp(Category, Entry.Category) == 0)
            {
                int64 Number;
                if (!Group.Int(Entry.Id, Number, Entry.Number, Entry.Number))
                {
                    return false;
                }
            }
        }
        if (!Group.Done())
        {
            return false;
        }
    }
    return Root.Done();
}

bool P2Bounds(FFields& Fields, const TCHAR* Stat, const TCHAR* Unit, FLDDemoP2Bounds& Out)
{
    const FString Prefix = FString(TEXT("STAT.")) + Stat;
    return Fields.Fixed(*(Prefix + TEXT(".Base")), Unit, TEXT("p2"), Out.BaseP2)
        && Fields.Fixed(*(Prefix + TEXT(".PerLevel")), Unit, TEXT("p2"), Out.PerLevelP2)
        && Fields.Fixed(*(Prefix + TEXT(".Min")), Unit, TEXT("p2"), Out.MinP2)
        && Fields.Fixed(*(Prefix + TEXT(".Max")), Unit, TEXT("p2"), Out.MaxP2);
}

bool RateBounds(FFields& Fields, const TCHAR* Stat, const TCHAR* Unit, FLDDemoRateBounds& Out)
{
    const FString Prefix = FString(TEXT("STAT.")) + Stat;
    return Fields.Fraction(*(Prefix + TEXT(".Base")), Unit, Out.Base)
        && Fields.Fraction(*(Prefix + TEXT(".PerLevel")), Unit, Out.PerLevel)
        && Fields.Fraction(*(Prefix + TEXT(".Min")), Unit, Out.Min)
        && Fields.Fraction(*(Prefix + TEXT(".Max")), Unit, Out.Max);
}

bool Parameters(const FValue& Value, FLDDemoContentParameters& P, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content.parameters"));
    if (!P2Bounds(F, TEXT("MaxHP"), TEXT("hp"), P.MaxHP)
        || !P2Bounds(F, TEXT("MaxMP"), TEXT("mp"), P.MaxMP)
        || !P2Bounds(F, TEXT("AD"), TEXT("points"), P.AD)
        || !P2Bounds(F, TEXT("AP"), TEXT("points"), P.AP)
        || !P2Bounds(F, TEXT("Armor"), TEXT("points"), P.Armor)
        || !P2Bounds(F, TEXT("MagicResist"), TEXT("points"), P.MagicResist)
        || !RateBounds(F, TEXT("HPRegen"), TEXT("hp/s"), P.HPRegen)
        || !RateBounds(F, TEXT("MPRegen"), TEXT("mp/s"), P.MPRegen))
    {
        return false;
    }
    struct FRangeField { const TCHAR* Base; const TCHAR* Prefix; const TCHAR* Unit; FLDDemoRationalBounds* Out; };
    for (const auto& Field : {
        FRangeField{TEXT("STAT.BaseAttackSpeed"), TEXT("STAT.AttackSpeed"), TEXT("attacks/s"), &P.AttackSpeed},
        {TEXT("UNIT.MoveSpeedCmPerSec"), TEXT("STAT.MoveSpeed"), TEXT("cm/s"), &P.MoveSpeedCmPerSec},
        {TEXT("UNIT.BasicAttackRangeCm"), TEXT("STAT.AttackRange"), TEXT("cm"), &P.AttackRangeCm}})
    {
        if (!F.Fraction(Field.Base, Field.Unit, Field.Out->Base, true)
            || !F.Fraction(*(FString(Field.Prefix) + TEXT(".Min")), Field.Unit, Field.Out->Min, true)
            || !F.Fraction(*(FString(Field.Prefix) + TEXT(".Max")), Field.Unit, Field.Out->Max, true))
        {
            return false;
        }
    }
    struct FFixedField { const TCHAR* Key; const TCHAR* Unit; const TCHAR* Storage; int64* Out; int64 Min; int64 Max; };
    const FFixedField Fixed[] =
    {
        {TEXT("STAT.AttackSpeedPerLevelRatio"), TEXT("ratio"), TEXT("bp"), &P.AttackSpeedPerLevelBp, 0, 10000},
        {TEXT("STAT.CritChance.Base"), TEXT("ratio"), TEXT("bp"), &P.CritChanceBaseBp, 0, 10000},
        {TEXT("STAT.CritMultiplier"), TEXT("ratio"), TEXT("bp"), &P.CritMultiplierBp, 10000, MAX_int64},
        {TEXT("STAT.CooldownHaste.Base"), TEXT("points"), TEXT("p2"), &P.CooldownHasteBaseP2, 0, MAX_int64},
        {TEXT("STAT.CooldownHaste.Max"), TEXT("points"), TEXT("p2"), &P.CooldownHasteMaxP2, 0, MAX_int64},
        {TEXT("UNIT.BasicAttackWindupSec"), TEXT("ms"), TEXT("ms"), &P.BasicAttackWindupMs, 1, MAX_int64},
        {TEXT("CAPTURE.HoldSec"), TEXT("ms"), TEXT("ms"), &P.CaptureHoldMs, 1, MAX_int64},
        {TEXT("TIMING.DefendRouteBudgetSec"), TEXT("ms"), TEXT("ms"), &P.DefendRouteBudgetMs, 1, MAX_int64},
        {TEXT("MATCH.DevelopmentSec"), TEXT("ms"), TEXT("ms"), &P.DevelopmentMs, 0, MAX_int64},
        {TEXT("LIFE.RespawnSec"), TEXT("ms"), TEXT("ms"), &P.RespawnMs, 1, MAX_int64},
        {TEXT("LIFE.ProtectionSec"), TEXT("ms"), TEXT("ms"), &P.ProtectionMs, 0, MAX_int64},
        {TEXT("REGEN.OutOfCombatDelaySec"), TEXT("ms"), TEXT("ms"), &P.OutOfCombatRegenDelayMs, 1, MAX_int64},
        {TEXT("REGEN.OutOfCombatMaxHPRatioPerSec"), TEXT("ratio"), TEXT("bp"), &P.OutOfCombatMaxHPRegenBp, 0, 10000},
        {TEXT("REGEN.OutOfCombatMaxMPRatioPerSec"), TEXT("ratio"), TEXT("bp"), &P.OutOfCombatMaxMPRegenBp, 0, 10000},
        {TEXT("SHOP.SellRatio"), TEXT("ratio"), TEXT("bp"), &P.SellRatioBp, 0, 10000},
        {TEXT("AI.RetreatHPRatio"), TEXT("ratio"), TEXT("bp"), &P.AI.RetreatHPBp, 0, 10000},
        {TEXT("AI.ResumeFarmHPRatio"), TEXT("ratio"), TEXT("bp"), &P.AI.ResumeFarmHPBp, 0, 10000},
        {TEXT("AI.HighCampEntryHPRatio"), TEXT("ratio"), TEXT("bp"), &P.AI.HighCampEntryHPBp, 0, 10000},
        {TEXT("AI.HighCampReserveHPRatio"), TEXT("ratio"), TEXT("bp"), &P.AI.HighCampReserveHPBp, 0, 10000},
        {TEXT("AI.LowCampReserveHPRatio"), TEXT("ratio"), TEXT("bp"), &P.AI.LowCampReserveHPBp, 0, 10000},
        {TEXT("AI.CaptureReactionBudgetSec"), TEXT("ms"), TEXT("ms"), &P.AI.CaptureReactionBudgetMs, 1, MAX_int64},
        {TEXT("AI.DecisionIntervalSec"), TEXT("ms"), TEXT("ms"), &P.AI.DecisionIntervalMs, 1, MAX_int64},
        {TEXT("AI.RetryPolicy.InitialBackoffSec"), TEXT("ms"), TEXT("ms"), &P.AI.RetryInitialBackoffMs, 1, MAX_int64},
        {TEXT("AI.RetryPolicy.MaxBackoffSec"), TEXT("ms"), TEXT("ms"), &P.AI.RetryMaxBackoffMs, 1, MAX_int64},
        {TEXT("AI.ExploreRevisitIntervalSec"), TEXT("ms"), TEXT("ms"), &P.AI.ExploreRevisitIntervalMs, 1, MAX_int64},
        {TEXT("AI.EngagementBudgetSec"), TEXT("ms"), TEXT("ms"), &P.AI.EngagementBudgetMs, 1, MAX_int64},
    };
    for (const auto& Field : Fixed)
    {
        if (!F.Fixed(Field.Key, Field.Unit, Field.Storage, *Field.Out, Field.Min, Field.Max))
        {
            return false;
        }
    }
    struct FCmField { const TCHAR* Key; FLDDemoRational* Out; };
    for (const auto& Field : {
        FCmField{TEXT("UNIT.CollisionRadiusCm"), &P.CollisionRadiusCm},
        {TEXT("VISION.RadiusCm"), &P.VisionRadiusCm}, {TEXT("SHOP.InteractRadiusCm"), &P.ShopInteractRadiusCm},
        {TEXT("CAPTURE.RadiusCm"), &P.CaptureRadiusCm}, {TEXT("CAMP.ResetArrivalRadiusCm"), &P.CampResetArrivalRadiusCm}})
    {
        if (!F.Fraction(Field.Key, TEXT("cm"), *Field.Out, true))
        {
            return false;
        }
    }
    struct FCountField { const TCHAR* Key; int64* Out; int64 Min; };
    for (const auto& Field : {
        FCountField{TEXT("SHOP.StartingGold"), &P.StartingGold, 0}, {TEXT("CHOICE.BaseWeight"), &P.ChoiceBaseWeight, 1},
        {TEXT("XP.BaseToNext"), &P.XPBaseToNext, 1}, {TEXT("XP.IncreasePerLevel"), &P.XPIncreasePerLevel, 0},
        {TEXT("REWARD.PlayerXP.Base"), &P.PlayerXPBase, 0}, {TEXT("REWARD.PlayerXP.PerLevel"), &P.PlayerXPPerLevel, 0},
        {TEXT("REWARD.PlayerGold.Base"), &P.PlayerGoldBase, 0}, {TEXT("REWARD.PlayerGold.PerLevel"), &P.PlayerGoldPerLevel, 0},
        {TEXT("AI.RetryPolicy.MaxAttempts"), &P.AI.RetryMaxAttempts, 1}})
    {
        if (!F.Int(Field.Key, *Field.Out, Field.Min))
        {
            return false;
        }
    }
    return F.Done();
}

bool Formulas(const FValue& Value, FLDDemoFormulas& Out, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content.formulas"));
    struct FEntry { const TCHAR* Key; const TCHAR* Name; ELDDemoFormula Expected; ELDDemoFormula* Out; };
#define DEMO_FORMULA(Key, Rule) {TEXT(#Key), TEXT(#Rule), ELDDemoFormula::Rule, &Out.Key}
    const FEntry Entries[] =
    {
        DEMO_FORMULA(StatProjection, AddFlatThenAdditivePercent),
        DEMO_FORMULA(Resistance, PointsOverScalePlusPoints), DEMO_FORMULA(Crit, OncePerPrimaryAttack),
        DEMO_FORMULA(Damage, FloorP2ThenShieldThenHP), DEMO_FORMULA(AttackSpeed, BaseTimesAdditiveRatio),
        DEMO_FORMULA(MoveSpeed, BaseTimesAdditiveRatio), DEMO_FORMULA(AttackRange, BasePlusFlat),
        DEMO_FORMULA(AttackMoveAcquireRadius, MinVisionTwiceAttackRange),
        DEMO_FORMULA(AttackIntervalMs, CeilReciprocalMs), DEMO_FORMULA(AttackWindupMs, CeilBaseWindupTimesBaseOverCurrentAS),
        DEMO_FORMULA(CooldownMs, CeilBaseTimesScaleOverScalePlusHaste), DEMO_FORMULA(Regen, CarrySubP2Remainder),
        DEMO_FORMULA(ResourceMaxChange, FloorFinalRatioRejectAliveUnderflow),
        DEMO_FORMULA(SellGold, FloorPurchasePriceTimesSellRatio), DEMO_FORMULA(XPToNext, LinearIncrement),
    };
#undef DEMO_FORMULA
    for (const auto& Entry : Entries)
    {
        if (!F.Literal(Entry.Key, Entry.Name))
        {
            return false;
        }
        *Entry.Out = Entry.Expected;
    }
    return F.Fixed(TEXT("ResistanceScale"), TEXT("points"), TEXT("p2"), Out.ResistanceScaleP2, 1)
        && F.Fixed(TEXT("HasteScale"), TEXT("points"), TEXT("p2"), Out.HasteScaleP2, 1) && F.Done();
}

bool Policies(const FValue& Value, FLDDemoPolicies& Out, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content.policies"));
    struct FEntry { const TCHAR* Key; const TCHAR* Name; ELDDemoPolicy Expected; ELDDemoPolicy* Out; };
#define DEMO_POLICY(Key, Rule) {TEXT(#Key), TEXT(#Rule), ELDDemoPolicy::Rule, &Out.Key}
    const FEntry Entries[] =
    {
        DEMO_POLICY(StatStacking, AddFlatThenAdditivePercent), DEMO_POLICY(DuplicateEquipment, Allowed),
        DEMO_POLICY(DuplicateEquipmentPassive, OncePerFamily), DEMO_POLICY(DuplicateEquipmentActive, IndependentSources),
        DEMO_POLICY(MissingSourceCooldown, ContinueServerTime), DEMO_POLICY(DeadChoiceCommit, Allowed),
        DEMO_POLICY(ResourceMaxChange, PreserveRatio), DEMO_POLICY(ReleasedEffectSourceLoss, ContinueForRemainingContender),
        DEMO_POLICY(SourceLeave, CancelUnsettledNoRefund), DEMO_POLICY(InvalidAggressionProtection, BreakOnAggressiveIntent),
        DEMO_POLICY(CritSampling, OncePerAttackId), DEMO_POLICY(LifestealBasis, PrimaryBasicAttackHPLost),
        DEMO_POLICY(LifestealRecursiveTriggers, Disabled), DEMO_POLICY(BasicSecondaryTriggers, Disabled),
        DEMO_POLICY(RegenIntegration, CarrySubP2Remainder), DEMO_POLICY(ShieldPolicy, ReplaceIfNotWeaker),
        DEMO_POLICY(CampAggroTrigger, ConfirmedCombatHit), DEMO_POLICY(CampResetPolicy, ReturnThenReset),
        DEMO_POLICY(CampManaPolicy, NotApplicable),
    };
#undef DEMO_POLICY
    for (const auto& Entry : Entries)
    {
        if (!F.Literal(Entry.Key, Entry.Name))
        {
            return false;
        }
        *Entry.Out = Entry.Expected;
    }
    if (!F.Flag(TEXT("ShopRequiresLineOfSight"), Out.ShopRequiresLineOfSight)
        || !F.Flag(TEXT("CampGroupRetaliation"), Out.CampGroupRetaliation)
        || !F.Flag(TEXT("CampReturningDamageable"), Out.CampReturningDamageable)
        || !F.Flag(TEXT("CampReturningControllable"), Out.CampReturningControllable)
        || !Check.Require(!Out.ShopRequiresLineOfSight && Out.CampGroupRetaliation
            && Out.CampReturningDamageable && Out.CampReturningControllable, F.Path, TEXT("unsupported boolean policy")))
    {
        return false;
    }
    const TArray<FValue>* Blocks;
    FString Move, Dash;
    if (!Array(F.Take(TEXT("RootBlocks")), Blocks, Check, F.Path, 2, 2)
        || !String((*Blocks)[0], Move, Check, F.Path) || !String((*Blocks)[1], Dash, Check, F.Path)
        || !Check.Require(Move.Equals(TEXT("Move"), ESearchCase::CaseSensitive)
            && Dash.Equals(TEXT("Dash"), ESearchCase::CaseSensitive), F.Path, TEXT("unsupported root restrictions")))
    {
        return false;
    }
    Out.RootBlocks = {ELDDemoRootBlock::Move, ELDDemoRootBlock::Dash};
    return F.Done();
}

struct FBonusField
{
    const TCHAR* Name;
    ELDDemoBonusStat Stat;
    const TCHAR* Unit;
    const TCHAR* Storage;
};
const FBonusField BonusFields[] =
{
    {TEXT("ADFlat"), ELDDemoBonusStat::ADFlat, TEXT("points"), TEXT("p2")},
    {TEXT("APFlat"), ELDDemoBonusStat::APFlat, TEXT("points"), TEXT("p2")},
    {TEXT("MaxHPFlat"), ELDDemoBonusStat::MaxHPFlat, TEXT("hp"), TEXT("p2")},
    {TEXT("MaxMPFlat"), ELDDemoBonusStat::MaxMPFlat, TEXT("mp"), TEXT("p2")},
    {TEXT("ArmorFlat"), ELDDemoBonusStat::ArmorFlat, TEXT("points"), TEXT("p2")},
    {TEXT("MagicResistFlat"), ELDDemoBonusStat::MagicResistFlat, TEXT("points"), TEXT("p2")},
    {TEXT("CooldownHasteFlat"), ELDDemoBonusStat::CooldownHasteFlat, TEXT("points"), TEXT("p2")},
    {TEXT("AttackRangeCmFlat"), ELDDemoBonusStat::AttackRangeCmFlat, TEXT("cm"), nullptr},
    {TEXT("AttackSpeedBonusRatio"), ELDDemoBonusStat::AttackSpeedBonusRatio, TEXT("ratio"), TEXT("bp")},
    {TEXT("MoveSpeedBonusRatio"), ELDDemoBonusStat::MoveSpeedBonusRatio, TEXT("ratio"), TEXT("bp")},
    {TEXT("CritChanceBonusRatio"), ELDDemoBonusStat::CritChanceBonusRatio, TEXT("ratio"), TEXT("bp")},
};

constexpr uint32 Bit(ELDDemoBonusStat Stat) { return 1u << uint8(Stat); }
constexpr uint32 OpBit(ELDDemoEffectOp Op) { return 1u << uint8(Op); }

uint32 StatMask(const TArray<FLDDemoStatBonus>& Stats)
{
    uint32 Result = 0;
    for (const auto& Stat : Stats)
    {
        Result |= Bit(Stat.Stat);
    }
    return Result;
}

bool Stats(const FValue& Value, TArray<FLDDemoStatBonus>& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    if (!Check.Good || !Check.Require(!F.Object->Values.IsEmpty(), Path, TEXT("empty stat definition")))
    {
        return false;
    }
    TArray<FString> Keys;
    for (const auto& Pair : F.Object->Values)
    {
        Keys.Emplace(Pair.Key.ToView());
    }
    Keys.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
    for (const FString& Key : Keys)
    {
        const FBonusField* Field = nullptr;
        for (const auto& Candidate : BonusFields)
        {
            if (Key.Equals(Candidate.Name, ESearchCase::CaseSensitive))
            {
                Field = &Candidate;
                break;
            }
        }
        if (!Check.Require(Field != nullptr, Path + TEXT(".") + Key, TEXT("unknown stat")))
        {
            return false;
        }
        FLDDemoStatBonus Bonus;
        Bonus.Stat = Field->Stat;
        if (Field->Storage)
        {
            const int64 Max = FCString::Strcmp(Field->Storage, TEXT("bp")) == 0 ? 10000 : MAX_int64;
            if (!F.Fixed(Field->Name, Field->Unit, Field->Storage, Bonus.Amount, 1, Max))
            {
                return false;
            }
        }
        else if (!F.Fraction(Field->Name, Field->Unit, Bonus.Cm, true))
        {
            return false;
        }
        Out.Add(MoveTemp(Bonus));
    }
    return F.Done();
}

bool Target(const FValue& Value, FLDDemoTargetDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Mode;
    if (!F.Text(TEXT("mode"), Mode))
    {
        return false;
    }
    if (Mode.Equals(TEXT("Self"), ESearchCase::CaseSensitive))
    {
        Out.Mode = ELDDemoTargetMode::Self;
    }
    else if (Mode.Equals(TEXT("DirectionDash"), ESearchCase::CaseSensitive))
    {
        Out.Mode = ELDDemoTargetMode::DirectionDash;
    }
    else if (Mode.Equals(TEXT("Unit"), ESearchCase::CaseSensitive) || Mode.Equals(TEXT("DirectionProjectile"), ESearchCase::CaseSensitive))
    {
        Out.Mode = Mode.Equals(TEXT("Unit"), ESearchCase::CaseSensitive) ? ELDDemoTargetMode::Unit : ELDDemoTargetMode::DirectionProjectile;
        if (!F.Fraction(TEXT("range"), TEXT("cm"), Out.RangeCm, true))
        {
            return false;
        }
        if (Out.Mode == ELDDemoTargetMode::DirectionProjectile
            && (!F.Fraction(TEXT("projectile_radius"), TEXT("cm"), Out.ProjectileRadiusCm, true)
                || !F.Fraction(TEXT("projectile_speed"), TEXT("cm/s"), Out.ProjectileSpeedCmPerSec, true)))
        {
            return false;
        }
    }
    else
    {
        return Check.Require(false, Path, TEXT("unsupported target mode"));
    }
    return F.Done();
}

bool Effect(const FValue& Value, FLDDemoEffectDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Op;
    int64 Opcode;
    if (!F.Identifier(TEXT("op"), Op) || !F.Int(TEXT("opcode"), Opcode, 1, 255))
    {
        return false;
    }
    const FNativeId* Entry = Native(TEXT("effect_ops"), Op);
    if (!Check.Require(Entry && Entry->Number == Opcode, Path, TEXT("unsupported or mismatched effect opcode")))
    {
        return false;
    }
    Out.Opcode = static_cast<uint8>(Opcode);
    switch (Out.GetOp())
    {
    case ELDDemoEffectOp::Damage:
    case ELDDemoEffectOp::Shield:
    {
        const bool Damage = Out.GetOp() == ELDDemoEffectOp::Damage;
        if (!F.Fixed(TEXT("base"), Damage ? TEXT("points") : TEXT("hp"), TEXT("p2"), Out.BaseP2)
            || !F.Fixed(TEXT("ap_ratio"), TEXT("ratio"), TEXT("bp"), Out.APRatioBp))
        {
            return false;
        }
        if (Damage)
        {
            FString Type;
            if (!F.Fixed(TEXT("ad_ratio"), TEXT("ratio"), TEXT("bp"), Out.ADRatioBp)
                || !F.Text(TEXT("damage_type"), Type)
                || !Check.Require(Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive)
                    || Type.Equals(TEXT("Magical"), ESearchCase::CaseSensitive), Path, TEXT("unknown damage type")))
            {
                return false;
            }
            Out.DamageType = Type.Equals(TEXT("Physical"), ESearchCase::CaseSensitive) ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical;
        }
        else if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        if (!Check.Require(Out.BaseP2 > 0 || Out.APRatioBp > 0 || Out.ADRatioBp > 0, Path, TEXT("ineffective effect")))
        {
            return false;
        }
        break;
    }
    case ELDDemoEffectOp::DispelRoot:
        break;
    case ELDDemoEffectOp::Root:
        if (!F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::StatBonus:
        if (!Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats")))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::OutOfCombatRegen:
        if (!F.Fixed(TEXT("delay"), TEXT("ms"), TEXT("ms"), Out.DelayMs, 1)
            || !F.Fraction(TEXT("rate"), TEXT("hp/s"), Out.HPPerSec, true))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Lifesteal:
    case ELDDemoEffectOp::RestoreMana:
    case ELDDemoEffectOp::Cleave:
        if (!F.Fixed(TEXT("ratio"), TEXT("ratio"), TEXT("bp"), Out.RatioBp, 1, 10000))
        {
            return false;
        }
        if (Out.GetOp() == ELDDemoEffectOp::Cleave && !F.Fraction(TEXT("radius"), TEXT("cm"), Out.RadiusCm, true))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::Dash:
        if (!F.Fraction(TEXT("distance"), TEXT("cm"), Out.DistanceCm, true)
            || !F.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), Out.DurationMs, 1))
        {
            return false;
        }
        break;
    case ELDDemoEffectOp::DashBoost:
        if (!F.Fixed(TEXT("cooldown_ratio"), TEXT("ratio"), TEXT("bp"), Out.CooldownRatioBp, 1, 10000)
            || !F.Fixed(TEXT("distance_bonus"), TEXT("ratio"), TEXT("bp"), Out.DistanceBonusBp)
            || !Check.Require(Out.CooldownRatioBp < 10000 || Out.DistanceBonusBp > 0, Path, TEXT("ineffective dash boost")))
        {
            return false;
        }
        break;
    default:
        return Check.Require(false, Path, TEXT("unimplemented effect projection"));
    }
    return F.Done();
}

bool Effects(const FValue& Value, TArray<FLDDemoEffectDefinition>& Out, uint32 Expected,
    FCheck& Check, const FString& Path)
{
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, Path, 1, 4))
    {
        return false;
    }
    uint32 Mask = 0;
    for (int32 I = 0; I < Rows->Num(); ++I)
    {
        FLDDemoEffectDefinition Definition;
        if (!Effect((*Rows)[I], Definition, Check, FString::Printf(TEXT("%s[%d]"), *Path, I)))
        {
            return false;
        }
        const uint32 B = OpBit(Definition.GetOp());
        if (!Check.Require(!(Mask & B), Path, TEXT("duplicate effect operation")))
        {
            return false;
        }
        Mask |= B;
        Out.Add(MoveTemp(Definition));
    }
    return Check.Require(Mask == Expected, Path, TEXT("missing or extra effect; definition/target projection mismatch"));
}

bool Rank(const FValue& Value, FLDDemoRankDefinition& Out, int32 ExpectedRank, bool Active,
    uint32 ExpectedOps, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    if (!F.SmallInt(TEXT("rank"), Out.Rank, ExpectedRank, ExpectedRank))
    {
        return false;
    }
    if (Active && (!F.Fixed(TEXT("mana_cost"), TEXT("mp"), TEXT("p2"), Out.ManaCostP2)
        || !F.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), Out.CooldownMs, 1)))
    {
        return false;
    }
    return Effects(F.Take(TEXT("effects")), Out.Effects, ExpectedOps, Check, Path + TEXT(".effects")) && F.Done();
}

bool Ability(const FValue& Value, FLDDemoAbilityDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Kind;
    if (!NativeIdentity(F, TEXT("abilities"), Out.Id, Out.NativeId) || !F.Text(TEXT("kind"), Kind))
    {
        return false;
    }
    ELDDemoTargetMode Mode = ELDDemoTargetMode::Self;
    uint32 ExpectedOps = 0, ExpectedStats = 0;
    bool Active = false;
    switch (Out.GetNativeId())
    {
    case ELDDemoAbilityId::ActiveArcBolt:
        Active = true; Mode = ELDDemoTargetMode::DirectionProjectile; ExpectedOps = OpBit(ELDDemoEffectOp::Damage); break;
    case ELDDemoAbilityId::ActiveGuardPulse:
        Active = true; ExpectedOps = OpBit(ELDDemoEffectOp::Shield) | OpBit(ELDDemoEffectOp::DispelRoot); break;
    case ELDDemoAbilityId::ActiveBindingSnare:
        Active = true; Mode = ELDDemoTargetMode::Unit; ExpectedOps = OpBit(ELDDemoEffectOp::Damage) | OpBit(ELDDemoEffectOp::Root); break;
    case ELDDemoAbilityId::PassiveBattleRhythm:
        ExpectedOps = OpBit(ELDDemoEffectOp::StatBonus); ExpectedStats = Bit(ELDDemoBonusStat::AttackSpeedBonusRatio); break;
    case ELDDemoAbilityId::PassiveSecondWind:
        ExpectedOps = OpBit(ELDDemoEffectOp::OutOfCombatRegen); break;
    case ELDDemoAbilityId::PassivePrecision:
        ExpectedOps = OpBit(ELDDemoEffectOp::StatBonus); ExpectedStats = Bit(ELDDemoBonusStat::CritChanceBonusRatio); break;
    default:
        return Check.Require(false, Path, TEXT("unimplemented ability projection"));
    }
    Out.Kind = Active ? ELDDemoAbilityKind::Active : ELDDemoAbilityKind::Passive;
    if (!Check.Require(Kind.Equals(Active ? TEXT("active") : TEXT("passive"), ESearchCase::CaseSensitive), Path, TEXT("wrong ability kind"))
        || !Target(F.Take(TEXT("target")), Out.Target, Check, Path + TEXT(".target"))
        || !Check.Require(Out.Target.Mode == Mode, Path, TEXT("wrong definition target")))
    {
        return false;
    }
    if (Active && (!F.Fixed(TEXT("windup"), TEXT("ms"), TEXT("ms"), Out.WindupMs)
        || !F.Identifier(TEXT("cooldown_family"), Out.CooldownFamily)
        || !Check.Require(Out.CooldownFamily == TEXT("cd_") + Out.Id.Mid(7), Path, TEXT("wrong cooldown family"))))
    {
        return false;
    }
    const TArray<FValue>* Rows;
    if (!Array(F.Take(TEXT("ranks")), Rows, Check, Path, 3, 3))
    {
        return false;
    }
    for (int32 I = 0; I < Rows->Num(); ++I)
    {
        FLDDemoRankDefinition Definition;
        if (!Rank((*Rows)[I], Definition, I + 1, Active, ExpectedOps, Check, FString::Printf(TEXT("%s.ranks[%d]"), *Path, I)))
        {
            return false;
        }
        if (ExpectedStats && !Check.Require(StatMask(Definition.Effects[0].Stats) == ExpectedStats,
            Path, TEXT("wrong passive stat definition")))
        {
            return false;
        }
        Out.Ranks.Add(MoveTemp(Definition));
    }
    return F.Done();
}

bool Attribute(const FValue& Value, FLDDemoAttributeDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    if (!NativeIdentity(F, TEXT("attributes"), Out.Id, Out.NativeId)
        || !Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats")) || !F.Done())
    {
        return false;
    }
    uint32 Expected = 0;
    switch (static_cast<ELDDemoAttributeId>(Out.NativeId))
    {
    case ELDDemoAttributeId::AttrMight: Expected = Bit(ELDDemoBonusStat::ADFlat); break;
    case ELDDemoAttributeId::AttrFocus: Expected = Bit(ELDDemoBonusStat::APFlat); break;
    case ELDDemoAttributeId::AttrVitality: Expected = Bit(ELDDemoBonusStat::MaxHPFlat); break;
    case ELDDemoAttributeId::AttrResolve: Expected = Bit(ELDDemoBonusStat::ArmorFlat) | Bit(ELDDemoBonusStat::MagicResistFlat); break;
    case ELDDemoAttributeId::AttrReach: Expected = Bit(ELDDemoBonusStat::AttackRangeCmFlat); break;
    default: return Check.Require(false, Path, TEXT("unimplemented attribute projection"));
    }
    return Check.Require(StatMask(Out.Stats) == Expected, Path, TEXT("wrong attribute stats"));
}

bool Equipment(const FValue& Value, FLDDemoEquipmentDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    if (!NativeIdentity(F, TEXT("equipment"), Out.Id, Out.NativeId)
        || !F.Int(TEXT("buy_price"), Out.BuyPrice, 1) || !F.Int(TEXT("sell_gold"), Out.SellGold)
        || !Stats(F.Take(TEXT("stats")), Out.Stats, Check, Path + TEXT(".stats")))
    {
        return false;
    }
    uint32 ExpectedStats = 0, ActiveOps = 0, PassiveOps = 0;
    ELDDemoTargetMode Mode = ELDDemoTargetMode::Self;
    const TCHAR* ActiveFamily = nullptr;
    const TCHAR* PassiveFamily = nullptr;
    switch (Out.GetNativeId())
    {
    case ELDDemoEquipmentId::ItemIronPlate:
        ExpectedStats = Bit(ELDDemoBonusStat::MaxHPFlat) | Bit(ELDDemoBonusStat::ArmorFlat); break;
    case ELDDemoEquipmentId::ItemEdgeBlade:
        ExpectedStats = Bit(ELDDemoBonusStat::ADFlat) | Bit(ELDDemoBonusStat::AttackSpeedBonusRatio); break;
    case ELDDemoEquipmentId::ItemRunicFocus:
        ExpectedStats = Bit(ELDDemoBonusStat::APFlat) | Bit(ELDDemoBonusStat::CooldownHasteFlat); break;
    case ELDDemoEquipmentId::ItemSiphonEmblem:
        ExpectedStats = Bit(ELDDemoBonusStat::ADFlat) | Bit(ELDDemoBonusStat::MagicResistFlat);
        PassiveFamily = TEXT("passive_siphon"); PassiveOps = OpBit(ELDDemoEffectOp::Lifesteal); break;
    case ELDDemoEquipmentId::ItemPhaseAnchor:
        ExpectedStats = Bit(ELDDemoBonusStat::MoveSpeedBonusRatio);
        PassiveFamily = TEXT("passive_stride"); PassiveOps = OpBit(ELDDemoEffectOp::DashBoost); break;
    case ELDDemoEquipmentId::ItemArcaneBattery:
        ExpectedStats = Bit(ELDDemoBonusStat::MaxMPFlat) | Bit(ELDDemoBonusStat::MagicResistFlat);
        ActiveFamily = TEXT("cd_arcane_battery"); ActiveOps = OpBit(ELDDemoEffectOp::RestoreMana) | OpBit(ELDDemoEffectOp::Shield); break;
    case ELDDemoEquipmentId::ItemEchoMaul:
        ExpectedStats = Bit(ELDDemoBonusStat::ADFlat) | Bit(ELDDemoBonusStat::AttackRangeCmFlat);
        PassiveFamily = TEXT("passive_cleave"); PassiveOps = OpBit(ELDDemoEffectOp::Cleave); break;
    default:
        return Check.Require(false, Path, TEXT("unimplemented equipment projection"));
    }
    if (!Check.Require(StatMask(Out.Stats) == ExpectedStats, Path, TEXT("wrong equipment stats")))
    {
        return false;
    }
    const FValue Active = F.Take(TEXT("active"));
    const FValue Passive = F.Take(TEXT("passive"));
    if (!Check.Good)
    {
        return false;
    }
    Out.HasActive = Active->Type != EJson::Null;
    Out.HasPassive = Passive->Type != EJson::Null;
    if (!Check.Require(Out.HasActive == (ActiveFamily != nullptr) && Out.HasPassive == (PassiveFamily != nullptr),
        Path, TEXT("wrong active/passive presence")))
    {
        return false;
    }
    if (Out.HasActive)
    {
        FFields A(Check, Active, Path + TEXT(".active"));
        if (!Target(A.Take(TEXT("target")), Out.Active.Target, Check, A.Path + TEXT(".target"))
            || !Check.Require(Out.Active.Target.Mode == Mode, A.Path, TEXT("wrong active target"))
            || !A.Fixed(TEXT("windup"), TEXT("ms"), TEXT("ms"), Out.Active.WindupMs)
            || !A.Identifier(TEXT("cooldown_family"), Out.Active.CooldownFamily)
            || !Check.Require(Out.Active.CooldownFamily == ActiveFamily, A.Path, TEXT("wrong cooldown family"))
            || !Rank(A.Take(TEXT("rank")), Out.Active.Rank, 1, true, ActiveOps, Check, A.Path + TEXT(".rank")) || !A.Done())
        {
            return false;
        }
    }
    if (Out.HasPassive)
    {
        FFields P(Check, Passive, Path + TEXT(".passive"));
        if (!P.Identifier(TEXT("family"), Out.Passive.Family)
            || !Check.Require(Out.Passive.Family == PassiveFamily, P.Path, TEXT("wrong passive family"))
            || !Effects(P.Take(TEXT("effects")), Out.Passive.Effects, PassiveOps, Check, P.Path + TEXT(".effects")) || !P.Done())
        {
            return false;
        }
    }
    return F.Done();
}

bool Monster(const FValue& Value, FLDDemoMonsterDefinition& Out, FCheck& Check, const FString& Path)
{
    FFields F(Check, Value, Path);
    FString Tier;
    if (!NativeIdentity(F, TEXT("monsters"), Out.Id, Out.NativeId) || !F.Text(TEXT("tier"), Tier)
        || !Check.Require(Tier.Equals(TEXT("Low"), ESearchCase::CaseSensitive)
            || Tier.Equals(TEXT("High"), ESearchCase::CaseSensitive), Path, TEXT("unknown camp tier")))
    {
        return false;
    }
    Out.Tier = Tier.Equals(TEXT("Low"), ESearchCase::CaseSensitive) ? ELDDemoCampTier::Low : ELDDemoCampTier::High;
    return F.Int(TEXT("count_per_camp"), Out.CountPerCamp, 1)
        && F.Fixed(TEXT("hp"), TEXT("hp"), TEXT("p2"), Out.HPP2, 1)
        && F.Fixed(TEXT("armor"), TEXT("points"), TEXT("p2"), Out.ArmorP2)
        && F.Fixed(TEXT("magic_resist"), TEXT("points"), TEXT("p2"), Out.MagicResistP2)
        && F.Fixed(TEXT("attack"), TEXT("points"), TEXT("p2"), Out.AttackP2, 1)
        && F.Fraction(TEXT("attack_speed"), TEXT("attacks/s"), Out.AttackSpeed, true)
        && F.Fixed(TEXT("attack_windup"), TEXT("ms"), TEXT("ms"), Out.AttackWindupMs, 1)
        && F.Int(TEXT("attack_interval_ms"), Out.AttackIntervalMs, 1)
        && F.Fraction(TEXT("attack_range"), TEXT("cm"), Out.AttackRangeCm, true)
        && F.Fraction(TEXT("move_speed"), TEXT("cm/s"), Out.MoveSpeedCmPerSec, true)
        && F.Fraction(TEXT("collision_radius"), TEXT("cm"), Out.CollisionRadiusCm, true)
        && F.Fraction(TEXT("leash_radius"), TEXT("cm"), Out.LeashRadiusCm, true)
        && F.Fraction(TEXT("perception_radius"), TEXT("cm"), Out.PerceptionRadiusCm, true)
        && F.Fixed(TEXT("lose_target_grace"), TEXT("ms"), TEXT("ms"), Out.LoseTargetGraceMs, 1)
        && F.Fixed(TEXT("respawn"), TEXT("ms"), TEXT("ms"), Out.RespawnMs, 1)
        && F.Int(TEXT("reward_xp"), Out.RewardXP) && F.Int(TEXT("reward_gold"), Out.RewardGold)
        && F.Fraction(TEXT("hp_regen"), TEXT("hp/s"), Out.HPRegenPerSec)
        && F.Fixed(TEXT("max_mp"), TEXT("mp"), TEXT("p2"), Out.MaxMPP2, 0, 0)
        && F.Fraction(TEXT("mp_regen"), TEXT("mp/s"), Out.MPRegenPerSec)
        && F.Fixed(TEXT("ap"), TEXT("points"), TEXT("p2"), Out.APP2, 0, 0)
        && F.Fixed(TEXT("crit_chance"), TEXT("ratio"), TEXT("bp"), Out.CritChanceBp, 0, 0)
        && Check.Require(Out.HPRegenPerSec.Numerator == 0 && Out.MPRegenPerSec.Numerator == 0,
            Path, TEXT("unsupported monster regeneration")) && F.Done();
}

// Mirrors DemoContent.display_text: invisible, control, bidirectional and astral characters are
// refused, braces are reserved by FText formatting, and description numbers come from typed fields.
bool ValidateDisplayText(const FString& Text, bool bAllowDigits, FCheck& Check, const FString& Path)
{
    for (const TCHAR C : Text)
    {
        const uint32 Code = static_cast<uint32>(C);
        const bool bForbidden = Code < 0x20 || (Code >= 0x7f && Code <= 0x9f) || (Code >= 0x200b && Code <= 0x200f)
            || (Code >= 0x2028 && Code <= 0x202e) || (Code >= 0x2060 && Code <= 0x206f) || Code == 0xfeff
            || Code == TEXT('{') || Code == TEXT('}') || (Code >= 0xd800 && Code <= 0xdfff);
        if (!Check.Require(!bForbidden, Path, TEXT("display text contains a forbidden character")))
        {
            return false;
        }
    }
    const auto IsEdgeSpace = [](TCHAR C) { return C == TEXT(' ') || static_cast<uint32>(C) == 0x3000; };
    if (!Check.Require(!Text.IsEmpty() && !IsEdgeSpace(Text[0]) && !IsEdgeSpace(Text[Text.Len() - 1]),
        Path, TEXT("display text has leading or trailing space")))
    {
        return false;
    }
    for (const TCHAR C : Text)
    {
        const uint32 Code = static_cast<uint32>(C);
        const bool bDigit = (Code >= TEXT('0') && Code <= TEXT('9')) || (Code >= 0xff10 && Code <= 0xff19);
        if (!Check.Require(bAllowDigits || !bDigit, Path,
            TEXT("display description contains digits; numbers come from typed fields")))
        {
            return false;
        }
    }
    return true;
}

template <typename T, typename ParseRow>
bool Definitions(const FValue& Value, TArray<T>& Out, FCheck& Check, const FString& Path,
    int32 Min, int32 Max, ParseRow Parse)
{
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, Path, Min, Max))
    {
        return false;
    }
    FString Previous;
    for (int32 I = 0; I < Rows->Num(); ++I)
    {
        T Row;
        if (!Parse((*Rows)[I], Row, Check, FString::Printf(TEXT("%s[%d]"), *Path, I))
            || !Check.Require(Previous.IsEmpty() || Previous < Row.Id, Path, TEXT("duplicate or unsorted definition ID")))
        {
            return false;
        }
        Previous = Row.Id;
        Out.Add(MoveTemp(Row));
    }
    return true;
}

bool Ids(const FValue& Value, TArray<FString>& Out, FCheck& Check, const FString& Path, int32 Count)
{
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, Path, Count, Count))
    {
        return false;
    }
    for (const auto& Row : *Rows)
    {
        FString Name;
        if (!String(Row, Name, Check, Path, 1, 63) || !Id(Name, Check, Path)
            || !Check.Require(Out.IsEmpty() || Out.Last() < Name, Path, TEXT("duplicate or unsorted ID")))
        {
            return false;
        }
        Out.Add(MoveTemp(Name));
    }
    return true;
}

bool Progression(const FValue& Value, FLDDemoProgressionDefinition& Out, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content.progression"));
    if (!F.SmallInt(TEXT("max_level"), Out.MaxLevel, 10, 10)
        || !F.SmallInt(TEXT("max_choices"), Out.MaxChoices, 10, 10)
        || !F.SmallInt(TEXT("max_skill_rank"), Out.MaxSkillRank, 3, 3)
        || !F.SmallInt(TEXT("active_slots"), Out.ActiveSlots, 2, 2)
        || !F.SmallInt(TEXT("equipment_slots"), Out.EquipmentSlots, 2, 2)
        || !F.Int(TEXT("xp_start"), Out.XPStart, 0, 0))
    {
        return false;
    }
    for (const auto& Pair : {TPair<const TCHAR*, TArray<int64>*>(TEXT("xp_thresholds"), &Out.XPThresholds),
        {TEXT("xp_to_next"), &Out.XPToNext}})
    {
        const int32 Count = Pair.Value == &Out.XPThresholds ? 10 : 9;
        const TArray<FValue>* Rows;
        if (!Array(F.Take(Pair.Key), Rows, Check, F.Path, Count, Count))
        {
            return false;
        }
        for (const auto& Row : *Rows)
        {
            int64 Number;
            if (!Integer(Row, Number, Check, F.Path))
            {
                return false;
            }
            Pair.Value->Add(Number);
        }
    }
    FFields Opening(Check, F.Take(TEXT("opening_offer")), TEXT("content.progression.opening_offer"));
    const TArray<FValue>* OpeningIds;
    if (!Array(Opening.Take(TEXT("attribute_ids")), OpeningIds, Check, Opening.Path, 1, 64))
    {
        return false;
    }
    for (const auto& Row : *OpeningIds)
    {
        FString Name;
        if (!String(Row, Name, Check, Opening.Path, 1, 63) || !Id(Name, Check, Opening.Path)
            || !Check.Require(Out.OpeningAttributeIds.IsEmpty() || Out.OpeningAttributeIds.Last() < Name, Opening.Path,
                TEXT("duplicate or unsorted opening offer attribute")))
        {
            return false;
        }
        Out.OpeningAttributeIds.Add(MoveTemp(Name));
    }
    if (!Opening.SmallInt(TEXT("attribute_count"), Out.OpeningAttributeCount, 0, 3)
        || !Opening.SmallInt(TEXT("passive_count"), Out.OpeningPassiveCount, 0, 3) || !Opening.Done())
    {
        return false;
    }
    return F.Done();
}

bool Content(const FValue& Value, FLDDemoContentDefinitions& Out, FCheck& Check)
{
    FFields F(Check, Value, TEXT("content"));
    FFields Identity(Check, F.Take(TEXT("identity")), TEXT("content.identity"));
    int64 Revision;
    if (!Identity.Int(TEXT("schema_version"), Revision, 1, 1)
        || !Identity.Int(TEXT("handler_revision"), Revision, 1, 1)
        || !Identity.Int(TEXT("projection_revision"), Revision, 3, 3))
    {
        return false;
    }
    FFields Approval(Check, Identity.Take(TEXT("approval")), TEXT("content.identity.approval"));
    if (!Approval.Text(TEXT("status"), Out.Identity.ApprovalStatus)
        || !Check.Require(Out.Identity.ApprovalStatus.Equals(TEXT("approved"), ESearchCase::CaseSensitive), Approval.Path, TEXT("approval metadata required"))
        || !Approval.Text(TEXT("decision"), Out.Identity.ApprovalDecision)
        || !Check.Require(!Out.Identity.ApprovalDecision.TrimStartAndEnd().IsEmpty(), Approval.Path, TEXT("empty decision reference"))
        || !Approval.Done() || !Identity.Done()
        || !Parameters(F.Take(TEXT("parameters")), Out.Parameters, Check)
        || !Formulas(F.Take(TEXT("formulas")), Out.Formulas, Check)
        || !Policies(F.Take(TEXT("policies")), Out.Policies, Check))
    {
        return false;
    }
    FFields Character(Check, F.Take(TEXT("character")), TEXT("content.character"));
    if (!NativeIdentity(Character, TEXT("character"), Out.Character.Id, Out.Character.NativeId))
    {
        return false;
    }
    FFields Basic(Check, Character.Take(TEXT("basic_attack")), TEXT("content.character.basic_attack"));
    if (!Basic.Literal(TEXT("target_mode"), TEXT("Unit")) || !Basic.Literal(TEXT("damage_type"), TEXT("Physical"))
        || !Basic.Fixed(TEXT("ad_ratio"), TEXT("ratio"), TEXT("bp"), Out.Character.BasicADRatioBp, 10000, 10000)
        || !Basic.Int(TEXT("interval_ms"), Out.Character.BasicIntervalMs, 1) || !Basic.Done())
    {
        return false;
    }
    // D19: the innate dash is projected into the single-rank active shape Combat executes for every source.
    FFields Dash(Check, Character.Take(TEXT("dash")), TEXT("content.character.dash"));
    FLDDemoEquipmentActive& InnateDash = Out.Character.Dash;
    FLDDemoEffectDefinition& DashMove = InnateDash.Rank.Effects.AddDefaulted_GetRef();
    InnateDash.Target.Mode = ELDDemoTargetMode::DirectionDash;
    InnateDash.Rank.Rank = 1;
    DashMove.Opcode = uint8(ELDDemoEffectOp::Dash);
    if (!Dash.Fixed(TEXT("mana_cost"), TEXT("mp"), TEXT("p2"), InnateDash.Rank.ManaCostP2)
        || !Dash.Fixed(TEXT("cooldown"), TEXT("ms"), TEXT("ms"), InnateDash.Rank.CooldownMs, 1)
        || !Dash.Fraction(TEXT("distance"), TEXT("cm"), DashMove.DistanceCm, true)
        || !Dash.Fixed(TEXT("duration"), TEXT("ms"), TEXT("ms"), DashMove.DurationMs, 1)
        || !Check.Require(DashMove.DurationMs < InnateDash.Rank.CooldownMs, Dash.Path, TEXT("dash outlasts its cooldown"))
        || !Dash.Done() || !Character.Done()
        || !Definitions(F.Take(TEXT("abilities")), Out.Abilities, Check, TEXT("content.abilities"), 1, 6, Ability)
        || !Definitions(F.Take(TEXT("attributes")), Out.Attributes, Check, TEXT("content.attributes"), 3, 5, Attribute)
        || !Definitions(F.Take(TEXT("equipment")), Out.Equipment, Check, TEXT("content.equipment"), 1, 7, Equipment)
        || !Progression(F.Take(TEXT("progression")), Out.Progression, Check))
    {
        return false;
    }
    FFields Camps(Check, F.Take(TEXT("camps")), TEXT("content.camps"));
    if (!Definitions(Camps.Take(TEXT("monsters")), Out.Monsters, Check, TEXT("content.camps.monsters"), 2, 2, Monster))
    {
        return false;
    }
    const TArray<FValue>* Placements;
    if (!Array(Camps.Take(TEXT("placements")), Placements, Check, Camps.Path, 5, 5))
    {
        return false;
    }
    for (const auto& ValueRow : *Placements)
    {
        FFields Row(Check, ValueRow, TEXT("content.camps.placements"));
        FLDDemoCampPlacement Placement;
        if (!Row.Identifier(TEXT("anchor_id"), Placement.AnchorId) || !Row.Identifier(TEXT("monster_id"), Placement.MonsterId)
            || !Row.Done() || !Check.Require(Out.Camps.IsEmpty() || Out.Camps.Last().AnchorId < Placement.AnchorId,
                Row.Path, TEXT("duplicate or unsorted camp placement")))
        {
            return false;
        }
        Out.Camps.Add(MoveTemp(Placement));
    }
    if (!Camps.Done())
    {
        return false;
    }
    FFields World(Check, F.Take(TEXT("world")), TEXT("content.world"));
    if (!World.Identifier(TEXT("map_id"), Out.World.MapId)
        || !Ids(World.Take(TEXT("spawn_ids")), Out.World.SpawnIds, Check, World.Path + TEXT(".spawn_ids"), 10)
        || !Ids(World.Take(TEXT("camp_ids")), Out.World.CampIds, Check, World.Path + TEXT(".camp_ids"), 5)
        || !World.Identifier(TEXT("shop_id"), Out.World.ShopId) || !World.Identifier(TEXT("capture_id"), Out.World.CaptureId)
        || !World.Done())
    {
        return false;
    }
    TSet<FString> DisplayIds;
    DisplayIds.Add(Out.Character.Id);
    for (const auto& Row : Out.Abilities) { DisplayIds.Add(Row.Id); }
    for (const auto& Row : Out.Attributes) { DisplayIds.Add(Row.Id); }
    for (const auto& Row : Out.Equipment) { DisplayIds.Add(Row.Id); }
    for (const auto& Row : Out.Monsters) { DisplayIds.Add(Row.Id); }
    for (const auto& Entry : NativeIds)
    {
        if (FCString::Strcmp(Entry.Category, TEXT("statuses")) == 0) { DisplayIds.Add(Entry.Id); }
    }
    const FValue Display = F.Take(TEXT("display"));
    TArray<FString> DisplayNames;
    const auto ParseDisplay = [&Out, &DisplayIds, &DisplayNames](const FValue& Row, FLDDemoDisplayDefinition& D, FCheck& C, const FString& Path)
    {
        FFields Fields(C, Row, Path);
        if (!Fields.Identifier(TEXT("id"), D.Id) || !Fields.Text(TEXT("namespace"), D.Namespace)
            || !Fields.Text(TEXT("name_key"), D.NameKey, 1, 128)
            || !Fields.Text(TEXT("name_text"), D.NameText, 1, 32)
            || !ValidateDisplayText(D.NameText, true, C, Path + TEXT(".name_text"))
            || !Fields.Text(TEXT("description_key"), D.DescriptionKey, 1, 128)
            || !Fields.Text(TEXT("description_text"), D.DescriptionText, 1, 160)
            || !ValidateDisplayText(D.DescriptionText, false, C, Path + TEXT(".description_text"))
            || !Fields.Identifier(TEXT("icon_id"), D.IconId)
            || !C.Require(DisplayIds.Contains(D.Id) && D.Namespace.Equals(TEXT("DemoContent"), ESearchCase::CaseSensitive)
                && D.NameKey.Equals(D.Id + TEXT(".name"), ESearchCase::CaseSensitive)
                && D.DescriptionKey.Equals(D.Id + TEXT(".description"), ESearchCase::CaseSensitive)
                && D.IconId == TEXT("icon_") + D.Id, Path, TEXT("unknown or noncanonical display reference"))
            || !C.Require(!DisplayNames.ContainsByPredicate([&D](const FString& Name)
                { return Name.Equals(D.NameText, ESearchCase::CaseSensitive); }), Path, TEXT("duplicate display name")))
        {
            return false;
        }
        DisplayNames.Add(D.NameText);
        if (D.Id == Out.Character.Id
            && (!Fields.Identifier(TEXT("portrait_icon_id"), D.PortraitIconId)
                || !C.Require(D.PortraitIconId == TEXT("portrait_") + D.Id, Path, TEXT("wrong portrait reference"))))
        {
            return false;
        }
        return Fields.Done();
    };
    TArray<uint8> DisplayBytes;
    FLDDemoContentDigest DisplayDigest;
    return Definitions(Display, Out.Display, Check, TEXT("content.display"), DisplayIds.Num(), DisplayIds.Num(), ParseDisplay)
        && CanonicalBytes(Display, DisplayBytes, Check) && Hash(DisplayBytes, DisplayDigest, Check, TEXT("content.display"))
        && Check.Require(DisplayDigest == Out.Identity.Display, TEXT("content.display"), TEXT("display digest mismatch")) && F.Done();
}

bool ContractBytes(const FLDDemoContentBundle& Bundle, const FLDDemoContentIdentity& Identity, FCheck& Check)
{
    FString DigestBytes;
    for (int32 I = 0; I < 32; ++I)
    {
        if (I) { DigestBytes += TEXT(", "); }
        DigestBytes += FString::Printf(TEXT("0x%02x"), Identity.Root.Bytes[I]);
    }
    const FString ExpectedContent = FString::Printf(
        TEXT("#pragma once\n#define LD_DEMO_CONTENT_SCHEMA_VERSION 1\n#define LD_DEMO_CONTENT_HANDLER_REVISION 1\n")
        TEXT("#define LD_DEMO_CONTENT_PROJECTION_REVISION 3\n#define LD_DEMO_CONTENT_PAYLOAD_SHA256 TEXT(\"%s\")\n")
        TEXT("#define LD_DEMO_CATALOG_SHA256 TEXT(\"%s\")\n#define LD_DEMO_CATALOG_DIGEST_BYTES {%s}\n"),
        *Identity.Content.ToHex(), *Identity.Root.ToHex(), *DigestBytes);
    const FTCHARToUTF8 UTF8(*ExpectedContent);
    if (!Check.Require(Bundle.ContentContract.Num() == UTF8.Length()
        && FMemory::Memcmp(Bundle.ContentContract.GetData(), UTF8.Get(), UTF8.Length()) == 0,
        TEXT("content contract"), TEXT("generated content contract does not match exact root/payload bytes")))
    {
        return false;
    }
    for (uint8 Byte : Bundle.WorldContract)
    {
        if (!Check.Require(Byte < 128, TEXT("world contract"), TEXT("expected ASCII generated contract")))
        {
            return false;
        }
    }
    const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bundle.WorldContract.GetData()), Bundle.WorldContract.Num());
    const FString WorldText(Decoded.Length(), Decoded.Get());
    const FString ExpectedWorld = FString::Printf(
        TEXT("#pragma once\n#define LD_DEMO_WORLD_SCHEMA_VERSION 2\n#define LD_DEMO_WORLD_PROJECTION_REVISION 2\n")
        TEXT("#define LD_DEMO_WORLD_PAYLOAD_SHA256 TEXT(\"%s\")\n"), *Identity.World.ToHex());
    return Check.Require(WorldText.Replace(TEXT("\r\n"), TEXT("\n")).Equals(ExpectedWorld, ESearchCase::CaseSensitive),
        TEXT("world contract"), TEXT("world schema, projection, byte digest or contract shape differs"));
}

bool Manifest(const FLDDemoContentBundle& Bundle, const FLDDemoContentDigest& ExpectedRoot,
    FLDDemoContentIdentity& Out, FCheck& Check)
{
    if (!Hash(Bundle.Manifest, Out.Root, Check, TEXT("manifest"))
        || !Check.Require(Out.Root == ExpectedRoot, TEXT("manifest"), TEXT("current expected root digest mismatch")))
    {
        return false;
    }
    FValue ManifestValue;
    if (!Parse(Bundle.Manifest, ManifestValue, Check, TEXT("manifest")))
    {
        return false;
    }
    FFields Root(Check, ManifestValue, TEXT("manifest"));
    if (!Root.Literal(TEXT("format"), TEXT("lockdown-demo-content"))
        || !Root.SmallInt(TEXT("schema_version"), Out.SchemaVersion, 1, 1)
        || !Root.SmallInt(TEXT("handler_revision"), Out.HandlerRevision, 1, 1)
        || !Root.SmallInt(TEXT("projection_revision"), Out.ProjectionRevision, 3, 3)
        || !Root.Digest(TEXT("author_schema_sha256"), Out.AuthorSchema)
        || !Root.Digest(TEXT("native_ids_sha256"), Out.NativeIds)
        || !Root.Digest(TEXT("native_contract_sha256"), Out.NativeContract)
        || !Root.Digest(TEXT("display_sha256"), Out.Display))
    {
        return false;
    }
    FFields ContentRef(Check, Root.Take(TEXT("content")), TEXT("manifest.content"));
    FFields WorldRef(Check, Root.Take(TEXT("world")), TEXT("manifest.world"));
    if (!ContentRef.Literal(TEXT("file"), TEXT("demo_content.json")) || !ContentRef.Digest(TEXT("sha256"), Out.Content)
        || !ContentRef.Done() || !WorldRef.Literal(TEXT("file"), TEXT("world.json"))
        || !WorldRef.Digest(TEXT("sha256"), Out.World))
    {
        return false;
    }
    FFields Contract(Check, WorldRef.Take(TEXT("contract")), TEXT("manifest.world.contract"));
    if (!Contract.SmallInt(TEXT("schema_version"), Out.WorldSchemaVersion, 2, 2)
        || !Contract.SmallInt(TEXT("projection_revision"), Out.WorldProjectionRevision, 2, 2)
        || !Contract.Digest(TEXT("sha256"), Out.WorldContract) || !Contract.Done() || !WorldRef.Done() || !Root.Done())
    {
        return false;
    }
    struct FChild { const TCHAR* Name; const TArray<uint8>* Bytes; const FLDDemoContentDigest* Digest; };
    for (const auto& Child : {
        FChild{TEXT("content"), &Bundle.Content, &Out.Content}, {TEXT("world"), &Bundle.World, &Out.World},
        {TEXT("author schema"), &Bundle.AuthorSchema, &Out.AuthorSchema}, {TEXT("native IDs"), &Bundle.NativeIds, &Out.NativeIds},
        {TEXT("native contract"), &Bundle.NativeContract, &Out.NativeContract}, {TEXT("world contract"), &Bundle.WorldContract, &Out.WorldContract}})
    {
        FLDDemoContentDigest Digest;
        if (!Hash(*Child.Bytes, Digest, Check, Child.Name)
            || !Check.Require(Digest == *Child.Digest, Child.Name, TEXT("file bytes differ from root manifest")))
        {
            return false;
        }
    }
    return ContractBytes(Bundle, Out, Check);
}
}

bool Project(const FLDDemoContentBundle& Bundle, const FLDDemoContentDigest& ExpectedRoot,
    FLDDemoContentDefinitions& Out, FString& Error)
{
    Error.Reset();
    FCheck Check(Error);
    FLDDemoContentDefinitions Candidate;
    FValue ContentValue, WorldValue, IdValue;
    if (!Manifest(Bundle, ExpectedRoot, Candidate.Identity, Check)
        || !Parse(Bundle.NativeIds, IdValue, Check, TEXT("native_ids"), false) || !NativeMap(IdValue, Check)
        || !Parse(Bundle.Content, ContentValue, Check, TEXT("content"))
        || !Content(ContentValue, Candidate, Check)
        || !ValidateNumbers(Candidate, Check)
        || !Parse(Bundle.World, WorldValue, Check, TEXT("world"))
        || !ValidateWorld(WorldValue, Candidate, Bundle, Check))
    {
        return false;
    }
    Out = MoveTemp(Candidate);
    return true;
}
}
