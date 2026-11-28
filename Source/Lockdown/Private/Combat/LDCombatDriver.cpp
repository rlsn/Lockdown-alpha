#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatPreInput.h"
#include "PK/LDPKDirector.h"
#include "Core/LDLog.h"
#include "Core/LDTrace.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthDirector.h"
#include "Framework/LDGameMode.h"
#include "World/LDCampDirector.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"

ALDGameMode* ALDCombatDirector::Core() const
{
    auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<ALDGameMode>():nullptr;
    return Mode && Mode->Combat==this ? Mode : nullptr;
}

ILDLifeAuthority* ALDCombatDirector::Authority() const
{
    return Core();
}

const FLDDemoContentSnapshot* ALDCombatDirector::Catalog() const
{
    const auto* Mode=Core();
    return Mode && Mode->DemoContent ? Mode->DemoContent.Get() : nullptr;
}

FVector ALDCombatDirector::FootOf(const ALDCharacter& Body)
{ return Body.GetActorLocation()-FVector(0,0,Body.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()); }

int64 ALDCombatDirector::NextDueMs(int64 NowMs) const
{
    int64 Due=MAX_int64;
    const auto Consider=[&Due,NowMs](int64 At) { if (At>NowMs && At<Due) { Due=At; } };
    const FLDDemoContentSnapshot* Content=Catalog();
    const int64 RecoveryDelayMs=Content ? Content->Get().Parameters.OutOfCombatRegenDelayMs : -1;
    const int64 DormDelayMs=Content ? Content->Get().Parameters.DormRegenDelayMs : -1;
    Consider(NextBranchDueMs(NowMs));
    for (const auto& Pair : Bodies)
    {
        const auto& Body=Pair.Value;
        if (Body.bPendingDeath) { continue; }
        if (Body.Action.IsSet()) { Consider(Body.Action->ReleaseAtMs); }
        for (const auto& Root : Body.Roots) { Consider(Root.ExpiresAtMs); }
        for (const auto& Disarm : Body.Disarms) { Consider(Disarm.ExpiresAtMs); }
        for (const auto& Stun : Body.Stuns) { Consider(Stun.ExpiresAtMs); }
        // #147: silences, the shared breach and the burn's ticks end or fall on exact boundaries.
        for (const auto& Silence : Body.Silences) { Consider(Silence.ExpiresAtMs); }
        if (Body.SharedBreach.Stacks>0) { Consider(Body.SharedBreach.ExpiresAtMs); }
        if (!Body.Burn.Layers.IsEmpty()) { Consider(LDCombatKeywords::NextBurnTickAtMs(Body.Burn)); }
        for (const auto& Package : Body.Empowers) { Consider(Package.ExpiresAtMs); }
        if (Proto.bAgency && !Body.Agency.Losses.IsEmpty()) { Consider(LDCombatControl::AgencyDueMs(Body.Agency,NowMs,Proto.Agency)); }
        if (Body.VoluntaryDash.IsSet()) { Consider(Body.VoluntaryDash->EndMs); }
        if (Body.Shield.IsSet()) { Consider(Body.Shield->ExpiresAtMs); }
        for (const FLDBodyCombatState::FShieldLayer& Layer : Body.ShieldLayers) { Consider(Layer.ExpiresAtMs); }
        for (const FLDBodyCombatState::FStaggerBleed& Bleed : Body.StaggerBleeds) { Consider(Bleed.NextTickMs); }
        Consider(Body.ParryUntilMs);
        Consider(Body.FrenzyUntilMs);
        Consider(Body.EagleLingerUntilMs);
        if (Body.Taunt.IsSet()) { Consider(Body.Taunt->ExpiresAtMs); }
        Consider(Body.DashUntilMs);
        Consider(Body.MonsterNextAttackAtMs);
        Consider(Body.MonsterGuardUntilMs);
        Consider(Body.MonsterRecoverUntilMs);
        // D21: a contender's out-of-combat recovery starts on its own exact boundary.
        if (Body.Victim.Kind==ELDVictimKind::Contender && RecoveryDelayMs>=0 && Body.LastCombatAtMs<=MAX_int64-RecoveryDelayMs)
        { Consider(Body.LastCombatAtMs+RecoveryDelayMs); }
        // D21 (#79): so does the dorm's fast recovery of a contender standing in a dorm.
        if (Body.bInDorm && DormDelayMs>=0 && Body.LastCombatAtMs<=MAX_int64-DormDelayMs)
        { Consider(Body.LastCombatAtMs+DormDelayMs); }
        // Temporary terms end exactly on their deadlines so the next motion uses the restored speed.
        for (const auto& Slow : Body.Slows) { Consider(Slow.ExpiresAtMs); }
        // #198 session D: a throw's Exposed ends on its exact boundary.
        Consider(Body.ExposedUntilMs);
        for (const auto& Bonus : Body.TimedBonuses) { Consider(Bonus.ExpiresAtMs); }
    }
    // #112 mechanics: zone pulses and their records' ends.
    for(const auto& Strike:MonsterStrikes){Consider(Strike.AtMs);}
    for(const auto& Drop:SlipperDrops){Consider(Drop.ExpiresAtMs);}
    for(const auto& Cordon:Cordons){Consider(Cordon.ExpiresAtMs);}
    for(const auto& Burst:ShardBursts){Consider(Burst.ExpiresAtMs);}
    Consider(NextMechanicsDueMs(NowMs));
    Consider(NextCardsDueMs(NowMs));
    // The basic cadence and every ability cooldown end on exact boundaries, not one frame late.
    for (const auto& Pair : Records)
    {
        Consider(Pair.Value.NextAttackAtMs);
        for (const auto& Track : Pair.Value.Tracks) { Consider(Track.ReadyAtMs); }
    }
    if (const auto* Mode=Core())
    {
        Consider(Mode->NextLifeDueMs());
        // #198: every rest step and field segment ends on an exact boundary.
        Consider(Mode->NextTimelineDueMs());
        Consider(Mode->NextShowdownDueMs());
        // P6: mid-field objective, daily modifier, locker and dealer instants (field progress T).
        Consider(Mode->NextPacingDueMs());
        // #198 session D: PK ring ticks and throw landings.
        if (Mode->PKDirector) { Consider(Mode->PKDirector->NextDueMs()); }
        // Opening time and the Solo completion must each be an exact step boundary; a resume inside a paused
        // contest is closed at its own instant by CaptureCompletesInsideStep, so it adds no stop here.
        Consider(Mode->NextCaptureDueMs());
        // A cleared camp refreshes exactly at its integer deadline, not on the following boundary.
        if (Mode->Camps) { Consider(Mode->Camps->NextRefreshDueMs()); }
        // D26: an airdrop is announced and lands on its exact boundary.
        Consider(Mode->NextAirdropDueMs());
        // #198 outer §5: a camp marker is revealed on its exact field-progress boundary.
        Consider(Mode->NextFieldMarkerDueMs());
        // D27: the leader notice refreshes on its exact boundary.
        Consider(Mode->NextLeaderDueMs());
    }
    return Due;
}

