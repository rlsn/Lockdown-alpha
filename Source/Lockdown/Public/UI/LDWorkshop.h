#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchTimeline.h"
#include "UI/LDHUDModel.h"
#include "UI/LDComponentStorage.h"
#include "UI/LDChoicesPanel.h"
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
struct FLDDemoContentDefinitions;

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
        // The tray's size (its rows stack upward in the pending area) and how many decisions wait there.
        FVector2D TrayDesired=FVector2D::ZeroVector;
        int32 Pending=0;
        // Low quality: the backdrop only darkens, without the blur.
        bool bLowQuality=false;
        // The tray's centre stage (the active offer, or the pink final mod page) replaces the board; the NPC beside the
        // player (its name rides on the phase bar) puts its services on the board.
        bool bStage=false;
        bool bFinalStage=false;
        FString NpcName;
        bool bServices=false;
    };
    void Present(const FInput& Input);
    bool IsOpen() const {return Mode!=EMode::Closed;}
    EMode GetMode() const {return Mode;}
    bool IsHit(FVector2D Absolute) const;
    // The storage's board and library rectangles (absolute), from the last layout.
    ULDComponentStorage::FWorkshopLayout StorageLayout() const;
    // The 构筑 tray's distance from the bottom-right corner, so its rows rise inside the pending area.
    FVector2D TrayInset() const {return FVector2D(TrayRight,TrayBottom);}
    // Where the rest actions (重置训练, 准备完毕) sit: right of the phase bar, left of the close button (local units).
    FVector2D ActionsAnchor() const {return ActionsAt;}
    // The tray's stage, dock and service places (absolute), from the last layout.
    ULDChoicesPanel::FWorkshopPlaces Places() const;
    TWeakObjectPtr<ULDComponentStorage> Storage;
    TWeakObjectPtr<ULDStatBar> StatBar;
    TWeakObjectPtr<ULDChoicesPanel> Choices;
    TFunction<void()> RequestClose;
    // The workshop's own "+" of the stat at Index (design order); the same one-click allocation as the band (ULDStatBar).
    ULDCommandButton* StatPlus(int32 Index) const {return StatRows.IsValidIndex(Index)?StatRows[Index].Plus:nullptr;}
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
private:
    struct FStatRow
    {
        FName StatId;
        UWidget* Row=nullptr;
        UImage* Picture=nullptr;
        UTextBlock* Glyph=nullptr;
        UTextBlock* Name=nullptr;
        UWidget* Meter=nullptr;
        UTextBlock* Points=nullptr;
        ULDCommandButton* Plus=nullptr;
        int32 Value=0;
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
        UTextBlock* Mana=nullptr;
        UTextBlock* Cooldown=nullptr;
        UTextBlock* Element=nullptr;
        // The locked final mod's icon over the head of what it binds.
        UImage* Final=nullptr;
        FName Active;
        FString AppliedIcon;
        TArray<ELDDemoElement> Elements;
        bool bEmpty=false;
    };
    struct FReaction
    {
        UBorder* Row=nullptr;
        UTextBlock* Pair=nullptr;
        UTextBlock* Name=nullptr;
        ELDDemoElement First=ELDDemoElement::None;
        ELDDemoElement Second=ELDDemoElement::None;
        bool bLit=false;
    };
    // Every rectangle of the current layout, in local units.
    struct FRects
    {
        FBox2D Phase,Character,Board,Capacity,Columns,Dock,Library,LibraryBody,Pending,Region,Stage;
        FBox2D Column[LDHUD::StorageColumnCount];
        float HeadHeight=0.f;
        float HeadIcon=100.f;
    };
    UPROPERTY() TObjectPtr<UCanvasPanel> Canvas;
    UPROPERTY() TObjectPtr<UBackgroundBlur> Blur;
    UPROPERTY() TObjectPtr<UBorder> Shade;
    UPROPERTY() TObjectPtr<UWidget> PhasePanel;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseClock;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseRest;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseStep;
    UPROPERTY() TObjectPtr<ULDCommandButton> CloseButton;
    UPROPERTY() TObjectPtr<UWidget> CharacterPanel;
    UPROPERTY() TObjectPtr<UBorder> Portrait;
    UPROPERTY() TObjectPtr<UTextBlock> PortraitGlyph;
    UPROPERTY() TObjectPtr<UTextBlock> LevelBadge;
    UPROPERTY() TObjectPtr<UTextBlock> HolderName;
    UPROPERTY() TObjectPtr<UTextBlock> LevelLine;
    UPROPERTY() TObjectPtr<UBorder> PointsBox;
    UPROPERTY() TObjectPtr<UTextBlock> PointsText;
    UPROPERTY() TObjectPtr<UVerticalBox> StatList;
    UPROPERTY() TObjectPtr<UVerticalBox> AttributeList;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> AttributeNames;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> AttributeValues;
    UPROPERTY() TObjectPtr<UWidget> BoardPanel;
    UPROPERTY() TObjectPtr<UTextBlock> CapacityCount;
    UPROPERTY() TObjectPtr<UTextBlock> CapacityUnlock;
    UPROPERTY() TObjectPtr<UWidget> CapacityTitle;
    UPROPERTY() TObjectPtr<UTextBlock> LockBadge;
    UPROPERTY() TObjectPtr<UWidget> LibraryPanel;
    UPROPERTY() TObjectPtr<UTextBlock> LibraryCount;
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
    UPROPERTY() TObjectPtr<UTextBlock> DockWhereTitle;
    UPROPERTY() TObjectPtr<UTextBlock> DockWhere;
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
    EMode Mode=EMode::Closed;
    FInput Latest;
    FLDProgressionView View;
    FRects Rects;
    FVector2D LastSize=FVector2D::ZeroVector;
    FVector2D ActionsAt=FVector2D::ZeroVector;
    float TrayRight=0.f,TrayBottom=0.f;
    int32 TagFilter=INDEX_NONE;
    double OpenedAt=-1.;
    // What the dock shows; kept when the pointer leaves, so the eye has a steady place to read.
    FString DockKey;
    uint8 DockKindValue=0;
    double UiNow() const;
    void Layout(FVector2D Size);
    void Place(UWidget* Widget,const FBox2D& Box);
    void RefreshCharacter();
    void RefreshHeads();
    void RefreshPhase();
    void RefreshDock(FVector2D Pointer);
    void ShowComponentDock(FName Component,const ULDComponentStorage::FFocus* Focus);
    void ShowTextDock(const FString& Key,const FString& Name,const FString& Summary,const FString& Body,const FString& IconId,const FString& Glyph);
    float LibraryIconFor(const FBox2D& Box) const;
};
