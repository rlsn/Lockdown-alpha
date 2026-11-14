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
    // A hit absorbed by the own shield.
    SelfShieldHit,
    // The own basic attack released (melee strike or projectile launch), from the owner projection's cue facts.
    SelfAttack,
    // 18 and 19 (the shop queue cues) are retired (#144).
    SelfShieldBreak = 20,
    // The own innate dash released, from its successful owner receipt.
    SelfDash,
    // The pointer entering a command button (UIClick plays when it is pressed).
    UIHover,
    // Build workshop moments (APP_SPEC §7 构筑): a component picked up, installed (after its receipt) or refused, the
    // workshop opening and closing, the offer cards dealt, an active chosen (after its receipt), a component received, a
    // point added, a hold confirm charging and the final mod locked (after its receipt).
    UIPickup,
    UIInstall,
    UIReject,
    UIWorkshopOpen,
    UIWorkshopClose,
    UIOfferDeal,
    UIChoose,
    UIGain,
    UIAddPoint,
    UIHoldCharge,
    UIFinalLock,
    Count UMETA(Hidden)
};

// Background music tracks (#94). Like cues they are local 2D presentation, chosen only from the room-public match phase
// and the shared authority clock. Numbers are stable technical identities, as for cues.
UENUM()
enum class ELDMusicTrack : uint8
{
    None,
    // The main menu and the room lobby share one loop that keeps playing across the travel between them.
    Menu,
    // The development phase, played once from T=0; the track spans the phase until the flagpole opens.
    Farm,
    // From the flagpole opening until the match ends, looping.
    Escalation,
    Count UMETA(Hidden)
};

namespace LDAudio
{
    inline constexpr int32 MusicTrackCount = int32(ELDMusicTrack::Count) - 1;
    // Seconds an outgoing track takes to fade out when the next one starts or the music stops.
    inline constexpr float MusicFadeOutSec = 1.5f;
    // Stable technical id (the enum name), e.g. "Menu"; also the manifest and asset-name key. "None" for None.
    LOCKDOWN_API FString MusicTrackId(ELDMusicTrack Track);
    LOCKDOWN_API bool ParseMusicTrackId(const FString& Id, ELDMusicTrack& Out);
    // Menu and Escalation loop; Farm plays once.
    LOCKDOWN_API bool MusicTrackLoops(ELDMusicTrack Track);

    // Retired numbers below Count, which are not cues.
    inline constexpr int32 RetiredCueCount = 4;
    // Playable cues: every number between None and Count except the retired ones.
    inline constexpr int32 CueCount = int32(ELDAudioCue::Count) - 1 - RetiredCueCount;
    LOCKDOWN_API bool IsPlayableCue(ELDAudioCue Cue);
    // The Index-th playable cue in enum order (the manifest and catalog order); None outside 0..CueCount-1.
    LOCKDOWN_API ELDAudioCue PlayableCue(int32 Index);
    // Stable technical id (the enum name), e.g. "KillNotice"; also the manifest and asset-name key.
    LOCKDOWN_API FString CueId(ELDAudioCue Cue);
    LOCKDOWN_API bool ParseCueId(const FString& Id, ELDAudioCue& Out);
}
