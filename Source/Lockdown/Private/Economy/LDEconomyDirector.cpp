#include "Economy/LDEconomyDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Development/LDWorldChecks.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Vision/LDVisionDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

FName ALDEconomyDirector::Wallet(FGuid Id)
{return FName(*(TEXT("Wallet:")+Id.ToString(EGuidFormats::Digits)));}
bool ALDEconomyDirector::Initialize(FString& Error)
{
    if(!HasAuthority()){Error=TEXT("Economy requires authority");return false;}
    if(Initialized){Error=TEXT("Economy already initialized; no refill");return false;}
    LDEconomy::FCatalog C;if(!C.Load(Error)){return false;}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();TMap<FName,int64> Accounts;M->EconomyWallets(Accounts);
    if(M->WorldChecks){M->WorldChecks->EconomyWallets(Accounts);}
    TMap<FName,int32> Budgets;const int32 N=M->EconomyContenderCount();
    for(const auto& P:Accounts){if(P.Key.ToString().StartsWith(TEXT("Wallet:"))){Budgets.Add(FName(*(TEXT("Courier:")+P.Key.ToString())),8);Budgets.Add(FName(*(TEXT("Sentinel:")+P.Key.ToString())),2);Budgets.Add(FName(*(TEXT("Core:")+P.Key.ToString())),3);Budgets.Add(FName(*(TEXT("Supply:")+P.Key.ToString())),1);}}
    Budgets.Add(TEXT("CoreOrders"),3*N);Budgets.Add(TEXT("SupplyOrders"),N);Budgets.Add(TEXT("ShortageOrder"),1);
    if(M->IsTacticalProfile()){for(int32 S=10;S<38;++S){Budgets.Add(FName(*FString::Printf(TEXT("B74Book%d"),S)),2);}for(int32 CacheIndex=6;CacheIndex<9;++CacheIndex){Budgets.Add(FName(*FString::Printf(TEXT("B74Cache%d"),CacheIndex)),1);}Budgets.Add(TEXT("B74Guardian"),1);Budgets.Add(TEXT("B74Endowment"),1);}
    if(M->IsTacticalProfile()){for(int32 WorldIndex=0;WorldIndex<3;++WorldIndex){Budgets.Add(FName(*FString::Printf(TEXT("B76Tower%d"),WorldIndex)),1);for(int32 Spawn=1;Spawn<=12;++Spawn){Budgets.Add(FName(*FString::Printf(TEXT("B76Camp%d.%d"),WorldIndex,Spawn)),1);}}Budgets.Add(TEXT("B76GuardianDefeat"),1);TArray<FName> Wallets;Accounts.GetKeys(Wallets);for(FName W:Wallets){if(W.ToString().StartsWith(TEXT("Wallet:"))){Accounts.Add(FName(*(TEXT("WorldReward:")+W.ToString().RightChop(7))),0);}}}
    Budgets.Add(TEXT("ProducerWest"),8);Budgets.Add(TEXT("ProducerEast"),8);
    for(int32 I=0;I<C.Nodes;++I){Budgets.Add(FName(*FString::Printf(TEXT("Outcrop%d"),I+1)),C.Charges);}
    for(const auto& Market:C.Markets){Accounts.Add(Market.Id,Market.Gold);}
    LDEconomy::FLedger Candidate;if(!Candidate.Initialize(FGuid::NewGuid(),Accounts,Budgets,Error)){return false;}
    LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("MarketStockEndowment");
    TArray<TObjectPtr<ALDItem>> Created;
    auto Cleanup=[&](){for(const auto& Item:Created){Item->Destroy();}};
    for(const auto& Market:C.Markets){for(const auto& Good:Market.Goods){for(int32 I=0;I<Good.Target;++I)
    {
        auto* Item=GetWorld()->SpawnActor<ALDItem>();if(!Item){Cleanup();Error=TEXT("Market item allocation failed; initialization rolled back");return false;}
        Created.Add(Item);Item->Kind=Good.Kind;Item->State=1;Item->Holder=this;Item->Institution=Market.Id;Item->OriginReceipt=T.Id;
        LDEconomy::FItemDelta D;D.Id=Item->Identity;D.Op=LDEconomy::EItemOp::Create;D.Kind=Good.Kind;D.To=Market.Id;T.Items.Add(D);
    }}}
    if(!Candidate.Commit(T,Error)||!Candidate.Reconcile(Error)){Cleanup();return false;}
    Catalog=MoveTemp(C);Ledger=MoveTemp(Candidate);for(uint8 Service=0;Service<4;++Service){MarketRevisions.Add(Service,1);}InstitutionalItems=MoveTemp(Created);Initialized=true;ScheduleId=FGuid::NewGuid();
    UE_LOG(LogTemp,Display,TEXT("LD_ECONOMY_INITIALIZED version=%d checksum=%s accountsGold=%lld institutionalItems=%d contenders=%d"),Catalog.Version,*Catalog.Checksum,Ledger.TotalGold(),InstitutionalItems.Num(),N);return true;
}
void ALDEconomyDirector::GoldChanged(FName Cause,int64 Mint,int64 Burn)
{if(!Record(Cause,Mint,Burn)){++AuditFailures;UE_LOG(LogTemp,Error,TEXT("LD_ECONOMY_RECORD_FAILED cause=%s"),*Cause.ToString());}}
void ALDEconomyDirector::ResidentEndowment(FGuid Id,int64 Gold)
{if(Initialized&&!Ledger.EndowAccount(Wallet(Id),Gold)){UE_LOG(LogTemp,Error,TEXT("LD_ECONOMY_ENDOWMENT_REJECTED %s"),*Id.ToString());}}
void ALDEconomyDirector::Retire(ALDPlayerController* PC)
{if(Initialized){auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();Ledger.Retire(Wallet(M->ContenderId(PC)));Ledger.Retire(BankAccount(M->ContenderId(PC)));}}

