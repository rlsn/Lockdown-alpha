#include "Framework/LDShowdownRing.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Character/LDNativeMotionTypes.h"

// Named (not anonymous) so unity builds cannot collide with another file's helpers.
namespace LDShowdownRingDetail
{
constexpr int64 DurationBound = int64(MAX_int32) * 1000;
constexpr double RadiusBoundCm = 1000000.0;
constexpr int64 BpWhole = 10000;
// MaxHpP2 * bp * ms / DamageScale = P2 (bp = 1e-4, ms = 1e-3 s).
constexpr int64 DamageScale = 10000000;
// Motion record instants convert from authority seconds; this much of a ms is floating noise, not time.
constexpr double PathToleranceMs = 1e-3;
// A crossing at an exact whole ms (up to floating noise) stays in that ms; any later instant rounds up to the next.
constexpr double EventEpsilonMs = 1e-6;

bool CheckedMul(int64 A, int64 B, int64& Out)
{
    check(A >= 0 && B >= 0);
    if (A != 0 && B > MAX_int64 / A) { return false; }
    Out = A * B;
    return true;
}

double RadiusAt(const FLDShowdownRing& Ring, double ElapsedMs)
{
    if (ElapsedMs <= 0 || Ring.DurationMs <= 0) { return Ring.InitialRadiusCm; }
    if (ElapsedMs >= double(Ring.DurationMs)) { return Ring.FinalRadiusCm; }
    return Ring.InitialRadiusCm + (Ring.FinalRadiusCm - Ring.InitialRadiusCm) * (ElapsedMs / double(Ring.DurationMs));
}

double Approx(const FLDDemoRational& Value) { return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : 0.; }
}

bool FLDShowdownRing::FromContent(const FLDDemoContentParameters& Content, int64 InDurationMs, FLDShowdownRing& Out, FString& Error)
{
    Out = {};
    Out.InitialRadiusCm = LDShowdownRingDetail::Approx(Content.ShowdownRingRadiusCm);
    Out.FinalRadiusCm = LDShowdownRingDetail::Approx(Content.ShowdownRingFinalRadiusCm);
    Out.DurationMs = InDurationMs;
    Out.TickMs = Content.ShowdownRingTickMs;
    Out.PreviewMs = Content.ShowdownRingPreviewMs;
    Out.EarlyDamageBp = Content.ShowdownRingEarlyDamageBp;
    Out.LateDamageBp = Content.ShowdownRingLateDamageBp;
    return Out.Validate(Error);
}

bool FLDShowdownRing::Validate(FString& Error) const
{
    using namespace LDShowdownRingDetail;
    if (!FMath::IsFinite(InitialRadiusCm) || !FMath::IsFinite(FinalRadiusCm) || !(0 < FinalRadiusCm && FinalRadiusCm < InitialRadiusCm)
        || InitialRadiusCm > RadiusBoundCm)
    {
        Error = TEXT("The showdown ring must shrink between positive, bounded radii.");
        return false;
    }
    for (const int64 Duration : {DurationMs, TickMs, PreviewMs})
    {
        if (Duration <= 0 || Duration > DurationBound)
        {
            Error = TEXT("Every showdown ring duration must be positive and bounded.");
            return false;
        }
    }
    if (EarlyDamageBp <= 0 || EarlyDamageBp > BpWhole || LateDamageBp <= 0 || LateDamageBp > BpWhole)
    {
        Error = TEXT("Every showdown ring damage rate must be in (0, 10000] basis points.");
        return false;
    }
    Error.Reset();
    return true;
}

double LDShowdownRing::RadiusCm(const FLDShowdownRing& Ring, int64 ElapsedMs)
{
    return LDShowdownRingDetail::RadiusAt(Ring, double(ElapsedMs));
}

int64 LDShowdownRing::DamageBp(const FLDShowdownRing& Ring, int64 ElapsedMs)
{
    if (ElapsedMs < 0) { return 0; }
    return ElapsedMs < Ring.MidMs() ? Ring.EarlyDamageBp : Ring.LateDamageBp;
}

int64 LDShowdownRing::NextTickMs(const FLDShowdownRing& Ring, int64 ElapsedMs)
{
    if (Ring.TickMs <= 0) { return MAX_int64; }
    if (ElapsedMs < 0) { return 0; }
    int64 Result = 0;
    return LDShowdownRingDetail::CheckedMul(ElapsedMs / Ring.TickMs + 1, Ring.TickMs, Result) ? Result : MAX_int64;
}

