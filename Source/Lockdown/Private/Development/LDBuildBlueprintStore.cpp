#include "Development/LDBuildBlueprintStore.h"
#include "Core/LDLog.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace LDBuildBlueprintStoreDetail
{
const TCHAR* const AutoPrefix = TEXT("match-");

// The match id an automatic file name stands for, or empty when the name is not one.
FString AutoNameId(const FString& Name)
{
    const FString Extension(LDBuildBlueprint::Extension);
    if (!Name.StartsWith(AutoPrefix, ESearchCase::CaseSensitive) || !Name.EndsWith(Extension, ESearchCase::CaseSensitive))
    {
        return FString();
    }
    const FString Id = Name.Mid(FCString::Strlen(AutoPrefix), Name.Len() - FCString::Strlen(AutoPrefix) - Extension.Len());
    if (Id.Len() != 32) { return FString(); }
    for (const TCHAR C : Id)
    {
        if (!FChar::IsHexDigit(C)) { return FString(); }
    }
    return Id;
}

bool ReadBack(const FString& Path, const TArray<uint8>& Expected)
{
    TArray<uint8> Bytes;
    return FFileHelper::LoadFileToArray(Bytes, *Path) && Bytes == Expected;
}

// Writes Bytes to Target through a sibling temporary file that never carries the blueprint extension.
bool WriteThroughTemporary(const TArray<uint8>& Bytes, const FString& Target, bool bReplace, FString& Error)
{
    IFileManager& Files = IFileManager::Get();
    const FString Temporary = Target + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    const auto Abandon = [&Files, &Temporary]() { Files.Delete(*Temporary, false, false, true); };
    if (!FFileHelper::SaveArrayToFile(Bytes, *Temporary))
    {
        Abandon();
        Error = TEXT("写入构筑蓝图文件失败（磁盘不可写或空间不足）。");
        return false;
    }
    if (!ReadBack(Temporary, Bytes))
    {
        Abandon();
        Error = TEXT("构筑蓝图写入后无法核验，没有保存。");
        return false;
    }
    if (!Files.Move(*Target, *Temporary, bReplace, false, false, true))
    {
        Abandon();
        Error = TEXT("构筑蓝图无法移动到最终位置，没有保存。");
        return false;
    }
    if (!ReadBack(Target, Bytes))
    {
        Error = TEXT("构筑蓝图已写入，但回读核验失败，请在蓝图页重新导出。");
        return false;
    }
    return true;
}

bool SerializeWithin(const FLDBlueprintRecord& Record, TArray<uint8>& Bytes, FString& Error)
{
    FString Detail;
    if (!LDBuildBlueprint::Serialize(Record, Bytes, Detail))
    {
        Error = FString::Printf(TEXT("构筑蓝图无法序列化：%s"), *Detail);
        return false;
    }
    if (Bytes.Num() > LDBuildBlueprint::MaxFileBytes)
    {
        Error = FString::Printf(TEXT("完整蓝图有 %.2f MiB，超过 2 MiB 上限；没有截断，也没有保存。本局结果仍可在此页只读查看。"),
            double(Bytes.Num()) / (1024. * 1024.));
        return false;
    }
    return true;
}
}

FString LDBuildBlueprintStore::UserDirectory()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Blueprints"));
}

FString LDBuildBlueprintStore::ExportDirectory(const FString& Directory)
{
    return FPaths::Combine(Directory, TEXT("Exports"));
}

FString LDBuildBlueprintStore::ImportDirectory(const FString& Directory)
{
    return FPaths::Combine(Directory, TEXT("Imports"));
}

FString LDBuildBlueprintStore::AutoFileName(const FString& MatchId)
{
    return LDBuildBlueprintStoreDetail::AutoPrefix + MatchId + LDBuildBlueprint::Extension;
}