bool ALDCombatDirector::AdvanceAuthority(const FLDMatchContext& Match,int64 ThroughMs,FString& Error)
{
    auto* Mode=Core();
    if (!Mode || !Mode->Vision || bAdvancing || !Mode->IsGameplayWriteOpen(Match)
        || ThroughMs<Mode->GetAuthorityNowMs())
    {
        Error=TEXT("The current Combat driver cannot advance this closed, stale or reentrant step.");
        return false;
    }
    int64 MaxStepMs=0;
    if (!LDMatchLifecycle::AuthoritySecondsToNowMs(GetDefault<ULDCharacterMovementComponent>()->MaxSimulationTimeStep,MaxStepMs,Error)
        || MaxStepMs<=0)
    {
        Error=TEXT("Native CMC has no representable positive simulation step.");
        return false;
    }
    TGuardValue<bool> Advancing(bAdvancing,true);
    while (Mode->IsGameplayWriteOpen(Match) && (Mode->IsCloseAwaitingSettlement() || Mode->GetAuthorityNowMs()<ThroughMs))
    {
        if (Mode->IsCloseAwaitingSettlement())
        {
            if (!RunSettlementStep(Match,Error)) { return false; }
            continue;
        }
        const int64 Now=Mode->GetAuthorityNowMs();
        const int64 End=FMath::Min(Now+FMath::Min(MaxStepMs,ThroughMs-Now),LD_TRACED(LDStep_NextDue,NextDueMs(Now)));
        Mode->ReleaseConsumedMotion();
        const auto Admission=Mode->BeginAuthorityStep(*this,Match,End);
        if (Admission!=ELDCommitReject::None)
        {
            Error=FString::Printf(TEXT("Core refused native step: %u."),uint8(Admission));
            return false;
        }
        // Bodies integrate in entity order. Each key is read once, not looked up again on every comparison.
        TArray<TWeakObjectPtr<ALDCharacter>> CurrentBodies;
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(LDStep_SortBodies);
            TArray<TPair<FGuid,TWeakObjectPtr<ALDCharacter>>> Ordered;
            Ordered.Reserve(Bodies.Num());
            for (const auto& Pair : Bodies)
            {
                if (Pair.Key.IsValid()) { Ordered.Emplace(Pair.Value.Victim.EntityId,Pair.Key); }
            }
            Ordered.Sort([](const auto& Left,const auto& Right) { return Left.Key<Right.Key; });
            CurrentBodies.Reserve(Ordered.Num());
            for (const auto& Entry : Ordered) { CurrentBodies.Add(Entry.Value); }
        }
        ContactMotion.Reset();
        if (!Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Motion,[&](const FLDGameplayWritePermit& Permit)
        {
            for (const auto& Weak : CurrentBodies)
            {
                auto* Body=Weak.Get();
                FLDCombatBodyRef Ref;
                if (!Body || !Mode->ReadBodyRef(*Body,Ref))
                { Error=TEXT("The native step contains a registered body without its current binding."); return false; }
                // Camp members are steered by their camp owner; contenders by their order owner.
                if (Ref.Victim.Kind==ELDVictimKind::CampMember)
                {
                    if (!Mode->Camps) { Error=TEXT("A registered camp body has no camp owner."); return false; }
                    // #198: a frozen world body has no path (FreezeForRest stopped it), so it integrates zero input.
                    if (!Mode->Camps->AdvanceMemberMotion(Mode->GetAuthorityStep(),Ref,Permit,Error)) { return false; }
                    continue;
                }
                // #198: a personal training dummy has no controller; it stands still (only forced motion moves it).
                if (Mode->IsTrainingDummy(Ref.Victim.EntityId))
                {
                    if (!Mode->AdvanceTrainingDummyMotion(Ref,Permit,Error)) { return false; }
                    continue;
                }
                auto* PC=Cast<ALDPlayerController>(Body->GetController());
                if (!PC) { Error=TEXT("A registered contender body has no motion owner."); return false; }
                if (!PC->Navigation->AdvanceAuthorityMotion(Mode->GetAuthorityStep(),Ref,Permit,Error)) { return false; }
            }
            return true;
        },Error))
        { return false; }
        // A paused holder who becomes sole again during this motion and is due before the step's end wins at that
        // instant: the step's later boundary instant must not act, so only owed rewards settle before Capture.
        const bool bCaptureInside=Mode->CaptureCompletesInsideStep();
        const bool bWorldRan=bCaptureInside
            ? Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::DeathsAndRewards,[&](const FLDGameplayWritePermit& Permit)
            {
                if (!Mode->Growth) { Error=TEXT("Death rewards require the growth domain of this match."); return false; }
                return RunRewardRetries(Permit,Error);
            },Error)
            : (Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Session,[&](const FLDGameplayWritePermit& Permit)
            { return Mode->ExpireProtections(Permit,Error); },Error)
            && Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Actions,[&](const FLDGameplayWritePermit& Permit)
            { return RunActions(Permit,Error); },Error)
            && Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Effects,[&](const FLDGameplayWritePermit& Permit)
            {
                // #198 session D: throws land and the PK ring burns after this boundary's ordinary effects.
                return RunEffects(Permit,Error) && (!Mode->PKDirector || Mode->PKDirector->AdvanceEffects(Permit,Error))
                    && Mode->AdvanceShowdownEffects(Permit,Error);
            },Error)
            && Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::DeathsAndRewards,[&](const FLDGameplayWritePermit& Permit)
            {
                // #198 session D: the PK is judged after every death of the boundary (a decided PK ends here).
                return RunDeaths(Permit,Error) && (!Mode->PKDirector || Mode->PKDirector->JudgeAfterDeaths(Permit,Error));
            },Error)
            && Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::RespawnsAndWorld,[&](const FLDGameplayWritePermit& Permit)
            {
                if (!Mode->Camps) { Error=TEXT("Camp lifecycles require the camp owner of this match."); return false; }
                // #198: the timeline first (rest entry/exit moves every contender), then field respawns; the world only
                // advances while a field segment runs.
                if (!LD_TRACED(LDWorld_Timeline,Mode->AdvanceTimeline(Permit,Error)) || !LD_TRACED(LDWorld_Respawns,Mode->RespawnDueContenders(Permit,Error))
                    || !LD_TRACED(LDWorld_InitialCamps,Mode->Camps->SpawnPendingInitialCamps(Permit,Error))) { return false; }
                if (!Mode->IsFieldRunning() || Mode->IsShowdown()) { return true; }
                return Mode->Camps->AdvanceCamps(Permit,Error)
                    && Mode->AdvanceAirdrop(Permit,Error) && Mode->AdvanceFieldMarkers(Permit,Error)
                    && Mode->AdvanceLeaderNotice(Permit,Error) && Mode->AdvanceFieldPacing(Permit,Error);
            },Error)
            && Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Transactions,[&](const FLDGameplayWritePermit& Permit)
            {
                if (!Mode->Growth) { Error=TEXT("Transactions require the growth domain of this match."); return false; }
                // #198: commands committed at a build deadline settle before the lock of the same boundary.
                return (Mode->IsShowdown() || (Mode->ClaimAirdrop(Permit,Error) && Mode->ClaimRecoveryOrbs(Permit,Error) && Mode->PayTrendingPoints(Permit,Error)
                        && Mode->ClaimFieldPacing(Permit,Error)
                        && Mode->RetryFieldRewards(Permit,Error)))
                    && Mode->Growth->ProcessTransactions(Permit,Error)
                    && Mode->LockTimelineBuild(Permit,Error)
                    // #198 session D: a settled PK pays its frozen awards once.
                    && (!Mode->PKDirector || Mode->PKDirector->ProcessTransactions(Permit,Error));
            },Error));
        if (!bWorldRan
            || !Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Capture,[&](const FLDGameplayWritePermit& Permit)
            { return Mode->ExecuteCapturePhase(Permit,Error); },Error)
            // Close locks the unique result and shuts the write gate before any cleanup or publication.
            || !Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Close,[&](const FLDGameplayWritePermit& Permit)
            { return Mode->ExecuteClosePhase(Permit,Error); },Error)
            || !Mode->FinishAuthorityStep(*this,Error)
            // Publish has no permit: growth receipts and owner projections, then the capture outcome.
            || !Mode->Growth || !Mode->Growth->PublishGrowth(Mode->AuthorityBoundary,Error)
            || !Mode->ExecuteCapturePublish(Mode->AuthorityBoundary,Error))
        {
            if (Error.IsEmpty() && !Mode->Growth) { Error=TEXT("Growth publication requires the growth domain of this match."); }
            return false;
        }
    }
    if (!Mode->IsGameplayWriteOpen(Match) && !RetryRewards.IsEmpty())
    {
        // No result locks over an owed reward, so only a fault closes the gate here: the reward stays explicitly unpaid.
        for (const auto& Fact : RetryRewards)
        {
            UE_LOG(LogLDCombat,Error,TEXT("LD_REWARD_UNPAID_AT_FAULT victim=%s life=%u"),
                *Fact.Key.Victim.EntityId.ToString(),Fact.Key.Victim.Life);
        }
        RetryRewards.Reset();
    }
    // After Close the observation owner may already have ended with the match; owner views still settle.
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(LDVisionPublishAuthority);
        if (Mode->IsGameplayWriteOpen(Match) && !Mode->Vision->PublishAuthority(Match,Error)) { return false; }
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(LDCombatPublishOwnerViews);
    return PublishOwnerViews(Error);
}

