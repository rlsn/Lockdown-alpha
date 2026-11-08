#include "Combat/LDCombatKeywords.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDSkillGrowth.h"

FLDKeywordTerms FLDKeywordTerms::From(const FLDDemoContentParameters& Parameters)
{
    FLDKeywordTerms Terms;
    Terms.BreachPerStackP2 = Parameters.BreachPerStackP2;
    Terms.BreachMaxStacks = int32(FMath::Clamp<int64>(Parameters.BreachMaxStacks, 0, MAX_int32));
    Terms.BreachMs = Parameters.BreachMs;
    Terms.BurnPerSecP2 = Parameters.BurnPerSecP2;
    Terms.BurnAPRatioBp = Parameters.BurnAPRatioBp;
    Terms.BurnTickMs = Parameters.BurnTickMs;
    Terms.BurnMs = Parameters.BurnMs;
    Terms.BurnMaxStacks = int32(FMath::Clamp<int64>(Parameters.BurnMaxStacks, 0, MAX_int32));
    Terms.LifestealCapBp = Parameters.LifestealCapBp;
    Terms.SkillGrowthPerLevelBp = Parameters.SkillGrowthPerLevelBp;
    Terms.SkillGrowthPerLevelAfter10Bp = Parameters.SkillGrowthPerLevelAfter10Bp;
    return Terms;
}

namespace LDCombatKeywords
{
    int32 BreachStacks(const FLDCombatSharedBreach& Breach, int64 AtMs)
    {
        return Breach.ExpiresAtMs > AtMs ? FMath::Max(Breach.Stacks, 0) : 0;
    }

    bool ApplyBreach(FLDCombatSharedBreach& Breach, int32 Stacks, int64 AtMs, const FLDKeywordTerms& Terms)
    {
        if (Stacks <= 0 || Terms.BreachMaxStacks <= 0 || Terms.BreachMs <= 0 || AtMs > MAX_int64 - Terms.BreachMs) { return false; }
        const int32 Kept = BreachStacks(Breach, AtMs);
        Breach.Stacks = FMath::Min(Terms.BreachMaxStacks, Kept + FMath::Min(Stacks, Terms.BreachMaxStacks));
        Breach.ExpiresAtMs = AtMs + Terms.BreachMs;
        return true;
    }

    int64 BreachFlatP2(const FLDCombatSharedBreach& Breach, int64 AtMs, const FLDKeywordTerms& Terms)
    {
        return int64(BreachStacks(Breach, AtMs)) * FMath::Max<int64>(Terms.BreachPerStackP2, 0);
    }

    int64 BreachedResistanceP2(int64 ResistanceP2, const FLDCombatSharedBreach& Breach, int64 AtMs, const FLDKeywordTerms& Terms)
    {
        return FMath::Max<int64>(0, ResistanceP2 - FMath::Clamp<int64>(BreachFlatP2(Breach, AtMs, Terms), 0, MAX_int32));
    }

    bool ExpireBreach(FLDCombatSharedBreach& Breach, int64 AtMs)
    {
        if (Breach.Stacks == 0 || Breach.ExpiresAtMs > AtMs) { return false; }
        Breach = FLDCombatSharedBreach();
        return true;
    }

    int64 BurnLayerPerSecondP2(int32 Level, int64 SourceAPP2, const FLDKeywordTerms& Terms)
    {
        int64 Base = 0;
        const int64 Factor = LDSkillGrowth::FactorBp(Terms.SkillGrowthPerLevelBp, Terms.SkillGrowthPerLevelAfter10Bp,
            uint8(FMath::Clamp(Level, 1, int32(MAX_uint8))));
        if (!LDSkillGrowth::GrowP2(FMath::Max<int64>(Terms.BurnPerSecP2, 0), Factor, Base)) { Base = 0; }
        const int64 AP = FMath::Max<int64>(SourceAPP2, 0), Ratio = FMath::Max<int64>(Terms.BurnAPRatioBp, 0);
        return Base + AP / 10000 * Ratio + AP % 10000 * Ratio / 10000;
    }

    bool BurnActive(const FLDCombatBurn& Burn, int64 AtMs)
    {
        return !Burn.Layers.IsEmpty() && AtMs <= Burn.ExpiresAtMs && Burn.NextTickAtMs <= Burn.ExpiresAtMs;
    }

    bool ApplyBurn(FLDCombatBurn& Burn, const FLDCombatBurnLayer& Layer, int32 Count, int64 AtMs, const FLDKeywordTerms& Terms)
    {
        if (Count <= 0 || Layer.SourceAPP2 < 0 || Layer.Level < 1 || Terms.BurnMaxStacks <= 0 || Terms.BurnTickMs <= 0 || Terms.BurnMs < Terms.BurnTickMs
            || AtMs > MAX_int64 - Terms.BurnMs)
        {
            return false;
        }
        if (!BurnActive(Burn, AtMs))
        {
            Burn = FLDCombatBurn();
            Burn.NextTickAtMs = AtMs + Terms.BurnTickMs;
        }
        for (int32 Index = 0; Index < FMath::Min(Count, Terms.BurnMaxStacks); ++Index)
        {
            if (Burn.Layers.Num() >= Terms.BurnMaxStacks) { Burn.Layers.RemoveAt(0); }
            Burn.Layers.Add(Layer);
        }
        Burn.ExpiresAtMs = AtMs + Terms.BurnMs;
        return true;
    }

