#include "Development/LDDevelopmentDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool LDDevelopmentTransaction::Sold(ELDSkill Skill)
{return Skill==ELDSkill::Step||Skill==ELDSkill::Spark||Skill==ELDSkill::Patch||Skill==ELDSkill::Parley;}
ALDBook::ALDBook(){bReplicates=false;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
ALDDevelopmentDirector::ALDDevelopmentDirector(){bReplicates=false;}
ALDBook* ALDDevelopmentDirector::OwnedBook(ALDPlayerController* PC,const FGuid& Id) const
{for(TActorIterator<ALDBook> It(GetWorld());It;++It){if(It->State==1&&It->Holder==PC&&It->Identity==Id){return *It;}}return nullptr;}
bool ALDDevelopmentDirector::NearStation(ALDPlayerController* PC,bool GuildOnly) const
{
    const APawn* Body=PC?PC->GetPawn():nullptr;if(!Body){return false;}
    const FVector From=Body->GetActorLocation();
    for(uint8 Site:{LDObjective::Guild,LDObjective::Station})
    {if(GuildOnly&&Site!=LDObjective::Guild){continue;}const FVector To=LDObjective::Position(Site);if(FVector::DistSquared2D(From,To)<=120*120&&LDGround::ClearSegment(GetWorld(),From,To)){return true;}}
    return false;
}
bool ALDDevelopmentDirector::Stationary(ALDPlayerController* PC) const
{
    const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    return Body&&Body->GetVelocity().SizeSquared2D()<=1&&Body->GetCharacterMovement()->GetCurrentAcceleration().SizeSquared2D()<=1&&Body->GetLastMovementInputVector().IsNearlyZero();
}
bool ALDDevelopmentDirector::Eligible(ALDPlayerController* PC,double Now,bool Quiet) const
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode->StateFor(PC);
    const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=Mode->Combat->StateFor(Body);
    return Mode->IsDevelopmentProfile()&&Mode->IsPlaying()&&O&&O->bActive&&Body&&S&&S->Hp>0&&Now>=S->ProtectionUntil
        &&(!Quiet||Now-FMath::Max(S->LastDamage,S->LastDealt)>=6);
}
void ALDDevelopmentDirector::FinishRequest(ALDPlayerController* PC,uint32 Id,const FString& Reason)
{
    auto& S=States.FindOrAdd(PC);S.Receipts.Add(Id,Reason);
    while(S.Receipts.Num()>32){uint32 Oldest=MAX_uint32;for(const auto& Pair:S.Receipts){Oldest=FMath::Min(Oldest,Pair.Key);}S.Receipts.Remove(Oldest);}
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();if(auto* O=Mode->StateFor(PC)){PC->ClientDevelopmentResult(*O,Id,Reason);}
}
void ALDDevelopmentDirector::Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Id,uint8 Kind,ELDSkill Skill,FGuid Item,const FLDBuildPlan& Plan)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode->StateFor(PC);
    if(!HasAuthority()||!O||O->Match!=Match||O->Life!=Life||!Id||Kind<1||Kind>4
        ||(Kind==LDDevelopmentTransaction::Prepare&&(Plan.Attributes.Num()!=6||Plan.Ranks.Num()!=LDDevelopment::SkillCount||Plan.Slots.Num()!=4)))
    {if(PC){PC->ClientIntentResult(Id,TEXT("Invalid development request or stale life."));}return;}
    auto& S=States.FindOrAdd(PC);
    if(const auto* Receipt=S.Receipts.Find(Id)){PC->ClientDevelopmentResult(*O,Id,*Receipt);return;}
    if(Id<=S.LastRequest){PC->ClientIntentResult(Id,TEXT("Expired development request."));return;}
    const double Now=GetWorld()->GetTimeSeconds();S.Tokens=FMath::Min(3.,S.Tokens+FMath::Max(0.,Now-S.RateAt)*4);S.RateAt=Now;
    int32 Count=0;for(const auto& R:Requests){if(R.PC==PC){++Count;}}
    if(S.Tokens<1||Count>=2){PC->ClientIntentResult(Id,TEXT("Development request rate limited."));return;}
    --S.Tokens;S.LastRequest=Id;S.Receipts.Add(Id,TEXT("Development request queued."));
    Requests.Add({PC,Match,Life,Id,Kind,Skill,Item,Plan});
}
void ALDDevelopmentDirector::RefreshHold(ALDPlayerController* PC,uint32 Life,uint32 Token,uint32 Sequence,bool Moving)
{
    auto* S=States.Find(PC);if(!S||!S->Kind||S->Life!=Life||S->Token!=Token||Sequence<=S->Sequence||Token<=S->CancelledThrough){return;}
    S->Sequence=Sequence;S->LastHold=GetWorld()->GetTimeSeconds();if(Moving){Cancel(PC,TEXT("Movement cancelled the transaction."));}
}
void ALDDevelopmentDirector::Release(ALDPlayerController* PC,uint32 Life,uint32 Token)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode->StateFor(PC);if(!O||O->Life!=Life){return;}
    auto& S=States.FindOrAdd(PC);S.CancelledThrough=FMath::Max(S.CancelledThrough,Token);
    if(S.Kind&&S.Token<=Token){Cancel(PC,TEXT("Released; nothing spent or consumed."));}
}
void ALDDevelopmentDirector::Cancel(ALDPlayerController* PC,const FString& Reason)
{
    auto* S=States.Find(PC);if(!S||!S->Kind){return;}const uint32 Token=S->Token;
    S->CancelledThrough=FMath::Max(S->CancelledThrough,Token);S->Kind=0;S->Book.Reset();
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();if(auto* O=Mode->StateFor(PC))
    {
        if(auto* Body=Cast<ALDCharacter>(PC->GetPawn())){if(auto* Combat=Mode->Combat->StateFor(Body);Combat&&Combat->Action==8){Combat->Action=0;Combat->ImpactAt=0;O->ActionCue=Mode->Combat->CueFor(Body);O->SkillEndsAt=0;}}
        O->DevelopmentKind=0;O->DevelopmentToken=0;O->DevelopmentEndsAt=0;Mode->SyncOwner(PC);
    }
    FinishRequest(PC,Token,Reason);
}
void ALDDevelopmentDirector::Request(const FRequest& R,double Now)
{
    auto* PC=R.PC.Get();if(!PC){return;}auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=Mode->StateFor(PC);auto& S=States.FindChecked(PC);
    auto Reject=[&](const TCHAR* Why){FinishRequest(PC,R.Id,Why);};
    if(!O||O->Match!=R.Match||O->Life!=R.Life||!Eligible(PC,Now,false)){Reject(TEXT("Development action unavailable or stale life."));return;}
    auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());auto* Combat=Mode->Combat->StateFor(Body);
    if(S.Kind||Combat->Action||(Mode->Objectives&&Mode->Objectives->HasChannel(PC))||Combat->LastDamage>=Now-1.e-6){Reject(TEXT("Finish the current action first."));return;}
    if(R.Kind==LDDevelopmentTransaction::Buy)
    {
        if(!NearStation(PC,true)||!LDDevelopmentTransaction::Sold(R.Skill)||O->Gold<40||O->Books.Num()>=4){Reject(TEXT("Buy at Guild: 40 gold and an unread-book space required."));return;}
        auto* Book=GetWorld()->SpawnActor<ALDBook>();if(!Book){Reject(TEXT("Book unavailable; nothing spent."));return;}
        Book->Skill=R.Skill;Book->Holder=PC;O->Gold-=40;FLDOwnedBook Entry;Entry.Id=Book->Identity;Entry.Skill=R.Skill;O->Books.Add(Entry);
        Mode->SyncOwner(PC);FinishRequest(PC,R.Id,TEXT("Purchased one unread book for 40 gold. Select it and hold E to read."));return;
    }
    if(R.Kind==LDDevelopmentTransaction::PickUp)
    {
        auto* Book=Mode->Vision?Cast<ALDBook>(Mode->Vision->ResolveObserved(PC,R.Item)):nullptr;
        if(!Book||Book->State!=0||O->Books.Num()>=4||FVector::DistSquared2D(Body->GetActorLocation(),Book->GetActorLocation())>80*80||!LDGround::ClearSegment(GetWorld(),Body->GetActorLocation(),Book->GetActorLocation()))
        {Reject(TEXT("No visible dropped book in reach, or inventory full."));return;}
        Book->State=1;Book->Holder=PC;FLDOwnedBook Entry;Entry.Id=Book->Identity;Entry.Skill=Book->Skill;O->Books.Add(Entry);
        Mode->SyncOwner(PC);FinishRequest(PC,R.Id,TEXT("Picked up one unread book."));return;
    }
    if(R.Id<=S.CancelledThrough||!Eligible(PC,Now,true)||!Stationary(PC)){Reject(TEXT("Hold E while stationary after six seconds without damage dealt or taken."));return;}
    ALDBook* Book=nullptr;
    if(R.Kind==LDDevelopmentTransaction::Prepare)
    {FLDCharacterBuild Candidate;FString Why;if(!NearStation(PC)||!LDDevelopment::Prepare(O->Build,R.Plan,Candidate,Why)){FinishRequest(PC,R.Id,Why.IsEmpty()?TEXT("Preparation requires a visible station within 120 cm."):Why);return;}}
    else
    {Book=OwnedBook(PC,R.Item);if(!Book||uint8(Book->Skill)<1||uint8(Book->Skill)>=LDDevelopment::SkillCount||LDDevelopment::Rank(O->Build,Book->Skill)>0){Reject(TEXT("Read an owned, unknown-skill book; known books are retained."));return;}}
    S.Kind=R.Kind;S.Token=R.Id;S.Life=R.Life;S.Match=R.Match;S.Plan=R.Plan;S.Book=Book;S.Sequence=0;S.LastHold=Now;S.Anchor=Body->GetActorLocation();S.EndsAt=Now+(Book?3.:2.);
    Combat->Action=8;Combat->Skill=ELDSkill::None;Combat->ImpactAt=S.EndsAt;
    if(Mode->Objectives){Mode->Objectives->Interrupt(PC,Now,true);}
    O->DevelopmentKind=S.Kind;O->DevelopmentToken=S.Token;O->DevelopmentEndsAt=S.EndsAt;Mode->SyncOwner(PC);
    FinishRequest(PC,R.Id,Book?TEXT("Reading accepted; keep holding E for three seconds."):TEXT("Preparation accepted; keep holding E for two seconds."));
}
void ALDDevelopmentDirector::Sample(double Now)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();for(const auto& R:Requests){Request(R,Now);}Requests.Reset();
    for(auto& Pair:States)
    {
        auto* PC=Pair.Key.Get();auto& S=Pair.Value;if(!PC||!S.Kind){continue;}auto* O=Mode->StateFor(PC);auto* Body=Cast<ALDCharacter>(PC->GetPawn());auto* Combat=Mode->Combat->StateFor(Body);
        const bool Common=O&&O->Match==S.Match&&O->Life==S.Life&&Eligible(PC,Now,true)&&Combat&&Combat->Action==8&&Stationary(PC)
            &&Now-S.LastHold<=.25&&S.Token>S.CancelledThrough&&FVector::DistSquared2D(Body->GetActorLocation(),S.Anchor)<=4;
        const bool Target=S.Kind==LDDevelopmentTransaction::Prepare?(NearStation(PC)&&O&&O->Build.Revision==S.Plan.ExpectedRevision)
            :(S.Book.IsValid()&&S.Book->State==1&&S.Book->Holder==PC&&O&&LDDevelopment::Rank(O->Build,S.Book->Skill)==0);
        if(!Common||!Target){Cancel(PC,TEXT("Transaction interrupted or changed; nothing spent or consumed."));continue;}
        if(Now+1.e-6<S.EndsAt){continue;}
        if(S.Kind==LDDevelopmentTransaction::Prepare)
        {
            FLDCharacterBuild Candidate;FString Why;if(!LDDevelopment::Prepare(O->Build,S.Plan,Candidate,Why)){Cancel(PC,Why);continue;}
            O->Build=MoveTemp(Candidate);
        }
        else
        {
            auto* Book=S.Book.Get();O->Build.Ranks[uint8(Book->Skill)]=1;++O->Build.Revision;
            O->Books.RemoveAll([&](const FLDOwnedBook& Entry){return Entry.Id==Book->Identity;});Book->Holder.Reset();Book->Destroy();
        }
        const bool Prepared=S.Kind==LDDevelopmentTransaction::Prepare;
        O->Hp=Combat->Hp;Mode->RefreshBuild(PC);
        Cancel(PC,Prepared?TEXT("Build committed atomically; cooldowns retained."):TEXT("Book consumed once; learned rank one without equipping or spending skill points."));
    }
    for(auto It=States.CreateIterator();It;++It){if(!It.Key().IsValid()){It.RemoveCurrent();}}
}
void ALDDevelopmentDirector::DropBooks(ALDPlayerController* PC,const FVector& Position)
{
    Cancel(PC,TEXT("Life ended; unread books dropped."));
    FVector Point(FMath::Clamp(Position.X,-2320.,2320.),FMath::Clamp(Position.Y,-1520.,1520.),12);FHitResult Ground;
    if(GetWorld()->LineTraceSingleByChannel(Ground,Point+FVector(0,0,600),Point-FVector(0,0,100),ECC_GameTraceChannel1)){Point.Z=Ground.ImpactPoint.Z+12;}
    for(TActorIterator<ALDBook> It(GetWorld());It;++It){if(It->State==1&&It->Holder==PC){It->State=0;It->Holder.Reset();It->SetActorLocation(Point);}}
    if(auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC)){O->Books.Reset();GetWorld()->GetAuthGameMode<ALDGameMode>()->SyncOwner(PC);}
}
