#include "UI/LDControlSettings.h"
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDInputMappingsTest,"Lockdown.Rules.ControlMappings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDInputMappingsTest::RunTest(const FString& Parameters)
{
    const FString Slot=TEXT("LockdownControlsProbe_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Settings=ULDControlSettings::Load(Slot);Settings->ResetBindings();FString Error;
    TestTrue(TEXT("Default registry is complete and conflict free"),Settings->Valid());
    TestTrue(TEXT("Native defaults migrate all four skills"),Settings->Key(TEXT("Skill1"))==EKeys::Q&&Settings->Key(TEXT("Skill2"))==EKeys::W&&Settings->Key(TEXT("Skill3"))==EKeys::E&&Settings->Key(TEXT("Skill4"))==EKeys::R);
    TestTrue(TEXT("Context, camera and quick items have distinct keys"),Settings->Key(TEXT("Context"))==EKeys::F&&Settings->Key(TEXT("Recenter"))==EKeys::SpaceBar&&Settings->Key(TEXT("Item1"))==EKeys::One);
    TestFalse(TEXT("Conflict cannot silently replace another command"),Settings->Rebind(TEXT("Skill1"),EKeys::W,Error));
    TestFalse(TEXT("Global Escape cannot be taken by a world action"),Settings->Rebind(TEXT("Skill1"),EKeys::Escape,Error));
    TestFalse(TEXT("Back remains reserved"),Settings->Rebind(TEXT("Back"),EKeys::Z,Error));
    TestFalse(TEXT("Wheel pulse cannot overlap zoom axis"),Settings->Rebind(TEXT("Skill1"),EKeys::MouseScrollUp,Error));
    TestTrue(TEXT("A free keyboard binding is accepted"),Settings->Rebind(TEXT("Skill1"),EKeys::Z,Error));
    TestEqual(TEXT("Displayed key derives from live registry"),Settings->Label(TEXT("Skill1")),EKeys::Z.GetDisplayName().ToString());
    Settings->TextScale=1.25f;Settings->bEdgePan=false;TestTrue(TEXT("Local preferences save"),Settings->Save());
    const auto* Loaded=ULDControlSettings::Load(Slot);
    TestTrue(TEXT("Actual SaveGame load preserves local mapping and settings"),Loaded->Key(TEXT("Skill1"))==EKeys::Z&&Loaded->TextScale==1.25f&&!Loaded->bEdgePan);
    Settings->ResetBindings();TestTrue(TEXT("Reset restores keys without resetting other preferences"),Settings->Key(TEXT("Skill1"))==EKeys::Q&&Settings->TextScale==1.25f&&!Settings->bEdgePan);
    TestTrue(TEXT("Isolated preferences fixture is removed"),UGameplayStatics::DeleteGameInSlot(Slot,0));
    return true;
}
#endif
