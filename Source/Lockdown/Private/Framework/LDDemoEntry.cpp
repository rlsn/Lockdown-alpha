#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentText.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
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
            Error = TEXT("启动参数 ") + FString(Option) + TEXT(" 已退役，请从 Demo 菜单开始；不支持旧玩法模式。");
            return false;
        }
    }
    const FString CanonicalPackage = UWorld::RemovePIEPrefix(PackageName);
    if (CanonicalPackage != FrontendMap && CanonicalPackage != WorldMap)
    {
        Error = TEXT("不支持的对局地图。唯一的对局世界是 L_Demo，旧地图已退役。");
        return false;
    }
    if (CanonicalPackage == FrontendMap && UGameplayStatics::HasOption(Options, TEXT("listen")))
    {
        Error = TEXT("主菜单不是对局房间，请从菜单创建 Demo 房间。");
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
    TSharedPtr<const FLDDemoContentSnapshot> Loaded;
    if (!VerifyPayloadFiles(Error, &DigestHex) || !FLDDemoContentDigest::ParseHex(DigestHex, Expected, Error)
        || !ULDDemoContentCatalog::Load(Expected, Loaded, Error) || !LDDemoContentText::Register(*Loaded, Error))
    {
        return false;
    }
    Out = MoveTemp(Loaded);
    return true;
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
            Error = TEXT("缺少已批准的 Demo 输入：") + FString(File)
                + TEXT("。请为本版本生成已批准的输入；不会用旧内容代替。");
            return false;
        }
    }
#if LD_HAS_DEMO_CONTRACT
    if (LD_DEMO_CONTENT_SCHEMA_VERSION != 1 || LD_DEMO_CONTENT_HANDLER_REVISION != 1
        || LD_DEMO_CONTENT_PROJECTION_REVISION != 3 || LD_DEMO_WORLD_SCHEMA_VERSION != 2
        || LD_DEMO_WORLD_PROJECTION_REVISION != 2)
    {
        Error = TEXT("不支持的 Demo 内容/世界 schema。请重新生成已批准的输入，并与原生代码一起重新构建。");
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
    Error = TEXT("缺少已编译的 Demo 内容/世界契约。已批准的载荷与生成头文件必须一起构建。");
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
        Error = TEXT("Demo 输入与本版本不一致：") + FPaths::GetCleanFilename(File)
            + TEXT("。请运行 Demo 编译检查并重新构建；不要手改生成的哈希。");
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
            Error = TEXT("缺少 Demo 世界资产：") + FString(Package)
                + TEXT("。必须生成并校验已批准的 Demo 世界；旧地图不能作为替代。");
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
        Error = TEXT("请输入主机名或 IPv4 地址，可在后面加 :端口。");
        return false;
    }
    const FRegexPattern Pattern(TEXT("^[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?(?::[0-9]{1,5})?$"));
    FRegexMatcher Matcher(Pattern, Address);
    if (!Matcher.FindNext() || Matcher.GetMatchEnding() != Address.Len())
    {
        Error = TEXT("只能输入主机名或 IPv4 地址和可选的 :端口，不能输入网址、地图或旅行参数。");
        return false;
    }
    FString Host, Port;
    if (Address.Split(TEXT(":"), &Host, &Port) && (FCString::Atoi(*Port) < 1 || FCString::Atoi(*Port) > 65535))
    {
        Error = TEXT("端口必须在 1 到 65535 之间。");
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
        Error = TEXT("项目设置缺少产品版本，此版本无法加入 Demo 房间。");
        return false;
    }
#if WITH_DEV_AUTOMATION_TESTS
    // DEV session-fault fixture only: its refused joiner announces another product version through the real Join path.
    FString Announced;
    if (FParse::Param(FCommandLine::Get(), TEXT("LDDemoSessionFaultProbe"))
        && FParse::Value(FCommandLine::Get(), TEXT("LDDemoSessionFaultProductOverride="), Announced))
    {
        Version = Announced;
    }
#endif
    const FRegexPattern Pattern(TEXT("^[0-9]+\\.[0-9]+\\.[0-9]+$"));
    FRegexMatcher Matcher(Pattern, Version);
    if (!Matcher.FindNext() || Matcher.GetMatchEnding() != Version.Len())
    {
        Error = TEXT("项目设置中的产品版本无效，此版本无法加入 Demo 房间。");
        Version.Reset();
        return false;
    }
    return true;
}

FString LDDemoEntry::MakeJoinRefusal(const TCHAR* Code, const TCHAR* Detail)
{
    return FString::Printf(TEXT("%s: %s"), Code, Detail);
}

bool LDDemoEntry::DescribeJoinRefusal(const FString& ServerMessage, FString& OutReason)
{
    FString Code, Detail;
    if (!ServerMessage.Split(TEXT(": "), &Code, &Detail, ESearchCase::CaseSensitive)) { return false; }
    const auto Is = [&Code](const TCHAR* Known) { return Code.Equals(Known, ESearchCase::CaseSensitive); };
    const TCHAR* const SameBuild = TEXT("请使用与房主相同的 Demo 版本。");
    const TCHAR* Reason = nullptr;
    const TCHAR* Hint = TEXT("");
    if (Is(JoinRefusal::MatchAlreadyStarted)) { Reason = TEXT("对局已开始，不能中途加入"); Hint = TEXT("请等房主回到房间后再加入。"); }
    else if (Is(JoinRefusal::RoomFull)) { Reason = TEXT("房间已满，最多 3 名真人，含房主"); }
    else if (Is(JoinRefusal::ProtocolMismatch)) { Reason = TEXT("版本与房主不兼容，网络协议不同"); Hint = SameBuild; }
    else if (Is(JoinRefusal::ProductMismatch)) { Reason = TEXT("版本与房主不兼容，产品版本不同"); Hint = SameBuild; }
    else if (Is(JoinRefusal::ContentMismatch)) { Reason = TEXT("版本与房主不兼容，内容数据不同"); Hint = SameBuild; }
    else if (Is(JoinRefusal::InvalidName)) { Reason = TEXT("房主不接受这个玩家名称"); Hint = TEXT("请修改名称后再加入。"); }
    else if (Is(JoinRefusal::RoomNotReady)) { Reason = TEXT("房主的房间尚未就绪"); }
    else { return false; }
    OutReason = FString::Printf(TEXT("%s（%s）。%s"), Reason, *Code, Hint);
    return true;
}
