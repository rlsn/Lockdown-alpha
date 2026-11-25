#pragma once
#include "CoreMinimal.h"

// Full-screen background images behind the title screen and the room lobby. They are imported from
// SourceData\UI\ui_images.json by Scripts\ImportDemoArt.ps1 -Mode ImportUI (LDDemoUIImageImport) into
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
    // The Lobby's seat cards: one front portrait of the hooper per seat colour (lobby_seat_1 .. lobby_seat_8), cut from the
    // rest room by Scripts/CaptureUICoverage.ps1 and Scripts/MakeLobbySeats.py. Seat is 0-based; empty for a seat outside 0..7.
    inline FString SeatId(int32 Seat){return Seat>=0&&Seat<8?FString::Printf(TEXT("lobby_seat_%d"),Seat+1):FString();}
    inline FString SeatPackage(int32 Seat){return Seat>=0&&Seat<8?FString::Printf(TEXT("/Game/Lockdown/Art/UI/T_LobbySeat%d"),Seat+1):FString();}
    inline FString SeatAsset(int32 Seat)
    {return Seat>=0&&Seat<8?FString::Printf(TEXT("/Game/Lockdown/Art/UI/T_LobbySeat%d.T_LobbySeat%d"),Seat+1,Seat+1):FString();}
}
