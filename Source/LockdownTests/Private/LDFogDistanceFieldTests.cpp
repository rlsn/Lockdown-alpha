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
    TestEqual(TEXT("Empty coverage is entirely hidden"), Field[64 * LDObservation::CoverageWidth + 64], 0.f);
    Bits.Init(255, LDObservation::CoverageBytes);
    LDFogDistanceField::Build(Bits, 10, 20, Field);
    TestEqual(TEXT("Full coverage is entirely clear"), Field[64 * LDObservation::CoverageWidth + 64], 1.f);
    Bits.Init(0, LDObservation::CoverageBytes);
    const int32 Centre = 64 * LDObservation::CoverageWidth + 64;
    Bits[Centre / 8] |= uint8(1 << (Centre % 8));
    LDFogDistanceField::Build(Bits, 10, 20, Field);
    const auto Cm = [&](int32 I) { return (Field[I] * 2 - 1) * LDFogDistanceField::RangeCm; };
    TestTrue(TEXT("A visible island has a positive interior"), Cm(Centre) > 0);
    TestTrue(TEXT("X distance uses the rectangular cell width"), FMath::Abs(Cm(Centre + 1) + 5.f) < .001f);
    TestTrue(TEXT("Y distance uses the rectangular cell height"), FMath::Abs(Cm(Centre + LDObservation::CoverageWidth) + 15.f) < .001f);
    TestTrue(TEXT("Diagonal distance is Euclidean"), FMath::Abs(Cm(Centre + LDObservation::CoverageWidth + 1) + FMath::Sqrt(500.f) - 5.f) < .001f);
    TestEqual(TEXT("A hidden far corner saturates"), Field[0], 0.f);
    // The shown field eases toward a new target. A blend confined to the cells that differ must leave the same field and
    // the same texture as a blend over every cell, step for step.
    const int32 Width = LDObservation::CoverageWidth, Height = LDObservation::CoverageHeight, Cells = LDObservation::CoverageCells;
    const TArray<float> Target = Field;
    TArray<float> Whole, Part;
    Whole.Init(0.f, Cells);
    Part = Whole;
    TestEqual(TEXT("Identical fields differ nowhere"), LDFogDistanceField::Difference(Target, Target).Area(), 0);
    FIntRect Region = LDFogDistanceField::Difference(Part, Target);
    TestTrue(TEXT("The changed rectangle holds the island and is far smaller than the map"),
        Region.Contains(FIntPoint(64, 64)) && Region.Area() > 0 && Region.Area() < Cells / 4);
    // Both textures start as the settled all-hidden field.
    TArray<uint8> WholeTexture, PartTexture, Pixels;
    WholeTexture.SetNumUninitialized(Cells * 4);
    for (int32 Index = 0; Index < Cells; ++Index)
    {
        WholeTexture[Index * 4] = WholeTexture[Index * 4 + 1] = WholeTexture[Index * 4 + 2] = 0;
        WholeTexture[Index * 4 + 3] = 255;
    }
    PartTexture = WholeTexture;
    int32 Steps = 0;
    for (; Steps < 200 && Region.Area() > 0; ++Steps)
    {
        LDFogDistanceField::Blend(Target, Whole, FIntRect(0, 0, Width, Height), .25f, WholeTexture.GetData());
        Pixels.SetNumUninitialized(Region.Area() * 4);
        const FIntRect Next = LDFogDistanceField::Blend(Target, Part, Region, .25f, Pixels.GetData());
        for (int32 Y = 0; Y < Region.Height(); ++Y)
        {
            FMemory::Memcpy(PartTexture.GetData() + ((Region.Min.Y + Y) * Width + Region.Min.X) * 4,
                Pixels.GetData() + Y * Region.Width() * 4, Region.Width() * 4);
        }
        TestTrue(TEXT("The cells still in motion never leave the rectangle that held them"),
            Next.Area() == 0 || (Region.Contains(Next.Min) && Next.Max.X <= Region.Max.X && Next.Max.Y <= Region.Max.Y));
        Region = Next;
    }
    TestTrue(TEXT("The confined blend settles"), Steps > 1 && Steps < 200 && Region.Area() == 0);
    TestTrue(TEXT("The confined blend ends on the target"), Part == Target);
    TestTrue(TEXT("Both blends leave the same field"), Whole == Part);
    TestTrue(TEXT("Both blends leave the same texture"), WholeTexture == PartTexture);
    return true;
}
#endif
