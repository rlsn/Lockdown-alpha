#include "World/LDCampDirector.h"
#include "World/LDCampMember.h"
#include "World/LDCampDecision.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDMatchProviders.h"
#include "Misc/ScopeExit.h"
#include "Navigation/LDPathSteering.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDVisionDirector.h"

namespace
{
    // Path-following bookkeeping for camp bodies; engineering tolerances, not gameplay values.
    constexpr double CampGoalToleranceCm = LDPathSteering::WaypointToleranceCm;
    constexpr double CampReplanShiftCm = 50.;
    constexpr double CampStallSeconds = 2.;

    bool CampPositiveCm(const FLDDemoRational& Value, double& Out)
    {
        Out = Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : 0;
        return FMath::IsFinite(Out) && Out > 0;
    }

    bool IsCampWorldFault(ELDWorldQueryFailure Failure)
    {
        return Failure == ELDWorldQueryFailure::MissingWorld || Failure == ELDWorldQueryFailure::StaleWorld
            || Failure == ELDWorldQueryFailure::InvalidCapsule || Failure == ELDWorldQueryFailure::InvalidSurface
            || Failure == ELDWorldQueryFailure::InvalidPosition;
    }

    FLDMatchProviderHooks MakeCampProvider()
    {
        FLDMatchProviderHooks Hooks;
        Hooks.Prepare = [](ALDGameMode& Mode, const FLDMatchStartContext& Start, FString& Error)
        {
            if (Mode.Camps)
            {
                Error = TEXT("The previous match's camp director was not reset.");
                return false;
            }
            FActorSpawnParameters Spawn;
            Spawn.Owner = &Mode;
            ALDCampDirector* Director = Mode.GetWorld()->SpawnActor<ALDCampDirector>(Spawn);
            if (!Director)
            {
                Error = TEXT("The camp director could not be created.");
                return false;
            }
            Mode.Camps = Director;
            return Director->BeginMatch(Start.Match, Start.StartedAtMs, Error);
        };
        Hooks.MatchReset = [](ALDGameMode& Mode, const FLDMatchReset&, FString& Error)
        {
            if (ALDCampDirector* Director = Mode.Camps.Get())
            {
                Director->EndMatch();
                Director->Destroy();
            }
            Mode.Camps = nullptr;
            Error.Reset();
            return true;
        };
        return Hooks;
    }

    const LDMatchProviders::FRegistrar CampProviderRegistration(ELDMatchProvider::Camps, &MakeCampProvider);
}

ALDCampDirector::ALDCampDirector()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
}

void ALDCampDirector::EndPlay(const EEndPlayReason::Type Reason)
{
    EndMatch();
    Super::EndPlay(Reason);
}

ALDGameMode* ALDCampDirector::AuthorityMode() const
{
    auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
    return Mode && HasAuthority() && Mode->Camps == this ? Mode : nullptr;
}

#if WITH_DEV_AUTOMATION_TESTS
bool ALDCampDirector::DevRefreshCleared(const FLDGameplayWritePermit& Permit, int32& OutWaiting, FString& Error)
{
    OutWaiting = 0;
    if (!CheckPermit(Permit, ELDCommitPhase::Transactions, Error)) { return false; }
    OutWaiting = State.DevRefreshNow(Permit.GetBoundary().AtMs);
    return true;
}
#endif

bool ALDCampDirector::CheckPermit(const FLDGameplayWritePermit& Permit, ELDCommitPhase Phase, FString& Error) const
{
    const ALDGameMode* Mode = AuthorityMode();
    if (!Mode || Mode->ValidateWritePermit(Permit, Phase) != ELDCommitReject::None
        || !State.IsActive() || State.GetMatch() != Mode->GetMatchContext() || !Content)
    {
        Error = TEXT("Camp work requires the active camp match and the current core permit for its phase.");
        return false;
    }
    Error.Reset();
    return true;
}

bool ALDCampDirector::BeginMatch(const FLDMatchContext& Match, int64 StartedAtMs, FString& Error)
{
    if (!HasAuthority() || State.IsActive() || !Bodies.IsEmpty())
    {
        Error = TEXT("Camp match preparation needs a fresh server-side director.");
        return false;
    }
    TSharedPtr<const FLDDemoContentSnapshot> Loaded;
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!LDDemoEntry::LoadRuntimeContent(Loaded, Error) || !Registry || !Registry->Ensure(Error) || !Registry->GetGeometry())
    {
        if (Error.IsEmpty()) { Error = TEXT("Camps need the approved content and the registered Demo world."); }
        return false;
    }
    FLDCampRoster Roster;
    if (!LDCamp::BuildRoster(Loaded->Get(), *Registry->GetGeometry(), Roster, Error)
        || !State.Begin(Match, MakeShared<const FLDCampRoster>(MoveTemp(Roster)), Error)
        || !LDBoss::Resolve(*Loaded, *Registry->GetGeometry(), StartedAtMs, Boss, Error))
    {
        return false;
    }
    Content = Loaded;
    Error.Reset();
    return true;
}

bool ALDCampDirector::SpawnInitialCamps(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!CheckPermit(Permit, ELDCommitPhase::RespawnsAndWorld, Error)) { return false; }
    if (!Bodies.IsEmpty())
    {
        Error = TEXT("Camps already spawned for this match.");
        return false;
    }
    for (int32 Camp = 0; Camp < State.NumCamps(); ++Camp)
    {
        bool bBlocked = false;
        if (!State.CanSpawnInitial(Camp, Error) || !SpawnGroup(Camp, Permit, bBlocked, Error)) { return false; }
        if (bBlocked) { return false; }
    }
    Error.Reset();
    return true;
}

bool ALDCampDirector::ReadMember(const FLDCampSpec& Spec, const FLDCombatBodyRef& Ref, FMemberRead& Out, FString& Error) const
{
    return ReadBody(Spec.SurfaceId, Ref, Out, Error);
}

