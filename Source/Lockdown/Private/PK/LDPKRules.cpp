#include "PK/LDPKRules.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Character/LDNativeMotionTypes.h"

// Named (not anonymous) so unity builds cannot collide with another file's helpers.
namespace LDPKRulesDetail
{
// Every approved duration is bounded by an int32 of seconds, every radius by 1e6 cm and every point count by what the
// public award view can carry, so the derived sums and products below stay inside int64.
constexpr int64 DurationBound = int64(MAX_int32) * 1000;
constexpr double RadiusBoundCm = 1000000.0;
constexpr int64 PointsBound = 64;
constexpr int64 BpWhole = 10000;
constexpr int64 MaxAwardMultiplierBp = MAX_int64 / (LDPKRules::SpawnAnchors * PointsBound);
// MaxHpP2 * bp * ms / RingDamageScale = P2 (bp = 1e-4, ms = 1e-3 s).
constexpr int64 RingDamageScale = 10000000;

bool RationalCm(const FLDDemoRational& Value, const TCHAR* Name, double& Out, FString& Error)
{
    if (Value.Denominator <= 0 || Value.Numerator < 0)
    {
        Error = FString::Printf(TEXT("The approved %s is not a nonnegative rational."), Name);
        return false;
    }
    Out = double(Value.Numerator) / double(Value.Denominator);
    return true;
}

bool PositiveRadius(double Value)
{
    return FMath::IsFinite(Value) && Value > 0 && Value <= RadiusBoundCm;
}

bool CheckedMul(int64 A, int64 B, int64& Out)
{
    check(A >= 0 && B >= 0);
    if (A != 0 && B > MAX_int64 / A) { return false; }
    Out = A * B;
    return true;
}

bool ScaleAwardPool(int64 BasePool, int64 MultiplierBp, int64& Out)
{
    int64 Scaled;
    if (BasePool < 0 || MultiplierBp < BpWhole || MultiplierBp > MaxAwardMultiplierBp
        || !CheckedMul(BasePool, MultiplierBp, Scaled)) { return false; }
    Out = Scaled / BpWhole;
    if (Scaled % BpWhole != 0) { ++Out; }
    return true;
}

// Fisher-Yates over the stream: every permutation equally likely.
void Shuffle(TArray<int32>& Values, FRandomStream& Random)
{
    for (int32 Index = Values.Num() - 1; Index > 0; --Index)
    {
        const int32 Other = Random.RandRange(0, Index);
        Values.Swap(Index, Other);
    }
}

// The ring radius at a (fractional) elapsed ms: held, then linear between the approved nodes.
double RadiusAtMs(const FLDPKParameters& Params, double ElapsedMs)
{
    if (ElapsedMs <= double(Params.ShrinkStartMs) || Params.MidMs <= Params.ShrinkStartMs) { return Params.ArenaRadiusCm; }
    if (ElapsedMs <= double(Params.MidMs))
    {
        const double Alpha = (ElapsedMs - double(Params.ShrinkStartMs)) / double(Params.MidMs - Params.ShrinkStartMs);
        return Params.ArenaRadiusCm + (Params.MidRadiusCm - Params.ArenaRadiusCm) * Alpha;
    }
    if (ElapsedMs <= double(Params.FinalMs) && Params.FinalMs > Params.MidMs)
    {
        const double Alpha = (ElapsedMs - double(Params.MidMs)) / double(Params.FinalMs - Params.MidMs);
        return Params.MidRadiusCm + (Params.FinalRadiusCm - Params.MidRadiusCm) * Alpha;
    }
    return Params.FinalRadiusCm;
}

// Motion record instants convert from authority seconds; this much of a ms is floating noise, not time.
constexpr double PathToleranceMs = 1e-3;
// A crossing at an exact whole ms (up to floating noise) stays in that ms; any later instant rounds up to the next.
constexpr double EventEpsilonMs = 1e-6;
}