bool ALDCombatDirector::RunSettlementStep(const FLDMatchContext& Match, FString& Error)
{
    auto* Mode=Core();
    const auto Admission=Mode->BeginSettlementStep(*this,Match);
    if (Admission!=ELDCommitReject::None)
    {
        Error=FString::Printf(TEXT("Core refused the settlement step: %u."),uint8(Admission));
        return false;
    }
    if (!Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::DeathsAndRewards,[&](const FLDGameplayWritePermit& Permit)
        {
            if (!Mode->Growth) { Error=TEXT("Death rewards require the growth domain of this match."); return false; }
            return RunRewardRetries(Permit,Error);
        },Error)
        || !Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Close,[&](const FLDGameplayWritePermit& Permit)
        { return Mode->ExecuteClosePhase(Permit,Error); },Error)
        || !Mode->FinishAuthorityStep(*this,Error)
        || !Mode->Growth || !Mode->Growth->PublishGrowth(Mode->AuthorityBoundary,Error)
        || !Mode->ExecuteCapturePublish(Mode->AuthorityBoundary,Error))
    {
        if (Error.IsEmpty() && !Mode->Growth) { Error=TEXT("Growth publication requires the growth domain of this match."); }
        return false;
    }
    return true;
}

bool ALDCombatDirector::RunActions(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Actions)!=ELDCommitReject::None)
    { Error=TEXT("Combat actions require the current Actions permit."); return false; }
    // Regeneration of the elapsed interval settles before this boundary's actions can start combat.
    if (!LD_TRACED(LDActions_Regenerate,Regenerate(Mode->GetAuthorityNowMs(),Error))) { return false; }
    // Lapsed temporary terms and HP-conditional bonuses are current before any action reads attributes.
    if (!LD_TRACED(LDActions_Temporary,RefreshAllTemporary(Mode->GetAuthorityNowMs(),Error))) { return false; }
    // A taunt whose taunter died, left or went out of the victim's observation ends before any intent is judged.
    if (!LD_TRACED(LDActions_Taunts,RefreshTaunts(Error))) { return false; }
    // K06: repeats due now release before this boundary's intents.
    if (!LD_TRACED(LDActions_AutoRecasts,RunAutoRecasts(Mode->GetAuthorityNowMs(),Error))) { return false; }
    // Camp members submit their basic attacks through the same entry first, so they are admitted below.
    if (!Mode->Camps) { Error=TEXT("Camp intents require the camp owner of this match."); return false; }
    // #198: camps stay idle (no aggro, chase or attack) while a rest freezes the world.
    if (!Mode->IsRestPhase(Mode->GetMatchContext()) && !LD_TRACED(LDActions_CampIntents,Mode->Camps->ProcessAuthorityIntents(Permit,Error))) { return false; }
    // Direct intents first: an ability may interrupt a basic windup before Navigation starts the next one.
    TArray<FGuid> Entities;
    Records.GetKeys(Entities);
    Entities.Sort();
    for (const FGuid& Entity : Entities)
    {
        auto* Record=FindRecord(Entity);
        if (!Record || Record->Pending.IsEmpty()) { continue; }
        TArray<FLDCombatActionRequest> Pending=MoveTemp(Record->Pending);
        Record->Pending.Reset();
        Pending.Sort([](const auto& A,const auto& B) { return A.DirectSequence<B.DirectSequence; });
        for (int32 Index=0; Index<Pending.Num(); ++Index)
        {
            const FLDCombatActionRequest& Request=Pending[Index];
            // Issue #329 §3.2: a Direct press during an active own ability windup (or behind an already buffered pre-input)
            // joins the unified 2-step 450ms FIFO pre-input queue in arrival order.
            if (ShouldBufferPreInput(Entity,Request.Entry))
            {
                FPendingCombatIntent Buffered;
                Buffered.Entry=Request.Entry;
                Buffered.CastMode=ELDCombatPreInputMode::Direct;
                Buffered.AimDirection=Request.Target.Direction;
                Buffered.GroundPointCm=Request.Target.Point;
                            Buffered.ExpiresAtMs=Mode->GetAuthorityNowMs()+LDCombatPreInput::HoldWindowMs;
                Buffered.Action=Request;
                BufferPreInputIntent(Entity,MoveTemp(Buffered));
                continue;
            }
            FLDCombatActionReceipt Receipt;
            if (!ProcessDirect(*Record,Request,Permit,Receipt,Error)) { return false; }
            Record=FindRecord(Entity);
            if (!Record) { break; }
            StoreReceipt(*Record,Request,Receipt);
            SendReceipt(*Record,Receipt);
        }
    }
    TArray<TPair<FGuid,TWeakObjectPtr<ALDPlayerController>>> Owners;
    for (const auto& Pair : Mode->Contenders)
    {
        if (Pair.Key.IsValid() && Mode->IsMemberPresent(Pair.Value.Session)) { Owners.Emplace(Pair.Value.Session,Pair.Key); }
    }
    Owners.Sort([](const auto& A,const auto& B) { return A.Key<B.Key; });
    for (const auto& Contender : Owners)
    {
        auto* PC=Contender.Value.Get();
        if (PC && !LD_TRACED(LDActions_Orders,PC->Navigation->ProcessAuthorityOrders(Permit,Error))) { return false; }
    }
    return LD_TRACED(LDActions_Release,ReleaseDue(Permit,Error));
}

