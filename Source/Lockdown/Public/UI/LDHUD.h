#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchTimeline.h"
#include "Audio/LDAudioCueTracker.h"
#include "Combat/LDDamageRecap.h"
#include "UI/LDWorldFeedback.h"
#include "UI/LDHUDModel.h"
#include "UI/LDUIPaint.h"
#include "PK/LDPKRules.h"
#include "Framework/LDPacingTypes.h"
#include "LDHUD.generated.h"
class UBorder;
class UHorizontalBox;
class UCanvasPanelSlot;
class UTextBlock;
class UImage;
class UProgressBar;
class USizeBox;
class ULDCommandButton;
class ULDMinimap;
class ULDPanel;

class ULDChoicesPanel;
class ULDComponentStorage;
class ULDStatBar;
class ULDWorkshop;
class ULDBuildRingGround;
class UVerticalBox;
class ULDDevTestPanel;
struct FLDDevTestResult;
class FLDDemoContentSnapshot;
struct FLDDemoContentDefinitions;
class UTexture2D;

// One own status as painted over the own overhead bars (ULDWorldLabels): its tone rim, glyph (shown without an imported
// picture), stacks over 1, and the remaining fraction of a timed status (-1 for none). ViewIndex is its LDHUD::SelfIcons entry.
struct FLDOwnStatusMark
{
    FString Glyph;
    bool bPositive=true;
    uint8 Stacks=1;
    float Remaining=-1.f;
    int32 ViewIndex=INDEX_NONE;
};

// The white damage trail behind a falling bar (local real time): it holds a moment, then falls to the bar; a rise snaps.
struct FLDHudTrail
{
    float Trail=-1.f;
    float Shown=-1.f;
    double HoldUntil=0.;
    double LastAt=0.;
    float Advance(float Fraction,double Now)
    {
        constexpr double Hold=.35,FallPerSecond=1.2;
        if(Trail<0.f||Fraction>=Trail){Trail=Fraction;Shown=Fraction;LastAt=Now;return Trail;}
        // A new loss restarts the hold, so a combo reads as one growing white run.
        if(Fraction<Shown-.0005f){HoldUntil=Now+Hold;}
        Shown=Fraction;
        if(Now>HoldUntil){Trail=FMath::Max(Fraction,Trail-float((Now-FMath::Max(LastAt,HoldUntil))*FallPerSecond));}
        LastAt=Now;
        return Trail;
    }
    void Reset(){*this=FLDHudTrail();}
};

// One Q/W/E/D entry: a world button that sends the same activation as its hotkey. It is enabled only while an intent can
// be submitted; execution limits colour it but never replace the server's decision. The face shows only the key,
// the icon and the momentum cost; the state is the frame and icon colour, and the tooltip keeps the words.
USTRUCT()
struct FLDHUDActionSlot
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<USizeBox> Box;
    UPROPERTY() TObjectPtr<ULDCommandButton> Button;
    UPROPERTY() TObjectPtr<UBorder> Frame;
    // The bound entry's icon (LDUIIcons) on a rounded square in the entry kind's colour; without the texture the square
    // shows the name's first character instead.
    UPROPERTY() TObjectPtr<UBorder> Icon;
    UPROPERTY() TObjectPtr<UImage> Picture;
    UPROPERTY() TObjectPtr<UTextBlock> Glyph;
    // The key band under the face: orange while the entry can act, ink otherwise, live red on a refused press.
    UPROPERTY() TObjectPtr<UBorder> KeyBand;
    UPROPERTY() TObjectPtr<UTextBlock> Key;
    // The element the bound active applies (LDHUD::SkillElement) as a square chip in its element ink on the face's corner.
    UPROPERTY() TObjectPtr<UBorder> ElementChip;
    UPROPERTY() TObjectPtr<UTextBlock> ElementGlyph;
    UPROPERTY() TObjectPtr<UBorder> CostChip;
    UPROPERTY() TObjectPtr<UTextBlock> Cost;
    UPROPERTY() TObjectPtr<UBorder> ControlChip;
    UPROPERTY() TObjectPtr<UTextBlock> ControlBadge;
    // A skill is bound: only such a slot can be dragged onto another (#97).
    bool bHasSource=false;
    // Over the icon: the cooldown mask shrinking with the remainder, its remaining time and the ready flash.
    UPROPERTY() TObjectPtr<UProgressBar> Cooldown;
    UPROPERTY() TObjectPtr<UTextBlock> Countdown;
    UPROPERTY() TObjectPtr<UBorder> Flash;
    // Skill ranks and momentum (issue #287 spec §12): the rank dots on the lower edge, the "+" on the corner while a skill
    // point can raise this skill, the missing momentum over a dimmed face and the overdrive hold filling the face.
    UPROPERTY() TObjectPtr<UTextBlock> Ranks;
    UPROPERTY() TObjectPtr<ULDCommandButton> Plus;
    UPROPERTY() TObjectPtr<UTextBlock> Gap;
    UPROPERTY() TObjectPtr<UProgressBar> Hold;
    // Local presentation only (real time): the applied look, the ready flash and the last rejected press of this slot.
    int32 AppliedLook=-1;
    FString AppliedIcon;
    int32 AppliedTint=-1;
    float AppliedOpacity=1.f;
    bool bCooling=false;
    uint32 CoolingMatch=0;
    uint32 CoolingLife=0;
    double FlashAt=-1;
    double RejectedAt=-1;
    // The armed aim of this slot was cancelled without a cast: one short red flash.
    double CancelledAt=-1;
};