bool ALDCampDirector::ReadBody(FName SurfaceId, const FLDCombatBodyRef& Ref, FMemberRead& Out, FString& Error) const
{
    Out = {};
    ALDGameMode* Mode = AuthorityMode();
    ALDCampMemberCharacter* Body = Bodies.FindRef(Ref.Victim.EntityId).Get();
    ALDCharacter* Resolved = nullptr;
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    FCollisionShape Capsule;
    if (!Mode || !Body || !Registry || Mode->ResolveBody(State.GetMatch(), Ref, Resolved) != ELDCommitReject::None
        || Resolved != Body || !ULDMapRegistry::ContentCapsule(*Content, Capsule, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("A living camp member is not the core's current registered body."); }
        return false;
    }
    const auto* Movement = Body->GetCharacterMovement();
    Out.Body = Body;
    Out.FootCm = Movement->GetActorFeetLocation();
    if (!Movement->IsMovingOnGround() || !Movement->CurrentFloor.IsWalkableFloor())
    {
        Error.Reset();
        return true;
    }
    FLDLegalGround Ground;
    ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
    FString GroundError;
    if (Registry->QueryGround(Registry->Revision(), Out.FootCm, Capsule, SurfaceId, Ground, Failure, GroundError))
    {
        Out.bGrounded = Ground.Floor.Get() == Movement->CurrentFloor.HitResult.GetComponent();
        Out.SurfaceId = Out.bGrounded ? Ground.SurfaceId : NAME_None;
    }
    else if (IsCampWorldFault(Failure))
    {
        Error = GroundError;
        return false;
    }
    Error.Reset();
    return true;
}

bool ALDCampDirector::BuildCandidates(const FLDCampMemberState& Member, const FMemberRead& Self, double NowSeconds,
    TArray<FLDAuthorityTargetSample>& Out, FString& Error) const
{
    Out.Reset();
    ALDGameMode* Mode = AuthorityMode();
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Mode || !Mode->Vision || !Registry)
    {
        Error = TEXT("Camp perception needs the core, the vision owner and the registered world.");
        return false;
    }
    for (const FLDCombatBodyRef& Source : Member.HostileSources)
    {
        FLDContenderLifeRead Life;
        if (!Mode->ReadContenderLife(State.GetMatch(), Source.Victim.EntityId, Life, Error)) { return false; }
        // A dead, departed or later life is no longer the recorded attacker.
        if (Life.Phase != ELDLifePhase::Alive || Life.Body != Source) { continue; }
        ALDCharacter* Target = nullptr;
        if (Mode->ResolveBody(State.GetMatch(), Source, Target) != ELDCommitReject::None || !Target)
        {
            Error = TEXT("A living hostile contender has no current registered body.");
            return false;
        }
        const FVector TargetFoot = Target->GetCharacterMovement()->GetActorFeetLocation();
        // Camp perception honours stealth exactly like a contender's observation.
        switch (Mode->Vision->TraceObservation(Self.FootCm, *Target, Registry->Revision()))
        {
        case ELDSightResult::Visible:
            Out.Add({Source, TargetFoot, Registry->Revision(), NowSeconds});
            break;
        case ELDSightResult::OutOfRange:
        case ELDSightResult::Blocked:
            break;
        default:
            Error = TEXT("Camp perception hit an invalid point or a stale world in TraceSight.");
            return false;
        }
    }
    Error.Reset();
    return true;
}

bool ALDCampDirector::PlanPath(const FMemberRead& Self, FName SurfaceId, const FVector& Goal, double NowSeconds,
    FMotion& Out, FString& Error) const
{
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    FCollisionShape Capsule;
    if (!Registry || !Self.bGrounded || !ULDMapRegistry::ContentCapsule(*Content, Capsule, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("NotGrounded: a camp member plans only from its registered support."); }
        return false;
    }
    FLDLegalGround BodyGround, From, GoalGround, To;
    FLDCompletePath Path;
    ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
    // Contacts can leave a body inside Recast's agent margin. Both the member and a chased body plan from their
    // nearest navmesh point within one capsule radius (LDPathSteering::PlanStart); a goal already on the navmesh,
    // such as a member home, is returned unchanged.
    if (!Registry->QueryGround(Registry->Revision(), Self.FootCm, Capsule, SurfaceId, BodyGround, Failure, Error)
        || !LDPathSteering::PlanStart(*Registry, BodyGround, Capsule, From, Failure, Error)
        || !Registry->QueryGround(Registry->Revision(), Goal, Capsule, SurfaceId, GoalGround, Failure, Error)
        || !LDPathSteering::PlanStart(*Registry, GoalGround, Capsule, To, Failure, Error)
        || !Registry->QueryCompletePath(Registry->Revision(), From, To, Capsule, Path, Failure, Error))
    {
        return false;
    }
    Out.Points = MoveTemp(Path.Points);
    Out.Index = Out.Points.Num() > 1 ? 1 : 0;
    Out.Goal = Goal;
    Out.bActive = true;
    Out.BestProgress = 0;
    Out.LastProgressSeconds = NowSeconds;
    Error.Reset();
    return true;
}

void ALDCampDirector::StopMotion(const FGuid& EntityId)
{
    if (FMotion* Track = Motion.Find(EntityId))
    {
        Track->bActive = false;
        Track->Points.Reset();
        Track->Index = 0;
    }
    LDPathSteering::HaltLocomotion(Bodies.FindRef(EntityId).Get());
}

