#include "Combat/LDAttributeProjection.h"

#include "../Development/LDDemoContentMath.h"

namespace LDAttributeProjection
{
namespace
{
using EError = ELDAttributeProjectionError;
using EAttribute = ELDProjectedAttribute;
using LDDemoContent::FBig;
using LDDemoContent::FExact;
using LDDemoContent::FNumbers;

constexpr int32 AttributeCount = static_cast<int32>(EAttribute::Count);

const TCHAR* AttributeName(EAttribute Attribute)
{
    switch (Attribute)
    {
    case EAttribute::MaxHP: return TEXT("MaxHP");
    case EAttribute::MaxMP: return TEXT("MaxMP");
    case EAttribute::AD: return TEXT("AD");
    case EAttribute::AP: return TEXT("AP");
    case EAttribute::Armor: return TEXT("Armor");
    case EAttribute::MagicResist: return TEXT("MagicResist");
    case EAttribute::AttackSpeed: return TEXT("AttackSpeed");
    case EAttribute::MoveSpeed: return TEXT("MoveSpeed");
    case EAttribute::AttackRange: return TEXT("AttackRange");
    case EAttribute::HPRegen: return TEXT("HPRegen");
    case EAttribute::MPRegen: return TEXT("MPRegen");
    case EAttribute::CritChance: return TEXT("CritChance");
    case EAttribute::CooldownHaste: return TEXT("CooldownHaste");
    default: return TEXT("Attribute");
    }
}

bool NonnegativeRational(const FLDDemoRational& Value)
{
    return Value.Numerator >= 0 && Value.Denominator > 0;
}

// Maps one typed bonus to its projected attribute and term kind; false for an unsupported stat.
bool StatTarget(ELDDemoBonusStat Stat, EAttribute& Attribute, bool& bRatio, bool& bBasisPoints)
{
    bRatio = false;
    bBasisPoints = false;
    switch (Stat)
    {
    case ELDDemoBonusStat::ADFlat: Attribute = EAttribute::AD; return true;
    case ELDDemoBonusStat::APFlat: Attribute = EAttribute::AP; return true;
    case ELDDemoBonusStat::MaxHPFlat: Attribute = EAttribute::MaxHP; return true;
    case ELDDemoBonusStat::MaxMPFlat: Attribute = EAttribute::MaxMP; return true;
    case ELDDemoBonusStat::ArmorFlat: Attribute = EAttribute::Armor; return true;
    case ELDDemoBonusStat::MagicResistFlat: Attribute = EAttribute::MagicResist; return true;
    case ELDDemoBonusStat::CooldownHasteFlat: Attribute = EAttribute::CooldownHaste; return true;
    case ELDDemoBonusStat::AttackRangeCmFlat: Attribute = EAttribute::AttackRange; return true;
    case ELDDemoBonusStat::AttackSpeedBonusRatio: Attribute = EAttribute::AttackSpeed; bRatio = bBasisPoints = true; return true;
    case ELDDemoBonusStat::MoveSpeedBonusRatio: Attribute = EAttribute::MoveSpeed; bRatio = bBasisPoints = true; return true;
    case ELDDemoBonusStat::ADBonusRatio: Attribute = EAttribute::AD; bRatio = bBasisPoints = true; return true;
    case ELDDemoBonusStat::CritChanceBonusRatio:
        // Probability points add to the base chance; they are not a percentage of it.
        Attribute = EAttribute::CritChance;
        bBasisPoints = true;
        return true;
    default:
        Attribute = EAttribute::Count;
        return false;
    }
}

struct FTermSum
{
    FExact Flat;
    FExact Ratio;
};

struct FAttributeSum
{
    FExact Base;
    FTermSum Level;
    FTermSum Choices;
    FTermSum Passives;
    FTermSum Equipment;
};

class FProjector
{
public:
    FProjector(const FLDAttributeBuildValues& InBuild, const FLDDemoContentSnapshot& InCatalog)
        : Build(InBuild), Catalog(InCatalog), Content(InCatalog.Get()), Check(MathError), Numbers(Check)
    {
        Sums.SetNum(AttributeCount);
    }

    bool Run(FLDAttributeProjection& Out)
    {
        return ValidateCatalog() && ResolveInputs() && RegisterEquipmentPassives() && Accumulate() && Finish(Out);
    }

    FLDAttributeProjectionFailure Failure;

private:
    bool Refuse(EError Code, const FString& Field, const FString& Message)
    {
        Failure.Code = Code;
        Failure.Field = Field;
        Failure.Message = Message;
        return false;
    }

    bool ArithmeticFailure(const FString& Field)
    {
        return Refuse(EError::Overflow, Field,
            MathError.IsEmpty() ? FString(TEXT("Exact attribute arithmetic failed")) : MathError);
    }

