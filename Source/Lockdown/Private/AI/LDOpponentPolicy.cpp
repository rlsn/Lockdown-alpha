#include "AI/LDOpponentPolicy.h"
#include "Combat/LDTacticalRules.h"
#include "Development/LDCampGrowth.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Development/LDProgressionRules.h"

// Unity builds merge translation units: every file-scope helper lives in this module-unique namespace.
namespace LDOpponentPolicy::Detail
{
using EBody = ELDOpponentBodyKind;
using EMode = ELDOpponentMode;
using EReason = ELDOpponentReason;

double ToDouble(const FLDDemoRational& Value)
{
    return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : 0.;
}

// Value / Max >= Bp / 10000, exact for catalog-bounded P2 resources.
bool RatioAtLeast(int64 Value, int64 Max, int64 Bp)
{
    if (Max <= 0) { return false; }
    if (Value > MAX_int64 / 10000 || Bp > MAX_int64 / Max)
    {
        return double(Value) * 10000. >= double(Bp) * double(Max);
    }
    return Value * 10000 >= Bp * Max;
}

int64 SaturatingAdd(int64 A, int64 B)
{
    return B > 0 && A > MAX_int64 - B ? MAX_int64 : A + B;
}

template <typename TDefinition>
int32 CatalogIndex(const TArray<TDefinition>& Definitions, const FString& Id)
{
    return Definitions.IndexOfByPredicate([&Id](const TDefinition& Definition)
    { return Definition.Id.Equals(Id, ESearchCase::CaseSensitive); });
}

void ClassifyEffect(const FLDDemoEffectDefinition& Effect, bool bSelfTarget, FLDOpponentUsage& Usage)
{
    switch (Effect.GetOp())
    {
    case ELDDemoEffectOp::Damage:
    case ELDDemoEffectOp::Root:
    case ELDDemoEffectOp::Slow:
    case ELDDemoEffectOp::Knockback:
    case ELDDemoEffectOp::ZoneSlow:
    case ELDDemoEffectOp::ZoneDamage:
    // #147 build v7 keywords on enemies.
    case ELDDemoEffectOp::Silence:
    case ELDDemoEffectOp::Breach:
    case ELDDemoEffectOp::Burn:
    case ELDDemoEffectOp::ZoneBurn:
    case ELDDemoEffectOp::AttackSlow:
    case ELDDemoEffectOp::ChannelCone:
    case ELDDemoEffectOp::GroundImpact:
    case ELDDemoEffectOp::ContactTrap:
    case ELDDemoEffectOp::DashContact:
        Usage.bEngage |= !bSelfTarget;
        break;
    case ELDDemoEffectOp::ControlDamageAmp:
    case ELDDemoEffectOp::DashContactDamage:
        Usage.bEngage = true;
        break;
    case ELDDemoEffectOp::Disarm:
    case ELDDemoEffectOp::Stun:
        Usage.bEngage |= !bSelfTarget;
        break;
    case ELDDemoEffectOp::TriggeredShield:
        Usage.bSelfProtect = true;
        break;
    case ELDDemoEffectOp::Shield:
    case ELDDemoEffectOp::DispelRoot:
    case ELDDemoEffectOp::DispelSlow:
        Usage.bSelfProtect |= bSelfTarget;
        break;
    case ELDDemoEffectOp::Dash:
    case ELDDemoEffectOp::DashContactRefund:
        Usage.bMobility = true;
        break;
    // Survival, basic-attack, stat, cooldown, mana and release riders add no structural usage of their own.
    case ELDDemoEffectOp::Zone:
    case ELDDemoEffectOp::CooldownRefund:
    case ELDDemoEffectOp::ManaOnHit:
    default:
        break;
    }
}

// The typed target and effects of one bound active source.
struct FSourceShape
{
    ELDDemoTargetMode Mode = ELDDemoTargetMode::Self;
    double RangeCm = 0;
    bool bDamage = false;
    bool bRoot = false;
    // Hard controls that also interrupt a spray (APP_SPEC 2.2); a self taunt reaches contenders within its radius.
    // bStun: a stun or (#147 D10) a silence.
    bool bStun = false;
    bool bTaunt = false;
    double TauntRadiusCm = 0;
    bool bShield = false;
    bool bDispelRoot = false;
    bool bDash = false;
    // #147: a self cast that hits bodies around the caster (SelfArea), a ground zone, and an empowered next basic.
    double AreaRadiusCm = 0;
    double ZoneRadiusCm = 0;
    bool bEmpowerBasic = false;
};

void ReadEffects(const TArray<FLDDemoEffectDefinition>& Effects, FSourceShape& Out)
{
    for (const auto& Effect : Effects)
    {
        switch (Effect.GetOp())
        {
        case ELDDemoEffectOp::Damage:
        case ELDDemoEffectOp::ZoneDamage:
        case ELDDemoEffectOp::Burn:
        case ELDDemoEffectOp::ZoneBurn: Out.bDamage = true; break;
        case ELDDemoEffectOp::SelfArea: Out.AreaRadiusCm = FMath::Max(Out.AreaRadiusCm, ToDouble(Effect.RadiusCm)); break;
        case ELDDemoEffectOp::Zone: Out.ZoneRadiusCm = FMath::Max(Out.ZoneRadiusCm, ToDouble(Effect.RadiusCm)); break;
        case ELDDemoEffectOp::GroundImpact:
        case ELDDemoEffectOp::ContactTrap: Out.ZoneRadiusCm = FMath::Max(Out.ZoneRadiusCm, ToDouble(Effect.RadiusCm)); break;
        case ELDDemoEffectOp::EmpowerBasic: Out.bEmpowerBasic = true; break;
        case ELDDemoEffectOp::Root: Out.bRoot = true; break;
        case ELDDemoEffectOp::Stun:
        case ELDDemoEffectOp::Silence: Out.bStun = true; break;
        case ELDDemoEffectOp::Taunt:
            Out.bTaunt = true;
            Out.TauntRadiusCm = FMath::Max(Out.TauntRadiusCm, ToDouble(Effect.RadiusCm));
            break;
        case ELDDemoEffectOp::Shield: Out.bShield = true; break;
        case ELDDemoEffectOp::DispelRoot: Out.bDispelRoot = true; break;
        case ELDDemoEffectOp::Dash:
            Out.bDash = true;
            Out.RangeCm = FMath::Max(Out.RangeCm, ToDouble(Effect.DistanceCm));
            break;
        default: break;
        }
    }
}

// The typed target and effects of the active bound to a source (#198: one value set per active).
bool ResolveSource(const FLDOpponentSource& Source, const FLDDemoContentSnapshot& Catalog, FSourceShape& Out)
{
    Out = {};
    FString Error;
    if (Source.Slot == ELDOpponentSourceSlot::Dash)
    {
        // The innate dash: its reach is the authority's current read (D20 boosts included), never a local recomputation.
        Out.Mode = ELDDemoTargetMode::DirectionDash;
        Out.bDash = true;
        Out.RangeCm = double(Source.RangeCm);
        return Source.RangeCm > 0;
    }
    const FLDDemoAbilityDefinition* Ability = nullptr;
    if (!Catalog.FindAbility(Source.DefinitionId, Ability, Error) || !Ability) { return false; }
    Out.Mode = Ability->Target.Mode;
    Out.RangeCm = ToDouble(Ability->Target.RangeCm);
    ReadEffects(Ability->Values.Effects, Out);
    return true;
}

struct FActivationNeed
{
    bool bProtect = false;
    bool bCleanseRoot = false;
    bool bAllowDamage = true;
    // The target is the public sole sprayer: any ready hard control (root, stun, taunt) clears his spray progress.
    bool bInterrupt = false;
    const FLDOpponentUnit* Target = nullptr;
    TOptional<FVector> DashDirection;
    // The target may be chased with any ready dash whose own reach lands inside attack range (ChaseDirection).
    bool bChase = false;
};

struct FContext
{
    FContext(const FLDOpponentDecisionFrame& InFrame, const FLDDemoContentSnapshot& InCatalog, FLDOpponentPolicyState& InState)
        : Frame(InFrame), Self(InFrame.Self), Catalog(InCatalog), AI(InCatalog.Get().Parameters.AI), State(InState),
          Now(InFrame.Public.NowMs) {}

    const FLDOpponentDecisionFrame& Frame;
    const FLDOpponentSelf& Self;
    const FLDDemoContentSnapshot& Catalog;
    const FLDDemoAIParameters& AI;
    FLDOpponentPolicyState& State;
    const int64 Now;
    const FLDOpponentPOI* Capture = nullptr;
    double CaptureRadiusCm = 0;
    double DormRadiusCm = 0;
    double AirdropRadiusCm = 0;
    double LeaderRadiusCm = 0;
    // D26: the visible Boss and the band in which it acquires a contender (its aggro radius plus its reach).
    const FLDOpponentUnit* Boss = nullptr;
    double BossBandCm = 0;
    double AttackRangeCm = 0;
    double AcquireRadiusCm = 0;
    double VisionRadiusCm = 0;
    bool bCaptureOpen = false;
    bool bInsideCircle = false;
    TArray<const FLDOpponentUnit*> Enemies;
    TArray<const FLDOpponentUnit*> Threats;

    bool Initialize()
    {
        const auto& Content = Catalog.Get();
        for (const auto& POI : Frame.POIs)
        {
            if (POI.Kind == ELDOpponentPOIKind::Capture && !Capture) { Capture = &POI; }
        }
        CaptureRadiusCm = ToDouble(Content.Parameters.CaptureRadiusCm);
        DormRadiusCm = ToDouble(Content.Parameters.DormRadiusCm);
        AirdropRadiusCm = ToDouble(Content.Parameters.AirdropPickupRadiusCm);
        LeaderRadiusCm = ToDouble(Content.Parameters.LeaderCircleRadiusCm);
        const FLDDemoMonsterDefinition* BossBody = Content.Monsters.FindByPredicate(
            [&Content](const FLDDemoMonsterDefinition& M) { return M.Tier == ELDDemoCampTier::Boss && M.Id == Content.Boss.Id; });
        BossBandCm = BossBody ? ToDouble(Content.Boss.AggroRadiusCm) + ToDouble(BossBody->AttackRangeCm) : 0.;
        AttackRangeCm = ToDouble(Self.Attributes.AttackRangeCm);
        VisionRadiusCm = ToDouble(Content.Parameters.VisionRadiusCm);
        if (Content.Formulas.AttackMoveAcquireRadius != ELDDemoFormula::MinVisionTwiceAttackRange
            || !(AttackRangeCm > 0) || !(VisionRadiusCm > 0) || !(CaptureRadiusCm > 0) || !(DormRadiusCm > 0) || !(AirdropRadiusCm > 0) || !(LeaderRadiusCm > 0) || !(BossBandCm > 0) || !Capture)
        {
            return false;
        }
        AcquireRadiusCm = FMath::Min(VisionRadiusCm, 2. * AttackRangeCm);
        bCaptureOpen = Frame.Public.Phase == ELDMatchPhase::CaptureOpen;
        bInsideCircle = FVector::Dist2D(Self.FootCm, Capture->FootCm) <= CaptureRadiusCm;
        for (const auto& Unit : Frame.Units)
        {
            if (Unit.Kind == ELDOpponentUnitKind::Contender && Unit.ContenderId != Self.ContenderId)
            {
                Enemies.Add(&Unit);
                Threats.Add(&Unit);
            }
            else if (Unit.Kind == ELDOpponentUnitKind::CampMember && Unit.MonsterId.Equals(Catalog.Get().Boss.Id, ESearchCase::CaseSensitive))
            {
                // D26: the Boss turns on anyone near it, so a seen Boss is always a threat; AIs never pick a fight with it.
                Boss = &Unit;
                Threats.Add(&Unit);
            }
            else if (Unit.Kind == ELDOpponentUnitKind::CampMember && !Unit.bReturning && !State.VisitPOI.IsNone()
                && State.VisitEngagedAtMs != 0)
            {
                if (const FLDOpponentPOI* Camp = CampOf(Unit); Camp && Camp->Id == State.VisitPOI) { Threats.Add(&Unit); }
            }
        }
        return true;
    }

