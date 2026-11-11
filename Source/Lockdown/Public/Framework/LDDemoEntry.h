#pragma once

#include "CoreMinimal.h"

class FLDDemoContentSnapshot;
struct FLDDemoContentDigest;

namespace LDDemoEntry
{
    inline constexpr TCHAR FrontendMap[] = TEXT("/Game/Lockdown/Generated/Maps/L_Frontend");
    inline constexpr TCHAR WorldMap[] = TEXT("/Game/Lockdown/Generated/Maps/L_Demo");
    inline constexpr TCHAR WorldDefinition[] = TEXT("/Game/Lockdown/Generated/Data/DA_DemoWorld");
    inline constexpr uint32 WireProtocol = 18;

    LOCKDOWN_API bool ValidateSelection(const FString& PackageName, const FString& Options, FString& Error);
    LOCKDOWN_API bool VerifyRuntimeFiles(FString& Error, FString* CatalogDigest = nullptr);
    LOCKDOWN_API bool VerifyPayloadFiles(FString& Error, FString* CatalogDigest = nullptr);
    LOCKDOWN_API bool LoadRuntimeContent(TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error);
    LOCKDOWN_API bool VerifyWorldPackages(FString& Error);
    LOCKDOWN_API bool ValidateAddress(const FString& Address, FString& Error);
    LOCKDOWN_API bool ReadProductVersion(FString& Version, FString& Error);
    // The title screen's version line. A release build ([Lockdown.Build] bReleaseBuild=True, set only by the nightly's
    // version commit) shows "版本 <product version>". Any other build shows "build <short commit>" (with a note when the
    // package was built from uncommitted changes), not the product version: on main that is only the version line the
    // build started from, and showing it would pass the build off as that release. Joining still compares the product
    // version.
    LOCKDOWN_API FString FormatTitleVersion(const FString& Version, bool bReleaseBuild, const FString& Commit, bool bDirty);
    LOCKDOWN_API FString TitleVersion(const FString& Version);
    LOCKDOWN_API bool VerifyFileHash(const FString& File, const FString& ExpectedSHA256, FString& Error);
#if WITH_DEV_AUTOMATION_TESTS
    // DEV content hot reload (-LDDevContentReload, uncooked editor/Standalone builds only). LoadRuntimeContent then
    // returns one pinned snapshot of the current Sync outputs on disk: demo_content.json may differ from the compiled
    // contract, everything else (world, schemas, native ids, contracts) must still match the build. The snapshot is
    // replaced only by RefreshDevContent, which the authority core calls when a new match starts, so no reader sees
    // numbers change during a match.
    LOCKDOWN_API bool IsDevContentReload();
    LOCKDOWN_API bool RefreshDevContent(FString& Error);
    // The catalog root compiled into this build (LDDemoContentContract.h). Generated assets stay bound to it.
    LOCKDOWN_API bool IsCompiledCatalogRoot(const FLDDemoContentDigest& Root);
#endif

    // A Host's PreLogin refusal reaches the joining client as "<Code>: <English detail>". The code is the stable
    // contract: the client shows only the reason mapped from it, never the English detail or engine text.
    namespace JoinRefusal
    {
        inline constexpr TCHAR RoomNotReady[] = TEXT("MissingDemoDefinition");
        inline constexpr TCHAR MatchAlreadyStarted[] = TEXT("MatchAlreadyStarted");
        inline constexpr TCHAR RoomFull[] = TEXT("RoomFull");
        inline constexpr TCHAR ProtocolMismatch[] = TEXT("ProtocolMismatch");
        inline constexpr TCHAR ProductMismatch[] = TEXT("ProductMismatch");
        inline constexpr TCHAR ContentMismatch[] = TEXT("ContentMismatch");
        inline constexpr TCHAR InvalidName[] = TEXT("InvalidName");
    }
    LOCKDOWN_API FString MakeJoinRefusal(const TCHAR* Code, const TCHAR* Detail);
    // True only for a known refusal code; OutReason is the player-facing reason naming that code.
    LOCKDOWN_API bool DescribeJoinRefusal(const FString& ServerMessage, FString& OutReason);
}
