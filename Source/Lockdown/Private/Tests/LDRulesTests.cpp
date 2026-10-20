#include "Core/LDRules.h"
#include "Misc/AutomationTest.h"
#include "Objectives/LDObjectiveRules.h"
#include "Development/LDCharacterBuild.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDConversionTest, "Lockdown.Rules.SpatialConversion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDConversionTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Heart at origin"), LDRules::WorldFromBaseline(FVector2D(1200, 800)), FVector::ZeroVector);
    TestEqual(TEXT("Map upper left"), LDRules::WorldFromBaseline(FVector2D::ZeroVector), FVector(-2400, 1600, 0));
    TestEqual(TEXT("Map lower right"), LDRules::WorldFromBaseline(FVector2D(2400, 1600)), FVector(2400, -1600, 0));
    const FVector2D Position(560, 400);
    TestEqual(TEXT("Position round trip"), LDRules::BaselineFromWorld(LDRules::WorldFromBaseline(Position)), Position);
    TestEqual(TEXT("Squared range scales by four"), LDRules::SquaredDistanceFromBaseline(48.0 * 48.0), 96.0 * 96.0);
    TestEqual(TEXT("Elevation is independent of planar scale"), LDRules::WorldFromBaseline(Position, 1).Z, 200.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDeathCostTest, "Lockdown.Rules.DeathCost",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDeathCostTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Level one opening return"), LDRules::DeathCost(1, 0, 0).DelaySeconds, 20.0);
    TestEqual(TEXT("Level two at 240 seconds"), LDRules::DeathCost(2, 0, 240).DelaySeconds, 40.0);
    TestEqual(TEXT("Level three at showdown"), LDRules::DeathCost(3, 101, 480).DelaySeconds, 105.0);
    TestEqual(TEXT("101 gold loses 31"), LDRules::DeathCost(3, 101, 480).LostGold, int64(31));
    TestEqual(TEXT("Ceiling at one gold"), LDRules::DeathCost(1, 1, 0).LostGold, int64(1));
    TestEqual(TEXT("Just before showdown"), LDRules::DeathCost(3, 0, 479.999).DelaySeconds, 55.0);
    TestEqual(TEXT("Overflow-safe loss"), LDRules::DeathCost(3, MAX_int64, 0).LostGold, int64(2767011611056432743LL));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDObjectiveTest, "Lockdown.Rules.ObjectiveBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDObjectiveTest::RunTest(const FString& Parameters)
{
    const auto* Catalog=ULDCharacterCatalog::Load();auto* Changed=NewObject<ULDCharacterCatalog>();FString Error;
    if(!TestNotNull(TEXT("Saved fabrication schema"),Catalog)){return false;}
    TestFalse(TEXT("Rehashed changed fabrication price rejected"),Changed->Import(Catalog->SourceJson.Replace(TEXT("\"goldCost\": 100"),TEXT("\"goldCost\": 1")),Error));
    TestFalse(TEXT("Rehashed normal seeded Seal rejected"),Changed->Import(Catalog->SourceJson.Replace(TEXT("\"normalSeeded\": false"),TEXT("\"normalSeeded\": true")),Error));
    TestTrue(TEXT("Exact fabrication resources suffice"),LDObjective::CanFabricate(true,4,100,false,false));
    TestFalse(TEXT("Witness required for fabrication"),LDObjective::CanFabricate(false,4,100,false,false));
    TestFalse(TEXT("Credits cannot be borrowed"),LDObjective::CanFabricate(true,3,100,false,false));
    TestFalse(TEXT("Gold cannot be borrowed"),LDObjective::CanFabricate(true,4,99,false,false));
    TestFalse(TEXT("Prior craft cannot repeat"),LDObjective::CanFabricate(true,8,200,true,false));
    TestFalse(TEXT("Capacity one is independent of resources"),LDObjective::CanFabricate(true,8,200,false,true));
    for (uint8 Flags = 0; Flags < 16; ++Flags)
    {
        TestEqual(TEXT("Ordinary route requires every defence down"), LDRules::HasHeartAccess(false, Flags), Flags == 0);
        TestTrue(TEXT("Seal bypass"), LDRules::HasHeartAccess(true, Flags));
    }
    TestTrue(TEXT("Late sample may commit deadline-equal claim"), LDRules::CanCompleteClaim(720.05, 720, true, false));
    TestFalse(TEXT("Late claim cannot win"), LDRules::CanCompleteClaim(721, 720.01, true, false));
    TestFalse(TEXT("Completion batch damage interrupts"), LDRules::CanCompleteClaim(720, 720, false, false));
    TestFalse(TEXT("Terminal state cannot finish twice"), LDRules::CanCompleteClaim(720, 720, true, true));
    TestFalse(TEXT("Not due"), LDRules::CanCompleteClaim(9.99, 10, true, false));
    TestEqual(TEXT("First XP threshold"), LDRules::LevelFromXp(30), 2);
    TestEqual(TEXT("Second XP threshold"), LDRules::LevelFromXp(80), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDViewOrderTest, "Lockdown.Rules.ViewOrdering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDViewOrderTest::RunTest(const FString& Parameters)
{
    LDRules::FViewCursor Cursor;
    TestTrue(TEXT("First view"), Cursor.Accept(1, 10));
    TestTrue(TEXT("Concealment advances revision"), Cursor.Accept(1, 11));
    TestFalse(TEXT("Old full view cannot resurrect hidden actor"), Cursor.Accept(1, 10));
    TestTrue(TEXT("Reappearance"), Cursor.Accept(1, 12));
    TestFalse(TEXT("Delayed concealment cannot remove reappearance"), Cursor.Accept(1, 11));
    TestFalse(TEXT("Duplicate"), Cursor.Accept(1, 12));
    TestTrue(TEXT("Fresh match resets revision"), Cursor.Accept(2, 1));
    TestFalse(TEXT("Old match cannot mutate fresh match"), Cursor.Accept(1, 100));
    return true;
}
#endif
