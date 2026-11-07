#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDCaptureTypes.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"

// Local presentation only. Other-subject formatting accepts an authorized observed unit plus public
// roster/content labels, never a canonical actor or another player's owner state.
// Player-visible Chinese follows APP_SPEC 1.2; identifiers, enums and content keys keep their technical names.
struct FLDDemoContentDefinitions;
struct FLDDemoStatBonus;
struct FLDDemoEffectDefinition;
struct FLDDemoTargetDefinition;
struct FLDDemoRankDefinition;
struct FLDDemoAbilityDefinition;
struct FLDLobbyView;
namespace LDHUD
{
    // #147 card text: the owner's level grows a skill's fixed amounts (G(L)); its stats fill the coefficients.
    struct FCardStats
    {
        uint8 Level=1;
        int64 ADP2=0;
        int64 APP2=0;
        int64 MaxHPP2=0;
        int64 ArmorP2=0;
    };
    // Level from the progression, stats from the owner's projected attributes; zero stats without a projection.
    LOCKDOWN_API FCardStats CardStatsOf(uint8 Level,const TArray<FLDCombatAttributeRowView>& Attributes);
    struct FCardText
    {
        FString Rich;
        FString Plain;
        // Keywords the text uses, in glossary order.
        TArray<FString> Keywords;
        // False when a placeholder reads nothing (never shown as a number).
        bool bComplete=true;
        // Each placeholder's value at Rank, in template order ("?" when it reads nothing).
        TArray<FString> Values;
    };
    // Renders a card template (LDDemoCardText) against Rank grown for Stats.Level; with From, every value that changes reads
    // "A → B". The 13 glossary keywords (LDTerms::CardKeywords) are wrapped in <b>…</> in Rich.
    LOCKDOWN_API FCardText CardText(const FString& Template,const FLDDemoRankDefinition& Rank,const FLDDemoRankDefinition* From,
        const FCardStats& Stats,int64 GrowthPerLevelBp);
    // Characters a player reads: everything but spaces (v7 R1/R2: a sentence at most 26, an advanced line at most 18).
    LOCKDOWN_API int32 CardLength(const FString& Plain);
    // A learned skill's effect lines: its card sentence at Rank for Stats, then, at its top (advanced) rank, the "高级：" line
    // with the change that rank adds. Empty when Ability has no such rank.
    LOCKDOWN_API TArray<FString> LearnedCardLines(const FString& Sentence,const FString& Advanced,const FLDDemoAbilityDefinition& Ability,
        uint8 Rank,const FCardStats& Stats,int64 GrowthPerLevelBp);
    // "<b>减速</>：…" for each keyword.
    LOCKDOWN_API TArray<FString> KeywordLines(const TArray<FString>& Keywords);
    // Real local frame intervals over a window of at least WindowSeconds (a long frame is never cut; it closes its window
    // late). AddFrame is true when a window just closed; FPS, FrameMs and PeakMs then describe it, Frames counts its frames
    // and Hitches the frames of at least HitchSeconds (0 counts none). The HUD uses the half-second default; the playtest
    // log's ten-second summary and its hitch lines use the same calculation.
    struct LOCKDOWN_API FFrameDiagnostics
    {
        FFrameDiagnostics()=default;
        FFrameDiagnostics(double InWindowSeconds,double InHitchSeconds):WindowSeconds(InWindowSeconds),HitchSeconds(InHitchSeconds){}
        double WindowSeconds=.5, HitchSeconds=0;
        double FPS=0, FrameMs=0, PeakMs=0;
        uint32 Frames=0, Hitches=0;
        bool AddFrame(double Seconds);
    private:
        double Elapsed=0, Peak=0;
        uint32 Count=0, HitchCount=0;
    };
    // Display text of an approved content definition from the DemoContent string table. False when the
    // definition, its display entry, the table or the key is missing: Out then holds the stable content id,
    // so the content error stays visible instead of a guessed name.
    LOCKDOWN_API bool ContentText(const FLDDemoContentDefinitions* Content,const FString& ContentId,bool bDescription,FString& Out);
    LOCKDOWN_API FString ContentName(const FLDDemoContentDefinitions* Content,const FString& ContentId);
    // #153: an offer of opening direction cards (opportunity 1); it is never rerolled.
    LOCKDOWN_API bool IsOpeningOffer(const FLDChoiceOfferView& Offer);
    // #147 two skill ranks: 初级 (1) and 高级 (2); anything else is named by its number.
    LOCKDOWN_API FString SkillRankName(int32 Rank);
    LOCKDOWN_API FString StatusName(const FLDDemoContentDefinitions* Content,uint8 StatusId);
    LOCKDOWN_API FString CreatureName(const FLDDemoContentDefinitions* Content,uint8 CreatureId);
    LOCKDOWN_API FString EquipmentName(const FLDDemoContentDefinitions* Content,uint8 EquipmentId);
    LOCKDOWN_API FString AbilityName(const FLDDemoContentDefinitions* Content,uint8 AbilityId);
    // Public roster name of a contender; empty when the roster does not list it (never invented).
    LOCKDOWN_API FString RosterName(const FLDLobbyView& Lobby,const FGuid& ContenderId);
    // The six roster-slot identity tints shared with the bodies (ULDHumanoidVisualComponent::ContenderTint), so
    // portraits match them.
    LOCKDOWN_API FLinearColor PaletteColor(uint8 Presentation);

