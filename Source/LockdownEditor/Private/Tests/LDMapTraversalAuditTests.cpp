#include "LDSceneAuthoringLibrary.h"
#include "AssetCompilingManager.h"
#include "Dom/JsonObject.h"
#include "FileHelpers.h"
#include "Framework/LDDemoEntry.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMapTraversalAuditTest,"Lockdown.Rules.StaticScene.MapTraversal",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMapTraversalAuditTest::RunTest(const FString& Parameters)
{
    UWorld* World=UEditorLoadingAndSavingUtils::LoadMap(
        FPackageName::LongPackageNameToFilename(LDDemoEntry::WorldMap,FPackageName::GetMapPackageExtension()));
    if(!TestNotNull(TEXT("Saved playable map"),World)){return false;}
    ON_SCOPE_EXIT{UEditorLoadingAndSavingUtils::NewBlankMap(false);};
    FlushAsyncLoading();FAssetCompilingManager::Get().FinishAllCompilation();
    // Rebuild the derived navigation in this disposable editor world after applying native tag policy.
    // The saved level is never written by the test.
    const FString BakeError=ULDSceneAuthoringLibrary::NavigationBakeError(World);
    if(!TestTrue(TEXT("Native navigation rebuild completes: ")+BakeError,BakeError.IsEmpty())){return false;}
    const FString Text=ULDSceneAuthoringLibrary::GameplayTraversalAudit(World,200);
    const FString Folder=FPaths::Combine(FPaths::ProjectDir(),TEXT("Artifacts/MapTraversalAudit"));
    IFileManager::Get().MakeDirectory(*Folder,true);
    FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Folder,TEXT("Native.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TSharedPtr<FJsonObject> Report;
    if(!TestTrue(TEXT("Native traversal report parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Report)&&Report.IsValid())){return false;}
    if(!TestTrue(TEXT("Native navigation and collision are ready"),Report->GetBoolField(TEXT("ready")))){AddError(Text);return false;}
    TestEqual(TEXT("All 200 deterministic standable pairs have complete native paths"),int32(Report->GetNumberField(TEXT("path_pairs_passed"))),200);
    TestTrue(TEXT("Path samples use physically standable native capsule feet"),Report->GetNumberField(TEXT("standable_nav_samples"))>=2);
    for(const TCHAR* Field:{TEXT("blocked_narrow_passages"),TEXT("curb_candidates"),
        TEXT("airwall_violations"),TEXT("hidden_collision"),TEXT("unreadable_visual_meshes"),TEXT("pocket_path_errors"),TEXT("coastal_sight_failures")})
    {TestEqual(FString(TEXT("No native map violations: "))+Field,Report->GetArrayField(Field).Num(),0);}
    AddInfo(FString::Printf(TEXT("Layout diagnostics: %d narrow passages and %d single-exit regions. See MapTraversalAudit/Native.json."),
        Report->GetArrayField(TEXT("narrow_passages")).Num(), Report->GetArrayField(TEXT("single_exit_pockets")).Num()));
    TestTrue(TEXT("All field spawns reach the field capture"),Report->GetBoolField(TEXT("field_spawns_reachable")));
    TestTrue(TEXT("All final entries reach the final capture"),Report->GetBoolField(TEXT("final_spawns_reachable")));
    TestTrue(TEXT("Below-zero playable coastal cells were actually sampled"),Report->GetNumberField(TEXT("negative_coastal_cells"))>0);
    TestTrue(TEXT("Open continuous coastal paths have bidirectional native sight"),Report->GetNumberField(TEXT("coastal_visible_pairs"))>0);
    const FString Placement=ULDSceneAuthoringLibrary::GameplayPlacementAudit(World,true);
    FFileHelper::SaveStringToFile(Placement,*FPaths::Combine(Folder,TEXT("Placements.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TSharedPtr<FJsonObject> Homes;
    if(!TestTrue(TEXT("Camp placement audit parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Placement),Homes)&&Homes.IsValid())){return false;}
    if(!TestTrue(TEXT("Authored camp roster is available"),Homes->GetBoolField(TEXT("ready")))){AddError(Placement);return false;}
    for(const auto& Value:Homes->GetArrayField(TEXT("members")))
    {
        const auto Row=Value->AsObject();const FString Name=Row->GetStringField(TEXT("camp"))+TEXT("/")+Row->GetStringField(TEXT("member"));
        TestTrue(Name+TEXT(" has grounded, clear native placement"),Row->GetBoolField(TEXT("grounded"))&&Row->GetBoolField(TEXT("clear")));
        // Camp AI returns to each authored home. The group centre and field capture are diagnostic route origins.
        if (!Row->GetBoolField(TEXT("centre_path")) || !Row->GetBoolField(TEXT("field_path")))
        {
            const auto& Home = Row->GetArrayField(TEXT("home_cm"));
            AddInfo(FString::Printf(TEXT("%s at (%g, %g, %g): centre route %s, field capture route %s. See MapTraversalAudit/Placements.json."),
                *Name, Home[0]->AsNumber(), Home[1]->AsNumber(), Home[2]->AsNumber(),
                Row->GetBoolField(TEXT("centre_path")) ? TEXT("complete") : TEXT("incomplete"),
                Row->GetBoolField(TEXT("field_path")) ? TEXT("complete") : TEXT("incomplete")));
        }
    }
    return !HasAnyErrors();
}
