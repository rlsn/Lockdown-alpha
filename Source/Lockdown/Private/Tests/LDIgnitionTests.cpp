#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Combat/LDEnvironmentRules.h"
#include "World/LDWorldObjects.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDIgnitionRules,"Lockdown.Rules.B91Ignition",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDIgnitionRules::RunTest(const FString&)
{
    LDEnvironment::FCell Oil;Oil.Oil.Until=10000;LDEnvironment::FCommand Q;Q.Input=LDEnvironment::EInput::Fire;Q.State.Created=1000;Q.State.Until=4000;Q.State.Life=FGuid::NewGuid();Q.State.Owner=Q.State.BudgetOwner=FGuid::NewGuid();Q.State.Cast=FGuid::NewGuid();Q.FireDuration=3000;
    TestTrue(TEXT("Socket ignition converts existing Oil"),LDEnvironment::Transition(Oil,Q,LDEnvironment::Stone,1000));TestEqual(TEXT("Torch Oil fire lasts exactly three seconds"),Oil.Burning.Until,int64(4000));TestEqual(TEXT("Oil is consumed without recursive extra reactions"),Oil.Mask(1000),uint8(4));TestTrue(TEXT("Original owner and life attribution retained"),Oil.Burning.Owner==Q.State.Owner&&Oil.Burning.Life==Q.State.Life);
    LDEnvironment::FCell Legacy;Legacy.Oil.Until=10000;Q.FireDuration=0;LDEnvironment::Transition(Legacy,Q,LDEnvironment::Stone,1000);TestEqual(TEXT("Existing non-Torch Oil reaction remains six seconds"),Legacy.Burning.Until,int64(7000));
    TestEqual(TEXT("Existing world verbs retain their wire values"),uint8(ELDObjectVerb::Chop),uint8(16));TestEqual(TEXT("Ignition adds one explicit item channel"),uint8(ELDObjectVerb::Ignite),uint8(17));return true;
}
#endif
