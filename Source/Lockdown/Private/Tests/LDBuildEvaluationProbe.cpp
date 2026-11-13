#include "Tests/LDBuildEvaluationProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AI/LDOpponentController.h"
#include "Algo/Find.h"
#include "Containers/Ticker.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDDevGrowthFixture.h"
#include "Development/LDGrowthDirector.h"
#include "Development/LDProgressionRules.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Evaluation/LDEvaluationSeeds.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool LDBuildEvaluation::SelectDuelOutcome(const TArray<FLDKillNotice>& Notices, uint64& LastSerial, const FGuid& Subject,
    const FGuid& Opponent, int64 StartedAtMs, int64 DuelTimeoutMs, FLDEvalOutcome& Out)
{
    Out = {};
    const uint64 PreviousSerial = LastSerial;
    const FLDKillNotice* Deaths[DuelAISlots] = {nullptr, nullptr};
    int64 Earliest = MAX_int64;
    for (const FLDKillNotice& Notice : Notices)
    {
        if (Notice.Serial <= PreviousSerial) { continue; }
        LastSerial = FMath::Max(LastSerial, Notice.Serial);
        if (!KillAtOrBeforeHorizon(StartedAtMs, DuelTimeoutMs, Notice.OccurredAtMs)) { continue; }
        const int32 Side = Notice.Victim == Subject ? 0 : Notice.Victim == Opponent ? 1 : INDEX_NONE;
        if (Side == INDEX_NONE || Notice.OccurredAtMs > Earliest) { continue; }
        if (Notice.OccurredAtMs < Earliest)
        {
            Deaths[0] = Deaths[1] = nullptr;
            Earliest = Notice.OccurredAtMs;
        }
        Deaths[Side] = &Notice;
    }
    if (!Deaths[0] && !Deaths[1]) { return false; }
    Out.OccurredAtMs = Earliest;
    Out.bTrade = Deaths[0] && Deaths[1];
    if (!Out.bTrade)
    {
        Out.VictimSide = Deaths[0] ? 0 : 1;
        Out.Killer = Deaths[Out.VictimSide]->Killer;
    }
    return true;
}

