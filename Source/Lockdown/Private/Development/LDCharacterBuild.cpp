#include "Development/LDCharacterBuild.h"
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

namespace LDDevelopment
{
    const TCHAR* SkillName(ELDSkill Skill)
    {
        static const TCHAR* Names[] = {TEXT("Empty"),TEXT("Step"),TEXT("Ambush"),TEXT("Spark"),TEXT("Patch"),TEXT("Pickpocket"),TEXT("Parley"),TEXT("Analyze"),TEXT("Survey"),TEXT("Heave")};
        return uint8(Skill) < SkillCount ? Names[uint8(Skill)] : TEXT("Invalid");
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
    const TCHAR* SubclassName(uint8 Subclass)
    {
        static const TCHAR* Names[] = {TEXT("None"),TEXT("Hauler"),TEXT("Medic"),TEXT("Cutpurse"),TEXT("Watcher"),TEXT("Channeler"),TEXT("Envoy")};
        return Subclass < 7 ? Names[Subclass] : TEXT("Invalid");
    }
    FLDCharacterBuild StartingBuild(uint8 ClassId, const ULDCharacterCatalog* Catalog)
    {
        check(ClassId < 3);
        FLDCharacterBuild B; B.ClassId = ClassId; B.Ranks.Init(0, SkillCount);
        const TArray<TArray<int32>> Starts = {{12,9,12,8,10,9},{9,12,9,10,12,8},{8,9,10,12,9,12}};
        const TArray<TArray<ELDSkill>> Skills = {{ELDSkill::Heave,ELDSkill::Patch},{ELDSkill::Pickpocket,ELDSkill::Survey},{ELDSkill::Spark,ELDSkill::Analyze}};
        B.Attributes = Catalog ? Catalog->Classes[ClassId].Attributes : Starts[ClassId];
        B.Slots.Init(ELDSkill::None, SlotCount);
        const auto& Starting = Catalog ? Catalog->Classes[ClassId].StartingSkills : Skills[ClassId];
        for (int32 I=0; I<Starting.Num(); ++I) { B.Ranks[int32(Starting[I])] = 1; B.Slots[I] = Starting[I]; }
        return B;
    }
    bool Valid(const FLDCharacterBuild& B)
    {
        if (B.ClassId>=3 || B.Attributes.Num()!=6 || B.Ranks.Num()!=SkillCount || B.Cooldowns.Num()!=SkillCount || B.Slots.Num()!=SlotCount
            || B.Level<1 || B.Level>5 || B.AttributePoints<0 || B.SkillPoints<0 || B.Subclass>6 || !B.Revision || B.Ranks[0]) { return false; }
        for (int32 V : B.Attributes) { if (V<8 || V>16) { return false; } }
        for (int32 I=0; I<SkillCount; ++I) { if (B.Ranks[I]>3 || !FMath::IsFinite(B.Cooldowns[I]) || B.Cooldowns[I]<0) { return false; } }
        TSet<ELDSkill> Seen;
        for (ELDSkill Skill : B.Slots)
        {
            if (Skill==ELDSkill::None) { continue; }
            if (uint8(Skill)>=SkillCount || !B.Ranks[uint8(Skill)] || Seen.Contains(Skill)) { return false; }
            Seen.Add(Skill);
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
    double MaxHP(const FLDCharacterBuild& B) { return FMath::Clamp(100. + 5*(B.Attributes[CON]-10),75.,150.); }
    double BasicDamage(const FLDCharacterBuild& B) { return FMath::Clamp(20. + .75*(B.Attributes[STR]-10),14.,28.); }
    double SpeedFactor(const FLDCharacterBuild& B) { return FMath::Clamp(1. + .01*(B.Attributes[DEX]-10),.90,1.10); }
    double SkillValue(const FLDCharacterBuild& B, ELDSkill S)
    {
        const int32 R = Rank(B,S); if (!R) { return 0; }
        const auto& A=B.Attributes;
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
        XP = FMath::Min<int64>(420, FMath::Clamp<int64>(XP,0,420) + FMath::Min<int64>(Amount,420));
        const int32 NewLevel=LevelForXP(XP), Earned=FMath::Max(0,NewLevel-B.Level);
        B.AttributePoints+=2*Earned; B.SkillPoints+=Earned; B.Level=FMath::Max(B.Level,NewLevel);
        if (Earned) { ++B.Revision; }
    }
    double DeathDelay(int32 Level, double Elapsed) { return FMath::Min(60.,10.+4*(FMath::Clamp(Level,1,5)-1)+5*FMath::FloorToDouble(FMath::Max(0.,Elapsed)/300)); }
    int64 DeathLoss(int64 Gold) { check(Gold>=0); return (Gold/100)*15+((Gold%100)*15+99)/100; }
    FLDBuildPlan PlanFrom(const FLDCharacterBuild& B)
    {
        FLDBuildPlan P; P.ExpectedRevision=B.Revision; P.Attributes=B.Attributes; P.Ranks=B.Ranks; P.Slots=B.Slots; P.Subclass=B.Subclass; return P;
    }
    bool Prepare(const FLDCharacterBuild& B, const FLDBuildPlan& P, FLDCharacterBuild& Result, FString& Error)
    {
        auto Reject=[&](const TCHAR* Reason){Error=Reason;return false;};
        if (!Valid(B) || P.ExpectedRevision!=B.Revision || P.Attributes.Num()!=6 || P.Ranks.Num()!=SkillCount || P.Slots.Num()!=SlotCount || P.Ranks[0]) { return Reject(TEXT("Stale or invalid build plan.")); }
        int32 AttributeCost=0, SkillCost=0;
        for (int32 I=0; I<6; ++I)
        {
            if (P.Attributes[I]<B.Attributes[I] || P.Attributes[I]>16) { return Reject(TEXT("Attributes cannot be refunded or exceed 16.")); }
            AttributeCost+=P.Attributes[I]-B.Attributes[I];
        }
        for (int32 I=1; I<SkillCount; ++I)
        {
            const int32 Old=B.Ranks[I], Next=P.Ranks[I];
            if (Next<Old || Next>3) { return Reject(TEXT("Invalid skill rank.")); }
            if (!Old && Next && (B.Level<2 || !CanTrain(B.ClassId,ELDSkill(I)))) { return Reject(TEXT("Class learning is unavailable.")); }
            if (!Old && Next) { ++SkillCost; }
            for (int32 R=FMath::Max(1,Old); R<Next; ++R) { SkillCost+=R==1?1:2; }
        }
        if (AttributeCost>B.AttributePoints || SkillCost>B.SkillPoints) { return Reject(TEXT("Not enough development points.")); }
        if ((B.Subclass && P.Subclass!=B.Subclass) || P.Subclass>6 || (P.Subclass && (B.Level<4 || (P.Subclass-1)/2!=B.ClassId))) { return Reject(TEXT("Subclass choice is unavailable or permanent.")); }
        FLDCharacterBuild Candidate=B; Candidate.Attributes=P.Attributes; Candidate.Ranks=P.Ranks; Candidate.Slots=P.Slots; Candidate.Subclass=P.Subclass;
        Candidate.AttributePoints-=AttributeCost; Candidate.SkillPoints-=SkillCost; ++Candidate.Revision;
        if (!Valid(Candidate)) { return Reject(TEXT("Equip at most four different learned skills.")); }
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
        TSet<ELDSkill> Seen;
        for (ELDSkill S:C.ClassLearning) { if (Seen.Contains(S) || !LDDevelopment::CanTrain(I,S)) { Error=TEXT("Invalid class learning reference."); return false; } Seen.Add(S); }
        for (uint8 S=1; S<LDDevelopment::SkillCount; ++S) { if (LDDevelopment::CanTrain(I,ELDSkill(S))!=Seen.Contains(ELDSkill(S))) { Error=TEXT("Missing class learning reference."); return false; } }
    }
    Error.Reset(); return true;
}
ULDCharacterCatalog* ULDCharacterCatalog::Load()
{
    return LoadObject<ULDCharacterCatalog>(nullptr,TEXT("/Game/Lockdown/Generated/Data/DA_CharacterCatalog.DA_CharacterCatalog"));
}
