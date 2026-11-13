#include "Framework/LDMatchTimeline.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Development/LDDemoContentDefinitions.h"

bool FLDTimelineParameters::FromContent(const FLDDemoContentParameters& Content, FLDTimelineParameters& Out, FString& Error)
{
    Out = {};
    Out.FieldSegmentMs = Content.FieldSegmentMs;
    Out.OpeningBuildMs = Content.RestOpeningBuildMs;
    Out.BuildMs = Content.RestBuildMs;
    Out.SignupMs = Content.RestSignupMs;
    Out.DrawMs = Content.RestDrawMs;
    Out.BettingMs = Content.RestBettingMs;
    Out.PKReadyMs = Content.RestPKReadyMs;
    Out.PKMaxMs = Content.RestPKMaxMs;
    Out.SettlementMs = Content.RestSettlementMs;
    Out.PayoutMs = Content.RestPayoutMs;
    Out.ReturnMs = Content.RestReturnMs;
    Out.RespawnMs[0] = Content.RespawnField1Ms;
    Out.RespawnMs[1] = Content.RespawnField2Ms;
    Out.RespawnMs[2] = Content.RespawnField3Ms;
    return Out.Validate(Error);
}

bool FLDTimelineParameters::Validate(FString& Error) const
{
    // Each value is bounded by an int32 of seconds so every sum of a whole match stays far inside int64.
    constexpr int64 Bound = int64(MAX_int32) * 1000;
    const int64 Durations[] = {FieldSegmentMs, OpeningBuildMs, BuildMs, SignupMs, DrawMs, BettingMs, PKReadyMs, PKMaxMs,
        SettlementMs, PayoutMs, ReturnMs, RespawnMs[0], RespawnMs[1], RespawnMs[2]};
    for (const int64 Duration : Durations)
    {
        if (Duration <= 0 || Duration > Bound)
        {
            Error = TEXT("Every approved timeline duration must be positive and bounded.");
            return false;
        }
    }
    if (SignupMs > BuildMs || SignupMs > OpeningBuildMs)
    {
        Error = TEXT("The PK sign-up must fit inside every rest build window.");
        return false;
    }
    Error.Reset();
    return true;
}

bool FLDMatchTimeline::Begin(const FLDTimelineParameters& InParameters, int64 StartMs, FString& Error)
{
    Reset();
    if (StartMs < 0 || !InParameters.Validate(Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("The timeline starts at a nonnegative authority instant."); }
        return false;
    }
    Parameters = InParameters;
    Stage = ELDMatchStage::Rest;
    TArray<FLDTimelineEvent> Ignored;
    if (!EnterRest(0, StartMs, Ignored, Error))
    {
        Reset();
        return false;
    }
    return true;
}

bool FLDMatchTimeline::IsSignupOpen(int64 AtMs) const
{
    return Stage == ELDMatchStage::Rest && RestIndex < FLDTimelineParameters::TimedSegments
        && Step == ELDRestStep::Build && AtMs >= StepEndsAtMs - Parameters.SignupMs
        && AtMs <= StepEndsAtMs;
}

int64 FLDMatchTimeline::FieldMsAt(int64 AtMs) const
{
    if (!IsFieldRunning() || AtMs <= FieldStartedAtMs) { return FieldBaseMs; }
    const int64 Elapsed = AtMs - FieldStartedAtMs;
    if (Stage == ELDMatchStage::Field)
    {
        const int64 EndT = int64(FieldSegment + 1) * Parameters.FieldSegmentMs;
        return FMath::Min(EndT, FieldBaseMs + Elapsed);
    }
    return FieldBaseMs > MAX_int64 - Elapsed ? MAX_int64 : FieldBaseMs + Elapsed;
}

int64 FLDMatchTimeline::FieldDueAuthorityMs(int64 AtMs, int64 FieldAtMs) const
{
    if (!IsFieldRunning() || FieldAtMs <= FieldMsAt(AtMs)) { return MAX_int64; }
    if (Stage == ELDMatchStage::Field && FieldAtMs > int64(FieldSegment + 1) * Parameters.FieldSegmentMs) { return MAX_int64; }
    const int64 Offset = FieldAtMs - FieldBaseMs;
    return FieldStartedAtMs > MAX_int64 - Offset ? MAX_int64 : FieldStartedAtMs + Offset;
}

