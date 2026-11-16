#pragma once
#include "CoreMinimal.h"

class UWorld;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only (-LDDemoArtCapture -LDDemoArtCaptureOutput=<Artifacts dir>): in the Demo map it poses every catalog
    // body and clip, renders them and the decorated field from the game camera angle, records that decorations stay
    // display only (no collision, sight trace or navigation influence) and exits.
    void StartArtCaptureProbe(UWorld& World);
}
#endif
