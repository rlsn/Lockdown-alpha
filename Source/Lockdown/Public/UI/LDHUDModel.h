#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDProgressionRules.h"
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
struct FLDDemoAbilityValues;
struct FLDDemoStatDefinition;
struct FLDLobbyView;
struct FLDPKParameters;
namespace LDHUD
{
    // #147/#198 card text: the owner's level grows an active's fixed amounts (G(L)); its stats fill the coefficients.
    struct FCardStats
    {
        uint8 Level=1;
        int64 ADP2=0;
        int64 APP2=0;
        int64 MaxHPP2=0;
        int64 ArmorP2=0;
        // A blueprint records a level but not the final projected attributes; show their coefficients instead.
        bool bShowScalingFormula=false;
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
        // Each placeholder's value, in template order ("?" when it reads nothing).
        TArray<FString> Values;
    };
    // Renders an active's card template (LDDemoCardText) against its single value set grown for Stats.Level. The glossary
    // keywords (LDTerms::CardKeywords) are wrapped in <b>…</> in Rich.
    LOCKDOWN_API FCardText CardText(const FString& Template,const FLDDemoAbilityValues& Values,const FCardStats& Stats,
        int64 GrowthPerLevelBp,int64 GrowthPerLevelAfter10Bp=0);
    // Renders a stat's card template: each [per_point.<Stat>] reads that per-point bonus of Stat, which never grows.
    LOCKDOWN_API FCardText StatCardText(const FString& Template,const FLDDemoStatDefinition& Stat);
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
    // The display row's IconId of a content entry (#85); empty without a display row.
    LOCKDOWN_API FString ContentIconId(const FLDDemoContentDefinitions* Content,const FString& ContentId);
    LOCKDOWN_API FString StatusName(const FLDDemoContentDefinitions* Content,uint8 StatusId);
    LOCKDOWN_API FString CreatureName(const FLDDemoContentDefinitions* Content,uint8 CreatureId);
    LOCKDOWN_API FString AbilityName(const FLDDemoContentDefinitions* Content,uint8 AbilityId);
    // Public roster name of a contender; empty when the roster does not list it (never invented).
    LOCKDOWN_API FString RosterName(const FLDLobbyView& Lobby,const FGuid& ContenderId);
    // The name over an observed contender-kind unit: its roster name; in a rest (bInRest) a contender id the roster does not
    // list is the own personal training dummy (only its owner observes it), named LDTerms::TrainingDummy. Empty otherwise.
    LOCKDOWN_API FString ObservedContenderName(const FLDLobbyView& Lobby,const FGuid& ContenderId,bool bInRest);
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
    LOCKDOWN_API FDeathView Death(const FLDOwnerState& Own,bool bClock,int64 NowMs,bool bFinalEliminated=false);
    LOCKDOWN_API FString PhaseLine(ELDMatchPhase Phase);
    // #198: the centre-screen return countdown of a rest: whole seconds rounded up ("5" .. "1") during the Return step until
    // its end (StepEndsAtMs; held at "1" until the authority enters the field), then LDTerms::CountdownGo for GoShowMs from
    // the field segment's start (Timeline.FieldStartedAtMs). Empty otherwise, without a clock or for another generation's
    // timeline; reaching zero only waits for the authority's field.
    inline constexpr int64 CountdownGoShowMs=1000;
    LOCKDOWN_API FString RestReturnCountdown(const FLDPublicMatchView& Public,bool bClock,int64 NowMs);
    // The player-facing name of a rest step; empty for None.
    LOCKDOWN_API FString RestStepText(ELDRestStep Step);
    // #198 timeline banner over the public capture line. Rest: "休息 R{n} · <step> · 剩 X 秒" and a detail line: in the build
    // step the PK sign-up window (opens in X s, with where the sign-up disk is / open with X s left), the number standing in
    // their disks and whether Self does; after the lock "PK 跳过" or the entrants, and that the build is locked outside the
    // build and payout steps. Field: "野外 n/3 · 下次休息 m:ss" from the field progress T
    // (LDMatchTimeline::FieldNowFromView); final field: "最终野外 · 旗杆开放". Empty for another generation's timeline or no
    // stage. Without a clock no remainder is guessed.
    struct FTimelineView
    {
        FString Headline;
        FString Detail;
        // The build step of a rest: the own sign-up disk is drawn (ALDSignupMarker).
        bool bSignupStep=false;
        // The sign-up window is open at NowMs: standing in the own disk signs up (the server judges the committed foot).
        bool bSignupOpen=false;
        // The public sign-ups name Self (it stands in its disk).
        bool bSignedUp=false;
    };
    LOCKDOWN_API FTimelineView TimelineBanner(const FLDPublicMatchView& Public,const FGuid& Self,bool bClock,int64 NowMs);
    // The player-facing result of an own training reset (ALDPlayerController::OnTrainingReset); every refusal says why.
    LOCKDOWN_API FString TrainingResetText(ELDTrainingResetResult Result);
    // ---- #198 session D: the PK of a rest and its spectators (public FLDPublicMatchView::PK, approved issue #198 comment
    // 6752601175). Presentation only: the server decides every bet and throw and the outcome. ----
    // 单挑 / 混战 / NvN from the drawn format: Teams split Entrants evenly over SideCount ("2v2", "3v3"). Empty for None or
    // a malformed team split.
    LOCKDOWN_API FString PKFormatText(ELDPKFormat Format,uint8 SideCount,int32 Entrants);
    // Relationship to an observed, publicly identified PK entrant. Team labels exist only for the current public team PK.
    LOCKDOWN_API FString PKRelationText(const FLDPublicMatchView& Public,const FGuid& Self,const FGuid& Target);
    // The public PK of this rest: the match, its timeline and its PK name the same generation, the stage is a rest and the
    // PK has a serial (0: this rest has no PK).
    LOCKDOWN_API bool HasPK(const FLDPublicMatchView& Public);
    // Self watches the arena: HasPK, the PKReady or PK step, and Self is not a living entrant (a spectator, an eliminated or
    // a forfeited entrant). The camera centres on PK.ArenaCentreCm meanwhile (ALDCharacter::CalcCamera); the caller checks
    // that the public view is the own match.
    LOCKDOWN_API bool IsSpectatingPK(const FLDPublicMatchView& Public,const FGuid& Self);
    // The contenders of one side in seat order, by public roster name ("、"-joined; an unlisted one is LDTerms::PKUnlisted).
    LOCKDOWN_API FString PKSideNames(const FLDPKView& PK,uint8 Side,const FLDLobbyView& Lobby);
    // The player-facing result of an own bet (ALDPlayerController::OnPKBet); only Placed reads as done, every refusal says why.
    LOCKDOWN_API FString PKBetText(ELDPKBetResult Result);
    // The player-facing result of an own throw (ALDPlayerController::OnPKThrow); only Thrown reads as done.
    LOCKDOWN_API FString PKThrowText(ELDPKThrowResult Result);
    // The own throw cooldown from the last answer's ReadyAtMs against the same-match authority time: "投掷冷却 X 秒" (like
    // CooldownText, never 0.0 while time remains); empty when ready (ReadyAtMs <= NowMs or none); syncing without a clock.
    LOCKDOWN_API FString PKThrowCooldown(int64 ReadyAtMs,bool bClock,int64 NowMs);
    // The frozen result of a decided PK: the winning side's names, 平局 or 本场无效; empty while undecided.
    LOCKDOWN_API FString PKResultText(const FLDPKView& PK,const FLDLobbyView& Lobby);
    struct FPKAwardRow
    {
        FString Name;
        // "参与 +1 · 胜者 +2 · 竞猜 +1 · 共 4 点" (zero parts left out).
        FString Points;
        int64 Total=0;
        bool bSelf=false;
    };
    // One row per PK.Awards entry with any points, in award order; names from the public roster.
    LOCKDOWN_API TArray<FPKAwardRow> PKAwardRows(const FLDPKView& PK,const FLDLobbyView& Lobby,const FGuid& Self);
    struct FPKSideView
    {
        uint8 Side=0;
        // The side's names with each member's 出局/离场 mark once the fight started, then the public bet count.
        FString Line;
        // "押 <names>" for the bet button.
        FString BetAction;
        bool bOwnSide=false;
        bool bOwnBet=false;
        bool bWinner=false;
        // Every member is out (eliminated or forfeited) after the fight started.
        bool bOut=false;
    };
    struct FPKPanelInput
    {
        FGuid Self;
        bool bClock=false;
        int64 NowMs=0;
        // The last own throw answer's ReadyAtMs for this PK (0: none yet).
        int64 ThrowReadyAtMs=0;
        bool bThrowArmed=false;
        bool bThrowInFlight=false;
        // Approved ring parameters for the ring line; null shows no ring line.
        const FLDPKParameters* Params=nullptr;
    };
    // The PK panel under the timeline: shown from the Draw to the Payout step of a rest with a PK. Bets show to an eligible
    // bettor (PK.Bettors) during the betting step only; the throw shows to an eligible bettor during the PK step while the
    // fight runs; the result and awards show in the settlement and payout steps. Eliminated entrants are read-only.
    struct FPKPanelView
    {
        bool bVisible=false;
        FString Title;
        // PKReady: "N 秒后开打 …"; PK: "最长还剩 N 秒 …" (whole seconds rounded up to the step end).
        FString Countdown;
        // "安全圈 18.0 米 · 10 秒后缩到 6.0 米" during the PK step with parameters and a clock.
        FString Ring;
        TArray<FPKSideView> Sides;
        FString Role;
        bool bSpectating=false;
        bool bCanBet=false;
        uint8 OwnBet=0;
        bool bCanThrow=false;
        // The throw button can be pressed now (not cooling, not in flight).
        bool bThrowReady=false;
        FString ThrowAction;
        bool bResult=false;
        FString Result;
        bool bOwnWin=false;
        TArray<FPKAwardRow> Awards;
    };
    LOCKDOWN_API FPKPanelView PKPanel(const FLDPublicMatchView& Public,const FLDLobbyView& Lobby,const FPKPanelInput& Input);
    // The ring clock: ms since PK.FightStartedAtMs at NowMs (0 before the fight or without a clock).
    LOCKDOWN_API int64 PKRingElapsedMs(const FLDPKView& PK,bool bClock,int64 NowMs);
    // Whole seconds rounded up as m:ss, so a running countdown never shows 0:00 early.
    LOCKDOWN_API FString Clock(int64 RemainingMs);
    // The match timer over the minimap: whole seconds elapsed since the match start (StartedAtMs, the opening rest R0),
    // rounded down, as m:ss with no leading zero on the minutes ("0:12", "12:05"). Empty without a clock or a start.
    LOCKDOWN_API FString MatchTimer(const FLDPublicMatchView& Public,bool bClock,int64 NowMs);
    struct FCircleView
    {
        FString Line;
        bool bProgress=false;
        float Progress=0.f;
    };
    // Public spray and guard state (APP_SPEC 2.2). Reaching zero only waits for the authority; it never announces a
    // spray or a winner. Before the final field the flagpole is closed and opens after the last rest (R3); its opening
    // instant is not scheduled yet, so no countdown is printed. The progress bar shows the sole sprayer's progress (Solo),
    // a paused holder's kept progress against SprayMs (Contested), otherwise the graffiti owner's guard against GuardMs
    // (CAPTURE.SpraySec/GuardSec). The graffiti owner and the guard are named in every open state; a contest without a
    // holder names no sprayer.
    LOCKDOWN_API FCircleView Circle(const FLDPublicMatchView& Public,const FString& SprayerName,const FString& OwnerName,bool bClock,int64 NowMs,
        int64 SprayMs=0,int64 GuardMs=0);
    // #198: a charm's map colour (heal green, mana blue, move speed gold).
    LOCKDOWN_API FLinearColor CharmColor(ELDCharmKind Kind);
    // The event feed's line for a downed Boss: the own last hit is told it got a build reward, others see the public roster name.
    LOCKDOWN_API FString BossDownLine(const FLDBossView& Boss,const FGuid& Self,const FString& KillerName,const FString& BossName);
    // #198: the camp marker colour shared by the minimap ring and the world label.
    LOCKDOWN_API FLinearColor MarkerColor();
    // D27 (#79): the public 上热搜 line: who is named; the own player is told directly.
    // Empty when nobody is named.
    LOCKDOWN_API FString LeaderLine(const FLDLeaderView& Notice,const FGuid& Self,const FString& LeaderName);
    // Public contender death notice; names come from the public roster and camp tiers from approved display text.
    // A contender kill lists its assisters after the line (AssisterNames in the notice's order).
    LOCKDOWN_API FString KillLine(const FLDKillNotice& Notice,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName,const FString& EliteCampName,const FString& BossName,const TArray<FString>& AssisterNames=TArray<FString>());
    struct FKillFeedLineView
    {
        FString Text;
        bool bSelf=false;
    };
    // A notice naming the player as victim, killer or assister says 你 there and is marked so the feed can highlight it.
    LOCKDOWN_API FKillFeedLineView KillFeedLine(const FLDKillNotice& Notice,const FGuid& Self,const FString& VictimName,const FString& KillerName,const FString& LowCampName,const FString& HighCampName,const FString& EliteCampName,const FString& BossName,const TArray<FString>& AssisterNames=TArray<FString>());
    // Cause line of the own death banner: the newest public notice of this generation naming the player as the victim at or
    // after NotBeforeMs (when the life that died was first seen alive, so an older death is never shown). Killers come from
    // the public roster and camp tiers from approved display text; empty while no such notice is known.
    LOCKDOWN_API FString DeathCause(const FLDKillFeedView& Feed,uint32 Generation,const FGuid& Self,int64 NotBeforeMs,const FLDLobbyView& Lobby,const FString& LowCampName,const FString& HighCampName,const FString& EliteCampName,const FString& BossName);
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

