#include "Framework/LDCaptureTypes.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDGrowthProtocol.h"

namespace
{
    constexpr int32 CaptureResultRowsMax = 6;
    constexpr uint8 CapturePresentationSlots = 6;
    constexpr int32 CaptureNameLengthMax = 64;
    // #184: a result row carries a committed level, up to the approved cap; a stale 10 here faulted every win above level 10.
    constexpr int32 CaptureLevelMax = LDDemoProgression::MaxLevel;

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
    if (!View.Generation || !View.PublicRevision || !LDMatchLifecycle::IsKnownPhase(View.Phase)
        || uint8(View.CircleState) > uint8(ELDCircleState::Guarded))
    { return FailCaptureView(Error, TEXT("Invalid public match header.")); }
    if (View.ServerNowMs < 0 || View.StartedAtMs < 0 || View.OpenAtMs < View.StartedAtMs)
    { return FailCaptureView(Error, TEXT("Invalid public match times.")); }
    const FLDShowdownView& Final = View.Showdown;
    if (Final.bStarted && (Final.StartedAtMs < View.StartedAtMs || Final.EndsAtMs <= Final.StartedAtMs
        || Final.RespawnsCloseAtMs <= Final.StartedAtMs || Final.RespawnsCloseAtMs >= Final.EndsAtMs
        || Final.ClosureAtMs < Final.StartedAtMs || Final.ClosureAtMs >= Final.EndsAtMs || Final.SprayMs <= 0 || Final.GuardMs <= 0
        || Final.CentreCm.ContainsNaN() || !FMath::IsFinite(Final.InitialRadiusCm) || !FMath::IsFinite(Final.FinalRadiusCm)
        || Final.FinalRadiusCm <= 0 || Final.InitialRadiusCm <= Final.FinalRadiusCm
        || View.Timeline.Stage != ELDMatchStage::FinalField || View.OpenAtMs >= Final.EndsAtMs))
    { return FailCaptureView(Error, TEXT("Invalid final-showdown public clock or arena.")); }
    if (View.OwnerBankedGuardMs < 0 || (!View.GraffitiOwner.IsValid() && View.OwnerBankedGuardMs != 0))
    { return FailCaptureView(Error, TEXT("Invalid public guard contribution.")); }
    const bool Solo = View.CircleState == ELDCircleState::Solo;
    const bool Paused = View.CircleState == ELDCircleState::Contested && View.Occupier.IsValid();
    if ((Solo || Paused) != View.Occupier.IsValid()
        || (Solo ? (View.SoloSinceMs < View.OpenAtMs || View.DueAtMs <= View.SoloSinceMs)
                 : (View.SoloSinceMs != 0 || View.DueAtMs != 0))
        || (Paused ? (View.PausedProgressMs <= 0 || View.PausedProgressMs > View.ServerNowMs - View.OpenAtMs)
                   : View.PausedProgressMs != 0))
    { return FailCaptureView(Error, TEXT("Spray holder and progress do not match the zone state.")); }
    const bool Owned = View.GraffitiOwner.IsValid();
    // The guard runs only while its owner is alone in the zone (Guarded); an empty zone pauses it and a sole sprayer
    // or a contest, which always holds a challenger, freezes it. Either way it publishes only its kept progress. A
    // kill (final_kill_seconds) can add kept progress beyond the elapsed time, never beyond the guard; a fading graffiti
    // (final_hit_and_run) has no guard at all.
    const bool GuardFrozen = View.CircleState != ELDCircleState::Guarded;
    const int64 KeptMax = Final.bStarted ? Final.GuardMs : View.ServerNowMs - View.OpenAtMs;
    if ((View.CircleState == ELDCircleState::Guarded && !Owned) || (Owned && View.Occupier == View.GraffitiOwner)
        || View.GraffitiPresentation >= CapturePresentationSlots || (!Owned && View.GraffitiPresentation != 0)
        || View.FadeAtMs < 0 || (View.FadeAtMs != 0 && (!Owned || View.FadeAtMs <= View.OpenAtMs))
        || uint8(View.GraffitiCause) > uint8(ELDGraffitiCause::Usurped) || (View.GraffitiCause == ELDGraffitiCause::None) == Owned
        || (!Owned || View.FadeAtMs != 0 ? (View.GuardDueAtMs != 0 || View.GuardKeptMs != 0)
            : GuardFrozen ? (View.GuardDueAtMs != 0 || View.GuardKeptMs < 0 || View.GuardKeptMs > KeptMax)
                          : (View.GuardDueAtMs <= View.OpenAtMs || View.GuardKeptMs != 0)))
    { return FailCaptureView(Error, TEXT("Graffiti owner and guard progress do not match the zone state.")); }
    // The final field's spray zones and public cards.
    if (Final.bStarted ? (View.PoleCentreCm.ContainsNaN() || !FMath::IsFinite(View.PoleRadiusCm) || View.PoleRadiusCm <= 0)
        : (!View.PoleCentreCm.IsZero() || View.PoleRadiusCm != 0 || View.CageUntilMs != 0 || !View.FinalMods.IsEmpty()
            || !View.PrivatePoles.IsEmpty()))
    { return FailCaptureView(Error, TEXT("Only the final field publishes its flagpole zone and final mods.")); }
    if (View.CageUntilMs < 0 || (View.CageUntilMs == 0 ? (!View.CageCentreCm.IsZero() || View.CageRadiusCm != 0 || View.CageIntegrityBp != 0)
        : (View.CageCentreCm.ContainsNaN() || !FMath::IsFinite(View.CageRadiusCm) || View.CageRadiusCm <= 0
            || View.CageIntegrityBp <= 0 || View.CageIntegrityBp > 10000)))
    { return FailCaptureView(Error, TEXT("Invalid final cage.")); }
    if (View.FinalMods.Num() > CaptureResultRowsMax
        || View.FinalMods.ContainsByPredicate([](const FLDFinalModHolderView& Row) { return !Row.Contender.IsValid() || Row.Mod.IsNone(); })
        || View.PrivatePoles.Num() > CaptureResultRowsMax)
    { return FailCaptureView(Error, TEXT("Invalid public final mods.")); }
    for (const FLDPrivatePoleView& Pole : View.PrivatePoles)
    {
        if (!Pole.Holder.IsValid() || Pole.Presentation >= CapturePresentationSlots || Pole.CentreCm.ContainsNaN() || !FMath::IsFinite(Pole.RadiusCm) || Pole.RadiusCm <= 0
            || uint8(Pole.CircleState) > uint8(ELDCircleState::Guarded) || Pole.GuardMs <= 0
            || Pole.GuardKeptMs < 0 || Pole.GuardKeptMs > Pole.GuardMs || Pole.PausedProgressMs < 0
            || (Pole.CircleState == ELDCircleState::Guarded && !Pole.bGraffiti)
            || (Pole.CircleState == ELDCircleState::Solo ? Pole.DueAtMs <= View.OpenAtMs : Pole.DueAtMs != 0)
            || (Pole.bGraffiti && Pole.CircleState == ELDCircleState::Guarded ? Pole.GuardDueAtMs <= View.OpenAtMs
                : Pole.GuardDueAtMs != 0))
        { return FailCaptureView(Error, TEXT("Invalid private flagpole.")); }
    }
    const FLDAirdropView& Drop = View.Airdrop;
    const bool bDrop = Drop.Phase != ELDAirdropPhase::None;
    // #198: world schedules (airdrop, Boss, leader notice) are field-progress times T, not authority ms.
    const FLDTimelineView& Line = View.Timeline;
    const int64 FieldNow = LDMatchTimeline::FieldNowFromView(Line, View.ServerNowMs);
    if (Line.bCountdownPaused ? (Line.Stage == ELDMatchStage::None || Line.CountdownPausedAtMs < Line.StepStartedAtMs
        || Line.CountdownPausedAtMs > View.ServerNowMs) : Line.CountdownPausedAtMs != 0)
    { return FailCaptureView(Error, TEXT("Invalid paused timeline clock.")); }
    if (uint8(Line.Stage) > uint8(ELDMatchStage::FinalField) || uint8(Line.Step) > uint8(ELDRestStep::Return)
        || (Line.Stage == ELDMatchStage::Rest) != (Line.Step != ELDRestStep::None) || Line.FieldBaseMs < 0
        || (Line.Stage != ELDMatchStage::None && Line.Generation != View.Generation))
    { return FailCaptureView(Error, TEXT("The timeline does not match its stage.")); }
    if (uint8(Drop.Phase) > uint8(ELDAirdropPhase::Expired) || bDrop != (Drop.Serial != 0) || bDrop == Drop.PointId.IsNone()
        || Drop.PositionCm.ContainsNaN() || (bDrop ? (Drop.LandAtMs <= 0 || Drop.ExpireAtMs <= Drop.LandAtMs)
            : (Drop.LandAtMs != 0 || Drop.ExpireAtMs != 0))
        || ((Drop.Phase == ELDAirdropPhase::Landed || Drop.Phase == ELDAirdropPhase::Claimed) && FieldNow < Drop.LandAtMs)
        || (Drop.Phase == ELDAirdropPhase::Expired && FieldNow < Drop.ExpireAtMs))
    { return FailCaptureView(Error, TEXT("The airdrop does not match its phase or the field clock.")); }
    // #198 outer §5: at most one High and one Elite camp marker, both of the same timed segment, on named camps.
    for (const FLDFieldMarkerView& Marker : View.Markers)
    {
        if (View.Markers.Num() > 2 || Marker.Segment < 1 || Marker.Segment > FLDTimelineParameters::TimedSegments
            || Marker.Segment != View.Markers[0].Segment
            || (Marker.Kind != ELDFieldMarkerKind::High && Marker.Kind != ELDFieldMarkerKind::Elite)
            || View.Markers.FilterByPredicate([&Marker](const FLDFieldMarkerView& Other) { return Other.Kind == Marker.Kind; }).Num() != 1
            || Marker.CampId.IsNone() || Marker.PositionCm.ContainsNaN()
            || Marker.Phase == ELDFieldMarkerPhase::None || uint8(Marker.Phase) > uint8(ELDFieldMarkerPhase::Expired)
            || Marker.RevealAtMs <= 0 || Marker.EndAtMs <= Marker.RevealAtMs || FieldNow < Marker.RevealAtMs
            || (Marker.Phase == ELDFieldMarkerPhase::Expired && FieldNow < Marker.EndAtMs)
            || (Marker.Phase == ELDFieldMarkerPhase::Revealed && Line.Stage == ELDMatchStage::Rest))
        { return FailCaptureView(Error, TEXT("A camp marker does not match its segment, kind, phase or the field clock.")); }
    }
    const FLDBossView& Boss = View.Boss;
    if (Boss.Phase == ELDBossPhase::None ? (Boss.AppearAtMs != 0 || Boss.LeaveAtMs != 0 || Boss.KilledBy.IsValid() || Boss.DownAtMs != 0)
        : (Boss.AppearAtMs <= 0 || Boss.LeaveAtMs <= Boss.AppearAtMs || uint8(Boss.Phase) > uint8(ELDBossPhase::Left)
            // A blocked appearance stays Pending past its time and retries; Pending never names a killer.
            || (Boss.Phase == ELDBossPhase::Pending ? (Boss.KilledBy.IsValid() || Boss.DownAtMs != 0)
                : Boss.Phase == ELDBossPhase::Alive ? (FieldNow < Boss.AppearAtMs || Boss.KilledBy.IsValid() || Boss.DownAtMs != 0)
                // #198 outer §5: it left unkilled at its leave instant.
                : Boss.Phase == ELDBossPhase::Left ? (FieldNow < Boss.LeaveAtMs || Boss.KilledBy.IsValid() || Boss.DownAtMs != 0)
                : (Boss.DownAtMs < Boss.AppearAtMs || Boss.DownAtMs > FieldNow || Boss.DownAtMs > Boss.LeaveAtMs))))
    { return FailCaptureView(Error, TEXT("The Boss view does not match its phase or the field clock.")); }
    // A rumor day names up to two; either may stand alone (the leader can die while the runner-up stays trending).
    const FLDLeaderView& Named = View.Leader;
    const auto SlotMismatch = [](const FGuid& Id, const FVector& Centre)
    { return Id.IsValid() ? Centre.ContainsNaN() : !Centre.IsZero(); };
    const bool bNamed = Named.Leader.IsValid() || Named.RunnerUp.IsValid();
    if (SlotMismatch(Named.Leader, Named.CircleCentreCm) || SlotMismatch(Named.RunnerUp, Named.RunnerUpCentreCm)
        || (Named.Leader.IsValid() && Named.Leader == Named.RunnerUp)
        || (bNamed ? (Named.RefreshedAtMs <= 0 || Named.RefreshedAtMs > FieldNow) : Named.RefreshedAtMs != 0))
    { return FailCaptureView(Error, TEXT("The leader notice does not match its leader or the field clock.")); }
    switch (View.Phase)
    {
    case ELDMatchPhase::Rest:
        // #198: a rest pauses the field; the flagpole only opens in the final field.
        if (View.CircleState != ELDCircleState::NotOpen || View.ResultId.IsValid() || Owned || Line.Stage != ELDMatchStage::Rest)
        { return FailCaptureView(Error, TEXT("A rest publishes its timeline with the closed circle and no result.")); }
        break;
    case ELDMatchPhase::Development:
        if (View.CircleState != ELDCircleState::NotOpen || View.ResultId.IsValid() || View.ServerNowMs >= View.OpenAtMs || Owned
            || (Line.Stage != ELDMatchStage::Field && Line.Stage != ELDMatchStage::FinalField))
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
            || View.ServerNowMs || View.StartedAtMs || View.OpenAtMs || View.CaptureEpoch || Owned || bDrop
            || Named.Leader.IsValid() || Boss.Phase != ELDBossPhase::None || Line.Stage != ELDMatchStage::None
            || !View.Markers.IsEmpty())
        { return FailCaptureView(Error, TEXT("Rooms outside an active match publish no spray state.")); }
        break;
    }
    Error.Reset();
    return true;
}

