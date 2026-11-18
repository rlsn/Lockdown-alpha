#pragma once

#include "CoreMinimal.h"
#include "LDPacingTypes.generated.h"

struct FLDDemoContentParameters;

// Spec §11.1 (P6) daily modifiers (今日热点): Count of these twelve are drawn from the match's private seed when the
// match begins and hold for the whole match. The numbers live in content (DAILY.*).
UENUM()
enum class ELDDailyModifier : uint8
{
    None = 0, Rain = 1, Cold = 2, Blackout = 3, Typhoon = 4, Heat = 5, Sale = 6, Delivery = 7, Patrol = 8, Exam = 9, Gym = 10,
    Overtime = 11, Rumor = 12
};

// Spec §11.2 (P6) camp affixes: a High, Elite or Ancient camp spawn carries at most one (AFFIX.*).
UENUM()
enum class ELDCampAffix : uint8 { None = 0, Overtime = 1, Teambuild = 2, Fridge = 3, Bonus = 4, Layoff = 5, Audit = 6 };

// Spec §11.3 vending machine items: a 30 s buff (attack speed, in-combat momentum or move speed) or the element drink (the
// next basic attacks carry the chosen element).
UENUM()
enum class ELDVendingItem : uint8 { None = 0, AttackSpeed = 1, Momentum = 2, MoveSpeed = 3, Drink = 4 };

// Owner-sent result of a spawn choice (ALDPlayerController::ServerChooseSpawn -> ClientChooseSpawn).
UENUM()
enum class ELDSpawnChoiceResult : uint8 { None = 0, Chosen = 1, NotContender = 2, StaleMatch = 3, StaleLife = 4, NoChoice = 5, OutOfRange = 6 };

// Owner-sent result of a vending purchase (ALDPlayerController::ServerVending -> ClientVending). Queued: accepted for the
// next Transactions boundary, which pays and applies it (or refuses with the reason logged).
UENUM()
enum class ELDVendingResult : uint8
{
    None = 0, Queued = 1, NotContender = 2, StaleMatch = 3, NotAlive = 4, NotAtMachine = 5, NotEnoughVouchers = 6, BadItem = 7,
    Busy = 8
};

namespace LDPacing
{
    constexpr int32 DailyModifiers = 12;
    constexpr int32 CampAffixes = 6;
    // The death screen shows at most this many spawn spots.
    constexpr int32 MaxSpawnChoices = 4;
    // The choice row shows at most five cards (the arena, the dealer and the bonus affix use all five).
    constexpr int32 MaxCardOfferSize = 5;

    // Stable text ids (dm_rain..dm_rumor, ca_overtime..ca_audit) for logs, icons and the DEV panel.
    LOCKDOWN_API const TCHAR* DailyId(ELDDailyModifier Modifier);
    LOCKDOWN_API const TCHAR* AffixId(ELDCampAffix Affix);
    LOCKDOWN_API ELDDailyModifier DailyFromId(const FString& Id);

    // Count distinct modifiers drawn uniformly without replacement from Seed, sorted by enum value.
    LOCKDOWN_API void DrawDailyModifiers(uint64 Seed, int32 Count, TArray<ELDDailyModifier>& Out);
    inline bool HasDaily(const TArray<ELDDailyModifier>& Active, ELDDailyModifier Modifier) { return Active.Contains(Modifier); }

    // Spec §10.4: a field respawn waits LIFE.RespawnBaseSec + LIFE.RespawnPerLevelSec x level, plus DAILY.ExamRespawnBonusSec
    // on exam week.
    LOCKDOWN_API int64 FieldRespawnWaitMs(const FLDDemoContentParameters& Parameters, int32 Level, bool bExamWeek);

    // Spec §11.2: the affix of a camp spawn: Overtime for every camp on overtime day; otherwise none for a Low camp and,
    // with probability AFFIX.Chance, one of the six uniformly. Roll and Pick are independent uniform draws in [0, 10000)
    // and [0, 6).
    LOCKDOWN_API ELDCampAffix RollAffix(const FLDDemoContentParameters& Parameters, bool bLowTier, bool bOvertimeDay, int64 Roll,
        int32 Pick);

    // Price after the sale day (half, rounded up), never below one voucher.
    LOCKDOWN_API int32 SalePrice(const FLDDemoContentParameters& Parameters, int32 Price, bool bSaleDay);
}