UCLASS()
class LOCKDOWN_API ULDHUD : public UUserWidget
{
    GENERATED_BODY()
public:
    // The owning controller's sample: audio cues always, the read model only when LDHUD::FReadModelGate asks for it.
    void SampleReadModel();
    // Always rebuilds (probes that change what the HUD shows and read it back at once).
    void RefreshReadModel();
    // #337 §1.2: immediately suspends HUD, Minimap and world-preview sampling/painting when the world begins leaving/teardown.
    void FreezeForWorldTeardown();
    bool IsFrozenForTeardown() const { return bFrozenForTeardown; }
    bool PointerOverUI() const;
    // Called by the hit-test-invisible world layer, below the HUD's panels.
    void PaintWorldPreviews(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer) const;
    TArray<FSlateRect> WorldLabelReservedAreas(const FGeometry& PaintGeometry) const;
    // The own status marks over the own overhead bars, empty while the band is hidden. Brush(Index) is the mark's picture,
    // null without an imported icon (the glyph stands in). The world layer reports the absolute rects it painted them at.
    const TArray<FLDOwnStatusMark>& GetOwnStatusMarks() const {return OwnStatusMarks;}
    const FSlateBrush* OwnStatusBrush(int32 Index) const
    {return OwnStatusTextures.IsValidIndex(Index)&&OwnStatusTextures[Index]&&OwnStatusBrushes.IsValidIndex(Index)?&OwnStatusBrushes[Index]:nullptr;}
    void ReportOwnStatusRects(TArray<FSlateRect> AbsoluteRects) const {OwnStatusHitRects=MoveTemp(AbsoluteRects);}
    // LDHUD::SelfIconsNotice: "状态同步中" in place of the marks while the own projection is missing; painted, never hit-tested.
    const FString& GetOwnStatusNotice() const {return OwnStatusNotice;}
    UPROPERTY() TObjectPtr<ULDMinimap> Minimap;
    // The normal main panels (VISION_AND_UI navigation) with DevTestPanel below. The HUD owns them; ULDUIRoot opens one at a
    // time. ChoicesPanel is the 构筑 panel: open, the build ring is open; it holds the offer, rest service and shop commands.
    UPROPERTY() TObjectPtr<ULDChoicesPanel> ChoicesPanel;
    // #227: the card storage's install commands the build ring sends (ULDComponentStorage); not a main panel.
    UPROPERTY() TObjectPtr<ULDComponentStorage> Storage;
    // The build workshop (APP_SPEC §7 构筑): the full view of the 构筑 tray, open while the tray is open (ULDWorkshop).
    UPROPERTY() TObjectPtr<ULDWorkshop> Workshop;
    // The open build ring's ground darkening, under every HUD surface (ULDBuildRingGround).
    UPROPERTY() TObjectPtr<ULDBuildRingGround> WorkshopGround;
    // The own point-bought stats under the name in the band's identity block: hover a stat for its "+", one click commits
    // one point (ULDStatBar). The 属性 entry and the Attributes key swap it for the attribute view and back.
    UPROPERTY() TObjectPtr<ULDStatBar> StatBar;
    void ToggleAttributeView();
    // The Workshop key: the same toggle as the 构筑 button, only while that button would accept a click or the workshop is open.
    void ToggleWorkshop();
    // Back: closes the storage's element chooser (V05Back, after an armed preview).
    bool CancelStorage();
    bool CloseRankUp();
    bool IsRankUpOpen() const { return RankSlot != INDEX_NONE; }
    // Selects spawn candidate Index (0-based) and pans the death camera there (#331).
    bool SelectSpawnCandidate(int32 Index);
    // Contextual shortcuts routed from ALDPlayerController / ULDUIRoot (#331).
    bool HandleContextualKeyPress(const FKey& Key, bool bAlt, bool bCtrl, bool bShift);
    void SelectResultContender(const FGuid& ContenderId);
    FGuid GetInspectedResultContender() const { return InspectedResultContender; }
    // DEV test session only (#78): the 测试 panel; its entry button and the 测试模式 marker show only in such a room.
    UPROPERTY() TObjectPtr<ULDDevTestPanel> DevTestPanel;
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void NativeDestruct() override;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    // Sees a left press on a filled Q/W/E slot before its button, so the press may become a drag (UpdateDrag).
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
    // #97 drag to swap: a left press on a filled Q/W/E slot that moves a few units drags a ghost of it; letting go over
    // another Q/W/E slot swaps the two in the controller's local slot order.
    int32 DragFrom=INDEX_NONE;
    FVector2D DragStart=FVector2D::ZeroVector;
    bool bDragging=false;
    UPROPERTY() TObjectPtr<UBorder> DragGhost;
    UPROPERTY() TObjectPtr<UTextBlock> DragGhostGlyph;
    UPROPERTY() TObjectPtr<UImage> DragGhostPicture;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> DragGhostSlot;
    void UpdateDrag();
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override;
private:
    // #225 DEV probe reads the rendered HUD rows.
    friend class FLDDemoOpponentProbe;
    bool bFrozenForTeardown=false;
    UPROPERTY() TObjectPtr<UBorder> NetSpikePanel;
    UPROPERTY() TObjectPtr<UTextBlock> NetSpikeText;
    double NetSpikeTickAt=-1., NetSpikeBadSince=-1., NetSpikeGoodSince=-1.;
    uint32 NetSpikeCorrections=0;
    TWeakObjectPtr<class ALDCharacter> NetSpikeBody;
    bool bNetSpikeShown=false, bNetSpikeLogged=false;
    UPROPERTY() TObjectPtr<UBorder> DiagnosticsPanel;
    UPROPERTY() TObjectPtr<UTextBlock> DiagnosticsText;
    LDHUD::FFrameDiagnostics FrameDiagnostics;
    double DiagnosticsTickAt=-1, DiagnosticsSampleAt=0;
    uint32 DiagnosticsCorrections=0;
    TWeakObjectPtr<class ALDCharacter> DiagnosticsBody;
    // Top centre (on-air target card): the seat plate (a creature's tint block and first character for a camp), the name, the
    // public level and the health figure on one slab, the painted health bar with the public momentum under it.
    UPROPERTY() TObjectPtr<UBorder> TargetCard;
    UPROPERTY() TObjectPtr<UWidget> TargetPortrait;
    UPROPERTY() TObjectPtr<UTextBlock> TargetGlyph;
    UPROPERTY() TObjectPtr<UTextBlock> TargetTitle;
    UPROPERTY() TObjectPtr<UTextBlock> TargetLevelText;
    UPROPERTY() TObjectPtr<UTextBlock> TargetHealth;
    UPROPERTY() TObjectPtr<UWidget> TargetBarSpot;
    struct FTargetPaint
    {
        bool bContender=false;
        int32 Seat=INDEX_NONE;
        FLinearColor CreatureInk=FLinearColor::Gray;
        bool bHealth=false;
        float Health=0.f;
        FLinearColor HealthInk=FLinearColor::White;
        bool bMomentum=false;
        float Momentum=0.f;
        bool bMomentumFull=false;
        bool bCharging=false;
        float MomentumNotch=-1.f;
    } TargetPaint;
    // The white damage trail behind the target's bar, kept for one target.
    mutable FLDHudTrail TargetTrail;
    FGuid TargetTrailHandle;
    // The target's public statuses as icon squares (a wrap row, no text line): each tile, its picture, its fallback glyph
    // and its stack count, grown on demand and collapsed when unused.
    UPROPERTY() TObjectPtr<class UWrapBox> TargetStatusRow;
    UPROPERTY() TArray<TObjectPtr<UWidget>> TargetStatusTiles;
    UPROPERTY() TArray<TObjectPtr<UBorder>> TargetStatusRims;
    UPROPERTY() TArray<TObjectPtr<UImage>> TargetStatusIcons;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> TargetStatusGlyphs;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> TargetStatusStacks;
    // #328 §4.3: 2.5s 55%-alpha lost-vision snapshot on TargetCard, seamlessly resuming if the contender re-enters sight.
    LDHUD::FObservedCard LastTargetCard;
    FString LastTargetRelation;
    FGuid LastTargetHandle,LastTargetContenderId;
    uint8 LastTargetCreatureId=0,LastTargetPresentation=0;
    bool bLastTargetContender=false,bHasTargetSnapshot=false;
    uint32 LastTargetMatch=0;
    double LastTargetSeenAtSec=-10.;
    // The visible contender's Q/W/E icon tiles (#198: an active has one level); the name's first character shows when the
    // icon is missing, and the key and name are the tile's tooltip.
    UPROPERTY() TObjectPtr<UWidget> TargetActivesRow;
    UPROPERTY() TArray<TObjectPtr<UBorder>> TargetActiveTiles;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> TargetActiveNames;
    UPROPERTY() TArray<TObjectPtr<UImage>> TargetActiveIcons;
    UPROPERTY() TObjectPtr<UBorder> PhaseBanner;
    // The top-centre stack (the banner and the rest actions); the full workshop moves it to the top-right corner.
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> TopCentreSlot;
    // #331: the workshop auto-opens on entering R1-R3 Build (never on field death), and closes on respawn if manually
    // opened while dead, or when leaving an auto-opened full-screen Rest step.
    bool bWorkshopSeenDead=false;
    bool bWorkshopSeenRestBuild=false;
    bool bWorkshopOpenedDuringDeath=false;
    bool bWasAutoWorkshopPhase=false;
    // The open workshop was opened by its moment, not by the player: it closes again when the moment ends.
    bool bWorkshopAutoOpened=false;
    // The rest actions panel's own brush, restored when it leaves the full workshop's top bar.
    FSlateBrush RestPanelBrush;
    bool bRestPanelBrushKept=false;
    // #198: the outer timeline over the capture line (LDHUD::TimelineBanner): the rest and its step, or the field segment;
    // then the rest's PK sign-up and lock detail.
    UPROPERTY() TObjectPtr<UTextBlock> TimelineText;
    UPROPERTY() TObjectPtr<UTextBlock> TimelineDetailText;
    // Under the banner while the own contender is in a rest: "重置训练" (ALDPlayerController::SubmitTrainingReset), an owner
    // intent the server decides; the last answer in this rest shows beside it and as a toast. The PK sign-up has no button:
    // standing in the own sign-up disk is the sign-up (the banner detail and ALDSignupMarker show it).
    UPROPERTY() TObjectPtr<UBorder> RestActionsPanel;
    UPROPERTY() TObjectPtr<ULDCommandButton> TrainingResetButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> RestReadyButton;
    // The ready button's caption (inside its painted block) and whether the own contender is ready (its block turns from
    // orange to a white-ruled panel).
    UPROPERTY() TObjectPtr<UTextBlock> RestReadyCaption;
    bool bRestReadyShown=false;
    UPROPERTY() TObjectPtr<UTextBlock> RestAnswerText;
    // A reset is waiting for its answer (a Listen Host answers inside the call); its button stays disabled meanwhile.
    bool bTrainingResetInFlight=false;
    bool bRestReadyInFlight=false;
    // The last answer to a rest action and the rest it belongs to; another rest or match never shows it.
    FString RestAnswer;
    uint32 RestAnswerMatch=0;
    uint8 RestAnswerRest=0;
    // #198 站圈报名: the own sign-up disk (ALDSignupMarker) during a rest's build step, from the own body's roster slot
    // (its public PresentationKey) and the local world and content; never replicated, rebuilt per match and slot.
    TWeakObjectPtr<class ALDSignupMarker> SignupMarker;
    uint32 SignupMarkerFailedMatch=0;
    void SyncSignupMarker(const FLDPublicMatchView& Public,uint32 OwnMatch,bool bShow,bool bWindowOpen,bool bSignedUp);
    void HandleTrainingReset(ELDTrainingResetResult Result);
    void HandleRestReady(ELDRestReadyResult Result);
    void SubmitRestReady();
    void SubmitTrainingReset();
    void ShowRestAnswer(const FString& Text);
    // #198 session D: the PK panel under the rest actions (LDHUD::PKPanel), from the draw to the payout step: format and
    // sides with their 出局/离场 marks, the PKReady/PK countdown and ring, one bet button per side for an eligible bettor in
    // the betting step, the throw button for an eligible bettor while the fight runs, then the frozen result and awards.
    // Bets and throws are owner intents the server decides (ALDPlayerController::SubmitPKBet/SubmitPKThrow); their answers
    // show in the panel and as a toast. Hit-tested, so its buttons take clicks.
    UPROPERTY() TObjectPtr<UBorder> PKPanelBorder;
    // The PK board (top right): a painted head (the PK, its countdown figure, the ring), one painted row per side that
    // is also its bet button while betting is open, the own role, the throw, the painted result bar and award rows.
    // The painters read PKPaint, the view of the last refresh.
    LDHUD::FPKPanelView PKPaint;
    UPROPERTY() TObjectPtr<UWidget> PKHead;
    UPROPERTY() TObjectPtr<UTextBlock> PKRoleText;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> PKBetButtons;
    UPROPERTY() TObjectPtr<ULDCommandButton> PKThrowButton;
    UPROPERTY() TObjectPtr<UWidget> PKResultBar;
    UPROPERTY() TArray<TObjectPtr<UWidget>> PKAwardRows;
    UPROPERTY() TObjectPtr<UTextBlock> PKAnswerText;
    // #331: compact PK matchup + betting strip kept visible at the top while Workshop is open during Draw / Betting.
    UPROPERTY() TObjectPtr<UBorder> PKCompactBar;
    UPROPERTY() TObjectPtr<UTextBlock> PKCompactTitle;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> PKCompactBetButtons;