    // Members are associated with the nearest public camp of their own creature; no hidden camp table.
    const FLDOpponentPOI* CampOf(const FLDOpponentUnit& Unit) const
    {
        const FLDOpponentPOI* Best = nullptr;
        double BestDistance = 0;
        for (const auto& POI : Frame.POIs)
        {
            if (POI.Kind != ELDOpponentPOIKind::Camp || !POI.MonsterId.Equals(Unit.MonsterId, ESearchCase::CaseSensitive)) { continue; }
            const double Distance = FVector::Dist2D(POI.FootCm, Unit.FootCm);
            if (!Best || Distance < BestDistance || (Distance == BestDistance && POI.Id.Compare(Best->Id) < 0))
            {
                Best = &POI;
                BestDistance = Distance;
            }
        }
        return Best;
    }

    const FLDOpponentPOI* FindPOI(FName Id) const
    {
        return Frame.POIs.FindByPredicate([Id](const FLDOpponentPOI& POI) { return POI.Id == Id; });
    }

    double Distance(const FVector& Point) const { return FVector::Dist2D(Self.FootCm, Point); }
    // D21: the public dorm this body's foot stands in (closed disk, as the authority judges it), or nullptr.
    const FLDOpponentPOI* DormAt() const
    {
        return Frame.POIs.FindByPredicate([this](const FLDOpponentPOI& POI)
            { return POI.Kind == ELDOpponentPOIKind::Dorm && Distance(POI.FootCm) <= DormRadiusCm; });
    }
    // The nearest public dorm whose walk is not suppressed by failure memory; ties by stable id.
    const FLDOpponentPOI* NearestDorm() const
    {
        const FLDOpponentPOI* Best = nullptr;
        for (const auto& POI : Frame.POIs)
        {
            if (POI.Kind != ELDOpponentPOIKind::Dorm || !CanAttempt(PathKey(POI.Id))) { continue; }
            if (!Best || Distance(POI.FootCm) < Distance(Best->FootCm)
                || (Distance(POI.FootCm) == Distance(Best->FootCm) && POI.Id.Compare(Best->Id) < 0)) { Best = &POI; }
        }
        return Best;
    }
    // D26: the public point of the current airdrop when its walk is not suppressed by failure memory, or nullptr.
    const FLDOpponentPOI* AirdropTarget() const
    {
        const FLDOpponentPOI* Drop = Frame.Public.AirdropPOI.IsNone() ? nullptr : FindPOI(Frame.Public.AirdropPOI);
        return Drop && Drop->Kind == ELDOpponentPOIKind::Airdrop && CanAttempt(PathKey(Drop->Id)) ? Drop : nullptr;
    }
    // D27: the public place nearest the centre of another contender's 上热搜 circle, inside that circle, when its walk
    // is not suppressed; a seen leader is then engaged by the ordinary engage branch. Only for a body at the notice's
    // minimum level with high-camp entry health.
    const FLDOpponentPOI* LeaderHuntTarget() const
    {
        if (!Frame.Public.Leader.IsValid() || Frame.Public.Leader == Self.ContenderId
            || Self.Level < Catalog.Get().Parameters.LeaderMinLevel || !RatioAtLeast(Self.HpP2, Self.MaxHpP2, AI.HighCampEntryHPBp))
        {
            return nullptr;
        }
        const FVector& Centre = Frame.Public.LeaderCircleCentreCm;
        // An arrival without the leader in sight is remembered in the visit memory, so neither that place nor any other
        // place reached recently is ordered again while the next one is walked to (no turnback, no repeated orders).
        if (Frame.Order.Kind == ELDOpponentOrderKind::MoveGround && Frame.Order.Status == ELDOpponentOrderStatus::Completed)
        {
            const FLDOpponentPOI* Reached = Frame.POIs.FindByPredicate([this](const FLDOpponentPOI& P) { return P.Id == Frame.Order.POI; });
            if (Reached && FVector::Dist2D(Reached->FootCm, Centre) <= LeaderRadiusCm) { RecordVisit(Reached->Id); }
        }
        const FLDOpponentPOI* Best = nullptr;
        for (const auto& POI : Frame.POIs)
        {
            if (RecentlyVisited(POI.Id) || !CanAttempt(PathKey(POI.Id))
                || FVector::Dist2D(POI.FootCm, Centre) > LeaderRadiusCm) { continue; }
            const double Distance = FVector::Dist2D(POI.FootCm, Centre);
            if (!Best || Distance < FVector::Dist2D(Best->FootCm, Centre)
                || (Distance == FVector::Dist2D(Best->FootCm, Centre) && POI.Id.Compare(Best->Id) < 0)) { Best = &POI; }
        }
        return Best;
    }
    bool Rooted() const { return Self.MoveBlockedUntilMs > Now; }
    bool NeedProtection() const { return !Threats.IsEmpty() && !RatioAtLeast(Self.HpP2, Self.MaxHpP2, AI.ResumeFarmHPBp); }

    uint64 Print(ELDOpponentFailure Category) const { return LDOpponentPolicy::Fingerprint(Category, Self); }
    bool CanAttempt(const FLDOpponentRetryKey& Key) const { return State.Failures.CanAttempt(Key, Print(Key.Category), Now); }
    // #160: refused walks from this standing point hold every new walk, whatever its goal.
    bool CanWalk() const { return CanAttempt(WalkKey()); }
    static FLDOpponentRetryKey WalkKey() { FLDOpponentRetryKey Key; Key.Category = ELDOpponentFailure::Walk; return Key; }
    bool Suppressed(const FLDOpponentRetryKey& Key) const { return State.Failures.IsSuppressed(Key, Print(Key.Category)); }
    static FLDOpponentRetryKey PathKey(FName POI) { FLDOpponentRetryKey Key; Key.Category = ELDOpponentFailure::Path; Key.POI = POI; return Key; }
    static FLDOpponentRetryKey EngagementKey(FName POI, const FGuid& Target)
    {
        FLDOpponentRetryKey Key;
        Key.Category = ELDOpponentFailure::Engagement;
        Key.POI = POI;
        Key.Target = Target;
        return Key;
    }

    int64 VisitedAt(FName POI) const
    {
        const auto* Visit = State.Visits.FindByPredicate([POI](const FLDOpponentVisit& V) { return V.POI == POI; });
        return Visit ? Visit->VisitedAtMs : 0;
    }

    void RecordVisit(FName POI) const
    {
        if (auto* Visit = State.Visits.FindByPredicate([POI](const FLDOpponentVisit& V) { return V.POI == POI; }))
        {
            Visit->VisitedAtMs = Now;
            return;
        }
        // Bounded by the static public POI set of the approved map.
        if (State.Visits.Num() < Frame.POIs.Num()) { State.Visits.Add(FLDOpponentVisit{POI, Now}); }
    }

