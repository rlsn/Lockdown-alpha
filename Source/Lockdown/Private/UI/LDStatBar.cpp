#include "UI/LDStatBar.h"
#include "Core/LDLog.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"
#include "UI/LDWidgets.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/World.h"

namespace
{
// Slate units at the 1080p reference, inside the band (which draws at its own scale).
constexpr float StatIconSize=44.f;
constexpr float StatGap=8.f;
constexpr float StatPlusHeight=24.f;
// The attribute view (#331): compact two-column list expanded ABOVE the permanently visible 5-stat row.
constexpr int32 AttrColumns=2;
constexpr float AttrGlyphSize=20.f;
constexpr double StatCoalesceSeconds=.12;
// An applied allocation that never shows in the own view stops holding the next one back after this long.
constexpr double StatAwaitTimeout=5.;
const FLinearColor StatFill(.02f,.03f,.05f,.9f);
const FLinearColor StatEdge(1,1,1,.18f);
const FLinearColor StatPlusFill(FColor(0xFF,0xC8,0x3D));
const FLinearColor StatPlusHover(FColor(0xFF,0xD8,0x66));
const FLinearColor StatPlusPress(FColor(0xE6,0xAE,0x2A));
const FLinearColor StatPlusIdle(.16f,.17f,.19f);
const FLinearColor StatPlusInk(FColor(0x1A,0x13,0x00));

FSlateRoundedBoxBrush StatRound(float Radius,const FLinearColor& Edge,float Width)
{FSlateRoundedBoxBrush Brush(FLinearColor::White,Radius,Edge,Width);Brush.OutlineSettings.bUseBrushTransparency=true;return Brush;}
void StatText(UTextBlock* Text,const FString& Value)
{if(Text&&!Text->GetText().ToString().Equals(Value,ESearchCase::CaseSensitive)){Text->SetText(FText::FromString(Value));}}
void StatShown(UWidget* Widget,bool bShown,ESlateVisibility Shown,ESlateVisibility Hidden=ESlateVisibility::Collapsed)
{
    if(!Widget){return;}
    const ESlateVisibility Next=bShown?Shown:Hidden;
    if(Widget->GetVisibility()!=Next){Widget->SetVisibility(Next);}
}
void StatBold(UTextBlock* Text,int32 Outline)
{
    auto Font=Text->GetFont();Font.TypefaceFontName=FName(TEXT("Bold"));
    if(Outline>0){Font.OutlineSettings.OutlineSize=Outline;Font.OutlineSettings.OutlineColor=LDHUDStyle::Rim;}
    Text->SetFont(Font);
}
int32 StatSum(const TArray<int32>& Counts){int32 Total=0;for(const int32 Count:Counts){Total+=Count;}return Total;}
int32 StatAbsSum(const TArray<int32>& Counts){int32 Total=0;for(const int32 Count:Counts){Total+=FMath::Abs(Count);}return Total;}
}

