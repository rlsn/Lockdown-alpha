#include "Economy/LDB92Deposits.h"
#include "Economy/LDB92Economy.h"
#include "Economy/LDProductionDirector.h"
#include "Population/LDB92Roster.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "World/LDWorldObjects.h"
#include "Region/LDMapDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

ALDB92Deposits::ALDB92Deposits(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
const TArray<FName>& ALDB92Deposits::Nodes()
{static const TArray<FName> Ids={TEXT("crystal_north"),TEXT("crystal_west"),TEXT("crystal_center"),TEXT("crystal_east"),TEXT("crystal_south"),TEXT("ore_north"),TEXT("ore_east"),TEXT("fiber_west"),TEXT("fiber_south")};return Ids;}
FName ALDB92Deposits::Budget(int32 Node){return Nodes().IsValidIndex(Node)?FName(*(TEXT("B92Deposit:")+Nodes()[Node].ToString())):NAME_None;}
ELDItemKind ALDB92Deposits::Kind(int32 Node){return Node<5?ELDItemKind::Crystal:Node<7?ELDItemKind::Ore:ELDItemKind::Fiber;}
FVector ALDB92Deposits::Position(const UWorld* W,int32 Node)
{const auto* M=ULDMapDefinition::Get(W);const auto* P=M&&Nodes().IsValidIndex(Node)?M->Find(Nodes()[Node]):nullptr;return P?P->Position:FVector::ZeroVector;}
int32 ALDB92Deposits::Nearby(ALDB92Controller* PC) const
{const APawn* B=PC?PC->GetPawn():nullptr;if(!B){return INDEX_NONE;}for(int32 I=0;I<Nodes().Num();++I){const auto P=Position(GetWorld(),I);if(FVector::DistSquared2D(B->GetActorLocation(),P)<=120*120&&FMath::Abs(B->GetActorLocation().Z-P.Z-90)<100){return I;}}return INDEX_NONE;}
bool ALDB92Deposits::Eligible(ALDB92Controller* PC,int32 Node,bool SkillChannel) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;auto* B=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=M&&M->Combat?M->Combat->StateFor(B):nullptr;auto* Env=ALDEnvironmentDirector::Get(GetWorld());const double Now=GetWorld()->GetTimeSeconds();FString Why;
    return Node>=0&&Nearby(PC)==Node&&M&&M->Population&&M->Population->Ready&&M->IsPlaying()&&Now<M->GetGameState<ALDGameState>()->MatchDeadline&&O&&O->bActive&&B&&S&&S->Hp>0&&Now>=O->ProtectedUntil&&Now>=S->ProtectionUntil&&Now-S->LastDamage>=6&&Now-S->LastDealt>=6&&(!S->Action||SkillChannel&&S->Action==7&&S->Skill==ELDSkill::Survey)&&!O->DevelopmentKind&&B->GetVelocity().SizeSquared2D()<1&&B->GetCharacterMovement()->IsMovingOnGround()&&M->Combat->Allows(B,LDTactical::EAction::Service)&&!ALDWorldObjects::Get(GetWorld())->HasInteraction(PC)&&M->Combat->QuietForPreparation(B)&&!ALDWorldObjects::Get(GetWorld())->Carried(PC)&&M->Population->Economy->Accounting().Remaining(Budget(Node))>0&&LDInventory::Fits(O->Inventory,O->Build,{}, {Kind(Node)},&Why)&&Env&&Env->Ray(B->GetActorLocation(),Position(GetWorld(),Node)+FVector(0,0,90),nullptr,Env->SightFor(PC));
}
void ALDB92Deposits::Progress(ALDB92Controller* PC,FState& S,bool On)
{if(!PC){return;}FLDB91Channel P;P.Match=PC->OwnerState.Match;P.Life=S.Life;P.Domain=4;P.Sequence=S.Sequence;P.Ends=S.Ends;P.Started=S.Ends-6;P.Label=TEXT("Extract ")+LDInventory::Name(Kind(S.Node),ELDSkill::None)+TEXT(" (automatic POI)");PC->ClientB91Channel(P,On);}
bool ALDB92Deposits::Active(ALDB92Controller* PC) const
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* S=M&&PC?States.Find(M->ContenderId(PC)):nullptr;return S&&S->Node!=INDEX_NONE;}
void ALDB92Deposits::Stop(ALDB92Controller* PC)
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!PC||!M->StateFor(PC)){return;}auto& S=States.FindOrAdd(M->ContenderId(PC));if(S.Node!=INDEX_NONE){Progress(PC,S,false);PC->ClientIntentResult(0,TEXT("Extraction stopped; leave the deposit to restart."));}S.Node=INDEX_NONE;S.Blocked=Nearby(PC);}
void ALDB92Deposits::Tick(float Delta)
{
    Super::Tick(Delta);const double Now=GetWorld()->GetTimeSeconds();if(Now<Next){return;}Next=Now+.1;auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->Population||!M->Population->Ready){return;}
    for(auto It=SurveyChannels.CreateIterator();It;++It){auto* PC=It.Key().Get();const auto* C=PC?M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn())):nullptr;if(!PC||!C||C->Action!=7||C->SkillToken!=It.Value()||C->Skill!=ELDSkill::Survey){if(PC){FLDB91Channel P;P.Match=PC->OwnerState.Match;P.Life=PC->OwnerState.Life;P.Sequence=It.Value();P.Domain=5;PC->ClientB91Channel(P,false);}It.RemoveCurrent();}}
    for(const auto& Seat:M->Population->Seats())
    {
        auto* PC=Cast<ALDB92Controller>(Seat.Controller.Get());if(!PC){continue;}auto& S=States.FindOrAdd(Seat.Id);const int32 Node=Nearby(PC);if(Node!=S.Blocked){S.Blocked=INDEX_NONE;}
        const auto* O=M->StateFor(PC);auto* B=Cast<ALDCharacter>(PC->GetPawn());const auto* C=M->Combat->StateFor(B);
        if(S.Node!=INDEX_NONE)
        {
            if(!O||!C||O->Life!=S.Life||C->Identity!=S.Body||S.Node!=Node||FVector::DistSquared2D(B->GetActorLocation(),S.Anchor)>4||!Eligible(PC,S.Node)){Stop(PC);continue;}
            if(Now<S.Ends){continue;}FString Why;const int32 Finished=S.Node;Progress(PC,S,false);S.Node=INDEX_NONE;
            if(!M->Population->Economy->ExtractDeposit(PC,Finished,Why)){S.Blocked=Node;PC->ClientIntentResult(0,Why);continue;}if(M->Population->Economy->Accounting().Remaining(Budget(Finished))==0){PC->ClientIntentResult(0,TEXT("Deposit depleted."));}
        }
        if(Node==INDEX_NONE||S.Blocked==Node||!Eligible(PC,Node)||NextSequence==MAX_uint32){continue;}
        S.Node=Node;S.Life=O->Life;S.Body=C->Identity;S.Anchor=B->GetActorLocation();S.Ends=Now+6;S.Sequence=++NextSequence;Progress(PC,S,true);
    }
}

