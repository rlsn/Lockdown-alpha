#pragma once

#include "CoreMinimal.h"
#include "Development/LDBuildBlueprint.h"

// Local build blueprint files (#198 §10). The owning machine writes its own record under
// FPaths::ProjectSavedDir()/Blueprints; exports go to Blueprints/Exports and received files are read from any of
// Blueprints, Blueprints/Exports and Blueprints/Imports. Nothing here uploads, links or executes a file.
namespace LDBuildBlueprintStore
{
    LOCKDOWN_API FString UserDirectory();
    LOCKDOWN_API FString ExportDirectory(const FString& Directory);
    LOCKDOWN_API FString ImportDirectory(const FString& Directory);
    // match-<32 hex digits>.ldbuild.json: the automatic record of one match.
    LOCKDOWN_API FString AutoFileName(const FString& MatchId);

    struct FRetention
    {
        int32 MaxFiles = LDBuildBlueprint::MaxAutoFiles;
        int64 MaxBytes = LDBuildBlueprint::MaxAutoBytes;
    };

    struct FPruneResult
    {
        int32 Deleted = 0;
        int32 Kept = 0;
        int64 KeptBytes = 0;
        // Files that should have been deleted but could not be; the newest records are never deleted in their place.
        TArray<FString> Errors;
    };
    // Deletes the oldest automatic records (origin auto, this schema and version, the file name of their own match id,
    // ordered by recorded_at_utc) in Directory itself until at most MaxFiles of them remain within MaxBytes. Exports,
    // other files, foreign or invalid records, subdirectories and the directory itself are never touched.
    LOCKDOWN_API FPruneResult Prune(const FString& Directory, const FRetention& Limits = FRetention());

    struct FWriteResult
    {
        bool bSaved = false;
        FString Path;
        // Player-facing reason when not saved; the earlier blueprints are kept.
        FString Error;
        FPruneResult Prune;
    };
    // The automatic record of Record's match: rejected without truncation above MaxFileBytes, written to a sibling
    // temporary file, read back, moved into place and read back again; only then is the directory pruned.
    LOCKDOWN_API FWriteResult WriteAuto(const FLDBlueprintRecord& Record, const FString& Directory,
        const FRetention& Limits = FRetention());
    // An explicit export (origin export, never pruned) under ExportDirectory(Directory), a new file name each time.
    LOCKDOWN_API FWriteResult Export(const FLDBlueprintRecord& Record, const FString& Directory);

    // Every *.ldbuild.json directly in Directory, its Exports and its Imports, newest first.
    LOCKDOWN_API void ListViewable(const FString& Directory, TArray<FString>& Out);
    // Strict read of one file for the read-only viewer (size first, then LDBuildBlueprint::Parse with MaxFileBytes).
    LOCKDOWN_API ELDBlueprintParseError ReadFile(const FString& Path, FLDBlueprintRecord& Out, FString& Detail);
    // Path relative to the project's Saved directory for display ("Saved/Blueprints/..."); never a machine path in config.
    LOCKDOWN_API FString DisplayPath(const FString& Path);
}