    // Local PK state, kept for one PK (match generation and serial) only: a bet or throw waiting for its answer, the armed
    // throw aim, the last throw answer's ReadyAtMs and the last answer line.
    bool bPKBetInFlight=false;
    bool bPKThrowArmed=false;
    bool bPKThrowInFlight=false;

    int64 PKThrowReadyAtMs=0;
    uint32 PKStateMatch=0;
    uint32 PKStateSerial=0;
    FString PKAnswer;
    void RefreshPK(class ALDPlayerController& PC,const FLDPublicMatchView& Public,const FLDLobbyView& Lobby,bool bClock,int64 NowMs);
    LDHUD::FPKPanelInput PKInput(const class ALDPlayerController& PC,bool bClock,int64 NowMs);
    void SubmitPKBet(int32 SideIndex);
    void TogglePKThrowAim();
    void HandlePKBet(ELDPKBetResult Result);
    void HandlePKThrow(ELDPKThrowResult Result,int64 ReadyAtMs);
    void ShowPKAnswer(const FString& Text);
    // The local arena rings (ALDPKRingMarker) during the PKReady and PK steps of the own match's PK; rebuilt per PK, never
    // replicated. The approved ring parameters come from the same content snapshot as the display labels.
    TWeakObjectPtr<class ALDPKRingMarker> PKRing;
    uint32 PKRingFailedMatch=0;
    uint32 PKRingFailedSerial=0;
    FLDPKParameters PKParams;
    const FLDDemoContentDefinitions* PKParamsSource=nullptr;
    bool bPKParamsValid=false;
    const FLDPKParameters* PKParameters();
    void SyncPKRing(const FLDPublicMatchView& Public,bool bOwnMatch,bool bClock,int64 NowMs);
public:
    // While the throw aim is armed the next world left click throws at the ground under the pointer (ThrowPKAtPointer) and a
    // right click or Back only cancels it (CancelPKThrowAim); neither issues a move, attack or inspect (LDPlayerInput.cpp).
    bool IsPKThrowArmed() const {return bPKThrowArmed;}
    void ThrowPKAtPointer();
    bool CancelPKThrowAim();
private:
    // P6 spec §10.4: the death screen's spawn choice under the rest actions, one button per candidate (the chosen one lit)
    // and the seconds left; an owner intent the server decides (ALDPlayerController::SubmitSpawnChoice).
    UPROPERTY() TObjectPtr<UBorder> SpawnChoicePanel;
    UPROPERTY() TObjectPtr<UTextBlock> SpawnChoiceCountdown;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> SpawnChoiceButtons;
    bool bSpawnChoiceInFlight=false;
    void SubmitSpawnChoice(int32 Index);
    void HandleSpawnChoice(ELDSpawnChoiceResult Result);
    // P6 spec §11.1: one glyph badge per daily modifier beside the match timer; the name and numbers are its tooltip.
    UPROPERTY() TArray<TObjectPtr<UBorder>> DailyBadges;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> DailyBadgeGlyphs;
    TArray<ELDDailyModifier> ShownDaily;
    // Local world rings, never replicated: the final showdown's street closure and the mid-field objective's circle.
    TWeakObjectPtr<class ALDShowdownRingMarker> ShowdownRing;
    TWeakObjectPtr<class ALDShowdownRingMarker> ObjectiveRing;
    uint32 RingFailedMatch=0;
    uint32 RingFailedKey=0;
    void SyncShowdownRing(const FLDPublicMatchView& Public,bool bOwnMatch,bool bClock,int64 NowMs);
    void SyncObjectiveRing(const FLDPublicMatchView& Public,bool bOwnMatch);
    // Keeps Ring built for (generation, key, centre) or destroys it; false when no ring can be shown.
    bool KeepRing(TWeakObjectPtr<class ALDShowdownRingMarker>& Ring,bool bShow,const FLDPublicMatchView& Public,uint32 Key,const FVector& Centre,const FLinearColor& Tint);
private:
    // Top-left score bug (PhaseBanner): the LIVE tag, the beat's label and clock, the day's modifiers, the eight-beat match
    // bar and the eight seat plates (LDHUD::ScoreBug / SeatPlates), then the flagpole line under it.
    UPROPERTY() TObjectPtr<UTextBlock> ScoreLabelText;
    UPROPERTY() TObjectPtr<UTextBlock> ScoreClockText;
    UPROPERTY() TObjectPtr<UWidget> ScoreBeatSpot;
    UPROPERTY() TObjectPtr<UWidget> ScorePlateSpot;
    LDHUD::FScoreBugView ShownBug;
    TArray<LDHUD::FSeatPlateView> ShownPlates;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseText;
    // Under the capture bar: the graffiti owner on the left, the guard state on the right.
    UPROPERTY() TObjectPtr<UWidget> PhaseFootRow;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseOwnerText;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseAsideText;
    UPROPERTY() TObjectPtr<UTextBlock> DevMarkerText;
    UPROPERTY() TObjectPtr<UWidget> CircleSpot;
    float CircleFraction=0.f;
    // Top-right feed (LDHUD::FEventFeed), painted as lower thirds: a kill as killer plate · cause · victim plate, every other
    // line as one slab, newest at the bottom; each wipes in and fades out on time.
    UPROPERTY() TObjectPtr<UBorder> EventFeedPanel;
    UPROPERTY() TObjectPtr<USizeBox> EventFeedSpot;
    LDHUD::FEventFeed EventFeed;
    void PaintFeed(const LDUIPaint::FContext& P) const;
    // #328 §4.4: the hold-Tab tactical scoreboard (seat plate, crown, last-seen level, guard bar, K/D/A, Q/W/E icons, final mod),
    // painted from ScoreBoard by PaintScoreboard.
    UPROPERTY() TObjectPtr<UBorder> ScorePanel;
    UPROPERTY() TObjectPtr<USizeBox> ScoreSpot;
    TArray<LDHUD::FTacticalScoreRow> ScoreBoard;
    void PaintScoreboard(const LDUIPaint::FContext& P) const;
    TMap<FGuid,LDHUD::FTacticalSighting> ScoreSightings;
    uint32 ScoreSightingsMatch=0;
    UPROPERTY() TObjectPtr<UBorder> ResultPanel;
    // The damage and highlight cards side by side under the result table.
    UPROPERTY() TObjectPtr<class UHorizontalBox> ResultCards;
    UPROPERTY() TObjectPtr<UTextBlock> ResultTitle;
    UPROPERTY() TObjectPtr<UTextBlock> ResultSubtitle;
    UPROPERTY() TObjectPtr<UTextBlock> ResultBoardTitle;
    UPROPERTY() TObjectPtr<ULDCommandButton> ResultInspectSelfButton;
    UPROPERTY() TObjectPtr<USizeBox> ResultWinnerBuild;
    UPROPERTY() TObjectPtr<class UVerticalBox> ResultRows;
    UPROPERTY() TArray<TObjectPtr<UBorder>> ResultRowCards;
    TArray<FGuid> ResultRowContenders;
    FGuid InspectedResultContender;
    // The own per-source damage of the shown result: a heading and a fixed grid of cells with icon + colored share bar (#331).
    UPROPERTY() TObjectPtr<UTextBlock> ResultDamageHeading;
    UPROPERTY() TArray<TObjectPtr<UWidget>> ResultDamageSlots;
    UPROPERTY() TArray<TObjectPtr<UImage>> ResultDamageIcons;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> ResultDamageGlyphs;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> ResultDamageCells;
    UPROPERTY() TArray<TObjectPtr<UProgressBar>> ResultDamageBars;
    uint8 ResultReportTab=0;
    void RefreshResultInspectedBuild(const struct FLDResultView& Result, const FGuid& OwnContenderId);
    // Spec §12 结算“本局最离谱”: one line per contender (LDHUD::ResultHighlights).
    UPROPERTY() TObjectPtr<UWidget> ResultHighlightsCard;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> ResultHighlightCells;
    UPROPERTY() TObjectPtr<ULDCommandButton> ReturnButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> LeaveRoomButton;
    UPROPERTY() TObjectPtr<UBorder> DeathBanner;
    UPROPERTY() TObjectPtr<UTextBlock> DeathTitle;
    UPROPERTY() TObjectPtr<UTextBlock> DeathCauseText;
    UPROPERTY() TObjectPtr<UTextBlock> DeathCountdown;
    UPROPERTY() TObjectPtr<UTextBlock> DeathNote;
    // The recap of the death shown: a heading, optional collapse button, one line per attacker, and a [B] workshop capsule (#331).
    UPROPERTY() TObjectPtr<UTextBlock> DeathRecapHeading;
    UPROPERTY() TObjectPtr<ULDCommandButton> DeathRecapToggle;
    UPROPERTY() TObjectPtr<UWidget> DeathRecapContainer;
    UPROPERTY() TObjectPtr<ULDCommandButton> DeathWorkshopButton;
    bool bDeathRecapCollapsed=false;
    // One icon chain per recap row (LDRecapChain), rebuilt only when its row changes.
    UPROPERTY() TArray<TObjectPtr<UHorizontalBox>> DeathRecapRows;
    TArray<LDHUD::FRecapChain> DeathRecapShown;
    UPROPERTY() TObjectPtr<UBorder> MapColumn;
    UPROPERTY() TObjectPtr<USizeBox> MapFrame;
    // The elapsed match time (LDHUD::MatchTimer), centred right above the minimap frame; click-through.
    UPROPERTY() TObjectPtr<UTextBlock> MatchTimerText;
    // The bottom lower-third strip (APP_SPEC §7): the seat block (own jersey number in the seat ink, the basic attack's combo
    // ticks under it), level and experience, the health figure over its segmented bar, momentum with the overdrive notch,
    // then the Q/W/E and D slots over their key bands. The slabs and bars are painted (PaintStrip) on the widgets' layout.
    UPROPERTY() TObjectPtr<UBorder> Band;
    // The seat block; its brush colour keeps the own identity tint (the workshop reads it), it draws nothing itself.
    UPROPERTY() TObjectPtr<UBorder> Portrait;
    UPROPERTY() TObjectPtr<UTextBlock> LevelBadgeText;
    UPROPERTY() TObjectPtr<UWidget> HealthBarSpot;
    UPROPERTY() TObjectPtr<UWidget> MomentumBarSpot;
    UPROPERTY() TObjectPtr<UWidget> StripPanelSpot;
    struct FStripPaint
    {
        int32 Seat=INDEX_NONE;
        bool bHealth=false;
        float Health=0.f;
        float Shield=0.f;
        FLinearColor HealthInk=FLinearColor::White;
        // One cut per 250 HP of the maximum (as a fraction; 0 = none).
        float HealthSegment=0.f;
        bool bMomentum=false;
        float Momentum=0.f;
        float Notch=-1.f;
        bool bOverdrive=false;
        int32 Combo=0;
    } StripPaint;
    mutable FLDHudTrail StripTrail;
    void PaintStrip(const LDUIPaint::FContext& P) const;
    // On-air moments, painted over the whole screen and never hit-tested (local real time, from real own events only):
    // an own kill (LDWorldFeedback Kill) wipes a compact K.O. plate with the own and the victim's plates in under the score
    // strip for KillMomentSeconds, the fight staying in view; a level-up flips an orange level card over the level column.
    static constexpr double KillMomentSeconds=1.4;
    static constexpr double LevelFlipSeconds=1.4;
    double KillMomentAt=-1.;
    int32 KillMomentVictimSeat=INDEX_NONE;
    double LevelFlipAt=-1.;
    int32 LevelFlipLevel=0;
    int64 LevelFlipPoints=0;
    // Stage change (field <-> rest, the final field): the stage last seen, when its title card began, and this frame's
    // countdown figure, beat (1 as a second lands, falling to 0), closing wipe (0..1) and the title of the stage ahead.
    static constexpr double StageCardSeconds=1.1;
    ELDMatchStage SeenStage=ELDMatchStage::None;
    uint32 SeenStageMatch=0;
    double StageWipeAt=-1.;
    FString StageWipeTitle,StageWipeSub,StageCount,StageNextTitle;
    float StageBeat=0.f,StagePreCover=0.f;
    // Which of the eight beats of the match the title card names (R0, field 1, R1, field 2, R2, field 3, R3, final field).
    int32 StageWipeBeat=0;
    void PaintMoments(const LDUIPaint::FContext& P) const;
    // DEV capture only (probes): a fixed age for the moments' clock (<0 releases it); the moments themselves still come
    // only from real events.
    float MomentFreezeAge=-1.f;
public:
    void FreezeMoments(float Age){MomentFreezeAge=Age;}
    // When the last own-kill moment began (local seconds), <0 before the first.
    double MomentKillAt() const {return KillMomentAt;}
    // When the last stage title card began (local seconds), <0 before the first; whether a stage countdown shows now.
    double MomentStageAt() const {return StageWipeAt;}
    bool HasStageCount() const {return !StageCount.IsEmpty();}
    bool HasMomentLevelUp() const {return LevelFlipAt>=0.;}
private:
    // The own stat attribute view (ULDStatBar) as a flyout above the strip, shown while the Attributes key holds it open.
    UPROPERTY() TObjectPtr<UWidget> StatFlyout;
    // Spec §12: the own life line (protection, death) above the seat block; plain alive shows none.
    UPROPERTY() TObjectPtr<UTextBlock> LifeText;
    // The own statuses (LDHUD::SelfIcons) are painted by ULDWorldLabels above the own overhead bars, as LoL-style icons
    // (OwnStatusMarks). The HUD builds the marks with the band's gate, keeps their textures alive and their remaining-time
    // spans (SelfIconRings); the world layer reports where it painted them, so a hovered mark opens the slot popup
    // (LDHUD::SelfIconDetail).
    TArray<LDHUD::FSelfIconView> SelfIconViews;
    TArray<FLDOwnStatusMark> OwnStatusMarks;
    FString OwnStatusNotice;
    TArray<FSlateBrush> OwnStatusBrushes;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> OwnStatusTextures;
    mutable TArray<FSlateRect> OwnStatusHitRects;
    LDHUD::FStatusRings SelfIconRings;
    UPROPERTY() TObjectPtr<UTextBlock> HealthText;
    UPROPERTY() TObjectPtr<UTextBlock> HealthMaxText;
    UPROPERTY() TObjectPtr<UTextBlock> MomentumText;
    UPROPERTY() TObjectPtr<UTextBlock> HealthRegenText;
    UPROPERTY() TObjectPtr<UProgressBar> LevelBar;
    FString LevelTip;
    // The toast's own canvas slot, kept right above the band.
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> ToastSlot;
    // The seat block's box: the storage's body column follows it.
    UPROPERTY() TObjectPtr<USizeBox> BodySlot;
    // The rank-up page (issue #287 spec §12, #331 3-rank preview): two big cards of the chosen skill's next rank, with
    // Rank 3 sub-branch preview chips beneath 2A and 2B during 1->2 selection; a click or 1/2 submits RankUpSkill.
    UPROPERTY() TObjectPtr<UBorder> RankPage;
    UPROPERTY() TObjectPtr<UTextBlock> RankTitle;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> RankCards;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> RankCardNames;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> RankCardLines;
    UPROPERTY() TArray<TObjectPtr<UImage>> RankCardIcons;
    UPROPERTY() TArray<TObjectPtr<UWidget>> RankSubRows;
    UPROPERTY() TArray<TObjectPtr<UBorder>> RankSubChips;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> RankSubNames;
    UPROPERTY() TArray<TObjectPtr<UImage>> RankSubIcons;
    int32 RankSlot=INDEX_NONE;
    TArray<ELDSkillBranch> RankOptions;
    FName RankAbility;
    void OpenRankUp(int32 ProgressionSlot);
    void SubmitRankUp(int32 Card);
    void RefreshRankUp(const FLDProgressionView* Progression,const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats);
    UPROPERTY() TArray<FLDHUDActionSlot> ActionSlots;
    // The hovered slot's full description (#328 §3 live skill tooltip + Alt/C expandable formula breakdown).
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> ActionDetailSlot;
    UPROPERTY() TObjectPtr<UTextBlock> ActionDetailName;
    UPROPERTY() TObjectPtr<UTextBlock> ActionDetailSummary;
    UPROPERTY() TObjectPtr<UTextBlock> ActionDetailBody;
    UPROPERTY() TObjectPtr<UBorder> ActionFormulaPanel;
    UPROPERTY() TObjectPtr<UTextBlock> ActionFormulaText;
    UPROPERTY() TObjectPtr<UTextBlock> ActionDetailFooter;
    UPROPERTY() TMap<FName,TObjectPtr<ULDCommandButton>> Commands;
    // The bottom-right key plates (属性, and 测试 in a DEV test room); the build entry is the ring's closed state (ULDWorkshop).
    UPROPERTY() TObjectPtr<UBorder> PromptFrame;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> PromptSlot;
    // #328 §4.2: 3-channel non-clobbering notifications (Channel A: 0.9s slot/cursor reject micro-tip; Channel B: right-side
    // max-3 3.5s loot/growth queue; Channel C: top-center broadcast / ToastPanel).
    UPROPERTY() TObjectPtr<UBorder> SlotRejectPanel;
    UPROPERTY() TObjectPtr<UTextBlock> SlotRejectText;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> SlotRejectSlot;
    double SlotRejectAt=-10.;
    int32 SlotRejectIndex=INDEX_NONE;
    FVector2D SlotRejectCursor=FVector2D::ZeroVector;
    struct FLootNoticeEntry
    {
        FString Text;
        uint8 Kind=0; // 0: skill (#F59E0B), 1: build card (#3B82F6), 2: element stone (#10B981), 3: chain/other
        double At=0.;
    };
    TArray<FLootNoticeEntry> LootNotices;
    UPROPERTY() TObjectPtr<UVerticalBox> LootQueueBox;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> LootQueueSlot;
    UPROPERTY() TArray<TObjectPtr<UBorder>> LootQueueCards;
    UPROPERTY() TArray<TObjectPtr<UBorder>> LootQueueDots;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> LootQueueTexts;
    uint32 LootSeenMatch=0;
    TSet<FName> LootKnownBlueprints;
    TSet<uint16> LootKnownCardSerials;
    void PushSlotRejectTip(const FString& Text,int32 ActionSlotIndex=INDEX_NONE);
    void PushLootNotice(const FString& Text,uint8 Kind);

