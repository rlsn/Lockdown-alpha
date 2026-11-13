#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Vision/LDFogDistanceField.h"
#include "Vision/LDViewTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFogDistanceFieldTest, "Lockdown.Rules.Vision.FogDistanceField",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDFogDistanceFieldTest::RunTest(const FString&)
{
    TArray<uint8> Bits;
    TArray<float> Field;
    Bits.Init(0, LDObservation::CoverageBytes);
    LDFogDistanceField::Build(Bits, 10, 20, Field);
    TestEqual(TEXT("Empty coverage is entirely hidden"), Field[64 * 128 + 64], 0.f);
    Bits.Init(255, LDObservation::CoverageBytes);
    LDFogDistanceField::Build(Bits, 10, 20, Field);
    TestEqual(TEXT("Full coverage is entirely clear"), Field[64 * 128 + 64], 1.f);
    Bits.Init(0, LDObservation::CoverageBytes);
    const int32 Centre = 64 * 128 + 64;
    Bits[Centre / 8] |= uint8(1 << (Centre % 8));
    LDFogDistanceField::Build(Bits, 10, 20, Field);
    const auto Cm = [&](int32 I) { return (Field[I] * 2 - 1) * LDFogDistanceField::RangeCm; };
    TestTrue(TEXT("A visible island has a positive interior"), Cm(Centre) > 0);
    TestTrue(TEXT("X distance uses the rectangular cell width"), FMath::Abs(Cm(Centre + 1) + 5.f) < .001f);
    TestTrue(TEXT("Y distance uses the rectangular cell height"), FMath::Abs(Cm(Centre + 128) + 15.f) < .001f);
    TestTrue(TEXT("Diagonal distance is Euclidean"), FMath::Abs(Cm(Centre + 129) + FMath::Sqrt(500.f) - 5.f) < .001f);
    TestEqual(TEXT("A hidden far corner saturates"), Field[0], 0.f);
    return true;
}
#endif
