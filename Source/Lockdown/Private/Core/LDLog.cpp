#include "Core/LDLog.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentIds.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProperties.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace LDLogDetail
{
    // The separators a reader splitting on Unicode whitespace (Python's str.isspace, the summarizer's \S) would see;
    // fixed here rather than taken from the C runtime's locale-dependent classification.
    bool IsUnicodeSpace(TCHAR Char)
    {
        return Char == 0x85 || Char == 0xA0 || Char == 0x1680 || (Char >= 0x2000 && Char <= 0x200A) || Char == 0x2028
            || Char == 0x2029 || Char == 0x202F || Char == 0x205F || Char == 0x3000;
    }

    bool ReadTrimmed(const FString& Path, FString& Out)
    {
        Out.Reset();
        if (!FFileHelper::LoadFileToString(Out, *Path)) { return false; }
        Out.TrimStartAndEndInline();
        return !Out.IsEmpty();
    }

    bool IsCommit(const FString& Text)
    {
        if (Text.Len() != 40) { return false; }
        for (const TCHAR Char : Text) { if (!FChar::IsHexDigit(Char)) { return false; } }
        return true;
    }

    // HEAD of the work tree at ProjectDir: a .git directory or a worktree's "gitdir:" file, loose or packed refs.
    bool GitHead(FString& OutCommit)
    {
        const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        FString GitDir = FPaths::Combine(Project, TEXT(".git"));
        FString Pointer;
        if (!FPaths::DirectoryExists(GitDir))
        {
            if (!ReadTrimmed(GitDir, Pointer) || !Pointer.StartsWith(TEXT("gitdir:"))) { return false; }
            GitDir = Pointer.RightChop(7).TrimStartAndEnd();
            if (FPaths::IsRelative(GitDir)) { GitDir = FPaths::Combine(Project, GitDir); }
        }
        FString CommonDir = GitDir, Common;
        if (ReadTrimmed(FPaths::Combine(GitDir, TEXT("commondir")), Common))
        {
            CommonDir = FPaths::IsRelative(Common) ? FPaths::Combine(GitDir, Common) : Common;
        }
        FString Head;
        if (!ReadTrimmed(FPaths::Combine(GitDir, TEXT("HEAD")), Head)) { return false; }
        if (!Head.StartsWith(TEXT("ref:")))
        {
            OutCommit = Head;
            return IsCommit(OutCommit);
        }
        const FString Ref = Head.RightChop(4).TrimStartAndEnd();
        for (const FString& Dir : {GitDir, CommonDir})
        {
            if (ReadTrimmed(FPaths::Combine(Dir, Ref), OutCommit) && IsCommit(OutCommit)) { return true; }
        }
        FString Packed;
        if (!FFileHelper::LoadFileToString(Packed, *FPaths::Combine(CommonDir, TEXT("packed-refs")))) { return false; }
        TArray<FString> Lines;
        Packed.ParseIntoArrayLines(Lines);
        for (const FString& Line : Lines)
        {
            FString Commit, Name;
            if (Line.Split(TEXT(" "), &Commit, &Name) && Name.TrimStartAndEnd() == Ref && IsCommit(Commit))
            {
                OutCommit = Commit;
                return true;
            }
        }
        return false;
    }

    // Archive\Windows is the packaged root; PackageWin64.ps1 writes the manifest two folders above it.
    bool PackageManifest(FString& OutCommit, bool& bOutDirty)
    {
        FString Json;
        const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::RootDir(), TEXT(".."), TEXT(".."), TEXT("PackageManifest.json")));
        if (!FFileHelper::LoadFileToString(Json, *Path)) { return false; }
        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        return FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid() && Root->TryGetStringField(TEXT("sourceRevision"), OutCommit)
            && IsCommit(OutCommit) && Root->TryGetBoolField(TEXT("sourceDirty"), bOutDirty);
    }
}

void LDLog::SourceRevision(FString& OutCommit, bool& bOutDirtyKnown, bool& bOutDirty)
{
    static bool bRead = false, bDirtyKnown = false, bDirty = false;
    static FString Commit;
    if (!bRead)
    {
        bRead = true;
        if (FPlatformProperties::RequiresCookedData()) { bDirtyKnown = LDLogDetail::PackageManifest(Commit, bDirty); }
        else if (!LDLogDetail::GitHead(Commit)) { Commit.Reset(); }
        if (!bDirtyKnown && !LDLogDetail::IsCommit(Commit)) { Commit.Reset(); }
    }
    OutCommit = Commit.IsEmpty() ? FString(TEXT("unknown")) : Commit;
    bOutDirtyKnown = bDirtyKnown;
    bOutDirty = bDirty;
}

DEFINE_LOG_CATEGORY(LogLDMatch);
DEFINE_LOG_CATEGORY(LogLDCombat);
DEFINE_LOG_CATEGORY(LogLDGrowth);
DEFINE_LOG_CATEGORY(LogLDNav);
DEFINE_LOG_CATEGORY(LogLDAI);
DEFINE_LOG_CATEGORY(LogLDWorld);
DEFINE_LOG_CATEGORY(LogLDNet);
DEFINE_LOG_CATEGORY(LogLDUI);
DEFINE_LOG_CATEGORY(LogLDContent);
DEFINE_LOG_CATEGORY(LogLDTest);
DEFINE_LOG_CATEGORY(LogLDTrace);

