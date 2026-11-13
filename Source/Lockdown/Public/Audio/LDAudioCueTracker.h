#pragma once
#include "CoreMinimal.h"
#include "Audio/LDAudioTypes.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDCaptureTypes.h"
#include "Framework/LDMatchLifecycleTypes.h"

// Everything one HUD sample may read for sound: room-public DTOs plus the owner's own projection. There is
// deliberately no observed-unit, actor or other-player input, so a cue can never reveal fogged or private state.
struct FLDAudioFrame
{
    const FLDPublicMatchView* Public = nullptr;
    const FLDKillFeedView* KillFeed = nullptr;
    const FLDResultView* Result = nullptr;
    // Own contender identity and life from the accepted owner projection.
    FGuid Self;
    uint32 OwnMatch = 0;
    uint32 OwnLife = 0;
    ELDLifePhase OwnLifePhase = ELDLifePhase::Unspawned;
    // Own projections; the tracker ignores them unless they belong to OwnMatch (and OwnLife for combat).
    const FLDCombatOwnerView* OwnCombat = nullptr;
    const FLDProgressionView* OwnProgression = nullptr;
    // Authority clock sample, used only for the same notice freshness rule as the kill-feed banner.
    bool bClock = false;
    int64 NowMs = 0;
    // Local presentation time, used only to limit repeats.
    double LocalSeconds = 0;
};

// Turns projection changes and own receipts into cues: each event sounds once, the first sample of a match only
// primes the state, and receipts of another match or life, duplicates or late answers stay silent.
class LOCKDOWN_API FLDAudioCueTracker
{
public:
    static constexpr int64 KillNoticeFreshMs = 6000;
    static constexpr double CaptureRepeatSeconds = 1.0;
    static constexpr double HitRepeatSeconds = 0.2;
    static constexpr int64 ResultFreshMs = 10000;

    void Observe(const FLDAudioFrame& Frame, TArray<ELDAudioCue>& Out);
    void ObserveCombatReceipt(const FLDCombatActionReceipt& Receipt, uint32 OwnMatch, uint32 OwnLife, TArray<ELDAudioCue>& Out);
private:
    void ObservePublic(const FLDAudioFrame& Frame, TArray<ELDAudioCue>& Out);
    void ObserveOwn(const FLDAudioFrame& Frame, TArray<ELDAudioCue>& Out);

    uint32 PublicGeneration = 0;
    bool bOpenSeen = false;
    uint64 CircleEpoch = 0;
    FGuid GraffitiSeen;
    double LastCaptureCueAt = -1e9;
    uint32 KillGeneration = 0;
    uint64 KillSerial = 0;
    FGuid ResultId;
    uint32 ShowdownGeneration = 0;
    int64 ShowdownSeenAtMs = -1;

    uint32 LifeMatch = 0;
    uint32 LifeNumber = 0;
    ELDLifePhase LifePhase = ELDLifePhase::Unspawned;
    uint32 HitMatch = 0;
    uint32 HitLife = 0;
    uint64 HitRevision = 0;
    int64 HitHPP2 = 0;
    int64 HitMaxHPP2 = 0;
    int64 HitShieldP2 = 0;
    int64 HitShieldEndsAtMs = 0;    // end of the shield status listed with the baseline; 0 when none was listed
    double LastHitAt = -1e9;
    int64 AttackReleasedAtMs = 0;   // the last own release (any kind) seen in this life; 0 when none was released
    uint32 GrowthMatch = 0;
    uint8 GrowthLevel = 0;
    uint8 GrowthEarned = 0;

    uint32 CastMatch = 0;
    uint32 CastLife = 0;
    TArray<uint64, TInlineAllocator<16>> CastKeys;
};

namespace LDAudio
{
    // Appends each cue at most once, keeping the first occurrence order.
    LOCKDOWN_API void AddUnique(TArray<ELDAudioCue>& Out, ELDAudioCue Cue);
}
