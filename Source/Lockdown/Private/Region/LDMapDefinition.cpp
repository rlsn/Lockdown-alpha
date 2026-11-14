#include "Region/LDMapDefinition.h"
#include "LDMapGeometryJson.h"
#include "../Development/LDDemoContentJson.h"
#include "Framework/LDDemoEntry.h"
namespace
{
TArray<uint8> SourceBytes(const FString& Text)
{
    const FTCHARToUTF8 Bytes(*Text);
    TArray<uint8> Result;
    Result.Append(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
    return Result;
}
}

bool ULDMapDefinition::Import(const TArray<uint8>& Json, const FLDDemoContentSnapshot& Content, FString& Error)
{
    Error.Reset();
    LDDemoContent::FCheck Check(Error);
    TUniquePtr<FLDDemoPreparedWorld> Prepared;
    if (!Check.Require(IsInGameThread(), TEXT("world import"), TEXT("import requires the game thread"))
        || !PrepareProjection(Json, Content, Prepared, Error))
    {
        return false;
    }
    DemoGeometry = MoveTemp(Prepared->Geometry);
    ContentRoot = Prepared->ContentRoot;
    SourceJson = MoveTemp(Prepared->SourceJson);
    Checksum = MoveTemp(Prepared->Checksum);
    MarkPackageDirty();
    return true;
}

bool ULDMapDefinition::PrepareProjection(const TArray<uint8>& Json, const FLDDemoContentSnapshot& Content,
    TUniquePtr<FLDDemoPreparedWorld>& Out, FString& Error)
{
    Error.Reset();
    LDDemoContent::FCheck Check(Error);
    LDDemoContent::FValue Root;
    FLDDemoContentDigest Hash;
    TUniquePtr<FLDDemoPreparedWorld> Candidate(new FLDDemoPreparedWorld());
    if (!LDDemoContent::Hash(Json, Hash, Check, TEXT("world"))
        || !Check.Require(Hash == Content.Get().Identity.World, TEXT("world"), TEXT("bytes differ from the trusted content root"))
        || !LDDemoContent::Parse(Json, Root, Check, TEXT("world"))
        || !LDDemoWorld::Project(Root, Content.Get(), Candidate->Geometry, Check))
    {
        return false;
    }
    const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Json.GetData()), Json.Num());
    Candidate->SourceJson = FString(Decoded.Length(), Decoded.Get());
    Candidate->Checksum = Hash.ToHex();
    Candidate->ContentRoot = Content.Get().Identity.Root;
    Out = MoveTemp(Candidate);
    return true;
}

bool ULDMapDefinition::ValidatePrepared(const FLDDemoPreparedWorld& Expected, FString& Error) const
{
    Error.Reset();
    LDDemoContent::FCheck Check(Error);
    if (!Check.Require(ContentRoot == Expected.ContentRoot, TEXT("world asset"), TEXT("asset belongs to another content root"))
        || !Check.Require(SourceJson.Equals(Expected.SourceJson, ESearchCase::CaseSensitive)
            && Checksum.Equals(Expected.Checksum, ESearchCase::CaseSensitive),
            TEXT("world asset"), TEXT("saved world bytes differ from the trusted root"))
        || !Check.Require(LDDemoContent::ExactStoredStruct(FLDDemoWorldGeometry::StaticStruct(), &DemoGeometry, &Expected.Geometry),
            TEXT("world asset"), TEXT("saved geometry differs from its complete typed projection")))
    {
        return false;
    }
    return true;
}

bool ULDMapDefinition::Read(const FLDDemoContentSnapshot& Content, TSharedPtr<const FLDDemoWorldGeometry>& Out, FString& Error) const
{
    TUniquePtr<FLDDemoPreparedWorld> Expected;
    if (!PrepareProjection(SourceBytes(SourceJson), Content, Expected, Error)) { return false; }
#if WITH_DEV_AUTOMATION_TESTS
    // DEV content hot reload: this asset stays bound to the compiled root while the reloaded content has its own.
    // The world bytes and the full typed geometry re-projected with the reloaded numbers must still match exactly,
    // so a number that moves world geometry is refused until Bootstrap and a rebuild.
    if (LDDemoEntry::IsDevContentReload() && Expected->ContentRoot != ContentRoot && LDDemoEntry::IsCompiledCatalogRoot(ContentRoot))
    {
        Expected->ContentRoot = ContentRoot;
    }
#endif
    if (!ValidatePrepared(*Expected, Error)) { return false; }
    Out = MakeShared<const FLDDemoWorldGeometry>(MoveTemp(Expected->Geometry));
    return true;
}

#if WITH_EDITOR
bool ULDMapDefinition::ReadSavedForEditing(FLDDemoWorldGeometry& Out, FString& Error) const
{
    Error.Reset();
    LDDemoContent::FCheck Check(Error);
    FLDDemoContentDigest Digest;
    LDDemoContent::FValue Root;
    FLDDemoContentDigest Source;
    if (!LDDemoContent::Hash(SourceBytes(SourceJson), Digest, Check, TEXT("saved authoring world"))
        || !Check.Require(Digest.ToHex() == Checksum,
            TEXT("saved authoring world"), TEXT("saved world bytes differ from their stored checksum"))
        || !LDDemoContent::Parse(SourceBytes(SourceJson), Root, Check, TEXT("saved authoring world"))) { return false; }
    LDDemoContent::FFields Fields(Check, Root, TEXT("saved authoring world"));
    if (!Fields.Digest(TEXT("source_sha256"), Source)
        || !Check.Require(Source == DemoGeometry.SourceDigest, TEXT("saved authoring world"),
            TEXT("saved geometry belongs to another author source"))) { return false; }
    Out = DemoGeometry;
    return true;
}
#endif

ALDMapAnchor::ALDMapAnchor()
{ bReplicates = false; RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root")); }