    bool ValidateCatalog()
    {
        const FLDDemoFormulas& Formulas = Content.Formulas;
        const FLDDemoPolicies& Policies = Content.Policies;
        const FLDDemoContentParameters& P = Content.Parameters;
        const FLDDemoProgressionDefinition& Progression = Content.Progression;
        bool bSupported = Formulas.StatProjection == ELDDemoFormula::AddFlatThenAdditivePercent
            && Policies.StatStacking == ELDDemoPolicy::AddFlatThenAdditivePercent
            && Formulas.AttackSpeed == ELDDemoFormula::BaseTimesAdditiveRatio
            && Formulas.MoveSpeed == ELDDemoFormula::BaseTimesAdditiveRatio
            && Formulas.AttackRange == ELDDemoFormula::BasePlusFlat
            && Policies.DuplicateEquipment == ELDDemoPolicy::Allowed
            && Policies.DuplicateEquipmentPassive == ELDDemoPolicy::OncePerFamily
            && Progression.MaxLevel >= 1 && Progression.MaxSkillRank >= 1
            && Progression.MaxChoices >= 0 && Progression.EquipmentSlots >= 0
            && P.AttackSpeedPerLevelBp >= 0 && P.CritChanceBaseBp >= 0
            && P.CooldownHasteBaseP2 >= 0 && P.CooldownHasteMaxP2 >= 0;
        for (const FLDDemoP2Bounds* Bounds : {&P.MaxHP, &P.MaxMP, &P.AD, &P.AP, &P.Armor, &P.MagicResist})
        {
            bSupported &= Bounds->BaseP2 >= 0 && Bounds->PerLevelP2 >= 0 && Bounds->MinP2 <= Bounds->MaxP2;
        }
        for (const FLDDemoRateBounds* Bounds : {&P.HPRegen, &P.MPRegen})
        {
            bSupported &= NonnegativeRational(Bounds->Base) && NonnegativeRational(Bounds->PerLevel)
                && NonnegativeRational(Bounds->Min) && NonnegativeRational(Bounds->Max);
        }
        for (const FLDDemoRationalBounds* Bounds : {&P.AttackSpeed, &P.MoveSpeedCmPerSec, &P.AttackRangeCm})
        {
            bSupported &= NonnegativeRational(Bounds->Base) && NonnegativeRational(Bounds->Min)
                && NonnegativeRational(Bounds->Max);
        }
        return bSupported || Refuse(EError::Catalog, TEXT("Catalog"),
            TEXT("Unsupported attribute projection, stacking, duplicate-equipment or parameter contract"));
    }

    bool ResolveInputs()
    {
        const FLDDemoProgressionDefinition& Progression = Content.Progression;
        if (Build.Level < 1 || int32(Build.Level) > Progression.MaxLevel)
        {
            return Refuse(EError::InputDomain, TEXT("Level"),
                FString::Printf(TEXT("Level %d is outside 1..%d"), int32(Build.Level), Progression.MaxLevel));
        }
        if (Build.EquippedItems.Num() > Progression.EquipmentSlots)
        {
            return Refuse(EError::InputDomain, TEXT("EquippedItems"), FString::Printf(
                TEXT("%d equipped items exceed %d equipment slots"), Build.EquippedItems.Num(), Progression.EquipmentSlots));
        }
        FString LookupError;
        int64 ConsumedChoices = 0;
        for (int32 Index = 0; Index < Build.LearnedPassives.Num(); ++Index)
        {
            const FLDLearnedPassiveValue& Learned = Build.LearnedPassives[Index];
            const FString Field = FString::Printf(TEXT("LearnedPassives[%d]"), Index);
            const FLDDemoAbilityDefinition* Ability = nullptr;
            if (!Catalog.FindAbility(Learned.AbilityId, Ability, LookupError))
            {
                return Refuse(EError::UnknownDefinition, Field, LookupError);
            }
            if (Ability->Kind != ELDDemoAbilityKind::Passive)
            {
                return Refuse(EError::UnknownDefinition, Field, Learned.AbilityId + TEXT(" is not a passive ability"));
            }
            if (Passives.Contains(Ability))
            {
                return Refuse(EError::DuplicateSource, Field, Learned.AbilityId + TEXT(" is listed more than once"));
            }
            if (Learned.Rank < 1 || int32(Learned.Rank) > Progression.MaxSkillRank)
            {
                return Refuse(EError::InputDomain, Field, FString::Printf(
                    TEXT("Rank %d is outside 1..%d"), int32(Learned.Rank), Progression.MaxSkillRank));
            }
            const FLDDemoRankDefinition* Rank = Ability->Ranks.FindByPredicate(
                [&Learned](const FLDDemoRankDefinition& Candidate) { return Candidate.Rank == int32(Learned.Rank); });
            if (Rank == nullptr)
            {
                return Refuse(EError::Catalog, Field, TEXT("The validated catalog has no row for this rank"));
            }
            Passives.Add(Ability);
            PassiveRanks.Add(Rank);
            ConsumedChoices += Learned.Rank;
        }
        for (int32 Index = 0; Index < Build.AttributeCards.Num(); ++Index)
        {
            const FLDAttributeCardValue& Card = Build.AttributeCards[Index];
            const FString Field = FString::Printf(TEXT("AttributeCards[%d]"), Index);
            const FLDDemoAttributeDefinition* Attribute = nullptr;
            if (!Catalog.FindAttribute(Card.AttributeId, Attribute, LookupError))
            {
                return Refuse(EError::UnknownDefinition, Field, LookupError);
            }
            if (Cards.Contains(Attribute))
            {
                return Refuse(EError::DuplicateSource, Field, Card.AttributeId + TEXT(" is listed more than once"));
            }
            if (Card.Count < 1)
            {
                return Refuse(EError::InputDomain, Field, TEXT("An attribute card entry must hold at least one card"));
            }
            Cards.Add(Attribute);
            ConsumedChoices += Card.Count;
        }
        // Each passive rank and each card consumed one choice: the opening one plus one per level-up, so the level bounds
        // them; unseen active choices only tighten this bound.
        const int64 VisibleChoiceBound = FMath::Min<int64>(int64(Build.Level), Progression.MaxChoices);
        if (ConsumedChoices > VisibleChoiceBound)
        {
            return Refuse(EError::InputDomain, TEXT("ChoiceBudget"), FString::Printf(
                TEXT("Passive ranks and attribute cards use %lld choices; level %d allows at most %lld"),
                ConsumedChoices, int32(Build.Level), VisibleChoiceBound));
        }
        for (int32 Index = 0; Index < Build.EquippedItems.Num(); ++Index)
        {
            const FLDDemoEquipmentDefinition* Item = nullptr;
            if (!Catalog.FindEquipment(Build.EquippedItems[Index], Item, LookupError))
            {
                return Refuse(EError::UnknownDefinition, FString::Printf(TEXT("EquippedItems[%d]"), Index), LookupError);
            }
            Items.Add(Item);
        }
        return true;
    }

