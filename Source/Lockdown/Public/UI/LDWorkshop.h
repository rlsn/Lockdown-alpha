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

// The build ring (构筑气场环, APP_SPEC §7 构筑; approved radial-HUD design Artifacts/Audits/PresentationOverhaul/build-redesign/A).
// Open (B), it grows around the own character without pausing or locking play, as one broadcast instrument: the five-stat
// radar lies on the ground under the character (ALDBuildRadarDecal) with the + chips at its corners, the element sockets and
// their reactions run on the inner track, the five skill hosts (本体, Q, W, E, 冲刺) carry their installed cards in fixed
// 3 + 4 + 5 places on a flat ink band with a white dial rule, the left table reads each stat's attributes, the right column is
// the rest shop, and the build bar docks above the HUD band with the offer on top (keys 1/2/3) or the library over it.
// Hovering anything shows its card above the ring and draws one white stroke through the cards and skills sharing a
// vocabulary key, in their order around the ring. Closed, the radar stays faint under the character with the capacity ticks,
// and the offer on top waits beside the HUD band over the build entry.
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
        // The HUD band's Q, W, E and D faces (absolute; empty when hidden): a linked skill's face takes the link's rule.
        TArray<FSlateRect> Skills;
        // The HUD's one-line toast (absolute; empty when hidden): the hover card keeps clear of it.
        FSlateRect Toast;
        // The rest actions' size (local units; zero when hidden): the ring keeps their corner clear.
        FVector2D Actions=FVector2D::ZeroVector;
    };
    void Present(const FInput& Input,const FGeometry* HudGeometry=nullptr);
    bool IsOpen() const {return Mode==EMode::Open;}
    // The ring is still growing in (the HUD keeps presenting every frame meanwhile).
    bool IsOpening() const;
    // Over a part of the ring that takes the pointer (world clicks elsewhere still move the character).
    bool IsHit(FVector2D Absolute) const;
    // Where the rest actions (重置训练, 准备完毕) stand while the ring is open: the bottom-right corner beside the HUD band,
    // or over the build bar's line when they would reach the band (local units, the actions' bottom-right corner).
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
    // Probes: the click a pointer press on that part makes (the same path as the mouse).
    void ProbeClick(EHit Kind,int32 Host,int32 Index);
    bool SelectHand(int32 Index);
    const LDBuildRing::FView& View() const {return Ring;}
    void ShowHand(bool bHand){bShowHand=bHand;}
    // Probes: hold the opening and the reveal at an age in seconds (negative releases them), and a part's link count.
    void FreezeMotion(float OpenAge,float RevealAge){FrozenOpen=OpenAge;FrozenReveal=RevealAge;}
    int32 LinkCount(EHit Kind,int32 Host,int32 Index) const {return LinkedTo(FHit{Kind,Host,Index}).Num();}
    // The current layout's faults, empty when every part is on screen, no side panel, bar or row reaches into the ring's
    // band, the table and shop rows stay apart, the offer or library row stands above the build bar and the bar above the
    // HUD band, and the hover card covers neither the hovered part nor the ring's cards (probes and tests).
    TArray<FString> LayoutProblems() const;
    // The number of unseen hand cards (the closed entry's 新 N).
    int32 FreshCount() const {return Ring.bValid?Ring.Fresh:0;}
    // The ground under the ring: a radial darkening centred on the character (no rectangle), with edge scrims, painted by
    // the HUD's ULDBuildRingGround layer under every HUD surface.
    int32 PaintGround(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer) const;
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
        // A convex outline, or an annular sector around Centre (radii X..Y, degrees Z..W).
        TArray<FVector2D> Shape;
        FVector2D Centre=FVector2D::ZeroVector;
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
        FVector2D Centre=FVector2D(960.,585.);
        // Each sector's outer rule (local units from the centre).
        float Outer[LDBuildRing::HostCount]={};
        // The build bar over the HUD band (its parts: points, capacity, library, commands), and the row above it (the offer
        // on top or the library).
        FBox2D Bar=FBox2D(ForceInit);
        TArray<FBox2D> Segments;
        float RowTop=0.f;
        // The left table and the shop: their top-left corner and their own scale (the text scale, fitted beside the ring).
        FVector2D Table=FVector2D::ZeroVector;
        float TableScale=1.f;
        float TableWidth=0.f;
        int32 TableColumns=2;
        // Where the attribute columns start and how wide each is (design px, times TableScale).
        float TableSubX=160.f;
        float TableSubW=84.f;
        FVector2D Shop=FVector2D::ZeroVector;
        float ShopScale=1.f;
        float ShopWidth=0.f;
        // The hover card above the ring's top (empty while nothing is hovered).
        FBox2D Card=FBox2D(ForceInit);
        TArray<FShape> Boxes;
        // Where each linkable thing sits (hosts, installed cards, hand cards, offers, shop items), for the link stroke.
        TMap<uint64,FVector2D> Anchors;
    };
    // A face in the hand or the offer row as laid out this frame.
    struct FFace
    {
        FHit Hit;
        FBox2D Box;
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
    FVector2D ActionsAt=FVector2D::ZeroVector;
    FVector2D SmoothedCentre=FVector2D::ZeroVector;
    bool bHasCentre=false;
    UPROPERTY() TObjectPtr<ALDBuildRadarDecal> Radar;

    double UiNow() const;
    float OpenAmount() const;
    void Rebuild();
    void ComputeLayout(const FGeometry& Geometry);
    void LayoutTable();
    void LayoutShop();
    void LayoutRow();
    void LayoutCard();
    void UpdateRadar();
    FHit HitAt(FVector2D Local) const;
    void Click(const FHit& Hit,bool bRight);
    void Tell(const FString& Text) const;
    // The hit currently drawn as hovered (the pin, else the pointer).
    FHit Focus() const {return Pinned.IsNone()?Hovered:Pinned;}
    // The vocabulary keys of a hovered thing, and the ring parts sharing one.
    uint32 VocabOf(const FHit& Hit) const;
    TArray<uint64> LinkedTo(const FHit& Hit) const;
    static uint64 AnchorKey(EHit Kind,int32 Host,int32 Index){return (uint64(uint8(Kind))<<48)|(uint64(uint16(Host+1))<<32)|uint64(uint32(Index+1));}
    const LDBuildRing::FCard* SelectedCard() const;
    // Whether Point lies on the ring's band (a sector between its inner rule and its ticks).
    bool OnBand(FVector2D Point,float Margin) const;
    // The text of the hover card for the focused part (empty name: none).
    struct FInfo
    {
        FString Name,Meta,Body,IconId;
        bool bBodyIcon=false;
        // Stat hover: the orange gain of one more point.
        FString Gain;
    };
    FInfo InfoOf(const FHit& Hit) const;
    // Painting, one layer of the instrument each.
    void PaintBand(LDRingPaint::FPainter& P) const;
    void PaintLinks(LDRingPaint::FPainter& P,const TArray<uint64>& Lit) const;
    void PaintSockets(LDRingPaint::FPainter& P) const;
    void PaintHosts(LDRingPaint::FPainter& P,const TArray<uint64>& Lit,bool bDim) const;
    void PaintRadarChips(LDRingPaint::FPainter& P) const;
    void PaintTable(LDRingPaint::FPainter& P) const;
    void PaintShop(LDRingPaint::FPainter& P,const TArray<uint64>& Lit) const;
    void PaintRow(LDRingPaint::FPainter& P,const TArray<uint64>& Lit) const;
    void PaintBar(LDRingPaint::FPainter& P) const;
    void PaintMatrix(LDRingPaint::FPainter& P) const;
    void PaintCard(LDRingPaint::FPainter& P,int32 LinkCount) const;
    void PaintSkillRules(LDRingPaint::FPainter& P,const TArray<uint64>& Lit) const;
    void PaintClosed(LDRingPaint::FPainter& P) const;
    // A square tile (the one container): its icon (or the name's first character), a 2 px rule, and the hover (4 px, 1.08),
    // lit (3 px) or dimmed state.
    enum class ETile : uint8 { Plain, Lit, Hot, Dim };
    void PaintTile(LDRingPaint::FPainter& P,const FBox2D& Box,const FString& IconId,const FString& Name,ETile State,
        LDBuildRing::ERarity Rarity=LDBuildRing::ERarity::Common,float RuleAlpha=.34f,UTexture2D* Override=nullptr) const;
};

// The ground layer of the open build ring: the HUD places it under its band, minimap and chrome, so the darkening never
// covers them; the ring itself paints above.
UCLASS()
class LOCKDOWN_API ULDBuildRingGround : public UUserWidget
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<ULDWorkshop> Ring;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
};