bool ALDCombatDirector::RunEffects(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Effects)!=ELDCommitReject::None)
    { Error=TEXT("Combat effects require the current Effects permit."); return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    // Skill branch follow-ups, pulses and bursts due now (a shield's natural end heals before statuses expire).
    if (!LD_TRACED(LDEffects_Branches,AdvanceBranches(Now,Error))) { return false; }
    // Half-open statuses end before this boundary's hits; regeneration already settled in Actions.
    if (Proto.bAgency) { AdvanceAgency(Now); }
    { TRACE_CPUPROFILER_EVENT_SCOPE(LDEffects_ExpireStatuses); ExpireStatuses(Now); }
    // B12: voluntary dashes that completed by now (motion already ran to this boundary) arm their empowered packages.
    if (!LD_TRACED(LDEffects_Dashes,ResolveDashCompletions(Now,Error))) { return false; }
    // Zones, burns and element records due now join this boundary's hits and projectiles.
    ReactionFacts.Reset();
    BoundaryReactionDamage.Reset();
    ReactionAssists.Reset();
    if (!LD_TRACED(LDEffects_SkillContent,AdvanceSkillContent(Now,Error)) || !LD_TRACED(LDEffects_Mechanics,AdvanceMechanics(Now,Error))
        || !LD_TRACED(LDEffects_Elements,AdvanceElements(Now,Error)) || !LD_TRACED(LDEffects_Ground,AdvanceGround(Now,Error))
        || !LD_TRACED(LDEffects_Burns,AdvanceBurns(Now,Error))
        || !LD_TRACED(LDEffects_Projectiles,AdvanceProjectiles(Error)))
    { return false; }
    // #198 F: C02 blocks seen by this boundary's motion, M12 debts, C08 stores and M02 locked batches.
    if (!LD_TRACED(LDEffects_MonsterStrikes,AdvanceMonsterStrikes(Now,Error)) || !LD_TRACED(LDEffects_Cards,AdvanceCards(Now,Error))) { return false; }
    // Every hit of this boundary, then the C01 windows it closed.
    if (!LD_TRACED(LDEffects_Hits,ApplyPendingHits(Now,Error))) { return false; }
    // Spec §6.5: chains idle past CHAIN.EndSec end here.
    if (!LD_TRACED(LDEffects_Chains,AdvanceChains(Now,Error))) { return false; }
    // Slows and HP changes of this boundary reach the effective attributes before the next motion step.
    if (!LD_TRACED(LDEffects_Temporary,RefreshAllTemporary(Now,Error))) { return false; }
    if (PendingContacts.IsEmpty()) { return true; }
    if (!Mode->Camps) { Error=TEXT("Camp contacts require the camp owner of this match."); return false; }
    const TArray<FLDCombatHostileContact> Contacts=MoveTemp(PendingContacts);
    PendingContacts.Reset();
    for (const auto& Contact : Contacts)
    {
        if (!Mode->Camps->OnCombatHostileContact(Contact,Permit,Error)) { return false; }
    }
    return true;
}