int64 FLDMatchTimeline::NextDueMs() const
{
    switch (Stage)
    {
    case ELDMatchStage::Rest: return StepEndsAtMs;
    case ELDMatchStage::Field:
        return FieldStartedAtMs + (int64(FieldSegment + 1) * Parameters.FieldSegmentMs - FieldBaseMs);
    default: return MAX_int64;
    }
}

int64 FLDMatchTimeline::RespawnWaitMs(int64 FieldT) const
{
    const int64 Index = Parameters.FieldSegmentMs > 0 ? FMath::Clamp<int64>(FieldT / Parameters.FieldSegmentMs, 0, FLDTimelineParameters::TimedSegments) : 0;
    return Index < FLDTimelineParameters::TimedSegments ? Parameters.RespawnMs[Index] : 0;
}

int64 FLDMatchTimeline::StepDuration(ELDRestStep InStep) const
{
    switch (InStep)
    {
    case ELDRestStep::Build: return RestIndex == 0 ? Parameters.OpeningBuildMs : Parameters.BuildMs;
    case ELDRestStep::Draw: return Parameters.DrawMs;
    case ELDRestStep::Betting: return Parameters.BettingMs;
    case ELDRestStep::PKReady: return Parameters.PKReadyMs;
    case ELDRestStep::PK: return Parameters.PKMaxMs;
    case ELDRestStep::Settlement: return Parameters.SettlementMs;
    case ELDRestStep::Payout: return Parameters.PayoutMs;
    case ELDRestStep::Return: return Parameters.ReturnMs;
    default: return 0;
    }
}

bool FLDMatchTimeline::EnterRest(uint8 Index, int64 AtMs, TArray<FLDTimelineEvent>& Out, FString& Error)
{
    if (Index > FLDTimelineParameters::TimedSegments)
    {
        Error = TEXT("The match has no rest after the final field.");
        return false;
    }
    Stage = ELDMatchStage::Rest;
    RestIndex = Index;
    FieldSegment = Index;
    LockedEntrants = 0;
    bPKSkipped = false;
    FLDTimelineEvent& Event = Out.AddDefaulted_GetRef();
    Event.Kind = ELDTimelineEventKind::EnterRest;
    Event.RestIndex = Index;
    Event.FieldSegment = FieldSegment;
    Event.AtMs = AtMs;
    Event.Value = FieldBaseMs;
    return EnterStep(ELDRestStep::Build, AtMs, Out, Error);
}

bool FLDMatchTimeline::EnterStep(ELDRestStep NewStep, int64 AtMs, TArray<FLDTimelineEvent>& Out, FString& Error)
{
    int64 EndsAt = 0;
    if (!LDMatchLifecycle::AddDurationMs(AtMs, StepDuration(NewStep), EndsAt, Error)) { return false; }
    Step = NewStep;
    StepStartedAtMs = AtMs;
    StepEndsAtMs = EndsAt;
    FLDTimelineEvent& Event = Out.AddDefaulted_GetRef();
    Event.Kind = ELDTimelineEventKind::EnterStep;
    Event.RestIndex = RestIndex;
    Event.FieldSegment = FieldSegment;
    Event.Step = NewStep;
    Event.AtMs = AtMs;
    return true;
}

bool FLDMatchTimeline::EnterField(int64 AtMs, TArray<FLDTimelineEvent>& Out, FString& Error)
{
    FieldSegment = RestIndex;
    Stage = FieldSegment >= FLDTimelineParameters::TimedSegments ? ELDMatchStage::FinalField : ELDMatchStage::Field;
    Step = ELDRestStep::None;
    FieldStartedAtMs = AtMs;
    StepStartedAtMs = AtMs;
    StepEndsAtMs = NextDueMs();
    FLDTimelineEvent& Event = Out.AddDefaulted_GetRef();
    Event.Kind = ELDTimelineEventKind::EnterField;
    Event.RestIndex = RestIndex;
    Event.FieldSegment = FieldSegment;
    Event.AtMs = AtMs;
    Event.Value = FieldBaseMs;
    Error.Reset();
    return true;
}

