#include "Economy/LDB92Economy.h"
#include "Combat/LDTacticalCatalog.h"
#include "Economy/LDEconomyDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Engine/World.h"
#include "Population/LDB92Roster.h"
#include "World/LDWorldObjects.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"

bool FLDB92Trade::operator==(const FLDB92Trade& B) const
{
    return Request==B.Request&&Contender==B.Contender&&Item==B.Item&&Market==B.Market&&Kind==B.Kind&&Buy==B.Buy
        &&QuoteRevision==B.QuoteRevision&&WalletRevision==B.WalletRevision&&ItemRevision==B.ItemRevision&&InventoryRevision==B.InventoryRevision&&Price==B.Price;
}
ALDB92Economy::ALDB92Economy(){bReplicates=false;PrimaryActorTick.bCanEverTick=false;}
void ALDB92Economy::EndPlay(const EEndPlayReason::Type Reason)
{for(auto P:Custody){if(IsValid(P)){P->Destroy();}}Custody.Reset();Super::EndPlay(Reason);}
FName ALDB92Economy::Wallet(FGuid Id){return ALDEconomyDirector::Wallet(Id);}
FName ALDB92Economy::Bank(FGuid Id){return ALDEconomyDirector::BankAccount(Id);}

bool ALDB92Economy::Initialize(FGuid Match,const TArray<FGuid>& Contenders,const TArray<FGuid>& Civilians,FString& Error,bool IncludeWorld)
{
    Error.Reset();auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    if(!HasAuthority()||Ready||!Match.IsValid()||Contenders.Num()!=24||Civilians.Num()!=96){return Fail(TEXT("B92 requires authority, one fresh match, 24 persistent contender IDs and 96 civilian IDs"));}
    TSet<FGuid> Ids;TMap<FName,int64> Accounts;TMap<FName,int32> Budgets;
    for(const auto& Group:{Contenders,Civilians}){for(FGuid Id:Group){if(!Id.IsValid()||Ids.Contains(Id)){return Fail(TEXT("Roster IDs must be valid, unique and disjoint"));}Ids.Add(Id);}}
    LDEconomy::FCatalog C;if(!C.LoadFull(Error)){return false;}
    for(FGuid Id:Contenders)
    {
        Accounts.Add(Wallet(Id),0);Accounts.Add(Bank(Id),0);
        const FString S=Id.ToString(EGuidFormats::Digits);
        Budgets.Add(FName(*(TEXT("Courier:")+S)),C.CourierLimit);Budgets.Add(FName(*(TEXT("CourierIssuance:")+S)),C.CourierIssuanceLimit);
        Budgets.Add(FName(*(TEXT("Core:")+S)),3);Budgets.Add(FName(*(TEXT("Supply:")+S)),1);
    }
    for(FGuid Id:Civilians){Accounts.Add(Wallet(Id),3);}
    Budgets.Add(TEXT("CoreOrders"),72);Budgets.Add(TEXT("SupplyOrders"),24);Budgets.Add(TEXT("ShortageOrder"),1);
    Budgets.Add(TEXT("ProducerRiverport"),29);Budgets.Add(TEXT("ProducerIronworks"),29);
    for(const auto& M:C.Markets){Accounts.Add(M.Id,M.Gold);}
    if(IncludeWorld){const auto* Skills=ULDTacticalCatalog::Load(8);if(!Skills){return false;}for(const auto& D:Skills->Abilities){if(D.Circle==3){Budgets.Add(BookBudget(D.Skill),2);}}}
    LDEconomy::FLedger Candidate;if(!Candidate.Initialize(Match,Accounts,Budgets,Error)){return false;}
    LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("B92MarketStockEndowment");
    TArray<TObjectPtr<ALDItem>> Created;
    auto Cleanup=[&](){for(auto P:Created){if(P&&P->Holder==this){P->Destroy();}}};
    for(const auto& M:C.Markets){for(const auto& G:M.Goods){for(int32 I=0;I<G.Target;++I)
    {
        auto* P=GetWorld()->SpawnActor<ALDItem>();if(!P){Cleanup();return Fail(TEXT("Market stock allocation failed; no accounts or partial endowment published"));}
        Created.Add(P);P->Kind=G.Kind;P->State=1;P->Holder=this;P->Institution=M.Id;P->OriginReceipt=T.Id;
        LDEconomy::FItemDelta D;D.Id=P->Identity;D.Kind=G.Kind;D.Op=LDEconomy::EItemOp::Create;D.To=M.Id;T.Items.Add(D);
    }}}
    FullWorld=IncludeWorld;
    if(!Candidate.Commit(T,Error)||(FullWorld&&!ImportWorld(Candidate,Created,Error))||!Candidate.Reconcile(Error)){Cleanup();FullWorld=false;return false;}
    Catalog=MoveTemp(C);Ledger=MoveTemp(Candidate);Custody=MoveTemp(Created);Seats.Reset();for(FGuid Id:Contenders){Seats.Add(Id);}
    for(const auto& M:Catalog.Markets){MarketRevisions.Add(M.Id,1);}Schedule=FGuid::NewGuid();Ready=true;
    UE_LOG(LogTemp,Display,TEXT("LD_B92_ECONOMY_INITIALIZED gold=%lld items=%d contenders=%d civilianAccounts=%d nativeServiceAdmission=pending"),Ledger.TotalGold(),Custody.Num(),Seats.Num(),Civilians.Num());
    return true;
}
FName ALDB92Economy::MarketFor(uint8 Service) const
{return Service>0&&Service<=Catalog.Markets.Num()?Catalog.Markets[Service-1].Id:NAME_None;}
TArray<ALDItem*> ALDB92Economy::Stock(FName Market,ELDItemKind Kind) const
{
    TArray<ALDItem*> Out;for(auto P:Custody){if(IsValid(P)&&P->State==1&&P->Holder==this&&P->Institution==Market&&P->Kind==Kind){Out.Add(P);}}
    Out.Sort([](const ALDItem& A,const ALDItem& B){return A.Identity.ToString()<B.Identity.ToString();});return Out;
}
FLDB92MarketQuote ALDB92Economy::Quote(FName Market) const
{
    FLDB92MarketQuote Q;Q.Market=Market;
    const auto* Definition=Catalog.Markets.FindByPredicate([&](const auto& M){return M.Id==Market;});const auto* Account=Ledger.Account(Market);
    if(!Ready||Closed||!Definition||!Account){return Q;}
    const bool IsShortageMarket=Market==MarketFor(Catalog.ShortageService);
    const uint32 Phase=IsShortageMarket?(LastSample>=Catalog.ShortageEnd?2:LastSample>=Catalog.ShortageStart?1:0):0;
    Q.Revision=(uint64(MarketRevisions.FindRef(Market))+Account->Revision-1)*4+Phase;Q.Cash=Account->Gold;
    for(const auto& G:Definition->Goods)
    {
        const int32 S=Stock(Market,G.Kind).Num();const bool Shortage=Phase==1&&G.Kind==ELDItemKind::Toolkit;
        const int32 N=Shortage?Catalog.ShortageNumerator:1,D=Shortage?Catalog.ShortageDenominator:1;
        Q.Goods.Add({G.Kind,S,G.Cap,LDEconomy::Ask(G,S,N,D),LDEconomy::Bid(G,S,N,D)});
    }
    return Q;
}
bool ALDB92Economy::Trade(const FLDB92Trade& R,FGuid& Received,FString& Error)
{
    Error.Reset();Received.Invalidate();auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    if(!HasAuthority()||!Ready||!R.Request.IsValid()||!Seats.Contains(R.Contender)){return Fail(TEXT("Trade requires an admitted persistent contender and authority"));}
    if(const auto* Old=Replays.Find(R.Request)){if(!(Old->Request==R)){return Fail(TEXT("Receipt reused with different terms"));}Received=Old->Received;return true;}
    if(Closed||Ledger.Receipt(R.Request)){return Fail(TEXT("Match closed or receipt already belongs to another event"));}
    if(!SynchronizeWorld(Error)||!Audit(Error)){return false;}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();ALDPlayerController* Buyer=nullptr;FLDOwnerState* BuyerState=nullptr;
    if(FullWorld&&M&&M->Population){for(const auto& Seat:M->Population->Seats()){if(Seat.Id==R.Contender){Buyer=Seat.Controller.Get();break;}}BuyerState=M->StateFor(Buyer);}
    if(FullWorld&&(!Buyer||!BuyerState||!BuyerState->bActive||BuyerState->Inventory.Revision!=R.InventoryRevision)){return Fail(TEXT("BuyerState inventory changed"));}
    const auto Q=Quote(R.Market);const auto* Offer=Q.Goods.FindByPredicate([&](const auto& G){return G.Kind==R.Kind;});
    const FName W=Wallet(R.Contender);const auto* A=Ledger.Account(W);const auto* Market=Ledger.Account(R.Market);
    if(!Offer||!Q.Revision||Q.Revision!=R.QuoteRevision||R.Price!=(R.Buy?Offer->Ask:Offer->Bid)||!A||!Market||A->Revision!=R.WalletRevision)
    {return Fail(TEXT("Quote, item class or wallet changed; confirm current unit terms"));}
    if(MarketRevisions.FindRef(R.Market)==MAX_uint32){return Fail(TEXT("Market revision exhausted"));}
    ALDItem* Physical=nullptr;
    if(R.Buy)
    {
        if(R.Item.IsValid()||R.ItemRevision||Offer->Stock<=0||A->Gold<R.Price){return Fail(TEXT("Buy needs stock and carried gold; client cannot choose an unseen stock identity"));}
        Physical=Stock(R.Market,R.Kind)[0];
    }
    else
    {
        for(auto P:Custody){if(P&&P->Identity==R.Item){Physical=P;break;}}
        if(!Physical||Physical->Kind!=R.Kind||Physical->State!=1||(FullWorld?Physical->Holder!=Buyer:Physical->Holder!=this||Physical->Institution!=W)||Physical->Revision!=R.ItemRevision||Physical->bReserved||Physical->BindingMask
            ||Offer->Stock>=Offer->Cap||Market->Gold<R.Price){return Fail(TEXT("Sell needs exact available owned commodity, stock capacity and funded market"));}
    }
    if(FullWorld&&R.Buy&&!LDInventory::Fits(BuyerState->Inventory,BuyerState->Build,{}, {R.Kind},&Error)){return false;}
    const auto* I=Ledger.Item(Physical->Identity);
    if(!I||I->Consumed||I->Location!=(R.Buy?R.Market:W)||Physical->Revision==MAX_uint32){return Fail(TEXT("Stock lineage or physical revision changed"));}
    LDEconomy::FTransaction T;T.Id=R.Request;T.Cause=R.Buy?TEXT("B92MarketBuy"):TEXT("B92MarketSell");
    T.Gold={{W,A->Revision,R.Buy?-int64(R.Price):int64(R.Price)},{R.Market,Market->Revision,R.Buy?int64(R.Price):-int64(R.Price)}};
    LDEconomy::FItemDelta D;D.Id=Physical->Identity;D.Kind=Physical->Kind;D.Op=LDEconomy::EItemOp::Transfer;D.Expected=I->Revision;D.From=I->Location;D.To=R.Buy?W:R.Market;T.Items.Add(D);
    if(!Ledger.Commit(T,Error)){return false;}
    Physical->Institution=D.To;++Physical->Revision;
    if(FullWorld)
    {
        if(R.Buy){uint8 Slot=0;while(BuyerState->Inventory.Items.ContainsByPredicate([&](const auto& X){return X.Slot==Slot;})){++Slot;}Physical->Holder=Buyer;Physical->Institution=NAME_None;Physical->Slot=Slot;FLDInventoryItem Entry;Physical->Project(Entry);BuyerState->Inventory.Items.Add(Entry);++BuyerState->Inventory.Revision;}
        else{LDInventory::Remove(BuyerState->Inventory,Physical->Identity);Physical->Holder=this;Physical->Slot=0;}
        BuyerState->Gold=Ledger.Account(W)->Gold;M->Inventory->Changed(Buyer);
    }
    ++MarketRevisions.FindChecked(R.Market);Received=R.Buy?Physical->Identity:FGuid();Replays.Add(R.Request,{R,Received});return true;
}
bool ALDB92Economy::ApplyInstitution(int32 Tick,int32 Row,FString& Error)
{
    const auto& Rule=Catalog.Institutions[Row];const FName Market=MarketFor(Rule.Service);const auto* A=Ledger.Account(Market);auto Items=Stock(Market,Rule.Kind);
    const auto* Def=Catalog.Markets.FindByPredicate([&](const auto& M){return M.Id==Market;});const auto* G=Def?Def->Goods.FindByPredicate([&](const auto& Good){return Good.Kind==Rule.Kind;}):nullptr;
    FLDB92InstitutionEvent Event;Event.At=Catalog.InstitutionTimes[Tick];Event.Row=Row;
    if(!A||!G||MarketRevisions.FindRef(Market)==MAX_uint32){Error=TEXT("Institution market missing or revision exhausted");return false;}
    if(Rule.Producer&&(A->Gold<Rule.Cost||Items.Num()>=G->Cap))
    {Event.Status=A->Gold<Rule.Cost?TEXT("InsufficientCash"):TEXT("StockFull");Events.Add(Event);return true;}
    if(!Rule.Producer&&Items.IsEmpty()){Event.Status=TEXT("Empty");Events.Add(Event);return true;}
    LDEconomy::FTransaction T;T.Id=FGuid(Schedule.A,Schedule.B,uint32(Tick+1),uint32(Row+1));T.Cause=Rule.Producer?TEXT("B92InstitutionProduction"):TEXT("B92InstitutionConsumption");
    ALDItem* P=nullptr;LDEconomy::FItemDelta D;D.Kind=Rule.Kind;
    if(Rule.Producer)
    {
        P=GetWorld()->SpawnActor<ALDItem>();if(!P){Event.Status=TEXT("AllocationFailed");Events.Add(Event);return true;}
        P->Kind=Rule.Kind;T.Burn=Rule.Cost;T.Gold.Add({Market,A->Revision,-int64(Rule.Cost)});T.Budget=Rule.Service==4?TEXT("ProducerRiverport"):TEXT("ProducerIronworks");T.Units=1;
        D.Id=P->Identity;D.Op=LDEconomy::EItemOp::Create;D.To=Market;
    }
    else
    {P=Items[0];const auto* I=Ledger.Item(P->Identity);if(!I||I->Consumed){Error=TEXT("Consumed stock lacks lineage");return false;}D.Id=P->Identity;D.Op=LDEconomy::EItemOp::Consume;D.Expected=I->Revision;D.From=Market;}
    T.Items.Add(D);if(!Ledger.Commit(T,Error)){if(Rule.Producer){P->Destroy();}return false;}
    if(Rule.Producer){P->Holder=this;P->Institution=Market;P->State=1;P->OriginReceipt=T.Id;Custody.Add(P);}
    else{Custody.Remove(P);P->State=2;P->Holder.Reset();if(FullWorld){ALDWorldObjects::Get(GetWorld())->RecordConsumption(P);}P->Destroy();}
    ++MarketRevisions.FindChecked(Market);Event.Status=TEXT("Committed");Event.Receipt=T.Id;Event.Item=P->Identity;Events.Add(Event);return true;
}
bool ALDB92Economy::Sample(double Elapsed,FString& Error)
{
    Error.Reset();if(!HasAuthority()||!Ready||!FMath::IsFinite(Elapsed)||Elapsed<LastSample){Error=TEXT("Institution clock must be authoritative, finite and monotonic");return false;}
    if(Closed){return true;}if(!SynchronizeWorld(Error)||!Audit(Error)){return false;}LastSample=Elapsed;
    if(Elapsed>=Catalog.Duration){Closed=true;return true;}
    while(Cursor<Catalog.InstitutionTimes.Num()&&Elapsed>=Catalog.InstitutionTimes[Cursor])
    {
        const int32 Tick=Cursor++;
        // A sample after the following interval cannot execute the old work.
        if(Elapsed>=Catalog.InstitutionTimes[Tick]+180)
        {for(int32 Row=0;Row<Catalog.Institutions.Num();++Row){Events.Add({Catalog.InstitutionTimes[Tick],Row,TEXT("Missed"),FGuid(),FGuid()});}continue;}
        for(int32 Row=0;Row<Catalog.Institutions.Num();++Row){if(!ApplyInstitution(Tick,Row,Error)){Closed=true;return false;}}
    }
    return true;
}
bool ALDB92Economy::Audit(FString& Error) const
{
    if(!Ready||!Ledger.Reconcile(Error)){if(Error.IsEmpty()){Error=TEXT("B92 economy not initialized");}return false;}
    TSet<FGuid> Seen;
    for(auto P:Custody)
    {
        const auto* I=IsValid(P)?Ledger.Item(P->Identity):nullptr;
        if(!I||I->Consumed||Seen.Contains(P->Identity)||(FullWorld?PhysicalLocation(P,Error)!=I->Location:P->State!=1||P->Holder!=this||P->Institution!=I->Location)||P->Kind!=I->Kind||P->OriginReceipt!=I->Origin||P->ParentIds!=I->Parents||P->Quality!=I->Quality||uint8(P->Skill)!=I->Skill)
        {Error=TEXT("B92 physical custody/lineage mismatch");return false;}Seen.Add(P->Identity);
    }
    int32 Live=0;for(const auto& Pair:Ledger.ItemRecords()){if(!Pair.Value.Consumed){++Live;}}
    if(Live!=Seen.Num()){Error=TEXT("B92 live stock has no exact physical projection");return false;}
    return true;
}
