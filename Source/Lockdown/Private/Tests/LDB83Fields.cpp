#include "Tests/LDB83Probe.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Character/LDCharacter.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "World/LDWorldDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/LDNavigationComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

void ALDB83Probe::Fields()
{
    using namespace LDEnvironment;auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();C->SetActorTickEnabled(false);uint32 Token=700000;
    auto Body=[](ALDPlayerController* PC){return CastChecked<ALDCharacter>(PC->GetPawn());};
    auto Advance=[&](double Seconds){W->TimeSeconds+=Seconds;C->AdvanceAuthority(W->GetTimeSeconds());};
    auto Now=[&](){return LDTactical::Milliseconds(W->GetTimeSeconds());};
    auto Place=[&](ALDPlayerController* PC,FVector Point){PC->Navigation->Stop();auto* D=Body(PC);D->SetActorLocation(Point+FVector(0,0,92));D->GetCharacterMovement()->StopMovementImmediately();D->GetCharacterMovement()->SetMovementMode(MOVE_None);auto* S=C->StateFor(D);S->ProtectionUntil=0;S->Tactical.Statuses.Reset();S->CoreRecoveryUntil=0;S->Hp=LDDevelopment::MaxHP(M->StateFor(PC)->Build);M->StateFor(PC)->ProtectedUntil=0;};
    auto Emit=[&](EInput Input,FVector Point,int32 Duration=5000){const bool Good=C->StageEnvironment(Body(A.Get()),Point,Input,true,0,180,Now(),Duration);C->CommitRiver(Now());Check(Good,TEXT("Native bounded service accepts isolated reaction command"));};
    auto CellAt=[&](FVector Point)->LDEnvironment::FCell{return C->RiverCells.FindRef(LDRiver::Cell(Point));};
    auto Cast=[&](ELDSkill Skill,FVector Point,AActor* Target=nullptr)
    {auto* O=M->StateFor(A.Get());O->Build.Ranks[uint8(Skill)]=1;O->Build.Slots[2]=Skill;O->Build.Actions[2].Skill=Skill;O->Build.Cooldowns[uint8(Skill)]=0;++O->Build.Revision;auto* S=C->StateFor(Body(A.Get()));S->Tactical.MP=LDTactical::MaxMP(O->Build);S->CoreRecoveryUntil=0;S->Tactical.Statuses.Reset();FLDTacticalRequest R;R.Schema=8;R.WireVersion=4;R.Token=++Token;R.BuildRevision=O->Build.Revision;R.Slot=2;R.Point=Point;R.Direction=(Point-Body(A.Get())->GetActorLocation()).GetSafeNormal2D();FString Why;const double MPBefore=S->Tactical.MP;Check(C->RequestContent(Body(A.Get()),R,Target,nullptr,Why),FString(TEXT("Native fixture knowledge/equip then normal costed cast: "))+LDDevelopment::SkillName(Skill)+TEXT(" / ")+Why);const auto* D=M->TacticalCatalog->Find(Skill);Advance((D?D->Windup:1000)/1000.+.001);Check(S->Tactical.MP<MPBefore-10,TEXT("Normal release spends authored MP despite diagnostic knowledge/MP setup"));};
    Advance(15);Place(A.Get(),FVector(-1900,-700,0));Place(B.Get(),FVector(-1500,-700,0));
    Emit(EInput::Frost,FVector(-1600,-800,0));Check(CellAt(FVector(-1500,-700,0)).Frozen.Until>Now(),TEXT("water_frost uses permanent river substrate"));
    Emit(EInput::Fire,FVector(-1600,-800,0));auto Cell=CellAt(FVector(-1500,-700,0));Check(!Cell.Frozen.Until&&Cell.Wet.Until>Now()&&Cell.Obscured.Until>Now()&&!Cell.Burning.Until,TEXT("ice_fire applies exactly Wet plus Steam, no recursive Burning"));
    Emit(EInput::Wind,FVector(-1600,-800,0));Check(!CellAt(FVector(-1500,-700,0)).Obscured.Until,TEXT("wind_obscured deletes selected state"));
    Emit(EInput::Lightning,FVector(-1600,-800,0),3000);Check(CellAt(FVector(-1500,-700,0)).Electrified.Until>Now(),TEXT("water_lightning creates bounded18DPS state"));
    auto* SB=C->StateFor(Body(B.Get()));const double HP=SB->Hp;Advance(.51);Check(SB->Hp<HP&&!SB->Tactical.Has(LDTactical::EStatus::Stun,Now()),TEXT("Actual environment tick damages Wet contact without hard control"));
    Advance(12);Place(A.Get(),FVector(-2700,-700,0));Place(B.Get(),FVector(-2300,-700,0));
    Cast(ELDSkill::Entangle,FVector(-2300,-700,0));Check(CellAt(FVector(-2300,-700,0)).Overgrown.Until>Now()&&SB->Tactical.SurfaceSlow==15&&SB->Tactical.Has(LDTactical::EStatus::Root,Now()),TEXT("Native Entangle root is separate from growth_soil15percent field slow"));
    Emit(EInput::Fire,FVector(-2300,-700,0));Check(!CellAt(FVector(-2300,-700,0)).Overgrown.Until&&CellAt(FVector(-2300,-700,0)).Burning.Until>Now(),TEXT("plant_fire consumes temporary growth; no tree collision edits"));
    Emit(EInput::Water,FVector(-2300,-700,0));Check(!CellAt(FVector(-2300,-700,0)).Burning.Until&&CellAt(FVector(-2300,-700,0)).Wet.Until>Now(),TEXT("water_extinguish keeps consumed fuel absent"));
    Advance(12);Place(A.Get(),FVector(-1900,-700,0));Place(B.Get(),FVector(-1500,-700,0));
    Cast(ELDSkill::ConductiveField,FVector(-1500,-700,0));Check(CellAt(FVector(-1500,-700,0)).Electrified.Until>Now(),TEXT("Native costed Conductive Field uses Wet/water requirement"));
    Emit(EInput::Frost,FVector(-1600,-800,0));const double BeforeShatter=SB->Hp;const int64 ReadyBefore=M->StateFor(B.Get())->WetFrostReadyAt;
    Cast(ELDSkill::ShatterBurst,FVector(-1500,-700,0));Check(SB->Hp<BeforeShatter&&!SB->Tactical.Has(LDTactical::EStatus::Stun,Now())&&M->StateFor(B.Get())->WetFrostReadyAt==ReadyBefore&&!CellAt(FVector(-1500,-700,0)).Frozen.Until,TEXT("Native ice_impact is one physical burst per victim-life, consumes ice, never Wet-stuns"));
    Advance(12);Place(A.Get(),FVector(-1500,-1000,0));Place(B.Get(),FVector(-1100,-1000,0));
    Emit(EInput::Oil,FVector(-1100,-1000,0),6000);Emit(EInput::Fire,FVector(-1100,-1000,0));Cell=CellAt(FVector(-1100,-1000,0));Check(Cell.Burning.Until>Now()&&!Cell.Oil.Until,TEXT("oil_fire consumes finite overlay and keeps new creator attribution"));
    // Unit Burning is dispellable; intact surface still contributes only its greater20DPS component.
    FLDCombatHit Direct{Body(A.Get()),Body(B.Get()),0,LDTactical::EDamage::Magical,false,0,++Token,C->StateFor(Body(A.Get()))->Identity};Direct.DirectFire=true;Direct.TargetLife=SB->Identity;C->ElementContact(Direct,Body(B.Get()),Now());TArray<FLDCombatHit> Damage;C->EnvironmentDamageAt=-1;C->EnvironmentDamage(Now(),Damage);double Raw=0;for(const auto& H:Damage){if(H.Target==Body(B.Get())){Raw+=H.Damage;}}Check(Raw==10,TEXT("Burning surface20DPS replaces unit12DPS rather than stacking32"));
    LDTactical::Dispel(SB->Tactical,false);C->EnvironmentDamageAt=-1;Damage.Reset();C->EnvironmentDamage(Now(),Damage);Raw=0;for(const auto& H:Damage){if(H.Target==Body(B.Get())){Raw+=H.Damage;}}Check(Raw==10,TEXT("Unit cleanse cannot erase an intact surface"));
    const auto Creator=M->ContenderId(A.Get());const auto SourceLife=C->StateFor(Body(A.Get()))->Identity;
    auto* Old=Body(A.Get());M->EndContenderLife(A.Get(),W->GetTimeSeconds());Old->Destroy();Check(CellAt(FVector(-1100,-1000,0)).Burning.Life==SourceLife,TEXT("Persistent damaging field retains creator's original source life after death"));
    // The inherited native world kill receipt accepts original field credit while creator is dead.
    ALDWorldPawn* Camp=nullptr;for(const auto& P:M->WorldRuntime->Pawns){if(P.IsValid()&&P->Kind==0&&!P->Paid){Camp=P.Get();break;}}
    if(Camp){Camp->Home=FVector(-1100,-1000,92);Camp->SetActorLocation(Camp->Home);Camp->AttackDamage=0;Camp->Target=Body(B.Get());auto* T=C->StateFor(Camp);T->Hp=1;const int64 XP=M->StateFor(A.Get())->Xp;Advance(.51);Check(Camp->Paid&&Camp->LastOwner==Creator&&M->StateFor(A.Get())->Xp==XP+90,TEXT("Original field creator receives one finite native camp reward after death"));const auto Receipts=M->Economy->Accounting().ReceiptCount();Check(!M->WorldRuntime->Defeated(Camp,W->GetTimeSeconds())&&M->Economy->Accounting().ReceiptCount()==Receipts,TEXT("Repeated lifecycle cleanup cannot pay another reward"));}
    else{Check(false,TEXT("Native finite camp exists for source-life attribution test"));}
    Advance(M->StateFor(A.Get())->ReturnAt-W->GetTimeSeconds()+.01);Place(A.Get(),FVector(-1500,-1000,0));Place(B.Get(),FVector(-1100,-1000,0));
    // Real projectile block and reflection. Sponsor stays original; creator changes to reflector.
    SB=C->StateFor(Body(B.Get()));SB->Tactical.BlockUntil=Now()+3000;Cast(ELDSkill::Firebolt,Body(B.Get())->GetActorLocation(),Body(B.Get()));Check(C->HeldEnvironment()>0,TEXT("Paid projectile holds worst-case cell/transition credit before impact"));Advance(.5);Check(C->EnvironmentLeases.IsEmpty()&&CellAt(FVector(-1100,-1000,0)).Burning.Until==0,TEXT("Spell-blocked projectile releases reservation exactly once without field"));
    SB->Tactical.ReflectUntil=Now()+3000;const auto Sponsor=M->ContenderId(A.Get()),Reflector=M->ContenderId(B.Get());Cast(ELDSkill::Firebolt,Body(B.Get())->GetActorLocation(),Body(B.Get()));Advance(1.0);bool Reflected=false;for(const auto& P:C->RiverCells){for(uint8 I=0;I<7;++I){const auto& R=P.Value.Get(EState(I));Reflected|=R.Owner==Reflector&&R.BudgetOwner==Sponsor&&R.Until>Now();}}Check(C->EnvironmentLeases.IsEmpty()&&Reflected,TEXT("Reflected paid packet changes creator but keeps original sponsor with no second capacity spend"));
    Advance(12);Place(A.Get(),FVector(-1900,-700,0));Place(B.Get(),FVector(-1500,-700,0));
    C->RefreshRiverContact(Body(B.Get()),Now(),true);M->Vision->RefreshEnvironment();const double BeforeChain=SB->Hp;
    Cast(ELDSkill::ChainLightning,Body(B.Get())->GetActorLocation(),Body(B.Get()));Check(SB->Hp<BeforeChain&&SB->Tactical.Has(LDTactical::EStatus::Wet,Now()),TEXT("Native costed Chain Lightning damages its authorized Wet target without consuming Wet"));
    Emit(EInput::Smoke,FVector(-1600,-800,0));Cast(ELDSkill::Gust,FVector(-1500,-700,0));Check(!CellAt(FVector(-1500,-700,0)).Obscured.Until,TEXT("Native costed Gust clears selected cone cells"));
    Advance(12);Place(A.Get(),FVector(-4500,-3100,0));Place(B.Get(),FVector(-4100,-3100,0));
    // Reserve all256 cells with four bounded budget owners; no input/source grant is counted as earned progress.
    TArray<FGuid> Owners={M->ContenderId(A.Get()),M->ContenderId(B.Get()),FGuid::NewGuid(),FGuid::NewGuid()};int32 Index=0;bool Full=true;
    for(int32 OwnerIndex=0;OwnerIndex<4;++OwnerIndex){for(int32 CastIndex=0;CastIndex<4;++CastIndex){auto Q=C->EnvironmentCommand(Body(A.Get()),EInput::Water,Now(),10000);Q.State.Owner=Q.State.BudgetOwner=Owners[OwnerIndex];for(int32 N=0;N<16;++N){Q.Cells.Add(FIntPoint(Index%60,Index/60));++Index;}Full&=C->QueueEnvironment(Q,Now());C->CommitRiver(Now());}}
    Check(Full&&C->RiverCells.Num()==256,TEXT("Native service reaches exactly256 cells with64 per budget owner"));Check(!C->ReserveEnvironment(Body(A.Get()),2,Now(),3000).IsValid(),TEXT("Full match rejects unpaid projectile reservation generically"));
    // Same-cell updates consume evaluation credit without consuming new cell capacity.
    bool Updated=true;for(int32 Batch=0;Batch<16;++Batch){auto Q=C->EnvironmentCommand(Body(A.Get()),EInput::Water,Now(),10000);Q.State.Owner=Q.State.BudgetOwner=Owners[0];for(int32 N=0;N<16;++N){Q.Cells.Add(FIntPoint(N,0));}Updated&=C->QueueEnvironment(Q,Now());C->CommitRiver(Now());}
    auto Excess=C->EnvironmentCommand(Body(A.Get()),EInput::Water,Now(),10000);Excess.Cells.Add(FIntPoint(0,0));Check(Updated&&C->RiverEvaluations==512&&!C->QueueEnvironment(Excess,Now())&&C->RiverCells.Num()==256,TEXT("Exactly512 native transition evaluations accepted; next command rejected without evicting cells"));
    TArray<double> Times;for(int32 I=0;I<200;++I){const double T=FPlatformTime::Seconds();C->SampleRiver(Now());Times.Add((FPlatformTime::Seconds()-T)*1000);}Times.Sort();Check(true,FString::Printf(TEXT("Measured256-cell maintenance: p95=%.3fms p99=%.3fms;2 remote contenders plus native bodies,4 budget owners; not4-client release performance"),Times[189],Times[197]));
    bool Private=true;for(TActorIterator<ALDTacticalField> It(W);It;++It){if(It->EnvironmentPool){Private&=!It->GetIsReplicated();}}Check(Private,TEXT("Canonical grid/reservation/creator records have no replicated actor channel"));
    Advance(10.1);Check(C->RiverCells.IsEmpty()&&C->HeldEnvironment()==0,TEXT("Expiry releases every record and reservation before later allocation"));
    auto* OA=M->StateFor(A.Get());OA->Build.Slots[2]=ELDSkill::Firebolt;OA->Build.Actions[2].Skill=ELDSkill::Firebolt;M->SyncOwner(A.Get());M->SyncOwner(B.Get());
}
