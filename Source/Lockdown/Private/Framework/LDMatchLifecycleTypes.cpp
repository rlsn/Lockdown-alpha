#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDDemoEntry.h"
#include "Kismet/GameplayStatics.h"
#include "UI/LDControlSettings.h"

namespace
{
    bool ConvertAuthoritySeconds(double Seconds, bool RoundUp, int64& Out, FString& Error)
    {
        Out = 0;
        const double Milliseconds = Seconds * 1000.;
        if (!FMath::IsFinite(Seconds) || Seconds < 0 || !FMath::IsFinite(Milliseconds)
            || Milliseconds >= 9223372036854775808.)
        {
            Error = TEXT("Authority time is nonfinite, negative or outside int64 milliseconds.");
            return false;
        }
        Out = RoundUp ? FMath::CeilToInt64(Milliseconds) : FMath::FloorToInt64(Milliseconds);
        Error.Reset();
        return true;
    }
}

bool LDMatchLifecycle::AuthoritySecondsToNowMs(double Seconds, int64& Out, FString& Error)
{ return ConvertAuthoritySeconds(Seconds, false, Out, Error); }

bool LDMatchLifecycle::AuthoritySecondsToEventMs(double Seconds, int64& Out, FString& Error)
{ return ConvertAuthoritySeconds(Seconds, true, Out, Error); }

bool LDMatchLifecycle::AddDurationMs(int64 StartMs, int64 DurationMs, int64& Out, FString& Error)
{
    Out = 0;
    if (StartMs < 0 || DurationMs < 0 || StartMs > MAX_int64 - DurationMs)
    {
        Error = TEXT("Authority deadline is negative or would overflow.");
        return false;
    }
    Out = StartMs + DurationMs;
    Error.Reset();
    return true;
}

bool LDMatchLifecycle::DeadlineFromSeconds(double StartSeconds, int64 DurationMs, int64& Out, FString& Error)
{
    int64 StartMs = 0;
    Out = 0;
    return AuthoritySecondsToEventMs(StartSeconds, StartMs, Error) && AddDurationMs(StartMs, DurationMs, Out, Error);
}

bool LDMatchLifecycle::AllocateGeneration(uint32& LastGeneration, uint32& Generation)
{
    Generation = 0;
    if (LastGeneration == MAX_uint32) { return false; }
    Generation = ++LastGeneration;
    return true;
}

ELDClockProjectionResult LDMatchLifecycle::AcceptClockView(const FLDDemoClockView& Candidate,
    FLDDemoClockView& Latest, FString& Error)
{
    Error.Reset();
    if (!Candidate.IsValid())
    {
        Error=TEXT("Invalid completed authority clock publication.");
        return ELDClockProjectionResult::Malformed;
    }
    if (Candidate.Generation<Latest.Generation
        || (Candidate.Generation==Latest.Generation && Candidate.StepSerial<Latest.StepSerial))
    {
        return ELDClockProjectionResult::Stale;
    }
    if (Candidate.Generation==Latest.Generation)
    {
        if (Candidate.NowMs<Latest.NowMs
            || (Candidate.StepSerial==Latest.StepSerial && Candidate.NowMs!=Latest.NowMs))
        {
            Error=TEXT("Authority clock time regressed or changed within an already published step.");
            return ELDClockProjectionResult::Malformed;
        }
        if (Candidate.StepSerial==Latest.StepSerial) { return ELDClockProjectionResult::Duplicate; }
    }
    Latest=Candidate;
    return ELDClockProjectionResult::Accepted;
}

