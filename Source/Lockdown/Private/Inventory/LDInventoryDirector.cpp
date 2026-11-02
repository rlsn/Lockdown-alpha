#include "Inventory/LDInventoryDirector.h"
#include "Development/LDV08Loadout.h"
#include "Economy/LDEconomyDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Vision/LDVisionDirector.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "World/LDWorldQueryTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
// Commands this director executes; every other opcode, including the retired Bind/Unbind/Read/Place/Pickup/Buy/Use,
// is refused with an explicit receipt. Survivor opcode values are never renumbered.
bool InventoryCommandSupported(uint8 Command)
{
    switch (Command)
    {
    case LDInventory::Move: case LDInventory::Sort: case LDInventory::Drop:
        return true;
    default:
        return false;
    }
}
}

LDInventoryDrop::ECheck LDInventoryDrop::FromGround(bool bLegal, ELDWorldQueryFailure Failure)
{
    if (bLegal) { return Failure == ELDWorldQueryFailure::None ? ECheck::Accept : ECheck::Fault; }
    switch (Failure)
    {
    case ELDWorldQueryFailure::InvalidPosition: case ELDWorldQueryFailure::Blocked:
    case ELDWorldQueryFailure::NotGrounded: case ELDWorldQueryFailure::WrongSurface:
        return ECheck::Refuse;
    default:
        return ECheck::Fault;
    }
}
LDInventoryDrop::ECheck LDInventoryDrop::FromSight(ELDSightResult Sight)
{
    switch (LDObservation::ResolveFromSight(Sight))
    {
    case ELDObservedResolve::Resolved: return ECheck::Accept;
    case ELDObservedResolve::TargetUnavailable: return ECheck::Refuse;
    default: return ECheck::Fault;
    }
}
LDInventoryRequest::EResult LDInventoryRequest::Admit(const FLDInventory& Inventory, uint8 Command, uint32 ExpectedInventoryRevision,
    uint32 ExpectedItemRevision, const FItemFacts& Item)
{
    if (Inventory.Revision != ExpectedInventoryRevision) { return EResult::StaleInventory; }
    if (Command == LDInventory::Sort) { return EResult::Accepted; }
    if (!Item.bOwned || Item.Revision != ExpectedItemRevision) { return EResult::StaleItem; }
    return Item.bReserved ? EResult::ReservedItem : EResult::Accepted;
}
LDInventoryRequest::EResult LDInventoryRequest::Organize(FLDInventory& Inventory, uint8 Command, uint32 ExpectedInventoryRevision,
    const FGuid& ItemId, uint32 ExpectedItemRevision, const FItemFacts& Item, uint8 Slot)
{
    if (Command != LDInventory::Move && Command != LDInventory::Sort) { return EResult::Unsupported; }
    const EResult Admission = Admit(Inventory, Command, ExpectedInventoryRevision, ExpectedItemRevision, Item);
    if (Admission != EResult::Accepted) { return Admission; }
    if (Inventory.Items.ContainsByPredicate([](const FLDInventoryItem& Entry) { return Entry.bReserved; })) { return EResult::ReservedBag; }
    if (Command == LDInventory::Move && Slot >= Inventory.Capacity) { return EResult::InvalidSlot; }
    if (Command == LDInventory::Sort)
    {
        Inventory.Items.Sort([](const FLDInventoryItem& A, const FLDInventoryItem& B)
        {
            if (A.Kind != B.Kind) { return A.Kind < B.Kind; }
            if (A.Skill != B.Skill) { return FCString::Stricmp(*LDInventory::Name(A.Kind, A.Skill), *LDInventory::Name(B.Kind, B.Skill)) < 0; }
            return A.Id.ToString() < B.Id.ToString();
        });
        for (int32 Index = 0; Index < Inventory.Items.Num(); ++Index) { Inventory.Items[Index].Slot = uint8(Index); }
    }
    else
    {
        for (FLDInventoryItem& Entry : Inventory.Items)
        {
            if (Entry.Slot == Slot) { Entry.Slot = Item.Slot; }
            else if (Entry.Id == ItemId) { Entry.Slot = Slot; }
        }
    }
    ++Inventory.Revision;
    return EResult::Accepted;
}

