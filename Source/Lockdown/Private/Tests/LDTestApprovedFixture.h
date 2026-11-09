#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Development/LDDemoContentCatalog.h"

class FAutomationTestBase;

// The approved_base catalog for rule tests: read from the isolated LDDemoContentFixtures that RunTests exports.
// The first call validates the fixture index and imports approved_base; later calls in the same process share that
// immutable snapshot instead of importing it again (#189). No live-data fallback.
namespace LDTestApprovedFixture
{
// False, with a test error, when the fixture is missing or invalid.
bool Read(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out);
}
#endif
