#include "UI/LDRecapChain.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDUIIcons.h"
#include "UI/LDWidgets.h"

namespace
{
UTextBlock* ChainText(UWidgetTree* Tree, const FString& Value, int32 Size, const FLinearColor& Color, LDUIFonts::ERole Role)
{
    UTextBlock* Label = LDWidgets::Text(Tree, Value, Size, NAME_None, Role);
    Label->SetAutoWrapText(false);
    Label->SetColorAndOpacity(FSlateColor(Color));
    return Label;
}

UBorder* Chip(UWidgetTree* Tree, UWidget* Child, const FLinearColor& Fill, FMargin Padding)
{
    auto* Border = Tree->ConstructWidget<UBorder>();
    Border->SetBrushColor(Fill);
    Border->SetPadding(Padding);
    Border->SetHorizontalAlignment(HAlign_Center);
    Border->SetVerticalAlignment(VAlign_Center);
    Border->SetContent(Child);
    return Border;
}

// The node's icon, or its glyph on a plain square when the icon is missing or there is none.
UWidget* IconSquare(UWidgetTree* Tree, const LDHUD::FRecapNode& Node, float Size)
{
    auto* Box = Tree->ConstructWidget<USizeBox>();
    Box->SetWidthOverride(Size);
    Box->SetHeightOverride(Size);
    auto* Overlay = Tree->ConstructWidget<UOverlay>();
    Box->SetContent(Overlay);
    auto* Image = Tree->ConstructWidget<UImage>();
    const bool bPicture = LDUIIcons::Apply(Image, Node.IconId, 4.f);
    if (!bPicture)
    {
        UTextBlock* Glyph = ChainText(Tree, Node.Glyph, FMath::Max(8, FMath::RoundToInt(Size * .55f)), LDHUDStyle::UIText, LDUIFonts::ERole::Strong);
        Glyph->SetJustification(ETextJustify::Center);
        auto* Square = Chip(Tree, Glyph, LDHUDStyle::UIRaised, FMargin(0));
        auto* SquareSlot = Overlay->AddChildToOverlay(Square);
        SquareSlot->SetHorizontalAlignment(HAlign_Fill);
        SquareSlot->SetVerticalAlignment(VAlign_Fill);
    }
    auto* ImageSlot = Overlay->AddChildToOverlay(Image);
    ImageSlot->SetHorizontalAlignment(HAlign_Fill);
    ImageSlot->SetVerticalAlignment(VAlign_Fill);
    return Box;
}
}

void LDRecapChain::Fill(UWidgetTree* Tree, UHorizontalBox* Row, const LDHUD::FRecapChain& Chain, float IconSize, int32 FontSize)
{
    if (!Tree || !Row) { return; }
    Row->ClearChildren();
    const float Space = FMath::Max(2.f, IconSize * .2f);
    const auto Add = [Row](UWidget* Widget, float Left)
    {
        UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Widget);
        Slot->SetVerticalAlignment(VAlign_Center);
        Slot->SetPadding(FMargin(Left, 0.f, 0.f, 0.f));
    };
    for (int32 Index = 0; Index < Chain.Nodes.Num(); ++Index)
    {
        const LDHUD::FRecapNode& Node = Chain.Nodes[Index];
        float Left = Index == 0 ? 0.f : Node.Link == LDHUD::ERecapLink::Gap ? Space * 4.f : Space;
        if (Index > 0 && Node.Link == LDHUD::ERecapLink::Arrow)
        {
            Add(ChainText(Tree, TEXT("→"), FontSize, LDHUDStyle::TextSecondary, LDUIFonts::ERole::Body), Space);
            Left = Space;
        }
        UWidget* Widget = nullptr;
        switch (Node.Kind)
        {
        case LDHUD::ERecapNode::Source:
        case LDHUD::ERecapNode::State:
            Widget = IconSquare(Tree, Node, IconSize);
            break;
        case LDHUD::ERecapNode::Attacker:
        case LDHUD::ERecapNode::Setter:
        {
            // A name chip: its glyph square, then the name.
            auto* Pair = Tree->ConstructWidget<UHorizontalBox>();
            LDHUD::FRecapNode Face = Node;
            Face.IconId.Reset();
            Pair->AddChildToHorizontalBox(IconSquare(Tree, Face, IconSize))->SetVerticalAlignment(VAlign_Center);
            const FLinearColor Ink = Node.Kind == LDHUD::ERecapNode::Attacker ? LDHUDStyle::UIText : LDHUDStyle::TextSecondary;
            UHorizontalBoxSlot* NameSlot = Pair->AddChildToHorizontalBox(ChainText(Tree, Node.Text, FontSize, Ink, LDUIFonts::ERole::Strong));
            NameSlot->SetVerticalAlignment(VAlign_Center);
            NameSlot->SetPadding(FMargin(Space, 0.f, 0.f, 0.f));
            Widget = Pair;
            break;
        }
        case LDHUD::ERecapNode::Reaction:
            Widget = Chip(Tree, ChainText(Tree, Node.Text, FontSize, LDHUDStyle::TextAccent, LDUIFonts::ERole::Strong), LDHUDStyle::UIRaised,
                FMargin(Space, 0.f));
            break;
        case LDHUD::ERecapNode::Amount:
            Widget = ChainText(Tree, Node.Text, FontSize, LDHUDStyle::UIText, LDUIFonts::ERole::Number);
            break;
        case LDHUD::ERecapNode::Tag:
            Widget = ChainText(Tree, Node.Text, FontSize, LDHUDStyle::UIAction, LDUIFonts::ERole::Strong);
            break;
        default:
            Widget = ChainText(Tree, Node.Text, FontSize, LDHUDStyle::TextSecondary, LDUIFonts::ERole::Body);
            break;
        }
        if (!Node.Name.IsEmpty()) { Widget->SetToolTipText(FText::FromString(Node.Name)); }
        Add(Widget, Left);
    }
}
