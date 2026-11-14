#include "Development/LDBuildBlueprint.h"
#include "LDDemoContentJson.h"

#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace LDBuildBlueprintDetail
{
using FValue = TSharedPtr<FJsonValue>;

const TCHAR* const MissingFieldText = TEXT("missing or multiply consumed field");

FValue JsonInt(int64 Value) { return MakeShared<FJsonValueNumberString>(FString::Printf(TEXT("%lld"), Value)); }
FValue JsonStr(const FString& Value) { return MakeShared<FJsonValueString>(Value); }
FValue JsonBool(bool bValue) { return MakeShared<FJsonValueBoolean>(bValue); }
FValue JsonNull() { return MakeShared<FJsonValueNull>(); }
FValue JsonArr(const TArray<FValue>& Values) { return MakeShared<FJsonValueArray>(Values); }
FValue JsonObj(const TSharedRef<FJsonObject>& Object) { return MakeShared<FJsonValueObject>(Object); }

const TCHAR* SlotOf(ELDCombatEntry Entry)
{
    switch (Entry)
    {
    case ELDCombatEntry::Q: return TEXT("Q");
    case ELDCombatEntry::W: return TEXT("W");
    case ELDCombatEntry::E: return TEXT("E");
    default: return nullptr;
    }
}

int32 SlotOrder(const FString& Slot)
{
    return Slot == TEXT("Q") ? 0 : Slot == TEXT("W") ? 1 : Slot == TEXT("E") ? 2 : INDEX_NONE;
}

// Stable install-parameter values of a blueprint component (the catalog element ids; no value for None).
const TCHAR* ElementKey(ELDDemoElement Element)
{
    switch (Element)
    {
    case ELDDemoElement::Fire: return TEXT("fire");
    case ELDDemoElement::Water: return TEXT("water");
    case ELDDemoElement::Electric: return TEXT("electric");
    default: return nullptr;
    }
}

const TCHAR* TargetKindKey(ELDComponentTargetKind Kind)
{
    switch (Kind)
    {
    case ELDComponentTargetKind::Active: return TEXT("active");
    case ELDComponentTargetKind::Component: return TEXT("component");
    case ELDComponentTargetKind::Dash: return TEXT("dash");
    default: return nullptr;
    }
}

// A recorded parameter is one of the keys StructureOf writes, with a value it can write.
bool ValidParameter(const FLDBlueprintParameter& Parameter)
{
    if (Parameter.Key == TEXT("element"))
    {
        return Parameter.Value == TEXT("fire") || Parameter.Value == TEXT("water") || Parameter.Value == TEXT("electric");
    }
    if (Parameter.Key == TEXT("target_kind"))
    {
        return Parameter.Value == TEXT("active") || Parameter.Value == TEXT("component") || Parameter.Value == TEXT("dash");
    }
    return Parameter.Key == TEXT("bound_component") && !Parameter.Value.IsEmpty();
}

bool Before(const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; }

bool IsHex(const FString& Text, int32 Length)
{
    if (Text.Len() != Length) { return false; }
    for (const TCHAR C : Text)
    {
        if (!FChar::IsHexDigit(C)) { return false; }
    }
    return true;
}

TSharedRef<FJsonObject> WriteStructure(const FLDBlueprintStructure& Structure, const TSharedRef<FJsonObject>& Into)
{
    TArray<FValue> Stats;
    for (const FLDBlueprintStat& Stat : Structure.Stats)
    {
        const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetField(TEXT("stat"), JsonStr(Stat.Stat));
        Row->SetField(TEXT("points"), JsonInt(Stat.Points));
        Stats.Add(JsonObj(Row));
    }
    TArray<FValue> Actives;
    for (const FLDBlueprintActive& Active : Structure.Actives)
    {
        const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetField(TEXT("slot"), JsonStr(Active.Slot));
        Row->SetField(TEXT("ability"), JsonStr(Active.Ability));
        Actives.Add(JsonObj(Row));
    }
    const TSharedRef<FJsonObject> Components = MakeShared<FJsonObject>();
    Components->SetField(TEXT("collected"), JsonBool(Structure.bComponentsCollected));
    if (Structure.bComponentsCollected)
    {
        TArray<FValue> Items;
        for (const FLDBlueprintComponent& Component : Structure.Components)
        {
            const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
            Row->SetField(TEXT("component"), JsonStr(Component.Component));
            Row->SetField(TEXT("host"), JsonStr(Component.Host));
            TArray<FValue> Parameters;
            for (const FLDBlueprintParameter& Parameter : Component.Parameters)
            {
                const TSharedRef<FJsonObject> Pair = MakeShared<FJsonObject>();
                Pair->SetField(TEXT("key"), JsonStr(Parameter.Key));
                Pair->SetField(TEXT("value"), JsonStr(Parameter.Value));
                Parameters.Add(JsonObj(Pair));
            }
            Row->SetField(TEXT("parameters"), JsonArr(Parameters));
            Items.Add(JsonObj(Row));
        }
        Components->SetField(TEXT("items"), JsonArr(Items));
    }
    else
    {
        Components->SetField(TEXT("items"), JsonNull());
    }
    Into->SetField(TEXT("stat_points"), JsonArr(Stats));
    Into->SetField(TEXT("actives"), JsonArr(Actives));
    Into->SetField(TEXT("components"), JsonObj(Components));
    if(Structure.FinalMod.IsEmpty()){Into->SetField(TEXT("final_mod"),JsonNull());}
    else
    {
        const auto& Selected=Structure.FinalMod;
        const TSharedRef<FJsonObject> Final=MakeShared<FJsonObject>();
        Final->SetField(TEXT("definition"),JsonStr(Selected.DefinitionId.ToString()));
        const TCHAR* Kind=TargetKindKey(Selected.TargetKind);
        Final->SetField(TEXT("target_kind"),JsonStr(Kind?Kind:TEXT("none")));
        Final->SetField(TEXT("target_id"),JsonStr(Selected.TargetId.IsNone()?FString():Selected.TargetId.ToString()));
        Into->SetField(TEXT("final_mod"),JsonObj(Final));
    }
    return Into;
}

using LDDemoContent::FCheck;
using LDDemoContent::FFields;

bool ReadStructure(FFields& Fields, FLDBlueprintStructure& Out)
{
    FCheck& Check = Fields.Check;
    const FString Path = Fields.Path;
    const TArray<FValue>* Stats = nullptr;
    if (!LDDemoContent::Array(Fields.Take(TEXT("stat_points")), Stats, Check, Path + TEXT(".stat_points"), 0, 64))
    {
        return false;
    }
    for (int32 Index = 0; Index < Stats->Num(); ++Index)
    {
        FFields Row(Check, (*Stats)[Index], FString::Printf(TEXT("%s.stat_points[%d]"), *Path, Index));
        FLDBlueprintStat Stat;
        int32 Points = 0;
        if (!Row.Identifier(TEXT("stat"), Stat.Stat) || !Row.SmallInt(TEXT("points"), Points, 1, MAX_int32) || !Row.Done()
            || !Check.Require(Out.Stats.IsEmpty() || Before(Out.Stats.Last().Stat, Stat.Stat), Row.Path,
                TEXT("stats must be unique and sorted by id")))
        {
            return false;
        }
        Stat.Points = Points;
        Out.Stats.Add(MoveTemp(Stat));
    }
    const TArray<FValue>* Actives = nullptr;
    if (!LDDemoContent::Array(Fields.Take(TEXT("actives")), Actives, Check, Path + TEXT(".actives"), 0, 3))
    {
        return false;
    }
    for (int32 Index = 0; Index < Actives->Num(); ++Index)
    {
        FFields Row(Check, (*Actives)[Index], FString::Printf(TEXT("%s.actives[%d]"), *Path, Index));
        FLDBlueprintActive Active;
        if (!Row.Text(TEXT("slot"), Active.Slot, 1, 1) || !Row.Identifier(TEXT("ability"), Active.Ability) || !Row.Done()
            || !Check.Require(SlotOrder(Active.Slot) != INDEX_NONE, Row.Path, TEXT("slot must be Q, W or E"))
            || !Check.Require(Out.Actives.IsEmpty() || SlotOrder(Out.Actives.Last().Slot) < SlotOrder(Active.Slot), Row.Path,
                TEXT("actives must be unique and sorted by slot"))
            || !Check.Require(!Out.Actives.ContainsByPredicate([&Active](const FLDBlueprintActive& Held) { return Held.Ability == Active.Ability; }),
                Row.Path, TEXT("an active is held once")))
        {
            return false;
        }
        Out.Actives.Add(MoveTemp(Active));
    }
    const FValue Final=Fields.Take(TEXT("final_mod"));
    if(!Check.Good){return false;}
    if(Final&&Final->Type!=EJson::Null)
    {
        FFields Row(Check,Final,Path+TEXT(".final_mod"));
        FString Id,Kind,Target;
        if(!Row.Identifier(TEXT("definition"),Id)||!Row.Text(TEXT("target_kind"),Kind,1,9)){return false;}
        if(Kind==TEXT("active")){Out.FinalMod.TargetKind=ELDComponentTargetKind::Active;}
        else if(Kind==TEXT("component")){Out.FinalMod.TargetKind=ELDComponentTargetKind::Component;}
        else if(Kind==TEXT("dash")){Out.FinalMod.TargetKind=ELDComponentTargetKind::Dash;}
        else if(!Check.Require(Kind==TEXT("none"),Row.Path,TEXT("unknown final source kind"))){return false;}
        const bool bNamed=Out.FinalMod.TargetKind==ELDComponentTargetKind::Active||Out.FinalMod.TargetKind==ELDComponentTargetKind::Component;
        if(!(bNamed?Row.Identifier(TEXT("target_id"),Target):Row.Text(TEXT("target_id"),Target,0,0))||!Row.Done()){return false;}
        Out.FinalMod.DefinitionId=FName(*Id);Out.FinalMod.TargetId=FName(*Target);
    }
    FFields Components(Check, Fields.Take(TEXT("components")), Path + TEXT(".components"));
    if (!Components.Flag(TEXT("collected"), Out.bComponentsCollected))
    {
        return false;
    }
    const FValue Items = Components.Take(TEXT("items"));
    if (!Check.Good)
    {
        return false;
    }
    if (!Out.bComponentsCollected)
    {
        return Check.Require(Items && Items->Type == EJson::Null, Components.Path + TEXT(".items"),
            TEXT("components that were not collected are null, never an empty list")) && Components.Done();
    }
    const TArray<FValue>* Rows = nullptr;
    if (!LDDemoContent::Array(Items, Rows, Check, Components.Path + TEXT(".items"), 0, 64))
    {
        return false;
    }
    for (int32 Index = 0; Index < Rows->Num(); ++Index)
    {
        FFields Row(Check, (*Rows)[Index], FString::Printf(TEXT("%s.items[%d]"), *Components.Path, Index));
        FLDBlueprintComponent Component;
        const TArray<FValue>* Parameters = nullptr;
        if (!Row.Identifier(TEXT("component"), Component.Component) || !Row.Text(TEXT("host"), Component.Host, 0, 63)
            || !LDDemoContent::Array(Row.Take(TEXT("parameters")), Parameters, Check, Row.Path + TEXT(".parameters"), 0, 32))
        {
            return false;
        }
        for (int32 ParameterIndex = 0; ParameterIndex < Parameters->Num(); ++ParameterIndex)
        {
            FFields Pair(Check, (*Parameters)[ParameterIndex], FString::Printf(TEXT("%s.parameters[%d]"), *Row.Path, ParameterIndex));
            FLDBlueprintParameter Parameter;
            if (!Pair.Identifier(TEXT("key"), Parameter.Key) || !Pair.Text(TEXT("value"), Parameter.Value, 0, 256) || !Pair.Done()
                || !Check.Require(ValidParameter(Parameter), Pair.Path, TEXT("unknown install parameter or value"))
                || !Check.Require(Component.Parameters.IsEmpty() || Before(Component.Parameters.Last().Key, Parameter.Key), Pair.Path,
                    TEXT("parameters must be unique and sorted by key")))
            {
                return false;
            }
            Component.Parameters.Add(MoveTemp(Parameter));
        }
        if (!Row.Done()
            || !Check.Require(Out.Components.IsEmpty() || Before(Out.Components.Last().Component, Component.Component), Row.Path,
                TEXT("components must be unique and sorted by id")))
        {
            return false;
        }
        Out.Components.Add(MoveTemp(Component));
    }
    return Components.Done();
}

bool ReadFacts(FFields& Version, FLDBlueprintVersion& Out)
{
    FFields Facts(Version.Check, Version.Take(TEXT("facts")), Version.Path + TEXT(".facts"));
    for (int32 Index = 0; Index < int32(ELDBlueprintFact::Count); ++Index)
    {
        const TCHAR* Key = LDBuildBlueprint::FactKey(ELDBlueprintFact(Index));
        FFields Fact(Facts.Check, Facts.Take(Key), Facts.Path + TEXT(".") + Key);
        FLDBlueprintFactValue& Value = Out.Facts[Index];
        if (!Fact.Flag(TEXT("collected"), Value.bCollected))
        {
            return false;
        }
        if (Value.bCollected)
        {
            if (!Fact.Int(TEXT("value"), Value.Value, 0))
            {
                return false;
            }
        }
        else
        {
            const FValue Raw = Fact.Take(TEXT("value"));
            Value.Value = 0;
            if (!Fact.Check.Require(Raw && Raw->Type == EJson::Null, Fact.Path + TEXT(".value"),
                TEXT("a fact that was not collected is null, never 0")))
            {
                return false;
            }
        }
        if (!Fact.Done())
        {
            return false;
        }
    }
    return Facts.Done();
}

bool OneOf(const FString& Value, std::initializer_list<const TCHAR*> Allowed)
{
    for (const TCHAR* Candidate : Allowed)
    {
        if (Value.Equals(Candidate, ESearchCase::CaseSensitive)) { return true; }
    }
    return false;
}

ELDBlueprintParseError Refuse(ELDBlueprintParseError Error, const FString& Message, FString& Detail)
{
    Detail = Message;
    Detail.RemoveFromStart(TEXT("DemoContent: "));
    return Error;
}
}