bool FLDMatchTimeline::Advance(int64 AtMs, int32 SignedUp, bool bAllowLock, bool bBodiesReady, TArray<FLDTimelineEvent>& Out,
    FString& Error)
{
    if (!IsActive())
    {
        Error = TEXT("The timeline is not running.");
        return false;
    }
    if (SignedUp < 0)
    {
        Error = TEXT("A sign-up count cannot be negative.");
        return false;
    }
    // Every transition happens at its own scheduled instant; one call settles a chain of zero-length steps too.
    for (int32 Guard = 0; Guard < 64; ++Guard)
    {
        const int64 Due = NextDueMs();
        if (Due > AtMs) { Error.Reset(); return true; }
        if (Stage == ELDMatchStage::Field)
        {
            FieldBaseMs = int64(FieldSegment + 1) * Parameters.FieldSegmentMs;
            if (!EnterRest(FieldSegment + 1, Due, Out, Error)) { return false; }
            continue;
        }
        if (Stage != ELDMatchStage::Rest)
        {
            Error = TEXT("Only a rest or a timed field segment can be due.");
            return false;
        }
        bool bStepped = false;
        switch (Step)
        {
        case ELDRestStep::Build:
        {
            if (!bAllowLock) { Error.Reset(); return true; }
            const int32 Entrants = RestIndex == FLDTimelineParameters::TimedSegments ? 0 : SignedUp;
            LockedEntrants = uint8(FMath::Min(Entrants, 255));
            FLDTimelineEvent& Lock = Out.AddDefaulted_GetRef();
            Lock.Kind = ELDTimelineEventKind::LockBuild;
            Lock.RestIndex = RestIndex;
            Lock.FieldSegment = FieldSegment;
            Lock.Step = ELDRestStep::Build;
            Lock.AtMs = Due;
            Lock.Value = Entrants;
            // Fewer than two entrants: no draw, betting, PK or payout; straight to the return countdown.
            bPKSkipped = Entrants < 2;
            bStepped = EnterStep(bPKSkipped ? ELDRestStep::Return : ELDRestStep::Draw, Due, Out, Error);
            break;
        }
        case ELDRestStep::Draw: bStepped = EnterStep(ELDRestStep::Betting, Due, Out, Error); break;
        case ELDRestStep::Betting: bStepped = EnterStep(ELDRestStep::PKReady, Due, Out, Error); break;
        case ELDRestStep::PKReady:
        case ELDRestStep::Return:
        {
            if (!bBodiesReady)
            {
                bEntryHeld = true;
                Error.Reset();
                return true;
            }
            // A held entry starts when its bodies stand ready; the paused T and the PK's full length start then.
            const int64 At = bEntryHeld ? AtMs : Due;
            bEntryHeld = false;
            bStepped = Step == ELDRestStep::PKReady ? EnterStep(ELDRestStep::PK, At, Out, Error) : EnterField(At, Out, Error);
            break;
        }
        case ELDRestStep::PK: bStepped = EnterStep(ELDRestStep::Settlement, Due, Out, Error); break;
        case ELDRestStep::Settlement: bStepped = EnterStep(ELDRestStep::Payout, Due, Out, Error); break;
        case ELDRestStep::Payout: bStepped = EnterStep(ELDRestStep::Return, Due, Out, Error); break;
        default: Error = TEXT("A rest has no current step."); return false;
        }
        if (!bStepped) { return false; }
    }
    Error = TEXT("The timeline did not settle within a bounded number of transitions.");
    return false;
}

bool FLDMatchTimeline::FinishPK(int64 AtMs, FString& Error)
{
    if (Stage != ELDMatchStage::Rest || Step != ELDRestStep::PK || AtMs < StepStartedAtMs || AtMs > StepEndsAtMs)
    {
        Error = TEXT("Only a running PK can end early, inside its own window.");
        return false;
    }
    StepEndsAtMs = AtMs;
    Error.Reset();
    return true;
}

