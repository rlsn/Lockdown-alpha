#include "UI/LDResultBuild.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Engine/Texture2D.h"
#include "Framework/LDCaptureTypes.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDUIIcons.h"
#include "UI/LDWidgets.h"
#include "UI/LDWorkshopModel.h"

namespace
{
ELDComponentTargetKind GrowthTarget(ELDResultBuildTarget Kind)
{
    switch (Kind)
    {
    case ELDResultBuildTarget::Active: return ELDComponentTargetKind::Active;
    case ELDResultBuildTarget::EffectSegment: return ELDComponentTargetKind::EffectSegment;
    case ELDResultBuildTarget::Component: return ELDComponentTargetKind::Component;
    case ELDResultBuildTarget::Dash: return ELDComponentTargetKind::Dash;
    default: return ELDComponentTargetKind::None;
    }
}

UTextBlock* BoardText(UWidgetTree* Tree, const FString& Text, int32 Size)
{
    UTextBlock* Label = LDWidgets::Text(Tree, Text, Size, NAME_None, LDUIFonts::ERole::Strong);
    Label->SetAutoWrapText(false);
    Label->SetJustification(ETextJustify::Center);
    Label->SetColorAndOpacity(LDHUDStyle::TextSecondary);
    return Label;
}

UWidget* BoardIcon(UWidgetTree* Tree, const FLDDemoContentDefinitions* Content, FName Id, float Size, bool bComponent,
    const FString& OverrideIcon = FString())
{
    auto* Box = Tree->ConstructWidget<USizeBox>();
    Box->SetWidthOverride(Size);
    Box->SetHeightOverride(Size);
    auto* Overlay = Tree->ConstructWidget<UOverlay>();
    Box->SetContent(Overlay);
    const FString Name = Id.IsNone() ? FString() : LDHUD::ContentName(Content, Id.ToString());
    const FString IconId = OverrideIcon.IsEmpty() && !Id.IsNone() ? LDHUD::ContentIconId(Content, Id.ToString()) : OverrideIcon;
    auto* Image = Tree->ConstructWidget<UImage>();
    const bool bHasIcon = !IconId.IsEmpty() && LDUIIcons::Apply(Image, IconId, 0.f);
    // Keep the brush's texture alive without painting a square behind the shaped tile.
    Image->SetVisibility(ESlateVisibility::Collapsed);
    Overlay->AddChildToOverlay(Image);
    const LDWorkshop::FComponentFacts Facts = bComponent ? LDWorkshop::ComponentFacts(Id, Content) : LDWorkshop::FComponentFacts();
    auto* Face = Tree->ConstructWidget<ULDPaintLayer>();
    const TWeakObjectPtr<UImage> Icon(Image);
    Face->Painter = [Icon, bComponent, Kind = uint8(Facts.Kind), Scarce = Facts.bScarce, bHasIcon](const LDUIPaint::FContext& Paint)
    {
        UTexture2D* Texture = Icon.IsValid() ? Cast<UTexture2D>(Icon->GetBrush().GetResourceObject()) : nullptr;
        const FBox2D Rect(FVector2D::ZeroVector, FVector2D(Paint.Geometry.GetLocalSize()));
        if (bComponent) { Paint.Tile(Paint.Layer, Rect, Kind, Scarce, LDUIPaint::ETileState::Normal, Texture); }
        else { Paint.SkillArt(Paint.Layer, Rect, false, !bHasIcon, Texture); }
    };
    auto* FaceSlot = Overlay->AddChildToOverlay(Face);
    FaceSlot->SetHorizontalAlignment(HAlign_Fill);
    FaceSlot->SetVerticalAlignment(VAlign_Fill);
    if (!bHasIcon && !Name.IsEmpty())
    {
        UTextBlock* Fallback = BoardText(Tree, LDHUD::IconGlyph(Name), FMath::Max(8, FMath::RoundToInt(Size * .5f)));
        auto* Slot = Overlay->AddChildToOverlay(Fallback);
        Slot->SetHorizontalAlignment(HAlign_Center);
        Slot->SetVerticalAlignment(VAlign_Center);
    }
    if (!Name.IsEmpty()) { Box->SetToolTipText(FText::FromString(Name)); }
    return Box;
}
}

