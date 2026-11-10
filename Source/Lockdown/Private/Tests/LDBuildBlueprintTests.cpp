#include "Combat/LDCombatTypes.h"
#include "Development/LDBuildBlueprint.h"
#include "Development/LDBuildBlueprintStore.h"
#include "Development/LDDemoContentDefinitions.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FBlueprintStatArg
{
    const TCHAR* Stat;
    int32 Points;
};
struct FBlueprintActiveArg
{
    const TCHAR* Ability;
    ELDCombatEntry Entry;
};
FLDBuildProjectionInput BlueprintBuild(uint8 Level, uint64 Revision, const TArray<FBlueprintStatArg>& Stats,
    const TArray<FBlueprintActiveArg>& Actives)
{
    FLDBuildProjectionInput Build;
    Build.Level=Level;
    Build.ResultBuildRevision=Revision;
    for(const FBlueprintStatArg& Stat:Stats){Build.StatPoints.Emplace(FString(Stat.Stat),Stat.Points);}
    for(const FBlueprintActiveArg& Active:Actives){Build.Actives.Add(FLDLearnedActiveValue{FName(Active.Ability),Active.Entry});}
    return Build;
}

LDBuildBlueprint::FHeader BlueprintHeader(const FGuid& Match, const FDateTime& At, int64 EndedAtMs)
{
    LDBuildBlueprint::FHeader Header;
    Header.ProductVersion=TEXT("0.10.0");
    Header.WireProtocol=15;
    Header.CatalogRoot=FString::ChrN(64,TEXT('a'));
    Header.CatalogSchemaVersion=3;
    Header.HandlerRevision=2;
    Header.ProjectionRevision=1;
    Header.MatchId=Match;
    Header.EndReason=TEXT("guard_succeeded");
    Header.Outcome=TEXT("won");
    Header.RecordedAtUtc=At;
    Header.StartedAtMs=3000;
    Header.EndedAtMs=EndedAtMs;
    Header.FinalLevel=2;
    Header.Kills=1;
    Header.Assists=2;
    return Header;
}

// Two versions: the empty match-start build, then one stat point and a Q active from 5 s.
bool BlueprintSample(FAutomationTestBase& Test, const FGuid& Match, const FDateTime& At, FLDBlueprintRecord& Out)
{
    FLDBuildLedger Ledger;
    const FGuid Contender=FGuid::NewGuid();
    Ledger.Observe(Contender,BlueprintBuild(1,0,{},{}),0);
    const int32 Second=Ledger.Observe(Contender,BlueprintBuild(2,3,{{TEXT("might"),1}},{{TEXT("bolt"),ELDCombatEntry::Q}}),5000);
    Ledger.Add(Contender,Second,ELDBlueprintFact::DamageDealt,1234);
    Ledger.Add(Contender,Second,ELDBlueprintFact::AbilityCasts,2);
    TArray<FLDBlueprintVersion> Versions;
    FString Error;
    const bool bComposed=Ledger.Close(Contender,90000,Versions)
        && LDBuildBlueprint::Compose(BlueprintHeader(Match,At,90000),Versions,Out,Error);
    Test.TestTrue(FString::Printf(TEXT("The sample record composes (%s)"),*Error),bComposed);
    return bComposed;
}

FString BlueprintUtf8(const TArray<uint8>& Bytes)
{
    const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()),Bytes.Num());
    return FString(Decoded.Length(),Decoded.Get());
}

TArray<uint8> BlueprintBytes(const FString& Text)
{
    const FTCHARToUTF8 Encoded(*Text,Text.Len());
    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(Encoded.Get()),Encoded.Length());
    return Bytes;
}