LDBuildBlueprintStore::FPruneResult LDBuildBlueprintStore::Prune(const FString& Directory, const FRetention& Limits)
{
    FPruneResult Result;
    IFileManager& Files = IFileManager::Get();
    struct FAuto
    {
        FString Path;
        FDateTime RecordedAt;
        int64 Bytes = 0;
    };
    TArray<FAuto> Autos;
    TArray<FString> Names;
    Files.FindFiles(Names, *FPaths::Combine(Directory, FString(TEXT("*")) + LDBuildBlueprint::Extension), true, false);
    for (const FString& Name : Names)
    {
        const FString Id = LDBuildBlueprintStoreDetail::AutoNameId(Name);
        if (Id.IsEmpty()) { continue; }
        const FString Path = FPaths::Combine(Directory, Name);
        FLDBlueprintRecord Record;
        FString Detail;
        if (ReadFile(Path, Record, Detail) != ELDBlueprintParseError::None || Record.Origin != LDBuildBlueprint::OriginAuto
            || !Record.MatchId.Equals(Id, ESearchCase::IgnoreCase))
        {
            continue;
        }
        Autos.Add({Path, Record.RecordedAtUtc, Files.FileSize(*Path)});
    }
    Autos.Sort([](const FAuto& A, const FAuto& B)
    { return A.RecordedAt != B.RecordedAt ? A.RecordedAt < B.RecordedAt : A.Path < B.Path; });
    int64 Total = 0;
    for (const FAuto& Auto : Autos) { Total += Auto.Bytes; }
    int32 Count = Autos.Num();
    for (const FAuto& Oldest : Autos)
    {
        if (Count <= Limits.MaxFiles && Total <= Limits.MaxBytes) { break; }
        if (Files.Delete(*Oldest.Path, true, false, true))
        {
            --Count;
            Total -= Oldest.Bytes;
            ++Result.Deleted;
            UE_LOG(LogLDUI, Log, TEXT("LD_BUILD_BLUEPRINT_PRUNED file=%s"), *FPaths::GetCleanFilename(Oldest.Path));
        }
        else
        {
            Result.Errors.Add(FPaths::GetCleanFilename(Oldest.Path));
            UE_LOG(LogLDUI, Warning, TEXT("LD_BUILD_BLUEPRINT_PRUNE_FAILED file=%s"), *FPaths::GetCleanFilename(Oldest.Path));
        }
    }
    Result.Kept = Count;
    Result.KeptBytes = Total;
    return Result;
}

LDBuildBlueprintStore::FWriteResult LDBuildBlueprintStore::WriteAuto(const FLDBlueprintRecord& Record, const FString& Directory,
    const FRetention& Limits)
{
    FWriteResult Result;
    FLDBlueprintRecord Auto = Record;
    Auto.Origin = LDBuildBlueprint::OriginAuto;
    TArray<uint8> Bytes;
    if (!Auto.bComplete)
    {
        Result.Error = TEXT("这份蓝图不完整，不作为自动记录保存。");
        return Result;
    }
    if (!LDBuildBlueprintStoreDetail::SerializeWithin(Auto, Bytes, Result.Error))
    {
        return Result;
    }
    if (!IFileManager::Get().MakeDirectory(*Directory, true))
    {
        Result.Error = TEXT("无法创建构筑蓝图目录，没有保存。");
        return Result;
    }
    Result.Path = FPaths::Combine(Directory, AutoFileName(Auto.MatchId));
    if (!LDBuildBlueprintStoreDetail::WriteThroughTemporary(Bytes, Result.Path, true, Result.Error))
    {
        return Result;
    }
    Result.bSaved = true;
    Result.Prune = Prune(Directory, Limits);
    if (!Result.Prune.Errors.IsEmpty())
    {
        Result.Error = FString::Printf(TEXT("本局蓝图已保存，但有 %d 份旧的自动蓝图删除失败。"), Result.Prune.Errors.Num());
    }
    return Result;
}

