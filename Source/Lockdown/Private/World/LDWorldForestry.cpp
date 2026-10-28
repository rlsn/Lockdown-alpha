#include "World/LDWorldObjects.h"
#include "World/LDEnvironmentDirector.h"
#include "World/LDB91ForestryContract.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Character/LDCharacter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

bool ALDWorldObjects::InitializeForestry(FString& Error)
{
    FString Json;if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/forestry_b91.json")))){Error=TEXT("Missing finite forestry overlay");return false;}
    Json=Json.Replace(TEXT("\r\n"),TEXT("\n"));FTCHARToUTF8 Bytes(*Json);FSHA256Signature Hash;SHA256(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length(),Hash.Signature);if(Hash.ToString().ToLower()!=LD_B91_FORESTRY_SHA){Error=TEXT("Stale forestry/control overlay");return false;}
    TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||Root->GetIntegerField(TEXT("finite_count"))!=11){return false;}
    for(const auto& V:Root->GetArrayField(TEXT("rows")))
    {const auto R=V->AsObject();const auto& P=R->GetArrayField(TEXT("position"));auto* O=GetWorld()->SpawnActor<ALDWorldObject>();O->Kind=ELDObjectKind::ForestryKnife;O->SourceId=FName(*R->GetStringField(TEXT("id")));O->SourcePosition=FVector(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber());FVector Ground;
     if(!FitSource(O,O->SourcePosition,Ground)){Error=TEXT("No legal forestry pad: ")+O->SourceId.ToString();O->Destroy();return false;}
     auto* Item=GetWorld()->SpawnActor<ALDItem>();Item->Kind=ELDItemKind::ForestryKnife;Item->Institution=O->SourceId;Item->OriginReceipt=FGuid::NewGuid();O->Physical=Item;O->LastClear=Ground;O->Apply(Ground+FVector(0,0,18),0,1);Item->SetActorLocation(O->GetActorLocation());Objects.Add(O);Finite.Add(Item->Identity,Item);}
    return true;
}
bool ALDWorldObjects::ForestryEligible(ALDB91Controller* PC,const FLDObjectRequest& R,FString& Why) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* Own=M->StateFor(PC);auto* Body=Cast<ALDCharacter>(PC->GetPawn());const auto* Item=M->Inventory->Owned(PC,R.Item);const auto* H=Holds.Find(PC);const bool Reserved=H&&H->Request.Sequence==R.Sequence&&H->Tool==Item;
    if(!M->Combat->Allows(Body,LDTactical::EAction::Item,&Why)){return false;}
    if(!Item||Item->Kind!=ELDItemKind::ForestryKnife||Item->Quality!=0||R.Accessory>=4||Own->Inventory.Bindings[7+R.Accessory]!=R.Item||Item->BindingMask!=(1<<(7+R.Accessory))||Item->Revision!=R.ItemRevision||Own->Inventory.Revision!=R.InventoryRevision||Item->bReserved!=Reserved)
    {Why=TEXT("Harvest/chop requires the current physically bound Forestry Knife");return false;}return true;
}
void ALDWorldObjects::ReserveTool(ALDB91Controller* PC,FHold& H)
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* Own=M->StateFor(PC);auto* Item=M->Inventory->Owned(PC,H.Request.Item);H.Tool=Item;Item->bReserved=true;++Item->Revision;++Own->Inventory.Revision;H.Request.ItemRevision=Item->Revision;H.Request.InventoryRevision=Own->Inventory.Revision;if(auto* E=const_cast<FLDInventoryItem*>(Own->Inventory.Find(Item->Identity))){Item->Project(*E);}M->Inventory->Changed(PC);}
