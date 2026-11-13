#include "AI/LDOpponentController.h"
#include "Core/LDLog.h"
#include "AI/LDOpponentFrame.h"
#include "CollisionShape.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Engine/World.h"
#include "Evaluation/LDEvaluationPolicy.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Navigation/LDNavigationComponent.h"
#include "PK/LDPKDirector.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "World/LDWorldQueryTypes.h"

namespace LDOpponentControllerDetail
{
constexpr int32 MaxOpponentsPerRoom = 3;

// Lifecycle delivery to this provider's own adapters only; never a perception input.
template <typename TVisit>
void ForEachOpponent(UWorld* World, TVisit&& Visit)
{
    if (!World) { return; }
    TArray<TWeakObjectPtr<ALDOpponentController>> Found;
    for (FConstControllerIterator It = World->GetControllerIterator(); It; ++It)
    {
        if (ALDOpponentController* Opponent = Cast<ALDOpponentController>(It->Get())) { Found.Add(Opponent); }
    }
    for (const TWeakObjectPtr<ALDOpponentController>& Weak : Found)
    {
        if (ALDOpponentController* Opponent = Weak.Get()) { Visit(*Opponent); }
    }
}

FLDMatchProviderHooks MakeOpponentHooks()
{
    FLDMatchProviderHooks Hooks;
    Hooks.Prepare = [](ALDGameMode& Mode, const FLDMatchStartContext& Start, FString& Error)
    {
        UWorld* World = Mode.GetWorld();
        if (!World || Start.AISlots.Num() > MaxOpponentsPerRoom)
        {
            Error = TEXT("A Demo room holds at most three opponent AI contenders.");
            return false;
        }
        for (int32 Slot = 0; Slot < Start.AISlots.Num(); ++Slot)
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Params.ObjectFlags |= RF_Transient;
            ALDOpponentController* Opponent = World->SpawnActor<ALDOpponentController>(Params);
            if (!Opponent)
            {
                Error = FString::Printf(TEXT("Opponent AI slot %d could not create its server controller."), Slot);
                return false;
            }
            const FLDAIRegistrationResult Result = Mode.RegisterAIContender(*Opponent, uint8(Slot), Start.Match);
            if (Result.Outcome != ELDAIRegistration::Registered)
            {
                Error = FString::Printf(TEXT("Opponent AI slot %d was not registered: %s"), Slot, *Result.Error);
                Opponent->Destroy();
                return false;
            }
            // A failed bind aborts the start; MatchReset then retires every adapter of this match.
            if (!Opponent->BindRegisteredSelf(Result.Binding, Error)) { return false; }
        }
        return true;
    };
    Hooks.MatchClosed = [](ALDGameMode& Mode, const FLDMatchClosed& Closed)
    {
        ForEachOpponent(Mode.GetWorld(), [&Closed](ALDOpponentController& Opponent) { Opponent.HandleMatchClosed(Closed); });
    };
    Hooks.MatchReset = [](ALDGameMode& Mode, const FLDMatchReset& Reset, FString&)
    {
        // Destroying a possessing controller here would run the leave path mid-reset (pawn destroyed, Logout with
        // the record still present). The core retires the body like a human's; each adapter then ends itself.
        ForEachOpponent(Mode.GetWorld(), [&Reset](ALDOpponentController& Opponent) { Opponent.HandleMatchReset(Reset); });
        return true;
    };
    Hooks.LifeCommitted = [](ALDGameMode& Mode, const FLDLifeNotice& Notice)
    {
        ForEachOpponent(Mode.GetWorld(), [&Notice](ALDOpponentController& Opponent) { Opponent.HandleLifeCommitted(Notice); });
    };
    return Hooks;
}

const LDMatchProviders::FRegistrar OpponentProvider(ELDMatchProvider::OpponentAI, &MakeOpponentHooks);

ELDOpponentCircle CircleOf(ELDCircleState State)
{
    switch (State)
    {
    case ELDCircleState::Solo: return ELDOpponentCircle::Solo;
    case ELDCircleState::Contested: return ELDOpponentCircle::Contested;
    case ELDCircleState::Guarded: return ELDOpponentCircle::Guarded;
    default: return ELDOpponentCircle::Empty;
    }
}

ELDOpponentOrderKind OrderKindOf(ELDOpponentBodyKind Kind)
{
    switch (Kind)
    {
    case ELDOpponentBodyKind::MoveGround: return ELDOpponentOrderKind::MoveGround;
    case ELDOpponentBodyKind::AttackUnit: return ELDOpponentOrderKind::AttackUnit;
    default: return ELDOpponentOrderKind::None;
    }
}
}

