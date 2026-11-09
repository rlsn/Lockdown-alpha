#include "Development/LDDevTestProtocol.h"

#include "Development/LDDemoContentDefinitions.h"
#include "Combat/LDAttributeProjection.h"
#include "UI/LDTerms.h"

bool FLDDevTestCommand::SameTerms(const FLDDevTestCommand& Other) const
{
    return Generation == Other.Generation && Life == Other.Life && RequestId == Other.RequestId && Op == Other.Op
        && Amount == Other.Amount && Definition == Other.Definition && Slot == Other.Slot && Stat == Other.Stat;
}

bool LDDevTest::IsKnownOp(ELDDevTestOp Op)
{
    switch (Op)
    {
    case ELDDevTestOp::GrantExperience:
    case ELDDevTestOp::RefillResources:
    case ELDDevTestOp::ResetCooldowns:
    case ELDDevTestOp::LearnOption:
    case ELDDevTestOp::SetAttribute:
    case ELDDevTestOp::ClearAttributes:
    case ELDDevTestOp::ResetToMatchStart:
    case ELDDevTestOp::RefreshCamps:
    case ELDDevTestOp::OpenCapture:
        return true;
    default:
        return false;
    }
}

const TCHAR* LDDevTest::OpName(ELDDevTestOp Op)
{
    switch (Op)
    {
    case ELDDevTestOp::GrantExperience: return TEXT("GrantExperience");
    case ELDDevTestOp::RefillResources: return TEXT("RefillResources");
    case ELDDevTestOp::ResetCooldowns: return TEXT("ResetCooldowns");
    case ELDDevTestOp::LearnOption: return TEXT("LearnOption");
    case ELDDevTestOp::SetAttribute: return TEXT("SetAttribute");
    case ELDDevTestOp::ClearAttributes: return TEXT("ClearAttributes");
    case ELDDevTestOp::ResetToMatchStart: return TEXT("ResetToMatchStart");
    case ELDDevTestOp::RefreshCamps: return TEXT("RefreshCamps");
    case ELDDevTestOp::OpenCapture: return TEXT("OpenCapture");
    default: return TEXT("Unknown");
    }
}

bool LDDevTest::IsMatchWide(ELDDevTestOp Op)
{
    return Op == ELDDevTestOp::RefreshCamps || Op == ELDDevTestOp::OpenCapture;
}

ELDGrowthReason LDDevTest::ValidateShape(const FLDDevTestCommand& Command)
{
    if (Command.RequestId == 0 || !IsKnownOp(Command.Op))
    {
        return ELDGrowthReason::InvalidRequest;
    }
    const bool bGrant = Command.Op == ELDDevTestOp::GrantExperience;
    const bool bSetAttribute = Command.Op == ELDDevTestOp::SetAttribute;
    if (bGrant ? Command.Amount <= 0 : (bSetAttribute ? Command.Amount < 0 : Command.Amount != 0))
    {
        return ELDGrowthReason::NumericDomain;
    }
    // ADBonusRatio and APBonusRatio are timed/conditional-only stats, so the permanent DEV layer ends at CritChanceBonusRatio.
    if (bSetAttribute ? Command.Stat > uint8(ELDDemoBonusStat::CritChanceBonusRatio) : Command.Stat != 0)
    {
        return ELDGrowthReason::InvalidRequest;
    }
    const bool bNamesDefinition = Command.Op == ELDDevTestOp::LearnOption;
    FName Decoded;
    if (bNamesDefinition ? !Command.Definition.Decode(Decoded) : !(Command.Definition == FLDDefinitionWire()))
    {
        return ELDGrowthReason::InvalidRequest;
    }
    const uint8 MaxSlot = Command.Op == ELDDevTestOp::LearnOption ? 3 : 0;
    if (Command.Slot > MaxSlot)
    {
        return ELDGrowthReason::InvalidRequest;
    }
    return ELDGrowthReason::None;
}

FLDDevTestResult LDDevTest::Refused(const FLDDevTestCommand& Command, ELDGrowthReason Reason, const FString& Detail)
{
    FLDDevTestResult Result;
    Result.Generation = Command.Generation;
    Result.RequestId = Command.RequestId;
    Result.Status = ELDAdmissionStatus::AdmissionRejected;
    Result.Reason = LDGrowthWire::Raw(Reason);
    Result.Detail = Detail;
    return Result;
}