TSharedRef<SWidget> ULDStatBar::RebuildWidget()
{
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if(!Row)
    {
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Root;
        // Secondary combat attributes expand ABOVE the primary 5-stat row when P is toggled (#331), never hiding the + buttons.
        AttrGrid=WidgetTree->ConstructWidget<UUniformGridPanel>();AttrGrid->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        AttrGrid->SetSlotPadding(FMargin(0,2,10,2));
        const TArray<uint8> Order=LDHUD::AttributeViewOrder();
        for(int32 Index=0;Index<Order.Num();++Index)
        {
            FLDCombatAttributeRowView Probe;Probe.Attribute=Order[Index];
            const LDHUD::FAttributeRowView Face=LDHUD::AttributeRow(Probe);
            FAttrCell& Cell=AttrCells.AddDefaulted_GetRef();Cell.Attribute=Order[Index];
            auto* Glyph=LDWidgets::Text(WidgetTree,Face.Glyph,LDHUDStyle::TypeTiny);Glyph->SetAutoWrapText(false);Glyph->SetJustification(ETextJustify::Center);StatBold(Glyph,1);
            auto* Icon=WidgetTree->ConstructWidget<UBorder>();Icon->SetBrush(StatRound(4.f,StatEdge,1.f));Icon->SetBrushColor(StatFill);
            Icon->SetPadding(FMargin(0));Icon->SetHorizontalAlignment(HAlign_Center);Icon->SetVerticalAlignment(VAlign_Center);Icon->SetContent(Glyph);
            auto* IconBox=WidgetTree->ConstructWidget<USizeBox>();IconBox->SetWidthOverride(AttrGlyphSize);IconBox->SetHeightOverride(AttrGlyphSize);IconBox->SetContent(Icon);
            Cell.Value=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeSmall);Cell.Value->SetAutoWrapText(false);StatBold(Cell.Value,1);
            Cell.Value->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            auto* Entry=WidgetTree->ConstructWidget<UHorizontalBox>();Entry->SetVisibility(ESlateVisibility::Visible);
            Entry->AddChildToHorizontalBox(IconBox)->SetVerticalAlignment(VAlign_Center);
            UHorizontalBoxSlot* ValueSlot=Entry->AddChildToHorizontalBox(Cell.Value);ValueSlot->SetPadding(FMargin(5,0,0,0));ValueSlot->SetVerticalAlignment(VAlign_Center);
            ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            Cell.Box=Entry;
            Built.Append({Glyph,Icon,IconBox,Cell.Value,Entry});
            UUniformGridSlot* GridSlot=AttrGrid->AddChildToUniformGrid(Entry,Index/AttrColumns,Index%AttrColumns);
            GridSlot->SetHorizontalAlignment(HAlign_Fill);GridSlot->SetVerticalAlignment(VAlign_Center);
        }
        auto* AttrPanel=LDWidgets::Panel(WidgetTree,AttrGrid,FMargin(8,6));
        AttrPanel->SetBrushColor(LDHUDStyle::PanelFill.CopyWithNewOpacity(.90f));
        AttrPanel->SetVisibility(ESlateVisibility::Collapsed);
        AttrFrame=AttrPanel;
        Root->AddChildToVerticalBox(AttrPanel)->SetPadding(FMargin(0,0,0,6));
        Row=WidgetTree->ConstructWidget<UHorizontalBox>();Row->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        Root->AddChildToVerticalBox(Row);
        // Until the catalog and the own view arrive the block says so instead of showing empty stats.
        Syncing=LDWidgets::Text(WidgetTree,TEXT("属性同步中"),LDHUDStyle::TypeSmall);Syncing->SetAutoWrapText(false);Syncing->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        Syncing->SetVisibility(ESlateVisibility::Collapsed);
        Root->AddChildToVerticalBox(Syncing)->SetPadding(FMargin(0,StatPlusHeight,0,0));
    }
    return Super::RebuildWidget();
}
void ULDStatBar::BuildCells()
{
    // Cells are built once from the catalog this process plays; a hot reload changes numbers only, never the ids.
    if(!LatestContent||!Row||!Cells.IsEmpty()){return;}
    const TArray<const FLDDemoStatDefinition*> StatOrder=LatestContent->StatsInDesignOrder();
    for(int32 Index=0;Index<StatOrder.Num();++Index)
    {
        FCell& Cell=Cells.AddDefaulted_GetRef();
        Cell.StatId=FName(*StatOrder[Index]->Id);
        // The "+" sits right above the icon in a reserved strip, so moving the pointer up to it never leaves the cell.
        Cell.Plus=LDWidgets::Button(WidgetTree,TEXT("+"),[this,Index](){Allocate(Index);},true,FName(*(TEXT("HUDStatPlus_")+StatOrder[Index]->Id)));
        // Held, it keeps adding a point at a time until the points run out.
        Cell.Plus->EnableRepeat([this,Index](){return CanAllocate(Index)&&Allocate(Index);});
        {
            FButtonStyle Style=Cell.Plus->GetStyle();
            Style.Normal=StatRound(5.f,LDHUDStyle::Rim,1.f);Style.Normal.TintColor=FSlateColor(StatPlusFill);
            Style.Hovered=StatRound(5.f,LDHUDStyle::Rim,1.f);Style.Hovered.TintColor=FSlateColor(StatPlusHover);
            Style.Pressed=StatRound(5.f,LDHUDStyle::Rim,1.f);Style.Pressed.TintColor=FSlateColor(StatPlusPress);
            Style.Disabled=StatRound(5.f,LDHUDStyle::Rim,1.f);Style.Disabled.TintColor=FSlateColor(StatPlusIdle);
            Style.NormalPadding=FMargin(0);Style.PressedPadding=FMargin(0);
            Cell.Plus->SetStyle(Style);
        }
        if(UTextBlock* Caption=Cast<UTextBlock>(Cell.Plus->GetContent()))
        {
            Caption->SetColorAndOpacity(StatPlusInk);StatBold(Caption,0);Caption->SetJustification(ETextJustify::Center);
            auto Font=Caption->GetFont();Font.Size=LDHUDStyle::TypeLead;Caption->SetFont(Font);
            if(UButtonSlot* CaptionSlot=Cast<UButtonSlot>(Caption->Slot)){CaptionSlot->SetPadding(FMargin(0));CaptionSlot->SetHorizontalAlignment(HAlign_Center);CaptionSlot->SetVerticalAlignment(VAlign_Center);}
        }
        Cell.Plus->SetVisibility(ESlateVisibility::Hidden);Built.Add(Cell.Plus);
        auto* PlusBox=WidgetTree->ConstructWidget<USizeBox>();PlusBox->SetWidthOverride(StatIconSize);PlusBox->SetHeightOverride(StatPlusHeight-2.f);
        PlusBox->SetContent(Cell.Plus);Built.Add(PlusBox);
        // The icon (the name's first character without its texture) and the allocated points on the corner.
        Cell.Picture=WidgetTree->ConstructWidget<UImage>();Cell.Picture->SetVisibility(ESlateVisibility::Collapsed);Built.Add(Cell.Picture);
        Cell.Glyph=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeLead);Cell.Glyph->SetAutoWrapText(false);Cell.Glyph->SetJustification(ETextJustify::Center);StatBold(Cell.Glyph,1);Built.Add(Cell.Glyph);
        Cell.Points=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeCaption);Cell.Points->SetAutoWrapText(false);StatBold(Cell.Points,2);Built.Add(Cell.Points);
        auto* Face=WidgetTree->ConstructWidget<UOverlay>();Built.Add(Face);
        auto* PictureSlot=Face->AddChildToOverlay(Cell.Picture);PictureSlot->SetHorizontalAlignment(HAlign_Fill);PictureSlot->SetVerticalAlignment(VAlign_Fill);
        auto* GlyphSlot=Face->AddChildToOverlay(Cell.Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
        auto* PointsSlot=Face->AddChildToOverlay(Cell.Points);PointsSlot->SetHorizontalAlignment(HAlign_Right);PointsSlot->SetVerticalAlignment(VAlign_Bottom);
        PointsSlot->SetPadding(FMargin(0,0,3,0));
        Cell.Face=WidgetTree->ConstructWidget<UBorder>();Cell.Face->SetBrush(StatRound(5.f,StatEdge,1.f));Cell.Face->SetBrushColor(StatFill);
        Cell.Face->SetPadding(FMargin(2));Cell.Face->SetContent(Face);Cell.Face->SetVisibility(ESlateVisibility::Visible);Built.Add(Cell.Face);
        auto* IconBox=WidgetTree->ConstructWidget<USizeBox>();IconBox->SetWidthOverride(StatIconSize);IconBox->SetHeightOverride(StatIconSize);
        IconBox->SetContent(Cell.Face);Built.Add(IconBox);
        auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Column->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Built.Add(Column);
        Column->AddChildToVerticalBox(PlusBox)->SetPadding(FMargin(0,0,0,2));
        Column->AddChildToVerticalBox(IconBox);
        Cell.Box=Column;
        Row->AddChildToHorizontalBox(Column)->SetPadding(FMargin(Index?StatGap:0.f,0,0,0));
    }
}
double ULDStatBar::UiNow() const{const UWorld* World=GetWorld();return World?World->GetRealTimeSeconds():0.;}
bool ULDStatBar::Usable() const
{
    return Latest.bValid&&LatestMatch!=0&&Latest.Generation==LatestMatch&&Latest.Life==LatestLife&&LatestContent
        &&!Cells.IsEmpty()&&Latest.StatPoints.Num()==Cells.Num();
}
int64 ULDStatBar::Available() const
{
    return FMath::Max<int64>(0,Latest.UnspentPoints-StatSum(Queued)-StatSum(InFlight));
}
int32 ULDStatBar::EffectivePoints(int32 Index) const
{
    if(!Cells.IsValidIndex(Index)){return 0;}
    const int32 Base=LDHUD::StatPointsOf(Latest,Cells[Index].StatId);
    const int32 Pending=(Queued.IsValidIndex(Index)?Queued[Index]:0)+(InFlight.IsValidIndex(Index)?InFlight[Index]:0);
    return FMath::Max(0,Base+Pending);
}
int32 ULDStatBar::Waiting() const{return StatAbsSum(Queued)+StatAbsSum(InFlight);}
ULDCommandButton* ULDStatBar::PlusButton(int32 Index) const{return Cells.IsValidIndex(Index)?Cells[Index].Plus:nullptr;}