bool ALDCampDirector::AdvanceMemberMotion(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Ref,
    const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!CheckPermit(Permit, ELDCommitPhase::Motion, Error)) { return false; }
    ALDGameMode* Mode = AuthorityMode();
    ALDCampMemberCharacter* Body = Bodies.FindRef(Ref.Victim.EntityId).Get();
    ALDCharacter* Resolved = nullptr;
    if ((!State.FindLiving(Ref) && !Boss.IsBody(Ref)) || !Body || Mode->ResolveBody(Step.Match, Ref, Resolved) != ELDCommitReject::None
        || Resolved != Body)
    {
        Error = TEXT("Camp motion was issued for a stale or unregistered camp body.");
        return false;
    }
    FMotion& Track = Motion.FindOrAdd(Ref.Victim.EntityId);
    const FVector Input = Track.bActive ? LDPathSteering::Steering(Track.Points, Track.Index, *Body) : FVector::ZeroVector;
    auto* Movement = CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement());
    if (!Movement->AdvanceAuthorityMovement(Step, Ref, Input, Permit, Error)) { return false; }
    if (!Track.bActive) { return true; }
    if (LDPathSteering::AtGoal(*Body, Track.Goal, CampGoalToleranceCm))
    {
        StopMotion(Ref.Victim.EntityId);
        return true;
    }
    const double Progress = LDPathSteering::AlongPath(Track.Points, Body->GetNavAgentLocation());
    if (Progress >= Track.BestProgress + 10)
    {
        Track.BestProgress = Progress;
        Track.LastProgressSeconds = Step.EndServerSeconds;
        if (FLDCampReturnWatch* Watch = Returns.Find(Ref.Victim.EntityId)) { Watch->Progressed(Step.EndServerSeconds); }
    }
    if (Step.EndServerSeconds - Track.LastProgressSeconds >= CampStallSeconds)
    {
        UE_LOG(LogTemp, Warning, TEXT("LD_CAMP_PATH_STALLED member=%s goal=%s"), *Ref.Victim.EntityId.ToString(), *Track.Goal.ToString());
        StopMotion(Ref.Victim.EntityId);
    }
    return true;
}

bool ALDCampDirector::ProcessAuthorityIntents(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!CheckPermit(Permit, ELDCommitPhase::Actions, Error)) { return false; }
    ALDGameMode* Mode = AuthorityMode();
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Mode->Combat || !Registry)
    {
        Error = TEXT("Camp intents need Combat and the registered world.");
        return false;
    }
    const double Now = Mode->GetAuthorityNowSeconds();
    const FLDDemoContentParameters& Parameters = Content->Get().Parameters;
    if (Parameters.CampReturnDeadEndPlans <= 0 || Parameters.CampReturnDeadEndPlans > MAX_int32 || Parameters.CampReturnDeadEndMs <= 0)
    {
        Error = TEXT("Approved CAMP.ReturnDeadEnd parameters are invalid.");
        return false;
    }
    const int32 DeadEndPlans = int32(Parameters.CampReturnDeadEndPlans);
    const double DeadEndSeconds = double(Parameters.CampReturnDeadEndMs) / 1000.;
    for (int32 Camp = 0; Camp < State.NumCamps(); ++Camp)
    {
        const FLDCampSpec& Spec = State.GetRoster()->Camps[Camp];
        for (int32 Slot = 0; Slot < Spec.Members.Num(); ++Slot)
        {
            const FLDCampMemberState& Member = State.FindCamp(Camp)->Members[Slot];
            if (!Member.bAlive) { continue; }
            const FLDCombatBodyRef Ref = Member.Body;
            const FGuid Entity = Ref.Victim.EntityId;
            FMemberRead Self;
            FLDCombatBodyRead Body;
            TArray<FLDAuthorityTargetSample> Candidates;
            if (!ReadMember(Spec, Ref, Self, Error) || !BuildCandidates(Member, Self, Now, Candidates, Error)) { return false; }
            if (!Mode->Combat->ReadCombatBody(Ref, Body))
            {
                Error = TEXT("Combat has no readable state for a living camp body.");
                return false;
            }
            FLDCampDecisionInput Input;
            Input.Match = State.GetMatch();
            Input.World = Registry->Revision();
            Input.Self = Ref;
            Input.NowSeconds = Now;
            Input.FootCm = Self.FootCm;
            Input.Grounded = Self.bGrounded;
            Input.SurfaceId = Self.SurfaceId;
            Input.CampAnchorFootCm = Spec.AnchorCm;
            Input.MemberHome = {Input.World, Spec.Members[Slot].HomeCm, Spec.SurfaceId};
            Input.Brain = Member.Brain;
            Input.bObservationComplete = true;
            Input.MovementAllowed = Body.bMovementAllowed;
            Input.AttackAllowed = Body.bAttackAllowed;
            Input.BasicAttackReady = Body.bBasicAttackReady;
            Input.bStrandedInPlace = Stranded.Contains(Entity);
            FLDCampDecision Decision;
            bool bBeganReturn = false;
            if (!LDCampDecision::Evaluate(*Content, Spec.CampId, Input, Member.HostileSources, Candidates, Decision, Error)
                || !State.ApplyDecision(Ref, Decision, bBeganReturn, Error))
            {
                return false;
            }
            if (bBeganReturn)
            {
                UE_LOG(LogTemp, Display, TEXT("LD_CAMP_RETURN camp=%s slot=%d life=%u epoch=%llu"),
                    *Spec.CampId.ToString(), Slot, Ref.Victim.Life, State.FindLiving(Ref)->ReturnEpoch);
            }
            const uint64 Epoch = State.FindLiving(Ref)->ReturnEpoch;
            FLDCampReturnWatch* Watch = nullptr;
            if (Decision.NextBrain.Phase == ELDCampBrainPhase::Returning)
            {
                Watch = &Returns.FindOrAdd(Entity);
                Watch->Observe(Epoch, Now);
                // Control pauses the return (ReturnHome needs MovementAllowed): paused time is not a stall, and it does
                // not wipe the time already spent without progress either.
                Watch->Tick(Now, Decision.Intent == ELDCampIntent::ReturnHome);
            }
            auto DeadEnd = [&](const TCHAR* Cause)
            {
                StopMotion(Entity);
                UE_LOG(LogTemp, Warning, TEXT("LD_CAMP_RETURN_DEAD_END camp=%s slot=%d epoch=%llu cause=%s refused=%d"),
                    *Spec.CampId.ToString(), Slot, Epoch, Cause, Watch->RefusedPlans);
            };
            switch (Decision.Intent)
            {
            case ELDCampIntent::Chase:
            case ELDCampIntent::ReturnHome:
            {
                if (!Decision.GoalFootCm.IsSet())
                {
                    Error = TEXT("Camp movement intent has no goal.");
                    return false;
                }
                const FVector Goal = Decision.GoalFootCm.GetValue();
                const bool bReturn = Decision.Intent == ELDCampIntent::ReturnHome;
                FMotion& Track = Motion.FindOrAdd(Entity);
                if (bReturn && Watch->bDeadEnd)
                {
                    // Waiting for RespawnsAndWorld to reset it where it stands.
                    StopMotion(Entity);
                    break;
                }
                if (bReturn && Watch->Stalled(Now, DeadEndSeconds))
                {
                    DeadEnd(TEXT("no_progress"));
                    break;
                }
                if (Track.bActive && FVector::DistSquared2D(Track.Goal, Goal) <= FMath::Square(CampReplanShiftCm)) { break; }
                if (bReturn && !Watch->MayPlan(Now)) { break; }
                FMotion Planned;
                FString PlanError;
                if (PlanPath(Self, Spec.SurfaceId, Goal, Now, Planned, PlanError))
                {
                    Track = MoveTemp(Planned);
                    if (bReturn) { Watch->PlanAccepted(); }
                    break;
                }
                StopMotion(Entity);
                if (!bReturn)
                {
                    UE_LOG(LogTemp, Verbose, TEXT("LD_CAMP_CHASE_PATH_REJECTED camp=%s slot=%d %s"), *Spec.CampId.ToString(), Slot, *PlanError);
                    break;
                }
                if (!Watch->bWarned)
                {
                    Watch->bWarned = true;
                    UE_LOG(LogTemp, Warning, TEXT("LD_CAMP_RETURN_BLOCKED camp=%s slot=%d epoch=%llu %s"),
                        *Spec.CampId.ToString(), Slot, Epoch, *PlanError);
                }
                // Refused plans are spaced by the stall interval, so the limit spans seconds rather than ticks.
                if (Watch->PlanRefused(Now, DeadEndPlans, CampStallSeconds)) { DeadEnd(TEXT("refused_plans")); }
                break;
            }
            case ELDCampIntent::RequestBasicAttack:
            {
                StopMotion(Entity);
                uint64 Sequence = 0;
                if (!Decision.Target.IsSet() || !State.NextDirectSequence(Ref, Sequence, Error))
                {
                    if (Error.IsEmpty()) { Error = TEXT("Camp attack intent has no target."); }
                    return false;
                }
                FLDCombatCaller Caller;
                Caller.Match = State.GetMatch();
                Caller.Kind = ELDVictimKind::CampMember;
                Caller.EntityId = Entity;
                FLDCombatActionRequest Request;
                Request.Generation = State.GetMatch().Generation;
                Request.Life = Ref.Victim.Life;
                Request.DirectSequence = Sequence;
                Request.Entry = ELDCombatEntry::Basic;
                Request.Target.Kind = ELDCombatTargetKind::Unit;
                Request.Target.ServerBody = Decision.Target.GetValue();
                const FLDCombatActionReceipt Receipt = Mode->Combat->SubmitAction(Caller, Request);
                UE_LOG(LogTemp, Verbose, TEXT("LD_CAMP_ATTACK camp=%s slot=%d sequence=%llu phase=%u reject=%u"),
                    *Spec.CampId.ToString(), Slot, Sequence, uint8(Receipt.Phase), uint8(Receipt.Reject));
                break;
            }
            case ELDCampIntent::Hold:
            case ELDCampIntent::ResetReady:
                StopMotion(Entity);
                break;
            default:
                Error = TEXT("Camp decision returned an unknown intent.");
                return false;
            }
        }
    }
    return ProcessBoss(Permit, Error);
}

