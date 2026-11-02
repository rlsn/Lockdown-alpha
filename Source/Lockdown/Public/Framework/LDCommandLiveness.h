#pragma once
#include "CoreMinimal.h"

namespace LDCommandLiveness
{
    // Issued orders tolerate transient frame/network stalls like navigation.
    // Explicit cancellation and gameplay interruption never wait for this timer.
    constexpr double Seconds=2.;
    inline bool Fresh(double Now,double Last,double Window=Seconds)
    {return FMath::IsFinite(Now)&&FMath::IsFinite(Last)&&Now>=Last-1.e-6&&Now-Last<Window;}
}
