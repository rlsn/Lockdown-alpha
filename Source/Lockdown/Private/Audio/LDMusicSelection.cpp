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
    // #198: a rest is the calm between field segments.
    case ELDMatchPhase::Rest:
    case ELDMatchPhase::Resetting:
        return Decide(ELDMusicTrack::Menu);
    case ELDMatchPhase::Development:
        // #198: the farm track (one field segment long) plays from the start of each field segment. Without the clock
        // or a published segment start the offset is unknown, so nothing changes.
        if (!Input.bClock || Public->Timeline.FieldStartedAtMs <= 0) { return Request; }
        return Decide(ELDMusicTrack::Farm, float(FMath::Max<int64>(0, Input.NowMs - Public->Timeline.FieldStartedAtMs)) / 1000.f);
    case ELDMatchPhase::CaptureOpen:
        return Decide(ELDMusicTrack::Escalation);
    case ELDMatchPhase::Results:
    case ELDMatchPhase::Closing:
    case ELDMatchPhase::Faulted:
        return Decide(ELDMusicTrack::None);
    }
    return Request;
}