ALDOpponentController::ALDOpponentController()
{
    PrimaryActorTick.bCanEverTick = true;
    // Reads the publication of the boundary that Combat's pre-physics driver completed this frame.
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

bool ALDOpponentController::BindRegisteredSelf(const FLDAIContenderBinding& InBinding, FString& Error)
{
    UWorld* World = GetWorld();
    ALDGameMode* Mode = World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
    const ULDMapRegistry* Registry = World ? World->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!HasAuthority() || !Mode || !InBinding.IsValid() || InBinding.GetMatch() != Mode->GetMatchContext()
        || Mode->ContenderId(this) != InBinding.GetContender() || (Binding.IsValid() && !(Binding == InBinding)))
    {
        Error = TEXT("Only the core-registered controller of this match's AI contender can bind it.");
        return false;
    }
    if (Binding == InBinding) { return true; }
    TSharedPtr<const FLDDemoContentSnapshot> Snapshot;
    const FLDDemoWorldGeometry* Geometry = Registry ? Registry->GetGeometry() : nullptr;
    TArray<FLDOpponentPOI> Places;
    if (!LDDemoEntry::LoadRuntimeContent(Snapshot, Error) || !Snapshot || !Geometry
        || !LDOpponentFrame::ReadPublicPOIs(*Geometry, *Snapshot, Places, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("The approved content or the public world is unavailable to the opponent."); }
        return false;
    }
    if (Snapshot->Get().Parameters.AI.DecisionIntervalMs <= 0)
    {
        Error = TEXT("The approved AI decision interval is not positive.");
        return false;
    }
    // Each contender approaches a separate standing position inside the shared sign-up disk.
    FVector SignupCentre;
    double SignupRadius = 0;
    const FLDMapBinding* SignupAnchor = nullptr;
    if (Mode->SignupDiskFor(InBinding.GetContender(), SignupCentre, SignupRadius))
    {
        const int32 Slot = Mode->GetRosterOrder().IndexOfByKey(InBinding.GetContender());
        SignupAnchor = Registry->Resolve(FName(*LDDemoWorld::PKSignupId(Slot)), Registry->Generation);
    }
    if (!SignupAnchor)
    {
        Error = TEXT("The opponent's roster slot has no registered PK sign-up position.");
        return false;
    }
    TArray<FLDOpponentPOI> Services;
    for (uint8 Index = 1; Index <= LDDemoWorld::RestServices; ++Index)
    {
        const FLDMapBinding* Anchor = Registry->Resolve(FName(*LDDemoWorld::RestServiceId(Index)), Registry->Generation);
        if (!Anchor || Anchor->Surface.IsNone())
        {
            Error = TEXT("The opponent cannot resolve a registered rest service and its ground.");
            return false;
        }
        FLDOpponentPOI& Place = Services.AddDefaulted_GetRef();
        Place.Id = Anchor->Id;
        Place.Kind = ELDOpponentPOIKind::RestService;
        Place.RestService = static_cast<ELDRestService>(Index);
        Place.SurfaceId = Anchor->Surface;
        Place.FootCm = Anchor->Position;
    }
    int32 OpeningSeed = 0;
    if (!Mode->DrawOpponentOpeningPlan(InBinding, OpeningPickAfterMs, OpeningSeed, Error)) { return false; }
    Binding = InBinding;
    Content = MoveTemp(Snapshot);
    POIs = MoveTemp(Places);
    SignupPOI = FLDOpponentPOI();
    SignupPOI.Id = SignupAnchor->Id;
    SignupPOI.Kind = ELDOpponentPOIKind::PKSignup;
    SignupPOI.SurfaceId = SignupAnchor->Surface;
    SignupPOI.FootCm = SignupAnchor->Position;
    RestServicePOIs = MoveTemp(Services);
#if WITH_DEV_AUTOMATION_TESTS
    Evaluation = LDEvaluationProfiles::Find(Binding.GetContender());
    if (Evaluation)
    {
        Evaluation->bBound = true;
        UE_LOG(LogLDAI, Display, TEXT("LD_EVAL_PROFILE contender=%s rival=%s site=%s choices=%d reaction_ms=%d aim_error_cm=%d skip_bp=%d"),
            *Binding.GetContender().ToString(), *Evaluation->Profile.Opponent.ToString(), *Evaluation->Profile.Site.ToString(),
            Evaluation->Profile.Choices.Num(), Evaluation->Profile.ReactionMs, Evaluation->Profile.AimErrorCm, Evaluation->Profile.SkipBp);
    }
#endif
    UE_LOG(LogLDAI, Log, TEXT("LD_AI_OPENING_PLAN contender=%s after_ms=%lld seed=%d"), *Binding.GetContender().ToString(),
        OpeningPickAfterMs, OpeningSeed);
    return true;
}

void ALDOpponentController::ForgetBodyWork()
{
    Incumbent = FOrderTrack();
    Candidate = FOrderTrack();
    StopTrack = FStopTrack();
    Action = FActionTrack();
    bWasInDorm = false;
}

void ALDOpponentController::HandleLifeCommitted(const FLDLifeNotice& Notice)
{
    if (!Binding.IsValid() || Notice.Match != Binding.GetMatch() || Notice.Body.Victim.Kind != ELDVictimKind::Contender
        || Notice.Body.Victim.EntityId != Binding.GetContender() || Notice.Change == ELDLifeChange::ProtectionEnded)
    {
        return;
    }
    // Joining, death, respawn and leave end every body-scoped request; the next decision reads the new life.
    ForgetBodyWork();
    NextDecisionAtMs = 0;
}

void ALDOpponentController::HandleMatchClosed(const FLDMatchClosed& Closed)
{
    if (!Binding.IsValid() || Closed.Match != Binding.GetMatch()) { return; }
    // The write gate is shut: no new intent and no receipt polling for this match.
    bClosed = true;
    ForgetBodyWork();
    Build = FBuildTrack();
    ReportTally();
}

void ALDOpponentController::HandleMatchReset(const FLDMatchReset& Reset)
{
    // Also reached by a controller whose start aborted before or during its bind.
    bClosed = true;
    bRetiring = true;
    RetiringMatch = Reset.Old;
    ForgetBodyWork();
    Build = FBuildTrack();
    ReportTally();
}

void ALDOpponentController::ReportTally()
{
    if (bTallyReported || !Binding.IsValid()) { return; }
    bTallyReported = true;
    UE_LOG(LogLDAI, Display, TEXT("LD_AI_SUMMARY contender=%s gen=%u orders=%u orders_refused=%u stops=%u actions=%u ")
        TEXT("actions_refused=%u dashes=%u/%u dorm_entries=%u airdrops=%u leader_hunts=%u boss_avoids=%u builds=%u builds_refused=%u paused_holds=%u/%u/%u ")
        TEXT("challenges=%u/%u/%u holder_attacks=%u owner_attacks=%u orders_rejected=%u pk=%u/%u/%u/%u"), *Binding.GetContender().ToString(),
        Binding.GetMatch().Generation, Tally.Orders, Tally.OrdersRefused, Tally.Stops, Tally.Actions, Tally.ActionsRefused,
        Tally.Dashes, Tally.DashesLanded, Tally.DormEntries, Tally.AirdropClaims, Tally.LeaderHunts, Tally.BossAvoids, Tally.Builds, Tally.BuildsRefused, Tally.PausedHolds,
        Tally.PausedHoldsKept, Tally.PausedHoldsReleased, Tally.Challenges, Tally.ChallengesKept, Tally.ChallengesReleased, Tally.HolderAttacks, Tally.OwnerAttacks,
        Tally.OrdersRejected, Tally.PKSignUps, Tally.PKBets, Tally.PKThrows, Tally.PKRefused);
}

