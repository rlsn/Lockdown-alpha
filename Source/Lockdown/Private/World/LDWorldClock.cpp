#include "Combat/LDCombatDirector.h"
#include "World/LDWorldDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Economy/LDProductionDirector.h"
#include "EngineUtils.h"
#include "Engine/World.h"
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
        if(M->IsV08Profile()){Due((FMath::FloorToDouble(LastAuthorityAt*10.+1.e-6)+1)/10.);for(const auto& P:RiverCells){Due(P.Value.Wet.Until/1000.);Due(P.Value.Frozen.Until/1000.);}}
        for(const auto& P:Bodies){const auto& S=P.Value;if(S.Action==1||S.Action==2||S.Action==6||S.Action==7){Due(S.ImpactAt);}if(S.Action==9||S.Action==10){Due(S.CoreEndsAt/1000.);}for(const auto& Status:S.Tactical.Statuses){Due(Status.Until/1000.);}Due(S.Tactical.BarrierUntil/1000.);Due(S.SalveNext/1000.);}
        for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){Due(It->ExpiresAt/1000.);if(It->Kind==ELDTacticalField::Ember){Due(It->NextTick/1000.);}}
        for(TActorIterator<ALDArcProjectile> It(GetWorld());It;++It){auto* Target=It->ActorVictim.IsValid()?It->ActorVictim.Get():static_cast<AActor*>(It->Victim.Get());if(!Target){continue;}const FVector Delta=It->Endpoint-It->GetActorLocation(),Dir=Delta.GetSafeNormal(),V=Target->GetActorLocation()-It->GetActorLocation();const double Along=FVector::DotProduct(V,Dir),Perp=V.SizeSquared()-Along*Along;if(Perp<=1600&&Along>=0){const double Hit=FMath::Max(0.,Along-FMath::Sqrt(FMath::Max(0.,1600-Perp)));if(Hit<=Delta.Size()){Due(FMath::CeilToDouble(It->AdvancedAt+1000*Hit/1200.)/1000.);}}}
        GetWorld()->TimeSeconds=Next;M->bDrainingEvents=Next+1.e-7<Actual;Sample(Next);LastAuthorityAt=Next;
    }
    GetWorld()->TimeSeconds=Actual;M->bDrainingEvents=false;
}
