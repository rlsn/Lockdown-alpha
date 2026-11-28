#include "AI/LDOpponentPolicy.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDCombatElements.h"
#include "Development/LDCampGrowth.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Development/LDProgressionRules.h"
#include "Development/LDBreakoutPresets.h"
#include "Vision/LDVisionDirector.h"

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
    // #198 elements and the element actives' riders on enemies.
    case ELDDemoEffectOp::Wet:
    case ELDDemoEffectOp::Electrify:
    case ELDDemoEffectOp::ChainBounce:
    case ELDDemoEffectOp::ZoneWet:
    case ELDDemoEffectOp::Rot:
    case ELDDemoEffectOp::Chill:
    case ELDDemoEffectOp::GroundState:
    case ELDDemoEffectOp::Fan:
    case ELDDemoEffectOp::Bag:
        Usage.bEngage |= !bSelfTarget;
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
    // #198 S16 & #345 S05: a damage cut that answers when hit or when it ends.
    case ELDDemoEffectOp::Brace:
    case ELDDemoEffectOp::ParryStance:
        Usage.bSelfProtect |= bSelfTarget;
        break;
    case ELDDemoEffectOp::FrenzyWaves:
        Usage.bEngage = true;
        break;
    case ELDDemoEffectOp::Dash:
        Usage.bMobility = true;
        break;
    // Survival, basic-attack, stat, cooldown, momentum and release riders add no structural usage of their own.
    case ELDDemoEffectOp::Zone:
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
    // The skill's element (ELDCombatElement numbering; spec §13 reaction choice).
    uint8 Element = 0;
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
        // N4 & #345 N6: a bag or cordon is placed on the target like a trap.
        case ELDDemoEffectOp::Bag:
        case ELDDemoEffectOp::CordonLine: Out.ZoneRadiusCm = FMath::Max(Out.ZoneRadiusCm, ToDouble(Effect.RadiusCm)); break;
        case ELDDemoEffectOp::EmpowerBasic:
        case ELDDemoEffectOp::FrenzyWaves: Out.bEmpowerBasic = true; break;
        case ELDDemoEffectOp::Root: Out.bRoot = true; break;
        case ELDDemoEffectOp::Stun:
        case ELDDemoEffectOp::Silence: Out.bStun = true; break;
        case ELDDemoEffectOp::Taunt:
            Out.bTaunt = true;
            Out.TauntRadiusCm = FMath::Max(Out.TauntRadiusCm, ToDouble(Effect.RadiusCm));
            break;
        case ELDDemoEffectOp::Shield:
        case ELDDemoEffectOp::Brace:
        case ELDDemoEffectOp::ParryStance: Out.bShield = true; break;
        case ELDDemoEffectOp::DispelRoot: Out.bDispelRoot = true; break;
        case ELDDemoEffectOp::Dash:
            Out.bDash = true;
            Out.RangeCm = FMath::Max(Out.RangeCm, ToDouble(Effect.DistanceCm));
            break;
        default: break;
        }
    }
}

