#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDProgressionRules.h"
#include "UI/LDHUDModel.h"
#include "LDComponentStorage.generated.h"
class UBorder;
class UCanvasPanel;
class UCanvasPanelSlot;
class UHorizontalBox;
class UImage;
class UOverlay;
class URichTextBlock;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UWrapBox;
class ULDCommandButton;
struct FLDDemoContentDefinitions;

// #227 component storage (APP_SPEC §7): the owned components not installed sit in a card stack right of the HUD band. Hovering
// the stack opens a grid of their icons above it and raises one row of equipped columns: 本体 left of the first Q/W/E slot,
// then each Q/W/E slot and D. Hovering any component shows its full description; the icons carry no words. Dragging a component onto a column or a host component installs it there (B18 asks for an element, C12 for the
// base it repeats); dragging an installed one back onto the storage uninstalls it, onto another place moves it (an uninstall,
// then an install once the view shows it). Local presentation over the own progression view; the server decides every
// command. #222: open in combat, while dead and in a PK; a closed match stage refuses with its reason.
UCLASS()
class LOCKDOWN_API ULDComponentStorage : public UUserWidget
{
    GENERATED_BODY()
public:
    // What the storage lines up with, in absolute (desktop) units from the HUD's last layout: the band (the stack sits right of
    // it) and each column's anchor in LDHUD::EStorageColumn order (a slot-sized place left of the first Q/W/E slot, the Q/W/E
    // slot showing that server slot, D). A column without an anchor (zero size) is not shown.
    struct FAnchors
    {
        FSlateRect Band;
        FSlateRect Columns[LDHUD::StorageColumnCount];
        bool bValid=false;
    };
    // Called on every HUD refresh. bShown: the gameplay HUD is shown for the own match.
    void Present(const FLDProgressionView* View,uint32 Match,LDProgression::EMatchStage Stage,ELDLifePhase LifePhase,
        const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats,const FAnchors& Anchors,bool bShown);
    // Over the stack, or over the open list, a column or the chooser, or dragging: world clicks belong to the storage.
    bool IsHit(FVector2D Absolute) const;
    bool IsExpanded() const {return bExpanded;}
    bool IsDragging() const {return Drag.bActive;}
    bool HasPendingRequest() const {return PendingRequest!=0||Follow.State!=EFollow::None;}
    // Back: closes the chooser or drops a drag without a command. False when there was neither.
    bool CancelInteraction();
    // The widgets the world labels keep clear of while they show.
    TArray<const UWidget*> ReservedWidgets() const;
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDBuildReceiptQueryResult& Result);
    TFunction<uint64(const FLDBuildCommand&,ELDGrowthReason&)> SubmitBuild;
    TFunction<uint64()> PeekRequestId;
    TFunction<void(const FString&)> Notify;
    // The end of a drag, also the probes' entry: Component let go over Column (and the installed Host there, or None), or over
    // the storage. Returns whether a command was sent or the chooser opened.
    bool DropOn(FName Component,LDHUD::EStorageColumn Column,FName Host);
    bool DropInStorage(FName Component);
    // The open chooser's option at Index (an element of B18 or a base of C12), which sends the command.
    bool ChooseOption(int32 Index);
    bool IsChoosing() const {return Chooser.bOpen;}
    // A component preview or storage hover marks this definition read for the current match.
    void MarkSeen(FName Component);
    void SetDetailsHeld(bool bHeld);
    // Probes: keeps the view open without a hover, for the open storage's screenshots and checks.
    void PinOpen(bool bPin){bPinned=bPin;}
    // The build workshop (APP_SPEC §7 构筑) lays the same columns and stored components out on its rule board and library:
    // each column in its board rectangle under the workshop's column head, the stored components grouped by kind in the
    // library rectangle. Absolute (desktop) units from the workshop's last layout. Without bActive the storage is the HUD
    // stack again.
    struct FWorkshopLayout
    {
        bool bActive=false;
        FSlateRect Columns[LDHUD::StorageColumnCount];
        // Each column's head height (the workshop draws the head); the installed components start below it.
        float HeadHeight=0.f;
        FSlateRect Library;
        // The installed components' icon scale (the bottom-of-screen workshop is smaller) and whether names show beside them.
        float TileScale=1.f;
        bool bNames=true;
        // The library's use-tag filter (ELDDemoComponentTag), or INDEX_NONE: non-matching components dim, nothing moves.
        int32 TagFilter=INDEX_NONE;
        // The library's icon size the workshop fits to its rectangle (68, 56, 46 or smaller), 0 for the count's tier.
        float LibraryIcon=0.f;
    };
    void SetWorkshop(const FWorkshopLayout& Layout);
    bool InWorkshop() const {return Workshop.bActive;}
    // What the workshop's dock and capacity bar follow: the component held (dragged or picked up), else the hovered one,
    // where it is, and the drop under the pointer while held.
    struct FFocus
    {
        FName Component;
        bool bInstalled=false;
        bool bHeld=false;
        bool bOverDrop=false;
        LDHUD::FComponentDrop Drop;
        // The places Component takes now (held or hovered in the library): legal columns light up, the rest dim.
        TArray<LDHUD::FComponentDrop> Drops;
    };
    const FFocus& Focus() const {return CurrentFocus;}
    const LDHUD::FStorageView& Model() const {return LatestModel;}
    const LDHUD::FBuildGateView& BuildGate() const {return Gate;}
    // Right click (D6): a stored component with exactly one place installs there; an installed one goes back to the storage.
    bool QuickInstall(FName Component);
    // A click without a drag (D6) picks the component up; the next click on a place drops it there (or anywhere else
    // drops it back).
    bool IsHolding() const {return Drag.bActive&&Drag.bPicked;}
    // The absolute rectangle of an installed component's face on the board or over the band (false when it is not shown).
    bool TileBox(FName Component,FSlateRect& Out) const;
    // The components a pending service confirm would take off or leave idle: their faces light orange before the hold.
    void SetWarnings(const TArray<FName>& Components){Warnings=Components;}
    // In an engagement window a newly received component only flies to the stack, without its name strip.
    void SetEngaged(bool bInEngaged){bEngaged=bInEngaged;}
    // The local text-size preference: rescales every text of the tree once per change; texts built later start scaled.
    void ApplyTextScale(float Scale);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