    struct FResourceView
    {
        bool bKnown=false;
        FString Text;
        float Fraction=0.f;
    };
    // Points from the owner projection. Max<=0 means not projected yet: shown as syncing, never as 0/0.
    // Health rounds up so a living body never reads 0; mana rounds down so a cost never looks affordable early.
    LOCKDOWN_API FResourceView Resource(const FString& Label,double Current,double Max,bool bRoundUp);
    LOCKDOWN_API FResourceView ResourceP2(const FString& Label,int64 CurrentP2,int64 MaxP2,bool bRoundUp);

    // Empty when the matching living owner projection is unavailable; never estimate from HP/MP deltas.
    LOCKDOWN_API FString RegenText(const FLDOwnerState& Own,const FLDCombatOwnerView* Combat,bool bHealth);

    struct FLevelView
    {
        FString Level;
        FString Experience;
        float Fraction=0.f;
        bool bMax=false;
    };
    // Thresholds are cumulative XP from the approved progression; the cap shows MAX instead of an eleventh level.
    LOCKDOWN_API FLevelView Level(int32 Level,int32 MaxLevel,int64 CumulativeXp,int64 LevelThresholdXp,bool bHasNext,int64 NextThresholdXp);
    LOCKDOWN_API FString Gold(int64 Gold);