ELDBlueprintParseError BlueprintParseText(const FString& Text)
{
    FLDBlueprintRecord Record;
    FString Detail;
    return LDBuildBlueprint::Parse(BlueprintBytes(Text),LDBuildBlueprint::MaxFileBytes,Record,Detail);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildBlueprintSerializeTest,"Lockdown.Rules.BuildBlueprint.Serialize",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDBuildBlueprintSerializeTest::RunTest(const FString& Parameters)
{
    const FGuid Match=FGuid::NewGuid();
    const FDateTime At(2026,11,10,2,3,4,567);
    FLDBlueprintRecord Record;
    if(!BlueprintSample(*this,Match,At,Record)){return false;}
    TArray<uint8> Bytes;
    FString Error;
    TestTrue(TEXT("A composed record serializes"),LDBuildBlueprint::Serialize(Record,Bytes,Error));
    const FString Text=BlueprintUtf8(Bytes);
    TestTrue(TEXT("The file names its schema and version"),Text.Contains(TEXT("\"schema\": \"lockdown.build-blueprint\""))
        &&Text.Contains(TEXT("\"schema_version\": 1"))&&Text.Contains(TEXT("\"origin\": \"auto\"")));
    const int32 Debt=Text.Find(TEXT("\"debt_repaid_p2\""));
    const FString DebtObject=Debt==INDEX_NONE?FString():Text.Mid(Debt,160);
    TestTrue(TEXT("A fact without a producer is written as not collected with a null value, never 0"),
        DebtObject.Contains(TEXT("\"collected\": false"))&&DebtObject.Contains(TEXT("\"value\": null")));
    const int32 Components=Text.Find(TEXT("\"components\""));
    TestTrue(TEXT("Components the build does not hold yet are not collected (null), never an empty list"),
        Components!=INDEX_NONE&&Text.Mid(Components,120).Contains(TEXT("\"items\": null")));
    TestFalse(TEXT("No contender identity, account or address is written"),Text.Contains(TEXT("\"contender"))||Text.Contains(TEXT("address")));

    FLDBlueprintRecord Parsed;
    FString Detail;
    TestTrue(TEXT("The written bytes read back strictly"),LDBuildBlueprint::Parse(Bytes,LDBuildBlueprint::MaxFileBytes,Parsed,Detail)==ELDBlueprintParseError::None);
    TestEqual(TEXT("Match id"),Parsed.MatchId,Match.ToString(EGuidFormats::Digits));
    TestEqual(TEXT("Recorded time keeps its millisecond"),Parsed.RecordedAtUtc.ToIso8601(),At.ToIso8601());
    TestTrue(TEXT("Final build and revision are the last version"),Parsed.FinalBuild==Record.FinalBuild&&Parsed.FinalRevision==3
        &&Parsed.FinalBuild.Actives.Num()==1&&Parsed.FinalBuild.Actives[0].Slot==TEXT("Q")&&Parsed.FinalBuild.Actives[0].Ability==TEXT("bolt"));
    TestTrue(TEXT("Both versions with their time segments"),Parsed.Versions.Num()==2&&Parsed.Versions[0].FromMs==0
        &&Parsed.Versions[0].ToMs==5000&&Parsed.Versions[1].FromMs==5000&&Parsed.Versions[1].ToMs==90000);
    if(Parsed.Versions.Num()==2)
    {
        const FLDBlueprintFactValue& Dealt=Parsed.Versions[1].Facts[int32(ELDBlueprintFact::DamageDealt)];
        const FLDBlueprintFactValue& Early=Parsed.Versions[0].Facts[int32(ELDBlueprintFact::DamageDealt)];
        const FLDBlueprintFactValue& Debt2=Parsed.Versions[1].Facts[int32(ELDBlueprintFact::DebtRepaid)];
        TestTrue(TEXT("The facts stay with the version that produced them"),Dealt.bCollected&&Dealt.Value==1234&&Early.bCollected&&Early.Value==0);
        TestTrue(TEXT("Not collected survives the round trip"),!Debt2.bCollected&&Debt2.Value==0);
    }
    TArray<uint8> Again;
    TestTrue(TEXT("Serialization is canonical: a read record writes the same bytes"),LDBuildBlueprint::Serialize(Parsed,Again,Error)&&Again==Bytes);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildBlueprintValidateTest,"Lockdown.Rules.BuildBlueprint.Validate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDBuildBlueprintValidateTest::RunTest(const FString& Parameters)
{
    FLDBlueprintRecord Record;
    if(!BlueprintSample(*this,FGuid::NewGuid(),FDateTime(2026,11,10),Record)){return false;}
    TArray<uint8> Bytes;
    FString Error;
    if(!TestTrue(TEXT("Sample serializes"),LDBuildBlueprint::Serialize(Record,Bytes,Error))){return false;}
    const FString Text=BlueprintUtf8(Bytes);
    TestTrue(TEXT("The valid sample passes"),BlueprintParseText(Text)==ELDBlueprintParseError::None);

    FLDBlueprintRecord Ignored;
    FString Detail;
    TArray<uint8> Oversize=Bytes;
    Oversize.SetNumZeroed(int32(LDBuildBlueprint::MaxFileBytes)+1);
    TestTrue(TEXT("More than 2 MiB is refused before parsing"),LDBuildBlueprint::Parse(Oversize,LDBuildBlueprint::MaxFileBytes,Ignored,Detail)==ELDBlueprintParseError::TooLarge);
    TArray<uint8> Invalid=Bytes;
    Invalid[20]=0xFF;
    TestTrue(TEXT("An invalid UTF-8 byte is refused"),LDBuildBlueprint::Parse(Invalid,LDBuildBlueprint::MaxFileBytes,Ignored,Detail)==ELDBlueprintParseError::NotUtf8);
    TArray<uint8> Overlong=Bytes;
    Overlong[20]=0xC0;Overlong[21]=0xA2;
    TestTrue(TEXT("An overlong UTF-8 form is refused"),LDBuildBlueprint::Parse(Overlong,LDBuildBlueprint::MaxFileBytes,Ignored,Detail)==ELDBlueprintParseError::NotUtf8);
    TestTrue(TEXT("Text that is no JSON is refused"),BlueprintParseText(TEXT("lockdown build"))==ELDBlueprintParseError::NotJson);
    TestTrue(TEXT("Another schema is refused"),BlueprintParseText(Text.Replace(TEXT("lockdown.build-blueprint"),TEXT("lockdown.other-record")))==ELDBlueprintParseError::WrongSchema);
    TestTrue(TEXT("An unknown schema version is refused, not guessed"),
        BlueprintParseText(Text.Replace(TEXT("\"schema_version\": 1"),TEXT("\"schema_version\": 2")))==ELDBlueprintParseError::UnknownVersion);
    TestTrue(TEXT("A missing required field is refused"),BlueprintParseText(Text.Replace(TEXT("\"player\":"),TEXT("\"players\":")))==ELDBlueprintParseError::MissingField);
    TestTrue(TEXT("A slot outside Q/W/E is refused"),BlueprintParseText(Text.Replace(TEXT("\"slot\": \"Q\""),TEXT("\"slot\": \"X\"")))==ELDBlueprintParseError::BadField);
    FString ZeroFilled=Text;
    const int32 FirstNull=ZeroFilled.Find(TEXT("\"value\": null"));
    if(TestTrue(TEXT("The sample has a fact that was not collected"),FirstNull!=INDEX_NONE))
    {
        ZeroFilled=ZeroFilled.Left(FirstNull)+TEXT("\"value\": 0")+ZeroFilled.Mid(FirstNull+FCString::Strlen(TEXT("\"value\": null")));
        TestTrue(TEXT("A not-collected fact filled with 0 is refused"),BlueprintParseText(ZeroFilled)==ELDBlueprintParseError::BadField);
    }
    TestTrue(TEXT("An unknown origin is refused"),BlueprintParseText(Text.Replace(TEXT("\"origin\": \"auto\""),TEXT("\"origin\": \"server\"")))==ELDBlueprintParseError::BadField);
    TestTrue(TEXT("An unknown field is refused"),
        BlueprintParseText(Text.Replace(TEXT("\"complete\": true"),TEXT("\"complete\": true,\n  \"grant\": true")))==ELDBlueprintParseError::BadField);

    // Notices against the local catalog: another root and an id this catalog no longer has are both stated.
    FLDDemoContentDefinitions Local;
    FLDDemoStatDefinition& Might=Local.Stats.AddDefaulted_GetRef();Might.Id=TEXT("might");
    TArray<FString> Notices;
    LDBuildBlueprint::Notices(Record,&Local,TEXT("0.10.0"),Notices);
    TestTrue(TEXT("Another catalog root is an explicit notice"),Notices.ContainsByPredicate([](const FString& Line){return Line.Contains(TEXT("目录不一致"));}));
    TestTrue(TEXT("A removed active is an explicit stale-install notice"),Notices.ContainsByPredicate([](const FString& Line){return Line.Contains(TEXT("bolt"));}));
    FLDDemoAbilityDefinition& Bolt=Local.Abilities.AddDefaulted_GetRef();Bolt.Id=TEXT("bolt");
    FLDBlueprintRecord Same=Record;Same.CatalogRoot=Local.Identity.Root.ToHex();
    Notices.Reset();
    LDBuildBlueprint::Notices(Same,&Local,TEXT("0.10.0"),Notices);
    TestTrue(TEXT("The same catalog and known ids give no notice"),Notices.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildBlueprintPruneTest,"Lockdown.Rules.BuildBlueprint.Prune",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDBuildBlueprintPruneTest::RunTest(const FString& Parameters)
{
    IFileManager& Files=IFileManager::Get();
    const FString Directory=FPaths::Combine(FPaths::AutomationTransientDir(),TEXT("BuildBlueprint"),FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const LDBuildBlueprintStore::FRetention Unbounded{1000,MAX_int64};
    const FDateTime Base(2026,1,1);
    TArray<FString> Autos;
    for(int32 Index=0;Index<52;++Index)
    {
        FLDBlueprintRecord Record;
        if(!BlueprintSample(*this,FGuid::NewGuid(),Base+FTimespan::FromMinutes(Index),Record)){return false;}
        const LDBuildBlueprintStore::FWriteResult Write=LDBuildBlueprintStore::WriteAuto(Record,Directory,Unbounded);
        if(!TestTrue(FString::Printf(TEXT("Automatic record %d is written (%s)"),Index,*Write.Error),Write.bSaved)){return false;}
        Autos.Add(Write.Path);
    }
    // Files that are never automatic records of this schema, all older than every automatic one.
    TArray<FString> Foreign;
    {
        FLDBlueprintRecord Exported;
        if(!BlueprintSample(*this,FGuid::NewGuid(),Base-FTimespan::FromDays(30),Exported)){return false;}
        Exported.Origin=LDBuildBlueprint::OriginExport;
        TArray<uint8> Bytes;FString Error;
        LDBuildBlueprint::Serialize(Exported,Bytes,Error);
        const FString Named=FPaths::Combine(Directory,LDBuildBlueprintStore::AutoFileName(Exported.MatchId));
        FFileHelper::SaveArrayToFile(Bytes,*Named);
        Foreign.Add(Named);
        FLDBlueprintRecord Future;
        if(!BlueprintSample(*this,FGuid::NewGuid(),Base-FTimespan::FromDays(29),Future)){return false;}
        LDBuildBlueprint::Serialize(Future,Bytes,Error);
        const FString Unknown=FPaths::Combine(Directory,LDBuildBlueprintStore::AutoFileName(Future.MatchId));
        FFileHelper::SaveStringToFile(BlueprintUtf8(Bytes).Replace(TEXT("\"schema_version\": 1"),TEXT("\"schema_version\": 2")),*Unknown,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        Foreign.Add(Unknown);
        const FString Broken=FPaths::Combine(Directory,LDBuildBlueprintStore::AutoFileName(FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        FFileHelper::SaveStringToFile(TEXT("{ not a blueprint"),*Broken);
        Foreign.Add(Broken);
        const FString Notes=FPaths::Combine(Directory,FString(TEXT("notes"))+LDBuildBlueprint::Extension);
        FFileHelper::SaveStringToFile(TEXT("hello"),*Notes);
        Foreign.Add(Notes);
        FLDBlueprintRecord Shared;
        if(!BlueprintSample(*this,FGuid::NewGuid(),Base-FTimespan::FromDays(28),Shared)){return false;}
        const LDBuildBlueprintStore::FWriteResult Export=LDBuildBlueprintStore::Export(Shared,Directory);
        TestTrue(FString::Printf(TEXT("An explicit export is written (%s)"),*Export.Error),Export.bSaved);
        Foreign.Add(Export.Path);
    }
    const LDBuildBlueprintStore::FPruneResult Count=LDBuildBlueprintStore::Prune(Directory);
    TestTrue(TEXT("Only the two oldest automatic records go to keep 50"),Count.Deleted==2&&Count.Kept==50&&Count.Errors.IsEmpty());
    TestTrue(TEXT("The two oldest automatic records are gone"),!Files.FileExists(*Autos[0])&&!Files.FileExists(*Autos[1]));
    TestTrue(TEXT("The newer automatic records stay"),Files.FileExists(*Autos[2])&&Files.FileExists(*Autos.Last()));
    for(const FString& Path:Foreign)
    {
        TestTrue(FString::Printf(TEXT("%s is never pruned"),*FPaths::GetCleanFilename(Path)),Files.FileExists(*Path));
    }
    // The byte bound: keep exactly what the newest ten occupy.
    int64 NewestTen=0;
    for(int32 Index=Autos.Num()-10;Index<Autos.Num();++Index){NewestTen+=Files.FileSize(*Autos[Index]);}
    const LDBuildBlueprintStore::FPruneResult Bytes=LDBuildBlueprintStore::Prune(Directory,{50,NewestTen});
    TestTrue(TEXT("The total size bound deletes the oldest until it holds"),Bytes.Deleted==40&&Bytes.Kept==10&&Bytes.KeptBytes==NewestTen);
    TestTrue(TEXT("The newest ten stay"),Files.FileExists(*Autos[Autos.Num()-10])&&!Files.FileExists(*Autos[Autos.Num()-11]));

    // A complete record above 2 MiB is not truncated: the save fails explicitly and nothing is written or deleted.
    FLDBuildLedger Ledger;
    const FGuid Contender=FGuid::NewGuid();
    for(int32 Index=0;Index<1800;++Index)
    {
        Ledger.Observe(Contender,BlueprintBuild(1,uint64(Index),{{TEXT("might"),Index+1}},{}),int64(Index)*10);
    }
    TArray<FLDBlueprintVersion> Versions;
    FLDBlueprintRecord Large;
    FString Error;
    const bool bLarge=Ledger.Close(Contender,90000,Versions)
        &&LDBuildBlueprint::Compose(BlueprintHeader(FGuid::NewGuid(),Base+FTimespan::FromDays(1),90000),Versions,Large,Error);
    TArray<uint8> LargeBytes;
    if(TestTrue(TEXT("The large record composes"),bLarge)&&TestTrue(TEXT("It is larger than 2 MiB"),
        LDBuildBlueprint::Serialize(Large,LargeBytes,Error)&&LargeBytes.Num()>LDBuildBlueprint::MaxFileBytes))
    {
        const LDBuildBlueprintStore::FWriteResult Write=LDBuildBlueprintStore::WriteAuto(Large,Directory);
        TestTrue(TEXT("The oversized automatic save fails with an explicit reason"),!Write.bSaved&&Write.Error.Contains(TEXT("2 MiB")));
        TestFalse(TEXT("No file of that match is written"),
            Files.FileExists(*FPaths::Combine(Directory,LDBuildBlueprintStore::AutoFileName(Large.MatchId))));
        TestTrue(TEXT("Earlier blueprints are kept"),Files.FileExists(*Autos.Last()));
        FLDBlueprintRecord Viewed;
        FString Detail;
        TestTrue(TEXT("The oversized record still reads for the in-match viewer within the transfer bound"),
            LDBuildBlueprint::Parse(LargeBytes,LDBuildBlueprint::MaxTransferBytes,Viewed,Detail)==ELDBlueprintParseError::None);
    }
    Files.DeleteDirectory(*Directory,false,true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildBlueprintAttributionTest,"Lockdown.Rules.BuildBlueprint.Attribution",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDBuildBlueprintAttributionTest::RunTest(const FString& Parameters)
{
    FLDBuildLedger Ledger;
    const FGuid Contender=FGuid::NewGuid(),Other=FGuid::NewGuid(),Effect=FGuid::NewGuid();
    const int32 Start=Ledger.Observe(Contender,BlueprintBuild(1,0,{},{}),1000);
    TestEqual(TEXT("The first version starts at the match start"),Ledger.Find(Contender)->Last().FromMs,int64(0));
    Ledger.Add(Contender,Start,ELDBlueprintFact::DamageDealt,100);
    Ledger.NoteEffect(Effect,Contender,Start);
    TestEqual(TEXT("A level-only revision stays in the same version"),Ledger.Observe(Contender,BlueprintBuild(2,1,{},{}),2000),Start);
    const int32 Next=Ledger.Observe(Contender,BlueprintBuild(2,2,{{TEXT("might"),1},{TEXT("grit"),2}},{}),3000);
    TestTrue(TEXT("A changed structure starts a new version at its commit"),Next==Start+1&&Ledger.Find(Contender)->Last().FromMs==3000);
    TestEqual(TEXT("The same structure in another order creates no version"),
        Ledger.Observe(Contender,BlueprintBuild(2,3,{{TEXT("grit"),2},{TEXT("might"),1}},{}),4000),Next);
    FLDBuildProjectionInput Dev=BlueprintBuild(2,3,{{TEXT("might"),1},{TEXT("grit"),2}},{});
    Dev.DevBonuses.AddDefaulted();
    TestEqual(TEXT("A DEV layer change creates no version"),Ledger.Observe(Contender,Dev,4500),Next);
    // A hit of an effect released before the change still belongs to the version that released it.
    const int32 Creator=Ledger.EffectVersion(Effect,Contender);
    TestEqual(TEXT("The effect keeps its creation version"),Creator,Start);
    TestEqual(TEXT("Another contender cannot claim the effect"),Ledger.EffectVersion(Effect,Other),int32(INDEX_NONE));
    Ledger.Add(Contender,Creator,ELDBlueprintFact::DamageDealt,50);
    Ledger.Add(Contender,Next,ELDBlueprintFact::Healing,30);
    TArray<FLDBlueprintVersion> Versions;
    TestFalse(TEXT("A close before the last version started is refused"),Ledger.Close(Contender,2500,Versions));
    if(!TestTrue(TEXT("The versions close at the match end"),Ledger.Close(Contender,9000,Versions)&&Versions.Num()==2)){return false;}
    TestTrue(TEXT("Each version keeps its time segment"),Versions[0].FromMs==0&&Versions[0].ToMs==3000&&Versions[1].FromMs==3000&&Versions[1].ToMs==9000);
    TestTrue(TEXT("Revisions and levels per version"),Versions[0].FirstRevision==0&&Versions[0].LastRevision==1&&Versions[0].LevelTo==2
        &&Versions[1].FirstRevision==2&&Versions[1].LastRevision==3&&Versions[1].bDevLayer&&!Versions[0].bDevLayer);
    TestTrue(TEXT("Early damage is never credited to the final build"),
        Versions[0].Facts[int32(ELDBlueprintFact::DamageDealt)].Value==150&&Versions[1].Facts[int32(ELDBlueprintFact::DamageDealt)].Value==0);
    TestTrue(TEXT("Recovery is its own fact"),Versions[1].Facts[int32(ELDBlueprintFact::Healing)].Value==30
        &&Versions[0].Facts[int32(ELDBlueprintFact::Healing)].Value==0);
    for(const ELDBlueprintFact Missing:{ELDBlueprintFact::DebtRepaid,ELDBlueprintFact::DamageReduced,ELDBlueprintFact::Regeneration,
        ELDBlueprintFact::Triggers,ELDBlueprintFact::Rejections})
    {
        TestTrue(FString::Printf(TEXT("%s has no producer and is not collected"),LDBuildBlueprint::FactKey(Missing)),
            !LDBuildBlueprint::IsFactCollected(Missing)&&!Versions[0].Facts[int32(Missing)].bCollected&&!Versions[1].Facts[int32(Missing)].bCollected);
    }
    TestTrue(TEXT("Components are not collected while the build holds none"),!Versions[1].Structure.bComponentsCollected);
    FLDBlueprintRecord Record;
    FString Error;
    TestTrue(TEXT("The record composes from the closed versions"),
        LDBuildBlueprint::Compose(BlueprintHeader(FGuid::NewGuid(),FDateTime(2026,11,10),9000),Versions,Record,Error));
    TestTrue(TEXT("The final build is the last version"),Record.FinalBuild==Versions[1].Structure&&Record.FinalRevision==3
        &&Record.FinalBuild.Stats.Num()==2&&Record.FinalBuild.Stats[0].Stat==TEXT("grit"));
    TestFalse(TEXT("Versions that do not end with the match are refused"),
        LDBuildBlueprint::Compose(BlueprintHeader(FGuid::NewGuid(),FDateTime(2026,11,10),8000),Versions,Record,Error));
    return true;
}
#endif