FName ALDEconomyDirector::BankAccount(FGuid Id)
{return FName(*(TEXT("Bank:")+Id.ToString(EGuidFormats::Digits)));}
bool ALDEconomyDirector::CommitBank(ALDPlayerController* PC,int64 Amount,bool Deposit,uint32 WR,uint32 BR,FString& Error)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);const FGuid Id=M->ContenderId(PC);
    const auto* W=Ledger.Account(Wallet(Id));const auto* B=Ledger.Account(BankAccount(Id));
    if(!Initialized||!O||!W||!B||W->Gold!=O->Gold||B->Gold!=O->BankGold){Error=TEXT("Balances changed; reopen Treasury");return false;}
    if(!Ledger.Reconcile(Error)){return false;}
    LDEconomy::FTransaction T;
    if(!LDEconomy::BankTransaction(Ledger,FGuid::NewGuid(),Wallet(Id),BankAccount(Id),WR,BR,Amount,Deposit,T,Error)||!Ledger.Commit(T,Error)){return false;}
    O->Gold=Ledger.Account(Wallet(Id))->Gold;O->BankGold=Ledger.Account(BankAccount(Id))->Gold;M->SyncOwner(PC);
    UE_LOG(LogTemp,Display,TEXT("LD_BANK_RECEIPT id=%s deposit=%d amount=%lld fee=%lld"),*T.Id.ToString(),Deposit,Amount,T.Burn);
    if(!Ledger.Reconcile(Error)){UE_LOG(LogTemp,Error,TEXT("LD_BANK_AUDIT_FAILED %s"),*Error);}
    return true;
}