    // Rounds a live remainder up to the next tenth, so it cannot read 0.0s while the deadline is still ahead.
    LOCKDOWN_API FString TenthsUp(int64 RemainingMs);
    // Remainder drawn over a cooling action slot: whole seconds rounded up from ten seconds, tenths rounded up below, so it
    // never reads 0.0 while time remains; empty once the track is ready.
    LOCKDOWN_API FString CooldownText(int64 RemainingMs);
    // Owner life line from the Lives projection. "Now" is only the matching authority clock sample: without one the
    // remaining time is unavailable, and a live countdown never reads zero before that clock reaches the deadline.
    LOCKDOWN_API FString LifeStatus(const FLDOwnerState& Own,bool bClock,int64 NowMs);
    struct FDeathView
    {
        bool bVisible=false;
        FString Title;
        FString Countdown;
        FString Note;
    };
    // Death banner (APP_SPEC 7.1): waiting time only; build and property are kept, and the display never revives.
    LOCKDOWN_API FDeathView Death(const FLDOwnerState& Own,bool bClock,int64 NowMs);
    LOCKDOWN_API FString PhaseLine(ELDMatchPhase Phase);
    // #153: the centre-screen opening countdown of the public match: whole seconds rounded up ("5" .. "1") while the
    // authority time is before T=0 (StartedAtMs), then "开淫！" for GoShowMs from T=0. Empty otherwise or without a clock;
    // reaching zero only waits for the authority's development phase.
    inline constexpr int64 CountdownGoShowMs=1000;
    LOCKDOWN_API FString OpeningCountdown(const FLDPublicMatchView& Public,bool bClock,int64 NowMs);
    // Whole seconds rounded up as m:ss, so a running countdown never shows 0:00 early.
    LOCKDOWN_API FString Clock(int64 RemainingMs);
    // The match timer over the minimap: whole seconds elapsed since T=0 (StartedAtMs), rounded down, as m:ss with no
    // leading zero on the minutes ("0:12", "12:05"). "0:00" through the opening countdown; empty without a clock or T=0.
    LOCKDOWN_API FString MatchTimer(const FLDPublicMatchView& Public,bool bClock,int64 NowMs);
    struct FCircleView
    {
        FString Line;
        bool bProgress=false;
        float Progress=0.f;
    };
    // Public spray and guard state (APP_SPEC 2.2). Reaching zero only waits for the authority; it never announces a
    // spray or a winner. The progress bar shows the sole sprayer's progress (Solo), a paused holder's kept progress
    // against SprayMs (Contested), otherwise the graffiti owner's guard against GuardMs (CAPTURE.SpraySec/GuardSec).
    // The graffiti owner and the guard are named in every open state; a contest without a holder names no sprayer.
    LOCKDOWN_API FCircleView Circle(const FLDPublicMatchView& Public,const FString& SprayerName,const FString& OwnerName,bool bClock,int64 NowMs,
        int64 SprayMs=0,int64 GuardMs=0);
    // The one most useful next step for the own player, shown under the public spray line: a pending growth choice,
    // then the open flagpole (spray, guard your own graffiti, or besiege its owner), then the dearest approved item the
    // own gold buys while a slot is empty, then early farming. Own projections, the public match and approved prices
    // only; never a hidden unit or another wallet. Empty when nothing applies or the match is not running.
    LOCKDOWN_API FString NextStep(const FLDPublicMatchView& Public,const FGuid& Self,const FLDProgressionView* Progression,const FLDEquipmentView* Equipment,const FLDDemoContentDefinitions* Content,
        const FString& OwnerName=FString());
    // D26 (#79): the public airdrop line under the capture state: the landing countdown and the landed drop. Empty when
    // there is no drop to show; the claim is an event of the event feed (FEventFeed), not a standing line.
    LOCKDOWN_API FString AirdropLine(const FLDAirdropView& Drop,bool bClock,int64 NowMs);
    // D26 (#79): the public Boss line: a countdown shortly before it appears and a reminder while it patrols. Empty
    // otherwise; who landed the last hit is an event of the event feed (FEventFeed).
    LOCKDOWN_API FString BossLine(const FLDBossView& Boss,const FString& BossName,bool bClock,int64 NowMs);
    // The event feed's line for a downed Boss: the own last hit is told it ate the pie, others see the public roster name.
    LOCKDOWN_API FString BossDownLine(const FLDBossView& Boss,const FGuid& Self,const FString& KillerName,const FString& BossName);
    // D27 (#79): the public 上热搜 line: who is named and the bounty on them; the own player is told directly.
    // Empty when nobody is named.
    LOCKDOWN_API FString LeaderLine(const FLDLeaderView& Notice,const FGuid& Self,const FString& LeaderName);
    // Public contender death notice; names come from the public roster and camp tiers from approved display text.
    // A contender kill lists its assisters after the line (AssisterNames in the notice's order).
    LOCKDOWN_API FString KillLine(const FLDKillNotice& Notice,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName,const FString& BossName,const TArray<FString>& AssisterNames=TArray<FString>());
    struct FKillFeedLineView
    {
        FString Text;
        bool bSelf=false;
    };
    // A notice naming the player as victim, killer or assister says 你 there and is marked so the feed can highlight it.
    LOCKDOWN_API FKillFeedLineView KillFeedLine(const FLDKillNotice& Notice,const FGuid& Self,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName,const FString& BossName,const TArray<FString>& AssisterNames=TArray<FString>());
    // Cause line of the own death banner: the newest public notice of this generation naming the player as the victim at or
    // after NotBeforeMs (when the life that died was first seen alive, so an older death is never shown). Killers come from
    // the public roster and camp tiers from approved display text; empty while no such notice is known.
    LOCKDOWN_API FString DeathCause(const FLDKillFeedView& Feed,uint32 Generation,const FGuid& Self,int64 NotBeforeMs,const FLDLobbyView& Lobby,const FString& LowCampName,const FString& HighCampName,const FString& BossName);
    LOCKDOWN_API FString ResultHeadline(const FLDResultView& Result,const FString& WinnerName);
    struct FResultRowView
    {
        FString Name;
        FString Kills;
        FString Deaths;
        FString Assists;
        FString Level;
        bool bWinner=false;
    };
    // One frozen row in locked roster order; statistics are display only and never decide the winner.
    LOCKDOWN_API FResultRowView ResultRow(const FLDResultRow& Row,const FGuid& Winner,const FGuid& Self);
    struct FScoreLine
    {
        FString Name;
        uint64 Kills=0;
        uint64 Deaths=0;
        uint64 Assists=0;
        bool bSelf=false;
    };
    // The hold-Tab scoreboard of that match: most kills first, then fewest deaths, then roster order. Names come from the
    // room roster; a contender no longer in it is shown as having left. Empty for another match's feed.
    LOCKDOWN_API TArray<FScoreLine> Scoreboard(const FLDKillFeedView& Feed,uint32 Generation,const FLDLobbyView& Lobby,const FGuid& Self);

