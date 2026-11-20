#include "World/LDFieldPacing.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Framework/LDMatchTimeline.h"
#include "World/LDAirdrop.h"

namespace LDFieldPacingDetail
{
double Cm(const FLDDemoRational& Value) { return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : 0.; }
bool Inside(const FVector& Centre, double Radius, const FVector& Foot) { return FVector::DistSquared2D(Centre, Foot) <= Radius * Radius; }
}

ELDObjectiveKind LDFieldPacing::KindOfSegment(int32 Segment)
{
    switch (Segment)
    {
    case 0: case 2: return ELDObjectiveKind::MiniFlag;
    case 1: return ELDObjectiveKind::Arena;
    default: return ELDObjectiveKind::None;
    }
}

int32 LDFieldPacing::Draw(uint64 Seed, uint64 Salt, int32 Count)
{
    if (Count <= 0) { return INDEX_NONE; }
    return int32(LDAirdrop::Mix(Seed ^ LDAirdrop::Mix(Salt + 0x632BE59BD9B4E019ull)) % uint64(Count));
}

bool FLDObjectiveState::Begin(const FLDDemoContentParameters& Parameters, TConstArrayView<LDFieldPacing::FPoint> Flags,
    const FVector& Arena, uint64 InSeed, FString& Error)
{
    *this = FLDObjectiveState();
    const auto& O = Parameters.Objective;
    SegmentMs = Parameters.FieldSegmentMs;
    StartMs = O.StartMs;
    AnnounceLeadMs = O.AnnounceLeadMs;
    FlagWindowMs = O.FlagWindowMs;
    FlagSprayMs = O.FlagSprayMs;
    ArenaMs = O.ArenaMs;
    FlagRadiusCm = LDFieldPacingDetail::Cm(O.FlagRadiusCm);
    ArenaRadiusCm = LDFieldPacingDetail::Cm(O.ArenaRadiusCm);
    MiniFlags = TArray<LDFieldPacing::FPoint>(Flags.GetData(), Flags.Num());
    ArenaCentreCm = Arena;
    Seed = InSeed == 0 ? 1 : InSeed;
    if (SegmentMs <= 0 || StartMs < AnnounceLeadMs || StartMs + FMath::Max(FlagWindowMs, ArenaMs) > SegmentMs || FlagSprayMs <= 0
        || FlagRadiusCm <= 0 || ArenaRadiusCm <= 0 || MiniFlags.IsEmpty() || Arena.ContainsNaN())
    {
        Error = TEXT("Mid-field objectives need approved timings, radii, mini flag points and the flagpole.");
        *this = FLDObjectiveState();
        return false;
    }
    Error.Reset();
    return true;
}

int64 FLDObjectiveState::NextDueT(int64 NowT) const
{
    if (!IsActive()) { return MAX_int64; }
    for (int32 Segment = 0; Segment < FLDTimelineParameters::TimedSegments; ++Segment)
    {
        const ELDObjectiveKind Kind = LDFieldPacing::KindOfSegment(Segment);
        const int64 Open = int64(Segment) * SegmentMs + StartMs;
        const int64 Close = Open + (Kind == ELDObjectiveKind::Arena ? ArenaMs : FlagWindowMs);
        for (const int64 At : {Open - AnnounceLeadMs, Open, Close})
        {
            if (At > NowT) { return At; }
        }
    }
    return MAX_int64;
}

