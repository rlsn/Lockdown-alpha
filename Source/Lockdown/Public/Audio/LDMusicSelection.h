#pragma once
#include "CoreMinimal.h"
#include "Audio/LDAudioTypes.h"

struct FLDPublicMatchView;

// What the local music should be this frame. Its only inputs are whether the local world is the frontend, the room-public
// match view and the shared authority clock: no owner, unit or actor state.
struct FLDMusicInput
{
    bool bFrontend = false;
    // Null, or generation 0, while the room state has not arrived yet.
    const FLDPublicMatchView* Public = nullptr;
    bool bClock = false;
    int64 NowMs = 0;
};

struct FLDMusicRequest
{
    // False keeps whatever plays now: the room state (or, in the development phase, the clock) is not known yet, so the
    // menu loop carries on through the travel into the lobby instead of stopping for a frame.
    bool bDecided = false;
    ELDMusicTrack Track = ELDMusicTrack::None;
    // Offset into the track if it starts now: the time elapsed since T=0 for Farm, so a late start stays aligned with the
    // flagpole opening. Zero for every other track.
    float StartSec = 0.f;
    bool operator==(const FLDMusicRequest& Other) const
    { return bDecided == Other.bDecided && Track == Other.Track && StartSec == Other.StartSec; }
};

namespace LDAudio
{
    // Frontend, lobby, start, countdown and reset: Menu. Development: Farm from the time since T=0. Flagpole open: Escalation.
    // Results, closing and a fault: no music, so the result jingle stands alone; the menu loop returns with the lobby.
    LOCKDOWN_API FLDMusicRequest SelectMusic(const FLDMusicInput& Input);
}