// The typed target and effects of the active bound to a source, read from its learned branch variant when it has one
// (a branch may change the target, element and effects, e.g. S11 3B1 adds the dash's contact damage).
bool ResolveSource(const FLDOpponentSource& Source, const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog,
    FSourceShape& Out)
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
    const FLDOpponentActive* Learned = Self.Actives.FindByPredicate([&Source](const FLDOpponentActive& Active)
        { return Active.Slot == uint8(Source.Slot) && Active.DefinitionId == Source.DefinitionId; });
    const FLDDemoAbilityBranch* Branch = Learned ? Ability->FindBranch(Learned->Branch) : nullptr;
    if (Learned && Learned->Branch != ELDSkillBranch::None && !Branch) { return false; }
    const FLDDemoTargetDefinition& Target = Branch ? Branch->Target : Ability->Target;
    Out.Mode = Target.Mode;
    Out.RangeCm = ToDouble(Target.RangeCm);
    Out.Element = uint8(Branch ? Branch->Element : Ability->Element);
    ReadEffects(Branch ? Branch->Values.Effects : Ability->Values.Effects, Out);
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
    double ObjectiveReachCm = 0;
    // #305: a body this close to a locker anchor stands well inside its pickup radius (EXPLORE.LockerRadiusCm / 2).
    double LockerStandCm = 0;
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
        // The final field's zone may be resized (final_double_yellow); the public radius is what counts.
        CaptureRadiusCm = Frame.Public.PoleRadiusCm > 0 ? double(Frame.Public.PoleRadiusCm) : ToDouble(Content.Parameters.CaptureRadiusCm);
        DormRadiusCm = ToDouble(Content.Parameters.DormRadiusCm);
        AirdropRadiusCm = ToDouble(Content.Parameters.AirdropPickupRadiusCm);
        ObjectiveReachCm = ToDouble(Content.Parameters.AI.ObjectiveReachCm);
        LockerStandCm = ToDouble(Content.Parameters.Explore.LockerRadiusCm) / 2.;
        LeaderRadiusCm = ToDouble(Content.Parameters.LeaderCircleRadiusCm);
        const FLDDemoMonsterDefinition* BossBody = Content.Monsters.FindByPredicate(
            [&Content](const FLDDemoMonsterDefinition& M) { return M.Tier == ELDDemoCampTier::Boss && M.Id == Content.Boss.Id; });
        BossBandCm = BossBody ? ToDouble(Content.Boss.AggroRadiusCm) + ToDouble(BossBody->AttackRangeCm) : 0.;
        AttackRangeCm = ToDouble(Self.Attributes.AttackRangeCm);
        VisionRadiusCm = ToDouble(Content.Parameters.VisionRadiusCm);
        if (Content.Formulas.AttackMoveAcquireRadius != ELDDemoFormula::MinVisionTwiceAttackRange
            || !(AttackRangeCm > 0) || !(VisionRadiusCm > 0) || !(CaptureRadiusCm > 0) || !(DormRadiusCm > 0) || !(AirdropRadiusCm > 0) || !(ObjectiveReachCm > 0) || !(LockerStandCm > 0) || !(LeaderRadiusCm > 0) || !(BossBandCm > 0) || !Capture)
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
                // D26: the Boss turns on anyone near it, so a seen Boss is always a threat; only a healthy AI fights it.
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

    // A healthy body (at least AI.HighCampEntryHPBp of its HP) contests a seen Boss instead of stepping away from it.
    bool BossFightReady() const
    {
        return Boss && Boss->HpP2.IsSet() && Boss->HpP2.GetValue() > 0 && RatioAtLeast(Self.HpP2, Self.MaxHpP2, AI.HighCampEntryHPBp);
    }

    // Members are associated with the nearest public camp of their own creature; no hidden camp table.
    const FLDOpponentPOI* CampOf(const FLDOpponentUnit& Unit) const
    {
        const FLDOpponentPOI* Best = nullptr;
        double BestDistance = 0;
        for (const auto& POI : Frame.POIs)
        {
            if (POI.Kind != ELDOpponentPOIKind::Camp) { continue; }
            const FLDDemoMonsterDefinition *Known=nullptr,*Seen=nullptr;
            FString LookupError;
            if(!Catalog.FindMonster(POI.MonsterId,Known,LookupError) || !Catalog.FindMonster(Unit.MonsterId,Seen,LookupError)
                || Known->Tier!=Seen->Tier){continue;}
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
    // B01 T04-03: the nearest camp-clear recovery orb in sight within the objective reach, for a body missing health or
    // momentum, when its walk is not suppressed; nullptr otherwise.
    const FLDOpponentPOI* RecoveryOrbTarget() const
    {
        if (Self.HpP2 >= Self.MaxHpP2 && Self.MomentumP2 >= Self.MaxMomentumP2) { return nullptr; }
        const FLDOpponentPOI* Best = nullptr;
        for (const auto& POI : Frame.POIs)
        {
            if (POI.Kind != ELDOpponentPOIKind::RecoveryOrb || Distance(POI.FootCm) > ObjectiveReachCm || !CanAttempt(PathKey(POI.Id))) { continue; }
            if (!Best || Distance(POI.FootCm) < Distance(Best->FootCm)) { Best = &POI; }
        }
        return Best;
    }
    // P6 spec §10.3: the public place of the live mid-field objective (a mini_flag_* point, or the flagpole for the arena)
    // while it is announced or open, within the approved reach, for a body at the approved entry health, when its walk is
    // not suppressed; nullptr otherwise.
    const FLDOpponentPOI* ObjectiveTarget() const
    {
        const FLDObjectiveView& Goal = Frame.Public.Objective;
        if ((Goal.Phase != ELDObjectivePhase::Announced && Goal.Phase != ELDObjectivePhase::Open)
            || !RatioAtLeast(Self.HpP2, Self.MaxHpP2, AI.ObjectiveEntryHPBp))
        {
            return nullptr;
        }
        const FLDOpponentPOI* Place = Goal.Kind == ELDObjectiveKind::Arena ? Capture
            : Goal.Kind == ELDObjectiveKind::MiniFlag && !Goal.AnchorId.IsNone() ? FindPOI(Goal.AnchorId) : nullptr;
        if (Place && Goal.Kind == ELDObjectiveKind::MiniFlag && Place->Kind != ELDOpponentPOIKind::MiniFlag) { Place = nullptr; }
        return Place && Distance(Place->FootCm) <= ObjectiveReachCm && CanAttempt(PathKey(Place->Id)) ? Place : nullptr;
    }
    // #305 spec §11.3: the nearest seen, unopened locker within the objective reach for a body at the objective entry
    // health, when its walk is not suppressed; nullptr otherwise.
    const FLDOpponentPOI* LockerTarget() const
    {
        if (!RatioAtLeast(Self.HpP2, Self.MaxHpP2, AI.ObjectiveEntryHPBp)) { return nullptr; }
        const FLDOpponentPOI* Best = nullptr;
        for (const auto& POI : Frame.POIs)
        {
            // A live walk to the locker keeps it: a path round a ramp can lead out of the straight-line reach first, and
            // dropping it there flips the body between the locker and its next goal at every decision.
            const bool bWalking = Frame.Order.IsLive() && Frame.Order.Kind == ELDOpponentOrderKind::MoveGround && Frame.Order.POI == POI.Id;
            if (POI.Kind != ELDOpponentPOIKind::Locker || (!bWalking && Distance(POI.FootCm) > ObjectiveReachCm) || !CanAttempt(PathKey(POI.Id))) { continue; }
            if (!Best || Distance(POI.FootCm) < Distance(Best->FootCm)) { Best = &POI; }
        }
        return Best;
    }
    // #305 spec §11.3: the visible second-hand dealer within the objective reach for a body at the objective entry health.
    const FLDOpponentUnit* DealerTarget() const
    {
        if (!RatioAtLeast(Self.HpP2, Self.MaxHpP2, AI.ObjectiveEntryHPBp)) { return nullptr; }
        return Frame.Units.FindByPredicate([this](const FLDOpponentUnit& Unit)
            { return Unit.bPeddler && Unit.HpP2.IsSet() && Unit.HpP2.GetValue() > 0 && Distance(Unit.FootCm) <= ObjectiveReachCm; });
    }
    // D27: the public place nearest the centre of another contender's 上热搜 circle, inside that circle, when its walk
    // is not suppressed; a seen leader is then engaged by the ordinary engage branch. Only for a body at the notice's
    // minimum level with high-camp entry health.
    const FLDOpponentPOI* LeaderHuntTarget() const
    {
        if (!Frame.Public.Leader.IsValid() || Frame.Public.Leader == Self.ContenderId
            || Self.Level < Catalog.Get().Parameters.LeaderMinLevel || !RatioAtLeast(Self.HpP2, Self.MaxHpP2, AI.HighCampEntryHPBp)
            // A leader too far above this body's level is left alone (its level is public with the notice).
            || int64(Frame.Public.LeaderLevel) > int64(Self.Level) + AI.LeaderHuntMaxLevelGap)
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

// A pickup point (a D26 drop, a #305 locker): near its middle the body stops; inside its pickup radius it lets a live
// walk to the point finish but never orders a new one, and stops any walk elsewhere. A crowded or kept-clear point ends
// every walk where the body already stands, and re-ordering it there loops a request at every decision.
FLDOpponentBodyIntent ApproachPickup(const FContext& C, const FLDOpponentPOI& Drop, double RadiusCm)
{
    const double Distance = C.Distance(Drop.FootCm);
    if (Distance * 2. <= RadiusCm) { return Halt(C); }
    if (Distance > RadiusCm) { return MoveTo(C, Drop); }
    // Whether walking is held or not, only a live walk to this drop is kept.
    const auto& Order = C.Frame.Order;
    if (!(Order.IsLive() && Order.Kind == ELDOpponentOrderKind::MoveGround && Order.POI == Drop.Id)) { return Halt(C); }
    FLDOpponentBodyIntent Keep;
    Keep.Kind = EBody::Keep;
    Keep.POI = Drop.Id;
    Keep.Key = FContext::PathKey(Drop.Id);
    return Keep;
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
    static constexpr ELDOpponentSourceSlot Fixed[] = {
        ELDOpponentSourceSlot::Q, ELDOpponentSourceSlot::W, ELDOpponentSourceSlot::E, ELDOpponentSourceSlot::Dash};
    // Spec §13 element and reaction choice: among damaging skills, one whose element reacts with the element the target
    // visibly holds goes first, then one that lays an element on a clean target; otherwise the fixed order.
    TArray<ELDOpponentSourceSlot, TInlineAllocator<4>> Ordered(Fixed, UE_ARRAY_COUNT(Fixed));
    if (Need.Target)
    {
        const auto Score = [&C, &Need](ELDOpponentSourceSlot Slot)
        {
            const auto* Source = C.Self.Sources.FindByPredicate([Slot](const FLDOpponentSource& S) { return S.Slot == Slot; });
            FSourceShape Shape;
            if (!Source || !ResolveSource(*Source, C.Self, C.Catalog, Shape) || Shape.Element == 0) { return 0; }
            const ELDCombatElement Held = static_cast<ELDCombatElement>(Need.Target->HeldElement);
            if (LDCombatElements::ReactionOf(Held, static_cast<ELDCombatElement>(Shape.Element)) != ELDCombatReaction::None) { return 2; }
            return Held == ELDCombatElement::None ? 1 : 0;
        };
        Ordered.StableSort([&Score](ELDOpponentSourceSlot A, ELDOpponentSourceSlot B) { return Score(A) > Score(B); });
    }
    for (int32 Tier = 0; Tier < 5; ++Tier)
    {
        for (const ELDOpponentSourceSlot Slot : (Tier == 3 ? TConstArrayView<ELDOpponentSourceSlot>(Ordered) : TConstArrayView<ELDOpponentSourceSlot>(Fixed)))
        {
            const auto* Source = C.Self.Sources.FindByPredicate([Slot](const FLDOpponentSource& S) { return S.Slot == Slot; });
            FSourceShape Shape;
            if (!Source || !Source->bReady || !ResolveSource(*Source, C.Self, C.Catalog, Shape)) { continue; }
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
                // A self taunt is a hard control: it interrupts a sole sprayer and, in a fight with a rival contender
                // inside its radius, forces that rival onto the caster like any other control.
                if (Shape.Mode == ELDDemoTargetMode::Self && Shape.bTaunt && Need.Target
                    && (Need.bInterrupt || (Need.bAllowDamage && Need.Target->Kind == ELDOpponentUnitKind::Contender))
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
                    else if (Shape.Mode == ELDDemoTargetMode::DirectionProjectile || Shape.Mode == ELDDemoTargetMode::DirectionCone
                        || Shape.Mode == ELDDemoTargetMode::DirectionLine)
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
            case 4:
                // A self shield or brace also covers an ordinary trade: once nothing better is ready, it goes up when a
                // rival contender stands within attack range (Tier 0 keeps the low-health protection first).
                bUse = Need.bAllowDamage && Need.Target && Need.Target->Kind == ELDOpponentUnitKind::Contender
                    && Shape.Mode == ELDDemoTargetMode::Self && Shape.bShield
                    && C.Distance(Need.Target->FootCm) <= C.AttackRangeCm;
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

// Issue #287 spec §13: a visible enemy warning about to land on the own foot is dashed out of with the ready D, at
// AI.DodgeChance (one stable roll per warning); without bRoll (DEV evaluation's WarningReactMs) every such warning is.
TOptional<FLDOpponentBodyIntent> DodgeIntent(const FContext& C, bool bRoll = true)
{
    const FLDOpponentSource* Dash = C.Self.Sources.FindByPredicate([](const FLDOpponentSource& S) { return S.Slot == ELDOpponentSourceSlot::Dash; });
    if (!Dash || !Dash->bReady) { return {}; }
    const FVector Foot(C.Self.FootCm.X, C.Self.FootCm.Y, 0);
    for (const FLDOpponentWarning& Warning : C.Frame.Warnings)
    {
        if (Warning.RemainingMs > 450) { continue; }
        const FVector Centre(Warning.CentreCm.X, Warning.CentreCm.Y, 0);
        const FVector Heading = FVector(Warning.Heading.X, Warning.Heading.Y, 0).GetSafeNormal();
        const FVector Offset = Foot - Centre;
        bool bInside = false;
        FVector Away = Offset.GetSafeNormal();
        if (Warning.LengthCm > 0 && !Heading.IsNearlyZero())
        {
            const double Ahead = FVector::DotProduct(Offset, Heading);
            const FVector Side = FVector::CrossProduct(FVector::UpVector, Heading);
            const double Across = FVector::DotProduct(Offset, Side);
            bInside = Warning.AngleDegrees > 0
                ? Offset.Size() <= Warning.LengthCm && (Offset.IsNearlyZero()
                    || FVector::DotProduct(Offset.GetSafeNormal(), Heading) >= FMath::Cos(FMath::DegreesToRadians(Warning.AngleDegrees * .5)))
                : Ahead >= 0 && Ahead <= Warning.LengthCm && FMath::Abs(Across) <= Warning.RadiusCm;
            // Out of a line or a cone sideways.
            Away = Across >= 0 ? Side : -Side;
        }
        else
        {
            bInside = Offset.Size() <= Warning.RadiusCm;
        }
        if (!bInside) { continue; }
        if (Away.IsNearlyZero()) { Away = Heading.IsNearlyZero() ? FVector::ForwardVector : FVector::CrossProduct(FVector::UpVector, Heading); }
        const uint32 Roll = HashCombine(GetTypeHash(Warning.Handle), GetTypeHash(C.Self.ContenderId)) % 10000;
        if (bRoll && int64(Roll) >= C.Catalog.Get().Parameters.AI.DodgeChanceBp) { continue; }
        FLDOpponentBodyIntent Body;
        Body.Kind = EBody::Activate;
        Body.Source = ELDOpponentSourceSlot::Dash;
        Body.TargetMode = ELDDemoTargetMode::DirectionDash;
        Body.Direction = Away.GetSafeNormal2D();
        Body.Key.Category = ELDOpponentFailure::Activation;
        Body.Key.Slot = uint8(ELDOpponentSourceSlot::Dash);
        if (C.CanAttempt(Body.Key)) { return Body; }
    }
    return {};
}

// The authoritative footprint of a public ground record (the same containment Combat uses).
FLDGroundState GroundShape(const FLDObservedGroundState& View)
{
    FLDGroundState Shape;
    Shape.CentreCm = View.CentreCm;
    Shape.RadiusCm = View.RadiusCm;
    Shape.Heading = FVector2D(FRotator(0, View.Heading * 360. / 256., 0).Vector());
    Shape.HalfLengthCm = View.Shape == ELDObservedGroundShape::Band ? View.ExtentCm : 0;
    Shape.AngleDegrees = View.Shape == ELDObservedGroundShape::Sector ? View.ExtentCm : 0;
    return Shape;
}

// Issue #287 spec §13: the AI never stays in an enemy's ground state; walks are planned around them (GroundWeightedCm in
// the controller's route choice); knocked or pushed into one it dashes out away from its centre with the ready D (a warning
// dodge comes first).
TOptional<FLDOpponentBodyIntent> LeaveGroundIntent(const FContext& C)
{
    const FLDOpponentSource* Dash = C.Self.Sources.FindByPredicate([](const FLDOpponentSource& S) { return S.Slot == ELDOpponentSourceSlot::Dash; });
    if (!Dash || !Dash->bReady) { return {}; }
    for (const FLDOpponentGround& Ground : C.Frame.Grounds)
    {
        if (!Ground.bEnemy) { continue; }
        if (!LDCombatElements::GroundContains(GroundShape(Ground.View), C.Self.FootCm)) { continue; }
        FVector Away = (C.Self.FootCm - Ground.View.CentreCm).GetSafeNormal2D();
        if (Away.IsNearlyZero()) { Away = FVector::ForwardVector; }
        FLDOpponentBodyIntent Body;
        Body.Kind = EBody::Activate;
        Body.Source = ELDOpponentSourceSlot::Dash;
        Body.TargetMode = ELDDemoTargetMode::DirectionDash;
        Body.Direction = Away;
        Body.Key.Category = ELDOpponentFailure::Activation;
        Body.Key.Slot = uint8(ELDOpponentSourceSlot::Dash);
        if (C.CanAttempt(Body.Key)) { return Body; }
    }
    return {};
}

// §6.2 stop-loss: a ready self shield is the usable self-protection that still lets a low body answer defence.
bool ProtectionReady(const FContext& C)
{
    for (const auto& Source : C.Self.Sources)
    {
        FSourceShape Shape;
        if (!Source.bReady || !ResolveSource(Source, C.Self, C.Catalog, Shape) || Shape.Mode != ELDDemoTargetMode::Self
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

// The visible rival graffiti owner within the acquire radius, matched only through authorized observations.
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

const FLDOpponentUnit* SelectEngageTarget(const FContext& C);

// A walk to a ground point of the final arena, clicked on the flagpole's surface; a live point walk is kept until it ends.
FLDOpponentBodyIntent MoveToPoint(const FContext& C, const FVector& Point)
{
    FLDOpponentBodyIntent Body;
    const auto& Order = C.Frame.Order;
    if (Order.IsLive() && Order.Kind == ELDOpponentOrderKind::MoveGround && Order.POI.IsNone()) { return Hold(C); }
    // Refused point walks wait out their own retry budget instead of being resent every decision.
    const FLDOpponentRetryKey Key = FContext::PathKey(TEXT("final_point"));
    if (!C.CanWalk() || !C.CanAttempt(Key)) { return Hold(C); }
    Body.Kind = EBody::MoveGround;
    Body.Point = Point;
    Body.Surface = C.Capture->SurfaceId;
    Body.Key = Key;
    return Body;
}

// The minimal play of the public final mods in the final field (content §4.7), read only from the public view: a weak body
// keeps away from a usurp or clear-out holder, nobody presses into a standing fence, a carrier runs from enemies inside the
// safe ring, a hit-and-run sprayer leaves the zone, a rival's private flagpole near its guard gets frozen, a private-pole
// holder works his own pole, and kill-seconds or clear-out holders hunt. False leaves the ordinary flagpole play.
bool FinalModBody(const FContext& C, FLDOpponentBodyIntent& Out)
{
    const FLDOpponentPublicMatch& Public = C.Frame.Public;
    const FName Mine = Public.FinalModOf(C.Self.ContenderId);
    const FVector& Foot = C.Self.FootCm;
    const auto Clamp = [&Public](FVector Point)
    {
        const FVector Offset = Point - Public.ArenaCentreCm;
        if (Public.SafeRadiusCm > 0 && Offset.Size2D() > Public.SafeRadiusCm)
        { Point = Public.ArenaCentreCm + Offset.GetSafeNormal2D() * Public.SafeRadiusCm; Point.Z = Public.ArenaCentreCm.Z; }
        return Point;
    };
    const auto Away = [&](const FVector& From, double Distance)
    {
        FVector Direction = (Foot - From).GetSafeNormal2D();
        if (Direction.IsNearlyZero()) { Direction = (Foot - Public.ArenaCentreCm).GetSafeNormal2D(); }
        if (Direction.IsNearlyZero()) { Direction = FVector::ForwardVector; }
        return Clamp(Foot + Direction * Distance);
    };
    const FLDOpponentUnit* Nearest = nullptr;
    for (const FLDOpponentUnit* Enemy : C.Enemies)
    {
        if (!Enemy->bProtected && (!Nearest || C.Distance(Enemy->FootCm) < C.Distance(Nearest->FootCm))) { Nearest = Enemy; }
    }
    // A weak body steps away from a visible holder whose kill would take its graffiti or finish a clear-out.
    if (!RatioAtLeast(C.Self.HpP2, C.Self.MaxHpP2, C.AI.RetreatHPBp))
    {
        for (const FLDOpponentUnit* Enemy : C.Enemies)
        {
            const FName Mod = Public.FinalModOf(Enemy->ContenderId);
            if ((Mod == TEXT("final_usurp") || Mod == TEXT("final_clear_out")) && C.Distance(Enemy->FootCm) <= C.AcquireRadiusCm)
            {
                ForgetTarget(C.State);
                Out = MoveToPoint(C, Away(Enemy->FootCm, 600.));
                return true;
            }
        }
    }
    // A standing fence keeps outsiders out: chase only enemies outside it, hit one inside only from where it stands
    // (in attack range, no walk), otherwise stop and wait for the fence to fall.
    if (Public.CageUntilMs > C.Now && FVector::Dist2D(Foot, Public.CageCentreCm) > Public.CageRadiusCm)
    {
        const FLDOpponentUnit* Outside = nullptr;
        const FLDOpponentUnit* InReach = nullptr;
        for (const FLDOpponentUnit* Enemy : C.Enemies)
        {
            if (Enemy->bProtected || HandleSuppressed(C, *Enemy)) { continue; }
            const bool bCaged = FVector::Dist2D(Enemy->FootCm, Public.CageCentreCm) <= Public.CageRadiusCm;
            if (!bCaged && (!Outside || C.Distance(Enemy->FootCm) < C.Distance(Outside->FootCm))) { Outside = Enemy; }
            if (bCaged && C.Distance(Enemy->FootCm) <= C.AttackRangeCm) { InReach = Enemy; }
        }
        if (Outside) { Out = EngageBody(C, *Outside); return true; }
        if (InReach) { Out = AttackBody(C, *InReach); return true; }
        ForgetTarget(C.State);
        Out = Halt(C);
        return true;
    }
    const bool bOwner = Public.Owner.IsValid() && Public.Owner == C.Self.ContenderId;
    // The carrier's flagpole follows him: he keeps away from enemies, never beyond the arena's last safe ring.
    if (bOwner && Mine == TEXT("final_carry_flag") && Public.CageUntilMs <= C.Now)
    {
        if (Nearest && C.Distance(Nearest->FootCm) <= C.AcquireRadiusCm)
        {
            ForgetTarget(C.State);
            Out = MoveToPoint(C, Away(Nearest->FootCm, 500.));
            return true;
        }
        Out = Halt(C);
        return true;
    }
    // A hit-and-run graffiti has no guard: its sprayer leaves the zone at once to spray again.
    if (bOwner && Mine == TEXT("final_hit_and_run") && Public.FadeAtMs > 0)
    {
        if (C.bInsideCircle)
        {
            // The first exit around the pole that is still outside the zone once kept inside the safe ring.
            ForgetTarget(C.State);
            const FVector Pole = C.Capture->FootCm;
            const double Reach = C.CaptureRadiusCm + 200.;
            FVector Away0 = (Foot - Pole).GetSafeNormal2D();
            if (Away0.IsNearlyZero()) { Away0 = (Public.ArenaCentreCm - Pole).GetSafeNormal2D(); }
            if (Away0.IsNearlyZero()) { Away0 = FVector::ForwardVector; }
            for (int32 Turn = 0; Turn < 8; ++Turn)
            {
                const FVector Exit = Clamp(Pole + Away0.RotateAngleAxis(45. * Turn, FVector::UpVector) * Reach);
                if (FVector::Dist2D(Exit, Pole) > C.CaptureRadiusCm + 50.) { Out = MoveToPoint(C, Exit); return true; }
            }
            Out = Halt(C);
            return true;
        }
        Out = Halt(C);
        return true;
    }
    // A rival's private flagpole is frozen by standing in it once its graffiti has a third of its guard.
    for (const FLDPrivatePoleView& Pole : Public.PrivatePoles)
    {
        if (Pole.Holder == C.Self.ContenderId || !Pole.bGraffiti) { continue; }
        const int64 Kept = Pole.GuardDueAtMs > 0 ? Pole.GuardMs - (Pole.GuardDueAtMs - C.Now) : Pole.GuardKeptMs;
        if (Kept * 3 < Pole.GuardMs) { continue; }
        if (FVector::Dist2D(Foot, Pole.CentreCm) > Pole.RadiusCm * .6) { Out = MoveToPoint(C, Pole.CentreCm); return true; }
        // Staying in freezes it: hit only what is in attack range without walking, never chase out of the zone.
        for (const FLDOpponentUnit* Enemy : C.Enemies)
        {
            if (!Enemy->bProtected && !HandleSuppressed(C, *Enemy) && C.Distance(Enemy->FootCm) <= C.AttackRangeCm)
            { Out = AttackBody(C, *Enemy); return true; }
        }
        ForgetTarget(C.State);
        Out = Halt(C);
        return true;
    }
    // A private-pole holder sprays and guards his own pole, fighting only who comes in.
    if (Mine == TEXT("final_private_pole"))
    {
        if (const FLDPrivatePoleView* Own = Public.PrivatePoles.FindByPredicate([&C](const FLDPrivatePoleView& Pole)
            { return Pole.Holder == C.Self.ContenderId; }))
        {
            if (FVector::Dist2D(Foot, Own->CentreCm) > Own->RadiusCm * .6) { Out = MoveToPoint(C, Own->CentreCm); return true; }
            for (const FLDOpponentUnit* Enemy : C.Enemies)
            {
                if (!Enemy->bProtected && FVector::Dist2D(Enemy->FootCm, Own->CentreCm) <= Own->RadiusCm)
                { Out = EngageBody(C, *Enemy); return true; }
            }
            ForgetTarget(C.State);
            Out = Halt(C);
            return true;
        }
    }
    // Kills win time or the match: these holders hunt whatever enemy they see.
    if ((Mine == TEXT("final_kill_seconds") || Mine == TEXT("final_clear_out")) && Nearest && !bOwner)
    {
        Out = EngageBody(C, *Nearest);
        return true;
    }
    return false;
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
        bool bBeyondSight = true;
        for (const auto* Threat : C.Threats)
        {
            const double Distance = FVector::Dist2D(POI.FootCm, Threat->FootCm);
            ThreatDistance = FMath::Min(ThreatDistance, Distance);
            const double Radius = C.VisionRadiusCm * LDObservation::HeightRadiusScale(C.Catalog.Get().Parameters.VisionRange,
                Threat->SightGroundZ.Get(Threat->FootCm.Z),POI.SightGroundZ.Get(POI.FootCm.Z));
            bBeyondSight &= Distance > Radius;
        }
        FRank Rank;
        Rank.bAway = LeadsAway(POI);
        Rank.bBeyondSight = bBeyondSight;
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
    bool bBestMarked = false;
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
                    LDCampGrowth::Stage(C.Catalog.Get().Parameters, C.Frame.Public.FieldNowMs), Grown, Error))
            {
                bOk=true;
                const uint8 Stage=LDCampGrowth::Stage(C.Catalog.Get().Parameters,C.Frame.Public.FieldNowMs);
                for(const auto& Possible:C.Catalog.Get().Monsters)
                {
                    if(Possible.Tier!=Monster->Tier){continue;}
                    if(!LDCampGrowth::ScaleBody(C.Catalog.Get().Parameters,Possible,Stage,Grown,Error)){bOk=false;break;}
                    const auto& Skill=Possible.Skill;
                    const int64 DamageBp=Skill.Pattern==ELDMonsterPattern::Enrage
                        ?10000+(Stage>=Skill.EvolveStage?Skill.EvolvedPassiveBp:Skill.PassiveBp)
                        :((Skill.Every-1)*10000+Skill.RatioBp*Skill.Pulses+(Stage>=Skill.EchoStage?Skill.EchoRatioBp:0))/Skill.Every;
                    Grown.AttackP2=Grown.AttackP2/10000*DamageBp+Grown.AttackP2%10000*DamageBp/10000;
                    if(Skill.LowHPHasteBp>0)
                    {Grown.AttackP2=Grown.AttackP2/10000*(10000+Skill.LowHPHasteBp)+Grown.AttackP2%10000*(10000+Skill.LowHPHasteBp)/10000;}
                    if(Skill.Pattern==ELDMonsterPattern::Guard){Grown.HPP2+=Grown.HPP2/4;}
                    const auto Estimate=LDOpponentPolicy::EstimateCampCost(Input,Grown,C.Catalog);
                    const int64 Reserve=Monster->Tier==ELDDemoCampTier::Low?C.AI.LowCampReserveHPBp:C.AI.HighCampReserveHPBp;
                    bOk &= Estimate.Status==ELDCampCostStatus::Ok && RatioAtLeast(Estimate.RemainingHpP2,Self.MaxHpP2,Reserve);
                }
            }
            Feasible.Add(POI.MonsterId, bOk);
        }
        if (!Feasible.FindChecked(POI.MonsterId)) { continue; }
        bool bVisible = false;
        for (const auto* Member : CampMembers(C, POI)) { bVisible |= !Member->bReturning; }
        // #198 outer §5: a publicly marked camp first (its first clear takes the marker), then visible attackable
        // members, then the least recently visited, nearest, stable id.
        const bool bMarked = C.Frame.Public.MarkedCamps.Contains(POI.Id);
        bool bBetter = !Best;
        if (Best)
        {
            const int64 Visited = C.VisitedAt(POI.Id), BestVisited = C.VisitedAt(Best->Id);
            const double Distance = C.Distance(POI.FootCm), BestDistance = C.Distance(Best->FootCm);
            bBetter = bMarked != bBestMarked ? bMarked
                : bVisible != bBestVisible ? bVisible
                : Visited != BestVisited ? Visited < BestVisited
                : Distance != BestDistance ? Distance < BestDistance
                : POI.Id.Compare(Best->Id) < 0;
        }
        if (bBetter)
        {
            Best = &POI;
            bBestVisible = bVisible;
            bBestMarked = bMarked;
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
    Context.Stage = Frame.Public.BuildStage();
    Context.LifePhase = Frame.Self.Phase;
    return Context;
}

FLDOpponentRetryKey BuildKey(ELDOpponentBuildKind Kind)
{
    FLDOpponentRetryKey Key;
    Key.Category = ELDOpponentFailure::Build;
    Key.Slot = uint8(Kind);
    return Key;
}

// §6.4 build order: the current offer's choice first, then the card offer's pick and a blueprint into an empty slot; in a
// rest build window the replacement; a stored card into a free slot; a respec and (with the offer choice) the NPC purchase;
// then the skill points and the unspent points. Each is sent only when the growth domain would admit it now; one build request at a time. The NPC comes before
// the points because the points step spends every unspent point.
bool DecideBuild(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog, FLDOpponentPolicyState& State,
    bool bChooseOffer, FLDOpponentBuildIntent& Out)
{
    const FLDOpponentSelf& Self = Frame.Self;
    if (Frame.bBuildChangeInFlight) { return false; }
    const int64 Now = Frame.Public.NowMs;
    const uint64 Print = LDOpponentPolicy::Fingerprint(ELDOpponentFailure::Build, Self);
    const LDProgression::FBuildContext Context = BuildContext(Frame);
    FString Error;
    // Spec §13: the match's breakout goal is drawn before the first build step, so the opening active already follows it.
    if (!State.bBreakoutDrawn)
    {
        State.BreakoutGoal = LDOpponentPolicy::DrawBreakoutGoal(Self.ContenderId, Frame.Generation, Frame.Public.Roster,
            Catalog.Get().Parameters.AI);
        State.bBreakoutDrawn = true;
    }
    if (bChooseOffer && Self.Offer.IsSet()
        && LDProgression::CommandGate(ELDBuildCommandKind::ActiveChoice, Context) == ELDGrowthReason::None)
    {
        FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::ChooseActive);
        Key.Target = Self.Offer->OfferId;
        if (State.Failures.CanAttempt(Key, Print, Now) && LDOpponentPolicy::SelectActive(Self, Catalog, Out.Option, Error, State.BreakoutGoal))
        {
            Out.Kind = ELDOpponentBuildKind::ChooseActive;
            Out.OfferId = Self.Offer->OfferId;
            Out.OfferRevision = Self.Offer->OfferRevision;
            Out.Key = Key;
            return true;
        }
    }
    // Take the one-time final rule before optional rest errands can consume the ready-shortened build window.
    if (Self.FinalModOfferId.IsValid() && !Self.Offer.IsSet() && !Self.FinalModOptions.IsEmpty()
        && LDProgression::CommandGate(ELDBuildCommandKind::FinalMod, Context) == ELDGrowthReason::None)
    {
        FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::FinalMod);
        Key.Target = Self.FinalModOfferId;
        if (State.Failures.CanAttempt(Key, Print, Now))
        {
            // Every final mod is universal: a uniform pick, fixed by the server-random offer identity.
            Out = FLDOpponentBuildIntent();
            Out.Kind = ELDOpponentBuildKind::FinalMod;
            Out.OfferId = Self.FinalModOfferId;
            Out.Option = Self.FinalModOptions[int32(GetTypeHash(Self.FinalModOfferId) % uint32(Self.FinalModOptions.Num()))];
            Out.Key = Key;
            return true;
        }
    }
    // Spec §13: the card offer is always resolved by a pick (installed on its default place when a slot is free).
    // Spec §13: an offer sharing nothing with the build goes for vouchers now and then.
    if (Self.CardOffer.IsSet() && LDProgression::CommandGate(ELDBuildCommandKind::SkipCards, Context) == ELDGrowthReason::None
        && LDOpponentPolicy::WantsCardSkip(Self, Catalog))
    {
        FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::SkipCards);
        Key.Target = Self.CardOffer->OfferId;
        if (State.Failures.CanAttempt(Key, Print, Now))
        {
            Out = FLDOpponentBuildIntent();
            Out.Kind = ELDOpponentBuildKind::SkipCards;
            Out.OfferId = Self.CardOffer->OfferId;
            Out.OfferRevision = Self.CardOffer->OfferRevision;
            Out.Key = Key;
            return true;
        }
    }
    if (Self.CardOffer.IsSet() && LDProgression::CommandGate(ELDBuildCommandKind::PickCard, Context) == ELDGrowthReason::None)
    {
        FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::PickCard);
        Key.Target = Self.CardOffer->OfferId;
        FString Card;
        if (State.Failures.CanAttempt(Key, Print, Now) && LDOpponentPolicy::SelectCard(Self, Catalog, State.BreakoutGoal, Card, Error))
        {
            Out = FLDOpponentBuildIntent();
            Out.Kind = ELDOpponentBuildKind::PickCard;
            Out.OfferId = Self.CardOffer->OfferId;
            Out.OfferRevision = Self.CardOffer->OfferRevision;
            Out.Option = Card;
            // A medal is kept in the library: SelectCardInstall then gives it an element slot without swapping one out.
            FLDCardTarget Target;
            if (!LDProgression::IsMedal(Catalog.Get(), FName(*Card))
                && LDOpponentPolicy::InstalledCards(Self, Catalog) < Self.CardCapacity && LDOpponentPolicy::CardTarget(Self, Catalog, Card, Target))
            {
                Out.CardTarget = Target.Kind;
                Out.TargetId = Target.Active.IsNone() ? FString() : Target.Active.ToString();
                Out.Element = Target.Element;
            }
            Out.Key = Key;
            return true;
        }
    }
    if (!Self.Blueprints.IsEmpty()
        && LDProgression::CommandGate(ELDBuildCommandKind::LearnBlueprint, Context) == ELDGrowthReason::None)
    {
        FString Blueprint;
        uint8 Slot = 0;
        if (LDOpponentPolicy::SelectBlueprint(Self, Catalog, Blueprint, Slot, Error, State.BreakoutGoal))
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
    // #198 rest replacement: a pending blueprint that the preference ranks strictly above the lowest learned active takes
    // its slot (never while an offer is undecided: OfferOutstanding).
    if (!Self.Offer.IsSet() && !Self.Blueprints.IsEmpty()
        && LDProgression::CommandGate(ELDBuildCommandKind::ReplaceActive, Context) == ELDGrowthReason::None)
    {
        FString Blueprint;
        uint8 Slot = 0;
        if (LDOpponentPolicy::SelectReplacement(Self, Catalog, Blueprint, Slot, Error, State.BreakoutGoal))
        {
            FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::ReplaceActive);
            Key.POI = FName(*Blueprint);
            if (State.Failures.CanAttempt(Key, Print, Now))
            {
                Out = FLDOpponentBuildIntent();
                Out.Kind = ELDOpponentBuildKind::ReplaceActive;
                Out.Blueprint = Blueprint;
                Out.Slot = Slot;
                Out.Key = Key;
                return true;
            }
        }
        // Every slot learned and nothing worth replacing: the blueprint is sold for its stat point.
        else if (Self.Actives.Num() >= 3
            && LDProgression::CommandGate(ELDBuildCommandKind::SellBlueprint, Context) == ELDGrowthReason::None)
        {
            FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::SellBlueprint);
            Key.POI = FName(*Self.Blueprints[0]);
            if (State.Failures.CanAttempt(Key, Print, Now))
            {
                Out = FLDOpponentBuildIntent();
                Out.Kind = ELDOpponentBuildKind::SellBlueprint;
                Out.Blueprint = Self.Blueprints[0];
                Out.Key = Key;
                return true;
            }
        }
    }
    // A stored card goes into a free slot as soon as it has a place (a skill learned for a skill card, a slot freed by a level).
    if (LDProgression::InstallGate(Context) == ELDGrowthReason::None)
    {
        uint16 Serial = 0;
        FLDCardTarget Target;
        if (LDOpponentPolicy::SelectCardInstall(Self, Catalog, Serial, Target, State.BreakoutGoal))
        {
            FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::InstallCard);
            Key.Slot = uint8(Serial & 0xFF);
            if (State.Failures.CanAttempt(Key, Print, Now))
            {
                Out = FLDOpponentBuildIntent();
                Out.Kind = ELDOpponentBuildKind::InstallCard;
                Out.CardSerial = Serial;
                Out.CardTarget = Target.Kind;
                Out.TargetId = Target.Active.IsNone() ? FString() : Target.Active.ToString();
                Out.Element = Target.Element;
                Out.Key = Key;
                return true;
            }
        }
        // B01 T04-04: a full workshop takes off its least fitting card for a stored one that fits better; the install
        // step above then puts the better card in.
        if (LDOpponentPolicy::SelectCardSwap(Self, Catalog, State.BreakoutGoal, Serial))
        {
            FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::UninstallCard);
            Key.Slot = uint8(Serial & 0xFF);
            if (State.Failures.CanAttempt(Key, Print, Now))
            {
                Out = FLDOpponentBuildIntent();
                Out.Kind = ELDOpponentBuildKind::UninstallCard;
                Out.CardSerial = Serial;
                Out.Key = Key;
                return true;
            }
        }
    }
    // #198 rest respec: points sitting outside the stats the AI spends on come back once; the points step re-spends them.
    if (LDProgression::CommandGate(ELDBuildCommandKind::Respec, Context) == ELDGrowthReason::None)
    {
        const FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::Respec);
        if (State.Failures.CanAttempt(Key, Print, Now) && LDOpponentPolicy::WantsRespec(Self, Catalog, Error, State.BreakoutGoal))
        {
            Out = FLDOpponentBuildIntent();
            Out.Kind = ELDOpponentBuildKind::Respec;
            Out.Key = Key;
            return true;
        }
    }
    // Spec §13 rest shop: the skill point while affordable and not bought out, otherwise the best affordable card.
    if (Self.Shop.bStocked && LDProgression::CommandGate(ELDBuildCommandKind::ShopBuy, Context) == ELDGrowthReason::None)
    {
        uint8 ShopSlot = 0;
        if (LDOpponentPolicy::SelectShopBuy(Self, Catalog, State.BreakoutGoal, ShopSlot))
        {
            FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::ShopBuy);
            Key.Slot = ShopSlot;
            if (State.Failures.CanAttempt(Key, Print, Now))
            {
                Out = FLDOpponentBuildIntent();
                Out.Kind = ELDOpponentBuildKind::ShopBuy;
                Out.Slot = ShopSlot;
                Out.OfferRevision = Self.Shop.Revision;
                Out.Key = Key;
                return true;
            }
        }
    }
    // Issue #287 spec §13: a skill point goes to the learned skill that deals the most damage at once (branch A/1 on ties).
    if (Self.SkillPoints > 0
        && LDProgression::CommandGate(ELDBuildCommandKind::RankUpSkill, Context) == ELDGrowthReason::None)
    {
        const FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::RankUpSkill);
        uint8 Slot = 0;
        ELDSkillBranch Branch = ELDSkillBranch::None;
        if (State.Failures.CanAttempt(Key, Print, Now) && LDOpponentPolicy::PickRankUp(Self, Catalog, Slot, Branch, State.BreakoutGoal))
        {
            Out = FLDOpponentBuildIntent();
            Out.Kind = ELDOpponentBuildKind::RankUpSkill;
            Out.Slot = Slot;
            Out.Branch = Branch;
            Out.Key = Key;
            return true;
        }
    }
    if (Self.UnspentPoints > 0
        && LDProgression::CommandGate(ELDBuildCommandKind::AllocatePoints, Context) == ELDGrowthReason::None)
    {
        const FLDOpponentRetryKey Key = BuildKey(ELDOpponentBuildKind::AllocatePoints);
        TArray<uint8> Counts;
        if (State.Failures.CanAttempt(Key, Print, Now) && LDOpponentPolicy::AllocatePoints(Self, Catalog, Counts, Error, State.BreakoutGoal))
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
    case ELDDemoTargetMode::DirectionCone:
    case ELDDemoTargetMode::DirectionLine: return 2;
    case ELDDemoTargetMode::DirectionProjectile: return 3;
    case ELDDemoTargetMode::Self: return 4;
    case ELDDemoTargetMode::DirectionDash: return 5;
    default: return 6;
    }
}

