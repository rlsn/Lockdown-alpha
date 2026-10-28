#include "Development/LDDevelopmentDirector.h"
#include "Development/LDV08Loadout.h"
#include "Combat/LDTacticalCatalog.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDTradeDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "Region/LDRegionDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "EngineUtils.h"
#include "Inventory/LDInventoryDirector.h"

namespace
{
    bool BindingAllowed(ALDGameMode* M,ALDCharacter* Body,const FLDOwnerState& O,const FLDInventory& Draft)
    {if(O.Build.RulesVersion!=8){return true;}if(Draft.Bindings.Num()!=11){return false;}for(int32 N=7;N<11;++N){if(O.Inventory.Bindings[N]!=Draft.Bindings[N]&&!M->Combat->Allows(Body,LDTactical::EAction::Item)){return false;}}return true;}
    bool DraftAllowed(ALDGameMode* M,ALDCharacter* Body,const FLDOwnerState& O,const FLDBuildPlan& P)
    {
        const bool Skill=M->Combat->Allows(Body,LDTactical::EAction::Skill),Item=M->Combat->Allows(Body,LDTactical::EAction::Item);
        if(O.Build.RulesVersion!=8){return Skill;}
        if(!LDV08::PreparationPermissions(O.Build,P,Skill,Item)||P.Bindings.Num()!=11){return false;}
        for(int32 N=7;N<11;++N){if(P.Bindings[N]!=O.Inventory.Bindings[N]&&!Item){return false;}}return true;
    }
}
bool LDDevelopmentTransaction::Sold(ELDSkill Skill)
{return Skill==ELDSkill::Step||Skill==ELDSkill::Spark||Skill==ELDSkill::Patch||Skill==ELDSkill::Parley;}
ALDBook::ALDBook(){Kind=ELDItemKind::Book;State=1;}
ALDDevelopmentDirector::ALDDevelopmentDirector(){bReplicates=false;}
ALDBook* ALDDevelopmentDirector::OwnedBook(ALDPlayerController* PC,const FGuid& Id) const
{for(TActorIterator<ALDBook> It(GetWorld());It;++It){if(It->State==1&&It->Holder==PC&&It->Identity==Id){return *It;}}return nullptr;}
bool ALDDevelopmentDirector::NearStation(ALDPlayerController* PC,bool GuildOnly) const
{
    const APawn* Body=PC?PC->GetPawn():nullptr;if(!Body){return false;}
    const FVector From=Body->GetActorLocation();
    for(uint8 Site:{uint8(LDObjective::Guild),uint8(LDObjective::Station),uint8(6)})
    {if(Site==6&&!LDRegion::Definition(GetWorld())){continue;}if(GuildOnly&&Site!=LDObjective::Guild){continue;}const FVector To=LDRegion::Site(GetWorld(),Site);if(FVector::DistSquared2D(From,To)<=120*120&&LDGround::ClearSegment(GetWorld(),From,To)){return true;}}
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
    return Mode->IsDevelopmentProfile()&&Mode->IsPlaying()&&O&&O->bActive&&Body&&S&&S->Hp>0&&Mode->Combat->Allows(const_cast<ALDCharacter*>(Body),LDTactical::EAction::Service)&&Now>=S->ProtectionUntil
        &&(!Quiet||(Now-FMath::Max(S->LastDamage,S->LastDealt)>=6&&(!Mode->IsTacticalProfile()||Mode->Combat->QuietForPreparation(const_cast<ALDCharacter*>(Body)))));
}
void ALDDevelopmentDirector::FinishRequest(ALDPlayerController* PC,uint32 Id,const FString& Reason)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("LDB86Probe"))){UE_LOG(LogTemp,Display,TEXT("LD_B86_DEVELOPMENT request=%u time=%.3f result=%s"),Id,GetWorld()->GetTimeSeconds(),*Reason);}
    auto& S=States.FindOrAdd(PC);S.Receipts.Add(Id,Reason);
    while(S.Receipts.Num()>32){uint32 Oldest=MAX_uint32;for(const auto& Pair:S.Receipts){Oldest=FMath::Min(Oldest,Pair.Key);}S.Receipts.Remove(Oldest);}
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();if(Mode->Inventory&&Mode->Inventory->Complete(PC,Id,Reason)){return;}if(auto* O=Mode->StateFor(PC)){PC->ClientDevelopmentResult(*O,Id,Reason);}
}
void ALDDevelopmentDirector::Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Id,uint8 Kind,ELDSkill Skill,FGuid Item,const FLDBuildPlan& Plan,uint32 InventoryRevision,uint32 ItemRevision)
{
    if(auto* Clock=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get()){Clock->FlushPastCommand();}
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode->StateFor(PC);
    if(!HasAuthority()||!O||O->Match!=Match||O->Life!=Life||!Id||Kind<1||(Kind>4&&Kind!=LDDevelopmentTransaction::Loadout)
        ||(Kind==LDDevelopmentTransaction::Prepare&&(Plan.Attributes.Num()!=6||Plan.RulesVersion!=O->Build.RulesVersion||Plan.CatalogDigest!=O->Build.CatalogDigest||Plan.Ranks.Num()!=LDDevelopment::Count(O->Build)||Plan.Slots.Num()!=LDDevelopment::Slots(O->Build))))
    {if(PC){FinishRequest(PC,Id,TEXT("Invalid development request or stale life."));}return;}
    auto& S=States.FindOrAdd(PC);
    if(const auto* Receipt=S.Receipts.Find(Id)){PC->ClientDevelopmentResult(*O,Id,*Receipt);return;}
    if(Id<=S.LastRequest){PC->ClientIntentResult(Id,TEXT("Expired development request."));return;}
    const double Now=GetWorld()->GetTimeSeconds();S.Tokens=FMath::Min(3.,S.Tokens+FMath::Max(0.,Now-S.RateAt)*4);S.RateAt=Now;
    int32 Count=0;for(const auto& R:Requests){if(R.PC==PC){++Count;}}
    if(S.Tokens<1||Count>=2){FinishRequest(PC,Id,TEXT("Development request rate limited."));return;}
    --S.Tokens;S.LastRequest=Id;S.Receipts.Add(Id,TEXT("Development request queued."));
    Mode->Objectives->CancelClaimForAction(PC,Now);Requests.Add({PC,Match,Life,Id,Kind,Skill,Item,Plan,InventoryRevision,ItemRevision});
}
bool ALDDevelopmentDirector::Replay(ALDPlayerController* PC,uint32 Token)
{const auto* State=States.Find(PC);const auto* Receipt=State?State->Receipts.Find(Token):nullptr;auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);if(!O||!Receipt){return false;}PC->ClientDevelopmentResult(*O,Token,*Receipt);return true;}
void ALDDevelopmentDirector::QueueLoadout(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token,uint32 InvRev,uint32 ItemRev,uint8 Command,FGuid Item,uint8 Slot)
{
    if(auto* Clock=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get()){Clock->FlushPastCommand();}const int32 Before=Requests.Num();Queue(PC,Match,Life,Token,LDDevelopmentTransaction::Loadout,ELDSkill::None,Item,FLDBuildPlan(),InvRev,ItemRev);if(Requests.Num()>Before){Requests.Last().Command=Command;Requests.Last().Slot=Slot;}}
