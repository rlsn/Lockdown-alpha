#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Framework/LDCaptureTypes.h"

class ULDMapRegistry;
class FLDDemoContentSnapshot;
struct FLDDormZones;

// D26 (#79) 天降外卖 rules, #198 outer §5 schedule. Drop n (1-based) lands at field progress T = StartedAt +
// AIRDROP.LandSec[n-1], is announced AIRDROP.AnnounceLeadSec earlier, and leaves unclaimed AIRDROP.ExpireSec after landing.
// The authority samples a connected field point at announcement using a private seed. Authored points are fallbacks. There
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
    // floor(MaxP2 * Bp / 10000) without overflow for non-negative inputs.
    LOCKDOWN_API int64 ShareP2(int64 MaxP2, int64 Bp);
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
    // The selected fallback list index for schedules driven without a field sampler.
    int32 PointIndex = INDEX_NONE;
    TOptional<LDAirdrop::FPoint> SampledPoint;
    ELDAirdropPhase Phase = ELDAirdropPhase::None;
    // Authority and owner view only; the public view never carries them.
    FGuid ClaimedBy;
    int64 ClaimedAtMs = 0;

    bool IsActive() const { return Schedule.IsValid(); }
    // Moves to the drop NowMs falls in: announcement, landing and expiry are inclusive boundaries; a claimed drop stays
    // claimed until the next announcement. True when the public view changed.
    using FSelectPoint = TFunction<bool(uint64, const LDAirdrop::FPoint*, LDAirdrop::FPoint&)>;
    bool Advance(int64 NowMs, const FSelectPoint& SelectPoint = {});
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
    bool SampleFieldPoint(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content, const FLDDormZones& Dorms,
        const FLDAirdropSchedule& Schedule, uint64 Serial, const FPoint* Previous, FPoint& Out, FString& Error);

    // Creates the schedule and the optional registered fallback point pool with a private match seed.
    LOCKDOWN_API bool Resolve(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content, int64 StartedAtMs,
        const FGuid& LedgerMatch, bool bDeliveryDay, FLDAirdropState& Out, FString& Error);
}
