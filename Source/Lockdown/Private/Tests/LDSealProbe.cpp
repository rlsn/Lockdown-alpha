#include "Tests/LDDevelopmentProbe.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if !UE_BUILD_SHIPPING
void ALDDevelopmentProbe::SealCheck()
{
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* G=W->GetGameState<ALDGameState>();auto* C=M->Combat.Get();auto* D=M->Objectives.Get();auto* PC=CastChecked<ALDPlayerController>(W->GetFirstPlayerController());auto* O=M->StateFor(PC);auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());FString Why;uint32 Token=20000,Seq=0;
    int32 Items=0;for(TActorIterator<ALDSeal> It(W);It;++It){++Items;}
    Check(!Items&&!O->SealId.IsValid()&&!O->FabricationReceipt.IsValid()&&!D->SeedFixtureSeal(),TEXT("Normal region has no seeded Seal and rejects the legacy seed fixture"));
    M->SetLobbyChoice(PC,1,true,Why);M->StartDevelopmentMatch(PC,Why);PC->SetMenuOpen(false);C->SetActorTickEnabled(false);
    for(TActorIterator<ALDResident> It(W);It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();It->SetActorLocation(FVector(-5000,3000,92));if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
    auto Place=[&](uint8 Site){Body->GetCharacterMovement()->StopMovementImmediately();Body->SetActorLocation(LDRegion::Site(W,Site)+FVector(0,50,92));};
    auto Advance=[&](double Dt){W->TimeSeconds+=Dt;C->Sample(W->GetTimeSeconds());};
    auto Press=[&](uint8 Site=LDObjective::Fabricator){PC->ServerContextRelease(O->Match,O->Life,Token);Advance(.4);Seq=0;PC->ServerContextRequest(O->Match,O->Life,++Token,Site,FGuid());C->Sample(W->GetTimeSeconds());};
    auto Hold=[&](double Duration){for(double Left=Duration;Left>1.e-7;){const double Dt=FMath::Min(.1,Left);W->TimeSeconds+=Dt;PC->ServerContextHold(O->Match,O->Life,Token,++Seq,false);C->Sample(W->GetTimeSeconds());Left-=Dt;}};
    auto Untouched=[&](){return O->Gold==100&&O->Commissions==4&&!O->SealId.IsValid()&&!O->FabricationReceipt.IsValid();};
    O->Gold=100;O->Commissions=4;O->PaidJobs=4;O->Witness=FGuid::NewGuid();O->ProtectedUntil=0;C->StateFor(Body)->ProtectionUntil=0;Place(1);const auto Witness=O->Witness;
    for(uint8 Case=0;Case<5;++Case)
    {
        if(Case==0){O->Witness.Invalidate();}if(Case==1){O->Commissions=3;}if(Case==2){O->Gold=99;}if(Case==3){O->SealId=FGuid::NewGuid();}if(Case==4){O->FabricationReceipt=FGuid::NewGuid();}
        Press();Hold(4.01);Check(!O->ChannelKind&&O->Gold==(Case==2?99:100)&&O->Commissions==(Case==1?3:4),FString::Printf(TEXT("Fabrication prerequisite %u rejects without spending"),Case));
        O->Witness=Witness;O->Commissions=4;O->Gold=100;O->SealId.Invalidate();O->FabricationReceipt.Invalidate();
    }
    O->Build.Slots.Init(ELDSkill::None,4);M->SyncOwner(PC);Press();Hold(3.9);Check(Untouched()&&O->ChannelKind==4,TEXT("Fabrication needs no equipped skill and cannot charge before four seconds"));
    PC->ServerContextRelease(O->Match,O->Life,Token);Hold(.2);Check(Untouched()&&!O->ChannelKind,TEXT("Release and late refresh cannot resurrect or charge a fabrication"));
    Press();Hold(1);Advance(.251);Check(Untouched()&&!O->ChannelKind,TEXT("Expired hold cancels fabrication at zero cost"));
    Press();Hold(1);W->TimeSeconds+=.1;PC->ServerContextHold(O->Match,O->Life,Token,++Seq,true);C->Sample(W->GetTimeSeconds());Check(Untouched()&&!O->ChannelKind,TEXT("Movement intent cancels fabrication at zero cost"));
    Press();Hold(1);Body->SetActorLocation(Body->GetActorLocation()+FVector(3,0,0));Hold(.1);Check(Untouched()&&!O->ChannelKind,TEXT("Actual displacement cancels fabrication at zero cost"));
    Place(1);Press();Hold(1);Place(3);Hold(.1);Check(Untouched()&&!O->ChannelKind,TEXT("Leaving the Guild cancels fabrication at zero cost"));
    Place(1);Press();Hold(3);O->Gold=99;Hold(1.01);Check(O->Gold==99&&O->Commissions==4&&!O->SealId.IsValid()&&!O->FabricationReceipt.IsValid(),TEXT("Completion revalidates actual available gold before spending"));O->Gold=100;
    Press();Hold(1);auto* Wall=W->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Box->SetBoxExtent(FVector(30,5,100));Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);Box->RegisterComponent();Wall->SetActorLocation(LDRegion::Site(W,1)+FVector(0,25,120));Hold(.1);Check(Untouched()&&!O->ChannelKind,TEXT("Lost clear Guild segment cancels fabrication at zero cost"));Wall->Destroy();
    Press();Hold(1);C->StateFor(Body)->ProtectionUntil=W->GetTimeSeconds()+2;Hold(.1);Check(Untouched()&&!O->ChannelKind,TEXT("Lost life eligibility cancels fabrication without a charge"));Advance(2.1);
    Press();Hold(1);const double Due=D->StateFor(PC)->Started+4;auto* Attacker=W->SpawnActor<ALDCharacter>(Body->GetActorLocation()-FVector(80,0,0),FRotator::ZeroRotator);Attacker->GetCharacterMovement()->DisableMovement();C->RequestBody(Attacker,0,FVector::ForwardVector,Why);C->StateFor(Attacker)->ImpactAt=Due;W->TimeSeconds=Due;PC->ServerContextHold(O->Match,O->Life,Token,++Seq,false);C->Sample(W->GetTimeSeconds());
    Check(Untouched()&&!O->ChannelKind&&O->Hp<O->MaxHp,TEXT("Real damage on the exact fabrication completion batch wins before spending"));Attacker->Destroy();
    Press();Hold(1);M->EndContenderLife(PC,W->GetTimeSeconds());Body->Destroy();Check(O->Gold==85&&O->Commissions==4&&!O->FabricationReceipt.IsValid()&&!O->SealId.IsValid(),TEXT("Death during fabrication charges only the normal fifteen-percent death loss"));W->TimeSeconds=O->ReturnAt+.01;M->ReturnDueContenders(W->GetTimeSeconds());Body=CastChecked<ALDCharacter>(PC->GetPawn());C->StateFor(Body)->ProtectionUntil=0;O->ProtectedUntil=0;O->Gold=100;Place(1);
    Press();D->FailNextFabricationForProbe();Hold(4.01);Check(Untouched()&&!O->ChannelKind&&PC->IntentFeedback.Contains(TEXT("failed")),TEXT("Injected item creation failure spends nothing and permits retry"));
    Press();Hold(4.01);const FGuid ItemId=O->SealId,Receipt=O->FabricationReceipt;ALDSeal* Item=nullptr;for(TActorIterator<ALDSeal> It(W);It;++It){if(It->Identity==ItemId){Item=*It;}}
    Check(Item&&ItemId.IsValid()&&Receipt.IsValid()&&Item->State==1&&Item->Carrier==PC&&!O->Gold&&!O->Commissions&&O->PaidJobs==4&&O->Witness==Witness,TEXT("Atomic fabrication owns one unique item, charges exactly and retains Witness and paid jobs"));
    PC->ServerContextRequest(O->Match,O->Life,Token,LDObjective::Fabricator,FGuid());Advance(.1);Check(O->SealId==ItemId&&O->FabricationReceipt==Receipt&&!O->Gold&&!O->Commissions,TEXT("Cached fabrication request replay cannot create or charge twice"));
    O->Gold=100;O->Commissions=4;Press();Hold(4.01);Check(O->SealId==ItemId&&O->Gold==100&&O->Commissions==4,TEXT("New request cannot bypass carry capacity or once-per-match craft"));
    D->EndLife(PC,Body->GetActorLocation(),W->GetTimeSeconds());Check(Item&&Item->State==0&&!Item->Carrier.IsValid()&&!O->SealId.IsValid()&&O->FabricationReceipt==Receipt,TEXT("Life cleanup drops the independent immutable item and retains the craft receipt"));
    Press();Hold(4.01);Check(!O->SealId.IsValid()&&O->Gold==100&&O->Commissions==4&&O->FabricationReceipt==Receipt,TEXT("Dropping a Seal does not reopen fabrication"));
    Press(1);Check(O->ContractStage==1&&O->PaidJobs==4,TEXT("Explicit courier work remains available after fabrication and spending credits"));
    PC->ServerContextRelease(O->Match,O->Life,Token);const uint32 OldLife=O->Life;M->EndContenderLife(PC,W->GetTimeSeconds());Check(O->FabricationReceipt==Receipt&&O->Witness==Witness&&O->PaidJobs==4,TEXT("Actual contender death retains craft receipt, Witness and paid budget"));
    W->TimeSeconds=O->ReturnAt+.01;M->ReturnDueContenders(W->GetTimeSeconds());Body=CastChecked<ALDCharacter>(PC->GetPawn());C->StateFor(Body)->ProtectionUntil=0;O->ProtectedUntil=0;Place(1);PC->ServerContextRequest(O->Match,OldLife,++Token,9,FGuid());Advance(.1);Check(!O->ChannelKind&&O->FabricationReceipt==Receipt,TEXT("Stale prior-life fabrication cannot change retained ownership history"));
    O->FabricationReceipt.Invalidate();O->Gold=100;O->Commissions=4;G->MatchDeadline=float(W->GetTimeSeconds()+3.8);Press();Hold(4.01);Check(M->IsFinished()&&G->EndReason==2&&!O->SealId.IsValid()&&Untouched(),TEXT("Fabrication scheduled beyond the fixed deadline cannot spend or win"));
}

