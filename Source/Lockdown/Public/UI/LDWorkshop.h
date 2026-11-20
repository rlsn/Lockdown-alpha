#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchTimeline.h"
#include "UI/LDHUDModel.h"
#include "UI/LDComponentStorage.h"
#include "UI/LDChoicesPanel.h"
#include "UI/LDWorkshopModel.h"
#include "UI/LDWorkshopPreview.h"
#include "LDWorkshop.generated.h"
class UBorder;
class UCanvasPanel;
class UCanvasPanelSlot;
class UHorizontalBox;
class UImage;
class URichTextBlock;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UWrapBox;
class UBackgroundBlur;
class ULDCommandButton;
class ULDStatBar;
class ULDChoicesPanel;
class ULDPaintLayer;
class UTexture2D;
struct FLDDemoContentDefinitions;
class FLDDemoContentSnapshot;

// The build workshop (APP_SPEC §7 构筑): the 构筑 tray's full view, open while the tray is open. Full screen in a rest's build,
// payout and return steps and in the own death wait (the world darkens and blurs but never pauses); elsewhere it keeps to the
// bottom 46% of the screen. It lays out the phase bar, the character column (stats, derived attributes, element reactions),
// the capacity bar, the rule board, the library and the pending area, and the info dock, and hands the board and library
// rectangles to the component storage and the pending rectangle to the 构筑 tray, which keep every rule, drop, request and
// receipt of their own. Presentation only: it reads the own view, the storage's focus and the tray's hovered card.
UCLASS()
class LOCKDOWN_API ULDWorkshop : public UUserWidget
{
    GENERATED_BODY()
public:
    enum class EMode : uint8 { Closed, Full, Field };
    struct FInput
    {
        EMode Mode=EMode::Closed;
        const FLDProgressionView* Progression=nullptr;
        uint32 Match=0;
        const FLDDemoContentDefinitions* Content=nullptr;
        const FLDDemoContentSnapshot* ContentSnapshot=nullptr;
        const FLDCombatOwnerView* Combat=nullptr;
        LDProgression::EMatchStage Stage=LDProgression::EMatchStage::Closed;
        bool bFinalField=false;
        LDHUD::FCardStats Stats;
        LDHUD::FSlotKeys Keys;
        FString DashKey;
        FString HolderName;
        FLinearColor HolderColor=FLinearColor::Gray;
        LDHUD::FLevelView Level;
        TArray<FLDCombatAttributeRowView> Attributes;
        // The phase bar: the rest and its step with the step's span, or the own death wait.
        bool bRest=false;
        uint8 RestIndex=0;
        ELDRestStep Step=ELDRestStep::None;
        int64 StepStartedAtMs=0;
        int64 StepEndsAtMs=0;
        bool bClock=false;
        int64 NowMs=0;
        bool bDead=false;
        FString DeathTitle;
        FString DeathCountdown;
        FString DeathClock;
        float DeathProgress=0.f;
        LDHUD::FDamageListView DeathRecap;
        // The tray's size (its rows stack upward in the pending area) and how many decisions wait there.
        FVector2D TrayDesired=FVector2D::ZeroVector;
        int32 Pending=0;
        // Low quality: the backdrop only darkens, without the blur.
        bool bLowQuality=false;
        // The tray's centre stage (the active offer, or the pink final mod page) replaces the board; the NPC beside the
        // player (its name rides on the phase bar) puts its services on the board.
        bool bStage=false;
        bool bFinalStage=false;
        // Spec §3.13: the open shop shelf takes the board's place (the library stays to sell from).
        bool bShop=false;
        bool bServices=false;
        // The respec takes the foot of the character column.
        bool bCharacterServices=false;
    };
    void Present(const FInput& Input);
    bool IsOpen() const {return Mode!=EMode::Closed;}
    EMode GetMode() const {return Mode;}
    // Present still fades the opening in (LDHUDStyle::MotionWorkshopOpen); the HUD presents every sample meanwhile.
    bool IsOpening() const;
    bool IsHit(FVector2D Absolute) const;
    // The storage's board and library rectangles (absolute), from the last layout.
    ULDComponentStorage::FWorkshopLayout StorageLayout() const;
    // The 构筑 tray's distance from the bottom-right corner, so its rows rise inside the pending area.
    FVector2D TrayInset() const {return FVector2D(TrayRight,TrayBottom);}
    // The width the tray rows wrap at inside the pending area (0: no limit).
    float TrayWrapWidth() const {return TrayWrap;}
    // The most height the tray can take without crossing the workshop boundary.
    float TrayHeightLimit() const {return TrayLimit;}
    // Where the rest actions (重置训练, 准备完毕) sit: right of the phase bar, left of the close button (local units).
    FVector2D ActionsAnchor() const {return ActionsAt;}
    // The tray's stage, dock and service places (absolute), from the last layout.
    ULDChoicesPanel::FWorkshopPlaces Places() const;
    TWeakObjectPtr<ULDComponentStorage> Storage;
    TWeakObjectPtr<ULDStatBar> StatBar;
    TWeakObjectPtr<ULDChoicesPanel> Choices;
    TFunction<void()> RequestClose;
    // The local text-size preference: rescales every text of the tree once per change; texts built later start scaled.
    void ApplyTextScale(float Scale);
    // The workshop's own "+" of the stat at Index (design order); the same one-click allocation as the band (ULDStatBar).
    ULDCommandButton* StatPlus(int32 Index) const {return StatRows.IsValidIndex(Index)?StatRows[Index].Plus:nullptr;}
    void AllocationApplied(const FLDBuildReceipt& Receipt);
    void FinalModApplied(const FLDBuildReceipt& Receipt,const FLDFinalModSelection& Selection);
    void InterruptFinalFeedback();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    // The trainer's forget: a right click on a skill head (ULDChoicesPanel::ForgetAtColumn).
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    // The wheel over a stat row of the character column: up adds a point, down takes one back (rest only), like the band.
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
private:
    friend class FLDHUDCardFacesTest;
    struct FStatRow
    {
        FName StatId;
        UWidget* Row=nullptr;
        UImage* Picture=nullptr;
        UTextBlock* Glyph=nullptr;
        UTextBlock* Name=nullptr;
        UWidget* Icon=nullptr;
        UWidget* Meter=nullptr;
        UTextBlock* Points=nullptr;
        ULDCommandButton* Plus=nullptr;
        int32 Value=0;
        // Flash only after the applied revision arrives in the owner view.
        double AddedAt=-100.;
        uint64 PendingFlashRevision=0;
        int32 FlashCell=INDEX_NONE;
        FString AppliedIcon;
    };
    struct FHead
    {
        UWidget* Box=nullptr;
        USizeBox* IconBox=nullptr;
        UBorder* IconFace=nullptr;
        UImage* Picture=nullptr;
        UTextBlock* Glyph=nullptr;
        UTextBlock* Key=nullptr;
        UTextBlock* Name=nullptr;
        UTextBlock* Momentum=nullptr;
        UTextBlock* Cooldown=nullptr;
        UTextBlock* Element=nullptr;
        // The locked final mod's icon over the head of what it binds.
        UImage* Final=nullptr;
        // The confirmed final mod stamps in over LDHUDStyle::MotionFinalLock.
        double FinalAt=-1.;
        FName Active;
        FString AppliedIcon;
        // The painted art's icon (none for the body and an empty slot).
        TWeakObjectPtr<UTexture2D> Icon;
        TArray<ELDDemoElement> Elements;
        bool bEmpty=false;
    };
    struct FReaction
    {
        UBorder* Row=nullptr;
        UTextBlock* Pair=nullptr;
        UTextBlock* FirstText=nullptr;
        UTextBlock* SecondText=nullptr;
        UTextBlock* Name=nullptr;
        ELDDemoElement First=ELDDemoElement::None;
        ELDDemoElement Second=ELDDemoElement::None;
        bool bLit=false;
    };
    // Every rectangle of the current layout, in local units.
    struct FRects
    {
        FBox2D Title,Phase,Character,Board,Capacity,Columns,Dock,Library,LibraryBody,Pending,Region,Stage,Shop;
        FBox2D Column[LDHUD::StorageColumnCount];
        float HeadHeight=0.f;
        float HeadTop=0.f;
        float HeadIcon=100.f;
    };
    UPROPERTY() TObjectPtr<UCanvasPanel> Canvas;
    UPROPERTY() TObjectPtr<UBackgroundBlur> Blur;
    UPROPERTY() TObjectPtr<UBorder> Shade;
    // Paints every surface, frame, meter and chip under the widgets (PaintBackdrop).
    UPROPERTY() TObjectPtr<ULDPaintLayer> Backdrop;
    UPROPERTY() TObjectPtr<USizeBox> PortraitFrame;
    UPROPERTY() TObjectPtr<UWidget> PhasePanel;
    // Mock 2: the page's own title "构筑" in the top-left corner, the phase pill right after it.
    UPROPERTY() TObjectPtr<UTextBlock> PageTitle;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseClock;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseRest;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseStep;
    UPROPERTY() TObjectPtr<ULDCommandButton> CloseButton;
    UPROPERTY() TObjectPtr<UWidget> CharacterPanel;
    UPROPERTY() TObjectPtr<UWidget> CharacterFit;
    UPROPERTY() TObjectPtr<UBorder> Portrait;
    UPROPERTY() TObjectPtr<UTextBlock> PortraitGlyph;
    UPROPERTY() TObjectPtr<UTextBlock> LevelBadge;
    UPROPERTY() TObjectPtr<UTextBlock> HolderName;
    UPROPERTY() TObjectPtr<UTextBlock> LevelLine;
    UPROPERTY() TObjectPtr<UBorder> PointsBox;
    // The foot of the character column the trainer's respec takes (no height without the trainer).
    UPROPERTY() TObjectPtr<USizeBox> RespecSpace;
    // The element reactions title and grid; the trainer's respec foot takes their place.
    UPROPERTY() TObjectPtr<UVerticalBox> ReactionBlock;
    UPROPERTY() TObjectPtr<UTextBlock> PointsText;
    UPROPERTY() TObjectPtr<UVerticalBox> StatList;
    UPROPERTY() TObjectPtr<UVerticalBox> AttributeList;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> AttributeNames;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> AttributeValues;
    UPROPERTY() TArray<TObjectPtr<UWidget>> AttributeHalves;
    UPROPERTY() TObjectPtr<UWidget> BoardPanel;
    UPROPERTY() TObjectPtr<UTextBlock> CapacityCount;
    UPROPERTY() TObjectPtr<UTextBlock> CapacityUnlock;
    UPROPERTY() TObjectPtr<UWidget> CapacityTitle;
    UPROPERTY() TObjectPtr<UTextBlock> LockBadge;
    UPROPERTY() TObjectPtr<UWidget> LibraryPanel;
    UPROPERTY() TObjectPtr<UTextBlock> LibraryCount;
    UPROPERTY() TObjectPtr<UWidget> LibraryPager;
    UPROPERTY() TObjectPtr<ULDCommandButton> LibraryPrevious;
    UPROPERTY() TObjectPtr<ULDCommandButton> LibraryNext;
    UPROPERTY() TObjectPtr<UTextBlock> LibraryPageText;
    UPROPERTY() TObjectPtr<UWrapBox> FilterRow;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> FilterButtons;
    UPROPERTY() TObjectPtr<UWidget> LibraryTitle;
    UPROPERTY() TObjectPtr<UWidget> DockLeft;
    UPROPERTY() TObjectPtr<UWidget> DockCentre;
    UPROPERTY() TObjectPtr<UWidget> DockRight;
    UPROPERTY() TObjectPtr<UWidget> PendingPanel;
    UPROPERTY() TObjectPtr<UTextBlock> PendingTitle;
    UPROPERTY() TObjectPtr<UWidget> DockPanel;
    UPROPERTY() TObjectPtr<USizeBox> DockIconBox;
    UPROPERTY() TObjectPtr<UBorder> DockIconFace;
    UPROPERTY() TObjectPtr<UImage> DockPicture;
    UPROPERTY() TObjectPtr<UTextBlock> DockGlyph;
    UPROPERTY() TObjectPtr<UTextBlock> DockKind;
    UPROPERTY() TObjectPtr<UTextBlock> DockTags;
    UPROPERTY() TObjectPtr<UTextBlock> DockName;
    UPROPERTY() TObjectPtr<UTextBlock> DockSummary;
    UPROPERTY() TObjectPtr<URichTextBlock> DockBody;
    UPROPERTY() TObjectPtr<UTextBlock> DockChips;
    // The death recap's icon chains (LDRecapChain), one row per recap row; empty and collapsed for every other dock.
    UPROPERTY() TObjectPtr<UVerticalBox> DockChains;
    TArray<LDHUD::FRecapChain> DockChainsShown;
    float DockChainsScale=0.f;
    UPROPERTY() TObjectPtr<UTextBlock> DockWhereTitle;
    UPROPERTY() TObjectPtr<UTextBlock> DockWhere;
    UPROPERTY() TObjectPtr<UTextBlock> DockPreviewTitle;
    UPROPERTY() TObjectPtr<URichTextBlock> DockPreviewBody;
    UPROPERTY() TObjectPtr<UHorizontalBox> DockPreviewPager;
    UPROPERTY() TObjectPtr<ULDCommandButton> DockPreviewPrevious;
    UPROPERTY() TObjectPtr<ULDCommandButton> DockPreviewNext;
    UPROPERTY() TObjectPtr<UTextBlock> DockPreviewPageText;
    UPROPERTY() TObjectPtr<UTextBlock> DockLinksTitle;
    UPROPERTY() TObjectPtr<UTextBlock> DockLinks;
    UPROPERTY() TObjectPtr<UTextBlock> DockNotesTitle;
    UPROPERTY() TObjectPtr<UTextBlock> DockNotes;
    UPROPERTY() TArray<TObjectPtr<UBorder>> ColumnBacks;
    UPROPERTY() TArray<TObjectPtr<UWidget>> Built;
    TArray<FStatRow> StatRows;
    TArray<FHead> Heads;
    TArray<FReaction> Reactions;
    TMap<UWidget*,UCanvasPanelSlot*> Slots;
    TMap<UWidget*,ESlateVisibility> ShownAs;
    float TextScale=1.f;
    // The artisan band's height at the top of the library (0 without it), from the last layout.
    // Where the capacity cells start (local x), from the last layout.
    float CapacityCellsAt=0.f,CapacityCellStep=45.f;
    float DockDividerX=0.f;
    void ScaleFresh(UTextBlock* Text) const;
    EMode Mode=EMode::Closed;
    EMode ClosingMode=EMode::Closed;
    FInput Latest;
    FLDProgressionView View;
    FLDFinalModSelection PendingFinalFeedback;
    uint64 PendingFinalRevision=0;
    TMap<FName,LDHUD::FCardConnections> LinkConnections;
    FRects Rects;
    FVector2D LastSize=FVector2D::ZeroVector;
    FVector2D ActionsAt=FVector2D::ZeroVector;
    float TrayRight=0.f,TrayBottom=0.f,TrayWrap=0.f,TrayLimit=0.f;
    int32 TagFilter=INDEX_NONE;
    int32 LibraryPage=0;
    LDWorkshop::FLibraryFit LibraryFit;
    double OpenedAt=-1.;
    double ClosingAt=-1.;
    FString LibraryWarning;
    // What the dock shows; kept when the pointer leaves, so the eye has a steady place to read.
    FString DockKey;
    LDWorkshopPreview::FResult Preview;
    int32 PreviewPage=0;
    uint8 DockKindValue=0;
    TWeakObjectPtr<UTexture2D> DockIcon;
    double UiNow() const;
    void Layout(FVector2D Size);
    void AnimateOpening();
    void PaintBackdrop(const LDUIPaint::FContext& Paint) const;
    // Whether the dock's right column has anything to show (where it sits, what it links with, what to watch).
    bool DockHasRight() const;
    void Place(UWidget* Widget,const FBox2D& Box);
    void RefreshCharacter();
    void RefreshHeads();
    void RefreshPhase();
    void RefreshDock(FVector2D Pointer);
    void RefreshPreviewPage();
    void ShowComponentDock(FName Component,const ULDComponentStorage::FFocus* Focus);
    void ShowTextDock(const FString& Key,const FString& Name,const FString& Summary,const FString& Body,const FString& IconId,const FString& Glyph);
    void ShowDockChains(const TArray<LDHUD::FRecapChain>& Chains);
};