void ALDOpponentController::NoteContest(const FLDOpponentDecisionFrame& Frame, const FLDOpponentDecision& Decision)
{
    const FLDOpponentPublicMatch& Public = Frame.Public;
    if (Frame.Self.Phase != ELDLifePhase::Alive || Public.Phase != ELDMatchPhase::CaptureOpen) { return; }
    const bool bAimed = Decision.Body.Kind == ELDOpponentBodyKind::AttackUnit || Decision.Body.Kind == ELDOpponentBodyKind::Activate;
    if (bAimed && Public.Owner.IsValid() && Public.Owner != Frame.Self.ContenderId && Frame.Units.ContainsByPredicate(
        [&](const FLDOpponentUnit& Unit) { return Unit.Handle == Decision.Body.Handle && Unit.ContenderId == Public.Owner; }))
    {
        ++Tally.OwnerAttacks;
    }
    if (Public.Circle != ELDOpponentCircle::Contested || !Public.Occupier.IsValid()) { return; }
    const bool bReleased = Decision.Mode == ELDOpponentMode::Disengage;
    if (Public.Occupier == Frame.Self.ContenderId)
    {
        ++Tally.PausedHolds;
        Tally.PausedHoldsKept += Decision.Mode == ELDOpponentMode::DefendOrHold ? 1 : 0;
        Tally.PausedHoldsReleased += bReleased ? 1 : 0;
        return;
    }
    if (Decision.Body.Kind == ELDOpponentBodyKind::AttackUnit && Frame.Units.ContainsByPredicate(
        [&](const FLDOpponentUnit& Unit) { return Unit.Handle == Decision.Body.Handle && Unit.ContenderId == Public.Occupier; }))
    {
        ++Tally.HolderAttacks;
    }
    if (!LDOpponentPolicy::InsideCircle(Frame, *Content)) { return; }
    ++Tally.Challenges;
    Tally.ChallengesKept += Decision.Mode == ELDOpponentMode::Contest || Decision.Mode == ELDOpponentMode::DefendOrHold
        || Decision.Mode == ELDOpponentMode::Engage ? 1 : 0;
    Tally.ChallengesReleased += bReleased ? 1 : 0;
}

void ALDOpponentController::ClientTravelInternal_Implementation(const FString&, ETravelType, bool, FGuid)
{
    UE_LOG(LogLDAI, Display, TEXT("LD_AI_TRAVEL_IGNORED contender=%s"), *Binding.GetContender().ToString());
}

void ALDOpponentController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UWorld* World = GetWorld();
    ALDGameMode* Mode = World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
    if (!HasAuthority()) { return; }
    if (bRetiring)
    {
        // After RetireMatch the record is gone, so ending this controller no longer runs the leave path.
        if (!Mode || Mode->GetMatchContext() != RetiringMatch) { Destroy(); }
        return;
    }
    if (!Mode || !Binding.IsValid() || !Content || bClosed) { return; }
    if (Mode->GetMatchContext() != Binding.GetMatch())
    {
        // A newer match never reuses this adapter; it ends itself on the next tick.
        bClosed = true;
        bRetiring = true;
        RetiringMatch = Binding.GetMatch();
        return;
    }
    const int64 NowMs = Mode->GetAuthorityNowMs();
    if (NowMs < NextDecisionAtMs) { return; }
    // A long frame plans once on the latest publication; missed decisions are never replayed.
    NextDecisionAtMs = NowMs + Content->Get().Parameters.AI.DecisionIntervalMs;
    FReads Reads;
    FLDOpponentDecisionFrame Frame;
    FString Problem;
    if (!BuildFrame(*Mode, NowMs, Reads, Frame, Problem))
    {
        FLDOpponentDecision Blocked;
        Blocked.Reason = ELDOpponentReason::ProviderUnavailable;
        Report(Frame, Blocked, Problem);
        return;
    }
    if (Reads.Owner.Life != BodyLife)
    {
        // Requests of an earlier life can only settle as stale; they are dropped without teaching anything.
        ForgetBodyWork();
        BodyLife = Reads.Owner.Life;
    }
    if (Reads.bCombat)
    {
        SettleOrders(Reads, Frame.Self, NowMs, Problem);
        SettleDirectAction(*Mode, Reads, Frame.Self, NowMs, Problem);
        Tally.DormEntries += Reads.Combat.bInDorm && !bWasInDorm ? 1 : 0;
        bWasInDorm = Reads.Combat.bInDorm;
    }
    if (Reads.ClaimedAirdrop && Reads.ClaimedAirdrop != LastClaimedAirdrop)
    {
        LastClaimedAirdrop = Reads.ClaimedAirdrop;
        ++Tally.AirdropClaims;
    }
    SettleBuild(*Mode, Frame.Self, NowMs, Problem);
    ComposeFrame(Frame);
#if WITH_DEV_AUTOMATION_TESTS
    const FLDOpponentDecision Decision = Evaluation
        ? LDEvaluationPolicy::Decide(Frame, *Content, Evaluation->Profile, Policy, Evaluation->State)
        : LDOpponentPolicy::Decide(Frame, *Content, Policy);
#else
    const FLDOpponentDecision Decision = LDOpponentPolicy::Decide(Frame, *Content, Policy);
