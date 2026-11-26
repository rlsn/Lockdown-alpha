#pragma once

#include "CoreMinimal.h"
#include "CollisionShape.h"
#include "World/LDWorldQueryTypes.h"

class FLDDemoContentSnapshot;
class ULDMapRegistry;
struct FLDDemoRational;

namespace LDFieldEcology
{
    double Centimeters(const FLDDemoRational& Value);

    // Finds a max-min spread with randomized starts and tie order. Previous feet are indexed by roster order.
    LOCKDOWN_API bool ChooseFieldSpawns(TConstArrayView<FVector> Candidates, TConstArrayView<TOptional<FVector>> Previous,
        double SeparationCm, FRandomStream& Random, TArray<int32>& Chosen);
    // Lattice points SpacingCm apart over the bounds, row by row from Min plus half a spacing; empty for a bad spacing.
    LOCKDOWN_API void GridPoints(const FBox2D& Bounds, double SpacingCm, TArray<FVector2D>& Out);
    LOCKDOWN_API bool NeedsRecovery(int64 HP, int64 MaxHP, int64 Momentum, int64 MaxMomentum);

    // Uses the authored field's bounds and capture binding, native floor support and complete navigation.
    class FGroundSampler
    {
    public:
        bool Initialize(const ULDMapRegistry& InRegistry, const FLDDemoContentSnapshot& Content, FString& Error);
        bool Resolve(FVector2D Point, FLDLegalGround& Out, double FlatClearanceCm = 0.) const;
        FVector2D Draw(FRandomStream& Random, double EdgeClearanceCm = 0.) const;
        const FBox2D& GetBounds() const { return Bounds; }
        const FLDLegalGround& Capture() const { return Hub; }

    private:
        const ULDMapRegistry* Registry = nullptr;
        FCollisionShape Capsule;
        FBox2D Bounds{ForceInit};
        FLDLegalGround Hub;
    };
}
