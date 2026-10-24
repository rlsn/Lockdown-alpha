#include "Inventory/LDEquipmentCatalog.h"
#include "LDEquipmentContract.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
namespace {FString Hash(const FString& S){FTCHARToUTF8 B(*S);FSHA256Signature H;SHA256(reinterpret_cast<const uint8*>(B.Get()),B.Length(),H.Signature);return H.ToString().ToLower();}}
bool ULDEquipmentCatalog::Import(const FString& Text,FString& Why)
{
    SourceJson=Text.Replace(TEXT("\r\n"),TEXT("\n"));Checksum=Hash(SourceJson);Items.Reset();Gates.Reset();
    if(Checksum!=LD_EQUIPMENT_SHA){Why=TEXT("Unsupported equipment semantics; sync implementation and native asset");return false;}
    TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJson),Root)||!Root){return false;}
    const TMap<FString,ELDItemKind> Kinds={{TEXT("iron_halberd"),ELDItemKind::IronHalberd},{TEXT("simple_staff"),ELDItemKind::SimpleStaff},{TEXT("runic_staff"),ELDItemKind::RunicStaff},{TEXT("conduit_staff"),ELDItemKind::ConduitStaff},{TEXT("bastion_plate"),ELDItemKind::BastionPlate},{TEXT("archivist_robe"),ELDItemKind::ArchivistRobe},{TEXT("merchant_harness"),ELDItemKind::MerchantHarness},{TEXT("pathfinder_boots"),ELDItemKind::PathfinderBoots},{TEXT("scholars_sigil"),ELDItemKind::ScholarsSigil},{TEXT("warding_eye"),ELDItemKind::WardingEye},{TEXT("weighted_satchel"),ELDItemKind::WeightedSatchel},{TEXT("brokers_seal"),ELDItemKind::BrokersSeal}};
    const TMap<FString,uint8> Slots={{TEXT("weapon"),0},{TEXT("offhand"),1},{TEXT("body"),2},{TEXT("feet"),3},{TEXT("charm"),4}};
    const TCHAR* Attr[]={TEXT("STR"),TEXT("DEX"),TEXT("CON"),TEXT("INT"),TEXT("WIS"),TEXT("CHA")};
    for(const auto& V:Root->GetArrayField(TEXT("items")))
    {
        const auto O=V->AsObject();FLDEquipmentDefinition D;D.Id=O->GetStringField(TEXT("id"));D.Kind=Kinds[D.Id];D.Name=O->GetStringField(TEXT("name"));D.Binding=Slots[O->GetStringField(TEXT("equipment_slot"))];D.TwoHanded=O->GetBoolField(TEXT("two_handed"));D.Weight=O->GetIntegerField(TEXT("weight"));
        auto& S=D.Stats;for(int32 A=0;A<6;++A){double N=0;O->GetObjectField(TEXT("primary_stats"))->TryGetNumberField(Attr[A],N);S.Primary[A]=int32(N);}
        const auto P=O->GetObjectField(TEXT("passive"));auto N=[&](const TCHAR* K,double Default=0.){double R=Default;P->TryGetNumberField(K,R);return R;};
        S.WeaponBase=N(TEXT("weapon_damage"),42);S.Reach=int32(N(TEXT("reach_cm"),96));S.Interval=int32(N(TEXT("attack_interval_ms"),1200));S.Armor=N(TEXT("armor"));S.FeeDiscount=int32(N(TEXT("discount_gold")));S.Movement=int32(N(TEXT("movement_percent")));S.RoadMovement=int32(N(TEXT("road_movement_percent")));S.MPRegen=FMath::RoundToInt(100*N(TEXT("mp_regen_per_second")));S.Capacity=int32(N(TEXT("capacity_bonus")));S.SpellPercent=int32(N(TEXT("spell_percent")));S.CastRange=int32(N(TEXT("cast_range_bonus_cm")));S.ControlPotency=uint8(N(TEXT("potency_rank")));S.Detector=D.Kind==ELDItemKind::WardingEye?250:0;
        Items.Add(D);
    }
    for(const auto& V:Root->GetArrayField(TEXT("ability_gates")))
    {const auto O=V->AsObject();FLDAbilityGate G;G.Skill=ELDSkill(O->GetIntegerField(TEXT("skill")));G.Circle=O->GetIntegerField(TEXT("circle"));G.School=O->GetStringField(TEXT("school"));for(const auto& A:O->GetArrayField(TEXT("requirements_any"))){for(const auto* K:Attr){double N=0;A->AsObject()->TryGetNumberField(K,N);G.Alternatives.Add(int32(N));}}for(const auto& K:O->GetArrayField(TEXT("potency_fields"))){G.Potency.Add(K->AsString());}Gates.Add(G);}
    Why.Reset();return true;
}
bool ULDEquipmentCatalog::Validate(FString& Why) const
{
    if(Checksum!=LD_EQUIPMENT_SHA||Hash(SourceJson)!=Checksum){Why=TEXT("Equipment checksum mismatch");return false;}
    auto* C=NewObject<ULDEquipmentCatalog>();if(!C->Import(SourceJson,Why)||C->Items.Num()!=Items.Num()||C->Gates.Num()!=Gates.Num()){return false;}
    for(int32 I=0;I<Items.Num();++I){if(!FLDEquipmentDefinition::StaticStruct()->CompareScriptStruct(&Items[I],&C->Items[I],0)){Why=TEXT("Derived equipment differs from source");return false;}}
    for(int32 I=0;I<Gates.Num();++I){if(!FLDAbilityGate::StaticStruct()->CompareScriptStruct(&Gates[I],&C->Gates[I],0)){Why=TEXT("Derived gate differs from source");return false;}}return true;
}
ULDEquipmentCatalog* ULDEquipmentCatalog::Load(){return LoadObject<ULDEquipmentCatalog>(nullptr,TEXT("/Game/Lockdown/Generated/V07/DA_EquipmentCatalog.DA_EquipmentCatalog"));}