#endif
    NoteContest(Frame, Decision);
    DispatchBody(*Mode, Reads, Frame, Decision.Body, NowMs, Problem);
    DispatchBuild(*Mode, Reads, Frame, Decision.Build, NowMs, Problem);
    DispatchPK(*Mode, Frame, Decision.PK, NowMs, Problem);
    Report(Frame, Decision, Problem);
}

bool ALDOpponentController::BuildFrame(ALDGameMode& Mode, int64 NowMs, FReads& Reads, FLDOpponentDecisionFrame& Frame,
    FString& Error) const
{
    using namespace LDOpponentControllerDetail;
    const uint32 Generation = Binding.GetMatch().Generation;
    FLDAISelfReadResult SelfRead = Mode.ReadAISelf(Binding);
    if (!SelfRead.Succeeded)
    {
        Error = SelfRead.Error.IsEmpty() ? TEXT("The core has no owner projection for this opponent.") : SelfRead.Error;
        return false;
    }
    if (!Mode.Combat || !Mode.Vision || !Mode.Development)
    {
        // A missing domain is an explicit stop; tracked requests stay pending until it answers.
        Error = TEXT("A combat, observation or progression domain of this match is unavailable.");
        return false;
    }
    Reads.Owner = MoveTemp(SelfRead.Self);
    const FLDOwnerState& Own = Reads.Owner;
    if (Own.Match != Generation || Own.ContenderId != Binding.GetContender())
    {
        Error = TEXT("The owner projection belongs to another match or contender.");
        return false;
    }
    const FLDPublicMatchView Public = Mode.ReadPublicMatch();
    if (Public.Generation != Generation)
    {
        Error = TEXT("The public match view belongs to another match.");
        return false;
    }
    // #198 §16.3: in a rest, the public room roster as every participant's HUD reads it (the own training dummy is not on it).
    if (Public.Phase == ELDMatchPhase::Rest)
    {
        const FLDLobbyView Lobby = Mode.ReadLobby();
        if (Lobby.Generation != Generation)
        {
            Error = TEXT("The public room roster belongs to another match.");
            return false;
        }
        for (const FLDLobbyMember& Member : Lobby.Members) { Frame.Public.Roster.Add(Member.ContenderId); }
    }
    Frame.Generation = Generation;
    Frame.POIs = POIs;
    if (Public.Phase == ELDMatchPhase::Rest)
    {
        Frame.SignupPOI = SignupPOI;
        Frame.RestServicePOIs = RestServicePOIs;
    }
    Frame.Public.Phase = Public.Phase;
    Frame.Public.NowMs = NowMs;
    Frame.Public.StartedAtMs = Public.StartedAtMs;
    // #198: the public timeline; camp growth and the build stage read it like a human client does.
    Frame.Public.Timeline = Public.Timeline;
    Frame.Public.FieldNowMs = LDMatchTimeline::FieldNowFromView(Public.Timeline, NowMs);
    Frame.Public.Circle = CircleOf(Public.CircleState);
    Frame.Public.Occupier = Public.CircleState == ELDCircleState::Solo || Public.CircleState == ELDCircleState::Contested
        ? Public.Occupier : FGuid();
    Frame.Public.DueAtMs = Public.DueAtMs;
    Frame.Public.Owner = Public.GraffitiOwner;
    Frame.Public.GuardDueAtMs = Public.GuardDueAtMs;
    Frame.Public.Leader = Public.Leader.Leader;
    Frame.Public.LeaderCircleCentreCm = Public.Leader.CircleCentreCm;
    // #198 D: the public PK (entrants, sides, bettors, bets, arena centre, fight start), as every participant reads it.
    Frame.Public.PK = Public.PK;
    // The own claim comes from the owner view; the public view never names the claimer.
    Reads.ClaimedAirdrop = Own.Growth.AirdropClaimedSerial;
    if (Public.Airdrop.Phase == ELDAirdropPhase::Announced || Public.Airdrop.Phase == ELDAirdropPhase::Landed)
    {
        Frame.Public.AirdropPOI = Public.Airdrop.PointId;
        Frame.Public.bAirdropLanded = Public.Airdrop.Phase == ELDAirdropPhase::Landed;
    }
    // #198: the lying charms, exactly as every participant's public view lists them.
    for (const FLDCharmView& Charm : Public.Charms) { Frame.Public.Charms.Emplace(Charm.PointId, Charm.Kind); }
    // #198 outer §5: the open camp markers, as every participant's public view lists them.
    for (const FLDFieldMarkerView& Marker : Public.Markers)
    {
        if (Marker.Phase == ELDFieldMarkerPhase::Revealed) { Frame.Public.MarkedCamps.Add(Marker.CampId); }
    }
    FLDOpponentSelf& Me = Frame.Self;
    Me.ContenderId = Own.ContenderId;
    Me.Phase = Own.LifePhase;
    Me.Life = Own.Life;
    Me.NearbyRestService = Own.NearbyRestService;
    // Unspawned or left: the policy stops without any further read.
    if (Own.LifePhase != ELDLifePhase::Alive && Own.LifePhase != ELDLifePhase::Dead) { return true; }
    const FLDGrowthOwnerView& Growth = Own.Growth;
    if (Growth.Generation != Generation
        || !LDOpponentFrame::ReadOwnBuild(Growth.Progression, *Content, Me, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("The own growth publication is not of this match."); }
        return false;
    }
    // The opening rest R0 starts at the match start; the drawn plan waits OpeningPickAfterMs into it.
    Me.OpeningPickAtMs = Public.StartedAtMs + OpeningPickAfterMs;
    if (Own.LifePhase == ELDLifePhase::Dead) { return true; }
    if (!Mode.Combat->ResolveControllerCaller(*this, Reads.CombatCaller, Error)
        || !Mode.Combat->BuildOwnerView(Reads.CombatCaller, Reads.Combat))
    {
        if (Error.IsEmpty()) { Error = TEXT("The own combat projection is unavailable."); }
        return false;
    }
    if (Reads.Combat.Generation != Generation || Reads.Combat.Life != Own.Life
        || !LDOpponentFrame::ReadOwnCombat(Reads.Combat, NowMs, Me, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("The own combat projection is not of the current life yet."); }
        return false;
    }
    Reads.bCombat = true;
    FLDWorldView View;
    if (!Mode.Vision->EnumerateVisibleUnits(*this, ELDObservationPurpose::NewAttackOrder, View, Error)) { return false; }
    if (View.ObserverLife != Own.Life || View.Footprint.SourceStatus != ELDObservationSource::ActiveBody)
    {
        Error = TEXT("The own observation frame is not of the current living body yet.");
        return false;
    }
    if (!LDOpponentFrame::ReadObservedUnits(View, Generation, *Content, Frame.Units, Error)) { return false; }
    Frame.ViewRevision = View.Revision;
    Me.FootCm = View.Footprint.SourceFeetCm;
    if (Public.Phase == ELDMatchPhase::CaptureOpen && ((Public.CircleState == ELDCircleState::Solo
        && Public.Occupier != Own.ContenderId) || (Public.GraffitiOwner.IsValid() && Public.GraffitiOwner != Own.ContenderId)))
    {
        double PathCm = 0;
        FString PathError;
        // An unanswered path stays unknown; the policy then treats the public deadline conservatively.
        if (ReadCapturePath(Me.FootCm, PathCm, PathError)) { Frame.CapturePathCm = PathCm; }
    }
    return true;
}

