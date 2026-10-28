#include "World/LDWorldObjects.h"
#include "World/LDB91Checkpoint.h"
#include "World/LDEnvironmentDirector.h"
#include "World/LDPortableTools.h"
#include "Region/LDMapDefinition.h"
#include "Combat/LDCombatDirector.h"
#include "Core/LDGroundQueries.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"

namespace
{
FString ObjectKey(const ALDWorldObject* O)
{return O->SourceId.IsNone()&&O->Kind==ELDObjectKind::LooseItem&&O->Physical.IsValid()?TEXT("drop_")+O->Physical->Identity.ToString(EGuidFormats::Digits):O->SourceId.ToString();}
FString SupplyKey(const FString& Origin,int Kind){return Origin+TEXT(":")+FString::FromInt(Kind);}
}
bool ALDWorldObjects::CheckpointSettled(FString& Error) const
{
    const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* E=ALDEnvironmentDirector::Get(GetWorld());
    if(!M||!M->IsEnvironmentProfile()||!Ready||!E||!E->Ready){Error=TEXT("Checkpoint requires the ready authoritative B91 environment");return false;}
    for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I){if(IsValid(I->Get())&&Cast<ALDPlayerController>(I->Get())){Error=TEXT("Checkpoint requires no connected contenders; owner-state resume is not implemented");return false;}}
    if(!Holds.IsEmpty()||!Cargo.IsEmpty()||!Returns.IsEmpty()||!E->Holds.IsEmpty()||!M->Combat->EnvironmentSettled()){Error=TEXT("Checkpoint waits for actions, cargo, traversal and temporary fields to settle");return false;}
    for(auto O:Objects){if(!O.IsValid()||O->bCarriedWorld||O->Carrier.IsValid()||O->LinkInstalled()||O->InstalledAt.IsValid()||O->BurnReceipt.IsValid()&&!O->Broken){Error=TEXT("Checkpoint requires settled props and retrieved portable links");return false;}}
    return Conserved(Error);
}
bool ALDWorldObjects::CaptureCheckpoint(LDCheckpoint::FData& D,FString& Error) const
{
    const double Now=GetWorld()->GetTimeSeconds();const int64 At=LDTactical::Milliseconds(Now);const auto* E=ALDEnvironmentDirector::Get(GetWorld());if(!E){Error=TEXT("Missing environment");return false;}D=LDCheckpoint::FData();D.Committed=Committed;D.DoorMutation=E->MutationRevision;
    for(auto W:Objects)
    {const auto* O=W.Get();if(!O){Error=TEXT("Missing source actor");return false;}LDCheckpoint::FObject R;R.Id=ObjectKey(O);R.Kind=int(O->Kind);R.Position=O->GetActorLocation();R.Clear=O->Kind==ELDObjectKind::LooseItem?R.Position-FVector(0,0,18):O->LastClear;R.Revision=O->Revision;R.Hp=O->Hp;R.Broken=O->Broken;R.Harvest=O->HarvestReceipt;R.Yield=O->YieldIdentity;R.Burn=O->BurnReceipt;R.Physical=O->Physical.IsValid()?O->Physical->Identity:FGuid();for(auto P:O->Contents){if(!P.IsValid()){Error=TEXT("Missing container item");return false;}R.Contents.Add(P->Identity);}D.Objects.Add(MoveTemp(R));}
    for(const auto& Pair:Finite)
    {
        const auto* P=Pair.Value.Get();if(!P){const auto* C=Consumption.Find(Pair.Key);if(!Consumed.Contains(Pair.Key)||!C){Error=TEXT("Missing consume provenance");return false;}D.Consumed.Add({Pair.Key,C->Receipt,C->Origin.ToString(),int(C->Kind)});continue;}
        if(P->State>1||P->bReserved||P->BindingMask||P->Quality||P->Skill!=ELDSkill::None||!P->ParentIds.IsEmpty()){Error=TEXT("Unsupported owned, reserved or derived item state");return false;}
        LDCheckpoint::FItem R;R.Id=P->Identity;R.Receipt=P->OriginReceipt;R.Origin=P->Institution.ToString();R.Kind=int(P->Kind);R.Revision=P->Revision;R.Position=P->GetActorLocation();R.Fuel=LDPortableTools::IsTool(P->Kind)?LDPortableTools::Remaining(P,Now):P->FuelMs;R.Lit=P->bLit;R.Ready=FMath::Max<int64>(0,P->ReadyAt-At);R.Proc=FMath::Max<int64>(0,P->ProcReadyAt-At);
        if(P->State==1){const auto* Container=Cast<ALDWorldObject>(P->Holder.Get());if(!Container||!Container->Container()){Error=TEXT("Checkpoint cannot serialize a live personal inventory");return false;}R.Location=ObjectKey(Container);}else if(P->Holder.IsValid()){Error=TEXT("Ground item has a holder");return false;}D.Items.Add(MoveTemp(R));
    }
    for(TActorIterator<ALDItem> I(GetWorld());I;++I){if(!I->IsActorBeingDestroyed()&&I->State!=2&&!Finite.Contains(I->Identity)){Error=TEXT("Unregistered physical item");return false;}}
    for(auto W:E->Doors){if(!W.IsValid()){return false;}D.Doors.Add({W->Id.ToString(),W->Revision,W->State()});}
    D.Objects.Sort([](const auto& A,const auto& B){return A.Id<B.Id;});D.Items.Sort([](const auto& A,const auto& B){return A.Id.ToString()<B.Id.ToString();});D.Consumed.Sort([](const auto& A,const auto& B){return A.Id.ToString()<B.Id.ToString();});D.Doors.Sort([](const auto& A,const auto& B){return A.Id<B.Id;});return true;
}
FString ALDWorldObjects::CheckpointFingerprint(FString& Error) const
{LDCheckpoint::FData D;return CaptureCheckpoint(D,Error)?LDCheckpoint::Hash(LDCheckpoint::Encode(D)):FString();}
bool ALDWorldObjects::ValidateCheckpoint(const LDCheckpoint::FData& D,FString& Error) const
{
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};LDCheckpoint::FData Initial;FString Why;if(!LDCheckpoint::Decode(InitialCheckpointPayload,Initial,Why)){return Fail(TEXT("Missing native initial checkpoint model"));}
    TMap<FString,const LDCheckpoint::FObject*> Base,Rows;for(const auto& R:Initial.Objects){Base.Add(R.Id,&R);}for(const auto& R:D.Objects){Rows.Add(R.Id,&R);}if(Base.Num()!=435||Rows.Num()!=D.Objects.Num()){return Fail(TEXT("Source census or identity mismatch"));}
    TMap<FGuid,const LDCheckpoint::FItem*> Items;TMap<FGuid,const LDCheckpoint::FConsumed*> Sinks;for(const auto& R:D.Items){Items.Add(R.Id,&R);}for(const auto& R:D.Consumed){Sinks.Add(R.Id,&R);}if(Items.Num()+Sinks.Num()!=D.Items.Num()+D.Consumed.Num()){return Fail(TEXT("Duplicate item identity"));}for(const auto& P:Items){if(Sinks.Contains(P.Key)){return Fail(TEXT("Consumed item resurrected"));}}
    TMap<FString,int32> Expected,Actual;TSet<FGuid> Receipts;for(const auto& I:Initial.Items){++Expected.FindOrAdd(SupplyKey(I.Origin,I.Kind));}
    TMap<FGuid,const LDCheckpoint::FObject*> Yields;TMap<FString,const LDCheckpoint::FObject*> Harvests;TMap<FGuid,int32> PhysicalRefs,Locations;
    const auto* Map=ULDMapDefinition::Get(GetWorld());
    for(const auto& R:D.Objects)
    {
        const auto* const* Found=Base.Find(R.Id);const auto* B=Found?*Found:nullptr;const auto Kind=ELDObjectKind(R.Kind);
        if(!B)
        {if(Kind!=ELDObjectKind::LooseItem||!R.Physical.IsValid()||R.Id!=TEXT("drop_")+R.Physical.ToString(EGuidFormats::Digits)||!R.Contents.IsEmpty()||R.Broken||R.Hp!=80||R.Harvest.IsValid()||R.Yield.IsValid()||R.Burn.IsValid()){return Fail(TEXT("Unknown source or invalid loose wrapper"));}}
        else
        {
            if(B->Kind!=R.Kind){return Fail(TEXT("Source kind changed"));}const bool Movable=Kind==ELDObjectKind::Crate||Kind==ELDObjectKind::Barrel||Kind==ELDObjectKind::Ladder||Kind==ELDObjectKind::Torch||Kind==ELDObjectKind::Lantern||Kind==ELDObjectKind::Rope||Kind==ELDObjectKind::ForestryKnife||Kind==ELDObjectKind::Water||Kind==ELDObjectKind::Oil;
            if(!Movable&&(!R.Position.Equals(B->Position,.01)||!R.Clear.Equals(B->Clear,.01))){return Fail(TEXT("Fixed source moved"));}
            if(R.Broken&&Kind!=ELDObjectKind::Crate&&Kind!=ELDObjectKind::Barrel&&Kind!=ELDObjectKind::Sapling&&Kind!=ELDObjectKind::TallGrass){return Fail(TEXT("Unsupported destruction"));}
            if(R.Broken!=(R.Hp==0)||R.Revision==1&&(R.Broken||R.Hp!=80||!R.Position.Equals(B->Position,.01))){return Fail(TEXT("Source damage/revision mismatch"));}
            if(R.Hp!=80&&Kind!=ELDObjectKind::Crate&&Kind!=ELDObjectKind::Barrel&&Kind!=ELDObjectKind::Sapling&&Kind!=ELDObjectKind::TallGrass){return Fail(TEXT("Non-destructible source damaged"));}
            if(Kind==ELDObjectKind::Sapling)
            {if((R.Harvest.IsValid()||R.Burn.IsValid())&&!R.Broken||R.Harvest.IsValid()&&R.Burn.IsValid()||R.Yield.IsValid()&&!R.Harvest.IsValid()){return Fail(TEXT("Invalid foliage receipt"));}if(R.Yield.IsValid()){if(Yields.Contains(R.Yield)){return Fail(TEXT("Duplicate harvest reward"));}Yields.Add(R.Yield,&R);auto* Source=Find(FName(*R.Id));if(!Source||Source->HarvestSource.IsNone()){return false;}Harvests.Add(Source->HarvestSource.ToString(),&R);}}
            else if(R.Harvest.IsValid()||R.Yield.IsValid()||R.Burn.IsValid()&&Kind!=ELDObjectKind::TallGrass){return Fail(TEXT("Receipt on a non-foliage source"));}
            if(Kind==ELDObjectKind::TallGrass&&(R.Broken!=R.Burn.IsValid()||!R.Broken&&R.Hp!=80)){return Fail(TEXT("Invalid burnt grass"));}
        }
        if(!Map||!ULDMapDefinition::InBounds(R.Position)){return Fail(TEXT("Source outside map"));}
        if(Kind==ELDObjectKind::Crate||Kind==ELDObjectKind::Barrel||Kind==ELDObjectKind::Ladder)
        {auto* O=Find(FName(*R.Id));FVector Surface;const FVector Floor=R.Position-FVector(0,0,O->Size().Z*.5+3);if(!SurfaceAt(Floor,Surface)||FMath::Abs(Surface.Z-Floor.Z)>80||Reserved(Surface,O->Size())){return Fail(TEXT("Saved solid prop has no legal supporting pad"));}}
        const bool Container=Kind==ELDObjectKind::Chest||Kind==ELDObjectKind::Crate||Kind==ELDObjectKind::Barrel||Kind==ELDObjectKind::Furniture;if(!R.Contents.IsEmpty()&&(!Container||R.Broken)){return Fail(TEXT("Invalid container contents"));}
        int Weight=0;for(auto G:R.Contents){const auto* const* I=Items.Find(G);if(!I||(*I)->Location!=R.Id){return Fail(TEXT("Container reference mismatch"));}Weight+=LDInventory::UnitWeight(ELDItemKind((*I)->Kind));++Locations.FindOrAdd(G);}if(Weight>60){return Fail(TEXT("Saved storage overweight"));}
        if(R.Physical.IsValid()){const auto* const* I=Items.Find(R.Physical);if(!I||++PhysicalRefs.FindOrAdd(R.Physical)>1){return Fail(TEXT("Missing or duplicate physical proxy"));}if((*I)->Location.IsEmpty()){if(!(*I)->Position.Equals(R.Position,.1)){return Fail(TEXT("Ground physical/proxy mismatch"));}++Locations.FindOrAdd(R.Physical);}}
        if(B&&B->Physical.IsValid())
        {const auto* Old=Initial.Items.FindByPredicate([&](const auto& I){return I.Id==B->Physical;});if(!Old){return false;}int Count=0;for(const auto& I:D.Items){if(I.Origin==Old->Origin&&I.Kind==Old->Kind){++Count;if(R.Physical!=I.Id){return Fail(TEXT("Physical source alias changed"));}}}if(Count==0&&R.Physical.IsValid()){return Fail(TEXT("Consumed source still has a physical actor"));}}
    }
    for(const auto& P:Base){if(!Rows.Contains(P.Key)){return Fail(TEXT("Authored source omitted"));}}
    auto Provenance=[&](FGuid Id,int Kind,const FString& Origin,FGuid Receipt)
    {if(!Receipt.IsValid()||Receipts.Contains(Receipt)){return false;}Receipts.Add(Receipt);if(const auto* const* H=Yields.Find(Id)){return Kind==int(ELDItemKind::Fiber)&&Origin==Find(FName(*(*H)->Id))->HarvestSource.ToString()&&Receipt==(*H)->Harvest;}if(Harvests.Contains(Origin)){return false;}++Actual.FindOrAdd(SupplyKey(Origin,Kind));return true;};
    for(const auto& I:D.Items)
    {const int Max=LDPortableTools::InitialFuel(ELDItemKind(I.Kind));if(!Provenance(I.Id,I.Kind,I.Origin,I.Receipt)||Locations.FindRef(I.Id)!=1||I.Fuel>Max||I.Lit&&(I.Fuel==0||Max==0)||!I.Location.IsEmpty()&&!Rows.Contains(I.Location)){return Fail(TEXT("Invalid item provenance, location or fuel"));}}
    for(const auto& I:D.Consumed){if(!Provenance(I.Id,I.Kind,I.Origin,I.Receipt)){return Fail(TEXT("Invalid consume provenance"));}}
    for(const auto& P:Yields){if(!Items.Contains(P.Key)&&!Sinks.Contains(P.Key)){return Fail(TEXT("Harvest reward disappeared"));}}
    if(Expected.Num()!=Actual.Num()){return Fail(TEXT("Endowment count changed"));}for(const auto& P:Expected){if(Actual.FindRef(P.Key)!=P.Value){return Fail(TEXT("Endowment duplicated or lost"));}}
    const auto* E=ALDEnvironmentDirector::Get(GetWorld());for(const auto& R:D.Doors){auto* Door=E->Door(FName(*R.Id));const bool Open=R.State&1,Locked=R.State&2,Broken=R.State&4;if(!Door||R.Revision>D.DoorMutation||Open&&Locked||Broken&&(!Open||Locked)||Locked&&!Door->Rear||Broken&&!Door->Rear||R.State&&R.Revision<2){return Fail(TEXT("Invalid door state or address"));}}
    return true;
}
bool ALDWorldObjects::SaveCheckpoint(const FString& Path,FString& Error)
{
    if(Path.IsEmpty()||!CheckpointSettled(Error)){return false;}LDCheckpoint::FData D;if(!CaptureCheckpoint(D,Error)||!ValidateCheckpoint(D,Error)){return false;}const FString Payload=LDCheckpoint::Encode(D);LDCheckpoint::FData Checked;if(!LDCheckpoint::Decode(Payload,Checked,Error)){return false;}
    const FString Full=FPaths::ConvertRelativePathToFull(Path),Temp=Full+TEXT(".")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".tmp");IFileManager::Get().MakeDirectory(*FPaths::GetPath(Full),true);const auto* E=ALDEnvironmentDirector::Get(GetWorld());
    if(!FFileHelper::SaveStringToFile(LDCheckpoint::Envelope(Payload,Contract,E->Contract),*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)||!IFileManager::Get().Move(*Full,*Temp,true,true,false,true)){IFileManager::Get().Delete(*Temp);Error=TEXT("Checkpoint atomic write failed");return false;}LastCheckpointHash=LDCheckpoint::Hash(Payload);UE_LOG(LogTemp,Display,TEXT("LD_B91_CHECKPOINT saved objects=%d live=%d consumed=%d sha=%s"),D.Objects.Num(),D.Items.Num(),D.Consumed.Num(),*LastCheckpointHash);return true;
}
bool ALDWorldObjects::RestoreCheckpoint(const FString& Path,FString& Error)
{
    if(!CheckpointSettled(Error)){return false;}const FString Current=CheckpointFingerprint(Error);if(bCheckpointRestored||Current.IsEmpty()||Current!=LDCheckpoint::Hash(InitialCheckpointPayload)){Error=TEXT("Checkpoint restore requires a pristine ownerless world and can run only once");return false;}
    if(IFileManager::Get().FileSize(*Path)<0||IFileManager::Get().FileSize(*Path)>5*1024*1024){Error=TEXT("Checkpoint missing or oversized");return false;}FString Text;LDCheckpoint::FData D;auto* E=ALDEnvironmentDirector::Get(GetWorld());if(!FFileHelper::LoadFileToString(Text,*Path)||!LDCheckpoint::Open(Text,Contract,E->Contract,D,Error)||!ValidateCheckpoint(D,Error)){return false;}
    // Stage allocations first. Failure leaves all live sources and goods intact.
    TMap<FGuid,ALDItem*> NewItems;TMap<FString,ALDWorldObject*> NewObjects;auto Rollback=[&](){for(auto P:NewItems){P.Value->Destroy();}for(auto P:NewObjects){P.Value->Destroy();}};
    for(const auto& R:D.Items){auto* P=GetWorld()->SpawnActor<ALDItem>();if(!P){Rollback();Error=TEXT("Checkpoint allocation failed");return false;}P->State=2;NewItems.Add(R.Id,P);}
    for(const auto& R:D.Objects){if(!Find(FName(*R.Id))){auto* O=GetWorld()->SpawnActor<ALDWorldObject>();if(!O){Rollback();Error=TEXT("Checkpoint allocation failed");return false;}NewObjects.Add(R.Id,O);}}
    for(auto O:Objects){O->Contents.Empty();O->Physical.Reset();}for(auto P:Finite){if(P.Value.IsValid()){P.Value->Destroy();}}Finite.Empty();Consumed.Empty();Consumption.Empty();
    const double Now=GetWorld()->GetTimeSeconds();const int64 At=LDTactical::Milliseconds(Now);
    for(const auto& R:D.Items){auto* P=NewItems.FindChecked(R.Id);P->Identity=R.Id;P->Kind=ELDItemKind(R.Kind);P->OriginReceipt=R.Receipt;P->Institution=FName(*R.Origin);P->Revision=R.Revision;P->FuelMs=R.Fuel;P->FuelAt=Now;P->bLit=R.Lit;P->ReadyAt=At+R.Ready;P->ProcReadyAt=At+R.Proc;P->State=R.Location.IsEmpty()?0:1;P->SetActorLocation(R.Position);Finite.Add(R.Id,P);}
    for(const auto& R:D.Consumed){Finite.Add(R.Id,TWeakObjectPtr<ALDItem>());Consumed.Add(R.Id);Consumption.Add(R.Id,{ELDItemKind(R.Kind),FName(*R.Origin),R.Receipt});}
    for(const auto& R:D.Objects){auto* O=Find(FName(*R.Id));if(!O){O=NewObjects.FindChecked(R.Id);O->SourceId=FName(*R.Id);O->Kind=ELDObjectKind::LooseItem;O->SourcePosition=R.Clear;Objects.Add(O);}O->LastClear=R.Clear;O->Hp=R.Hp;O->Broken=R.Broken;O->HarvestReceipt=R.Harvest;O->YieldIdentity=R.Yield;O->BurnReceipt=R.Burn;O->BurnClearAt=0;O->Physical=NewItems.FindRef(R.Physical);for(auto Id:R.Contents){auto* P=NewItems.FindChecked(Id);P->Holder=O;O->Contents.Add(P);}O->AppliedState=255;O->Apply(R.Position,O->Flags(),R.Revision);O->SetObserved(false);}
    for(const auto& R:D.Doors){E->Door(FName(*R.Id))->Apply(R.State,R.Revision);}Committed=D.Committed;E->MutationRevision=D.DoorMutation;Clients.Empty();GrassBodies.Empty();E->Clients.Empty();E->Observers.Empty();E->Journal.Empty();bCheckpointRestored=true;
    check(Conserved(Error));UE_LOG(LogTemp,Display,TEXT("LD_B91_CHECKPOINT restored objects=%d live=%d consumed=%d"),D.Objects.Num(),D.Items.Num(),D.Consumed.Num());return true;
}
bool ALDWorldObjects::InitializeCheckpoint(FString& Error)
{
    LDCheckpoint::FData D;if(!CaptureCheckpoint(D,Error)){return false;}InitialCheckpointPayload=LDCheckpoint::Encode(D);FParse::Value(FCommandLine::Get(),TEXT("LDB91Checkpoint="),AutomaticCheckpoint);FString Load;if(FParse::Value(FCommandLine::Get(),TEXT("LDB91Restore="),Load)&&!RestoreCheckpoint(Load,Error)){return false;}return true;
}
void ALDWorldObjects::TickCheckpoint(double Now)
{
    if(AutomaticCheckpoint.IsEmpty()||Now<NextCheckpoint){return;}NextCheckpoint=Now+2;for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I){if(IsValid(I->Get())){return;}}FString Error;if(!CheckpointSettled(Error)){return;}const FString Fingerprint=CheckpointFingerprint(Error);if(Fingerprint.IsEmpty()||Fingerprint==LastCheckpointHash){return;}if(!SaveCheckpoint(AutomaticCheckpoint,Error)&&Error!=LastCheckpointError){UE_LOG(LogTemp,Warning,TEXT("LD_B91_CHECKPOINT deferred: %s"),*Error);LastCheckpointError=Error;}
}
static FAutoConsoleCommandWithWorldAndArgs SaveB91Checkpoint(TEXT("LD.B91.Checkpoint"),TEXT("Save a settled, ownerless B91 world: LD.B91.Checkpoint <server-local-path>"),FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World){FString Error;auto* O=ALDWorldObjects::Get(World);if(Args.Num()!=1||!O||!O->SaveCheckpoint(Args[0],Error)){UE_LOG(LogTemp,Warning,TEXT("LD_B91_CHECKPOINT refused: %s"),*Error);}}));
