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
struct FLDLobbyView;
namespace LDHUD
{
    // Display text of an approved content definition from the DemoContent string table. False when the
    // definition, its display entry, the table or the key is missing: Out then holds the stable content id,
    // so the content error stays visible instead of a guessed name.
    LOCKDOWN_API bool ContentText(const FLDDemoContentDefinitions* Content,const FString& ContentId,bool bDescription,FString& Out);
    LOCKDOWN_API FString ContentName(const FLDDemoContentDefinitions* Content,const FString& ContentId);
    LOCKDOWN_API FString StatusName(const FLDDemoContentDefinitions* Content,uint8 StatusId);
    LOCKDOWN_API FString CreatureName(const FLDDemoContentDefinitions* Content,uint8 CreatureId);
    LOCKDOWN_API FString EquipmentName(const FLDDemoContentDefinitions* Content,uint8 EquipmentId);
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
    // Whole seconds rounded up as m:ss, so a running countdown never shows 0:00 early.
    LOCKDOWN_API FString Clock(int64 RemainingMs);
    struct FCircleView
    {
        FString Line;
        bool bProgress=false;
        float Progress=0.f;
    };
    // Public capture state (APP_SPEC 2.2). Reaching zero only waits for the authority; it never announces a winner.
    LOCKDOWN_API FCircleView Circle(const FLDPublicMatchView& Public,const FString& OccupierName,bool bClock,int64 NowMs);
    // Public contender death notice; names come from the public roster and camp tiers from approved display text.
    LOCKDOWN_API FString KillLine(const FLDKillNotice& Notice,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName);
    struct FKillFeedLineView
    {
        FString Text;
        bool bSelf=false;
    };
    // A notice naming the player as victim or killer says 你 there and is marked so the feed can highlight it.
    LOCKDOWN_API FKillFeedLineView KillFeedLine(const FLDKillNotice& Notice,const FGuid& Self,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName);
    // Cause line of the own death banner: the newest public notice of this generation naming the player as the victim at or
    // after NotBeforeMs (when the life that died was first seen alive, so an older death is never shown). Killers come from
    // the public roster and camp tiers from approved display text; empty while no such notice is known.
    LOCKDOWN_API FString DeathCause(const FLDKillFeedView& Feed,uint32 Generation,const FGuid& Self,int64 NotBeforeMs,const FLDLobbyView& Lobby,const FString& LowCampName,const FString& HighCampName);
    LOCKDOWN_API FString ResultHeadline(const FLDResultView& Result,const FString& WinnerName);
    struct FResultRowView
    {
        FString Name;
        FString Kills;
        FString Deaths;
        FString Level;
        bool bWinner=false;
    };
    // One frozen row in locked roster order; statistics are display only and never decide the winner.
    LOCKDOWN_API FResultRowView ResultRow(const FLDResultRow& Row,const FGuid& Winner,const FGuid& Self);

    enum class EActionSlot : uint8 { Q, W, Item1, Item2 };
    enum EActionBlock : uint32
    {
        ActionUnsynced=1<<0, ActionEmpty=1<<1, ActionNoActive=1<<2, ActionCooldown=1<<3, ActionMana=1<<4,
        ActionControlled=1<<5, ActionDead=1<<6, ActionNotPlaying=1<<7, ActionServer=1<<8
    };
    struct FActionInput
    {
        EActionSlot Slot=EActionSlot::Q;
        bool bProjected=false;    // a current owner combat projection exists for this match and life
        bool bHasSource=false;
        FString Name;             // display name of the bound definition
        uint8 Rank=0;             // learned rank; equipment has none
        bool bHasActive=false;
        int64 ManaCostP2=0;
        int64 ReadyAtMs=0;        // this source's own track end on the authority clock
        int64 CooldownMs=0;       // duration last issued on this track, for the sweep only
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
    };
    struct FActionView
    {
        EActionSlot Slot=EActionSlot::Q;
        FString Key;
        FString Kind;
        FString Name;
        FString State;
        FString Tooltip;
        // Intent submission and execution limits are separate (UI 4): blocks grey the slot and explain why, but an
        // explicit activation is still sent so the server decides, including protection removal and EmptySource.
        bool bCanSubmitIntent=false;
        uint32 ExecutionBlocks=0;
        int64 CooldownRemainingMs=0;
        float CooldownFraction=0.f;
        FString Countdown;        // CooldownText of the remainder; empty when not cooling
        FString Cost;             // the active's mana cost rounded like the shown mana, e.g. 胆量 30; empty without one
    };
    LOCKDOWN_API FActionView Action(const FActionInput& Input,const FActionContext& Context);
    // Slot look: every limit has its own look and caption, so no state relies on colour alone. A dead, unsynced or empty
    // slot outranks arming; an armed entry outranks its remaining limits, then control, cooldown, mana and server reasons.
    enum class EActionLook : uint8 { Ready, Armed, Cooling, NoMana, Controlled, Empty, Blocked };
    struct FActionLookView
    {
        EActionLook Look=EActionLook::Blocked;
        FString Caption;
    };
    LOCKDOWN_API FActionLookView ActionLook(const FActionView& View,bool bArmed);