    bool RegisterEquipmentPassives()
    {
        for (int32 Index = 0; Index < Items.Num(); ++Index)
        {
            const FLDDemoEquipmentDefinition& Item = *Items[Index];
            if (!Item.HasPassive)
            {
                continue;
            }
            const FString Field = FString::Printf(TEXT("EquippedItems[%d]"), Index);
            FLDEquipmentPassiveFamily* Family = Families.FindByPredicate(
                [&Item](const FLDEquipmentPassiveFamily& Existing)
                {
                    return Existing.Family.Equals(Item.Passive.Family, ESearchCase::CaseSensitive);
                });
            if (Family == nullptr)
            {
                for (const FLDDemoEffectDefinition& Effect : Item.Passive.Effects)
                {
                    // Approved equipment passives are combat triggers and contribute no attribute terms.
                    if (Effect.GetOp() != ELDDemoEffectOp::Lifesteal && Effect.GetOp() != ELDDemoEffectOp::Cleave)
                    {
                        return Refuse(EError::Catalog, Field,
                            Item.Id + TEXT(" has an equipment passive effect without an attribute projection rule"));
                    }
                }
                Family = &Families.AddDefaulted_GetRef();
                Family->Family = Item.Passive.Family;
                Family->EquipmentId = Item.Id;
            }
            else if (!Family->EquipmentId.Equals(Item.Id, ESearchCase::CaseSensitive))
            {
                return Refuse(EError::Aggregation, Field, FString::Printf(
                    TEXT("%s and %s share passive family %s without an approved aggregation rule"),
                    *Family->EquipmentId, *Item.Id, *Family->Family));
            }
            Family->EquippedIndices.Add(Index);
        }
        Families.Sort([](const FLDEquipmentPassiveFamily& A, const FLDEquipmentPassiveFamily& B)
        {
            return A.Family.Compare(B.Family, ESearchCase::CaseSensitive) < 0;
        });
        return true;
    }

    bool AddBonus(const FLDDemoStatBonus& Bonus, FTermSum FAttributeSum::* Category, const FExact& Multiplier,
        const FString& Field)
    {
        EAttribute Attribute = EAttribute::Count;
        bool bRatio = false;
        bool bBasisPoints = false;
        StatTarget(Bonus.Stat, Attribute, bRatio, bBasisPoints);
        const bool bRange = Bonus.Stat == ELDDemoBonusStat::AttackRangeCmFlat;
        if (Attribute == EAttribute::Count || (bRange ? !NonnegativeRational(Bonus.Cm) : Bonus.Amount < 0))
        {
            return Refuse(EError::Catalog, Field, TEXT("Unsupported or negative stat bonus"));
        }
        const FExact Amount = Numbers.Mul(
            bRange ? Numbers.Value(Bonus.Cm) : Numbers.Value(Bonus.Amount, bBasisPoints ? 10000 : 100), Multiplier);
        FTermSum& Terms = Sums[int32(Attribute)].*Category;
        FExact& Target = bRatio ? Terms.Ratio : Terms.Flat;
        Target = Numbers.Add(Target, Amount);
        return Check.Good || ArithmeticFailure(Field);
    }