LDBuildBlueprintStore::FWriteResult LDBuildBlueprintStore::Export(const FLDBlueprintRecord& Record, const FString& Directory)
{
    FWriteResult Result;
    FLDBlueprintRecord Exported = Record;
    Exported.Origin = LDBuildBlueprint::OriginExport;
    TArray<uint8> Bytes;
    if (!LDBuildBlueprintStoreDetail::SerializeWithin(Exported, Bytes, Result.Error))
    {
        return Result;
    }
    const FString Folder = ExportDirectory(Directory);
    if (!IFileManager::Get().MakeDirectory(*Folder, true))
    {
        Result.Error = TEXT("无法创建导出目录，没有导出。");
        return Result;
    }
    const FString Stem = FString::Printf(TEXT("build-%s-%s"), *Exported.MatchId.Left(8).ToLower(),
        *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")));
    for (int32 Attempt = 1; Attempt <= 99 && Result.Path.IsEmpty(); ++Attempt)
    {
        const FString Candidate = FPaths::Combine(Folder, (Attempt == 1 ? Stem : FString::Printf(TEXT("%s-%d"), *Stem, Attempt))
            + LDBuildBlueprint::Extension);
        if (!IFileManager::Get().FileExists(*Candidate)) { Result.Path = Candidate; }
    }
    if (Result.Path.IsEmpty())
    {
        Result.Error = TEXT("导出目录里同名文件太多，没有导出。");
        return Result;
    }
    // Never replaces an existing file.
    Result.bSaved = LDBuildBlueprintStoreDetail::WriteThroughTemporary(Bytes, Result.Path, false, Result.Error);
    return Result;
}

void LDBuildBlueprintStore::ListViewable(const FString& Directory, TArray<FString>& Out)
{
    Out.Reset();
    IFileManager& Files = IFileManager::Get();
    struct FEntry
    {
        FString Path;
        FDateTime Modified;
    };
    TArray<FEntry> Entries;
    for (const FString& Folder : {Directory, ExportDirectory(Directory), ImportDirectory(Directory)})
    {
        TArray<FString> Names;
        Files.FindFiles(Names, *FPaths::Combine(Folder, FString(TEXT("*")) + LDBuildBlueprint::Extension), true, false);
        for (const FString& Name : Names)
        {
            const FString Path = FPaths::Combine(Folder, Name);
            Entries.Add({Path, Files.GetTimeStamp(*Path)});
        }
    }
    Entries.Sort([](const FEntry& A, const FEntry& B) { return A.Modified != B.Modified ? A.Modified > B.Modified : A.Path < B.Path; });
    for (const FEntry& Entry : Entries) { Out.Add(Entry.Path); }
}

ELDBlueprintParseError LDBuildBlueprintStore::ReadFile(const FString& Path, FLDBlueprintRecord& Out, FString& Detail)
{
    Out = FLDBlueprintRecord();
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size < 0)
    {
        Detail = TEXT("文件不存在");
        return ELDBlueprintParseError::NotJson;
    }
    if (Size > LDBuildBlueprint::MaxFileBytes)
    {
        Detail = FString::Printf(TEXT("%lld bytes"), Size);
        return ELDBlueprintParseError::TooLarge;
    }
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() != Size)
    {
        Detail = TEXT("无法读取完整文件");
        return ELDBlueprintParseError::NotJson;
    }
    return LDBuildBlueprint::Parse(Bytes, LDBuildBlueprint::MaxFileBytes, Out, Detail);
}

FString LDBuildBlueprintStore::DisplayPath(const FString& Path)
{
    FString Full = FPaths::ConvertRelativePathToFull(Path);
    FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
    // MakePathRelativeTo treats a path without the trailing separator as a file in its parent.
    if (!Saved.EndsWith(TEXT("/"))) { Saved += TEXT("/"); }
    if (FPaths::MakePathRelativeTo(Full, *Saved) && !Full.StartsWith(TEXT("..")))
    {
        return FPaths::Combine(TEXT("Saved"), Full);
    }
    return FPaths::GetCleanFilename(Path);
}
