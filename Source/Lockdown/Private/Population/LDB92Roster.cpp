#include "Population/LDB92Roster.h"
#include "Population/LDB92PopulationContract.h"
#include "Economy/LDB92Economy.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Character/LDCharacter.h"
#include "Development/LDDevelopmentDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapDefinition.h"
#include "Vision/LDVisionDirector.h"
#include "World/LDWorldObjects.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameSession.h"
#include "NavigationSystem.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

ALDB92GameMode::ALDB92GameMode(){PlayerControllerClass=ALDB92Controller::StaticClass();HUDClass=ALDB92HUD::StaticClass();}
void ALDB92Controller::ClientB92Admission_Implementation(const FLDOwnerState& State,uint32 CombatFloor)
{ClientLifeReceipt_Implementation(State);RaiseCombatTokenFloor(CombatFloor);}
ALDB92SeatController::ALDB92SeatController(){bReplicates=false;}
void ALDB92SeatController::BeginPlay(){ALDPlayerController::BeginPlay();SetActorTickEnabled(false);if(PlayerState){PlayerState->SetReplicates(false);}}
void ALDB92Controller::PawnLeavingGame()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();FString Why;
    if(M&&M->Population&&!GetWorld()->bIsTearingDown&&M->Population->Release(this,Why)){return;}
    Super::PawnLeavingGame();
}
void ALDB92Controller::Destroyed()
{
    // Run before APlayerController can destroy its pawn or unpossess it. Logout
    // and PawnLeavingGame may follow; all three share the idempotent release.
    if(HasAuthority()&&!GetWorld()->bIsTearingDown){if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Population){FString Why;M->Population->Release(this,Why);}}
    Super::Destroyed();
}
ALDB92Roster::ALDB92Roster(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
bool ALDB92Roster::Configure(int32 Humans,FString& Error)
{
    if(!HasAuthority()||Configured||Humans<1||Humans>12){Error=TEXT("Roster configuration requires one authority and 1–12 human capacity");return false;}
    Limit=Humans;Match=FGuid::NewGuid();Began=GetWorld()->GetTimeSeconds();Configured=true;return true;
}
int32 ALDB92Roster::Humans() const {int32 N=0;for(const auto& S:Contenders){N+=S.Human;}return N;}
bool ALDB92Roster::CanAdmit() const {return !Ending&&Humans()+Pending.Num()<Limit;}
FVector ALDB92Roster::StartFor(ALDPlayerController* PC) const
{for(const auto& S:Contenders){if(S.Controller==PC){return S.Start;}}return FVector::ZeroVector;}

bool ALDB92Roster::Fit(FVector Source,const TArray<FVector>& Occupied,FVector& Out) const
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());if(!Nav){return false;}
    for(int32 Ring=0;Ring<=10;++Ring){for(int32 X=-Ring;X<=Ring;++X){for(int32 Y=-Ring;Y<=Ring;++Y)
    {
        if(FMath::Max(FMath::Abs(X),FMath::Abs(Y))!=Ring){continue;}const FVector P=Source+FVector(X*100,Y*100,0);
        if(FVector::DistSquared2D(P,Source)>1000*1000){continue;}
        FHitResult Hit;if(!GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,120),P-FVector(0,0,180),ECC_GameTraceChannel1)||Hit.ImpactNormal.Z<FMath::Cos(FMath::DegreesToRadians(40.))){continue;}
        const FVector Center=Hit.ImpactPoint+FVector(0,0,95);FNavLocation Floor;
        if(!Nav->ProjectPointToNavigation(Hit.ImpactPoint,Floor,FVector(60,60,50))||FVector::DistSquared2D(Floor.Location,Hit.ImpactPoint)>60*60||FMath::Abs(Floor.Location.Z-Hit.ImpactPoint.Z)>50
            ||GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(30,90))
            ||Occupied.ContainsByPredicate([&](FVector A){return FMath::Abs(A.Z-Center.Z)<180&&FVector::DistSquared2D(A,Center)<90*90;})){continue;}
        Out=Center;return true;
    }}}
    return false;
}
bool ALDB92Roster::Initialize(FString& Error)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* Map=ULDMapDefinition::Get(GetWorld());
    if(!Configured||Ready||Initializing||!M||M->Population!=this||!M->IsPopulationProfile()||!Map||M->Economy||M->WorldChecks||!M->Contenders.IsEmpty())
    {Error=TEXT("Roster requires a pristine population review, without legacy identities/economy");return false;}
    FString PopulationJson,SchedulesJson;TSharedPtr<FJsonObject> Definition,Schedules;
    if(!FFileHelper::LoadFileToString(PopulationJson,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/population_b92.json")))
        ||!FFileHelper::LoadFileToString(SchedulesJson,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/schedules_b92.json")))
        ||FMD5::HashAnsiString(*PopulationJson)!=LDB92PopulationContract::Population||FMD5::HashAnsiString(*SchedulesJson)!=LDB92PopulationContract::Schedules
        ||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PopulationJson),Definition)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SchedulesJson),Schedules))
    {Error=TEXT("Population/routine contract mismatch; run SyncB92Population.py");return false;}
    TArray<FLDB92Person> People;TSet<FName> Sources;
    auto Front=[&](FName POI){const auto* Service=Map->ServiceFor(POI);return Service?Service->Position-FVector(0,1400,0):Map->NavigationTarget(POI);};
    for(const auto& Value:Schedules->GetArrayField(TEXT("rows")))
    {
        const auto J=Value->AsObject();FLDB92Person P;P.Source=*J->GetStringField(TEXT("id"));P.Id=FGuid::NewGuid();
        if(P.Source.IsNone()||Sources.Contains(P.Source)){Error=TEXT("Duplicate population source");return false;}Sources.Add(P.Source);
        const FString RoutineRole=J->GetStringField(TEXT("role"));
        if(RoutineRole==TEXT("civilian"))
        {
            P.Role=ELDB92Role::Civilian;const FName Home=*J->GetStringField(TEXT("home_building_id"));const auto* B=Map->Buildings.FindByPredicate([&](const auto& V){return V.Id==Home;});
            if(!B||!Map->Find(*J->GetStringField(TEXT("work_poi_id")))||!Map->Find(*J->GetStringField(TEXT("rest_poi_id")))){Error=TEXT("Civilian source has an unknown home/work/rest endpoint");return false;}
            P.Home=B->Position-FVector(0,1400,0);
        }
        else if(RoutineRole==TEXT("traveler"))
        {
            P.Role=ELDB92Role::Traveler;const FName From=*J->GetStringField(TEXT("from_poi_id")),To=*J->GetStringField(TEXT("to_poi_id"));
            if(!Map->Find(From)||!Map->Find(To)||From==To){Error=TEXT("Traveler needs two known endpoints");return false;}P.Home=Front(From);
        }
        else if(RoutineRole==TEXT("guard"))
        {P.Role=ELDB92Role::Guard;const FName Home=*J->GetStringField(TEXT("home_poi_id"));if(!Map->Find(Home)){Error=TEXT("Unknown guard home");return false;}P.Home=Front(Home);}
        else{Error=TEXT("Unknown routine role");return false;}
        People.Add(P);
    }
    for(const auto& Settlement:Map->Settlements)
    {
        int32 Staff=0;for(const auto& Service:Map->Services){if(Service.Settlement!=Settlement.Id||Staff==4||Settlement.Kind==TEXT("town")){continue;}
            // Five cities staff four shared institutions each; village access is a kiosk.
            if(Settlement.Id.ToString().StartsWith(TEXT("town_"))){continue;}
            FLDB92Person P;P.Source=FName(*FString::Printf(TEXT("official_%02d"),People.Num()-152));P.Id=FGuid::NewGuid();P.Role=ELDB92Role::Official;P.Home=Front(Service.POI);People.Add(P);++Staff;
        }
    }
    int32 Hostile=0;for(const auto& POI:Map->POIs){if(POI.Kind!=TEXT("camp")&&POI.Kind!=TEXT("boss")){continue;}FLDB92Person P;P.Source=FName(*FString::Printf(TEXT("hostile_%02d"),Hostile++));P.Id=FGuid::NewGuid();P.Role=ELDB92Role::Hostile;P.Home=POI.Position;People.Add(P);}
    for(const auto& Settlement:Map->Settlements){FLDB92Person P;P.Source=FName(*FString::Printf(TEXT("hostile_%02d"),Hostile++));P.Id=FGuid::NewGuid();P.Role=ELDB92Role::Hostile;P.Home=Settlement.Position+FVector(2200,0,0);People.Add(P);}
    int32 Counts[5]={};for(const auto& P:People){++Counts[uint8(P.Role)];}
    if(People.Num()!=196||Counts[0]!=96||Counts[1]!=32||Counts[2]!=24||Counts[3]!=20||Counts[4]!=24||Map->Spawns.Num()!=24)
    {Error=TEXT("Population budget is not 96/32/24/20/24 plus24 contenders");return false;}
    TArray<FVector> Pads,SeatPads;
    for(const auto& Spawn:Map->Spawns){FVector P;if(!Fit(Spawn.Position,Pads,P)){Error=TEXT("No native contender pad for ")+Spawn.Id.ToString();return false;}Pads.Add(P);SeatPads.Add(P);}
    for(auto& P:People){FVector Pad;if(!Fit(P.Home,Pads,Pad)){Error=TEXT("No native role pad within10m for ")+P.Source.ToString();return false;}P.Home=Pad;Pads.Add(Pad);}
    Initializing=true;TArray<AActor*> Created;
    auto Cleanup=[&](){M->Contenders.Reset();Contenders.Reset();Population.Reset();for(auto* A:Created){if(IsValid(A)){A->Destroy();}}Initializing=false;};
    TArray<FGuid> SeatIds,Civilians;
    for(int32 I=0;I<24;++I)
    {
        auto* PC=GetWorld()->SpawnActor<ALDB92SeatController>();if(!PC){Error=TEXT("Seat controller allocation failed");Cleanup();return false;}Created.Add(PC);
        FLDContenderRecord R;R.Session=FGuid::NewGuid();R.State.bDevelopmentProfile=true;R.State.WardCharges=0;R.State.Build=LDDevelopment::TacticalStart(I%9,M->TacticalCatalog->Checksum,8);
        R.State.Hp=R.State.MaxHp=LDDevelopment::MaxHP(R.State.Build);R.State.ProtectedUntil=GetWorld()->GetTimeSeconds()+3;M->Contenders.Add(PC,R);
        auto* Body=GetWorld()->SpawnActor<ALDCharacter>(SeatPads[I],FRotator::ZeroRotator);if(!Body){Error=TEXT("Contender body allocation failed");Cleanup();return false;}Created.Add(Body);PC->Possess(Body);Body->SetReplicates(false);
        M->Inventory->Initialize(PC);M->RefreshBuild(PC,true);M->Combat->StateFor(Body)->ProtectionUntil=R.State.ProtectedUntil;
        FLDB92Seat S;S.Id=R.Session;S.Controller=PC;S.Body=Body;S.Start=SeatPads[I];Contenders.Add(S);SeatIds.Add(S.Id);
    }
    for(auto& P:People)
    {
        auto* Body=GetWorld()->SpawnActor<ALDCharacter>(P.Home,FRotator::ZeroRotator);if(!Body){Error=TEXT("Population body allocation failed");Cleanup();return false;}Created.Add(Body);Body->SetReplicates(false);Body->SetActorTickEnabled(false);P.Body=Body;
        if(P.Role==ELDB92Role::Civilian){Civilians.Add(P.Id);}if(P.Role==ELDB92Role::Official){M->Combat->StateFor(Body)->ProtectionUntil=MAX_dbl;}
    }
    Economy=GetWorld()->SpawnActor<ALDB92Economy>();if(!Economy){Error=TEXT("Economy allocation failed");Cleanup();return false;}Created.Add(Economy);
    if(!Economy->Initialize(Match,SeatIds,Civilians,Error,true)){Economy=nullptr;Cleanup();return false;}
    Population=MoveTemp(People);Ready=true;Initializing=false;Services=GetWorld()->SpawnActor<ALDB92Services>();if(!Services){Error=TEXT("Service authority allocation failed");return false;}Services->AddTickPrerequisiteActor(this);M->MatchStartedAt=GetWorld()->GetTimeSeconds();M->GetGameState<ALDGameState>()->MatchDeadline=M->StartedAt()+5400;
    UE_LOG(LogTemp,Display,TEXT("LD_B92_ROSTER_READY contenders=24 noncontenders=196 humanCapacity=%d civilianGold=288 routines=not_implemented contenderPolicy=not_implemented"),Limit);
    return Audit(Error);
}

