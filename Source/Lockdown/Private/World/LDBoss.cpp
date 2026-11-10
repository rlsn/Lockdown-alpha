#include "World/LDBoss.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapGeometry.h"

bool FLDBossRules::IsValid() const
{
    return StartedAtMs >= 0 && AppearAtMs > 0 && LeaveAtMs > AppearAtMs && FMath::IsFinite(AggroRadiusCm) && FMath::IsFinite(AttackRangeCm)
        && AttackRangeCm > 0 && AggroRadiusCm > AttackRangeCm && AttackerMemoryMs > 0 && !MonsterId.IsNone()
        && CreatureId != 0 && !SurfaceId.IsNone() && Waypoints.Num() == 4
        && !Waypoints.ContainsByPredicate([](const FVector& Point) { return Point.ContainsNaN(); });
}

bool FLDBossState::Spawned(const FLDCombatBodyRef& Ref, int64 NowMs)
{
    if (!IsSpawnDue(NowMs) || !Ref.IsValid() || Ref.Victim.EntityId != EntityId) { return false; }
    Phase = ELDBossPhase::Alive;
    Body = Ref;
    Waypoint = 0;
    Attackers.Reset();
    return true;
}

void FLDBossState::NoteAttack(const FLDCombatBodyRef& Source, int64 AtMs)
{
    if (Phase != ELDBossPhase::Alive || !Source.IsValid() || Source.Victim.Kind != ELDVictimKind::Contender) { return; }
    if (FAttacker* Known = Attackers.FindByPredicate([&Source](const FAttacker& A) { return A.Source == Source; }))
    {
        Known->AtMs = FMath::Max(Known->AtMs, AtMs);
        return;
    }
    Attackers.Add({Source, AtMs});
}

bool FLDBossState::Died(const FLDCombatBodyRef& Ref, const FGuid& Killer, int64 AtMs)
{
    if (!IsBody(Ref)) { return false; }
    Phase = ELDBossPhase::Down;
    Body = {};
    Attackers.Reset();
    KilledBy = Killer;
    DownAtMs = AtMs;
    return true;
}

bool FLDBossState::Leave(int64 NowMs)
{
    if (!IsLeaveDue(NowMs)) { return false; }
    Phase = ELDBossPhase::Left;
    Body = {};
    Attackers.Reset();
    return true;
}

FLDBossDecision FLDBossState::Decide(int64 NowMs, const FVector& SelfFootCm, TConstArrayView<LDBoss::FCandidate> Living,
    bool bMovementAllowed, bool bAttackReady)
{
    FLDBossDecision Out;
    if (Phase != ELDBossPhase::Alive) { return Out; }
    Attackers.RemoveAll([this, NowMs](const FAttacker& A) { return NowMs - A.AtMs > Rules.AttackerMemoryMs; });
    // Nearest first, then the entity id, so the order of the inputs never decides.
    const LDBoss::FCandidate* Best = nullptr;
    bool bBestAttacker = false;
    double BestDistance = 0;
    for (const LDBoss::FCandidate& Candidate : Living)
    {
        if (!Candidate.bVisible || !Candidate.Body.IsValid() || Candidate.FootCm.ContainsNaN()) { continue; }
        const bool bAttacker = Attackers.ContainsByPredicate([&Candidate](const FAttacker& A) { return A.Source == Candidate.Body; });
        const double Distance = FVector::DistSquared2D(SelfFootCm, Candidate.FootCm);
        if (!bAttacker && Distance > Rules.AggroRadiusCm * Rules.AggroRadiusCm) { continue; }
        const bool bBetter = !Best || (bAttacker != bBestAttacker ? bAttacker
            : Distance != BestDistance ? Distance < BestDistance
            : Candidate.Body.Victim.EntityId < Best->Body.Victim.EntityId);
        if (bBetter)
        {
            Best = &Candidate;
            bBestAttacker = bAttacker;
            BestDistance = Distance;
        }
    }
    if (!Best)
    {
        Out.Intent = bMovementAllowed ? ELDBossIntent::Patrol : ELDBossIntent::None;
        Out.GoalFootCm = PatrolGoal();
        return Out;
    }
    Out.Target = Best->Body;
    Out.GoalFootCm = Best->FootCm;
    if (BestDistance <= Rules.AttackRangeCm * Rules.AttackRangeCm)
    {
        Out.Intent = bAttackReady ? ELDBossIntent::Attack : ELDBossIntent::None;
    }
    else
    {
        Out.Intent = bMovementAllowed ? ELDBossIntent::Chase : ELDBossIntent::None;
    }
    return Out;
}

FLDBossView FLDBossState::View() const
{
    FLDBossView Out;
    if (!IsActive()) { return Out; }
    Out.Phase = Phase;
    Out.AppearAtMs = AppearAtMs();
    Out.LeaveAtMs = LeaveAtMs();
    Out.KilledBy = KilledBy;
    Out.DownAtMs = DownAtMs;
    return Out;
}

bool LDBoss::Resolve(const FLDDemoContentSnapshot& Content, const FLDDemoWorldGeometry& Geometry, int64 StartedAtMs,
    FLDBossState& Out, FString& Error)
{
    Out = {};
    const FLDDemoContentDefinitions& D = Content.Get();
    const FLDDemoMonsterDefinition* Body = D.Monsters.FindByPredicate(
        [&D](const FLDDemoMonsterDefinition& M) { return M.Tier == ELDDemoCampTier::Boss && M.Id == D.Boss.Id; });
    auto Cm = [](const FLDDemoRational& Value) { return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : 0.; };
    FLDBossState Candidate;
    FLDBossRules& R = Candidate.Rules;
    R.StartedAtMs = StartedAtMs;
    R.AppearAtMs = D.Boss.AppearAtMs;
    R.LeaveAtMs = D.Boss.LeaveAtMs;
    R.AggroRadiusCm = Cm(D.Boss.AggroRadiusCm);
    R.AttackerMemoryMs = D.Boss.AttackerMemoryMs;
    if (Body)
    {
        R.AttackRangeCm = Cm(Body->AttackRangeCm);
        R.MonsterId = FName(*Body->Id);
        R.CreatureId = Body->NativeId;
    }
    // Waypoints in id order: boss_patrol_1 .. boss_patrol_4.
    TArray<const FLDDemoWorldAnchor*> Points;
    for (const FLDDemoWorldAnchor& Anchor : Geometry.Anchors)
    {
        if (Anchor.Kind == ELDDemoWorldAnchorKind::BossWaypoint) { Points.Add(&Anchor); }
    }
    Points.Sort([](const FLDDemoWorldAnchor& A, const FLDDemoWorldAnchor& B) { return A.Id < B.Id; });
    for (const FLDDemoWorldAnchor* Anchor : Points)
    {
        FVector Point;
        if (!Anchor->PositionCm.ToVector(Point, Error)) { return false; }
        R.Waypoints.Add(Point);
        if (R.SurfaceId.IsNone()) { R.SurfaceId = FName(*Anchor->SurfaceId); }
    }
    Candidate.Phase = ELDBossPhase::Pending;
    Candidate.EntityId = FGuid::NewGuid();
    if (!Body || !R.IsValid())
    {
        Error = TEXT("The approved Boss block, its monster row or the four Boss waypoints are invalid.");
        return false;
    }
    Out = MoveTemp(Candidate);
    Error.Reset();
    return true;
}
