#pragma once

#include "CoreMinimal.h"

namespace LDDashContactRules
{
    struct FContactInterval
    {
        double FromSeconds=0,ToSeconds=0;
    };
    // Centres of two upright capsules during the same time interval. No contact is a successful query with no fraction.
    LOCKDOWN_API bool FirstContact(const FVector& SourceFrom, const FVector& SourceTo,
        const FVector& TargetFrom, const FVector& TargetTo, double RadiusSum, double StemHalfLengthSum,
        TOptional<double>& OutFraction, FString& Error);
}
