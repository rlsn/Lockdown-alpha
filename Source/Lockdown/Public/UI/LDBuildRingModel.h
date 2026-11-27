#pragma once
#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDGrowthProtocol.h"
#include "UI/LDHUDModel.h"

struct FLDCombatAttributeRowView;

// The build ring's read model (构筑气场环, APP_SPEC §7): what each layer of the ring around the own character shows, assembled
// from the own progression view, the card storage view, the own attribute projection and the approved catalog. The ring
// geometry follows the approved radial-HUD comp (Artifacts/Audits/PresentationOverhaul/build-redesign/A) in 1080p design
// pixels. Presentation only: legality, prices, capacity and every command stay with growth and the server; a link (联动) is
// only ever a vocabulary key two catalog rows share.
namespace LDBuildRing
{
    // The ring's own art (SourceData/UI, listed in ui_images.json with its provenance and imported by
    // Scripts\ImportDemoArt.ps1 -Mode ImportUI): the 本体 host icon. Everything else is painted. A missing texture falls back
    // to the painted name and is logged once.
    enum class EArt : uint8 { Body, Count };
    struct FArt
    {
        const TCHAR* Id;
        const TCHAR* Package;
    };
    inline constexpr FArt Art[int32(EArt::Count)]={
        {TEXT("ring_body"),TEXT("/Game/Lockdown/Art/UI/Ring/T_RingBody")}};
    // The package of a ring art id ("" when the id is not one of Art).
    LOCKDOWN_API FString ArtPackage(const FString& Id);
    // Radii and sizes at the 1080p reference (design px).
    inline constexpr float RadarRadius=84.f;
    // The radar's first axis turns this far clockwise from the screen's up, so its top edge is flat and no corner (and no
    // stat chip) sits under the own character.
    inline constexpr float RadarTurn=36.f;
    inline constexpr float ChipRadius=112.f;
    inline constexpr float GemRadius=168.f;
    inline constexpr float GemSize=64.f;
    inline constexpr float SectorInner=210.f;
    inline constexpr float HostRadius=262.f;
    inline constexpr float HostSize=88.f;
    // A card tile (its icon is the tile less a 2 px rule on each side).
    inline constexpr float NodeSize=58.f;
    inline constexpr float SectorHalf=21.3f;
    // The flat band's outer rule while a sector needs two rows or fewer, and the dial ticks beyond it.
    inline constexpr float BandOuter=462.f;
    inline constexpr float TickLength=16.f;
    inline constexpr int32 RowCount=4;
    inline constexpr float RowRadius[RowCount]={352.f,422.f,492.f,562.f};
    inline constexpr int32 RowSlots[RowCount]={3,4,5,5};
    // Degrees between two neighbouring places of a row.
    inline constexpr float RowPitch[RowCount]={12.f,11.f,8.2f,7.5f};
    // The five hosts in LDHUD::EStorageColumn order (本体, Q, W, E, 冲刺), degrees clockwise from straight up.
    inline constexpr int32 HostCount=5;
    inline constexpr float HostAngle[HostCount]={-86.f,-43.f,0.f,43.f,86.f};

    // The polar place (X = radius, Y = degrees) of slot Index of a sector at HostAngleDegrees: rows fill from the middle
    // outwards (3 + 4 + 5, then a fourth row), so an install or uninstall never moves another card.
    LOCKDOWN_API FVector2D SlotPolar(int32 Index,float HostAngleDegrees);
    // The rows Count slots take (at least one).
    LOCKDOWN_API int32 RowsFor(int32 Count);
    // A sector's outer rule for Count slots: the band's own edge, or past the third and fourth rows when they are needed.
    LOCKDOWN_API float OuterFor(int32 Count);
    // Every scale of the ring for a viewport PixelHeight tall: layout (H / 1080), ring (never under 2/3, so a card icon keeps
    // 36 px) and text (never under 2/3, so 18 px text keeps 12 px, times the player's text size). All three convert design
    // px to pixels.
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
        // A skill host's action-bar position (0..2): its sector sits where that key's sector is, so a slot swap on the
        // action bar swaps the sectors too (INDEX_NONE: the body and the dash keep their own place).
        int32 Visual=INDEX_NONE;
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
    // One derived attribute of the own projection and the stat whose points raise it (the radar chip whose second line shows it).
    struct FDerived
    {
        uint8 Attribute=0;
        FString Name;
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
    // The five-stat table's mapping, from the catalog's per-point bonuses (the attribute projection's own inputs): every
    // (stat in design order, ELDProjectedAttribute) pair a point of that stat raises, stat by stat in per-point order, each
    // attribute once.
    LOCKDOWN_API TArray<FIntPoint> StatAttributes(const FLDDemoContentDefinitions* Content);
    // The one-character form tag of a card (技, 冲, 体, 元) and the place it is installed on (装在本体上 …).
    LOCKDOWN_API FString FormTag(ELDCardForm Form);
    LOCKDOWN_API FString FormPlace(ELDCardForm Form);
    // The hand's left-to-right group of a form (skill, dash, body, medal).
    LOCKDOWN_API int32 HandGroup(ELDCardForm Form);
}