void ALDDevelopmentProbe::SealServerTick()
{
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* G=W->GetGameState<ALDGameState>();auto* C=M->Combat.Get();const auto* D=LDRegion::Definition(W);
    if(!Phase){for(FConstPlayerControllerIterator It=W->GetPlayerControllerIterator();It;++It){if(auto* PC=Cast<ALDPlayerController>(It->Get());PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}if(A.IsValid()&&B.IsValid()){Check(G->LobbyCount==2,TEXT("Earned acquisition network has two actual independent remote owners"));Enter(1);}return;}
    if(Phase==25){if(FPlatformTime::Seconds()-PhaseAt>3){Finish();}return;}
    if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("Acquisition client disconnected early"));Finish();return;}
    auto* AS=M->StateFor(A.Get());auto* BS=M->StateFor(B.Get());auto* AB=Cast<ALDCharacter>(A->GetPawn());auto* BB=Cast<ALDCharacter>(B->GetPawn());const double Elapsed=FPlatformTime::Seconds()-PhaseAt;
    auto Place=[&](uint8 Site){AB->SetActorLocation(D->SitePosition(Site)+FVector(-40,50,92));BB->SetActorLocation(D->SitePosition(Site)+FVector(40,50,92));};
    if(Phase==1&&AS->bReady&&BS->bReady){Enter(2);}
    else if(Phase==2&&G->bMatchStarted&&Elapsed>1)
    {
        for(auto* PC:{A.Get(),B.Get()}){auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(D->Guild+FVector(PC==A.Get()?-40:40,210,92));Body->GetCharacterMovement()->StopMovementImmediately();C->StateFor(Body)->ProtectionUntil=0;M->StateFor(PC)->ProtectedUntil=0;M->SyncOwner(PC);}OriginalDeadline=M->Objectives->Deadline();Enter(3);
    }
    else if(Phase==3&&Elapsed>2.5){Check(AS->Witness.IsValid()&&BS->Witness.IsValid()&&!AS->Gold&&!BS->Gold,TEXT("Both remote starting Survey holds earn real Witness receipts without resources"));Place(1);Enter(5);}
    else if(Phase>=5&&Phase<=16)
    {
        const int32 Job=(Phase-5)/3,Step=(Phase-5)%3;
        if(Step==0&&Elapsed>1.5){Check(AS->ContractStage==1&&BS->ContractStage==1,FString::Printf(TEXT("Both remote owners accept earned courier %d"),Job+1));AB->SetActorLocation(D->Node(AS->ContractDestination)->Position+FVector(-40,50,92));BB->SetActorLocation(D->Node(BS->ContractDestination)->Position+FVector(40,50,92));Enter(Phase+1);}
        else if(Step==1&&Elapsed>4.5){Check(AS->ContractStage==2&&BS->ContractStage==2,TEXT("Remote package collection requires its real held timer"));Place(1);Enter(Phase+1);}
        else if(Step==2&&Elapsed>4.5)
        {
            Check(AS->Gold==25*(Job+1)&&BS->Gold==25*(Job+1)&&AS->PaidJobs==Job+1&&BS->PaidJobs==Job+1&&AS->Commissions==Job+1&&BS->Commissions==Job+1,TEXT("Remote deliveries produce exact independent earned balances"));
            if(Job==3){Enter(17);}else{W->TimeSeconds=FMath::Max(AS->WorkReadyAt,BS->WorkReadyAt)+.1;C->Sample(W->GetTimeSeconds());Enter(Phase+1);}
        }
    }
    else if(Phase==17&&Elapsed>5.5)
    {
        ProbeSeal=AS->SealId;ProbeCraft=AS->FabricationReceipt;Check(ProbeSeal.IsValid()&&BS->SealId.IsValid()&&ProbeSeal!=BS->SealId&&ProbeCraft.IsValid()&&BS->FabricationReceipt.IsValid()&&!AS->Gold&&!BS->Gold&&!AS->Commissions&&!BS->Commissions&&AS->PaidJobs==4&&BS->PaidJobs==4,TEXT("Concurrent remote fabrication atomically consumes each owner's four earned jobs once"));Enter(18);
    }
    else if(Phase==18&&Elapsed>1.5)
    {
        Check(AS->SealId==ProbeSeal&&AS->FabricationReceipt==ProbeCraft&&!AS->Gold&&!BS->Gold,TEXT("Delayed duplicate and new fabrication requests never duplicate items or charges"));M->EndContenderLife(A.Get(),W->GetTimeSeconds());AB->Destroy();for(TActorIterator<ALDSeal> It(W);It;++It){if(It->Identity==ProbeSeal){BB->SetActorLocation(It->GetActorLocation()+FVector(0,30,76));}}Enter(19);
    }
    else if(Phase==19&&Elapsed>2)
    {
        Check(!AS->SealId.IsValid()&&AS->FabricationReceipt==ProbeCraft&&BS->SealId.IsValid(),TEXT("Real remote crafter death drops its earned item; the other full carry slot cannot take it"));
        // Separate the second death site so recovery cannot select its owner's other item.
        BB->SetActorLocation(D->Guild+FVector(500,-500,92));M->EndContenderLife(B.Get(),W->GetTimeSeconds());BB->Destroy();W->TimeSeconds=FMath::Max(AS->ReturnAt,BS->ReturnAt)+.1;M->ReturnDueContenders(W->GetTimeSeconds());AB=CastChecked<ALDCharacter>(A->GetPawn());BB=CastChecked<ALDCharacter>(B->GetPawn());
        for(auto* PC:{A.Get(),B.Get()}){M->StateFor(PC)->ProtectedUntil=0;C->StateFor(CastChecked<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;}
        BS->Witness.Invalidate();AB->SetActorLocation(D->Guild+FVector(500,500,92));for(TActorIterator<ALDSeal> It(W);It;++It){if(It->Identity==ProbeSeal){BB->SetActorLocation(It->GetActorLocation()+FVector(0,30,76));}}
        M->SyncOwner(B.Get());Enter(20);
    }
    else if(Phase==20&&Elapsed>2)
    {
        Check(ProbeSeal.IsValid()&&BS->SealId==ProbeSeal&&!BS->Witness.IsValid()&&AS->FabricationReceipt==ProbeCraft,TEXT("Other remote contender recovers the crafter's earned immutable Seal without Witness"));BB->SetActorLocation(D->Heart+FVector(0,50,92));AB->SetActorLocation(D->Heart+FVector(230,0,92));Enter(21);
    }
    else if(Phase==21&&Elapsed>2.5)
    {Check(G->bContested&&!M->IsFinished()&&G->DefensiveFlags==15,TEXT("Living unprotected contender within 240 cm resets recovered-Seal claim"));AB->SetActorLocation(D->Guild+FVector(500,500,92));Enter(22);}
    else if(Phase==22&&Elapsed>11)
    {
        Check(G->EndReason==1&&G->WinningRoute==LDObjective::SealRoute&&BS->bWinner&&!AS->bWinner&&G->ResultSerial==1&&M->Objectives->Deadline()==OriginalDeadline,TEXT("Recovered earned Seal wins once after ten uncontested seconds under the original deadline"));
        int32 Consumed=0,Dropped=0;for(TActorIterator<ALDSeal> It(W);It;++It){Consumed+=It->State==2;Dropped+=It->State==0;}Check(Consumed==1&&Dropped==1&&!BS->SealId.IsValid()&&!AS->SealId.IsValid()&&AS->FabricationReceipt==ProbeCraft,TEXT("Winning item alone is consumed; the other earned item and craft histories remain independent"));Enter(24);
    }
    else if(Phase==24&&Elapsed>2){Enter(25);}
}

