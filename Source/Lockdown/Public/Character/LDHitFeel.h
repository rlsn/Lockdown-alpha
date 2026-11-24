#pragma once

#include "CoreMinimal.h"

// The attacker's hit confirmation and the impact effect (APP_SPEC 8). When the owner's own receipt of a landed hit
// arrives (FLDCombatCueFacts LastDealt*), the own body holds its striking pose, its hand flares, its camera kicks along
// its facing and an onomatopoeia pops in front of it; every visible landed hit bursts into impact particles at the body it
// struck (LDImpactFx). Presentation only, on the local client; it never pauses input, movement or authority time.
// Every amount is a client console variable (ld.Feel.*) that the DEV test panel and the settings page's effects tab step
// live; the values last for the running game only and start from playtest starting points, not approved values.
namespace LDHitFeel
{
    struct FTuning
    {
        float StopSeconds = 0;
        float CritStopSeconds = 0;
        float KillStopSeconds = 0;
        float BreakStopSeconds = 0;
        float KickCm = 0;
        float KickSeconds = 0;
        float KillKickScale = 0;
        float SparkScale = 0;
        float CritSparkScale = 0;
        float ImpactScale = 0;
        int32 ImpactSparks = 0;
        float ImpactSpeed = 0;
        float ImpactGlow = 0;
        float ImpactSeconds = 0;
        bool bWords = false;
        bool bPredictSwing = false;
        int64 PredictGraceMs = 0;
        float TrailWidthCm = 0;
        float TrailSeconds = 0;
        float TrailGlow = 0;
        float TrailReach = 0;
        float CastScale = 0;
        float CastGlow = 0;
        float CastSparksPerSecond = 0;
        float SwingVolume = 0;
        float ConfirmVolume = 0;
    };
    LOCKDOWN_API FTuning Read();

    enum class EUnit : uint8 { Ms, Cm, Scale, Count, Speed, Switch };
    // One tunable as the effects tab lists it, in FTuning order: its section, its starting value, its step and the upper
    // end of its range (the lower end is 0).
    struct FField
    {
        const TCHAR* ConsoleName;
        const TCHAR* Label;
        const TCHAR* Section;
        float Default;
        float Step;
        float Max;
        EUnit Unit;
    };
    LOCKDOWN_API TConstArrayView<FField> Fields();
    LOCKDOWN_API float Value(int32 Index);
    // The field's current value with its unit ("60 毫秒", "1.4×", "开").
    LOCKDOWN_API FString Current(int32 Index);
    // Moves the field one step up (+1) or down (-1) within [0, Max]; false when it is already at that end.
    LOCKDOWN_API bool Step(int32 Index, int32 Direction);
    // Every field back to its starting value.
    LOCKDOWN_API void ResetAll();
    LOCKDOWN_API bool IsAllDefault();

    // Advance the receipt watermark only for a newer, non-future receipt. A stale receipt is primed without playback.
    constexpr int64 FreshMs = 500;
    LOCKDOWN_API bool TakeFreshReceipt(int64& SeenAtMs, int64 ReceiptAtMs, int64 NowMs);

    // The weight of one receipt by its flags (LDObservation::EHitFlags plus LDCombatCues::DealtLethal), heaviest first:
    // a kill holds longest and kicks hardest, then a critical hit, then a broken shield, then an ordinary hit.
    enum class ETier : uint8 { Normal, ShieldBreak, Critical, Kill };
    LOCKDOWN_API ETier TierOf(uint8 HitFlags);
    struct FConfirm
    {
        float StopSeconds = 0;
        float SparkScale = 0;
        float KickCm = 0;
        ETier Tier = ETier::Normal;
    };
    LOCKDOWN_API FConfirm ConfirmOf(const FTuning& Tuning, uint8 HitFlags);
    // The onomatopoeia of one receipt: "啪！", a shield's "铛！", "破盾！", "暴击！" and a kill's "终结！".
    LOCKDOWN_API FString Word(uint8 HitFlags);

    // The own swing prediction (presentation only): while the accepted attack order stands in reach of its target and
    // the basic attack's cadence is due, the own body starts its swing at once instead of a round trip later. The
    // predicted pose holds short of its impact frame until the authority's start of that swing confirms it; with no
    // start within GraceMs (the round trip plus PredictGraceMs), a cast, dash or block, a lost view or a disengaged
    // order it is withdrawn and the pose rewinds to rest (LDBodyMotion::RetractAge). Never sends or delays a request.
    struct FSwingFacts
    {
        bool bEnabled = false;
        bool bCurrent = false;       // the own view of the current life at a known authority clock
        bool bBlocked = false;       // a status blocks basic attacks
        bool bBusy = false;          // another action winds up or a dash runs
        bool bEngaged = false;       // ULDNavigationComponent::ReadBasicEngagement
        int64 NowMs = 0;
        int64 NextAttackAtMs = 0;    // the committed cadence; the swing it allows is the prediction's key
        int64 LastBasicStartMs = 0;  // the latest authority start of a basic attack this life, 0 = none
        int64 GraceMs = 0;
    };
    struct FSwingPrediction
    {
        bool bActive = false;
        int64 KeyAtMs = 0;
        int64 StartedAtMs = 0;
        int64 SpentKeyAtMs = -1;     // the last key confirmed or withdrawn; never predicted again while engaged
    };
    enum class ESwingStep : uint8 { None, Started, Confirmed, Withdrawn };
    LOCKDOWN_API ESwingStep AdvanceSwing(FSwingPrediction& State, const FSwingFacts& Facts);
    // The predicted pose's age: it plays on and holds at SwingHoldFraction of the windup, before the release flash
    // (LDBodyMotion::Evaluate starts it at 80% of the impact age).
    constexpr float SwingHoldFraction = .75f;
    LOCKDOWN_API float SwingHoldSeconds(int64 WindupMs);
    LOCKDOWN_API const TCHAR* SwingStepName(ESwingStep Step);
}
