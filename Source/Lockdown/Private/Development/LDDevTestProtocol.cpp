#include "Development/LDDevTestProtocol.h"

#include "Development/LDDemoContentDefinitions.h"

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
    case ELDDevTestOp::GrantGold:
    case ELDDevTestOp::RefillResources:
    case ELDDevTestOp::ResetCooldowns:
    case ELDDevTestOp::LearnOption:
    case ELDDevTestOp::BuyItem:
    case ELDDevTestOp::SellItem:
    case ELDDevTestOp::SetAttribute:
    case ELDDevTestOp::ClearAttributes:
    case ELDDevTestOp::ResetToMatchStart:
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
    case ELDDevTestOp::GrantGold: return TEXT("GrantGold");
    case ELDDevTestOp::RefillResources: return TEXT("RefillResources");
    case ELDDevTestOp::ResetCooldowns: return TEXT("ResetCooldowns");
    case ELDDevTestOp::LearnOption: return TEXT("LearnOption");
    case ELDDevTestOp::BuyItem: return TEXT("BuyItem");
    case ELDDevTestOp::SellItem: return TEXT("SellItem");
    case ELDDevTestOp::SetAttribute: return TEXT("SetAttribute");
    case ELDDevTestOp::ClearAttributes: return TEXT("ClearAttributes");
    case ELDDevTestOp::ResetToMatchStart: return TEXT("ResetToMatchStart");
    default: return TEXT("Unknown");
    }
}

ELDGrowthReason LDDevTest::ValidateShape(const FLDDevTestCommand& Command)
{
    if (Command.RequestId == 0 || !IsKnownOp(Command.Op))
    {
        return ELDGrowthReason::InvalidRequest;
    }
    const bool bGrant = Command.Op == ELDDevTestOp::GrantExperience || Command.Op == ELDDevTestOp::GrantGold;
    const bool bSetAttribute = Command.Op == ELDDevTestOp::SetAttribute;
    if (bGrant ? Command.Amount <= 0 : (bSetAttribute ? Command.Amount < 0 : Command.Amount != 0))
    {
        return ELDGrowthReason::NumericDomain;
    }
    // ADBonusRatio is a timed/conditional-only stat, so the permanent DEV layer ends at CritChanceBonusRatio.
    if (bSetAttribute ? Command.Stat > uint8(ELDDemoBonusStat::CritChanceBonusRatio) : Command.Stat != 0)
    {
        return ELDGrowthReason::InvalidRequest;
    }
    const bool bNamesDefinition = Command.Op == ELDDevTestOp::LearnOption || Command.Op == ELDDevTestOp::BuyItem;
    FName Decoded;
    if (bNamesDefinition ? !Command.Definition.Decode(Decoded) : !(Command.Definition == FLDDefinitionWire()))
    {
        return ELDGrowthReason::InvalidRequest;
    }
    const uint8 MaxSlot = Command.Op == ELDDevTestOp::LearnOption || Command.Op == ELDDevTestOp::SellItem ? 2 : 0;
    const uint8 MinSlot = Command.Op == ELDDevTestOp::SellItem ? 1 : 0;
    if (Command.Slot < MinSlot || Command.Slot > MaxSlot)
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
