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
    // 9 (the own skill release) is retired: every body's release sounds from its presentation (LDAudioFeel, skill table).
    // 10 and 11 (the shop trade cues) are retired (#144).
    LevelUp = 12,
    SelfDeath,
    SelfRespawn,
    UIClick,
    // A hit absorbed by the own shield.
    SelfShieldHit,
    // 17 (the own basic swing) is retired: every body's swing sounds at its presented release (SwingLight, SwingFinisher).
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
    // Two local layers of the owner's confirmed hit, not the sound of the owner's body being hit.
    SelfDealtImpact,
    SelfDealtAccent,
    // #345 tactical combat & workshop audio cues: parry counter-shock, world pickup, and medal tier fusion.
    // 36 (the own perfect dodge) is retired.
    SelfParry = 37,
    SelfPickup,
    UIMedalFuse,
    // Body sounds of every visible body, on the same presentation hooks as its effects (LDAudioFeel, AUDIO_AND_FEEL sync
    // table): a combo swing (steps 1-2) and the finisher (step 3) at the release, and the struck body's impact by hit look.
    SwingLight,
    SwingFinisher,
    HitBasic,
    HitFinisher,
    HitElectric,
    HitFire,
    HitWater,
    HitMagic,
    // The windup of a cast long enough to charge (cut at its release), and the release of a skill without a sound of its own.
    CastCharge,
    CastRelease,
    // The owner's confirmation accent of a critical hit and of a kill, layered over SelfDealtImpact.
    SelfDealtCrit,
    SelfDealtKill,
    // A visible body's element reaction (ELDCombatReaction), at the reaction's own time.
    ReactionFog,
    ReactionConduct,
    ReactionOverload,
    ReactionMelt,
    ReactionMethane,
    ReactionFreeze,
    ReactionFerment,
    ReactionShatter,
    ReactionCorrode,
    ReactionFrostbite,
    // An element state starting on a visible body (burn, wet, electrified, chilled, rotten): a short sting, no loop.
    StateBurn,
    StateWet,
    StateShock,
    StateChill,
    StateRot,
    // The match's field segment starting (the public phase entering development from the countdown).
    MatchStart,
    // Another visible body knocked out (its fall and dust ring); the own death is SelfDeath.
    BodyFall,
    // A visible public ground state (ELDGroundStateKind Puddle..Field): one quiet loop at its centre while it is shown.
    GroundPuddle,
    GroundIce,
    GroundFire,
    GroundMiasma,
    GroundField,
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

// The user volume buses (settings audio tab). Master scales everything; every sound routes to exactly one of the others.
// There is no voice bus: the Demo has no voice lines.
UENUM()
enum class ELDAudioBus : uint8
{
    Master,
    Music,
    Sfx,
    UI,
    Count UMETA(Hidden)
};

namespace LDAudio
{
    inline constexpr int32 BusCount = int32(ELDAudioBus::Count);
    // Stable technical id ("Master", "Music", "Sfx", "UI"); also the manifest "bus" key.
    LOCKDOWN_API FString BusId(ELDAudioBus Bus);
    LOCKDOWN_API bool ParseBusId(const FString& Id, ELDAudioBus& Out);
    inline constexpr int32 MusicTrackCount = int32(ELDMusicTrack::Count) - 1;
    // Seconds an outgoing track takes to fade out when the next one starts or the music stops.
    inline constexpr float MusicFadeOutSec = 1.5f;
    // Stable technical id (the enum name), e.g. "Menu"; also the manifest and asset-name key. "None" for None.
    LOCKDOWN_API FString MusicTrackId(ELDMusicTrack Track);
    LOCKDOWN_API bool ParseMusicTrackId(const FString& Id, ELDMusicTrack& Out);
    // Menu and Escalation loop; Farm plays once.
    LOCKDOWN_API bool MusicTrackLoops(ELDMusicTrack Track);
    // The ground-state cues loop (they play while their ground is shown); every other cue is a one-shot.
    LOCKDOWN_API bool CueLoops(ELDAudioCue Cue);
    // The loop of a ground state kind (ELDGroundStateKind), None for a kind without one.
    LOCKDOWN_API ELDAudioCue GroundLoopCue(uint8 Kind);

    // Retired numbers below Count, which are not cues.
    inline constexpr int32 RetiredCueCount = 7;
    // Playable cues: every number between None and Count except the retired ones.
    inline constexpr int32 CueCount = int32(ELDAudioCue::Count) - 1 - RetiredCueCount;
    LOCKDOWN_API bool IsPlayableCue(ELDAudioCue Cue);
    // The Index-th playable cue in enum order (the manifest and catalog order); None outside 0..CueCount-1.
    LOCKDOWN_API ELDAudioCue PlayableCue(int32 Index);
    // Stable technical id (the enum name), e.g. "KillNotice"; also the manifest and asset-name key.
    LOCKDOWN_API FString CueId(ELDAudioCue Cue);
    LOCKDOWN_API bool ParseCueId(const FString& Id, ELDAudioCue& Out);
}
