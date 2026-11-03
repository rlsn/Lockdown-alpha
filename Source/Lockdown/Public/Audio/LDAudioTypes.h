#pragma once
#include "CoreMinimal.h"
#include "LDAudioTypes.generated.h"

// Every Demo sound cue. All cues are local 2D presentation: public cues come only from room-public DTOs
// (kill feed, public match, frozen result), own cues only from the owner projection or own receipts.
UENUM()
enum class ELDAudioCue : uint8
{
    None,
    KillNotice,
    CaptureOpened,
    CaptureStarted,
    CaptureContested,
    MatchVictory,
    MatchDefeat,
    MatchEndedNoWinner,
    SelfHit,
    SelfCast,
    PurchaseSucceeded,
    TradeRejected,
    LevelUp,
    SelfDeath,
    SelfRespawn,
    UIClick,
    Count UMETA(Hidden)
};

namespace LDAudio
{
    inline constexpr int32 CueCount = int32(ELDAudioCue::Count) - 1;
    inline bool IsPlayableCue(ELDAudioCue Cue) { return Cue > ELDAudioCue::None && Cue < ELDAudioCue::Count; }
    // Stable technical id (the enum name), e.g. "KillNotice"; also the manifest and asset-name key.
    LOCKDOWN_API FString CueId(ELDAudioCue Cue);
    LOCKDOWN_API bool ParseCueId(const FString& Id, ELDAudioCue& Out);
}