bool ALDCombatDirector::RunDeaths(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::DeathsAndRewards)!=ELDCommitReject::None)
    { Error=TEXT("Deaths require the current DeathsAndRewards permit."); return false; }
    if (!Mode->Growth) { Error=TEXT("Death rewards require the growth domain of this match."); return false; }
    if (!RunRewardRetries(Permit,Error)) { return false; }
    if (PendingDeaths.IsEmpty()) { return true; }
    // Every fact of this Effects set was locked before any of them is consumed.
    TArray<FLDCommittedDeath> Deaths=MoveTemp(PendingDeaths);
    PendingDeaths.Reset();
    Deaths.Sort([](const FLDCommittedDeath& A,const FLDCommittedDeath& B)
    {
        return A.OccurredAtSeconds!=B.OccurredAtSeconds ? A.OccurredAtSeconds<B.OccurredAtSeconds
            : A.Key.Victim.EntityId<B.Key.Victim.EntityId;
    });
    for (const auto& Fact : Deaths)
    {
        if (!Mode->Vision->RecordObservedDeath(Permit,Fact.VictimBody,Fact.OccurredAtSeconds,Error)) { return false; }
        if (Fact.Key.Victim.Kind==ELDVictimKind::Contender)
        {
            if (!Mode->EndContenderLife(Fact,Permit,Error)) { return false; }
            continue;
        }
        // The camp owner records its member's death (and removes the actor) after the core ended the life.
        if (!Mode->Camps) { Error=TEXT("A camp death requires the camp owner of this match."); return false; }
        if (!Mode->EndCampMemberLife(Fact,Permit,Error) || !Mode->Camps->ConsumeMemberDeath(Fact,Permit,Error)) { return false; }
    }
    // Rewards follow every life end of the set; each RewardBasis was frozen at its own fatal hit.
    for (const auto& Fact : Deaths)
    {
        if (!CommitReward(Fact,false,Permit,Error)) { return false; }
    }
    return true;
}

