#include "Framework/LDGameMode.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "Engine/World.h"

bool ALDGameMode::BeginShowdown(const FLDGameplayWritePermit& Permit, FString& Error)
{
    const auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    const auto* Settings = &ShowdownRules;
    const auto* Capture = Registry ? Registry->Resolve(TEXT("final_capture"), Registry->Generation) : nullptr;
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None
        || !DemoContent || !Capture || !Capture->Floor.IsValid() || !Settings->Validate(Error)
        || !FLDShowdownRing::FromContent(DemoContent->Get().Parameters, Settings->DurationMs - Settings->ClosureStartsMs, ShowdownRing, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Final showdown requires its registered arena, configuration and entry permit."); }
        return false;
    }
    CaptureCircle.CaptureId = TEXT("final_capture");
    CaptureCircle.CenterCm = FVector2D(Capture->Position);
    CaptureCircle.Ground = Capture->Floor;
    CaptureCircle.RadiusCm = CaptureBaseRadiusCm;
    CaptureCircle.bAnyGround = false;
    CaptureAnchorCm = CaptureCircle.CenterCm;
    CapturePoleCm = Capture->Position;
    ++CaptureCircleSerial;
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
    Showdown.InitialRadiusCm = ShowdownRing.InitialRadiusCm;
    Showdown.FinalRadiusCm = ShowdownRing.FinalRadiusCm;
    ShowdownRingSettledThroughMs = Showdown.ClosureAtMs;
    ShowdownRingTickAtMs = Showdown.EndsAtMs;
    if (ShowdownRing.TickMs < ShowdownRing.DurationMs
        && !LDMatchLifecycle::AddDurationMs(Showdown.ClosureAtMs, ShowdownRing.TickMs, ShowdownRingTickAtMs, Error))
    { return false; }
    ShowdownRespawns.Reset();
    ShowdownKills.Reset();
    ShowdownDamageP2.Reset();
    ShowdownBurn.Reset();
    bShowdownClosePending = false;
    const FLDDemoContentParameters& ContentParams = DemoContent->Get().Parameters;
    CaptureReducer.SetShowdownRatchets(ContentParams.ShowdownSprayRatchetMs, ContentParams.ShowdownGuardRatchetMs,
        ContentParams.ShowdownKillGuardBonusMs);
    int64 OpensAt = 0;
    if (!LDMatchLifecycle::AddDurationMs(Showdown.StartedAtMs, Settings->UnlockDelayMs, OpensAt, Error)
        || !CaptureReducer.ScheduleOpen(OpensAt, Error) || !BeginFinalMods(*Registry, Error)) { return false; }
    UE_LOG(LogLDMatch, Display, TEXT("LD_SHOWDOWN_BEGIN generation=%u at_ms=%lld opens_at_ms=%lld ends_at_ms=%lld"),
        MatchContext.Generation, Showdown.StartedAtMs, OpensAt, Showdown.EndsAtMs);
    TRACE_BOOKMARK(TEXT("LD showdown"));
    return true;
}