const TCHAR* LDBuildBlueprint::FactKey(ELDBlueprintFact Fact)
{
    switch (Fact)
    {
    case ELDBlueprintFact::DamageDealt: return TEXT("damage_dealt_p2");
    case ELDBlueprintFact::DamageToContenders: return TEXT("damage_to_contenders_p2");
    case ELDBlueprintFact::DamageTaken: return TEXT("damage_taken_p2");
    case ELDBlueprintFact::ShieldAbsorbed: return TEXT("shield_absorbed_p2");
    case ELDBlueprintFact::HealthLost: return TEXT("health_lost_p2");
    case ELDBlueprintFact::Healing: return TEXT("healing_p2");
    case ELDBlueprintFact::ShieldGranted: return TEXT("shield_granted_p2");
    case ELDBlueprintFact::BasicAttacks: return TEXT("basic_attacks");
    case ELDBlueprintFact::AbilityCasts: return TEXT("ability_casts");
    case ELDBlueprintFact::Regeneration: return TEXT("regeneration_p2");
    case ELDBlueprintFact::DebtRepaid: return TEXT("debt_repaid_p2");
    case ELDBlueprintFact::DamageReduced: return TEXT("damage_reduced_p2");
    case ELDBlueprintFact::Triggers: return TEXT("triggers");
    case ELDBlueprintFact::Rejections: return TEXT("rejections");
    default: return TEXT("unknown");
    }
}

