#include "Combat/LDProtoFlags.h"
#include "Core/LDLog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace LDProtoDetail
{
    // An optional seconds switch in (Min, Max]; untouched when absent.
    bool Seconds(const TCHAR* CommandLine, const TCHAR* Key, double Min, double Max, int64& OutMs, FString& Error)
    {
        FString Text;
        if (!FParse::Value(CommandLine, Key, Text)) { return true; }
        const double Value = FCString::Atod(*Text);
        if (!Text.IsNumeric() || !FMath::IsFinite(Value) || Value <= Min || Value > Max)
        {
            Error = FString::Printf(TEXT("-%s%s is not a number of seconds in (%g, %g]."), Key, *Text, Min, Max);
            return false;
        }
        OutMs = FMath::RoundToInt64(Value * 1000.);
        return true;
    }
}

bool LDProto::Parse(const TCHAR* CommandLine, FLDProtoSettings& Out, FString& Error)
{
    Out = FLDProtoSettings();
    Out.bAgency = FParse::Param(CommandLine, TEXT("LDProtoAgency"));
    FLDAgencyParams& Agency = Out.Agency;
    FString Cut;
    if (FParse::Value(CommandLine, TEXT("LDProtoAgencyCut="), Cut))
    {
        if (Cut == TEXT("Immediate")) { Agency.bCut = true; }
        else if (Cut == TEXT("NextControl")) { Agency.bCut = false; }
        else { Error = TEXT("-LDProtoAgencyCut= takes Immediate or NextControl."); return false; }
    }
    if (!LDProtoDetail::Seconds(CommandLine, TEXT("LDProtoAgencyWindow="), 0., 60., Agency.WindowMs, Error)
        || !LDProtoDetail::Seconds(CommandLine, TEXT("LDProtoAgencyThreshold="), 0., 60., Agency.ThresholdMs, Error)
        || !LDProtoDetail::Seconds(CommandLine, TEXT("LDProtoAgencyGuard="), 0., 60., Agency.GuardMs, Error)
        || !LDProtoDetail::Seconds(CommandLine, TEXT("LDProtoAgencyCooldown="), 0., 600., Agency.CooldownMs, Error))
    { return false; }
    if (Agency.ThresholdMs > Agency.WindowMs)
    { Error = TEXT("-LDProtoAgencyThreshold= cannot exceed the window."); return false; }
    int32 RollFrom = Agency.RollFrom;
    if (FParse::Value(CommandLine, TEXT("LDProtoAgencyRollFrom="), RollFrom))
    {
        if (RollFrom < 1 || RollFrom > 16) { Error = TEXT("-LDProtoAgencyRollFrom= takes 1..16."); return false; }
        Agency.RollFrom = RollFrom;
    }
    return LDProtoDetail::Seconds(CommandLine, TEXT("LDProtoPotDelay="), 0., 5., Out.PotDelayMs, Error);
}

const FLDProtoSettings& LDProto::Settings()
{
    static const FLDProtoSettings Read = []
    {
        FLDProtoSettings Out;
        FString Error;
        if (!Parse(FCommandLine::Get(), Out, Error))
        {
            UE_LOG(LogLDCombat, Error, TEXT("LD_PROTO_INVALID %s No prototype is enabled."), *Error);
            return FLDProtoSettings();
        }
        if (Out.Any()) { UE_LOG(LogLDCombat, Display, TEXT("LD_PROTO %s"), *Describe(Out)); }
        return Out;
    }();
    return Read;
}

FString LDProto::Describe(const FLDProtoSettings& Settings)
{
    TArray<FString> Flags, Params;
    if (Settings.bAgency)
    {
        const FLDAgencyParams& A = Settings.Agency;
        Flags.Add(TEXT("Agency"));
        Params.Add(FString::Printf(TEXT("agency_cut=%s agency_window_ms=%lld agency_threshold_ms=%lld agency_guard_ms=%lld agency_cooldown_ms=%lld agency_roll_from=%d"),
            A.bCut ? TEXT("Immediate") : TEXT("NextControl"), A.WindowMs, A.ThresholdMs, A.GuardMs, A.CooldownMs, A.RollFrom));
    }
    if (Settings.PotDelayMs >= 0)
    {
        Flags.Add(TEXT("PotDelay"));
        Params.Add(FString::Printf(TEXT("pot_delay_ms=%lld"), Settings.PotDelayMs));
    }
    return FString::Printf(TEXT("flags=%s params=%s"), *FString::Join(Flags, TEXT(",")), *FString::Join(Params, TEXT(" ")));
}

FName LDProto::PotSkillId()
{
    static const FName Id(TEXT("active_quick_meeting"));
    return Id;
}
