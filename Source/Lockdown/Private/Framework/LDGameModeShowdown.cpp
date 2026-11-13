#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapRegistry.h"
#include "Engine/World.h"

bool ALDGameMode::BeginShowdown(const FLDGameplayWritePermit& Permit, FString& Error)
{
    const auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    const auto* Settings = &ShowdownRules;
    const auto* Capture = Registry ? Registry->Resolve(TEXT("final_capture"), Registry->Generation) : nullptr;
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None
        || !DemoContent || !Capture || !Capture->Floor.IsValid() || !Settings->Validate(Error)
        || !FLDPKParameters::FromContent(DemoContent->Get().Parameters, ShowdownRing, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Final showdown requires its registered arena, configuration and entry permit."); }
        return false;
    }
    CaptureCircle.CaptureId = TEXT("final_capture");
    CaptureCircle.CenterCm = FVector2D(Capture->Position);
    CaptureCircle.Ground = Capture->Floor;
    Showdown = {};
    Showdown.bStarted = true;
    Showdown.StartedAtMs = Permit.GetBoundary().AtMs;
    if (!LDMatchLifecycle::AddDurationMs(Showdown.StartedAtMs, Settings->DurationMs, Showdown.EndsAtMs, Error)
        || !LDMatchLifecycle::AddDurationMs(Showdown.StartedAtMs, Settings->RespawnCutoffMs, Showdown.RespawnsCloseAtMs, Error)
        || !LDMatchLifecycle::AddDurationMs(Showdown.StartedAtMs, Settings->ClosureStartsMs, Showdown.ClosureAtMs, Error))
    { return false; }
    Showdown.CentreCm = Capture->Position;
    Showdown.SprayMs = Settings->SprayMs;
    Showdown.GuardMs = Settings->GuardMs;
    Showdown.InitialRadiusCm = ShowdownRing.ArenaRadiusCm;
    Showdown.FinalRadiusCm = ShowdownRing.MidRadiusCm;
    // Reuse the proven swept-foot environmental ring, without PK's last-survivor judge or rewards.
    ShowdownRing.ShrinkStartMs = 0;
    ShowdownRing.MaxMs = Settings->DurationMs - Settings->ClosureStartsMs;
    ShowdownRing.MidMs = ShowdownRing.MaxMs / 2;
    ShowdownRing.FinalMs = ShowdownRing.MaxMs;
    ShowdownRing.FinalRadiusCm = Showdown.FinalRadiusCm;
    ShowdownRing.MidRadiusCm = (Showdown.InitialRadiusCm + Showdown.FinalRadiusCm) * .5;
    ShowdownRingSettledThroughMs = Showdown.ClosureAtMs;
    ShowdownRingTickAtMs = Showdown.EndsAtMs;
    if (ShowdownRing.TickMs < ShowdownRing.MaxMs
        && !LDMatchLifecycle::AddDurationMs(Showdown.ClosureAtMs, ShowdownRing.TickMs, ShowdownRingTickAtMs, Error))
    { return false; }
    ShowdownRespawns.Reset();
    ShowdownBurn.Reset();
    bShowdownClosePending = false;
    int64 OpensAt = 0;
    if (!LDMatchLifecycle::AddDurationMs(Showdown.StartedAtMs, Settings->UnlockDelayMs, OpensAt, Error)
        || !CaptureReducer.ScheduleOpen(OpensAt, Error)) { return false; }
    UE_LOG(LogLDMatch, Display, TEXT("LD_SHOWDOWN_BEGIN generation=%u at_ms=%lld opens_at_ms=%lld ends_at_ms=%lld"),
        MatchContext.Generation, Showdown.StartedAtMs, OpensAt, Showdown.EndsAtMs);
    return true;
}

int64 ALDGameMode::NextShowdownDueMs() const
{
    if (!IsGameplayWriteOpen(MatchContext) || !Showdown.bStarted) { return MAX_int64; }
    int64 Due = Showdown.EndsAtMs;
    for (const int64 At : {Showdown.RespawnsCloseAtMs, Showdown.ClosureAtMs, ShowdownRingTickAtMs})
    {
        if (At > AuthorityNowMs) { Due = FMath::Min(Due, At); }
    }
    return Due;
}