bool FLDObjectiveState::Advance(int64 NowT)
{
    if (!IsActive()) { return false; }
    const FLDObjectiveView Before = View;
    for (int32 Segment = 0; Segment < FLDTimelineParameters::TimedSegments; ++Segment)
    {
        const ELDObjectiveKind Kind = LDFieldPacing::KindOfSegment(Segment);
        const int64 Open = int64(Segment) * SegmentMs + StartMs;
        const int64 Close = Open + (Kind == ELDObjectiveKind::Arena ? ArenaMs : FlagWindowMs);
        if (NowT < Open - AnnounceLeadMs) { break; }
        if (View.Serial != Segment + 1)
        {
            // A new segment's objective: drawn once from the server-only seed.
            View = FLDObjectiveView();
            View.Serial = uint8(Segment + 1);
            View.Kind = Kind;
            View.OpensAtT = Open;
            View.ClosesAtT = Close;
            if (Kind == ELDObjectiveKind::Arena)
            {
                View.CentreCm = ArenaCentreCm;
                View.RadiusCm = float(ArenaRadiusCm);
            }
            else
            {
                const LDFieldPacing::FPoint& Point = MiniFlags[LDFieldPacing::Draw(Seed, uint64(Segment + 1), MiniFlags.Num())];
                View.AnchorId = Point.Id;
                View.CentreCm = Point.PositionCm;
                View.RadiusCm = float(FlagRadiusCm);
                View.SprayNeedMs = FlagSprayMs;
            }
            View.Phase = ELDObjectivePhase::Announced;
            ArenaDamageP2.Reset();
            ArenaLastHitT.Reset();
        }
        if (View.Phase == ELDObjectivePhase::Announced && NowT >= Open)
        {
            View.Phase = ELDObjectivePhase::Open;
            LastStepT = Open;
        }
        // The arena's close is settled by its owner (the winner needs the living feet); a flag nobody sprayed expires.
        if (View.Phase == ELDObjectivePhase::Open && NowT >= Close && Kind == ELDObjectiveKind::MiniFlag)
        {
            View.Phase = ELDObjectivePhase::Expired;
            View.Sprayer.Invalidate();
            View.SprayMs = 0;
            View.bContested = false;
        }
    }
    return Before.Phase != View.Phase || Before.Serial != View.Serial;
}

bool FLDObjectiveState::IsInside(const FVector& FootCm) const
{
    return View.Phase == ELDObjectivePhase::Open && LDFieldPacingDetail::Inside(View.CentreCm, View.RadiusCm, FootCm);
}

bool FLDObjectiveState::StepFlag(int64 NowT, TConstArrayView<LDFieldPacing::FPresence> Living, bool& bChanged)
{
    bChanged = false;
    if (View.Kind != ELDObjectiveKind::MiniFlag || View.Phase != ELDObjectivePhase::Open) { return false; }
    const int64 End = FMath::Min(NowT, View.ClosesAtT);
    const int64 Elapsed = FMath::Max<int64>(0, End - LastStepT);
    LastStepT = FMath::Max(LastStepT, End);
    TArray<FGuid> Inside;
    for (const LDFieldPacing::FPresence& Presence : Living)
    {
        // A protected or hard-controlled life cannot spray, like at the flagpole.
        if (!Presence.bProtected && !Presence.bControlled && IsInside(Presence.FootCm)) { Inside.Add(Presence.Contender); }
    }
    const FGuid OldSprayer = View.Sprayer;
    const int64 OldSpray = View.SprayMs;
    const bool bOldContested = View.bContested;
    // Leaving clears the spray; a hard control clears it too (flagpole rule).
    const LDFieldPacing::FPresence* Holder = View.Sprayer.IsValid()
        ? Living.FindByPredicate([this](const LDFieldPacing::FPresence& P) { return P.Contender == View.Sprayer; }) : nullptr;
    if (View.Sprayer.IsValid() && (!Holder || !Inside.Contains(View.Sprayer) || Holder->bControlled))
    {
        View.Sprayer.Invalidate();
        View.SprayMs = 0;
    }
    View.bContested = Inside.Num() >= 2;
    if (Inside.Num() == 1)
    {
        if (View.Sprayer != Inside[0])
        {
            // A new sole sprayer starts from zero at this boundary.
            View.Sprayer = Inside[0];
            View.SprayMs = 0;
        }
        else
        {
            View.SprayMs = FMath::Min(View.SprayNeedMs, View.SprayMs + Elapsed);
        }
    }
    // Two or more inside pause the holder's progress (kept while it stays inside).
    bChanged = OldSprayer != View.Sprayer || OldSpray != View.SprayMs || bOldContested != View.bContested;
    if (View.Sprayer.IsValid() && !View.bContested && View.SprayMs >= View.SprayNeedMs)
    {
        View.Winner = View.Sprayer;
        View.Phase = ELDObjectivePhase::Won;
        bChanged = true;
        return true;
    }
    return false;
}

void FLDObjectiveState::NoteArenaDamage(const FGuid& Attacker, int64 DamageP2, int64 NowT)
{
    if (View.Kind != ELDObjectiveKind::Arena || View.Phase != ELDObjectivePhase::Open || !Attacker.IsValid() || DamageP2 <= 0) { return; }
    int64& Total = ArenaDamageP2.FindOrAdd(Attacker);
    Total = Total > MAX_int64 - DamageP2 ? MAX_int64 : Total + DamageP2;
    ArenaLastHitT.Add(Attacker, NowT);
}