    // The band's five slots: the Q/W actives, the two equipment slots (shown, never activated) and the D dash.
    enum class EActionSlot : uint8 { Q, W, Equipment1, Equipment2, Dash };
    // ActionPassive: an occupied equipment slot, whose stats and passive apply without any activation.
    enum EActionBlock : uint32
    {
        ActionUnsynced=1<<0, ActionEmpty=1<<1, ActionPassive=1<<2, ActionCooldown=1<<3, ActionMana=1<<4,
        ActionControlled=1<<5, ActionDead=1<<6, ActionNotPlaying=1<<7, ActionServer=1<<8
    };
    LOCKDOWN_API bool IsEquipmentSlot(EActionSlot Slot);
    struct FActionInput
    {
        EActionSlot Slot=EActionSlot::Q;
        bool bProjected=false;    // a current owner combat projection exists for this match and life
        bool bHasSource=false;    // an active is bound, or the equipment slot holds an item
        FString Name;             // display name of the bound definition
        FName DefinitionId;       // the bound definition, for the hover description
        uint8 Rank=0;             // learned rank; equipment has none
        bool bUpgraded=false;     // the held equipment instance is upgraded (112 §7)
        int64 ManaCostP2=0;
        int64 ReadyAtMs=0;        // this source's own track end on the authority clock
        int64 CooldownMs=0;       // duration last issued on this track, for the sweep only
        int32 RangeCm=0;          // the active's current reach (the dash distance for D)
        bool bControlled=false;   // an own control status forbids this entry kind
        FString ServerBlock;      // display text of the server's current reason; empty when none
    };
    struct FActionContext
    {
        FString KeyLabel;
        bool bOwnContext=false;   // the local owner projection belongs to the current match and life
        bool bPlaying=false;
        bool bAlive=false;
        bool bInputCaptured=false;  // Combat's input half: menu/Settings, child confirmation or text edit, keys still held
        bool bClock=false;
        int64 NowMs=0;
        bool bManaKnown=false;
        int64 ManaP2=0;
        // #147: the owner's level and stats, so a skill's card text shows the numbers it has now.
        FCardStats Card;
    };
    struct FActionView
    {
        EActionSlot Slot=EActionSlot::Q;
        FString Key;
        FString Kind;
        FString Name;
        FString State;
        // Intent submission and execution limits are separate (UI 4): blocks grey the slot and explain why, but an
        // explicit activation is still sent so the server decides, including protection removal and EmptySource.
        bool bCanSubmitIntent=false;
        uint32 ExecutionBlocks=0;
        int64 CooldownRemainingMs=0;
        float CooldownFraction=0.f;
        FString Countdown;        // CooldownText of the remainder; empty when not cooling
        FString Cost;             // the active's mana cost rounded like the shown mana, number only, e.g. "30"; empty without one
        FString Glyph;            // the placeholder icon's character (the name's first); empty without a source
        FString Rank;             // the learned rank of Q/W, number only; empty for equipment, the dash and empty slots
    };
    LOCKDOWN_API FActionView Action(const FActionInput& Input,const FActionContext& Context);
    // Slot look: the slot face says its state by colour alone (the tooltip keeps the words). A dead, unsynced or empty
    // slot outranks arming; an armed entry outranks its remaining limits, then control, cooldown, mana and server reasons.
    enum class EActionLook : uint8 { Ready, Armed, Cooling, NoMana, Controlled, Empty, Blocked };
    LOCKDOWN_API EActionLook ActionLook(const FActionView& View,bool bArmed);
    // The hover description of a slot, laid out like the shop and blind-box popups: the name, "<kind> [<key>] · <state>",
    // then the description, the targeting and the current rank's (or the equipment's, upgraded when it is) effect lines.
    struct FActionDetailView
    {
        FString Name;
        FString Summary;
        FString Body;
    };
    LOCKDOWN_API FActionDetailView ActionDetail(const FActionView& View,const FActionInput& Input,const FActionContext& Context,const FLDDemoContentDefinitions* Content);