// The engagement and self-protection roles (§6.4) a set of actives covers.
struct FRoleCover
{
    bool bEngage = false;
    bool bProtect = false;
};

bool CoverRoles(const FLDDemoContentSnapshot& Catalog, const FString& Id, FRoleCover& Cover, FString& Error)
{
    const FLDDemoAbilityDefinition* Ability = nullptr;
    if (!Catalog.FindAbility(Id, Ability, Error) || !Ability) { return false; }
    const FLDOpponentUsage Usage = LDOpponentPolicy::AbilityUsage(*Ability);
    Cover.bEngage |= Usage.bEngage;
    Cover.bProtect |= Usage.bSelfProtect;
    return true;
}

// One active's place in the §6.4 preference against the roles others cover: whether it brings a role they lack, its target
// mode, then its catalog order. Catalog order is unique per active, so Before is a strict total order.
struct FActiveRank
{
    // A skill of the match's breakout goal comes before every other consideration.
    bool bGoal = false;
    bool bGain = false;
    int32 Mode = 0;
    int32 Order = 0;
    bool Before(const FActiveRank& Other) const
    {
        return bGoal != Other.bGoal ? bGoal : bGain != Other.bGain ? bGain : Mode != Other.Mode ? Mode < Other.Mode : Order < Other.Order;
    }
};