bool FLDPKParameters::FromContent(const FLDDemoContentParameters& Content, FLDPKParameters& Out, FString& Error)
{
    Out = {};
    if (!LDPKRulesDetail::RationalCm(Content.PKArenaRadiusCm, TEXT("PK arena radius"), Out.ArenaRadiusCm, Error)
        || !LDPKRulesDetail::RationalCm(Content.PKRingMidRadiusCm, TEXT("PK ring mid radius"), Out.MidRadiusCm, Error)
        || !LDPKRulesDetail::RationalCm(Content.PKRingFinalRadiusCm, TEXT("PK ring final radius"), Out.FinalRadiusCm, Error)
        || !LDPKRulesDetail::RationalCm(Content.ThrowRadiusCm, TEXT("throw radius"), Out.ThrowRadiusCm, Error)
        || !LDPKRulesDetail::RationalCm(Content.ThrowRangeCm, TEXT("throw range"), Out.ThrowRangeCm, Error))
    {
        Out = {};
        return false;
    }
    Out.ShrinkStartMs = Content.PKRingShrinkStartMs;
    Out.MidMs = Content.PKRingMidMs;
    Out.FinalMs = Content.PKRingFinalMs;
    Out.PreviewMs = Content.PKRingPreviewMs;
    Out.MaxMs = Content.RestPKMaxMs;
    Out.TickMs = Content.PKRingTickMs;
    Out.EarlyDamageBp = Content.PKRingEarlyDamageBp;
    Out.MidDamageBp = Content.PKRingMidDamageBp;
    Out.FinalDamageBp = Content.PKRingFinalDamageBp;
    Out.TeamFormatBp = Content.PKTeamFormatBp;
    Out.RewardMultiplierBp = Content.PKRewardMultiplierBp;
    Out.ParticipationPoints = Content.PKParticipationPoints;
    Out.WinnerPointsMax = Content.PKWinnerPointsMax;
    Out.BettingPoolMaxPoints = Content.PKBettingPoolMaxPoints;
    Out.ThrowCooldownMs = Content.ThrowCooldownMs;
    Out.ThrowTelegraphMs = Content.ThrowTelegraphMs;
    Out.ThrowSlowBp = Content.ThrowSlowBp;
    Out.ThrowSlowMs = Content.ThrowSlowMs;
    Out.ThrowExposeMs = Content.ThrowExposeMs;
    return Out.Validate(Error);
}

bool FLDPKParameters::Validate(FString& Error) const
{
    if (!LDPKRulesDetail::PositiveRadius(ArenaRadiusCm) || !LDPKRulesDetail::PositiveRadius(MidRadiusCm) || !LDPKRulesDetail::PositiveRadius(FinalRadiusCm))
    {
        Error = TEXT("Every approved PK ring radius must be positive, finite and bounded.");
        return false;
    }
    if (!(FinalRadiusCm < MidRadiusCm && MidRadiusCm < ArenaRadiusCm))
    {
        Error = TEXT("The PK ring must shrink: final radius < mid radius < arena radius.");
        return false;
    }
    const int64 Durations[] = {MidMs, FinalMs, PreviewMs, MaxMs, TickMs, ThrowCooldownMs, ThrowTelegraphMs, ThrowSlowMs,
        ThrowExposeMs};
    for (const int64 Duration : Durations)
    {
        if (Duration <= 0 || Duration > LDPKRulesDetail::DurationBound)
        {
            Error = TEXT("Every approved PK and throw duration must be positive and bounded.");
            return false;
        }
    }
    if (ShrinkStartMs < 0 || !(ShrinkStartMs < MidMs && MidMs < FinalMs && FinalMs < MaxMs))
    {
        Error = TEXT("The PK ring nodes must be ordered: 0 <= shrink start < mid < final < PK maximum.");
        return false;
    }
    if (TickMs > MaxMs)
    {
        Error = TEXT("The PK ring damage tick must fit inside the PK.");
        return false;
    }
    const int64 Rates[] = {EarlyDamageBp, MidDamageBp, FinalDamageBp, ThrowSlowBp};
    for (const int64 Rate : Rates)
    {
        if (Rate <= 0 || Rate > LDPKRulesDetail::BpWhole)
        {
            Error = TEXT("Every approved PK ring damage rate and the throw slow must be in (0, 10000] basis points.");
            return false;
        }
    }
    if (TeamFormatBp < 0 || TeamFormatBp > LDPKRulesDetail::BpWhole)
    {
        Error = TEXT("The approved PK team format probability must be in [0, 10000] basis points.");
        return false;
    }
    if (RewardMultiplierBp < LDPKRulesDetail::BpWhole || RewardMultiplierBp > LDPKRulesDetail::MaxAwardMultiplierBp)
    {
        Error = TEXT("The approved PK reward multiplier is outside the safe range.");
        return false;
    }
    const int64 Points[] = {ParticipationPoints, WinnerPointsMax, BettingPoolMaxPoints};
    for (const int64 Value : Points)
    {
        if (Value < 0 || Value > LDPKRulesDetail::PointsBound)
        {
            Error = TEXT("Every approved PK award must be a nonnegative bounded point count.");
            return false;
        }
    }
    if (!LDPKRulesDetail::PositiveRadius(ThrowRadiusCm) || !LDPKRulesDetail::PositiveRadius(ThrowRangeCm))
    {
        Error = TEXT("The approved throw radius and range must be positive, finite and bounded.");
        return false;
    }
    Error.Reset();
    return true;
}

