#include "Economy/LDB92Economy.h"
#include "Economy/LDB92Services.h"
#include "Population/LDB92Roster.h"
#include "Engine/World.h"

bool ALDB92Economy::PurchaseCharter(ALDB92Controller* PC,FName POI,const FLDB92MarketOrder& R,FGuid Receipt,FString& Error)
{
    auto Reject=[&](const TCHAR* Why){Error=Why;return false;};auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(PC):nullptr;
    if(O&&O->CharterPurchase.IsValid()){return Reject(TEXT("Registered Charter already owned; nothing charged"));}
    if(!HasAuthority()||!Ready||!FullWorld||Closed||!O||!O->bActive||!Seats.Contains(M->ContenderId(PC))||POI!=TEXT("capital_treasury")||!R.Charter||R.WorkRecipe||R.CourierAction||R.CourierRevision||R.CourierToken.IsValid()||!R.Destination.IsNone()||R.Supply||R.BankAction||!R.Inputs.IsEmpty()||R.Item.IsValid()||R.ItemRevision||R.Skill!=ELDSkill::None||!R.Buy||R.Price!=100||R.Revision||R.Amount<0||R.Amount>100||R.InventoryRevision!=O->Inventory.Revision||!Receipt.IsValid()||Ledger.Receipt(Receipt)||!M->IsPlaying()||GetWorld()->GetTimeSeconds()+94>M->GetGameState<ALDGameState>()->MatchDeadline)
    {return Reject(TEXT("Registered Charter needs current Treasury terms and94 seconds remaining"));}
    auto Proof=[&](FGuid Id,FName Cause){const auto* P=Ledger.Receipt(Id);return P&&P->Transaction.Cause==Cause;};
    if(!Proof(O->Witness,TEXT("B92CharterWitness"))||!Proof(O->IndustrialEndorsement,TEXT("B92CoreProcurement"))||!Proof(O->SupplyEndorsement,TEXT("B92SupplyProcurement")))
    {return Reject(TEXT("Charter needs real Industrial, Supply and Witness receipts"));}
    if(!SynchronizeWorld(Error)||!Audit(Error)){return false;}const auto Id=M->ContenderId(PC);const auto W=Wallet(Id),B=Bank(Id);const auto* WA=Ledger.Account(W);const auto* BA=Ledger.Account(B);
    if(!WA||!BA||WA->Revision!=R.WalletRevision||BA->Revision!=R.BankRevision||WA->Gold!=O->Gold||BA->Gold!=O->BankGold||WA->Gold<R.Amount||BA->Gold<100-R.Amount){return Reject(TEXT("Exact wallet/bank split or balances changed; nothing charged"));}
    LDEconomy::FTransaction T;T.Id=Receipt;T.Cause=TEXT("B92RegisteredCharterPurchase");T.Burn=100;if(R.Amount){T.Gold.Add({W,WA->Revision,-R.Amount});}if(R.Amount<100){T.Gold.Add({B,BA->Revision,-(100-R.Amount)});}
    if(!Ledger.Commit(T,Error)){return false;}O->Gold=Ledger.Account(W)->Gold;O->BankGold=Ledger.Account(B)->Gold;O->CharterPurchase=T.Id;O->CharterReadyAt=GetWorld()->GetTimeSeconds();M->SyncOwner(PC);Error=TEXT("Registered Charter purchased once;100 gold burned");return true;
}