bool ALDOpponentController::ReadCapturePath(const FVector& FootCm, double& OutCm, FString& Error) const
{
    const ULDMapRegistry* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    const FLDOpponentPOI* Capture = POIs.FindByPredicate([](const FLDOpponentPOI& POI) { return POI.Kind == ELDOpponentPOIKind::Capture; });
    FCollisionShape Capsule;
    if (!Registry || !Registry->Enabled() || !Capture || !ULDMapRegistry::ContentCapsule(*Content, Capsule, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("The public capture path cannot be queried."); }
        return false;
    }
    FLDLegalGround From, To;
    From.World = To.World = Registry->Revision();
    From.FootCm = FootCm;
    // One registered ground (D02); the registry still re-checks the body's real support for this hint.
    From.SurfaceId = Capture->SurfaceId;
    To.FootCm = Capture->FootCm;
    To.SurfaceId = Capture->SurfaceId;
    FLDCompletePath Path;
    ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
    if (!Registry->QueryCompletePath(Registry->Revision(), From, To, Capsule, Path, Failure, Error)) { return false; }
    // To the public centre: never shorter than the path to the circle edge, so the estimate stays conservative.
    OutCm = Path.LengthCm;
    return true;
}

FLDOrderCaller ALDOpponentController::OrderCaller(const FReads& Reads)
{
    FLDOrderCaller Caller;
    Caller.Match = Binding.GetMatch();
    Caller.Body = Reads.Owner.BodyRef;
    Caller.Controller = this;
    return Caller;
}

void ALDOpponentController::Learn(const FLDOpponentRetryKey& Key, ELDOpponentSettle Settle, const FLDOpponentSelf& Self,
    int64 NowMs)
{
    ELDOpponentOutcome Outcome = ELDOpponentOutcome::Failed;
    if (LDOpponentRequests::ToOutcome(Settle, Outcome))
    {
        LDOpponentPolicy::RecordOutcome(Policy, Key, Outcome, Self, NowMs, Content->Get().Parameters.AI);
    }
}

void ALDOpponentController::SettleOrders(const FReads& Reads, const FLDOpponentSelf& Self, int64 NowMs, FString& Problem)
{
    if (!Navigation) { return; }
    const FLDOrderCaller Caller = OrderCaller(Reads);
    // Navigation applies a queued request at the next Actions phase; past that promise plus the longest retry
    // wait a request counts as a failed attempt instead of holding the body forever.
    const int64 Patience = Content->Get().Parameters.AI.RetryMaxBackoffMs;
    if (StopTrack.bHas)
    {
        const FLDOrderReceipt Receipt = Navigation->QueryOrder(Caller, StopTrack.Identity);
        if (Receipt.Status != ELDOrderStatus::Queued || NowMs > StopTrack.SentAtMs + Patience) { StopTrack.bHas = false; }
    }
    if (Candidate.bHas)
    {
        const FLDOrderReceipt Receipt = Navigation->QueryOrder(Caller, Candidate.Request.Identity);
        const ELDOpponentSettle Settle = LDOpponentRequests::SettleOrder(Receipt);
        if (Receipt.Status == ELDOrderStatus::Queued)
        {
            if (NowMs <= Candidate.SentAtMs + Patience) { return; }
            Problem = TEXT("order admission overdue");
            Learn(Candidate.Intent.Key, ELDOpponentSettle::Failed, Self, NowMs);
            Candidate = FOrderTrack();
        }
        else if (Receipt.Status == ELDOrderStatus::Rejected)
        {
            // A refused replacement leaves the incumbent order running unchanged.
            ++Tally.OrdersRejected;
            Learn(Candidate.Intent.Key, Settle, Self, NowMs);
            // Only a refusal for ground or path speaks about the body's walking; any other leaves its budget as it was.
            if (Receipt.Failure == ELDOrderFailure::InvalidGround || Receipt.Failure == ELDOrderFailure::NoCompletePath)
            {
                LDOpponentPolicy::RecordWalkAdmission(Policy, Candidate.Intent.Key, true, Self, NowMs, Content->Get().Parameters.AI);
            }
            Candidate = FOrderTrack();
        }
        else
        {
            // Admitted, or cancelled together with the incumbent by a Stop or a life end: it replaced the incumbent.
            // An admitted attack may strike in place without planning any path, so only a ground walk proves the body can walk.
            if (Receipt.Status != ELDOrderStatus::Cancelled && Candidate.Intent.Kind != ELDOpponentBodyKind::AttackUnit)
            {
                LDOpponentPolicy::RecordWalkAdmission(Policy, Candidate.Intent.Key, false, Self, NowMs, Content->Get().Parameters.AI);
            }
            Incumbent = Candidate;
            Incumbent.Status = LDOpponentFrame::OrderStatus(Receipt.Status);
            Candidate = FOrderTrack();
            if (Settle != ELDOpponentSettle::InFlight) { FinishIncumbent(Settle, Self, NowMs); }
            return;
        }
    }
    if (Incumbent.bHas && !Incumbent.bFinal)
    {
        const FLDOrderReceipt Receipt = Navigation->QueryOrder(Caller, Incumbent.Request.Identity);
        Incumbent.Status = LDOpponentFrame::OrderStatus(Receipt.Status);
        const ELDOpponentSettle Settle = LDOpponentRequests::SettleOrder(Receipt);
        if (Settle != ELDOpponentSettle::InFlight) { FinishIncumbent(Settle, Self, NowMs); }
    }
}