bool LDCapture::ValidateResultBuild(const FLDResultBuild& Build, FString& Error)
{
    if (!Build.bAvailable)
    {
        if (!Build.CatalogRoot.IsEmpty() || !Build.Actives.IsEmpty() || !Build.Cards.IsEmpty()
            || !Build.FinalMod.IsNone())
        { return FailCaptureView(Error, TEXT("An unavailable result build must not carry partial data.")); }
        Error.Reset();
        return true;
    }
    FLDDemoContentDigest Root;
    if (!FLDDemoContentDigest::ParseHex(Build.CatalogRoot, Root, Error) || Build.Actives.Num() > 3 || Build.Cards.Num() > 32)
    { return FailCaptureView(Error, TEXT("Invalid result build catalog or size.")); }
    auto ValidId = [](FName Id)
    {
        FLDDefinitionWire Wire;
        return FLDDefinitionWire::Encode(Id, Wire);
    };
    TSet<FName> ActiveIds;
    TSet<uint8> Slots;
    for (const FLDResultBuildActive& Active : Build.Actives)
    {
        if (!ValidId(Active.DefinitionId) || Active.Slot >= 3 || Slots.Contains(Active.Slot) || ActiveIds.Contains(Active.DefinitionId))
        { return FailCaptureView(Error, TEXT("Invalid or duplicate result build active.")); }
        Slots.Add(Active.Slot);
        ActiveIds.Add(Active.DefinitionId);
    }
    for (const FLDResultBuildCard& Card : Build.Cards)
    {
        const bool bTarget = Card.Target == ELDResultBuildTarget::Active ? ActiveIds.Contains(Card.TargetId)
            : (Card.Target == ELDResultBuildTarget::Body || Card.Target == ELDResultBuildTarget::Dash) && Card.TargetId.IsNone();
        if (!ValidId(Card.DefinitionId) || !bTarget || uint8(Card.Element) > uint8(ELDDemoElement::Rot))
        { return FailCaptureView(Error, TEXT("Invalid result build card.")); }
    }
    if (!Build.FinalMod.IsNone() && !ValidId(Build.FinalMod))
    { return FailCaptureView(Error, TEXT("Invalid result final mod.")); }
    Error.Reset();
    return true;
}