bool LDBuildBlueprint::IsFactCollected(ELDBlueprintFact Fact)
{
    return int32(Fact) >= 0 && int32(Fact) < int32(ELDBlueprintFact::Count);
}

// ---------------------------------------------------------------- ledger

void FLDBuildLedger::Reset()
{
    Contenders.Reset();
    Effects.Reset();
}

FLDBlueprintStructure FLDBuildLedger::StructureOf(const FLDBuildProjectionInput& Build)
{
    FLDBlueprintStructure Out;
    for (const FLDStatPointsValue& Stat : Build.StatPoints)
    {
        if (Stat.Points > 0) { Out.Stats.Add({Stat.StatId, Stat.Points}); }
    }
    Out.Stats.Sort([](const FLDBlueprintStat& A, const FLDBlueprintStat& B) { return LDBuildBlueprintDetail::Before(A.Stat, B.Stat); });
    for (const FLDLearnedActiveValue& Active : Build.Actives)
    {
        const TCHAR* Slot = LDBuildBlueprintDetail::SlotOf(Active.Entry);
        if (Slot) { Out.Actives.Add({Slot, Active.AbilityId.ToString()}); }
    }
    Out.Actives.Sort([](const FLDBlueprintActive& A, const FLDBlueprintActive& B)
    { return LDBuildBlueprintDetail::SlotOrder(A.Slot) < LDBuildBlueprintDetail::SlotOrder(B.Slot); });
    // #198 §16.4: the installed components and their install parameters are structure: changing only an install, its
    // target or its binding is a new version, so its contribution is never merged into the old one.
    Out.bComponentsCollected = true;
    Out.FinalMod=Build.FinalMod;
    for (const FLDInstalledComponentValue& Installed : Build.Components)
    {
        FLDBlueprintComponent& Row = Out.Components.AddDefaulted_GetRef();
        Row.Component = Installed.ComponentId.ToString();
        const bool bNamedTarget = Installed.TargetKind == ELDComponentTargetKind::Active
            || Installed.TargetKind == ELDComponentTargetKind::Component;
        Row.Host = bNamedTarget ? Installed.TargetId.ToString()
            : Installed.BoundActive.IsNone() ? FString() : Installed.BoundActive.ToString();
        if (!Installed.BoundComponent.IsNone())
        {
            Row.Parameters.Add({TEXT("bound_component"), Installed.BoundComponent.ToString()});
        }
        if (const TCHAR* Element = LDBuildBlueprintDetail::ElementKey(Installed.Element))
        {
            Row.Parameters.Add({TEXT("element"), Element});
        }
        if (const TCHAR* Kind = LDBuildBlueprintDetail::TargetKindKey(Installed.TargetKind))
        {
            Row.Parameters.Add({TEXT("target_kind"), Kind});
        }
    }
    Out.Components.Sort([](const FLDBlueprintComponent& A, const FLDBlueprintComponent& B)
    { return LDBuildBlueprintDetail::Before(A.Component, B.Component); });
    return Out;
}

