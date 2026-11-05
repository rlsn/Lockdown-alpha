#include "Framework/LDCaptureTypes.h"

namespace
{
    constexpr int32 CaptureResultRowsMax = 6;
    constexpr int32 CaptureNameLengthMax = 64;
    constexpr uint8 CaptureLevelMax = 10;

    bool FailCaptureView(FString& Error, const TCHAR* Message)
    {
        Error = Message;
        return false;
    }
}

bool FLDCaptureEventTime::FromMs(int64 Ms, FLDCaptureEventTime& Out, FString& Error)
{
    Out = {};
    if (Ms < 0) { return FailCaptureView(Error, TEXT("Capture instant is negative.")); }
    Out.Seconds = double(Ms) / 1000.;
    Out.BucketMs = Ms;
    Error.Reset();
    return true;
}

bool FLDCaptureEventTime::FromSeconds(double RawSeconds, FLDCaptureEventTime& Out, FString& Error)
{
    Out = {};
    int64 Bucket = 0;
    if (!LDMatchLifecycle::AuthoritySecondsToEventMs(RawSeconds, Bucket, Error)) { return false; }
    Out.Seconds = RawSeconds;
    Out.BucketMs = Bucket;
    return true;
}

bool LDCapture::ValidatePublicMatchView(const FLDPublicMatchView& View, FString& Error)
{
    if (!View.Generation || !View.PublicRevision || uint8(View.Phase) > uint8(ELDMatchPhase::Faulted)
        || uint8(View.CircleState) > uint8(ELDCircleState::Contested))
    { return FailCaptureView(Error, TEXT("Invalid public match header.")); }
    if (View.ServerNowMs < 0 || View.StartedAtMs < 0 || View.OpenAtMs < View.StartedAtMs)
    { return FailCaptureView(Error, TEXT("Invalid public match times.")); }
    const bool Solo = View.CircleState == ELDCircleState::Solo;
    const bool Paused = View.CircleState == ELDCircleState::Contested && View.Occupier.IsValid();
    if ((Solo || Paused) != View.Occupier.IsValid()
        || (Solo ? (View.SoloSinceMs < View.OpenAtMs || View.DueAtMs <= View.SoloSinceMs)
                 : (View.SoloSinceMs != 0 || View.DueAtMs != 0))
        || (Paused ? (View.PausedProgressMs <= 0 || View.PausedProgressMs > View.ServerNowMs - View.OpenAtMs)
                   : View.PausedProgressMs != 0))
    { return FailCaptureView(Error, TEXT("Capture occupier and progress do not match the circle state.")); }
    const FLDAirdropView& Drop = View.Airdrop;
    const bool bDrop = Drop.Phase != ELDAirdropPhase::None;
    if (uint8(Drop.Phase) > uint8(ELDAirdropPhase::Claimed) || bDrop != (Drop.Serial != 0) || bDrop == Drop.PointId.IsNone()
        || Drop.PositionCm.ContainsNaN() || (bDrop ? Drop.LandAtMs <= View.StartedAtMs : Drop.LandAtMs != 0)
        || (Drop.Phase == ELDAirdropPhase::Claimed) != Drop.ClaimedBy.IsValid()
        || (Drop.Phase == ELDAirdropPhase::Claimed ? Drop.ClaimedAtMs < Drop.LandAtMs : Drop.ClaimedAtMs != 0)
        || ((Drop.Phase == ELDAirdropPhase::Landed || Drop.Phase == ELDAirdropPhase::Claimed) && View.ServerNowMs < Drop.LandAtMs))
    { return FailCaptureView(Error, TEXT("The airdrop does not match its phase or the match clock.")); }
    const FLDBossView& Boss = View.Boss;
    if (Boss.Phase == ELDBossPhase::None ? (Boss.AppearAtMs != 0 || Boss.KilledBy.IsValid() || Boss.DownAtMs != 0)
        : (Boss.AppearAtMs <= View.StartedAtMs
            // A blocked appearance stays Pending past its time and retries; Pending never names a killer.
            || (Boss.Phase == ELDBossPhase::Pending ? (Boss.KilledBy.IsValid() || Boss.DownAtMs != 0)
                : Boss.Phase == ELDBossPhase::Alive ? (View.ServerNowMs < Boss.AppearAtMs || Boss.KilledBy.IsValid() || Boss.DownAtMs != 0)
                : (Boss.DownAtMs < Boss.AppearAtMs || Boss.DownAtMs > View.ServerNowMs))))
    { return FailCaptureView(Error, TEXT("The Boss view does not match its phase or the match clock.")); }
    const FLDLeaderView& Named = View.Leader;
    if (Named.Leader.IsValid() ? (Named.CircleCentreCm.ContainsNaN() || Named.RefreshedAtMs <= View.StartedAtMs
            || Named.RefreshedAtMs > View.ServerNowMs || Named.BountyGold <= 0)
        : (Named.RefreshedAtMs != 0 || Named.BountyGold != 0 || !Named.CircleCentreCm.IsZero()))
    { return FailCaptureView(Error, TEXT("The leader notice does not match its leader or the match clock.")); }
    switch (View.Phase)
    {
    case ELDMatchPhase::Development:
        if (View.CircleState != ELDCircleState::NotOpen || View.ResultId.IsValid() || View.ServerNowMs >= View.OpenAtMs)
        { return FailCaptureView(Error, TEXT("The development phase cannot publish an open circle or a result.")); }
        break;
    case ELDMatchPhase::CaptureOpen:
        if (View.CircleState == ELDCircleState::NotOpen || View.ResultId.IsValid() || View.ServerNowMs < View.OpenAtMs)
        { return FailCaptureView(Error, TEXT("An open circle needs a circle state and no result.")); }
        break;
    case ELDMatchPhase::Results:
        if (!View.ResultId.IsValid()) { return FailCaptureView(Error, TEXT("Results require the locked result identity.")); }
        break;
    default:
        if (View.CircleState != ELDCircleState::NotOpen || View.ResultId.IsValid()
            || View.ServerNowMs || View.StartedAtMs || View.OpenAtMs || View.CaptureEpoch || bDrop || Named.Leader.IsValid()
            || Boss.Phase != ELDBossPhase::None)
        { return FailCaptureView(Error, TEXT("Rooms outside an active match publish no capture state.")); }
        break;
    }
    Error.Reset();
    return true;
}

