#include "Character/LDHitFeel.h"
#include "Combat/LDCombatCues.h"
#include "HAL/IConsoleManager.h"
#include "Vision/LDViewTypes.h"

namespace
{
using LDHitFeel::EUnit;
const TCHAR* const StopSection = TEXT("命中停顿");
const TCHAR* const CameraSection = TEXT("镜头");
const TCHAR* const HandSection = TEXT("出手闪光");
const TCHAR* const ImpactSection = TEXT("命中粒子");
const TCHAR* const WordSection = TEXT("拟声字");
const TCHAR* const PredictSection = TEXT("出手预测");
const TCHAR* const TrailSection = TEXT("挥击轨迹");
const TCHAR* const CastSection = TEXT("施法聚能");
const TCHAR* const AudioSection = TEXT("攻击音效");
// Playtest starting points in FTuning order.
const LDHitFeel::FField HitFeelFields[] =
{
    { TEXT("ld.Feel.HitStopMs"), TEXT("普通命中"), StopSection, 60.f, 10.f, 300.f, EUnit::Ms },
    { TEXT("ld.Feel.CritHitStopMs"), TEXT("暴击"), StopSection, 100.f, 10.f, 300.f, EUnit::Ms },
    { TEXT("ld.Feel.KillHitStopMs"), TEXT("击杀"), StopSection, 160.f, 10.f, 400.f, EUnit::Ms },
    { TEXT("ld.Feel.BreakHitStopMs"), TEXT("破盾"), StopSection, 90.f, 10.f, 300.f, EUnit::Ms },
    { TEXT("ld.Feel.KickCm"), TEXT("推镜距离"), CameraSection, 6.f, 1.f, 30.f, EUnit::Cm },
    { TEXT("ld.Feel.KickMs"), TEXT("推镜时长"), CameraSection, 120.f, 10.f, 400.f, EUnit::Ms },
    { TEXT("ld.Feel.KillKickScale"), TEXT("击杀推镜倍数"), CameraSection, 2.f, .25f, 5.f, EUnit::Scale },
    { TEXT("ld.Feel.SparkScale"), TEXT("普通闪光"), HandSection, 1.4f, .1f, 4.f, EUnit::Scale },
    { TEXT("ld.Feel.CritSparkScale"), TEXT("暴击与击杀闪光"), HandSection, 2.2f, .1f, 4.f, EUnit::Scale },
    { TEXT("ld.Feel.ImpactScale"), TEXT("整体大小"), ImpactSection, 1.8f, .1f, 3.f, EUnit::Scale },
    { TEXT("ld.Feel.ImpactGlow"), TEXT("亮度"), ImpactSection, 6.f, 1.f, 30.f, EUnit::Scale },
    { TEXT("ld.Feel.ImpactMs"), TEXT("持续时间"), ImpactSection, 280.f, 20.f, 1000.f, EUnit::Ms },
    { TEXT("ld.Feel.Words"), TEXT("显示拟声字"), WordSection, 1.f, 1.f, 1.f, EUnit::Switch },
    { TEXT("ld.Feel.PredictSwing"), TEXT("本地预测普攻"), PredictSection, 1.f, 1.f, 1.f, EUnit::Switch },
    { TEXT("ld.Feel.PredictGraceMs"), TEXT("撤回前多等"), PredictSection, 150.f, 10.f, 600.f, EUnit::Ms },
    { TEXT("ld.Feel.TrailWidth"), TEXT("轨迹宽度"), TrailSection, 110.f, 5.f, 180.f, EUnit::Cm },
    { TEXT("ld.Feel.TrailMs"), TEXT("轨迹余韵"), TrailSection, 260.f, 20.f, 600.f, EUnit::Ms },
    { TEXT("ld.Feel.TrailGlow"), TEXT("轨迹亮度"), TrailSection, 2.f, 1.f, 30.f, EUnit::Scale },
    { TEXT("ld.Feel.TrailReach"), TEXT("轨迹延展"), TrailSection, 2.25f, .25f, 4.f, EUnit::Scale },
    { TEXT("ld.Feel.CastScale"), TEXT("聚能大小"), CastSection, 1.4f, .1f, 3.f, EUnit::Scale },
    { TEXT("ld.Feel.CastGlow"), TEXT("聚能亮度"), CastSection, 4.f, 1.f, 30.f, EUnit::Scale },
    { TEXT("ld.Feel.CastSparks"), TEXT("火星每秒"), CastSection, 80.f, 5.f, 120.f, EUnit::Count },
    { TEXT("ld.Feel.SwingVolume"), TEXT("挥击音量"), AudioSection, 1.f, .1f, 2.f, EUnit::Scale },
    { TEXT("ld.Feel.ConfirmVolume"), TEXT("命中音量"), AudioSection, 1.f, .1f, 2.f, EUnit::Scale },
};
constexpr int32 FieldCount = UE_ARRAY_COUNT(HitFeelFields);
float HitFeelValues[FieldCount];

struct FHitFeelRegistration
{
    FHitFeelRegistration()
    {
        for (int32 Index = 0; Index < FieldCount; ++Index)
        {
            HitFeelValues[Index] = HitFeelFields[Index].Default;
            IConsoleManager::Get().RegisterConsoleVariableRef(HitFeelFields[Index].ConsoleName, HitFeelValues[Index],
                *FString::Printf(TEXT("Hit feel (presentation only): %s / %s."), HitFeelFields[Index].Section, HitFeelFields[Index].Label));
        }
    }
} HitFeelRegistration;

float Clamped(int32 Index)
{
    const float Value = HitFeelValues[Index];
    return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, HitFeelFields[Index].Max) : 0.f;
}