bool ALDGameMode::AdvanceShowdownEffects(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!Showdown.bStarted) { Error.Reset(); return true; }
    if (ValidateWritePermit(Permit, ELDCommitPhase::Effects) != ELDCommitReject::None || !Combat)
    {
        Error = TEXT("Street closure needs the current Effects permit and combat authority.");
        return false;
    }
    const int64 Now = Permit.GetBoundary().AtMs;
    if (Now <= ShowdownRingSettledThroughMs) { Error.Reset(); return true; }
    const int64 From = ShowdownRingSettledThroughMs - Showdown.ClosureAtMs;
    const int64 To = Now - Showdown.ClosureAtMs;
    for (const FGuid& Id : RosterOrder)
    {
        FLDCombatBodyRef Body;
        FVector Foot;
        if (!ReadLivingBody(Id, Body, Foot)) { continue; }
        const FLDNativeMotionBatch* Motion = ReadStepMotion(Id);
        TArray<LDPKRules::FFootSegment> Path;
        TArray<TPair<int64, int64>> Outside;
        int64 Hp = 0, MaxHp = 0;
        if (!Motion || !Combat->ReadBodyHealth(Body, Hp, MaxHp)
            || !LDPKRules::FootPathFromMotion(*Motion, Showdown.ClosureAtMs, From, To, Path)
            || !LDPKRules::OutsideRingIntervals(ShowdownRing, Showdown.CentreCm, Path, Outside))
        {
            Error = TEXT("Final street closure requires consistent authoritative living-body motion.");
            return false;
        }
        auto& Burn = ShowdownBurn.FindOrAdd(Id);
        if (Burn.Life != Body.Victim.Life) { Burn = {Body.Victim.Life, 0}; }
        for (const auto& Interval : Outside)
        {
            if (!LDPKRules::AccumulateRingDamage(ShowdownRing, MaxHp, Interval.Key, Interval.Value, Burn.AccumulatorE7))
            {
                Error = TEXT("Final street damage overflowed.");
                return false;
            }
        }
    }
    ShowdownRingSettledThroughMs = Now;
    if (Now < ShowdownRingTickAtMs) { Error.Reset(); return true; }
    const int64 NextTick = LDPKRules::NextRingTickMs(ShowdownRing, To);
    ShowdownRingTickAtMs = Showdown.EndsAtMs;
    if (NextTick < ShowdownRing.MaxMs
        && !LDMatchLifecycle::AddDurationMs(Showdown.ClosureAtMs, NextTick, ShowdownRingTickAtMs, Error)) { return false; }
    for (auto& Pair : ShowdownBurn)
    {
        FLDCombatBodyRef Body;
        FVector Foot;
        const int64 Amount = LDPKRules::TakeRingDamageP2(Pair.Value.AccumulatorE7);
        if (Amount > 0 && ReadLivingBody(Pair.Key, Body, Foot) && Body.Victim.Life == Pair.Value.Life
            && !Combat->ApplyEnvironmentDamage(Body, Amount, Permit, Error)) { return false; }
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::CloseShowdownIfDue(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!Showdown.bStarted) { return true; }
    const int64 Now = Permit.GetBoundary().AtMs;
    bool bCanContinue = false;
    for (const FGuid& Id : RosterOrder)
    {
        const FLDLifeRecord* Life = Lives.Find(Id);
        if (IsMemberPresent(Id) && Life
            && (Life->Phase == ELDLifePhase::Alive || (Life->Phase == ELDLifePhase::Dead && Life->RespawnAtMs > 0)))
        {
            bCanContinue = true;
            break;
        }
    }
    if (Now < Showdown.EndsAtMs && bCanContinue) { return true; }
    bShowdownClosePending = true;
    if (HasPendingRewardRetries()) { return true; }
    bShowdownClosePending = false;
    const FGuid Winner = bCanContinue ? CaptureReducer.TimeoutWinner() : FGuid();
    return TryCloseMatch(bCanContinue ? ELDMatchEndReason::ObjectiveTimeout : ELDMatchEndReason::NoChampion,
        Winner, FMath::Min(Now, Showdown.EndsAtMs), Error);
}