ELDLobbyProjectionResult LDMatchLifecycle::AcceptLobbyView(const FLDLobbyView& Candidate, FLDLobbyView& Latest, FString& Error)
{
    Error.Reset();
    if (!Candidate.Generation || !Candidate.PublicRevision || !Candidate.RosterRevision
        || uint8(Candidate.Phase) > uint8(ELDMatchPhase::Faulted) || Candidate.Members.Num() > 6
        || Candidate.AICount > 3 || Candidate.StartBlocker.Len() > 512)
    {
        Error = TEXT("Invalid Lobby publication header.");
        return ELDLobbyProjectionResult::Malformed;
    }
    TSet<FGuid> Ids;
    int32 Humans = 0, AI = 0, Hosts = 0;
    bool AllHumansReady = true;
    for (const FLDLobbyMember& Member : Candidate.Members)
    {
        if (!Member.ContenderId.IsValid() || Ids.Contains(Member.ContenderId) || Member.Name.Len() > 64
            || Member.Name.Len() != FCString::Strlen(*Member.Name)
            || (Member.Kind != ELDContenderKind::Human && Member.Kind != ELDContenderKind::OpponentAI)
            || (Member.Host && Member.Kind != ELDContenderKind::Human))
        {
            Error = TEXT("Invalid or duplicate Lobby member.");
            return ELDLobbyProjectionResult::Malformed;
        }
        Ids.Add(Member.ContenderId);
        if (Member.Kind == ELDContenderKind::Human) { ++Humans; AllHumansReady &= Member.Ready; }
        else { ++AI; }
        Hosts += Member.Host ? 1 : 0;
    }
    const bool Closing = Candidate.Phase == ELDMatchPhase::Closing || Candidate.Phase == ELDMatchPhase::Faulted;
    if (Humans > 3 || AI != Candidate.AICount || Hosts > 1 || (!Closing && Humans > 0 && Hosts != 1)
        || (Candidate.CanStart && (Candidate.Phase != ELDMatchPhase::Lobby || !Humans || !AllHumansReady
            || Hosts != 1 || !Candidate.StartBlocker.IsEmpty())))
    {
        Error = TEXT("Lobby membership, Host or start readiness is inconsistent.");
        return ELDLobbyProjectionResult::Malformed;
    }
    if (Candidate.Generation < Latest.Generation
        || (Candidate.Generation == Latest.Generation
            && (Candidate.PublicRevision <= Latest.PublicRevision || Candidate.RosterRevision < Latest.RosterRevision)))
    {
        Error = TEXT("Stale or repeated Lobby publication.");
        return ELDLobbyProjectionResult::Stale;
    }
    Latest = Candidate;
    return ELDLobbyProjectionResult::Accepted;
}

ELDSessionWindowResult FLDSessionRequestWindow::Begin(const FLDSessionRequest& Request, FLDSessionReceipt& Receipt)
{
    Receipt.Key = Request.Key;
    Receipt.Outcome = ELDSessionOutcome::Rejected;
    if (!Request.HasValidShape())
    {
        Receipt.Reason = ELDSessionReject::InvalidRequest;
        return ELDSessionWindowResult::ExistingResult;
    }
    if (Pending)
    {
        if (Request == ActiveRequest)
        {
            Receipt.Outcome = ELDSessionOutcome::Pending;
            Receipt.Reason = ELDSessionReject::None;
        }
        else
        {
            Receipt.Reason = Request.Key == ActiveRequest.Key ? ELDSessionReject::RequestConflict : ELDSessionReject::RequestInFlight;
        }
        return ELDSessionWindowResult::ExistingResult;
    }
    if (Request.Key.RequestSequence <= LastSequence)
    {
        if (Request.Key.RequestSequence < LastSequence) { Receipt.Reason = ELDSessionReject::ReceiptExpired; }
        else if (!(Request == LastRequest)) { Receipt.Reason = ELDSessionReject::RequestConflict; }
        else { Receipt = LastReceipt; }
        return ELDSessionWindowResult::ExistingResult;
    }
    ActiveRequest = Request;
    Pending = true;
    return ELDSessionWindowResult::NewRequest;
}

bool FLDSessionRequestWindow::Complete(const FLDSessionReceipt& Receipt)
{
    if (!Pending || !(Receipt.Key == ActiveRequest.Key) || Receipt.Outcome == ELDSessionOutcome::Pending) { return false; }
    LastSequence = ActiveRequest.Key.RequestSequence;
    LastRequest = ActiveRequest;
    LastReceipt = Receipt;
    Pending = false;
    return true;
}

bool LDMatchLifecycle::ResolveContenderName(const FString& Options, FString& OutName, FString& Error)
{
    OutName.Reset();
    if (!UGameplayStatics::HasOption(Options, TEXT("LDName")))
    {
        OutName = DefaultContenderName;
        Error.Reset();
        return true;
    }
    const FString Requested = UGameplayStatics::ParseOption(Options, TEXT("LDName"));
    if (!ULDControlSettings::ValidatePlayerName(Requested, Error))
    {
        Error = LDDemoEntry::MakeJoinRefusal(LDDemoEntry::JoinRefusal::InvalidName, *Error);
        return false;
    }
    OutName = Requested;
    Error.Reset();
    return true;
}