#include "Inventory/LDAcquisitionCatalog.h"
#include "LDAcquisitionContract.h"
#include "Development/LDB84Contract.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
namespace {FString AcquisitionHash(const FString& S){FTCHARToUTF8 B(*S);FSHA256Signature H;SHA256(reinterpret_cast<const uint8*>(B.Get()),B.Length(),H.Signature);return H.ToString().ToLower();}}
ELDItemKind ULDAcquisitionCatalog::Kind(const FString& Id)
{
    // Append-only native values are encoded in the checksum-closed source mapping.
    const auto* C=Load();if(C){if(const auto* D=C->Items.FindByPredicate([&](const auto& I){return I.Id==Id;})){return D->Kind;}}
    const TMap<FString,ELDItemKind> M={{TEXT("Fiber"),ELDItemKind::Fiber},{TEXT("Ore"),ELDItemKind::Ore},{TEXT("Crystal"),ELDItemKind::Crystal},{TEXT("Toolkit"),ELDItemKind::Toolkit},{TEXT("Core"),ELDItemKind::Core},{TEXT("arcane_catalyst"),ELDItemKind::ArcaneCatalyst},{TEXT("heart_fragment"),ELDItemKind::HeartFragment},{TEXT("ancient_key_a"),ELDItemKind::AncientKeyA},{TEXT("ancient_key_b"),ELDItemKind::AncientKeyB},{TEXT("ancient_key_c"),ELDItemKind::AncientKeyC},{TEXT("heartbreaker"),ELDItemKind::Heartbreaker}};
    const auto* K=M.Find(Id);return K?*K:ELDItemKind::Seal;
}
bool ULDAcquisitionCatalog::Import(const FString& Text,FString& Why)
{
    SourceJson=Text.Replace(TEXT("\r\n"),TEXT("\n"));Checksum=AcquisitionHash(SourceJson);Items.Reset();Recipes.Reset();
    if(Checksum!=LD_ACQUISITION_SHA&&Checksum!=LD_B84_ACQUISITION_SHA){Why=TEXT("Acquisition checksum mismatch");return false;}
    TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJson),Root)||!Root){return false;}
    const auto* Enum=StaticEnum<ELDItemKind>();
    for(const auto& V:Root->GetArrayField(TEXT("items")))
    {const auto O=V->AsObject();FLDAcquisitionItem I;I.Id=O->GetStringField(TEXT("id"));I.Name=O->GetStringField(TEXT("name"));I.Kind=ELDItemKind(Enum->GetValueByNameString(O->GetStringField(TEXT("kind"))));I.Weight=O->GetIntegerField(TEXT("weight"));I.Tier=O->GetIntegerField(TEXT("tier"));double Price=-1;O->TryGetNumberField(TEXT("official_price_gold"),Price);I.Price=int32(Price);const auto P=O->GetObjectField(TEXT("passive"));double N=0;I.QualityEligible=O->GetBoolField(TEXT("craftsmanship_eligible"))&&(P->TryGetNumberField(TEXT("weapon_damage"),N)||P->TryGetNumberField(TEXT("armor"),N));if(!O->Values[TEXT("active")]->IsNull()){const auto A=O->GetObjectField(TEXT("active"));I.QualityEligible|=O->GetBoolField(TEXT("craftsmanship_eligible"))&&A->GetStringField(TEXT("op"))==TEXT("barrier");}Items.Add(I);}
    auto Resolve=[&](const FString& Id){const auto* I=Items.FindByPredicate([&](const auto& D){return D.Id==Id;});return I?I->Kind:Kind(Id);};
    for(const auto& V:Root->GetObjectField(TEXT("acquisition"))->GetArrayField(TEXT("recipes")))
    {const auto O=V->AsObject();FLDAcquisitionRecipe R;R.Id=O->GetStringField(TEXT("id"));R.Output=Resolve(O->GetStringField(TEXT("output")));R.Fee=O->GetIntegerField(TEXT("fee_gold"));R.HoldMs=O->GetIntegerField(TEXT("hold_ms"));const FString Station=O->GetStringField(TEXT("station"));R.Station=Station==TEXT("heart_forge")?2:Station==TEXT("advanced_forge")?1:0;for(const auto& Pair:O->GetObjectField(TEXT("inputs"))->Values){for(int32 N=0;N<Pair.Value->AsNumber();++N){R.Inputs.Add(Resolve(FString(*Pair.Key)));}}Recipes.Add(R);}
    Why.Reset();return (Checksum==LD_B84_ACQUISITION_SHA?Items.Num()==80&&Recipes.Num()==20:Items.Num()==40&&Recipes.Num()==12);
}
bool ULDAcquisitionCatalog::Validate(FString& Why) const
{
    if(Checksum!=LD_ACQUISITION_SHA&&Checksum!=LD_B84_ACQUISITION_SHA||AcquisitionHash(SourceJson)!=Checksum){return false;}auto* C=NewObject<ULDAcquisitionCatalog>();if(!C->Import(SourceJson,Why)||C->Items.Num()!=Items.Num()||C->Recipes.Num()!=Recipes.Num()){return false;}
    for(int32 N=0;N<Items.Num();++N){if(!FLDAcquisitionItem::StaticStruct()->CompareScriptStruct(&Items[N],&C->Items[N],0)){return false;}}
    for(int32 N=0;N<Recipes.Num();++N){if(!FLDAcquisitionRecipe::StaticStruct()->CompareScriptStruct(&Recipes[N],&C->Recipes[N],0)){return false;}}return true;
}
ULDAcquisitionCatalog* ULDAcquisitionCatalog::Load(uint8 Rules){if(Rules==8){static ULDAcquisitionCatalog* Cached=nullptr;if(!Cached){FString Text,Why;if(!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V08/acquisition_b84.json")))){return nullptr;}auto* C=NewObject<ULDAcquisitionCatalog>();if(!C->Import(Text,Why)){return nullptr;}C->AddToRoot();Cached=C;}return Cached;}return LoadObject<ULDAcquisitionCatalog>(nullptr,TEXT("/Game/Lockdown/Generated/V07/DA_AcquisitionCatalog.DA_AcquisitionCatalog"));}
