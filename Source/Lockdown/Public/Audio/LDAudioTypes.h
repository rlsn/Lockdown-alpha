#pragma once
#include "CoreMinimal.h"
#include "LDAudioTypes.generated.h"

// Every Demo sound cue. All cues are local 2D presentation: public cues come only from room-public DTOs
// (kill feed, public match, frozen result), own cues only from the owner projection or own receipts.
// Cue numbers are stable technical identities: a removed cue retires its number as a hole, never reused.
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
    // 10 and 11 (the shop trade cues) are retired (#144).
    LevelUp = 12,
    SelfDeath,
    SelfRespawn,
    UIClick,
    // A hit fully absorbed by the own shield: the hit sound's sample, quieter.
    SelfShieldHit,
    // The own basic attack released (melee strike or projectile launch), from the owner projection's cue facts.
    SelfAttack,
    // 18 and 19 (the shop queue cues) are retired (#144); the next cue is appended as 20.
    Count UMETA(Hidden)
};

namespace LDAudio
{
    // Retired numbers below Count, which are not cues.
    inline constexpr int32 RetiredCueCount = 2;
    // Playable cues: every number between None and Count except the retired ones.
    inline constexpr int32 CueCount = int32(ELDAudioCue::Count) - 1 - RetiredCueCount;
    LOCKDOWN_API bool IsPlayableCue(ELDAudioCue Cue);
    // The Index-th playable cue in enum order (the manifest and catalog order); None outside 0..CueCount-1.
    LOCKDOWN_API ELDAudioCue PlayableCue(int32 Index);
    // Stable technical id (the enum name), e.g. "KillNotice"; also the manifest and asset-name key.
    LOCKDOWN_API FString CueId(ELDAudioCue Cue);
    LOCKDOWN_API bool ParseCueId(const FString& Id, ELDAudioCue& Out);
}
