#include "World/LDSpawnPlacement.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDUniformSpawnSampleTest, "Lockdown.Rules.DemoSpawn.UnbiasedSample",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDUniformSpawnSampleTest::RunTest(const FString& Parameters)
{
    uint32 Index=999;
    TestFalse(TEXT("Empty pool cannot yield a selection"),LDSpawnPlacement::TryUniformSample(0,0,Index));
    TestEqual(TEXT("Refused mapping has no candidate index"),Index,uint32(0));
    for (uint32 Raw=0; Raw<6; ++Raw)
    {
        TestFalse(TEXT("The six biased-prefix samples are redrawn for ten anchors"),
            LDSpawnPlacement::TryUniformSample(Raw,10,Index));
    }
    uint32 Counts[10]={};
    for (uint32 Raw=6; Raw<100006; ++Raw)
    {
        if (!LDSpawnPlacement::TryUniformSample(Raw,10,Index) || Index>=10)
        { AddError(TEXT("Accepted ten-anchor sample failed.")); return false; }
        ++Counts[Index];
    }
    for (uint32 Count : Counts) { TestEqual(TEXT("Every anchor has the same number of accepted sample preimages"),Count,uint32(10000)); }
    const uint64 Space=uint64(1)<<32;
    for (uint32 Bound : {uint32(1),uint32(2),uint32(3),uint32(7),uint32(10),uint32(13),uint32(256),MAX_uint32})
    {
        const uint32 Rejected=uint32(Space%Bound);
        TestEqual(TEXT("The full accepted RNG space is an exact multiple of the bound"),(Space-Rejected)%Bound,uint64(0));
        TestTrue(TEXT("The first unbiased sample is accepted"),LDSpawnPlacement::TryUniformSample(Rejected,Bound,Index));
        TestTrue(TEXT("The last uint32 sample is accepted"),LDSpawnPlacement::TryUniformSample(MAX_uint32,Bound,Index));
        TestTrue(TEXT("Accepted samples stay inside the complete pool"),Index<Bound);
        if (Rejected) { TestFalse(TEXT("The excluded prefix never uses modulo fallback"),LDSpawnPlacement::TryUniformSample(Rejected-1,Bound,Index)); }
    }
    return true;
}
#endif