bool LDCapture::ValidateResultView(const FLDResultView& View, FString& Error)
{
    if (!View.Generation || !View.ResultId.IsValid() || !View.ResultSerial
        || View.Reason == ELDMatchEndReason::None || uint8(View.Reason) > uint8(ELDMatchEndReason::ClearOut)
        || ((View.Reason == ELDMatchEndReason::GuardSucceeded || View.Reason == ELDMatchEndReason::SprayCount
            || View.Reason == ELDMatchEndReason::ClearOut) && !View.Winner.IsValid())
        || ((View.Reason == ELDMatchEndReason::HostEnded || View.Reason == ELDMatchEndReason::NoChampion) && View.Winner.IsValid()))
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
        if (Row.BankedGuardMs < 0 || Row.BankedGuardMs > View.DurationMs)
        { return FailCaptureView(Error, TEXT("Invalid result objective contribution.")); }
        if (!ValidateResultBuild(Row.Build, Error)) { return false; }
        Ids.Add(Row.ContenderId);
        Humans += Row.Kind == ELDContenderKind::Human ? 1 : 0;
        if (Row.ContenderId == View.Winner)
        {
            if (Row.bLeft) { return FailCaptureView(Error, TEXT("A contender who left cannot be the winner.")); }
            WinnerListed = true;
        }
    }
    if (!WinnerListed || Humans < 1 || Humans > LDMatchLifecycle::MaxContenders
        || View.Rows.Num() - Humans > LDMatchLifecycle::MaxOpponents)
    { return FailCaptureView(Error, TEXT("Result winner or roster composition is inconsistent.")); }
    Error.Reset();
    return true;
}

