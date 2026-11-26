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
class FLDDemoContentSnapshot;
struct FLDDemoContentDefinitions;
struct FLDDeathRecapView;
struct FLDDamageSummaryView;
struct FLDReactionAssistView;
struct FLDDemoStatBonus;
struct FLDDemoEffectDefinition;
struct FLDDemoTargetDefinition;
struct FLDDemoAbilityValues;
struct FLDDemoStatDefinition;
struct FLDLobbyView;
struct FLDPKParameters;
namespace LDHUD
{
    // How often the owning controller samples the HUD read model; a slower frame rate samples once per frame. A sample
    // rebuilds only when FReadModelGate says so.
    inline constexpr double ReadModelHz=30.;
    // While nothing on the shown HUD moves, its clock texts (whole seconds) are sampled this often, so a second ticks over
    // at most 1/ReadModelClockHz late; tenths and moving bars mark the build live and keep the full rate.
    inline constexpr double ReadModelClockHz=10.;
    // What a read model build was made from (ULDHUD::SampleReadModel). Views counts changes of the compared projections
    // (owner, combat, public match, lobby, kill feed, result, death recap, body, content), Local the HUD's own answers and
    // notices; Frame names the inspected unit and its observed frame; Interaction and Pointer are Slate's last user input
    // and the cursor while it is over the HUD; Flags packs the local menu, panel, key, armed-entry and slot-order state;
    // Clock is ReadModelClock's bucket.
    struct FReadModelKey
    {
        uint64 Views=0;
        uint64 Local=0;
        uint32 CombatReceipts=0;
        uint32 Feedback=0;
        uint64 Frame=0;
        double Interaction=0;
        FVector2D Pointer=FVector2D::ZeroVector;
        FVector2D Viewport=FVector2D::ZeroVector;
        float TextScale=1.f;
        uint64 Flags=0;
        int64 Clock=-1;
        // Every input but the clock bucket.
        LOCKDOWN_API bool SameInputs(const FReadModelKey& Other) const;
    };
    // The clock bucket of a sample: real seconds in steps of 1/ReadModelClockHz while timed text shows, otherwise -1.
    LOCKDOWN_API int64 ReadModelClock(double RealSeconds,bool bTimed);
    // A countdown ring over a step this long or longer moves under half a pixel per clock bucket (the full workshop's
    // 23-unit step ring), so the clock bucket draws it as smoothly as every sample does.
    inline constexpr int64 ReadModelSmoothRingMs=30000;
    // Whether a ring counting down StartedAtMs..EndsAtMs needs every sample: a known step shorter than ReadModelSmoothRingMs.
    LOCKDOWN_API bool ReadModelRingMoves(int64 StartedAtMs,int64 EndsAtMs);
    // Rebuild on the first sample, on every sample after a live build (something on screen moves with time), when the key
    // changed, and once more after an input change, because layout reads last frame's geometry of what that build set.
    class LOCKDOWN_API FReadModelGate
    {
    public:
        bool ShouldRebuild(const FReadModelKey& Key) const;
        void Rebuilt(const FReadModelKey& Key,bool bLive);
    private:
        FReadModelKey Last;
        bool bHas=false;
        bool bLive=false;
        bool bSettle=false;
    };
    // #147/#198 card text: the owner's level grows an active's fixed amounts (G(L)); its stats fill the coefficients.
    struct FCardStats
    {
        uint8 Level=1;
        int64 ADP2=0;
        int64 BonusADP2=0;
        int64 APP2=0;
        int64 MaxHPP2=0;
        // Max HP above the character's own base and level growth: what damage coefficients read.
        int64 BonusMaxHPP2=0;
        int64 ArmorP2=0;
        int64 MagicResistP2=0;
        // A blueprint records a level but not the final projected attributes; show their coefficients instead.
        bool bShowScalingFormula=false;
        // A learned skill's current card damage factor (10000 = none), applied to its damage numbers.
        int64 DamageFactorBp=10000;
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
    // Renders an active's card template (LDDemoCardText) against its value set grown for Stats.Level at Rank (G(L) and the
    // rank scales). The glossary keywords (LDTerms::CardKeywords) are wrapped in <b>…</> in Rich.
    LOCKDOWN_API FCardText CardText(const FString& Template,const FLDDemoAbilityValues& Values,const FCardStats& Stats,
        const FLDDemoSkillRankParameters& Ranks,int64 GrowthPerLevelBp,uint8 Rank=1);
    // Renders a stat's card template: each [per_point.<Stat>] reads that per-point bonus of Stat, which never grows.
    // A learned branch's view of the skill's whole description: each placeholder reads the branch's values, and one whose
    // effect the branch removed or replaced reads the level-1 values instead (the branch steps then say what changed).
    LOCKDOWN_API FCardText PathCardText(const FString& Template,const FLDDemoAbilityValues& PathValues,const FLDDemoAbilityValues& BaseValues,
        const FCardStats& Stats,const FLDDemoSkillRankParameters& Ranks,int64 GrowthPerLevelBp,uint8 Rank);
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
    // The approved icon of a status definition; empty when it has none.
    LOCKDOWN_API FString StatusIconId(const FLDDemoContentDefinitions* Content,uint8 StatusId);
    // A control status (stun, root, silence, disarm, knockback, taunt): a head bar shows its remaining time as a bar.
    LOCKDOWN_API bool StatusIsHardControl(uint8 StatusId);
    LOCKDOWN_API FString CreatureName(const FLDDemoContentDefinitions* Content,uint8 CreatureId);
    LOCKDOWN_API FString AbilityName(const FLDDemoContentDefinitions* Content,uint8 AbilityId);
    // Public roster name of a contender; empty when the roster does not list it (never invented).
    LOCKDOWN_API FString RosterName(const FLDLobbyView& Lobby,const FGuid& ContenderId);
    // The name over an observed contender-kind unit: its roster name; in a rest (bInRest) a contender id the roster does not
    // list is the own personal training dummy (only its owner observes it), named LDTerms::TrainingDummy. Empty otherwise.
    LOCKDOWN_API FString ObservedContenderName(const FLDLobbyView& Lobby,const FGuid& ContenderId,bool bInRest);
    // The eight roster-slot identity tints shared with the bodies (ULDHumanoidVisualComponent::ContenderTint), so
    // portraits match them.
    LOCKDOWN_API FLinearColor PaletteColor(uint8 Presentation);