    void BurnTickShares(const FLDCombatBurn& Burn, const FLDKeywordTerms& Terms, TArray<FLDBurnTickShare>& Out)
    {
        Out.Reset();
        for (const FLDCombatBurnLayer& Layer : Burn.Layers)
        {
            const int64 PerSecond = BurnLayerPerSecondP2(Layer.Level, Layer.SourceAPP2, Terms);
            const int64 Tick = PerSecond / 1000 * Terms.BurnTickMs + PerSecond % 1000 * Terms.BurnTickMs / 1000;
            FLDBurnTickShare* Share = Out.FindByPredicate([&Layer](const FLDBurnTickShare& Row)
            { return Row.SourceEntity == Layer.SourceEntity && Row.SourceBody == Layer.SourceBody && Row.Source == Layer.Source; });
            if (!Share)
            {
                Share = &Out.AddDefaulted_GetRef();
                Share->SourceEntity = Layer.SourceEntity;
                Share->SourceBody = Layer.SourceBody;
                Share->CreditOwner = Layer.CreditOwner;
                Share->Source = Layer.Source;
            }
            Share->DamageP2 += Tick;
        }
    }

    bool ExpireBurn(FLDCombatBurn& Burn, int64 AtMs)
    {
        if (Burn.Layers.IsEmpty() && Burn.ExpiresAtMs == 0) { return false; }
        if (BurnActive(Burn, AtMs)) { return false; }
        Burn = FLDCombatBurn();
        return true;
    }

    int32 RemoveBurnSource(FLDCombatBurn& Burn, const FGuid& Source)
    {
        const int32 Removed = Burn.Layers.RemoveAll([&Source](const FLDCombatBurnLayer& Layer) { return Layer.SourceEntity == Source; });
        if (Removed > 0 && Burn.Layers.IsEmpty()) { Burn = FLDCombatBurn(); }
        return Removed;
    }

    bool SegmentEntry(const FVector& From, const FVector& To, const FVector& Centre, double Reach, double& OutFraction)
    {
        const FVector2D Delta(To.X-From.X,To.Y-From.Y), Offset(From.X-Centre.X,From.Y-Centre.Y);
        const double Outside=Offset.SizeSquared()-Reach*Reach;
        if (Outside<=0) { OutFraction=0; return true; }
        const double LengthSquared=Delta.SizeSquared();
        if (LengthSquared<=UE_DOUBLE_SMALL_NUMBER) { return false; }
        const double Half=FVector2D::DotProduct(Offset,Delta);
        const double Discriminant=Half*Half-LengthSquared*Outside;
        if (Discriminant<0) { return false; }
        const double Fraction=(-Half-FMath::Sqrt(Discriminant))/LengthSquared;
        if (Fraction<0 || Fraction>1) { return false; }
        OutFraction=Fraction;
        return true;
    }

    void PierceContacts(const FVector& From, const FVector& To, double FlightRadiusCm, const FGuid& SourceEntity,
        const TArray<FLDCombatBodyRef>& Struck, const TArray<FLDFlightCandidate>& Candidates, TArray<int32>& OutIndices)
    {
        OutIndices.Reset();
        TArray<TPair<double,int32>> Entered;
        for (int32 Index = 0; Index < Candidates.Num(); ++Index)
        {
            const FLDFlightCandidate& Candidate = Candidates[Index];
            double Entry = 0;
            if (Candidate.Body.Victim.EntityId == SourceEntity || Struck.Contains(Candidate.Body)
                || !SegmentEntry(From, To, Candidate.FootCm, FlightRadiusCm + Candidate.RadiusCm, Entry))
            {
                continue;
            }
            Entered.Add({Entry, Index});
        }
        Entered.StableSort([](const TPair<double,int32>& Left, const TPair<double,int32>& Right) { return Left.Key < Right.Key; });
        for (const TPair<double,int32>& Row : Entered) { OutIndices.Add(Row.Value); }
    }

    int64 CapLifestealBp(int64 SumBp, const FLDKeywordTerms& Terms)
    {
        return FMath::Clamp<int64>(SumBp, 0, FMath::Max<int64>(Terms.LifestealCapBp, 0));
    }

    bool KnockbackHitWall(const FVector& StartFootCm, const FVector& EndFootCm, double ThresholdCm)
    {
        if (StartFootCm.ContainsNaN() || EndFootCm.ContainsNaN() || !FMath::IsFinite(ThresholdCm) || ThresholdCm <= 0) { return false; }
        return FVector::Dist2D(StartFootCm, EndFootCm) < ThresholdCm;
    }

    int64 ShieldBurstShareP2(int64 GrantedP2, int64 RatioBp, bool bNearbyEnemy)
    {
        const int64 AppliedRatio = bNearbyEnemy ? RatioBp / 2 : RatioBp;
        if (GrantedP2 <= 0 || AppliedRatio <= 0 || AppliedRatio > 10000) { return 0; }
        return GrantedP2 / 10000 * AppliedRatio + GrantedP2 % 10000 * AppliedRatio / 10000;
    }
}
