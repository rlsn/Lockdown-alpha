#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchTimeline.h"
#include "UI/LDBuildRingModel.h"
#include "UI/LDBuildRingPaint.h"
#include "UI/LDHUDModel.h"
#include "LDWorkshop.generated.h"

class ALDBuildRadarDecal;
class ULDChoicesPanel;
class ULDComponentStorage;
class ULDStatBar;
class UTexture2D;
struct FLDCombatOwnerView;
struct FLDDemoContentDefinitions;
class FLDDemoContentSnapshot;

// The build ring (构筑气场环, APP_SPEC §7 构筑; approved design Artifacts/Audits/WorkshopRedesign). Open (B), it grows around
// the own character without pausing or locking play: the five-stat radar lies on the ground under the character
// (ALDBuildRadarDecal), the element sockets and their reactions sit on the inner ring, the five skill hosts (本体, Q, W, E,
// 冲刺) carry their installed cards in fixed 3 + 4 + 5 slots, the left wing lists the live derived attributes, the right
// wing is the rest shop, and the hand (or the offer on top of the queue, keys 1/2/3) lies above the HUD band. Hovering
// anything shows its card in the top-left corner and links it to the cards and skills sharing a vocabulary key. Closed, the
// radar stays faint under the character with the capacity ticks, and the offer on top waits beside the HUD band.
// Presentation only: a click is an intent sent through the existing command owners (the card storage for installs, the
// 构筑 tray for offers, the shop and rest services, the stat bar for points); the server decides and every refusal reads as
// one line.
UCLASS()
class LOCKDOWN_API ULDWorkshop : public UUserWidget
{
    GENERATED_BODY()
public:
    enum class EMode : uint8 { Closed, Open };
    struct FInput
    {
        EMode Mode=EMode::Closed;
        // The gameplay HUD shows for the own match (the closed state draws only then).
        bool bShown=false;
        const FLDProgressionView* Progression=nullptr;
        uint32 Match=0;
        const FLDDemoContentDefinitions* Content=nullptr;
        const FLDDemoContentSnapshot* ContentSnapshot=nullptr;
        const FLDCombatOwnerView* Combat=nullptr;
        LDProgression::EMatchStage Stage=LDProgression::EMatchStage::Closed;
        LDHUD::FCardStats Stats;
        LDHUD::FSlotKeys Keys;
        FString DashKey;
        FString OpenKey;
        TArray<FLDCombatAttributeRowView> Attributes;
        bool bRest=false;
        bool bDead=false;
        // HUD surfaces the ring keeps clear of (absolute; empty when hidden): the band, the minimap and the top banner.
        FSlateRect Band;
        FSlateRect Map;
        FSlateRect Banner;
    };
    void Present(const FInput& Input,const FGeometry* HudGeometry=nullptr);
    bool IsOpen() const {return Mode==EMode::Open;}
    // The ring is still growing in (the HUD keeps presenting every frame meanwhile).
    bool IsOpening() const;
    // Over a part of the ring that takes the pointer (world clicks elsewhere still move the character).
    bool IsHit(FVector2D Absolute) const;
    // Where the rest actions (重置训练, 准备完毕) stand while the ring is open: the top-right corner under the ring's own
    // buttons, clear of every arc (local units, the right edge of the actions).
    FVector2D ActionsAnchor() const {return ActionsAt;}
    // Keys 1/2/3 take an option of the offer on top, open or closed; true when the key was taken.
    bool HandleKey(const FKey& Key);
    TWeakObjectPtr<ULDComponentStorage> Storage;
    TWeakObjectPtr<ULDStatBar> StatBar;
    TWeakObjectPtr<ULDChoicesPanel> Choices;
    TFunction<void()> RequestClose;
    TFunction<void()> RequestOpen;
    // A local refusal line (the HUD's toast).
    TFunction<void(const FString&)> Notify;
    void ApplyTextScale(float Scale);
    void AllocationApplied(const FLDBuildReceipt& Receipt);
    void FinalModApplied(const FLDBuildReceipt& Receipt,const FLDFinalModSelection& Selection);
    void InterruptFinalFeedback();
    // Probes and tests: hover a part as if the pointer were over it (Kind INDEX_NONE releases it), select a hand card.
    enum class EHit : uint8 { None, Stat, Host, Gem, Installed, Drop, Hand, Offer, OfferPile, HandPile, Reroll, Skip, Shop, ShopRefresh,
        Close, Matrix, Respec, Element, ConfirmYes, ConfirmNo, Entry, Wing };
    void PinHover(EHit Kind,int32 Host,int32 Index);
    bool SelectHand(int32 Index);
    const LDBuildRing::FView& View() const {return Ring;}
    void ShowHand(bool bHand){bShowHand=bHand;}
    // Probes: hold the opening and the reveal at an age in seconds (negative releases them), and a part's link count.
    void FreezeMotion(float OpenAge,float RevealAge){FrozenOpen=OpenAge;FrozenReveal=RevealAge;}
    int32 LinkCount(EHit Kind,int32 Host,int32 Index) const {return LinkedTo(FHit{Kind,Host,Index}).Num();}
    // The current layout's faults, empty when every part is on screen, the wing rows stay apart below the inspector's corner
    // and above the hand, and the hand stands above the band line (probes and tests).
    TArray<FString> LayoutProblems() const;
    // The number of unseen hand cards (the closed entry's 新 N).
    int32 FreshCount() const {return Ring.bValid?Ring.Fresh:0;}
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
private:
    friend class FLDBuildRingLayoutTest;
    friend class FLDBuildRingIntentTest;
    friend class FLDHUDStatWheelTest;
    struct FHit
    {
        EHit Kind=EHit::None;
        int32 Host=INDEX_NONE;
        int32 Index=INDEX_NONE;
        bool operator==(const FHit& Other) const {return Kind==Other.Kind&&Host==Other.Host&&Index==Other.Index;}
        bool IsNone() const {return Kind==EHit::None;}
    };
    struct FShape
    {
        FHit Hit;
        // A convex outline, or a circle (Radius > 0) around Centre.
        TArray<FVector2D> Shape;
        FVector2D Centre=FVector2D::ZeroVector;
        float Radius=0.f;
        // An annular sector around Centre: radii X..Y, degrees Z..W.
        bool bWedge=false;
        FVector4f Wedge=FVector4f(0.f,0.f,0.f,0.f);
    };
    // One frame's layout in local units, shared by the paint pass and the hit test.
    struct FLayout
    {
        float U=1.f;      // design px -> local units (layout scale)
        float R=1.f;      // ring scale
        float F=1.f;      // text scale
        float K=1.f;      // local units -> pixels
        FVector2D Size=FVector2D(1920.,1080.);
        FVector2D Centre=FVector2D(960.,580.);
        float HandTop=0.f;
        float HandBottom=0.f;
        bool bWingCompact=false;
        float RingBottom=0.f;
        int32 OfferLines=3;
        float CardWidth=0.f;
        FVector2D ShopLabel=FVector2D::ZeroVector;
        float WingWidth=0.f;
        float InspectorBottom=0.f;
        TArray<FShape> Boxes;
        // Where each linkable thing sits (hosts, installed cards, hand cards, offers, shop items), for the link lines.
        TMap<uint64,FVector2D> Anchors;
    };
    // A face in the hand or the offer row as laid out this frame.
    struct FFace
    {
        FHit Hit;
        FBox2D Box;
        float Degrees=0.f;
    };
    EMode Mode=EMode::Closed;
    FInput Latest;
    FLDProgressionView View0;
    LDBuildRing::FView Ring;
    LDHUD::FStorageView StorageView;
    FLayout Layout;
    TArray<FFace> HandFaces;
    TArray<FFace> OfferFaces;
    FHit Hovered;
    FHit Pinned;
    double HoveredAt=-1.;
    // The selected hand card (its first copy's key), or a blueprint of the blueprint batch.
    FName Selected;
    int32 SelectedBlueprint=INDEX_NONE;
    bool bShowHand=false;
    float FrozenOpen=-1.f;
    float FrozenReveal=-1.f;
    bool bMatrix=false;
    float TextScale=1.f;
    double OpenedAt=-1.;
    double ClosedAt=-1.;
    // The batch on top as last shown (its kind and options), for the reveal.
    uint32 BatchKey=0;
    double BatchAt=-1.;
    // The last page turn between the hand and the offers.
    double PageAt=-1.;
    // Install feedback: the installed keys of the last view and when each new one landed.
    TSet<FName> KnownInstalled;
    TMap<FName,double> Landed;
    // Per-face hover and lift springs (0..1) by face key.
    TMap<uint64,float> Lift;
    double LastTick=-1.;
    FVector2D ActionsAt=FVector2D::ZeroVector;
    FVector2D SmoothedCentre=FVector2D::ZeroVector;
    bool bHasCentre=false;
    UPROPERTY() TObjectPtr<ALDBuildRadarDecal> Radar;

