#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only rendered capture of the vector cast in a solo Listen Host room: the cordon tape (Q) and the desk fan (W) are
    // learned through the DEV test session, then each case presses the key with the pointer at a ground start, drags the
    // pointer along its angle (or not at all for a tap) and releases, shooting the held preview and the placed shape.
    // Keys go through the player input and the pointer through the navigation pointer hook, so the product's own aim,
    // request and server path run. Inert unless -LDVectorCastProbe is present; -LDVectorCastOutput names the folder.
    void StartVectorCastProbe(ALDPlayerController& Controller);
}
#endif
