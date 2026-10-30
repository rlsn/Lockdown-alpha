#include "Economy/LDB92Services.h"
#include "Economy/LDB92Economy.h"
#include "Population/LDB92Roster.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDVendorDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Engine/World.h"

TArray<FName> ALDB92Services::Endpoints()
{
    auto Out=Markets();
    Out.Append({TEXT("capital_supply"),TEXT("highfort_supply"),TEXT("scholars_supply"),TEXT("riverport_supply"),TEXT("ironworks_supply"),TEXT("highfort_barracks"),TEXT("scholars_archive"),TEXT("capital_workshop"),TEXT("highfort_workshop"),TEXT("scholars_workshop"),TEXT("riverport_workshop"),TEXT("ironworks_workshop"),TEXT("oakmere"),TEXT("millford"),TEXT("reedbank"),TEXT("ashcross"),TEXT("bellwick"),TEXT("stonewake"),TEXT("capital_guild"),TEXT("riverport_guild")});return Out;
}
bool ALDB92Services::Official(FName POI)
{return POI==TEXT("capital_supply")||POI==TEXT("highfort_supply")||POI==TEXT("scholars_supply")||POI==TEXT("riverport_supply")||POI==TEXT("ironworks_supply")||POI==TEXT("highfort_barracks")||POI==TEXT("scholars_archive");}
FName ALDB92Economy::BookBudget(ELDSkill Skill)
{return FName(*FString::Printf(TEXT("B92OfficialBook%d"),uint8(Skill)));}
TArray<FLDB92Offer> ALDB92Services::SupplyOffers(FName POI,const ALDB92Economy* E)
{
    TArray<FLDB92Offer> Out;if(!Official(POI)||!E){return Out;}
    const bool Supply=POI.ToString().EndsWith(TEXT("_supply"));
    const int32 Minimum=Supply?1:2,Maximum=Supply?1:POI==TEXT("scholars_archive")?3:2;
    auto Add=[&](ELDItemKind Kind,ELDSkill Skill,int32 Price,int32 Stock=-1){FLDB92Offer O;O.Kind=Kind;O.Skill=Skill;O.Ask=Price;O.Stock=Stock;Out.Add(O);};
    if(Supply)
    {
        Add(ELDItemKind::Fiber,ELDSkill::None,4);Add(ELDItemKind::Ore,ELDSkill::None,6);
        if(const auto* C=ULDEquipmentCatalog::Load(8)){for(const auto& D:C->Items){if(D.Price>0&&D.Kind!=ELDItemKind::Book&&D.Kind!=ELDItemKind::Fiber&&D.Kind!=ELDItemKind::Ore){Add(D.Kind,ELDSkill::None,D.Price);}}}
    }
    if(const auto* C=ULDTacticalCatalog::Load(8))
    {
        if(Supply){for(auto Skill:{ELDSkill::Step,ELDSkill::Spark,ELDSkill::Patch,ELDSkill::Parley}){Add(ELDItemKind::Book,Skill,40);}}
        for(const auto& D:C->Abilities){if(D.Circle>=Minimum&&D.Circle<=Maximum){Add(ELDItemKind::Book,D.Skill,D.Circle==1?40:D.Circle==2?60:90,D.Circle==3?E->Accounting().Remaining(ALDB92Economy::BookBudget(D.Skill)):-1);}}
    }
    return Out;
}
bool ALDB92Economy::PurchaseOfficial(ALDB92Controller* PC,FName POI,FGuid Receipt,const FLDB92MarketOrder& R,FString& Error)
{
    Error.Reset();auto Reject=[&](const TCHAR* Why){Error=Why;return false;};
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(PC):nullptr;
    if(!HasAuthority()||!Ready||Closed||!FullWorld||!O||!O->bActive||!Seats.Contains(M->ContenderId(PC))||!Receipt.IsValid()||Ledger.Receipt(Receipt)||!R.Supply||!R.Buy||R.BankAction||R.Amount||R.BankRevision||R.Item.IsValid()||R.ItemRevision||OfficialRevision==MAX_uint32||O->Inventory.Revision==MAX_uint32)
    {return Reject(TEXT("Official purchase unavailable or invalid terms"));}
    if(!SynchronizeWorld(Error)||!Audit(Error)){return false;}
    const auto Offers=ALDB92Services::SupplyOffers(POI,this);const auto* Offer=Offers.FindByPredicate([&](const auto& V){return V.Kind==R.Kind&&V.Skill==R.Skill;});
    const auto W=Wallet(M->ContenderId(PC));const auto* Account=Ledger.Account(W);
    if(!Offer||Offer->Stock==0||Offer->Ask!=R.Price||R.Revision!=OfficialRevision||O->Inventory.Revision!=R.InventoryRevision||!Account||Account->Revision!=R.WalletRevision||Account->Gold!=O->Gold||Account->Gold<R.Price)
    {return Reject(TEXT("Stock, price, wallet or bag changed; review official terms"));}
    if(!LDInventory::Fits(O->Inventory,O->Build,{}, {R.Kind},&Error)){return false;}
    ALDItem* P=R.Kind==ELDItemKind::Book?GetWorld()->SpawnActor<ALDBook>():GetWorld()->SpawnActor<ALDItem>();if(!P){return Reject(TEXT("Allocation failed; nothing spent"));}
    P->Kind=R.Kind;P->Skill=R.Skill;P->OriginReceipt=Receipt;
    LDEconomy::FTransaction T;T.Id=Receipt;T.Cause=TEXT("B92OfficialPurchase");T.Burn=R.Price;T.Gold.Add({W,Account->Revision,-int64(R.Price)});
    if(Offer->Stock>=0){T.Budget=BookBudget(R.Skill);T.Units=1;}
    LDEconomy::FItemDelta D;D.Id=P->Identity;D.Kind=R.Kind;D.Skill=uint8(R.Skill);D.Op=LDEconomy::EItemOp::Create;D.To=W;T.Items.Add(D);
    if(!Ledger.Commit(T,Error)){P->Destroy();return false;}
    uint8 Slot=0;while(O->Inventory.Items.ContainsByPredicate([&](const auto& I){return I.Slot==Slot;})){++Slot;}
    P->Holder=PC;P->State=1;P->Slot=Slot;++P->Revision;FLDInventoryItem Entry;P->Project(Entry);O->Inventory.Items.Add(Entry);++O->Inventory.Revision;Custody.Add(P);O->Gold=Ledger.Account(W)->Gold;++OfficialRevision;M->Inventory->Changed(PC);return true;
}
