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
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"
#include "UI/LDWidgets.h"
#include "UI/LDWorkshopModel.h"

namespace
{
ELDCardTargetKind GrowthTarget(ELDResultBuildTarget Kind)
{
    switch (Kind)
    {
    case ELDResultBuildTarget::Active: return ELDCardTargetKind::Skill;
    case ELDResultBuildTarget::Dash: return ELDCardTargetKind::Dash;
    default: return ELDCardTargetKind::Body;
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
    const FName Definition = bComponent ? LDHUD::CardDefinition(Id) : Id;
    const FString Name = Id.IsNone() ? FString() : LDHUD::ContentName(Content, Definition.ToString());
    const FString IconId = OverrideIcon.IsEmpty() && !Id.IsNone() ? LDHUD::ContentIconId(Content, Definition.ToString()) : OverrideIcon;
    auto* Image = Tree->ConstructWidget<UImage>();
    const bool bHasIcon = !IconId.IsEmpty() && LDUIIcons::Apply(Image, IconId, 0.f);
    // Keep the brush's texture alive without painting a square behind the shaped tile.
    Image->SetVisibility(ESlateVisibility::Collapsed);
    Overlay->AddChildToOverlay(Image);
    const LDWorkshop::FCardFacts Facts = bComponent ? LDWorkshop::CardFacts(Id, Content) : LDWorkshop::FCardFacts();
    auto* Face = Tree->ConstructWidget<ULDPaintLayer>();
    const TWeakObjectPtr<UImage> Icon(Image);
    Face->Painter = [Icon, bComponent, Kind = uint8(Facts.Type), bHasIcon](const LDUIPaint::FContext& Paint)
    {
        UTexture2D* Texture = Icon.IsValid() ? Cast<UTexture2D>(Icon->GetBrush().GetResourceObject()) : nullptr;
        const FBox2D Rect(FVector2D::ZeroVector, FVector2D(Paint.Geometry.GetLocalSize()));
        if (bComponent) { Paint.Tile(Paint.Layer, Rect, Kind, LDUIPaint::ETileState::Normal, Texture); }
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
        Row.Branch = Active.Branch;
    }
    for (const FLDResultBuildCard& Card : Build.Cards)
    {
        FLDCardInstanceView& Row = Progression.Cards.AddDefaulted_GetRef();
        Row.Serial = uint16(Progression.Cards.Num());
        Row.DefinitionId = Card.DefinitionId;
        Row.Target.Kind = GrowthTarget(Card.Target);
        Row.Target.Active = Card.TargetId;
        Row.Target.Element = Card.Element;
    }
    LDHUD::FStorageView Board = LDHUD::CardStorage(Progression, Content);
    // The result board lists a column's element medal first, with its cards.
    for (LDHUD::FStorageColumnView& Column : Board.Columns) { if (Column.bHasMedal) { Column.Entries.Insert(Column.Medal, 0); } }
    int32 Most = 0;
    for (const LDHUD::FStorageColumnView& Column : Board.Columns) { Most = FMath::Max(Most, Column.Entries.Num()); }
    const float ColumnWidth = Width / float(LDHUD::StorageColumnCount);
    const bool bExpanded = Height >= 180.f;
    const bool bShowStats = bExpanded && Height >= 205.f && !Build.StatPoints.IsEmpty();
    const float StatStripHeight = bShowStats ? 26.f : 0.f;
    const float BoardHeight = Height - StatStripHeight;
    const float Header = bExpanded ? 64.f : FMath::Min(BoardHeight * .34f, ColumnWidth * .36f);
    const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(Most, 2));
    const float TileSize = bExpanded ? 54.f
        : FMath::Clamp(FMath::Min((ColumnWidth - 4.f) / 2.f, (BoardHeight - Header - 4.f) / Rows - 1.f), 4.f, Header * .72f);

    // A final mod binds no source: it sits on the body column.
    const int32 FinalColumn = Build.FinalMod.IsNone() ? INDEX_NONE : int32(LDHUD::EStorageColumn::Body);
    auto* Outer = Tree->ConstructWidget<UVerticalBox>();
    auto* Columns = Tree->ConstructWidget<UHorizontalBox>();
    Outer->AddChildToVerticalBox(Columns);
    Bounds->SetContent(Outer);
    // 普攻, Q, W, E, D, 本体 left to right, like the workshop board.
    for (int32 Position = 0; Position < LDHUD::StorageColumnCount; ++Position)
    {
        const LDHUD::EStorageColumn ColumnId = LDHUD::BoardColumn(Position);
        const int32 Index = int32(ColumnId);
        if (!Board.Columns.IsValidIndex(Index)) { continue; }
        const LDHUD::FStorageColumnView& Column = Board.Columns[Index];
        auto* ColumnBox = Tree->ConstructWidget<USizeBox>();
        ColumnBox->SetWidthOverride(ColumnWidth);
        auto* Stack = Tree->ConstructWidget<UVerticalBox>();
        ColumnBox->SetContent(Stack);
        Columns->AddChildToHorizontalBox(ColumnBox);
        auto* Head = Tree->ConstructWidget<UHorizontalBox>();
        Stack->AddChildToVerticalBox(Head);
        auto* Label = BoardText(Tree, LDHUD::ColumnLabel(ColumnId), FMath::Max(8, FMath::RoundToInt(Header * .37f * TextScale)));
        Head->AddChildToHorizontalBox(Label)->SetVerticalAlignment(VAlign_Center);
        if (ColumnId != LDHUD::EStorageColumn::Body)
        {
            UWidget* Icon = BoardIcon(Tree, Content, Column.Active, Header, false,
                ColumnId == LDHUD::EStorageColumn::Dash ? FString(LDUIIcons::Dash) : FString());
            Head->AddChildToHorizontalBox(Icon)->SetPadding(FMargin(2, 0, 0, 0));
            if (!Column.Active.IsNone())
            {
                const FLDResultBuildActive* FoundActive = Build.Actives.FindByPredicate([&Column](const FLDResultBuildActive& A)
                { return A.DefinitionId == Column.Active; });
                if (FoundActive)
                {
                    const uint8 Rank = LDSkillBranch::RankOf(FoundActive->Branch);
                    FString Tip = LDHUD::ContentName(Content, Column.Active.ToString()) + FString::Printf(TEXT(" · %s %u"), LDTerms::SkillRank, uint32(Rank));
                    if (FoundActive->Branch != ELDSkillBranch::None)
                    {
                        const LDHUD::FBranchCardView BranchView = LDHUD::BranchCard(Content, Column.Active, FoundActive->Branch, LDHUD::FCardStats());
                        if (!BranchView.Name.IsEmpty()) { Tip += TEXT("（") + BranchView.Name + TEXT("）"); }
                    }
                    Icon->SetToolTipText(FText::FromString(Tip));
                    if (bExpanded)
                    {
                        UTextBlock* Dots = BoardText(Tree, LDHUD::RankDots(Rank), FMath::Max(8, FMath::RoundToInt(9 * TextScale)));
                        Dots->SetColorAndOpacity(Rank >= 3 ? LDHUDStyle::UIAction : LDHUDStyle::TextAccent);
                        auto* DotsSlot = Head->AddChildToHorizontalBox(Dots);
                        DotsSlot->SetVerticalAlignment(VAlign_Bottom);
                        DotsSlot->SetPadding(FMargin(2, 0, 0, 0));
                    }
                }
            }
        }
        if (FinalColumn == Index)
        {
            UWidget* Final = BoardIcon(Tree, Content, Build.FinalMod, bExpanded ? 54.f : Header * .65f, false);
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
            UWidget* Tile = BoardIcon(Tree, Content, Entry.Card, TileSize, true);
            if (Entry.Copies > 1) { Tile->SetToolTipText(FText::FromString(LDHUD::ContentName(Content, LDHUD::CardDefinition(Entry.Card).ToString())
                + TEXT(" ") + LDTerms::CopiesBadge(Entry.Copies))); }
            Grid->AddChildToWrapBox(Tile);
        }
        if (Visible < Column.Entries.Num())
        {
            auto* More = Tree->ConstructWidget<USizeBox>();
            More->SetWidthOverride(TileSize);More->SetHeightOverride(TileSize);
            More->SetContent(BoardText(Tree,FString::Printf(TEXT("+%d"),Column.Entries.Num()-Visible),FMath::RoundToInt(20*TextScale)));
            TArray<FString> Names;
            for(int32 EntryIndex=Visible;EntryIndex<Column.Entries.Num();++EntryIndex)
            {Names.Add(LDHUD::ContentName(Content,LDHUD::CardDefinition(Column.Entries[EntryIndex].Card).ToString()));}
            More->SetToolTipText(FText::FromString(FString::Join(Names,TEXT("\n"))));
            Grid->AddChildToWrapBox(More);
        }
    }
    if (bShowStats)
    {
        auto* StatRow = Tree->ConstructWidget<UHorizontalBox>();
        const TArray<const FLDDemoStatDefinition*> StatOrder = Content->StatsInDesignOrder();
        const int32 Threshold = int32(FMath::Max<int64>(1, Content->Parameters.KeystoneThresholdPoints));
        for (int32 StatIdx = 0; StatIdx < StatOrder.Num() && Build.StatPoints.IsValidIndex(StatIdx); ++StatIdx)
        {
            const int32 Pts = int32(Build.StatPoints[StatIdx]);
            const FString StatName = LDHUD::ContentName(Content, StatOrder[StatIdx]->Id);
            const FString EntryText = FString::Printf(TEXT("%s %d%s"), *StatName, Pts, Pts >= Threshold ? TEXT("★") : TEXT(""));
            UTextBlock* Chip = BoardText(Tree, EntryText, FMath::Max(10, FMath::RoundToInt(13 * TextScale)));
            Chip->SetColorAndOpacity(Pts >= Threshold ? LDHUDStyle::UIAction : LDHUDStyle::UIText);
            StatRow->AddChildToHorizontalBox(Chip)->SetPadding(FMargin(StatIdx ? 14.f : 0.f, 0, 0, 0));
        }
        Outer->AddChildToVerticalBox(StatRow)->SetPadding(FMargin(0, 6, 0, 0));
    }
    return Bounds;
}
