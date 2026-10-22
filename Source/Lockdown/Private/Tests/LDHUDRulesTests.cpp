#include "UI/LDHUDModel.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDMaskTest,"Lockdown.Rules.HUDObservationBoundary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDMaskTest::RunTest(const FString& Parameters)
{
    FLDOwnerState Own;Own.Build=LDDevelopment::StartingBuild(1);Own.Gold=98765;Own.Hp=12.34;
    FLDObservedEntity Observed;Observed.Kind=ELDObservedKind::Body;Observed.ActionCue=6;
    const FString Mask=LDHUD::Inspector(Own,&Observed,0);
    TestTrue(TEXT("Other inspector explicitly marks every private category Unknown"),Mask.Contains(TEXT("HP / level / class: Unknown"))&&Mask.Contains(TEXT("Attributes / skills / ranks: Unknown"))&&Mask.Contains(TEXT("Gold / inventory: Unknown")));
    TestTrue(TEXT("Other inspector can show its visible cast cue"),Mask.Contains(TEXT("Spark cast")));
    Own.Build.ClassId=2;Own.Gold=123456;Own.Hp=90;
    TestEqual(TEXT("Other inspection is independent of private own values"),LDHUD::Inspector(Own,&Observed,0),Mask);
    TestFalse(TEXT("Other inspector cannot display own wallet as if it belonged to the stranger"),Mask.Contains(TEXT("98765")));
    TestTrue(TEXT("Self inspector uses actual own class"),LDHUD::Inspector(Own,nullptr,0).Contains(TEXT("Scholar")));
    const FLDVisionGrid Grid;
    const FVector World(-1700,-1000,200);const auto Fraction=LDHUD::MapPoint(World,Grid);const auto Back=LDHUD::MapWorld(Fraction,Grid);
    TestTrue(TEXT("Map conversion round trips XY and deliberately grants no height or dynamic target"),FVector::Dist2D(World,Back)<.001&&Back.Z==0);
    TestTrue(TEXT("Map edge clamp stays inside authored world"),LDVision::InWorld(LDHUD::MapWorld(FVector2D(5,-3),Grid),Grid));
    TestTrue(TEXT("Tooltip describes the own skill outcome and retained cooldown"),LDHUD::Tooltip(Own.Build,ELDSkill::Spark).Contains(TEXT("Cooldown 6s")));
    return true;
}
#endif
