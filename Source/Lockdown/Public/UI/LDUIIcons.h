#pragma once
#include "CoreMinimal.h"

class UImage;
class UTexture2D;

// Skill, passive, attribute, opening and status icons (#85). The 64 px PNGs of SourceData\UI\Icons are listed with their
// SHA-256 in SourceData\UI\ui_icons.json and imported by Scripts\ImportDemoArt.ps1 -Mode ImportUI (LDDemoUIImageImport) into
// /Game/Lockdown/Art/UI/Icons/T_<IconId>, which is always cooked. A content display row names its icon by IconId
// (icon_<contentId>); the dash and the two recoveries have no display row and use the ids below.
// An id without an imported texture is logged once as LD_UI_ICON_MISSING; the widget then shows the name's first character
// on a plain square, so a missing import is visible instead of a blank slot.
namespace LDUIIcons
{
    inline constexpr TCHAR Folder[] = TEXT("/Game/Lockdown/Art/UI/Icons");
    inline constexpr TCHAR Dash[] = TEXT("icon_dash");
    inline constexpr TCHAR OutOfCombatRegen[] = TEXT("icon_out_of_combat_regen");
    inline constexpr TCHAR DormRegen[] = TEXT("icon_dorm_regen");

    // The texture package of an icon id ("" for an id that is not a lower-case identifier).
    LOCKDOWN_API FString PackageFor(const FString& IconId);
    // The imported texture of IconId; null (logged once per id) when the id is malformed or the texture is not imported.
    LOCKDOWN_API UTexture2D* Find(const FString& IconId);
    // Shows IconId's texture on Image with rounded corners and returns true; collapses Image and returns false for an
    // empty id or a missing texture. Tint multiplies the picture (greys a blocked slot).
    LOCKDOWN_API bool Apply(UImage* Image, const FString& IconId, float CornerRadius, const FLinearColor& Tint = FLinearColor::White);
}