bool LDPKRules::DrawFormat(int32 Entrants, int64 TeamFormatBp, FRandomStream& Random, ELDPKFormat& Out)
{
    Out = ELDPKFormat::None;
    if (Entrants < 0 || Entrants > SpawnAnchors || TeamFormatBp < 0 || TeamFormatBp > LDPKRulesDetail::BpWhole) { return false; }
    switch (Entrants)
    {
    case 0:
    case 1: Out = ELDPKFormat::None; break;
    case 2: Out = ELDPKFormat::Duel; break;
    case 3:
    case 5: Out = ELDPKFormat::FreeForAll; break;
    default:
        Out = int64(Random.RandRange(0, int32(LDPKRulesDetail::BpWhole) - 1)) < TeamFormatBp ? ELDPKFormat::Teams : ELDPKFormat::FreeForAll;
        break;
    }
    return true;
}

void LDPKRules::EqualPartitions(int32 N, TArray<TArray<uint8>>& Out)
{
    Out.Reset();
    if (N < 2 || N > SpawnAnchors || N % 2 != 0) { return; }
    // Side 1 is the bit set; seat 0 always on side 1 makes each unlabeled partition appear once.
    for (uint32 Mask = 1; Mask < (1u << N); Mask += 2)
    {
        if (int32(FMath::CountBits(Mask)) != N / 2) { continue; }
        TArray<uint8>& Sides = Out.AddDefaulted_GetRef();
        Sides.SetNumUninitialized(N);
        for (int32 Seat = 0; Seat < N; ++Seat) { Sides[Seat] = (Mask & (1u << Seat)) ? 1 : 2; }
    }
}

