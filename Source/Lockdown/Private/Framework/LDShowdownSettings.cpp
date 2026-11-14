#include "Framework/LDShowdownSettings.h"
#include "Development/LDDemoContentDefinitions.h"

bool FLDShowdownParameters::FromContent(const FLDDemoContentParameters& Content, FLDShowdownParameters& Out, FString& Error)
{
    FLDShowdownParameters Value;
    Value.UnlockDelayMs = Content.ShowdownUnlockDelayMs;
    Value.SprayMs = Content.CaptureSprayMs;
    Value.GuardMs = Content.CaptureGuardMs;
    Value.DurationMs = Content.ShowdownDurationMs;
    Value.RespawnLimit = Content.ShowdownRespawnLimit;
    Value.FirstRespawnMs = Content.ShowdownFirstRespawnMs;
    Value.SecondRespawnMs = Content.ShowdownSecondRespawnMs;
    Value.RespawnCutoffMs = Content.ShowdownRespawnCutoffMs;
    Value.ClosureStartsMs = Content.ShowdownClosureStartsMs;
    if (!Value.Validate(Error)) { return false; }
    Out = Value;
    return true;
}

bool FLDShowdownParameters::Validate(FString& Error) const
{
    if (UnlockDelayMs < 0 || SprayMs <= 0 || GuardMs <= 0 || DurationMs <= UnlockDelayMs || DurationMs > MAX_int32
        || SprayMs > DurationMs || GuardMs > DurationMs || UnlockDelayMs + SprayMs + GuardMs > DurationMs
        || RespawnLimit < 0 || RespawnLimit > 2 || FirstRespawnMs <= 0 || SecondRespawnMs <= 0
        || RespawnCutoffMs <= 0 || RespawnCutoffMs >= DurationMs
        || ClosureStartsMs < UnlockDelayMs || ClosureStartsMs >= DurationMs)
    {
        Error = TEXT("Invalid final-showdown objective, respawn or street-closure configuration.");
        return false;
    }
    Error.Reset();
    return true;
}

int64 FLDShowdownParameters::RespawnWaitMs(int32 QueuedCount, int64 ElapsedMs) const
{
    if (ElapsedMs < 0 || ElapsedMs >= RespawnCutoffMs || QueuedCount < 0 || QueuedCount >= RespawnLimit) { return 0; }
    return QueuedCount == 0 ? FirstRespawnMs : SecondRespawnMs;
}