bool ALDCombatDirector::RunRewardRetries(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::DeathsAndRewards)!=ELDCommitReject::None)
    { Error=TEXT("Reward retries require the current DeathsAndRewards permit."); return false; }
    // A reward refused as retryable at the previous boundary gets its single second attempt first.
    const TArray<FLDCommittedDeath> Retries=MoveTemp(RetryRewards);
    RetryRewards.Reset();
    for (const auto& Fact : Retries)
    {
        if (!CommitReward(Fact,true,Permit,Error)) { return false; }
    }
    return true;
}

bool ALDCombatDirector::CommitReward(const FLDCommittedDeath& Fact, bool bRetry, const FLDGameplayWritePermit& Permit, FString& Error)
{
    const FLDRewardReceipt Receipt=Core()->Growth->CommitDeathReward(Fact,Permit);
    if (Receipt.Outcome==ELDRewardOutcome::Granted || Receipt.Outcome==ELDRewardOutcome::NoReward) { return true; }
    if (Receipt.Outcome==ELDRewardOutcome::RetryableNotCommitted && !bRetry)
    {
        UE_LOG(LogLDCombat,Warning,TEXT("LD_REWARD_RETRY victim=%s life=%u reason=%u"),
            *Fact.Key.Victim.EntityId.ToString(),Fact.Key.Victim.Life,uint32(Receipt.Reason));
        RetryRewards.Add(Fact);
        return true;
    }
    Error=FString::Printf(TEXT("The reward of %s life %u stayed unpaid: outcome %u, reason %u."),
        *Fact.Key.Victim.EntityId.ToString(),Fact.Key.Victim.Life,uint32(Receipt.Outcome),uint32(Receipt.Reason));
    return false;
}

