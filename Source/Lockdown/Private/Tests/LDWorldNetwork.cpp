#include "Tests/LDWorldProbe.h"
#include "World/LDWorldDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Combat/LDCombatDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Vision/LDWorldViewRelay.h"
#include "Vision/LDVisionDirector.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
void ALDWorldProbe::Network()
{
#if !UE_BUILD_SHIPPING
    const double Now=FPlatformTime::Seconds();const bool Client=GetNetMode()==NM_Client;
    if(!Initialized){Initialized=true;NetStarted=NetAt=Now;FString PeerRole;FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),PeerRole);Route=TEXT("Network")+PeerRole;if(Client){A=CastChecked<ALDPlayerController>(GetOwner());A->ServerProbeHello(PeerRole==TEXT("A")?1:2);}else{Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();}}
    if(Now-NetStarted>100){Check(false,TEXT("Focused world network lane timed out"));Finish();return;}
    if(Client)
    {
        if(Now-NetPing>.15){NetPing=Now;A->ServerProbePing(Now);}if(NetPhase!=A->ProbePhase){NetPhase=A->ProbePhase;NetAt=Now;NetSent=false;NetChecked=false;Sequence=0;}const double Elapsed=Now-NetAt;const auto& Own=A->OwnerState;const bool IsA=Route==TEXT("NetworkA");const uint32 Id=uint32(50000+NetPhase*100);auto* G=GetWorld()->GetGameState<ALDGameState>();auto* V=A->LocalView();
        if(!NetSent&&Elapsed>.7&&NetPhase>0&&NetPhase<6)
        {
            if(NetPhase==1){A->ServerContextRequest(Own.Match,Own.Life,Id,10,{});NetSent=true;}
            if(NetPhase==2&&IsA){FGuid Key;if(V){for(const auto& E:V->LastView().Entities){if(E.Kind==ELDObservedKind::Cargo&&E.Presentation==1&&FVector::Dist2D(E.Position,A->GetPawn()->GetActorLocation())<80){Key=E.Id;break;}}}if(Key.IsValid()){A->ServerContextRequest(Own.Match,Own.Life,Id,14,Key);NetSent=true;}}
            if(NetPhase==3&&!IsA){A->ServerContextRequest(Own.Match,Own.Life,Id,11,{});NetSent=true;}
            if(NetPhase==5&&IsA){A->ServerContextRequest(Own.Match,Own.Life,Id,3,{});A->ServerContextRequest(Own.Match,Own.Life,Id,3,{});NetSent=true;}
        }
        if(NetSent&&Now-NetHold>.09&&((NetPhase==1&&IsA)||(NetPhase==2&&IsA)||(NetPhase==3&&!IsA)||(NetPhase==5&&IsA))){NetHold=Now;A->ServerContextHold(Own.Match,Own.Life,Id,++Sequence,false);}
        const double CheckAt=NetPhase==1?5.2:NetPhase==2?4.5:NetPhase==3?7.2:NetPhase==5?11.7:1.2;
        if(!NetChecked&&NetPhase>0&&Elapsed>CheckAt){NetChecked=true;Check(V&&!V->IsStale(),TEXT("Independent client receives current sanitized native view"));int32 Canonical=0;for(TActorIterator<ALDWorldPawn> It(GetWorld());It;++It){++Canonical;}for(TActorIterator<ALDItem> It(GetWorld());It;++It){++Canonical;}Check(Canonical==0,TEXT("Canonical encounter/item actors never replicate to rival client"));
            if(NetPhase==1){Check(G&&G->Breaches.Num()==1&&G->Breaches[0].Route==5,TEXT("Anonymous key warning reaches both independent clients"));Check(IsA?Own.BreachRoute==5:Own.BreachRoute==0,TEXT("Personal access arrives only on owning controller"));Check(V&&V->LastView().Entities.ContainsByPredicate([](const auto& E){return E.Kind==ELDObservedKind::WorldSite;}),TEXT("Native antechamber marker uses bounded world category"));}
            if(NetPhase==2){Check(Own.BreachRoute==0,TEXT("Revoked key lease stays revoked after held re-pickup"));if(IsA){Check(Own.Inventory.Items.Num()==3&&Own.KeysReadyAt>0,TEXT("Owner receives retained exact keys and persistent reuse endpoint"));}}
            if(NetPhase==3){Check(IsA?Own.BreachRoute==0:Own.BreachRoute==6,TEXT("Artifact opening grants one owner only"));Check(G&&G->Breaches.ContainsByPredicate([](const auto& N){return N.Route==6;}),TEXT("Artifact30s public warning is replicated without owner identity"));}
            if(NetPhase==4){if(!IsA){Check(!Own.bActive&&Own.BreachRoute==0&&Own.Inventory.Items.IsEmpty(),TEXT("Dead artifact owner receives cancellation and no consumed-item refund"));}}
            if(NetPhase==5){Check(G&&G->EndReason==1&&G->ResultSerial==1&&G->WinningRoute==4,TEXT("Public nonowner claim produces one common terminal result"));Check(Own.bWinner==IsA,TEXT("Independent owners agree on the sole winner"));Check(G&&G->DefensiveFlags==6,TEXT("Destroyed tower state and shortcut facts replicate"));}
        }
        if(NetPhase==6&&Elapsed>1.2){auto Samples=A->ProbeRtts;Samples.Sort();Check(Samples.Num()>15,TEXT("Independent transport echo samples measured"));if(Samples.Num()){Check(Samples[Samples.Num()/2]>=20&&Samples[Samples.Num()/2]<400,FString::Printf(TEXT("Measured echo median %.1fms"),Samples[Samples.Num()/2]));}Finish();}return;
    }
    auto* W=GetWorld();auto* M=Mode.Get();auto Enter=[&](int32 Phase){NetPhase=Phase;NetAt=Now;A->ClientProbePhase(Phase);B->ClientProbePhase(Phase);};
    auto Place=[&](ALDPlayerController* PC,FVector Point){M->Objectives->Interrupt(PC,W->GetTimeSeconds(),true);PC->Navigation->ResetLife();auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());Body->GetCharacterMovement()->SetMovementMode(MOVE_None);Body->GetCharacterMovement()->StopMovementImmediately();Body->SetActorLocation(Point+FVector(0,0,92));auto* S=M->Combat->StateFor(Body);S->ProtectionUntil=0;auto* O=M->StateFor(PC);O->ProtectedUntil=0;M->SyncOwner(PC);};
    auto Grant=[&](ALDPlayerController* PC,ELDItemKind Kind){auto* Item=W->SpawnActor<ALDItem>();Item->Kind=Kind;Check(M->Inventory->Adopt(PC,Item,TEXT("DiagnosticItemCreation")),TEXT("Explicit focused network physical fixture source"));return Item;};
    if(NetPhase==0){for(FConstPlayerControllerIterator It=W->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}if(!A||!B){return;}FString Why;M->SetLobbyChoice(A,4,true,Why);M->SetLobbyChoice(B,3,true,Why);if(!M->StartDevelopmentMatch(A,Why)&&!M->StartDevelopmentMatch(B,Why)){Check(false,Why);Finish();return;}Check(M->WorldRuntime&&M->WorldRuntime->Pawns.Num()==7,TEXT("Dedicated new region creates three camps, guardian and three towers"));Place(A,FVector(-50,-500,0));Place(B,FVector(50,-500,0));NetKey=Grant(A,ELDItemKind::AncientKeyA)->Identity;Grant(A,ELDItemKind::AncientKeyB);Grant(A,ELDItemKind::AncientKeyC);Enter(1);return;}
    auto* R=M->WorldRuntime.Get();auto* G=M->GetGameState<ALDGameState>();const double Elapsed=Now-NetAt;
    if(NetPhase==1&&Elapsed>6){Check(M->StateFor(A)->BreachRoute==5&&M->StateFor(B)->BreachRoute==0,TEXT("Remote keys activate while foreign owner without keys is rejected"));auto* Key=M->Inventory->Owned(A,NetKey);NetReady=Key?Key->ReadyAt:0;if(!Key){Check(false,TEXT("Key source present"));Finish();return;}Key->SetActorLocation(FVector(-50,-500,15));M->Inventory->Remove(A,Key,false);Check(!R->Leases.Contains(A),TEXT("Key removal revokes before next client command"));Enter(2);}
    else if(NetPhase==2&&Elapsed>5.3){const auto* Key=M->Inventory->Owned(A,NetKey);Check(Key&&Key->ReadyAt==NetReady&&!R->Leases.Contains(A),TEXT("Remote3s re-pickup retains ID/cooldown and cannot restore old lease"));Grant(B,ELDItemKind::Heartbreaker);Enter(3);}
    else if(NetPhase==3&&Elapsed>8){Check(M->StateFor(B)->BreachRoute==6&&M->StateFor(B)->Inventory.Items.IsEmpty(),TEXT("Remote6s artifact activation consumes exactly once"));auto* Body=CastChecked<ALDCharacter>(B->GetPawn());M->EndContenderLife(B,W->GetTimeSeconds());Body->Destroy();Check(!R->Leases.Contains(B),TEXT("Artifact death cancellation occurs on dedicated authority"));Enter(4);}
    else if(NetPhase==4&&Elapsed>2.5){Place(A,FVector(0,-40,0));G->CharterWindow=FGuid::NewGuid();G->CharterOpenAt=W->GetTimeSeconds();G->CharterCloseAt=W->GetTimeSeconds()+20;G->CharterPhase=2;for(const auto& V:R->Pawns){if(V.IsValid()&&V->Kind==2&&V->Index==0){M->Combat->StateFor(V.Get())->Hp=0;Check(R->Defeated(V.Get(),W->GetTimeSeconds()),TEXT("Labelled tower death removes one public sight source and opens shortcut"));V->Destroy();break;}}Enter(5);}
    else if(NetPhase==5&&Elapsed>13){Check(G->EndReason==1&&G->ResultSerial==1&&G->WinningRoute==4&&M->StateFor(A)->bWinner&&!M->StateFor(A)->CharterPurchase.IsValid(),TEXT("Nonregistered remote claimant wins public opening with one terminal receipt"));FString Why;Check(M->Economy->Audit(Why)&&!M->Economy->AuditFailures,TEXT("Focused remote source and terminal ledger reconcile: ")+Why);Enter(6);}
    else if(NetPhase==6&&Elapsed>3){Finish();}
#endif
}