bool FLDMatchTimeline::FinishBuildEarly(int64 AtMs, bool& bChanged, FString& Error)
{
    bChanged = false;
    if (!IsRest() || Step != ELDRestStep::Build || AtMs < StepStartedAtMs || AtMs >= StepEndsAtMs)
    {
        Error = TEXT("Ready only applies inside the current rest build window.");
        return false;
    }
    if (StepEndsAtMs - AtMs > Parameters.SignupMs)
    {
        StepEndsAtMs = AtMs + Parameters.SignupMs;
        bChanged = true;
    }
    Error.Reset();
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool FLDMatchTimeline::DevSkipToFinalRest(int64 AtMs, TArray<FLDTimelineEvent>& Out, FString& Error)
{
    if (Stage != ELDMatchStage::Field || AtMs < FieldStartedAtMs)
    {
        Error = TEXT("Only a running timed field segment can skip to the final field.");
        return false;
    }
    FieldBaseMs = int64(FLDTimelineParameters::TimedSegments) * Parameters.FieldSegmentMs;
    if (!EnterRest(FLDTimelineParameters::TimedSegments, AtMs, Out, Error)) { return false; }
    bPKSkipped = true;
    return EnterStep(ELDRestStep::Return, AtMs, Out, Error);
}
#endif

FLDTimelineView FLDMatchTimeline::MakeView(uint32 Generation, uint64 Revision) const
{
    FLDTimelineView View;
    View.Generation = Generation;
    View.Revision = Revision;
    View.Stage = Stage;
    View.RestIndex = RestIndex;
    View.FieldSegment = FieldSegment;
    View.Step = Step;
    View.StepStartedAtMs = StepStartedAtMs;
    View.StepEndsAtMs = StepEndsAtMs;
    View.SignupOpensAtMs = Stage == ELDMatchStage::Rest && RestIndex < FLDTimelineParameters::TimedSegments
        && Step == ELDRestStep::Build ? StepEndsAtMs - Parameters.SignupMs : 0;
    View.FieldBaseMs = FieldBaseMs;
    View.FieldStartedAtMs = FieldStartedAtMs;
    View.FieldEndT = Stage == ELDMatchStage::FinalField ? -1 : int64(FieldSegment + 1) * Parameters.FieldSegmentMs;
    View.LockedEntrants = LockedEntrants;
    View.bPKSkipped = bPKSkipped;
    return View;
}

int64 LDMatchTimeline::FieldNowFromView(const FLDTimelineView& View, int64 ServerNowMs)
{
    const bool bRuns = View.Stage == ELDMatchStage::Field || View.Stage == ELDMatchStage::FinalField;
    if (!bRuns || ServerNowMs <= View.FieldStartedAtMs) { return View.FieldBaseMs; }
    const int64 T = View.FieldBaseMs + (ServerNowMs - View.FieldStartedAtMs);
    return View.Stage == ELDMatchStage::Field && View.FieldEndT >= 0 ? FMath::Min(T, View.FieldEndT) : T;
}

const TCHAR* LDMatchTimeline::StepName(ELDRestStep Step)
{
    switch (Step)
    {
    case ELDRestStep::Build: return TEXT("build");
    case ELDRestStep::Draw: return TEXT("draw");
    case ELDRestStep::Betting: return TEXT("betting");
    case ELDRestStep::PKReady: return TEXT("pk_ready");
    case ELDRestStep::PK: return TEXT("pk");
    case ELDRestStep::Settlement: return TEXT("settlement");
    case ELDRestStep::Payout: return TEXT("payout");
    case ELDRestStep::Return: return TEXT("return");
    default: return TEXT("none");
    }
}

const TCHAR* LDMatchTimeline::StageName(ELDMatchStage Stage)
{
    switch (Stage)
    {
    case ELDMatchStage::Rest: return TEXT("rest");
    case ELDMatchStage::Field: return TEXT("field");
    case ELDMatchStage::FinalField: return TEXT("final_field");
    default: return TEXT("none");
    }
}
