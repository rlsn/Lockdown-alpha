#include "Tests/LDObjectiveProbe.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Core/LDRules.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"

ALDObjectiveProbe::ALDObjectiveProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDObjectiveProbe::Check(bool Pass,const FString& Text)
{Checks.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Text);if(!Pass){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_OBJECTIVE_PROBE %s"),*Checks.Last());}
void ALDObjectiveProbe::Place(FVector Point)
{auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(Point,false,nullptr,ETeleportType::TeleportPhysics);Body->GetCharacterMovement()->StopMovementImmediately();Body->GetCharacterMovement()->DisableMovement();}
void ALDObjectiveProbe::Refresh(){Director->RefreshHold(PC,PC->OwnerState.Life,Token,++Sequence,false,Now);}
void ALDObjectiveProbe::Press(uint8 Target,FGuid Id)
{Release();Now+=.1;++Token;Sequence=0;Director->Queue(PC,PC->OwnerState.Match,PC->OwnerState.Life,Token,Target,Id);Refresh();Director->Sample(Now);}
void ALDObjectiveProbe::Hold(double Seconds)
{const double End=Now+Seconds;while(Now<End-1.e-6){Now=FMath::Min(End,Now+.1);Refresh();Director->Sample(Now);}}
void ALDObjectiveProbe::Release(){Director->Release(PC,PC->OwnerState.Life,Token);}
FGuid ALDObjectiveProbe::VisibleSeal()
{
    auto* V=GetWorld()->GetAuthGameMode<ALDGameMode>()->Vision.Get();V->SetPublicSensor(false);V->Tick(0);
    if(const auto* View=V->AuthorityViewFor(PC)){for(const auto& E:View->Entities){if(E.Kind==ELDObservedKind::Seal){return E.Id;}}}return FGuid();
}
void ALDObjectiveProbe::Run()
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();Director=Mode->Objectives;PC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* C=Mode->Combat.Get();auto* GS=GetWorld()->GetGameState<ALDGameState>();C->SetActorTickEnabled(false);Now=GetWorld()->GetTimeSeconds();
    int32 NpcIndex=0;
    for(TActorIterator<ALDResident> It(GetWorld());It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();It->SetActorLocation(FVector(-2100+NpcIndex++*70,1300,92));if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
    FString Case;FParse::Value(FCommandLine::Get(),TEXT("LDObjectiveCase="),Case);
    if(Case==TEXT("Withdraw"))
    {
        Place(LDObjective::Position(LDObjective::Guild)+FVector(0,60,92));Press(LDObjective::Guild);
        auto* Item=Director->SeedFixtureSeal();const FGuid Identity=Item->Identity;
        Place(LDObjective::SeedPosition()+FVector(30,0,76));Press(LDObjective::Seal,VisibleSeal());
        Place(FVector(60,0,92));Press(LDObjective::Heart);Hold(.5);
        Check(PC->OwnerState.ContractStage==1&&PC->OwnerState.SealId==Identity&&PC->OwnerState.ChannelKind==3,TEXT("Withdrawing contender has real private work, carried Seal and active hold"));
        Director->Queue(PC,PC->OwnerState.Match,PC->OwnerState.Life,++Token,LDObjective::Heart,FGuid());
        const FVector Point=PC->GetPawn()->GetActorLocation();Mode->Logout(PC);
        Check(!Mode->StateFor(PC)&&!Director->StateFor(PC)&&!Director->QueuedRequestCount(),TEXT("Logout removes contender, held state and queued transactions"));
        Check(Item->Identity==Identity&&Item->State==0&&!Item->Carrier.IsValid()&&FVector::DistSquared2D(Point,Item->GetActorLocation())<120*120,TEXT("Logout drops exactly one immutable Seal on nearby valid ground"));
        Director->Sample(Now+1);Check(!Mode->IsFinished()&&!GS->bClaimActive,TEXT("Withdrawal cleanup grants no automatic winner"));
        Director->RefreshHold(PC,PC->OwnerState.Life,Token,++Sequence,false,Now+2);Director->Sample(Now+2);
        Check(!Director->StateFor(PC)&&Item->State==0,TEXT("Late hold after logout cannot recreate state or ownership"));Finish();return;
    }
    if(Case==TEXT("ExactDeadline")||Case==TEXT("LateClaim")||Case==TEXT("NoClaim"))
    {
        Director->SetDefencesFixture(0);Place(FVector(60,0,92));
        if(Case==TEXT("NoClaim"))
        {Director->Sample(Now+10);Check(!Mode->IsFinished(),TEXT("One living contender does not win"));Mode->EndContenderLife(PC,Now+11);Director->Sample(Now+12);Check(!Mode->IsFinished(),TEXT("Zero active contenders do not win"));Now=Director->Deadline();Director->Sample(Now);Check(GS->EndReason==2&&GS->WinningRoute==0,TEXT("Deadline with no claim has no winner"));}
        else
        {
            Now=Director->Deadline()-(Case==TEXT("ExactDeadline")?10.:9.9);++Token;Director->Queue(PC,PC->OwnerState.Match,PC->OwnerState.Life,Token,LDObjective::Heart,FGuid());Refresh();Director->Sample(Now);const double Scheduled=Director->StateFor(PC)->Started+10;
            Now=Director->Deadline()+.5;Refresh();Director->Sample(Now);
            Check(Case==TEXT("ExactDeadline")?Scheduled<=Director->Deadline()+1.e-6:Scheduled>Director->Deadline(),TEXT("Controlled scheduled completion boundary"));
            Check(Case==TEXT("ExactDeadline")?GS->EndReason==1&&GS->WinningRoute==LDObjective::Assault:GS->EndReason==2,TEXT("Late evaluation honors scheduled deadline rather than processing order"));
        }
        const auto Receipt=GS->ResultReceipt;const uint32 Serial=GS->ResultSerial;Director->Sample(Now+100);
        Check(Receipt.IsValid()&&GS->ResultReceipt==Receipt&&Serial==1&&GS->ResultSerial==1,TEXT("Terminal result commits exactly once"));Finish();return;
    }
    Place(FVector(1700,-900,92));Press(LDObjective::Guild);Check(PC->OwnerState.ContractStage==0,TEXT("Out-of-range Guild request grants no contract"));
    Place(LDObjective::Position(LDObjective::Guild)+FVector(0,60,92));Press(LDObjective::Guild);
    const FGuid Contract=PC->OwnerState.ContractId;
    Check(Contract.IsValid()&&PC->OwnerState.ContractStage==1&&PC->OwnerState.Gold==0,TEXT("Guild acceptance creates private collect stage without reward"));
    PC->ServerContextRequest(PC->OwnerState.Match,PC->OwnerState.Life,Token,LDObjective::Guild,FGuid());
    Check(PC->OwnerState.ContractId==Contract,TEXT("Duplicate acceptance does not replace the contract"));
    Place(LDObjective::Position(LDObjective::Station)+FVector(0,60,92));Press(LDObjective::Station);Hold(1);
    Check(PC->OwnerState.ChannelKind==LDObjective::Collect&&PC->OwnerState.ChannelProgress>=.99,TEXT("Collection exposes confirmed private progress"));
    Now+=.1;Director->RefreshHold(PC,PC->OwnerState.Life,Token,++Sequence,true,Now);Director->Sample(Now);
    Check(!PC->OwnerState.ChannelKind&&PC->OwnerState.ContractStage==1,TEXT("Movement intent resets collection without consuming the contract"));
    Release();Director->RefreshHold(PC,PC->OwnerState.Life,Token,++Sequence,false,Now+.01);Director->Sample(Now+.01);
    Check(!PC->OwnerState.ChannelKind,TEXT("Late hold cannot resurrect a released interaction"));
    Press(LDObjective::Station);Now+=.251;Director->Sample(Now);
    Check(!PC->OwnerState.ChannelKind&&PC->OwnerState.ContractStage==1,TEXT("Missing refresh expires after 250ms"));
    Press(LDObjective::Station);Hold(2.99);Check(PC->OwnerState.ContractStage==1,TEXT("No collection before three seconds"));Hold(.02);
    Check(PC->OwnerState.ContractStage==2&&PC->OwnerState.Gold==0&&PC->OwnerState.Xp==0,TEXT("Three-second collection creates package with no early payout"));
    Place(LDObjective::Position(LDObjective::Guild)+FVector(0,60,92));Press(LDObjective::Guild);const double Due=Director->StateFor(PC)->Started+3;
    auto* Attacker=GetWorld()->SpawnActor<ALDCharacter>(PC->GetPawn()->GetActorLocation()-FVector(80,0,0),FRotator::ZeroRotator);Attacker->GetCharacterMovement()->DisableMovement();FString Reason;
    C->RequestBody(Attacker,0,FVector::ForwardVector,Reason);C->StateFor(Attacker)->ImpactAt=Due;Now=Due;Refresh();C->Sample(Now);
    Check(PC->OwnerState.Hp==80&&!PC->OwnerState.ChannelKind&&PC->OwnerState.Gold==0&&PC->OwnerState.DeliverySerial==0,TEXT("Completion-batch damage interrupts delivery before payout"));
    Attacker->Destroy();Now+=.1;Press(LDObjective::Guild);Hold(3.01);
    Check(PC->OwnerState.Gold==25&&PC->OwnerState.Xp==15&&PC->OwnerState.Commissions==1&&PC->OwnerState.DeliverySerial==1&&!PC->OwnerState.ContractStage,TEXT("Delivery commits exactly25 gold and15 XP once"));
    PC->ServerContextRequest(PC->OwnerState.Match,PC->OwnerState.Life,Token,LDObjective::Guild,FGuid());Director->Sample(Now+.1);
    Check(PC->OwnerState.Gold==25&&PC->OwnerState.DeliverySerial==1,TEXT("Duplicate completion cannot pay twice"));
    Press(LDObjective::Guild);Check(!PC->OwnerState.ContractStage,TEXT("Twenty-second personal work cooldown blocks acceptance"));
    Now=PC->OwnerState.WorkReadyAt;Press(LDObjective::Guild);Check(PC->OwnerState.ContractStage==1,TEXT("Courier can repeat after cooldown"));
    Place(LDObjective::Position(LDObjective::Station)+FVector(0,60,92));Press(LDObjective::Station);Hold(3.01);
    Place(LDObjective::Position(LDObjective::Guild)+FVector(0,60,92));Press(LDObjective::Guild);Hold(3.01);
    Check(PC->OwnerState.Gold==50&&PC->OwnerState.Xp==30&&LDRules::LevelFromXp(PC->OwnerState.Xp)==2,TEXT("Second real delivery reaches retained hero level two without upgrades"));
    Place(FVector(60,0,92));
    for(uint8 Flags=1;Flags<16;++Flags){Director->SetDefencesFixture(Flags);Press(LDObjective::Heart);Check(!PC->OwnerState.ChannelKind,FString::Printf(TEXT("No Seal bypass with defensive mask %u"),Flags));}
    Director->SetDefencesFixture(0);Press(LDObjective::Heart);Hold(.6);Check(PC->OwnerState.ChannelKind==3&&PC->OwnerState.ChannelRoute==LDObjective::Assault,TEXT("Ordinary route opens only when all four flags are clear"));
    Release();Now+=.1;Director->Sample(Now);Check(!PC->OwnerState.ChannelKind&&Director->HeartSensorActive(Now+1),TEXT("Release resets claim while shared Heart sight lingers"));
    Check(!Director->HeartSensorActive(Now+2.1),TEXT("Heart sight expires two seconds after final held attempt"));
    Press(LDObjective::Heart);Hold(.4);Place(FVector(220,0,92));Now+=.1;Refresh();Director->Sample(Now);
    Check(!PC->OwnerState.ChannelKind,TEXT("Leaving Heart range resets progress"));
    Place(FVector(60,0,92));Press(LDObjective::Heart);Hold(.4);
    Attacker=GetWorld()->SpawnActor<ALDCharacter>(PC->GetPawn()->GetActorLocation()-FVector(80,0,0),FRotator::ZeroRotator);Attacker->GetCharacterMovement()->DisableMovement();C->RequestBody(Attacker,0,FVector::ForwardVector,Reason);
    const double ClaimDue=Director->StateFor(PC)->Started+10;C->StateFor(Attacker)->ImpactAt=ClaimDue;Now=ClaimDue;Refresh();C->Sample(Now);
    Check(!Mode->IsFinished()&&!PC->OwnerState.ChannelKind,TEXT("Damage on ten-second completion batch prevents a Heart win"));Attacker->Destroy();
    Director->SetDefencesFixture(15);ALDSeal* Item=Director->SeedFixtureSeal();Check(Item&&!Director->SeedFixtureSeal(),TEXT("Fixture creates at most one immutable Seal"));
    const FGuid Immutable=Item->Identity;Place(LDObjective::SeedPosition()+FVector(30,0,76));FGuid Id=VisibleSeal();Check(Id.IsValid(),TEXT("Dropped Seal enters only a permitted observed-ID view"));
    // This injected-clock scenario still has a legitimate lingering Heart source near Guild.
    // Move the item beyond that source as well as beyond the body's sight before testing concealment.
    Item->SetActorLocation(LDObjective::SeedPosition()-FVector(500,0,0));Place(FVector(1700,-900,92));VisibleSeal();Check(!Mode->Vision->ResolveObserved(PC,Id),TEXT("A newly hidden item cannot be resolved by an old observed ID"));
    Item->SetActorLocation(LDObjective::SeedPosition());Place(LDObjective::SeedPosition()+FVector(30,0,76));Id=VisibleSeal();Press(LDObjective::Seal,Id);
    Check(Item->State==1&&Item->Carrier==PC&&PC->OwnerState.SealId==Immutable,TEXT("Real pickup atomically transfers one item to its owner"));
    VisibleSeal();const auto* View=Mode->Vision->AuthorityViewFor(PC);Check(View&&!View->Entities.ContainsByPredicate([](const auto& E){return E.Kind==ELDObservedKind::Seal;}),TEXT("Carried Seal has no world pose or carrier-body marker in observations"));
    // Start a new contract and package, then lose both on a real damage death.
    Now=FMath::Max(Now,double(PC->OwnerState.WorkReadyAt));Place(LDObjective::Position(LDObjective::Guild)+FVector(0,60,92));Press(LDObjective::Guild);
    Place(LDObjective::Position(LDObjective::Station)+FVector(0,60,92));Press(LDObjective::Station);Hold(3.01);
    const uint32 OldLife=PC->OwnerState.Life;auto* OldBody=CastChecked<ALDCharacter>(PC->GetPawn());C->StateFor(OldBody)->Hp=20;
    Attacker=GetWorld()->SpawnActor<ALDCharacter>(OldBody->GetActorLocation()-FVector(80,0,0),FRotator::ZeroRotator);Attacker->GetCharacterMovement()->DisableMovement();C->RequestBody(Attacker,0,FVector::ForwardVector,Reason);Now+=.2;C->StateFor(Attacker)->ImpactAt=Now;C->Sample(Now);Attacker->Destroy();
    Check(!PC->OwnerState.bActive&&!PC->OwnerState.ContractStage&&!PC->OwnerState.SealId.IsValid()&&Item->State==0&&Item->Identity==Immutable,TEXT("Real death cancels package and drops exactly the carried immutable item"));
    for(TActorIterator<ALDCorpse> It(GetWorld());It;++It){It->Destroy();}
    Check(IsValid(Item)&&Item->State==0,TEXT("Explicit corpse cleanup cannot remove the independent dropped Seal"));
    Check(PC->OwnerState.Xp==30&&PC->OwnerState.Commissions==2,TEXT("Death retains XP and completed commission count"));
    Now=PC->OwnerState.ReturnAt+.01;Mode->ReturnDueContenders(Now);Place(Item->GetActorLocation()+FVector(30,0,76));Id=VisibleSeal();Press(LDObjective::Seal,Id);
    Check(!PC->OwnerState.SealId.IsValid()&&Item->State==0,TEXT("Return protection blocks Seal pickup"));
    Now=PC->OwnerState.ProtectedUntil+.01;Press(LDObjective::Seal,Id);Check(PC->OwnerState.SealId==Immutable&&PC->OwnerState.Life==OldLife+1,TEXT("Returned life can recover the same item after protection"));
    PC->ServerContextRequest(PC->OwnerState.Match,OldLife,++Token,LDObjective::Heart,FGuid());Check(PC->OwnerState.SealId==Immutable,TEXT("Old-life command cannot transfer or consume recovered item"));
    Place(FVector(60,0,92));Press(LDObjective::Heart);Check(PC->OwnerState.ChannelRoute==LDObjective::SealRoute&&GS->DefensiveFlags==15,TEXT("Only the actual carrier bypasses active defences via Seal route"));
    auto* Resident=GetWorld()->SpawnActor<ALDResident>(FVector(-60,0,100),FRotator::ZeroRotator);Resident->SetActorTickEnabled(false);Resident->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(Resident->GetController())){AI->StopMovement();}
    Hold(9.99);Check(!Mode->IsFinished()&&!GS->bContested,TEXT("Residents do not contest and no claim wins before ten seconds"));Hold(.02);
    Check(GS->EndReason==1&&GS->WinningRoute==LDObjective::SealRoute&&GS->ResultSerial==1&&PC->OwnerState.bWinner,TEXT("Uninterrupted ten-second claim commits one winning receipt"));
    Check(Item->State==2&&!PC->OwnerState.SealId.IsValid(),TEXT("Seal-route victory consumes exactly the winning item"));
    const auto Receipt=GS->ResultReceipt;Director->Sample(Now+100);Check(GS->ResultReceipt==Receipt&&GS->ResultSerial==1,TEXT("Later samples cannot replace terminal result"));
    Check(!C->RequestBody(Cast<ALDCharacter>(PC->GetPawn()),0,FVector::ForwardVector,Reason),TEXT("Competitive combat is rejected after result"));Finish();
}
void ALDObjectiveProbe::Tick(float Delta)
{
    Super::Tick(Delta);auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(GetWorld()->GetTimeSeconds()<.5||!Nav||UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)){return;}SetActorTickEnabled(false);Run();
}
void ALDObjectiveProbe::Finish()
{
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("failures"),Failures);TArray<TSharedPtr<FJsonValue>> Lines;for(const auto& Text:Checks){Lines.Add(MakeShared<FJsonValueString>(Text));}Json->SetArrayField(TEXT("checks"),Lines);
    FString Output,Path;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));FParse::Value(FCommandLine::Get(),TEXT("LDProbeReport="),Path);
    if(Path.IsEmpty()||!FFileHelper::SaveStringToFile(Output,*Path)){++Failures;}FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
