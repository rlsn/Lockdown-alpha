#include "Evaluation/LDEvaluationSeeds.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Core/LDLog.h"
#include "Hash/CityHash.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace LDEvaluationSeedsDetail
{
TOptional<FLDEvaluationSeeds> ArmedSeeds;
uint32 SessionsIssued = 0;
}

bool LDEvaluationSeeds::IsSwitchPresent()
{
    static const bool bPresent = FParse::Param(FCommandLine::Get(), TEXT("LDBuildEvaluation"));
    return bPresent;
}

uint64 LDEvaluationSeeds::Stream(uint64 Root, const TCHAR* Name)
{
    const FTCHARToUTF8 Label(*FString::Printf(TEXT("LDEval/%u/%s"), DerivationVersion, Name));
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(Label.Get()), Label.Length());
    for (int32 Byte = 0; Byte < 8; ++Byte) { Bytes.Add(uint8(Root >> (8 * Byte))); }
    return CityHash64(reinterpret_cast<const char*>(Bytes.GetData()), uint32(Bytes.Num()));
}

FGuid LDEvaluationSeeds::StreamGuid(uint64 Root, const TCHAR* Name)
{
    const uint64 High = Stream(Root, *FString::Printf(TEXT("%s.hi"), Name));
    const uint64 Low = Stream(Root, *FString::Printf(TEXT("%s.lo"), Name));
    FGuid Out(uint32(High >> 32), uint32(High), uint32(Low >> 32), uint32(Low));
    // A zero GUID is invalid; its chance is 2^-128, and a fixed nonzero word keeps the function total.
    if (!Out.IsValid()) { Out.D = 1; }
    return Out;
}

FLDEvaluationSeeds LDEvaluationSeeds::Derive(uint64 Root)
{
    FLDEvaluationSeeds Out;
    Out.Root = Root;
    Out.MatchSecret = StreamGuid(Root, TEXT("growth.match_secret"));
    Out.SpawnSeed = int32(uint32(Stream(Root, TEXT("world.spawn"))));
    Out.CritSeed = int32(uint32(Stream(Root, TEXT("combat.crit"))));
    Out.AirdropSeed = Stream(Root, TEXT("world.airdrop"));
    Out.AirdropSeed = Out.AirdropSeed ? Out.AirdropSeed : 1;
    for (int32 Slot = 0; Slot < FLDEvaluationSeeds::MaxAISlots; ++Slot)
    {
        Out.AIContenders[Slot] = StreamGuid(Root, *FString::Printf(TEXT("identity.ai.%d"), Slot));
    }
    return Out;
}

bool LDEvaluationSeeds::Arm(uint64 Root, FString& Error)
{
    if (!IsSwitchPresent())
    {
        Error = TEXT("Build evaluation seeds need a process started with -LDBuildEvaluation.");
        return false;
    }
    LDEvaluationSeedsDetail::ArmedSeeds = Derive(Root);
    UE_LOG(LogLDTest, Display, TEXT("LD_EVAL_SEEDS_ARMED root=%llu derivation=%u"), Root, DerivationVersion);
    return true;
}

void LDEvaluationSeeds::Disarm()
{
    LDEvaluationSeedsDetail::ArmedSeeds.Reset();
}

const FLDEvaluationSeeds* LDEvaluationSeeds::Armed()
{
    return IsSwitchPresent() && LDEvaluationSeedsDetail::ArmedSeeds.IsSet() ? &LDEvaluationSeedsDetail::ArmedSeeds.GetValue() : nullptr;
}

FGuid LDEvaluationSeeds::NextSessionId()
{
    if (!IsSwitchPresent()) { return FGuid::NewGuid(); }
    return StreamGuid(0, *FString::Printf(TEXT("identity.session.%u"), LDEvaluationSeedsDetail::SessionsIssued++));
}
#endif
