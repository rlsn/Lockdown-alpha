#pragma once

#include "CoreMinimal.h"
#include "LDShowdownSettings.generated.h"

struct FLDDemoContentParameters;

USTRUCT()
struct LOCKDOWN_API FLDShowdownView
{
    GENERATED_BODY()
    UPROPERTY() bool bStarted = false;
    UPROPERTY() int64 StartedAtMs = 0;
    UPROPERTY() int64 EndsAtMs = 0;
    UPROPERTY() int64 RespawnsCloseAtMs = 0;
    UPROPERTY() int64 ClosureAtMs = 0;
    UPROPERTY() int64 SprayMs = 0;
    UPROPERTY() int64 GuardMs = 0;
    UPROPERTY() FVector CentreCm = FVector::ZeroVector;
    UPROPERTY() double InitialRadiusCm = 0;
    UPROPERTY() double FinalRadiusCm = 0;
};

namespace LDShowdown
{
    inline double StreetRadiusCm(const FLDShowdownView& View, int64 NowMs)
    {
        const double Alpha = View.EndsAtMs > View.ClosureAtMs
            ? FMath::Clamp(double(NowMs - View.ClosureAtMs) / double(View.EndsAtMs - View.ClosureAtMs), 0., 1.) : 0.;
        return FMath::Lerp(View.InitialRadiusCm, View.FinalRadiusCm, Alpha);
    }
}

struct LOCKDOWN_API FLDShowdownParameters
{
    int64 UnlockDelayMs = 0;
    int64 SprayMs = 0;
    int64 GuardMs = 0;
    int64 DurationMs = 0;
    int64 RespawnLimit = 0;
    int64 FirstRespawnMs = 0;
    int64 SecondRespawnMs = 0;
    int64 RespawnCutoffMs = 0;
    int64 ClosureStartsMs = 0;

    static bool FromContent(const FLDDemoContentParameters& Content, FLDShowdownParameters& Out, FString& Error);
    bool Validate(FString& Error) const;
    int64 RespawnWaitMs(int32 QueuedCount, int64 ElapsedMs) const;
};