    bool RecentlyVisited(FName POI) const
    {
        const int64 At = VisitedAt(POI);
        return At != 0 && Now - At < AI.ExploreRevisitIntervalMs;
    }
};

void ForgetTarget(FLDOpponentPolicyState& State)
{
    State.TargetHandle.Invalidate();
    State.TargetEpoch = 0;
    State.TargetEngagedAtMs = 0;
}

// A contender engagement keeps its own target and budget when a camp visit ends.
void EndVisit(FLDOpponentPolicyState& State, bool bForgetTarget = true)
{
    State.VisitPOI = NAME_None;
    State.VisitEngagedAtMs = 0;
    if (bForgetTarget) { ForgetTarget(State); }
}

void ForgetBody(FLDOpponentPolicyState& State)
{
    EndVisit(State);
    State.ExplorePOI = NAME_None;
    State.RetreatPOI = NAME_None;
    State.bEscapingBoss = false;
    State.bRecovering = false;
}

bool UnitBefore(const FContext& C, const FLDOpponentUnit& A, const FLDOpponentUnit& B)
{
    const bool bAHp = A.HpP2.IsSet(), bBHp = B.HpP2.IsSet();
    if (bAHp != bBHp) { return bAHp; }
    if (bAHp && A.HpP2.GetValue() != B.HpP2.GetValue()) { return A.HpP2.GetValue() < B.HpP2.GetValue(); }
    const double DA = C.Distance(A.FootCm), DB = C.Distance(B.FootCm);
    if (DA != DB) { return DA < DB; }
    return A.Handle < B.Handle;
}

bool IsCurrentTarget(const FContext& C, const FLDOpponentUnit& Unit)
{
    return C.State.TargetHandle.IsValid() && Unit.Handle == C.State.TargetHandle && Unit.Epoch == C.State.TargetEpoch;
}

void Engage(const FContext& C, const FLDOpponentUnit& Unit)
{
    if (!IsCurrentTarget(C, Unit))
    {
        C.State.TargetHandle = Unit.Handle;
        C.State.TargetEpoch = Unit.Epoch;
        C.State.TargetEngagedAtMs = C.Now;
    }
}

FLDOpponentBodyIntent MoveTo(const FContext& C, const FLDOpponentPOI& POI)
{
    FLDOpponentBodyIntent Body;
    Body.POI = POI.Id;
    Body.Key = FContext::PathKey(POI.Id);
    const auto& Order = C.Frame.Order;
    Body.Kind = Order.IsLive() && Order.Kind == ELDOpponentOrderKind::MoveGround && Order.POI == POI.Id ? EBody::Keep
        : EBody::MoveGround;
    // A body whose walks are refused where it stands keeps what it has instead of trying the next place at once (#160).
    if (Body.Kind != EBody::Keep && !C.CanWalk()) { Body.Kind = Order.IsLive() ? EBody::Keep : EBody::None; }
    return Body;
}

bool HandleSuppressed(const FContext& C, const FLDOpponentUnit& Unit)
{
    return C.Suppressed(FContext::EngagementKey(NAME_None, Unit.Handle));
}

FLDOpponentBodyIntent Halt(const FContext& C)
{
    FLDOpponentBodyIntent Body;
    Body.Kind = C.Frame.Order.IsLive() ? EBody::Stop : EBody::None;
    return Body;
}

// Keeps any live order without sending anything new.
FLDOpponentBodyIntent Hold(const FContext& C)
{
    FLDOpponentBodyIntent Body;
    Body.Kind = C.Frame.Order.IsLive() ? EBody::Keep : EBody::None;
    return Body;
}

// A refused attack on this handle waits out its backoff instead of re-sending every decision. Meanwhile the
// body keeps its current order: stopping would only end useful motion toward the same fight.
FLDOpponentBodyIntent AttackBody(const FContext& C, const FLDOpponentUnit& Unit)
{
    FLDOpponentBodyIntent Body;
    Body.Handle = Unit.Handle;
    Body.Epoch = Unit.Epoch;
    Body.Key = FContext::EngagementKey(NAME_None, Unit.Handle);
    const auto& Order = C.Frame.Order;
    if (Order.IsLive() && Order.Kind == ELDOpponentOrderKind::AttackUnit && Order.Handle == Unit.Handle
        && Order.Epoch == Unit.Epoch)
    {
        Body.Kind = EBody::Keep;
        return Body;
    }
    // A target already in reach is struck where the body stands; only a chase needs the body's walking (#160).
    const bool bInReach = C.Distance(Unit.FootCm) <= C.AttackRangeCm;
    if (!C.CanAttempt(Body.Key) || (!bInReach && !C.CanWalk())) { return Hold(C); }
    Body.Kind = EBody::AttackUnit;
    return Body;
}

// Chase (D19): a ready dash of reach Reach closes on a target that keeps more than half that reach beyond attack range
// but is no further than one reach beyond it, so the landing is inside attack range.
TOptional<FVector> ChaseDirection(const FContext& C, const FLDOpponentUnit& Target, double Reach)
{
    if (!(Reach > 0)) { return {}; }
    const double Gap = C.Distance(Target.FootCm);
    if (Gap <= C.AttackRangeCm + .5 * Reach || Gap > C.AttackRangeCm + Reach) { return {}; }
    const FVector Toward = (Target.FootCm - C.Self.FootCm).GetSafeNormal2D();
    return Toward.IsNearlyZero() ? TOptional<FVector>() : TOptional<FVector>(Toward);
}

// §6.3 fixed order: self-protection, mobility, control, damage; basic attacks stay with the order.
TOptional<FLDOpponentBodyIntent> PickActivation(const FContext& C, const FActivationNeed& Need)
{
    if (C.Self.bChanneling) { return {}; }
    static constexpr ELDOpponentSourceSlot Slots[] = {
        ELDOpponentSourceSlot::Q, ELDOpponentSourceSlot::W, ELDOpponentSourceSlot::E, ELDOpponentSourceSlot::Dash};
    for (int32 Tier = 0; Tier < 4; ++Tier)
    {
        for (const ELDOpponentSourceSlot Slot : Slots)
        {
            const auto* Source = C.Self.Sources.FindByPredicate([Slot](const FLDOpponentSource& S) { return S.Slot == Slot; });
            FSourceShape Shape;
            if (!Source || !Source->bReady || !ResolveSource(*Source, C.Catalog, Shape)) { continue; }
            FLDOpponentRetryKey Key;
            Key.Category = ELDOpponentFailure::Activation;
            Key.Slot = uint8(Slot);
            const bool bTargetInRange = Need.Target && C.Distance(Need.Target->FootCm) <= Shape.RangeCm;
            FLDOpponentBodyIntent Body;
            Body.Kind = EBody::Activate;
            Body.Source = Slot;
            Body.TargetMode = Shape.Mode;
            bool bUse = false;
            switch (Tier)
            {
            case 0:
                bUse = Shape.Mode == ELDDemoTargetMode::Self
                    && ((Shape.bShield && Need.bProtect) || (Shape.bDispelRoot && Need.bCleanseRoot));
                break;
            case 1:
                if (Shape.Mode == ELDDemoTargetMode::DirectionDash && Shape.bDash)
                {
                    TOptional<FVector> Direction = Need.DashDirection.IsSet() ? Need.DashDirection
                        : Need.bChase && Need.Target ? ChaseDirection(C, *Need.Target, Shape.RangeCm) : TOptional<FVector>();
                    if (!Direction.IsSet() && Need.bAllowDamage && bTargetInRange && Shape.bDamage)
                    {
                        const FVector Toward=(Need.Target->FootCm-C.Self.FootCm).GetSafeNormal2D();
                        if (!Toward.IsNearlyZero()) { Direction=Toward; }
                    }
                    bUse = Direction.IsSet();
                    if (bUse) { Body.Direction = Direction.GetValue(); }
                }
                break;
            case 2:
                if (Need.bInterrupt && Shape.Mode == ELDDemoTargetMode::Self && Shape.bTaunt && Need.Target
                    && C.Distance(Need.Target->FootCm) <= Shape.TauntRadiusCm)
                {
                    bUse = true;
                    Key.Target = Need.Target->Handle;
                    break;
                }
                [[fallthrough]];
            case 3:
                if ((Tier == 2 ? Shape.bRoot || (Need.bInterrupt && Shape.bStun) : Shape.bDamage && Need.bAllowDamage) && bTargetInRange)
                {
                    if (Shape.Mode == ELDDemoTargetMode::Unit)
                    {
                        bUse = true;
                        Body.Handle = Need.Target->Handle;
                        Body.Epoch = Need.Target->Epoch;
                        Key.Target = Need.Target->Handle;
                    }
                    else if (Shape.Mode == ELDDemoTargetMode::DirectionProjectile || Shape.Mode == ELDDemoTargetMode::DirectionCone)
                    {
                        const FVector Direction = (Need.Target->FootCm - C.Self.FootCm).GetSafeNormal2D();
                        bUse = !Direction.IsNearlyZero();
                        Body.Direction = Direction;
                        Key.Target = Need.Target->Handle;
                    }
                    else if (Shape.Mode == ELDDemoTargetMode::GroundPoint && Shape.ZoneRadiusCm > 0)
                    {
                        // #147 active_quick_meeting: the zone is centred on the target's public foot.
                        bUse = true;
                        Body.Point = Need.Target->FootCm;
                        Key.Target = Need.Target->Handle;
                    }
                }
                if (Tier == 3 && Need.bAllowDamage && Need.Target && Shape.Mode == ELDDemoTargetMode::Self)
                {
                    // #147: a self area (active_seethe) needs the target inside its radius; an empowered basic without
                    // its own movement (active_triple_combo) needs the target inside attack range.
                    const double Gap = C.Distance(Need.Target->FootCm);
                    if ((Shape.bDamage && Shape.AreaRadiusCm > 0 && Gap <= Shape.AreaRadiusCm)
                        || (Shape.bEmpowerBasic && !Shape.bDash && Gap <= C.AttackRangeCm))
                    {
                        bUse = true;
                        Key.Target = Need.Target->Handle;
                    }
                }
                break;
            default:
                break;
            }
            if (bUse && C.CanAttempt(Key))
            {
                Body.Key = Key;
                return Body;
            }
        }
    }
    return {};
}

// §6.2 stop-loss: a ready self shield is the usable self-protection that still lets a low body answer defence.
bool ProtectionReady(const FContext& C)
{
    for (const auto& Source : C.Self.Sources)
    {
        FSourceShape Shape;
        if (!Source.bReady || !ResolveSource(Source, C.Catalog, Shape) || Shape.Mode != ELDDemoTargetMode::Self
            || !Shape.bShield)
        {
            continue;
        }
        FLDOpponentRetryKey Key;
        Key.Category = ELDOpponentFailure::Activation;
        Key.Slot = uint8(Source.Slot);
        if (C.CanAttempt(Key)) { return true; }
    }
    return false;
}

// The public sole sprayer, matched by the authorized ContenderId of a visible unit.
bool IsSoleSprayer(const FContext& C, const FLDOpponentUnit& Unit)
{
    const auto& Public = C.Frame.Public;
    return Public.Circle == ELDOpponentCircle::Solo && Public.Occupier.IsValid() && Unit.Kind == ELDOpponentUnitKind::Contender
        && Unit.ContenderId == Public.Occupier;
}

FLDOpponentBodyIntent EngageBody(const FContext& C, const FLDOpponentUnit& Target, bool bAllowDamage = true)
{
    Engage(C, Target);
    FActivationNeed Need;
    Need.bProtect = C.NeedProtection();
    Need.bAllowDamage = bAllowDamage;
    Need.bInterrupt = IsSoleSprayer(C, Target);
    Need.Target = &Target;
    // Only a visible rival contender is chased with a dash (innate or a learned dash active); camp members never are.
    Need.bChase = Target.Kind == ELDOpponentUnitKind::Contender && !Need.bProtect && bAllowDamage && !C.Rooted();
    if (auto Activation = PickActivation(C, Need)) { return Activation.GetValue(); }
    return AttackBody(C, Target);
}

const FLDOpponentUnit* CircleTarget(const FContext& C)
{
    // A contest only pauses the spray holder's progress, so a rival holder inside the zone is fought first, then a rival
    // graffiti owner standing in it (only his death clears the graffiti). Both are matched by the authorized ContenderId
    // of a visible unit, never turned into a target from the public id alone.
    const FGuid& Holder = C.Frame.Public.Occupier;
    const FGuid& Owner = C.Frame.Public.Owner;
    const FLDOpponentUnit* Current = nullptr;
    const FLDOpponentUnit* Best = nullptr;
    const FLDOpponentUnit* RivalOwner = nullptr;
    for (const auto* Unit : C.Enemies)
    {
        if (Unit->bProtected || HandleSuppressed(C, *Unit) || FVector::Dist2D(Unit->FootCm, C.Capture->FootCm) > C.CaptureRadiusCm) { continue; }
        if (Holder.IsValid() && Unit->Kind == ELDOpponentUnitKind::Contender && Unit->ContenderId == Holder) { return Unit; }
        if (Owner.IsValid() && Unit->Kind == ELDOpponentUnitKind::Contender && Unit->ContenderId == Owner) { RivalOwner = Unit; }
        if (IsCurrentTarget(C, *Unit)) { Current = Unit; }
        if (!Best || UnitBefore(C, *Unit, *Best)) { Best = Unit; }
    }
    return RivalOwner ? RivalOwner : Current ? Current : Best;
}

// Siege (APP_SPEC 2.2): the visible rival graffiti owner within the acquire radius, wherever he stands; his death clears
// the graffiti. Matched by the authorized ContenderId of a visible unit.
const FLDOpponentUnit* OwnerTarget(const FContext& C)
{
    const FGuid& Owner = C.Frame.Public.Owner;
    if (!Owner.IsValid() || Owner == C.Self.ContenderId) { return nullptr; }
    for (const auto* Unit : C.Enemies)
    {
        if (Unit->Kind == ELDOpponentUnitKind::Contender && Unit->ContenderId == Owner && !Unit->bProtected
            && !HandleSuppressed(C, *Unit) && C.Distance(Unit->FootCm) <= C.AcquireRadiusCm)
        {
            return Unit;
        }
    }
    return nullptr;
}

// Spray, guard or besiege: move to the public zone entry, then fight only rivals inside it. Outside the zone a visible
// rival graffiti owner within reach is besieged on the way. Mobility is spent only when a public deadline makes the
// trip necessary.
bool CircleBody(const FContext& C, bool bUrgent, FLDOpponentBodyIntent& Out)
{
    if (!C.bInsideCircle)
    {
        if (const auto* Owner = OwnerTarget(C))
        {
            Out = EngageBody(C, *Owner);
            return true;
        }
    }
    if (C.bInsideCircle)
    {
        if (const auto* Target = CircleTarget(C))
        {
            Out = EngageBody(C, *Target);
            return true;
        }
        ForgetTarget(C.State);
        FActivationNeed Need;
        Need.bProtect = C.NeedProtection();
        if (auto Activation = PickActivation(C, Need)) { Out = Activation.GetValue(); return true; }
        const auto& Order = C.Frame.Order;
        Out = Order.IsLive() && Order.Kind == ELDOpponentOrderKind::MoveGround && Order.POI == C.Capture->Id
            ? MoveTo(C, *C.Capture) : Halt(C);
        return true;
    }
    const auto Key = FContext::PathKey(C.Capture->Id);
    if (C.Suppressed(Key)) { return false; }
    ForgetTarget(C.State);
    if (!C.CanAttempt(Key))
    {
        Out = Halt(C);
        return true;
    }
    FActivationNeed Need;
    Need.bProtect = C.NeedProtection();
    Need.bCleanseRoot = bUrgent && C.Rooted();
    // The landing and the path are revalidated by the world and the shared motion boundary.
    const FVector Toward = (C.Capture->FootCm - C.Self.FootCm).GetSafeNormal2D();
    if (bUrgent && !Toward.IsNearlyZero()) { Need.DashDirection = Toward; }
    if (auto Activation = PickActivation(C, Need)) { Out = Activation.GetValue(); return true; }
    Out = MoveTo(C, *C.Capture);
    return true;
}

bool ReachedPOI(const FContext& C, FName POI)
{
    const auto& Order = C.Frame.Order;
    return Order.Kind == ELDOpponentOrderKind::MoveGround && Order.POI == POI && Order.Status == ELDOpponentOrderStatus::Completed;
}

const FLDOpponentPOI* PickRetreat(const FContext& C)
{
    FVector Centroid = FVector::ZeroVector;
    for (const auto* Threat : C.Threats) { Centroid += Threat->FootCm; }
    Centroid /= double(C.Threats.Num());
    const FVector Away = (C.Self.FootCm - Centroid).GetSafeNormal2D();
    const auto LeadsAway = [&C, &Away](const FLDOpponentPOI& POI)
    {
        return Away.IsNearlyZero() || FVector::DotProduct((POI.FootCm - C.Self.FootCm).GetSafeNormal2D(), Away) >= 0;
    };
    // Public ground only: never the circle, the abandoned camp, a camp with visible members or the place this body has
    // just reached.
    const auto Usable = [&C](const FLDOpponentPOI& POI)
    {
        if (POI.Kind == ELDOpponentPOIKind::Capture || POI.Id == C.State.VisitPOI
            || C.Suppressed(FContext::PathKey(POI.Id)) || ReachedPOI(C, POI.Id)) { return false; }
        for (const auto& Unit : C.Frame.Units)
        {
            if (Unit.Kind == ELDOpponentUnitKind::CampMember && C.CampOf(Unit) == &POI) { return false; }
        }
        return true;
    };
    // An escape commits to its place while that still leads away; re-ranking near-equal places on every decision
    // would turn it into circling around the pursuer.
    if (const FLDOpponentPOI* Kept = C.State.RetreatPOI.IsNone() ? nullptr : C.FindPOI(C.State.RetreatPOI);
        Kept && Usable(*Kept) && LeadsAway(*Kept))
    {
        return Kept;
    }
    struct FRank
    {
        bool bAway = false;
        bool bBeyondSight = false;
        double Lead = 0;
        double SelfDistance = 0;
    };
    // Leading away first, then ending beyond the nearest threat's sight, then the widest lead over that threat,
    // then nearest, then stable id.
    const auto Before = [](const FRank& A, const FLDOpponentPOI& APOI, const FRank& B, const FLDOpponentPOI& BPOI)
    {
        if (A.bAway != B.bAway) { return A.bAway; }
        if (A.bBeyondSight != B.bBeyondSight) { return A.bBeyondSight; }
        if (A.Lead != B.Lead) { return A.Lead > B.Lead; }
        if (A.SelfDistance != B.SelfDistance) { return A.SelfDistance < B.SelfDistance; }
        return APOI.Id.Compare(BPOI.Id) < 0;
    };
    const FLDOpponentPOI* Best = nullptr;
    FRank BestRank;
    for (const auto& POI : C.Frame.POIs)
    {
        if (!Usable(POI)) { continue; }
        double ThreatDistance = TNumericLimits<double>::Max();
        for (const auto* Threat : C.Threats) { ThreatDistance = FMath::Min(ThreatDistance, FVector::Dist2D(POI.FootCm, Threat->FootCm)); }
        FRank Rank;
        Rank.bAway = LeadsAway(POI);
        Rank.bBeyondSight = ThreatDistance > C.VisionRadiusCm;
        Rank.SelfDistance = C.Distance(POI.FootCm);
        Rank.Lead = ThreatDistance - Rank.SelfDistance;
        if (!Best || Before(Rank, POI, BestRank, *Best))
        {
            Best = &POI;
            BestRank = Rank;
        }
    }
    C.State.RetreatPOI = Best ? Best->Id : NAME_None;
    return Best;
}

FLDOpponentBodyIntent DisengageBody(const FContext& C)
{
    ForgetTarget(C.State);
    const FLDOpponentPOI* Retreat = PickRetreat(C);
    const FLDOpponentUnit* Nearest = nullptr;
    for (const auto* Threat : C.Threats)
    {
        if (!Nearest || C.Distance(Threat->FootCm) < C.Distance(Nearest->FootCm)) { Nearest = Threat; }
    }
    FActivationNeed Need;
    Need.bProtect = true;
    Need.bCleanseRoot = C.Rooted();
    Need.bAllowDamage = false;
    Need.Target = Nearest;
    if (Retreat)
    {
        const FVector Direction = (Retreat->FootCm - C.Self.FootCm).GetSafeNormal2D();
        if (!Direction.IsNearlyZero()) { Need.DashDirection = Direction; }
    }
    if (auto Activation = PickActivation(C, Need)) { return Activation.GetValue(); }
    if (!Retreat) { return Halt(C); }
    if (!C.CanAttempt(FContext::PathKey(Retreat->Id))) { return Halt(C); }
    return MoveTo(C, *Retreat);
}

const FLDOpponentUnit* SelectEngageTarget(const FContext& C)
{
    for (const auto* Unit : C.Enemies)
    {
        if (!IsCurrentTarget(C, *Unit) || Unit->bProtected || HandleSuppressed(C, *Unit)) { continue; }
        if (C.Now - C.State.TargetEngagedAtMs <= C.AI.EngagementBudgetMs) { return Unit; }
        // Budget spent on this visibility interval: stop chasing without inventing a stronger target.
        C.State.Failures.Suppress(FContext::EngagementKey(NAME_None, Unit->Handle), C.Print(ELDOpponentFailure::Engagement));
        ForgetTarget(C.State);
        break;
    }
    const FLDOpponentUnit* Best = nullptr;
    for (const auto* Unit : C.Enemies)
    {
        if (Unit->bProtected || C.Distance(Unit->FootCm) > C.AcquireRadiusCm || HandleSuppressed(C, *Unit)) { continue; }
        if (!Best || UnitBefore(C, *Unit, *Best)) { Best = Unit; }
    }
    return Best;
}

TArray<const FLDOpponentUnit*> CampMembers(const FContext& C, const FLDOpponentPOI& Camp)
{
    TArray<const FLDOpponentUnit*> Members;
    for (const auto& Unit : C.Frame.Units)
    {
        if (Unit.Kind == ELDOpponentUnitKind::CampMember && C.CampOf(Unit) == &Camp) { Members.Add(&Unit); }
    }
    return Members;
}

const FLDOpponentPOI* SelectCamp(const FContext& C)
{
    const auto& Self = C.Self;
    // The opening pick is part of the starting build; growth means a level gained.
    const bool bGrowth = Self.Level > 1;
    const bool bHighEntry = bGrowth && RatioAtLeast(Self.HpP2, Self.MaxHpP2, C.AI.HighCampEntryHPBp);
    FLDCampCostInput Input;
    Input.CurrentHpP2 = Self.HpP2;
    Input.MaxHpP2 = Self.MaxHpP2;
    Input.Attributes = Self.Attributes;
    Input.BasicReadyInMs = FMath::Max<int64>(0, Self.BasicReadyAtMs - C.Now);
    Input.EngagementBudgetMs = C.AI.EngagementBudgetMs;
    TMap<FString, bool> Feasible;
    const FLDOpponentPOI* Best = nullptr;
    bool bBestVisible = false;
    for (const auto& POI : C.Frame.POIs)
    {
        // #97: the Elite tier takes the High tier's entry and reserve rules, with its own (higher) cost estimate.
        if (POI.Kind != ELDOpponentPOIKind::Camp || (POI.Tier != ELDDemoCampTier::Low && !bHighEntry)
            || !C.CanAttempt(FContext::PathKey(POI.Id)) || C.Suppressed(FContext::EngagementKey(POI.Id, FGuid()))
            || C.RecentlyVisited(POI.Id)) { continue; }
        if (!Feasible.Contains(POI.MonsterId))
        {
            const FLDDemoMonsterDefinition* Monster = nullptr;
            FString Error;
            bool bOk = false;
            // #97: a living camp spawned no later than now, so the current public stage bounds its growth from above.
            FLDDemoMonsterDefinition Grown;
            if (C.Catalog.FindMonster(POI.MonsterId, Monster, Error) && Monster
                && LDCampGrowth::ScaleBody(C.Catalog.Get().Parameters, *Monster,
                    LDCampGrowth::Stage(C.Catalog.Get().Parameters, C.Now - C.Frame.Public.StartedAtMs), Grown, Error))
            {
                const auto Estimate = LDOpponentPolicy::EstimateCampCost(Input, Grown, C.Catalog);
                const int64 Reserve = Monster->Tier == ELDDemoCampTier::Low ? C.AI.LowCampReserveHPBp : C.AI.HighCampReserveHPBp;
                bOk = Estimate.Status == ELDCampCostStatus::Ok && RatioAtLeast(Estimate.RemainingHpP2, Self.MaxHpP2, Reserve);
            }
            Feasible.Add(POI.MonsterId, bOk);
        }
        if (!Feasible.FindChecked(POI.MonsterId)) { continue; }
        bool bVisible = false;
        for (const auto* Member : CampMembers(C, POI)) { bVisible |= !Member->bReturning; }
        // Visible attackable members first, then the least recently visited, nearest, stable id.
        bool bBetter = !Best;
        if (Best)
        {
            const int64 Visited = C.VisitedAt(POI.Id), BestVisited = C.VisitedAt(Best->Id);
            const double Distance = C.Distance(POI.FootCm), BestDistance = C.Distance(Best->FootCm);
            bBetter = bVisible != bBestVisible ? bVisible
                : Visited != BestVisited ? Visited < BestVisited
                : Distance != BestDistance ? Distance < BestDistance
                : POI.Id.Compare(Best->Id) < 0;
        }
        if (bBetter)
        {
            Best = &POI;
            bBestVisible = bVisible;
        }
    }
    return Best;
}

const FLDOpponentUnit* PickCampTarget(const FContext& C, const TArray<const FLDOpponentUnit*>& Members, bool& bAbandon)
{
    bAbandon = false;
    const FLDOpponentUnit* Best = nullptr;
    for (const auto* Member : Members)
    {
        // A member lost under this build is not re-picked while its visibility interval lasts.
        if (HandleSuppressed(C, *Member)) { continue; }
        const bool bReachable = !Member->bReturning || C.Distance(Member->FootCm) <= C.AttackRangeCm;
        if (IsCurrentTarget(C, *Member))
        {
            if (bReachable) { return Member; }
            // A returning target outside current reach ends the visit; no chase toward its home.
            bAbandon = true;
            return nullptr;
        }
        if (bReachable && (!Best || UnitBefore(C, *Member, *Best))) { Best = Member; }
    }
    return Best;
}

bool FarmBody(const FContext& C, FLDOpponentBodyIntent& Out)
{
    auto& State = C.State;
    const FLDOpponentPOI* Camp = State.VisitPOI.IsNone() ? nullptr : C.FindPOI(State.VisitPOI);
    if (Camp && State.VisitEngagedAtMs != 0 && C.Now - State.VisitEngagedAtMs > C.AI.EngagementBudgetMs)
    {
        State.Failures.Suppress(FContext::EngagementKey(Camp->Id, FGuid()), C.Print(ELDOpponentFailure::Engagement));
        C.RecordVisit(Camp->Id);
        EndVisit(State);
        Camp = nullptr;
    }
    if (Camp && C.Suppressed(FContext::PathKey(Camp->Id)))
    {
        EndVisit(State);
        Camp = nullptr;
    }
    for (int32 Attempt = 0; Attempt < 2; ++Attempt)
    {
        if (!Camp)
        {
            Camp = SelectCamp(C);
            if (!Camp) { return false; }
            EndVisit(State);
            State.VisitPOI = Camp->Id;
        }
        bool bAbandon = false;
        if (const FLDOpponentUnit* Target = PickCampTarget(C, CampMembers(C, *Camp), bAbandon))
        {
            if (State.VisitEngagedAtMs == 0) { State.VisitEngagedAtMs = C.Now; }
            Out = EngageBody(C, *Target);
            return true;
        }
        const auto& Order = C.Frame.Order;
        const bool bArrived = Order.Kind == ELDOpponentOrderKind::MoveGround && Order.POI == Camp->Id
            && Order.Status == ELDOpponentOrderStatus::Completed;
        if (!bAbandon && !bArrived)
        {
            ForgetTarget(State);
            Out = C.CanAttempt(FContext::PathKey(Camp->Id)) ? MoveTo(C, *Camp) : Halt(C);
            return true;
        }
        // Arrival without an attackable member is only "nothing visible now", never "camp cleared".
        C.RecordVisit(Camp->Id);
        EndVisit(State);
        Camp = nullptr;
    }
    Out = Halt(C);
    return true;
}

bool ExploreBody(const FContext& C, FLDOpponentBodyIntent& Out)
{
    auto& State = C.State;
    if (!State.ExplorePOI.IsNone())
    {
        const auto& Order = C.Frame.Order;
        if (Order.Kind == ELDOpponentOrderKind::MoveGround && Order.POI == State.ExplorePOI
            && Order.Status == ELDOpponentOrderStatus::Completed)
        {
            C.RecordVisit(State.ExplorePOI);
            State.ExplorePOI = NAME_None;
        }
    }
    const FLDOpponentPOI* Target = State.ExplorePOI.IsNone() ? nullptr : C.FindPOI(State.ExplorePOI);
    if (Target && !C.CanAttempt(FContext::PathKey(Target->Id))) { Target = nullptr; }
    if (!Target)
    {
        for (const auto& POI : C.Frame.POIs)
        {
            if (POI.Kind == ELDOpponentPOIKind::Capture || !C.CanAttempt(FContext::PathKey(POI.Id))) { continue; }
            // Least recently visited public place first, then nearest, then stable id.
            bool bBetter = !Target;
            if (Target)
            {
                const int64 Visited = C.VisitedAt(POI.Id), TargetVisited = C.VisitedAt(Target->Id);
                const double Distance = C.Distance(POI.FootCm), TargetDistance = C.Distance(Target->FootCm);
                bBetter = Visited != TargetVisited ? Visited < TargetVisited
                    : Distance != TargetDistance ? Distance < TargetDistance
                    : POI.Id.Compare(Target->Id) < 0;
            }
            if (bBetter) { Target = &POI; }
        }
    }
    if (!Target)
    {
        State.ExplorePOI = NAME_None;
        return false;
    }
    State.ExplorePOI = Target->Id;
    Out = MoveTo(C, *Target);
    return true;
}

// Where the match is for a build command, as the growth domain judges it at commit (LDProgression::CommandGate).
LDProgression::FBuildContext BuildContext(const FLDOpponentDecisionFrame& Frame)
{
    LDProgression::FBuildContext Context;
    const ELDMatchPhase Phase = Frame.Public.Phase;
    Context.Stage = Phase == ELDMatchPhase::Countdown ? LDProgression::EMatchStage::Countdown
        : Phase == ELDMatchPhase::Development || Phase == ELDMatchPhase::CaptureOpen ? LDProgression::EMatchStage::Live
        : LDProgression::EMatchStage::Closed;
    Context.LifePhase = Frame.Self.Phase;
    Context.bSafe = Frame.Self.BuildSafeAtMs <= Frame.Public.NowMs;
    return Context;
}

FLDOpponentRetryKey BuildKey(ELDOpponentBuildKind Kind)
{
    FLDOpponentRetryKey Key;
    Key.Category = ELDOpponentFailure::Build;
    Key.Slot = uint8(Kind);
    return Key;
}

// §6.4 build order: the current offer's choice first, then a blueprint into an empty slot, then the unspent points. Each is
// sent only when the growth domain would admit it now; one build request at a time.
bool DecideBuild(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog, FLDOpponentPolicyState& State,
    bool bChooseOffer, FLDOpponentBuildIntent& Out)
{
    const FLDOpponentSelf& Self = Frame.Self;
    if (Frame.bBuildChangeInFlight) { return false; }
    const int64 Now = Frame.Public.NowMs;
    const uint64 Print = LDOpponentPolicy::Fingerprint(ELDOpponentFailure::Build, Self);
    const LDProgression::FBuildContext Context = BuildContext(Frame);
    FString Error;
    if (bChooseOffer && Self.Offer.IsSet()
        && LDProgression::CommandGate(ELDBuildCommandKind::ActiveChoice, Context) == ELDGrowthReason::None)
    {
        FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::ChooseActive);
        Key.Target = Self.Offer->OfferId;
        if (State.Failures.CanAttempt(Key, Print, Now) && LDOpponentPolicy::SelectActive(Self, Catalog, Out.Option, Error))
        {
            Out.Kind = ELDOpponentBuildKind::ChooseActive;
            Out.OfferId = Self.Offer->OfferId;
            Out.OfferRevision = Self.Offer->OfferRevision;
            Out.Key = Key;
            return true;
        }
    }
    if (!Self.Blueprints.IsEmpty()
        && LDProgression::CommandGate(ELDBuildCommandKind::LearnBlueprint, Context) == ELDGrowthReason::None)
    {
        FString Blueprint;
        uint8 Slot = 0;
        if (LDOpponentPolicy::SelectBlueprint(Self, Catalog, Blueprint, Slot, Error))
        {
            FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::LearnBlueprint);
            Key.POI = FName(*Blueprint);
            if (State.Failures.CanAttempt(Key, Print, Now))
            {
                Out = FLDOpponentBuildIntent();
                Out.Kind = ELDOpponentBuildKind::LearnBlueprint;
                Out.Blueprint = Blueprint;
                Out.Slot = Slot;
                Out.Key = Key;
                return true;
            }
        }
    }
    if (Self.UnspentPoints > 0
        && LDProgression::CommandGate(ELDBuildCommandKind::AllocatePoints, Context) == ELDGrowthReason::None)
    {
        const FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::AllocatePoints);
        TArray<uint8> Counts;
        if (State.Failures.CanAttempt(Key, Print, Now) && LDOpponentPolicy::AllocatePoints(Self, Catalog, Counts, Error))
        {
            Out = FLDOpponentBuildIntent();
            Out.Kind = ELDOpponentBuildKind::AllocatePoints;
            Out.PointCounts = MoveTemp(Counts);
            Out.Key = Key;
            return true;
        }
    }
    Out = FLDOpponentBuildIntent();
    return false;
}