bool ALDCampDirector::ProcessBoss(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (Boss.Phase != ELDBossPhase::Alive)
    {
        Error.Reset();
        return true;
    }
    ALDGameMode* Mode = AuthorityMode();
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Mode || !Mode->Combat || !Mode->Vision || !Registry)
    {
        Error = TEXT("The Boss needs Combat, vision and the registered world.");
        return false;
    }
    const FLDCombatBodyRef Ref = Boss.Body;
    const FGuid Entity = Ref.Victim.EntityId;
    FMemberRead Self;
    FLDCombatBodyRead Body;
    if (!ReadBody(Boss.Rules.SurfaceId, Ref, Self, Error)) { return false; }
    if (!Mode->Combat->ReadCombatBody(Ref, Body))
    {
        Error = TEXT("Combat has no readable state for the living Boss.");
        return false;
    }
    // Every living contender, as the Boss sees it now (stealth honoured exactly like a contender's observation).
    TArray<LDAirdrop::FCandidate> Feet;
    Mode->Combat->ReadLivingContenderFeet(Feet);
    TArray<LDBoss::FCandidate> Living;
    for (const LDAirdrop::FCandidate& Foot : Feet)
    {
        FLDContenderLifeRead Life;
        if (!Mode->ReadContenderLife(State.GetMatch(), Foot.Contender, Life, Error)) { return false; }
        ALDCharacter* Target = nullptr;
        if (Life.Phase != ELDLifePhase::Alive || Mode->ResolveBody(State.GetMatch(), Life.Body, Target) != ELDCommitReject::None || !Target)
        {
            continue;
        }
        LDBoss::FCandidate& Candidate = Living.AddDefaulted_GetRef();
        Candidate.Body = Life.Body;
        Candidate.FootCm = Foot.FootCm;
        switch (Mode->Vision->TraceObservation(Self.FootCm, *Target, Registry->Revision()))
        {
        case ELDSightResult::Visible: Candidate.bVisible = true; break;
        case ELDSightResult::OutOfRange:
        case ELDSightResult::Blocked: break;
        default:
            Error = TEXT("Boss perception hit an invalid point or a stale world in TraceSight.");
            return false;
        }
    }
    const double Now = Mode->GetAuthorityNowSeconds();
    const FLDBossDecision Decision = Boss.Decide(Permit.GetBoundary().AtMs, Self.FootCm, Living,
        Body.bMovementAllowed && Self.bGrounded, Body.bAttackAllowed && Body.bBasicAttackReady);
    FMotion& Track = Motion.FindOrAdd(Entity);
    auto Walk = [&](const FVector& Goal, const TCHAR* What)
    {
        if (Track.bActive && FVector::DistSquared2D(Track.Goal, Goal) <= FMath::Square(CampReplanShiftCm)) { return; }
        FMotion Planned;
        FString PlanError;
        if (PlanPath(Self, Boss.Rules.SurfaceId, Goal, Now, Planned, PlanError))
        {
            Track = MoveTemp(Planned);
            return;
        }
        StopMotion(Entity);
        UE_LOG(LogTemp, Warning, TEXT("LD_BOSS_PATH_REJECTED %s %s"), What, *PlanError);
    };
    switch (Decision.Intent)
    {
    case ELDBossIntent::Patrol:
        // A reached waypoint hands over to the next one; the loop never ends while the Boss lives.
        if (LDPathSteering::AtGoal(*Self.Body, Boss.PatrolGoal(), CampGoalToleranceCm))
        {
            UE_LOG(LogTemp, Display, TEXT("LD_BOSS_WAYPOINT reached=%d at_ms=%lld"), Boss.Waypoint + 1, Permit.GetBoundary().AtMs);
            Boss.ReachedWaypoint();
        }
        Walk(Boss.PatrolGoal(), TEXT("patrol"));
        break;
    case ELDBossIntent::Chase:
        Walk(Decision.GoalFootCm, TEXT("chase"));
        break;
    case ELDBossIntent::Attack:
    {
        StopMotion(Entity);
        FLDCombatCaller Caller;
        Caller.Match = State.GetMatch();
        Caller.Kind = ELDVictimKind::CampMember;
        Caller.EntityId = Entity;
        FLDCombatActionRequest Request;
        Request.Generation = State.GetMatch().Generation;
        Request.Life = Ref.Victim.Life;
        Request.DirectSequence = ++BossSequence;
        Request.Entry = ELDCombatEntry::Basic;
        Request.Target.Kind = ELDCombatTargetKind::Unit;
        Request.Target.ServerBody = Decision.Target;
        const FLDCombatActionReceipt Receipt = Mode->Combat->SubmitAction(Caller, Request);
        UE_LOG(LogTemp, Display, TEXT("LD_BOSS_ATTACK target=%s sequence=%llu phase=%u reject=%u"),
            *Decision.Target.Victim.EntityId.ToString(), Request.DirectSequence, uint8(Receipt.Phase), uint8(Receipt.Reject));
        break;
    }
    case ELDBossIntent::None:
    default:
        // In range but cooling down, or controlled: hold where it stands.
        StopMotion(Entity);
        break;
    }
    Error.Reset();
    return true;
}

