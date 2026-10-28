#include "Framework/LDCommandLiveness.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "World/LDWorldDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Engine/World.h"
double ALDObjectiveDirector::NextEvent(double After,double Through) const
{double Next=Through;for(const auto& P:Interactions){const auto& S=P.Value;const double End=S.Started+(S.Seconds>0?S.Seconds:LDObjective::Duration(S.Channel));if(S.Channel&&End>After+1.e-7&&End<Next){Next=End;}}return Next;}
void ALDObjectiveDirector::SampleWorldClaims(double Now)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=M->GetGameState<ALDGameState>();TArray<ALDPlayerController*> Order;for(const auto& P:Interactions){if(P.Key.IsValid()&&(P.Value.Channel==LDObjective::Claim||P.Value.Channel>=5)){Order.Add(P.Key.Get());}}
    Order.Sort([&](const auto& A,const auto& B){const auto& X=Interactions.FindChecked(&A);const auto& Y=Interactions.FindChecked(&B);const double EX=X.Started+X.Seconds,EY=Y.Started+Y.Seconds;return EX!=EY?EX<EY:X.Ordinal<Y.Ordinal;});G->bClaimActive=false;G->bContested=false;G->ClaimProgress=0;G->ClaimRoute=0;
    for(auto* PC:Order)
    {
        auto& S=Interactions.FindChecked(PC);auto* O=M->StateFor(PC);auto* B=Cast<ALDCharacter>(PC->GetPawn());const auto* C=M->Combat->StateFor(B);const double End=S.Started+S.Seconds,At=FMath::Min(Now,End);
        const bool Legal=O&&B&&C&&Eligible(PC,Now)&&S.Life==O->Life&&S.Body.Get()==B&&S.bHeld&&LDCommandLiveness::ForProfile(At,S.LastHold,M->IsUIControlProfile())&&S.LastHold<=Now+1.e-7&&Stationary(PC,S)&&Near(PC,S.Target)&&FVector::DistSquared2D(B->GetActorLocation(),S.Anchor)<=4&&!C->Action&&S.LastInterrupt<At-1.e-7;
        if(!Legal){ClearChannel(PC,S);S.bHeld=false;continue;}
        if(S.Channel>=5)
        {
            FString Why;if(C->LastDamage!=S.Damage||!M->WorldRuntime->CanActivate(PC,S.Route,End,Why)){ClearChannel(PC,S);S.bHeld=false;continue;}
            if(Now+1.e-7<End){UpdatePrivate(PC,S,Now);continue;}const bool Good=M->WorldRuntime->Activate(PC,S.Route,End,Why);ClearChannel(PC,S);S.bHeld=false;PC->CompleteContextRequest(S.Token,Good?TEXT("Activation committed once; observe the public warning or recovery cooldown"):Why);continue;
        }
        const uint8 Route=M->WorldRuntime->Authorization(PC,At,End);if(!Route||End>Deadline()+1.e-7){ClearChannel(PC,S);S.bHeld=false;continue;}
        S.Route=Route;G->bClaimActive=true;G->ClaimRoute|=Route;HeartSensorUntil=Now+2;G->ClaimProgress=FMath::Max(G->ClaimProgress,float(FMath::Clamp(Now-S.Started,0.,10.)));UpdatePrivate(PC,S,Now);
        if(Now+1.e-7>=End){UE_LOG(LogTemp,Display,TEXT("LD_B76_CLAIM_COMPLETE ordinal=%llu scheduled=%.6f sampled=%.6f route=%u"),S.Ordinal,End,Now,Route);Finish(PC,Route,End);return;}
    }
    G->ForceNetUpdate();
}