bool LDShowdownRing::AccumulateDamage(const FLDShowdownRing& Ring, int64 MaxHpP2, int64 FromMs, int64 ToMs, int64& InOutAccumulatorE7)
{
    if (MaxHpP2 < 0 || FromMs < 0 || ToMs < FromMs || InOutAccumulatorE7 < 0) { return false; }
    int64 Sum = InOutAccumulatorE7;
    int64 Cursor = FromMs;
    while (Cursor < ToMs)
    {
        const int64 Rate = DamageBp(Ring, Cursor);
        const int64 SegmentEnd = Cursor < Ring.MidMs() ? FMath::Min(ToMs, Ring.MidMs()) : ToMs;
        int64 PerMs = 0, Part = 0;
        if (!LDShowdownRingDetail::CheckedMul(MaxHpP2, Rate, PerMs) || !LDShowdownRingDetail::CheckedMul(PerMs, SegmentEnd - Cursor, Part)
            || Sum > MAX_int64 - Part)
        {
            return false;
        }
        Sum += Part;
        Cursor = SegmentEnd;
    }
    InOutAccumulatorE7 = Sum;
    return true;
}

int64 LDShowdownRing::TakeDamageP2(int64& InOutAccumulatorE7)
{
    if (InOutAccumulatorE7 <= 0) { return 0; }
    const int64 Whole = InOutAccumulatorE7 / LDShowdownRingDetail::DamageScale;
    InOutAccumulatorE7 -= Whole * LDShowdownRingDetail::DamageScale;
    return Whole;
}

bool LDShowdownRing::FootPathFromMotion(const FLDNativeMotionBatch& Batch, int64 ClosureStartedAtMs, int64 FromMs, int64 ToMs,
    TArray<FFootSegment>& Out)
{
    Out.Reset();
    if (FromMs < 0 || ToMs < FromMs || Batch.StartFootCm.ContainsNaN() || Batch.EndFootCm.ContainsNaN()) { return false; }
    const double From = double(FromMs), To = double(ToMs);
    double Cursor = From;
    FVector Foot = Batch.StartFootCm;
    const auto Stand = [&Out, &Cursor, &Foot](double Until)
    {
        if (Until > Cursor) { Out.Add({Cursor, Until, Foot, Foot}); }
        Cursor = FMath::Max(Cursor, Until);
    };
    for (const FLDNativeMotionRecord& Record : Batch.Records)
    {
        const double Start = Record.StartServerSeconds * 1000. - double(ClosureStartedAtMs);
        const double End = Record.EndServerSeconds * 1000. - double(ClosureStartedAtMs);
        if (!FMath::IsFinite(Start) || !FMath::IsFinite(End) || End < Start || Start + LDShowdownRingDetail::PathToleranceMs < Cursor
            || End > To + LDShowdownRingDetail::PathToleranceMs || Record.ToFootCm.ContainsNaN()
            || !Record.FromFootCm.Equals(Foot, .001))
        {
            Out.Reset();
            return false;
        }
        const double S = FMath::Clamp(Start, Cursor, To);
        const double E = FMath::Clamp(End, S, To);
        Stand(S);
        if (E > S)
        {
            // Noise-clamped ends keep the record's own straight line.
            const double Span = End - Start;
            const FVector AtS = Span > 0 ? FMath::Lerp(Record.FromFootCm, Record.ToFootCm, (S - Start) / Span) : Record.FromFootCm;
            const FVector AtE = Span > 0 ? FMath::Lerp(Record.FromFootCm, Record.ToFootCm, (E - Start) / Span) : Record.ToFootCm;
            Out.Add({S, E, AtS, AtE});
            Cursor = E;
        }
        // A record without duration (a support change or an instant correction) moves the foot at once.
        Foot = Record.ToFootCm;
    }
    if (!Foot.Equals(Batch.EndFootCm, .001))
    {
        Out.Reset();
        return false;
    }
    Stand(To);
    return true;
}

