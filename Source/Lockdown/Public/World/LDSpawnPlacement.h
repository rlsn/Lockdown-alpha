#pragma once

#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "World/LDWorldQueryTypes.h"
#include "UObject/ObjectKey.h"

struct FLDSpawnRequestKey
{
    FLDMatchContext Match;
    FGuid ContenderId;
    uint32 ProposedLife=0;
    uint64 Sequence=0;
    bool IsValid() const { return Match.IsValid() && ContenderId.IsValid() && ProposedLife && Sequence; }
    bool operator==(const FLDSpawnRequestKey& Other) const
    {
        return Match==Other.Match && ContenderId==Other.ContenderId
            && ProposedLife==Other.ProposedLife && Sequence==Other.Sequence;
    }
};

class ULDMapRegistry;

class FLDSpawnSelection
{
public:
    const FLDSpawnRequestKey& Request() const { return Key; }
    FName AnchorId() const { return Anchor; }
private:
    friend class ULDMapRegistry;
    FLDSpawnSelection()=default;
    FObjectKey Owner;
    FLDSpawnRequestKey Key;
    FLDWorldRevision World;
    FName Anchor;
};

class FLDSpawnReservation
{
public:
    const FLDSpawnRequestKey& Request() const { return Key; }
    const FLDLegalGround& Ground() const { return Placement; }
    const FVector& BodyOrigin() const { return Origin; }
    FName AnchorId() const { return Anchor; }
    int32 OffsetIndex() const { return Offset; }
private:
    friend class ULDMapRegistry;
    FLDSpawnReservation()=default;
    FObjectKey Owner;
    FLDSpawnRequestKey Key;
    FGuid Token;
    FName Anchor;
    int32 Offset=INDEX_NONE;
    FLDLegalGround Placement;
    FVector Origin=FVector::ZeroVector;
};

namespace LDSpawnPlacement
{
    LOCKDOWN_API FVector StandingOrigin(const FLDLegalGround& Ground);
    LOCKDOWN_API bool OverlapsStatic(UWorld& World, const FVector& Origin, const FCollisionShape& Capsule);

    // Rejected samples must be redrawn from the same RNG, not mapped modulo the bound.
    LOCKDOWN_API bool TryUniformSample(uint32 Sample, uint32 Bound, uint32& Index);

    // The registry's memo of a contender's latest spawn request.
    struct FPriorRequest
    {
        FLDSpawnRequestKey Request;
        bool bFinalized = false;
        bool bHeld = false;
        // #198: the authority dropped this request unfinalized (a blocked phase entry that is placed elsewhere or tried
        // again jointly); it never replays, and the same life may be placed under a newer sequence.
        bool bWithdrawn = false;
    };
    enum class EAdmission : uint8 { New, Replay, Conflict, Refused };
    // Replay: the same request again (its recorded selection answers). Conflict: the same identity with other terms.
    // New: a newer sequence after a finalized request for a later life, or after a withdrawn one for the same or a later
    // life. Refused: anything else (an older or still-pending identity, or a withdrawn one repeated).
    LOCKDOWN_API EAdmission Admit(const FPriorRequest* Previous, const FLDSpawnRequestKey& Request);
}
