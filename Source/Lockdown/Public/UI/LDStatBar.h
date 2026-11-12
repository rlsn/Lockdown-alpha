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
// carries the previous one; nothing is predicted: the corner shows the view's points plus the waiting ones as "+n". The
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
    // The "+" of the stat at Index (design order): queues one point and sends it as soon as it may. False with a toast when
    // refused locally (no data, closed stage, no unspent point left).
    bool Allocate(int32 Index);
    // Stat icons <-> attribute view; each match starts on the stat icons.
    void ToggleView();
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
    void PinHover(int32 Index){PinnedHover=Index;}
    TFunction<uint64(const FLDBuildCommand&,ELDGrowthReason&)> SubmitBuild;
    TFunction<uint64()> PeekRequestId;
    TFunction<void(const FString&)> Notify;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
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
    UPROPERTY() TObjectPtr<USizeBox> AttrFrame;
    UPROPERTY() TObjectPtr<UUniformGridPanel> AttrGrid;
    // Every widget built for the cells, kept alive with the tree.
    UPROPERTY() TArray<TObjectPtr<UWidget>> Built;
    TArray<FCell> Cells;
    TArray<FAttrCell> AttrCells;
    bool bAttributes=false;
    int32 AttrHovered=INDEX_NONE;
    FLDProgressionView Latest;
    uint32 LatestMatch=0,LatestLife=0;
    const FLDDemoContentDefinitions* LatestContent=nullptr;
    TArray<FLDCombatAttributeRowView> LatestAttributes;
    LDHUD::FBuildGateView Gate;
    bool bShownNow=false;
    int32 HoveredIndex=INDEX_NONE;
    int32 PinnedHover=INDEX_NONE;
    // Per stat in design order: clicked but not sent, and sent but not yet in the own view.
    TArray<int32> Queued;
    TArray<int32> InFlight;
    uint64 PendingRequest=0;
    uint32 Answers=0;
    // An applied allocation waits for the own view to carry this build revision before the next one goes out.
    uint64 AwaitRevision=0;
    double AwaitSince=0;
    bool Usable() const;
    int64 Available() const;
    void BuildCells();
    void Refresh();
    void RefreshAttributes();
    const FLDCombatAttributeRowView* AttributeOf(uint8 Attribute) const;
    void Flush();
    void DropWaiting();
    bool Send(FLDBuildCommand Command);
    void ShowFinal(const FLDBuildReceipt& Receipt);
    double UiNow() const;
};