// The stable rank of a target mode in the fixed §6.4 preference; lower first.
int32 TargetModePreference(ELDDemoTargetMode Mode)
{
    switch (Mode)
    {
    case ELDDemoTargetMode::Unit: return 0;
    case ELDDemoTargetMode::GroundPoint: return 1;
    case ELDDemoTargetMode::DirectionCone: return 2;
    case ELDDemoTargetMode::DirectionProjectile: return 3;
    case ELDDemoTargetMode::Self: return 4;
    case ELDDemoTargetMode::DirectionDash: return 5;
    default: return 6;
    }
}

// Picks the preferred of Candidates (active ids) against the roles the learned actives and blueprints already cover.
bool PreferActive(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, const TArray<FString>& Candidates,
    FString& Out, FString& Error)
{
    const auto& Content = Catalog.Get();
    bool bEngageCovered = false;
    bool bProtectCovered = false;
    const auto Cover = [&](const FString& Id)
    {
        const FLDDemoAbilityDefinition* Ability = nullptr;
        if (!Catalog.FindAbility(Id, Ability, Error) || !Ability) { return false; }
        const FLDOpponentUsage Usage = LDOpponentPolicy::AbilityUsage(*Ability);
        bEngageCovered |= Usage.bEngage;
        bProtectCovered |= Usage.bSelfProtect;
        return true;
    };
    for (const FLDOpponentActive& Active : Self.Actives) { if (!Cover(Active.DefinitionId)) { return false; } }
    for (const FString& Blueprint : Self.Blueprints) { if (!Cover(Blueprint)) { return false; } }
    bool bFound = false;
    bool bBestGain = false;
    int32 BestMode = 0, BestOrder = 0;
    for (const FString& Id : Candidates)
    {
        const FLDDemoAbilityDefinition* Ability = nullptr;
        if (!Catalog.FindAbility(Id, Ability, Error) || !Ability) { return false; }
        const FLDOpponentUsage Usage = LDOpponentPolicy::AbilityUsage(*Ability);
        const bool bGain = (Usage.bEngage && !bEngageCovered) || (Usage.bSelfProtect && !bProtectCovered);
        const int32 Mode = TargetModePreference(Ability->Target.Mode);
        const int32 Order = CatalogIndex(Content.Abilities, Id);
        if (!bFound || (bGain != bBestGain ? bGain : Mode != BestMode ? Mode < BestMode : Order < BestOrder))
        {
            bFound = true;
            bBestGain = bGain;
            BestMode = Mode;
            BestOrder = Order;
            Out = Id;
        }
    }
    if (!bFound) { Error = TEXT("No candidate active to prefer."); }
    return bFound;
}
}

