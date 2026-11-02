#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"

ALDGameMode* ALDCombatDirector::Core() const
{
    auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<ALDGameMode>():nullptr;
    return Mode && Mode->Combat==this ? Mode : nullptr;
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
    for (const auto& Pair : Bodies)
    {
        const auto& Body=Pair.Value;
        if (Body.bPendingDeath) { continue; }
        if (Body.Action.IsSet()) { Consider(Body.Action->ReleaseAtMs); }
        for (const auto& Root : Body.Roots) { Consider(Root.ExpiresAtMs); }
        if (Body.Shield.IsSet()) { Consider(Body.Shield->ExpiresAtMs); }
        Consider(Body.DashUntilMs);
        Consider(Body.MonsterNextAttackAtMs);
        if (Body.AttributeProjection.OutOfCombatHPRegen.IsSet() && Body.LastCombatAtMs<=MAX_int64-Body.AttributeProjection.OutOfCombatHPRegen->DelayMs)
        { Consider(Body.LastCombatAtMs+Body.AttributeProjection.OutOfCombatHPRegen->DelayMs); }
    }
    // The basic cadence and every ability cooldown end on exact boundaries, not one frame late.
    for (const auto& Pair : Records)
    {
        Consider(Pair.Value.NextAttackAtMs);
        for (const auto& Track : Pair.Value.Tracks) { Consider(Track.ReadyAtMs); }
    }
    if (const auto* Mode=Core())
    {
        Consider(Mode->NextLifeDueMs());
        // Opening time and the Solo completion must each be an exact step boundary.
        Consider(Mode->NextCaptureDueMs());
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
    while (Mode->IsGameplayWriteOpen(Match) && Mode->GetAuthorityNowMs()<ThroughMs)
    {
        const int64 Now=Mode->GetAuthorityNowMs();
        const int64 End=FMath::Min(Now+FMath::Min(MaxStepMs,ThroughMs-Now),NextDueMs(Now));
        Mode->ReleaseConsumedMotion();
        const auto Admission=Mode->BeginAuthorityStep(*this,Match,End);
        if (Admission!=ELDCommitReject::None)
        {
            Error=FString::Printf(TEXT("Core refused native step: %u."),uint8(Admission));
            return false;
        }
        TArray<TWeakObjectPtr<ALDCharacter>> CurrentBodies;
        Bodies.GetKeys(CurrentBodies);
        CurrentBodies.Sort([&](const auto& Left,const auto& Right)
        { return Bodies.FindChecked(Left).Victim.EntityId<Bodies.FindChecked(Right).Victim.EntityId; });
        if (!Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Motion,[&](const FLDGameplayWritePermit& Permit)
        {
            for (const auto& Weak : CurrentBodies)
            {
                auto* Body=Weak.Get();
                auto* PC=Body?Cast<ALDPlayerController>(Body->GetController()):nullptr;
                FLDCombatBodyRef Ref;
                if (!Body || !PC || !Mode->ReadBodyRef(*Body,Ref))
                { Error=TEXT("The native step contains a registered body without its motion owner."); return false; }
                if (!PC->Navigation->AdvanceAuthorityMotion(Mode->GetAuthorityStep(),Ref,Permit,Error)) { return false; }
            }
            return true;
        },Error)
            || !Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Session,[&](const FLDGameplayWritePermit& Permit)
            { return Mode->ExpireProtections(Permit,Error); },Error)
            || !Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Actions,[&](const FLDGameplayWritePermit& Permit)
            { return RunActions(Permit,Error); },Error)
            || !Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Effects,[&](const FLDGameplayWritePermit& Permit)
            { return RunEffects(Permit,Error); },Error)
            || !Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::DeathsAndRewards,[&](const FLDGameplayWritePermit& Permit)
            { return RunDeaths(Permit,Error); },Error)
            || !Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::RespawnsAndWorld,[&](const FLDGameplayWritePermit& Permit)
            { return Mode->RespawnDueContenders(Permit,Error); },Error)
            || !Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Capture,[&](const FLDGameplayWritePermit& Permit)
            { return Mode->ExecuteCapturePhase(Permit,Error); },Error)
            // Close locks the unique result and shuts the write gate before any cleanup or publication.
            || !Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Close,[&](const FLDGameplayWritePermit& Permit)
            { return Mode->ExecuteClosePhase(Permit,Error); },Error)
            || !Mode->FinishAuthorityStep(*this,Error)
            || !Mode->ExecuteCapturePublish(Mode->AuthorityBoundary,Error)) { return false; }
    }
    // After Close the observation owner may already have ended with the match; owner views still settle.
    if (Mode->IsGameplayWriteOpen(Match) && !Mode->Vision->PublishAuthority(Match,Error)) { return false; }
    return PublishOwnerViews(Error);
}

bool ALDCombatDirector::RunActions(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Actions)!=ELDCommitReject::None)
    { Error=TEXT("Combat actions require the current Actions permit."); return false; }
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
        for (const auto& Request : Pending)
        {
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
        if (PC && !PC->Navigation->ProcessAuthorityOrders(Permit,Error)) { return false; }
    }
    return ReleaseDue(Permit,Error);
}

bool ALDCombatDirector::RunEffects(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Effects)!=ELDCommitReject::None)
    { Error=TEXT("Combat effects require the current Effects permit."); return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    // Half-open statuses end before this boundary's hits; regeneration covers the interval before them.
    ExpireStatuses(Now);
    if (!Regenerate(Now,Error) || !AdvanceProjectiles(Error)) { return false; }
    for (int32 Index=0; Index<PendingHits.Num(); ++Index)
    {
        const FLDCombatHit Hit=PendingHits[Index];
        if (!ApplyHit(Hit,Error)) { return false; }
    }
    PendingHits.Reset();
    return true;
}

bool ALDCombatDirector::RunDeaths(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::DeathsAndRewards)!=ELDCommitReject::None)
    { Error=TEXT("Deaths require the current DeathsAndRewards permit."); return false; }
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
        const bool Ended=Fact.Key.Victim.Kind==ELDVictimKind::Contender
            ? Mode->EndContenderLife(Fact,Permit,Error) : Mode->EndCampMemberLife(Fact,Permit,Error);
        if (!Ended) { return false; }
    }
    return true;
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
        FLDCombatCaller Caller{Mode->GetMatchContext(),ELDVictimKind::Contender,Pair.Value.Session,PC};
        FLDCombatOwnerView View;
        if (!BuildOwnerView(Caller,View)) { Error=TEXT("A present contender has no Combat owner projection."); return false; }
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