void ALDDevelopmentProbe::SealClientTick()
{
    auto* PC=Cast<ALDPlayerController>(GetOwner());if(!PC||!PC->OwnerState.Match){return;}const auto& O=PC->OwnerState;const bool IsA=Role==TEXT("A");const double Wall=FPlatformTime::Seconds();
    if(Wall-LastPing>.2){LastPing=Wall;PC->ServerProbePing(Wall);}if(PC->ProbePhase!=Phase){if(Phase==21&&!IsA){PC->ServerContextRelease(O.Match,O.Life,32100);}Phase=PC->ProbePhase;PhaseAt=Wall;bChoiceSent=false;bChecked=false;}const double Elapsed=Wall-PhaseAt;const uint32 Token=30000+Phase*100;
    if(Phase==1){if(!bChoiceSent){PC->ChooseLobbyClass(1);bChoiceSent=true;}if(!bReadySent&&O.Build.ClassId==1&&Elapsed>.4){PC->ToggleLobbyReady();bReadySent=true;}}
    if(Phase==2&&!bStartSent&&O.bLobbyHost){PC->StartLobby();bStartSent=true;}
    const bool Job=Phase>=5&&Phase<=16;
    if((Phase==3||Job||Phase==17||Phase==21||Phase==22)&&!bChoiceSent&&Elapsed>.4)
    {bChoiceSent=true;if(Phase==3){PC->ServerSkillAction(O.Match,O.Life,Token,O.Build.Revision,1,FVector::ForwardVector);}else if((Phase!=21&&Phase!=22)||!IsA){uint8 Site=Phase==17?9:(Phase==21||Phase==22)?3:1;if(Job&&(Phase-5)%3==1){const auto* D=LDRegion::Definition(GetWorld());const auto* N=D?D->Node(O.ContractDestination):nullptr;Site=N?N->Site:0;}PC->ServerContextRequest(O.Match,O.Life,Token,Site,FGuid());}}
    const bool Held=Phase==3||(Job&&(Phase-5)%3!=0)||Phase==17||((Phase==21||Phase==22)&&!IsA);
    if(Held&&Elapsed>.4&&Wall-SkillRefreshAt>.1){SkillRefreshAt=Wall;if(Phase==3){PC->ServerSkillHold(O.Match,O.Life,Token,uint32(Elapsed*1000),false);}else{PC->ServerContextHold(O.Match,O.Life,Token,uint32(Elapsed*1000),false);}}
    if(Phase==18&&!bChoiceSent&&Elapsed>.4){bChoiceSent=true;PC->ServerContextRequest(O.Match,O.Life,31700,9,FGuid());PC->ServerContextRequest(O.Match,O.Life,Token,9,FGuid());}
    if((Phase==19||Phase==20)&&!IsA&&!bChoiceSent&&Elapsed>.6)
    {
        if(const auto* Relay=PC->LocalView()){for(const auto& E:Relay->LastView().Entities){if(E.Kind==ELDObservedKind::Seal&&FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(),E.Position)<=80*80){bChoiceSent=true;PC->ServerContextRequest(O.Match,O.Life,Token,4,E.Id);break;}}}
    }
    if(Phase==19&&!IsA&&bChoiceSent&&!bChecked&&Elapsed>1.5){bChecked=true;Check(O.SealId.IsValid()&&PC->IntentFeedback.Contains(TEXT("full")),TEXT("Remote recovery request is explicitly rejected while its carry slot is full"));}
    if(Phase==24&&!bChecked&&Elapsed>.5)
    {
        bChecked=true;Check(O.PaidJobs==4&&!O.Commissions&&!O.Gold&&O.FabricationReceipt.IsValid(),TEXT("Owner retains its exact independent paid/craft accounting"));Check(O.bWinner==!IsA&&!O.SealId.IsValid(),TEXT("Owner terminal receipt identifies only the actual recovered-item winner"));Check(O.Witness.IsValid()==IsA,TEXT("Recovery winner requires no Witness; the crafter retains its accomplishment"));int32 CanonicalItems=0;for(TActorIterator<ALDSeal> It(GetWorld());It;++It){++CanonicalItems;}Check(!CanonicalItems,TEXT("Remote clients receive filtered observations, never canonical Seal actors"));const auto* G=GetWorld()->GetGameState<ALDGameState>();Check(G->DefensiveFlags==15&&G->ResultSerial==1&&G->ResultReceipt.IsValid(),TEXT("Public final receipt preserves protected central defences and one result"));
    }
    if(Phase==25&&Elapsed>1){Finish();}
}
#endif