namespace LDBuildEvaluationDetail
{
bool OnlyKeys(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed, const FString& Where, FString& Error)
{
    for (const auto& Pair : Object.Values)
    {
        bool bKnown = false;
        for (const TCHAR* Key : Allowed) { bKnown |= Pair.Key.ToView().Equals(Key, ESearchCase::CaseSensitive); }
        if (!bKnown)
        {
            Error = FString::Printf(TEXT("%s: unknown field '%s'"), *Where, *Pair.Key);
            return false;
        }
    }
    return true;
}

bool ReadString(const FJsonObject& Object, const TCHAR* Key, const FString& Where, FString& Out, FString& Error)
{
    const TSharedPtr<FJsonValue> Value = Object.Values.FindRef(Key);
    if (!Value.IsValid() || Value->Type != EJson::String || Value->AsString().IsEmpty())
    {
        Error = FString::Printf(TEXT("%s.%s: a non-empty string is required"), *Where, Key);
        return false;
    }
    Out = Value->AsString();
    return true;
}

bool ReadInteger(const FJsonObject& Object, const TCHAR* Key, const FString& Where, int64 Min, int64 Max, int64& Out, FString& Error)
{
    const TSharedPtr<FJsonValue> Value = Object.Values.FindRef(Key);
    const double Number = Value.IsValid() && Value->Type == EJson::Number ? Value->AsNumber() : -1.;
    if (!Value.IsValid() || Value->Type != EJson::Number || !FMath::IsFinite(Number) || FMath::Frac(Number) != 0.
        || Number < double(Min) || Number > double(Max))
    {
        Error = FString::Printf(TEXT("%s.%s: an integer in [%lld, %lld] is required"), *Where, Key, Min, Max);
        return false;
    }
    Out = int64(Number);
    return true;
}

bool ReadArray(const FJsonObject& Object, const TCHAR* Key, const FString& Where, const TArray<TSharedPtr<FJsonValue>>*& Out,
    FString& Error)
{
    const TSharedPtr<FJsonValue> Value = Object.Values.FindRef(Key);
    if (!Value.IsValid() || Value->Type != EJson::Array)
    {
        Error = FString::Printf(TEXT("%s.%s: an array is required"), *Where, Key);
        return false;
    }
    Out = &Value->AsArray();
    return true;
}

bool ReadObject(const FJsonObject& Object, const TCHAR* Key, const FString& Where, TSharedPtr<FJsonObject>& Out, FString& Error)
{
    const TSharedPtr<FJsonValue> Value = Object.Values.FindRef(Key);
    if (!Value.IsValid() || Value->Type != EJson::Object || !Value->AsObject().IsValid())
    {
        Error = FString::Printf(TEXT("%s.%s: an object is required"), *Where, Key);
        return false;
    }
    Out = Value->AsObject();
    return true;
}

bool IsIdentifier(const FString& Text, int32 MaxLength)
{
    if (Text.IsEmpty() || Text.Len() > MaxLength) { return false; }
    for (const TCHAR C : Text)
    {
        if (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('-') && C != TEXT('.')) { return false; }
    }
    return true;
}

bool ParseSeed(const FString& Text, uint64& Out)
{
    if (Text.IsEmpty() || Text.Len() > 20) { return false; }
    for (const TCHAR C : Text) { if (!FChar::IsDigit(C)) { return false; } }
    Out = FCString::Strtoui64(*Text, nullptr, 10);
    return FString::Printf(TEXT("%llu"), Out) == Text;
}

bool ParseChoice(const FJsonObject& Object, const FString& Where, FLDEvaluationChoice& Out, FString& Error)
{
    FString Rule;
    if (!OnlyKeys(Object, {TEXT("rule"), TEXT("index"), TEXT("definition")}, Where, Error)
        || !ReadString(Object, TEXT("rule"), Where, Rule, Error)) { return false; }
    Out = {};
    if (Rule == TEXT("ai_select"))
    {
        Out.Rule = ELDEvaluationChoiceRule::AISelect;
        if (Object.Values.Contains(TEXT("index")) || Object.Values.Contains(TEXT("definition")))
        {
            Error = Where + TEXT(": ai_select takes no index or definition");
            return false;
        }
        return true;
    }
    if (Rule == TEXT("option_index"))
    {
        int64 Index = 0;
        if (!ReadInteger(Object, TEXT("index"), Where, 0, 2, Index, Error)) { return false; }
        if (Object.Values.Contains(TEXT("definition"))) { Error = Where + TEXT(": option_index takes no definition"); return false; }
        Out.Rule = ELDEvaluationChoiceRule::OptionIndex;
        Out.Index = int32(Index);
    }
    else if (Rule == TEXT("definition"))
    {
        if (!ReadString(Object, TEXT("definition"), Where, Out.DefinitionId, Error)) { return false; }
        if (Object.Values.Contains(TEXT("index"))) { Error = Where + TEXT(": definition takes no index"); return false; }
        if (!IsIdentifier(Out.DefinitionId, 96)) { Error = Where + TEXT(".definition: not a definition id"); return false; }
        Out.Rule = ELDEvaluationChoiceRule::Definition;
    }
    else
    {
        Error = FString::Printf(TEXT("%s.rule: unknown rule '%s'"), *Where, *Rule);
        return false;
    }
    return true;
}

// The approved definition id subset of the build wire ([a-z][a-z0-9_]*, at most 63 bytes).
bool IsDefinitionId(const FString& Text)
{
    if (Text.IsEmpty() || Text.Len() > 63 || !(Text[0] >= TEXT('a') && Text[0] <= TEXT('z'))) { return false; }
    for (const TCHAR C : Text)
    {
        if (!((C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('_'))) { return false; }
    }
    return true;
}

bool ReadDefinition(const FJsonObject& Object, const TCHAR* Key, const FString& Where, FString& Out, FString& Error)
{
    if (!ReadString(Object, Key, Where, Out, Error)) { return false; }
    if (!IsDefinitionId(Out))
    {
        Error = FString::Printf(TEXT("%s.%s: not a definition id"), *Where, Key);
        return false;
    }
    return true;
}

// A planned build step (BuildEvaluationLockdown.py writes it): step, at and only the fields its command takes. Growth
// judges ownership, targets and bindings at commit; this refuses shapes no command could carry and the rest-only commands
// (spec §5) on the field.
bool ParseStep(const FJsonObject& Object, const FString& Where, FLDEvaluationBuildStep& Out, FString& Error)
{
    using EKind = ELDEvaluationStepKind;
    Out = {};
    FString Step, At;
    if (!ReadString(Object, TEXT("step"), Where, Step, Error) || !ReadString(Object, TEXT("at"), Where, At, Error)) { return false; }
    if (At == TEXT("opening_rest")) { Out.Window = ELDEvaluationStepWindow::OpeningRest; }
    else if (At == TEXT("field")) { Out.Window = ELDEvaluationStepWindow::Field; }
    else
    {
        Error = FString::Printf(TEXT("%s.at: opening_rest or field, not '%s'"), *Where, *At);
        return false;
    }
    bool bShape = false;
    if (Step == TEXT("opening_base"))
    {
        Out.Kind = EKind::OpeningBase;
        bShape = OnlyKeys(Object, {TEXT("step"), TEXT("at"), TEXT("component"), TEXT("bind")}, Where, Error);
    }
    else if (Step == TEXT("install"))
    {
        Out.Kind = EKind::InstallComponent;
        bShape = OnlyKeys(Object, {TEXT("step"), TEXT("at"), TEXT("component"), TEXT("bind"), TEXT("target"), TEXT("base")}, Where, Error);
    }
    else if (Step == TEXT("uninstall"))
    {
        Out.Kind = EKind::UninstallComponent;
        bShape = OnlyKeys(Object, {TEXT("step"), TEXT("at"), TEXT("component")}, Where, Error);
    }
    else if (Step == TEXT("allocate_points"))
    {
        Out.Kind = EKind::AllocatePoints;
        bShape = OnlyKeys(Object, {TEXT("step"), TEXT("at"), TEXT("points")}, Where, Error);
    }
    else if (Step == TEXT("learn") || Step == TEXT("replace"))
    {
        Out.Kind = Step == TEXT("learn") ? EKind::LearnBlueprint : EKind::ReplaceActive;
        bShape = OnlyKeys(Object, {TEXT("step"), TEXT("at"), TEXT("blueprint"), TEXT("slot")}, Where, Error);
    }
    else if (Step == TEXT("respec"))
    {
        Out.Kind = EKind::Respec;
        bShape = OnlyKeys(Object, {TEXT("step"), TEXT("at")}, Where, Error);
    }
    else
    {
        Error = FString::Printf(TEXT("%s.step: unknown step '%s'"), *Where, *Step);
        return false;
    }
    if (!bShape) { return false; }
    if ((Out.Kind == EKind::UninstallComponent || Out.Kind == EKind::ReplaceActive || Out.Kind == EKind::Respec)
        && Out.Window != ELDEvaluationStepWindow::OpeningRest)
    {
        Error = FString::Printf(TEXT("%s.at: %s is a rest-window command (spec §5); the field never admits it"), *Where, *Step);
        return false;
    }
    if (Out.Kind == EKind::OpeningBase || Out.Kind == EKind::InstallComponent || Out.Kind == EKind::UninstallComponent)
    {
        if (!ReadDefinition(Object, TEXT("component"), Where, Out.Component, Error)) { return false; }
    }
    if (Object.Values.Contains(TEXT("bind")))
    {
        TSharedPtr<FJsonObject> Bind;
        FString Element;
        const FString At2 = Where + TEXT(".bind");
        if (!ReadObject(Object, TEXT("bind"), Where, Bind, Error) || !OnlyKeys(*Bind, {TEXT("active"), TEXT("element")}, At2, Error)
            || !ReadDefinition(*Bind, TEXT("active"), At2, Out.BoundActive, Error)
            || !ReadString(*Bind, TEXT("element"), At2, Element, Error))
        {
            return false;
        }
        Out.Element = Element == TEXT("fire") ? ELDDemoElement::Fire : Element == TEXT("water") ? ELDDemoElement::Water
            : Element == TEXT("electric") ? ELDDemoElement::Electric : ELDDemoElement::None;
        if (Out.Element == ELDDemoElement::None)
        {
            Error = FString::Printf(TEXT("%s.element: fire, water or electric, not '%s'"), *At2, *Element);
            return false;
        }
    }
    if (Object.Values.Contains(TEXT("target")))
    {
        TSharedPtr<FJsonObject> Target;
        FString Kind;
        const FString At2 = Where + TEXT(".target");
        if (!ReadObject(Object, TEXT("target"), Where, Target, Error) || !OnlyKeys(*Target, {TEXT("kind"), TEXT("id")}, At2, Error)
            || !ReadString(*Target, TEXT("kind"), At2, Kind, Error))
        {
            return false;
        }
        const bool bNamed = Kind == TEXT("active") || Kind == TEXT("component");
        if (Kind == TEXT("active")) { Out.TargetKind = ELDComponentTargetKind::Active; }
        else if (Kind == TEXT("component")) { Out.TargetKind = ELDComponentTargetKind::Component; }
        else if (Kind == TEXT("dash")) { Out.TargetKind = ELDComponentTargetKind::Dash; }
        else if (Kind != TEXT("self"))
        {
            Error = FString::Printf(TEXT("%s.kind: active, component, dash or self, not '%s'"), *At2, *Kind);
            return false;
        }
        if (bNamed && !ReadDefinition(*Target, TEXT("id"), At2, Out.TargetId, Error)) { return false; }
        if (!bNamed && Target->Values.Contains(TEXT("id")))
        {
            Error = FString::Printf(TEXT("%s.id: a %s target names no definition"), *At2, *Kind);
            return false;
        }
    }
    if (Object.Values.Contains(TEXT("base")) && !ReadDefinition(Object, TEXT("base"), Where, Out.BoundComponent, Error)) { return false; }
    if (Out.Kind == EKind::LearnBlueprint || Out.Kind == EKind::ReplaceActive)
    {
        FString Slot;
        if (!ReadDefinition(Object, TEXT("blueprint"), Where, Out.Blueprint, Error) || !ReadString(Object, TEXT("slot"), Where, Slot, Error))
        {
            return false;
        }
        Out.Slot = Slot == TEXT("Q") ? 0 : Slot == TEXT("W") ? 1 : Slot == TEXT("E") ? 2 : 3;
        if (Out.Slot > 2)
        {
            Error = FString::Printf(TEXT("%s.slot: Q, W or E, not '%s'"), *Where, *Slot);
            return false;
        }
    }
    if (Out.Kind == EKind::AllocatePoints)
    {
        TSharedPtr<FJsonObject> Points;
        if (!ReadObject(Object, TEXT("points"), Where, Points, Error)) { return false; }
        if (Points->Values.IsEmpty())
        {
            Error = Where + TEXT(".points: at least one stat");
            return false;
        }
        for (const auto& Pair : Points->Values)
        {
            int64 Count = 0;
            const FString StatId(Pair.Key.ToView());
            if (!IsDefinitionId(StatId))
            {
                Error = FString::Printf(TEXT("%s.points: '%s' is not a stat id"), *Where, *StatId);
                return false;
            }
            if (!ReadInteger(*Points, *StatId, Where + TEXT(".points"), 1, MAX_uint8, Count, Error)) { return false; }
            Out.Points.Add({StatId, int32(Count)});
        }
        Out.Points.Sort([](const FLDOpponentStatPoints& A, const FLDOpponentStatPoints& B) { return A.StatId < B.StatId; });
    }
    return true;
}

bool ParseContender(const FJsonObject& Object, const FString& Where, FLDEvalContenderPlan& Out, FString& Error)
{
    using namespace LDBuildEvaluation;
    int64 Slot = 0, Supply = 0;
    const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* OpeningPreset = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* LevelComponents = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Presets = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Build = nullptr;
    if (!OnlyKeys(Object, {TEXT("role"), TEXT("slot"), TEXT("supply_xp"), TEXT("choices"), TEXT("opening_preset"),
            TEXT("presets"), TEXT("level_components"), TEXT("build"), TEXT("stance")}, Where, Error)
        || !ReadString(Object, TEXT("role"), Where, Out.Role, Error)
        || !ReadInteger(Object, TEXT("slot"), Where, 0, DuelAISlots - 1, Slot, Error)
        || !ReadInteger(Object, TEXT("supply_xp"), Where, 0, 1000000, Supply, Error)
        || !ReadArray(Object, TEXT("choices"), Where, Choices, Error)
        || !ReadArray(Object, TEXT("opening_preset"), Where, OpeningPreset, Error)
        || !ReadArray(Object, TEXT("level_components"), Where, LevelComponents, Error)
        || !ReadArray(Object, TEXT("presets"), Where, Presets, Error)
        || !ReadArray(Object, TEXT("build"), Where, Build, Error))
    {
        return false;
    }
    if (Out.Role != TEXT("subject") && Out.Role != TEXT("opponent"))
    {
        Error = Where + TEXT(".role: subject or opponent");
        return false;
    }
    Out.Slot = int32(Slot);
    Out.SupplyXP = Supply;
    if (Object.HasField(TEXT("stance")))
    {
        const FString At = Where + TEXT(".stance");
        TSharedPtr<FJsonObject> Stance;
        FString Kind;
        int64 Standoff = 0, Threshold = 0, Leash = 0, Reaction = 0, Aim = 0, Skip = 0;
        if (!ReadObject(Object, TEXT("stance"), Where, Stance, Error)
            || !OnlyKeys(*Stance, {TEXT("kind"), TEXT("standoff_cm"), TEXT("threshold_bp"), TEXT("leash_cm"), TEXT("reaction_ms"),
                TEXT("aim_error_cm"), TEXT("skip_bp")}, At, Error)
            || !ReadString(*Stance, TEXT("kind"), At, Kind, Error))
        {
            return false;
        }
        if ((Stance->HasField(TEXT("reaction_ms")) && !ReadInteger(*Stance, TEXT("reaction_ms"), At, 0, LDEvaluationPolicy::MaxReactionMs,
                Reaction, Error))
            || (Stance->HasField(TEXT("aim_error_cm")) && !ReadInteger(*Stance, TEXT("aim_error_cm"), At, 0,
                LDEvaluationPolicy::MaxAimErrorCm, Aim, Error))
            || (Stance->HasField(TEXT("skip_bp")) && !ReadInteger(*Stance, TEXT("skip_bp"), At, 0, LDEvaluationPolicy::MaxSkipBp,
                Skip, Error)))
        {
            return false;
        }
        Out.ReactionMs = int32(Reaction);
        Out.AimErrorCm = int32(Aim);
        Out.SkipBp = int32(Skip);
        const bool bOperation = Stance->HasField(TEXT("reaction_ms")) || Stance->HasField(TEXT("aim_error_cm"))
            || Stance->HasField(TEXT("skip_bp"));
        if (Kind == TEXT("pursue") && bOperation)
        {
            // An explicit pursue only carries operation knobs; a plain pursue is the absent stance.
            if (Stance->HasField(TEXT("standoff_cm")) || Stance->HasField(TEXT("threshold_bp")) || Stance->HasField(TEXT("leash_cm")))
            {
                Error = At + TEXT(": a pursue stance keeps no standoff_cm, threshold_bp or leash_cm");
                return false;
            }
            Out.Stance = ELDEvaluationStance::Pursue;
            Out.StandoffCm = Out.ThresholdBp = Out.LeashCm = 0;
        }
        else
        {
            static const TPair<const TCHAR*, ELDEvaluationStance> Kinds[] = {{TEXT("kite"), ELDEvaluationStance::Kite},
                {TEXT("burst"), ELDEvaluationStance::Burst}, {TEXT("hold"), ELDEvaluationStance::Hold}, {TEXT("guard"), ELDEvaluationStance::Guard}};
            const TPair<const TCHAR*, ELDEvaluationStance>* Known = Algo::FindByPredicate(Kinds,
                [&Kind](const TPair<const TCHAR*, ELDEvaluationStance>& Row) { return Kind == Row.Key; });
            if (!Known)
            {
                // The pursue stance is the absent field: one spelling per behaviour.
                Error = At + TEXT(".kind: kite, burst, hold or guard (omit the stance to pursue)");
                return false;
            }
            const bool bGuard = Known->Value == ELDEvaluationStance::Guard;
            if (!ReadInteger(*Stance, TEXT("standoff_cm"), At, LDEvaluationPolicy::MinStandoffCm, LDEvaluationPolicy::MaxStandoffCm,
                Standoff, Error)
                || (Stance->HasField(TEXT("leash_cm")) && !ReadInteger(*Stance, TEXT("leash_cm"), At, LDEvaluationPolicy::MinLeashCm,
                    LDEvaluationPolicy::MaxLeashCm, Leash, Error)))
            {
                return false;
            }
            if (bGuard != Stance->HasField(TEXT("threshold_bp")))
            {
                Error = At + TEXT(".threshold_bp: required by guard and only by guard");
                return false;
            }
            if (bGuard && !ReadInteger(*Stance, TEXT("threshold_bp"), At, LDEvaluationPolicy::MinThresholdBp,
                LDEvaluationPolicy::MaxThresholdBp, Threshold, Error))
            {
                return false;
            }
            Out.Stance = Known->Value;
            Out.StandoffCm = int32(Standoff);
            Out.ThresholdBp = int32(Threshold);
            Out.LeashCm = int32(Leash);
        }
    }
    if (LevelComponents->Num() > 23)
    {
        Error = Where + TEXT(".level_components: at most 23 distinct component ids");
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Component : *LevelComponents)
    {
        FString Id;
        if (!Component.IsValid() || !Component->TryGetString(Id) || !IsDefinitionId(Id)
            || Out.LevelComponents.Contains(FName(*Id)))
        {
            Error = Where + TEXT(".level_components: distinct component ids are required");
            return false;
        }
        Out.LevelComponents.Add(FName(*Id));
    }
    if (Choices->IsEmpty() || Choices->Num() > MaxChoices)
    {
        Error = FString::Printf(TEXT("%s.choices: 1..%d rules"), *Where, MaxChoices);
        return false;
    }
    for (int32 Index = 0; Index < Choices->Num(); ++Index)
    {
        const FString At = FString::Printf(TEXT("%s.choices[%d]"), *Where, Index);
        const TSharedPtr<FJsonObject>* Row = nullptr;
        if (!(*Choices)[Index].IsValid() || !(*Choices)[Index]->TryGetObject(Row) || !Row || !Row->IsValid())
        {
            Error = At + TEXT(": an object is required");
            return false;
        }
        if (!ParseChoice(**Row, At, Out.Choices.AddDefaulted_GetRef(), Error)) { return false; }
    }
    if (OpeningPreset->Num() > 3)
    {
        Error = Where + TEXT(".opening_preset: at most three active ids");
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Card : *OpeningPreset)
    {
        FString Id;
        if (!Card.IsValid() || !Card->TryGetString(Id) || !IsDefinitionId(Id) || Out.OpeningPreset.Contains(FName(*Id)))
        {
            Error = Where + TEXT(".opening_preset: distinct active ids are required");
            return false;
        }
        Out.OpeningPreset.Add(FName(*Id));
    }
    if (Presets->Num() >= MaxChoices)
    {
        Error = Where + TEXT(".presets: too many");
        return false;
    }
    for (int32 Index = 0; Index < Presets->Num(); ++Index)
    {
        const FString At = FString::Printf(TEXT("%s.presets[%d]"), *Where, Index);
        const TArray<TSharedPtr<FJsonValue>>* Cards = nullptr;
        if (!(*Presets)[Index].IsValid() || !(*Presets)[Index]->TryGetArray(Cards) || !Cards || Cards->IsEmpty() || Cards->Num() > 3)
        {
            Error = At + TEXT(": one to three active ids are required");
            return false;
        }
        TArray<FName>& Row = Out.Presets.AddDefaulted_GetRef();
        for (const TSharedPtr<FJsonValue>& Card : *Cards)
        {
            FString Id;
            if (!Card.IsValid() || !Card->TryGetString(Id) || !IsIdentifier(Id, 96) || Row.Contains(FName(*Id)))
            {
                Error = At + TEXT(": distinct active ids are required");
                return false;
            }
            Row.Add(FName(*Id));
        }
    }
    if (Build->Num() > MaxBuildSteps)
    {
        Error = FString::Printf(TEXT("%s.build: at most %d steps"), *Where, MaxBuildSteps);
        return false;
    }
    bool bField = false;
    for (int32 Index = 0; Index < Build->Num(); ++Index)
    {
        const FString At = FString::Printf(TEXT("%s.build[%d]"), *Where, Index);
        const TSharedPtr<FJsonObject>* Row = nullptr;
        if (!(*Build)[Index].IsValid() || !(*Build)[Index]->TryGetObject(Row) || !Row || !Row->IsValid())
        {
            Error = At + TEXT(": an object is required");
            return false;
        }
        FLDEvaluationBuildStep& Step = Out.Build.AddDefaulted_GetRef();
        if (!ParseStep(**Row, At, Step, Error)) { return false; }
        // Steps run in order, and the opening rest comes before the field.
        if (bField && Step.Window == ELDEvaluationStepWindow::OpeningRest)
        {
            Error = At + TEXT(".at: an opening_rest step after a field step");
            return false;
        }
        bField |= Step.Window == ELDEvaluationStepWindow::Field;
    }
    return true;
}

// Authority LD_* lines of one Case, as the Host log records them; the categories carry the playtest log contract.
struct FTraceCapture final : FOutputDevice
{
    FCriticalSection Lock;
    TArray<FString> Lines;
    bool bOn = false;

    virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type, const FName& Category) override
    {
        if (!Text || FCString::Strncmp(Text, TEXT("LD_"), 3) != 0) { return; }
        if (Category != LogLDCombat.GetCategoryName() && Category != LogLDGrowth.GetCategoryName()
            && Category != LogLDMatch.GetCategoryName() && Category != LogLDAI.GetCategoryName()
            && Category != LogLDTrace.GetCategoryName() && Category != LogLDTest.GetCategoryName())
        {
            return;
        }
        FScopeLock Guard(&Lock);
        if (bOn) { Lines.Add(Text); }
    }
    virtual bool CanBeUsedOnAnyThread() const override { return true; }
    virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

    void Begin()
    {
        FScopeLock Guard(&Lock);
        Lines.Reset();
        bOn = true;
    }

    TArray<FString> End()
    {
        GLog->Flush();
        FScopeLock Guard(&Lock);
        bOn = false;
        return MoveTemp(Lines);
    }
};

FString PhaseName(ELDEvaluationPhase Phase)
{
    switch (Phase)
    {
    case ELDEvaluationPhase::Building: return TEXT("Building");
    case ELDEvaluationPhase::Approaching: return TEXT("Approaching");
    case ELDEvaluationPhase::Engaging: return TEXT("Engaging");
    case ELDEvaluationPhase::Failed: return TEXT("Failed");
    default: return TEXT("Waiting");
    }
}

FString GrantName(ELDDevGrantStatus Status)
{
    switch (Status)
    {
    case ELDDevGrantStatus::Pending: return TEXT("Pending");
    case ELDDevGrantStatus::Granted: return TEXT("Granted");
    case ELDDevGrantStatus::Rejected: return TEXT("Rejected");
    default: return TEXT("Unknown");
    }
}

FString Id(const FGuid& Value) { return Value.IsValid() ? Value.ToString(EGuidFormats::Digits) : FString(); }
}