    // The band's four slots: the Q/W/E actives and the D dash.
    enum class EActionSlot : uint8 { Q, W, E, Dash };
    enum EActionBlock : uint32
    {
        ActionUnsynced=1<<0, ActionEmpty=1<<1, ActionCooldown=1<<3, ActionMana=1<<4,
        ActionControlled=1<<5, ActionDead=1<<6, ActionNotPlaying=1<<7, ActionServer=1<<8
    };
    struct FActionInput
    {
        EActionSlot Slot=EActionSlot::Q;
        bool bProjected=false;    // a current owner combat projection exists for this match and life
        bool bHasSource=false;    // an active is bound
        FString Name;             // display name of the bound definition
        FName DefinitionId;       // the bound definition, for the hover description
        FString IconId;           // the bound definition's icon (LDUIIcons); the dash's own icon for D
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
        // #147/#198: the owner's level and stats, so a skill's card text shows the numbers it has now.
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
        FString IconId;           // the icon texture id (LDUIIcons); empty without a source
        FString Glyph;            // shown only when IconId has no texture: the name's first character; empty without a source
    };
    LOCKDOWN_API FActionView Action(const FActionInput& Input,const FActionContext& Context);
    // Slot look: the slot face says its state by colour alone (the tooltip keeps the words). A dead, unsynced or empty
    // slot outranks arming; an armed entry outranks its remaining limits, then control, cooldown, mana and server reasons.
    enum class EActionLook : uint8 { Ready, Armed, Cooling, NoMana, Controlled, Empty, Blocked };
    LOCKDOWN_API EActionLook ActionLook(const FActionView& View,bool bArmed);
    // The hover description of a slot, laid out like the growth panel's popups: the name, "<kind> [<key>] · <state>", then
    // the active's card sentence with the owner's current numbers, the targeting and the cost (the dash: its reach).
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
        // A visible contender's Q/W/E actives (#112 §10, #198 single level): key and display name; never cooldowns.
        struct FActiveChip
        {
            FString Key;
            FString Name;
            bool bEmpty=true;
        };
        bool bActives=false;
        FActiveChip Actives[3];
    };
    // SC-07 whitelist only: name, level, HP, MP, public statuses and the Q/W/E actives.
    LOCKDOWN_API FObservedCard ObservedCard(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec);
    LOCKDOWN_API FString ObservedUnit(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec);

    // Typed parameters of approved content (CONTENT 12.5): prose comes from the display table, numbers only from the
    // immutable snapshot fields, formatted here without recomputing any battle formula.
    LOCKDOWN_API FString StatLine(const FLDDemoStatBonus& Stat);
    LOCKDOWN_API FString EffectLine(const FLDDemoEffectDefinition& Effect);
    LOCKDOWN_API FString TargetLine(const FLDDemoTargetDefinition& Target);
    // The mana cost and cooldown line of an active's value set.
    LOCKDOWN_API FString CostLine(const FLDDemoAbilityValues& Values);
    // #198 growth panel: one active of the current offer, a pending blueprint or a learned slot, rendered for the owner's
    // current level and stats. Card faces show the icon, the name, the sentence and the targeting/cost line; the hover popup
    // adds the keyword explanations.
    struct FActiveCardView
    {
        FName DefinitionId;
        FString Name;
        // Display-table icon reference (empty without a display row), drawn through LDUIIcons; Glyph (the name's first
        // character) shows only when that texture is missing.
        FString IconId;
        FString Glyph;
        // The one-sentence rule with this level's numbers, as rich text with the keywords in <b>…</>, and its plain twin.
        FString Sentence;
        FString SentencePlain;
        // "<b>关键词</>：解释" for every keyword the sentence uses, in glossary order.
        TArray<FString> Keywords;
        FString Target;
        FString Cost;
        // False when the catalog has no such active or display row; Sentence then says so instead of inventing a face.
        bool bKnown=false;
    };
    LOCKDOWN_API FActiveCardView ActiveCard(FName DefinitionId,const FLDDemoContentDefinitions* Content,const FCardStats& Stats);
    // One of the point-bought stats (#198) with the points the owner allocated to it and its per-point text.
    struct FStatRowView
    {
        FName StatId;
        FString Name;
        FString IconId;
        FString Glyph;
        FString PerPoint;
        FString PerPointRich;
        int32 Points=0;
        bool bKnown=false;
    };
    LOCKDOWN_API FStatRowView StatRow(const FLDDemoStatDefinition& Stat,int32 Points,const FLDDemoContentDefinitions* Content);
    // The points the own progression allocated to StatId; 0 when it lists none.
    LOCKDOWN_API int32 StatPointsOf(const FLDProgressionView& Progression,FName StatId);
    // One attribute a point of a stat raises: its name, its current final value from the own projection ("?" while the
    // projection lacks it) and the gain of one point ("+2.5", "+2%", "+0.08/s").
    struct FStatEffectView
    {
        FString Attribute;
        FString Current;
        FString PerPoint;
    };
    // Every attribute in Stat's per-point list, in that order (the stat icon's hover popup).
    LOCKDOWN_API TArray<FStatEffectView> StatEffects(const FLDDemoStatDefinition& Stat,const TArray<FLDCombatAttributeRowView>& Attributes);
    // The active in Q/W/E slot Slot, or None.
    LOCKDOWN_API FName ActiveInSlot(const FLDProgressionView& Progression,uint8 Slot);
    // "Q", "W" or "E": the server's slot name (widget names, observed contenders).
    LOCKDOWN_API FString SlotKey(uint8 Slot);
    // The key the own player presses for each server slot, as the HUD shows it: the player's bindings and local slot order
    // decide it, so text that names an own slot reads the same key as the HUD slot. Unset entries read as SlotKey.
    struct FSlotKeys
    {
        FString Keys[3];
        LOCKDOWN_API FString Of(uint8 Slot) const;
        bool operator==(const FSlotKeys& Other) const{return Keys[0]==Other.Keys[0]&&Keys[1]==Other.Keys[1]&&Keys[2]==Other.Keys[2];}
    };
    // What the own contender may do with its build right now, judged like the server judges a commit (LDProgression's
    // CommandGate over the build stage of the public match, LDProgression::StageForPublic, and the own life phase). A hint
    // only: the server decides each command. Each Blocked line names the reason ("阵亡中", "休息时才能用"); empty when
    // allowed. #222: choosing, learning, allocating and installing are open at all times of a running match.
    struct FBuildGateView
    {
        bool bCanChoose=false;
        FString ChooseBlocked;
        bool bCanLearn=false;
        FString LearnBlocked;
        bool bCanAllocate=false;
        FString AllocateBlocked;
        // #198 rest services: open only in a rest build window (the build and payout steps), alive.
        bool bCanBuyNpc=false;
        FString NpcBlocked;
        bool bCanRespec=false;
        FString RespecBlocked;
        bool bCanReplace=false;
        FString ReplaceBlocked;
        // Phase 9: the final mod three-choice.
        bool bCanFinalMod=false;
        FString FinalModBlocked;
        bool bCanForget=false;
        FString ForgetBlocked;
        // #198 G: an elite camp's pending component choice (like the active choice: field or rest, alive or dead) and the
        // rest material services FillGap and Dismantle (rest build window only, like the NPC).
        bool bCanComponentChoice=false;
        FString ComponentChoiceBlocked;
        bool bCanFillGap=false;
        FString FillGapBlocked;
        bool bCanDismantle=false;
        FString DismantleBlocked;
        // The stage is a rest (open or locked): the panel shows the rest services, enabled or with their reasons.
        bool bInRest=false;
        // #198 B: installing, moving and uninstalling a component, as growth gates them (LDProgression::InstallGate). The
        // opening base pick itself follows bCanChoose.
        bool bCanInstall=false;
        FString InstallBlocked;
    };
    LOCKDOWN_API FBuildGateView BuildGate(LDProgression::EMatchStage Stage,ELDLifePhase LifePhase);
    // The player-facing why of a refused or stored build step.
    LOCKDOWN_API FString BuildBlockedText(ELDGrowthReason Reason);
    // The result line of an applied build receipt: 已学到 Q：名称, 已存为图纸：名称（原因）, the points added and left, or a
    // rest service's result (the NPC points paid, the respec refund, the replaced or forgotten active), or the component taken,
    // enabled (已启用构件：名称), stored with the reason or removed; #198 G the component choice taken (or its points), the gap
    // filled with its price, and the dismantle's removed, received and uninstalled components.
    // Keys names a learned or replaced slot.
    LOCKDOWN_API FString BuildReceiptText(const FLDBuildReceipt& Receipt,const FLDDemoContentDefinitions* Content,const FSlotKeys& Keys=FSlotKeys());
    // 火 / 水 / 电.
    LOCKDOWN_API FString ElementName(ELDDemoElement Element);
    // The learned actives an ActiveElement component may bind (its BindActives), in Q/W/E order.
    LOCKDOWN_API TArray<FName> BindableActives(const FLDDemoComponentDefinition& Component,const FLDProgressionView& Progression);
    // #198 F: one place an owned mod or connector may be installed now (a hint; the server decides): a learned active
    // (Active, TargetId), another installed component (Component, TargetId), the innate D (Dash) or the holder (None).
    struct FWorkshopTargetView
    {
        ELDComponentTargetKind Kind=ELDComponentTargetKind::None;
        FName TargetId;
        // The active's or component's display name, 冲刺(D) or 自身.
        FString Label;
    };
    // The places Component takes now, in its catalog list order: each learned active of TargetActives, each installed other
    // component of TargetComponents, then 冲刺(D) (bTargetDash) and 自身 (bTargetSelf). Empty for a base component.
    LOCKDOWN_API TArray<FWorkshopTargetView> WorkshopTargets(const FLDDemoComponentDefinition& Component,const FLDProgressionView& Progression,
        const FLDDemoContentDefinitions* Content);
    // C12: the installed bases of its ExtraBases it may repeat, in list order, without a closed element (water and electric
    // wait for #198 A). Empty for any other component.
    LOCKDOWN_API TArray<FName> RepeatableBases(const FLDDemoComponentDefinition& Component,const FLDProgressionView& Progression,
        const FLDDemoContentDefinitions* Content);
    // What an installed component sits on, for its row: B18's "（技能 · 元素）", a mod or connector's "（装在：X）" and C12's
    // "（X · 重复 Y）"; empty when it is not installed or a plain base.
    LOCKDOWN_API FString InstalledTargetText(const FLDComponentInstanceView& Instance,const FLDDemoContentDefinitions* Content);
    // A component's card: icon, name and its display sentence with the owner's level grown numbers (like an active's).
    struct FComponentCardView
    {
        FName DefinitionId;
        FString Name;
        FString IconId;
        FString Glyph;
        FString Sentence;
        FString SentencePlain;
        TArray<FString> Keywords;
        // Its kind and use tags for the popup's category line: "技能改装 · 弹道 · 法术".
        FString KindLine;
        // Enabling it asks for a learned active and an element first (B18).
        bool bBinding=false;
        // False when the catalog has no such component or display row.
        bool bKnown=false;
    };
    LOCKDOWN_API FComponentCardView ComponentCard(FName DefinitionId,const FLDDemoContentDefinitions* Content,const FCardStats& Stats);
    // The 构筑 tray's hover popup of a card (the HUD's slot popup; the card face shows only the icon and the name): the name,
    // the kind ("技能 · <target> · <cost>", "图纸 · …" or "构件"), then the full rule with this level's numbers and its
    // keyword explanations, and Outcome (what a click does besides the plain take, or why it cannot be taken now) when not
    // empty.
    LOCKDOWN_API FActionDetailView ActiveChoiceDetail(const FActiveCardView& Card,bool bBlueprint,const FString& Outcome);
    LOCKDOWN_API FActionDetailView ComponentChoiceDetail(const FComponentCardView& Card,const FString& Outcome);

    // #227 component storage (APP_SPEC §7): the owned components not installed, and the equipped columns 本体 (base components
    // and the holder's own mods), Q, W and E by server slot, and 冲刺(D). Each installed component sits in the column of what it
    // is installed on; one installed on another component nests under that host (Depth + 1), right after it. Local
    // presentation over the own progression view only; the server decides every install, move and uninstall.
    enum class EStorageColumn : uint8 { Body=0, SlotQ=1, SlotW=2, SlotE=3, Dash=4 };
    constexpr int32 StorageColumnCount=5;
    // The column of a learned active's server slot 0..2.
    LOCKDOWN_API EStorageColumn SlotColumn(uint8 Slot);
    struct FEquippedView
    {
        FName Component;
        // The installed component it sits on (None: the column itself) and how deep it nests.
        FName Host;
        int32 Depth=0;
        // B18's bound element ("火") and C12's "重复 X"; empty otherwise.
        FString Note;
    };
    struct FStorageColumnView
    {
        EStorageColumn Column=EStorageColumn::Body;
        // 本体, 冲刺, or 空技能槽 for an empty slot; empty for a held slot, whose place is its Active's name. Names the place
        // in notices; only 本体 shows it as a caption, the other columns stand on their own slot.
        FString Title;
        // A slot column: its learned active; None for an empty slot (which takes nothing), the body and the dash.
        FName Active;
        TArray<FEquippedView> Entries;
    };
    struct FStorageView
    {
        // Owned components not installed, in the view's order.
        TArray<FName> Stored;
        // Of those, the ones never enabled yet (the stack's badge).
        int32 Fresh=0;
        // Installed components and the level's workshop capacity (LDProgression::WorkshopCapacity); every install takes one.
        int32 Installed=0;
        int32 Capacity=0;
        // StorageColumnCount columns in EStorageColumn order.
        TArray<FStorageColumnView> Columns;
    };
    LOCKDOWN_API FStorageView ComponentStorage(const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content,
        const TSet<FName>* Seen=nullptr);
    // One place a component may be dropped now: a column, or an installed host component in it, with the install target the
    // command carries. B18 still asks for one of Elements (bound to BindActive); C12 for one of Bases.
    struct FComponentDrop
    {
        EStorageColumn Column=EStorageColumn::Body;
        FName Host;
        ELDComponentTargetKind Kind=ELDComponentTargetKind::None;
        FName TargetId;
        FName BindActive;
        TArray<ELDDemoElement> Elements;
        TArray<FName> Bases;
        // A replace drop (ReplaceDrops): the installed component the install takes off first, on whose tile Host it lands, and
        // everything that comes off with it (Replacing first, then DetachedWith(Replacing)).
        FName Replacing;
        TArray<FName> Lost;
    };
    // Every place Component may be dropped now, from the same lists the server's install rules read (BindableActives,
    // WorkshopTargets, RepeatableBases): a base on 本体; B18 on each bindable learned active's column; a mod or connector on
    // each learned active, installed host, D or 本体 it takes. A component dragged out of a column never lands on itself or on
    // anything installed on it (those come off with it), nor where an installed component of its form sits (TargetConflict).
    // Empty for an unknown component. A hint only.
    LOCKDOWN_API TArray<FComponentDrop> ComponentDrops(FName Component,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    // The installed component of Component's form on the place a drop names (any installed base of its form for a base), the
    // one the server's TargetConflict names; None when there is none.
    LOCKDOWN_API FName FormMateAt(FName Component,ELDComponentTargetKind Kind,FName TargetId,const FLDProgressionView& Progression,
        const FLDDemoContentDefinitions* Content);
    // The atomic replace drops of a stored Component (the server's InstallComponent with Replacing): one on the tile of each
    // installed component R where the plain drops take nothing, landing in R's column on the build with R and what sits on it
    // taken off. A full workshop or a form-mate on the place is the usual reason. Empty for an installed component.
    LOCKDOWN_API TArray<FComponentDrop> ReplaceDrops(FName Component,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    // What letting go of Component on Drop does, for the drag caption: "装到 Q", "替换 X · 连带拆下 Y、Z".
    LOCKDOWN_API FString DropPreviewText(FName Component,const FComponentDrop& Drop,const FLDProgressionView& Progression,
        const FLDDemoContentDefinitions* Content);
    // The owned components and learned actives Component works with, for the popup's 联动 line: what it installs on or
    // repeats (learned actives, owned host components, C12's bases), the owned mods and connectors that install on it, and its
    // owned form-mates (one per place). Empty when it names none.
    LOCKDOWN_API FString ConnectionsText(FName Component,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    // The drop of Drops a pointer over Column (and over the installed Host there, or None) selects: the host's own drop when
    // it takes one, else the column's own; null when neither does.
    LOCKDOWN_API const FComponentDrop* FindDrop(const TArray<FComponentDrop>& Drops,EStorageColumn Column,FName Host);
    // The hover line of a stored component: the columns its Drops reach, once each in column order ("可装：本体、Q、D"), or
    // that it has no place now.
    LOCKDOWN_API FString DropColumnsText(const TArray<FComponentDrop>& Drops);
    // Whether a stored component cannot be installed anywhere now because every workshop slot is taken (moving an installed
    // one is still open: it frees its own slot first).
    LOCKDOWN_API bool WorkshopFull(const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    // The where line of a stored component's popup: the columns it reaches, or that the workshop is full.
    LOCKDOWN_API FString StoredPlaceText(FName Component,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    // The installed components that come off with Component (installed on it, transitively, or repeating it), in the view's
    // order; empty when it is not installed.
    LOCKDOWN_API TArray<FName> DetachedWith(FName Component,const FLDProgressionView& Progression);

    // What the 构筑 tray has cards for in the own view of Match: the shown active offer, blueprints while a Q/W/E slot is
    // empty, the opening base pick and pending elite camp component choices (#198 G). The 构筑 button is lit while any is
    // pending and greyed out (unclickable) otherwise. Offers counts the queued active opportunities, so a new one pulses the
    // button. #227: owned components wait in the storage stack instead; unspent points wait on the HUD's stat icons and the
    // portrait's badge.
    struct FGrowthPending
    {
        bool bOffer=false;
        int32 Offers=0;
        int32 Blueprints=0;
        bool bOpeningBase=false;
        // #198 G: pending elite camp component choices.
        int32 ComponentChoices=0;
        // Phase 9: an untaken final mod three-choice.
        bool bFinalMod=false;
        bool Any() const {return bOffer||Blueprints>0||bOpeningBase||ComponentChoices>0||bFinalMod;}
    };
    LOCKDOWN_API FGrowthPending GrowthPending(const FLDProgressionView* Progression,uint32 Match,const FLDDemoContentDefinitions* Content);
    // The 构筑 tray shows its component rows (the opening base pick and the elite choices) as icon-only tiles instead of
    // named cards once they hold more than ChoiceTrayCardLimit cards together (an all-owned choice counts its points card).
    constexpr int32 ChoiceTrayCardLimit=6;
    LOCKDOWN_API bool ChoiceTrayTiles(const FLDProgressionView& Progression);
    // #198 G: the own view already owns a component of this definition.
    LOCKDOWN_API bool OwnsComponent(const FLDProgressionView& Progression,FName Component);
    // #198 G: every option of a pending component choice is already owned, so only the empty-pool points remain to take.
    LOCKDOWN_API bool ComponentChoiceAllOwned(const FLDComponentChoiceView& Choice,const FLDProgressionView& Progression);
    // #198 G rest material services (APP_SPEC §4.5) as the panel offers them, judged with growth's own rules over the owner
    // view (LDProgression::BuildFromView): a hint only, the server decides each command.
    struct FGapTargetView
    {
        // A never-enabled mod or connector without any compatible entry, and the unowned common bases that would give it one.
        FName Target;
        TArray<FName> Fillers;
    };
    struct FMaterialServicesView
    {
        // FillGap: the price (GROWTH.FillGapPoints), the targets with at least one filler in catalog order, and why it is
        // refused now (empty when it may be sent).
        int64 FillGapPoints=0;
        TArray<FGapTargetView> Gaps;
        FString FillGapBlocked;
        // Dismantle: the input count (GROWTH.DismantleInputCount), the owned common components in the view's order, and why it
        // is refused now.
        int64 DismantleCount=0;
        TArray<FName> DismantleCandidates;
        FString DismantleBlocked;
    };
    LOCKDOWN_API FMaterialServicesView MaterialServices(const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content,
        const FBuildGateView& Gate);
    // Why a dismantle of exactly these components would be refused (LDProgression::DismantleInputReason); empty when legal.
    LOCKDOWN_API FString DismantleSelectionRefusal(const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content,
        const TArray<FName>& Surrendered);
    // The installed components a dismantle of Surrendered sends back to the inventory: every installed one that sits on a
    // surrendered (or thereby uninstalled) host component or repeats such a base (C12), in the view's order.
    LOCKDOWN_API TArray<FName> DismantleDetached(const FLDProgressionView& Progression,const TArray<FName>& Surrendered);
    // #198 G field reward notices: the toast line of one field reward ("天降外卖：+200 流量，获得图纸「X」"), naming the event,
    // its XP and what its build part gave (a blueprint, a scarce or common component, the empty-pool points, a ready
    // two-choice or a held blueprint).
    LOCKDOWN_API FString FieldRewardEventName(FName Event);
    LOCKDOWN_API FString FieldRewardNotice(const FLDFieldRewardView& Reward,const FLDDemoContentDefinitions* Content);
    // The field rewards listed past the first Known entries of the own view (the list only grows within a match).
    LOCKDOWN_API TArray<FLDFieldRewardView> NewFieldRewards(const FLDProgressionView& Progression,int32 Known);
    // Tells each field reward once: the first valid view of a match primes (a reconnect never replays old rewards), later
    // views return the entries added since. Local presentation only.
    class LOCKDOWN_API FFieldRewardWatch
    {
    public:
        TArray<FLDFieldRewardView> Observe(const FLDProgressionView* Progression,uint32 Match);
    private:
        uint32 SeenMatch=0;
        int32 Known=0;
    };
    struct FAttributeRowView
    {
        FString Name;
        // The icon face's character while the attribute has no icon texture.
        FString Glyph;
        // The final value alone (no unit, no clamp note), for the HUD's attribute view.
        FString Value;
        FString Final;
        FString Breakdown;
        bool bClamped=false;
    };
    // One combat-projected attribute with its sources; the HUD never recomputes the projection.
    LOCKDOWN_API FAttributeRowView AttributeRow(const FLDCombatAttributeRowView& Row);
    // The attributes of the HUD's attribute view (ELDProjectedAttribute ids, in this order): every projected attribute the
    // HUD shows nowhere else. The health and mana bars already carry both maxima and both regenerations.
    LOCKDOWN_API TArray<uint8> AttributeViewOrder();
    LOCKDOWN_API FString GrowthReasonText(ELDGrowthReason Reason);
    LOCKDOWN_API FString CombatRejectText(ELDCombatReject Reject);
    // Owner combat entry -> action input; Unavailable stays a display-only server reason.
    LOCKDOWN_API FActionInput ActionInput(const FLDCombatEntryView& Entry,EActionSlot Slot,const FLDDemoContentDefinitions* Content);
    // One square of the own status strip above the centre of the band: a temporary status or an active recovery (脱战回复中,
    // 躺平角回血中).
    // IconId is its icon (LDUIIcons) and Glyph the name's first character, shown only without that texture; Tip is the
    // name with its stacks, shield and remainder. The hover popup is built from it by SelfIconDetail.
    // bPositive is the square's tone: green when it helps its bearer, red when it hurts (StatusIsPositive); recoveries
    // are always positive. #198 has no passive in content yet; base components bring their squares back (#198 PR3).
    enum class ESelfIconKind : uint8 { Control, Temporary };
    enum class ESelfIconSource : uint8 { Status, OutOfCombatRegen, DormRegen };
    struct FSelfIconView
    {
        ESelfIconKind Kind=ESelfIconKind::Temporary;
        ESelfIconSource Source=ESelfIconSource::Status;
        bool bPositive=true;
        FString IconId;
        FString Glyph;
        FString Tip;
        // The content entry the popup describes: the status.
        FString ContentId;
        // Stable within one status, so the HUD keeps its remaining-time ring across samples.
        FName Key;
        // Timed temporary statuses only: the authority instant it ends; 0 for conditional states.
        int64 ExpiresAtMs=0;
        uint8 Stacks=1;
    };
    struct FSelfIconsView
    {
        // Every one, unbounded: control statuses, then other temporary statuses and active recoveries.
        TArray<FSelfIconView> Items;
        // A missing own projection: the strip says syncing instead of showing none.
        bool bSyncing=false;
    };
    // Own temporary statuses (passive auras and respawn protection, which the life line shows, are left out; shield
    // points round up and a remainder needs the matching authority clock) and the active out-of-combat and dorm recoveries
    // (conditional, so no remaining time).
    LOCKDOWN_API FSelfIconsView SelfIcons(const FLDCombatOwnerView* Combat,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* Content);
    // The hover popup of one status square, laid out like the slot popup: the Tip line, the kind ("控制状态", "临时状态",
    // followed by its tone, " · 正面" or " · 负面"), then the full content description: a status's text or a recovery's rule
    // with the REGEN/DORM parameters. Built only for the hovered square.
    LOCKDOWN_API FActionDetailView SelfIconDetail(const FSelfIconView& Icon,const FLDDemoContentDefinitions* Content);
    // Whether a status helps its bearer (护盾, 增益, 强化普攻, 复活保护, 回位) rather than hurts it (every control, 减速,
    // 破防, 灼烧). A fixed property of the native status identity; an unknown id reads as negative.
    LOCKDOWN_API bool StatusIsPositive(uint8 StatusId);
    // The missing-icon stand-in of a name: its first character, Latin letters upper-cased; "?" for an empty name.
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

    // When the docked growth panel folds back to the 构筑 button by itself, so an automatic opening does not keep covering the
    // battlefield. Folding only hides the panel: the offer, the blueprints, the staged points and any submitted request keep
    // their state, and nothing is chosen. Local presentation time only.
    // #222: the player builds at any time, so a panel the player opened never folds by itself (not in combat, a PK or on a
    // hit). An automatic opening folds after AutoOpenSeconds or on an own-body hit, unless the player engages with it.
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
        double OpenedAt=0;
    };
    // D10/APP_SPEC §4.2: the growth panel opens by itself once per match, when the opening active offer (opportunity 1) is
    // first seen in a valid view of the current match, and (#198 B) once when the opening base pick first opens. Later
    // offers, new points and new components never open it (they only light the 开盲盒 button's count), so it does not
    // cover a fight. The caller still never opens it over the player's own panel or captured input. Local presentation only.
    class LOCKDOWN_API FGrowthAutoOpen
    {
    public:
        // True once per match for the first view that shows the opening offer, and once for the first showing the opening
        // base pick.
        bool Observe(const FLDProgressionView& View,uint32 Match);
    private:
        uint32 SeenMatch=0;
        bool bOpeningShown=false;
        bool bOpeningBaseShown=false;
    };

    // The top-left event feed: room-public events as short lines, oldest first, so the newest sits at the bottom. Each line
    // stays LineSeconds on local presentation time; beyond MaxLines the oldest leaves early. Sources are the public match
    // view, the public kill feed and the public roster; the only owner facts are the own airdrop claim serial, which tells
    // its owner 你抢到了 while everyone else only learns that the drop is gone, and the own field rewards (#198), which do
    // the same for a camp marker.
    struct FEventFeedInput
    {
        const FLDPublicMatchView* Public=nullptr;
        const FLDKillFeedView* Kills=nullptr;
        const FLDLobbyView* Lobby=nullptr;
        FGuid Self;
        uint64 OwnAirdropClaimSerial=0;
        // The own field reward history of this match (owner view); null when unknown.
        const TArray<FLDFieldRewardView>* OwnFieldRewards=nullptr;
        bool bClock=false;
        int64 NowMs=0;
        double LocalSeconds=0;
        FString LowCampName,HighCampName,EliteCampName,BossName;
    };
    struct FEventLine
    {
        FString Text;
        bool bSelf=false;   // the line is about the player (said with 你), so the feed can highlight it
        double At=0;        // local presentation time it was first shown
        uint64 AirdropClaimSerial=0;  // a drop claim line: relabelled while shown, as the owner view may arrive later
        // A camp marker claim line (its kind and segment): relabelled the same way from the own field rewards.
        ELDFieldMarkerKind MarkerKind=ELDFieldMarkerKind::None;
        uint8 MarkerSegment=0;
    };
    // Kills are told when their notice arrives, unless it was already older than StaleKillMs (a late join). Every other
    // event is a change between two observations of the same match: the first observation of a match only records the
    // state, so a screen that joins mid-match never replays what already happened. Told events: the flagpole opening, a
    // drop landing, being claimed and expiring, the Boss appearing, going down and leaving, camp markers being revealed,
    // claimed and expiring, a new 上热搜 leader, a new graffiti owner and the same owner's frozen guard dropping to zero (he
    // left the zone while a challenger was in it).
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
        // The camp markers as last observed: their segment and the High / Elite phases.
        uint8 MarkerSegment=0;
        ELDFieldMarkerPhase MarkerPhases[2]={ELDFieldMarkerPhase::None,ELDFieldMarkerPhase::None};
        FGuid Leader;
        FGuid GraffitiOwner;
        // The guard as last observed: frozen progress, or a running guard (GuardDueAtMs set).
        int64 GuardKeptMs=0;
        int64 GuardDueAtMs=0;
    };
}