bool LDCapture::ValidateResultView(const FLDResultView& View, FString& Error)
{
    if (!View.Generation || !View.ResultId.IsValid() || !View.ResultSerial
        || (View.Reason != ELDMatchEndReason::CaptureCompleted && View.Reason != ELDMatchEndReason::HostEnded)
        || (View.Reason == ELDMatchEndReason::CaptureCompleted) != View.Winner.IsValid())
    { return FailCaptureView(Error, TEXT("Invalid result identity, reason or winner.")); }
    if (View.StartedAtMs < 0 || View.EndedAtMs < View.StartedAtMs || View.DurationMs != View.EndedAtMs - View.StartedAtMs)
    { return FailCaptureView(Error, TEXT("Invalid result duration.")); }
    if (View.Rows.IsEmpty() || View.Rows.Num() > CaptureResultRowsMax)
    { return FailCaptureView(Error, TEXT("Result rows must cover the locked roster.")); }
    TSet<FGuid> Ids;
    int32 Humans = 0;
    bool WinnerListed = !View.Winner.IsValid();
    for (const FLDResultRow& Row : View.Rows)
    {
        if (!Row.ContenderId.IsValid() || Ids.Contains(Row.ContenderId) || Row.Name.IsEmpty()
            || Row.Name.Len() > CaptureNameLengthMax || Row.Name.Len() != FCString::Strlen(*Row.Name)
            || (Row.Kind != ELDContenderKind::Human && Row.Kind != ELDContenderKind::OpponentAI)
            || Row.FinalLevel < 1 || Row.FinalLevel > CaptureLevelMax)
        { return FailCaptureView(Error, TEXT("Invalid or duplicate result row.")); }
        Ids.Add(Row.ContenderId);
        Humans += Row.Kind == ELDContenderKind::Human ? 1 : 0;
        if (Row.ContenderId == View.Winner)
        {
            if (Row.bLeft) { return FailCaptureView(Error, TEXT("A contender who left cannot be the winner.")); }
            WinnerListed = true;
        }
    }
    if (!WinnerListed || Humans < 1 || Humans > 3 || View.Rows.Num() - Humans > 3)
    { return FailCaptureView(Error, TEXT("Result winner or roster composition is inconsistent.")); }
    Error.Reset();
    return true;
}

