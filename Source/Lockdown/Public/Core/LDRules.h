#pragma once

#include "CoreMinimal.h"

// Pure rules derived from baseline 74d3d3e; no actor or network dependencies.
namespace LDRules
{
    constexpr double CentimetresPerPixel = 2.0;
    constexpr double TickSeconds = 0.05;
    constexpr double AuthorityInterval = 0.05;
    constexpr double ViewInterval = 0.1;
    constexpr double StaleIntentSeconds = 0.25;
    constexpr double MatchDeadlineSeconds = 720.0;

    inline FVector WorldFromBaseline(const FVector2D& Pixel, int32 VisionLevel = 0)
    {
        return FVector((Pixel.X - 1200.0) * CentimetresPerPixel,
            (800.0 - Pixel.Y) * CentimetresPerPixel, VisionLevel * 200.0);
    }

    inline FVector2D BaselineFromWorld(const FVector& World)
    {
        return FVector2D(World.X / CentimetresPerPixel + 1200.0,
            800.0 - World.Y / CentimetresPerPixel);
    }

    inline double SquaredDistanceFromBaseline(double PixelDistanceSquared)
    {
        return PixelDistanceSquared * CentimetresPerPixel * CentimetresPerPixel;
    }

    struct FDeathCost
    {
        double DelaySeconds = 0.0;
        int64 LostGold = 0;
    };

    // Snapshot the death time once; later phases never extend an existing return.
    inline FDeathCost DeathCost(int32 Level, int64 Gold, double MatchSeconds)
    {
        check(Level >= 1 && Level <= 3 && Gold >= 0 && FMath::IsFinite(MatchSeconds) && MatchSeconds >= 0.0);
        const int64 Rate = 20 + 5 * (Level - 1);
        // Quotient/remainder arithmetic avoids overflowing Gold * Rate.
        const int64 Loss = (Gold / 100) * Rate + ((Gold % 100) * Rate + 99) / 100;
        return {20.0 + 10.0 * (Level - 1) + 5.0 * FMath::FloorToDouble(MatchSeconds / 120.0)
            + (MatchSeconds >= 480.0 ? 45.0 : 0.0), Loss};
    }

    inline int32 LevelFromXp(int64 Xp)
    {
        return Xp >= 80 ? 3 : (Xp >= 30 ? 2 : 1);
    }

    inline bool HasHeartAccess(bool bSeal, uint8 DefensiveFlags)
    {
        return bSeal || DefensiveFlags == 0;
    }

    // Damage/death/contest/hold validation occurs before evaluating this predicate.
    inline bool CanCompleteClaim(double Now, double CompletionAt, bool bStillValid, bool bAlreadyFinished)
    {
        return FMath::IsFinite(Now) && FMath::IsFinite(CompletionAt) && CompletionAt >= 10.0
            && bStillValid && !bAlreadyFinished && Now >= CompletionAt && CompletionAt <= MatchDeadlineSeconds;
    }

    struct FViewCursor
    {
        uint32 Generation = 0;
        uint64 Revision = 0;

        bool Accept(uint32 IncomingGeneration, uint64 IncomingRevision)
        {
            if (IncomingGeneration < Generation || (IncomingGeneration == Generation && IncomingRevision <= Revision))
            {
                return false;
            }
            Generation = IncomingGeneration;
            Revision = IncomingRevision;
            return true;
        }
    };
}
