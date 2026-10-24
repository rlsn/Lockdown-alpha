#include "Combat/LDTacticalCatalog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
namespace
{
    FString Hash(const FString& Text){FTCHARToUTF8 B(*Text);FSHA256Signature D;SHA256(reinterpret_cast<const uint8*>(B.Get()),B.Length(),D.Signature);return D.ToString().ToLower();}
}
bool ULDTacticalCatalog::Import(const FString& Text,FString& Error)
{
    auto Reject=[&](const TCHAR* Why){Error=Why;return false;};
    TSharedPtr<FJsonObject> Root;SourceJson=Text.Replace(TEXT("\r\n"),TEXT("\n"));Checksum=Hash(SourceJson);Abilities.Reset();
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJson),Root)||!Root||Root->GetIntegerField(TEXT("schema_version"))!=7){return Reject(TEXT("Invalid B71 catalog schema"));}
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Root->TryGetArrayField(TEXT("abilities"),Rows)||Rows->Num()!=4){return Reject(TEXT("B71 requires exactly four implemented abilities"));}
    const TMap<FString,ELDSkill> Supported={{TEXT("anchor_bash"),ELDSkill::AnchorBash},{TEXT("arc_bolt"),ELDSkill::ArcBolt},{TEXT("purge_touch"),ELDSkill::PurgeTouch},{TEXT("surety_shell"),ELDSkill::SuretyShell}};
    TSet<ELDSkill> Seen;
    for(const auto& Row:*Rows)
    {
        const auto O=Row->AsObject();if(!O){return Reject(TEXT("Invalid ability row"));}
        FString Id;if(!O->TryGetStringField(TEXT("id"),Id)||!Supported.Contains(Id)||Seen.Contains(Supported[Id])){return Reject(TEXT("Unsupported or duplicate B71 ability"));}
        FLDTacticalDefinition D;D.Id=Id;D.Skill=Supported[Id];Seen.Add(D.Skill);
        auto Integer=[&](const TCHAR* Key,int32 Max,int32& Value){double N=0;if(!O->TryGetNumberField(Key,N)||!FMath::IsFinite(N)||N<0||N>Max||N!=FMath::FloorToDouble(N)){return false;}Value=int32(N);return true;};
        if(!Integer(TEXT("windup_ms"),10000,D.Windup)||!Integer(TEXT("mana_cost"),1000,D.Mana)||!Integer(TEXT("cooldown_ms"),300000,D.Cooldown)||!D.Cooldown||!Integer(TEXT("range_cm"),2000,D.Range)){return Reject(TEXT("Invalid action number"));}
        const TArray<TSharedPtr<FJsonValue>>* Effects=nullptr;if(!O->TryGetArrayField(TEXT("effects"),Effects)){return Reject(TEXT("Missing operation array"));}
        TArray<FString> Operations;for(const auto& Effect:*Effects){const auto E=Effect->AsObject();FString Op;if(!E||!E->TryGetStringField(TEXT("op"),Op)){return Reject(TEXT("Invalid operation"));}Operations.Add(Op);}
        const TArray<FString> Expected=D.Skill==ELDSkill::AnchorBash?TArray<FString>{TEXT("damage"),TEXT("status")}:D.Skill==ELDSkill::ArcBolt?TArray<FString>{TEXT("projectile_damage")}:D.Skill==ELDSkill::PurgeTouch?TArray<FString>{TEXT("basic_dispel")}:TArray<FString>{TEXT("barrier")};
        if(Operations!=Expected){return Reject(TEXT("Unimplemented B71 operation sequence"));}
        // Pin semantic payloads as well as op names; unsupported edits cannot silently work.
        const auto First=(*Effects)[0]->AsObject();
        if(D.Skill==ELDSkill::AnchorBash&&(First->GetStringField(TEXT("kind"))!=TEXT("physical")||First->GetNumberField(TEXT("amount"))!=45||(*Effects)[1]->AsObject()->GetStringField(TEXT("id"))!=TEXT("stun"))){return Reject(TEXT("Unsupported Anchor Bash semantics"));}
        if(D.Skill==ELDSkill::ArcBolt&&(First->GetStringField(TEXT("kind"))!=TEXT("magical")||First->GetIntegerField(TEXT("speed_cm_s"))!=1200)){return Reject(TEXT("Unsupported projectile semantics"));}
        if(D.Skill==ELDSkill::PurgeTouch&&First->GetStringField(TEXT("polarity"))!=TEXT("self-negative; other-negative-if-consented-else-positive")){return Reject(TEXT("Unsupported dispel polarity"));}
        const auto Scaling=O->GetObjectField(TEXT("rank_scaling"));const TCHAR* Key=D.Skill==ELDSkill::AnchorBash?TEXT("stun_ms"):D.Skill==ELDSkill::ArcBolt?TEXT("damage"):D.Skill==ELDSkill::PurgeTouch?TEXT("cooldown_ms"):TEXT("barrier");
        if(!Scaling||Scaling->Values.Num()!=1){return Reject(TEXT("Unsupported rank schema"));}
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!Scaling->TryGetArrayField(Key,Values)||Values->Num()!=3){return Reject(TEXT("Invalid ranks"));}
        for(const auto& V:*Values){double N=0;if(!V->TryGetNumber(N)||!FMath::IsFinite(N)||N<=0||N>300000||N!=FMath::FloorToDouble(N)){return Reject(TEXT("Invalid rank value"));}D.Magnitudes.Add(int32(N));}
        Abilities.Add(D);
    }
    const TSharedPtr<FJsonObject>* ItemPtr=nullptr;const TSharedPtr<FJsonObject>* Active=nullptr;
    if(!Root->TryGetObjectField(TEXT("null_bell"),ItemPtr)||!(*ItemPtr)->TryGetObjectField(TEXT("active"),Active)){return Reject(TEXT("Missing Null Bell action"));}
    const auto Item=*ItemPtr;
    if(Item->GetStringField(TEXT("id"))!=TEXT("null_bell")||(*Active)->GetStringField(TEXT("op"))!=TEXT("basic_dispel_self")||Item->GetIntegerField(TEXT("official_price_gold"))!=8||Item->GetIntegerField(TEXT("cooldown_ms"))!=12000){return Reject(TEXT("Unsupported Null Bell contract"));}
    NullBellPrice=8;NullBellCooldown=12000;Error.Reset();return true;
}
bool ULDTacticalCatalog::Validate(FString& Error) const
{
    if(Checksum.Len()!=64||Checksum!=Hash(SourceJson)){Error=TEXT("B71 catalog checksum mismatch");return false;}
    auto* Copy=NewObject<ULDTacticalCatalog>();if(!Copy->Import(SourceJson,Error)||Copy->Abilities.Num()!=Abilities.Num()){return false;}
    for(const auto& A:Abilities){const auto* B=Copy->Find(A.Skill);if(!B||B->Id!=A.Id||B->Windup!=A.Windup||B->Mana!=A.Mana||B->Cooldown!=A.Cooldown||B->Range!=A.Range||B->Magnitudes!=A.Magnitudes){Error=TEXT("B71 derived catalog differs from source");return false;}}
    return NullBellPrice==Copy->NullBellPrice&&NullBellCooldown==Copy->NullBellCooldown;
}
ULDTacticalCatalog* ULDTacticalCatalog::Load(){return LoadObject<ULDTacticalCatalog>(nullptr,TEXT("/Game/Lockdown/Generated/V07/DA_CombatCatalog.DA_CombatCatalog"));}