bool ALDCampDirector::OnCombatHostileContact(const FLDCombatHostileContact& Fact, const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!CheckPermit(Permit, ELDCommitPhase::Effects, Error)) { return false; }
    if (Boss.IsBody(Fact.VictimBody))
    {
        int64 AtMs = 0;
        if (!LDMatchLifecycle::DeadlineFromSeconds(Fact.OccurredAtSeconds, 0, AtMs, Error)) { return false; }
        Boss.NoteAttack(Fact.SourceBody, AtMs);
        Error.Reset();
        return true;
    }
    const ELDCampContactResult Result = State.ApplyContact({Fact.VictimBody, Fact.SourceBody, Fact.OccurredAtSeconds}, Error);
    if (Result == ELDCampContactResult::Rejected) { return false; }
    if (Result == ELDCampContactResult::Retaliating)
    {
        UE_LOG(LogTemp, Display, TEXT("LD_CAMP_RETALIATE victim=%s source=%s life=%u at=%.3f"),
            *Fact.VictimBody.Victim.EntityId.ToString(), *Fact.SourceBody.Victim.EntityId.ToString(),
            Fact.SourceBody.Victim.Life, Fact.OccurredAtSeconds);
    }
    Error.Reset();
    return true;
}

bool ALDCampDirector::ConsumeMemberDeath(const FLDCommittedDeath& Fact, const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!CheckPermit(Permit, ELDCommitPhase::DeathsAndRewards, Error)) { return false; }
    if (Boss.IsBody(Fact.VictimBody))
    {
        const FGuid Entity = Fact.VictimBody.Victim.EntityId;
        Boss.Died(Fact.VictimBody, Fact.CreditOwner, Fact.OccurredAtMs);
        Motion.Remove(Entity);
        if (ALDCampMemberCharacter* Body = Bodies.FindRef(Entity).Get()) { Body->Destroy(); }
        Bodies.Remove(Entity);
        AuthorityMode()->MarkPublicMatchDirty();
        UE_LOG(LogTemp, Display, TEXT("LD_BOSS_DOWN killer=%s at_ms=%lld"), *Fact.CreditOwner.ToString(), Fact.OccurredAtMs);
        return GrantApproval(Fact.CreditOwner, Permit, Error);
    }
    int32 Camp = INDEX_NONE;
    State.FindMember(Fact.Key.Victim.EntityId, &Camp);
    const ELDCampDeathResult Result = State.RecordDeath(Fact.Key, Fact.OccurredAtMs, Error);
    if (Result == ELDCampDeathResult::Rejected) { return false; }
    if (Result == ELDCampDeathResult::Duplicate)
    {
        Error.Reset();
        return true;
    }
    const FGuid Entity = Fact.Key.Victim.EntityId;
    Motion.Remove(Entity);
    Returns.Remove(Entity);
    Stranded.Remove(Entity);
    if (ALDCampMemberCharacter* Body = Bodies.FindRef(Entity).Get()) { Body->Destroy(); }
    Bodies.Remove(Entity);
    UE_LOG(LogTemp, Display, TEXT("LD_CAMP_DEATH camp=%s life=%u owner=%s cleared=%d due_ms=%lld"),
        *State.GetRoster()->Camps[Camp].CampId.ToString(), Fact.Key.Victim.Life, *Fact.CreditOwner.ToString(),
        Result == ELDCampDeathResult::CampCleared, State.FindCamp(Camp)->RespawnDueMs);
    Error.Reset();
    return true;
}

