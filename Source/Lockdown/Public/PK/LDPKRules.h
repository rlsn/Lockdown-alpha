#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "PK/LDPKTypes.h"

struct FLDDemoContentParameters;

// #198 session D: the pure PK rules (approved outer parameters, issue #198 comment 6752601175 §2-§4 and §6). Format and
// spawn draw, the shrinking ring and its damage, the outcome judge and the award split. No world access: the authority
// (ALDPKDirector) passes every fact and random stream in.

// Ring times are ms since the PK step's start; radii are cm; damage rates are basis points of the PK-start max HP per
// second.
struct LOCKDOWN_API FLDPKParameters
{
    double ArenaRadiusCm = 0, MidRadiusCm = 0, FinalRadiusCm = 0;
    int64 ShrinkStartMs = 0, MidMs = 0, FinalMs = 0, PreviewMs = 0, MaxMs = 0, TickMs = 0;
    int64 EarlyDamageBp = 0, MidDamageBp = 0, FinalDamageBp = 0;
    int64 TeamFormatBp = 0;
    int64 ParticipationPoints = 0, WinnerPointsMax = 0, BettingPoolMaxPoints = 0;
    int64 ThrowCooldownMs = 0, ThrowTelegraphMs = 0, ThrowSlowBp = 0, ThrowSlowMs = 0, ThrowExposeMs = 0;
    double ThrowRadiusCm = 0, ThrowRangeCm = 0;

    // From the approved content: PK.*, THROW.* and REST.PKMaxSec (MaxMs).
    static bool FromContent(const FLDDemoContentParameters& Content, FLDPKParameters& Out, FString& Error);
    bool Validate(FString& Error) const;
};

// The draw made once after the roster lock. Seats are indices into the sorted entrant list.
struct LOCKDOWN_API FLDPKDraw
{
    ELDPKFormat Format = ELDPKFormat::None;
    uint8 SideCount = 0;
    // 1-based side of every seat.
    TArray<uint8> SideOfSeat;
    // 0..SpawnAnchors-1 index into the PK spawn anchors (counter-clockwise, 60 degrees apart).
    TArray<int32> AnchorOfSeat;
};

// Points of one entrant seat (Bettor == INDEX_NONE) or one bettor (Seat == INDEX_NONE).
struct LOCKDOWN_API FLDPKAward
{
    int32 Seat = INDEX_NONE;
    int32 Bettor = INDEX_NONE;
    int64 Participation = 0, Winner = 0, Bet = 0;
};

namespace LDPKRules
{
    constexpr int32 SpawnAnchors = 6;

    // 0-1 entrants: None; 2: Duel; 3 or 5: FreeForAll; 4 or 6: Teams with probability TeamFormatBp/10000, FreeForAll
    // otherwise. False for a count outside 0..6 or a probability outside 0..10000.
    LOCKDOWN_API bool DrawFormat(int32 Entrants, int64 TeamFormatBp, FRandomStream& Random, ELDPKFormat& Out);
    // Every unlabeled equal 2-partition of N (even, 4 or 6) in canonical form (seat 0 on side 1), deterministic order.
    LOCKDOWN_API void EqualPartitions(int32 N, TArray<TArray<uint8>>& Out);
    // Format, sides and spawn anchors of 2..6 entrants. Teams are a uniform choice among every equal partition; the spawn
    // ring rotation and the seat order inside the chosen anchors are uniform too.
    LOCKDOWN_API bool Draw(int32 Entrants, const FLDPKParameters& Params, FRandomStream& Random, FLDPKDraw& Out, FString& Error);
    LOCKDOWN_API double RingRadiusCm(const FLDPKParameters& Params, int64 ElapsedMs);
    // The radius the ring is heading to and the elapsed ms it gets there, when a preview is due at ElapsedMs (within PreviewMs before a shrink or during it); false otherwise.
    LOCKDOWN_API bool RingPreview(const FLDPKParameters& Params, int64 ElapsedMs, double& OutRadiusCm, int64& OutAtMs);
    // XY distance of the foot from the centre against the radius at ElapsedMs; the boundary is inside.
    LOCKDOWN_API bool IsInsideRing(const FLDPKParameters& Params, const FVector& CentreCm, const FVector& FootCm, int64 ElapsedMs);
    // Damage rate outside the ring at ElapsedMs (0 before the PK step).
    LOCKDOWN_API int64 RingDamageBp(const FLDPKParameters& Params, int64 ElapsedMs);
    // The next multiple of TickMs strictly after ElapsedMs (MAX_int64 when it cannot be represented or TickMs is invalid).
    LOCKDOWN_API int64 NextRingTickMs(const FLDPKParameters& Params, int64 ElapsedMs);
    // Adds MaxHpP2 * rate(bp) * ms over the outside interval [FromMs, ToMs), split at the rate boundaries, in units of
    // P2 * 1e7. False (accumulator unchanged) on a negative or reversed input or an overflow.
    LOCKDOWN_API bool AccumulateRingDamage(const FLDPKParameters& Params, int64 MaxHpP2, int64 FromMs, int64 ToMs, int64& InOutAccumulatorE7);
    // Removes and returns the whole P2 part of the accumulator; the remainder stays for the next settlement.
    LOCKDOWN_API int64 TakeRingDamageP2(int64& InOutAccumulatorE7);
    // Won (OutWinnerSide set) when exactly one side has a living member; Draw when none has, or at the deadline with two
    // or more; Running otherwise. None on malformed input. Never decided by HP, damage or order.
    LOCKDOWN_API ELDPKOutcome Judge(const TArray<uint8>& SideOfSeat, const TArray<bool>& AliveOfSeat, bool bDeadline, uint8& OutWinnerSide);
    // Split Points over Recipients with the floor + rotated remainder rule; Out[i] parallel to the recipients.
    LOCKDOWN_API void SplitWithRotation(int64 Points, int32 Recipients, uint32 Serial, TArray<int64>& Out);
    // Awards of a decided PK. EligibleOfSeat: not forfeited and still present at settlement. Bettors: the eligible bettors
    // locked at the roster lock (sorted id order) with BetSide (0 = no bet) and BettorPresent. Out: one row per seat with
    // non-zero points, then one row per bettor with non-zero points.
    LOCKDOWN_API bool ComputeAwards(const FLDPKParameters& Params, ELDPKOutcome Outcome, uint8 WinnerSide, uint32 Serial,
        const TArray<uint8>& SideOfSeat, const TArray<bool>& EligibleOfSeat, const TArray<uint8>& BetSideOfBettor,
        const TArray<bool>& BettorPresent, TArray<FLDPKAward>& Out, FString& Error);
}
