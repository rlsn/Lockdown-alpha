#include "Development/LDCharacterBuild.h"
#include "Development/LDV08Loadout.h"
#include "Combat/LDTacticalCatalog.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

FLDCharacterBuild::FLDCharacterBuild()
{
    Ranks.Init(0, LDDevelopment::SkillCount);
    Ranks[int32(ELDSkill::Heave)] = Ranks[int32(ELDSkill::Patch)] = 1;
    Slots = {ELDSkill::Heave, ELDSkill::Patch, ELDSkill::None, ELDSkill::None};
    Cooldowns.Init(0, LDDevelopment::SkillCount);
}

bool FLDBuildPlan::NetSerialize(FArchive& Ar,UPackageMap* Map,bool& Success)
{
    if(Ar.IsSaving()&&RulesVersion!=6&&RulesVersion!=7&&RulesVersion!=8){Success=false;Ar.SetError();return false;}
    uint8 Present=ExpectedRevision||Attributes.Num()||Ranks.Num()||Slots.Num()||Subclass?1:0;
    Ar.SerializeBits(&Present,1);Success=true;
    if(!Present){if(Ar.IsLoading()){*this=FLDBuildPlan();}return true;}
    // Preserve v06 bytes. UINT32_MAX is reserved as the extended-wire marker.
    uint32 Header=RulesVersion>=7?MAX_uint32:ExpectedRevision;
    Ar<<Header;
    if(Header==MAX_uint32)
    {
        uint8 Schema=RulesVersion;Ar<<Schema;if(Schema!=7&&Schema!=8){Success=false;Ar.SetError();return false;}
        RulesVersion=Schema;
        // A bounded 32-byte digest, never an unbounded client string allocation.
        uint8 Bytes[32]={};if(Ar.IsSaving()){if(CatalogDigest.Len()!=64){Success=false;Ar.SetError();return false;}for(TCHAR C:CatalogDigest){if(!FChar::IsHexDigit(C)){Success=false;Ar.SetError();return false;}}HexToBytes(CatalogDigest,Bytes);}
        Ar.Serialize(Bytes,32);if(Ar.IsLoading()){CatalogDigest=BytesToHex(Bytes,32).ToLower();}
        Ar<<ExpectedRevision;
    }
    else{RulesVersion=6;ExpectedRevision=Header;CatalogDigest.Reset();}
    const int32 Count=RulesVersion==8?54:RulesVersion==7?38:10,SlotCount=RulesVersion==8?6:RulesVersion==7?5:4;
    if(Ar.IsLoading()){Attributes.SetNum(6);Ranks.SetNum(Count);Slots.SetNum(SlotCount);}
    if(Attributes.Num()!=6||Ranks.Num()!=Count||Slots.Num()!=SlotCount){Success=false;Ar.SetError();return false;}
    for(auto& Value:Attributes){uint8 Wire=Ar.IsLoading()?0:uint8(Value);if(Ar.IsSaving()&&(Value<0||Value>31)){Success=false;}Ar.SerializeBits(&Wire,5);if(Ar.IsLoading()){Value=Wire;}}
    for(auto& Value:Ranks){uint8 Wire=Ar.IsLoading()?0:Value;if(Ar.IsSaving()&&Value>3){Success=false;}Ar.SerializeBits(&Wire,2);if(Ar.IsLoading()){Value=Wire;}}
    for(auto& Value:Slots){uint8 Wire=Ar.IsLoading()?0:uint8(Value);if(RulesVersion>=7){uint16 Wide=Ar.IsLoading()?0:uint16(Value);Ar<<Wide;if(Wide>=(RulesVersion==8?54:38)){Success=false;}Wire=uint8(Wide);}else{if(Ar.IsSaving()&&Wire>15){Success=false;}Ar.SerializeBits(&Wire,4);}if(Ar.IsLoading()){Value=ELDSkill(Wire);}}
    uint8 SubclassWire=Ar.IsLoading()?0:Subclass;if(Ar.IsSaving()&&Subclass>7){Success=false;}Ar.SerializeBits(&SubclassWire,3);if(Ar.IsLoading()){Subclass=SubclassWire;}
    if(RulesVersion==8)
    {
        if(Ar.IsLoading()){Actions.SetNum(6);Bindings.SetNum(11);}
        if(Actions.Num()!=6||Bindings.Num()!=11){Success=false;Ar.SetError();return false;}
        Ar<<InventoryRevision;
        for(auto& Ref:Bindings){Ar<<Ref;}
        for(auto& Ref:Actions){uint8 Source=uint8(Ref.Source),Skill=uint8(Ref.Skill);Ar<<Source<<Skill<<Ref.Instance;uint8 Bytes[64]={};
            if(Ar.IsSaving()){FTCHARToUTF8 Name(*Ref.Definition.ToString());if(Name.Length()>63){Success=false;Ar.SetError();return false;}FMemory::Memcpy(Bytes,Name.Get(),Name.Length());}
            Ar.Serialize(Bytes,64);if(Ar.IsLoading()){Bytes[63]=0;Ref.Source=ELDActionSource(Source);Ref.Skill=ELDSkill(Skill);Ref.Definition=FName(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes)));}
            Success&=Source<=1&&Skill<54;}
    }
    Success&=!Ar.IsError();if(!Success){Ar.SetError();}return Success;
}