const FLDOpponentRetryLedger::FEntry* FLDOpponentRetryLedger::Find(const FLDOpponentRetryKey& Key) const
{
    return Entries.FindByPredicate([&Key](const FEntry& Entry) { return Entry.Key == Key; });
}

FLDOpponentRetryLedger::FEntry& FLDOpponentRetryLedger::FindOrAdd(const FLDOpponentRetryKey& Key, uint64 Fingerprint)
{
    if (FEntry* Existing = Entries.FindByPredicate([&Key](const FEntry& Entry) { return Entry.Key == Key; }))
    {
        if (Existing->Fingerprint != Fingerprint)
        {
            *Existing = FEntry();
            Existing->Key = Key;
            Existing->Fingerprint = Fingerprint;
            Existing->Order = NextOrder++;
        }
        return *Existing;
    }
    if (Entries.Num() >= Capacity)
    {
        int32 Oldest = 0;
        for (int32 Index = 1; Index < Entries.Num(); ++Index)
        {
            if (Entries[Index].Order < Entries[Oldest].Order) { Oldest = Index; }
        }
        Entries.RemoveAtSwap(Oldest, EAllowShrinking::No);
    }
    FEntry& Added = Entries.AddDefaulted_GetRef();
    Added.Key = Key;
    Added.Fingerprint = Fingerprint;
    Added.Order = NextOrder++;
    return Added;
}

