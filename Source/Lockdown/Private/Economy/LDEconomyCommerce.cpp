#include "Economy/LDEconomyDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Engine/World.h"
#include "EngineUtils.h"

FName ALDEconomyDirector::MarketId(uint8 Service)
{static const FName Names[]={NAME_None,TEXT("City"),TEXT("WestTown"),TEXT("EastTown")};return Service<4?Names[Service]:NAME_None;}
TArray<ALDItem*> ALDEconomyDirector::MarketItems(uint8 Service,ELDItemKind Kind) const
{
    TArray<ALDItem*> Items;const FName Market=MarketId(Service);
    if(Market.IsNone()){return Items;}
    for(TActorIterator<ALDItem> It(GetWorld());It;++It){if(It->State==1&&It->Holder==this&&It->Institution==Market&&It->Kind==Kind){Items.Add(*It);}}
    Items.Sort([&](const ALDItem& A,const ALDItem& B){const auto* RA=Ledger.Receipt(A.OriginReceipt);const auto* RB=Ledger.Receipt(B.OriginReceipt);const uint64 SA=RA?RA->Serial:0,SB=RB?RB->Serial:0;return SA!=SB?SA<SB:A.Identity.ToString()<B.Identity.ToString();});return Items;
}
FLDCommerceView ALDEconomyDirector::CommerceQuote(uint8 Service) const
{
    FLDCommerceView Q;Q.Service=Service;Q.Revision=MarketRevisions.FindRef(Service);Q.QuotedAt=GetWorld()->GetTimeSeconds();if(!Initialized||Service>3){return Q;}
    if(!Service)
    {for(auto Kind:{ELDItemKind::Fiber,ELDItemKind::Ore}){FLDCommerceOffer O;O.Kind=Kind;O.Stock=O.Cap=-1;O.Ask=Kind==ELDItemKind::Fiber?Catalog.FiberPrice:Catalog.OrePrice;O.Bid=-1;Q.Offers.Add(O);}return Q;}
    const auto* Def=Catalog.Markets.FindByPredicate([&](const auto& M){return M.Id==MarketId(Service);});const auto* A=Ledger.Account(MarketId(Service));if(!Def||!A){return Q;}Q.Cash=A->Gold;const uint64 Revision=uint64(Q.Revision)+A->Revision-1;Q.Revision=Revision<MAX_uint32?uint32(Revision):0;
    for(const auto& Good:Def->Goods){FLDCommerceOffer O;O.Kind=Good.Kind;O.Stock=MarketItems(Service,Good.Kind).Num();O.Cap=Good.Cap;O.Ask=LDEconomy::Ask(Good,O.Stock);O.Bid=LDEconomy::Bid(Good,O.Stock);Q.Offers.Add(O);}return Q;
}
bool ALDEconomyDirector::CommitCommerce(ALDPlayerController* PC,uint8 Service,uint32 Revision,uint8 Command,ELDItemKind Kind,FGuid Id,uint32 ItemRevision,uint32 InventoryRevision,int32 Price,FGuid& Received,FString& Error)
{
    Error.Reset();Received.Invalidate();auto Reject=[&](const TCHAR* Why){Error=Why;return false;};auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(PC):nullptr;
    if(!Initialized||!M->IsPlaying()||!O||Service>3||(Command!=1&&Command!=2)){return Reject(TEXT("Commerce unavailable"));}
    if(!Audit(Error)){return false;}const auto Q=CommerceQuote(Service);const auto* Offer=Q.Offers.FindByPredicate([&](const auto& F){return F.Kind==Kind;});
    if(!Offer||(!Service&&Command==2)){return Reject(TEXT("This item has no Buy/Sell service here"));}
    if(Q.Revision!=Revision||Price!=(Command==1?Offer->Ask:Offer->Bid)){return Reject(TEXT("Quote changed · Review updated terms"));}
    if(!Q.Revision||Q.Revision>=MAX_uint32-2||O->Inventory.Revision!=InventoryRevision){return Reject(TEXT("Bag or market changed · Review again"));}
    const FName W=Wallet(M->ContenderId(PC)),Market=MarketId(Service);const auto* WalletState=Ledger.Account(W);const auto* MarketState=Service?Ledger.Account(Market):nullptr;
    if(!WalletState||WalletState->Gold!=O->Gold){return Reject(TEXT("Wallet changed · Review again"));}
    if(Command==1)
    {if(Service&&Offer->Stock<=0){return Reject(TEXT("Market stock empty"));}if(O->Gold<Price){return Reject(TEXT("Insufficient carried gold"));}if(!LDInventory::Fits(O->Inventory,O->Build,{}, {Kind},&Error)){return false;}if(Service&&MarketState->Gold>MAX_int64-Price){return Reject(TEXT("Market cash overflow"));}}
    else
    {if(Offer->Stock>=Offer->Cap){return Reject(TEXT("Market stock full"));}if(MarketState->Gold<Price){return Reject(TEXT("Market has insufficient cash"));}if(O->Gold>MAX_int64-Price){return Reject(TEXT("Wallet overflow"));}}
    ALDItem* Physical=nullptr;
    if(Command==2){Physical=M->Inventory->Owned(PC,Id);if(!Physical||Physical->Kind!=Kind||Physical->Revision!=ItemRevision||Physical->bReserved){return Reject(TEXT("Owned item changed or reserved"));}}
    else if(Service){const auto Stock=MarketItems(Service,Kind);if(Stock.IsEmpty()){return Reject(TEXT("Market stock empty"));}Physical=Stock[0];}
    else {
#if !UE_BUILD_SHIPPING
        if(bFailNextSupply&&AllowDiagnosticItems()){bFailNextSupply=false;return Reject(TEXT("Item allocation failed · Nothing spent"));}
#endif
        Physical=GetWorld()->SpawnActor<ALDItem>();if(!Physical){return Reject(TEXT("Item allocation failed · Nothing spent"));}Physical->Kind=Kind;}
    if(Physical->Revision==MAX_uint32){if(!Service){Physical->Destroy();}return Reject(TEXT("Item revision exhausted"));}
    LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=!Service?TEXT("OfficialMaterialPurchase"):Command==1?TEXT("MarketBuy"):TEXT("MarketSell");
    T.Gold.Add({W,WalletState->Revision,Command==1?-int64(Price):int64(Price)});
    if(Service){T.Gold.Add({Market,MarketState->Revision,Command==1?int64(Price):-int64(Price)});}else{T.Burn=Price;}
    LDEconomy::FItemDelta D;D.Id=Physical->Identity;D.Kind=Kind;D.To=Command==1?W:Market;
    if(!Service){D.Op=LDEconomy::EItemOp::Create;}else{const auto* Previous=Ledger.Item(D.Id);if(!Previous){return Reject(TEXT("Physical stock lacks journal record"));}D.Op=LDEconomy::EItemOp::Transfer;D.From=Previous->Location;D.Expected=Previous->Revision;}
    T.Items.Add(D);
    if(!Ledger.Commit(T,Error)){if(!Service){Physical->Destroy();}return false;}
    // No fallible asset allocation or callbacks remain. Apply the physical/bag
    // projection in the same authority stack before publishing the receipt.
    if(Command==1)
    {
        uint8 Slot=0;while(O->Inventory.Items.ContainsByPredicate([&](const auto& I){return I.Slot==Slot;})){++Slot;}
        Physical->Holder=PC;Physical->State=1;Physical->Institution=NAME_None;Physical->Slot=Slot;++Physical->Revision;if(!Service){Physical->OriginReceipt=T.Id;}
        FLDInventoryItem Entry;Entry.Id=D.Id;Entry.Kind=Kind;Entry.Slot=Slot;Entry.Revision=Physical->Revision;O->Inventory.Items.Add(Entry);++O->Inventory.Revision;Received=D.Id;
    }
    else{LDInventory::Remove(O->Inventory,D.Id);Physical->Holder=this;Physical->Institution=Market;Physical->State=1;Physical->Slot=0;++Physical->Revision;}
    O->Gold=Ledger.Account(W)->Gold;if(Service){++MarketRevisions.FindChecked(Service);}M->Inventory->Changed(PC);
    if(!Audit(Error)){++AuditFailures;UE_LOG(LogTemp,Error,TEXT("LD_COMMERCE_AUDIT %s"),*Error);}
    UE_LOG(LogTemp,Display,TEXT("LD_COMMERCE_RECEIPT id=%s cause=%s service=%u price=%d item=%s"),*T.Id.ToString(),*T.Cause.ToString(),Service,Price,*D.Id.ToString());return true;
}
