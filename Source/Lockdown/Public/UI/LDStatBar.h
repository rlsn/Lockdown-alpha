#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDProgressionRules.h"
#include "UI/LDHUDModel.h"
#include "LDStatBar.generated.h"
class UBorder;
class UHorizontalBox;
class UImage;
class USizeBox;
class UUniformGridPanel;
class UTextBlock;
class ULDCommandButton;
struct FLDDemoContentDefinitions;

// The own point-bought stats in the HUD band's identity block: one icon per stat in the stats' design order, its allocated
// points on the corner. Hovering a stat raises a "+" right above its icon while points are unspent; a click on it commits one
// point at once (no staging, no confirm). Clicks that come while an allocation is on its way, or while another build request
// holds the owner's single in-flight slot, are queued locally and sent together as the next AllocatePoints once the own view
// carries the previous one; nothing is predicted: the corner shows the view's points plus the waiting ones as "+n". The mouse
// wheel over a stat adds one point (up, like its "+") or, in a rest build window only, takes one back (down: a one-point
// Respec); on the field points only go in. The
// server decides every point; a refusal drops the waiting points and says why. The hovered stat's popup (the HUD's slot
// popup, Hovered) lists each attribute the stat raises with its current value and the gain of one point.
// The HUD's 属性 entry turns the block into the attribute view and back (ToggleView): one icon and the final value per
// attribute of LDHUD::AttributeViewOrder, in a grid of the stat row's height; the hovered one's popup names it with its sources.
UCLASS()
class LOCKDOWN_API ULDStatBar : public UUserWidget
{
    GENERATED_BODY()
public:
    // Called on every HUD refresh. Stage and LifePhase judge what the server would refuse (LDHUD::BuildGate); Attributes are
    // the own projection's final rows (empty while none is published) for the popup's current values; bShown: the gameplay
    // HUD shows for the own match.
    void Present(const FLDProgressionView* View,uint32 Match,uint32 Life,LDProgression::EMatchStage Stage,ELDLifePhase LifePhase,
        const FLDDemoContentDefinitions* Content,const TArray<FLDCombatAttributeRowView>& Attributes,bool bShown);
    // The "+" of the stat at Index (design order): queues points (Shift=+5, Ctrl=to 18 keystone when Count<=0) and coalesces
    // rapid bursts within 120ms. False with a toast when refused locally (no data, closed stage, no unspent point left).
    bool Allocate(int32 Index,int32 RequestedCount=0);
    // Whether Allocate(Index) would queue a point now; no notice (the held "+" asks before each repeat).
    bool CanAllocate(int32 Index) const;
    // Refunds points of the stat at Index back to unspent (Shift=-5 when Count<=0; rest build window only), coalesced smoothly.
    bool Refund(int32 Index,int32 RequestedCount=0);
    int32 EffectivePoints(int32 Index) const;
    int64 EffectiveUnspent() const {return Available();}
    void FlushNow();
    // Stat icons <-> attribute view; each match starts on the stat icons.
    void ToggleView();
    void SetDetailsHeld(bool bHeld);
    bool ShowsAttributes() const {return bAttributes;}
    // The hovered stat's cell (its "+" and icon) with its popup lines, or null.
    const UWidget* Hovered(LDHUD::FActionDetailView& Out) const;
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDBuildReceiptQueryResult& Result);
    bool HasPendingRequest() const {return PendingRequest!=0;}
    // Points clicked and not yet in the own view (queued or on their way).
    int32 Waiting() const;
    int32 StatCount() const {return Cells.Num();}
    // Probes: the "+" of the stat at Index, and a hover kept without a pointer (INDEX_NONE releases it).
    ULDCommandButton* PlusButton(int32 Index) const;
    // Probes and tests: the stat icon cell at Index (its box), or null.
    const UWidget* CellBox(int32 Index) const {return Cells.IsValidIndex(Index)?Cells[Index].Box:nullptr;}
    void PinHover(int32 Index){PinnedHover=Index;}
    int32 HoverIndex() const {return HoveredIndex;}
    TFunction<uint64(const FLDBuildCommand&,ELDGrowthReason&)> SubmitBuild;
    TFunction<uint64()> PeekRequestId;
    TFunction<void(const FString&)> Notify;
    TFunction<void(const FLDBuildReceipt&)> AllocationApplied;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
private:
    struct FCell
    {
        FName StatId;
        UWidget* Box=nullptr;
        UBorder* Face=nullptr;
        UImage* Picture=nullptr;
        UTextBlock* Glyph=nullptr;
        UTextBlock* Points=nullptr;
        ULDCommandButton* Plus=nullptr;
        FString AppliedIcon;
        int32 AppliedHover=-1;
    };
    struct FAttrCell
    {
        uint8 Attribute=0;
        UWidget* Box=nullptr;
        UTextBlock* Value=nullptr;
    };
    UPROPERTY() TObjectPtr<UHorizontalBox> Row;
    UPROPERTY() TObjectPtr<UTextBlock> Syncing;
    UPROPERTY() TObjectPtr<UWidget> AttrFrame;
    UPROPERTY() TObjectPtr<UUniformGridPanel> AttrGrid;
    // Every widget built for the cells, kept alive with the tree.
    UPROPERTY() TArray<TObjectPtr<UWidget>> Built;
    TArray<FCell> Cells;
    TArray<FAttrCell> AttrCells;
    bool bAttributes=false;
    bool bDetailsHeld=false;
    int32 AttrHovered=INDEX_NONE;
    FLDProgressionView Latest;
    uint32 LatestMatch=0,LatestLife=0;
    const FLDDemoContentDefinitions* LatestContent=nullptr;
    TArray<FLDCombatAttributeRowView> LatestAttributes;
    LDHUD::FBuildGateView Gate;
    bool bShownNow=false;
    int32 HoveredIndex=INDEX_NONE;
    int32 PinnedHover=INDEX_NONE;
    // Per stat in design order: signed delta (+allocate / -refund) coalesced locally, and sent delta not yet in own view.
    TArray<int32> Queued;
    TArray<int32> InFlight;
    uint64 PendingRequest=0;
    uint32 Answers=0;
    // An applied allocation/respec waits for the own view to carry this build revision before the next batch goes out.
    uint64 AwaitRevision=0;
    double AwaitSince=0;
    double LastBatchSentAt=-1.;
    int32 ResolveAllocateStep(int32 Index) const;
    int32 ResolveRefundStep(int32 Index) const;
    bool Usable() const;
    int64 Available() const;
    void BuildCells();
    void Refresh();
    void RefreshAttributes();
    const FLDCombatAttributeRowView* AttributeOf(uint8 Attribute) const;
    void Flush(bool bIgnoreWindow=false);
    void DropWaiting();
    bool Send(FLDBuildCommand Command);
    void ShowFinal(const FLDBuildReceipt& Receipt);
    double UiNow() const;
};
