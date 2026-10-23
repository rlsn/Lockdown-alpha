#include "Inventory/LDInventoryDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Development/LDDevelopmentDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Core/LDGroundQueries.h"
#include "Vision/LDVisionDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void ALDInventoryDirector::FailNextWardForProbe()
{
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("LDInventoryProbe"))){bFailWard=true;}
#endif
}

void ALDInventoryDirector::Initialize(ALDPlayerController* PC)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);if(!O||O->Inventory.bInitialized){return;}
    O->Inventory.bInitialized=true;O->Inventory.Capacity=uint8(FMath::Clamp(Capacity,1,64));const uint8 Charges=O->WardCharges;O->WardCharges=0;
    for(uint8 I=0;I<Charges;++I){auto* Item=GetWorld()->SpawnActor<ALDItem>();check(Item);const bool Adopted=Adopt(PC,Item,TEXT("CarriedEndowment"));check(Adopted);}Changed(PC);
}
ALDItem* ALDInventoryDirector::Owned(AActor* Container,FGuid Id) const
{for(TActorIterator<ALDItem> It(GetWorld());It;++It){if(It->State==1&&It->Holder==Container&&It->Identity==Id){return *It;}}return nullptr;}
bool ALDInventoryDirector::HasSpace(ALDPlayerController* PC,ELDItemKind Kind,FString* Reason) const
{
    const auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);return O&&LDInventory::Fits(O->Inventory,O->Build,{}, {Kind},Reason);
}
void ALDInventoryDirector::Changed(ALDPlayerController* PC)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);if(!O){return;}
    // Legacy own fields are compatibility projections, never a second V05 store.
    O->Books.Reset();O->WardCharges=0;O->SealId.Invalidate();
    for(const auto& Item:O->Inventory.Items){if(Item.Kind==ELDItemKind::Book){FLDOwnedBook B;B.Id=Item.Id;B.Skill=Item.Skill;O->Books.Add(B);}else if(Item.Kind==ELDItemKind::WardCharge){++O->WardCharges;}else if(Item.Kind==ELDItemKind::Seal){O->SealId=Item.Id;}}
    check(LDInventory::Valid(O->Inventory));M->SyncOwner(PC);
}
bool ALDInventoryDirector::Adopt(ALDPlayerController* PC,ALDItem* Item,FName CreationCause,bool Journal)
{
    auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);if(!Item||!O||!HasSpace(PC,Item->Kind)||O->Inventory.Find(Item->Identity)||(Item->State==1&&Item->Holder.IsValid())){return false;}
    auto* Economy=GetWorld()->GetAuthGameMode<ALDGameMode>()->Economy.Get();
    if(Economy&&Item->Kind==ELDItemKind::Seal){return false;}
    if(Economy&&!Economy->Accounting().Item(Item->Identity)&&CreationCause.IsNone())
    {if(Economy->AllowDiagnosticItems()){CreationCause=TEXT("DiagnosticItemCreation");}else{return false;}}
    uint8 Slot=0;while(O->Inventory.Items.ContainsByPredicate([&](const auto& I){return I.Slot==Slot;})){++Slot;}
    Item->Holder=PC;Item->State=1;Item->Slot=Slot;++Item->Revision;
    if(auto* Seal=Cast<ALDSeal>(Item)){Seal->Carrier=PC;}
    FLDInventoryItem Entry;Entry.Id=Item->Identity;Entry.Kind=Item->Kind;Entry.Skill=Item->Skill;Entry.Slot=Slot;Entry.Revision=Item->Revision;Entry.bReserved=Item->bReserved;O->Inventory.Items.Add(Entry);++O->Inventory.Revision;Changed(PC);if(Economy&&Journal){if(!Economy->Record(CreationCause.IsNone()?FName(TEXT("Pickup")):CreationCause)){++Economy->AuditFailures;}}return true;
}
void ALDInventoryDirector::Remove(ALDPlayerController* PC,ALDItem* Item,bool Consume,FName Cause)
{
    if(!Item||Item->Holder!=PC||Item->State!=1){return;}auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);if(!O){return;}
    LDInventory::Remove(O->Inventory,Item->Identity);Item->Holder.Reset();Item->State=Consume?2:0;Item->bReserved=false;++Item->Revision;Changed(PC);if(auto* E=GetWorld()->GetAuthGameMode<ALDGameMode>()->Economy.Get()){if(!E->Record(Cause.IsNone()?FName(Consume?TEXT("ItemConsumed"):TEXT("Drop")):Cause,0,0,Consume?TArray<FGuid>{Item->Identity}:TArray<FGuid>{})){++E->AuditFailures;}}if(Consume){Item->Destroy();}
}
void ALDInventoryDirector::EndLife(ALDPlayerController* PC)
{
    auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);if(!O){return;}const auto Items=O->Inventory.Items;
    for(const auto& Entry:Items)
    {
        if(auto* Item=Owned(PC,Entry.Id))
        {
            Item->bReserved=false;
            if(Item->Kind==ELDItemKind::WardCharge){Remove(PC,Item,true,TEXT("UnusedWardLoss"));}
            else if(LDInventory::Commodity(Item->Kind))
            {
                FVector Surface;const FVector Point=PC->GetPawn()?PC->GetPawn()->GetActorLocation():LDRegion::Site(GetWorld(),1);
                if(!LDGround::MapSurface(GetWorld(),Point,Surface)){const bool Found=LDGround::MapSurface(GetWorld(),LDRegion::Site(GetWorld(),1)+FVector(80,0,0),Surface);check(Found);}
                Item->SetActorLocation(Surface+FVector(0,0,12));Remove(PC,Item,false);
            }
        }
    }
}
bool ALDInventoryDirector::Eligible(ALDPlayerController* PC,double Now) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);const auto* S=M->Combat->StateFor(PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr);
    return M->IsPlaying()&&O&&O->bActive&&PC->GetPawn()&&S&&S->Hp>0&&Now>=O->ProtectedUntil&&Now>=S->ProtectionUntil&&!S->Action&&!M->Objectives->HasChannel(PC)&&!O->DevelopmentKind;
}
void ALDInventoryDirector::QuotePickup(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token,FGuid Observed)
{
    auto& Client=Clients.FindOrAdd(PC);const double At=GetWorld()->GetTimeSeconds();Client.Tokens=FMath::Min(8.,Client.Tokens+FMath::Max(0.,At-Client.At)*8);Client.At=At;if(Client.Tokens<1){PC->ClientIntentResult(Token,TEXT("Inventory busy; try again"));return;}--Client.Tokens;
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);auto* Item=Cast<ALDItem>(M->Vision->ResolveObserved(PC,Observed));
    if(!Token||!O||O->Match!=Match||O->Life!=Life||!Eligible(PC,GetWorld()->GetTimeSeconds())||!Item||Item->State!=0||Item->bReserved||FVector::Dist2D(PC->GetPawn()->GetActorLocation(),Item->GetActorLocation())>80||!LDGround::ClearSegment(GetWorld(),PC->GetPawn()->GetActorLocation(),Item->GetActorLocation())||!M->Vision->Ray({PC->GetPawn()->GetActorLocation(),80,M->Vision->LevelAt(PC->GetPawn()->GetActorLocation())},Item->GetActorLocation())){PC->ClientIntentResult(Token,TEXT("No visible dropped item in reach"));return;}
    // No canonical identity, position, owner or private bag is disclosed.
    PC->ClientPickupQuote(Match,Life,Token,Observed,O->Inventory.Revision,Item->Revision);
}
void ALDInventoryDirector::Reply(ALDPlayerController* PC,uint32 Id,const FString& Reason)
{
    auto& C=Clients.FindOrAdd(PC);C.Receipts.Add(Id,Reason);while(C.Receipts.Num()>64){uint32 Old=MAX_uint32;for(const auto& P:C.Receipts){Old=FMath::Min(Old,P.Key);}C.Receipts.Remove(Old);}
    if(auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC)){PC->ClientInventoryResult(*O,Id,Reason);}
}
bool ALDInventoryDirector::Complete(ALDPlayerController* PC,uint32 Id,const FString& Reason)
{auto* C=Clients.Find(PC);if(!C||!C->Receipts.Contains(Id)){return false;}Reply(PC,Id,Reason);return true;}
void ALDInventoryDirector::Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Id,uint32 InvRev,uint8 Command,FGuid Item,uint32 ItemRev,uint8 Slot,FVector Point)
{
    auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);if(!O||O->Match!=Match||O->Life!=Life||!Id||Command<1||Command>LDInventory::Buy||Point.ContainsNaN()){PC->ClientIntentResult(Id,TEXT("Inventory action unavailable"));return;}
    auto& C=Clients.FindOrAdd(PC);if(const auto* Receipt=C.Receipts.Find(Id)){PC->ClientInventoryResult(*O,Id,*Receipt);return;}
    if(Id<=C.Last){PC->ClientIntentResult(Id,TEXT("Expired inventory request"));return;}
    const double Now=GetWorld()->GetTimeSeconds();C.Tokens=FMath::Min(8.,C.Tokens+FMath::Max(0.,Now-C.At)*8);C.At=Now;
    int32 Count=0;for(const auto& R:Requests){Count+=R.PC==PC;}if(C.Tokens<1||Count>=4){PC->ClientIntentResult(Id,TEXT("Inventory busy; try again"));return;}--C.Tokens;C.Last=Id;C.Receipts.Add(Id,TEXT("Inventory request pending"));Requests.Add({PC,Match,Life,Id,InvRev,Command,Item,ItemRev,Slot,Point});
}
void ALDInventoryDirector::Sample(double Now)
{const auto Pending=MoveTemp(Requests);Requests.Reset();for(const auto& R:Pending){Request(R,Now);}for(auto It=Clients.CreateIterator();It;++It){if(!It.Key().IsValid()){It.RemoveCurrent();}}}
void ALDInventoryDirector::Request(const FRequest& R,double Now)
{
    auto* PC=R.PC.Get();if(!PC){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);auto Reject=[&](const TCHAR* Why){Reply(PC,R.Id,Why);};
    if(!O||O->Match!=R.Match||O->Life!=R.Life||!M->IsPlaying()||O->Inventory.Revision!=R.InventoryRevision){Reject(TEXT("Inventory changed; review again"));return;}
    ALDItem* Item=R.Command==LDInventory::Pickup?Cast<ALDItem>(M->Vision->ResolveObserved(PC,R.Item)):Owned(PC,R.Item);
    if(M->Economy&&Item&&Item->Kind==ELDItemKind::Seal){Reject(TEXT("Legacy Seals are disabled in the economy profile"));return;}
    if(R.Command!=LDInventory::Sort&&R.Command!=LDInventory::Buy&&(!Item||Item->Revision!=R.ItemRevision||Item->bReserved)){Reject(TEXT("Item changed or unavailable"));return;}
    if(R.Command<=LDInventory::Unbind)
    {
        if(R.Command==LDInventory::Sort||R.Command==LDInventory::Move)
        {
            if(O->Inventory.Items.ContainsByPredicate([](const auto& I){return I.bReserved;})){Reject(TEXT("Reserved items cannot be moved"));return;}
            if(R.Command==LDInventory::Sort){O->Inventory.Items.Sort([](const auto& A,const auto& B){if(A.Kind!=B.Kind){return A.Kind<B.Kind;}if(A.Skill!=B.Skill){return FCString::Stricmp(*LDInventory::Name(A.Kind,A.Skill),*LDInventory::Name(B.Kind,B.Skill))<0;}return A.Id.ToString()<B.Id.ToString();});for(int32 I=0;I<O->Inventory.Items.Num();++I){O->Inventory.Items[I].Slot=uint8(I);}}
            else{if(R.Slot>=O->Inventory.Capacity){Reject(TEXT("Invalid bag slot"));return;}const uint8 Before=Item->Slot;for(auto& Entry:O->Inventory.Items){if(Entry.Slot==R.Slot){Entry.Slot=Before;}else if(Entry.Id==R.Item){Entry.Slot=R.Slot;}}}
            for(const auto& Entry:O->Inventory.Items){if(auto* Physical=Owned(PC,Entry.Id)){Physical->Slot=Entry.Slot;}}++O->Inventory.Revision;
        }
        else if(R.Command==LDInventory::Bind){FString Why;if(!LDInventory::BindQuick(O->Inventory,R.Item,R.Slot,Why)){Reply(PC,R.Id,Why);return;}}
        else{for(auto& Ref:O->Inventory.Quick){if(Ref==R.Item){Ref.Invalidate();}}++O->Inventory.Revision;}
        Changed(PC);Reply(PC,R.Id,TEXT("Inventory organized"));return;
    }
    if(!Eligible(PC,Now)){Reject(TEXT("Item use unavailable while protected, waiting or occupied"));return;}
    if(R.Command==LDInventory::Read)
    {
        if(Item->Kind!=ELDItemKind::Book||LDDevelopment::Rank(O->Build,Item->Skill)){Reject(TEXT("Already learned; book kept"));return;}
        M->Development->Queue(PC,R.Match,R.Life,R.Id,LDDevelopmentTransaction::Read,ELDSkill::None,R.Item,FLDBuildPlan(),R.InventoryRevision,R.ItemRevision);return;
    }
    if(R.Command==LDInventory::Place)
    {
        if(Item->Kind!=ELDItemKind::WardCharge){Reject(TEXT("Choose a ward charge"));return;}
        if(bFailWard){bFailWard=false;Reject(TEXT("Ward could not be placed; charge kept"));return;}
        FString Why;if(M->Vision->PlaceWard(PC,R.Point,Why,Item->Identity)){Reply(PC,R.Id,Why);}else{Reply(PC,R.Id,Why);}return;
    }
    if(R.Command==LDInventory::Drop)
    {
        FVector Surface;const APawn* Body=PC->GetPawn();if(FVector::Dist2D(Body->GetActorLocation(),R.Point)>80||!LDGround::MapSurface(GetWorld(),R.Point,Surface)||!LDGround::ClearSegment(GetWorld(),Body->GetActorLocation(),Surface)||!M->Vision->Ray({Body->GetActorLocation(),80,M->Vision->LevelAt(Body->GetActorLocation())},Surface)||GetWorld()->OverlapAnyTestByObjectType(Surface+FVector(0,0,16),FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(12))){Reject(TEXT("No legal drop ground within 80 cm"));return;}
        if(M->Trade){M->Trade->CancelFor(PC);}if(auto* Seal=Cast<ALDSeal>(Item)){M->Objectives->AnnounceVoluntaryDrop(Seal,Surface+FVector(0,0,16));}else{Item->SetActorLocation(Surface+FVector(0,0,16));}
        Remove(PC,Item,false);Reply(PC,R.Id,TEXT("Item dropped"));return;
    }
    if(R.Command==LDInventory::Pickup)
    {
        FString CapacityReason;if(!HasSpace(PC,Item->Kind,&CapacityReason)){Reply(PC,R.Id,CapacityReason);return;}
        if(Item->State!=0||Item->Holder.IsValid()||FVector::Dist2D(PC->GetPawn()->GetActorLocation(),Item->GetActorLocation())>80||!LDGround::ClearSegment(GetWorld(),PC->GetPawn()->GetActorLocation(),Item->GetActorLocation())||!M->Vision->Ray({PC->GetPawn()->GetActorLocation(),80,M->Vision->LevelAt(PC->GetPawn()->GetActorLocation())},Item->GetActorLocation())){Reject(TEXT("Item no longer in reach"));return;}
        if(M->Trade){M->Trade->CancelFor(PC);}if(!Adopt(PC,Item)){Reject(TEXT("Inventory full"));return;}Reply(PC,R.Id,TEXT("Item picked up"));return;
    }
    if(R.Command==LDInventory::Buy)
    {
        Reject(TEXT("Open the Guild bookshop and confirm its current quote"));return;
    }
}
