#include "World/LDPortableTools.h"
#include "World/LDB91ToolsContract.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDInventoryDirector.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
bool LDPortableTools::IsTool(ELDItemKind Kind)
{return Kind==ELDItemKind::Torch||Kind==ELDItemKind::Lantern||Kind==ELDItemKind::Rope||Kind==ELDItemKind::ForestryKnife;}
int32 LDPortableTools::InitialFuel(ELDItemKind Kind)
{return Kind==ELDItemKind::Torch?180000:Kind==ELDItemKind::Lantern?300000:0;}
bool LDPortableTools::Load(FString& Json,FString& Error)
{
    if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/tools_b91.json")))){Error=TEXT("Missing B91 tools overlay");return false;}
    Json=Json.Replace(TEXT("\r\n"),TEXT("\n"));FTCHARToUTF8 Bytes(*Json);FSHA256Signature H;SHA256(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length(),H.Signature);
    if(H.ToString().ToLower()!=LD_B91_TOOLS_SHA){Error=TEXT("Stale/unknown portable-tool overlay");return false;}return true;
}
const FLDEquipmentDefinition* LDPortableTools::Definition(ELDItemKind Kind)
{
    if(!IsTool(Kind)){return nullptr;}static TArray<FLDEquipmentDefinition> Definitions;
    if(Definitions.IsEmpty())
    {
        FString Json,Error;if(!Load(Json,Error)){return nullptr;}TSharedPtr<FJsonObject> Root;
        if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)){return nullptr;}
        for(const auto& V:Root->GetArrayField(TEXT("definitions")))
        {const auto Row=V->AsObject();FLDEquipmentDefinition D;D.Kind=ELDItemKind(int32(Row->GetNumberField(TEXT("native_kind"))));D.Id=Row->GetStringField(TEXT("id"));D.Name=Row->GetStringField(TEXT("name"));D.Binding=7;D.Weight=Row->GetIntegerField(TEXT("weight"));D.Price=-1;D.Stats.WeaponBase=0;D.Operation=TEXT("portable_world_tool");D.ModifierHelp=TEXT("Finite Standard world tool; no passive stats, official price or material output");FJsonSerializer::Serialize(Row->GetObjectField(TEXT("active")).ToSharedRef(),TJsonWriterFactory<>::Create(&D.ActiveJson));Definitions.Add(D);}
    }
    if(Definitions.Num()==3)
    {FLDEquipmentDefinition D;D.Kind=ELDItemKind::ForestryKnife;D.Id=TEXT("forestry_knife");D.Name=TEXT("Forestry Knife");D.Binding=7;D.Weight=2;D.Price=-1;D.Stats.WeaponBase=0;D.Operation=TEXT("forestry_tool");D.ModifierHelp=TEXT("Finite Standard tool; target a sapling to harvest/chop. No passive stats or refill.");D.ActiveJson=TEXT("{\"op\":\"forestry_tool\",\"range_cm\":120,\"channel_ms\":4000,\"blocked_by\":\"Mute\"}");Definitions.Add(D);}
    return Definitions.FindByPredicate([&](const auto& D){return D.Kind==Kind;});
}
int32 LDPortableTools::Remaining(const ALDItem* Item,double Now)
{return Item?FMath::Max(0,Item->FuelMs-(Item->bLit?FMath::FloorToInt(FMath::Max(0.,Now-Item->FuelAt)*1000):0)):0;}
uint8 LDPortableTools::LightFor(const ALDPlayerController* PC)
{
    if(!PC){return 0;}uint8 Mode=0;const auto* M=PC->GetWorld()->GetAuthGameMode<ALDGameMode>();
    for(const auto& E:PC->OwnerState.Inventory.Items)
    {if(E.Kind!=ELDItemKind::Torch&&E.Kind!=ELDItemKind::Lantern){continue;}bool Lit=E.bLit&&E.FuelMs>0;
     if(M&&M->Inventory){const auto* P=M->Inventory->Owned(const_cast<ALDPlayerController*>(PC),E.Id);Lit=P&&P->bLit&&Remaining(P,PC->GetWorld()->GetTimeSeconds())>0;}
     if(Lit){Mode=FMath::Max<uint8>(Mode,E.Kind==ELDItemKind::Lantern?2:1);}}
    return Mode;
}
