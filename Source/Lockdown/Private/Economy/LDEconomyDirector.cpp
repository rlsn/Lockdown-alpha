#include "Economy/LDEconomyDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

FName ALDEconomyDirector::Wallet(FGuid Id)
{return FName(*(TEXT("Wallet:")+Id.ToString(EGuidFormats::Digits)));}
void ALDEconomyDirector::Retire(ALDPlayerController* PC)
{if(Initialized){auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();Ledger.Retire(Wallet(M->ContenderId(PC)));}}

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
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {if(auto* PC=Cast<ALDPlayerController>(It->Get())){if(const auto* O=M->StateFor(PC)){for(const auto& I:O->Inventory.Items){const auto* P=Out.Find(I.Id);if(!P||P->Location!=Wallet(M->ContenderId(PC))){Error=TEXT("Bag contains missing physical item");return false;}}}}}
    return true;
}
bool ALDEconomyDirector::Record(FName Cause,int64 Mint,int64 Burn,const TArray<FGuid>& Sinks)
{
    if(!Initialized){UE_LOG(LogTemp,Error,TEXT("LD_ECONOMY_NOT_INITIALIZED: no transaction committed"));return false;}FString Error;TMap<FGuid,FPhysical> Physical;
    auto Fail=[&](){UE_LOG(LogTemp,Error,TEXT("LD_ECONOMY_RECORD_ERROR cause=%s reason=%s"),*Cause.ToString(),*Error);return false;};
    if(!Snapshot(Physical,Error)){return Fail();}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();TMap<FName,int64> Actual;M->EconomyWallets(Actual);
    LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=Cause;T.Mint=Mint;T.Burn=Burn;
    for(const auto& P:Actual){const auto* A=Ledger.Account(P.Key);if(!A){Error=TEXT("Unregistered wallet");return Fail();}if(A->Gold!=P.Value){T.Gold.Add({P.Key,A->Revision,P.Value-A->Gold});}}
    const bool CanCreate=Cause==TEXT("CarriedEndowment");
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
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();TMap<FName,int64> Actual;M->EconomyWallets(Actual);
    for(const auto& P:Actual){const auto* A=Ledger.Account(P.Key);if(!A||A->Gold!=P.Value){Error=TEXT("Actual balance/journal mismatch");return false;}}return true;
}
bool ALDEconomyDirector::Export(FName Event,FString& Error) const
{
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Economy");IFileManager::Get().MakeDirectory(*Folder,true);
    TSharedPtr<FJsonObject> Json;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Ledger.ExportJson(Event)),Json);
    if(!Json){Error=TEXT("Invalid journal export");return false;}Json->SetNumberField(TEXT("auditFailures"),AuditFailures);
    const FString File=Folder/(FString::Printf(TEXT("%s-%s.json"),*Event.ToString(),*Json->GetStringField(TEXT("match"))));FString Output;FJsonSerializer::Serialize(Json.ToSharedRef(),TJsonWriterFactory<>::Create(&Output));
    if(!FFileHelper::SaveStringToFile(Output,*File,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)){Error=TEXT("Could not write economy evidence");return false;}return true;
}
void ALDEconomyDirector::EndPlay(const EEndPlayReason::Type Reason)
{if(Initialized){FString Error;Export(TEXT("Reset"),Error);}Super::EndPlay(Reason);}
