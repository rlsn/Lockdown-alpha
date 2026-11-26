#include "World/LDFieldEcology.h"

#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapRegistry.h"
#include "World/LDSpawnPlacement.h"

double LDFieldEcology::Centimeters(const FLDDemoRational& Value)
{
    return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : 0.;
}

bool LDFieldEcology::ChooseFieldSpawns(TConstArrayView<FVector> Candidates, TConstArrayView<TOptional<FVector>> Previous,
    double SeparationCm, FRandomStream& Random, TArray<int32>& Chosen)
{
    Chosen.Reset();
    TArray<int32> Permutation;
    for (int32 Index = 0; Index < Candidates.Num(); ++Index) { Permutation.Add(Index); }
    for (int32 Index = Permutation.Num() - 1; Index > 0; --Index)
    {
        Permutation.Swap(Index, Random.RandRange(0, Index));
    }
    constexpr double PreviousDistanceCm = 900.;
    for (int32 Start = 0; Start < Permutation.Num(); ++Start)
    {
        Chosen.Reset();
        for (const TOptional<FVector>& Old : Previous)
        {
            int32 Best = INDEX_NONE;
            double BestDistance = 0.;
            for (int32 Offset = 0; Offset < Permutation.Num(); ++Offset)
            {
                const int32 Index = Permutation[(Start + Offset) % Permutation.Num()];
                const FVector& Foot = Candidates[Index];
                if (Old.IsSet() && FVector::DistSquared2D(Old.GetValue(), Foot) < FMath::Square(PreviousDistanceCm))
                {
                    continue;
                }
                double Nearest = TNumericLimits<double>::Max();
                for (int32 Other : Chosen)
                {
                    Nearest = FMath::Min(Nearest, FVector::DistSquared2D(Candidates[Other], Foot));
                }
                if (Nearest >= FMath::Square(SeparationCm) && Nearest > BestDistance)
                {
                    Best = Index;
                    BestDistance = Nearest;
                }
            }
            if (Best == INDEX_NONE) { break; }
            Chosen.Add(Best);
        }
        if (Chosen.Num() == Previous.Num()) { return true; }
    }
    // The farthest-point passes commit to each pick. A bounded search that takes picks back finds the layouts they miss.
    Chosen.Reset();
    int32 Budget = 200000;
    const TFunction<bool()> Place = [&]() -> bool
    {
        if (Chosen.Num() == Previous.Num()) { return true; }
        const TOptional<FVector>& Old = Previous[Chosen.Num()];
        for (int32 Index : Permutation)
        {
            if (--Budget < 0) { return false; }
            const FVector& Foot = Candidates[Index];
            if ((Old.IsSet() && FVector::DistSquared2D(Old.GetValue(), Foot) < FMath::Square(PreviousDistanceCm))
                || Chosen.ContainsByPredicate([&](int32 Other)
                    { return FVector::DistSquared2D(Candidates[Other], Foot) < FMath::Square(SeparationCm); }))
            {
                continue;
            }
            Chosen.Add(Index);
            if (Place()) { return true; }
            Chosen.Pop();
        }
        return false;
    };
    if (Place()) { return true; }
    Chosen.Reset();
    return false;
}

bool LDFieldEcology::NeedsRecovery(int64 HP, int64 MaxHP, int64 Momentum, int64 MaxMomentum)
{
    return HP > 0 && (HP < MaxHP || Momentum < MaxMomentum);
}

