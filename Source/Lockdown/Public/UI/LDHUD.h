#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchTimeline.h"
#include "Audio/LDAudioCueTracker.h"
#include "Combat/LDDamageRecap.h"
#include "UI/LDWorldFeedback.h"
#include "UI/LDHUDModel.h"
#include "PK/LDPKRules.h"
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
    UPROPERTY() TObjectPtr<UTextBlock> Key;
    UPROPERTY() TObjectPtr<UBorder> CostChip;
    UPROPERTY() TObjectPtr<UTextBlock> Cost;
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
    // The normal main panels (VISION_AND_UI navigation) with DevTestPanel below. The HUD owns and hit-tests them; ULDUIRoot opens one at a
    // time. ChoicesPanel is the #198 构筑 card tray behind the bottom-right 构筑 button, and the rest's NPC service window.
    UPROPERTY() TObjectPtr<ULDChoicesPanel> ChoicesPanel;
    // #227: the component storage right of the band and its equipped columns (ULDComponentStorage); not a main panel.
    UPROPERTY() TObjectPtr<ULDComponentStorage> Storage;
    // The build workshop (APP_SPEC §7 构筑): the full view of the 构筑 tray, open while the tray is open (ULDWorkshop).
    UPROPERTY() TObjectPtr<ULDWorkshop> Workshop;
    // The own point-bought stats under the name in the band's identity block: hover a stat for its "+", one click commits
    // one point (ULDStatBar). The 属性 entry and the Attributes key swap it for the attribute view and back.
    UPROPERTY() TObjectPtr<ULDStatBar> StatBar;
    void ToggleAttributeView();
    // The Workshop key: the same toggle as the 构筑 button, only while that button would accept a click or the workshop is open.
    void ToggleWorkshop();
    // Back: closes the storage's chooser or drops its drag (V05Back, after an armed preview).
    bool CancelStorage();
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
    // #225 DEV probe reads the rendered PK settlement rows.
    friend class FLDDemoOpponentProbe;
    UPROPERTY() TObjectPtr<UBorder> DiagnosticsPanel;
    UPROPERTY() TObjectPtr<UTextBlock> DiagnosticsText;
    LDHUD::FFrameDiagnostics FrameDiagnostics;
    double DiagnosticsTickAt=-1, DiagnosticsSampleAt=0;
    uint32 DiagnosticsCorrections=0;
    TWeakObjectPtr<class ALDCharacter> DiagnosticsBody;
    UPROPERTY() TObjectPtr<UBorder> TargetCard;
    UPROPERTY() TObjectPtr<UBorder> TargetPortrait;
    UPROPERTY() TObjectPtr<UTextBlock> TargetGlyph;
    UPROPERTY() TObjectPtr<UTextBlock> TargetTitle;
    UPROPERTY() TObjectPtr<UProgressBar> TargetHealthBar;
    UPROPERTY() TObjectPtr<UTextBlock> TargetHealth;
    UPROPERTY() TObjectPtr<UWidget> TargetMomentumRow;
    UPROPERTY() TObjectPtr<UProgressBar> TargetMomentumBar;
    UPROPERTY() TObjectPtr<UTextBlock> TargetMomentum;
    UPROPERTY() TObjectPtr<UTextBlock> TargetStatuses;
    // The visible contender's Q/W/E icon tiles (#198: an active has one level); the name's first character shows when the
    // icon is missing, and the key and name are the tile's tooltip.
    UPROPERTY() TObjectPtr<UWidget> TargetActivesRow;
    UPROPERTY() TArray<TObjectPtr<UBorder>> TargetActiveTiles;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> TargetActiveNames;
    UPROPERTY() TArray<TObjectPtr<UImage>> TargetActiveIcons;
    UPROPERTY() TObjectPtr<UBorder> PhaseBanner;
    // The top-centre stack (the banner and the rest actions); the full workshop moves it to the top-right corner.
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> TopCentreSlot;
    // The workshop opens by itself when the own death wait begins (never for a rest), and closes when that full-screen
    // moment ends.
    bool bWorkshopSeenDead=false;
    bool bWorkshopWasFull=false;
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
    UPROPERTY() TObjectPtr<UTextBlock> PKTitleText;
    UPROPERTY() TObjectPtr<UTextBlock> PKCountdownText;
    UPROPERTY() TObjectPtr<UTextBlock> PKRingText;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> PKSideTexts;
    UPROPERTY() TObjectPtr<UTextBlock> PKRoleText;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> PKBetButtons;
    UPROPERTY() TObjectPtr<ULDCommandButton> PKThrowButton;
    UPROPERTY() TObjectPtr<UTextBlock> PKResultLine;
    UPROPERTY() TObjectPtr<UTextBlock> PKAwardsTitle;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> PKAwardTexts;
    UPROPERTY() TObjectPtr<UTextBlock> PKAnswerText;

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
    UPROPERTY() TObjectPtr<UTextBlock> PhaseText;
    // Under the capture bar (Mock 1): the graffiti owner on the left, the guard state on the right.
    UPROPERTY() TObjectPtr<UWidget> PhaseFootRow;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseOwnerText;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseAsideText;
    UPROPERTY() TObjectPtr<UTextBlock> DevMarkerText;
    UPROPERTY() TObjectPtr<UProgressBar> CircleProgress;
    // Bottom-left event feed (LDHUD::FEventFeed): one row per shown line, newest at the bottom.
    UPROPERTY() TObjectPtr<UBorder> EventFeedPanel;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> EventFeedRows;
    LDHUD::FEventFeed EventFeed;
    // Hold-Tab scoreboard (LDHUD::Scoreboard), centred: one row per contender, four cells a row (name, K, D, A).
    UPROPERTY() TObjectPtr<UBorder> ScorePanel;
    UPROPERTY() TArray<TObjectPtr<UWidget>> ScoreRows;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> ScoreCells;
    UPROPERTY() TObjectPtr<UBorder> ResultPanel;
    UPROPERTY() TObjectPtr<UTextBlock> ResultTitle;
    UPROPERTY() TObjectPtr<UTextBlock> ResultSubtitle;
    UPROPERTY() TObjectPtr<USizeBox> ResultWinnerBuild;
    UPROPERTY() TObjectPtr<class UVerticalBox> ResultRows;
    // The own per-source damage of the shown result: a heading and a fixed grid of cells (never a scroll view).
    UPROPERTY() TObjectPtr<UTextBlock> ResultDamageHeading;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> ResultDamageCells;
    UPROPERTY() TObjectPtr<ULDCommandButton> ReturnButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> LeaveRoomButton;
    UPROPERTY() TObjectPtr<UBorder> DeathBanner;
    UPROPERTY() TObjectPtr<UTextBlock> DeathTitle;
    UPROPERTY() TObjectPtr<UTextBlock> DeathCauseText;
    UPROPERTY() TObjectPtr<UTextBlock> DeathCountdown;
    UPROPERTY() TObjectPtr<UTextBlock> DeathNote;
    // The recap of the death shown: a heading and one line per attacker.
    UPROPERTY() TObjectPtr<UTextBlock> DeathRecapHeading;
    // One icon chain per recap row (LDRecapChain), rebuilt only when its row changes.
    UPROPERTY() TArray<TObjectPtr<UHorizontalBox>> DeathRecapRows;
    TArray<LDHUD::FRecapChain> DeathRecapShown;
    // #198: the centre-screen rest return countdown ("5" .. "1", then "开淫！" as the field starts; LDHUD::RestReturnCountdown);
    // click-through.
    UPROPERTY() TObjectPtr<UTextBlock> ReturnCountdownText;
    UPROPERTY() TObjectPtr<UBorder> MapColumn;
    UPROPERTY() TObjectPtr<USizeBox> MapFrame;
    // The elapsed match time (LDHUD::MatchTimer), centred right above the minimap frame; click-through.
    UPROPERTY() TObjectPtr<UTextBlock> MatchTimerText;
    UPROPERTY() TObjectPtr<UBorder> Band;
    UPROPERTY() TObjectPtr<UBorder> Portrait;
    UPROPERTY() TObjectPtr<UTextBlock> PortraitGlyph;
    UPROPERTY() TObjectPtr<UTextBlock> LevelBadgeText;
    // Unspent points on the portrait's bottom-left corner (the level rides on the bottom-right); hidden at 0.
    UPROPERTY() TObjectPtr<UBorder> PointsBadge;
    UPROPERTY() TObjectPtr<UTextBlock> PointsBadgeText;
    UPROPERTY() TObjectPtr<UTextBlock> SelfName;
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
    UPROPERTY() TObjectPtr<UProgressBar> HealthBar;
    UPROPERTY() TObjectPtr<UTextBlock> HealthText;
    UPROPERTY() TObjectPtr<UProgressBar> MomentumBar;
    UPROPERTY() TObjectPtr<UTextBlock> MomentumText;
    UPROPERTY() TObjectPtr<UTextBlock> HealthRegenText;
    UPROPERTY() TObjectPtr<UProgressBar> LevelBar;
    FString LevelTip;
    // The toast's own canvas slot, kept right above the band.
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> ToastSlot;
    UPROPERTY() TObjectPtr<USizeBox> BodySlot;
    // The basic attack's three combo pips under the body slot: the current step lit, the third gold.
    UPROPERTY() TArray<TObjectPtr<UBorder>> ComboPips;
    // The rank-up page (issue #287 spec §12): two big cards of the chosen skill's next rank; a click submits RankUpSkill.
    UPROPERTY() TObjectPtr<UBorder> RankPage;
    UPROPERTY() TObjectPtr<UTextBlock> RankTitle;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> RankCards;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> RankCardNames;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> RankCardLines;
    UPROPERTY() TArray<TObjectPtr<UImage>> RankCardIcons;
    int32 RankSlot=INDEX_NONE;
    TArray<ELDSkillBranch> RankOptions;
    FName RankAbility;
    void OpenRankUp(int32 ProgressionSlot);
    void SubmitRankUp(int32 Card);
    void RefreshRankUp(const FLDProgressionView* Progression,const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats);
    UPROPERTY() TObjectPtr<UBorder> BodyPortrait;
    UPROPERTY() TObjectPtr<UTextBlock> BodyPortraitGlyph;
    UPROPERTY() TArray<FLDHUDActionSlot> ActionSlots;
    // The hovered slot's full description, popped up above it like the blind-box descriptions; it takes no input.
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> ActionDetailSlot;
    UPROPERTY() TObjectPtr<UTextBlock> ActionDetailName;
    UPROPERTY() TObjectPtr<UTextBlock> ActionDetailSummary;
    UPROPERTY() TObjectPtr<UTextBlock> ActionDetailBody;
    UPROPERTY() TMap<FName,TObjectPtr<ULDCommandButton>> Commands;
    // The 构筑 button in the bottom-right corner, with the card tray it toggles right above it: lit while anything is pending
    // (LDHUD::GrowthPending), greyed out otherwise, and pulsing for a few seconds after the opportunities or component choices
    // rise.
    UPROPERTY() TObjectPtr<UBorder> PendingPrompt;
    UPROPERTY() TObjectPtr<UBorder> PromptFrame;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> PromptSlot;
    UPROPERTY() TObjectPtr<ULDCommandButton> PromptButton;
    UPROPERTY() TObjectPtr<UTextBlock> PromptText;
    UPROPERTY() TObjectPtr<UBorder> PromptBadge;
    UPROPERTY() TObjectPtr<UTextBlock> PromptBadgeText;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> PromptBadgeSlot;
    int32 PromptOffers=0;
    int32 PromptCardOffers=0;
    double PromptPulseAt=-1;
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
