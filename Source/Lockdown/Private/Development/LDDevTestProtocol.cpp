#include "Development/LDDevTestProtocol.h"

#include "Development/LDDemoContentDefinitions.h"
#include "Framework/LDPacingTypes.h"
#include "Combat/LDAttributeProjection.h"
#include "UI/LDTerms.h"

bool FLDDevTestCommand::SameTerms(const FLDDevTestCommand& Other) const
{
    return Generation == Other.Generation && Life == Other.Life && RequestId == Other.RequestId && Op == Other.Op
        && Amount == Other.Amount && Definition == Other.Definition && Slot == Other.Slot && Stat == Other.Stat
        && CameraPosition == Other.CameraPosition;
}

bool LDDevTest::IsKnownOp(ELDDevTestOp Op)
{
    switch (Op)
    {
    case ELDDevTestOp::GrantExperience:
    case ELDDevTestOp::RefillResources:
    case ELDDevTestOp::ResetCooldowns:
    case ELDDevTestOp::SetAttribute:
    case ELDDevTestOp::ClearAttributes:
    case ELDDevTestOp::ResetToMatchStart:
    case ELDDevTestOp::RefreshCamps:
    case ELDDevTestOp::OpenCapture:
    case ELDDevTestOp::GrantBlueprint:
    case ELDDevTestOp::LearnActive:
    case ELDDevTestOp::AllocatePoints:
    case ELDDevTestOp::SkipPreparation:
    case ELDDevTestOp::SetPeaceful:
    case ELDDevTestOp::TeleportToCamera:
    case ELDDevTestOp::ApplyBreakoutPreset:
    case ELDDevTestOp::SpawnGroundState:
    case ELDDevTestOp::SetDailyPreset:
    case ELDDevTestOp::SetMomentum:
    case ELDDevTestOp::GrantSkillPoint:
    case ELDDevTestOp::SetInvulnerable:
    case ELDDevTestOp::SetCountdownPaused:
    case ELDDevTestOp::AdvanceCountdown:
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
    case ELDDevTestOp::SetAttribute: return TEXT("SetAttribute");
    case ELDDevTestOp::ClearAttributes: return TEXT("ClearAttributes");
    case ELDDevTestOp::ResetToMatchStart: return TEXT("ResetToMatchStart");
    case ELDDevTestOp::RefreshCamps: return TEXT("RefreshCamps");
    case ELDDevTestOp::OpenCapture: return TEXT("OpenCapture");
    case ELDDevTestOp::GrantBlueprint: return TEXT("GrantBlueprint");
    case ELDDevTestOp::LearnActive: return TEXT("LearnActive");
    case ELDDevTestOp::AllocatePoints: return TEXT("AllocatePoints");
    case ELDDevTestOp::SkipPreparation: return TEXT("SkipPreparation");
    case ELDDevTestOp::SetPeaceful: return TEXT("SetPeaceful");
    case ELDDevTestOp::TeleportToCamera: return TEXT("TeleportToCamera");
    case ELDDevTestOp::ApplyBreakoutPreset: return TEXT("ApplyBreakoutPreset");
    case ELDDevTestOp::SpawnGroundState: return TEXT("SpawnGroundState");
    case ELDDevTestOp::SetDailyPreset: return TEXT("SetDailyPreset");
    case ELDDevTestOp::SetMomentum: return TEXT("SetMomentum");
    case ELDDevTestOp::GrantSkillPoint: return TEXT("GrantSkillPoint");
    case ELDDevTestOp::SetInvulnerable: return TEXT("SetInvulnerable");
    case ELDDevTestOp::SetCountdownPaused: return TEXT("SetCountdownPaused");
    case ELDDevTestOp::AdvanceCountdown: return TEXT("AdvanceCountdown");
    default: return TEXT("Unknown");
    }
}

bool LDDevTest::IsMatchWide(ELDDevTestOp Op)
{
    return Op == ELDDevTestOp::RefreshCamps || Op == ELDDevTestOp::OpenCapture || Op == ELDDevTestOp::SkipPreparation
        || Op == ELDDevTestOp::SetDailyPreset
        || Op == ELDDevTestOp::SetCountdownPaused || Op == ELDDevTestOp::AdvanceCountdown;
}