bool LDPKRules::Draw(int32 Entrants, const FLDPKParameters& Params, FRandomStream& Random, FLDPKDraw& Out, FString& Error)
{
    Out = {};
    if (Entrants < 2 || Entrants > SpawnAnchors)
    {
        Error = FString::Printf(TEXT("A PK draw needs 2..%d entrants, not %d."), SpawnAnchors, Entrants);
        return false;
    }
    ELDPKFormat Format = ELDPKFormat::None;
    if (!DrawFormat(Entrants, Params.TeamFormatBp, Random, Format) || Format == ELDPKFormat::None)
    {
        Error = TEXT("The PK format could not be drawn from the approved parameters.");
        return false;
    }
    Out.Format = Format;
    if (Format == ELDPKFormat::Teams)
    {
        TArray<TArray<uint8>> Partitions;
        EqualPartitions(Entrants, Partitions);
        if (Partitions.IsEmpty())
        {
            Error = TEXT("Teams need an even entrant count.");
            Out = {};
            return false;
        }
        Out.SideCount = 2;
        Out.SideOfSeat = Partitions[Random.RandRange(0, Partitions.Num() - 1)];
    }
    else
    {
        Out.SideCount = uint8(Entrants);
        Out.SideOfSeat.SetNumUninitialized(Entrants);
        for (int32 Seat = 0; Seat < Entrants; ++Seat) { Out.SideOfSeat[Seat] = uint8(Seat + 1); }
    }

    const int32 Rotation = Random.RandRange(0, SpawnAnchors - 1);
    Out.AnchorOfSeat.Init(INDEX_NONE, Entrants);
    if (Format == ELDPKFormat::Teams)
    {
        // Side 1 takes the half starting at the rotation, side 2 the opposite half.
        const int32 PerSide = Entrants / 2;
        for (uint8 Side = 1; Side <= 2; ++Side)
        {
            TArray<int32> Slots;
            for (int32 Index = 0; Index < PerSide; ++Index)
            {
                Slots.Add((Rotation + (Side == 1 ? 0 : SpawnAnchors / 2) + Index) % SpawnAnchors);
            }
            LDPKRulesDetail::Shuffle(Slots, Random);
            int32 Next = 0;
            for (int32 Seat = 0; Seat < Entrants; ++Seat)
            {
                if (Out.SideOfSeat[Seat] == Side) { Out.AnchorOfSeat[Seat] = Slots[Next++]; }
            }
        }
    }
    else
    {
        // Ring offsets that keep every pair at least one anchor step apart and spread the entrants evenly.
        TArray<int32> Offsets;
        switch (Entrants)
        {
        case 2: Offsets = {0, 3}; break;
        case 3: Offsets = {0, 2, 4}; break;
        case 4: Offsets = {0, 1, 3, 4}; break;
        case 5: Offsets = {0, 1, 2, 3, 4}; break;
        default: Offsets = {0, 1, 2, 3, 4, 5}; break;
        }
        for (int32& Offset : Offsets) { Offset = (Rotation + Offset) % SpawnAnchors; }
        LDPKRulesDetail::Shuffle(Offsets, Random);
        for (int32 Seat = 0; Seat < Entrants; ++Seat) { Out.AnchorOfSeat[Seat] = Offsets[Seat]; }
    }
    Error.Reset();
    return true;
}

double LDPKRules::RingRadiusCm(const FLDPKParameters& Params, int64 ElapsedMs)
{
    return LDPKRulesDetail::RadiusAtMs(Params, double(ElapsedMs));
}

bool LDPKRules::RingPreview(const FLDPKParameters& Params, int64 ElapsedMs, double& OutRadiusCm, int64& OutAtMs)
{
    OutRadiusCm = 0;
    OutAtMs = 0;
    if (ElapsedMs >= Params.ShrinkStartMs - Params.PreviewMs && ElapsedMs < Params.MidMs)
    {
        OutRadiusCm = Params.MidRadiusCm;
        OutAtMs = Params.MidMs;
        return true;
    }
    if (ElapsedMs >= Params.MidMs && ElapsedMs < Params.FinalMs)
    {
        OutRadiusCm = Params.FinalRadiusCm;
        OutAtMs = Params.FinalMs;
        return true;
    }
    return false;
}

bool LDPKRules::IsInsideRing(const FLDPKParameters& Params, const FVector& CentreCm, const FVector& FootCm, int64 ElapsedMs)
{
    const double Radius = RingRadiusCm(Params, ElapsedMs);
    const double Dx = double(FootCm.X) - double(CentreCm.X);
    const double Dy = double(FootCm.Y) - double(CentreCm.Y);
    return Dx * Dx + Dy * Dy <= Radius * Radius;
}

int64 LDPKRules::RingDamageBp(const FLDPKParameters& Params, int64 ElapsedMs)
{
    if (ElapsedMs < 0) { return 0; }
    if (ElapsedMs < Params.MidMs) { return Params.EarlyDamageBp; }
    if (ElapsedMs < Params.FinalMs) { return Params.MidDamageBp; }
    return Params.FinalDamageBp;
}

int64 LDPKRules::NextRingTickMs(const FLDPKParameters& Params, int64 ElapsedMs)
{
    if (Params.TickMs <= 0) { return MAX_int64; }
    if (ElapsedMs < 0) { return 0; }
    const int64 Ticks = ElapsedMs / Params.TickMs + 1;
    int64 Result = 0;
    return LDPKRulesDetail::CheckedMul(Ticks, Params.TickMs, Result) ? Result : MAX_int64;
}