    struct FObservedCard
    {
        FString Title;
        FResourceView Health;
        bool bMana=false;
        FResourceView Mana;
        FString Statuses;
        bool bEquipment=false;
        FString Equipment;
        // A visible contender's Q/W actives (#112 §10): key, display name, rank and the Lv3 mark; never cooldowns.
        struct FActiveChip
        {
            FString Key;
            FString Name;
            FString Rank;
            bool bEmpty=true;
            bool bMaxRank=false;
        };
        bool bActives=false;
        FActiveChip Actives[2];
    };
    // SC-07 whitelist only: name, level, HP, MP, public statuses, two equipment definitions and the two Q/W actives with ranks.
    LOCKDOWN_API FObservedCard ObservedCard(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec);
    LOCKDOWN_API FString ObservedUnit(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec);

    // Typed parameters of approved content (CONTENT 12.5): prose comes from the display table, numbers only from the
    // immutable snapshot fields, formatted here without recomputing any battle formula.
    LOCKDOWN_API FString StatLine(const FLDDemoStatBonus& Stat);
    // A wild card's explicit deduction, e.g. "移速 -4%".
    LOCKDOWN_API FString PenaltyLine(const FLDDemoStatBonus& Stat);
    LOCKDOWN_API FString EffectLine(const FLDDemoEffectDefinition& Effect);
    LOCKDOWN_API FString TargetLine(const FLDDemoTargetDefinition& Target);
    // The mana cost and cooldown line of an active rank.
    LOCKDOWN_API FString CostLine(const FLDDemoRankDefinition& Rank);
    // Placeholder icon family of a card until real art lands (#93): each family has its own shape and colour, and the card's
    // kind line names it, so no card relies on colour alone.
    enum class EChoiceIcon : uint8 {Unknown,Active,Passive,Attribute};
    struct FChoiceCardView
    {
        FString Kind;
        FString Name;
        FString RankText;
        // The compact card's short second line (#97): "新" for a new skill, "初→高" for an upgrade, the pick count for an
        // attribute; the icon shape names the kind and the HUD shows a needed replacement. Detail is "<kind> · <rank>".
        FString Summary;
        FString Detail;
        // #147 card face: the one-sentence rule with this level's numbers ("A → B" on an upgrade) and the "高级：" line, as
        // rich text with the keywords in <b>…</>; an attribute card's sentence is its stat line. Plain twins drop the markup.
        FString Sentence;
        FString SentencePlain;
        FString Advanced;
        FString AdvancedPlain;
        // "<b>关键词</>：解释" for every keyword the face uses, in glossary order.
        TArray<FString> Keywords;
        // Details below the face: targeting and the active's mana and cooldown.
        TArray<FString> Lines;
        // Display-table icon reference (empty when the definition has no display row); the placeholder draws Icon + Glyph.
        FString IconId;
        EChoiceIcon Icon=EChoiceIcon::Unknown;
        FString Glyph;
        bool bNeedsReplacement=false;
    };
    // One of the three fixed options of the current offer; a learn-active card with both Q/W filled needs an explicit replacement.
    // Numbers are those of the owner's level and stats in Stats.
    LOCKDOWN_API FChoiceCardView ChoiceCard(const FLDChoiceOption& Option,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content,
        const FCardStats& Stats);
    struct FShopItemView
    {
        FString Name;
        FString Price;
        TArray<FString> Lines;
        bool bAffordable=false;
    };
    LOCKDOWN_API FShopItemView ShopItem(const FLDShopItemView& Item,int64 Gold,const FLDDemoContentDefinitions* Content);
    // Shop pre-orders (like a quick-buy list): at most this many queued items, kept locally for one match.
    constexpr int32 MaxShopPreorders=2;
    // The approved catalog as shop rows (definition, buy and sell price), for browsing and pre-ordering away from
    // the shop; a session's quote replaces it whenever one is shown. Empty without content.
    LOCKDOWN_API TArray<FLDShopItemView> CatalogItems(const FLDDemoContentDefinitions* Content);
    // Index into Queue of the pre-order to buy next: queue order, the first one the listing prices, the gold covers and an
    // empty equipment slot can hold. A definition in Upgradable (a held copy whose upgrade is offered) is bought as that
    // upgrade instead: at UpgradePrice and without a free slot, never as a second copy. INDEX_NONE when none qualifies
    // (no free slot, too little gold, or not listed).
    LOCKDOWN_API int32 NextPreorder(const TArray<FName>& Queue,const TArray<FLDShopItemView>& Listing,int64 Gold,int32 FreeSlots,
        const TArray<FName>& Upgradable=TArray<FName>(),int64 UpgradePrice=0);
    // 112 §7: what an upgraded copy holds (stats +35% floored, the upgraded passive terms); empty parts are omitted.
    LOCKDOWN_API TArray<FString> UpgradeLines(const FLDDemoEquipmentDefinition& Item);
    struct FAttributeRowView
    {
        FString Name;
        FString Final;
        FString Breakdown;
        bool bClamped=false;
    };
    // One combat-projected attribute with its sources; the HUD never recomputes the projection.
    LOCKDOWN_API FAttributeRowView AttributeRow(const FLDCombatAttributeRowView& Row);
    LOCKDOWN_API FString GrowthReasonText(ELDGrowthReason Reason);
    LOCKDOWN_API FString CombatRejectText(ELDCombatReject Reject);
    // Owner combat entry -> action input; Unavailable stays a display-only server reason.
    LOCKDOWN_API FActionInput ActionInput(const FLDCombatEntryView& Entry,EActionSlot Slot,const FLDDemoContentDefinitions* Content);
    // Own equipment slot -> equipment slot input (projected once the own equipment view is known).
    LOCKDOWN_API FActionInput EquipmentInput(const FLDEquipmentSlotView& Held,EActionSlot Slot,const FLDDemoContentDefinitions* Content);
    // One square of the own status grid under the 属性 button: a temporary status, a learned passive or an equipment
    // passive. Glyph is the placeholder icon (the first character of the name); Tip is the name with its stacks, shield
    // and remainder (statuses) or rank name (learned passives). The hover popup is built from it by SelfIconDetail.
    enum class ESelfIconKind : uint8 { Control, Temporary, Passive };
    enum class ESelfIconSource : uint8 { Status, Learned, Equipment };
    struct FSelfIconView
    {
        ESelfIconKind Kind=ESelfIconKind::Passive;
        ESelfIconSource Source=ESelfIconSource::Status;
        FString Glyph;
        FString Tip;
        // The content entry the popup describes: the status, the learned passive or the equipment granting the passive.
        FString ContentId;
        uint8 Rank=0;
        bool bUpgraded=false;
        // Stable within one status or passive, so the HUD keeps its remaining-time ring across samples.
        FName Key;
        // Timed temporary statuses only: the authority instant it ends; 0 for conditional states and passives.
        int64 ExpiresAtMs=0;
        uint8 Stacks=1;
    };
    inline constexpr int32 SelfIconColumns=6;
    inline constexpr int32 SelfIconRows=2;
    inline constexpr int32 SelfIconMax=SelfIconColumns*SelfIconRows;
    struct FSelfIconsView
    {
        // At most SelfIconMax: control statuses, other temporary statuses, then learned and equipment passives.
        TArray<FSelfIconView> Items;
        // Items beyond SelfIconMax; the grid shows "…" and the attribute panel lists them all.
        int32 Overflow=0;
        // A missing own projection: the grid says syncing instead of showing none.
        bool bSyncing=false;
    };
    // Own temporary statuses (passive auras and respawn protection, which the life line shows, are left out; shield
    // points round up and a remainder needs the matching authority clock) and own passives (learned passives with their
    // rank, then each equipment passive once, named after the equipment granting it).
    LOCKDOWN_API FSelfIconsView SelfIcons(const FLDProgressionView* Progression,const FLDCombatOwnerView* Combat,bool bClock,int64 NowMs,
        const FLDDemoContentDefinitions* Content);
    // The hover popup of one status square, laid out like the slot popup: the Tip line, the kind ("控制状态", "临时状态",
    // "角色被动", "装备被动", upgraded when it is), then the full content description: a status's or equipment's text
    // (with the equipment's passive effect lines), or a learned passive's card lines rendered for Card (LearnedCardLines).
    // Built only for the hovered square.
    LOCKDOWN_API FActionDetailView SelfIconDetail(const FSelfIconView& Icon,const FCardStats& Card,const FLDDemoContentDefinitions* Content);
    // The placeholder icon of a name: its first character, Latin letters upper-cased; "?" for an empty name.
    LOCKDOWN_API FString IconGlyph(const FString& Name);
    // Remaining-time rings of the timed own statuses. The view carries only the end instant, so the remaining time when
    // an end instant is first seen is the full ring, and a later end (a refresh) restarts it. Presentation only: a
    // status first seen part-way (a reconnect) starts full.
    class LOCKDOWN_API FStatusRings
    {
    public:
        // Remaining fraction in [0,1] of Key's current span.
        float Fraction(FName Key,int64 ExpiresAtMs,int64 NowMs);
        // Forgets every span whose key is not live, so an expired and re-applied status starts a fresh ring.
        void Retain(const TArray<FName>& Live);
    private:
        struct FSpan{int64 ExpiresAtMs=0;int64 SpanMs=0;};
        TMap<FName,FSpan> Spans;
    };
    LOCKDOWN_API FString EntryLabel(ELDCombatEntry Entry);
    // Combat receipt notice: an execution failure of the player's own request and the loss of respawn protection are
    // separate facts (UI 6.2); a cancel or a non-final phase says nothing. Empty when there is nothing to tell.
    LOCKDOWN_API FString CombatReceiptNotice(const FLDCombatActionReceipt& Receipt);
    // The same execution failure for the pressed Q/W/D slot, which already names its entry: the reason only. Empty for
    // other entries and for anything CombatReceiptNotice would not report as a failure.
    LOCKDOWN_API FString CombatReceiptSlotNotice(const FLDCombatActionReceipt& Receipt);
    // The toast line for every own receipt that arrived since the last frame, oldest first: each distinct notice once, in
    // arrival order. Receipts of another match or life tell nothing; the lost protection is told once per life revision.
    LOCKDOWN_API FString CombatReceiptsNotice(const TArray<FLDCombatActionReceipt>& Arrived,uint32 Match,uint32 Life,uint64& ToldProtectionLifeRevision);

