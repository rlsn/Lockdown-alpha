#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Framework/LDCaptureTypes.h"

class ULDMapRegistry;
class FLDDemoContentSnapshot;

// D26 (#79) 天降外卖 rules, #198 outer §5 schedule. Drop n (1-based) lands at field progress T = StartedAt +
// AIRDROP.LandSec[n-1] on one of the public airdrop points, is announced AIRDROP.AnnounceLeadSec earlier, and leaves
// unclaimed AIRDROP.ExpireSec after landing; there is no drop after the list. Each drop's point is drawn from a
// server-only seed among the points other than the previous drop's, so it is known only from its announcement. There
// is one current drop: the next announcement replaces it (claimed or expired). The first living, unprotected contender
// whose foot is inside the closed AIRDROP.PickupRadiusCm disk of a landed drop claims it once (nearest first; an exact
// tie is a seeded server draw, not a contender-id order). The public view never names the claimer; the claimer learns
// it from its own view.
namespace LDAirdrop
{
    struct FPoint
    {
        FName Id;
        FVector PositionCm = FVector::ZeroVector;
    };

    struct FCandidate
    {
        FGuid Contender;
        FVector FootCm = FVector::ZeroVector;
        // Respawn protection: a protected contender cannot claim (it could otherwise take the drop while invulnerable).
        bool bProtected = false;
    };

    // A 64-bit mix (splitmix64 finaliser); the airdrop draws are pure functions of the server seed.
    LOCKDOWN_API uint64 Mix(uint64 Value);
    // The point of drop Serial (1-based) among Count points: any point for the first drop, otherwise one of the
    // Count - 1 points other than Previous. INDEX_NONE when Count is not positive.
    LOCKDOWN_API int32 NextPoint(uint64 Seed, uint64 Serial, int32 Previous, int32 Count);
    // The nearest unprotected candidate inside the closed pickup disk; an exact tie goes to the lowest draw of TieSeed
    // and the contender, so neither the input order nor the contender id decides. False when none.
    LOCKDOWN_API bool PickClaimer(const FVector& CentreCm, double RadiusCm, TConstArrayView<FCandidate> Candidates,
        uint64 TieSeed, FGuid& Out);
}

struct LOCKDOWN_API FLDAirdropSchedule
{
    int64 StartedAtMs = 0;
    // Landing offsets from StartedAtMs, strictly increasing; one drop each.
    TArray<int64> LandMs;
    int64 AnnounceLeadMs = 0;
    int64 ExpireMs = 0;
    // Server-only seed for the point draws and pickup ties, fresh per match and never published.
    uint64 Seed = 0;
    TArray<LDAirdrop::FPoint> Points;
    double PickupRadiusCm = 0;

    bool IsValid() const;
    int32 NumDrops() const { return LandMs.Num(); }
    // Overflow-checked instants of drop Index (0-based); false when Index is not a scheduled drop or not representable.
    bool LandAtMs(uint64 Index, int64& Out) const;
    bool AnnounceAtMs(uint64 Index, int64& Out) const;
    bool ExpireAtMs(uint64 Index, int64& Out) const;
};

// Authority state of the current drop. Serial is the 1-based drop number; zero before the first announcement.
struct LOCKDOWN_API FLDAirdropState
{
    FLDAirdropSchedule Schedule;
    uint64 Serial = 0;
    // Index into Schedule.Points of the current drop; INDEX_NONE before the first announcement.
    int32 PointIndex = INDEX_NONE;
    ELDAirdropPhase Phase = ELDAirdropPhase::None;
    // Authority and owner view only; the public view never carries them.
    FGuid ClaimedBy;
    int64 ClaimedAtMs = 0;

    bool IsActive() const { return Schedule.IsValid(); }
    // Moves to the drop NowMs falls in: announcement, landing and expiry are inclusive boundaries; a claimed drop stays
    // claimed until the next announcement. True when the public view changed.
    bool Advance(int64 NowMs);
    // The next announcement, landing or expiry boundary strictly after NowMs; MAX_int64 when none.
    int64 NextDueMs(int64 NowMs) const;
    // Landed -> Claimed by Contender at NowMs; false in any other phase.
    bool Claim(const FGuid& Contender, int64 NowMs);
    // The drop lies on its point (announced or landed, not yet claimed or expired): charms keep off it.
    bool HoldsPoint() const { return Phase == ELDAirdropPhase::Announced || Phase == ELDAirdropPhase::Landed; }
    const LDAirdrop::FPoint* CurrentPoint() const;
    // The tie draw of the current drop's pickup.
    uint64 TieSeed() const { return LDAirdrop::Mix(Schedule.Seed ^ LDAirdrop::Mix(~Serial)); }
    FLDAirdropView View() const;
};

namespace LDAirdrop
{
    // Binds every compiled airdrop anchor to its registered ground position and the approved AIRDROP parameters for a
    // match that started at StartedAtMs, with a fresh server-only seed. No fallback values.
    LOCKDOWN_API bool Resolve(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content, int64 StartedAtMs,
        const FGuid& LedgerMatch, FLDAirdropState& Out, FString& Error);
}