void ULDStatBar::Present(const FLDProgressionView* View,uint32 Match,uint32 Life,LDProgression::EMatchStage Stage,ELDLifePhase LifePhase,
    const FLDDemoContentDefinitions* Content,const TArray<FLDCombatAttributeRowView>& Attributes,bool bShown)
{
    // Another match's answers never reach this one: its waiting points end with it.
    if(Match!=LatestMatch){PendingRequest=0;AwaitRevision=0;LastBatchSentAt=-1.;Queued.Reset();InFlight.Reset();bAttributes=false;}
    LatestMatch=Match;LatestLife=Life;
    Latest=View?*View:FLDProgressionView();
    LatestContent=Content;LatestAttributes=Attributes;
    BuildCells();
    Queued.SetNumZeroed(Cells.Num());InFlight.SetNumZeroed(Cells.Num());
    Gate=Usable()?LDHUD::BuildGate(Stage,LifePhase):LDHUD::FBuildGateView();
    bShownNow=bShown;
    // The last applied allocation is in the view (or never arrived): its points stop counting as waiting.
    if(AwaitRevision!=0&&(Latest.BuildRevision>=AwaitRevision||UiNow()-AwaitSince>StatAwaitTimeout))
    {
        if(Latest.BuildRevision<AwaitRevision){UE_LOG(LogLDUI,Warning,TEXT("LD_HUD_STAT_VIEW_LATE The own view did not carry the applied allocation within %.0f s."),StatAwaitTimeout);}
        AwaitRevision=0;for(int32& Count:InFlight){Count=0;}
    }
    Flush();
    Refresh();
}
void ULDStatBar::ToggleView()
{
    bAttributes=!bAttributes;AttrHovered=INDEX_NONE;
    Refresh();
}
void ULDStatBar::SetDetailsHeld(bool bHeld)
{
    if(bDetailsHeld==bHeld){return;}
    bDetailsHeld=bHeld;AttrHovered=INDEX_NONE;
    Refresh();
}
const FLDCombatAttributeRowView* ULDStatBar::AttributeOf(uint8 Attribute) const
{
    return LatestAttributes.FindByPredicate([Attribute](const FLDCombatAttributeRowView& Row){return Row.Attribute==Attribute;});
}
void ULDStatBar::RefreshAttributes()
{
    // The projection's own rows, never a recomputed value; an attribute it does not publish reads as a dash.
    for(const FAttrCell& Cell:AttrCells)
    {
        const FLDCombatAttributeRowView* Published=AttributeOf(Cell.Attribute);
        StatText(Cell.Value,Published?LDHUD::AttributeRow(*Published).Value:FString(TEXT("—")));
    }
}
void ULDStatBar::Refresh()
{
    const bool bStats=Usable();
    const bool bAttrs=bAttributes&&!LatestAttributes.IsEmpty();
    StatShown(Syncing,!bStats&&!bAttrs,ESlateVisibility::HitTestInvisible);
    StatShown(Row,bStats,ESlateVisibility::SelfHitTestInvisible);
    StatShown(AttrFrame,bAttrs,ESlateVisibility::SelfHitTestInvisible);
    if(bAttrs){RefreshAttributes();}
    if(!bStats){return;}
    const TArray<const FLDDemoStatDefinition*> StatOrder=LatestContent->StatsInDesignOrder();
    const bool bCanSpend=Available()>0;
    for(int32 Index=0;Index<Cells.Num()&&Index<StatOrder.Num();++Index)
    {
        FCell& Cell=Cells[Index];
        const LDHUD::FStatRowView View=LDHUD::StatRow(*StatOrder[Index],LDHUD::StatPointsOf(Latest,Cell.StatId),LatestContent);
        if(Cell.AppliedIcon!=View.IconId)
        {
            Cell.AppliedIcon=View.IconId;
            StatShown(Cell.Glyph,!LDUIIcons::Apply(Cell.Picture,View.IconId,4.f),ESlateVisibility::HitTestInvisible);
        }
        StatText(Cell.Glyph,View.Glyph);
        const int32 Pending=Queued[Index]+InFlight[Index];
        const int32 Optimistic=FMath::Max(0,View.Points+Pending);
        StatText(Cell.Points,FString::FromInt(Optimistic));
        Cell.Points->SetColorAndOpacity(Pending!=0?LDHUDStyle::TextAccent:Optimistic>=int32(LatestContent->Parameters.KeystoneThresholdPoints)?LDHUDStyle::UIAction:LDHUDStyle::TextPrimary);
        // The "+" stays accessible on the permanently visible 5-stat row (#331).
        const bool bPlus=bShownNow&&bCanSpend&&(Index==HoveredIndex||bAttributes);
        StatShown(Cell.Plus,bPlus,ESlateVisibility::Visible,ESlateVisibility::Hidden);
        if(Cell.Plus->GetIsEnabled()!=Gate.bCanAllocate){Cell.Plus->SetIsEnabled(Gate.bCanAllocate);}
        const bool bHover=Index==HoveredIndex;
        if(Cell.AppliedHover!=int32(bHover))
        {
            Cell.AppliedHover=int32(bHover);
            Cell.Face->SetBrush(StatRound(5.f,bHover?LDHUDStyle::TextAccent:StatEdge,bHover?2.f:1.f));Cell.Face->SetBrushColor(StatFill);
        }
    }
}
void ULDStatBar::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    Flush();
    int32 Next=INDEX_NONE;
    AttrHovered=INDEX_NONE;
    if(bShownNow&&FSlateApplication::IsInitialized())
    {
        const FVector2D Pointer=FSlateApplication::Get().GetCursorPos();
        if(bAttributes&&AttrFrame&&AttrFrame->IsVisible())
        {
            for(int32 Index=0;Index<AttrCells.Num();++Index)
            {if(AttrCells[Index].Box->GetCachedGeometry().IsUnderLocation(Pointer)){AttrHovered=Index;break;}}
        }
        for(int32 Index=0;Index<Cells.Num();++Index)
        {if(Cells[Index].Box&&Cells[Index].Box->IsVisible()&&Cells[Index].Box->GetCachedGeometry().IsUnderLocation(Pointer)){Next=Index;break;}}
        if(Next==INDEX_NONE&&Cells.IsValidIndex(PinnedHover)){Next=PinnedHover;}
    }
    if(Next!=HoveredIndex){HoveredIndex=Next;Refresh();}
}
const UWidget* ULDStatBar::Hovered(LDHUD::FActionDetailView& Out) const
{
    if(bShownNow&&bAttributes&&AttrCells.IsValidIndex(AttrHovered)&&HoveredIndex==INDEX_NONE)
    {
        const FAttrCell& Cell=AttrCells[AttrHovered];
        const FLDCombatAttributeRowView* Published=AttributeOf(Cell.Attribute);
        if(!Published){return nullptr;}
        const LDHUD::FAttributeRowView View=LDHUD::AttributeRow(*Published);
        Out.Name=View.Name;Out.Summary=View.Final;Out.Body=View.Breakdown;
        return Cell.Box;
    }
    if(!bShownNow||!Usable()||!Cells.IsValidIndex(HoveredIndex)){return nullptr;}
    const TArray<const FLDDemoStatDefinition*> StatOrder=LatestContent->StatsInDesignOrder();
    if(!StatOrder.IsValidIndex(HoveredIndex)){return nullptr;}
    const FCell& Cell=Cells[HoveredIndex];
    const int32 CurrentPts=EffectivePoints(HoveredIndex);
    const LDHUD::FStatRowView View=LDHUD::StatRow(*StatOrder[HoveredIndex],CurrentPts,LatestContent);
    const int32 AddStep=FMath::Max(1,ResolveAllocateStep(HoveredIndex));
    const int32 RefundStep=FMath::Max(1,ResolveRefundStep(HoveredIndex));
    const LDHUD::FStatHoverPreview Preview=LDHUD::StatHoverPreview(*StatOrder[HoveredIndex],CurrentPts,AddStep,RefundStep,
        Gate.bCanRespec,LatestAttributes,*LatestContent);
    Out.Name=View.Name;
    Out.Summary=Preview.DeltaSummary+TEXT(" · ")+LDTerms::UnspentPoints(FMath::Max<int64>(0,Available()));
    TArray<FString> Lines=Preview.AttributeLines;
    Lines.Add(Preview.KeystoneProgress);
    if(!Preview.KeystoneRule.IsEmpty()){Lines.Add(Preview.KeystoneRule);}
    Lines.Add(TEXT("Shift+点击：±5 点 · Ctrl+点击：凑满 18 点关键石"));
    if(Preview.bKeystoneLossWarning&&!Preview.RefundWarning.IsEmpty()){Lines.Add(Preview.RefundWarning);}
    Out.Body=FString::Join(Lines,TEXT("\n"));
    if(Available()>0&&!Gate.bCanAllocate){Out.Body+=TEXT("\n\n现在不能加点：")+Gate.AllocateBlocked;}
    return Cell.Box;
}

