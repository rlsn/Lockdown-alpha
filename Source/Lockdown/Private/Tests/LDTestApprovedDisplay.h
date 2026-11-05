#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
class FAutomationTestBase;

// Approved display names for rule tests (#111): read from the isolated LDDemoContentFixtures approved_base that
// RunTests exports, so a test compares against the catalog's entry names instead of repeating them. No live-data fallback.
namespace LDTestApprovedDisplay
{
// Name text by display id; false, with a test error, when the fixture is missing or invalid.
bool Names(FAutomationTestBase& Test,TMap<FString,FString>& Out);
}
#endif