bool FLDOpponentRetryLedger::CanAttempt(const FLDOpponentRetryKey& Key, uint64 Fingerprint, int64 NowMs) const
{
    const FEntry* Entry = Find(Key);
    return !Entry || Entry->Fingerprint != Fingerprint || (!Entry->bSuppressed && NowMs >= Entry->RetryAtMs);
}

bool FLDOpponentRetryLedger::IsSuppressed(const FLDOpponentRetryKey& Key, uint64 Fingerprint) const
{
    const FEntry* Entry = Find(Key);
    return Entry && Entry->Fingerprint == Fingerprint && Entry->bSuppressed;
}

void FLDOpponentRetryLedger::RecordFailure(const FLDOpponentRetryKey& Key, uint64 Fingerprint, int64 NowMs,
    const FLDDemoAIParameters& AI)
{
    FEntry& Entry = FindOrAdd(Key, Fingerprint);
    if (Entry.bSuppressed) { return; }
    ++Entry.Attempts;
    if (Entry.Attempts >= FMath::Max<int64>(1, AI.RetryMaxAttempts))
    {
        Entry.bSuppressed = true;
        return;
    }
    const int64 Cap = FMath::Max<int64>(0, AI.RetryMaxBackoffMs);
    int64 Wait = FMath::Clamp<int64>(AI.RetryInitialBackoffMs, 0, Cap);
    for (int64 Step = 1; Step < Entry.Attempts && Wait < Cap; ++Step) { Wait = Wait > Cap / 2 ? Cap : Wait * 2; }
    Entry.RetryAtMs = LDOpponentPolicy::Detail::SaturatingAdd(NowMs, Wait);
}

void FLDOpponentRetryLedger::Suppress(const FLDOpponentRetryKey& Key, uint64 Fingerprint)
{
    FindOrAdd(Key, Fingerprint).bSuppressed = true;
}

void FLDOpponentRetryLedger::RecordSuccess(const FLDOpponentRetryKey& Key)
{
    Entries.RemoveAll([&Key](const FEntry& Entry) { return Entry.Key == Key; });
}

