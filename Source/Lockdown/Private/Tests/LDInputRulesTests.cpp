#include "UI/LDControlSettings.h"
#include "Framework/LDCommandLiveness.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDInputMappingsTest,"Lockdown.Rules.ControlMappings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDInputMappingsTest::RunTest(const FString& Parameters)
{
    const auto Storage=FLDLocalSettingsStorage::Automation(FGuid::NewGuid());
    const auto Initial=ULDControlSettings::LoadLocalSettings(Storage);
    auto* Settings=Initial.Settings;
    if(!TestNotNull(TEXT("Missing isolated preferences yield explicit current defaults"),Settings)){return false;}
    TestTrue(TEXT("First run is Missing, never Loaded"),Initial.Status==ELDLocalSettingsLoadStatus::Missing);
    Settings->ResetBindings();FString Error;
    TestTrue(TEXT("Default registry is complete and conflict free"),Settings->Valid());
    TestTrue(TEXT("Demo registry uses Q/W abilities, 1/2 equipment actives and the D dash"),Settings->Key(TEXT("Skill1"))==EKeys::Q&&Settings->Key(TEXT("Skill2"))==EKeys::W&&Settings->Key(TEXT("Item1"))==EKeys::One&&Settings->Key(TEXT("Item2"))==EKeys::Two&&Settings->Key(TEXT("Dash"))==EKeys::D);
    TestTrue(TEXT("The dash can be rebound to a free key"),Settings->Rebind(TEXT("Dash"),EKeys::F,Error)&&Settings->Key(TEXT("Dash"))==EKeys::F&&Settings->Rebind(TEXT("Dash"),EKeys::D,Error));
    TestTrue(TEXT("Retired keys are absent; renamed and camera bindings remain"),!Settings->Bindings.Contains(TEXT("Context"))&&!Settings->Bindings.Contains(TEXT("Skill3"))&&!Settings->Bindings.Contains(TEXT("Item3"))&&!Settings->Bindings.Contains(TEXT("Sprint"))&&!Settings->Bindings.Contains(TEXT("Help"))&&Settings->Key(TEXT("AttackMove"))==EKeys::A&&Settings->Key(TEXT("Attributes"))==EKeys::P&&Settings->Key(TEXT("Recenter"))==EKeys::SpaceBar);
    TestFalse(TEXT("Conflict cannot silently replace another command"),Settings->Rebind(TEXT("Skill1"),EKeys::W,Error));
    TestFalse(TEXT("Global Escape cannot be taken by a world action"),Settings->Rebind(TEXT("Skill1"),EKeys::Escape,Error));
    TestFalse(TEXT("Back remains reserved"),Settings->Rebind(TEXT("Back"),EKeys::Z,Error));
    TestFalse(TEXT("Wheel pulse cannot overlap zoom axis"),Settings->Rebind(TEXT("Skill1"),EKeys::MouseScrollUp,Error));
    TestTrue(TEXT("A free keyboard binding is accepted"),Settings->Rebind(TEXT("Skill1"),EKeys::Z,Error));
    TestEqual(TEXT("Displayed key derives from live registry"),Settings->Label(TEXT("Skill1")),EKeys::Z.GetDisplayName().ToString());
    Settings->Bindings.Add(TEXT("Context"),EKeys::F);
    TestFalse(TEXT("Unknown action in a raw map cannot resolve"),Settings->Key(TEXT("Context")).IsValid());
    TestTrue(TEXT("Unknown action is rejected, not stripped and saved"),Settings->SaveLocalSettings().Status==ELDLocalSettingsSaveStatus::Invalid);
    Settings->Bindings.Remove(TEXT("Context"));
    Settings->TextScale=1.25f;Settings->bEdgePan=false;TestTrue(TEXT("Local preferences save and read back"),Settings->SaveLocalSettings().IsSaved());
    const auto Reload=ULDControlSettings::LoadLocalSettings(Storage);
    const auto* Loaded=Reload.Settings;
    TestTrue(TEXT("Disk load is explicit Loaded"),Reload.Status==ELDLocalSettingsLoadStatus::Loaded);
    TestTrue(TEXT("Actual disk load preserves local mapping and preferences"),Loaded&&Loaded->Key(TEXT("Skill1"))==EKeys::Z&&Loaded->TextScale==1.25f&&!Loaded->bEdgePan);
    Settings->ResetBindings();TestTrue(TEXT("Reset restores keys without resetting other preferences"),Settings->Key(TEXT("Skill1"))==EKeys::Q&&Settings->TextScale==1.25f&&!Settings->bEdgePan);
    TestTrue(TEXT("Isolated preferences fixture is removed"),IFileManager::Get().Delete(*Storage.Filename(),false,false,true));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDIssuedLivenessTest,"Lockdown.Rules.IssuedCommandLiveness",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDIssuedLivenessTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("A351.8ms rendered frame gap is live"),LDCommandLiveness::Fresh(10.3518,10));
    TestTrue(TEXT("A transient one-second stall is live"),LDCommandLiveness::Fresh(11,10));
    TestTrue(TEXT("Before the two-second edge"),LDCommandLiveness::Fresh(11.9999,10));
    TestFalse(TEXT("Expiry is strict at two seconds"),LDCommandLiveness::Fresh(12,10));
    TestFalse(TEXT("Future heartbeat cannot extend liveness"),LDCommandLiveness::Fresh(10,11));
    return true;
}
#endif