bool LDFieldEcology::FGroundSampler::Initialize(const ULDMapRegistry& InRegistry,
    const FLDDemoContentSnapshot& Content, FString& Error)
{
    const FLDMapBinding* Binding = InRegistry.Resolve(FName(*Content.Get().World.CaptureId), InRegistry.Generation);
    const FLDDemoWorldGeometry* Geometry = InRegistry.GetGeometry();
    const FLDDemoWorldSurface* Surface = Binding && Geometry ? Geometry->Surfaces.FindByPredicate(
        [Binding](const FLDDemoWorldSurface& Entry) { return FName(*Entry.Id) == Binding->Surface; }) : nullptr;
    FVector Min, Max;
    if (!Surface || !Surface->MinCm.ToVector(Min, Error) || !Surface->MaxCm.ToVector(Max, Error)
        || Max.X <= Min.X || Max.Y <= Min.Y || !ULDMapRegistry::ContentCapsule(Content, Capsule, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Field placement requires a registered capture surface and playable bounds."); }
        return false;
    }
    ELDWorldQueryFailure Failure;
    if (!InRegistry.QueryGround(InRegistry.Revision(), Binding->Position, Capsule, Binding->Surface, Hub, Failure, Error))
    {
        return false;
    }
    Registry = &InRegistry;
    Bounds = FBox2D(FVector2D(Min), FVector2D(Max));
    Error.Reset();
    return true;
}

void LDFieldEcology::GridPoints(const FBox2D& Bounds, double SpacingCm, TArray<FVector2D>& Out)
{
    Out.Reset();
    if (!Bounds.bIsValid || !FMath::IsFinite(SpacingCm) || SpacingCm <= 0.) { return; }
    for (double Y = Bounds.Min.Y + SpacingCm * .5; Y <= Bounds.Max.Y; Y += SpacingCm)
    {
        for (double X = Bounds.Min.X + SpacingCm * .5; X <= Bounds.Max.X; X += SpacingCm) { Out.Emplace(X, Y); }
    }
}

FVector2D LDFieldEcology::FGroundSampler::Draw(FRandomStream& Random, double EdgeClearanceCm) const
{
    const FVector2D Inset(EdgeClearanceCm, EdgeClearanceCm);
    const FVector2D Min = Bounds.Min + Inset;
    const FVector2D Max = Bounds.Max - Inset;
    if (Min.X >= Max.X || Min.Y >= Max.Y) { return Bounds.GetCenter(); }
    const double X = FMath::Lerp(Min.X, Max.X, double(Random.GetFraction()));
    const double Y = FMath::Lerp(Min.Y, Max.Y, double(Random.GetFraction()));
    return FVector2D(X, Y);
}

bool LDFieldEcology::FGroundSampler::Resolve(FVector2D Point, FLDLegalGround& Out, double FlatClearanceCm) const
{
    if (!Registry || !Bounds.IsInsideOrOn(Point)) { return false; }
    FVector Foot, Normal;
    if (!Registry->ResolveWalkableGround(Point, Foot, &Normal)) { return false; }
    FString Error;
    ELDWorldQueryFailure Failure;
    if (!Registry->QueryGround(Registry->Revision(), Foot, Capsule, Hub.SurfaceId, Out, Failure, Error)) { return false; }
    if (FlatClearanceCm > 0.)
    {
        // A capsule-clear center alone can leave a crate hanging over a ledge or inside nearby cover.
        constexpr double FlatNormalZ = .995;
        constexpr double FlatHeightToleranceCm = 10.;
        const FVector2D Inset(FlatClearanceCm, FlatClearanceCm);
        if (!Bounds.IsInsideOrOn(Point - Inset) || !Bounds.IsInsideOrOn(Point + Inset) || Normal.Z < FlatNormalZ)
        {
            return false;
        }
        for (int32 Direction = 0; Direction < 8; ++Direction)
        {
            const double Angle = Direction * UE_DOUBLE_PI / 4.;
            const FVector2D Rim = Point + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * FlatClearanceCm;
            FVector RimFoot, RimNormal;
            FLDLegalGround RimGround;
            if (!Registry->ResolveWalkableGround(Rim, RimFoot, &RimNormal) || RimNormal.Z < FlatNormalZ
                || FMath::Abs(RimFoot.Z - Foot.Z) > FlatHeightToleranceCm
                || !Registry->QueryGround(Registry->Revision(), RimFoot, Capsule, Hub.SurfaceId, RimGround, Failure, Error))
            {
                return false;
            }
        }
        const FCollisionShape Clearance = FCollisionShape::MakeBox(
            FVector(FlatClearanceCm, FlatClearanceCm, Capsule.GetCapsuleHalfHeight()));
        if (LDSpawnPlacement::OverlapsStatic(*Registry->GetWorld(),
            LDSpawnPlacement::StandingOrigin(Out), Clearance)) { return false; }
    }
    FLDCompletePath Path;
    return Registry->QueryCompletePath(Registry->Revision(), Out, Hub, Capsule, Path, Failure, Error);
}
