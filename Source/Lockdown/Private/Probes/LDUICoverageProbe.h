#pragma once
#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only rendered review captures for the UI coverage list (VISION_AND_UI 表现层覆盖清单). On the frontend map it walks
    // the main menu and its pages (how to play, codex, start page, every settings tab, the exit confirmation); in a solo
    // Listen Host room it walks the Lobby (roster, blueprint viewer, settings), the opening rest, the field HUD, the held
    // scoreboard, the in-match menu and its confirmation, the final field and the result screen with another seat
    // inspected. Every page is shot at 1920x1080 and 1280x720 with 100% and 125% text. Pages are reached through the
    // product's own buttons and DEV test operations; the images are for a person to look at, the report only proves the
    // run reached each page. Inert unless -LDUICoverageProbe is present; -LDUICoverageOutput names the evidence folder.
    void StartUICoverageProbe(ALDPlayerController& Controller);
}
#endif
