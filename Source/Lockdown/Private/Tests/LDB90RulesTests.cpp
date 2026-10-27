#include "Region/LDMapDefinition.h"
#include "Region/LDMapSurfaceSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB90MapRules,"Lockdown.Rules.B90Map",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB90MapRules::RunTest(const FString& Parameters)
{
    FString Text, Error;
    if(!TestTrue(TEXT("Staged full-map input exists"),FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/map_b90.json"))))) { return false; }
    auto* D=NewObject<ULDMapDefinition>();
    if(!TestTrue(TEXT("Checksum-closed spatial import"),D->Import(Text,Error))) { return false; }
    const FString Hash=D->Checksum;
    TestTrue(TEXT("Repeat import restores rather than appends"),D->Import(Text,Error)&&D->Checksum==Hash&&D->POIs.Num()==96&&D->Spawns.Num()==24&&D->Routes.Num()==117);
    TestFalse(TEXT("Unknown semantic field rejected"),D->Import(Text.Replace(TEXT("\"schema\":9"),TEXT("\"unimplemented_verb\":true,\"schema\":9")),Error));
    TestFalse(TEXT("Changed coordinate units rejected"),D->Import(Text.Replace(TEXT("\"speed\":420"),TEXT("\"speed\":42000")),Error));
    TestTrue(TEXT("Rejected import leaves previous usable definition"),D->Validate(Error));
    D->POIs[1].Id=D->POIs[0].Id; TestFalse(TEXT("Duplicate identity rejected before building"),D->Validate(Error));
    D->Import(Text,Error); D->Routes[0].To=TEXT("missing"); TestFalse(TEXT("Unknown route destination rejected"),D->Validate(Error));
    D->Import(Text,Error); D->Spawns[0].Position.X=118001; TestFalse(TEXT("Spawn beyond playable apron rejected"),D->Validate(Error));
    D->Import(Text,Error);
    TestTrue(TEXT("Dry claim pad is authored height"),FMath::Abs(D->Height(FVector2D(D->Find(TEXT("heart"))->Position))-400)<1);
    TestTrue(TEXT("Permanent river floor is lowland"),FMath::Abs(D->Height(FVector2D(-27000,65000)))<1);
    TestTrue(TEXT("River floor contact"),D->RiverAt(FVector(-27000,65000,0)));
    TestFalse(TEXT("Dry bridge above water has no river contact"),D->RiverAt(FVector(-27000,65000,200)));
    TestFalse(TEXT("Dry Heart circle"),D->RiverAt(D->Find(TEXT("heart"))->Position));
    TestTrue(TEXT("Western Heart inset provides water alternative"),D->RiverAt(D->WetInset));
    for(FName Id:D->Bridges) { TestTrue(TEXT("Bridge target selects fitted dry layer"),D->NavigationTarget(Id).Z>=300&&!D->RiverAt(D->NavigationTarget(Id))); }
    FIntPoint Cell;
    TestTrue(TEXT("Independent full-map cell origin"),ULDMapSurfaceSubsystem::Cell(FVector(-120000,-120000,0),Cell)&&Cell==FIntPoint(0,0));
    TestTrue(TEXT("Negative coordinates retain 2m precision"),ULDMapSurfaceSubsystem::Cell(FVector(-1,-201,0),Cell)&&Cell==FIntPoint(599,598));
    TestTrue(TEXT("Last cell uses full 1200x1200 address space"),ULDMapSurfaceSubsystem::Cell(FVector(119999,119999,0),Cell)&&Cell==FIntPoint(1199,1199));
    TestFalse(TEXT("Upper cell boundary is exclusive"),ULDMapSurfaceSubsystem::Cell(FVector(120000,0,0),Cell));
    TestFalse(TEXT("Lower cell boundary rejects overflow"),ULDMapSurfaceSubsystem::Cell(FVector(-120001,0,0),Cell));
    TestFalse(TEXT("Full-map origin is outside legacy extent assumptions"),ULDMapDefinition::InBounds(FVector(120000,0,0)));
    return true;
}
#endif