int32 FLDBuildLedger::Observe(const FGuid& Contender, const FLDBuildProjectionInput& Build, int64 AtMs)
{
    TArray<FLDBlueprintVersion>& List = Contenders.FindOrAdd(Contender);
    // The structure changes only with a new build revision; the same revision is the current version.
    if (!List.IsEmpty() && List.Last().LastRevision == Build.ResultBuildRevision)
    {
        FLDBlueprintVersion& Current = List.Last();
        Current.LevelTo = Build.Level;
        Current.bDevLayer |= !Build.DevBonuses.IsEmpty();
        return List.Num() - 1;
    }
    FLDBlueprintStructure Structure = StructureOf(Build);
    if (!List.IsEmpty() && List.Last().Structure == Structure)
    {
        FLDBlueprintVersion& Current = List.Last();
        Current.LastRevision = FMath::Max(Current.LastRevision, Build.ResultBuildRevision);
        Current.LevelTo = Build.Level;
        Current.bDevLayer |= !Build.DevBonuses.IsEmpty();
        return List.Num() - 1;
    }
    const int64 From = List.IsEmpty() ? 0 : FMath::Max(AtMs, List.Last().FromMs);
    if (!List.IsEmpty()) { List.Last().ToMs = From; }
    FLDBlueprintVersion& Added = List.AddDefaulted_GetRef();
    Added.FirstRevision = Added.LastRevision = Build.ResultBuildRevision;
    Added.FromMs = Added.ToMs = From;
    Added.LevelFrom = Added.LevelTo = Build.Level;
    Added.bDevLayer = !Build.DevBonuses.IsEmpty();
    Added.Structure = MoveTemp(Structure);
    return List.Num() - 1;
}

void FLDBuildLedger::NoteEffect(const FGuid& EffectId, const FGuid& Contender, int32 Version)
{
    const TArray<FLDBlueprintVersion>* List = Contenders.Find(Contender);
    if (EffectId.IsValid() && List && List->IsValidIndex(Version)) { Effects.Add(EffectId, FEffect{Contender, Version}); }
}

int32 FLDBuildLedger::EffectVersion(const FGuid& EffectId, const FGuid& Contender) const
{
    const FEffect* Effect = Effects.Find(EffectId);
    return Effect && Effect->Contender == Contender ? Effect->Version : INDEX_NONE;
}

void FLDBuildLedger::Add(const FGuid& Contender, int32 Version, ELDBlueprintFact Fact, int64 Amount)
{
    TArray<FLDBlueprintVersion>* List = Contenders.Find(Contender);
    if (!ensureMsgf(List && List->IsValidIndex(Version) && int32(Fact) < int32(ELDBlueprintFact::Count)
        && LDBuildBlueprint::IsFactCollected(Fact) && Amount >= 0,
        TEXT("A blueprint fact needs an observed version, a collected fact and a nonnegative amount.")))
    {
        return;
    }
    int64& Value = (*List)[Version].Facts[int32(Fact)].Value;
    Value = Amount > MAX_int64 - Value ? MAX_int64 : Value + Amount;
}

const TArray<FLDBlueprintVersion>* FLDBuildLedger::Find(const FGuid& Contender) const
{
    return Contenders.Find(Contender);
}