void ALDDevelopmentDirector::RefreshHold(ALDPlayerController* PC,uint32 Life,uint32 Token,uint32 Sequence,bool Moving)
{
    if(auto* Clock=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get()){Clock->FlushPastCommand();}
    auto* S=States.Find(PC);if(!S||!S->Kind||S->Life!=Life||S->Token!=Token||Sequence<=S->Sequence||Token<=S->CancelledThrough){return;}
    S->Sequence=Sequence;S->LastHold=GetWorld()->GetTimeSeconds();if(Moving){Cancel(PC,TEXT("Movement cancelled the transaction."));}
}
void ALDDevelopmentDirector::Release(ALDPlayerController* PC,uint32 Life,uint32 Token)
{
    if(auto* Clock=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get()){Clock->FlushPastCommand();}
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode->StateFor(PC);if(!O||O->Life!=Life){return;}
    auto& S=States.FindOrAdd(PC);S.CancelledThrough=FMath::Max(S.CancelledThrough,Token);
    if(S.Kind&&S.Token<=Token){Cancel(PC,TEXT("Stopped; nothing spent or consumed."));}
}
void ALDDevelopmentDirector::Cancel(ALDPlayerController* PC,const FString& Reason)
{
    auto* S=States.Find(PC);if(!S||!S->Kind){return;}const uint32 Token=S->Token;
    S->CancelledThrough=FMath::Max(S->CancelledThrough,Token);S->Kind=0;S->Book.Reset();
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();if(auto* O=Mode->StateFor(PC))
    {
        if(auto* Body=Cast<ALDCharacter>(PC->GetPawn())){if(auto* Combat=Mode->Combat->StateFor(Body);Combat&&Combat->Action==8){Combat->Action=0;Combat->ImpactAt=0;O->ActionCue=Mode->Combat->CueFor(Body);O->SkillEndsAt=0;}}
        O->DevelopmentKind=0;O->DevelopmentToken=0;O->DevelopmentEndsAt=0;O->DevelopmentItem.Invalidate();Mode->SyncOwner(PC);
    }
    FinishRequest(PC,Token,Reason);
}
void ALDDevelopmentDirector::Request(const FRequest& R,double Now)
{
    auto* PC=R.PC.Get();if(!PC){return;}auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=Mode->StateFor(PC);auto& S=States.FindChecked(PC);
    auto Reject=[&](const TCHAR* Why){FinishRequest(PC,R.Id,Why);};
    if(!O||O->Match!=R.Match||O->Life!=R.Life||!Eligible(PC,Now,false)){Reject(TEXT("Development action unavailable or stale life."));return;}
    if(Mode->Inventory&&R.Kind!=LDDevelopmentTransaction::Prepare)
    {auto* Item=Mode->Inventory->Owned(PC,R.Item);if((R.Kind!=LDDevelopmentTransaction::Read&&R.Kind!=LDDevelopmentTransaction::Loadout)||!Item||(R.Kind==LDDevelopmentTransaction::Read&&Item->Kind!=ELDItemKind::Book)||Item->bReserved||Item->Revision!=R.ItemRevision||O->Inventory.Revision!=R.InventoryRevision){Reject(TEXT("Use a current owned inventory item"));return;}}
    auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());auto* Combat=Mode->Combat->StateFor(Body);
    if(S.Kind||Combat->Action||(Mode->Objectives&&Mode->Objectives->HasChannel(PC))||Combat->LastDamage>=Now-1.e-6){Reject(TEXT("Finish the current action first."));return;}
    if(R.Kind==LDDevelopmentTransaction::Buy)
    {
        if(!NearStation(PC,true)||!LDDevelopmentTransaction::Sold(R.Skill)||O->Gold<40||O->Books.Num()>=4){Reject(TEXT("Buy at Guild: 40 gold and an unread-book space required."));return;}
        auto* Book=GetWorld()->SpawnActor<ALDBook>();if(!Book){Reject(TEXT("Book unavailable; nothing spent."));return;}
        Book->Skill=R.Skill;Book->Holder=PC;O->Gold-=40;Mode->RecordEconomyGold(TEXT("LegacyBookPurchase"),0,40);FLDOwnedBook Entry;Entry.Id=Book->Identity;Entry.Skill=R.Skill;O->Books.Add(Entry);
        Mode->SyncOwner(PC);FinishRequest(PC,R.Id,TEXT("Purchased one unread book for 40 gold. Select it and use once to read; S stops."));return;
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
    if(R.Kind==LDDevelopmentTransaction::Loadout)
    {FString Why;if(!Mode->IsTacticalProfile()||((R.Command==LDInventory::Bind||R.Command==LDInventory::Unbind||O->Build.RulesVersion==8&&R.Slot>=7)&&!Mode->Combat->Allows(Body,LDTactical::EAction::Item))||!LDGear::Propose(O->Inventory,O->Build,R.Command,R.Item,R.Slot,S.Loadout,Why)||!BindingAllowed(Mode,Body,*O,S.Loadout)){FinishRequest(PC,R.Id,Why.IsEmpty()?TEXT("Quick preparation blocked by control"):Why);return;}S.InventoryRevision=O->Inventory.Revision;S.BuildRevision=O->Build.Revision;S.Command=R.Command;S.Slot=R.Slot;}
    else if(R.Kind==LDDevelopmentTransaction::Prepare)
    {if(!DraftAllowed(Mode,Body,*O,R.Plan)){Reject(TEXT("Silence/Mute prevents the changed action sources"));return;}FLDCharacterBuild Candidate;FLDInventory Projected;FString Why;if(!NearStation(PC)||(O->Build.RulesVersion==8?!LDV08::Project(O->Inventory,O->Build,R.Plan,Projected,Candidate,Why):!LDDevelopment::Prepare(O->Build,R.Plan,Candidate,Why))||(Mode->Inventory&&!LDInventory::Fits(O->Inventory,Candidate,{}, {},&Why))){FinishRequest(PC,R.Id,Why.IsEmpty()?TEXT("Preparation requires a visible station within 120 cm."):Why);return;}}
    else
    {Book=OwnedBook(PC,R.Item);if(!Book||uint8(Book->Skill)<1||uint8(Book->Skill)>=LDDevelopment::Count(O->Build)||LDDevelopment::Rank(O->Build,Book->Skill)>0||(Mode->IsTacticalProfile()&&uint8(Book->Skill)>=10&&(!Mode->TacticalCatalog->Find(Book->Skill)))){Reject(TEXT("Read an owned, unknown-skill book; known books are retained."));return;}}
    if(Mode->Trade){Mode->Trade->CancelFor(PC);}S.Kind=R.Kind;S.Token=R.Id;S.Life=R.Life;S.Match=R.Match;S.Plan=R.Plan;S.Book=Book;S.Sequence=0;S.LastHold=Now;S.Anchor=Body->GetActorLocation();S.EndsAt=Now+(Book?3.:2.);
    Combat->Action=8;Combat->Skill=ELDSkill::None;Combat->ImpactAt=S.EndsAt;
    if(Mode->Objectives){Mode->Objectives->Interrupt(PC,Now,true);}
    O->DevelopmentKind=S.Kind;O->DevelopmentToken=S.Token;O->DevelopmentEndsAt=S.EndsAt;O->DevelopmentItem=R.Kind==LDDevelopmentTransaction::Loadout?R.Item:Book?Book->Identity:FGuid();Mode->SyncOwner(PC);
    FinishRequest(PC,R.Id,Book?TEXT("Reading started for three seconds; S stops."):TEXT("Preparation started for two seconds; S stops."));
}
void ALDDevelopmentDirector::Sample(double Now)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();for(const auto& R:Requests){Request(R,Now);}Requests.Reset();
    for(auto& Pair:States)
    {
        auto* PC=Pair.Key.Get();auto& S=Pair.Value;if(!PC||!S.Kind){continue;}auto* O=Mode->StateFor(PC);auto* Body=Cast<ALDCharacter>(PC->GetPawn());auto* Combat=Mode->Combat->StateFor(Body);
        const bool Common=O&&O->Match==S.Match&&O->Life==S.Life&&Eligible(PC,Now,true)&&Combat&&Combat->Action==8&&Stationary(PC)
            &&Now-S.LastHold<=.25&&S.Token>S.CancelledThrough&&FVector::DistSquared2D(Body->GetActorLocation(),S.Anchor)<=4;
        const bool Target=S.Kind==LDDevelopmentTransaction::Loadout?(O&&O->Inventory.Revision==S.InventoryRevision&&O->Build.Revision==S.BuildRevision&&(!(S.Command==LDInventory::Bind||S.Command==LDInventory::Unbind||O->Build.RulesVersion==8&&S.Slot>=7)||Mode->Combat->Allows(Body,LDTactical::EAction::Item))):S.Kind==LDDevelopmentTransaction::Prepare?(NearStation(PC)&&O&&O->Build.Revision==S.Plan.ExpectedRevision)
            :(S.Book.IsValid()&&S.Book->State==1&&S.Book->Holder==PC&&!S.Book->bReserved&&O&&LDDevelopment::Rank(O->Build,S.Book->Skill)==0);
        if(!Common||!Target||(S.Kind==LDDevelopmentTransaction::Loadout&&!BindingAllowed(Mode,Body,*O,S.Loadout))){Cancel(PC,TEXT("Transaction interrupted or changed; nothing spent or consumed."));continue;}
        if(Now+1.e-6<S.EndsAt){continue;}
        if(S.Kind==LDDevelopmentTransaction::Loadout)
        {
            auto Candidate=O->Build;Candidate.Gear=LDGear::Derive(S.Loadout);FString Why;if(!LDInventory::Fits(S.Loadout,Candidate,{}, {},&Why)){Cancel(PC,Why);continue;}
            // All physical identities/revisions were frozen by InventoryRevision. Publish once.
            O->Inventory=S.Loadout;
            for(auto& Entry:O->Inventory.Items){auto* Item=Mode->Inventory->Owned(PC,Entry.Id);check(Item);if(Item->BindingMask!=Entry.BindingMask){Item->BindingMask=Entry.BindingMask;++Item->Revision;Entry.Revision=Item->Revision;}}
            ++O->Build.Revision;Mode->Inventory->Changed(PC);
        }
        else if(S.Kind==LDDevelopmentTransaction::Prepare)
        {
            FLDCharacterBuild Candidate;FLDInventory Projected;FString Why;if(!DraftAllowed(Mode,Body,*O,S.Plan)||(O->Build.RulesVersion==8?!LDV08::Project(O->Inventory,O->Build,S.Plan,Projected,Candidate,Why):!LDDevelopment::Prepare(O->Build,S.Plan,Candidate,Why))||(Mode->Inventory&&!LDInventory::Fits(O->Inventory,Candidate,{}, {},&Why))){Cancel(PC,Why);continue;}
            if(O->Build.RulesVersion==8){O->Inventory=MoveTemp(Projected);++O->Inventory.Revision;for(auto& Entry:O->Inventory.Items){auto* Item=Mode->Inventory->Owned(PC,Entry.Id);check(Item);if(Item->BindingMask!=Entry.BindingMask){Item->BindingMask=Entry.BindingMask;++Item->Revision;Entry.Revision=Item->Revision;}}}
            O->Build=MoveTemp(Candidate);if(O->Build.RulesVersion==8){Mode->Inventory->Changed(PC);}
        }
        else
        {
            auto* Book=S.Book.Get();
            if(Mode->IsTacticalProfile()&&uint8(Book->Skill)>=10&&(!Mode->TacticalCatalog->Find(Book->Skill))){Cancel(PC,TEXT("Book definition no longer supported"));continue;}
            O->Build.Ranks[uint8(Book->Skill)]=1;++O->Build.Revision;
            if(Mode->Inventory){Mode->Inventory->Remove(PC,Book,true,TEXT("BookRead"));}else{O->Books.RemoveAll([&](const FLDOwnedBook& Entry){return Entry.Id==Book->Identity;});Book->Holder.Reset();Book->Destroy();}
        }
        const bool Loadout=S.Kind==LDDevelopmentTransaction::Loadout;const bool Prepared=S.Kind==LDDevelopmentTransaction::Prepare;
        O->Hp=Combat->Hp;Mode->RefreshBuild(PC);
        Cancel(PC,Loadout?TEXT("Loadout committed once; capacity and cooldowns retained."):Prepared?TEXT("Build committed atomically; cooldowns retained."):TEXT("Book consumed once; learned rank one without equipping or spending skill points."));
    }
    for(auto It=States.CreateIterator();It;++It){if(!It.Key().IsValid()){It.RemoveCurrent();}}
}
void ALDDevelopmentDirector::DropBooks(ALDPlayerController* PC,const FVector& Position)
{
    Cancel(PC,TEXT("Life ended; unread books dropped."));
    bool HasBooks=false;for(TActorIterator<ALDBook> It(GetWorld());It;++It){HasBooks|=It->State==1&&It->Holder==PC;}
    if(!HasBooks){if(auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC)){O->Books.Reset();GetWorld()->GetAuthGameMode<ALDGameMode>()->SyncOwner(PC);}return;}
    FVector Point(FMath::Clamp(Position.X,-2320.,2320.),FMath::Clamp(Position.Y,-1520.,1520.),12);FHitResult Ground;
    if(GetWorld()->LineTraceSingleByChannel(Ground,Point+FVector(0,0,600),Point-FVector(0,0,100),ECC_GameTraceChannel1)){Point.Z=Ground.ImpactPoint.Z+12;}
    if(GetWorld()->GetAuthGameMode<ALDGameMode>()->Inventory){FVector Surface;if(!LDGround::MapSurface(GetWorld(),Position,Surface)){const bool Valid=LDGround::MapSurface(GetWorld(),LDRegion::Site(GetWorld(),1)+FVector(80,0,0),Surface);check(Valid);}Point=Surface+FVector(0,0,12);}
    for(TActorIterator<ALDBook> It(GetWorld());It;++It){if(It->State==1&&It->Holder==PC){It->SetActorLocation(Point);if(auto* Inv=GetWorld()->GetAuthGameMode<ALDGameMode>()->Inventory.Get()){Inv->Remove(PC,*It,false);}else{It->State=0;It->Holder.Reset();}}}
    if(auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC)){O->Books.Reset();GetWorld()->GetAuthGameMode<ALDGameMode>()->SyncOwner(PC);}
}
