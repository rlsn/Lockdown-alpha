#pragma once
#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

// Interface typography: Source Han Sans SC (SIL OFL 1.1). SourceData\UI\ui_fonts.json lists the Regular, Bold and Heavy
// faces of SourceData\UI\Fonts with their SHA-256; Scripts\ImportDemoArt.ps1 -Mode ImportUI (LDDemoUIImageImport) imports
// each into /Game/Lockdown/Art/UI/Fonts/FF_<Typeface>, which is always cooked. One composite font holds the three faces, so
// FSlateFontInfo::TypefaceFontName "Regular", "Bold" or "Heavy" picks a weight.
// A missing face is logged once as LD_UI_FONT_MISSING and every role falls back to the engine default font.
namespace LDUIFonts
{
    inline constexpr TCHAR Folder[] = TEXT("/Game/Lockdown/Art/UI/Fonts");
    inline constexpr const TCHAR* Typefaces[] = { TEXT("Regular"), TEXT("Bold"), TEXT("Heavy") };

    // Title: Heavy with an 8 degree slant. Body: Regular. Strong: Bold body text that leads its group. Number: Bold figures.
    enum class ERole : uint8 { Title, Body, Strong, Number };

    // The font face package of a typeface ("" for a name outside Typefaces).
    LOCKDOWN_API FString PackageFor(const FString& Typeface);
    LOCKDOWN_API FSlateFontInfo Font(ERole Role, float Size, const FFontOutlineSettings& Outline = FFontOutlineSettings());
}
