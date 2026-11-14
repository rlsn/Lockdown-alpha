#include "UI/LDUIFonts.h"
#include "Core/LDLog.h"
#include "UI/LDHUDStyle.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Misc/PackageName.h"
#include "Styling/CoreStyle.h"

namespace
{
    TSharedPtr<const FCompositeFont> InterfaceFont()
    {
        static TSharedPtr<FStandaloneCompositeFont> Font;
        static bool bTried = false;
        if (bTried) { return Font; }
        bTried = true;
        TSharedRef<FStandaloneCompositeFont> Built = MakeShared<FStandaloneCompositeFont>();
        for (const TCHAR* Typeface : LDUIFonts::Typefaces)
        {
            const FString Package = LDUIFonts::PackageFor(Typeface);
            const UFontFace* Face = FPackageName::DoesPackageExist(Package)
                ? LoadObject<UFontFace>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package))) : nullptr;
            if (!Face)
            {
                UE_LOG(LogLDUI, Error, TEXT("LD_UI_FONT_MISSING %s has no imported font face; run ImportDemoArt.ps1 -Mode ImportUI."), *Package);
                return Font;
            }
            FTypefaceEntry& Entry = Built->DefaultTypeface.Fonts.Emplace_GetRef(FName(Typeface));
            Entry.Font = FFontData(Face);
        }
        Font = Built;
        return Font;
    }
}

FString LDUIFonts::PackageFor(const FString& Typeface)
{
    for (const TCHAR* Known : Typefaces)
    {
        if (Typeface.Equals(Known, ESearchCase::CaseSensitive)) { return FString(Folder) / (TEXT("FF_") + Typeface); }
    }
    return FString();
}

FSlateFontInfo LDUIFonts::Font(ERole Role, float Size, const FFontOutlineSettings& Outline)
{
    const TCHAR* Typeface = Role == ERole::Title ? TEXT("Heavy") : Role == ERole::Body ? TEXT("Regular") : TEXT("Bold");
    const TSharedPtr<const FCompositeFont> Composite = InterfaceFont();
    FSlateFontInfo Info = Composite.IsValid()
        ? FSlateFontInfo(Composite, Size, FName(Typeface), Outline)
        : FCoreStyle::GetDefaultFontStyle(Role == ERole::Body ? TEXT("Regular") : TEXT("Bold"), Size, Outline);
    if (Role == ERole::Title) { Info.SkewAmount = LDHUDStyle::TitleSkew; }
    return Info;
}
