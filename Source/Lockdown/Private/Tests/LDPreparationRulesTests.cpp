#include "Development/LDCharacterBuild.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreNet.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPreparationWireTest,"Lockdown.Rules.PreparationWire",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDPreparationWireTest::RunTest(const FString& Parameters)
{
    auto B=LDDevelopment::StartingBuild(1);auto P=LDDevelopment::PlanFrom(B);P.ExpectedRevision=39;P.Attributes[0]=12;P.Ranks[3]=2;P.Slots[3]=ELDSkill::Spark;
    FNetBitWriter Writer(nullptr,4096);bool Good=false;P.NetSerialize(Writer,nullptr,Good);
    TestTrue(TEXT("Plan has a constant bounded wire representation"),Good&&Writer.GetNumBytes()<32);
    FLDBuildPlan Read;FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits());Read.NetSerialize(Reader,nullptr,Good);
    TestTrue(TEXT("Plan round trip preserves draft and revision"),Good&&Read.ExpectedRevision==39&&Read.Attributes==P.Attributes&&Read.Ranks==P.Ranks&&Read.Slots==P.Slots);
    P.Attributes.Add(10);FNetBitWriter Invalid(nullptr,4096);P.NetSerialize(Invalid,nullptr,Good);TestFalse(TEXT("Oversized draft rejected before unbounded serialization"),Good);
    FLDBuildPlan Empty;FNetBitWriter EmptyWriter(nullptr,4096);Empty.NetSerialize(EmptyWriter,nullptr,Good);
    FNetBitReader EmptyReader(nullptr,EmptyWriter.GetData(),EmptyWriter.GetNumBits());Read.NetSerialize(EmptyReader,nullptr,Good);
    TestTrue(TEXT("Non-preparation requests carry no arbitrary draft arrays"),Good&&Read.Attributes.IsEmpty()&&Read.Ranks.IsEmpty()&&Read.Slots.IsEmpty());
    const auto* Catalog=ULDCharacterCatalog::Load();if(!TestNotNull(TEXT("Saved catalog"),Catalog)){return false;}
    auto* Changed=NewObject<ULDCharacterCatalog>();FString Error;
    TestFalse(TEXT("Changed book price rejected even with recomputed checksum"),Changed->Import(Catalog->SourceJson.Replace(TEXT("\"priceGold\": 40"),TEXT("\"priceGold\": 1")),Error));
    TestFalse(TEXT("Changed preparation duration rejected"),Changed->Import(Catalog->SourceJson.Replace(TEXT("\"channelSeconds\": 2"),TEXT("\"channelSeconds\": 0.1")),Error));
    return true;
}
#endif