    struct FObservedCard
    {
        FString Title;
        FResourceView Health;
        bool bMana=false;
        FResourceView Mana;
        FString Statuses;
        bool bEquipment=false;
        FString Equipment;
    };
    // SC-07 whitelist only: name, level, HP, MP, public statuses and two equipment definitions.
    LOCKDOWN_API FObservedCard ObservedCard(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec);
    LOCKDOWN_API FString ObservedUnit(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec);

    // Typed parameters of approved content (CONTENT 12.5): prose comes from the display table, numbers only from the
    // immutable snapshot fields, formatted here without recomputing any battle formula.
    LOCKDOWN_API FString StatLine(const FLDDemoStatBonus& Stat);
    LOCKDOWN_API FString EffectLine(const FLDDemoEffectDefinition& Effect);
    LOCKDOWN_API FString TargetLine(const FLDDemoTargetDefinition& Target);
    // Effect lines of one rank; actives add their mana cost and cooldown.
    LOCKDOWN_API TArray<FString> RankLines(const FLDDemoRankDefinition& Rank,bool bActive);
    struct FChoiceCardView
    {
        FString Kind;
        FString Name;
        FString RankText;
        FString Description;
        TArray<FString> Lines;
        bool bNeedsReplacement=false;
    };
    // One of the three fixed options of the current offer; a learn-active card with both Q/W filled needs an explicit replacement.
    LOCKDOWN_API FChoiceCardView ChoiceCard(const FLDChoiceOption& Option,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    struct FShopItemView
    {
        FString Name;
        FString Price;
        TArray<FString> Lines;
        bool bAffordable=false;
    };
    LOCKDOWN_API FShopItemView ShopItem(const FLDShopItemView& Item,int64 Gold,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* Content);
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
    // Own temporary statuses for the HUD band: passive auras and respawn protection (shown on the life line) are left
    // out; shield points round up and a remainder needs the matching authority clock. Empty when none.
    LOCKDOWN_API FString SelfStatusLine(const FLDCombatOwnerView& Combat,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* Content);
    // Own passives for the HUD band: learned passives with their rank, then each equipment passive once, named after
    // the equipment granting it. A missing projection reads as syncing, never as "none".
    LOCKDOWN_API FString PassiveLine(const FLDProgressionView* Progression,const FLDCombatOwnerView* Combat,const FLDDemoContentDefinitions* Content);
    LOCKDOWN_API FString EntryLabel(ELDCombatEntry Entry);
    // Combat receipt notice: an execution failure of the player's own request and the loss of respawn protection are
    // separate facts (UI 6.2); a cancel or a non-final phase says nothing. Empty when there is nothing to tell.
    LOCKDOWN_API FString CombatReceiptNotice(const FLDCombatActionReceipt& Receipt);
    // The same execution failure for the pressed Q/W/1/2 slot, which already names its entry: the reason only. Empty for
    // other entries and for anything CombatReceiptNotice would not report as a failure.
    LOCKDOWN_API FString CombatReceiptSlotNotice(const FLDCombatActionReceipt& Receipt);
}