    double UiNow() const;
    float OpenAmount() const;
    void Rebuild();
    void ComputeLayout(const FGeometry& Geometry);
    void UpdateRadar();
    FHit HitAt(FVector2D Local) const;
    void Click(const FHit& Hit,bool bRight);
    void Tell(const FString& Text) const;
    // The level plate's width (LV, capacity pips, unspent points), its right edge at 800 design px.
    float LevelPlateWidth() const;
    // The hit currently drawn as hovered (the pin, else the pointer).
    FHit Focus() const {return Pinned.IsNone()?Hovered:Pinned;}
    // The vocabulary keys of a hovered thing, and whether a field thing shares one.
    uint32 VocabOf(const FHit& Hit) const;
    TArray<uint64> LinkedTo(const FHit& Hit) const;
    static uint64 AnchorKey(EHit Kind,int32 Host,int32 Index){return (uint64(uint8(Kind))<<48)|(uint64(uint16(Host+1))<<32)|uint64(uint32(Index+1));}
    const LDBuildRing::FCard* SelectedCard() const;
    // Painting, one layer of the ring each.
    void PaintDim(LDRingPaint::FPainter& P) const;
    void PaintSectors(LDRingPaint::FPainter& P,const TArray<uint64>& Lit,bool bDim) const;
    void PaintGems(LDRingPaint::FPainter& P) const;
    void PaintRadarLabels(LDRingPaint::FPainter& P) const;
    void PaintWing(LDRingPaint::FPainter& P) const;
    void PaintShop(LDRingPaint::FPainter& P,const TArray<uint64>& Lit) const;
    void PaintBottom(LDRingPaint::FPainter& P,const TArray<uint64>& Lit) const;
    void PaintTop(LDRingPaint::FPainter& P) const;
    void PaintLinks(LDRingPaint::FPainter& P,const TArray<uint64>& Lit) const;
    void PaintInspector(LDRingPaint::FPainter& P,int32 LinkCount) const;
    void PaintClosed(LDRingPaint::FPainter& P) const;
    void PaintNode(LDRingPaint::FPainter& P,const FBox2D& Box,const LDBuildRing::FCard& Card,float Dim,bool bHot,bool bLit,float Flash) const;
};
