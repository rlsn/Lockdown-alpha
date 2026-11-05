#pragma once
#include "CoreMinimal.h"

// Full-screen background images behind the title screen and the room lobby. They are imported from
// SourceData\UI\ui_images.json by Scripts\ImportDemoArt.ps1 -Mode ImportBackgrounds (LDDemoUIImageImport) into
// /Game/Lockdown/Art/UI, which is always cooked. The UI root loads them by these paths; a missing one is logged as
// LD_UI_BACKGROUND_MISSING and the page shows its plain backing instead of the picture.
namespace LDUIBackgrounds
{
    inline constexpr TCHAR TitleId[] = TEXT("title_background");
    inline constexpr TCHAR LobbyId[] = TEXT("lobby_background");
    inline constexpr TCHAR TitlePackage[] = TEXT("/Game/Lockdown/Art/UI/T_TitleBackground");
    inline constexpr TCHAR LobbyPackage[] = TEXT("/Game/Lockdown/Art/UI/T_LobbyBackground");
    inline constexpr TCHAR TitleAsset[] = TEXT("/Game/Lockdown/Art/UI/T_TitleBackground.T_TitleBackground");
    inline constexpr TCHAR LobbyAsset[] = TEXT("/Game/Lockdown/Art/UI/T_LobbyBackground.T_LobbyBackground");
}