void Assign(int32 Index, float Value)
{
    if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(HitFeelFields[Index].ConsoleName))
    { Variable->Set(Value, ECVF_SetByConsole); }
    else { HitFeelValues[Index] = Value; }
}
}

LDHitFeel::FTuning LDHitFeel::Read()
{
    FTuning Tuning;
    Tuning.StopSeconds = Clamped(0) / 1000.f;
    Tuning.CritStopSeconds = Clamped(1) / 1000.f;
    Tuning.KillStopSeconds = Clamped(2) / 1000.f;
    Tuning.BreakStopSeconds = Clamped(3) / 1000.f;
    Tuning.KickCm = Clamped(4);
    Tuning.KickSeconds = Clamped(5) / 1000.f;
    Tuning.KillKickScale = Clamped(6);
    Tuning.SparkScale = Clamped(7);
    Tuning.CritSparkScale = Clamped(8);
    Tuning.ImpactScale = Clamped(9);
    Tuning.ImpactGlow = Clamped(10);
    Tuning.ImpactSeconds = Clamped(11) / 1000.f;
    Tuning.bWords = Clamped(12) >= .5f;
    Tuning.bPredictSwing = Clamped(13) >= .5f;
    Tuning.PredictGraceMs = FMath::RoundToInt64(Clamped(14));
    Tuning.TrailWidthCm = Clamped(15);
    Tuning.TrailSeconds = Clamped(16) / 1000.f;
    Tuning.TrailGlow = Clamped(17);
    Tuning.TrailReach = Clamped(18);
    Tuning.CastScale = Clamped(19);
    Tuning.CastGlow = Clamped(20);
    Tuning.CastSparksPerSecond = Clamped(21);
    Tuning.SwingVolume = Clamped(22);
    Tuning.ConfirmVolume = Clamped(23);
    return Tuning;
}


TConstArrayView<LDHitFeel::FField> LDHitFeel::Fields()
{
    return MakeArrayView(HitFeelFields);
}

float LDHitFeel::Value(int32 Index)
{
    return Index >= 0 && Index < FieldCount ? Clamped(Index) : 0.f;
}

FString LDHitFeel::Current(int32 Index)
{
    if (Index < 0 || Index >= FieldCount) { return FString(); }
    const float Shown = Clamped(Index);
    switch (HitFeelFields[Index].Unit)
    {
    case EUnit::Ms: return FString::Printf(TEXT("%.0f 毫秒"), Shown);
    case EUnit::Cm: return FString::Printf(TEXT("%.0f 厘米"), Shown);
    case EUnit::Count: return FString::Printf(TEXT("%.0f 个"), Shown);
    case EUnit::Speed: return FString::Printf(TEXT("%.0f cm/s"), Shown);
    case EUnit::Switch: return Shown >= .5f ? TEXT("开") : TEXT("关");
    default: return FString::Printf(TEXT("%.2g×"), Shown);
    }
}

bool LDHitFeel::Step(int32 Index, int32 Direction)
{
    if (Index < 0 || Index >= FieldCount || Direction == 0) { return false; }
    const FField& Field = HitFeelFields[Index];
    const float Now = Clamped(Index);
    // Snap to the step grid so repeated steps never accumulate float drift.
    const float Next = FMath::Clamp(FMath::RoundToFloat(Now / Field.Step + (Direction > 0 ? 1.f : -1.f)) * Field.Step, 0.f, Field.Max);
    if (FMath::IsNearlyEqual(Next, Now)) { return false; }
    Assign(Index, Next);
    return true;
}

void LDHitFeel::ResetAll()
{
    for (int32 Index = 0; Index < FieldCount; ++Index) { Assign(Index, HitFeelFields[Index].Default); }
}

bool LDHitFeel::IsAllDefault()
{
    for (int32 Index = 0; Index < FieldCount; ++Index)
    {
        if (!FMath::IsNearlyEqual(Clamped(Index), HitFeelFields[Index].Default)) { return false; }
    }
    return true;
}