namespace LDOpponentPolicy
{
int64 RetaliationHits(int64 RetaliateFromMs, int64 DeathMs, int64 IntervalMs, int64 WindupMs)
{
    if (IntervalMs <= 0 || WindupMs < 0 || RetaliateFromMs > DeathMs || DeathMs - RetaliateFromMs < WindupMs) { return 0; }
    return (DeathMs - RetaliateFromMs - WindupMs) / IntervalMs + 1;
}

FLDCampCostEstimate EstimateCampCost(const FLDCampCostInput& Input, const FLDDemoMonsterDefinition& Monster,
    const FLDDemoContentSnapshot& Catalog)
{
    FLDCampCostEstimate Out;
    const auto Unavailable = [](const FString& Message)
    {
        FLDCampCostEstimate Failed;
        Failed.Status = ELDCampCostStatus::EstimateUnavailable;
        Failed.Error = Message;
        return Failed;
    };
    const auto& Content = Catalog.Get();
    if (Input.MaxHpP2 <= 0 || Input.CurrentHpP2 <= 0 || Input.CurrentHpP2 > Input.MaxHpP2 || Input.BasicReadyInMs < 0
        || Input.EngagementBudgetMs <= 0)
    {
        return Unavailable(TEXT("Camp cost needs a live HP pool and a positive engagement budget."));
    }
    if (Monster.CountPerCamp <= 0 || Monster.HPP2 <= 0 || Monster.AttackP2 < 0 || Monster.AttackIntervalMs <= 0
        || Monster.AttackWindupMs <= 0 || Monster.AttackWindupMs >= Monster.AttackIntervalMs)
    {
        return Unavailable(TEXT("The public camp template has no valid member count, health or attack timing."));
    }
    FLDBasicTimingInput Timing;
    Timing.BaseAttackSpeed = Content.Parameters.AttackSpeed.Base;
    Timing.CurrentAttackSpeed = Input.Attributes.AttackSpeed;
    Timing.BaseWindupMs = Content.Parameters.BasicAttackWindupMs;
    const auto Basic = LDTactical::EvaluateBasicTiming(Timing, Catalog);
    if (!Basic.IsSuccess()) { return Unavailable(Basic.Error.Message); }

    FLDDamageEvaluationInput Dealt;
    Dealt.Definition.Emplace(0, Content.Character.BasicADRatioBp, 0, Content.Character.BasicDamageType);
    Dealt.HitClass = ELDCombatHitClass::BasicPrimary;
    Dealt.bCritical = false;
    Dealt.SourceADP2 = Input.Attributes.ADP2;
    Dealt.TargetResistanceP2 = Content.Character.BasicDamageType == ELDDemoDamageType::Physical ? Monster.ArmorP2 : Monster.MagicResistP2;
    Dealt.TargetHPP2 = Monster.HPP2;
    Dealt.TargetShieldP2 = int64(0);
    Dealt.bTargetProtected = false;
    const auto PerHit = LDTactical::EvaluateDamage(Dealt, Catalog);
    if (!PerHit.IsSuccess()) { return Unavailable(PerHit.Error.Message); }

    // Members strike once per public attack template: the attack points as a physical basic hit.
    FLDDamageEvaluationInput Taken;
    Taken.Definition.Emplace(Monster.AttackP2, 0, 0, ELDDemoDamageType::Physical);
    Taken.HitClass = ELDCombatHitClass::BasicPrimary;
    Taken.bCritical = false;
    Taken.TargetResistanceP2 = Input.Attributes.ArmorP2;
    Taken.TargetHPP2 = Input.CurrentHpP2;
    Taken.TargetShieldP2 = int64(0);
    Taken.bTargetProtected = false;
    const auto PerRetaliation = LDTactical::EvaluateDamage(Taken, Catalog);
    if (!PerRetaliation.IsSuccess()) { return Unavailable(PerRetaliation.Error.Message); }

    const int64 Damage = PerHit.Value->DamageP2;
    if (Damage <= 0)
    {
        Out.Status = ELDCampCostStatus::NotFeasible;
        Out.Error = TEXT("A basic hit deals no effective damage to this camp template.");
        return Out;
    }
    const int64 Interval = Basic.Value->IntervalMs;
    const int64 FirstHitMs = Input.BasicReadyInMs + Basic.Value->WindupMs;
    const int64 HitsPerMember = (Monster.HPP2 - 1) / Damage + 1;
    if (FirstHitMs > Input.EngagementBudgetMs || HitsPerMember > MAX_int64 / Monster.CountPerCamp
        || HitsPerMember * Monster.CountPerCamp - 1 > (Input.EngagementBudgetMs - FirstHitMs) / Interval)
    {
        Out.Status = ELDCampCostStatus::NotFeasible;
        Out.Error = TEXT("Clearing this camp template exceeds the engagement budget.");
        return Out;
    }
    Out.PlayerHits = HitsPerMember * Monster.CountPerCamp;
    Out.ClearMs = FirstHitMs + (Out.PlayerHits - 1) * Interval;
    for (int64 Member = 0; Member < Monster.CountPerCamp; ++Member)
    {
        const int64 DeathMs = FirstHitMs + ((Member + 1) * HitsPerMember - 1) * Interval;
        Out.RetaliationHits += RetaliationHits(FirstHitMs, DeathMs, Monster.AttackIntervalMs, Monster.AttackWindupMs);
    }
    const int64 Received = PerRetaliation.Value->DamageP2;
    Out.DamageTakenP2 = Received > 0 && Out.RetaliationHits > MAX_int64 / Received ? MAX_int64 : Out.RetaliationHits * Received;
    Out.RemainingHpP2 = FMath::Max<int64>(0, Input.CurrentHpP2 - Out.DamageTakenP2);
    Out.Status = Out.RemainingHpP2 > 0 ? ELDCampCostStatus::Ok : ELDCampCostStatus::NotFeasible;
    if (Out.Status == ELDCampCostStatus::NotFeasible) { Out.Error = TEXT("The template's retaliation exhausts current HP."); }
    return Out;
}

FLDOpponentUsage AbilityUsage(const FLDDemoAbilityDefinition& Ability)
{
    using namespace Detail;
    FLDOpponentUsage Usage;
    const bool bSelfTarget = Ability.Target.Mode == ELDDemoTargetMode::Self;
    for (const auto& Effect : Ability.Values.Effects) { ClassifyEffect(Effect, bSelfTarget, Usage); }
    return Usage;
}

bool SelectActive(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FString& Out, FString& Error)
{
    using namespace Detail;
    Out.Reset();
    if (!Self.Offer.IsSet() || Self.Offer->Options.IsEmpty())
    {
        Error = TEXT("No current active offer.");
        return false;
    }
    return PreferActive(Self, Catalog, Self.Offer->Options, Out, Error);
}

bool SelectBlueprint(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FString& OutBlueprint, uint8& OutSlot,
    FString& Error)
{
    using namespace Detail;
    OutBlueprint.Reset();
    OutSlot = 0;
    bool bTaken[3] = {false, false, false};
    for (const FLDOpponentActive& Active : Self.Actives) { if (Active.Slot < 3) { bTaken[Active.Slot] = true; } }
    while (OutSlot < 3 && bTaken[OutSlot]) { ++OutSlot; }
    if (OutSlot >= 3)
    {
        Error = TEXT("Q, W and E are taken; a blueprint waits for the rest-only replacement.");
        return false;
    }
    if (Self.Blueprints.IsEmpty())
    {
        Error = TEXT("No pending blueprint.");
        return false;
    }
    return PreferActive(Self, Catalog, Self.Blueprints, OutBlueprint, Error);
}

bool AllocatePoints(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, TArray<uint8>& Out, FString& Error)
{
    using namespace Detail;
    Out.Reset();
    const auto& Content = Catalog.Get();
    if (Self.UnspentPoints <= 0)
    {
        Error = TEXT("No unspent points.");
        return false;
    }
    if (Self.StatPoints.Num() != Content.Stats.Num())
    {
        Error = TEXT("The own stat points do not list every catalog stat.");
        return false;
    }
    // Strength serves a body that scales with AD (any learned active or the basic attack), brains one that scales with AP.
    bool bAD = Content.Character.BasicADRatioBp > 0;
    bool bAP = false;
    for (const FLDOpponentActive& Active : Self.Actives)
    {
        const FLDDemoAbilityDefinition* Ability = nullptr;
        if (!Catalog.FindAbility(Active.DefinitionId, Ability, Error) || !Ability) { return false; }
        for (const FLDDemoEffectDefinition& Effect : Ability->Values.Effects)
        {
            bAD |= Effect.ADRatioBp > 0;
            bAP |= Effect.APRatioBp > 0;
        }
    }
    TArray<int64> Points;
    TArray<bool> Relevant;
    const TArray<const FLDDemoStatDefinition*> StatOrder = Content.StatsInDesignOrder();
    for (int32 Index = 0; Index < StatOrder.Num(); ++Index)
    {
        const FLDDemoStatDefinition& Stat = *StatOrder[Index];
        if (!Self.StatPoints[Index].StatId.Equals(Stat.Id, ESearchCase::CaseSensitive))
        {
            Error = TEXT("The own stat points are not in the stats' design order.");
            return false;
        }
        Points.Add(Self.StatPoints[Index].Points);
        const ELDDemoStatId Id = Stat.GetNativeId();
        Relevant.Add(Id == ELDDemoStatId::StatPhysique || Id == ELDDemoStatId::StatThickSkin
            || (Id == ELDDemoStatId::StatStrength && bAD) || (Id == ELDDemoStatId::StatBrains && bAP));
    }
    Out.SetNumZeroed(Content.Stats.Num());
    for (int64 Left = Self.UnspentPoints; Left > 0; --Left)
    {
        int32 Best = INDEX_NONE;
        for (int32 Index = 0; Index < Points.Num(); ++Index)
        {
            if (!Relevant[Index] || Out[Index] == MAX_uint8) { continue; }
            if (Best == INDEX_NONE || Points[Index] < Points[Best]) { Best = Index; }
        }
        if (Best == INDEX_NONE) { break; }
        ++Points[Best];
        ++Out[Best];
    }
    if (!Out.ContainsByPredicate([](uint8 Count) { return Count > 0; }))
    {
        Error = TEXT("No relevant catalog stat takes a point.");
        Out.Reset();
        return false;
    }
    return true;
}

bool IsCaptureDefenseUrgent(const FLDCaptureUrgencyInput& Input)
{
    using namespace Detail;
    if (Input.DueAtMs <= Input.NowMs) { return true; }
    const int64 Remaining = Input.DueAtMs - Input.NowMs;
    int64 Needed = FMath::Max<int64>(0, Input.ReactionBudgetMs);
    Needed = SaturatingAdd(Needed, FMath::Max<int64>(0, Input.MoveBlockedUntilMs - Input.NowMs));
    if (!Input.bInside)
    {
        const double Speed = ToDouble(Input.MoveSpeedCmPerSec);
        if (!Input.PathCm.IsSet() || !FMath::IsFinite(Input.PathCm.GetValue()) || Input.PathCm.GetValue() < 0
            || !FMath::IsFinite(Speed) || !(Speed > 0))
        {
            return true;
        }
        const double Travel = FMath::CeilToDouble(Input.PathCm.GetValue() * 1000. / Speed);
        if (!(Travel < double(Remaining))) { return true; }
        Needed = SaturatingAdd(Needed, int64(Travel));
    }
    return Needed >= Remaining;
}

uint64 Fingerprint(ELDOpponentFailure Category, const FLDOpponentSelf& Self)
{
    switch (Category)
    {
    case ELDOpponentFailure::Engagement: return Self.BuildRevision;
    // A build step is retried afresh once the build or the offer changed.
    case ELDOpponentFailure::Build: return Self.BuildRevision * 1000003ull + (Self.Offer.IsSet() ? Self.Offer->OfferRevision : 0);
    case ELDOpponentFailure::Walk:
    {
        // The standing point to the centimetre, in this life: any move or a respawn gives the body's walking a fresh budget.
        const FIntVector Spot(FMath::RoundToInt(Self.FootCm.X), FMath::RoundToInt(Self.FootCm.Y), FMath::RoundToInt(Self.FootCm.Z));
        return (uint64(Self.Life) << 32) | uint64(GetTypeHash(Spot));
    }
    default: return Self.Life;
    }
}

bool InsideCircle(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog)
{
    const FLDOpponentPOI* Capture = Frame.POIs.FindByPredicate(
        [](const FLDOpponentPOI& POI) { return POI.Kind == ELDOpponentPOIKind::Capture; });
    return Capture && FVector::Dist2D(Frame.Self.FootCm, Capture->FootCm) <= Detail::ToDouble(Catalog.Get().Parameters.CaptureRadiusCm);
}

FLDOpponentDecision Decide(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
    FLDOpponentPolicyState& State)
{
    using namespace Detail;
    const auto& Self = Frame.Self;
    if (State.Generation != Frame.Generation)
    {
        State = FLDOpponentPolicyState();
        State.Generation = Frame.Generation;
    }
    if (State.Life != Self.Life)
    {
        State.Life = Self.Life;
        ForgetBody(State);
    }
    FLDOpponentDecision Out;
    const auto Finish = [&Out, &State](EMode Mode, EReason Reason)
    {
        Out.Mode = Mode;
        Out.Reason = Reason;
        State.Mode = Mode;
        return Out;
    };
    const bool bRunning = Frame.Public.Phase == ELDMatchPhase::Development || Frame.Public.Phase == ELDMatchPhase::CaptureOpen;
    // #153: the opening countdown admits only build steps, from this adapter's drawn moment on; the body never moves.
    const bool bCountdown = Frame.Public.Phase == ELDMatchPhase::Countdown;
    if (!Frame.Generation || !(bRunning || bCountdown) || !Self.ContenderId.IsValid()
        || (Self.Phase != ELDLifePhase::Alive && Self.Phase != ELDLifePhase::Dead))
    {
        ForgetBody(State);
        return Finish(EMode::Stopped, EReason::Stopped);
    }
    if (bCountdown)
    {
        ForgetBody(State);
        if (Frame.Public.NowMs >= Self.OpeningPickAtMs && DecideBuild(Frame, Catalog, State, true, Out.Build))
        {
            return Finish(EMode::Choose, EReason::ChooseOffer);
        }
        return Finish(EMode::Stopped, EReason::Stopped);
    }
    FContext C(Frame, Catalog, State);
    if (!C.Initialize())
    {
        ForgetBody(State);
        return Finish(EMode::Stopped, EReason::ProviderUnavailable);
    }
    const bool bBuilt = DecideBuild(Frame, Catalog, State, true, Out.Build);
    if (Self.Phase == ELDLifePhase::Dead)
    {
        ForgetBody(State);
        return Finish(EMode::DeadWaiting, bBuilt ? EReason::ChooseOffer : EReason::DeadWaiting);
    }
    // While an accepted action owns the body, nothing new is sent that could cancel its windup.
    const bool bActionLive = Self.bChanneling || (Frame.Order.IsLive() && Frame.Order.Kind == ELDOpponentOrderKind::Action);

    const bool bWasRecovering = State.bRecovering;
    // D21: a recovering body that reached a dorm stays until full instead of leaving at the resume line.
    State.bRecovering = bWasRecovering
        ? !RatioAtLeast(Self.HpP2, Self.MaxHpP2, C.DormAt() ? 10000 : C.AI.ResumeFarmHPBp)
        : !RatioAtLeast(Self.HpP2, Self.MaxHpP2, C.AI.RetreatHPBp);
    if (State.bRecovering && !bWasRecovering)
    {
        // Stop-loss ends this investment; a camp abandoned under this build is not retried at once.
        if (!State.VisitPOI.IsNone() && State.VisitEngagedAtMs != 0)
        {
            State.Failures.Suppress(FContext::EngagementKey(State.VisitPOI, FGuid()), C.Print(ELDOpponentFailure::Engagement));
            C.RecordVisit(State.VisitPOI);
        }
        EndVisit(State);
    }
    // D26 (#160): an escape from the Boss lasts while its walk to the retreat place is live and the Boss is in sight.
    if (State.bEscapingBoss)
    {
        const auto& Order = Frame.Order;
        State.bEscapingBoss = C.Boss && !State.RetreatPOI.IsNone() && Order.IsLive()
            && Order.Kind == ELDOpponentOrderKind::MoveGround && Order.POI == State.RetreatPOI;
    }
    if (!State.bRecovering && !State.bEscapingBoss) { State.RetreatPOI = NAME_None; }
    const auto Body = [&Out, bActionLive](const FLDOpponentBodyIntent& Intent)
    {
        Out.Body = Intent;
        if (bActionLive) { Out.Body = FLDOpponentBodyIntent(); Out.Body.Kind = EBody::Keep; }
    };

    // A contest only pauses the spray holder's progress and leaving, dying or a hard control clears it, so a paused holder
    // keeps holding and fights inside the zone like a sole sprayer; only the stop-loss line below lets it go.
    const bool bHolder = C.bCaptureOpen && (Frame.Public.Circle == ELDOpponentCircle::Solo || Frame.Public.Circle == ELDOpponentCircle::Contested)
        && Frame.Public.Occupier == Self.ContenderId;
    const bool bHoldingSolo = bHolder && Frame.Public.Circle == ELDOpponentCircle::Solo;
    const bool bHoldingPaused = bHolder && Frame.Public.Circle == ELDOpponentCircle::Contested;
    // The graffiti owner guards the zone (standing in it blocks every sprayer) but his death clears the graffiti, so he
    // never fights below the stop-loss line. Every other body answers a rival graffiti: besiege its owner or repaint.
    const bool bOwner = C.bCaptureOpen && Frame.Public.Owner.IsValid() && Frame.Public.Owner == Self.ContenderId;
    const bool bRivalGraffiti = C.bCaptureOpen && Frame.Public.Owner.IsValid() && !bOwner;
    bool bUrgent = false;
    const int64 PublicDueMs = Frame.Public.Circle == ELDOpponentCircle::Solo && !bHoldingSolo ? Frame.Public.DueAtMs
        : bRivalGraffiti ? Frame.Public.GuardDueAtMs : 0;
    if (C.bCaptureOpen && PublicDueMs > 0)
    {
        FLDCaptureUrgencyInput Urgency;
        Urgency.NowMs = C.Now;
        Urgency.DueAtMs = PublicDueMs;
        Urgency.bInside = C.bInsideCircle;
        Urgency.PathCm = Frame.CapturePathCm;
        Urgency.MoveSpeedCmPerSec = Self.Attributes.MoveSpeedCmPerSec;
        Urgency.MoveBlockedUntilMs = Self.MoveBlockedUntilMs;
        Urgency.ReactionBudgetMs = C.AI.CaptureReactionBudgetMs;
        bUrgent = IsCaptureDefenseUrgent(Urgency);
    }
    // §6.2: recovering still answers an urgent public defence; only below the stop-loss line without ready self-protection
    // does escaping come first, and such a body does not charge the holder. §6.1 P2: a body already contesting inside the
    // circle stays and fights the rivals there instead of running out once the hold is broken; under D12 tagging the circle
    // and fleeing would only let the holder resume its paused progress.
    const bool bStopLoss = State.bRecovering && !RatioAtLeast(Self.HpP2, Self.MaxHpP2, C.AI.RetreatHPBp) && !ProtectionReady(C);
    const bool bContesting = C.bCaptureOpen && C.bInsideCircle && Frame.Public.Circle == ELDOpponentCircle::Contested;
    // §6.5 bounded investment: a contest of the circle that ends in a stop-loss exit is one failed attempt on the capture
    // under this build. After RetryPolicy.MaxAttempts the body stops answering the circle until its build changes, so a
    // weaker body recovers and grows instead of entering again and again only to be pushed out.
    const FLDOpponentRetryKey CaptureKey = FContext::EngagementKey(C.Capture->Id, FGuid());
    if (bStopLoss && !bHoldingSolo && !bOwner && (State.Mode == EMode::DefendOrHold || State.Mode == EMode::Contest))
    {
        State.Failures.RecordFailure(CaptureKey, C.Print(ELDOpponentFailure::Engagement), C.Now, C.AI);
    }
    const bool bMayContest = C.CanAttempt(CaptureKey);
    // 2026-11-07: the guard runs only while its owner stands alone in the zone; an empty zone pauses it, a challenger
    // beside him freezes it and his walking out under that challenger resets it to zero. So the owner goes back to the
    // zone and stays in it, recovering too, until the stop-loss line, where his death would clear the graffiti.
    const bool bOwnerStays = bOwner && !bStopLoss;
    FLDOpponentBodyIntent Intent;
    if ((bHoldingSolo || (bHoldingPaused && !bStopLoss) || bOwnerStays
            || ((bUrgent || bContesting || bRivalGraffiti) && !bStopLoss && bMayContest))
        && CircleBody(C, bUrgent, Intent))
    {
        EndVisit(State, false);
        Body(Intent);
        if (bHolder) { return Finish(EMode::DefendOrHold, EReason::HoldCapture); }
        if (bOwner) { return Finish(EMode::DefendOrHold, EReason::GuardGraffiti); }
        if (bRivalGraffiti) { return Finish(bUrgent ? EMode::DefendOrHold : EMode::Contest, EReason::BesiegeOwner); }
        if (bUrgent) { return Finish(EMode::DefendOrHold, EReason::PublicCaptureUrgent); }
        return Finish(EMode::Contest, EReason::ContestCapture);
    }
    if (State.bRecovering && !C.Threats.IsEmpty() && !bHoldingSolo)
    {
        Body(DisengageBody(C));
        return Finish(EMode::Disengage, EReason::Recover);
    }
    // D26 (#79): a body inside the Boss's acquire band steps away from it before anything but the circle; out of the
    // band it carries on, and a recovering body has already disengaged above. #160: an escape that has left the band
    // walks on to its place while the Boss stays in sight; turning back at the band's edge sent a new order on every
    // decision, flipping between the escape and the errand that led back toward the Boss.
    const bool bInBossBand = C.Boss && C.Distance(C.Boss->FootCm) <= C.BossBandCm;
    if (C.Boss && !bHoldingSolo && (bInBossBand || State.bEscapingBoss))
    {
        EndVisit(State, false);
        const FLDOpponentPOI* Kept = bInBossBand ? nullptr : C.FindPOI(State.RetreatPOI);
        Body(Kept ? MoveTo(C, *Kept) : DisengageBody(C));
        State.bEscapingBoss = !State.RetreatPOI.IsNone() && Out.Body.POI == State.RetreatPOI
            && (Out.Body.Kind == EBody::MoveGround || Out.Body.Kind == EBody::Keep);
        return Finish(EMode::Disengage, EReason::AvoidBoss);
    }
    State.bEscapingBoss = false;
    if (!State.bRecovering)
    {
        if (const FLDOpponentUnit* Target = SelectEngageTarget(C))
        {
            EndVisit(State, false);
            Body(EngageBody(C, *Target));
            return Finish(EMode::Engage, EReason::EngageTarget);
        }
    }
    if (State.bRecovering)
    {
        // §6.1 P2: an escape under way finishes its walk, also once the pursuer has dropped out of sight. Otherwise
        // every glimpse at the edge of sight flips the body between its errand and the escape, and it never reaches
        // either (#79).
        const auto& Order = Frame.Order;
        const FLDOpponentPOI* Retreat = State.RetreatPOI.IsNone() ? nullptr : C.FindPOI(State.RetreatPOI);
        if (Retreat && Order.IsLive() && Order.Kind == ELDOpponentOrderKind::MoveGround && Order.POI == Retreat->Id)
        {
            Body(MoveTo(C, *Retreat));
            return Finish(EMode::Disengage, EReason::Recover);
        }
        // D21: inside a dorm the fast recovery comes first; a visible threat above still sends it away.
        if (C.DormAt())
        {
            Body(Halt(C));
            return Finish(EMode::Disengage, EReason::Recover);
        }
        // D21: otherwise walk to the nearest dorm for its fast recovery; it is not a safe zone, so defence and threats
        // above still come first. While a rival sprays alone or guards its graffiti the body stays within its defence
        // reach instead: walking off to a corner would flip the public urgency on and off at every step.
        // Without a reachable dorm, wait on genuine regeneration.
        const bool bRivalHolds = C.bCaptureOpen && ((Frame.Public.Circle == ELDOpponentCircle::Solo && !bHoldingSolo) || bRivalGraffiti);
        if (const FLDOpponentPOI* Dorm = bRivalHolds ? nullptr : C.NearestDorm())
        {
            Body(MoveTo(C, *Dorm));
            return Finish(EMode::Disengage, EReason::Recover);
        }
        FActivationNeed Need;
        auto Activation = PickActivation(C, Need);
        Body(Activation.IsSet() ? Activation.GetValue() : Halt(C));
        return Finish(EMode::Disengage, EReason::Recover);
    }
    // D26 (#79): a healthy body goes for a public airdrop that is announced or on the ground; first come, first served.
    // It stays below defending, holding and an active contest of the circle, and does not leave a camp fight it began.
    // On the point it waits for the landing instead of re-ordering the same walk.
    if (const FLDOpponentPOI* Drop = State.VisitEngagedAtMs == 0 ? C.AirdropTarget() : nullptr)
    {
        EndVisit(State, false);
        Body(C.Distance(Drop->FootCm) * 2. <= C.AirdropRadiusCm ? Halt(C) : MoveTo(C, *Drop));
        return Finish(EMode::Airdrop, EReason::ContestAirdrop);
    }
    if (C.bCaptureOpen && bMayContest && CircleBody(C, false, Intent))
    {
        EndVisit(State, false);
        Body(Intent);
        return Finish(EMode::Contest, EReason::ContestCapture);
    }
    // D27 (#79): a strong, healthy body hunts a named leader: below the circle and the airdrop, above
    // farming, and never out of a camp fight it began. Once the leader is in sight the engage branch takes over.
    if (const FLDOpponentPOI* Hunt = State.VisitEngagedAtMs == 0 ? C.LeaderHuntTarget() : nullptr)
    {
        EndVisit(State, false);
        Body(MoveTo(C, *Hunt));
        return Finish(EMode::Explore, EReason::HuntLeader);
    }
    if (FarmBody(C, Intent))
    {
        Body(Intent);
        return Finish(EMode::Farm, EReason::FarmCamp);
    }
    if (ExploreBody(C, Intent))
    {
        Body(Intent);
        return Finish(EMode::Explore, EReason::Explore);
    }
    Body(Halt(C));
    return Finish(EMode::Explore, EReason::NoLegalTarget);
}

void RecordOutcome(FLDOpponentPolicyState& State, const FLDOpponentRetryKey& Key, ELDOpponentOutcome Outcome,
    const FLDOpponentSelf& Self, int64 NowMs, const FLDDemoAIParameters& AI)
{
    using namespace Detail;
    switch (Outcome)
    {
    case ELDOpponentOutcome::Succeeded:
        State.Failures.RecordSuccess(Key);
        return;
    case ELDOpponentOutcome::TargetLost:
        State.Failures.Suppress(Key, Fingerprint(Key.Category, Self));
        if (Key.Target.IsValid()
            && (Key.Category == ELDOpponentFailure::Engagement || Key.Category == ELDOpponentFailure::Activation))
        {
            // Hidden, destroyed, expired or another life are indistinguishable: stop pursuing this handle.
            State.Failures.Suppress(FContext::EngagementKey(NAME_None, Key.Target), Fingerprint(ELDOpponentFailure::Engagement, Self));
            if (State.TargetHandle == Key.Target) { ForgetTarget(State); }
        }
        return;
    default:
        State.Failures.RecordFailure(Key, Fingerprint(Key.Category, Self), NowMs, AI);
        return;
    }
}

void RecordWalkAdmission(FLDOpponentPolicyState& State, const FLDOpponentRetryKey& Goal, bool bRefusedForGround,
    const FLDOpponentSelf& Self, int64 NowMs, const FLDDemoAIParameters& AI)
{
    using namespace Detail;
    const FLDOpponentRetryKey Walk = FContext::WalkKey();
    if (!bRefusedForGround)
    {
        State.Failures.RecordSuccess(Walk);
        State.bHasRefusedWalk = false;
        return;
    }
    // The same goal refused again is that goal's own failure memory; only a different goal points at the body.
    const bool bOtherGoal = !State.bHasRefusedWalk || !(State.LastRefusedWalk == Goal);
    State.LastRefusedWalk = Goal;
    State.bHasRefusedWalk = true;
    if (bOtherGoal) { State.Failures.RecordFailure(Walk, Fingerprint(ELDOpponentFailure::Walk, Self), NowMs, AI); }
}

bool DecideLearnOrAllocate(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
    FLDOpponentPolicyState& State, FLDOpponentBuildIntent& Out)
{
    return Detail::DecideBuild(Frame, Catalog, State, false, Out);
}

#if WITH_DEV_AUTOMATION_TESTS
FLDOpponentBodyIntent EngageUnit(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
    FLDOpponentPolicyState& State, const FLDOpponentUnit& Target)
{
    using namespace Detail;
    FContext C(Frame, Catalog, State);
    if (!C.Initialize()) { return {}; }
    return EngageBody(C, Target);
}

FLDOpponentBodyIntent WalkTo(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
    FLDOpponentPolicyState& State, FName POI)
{
    using namespace Detail;
    FContext C(Frame, Catalog, State);
    const FLDOpponentPOI* Place = C.FindPOI(POI);
    if (!Place || !C.Initialize()) { return {}; }
    return MoveTo(C, *Place);
}
#endif
}