FGuid FLDObjectiveState::ArenaWinner(TConstArrayView<LDFieldPacing::FPresence> Living) const
{
    FGuid Best;
    int64 BestDamage = 0, BestLast = MAX_int64;
    for (const LDFieldPacing::FPresence& Presence : Living)
    {
        if (!LDFieldPacingDetail::Inside(View.CentreCm, View.RadiusCm, Presence.FootCm)) { continue; }
        const int64 Damage = ArenaDamageP2.FindRef(Presence.Contender);
        if (Damage <= 0) { continue; }
        const int64 Last = ArenaLastHitT.FindRef(Presence.Contender);
        // Spec §10.3: the most damage; ties by the earlier last hit (then the smaller id, deterministic).
        if (Damage > BestDamage || (Damage == BestDamage && (Last < BestLast || (Last == BestLast && Presence.Contender < Best))))
        {
            Best = Presence.Contender;
            BestDamage = Damage;
            BestLast = Last;
        }
    }
    return Best;
}

void LDFieldPacing::DrawCandidates(uint64 Seed, uint64 Salt, TConstArrayView<FName> Anchors, int32 Count, TArray<FName>& Out)
{
    Out = TArray<FName>(Anchors.GetData(), Anchors.Num());
    const int32 Take = FMath::Clamp(Count, 0, Out.Num());
    for (int32 Index = 0; Index < Take; ++Index)
    {
        const int32 Pick = Index + Draw(Seed, Salt * 31 + uint64(Index), Out.Num() - Index);
        Out.Swap(Index, Pick);
    }
    Out.SetNum(Take);
}

int32 LDFieldPacing::NearestCandidate(TConstArrayView<FVector> Candidates, TConstArrayView<FVector> Goals)
{
    int32 Best = Candidates.IsEmpty() ? INDEX_NONE : 0;
    double BestDistance = MAX_dbl;
    for (int32 Index = 0; Index < Candidates.Num(); ++Index)
    {
        for (const FVector& Goal : Goals)
        {
            const double Distance = FVector::DistSquared2D(Candidates[Index], Goal);
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                Best = Index;
            }
        }
    }
    return Best;
}

bool FLDLockerState::Begin(const FLDDemoContentParameters& Parameters, TConstArrayView<LDFieldPacing::FPoint> Points, uint64 Seed,
    FString& Error)
{
    *this = FLDLockerState();
    RadiusCm = LDFieldPacingDetail::Cm(Parameters.Explore.LockerRadiusCm);
    ChannelMs = Parameters.Explore.LockerChannelMs;
    const auto& X = Parameters.Explore;
    const int32 Count = X.LockerCount;
    if (RadiusCm <= 0 || ChannelMs <= 0 || Count < 1 || Count > Points.Num() || X.LockerEarliestMs < 0
        || X.LockerEarliestMs > X.LockerLatestMs || X.LockerStayMs <= ChannelMs)
    {
        Error = FString::Printf(TEXT("Delivery lockers need %d stocked of the %d locker_* points and approved timings."), Count, Points.Num());
        return false;
    }
    const uint64 Span = uint64(X.LockerLatestMs - X.LockerEarliestMs) + 1;
    TArray<FName> Ids;
    for (const LDFieldPacing::FPoint& Point : Points) { Ids.Add(Point.Id); }
    TArray<FName> Stocked;
    LDFieldPacing::DrawCandidates(Seed, 0x10CCE7, Ids, Count, Stocked);
    Stocked.Sort(FNameLexicalLess());
    for (int32 Index = 0; Index < Stocked.Num(); ++Index)
    {
        const FName& Id = Stocked[Index];
        FLocker& Locker = Lockers.AddDefaulted_GetRef();
        Locker.Point = *Points.FindByPredicate([&Id](const LDFieldPacing::FPoint& P) { return P.Id == Id; });
        Locker.AppearT = X.LockerEarliestMs + int64(LDAirdrop::Mix(Seed ^ (0x10CCA0ull + uint64(Index))) % Span);
        Locker.LeaveT = Locker.AppearT + X.LockerStayMs;
    }
    Error.Reset();
    return true;
}