bool ALDCampDirector::TryReset(int32 Camp, int32 Slot, const FLDGameplayWritePermit& Permit, FString& Error)
{
    ALDGameMode* Mode = AuthorityMode();
    const FLDCampSpec& Spec = State.GetRoster()->Camps[Camp];
    const FLDCombatBodyRef Ref = State.FindCamp(Camp)->Members[Slot].Body;
    double Arrival = 0;
    if (!CampPositiveCm(Content->Get().Parameters.CampResetArrivalRadiusCm, Arrival))
    {
        Error = TEXT("Approved camp reset arrival radius is invalid.");
        return false;
    }
    FMemberRead Self;
    if (!ReadMember(Spec, Ref, Self, Error)) { return false; }
    // HomeProof: true same-surface support within the approved arrival radius of the fixed home.
    const bool bHome = Self.bGrounded && Self.SurfaceId == Spec.SurfaceId
        && FVector::DistSquared2D(Self.FootCm, Spec.Members[Slot].HomeCm) <= Arrival * Arrival;
    // #28 dead end: a return with no legal way home resets where the member stands, on real support only.
    const FLDCampReturnWatch* Watch = Returns.Find(Ref.Victim.EntityId);
    const bool bInPlace = !bHome && Self.bGrounded && Watch && Watch->bDeadEnd
        && Watch->Epoch == State.FindCamp(Camp)->Members[Slot].ReturnEpoch;
    if (!bHome && !bInPlace)
    {
        Error.Reset();
        return true;
    }
    FLDCampResetKey Key;
    FLDCombatBodyRead Body;
    if (!State.ReadResetKey(Ref, Key, Error)) { return false; }
    if (!Mode->Combat->ReadCombatBody(Ref, Body))
    {
        Error = TEXT("Combat has no readable state for a returning camp body.");
        return false;
    }
    FLDCampResetInput Input;
    Input.Body = Ref;
    Input.ReturnEpoch = Key.ReturnEpoch;
    Input.HomeFootCm = Spec.Members[Slot].HomeCm;
    Input.bInPlace = bInPlace;
    Input.ExpectedResourceRevision = Body.ResourceRevision;
    Input.ExpectedStateRevision = Body.StateRevision;
    FLDCampResetPrepareResult Prepared = Mode->Combat->PrepareCampReset(Input, Permit);
    if (!Prepared.bAlreadyApplied)
    {
        if (!Prepared.Value.IsSet())
        {
            UE_LOG(LogTemp, Verbose, TEXT("LD_CAMP_RESET_DEFERRED camp=%s slot=%d epoch=%llu error=%u %s"),
                *Spec.CampId.ToString(), Slot, Key.ReturnEpoch, uint8(Prepared.Error), *Prepared.Message);
            Error.Reset();
            return true;
        }
        Mode->Combat->ApplyCampReset(MoveTemp(Prepared.Value.GetValue()), Permit);
    }
    if (!State.CompleteReset(Key, Mode->GetAuthorityNowSeconds(), Error)) { return false; }
    StopMotion(Ref.Victim.EntityId);
    Returns.Remove(Ref.Victim.EntityId);
    // Only a reset at home ends the stranded hold; another in-place reset keeps it.
    if (bInPlace) { Stranded.Add(Ref.Victim.EntityId); } else { Stranded.Remove(Ref.Victim.EntityId); }
    UE_LOG(LogTemp, Display, TEXT("LD_CAMP_RESET camp=%s slot=%d life=%u epoch=%llu in_place=%d"),
        *Spec.CampId.ToString(), Slot, Ref.Victim.Life, Key.ReturnEpoch, bInPlace ? 1 : 0);
    return true;
}

bool ALDCampDirector::AdvanceCamps(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!CheckPermit(Permit, ELDCommitPhase::RespawnsAndWorld, Error)) { return false; }
    ALDGameMode* Mode = AuthorityMode();
    for (int32 Camp = 0; Camp < State.NumCamps(); ++Camp)
    {
        for (int32 Slot = 0; Slot < State.FindCamp(Camp)->Members.Num(); ++Slot)
        {
            const FLDCampMemberState& Member = State.FindCamp(Camp)->Members[Slot];
            if (Member.bAlive && Member.Brain.Phase == ELDCampBrainPhase::Returning
                && !TryReset(Camp, Slot, Permit, Error))
            {
                return false;
            }
        }
    }
    for (int32 Camp = 0; Camp < State.NumCamps(); ++Camp)
    {
        if (!State.IsRespawnDue(Camp, Mode->GetAuthorityNowMs())) { continue; }
        bool bBlocked = false;
        if (!SpawnGroup(Camp, Permit, bBlocked, Error)) { return false; }
        if (!bBlocked)
        {
            BlockedRefresh.Remove(Camp);
            continue;
        }
        // The ticket stays pending on the same camp; refresh retries at its own local placements.
        if (!BlockedRefresh.Contains(Camp))
        {
            UE_LOG(LogTemp, Warning, TEXT("LD_CAMP_REFRESH_BLOCKED camp=%s %s"), *State.GetRoster()->Camps[Camp].CampId.ToString(), *Error);
            BlockedRefresh.Add(Camp);
        }
    }
    if (Boss.IsSpawnDue(Mode->GetAuthorityNowMs()))
    {
        bool bBlocked = false;
        if (!SpawnBoss(Permit, bBlocked, Error)) { return false; }
        // A blocked appearance retries at the next boundary on the same waypoint.
        if (bBlocked && !bBossBlockedLogged)
        {
            UE_LOG(LogTemp, Warning, TEXT("LD_BOSS_SPAWN_BLOCKED %s"), *Error);
            bBossBlockedLogged = true;
        }
    }
    Error.Reset();
    return true;
}