namespace LDDevelopment
{
    const TCHAR* SkillName(ELDSkill Skill)
    {
        static const TCHAR* Names[] = {TEXT("Empty"),TEXT("Step"),TEXT("Ambush"),TEXT("Spark"),TEXT("Patch"),TEXT("Pickpocket"),TEXT("Parley"),TEXT("Analyze"),TEXT("Survey"),TEXT("Heave"),TEXT("Anchor Bash"),TEXT("Brace Wall"),TEXT("Veil Step"),TEXT("Hamstring"),TEXT("Arc Bolt"),TEXT("Counterpulse"),TEXT("Farwatch Ward"),TEXT("Wire Snare"),TEXT("Ledger Mark"),TEXT("Surety Shell"),TEXT("False Trail"),TEXT("Smoke Veil"),TEXT("Purge Touch"),TEXT("Seers Flare"),TEXT("Ember Line"),TEXT("Grapple"),TEXT("Null Seal"),TEXT("Grounding Field"),TEXT("Force Wave"),TEXT("Guard Swap"),TEXT("Slipstream"),TEXT("Remedy Bind"),TEXT("Break Sigil"),TEXT("Arcane Prison"),TEXT("Temporal Lock"),TEXT("Sovereign Ward"),TEXT("Grand Exchange"),TEXT("Gate Weave")};
        switch(Skill){case ELDSkill::Firebolt:return TEXT("Firebolt");case ELDSkill::Gust:return TEXT("Gust");case ELDSkill::Entangle:return TEXT("Entangle");case ELDSkill::ChainLightning:return TEXT("Chain Lightning");case ELDSkill::ShatterBurst:return TEXT("Shatter Burst");case ELDSkill::ConductiveField:return TEXT("Conductive Field");default:break;}
        if(Skill==ELDSkill::Frostline){return TEXT("Frostline");}if(Skill==ELDSkill::WaterJet){return TEXT("Water Jet");}
        return uint8(Skill) < V07SkillCount ? Names[uint8(Skill)] : TEXT("Invalid");
    }
    ELDSkill SkillFromName(const FString& Name)
    {
        for (uint8 I=1; I<SkillCount; ++I) { if (Name == SkillName(ELDSkill(I))) { return ELDSkill(I); } }
        return ELDSkill::None;
    }
    const TCHAR* ClassName(uint8 ClassId)
    {
        static const TCHAR* Names[] = {TEXT("Laborer"),TEXT("Rogue"),TEXT("Scholar")};
        return ClassId < 3 ? Names[ClassId] : TEXT("Invalid");
    }
    const TCHAR* PresetName(const FLDCharacterBuild& B)
    {static const TCHAR* Names[]={TEXT("Vanguard"),TEXT("Infiltrator"),TEXT("Arcanist"),TEXT("Seeker"),TEXT("Broker"),TEXT("Trickster")};return B.RulesVersion>=7&&B.Preset>=3&&B.Preset<9?Names[B.Preset-3]:ClassName(B.ClassId);}
    const TCHAR* SubclassName(uint8 Subclass)
    {
        static const TCHAR* Names[] = {TEXT("None"),TEXT("Hauler"),TEXT("Medic"),TEXT("Cutpurse"),TEXT("Watcher"),TEXT("Channeler"),TEXT("Envoy")};
        return Subclass < 7 ? Names[Subclass] : TEXT("Invalid");
    }
    FLDCharacterBuild StartingBuild(uint8 ClassId, const ULDCharacterCatalog* Catalog)
    {
        check(ClassId < 3);
        FLDCharacterBuild B; B.ClassId = ClassId; B.Preset=ClassId; B.Ranks.Init(0, SkillCount);
        const TArray<TArray<int32>> Starts = {{12,9,12,8,10,9},{9,12,9,10,12,8},{8,9,10,12,9,12}};
        const TArray<TArray<ELDSkill>> Skills = {{ELDSkill::Heave,ELDSkill::Patch},{ELDSkill::Pickpocket,ELDSkill::Survey},{ELDSkill::Spark,ELDSkill::Analyze}};
        B.Attributes = Catalog ? Catalog->Classes[ClassId].Attributes : Starts[ClassId];
        B.Slots.Init(ELDSkill::None, SlotCount);
        const auto& Starting = Catalog ? Catalog->Classes[ClassId].StartingSkills : Skills[ClassId];
        for (int32 I=0; I<Starting.Num(); ++I) { B.Ranks[int32(Starting[I])] = 1; B.Slots[I] = Starting[I]; }
        return B;
    }
    FLDCharacterBuild TacticalStart(uint8 C,const FString& Digest,uint8 Rules)
    {
        check(C<9);const uint8 LegacyAdapters[]={0,1,2,1,2,1};auto B=StartingBuild(C<3?C:LegacyAdapters[C-3]);B.Preset=C;B.RulesVersion=Rules;B.CatalogDigest=Digest;
        const TArray<TArray<int32>> Starts={{11,8,10,8,8,8},{8,11,8,8,10,8},{8,8,8,11,8,10}};
        B.Attributes=Starts[B.ClassId];B.Ranks.SetNumZeroed(Rules==8?54:38);B.Cooldowns.SetNumZeroed(Rules==8?54:38);B.Slots.SetNum(Rules==8?6:5);
        if(C>=3){const auto* Catalog=ULDTacticalCatalog::Load(Rules);check(Catalog&&Catalog->Starters.IsValidIndex(C-3));const auto& K=Catalog->Starters[C-3];B.Attributes=K.Attributes;B.Ranks.Init(0,Rules==8?54:38);B.Slots.Init(ELDSkill::None,Rules==8?6:5);for(int32 I=0;I<K.Skills.Num();++I){B.Ranks[uint8(K.Skills[I])]=1;B.Slots[I]=K.Skills[I];}}if(Rules==8){B.Actions.SetNum(6);for(int32 N=0;N<6;++N){B.Actions[N].Skill=B.Slots[N];}}return B;
    }
    bool Valid(const FLDCharacterBuild& B)
    {
        if (B.ClassId>=3 || B.Preset>=(B.RulesVersion>=7?9:3) || B.Attributes.Num()!=6 || B.Ranks.Num()!=Count(B) || B.Cooldowns.Num()!=Count(B) || B.Slots.Num()!=Slots(B)
            || B.Level<1 || B.Level>(B.RulesVersion>=7?15:5) || (B.RulesVersion!=6&&B.RulesVersion!=7&&B.RulesVersion!=8) || (B.RulesVersion>=7&&B.CatalogDigest.Len()!=64) || B.AttributePoints<0 || B.SkillPoints<0 || B.Subclass>6 || !B.Revision || B.Ranks[0]) { return false; }
        for (int32 V : B.Attributes) { if (V<8 || V>16) { return false; } }
        for (int32 I=0; I<Count(B); ++I) { if (B.Ranks[I]>3 || !FMath::IsFinite(B.Cooldowns[I]) || B.Cooldowns[I]<0) { return false; } }
        if(B.RulesVersion==8&&(B.Actions.Num()!=6||!LDV08::PresetEnabled(B.Preset))){return false;}
        if(B.RulesVersion==8){for(int32 N=0;N<6;++N){const auto& A=B.Actions[N];if(uint8(A.Source)>1||(A.Source==ELDActionSource::Learned?(A.Skill!=B.Slots[N]||A.Instance.IsValid()||!A.Definition.IsNone()):(B.Slots[N]!=ELDSkill::None||A.Skill!=ELDSkill::None||!A.Instance.IsValid()||A.Definition.IsNone()))){return false;}}}
        TSet<ELDSkill> Seen;int32 Capstones=0;
        for (ELDSkill Skill : B.Slots)
        {
            if (Skill==ELDSkill::None) { continue; }
            if (uint8(Skill)>=Count(B) || !Implemented(Skill) || !B.Ranks[uint8(Skill)] || (B.RulesVersion>=7&&!LDGear::NaturalGate(B,Skill)) || Seen.Contains(Skill)) { return false; }
            Seen.Add(Skill);if(B.RulesVersion>=7){const auto* C=ULDEquipmentCatalog::Load(B.RulesVersion);const auto* G=C?C->Gate(Skill):nullptr;if(G&&G->Circle==4&&++Capstones>1){return false;}}
        }
        return !B.Subclass || (B.Level>=4 && (B.Subclass-1)/2 == B.ClassId);
    }
    bool CanTrain(uint8 C, ELDSkill S)
    {
        return (C==0 && (S==ELDSkill::Heave || S==ELDSkill::Patch || S==ELDSkill::Step))
            || (C==1 && (S==ELDSkill::Pickpocket || S==ELDSkill::Survey || S==ELDSkill::Ambush || S==ELDSkill::Step))
            || (C==2 && (S==ELDSkill::Spark || S==ELDSkill::Analyze || S==ELDSkill::Parley || S==ELDSkill::Patch));
    }
    int32 Rank(const FLDCharacterBuild& B, ELDSkill S) { return B.Ranks.IsValidIndex(uint8(S)) ? B.Ranks[uint8(S)] : 0; }
    bool Equipped(const FLDCharacterBuild& B, ELDSkill S) { return S!=ELDSkill::None && Rank(B,S)>0 && B.Slots.Contains(S); }
    TArray<int32> Effective(const FLDCharacterBuild& B){TArray<int32> A=B.Attributes;if(B.RulesVersion>=7&&A.Num()==6&&B.Gear.Primary.Num()==6){for(int32 I=0;I<6;++I){A[I]+=B.Gear.Primary[I];}}return A;}
    double TacticalDeathDelay(int32 Level,double Elapsed){return FMath::Min(90.,8.+2*FMath::Clamp(Level,1,15)+FMath::FloorToDouble(FMath::Max(0.,Elapsed)/120));}
    double MaxHP(const FLDCharacterBuild& B) { if(B.RulesVersion>=7){return 600.+25*(Effective(B)[CON]-8)+18*(B.Level-1);} return FMath::Clamp(100. + 5*(B.Attributes[CON]-10),75.,150.); }
    double BasicDamage(const FLDCharacterBuild& B) { if(B.RulesVersion>=7){return B.Gear.WeaponBase+2*FMath::Max(0,Effective(B)[STR]-8);} return FMath::Clamp(20. + .75*(B.Attributes[STR]-10),14.,28.); }
    double SpeedFactor(const FLDCharacterBuild& B) { return FMath::Clamp(1. + .01*(B.Attributes[DEX]-10),.90,1.10); }
    double SkillValue(const FLDCharacterBuild& B, ELDSkill S)
    {
        const int32 R = Rank(B,S); if (!R) { return 0; }
        const auto A=Effective(B);
        switch(S)
        {
        case ELDSkill::Step: return FMath::Clamp(360. + 10*(A[DEX]-10) + 20*(R-1),300.,420.);
        case ELDSkill::Ambush: return FMath::Clamp(60. + .5*(A[STR]-10) + .5*(A[DEX]-10) + 4*(R-1),50.,80.);
        case ELDSkill::Spark: return FMath::Clamp(24. + 1.25*(A[INT]-10) + 4*(R-1) + (B.Subclass==5?3:0),18.,45.);
        case ELDSkill::Patch: return FMath::Clamp(25. + (A[WIS]-10) + 4*(R-1) + (B.Subclass==2?3:0),18.,40.);
        case ELDSkill::Heave: return A[STR] + 2*(R-1) + (B.Subclass==1?2:0);
        case ELDSkill::Survey: return A[WIS] + 2*(R-1) + (B.Subclass==4?2:0);
        case ELDSkill::Parley: return A[CHA] + 2*(R-1) + (B.Subclass==6?2:0);
        case ELDSkill::Analyze: return A[INT] + 2*(R-1);
        default: return 0;
        }
    }
    double Cooldown(ELDSkill S)
    {
        if(uint8(S)>=10){const auto* C=ULDTacticalCatalog::Load(uint8(S)>=38?8:7);const auto* D=C?C->Find(S):nullptr;return D?D->Cooldown/1000.:0;}
        static const double Values[] = {0,7,18,6,12,20,8,6,6,6};
        return uint8(S)<SkillCount ? Values[uint8(S)] : 0;
    }
    double ChannelSeconds(ELDSkill S)
    {
        return S==ELDSkill::Patch || S==ELDSkill::Pickpocket || S==ELDSkill::Survey ? 1 : 2;
    }
    int32 LevelForXP(int64 XP) { return XP>=420?5:XP>=270?4:XP>=150?3:XP>=60?2:1; }
    void AwardXP(FLDCharacterBuild& B, int64& XP, int64 Amount)
    {
        if (Amount<=0) { return; }
        if(B.RulesVersion>=7)
        {
            const int64 Added=FMath::Min<int64>(Amount,MAX_int64-FMath::Max<int64>(XP,0));XP=FMath::Max<int64>(XP,0)+Added;
            const int64 Thresholds[]={0,75,150,300,450,650,875,1125,1400,1700,2025,2375,2750,3150,3575};int32 Level=1;for(int32 I=1;I<15;++I){if(XP>=Thresholds[I]){Level=I+1;}}
            const int32 Earned=FMath::Max(0,Level-B.Level);B.AttributePoints+=Earned;B.SkillPoints+=Earned;B.Level=FMath::Max(B.Level,Level);if(Earned){++B.Revision;}return;
        }
        XP = FMath::Min<int64>(420, FMath::Clamp<int64>(XP,0,420) + FMath::Min<int64>(Amount,420));
        const int32 NewLevel=LevelForXP(XP), Earned=FMath::Max(0,NewLevel-B.Level);
        B.AttributePoints+=2*Earned; B.SkillPoints+=Earned; B.Level=FMath::Max(B.Level,NewLevel);
        if (Earned) { ++B.Revision; }
    }
    double DeathDelay(int32 Level, double Elapsed) { return FMath::Min(60.,10.+4*(FMath::Clamp(Level,1,5)-1)+5*FMath::FloorToDouble(FMath::Max(0.,Elapsed)/300)); }
    int64 DeathLoss(int64 Gold) { check(Gold>=0); return (Gold/100)*15+((Gold%100)*15+99)/100; }
    FLDBuildPlan PlanFrom(const FLDCharacterBuild& B)
    {
        FLDBuildPlan P; P.RulesVersion=B.RulesVersion;P.CatalogDigest=B.CatalogDigest;P.ExpectedRevision=B.Revision; P.Attributes=B.Attributes; P.Ranks=B.Ranks; P.Slots=B.Slots;P.Actions=B.Actions; P.Subclass=B.Subclass; return P;
    }
    bool Prepare(const FLDCharacterBuild& B, const FLDBuildPlan& P, FLDCharacterBuild& Result, FString& Error)
    {
        auto Reject=[&](const TCHAR* Reason){Error=Reason;return false;};
        if (!Valid(B) || P.ExpectedRevision!=B.Revision || P.RulesVersion!=B.RulesVersion || P.CatalogDigest!=B.CatalogDigest || P.Attributes.Num()!=6 || P.Ranks.Num()!=Count(B) || P.Slots.Num()!=Slots(B) || P.Ranks[0]) { return Reject(TEXT("Stale or invalid build plan.")); }
        int32 AttributeCost=0, SkillCost=0;
        for (int32 I=0; I<6; ++I)
        {
            if (P.Attributes[I]<B.Attributes[I] || P.Attributes[I]>16) { return Reject(TEXT("Attributes cannot be refunded or exceed 16.")); }
            AttributeCost+=P.Attributes[I]-B.Attributes[I];
        }
        for (int32 I=1; I<Count(B); ++I)
        {
            const int32 Old=B.Ranks[I], Next=P.Ranks[I];
            if (Next<Old || Next>3 || (Next&&!Implemented(ELDSkill(I))) || (B.RulesVersion>=7&&Next>Old&&((Next>=2&&B.Level<3)||(Next>=3&&B.Level<6)))) { return Reject(TEXT("Invalid skill rank.")); }
            if (!Old && Next && (B.RulesVersion>=7 || B.Level<2 || !CanTrain(B.ClassId,ELDSkill(I)))) { return Reject(TEXT("Class learning is unavailable.")); }
            if (!Old && Next) { ++SkillCost; }
            for (int32 R=FMath::Max(1,Old); R<Next; ++R) { SkillCost+=R==1?1:2; }
        }
        if (AttributeCost>B.AttributePoints || SkillCost>B.SkillPoints) { return Reject(TEXT("Not enough development points.")); }
        if ((B.Subclass && P.Subclass!=B.Subclass) || P.Subclass>6 || (P.Subclass && (B.Level<4 || (P.Subclass-1)/2!=B.ClassId))) { return Reject(TEXT("Subclass choice is unavailable or permanent.")); }
        FLDCharacterBuild Candidate=B; Candidate.Attributes=P.Attributes; Candidate.Ranks=P.Ranks; Candidate.Slots=P.Slots;Candidate.Actions=P.Actions; Candidate.Subclass=P.Subclass;
        Candidate.AttributePoints-=AttributeCost; Candidate.SkillPoints-=SkillCost; ++Candidate.Revision;
        if (!Valid(Candidate)) { return Reject(TEXT("Equip different legal learned skills within the active profile slot limit.")); }
        Result=MoveTemp(Candidate); Error.Reset(); return true;
    }
    bool TheftSucceeds(int32 Roll, int32 D, int32 R, bool Cutpurse, int32 TargetWIS, bool Alert)
    {
        return Roll>=1 && Roll<=20 && Roll+FMath::FloorToInt((D-10)/2.)+R+(Cutpurse?1:0)>=12+FMath::FloorToInt((TargetWIS-10)/2.)+(Alert?2:0);
    }
}