bool LDBuildEvaluation::ParseInput(const FString& Json, FLDEvalRunInput& Out, FString& Error)
{
    using namespace LDBuildEvaluationDetail;
    Out = {};
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
    {
        Error = TEXT("input: not a JSON object");
        return false;
    }
    FString Schema;
    int64 Version = 0;
    const TArray<TSharedPtr<FJsonValue>>* Cases = nullptr;
    if (!OnlyKeys(*Root, {TEXT("schema"), TEXT("schema_version"), TEXT("run_id"), TEXT("content_root"), TEXT("cases")},
            TEXT("input"), Error)
        || !ReadString(*Root, TEXT("schema"), TEXT("input"), Schema, Error)
        || !ReadInteger(*Root, TEXT("schema_version"), TEXT("input"), 0, MAX_int32, Version, Error)
        || !ReadString(*Root, TEXT("run_id"), TEXT("input"), Out.RunId, Error)
        || !ReadString(*Root, TEXT("content_root"), TEXT("input"), Out.ContentRoot, Error)
        || !ReadArray(*Root, TEXT("cases"), TEXT("input"), Cases, Error))
    {
        return false;
    }
    if (Schema != InputSchema || Version != InputSchemaVersion)
    {
        Error = FString::Printf(TEXT("input: schema %s/%lld is not %s/%d"), *Schema, Version, InputSchema, InputSchemaVersion);
        return false;
    }
    if (!IsIdentifier(Out.RunId, 128) || Out.ContentRoot.Len() != 64)
    {
        Error = TEXT("input: run_id is not an identifier or content_root is not a SHA-256 hex digest");
        return false;
    }
    if (Cases->IsEmpty() || Cases->Num() > MaxCases)
    {
        Error = FString::Printf(TEXT("input.cases: 1..%d cases"), MaxCases);
        return false;
    }
    TSet<FString> CaseIds;
    for (int32 Index = 0; Index < Cases->Num(); ++Index)
    {
        const FString Where = FString::Printf(TEXT("input.cases[%d]"), Index);
        const TSharedPtr<FJsonObject>* Row = nullptr;
        if (!(*Cases)[Index].IsValid() || !(*Cases)[Index]->TryGetObject(Row) || !Row || !Row->IsValid())
        {
            Error = Where + TEXT(": an object is required");
            return false;
        }
        const FJsonObject& Object = **Row;
        FLDEvalCase& Case = Out.Cases.AddDefaulted_GetRef();
        FString Seed, Site;
        int64 Timeout = 0, Wall = 0;
        const TArray<TSharedPtr<FJsonValue>>* Contenders = nullptr;
        if (!OnlyKeys(Object, {TEXT("case_id"), TEXT("seed"), TEXT("site"), TEXT("duel_timeout_ms"), TEXT("wall_timeout_s"),
                TEXT("contenders")}, Where, Error)
            || !ReadString(Object, TEXT("case_id"), Where, Case.CaseId, Error)
            || !ReadString(Object, TEXT("seed"), Where, Seed, Error)
            || !ReadString(Object, TEXT("site"), Where, Site, Error)
            || !ReadInteger(Object, TEXT("duel_timeout_ms"), Where, 1000, 600000, Timeout, Error)
            || !ReadInteger(Object, TEXT("wall_timeout_s"), Where, 10, 3600, Wall, Error)
            || !ReadArray(Object, TEXT("contenders"), Where, Contenders, Error))
        {
            return false;
        }
        if (!IsIdentifier(Case.CaseId, 96) || CaseIds.Contains(Case.CaseId))
        {
            Error = Where + TEXT(".case_id: a unique identifier is required");
            return false;
        }
        CaseIds.Add(Case.CaseId);
        if (!ParseSeed(Seed, Case.Seed))
        {
            Error = Where + TEXT(".seed: a decimal uint64 string is required");
            return false;
        }
        if (!IsIdentifier(Site, 64))
        {
            Error = Where + TEXT(".site: a public place id is required");
            return false;
        }
        Case.Site = FName(*Site);
        Case.DuelTimeoutMs = Timeout;
        Case.WallTimeoutSeconds = int32(Wall);
        if (Contenders->Num() != DuelAISlots)
        {
            Error = Where + TEXT(".contenders: exactly one subject and one opponent");
            return false;
        }
        for (int32 Side = 0; Side < Contenders->Num(); ++Side)
        {
            const FString At = FString::Printf(TEXT("%s.contenders[%d]"), *Where, Side);
            const TSharedPtr<FJsonObject>* Plan = nullptr;
            if (!(*Contenders)[Side].IsValid() || !(*Contenders)[Side]->TryGetObject(Plan) || !Plan || !Plan->IsValid())
            {
                Error = At + TEXT(": an object is required");
                return false;
            }
            if (!ParseContender(**Plan, At, Case.Contenders.AddDefaulted_GetRef(), Error)) { return false; }
        }
        const FLDEvalContenderPlan& A = Case.Contenders[0];
        const FLDEvalContenderPlan& B = Case.Contenders[1];
        if (A.Role == B.Role || A.Slot == B.Slot)
        {
            Error = Where + TEXT(".contenders: one subject and one opponent in distinct slots");
            return false;
        }
    }
    return true;
}