bool ALDB92Roster::Transfer(int32 Index,ALDPlayerController* Next,bool Human,FString& Error,ALDPlayerController* Previous)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!Ready||!Next||Next->GetWorld()!=GetWorld()||!Contenders.IsValidIndex(Index)||M->Contenders.Contains(Next)){Error=TEXT("Invalid control transfer");return false;}
    auto& Seat=Contenders[Index];auto* Old=Previous?Previous:Seat.Controller.Get();const auto* Record=M->Contenders.Find(Seat.Controller);
    if(!Old||!Record||Record->Session!=Seat.Id){Error=TEXT("Persistent seat lost its previous authority record");return false;}
    M->Combat->FlushPastCommand(); // settle accepted past events before cancelling future orders
    if(!Economy->SynchronizeWorld(Error)){return false;}
    auto* Body=Cast<ALDCharacter>(Old->GetPawn());if(!Body&&Record->State.bActive){Body=Seat.Body.Get();}
    uint32 CombatFloor=Old->CombatTokenFloor();
    if(const auto* S=M->Combat->StateFor(Body)){CombatFloor=FMath::Max(CombatFloor,FMath::Max3(S->LastCoreToken,S->CancelledCoreThrough,FMath::Max(S->AttackToken,S->SkillToken)));}
    if(Services){Services->Close(Cast<ALDB92Controller>(Old));}
    Old->CancelInterruptibleHolds();if(Old->Navigation){Old->Navigation->Stop();Old->Navigation->ResetLife();}
    if(auto* B91=Cast<ALDB91Controller>(Old))
    {if(auto* Objects=ALDWorldObjects::Get(GetWorld())){Objects->Cancel(B91,TEXT("Control changed"));}if(auto* E=ALDEnvironmentDirector::Get(GetWorld())){E->Cancel(B91,TEXT("Control changed"));}}
    if(M->Development){M->Development->Cancel(Old,TEXT("Control changed"));}if(M->Trade){M->Trade->CancelFor(Old,TEXT("Control changed"));}
    M->Combat->ReleaseSkillHold(Old,MAX_uint32);M->Combat->CancelTactical(Body,TEXT("Control changed"));
    if(auto* S=M->Combat->StateFor(Body)){S->bAttackOrder=false;S->AttackTarget.Reset();}
    // Cancellation may have revised the bag/build. Copy only after it finishes.
    FLDContenderRecord Saved=M->Contenders.FindChecked(Seat.Controller);Saved.State.bLobbyHost=false;
    if(APawn* Extra=Next->GetPawn()){Next->UnPossess();Extra->Destroy();}
    if(M->Vision){M->Vision->Unregister(Old);}Old->UnPossess();M->Contenders.Remove(Seat.Controller);M->Contenders.Add(Next,MoveTemp(Saved));
    for(TActorIterator<ALDItem> It(GetWorld());It;++It){if(It->Holder==Old){It->Holder=Next;}}
    Seat.Controller=Next;Seat.Human=Human;Seat.Body=Body;
    if(Body){Body->bOnlyRelevantToOwner=true;Body->bAlwaysRelevant=false;Body->SetReplicates(Human);Next->Possess(Body);}
    M->SyncOwner(Next);Next->RaiseCombatTokenFloor(CombatFloor);
    CastChecked<ALDB92Controller>(Next)->ClientB92Admission(Next->OwnerState,CombatFloor); // state and input floor precede sequence-bound cargo presentation
    if(auto* Objects=ALDWorldObjects::Get(GetWorld());Objects&&!Objects->TransferCarrier(Cast<ALDB91Controller>(Old),Cast<ALDB91Controller>(Next))){UE_LOG(LogTemp,Fatal,TEXT("B92 carrier binding invariant failed during control transfer"));}
    if(Next->Navigation){Next->Navigation->ResetLife();}if(M->Vision){M->Vision->Register(Next);}
    if(!Human){Next->SetReplicates(false);}if(Old->IsA<ALDB92SeatController>()){Old->Destroy();}
    UE_LOG(LogTemp,Display,TEXT("LD_B92_CONTROL_TRANSFER seat=%s human=%d life=%u inventory=%u"),*Seat.Id.ToString(),Human,Next->OwnerState.Life,Next->OwnerState.Inventory.Revision);return true;
}
bool ALDB92Roster::Admit(ALDPlayerController* PC,FString& Error)
{
    Error.Reset();if(!HasAuthority()||Ending||!IsValid(PC)||PC->IsActorBeingDestroyed()||PC->GetWorld()!=GetWorld()||!PC->IsA<ALDB92Controller>()||PC->IsA<ALDB92SeatController>()){Error=TEXT("Invalid human admission");return false;}
    for(const auto& S:Contenders){if(S.Controller==PC){return S.Human;}}
    if(Pending.Contains(PC)){return true;}if(!CanAdmit()){Error=TEXT("All configured human seats are occupied");return false;}
    if(!Ready){Pending.Add(PC);return true;}
    for(int32 I=0;I<Contenders.Num();++I){if(!Contenders[I].Human){return Transfer(I,PC,true,Error);}}
    Error=TEXT("No existing AI seat available");return false;
}
bool ALDB92Roster::Release(ALDPlayerController* PC,FString& Error)
{
    if(!HasAuthority()||Ending||!PC){return false;}Pending.Remove(PC);
    for(int32 I=0;I<Contenders.Num();++I){if(Contenders[I].Human&&Contenders[I].Controller==PC)
    {
        auto* Next=GetWorld()->SpawnActor<ALDB92SeatController>();if(!Next){Error=TEXT("Cannot allocate takeover controller");return false;}
        if(!Transfer(I,Next,false,Error,PC)){Next->Destroy();return false;}return true;
    }}return false;
}
bool ALDB92Roster::Audit(FString& Error) const
{
    const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!Ready||!M||Contenders.Num()!=24||Population.Num()!=196||M->Contenders.Num()!=24||Humans()>Limit||!Economy||!Economy->Audit(Error)){if(Error.IsEmpty()){Error=TEXT("Roster budget/account invariant failed");}return false;}
    TSet<FGuid> Ids;TSet<ALDCharacter*> Bodies;
    for(const auto& S:Contenders)
    {
        const auto* R=M->Contenders.Find(S.Controller);auto* PC=S.Controller.Get();auto* B=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
        if(!R||R->Session!=S.Id||!S.Id.IsValid()||Ids.Contains(S.Id)||!PC||(R->State.bActive&&(!B||Bodies.Contains(B)))||(!S.Human&&!PC->IsA<ALDB92SeatController>())){Error=TEXT("Contender identity/body/control invariant failed");return false;}
        Ids.Add(S.Id);if(B){Bodies.Add(B);}if(!Economy->Accounting().Account(ALDB92Economy::Wallet(S.Id))||!Economy->Accounting().Account(ALDB92Economy::Bank(S.Id))){Error=TEXT("Seat account missing");return false;}
        if(R->State.Gold!=Economy->Accounting().Account(ALDB92Economy::Wallet(S.Id))->Gold||R->State.BankGold!=Economy->Accounting().Account(ALDB92Economy::Bank(S.Id))->Gold){Error=TEXT("Seat balances differ from persistent accounts");return false;}
    }
    for(const auto& P:Population){auto* B=P.Body.Get();if(!P.Id.IsValid()||Ids.Contains(P.Id)||!B||Bodies.Contains(B)||B->GetIsReplicated()||B->IsA<ALDResident>()){Error=TEXT("Role body/source invariant failed");return false;}Ids.Add(P.Id);Bodies.Add(B);}
    return true;
}
bool ALDB92Roster::RecordGold(FName Cause,int64 Mint,int64 Burn,FString& Error)
{
    if(Ready&&!Economy->SynchronizeWorld(Error)){return false;}
    if(Ready&&Cause==TEXT("PeerTransfer")&&!Mint&&!Burn)
    {
        auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=Cause;
        for(const auto& Seat:Contenders){const auto* O=M->StateFor(Seat.Controller.Get());const FName W=ALDB92Economy::Wallet(Seat.Id);const auto* A=Economy->Ledger.Account(W);if(!O||!A){Error=TEXT("Peer account missing");return false;}if(O->Gold!=A->Gold){T.Gold.Add({W,A->Revision,O->Gold-A->Gold});}}
        return T.Gold.IsEmpty()||Economy->Ledger.Commit(T,Error); // ledger rejects any nonconserved projection atomically
    }
    if(!Ready||Cause!=TEXT("DeathTax")||Mint||Burn<0){Error=TEXT("Unclassified population wallet mutation; reward/service integration is pending");return false;}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=Cause;T.Burn=Burn;
    for(const auto& Seat:Contenders)
    {
        const auto* O=M->StateFor(Seat.Controller.Get());const FName W=ALDB92Economy::Wallet(Seat.Id);const auto* A=Economy->Ledger.Account(W);
        if(!O||!A){Error=TEXT("Death-tax account missing");return false;}
        if(A->Gold!=O->Gold)
        {if(T.Gold.Num()||A->Gold-O->Gold!=Burn||Burn!=LDDevelopment::DeathLoss(A->Gold)){Error=TEXT("Death tax must match exactly one existing wallet at the inherited15% ceiling");return false;}T.Gold.Add({W,A->Revision,-Burn});}
    }
    if(!Burn){return T.Gold.IsEmpty();}return T.Gold.Num()==1&&Economy->Ledger.Commit(T,Error);
}
void ALDB92Roster::Tick(float Delta)
{
    Super::Tick(Delta);if(!Configured||Ending){return;}
    if(!Ready)
    {
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());if(!Nav||Nav->IsNavigationBuildInProgress()||GetWorld()->GetTimeSeconds()-Began<2){return;}
        if(!Initialize(Failure)){UE_LOG(LogTemp,Fatal,TEXT("B92 population initialization rejected: %s"),*Failure);return;}
        const auto Waiting=Pending.Array();Pending.Reset();for(auto Weak:Waiting){if(auto* PC=Weak.Get()){FString Why;if(!Admit(PC,Why)){PC->Destroy();}}}
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&!PC->IsA<ALDB92SeatController>()&&!GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC)){FString Why;if(!Admit(PC,Why)){PC->Destroy();}}}
    }
    for(auto& S:Contenders){if(auto* PC=S.Controller.Get()){S.Body=Cast<ALDCharacter>(PC->GetPawn());}}
    for(auto It=Pending.CreateIterator();It;++It){if(!It->IsValid()){It.RemoveCurrent();}}
    FString Why;if(!Economy->Sample(GetWorld()->GetTimeSeconds()-GetWorld()->GetAuthGameMode<ALDGameMode>()->StartedAt(),Why)){UE_LOG(LogTemp,Fatal,TEXT("B92 institution clock rejected: %s"),*Why);}
}
void ALDB92Roster::EndPlay(const EEndPlayReason::Type Reason)
{
    Ending=true;for(const auto& S:Contenders){if(S.Body.IsValid()){S.Body->Destroy();}if(S.Controller.IsValid()&&S.Controller->IsA<ALDB92SeatController>()){S.Controller->Destroy();}}
    for(const auto& P:Population){if(P.Body.IsValid()){P.Body->Destroy();}}if(Services){Services->Destroy();}if(Economy){Economy->Destroy();}Super::EndPlay(Reason);
}