bool FLDBuildLedger::Close(const FGuid& Contender, int64 EndMs, TArray<FLDBlueprintVersion>& Out) const
{
    const TArray<FLDBlueprintVersion>* List = Contenders.Find(Contender);
    if (!List || List->IsEmpty() || EndMs < List->Last().FromMs)
    {
        return false;
    }
    Out = *List;
    Out.Last().ToMs = EndMs;
    return true;
}

// ---------------------------------------------------------------- record

bool LDBuildBlueprint::Compose(const FHeader& Header, const TArray<FLDBlueprintVersion>& Versions, FLDBlueprintRecord& Out,
    FString& Error)
{
    if (Versions.IsEmpty())
    {
        Error = TEXT("No build version was recorded for this contender.");
        return false;
    }
    for (int32 Index = 0; Index < Versions.Num(); ++Index)
    {
        const FLDBlueprintVersion& Version = Versions[Index];
        const bool bLinked = Index + 1 < Versions.Num() ? Version.ToMs == Versions[Index + 1].FromMs
            && Version.LastRevision <= Versions[Index + 1].FirstRevision : Version.ToMs == Header.EndedAtMs;
        if (Version.FromMs < 0 || Version.ToMs < Version.FromMs || Version.LastRevision < Version.FirstRevision || !bLinked)
        {
            Error = FString::Printf(TEXT("Build version %d is out of order or not closed at the match end."), Index);
            return false;
        }
    }
    if (!Header.MatchId.IsValid() || Header.EndedAtMs < Header.StartedAtMs)
    {
        Error = TEXT("The result identity or times are invalid.");
        return false;
    }
    Out = FLDBlueprintRecord();
    Out.Origin = OriginAuto;
    Out.bComplete = true;
    Out.ProductVersion = Header.ProductVersion;
    Out.WireProtocol = Header.WireProtocol;
    Out.CatalogRoot = Header.CatalogRoot;
    Out.CatalogSchemaVersion = Header.CatalogSchemaVersion;
    Out.HandlerRevision = Header.HandlerRevision;
    Out.ProjectionRevision = Header.ProjectionRevision;
    Out.MatchId = Header.MatchId.ToString(EGuidFormats::Digits);
    Out.Mode = ModeDemo;
    Out.EndReason = Header.EndReason;
    Out.Outcome = Header.Outcome;
    Out.RecordedAtUtc = Header.RecordedAtUtc;
    Out.StartedAtMs = Header.StartedAtMs;
    Out.EndedAtMs = Header.EndedAtMs;
    Out.DurationMs = Header.EndedAtMs - Header.StartedAtMs;
    Out.FinalLevel = Header.FinalLevel;
    Out.Kills = Header.Kills;
    Out.Deaths = Header.Deaths;
    Out.Assists = Header.Assists;
    Out.FinalRevision = Versions.Last().LastRevision;
    Out.FinalBuild = Versions.Last().Structure;
    Out.Versions = Versions;
    return true;
}

bool LDBuildBlueprint::Serialize(const FLDBlueprintRecord& Record, TArray<uint8>& Out, FString& Error)
{
    using namespace LDBuildBlueprintDetail;
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetField(TEXT("schema"), JsonStr(Schema));
    Root->SetField(TEXT("schema_version"), JsonInt(SchemaVersion));
    Root->SetField(TEXT("origin"), JsonStr(Record.Origin));
    Root->SetField(TEXT("complete"), JsonBool(Record.bComplete));
    Root->SetField(TEXT("product_version"), JsonStr(Record.ProductVersion));
    Root->SetField(TEXT("recorded_at_utc"), JsonStr(Record.RecordedAtUtc.ToIso8601()));

    const TSharedRef<FJsonObject> Rules = MakeShared<FJsonObject>();
    Rules->SetField(TEXT("wire_protocol"), JsonInt(Record.WireProtocol));
    Rules->SetField(TEXT("catalog_root"), JsonStr(Record.CatalogRoot));
    Rules->SetField(TEXT("catalog_schema_version"), JsonInt(Record.CatalogSchemaVersion));
    Rules->SetField(TEXT("handler_revision"), JsonInt(Record.HandlerRevision));
    Rules->SetField(TEXT("projection_revision"), JsonInt(Record.ProjectionRevision));
    Root->SetField(TEXT("rules"), JsonObj(Rules));

    const TSharedRef<FJsonObject> Match = MakeShared<FJsonObject>();
    Match->SetField(TEXT("match_id"), JsonStr(Record.MatchId));
    Match->SetField(TEXT("mode"), JsonStr(Record.Mode));
    Match->SetField(TEXT("end_reason"), JsonStr(Record.EndReason));
    Match->SetField(TEXT("outcome"), JsonStr(Record.Outcome));
    Match->SetField(TEXT("started_at_ms"), JsonInt(Record.StartedAtMs));
    Match->SetField(TEXT("ended_at_ms"), JsonInt(Record.EndedAtMs));
    Match->SetField(TEXT("duration_ms"), JsonInt(Record.DurationMs));
    Root->SetField(TEXT("match"), JsonObj(Match));

    const TSharedRef<FJsonObject> Player = MakeShared<FJsonObject>();
    Player->SetField(TEXT("final_level"), JsonInt(Record.FinalLevel));
    Player->SetField(TEXT("kills"), JsonInt(Record.Kills));
    Player->SetField(TEXT("deaths"), JsonInt(Record.Deaths));
    Player->SetField(TEXT("assists"), JsonInt(Record.Assists));
    Root->SetField(TEXT("player"), JsonObj(Player));

    const TSharedRef<FJsonObject> Final = MakeShared<FJsonObject>();
    Final->SetField(TEXT("revision"), JsonInt(int64(Record.FinalRevision)));
    Root->SetField(TEXT("final_build"), JsonObj(WriteStructure(Record.FinalBuild, Final)));

    TArray<FValue> Versions;
    for (const FLDBlueprintVersion& Version : Record.Versions)
    {
        const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetField(TEXT("first_revision"), JsonInt(int64(Version.FirstRevision)));
        Row->SetField(TEXT("last_revision"), JsonInt(int64(Version.LastRevision)));
        Row->SetField(TEXT("from_ms"), JsonInt(Version.FromMs));
        Row->SetField(TEXT("to_ms"), JsonInt(Version.ToMs));
        Row->SetField(TEXT("level_from"), JsonInt(Version.LevelFrom));
        Row->SetField(TEXT("level_to"), JsonInt(Version.LevelTo));
        Row->SetField(TEXT("dev_layer"), JsonBool(Version.bDevLayer));
        WriteStructure(Version.Structure, Row);
        const TSharedRef<FJsonObject> Facts = MakeShared<FJsonObject>();
        for (int32 Index = 0; Index < int32(ELDBlueprintFact::Count); ++Index)
        {
            const FLDBlueprintFactValue& Value = Version.Facts[Index];
            const TSharedRef<FJsonObject> Fact = MakeShared<FJsonObject>();
            Fact->SetField(TEXT("collected"), JsonBool(Value.bCollected));
            Fact->SetField(TEXT("value"), Value.bCollected ? JsonInt(Value.Value) : JsonNull());
            Facts->SetField(FactKey(ELDBlueprintFact(Index)), JsonObj(Fact));
        }
        Row->SetField(TEXT("facts"), JsonObj(Facts));
        Versions.Add(JsonObj(Row));
    }
    Root->SetField(TEXT("versions"), JsonArr(Versions));

    LDDemoContent::FCheck Check(Error);
    if (!LDDemoContent::CanonicalBytes(JsonObj(Root), Out, Check))
    {
        Error.RemoveFromStart(TEXT("DemoContent: "));
        return false;
    }
    Error.Reset();
    return true;
}