bool ALDCombatDirector::PublishOwnerViews(FString& Error)
{
    auto* Mode=Core();
    if (!Mode) { Error=TEXT("Owner views require the current core."); return false; }
    for (const auto& Pair : Mode->Contenders)
    {
        auto* PC=Pair.Key.Get();
        auto* Record=FindRecord(Pair.Value.Session);
        if (!PC || !Record || !Mode->IsMemberPresent(Pair.Value.Session)) { continue; }
        // A marked revision or another match or life publishes at once. Without either, only the time-driven terms
        // below can differ, so building the whole view every step to compare them is polled at OwnerViewPollMs.
        const int64 NowMs=Mode->GetAuthorityNowMs();
        const auto* LifeRecord=Mode->Lives.Find(Pair.Value.Session);
        const bool bSameLife=LifeRecord && LifeRecord->Life==PC->CombatView.Life
            && PC->CombatView.Generation==Mode->GetMatchContext().Generation;
        if (bSameLife && Record->ViewRevision==Record->PublishedViewRevision && NowMs<Record->NextOwnerViewPollMs) { continue; }
        Record->NextOwnerViewPollMs=NowMs+OwnerViewPollMs;
        FLDCombatCaller Caller{Mode->GetMatchContext(),ELDVictimKind::Contender,Pair.Value.Session,PC};
        FLDCombatOwnerView View;
        if (!BuildOwnerView(Caller,View)) { Error=TEXT("A present contender has no Combat owner projection."); return false; }
        // These owner-only facts can change without another resource mutation (a K16 idle factor that grows, a K18 window
        // that ends). Advance the revision so clients accept the publication instead of discarding it as an older view.
        bool bEntryTermsChanged=View.Entries.Num()!=PC->CombatView.Entries.Num();
        for (int32 Index=0; !bEntryTermsChanged && Index<View.Entries.Num(); ++Index)
        {
            const FLDCombatEntryView& New=View.Entries[Index];
            const FLDCombatEntryView& Old=PC->CombatView.Entries[Index];
            bEntryTermsChanged=New.DamageFactorBp!=Old.DamageFactorBp || New.NextCooldownMs!=Old.NextCooldownMs;
        }
        if (bEntryTermsChanged || View.HPRegenPerSec!=PC->CombatView.HPRegenPerSec
            || View.bPreviewLiving!=PC->CombatView.bPreviewLiving || View.bPreviewFinalField!=PC->CombatView.bPreviewFinalField
            || View.RespawnDurationMs!=PC->CombatView.RespawnDurationMs)
        {
            MarkViewChanged(Pair.Value.Session);
            View.Revision=Record->ViewRevision;
        }
        const bool Changed=View.Revision!=Record->PublishedViewRevision || View.Generation!=PC->CombatView.Generation
            || View.Life!=PC->CombatView.Life || View.bHasBody!=PC->CombatView.bHasBody;
        if (!Changed) { continue; }
        Record->PublishedViewRevision=View.Revision;
        PC->CombatView=MoveTemp(View);
        Mode->SyncOwner(PC);
        if (PC->IsLocalController()) { PC->OnRep_CombatView(); }
        PC->ForceNetUpdate();
    }
    return true;
}
