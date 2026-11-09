#include "UI/LDUIIcons.h"
#include "Core/LDLog.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Styling/SlateBrush.h"

namespace
{
    bool IconIdIsWellFormed(const FString& IconId)
    {
        if (!IconId.StartsWith(TEXT("icon_"))) { return false; }
        for (const TCHAR Char : IconId)
        {
            if (!((Char >= 'a' && Char <= 'z') || (Char >= '0' && Char <= '9') || Char == '_')) { return false; }
        }
        return true;
    }
}

FString LDUIIcons::PackageFor(const FString& IconId)
{
    return IconIdIsWellFormed(IconId) ? FString(Folder) / (TEXT("T_") + IconId) : FString();
}

UTexture2D* LDUIIcons::Find(const FString& IconId)
{
    const FString Package = PackageFor(IconId);
    UTexture2D* Texture = nullptr;
    if (!Package.IsEmpty())
    {
        const FString Path = Package + TEXT(".") + FPackageName::GetShortName(Package);
        Texture = FindObject<UTexture2D>(nullptr, *Path);
        if (!Texture && FPackageName::DoesPackageExist(Package)) { Texture = LoadObject<UTexture2D>(nullptr, *Path); }
    }
    if (!Texture)
    {
        static TSet<FString> Reported;
        bool bReported = false;
        Reported.Add(IconId, &bReported);
        if (!bReported)
        {
            UE_LOG(LogLDUI, Error, TEXT("LD_UI_ICON_MISSING '%s' has no imported icon texture; run ImportDemoArt.ps1 -Mode ImportUI."), *IconId);
        }
    }
    return Texture;
}

bool LDUIIcons::Apply(UImage* Image, const FString& IconId, float CornerRadius, const FLinearColor& Tint)
{
    if (!Image) { return false; }
    UTexture2D* Texture = IconId.IsEmpty() ? nullptr : Find(IconId);
    if (!Texture)
    {
        Image->SetVisibility(ESlateVisibility::Collapsed);
        return false;
    }
    if (Image->GetBrush().GetResourceObject() != Texture || !Image->GetColorAndOpacity().Equals(Tint))
    {
        FSlateBrush Brush;
        Brush.SetResourceObject(Texture);
        Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
        Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
        Brush.OutlineSettings = FSlateBrushOutlineSettings(CornerRadius);
        Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
        Image->SetBrush(Brush);
        Image->SetColorAndOpacity(Tint);
    }
    Image->SetVisibility(ESlateVisibility::HitTestInvisible);
    return true;
}