UWidget* LDResultBuild::Make(UWidgetTree* Tree, const FLDResultBuild& Build, const FLDDemoContentDefinitions* Content,
    float Width, float Height, float TextScale)
{
    auto* Bounds = Tree->ConstructWidget<USizeBox>();
    Bounds->SetWidthOverride(Width);
    Bounds->SetHeightOverride(Height);
    FString Error;
    if (!Build.bAvailable) { Error = TEXT("构筑摘要不可用"); }
    else if (!Content) { Error = TEXT("本地构筑目录不可用"); }
    else if (!Build.CatalogRoot.Equals(Content->Identity.Root.ToHex(), ESearchCase::IgnoreCase)) { Error = TEXT("构筑目录不匹配"); }
    else if (!LDCapture::ValidateResultBuild(Build, Error)) { Error = TEXT("构筑摘要无效"); }
    if (!Error.IsEmpty())
    {
        Bounds->SetContent(BoardText(Tree, Error, FMath::RoundToInt(12 * TextScale)));
        return Bounds;
    }

    FLDProgressionView Progression;
    Progression.bValid = true;
    Progression.Level = 1;
    for (const FLDResultBuildActive& Active : Build.Actives)
    {
        FLDActiveSlotView& Row = Progression.Actives.AddDefaulted_GetRef();
        Row.DefinitionId = Active.DefinitionId;
        Row.Slot = Active.Slot;
    }
    for (const FLDResultBuildComponent& Component : Build.Components)
    {
        FLDComponentInstanceView& Row = Progression.Components.AddDefaulted_GetRef();
        Row.DefinitionId = Component.DefinitionId;
        Row.bInstalled = true;
        Row.Target.Kind = GrowthTarget(Component.Target);
        Row.Target.Definition = Component.TargetId;
        Row.Target.BoundActive = Component.BoundActive;
    }
    const LDHUD::FStorageView Board = LDHUD::ComponentStorage(Progression, Content);
    int32 Most = 0;
    for (const LDHUD::FStorageColumnView& Column : Board.Columns) { Most = FMath::Max(Most, Column.Entries.Num()); }
    const float ColumnWidth = Width / float(LDHUD::StorageColumnCount);
    const bool bExpanded = Height >= 180.f;
    const float Header = bExpanded ? 64.f : FMath::Min(Height * .34f, ColumnWidth * .36f);
    const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(Most, 2));
    const float TileSize = bExpanded ? 56.f
        : FMath::Clamp(FMath::Min((ColumnWidth - 4.f) / 2.f, (Height - Header - 4.f) / Rows - 1.f), 4.f, Header * .72f);

    int32 FinalColumn = INDEX_NONE;
    if (!Build.FinalMod.IsNone())
    {
        if (Build.FinalTarget == ELDResultBuildTarget::Body) { FinalColumn = 0; }
        else if (Build.FinalTarget == ELDResultBuildTarget::Dash) { FinalColumn = 4; }
        else
        {
            for (int32 Index = 0; Index < Board.Columns.Num(); ++Index)
            {
                const LDHUD::FStorageColumnView& Column = Board.Columns[Index];
                if (((Build.FinalTarget == ELDResultBuildTarget::Active || Build.FinalTarget == ELDResultBuildTarget::EffectSegment)
                        && Column.Active == Build.FinalTargetId)
                    || (Build.FinalTarget == ELDResultBuildTarget::Component
                        && Column.Entries.ContainsByPredicate([&Build](const LDHUD::FEquippedView& Entry)
                            { return Entry.Component == Build.FinalTargetId; })))
                { FinalColumn = Index; break; }
            }
        }
    }

    const bool bDormantFinal = !Build.FinalMod.IsNone() && FinalColumn == INDEX_NONE;
    if (bDormantFinal) { FinalColumn = 0; }
    auto* Columns = Tree->ConstructWidget<UHorizontalBox>();
    Bounds->SetContent(Columns);
    static const TCHAR* const Labels[] = {TEXT("本体"), TEXT("Q"), TEXT("W"), TEXT("E"), TEXT("D")};
    for (int32 Index = 0; Index < Board.Columns.Num(); ++Index)
    {
        const LDHUD::FStorageColumnView& Column = Board.Columns[Index];
        auto* ColumnBox = Tree->ConstructWidget<USizeBox>();
        ColumnBox->SetWidthOverride(ColumnWidth);
        auto* Stack = Tree->ConstructWidget<UVerticalBox>();
        ColumnBox->SetContent(Stack);
        Columns->AddChildToHorizontalBox(ColumnBox);
        auto* Head = Tree->ConstructWidget<UHorizontalBox>();
        Stack->AddChildToVerticalBox(Head);
        auto* Label = BoardText(Tree, Labels[Index], FMath::Max(8, FMath::RoundToInt(Header * .37f * TextScale)));
        Head->AddChildToHorizontalBox(Label)->SetVerticalAlignment(VAlign_Center);
        if (Index > 0)
        {
            UWidget* Icon = BoardIcon(Tree, Content, Column.Active, Header, false, Index == 4 ? FString(LDUIIcons::Dash) : FString());
            Head->AddChildToHorizontalBox(Icon)->SetPadding(FMargin(2, 0, 0, 0));
        }
        if (FinalColumn == Index)
        {
            UWidget* Final = BoardIcon(Tree, Content, Build.FinalMod, bExpanded ? 56.f : Header * .65f, false);
            if (bDormantFinal)
            {
                Final->SetRenderOpacity(.45f);
                Final->SetToolTipText(FText::FromString(LDHUD::ContentName(Content, Build.FinalMod.ToString()) + TEXT(" · 来源已拆下")));
            }
            Head->AddChildToHorizontalBox(Final)->SetPadding(FMargin(2, 0, 0, 0));
        }
        auto* Grid = Tree->ConstructWidget<UWrapBox>();
        Grid->SetExplicitWrapSize(true);
        Grid->SetWrapSize(ColumnWidth - 2.f);
        Grid->SetInnerSlotPadding(FVector2D(1.f, 1.f));
        Stack->AddChildToVerticalBox(Grid)->SetPadding(FMargin(0, 2, 0, 0));
        const int32 Visible = bExpanded && Column.Entries.Num() > 4 ? 3 : Column.Entries.Num();
        for (int32 EntryIndex = 0; EntryIndex < Visible; ++EntryIndex)
        {
            const LDHUD::FEquippedView& Entry = Column.Entries[EntryIndex];
            UWidget* Tile = BoardIcon(Tree, Content, Entry.Component, TileSize, true);
            Grid->AddChildToWrapBox(Tile)->SetPadding(FMargin(Entry.Depth > 0 ? 1.f : 0.f, 0, 0, 0));
        }
        if (Visible < Column.Entries.Num())
        {
            auto* More = Tree->ConstructWidget<USizeBox>();
            More->SetWidthOverride(TileSize);More->SetHeightOverride(TileSize);
            More->SetContent(BoardText(Tree,FString::Printf(TEXT("+%d"),Column.Entries.Num()-Visible),FMath::RoundToInt(20*TextScale)));
            TArray<FString> Names;
            for(int32 EntryIndex=Visible;EntryIndex<Column.Entries.Num();++EntryIndex)
            {Names.Add(LDHUD::ContentName(Content,Column.Entries[EntryIndex].Component.ToString()));}
            More->SetToolTipText(FText::FromString(FString::Join(Names,TEXT("\n"))));
            Grid->AddChildToWrapBox(More);
        }
    }
    return Bounds;
}
