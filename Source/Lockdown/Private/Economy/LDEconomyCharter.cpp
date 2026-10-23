#include "Economy/LDEconomyDirector.h"
#include "Economy/LDProductionDirector.h"
#include "Economy/LDBankDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Engine/World.h"

FName ALDEconomyDirector::OrderBudget(FGuid Id,bool Core)
{return FName(*(FString(Core?TEXT("Core:"):TEXT("Supply:"))+Wallet(Id).ToString()));}
bool ALDEconomyDirector::OrderAvailable(ALDPlayerController* PC,bool Core) const
{
    const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    return Initialized&&M&&M->IsPlaying()&&Ledger.Remaining(Core?TEXT("CoreOrders"):TEXT("SupplyOrders"))>0&&Ledger.Remaining(OrderBudget(M->ContenderId(PC),Core))>0;
}
bool ALDEconomyDirector::CommitOrder(ALDPlayerController* PC,bool Core,const TArray<FLDInventoryItem>& Inputs,FString& Error)
{
    Error.Reset();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(PC):nullptr;const int32 Reward=Core?80:50;
    if(!O||!OrderAvailable(PC,Core)||!M->Production->Eligible(PC,Core?2:3,true,true)||!ALDProductionDirector::RecipeInputs(Core?6:7,Inputs)||O->Gold>MAX_int64-Reward){Error=TEXT("Procurement unavailable; nothing consumed");return false;}
    if(!Audit(Error)){return false;}const FName W=Wallet(M->ContenderId(PC));const auto* A=Ledger.Account(W);if(!A||A->Gold!=O->Gold){return false;}
    TArray<ALDItem*> Physical;LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=Core?TEXT("CoreProcurement"):TEXT("SupplyProcurement");T.Mint=Reward;T.Gold.Add({W,A->Revision,Reward});T.Budget=Core?TEXT("CoreOrders"):TEXT("SupplyOrders");T.Units=1;T.Budgets.Add({OrderBudget(M->ContenderId(PC),Core),1});
    for(const auto& I:Inputs)
    {
        auto* P=M->Inventory->Owned(PC,I.Id);const auto* B=O->Inventory.Find(I.Id);const auto* L=Ledger.Item(I.Id);
        if(!P||!B||!L||L->Consumed||L->Location!=W||P->Kind!=I.Kind||P->Revision!=I.Revision||B->Revision!=I.Revision||!P->bReserved||!B->bReserved){Error=TEXT("Exact reserved procurement input changed");return false;}
        LDEconomy::FItemDelta D;D.Id=I.Id;D.Kind=I.Kind;D.Op=LDEconomy::EItemOp::Consume;D.Expected=L->Revision;D.From=W;T.Items.Add(D);Physical.Add(P);
    }
    if(!Ledger.Commit(T,Error)){return false;}
    for(auto* P:Physical){LDInventory::Remove(O->Inventory,P->Identity);P->bReserved=false;P->State=2;P->Holder.Reset();P->Destroy();}
    O->Gold=Ledger.Account(W)->Gold;
    if(Core){++O->CoreOrders;if(!O->IndustrialEndorsement.IsValid()){O->IndustrialEndorsement=T.Id;}}
    else{++O->SupplyOrders;O->SupplyEndorsement=T.Id;}
    SampleCharter(GetWorld()->GetTimeSeconds());M->Inventory->Changed(PC);M->AwardExperience(PC,Core?60:30);
    if(!Audit(Error)){++AuditFailures;}UE_LOG(LogTemp,Display,TEXT("LD_PROCUREMENT_RECEIPT %s core=%d"),*T.Id.ToString(),Core);return true;
}
bool ALDEconomyDirector::CommitCharterPurchase(ALDPlayerController* PC,int64 Share,uint32 WR,uint32 BR,FString& Error)
{
    Error.Reset();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(PC):nullptr;auto* G=GetWorld()->GetGameState<ALDGameState>();const double Now=GetWorld()->GetTimeSeconds();
    const FGuid Id=M?M->ContenderId(PC):FGuid();const FName W=Wallet(Id),B=BankAccount(Id);const auto* WA=Ledger.Account(W);const auto* BA=Ledger.Account(B);
    if(!Initialized||!O||!G||!M->Bank->Eligible(PC)||!O->IndustrialEndorsement.IsValid()||!O->SupplyEndorsement.IsValid()||!O->Witness.IsValid()||O->CharterPurchase.IsValid()||Now+94>G->MatchDeadline||Share<0||Share>100||!WA||!BA||WA->Revision!=WR||BA->Revision!=BR||WA->Gold<Share||BA->Gold<100-Share)
    {Error=TEXT("Charter needs both endorsements, Witness, exact 100 gold split, current balances and 94 seconds remaining");return false;}
    if(!Audit(Error)){return false;}LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("RegisteredCharterPurchase");T.Burn=100;T.Gold={{W,WR,-Share},{B,BR,-(100-Share)}};
    if(!Ledger.Commit(T,Error)){return false;}O->Gold=Ledger.Account(W)->Gold;O->BankGold=Ledger.Account(B)->Gold;O->CharterPurchase=T.Id;O->CharterReadyAt=Now;M->SyncOwner(PC);
    if(!Audit(Error)){++AuditFailures;}return true;
}
bool ALDEconomyDirector::CanActivateCharter(ALDPlayerController* PC,double CompletionAt) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;const auto* G=GetWorld()->GetGameState<ALDGameState>();const double Now=GetWorld()->GetTimeSeconds();
    return Initialized&&O&&G&&M->IsPlaying()&&O->bActive&&O->CharterPurchase.IsValid()&&!O->CharterActivation.IsValid()&&!O->bCharterAwaitingRespawn&&Now>=O->CharterReadyAt&&CompletionAt+90<=G->MatchDeadline&&(!G->CharterWindow.IsValid()||Now>=G->CharterCloseAt);
}
bool ALDEconomyDirector::ActivateCharter(ALDPlayerController* PC,FString& Error)
{
    const double Now=GetWorld()->GetTimeSeconds();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(!CanActivateCharter(PC,Now)||!M->Production->Eligible(PC,2,true,true)){Error=TEXT("Charter activation interrupted or another window already active");return false;}
    auto* O=M->StateFor(PC);auto* G=GetWorld()->GetGameState<ALDGameState>();
    LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("RegisteredCharterActivation");if(!Ledger.Commit(T,Error)){return false;}
    O->CharterActivation=T.Id;G->CharterWindow=T.Id;G->CharterWarningAt=Now;G->CharterOpenAt=Now+30;G->CharterCloseAt=Now+90;SampleCharter(Now);M->SyncOwner(PC);return true;
}
void ALDEconomyDirector::SampleCharter(double Now)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=GetWorld()->GetGameState<ALDGameState>();if(!Initialized||!G||!M->IsPlaying()){return;}
    G->CoreOrdersRemaining=Ledger.Remaining(TEXT("CoreOrders"));G->SupplyOrdersRemaining=Ledger.Remaining(TEXT("SupplyOrders"));
    G->CharterPhase=!G->CharterWindow.IsValid()?0:Now>=G->CharterCloseAt?3:Now>=G->CharterOpenAt?2:1;G->ForceNetUpdate();
}
