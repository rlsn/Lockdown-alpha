#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

#if __has_include("../Development/LDDemoContentContract.h") && __has_include("../World/LDDemoWorldContract.h")
#include "../Development/LDDemoContentContract.h"
#include "../World/LDDemoWorldContract.h"
#define LD_HAS_DEMO_CONTRACT 1
#else
#define LD_HAS_DEMO_CONTRACT 0
#endif

THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

bool LDDemoEntry::ValidateSelection(const FString& PackageName, const FString& Options, FString& Error)
{
    Error.Reset();
    for (const TCHAR* Option : {TEXT("Profile"), TEXT("Humans"), TEXT("game")})
    {
        if (UGameplayStatics::HasOption(Options, Option))
        {
            Error = TEXT("Retired launch option: ") + FString(Option) + TEXT(". Use the Demo menu; legacy profiles are not supported.");
            return false;
        }
    }
    const FString CanonicalPackage = UWorld::RemovePIEPrefix(PackageName);
    if (CanonicalPackage != FrontendMap && CanonicalPackage != WorldMap)
    {
        Error = TEXT("Unsupported gameplay map. The only gameplay world is L_Demo; legacy maps are retired.");
        return false;
    }
    if (CanonicalPackage == FrontendMap && UGameplayStatics::HasOption(Options, TEXT("listen")))
    {
        Error = TEXT("The frontend is not a gameplay room. Create a Demo room from the menu.");
        return false;
    }
    return true;
}

bool LDDemoEntry::VerifyRuntimeFiles(FString& Error, FString* CatalogDigest)
{
    if (CatalogDigest) { CatalogDigest->Reset(); }
    TSharedPtr<const FLDDemoContentSnapshot> Saved;
    if (!LoadRuntimeContent(Saved, Error)) { return false; }
    if (CatalogDigest) { *CatalogDigest = Saved->Get().Identity.Root.ToHex(); }
    return true;
}

bool LDDemoEntry::LoadRuntimeContent(TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error)
{
    FString DigestHex;
    FLDDemoContentDigest Expected;
    return VerifyPayloadFiles(Error, &DigestHex)
        && FLDDemoContentDigest::ParseHex(DigestHex, Expected, Error)
        && ULDDemoContentCatalog::Load(Expected, Out, Error);
}

bool LDDemoEntry::VerifyPayloadFiles(FString& Error, FString* CatalogDigest)
{
    Error.Reset();
    if (CatalogDigest) { CatalogDigest->Reset(); }
    const FString Root = FPaths::ProjectContentDir() / TEXT("Lockdown/Data/Demo");
    for (const TCHAR* File : {TEXT("manifest.json"), TEXT("demo_content.json"), TEXT("world.json")})
    {
        if (!FPaths::FileExists(Root / File))
        {
            Error = TEXT("Missing approved Demo input: ") + FString(File)
                + TEXT(". Generate the approved inputs for this build; legacy content will not be substituted.");
            return false;
        }
    }
#if LD_HAS_DEMO_CONTRACT
    if (LD_DEMO_CONTENT_SCHEMA_VERSION != 1 || LD_DEMO_CONTENT_HANDLER_REVISION != 1
        || LD_DEMO_CONTENT_PROJECTION_REVISION != 2 || LD_DEMO_WORLD_SCHEMA_VERSION != 2
        || LD_DEMO_WORLD_PROJECTION_REVISION != 2)
    {
        Error = TEXT("Unsupported Demo content/world schema. Regenerate approved inputs and rebuild their native consumers together.");
        return false;
    }
    const TPair<const TCHAR*, const TCHAR*> Files[] = {
        {TEXT("manifest.json"), LD_DEMO_CATALOG_SHA256},
        {TEXT("demo_content.json"), LD_DEMO_CONTENT_PAYLOAD_SHA256},
        {TEXT("world.json"), LD_DEMO_WORLD_PAYLOAD_SHA256}
    };
    for (const auto& File : Files)
    {
        if (!VerifyFileHash(Root / File.Key, File.Value, Error)) { return false; }
    }
    if (CatalogDigest) { *CatalogDigest = LD_DEMO_CATALOG_SHA256; }
    return true;
#else
    Error = TEXT("Missing compiled Demo content/world contracts. Approved payloads and generated headers must be built together.");
    return false;
#endif
}

bool LDDemoEntry::VerifyFileHash(const FString& File, const FString& ExpectedSHA256, FString& Error)
{
    Error.Reset();
    bool ValidDigest = ExpectedSHA256.Len() == 64;
    for (TCHAR Character : ExpectedSHA256)
    {
        ValidDigest &= (Character >= TEXT('0') && Character <= TEXT('9')) || (Character >= TEXT('a') && Character <= TEXT('f'));
    }
    TArray<uint8> Bytes;
    FSHA256Signature Digest;
    if (!ValidDigest || !FFileHelper::LoadFileToArray(Bytes, *File) || Bytes.IsEmpty()
        || !SHA256(Bytes.GetData(), Bytes.Num(), Digest.Signature) || Digest.ToString().ToLower() != ExpectedSHA256)
    {
        Error = TEXT("Demo input differs from this build: ") + FPaths::GetCleanFilename(File)
            + TEXT(". Run the Demo compiler checks and rebuild; do not edit generated hashes.");
        return false;
    }
    return true;
}

bool LDDemoEntry::VerifyWorldPackages(FString& Error)
{
    Error.Reset();
    for (const TCHAR* Package : {WorldMap, WorldDefinition})
    {
        if (!FPackageName::DoesPackageExist(Package))
        {
            Error = TEXT("Missing Demo world asset: ") + FString(Package)
                + TEXT(". The approved Demo world must be generated and validated; legacy maps are not a fallback.");
            return false;
        }
    }
    return true;
}

bool LDDemoEntry::ValidateAddress(const FString& Address, FString& Error)
{
    Error.Reset();
    if (Address.IsEmpty() || Address.Len() > 259)
    {
        Error = TEXT("Enter a host name or IPv4 address, optionally followed by :port.");
        return false;
    }
    const FRegexPattern Pattern(TEXT("^[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?(?::[0-9]{1,5})?$"));
    FRegexMatcher Matcher(Pattern, Address);
    if (!Matcher.FindNext() || Matcher.GetMatchEnding() != Address.Len())
    {
        Error = TEXT("Enter only a host name or IPv4 address and optional :port, not a URL, map, or travel options.");
        return false;
    }
    FString Host, Port;
    if (Address.Split(TEXT(":"), &Host, &Port) && (FCString::Atoi(*Port) < 1 || FCString::Atoi(*Port) > 65535))
    {
        Error = TEXT("Port must be between 1 and 65535.");
        return false;
    }
    return true;
}

bool LDDemoEntry::ReadProductVersion(FString& Version, FString& Error)
{
    Version.Reset();
    Error.Reset();
    if (!GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni)
        || Version.IsEmpty())
    {
        Error = TEXT("Missing product version in project settings. This build cannot join a Demo room.");
        return false;
    }
    const FRegexPattern Pattern(TEXT("^[0-9]+\\.[0-9]+\\.[0-9]+$"));
    FRegexMatcher Matcher(Pattern, Version);
    if (!Matcher.FindNext() || Matcher.GetMatchEnding() != Version.Len())
    {
        Error = TEXT("Invalid product version in project settings. This build cannot join a Demo room.");
        Version.Reset();
        return false;
    }
    return true;
}