void ALDOpponentController::FinishIncumbent(ELDOpponentSettle Settle, const FLDOpponentSelf& Self, int64 NowMs)
{
    Incumbent.bFinal = true;
    Learn(Incumbent.Intent.Key, Settle, Self, NowMs);
}

void ALDOpponentController::SettleDirectAction(ALDGameMode& Mode, const FReads& Reads, const FLDOpponentSelf& Self,
    int64 NowMs, FString& Problem)
{
    if (!Action.bHas || Action.bFinal) { return; }
    const FLDCombatActionReceipt Receipt = Mode.Combat->QueryActionReceipt(Reads.CombatCaller, Action.DirectSequence);
    ELDOpponentSettle Settle = LDOpponentRequests::SettleAction(Receipt);
    if (Settle == ELDOpponentSettle::InFlight)
    {
        // Queued is admitted at the next Actions phase; Accepted ends at the release time Combat published for it.
        const bool bReleasing = Receipt.Phase == ELDCombatPhase::Accepted && Receipt.CombatToken != 0
            && Reads.Combat.ActionToken == Receipt.CombatToken;
        const int64 PromisedMs = bReleasing ? FMath::Max(Action.SentAtMs, Reads.Combat.ActionReleaseAtMs) : Action.SentAtMs;
        if (NowMs <= PromisedMs + Content->Get().Parameters.AI.RetryMaxBackoffMs) { return; }
        FLDCombatCancelRequest Cancel;
        Cancel.Generation = Binding.GetMatch().Generation;
        Cancel.Life = Reads.Owner.Life;
        Cancel.ThroughDirectSequence = Action.DirectSequence;
        // One cancel bounded by this sequence; released effects and later sequences are untouched.
        Mode.Combat->CancelAction(Reads.CombatCaller, Cancel);
        Problem = TEXT("action receipt overdue; cancelled");
        Settle = ELDOpponentSettle::Failed;
    }
    Action.bFinal = true;
    Tally.DashesLanded += Settle == ELDOpponentSettle::Succeeded && Action.Intent.Source == ELDOpponentSourceSlot::Dash ? 1 : 0;
    Learn(Action.Intent.Key, Settle, Self, NowMs);
}

void ALDOpponentController::SettleBuild(ALDGameMode& Mode, const FLDOpponentSelf& Self, int64 NowMs, FString& Problem)
{
    if (!Build.bHas || !Mode.Development) { return; }
    const ELDOpponentSettle Settle = LDOpponentRequests::SettleBuildQuery(
        Mode.Development->QueryBuildReceipt(Binding.GetContender(), Build.Query));
    if (Settle == ELDOpponentSettle::InFlight)
    {
        // Growth commits at the next Transactions phase. A late receipt is reported; the request is never
        // replaced, because its family accepts no other request while it is pending.
        if (NowMs > Build.SentAtMs + Content->Get().Parameters.AI.RetryMaxBackoffMs) { Problem = TEXT("build receipt overdue"); }
        return;
    }
    Learn(Build.Key, Settle, Self, NowMs);
    Build = FBuildTrack();
}

void ALDOpponentController::ComposeFrame(FLDOpponentDecisionFrame& Frame) const
{
    using namespace LDOpponentControllerDetail;
    FLDOpponentOrder& Current = Frame.Order;
    Current = FLDOpponentOrder();
    if (Action.bHas && !Action.bFinal)
    {
        Current.Kind = ELDOpponentOrderKind::Action;
        Current.Status = ELDOpponentOrderStatus::Active;
    }
    else if (Candidate.bHas)
    {
        // The latest intent stays visible while Navigation has not admitted or refused it.
        Current.Kind = OrderKindOf(Candidate.Intent.Kind);
        Current.Status = ELDOpponentOrderStatus::Pending;
        Current.POI = Candidate.Intent.POI;
        Current.Handle = Candidate.Intent.Handle;
        Current.Epoch = Candidate.Intent.Epoch;
    }
    else if (Incumbent.bHas)
    {
        Current.Kind = OrderKindOf(Incumbent.Intent.Kind);
        Current.Status = Incumbent.Status;
        Current.POI = Incumbent.Intent.POI;
        Current.Handle = Incumbent.Intent.Handle;
        Current.Epoch = Incumbent.Intent.Epoch;
    }
    Frame.bBuildChangeInFlight = Build.bHas;
    Frame.PKThrowReadyAtMs = PKThrowSerial != 0 && PKThrowSerial == Frame.Public.PK.Serial ? PKThrowReadyAtMs : 0;
}

