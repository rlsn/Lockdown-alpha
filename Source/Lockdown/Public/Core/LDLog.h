#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"

struct FLDCombatSourceRef;

// Lockdown log categories. Every runtime line reads `LD_<TAG> key=value ...`; the tag and its fields are the contract that
// Scripts/SummarizePlaytestLog.py and the scenario runners read (docs/DEVELOPMENT.md, playtest troubleshooting). The category
// only groups lines so that one area can be silenced or raised with -LogCmds="LogLDNav Verbose" without touching the rest.
LOCKDOWN_API DECLARE_LOG_CATEGORY_EXTERN(LogLDMatch, Log, All);   // room, lobby, entry, match lifecycle, lives, capture, result
LOCKDOWN_API DECLARE_LOG_CATEGORY_EXTERN(LogLDCombat, Log, All);  // combat director, committed hits, combat inputs and receipts
LOCKDOWN_API DECLARE_LOG_CATEGORY_EXTERN(LogLDGrowth, Log, All);  // rewards, levels, three-choice offers, shop, ledger, DEV growth
LOCKDOWN_API DECLARE_LOG_CATEGORY_EXTERN(LogLDNav, Log, All);     // orders, path steering, native movement
LOCKDOWN_API DECLARE_LOG_CATEGORY_EXTERN(LogLDAI, Log, All);      // opponent AI decisions and summaries
LOCKDOWN_API DECLARE_LOG_CATEGORY_EXTERN(LogLDWorld, Log, All);   // camps, boss, airdrop, leader, map registry, vision relay
LOCKDOWN_API DECLARE_LOG_CATEGORY_EXTERN(LogLDNet, Log, All);     // frame time, hitches and connection quality summaries
LOCKDOWN_API DECLARE_LOG_CATEGORY_EXTERN(LogLDUI, Log, All);      // HUD, panels, local input and settings
LOCKDOWN_API DECLARE_LOG_CATEGORY_EXTERN(LogLDContent, Log, All); // approved content, art and audio catalogs
LOCKDOWN_API DECLARE_LOG_CATEGORY_EXTERN(LogLDTest, Log, All);    // DEV scenario probes and fixtures
// Once-per-second samples: the Host heartbeat of every contender and a client's own state line. The only periodic
// per-contender output, kept apart so -LogCmds="LogLDTrace off" removes it (and its sampling work) entirely.
LOCKDOWN_API DECLARE_LOG_CATEGORY_EXTERN(LogLDTrace, Log, All);

namespace LDLog
{
    // A free-text value for a key=value line: bare when it has no space, quote, '=' or control character, otherwise in
    // double quotes with backslash and quote escaped. Empty becomes "".
    LOCKDOWN_API FString Quote(const FString& Value);
    // Hundredths (the P2 resource unit) as a fixed two-decimal number: 12345 -> 123.45, -5 -> -0.05.
    LOCKDOWN_API FString P2(int64 Value);
    // Whole centimetres x,y,z.
    LOCKDOWN_API FString Cm(const FVector& Value);
    // The 32-digit form every LD line uses for identities; '-' for an invalid id.
    LOCKDOWN_API FString Id(const FGuid& Value);
    // Status id (ELDDemoStatusId) as a stable lower-case word; the number for an unknown id.
    LOCKDOWN_API FString StatusName(uint8 StatusId);
    // A combat source: basic, learned:<ability>, gear:<item>, innate:<id>, or none for a sourceless effect.
    LOCKDOWN_API FString Source(const FLDCombatSourceRef& Source);
    // The source revision of this build, read once per process: a packaged build reads the PackageManifest.json that
    // PackageWin64.ps1 writes beside its Archive (sourceRevision, sourceDirty); an uncooked run reads the work tree's git
    // HEAD (dirty state unknown). "unknown" when neither is found.
    LOCKDOWN_API void SourceRevision(FString& OutCommit, bool& bOutDirtyKnown, bool& bOutDirty);
    // The declared name of a reflected enum value (ELDKillCause::Contender -> Contender); the number when unnamed.
    template<typename TEnum> FString EnumName(TEnum Value)
    {
        const UEnum* Enum = StaticEnum<TEnum>();
        return Enum ? Enum->GetNameStringByValue(int64(Value)) : FString::Printf(TEXT("%lld"), int64(Value));
    }

    // At most one line per IntervalSeconds of the caller's clock. Lines held back in between are counted and handed to the
    // next allowed line, so a burst reads as one line with suppressed=N instead of N lines.
    struct LOCKDOWN_API FRateLimit
    {
        FRateLimit() = default;
        explicit FRateLimit(double InIntervalSeconds) : IntervalSeconds(InIntervalSeconds) {}
        double IntervalSeconds = 1;
        bool Allow(double NowSeconds, uint32& OutSuppressed);
    private:
        bool bAny = false;
        double LastAt = 0;
        uint32 Held = 0;
    };

    // Fixed-period sampling on a monotonic millisecond clock (the authority clock or real time): Due is true once for every
    // period boundary the clock reaches, never twice for one boundary and never for missed ones in between. A clock that
    // goes back (a new match starts its authority time again) restarts the gate at that instant.
    struct LOCKDOWN_API FPeriodicGate
    {
        FPeriodicGate() = default;
        explicit FPeriodicGate(int64 InPeriodMs) : PeriodMs(InPeriodMs) {}
        int64 PeriodMs = 1000;
        bool Due(int64 NowMs);
        void Reset() { bStarted = false; }
    private:
        bool bStarted = false;
        int64 LastMs = 0;
        int64 NextMs = 0;
    };
}