    // The own training combo (ALDPlayerController::TrainingCombo) already shown beside the rest actions.
    uint32 SeenTrainingCombo=0;
    // ALDPlayerController::GetArmedCancelSerial already shown as a slot flash.
    uint32 SeenArmedCancel=0;
    UPROPERTY() TObjectPtr<UBorder> ToastPanel;
    UPROPERTY() TObjectPtr<UTextBlock> Toast;
    UPROPERTY() TArray<TObjectPtr<UBorder>> HitPanels;
    double FeedbackAt=0;
    double FeedbackDuration=5;
    int32 FeedbackRepeatCount=0;
    // The toast shows controller intent feedback and local panel notices; SeenIntent tracks only the former.
    FString LastFeedback,LastFeedbackBase;
    FString SeenIntent;
    uint32 SeenFeedback=0;
    void ShowToast(const FString& Text);
    uint32 SeenCombatReceipt=0;
    uint64 ToldProtectionLifeRevision=0;
    // D10/#198: the growth panel opens by itself for a new offer or new points; a refused open is retried for a short
    // while (the root may still be leaving the lobby) unless the player's own panel or captured input ends the chance.
    LDHUD::FGrowthAutoOpen GrowthAutoOpen;
    double AutoOpenSince=-1;
    // #198 G: each new field reward of the own view is toasted once (LDHUD::FieldRewardNotice).
    LDHUD::FFieldRewardWatch FieldRewardWatch;
    LDHUD::FMedalDropWatch MedalDropWatch;
    // Own-body hits (the same health and shield losses the world feedback floats), which fold the growth panel.
    LDWorldFeedback::FTracker OwnHitWatch;
    void HandleGrowthSubmit(const FLDSubmitResult& Result);
    void HandleBuildReceipt(const FLDBuildReceiptQueryResult& Result);
    void RefreshScoreboard(const class ALDGameState& Public,const FLDOwnerState& Own);
    // Authority time at which the current life was first seen alive; the death banner takes its cause only from notices
    // at or after it, so a previous death never stands in while this one's notice is still on its way.
    uint32 LifeSeenMatch=0;
    uint32 LifeSeenLife=0;
    int64 LifeSeenAliveMs=0;
    FGuid ShownResultId;
    uint64 ShownResultSerial=0;
    // Spec §6.5: the last chain end of the own view this HUD handed to the codex, per match.
    uint32 ChainMatch=0;
    uint32 ChainSerial=0;
    // The result enters winner first, then the rows one by one, then its buttons (real seconds; -1 when settled).
    double ResultShownAt=-1.;
    UPROPERTY() TArray<TObjectPtr<UWidget>> ResultEntries;
    UPROPERTY() TObjectPtr<UWidget> ResultActionsBox;
    UPROPERTY() TObjectPtr<UWidget> ResultWinnerBand;
    void AnimateResult();
    float AppliedScale=0;
    // The last known owner level and stats for the growth panel's card numbers (#147).
    LDHUD::FCardStats ChoiceStats;
    // Approved public definitions used only for display labels; loaded once per widget.
    TSharedPtr<const FLDDemoContentSnapshot> DisplayContent;
    bool bDisplayContentTried=false;
    const FLDDemoContentDefinitions* DisplayDefinitions();
    void RefreshEventFeed(const class ALDGameState& Public,const FLDOwnerState& Own,const FLDDemoContentDefinitions* Content,bool bClock,int64 NowMs);
    void RefreshResult(class ALDPlayerController& PC,const class ALDGameState& Public);
    bool IsUIAt(FVector2D Absolute) const;
    TArray<ULDPanel*,TInlineAllocator<4>> MainPanels() const;
    void HandleDevTestResult(const FLDDevTestResult& Result);
    // ---- Audio: local 2D cues from the same public DTOs and own projection/receipts this HUD reads ----
    FLDAudioCueTracker AudioCues;
    uint32 AudioCombatReceipt=0;
    bool bAudioPrimed=false;
    void ObserveAudio(class ALDPlayerController& PC,const class ALDGameState& Public);
    void PlayAudioCues(const TArray<ELDAudioCue>& Cues);
    // ---- Read model gate (SampleReadModel) ----
    void RebuildReadModel(class ALDPlayerController& PC,const class ALDGameState& GS);
    LDHUD::FReadModelKey ReadModelKey(class ALDPlayerController& PC,const class ALDGameState& GS);
    LDHUD::FReadModelGate ReadModelGate;
    // Set by a build in which something on screen moves with time, so the next sample rebuilds as well.
    bool bReadModelLive=false;
    // Bumped by the HUD's own answers and notices (server results routed here, toasts, local toggles).
    uint64 ReadModelLocal=0;
    void MarkReadModelDirty(){++ReadModelLocal;}
    // The projections the read model consumes, as last seen; any difference advances ReadModelViews. Those without a
    // revision every writer advances (the Listen Host writes them in place) are kept and compared by value.
    uint64 ReadModelViews=0;
    struct FReadModelSeen
    {
        FLDOwnerState Owner;
        uint32 PublicGeneration=0;
        uint64 PublicRevision=0;
        uint64 LobbyRevision=0;
        uint64 RosterRevision=0;
        FLDKillFeedView Kills;
        FLDResultView Result;
        FLDDeathRecapView Recap;
        FLDDamageSummaryView Summary;
        uint64 ReactionAssistSerial=0;
        uint32 CombatGeneration=0;
        uint32 CombatLife=0;
        uint64 CombatRevision=0;
        bool bCombatBody=false;
        TWeakObjectPtr<const AActor> Body;
        int32 Presentation=-1;
        const void* Content=nullptr;
    } ReadModelSeen;
};