void ALDOpponentController::DispatchBody(ALDGameMode& Mode, const FReads& Reads, const FLDOpponentDecisionFrame& Frame,
    const FLDOpponentBodyIntent& Intent, int64 NowMs, FString& Problem)
{
    if (Intent.Kind == ELDOpponentBodyKind::None || Intent.Kind == ELDOpponentBodyKind::Keep) { return; }
    const FLDOpponentSelf& Self = Frame.Self;
    const uint32 Generation = Binding.GetMatch().Generation;
    const uint32 Life = Reads.Owner.BodyRef.Victim.Life;
    if (!Reads.bCombat || !Navigation || !Mode.Combat)
    {
        Problem = TEXT("A body intent arrived without a current living body.");
        return;
    }
    // Taunted: Navigation chases the taunter and Combat refuses abilities; wait instead of retrying.
    if (LDOpponentRequests::BodyIntentsHeld(Reads.Combat, NowMs)) { return; }
    if (Intent.Kind == ELDOpponentBodyKind::Stop)
    {
        // One stop covers the incumbent and a queued candidate. A refused stop is retried a bounded number of
        // times for the same orders; they then run to their own end.
        const bool bLive = Candidate.bHas || (Incumbent.bHas && !Incumbent.bFinal);
        if (!bLive || StopTrack.bHas || StopTrack.Attempts >= Content->Get().Parameters.AI.RetryMaxAttempts) { return; }
        FLDMoveIdentity Identity;
        ++StopTrack.Attempts;
        if (!LDOpponentRequests::NextMoveIdentity(LastIdentity, Generation, Life, true, Identity))
        {
            Problem = TEXT("The order identity space of this life is exhausted.");
            return;
        }
        LastIdentity = Identity;
        ++Tally.Stops;
        const FLDOrderReceipt Receipt = Navigation->CancelOrder(OrderCaller(Reads), Identity, ELDOrderCancelReason::Stop);
        if (Receipt.Status == ELDOrderStatus::Rejected)
        {
            Problem = FString::Printf(TEXT("stop refused failure=%u"), uint32(Receipt.Failure));
            return;
        }
        StopTrack.bHas = true;
        StopTrack.Identity = Identity;
        StopTrack.SentAtMs = NowMs;
        return;
    }
    if (Intent.Kind == ELDOpponentBodyKind::Activate)
    {
        FLDCombatActionRequest Request;
        FString Error;
        if (DirectSequence == MAX_uint64 || !LDOpponentRequests::BuildAction(Intent, Generation, Life, DirectSequence + 1,
            Reads.Combat.SourceRevision, Request, Error))
        {
            Problem = Error.IsEmpty() ? FString(TEXT("The direct sequence of this match is exhausted.")) : Error;
            Learn(Intent.Key, ELDOpponentSettle::Failed, Self, NowMs);
            return;
        }
        DirectSequence = Request.DirectSequence;
        ++Tally.Actions;
        Tally.Dashes += Intent.Source == ELDOpponentSourceSlot::Dash ? 1 : 0;
        const ELDOpponentSettle Settle = LDOpponentRequests::SettleAction(Mode.Combat->SubmitAction(Reads.CombatCaller, Request));
        Action = FActionTrack();
        Action.bHas = true;
        Action.Intent = Intent;
        Action.DirectSequence = Request.DirectSequence;
        Action.SentAtMs = NowMs;
        if (Settle != ELDOpponentSettle::InFlight)
        {
            // A fresh submission resolves at once only when Combat refused it on receipt.
            ++Tally.ActionsRefused;
            Action.bFinal = true;
            Learn(Intent.Key, Settle, Self, NowMs);
        }
        return;
    }
    FLDMoveIdentity Identity;
    FLDSpatialOrderRequest Request;
    FString Error;
    // Rest walks are admitted only to the own sign-up disk or the registered service anchors.
    const bool bSignupWalk = Frame.SignupPOI.IsSet() && Intent.Kind == ELDOpponentBodyKind::MoveGround && Intent.POI == SignupPOI.Id;
    const bool bServiceWalk = Intent.Kind == ELDOpponentBodyKind::MoveGround
        && Frame.RestServicePOIs.ContainsByPredicate([&Intent](const FLDOpponentPOI& Place) { return Place.Id == Intent.POI; });
    const TArray<FLDOpponentPOI> SignupPlaces = bSignupWalk ? TArray<FLDOpponentPOI>{SignupPOI} : TArray<FLDOpponentPOI>();
    const TArray<FLDOpponentPOI>& Places = bSignupWalk ? SignupPlaces : bServiceWalk ? Frame.RestServicePOIs : POIs;
    if (!LDOpponentRequests::NextMoveIdentity(LastIdentity, Generation, Life, false, Identity)
        || !LDOpponentRequests::BuildOrder(Intent, Places, Identity, Request, Error))
    {
        Problem = Error.IsEmpty() ? FString(TEXT("The order identity space of this life is exhausted.")) : Error;
        Learn(Intent.Key, ELDOpponentSettle::Failed, Self, NowMs);
        return;
    }
    LastIdentity = Identity;
    ++Tally.Orders;
    Tally.PKSignUps += bSignupWalk ? 1 : 0;
    const FLDOrderReceipt Receipt = Navigation->SubmitOrder(OrderCaller(Reads), Request);
    StopTrack = FStopTrack();
    if (Receipt.Status == ELDOrderStatus::Rejected)
    {
        // Refused on receipt; any incumbent order keeps running.
        ++Tally.OrdersRefused;
        Learn(Intent.Key, LDOpponentRequests::SettleOrder(Receipt), Self, NowMs);
        return;
    }
    // A newer submission replaces a still-queued candidate inside Navigation as well.
    Candidate = FOrderTrack();
    Candidate.bHas = true;
    Candidate.Intent = Intent;
    Candidate.Request = Request;
    Candidate.Status = ELDOpponentOrderStatus::Pending;
    Candidate.SentAtMs = NowMs;
}