bool ALDCampDirector::SpawnBoss(const FLDGameplayWritePermit& Permit, bool& bBlocked, FString& Error)
{
    bBlocked = false;
    ALDGameMode* Mode = AuthorityMode();
    const FLDDemoMonsterDefinition* Monster = nullptr;
    if (!Mode || !Content->FindMonster(Boss.Rules.MonsterId.ToString(), Monster, Error) || !Monster)
    {
        if (Error.IsEmpty()) { Error = TEXT("The Boss spawn needs the core and its approved monster row."); }
        return false;
    }
    FLDLegalGround Ground;
    if (!FindGround(Boss.Rules.Waypoints[0], Boss.Rules.SurfaceId, TEXT("boss"), {}, Ground, bBlocked, Error)) { return false; }
    if (bBlocked) { return true; }
    ALDCampMemberCharacter* Body = nullptr;
    bool bCommitted = false;
    ON_SCOPE_EXIT { if (!bCommitted && IsValid(Body)) { Body->Destroy(); } };
    if (!CreateBody(Ground, *Monster, TEXT("boss"), Body, Error)) { return false; }
    TArray<FLDCombatBodyRef> Refs;
    if (!Mode->SpawnCampMembers({{Boss.EntityId, Body, Boss.Rules.MonsterId}}, Permit, Refs, Error)) { return false; }
    bCommitted = true;
    if (Refs.Num() != 1 || !Boss.Spawned(Refs[0], Mode->GetAuthorityNowMs()))
    {
        Error = TEXT("Core committed a Boss life that the Boss state refused.");
        return false;
    }
    Bodies.Add(Boss.EntityId, Body);
    Motion.Add(Boss.EntityId, {});
    Body->SetActorHiddenInGame(false);
    Body->SetActorTickEnabled(true);
    Mode->MarkPublicMatchDirty();
    UE_LOG(LogTemp, Display, TEXT("LD_BOSS_SPAWN life=%u at_ms=%lld waypoint=%s"), Refs[0].Victim.Life, Mode->GetAuthorityNowMs(),
        *Boss.Rules.Waypoints[0].ToString());
    Error.Reset();
    return true;
}

bool ALDCampDirector::GrantApproval(const FGuid& Killer, const FLDGameplayWritePermit& Permit, FString& Error)
{
    ALDGameMode* Mode = AuthorityMode();
    FLDContenderLifeRead Life;
    // Only a contender's last hit is approved; a killer already dead by the same step gets nothing.
    if (!Killer.IsValid() || !Mode->ReadContenderLife(State.GetMatch(), Killer, Life, Error) || Life.Phase != ELDLifePhase::Alive)
    {
        Error.Reset();
        return true;
    }
    const FLDDemoBossDefinition& Rules = Content->Get().Boss;
    bool bApplied = false;
    if (!Mode->Combat->GrantTimedBonus(Life.Body, FName(*Rules.Id), Rules.ApprovalStats, Rules.ApprovalDurationMs, Permit, bApplied, Error))
    {
        return false;
    }
    UE_LOG(LogTemp, Display, TEXT("LD_BOSS_APPROVAL contender=%s applied=%d duration_ms=%lld"), *Killer.ToString(), bApplied ? 1 : 0,
        Rules.ApprovalDurationMs);
    return true;
}

bool ALDCampDirector::FindPlacement(const FLDCampSpec& Spec, int32 Slot, const TArray<FVector>& Reserved,
    FLDLegalGround& Out, bool& bBlocked, FString& Error) const
{
    return FindGround(Spec.Members[Slot].HomeCm, Spec.SurfaceId, FString::Printf(TEXT("camp %s member %d"), *Spec.CampId.ToString(), Slot),
        Reserved, Out, bBlocked, Error);
}

