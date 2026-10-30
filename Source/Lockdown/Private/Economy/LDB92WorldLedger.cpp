#include "Economy/LDB92Economy.h"
#include "World/LDWorldObjects.h"
#include "Framework/LDGameMode.h"
#include "Inventory/LDInventoryDirector.h"
#include "Engine/World.h"
#include "EngineUtils.h"

FName ALDB92Economy::PhysicalLocation(const ALDItem* P,FString& Error) const
{
    if(!P||P->State>1){Error=TEXT("Physical item is not live");return NAME_None;}
    if(P->State==0){if(P->Holder.IsValid()){Error=TEXT("Ground item still has a holder");return NAME_None;}return TEXT("World");}
    if(P->Holder==this){return P->Institution;}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(auto* PC=Cast<ALDPlayerController>(P->Holder.Get()))
    {
        const auto Id=M->ContenderId(PC);const auto* O=M->StateFor(PC);const auto* I=O?O->Inventory.Find(P->Identity):nullptr;
        if(!Seats.Contains(Id)||!I||I->Kind!=P->Kind||I->Skill!=P->Skill||I->Revision!=P->Revision||I->Slot!=P->Slot||I->Quality!=P->Quality||I->BindingMask!=P->BindingMask||I->bReserved!=P->bReserved)
        {Error=TEXT("Physical owner/bag projection differs");return NAME_None;}return Wallet(Id);
    }
    if(auto* O=Cast<ALDWorldObject>(P->Holder.Get());O&&O->Contents.ContainsByPredicate([&](auto I){return I.Get()==P;})&&!O->SourceId.IsNone())
    {return FName(*(TEXT("Object:")+O->SourceId.ToString()));}
    Error=TEXT("Unsupported or missing physical item holder");return NAME_None;
}
bool ALDB92Economy::ImportWorld(LDEconomy::FLedger& Candidate,TArray<TObjectPtr<ALDItem>>& Items,FString& Error)
{
    auto* Objects=ALDWorldObjects::Get(GetWorld());
    if(!Objects||!Objects->Ready||Objects->Finite.Num()!=143||!Objects->Consumed.IsEmpty()||Objects->bCheckpointRestored)
    {Error=TEXT("Full economy requires the fresh finite B91 world, not a partial personal/economy restore");return false;}
    for(const auto& Pair:Objects->Finite)
    {
        auto* P=Pair.Value.Get();if(!P||!P->OriginReceipt.IsValid()||Candidate.Receipt(P->OriginReceipt)){Error=TEXT("Initial world source has invalid/duplicate provenance");return false;}
        const FName Location=PhysicalLocation(P,Error);if(Location.IsNone()){return false;}
        LDEconomy::FTransaction T;T.Id=P->OriginReceipt;T.Cause=TEXT("B92WorldEndowment");LDEconomy::FItemDelta D;D.Id=P->Identity;D.Kind=P->Kind;D.Skill=uint8(P->Skill);D.Quality=P->Quality;D.Op=LDEconomy::EItemOp::Create;D.To=Location;T.Items.Add(D);
        if(!Candidate.Commit(T,Error)){return false;}Items.Add(P);
    }
    return true;
}
void ALDB92Economy::RecordConsumption(ALDItem* P)
{if(Ready&&FullWorld&&P&&Ledger.Item(P->Identity)){ConsumedWorld.Add(P->Identity);}}
bool ALDB92Economy::SynchronizeWorld(FString& Error)
{
    Error.Reset();if(!FullWorld){return true;}if(!Ready||!HasAuthority()){Error=TEXT("World accounting requires initialized authority");return false;}
    auto* Objects=ALDWorldObjects::Get(GetWorld());if(!Objects){Error=TEXT("World source registry missing");return false;}
    TMap<FGuid,ALDItem*> Live;
    for(TActorIterator<ALDItem> It(GetWorld());It;++It)
    {
        auto* P=*It;if(P->State==2||P->IsActorBeingDestroyed()){continue;}
        if(Live.Contains(P->Identity)){Error=TEXT("Duplicate physical identity");return false;}Live.Add(P->Identity,P);
        if(!Ledger.Item(P->Identity))
        {
            const auto* Tree=Objects->Objects.FindByPredicate([&](auto O){return O.IsValid()&&O->Kind==ELDObjectKind::Sapling&&O->Broken&&O->HarvestReceipt==P->OriginReceipt&&O->YieldIdentity==P->Identity;});
            if(P->Kind!=ELDItemKind::Fiber||!Tree||!Objects->Finite.Contains(P->Identity)||!P->OriginReceipt.IsValid()||Ledger.Receipt(P->OriginReceipt))
            {Error=TEXT("Unclassified world item creation");return false;}
            const FName Location=PhysicalLocation(P,Error);if(Location.IsNone()){return false;}
            LDEconomy::FTransaction T;T.Id=P->OriginReceipt;T.Cause=TEXT("B92SaplingHarvest");LDEconomy::FItemDelta D;D.Id=P->Identity;D.Kind=P->Kind;D.Op=LDEconomy::EItemOp::Create;D.To=Location;T.Items.Add(D);
            if(!Ledger.Commit(T,Error)){return false;}Custody.Add(P);
        }
    }
    LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("B92WorldProjection");
    for(const auto& Pair:Ledger.ItemRecords())
    {
        const auto& Old=Pair.Value;if(Old.Consumed){if(Live.Contains(Pair.Key)){Error=TEXT("Consumed identity reappeared");return false;}continue;}
        auto* const* Found=Live.Find(Pair.Key);LDEconomy::FItemDelta D;D.Id=Pair.Key;D.Kind=Old.Kind;D.Expected=Old.Revision;D.From=Old.Location;
        if(!Found)
        {if(!ConsumedWorld.Contains(Pair.Key)){Error=TEXT("Item disappeared without a consume boundary");return false;}D.Op=LDEconomy::EItemOp::Consume;T.Items.Add(D);continue;}
        const auto* P=*Found;const FName Location=PhysicalLocation(P,Error);if(Location.IsNone()){return false;}
        if(P->Kind!=Old.Kind||uint8(P->Skill)!=Old.Skill||P->Quality!=Old.Quality||P->OriginReceipt!=Old.Origin||P->ParentIds!=Old.Parents){Error=TEXT("Physical lineage changed");return false;}
        if(Location!=Old.Location){D.Op=LDEconomy::EItemOp::Transfer;D.To=Location;T.Items.Add(D);}
    }
    if(!T.Items.IsEmpty()&&!Ledger.Commit(T,Error)){return false;}
    Custody.RemoveAll([&](auto P){return !IsValid(P)||P->State==2||P->IsActorBeingDestroyed();});ConsumedWorld.Reset();return true;
}
