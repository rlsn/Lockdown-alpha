#pragma once

#include "CoreMinimal.h"

class FLDDemoContentSnapshot;

namespace LDDemoEntry
{
    inline constexpr TCHAR FrontendMap[] = TEXT("/Game/Lockdown/Generated/Maps/L_Frontend");
    inline constexpr TCHAR WorldMap[] = TEXT("/Game/Lockdown/Generated/Maps/L_Demo");
    inline constexpr TCHAR WorldDefinition[] = TEXT("/Game/Lockdown/Generated/Data/DA_DemoWorld");
    inline constexpr uint32 WireProtocol = 2;

    LOCKDOWN_API bool ValidateSelection(const FString& PackageName, const FString& Options, FString& Error);
    LOCKDOWN_API bool VerifyRuntimeFiles(FString& Error, FString* CatalogDigest = nullptr);
    LOCKDOWN_API bool VerifyPayloadFiles(FString& Error, FString* CatalogDigest = nullptr);
    LOCKDOWN_API bool LoadRuntimeContent(TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error);
    LOCKDOWN_API bool VerifyWorldPackages(FString& Error);
    LOCKDOWN_API bool ValidateAddress(const FString& Address, FString& Error);
    LOCKDOWN_API bool ReadProductVersion(FString& Version, FString& Error);
    LOCKDOWN_API bool VerifyFileHash(const FString& File, const FString& ExpectedSHA256, FString& Error);
}