bool ALDGameMode::BeginFinalMods(const ULDMapRegistry& Registry, FString& Error)
{
    // Every taken final mod is public from the arena entry; the flagpole judges its holder's rule from the opening on.
    if (!Development)
    {
        Error = TEXT("Final mods require the development domain.");
        return false;
    }
    FLDCaptureFinalRules Rules;
    ShowdownMods.Reset();
    PrivatePoles.Reset();
    CageSprays.Reset();
    CageUntilMs = 0;
    for (int32 Slot = 0; Slot < RosterOrder.Num(); ++Slot)
    {
        const FGuid& Id = RosterOrder[Slot];
        FName ModId;
        if (!IsMemberPresent(Id) || !Development->ReadFinalMod(Id, ModId) || ModId.IsNone()) { continue; }
        const FLDDemoFinalModDefinition* Card = nullptr;
        if (!DemoContent->FindFinalMod(ModId.ToString(), Card, Error) || !Card) { return false; }
        ShowdownMods.Add({Id, ModId});
        Rules.Mods.Add(Id, Card->GetNativeId());
        const double RadiusCm = Card->RadiusCm.Denominator > 0 ? double(Card->RadiusCm.Numerator) / double(Card->RadiusCm.Denominator) : 0.;
        switch (Card->GetNativeId())
        {
        case ELDDemoFinalModId::FinalHitAndRun:
            Rules.HitAndRunSprays = Card->Sprays;
            Rules.HitAndRunFadeMs = Card->FadeMs;
            break;
        case ELDDemoFinalModId::FinalKillSeconds: Rules.KillGuardMs = Card->GuardMs; break;
        case ELDDemoFinalModId::FinalCage:
            CageDurationMs = Card->DurationMs;
            CageHPP2 = Card->HPP2;
            CageSprays.Add(Id, CaptureReducer.GetCompletedSprays(Id));
            break;
        case ELDDemoFinalModId::FinalDoubleYellow: DoubleYellowRadiusCm = RadiusCm; break;
        case ELDDemoFinalModId::FinalPrivatePole:
        {
            // Beside the holder's own arena entrance (its semantic final_spawn anchor), wherever the author placed it.
            const FName Entry(*LDDemoWorld::FinalSpawnId(Slot));
            const FLDMapBinding* Binding = Registry.Resolve(Entry, Registry.Generation);
            if (!Binding || !Binding->Floor.IsValid() || RadiusCm <= 0)
            {
                Error = FString::Printf(TEXT("The private flagpole needs the registered arena entrance %s and its approved radius."),
                    *Entry.ToString());
                return false;
            }
            FLDPrivatePole& Pole = PrivatePoles.AddDefaulted_GetRef();
            Pole.Holder = Id;
            Pole.Circle.CaptureId = Entry;
            Pole.Circle.CenterCm = FVector2D(Binding->Position.X, Binding->Position.Y);
            Pole.Circle.RadiusCm = RadiusCm;
            Pole.Circle.Ground = Binding->Floor;
            Pole.Circle.bAnyGround = true;
            if (!Pole.Reducer.BeginPrivate(CaptureReducer, Id, Card->SprayMs, Card->GuardMs, Error)) { return false; }
            break;
        }
        default: break;
        }
    }
    Rules.GuardCheckpointMs = DemoContent->Get().Parameters.CaptureGuardCheckpointMs;
    Rules.GuardResumeMs = DemoContent->Get().Parameters.CaptureGuardResumeMs;
    if (!CaptureReducer.SetFinalRules(Rules, Error)) { return false; }
    for (const FLDFinalModHolderView& Row : ShowdownMods)
    {
        UE_LOG(LogLDMatch, Display, TEXT("LD_FINAL_MOD_PUBLIC contender=%s mod=%s"), *LDLog::Id(Row.Contender), *Row.Mod.ToString());
    }
    Error.Reset();
    return true;
}

int64 ALDGameMode::NextShowdownDueMs() const
{
    if (!IsGameplayWriteOpen(MatchContext) || !Showdown.bStarted || Timeline.IsCountdownPaused()) { return MAX_int64; }
    int64 Due = Showdown.EndsAtMs;
    for (const int64 At : {Showdown.RespawnsCloseAtMs, Showdown.ClosureAtMs, ShowdownRingTickAtMs})
    {
        if (At > AuthorityNowMs) { Due = FMath::Min(Due, At); }
    }
    return Due;
}