TConstArrayView<LDDevTest::FStatField> LDDevTest::StatFields()
{
    static const FStatField Fields[] =
    {
        {TEXT("MaxHP"), uint8(ELDDemoBonusStat::MaxHPFlat), 2, TEXT("points"), uint8(ELDProjectedAttribute::MaxHP)},
        {TEXT("MaxMP"), uint8(ELDDemoBonusStat::MaxMPFlat), 2, TEXT("points"), uint8(ELDProjectedAttribute::MaxMP)},
        {TEXT("AD"), uint8(ELDDemoBonusStat::ADFlat), 2, TEXT("points"), uint8(ELDProjectedAttribute::AD)},
        {TEXT("AP"), uint8(ELDDemoBonusStat::APFlat), 2, TEXT("points"), uint8(ELDProjectedAttribute::AP)},
        {TEXT("Armor"), uint8(ELDDemoBonusStat::ArmorFlat), 2, TEXT("points"), uint8(ELDProjectedAttribute::Armor)},
        {TEXT("MR"), uint8(ELDDemoBonusStat::MagicResistFlat), 2, TEXT("points"), uint8(ELDProjectedAttribute::MagicResist)},
        {TEXT("Haste"), uint8(ELDDemoBonusStat::CooldownHasteFlat), 2, TEXT("points"), uint8(ELDProjectedAttribute::CooldownHaste)},
        {TEXT("Range"), uint8(ELDDemoBonusStat::AttackRangeCmFlat), 0, TEXT("cm"), uint8(ELDProjectedAttribute::AttackRange)},
        {TEXT("AttackSpeed"), uint8(ELDDemoBonusStat::AttackSpeedBonusRatio), 2, TEXT("% of base"), uint8(ELDProjectedAttribute::AttackSpeed)},
        {TEXT("MoveSpeed"), uint8(ELDDemoBonusStat::MoveSpeedBonusRatio), 2, TEXT("% of base"), uint8(ELDProjectedAttribute::MoveSpeed)},
        {TEXT("Crit"), uint8(ELDDemoBonusStat::CritChanceBonusRatio), 2, TEXT("percentage points"), uint8(ELDProjectedAttribute::CritChance)},
    };
    return Fields;
}

bool LDDevTest::ParseAmount(const FString& Raw, int64& Out)
{
    Out = 0;
    const FString Text = Raw.TrimStartAndEnd();
    if (Text.IsEmpty() || Text.Len() > 18)
    {
        return false;
    }
    for (const TCHAR Char : Text)
    {
        if (!FChar::IsDigit(Char))
        {
            return false;
        }
    }
    Out = FCString::Atoi64(*Text);
    return Out > 0;
}

bool LDDevTest::ParseScaled(const FString& Raw, int32 Decimals, int64& Out)
{
    Out = 0;
    const FString Text = Raw.TrimStartAndEnd();
    FString Whole, Fraction;
    if (!Text.Split(TEXT("."), &Whole, &Fraction))
    {
        Whole = Text;
    }
    else if (Fraction.IsEmpty())
    {
        return false;
    }
    if (Whole.IsEmpty() || Whole.Len() > 15 || Fraction.Len() > Decimals)
    {
        return false;
    }
    for (const TCHAR Char : Whole + Fraction)
    {
        if (!FChar::IsDigit(Char))
        {
            return false;
        }
    }
    while (Fraction.Len() < Decimals)
    {
        Fraction += TEXT("0");
    }
    Out = FCString::Atoi64(*(Whole + Fraction));
    return true;
}

const TCHAR* LDDevTest::OpLabel(ELDDevTestOp Op)
{
    switch (Op)
    {
    case ELDDevTestOp::GrantExperience: { static const FString Label=FString(TEXT("加"))+LDTerms::Experience; return *Label; }
    case ELDDevTestOp::RefillResources: { static const FString Label=FString(TEXT("补满"))+LDTerms::Health+TEXT("与")+LDTerms::Mana; return *Label; }
    case ELDDevTestOp::ResetCooldowns: return TEXT("重置冷却");
    case ELDDevTestOp::LearnOption: return TEXT("选择卡牌");
    case ELDDevTestOp::SetAttribute: return TEXT("设置测试属性");
    case ELDDevTestOp::ClearAttributes: return TEXT("清除测试属性");
    case ELDDevTestOp::ResetToMatchStart: return TEXT("重置到开局");
    case ELDDevTestOp::RefreshCamps: return TEXT("刷新野怪");
    case ELDDevTestOp::OpenCapture: return TEXT("提前开圈");
    default: return TEXT("未知操作");
    }
}