// The goal preset's entry for an active; null when the goal does not name it.
const FLDBreakoutSkill* GoalSkill(const FLDBreakoutPreset* Preset, const FString& Id)
{
    return Preset ? Preset->Skills.FindByPredicate([&Id](const FLDBreakoutSkill& Skill) { return Id == Skill.Ability; }) : nullptr;
}

const FLDBreakoutPreset* GoalPreset(FName Goal) { return Goal.IsNone() ? nullptr : LDBreakoutPresets::Find(Goal); }

// A goal that names a stat (攒个大的: strength, 刺猬肉山: thick skin) makes it relevant and the primary keystone, the
// build's own primary becoming the secondary. Indices are the stats' design order, as RelevantStats returns them.
void ApplyGoalStat(const FLDBreakoutPreset* Preset, const FLDDemoContentSnapshot& Catalog, TArray<bool>& Relevant,
    int32& Primary, int32& Secondary)
{
    if (!Preset || !Preset->Stat || Preset->StatPoints <= 0) { return; }
    const TArray<const FLDDemoStatDefinition*> Order = Catalog.Get().StatsInDesignOrder();
    const int32 Index = Order.IndexOfByPredicate([Preset](const FLDDemoStatDefinition* Stat) { return Stat && Stat->Id == Preset->Stat; });
    if (!Relevant.IsValidIndex(Index)) { return; }
    Relevant[Index] = true;
    if (Primary != Index) { Secondary = Primary; Primary = Index; }
}

// The next branch on the way to Target from Current (None, A then A1/A2, B then B1/B2); None when Current is not on the way.
ELDSkillBranch NextBranchToward(ELDSkillBranch Current, ELDSkillBranch Target)
{
    using EB = ELDSkillBranch;
    const bool bA = Target == EB::A || Target == EB::A1 || Target == EB::A2;
    const bool bB = Target == EB::B || Target == EB::B1 || Target == EB::B2;
    if (Current == EB::None) { return bA ? EB::A : bB ? EB::B : EB::None; }
    if ((Current == EB::A && (Target == EB::A1 || Target == EB::A2)) || (Current == EB::B && (Target == EB::B1 || Target == EB::B2)))
    { return Target; }
    return EB::None;
}

bool RankActive(const FLDDemoContentSnapshot& Catalog, const FString& Id, const FRoleCover& Others, FActiveRank& Out,
    FString& Error, const FLDBreakoutPreset* Preset = nullptr)
{
    const FLDDemoAbilityDefinition* Ability = nullptr;
    if (!Catalog.FindAbility(Id, Ability, Error) || !Ability) { return false; }
    const FLDOpponentUsage Usage = LDOpponentPolicy::AbilityUsage(*Ability);
    Out.bGoal = GoalSkill(Preset, Id) != nullptr;
    Out.bGain = (Usage.bEngage && !Others.bEngage) || (Usage.bSelfProtect && !Others.bProtect);
    Out.Mode = TargetModePreference(Ability->Target.Mode);
    Out.Order = CatalogIndex(Catalog.Get().Abilities, Id);
    return true;
}

// Picks the preferred of Candidates (active ids) against the roles the learned actives and blueprints already cover.
bool PreferActive(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, const TArray<FString>& Candidates,
    FString& Out, FString& Error, const FLDBreakoutPreset* Preset = nullptr)
{
    FRoleCover Cover;
    for (const FLDOpponentActive& Active : Self.Actives) { if (!CoverRoles(Catalog, Active.DefinitionId, Cover, Error)) { return false; } }
    for (const FString& Blueprint : Self.Blueprints) { if (!CoverRoles(Catalog, Blueprint, Cover, Error)) { return false; } }
    bool bFound = false;
    FActiveRank Best;
    for (const FString& Id : Candidates)
    {
        FActiveRank Rank;
        if (!RankActive(Catalog, Id, Cover, Rank, Error, Preset)) { return false; }
        if (!bFound || Rank.Before(Best))
        {
            bFound = true;
            Best = Rank;
            Out = Id;
        }
    }
    if (!bFound) { Error = TEXT("No candidate active to prefer."); }
    return bFound;
}

// The own allocated points per stat (design order), whether AllocatePoints/WantsRespec treat each as build-relevant,
// and the primary + secondary 18-point keystone targets. Accounts for active Lv2/3 branches, AdaptiveRatioBp, and
// basic-attack/attack-speed/crit/mobility card & skill vocabulary, without excluding StatAgility.
bool RelevantStats(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, TArray<int64>& OutPoints,
    TArray<bool>& OutRelevant, int32& OutPrimaryKeystone, int32& OutSecondaryKeystone, FString& Error)
{
    OutPoints.Reset();
    OutRelevant.Reset();
    OutPrimaryKeystone = INDEX_NONE;
    OutSecondaryKeystone = INDEX_NONE;
    const auto& Content = Catalog.Get();
    if (Self.StatPoints.Num() != Content.Stats.Num())
    {
        Error = TEXT("The own stat points do not list every catalog stat.");
        return false;
    }
    const TArray<const FLDDemoStatDefinition*> StatOrder = Content.StatsInDesignOrder();
    int64 ExistingStrength = 0, ExistingBrains = 0;
    for (int32 Index = 0; Index < StatOrder.Num(); ++Index)
    {
        const FLDDemoStatDefinition& Stat = *StatOrder[Index];
        if (!Self.StatPoints[Index].StatId.Equals(Stat.Id, ESearchCase::CaseSensitive))
        {
            Error = TEXT("The own stat points are not in the stats' design order.");
            OutPoints.Reset();
            return false;
        }
        const int64 Pts = Self.StatPoints[Index].Points;
        OutPoints.Add(Pts);
        if (Stat.GetNativeId() == ELDDemoStatId::StatStrength) { ExistingStrength = Pts; }
        else if (Stat.GetNativeId() == ELDDemoStatId::StatBrains) { ExistingBrains = Pts; }
    }

    constexpr uint32 BasicSpeedCritVocab = (1u << uint32(ELDCardVocab::BasicHit))
        | (1u << uint32(ELDCardVocab::Combo3))
        | (1u << uint32(ELDCardVocab::AttackSpeed))
        | (1u << uint32(ELDCardVocab::Crit));
    constexpr uint32 MobilityVocab = (1u << uint32(ELDCardVocab::Dash))
        | (1u << uint32(ELDCardVocab::DashContact))
        | (1u << uint32(ELDCardVocab::MoveSpeed));
    constexpr uint32 CastElementVocab = (1u << uint32(ELDCardVocab::Reaction))
        | (1u << uint32(ELDCardVocab::Element))
        | (1u << uint32(ELDCardVocab::SkillCast));
    constexpr uint32 SurvivalHPVocab = (1u << uint32(ELDCardVocab::MaxHP))
        | (1u << uint32(ELDCardVocab::Heal))
        | (1u << uint32(ELDCardVocab::Shield));

    int64 ADWeight = 0;
    int64 APWeight = 0;
    int64 AdaptiveWeight = 0;
    int64 AgilityWeight = 0;
    int64 PhysiqueWeight = 2000;
    int64 SkinWeight = 1500;
    int32 BasicCardCount = 0;
    int32 CastCardCount = 0;

    for (const FLDOpponentCard& Owned : Self.Cards)
    {
        if (!Owned.IsInstalled()) { continue; }
        const FLDDemoCardDefinition* Card = Content.Cards.FindByPredicate(
            [&Owned](const FLDDemoCardDefinition& Row) { return Row.Id == Owned.DefinitionId; });
        if (!Card) { continue; }
        if ((Card->VocabMask & BasicSpeedCritVocab) != 0)
        {
            ++BasicCardCount;
            AgilityWeight += 5000;
            ADWeight += 3500;
        }
        if ((Card->VocabMask & MobilityVocab) != 0)
        {
            AgilityWeight += 2500;
        }
        if ((Card->VocabMask & CastElementVocab) != 0)
        {
            ++CastCardCount;
        }
        if ((Card->VocabMask & SurvivalHPVocab) != 0)
        {
            PhysiqueWeight += 1500;
        }
        if ((Card->VocabMask & (1u << uint32(ELDCardVocab::Armor))) != 0)
        {
            SkinWeight += 2000;
        }
    }

    for (const FLDOpponentActive& Active : Self.Actives)
    {
        const FLDDemoAbilityDefinition* Ability = nullptr;
        if (!Catalog.FindAbility(Active.DefinitionId, Ability, Error) || !Ability)
        {
            OutPoints.Reset();
            return false;
        }
        const FLDDemoAbilityBranch* Branch = Ability->FindBranch(Active.Branch);
        if (Active.Branch != ELDSkillBranch::None && !Branch)
        {
            Error = TEXT("Learned active references an unknown skill branch.");
            OutPoints.Reset();
            return false;
        }
        const FLDDemoAbilityValues& ActiveValues = Branch ? Branch->Values : Ability->Values;
        const uint32 ActiveVocab = Ability->VocabMask | (Branch ? Branch->VocabMask : 0u);
        for (const FLDDemoEffectDefinition& Effect : ActiveValues.Effects)
        {
            ADWeight += Effect.ADRatioBp;
            APWeight += Effect.APRatioBp;
            AdaptiveWeight += Effect.AdaptiveRatioBp;
            PhysiqueWeight += Effect.MaxHPRatioBp;
            SkinWeight += Effect.ArmorRatioBp;
            const ELDDemoEffectOp Op = Effect.GetOp();
            if (Op == ELDDemoEffectOp::EmpowerBasic || Op == ELDDemoEffectOp::FollowBasic || Op == ELDDemoEffectOp::BasicRider)
            {
                AgilityWeight += 4500;
                ADWeight += 2500;
            }
        }
        if ((ActiveVocab & BasicSpeedCritVocab) != 0)
        {
            AgilityWeight += 3000;
            ADWeight += 1500;
        }
    }

    if (AdaptiveWeight > 0)
    {
        const bool bPreferAPForAdaptive = APWeight > ADWeight
            || (APWeight == ADWeight && (ExistingBrains > ExistingStrength || (CastCardCount > BasicCardCount && ExistingStrength == 0)));
        if (bPreferAPForAdaptive) { APWeight += AdaptiveWeight; }
        else { ADWeight += AdaptiveWeight; }
    }

    const int64 EffectiveStrengthWeight = ADWeight > 0 ? ADWeight
        : (APWeight == 0 && AgilityWeight == 0 && Content.Character.BasicADRatioBp > 0 ? Content.Character.BasicADRatioBp : 0);

    TArray<int64, TInlineAllocator<5>> Weights;
    for (int32 Index = 0; Index < StatOrder.Num(); ++Index)
    {
        const ELDDemoStatId Id = StatOrder[Index]->GetNativeId();
        bool bRel = false;
        int64 Weight = 0;
        switch (Id)
        {
        case ELDDemoStatId::StatPhysique:
            bRel = true;
            Weight = PhysiqueWeight;
            break;
        case ELDDemoStatId::StatThickSkin:
            bRel = true;
            Weight = SkinWeight;
            break;
        case ELDDemoStatId::StatStrength:
            bRel = EffectiveStrengthWeight > 0 || BasicCardCount > 0;
            Weight = EffectiveStrengthWeight;
            break;
        case ELDDemoStatId::StatBrains:
            bRel = APWeight > 0;
            Weight = APWeight;
            break;
        case ELDDemoStatId::StatAgility:
            bRel = true;
            Weight = AgilityWeight;
            break;
        default:
            break;
        }
        OutRelevant.Add(bRel);
        Weights.Add(bRel ? Weight : -1);
    }

    for (int32 Index = 0; Index < Weights.Num(); ++Index)
    {
        if (!OutRelevant[Index]) { continue; }
        if (OutPrimaryKeystone == INDEX_NONE || Weights[Index] > Weights[OutPrimaryKeystone])
        {
            OutSecondaryKeystone = OutPrimaryKeystone;
            OutPrimaryKeystone = Index;
        }
        else if (OutSecondaryKeystone == INDEX_NONE || Weights[Index] > Weights[OutSecondaryKeystone])
        {
            OutSecondaryKeystone = Index;
        }
    }
    return true;
}