bool ULDCharacterCatalog::Import(const FString& Text, FString& Error)
{
    SourceJson=Text.Replace(TEXT("\r\n"),TEXT("\n"));
    FTCHARToUTF8 Bytes(*SourceJson); FSHA256Signature Digest;
    if (!SHA256(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length(),Digest.Signature)) { Error=TEXT("Cannot hash character catalog."); return false; }
    SourceChecksum=Digest.ToString().ToLower();
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJson),Root) || !Root.IsValid()) { Error=TEXT("Invalid catalog JSON."); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    double Schema=0;
    if (!Root->TryGetNumberField(TEXT("schemaVersion"),Schema) || Schema!=1 || !Root->TryGetArrayField(TEXT("classes"),Rows) || Rows->Num()!=3) { Error=TEXT("Unsupported catalog schema/classes."); return false; }
    Classes.Reset();
    for (const auto& Row:*Rows)
    {
        const auto Object=Row->AsObject(); if (!Object.IsValid()) { Error=TEXT("Invalid class row."); return false; }
        FLDClassDefinition Class;
        const TSharedPtr<FJsonObject>* Attributes=nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Starts=nullptr; const TArray<TSharedPtr<FJsonValue>>* Learning=nullptr;
        if (!Object->TryGetStringField(TEXT("id"),Class.Id) || !Object->TryGetObjectField(TEXT("attributes"),Attributes)
            || !Object->TryGetArrayField(TEXT("startingSkills"),Starts) || !Object->TryGetArrayField(TEXT("classLearning"),Learning)) { Error=TEXT("Incomplete class row."); return false; }
        for (const TCHAR* Key:{TEXT("STR"),TEXT("DEX"),TEXT("CON"),TEXT("INT"),TEXT("WIS"),TEXT("CHA")})
        {
            double Value=0; if (!(*Attributes)->TryGetNumberField(Key,Value) || !FMath::IsFinite(Value) || Value<8 || Value>16 || Value!=FMath::FloorToDouble(Value)) { Error=TEXT("Invalid attribute."); return false; }
            Class.Attributes.Add(int32(Value));
        }
        for (const auto& Value:*Starts) { Class.StartingSkills.Add(LDDevelopment::SkillFromName(Value->AsString())); }
        for (const auto& Value:*Learning) { Class.ClassLearning.Add(LDDevelopment::SkillFromName(Value->AsString())); }
        Classes.Add(MoveTemp(Class));
    }
    return Validate(Error);
}
bool ULDCharacterCatalog::Validate(FString& Error) const
{
    if (SchemaVersion!=1 || Classes.Num()!=3 || SourceChecksum.Len()!=64) { Error=TEXT("Invalid character catalog header."); return false; }
    FTCHARToUTF8 Bytes(*SourceJson); FSHA256Signature Digest;
    if (!SHA256(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length(),Digest.Signature) || Digest.ToString().ToLower()!=SourceChecksum)
    { Error=TEXT("Character catalog checksum mismatch."); return false; }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJson),Root) || !Root.IsValid())
    { Error=TEXT("Character catalog source is invalid."); return false; }
    const TSharedPtr<FJsonObject>* Derived=nullptr; const TSharedPtr<FJsonObject>* Progression=nullptr;
    const TSharedPtr<FJsonObject>* Attributes=nullptr; const TSharedPtr<FJsonObject>* Profiles=nullptr;
    if (!Root->TryGetObjectField(TEXT("derived"),Derived) || !Root->TryGetObjectField(TEXT("progression"),Progression)
        || !Root->TryGetObjectField(TEXT("attributes"),Attributes) || !Root->TryGetObjectField(TEXT("profiles"),Profiles))
    { Error=TEXT("Missing character rule groups."); return false; }
    const TArray<TPair<FString,FString>> Formulas={
        {TEXT("maxHP"),TEXT("clamp(100 + 5 * (CON - 10), 75, 150)")},
        {TEXT("basicDamage"),TEXT("clamp(20 + 0.75 * (STR - 10), 14, 28)")},
        {TEXT("walkSpeed"),TEXT("180 * clamp(1 + 0.01 * (DEX - 10), 0.90, 1.10)")},
        {TEXT("sprintSpeed"),TEXT("300 * clamp(1 + 0.01 * (DEX - 10), 0.90, 1.10)")}};
    for (const auto& Formula:Formulas)
    {
        FString Actual;
        if (!(*Derived)->TryGetStringField(Formula.Key,Actual) || Actual!=Formula.Value) { Error=TEXT("Derived formula differs from schema-one native rules: ")+Formula.Key; return false; }
    }
    const TArray<TSharedPtr<FJsonValue>>* Levels=nullptr;
    if (!(*Progression)->TryGetArrayField(TEXT("cumulativeXP"),Levels) || Levels->Num()!=5)
    { Error=TEXT("Invalid character level thresholds."); return false; }
    const int32 ExpectedLevels[]={0,60,150,270,420};
    for (int32 I=0;I<5;++I) { double Actual=0; if (!(*Levels)[I]->TryGetNumber(Actual)||Actual!=ExpectedLevels[I]) { Error=TEXT("Level thresholds differ from native rules."); return false; } }
    auto NumberIs=[](const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,double Expected)
    { double Actual=0; return Object->TryGetNumberField(Key,Actual)&&Actual==Expected; };
    const TSharedPtr<FJsonObject>* Witness=nullptr;const TSharedPtr<FJsonObject>* Theft=nullptr;const TArray<TSharedPtr<FJsonValue>>* Alternatives=nullptr;
    if(!Root->TryGetObjectField(TEXT("witness"),Witness)||!Root->TryGetObjectField(TEXT("theft"),Theft)
        ||!NumberIs(*Witness,TEXT("threshold"),12)||!NumberIs(*Witness,TEXT("rangeCm"),120)||!NumberIs(*Witness,TEXT("receiptsPerContender"),1)||!NumberIs(*Witness,TEXT("xp"),0)||!NumberIs(*Witness,TEXT("gold"),0)
        ||!(*Witness)->TryGetArrayField(TEXT("alternatives"),Alternatives)||Alternatives->Num()!=4
        ||!NumberIs(*Theft,TEXT("rangeCm"),100)||!NumberIs(*Theft,TEXT("rearTotalSectorDegrees"),120)||!NumberIs(*Theft,TEXT("pairWindowSeconds"),90)
        ||!NumberIs(*Theft,TEXT("channelSeconds"),1)||!NumberIs(*Theft,TEXT("skillCooldownSeconds"),20)||!NumberIs(*Theft,TEXT("transferGoldMaximum"),12)
        ||!NumberIs(*Theft,TEXT("failureAlertSeconds"),30)||!NumberIs(*Theft,TEXT("failureExposureSeconds"),4)||!NumberIs(*Theft,TEXT("residentInitialGold"),36)||!NumberIs(*Theft,TEXT("xp"),0))
    {Error=TEXT("Witness/theft tuning differs from native schema-one rules.");return false;}
    TSet<FString> ExpectedAlternatives={TEXT("Heave"),TEXT("Analyze"),TEXT("Parley"),TEXT("Survey")};for(const auto& A:*Alternatives){if(!ExpectedAlternatives.Remove(A->AsString())){Error=TEXT("Invalid Charter alternative.");return false;}}
    for(const TCHAR* Key:{TEXT("requiresEquipped"),TEXT("rejectInsufficientScoreBeforeCooldown")}){bool Value=false;if(!(*Witness)->TryGetBoolField(Key,Value)||!Value){Error=TEXT("Invalid Charter activation flag.");return false;}}
    for(const auto& Item:TArray<TPair<FString,bool>>{{TEXT("requiresBodyLOS"),true},{TEXT("targetMustBeUnexposed"),true},{TEXT("reservePairAtAcceptedStart"),true},{TEXT("naturalRollOverrides"),false},{TEXT("drawOnInvalidOrPreview"),false},{TEXT("refillWalletOnRespawn"),false}})
    {bool Value=false;if(!(*Theft)->TryGetBoolField(Item.Key,Value)||Value!=Item.Value){Error=TEXT("Invalid theft commitment flag.");return false;}}
    const TArray<TSharedPtr<FJsonValue>>* Rolls=nullptr;FString Bonus,Difficulty;
    if(!(*Theft)->TryGetArrayField(TEXT("rollInclusive"),Rolls)||Rolls->Num()!=2||(*Rolls)[0]->AsNumber()!=1||(*Rolls)[1]->AsNumber()!=20||!NumberIs(*Theft,TEXT("drawsPerValidCompletion"),1)
        ||!(*Theft)->TryGetStringField(TEXT("attackerBonus"),Bonus)||Bonus!=TEXT("floor((DEX - 10) / 2) + R + (Cutpurse ? 1 : 0)")||!(*Theft)->TryGetStringField(TEXT("difficulty"),Difficulty)||Difficulty!=TEXT("12 + floor((targetWIS - 10) / 2) + (targetAlertActive ? 2 : 0)"))
    {Error=TEXT("Invalid theft roll contract.");return false;}
    const TSharedPtr<FJsonObject> *Courier=nullptr,*Sentinel=nullptr,*Sabotage=nullptr,*RegionWorld=nullptr;
    if(!Root->TryGetObjectField(TEXT("courier"),Courier)||!Root->TryGetObjectField(TEXT("sentinel"),Sentinel)||!Root->TryGetObjectField(TEXT("sabotage"),Sabotage)||!Root->TryGetObjectField(TEXT("world"),RegionWorld)
        ||!NumberIs(*Courier,TEXT("maxPaidContracts"),8)||!NumberIs(*Courier,TEXT("collectChannelSeconds"),3)||!NumberIs(*Courier,TEXT("deliverChannelSeconds"),3)||!NumberIs(*Courier,TEXT("nextAcceptDelayAfterDeliverySeconds"),20)||!NumberIs(*Courier,TEXT("rewardGold"),25)||!NumberIs(*Courier,TEXT("rewardXP"),45)||!NumberIs(*Courier,TEXT("rewardCommissionCredits"),1)
        ||!NumberIs(*RegionWorld,TEXT("firstVisitXP"),25)||!NumberIs(*Sentinel,TEXT("maxHP"),100)||!NumberIs(*Sentinel,TEXT("damage"),12)||!NumberIs(*Sentinel,TEXT("reachCm"),96)||!NumberIs(*Sentinel,TEXT("windupSeconds"),.3)||!NumberIs(*Sentinel,TEXT("startIntervalSeconds"),1.2)||!NumberIs(*Sentinel,TEXT("acquireRadiusCm"),500)||!NumberIs(*Sentinel,TEXT("leashRadiusCm"),600)||!NumberIs(*Sentinel,TEXT("resetLivingHPAfterNoLegalTargetSeconds"),6)||!NumberIs(*Sentinel,TEXT("respawnSeconds"),60)||!NumberIs(*Sentinel,TEXT("rewardXP"),45)||!NumberIs(*Sentinel,TEXT("rewardGold"),25)||!NumberIs(*Sentinel,TEXT("maxPaidDefeatsPerContender"),2)
        ||!NumberIs(*Sabotage,TEXT("minimumScore"),14)||!NumberIs(*Sabotage,TEXT("rangeCm"),120)||!NumberIs(*Sabotage,TEXT("channelSeconds"),2)||!NumberIs(*Sabotage,TEXT("offlineSeconds"),30)||!NumberIs(*Sabotage,TEXT("targetLockoutAfterCommitSeconds"),60)||!NumberIs(*Sabotage,TEXT("exposureSeconds"),4))
    {Error=TEXT("Regional reward/encounter/sabotage tuning differs from native schema one.");return false;}
    for(const auto& Entry:TArray<TPair<TSharedPtr<FJsonObject>,FString>>{{*Courier,TEXT("oneActive")},{*Courier,TEXT("deathCancelsActive")},{*Courier,TEXT("paidCountSurvivesDeath")},{*RegionWorld,TEXT("firstVisitRequiresTraversal")}}){bool Value=false;if(!Entry.Key->TryGetBoolField(Entry.Value,Value)||!Value){Error=TEXT("Invalid regional receipt flag.");return false;}}
    for(const auto& Entry:TArray<TPair<TSharedPtr<FJsonObject>,FString>>{{*Sentinel,TEXT("targetsResidents")},{*Sabotage,TEXT("changesHeartDefence")}}){bool Value=true;if(!Entry.Key->TryGetBoolField(Entry.Value,Value)||Value){Error=TEXT("Invalid encounter target/defence flag.");return false;}}
    const TSharedPtr<FJsonObject>* Seal=nullptr;FString CraftLocation;
    if(!Root->TryGetObjectField(TEXT("seal"),Seal)||!NumberIs(*Seal,TEXT("commissionCreditCost"),4)||!NumberIs(*Seal,TEXT("goldCost"),100)||!NumberIs(*Seal,TEXT("channelSeconds"),4)||!NumberIs(*Seal,TEXT("maxCraftsPerContender"),1)||!NumberIs(*Seal,TEXT("carryCapacity"),1)||!(*Seal)->TryGetStringField(TEXT("location"),CraftLocation)||CraftLocation!=TEXT("Guild"))
    {Error=TEXT("Seal fabrication tuning differs from native schema one.");return false;}
    for(const auto& Entry:TArray<TPair<FString,bool>>{{TEXT("requiresWitness"),true},{TEXT("dropOnDeath"),true},{TEXT("pickupRequiresWitness"),false},{TEXT("assemblyConsumesSkillSlot"),false},{TEXT("normalSeeded"),false}})
    {bool Value=false;if(!(*Seal)->TryGetBoolField(Entry.Key,Value)||Value!=Entry.Value){Error=TEXT("Invalid Seal fabrication flag.");return false;}}
    const TSharedPtr<FJsonObject>* Books=nullptr;const TSharedPtr<FJsonObject>* Preparation=nullptr;const TArray<TSharedPtr<FJsonValue>>* Stock=nullptr;
    if(!Root->TryGetObjectField(TEXT("books"),Books)||!Root->TryGetObjectField(TEXT("preparation"),Preparation)
        ||!NumberIs(*Books,TEXT("priceGold"),40)||!NumberIs(*Books,TEXT("unreadCapacity"),4)||!NumberIs(*Books,TEXT("readChannelSeconds"),3)
        ||!NumberIs(*Books,TEXT("quietSeconds"),6)||!NumberIs(*Books,TEXT("taughtRank"),1)||!NumberIs(*Books,TEXT("skillPointCost"),0)
        ||!NumberIs(*Preparation,TEXT("rangeCm"),120)||!NumberIs(*Preparation,TEXT("channelSeconds"),2)||!NumberIs(*Preparation,TEXT("quietSeconds"),6)
        ||!(*Books)->TryGetArrayField(TEXT("skills"),Stock)||Stock->Num()!=4)
    {Error=TEXT("Book/preparation tuning differs from native schema-one rules.");return false;}
    TSet<FString> ExpectedStock={TEXT("Step"),TEXT("Spark"),TEXT("Patch"),TEXT("Parley")};
    for(const auto& Entry:*Stock){const FString Name=Entry->AsString();if(!ExpectedStock.Remove(Name)){Error=TEXT("Invalid book stock.");return false;}}
    const TArray<TSharedPtr<FJsonValue>>* Skills=nullptr;
    if (!Root->TryGetArrayField(TEXT("skills"),Skills)||Skills->Num()!=LDDevelopment::SkillCount-1)
    { Error=TEXT("Expected exactly nine active skill definitions."); return false; }
    const TCHAR* FormulaKeys[]={TEXT(""),TEXT("distanceCm"),TEXT("damage"),TEXT("damage"),TEXT("heal"),TEXT("resolution"),TEXT("score"),TEXT("score"),TEXT("score"),TEXT("score")};
    const TCHAR* SkillFormulas[]={TEXT(""),
        TEXT("clamp(360 + 10 * (DEX - 10) + 20 * (R - 1), 300, 420)"),
        TEXT("clamp(60 + 0.5 * (STR - 10) + 0.5 * (DEX - 10) + 4 * (R - 1), 50, 80)"),
        TEXT("clamp(24 + 1.25 * (INT - 10) + 4 * (R - 1) + ChannelerBonus, 18, 45)"),
        TEXT("clamp(25 + (WIS - 10) + 4 * (R - 1) + MedicBonus, 18, 40)"),TEXT("theft"),
        TEXT("CHA + 2 * (R - 1) + EnvoyBonus"),TEXT("INT + 2 * (R - 1)"),TEXT("WIS + 2 * (R - 1) + WatcherBonus"),TEXT("STR + 2 * (R - 1) + HaulerBonus")};
    TSet<ELDSkill> SkillIds;
    for (const auto& Row:*Skills)
    {
        const auto Object=Row->AsObject(); FString Id,Input,Formula;
        if (!Object.IsValid()||!Object->TryGetStringField(TEXT("id"),Id)) { Error=TEXT("Invalid skill row."); return false; }
        const ELDSkill Skill=LDDevelopment::SkillFromName(Id);const uint8 Index=uint8(Skill);
        if (!Index||SkillIds.Contains(Skill)||!NumberIs(Object,TEXT("cooldownSeconds"),LDDevelopment::Cooldown(Skill))
            ||!Object->TryGetStringField(TEXT("input"),Input)||Input!=(Index<4?TEXT("press"):TEXT("hold"))
            ||!Object->TryGetStringField(FormulaKeys[Index],Formula)||Formula!=SkillFormulas[Index])
        { Error=TEXT("Skill formula, input or cooldown differs from native schema-one rules: ")+Id; return false; }
        SkillIds.Add(Skill);
        if ((Index>=4&&!NumberIs(Object,TEXT("channelSeconds"),LDDevelopment::ChannelSeconds(Skill)))
            ||(Skill==ELDSkill::Step&&!NumberIs(Object,TEXT("travelSeconds"),.2))
            ||(Skill==ELDSkill::Ambush&&(!NumberIs(Object,TEXT("windupSeconds"),.5)||!NumberIs(Object,TEXT("recoverySeconds"),.25)||!NumberIs(Object,TEXT("rangeCm"),112)||!NumberIs(Object,TEXT("aimTotalConeDegrees"),60)))
            ||(Skill==ELDSkill::Spark&&(!NumberIs(Object,TEXT("windupSeconds"),.4)||!NumberIs(Object,TEXT("recoverySeconds"),.35)||!NumberIs(Object,TEXT("rangeCm"),500)||!NumberIs(Object,TEXT("aimTotalConeDegrees"),60)||!NumberIs(Object,TEXT("targets"),1))))
        { Error=TEXT("Skill timing/target contract differs from native rules: ")+Id; return false; }
    }
    const TSharedPtr<FJsonObject>* Integration=nullptr;
    if (!NumberIs(*Attributes,TEXT("minimum"),8) || !NumberIs(*Attributes,TEXT("maximum"),16) || !NumberIs(*Attributes,TEXT("startTotal"),60)
        || !NumberIs(*Progression,TEXT("xpCap"),420) || !NumberIs(*Progression,TEXT("attributePointsPerLevel"),2) || !NumberIs(*Progression,TEXT("skillPointsPerLevel"),1)
        || !(*Profiles)->TryGetObjectField(TEXT("integration"),Integration) || !NumberIs(*Integration,TEXT("maxHumans"),4)
        || !NumberIs(*Integration,TEXT("deadlineSeconds"),1500) || !NumberIs(*Integration,TEXT("maxLevel"),5) || !NumberIs(*Integration,TEXT("activeSlots"),4))
    { Error=TEXT("Character/profile tuning differs from native schema-one rules."); return false; }
    for (uint8 I=0; I<3; ++I)
    {
        const auto Expected=LDDevelopment::StartingBuild(I);
        const auto& C=Classes[I];
        if (C.Id!=LDDevelopment::ClassName(I) || C.Attributes!=Expected.Attributes || C.StartingSkills.Num()!=2
            || C.StartingSkills[0]!=Expected.Slots[0] || C.StartingSkills[1]!=Expected.Slots[1]) { Error=TEXT("Class preset differs from schema-one rules."); return false; }
        TSet<ELDSkill> Seen;int32 Capstones=0;
        for (ELDSkill S:C.ClassLearning) { if (Seen.Contains(S) || !LDDevelopment::CanTrain(I,S)) { Error=TEXT("Invalid class learning reference."); return false; } Seen.Add(S); }
        for (uint8 S=1; S<LDDevelopment::SkillCount; ++S) { if (LDDevelopment::CanTrain(I,ELDSkill(S))!=Seen.Contains(ELDSkill(S))) { Error=TEXT("Missing class learning reference."); return false; } }
    }
    Error.Reset(); return true;
}
ULDCharacterCatalog* ULDCharacterCatalog::Load()
{
    return LoadObject<ULDCharacterCatalog>(nullptr,TEXT("/Game/Lockdown/Generated/Data/DA_CharacterCatalog.DA_CharacterCatalog"));
}