void ALDB92Deposits::BeginPlay()
{
    Super::BeginPlay();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!HasAuthority()||!M||!M->Combat){return;}
    for(int32 Node=0;Node<5;++Node){auto* Target=GetWorld()->SpawnActor<ALDProductionSite>();if(!Target){UE_LOG(LogTemp,Fatal,TEXT("B92 Survey target allocation failed"));return;}Target->SetReplicates(false);Target->SetActorLocation(Position(GetWorld(),Node)+FVector(0,0,90));SurveyTargets.Add(Target);M->Combat->RegisterSkillTask(Target,{ELDSkill::Survey},[this,Node](ALDCharacter* B,ELDSkill Skill,const FLDCharacterBuild& Build,uint32 Token,ELDSkillTaskPhase Phase,FString& Error){return Skill==ELDSkill::Survey&&SurveyTask(B,Node,Build,Token,Phase,Error);});}
}
void ALDB92Deposits::EndPlay(const EEndPlayReason::Type Reason)
{for(auto T:SurveyTargets){if(IsValid(T)){T->Destroy();}}Super::EndPlay(Reason);}
bool ALDB92Deposits::SurveyTask(ALDCharacter* Body,int32 Node,const FLDCharacterBuild& Build,uint32 Token,ELDSkillTaskPhase Phase,FString& Error)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Body?Cast<ALDB92Controller>(Body->GetController()):nullptr;const auto* O=M?M->StateFor(PC):nullptr;const auto* C=M&&M->Combat?M->Combat->StateFor(Body):nullptr;
    if(!PC||!O||!C||!Token||Node<0||Node>=5||!SurveyTargets.IsValidIndex(Node)||!LDDevelopment::Equipped(Build,ELDSkill::Survey)||!LDDevelopment::Equipped(O->Build,ELDSkill::Survey)||O->Build.Revision!=Build.Revision||LDDevelopment::SkillValue(Build,ELDSkill::Survey)<12||LDDevelopment::SkillValue(O->Build,ELDSkill::Survey)<12||!Eligible(PC,Node,true))
    {Error=TEXT("Survey needs prepared learned score12, quiet sight/reach, capacity and a live Crystal charge");return false;}
    if(Phase==ELDSkillTaskPhase::Accept)
    {Stop(PC);SurveyChannels.Add(PC,Token);FLDB91Channel P;P.Match=O->Match;P.Life=O->Life;P.Sequence=Token;P.Domain=5;P.Started=GetWorld()->GetTimeSeconds();P.Ends=P.Started+LDDevelopment::ChannelSeconds(ELDSkill::Survey);P.Label=TEXT("Survey Crystal (prepared ability)");PC->ClientB91Channel(P,true);return true;}
    if(Phase!=ELDSkillTaskPhase::Commit){return true;}
    if(C->Action!=7||C->SkillToken!=Token||C->Target!=SurveyTargets[Node]){Error=TEXT("Survey channel is no longer current");return false;}
    if(!M->Population->Economy->ExtractDeposit(PC,Node,Error,true)){return false;}Error=TEXT("Survey complete; one finite Crystal, no gold or XP");return true;
}