    // When the docked three-choice panel folds back to the pending-choice pill by itself, so it does not keep covering
    // the right side of the battlefield. Folding only hides the panel: the opportunity, the offer and any submitted
    // request keep their state, and nothing is picked. Local presentation time only.
    // - The opening offer's automatic open (D10) folds after AutoOpenSeconds unless the player has engaged with it
    //   (a previewed card, the replacement confirmation or a request in flight); engaging ends that timer for the opening.
    // - A hit on the own body folds an open panel unless the player is engaged. After such a fold, the player's next own
    //   opening is respected and is not folded by hits again until it closes.
    class LOCKDOWN_API FChoicesFold
    {
    public:
        static constexpr double AutoOpenSeconds=6.;
        void Opened(bool bAutomatic,double Now);
        void Closed();
        // True when the open panel should fold now; the caller closes the panel, which reports Closed().
        bool ShouldFold(double Now,bool bEngaged,bool bOwnHit);
        bool IsAutomatic() const {return bOpen&&bAutomatic;}
    private:
        bool bOpen=false;
        bool bAutomatic=false;
        bool bHitImmune=false;
        bool bFoldedByHit=false;
        double OpenedAt=0;
    };

    // The top-left event feed: room-public events as short lines, oldest first, so the newest sits at the bottom. Each line
    // stays LineSeconds on local presentation time; beyond MaxLines the oldest leaves early. Sources are the public match
    // view, the public kill feed and the public roster; the only owner fact is the own airdrop claim serial, which tells
    // its owner 你抢到了 while everyone else only learns that the drop is gone.
    struct FEventFeedInput
    {
        const FLDPublicMatchView* Public=nullptr;
        const FLDKillFeedView* Kills=nullptr;
        const FLDLobbyView* Lobby=nullptr;
        FGuid Self;
        uint64 OwnAirdropClaimSerial=0;
        bool bClock=false;
        int64 NowMs=0;
        double LocalSeconds=0;
        FString LowCampName,HighCampName,BossName;
    };
    struct FEventLine
    {
        FString Text;
        bool bSelf=false;   // the line is about the player (said with 你), so the feed can highlight it
        double At=0;        // local presentation time it was first shown
        uint64 AirdropClaimSerial=0;  // a drop claim line: relabelled while shown, as the owner view may arrive later
    };
    // Kills are told when their notice arrives, unless it was already older than StaleKillMs (a late join). Every other
    // event is a change between two observations of the same match: the first observation of a match only records the
    // state, so a screen that joins mid-match never replays what already happened. Told events: the flagpole opening, a
    // drop landing and being claimed, the Boss appearing and going down, a new 上热搜 leader, a new graffiti owner and the
    // same owner's frozen guard dropping to zero (he left the zone while a challenger was in it).
    class LOCKDOWN_API FEventFeed
    {
    public:
        static constexpr double LineSeconds=5.;
        static constexpr int32 MaxLines=6;
        static constexpr int64 StaleKillMs=6000;
        void Observe(const FEventFeedInput& Input);
        const TArray<FEventLine>& Lines() const {return Shown;}
    private:
        TArray<FEventLine> Shown;
        uint32 Generation=0;
        bool bPrimed=false;
        uint64 KillSerial=0;
        ELDMatchPhase Phase=ELDMatchPhase::Faulted;
        uint64 AirdropSerial=0;
        ELDAirdropPhase AirdropPhase=ELDAirdropPhase::None;
        ELDBossPhase BossPhase=ELDBossPhase::None;
        FGuid Leader;
        FGuid GraffitiOwner;
        // The guard as last observed: frozen progress, or a running guard (GuardDueAtMs set).
        int64 GuardKeptMs=0;
        int64 GuardDueAtMs=0;
    };
}
