#include "Inventory/LDAcquisitionDirector.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
namespace
{
    FName Cache(uint8 Site){return FName(*FString::Printf(TEXT("B74Cache%d"),Site));}
    FName Stock(ELDSkill Skill){return FName(*FString::Printf(TEXT("B74Book%d"),uint8(Skill)));}
    void Publish(ALDGameMode* M,ALDPlayerController* PC,ALDItem* Item)
    {
        auto* O=M->StateFor(PC);uint8 Slot=0;while(O->Inventory.Items.ContainsByPredicate([&](const auto& I){return I.Slot==Slot;})){++Slot;}
        Item->State=1;Item->Holder=PC;Item->Slot=Slot;Item->bReserved=false;++Item->Revision;
        FLDInventoryItem I;I.Id=Item->Identity;I.Kind=Item->Kind;I.Skill=Item->Skill;I.Slot=Slot;I.Revision=Item->Revision;I.ReadyAt=Item->ReadyAt;I.Quality=Item->Quality;O->Inventory.Items.Add(I);++O->Inventory.Revision;
    }
}
bool ALDAcquisitionDirector::Initialize(FString& Why)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* E=M->Economy.Get();const auto* C=ULDAcquisitionCatalog::Load();
    if(!C||!C->Validate(Why)){Why=TEXT("Missing or stale acquisition catalog");return false;}
    if(E->Ledger.Remaining(TEXT("B74Endowment"))==0){return true;}
    LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("B74CacheEndowment");T.Budget=TEXT("B74Endowment");T.Units=1;TArray<ALDItem*> Created;
    const ELDSkill Skills[]={ELDSkill::TemporalLock,ELDSkill::GrandExchange,ELDSkill::GateWeave};
    for(uint8 Site=6;Site<9;++Site){for(int32 N=0;N<3;++N)
    {
        ALDItem* I=N==2?GetWorld()->SpawnActor<ALDBook>():GetWorld()->SpawnActor<ALDItem>();if(!I){for(auto* P:Created){P->Destroy();}return false;}Created.Add(I);
        I->Kind=N==0?ELDItemKind(uint8(ELDItemKind::AncientKeyA)+Site-6):N==1?ELDItemKind::ArcaneCatalyst:ELDItemKind::Book;I->Skill=N==2?Skills[Site-6]:ELDSkill::None;I->State=1;I->Holder=E;I->Institution=Cache(Site);I->OriginReceipt=T.Id;
        LDEconomy::FItemDelta D;D.Id=I->Identity;D.Kind=I->Kind;D.Skill=uint8(I->Skill);D.Op=LDEconomy::EItemOp::Create;D.To=I->Institution;T.Items.Add(D);
    }}
    if(!E->Ledger.Commit(T,Why)){for(auto* P:Created){P->Destroy();}return false;}return E->Audit(Why);
}
bool ALDAcquisitionDirector::Preview(const FLDInventory& Bag,const FLDCharacterBuild& Build,uint8 Recipe,uint8 Mode,uint8 Site,FGuid Target,FLDProductionPlan& Out,FString& Why)
{
    auto Reject=[&](const TCHAR* S){Why=S;return false;};const auto* C=ULDAcquisitionCatalog::Load();if(!C||Build.RulesVersion!=7||Mode>1){return Reject(TEXT("Acquisition profile or catalog unavailable"));}
    Out=FLDProductionPlan();Out.Site=Site;Out.Recipe=Recipe;Out.Mode=Mode;Out.Target=Target;Out.CatalogDigest=C->Checksum;Out.InventoryRevision=Bag.Revision;Out.BuildRevision=Build.Revision;
    TArray<ELDItemKind> Needs;TArray<FGuid> Removed;TArray<ELDItemKind> Incoming;int32 Base=0;
    if(Recipe>=20&&Recipe<32)
    {
        const auto& R=C->Recipes[Recipe-20];if((R.Station==0&&(Site<2||Site>4))||(R.Station==1&&Site!=4)||(R.Station==2&&Site!=5)||(Mode&& (Site!=4||R.Station==2))){return Reject(TEXT("Wrong workshop; work orders require East and cannot build T4"));}Needs=R.Inputs;Incoming.Add(R.Output);Base=R.Fee;
    }
    else if(Recipe==32||Recipe==33)
    {
        const auto* I=Bag.Find(Target);const auto* D=I?C->Find(I->Kind):nullptr;
        if(Mode||!I||!D||!D->QualityEligible||I->Quality!=Recipe-32||I->bReserved||I->BindingMask||(Recipe==32?(Site<2||Site>4):Site!=4)){return Reject(TEXT("Unbind eligible gear; quality must advance one step at the correct forge"));}
        Out.Inputs.Add(*I);Needs.Add(ELDItemKind::Toolkit);if(Recipe==33){Needs.Add(ELDItemKind::Crystal);}Base=Recipe==32?6:12;
    }
    else if(Recipe==34||Recipe==35){if(Mode||Site<6||Site>8){return Reject(TEXT("Approach a ruin cache"));}Why.Reset();return true;}
    else{return Reject(TEXT("Unsupported acquisition recipe"));}
    Out.Fee=FMath::Max(1,Base-FMath::Min(1,Build.Gear.FeeDiscount));TSet<FGuid> Used;for(const auto& I:Out.Inputs){Used.Add(I.Id);}
    // Slot order is visible and stable; reservations and equipment never disappear into inputs.
    auto Items=Bag.Items;Items.Sort([](const auto& A,const auto& B){return A.Slot<B.Slot;});
    for(auto Kind:Needs)
    {
        const auto* I=Items.FindByPredicate([&](const auto& V){return V.Kind==Kind&&!V.bReserved&&!V.BindingMask&&!Used.Contains(V.Id);});
        if(I){Out.Inputs.Add(*I);Used.Add(I->Id);Removed.Add(I->Id);}
        else{const auto* D=C->Find(Kind);if(!Mode||!D||D->Price<0){return Reject(TEXT("Supply every nonofficial input; work orders never fabricate Toolkit/Core/Crystal/catalysts"));}Out.Fee+=D->Price;}
    }
    if(!LDInventory::Fits(Bag,Build,Removed,Incoming,&Why)){return false;}Why.Reset();return true;
}
bool ALDAcquisitionDirector::Terms(ALDPlayerController* PC,const FLDProductionPlan& P,bool Reserved,FString& Why) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);const auto* C=ULDAcquisitionCatalog::Load();auto Reject=[&](const TCHAR* S){Why=S;return false;};
    if(!M->IsTacticalProfile()||!O||!C||P.CatalogDigest!=C->Checksum||P.RecipeRevision!=1||P.Inputs.Num()>4||P.Coupon.IsValid()||P.BuildRevision!=O->Build.Revision||P.InventoryRevision!=O->Inventory.Revision){return Reject(TEXT("Acquisition terms or natural/equipment build changed"));}
    if(P.Recipe==34||P.Recipe==35)
    {
        if(P.Mode||P.Fee||!P.Inputs.IsEmpty()||P.Site<6||P.Site>8){return Reject(TEXT("Invalid cache operation"));}
        if(P.Recipe==34){return true;}if(M->Economy->Ledger.Remaining(Cache(P.Site))!=0){return Reject(TEXT("Open this cache before selecting individual contents"));}
        for(TActorIterator<ALDItem> It(GetWorld());It;++It){if(It->Identity==P.Target&&It->State==1&&It->Holder==M->Economy&&It->Institution==Cache(P.Site)){return LDInventory::Fits(O->Inventory,O->Build,{}, {It->Kind},&Why);}}return Reject(TEXT("Cache item already taken"));
    }
    FLDInventory Bag=O->Inventory;TSet<FGuid> Seen;
    for(const auto& I:P.Inputs)
    {
        auto* A=M->Inventory->Owned(PC,I.Id);auto* Own=Bag.Items.FindByPredicate([&](const auto& V){return V.Id==I.Id;});
        if(!A||!Own||Seen.Contains(I.Id)||A->Kind!=I.Kind||A->Revision!=I.Revision||Own->Revision!=I.Revision||A->Quality!=I.Quality||A->BindingMask||Own->BindingMask||A->bReserved!=Reserved||Own->bReserved!=Reserved){return Reject(TEXT("Exact input changed, reserved or still equipped"));}Seen.Add(I.Id);Own->bReserved=false;
    }
    if(P.InventoryRevision!=O->Inventory.Revision){return Reject(TEXT("Inventory changed; preview again"));}
    FLDProductionPlan Check;if(!Preview(Bag,O->Build,P.Recipe,P.Mode,P.Site,P.Target,Check,Why)){return false;}
    if(Check.Fee!=P.Fee||Check.Inputs.Num()!=P.Inputs.Num()||O->Gold<P.Fee){return Reject(TEXT("Fee, funds or exact inputs changed"));}
    for(const auto& I:Check.Inputs){if(!Seen.Contains(I.Id)){return Reject(TEXT("Exact input selection changed"));}}return true;
}
bool ALDAcquisitionDirector::Commit(ALDPlayerController* PC,const FLDProductionPlan& P,FString& Why)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* E=M->Economy.Get();auto* O=M->StateFor(PC);const auto* C=ULDAcquisitionCatalog::Load();if(!Terms(PC,P,true,Why)||!E->Audit(Why)){return false;}
    if(P.Recipe==34)
    {if(E->Ledger.Remaining(Cache(P.Site))>0){LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("RuinCacheOpen");T.Budget=Cache(P.Site);T.Units=1;if(!E->Ledger.Commit(T,Why)){return false;}}CacheView(PC,P.Site);return true;}
    if(P.Recipe==35)
    {for(TActorIterator<ALDItem> It(GetWorld());It;++It){if(It->Identity!=P.Target){continue;}auto* I=*It;LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("RuinCachePickup");const auto* L=E->Ledger.Item(I->Identity);LDEconomy::FItemDelta D;D.Id=I->Identity;D.Kind=I->Kind;D.Op=LDEconomy::EItemOp::Transfer;D.Expected=L->Revision;D.From=L->Location;D.To=E->Wallet(M->ContenderId(PC));T.Items.Add(D);if(!E->Ledger.Commit(T,Why)){return false;}Publish(M,PC,I);M->Inventory->Changed(PC);CacheView(PC,P.Site);return E->Audit(Why);}return false;}
    const FName W=E->Wallet(M->ContenderId(PC));const auto* Account=E->Ledger.Account(W);if(!Account||Account->Gold!=O->Gold){return false;}
    const bool Quality=P.Recipe>=32;LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=Quality?TEXT("EquipmentQuality"):P.Mode?TEXT("EquipmentWorkOrder"):TEXT("EquipmentAssembly");T.Burn=P.Fee;T.Gold.Add({W,Account->Revision,-int64(P.Fee)});
    TArray<ALDItem*> Consumed;TArray<FGuid> Parents;TArray<ELDItemKind> Missing=Quality?TArray<ELDItemKind>{}:C->Recipes[P.Recipe-20].Inputs;int64 Endpoint=0;ALDItem* Existing=nullptr;
    for(const auto& I:P.Inputs)
    {
        auto* A=M->Inventory->Owned(PC,I.Id);const auto* L=E->Ledger.Item(I.Id);LDEconomy::FItemDelta D;D.Id=I.Id;D.Kind=I.Kind;D.Expected=L->Revision;D.From=W;
        if(Quality&&I.Id==P.Target){Existing=A;D.Op=LDEconomy::EItemOp::Quality;D.To=W;D.Quality=A->Quality+1;}
        else{D.Op=LDEconomy::EItemOp::Consume;Consumed.Add(A);Parents.Add(I.Id);if(!Quality){Missing.RemoveSingle(I.Kind);}}T.Items.Add(D);Endpoint=FMath::Max(Endpoint,A->ReadyAt);
    }
    for(auto K:Missing){LDEconomy::FItemDelta D;D.Id=FGuid::NewGuid();D.Kind=K;D.Op=LDEconomy::EItemOp::Transient;D.To=W;T.Items.Add(D);Parents.Add(D.Id);}
    ALDItem* Created=nullptr;
    if(!Quality)
    {Created=GetWorld()->SpawnActor<ALDItem>();if(!Created){Why=TEXT("Output allocation failed; nothing spent");return false;}Created->Kind=C->Recipes[P.Recipe-20].Output;Created->ReadyAt=Endpoint;Created->OriginReceipt=T.Id;Created->ParentIds=Parents;LDEconomy::FItemDelta D;D.Id=Created->Identity;D.Kind=Created->Kind;D.Op=LDEconomy::EItemOp::Create;D.To=W;D.Parents=Parents;T.Items.Add(D);}
    if(!E->Ledger.Commit(T,Why)){if(Created){Created->Destroy();}return false;}
    for(auto* I:Consumed){LDInventory::Remove(O->Inventory,I->Identity);I->State=2;I->Holder.Reset();I->Destroy();}
    if(Existing){++Existing->Quality;++Existing->Revision;Existing->bReserved=false;auto* I=O->Inventory.Items.FindByPredicate([&](const auto& V){return V.Id==Existing->Identity;});I->Quality=Existing->Quality;I->Revision=Existing->Revision;I->bReserved=false;++O->Inventory.Revision;}
    else{Publish(M,PC,Created);}O->Gold=E->Ledger.Account(W)->Gold;M->Inventory->Changed(PC);if(!E->Audit(Why)){++E->AuditFailures;return false;}return true;
}
void ALDAcquisitionDirector::Purchase(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token,uint8 Site,ELDItemKind Kind,ELDSkill Skill,uint32 BagRevision,const FString& Digest)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);auto& Client=Clients.FindOrAdd(PC);auto Reply=[&](const FString& Why){Client.Receipts.Add(Token,Why);while(Client.Receipts.Num()>32){uint32 Old=MAX_uint32;for(const auto& V:Client.Receipts){Old=FMath::Min(Old,V.Key);}Client.Receipts.Remove(Old);}M->SyncOwner(PC);PC->ClientIntentResult(Token,Why);};
    if(!Token||!O||Match!=O->Match||Life!=O->Life){return;}if(const auto* Receipt=Client.Receipts.Find(Token)){PC->ClientIntentResult(Token,*Receipt);return;}if(Token<=Client.Last){return;}Client.Last=Token;const double Now=GetWorld()->GetTimeSeconds();Client.Tokens=FMath::Min(4.,Client.Tokens+FMath::Max(0.,Now-Client.At)*4);Client.At=Now;
    const auto* C=ULDAcquisitionCatalog::Load();if(Client.Tokens<1||!M->IsTacticalProfile()||!C||Digest!=C->Checksum||BagRevision!=O->Inventory.Revision||Site<2||Site>4||!M->Production->Eligible(PC,Site,false)){Reply(TEXT("Supply terms, range, sight, life or inventory changed"));return;}--Client.Tokens;
    int32 Price=-1;FName Budget;
    if(Kind==ELDItemKind::Book)
    {const auto* G=ULDEquipmentCatalog::Load()->Gate(Skill);if(LDDevelopmentTransaction::Sold(Skill)&&Site==2){Price=40;}else if(G){if(G->Circle==1&&Site==2){Price=40;}if(G->Circle==2&&(Site==3||Site==4)){Price=60;}if(G->Circle==3&&Site==4){Price=90;Budget=Stock(Skill);}}}
    else if(Site==2){const auto* D=C->Find(Kind);if(D){Price=D->Price;}}
    FString Why;auto* E=M->Economy.Get();if(Price<0||O->Gold<Price||(!Budget.IsNone()&&E->Ledger.Remaining(Budget)<=0)||!M->Inventory->HasSpace(PC,Kind,&Why)||!E->Audit(Why)){Reply(Why.IsEmpty()?TEXT("Not sold here, insufficient wallet or global book stock exhausted"):Why);return;}
    ALDItem* I=Kind==ELDItemKind::Book?GetWorld()->SpawnActor<ALDBook>():GetWorld()->SpawnActor<ALDItem>();if(!I){Reply(TEXT("Creation failed; nothing spent"));return;}I->Kind=Kind;I->Skill=Kind==ELDItemKind::Book?Skill:ELDSkill::None;
    LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=Kind==ELDItemKind::Book?TEXT("TieredBookPurchase"):TEXT("OfficialEquipmentSupply");T.Burn=Price;const FName W=E->Wallet(M->ContenderId(PC));T.Gold.Add({W,E->Ledger.Account(W)->Revision,-int64(Price)});if(!Budget.IsNone()){T.Budget=Budget;T.Units=1;}LDEconomy::FItemDelta D;D.Id=I->Identity;D.Kind=Kind;D.Skill=uint8(I->Skill);D.Op=LDEconomy::EItemOp::Create;D.To=W;T.Items.Add(D);
    if(!E->Ledger.Commit(T,Why)){I->Destroy();Reply(Why);return;}I->OriginReceipt=T.Id;Publish(M,PC,I);O->Gold=E->Ledger.Account(W)->Gold;M->Inventory->Changed(PC);if(!E->Audit(Why)){++E->AuditFailures;}Reply(FString::Printf(TEXT("Purchased one %s for %dg; no XP"),*LDInventory::Name(Kind,Skill),Price));
}
void ALDAcquisitionDirector::CacheView(ALDPlayerController* PC,uint8 Site)
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(Site<6||Site>8||M->Economy->Ledger.Remaining(Cache(Site))!=0){return;}TArray<FLDInventoryItem> Items;for(TActorIterator<ALDItem> It(GetWorld());It;++It){if(It->State==1&&It->Holder==M->Economy&&It->Institution==Cache(Site)){FLDInventoryItem I;I.Id=It->Identity;I.Kind=It->Kind;I.Skill=It->Skill;I.Revision=It->Revision;Items.Add(I);}}PC->ClientAcquisitionCache(Site,Items);}
bool ALDAcquisitionDirector::GuardianDrops(FVector Position,FGuid DefeatReceipt,FString& Why)
{
    // This adapter is deliberately unreachable until the B76 guardian owns a legal defeat receipt.
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* E=M->Economy.Get();const auto* R=E->Ledger.Receipt(DefeatReceipt);if(!R||R->Transaction.Cause!=TEXT("GuardianDefeat")||E->Ledger.Remaining(TEXT("B74Guardian"))!=1){Why=TEXT("No unspent guardian defeat receipt");return false;}
    ALDItem* Fragment=GetWorld()->SpawnActor<ALDItem>();ALDBook* Book=GetWorld()->SpawnActor<ALDBook>();if(!Fragment||!Book){if(Fragment){Fragment->Destroy();}if(Book){Book->Destroy();}return false;}
    Fragment->Kind=ELDItemKind::HeartFragment;Book->Skill=ELDSkill::SovereignWard;LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("GuardianPhysicalDrops");T.Budget=TEXT("B74Guardian");T.Units=1;
    for(ALDItem* I:{Fragment,static_cast<ALDItem*>(Book)}){I->State=0;I->SetActorLocation(Position+FVector(0,0,12));I->OriginReceipt=T.Id;LDEconomy::FItemDelta D;D.Id=I->Identity;D.Kind=I->Kind;D.Skill=uint8(I->Skill);D.Op=LDEconomy::EItemOp::Create;D.To=TEXT("World");T.Items.Add(D);}
    if(!E->Ledger.Commit(T,Why)){Fragment->Destroy();Book->Destroy();return false;}return E->Audit(Why);
}

double ALDAcquisitionDirector::HoldSeconds(const FLDProductionPlan& P) const
{const auto* C=ULDAcquisitionCatalog::Load();if(P.Recipe>=20&&P.Recipe<32&&C){return C->Recipes[P.Recipe-20].HoldMs/1000.;}if(P.Recipe==32||P.Recipe==34){return 4;}if(P.Recipe==33){return 6;}if(P.Recipe==35){for(TActorIterator<ALDItem> It(GetWorld());It;++It){if(It->Identity==P.Target){return It->Kind>=ELDItemKind::AncientKeyA&&It->Kind<=ELDItemKind::AncientKeyC?3:.01;}}}return 0;}
bool ALDAcquisitionDirector::Quiet(const FLDProductionPlan& P) const{return P.Recipe!=35||HoldSeconds(P)>1;}