    bool Accumulate()
    {
        const FLDDemoContentParameters& P = Content.Parameters;
        const FExact Steps = Numbers.Value(int64(Build.Level) - 1);
        struct FPointGrowth { EAttribute Attribute; const FLDDemoP2Bounds* Bounds; };
        const FPointGrowth PointGrowth[] =
        {
            {EAttribute::MaxHP, &P.MaxHP}, {EAttribute::MaxMP, &P.MaxMP}, {EAttribute::AD, &P.AD},
            {EAttribute::AP, &P.AP}, {EAttribute::Armor, &P.Armor}, {EAttribute::MagicResist, &P.MagicResist},
        };
        for (const FPointGrowth& Row : PointGrowth)
        {
            FAttributeSum& Sum = Sums[int32(Row.Attribute)];
            Sum.Base = Numbers.Value(Row.Bounds->BaseP2, 100);
            Sum.Level.Flat = Numbers.Mul(Numbers.Value(Row.Bounds->PerLevelP2, 100), Steps);
        }
        struct FRateGrowth { EAttribute Attribute; const FLDDemoRateBounds* Bounds; };
        const FRateGrowth RateGrowth[] = {{EAttribute::HPRegen, &P.HPRegen}, {EAttribute::MPRegen, &P.MPRegen}};
        for (const FRateGrowth& Row : RateGrowth)
        {
            FAttributeSum& Sum = Sums[int32(Row.Attribute)];
            Sum.Base = Numbers.Value(Row.Bounds->Base);
            Sum.Level.Flat = Numbers.Mul(Numbers.Value(Row.Bounds->PerLevel), Steps);
        }
        FAttributeSum& AttackSpeed = Sums[int32(EAttribute::AttackSpeed)];
        AttackSpeed.Base = Numbers.Value(P.AttackSpeed.Base);
        AttackSpeed.Level.Ratio = Numbers.Mul(Numbers.Value(P.AttackSpeedPerLevelBp, 10000), Steps);
        Sums[int32(EAttribute::MoveSpeed)].Base = Numbers.Value(P.MoveSpeedCmPerSec.Base);
        Sums[int32(EAttribute::AttackRange)].Base = Numbers.Value(P.AttackRangeCm.Base);
        Sums[int32(EAttribute::CritChance)].Base = Numbers.Value(P.CritChanceBaseBp, 10000);
        Sums[int32(EAttribute::CooldownHaste)].Base = Numbers.Value(P.CooldownHasteBaseP2, 100);
        if (!Check.Good)
        {
            return ArithmeticFailure(TEXT("Catalog"));
        }
        for (int32 Index = 0; Index < Cards.Num(); ++Index)
        {
            const FExact Count = Numbers.Value(Build.AttributeCards[Index].Count);
            const FString Field = FString::Printf(TEXT("AttributeCards[%d]"), Index);
            for (const FLDDemoStatBonus& Bonus : Cards[Index]->Stats)
            {
                if (!AddBonus(Bonus, &FAttributeSum::Choices, Count, Field))
                {
                    return false;
                }
            }
        }
        const FExact One = Numbers.Value(1);
        for (int32 Index = 0; Index < PassiveRanks.Num(); ++Index)
        {
            const FString Field = FString::Printf(TEXT("LearnedPassives[%d]"), Index);
            for (const FLDDemoEffectDefinition& Effect : PassiveRanks[Index]->Effects)
            {
                if (Effect.GetOp() == ELDDemoEffectOp::StatBonus)
                {
                    for (const FLDDemoStatBonus& Bonus : Effect.Stats)
                    {
                        if (!AddBonus(Bonus, &FAttributeSum::Passives, One, Field))
                        {
                            return false;
                        }
                    }
                }
                else if (Effect.GetOp() == ELDDemoEffectOp::OutOfCombatRegen)
                {
                    // The bonus depends on combat time, so it is reported beside the always-on rate.
                    if (OutOfCombat.IsSet())
                    {
                        return Refuse(EError::Aggregation, Field,
                            TEXT("Several conditional regeneration sources have no approved aggregation rule"));
                    }
                    if (Effect.DelayMs <= 0 || !NonnegativeRational(Effect.HPPerSec))
                    {
                        return Refuse(EError::Catalog, Field,
                            TEXT("Conditional regeneration requires a positive delay and a nonnegative rate"));
                    }
                    FLDOutOfCombatRegenTerm& Term = OutOfCombat.Emplace();
                    Term.AbilityId = Passives[Index]->Id;
                    Term.Rank = Build.LearnedPassives[Index].Rank;
                    Term.DelayMs = Effect.DelayMs;
                    OutOfCombatRate = Numbers.Value(Effect.HPPerSec);
                }
                else if (Effect.GetOp() == ELDDemoEffectOp::LowHpStatBonus)
                {
                    if (LowHP.IsSet())
                    {
                        return Refuse(EError::Aggregation, Field, TEXT("Several low-HP passives have no approved aggregation rule"));
                    }
                    if (Effect.RatioBp <= 0 || Effect.RatioBp >= 10000 || Effect.Stats.IsEmpty())
                    {
                        return Refuse(EError::Catalog, Field, TEXT("A low-HP passive requires a threshold inside the pool and stats"));
                    }
                    FLDLowHPBonusTerm& Term = LowHP.Emplace();
                    Term.AbilityId = Passives[Index]->Id;
                    Term.Rank = Build.LearnedPassives[Index].Rank;
                    Term.ThresholdBp = Effect.RatioBp;
                    Term.Stats = Effect.Stats;
                }
                else if (Effect.GetOp() == ELDDemoEffectOp::AmbushDamage)
                {
                    if (Ambush.IsSet())
                    {
                        return Refuse(EError::Aggregation, Field, TEXT("Several ambush passives have no approved aggregation rule"));
                    }
                    if (Effect.DelayMs <= 0 || Effect.BaseP2 < 0 || Effect.ADRatioBp < 0 || Effect.APRatioBp < 0)
                    {
                        return Refuse(EError::Catalog, Field, TEXT("An ambush passive requires a positive delay and nonnegative terms"));
                    }
                    FLDAmbushDamageTerm& Term = Ambush.Emplace();
                    Term.AbilityId = Passives[Index]->Id;
                    Term.Rank = Build.LearnedPassives[Index].Rank;
                    Term.DelayMs = Effect.DelayMs;
                    Term.BaseP2 = Effect.BaseP2;
                    Term.ADRatioBp = Effect.ADRatioBp;
                    Term.APRatioBp = Effect.APRatioBp;
                    Term.DamageType = Effect.DamageType;
                }
                else if (Effect.GetOp() == ELDDemoEffectOp::ControlDamageAmp)
                {
                    if (ControlAmp.IsSet())
                    {
                        return Refuse(EError::Aggregation, Field, TEXT("Several control amplification passives have no approved aggregation rule"));
                    }
                    if (Effect.RatioBp <= 0 || Effect.DurationMs <= 0)
                    {
                        return Refuse(EError::Catalog, Field, TEXT("Control amplification requires a positive ratio and duration"));
                    }
                    FLDControlDamageAmpTerm& Term = ControlAmp.Emplace();
                    Term.AbilityId = Passives[Index]->Id;
                    Term.Rank = Build.LearnedPassives[Index].Rank;
                    Term.RatioBp = Effect.RatioBp;
                    Term.DurationMs = Effect.DurationMs;
                }
                else
                {
                    return Refuse(EError::Catalog, Field, TEXT("Learned passive effect has no attribute projection rule"));
                }
            }
        }
        for (int32 Index = 0; Index < Items.Num(); ++Index)
        {
            const FString Field = FString::Printf(TEXT("EquippedItems[%d]"), Index);
            for (const FLDDemoStatBonus& Bonus : Items[Index]->Stats)
            {
                if (!AddBonus(Bonus, &FAttributeSum::Equipment, One, Field))
                {
                    return false;
                }
            }
        }
        return Check.Good || ArithmeticFailure(TEXT("Accumulate"));
    }

    bool ToRational(const FExact& Value, FLDDemoRational& Out, const FString& Field)
    {
        if (!Numbers.NativePair(Value, Field))
        {
            return false;
        }
        Out.Numerator = Value.N.ToInt();
        Out.Denominator = Value.D.ToInt();
        return true;
    }

    bool StoreRational(const FExact& Value, FLDDemoRational& Out, const FString& Field)
    {
        return ToRational(Value, Out, Field) || ArithmeticFailure(Field);
    }

    bool Within(const FExact& Value, const FLDDemoRational& Min, const FLDDemoRational& Max, const FString& Field)
    {
        const bool bInside = Numbers.LE(Numbers.Value(Min), Value) && Numbers.LE(Value, Numbers.Value(Max));
        if (!Check.Good)
        {
            return ArithmeticFailure(Field);
        }
        return bInside || Refuse(EError::OutOfBounds, Field, Field + TEXT(" is outside its catalog bound"));
    }