bool LDCapture::ValidateKillFeed(const FLDKillFeedView& View, FString& Error)
{
    if (!View.Generation || View.Notices.Num() > MaxKillNotices)
    { return FailCaptureView(Error, TEXT("Invalid kill feed header.")); }
    uint64 Previous = 0;
    for (const FLDKillNotice& Notice : View.Notices)
    {
        if (!Notice.Serial || Notice.Serial <= Previous || !Notice.Victim.IsValid() || Notice.OccurredAtMs < 0
            || uint8(Notice.Cause) > uint8(ELDKillCause::Boss)
            || (Notice.Cause == ELDKillCause::Contender) != Notice.Killer.IsValid() || Notice.Killer == Notice.Victim)
        { return FailCaptureView(Error, TEXT("Invalid kill notice.")); }
        Previous = Notice.Serial;
    }
    Error.Reset();
    return true;
}

ELDLobbyProjectionResult LDCapture::AcceptPublicMatchView(const FLDPublicMatchView& Candidate,
    FLDPublicMatchView& Latest, FString& Error)
{
    if (!ValidatePublicMatchView(Candidate, Error)) { return ELDLobbyProjectionResult::Malformed; }
    if (Candidate.Generation < Latest.Generation
        || (Candidate.Generation == Latest.Generation && Candidate.PublicRevision <= Latest.PublicRevision))
    {
        Error = TEXT("Stale or repeated public match publication.");
        return ELDLobbyProjectionResult::Stale;
    }
    Latest = Candidate;
    return ELDLobbyProjectionResult::Accepted;
}

ELDLobbyProjectionResult LDCapture::AcceptResultView(const FLDResultView& Candidate, FLDResultView& Latest, FString& Error)
{
    if (!ValidateResultView(Candidate, Error)) { return ELDLobbyProjectionResult::Malformed; }
    if (Candidate.Generation < Latest.Generation
        || (Candidate.Generation == Latest.Generation && Candidate.ResultSerial <= Latest.ResultSerial))
    {
        Error = TEXT("Stale or repeated result publication.");
        return ELDLobbyProjectionResult::Stale;
    }
    Latest = Candidate;
    return ELDLobbyProjectionResult::Accepted;
}

ELDLobbyProjectionResult LDCapture::AcceptKillFeed(const FLDKillFeedView& Candidate, FLDKillFeedView& Latest, FString& Error)
{
    if (!ValidateKillFeed(Candidate, Error)) { return ELDLobbyProjectionResult::Malformed; }
    const uint64 CandidateLast = Candidate.Notices.IsEmpty() ? 0 : Candidate.Notices.Last().Serial;
    const uint64 LatestLast = Latest.Notices.IsEmpty() ? 0 : Latest.Notices.Last().Serial;
    if (Candidate.Generation < Latest.Generation
        || (Candidate.Generation == Latest.Generation && CandidateLast <= LatestLast))
    {
        Error = TEXT("Stale or repeated kill feed publication.");
        return ELDLobbyProjectionResult::Stale;
    }
    Latest = Candidate;
    return ELDLobbyProjectionResult::Accepted;
}
