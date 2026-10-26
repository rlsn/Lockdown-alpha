#include "Tests/LDB84Probe.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDRiverRules.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"

void ALDB84Probe::Boundaries()
{
    Catalog();
    Fields();
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();auto* O=M->Objectives.Get();
    C->SetActorTickEnabled(false);uint32 Token=950000,SequenceId=0;
    auto Body=[](ALDPlayerController* P){return CastChecked<ALDCharacter>(P->GetPawn());};
    auto Advance=[&](double Through){W->TimeSeconds=Through;C->AdvanceAuthority(Through);};
    auto Place=[&](ALDPlayerController* P,FVector Point){P->Navigation->Stop();auto* D=Body(P);D->GetCharacterMovement()->StopMovementImmediately();D->SetActorLocation(Point+FVector(0,0,92));D->GetCharacterMovement()->SetMovementMode(MOVE_None);auto* S=C->StateFor(D);S->ProtectionUntil=0;M->StateFor(P)->ProtectedUntil=0;S->Hp=LDDevelopment::MaxHP(M->StateFor(P)->Build);S->Tactical.Statuses.Reset();S->CoreRecoveryUntil=0;};
    Advance(W->GetTimeSeconds()+30);Place(A.Get(),FVector(-1900,-700,0));Place(B.Get(),FVector(-1550,-700,0));
    auto* SA=C->StateFor(Body(A.Get()));auto* SB=C->StateFor(Body(B.Get()));int64 Now=LDTactical::Milliseconds(W->GetTimeSeconds());
    auto HasNativePath=[&](FVector From,FVector To){const auto* P=UNavigationSystemV1::FindPathToLocationSynchronously(W,From+FVector(0,0,92),To+FVector(0,0,92));return P&&P->IsValid()&&!P->IsPartial();};
    Check(HasNativePath(FVector(-2100,-1400,0),FVector(-1100,-1400,0))&&HasNativePath(FVector(-800,-800,0),FVector(0,-800,0))&&HasNativePath(FVector(-800,-400,0),FVector(0,-400,0)),TEXT("City dry detour, Heart dry detour and Heart ford have complete native paths"));
    Place(A.Get(),FVector(-1700,-700,0));C->RefreshRiverContact(Body(A.Get()),Now,true);LDTactical::Dispel(SA->Tactical,false);C->RefreshRiverContact(Body(A.Get()),Now);
    Check(!SA->Tactical.Has(LDTactical::EStatus::Wet,Now),TEXT("Basic cleanse clears current Wet until next environment boundary"));
    Advance(W->GetTimeSeconds()+.101);Now=LDTactical::Milliseconds(W->GetTimeSeconds());Check(SA->Tactical.Has(LDTactical::EStatus::Wet,Now),TEXT("Continuous native river contact reasserts Wet at100ms boundary"));
    Body(A.Get())->SetActorLocation(FVector(-1900,-700,92));C->RefreshRiverContact(Body(A.Get()),Now);
    Check(SA->Tactical.Has(LDTactical::EStatus::Wet,Now+2999)&&!SA->Tactical.Has(LDTactical::EStatus::Wet,Now+3000),TEXT("Ordinary river exit leaves exactly3000ms residual"));
    Place(B.Get(),FVector(-400,-400,0));C->RefreshRiverContact(Body(B.Get()),Now,true);
    Check(SB->Tactical.Has(LDTactical::EStatus::Wet,Now+1499)&&!SB->Tactical.Has(LDTactical::EStatus::Wet,Now+1500),TEXT("Bound Waders apply1500ms residual at authored Heart water"));
    Place(B.Get(),FVector(-1550,-700,0));C->RefreshRiverContact(Body(B.Get()),Now,true);
    Check(C->StageRiver(Body(A.Get()),FVector(-1600,-800,0),true,true,0,180,Now,5000),TEXT("Native frozen river command stages within budget"));C->CommitRiver(Now);
    const auto SourceLife=SA->Identity;const auto* Cell=C->RiverCells.Find(FIntPoint(22,16));bool Snapshot=false;for(const auto& P:C->RiverCells){Snapshot|=P.Value.Frozen.Life==SourceLife&&P.Value.Frozen.Until==Now+6000;}
    Check(Snapshot,TEXT("Field Practice snapshots6s Frozen with original source life"));
    auto* OldA=Body(A.Get());M->EndContenderLife(A.Get(),W->GetTimeSeconds());Check(!C->RiverCells.IsEmpty()&&C->RiverCells.CreateConstIterator().Value().Cue->Active(Now),TEXT("Persistent surface survives caster death independently of wards/traps"));OldA->Destroy();Advance(M->StateFor(A.Get())->ReturnAt+.01);
    Place(A.Get(),FVector(-1900,-700,0));Place(B.Get(),FVector(-1550,-700,0));SA=C->StateFor(Body(A.Get()));SB=C->StateFor(Body(B.Get()));Now=LDTactical::Milliseconds(W->GetTimeSeconds());
    C->RefreshRiverContact(Body(B.Get()),Now,true);auto* OB=M->StateFor(B.Get());OB->WetFrostReadyAt=0;
    FLDCombatHit H{Body(A.Get()),Body(B.Get()),0,LDTactical::EDamage::Magical,false,0,++Token};H.DirectFrost=true;H.TargetLife=SB->Identity;
    C->ElementContact(H,Body(B.Get()),Now);const int64 Saved=OB->WetFrostReadyAt;const auto OldLife=SB->Identity;
    Check(Saved==Now+6000&&SB->Tactical.Has(LDTactical::EStatus::Stun,Now),TEXT("Native reaction owns fixed600ms Stun and6000ms contender endpoint"));
    C->RefreshRiverContact(Body(B.Get()),Now+100,true);C->ElementContact(H,Body(B.Get()),Now+100);Check(OB->WetFrostReadyAt==Saved&&SB->Tactical.Has(LDTactical::EStatus::Wet,Now+100),TEXT("Duplicate cast/victim-life cannot consume refreshed Wet or extend lock"));
    LDTactical::Dispel(SB->Tactical,true);Check(OB->WetFrostReadyAt==Saved,TEXT("Native strong dispel preserves contender reaction readiness"));
    auto* OldB=Body(B.Get());M->EndContenderLife(B.Get(),W->GetTimeSeconds());OldB->Destroy();Advance(OB->ReturnAt+.01);Check(OB->WetFrostReadyAt==Saved&&C->StateFor(Body(B.Get()))->Identity!=OldLife,TEXT("Death and native return preserve exact endpoint across a fresh body life"));
    Place(A.Get(),FVector(-1900,-700,0));Place(B.Get(),FVector(-1550,-700,0));Now=LDTactical::Milliseconds(W->GetTimeSeconds());C->RefreshRiverContact(Body(B.Get()),Now,true);C->ElementContact(H,Body(B.Get()),Now);Check(OB->WetFrostReadyAt==Saved,TEXT("Old-life direct packet cannot retarget returned victim"));
    // Fill the real bounded service with four distinct16-cell commands, then exercise release rejection.
    C->RiverCells.Reset();Place(A.Get(),FVector(-4500,-3100,0));Now=LDTactical::Milliseconds(W->GetTimeSeconds());bool Filled=true;
    for(int32 N=0;N<4;++N){Filled&=C->StageRiver(Body(A.Get()),FVector(-4100+1200*N,-3100,0),true,false,0,450,Now,10000);C->CommitRiver(Now);}
    Check(Filled&&C->RiverCells.Num()==64,TEXT("Four bounded native commands occupy exactly64 distinct owner cells"));
    Place(A.Get(),FVector(-1900,-700,0));auto* OA=M->StateFor(A.Get());SA=C->StateFor(Body(A.Get()));const double Mana=SA->Tactical.MP,Cooldown=OA->Build.Cooldowns[40];FLDTacticalRequest R;R.Schema=8;R.WireVersion=4;R.Token=++Token;R.BuildRevision=OA->Build.Revision;R.Slot=1;R.Point=FVector(-1300,-700,0);R.Direction=FVector::ForwardVector;FString Why;
    Check(C->RequestContent(Body(A.Get()),R,nullptr,nullptr,Why),TEXT("Capacity test accepts stationary Water Jet windup"));Advance(W->GetTimeSeconds()+.401);
    Check(SA->Tactical.MP>=Mana&&OA->Build.Cooldowns[40]==Cooldown&&C->RiverCells.Num()==64,TEXT("Full owner capacity rejects entire release before MP/cooldown; no enemy deletion"));Advance(W->GetTimeSeconds()+10.1);Check(C->RiverCells.IsEmpty(),TEXT("Expiry always retires bounded allocations"));
    // Actual item packet block, then two exact claim boundaries through the normal due clock.
    auto Prism=[&](double ReleaseAt)
    {Advance(ReleaseAt-.25);auto* Own=M->StateFor(A.Get());const auto* I=Own->Inventory.Items.FindByPredicate([](const auto& V){return V.Kind==ELDItemKind::FrostPrism;});if(!I){Check(false,TEXT("Retained physical Prism"));return;}
        FLDTacticalRequest P;P.Schema=8;P.WireVersion=4;P.ActionKind=1;P.SourceKind=2;P.Slot=0;P.Item=I->Id;P.ItemRevision=I->Revision;P.InventoryRevision=Own->Inventory.Revision;P.BuildRevision=Own->Build.Revision;P.Token=++Token;
        Check(C->RequestContent(Body(A.Get()),P,Body(B.Get()),nullptr,Why),TEXT("Native bound Prism packet accepted: ")+Why);};
    Advance(W->GetTimeSeconds()+30);Place(A.Get(),FVector(-200,0,0));Place(B.Get(),FVector(0,0,0));SB=C->StateFor(Body(B.Get()));OB->WetFrostReadyAt=0;Now=LDTactical::Milliseconds(W->GetTimeSeconds());LDTactical::Apply(SB->Tactical,LDTactical::EStatus::Wet,Now+10000,{});SB->Tactical.BlockUntil=Now+2000;SB->Tactical.GuardUntil=Now+2000;
    const double Release=W->GetTimeSeconds()+.3;Prism(Release);Advance(Release+.001);Check(OB->WetFrostReadyAt==0&&SB->Tactical.Has(LDTactical::EStatus::Wet,LDTactical::Milliseconds(Release))&&SB->Tactical.GuardUntil==Now+2000,TEXT("Real spell-blocked Frost packet spends no Wet/guard/reaction lock"));
    auto Claim=[&](bool Guarded)
    {
        Advance(W->GetTimeSeconds()+25);Place(A.Get(),FVector(-200,0,0));Place(B.Get(),FVector(0,0,0));SB=C->StateFor(Body(B.Get()));OB->WetFrostReadyAt=0;const double Start=W->GetTimeSeconds();auto* G=M->GetGameState<ALDGameState>();G->CharterWindow=FGuid::NewGuid();G->CharterOpenAt=Start;G->CharterCloseAt=Start+100;G->CharterPhase=2;
        const uint32 ClaimToken=++Token;O->Queue(B.Get(),OB->Match,OB->Life,ClaimToken,3,{});O->Sample(Start);const auto* Held=O->StateFor(B.Get());if(!Held||Held->Channel!=3){Check(false,TEXT("Native stationary Heart claim accepted"));return;}const double End=Held->Started+Held->Seconds;
        auto HoldThrough=[&](double Through){while(W->GetTimeSeconds()+1.e-7<Through&&!M->IsFinished()){const double T=FMath::Min(Through,W->GetTimeSeconds()+.1);W->TimeSeconds=T;O->RefreshHold(B.Get(),OB->Life,ClaimToken,++SequenceId,false,T);C->AdvanceAuthority(T);}};
        HoldThrough(End-.3);Now=LDTactical::Milliseconds(W->GetTimeSeconds());LDTactical::Apply(SB->Tactical,LDTactical::EStatus::Wet,LDTactical::Milliseconds(End)+3000,{});SB->Tactical.GuardUntil=Guarded?LDTactical::Milliseconds(End)+1000:0;Prism(End);HoldThrough(End-.000001);Advance(End+.001);
        Check(OB->WetFrostReadyAt==LDTactical::Milliseconds(End)+6000,TEXT("Due Prism reaction commits shared readiness at exact claim boundary"));
        Check(Guarded?G->ResultSerial==1&&OB->bWinner:G->ResultSerial==0&&!O->HasChannel(B.Get()),Guarded?TEXT("Guard-suppressed Stun and nonlethal damage allow stationary claim completion"):TEXT("Due applied Frost Stun cancels before same-boundary Heart completion"));
    };
    Claim(false);Claim(true);
}