enum class EPKFight : uint8 { Ring, Fight };

// #198 D: a living entrant's PK life fights only the visible living entrants of other sides; teammates, camp members and
// anything else in its frame are neither a target nor a threat. The ring comes first: outside the safe or previewed radius
// the body walks to the public place nearest the arena centre (the capture anchor) through the ordinary order path.
// Without a visible enemy it walks there until inside the approved final ring radius, then keeps what it has.
EPKFight PKFightBody(FContext& C, const FLDPKParameters& Params, FLDOpponentBodyIntent& Out)
{
    const FLDPKView& PK = C.Frame.Public.PK;
    const FGuid& Me = C.Self.ContenderId;
    C.Enemies.RemoveAll([&PK, &Me](const FLDOpponentUnit* Unit)
    {
        const FLDPKEntrantView* Entrant = Unit->Kind == ELDOpponentUnitKind::Contender && Unit->ContenderId != Me
            ? PK.FindEntrant(Unit->ContenderId) : nullptr;
        return !Entrant || !Entrant->bAlive || LDOpponentPolicy::IsPKTeammate(PK, Me, Unit->ContenderId);
    });
    C.Threats = C.Enemies;
    C.Boss = nullptr;
    const FLDOpponentPOI* Centre = nullptr;
    for (const auto& POI : C.Frame.POIs)
    {
        const double Distance = FVector::Dist2D(POI.FootCm, PK.ArenaCentreCm);
        const double BestDistance = Centre ? FVector::Dist2D(Centre->FootCm, PK.ArenaCentreCm) : 0.;
        if (!Centre || Distance < BestDistance || (Distance == BestDistance && POI.Id.Compare(Centre->Id) < 0)) { Centre = &POI; }
    }
    const auto WalkToCentre = [&C, Centre]()
    {
        return Centre && C.CanAttempt(FContext::PathKey(Centre->Id)) ? MoveTo(C, *Centre) : Hold(C);
    };
    if (LDOpponentPolicy::PKRingRetreat(Params, PK, C.Self.FootCm, C.Now))
    {
        ForgetTarget(C.State);
        Out = WalkToCentre();
        return EPKFight::Ring;
    }
    // An enemy seen only through a teammate or an exposure cannot be attacked yet: an attack on it would be refused as
    // unavailable and, while it stays in view, never be tried again. Walk at it instead until the own sight takes it.
    const FLDOpponentUnit* Target = nullptr;
    const FLDOpponentUnit* Seen = nullptr;
    // In the arena an enemy stays in view, so its handle never renews: a handle suppressed after one unavailable answer
    // (sight lost between the decision and the order) would stay out of the fight for good. The authority resolving it for
    // an attack in this very frame outweighs that memory.
    for (const auto* Unit : C.Enemies)
    {
        if (Unit->bProtected) { continue; }
        const double Distance = C.Distance(Unit->FootCm);
        const FLDOpponentUnit*& Best = Unit->bTargetable ? Target : Seen;
        if (!Best || Distance < C.Distance(Best->FootCm)
            || (Distance == C.Distance(Best->FootCm) && Unit->Handle < Best->Handle)) { Best = Unit; }
    }
    if (Target)
    {
        if (HandleSuppressed(C, *Target)) { C.State.Failures.RecordSuccess(FContext::EngagementKey(NAME_None, Target->Handle)); }
        Out = EngageBody(C, *Target);
        return EPKFight::Fight;
    }
    ForgetTarget(C.State);
    if (Seen && Centre)
    {
        const auto& Order = C.Frame.Order;
        const FLDOpponentRetryKey Key = FContext::PathKey(TEXT("pk_approach"));
        if (Order.IsLive() && Order.Kind == ELDOpponentOrderKind::MoveGround && Order.POI.IsNone()) { Out = Hold(C); }
        else if (!C.CanWalk() || !C.CanAttempt(Key)) { Out = Hold(C); }
        else
        {
            Out = FLDOpponentBodyIntent();
            Out.Kind = EBody::MoveGround;
            Out.Point = Seen->FootCm;
            Out.Surface = Centre->SurfaceId;
            Out.Key = Key;
        }
        return EPKFight::Fight;
    }
    Out = C.Distance(PK.ArenaCentreCm) > Params.FinalRadiusCm ? WalkToCentre() : Hold(C);
    return EPKFight::Fight;
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

bool SelectActive(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FString& Out, FString& Error, FName Goal)
{
    using namespace Detail;
    Out.Reset();
    if (!Self.Offer.IsSet() || Self.Offer->Options.IsEmpty())
    {
        Error = TEXT("No current active offer.");
        return false;
    }
    return PreferActive(Self, Catalog, Self.Offer->Options, Out, Error, GoalPreset(Goal));
}

bool SelectBlueprint(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FString& OutBlueprint, uint8& OutSlot,
    FString& Error, FName Goal)
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
    return PreferActive(Self, Catalog, Self.Blueprints, OutBlueprint, Error, GoalPreset(Goal));
}

bool SelectReplacement(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FString& OutBlueprint, uint8& OutSlot,
    FString& Error, FName Goal)
{
    using namespace Detail;
    const FLDBreakoutPreset* Preset = GoalPreset(Goal);
    OutBlueprint.Reset();
    OutSlot = 0;
    const FLDOpponentActive* BySlot[3] = {nullptr, nullptr, nullptr};
    for (const FLDOpponentActive& Active : Self.Actives) { if (Active.Slot < 3) { BySlot[Active.Slot] = &Active; } }
    if (!BySlot[0] || !BySlot[1] || !BySlot[2])
    {
        Error = TEXT("A Q/W/E slot is empty: a blueprint is learned into it, never replacing an active.");
        return false;
    }
    if (Self.Blueprints.IsEmpty())
    {
        Error = TEXT("No pending blueprint.");
        return false;
    }
    // The learned active ranked lowest against the roles of the other two, and those two roles.
    int32 Lowest = INDEX_NONE;
    FActiveRank LowestRank;
    FRoleCover Kept;
    for (int32 Slot = 0; Slot < 3; ++Slot)
    {
        FRoleCover Others;
        for (int32 Other = 0; Other < 3; ++Other)
        {
            if (Other != Slot && !CoverRoles(Catalog, BySlot[Other]->DefinitionId, Others, Error)) { return false; }
        }
        FActiveRank Rank;
        if (!RankActive(Catalog, BySlot[Slot]->DefinitionId, Others, Rank, Error, Preset)) { return false; }
        if (Lowest == INDEX_NONE || LowestRank.Before(Rank))
        {
            Lowest = Slot;
            LowestRank = Rank;
            Kept = Others;
        }
    }
    bool bFound = false;
    FActiveRank BestRank;
    for (const FString& Blueprint : Self.Blueprints)
    {
        FActiveRank Rank;
        if (!RankActive(Catalog, Blueprint, Kept, Rank, Error, Preset)) { return false; }
        if (!bFound || Rank.Before(BestRank))
        {
            bFound = true;
            BestRank = Rank;
            OutBlueprint = Blueprint;
        }
    }
    if (!BestRank.Before(LowestRank))
    {
        OutBlueprint.Reset();
        Error = TEXT("No pending blueprint ranks above the lowest learned active.");
        return false;
    }
    OutSlot = uint8(Lowest);
    return true;
}

