#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Framework/LDCaptureTypes.h"
#include "World/LDAirdrop.h"

class FLDDemoContentSnapshot;

// #198 field charms (outer parameters §7). Waves land at field progress T = CHARM.FirstWave + n * CHARM.WaveInterval for
// n < CHARM.WaveCount (T only advances while a field segment runs; rests and PK freeze it). Each wave draws CHARM.PerWave
// distinct kinds of the three uniformly without replacement and places them on distinct airdrop points, excluding the
// point of the announced or landed airdrop and the points of charms still lying. Too few free points skip the missing
// instances with a logged reason; nothing is granted instead. A charm lies CHARM.Lifetime of field progress from its
// wave instant; entering a rest removes every unclaimed charm. A living, unprotected contender whose foot is inside
// the closed CHARM.PickupRadius disk claims it; several at the same boundary are decided once by a fixed fair random
// key, not by distance or contender order.
namespace LDCharms
{
    inline constexpr int32 KindCount = 3;
    // The timed-bonus source of the move-speed charm: a later pickup replaces (refreshes) it, never stacks.
    LOCKDOWN_API FName MoveSpeedSource();

    // Heal, Mana, MoveSpeed for Index 0..2; None otherwise.
    LOCKDOWN_API ELDCharmKind KindAt(int32 Index);
    LOCKDOWN_API const TCHAR* KindName(ELDCharmKind Kind);
    // Count distinct values of [0, N) drawn uniformly without replacement (a seeded partial Fisher-Yates), in draw
    // order. Empty when Count or N is not positive; at most N values.
    LOCKDOWN_API void DrawDistinct(uint64 Seed, int32 N, int32 Count, TArray<int32>& Out);
    // The eligible candidate (valid, unprotected, foot inside the closed disk) with the lowest fair random key of
    // TieSeed and its id. False when none.
    LOCKDOWN_API bool PickClaimer(const FVector& CentreCm, double RadiusCm, TConstArrayView<LDAirdrop::FCandidate> Candidates,
        uint64 TieSeed, FGuid& Out);
    // floor(MaxP2 * Bp / 10000) without overflow for non-negative inputs.
    LOCKDOWN_API int64 ShareP2(int64 MaxP2, int64 Bp);
}

struct LOCKDOWN_API FLDCharmSchedule
{
    int64 FirstWaveMs = 0;
    int64 WaveIntervalMs = 0;
    int64 WaveCount = 0;
    int64 PerWave = 0;
    int64 LifetimeMs = 0;
    double PickupRadiusCm = 0;
    // Server-only, never published.
    uint64 Seed = 0;
    // The airdrop points, in the airdrop schedule's order (so an airdrop point index names the same point here).
    TArray<LDAirdrop::FPoint> Points;

    bool IsValid() const;
    // Overflow-checked field instant of wave Index (0-based); false when not representable or out of range.
    bool WaveAtMs(int64 Index, int64& Out) const;
};

struct FLDCharmInstance
{
    uint64 Serial = 0;
    ELDCharmKind Kind = ELDCharmKind::None;
    int32 PointIndex = INDEX_NONE;
    int64 Wave = 0;
    int64 SpawnFieldMs = 0;
    int64 ExpireFieldMs = 0;
};

struct FLDCharmSkip
{
    int64 Wave = 0;
    int32 Missing = 0;
    const TCHAR* Reason = TEXT("");
};

// What one Advance did, in order, for the authority log.
struct FLDCharmAdvance
{
    TArray<FLDCharmInstance> Expired;
    TArray<FLDCharmInstance> Spawned;
    TArray<FLDCharmSkip> Skipped;
    bool Changed() const { return !Expired.IsEmpty() || !Spawned.IsEmpty(); }
};

// What a claim applied to the claimer's body, for the authority log.
struct FLDCharmEffectFact
{
    int64 HpBeforeP2 = 0;
    int64 HpAfterP2 = 0;
    int64 MpBeforeP2 = 0;
    int64 MpAfterP2 = 0;
    // Move speed: the refreshed bonus end, authority ms.
    int64 BonusUntilMs = 0;
};

struct LOCKDOWN_API FLDCharmState
{
    FLDCharmSchedule Schedule;
    // Waves already placed or skipped.
    int64 WavesDone = 0;
    uint64 LastSerial = 0;
    // The charms lying now, in serial order.
    TArray<FLDCharmInstance> Active;

    bool IsActive() const { return Schedule.IsValid(); }
    // Field progress FieldMs never regresses. Expires every charm whose lifetime ended at or before FieldMs, then places
    // every wave due at or before FieldMs; a wave whose whole lifetime already passed is skipped (Overdue).
    // BlockedPoint is the point index the current airdrop holds, INDEX_NONE when none.
    void Advance(int64 FieldMs, int32 BlockedPoint, FLDCharmAdvance& Out);
    // The next wave or expiry strictly after FieldMs; MAX_int64 when none.
    int64 NextDueFieldMs(int64 FieldMs) const;
    // Removes a lying charm; false when Serial is not lying.
    bool Remove(uint64 Serial, FLDCharmInstance& Out);
    // Rest entry: every unclaimed charm leaves without any reward.
    void Clear(TArray<FLDCharmInstance>& Out);
    // The claim key seed of one charm.
    uint64 TieSeed(uint64 Serial) const { return LDAirdrop::Mix(Schedule.Seed ^ LDAirdrop::Mix(~Serial)); }
    const LDAirdrop::FPoint* PointOf(const FLDCharmInstance& Charm) const;
    void View(TArray<FLDCharmView>& Out) const;
};

namespace LDCharms
{
    // Binds the approved CHARM parameters to the match's airdrop points (already bound to registered ground by
    // LDAirdrop::Resolve) with a server-only seed derived from the airdrop seed. No fallback values.
    LOCKDOWN_API bool Resolve(const FLDAirdropState& Airdrop, const FLDDemoContentSnapshot& Content, FLDCharmState& Out,
        FString& Error);
}
