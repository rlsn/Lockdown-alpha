#include "Audio/LDMusicSelection.h"
#include "Framework/LDCaptureTypes.h"

FLDMusicRequest LDAudio::SelectMusic(const FLDMusicInput& Input)
{
    FLDMusicRequest Request;
    auto Decide = [&Request](ELDMusicTrack Track, float StartSec = 0.f)
    {
        Request.bDecided = true;
        Request.Track = Track;
        Request.StartSec = StartSec;
        return Request;
    };
    if (Input.bFrontend) { return Decide(ELDMusicTrack::Menu); }
    const FLDPublicMatchView* Public = Input.Public;
    if (!Public || Public->Generation == 0) { return Request; }
    switch (Public->Phase)
    {
    case ELDMatchPhase::Lobby:
    case ELDMatchPhase::Starting:
    case ELDMatchPhase::Countdown:
    case ELDMatchPhase::Resetting:
        return Decide(ELDMusicTrack::Menu);
    case ELDMatchPhase::Development:
        // Without the clock the offset is unknown; starting at 0 would drift from the flagpole opening.
        if (!Input.bClock || Public->StartedAtMs <= 0) { return Request; }
        return Decide(ELDMusicTrack::Farm, float(FMath::Max<int64>(0, Input.NowMs - Public->StartedAtMs)) / 1000.f);
    case ELDMatchPhase::CaptureOpen:
        return Decide(ELDMusicTrack::Escalation);
    case ELDMatchPhase::Results:
    case ELDMatchPhase::Closing:
    case ELDMatchPhase::Faulted:
        return Decide(ELDMusicTrack::None);
    }
    return Request;
}
