#pragma once
#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDGrowthProtocol.h"
#include "UI/LDHUDModel.h"

struct FLDCombatAttributeRowView;

// The build ring's read model (构筑气场环, APP_SPEC §7): what each layer of the ring around the own character shows, assembled
// from the own progression view, the card storage view, the own attribute projection and the approved catalog. The ring
// geometry follows the approved mock (Artifacts/Audits/WorkshopRedesign/ring-aaa.src.html) in 1080p design pixels.
// Presentation only: legality, prices, capacity and every command stay with growth and the server; a link (联动) is only ever
// a vocabulary key two catalog rows share.
namespace LDBuildRing
{
    // The ring's own art (SourceData/UI, listed in ui_images.json with its provenance and imported by
    // Scripts\ImportDemoArt.ps1 -Mode ImportUI): the spray splat behind a host, the card frames' brushed metal, the medal
    // socket, the 新 sticker and the 本体 host icon. A missing texture falls back to the painted shape and is logged once.
    enum class EArt : uint8 { Splat, Metal, Socket, Fresh, Body, Count };
    struct FArt
    {
        const TCHAR* Id;
        const TCHAR* Package;
    };
    inline constexpr FArt Art[int32(EArt::Count)]={
        {TEXT("ring_splat"),TEXT("/Game/Lockdown/Art/UI/Ring/T_RingSplat")},
        {TEXT("ring_metal"),TEXT("/Game/Lockdown/Art/UI/Ring/T_RingMetal")},
        {TEXT("ring_socket"),TEXT("/Game/Lockdown/Art/UI/Ring/T_RingSocket")},
        {TEXT("ring_fresh"),TEXT("/Game/Lockdown/Art/UI/Ring/T_RingFresh")},
        {TEXT("ring_body"),TEXT("/Game/Lockdown/Art/UI/Ring/T_RingBody")}};
    // The package of a ring art id ("" when the id is not one of Art).
    LOCKDOWN_API FString ArtPackage(const FString& Id);
    // Radii and sizes at the 1080p reference (design px).
    inline constexpr float RadarRadius=92.f;
    inline constexpr float TrackRadius=143.f;
    inline constexpr float GemRadius=170.f;
    inline constexpr float GemSize=46.f;
    inline constexpr float SectorInner=196.f;
    inline constexpr float HostRadius=234.f;
    inline constexpr float HostSize=68.f;
    inline constexpr float NodeSize=54.f;
    inline constexpr float SectorHalf=20.5f;
    inline constexpr float WingRadius=600.f;
    inline constexpr int32 RowCount=4;
    inline constexpr float RowRadius[RowCount]={302.f,362.f,422.f,482.f};
    inline constexpr int32 RowSlots[RowCount]={3,4,5,5};
    inline constexpr float RowGap[RowCount]={6.f,6.f,4.f,4.f};
    // The five hosts in LDHUD::EStorageColumn order (本体, Q, W, E, 冲刺), degrees clockwise from straight up.
    inline constexpr int32 HostCount=5;
    inline constexpr float HostAngle[HostCount]={-88.f,-44.f,0.f,44.f,88.f};

    // The polar place (X = radius, Y = degrees) of slot Index of a sector at HostAngleDegrees: rows fill from the middle
    // outwards (3 + 4 + 5, then a fourth row), so an install or uninstall never moves another card.
    LOCKDOWN_API FVector2D SlotPolar(int32 Index,float HostAngleDegrees);
    // The rows Count slots take (at least one).
    LOCKDOWN_API int32 RowsFor(int32 Count);
    // Every scale of the ring for a viewport PixelHeight tall: layout (H / 1080), ring (never under .8) and text (never under
    // .875, times the player's text size). All three convert design px to pixels.
    struct FScale
    {
        float Layout=1.f;
        float Ring=1.f;
        float Text=1.f;
    };
    LOCKDOWN_API FScale ScaleFor(float PixelHeight,float TextScale);

