#include "Tests/LDTestApprovedDisplay.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Development/LDDemoContentCatalog.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

bool LDTestApprovedDisplay::Names(FAutomationTestBase& Test,TMap<FString,FString>& Out)
{
    // One import per process: every caller reads the same approved_base.
    static TMap<FString,FString> Cached;
    if(!Cached.IsEmpty()){Out=Cached;return true;}
    FString Root,Error,Text;
    if(!FParse::Value(FCommandLine::Get(),TEXT("LDDemoContentFixtures="),Root)||Root.IsEmpty()
        ||!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Root),FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(),TEXT("Artifacts"))))
    {Test.AddError(TEXT("Display-name tests require isolated LDDemoContentFixtures with approved_base; no live-data fallback."));return false;}
    TSharedPtr<FJsonObject> Index;
    if(!FFileHelper::LoadFileToString(Text,*FPaths::Combine(Root,TEXT("index.json")))
        ||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Index)||!Index)
    {Test.AddError(TEXT("Unreadable content fixture index."));return false;}
    FString Format;bool bTestOnly=false;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Index->TryGetStringField(TEXT("format"),Format)||Format!=TEXT("demo-native-fixtures-1")
        ||!Index->TryGetBoolField(TEXT("test_only"),bTestOnly)||!bTestOnly||!Index->TryGetArrayField(TEXT("valid"),Rows))
    {Test.AddError(TEXT("Invalid content fixture index."));return false;}
    bool bFound=false;FLDDemoContentDigest Digest;
    for(const TSharedPtr<FJsonValue>& Value:*Rows)
    {
        const TSharedPtr<FJsonObject>* Row=nullptr;FString Name,Hex;
        if(!Value->TryGetObject(Row)||!(*Row)->TryGetStringField(TEXT("name"),Name)){Test.AddError(TEXT("Invalid fixture case."));return false;}
        if(Name!=TEXT("approved_base")){continue;}
        if(bFound||!(*Row)->TryGetStringField(TEXT("catalog_sha256"),Hex)||!FLDDemoContentDigest::ParseHex(Hex,Digest,Error))
        {Test.AddError(TEXT("Duplicate or invalid approved_base fixture."));return false;}
        bFound=true;
    }
    if(!bFound){Test.AddError(TEXT("Missing approved_base fixture."));return false;}
    FLDDemoContentBundle Bundle;TUniquePtr<FLDDemoPreparedContent> Prepared;TSharedPtr<const FLDDemoContentSnapshot> Snapshot;
    auto* Asset=NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    if(!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Root,TEXT("approved_base")),Bundle,Error)
        ||!ULDDemoContentCatalog::PrepareImport(*Asset,Bundle,Digest,Prepared,Error)
        ||!Asset->CommitImport(Prepared,Digest,Error)||!Asset->Read(Digest,Snapshot,Error)||!Snapshot)
    {Test.AddError(Error);return false;}
    for(const FLDDemoDisplayDefinition& Display:Snapshot->Get().Display){Cached.Add(Display.Id,Display.NameText);}
    if(Cached.IsEmpty()){Test.AddError(TEXT("The approved catalog has no display rows."));return false;}
    Out=Cached;
    return true;
}
#endif