bool ALDGameMode::AdvanceShowdownEffects(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!Showdown.bStarted || Timeline.IsCountdownPaused()) { Error.Reset(); return true; }
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
        TArray<LDShowdownRing::FFootSegment> Path;
        TArray<TPair<int64, int64>> Outside;
        int64 Hp = 0, MaxHp = 0;
        if (!Motion || !Combat->ReadBodyHealth(Body, Hp, MaxHp)
            || !LDShowdownRing::FootPathFromMotion(*Motion, Showdown.ClosureAtMs, From, To, Path)
            || !LDShowdownRing::OutsideIntervals(ShowdownRing, Showdown.CentreCm, Path, Outside))
        {
            Error = TEXT("Final street closure requires consistent authoritative living-body motion.");
            return false;
        }
        auto& Burn = ShowdownBurn.FindOrAdd(Id);
        if (Burn.Life != Body.Victim.Life) { Burn = {Body.Victim.Life, 0}; }
        for (const auto& Interval : Outside)
        {
            if (!LDShowdownRing::AccumulateDamage(ShowdownRing, MaxHp, Interval.Key, Interval.Value, Burn.AccumulatorE7))
            {
                Error = TEXT("Final street damage overflowed.");
                return false;
            }
        }
    }
    ShowdownRingSettledThroughMs = Now;
    if (Now < ShowdownRingTickAtMs) { Error.Reset(); return true; }
    const int64 NextTick = LDShowdownRing::NextTickMs(ShowdownRing, To);
    ShowdownRingTickAtMs = Showdown.EndsAtMs;
    if (NextTick < ShowdownRing.DurationMs
        && !LDMatchLifecycle::AddDurationMs(Showdown.ClosureAtMs, NextTick, ShowdownRingTickAtMs, Error)) { return false; }
    for (auto& Pair : ShowdownBurn)
    {
        FLDCombatBodyRef Body;
        FVector Foot;
        const int64 Amount = LDShowdownRing::TakeDamageP2(Pair.Value.AccumulatorE7);
        if (Amount > 0 && ReadLivingBody(Pair.Key, Body, Foot) && Body.Victim.Life == Pair.Value.Life
            && !Combat->ApplyEnvironmentDamage(Body, Amount, Permit, Error)) { return false; }
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::CloseShowdownIfDue(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!Showdown.bStarted || Timeline.IsCountdownPaused()) { return true; }
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
    for (FLDPrivatePole& Pole : PrivatePoles)
    {
        if (Pole.bCredited) { continue; }
        if (!CaptureReducer.CreditGuard(Pole.Holder, Pole.Reducer.GetBankedGuardMs(Pole.Holder) / 2, Error)) { return false; }
        Pole.bCredited = true;
    }
    FGuid Winner;
    if (bCanContinue)
    {
        const int64 CloseAt = FMath::Min(Now, Showdown.EndsAtMs);
        TArray<FLDShowdownTimeoutCandidate> Candidates;
        for (const FGuid& Id : RosterOrder)
        {
            const FLDLifeRecord* Life = Lives.Find(Id);
            FLDShowdownTimeoutCandidate& Row = Candidates.AddDefaulted_GetRef();
            Row.Contender = Id;
            Row.bEligible = IsMemberPresent(Id) && Life && Life->Phase == ELDLifePhase::Alive && Life->ProtectedUntilMs <= CloseAt;
            Row.GuardTargetMs = FMath::Max<int64>(1, CaptureReducer.GetGuardMs());
            Row.GuardProgressMs = CaptureReducer.GetEffectiveGuardMs(Id, CloseAt);
            Row.CompletedSprays = CaptureReducer.GetCompletedSprays(Id);
            for (const FLDPrivatePole& Pole : PrivatePoles)
            {
                if (!Pole.Reducer.IsActive()) { continue; }
                const int64 PoleTarget = FMath::Max<int64>(1, Pole.Reducer.GetGuardMs());
                const int64 PoleProgress = Pole.Reducer.GetEffectiveGuardMs(Id, CloseAt);
                if (PoleProgress * Row.GuardTargetMs > Row.GuardProgressMs * PoleTarget)
                {
                    Row.GuardProgressMs = PoleProgress;
                    Row.GuardTargetMs = PoleTarget;
                }
                Row.CompletedSprays = FMath::Max(Row.CompletedSprays, Pole.Reducer.GetCompletedSprays(Id));
            }
            Row.ShowdownKills = ShowdownKills.FindRef(Id);
            Row.ShowdownDamageP2 = ShowdownDamageP2.FindRef(Id);
        }
        Winner = FLDCaptureReducer::ResolveShowdownTimeoutWinner(Candidates);
    }
    return TryCloseMatch(bCanContinue ? ELDMatchEndReason::ObjectiveTimeout : ELDMatchEndReason::NoChampion,
        Winner, FMath::Min(Now, Showdown.EndsAtMs), Error);
}