    enum class ERarity : uint8 { Common, Rare, Legendary };
    // One face: an installed group of copies, a stored group of copies (the hand) or a medal.
    struct FCard
    {
        FName Key;
        TArray<FName> Keys;
        FName Definition;
        int32 Copies=1;
        ERarity Rarity=ERarity::Common;
        ELDCardForm Form=ELDCardForm::Body;
        FString Name;
        FString IconId;
        bool bFresh=false;
        // A medal's element and fusion tier.
        ELDDemoElement Element=ELDDemoElement::None;
        uint8 Tier=0;
    };
    // One skill host (a sector): 本体, the Q/W/E slot of that server slot, or 冲刺.
    struct FHost
    {
        LDHUD::EStorageColumn Column=LDHUD::EStorageColumn::Body;
        FName Active;
        ELDSkillBranch Branch=ELDSkillBranch::None;
        FString Name;
        FString IconId;
        // The key the player presses (the body has none).
        FString Key;
        bool bEmpty=false;
        // The element socket (a learned skill's or the body's) and the medal in it.
        bool bSocket=false;
        bool bHasMedal=false;
        FCard Medal;
        TArray<FCard> Cards;
        ELDCardForm Form=ELDCardForm::Body;
    };
    struct FStat
    {
        FName Id;
        FString Name;
        FString IconId;
        int32 Points=0;
        bool bKeystone=false;
    };
    // One derived attribute of the own projection, and the stat whose points raise it (INDEX_NONE: none).
    struct FDerived
    {
        uint8 Attribute=0;
        FString Name;
        // One character for the compact chip.
        FString Glyph;
        FString Value;
        int32 Source=INDEX_NONE;
    };
    enum class EBatch : uint8 { None, Final, Active, Card, Blueprint };
    struct FOption
    {
        FName Id;
        FString Name;
        FString IconId;
        FString Text;
        ERarity Rarity=ERarity::Common;
        ELDCardForm Form=ELDCardForm::Body;
    };
    // The decision on top of the queue (one batch), and how many more wait behind it.
    struct FBatch
    {
        EBatch Kind=EBatch::None;
        FString Title;
        TArray<FOption> Options;
        int32 Queued=0;
        int32 Rerolls=0;
        int64 SkipPoints=0;
    };
    struct FShopItem
    {
        // The shelf slot, or FLDBuildCommand::ShopSkillPointSlot for the skill point.
        int32 Slot=INDEX_NONE;
        ELDShopItemKind Kind=ELDShopItemKind::None;
        FName Id;
        FString Name;
        FString IconId;
        FString Note;
        int32 Price=0;
        bool bSold=false;
        ERarity Rarity=ERarity::Common;
    };
    // Two socketed hosts whose medals react; adjacent hosts meet on the element ring, others on the inner track.
    struct FReaction
    {
        int32 A=0;
        int32 B=0;
        ELDDemoElement First=ELDDemoElement::None;
        ELDDemoElement Second=ELDDemoElement::None;
        FString Name;
    };
    struct FView
    {
        bool bValid=false;
        TArray<FHost> Hosts;
        // The stored cards in their hand groups: skill, dash, body cards, then medals.
        TArray<FCard> Hand;
        TArray<FStat> Stats;
        TArray<FDerived> Derived;
        TArray<FReaction> Reactions;
        FBatch Batch;
        bool bShop=false;
        TArray<FShopItem> Shop;
        int32 RefreshPrice=0;
        int32 Level=0;
        int32 Installed=0;
        int32 Capacity=0;
        // The capacity at the top level (the closed state's tick ring).
        int32 MaxCapacity=0;
        int64 Unspent=0;
        int32 Keystone=0;
        int32 Fresh=0;
    };
    struct FInputs
    {
        const FLDProgressionView* Progression=nullptr;
        const LDHUD::FStorageView* Storage=nullptr;
        const FLDDemoContentDefinitions* Content=nullptr;
        const TArray<FLDCombatAttributeRowView>* Attributes=nullptr;
        LDHUD::FCardStats Stats;
        LDHUD::FSlotKeys Keys;
        FString DashKey;
        // The stat points shown (the view's plus the allocations on their way), in design order; empty: the view's.
        TArray<int32> Points;
        int64 Unspent=0;
        bool bRestShop=false;
    };
    LOCKDOWN_API FView Build(const FInputs& In);
    LOCKDOWN_API ERarity RarityOf(const FLDDemoContentDefinitions* Content,FName Definition);
    LOCKDOWN_API FString RarityName(ERarity Rarity);
    // The vocabulary keys of a card or an active (0 when the catalog has none).
    LOCKDOWN_API uint32 CardVocab(const FLDDemoContentDefinitions* Content,FName Definition);
    LOCKDOWN_API uint32 ActiveVocab(const FLDDemoContentDefinitions* Content,FName Active);
    // The stat (design order) whose per-point bonus raises Attribute (ELDProjectedAttribute), or INDEX_NONE.
    LOCKDOWN_API int32 StatSourceOf(const FLDDemoContentDefinitions* Content,uint8 Attribute);
    // The derived rows of the left wing, in the mock's order, from the own projection (rows it lacks are left out).
    LOCKDOWN_API TArray<uint8> WingAttributes();
    // The one-character form tag of a card (技, 冲, 体, 元) and the place it is installed on (装在本体上 …).
    LOCKDOWN_API FString FormTag(ELDCardForm Form);
    LOCKDOWN_API FString FormPlace(ELDCardForm Form);
    // The hand's left-to-right group of a form (skill, dash, body, medal).
    LOCKDOWN_API int32 HandGroup(ELDCardForm Form);
}
