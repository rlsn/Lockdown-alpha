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
    // Rejected samples must be redrawn from the same RNG, not mapped modulo the bound.
    LOCKDOWN_API bool TryUniformSample(uint32 Sample, uint32 Bound, uint32& Index);
}
