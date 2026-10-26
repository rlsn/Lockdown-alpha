#include "Combat/LDTacticalCatalog.h"
#include "Development/LDB82Contract.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "LDTacticalContract.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
namespace
{
    FString TacticalHash(const FString& Text){FTCHARToUTF8 B(*Text);FSHA256Signature D;SHA256(reinterpret_cast<const uint8*>(B.Get()),B.Length(),D.Signature);return D.ToString().ToLower();}
}
bool ULDTacticalCatalog::Import(const FString& Text,FString& Error)
{
    SourceJson=Text.Replace(TEXT("\r\n"),TEXT("\n"));Checksum=TacticalHash(SourceJson);Abilities.Reset();Starters.Reset();
    auto Reject=[&](){Error=TEXT("Unsupported tactical contract; sync source, importer and native asset together");return false;};
    // Fail closed on every semantic edit, including unknown operations and payloads.
    RulesVersion=Checksum==LD_B82_COMBAT_SHA?8:7;
    if(Checksum!=(RulesVersion==8?LD_B82_COMBAT_SHA:LD_TACTICAL_CONTRACT_SHA)){return Reject();}
    TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJson),Root)||!Root||Root->GetIntegerField(TEXT("schema_version"))!=RulesVersion){return Reject();}
    TMap<FString,ELDSkill> Supported={{TEXT("anchor_bash"),ELDSkill::AnchorBash},{TEXT("brace_wall"),ELDSkill::BraceWall},{TEXT("veil_step"),ELDSkill::VeilStep},{TEXT("hamstring"),ELDSkill::Hamstring},{TEXT("arc_bolt"),ELDSkill::ArcBolt},{TEXT("counterpulse"),ELDSkill::Counterpulse},{TEXT("farwatch_ward"),ELDSkill::FarwatchWard},{TEXT("wire_snare"),ELDSkill::WireSnare},{TEXT("ledger_mark"),ELDSkill::LedgerMark},{TEXT("surety_shell"),ELDSkill::SuretyShell},{TEXT("false_trail"),ELDSkill::FalseTrail},{TEXT("smoke_veil"),ELDSkill::SmokeVeil},{TEXT("purge_touch"),ELDSkill::PurgeTouch},{TEXT("seers_flare"),ELDSkill::SeersFlare},{TEXT("ember_line"),ELDSkill::EmberLine},{TEXT("grapple"),ELDSkill::Grapple},{TEXT("null_seal"),ELDSkill::NullSeal},{TEXT("grounding_field"),ELDSkill::GroundingField},{TEXT("force_wave"),ELDSkill::ForceWave},{TEXT("guard_swap"),ELDSkill::GuardSwap},{TEXT("slipstream"),ELDSkill::Slipstream},{TEXT("remedy_bind"),ELDSkill::RemedyBind},{TEXT("break_sigil"),ELDSkill::BreakSigil},{TEXT("arcane_prison"),ELDSkill::ArcanePrison},{TEXT("temporal_lock"),ELDSkill::TemporalLock},{TEXT("sovereign_ward"),ELDSkill::SovereignWard},{TEXT("grand_exchange"),ELDSkill::GrandExchange},{TEXT("gate_weave"),ELDSkill::GateWeave}};
    if(RulesVersion==8){Supported.Add(TEXT("frostline"),ELDSkill::Frostline);Supported.Add(TEXT("water_jet"),ELDSkill::WaterJet);}
    const TMap<FString,ELDTacticalTarget> Targets={{TEXT("self"),ELDTacticalTarget::Self},{TEXT("hostile_unit"),ELDTacticalTarget::Unit},{TEXT("self_or_observed_unit"),ELDTacticalTarget::SelfOrUnit},{TEXT("ground"),ELDTacticalTarget::Ground},{TEXT("cone"),ELDTacticalTarget::Cone},{TEXT("line"),ELDTacticalTarget::Line},{TEXT("anchor_or_hostile_unit"),ELDTacticalTarget::AnchorOrUnit},{TEXT("owned_marker"),ELDTacticalTarget::Marker},{TEXT("two_observed_units"),ELDTacticalTarget::TwoUnits},{TEXT("two_ground_points"),ELDTacticalTarget::TwoPoints}};
    const auto& Rows=Root->GetArrayField(TEXT("abilities"));if(Rows.Num()!=(RulesVersion==8?29:28)){return Reject();}
    TSet<ELDSkill> Seen;
    for(const auto& Row:Rows)
    {
        const auto O=Row->AsObject();const FString Id=O->GetStringField(TEXT("id"));const FString Target=O->GetStringField(TEXT("targeting"));
        if(!Supported.Contains(Id)||Seen.Contains(Supported[Id])||!Targets.Contains(Target)){return Reject();}
        FLDTacticalDefinition D;D.Id=Id;D.Skill=Supported[Id];Seen.Add(D.Skill);D.Target=Targets[Target];
        D.Windup=O->GetIntegerField(TEXT("windup_ms"));D.Mana=O->GetIntegerField(TEXT("mana_cost"));D.Cooldown=O->GetIntegerField(TEXT("cooldown_ms"));D.Range=O->GetIntegerField(TEXT("range_cm"));D.Area=O->GetIntegerField(TEXT("area_cm"));D.Circle=O->GetIntegerField(TEXT("circle"));
        const auto Scaling=O->GetObjectField(TEXT("rank_scaling"));if(Scaling->Values.Num()!=1){return Reject();}
        for(const auto& Scale:Scaling->Values){D.RankKey=Scale.Key;for(const auto& V:Scale.Value->AsArray()){D.Magnitudes.Add(int32(V->AsNumber()));}}
        static const TCHAR* Attr[]={TEXT("STR"),TEXT("DEX"),TEXT("CON"),TEXT("INT"),TEXT("WIS"),TEXT("CHA")};
        for(const auto& Gate:O->GetArrayField(TEXT("requirements_any")))
        {for(const auto* A:Attr){double N=0;Gate->AsObject()->TryGetNumberField(A,N);D.Gates.Add(int32(N));}}
        Abilities.Add(D);
    }
    for(const auto& V:Root->GetArrayField(TEXT("starter_kits")))
    {const auto O=V->AsObject();FLDStarterKit K;K.Name=O->GetStringField(TEXT("name"));for(const TCHAR* A:{TEXT("STR"),TEXT("DEX"),TEXT("CON"),TEXT("INT"),TEXT("WIS"),TEXT("CHA")}){K.Attributes.Add(O->GetObjectField(TEXT("attributes"))->GetIntegerField(A));}for(const auto& S:O->GetArrayField(TEXT("skills"))){if(!Supported.Contains(S->AsString())){return Reject();}K.Skills.Add(Supported[S->AsString()]);}Starters.Add(K);}
    if(Starters.Num()!=6){return Reject();}
    NullBellPrice=8;NullBellCooldown=12000;Error.Reset();return true;
}
bool ULDTacticalCatalog::NaturalGate(const FLDTacticalDefinition& D,const FLDCharacterBuild& B)
{
    if(B.Attributes.Num()!=6||D.Gates.IsEmpty()||D.Gates.Num()%6){return false;}
    for(int32 I=0;I<D.Gates.Num();I+=6){bool Pass=true;for(int32 A=0;A<6;++A){Pass&=B.Attributes[A]>=D.Gates[I+A];}if(Pass){return true;}}return false;
}
bool ULDTacticalCatalog::Validate(FString& Error) const
{
    if(Checksum!=(RulesVersion==8?LD_B82_COMBAT_SHA:LD_TACTICAL_CONTRACT_SHA)||Checksum!=TacticalHash(SourceJson)){Error=TEXT("Tactical checksum mismatch");return false;}
    auto* Copy=NewObject<ULDTacticalCatalog>();if(!Copy->Import(SourceJson,Error)||Copy->Abilities.Num()!=Abilities.Num()){return false;}
    for(const auto& A:Abilities){const auto* B=Copy->Find(A.Skill);if(!B||B->Id!=A.Id||B->Windup!=A.Windup||B->Mana!=A.Mana||B->Cooldown!=A.Cooldown||B->Range!=A.Range||B->Area!=A.Area||B->Circle!=A.Circle||B->Target!=A.Target||B->RankKey!=A.RankKey||B->Gates!=A.Gates||B->Magnitudes!=A.Magnitudes){Error=TEXT("Derived tactical data differs from source");return false;}}
    if(Starters.Num()!=Copy->Starters.Num()){return false;}for(int32 I=0;I<Starters.Num();++I){if(!FLDStarterKit::StaticStruct()->CompareScriptStruct(&Starters[I],&Copy->Starters[I],0)){Error=TEXT("Starter differs from source");return false;}}
    return NullBellPrice==Copy->NullBellPrice&&NullBellCooldown==Copy->NullBellCooldown;
}
ULDTacticalCatalog* ULDTacticalCatalog::Load(uint8 Rules){if(Rules==8){static ULDTacticalCatalog* Cached=nullptr;if(!Cached){FString Text,Why;if(!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V08/combat_b82.json")))){return nullptr;}auto* C=NewObject<ULDTacticalCatalog>();if(!C->Import(Text,Why)){return nullptr;}C->AddToRoot();Cached=C;}return Cached;}return LoadObject<ULDTacticalCatalog>(nullptr,TEXT("/Game/Lockdown/Generated/V07/DA_CombatCatalog.DA_CombatCatalog"));}