void ALDOpponentController::DispatchBuild(ALDGameMode& Mode, const FReads& Reads, const FLDOpponentDecisionFrame& Frame,
    const FLDOpponentBuildIntent& Intent, int64 NowMs, FString& Problem)
{
    if (Intent.Kind == ELDOpponentBuildKind::None || Build.bHas) { return; }
    const FLDOpponentSelf& Self = Frame.Self;
    FString Error;
    FLDBuildCommand Command;
    if (!Mode.Development || !LDOpponentRequests::BuildCommand(Intent, Reads.Owner.Growth.Progression, *Content, BuildRequestId,
        Command, Error))
    {
        Problem = Error.IsEmpty() ? FString(TEXT("The progression domain is unavailable.")) : Error;
        Learn(Intent.Key, ELDOpponentSettle::Failed, Self, NowMs);
        return;
    }
    ++Tally.Builds;
    const FLDSubmitResult Result = Mode.Development->QueueBuild(Binding.GetContender(), Command);
    const uint64 RequestId = BuildRequestId;
    // Only a reserved sequence is consumed; an unreserved admission refusal leaves the id to the next request.
    if (Result.bSequenceReserved) { ++BuildRequestId; }
    const ELDOpponentSettle Settle = LDOpponentRequests::SettleSubmit(Result);
    if (Settle == ELDOpponentSettle::InFlight)
    {
        Build.bHas = true;
        Build.Query.Generation = Binding.GetMatch().Generation;
        Build.Query.Family = ELDGrowthFamily::Build;
        Build.Query.RequestId = RequestId;
        Build.Key = Intent.Key;
        Build.SentAtMs = NowMs;
        return;
    }
    if (Settle != ELDOpponentSettle::Succeeded)
    {
        ++Tally.BuildsRefused;
        Problem = FString::Printf(TEXT("build request refused status=%u reason=%u"), uint8(Result.Status), uint32(Result.Reason));
    }
    Learn(Intent.Key, Settle, Self, NowMs);
}

void ALDOpponentController::DispatchPK(ALDGameMode& Mode, const FLDOpponentDecisionFrame& Frame,
    const FLDOpponentPKIntent& Intent, int64 NowMs, FString& Problem)
{
    if (Intent.Kind == ELDOpponentPKKind::None) { return; }
    const uint32 Generation = Binding.GetMatch().Generation;
    const FString Contender = Binding.GetContender().ToString();
    if (!Mode.PKDirector)
    {
        // A bet or throw names a public PK, so its director must exist; a missing one is an explicit stop of this request.
        ++Tally.PKRefused;
        Problem = TEXT("A PK request arrived without the match's PK director.");
        return;
    }
    if (Intent.Kind == ELDOpponentPKKind::Bet)
    {
        ++Tally.PKBets;
        const ELDPKBetResult Result = Mode.PKDirector->SubmitBet(Binding.GetContender(), Generation, Intent.Serial, Intent.Side);
        UE_LOG(LogLDAI, Display, TEXT("LD_AI_PK_BET contender=%s gen=%u pk=%u side=%u result=%u"), *Contender, Generation,
            Intent.Serial, uint32(Intent.Side), uint32(Result));
        if (Result != ELDPKBetResult::Placed)
        {
            ++Tally.PKRefused;
            Problem = FString::Printf(TEXT("PK bet refused result=%u"), uint32(Result));
        }
        return;
    }
    ++Tally.PKThrows;
    int64 ReadyAtMs = 0;
    const ELDPKThrowResult Result = Mode.PKDirector->SubmitThrow(Binding.GetContender(), Generation, Intent.Serial,
        Intent.TargetCm, ReadyAtMs);
    PKThrowSerial = Intent.Serial;
    // A thrown or cooling-down answer names the own ready time; any other refusal waits one approved throw cooldown before
    // the next attempt, so a refused target is never re-sent on every decision.
    PKThrowReadyAtMs = Result == ELDPKThrowResult::Thrown || Result == ELDPKThrowResult::CoolingDown
        ? ReadyAtMs : FMath::Max(ReadyAtMs, NowMs + Content->Get().Parameters.ThrowCooldownMs);
    UE_LOG(LogLDAI, Display, TEXT("LD_AI_PK_THROW contender=%s gen=%u pk=%u result=%u ready_ms=%lld"), *Contender, Generation,
        Intent.Serial, uint32(Result), PKThrowReadyAtMs);
    if (Result != ELDPKThrowResult::Thrown)
    {
        ++Tally.PKRefused;
        Problem = FString::Printf(TEXT("PK throw refused result=%u"), uint32(Result));
    }
}

void ALDOpponentController::Report(const FLDOpponentDecisionFrame& Frame, const FLDOpponentDecision& Decision,
    const FString& Problem)
{
    if (Decision.Mode == ReportedMode && Decision.Reason == ReportedReason && Problem == ReportedProblem) { return; }
    Tally.LeaderHunts += Decision.Reason == ELDOpponentReason::HuntLeader && ReportedReason != ELDOpponentReason::HuntLeader ? 1 : 0;
    Tally.BossAvoids += Decision.Reason == ELDOpponentReason::AvoidBoss && ReportedReason != ELDOpponentReason::AvoidBoss ? 1 : 0;
    ReportedMode = Decision.Mode;
    ReportedReason = Decision.Reason;
    ReportedProblem = Problem;
    // Bounded reason codes and own input revisions only; never another contender's private state.
    UE_LOG(LogLDAI, Display, TEXT("LD_AI_DECISION contender=%s gen=%u life=%u view=%u mode=%u reason=%u body=%u build=%u%s%s"),
        *Binding.GetContender().ToString(), Frame.Generation, Frame.Self.Life, Frame.ViewRevision, uint32(Decision.Mode),
        uint32(Decision.Reason), uint32(Decision.Body.Kind), uint32(Decision.Build.Kind),
        Problem.IsEmpty() ? TEXT("") : TEXT(" problem="), *Problem);
}