bool LDHitFeel::TakeFreshReceipt(int64& SeenAtMs, int64 ReceiptAtMs, int64 NowMs)
{
    if (ReceiptAtMs <= 0 || ReceiptAtMs <= SeenAtMs || NowMs < ReceiptAtMs) { return false; }
    SeenAtMs = ReceiptAtMs;
    return NowMs - ReceiptAtMs < FreshMs;
}

LDHitFeel::ETier LDHitFeel::TierOf(uint8 HitFlags)
{
    if (HitFlags & LDCombatCues::DealtLethal) { return ETier::Kill; }
    // #305: an overdrive blow lands with a critical hit's weight.
    if (HitFlags & (LDObservation::Critical | LDCombatCues::DealtOverdrive)) { return ETier::Critical; }
    if (HitFlags & LDObservation::ShieldBroken) { return ETier::ShieldBreak; }
    return ETier::Normal;
}

LDHitFeel::FConfirm LDHitFeel::ConfirmOf(const FTuning& Tuning, uint8 HitFlags)
{
    FConfirm Confirm;
    Confirm.Tier = TierOf(HitFlags);
    switch (Confirm.Tier)
    {
    case ETier::Kill:
        Confirm.StopSeconds = Tuning.KillStopSeconds;
        Confirm.SparkScale = Tuning.CritSparkScale;
        Confirm.KickCm = Tuning.KickCm * Tuning.KillKickScale;
        break;
    case ETier::Critical:
        Confirm.StopSeconds = Tuning.CritStopSeconds;
        Confirm.SparkScale = Tuning.CritSparkScale;
        Confirm.KickCm = Tuning.KickCm * 1.5f;
        break;
    case ETier::ShieldBreak:
        Confirm.StopSeconds = Tuning.BreakStopSeconds;
        Confirm.SparkScale = Tuning.SparkScale;
        Confirm.KickCm = Tuning.KickCm * 1.25f;
        break;
    default:
        Confirm.StopSeconds = Tuning.StopSeconds;
        Confirm.SparkScale = Tuning.SparkScale;
        Confirm.KickCm = Tuning.KickCm;
        break;
    }
    return Confirm;
}

FString LDHitFeel::Word(uint8 HitFlags)
{
    switch (TierOf(HitFlags))
    {
    case ETier::Kill: return TEXT("终结！");
    case ETier::Critical: return (HitFlags & LDCombatCues::DealtOverdrive) ? TEXT("爆劲！") : TEXT("暴击！");
    case ETier::ShieldBreak: return TEXT("破盾！");
    default: return (HitFlags & LDObservation::ShieldAbsorbed) ? TEXT("铛！") : TEXT("啪！");
    }
}

LDHitFeel::ESwingStep LDHitFeel::AdvanceSwing(FSwingPrediction& State, const FSwingFacts& Facts)
{
    const int64 KeyAtMs = State.bActive ? State.KeyAtMs : Facts.NextAttackAtMs;
    // The authority starts a basic attack only once its cadence is due, so a start at or after the key is that swing.
    const bool bAuthorityStarted = Facts.LastBasicStartMs > 0 && Facts.LastBasicStartMs >= KeyAtMs;
    const bool bAllowed = Facts.bEnabled && Facts.bCurrent && !Facts.bBlocked && !Facts.bBusy && Facts.bEngaged;
    if (!Facts.bEngaged) { State.SpentKeyAtMs = -1; }
    if (State.bActive)
    {
        if (bAuthorityStarted)
        {
            State.bActive = false;
            State.SpentKeyAtMs = State.KeyAtMs;
            return ESwingStep::Confirmed;
        }
        const bool bLate = Facts.NowMs > State.StartedAtMs + FMath::Max<int64>(Facts.GraceMs, 0);
        if (!bAllowed || bLate)
        {
            State.bActive = false;
            // A disengaged swing may be predicted again once the order engages again; any other waits for a new cadence.
            State.SpentKeyAtMs = Facts.bEngaged ? State.KeyAtMs : -1;
            return ESwingStep::Withdrawn;
        }
        return ESwingStep::None;
    }
    if (!bAllowed || Facts.NowMs < Facts.NextAttackAtMs || Facts.NextAttackAtMs == State.SpentKeyAtMs || bAuthorityStarted)
    { return ESwingStep::None; }
    State.bActive = true;
    State.KeyAtMs = Facts.NextAttackAtMs;
    State.StartedAtMs = FMath::Max(Facts.NextAttackAtMs, Facts.NowMs);
    return ESwingStep::Started;
}

float LDHitFeel::SwingHoldSeconds(int64 WindupMs)
{
    return SwingHoldFraction * float(FMath::Max<int64>(WindupMs, 0)) / 1000.f;
}

const TCHAR* LDHitFeel::SwingStepName(ESwingStep Step)
{
    switch (Step)
    {
    case ESwingStep::Started: return TEXT("start");
    case ESwingStep::Confirmed: return TEXT("confirm");
    case ESwingStep::Withdrawn: return TEXT("withdraw");
    default: return TEXT("none");
    }
}
