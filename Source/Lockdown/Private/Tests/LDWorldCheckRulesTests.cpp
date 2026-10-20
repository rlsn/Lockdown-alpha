#include "Development/LDCharacterBuild.h"
#include "Development/LDWorldChecks.h"
#include "Misc/AutomationTest.h"
#include "Vision/LDViewTypes.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldCheckRules,"Lockdown.Rules.WorldChecks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldCheckRules::RunTest(const FString& Parameters)
{
    int32 Successes=0;for(int32 Roll=1;Roll<=20;++Roll){Successes+=LDDevelopment::TheftSucceeds(Roll,12,1,false,10,false)?1:0;}
    TestEqual(TEXT("DEX12 rank1 versus WIS10 succeeds on exactly eleven d20 outcomes"),Successes,11);
    TestTrue(TEXT("Out-of-tuning formula fixture: natural one has no forced failure"),LDDevelopment::TheftSucceeds(1,16,3,true,0,false));
    TestFalse(TEXT("Out-of-tuning formula fixture: natural twenty has no forced success"),LDDevelopment::TheftSucceeds(20,8,1,false,40,true));
    TestFalse(TEXT("Negative odd attribute deltas use floor"),LDDevelopment::TheftSucceeds(11,9,1,false,10,false));
    TestFalse(TEXT("Alert adds two to the live resistance"),LDDevelopment::TheftSucceeds(10,12,1,false,10,true));
    TSet<ELDSkill> Services;for(uint8 I=0;I<4;++I){Services.Add(LDCharter::Skill(I));TestTrue(TEXT("Temporary B44 services remain in the current world"),LDVision::InWorld(LDCharter::Position(I)));}
    TestEqual(TEXT("Four distinct public services"),Services.Num(),4);
    auto* Catalog=ULDCharacterCatalog::Load();if(!TestNotNull(TEXT("Saved schema"),Catalog)){return false;}auto* Changed=NewObject<ULDCharacterCatalog>();FString Error;
    TestFalse(TEXT("Rehashed changed retry window rejected"),Changed->Import(Catalog->SourceJson.Replace(TEXT("\"pairWindowSeconds\": 90"),TEXT("\"pairWindowSeconds\": 1")),Error));
    TestFalse(TEXT("Rehashed changed Charter threshold rejected"),Changed->Import(Catalog->SourceJson.Replace(TEXT("\"threshold\": 12"),TEXT("\"threshold\": 1")),Error));
    TestFalse(TEXT("Rehashed changed draw contract rejected"),Changed->Import(Catalog->SourceJson.Replace(TEXT("\"drawOnInvalidOrPreview\": false"),TEXT("\"drawOnInvalidOrPreview\": true")),Error));
    return true;
}
#endif
