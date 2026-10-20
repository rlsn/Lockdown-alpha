#include "Region/LDRegionDefinition.h"
#include "Development/LDCharacterBuild.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreNet.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRegionRules,"Lockdown.Rules.Region",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDRegionRules::RunTest(const FString& Parameters)
{
    const auto* A=LoadObject<ULDRegionDefinition>(nullptr,TEXT("/Game/Lockdown/Generated/Data/DA_CivicRegion.DA_CivicRegion"));const auto* B=LoadObject<ULDRegionDefinition>(nullptr,TEXT("/Game/Lockdown/Generated/Data/DA_AlternateRegion.DA_AlternateRegion"));FString Error;
    if(!TestNotNull(TEXT("Saved main region"),A)||!TestNotNull(TEXT("Saved alternate region"),B)){return false;}
    TestTrue(TEXT("Both graphs validate"),A->Validate(Error)&&B->Validate(Error));TestTrue(TEXT("Alternate changes node count and IDs"),A->Nodes.Num()!=B->Nodes.Num()&&A->CityRegion!=B->CityRegion);TestTrue(TEXT("Alternate changes grid bounds"),A->Grid.CellSize!=B->Grid.CellSize);
    auto* Changed=NewObject<ULDRegionDefinition>();TestTrue(TEXT("Definition imports from its saved companion"),Changed->Import(A->SourceJson,Error));Changed->Routes.Reset();TestFalse(TEXT("Disconnected graph rejected"),Changed->Validate(Error));
    Changed->Import(A->SourceJson,Error);Changed->Nodes[1].Id=Changed->Nodes[0].Id;TestFalse(TEXT("Duplicate opaque region identity rejected"),Changed->Validate(Error));
    for(const auto* D:{A,B}){for(const auto& N:D->Nodes){const int32 Cell=LDVision::Cell(N.Position,D->Grid);TestTrue(TEXT("Regional POIs have encoded cells"),Cell!=INDEX_NONE);TestTrue(TEXT("Cell centre remains within authored region"),LDVision::InWorld(LDVision::Centre(Cell,D->Grid),D->Grid));}}
    FLDWorldView V;V.Generation=1;V.Revision=1;V.Grid=B->Grid;V.Current.Init(0,LDVision::MaskBytes);V.Explored.Init(0,LDVision::MaskBytes);FLDObservedEntity E;E.Id=FGuid::NewGuid();E.Position=B->Node(B->Towns()[0])->Position;E.Kind=ELDObservedKind::WatchControl;V.Entities.Add(E);
    FNetBitWriter Writer(nullptr,65536);bool Ok=true;V.NetSerialize(Writer,nullptr,Ok);FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits());FLDWorldView Read;Read.NetSerialize(Reader,nullptr,Ok);TestTrue(TEXT("Regional poses beyond the migration bounds round trip"),Ok&&Read.Grid.CellSize==B->Grid.CellSize&&Read.Entities[0].Position.Equals(E.Position,.11));
    V.Grid.CellSize=0;FNetBitWriter Bad(nullptr,65536);V.NetSerialize(Bad,nullptr,Ok);TestFalse(TEXT("Invalid grid rejected before payload allocation"),Ok);
    const auto* Catalog=ULDCharacterCatalog::Load();auto* Rehashed=NewObject<ULDCharacterCatalog>();TestFalse(TEXT("Changed paid job budget rejected"),Rehashed->Import(Catalog->SourceJson.Replace(TEXT("\"maxPaidContracts\": 8"),TEXT("\"maxPaidContracts\": 9")),Error));TestFalse(TEXT("Changed sentinel damage rejected"),Rehashed->Import(Catalog->SourceJson.Replace(TEXT("\"damage\": 12"),TEXT("\"damage\": 13")),Error));
    return true;
}
#endif
