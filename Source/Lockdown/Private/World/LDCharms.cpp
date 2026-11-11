#include "World/LDCharms.h"
#include "Development/LDDemoContentCatalog.h"

namespace
{
    bool CharmMul(int64 A, int64 B, int64& Out)
    {
        if (A < 0 || B < 0) { return false; }
        if (A == 0 || B == 0) { Out = 0; return true; }
        if (A > MAX_int64 / B) { return false; }
        Out = A * B;
        return true;
    }

    bool CharmAdd(int64 A, int64 B, int64& Out)
    {
        if ((B > 0 && A > MAX_int64 - B) || (B < 0 && A < MIN_int64 - B)) { return false; }
        Out = A + B;
        return true;
    }

    uint64 ContenderKey(uint64 Seed, const FGuid& Id)
    {
        return LDAirdrop::Mix(Seed ^ LDAirdrop::Mix((uint64(Id.A) << 32 | Id.B) ^ LDAirdrop::Mix(uint64(Id.C) << 32 | Id.D)));
    }
}

FName LDCharms::MoveSpeedSource()
{
    static const FName Source(TEXT("charm_move_speed"));
    return Source;
}

ELDCharmKind LDCharms::KindAt(int32 Index)
{
    switch (Index)
    {
    case 0: return ELDCharmKind::Heal;
    case 1: return ELDCharmKind::Mana;
    case 2: return ELDCharmKind::MoveSpeed;
    default: return ELDCharmKind::None;
    }
}

const TCHAR* LDCharms::KindName(ELDCharmKind Kind)
{
    switch (Kind)
    {
    case ELDCharmKind::Heal: return TEXT("heal");
    case ELDCharmKind::Mana: return TEXT("mana");
    case ELDCharmKind::MoveSpeed: return TEXT("move_speed");
    default: return TEXT("none");
    }
}

void LDCharms::DrawDistinct(uint64 Seed, int32 N, int32 Count, TArray<int32>& Out)
{
    Out.Reset();
    if (N <= 0 || Count <= 0) { return; }
    TArray<int32> Pool;
    Pool.Reserve(N);
    for (int32 Index = 0; Index < N; ++Index) { Pool.Add(Index); }
    const int32 Take = FMath::Min(N, Count);
    for (int32 Step = 0; Step < Take; ++Step)
    {
        const uint64 Roll = LDAirdrop::Mix(Seed ^ LDAirdrop::Mix(uint64(Step) + 1));
        const int32 Pick = Step + int32(Roll % uint64(N - Step));
        Pool.Swap(Step, Pick);
        Out.Add(Pool[Step]);
    }
}

bool LDCharms::PickClaimer(const FVector& CentreCm, double RadiusCm, TConstArrayView<LDAirdrop::FCandidate> Candidates,
    uint64 TieSeed, FGuid& Out)
{
    Out.Invalidate();
    uint64 BestKey = 0;
    const double Limit = RadiusCm * RadiusCm;
    for (const LDAirdrop::FCandidate& Candidate : Candidates)
    {
        if (!Candidate.Contender.IsValid() || Candidate.bProtected || Candidate.FootCm.ContainsNaN()) { continue; }
        const double Distance = FVector::DistSquared2D(CentreCm, Candidate.FootCm);
        if (!FMath::IsFinite(Distance) || Distance > Limit) { continue; }
        const uint64 Key = ContenderKey(TieSeed, Candidate.Contender);
        if (!Out.IsValid() || Key < BestKey)
        {
            Out = Candidate.Contender;
            BestKey = Key;
        }
    }
    return Out.IsValid();
}

int64 LDCharms::ShareP2(int64 MaxP2, int64 Bp)
{
    if (MaxP2 <= 0 || Bp <= 0) { return 0; }
    return MaxP2 / 10000 * Bp + MaxP2 % 10000 * Bp / 10000;
}

bool FLDCharmSchedule::IsValid() const
{
    if (FirstWaveMs <= 0 || WaveIntervalMs <= 0 || WaveCount <= 0 || PerWave <= 0 || PerWave > LDCharms::KindCount
        || LifetimeMs <= 0 || LifetimeMs > WaveIntervalMs || !FMath::IsFinite(PickupRadiusCm) || PickupRadiusCm <= 0
        || Seed == 0 || Points.Num() <= PerWave)
    {
        return false;
    }
    int64 Last = 0;
    if (!WaveAtMs(WaveCount - 1, Last)) { return false; }
    for (const LDAirdrop::FPoint& Point : Points)
    {
        if (Point.Id.IsNone() || Point.PositionCm.ContainsNaN()) { return false; }
    }
    return true;
}

bool FLDCharmSchedule::WaveAtMs(int64 Index, int64& Out) const
{
    int64 Offset = 0;
    return Index >= 0 && Index < WaveCount && CharmMul(Index, WaveIntervalMs, Offset) && CharmAdd(FirstWaveMs, Offset, Out);
}

