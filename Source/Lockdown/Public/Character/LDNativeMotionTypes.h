#pragma once

#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "World/LDWorldQueryTypes.h"

class UPrimitiveComponent;

enum class ELDNativeMotionRecordKind : uint8 { Movement, Adjustment, Support };

struct FLDNativeSupportSample
{
    bool Grounded=false;
    TWeakObjectPtr<UPrimitiveComponent> Floor;
};

struct FLDNativeMotionRecord
{
    uint64 Sequence=0;
    ELDNativeMotionRecordKind Kind=ELDNativeMotionRecordKind::Movement;
    FVector FromFootCm=FVector::ZeroVector;
    FVector ToFootCm=FVector::ZeroVector;
    double StartServerSeconds=0;
    double EndServerSeconds=0;
    bool RootMotion=false;
    FLDNativeSupportSample Support;
};

// Immutable after native scope completion. Record sequence is not a published
// event sequence or a provider Fence; consumers must explicitly release batches.
struct FLDNativeMotionBatch
{
    FLDAuthorityStep Step;
    FLDCombatBodyRef Body;
    FGuid StreamId;
    FLDWorldRevision World;
    FVector StartFootCm=FVector::ZeroVector;
    FVector EndFootCm=FVector::ZeroVector;
    FLDNativeSupportSample InitialSupport;
    FLDNativeSupportSample FinalSupport;
    uint64 PreviousSequence=0;
    uint64 ThroughSequence=0;
    uint32 RevertedTrials=0;
    TArray<FLDNativeMotionRecord> Records;
};
