#pragma once

#include "CoreMinimal.h"

namespace LDDashContactRules
{
    struct FContactInterval
    {
        double FromSeconds=0,ToSeconds=0;
    };
    struct FGroundBand
    {
        FVector FromCm=FVector::ZeroVector,ToCm=FVector::ZeroVector;
        double StartSeconds=0,EndSeconds=0,DurationSeconds=0;
    };
    struct FGroundBandShape
    {
        FVector FromCm=FVector::ZeroVector,ToCm=FVector::ZeroVector;
    };
    // Centres of two upright capsules during the same time interval. No contact is a successful query with no fraction.
    LOCKDOWN_API bool FirstContact(const FVector& SourceFrom, const FVector& SourceTo,
        const FVector& TargetFrom, const FVector& TargetTo, double RadiusSum, double StemHalfLengthSum,
        TOptional<double>& OutFraction, FString& Error);
    // Public geometry derived from a completed native movement record: the head follows the walked path and the tail
    // expires at the same rate. Neither a future portion nor an expired portion is a contact surface.
    LOCKDOWN_API bool SampleGroundBand(const FGroundBand& Band,double AtSeconds,TOptional<FGroundBandShape>& Out,FString& Error);
    LOCKDOWN_API bool GroundBandContacts(const FVector& FromCm,const FVector& ToCm,double FromSeconds,double ToSeconds,
        const FGroundBand& Band,double HalfWidthCm,TArray<FContactInterval>& Out,FString& Error);
}
