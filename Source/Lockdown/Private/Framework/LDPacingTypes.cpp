#include "Framework/LDPacingTypes.h"
#include "Development/LDDemoContentDefinitions.h"
#include "World/LDAirdrop.h"

namespace LDPacingDetail
{
const TCHAR* const DailyIds[] = {TEXT("none"), TEXT("dm_rain"), TEXT("dm_cold"), TEXT("dm_blackout"), TEXT("dm_typhoon"),
    TEXT("dm_heat"), TEXT("dm_sale"), TEXT("dm_delivery"), TEXT("dm_patrol"), TEXT("dm_exam"), TEXT("dm_gym"), TEXT("dm_overtime"),
    TEXT("dm_rumor")};
const TCHAR* const AffixIds[] = {TEXT("none"), TEXT("ca_overtime"), TEXT("ca_teambuild"), TEXT("ca_fridge"), TEXT("ca_bonus"),
    TEXT("ca_layoff"), TEXT("ca_audit")};
static_assert(UE_ARRAY_COUNT(DailyIds) == LDPacing::DailyModifiers + 1, "one id per daily modifier");
static_assert(UE_ARRAY_COUNT(AffixIds) == LDPacing::CampAffixes + 1, "one id per camp affix");
}

const TCHAR* LDPacing::DailyId(ELDDailyModifier Modifier)
{
    const int32 Index = int32(Modifier);
    return Index >= 0 && Index <= DailyModifiers ? LDPacingDetail::DailyIds[Index] : TEXT("none");
}

const TCHAR* LDPacing::AffixId(ELDCampAffix Affix)
{
    const int32 Index = int32(Affix);
    return Index >= 0 && Index <= CampAffixes ? LDPacingDetail::AffixIds[Index] : TEXT("none");
}

ELDDailyModifier LDPacing::DailyFromId(const FString& Id)
{
    for (int32 Index = 1; Index <= DailyModifiers; ++Index)
    {
        if (Id.Equals(LDPacingDetail::DailyIds[Index], ESearchCase::CaseSensitive)) { return ELDDailyModifier(Index); }
    }
    return ELDDailyModifier::None;
}

void LDPacing::DrawDailyModifiers(uint64 Seed, int32 Count, TArray<ELDDailyModifier>& Out)
{
    Out.Reset();
    TArray<int32> Pool;
    for (int32 Index = 1; Index <= DailyModifiers; ++Index) { Pool.Add(Index); }
    // Fisher-Yates prefix over the seeded stream: every subset of Count is equally likely.
    uint64 State = Seed;
    const int32 Take = FMath::Clamp(Count, 0, DailyModifiers);
    for (int32 Index = 0; Index < Take; ++Index)
    {
        State = LDAirdrop::Mix(State + 0x9E3779B97F4A7C15ull);
        const int32 Pick = Index + int32(State % uint64(Pool.Num() - Index));
        Pool.Swap(Index, Pick);
        Out.Add(ELDDailyModifier(Pool[Index]));
    }
    Out.Sort();
}

int64 LDPacing::FieldRespawnWaitMs(const FLDDemoContentParameters& Parameters, int32 Level, bool bExamWeek)
{
    const int64 Wait = Parameters.RespawnBaseMs + Parameters.RespawnPerLevelMs * FMath::Max(Level, 0);
    return Wait + (bExamWeek ? Parameters.Daily.ExamRespawnBonusMs : 0);
}

ELDCampAffix LDPacing::RollAffix(const FLDDemoContentParameters& Parameters, bool bLowTier, bool bOvertimeDay, int64 Roll, int32 Pick)
{
    // Overtime day: every camp, Low ones included, spawns working overtime.
    if (bOvertimeDay) { return ELDCampAffix::Overtime; }
    if (bLowTier) { return ELDCampAffix::None; }
    if (Roll < 0 || Roll >= Parameters.Affix.ChanceBp || Pick < 0 || Pick >= CampAffixes) { return ELDCampAffix::None; }
    return ELDCampAffix(Pick + 1);
}

int32 LDPacing::SalePrice(const FLDDemoContentParameters& Parameters, int32 Price, bool bSaleDay)
{
    if (!bSaleDay || Price <= 0) { return Price; }
    const int64 Scaled = (int64(Price) * Parameters.Daily.SalePriceScaleBp + 9999) / 10000;
    return int32(FMath::Max<int64>(1, Scaled));
}