    // Exact integer in a storage scale (P2 = 100, basis points = 10000) within inclusive bounds.
    bool StoreScaled(const FExact& Value, int64 Scale, int64 Min, int64 Max, int64& Out, const FString& Field)
    {
        const FExact Scaled = Numbers.Mul(Value, Numbers.Value(Scale));
        if (!Check.Good)
        {
            return ArithmeticFailure(Field);
        }
        if (!(Scaled.D == FBig(1)))
        {
            return Refuse(EError::Catalog, Field, Field + TEXT(" is not exactly representable in its storage unit"));
        }
        if (!Numbers.NativePair(Scaled, Field))
        {
            return ArithmeticFailure(Field);
        }
        const int64 Stored = Scaled.N.ToInt();
        if (Stored < Min || Stored > Max)
        {
            return Refuse(EError::OutOfBounds, Field,
                FString::Printf(TEXT("%s %lld is outside %lld..%lld"), *Field, Stored, Min, Max));
        }
        Out = Stored;
        return true;
    }

    bool StoreFinal(EAttribute Attribute, const FExact& Value, FLDProjectedAttributes& Final)
    {
        const FLDDemoContentParameters& P = Content.Parameters;
        const FString Field = AttributeName(Attribute);
        switch (Attribute)
        {
        case EAttribute::MaxHP: return StoreScaled(Value, 100, P.MaxHP.MinP2, P.MaxHP.MaxP2, Final.MaxHPP2, Field);
        case EAttribute::MaxMP: return StoreScaled(Value, 100, P.MaxMP.MinP2, P.MaxMP.MaxP2, Final.MaxMPP2, Field);
        case EAttribute::AD: return StoreScaled(Value, 100, P.AD.MinP2, P.AD.MaxP2, Final.ADP2, Field);
        case EAttribute::AP: return StoreScaled(Value, 100, P.AP.MinP2, P.AP.MaxP2, Final.APP2, Field);
        case EAttribute::Armor: return StoreScaled(Value, 100, P.Armor.MinP2, P.Armor.MaxP2, Final.ArmorP2, Field);
        case EAttribute::MagicResist:
            return StoreScaled(Value, 100, P.MagicResist.MinP2, P.MagicResist.MaxP2, Final.MagicResistP2, Field);
        case EAttribute::AttackSpeed: return StoreRational(Value, Final.AttackSpeed, Field);
        case EAttribute::MoveSpeed: return StoreRational(Value, Final.MoveSpeedCmPerSec, Field);
        case EAttribute::AttackRange:
            return Within(Value, P.AttackRangeCm.Min, P.AttackRangeCm.Max, Field)
                && StoreRational(Value, Final.AttackRangeCm, Field);
        case EAttribute::HPRegen:
            // The catalog bound also covers the out-of-combat peak, so check both rates.
            return Within(Value, P.HPRegen.Min, P.HPRegen.Max, Field)
                && (!OutOfCombat.IsSet() || Within(Numbers.Add(Value, OutOfCombatRate), P.HPRegen.Min, P.HPRegen.Max,
                    TEXT("OutOfCombatHPRegen")))
                && StoreRational(Value, Final.HPRegenPerSec, Field);
        case EAttribute::MPRegen:
            return Within(Value, P.MPRegen.Min, P.MPRegen.Max, Field) && StoreRational(Value, Final.MPRegenPerSec, Field);
        case EAttribute::CritChance: return StoreScaled(Value, 10000, 0, 10000, Final.CritChanceBp, Field);
        case EAttribute::CooldownHaste:
            return StoreScaled(Value, 100, 0, P.CooldownHasteMaxP2, Final.CooldownHasteP2, Field);
        default:
            return Refuse(EError::Catalog, Field, TEXT("Unknown projected attribute"));
        }
    }

    bool Finish(FLDAttributeProjection& Out)
    {
        const FLDDemoContentParameters& P = Content.Parameters;
        const FExact One = Numbers.Value(1);
        Out.Contributions.Reset(AttributeCount);
        for (int32 Index = 0; Index < AttributeCount; ++Index)
        {
            const EAttribute Attribute = static_cast<EAttribute>(Index);
            const FString Field = AttributeName(Attribute);
            const FAttributeSum& Sum = Sums[Index];
            const FExact Flat = Numbers.Add(Numbers.Add(Sum.Level.Flat, Sum.Choices.Flat),
                Numbers.Add(Sum.Passives.Flat, Sum.Equipment.Flat));
            const FExact Ratio = Numbers.Add(Numbers.Add(Sum.Level.Ratio, Sum.Choices.Ratio),
                Numbers.Add(Sum.Passives.Ratio, Sum.Equipment.Ratio));
            FExact Value = Numbers.Mul(Numbers.Add(Sum.Base, Flat), Numbers.Add(One, Ratio));
            bool bClamped = false;
            if (Attribute == EAttribute::AttackSpeed || Attribute == EAttribute::MoveSpeed)
            {
                // FORMULA.AttackSpeed/MoveSpeed clamp last; every other bound refuses instead.
                const FLDDemoRationalBounds& Bounds = Attribute == EAttribute::AttackSpeed ? P.AttackSpeed : P.MoveSpeedCmPerSec;
                const FExact Min = Numbers.Value(Bounds.Min);
                const FExact Max = Numbers.Value(Bounds.Max);
                if (Numbers.LT(Value, Min))
                {
                    Value = Min;
                    bClamped = true;
                }
                else if (Numbers.LT(Max, Value))
                {
                    Value = Max;
                    bClamped = true;
                }
            }
            if (!Check.Good)
            {
                return ArithmeticFailure(Field);
            }
            if (!StoreFinal(Attribute, Value, Out.Final))
            {
                return false;
            }
            FLDAttributeContributions& Row = Out.Contributions.AddDefaulted_GetRef();
            Row.Attribute = Attribute;
            Row.bClamped = bClamped;
            const bool bRowStored = ToRational(Sum.Base, Row.Base, Field)
                && ToRational(Sum.Level.Flat, Row.LevelGrowth.Flat, Field)
                && ToRational(Sum.Level.Ratio, Row.LevelGrowth.AdditiveRatio, Field)
                && ToRational(Sum.Choices.Flat, Row.PermanentChoices.Flat, Field)
                && ToRational(Sum.Choices.Ratio, Row.PermanentChoices.AdditiveRatio, Field)
                && ToRational(Sum.Passives.Flat, Row.Passives.Flat, Field)
                && ToRational(Sum.Passives.Ratio, Row.Passives.AdditiveRatio, Field)
                && ToRational(Sum.Equipment.Flat, Row.Equipment.Flat, Field)
                && ToRational(Sum.Equipment.Ratio, Row.Equipment.AdditiveRatio, Field)
                && ToRational(Value, Row.Final, Field);
            if (!bRowStored)
            {
                return ArithmeticFailure(Field);
            }
        }
        if (OutOfCombat.IsSet())
        {
            if (!StoreRational(OutOfCombatRate, OutOfCombat->HPPerSec, TEXT("OutOfCombatHPRegen")))
            {
                return false;
            }
            Out.OutOfCombatHPRegen = OutOfCombat;
        }
        Out.LowHPBonus = LowHP;
        Out.Ambush = Ambush;
        Out.ControlAmp = ControlAmp;
        Out.EquipmentPassives = MoveTemp(Families);
        return true;
    }

