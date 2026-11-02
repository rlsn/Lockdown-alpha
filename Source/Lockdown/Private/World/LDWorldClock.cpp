#include "Combat/LDCombatDirector.h"
#include "World/LDWorldDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Economy/LDProductionDirector.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Character/LDCharacterMovementComponent.h"

bool ALDCombatDirector::AdvanceAuthority(const FLDMatchContext& Match, int64 ThroughMs, FString& Error)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (!Mode || Mode->Combat!=this || bAdvancing || !Mode->IsGameplayWriteOpen(Match)
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
    while (Mode->GetAuthorityNowMs()<ThroughMs)
    {
        const int64 End=Mode->GetAuthorityNowMs()+FMath::Min(MaxStepMs,ThroughMs-Mode->GetAuthorityNowMs());
        const auto Admission=Mode->BeginAuthorityStep(*this,Match,End);
        if (Admission!=ELDCommitReject::None)
        {
            Error=FString::Printf(TEXT("Core refused native step: %u."),uint8(Admission));
            return false;
        }
        TArray<TWeakObjectPtr<ALDCharacter>> CurrentBodies;
        Bodies.GetKeys(CurrentBodies);
        CurrentBodies.Sort([&](const auto& Left, const auto& Right)
        { return Bodies.FindChecked(Left).Victim.EntityId<Bodies.FindChecked(Right).Victim.EntityId; });
        if (!Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Motion,[&](const FLDGameplayWritePermit& Permit)
        {
            for (const auto& Weak : CurrentBodies)
            {
                auto* Body=Weak.Get();
                auto* PC=Body?Cast<ALDPlayerController>(Body->GetController()):nullptr;
                FLDCombatBodyRef Ref;
                if (!Body || !PC || !Mode->ReadBodyRef(*Body,Ref))
                { Error=TEXT("The native step contains an invalid registered contender body."); return false; }
                if (!PC->Navigation->AdvanceAuthorityMotion(Mode->GetAuthorityStep(),Ref,Permit,Error)) { return false; }
            }
            return true;
        },Error)) { return false; }
        if (!Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Actions,[&](const FLDGameplayWritePermit& Permit)
        {
            for (const auto& Weak : CurrentBodies)
            {
                auto* Body=Weak.Get();
                auto* PC=Body?Cast<ALDPlayerController>(Body->GetController()):nullptr;
                if (!PC) { Error=TEXT("The owned order consumer disappeared during a native step."); return false; }
                if (!PC->Navigation->ProcessAuthorityOrders(Permit,Error)) { return false; }
            }
            return true;
        },Error) || !Mode->FinishAuthorityStep(*this,Error)) { return false; }
    }
    Error.Reset();
    return true;
}

void ALDCombatDirector::FlushPastCommand()
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(M&&M->WorldRuntime&&!bSampling&&!bAdvancing){AdvanceAuthority(GetWorld()->GetTimeSeconds()-1.e-6);}}
void ALDCombatDirector::AdvanceAuthority(double Through)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->WorldRuntime||bSampling||bAdvancing){return;}TGuardValue<bool> Guard(bAdvancing,true);const double Actual=GetWorld()->GetTimeSeconds();
    if(LastAuthorityAt<0){LastAuthorityAt=M->StartedAt();}if(Through<=LastAuthorityAt+1.e-7){return;}
    // Native movement has already executed. An ambiguous changed pose cancels an old
    // stationary hold before draining; no historical client trajectory is fabricated.
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());const auto* S=M->Objectives->StateFor(PC);if(S&&S->Channel&&PC&&PC->GetPawn()&&FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(),S->Anchor)>4){M->Objectives->Interrupt(PC,LastAuthorityAt,true);}}
    while(LastAuthorityAt+1.e-7<Through&&!M->IsFinished())
    {
        double Next=FMath::Min(Through,LastAuthorityAt+.05);auto Due=[&](double T){if(T>LastAuthorityAt+1.e-7&&T<Next){Next=T;}};auto* G=M->GetGameState<ALDGameState>();Due(G->MatchDeadline);Due(G->EconomyNextTick);Due(G->CharterOpenAt);Due(G->CharterCloseAt);Next=M->WorldRuntime->NextEvent(LastAuthorityAt,Next);Next=M->Objectives->NextEvent(LastAuthorityAt,Next);if(M->Production){Next=M->Production->NextEvent(LastAuthorityAt,Next);}
        if(M->IsV08Profile()){Due((FMath::FloorToDouble(LastAuthorityAt*10.+1.e-6)+1)/10.);for(const auto& P:RiverCells){for(uint8 I=0;I<7;++I){Due(P.Value.Get(LDEnvironment::EState(I)).Until/1000.);}}}
        for(const auto& P:Bodies){const auto& S=P.Value;if(S.Action==1||S.Action==2||S.Action==6||S.Action==7){Due(S.ImpactAt);}if(S.Action==9||S.Action==10){Due(S.CoreEndsAt/1000.);}for(const auto& Status:S.Tactical.Statuses){Due(Status.Until/1000.);}Due(S.Tactical.BarrierUntil/1000.);Due(S.SalveNext/1000.);}
        for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){Due(It->ExpiresAt/1000.);if(It->Kind==ELDTacticalField::Ember||It->Kind==ELDTacticalField::Conduit){Due(It->NextTick/1000.);}}
        for(TActorIterator<ALDArcProjectile> It(GetWorld());It;++It){auto* Target=It->ActorVictim.IsValid()?It->ActorVictim.Get():static_cast<AActor*>(It->Victim.Get());if(!Target){continue;}const FVector Delta=It->Endpoint-It->GetActorLocation(),Dir=Delta.GetSafeNormal(),V=Target->GetActorLocation()-It->GetActorLocation();const double Along=FVector::DotProduct(V,Dir),Perp=V.SizeSquared()-Along*Along;if(Perp<=1600&&Along>=0){const double Hit=FMath::Max(0.,Along-FMath::Sqrt(FMath::Max(0.,1600-Perp)));if(Hit<=Delta.Size()){Due(FMath::CeilToDouble(It->AdvancedAt+1000*Hit/1200.)/1000.);}}}
        GetWorld()->TimeSeconds=Next;M->bDrainingEvents=Next+1.e-7<Actual;Sample(Next);LastAuthorityAt=Next;
    }
    GetWorld()->TimeSeconds=Actual;M->bDrainingEvents=false;
}