enum class ELDEvalStage : uint8 { Join, Lobby, Starting, Running, Closing, Returning };

class FLDBuildEvaluationProbe : public TSharedFromThis<FLDBuildEvaluationProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output;
    FLDEvalRunInput Input;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TUniquePtr<LDBuildEvaluationDetail::FTraceCapture> Capture;
    FString RunError;
    int32 CaseIndex = 0;
    int32 CasesWritten = 0;
    ELDEvalStage Stage = ELDEvalStage::Join;
    double LastCommandAt = -100;
    double RunStartedAt = 0;
    bool bFinished = false;

    struct FSide
    {
        FGuid Contender;
        TSharedPtr<FLDEvaluationRun> Run;
        uint64 Ticket = 0;
        int32 PresetsQueued = 0;
    };
    // Per Case, in the order of the Case's contender plans.
    FSide Sides[LDBuildEvaluation::DuelAISlots];
    uint32 MatchGeneration = 0;
    int64 StartedAtMs = -1;
    bool bSupplySent = false;
    uint64 LastKillSerial = 0;
    double CaseStartedAt = 0;
    double ReturnedAt = 0;
    bool bReturnSent = false;
    FString CaseStatus, CaseReason, OutcomeKind;
    int32 VictimSide = INDEX_NONE;
    FGuid Killer;
    int64 OutcomeAtMs = -1;
    int64 EndedAtMs = -1;

    const FLDEvalCase& Case() const { return Input.Cases[CaseIndex]; }

    bool Resend(double Interval = 1.)
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - LastCommandAt < Interval) { return false; }
        LastCommandAt = Now;
        return true;
    }

    bool Save(const TSharedRef<FJsonObject>& Value, const FString& Relative) const
    {
        FString Json;
        const FString File = FPaths::Combine(Output, Relative);
        return FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *(File + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*File, *(File + TEXT(".tmp")), true, true);
    }

    bool FinishRun()
    {
        if (bFinished) { return false; }
        bFinished = true;
        if (Capture)
        {
            GLog->RemoveOutputDevice(Capture.Get());
            Capture.Reset();
        }
        for (const FSide& Side : Sides) { LDDevGrowthFixture::DiscardEvaluationOpening(Side.Contender); }
        LDEvaluationSeeds::Disarm();
        LDEvaluationProfiles::Clear();
        const bool bPassed = RunError.IsEmpty() && CasesWritten == Input.Cases.Num();
        FString Version, VersionError, Commit;
        bool bDirtyKnown = false, bDirty = false;
        if (!LDDemoEntry::ReadProductVersion(Version, VersionError)) { Version = TEXT("unknown"); }
        LDLog::SourceRevision(Commit, bDirtyKnown, bDirty);
        auto Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("schema"), LDBuildEvaluation::RunSchema);
        Report->SetNumberField(TEXT("schema_version"), LDBuildEvaluation::ResultSchemaVersion);
        Report->SetStringField(TEXT("run_id"), Input.RunId);
        Report->SetBoolField(TEXT("passed"), bPassed);
        Report->SetStringField(TEXT("detail"), RunError);
        Report->SetNumberField(TEXT("cases_requested"), Input.Cases.Num());
        Report->SetNumberField(TEXT("cases_written"), CasesWritten);
        Report->SetStringField(TEXT("content_root"), Content ? Content->Get().Identity.Root.ToHex() : FString());
        Report->SetStringField(TEXT("product_version"), Version);
        Report->SetStringField(TEXT("source_commit"), Commit);
        Report->SetStringField(TEXT("source_dirty"), bDirtyKnown ? (bDirty ? TEXT("true") : TEXT("false")) : TEXT("unknown"));
        Report->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
        Report->SetBoolField(TEXT("fixed_time_step"), FApp::UseFixedTimeStep());
        Report->SetNumberField(TEXT("fixed_delta_seconds"), FApp::UseFixedTimeStep() ? FApp::GetFixedDeltaTime() : 0.);
        Report->SetNumberField(TEXT("seed_derivation"), LDEvaluationSeeds::DerivationVersion);
        Report->SetNumberField(TEXT("wall_seconds"), FPlatformTime::Seconds() - RunStartedAt);
        const bool bWritten = Save(Report, TEXT("native_run.json"));
        UE_LOG(LogLDTest, Display, TEXT("LD_EVAL_RUN passed=%d cases=%d/%d detail=%s"), bPassed && bWritten, CasesWritten,
            Input.Cases.Num(), *LDLog::Quote(RunError));
        FPlatformMisc::RequestExitWithStatus(false, bPassed && bWritten ? 0 : 2);
        return false;
    }

    bool FailRun(const FString& Detail)
    {
        if (RunError.IsEmpty()) { RunError = Detail; }
        return FinishRun();
    }

    ALDOpponentController* FindAdapter(UWorld& World, ALDGameMode& Mode, const FGuid& Contender) const
    {
        for (TActorIterator<ALDOpponentController> It(&World); It; ++It)
        {
            if (IsValid(*It) && Mode.ContenderId(*It) == Contender) { return *It; }
        }
        return nullptr;
    }

    bool BeginCase(ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        using namespace LDBuildEvaluation;
        const FLDEvalCase& Row = Case();
        for (FSide& Side : Sides) { Side = FSide(); }
        StartedAtMs = OutcomeAtMs = EndedAtMs = -1;
        bSupplySent = bReturnSent = false;
        LastKillSerial = 0;
        CaseStatus.Reset();
        CaseReason.Reset();
        OutcomeKind.Reset();
        VictimSide = INDEX_NONE;
        Killer.Invalidate();
        CaseStartedAt = FPlatformTime::Seconds();
        FString Error;
        LDEvaluationProfiles::Clear();
        if (!LDEvaluationSeeds::Arm(Row.Seed, Error)) { return FailRun(Error); }
        const FLDEvaluationSeeds Seeds = LDEvaluationSeeds::Derive(Row.Seed);
        for (int32 Index = 0; Index < DuelAISlots; ++Index) { Sides[Index].Contender = Seeds.AIContenders[Row.Contenders[Index].Slot]; }
        for (int32 Index = 0; Index < DuelAISlots; ++Index)
        {
            FLDEvaluationProfile Profile;
            Profile.Opponent = Sides[1 - Index].Contender;
            Profile.Site = Row.Site;
            Profile.Choices = Row.Contenders[Index].Choices;
            Profile.Build = Row.Contenders[Index].Build;
            Profile.Stance = Row.Contenders[Index].Stance;
            Profile.StandoffCm = Row.Contenders[Index].StandoffCm;
            Profile.ThresholdBp = Row.Contenders[Index].ThresholdBp;
            Profile.LeashCm = Row.Contenders[Index].LeashCm;
            Profile.ReactionMs = Row.Contenders[Index].ReactionMs;
            Profile.AimErrorCm = Row.Contenders[Index].AimErrorCm;
            Profile.SkipBp = Row.Contenders[Index].SkipBp;
            Profile.OperationSeed = Row.Seed;
            if (LDProgression::LevelForXP(Content->Get().Progression, Row.Contenders[Index].SupplyXP, Profile.SupplyLevel)
                != ELDGrowthReason::None)
            {
                return FailRun(FString::Printf(TEXT("The %s supply of %lld XP has no catalog level."), *Row.Contenders[Index].Role,
                    Row.Contenders[Index].SupplyXP));
            }
            if (!LDEvaluationProfiles::Stage(Sides[Index].Contender, Profile, Error)) { return FailRun(Error); }
            Sides[Index].Run = LDEvaluationProfiles::Find(Sides[Index].Contender);
        }
        MatchGeneration = Lobby.Generation;
        Capture->Begin();
        UE_LOG(LogLDTest, Display, TEXT("LD_EVAL_CASE_BEGIN case=%s index=%d seed=%llu subject=%s opponent=%s"), *Row.CaseId,
            CaseIndex, Row.Seed, *LDBuildEvaluationDetail::Id(Sides[0].Contender), *LDBuildEvaluationDetail::Id(Sides[1].Contender));
        for (int32 Index = 0; Index < DuelAISlots; ++Index)
        {
            const TArray<FName>& Opening = Row.Contenders[Index].OpeningPreset;
            if (Opening.IsEmpty()) { continue; }
            if (!LDDevGrowthFixture::StageEvaluationOpening(PC.GetWorld(), Sides[Index].Contender, Opening, Error))
            {
                return FailRun(FString::Printf(TEXT("The %s opening preset was refused: %s"), *Row.Contenders[Index].Role, *Error));
            }
            UE_LOG(LogLDTest, Display, TEXT("LD_EVAL_OPENING_PRESET contender=%s cards=%s"),
                *LDBuildEvaluationDetail::Id(Sides[Index].Contender),
                *FString::JoinBy(Opening, TEXT(","), [](const FName& Id) { return Id.ToString(); }));
        }
        PC.StartDemoMatch();
        Stage = ELDEvalStage::Starting;
        return true;
    }

    // The Case ends with one status; the trace and the result are written at once, before the match closes.
    void EndCase(ALDGameMode& Mode, UWorld& World, const FString& Status, const FString& Reason)
    {
        using namespace LDBuildEvaluationDetail;
        if (!CaseStatus.IsEmpty()) { return; }
        CaseStatus = Status;
        CaseReason = Reason;
        EndedAtMs = Mode.GetAuthorityNowMs();
        const FLDEvalCase& Row = Case();
        const TArray<FString> Lines = Capture->End();
        FString Trace = FString::Join(Lines, TEXT("\n"));
        Trace += TEXT("\n");
        const FString TracePath = FString::Printf(TEXT("traces/%s.log"), *Row.CaseId);
        const bool bTrace = FFileHelper::SaveStringToFile(Trace, *FPaths::Combine(Output, TracePath),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("schema"), LDBuildEvaluation::ResultSchema);
        Result->SetNumberField(TEXT("schema_version"), LDBuildEvaluation::ResultSchemaVersion);
        Result->SetStringField(TEXT("run_id"), Input.RunId);
        Result->SetStringField(TEXT("case_id"), Row.CaseId);
        Result->SetStringField(TEXT("seed"), FString::Printf(TEXT("%llu"), Row.Seed));
        Result->SetStringField(TEXT("status"), bTrace ? Status : TEXT("Faulted"));
        Result->SetStringField(TEXT("reason"), bTrace ? Reason : TEXT("the trace could not be written"));
        Result->SetNumberField(TEXT("generation"), MatchGeneration);
        Result->SetStringField(TEXT("content_root"), Content ? Content->Get().Identity.Root.ToHex() : FString());
        Result->SetNumberField(TEXT("started_at_ms"), double(StartedAtMs));
        Result->SetNumberField(TEXT("ended_at_ms"), double(EndedAtMs));
        auto Outcome = MakeShared<FJsonObject>();
        Outcome->SetStringField(TEXT("kind"), OutcomeKind.IsEmpty() ? TEXT("none") : *OutcomeKind);
        Outcome->SetStringField(TEXT("victim"), Sides[0].Contender.IsValid() && VictimSide != INDEX_NONE ? Row.Contenders[VictimSide].Role : FString());
        int32 KillerSide = INDEX_NONE;
        for (int32 Index = 0; Index < LDBuildEvaluation::DuelAISlots; ++Index) { if (Killer.IsValid() && Sides[Index].Contender == Killer) { KillerSide = Index; } }
        Outcome->SetStringField(TEXT("killer"), KillerSide != INDEX_NONE ? Row.Contenders[KillerSide].Role
            : Killer.IsValid() ? FString(TEXT("other")) : FString());
        Outcome->SetNumberField(TEXT("t_ms"), double(OutcomeAtMs >= 0 && StartedAtMs >= 0 ? OutcomeAtMs - StartedAtMs : -1));
        Result->SetObjectField(TEXT("outcome"), Outcome);
        TArray<TSharedPtr<FJsonValue>> Contenders;
        for (int32 Index = 0; Index < LDBuildEvaluation::DuelAISlots; ++Index)
        {
            const FSide& Side = Sides[Index];
            auto Value = MakeShared<FJsonObject>();
            Value->SetStringField(TEXT("role"), Row.Contenders[Index].Role);
            Value->SetNumberField(TEXT("slot"), Row.Contenders[Index].Slot);
            Value->SetStringField(TEXT("contender"), Id(Side.Contender));
            const FLDProgressionView View = Mode.Development ? Mode.Development->ReadProgression(Side.Contender) : FLDProgressionView();
            Value->SetBoolField(TEXT("progression_valid"), View.bValid);
            Value->SetNumberField(TEXT("level"), View.Level);
            Value->SetNumberField(TEXT("xp"), double(View.CumulativeXP));
            Value->SetNumberField(TEXT("choices"), View.ActiveOpportunitiesConsumed);
            Value->SetNumberField(TEXT("planned_choices"), Row.Contenders[Index].Choices.Num());
            Value->SetNumberField(TEXT("unspent_points"), double(View.UnspentPoints));
            TArray<TSharedPtr<FJsonValue>> StatPoints;
            for (const FLDStatPointsView& Stat : View.StatPoints)
            {
                auto Points = MakeShared<FJsonObject>();
                Points->SetStringField(TEXT("id"), Stat.StatId.ToString());
                Points->SetNumberField(TEXT("points"), Stat.Points);
                StatPoints.Add(MakeShared<FJsonValueObject>(Points));
            }
            Value->SetArrayField(TEXT("stat_points"), StatPoints);
            TArray<TSharedPtr<FJsonValue>> Actives;
            for (const FLDActiveSlotView& Active : View.Actives)
            {
                auto Learned = MakeShared<FJsonObject>();
                Learned->SetStringField(TEXT("id"), Active.DefinitionId.ToString());
                Learned->SetStringField(TEXT("slot"), Active.Slot == 0 ? TEXT("Q") : Active.Slot == 1 ? TEXT("W") : TEXT("E"));
                Actives.Add(MakeShared<FJsonValueObject>(Learned));
            }
            Value->SetArrayField(TEXT("actives"), Actives);
            TArray<TSharedPtr<FJsonValue>> Blueprints;
            for (const FName& Blueprint : View.PendingBlueprints) { Blueprints.Add(MakeShared<FJsonValueString>(Blueprint.ToString())); }
            Value->SetArrayField(TEXT("blueprints"), Blueprints);
            const ALDOpponentController* Adapter = FindAdapter(World, Mode, Side.Contender);
            const bool bBody = Adapter && Adapter->CombatView.bHasBody;
            Value->SetBoolField(TEXT("has_body"), bBody);
            Value->SetNumberField(TEXT("hp_p2"), bBody ? double(Adapter->CombatView.HPP2) : 0.);
            Value->SetNumberField(TEXT("max_hp_p2"), bBody ? double(Adapter->CombatView.MaxHPP2) : 0.);
            Value->SetNumberField(TEXT("mp_p2"), bBody ? double(Adapter->CombatView.MPP2) : 0.);
            Value->SetNumberField(TEXT("max_mp_p2"), bBody ? double(Adapter->CombatView.MaxMPP2) : 0.);
            Value->SetBoolField(TEXT("profile_bound"), Side.Run && Side.Run->bBound);
            Value->SetStringField(TEXT("profile_phase"), Side.Run ? PhaseName(Side.Run->State.Phase) : FString(TEXT("Missing")));
            Value->SetStringField(TEXT("profile_failure"), Side.Run ? Side.Run->State.Failure : FString());
            FString GrantDetail;
            const ELDDevGrantStatus Grant = Side.Ticket && Mode.Growth ? Mode.Growth->DevGrantStatus(Side.Ticket, GrantDetail)
                : ELDDevGrantStatus::Unknown;
            Value->SetNumberField(TEXT("supply_xp"), double(Row.Contenders[Index].SupplyXP));
            Value->SetStringField(TEXT("supply_status"), Row.Contenders[Index].SupplyXP > 0 ? GrantName(Grant) : FString(TEXT("None")));
            Value->SetNumberField(TEXT("presets_requested"), Row.Contenders[Index].Presets.Num());
            Value->SetNumberField(TEXT("presets_queued"), Side.PresetsQueued);
            Value->SetNumberField(TEXT("planned_steps"), Row.Contenders[Index].Build.Num());
            Value->SetNumberField(TEXT("steps_committed"), Side.Run ? Side.Run->State.StepsCommitted : 0);
            // #198 workshop: the terminal components with their history and install target, as growth holds them.
            TArray<TSharedPtr<FJsonValue>> Components;
            for (const FLDComponentInstanceView& Owned : View.Components)
            {
                auto Component = MakeShared<FJsonObject>();
                Component->SetStringField(TEXT("id"), Owned.DefinitionId.ToString());
                Component->SetBoolField(TEXT("installed"), Owned.bInstalled);
                Component->SetBoolField(TEXT("ever_installed"), Owned.bEverInstalled);
                Component->SetStringField(TEXT("source"), LDLog::EnumName(Owned.Source));
                Component->SetNumberField(TEXT("source_level"), Owned.SourceLevel);
                Component->SetStringField(TEXT("target_kind"), LDLog::EnumName(Owned.Target.Kind));
                Component->SetStringField(TEXT("target_id"), Owned.Target.Definition.IsNone() ? FString() : Owned.Target.Definition.ToString());
                Component->SetStringField(TEXT("element"), LDLog::EnumName(Owned.Target.Element));
                Component->SetStringField(TEXT("bound_active"), Owned.Target.BoundActive.IsNone() ? FString() : Owned.Target.BoundActive.ToString());
                Component->SetStringField(TEXT("bound_component"),
                    Owned.Target.BoundComponent.IsNone() ? FString() : Owned.Target.BoundComponent.ToString());
                Components.Add(MakeShared<FJsonValueObject>(Component));
            }
            Value->SetArrayField(TEXT("components"), Components);
            Contenders.Add(MakeShared<FJsonValueObject>(Value));
        }
        Result->SetArrayField(TEXT("contenders"), Contenders);
        Result->SetStringField(TEXT("trace"), TracePath);
        Result->SetNumberField(TEXT("trace_lines"), Lines.Num());
        Result->SetNumberField(TEXT("wall_seconds"), FPlatformTime::Seconds() - CaseStartedAt);
        if (!Save(Result, FString::Printf(TEXT("results/%s.json"), *Row.CaseId)))
        {
            RunError = TEXT("A Case result could not be written: ") + Row.CaseId;
        }
        else { ++CasesWritten; }
        UE_LOG(LogLDTest, Display, TEXT("LD_EVAL_CASE_END case=%s status=%s outcome=%s reason=%s lines=%d"), *Row.CaseId, *Status,
            OutcomeKind.IsEmpty() ? TEXT("none") : *OutcomeKind, *LDLog::Quote(Reason), Lines.Num());
        LDEvaluationSeeds::Disarm();
        Stage = ELDEvalStage::Closing;
    }

    bool TickStarting(ALDPlayerController& PC, ALDGameMode& Mode, UWorld& World, const ALDGameState& Public)
    {
        const FLDLobbyView& Lobby = Public.DemoLobby;
        const FLDPublicMatchView& View = Public.PublicMatch;
        if (Lobby.Phase == ELDMatchPhase::Lobby && Lobby.Generation != MatchGeneration)
        {
            Capture->End();
            return FailRun(TEXT("The Case start aborted: ") + PC.IntentFeedback);
        }
        if (View.Generation != MatchGeneration || (View.Phase != ELDMatchPhase::Rest && View.Phase != ELDMatchPhase::Development))
        {
            if (FPlatformTime::Seconds() - CaseStartedAt > Case().WallTimeoutSeconds)
            {
                Capture->End();
                return FailRun(TEXT("The Case match did not start within its wall budget."));
            }
            return true;
        }
        Stage = ELDEvalStage::Running;
        for (int32 Index = 0; Index < LDBuildEvaluation::DuelAISlots; ++Index)
        {
            const FSide& Side = Sides[Index];
            if (!Mode.Development || !Mode.Development->ReadProgression(Side.Contender).bValid)
            {
                EndCase(Mode, World, TEXT("Invalid"), TEXT("the seeded AI identity is not in the locked roster"));
                return true;
            }
            if (!Side.Run || !Side.Run->bBound)
            {
                EndCase(Mode, World, TEXT("Invalid"), TEXT("an AI adapter did not take its evaluation profile"));
                return true;
            }
        }
        for (int32 Index = 0; Index < LDBuildEvaluation::DuelAISlots; ++Index)
        {
            for (const TArray<FName>& Preset : Case().Contenders[Index].Presets)
            {
                FString Error;
                if (!Mode.Development->DevQueueOffer(Sides[Index].Contender, Preset, Error))
                {
                    EndCase(Mode, World, TEXT("Invalid"), FString::Printf(TEXT("%s preset refused: %s"), *Case().Contenders[Index].Role, *Error));
                    return true;
                }
                ++Sides[Index].PresetsQueued;
            }
        }
        return true;
    }

    bool TickRunning(ALDPlayerController& PC, ALDGameMode& Mode, UWorld& World, const ALDGameState& Public)
    {
        const FLDEvalCase& Row = Case();
        const FLDPublicMatchView& View = Public.PublicMatch;
        if (FPlatformTime::Seconds() - CaseStartedAt > Row.WallTimeoutSeconds)
        {
            EndCase(Mode, World, TEXT("BudgetExceeded"), TEXT("the Case exceeded its wall-clock budget"));
            return true;
        }
        for (int32 Index = 0; Index < LDBuildEvaluation::DuelAISlots; ++Index)
        {
            if (Sides[Index].Run && Sides[Index].Run->State.Phase == ELDEvaluationPhase::Failed)
            {
                EndCase(Mode, World, TEXT("Invalid"), FString::Printf(TEXT("%s plan refused: %s"), *Row.Contenders[Index].Role,
                    *Sides[Index].Run->State.Failure));
                return true;
            }
        }
        if (View.Generation != MatchGeneration
            || (View.Phase != ELDMatchPhase::Rest && View.Phase != ELDMatchPhase::Development && View.Phase != ELDMatchPhase::CaptureOpen))
        {
            EndCase(Mode, World, TEXT("Faulted"), FString::Printf(TEXT("the match left play (phase %u)"), uint8(View.Phase)));
            return true;
        }
        if (View.Phase == ELDMatchPhase::Rest)
        {
            if (StartedAtMs >= 0)
            {
                EndCase(Mode, World, TEXT("Invalid"), TEXT("the duel crossed the first field boundary"));
                return true;
            }
            if (View.Timeline.RestIndex == 0 && View.Timeline.Step == ELDRestStep::Build
                && Sides[0].Run && Sides[0].Run->State.bOpeningReady
                && Sides[1].Run && Sides[1].Run->State.bOpeningReady
                && !Mode.IsRestReady(PC.OwnerState.ContenderId) && Mode.CanRestReady(PC.OwnerState.ContenderId) && Resend(.5))
            {
                PC.SubmitRestReady(true);
            }
            return true;
        }
        if (View.Timeline.Stage != ELDMatchStage::Field || View.Timeline.FieldSegment != 0
            || View.Timeline.FieldBaseMs != 0 || View.Timeline.FieldStartedAtMs < 0
            || View.Timeline.StepEndsAtMs <= View.Timeline.FieldStartedAtMs
            || Row.DuelTimeoutMs >= View.Timeline.StepEndsAtMs - View.Timeline.FieldStartedAtMs
            || (StartedAtMs >= 0 && StartedAtMs != View.Timeline.FieldStartedAtMs))
        {
            EndCase(Mode, World, TEXT("Invalid"), TEXT("the duel must stay within the first field"));
            return true;
        }
        if (StartedAtMs < 0) { StartedAtMs = View.Timeline.FieldStartedAtMs; }
        if (!bSupplySent)
        {
            bSupplySent = true;
            for (int32 Index = 0; Index < LDBuildEvaluation::DuelAISlots; ++Index)
            {
                if (Row.Contenders[Index].SupplyXP <= 0) { continue; }
                FString Error;
                const FLDEvalContenderPlan& Plan = Row.Contenders[Index];
                Sides[Index].Ticket = !Mode.Growth ? 0 : Plan.LevelComponents.IsEmpty()
                    ? Mode.Growth->DevGrantReward(Sides[Index].Contender, Plan.SupplyXP, Error)
                    : Mode.Growth->DevGrantEvaluationReward(Sides[Index].Contender, Plan.SupplyXP, Plan.LevelComponents, Error);
                if (!Sides[Index].Ticket)
                {
                    EndCase(Mode, World, TEXT("Invalid"), TEXT("supply refused: ") + Error);
                    return true;
                }
            }
        }
        for (int32 Index = 0; Index < LDBuildEvaluation::DuelAISlots; ++Index)
        {
            FString Detail;
            if (Sides[Index].Ticket && Mode.Growth->DevGrantStatus(Sides[Index].Ticket, Detail) == ELDDevGrantStatus::Rejected)
            {
                EndCase(Mode, World, TEXT("Invalid"), TEXT("supply rejected: ") + Detail);
                return true;
            }
        }
        FLDEvalOutcome Observed;
        if (LDBuildEvaluation::SelectDuelOutcome(Public.KillFeed.Notices, LastKillSerial, Sides[0].Contender, Sides[1].Contender,
                StartedAtMs, Row.DuelTimeoutMs, Observed))
        {
            OutcomeKind = Observed.bTrade ? TEXT("trade") : TEXT("kill");
            VictimSide = Observed.VictimSide;
            Killer = Observed.Killer;
            OutcomeAtMs = Observed.OccurredAtMs;
            EndCase(Mode, World, TEXT("Completed"), FString());
            return true;
        }
        if (Mode.GetAuthorityNowMs() - StartedAtMs >= Row.DuelTimeoutMs)
        {
            OutcomeKind = TEXT("timeout");
            OutcomeAtMs = StartedAtMs + Row.DuelTimeoutMs;
            EndCase(Mode, World, TEXT("Completed"), TEXT("undecided at the duel timeout (censored)"));
        }
        return true;
    }

    bool Tick()
    {
        if (bFinished) { return false; }
        ALDPlayerController* PC = Controller.Get();
        UWorld* World = PC ? PC->GetWorld() : nullptr;
        ALDGameState* Public = World ? World->GetGameState<ALDGameState>() : nullptr;
        ALDGameMode* Mode = World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
        if (!PC || !World || !Public || !Mode) { return true; }
        const FLDLobbyView& Lobby = Public->DemoLobby;
        const FLDPublicMatchView& View = Public->PublicMatch;
        switch (Stage)
        {
        case ELDEvalStage::Join:
        {
            if (!PC->IsLocalController() || !Lobby.Generation) { return true; }
            if (World->GetNetMode() != NM_ListenServer) { return FailRun(TEXT("A build evaluation runs on a Listen Host.")); }
            if (!LDDevGrowthFixture::IsSwitchPresent()) { return FailRun(TEXT("A build evaluation needs -LDDevGrowthFixture for its XP supply.")); }
            FString Error;
            if (!LDDemoEntry::LoadRuntimeContent(Content, Error) || !Content) { return FailRun(TEXT("Approved content does not load: ") + Error); }
            if (!Content->Get().Identity.Root.ToHex().Equals(Input.ContentRoot, ESearchCase::IgnoreCase))
            {
                return FailRun(FString::Printf(TEXT("The loaded catalog %s is not the planned %s; plan again."),
                    *Content->Get().Identity.Root.ToHex(), *Input.ContentRoot));
            }
            Stage = ELDEvalStage::Lobby;
            return true;
        }
        case ELDEvalStage::Lobby:
        {
            if (Lobby.Phase != ELDMatchPhase::Lobby) { return true; }
            if (CaseIndex >= Input.Cases.Num()) { return FinishRun(); }
            int32 Humans = 0;
            for (const FLDLobbyMember& Row : Lobby.Members) { Humans += Row.Kind == ELDContenderKind::Human ? 1 : 0; }
            if (Humans != 1) { return FailRun(TEXT("A build evaluation needs the Host as the only human.")); }
            if (Lobby.AICount != LDBuildEvaluation::DuelAISlots)
            {
                if (Resend()) { PC->SetDemoAICount(LDBuildEvaluation::DuelAISlots); }
                return true;
            }
            const FLDLobbyMember* Self = Lobby.Members.FindByPredicate([PC](const FLDLobbyMember& Row)
            { return Row.ContenderId == PC->OwnerState.ContenderId; });
            if (!Self || !Self->Ready)
            {
                if (Resend()) { PC->SetDemoReady(true); }
                return true;
            }
            if (!Lobby.CanStart || !Lobby.StartBlocker.IsEmpty()) { return FailRun(TEXT("The ready room cannot start: ") + Lobby.StartBlocker); }
            return BeginCase(*PC, Lobby);
        }
        case ELDEvalStage::Starting:
            return TickStarting(*PC, *Mode, *World, *Public);
        case ELDEvalStage::Running:
            return TickRunning(*PC, *Mode, *World, *Public);
        case ELDEvalStage::Closing:
            if (View.Generation == MatchGeneration && View.Phase == ELDMatchPhase::Results)
            {
                Stage = ELDEvalStage::Returning;
                ReturnedAt = 0;
                return true;
            }
            // The Host ends the match; the core refuses an end inside the countdown or a step and is asked again.
            if (Resend(.5)) { PC->EndDemoMatch(); }
            if (FPlatformTime::Seconds() - CaseStartedAt > Case().WallTimeoutSeconds + 60.) { return FailRun(TEXT("The Case match did not end.")); }
            return true;
        case ELDEvalStage::Returning:
        {
            if (!bReturnSent || (View.Phase == ELDMatchPhase::Results && Resend(3.)))
            {
                PC->ReturnDemoToLobby();
                bReturnSent = true;
            }
            if (Lobby.Generation == MatchGeneration || Lobby.Phase != ELDMatchPhase::Lobby || View.Generation != Lobby.Generation
                || View.Phase != ELDMatchPhase::Lobby || PC->GetPawn()) { return true; }
            int32 Remaining = 0;
            for (TActorIterator<ALDOpponentController> It(World); It; ++It) { Remaining += IsValid(*It) ? 1 : 0; }
            if (!ReturnedAt) { ReturnedAt = FPlatformTime::Seconds(); }
            if (Remaining)
            {
                if (FPlatformTime::Seconds() - ReturnedAt < 15.) { return true; }
                return FailRun(TEXT("An AI controller outlived its Case match."));
            }
            LDEvaluationProfiles::Clear();
            ++CaseIndex;
            Stage = ELDEvalStage::Lobby;
            if (!RunError.IsEmpty()) { return FinishRun(); }
            return true;
        }
        default:
            return true;
        }
    }
};

