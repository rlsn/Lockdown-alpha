#include "Economy/LDB92Economy.h"
#include "Economy/LDB92Deposits.h"
#include "Population/LDB92Roster.h"
#include "Inventory/LDInventoryDirector.h"
#include "Engine/World.h"

bool ALDB92Economy::ExtractDeposit(ALDB92Controller* PC,int32 Node,FString& Error)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(PC):nullptr;
    if(!HasAuthority()||!Ready||!FullWorld||Closed||!O||!O->bActive||!Seats.Contains(M->ContenderId(PC))||!ALDB92Deposits::Nodes().IsValidIndex(Node)||Ledger.Remaining(ALDB92Deposits::Budget(Node))<=0||O->Inventory.Revision==MAX_uint32){Error=TEXT("Deposit depleted or extraction unavailable");return false;}
    if(!SynchronizeWorld(Error)||!Audit(Error)||!LDInventory::Fits(O->Inventory,O->Build,{}, {ALDB92Deposits::Kind(Node)},&Error)){return false;}
    auto* P=GetWorld()->SpawnActor<ALDItem>();if(!P){Error=TEXT("Allocation failed; charge retained");return false;}P->Kind=ALDB92Deposits::Kind(Node);
    LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("B92DepositExtraction");T.Budget=ALDB92Deposits::Budget(Node);T.Units=1;LDEconomy::FItemDelta D;D.Id=P->Identity;D.Kind=P->Kind;D.Op=LDEconomy::EItemOp::Create;D.To=Wallet(M->ContenderId(PC));T.Items.Add(D);
    if(!Ledger.Commit(T,Error)){P->Destroy();return false;}
    uint8 Slot=0;while(O->Inventory.Items.ContainsByPredicate([&](const auto& I){return I.Slot==Slot;})){++Slot;}P->Slot=Slot;P->State=1;P->Holder=PC;P->OriginReceipt=T.Id;++P->Revision;FLDInventoryItem Entry;P->Project(Entry);O->Inventory.Items.Add(Entry);++O->Inventory.Revision;Custody.Add(P);M->Inventory->Changed(PC);return true;
}
