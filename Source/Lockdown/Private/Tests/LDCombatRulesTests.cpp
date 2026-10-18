#include "Combat/LDCombatRules.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatGeometryTest,"Lockdown.Rules.CombatGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDCombatGeometryTest::RunTest(const FString& Parameters)
{
    const FVector O=FVector::ZeroVector,F=FVector::ForwardVector;
    TestTrue(TEXT("Basic exact reach and cone edge"),LDCombat::InCone(O,FRotator(0,40,0).Vector()*96,F,96,40));
    TestFalse(TEXT("Outside reach"),LDCombat::InCone(O,F*96.01,F,96,40));
    TestFalse(TEXT("Outside cone"),LDCombat::InCone(O,FRotator(0,40.01,0).Vector()*80,F,96,40));
    TestTrue(TEXT("Basic overlap legal"),LDCombat::InCone(O,O,F,96,40));
    TestTrue(TEXT("Rear centre"),LDCombat::IsRear(F*-90,O,F));
    TestTrue(TEXT("Rear edge"),LDCombat::IsRear(FRotator(0,120,0).Vector()*90,O,F));
    TestFalse(TEXT("Rear outside"),LDCombat::IsRear(FRotator(0,119.9,0).Vector()*90,O,F));
    TestFalse(TEXT("Assassin overlap illegal"),LDCombat::IsRear(O,O,F));
    TestFalse(TEXT("Quiet preparation incomplete"),LDCombat::Quiet(1,.01,0,false,false));
    TestTrue(TEXT("Exact quiet/exposure expiry"),LDCombat::Quiet(4,3,4,false,false));
    TestFalse(TEXT("Sprint blocks preparation"),LDCombat::Quiet(4,0,0,true,false));
    TestFalse(TEXT("Dash blocks preparation"),LDCombat::Quiet(4,0,0,false,true));
    TestFalse(TEXT("Recent dealt damage alerts target"),LDCombat::TargetUnalert(10,0,6.01));
    TestTrue(TEXT("Four second boundary"),LDCombat::TargetUnalert(10,6,0));
    return true;
}
#endif