void LDDemoTests::StartBuildEvaluation(ALDPlayerController& Controller)
{
    if (!LDEvaluationSeeds::IsSwitchPresent() || !Controller.IsLocalController() || Cast<ALDOpponentController>(&Controller)
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap) { return; }
    // A world travel creates the local controller again; the evaluation runs once per process.
    static bool bStarted = false;
    if (bStarted) { return; }
    bStarted = true;
    auto Probe = MakeShared<FLDBuildEvaluationProbe>();
    FString InputPath, Json, Error;
    const FString Artifacts = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"));
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDBuildEvalInput="), InputPath)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDBuildEvalOutput="), Probe->Output)
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output), Artifacts)
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(InputPath), Artifacts)
        || !FFileHelper::LoadFileToString(Json, *InputPath)
        || !LDBuildEvaluation::ParseInput(Json, Probe->Input, Error))
    {
        UE_LOG(LogLDTest, Error, TEXT("LD_EVAL_RUN Invalid build evaluation arguments or input: %s"), *LDLog::Quote(Error));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    IFileManager::Get().MakeDirectory(*FPaths::Combine(Probe->Output, TEXT("results")), true);
    IFileManager::Get().MakeDirectory(*FPaths::Combine(Probe->Output, TEXT("traces")), true);
    Probe->Controller = &Controller;
    Probe->RunStartedAt = FPlatformTime::Seconds();
    Probe->Capture = MakeUnique<LDBuildEvaluationDetail::FTraceCapture>();
    GLog->AddOutputDevice(Probe->Capture.Get());
    UE_LOG(LogLDTest, Display, TEXT("LD_EVAL_RUN_BEGIN run=%s cases=%d fixed_step=%d"), *Probe->Input.RunId, Probe->Input.Cases.Num(),
        FApp::UseFixedTimeStep() ? 1 : 0);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), 0.f);
}
#endif
