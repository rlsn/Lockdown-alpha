#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only rendered capture of the build ring (构筑气场环) in a solo Listen Host room: the opening rest is skipped, a DEV
    // XP grant brings the queued offers, the probe takes some of them through the ring's own command owners, then shoots
    // the open ring (offer row, hover links, hand selection, stat preview), the closed state, the reveal keyframes and,
    // in the first mid-match rest, the shop wing, at 1920x1080 and 1280x720 with 100% and 125% text. Inert unless
    // -LDBuildRingProbe is present; -LDBuildRingOutput names the evidence folder.
    void StartBuildRingProbe(ALDPlayerController& Controller);
}
#endif
