#pragma once

#include "CoreMinimal.h"
#include "Navigation/LDOrderTypes.h"
#include "Vision/LDViewTypes.h"

// Authority order book of one controller, bound to one match and life. Its request watermark and activation
// throttle only mean something against that match's clock (which restarts at 0 in every match) and that life's
// identities, while the controller outlives both: a new binding or a match reset starts all of it over together.
struct FLDOrderBook
{
    uint32 Match=0;
    uint32 Life=0;
    // Newest identity received for this binding; a later order must exceed it.
    FLDMoveIdentity Received;
    // Earliest authority second of this match at which the next non-Stop order may activate.
    double NextActivationSeconds=0;

    bool IsBoundTo(uint32 InMatch, uint32 InLife) const { return Match==InMatch && Life==InLife; }
    void Bind(const FLDMoveIdentity& Identity)
    {
        Reset();
        Match=Received.Match=Identity.Match;
        Life=Received.Life=Identity.Life;
    }
    void Reset() { *this=FLDOrderBook(); }
    bool ActivationDue(double NowSeconds) const { return NowSeconds>=NextActivationSeconds; }
    void ArmActivation(double NowSeconds, double IntervalSeconds) { NextActivationSeconds=NowSeconds+IntervalSeconds; }
};

namespace LDOrderRules
{
    // D03 attack-move acquisition among the currently visible units: the planar-nearest unit within RadiusCm
    // of FootCm, never the attacker itself; equal distances take the smaller observer handle.
    // Returns the unit index, or INDEX_NONE when no candidate qualifies.
    LOCKDOWN_API int32 NearestCandidate(const TArray<FLDObservedUnitView>& Units, const FVector& FootCm,
        double RadiusCm, const FGuid& Self);
}