bool LDPKRules::AccumulateRingDamage(const FLDPKParameters& Params, int64 MaxHpP2, int64 FromMs, int64 ToMs, int64& InOutAccumulatorE7)
{
    if (MaxHpP2 < 0 || FromMs < 0 || ToMs < FromMs || InOutAccumulatorE7 < 0) { return false; }
    int64 Sum = InOutAccumulatorE7;
    int64 Cursor = FromMs;
    while (Cursor < ToMs)
    {
        const int64 Rate = RingDamageBp(Params, Cursor);
        if (Rate < 0) { return false; }
        int64 SegmentEnd = ToMs;
        if (Cursor < Params.MidMs) { SegmentEnd = FMath::Min(ToMs, Params.MidMs); }
        else if (Cursor < Params.FinalMs) { SegmentEnd = FMath::Min(ToMs, Params.FinalMs); }
        int64 PerMs = 0;
        int64 Part = 0;
        if (!LDPKRulesDetail::CheckedMul(MaxHpP2, Rate, PerMs) || !LDPKRulesDetail::CheckedMul(PerMs, SegmentEnd - Cursor, Part) || Sum > MAX_int64 - Part)
        {
            return false;
        }
        Sum += Part;
        Cursor = SegmentEnd;
    }
    InOutAccumulatorE7 = Sum;
    return true;
}

int64 LDPKRules::TakeRingDamageP2(int64& InOutAccumulatorE7)
{
    if (InOutAccumulatorE7 <= 0) { return 0; }
    const int64 Whole = InOutAccumulatorE7 / LDPKRulesDetail::RingDamageScale;
    InOutAccumulatorE7 -= Whole * LDPKRulesDetail::RingDamageScale;
    return Whole;
}