    const FLDAttributeBuildValues& Build;
    const FLDDemoContentSnapshot& Catalog;
    const FLDDemoContentDefinitions& Content;
    FString MathError;
    LDDemoContent::FCheck Check;
    FNumbers Numbers;
    TArray<FAttributeSum> Sums;
    TArray<const FLDDemoAbilityDefinition*, TInlineAllocator<8>> Passives;
    TArray<const FLDDemoRankDefinition*, TInlineAllocator<8>> PassiveRanks;
    TArray<const FLDDemoAttributeDefinition*, TInlineAllocator<8>> Cards;
    TArray<const FLDDemoEquipmentDefinition*, TInlineAllocator<2>> Items;
    TArray<FLDEquipmentPassiveFamily> Families;
    TOptional<FLDOutOfCombatRegenTerm> OutOfCombat;
    FExact OutOfCombatRate;
    TOptional<FLDLowHPBonusTerm> LowHP;
    TOptional<FLDAmbushDamageTerm> Ambush;
    TOptional<FLDControlDamageAmpTerm> ControlAmp;
};

// Exact value of one projected attribute as stored in Final.
FExact FinalValue(EAttribute Attribute, const FLDProjectedAttributes& Final, FNumbers& N)
{
    switch (Attribute)
    {
    case EAttribute::MaxHP: return N.Value(Final.MaxHPP2, 100);
    case EAttribute::MaxMP: return N.Value(Final.MaxMPP2, 100);
    case EAttribute::AD: return N.Value(Final.ADP2, 100);
    case EAttribute::AP: return N.Value(Final.APP2, 100);
    case EAttribute::Armor: return N.Value(Final.ArmorP2, 100);
    case EAttribute::MagicResist: return N.Value(Final.MagicResistP2, 100);
    case EAttribute::AttackSpeed: return N.Value(Final.AttackSpeed);
    case EAttribute::MoveSpeed: return N.Value(Final.MoveSpeedCmPerSec);
    case EAttribute::AttackRange: return N.Value(Final.AttackRangeCm);
    case EAttribute::HPRegen: return N.Value(Final.HPRegenPerSec);
    case EAttribute::MPRegen: return N.Value(Final.MPRegenPerSec);
    case EAttribute::CritChance: return N.Value(Final.CritChanceBp, 10000);
    case EAttribute::CooldownHaste: return N.Value(Final.CooldownHasteP2, 100);
    default: return N.Value(0);
    }
}

// Signed reduced pair; temporary ratios may be negative (a slow).
bool SignedRational(const FExact& Value, FLDDemoRational& Out, FNumbers& N)
{
    FExact Magnitude = Value;
    const bool bNegative = Magnitude.N.IsNegative();
    if (bNegative) { Magnitude.N.Negate(); }
    if (!N.NativePair(Magnitude, TEXT("TemporaryTerms"))) { return false; }
    Out.Numerator = bNegative ? -Magnitude.N.ToInt() : Magnitude.N.ToInt();
    Out.Denominator = Magnitude.D.ToInt();
    return true;
}

// floor(Value x Scale) clamped to [Min, Max]; Value is nonnegative.
bool FloorScaled(const FExact& Value, int64 Scale, int64 Min, int64 Max, int64& Out, bool& bClamped, FNumbers& N)
{
    const FExact Scaled = N.Mul(Value, N.Value(Scale));
    FBig Quotient = Scaled.N;
    FBig Remainder;
    Quotient.DivideWithRemainder(Scaled.D, Remainder);
    if (!N.NativePair({Quotient, FBig(1)}, TEXT("TemporaryTerms"))) { return false; }
    const int64 Floored = Quotient.ToInt();
    Out = FMath::Clamp(Floored, Min, Max);
    bClamped = Out != Floored;
    return true;
}

bool ClampRational(const FExact& Value, const FLDDemoRational& Min, const FLDDemoRational& Max, FLDDemoRational& Out,
    bool& bClamped, FNumbers& N)
{
    FExact Clamped = Value;
    if (N.LT(Clamped, N.Value(Min))) { Clamped = N.Value(Min); }
    else if (N.LT(N.Value(Max), Clamped)) { Clamped = N.Value(Max); }
    bClamped = !N.EQ(Clamped, Value);
    return SignedRational(Clamped, Out, N);
}
}

FLDAttributeProjectionResult Project(const FLDAttributeBuildValues& Build, const FLDDemoContentSnapshot& Catalog)
{
    FLDAttributeProjectionResult Result;
    FLDAttributeProjection Projection;
    FProjector Projector(Build, Catalog);
    if (!Projector.Run(Projection))
    {
        Result.Error = Projector.Failure;
        return Result;
    }
    Result.Value.Emplace(MoveTemp(Projection));
    return Result;
}

FLDAttributeProjectionResult ApplyTemporary(const FLDAttributeProjection& Build, const FLDTemporaryAttributeTerms& Terms,
    const FLDDemoContentSnapshot& Catalog)
{
    FLDAttributeProjectionResult Result;
    const auto Fail = [&Result](EError Code, const FString& Field, const FString& Message)
    {
        Result.Value.Reset();
        Result.Error.Code = Code;
        Result.Error.Field = Field;
        Result.Error.Message = Message;
        return Result;
    };
    FLDAttributeProjection Out = Build;
    if (Terms.IsEmpty())
    {
        Result.Value.Emplace(MoveTemp(Out));
        return Result;
    }
    if (Terms.MoveSlowBp < 0 || Terms.MoveSlowBp >= 10000)
    {
        return Fail(EError::InputDomain, TEXT("MoveSlowBp"), TEXT("A slow must keep some move speed"));
    }
    const bool bRows = Build.Contributions.Num() == AttributeCount;
    if (!bRows && !Build.Contributions.IsEmpty())
    {
        return Fail(EError::InputDomain, TEXT("Contributions"), TEXT("A build projection has a partial contribution table"));
    }
    FString MathError;
    LDDemoContent::FCheck Check(MathError);
    FNumbers N(Check);
    TArray<FTermSum> Temporary;
    Temporary.SetNum(AttributeCount);
    TArray<bool> Touched;
    Touched.Init(false, AttributeCount);
    for (int32 Index = 0; Index < Terms.Bonuses.Num(); ++Index)
    {
        const FLDDemoStatBonus& Bonus = Terms.Bonuses[Index];
        const FString Field = FString::Printf(TEXT("Temporary[%d]"), Index);
        EAttribute Attribute = EAttribute::Count;
        bool bRatio = false, bBasisPoints = false;
        const bool bRange = Bonus.Stat == ELDDemoBonusStat::AttackRangeCmFlat;
        if (!StatTarget(Bonus.Stat, Attribute, bRatio, bBasisPoints) || Attribute == EAttribute::MaxHP || Attribute == EAttribute::MaxMP
            || (bRange ? !NonnegativeRational(Bonus.Cm) : Bonus.Amount < 0))
        {
            return Fail(EError::InputDomain, Field, TEXT("A temporary bonus must be a nonnegative, non-resource stat"));
        }
        const FExact Amount = bRange ? N.Value(Bonus.Cm) : N.Value(Bonus.Amount, bBasisPoints ? 10000 : 100);
        FTermSum& Sum = Temporary[int32(Attribute)];
        (bRatio ? Sum.Ratio : Sum.Flat) = N.Add(bRatio ? Sum.Ratio : Sum.Flat, Amount);
        Touched[int32(Attribute)] = true;
    }
    if (Terms.MoveSlowBp > 0)
    {
        FTermSum& Sum = Temporary[int32(EAttribute::MoveSpeed)];
        Sum.Ratio = N.Sub(Sum.Ratio, N.Value(Terms.MoveSlowBp, 10000));
        Touched[int32(EAttribute::MoveSpeed)] = true;
    }
    const FLDDemoContentParameters& P = Catalog.Get().Parameters;
    const FExact One = N.Value(1);
    for (int32 Index = 0; Index < AttributeCount; ++Index)
    {
        if (!Touched[Index]) { continue; }
        const EAttribute Attribute = static_cast<EAttribute>(Index);
        const FString Field = AttributeName(Attribute);
        FExact Base, Flat = N.Value(0), Ratio = N.Value(0);
        if (bRows)
        {
            const FLDAttributeContributions& Row = Build.Contributions[Index];
            Base = N.Value(Row.Base);
            for (const FLDAttributeTerms* Terms2 : {&Row.LevelGrowth, &Row.PermanentChoices, &Row.Passives, &Row.Equipment})
            {
                Flat = N.Add(Flat, N.Value(Terms2->Flat));
                Ratio = N.Add(Ratio, N.Value(Terms2->AdditiveRatio));
            }
        }
        else
        {
            Base = FinalValue(Attribute, Build.Final, N);
        }
        FExact Value = N.Mul(N.Add(N.Add(Base, Flat), Temporary[Index].Flat), N.Add(N.Add(One, Ratio), Temporary[Index].Ratio));
        if (N.LT(Value, N.Value(0))) { Value = N.Value(0); }
        if (!Check.Good) { return Fail(EError::Overflow, Field, MathError); }
        FLDProjectedAttributes& Final = Out.Final;
        bool bClamped = false;
        bool bStored = true;
        switch (Attribute)
        {
        case EAttribute::AD: bStored = FloorScaled(Value, 100, P.AD.MinP2, P.AD.MaxP2, Final.ADP2, bClamped, N); break;
        case EAttribute::AP: bStored = FloorScaled(Value, 100, P.AP.MinP2, P.AP.MaxP2, Final.APP2, bClamped, N); break;
        case EAttribute::Armor: bStored = FloorScaled(Value, 100, P.Armor.MinP2, P.Armor.MaxP2, Final.ArmorP2, bClamped, N); break;
        case EAttribute::MagicResist:
            bStored = FloorScaled(Value, 100, P.MagicResist.MinP2, P.MagicResist.MaxP2, Final.MagicResistP2, bClamped, N); break;
        case EAttribute::CooldownHaste:
            bStored = FloorScaled(Value, 100, 0, P.CooldownHasteMaxP2, Final.CooldownHasteP2, bClamped, N); break;
        case EAttribute::CritChance: bStored = FloorScaled(Value, 10000, 0, 10000, Final.CritChanceBp, bClamped, N); break;
        case EAttribute::AttackSpeed:
            bStored = ClampRational(Value, P.AttackSpeed.Min, P.AttackSpeed.Max, Final.AttackSpeed, bClamped, N); break;
        case EAttribute::MoveSpeed:
            bStored = ClampRational(Value, P.MoveSpeedCmPerSec.Min, P.MoveSpeedCmPerSec.Max, Final.MoveSpeedCmPerSec, bClamped, N); break;
        case EAttribute::AttackRange:
            bStored = ClampRational(Value, P.AttackRangeCm.Min, P.AttackRangeCm.Max, Final.AttackRangeCm, bClamped, N); break;
        default:
            return Fail(EError::InputDomain, Field, TEXT("This attribute takes no temporary term"));
        }
        if (!bStored || !Check.Good) { return Fail(EError::Overflow, Field, MathError); }
        if (bRows)
        {
            FLDAttributeContributions& Row = Out.Contributions[Index];
            const FExact Stored = FinalValue(Attribute, Out.Final, N);
            if (!SignedRational(Temporary[Index].Flat, Row.Temporary.Flat, N)
                || !SignedRational(Temporary[Index].Ratio, Row.Temporary.AdditiveRatio, N)
                || !SignedRational(Stored, Row.Final, N))
            {
                return Fail(EError::Overflow, Field, MathError);
            }
            Row.bClamped = Row.bClamped || bClamped;
        }
    }
    Result.Value.Emplace(MoveTemp(Out));
    return Result;
}

FLDResourceMaxChangeResult ResourceMaxChange(const FLDResourceMaxChangeInput& Input, const FLDDemoContentSnapshot& Catalog)
{
    const auto Fail = [](EError Code, const TCHAR* Field, const FString& Message)
    {
        FLDResourceMaxChangeResult Refused;
        Refused.Error.Code = Code;
        Refused.Error.Field = Field;
        Refused.Error.Message = Message;
        return Refused;
    };
    const FLDDemoContentDefinitions& Content = Catalog.Get();
    if (Content.Formulas.ResourceMaxChange != ELDDemoFormula::FloorFinalRatioRejectAliveUnderflow
        || Content.Policies.ResourceMaxChange != ELDDemoPolicy::PreserveRatio)
    {
        return Fail(EError::Catalog, TEXT("Catalog"), TEXT("Unsupported resource maximum change contract"));
    }
    if (Input.Pool == ELDResourcePool::None)
    {
        return Fail(EError::MissingValue, TEXT("Pool"), TEXT("The resource pool must be explicit"));
    }
    if (Input.Pool != ELDResourcePool::HP && Input.Pool != ELDResourcePool::MP)
    {
        return Fail(EError::InputDomain, TEXT("Pool"), TEXT("Unknown resource pool"));
    }
    if (!Input.OldCurrentP2.IsSet())
    {
        return Fail(EError::MissingValue, TEXT("OldCurrentP2"), TEXT("The authoritative current resource is required"));
    }
    if (!Input.OldMaxP2.IsSet())
    {
        return Fail(EError::MissingValue, TEXT("OldMaxP2"), TEXT("The authoritative old maximum is required"));
    }
    if (!Input.NewMaxP2.IsSet())
    {
        return Fail(EError::MissingValue, TEXT("NewMaxP2"), TEXT("The final projected maximum is required"));
    }
    const int64 OldCurrent = Input.OldCurrentP2.GetValue();
    const int64 OldMax = Input.OldMaxP2.GetValue();
    const int64 NewMax = Input.NewMaxP2.GetValue();
    if (OldCurrent < 0 || OldMax < 0 || NewMax < 0)
    {
        return Fail(EError::InputDomain, TEXT("Resources"), TEXT("P2 resources and maximums must be nonnegative"));
    }
    if (OldCurrent > OldMax)
    {
        return Fail(EError::InputDomain, TEXT("OldCurrentP2"), TEXT("The current resource exceeds its old maximum"));
    }
    int64 NewCurrent = OldCurrent;
    if (NewMax != OldMax)
    {
        if (OldMax == 0)
        {
            return Fail(EError::NumericDomain, TEXT("OldMaxP2"),
                TEXT("UndefinedResourceRatio: a zero old maximum has no ratio to preserve"));
        }
        if (OldCurrent == 0 || NewMax <= MAX_int64 / OldCurrent)
        {
            NewCurrent = OldCurrent * NewMax / OldMax;
        }
        else
        {
            // OldCurrent <= OldMax keeps the exact quotient within NewMax and therefore within int64.
            FBig Quotient = FBig(OldCurrent) * FBig(NewMax);
            FBig Remainder;
            Quotient.DivideWithRemainder(FBig(OldMax), Remainder);
            NewCurrent = Quotient.ToInt();
        }
        if (Input.Pool == ELDResourcePool::HP && OldCurrent > 0 && NewCurrent == 0)
        {
            return Fail(EError::NumericUnderflow, TEXT("NewMaxP2"),
                TEXT("Positive HP would quantize to zero; the change is refused rather than raised or made lethal"));
        }
    }
    FLDResourceMaxChangeResult Result;
    Result.Value.Emplace().NewCurrentP2 = NewCurrent;
    return Result;
}
}

bool FLDTemporaryAttributeTerms::operator==(const FLDTemporaryAttributeTerms& Other) const
{
    if (MoveSlowBp != Other.MoveSlowBp || Bonuses.Num() != Other.Bonuses.Num()) { return false; }
    for (int32 Index = 0; Index < Bonuses.Num(); ++Index)
    {
        const FLDDemoStatBonus& A = Bonuses[Index];
        const FLDDemoStatBonus& B = Other.Bonuses[Index];
        if (A.Stat != B.Stat || A.Amount != B.Amount || A.Cm.Numerator != B.Cm.Numerator || A.Cm.Denominator != B.Cm.Denominator)
        {
            return false;
        }
    }
    return true;
}