ELDBlueprintParseError LDBuildBlueprint::Parse(const TArray<uint8>& Bytes, int64 MaxBytes, FLDBlueprintRecord& Out,
    FString& Detail)
{
    using namespace LDBuildBlueprintDetail;
    using EError = ELDBlueprintParseError;
    Out = FLDBlueprintRecord();
    Detail.Reset();
    if (Bytes.Num() > MaxBytes || Bytes.Num() > LDDemoContent::MaxFileBytes)
    {
        return Refuse(EError::TooLarge, FString::Printf(TEXT("%d bytes"), Bytes.Num()), Detail);
    }
    if (Bytes.IsEmpty())
    {
        return Refuse(EError::NotJson, TEXT("empty file"), Detail);
    }
    // Strict UTF-8: no NUL, and the bytes are exactly the encoding of their own decoding (no invalid or overlong form).
    {
        const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
        const FString Text(Decoded.Length(), Decoded.Get());
        const FTCHARToUTF8 Encoded(*Text, Text.Len());
        if (Bytes.Contains(0) || Encoded.Length() != Bytes.Num()
            || FMemory::Memcmp(Encoded.Get(), Bytes.GetData(), Bytes.Num()) != 0)
        {
            return Refuse(EError::NotUtf8, TEXT("input is not strict UTF-8"), Detail);
        }
    }
    FString Error;
    FCheck Check(Error);
    FValue Root;
    if (!LDDemoContent::Parse(Bytes, Root, Check, TEXT("blueprint"), false) || !Root || Root->Type != EJson::Object)
    {
        return Refuse(EError::NotJson, Error.IsEmpty() ? FString(TEXT("the root is not a JSON object")) : Error, Detail);
    }
    {
        const TSharedPtr<FJsonObject> Object = Root->AsObject();
        const FValue SchemaValue = Object->TryGetField(FStringView(TEXT("schema")));
        if (!SchemaValue || SchemaValue->Type != EJson::String || !SchemaValue->AsString().Equals(Schema, ESearchCase::CaseSensitive))
        {
            return Refuse(EError::WrongSchema, TEXT("schema is not lockdown.build-blueprint"), Detail);
        }
        FString VersionError;
        FCheck VersionCheck(VersionError);
        int64 Version = 0;
        if (!LDDemoContent::Integer(Object->TryGetField(FStringView(TEXT("schema_version"))), Version, VersionCheck,
            TEXT("blueprint.schema_version"), MIN_int64, MAX_int64))
        {
            return Refuse(EError::BadField, VersionError, Detail);
        }
        if (Version != SchemaVersion)
        {
            return Refuse(EError::UnknownVersion, FString::Printf(TEXT("schema_version %lld"), Version), Detail);
        }
    }
    const auto Fail = [&Error, &Detail]()
    {
        return Refuse(Error.Contains(MissingFieldText) ? ELDBlueprintParseError::MissingField : ELDBlueprintParseError::BadField,
            Error, Detail);
    };
    FLDBlueprintRecord Record;
    FFields Fields(Check, Root, TEXT("blueprint"));
    int64 Number = 0;
    FString RecordedAt;
    if (!Fields.Literal(TEXT("schema"), Schema) || !Fields.Int(TEXT("schema_version"), Number, SchemaVersion, SchemaVersion)
        || !Fields.Text(TEXT("origin"), Record.Origin, 1, 16)
        || !Check.Require(OneOf(Record.Origin, {OriginAuto, OriginExport}), TEXT("blueprint.origin"), TEXT("origin must be auto or export"))
        || !Fields.Flag(TEXT("complete"), Record.bComplete)
        || !Fields.Text(TEXT("product_version"), Record.ProductVersion, 1, 32)
        || !Fields.Text(TEXT("recorded_at_utc"), RecordedAt, 1, 40)
        || !Check.Require(FDateTime::ParseIso8601(*RecordedAt, Record.RecordedAtUtc), TEXT("blueprint.recorded_at_utc"),
            TEXT("expected an ISO 8601 UTC time")))
    {
        return Fail();
    }
    {
        FFields Rules(Check, Fields.Take(TEXT("rules")), TEXT("blueprint.rules"));
        if (!Rules.SmallInt(TEXT("wire_protocol"), Record.WireProtocol, 1, MAX_int32)
            || !Rules.Text(TEXT("catalog_root"), Record.CatalogRoot, 64, 64)
            || !Check.Require(IsHex(Record.CatalogRoot, 64), Rules.Path + TEXT(".catalog_root"), TEXT("expected 64 hex digits"))
            || !Rules.SmallInt(TEXT("catalog_schema_version"), Record.CatalogSchemaVersion, 0, MAX_int32)
            || !Rules.SmallInt(TEXT("handler_revision"), Record.HandlerRevision, 0, MAX_int32)
            || !Rules.SmallInt(TEXT("projection_revision"), Record.ProjectionRevision, 0, MAX_int32) || !Rules.Done())
        {
            return Fail();
        }
    }
    {
        FFields Match(Check, Fields.Take(TEXT("match")), TEXT("blueprint.match"));
        if (!Match.Text(TEXT("match_id"), Record.MatchId, 32, 32)
            || !Check.Require(IsHex(Record.MatchId, 32), Match.Path + TEXT(".match_id"), TEXT("expected 32 hex digits"))
            || !Match.Text(TEXT("mode"), Record.Mode, 1, 16)
            || !Check.Require(Record.Mode == ModeDemo, Match.Path + TEXT(".mode"), TEXT("unknown mode"))
            || !Match.Text(TEXT("end_reason"), Record.EndReason, 1, 32)
            || !Check.Require(OneOf(Record.EndReason, {TEXT("guard_succeeded"), TEXT("host_ended")}), Match.Path + TEXT(".end_reason"),
                TEXT("unknown end reason"))
            || !Match.Text(TEXT("outcome"), Record.Outcome, 1, 16)
            || !Check.Require(OneOf(Record.Outcome, {TEXT("won"), TEXT("lost"), TEXT("no_winner")}), Match.Path + TEXT(".outcome"),
                TEXT("unknown outcome"))
            || !Match.Int(TEXT("started_at_ms"), Record.StartedAtMs, 0)
            || !Match.Int(TEXT("ended_at_ms"), Record.EndedAtMs, Record.StartedAtMs)
            || !Match.Int(TEXT("duration_ms"), Record.DurationMs, 0)
            || !Check.Require(Record.DurationMs == Record.EndedAtMs - Record.StartedAtMs, Match.Path + TEXT(".duration_ms"),
                TEXT("duration differs from the match times"))
            || !Match.Done())
        {
            return Fail();
        }
    }
    {
        FFields Player(Check, Fields.Take(TEXT("player")), TEXT("blueprint.player"));
        if (!Player.Int(TEXT("final_level"), Number, 1, MAX_uint8) || !Player.Int(TEXT("kills"), Record.Kills, 0)
            || !Player.Int(TEXT("deaths"), Record.Deaths, 0) || !Player.Int(TEXT("assists"), Record.Assists, 0) || !Player.Done())
        {
            return Fail();
        }
        Record.FinalLevel = uint8(Number);
    }
    {
        FFields Final(Check, Fields.Take(TEXT("final_build")), TEXT("blueprint.final_build"));
        if (!Final.Int(TEXT("revision"), Number, 0) || !ReadStructure(Final, Record.FinalBuild) || !Final.Done())
        {
            return Fail();
        }
        Record.FinalRevision = uint64(Number);
    }
    const TArray<FValue>* Versions = nullptr;
    if (!LDDemoContent::Array(Fields.Take(TEXT("versions")), Versions, Check, TEXT("blueprint.versions"), 1, 4096))
    {
        return Fail();
    }
    for (int32 Index = 0; Index < Versions->Num(); ++Index)
    {
        FFields Row(Check, (*Versions)[Index], FString::Printf(TEXT("blueprint.versions[%d]"), Index));
        FLDBlueprintVersion Version;
        int64 First = 0, Last = 0, LevelFrom = 0, LevelTo = 0;
        if (!Row.Int(TEXT("first_revision"), First, 0) || !Row.Int(TEXT("last_revision"), Last, First)
            || !Row.Int(TEXT("from_ms"), Version.FromMs, 0) || !Row.Int(TEXT("to_ms"), Version.ToMs, Version.FromMs)
            || !Row.Int(TEXT("level_from"), LevelFrom, 1, MAX_uint8) || !Row.Int(TEXT("level_to"), LevelTo, 1, MAX_uint8)
            || !Row.Flag(TEXT("dev_layer"), Version.bDevLayer) || !ReadStructure(Row, Version.Structure)
            || !ReadFacts(Row, Version) || !Row.Done())
        {
            return Fail();
        }
        Version.FirstRevision = uint64(First);
        Version.LastRevision = uint64(Last);
        Version.LevelFrom = uint8(LevelFrom);
        Version.LevelTo = uint8(LevelTo);
        if (!Record.Versions.IsEmpty())
        {
            const FLDBlueprintVersion& Previous = Record.Versions.Last();
            if (!Check.Require(Previous.ToMs == Version.FromMs && Previous.LastRevision <= Version.FirstRevision, Row.Path,
                TEXT("versions must follow each other in time and revision")))
            {
                return Fail();
            }
        }
        Record.Versions.Add(MoveTemp(Version));
    }
    const FLDBlueprintVersion& LastVersion = Record.Versions.Last();
    if (!Fields.Done()
        || !Check.Require(LastVersion.ToMs == Record.EndedAtMs, TEXT("blueprint.versions"), TEXT("the last version must end with the match"))
        || !Check.Require(LastVersion.Structure == Record.FinalBuild && LastVersion.LastRevision == Record.FinalRevision,
            TEXT("blueprint.final_build"), TEXT("the final build must be the last version")))
    {
        return Fail();
    }
    Out = MoveTemp(Record);
    return ELDBlueprintParseError::None;
}

