#pragma once
#include "CoreMinimal.h"
#include "UI/LDHUDModel.h"

class UHorizontalBox;
class UWidgetTree;

// Draws one death recap row (LDHUD::FDamageListView::Chains) as an icon chain into Row, replacing its children: icon
// nodes are IconSize squares (the imported icon, or the node's glyph on a plain square), text nodes are FontSize chips,
// an Arrow link draws "→" and a Gap link a wider space. Every node carries its full name as hover text. Display only.
namespace LDRecapChain
{
    LOCKDOWN_API void Fill(UWidgetTree* Tree, UHorizontalBox* Row, const LDHUD::FRecapChain& Chain, float IconSize, int32 FontSize);
}