bool ALDCampDirector::FindGround(const FVector& HomeCm, FName SurfaceId, const FString& Label, const TArray<FVector>& Reserved,
    FLDLegalGround& Out, bool& bBlocked, FString& Error) const
{
    bBlocked = false;
    ALDGameMode* Mode = AuthorityMode();
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    const FLDDemoWorldGeometry* Geometry = Registry ? Registry->GetGeometry() : nullptr;
    FCollisionShape Capsule;
    if (!Mode || !Geometry || Geometry->SpawnOffsetsCm.IsEmpty() || !ULDMapRegistry::ContentCapsule(*Content, Capsule, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Camp placement needs the registered Demo world and its local placement offsets."); }
        return false;
    }
    const double Separation = 2. * Capsule.GetCapsuleRadius();
    for (const FLDDemoWorldPoint& OffsetPoint : Geometry->SpawnOffsetsCm)
    {
        FVector Offset;
        if (!OffsetPoint.ToVector(Offset, Error)) { return false; }
        const FVector Foot = HomeCm + FVector(Offset.X, Offset.Y, 0);
        FLDLegalGround Ground;
        ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
        FString GroundError;
        if (!Registry->QueryGround(Registry->Revision(), Foot, Capsule, SurfaceId, Ground, Failure, GroundError))
        {
            if (IsCampWorldFault(Failure))
            {
                Error = GroundError;
                return false;
            }
            continue;
        }
        // Bodies ignore pawn collision, so local placement keeps every current registered body apart explicitly.
        bool bClear = !Reserved.ContainsByPredicate([&](const FVector& Other)
            { return FVector::DistSquared2D(Other, Foot) < Separation * Separation; });
        for (TActorIterator<ALDCharacter> It(GetWorld()); bClear && It; ++It)
        {
            FLDCombatBodyRef Occupant;
            bClear = It->IsActorBeingDestroyed() || !Mode->ReadBodyRef(**It, Occupant)
                || FVector::DistSquared2D(It->GetCharacterMovement()->GetActorFeetLocation(), Foot) >= Separation * Separation;
        }
        if (bClear)
        {
            Out = MoveTemp(Ground);
            Error.Reset();
            return true;
        }
    }
    bBlocked = true;
    Error = FString::Printf(TEXT("SpawnBlocked: %s has no clear local placement."), *Label);
    return true;
}

bool ALDCampDirector::SpawnGroup(int32 Camp, const FLDGameplayWritePermit& Permit, bool& bBlocked, FString& Error)
{
    bBlocked = false;
    ALDGameMode* Mode = AuthorityMode();
    const FLDCampSpec& Spec = State.GetRoster()->Camps[Camp];
    const FLDDemoMonsterDefinition* Monster = nullptr;
    FCollisionShape Capsule;
    if (!Mode || !Content->FindMonster(Spec.MonsterId.ToString(), Monster, Error)
        || !ULDMapRegistry::ContentCapsule(*Content, Capsule, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Camp spawn needs the core and the approved monster."); }
        return false;
    }
    TArray<FLDLegalGround> Grounds;
    TArray<FVector> Reserved;
    for (int32 Slot = 0; Slot < Spec.Members.Num(); ++Slot)
    {
        FLDLegalGround Ground;
        if (!FindPlacement(Spec, Slot, Reserved, Ground, bBlocked, Error)) { return false; }
        if (bBlocked) { return true; }
        Reserved.Add(Ground.FootCm);
        Grounds.Add(MoveTemp(Ground));
    }
    TArray<ALDCampMemberCharacter*> Spawned;
    bool bCommitted = false;
    ON_SCOPE_EXIT
    {
        if (!bCommitted)
        {
            for (ALDCampMemberCharacter* Prepared : Spawned)
            {
                if (IsValid(Prepared)) { Prepared->Destroy(); }
            }
        }
    };
    for (int32 Slot = 0; Slot < Spec.Members.Num(); ++Slot)
    {
        ALDCampMemberCharacter* Body = nullptr;
        const bool bCreated = CreateBody(Grounds[Slot], *Monster, FString::Printf(TEXT("camp %s member %d"), *Spec.CampId.ToString(), Slot),
            Body, Error);
        if (Body) { Spawned.Add(Body); }
        if (!bCreated) { return false; }
    }
    TArray<FLDCampMemberSpawn> Requests;
    for (int32 Slot = 0; Slot < Spawned.Num(); ++Slot)
    {
        Requests.Add({State.FindCamp(Camp)->Members[Slot].EntityId, Spawned[Slot], Spec.MonsterId});
    }
    TArray<FLDCombatBodyRef> Refs;
    if (!Mode->SpawnCampMembers(Requests, Permit, Refs, Error)) { return false; }
    bCommitted = true;
    if (!State.CommitSpawn(Camp, Refs, Mode->GetAuthorityNowSeconds(), Error))
    {
        Error = TEXT("Core committed camp lives that camp bookkeeping refused: ") + Error;
        return false;
    }
    for (int32 Slot = 0; Slot < Spawned.Num(); ++Slot)
    {
        Bodies.Add(Refs[Slot].Victim.EntityId, Spawned[Slot]);
        Motion.Add(Refs[Slot].Victim.EntityId, {});
        Spawned[Slot]->SetActorHiddenInGame(false);
        Spawned[Slot]->SetActorTickEnabled(true);
    }
    UE_LOG(LogTemp, Display, TEXT("LD_CAMP_SPAWN camp=%s generation=%u members=%d life=%u at=%.3f"),
        *Spec.CampId.ToString(), State.FindCamp(Camp)->Generation, Spawned.Num(), Refs[0].Victim.Life, Mode->GetAuthorityNowSeconds());
    Error.Reset();
    return true;
}

bool ALDCampDirector::CreateBody(const FLDLegalGround& Ground, const FLDDemoMonsterDefinition& Monster, const FString& Label,
    ALDCampMemberCharacter*& Out, FString& Error)
{
    Out = nullptr;
    FCollisionShape Capsule;
    if (!ULDMapRegistry::ContentCapsule(*Content, Capsule, Error)) { return false; }
    // Contender placement's support gap: the capsule rests inside CMC's stable floor band, not on the surface itself.
    const double FloorGap = (UCharacterMovementComponent::MIN_FLOOR_DIST + UCharacterMovementComponent::MAX_FLOOR_DIST) * .5;
    const FVector Origin = Ground.SurfaceCm + FVector(0, 0, Capsule.GetCapsuleHalfHeight() + FloorGap);
    const FTransform Transform(FRotator::ZeroRotator, Origin);
    auto* Body = GetWorld()->SpawnActorDeferred<ALDCampMemberCharacter>(ALDCampMemberCharacter::StaticClass(), Transform,
        this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Body)
    {
        Error = TEXT("Camp body allocation failed.");
        return false;
    }
    Out = Body;
    Body->SetActorHiddenInGame(true);
    Body->SetActorTickEnabled(false);
    if (!Body->PrepareMonster(*Content, Monster, Error)) { return false; }
    Body->FinishSpawning(Transform);
    if (!IsValid(Body) || Body->IsActorBeingDestroyed())
    {
        Error = TEXT("Camp body failed native construction.");
        return false;
    }
    auto* Movement = Body->GetCharacterMovement();
    Movement->SetComponentTickEnabled(false);
    Movement->SetMovementMode(MOVE_Walking);
    Movement->FindFloor(Body->GetActorLocation(), Movement->CurrentFloor, false, nullptr);
    Movement->SetBaseFromFloor(Movement->CurrentFloor);
    if (!Movement->CurrentFloor.IsWalkableFloor() || Movement->CurrentFloor.HitResult.GetComponent() != Ground.Floor.Get()
        || !Body->GetActorLocation().Equals(Origin, .000001))
    {
        Error = FString::Printf(TEXT("Prepared body does not occupy its registered placement: %s walkable=%d floor=%s expected=%s location=%s origin=%s."),
            *Label, Movement->CurrentFloor.IsWalkableFloor(), *GetNameSafe(Movement->CurrentFloor.HitResult.GetComponent()),
            *GetNameSafe(Ground.Floor.Get()), *Body->GetActorLocation().ToString(), *Origin.ToString());
        return false;
    }
    Error.Reset();
    return true;
}

void ALDCampDirector::EndMatch()
{
    for (const auto& Pair : Bodies)
    {
        if (ALDCampMemberCharacter* Body = Pair.Value.Get()) { Body->Destroy(); }
    }
    Bodies.Reset();
    Motion.Reset();
    Returns.Reset();
    Stranded.Reset();
    BlockedRefresh.Reset();
    Boss = {};
    bBossBlockedLogged = false;
    BossSequence = 0;
    State.End();
    Content.Reset();
}

bool ALDCampDirector::AppendObservedFacts(const FLDMatchContext& Match, const FLDCombatBodyRef& Body,
    FLDObservedPublicFacts& InOut, FString& Error) const
{
    if (!State.IsActive() || Match != State.GetMatch())
    {
        Error = TEXT("Observed camp facts were requested for another match.");
        return false;
    }
    // D26: the Boss publishes only its creature identity; it has no camp cue.
    if (Boss.IsBody(Body))
    {
        if (InOut.CreatureId != 0 && InOut.CreatureId != Boss.Rules.CreatureId)
        {
            Error = TEXT("Observed Boss facts conflict with another producer.");
            return false;
        }
        InOut.CreatureId = Boss.Rules.CreatureId;
        Error.Reset();
        return true;
    }
    return State.AppendObservedFacts(Body, InOut, Error);
}