bool ALDEconomyDirector::AllowDiagnosticItems() const
{
#if !UE_BUILD_SHIPPING
    return FParse::Param(FCommandLine::Get(),TEXT("LDB81Probe"))||FParse::Param(FCommandLine::Get(),TEXT("LDContentNetProbe"))||(FParse::Param(FCommandLine::Get(),TEXT("LDContentProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDWorldBoundaryProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDWorldNetProbe")))||FParse::Param(FCommandLine::Get(),TEXT("LDAcquisitionNetProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDLoadoutProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDTacticalProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDEconomyUIProbe"))||(FParse::Param(FCommandLine::Get(),TEXT("LDCharterProbe"))&&!FParse::Param(FCommandLine::Get(),TEXT("LDCharterJourney")))||FParse::Param(FCommandLine::Get(),TEXT("LDInstitutionProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDProductionProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDCommerceProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDInventoryProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDEconomyProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDDevelopmentProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDTradeProbe"));
#else
    return false;
#endif
}
bool ALDEconomyDirector::Snapshot(TMap<FGuid,FPhysical>& Out,FString& Error) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    for(TActorIterator<ALDItem> It(GetWorld());It;++It)
    {
        auto* I=*It;if(I->State==2){continue;}
        if(!I->Identity.IsValid()||I->State>2||Out.Contains(I->Identity)){Error=TEXT("Invalid or duplicate physical item identity");return false;}
        FName Location=TEXT("World");
        if(I->State==1)
        {
            if(I->Holder==this){if(I->Institution.IsNone()){Error=TEXT("Missing institution address");return false;}Location=I->Institution;}
            else if(auto* PC=Cast<ALDPlayerController>(I->Holder.Get()))
            {
                Location=Wallet(M->ContenderId(PC));const auto* O=M->StateFor(PC);const auto* Entry=O?O->Inventory.Find(I->Identity):nullptr;
                if(!Entry||Entry->Kind!=I->Kind||Entry->Skill!=I->Skill||Entry->Slot!=I->Slot||Entry->Revision!=I->Revision||Entry->bReserved!=I->bReserved||Entry->BindingMask!=I->BindingMask||Entry->ReadyAt!=I->ReadyAt||Entry->Quality!=I->Quality){Error=TEXT("Physical item differs from owner bag");return false;}
            }
            else{Error=TEXT("Carried item has no persistent holder");return false;}
        }
        else if(I->Holder.IsValid()){Error=TEXT("Dropped item still has a holder");return false;}
        Out.Add(I->Identity,{I->Kind,Location,I});
    }
    for(TActorIterator<ALDWard> It(GetWorld());It;++It)
    {
        if(!It->SourceCharge.IsValid()){continue;}
        if(Out.Contains(It->SourceCharge)){Error=TEXT("Ward charge counted twice");return false;}
        Out.Add(It->SourceCharge,{ELDItemKind::WardCharge,FName(*(TEXT("ActiveWard:")+It->Identity.ToString())),nullptr});
    }
    for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){if(It->ChargeIdentity.IsValid()&&!It->IsActorBeingDestroyed()){if(Out.Contains(It->ChargeIdentity)){Error=TEXT("Tactical source counted twice");return false;}Out.Add(It->ChargeIdentity,{It->ChargeKind,FName(*(TEXT("ActiveTactical:")+It->Identity.ToString())),nullptr});}}
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {if(auto* PC=Cast<ALDPlayerController>(It->Get())){if(const auto* O=M->StateFor(PC)){for(const auto& I:O->Inventory.Items){const auto* P=Out.Find(I.Id);if(!P||P->Location!=Wallet(M->ContenderId(PC))){Error=TEXT("Bag contains missing physical item");return false;}}}}}
    return true;
}
bool ALDEconomyDirector::Record(FName Cause,int64 Mint,int64 Burn,const TArray<FGuid>& Sinks)
{
    if(!Initialized){return true;}FString Error;TMap<FGuid,FPhysical> Physical;
    auto Fail=[&](){UE_LOG(LogTemp,Error,TEXT("LD_ECONOMY_RECORD_ERROR cause=%s reason=%s"),*Cause.ToString(),*Error);return false;};
    if(!Snapshot(Physical,Error)){return Fail();}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();TMap<FName,int64> Actual;M->EconomyWallets(Actual);if(M->WorldChecks){M->WorldChecks->EconomyWallets(Actual);}
    LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=Cause;T.Mint=Mint;T.Burn=Burn;
    for(const auto& P:Actual){const auto* A=Ledger.Account(P.Key);if(!A){Error=TEXT("Unregistered wallet");return Fail();}if(A->Gold!=P.Value){T.Gold.Add({P.Key,A->Revision,P.Value-A->Gold});}}
    const bool CanCreate=Cause==TEXT("CarriedEndowment")||Cause==TEXT("OfficialBookPurchase")||(Cause==TEXT("DiagnosticItemCreation")&&AllowDiagnosticItems());
    for(const auto& P:Physical)
    {
        const auto* Old=Ledger.Item(P.Key);const auto& I=P.Value;LDEconomy::FItemDelta D;D.Id=P.Key;D.Kind=I.Kind;D.To=I.Location;
        if(!Old){if(!CanCreate){Error=TEXT("Unclassified item creation");return Fail();}D.Op=LDEconomy::EItemOp::Create;if(I.Actor){D.Parents=I.Actor->ParentIds;D.Skill=uint8(I.Actor->Skill);D.Quality=I.Actor->Quality;}T.Items.Add(D);}
        else
        {
            if(Old->Consumed||Old->Kind!=I.Kind||(I.Actor&&(I.Actor->OriginReceipt!=Old->Origin||I.Actor->ParentIds!=Old->Parents))){Error=TEXT("Item resurrected or causal metadata changed");return Fail();}
            if(Old->Location!=I.Location){D.Op=LDEconomy::EItemOp::Transfer;D.From=Old->Location;D.Expected=Old->Revision;T.Items.Add(D);}
        }
    }
    for(const auto& P:Ledger.ItemRecords())
    {if(!P.Value.Consumed&&!Physical.Contains(P.Key)){if(!Sinks.Contains(P.Key)){Error=TEXT("Unclassified missing item");return Fail();}LDEconomy::FItemDelta D;D.Id=P.Key;D.Kind=P.Value.Kind;D.Op=LDEconomy::EItemOp::Consume;D.From=P.Value.Location;D.Expected=P.Value.Revision;T.Items.Add(D);}}
    if(Cause==TEXT("CourierReward")||Cause==TEXT("SentinelReward"))
    {for(const auto& D:T.Gold){if(D.Delta>0){T.Budget=FName(*((Cause==TEXT("CourierReward")?FString(TEXT("Courier:")):FString(TEXT("Sentinel:")))+D.Account.ToString()));T.Units=1;}}}
    if(!Ledger.Commit(T,Error)){return Fail();}
    for(const auto& D:T.Items){if(D.Op==LDEconomy::EItemOp::Create){if(auto* P=Physical.Find(D.Id);P&&P->Actor){P->Actor->OriginReceipt=T.Id;}}}
    if(!Audit(Error)){return Fail();}
    UE_LOG(LogTemp,Display,TEXT("LD_ECONOMY_RECEIPT id=%s cause=%s mint=%lld burn=%lld goldDeltas=%d itemDeltas=%d total=%lld"),*T.Id.ToString(),*Cause.ToString(),Mint,Burn,T.Gold.Num(),T.Items.Num(),Ledger.TotalGold());return true;
}
bool ALDEconomyDirector::Audit(FString& Error) const
{
    if(!Initialized){Error=TEXT("Economy not initialized");return false;}if(!Ledger.Reconcile(Error)){return false;}
    TMap<FGuid,FPhysical> Physical;if(!Snapshot(Physical,Error)){return false;}
    int32 Live=0;for(const auto& P:Ledger.ItemRecords())
    {if(!P.Value.Consumed){++Live;const auto* I=Physical.Find(P.Key);if(!I||I->Kind!=P.Value.Kind||I->Location!=P.Value.Location||(I->Actor&&(I->Actor->OriginReceipt!=P.Value.Origin||I->Actor->ParentIds!=P.Value.Parents||I->Actor->Quality!=P.Value.Quality||uint8(I->Actor->Skill)!=P.Value.Skill))){Error=TEXT("Physical world/journal mismatch");return false;}}}
    if(Live!=Physical.Num()){Error=TEXT("Unregistered physical item");return false;}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();TMap<FName,int64> Actual;M->EconomyWallets(Actual);if(M->WorldChecks){M->WorldChecks->EconomyWallets(Actual);}
    for(const auto& P:Actual){const auto* A=Ledger.Account(P.Key);if(!A||A->Gold!=P.Value){Error=TEXT("Actual balance/journal mismatch");return false;}}return true;
}
bool ALDEconomyDirector::Export(FName Event,FString& Error) const
{
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Economy");IFileManager::Get().MakeDirectory(*Folder,true);
    TSharedPtr<FJsonObject> Json;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Ledger.ExportJson(Event)),Json);
    if(!Json){Error=TEXT("Invalid journal export");return false;}Json->SetStringField(TEXT("catalogChecksum"),Catalog.Checksum);Json->SetNumberField(TEXT("catalogVersion"),Catalog.Version);Json->SetNumberField(TEXT("auditFailures"),AuditFailures);
    const FString File=Folder/(FString::Printf(TEXT("%s-%s.json"),*Event.ToString(),*Json->GetStringField(TEXT("match"))));FString Output;FJsonSerializer::Serialize(Json.ToSharedRef(),TJsonWriterFactory<>::Create(&Output));
    if(!FFileHelper::SaveStringToFile(Output,*File,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)){Error=TEXT("Could not write economy evidence");return false;}return true;
}
void ALDEconomyDirector::EndPlay(const EEndPlayReason::Type Reason)
{if(Initialized){FString Error;Export(TEXT("Reset"),Error);}Super::EndPlay(Reason);}