FString LDBuildBlueprint::DescribeError(ELDBlueprintParseError Error, const FString& Detail)
{
    FString Reason;
    switch (Error)
    {
    case ELDBlueprintParseError::None: return FString();
    case ELDBlueprintParseError::TooLarge: Reason = TEXT("文件超过 2 MiB 上限"); break;
    case ELDBlueprintParseError::NotUtf8: Reason = TEXT("文件不是有效的 UTF-8 文本"); break;
    case ELDBlueprintParseError::NotJson: Reason = TEXT("文件不是有效的 JSON"); break;
    case ELDBlueprintParseError::WrongSchema: Reason = TEXT("不是构筑蓝图文件"); break;
    case ELDBlueprintParseError::UnknownVersion: Reason = TEXT("未知的蓝图版本，此版本无法读取"); break;
    case ELDBlueprintParseError::MissingField: Reason = TEXT("缺少必需字段"); break;
    case ELDBlueprintParseError::BadField: Reason = TEXT("字段内容不合法"); break;
    }
    return Detail.IsEmpty() ? Reason : FString::Printf(TEXT("%s（%s）"), *Reason, *Detail);
}

void LDBuildBlueprint::Notices(const FLDBlueprintRecord& Record, const FLDDemoContentDefinitions* Local,
    const FString& LocalProduct, TArray<FString>& Out)
{
    if (!Record.bComplete)
    {
        Out.Add(TEXT("记录者把这份蓝图标为不完整；缺的部分不代表 0。"));
    }
    if (!LocalProduct.IsEmpty() && Record.ProductVersion != LocalProduct)
    {
        Out.Add(FString::Printf(TEXT("这份蓝图来自产品版本 %s，本机是 %s。"), *Record.ProductVersion, *LocalProduct));
    }
    if (!Local)
    {
        Out.Add(TEXT("本机内容目录不可用，无法核对技能与属性。"));
        return;
    }
    if (!Record.CatalogRoot.Equals(Local->Identity.Root.ToHex(), ESearchCase::IgnoreCase))
    {
        Out.Add(TEXT("目录不一致：这份蓝图来自另一版内容目录，名称按本机目录显示，数值可能已经不同。"));
    }
    TSet<FString> Missing;
    const auto CheckStructure = [Local, &Missing](const FLDBlueprintStructure& Structure)
    {
        if(!Structure.FinalMod.IsEmpty()&&!Local->FinalMods.ContainsByPredicate([&Structure](const FLDDemoFinalModDefinition& Row)
            {return FName(*Row.Id)==Structure.FinalMod.DefinitionId;}))
        {Missing.Add(Structure.FinalMod.DefinitionId.ToString());}
        for (const FLDBlueprintStat& Stat : Structure.Stats)
        {
            if (!Local->Stats.ContainsByPredicate([&Stat](const FLDDemoStatDefinition& Row) { return Row.Id == Stat.Stat; }))
            { Missing.Add(Stat.Stat); }
        }
        for (const FLDBlueprintActive& Active : Structure.Actives)
        {
            if (!Local->Abilities.ContainsByPredicate([&Active](const FLDDemoAbilityDefinition& Row) { return Row.Id == Active.Ability; }))
            { Missing.Add(Active.Ability); }
        }
        const auto IsComponent = [Local](const FString& Id)
        { return Local->Components.ContainsByPredicate([&Id](const FLDDemoComponentDefinition& Row) { return Row.Id == Id; }); };
        const auto IsActive = [Local](const FString& Id)
        { return Local->Abilities.ContainsByPredicate([&Id](const FLDDemoAbilityDefinition& Row) { return Row.Id == Id; }); };
        for (const FLDBlueprintComponent& Component : Structure.Components)
        {
            if (!IsComponent(Component.Component)) { Missing.Add(Component.Component); }
            if (!Component.Host.IsEmpty() && !IsComponent(Component.Host) && !IsActive(Component.Host)) { Missing.Add(Component.Host); }
            for (const FLDBlueprintParameter& Parameter : Component.Parameters)
            {
                if (Parameter.Key == TEXT("bound_component") && !IsComponent(Parameter.Value)) { Missing.Add(Parameter.Value); }
            }
        }
    };
    CheckStructure(Record.FinalBuild);
    for (const FLDBlueprintVersion& Version : Record.Versions) { CheckStructure(Version.Structure); }
    TArray<FString> Sorted = Missing.Array();
    Sorted.Sort();
    for (const FString& Id : Sorted)
    {
        Out.Add(FString::Printf(TEXT("失效安装：本机目录没有 %s，只显示原始编号。"), *Id));
    }
}