ELDGrowthReason LDDevTest::ValidateShape(const FLDDevTestCommand& Command)
{
    if (Command.RequestId == 0 || !IsKnownOp(Command.Op))
    {
        return ELDGrowthReason::InvalidRequest;
    }
    const bool bPositive = Command.Op == ELDDevTestOp::GrantExperience || Command.Op == ELDDevTestOp::AllocatePoints
        || Command.Op == ELDDevTestOp::AdvanceCountdown;
    const bool bSetAttribute = Command.Op == ELDDevTestOp::SetAttribute;
    // SetAttribute and SetMomentum take any amount from 0; the commit checks momentum against MOMENTUM.Max.
    const bool bFromZero = bSetAttribute || Command.Op == ELDDevTestOp::SetMomentum;
    if (bPositive ? Command.Amount <= 0 : (bFromZero ? Command.Amount < 0
        : (Command.Op == ELDDevTestOp::SetPeaceful || Command.Op == ELDDevTestOp::SetInvulnerable || Command.Op == ELDDevTestOp::SetCountdownPaused) ? (Command.Amount < 0 || Command.Amount > 1)
        : Command.Op == ELDDevTestOp::SpawnGroundState ? (Command.Amount < 1 || Command.Amount > 5)
        : Command.Op == ELDDevTestOp::SetDailyPreset ? !LDDevTest::DecodeDailyPreset(Command.Amount, nullptr) : Command.Amount != 0))
    {
        return ELDGrowthReason::NumericDomain;
    }
    // ADBonusRatio and APBonusRatio are timed/conditional-only stats, so the permanent DEV layer ends at CritChanceBonusRatio.
    if (bSetAttribute ? Command.Stat > uint8(ELDDemoBonusStat::CritChanceBonusRatio) : Command.Stat != 0)
    {
        return ELDGrowthReason::InvalidRequest;
    }
    const bool bNamesDefinition = Command.Op == ELDDevTestOp::GrantBlueprint || Command.Op == ELDDevTestOp::LearnActive
        || Command.Op == ELDDevTestOp::AllocatePoints || Command.Op == ELDDevTestOp::ApplyBreakoutPreset;
    FName Decoded;
    if (bNamesDefinition ? !Command.Definition.Decode(Decoded) : !(Command.Definition == FLDDefinitionWire()))
    {
        return ELDGrowthReason::InvalidRequest;
    }
    const uint8 MaxSlot = Command.Op == ELDDevTestOp::LearnActive ? 3 : 0;
    if (Command.Slot > MaxSlot)
    {
        return ELDGrowthReason::InvalidRequest;
    }
    if (Command.CameraPosition.ContainsNaN() || Command.CameraPosition.GetAbsMax() > HALF_WORLD_MAX
        || (Command.Op != ELDDevTestOp::TeleportToCamera && Command.CameraPosition != FVector::ZeroVector))
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
    case ELDDevTestOp::RefillResources: { static const FString Label=FString(TEXT("补满"))+LDTerms::Health+TEXT("与")+LDTerms::Momentum; return *Label; }
    case ELDDevTestOp::ResetCooldowns: return TEXT("重置冷却");
    case ELDDevTestOp::SetAttribute: return TEXT("设置测试属性");
    case ELDDevTestOp::ClearAttributes: return TEXT("清除测试属性");
    case ELDDevTestOp::ResetToMatchStart: return TEXT("重置到开局");
    case ELDDevTestOp::RefreshCamps: return TEXT("刷新野怪");
    case ELDDevTestOp::OpenCapture: return TEXT("提前开圈");
    case ELDDevTestOp::GrantBlueprint: return TEXT("授予技能图纸");
    case ELDDevTestOp::LearnActive: return TEXT("直接学会技能");
    case ELDDevTestOp::AllocatePoints: return TEXT("分配属性点");
    case ELDDevTestOp::SkipPreparation: return TEXT("跳过准备，进入场景");
    case ELDDevTestOp::SetPeaceful: return TEXT("和平模式");
    case ELDDevTestOp::TeleportToCamera: return TEXT("传送到镜头位置");
    case ELDDevTestOp::ApplyBreakoutPreset: return TEXT("质变样板");
    case ELDDevTestOp::SpawnGroundState: return TEXT("生成地面状态");
    case ELDDevTestOp::SetDailyPreset: return TEXT("下一局热点");
    case ELDDevTestOp::SetMomentum: { static const FString Label=FString(TEXT("设置"))+LDTerms::Momentum; return *Label; }
    case ELDDevTestOp::GrantSkillPoint: return TEXT("加 1 个技能点");
    case ELDDevTestOp::SetInvulnerable: return TEXT("生命不减");
    case ELDDevTestOp::SetCountdownPaused: return TEXT("阶段倒计时暂停");
    case ELDDevTestOp::AdvanceCountdown: return TEXT("阶段倒计时前进");
    default: return TEXT("未知操作");
    }
}

bool LDDevTest::DecodeDailyPreset(int64 Amount, TArray<ELDDailyModifier>* Out)
{
    if (Out) { Out->Reset(); }
    if (Amount == 0) { return true; }
    const int64 First = Amount % 16;
    const int64 Second = Amount / 16;
    if (Amount < 0 || First < 1 || First > LDPacing::DailyModifiers || Second < 1 || Second > LDPacing::DailyModifiers || First == Second)
    {
        return false;
    }
    if (Out) { Out->Add(ELDDailyModifier(First)); Out->Add(ELDDailyModifier(Second)); }
    return true;
}
