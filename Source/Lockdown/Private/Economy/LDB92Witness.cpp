#include "Economy/LDB92Witness.h"
#include "Economy/LDB92Economy.h"
#include "Population/LDB92Roster.h"
#include "Combat/LDCombatDirector.h"
#include "World/LDWorldObjects.h"
#include "Core/LDGroundQueries.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

ALDB92Witness::ALDB92Witness(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
const TArray<FName>& ALDB92Witness::Guilds()
{static const TArray<FName> Values={TEXT("capital_guild"),TEXT("riverport_guild")};return Values;}
bool ALDB92Witness::Profession(ELDSkill Skill)
{for(uint8 I=0;I<4;++I){if(Skill==LDCharter::Skill(I)){return true;}}return false;}
FVector ALDB92Witness::Position(const UWorld* W,int32 Guild,uint8 Task)
{
    if(!Guilds().IsValidIndex(Guild)||Task>=4){return FVector::ZeroVector;}
    const FVector Offsets[]={FVector(-140,0,0),FVector(0,-140,0),FVector(140,0,0),FVector(0,140,0)};
    return ALDB92Services::Counter(W,Guilds()[Guild])+Offsets[Task];
}
void ALDB92Witness::BeginPlay()
{
    Super::BeginPlay();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!HasAuthority()||!M||!M->Combat){return;}
    for(int32 G=0;G<Guilds().Num();++G){for(uint8 I=0;I<4;++I){auto* T=GetWorld()->SpawnActor<ALDCharterTask>();check(T);T->Service=I;T->SetActorLocation(Position(GetWorld(),G,I)+FVector(0,0,90));const int32 Index=Targets.Add(T);M->Combat->RegisterSkillTask(T,{LDCharter::Skill(I)},[this,Index](ALDCharacter* B,ELDSkill K,const FLDCharacterBuild& Build,uint32 Token,ELDSkillTaskPhase Phase,FString& Error){return Execute(B,Index,Build,K,Token,Phase,Error);});}}
}
void ALDB92Witness::EndPlay(const EEndPlayReason::Type Reason)
{for(auto T:Targets){if(IsValid(T)){T->Destroy();}}Super::EndPlay(Reason);}
void ALDB92Witness::Tick(float Delta)
{
    Super::Tick(Delta);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->Combat){return;}
    for(auto It=Channels.CreateIterator();It;++It){auto* PC=It.Key().Get();const auto* C=PC?M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn())):nullptr;if(!PC||!C||C->Action!=7||C->SkillToken!=It.Value()){if(PC){FLDB91Channel P;P.Match=PC->OwnerState.Match;P.Life=PC->OwnerState.Life;P.Domain=7;P.Sequence=It.Value();PC->ClientB91Channel(P,false);}It.RemoveCurrent();}}
}
bool ALDB92Witness::Execute(ALDCharacter* B,int32 Index,const FLDCharacterBuild& Build,ELDSkill Skill,uint32 Token,ELDSkillTaskPhase Phase,FString& Error)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=B?Cast<ALDB92Controller>(B->GetController()):nullptr;auto* O=M?M->StateFor(PC):nullptr;const auto* C=M&&M->Combat?M->Combat->StateFor(B):nullptr;const double Now=GetWorld()->GetTimeSeconds();const auto* Env=ALDEnvironmentDirector::Get(GetWorld());
    if(!Targets.IsValidIndex(Index)||!PC||!O||!C||!M->Population||!M->Population->Ready||!M->IsPlaying()||Now>=M->GetGameState<ALDGameState>()->MatchDeadline||!O->bActive||C->Hp<=0||Now<O->ProtectedUntil||Now<C->ProtectionUntil||!Token||Skill!=LDCharter::Skill(Index%4)||!LDDevelopment::Equipped(Build,Skill)||!LDDevelopment::Equipped(O->Build,Skill)||Build.Revision!=O->Build.Revision||LDDevelopment::SkillValue(Build,Skill)<12||LDDevelopment::SkillValue(O->Build,Skill)<12||!M->Combat->Allows(B,LDTactical::EAction::Skill)||O->DevelopmentKind||B->GetVelocity().SizeSquared2D()>1||!B->GetCharacterMovement()->IsMovingOnGround()||ALDWorldObjects::Get(GetWorld())->HasInteraction(PC)||FVector::DistSquared2D(B->GetActorLocation(),Targets[Index]->GetActorLocation())>120*120||!LDGround::ClearSegment(GetWorld(),B->GetActorLocation(),Targets[Index]->GetActorLocation())||!Env||!Env->Ray(B->GetActorLocation(),Targets[Index]->GetActorLocation(),nullptr,Env->SightFor(PC)))
    {Error=TEXT("Witness needs the matching prepared learned skill at score12 and a visible reachable guild task");return false;}
    if(O->Witness.IsValid()){Error=TEXT("Witness already recorded; no duplicate reward or receipt");return false;}
    if(Phase==ELDSkillTaskPhase::Accept){Channels.Add(PC,Token);FLDB91Channel P;P.Match=O->Match;P.Life=O->Life;P.Domain=7;P.Sequence=Token;P.Started=Now;P.Ends=Now+LDDevelopment::ChannelSeconds(Skill);P.Label=FString(TEXT("Witness: "))+LDCharter::Name(Index%4);PC->ClientB91Channel(P,true);return true;}
    if(Phase!=ELDSkillTaskPhase::Commit){return true;}
    if(C->Action!=7||C->SkillToken!=Token||C->Target!=Targets[Index]){Error=TEXT("Witness channel is no longer current");return false;}
    FGuid Receipt;if(!M->Population->Economy->RecordWitness(PC,Receipt,Error)){return false;}Error=TEXT("Witness recorded once; no gold, XP or material granted");return true;
}
bool ALDB92Economy::RecordWitness(ALDB92Controller* PC,FGuid& Receipt,FString& Error)
{
    Receipt.Invalidate();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(PC):nullptr;
    if(!HasAuthority()||!Ready||!FullWorld||Closed||!O||!O->bActive||!Seats.Contains(M->ContenderId(PC))||O->Witness.IsValid()){Error=TEXT("Witness unavailable or already recorded");return false;}
    LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("B92CharterWitness");if(!Ledger.Commit(T,Error)){return false;}O->Witness=T.Id;Receipt=T.Id;M->SyncOwner(PC);return true;
}