FString LDLog::Quote(const FString& Value)
{
    bool bBare = !Value.IsEmpty();
    for (const TCHAR Char : Value)
    {
        if (Char <= TEXT(' ') || Char == TEXT('"') || Char == TEXT('=') || Char == TEXT('\\') || Char == 0x7f || LDLogDetail::IsUnicodeSpace(Char))
        {
            bBare = false;
            break;
        }
    }
    if (bBare) { return Value; }
    FString Out;
    Out.Reserve(Value.Len() + 2);
    Out.AppendChar(TEXT('"'));
    for (const TCHAR Char : Value)
    {
        if (Char == TEXT('"') || Char == TEXT('\\')) { Out.AppendChar(TEXT('\\')); Out.AppendChar(Char); }
        else if (Char == TEXT('\n')) { Out += TEXT("\\n"); }
        else if (Char == TEXT('\r')) { Out += TEXT("\\r"); }
        else if (Char == TEXT('\t')) { Out += TEXT("\\t"); }
        else if (Char < TEXT(' ') || Char == 0x7f) { Out.AppendChar(TEXT('?')); }
        else { Out.AppendChar(Char); }
    }
    Out.AppendChar(TEXT('"'));
    return Out;
}

FString LDLog::P2(int64 Value)
{
    // Magnitude through uint64 so MIN_int64 has no signed overflow.
    const bool bNegative = Value < 0;
    const uint64 Magnitude = bNegative ? uint64(0) - uint64(Value) : uint64(Value);
    return FString::Printf(TEXT("%s%llu.%02llu"), bNegative ? TEXT("-") : TEXT(""), Magnitude / 100, Magnitude % 100);
}

FString LDLog::Cm(const FVector& Value)
{
    return FString::Printf(TEXT("%lld,%lld,%lld"), FMath::RoundToInt64(Value.X), FMath::RoundToInt64(Value.Y), FMath::RoundToInt64(Value.Z));
}

FString LDLog::Id(const FGuid& Value)
{
    return Value.IsValid() ? Value.ToString(EGuidFormats::Digits) : FString(TEXT("-"));
}

FString LDLog::StatusName(uint8 StatusId)
{
    switch (static_cast<ELDDemoStatusId>(StatusId))
    {
    case ELDDemoStatusId::Root: return TEXT("root");
    case ELDDemoStatusId::Shield: return TEXT("shield");
    case ELDDemoStatusId::CampReturning: return TEXT("camp_returning");
    case ELDDemoStatusId::RespawnProtection: return TEXT("respawn_protection");
    case ELDDemoStatusId::Slow: return TEXT("slow");
    case ELDDemoStatusId::StatBoost: return TEXT("stat_boost");
    case ELDDemoStatusId::Exposed: return TEXT("exposed");
    case ELDDemoStatusId::Taunt: return TEXT("taunt");
    case ELDDemoStatusId::Disarm: return TEXT("disarm");
    case ELDDemoStatusId::Stun: return TEXT("stun");
    case ELDDemoStatusId::Empowered: return TEXT("empowered");
    case ELDDemoStatusId::Knockback: return TEXT("knockback");
    case ELDDemoStatusId::Silence: return TEXT("silence");
    case ELDDemoStatusId::Burn: return TEXT("burn");
    default: return FString::FromInt(StatusId);
    }
}

FString LDLog::Source(const FLDCombatSourceRef& Source)
{
    switch (Source.Kind)
    {
    case ELDCombatSourceKind::Basic: return TEXT("basic");
    case ELDCombatSourceKind::Learned: return TEXT("learned:") + Source.DefinitionId.ToString();
    case ELDCombatSourceKind::Innate: return TEXT("innate:") + Source.DefinitionId.ToString();
    case ELDCombatSourceKind::Component: return TEXT("component:") + Source.DefinitionId.ToString();
    default: return TEXT("none");
    }
}

bool LDLog::FRateLimit::Allow(double NowSeconds, uint32& OutSuppressed)
{
    OutSuppressed = 0;
    if (bAny && NowSeconds >= LastAt && NowSeconds - LastAt < IntervalSeconds)
    {
        if (Held < MAX_uint32) { ++Held; }
        return false;
    }
    OutSuppressed = Held;
    Held = 0;
    bAny = true;
    LastAt = NowSeconds;
    return true;
}

bool LDLog::FPeriodicGate::Due(int64 NowMs)
{
    const int64 Period = FMath::Max<int64>(PeriodMs, 1);
    if (!bStarted || NowMs < LastMs || NowMs >= NextMs)
    {
        bStarted = true;
        LastMs = NowMs;
        // The next boundary after now, so later samples land on whole periods of the clock.
        const int64 Floor = NowMs >= 0 ? NowMs / Period * Period : -((-NowMs + Period - 1) / Period) * Period;
        NextMs = Floor <= MAX_int64 - Period ? Floor + Period : MAX_int64;
        return true;
    }
    LastMs = NowMs;
    return false;
}