    struct FResourceView
    {
        bool bKnown=false;
        FString Text;
        float Fraction=0.f;
        // The rounded current and maximum as bare figures (the broadcast chrome's big number); empty while unknown.
        FString Now;
        FString Max;
    };
    // Points from the owner projection. Max<=0 means not projected yet: shown as syncing, never as 0/0.
    // Health rounds up so a living body never reads 0; momentum rounds down so a cost never looks affordable early.
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
        bool bWaiting=false;
        bool bProgressKnown=false;
        float Progress=0.f;
        FString Title;
        FString Countdown;
        FString Clock;
        FString Note;
    };
    // Death banner (APP_SPEC 7.1): waiting time only; build and property are kept, and the display never revives.
    LOCKDOWN_API FDeathView Death(const FLDOwnerState& Own,bool bClock,int64 NowMs,bool bFinalEliminated=false,
        const FLDCombatOwnerView* Combat=nullptr);
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
    LOCKDOWN_API bool TimelineDetailVisible(const FLDPublicMatchView& Public,const FTimelineView& Timeline,bool bChoicesRestOpen);
    LOCKDOWN_API FString RestReadyText(ELDRestReadyResult Result);
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
        // For the painted board: the roster seat (INDEX_NONE off the roster) and the three parts.
        int32 Seat=INDEX_NONE;
        int32 Participation=0,Winner=0,Bet=0;
    };
    // One row per PK.Awards entry with any points, in award order; names from the public roster.
    LOCKDOWN_API TArray<FPKAwardRow> PKAwardRows(const FLDPKView& PK,const FLDLobbyView& Lobby,const FGuid& Self);
    // One entrant of a side for the painted board: seat plate, name, level, whether out (eliminated or forfeited).
    struct FPKMemberView
    {
        int32 Seat=INDEX_NONE;
        FString Name;
        uint8 Level=0;
        bool bOut=false;
        bool bSelf=false;
    };
    struct FPKSideView
    {
        uint8 Side=0;
        TArray<FPKMemberView> Members;
        // Public bets on this side, and the payout per share while betting is open (0: not shown).
        int32 Bets=0;
        double Odds=0.;
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
        bool bMatchupReveal=false;
        FString Title;
        // PKReady: "N 秒后开打 …"; PK: "最长还剩 N 秒 …" (whole seconds rounded up to the step end).
        FString Countdown;
        // The same countdown as a figure with its short caption (下注 / 开打 / 剩余); -1 without one.
        int64 SecondsLeft=-1;
        FString CountLabel;
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
    // ---- P6 (spec §10-§12): daily modifiers, camp affixes and the death screen's spawn choice ----
    // The hover text of a daily modifier or a camp affix, its numbers from the approved content.
    LOCKDOWN_API FString DailyDescription(ELDDailyModifier Modifier,const FLDDemoContentParameters& Parameters);
    LOCKDOWN_API FString AffixDescription(ELDCampAffix Affix,const FLDDemoContentParameters& Parameters);
    // The own dead life's spawn candidates while its choice window is open: their count, the chosen one (INDEX_NONE: the first
    // is taken at the end) and the seconds left.
    struct FSpawnChoiceView
    {
        bool bVisible=false;
        int32 Count=0;
        int32 Chosen=INDEX_NONE;
        FString Countdown;
    };
    LOCKDOWN_API FSpawnChoiceView SpawnChoice(const FLDOwnerState& Own,bool bClock,int64 NowMs);
    // One-line answer to a refused spawn choice.
    LOCKDOWN_API FString SpawnChoiceText(ELDSpawnChoiceResult Result);
    // Whole seconds rounded up as m:ss, so a running countdown never shows 0:00 early.
    LOCKDOWN_API FString Clock(int64 RemainingMs);
    // The match timer over the minimap: whole seconds elapsed since the match start (StartedAtMs, the opening rest R0),
    // rounded down, as m:ss with no leading zero on the minutes ("0:12", "12:05"). Empty without a clock or a start.
    LOCKDOWN_API FString MatchTimer(const FLDPublicMatchView& Public,bool bClock,int64 NowMs);
    struct FCircleView
    {
        FString Line;
        // The top-centre panel's split of Line (Mock 1): Head over the bar, then the graffiti owner on the left and the
        // guard state on the right. Head carries the spray or contest part when there is one, otherwise the guard state
        // (Aside then empty); Head · Owner · Aside together say exactly what Line says.
        FString Head;
        FString Owner;
        FString Aside;
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
    // #198: a charm's map colour (heal green, momentum orange, move speed gold).
    // The event feed's line for a downed Boss: the own last hit is told it got a build reward, others see the public roster name.
    LOCKDOWN_API FString BossDownLine(const FLDBossView& Boss,const FGuid& Self,const FString& KillerName,const FString& BossName);
    // #198: the camp marker colour shared by the minimap ring and the world label.
    LOCKDOWN_API FLinearColor MarkerColor();
    // D27 (#79): the public 上热搜 line: who is named; the own player is told directly.
    // Empty when nobody is named.
    LOCKDOWN_API FString LeaderLine(const FGuid& Trending,const FGuid& Self,const FString& LeaderName);
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
    // Owner damage statistics (Combat/LDDamageRecap.h) as display text. Amounts are whole points, a positive fraction
    // rounded up. A basic reads 普攻 and the innate dash 冲刺; abilities, components and final mods read their approved
    // display name.
    LOCKDOWN_API FString DamageSourceName(const FLDDemoContentDefinitions* Content,const FLDCombatSourceRef& Source);
    // One node of a death recap row drawn as an icon chain. Attacker and Setter show Glyph on a chip with Text beside it;
    // Source and State show the imported IconId (Glyph on a plain square when it has none); Reaction, Amount, Tag and Rest
    // are short text chips. Name is the node's full name for hover text. Link says what is drawn before the node: an arrow
    // (who triggered → what it consumed → what it produced) or a gap between two entries of one row.
    enum class ERecapNode:uint8{Attacker,Source,Setter,State,Reaction,Amount,Tag,Rest};
    enum class ERecapLink:uint8{None,Arrow,Gap};
    struct FRecapNode
    {
        ERecapNode Kind=ERecapNode::Rest;
        ERecapLink Link=ERecapLink::None;
        FString IconId;
        FString Glyph;
        FString Text;
        FString Name;
        bool operator==(const FRecapNode& Other) const
        {return Kind==Other.Kind&&Link==Other.Link&&IconId==Other.IconId&&Glyph==Other.Glyph&&Text==Other.Text&&Name==Other.Name;}
    };
    struct FRecapChain
    {
        TArray<FRecapNode> Nodes;
        bool operator==(const FRecapChain& Other) const {return Nodes==Other.Nodes;}
    };
    struct FDamageShareRow
    {
        FString Name;
        FString ValueText;
        FString PercentText;
        FString IconId;
        FString Glyph;
        float Share=0.f;
        // 0: basic, 1: active skill, 2: dash, 3: card, 4: reaction/other.
        uint8 Kind=0;
    };
    struct FDamageListView
    {
        // False: no statistic was delivered for this death or this result; the caller says so instead of showing zeros.
        bool bAvailable=false;
        FString Heading;
        TArray<FString> Rows;
        // The death recap only: Rows[i] as an icon chain (same order and content).
        TArray<FRecapChain> Chains;
        // The result damage breakdown: per-source icon, amount and share bar.
        TArray<FDamageShareRow> Shares;
    };
    // Bounds that keep both lists on screen without scrolling; what does not fit is summed into one 其他 entry.
    inline constexpr int32 DeathRecapAttackersMax=4;
    inline constexpr int32 DeathRecapSourcesMax=3;
    inline constexpr int32 DamageSummarySourcesMax=8;
    // The own death banner's recap: available only for the recap of exactly the dead life shown. One row per attacker in
    // the recap's order: name, total, then its sources. Contenders read their public roster name, camp members their
    // approved display name. Reaction damage reads its rule chain, source → [setter的]state → reaction (你的 when the
    // dead player set it up himself, nothing when the attacker did). A row ends with 击杀 (who landed the fatal hit),
    // 助攻 (a credited assister, as frozen in the death fact). Chains give each row as icon nodes: the attacker and its
    // total, then per source its ReactionChainNodes and amount, then the tag.
    LOCKDOWN_API FString ReactionChainText(const FLDDemoContentDefinitions* Content,const FLDDamageLine& Line,const FGuid& Self,const FString& SetterName);
    // ReactionChainText as nodes: Source, then for reaction damage → [Setter] State → Reaction (an Overload splash that
    // consumed nothing on this body: Source → Reaction). The setter node is 你 for the dead player's own state, the
    // setter's name (未列名参赛者 when unlisted) for another contender's, and absent when unnamed or the attacker itself.
    LOCKDOWN_API void ReactionChainNodes(const FLDDemoContentDefinitions* Content,const FLDDamageLine& Line,const FGuid& Self,const FString& SetterName,TArray<FRecapNode>& Out);
    LOCKDOWN_API FDamageListView DeathRecap(const FLDDeathRecapView& Recap,const FLDOwnerState& Own,const FLDLobbyView& Lobby,const FLDDemoContentDefinitions* Content);
    // The result page's own damage: available only for the summary of exactly the result shown. One row per source.
    LOCKDOWN_API FDamageListView DamageSummary(const FLDDamageSummaryView& Summary,const FLDResultView& Result,const FLDDemoContentDefinitions* Content);
    // Spec §6.5 chains: a step kind's display name (a card or skill name, a reaction, a ground state, 击杀) and a key as one line.
    LOCKDOWN_API FString ChainStepName(const FLDDemoContentDefinitions* Content,FName Step);
    LOCKDOWN_API FString ChainKeyText(const FLDDemoContentDefinitions* Content,const TArray<FName>& Key);
    // The one-line prompt of a chain the local codex had not seen: "新连锁：A → B → C".
    LOCKDOWN_API FString NewChainNotice(const FLDDemoContentDefinitions* Content,const TArray<FName>& Key);
    // Spec §12 结算“本局最离谱”: one row per contender of the result (its standings order, at most ResultHighlightsMax): its
    // longest chain and its strongest hit on a contender (source, points, share of that target's max HP). Available only for
    // the summary of exactly the result shown.
    inline constexpr int32 ResultHighlightsMax=LDMatchLifecycle::MaxContenders;
    struct FResultHighlightRow
    {
        FString Name;
        FString Chain;
        FString Best;
        bool bOwn=false;
    };
    struct FResultHighlightsView
    {
        bool bAvailable=false;
        TArray<FResultHighlightRow> Rows;
    };
    LOCKDOWN_API FResultHighlightsView ResultHighlights(const FLDDamageSummaryView& Summary,const FLDResultView& Result,const FGuid& Self,
        const FLDDemoContentDefinitions* Content);
    // The prompt said to a reaction's setter: "你的潮湿被老王引爆（导电）". It reports the detonation only; assist credit
    // still comes from the setter's own contribution within REWARD.AssistWindowSec, as for every assist.
    LOCKDOWN_API FString ReactionAssistText(const FLDDemoContentDefinitions* Content,const FLDReactionAssistView& View,const FString& TriggerName);
    struct FScoreLine
    {
        FGuid ContenderId;
        FString Name;
        uint64 Kills=0;
        uint64 Deaths=0;
        uint64 Assists=0;
        bool bSelf=false;
    };
    // The hold-Tab scoreboard of that match: most kills first, then fewest deaths, then roster order. Names come from the
    // room roster; a contender no longer in it is shown as having left. Empty for another match's feed.
    LOCKDOWN_API TArray<FScoreLine> Scoreboard(const FLDKillFeedView& Feed,uint32 Generation,const FLDLobbyView& Lobby,const FGuid& Self);
    // The contender's public final mod name in the final field (empty without one).
    LOCKDOWN_API FString PublicFinalMod(const FLDPublicMatchView& Public,const FGuid& Contender,const FLDDemoContentDefinitions* Content);

    // The band's four slots: the Q/W/E actives and the D dash.
    enum class EActionSlot : uint8 { Q, W, E, Dash };
    enum EActionBlock : uint32
    {
        ActionUnsynced=1<<0, ActionEmpty=1<<1, ActionCooldown=1<<3, ActionMomentum=1<<4,
        ActionControlled=1<<5, ActionDead=1<<6, ActionNotPlaying=1<<7, ActionServer=1<<8
    };
    struct FActionInput
    {
        EActionSlot Slot=EActionSlot::Q;
        bool bProjected=false;    // a current owner combat projection exists for this match and life
        bool bHasSource=false;    // an active is bound
        FString Name;             // display name of the bound definition
        FName DefinitionId;       // the bound definition, for the hover description
        ELDSkillBranch Branch=ELDSkillBranch::None; // its learned path, so the description shows its current rank
        int64 DamageFactorBp=10000;  // its card damage factors now (owner view)
        int64 NextCooldownMs=0;      // the cooldown its next release would issue after haste and cards; 0 when unknown
        FString IconId;           // the bound definition's icon (LDUIIcons); the dash's own icon for D
        int64 MomentumCostP2=0;
        int64 ReadyAtMs=0;        // this source's own track end on the authority clock
        int64 CooldownMs=0;       // duration last issued on this track, for the sweep only
        int32 RangeCm=0;          // the active's current reach (the dash distance for D)
        bool bControlled=false;   // an own control status forbids this entry kind
        FString ControlLabel;     // short specific control badge when controlled (e.g. "定身", "沉默", "眩晕")
        ELDCombatReject ServerReject=ELDCombatReject::None; // structured server unavailability reason
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
        bool bMomentumKnown=false;
        int64 MomentumP2=0;
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
        // Intent submission and execution limits are separate (MOVEMENT_INPUT_AND_CAMERA input ownership): blocks grey the slot and explain why, but an
        // explicit activation is still sent so the server decides, including protection removal and EmptySource.
        bool bCanSubmitIntent=false;
        uint32 ExecutionBlocks=0;
        int64 CooldownRemainingMs=0;
        float CooldownFraction=0.f;
        FString Countdown;        // CooldownText of the remainder; empty when not cooling
        FString Cost;             // the active's momentum cost rounded like the shown bar, number only, e.g. "30"; empty without one
        FString Gap;              // momentum short: the missing momentum, e.g. "-12"; empty otherwise
        FString LockBadge;        // short slot badge when controlled or phase-locked (e.g. "定身", "沉默", "眩晕", "禁"); empty otherwise
        FString IconId;           // the icon texture id (LDUIIcons); empty without a source
        FString Glyph;            // shown only when IconId has no texture: the name's first character; empty without a source
    };
    LOCKDOWN_API FString ControlStatusBadge(TConstArrayView<FLDCombatStatusView> Statuses,uint8 ControlMask);
    LOCKDOWN_API FActionView Action(const FActionInput& Input,const FActionContext& Context);
    // Skill ranks (issue #287 spec §12): the rank dots of a slot ("●●○") and the branch cards of its next rank.
    LOCKDOWN_API FString RankDots(uint8 Rank);
    // The branch paths the next rank offers: A/B from rank 1, A1/A2 under A, B1/B2 under B, none at rank 3.
    LOCKDOWN_API TArray<ELDSkillBranch> NextBranches(ELDSkillBranch Current);
    struct FBranchCardView
    {
        bool bKnown=false;
        ELDSkillBranch Path=ELDSkillBranch::None;
        FString Name;      // the step's display name
        FString Line;      // its one-line change with the owner's numbers
        FString Detail;    // the whole active card at that branch and rank (hover)
        FString IconId;    // the active's icon
    };
    LOCKDOWN_API FBranchCardView BranchCard(const FLDDemoContentDefinitions* Content,FName AbilityId,ELDSkillBranch Path,
        const FCardStats& Stats);
    // Slot look: the slot face says its state by colour alone (the tooltip keeps the words). A dead, unsynced or empty
    // slot outranks arming; an armed entry outranks its remaining limits, then control, cooldown, momentum and server reasons.
    enum class EActionLook : uint8 { Ready, Armed, Cooling, MomentumShort, Controlled, Empty, Blocked };
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
        // Title's parts for the broadcast target card: the roster or creature name and a contender's public level (0 for none).
        FString Name;
        int32 Level=0;
        FResourceView Health;
        bool bMomentum=false;
        bool bMomentumFull=false;   // a full public bar: the target may overdrive
        bool bCharging=false;       // the target holds an overdrive charge
        FResourceView Momentum;
        FString Statuses;
        // The same public statuses as icon squares: the picture (or the name's first character), green when it helps the
        // bearer and red when it hurts, stacks over 1 in the corner; the name, stacks and remaining time are the tooltip.
        struct FStatusChip
        {
            FString IconId;
            FString Glyph;
            FString Tip;
            bool bPositive=true;
            uint8 Stacks=1;
        };
        TArray<FStatusChip> StatusChips;
        // A visible contender's Q/W/E actives (#112 §10, #198 single level): key, display name and icon; never cooldowns.
        struct FActiveChip
        {
            FString Key;
            FString Name;
            FString IconId;
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
    // The momentum cost and cooldown line of an active's value set.
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
    // Branch is the learned path (None = rank 1, the face of an offer); the card reads that path's values at its rank.
    LOCKDOWN_API FActiveCardView ActiveCard(FName DefinitionId,const FLDDemoContentDefinitions* Content,const FCardStats& Stats,
        ELDSkillBranch Branch=ELDSkillBranch::None,int64 CooldownMs=0);
    struct FFinalModCardView
    {
        FString Name,IconId,Glyph;
        FActionDetailView Detail;
        bool bKnown=false;
    };
    LOCKDOWN_API FFinalModCardView FinalModCard(FName DefinitionId,const FLDDemoContentDefinitions* Content,const FCardStats& Stats);
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
    // Success feedback needs a complete, applied allocation for this match and life.
    LOCKDOWN_API bool AppliedStatFeedback(const FLDBuildReceipt& Receipt,uint32 Match,uint32 Life,uint64 Request,int32 StatCount);
    // A final-mod stamp needs the matching applied receipt and a committed build revision.
    LOCKDOWN_API bool AppliedFinalFeedback(const FLDBuildReceipt& Receipt,uint32 Match,uint32 Life,uint64 Request,FName Definition);
    LOCKDOWN_API bool FinalStampReady(const FLDProgressionView& View,uint64 Revision,const FLDFinalModSelection& Selection,bool bBoardVisible);
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
    // Live before->after secondary stat preview, 18-point keystone progress/effect and refund loss warning (#331).
    struct FStatHoverPreview
    {
        FString DeltaSummary;
        TArray<FString> AttributeLines;
        FString KeystoneProgress;
        FString KeystoneRule;
        FString RefundWarning;
        bool bKeystoneActive=false;
        bool bKeystoneLossWarning=false;
    };
    LOCKDOWN_API FStatHoverPreview StatHoverPreview(const FLDDemoStatDefinition& Stat,int32 CurrentPoints,int32 AddStep,int32 RefundStep,
        bool bCanRespec,const TArray<FLDCombatAttributeRowView>& Attributes,const FLDDemoContentDefinitions& Content,
        const FLDDemoContentSnapshot* ContentSnapshot=nullptr,const FLDProgressionView* Progression=nullptr,
        const FLDCombatOwnerView* Combat=nullptr);
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
        // Spec §3.13: the rest shop (buy, refresh, sell) is open in the rest build window.
        bool bCanShop=false;
        FString ShopBlocked;
        bool bCanRespec=false;
        FString RespecBlocked;
        bool bCanReplace=false;
        FString ReplaceBlocked;
        // Phase 9: the final mod three-choice.
        bool bCanFinalMod=false;
        FString FinalModBlocked;
        bool bCanForget=false;
        FString ForgetBlocked;
        // A card offer's pick, skip, reroll and lock (like the active choice: field or rest, alive or dead).
        bool bCanCardOffer=false;
        FString CardOfferBlocked;
        // The stage is a rest (open or locked): the panel shows the rest services, enabled or with their reasons.
        bool bInRest=false;
        // On the field, resting in a dorm out of combat: a blueprint may replace a learned skill here.
        bool bDormSwap=false;
        // Installing, moving and uninstalling a card, as growth gates them (LDProgression::InstallGate).
        bool bCanInstall=false;
        FString InstallBlocked;
    };
    LOCKDOWN_API FBuildGateView BuildGate(LDProgression::EMatchStage Stage,ELDLifePhase LifePhase,bool bDormRested=false);
    // The player-facing why of a refused or stored build step.
    LOCKDOWN_API FString BuildBlockedText(ELDGrowthReason Reason);
    // The result line of an applied build receipt: 已学到 Q：名称, 已存为图纸：名称（原因）, the points added and left, a rest
    // service's result (the NPC points paid, the respec refund, the replaced or forgotten active), or a card taken (installed
    // or kept with the reason), installed, removed, skipped or rerolled. Keys names a learned or replaced slot.
    LOCKDOWN_API FString BuildReceiptText(const FLDBuildReceipt& Receipt,const FLDDemoContentDefinitions* Content,const FSlotKeys& Keys=FSlotKeys());
    // 火 / 水 / 电.
    LOCKDOWN_API FString ElementName(ELDDemoElement Element);
    // A card's UI key: its definition id numbered with its serial (FName number), so copies of one definition stay apart
    // while every widget keys on an FName. CardDefinition and CardSerial read a key back.
    LOCKDOWN_API FName CardKey(FName Definition,uint16 Serial);
    LOCKDOWN_API FName CardDefinition(FName Key);
    LOCKDOWN_API uint16 CardSerial(FName Key);
    LOCKDOWN_API const FLDCardInstanceView* FindCard(const FLDProgressionView& Progression,FName Key);
    LOCKDOWN_API const FLDDemoCardDefinition* CardDefinitionOf(const FLDDemoContentDefinitions* Content,FName KeyOrId);
    // How many copies of Definition the own view owns.
    LOCKDOWN_API int32 OwnedCopies(const FLDProgressionView& Progression,FName Definition);
    // A card's face: icon, name, its form and type (the frame's shape) and its display sentence with the owner's level grown
    // numbers (like an active's).
    struct FCardFaceView
    {
        FName DefinitionId;
        FString Name;
        FString IconId;
        FString Glyph;
        FString Sentence;
        FString SentencePlain;
        TArray<FString> Keywords;
        // The popup's category line: "普攻 · 触发".
        FString KindLine;
        ELDCardForm Form=ELDCardForm::Body;
        ELDCardType Type=ELDCardType::Trigger;
        // Installing it asks for an element first (Y25).
        bool bChooseElement=false;
        // False when the catalog has no such card or display row.
        bool bKnown=false;
    };
    LOCKDOWN_API FCardFaceView CardFace(FName DefinitionId,const FLDDemoContentDefinitions* Content,const FCardStats& Stats);

    // The card storage (APP_SPEC §7): the owned cards not installed, and the equipped columns Q, W and E by server slot,
    // 冲刺(D) and 本体 (which carries the basic attack); each installed card sits in the column of its target, an element medal
    // in the column of the skill or body whose element slot holds it. Copies of one card in one place show as one face with a
    // ×n badge. Local presentation over the own progression view only; the server decides every command.
    enum class EStorageColumn : uint8 { Body=0, SlotQ=1, SlotW=2, SlotE=3, Dash=4 };
    constexpr int32 StorageColumnCount=5;
    // The board's left-to-right order: Q, W, E, 冲刺, 本体.
    LOCKDOWN_API EStorageColumn BoardColumn(int32 Position);
    LOCKDOWN_API int32 BoardPosition(EStorageColumn Column);
    // The column's short label: Q, W, E, D, 本体.
    LOCKDOWN_API FString ColumnLabel(EStorageColumn Column);
    // The column of a learned active's server slot 0..2.
    LOCKDOWN_API EStorageColumn SlotColumn(uint8 Slot);
    // The column of a card form (a skill card: the learned skill's slot column).
    LOCKDOWN_API bool CardColumn(const FLDProgressionView& Progression,ELDCardTargetKind Kind,FName Active,EStorageColumn& OutColumn);
    // The column a taken final mod shows on: the body column (it binds no source).
    LOCKDOWN_API bool FinalModColumn(const FLDProgressionView& Progression,EStorageColumn& OutColumn);
    struct FEquippedView
    {
        // The first copy's key; Keys lists every copy in the place (serial order), Copies their count.
        FName Card;
        TArray<FName> Keys;
        int32 Copies=1;
        // Y25's chosen element ("火") or medal tier ("Ⅰ/Ⅱ/Ⅲ"); empty otherwise.
        FString Note;
        uint8 MedalTier=0;
    };
    struct FStorageColumnView
    {
        EStorageColumn Column=EStorageColumn::Body;
        // 普攻, 本体, 冲刺, or 空技能槽 for an empty slot; empty for a held slot, whose place is its Active's name.
        FString Title;
        // A slot column: its learned active; None for an empty slot (which takes nothing), the basic, the body and the dash.
        FName Active;
        TArray<FEquippedView> Entries;
        // The element slot (a learned skill's or the body's): whether the column has one, and the medal in it.
        bool bMedalSlot=false;
        bool bHasMedal=false;
        FEquippedView Medal;
    };
    struct FStoredView
    {
        // The first copy's key and every copy's.
        FName Card;
        TArray<FName> Keys;
        int32 Copies=1;
        // Received since the player last looked (the stack's badge).
        bool bFresh=false;
    };
    struct FStorageView
    {
        // Owned cards not installed, one entry per definition, in serial order of their first copy.
        TArray<FStoredView> Stored;
        // Of those, the entries never looked at yet.
        int32 Fresh=0;
        // Installed cards and the level's capacity (LDProgression::CardCapacity); every copy takes one.
        int32 Installed=0;
        int32 Capacity=0;
        // StorageColumnCount columns in EStorageColumn order.
        TArray<FStorageColumnView> Columns;
    };
    LOCKDOWN_API FStorageView CardStorage(const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content,
        const TSet<FName>* Seen=nullptr);
    // One place a card may be dropped now: a column with its install target. Y25 still asks for one of Elements. A replace
    // drop lands on the tile of the installed card Replacing (Host), which goes back to the library in the same commit.
    struct FCardDrop
    {
        EStorageColumn Column=EStorageColumn::Body;
        FName Host;
        ELDCardTargetKind Kind=ELDCardTargetKind::None;
        FName TargetId;
        TArray<ELDDemoElement> Elements;
        FName Replacing;
    };
    // Every place the card Key may be dropped now (its form's columns: the basic, each learned skill, the dash or the body),
    // judged by growth's own target rule (LDProgression::CardTargetReason); none past the capacity for a stored card (a move
    // frees its own slot). Empty for an unknown card. A hint only.
    LOCKDOWN_API TArray<FCardDrop> CardDrops(FName Key,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    // The atomic replace drops of a stored card on a full workshop (the server's InstallCard with ReplacingSerial): one on the
    // tile of each installed card in a column the card takes.
    LOCKDOWN_API TArray<FCardDrop> ReplaceDrops(FName Key,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    // Atomic replace drops in a specific column of a full workshop.
    LOCKDOWN_API TArray<FCardDrop> ColumnReplaceDrops(FName Key,EStorageColumn Column,const FLDProgressionView& Progression,
        const FLDDemoContentDefinitions* Content);
    // The learned skills and owned cards sharing a vocabulary key with the card (spec §6.3), for the popup's 联动 line and
    // the workshop's link lines.
    struct FCardConnections
    {
        TArray<FName> Actives,Cards;
    };
    LOCKDOWN_API FCardConnections CardConnections(FName Key,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    // The drop of Drops a pointer over Column (and over the installed Host there, or None) selects: the host's own drop when
    // it takes one, else the column's own; null when neither does.
    LOCKDOWN_API const FCardDrop* FindDrop(const TArray<FCardDrop>& Drops,EStorageColumn Column,FName Host);
    // The hover line of a stored card: the columns its Drops reach ("可装：普攻、Q"), or that it has no place now.
    LOCKDOWN_API FString DropColumnsText(const TArray<FCardDrop>& Drops);
    // Whether every capacity slot holds an installed card.
    LOCKDOWN_API bool WorkshopFull(const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);
    // The where line of a stored card's popup: the columns it reaches, or that the workshop is full.
    LOCKDOWN_API FString StoredPlaceText(FName Key,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content);

    // Match a consumed opportunity to the applied receipt of the exact request and life that submitted it.
    struct FOfferMotionTicket
    {
        uint32 Match=0,Life=0;
        uint64 Request=0;
        FGuid Offer;
        FName Option;
        ELDBuildCommandKind Kind=ELDBuildCommandKind::ActiveChoice;
        LOCKDOWN_API bool Matches(const FLDBuildReceipt& Receipt,uint32 CurrentMatch,uint32 CurrentLife) const;
    };
    // Field reward notices: the toast line of one field reward ("天降外卖：+200 流量，获得图纸「X」"), naming the event, its XP
    // and what its build part gave (a blueprint, a card offer or a held blueprint).
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
    // Tells each element medal that drops once ("获得【…】"): the first valid view of a match primes, later views name the
    // medals whose serial is past the highest one seen. Local presentation only.
    class LOCKDOWN_API FMedalDropWatch
    {
    public:
        TArray<FString> Observe(const FLDProgressionView* Progression,uint32 Match,const FLDDemoContentDefinitions* Content);
    private:
        uint32 SeenMatch=0;
        uint16 HighestSerial=0;
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
    // HUD shows nowhere else. The health and momentum bars already carry their maxima and the health regeneration.
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
    // The one line painted where the own status icons sit over the own overhead bars: "状态同步中" while the own projection
    // is missing and the band is shown (bShown false while the workshop hides it), empty otherwise.
    LOCKDOWN_API FString SelfIconsNotice(const FSelfIconsView& View,bool bShown);
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
    // separate facts (VISION_AND_UI feedback); a cancel or a non-final phase says nothing. Empty when there is nothing to tell.
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
    // first seen in a valid view of the current match, and once when the opening card offer first shows. Later offers, new
    // points and new cards never open it (they only light the 开盲盒 button's count), so it does not
    // cover a fight. The caller still never opens it over the player's own panel or captured input. Local presentation only.
    class LOCKDOWN_API FGrowthAutoOpen
    {
    public:
        // True once per match for the first view that shows the opening offer, and once for the first showing the opening
        // card offer.
        bool Observe(const FLDProgressionView& View,uint32 Match);
    private:
        uint32 SeenMatch=0;
        bool bOpeningShown=false;
        bool bOpeningCardsShown=false;
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
        // The own reaction assist prompts (owner channel, newest last, client-local serials); null when unknown. Only those of
        // OwnLife (the own current life in this match; 0 when unknown) are told.
        const TArray<FLDReactionAssistView>* OwnReactionAssists=nullptr;
        uint32 OwnLife=0;
        const FLDDemoContentDefinitions* Content=nullptr;
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
        // A public death notice, drawn as the lower third killer plate · cause · victim plate: the seats (SeatOf, -1 for
        // none), the public roster names and, for a death no contender owned, the camp or creature that caused it
        // (KillerLabel). The notice carries no skill, so the middle cell names the cause kind only.
        bool bKill=false;
        ELDKillCause Cause=ELDKillCause::Unowned;
        int32 KillerSeat=INDEX_NONE;
        int32 VictimSeat=INDEX_NONE;
        FString KillerName;
        FString VictimName;
        int32 Assists=0;
    };
    // Kills are told when their notice arrives, unless it was already older than StaleKillMs (a late join). Every other
    // event is a change between two observations of the same match: the first observation of a match only records the
    // state, so a screen that joins mid-match never replays what already happened. Told events: the flagpole opening, a
    // drop landing, being claimed and expiring, the Boss appearing, going down and leaving, camp markers being revealed,
    // claimed and expiring, a new 上热搜 leader, a new graffiti owner and the same owner's frozen guard dropping to zero (he
    // left the zone while a challenger was in it). An own reaction prompt of this match and the own current life is told
    // like a kill, also on the first observation of a match and when it was kept before the feed's first observation;
    // stale ones (older than StaleKillMs) and those of an earlier match or life are skipped, and each is told at most once
    // (the local serial never resets). A prompt of a match or life the client does not know yet, or any prompt while the
    // clock is unknown, waits with the later ones until they catch up. It is also the observation's AssistPrompt, for a
    // one-off toast.
    class LOCKDOWN_API FEventFeed
    {
    public:
        static constexpr double LineSeconds=5.;
        static constexpr int32 MaxLines=6;
        static constexpr int64 StaleKillMs=6000;
        void Observe(const FEventFeedInput& Input);
        const TArray<FEventLine>& Lines() const {return Shown;}
        // The newest reaction assist told by the last Observe; empty when none was.
        const FString& AssistPrompt() const {return Prompt;}
    private:
        TArray<FEventLine> Shown;
        FString Prompt;
        uint64 ReactionAssistSerial=0;
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
        FGuid RunnerUp;
        FGuid GraffitiOwner;
        // The final mods' public field as last observed: the fading graffiti, the fence, whether the flagpole was away from
        // its anchor and which private flagpoles carried graffiti.
        int64 FadeAtMs=0;
        int64 CageUntilMs=0;
        bool bPoleAway=false;
        TArray<FGuid> PrivateSprayed;
        // The guard as last observed: frozen progress, or a running guard (GuardDueAtMs set).
        int64 GuardKeptMs=0;
        int64 GuardDueAtMs=0;
    };

    // ---- On-air broadcast chrome (APP_SPEC §7) ----
    // A contender's public seat: his start roster position in the lobby (the body tint key, LDBroadcast::SeatInk), shown as
    // his jersey number (seat + 1) on every plate. INDEX_NONE when the roster does not list him.
    LOCKDOWN_API int32 SeatOf(const FLDLobbyView& Lobby,const FGuid& ContenderId);
    // The camp, creature or Boss that caused a death no contender owned (the same names as KillLine); empty for a contender
    // kill or an unowned death.
    LOCKDOWN_API FString KillerLabel(const FLDKillNotice& Notice,const FString& LowCampName,const FString& HighCampName,
        const FString& EliteCampName,const FString& BossName);
    // The element a learned active applies (its branch's element, else the ability's; ELDCombatElement order), 0 for none or
    // unknown content. The chip on the slot shows it; medals that change the overdrive element stay in the tooltip.
    LOCKDOWN_API uint8 SkillElement(const FLDDemoContentDefinitions* Content,FName DefinitionId,ELDSkillBranch Branch);
    // The one-character element name (烫 湿 麻 冻 馊), empty for none.
    LOCKDOWN_API FString ElementGlyph(uint8 Element);
    // The match as eight beats: R0, field 1, R1, field 2, R2, field 3, R3, final field.
    inline constexpr int32 ScoreBugBeats=8;
    struct FScoreBugView
    {
        bool bKnown=false;
        // The current beat (0..7) and whether it is a rest.
        int32 Beat=0;
        bool bRest=false;
        bool bFinal=false;
        // "野外 2/3", "休息 R1", "终局" or "最终休息".
        FString Label;
        // The remaining time of the current beat ("1:37"); empty without a clock or when the beat has no end yet.
        FString Clock;
        // The clock is in its last ten seconds, or paused.
        bool bUrgent=false;
        bool bPaused=false;
    };
    LOCKDOWN_API FScoreBugView ScoreBug(const FLDPublicMatchView& Public,bool bClock,int64 NowMs);
    // One seat plate of the score bug: only public facts (the roster, the graffiti owner, the 上热搜 leader and the public
    // death notices of the feed window).
    struct FSeatPlateView
    {
        int32 Seat=INDEX_NONE;
        FGuid ContenderId;
        FString Name;
        bool bSelf=false;
        bool bGraffiti=false;
        bool bLeader=false;
        // A public death notice of this seat arrived within FEventFeed::LineSeconds (an event, never a claim of "dead").
        bool bRecentKO=false;
    };
    LOCKDOWN_API TArray<FSeatPlateView> SeatPlates(const FLDLobbyView& Lobby,const FLDPublicMatchView& Public,const FLDKillFeedView& Kills,
        const FGuid& Self,bool bClock,int64 NowMs);

    // #328 §2: separate bottom-right micro-badge counts for Active Skills, Build Cards and Element Stones (medal_*).
    struct FPendingCategoryCounts
    {
        int32 SkillCount=0;
        int32 CardCount=0;
        int32 StoneCount=0;
    };
    LOCKDOWN_API FPendingCategoryCounts PendingCategoryCounts(const FLDProgressionView* Progression,uint32 Match,const FLDDemoContentDefinitions* Content);

    // #328 §3: live skill tooltip incorporating Rank+Branch, real-time AD/AP/MaxHP/Armor/CooldownHaste, installed build
    // cards (SkillDamageFactor, IdleReadyAmp, SkillGrowth, SkillAdRider, CooldownCost), 18-point keystones, and Alt/C breakdown.
    struct FLiveSkillTooltip
    {
        FActionDetailView Detail;
        // 0: neutral, 1: physical (#FF9F43), 2: magical (#38BDF8), 3: true (#FFFFFF), 4: shield/heal (#4ADE80).
        uint8 PrimaryTone=0;
        int64 FinalPrimaryDamageP2=0;
        int64 RiderDamageP2=0;
        int64 FinalCooldownMs=0;
        int64 FinalMomentumCostP2=0;
        uint8 OverdriveElement=0;
        FString OverdriveLine;
        bool bHasFormula=false;
        FString FooterHint;
        TArray<FString> FormulaLines;
    };
    LOCKDOWN_API FLiveSkillTooltip EvaluateLiveSkillTooltip(const FActionView& View,const FActionInput& Input,
        const FActionContext& Context,const FLDProgressionView* Build,const FLDCombatOwnerView* OwnCombat,
        const FLDDemoContentDefinitions* Content,bool bExpandFormula,int32 SkillHitStacks=0);

    // #328 §4.4: 6-player hold-Tab tactical scoreboard row (authorized public + sighted-in-vision facts only).
    struct FTacticalSighting
    {
        uint8 Presentation=0;
        uint8 LastSeenLevel=0;
        uint8 RevealedActives[3]={0,0,0};
        int64 KnownGuardMs=0;
    };
    struct FTacticalScoreRow
    {
        FGuid ContenderId;
        uint8 Presentation=0;
        FString Name;
        bool bSelf=false;
        bool bLeaderCrown=false;
        FString LevelText;
        float GraffitiFraction=0.f;
        FString GraffitiText;
        uint64 Kills=0,Deaths=0,Assists=0;
        FString ActiveIcons[3];
        FString ActiveNames[3];
        FString FinalModText;
    };
    LOCKDOWN_API TArray<FTacticalScoreRow> BuildTacticalScoreboard(const FLDPublicMatchView& Public,const FLDKillFeedView& Feed,
        const FLDLobbyView& Lobby,const FGuid& Self,const TMap<FGuid,FTacticalSighting>& Sightings,
        const FLDProgressionView* OwnProgression,const FLDDemoContentDefinitions* Content,bool bClock,int64 NowMs);
}
