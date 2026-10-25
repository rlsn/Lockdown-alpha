#include "Misc/AutomationTest.h"
#include "World/LDWorldDirector.h"
#include "Framework/LDGameMode.h"
#include "UObject/UnrealType.h"
#include "Net/DataBunch.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldContractTest,"Lockdown.Rules.WorldContractAndPublicCues",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldContractTest::RunTest(const FString& Parameters)
{
    FString Why;const auto* C=ULDWorldCatalog::Load();if(!TestNotNull(TEXT("Native world asset"),C)){return false;}TestTrue(TEXT("Imported adopted world and authored layout checksum"),C->Validate(Why));auto* Changed=NewObject<ULDWorldCatalog>();TestFalse(TEXT("Edited imported reward source refuses closed checksum"),Changed->Import(C->SourceJson+TEXT(" "),Why));
    TSharedPtr<FJsonObject> D;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(C->SourceJson),D);const auto W=D->GetObjectField(TEXT("world"));TestEqual(TEXT("Three camps"),W->GetObjectField(TEXT("camp"))->GetIntegerField(TEXT("count")),3);TestEqual(TEXT("Twelve match sources each"),W->GetObjectField(TEXT("camp"))->GetIntegerField(TEXT("spawns_each")),12);const auto R=D->GetObjectField(TEXT("rewards"));TestEqual(TEXT("World-only maximum minted gold"),R->GetIntegerField(TEXT("camp_spawns"))*R->GetIntegerField(TEXT("camp_gold"))+R->GetIntegerField(TEXT("tower_count"))*R->GetIntegerField(TEXT("tower_gold"))+R->GetIntegerField(TEXT("boss_gold")),R->GetIntegerField(TEXT("max_new_world_gold")));
    for(const TCHAR* Shrine:{TEXT("ShrineWest"),TEXT("ShrineEast")}){for(const TCHAR* Tower:{TEXT("GrandTowerA"),TEXT("GrandTowerB"),TEXT("GrandTowerC")}){TestTrue(TEXT("Authored shrine recovery outside every tower range"),FVector::Dist2D(C->Position(Shrine),C->Position(Tower))>650);}}
    int32 Facts=0;for(TFieldIterator<FProperty> It(FLDBreachNotice::StaticStruct());It;++It){++Facts;TestTrue(TEXT("Anonymous breach carries only route and endpoints"),TSet<FName>{TEXT("Route"),TEXT("WarningAt"),TEXT("OpenAt"),TEXT("CloseAt")}.Contains(It->GetFName()));}TestEqual(TEXT("Four public breach facts; no owner/position/item identities"),Facts,4);
    FLDWorldView V;V.Generation=1;V.Revision=1;V.Current.Init(0,LDVision::MaskBytes);V.Explored.Init(0,LDVision::MaskBytes);FLDObservedEntity E;E.Id=FGuid::NewGuid();E.Position=FVector(0,0,15);E.Kind=ELDObservedKind::TacticalField;E.Presentation=15;V.Entities.Add(E);FNetBitWriter Writer(nullptr,65536);bool Good=false;V.NetSerialize(Writer,nullptr,Good);FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits());FLDWorldView Read;Read.NetSerialize(Reader,nullptr,Good);TestTrue(TEXT("Observed guardian pulse cue fits unchanged tuple"),Good&&Read.Entities[0].Presentation==15);V.Entities[0].Presentation=16;FNetBitWriter Bad(nullptr,65536);V.NetSerialize(Bad,nullptr,Good);TestFalse(TEXT("Unknown field remains rejected"),Good);
    for(auto Kind:{ELDObservedKind::WorldCreature,ELDObservedKind::WorldStructure,ELDObservedKind::WorldSite}){V.Entities[0].Kind=Kind;V.Entities[0].Presentation=Kind==ELDObservedKind::WorldCreature?1:2;FNetBitWriter Valid(nullptr,65536);V.NetSerialize(Valid,nullptr,Good);TestTrue(TEXT("Bounded public world category"),Good);V.Entities[0].Presentation=3;FNetBitWriter Invalid(nullptr,65536);V.NetSerialize(Invalid,nullptr,Good);TestFalse(TEXT("Unknown world presentation rejected"),Good);}return true;
}
#endif
