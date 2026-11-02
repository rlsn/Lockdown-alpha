#pragma once
#include "CoreMinimal.h"
#include "Vision/LDViewTypes.h"

// Local presentation only. Other-subject formatting accepts an authorized observed unit plus public
// roster/content labels, never a canonical actor or another player's owner state.
struct FLDDemoContentDefinitions;
namespace LDHUD
{
    LOCKDOWN_API FString Inspector(const FLDOwnerState& Own);
    // Owner life line from the Lives projection. "Now" is only the matching authority clock sample: without one the
    // remaining time is unavailable, and a live countdown never reads zero before that clock reaches the deadline.
    LOCKDOWN_API FString LifeStatus(const FLDOwnerState& Own,bool bClock,int64 NowMs);
    // SC-07 whitelist only: name, level, HP, MP, public statuses and two equipment slots.
    LOCKDOWN_API FString ObservedUnit(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec);
    // Map fractions over the registered ground bounds that observation coverage also uses.
    LOCKDOWN_API FVector2D MapPoint(const FVector& World,const FBox2D& Ground);
    LOCKDOWN_API FVector MapWorld(const FVector2D& Fraction,const FBox2D& Ground);
}