void ALDInventoryDirector::Initialize(ALDPlayerController* PC)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);if(!O||O->Inventory.bInitialized){return;}
    O->Inventory.bInitialized=true;O->Inventory.Capacity=uint8(FMath::Clamp(Capacity,1,64));Changed(PC);
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
    LDV08::InvalidateSources(O->Build,O->Inventory);check(LDInventory::Valid(O->Inventory));M->SyncOwner(PC);
}
bool ALDInventoryDirector::Adopt(ALDPlayerController* PC,ALDItem* Item,FName CreationCause,bool Journal)
{
    // Only the bag commodities stay adoptable; every other legacy item kind has no Demo definition.
    auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);if(!Item||!O||!LDInventory::Commodity(Item->Kind)||!HasSpace(PC,Item->Kind)||O->Inventory.Find(Item->Identity)||(Item->State==1&&Item->Holder.IsValid())){return false;}
    auto* Economy=GetWorld()->GetAuthGameMode<ALDGameMode>()->Economy.Get();
    // An instance the ledger does not know needs a formal creation cause; there is no diagnostic bypass.
    if(Economy&&!Economy->Accounting().Item(Item->Identity)&&CreationCause.IsNone()){return false;}
    uint8 Slot=0;while(O->Inventory.Items.ContainsByPredicate([&](const auto& I){return I.Slot==Slot;})){++Slot;}
    Item->Holder=PC;Item->State=1;Item->Slot=Slot;++Item->Revision;
    FLDInventoryItem Entry;Item->Project(Entry);O->Inventory.Items.Add(Entry);++O->Inventory.Revision;Changed(PC);if(Economy&&Journal){if(!Economy->Record(CreationCause.IsNone()?FName(TEXT("Pickup")):CreationCause)){++Economy->AuditFailures;}}return true;
}
void ALDInventoryDirector::Remove(ALDPlayerController* PC,ALDItem* Item,bool Consume,FName Cause)
{
    if(!Item||Item->Holder!=PC||Item->State!=1){return;}auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);if(!O){return;}
    LDInventory::Remove(O->Inventory,Item->Identity);Item->Holder.Reset();Item->State=Consume?2:0;Item->bReserved=false;++Item->Revision;Changed(PC);if(auto* E=GetWorld()->GetAuthGameMode<ALDGameMode>()->Economy.Get()){if(!E->Record(Cause.IsNone()?FName(Consume?TEXT("ItemConsumed"):TEXT("Drop")):Cause,0,0,Consume?TArray<FGuid>{Item->Identity}:TArray<FGuid>{})){++E->AuditFailures;}}if(Consume){Item->Destroy();}
}
// Dropping needs the caller's current living body in the running match, as the core's life table reports it.
bool ALDInventoryDirector::Eligible(ALDPlayerController* PC,ALDCharacter*& Body,FString& Why) const
{
    Body=nullptr;auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;
    if(!M||!M->IsPlaying()||!O||!O->bActive){Why=TEXT("Item actions need a running match");return false;}
    FLDContenderLifeRead Life;ALDCharacter* Resolved=nullptr;
    if(!M->ReadContenderLife(M->GetMatchContext(),M->ContenderId(PC),Life,Why)){if(Why.IsEmpty()){Why=TEXT("The core has no current life for this contender");}return false;}
    if(Life.Phase!=ELDLifePhase::Alive){Why=TEXT("Item actions need a living body");return false;}
    if(M->ResolveBody(M->GetMatchContext(),Life.Body,Resolved)!=ELDCommitReject::None||!Resolved||Resolved!=PC->GetPawn()){Why=TEXT("Item actions need the current registered body");return false;}
    Body=Resolved;Why.Reset();return true;
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
    auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);if(!O||O->Match!=Match||O->Life!=Life||!Id||!InventoryCommandSupported(Command)||Point.ContainsNaN()){PC->ClientIntentResult(Id,TEXT("Inventory action unavailable"));return;}
    auto& C=Clients.FindOrAdd(PC);if(const auto* Receipt=C.Receipts.Find(Id)){PC->ClientInventoryResult(*O,Id,*Receipt);return;}
    if(Id<=C.Last){PC->ClientIntentResult(Id,TEXT("Expired inventory request"));return;}
    const double Now=GetWorld()->GetTimeSeconds();C.Tokens=FMath::Min(8.,C.Tokens+FMath::Max(0.,Now-C.At)*8);C.At=Now;
    int32 Count=0;for(const auto& R:Requests){Count+=R.PC==PC;}if(C.Tokens<1||Count>=4){PC->ClientIntentResult(Id,TEXT("Inventory busy; try again"));return;}--C.Tokens;C.Last=Id;C.Receipts.Add(Id,TEXT("Inventory request pending"));Requests.Add({PC,Match,Life,Id,InvRev,Command,Item,ItemRev,Slot,Point});
}
void ALDInventoryDirector::Sample(double)
{const auto Pending=MoveTemp(Requests);Requests.Reset();for(const auto& R:Pending){Request(R);}for(auto It=Clients.CreateIterator();It;++It){if(!It.Key().IsValid()){It.RemoveCurrent();}}}
void ALDInventoryDirector::Request(const FRequest& R)
{
    auto* PC=R.PC.Get();if(!PC){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);auto Reject=[&](const TCHAR* Why){Reply(PC,R.Id,Why);};
    if(!O||O->Match!=R.Match||O->Life!=R.Life||!M->IsPlaying()){Reject(TEXT("Inventory changed; review again"));return;}
    ALDItem* Item=Owned(PC,R.Item);
    LDInventoryRequest::FItemFacts Facts;
    if(Item){Facts.bOwned=true;Facts.Revision=Item->Revision;Facts.bReserved=Item->bReserved;Facts.Slot=Item->Slot;}
    // One admission decides every refusal before any ownership, reference or version change.
    auto Refuse=[&](LDInventoryRequest::EResult Result)
    {
        switch(Result)
        {
        case LDInventoryRequest::EResult::StaleInventory: Reject(TEXT("Inventory changed; review again")); break;
        case LDInventoryRequest::EResult::StaleItem: case LDInventoryRequest::EResult::ReservedItem: Reject(TEXT("Item changed or unavailable")); break;
        case LDInventoryRequest::EResult::ReservedBag: Reject(TEXT("Reserved items cannot be moved")); break;
        case LDInventoryRequest::EResult::InvalidSlot: Reject(TEXT("Invalid bag slot")); break;
        default: Reject(TEXT("Inventory action unavailable")); break;
        }
    };
    if(R.Command==LDInventory::Sort||R.Command==LDInventory::Move)
    {
        const LDInventoryRequest::EResult Result=LDInventoryRequest::Organize(O->Inventory,R.Command,R.InventoryRevision,R.Item,R.ItemRevision,Facts,R.Slot);
        if(Result!=LDInventoryRequest::EResult::Accepted){Refuse(Result);return;}
        for(const auto& Entry:O->Inventory.Items){if(auto* Physical=Owned(PC,Entry.Id)){Physical->Slot=Entry.Slot;}}
        Changed(PC);Reply(PC,R.Id,TEXT("Inventory organized"));return;
    }
    if(R.Command==LDInventory::Drop)
    {
        const LDInventoryRequest::EResult Admission=LDInventoryRequest::Admit(O->Inventory,R.Command,R.InventoryRevision,R.ItemRevision,Facts);
        if(Admission!=LDInventoryRequest::EResult::Accepted){Refuse(Admission);return;}
        FString Why;ALDCharacter* Body=nullptr;if(!Eligible(PC,Body,Why)){Reply(PC,R.Id,Why);return;}
        // Server diagnostics stay in the fault record; the owner only receives a plain refusal.
        auto Fault=[&](const FString& Diagnostic){const FString Record=TEXT("Inventory drop ")+Item->Identity.ToString()+TEXT(": ")+Diagnostic;UE_LOG(LogTemp,Error,TEXT("LD_INVENTORY_DROP_FAULT %s"),*Record);M->ReportAuthorityFault(M->GetMatchContext(),Record);Reject(TEXT("Drop unavailable: world authority fault"));};
        if(FVector::Dist2D(Body->GetActorLocation(),R.Point)>80){Reject(TEXT("No legal drop ground within 80 cm"));return;}
        const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();const FLDDemoWorldGeometry* Geometry=Registry&&Registry->Enabled()?Registry->GetGeometry():nullptr;
        FVector Floor;FString Error;
        if(!M->Vision||!Geometry||Geometry->Surfaces.Num()!=1||!Geometry->Surfaces[0].MinCm.ToVector(Floor,Error)){Fault(TEXT("registered world or sight unavailable ")+Error);return;}
        // The requested XY is client input: its registered-ground failures are ordinary refusals, never faults.
        FLDLegalGround Ground;ELDWorldQueryFailure GroundFailure=ELDWorldQueryFailure::None;
        const bool bLegal=Registry->QueryGround(Registry->Revision(),FVector(R.Point.X,R.Point.Y,Floor.Z),Body->GetCapsuleComponent()->GetCollisionShape(),FName(*Geometry->Surfaces[0].Id),Ground,GroundFailure,Error);
        const auto GroundCheck=LDInventoryDrop::FromGround(bLegal,GroundFailure);
        if(GroundCheck==LDInventoryDrop::ECheck::Fault){Fault(Error);return;}
        if(GroundCheck!=LDInventoryDrop::ECheck::Accept){Reject(TEXT("No legal drop ground within 80 cm"));return;}
        // Sight from the actual body to that validated ground follows observation's geometry and fault rule.
        const ELDSightResult Sight=M->Vision->TraceSight(Body->GetActorLocation()-FVector(0,0,Body->GetSimpleCollisionHalfHeight()),Ground.FootCm,Registry->Revision());
        const auto SightCheck=LDInventoryDrop::FromSight(Sight);
        if(SightCheck==LDInventoryDrop::ECheck::Fault){Fault(FString::Printf(TEXT("sight result %d"),int32(Sight)));return;}
        if(SightCheck!=LDInventoryDrop::ECheck::Accept){Reject(TEXT("No legal drop ground within 80 cm"));return;}
        Item->SetActorLocation(Ground.SurfaceCm+FVector(0,0,16));Remove(PC,Item,false);Reply(PC,R.Id,TEXT("Item dropped"));return;
    }
    Reject(TEXT("Inventory action unavailable"));
}