bool LDShowdownRing::OutsideIntervals(const FLDShowdownRing& Ring, const FVector& CentreCm, const TArray<FFootSegment>& Path,
    TArray<TPair<int64, int64>>& Out)
{
    Out.Reset();
    const auto Push = [&Out](double From, double To)
    {
        const int64 A = int64(FMath::CeilToDouble(From - LDShowdownRingDetail::EventEpsilonMs));
        const int64 B = int64(FMath::CeilToDouble(To - LDShowdownRingDetail::EventEpsilonMs));
        if (B <= A) { return; }
        if (!Out.IsEmpty() && Out.Last().Value >= A)
        {
            Out.Last().Value = FMath::Max(Out.Last().Value, B);
            return;
        }
        Out.Emplace(A, B);
    };
    const double Nodes[] = {0., double(Ring.DurationMs)};
    for (const FFootSegment& Segment : Path)
    {
        if (!FMath::IsFinite(Segment.FromMs) || !FMath::IsFinite(Segment.ToMs) || Segment.FromMs < 0 || Segment.ToMs < Segment.FromMs
            || Segment.FromCm.ContainsNaN() || Segment.ToCm.ContainsNaN())
        {
            Out.Reset();
            return false;
        }
        if (Segment.ToMs == Segment.FromMs) { continue; }
        // Between the ring's nodes both the foot and the radius are linear in time, so the crossings are the roots of one
        // quadratic: |P(s) - C|^2 - R(s)^2 = 0.
        TArray<double, TInlineAllocator<4>> Cuts;
        Cuts.Add(Segment.FromMs);
        for (const double Node : Nodes)
        {
            if (Node > Segment.FromMs && Node < Segment.ToMs) { Cuts.Add(Node); }
        }
        Cuts.Add(Segment.ToMs);
        const double Length = Segment.ToMs - Segment.FromMs;
        const auto FootAt = [&Segment, &CentreCm, Length](double T)
        {
            const double Alpha = (T - Segment.FromMs) / Length;
            return FVector2D(FMath::Lerp(double(Segment.FromCm.X), double(Segment.ToCm.X), Alpha) - double(CentreCm.X),
                FMath::Lerp(double(Segment.FromCm.Y), double(Segment.ToCm.Y), Alpha) - double(CentreCm.Y));
        };
        for (int32 Index = 0; Index + 1 < Cuts.Num(); ++Index)
        {
            const double A = Cuts[Index], B = Cuts[Index + 1], Span = B - A;
            if (Span <= 0) { continue; }
            const FVector2D P0 = FootAt(A), V = (FootAt(B) - P0) / Span;
            const double R0 = LDShowdownRingDetail::RadiusAt(Ring, A);
            const double K = (LDShowdownRingDetail::RadiusAt(Ring, B) - R0) / Span;
            const double QA = FVector2D::DotProduct(V, V) - K * K;
            const double QB = 2. * (FVector2D::DotProduct(P0, V) - R0 * K);
            const double QC = FVector2D::DotProduct(P0, P0) - R0 * R0;
            const auto Outside = [QA, QB, QC](double S) { return (QA * S + QB) * S + QC > 0; };
            TArray<double, TInlineAllocator<4>> Splits;
            Splits.Add(0);
            const double Scale = FMath::Max3(FMath::Abs(QA) * Span * Span, FMath::Abs(QB) * Span, FMath::Abs(QC));
            if (FMath::Abs(QA) * Span * Span <= 1e-12 * FMath::Max(Scale, 1.))
            {
                if (QB != 0) { Splits.Add(-QC / QB); }
            }
            else
            {
                const double Disc = QB * QB - 4. * QA * QC;
                if (Disc >= 0)
                {
                    const double Root = FMath::Sqrt(Disc);
                    const double Q = -.5 * (QB + (QB >= 0 ? Root : -Root));
                    Splits.Add(Q / QA);
                    if (Q != 0) { Splits.Add(QC / Q); }
                }
            }
            Splits.Add(Span);
            Splits.RemoveAll([Span](double S) { return !FMath::IsFinite(S) || S < 0 || S > Span; });
            Splits.Sort();
            for (int32 Part = 0; Part + 1 < Splits.Num(); ++Part)
            {
                const double Lo = Splits[Part], Hi = Splits[Part + 1];
                if (Hi > Lo && Outside(.5 * (Lo + Hi))) { Push(A + Lo, A + Hi); }
            }
        }
    }
    return true;
}