bool AllocatePoints(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, TArray<uint8>& Out, FString& Error, FName Goal)
{
    using namespace Detail;
    Out.Reset();
    if (Self.UnspentPoints <= 0)
    {
        Error = TEXT("No unspent points.");
        return false;
    }
    TArray<int64> Points;
    TArray<bool> Relevant;
    int32 PrimaryKeystone = INDEX_NONE;
    int32 SecondaryKeystone = INDEX_NONE;
    if (!RelevantStats(Self, Catalog, Points, Relevant, PrimaryKeystone, SecondaryKeystone, Error)) { return false; }
    ApplyGoalStat(GoalPreset(Goal), Catalog, Relevant, PrimaryKeystone, SecondaryKeystone);
    Out.SetNumZeroed(Catalog.Get().Stats.Num());
    int64 Left = Self.UnspentPoints;
    const int64 KeystoneThreshold = Catalog.Get().Parameters.KeystoneThresholdPoints;
    if (KeystoneThreshold <= 0)
    {
        Error = TEXT("The catalog has no valid keystone threshold.");
        Out.Reset();
        return false;
    }
    if (!Points.IsValidIndex(PrimaryKeystone) || !Relevant[PrimaryKeystone])
    {
        Error = TEXT("The catalog has no relevant stat for a keystone.");
        Out.Reset();
        return false;
    }
    // Secure the primary 18-point keystone first, then focus the secondary core stat toward its 18-point keystone
    // before distributing surplus late-game points across the remaining relevant stats.
    while (Left > 0 && Points.IsValidIndex(PrimaryKeystone) && Points[PrimaryKeystone] < KeystoneThreshold
        && Out[PrimaryKeystone] < MAX_uint8)
    {
        ++Points[PrimaryKeystone];
        ++Out[PrimaryKeystone];
        --Left;
    }
    while (Left > 0 && Points.IsValidIndex(SecondaryKeystone) && SecondaryKeystone != PrimaryKeystone
        && Relevant[SecondaryKeystone] && Points[SecondaryKeystone] < KeystoneThreshold
        && Out[SecondaryKeystone] < MAX_uint8)
    {
        ++Points[SecondaryKeystone];
        ++Out[SecondaryKeystone];
        --Left;
    }
    for (; Left > 0; --Left)
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

bool WantsRespec(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FString& Error, FName Goal)
{
    using namespace Detail;
    TArray<int64> Points;
    TArray<bool> Relevant;
    int32 PrimaryKeystone = INDEX_NONE;
    int32 SecondaryKeystone = INDEX_NONE;
    if (!RelevantStats(Self, Catalog, Points, Relevant, PrimaryKeystone, SecondaryKeystone, Error)) { return false; }
    ApplyGoalStat(GoalPreset(Goal), Catalog, Relevant, PrimaryKeystone, SecondaryKeystone);
    for (int32 Index = 0; Index < Points.Num(); ++Index)
    {
        if (Points[Index] > 0 && !Relevant[Index]) { return true; }
    }
    Error = TEXT("Every allocated point sits in a stat the AI spends on.");
    return false;
}


FName DrawBreakoutGoal(const FGuid& Contender, uint32 Generation, const TArray<FGuid>& Roster, const FLDDemoAIParameters& AI)
{
    TArray<const FLDBreakoutPreset*> Presets;
    for (const FLDBreakoutPreset& Preset : LDBreakoutPresets::All()) { if (Preset.bAIGoal) { Presets.Add(&Preset); } }
    if (Presets.IsEmpty() || AI.BreakoutChanceBp <= 0) { return NAME_None; }
    // One deterministic draw per contender and match: the chance roll comes from its own hash.
    const uint32 Hash = HashCombine(GetTypeHash(Contender), GetTypeHash(Generation));
    if (int64(Hash % 10000) >= AI.BreakoutChanceBp) { return NAME_None; }
    // The match rotates the presets and each roster seat takes the next one, so a room's AIs take different goals while
    // there are presets for them; a contender off the roster draws from its own hash.
    const int32 Seat = Roster.IndexOfByKey(Contender);
    const uint32 Offset = HashCombine(GetTypeHash(Generation), 0x42524b4fu);
    const uint32 Pick = Seat == INDEX_NONE ? Hash / 10000 : Offset + uint32(Seat);
    return FName(Presets[Pick % uint32(Presets.Num())]->Id);
}

void BreakoutProgress(const FLDOpponentSelf& Self, FName Goal, int32& OutSkills, int32& OutSkillsTotal, int32& OutCards,
    int32& OutCardsTotal)
{
    OutSkills = OutSkillsTotal = OutCards = OutCardsTotal = 0;
    const FLDBreakoutPreset* Preset = Detail::GoalPreset(Goal);
    if (!Preset) { return; }
    OutSkillsTotal = Preset->Skills.Num();
    for (const FLDBreakoutSkill& Wanted : Preset->Skills)
    {
        OutSkills += Self.Actives.ContainsByPredicate([&Wanted](const FLDOpponentActive& Active)
            { return Active.DefinitionId == Wanted.Ability && Active.Branch == Wanted.Branch; }) ? 1 : 0;
    }
    OutCardsTotal = Preset->Cards.Num();
    TArray<bool> Used;
    Used.SetNumZeroed(Self.Cards.Num());
    for (const FLDBreakoutCard& Wanted : Preset->Cards)
    {
        for (int32 Index = 0; Index < Self.Cards.Num(); ++Index)
        {
            if (!Used[Index] && Self.Cards[Index].IsInstalled() && Self.Cards[Index].DefinitionId == Wanted.Card)
            {
                Used[Index] = true;
                ++OutCards;
                break;
            }
        }
    }
}

namespace
{
// The own build as growth reads it for a card's place: the learned actives (slot and branch) on a match-start build.
FLDGrowthBuild OpponentBuild(const FLDOpponentSelf& Self, const FLDDemoContentDefinitions& Content)
{
    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Level = Self.Level;
    for (const FLDOpponentActive& Active : Self.Actives) { Build.Actives.Add({FName(*Active.DefinitionId), Active.Slot, Active.Branch}); }
    LDProgression::NormalizeBuild(Build);
    return Build;
}

// The vocabulary keys of the installed cards and learned skills (spec §6.3).
uint32 OpponentVocab(const FLDOpponentSelf& Self, const FLDDemoContentDefinitions& Content)
{
    uint32 Vocab = 0;
    for (const FLDOpponentCard& Owned : Self.Cards)
    {
        if (!Owned.IsInstalled()) { continue; }
        if (const FLDDemoCardDefinition* Card = Content.Cards.FindByPredicate([&Owned](const FLDDemoCardDefinition& Row) { return Row.Id == Owned.DefinitionId; }))
        { Vocab |= Card->VocabMask; }
    }
    for (const FLDOpponentActive& Active : Self.Actives)
    {
        if (const FLDDemoAbilityDefinition* Ability = Content.Abilities.FindByPredicate([&Active](const FLDDemoAbilityDefinition& Row) { return Row.Id == Active.DefinitionId; }))
        {
            const FLDDemoAbilityBranch* Branch = Ability->FindBranch(Active.Branch);
            Vocab |= Ability->VocabMask | (Branch ? Branch->VocabMask : 0u);
        }
    }
    return Vocab;
}

// How well an owned card fits the build for keeping it installed: AI.CardVocabScore per vocabulary key it shares with the
// learned skills, plus AI.BreakoutCardScore for a card of Goal. It reads the skills only, not the installed cards, so
// swapping cards never changes another card's fit.
double OpponentCardFit(const FLDOpponentSelf& Self, const FLDDemoContentDefinitions& Content, FName Goal, const FString& Id)
{
    const FLDDemoCardDefinition* Card = Content.Cards.FindByPredicate([&Id](const FLDDemoCardDefinition& Row) { return Row.Id == Id; });
    if (!Card) { return -1.; }
    uint32 Vocab = 0;
    for (const FLDOpponentActive& Active : Self.Actives)
    {
        if (const FLDDemoAbilityDefinition* Ability = Content.Abilities.FindByPredicate([&Active](const FLDDemoAbilityDefinition& Row) { return Row.Id == Active.DefinitionId; }))
        {
            const FLDDemoAbilityBranch* Branch = Ability->FindBranch(Active.Branch);
            Vocab |= Ability->VocabMask | (Branch ? Branch->VocabMask : 0u);
        }
    }
    const FLDDemoAIParameters& AI = Content.Parameters.AI;
    const FLDBreakoutPreset* Preset = Goal.IsNone() ? nullptr : LDBreakoutPresets::Find(Goal);
    return double(AI.CardVocabScore) * double(FMath::CountBits64(uint64(Card->VocabMask & Vocab)))
        + (Preset && LDBreakoutPresets::HasCard(*Preset, FName(*Id)) ? double(AI.BreakoutCardScore) : 0.);
}
}

bool SelectCard(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FName Goal, FString& OutCard, FString& Error)
{
    OutCard.Reset();
    if (!Self.CardOffer.IsSet() || Self.CardOffer->Options.IsEmpty()) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog.Get();
    const FLDDemoAIParameters& AI = Content.Parameters.AI;
    const uint32 Vocab = OpponentVocab(Self, Content);
    const FLDBreakoutPreset* Preset = Goal.IsNone() ? nullptr : LDBreakoutPresets::Find(Goal);
    double Best = -1.;
    for (const FString& Option : Self.CardOffer->Options)
    {
        const FLDDemoCardDefinition* Card = Content.Cards.FindByPredicate([&Option](const FLDDemoCardDefinition& Row) { return Row.Id == Option; });
        if (!Card)
        {
            Error = FString::Printf(TEXT("Card option %s is not a catalog card."), *Option);
            return false;
        }
        // The jitter in [0, 1): a deterministic draw of this offer and card, so a retry picks the same card.
        const uint32 Hash = HashCombine(GetTypeHash(Self.CardOffer->OfferId), GetTypeHash(Option));
        double Score = double(AI.CardVocabScore) * double(FMath::CountBits64(uint64(Card->VocabMask & Vocab))) + double(Hash % 10000) / 10000.;
        if (Preset && LDBreakoutPresets::HasCard(*Preset, FName(*Option))) { Score += double(AI.BreakoutCardScore); }
        if (Score > Best)
        {
            Best = Score;
            OutCard = Option;
        }
    }
    return !OutCard.IsEmpty();
}

bool WantsCardSkip(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog)
{
    if (!Self.CardOffer.IsSet() || Self.CardOffer->Options.IsEmpty()) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog.Get();
    const uint32 Vocab = OpponentVocab(Self, Content);
    for (const FString& Option : Self.CardOffer->Options)
    {
        const FLDDemoCardDefinition* Card = Content.Cards.FindByPredicate([&Option](const FLDDemoCardDefinition& Row) { return Row.Id == Option; });
        if (!Card || (Card->VocabMask & Vocab) != 0) { return false; }
    }
    // A deterministic draw of the offer, so a retry decides the same.
    const uint32 Hash = HashCombine(GetTypeHash(Self.CardOffer->OfferId), 0x534b4950u);
    return int64(Hash % 10000) < Content.Parameters.AI.CardSkipChanceBp;
}

bool SelectShopBuy(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FName Goal, uint8& OutSlot)
{
    OutSlot = 0;
    const FLDShopView& Shop = Self.Shop;
    if (!Shop.bStocked) { return false; }
    // Spec §13: the skill point when the unspent stat points pay it and it is not bought out, then the random medal likewise,
    // otherwise the best affordable card.
    if (Shop.SkillPointsBought < Shop.SkillPointsMax && Shop.SkillPointPrice > 0 && Self.UnspentPoints >= Shop.SkillPointPrice)
    {
        OutSlot = FLDBuildCommand::ShopSkillPointSlot;
        return true;
    }
    if (Shop.MedalsBought < Shop.MedalsMax && Shop.MedalPrice > 0 && Self.UnspentPoints >= Shop.MedalPrice)
    {
        OutSlot = FLDBuildCommand::ShopMedalSlot;
        return true;
    }
    const FLDDemoContentDefinitions& Content = Catalog.Get();
    const FLDDemoAIParameters& AI = Content.Parameters.AI;
    const uint32 Vocab = OpponentVocab(Self, Content);
    const FLDBreakoutPreset* Preset = Goal.IsNone() ? nullptr : LDBreakoutPresets::Find(Goal);
    double Best = -1.;
    for (int32 Index = 0; Index < Shop.Slots.Num(); ++Index)
    {
        const FLDShopSlotView& Slot = Shop.Slots[Index];
        if (Slot.Kind != ELDShopItemKind::Card || Slot.bSold || Slot.Price <= 0 || Self.UnspentPoints < Slot.Price) { continue; }
        const FLDDemoCardDefinition* Card = Content.Cards.FindByPredicate(
            [&Slot](const FLDDemoCardDefinition& Row) { return FName(*Row.Id) == Slot.DefinitionId; });
        if (!Card) { continue; }
        const uint32 Hash = HashCombine(GetTypeHash(Shop.Revision), GetTypeHash(Slot.DefinitionId));
        double Score = double(AI.CardVocabScore) * double(FMath::CountBits64(uint64(Card->VocabMask & Vocab))) + double(Hash % 10000) / 10000.;
        if (Preset && LDBreakoutPresets::HasCard(*Preset, Slot.DefinitionId)) { Score += double(AI.BreakoutCardScore); }
        if (Score > Best)
        {
            Best = Score;
            OutSlot = uint8(Index);
        }
    }
    return Best >= 0.;
}

bool CardTarget(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, const FString& Card, FLDCardTarget& Out)
{
    const FLDDemoContentDefinitions& Content = Catalog.Get();
    return LDProgression::DefaultCardTarget(Content, OpponentBuild(Self, Content), FName(*Card), Out);
}

int32 InstalledCards(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog)
{
    const FLDDemoContentDefinitions& Content = Catalog.Get();
    return Self.Cards.FilterByPredicate([&Content](const FLDOpponentCard& Owned)
        { return Owned.IsInstalled() && !LDProgression::IsMedal(Content, FName(*Owned.DefinitionId)); }).Num();
}

namespace
{
// The element slot a stored medal goes into: a host whose same medal can still fuse, else the first empty one (Q/W/E, then
// the body). Never a swap, which would hand the swapped medal back to be installed again.
bool OpponentMedalTarget(const FLDOpponentSelf& Self, const FLDDemoContentDefinitions& Content, const FLDOpponentCard& Medal,
    FLDCardTarget& Out)
{
    const uint8 MaxTier = uint8(FMath::Clamp(Content.Parameters.Medal.MaxTier, 1, 3));
    TArray<FLDCardTarget> Hosts;
    for (const FLDOpponentActive& Active : Self.Actives)
    {
        FLDCardTarget& Host = Hosts.AddDefaulted_GetRef();
        Host.Kind = ELDCardTargetKind::Skill;
        Host.Active = FName(*Active.DefinitionId);
    }
    Hosts.AddDefaulted_GetRef().Kind = ELDCardTargetKind::Body;
    const FLDCardTarget* Empty = nullptr;
    for (const FLDCardTarget& Host : Hosts)
    {
        const FLDOpponentCard* Held = Self.Cards.FindByPredicate([&](const FLDOpponentCard& Owned)
        {
            return Owned.TargetKind == Host.Kind && FName(*Owned.TargetActive) == Host.Active
                && LDProgression::IsMedal(Content, FName(*Owned.DefinitionId));
        });
        if (!Held) { Empty = Empty ? Empty : &Host; continue; }
        if (Held->DefinitionId == Medal.DefinitionId && Held->Tier < MaxTier) { Out = Host; return true; }
    }
    if (Empty) { Out = *Empty; }
    return Empty != nullptr;
}
}

bool SelectCardInstall(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, uint16& OutSerial, FLDCardTarget& OutTarget,
    FName Goal)
{
    const FLDDemoContentDefinitions& Content = Catalog.Get();
    const bool bFull = InstalledCards(Self, Catalog) >= Self.CardCapacity;
    double Best = -1.;
    for (const FLDOpponentCard& Owned : Self.Cards)
    {
        if (Owned.IsInstalled()) { continue; }
        FLDCardTarget Target;
        if (LDProgression::IsMedal(Content, FName(*Owned.DefinitionId)))
        {
            // A medal takes no capacity slot and goes first.
            if (OpponentMedalTarget(Self, Content, Owned, Target)) { OutSerial = Owned.Serial; OutTarget = Target; return true; }
            continue;
        }
        if (bFull || !CardTarget(Self, Catalog, Owned.DefinitionId, Target)) { continue; }
        // B01 T04-04: the most fitting stored card takes a free slot, so a swap never puts the card it took off back.
        const double Score = OpponentCardFit(Self, Content, Goal, Owned.DefinitionId);
        if (Score > Best)
        {
            Best = Score;
            OutSerial = Owned.Serial;
            OutTarget = Target;
        }
    }
    return Best >= 0.;
}

bool SelectCardSwap(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FName Goal, uint16& OutSerial)
{
    const FLDDemoContentDefinitions& Content = Catalog.Get();
    if (Self.CardCapacity <= 0 || InstalledCards(Self, Catalog) < Self.CardCapacity) { return false; }
    double BestStored = -1.;
    for (const FLDOpponentCard& Owned : Self.Cards)
    {
        FLDCardTarget Target;
        if (Owned.IsInstalled() || LDProgression::IsMedal(Content, FName(*Owned.DefinitionId))
            || !CardTarget(Self, Catalog, Owned.DefinitionId, Target)) { continue; }
        BestStored = FMath::Max(BestStored, OpponentCardFit(Self, Content, Goal, Owned.DefinitionId));
    }
    double Lowest = TNumericLimits<double>::Max();
    for (const FLDOpponentCard& Owned : Self.Cards)
    {
        if (!Owned.IsInstalled() || LDProgression::IsMedal(Content, FName(*Owned.DefinitionId))) { continue; }
        const double Fit = OpponentCardFit(Self, Content, Goal, Owned.DefinitionId);
        if (Fit < Lowest)
        {
            Lowest = Fit;
            OutSerial = Owned.Serial;
        }
    }
    return BestStored >= 0. && BestStored > Lowest;
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

bool SignupWindowOpen(const FLDOpponentPublicMatch& Public)
{
    const FLDTimelineView& Timeline = Public.Timeline;
    return Public.Phase == ELDMatchPhase::Rest && Timeline.Stage == ELDMatchStage::Rest && Timeline.Step == ELDRestStep::Build
        && Timeline.SignupOpensAtMs > 0 && Public.NowMs >= Timeline.SignupOpensAtMs && Public.NowMs < Timeline.StepEndsAtMs;
}

bool WantsPKSignUp(const FLDOpponentPublicMatch& Public, const FGuid& Self)
{
    return SignupWindowOpen(Public) && Self.IsValid() && !Public.Timeline.Signups.Contains(Self);
}

uint8 PKOwnBetSide(const FLDPKView& PK, const FGuid& Self)
{
    const FLDPKBetView* Bet = PK.Bets.FindByPredicate([&Self](const FLDPKBetView& B) { return B.Spectator == Self; });
    return Bet ? Bet->Side : 0;
}

uint8 PKBetSide(const FLDOpponentPublicMatch& Public, const FGuid& Self)
{
    const FLDPKView& PK = Public.PK;
    if (Public.Phase != ELDMatchPhase::Rest || Public.Timeline.Step != ELDRestStep::Betting || PK.Serial == 0
        || !Self.IsValid() || !PK.IsBettor(Self) || PKOwnBetSide(PK, Self) != 0 || PK.Entrants.IsEmpty())
    {
        return 0;
    }
    return PK.Entrants[0].Side;
}

const FLDOpponentUnit* PKThrowTarget(const FLDPKView& PK, uint8 BetSide, const TArray<FLDOpponentUnit>& Units,
    double ThrowRangeCm)
{
    if (BetSide == 0) { return nullptr; }
    for (const FLDPKEntrantView& Entrant : PK.Entrants)
    {
        if (!Entrant.bAlive || Entrant.Side == 0 || Entrant.Side == BetSide) { continue; }
        const FLDOpponentUnit* Seen = Units.FindByPredicate([&Entrant, &PK, ThrowRangeCm](const FLDOpponentUnit& Unit)
        {
            return Unit.Kind == ELDOpponentUnitKind::Contender && Unit.ContenderId == Entrant.Contender
                && FVector::Dist2D(Unit.FootCm, PK.ThrowAnchorCm) <= ThrowRangeCm;
        });
        if (Seen) { return Seen; }
    }
    return nullptr;
}

bool IsPKTeammate(const FLDPKView& PK, const FGuid& Self, const FGuid& Other)
{
    if (Self == Other) { return false; }
    const FLDPKEntrantView* Mine = PK.FindEntrant(Self);
    const FLDPKEntrantView* Theirs = PK.FindEntrant(Other);
    return Mine && Theirs && Mine->Side != 0 && Mine->Side == Theirs->Side;
}

bool PKRingRetreat(const FLDPKParameters& Params, const FLDPKView& PK, const FVector& FootCm, int64 NowMs)
{
    if (PK.FightStartedAtMs <= 0 || NowMs < PK.FightStartedAtMs) { return false; }
    const int64 ElapsedMs = NowMs - PK.FightStartedAtMs;
    const double DistanceCm = FVector::Dist2D(FootCm, PK.ArenaCentreCm);
    if (DistanceCm > LDPKRules::RingRadiusCm(Params, ElapsedMs)) { return true; }
    double PreviewCm = 0;
    int64 PreviewAtMs = 0;
    return LDPKRules::RingPreview(Params, ElapsedMs, PreviewCm, PreviewAtMs) && DistanceCm > PreviewCm;
}

const FLDOpponentUnit* TrainingDummy(const FLDOpponentDecisionFrame& Frame)
{
    const TArray<FGuid>& Roster = Frame.Public.Roster;
    if (Frame.Public.Phase != ELDMatchPhase::Rest || !Frame.Self.ContenderId.IsValid() || !Roster.Contains(Frame.Self.ContenderId))
    {
        return nullptr;
    }
    const FLDOpponentUnit* Out = nullptr;
    for (const FLDOpponentUnit& Unit : Frame.Units)
    {
        if (Unit.Kind != ELDOpponentUnitKind::Contender || !Unit.ContenderId.IsValid() || Roster.Contains(Unit.ContenderId)) { continue; }
        if (!Out || Unit.Handle < Out->Handle) { Out = &Unit; }
    }
    return Out;
}


bool PickRankUp(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, uint8& OutSlot, ELDSkillBranch& OutBranch,
    FName Goal)
{
    // A learned goal skill below its goal branch comes first, along the goal's path (preset order).
    if (const FLDBreakoutPreset* Preset = Detail::GoalPreset(Goal))
    {
        for (const FLDBreakoutSkill& Wanted : Preset->Skills)
        {
            const FLDOpponentActive* Active = Self.Actives.FindByPredicate([&Wanted](const FLDOpponentActive& Each)
                { return Each.DefinitionId == Wanted.Ability; });
            const ELDSkillBranch Next = Active ? Detail::NextBranchToward(Active->Branch, Wanted.Branch) : ELDSkillBranch::None;
            if (Next != ELDSkillBranch::None)
            {
                OutSlot = Active->Slot;
                OutBranch = Next;
                return true;
            }
        }
    }
    // The damage one release of each learned skill deals now (its base plus the own AD/AP shares of every damage package).
    int64 Best = -1;
    bool bFound = false;
    for (const FLDOpponentActive& Active : Self.Actives)
    {
        if (LDSkillBranch::RankOf(Active.Branch) >= 3) { continue; }
        const FLDDemoAbilityDefinition* Ability = Catalog.Get().Abilities.FindByPredicate([&Active](const FLDDemoAbilityDefinition& Row)
        { return Row.Id == Active.DefinitionId; });
        if (!Ability) { continue; }
        const FLDDemoAbilityBranch* Variant = Ability->FindBranch(Active.Branch);
        const FLDDemoAbilityValues& Values = Variant ? Variant->Values : Ability->Values;
        int64 Damage = 0;
        for (const FLDDemoEffectDefinition& Effect : Values.Effects)
        {
            const ELDDemoEffectOp Op = Effect.GetOp();
            if (Op != ELDDemoEffectOp::Damage && Op != ELDDemoEffectOp::ZoneDamage && Op != ELDDemoEffectOp::EmpowerBasic) { continue; }
            Damage += Effect.BaseP2 + Effect.ADRatioBp * Self.Attributes.ADP2 / 10000 + Effect.APRatioBp * Self.Attributes.APP2 / 10000;
        }
        if (Damage > Best || (Damage == Best && bFound && Active.Slot < OutSlot))
        {
            Best = Damage;
            OutSlot = Active.Slot;
            bFound = true;
            // Spec §13: the next branch sharing more vocabulary keys with the installed cards; A (or the first of the pair) on
            // ties.
            const ELDSkillBranch First = Active.Branch == ELDSkillBranch::None ? ELDSkillBranch::A
                : Active.Branch == ELDSkillBranch::A ? ELDSkillBranch::A1 : ELDSkillBranch::B1;
            const ELDSkillBranch Second = Active.Branch == ELDSkillBranch::None ? ELDSkillBranch::B
                : Active.Branch == ELDSkillBranch::A ? ELDSkillBranch::A2 : ELDSkillBranch::B2;
            uint32 CardVocab = 0;
            for (const FLDOpponentCard& Owned : Self.Cards)
            {
                if (!Owned.IsInstalled()) { continue; }
                if (const FLDDemoCardDefinition* Card = Catalog.Get().Cards.FindByPredicate([&Owned](const FLDDemoCardDefinition& Row) { return Row.Id == Owned.DefinitionId; }))
                { CardVocab |= Card->VocabMask; }
            }
            const auto Shared = [Ability, CardVocab](ELDSkillBranch Path)
            {
                const FLDDemoAbilityBranch* Branch = Ability->FindBranch(Path);
                return Branch ? FMath::CountBits64(uint64(Branch->VocabMask & CardVocab)) : -1;
            };
            OutBranch = Shared(Second) > Shared(First) ? Second : First;
        }
    }
    return bFound;
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
    // Rest builds use the human's commands after this adapter's drawn moment and only in the open build window.
    const bool bRest = Frame.Public.Phase == ELDMatchPhase::Rest;
    if (!Frame.Generation || !(bRunning || bRest) || !Self.ContenderId.IsValid()
        || (Self.Phase != ELDLifePhase::Alive && Self.Phase != ELDLifePhase::Dead))
    {
        ForgetBody(State);
        return Finish(EMode::Stopped, EReason::Stopped);
    }
    if (bRest)
    {
        const FLDTimelineView& Timeline = Frame.Public.Timeline;
        const FLDPKView& PK = Frame.Public.PK;
        const bool bFightRunning = Timeline.Step == ELDRestStep::PK && PK.Serial != 0 && PK.Outcome == ELDPKOutcome::Running;
        // #198 D: a living entrant's PK life fights in the arena while the PK step runs (its inputs are open only then).
        const FLDPKEntrantView* Entrant = PK.Serial != 0 ? PK.FindEntrant(Self.ContenderId) : nullptr;
        if (bFightRunning && Entrant && Entrant->bAlive && Self.Phase == ELDLifePhase::Alive)
        {
            FLDPKParameters Ring;
            FString RingError;
            FContext C(Frame, Catalog, State);
            if (!FLDPKParameters::FromContent(Catalog.Get().Parameters, Ring, RingError) || !C.Initialize())
            {
                ForgetBody(State);
                return Finish(EMode::Stopped, EReason::ProviderUnavailable);
            }
            const EPKFight Fight = PKFightBody(C, Ring, Out.Body);
            if (Self.bChanneling || (Frame.Order.IsLive() && Frame.Order.Kind == ELDOpponentOrderKind::Action))
            {
                Out.Body = FLDOpponentBodyIntent();
                Out.Body.Kind = EBody::Keep;
            }
            return Finish(EMode::PK, Fight == EPKFight::Ring ? EReason::PKRing : EReason::PKFight);
        }
        // Everyone else stays in the shared rest room; spectating sends no order while the arena runs.
        ForgetBody(State);
        // #198 D: at most one PK request per decision (a bet once per PK); throws wait for the own cooldown from the latest
        // throw answer. Standing in the shared disk is the sign-up the authority judges from the committed foot,
        // so while the window is open the body walks into it and then stays there (no training) until the lock; an accepted
        // action or a channel finishes first.
        EReason PKReason = EReason::None;
        if (SignupWindowOpen(Frame.Public) && Frame.SignupPOI.IsSet() && Self.Phase == ELDLifePhase::Alive)
        {
            const FLDOpponentPOI& Disk = Frame.SignupPOI.GetValue();
            const auto& Order = Frame.Order;
            const bool bWalking = Order.IsLive() && Order.Kind == ELDOpponentOrderKind::MoveGround && Order.POI == Disk.Id;
            const bool bActing = Self.bChanneling || (Order.IsLive() && Order.Kind == ELDOpponentOrderKind::Action);
            if (bWalking || bActing) { Out.Body.Kind = EBody::Keep; }
            else if (Frame.Public.Timeline.Signups.Contains(Self.ContenderId))
            {
                Out.Body.Kind = Order.IsLive() ? EBody::Stop : EBody::Keep;
            }
            else
            {
                Out.Body.POI = Disk.Id;
                Out.Body.Key = FContext::PathKey(Disk.Id);
                Out.Body.Kind = EBody::MoveGround;
            }
            PKReason = EReason::PKSignUp;
        }
        else if (const uint8 Side = PKBetSide(Frame.Public, Self.ContenderId); Side != 0 && State.PKBetSerial != PK.Serial)
        {
            State.PKBetSerial = PK.Serial;
            Out.PK.Kind = ELDOpponentPKKind::Bet;
            Out.PK.Serial = PK.Serial;
            Out.PK.Side = Side;
            PKReason = EReason::PKBet;
        }
        else if (bFightRunning && PK.IsBettor(Self.ContenderId) && Frame.Public.NowMs >= Frame.PKThrowReadyAtMs)
        {
            if (const FLDOpponentUnit* Target = PKThrowTarget(PK, PKOwnBetSide(PK, Self.ContenderId), Frame.Units,
                ToDouble(Catalog.Get().Parameters.ThrowRangeCm)))
            {
                Out.PK.Kind = ELDOpponentPKKind::Throw;
                Out.PK.Serial = PK.Serial;
                Out.PK.TargetCm = Target->FootCm;
                PKReason = EReason::PKThrow;
            }
        }
        const bool bBuildWindow = Frame.Public.BuildStage() == LDProgression::EMatchStage::Rest;
        const bool bBuildsNow = bBuildWindow && Frame.Public.NowMs >= Self.OpeningPickAtMs;
        if (bBuildsNow && DecideBuild(Frame, Catalog, State, true, Out.Build))
        {
            return Finish(EMode::Choose, EReason::ChooseOffer);
        }
        // Once nothing is left to build and nothing is in flight, a living body tries its build on its training target with
        // the production engagement (§6.3 activations, then the basic attack), the only unit it ever
        // engages here; the dummy is known only by the HUD's roster rule. A decision sending a PK request sends no body
        // intent (a running training order goes on); training resumes at the next decision.
        if (bBuildsNow && PKReason == EReason::None && !Frame.bBuildChangeInFlight && Self.Phase == ELDLifePhase::Alive)
        {
            if (const FLDOpponentUnit* Dummy = TrainingDummy(Frame))
            {
                FContext C(Frame, Catalog, State);
                if (!C.Initialize()) { return Finish(EMode::Stopped, EReason::ProviderUnavailable); }
                if (!Dummy->bProtected && !HandleSuppressed(C, *Dummy))
                {
                    Out.Body = EngageBody(C, *Dummy);
                    if (Self.bChanneling || (Frame.Order.IsLive() && Frame.Order.Kind == ELDOpponentOrderKind::Action))
                    {
                        Out.Body = FLDOpponentBodyIntent();
                        Out.Body.Kind = EBody::Keep;
                    }
                    return Finish(EMode::Engage, EReason::Train);
                }
            }
        }
        // Once the build window closes, a training order still running stops; an accepted action finishes first.
        if (!bBuildWindow && Frame.Order.IsLive() && Frame.Order.Kind != ELDOpponentOrderKind::Action)
        {
            Out.Body.Kind = EBody::Stop;
        }
        if (PKReason != EReason::None) { return Finish(EMode::PK, PKReason); }
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
    // A telegraph about to land on the own foot comes first (issue #287 spec §13).
    if (!bActionLive && Self.Phase == ELDLifePhase::Alive)
    {
        if (const TOptional<FLDOpponentBodyIntent> Dodge = DodgeIntent(C))
        {
            Body(Dodge.GetValue());
            return Finish(State.Mode, EReason::EngageTarget);
        }
        if (const TOptional<FLDOpponentBodyIntent> Leave = LeaveGroundIntent(C))
        {
            Body(Leave.GetValue());
            return Finish(State.Mode, EReason::EngageTarget);
        }
        // B01 T03-02: a camp member that walls off the kept walk with no way round is fought through.
        if (const FLDOpponentUnit* Wall = WallingUnit(Frame))
        {
            Body(EngageBody(C, *Wall));
            return Finish(EMode::Engage, EReason::EngageTarget);
        }
    }

    if (Frame.Public.Timeline.Stage == ELDMatchStage::FinalField)
    {
        FLDOpponentBodyIntent Intent;
        EndVisit(State, false);
        State.bRecovering = false;
        // B01 T04-03: once the street closure runs, a body outside the current ring (or about to be passed by it) walks
        // back in before anything else; outside it only loses health.
        const double Street = double(Frame.Public.StreetRadiusCm);
        const FVector FromCentre = (Self.FootCm - Frame.Public.ArenaCentreCm) * FVector(1, 1, 0);
        if (Street > 0 && C.Now >= Frame.Public.ClosureAtMs && FromCentre.Size() > Street - StreetRingMarginCm)
        {
            ForgetTarget(State);
            FVector Inside = Frame.Public.ArenaCentreCm
                + FromCentre.GetSafeNormal() * FMath::Max(0., Street - 2. * StreetRingMarginCm);
            Inside.Z = Frame.Public.ArenaCentreCm.Z;
            Body(MoveToPoint(C, Inside));
            return Finish(EMode::Contest, EReason::ContestCapture);
        }
        if (C.bCaptureOpen && FinalModBody(C, Intent)) { Body(Intent); }
        else if (CircleBody(C, true, Intent)) { Body(Intent); }
        else { Body(Halt(C)); }
        return Finish(EMode::Contest, EReason::ContestCapture);
    }

    // A contest only pauses the spray holder's progress and leaving, dying or a hard control clears it, so a paused holder
    // keeps holding and fights inside the zone like a sole sprayer; only the stop-loss line below lets it go.
    const bool bHolder = C.bCaptureOpen && (Frame.Public.Circle == ELDOpponentCircle::Solo || Frame.Public.Circle == ELDOpponentCircle::Contested)
        && Frame.Public.Occupier == Self.ContenderId;
    const bool bHoldingSolo = bHolder && Frame.Public.Circle == ELDOpponentCircle::Solo;
    const bool bHoldingPaused = bHolder && Frame.Public.Circle == ELDOpponentCircle::Contested;
    // The owner guards the zone; rivals can pause him or repaint.
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
    // Only the owner alone in the zone advances the guard.
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
    if (C.Boss && !bHoldingSolo && !State.bRecovering && C.BossFightReady())
    {
        EndVisit(State, false);
        State.bEscapingBoss = false;
        Body(EngageBody(C, *C.Boss));
        return Finish(EMode::Engage, EReason::EngageTarget);
    }
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
        // After escaping a threat, continue to recovery instead of turning back toward a charm at the sight edge.
        const FLDOpponentPOI* Drop = bRivalHolds || !State.RetreatPOI.IsNone() ? nullptr : C.AirdropTarget();
        // B01 T04-03: a recovery orb in sight heals at once; it comes before the airdrop and the dorm.
        if (const FLDOpponentPOI* Orb = bRivalHolds ? nullptr : C.RecoveryOrbTarget())
        {
            Body(ApproachPickup(C, *Orb, C.AirdropRadiusCm));
            return Finish(EMode::Disengage, EReason::Recover);
        }
        if (Drop && (C.Self.HpP2 < C.Self.MaxHpP2 || C.Self.MomentumP2 < C.Self.MaxMomentumP2))
        {
            Body(ApproachPickup(C, *Drop, C.AirdropRadiusCm));
            return Finish(EMode::Airdrop, EReason::ContestAirdrop);
        }
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
    // B01 T04-03: a recovery orb in sight is picked up first by a body missing health or momentum, out of a camp fight.
    if (const FLDOpponentPOI* Orb = State.VisitEngagedAtMs == 0 ? C.RecoveryOrbTarget() : nullptr)
    {
        EndVisit(State, false);
        Body(ApproachPickup(C, *Orb, C.AirdropRadiusCm));
        return Finish(EMode::Disengage, EReason::Recover);
    }
    if (const FLDOpponentPOI* Drop = State.VisitEngagedAtMs == 0 ? C.AirdropTarget() : nullptr)
    {
        EndVisit(State, false);
        Body(ApproachPickup(C, *Drop, C.AirdropRadiusCm));
        return Finish(EMode::Airdrop, EReason::ContestAirdrop);
    }
    // P6 spec §10.3: a healthy body within reach goes for the live mini flag or the arena, below the airdrop and above
    // farming, never out of a camp fight it began. It waits on the point (well inside the circle) for the opening; a
    // contender seen there is engaged by the ordinary engage branch above.
    if (const FLDOpponentPOI* Goal = State.VisitEngagedAtMs == 0 ? C.ObjectiveTarget() : nullptr)
    {
        EndVisit(State, false);
        Body(C.Distance(Goal->FootCm) * 2. <= double(Frame.Public.Objective.RadiusCm) ? Halt(C) : MoveTo(C, *Goal));
        return Finish(EMode::Objective, EReason::ContestObjective);
    }
    // #305 spec §11.3: below the objectives, a healthy body chases the visible dealer, then opens a seen locker (standing
    // on it channels the pickup), never out of a camp fight it began.
    const FLDOpponentUnit* Dealer = State.VisitEngagedAtMs == 0 ? C.DealerTarget() : nullptr;
    if (Dealer && !HandleSuppressed(C, *Dealer))
    {
        EndVisit(State, false);
        Body(AttackBody(C, *Dealer));
        return Finish(EMode::Objective, EReason::ChaseDealer);
    }
    if (const FLDOpponentPOI* Locker = State.VisitEngagedAtMs == 0 ? C.LockerTarget() : nullptr)
    {
        EndVisit(State, false);
        Body(ApproachPickup(C, *Locker, C.LockerStandCm * 2.));
        return Finish(EMode::Objective, EReason::OpenLocker);
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

bool DecideFollowUpBuild(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
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

TOptional<FLDOpponentBodyIntent> DodgeWarning(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
    FLDOpponentPolicyState& State)
{
    using namespace Detail;
    FContext C(Frame, Catalog, State);
    if (!C.Initialize()) { return {}; }
    return DodgeIntent(C, false);
}

#endif
}

double LDOpponentPolicy::GroundWeightedCm(TConstArrayView<FVector> Points, TConstArrayView<FLDOpponentGround> Grounds, int64 CostBp)
{
    TArray<FLDGroundState, TInlineAllocator<8>> Enemy;
    for (const FLDOpponentGround& Ground : Grounds)
    {
        if (Ground.bEnemy) { Enemy.Add(Detail::GroundShape(Ground.View)); }
    }
    const double Cost = FMath::Max<int64>(CostBp, 10000) / 10000.;
    double Total = 0;
    for (int32 I = 1; I < Points.Num(); ++I)
    {
        const FVector A = Points[I - 1], B = Points[I];
        const double Length = FVector::Dist2D(A, B);
        if (Length <= 0) { continue; }
        const int32 Steps = FMath::Max(1, FMath::CeilToInt(Length / GroundPathSampleCm));
        for (int32 Step = 0; Step < Steps; ++Step)
        {
            const FVector At = FMath::Lerp(A, B, (Step + .5) / Steps);
            const bool bInside = Enemy.ContainsByPredicate([&At](const FLDGroundState& Shape) { return LDCombatElements::GroundContains(Shape, At); });
            Total += Length / Steps * (bInside ? Cost : 1.);
        }
    }
    return Total;
}

void LDOpponentPolicy::GroundDetours(const FVector& From, const FVector& To, TConstArrayView<FLDOpponentGround> Grounds,
    TArray<FVector>& Out)
{
    Out.Reset();
    const FVector Along = (To - From).GetSafeNormal2D();
    if (Along.IsNearlyZero()) { return; }
    const FVector Side(-Along.Y, Along.X, 0);
    for (const FLDOpponentGround& Ground : Grounds)
    {
        if (!Ground.bEnemy) { continue; }
        // The footprint's bounding circle: a band reaches its half length beyond its radius.
        const double Bound = Ground.View.RadiusCm
            + (Ground.View.Shape == ELDObservedGroundShape::Band ? Ground.View.ExtentCm : 0.);
        const FVector Centre(Ground.View.CentreCm.X, Ground.View.CentreCm.Y, From.Z);
        const FVector Closest = FMath::ClosestPointOnSegment(Centre, FVector(From.X, From.Y, From.Z), FVector(To.X, To.Y, From.Z));
        if (FVector::Dist2D(Closest, Centre) >= Bound) { continue; }
        for (const double Sign : {1., -1.}) { Out.Add(Centre + Side * Sign * (Bound + GroundDetourMarginCm)); }
    }
}

int32 LDOpponentPolicy::BlockingBody(TConstArrayView<FVector> Points, TConstArrayView<FBodyDisc> Bodies, double WalkerRadiusCm,
    double LookaheadCm, FVector& OutAlong)
{
    OutAlong = FVector::ZeroVector;
    if (Points.Num() < 2) { return INDEX_NONE; }
    const FVector2D End(Points.Last());
    double Walked = 0;
    int32 Best = INDEX_NONE;
    double BestAt = TNumericLimits<double>::Max();
    for (int32 I = 1; I < Points.Num() && Walked < LookaheadCm; ++I)
    {
        const FVector2D A(Points[I - 1]), B(Points[I]);
        const double Length = FVector2D::Distance(A, B);
        if (Length <= 0) { continue; }
        // Only the part of this segment inside the lookahead is judged.
        const FVector2D Cut = Walked + Length > LookaheadCm ? A + (B - A) * ((LookaheadCm - Walked) / Length) : B;
        for (int32 Index = 0; Index < Bodies.Num(); ++Index)
        {
            const FBodyDisc& Body = Bodies[Index];
            const FVector2D Centre(Body.CentreCm);
            const double Clearance = Body.RadiusCm + WalkerRadiusCm;
            if (FVector2D::DistSquared(Centre, End) <= FMath::Square(Clearance)) { continue; }
            const FVector2D Closest = FMath::ClosestPointOnSegment2D(Centre, A, Cut);
            if (FVector2D::DistSquared(Closest, Centre) >= FMath::Square(Clearance)) { continue; }
            // A walker already touching a body is not blocked by a first leg that slides past it rather than into it.
            if (I == 1 && FVector2D::Distance(Closest, Centre) >= FVector2D::Distance(A, Centre) - BodyGrazeCm) { continue; }
            const double At = Walked + FVector2D::Distance(A, Closest);
            if (At < BestAt)
            {
                BestAt = At;
                Best = Index;
                OutAlong = FVector(B - A, 0).GetSafeNormal2D();
            }
        }
        if (Best != INDEX_NONE) { return Best; }
        Walked += Length;
    }
    return Best;
}

void LDOpponentPolicy::BodyDetours(const FBodyDisc& Body, const FVector& Along, double WalkerRadiusCm, TArray<FVector>& Out)
{
    Out.Reset();
    const FVector Heading = Along.GetSafeNormal2D();
    if (Heading.IsNearlyZero()) { return; }
    const FVector Side(-Heading.Y, Heading.X, 0);
    const double Clearance = Body.RadiusCm + WalkerRadiusCm;
    for (int32 Step = 1; Step <= 3; ++Step)
    {
        for (const double Sign : {1., -1.}) { Out.Add(Body.CentreCm + Side * Sign * (Clearance + GroundDetourMarginCm * Step)); }
    }
}

const FLDOpponentUnit* LDOpponentPolicy::WallingUnit(const FLDOpponentDecisionFrame& Frame)
{
    if (!Frame.WallingHandle.IsValid()) { return nullptr; }
    return Frame.Units.FindByPredicate([&Frame](const FLDOpponentUnit& Unit)
    {
        return Unit.Kind == ELDOpponentUnitKind::CampMember && Unit.Handle == Frame.WallingHandle && Unit.Epoch == Frame.WallingEpoch;
    });
}