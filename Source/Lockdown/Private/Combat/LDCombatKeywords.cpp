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

    int64 BurnLayerTickP2(const FLDCombatBurnLayer& Layer, const FLDKeywordTerms& Terms)
    {
        const int64 PerSecond = BurnLayerPerSecondP2(Layer.Level, Layer.SourceAPP2, Terms);
        const int64 TickMs = FMath::Max<int64>(Terms.BurnTickMs, 0);
        return PerSecond / 1000 * TickMs + PerSecond % 1000 * TickMs / 1000;
    }

    int32 BurnLayerTicksLeft(const FLDCombatBurnLayer& Layer, const FLDKeywordTerms& Terms)
    {
        if (Terms.BurnTickMs <= 0 || Layer.NextTickAtMs <= 0 || Layer.NextTickAtMs > Layer.ExpiresAtMs) { return 0; }
        return int32(FMath::Min<int64>((Layer.ExpiresAtMs - Layer.NextTickAtMs) / Terms.BurnTickMs + 1, MAX_int32));
    }

    bool BurnActive(const FLDCombatBurn& Burn, int64 AtMs)
    {
        return Burn.Layers.ContainsByPredicate([AtMs](const FLDCombatBurnLayer& Layer)
        { return AtMs <= Layer.ExpiresAtMs && Layer.NextTickAtMs <= Layer.ExpiresAtMs; });
    }

    int64 BurnExpiresAtMs(const FLDCombatBurn& Burn)
    {
        int64 Latest = 0;
        for (const FLDCombatBurnLayer& Layer : Burn.Layers) { Latest = FMath::Max(Latest, Layer.ExpiresAtMs); }
        return Latest;
    }

    int64 NextBurnTickAtMs(const FLDCombatBurn& Burn)
    {
        int64 Earliest = MAX_int64;
        for (const FLDCombatBurnLayer& Layer : Burn.Layers) { Earliest = FMath::Min(Earliest, Layer.NextTickAtMs); }
        return Earliest;
    }

    bool ApplyBurn(FLDCombatBurn& Burn, const FLDCombatBurnLayer& Layer, int32 Count, int64 AtMs, const FLDKeywordTerms& Terms)
    {
        if (Count <= 0 || Layer.SourceAPP2 < 0 || Layer.Level < 1 || Terms.BurnMaxStacks <= 0 || Terms.BurnTickMs <= 0 || Terms.BurnMs < Terms.BurnTickMs
            || AtMs < 0 || AtMs > MAX_int64 - Terms.BurnMs)
        {
            return false;
        }
        ExpireBurn(Burn, AtMs);
        FLDCombatBurnLayer Fresh = Layer;
        Fresh.ExpiresAtMs = AtMs + Terms.BurnMs;
        Fresh.NextTickAtMs = AtMs + Terms.BurnTickMs;
        for (int32 Index = 0; Index < FMath::Min(Count, Terms.BurnMaxStacks); ++Index)
        {
            if (Burn.Layers.Num() >= Terms.BurnMaxStacks)
            {
                int32 First = 0;
                for (int32 Other = 1; Other < Burn.Layers.Num(); ++Other)
                {
                    if (Burn.Layers[Other].ExpiresAtMs < Burn.Layers[First].ExpiresAtMs) { First = Other; }
                }
                Burn.Layers.RemoveAt(First);
            }
            Burn.Layers.Add(Fresh);
        }
        return true;
    }

    namespace
    {
        FLDBurnTickShare& ShareOf(TArray<FLDBurnTickShare>& Out, const FLDCombatBurnLayer& Layer)
        {
            if (FLDBurnTickShare* Share = Out.FindByPredicate([&Layer](const FLDBurnTickShare& Row)
                { return Row.SourceEntity == Layer.SourceEntity && Row.SourceBody == Layer.SourceBody && Row.Source == Layer.Source; }))
            {
                return *Share;
            }
            FLDBurnTickShare& Share = Out.AddDefaulted_GetRef();
            Share.SourceEntity = Layer.SourceEntity;
            Share.SourceBody = Layer.SourceBody;
            Share.CreditOwner = Layer.CreditOwner;
            Share.Source = Layer.Source;
            return Share;
        }
    }

    bool PopBurnTick(FLDCombatBurn& Burn, int64 AtMs, const FLDKeywordTerms& Terms, int64& OutTickAtMs, TArray<FLDBurnTickShare>& Out)
    {
        Out.Reset();
        OutTickAtMs = NextBurnTickAtMs(Burn);
        if (Terms.BurnTickMs <= 0 || OutTickAtMs == MAX_int64 || OutTickAtMs > AtMs) { return false; }
        for (FLDCombatBurnLayer& Layer : Burn.Layers)
        {
            if (Layer.NextTickAtMs != OutTickAtMs) { continue; }
            if (Layer.NextTickAtMs <= Layer.ExpiresAtMs) { ShareOf(Out, Layer).DamageP2 += BurnLayerTickP2(Layer, Terms); }
            Layer.NextTickAtMs = Layer.NextTickAtMs > MAX_int64 - Terms.BurnTickMs ? MAX_int64 : Layer.NextTickAtMs + Terms.BurnTickMs;
        }
        Burn.Layers.RemoveAll([](const FLDCombatBurnLayer& Layer) { return Layer.NextTickAtMs > Layer.ExpiresAtMs; });
        return true;
    }

    void BurnRemainingShares(const FLDCombatBurn& Burn, const FLDKeywordTerms& Terms, TArray<FLDBurnTickShare>& Out)
    {
        Out.Reset();
        for (const FLDCombatBurnLayer& Layer : Burn.Layers)
        {
            const int32 Ticks = BurnLayerTicksLeft(Layer, Terms);
            if (Ticks > 0) { ShareOf(Out, Layer).DamageP2 += BurnLayerTickP2(Layer, Terms) * Ticks; }
        }
        Out.RemoveAll([](const FLDBurnTickShare& Share) { return Share.DamageP2 <= 0; });
    }

    bool ExpireBurn(FLDCombatBurn& Burn, int64 AtMs)
    {
        return Burn.Layers.RemoveAll([AtMs](const FLDCombatBurnLayer& Layer)
        { return Layer.NextTickAtMs > Layer.ExpiresAtMs || Layer.ExpiresAtMs < AtMs; }) > 0;
    }

    int32 RemoveBurnSource(FLDCombatBurn& Burn, const FGuid& Source)
    {
        return Burn.Layers.RemoveAll([&Source](const FLDCombatBurnLayer& Layer) { return Layer.SourceEntity == Source; });
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

    int64 CapLifestealBp(int64 SumBp, const FLDKeywordTerms& Terms)
    {
        return FMath::Clamp<int64>(SumBp, 0, FMath::Max<int64>(Terms.LifestealCapBp, 0));
    }

    bool ExpireLedger(FLDCombatLedger& Ledger, int64 AtMs)
    {
        if (Ledger.EntriesP2.IsEmpty() || Ledger.WindowMs <= 0 || Ledger.LastEntryAtMs > MAX_int64 - Ledger.WindowMs
            || AtMs < Ledger.LastEntryAtMs + Ledger.WindowMs)
        {
            return false;
        }
        Ledger.EntriesP2.Reset();
        return true;
    }

    bool AddLedgerEntry(FLDCombatLedger& Ledger, int64 ValueP2, int64 AtMs, int32 Count, int64 WindowMs, int64& OutCashOutP2,
        bool& bOutRefused)
    {
        OutCashOutP2 = 0;
        bOutRefused = ValueP2 < 0 || Count < 1 || WindowMs < 1;
        if (bOutRefused) { return false; }
        ExpireLedger(Ledger, AtMs);
        const bool bNewBatch = Ledger.EntriesP2.IsEmpty();
        const int32 Threshold = bNewBatch ? Count : Ledger.Count;
        int64 Sum = ValueP2;
        for (const int64 Entry : Ledger.EntriesP2)
        {
            if (Entry > MAX_int64 - Sum) { bOutRefused = true; return false; }
            Sum += Entry;
        }
        if (bNewBatch)
        {
            Ledger.Count = Count;
            Ledger.WindowMs = WindowMs;
        }
        Ledger.LastEntryAtMs = AtMs;
        if (Ledger.EntriesP2.Num() + 1 >= Threshold)
        {
            Ledger.EntriesP2.Reset();
            OutCashOutP2 = Sum;
            return true;
        }
        Ledger.EntriesP2.Add(ValueP2);
        return false;
    }
}