void FLDLockerState::Step(int64 NowT, TConstArrayView<LDFieldPacing::FPresence> Living, TArray<TPair<int32, FGuid>>& Opened, bool& bChanged)
{
    Opened.Reset();
    bChanged = false;
    for (int32 Index = 0; Index < Lockers.Num(); ++Index)
    {
        FLocker& Locker = Lockers[Index];
        const bool bOut = Locker.IsOut(NowT);
        if (bOut != Locker.bShown) { Locker.bShown = bOut; bChanged = true; }
        if (!bOut)
        {
            if (Locker.Opener.IsValid()) { Locker.Opener.Invalidate(); bChanged = true; }
            continue;
        }
        const auto InsideOf = [&](const LDFieldPacing::FPresence& P)
        { return !P.bProtected && LDFieldPacingDetail::Inside(Locker.Point.PositionCm, RadiusCm, P.FootCm); };
        if (Locker.Opener.IsValid())
        {
            const LDFieldPacing::FPresence* Opener = Living.FindByPredicate([&](const LDFieldPacing::FPresence& P) { return P.Contender == Locker.Opener; });
            if (!Opener || !InsideOf(*Opener) || Opener->bDamaged || Opener->bControlled)
            {
                Locker.Opener.Invalidate();
                bChanged = true;
            }
            else if (NowT - Locker.ChannelSinceT >= ChannelMs)
            {
                Locker.bOpened = true;
                Opened.Emplace(Index, Locker.Opener);
                Locker.Opener.Invalidate();
                bChanged = true;
                continue;
            }
        }
        if (!Locker.Opener.IsValid())
        {
            FGuid Next;
            for (const LDFieldPacing::FPresence& Presence : Living)
            {
                if (InsideOf(Presence) && !Presence.bDamaged && !Presence.bControlled && (!Next.IsValid() || Presence.Contender < Next))
                {
                    Next = Presence.Contender;
                }
            }
            if (Next.IsValid())
            {
                Locker.Opener = Next;
                Locker.ChannelSinceT = NowT;
                bChanged = true;
            }
        }
    }
}

int64 FLDLockerState::NextDueT(int64 NowT) const
{
    int64 Due = MAX_int64;
    for (const FLocker& Locker : Lockers)
    {
        if (Locker.bOpened) { continue; }
        if (Locker.AppearT > NowT) { Due = FMath::Min(Due, Locker.AppearT); }
        else if (Locker.LeaveT > NowT) { Due = FMath::Min(Due, Locker.LeaveT); }
        if (Locker.Opener.IsValid()) { Due = FMath::Min(Due, Locker.ChannelSinceT + ChannelMs); }
    }
    return Due;
}

void FLDLockerState::View(int64 NowT, TArray<FLDLockerView>& Out) const
{
    Out.Reset();
    for (const FLocker& Locker : Lockers)
    {
        if (!Locker.IsOut(NowT)) { continue; }
        FLDLockerView& Row = Out.AddDefaulted_GetRef();
        Row.AnchorId = Locker.Point.Id;
        Row.PositionCm = Locker.Point.PositionCm;
        Row.ClosesAtT = Locker.LeaveT;
        Row.bChanneling = Locker.Opener.IsValid();
    }
}

bool FLDPeddlerPlan::Plan(const FLDDemoContentParameters& Parameters, TConstArrayView<FName> Camps, uint64 Seed, FString& Error)
{
    *this = FLDPeddlerPlan();
    const auto& X = Parameters.Explore;
    if (Camps.IsEmpty() || X.PeddlerEarliestMs > X.PeddlerLatestMs || X.PeddlerStayMs <= 0)
    {
        Error = TEXT("The dealer needs camp anchors and an approved appearance window.");
        return false;
    }
    const uint64 Span = uint64(X.PeddlerLatestMs - X.PeddlerEarliestMs) + 1;
    AppearT = X.PeddlerEarliestMs + int64(LDAirdrop::Mix(Seed ^ 0x9EDD1E5ull) % Span);
    LeaveT = AppearT + X.PeddlerStayMs;
    CampAnchor = Camps[LDFieldPacing::Draw(Seed, 0x9EDD1E6ull, Camps.Num())];
    OffsetAngleRad = double(LDAirdrop::Mix(Seed ^ 0x9EDD1E7ull) % 3600) / 3600. * 2. * UE_DOUBLE_PI;
    Error.Reset();
    return true;
}
