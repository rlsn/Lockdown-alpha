#include "World/LDWorldObjects.h"
#include "World/LDEnvironmentDirector.h"
#include "World/LDPortableTools.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Character/LDCharacter.h"
#include "Development/LDDevelopmentDirector.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"

bool ALDWorldObjects::InitializeTools(FString& Error)
{
    FString Json;if(!LDPortableTools::Load(Json,Error)){return false;}TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)){return false;}
    for(const auto& V:Root->GetArrayField(TEXT("rows")))
    {
        const auto Row=V->AsObject();const auto& P=Row->GetArrayField(TEXT("position"));const FVector Source(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber());
        auto* O=GetWorld()->SpawnActor<ALDWorldObject>();O->Kind=ELDObjectKind(int32(Row->GetNumberField(TEXT("kind"))));O->SourceId=FName(*Row->GetStringField(TEXT("id")));O->SourcePosition=Source;FVector Surface=Source;
        if(!FitSource(O,Source,Surface)){Error=TEXT("No legal portable pad within 10m: ")+O->SourceId.ToString();O->Destroy();return false;}
        auto* Item=GetWorld()->SpawnActor<ALDItem>();Item->Kind=O->Kind==ELDObjectKind::Torch?ELDItemKind::Torch:O->Kind==ELDObjectKind::Lantern?ELDItemKind::Lantern:ELDItemKind::Rope;Item->FuelMs=LDPortableTools::InitialFuel(Item->Kind);Item->FuelAt=GetWorld()->GetTimeSeconds();Item->Institution=O->SourceId;Item->OriginReceipt=FGuid::NewGuid();O->Physical=Item;O->LastClear=Surface;
        O->Apply(Surface+FVector(0,0,O->Size().Z*.5+3),0,1);Item->SetActorLocation(O->GetActorLocation());Objects.Add(O);Finite.Add(Item->Identity,Item);
    }
    return true;
}
bool ALDWorldObjects::ToggleTool(ALDB91Controller* PC,const FLDObjectRequest& R,FString& Why)
{
    auto Fail=[&](const TCHAR* Text){Why=Text;return false;};auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(PC):nullptr;auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;auto* S=Body&&M&&M->Combat?M->Combat->StateFor(Body):nullptr;const double Now=GetWorld()->GetTimeSeconds();
    if(!O||!Body||!S||!O->bActive||!M->IsPlaying()||S->Hp<=0||Now<O->ProtectedUntil||Now<S->ProtectionUntil||S->Action||O->DevelopmentKind){return Fail(TEXT("Portable activation unavailable"));}
    if(!ForceDrop(PC)){return Fail(TEXT("No safe cargo drop; portable action unavailable"));}
    if(!M->Combat->Allows(Body,LDTactical::EAction::Item,&Why)){return false;}
    auto* Item=M->Inventory->Owned(PC,R.Item);
    if(!Item||R.Accessory>=4||O->Inventory.Revision!=R.InventoryRevision||Item->Revision!=R.ItemRevision||Item->bReserved||Item->Quality!=0||O->Inventory.Bindings[7+R.Accessory]!=R.Item||!(Item->BindingMask&(1<<(7+R.Accessory)))
       ||(Item->Kind!=ELDItemKind::Torch&&Item->Kind!=ELDItemKind::Lantern)){return Fail(TEXT("Current physically bound Torch or Lantern required"));}
    const int Remaining=LDPortableTools::Remaining(Item,Now);if(Remaining<=0){return Fail(TEXT("Fuel exhausted; this instance cannot refill"));}
    Cancel(PC,TEXT("Portable activation cancelled held world interaction"));if(auto* E=ALDEnvironmentDirector::Get(GetWorld())){E->Cancel(PC,TEXT("Portable activation cancelled doorway interaction"));}
    Item->FuelMs=Remaining;Item->FuelAt=Now;Item->bLit=!Item->bLit;++Item->Revision;
    if(auto* Entry=const_cast<FLDInventoryItem*>(O->Inventory.Find(Item->Identity))){Item->Project(*Entry);}++O->Inventory.Revision;M->Inventory->Changed(PC);
    Why=Item->bLit?TEXT("Cosmetic light on; retained fuel burns without adding sight"):TEXT("Light off; remaining fuel retained");return true;
}
void ALDWorldObjects::TickTools(double Now)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}
    for(const auto& Pair:Finite)
    {
        auto* Item=Pair.Value.Get();if(!Item||!LDPortableTools::IsTool(Item->Kind)||!Item->bLit||LDPortableTools::Remaining(Item,Now)>0){continue;}
        Item->FuelMs=0;Item->FuelAt=Now;Item->bLit=false;++Item->Revision;
        if(auto* PC=Cast<ALDPlayerController>(Item->Holder.Get()))
        {if(auto* O=M->StateFor(PC)){if(auto* Entry=const_cast<FLDInventoryItem*>(O->Inventory.Find(Item->Identity))){Item->Project(*Entry);}++O->Inventory.Revision;M->Inventory->Changed(PC);}}
        else if(Item->State==0){for(auto Object:Objects){if(Object.IsValid()&&Object->Physical==Item){Object->Apply(Object->GetActorLocation(),Object->Flags(),Object->Revision+1);}}}
    }
    if(Now<NextFuelPublish){return;}NextFuelPublish=Now+1;
    for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I)
    {auto* PC=Cast<ALDPlayerController>(I->Get());auto* O=PC?M->StateFor(PC):nullptr;if(!O){continue;}bool Changed=false;
     for(auto& Entry:O->Inventory.Items){if(!LDPortableTools::IsTool(Entry.Kind)){continue;}if(auto* Item=M->Inventory->Owned(PC,Entry.Id))
     {const int Before=Entry.FuelMs;const bool Lit=Entry.bLit;Item->Project(Entry);Changed|=Before!=Entry.FuelMs||Lit!=Entry.bLit;}}
     if(Changed){M->SyncOwner(PC);}}
}