const FLDResultRow* LDCapture::PreviousLobbyResult(const FLDResultView& Result, const FLDLobbyView& Lobby,
    const FLDLobbyMember& Member)
{
    if (Lobby.Phase != ELDMatchPhase::Lobby || !Lobby.PreviousResultId.IsValid()
        || Lobby.PreviousResultId != Result.ResultId || !Result.Generation || Result.Generation >= Lobby.Generation)
    { return nullptr; }
    const FGuid Id = Member.Kind == ELDContenderKind::Human ? Member.ContenderId : Member.PreviousAIContender;
    if (!Id.IsValid()) { return nullptr; }
    return Result.Rows.FindByPredicate([&](const FLDResultRow& Row) { return Row.ContenderId == Id && Row.Kind == Member.Kind; });
}

bool LDCapture::ValidateKillFeed(const FLDKillFeedView& View, FString& Error)
{
    if (!View.Generation || View.Notices.Num() > MaxKillNotices)
    { return FailCaptureView(Error, TEXT("Invalid kill feed header.")); }
    uint64 Previous = 0;
    for (const FLDKillNotice& Notice : View.Notices)
    {
        if (!Notice.Serial || Notice.Serial <= Previous || !Notice.Victim.IsValid() || Notice.OccurredAtMs < 0
            || uint8(Notice.Cause) > uint8(LastKillCause)
            || (Notice.Cause == ELDKillCause::Contender) != Notice.Killer.IsValid() || Notice.Killer == Notice.Victim
            || (Notice.Cause != ELDKillCause::Contender && !Notice.Assisters.IsEmpty())
            || Notice.CreatureName.Len()>80
            || Notice.Assisters.Num() > CaptureResultRowsMax - 2)
        { return FailCaptureView(Error, TEXT("Invalid kill notice.")); }
        for (int32 Index = 0; Index < Notice.Assisters.Num(); ++Index)
        {
            const FGuid& Assister = Notice.Assisters[Index];
            if (!Assister.IsValid() || Assister == Notice.Victim || Assister == Notice.Killer
                || (Index > 0 && !(Notice.Assisters[Index - 1] < Assister)))
            { return FailCaptureView(Error, TEXT("Invalid kill notice assisters.")); }
        }
        Previous = Notice.Serial;
    }
    if (View.Scores.Num() > MaxScoreRows) { return FailCaptureView(Error, TEXT("Too many scoreboard rows.")); }
    for (int32 Index = 0; Index < View.Scores.Num(); ++Index)
    {
        const FGuid& Id = View.Scores[Index].ContenderId;
        for (int32 Earlier = 0; Earlier < Index; ++Earlier)
        {
            if (View.Scores[Earlier].ContenderId == Id) { return FailCaptureView(Error, TEXT("Duplicate scoreboard contender.")); }
        }
        if (!Id.IsValid()) { return FailCaptureView(Error, TEXT("Invalid scoreboard contender.")); }
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