void FLDCharmState::Advance(int64 FieldMs, int32 BlockedPoint, FLDCharmAdvance& Out)
{
    Out = {};
    if (!Schedule.IsValid()) { return; }
    for (int32 Index = 0; Index < Active.Num();)
    {
        if (Active[Index].ExpireFieldMs <= FieldMs)
        {
            Out.Expired.Add(Active[Index]);
            Active.RemoveAt(Index);
            continue;
        }
        ++Index;
    }
    int64 WaveMs = 0;
    while (WavesDone < Schedule.WaveCount && Schedule.WaveAtMs(WavesDone, WaveMs) && WaveMs <= FieldMs)
    {
        const int64 Wave = WavesDone++;
        int64 ExpireMs = 0;
        if (!CharmAdd(WaveMs, Schedule.LifetimeMs, ExpireMs) || ExpireMs <= FieldMs)
        {
            Out.Skipped.Add({Wave, int32(Schedule.PerWave), TEXT("Overdue")});
            continue;
        }
        TArray<int32> Free;
        for (int32 Point = 0; Point < Schedule.Points.Num(); ++Point)
        {
            if (Point == BlockedPoint) { continue; }
            if (Active.ContainsByPredicate([Point](const FLDCharmInstance& Charm) { return Charm.PointIndex == Point; })) { continue; }
            Free.Add(Point);
        }
        const uint64 WaveSeed = LDAirdrop::Mix(Schedule.Seed ^ LDAirdrop::Mix(uint64(Wave)));
        TArray<int32> Kinds, Points;
        LDCharms::DrawDistinct(LDAirdrop::Mix(WaveSeed ^ 1), LDCharms::KindCount, int32(Schedule.PerWave), Kinds);
        LDCharms::DrawDistinct(LDAirdrop::Mix(WaveSeed ^ 2), Free.Num(), int32(Schedule.PerWave), Points);
        for (int32 Slot = 0; Slot < Points.Num(); ++Slot)
        {
            FLDCharmInstance& Charm = Active.AddDefaulted_GetRef();
            Charm.Serial = ++LastSerial;
            Charm.Kind = LDCharms::KindAt(Kinds[Slot]);
            Charm.PointIndex = Free[Points[Slot]];
            Charm.Wave = Wave;
            Charm.SpawnFieldMs = WaveMs;
            Charm.ExpireFieldMs = ExpireMs;
            Out.Spawned.Add(Charm);
        }
        if (Points.Num() < Schedule.PerWave)
        {
            Out.Skipped.Add({Wave, int32(Schedule.PerWave) - Points.Num(), TEXT("NoFreePoint")});
        }
    }
}

int64 FLDCharmState::NextDueFieldMs(int64 FieldMs) const
{
    int64 Due = MAX_int64, WaveMs = 0;
    if (!Schedule.IsValid()) { return Due; }
    if (WavesDone < Schedule.WaveCount && Schedule.WaveAtMs(WavesDone, WaveMs) && WaveMs > FieldMs) { Due = WaveMs; }
    for (const FLDCharmInstance& Charm : Active)
    {
        if (Charm.ExpireFieldMs > FieldMs) { Due = FMath::Min(Due, Charm.ExpireFieldMs); }
    }
    return Due;
}

bool FLDCharmState::Remove(uint64 Serial, FLDCharmInstance& Out)
{
    const int32 Index = Active.IndexOfByPredicate([Serial](const FLDCharmInstance& Charm) { return Charm.Serial == Serial; });
    if (Index == INDEX_NONE) { return false; }
    Out = Active[Index];
    Active.RemoveAt(Index);
    return true;
}

void FLDCharmState::Clear(TArray<FLDCharmInstance>& Out)
{
    Out = MoveTemp(Active);
    Active.Reset();
}

const LDAirdrop::FPoint* FLDCharmState::PointOf(const FLDCharmInstance& Charm) const
{
    return Schedule.Points.IsValidIndex(Charm.PointIndex) ? &Schedule.Points[Charm.PointIndex] : nullptr;
}

void FLDCharmState::View(TArray<FLDCharmView>& Out) const
{
    Out.Reset();
    for (const FLDCharmInstance& Charm : Active)
    {
        const LDAirdrop::FPoint* Point = PointOf(Charm);
        if (!Point) { continue; }
        FLDCharmView& View = Out.AddDefaulted_GetRef();
        View.Serial = Charm.Serial;
        View.Kind = Charm.Kind;
        View.PointId = Point->Id;
        View.PositionCm = Point->PositionCm;
        View.ExpireAtFieldMs = Charm.ExpireFieldMs;
    }
}

bool LDCharms::Resolve(const FLDAirdropState& Airdrop, const FLDDemoContentSnapshot& Content, FLDCharmState& Out, FString& Error)
{
    Out = {};
    if (!Airdrop.IsActive())
    {
        Error = TEXT("Charms need this match's resolved airdrop points.");
        return false;
    }
    const auto& P = Content.Get().Parameters;
    FLDCharmState Candidate;
    FLDCharmSchedule& S = Candidate.Schedule;
    S.FirstWaveMs = P.CharmFirstWaveMs;
    S.WaveIntervalMs = P.CharmWaveIntervalMs;
    S.WaveCount = P.CharmWaveCount;
    S.PerWave = P.CharmPerWave;
    S.LifetimeMs = P.CharmLifetimeMs;
    S.PickupRadiusCm = P.CharmPickupRadiusCm.Denominator > 0
        ? double(P.CharmPickupRadiusCm.Numerator) / double(P.CharmPickupRadiusCm.Denominator) : 0.;
    S.Points = Airdrop.Schedule.Points;
    // Server-only like the airdrop seed it derives from (an armed build-evaluation Case replays both).
    S.Seed = LDAirdrop::Mix(Airdrop.Schedule.Seed ^ 0x43484152'4D533139ull);
    S.Seed = S.Seed ? S.Seed : 1;
    if (P.CharmHealMaxHPBp <= 0 || P.CharmManaMaxMPBp <= 0 || P.CharmMoveSpeedBp <= 0 || P.CharmMoveSpeedMs <= 0 || !S.IsValid())
    {
        Error = TEXT("The approved charm parameters or the airdrop points they use are invalid.");
        return false;
    }
    Out = MoveTemp(Candidate);
    Error.Reset();
    return true;
}