bool LDPKRules::FootPathFromMotion(const FLDNativeMotionBatch& Batch, int64 FightStartedAtMs, int64 FromMs, int64 ToMs,
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
        const double Start = Record.StartServerSeconds * 1000. - double(FightStartedAtMs);
        const double End = Record.EndServerSeconds * 1000. - double(FightStartedAtMs);
        if (!FMath::IsFinite(Start) || !FMath::IsFinite(End) || End < Start || Start + LDPKRulesDetail::PathToleranceMs < Cursor
            || End > To + LDPKRulesDetail::PathToleranceMs || Record.ToFootCm.ContainsNaN()
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

bool LDPKRules::OutsideRingIntervals(const FLDPKParameters& Params, const FVector& CentreCm, const TArray<FFootSegment>& Path,
    TArray<TPair<int64, int64>>& Out)
{
    Out.Reset();
    const auto Push = [&Out](double From, double To)
    {
        const int64 A = int64(FMath::CeilToDouble(From - LDPKRulesDetail::EventEpsilonMs));
        const int64 B = int64(FMath::CeilToDouble(To - LDPKRulesDetail::EventEpsilonMs));
        if (B <= A) { return; }
        if (!Out.IsEmpty() && Out.Last().Value >= A)
        {
            Out.Last().Value = FMath::Max(Out.Last().Value, B);
            return;
        }
        Out.Emplace(A, B);
    };
    const double Nodes[] = {double(Params.ShrinkStartMs), double(Params.MidMs), double(Params.FinalMs)};
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
        TArray<double, TInlineAllocator<5>> Cuts;
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
            const double R0 = LDPKRulesDetail::RadiusAtMs(Params, A);
            const double K = (LDPKRulesDetail::RadiusAtMs(Params, B) - R0) / Span;
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

ELDPKOutcome LDPKRules::Judge(const TArray<uint8>& SideOfSeat, const TArray<bool>& AliveOfSeat, bool bDeadline, uint8& OutWinnerSide)
{
    OutWinnerSide = 0;
    if (SideOfSeat.Num() != AliveOfSeat.Num()) { return ELDPKOutcome::None; }
    uint32 AliveSides = 0;
    for (int32 Seat = 0; Seat < SideOfSeat.Num(); ++Seat)
    {
        const uint8 Side = SideOfSeat[Seat];
        if (Side == 0 || Side > SpawnAnchors) { return ELDPKOutcome::None; }
        if (AliveOfSeat[Seat]) { AliveSides |= 1u << Side; }
    }
    const int32 Count = int32(FMath::CountBits(AliveSides));
    if (Count == 0) { return ELDPKOutcome::Draw; }
    if (Count == 1)
    {
        OutWinnerSide = uint8(FMath::CountTrailingZeros(AliveSides));
        return ELDPKOutcome::Won;
    }
    return bDeadline ? ELDPKOutcome::Draw : ELDPKOutcome::Running;
}

void LDPKRules::SplitWithRotation(int64 Points, int32 Recipients, uint32 Serial, TArray<int64>& Out)
{
    Out.Reset();
    if (Recipients <= 0) { return; }
    Out.Init(0, Recipients);
    if (Points <= 0) { return; }
    const int64 Each = Points / Recipients;
    const int64 Remainder = Points % Recipients;
    const int32 Start = Serial == 0 ? 0 : int32((Serial - 1) % uint32(Recipients));
    for (int32 Index = 0; Index < Recipients; ++Index) { Out[Index] = Each; }
    for (int64 Extra = 0; Extra < Remainder; ++Extra) { ++Out[int32((Start + Extra) % Recipients)]; }
}

bool LDPKRules::ComputeAwards(const FLDPKParameters& Params, ELDPKOutcome Outcome, uint8 WinnerSide, uint32 Serial,
    const TArray<uint8>& SideOfSeat, const TArray<bool>& EligibleOfSeat, const TArray<uint8>& BetSideOfBettor,
    const TArray<bool>& BettorPresent, TArray<FLDPKAward>& Out, FString& Error)
{
    Out.Reset();
    if (Outcome != ELDPKOutcome::Won && Outcome != ELDPKOutcome::Draw && Outcome != ELDPKOutcome::NoContest)
    {
        Error = TEXT("PK awards are only computed for a decided PK.");
        return false;
    }
    if (SideOfSeat.Num() != EligibleOfSeat.Num() || BetSideOfBettor.Num() != BettorPresent.Num())
    {
        Error = TEXT("PK award inputs must be parallel arrays.");
        return false;
    }
    if (SideOfSeat.Num() < 2 || SideOfSeat.Num() > SpawnAnchors)
    {
        Error = TEXT("A PK has 2..6 locked entrants.");
        return false;
    }
    const int64 Points[] = {Params.ParticipationPoints, Params.WinnerPointsMax, Params.BettingPoolMaxPoints};
    for (const int64 Value : Points)
    {
        if (Value < 0 || Value > LDPKRulesDetail::PointsBound)
        {
            Error = TEXT("PK award parameters must be nonnegative bounded point counts.");
            return false;
        }
    }
    if (Params.RewardMultiplierBp < LDPKRulesDetail::BpWhole
        || Params.RewardMultiplierBp > LDPKRulesDetail::MaxAwardMultiplierBp)
    {
        Error = TEXT("PK reward multiplier is outside the safe range.");
        return false;
    }
    uint8 MaxSide = 0;
    for (const uint8 Side : SideOfSeat)
    {
        if (Side == 0 || Side > SpawnAnchors)
        {
            Error = TEXT("Every PK seat has a side in 1..6.");
            return false;
        }
        MaxSide = FMath::Max(MaxSide, Side);
    }
    for (const uint8 Side : BetSideOfBettor)
    {
        if (Side > MaxSide)
        {
            Error = TEXT("A PK bet names a side that does not exist.");
            return false;
        }
    }
    if (Outcome == ELDPKOutcome::Won && (WinnerSide == 0 || !SideOfSeat.Contains(WinnerSide)))
    {
        Error = TEXT("A won PK needs an existing winner side.");
        return false;
    }
    if (Outcome == ELDPKOutcome::NoContest)
    {
        Error.Reset();
        return true;
    }

    const int32 Entrants = SideOfSeat.Num();
    TArray<FLDPKAward> Seats;
    Seats.SetNum(Entrants);
    TArray<int32> EligibleParticipants;
    for (int32 Seat = 0; Seat < Entrants; ++Seat)
    {
        Seats[Seat].Seat = Seat;
        if (EligibleOfSeat[Seat]) { EligibleParticipants.Add(Seat); }
    }
    int64 ParticipationBasePool = 0;
    int64 ParticipationPool = 0;
    if (!LDPKRulesDetail::CheckedMul(EligibleParticipants.Num(), Params.ParticipationPoints, ParticipationBasePool)
        || !LDPKRulesDetail::ScaleAwardPool(ParticipationBasePool, Params.RewardMultiplierBp, ParticipationPool))
    {
        Error = TEXT("PK participation award pool is not representable.");
        return false;
    }
    TArray<int64> Split;
    SplitWithRotation(ParticipationPool, EligibleParticipants.Num(), Serial, Split);
    for (int32 Index = 0; Index < EligibleParticipants.Num(); ++Index)
    {
        Seats[EligibleParticipants[Index]].Participation = Split[Index];
    }
    TArray<FLDPKAward> Bettors;
    Bettors.SetNum(BetSideOfBettor.Num());
    for (int32 Bettor = 0; Bettor < Bettors.Num(); ++Bettor) { Bettors[Bettor].Bettor = Bettor; }

    if (Outcome == ELDPKOutcome::Won)
    {
        // Eliminated but present teammates share the win; a forfeited or departed one does not.
        TArray<int32> Winners;
        for (int32 Seat = 0; Seat < Entrants; ++Seat)
        {
            if (SideOfSeat[Seat] == WinnerSide && EligibleOfSeat[Seat]) { Winners.Add(Seat); }
        }
        const int64 WinnerBasePool = FMath::Min<int64>((Entrants + 1) / 2, Params.WinnerPointsMax);
        int64 WinnerPool = 0;
        if (!LDPKRulesDetail::ScaleAwardPool(WinnerBasePool, Params.RewardMultiplierBp, WinnerPool))
        {
            Error = TEXT("PK winner award pool is not representable.");
            return false;
        }
        SplitWithRotation(WinnerPool, Winners.Num(), Serial, Split);
        for (int32 Index = 0; Index < Winners.Num(); ++Index) { Seats[Winners[Index]].Winner = Split[Index]; }

        TArray<int32> Correct;
        for (int32 Bettor = 0; Bettor < BetSideOfBettor.Num(); ++Bettor)
        {
            if (BetSideOfBettor[Bettor] == WinnerSide && BettorPresent[Bettor]) { Correct.Add(Bettor); }
        }
        const int64 BettingBasePool = FMath::Min<int64>(BetSideOfBettor.Num(), Params.BettingPoolMaxPoints);
        int64 BettingPool = 0;
        if (!LDPKRulesDetail::ScaleAwardPool(BettingBasePool, Params.RewardMultiplierBp, BettingPool))
        {
            Error = TEXT("PK betting award pool is not representable.");
            return false;
        }
        SplitWithRotation(BettingPool, Correct.Num(), Serial, Split);
        for (int32 Index = 0; Index < Correct.Num(); ++Index) { Bettors[Correct[Index]].Bet = Split[Index]; }
    }

    // Validate the entire settlement before exposing any row to the uint8 public/payment projection.
    for (const TArray<FLDPKAward>* Group : {&Seats, &Bettors})
    {
        for (const FLDPKAward& Award : *Group)
        {
            if (Award.Participation > MAX_uint8 || Award.Winner > MAX_uint8 || Award.Bet > MAX_uint8)
            {
                Error = TEXT("A PK award exceeds the public payment representation.");
                return false;
            }
        }
    }
    for (const FLDPKAward& Award : Seats)
    {
        if (Award.Participation + Award.Winner + Award.Bet > 0) { Out.Add(Award); }
    }
    for (const FLDPKAward& Award : Bettors)
    {
        if (Award.Participation + Award.Winner + Award.Bet > 0) { Out.Add(Award); }
    }
    Error.Reset();
    return true;
}
