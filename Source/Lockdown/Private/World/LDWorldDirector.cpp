#include "World/LDWorldDirector.h"
#include "LDWorldContract.h"
#include "Combat/LDTacticalCatalog.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Vision/LDVisionDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Core/LDGroundQueries.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
namespace {
FString WorldHash(const FString& S){FTCHARToUTF8 B(*S);FSHA256Signature H;SHA256(reinterpret_cast<const uint8*>(B.Get()),B.Length(),H.Signature);return H.ToString().ToLower();}
TSharedPtr<FJsonObject> WorldData(){TSharedPtr<FJsonObject> D;const auto* C=ULDWorldCatalog::Load();if(C){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(C->SourceJson),D);}return D;}
FName WorldSpawnBudget(uint8 K,uint8 I,int32 Ordinal){return FName(*(K==0?FString::Printf(TEXT("B76Camp%d.%d"),I,Ordinal):K==1?TEXT("B76GuardianDefeat"):FString::Printf(TEXT("B76Tower%d"),I)));}
}
bool ULDWorldCatalog::Import(const FString& Text,FString& Error){SourceJson=Text.Replace(TEXT("\r\n"),TEXT("\n"));Checksum=WorldHash(SourceJson);if(Checksum!=LD_WORLD_SHA){Error=TEXT("World contract differs from adopted rules/layout");return false;}return true;}
bool ULDWorldCatalog::Validate(FString& Error) const {if(Checksum!=LD_WORLD_SHA||WorldHash(SourceJson)!=Checksum){Error=TEXT("Stale world catalog");return false;}return true;}
ULDWorldCatalog* ULDWorldCatalog::Load(){return LoadObject<ULDWorldCatalog>(nullptr,TEXT("/Game/Lockdown/Generated/V07/DA_WorldCatalog.DA_WorldCatalog"));}
FVector ULDWorldCatalog::Position(const FString& Name) const {TSharedPtr<FJsonObject> D;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJson),D)){return {};}const auto& A=D->GetObjectField(TEXT("layout"))->GetArrayField(Name);return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());}
FVector ALDWorldDirector::Position(const FString& Name) const{return ULDWorldCatalog::Load()->Position(Name);}
ALDWorldPawn::ALDWorldPawn(const FObjectInitializer& Initializer):Super(Initializer){bReplicates=false;AutoPossessAI=EAutoPossessAI::PlacedInWorldOrSpawned;AIControllerClass=AAIController::StaticClass();}
ALDWorldPawn* ALDWorldDirector::Spawn(uint8 K,uint8 Index,double At)
{
    const auto D=WorldData()->GetObjectField(TEXT("world"))->GetObjectField(K==0?TEXT("camp"):K==1?TEXT("boss"):TEXT("tower"));const FString Name=K==0?FString::Printf(TEXT("Camp%c"),'A'+Index):K==1?TEXT("BossLair"):FString::Printf(TEXT("GrandTower%c"),'A'+Index);FVector Ground;if(!LDGround::MapSurface(GetWorld(),Position(Name),Ground)){return nullptr;}
    auto* P=GetWorld()->SpawnActor<ALDWorldPawn>(Ground+FVector(0,0,92),FRotator::ZeroRotator);if(!P){return nullptr;}if(auto* AI=Cast<AAIController>(P->GetController())){AI->SetReplicates(false);}P->Kind=K;P->Index=Index;if(K==0){P->SpawnId=CampIds[Index][CampCursor[Index]];}P->Home=P->GetActorLocation();P->Maximum=D->GetNumberField(TEXT("hp"));P->Armor=D->GetNumberField(TEXT("armor"));P->MR=K==0?.15:D->GetNumberField(TEXT("magic_resist"));P->AttackDamage=D->GetNumberField(TEXT("attack_damage"));P->Interval=D->GetNumberField(TEXT("attack_interval_ms"))/1000.;P->NextAttack=At+P->Interval;P->NextPulse=K==1?At+10:0;P->GetCharacterMovement()->MaxWalkSpeed=260;auto* C=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get();C->StateFor(P)->Hp=P->Maximum;
    // Authored v08 camp combatants have a declared level-one spell kit, no inventory or wallet grant.
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(K==0&&M->IsV08Profile())
    {
        const auto B=LDDevelopment::TacticalStart(5,M->TacticalCatalog->Checksum,8);
        if(C->RegisterNPCBuild(P,B)){C->StateFor(P)->Tactical.MP=LDTactical::MaxMP(B);}
    }
    Pawns.Add(P);return P;
}
void ALDWorldDirector::Initialize()
{
    if(bReady){return;}FString Why;const auto* C=ULDWorldCatalog::Load();check(C&&C->Validate(Why));bReady=true;auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=M->GetGameState<ALDGameState>();G->DefensiveFlags=7;const double At=M->StartedAt();
    for(uint8 N=0;N<3;++N){for(int32 Id=0;Id<12;++Id){CampIds[N][Id]=FGuid::NewGuid();}CampNext[N]=At+150;Spawn(2,N,At);if(Spawn(0,N,At)){++CampCursor[N];}else{CampNext[N]=At;}}Spawn(1,0,At);
    for(uint8 N=0;N<3;++N){auto* S=GetWorld()->SpawnActor<ALDWorldSite>(Position(N==0?TEXT("HeartAntechamber"):N==1?TEXT("ShrineWest"):TEXT("ShrineEast"))+FVector(0,0,15),FRotator::ZeroRotator);S->Kind=N;}
}
TArray<FLDSightSource> ALDWorldDirector::TowerSight() const
{TArray<FLDSightSource> Out;for(auto& Weak:Pawns){auto* P=Weak.Get();if(P&&P->Kind==2&&!P->Paid){Out.Add({P->GetActorLocation(),600,1});}}return Out;}
void ALDWorldDirector::Prepare(double At,TArray<FLDCombatHit>& Hits)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!bReady||!M->IsPlaying()||At>M->GetGameState<ALDGameState>()->MatchDeadline){return;}auto* C=M->Combat.Get();
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());auto* O=M->StateFor(PC);auto* B=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=C->StateFor(B);if(O&&S){const FGuid Id=M->ContenderId(PC);LifeOwners.Add(S->Identity,Id);Owners.Add(Id,PC);
        if(!M->bDrainingEvents){auto& T=Traversal.FindOrAdd(Id);const FVector Here=B->GetActorLocation();const double Dt=At-T.At;const bool Eligible=O->bActive&&S->Hp>0&&At>=O->ProtectedUntil&&At>=S->ProtectionUntil&&B->GetCharacterMovement()->IsMovingOnGround();if(Eligible&&T.Eligible&&T.Life==O->Life&&Dt>0&&Dt<=.25&&FVector::DistSquared2D(Here,T.Position)>.01&&FVector::Dist2D(Here,T.Position)<=B->GetCharacterMovement()->GetMaxSpeed()*Dt+30&&B->GetVelocity().SizeSquared2D()>1){for(uint8 Ruin=0;Ruin<3;++Ruin){const FVector Point=Position(FString::Printf(TEXT("Ruin%c"),'A'+Ruin));if(!Discoveries.FindOrAdd(Id).Contains(Ruin)&&FVector::DistSquared2D(T.Position,Point)>180*180&&FVector::DistSquared2D(Here,Point)<=180*180){Discoveries.FindOrAdd(Id).Add(Ruin);M->AwardExperience(PC,50);UE_LOG(LogTemp,Display,TEXT("LD_B76_DISCOVERY owner=%s ruin=%u xp=50"),*Id.ToString(),Ruin);}}}T={Here,At,O->Life,Eligible};}
    }}
    for(uint8 N=0;N<3;++N){bool Living=false;for(const auto& P:Pawns){Living|=P.IsValid()&&P->Kind==0&&P->Index==N&&!P->Paid;}if(!Living&&CampCursor[N]<12&&At>=CampNext[N]){if(Spawn(0,N,At)){++CampCursor[N];CampNext[N]=At+150;}}}
    for(const auto& Weak:Pawns)
    {
        auto* P=Weak.Get();auto* S=C->StateFor(P);if(!P||!S||P->Paid){continue;}if(S->Hp<=0){if(Defeated(P,At)){C->EndContentLife(P);P->Destroy();}continue;}ALDCharacter* Target=nullptr;double Best=DBL_MAX;FGuid BestId;
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());auto* B=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* O=M->StateFor(PC);const auto* T=C->StateFor(B);if(M->bDrainingEvents&&P->Target.Get()!=B){continue;}if(!B||!O||!O->bActive||!T||T->Hp<=0||At<T->ProtectionUntil||At<O->ProtectedUntil||C->Concealed(B)){continue;}const double D=FVector::DistSquared2D(P->GetActorLocation(),B->GetActorLocation());const double Range=S->Tactical.Has(LDTactical::EStatus::Blind,LDTactical::Milliseconds(At))?200:P->Kind==2?650:1000;if(D>Range*Range||FVector::DistSquared2D(P->Home,B->GetActorLocation())>Range*Range||!M->Vision->Ray({P->GetActorLocation(),float(Range),M->Vision->LevelAt(P->GetActorLocation())},B->GetActorLocation())){continue;}if(D<Best||(D==Best&&T->Identity.ToString()<BestId.ToString())){Target=B;Best=D;BestId=T->Identity;}}
        if(!M->bDrainingEvents){P->Target=Target;}auto* AI=Cast<AAIController>(P->GetController());if(!Target||FVector::DistSquared2D(P->Home,P->GetActorLocation())>1000*1000){if(P->Kind!=2){S->Hp=P->Maximum;if(AI&&!M->bDrainingEvents){AI->MoveToLocation(P->Home,20);}}P->PulseAt=0;continue;}
        P->SetFacing((Target->GetActorLocation()-P->GetActorLocation()).Rotation().Yaw);
        // Decide only on a currently visible body and its public Wet cue. The ordinary
        // request boundary rechecks range, gates, MP, Silence and absolute cooldown.
        if(M->IsV08Profile()&&P->Kind==0&&!M->bDrainingEvents&&S->bHasNPCBuild&&!S->Action&&Best<=650*650&&!C->EnvironmentOccludes(P->GetActorLocation(),Target->GetActorLocation()))
        {
            const auto* Visible=C->StateFor(Target);
            if(Visible&&Visible->Tactical.Has(LDTactical::EStatus::Wet,LDTactical::Milliseconds(At)))
            {
                FLDTacticalRequest R;R.Schema=8;R.WireVersion=4;R.Token=S->LastCoreToken+1;R.BuildRevision=S->NPCBuild.Revision;R.Slot=uint8(S->NPCBuild.Slots.Find(ELDSkill::Frostline));R.Point=Target->GetActorLocation();R.Direction=(FVector(R.Point)-P->GetActorLocation()).GetSafeNormal2D();FString Why;
                C->RequestContent(P,R,nullptr,nullptr,Why);
            }
        }
        if(S->Action&&P->Kind==0&&M->IsV08Profile()){if(AI&&!M->bDrainingEvents){AI->StopMovement();}continue;}
        if(P->Kind!=2&&AI&&!M->bDrainingEvents){if(Best>200*200){AI->MoveToActor(Target,90);}else{AI->StopMovement();}}
        if(P->Kind==1&&P->PulseAt>0&&At>=P->PulseAt){for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* B=Cast<ALDCharacter>(It->Get()->GetPawn());const auto* T=C->StateFor(B);if(B&&T&&T->Hp>0&&At>=T->ProtectionUntil&&FVector::DistSquared2D(B->GetActorLocation(),P->PulsePoint)<=400*400&&LDGround::ClearSegment(GetWorld(),P->PulsePoint,B->GetActorLocation())){Hits.Add({P,B,120,LDTactical::EDamage::Magical,false});}}P->PulseAt=0;}
        if(P->Kind==1&&At>=P->NextPulse){P->PulsePoint=P->GetActorLocation();P->PulseAt=At+1.5;P->NextPulse=At+10;if(!C->CreateField(P,ELDTacticalField::GuardianPulse,P->PulsePoint,LDTactical::Milliseconds(At),1500,1,400)){P->PulseAt=0;}}
        if(!C->Allows(P,LDTactical::EAction::Attack)||At<P->NextAttack||Best>FMath::Square(P->Kind==2?650:220)){continue;}P->NextAttack=At+P->Interval;
        if(P->Kind==2){auto* Projectile=GetWorld()->SpawnActor<ALDArcProjectile>(P->GetActorLocation(),FRotator::ZeroRotator);if(Projectile){Projectile->Source=P;Projectile->CasterIdentity=S->Identity;Projectile->Victim=Target;Projectile->VictimLife=C->StateFor(Target)->Identity;Projectile->Endpoint=Target->GetActorLocation();Projectile->Amount=P->AttackDamage;Projectile->Basic=true;Projectile->AdvancedAt=LDTactical::Milliseconds(At);}}
        else{Hits.Add({P,Target,P->AttackDamage,LDTactical::EDamage::Physical,false});}
    }
}
void ALDWorldDirector::Contact(const FLDCombatHit& Hit,double Amount)
{
    if(Amount<=0){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* S=M->Combat->StateFor(Hit.Source);FGuid OwnerId=Hit.CreditOwner.IsValid()?Hit.CreditOwner:LifeOwners.FindRef(Hit.SourceIdentity.IsValid()?Hit.SourceIdentity:S?S->Identity:FGuid());if(!Hit.CreditOwner.IsValid()){if(auto* PC=Hit.Source?Cast<ALDPlayerController>(Hit.Source->GetController()):nullptr){OwnerId=M->ContenderId(PC);}}if(!OwnerId.IsValid()||(Hit.CreditOwner.IsValid()&&!Owners.Contains(OwnerId))){if(Hit.CreditOwner.IsValid()){if(auto* P=Cast<ALDWorldPawn>(Hit.Target)){P->LastOwner.Invalidate();P->LastHitter.Reset();}else if(const auto* T=M->Combat->StateFor(Cast<ALDCharacter>(Hit.Target))){LastDamageOwner.Remove(T->Identity);}}return;}if(auto* P=Cast<ALDWorldPawn>(Hit.Target)){P->LastOwner=OwnerId;P->LastHitter=Owners.FindRef(OwnerId);}else if(const auto* T=M->Combat->StateFor(Cast<ALDCharacter>(Hit.Target))){LastDamageOwner.Add(T->Identity,OwnerId);}
}
bool ALDWorldDirector::Reward(ALDWorldPawn* P,int32 Gold,int32 XP,FName Cause,FGuid& Receipt)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* E=M->Economy.Get();LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=Cause;T.Budget=WorldSpawnBudget(P->Kind,P->Index,P->Kind==0?CampCursor[P->Index]:0);T.Units=1;auto* PC=Owners.FindRef(P->LastOwner).Get();auto* O=PC?M->StateFor(PC):nullptr;FName Account=O?E->Wallet(P->LastOwner):FName(*(TEXT("WorldReward:")+P->LastOwner.ToString(EGuidFormats::Digits)));
    if(P->LastOwner.IsValid()){const auto* A=E->Ledger.Account(Account);if(!A){return false;}T.Mint=Gold;T.Gold.Add({Account,A->Revision,Gold});}
    TArray<ALDItem*> Drops;if(P->Kind!=0){const int32 Count=P->Kind==1?2:1;for(int32 N=0;N<Count;++N){auto* Item=P->Kind==1&&N==1?static_cast<ALDItem*>(GetWorld()->SpawnActor<ALDBook>()):GetWorld()->SpawnActor<ALDItem>();if(!Item){for(auto* D:Drops){D->Destroy();}return false;}Item->Kind=P->Kind==2?ELDItemKind::TowerRelic:N==0?ELDItemKind::HeartFragment:ELDItemKind::Book;Item->Skill=P->Kind==1&&N==1?ELDSkill::SovereignWard:ELDSkill::None;Item->State=0;Item->OriginReceipt=T.Id;FVector Floor;if(!LDGround::MapSurface(GetWorld(),P->GetActorLocation()+FVector(50*N,0,0),Floor)&&!LDGround::MapSurface(GetWorld(),P->Home+FVector(50*N,0,0),Floor)){Item->Destroy();for(auto* Drop:Drops){Drop->Destroy();}return false;}Item->SetActorLocation(Floor+FVector(0,0,15));Drops.Add(Item);LDEconomy::FItemDelta D;D.Id=Item->Identity;D.Kind=Item->Kind;D.Skill=uint8(Item->Skill);D.Op=LDEconomy::EItemOp::Create;D.To=TEXT("World");T.Items.Add(D);}if(P->Kind==1){T.Budgets.Add({TEXT("B74Guardian"),1});}}
    FString Why;if(!E->Ledger.Commit(T,Why)){for(auto* D:Drops){D->Destroy();}UE_LOG(LogTemp,Error,TEXT("LD_B76_REWARD_FAILED %s"),*Why);return false;}Receipt=T.Id;if(P->LastOwner.IsValid()){AttributedXP.FindOrAdd(P->LastOwner)+=XP;}if(O){O->Gold=E->Ledger.Account(Account)->Gold;M->AwardExperience(PC,XP);M->SyncOwner(PC);}UE_LOG(LogTemp,Display,TEXT("LD_B76_DEFEAT spawn=%s kind=%u index=%u receipt=%s owner=%s gold=%d xp=%d"),*P->SpawnId.ToString(),P->Kind,P->Index,*T.Id.ToString(),*P->LastOwner.ToString(),P->LastOwner.IsValid()?Gold:0,P->LastOwner.IsValid()?XP:0);return E->Audit(Why);
}
bool ALDWorldDirector::Defeated(ALDWorldPawn* P,double At)
{if(!P||P->Paid){return false;}FGuid R;if(!Reward(P,P->Kind==0?12:P->Kind==1?90:40,P->Kind==0?90:P->Kind==1?250:150,P->Kind==0?TEXT("CampDefeat"):P->Kind==1?TEXT("GuardianDefeat"):TEXT("GrandTowerDefeat"),R)){return false;}P->Paid=true;if(P->Kind==2){auto* G=GetWorld()->GetGameState<ALDGameState>();G->DefensiveFlags&=~(1<<P->Index);Publish(At);}return true;}
void ALDWorldDirector::Publish(double At)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=M->GetGameState<ALDGameState>();if(G->CharterWindow.IsValid()&&!CharterHistory.ContainsByPredicate([&](const auto& W){return W.Id==G->CharterWindow;})){CharterHistory.Add({G->CharterWindow,G->CharterOpenAt,G->CharterCloseAt});}G->Breaches.RemoveAll([&](const auto& B){return At>B.CloseAt;});for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC){InventoryChanged(PC);}}
    for(TActorIterator<AStaticMeshActor> It(GetWorld());It;++It){for(uint8 N=0;N<3;++N){if(!It->IsHidden()&&!(G->DefensiveFlags&(1<<N))&&It->ActorHasTag(FName(*FString::Printf(TEXT("TowerDoor%c"),'A'+N)))){It->SetActorHiddenInGame(true);It->SetActorEnableCollision(false);FNavigationSystem::UpdateComponentData(*It->GetStaticMeshComponent());}}}
    for(auto It=Leases.CreateIterator();It;++It){if(At>It.Value().Close){It.RemoveCurrent();}}
}
void ALDWorldDirector::PvPKill(ALDCharacter* Victim,double At)
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* S=M->Combat->StateFor(Victim);auto* V=Cast<ALDPlayerController>(Victim->GetController());if(!S||!V){return;}const FGuid Killer=LastDamageOwner.FindRef(S->Identity),Target=M->ContenderId(V);auto* PC=Owners.FindRef(Killer).Get();if(!Killer.IsValid()||Killer==Target||!PC||!M->StateFor(PC)){return;}const FString Pair=Killer.ToString()+Target.ToString();if(PvPReady.FindRef(Pair)>At||PvPEarned.FindRef(Killer)>=300){return;}PvPReady.Add(Pair,At+180);PvPEarned.FindOrAdd(Killer)+=50;M->AwardExperience(PC,50);}
double ALDWorldDirector::NextEvent(double After,double Through) const
{double Next=Through;auto Due=[&](double T){if(T>After+1.e-7&&T<Next){Next=T;}};for(int32 N=0;N<3;++N){if(CampCursor[N]<12){Due(CampNext[N]);}}for(const auto& W:Pawns){const auto* P=W.Get();if(P&&!P->Paid){Due(P->NextAttack);if(P->Kind==1){Due(P->NextPulse);Due(P->PulseAt);}}}return Next;}