int32 ULDStatBar::ResolveAllocateStep(int32 Index) const
{
    if(!Cells.IsValidIndex(Index)||!LatestContent){return 1;}
    const int64 Free=Available();
    if(Free<=0){return 1;}
    const FModifierKeysState Mods=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetModifierKeys():FModifierKeysState();
    const int32 Current=EffectivePoints(Index);
    const int32 Threshold=int32(FMath::Max<int64>(1,LatestContent->Parameters.KeystoneThresholdPoints));
    if(Mods.IsControlDown())
    {
        const int32 Needed=Threshold>Current?Threshold-Current:1;
        return int32(FMath::Min<int64>(Free,Needed));
    }
    if(Mods.IsShiftDown())
    {
        return int32(FMath::Min<int64>(Free,5));
    }
    return 1;
}

int32 ULDStatBar::ResolveRefundStep(int32 Index) const
{
    if(!Cells.IsValidIndex(Index)){return 1;}
    const int32 Current=EffectivePoints(Index);
    if(Current<=0){return 1;}
    const FModifierKeysState Mods=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetModifierKeys():FModifierKeysState();
    if(Mods.IsShiftDown()){return FMath::Min(Current,5);}
    return 1;
}

bool ULDStatBar::CanAllocate(int32 Index) const
{
    return Usable()&&Cells.IsValidIndex(Index)&&Gate.bCanAllocate&&Available()>0
        &&EffectivePoints(Index)<MAX_uint8;
}
bool ULDStatBar::Allocate(int32 Index,int32 RequestedCount)
{
    const auto Refuse=[this](const FString& Why){if(Notify){Notify(FString(TEXT("加点："))+Why);}return false;};
    if(!Usable()){return Refuse(TEXT("属性数据同步中。"));}
    if(!Cells.IsValidIndex(Index)){return false;}
    if(!Gate.bCanAllocate){return Refuse(Gate.AllocateBlocked);}
    if(Available()<=0){return Refuse(TEXT("没有未分配点数了。"));}
    const int32 Room=MAX_uint8-EffectivePoints(Index);
    if(Room<=0){return Refuse(TEXT("这一项加不下了。"));}
    const int32 Step=FMath::Clamp(RequestedCount>0?RequestedCount:ResolveAllocateStep(Index),1,int32(FMath::Min<int64>(Available(),Room)));
    Queued[Index]+=Step;
    Flush();
    Refresh();
    return true;
}
bool ULDStatBar::Refund(int32 Index,int32 RequestedCount)
{
    const auto Refuse=[this](const FString& Why){if(Notify){Notify(FString(TEXT("退点："))+Why);}return false;};
    if(!Usable()){return Refuse(TEXT("属性数据同步中。"));}
    if(!Cells.IsValidIndex(Index)){return false;}
    if(!Gate.bCanRespec){return Refuse(Gate.RespecBlocked);}
    const int32 Current=EffectivePoints(Index);
    if(Current<=0){return Refuse(LDHUD::GrowthReasonText(ELDGrowthReason::NothingToRespec));}
    const int32 Step=FMath::Clamp(RequestedCount>0?RequestedCount:ResolveRefundStep(Index),1,Current);
    Queued[Index]-=Step;
    Flush();
    Refresh();
    return true;
}
FReply ULDStatBar::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Event.GetEffectingButton()==EKeys::RightMouseButton&&bShownNow)
    {
        for(int32 Index=0;Index<Cells.Num();++Index)
        {
            if(Cells[Index].Box&&Cells[Index].Box->IsVisible()&&Cells[Index].Box->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition()))
            {
                Refund(Index);
                return FReply::Handled();
            }
        }
    }
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}
FReply ULDStatBar::NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event)
{
    // The wheel over a stat icon: up adds points, down takes points back (rest only); elsewhere the bar lets it pass.
    int32 Under=INDEX_NONE;
    if(bShownNow)
    {
        for(int32 Index=0;Index<Cells.Num();++Index)
        {
            if(Cells[Index].Box&&Cells[Index].Box->IsVisible()&&Cells[Index].Box->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition()))
            {Under=Index;break;}
        }
    }
    if(Under==INDEX_NONE||Event.GetWheelDelta()==0.f){return Super::NativeOnMouseWheel(Geometry,Event);}
    if(Event.GetWheelDelta()>0.f){Allocate(Under);}else{Refund(Under);}
    return FReply::Handled();
}
void ULDStatBar::FlushNow()
{
    Flush(true);
}
void ULDStatBar::Flush(bool bIgnoreWindow)
{
    // Leading-edge + 120ms trailing coalescing: first click sends immediately; rapid bursts within 120ms or while a request
    // is in flight coalesce locally with optimistic UI and send as one authoritative batch.
    if(PendingRequest||AwaitRevision!=0||StatAbsSum(Queued)==0||!Usable()){return;}
    if(PeekRequestId&&PeekRequestId()==0){return;}
    const double Now=UiNow();
    if(!bIgnoreWindow&&Now>0.&&LastBatchSentAt>=0.&&Now-LastBatchSentAt<StatCoalesceSeconds){return;}
    bool bHasPositive=false,bHasNegative=false;
    for(const int32 Delta:Queued){if(Delta>0){bHasPositive=true;}else if(Delta<0){bHasNegative=true;}}
    const TArray<const FLDDemoStatDefinition*> StatOrder=LatestContent->StatsInDesignOrder();
    if(bHasPositive)
    {
        if(!Gate.bCanAllocate){if(Notify){Notify(FString(TEXT("加点："))+Gate.AllocateBlocked);}DropWaiting();return;}
        FLDBuildCommand Command;Command.Kind=ELDBuildCommandKind::AllocatePoints;
        for(int32 Index=0;Index<Cells.Num();++Index)
        {
            if(!StatOrder.IsValidIndex(Index)||Latest.StatPoints[Index].StatId!=FName(*StatOrder[Index]->Id))
            {
                if(Notify){Notify(TEXT("加点：没有提交：属性数据与内容目录不一致。"));}
                DropWaiting();return;
            }
            const int32 SendCount=FMath::Clamp(Queued[Index],0,int32(MAX_uint8));
            Command.PointCounts.Add(uint8(SendCount));
        }
        for(int32 Index=0;Index<Cells.Num();++Index)
        {
            if(Queued[Index]>0){const int32 SentCount=FMath::Min(Queued[Index],int32(MAX_uint8));InFlight[Index]+=SentCount;Queued[Index]-=SentCount;}
        }
        LastBatchSentAt=Now;
        Send(Command);
        return;
    }
    if(bHasNegative)
    {
        if(!Gate.bCanRespec){if(Notify){Notify(FString(TEXT("退点："))+Gate.RespecBlocked);}DropWaiting();return;}
        FLDBuildCommand Command;Command.Kind=ELDBuildCommandKind::Respec;
        for(int32 Index=0;Index<Cells.Num();++Index)
        {
            if(!StatOrder.IsValidIndex(Index)||Latest.StatPoints[Index].StatId!=FName(*StatOrder[Index]->Id))
            {
                if(Notify){Notify(TEXT("退点：没有提交：属性数据与内容目录不一致。"));}
                DropWaiting();return;
            }
            const int32 RefundCount=FMath::Clamp(-Queued[Index],0,LDHUD::StatPointsOf(Latest,Cells[Index].StatId));
            Command.PointCounts.Add(uint8(RefundCount));
        }
        bool bAny=false;
        for(const uint8 C:Command.PointCounts){bAny|=C>0;}
        if(!bAny){for(int32& Delta:Queued){if(Delta<0){Delta=0;}}return;}
        for(int32 Index=0;Index<Cells.Num();++Index)
        {
            if(Queued[Index]<0)
            {
                const int32 RefundCount=int32(Command.PointCounts[Index]);
                InFlight[Index]-=RefundCount;
                Queued[Index]+=RefundCount;
                if(Queued[Index]<0){Queued[Index]=0;}
            }
        }
        LastBatchSentAt=Now;
        Send(Command);
    }
}
void ULDStatBar::DropWaiting()
{
    for(int32& Count:Queued){Count=0;}
    for(int32& Count:InFlight){Count=0;}
    AwaitRevision=0;
}
bool ULDStatBar::Send(FLDBuildCommand Command)
{
    if(!SubmitBuild)
    {
        UE_LOG(LogLDUI,Error,TEXT("LD_STAT_SUBMIT_UNAVAILABLE The growth build entry is not connected to the stat bar."));
        DropWaiting();
        if(Notify){Notify(TEXT("成长入口不可用，本次没有提交。"));}
        return false;
    }
    Command.ExpectedBuildRevision=Latest.BuildRevision;Command.CatalogDigest=Latest.CatalogDigest;
    // A Listen Host answers inside the submit call, so the pending entry is keyed to the id it will receive first.
    const uint64 Expected=PeekRequestId?PeekRequestId():0;
    const uint32 Before=Answers;PendingRequest=Expected;
    ELDGrowthReason Reason=ELDGrowthReason::None;
    const uint64 Request=SubmitBuild(Command,Reason);
    if(!Request)
    {
        if(PendingRequest==Expected){PendingRequest=0;}
        DropWaiting();
        const FString Why=LDHUD::GrowthReasonText(Reason);
        if(Notify){Notify(TEXT("加点：没有提交：")+(Why.IsEmpty()?FString(TEXT("原因未知")):Why));}
        return false;
    }
    if(Answers==Before){PendingRequest=Request;}
    return true;
}
void ULDStatBar::ShowFinal(const FLDBuildReceipt& Receipt)
{
    const uint64 Request=PendingRequest;
    PendingRequest=0;
    if(Receipt.Header.Outcome==ELDReceiptOutcome::Applied)
    {
        // Applied points count as waiting until the own view carries them; then the queued ones follow.
        AwaitRevision=Receipt.Header.Revisions.bHasBuild?Receipt.Header.Revisions.BuildRevision:Latest.BuildRevision+1;
        AwaitSince=UiNow();
        if(AllocationApplied&&LDHUD::AppliedStatFeedback(Receipt,LatestMatch,LatestLife,Request,Cells.Num()))
        {AllocationApplied(Receipt);}
        return;
    }
    DropWaiting();
    if(Notify){Notify(TEXT("加点：")+LDHUD::BuildReceiptText(Receipt,LatestContent));}
}
// The pending entry clears exactly when the controller releases its in-flight request (like the storage's).
void ULDStatBar::HandleSubmit(const FLDSubmitResult& Result)
{
    if(Result.Family!=ELDGrowthFamily::Build||!PendingRequest||Result.RequestId!=PendingRequest||Result.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Status)
    {
    case ELDAdmissionStatus::AcceptedPending: return;
    case ELDAdmissionStatus::AdmissionRejected:
        if(Result.bSequenceReserved){return;}
        PendingRequest=0;DropWaiting();
        if(Notify){Notify(TEXT("加点：没有受理：")+LDHUD::GrowthReasonText(LDGrowthWire::Reason(Result.Reason)));}
        return;
    case ELDAdmissionStatus::Final:
        if(Result.bHasBuildReceipt){ShowFinal(Result.BuildReceipt);return;}
        PendingRequest=0;DropWaiting();
        if(Notify){Notify(TEXT("加点：请求已结束，请以当前状态为准。"));}
        return;
    default:
        if(Notify){Notify(TEXT("加点：无法识别服务器的答复，请以当前状态为准。"));}
        return;
    }
}
void ULDStatBar::HandleReceipt(const FLDBuildReceiptQueryResult& Result)
{
    if(Result.Query.Family!=ELDGrowthFamily::Build||!PendingRequest||Result.Query.RequestId!=PendingRequest||Result.Query.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Lookup)
    {
    case ELDReceiptLookup::Pending: return;
    case ELDReceiptLookup::Final:
        if(Result.bHasReceipt){ShowFinal(Result.Receipt);return;}
        PendingRequest=0;DropWaiting();
        if(Notify){Notify(TEXT("加点：回执缺少结果，请以当前状态为准。"));}
        return;
    case ELDReceiptLookup::ReceiptExpired:
        PendingRequest=0;DropWaiting();
        if(Notify){Notify(TEXT("加点：回执已过期，请以当前状态为准。"));}
        return;
    case ELDReceiptLookup::NotFound:
        if(Notify){Notify(TEXT("加点：服务器没有找到这次请求，请以当前状态为准。"));}
        return;
    default:
        if(Notify){Notify(TEXT("加点：回执查询无效，请以当前状态为准。"));}
        return;
    }
}
