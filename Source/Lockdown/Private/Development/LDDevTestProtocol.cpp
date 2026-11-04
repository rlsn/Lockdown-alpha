#include "Development/LDDevTestProtocol.h"

bool FLDDevTestCommand::SameTerms(const FLDDevTestCommand& Other) const
{
    return Generation == Other.Generation && Life == Other.Life && RequestId == Other.RequestId && Op == Other.Op
        && Amount == Other.Amount;
}

bool LDDevTest::IsKnownOp(ELDDevTestOp Op)
{
    switch (Op)
    {
    case ELDDevTestOp::GrantExperience:
    case ELDDevTestOp::GrantGold:
    case ELDDevTestOp::RefillResources:
    case ELDDevTestOp::ResetCooldowns:
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
    if (bGrant ? Command.Amount <= 0 : Command.Amount != 0)
    {
        return ELDGrowthReason::NumericDomain;
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