private:
    // How a face is drawn this frame (NativePaint): the kind's shape and rim, or a state colour that overrides it.
    enum class EFaceState : uint8 { Normal, Hovered, Held, Legal, Replace, Detached, Pick, Rejected, Dim };
    // One stored component's icon in the open grid.
    struct FCard
    {
        FName Component;
        UBorder* Face=nullptr;
        USizeBox* Size=nullptr;
        UWidget* FreshChip=nullptr;
        // Workshop: the library cell (icon and name) the filter dims.
        UWidget* Cell=nullptr;
        bool bFresh=false;
        uint8 Kind=0;
        bool bScarce=false;
        bool bPlaceless=false;
        EFaceState State=EFaceState::Normal;
        int32 AppliedLook=-1;
    };
    // One installed component in a column; nested ones are smaller and indented under their host.
    struct FTile
    {
        FName Component;
        FName Host;
        LDHUD::EStorageColumn Column=LDHUD::EStorageColumn::Body;
        UBorder* Face=nullptr;
        uint8 Kind=0;
        bool bScarce=false;
        EFaceState State=EFaceState::Normal;
        int32 AppliedLook=-1;
    };
    struct FColumn
    {
        USizeBox* Frame=nullptr;
        UBorder* Panel=nullptr;
        UVerticalBox* List=nullptr;
        UWidget* Placeholder=nullptr;
        USizeBox* PlaceholderBox=nullptr;
        UBorder* PlaceholderFace=nullptr;
        UTextBlock* PlaceholderText=nullptr;
        UTextBlock* Title=nullptr;
        // A held slot's active: its name is the place a notice names.
        FName Active;
        UCanvasPanelSlot* Slot=nullptr;
        int32 AppliedLook=-1;
    };
    enum class EFollow : uint8 { None, AwaitUninstall, AwaitView };
    struct FPress
    {
        FName Component;
        bool bFromColumn=false;
        FVector2D At=FVector2D::ZeroVector;
        bool bArmed=false;
    };
    struct FDrag
    {
        FName Component;
        bool bFromColumn=false;
        bool bActive=false;
        // Picked up by a click (D6) rather than held: it follows the pointer until the next click.
        bool bPicked=false;
        double Since=0;
        TArray<LDHUD::FComponentDrop> Drops;
    };
    struct FChooser
    {
        bool bOpen=false;
        FName Component;
        LDHUD::FComponentDrop Drop;
        FVector2D At=FVector2D::ZeroVector;
    };
    // A move's install half, sent once the uninstall is applied and the own view carries its build revision.
    struct FFollowUp
    {
        EFollow State=EFollow::None;
        FName Component;
        FLDBuildCommand Install;
        uint64 AfterRevision=0;
        double Since=0;
    };

    UPROPERTY() TObjectPtr<UCanvasPanel> Canvas;
    UPROPERTY() TObjectPtr<UWidget> StackFrame;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> StackSlot;
    UPROPERTY() TObjectPtr<UImage> StackPicture;
    UPROPERTY() TObjectPtr<UTextBlock> StackGlyph;
    UPROPERTY() TObjectPtr<UBorder> StackTop;
    UPROPERTY() TArray<TObjectPtr<UBorder>> StackBacks;
    UPROPERTY() TObjectPtr<UTextBlock> StackCount;
    // Under the caption: installed / workshop capacity.
    UPROPERTY() TObjectPtr<UTextBlock> StackCapacity;
    UPROPERTY() TObjectPtr<UWidget> StackFreshChip;
    UPROPERTY() TObjectPtr<UTextBlock> StackFresh;
    UPROPERTY() TObjectPtr<UWidget> ListFrame;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> ListSlot;
    UPROPERTY() TObjectPtr<UWrapBox> CardList;
    UPROPERTY() TObjectPtr<UWidget> PopupFrame;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> PopupSlot;
    UPROPERTY() TObjectPtr<UTextBlock> PopupName;
    UPROPERTY() TObjectPtr<UTextBlock> PopupWhere;
    UPROPERTY() TObjectPtr<URichTextBlock> PopupBody;
    UPROPERTY() TObjectPtr<UTextBlock> PopupHint;
    UPROPERTY() TObjectPtr<UWidget> ChooserFrame;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> ChooserSlot;
    UPROPERTY() TObjectPtr<UTextBlock> ChooserTitle;
    UPROPERTY() TObjectPtr<UHorizontalBox> ChooserLine;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> ChooserButtons;
    UPROPERTY() TObjectPtr<UWidget> GhostFrame;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> GhostSlot;
    UPROPERTY() TObjectPtr<UImage> GhostPicture;
    UPROPERTY() TObjectPtr<UTextBlock> GhostGlyph;
    // One line beside the ghost: what letting go here does (装到 Q / 替换 X · 连带拆下 Y / 不能装在这里).
    UPROPERTY() TObjectPtr<UWidget> GhostCaptionFrame;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> GhostCaptionSlot;
    UPROPERTY() TObjectPtr<UTextBlock> GhostCaption;
    // The workshop library: one group per kind (its title and count over a wrap of cells).
    UPROPERTY() TObjectPtr<UWidget> LibraryFrame;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> LibrarySlot;
    UPROPERTY() TObjectPtr<UVerticalBox> LibraryBox;
    UPROPERTY() TArray<TObjectPtr<UWidget>> LibraryGroups;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> LibraryCounts;
    UPROPERTY() TArray<TObjectPtr<UWrapBox>> LibraryWraps;
    // Every widget built for the current cards and tiles; replaced on each rebuild.
    UPROPERTY() TArray<TObjectPtr<UWidget>> Built;
    TArray<FCard> Cards;
    TArray<FTile> Tiles;
    TArray<FColumn> Columns;

    FLDProgressionView Latest;
    LDHUD::FStorageView LatestModel;
    FWorkshopLayout Workshop;
    FFocus CurrentFocus;
    // The hovered stored component's places, kept while the hover stays on it.
    FName HoverDropsOf;
    TArray<LDHUD::FComponentDrop> HoverDrops;
    // The request in flight: its component and where it was let go (the waiting ring), the last refusal (red frame and a
    // shake) and the last install (the landing flash). Local presentation time.
    FName PendingComponent;
    FVector2D PendingAt=FVector2D::ZeroVector;
    bool bPendingAt=false;
    FName RejectedComponent;
    double RejectedAt=-100.;
    FName LandedComponent;
    double LandedAt=-100.;
    bool bWasPressed=false;
    FName GhostShowing;
    TArray<FName> Warnings;
    bool bEngaged=false;
    // A component received since the last view flies from the screen centre into the stack, then its name stays above it.
    struct FFly
    {
        FName Component;
        double At=0.;
        bool bStrip=true;
    };
    TArray<FFly> Flies;
    TSet<FName> KnownOwned;
    float TextScale=1.f;
    uint32 KnownMatch=0;
    uint32 LatestMatch=0;
    TSet<FName> SeenComponents;
    LDHUD::FBuildGateView Gate;
    const FLDDemoContentDefinitions* LatestContent=nullptr;
    LDHUD::FCardStats LatestStats;
    FAnchors Anchors;
    FString BuiltKey;
    bool bShownNow=false;
    bool bExpanded=false;
    bool bPinned=false;
    bool bDetailsHeld=false;
    double LastInsideAt=0;
    FPress Press;
    FDrag Drag;
    FChooser Chooser;
    FFollowUp Follow;
    uint64 PendingRequest=0;
    uint32 Answers=0;

    UBorder* IconFace(float Size,UImage*& OutPicture,UTextBlock*& OutGlyph,UOverlay*& OutStack,USizeBox*& OutBox);
    void ShowIcon(UImage* Picture,UTextBlock* Glyph,FName Component,float Radius=6.f,bool bDim=false) const;
    void Rebuild(const LDHUD::FStorageView& Model);
    void BuildCard(FName Component,bool bFresh);
    void BuildTile(FColumn& Column,const LDHUD::FEquippedView& Entry,LDHUD::EStorageColumn ColumnId);
    void BuildWorkshopCard(FName Component,bool bFresh,float Size);
    void BuildWorkshopTile(FColumn& Column,const LDHUD::FEquippedView& Entry,LDHUD::EStorageColumn ColumnId,float Size);
    void LayoutWorkshop(const FGeometry& Geometry,FVector2D Pointer);
    // The face looks of this frame (both layouts): hover, held, legal and replace places, the refusal and the C12 pick.
    void UpdateStates(FVector2D Pointer);
    void UpdateFocus(FVector2D Pointer);
    bool PickUp(FName Component,bool bFromColumn);
    bool DropHeld(FVector2D At);
    double UiNow() const;
    float FaceRadius(uint8 Kind,float Size) const;
    // Whether Drops take something on Column (its own place only with bOwnOnly), replacements aside.
    static bool Drags(const TArray<LDHUD::FComponentDrop>& Drops,LDHUD::EStorageColumn Column,bool bOwnOnly);
    void StyleFace(UBorder* Face,FName Component,uint8 Kind,float Size,bool bDim) const;
    void ScaleBuiltText();
    void Layout(const FGeometry& Geometry,FVector2D Pointer);
    bool OverExpanded(FVector2D Absolute) const;
    bool BeginPress(FVector2D Absolute);
    void UpdateDrag(FVector2D Pointer,bool bHeld);
    void EndDrag();
    // What the pointer is over at the end of a drag.
    bool StorageAt(FVector2D Absolute) const;
    bool ColumnAt(FVector2D Absolute,LDHUD::EStorageColumn& OutColumn,FName& OutHost) const;
    FString PlaceName(LDHUD::EStorageColumn Column,FName Host) const;
    FString ComponentName(FName Component) const;
    void OpenChooser(FName Component,const LDHUD::FComponentDrop& Drop);
    void CloseChooser();
    bool Commit(FName Component,const LDHUD::FComponentDrop& Drop,ELDDemoElement Element,FName Base);
    bool Send(FLDBuildCommand Command);
    void ShowFinal(const FLDBuildReceipt& Receipt);
    void FinishRequest(const FString& Text,bool bRefused=false);
};
