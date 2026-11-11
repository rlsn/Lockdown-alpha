#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchTimeline.h"
#include "Audio/LDAudioCueTracker.h"
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
class ULDAttributesPanel;
class ULDChoicesPanel;
class ULDDevTestPanel;
struct FLDDevTestResult;
class FLDDemoContentSnapshot;
struct FLDDemoContentDefinitions;

// One Q/W/E/D entry: a world button that sends the same activation as its hotkey. It is enabled only while an intent can
// be submitted; execution limits colour it but never replace the server's decision. The face shows only the key,
// the icon and the mana cost; the state is the frame and icon colour, and the tooltip keeps the words.
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
    // Local presentation only (real time): the applied look, the ready flash and the last rejected press of this slot.
    int32 AppliedLook=-1;
    FString AppliedIcon;
    int32 AppliedTint=-1;
    float AppliedOpacity=1.f;
    float AppliedPop=1.f;
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
    void RefreshReadModel();
    bool PointerOverUI() const;
    // Called by the hit-test-invisible world layer, below the HUD's panels.
    void PaintWorldPreviews(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer) const;
    TArray<FSlateRect> WorldLabelReservedAreas(const FGeometry& PaintGeometry) const;
    UPROPERTY() TObjectPtr<ULDMinimap> Minimap;
    // The two normal main panels (UI 3.1). The HUD owns and hit-tests them; ULDUIRoot opens one at a time. ChoicesPanel is
    // the #198 growth panel (active offer, blueprints and points) behind the 开盲盒 button.
    UPROPERTY() TObjectPtr<ULDAttributesPanel> AttributesPanel;
    UPROPERTY() TObjectPtr<ULDChoicesPanel> ChoicesPanel;
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
    UPROPERTY() TObjectPtr<UWidget> TargetManaRow;
    UPROPERTY() TObjectPtr<UProgressBar> TargetManaBar;
    UPROPERTY() TObjectPtr<UTextBlock> TargetMana;
    UPROPERTY() TObjectPtr<UTextBlock> TargetStatuses;
    // The visible contender's Q/W/E tiles: key and name (#198: an active has one level).
    UPROPERTY() TObjectPtr<UWidget> TargetActivesRow;
    UPROPERTY() TArray<TObjectPtr<UBorder>> TargetActiveTiles;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> TargetActiveNames;
    UPROPERTY() TObjectPtr<UBorder> PhaseBanner;
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
    UPROPERTY() TObjectPtr<ULDCommandButton> RestServiceButton;
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
    void OpenRestService();
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
    UPROPERTY() TObjectPtr<class UScrollBox> PKScroll;
    // Local PK state, kept for one PK (match generation and serial) only: a bet or throw waiting for its answer, the armed
    // throw aim, the last throw answer's ReadyAtMs and the last answer line.
    bool bPKBetInFlight=false;
    bool bPKThrowArmed=false;
    bool bPKThrowInFlight=false;
    bool bPKAwardsRevealed=false;
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
    UPROPERTY() TObjectPtr<UTextBlock> DevMarkerText;
    UPROPERTY() TObjectPtr<UProgressBar> CircleProgress;
    // Secondary objective details and the own next step sit with the minimap, away from the screen centre.
    UPROPERTY() TObjectPtr<UTextBlock> WorldSummaryText;
    // Top-left event feed (LDHUD::FEventFeed): one row per shown line, newest at the bottom.
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
    UPROPERTY() TObjectPtr<class UVerticalBox> ResultRows;
    UPROPERTY() TObjectPtr<ULDCommandButton> ReturnButton;
    UPROPERTY() TObjectPtr<UBorder> DeathBanner;
    UPROPERTY() TObjectPtr<UTextBlock> DeathTitle;
    UPROPERTY() TObjectPtr<UTextBlock> DeathCauseText;
    UPROPERTY() TObjectPtr<UTextBlock> DeathCountdown;
    UPROPERTY() TObjectPtr<UTextBlock> DeathNote;
    // #198: the centre-screen rest return countdown ("5" .. "1", then "开淫！" as the field starts; LDHUD::RestReturnCountdown);
    // click-through.
    UPROPERTY() TObjectPtr<UTextBlock> ReturnCountdownText;
    UPROPERTY() TObjectPtr<UBorder> MapColumn;
    UPROPERTY() TObjectPtr<USizeBox> MapFrame;
    // The elapsed match time (LDHUD::MatchTimer), centred right above the minimap frame; click-through.
    UPROPERTY() TObjectPtr<UTextBlock> MatchTimerText;
    UPROPERTY() TObjectPtr<UTextBlock> SightText;
    UPROPERTY() TObjectPtr<UBorder> Band;
    UPROPERTY() TObjectPtr<UBorder> Portrait;
    UPROPERTY() TObjectPtr<UTextBlock> PortraitGlyph;
    UPROPERTY() TObjectPtr<UTextBlock> LevelBadgeText;
    UPROPERTY() TObjectPtr<UTextBlock> SelfName;
    UPROPERTY() TObjectPtr<UTextBlock> LifeText;
    // The own status grid (LDHUD::SelfIcons) under the 属性 button: SelfIconMax prebuilt squares in SelfIconRows rows,
    // "…" under a full grid, a syncing line while an own projection is missing. Timed statuses get a remaining-time
    // ring (SelfIconRing, -1 for none) that NativePaint draws around the square.
    UPROPERTY() TArray<TObjectPtr<UHorizontalBox>> SelfIconRowBoxes;
    UPROPERTY() TArray<TObjectPtr<UBorder>> SelfIconCells;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SelfIconGlyphs;
    UPROPERTY() TArray<TObjectPtr<UImage>> SelfIconPictures;
    TArray<FString> SelfIconApplied;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SelfIconStacks;
    UPROPERTY() TObjectPtr<UTextBlock> SelfIconMore;
    UPROPERTY() TObjectPtr<UTextBlock> SelfIconSyncing;
    // The squares' current views; the hovered one opens the slot popup (LDHUD::SelfIconDetail).
    TArray<LDHUD::FSelfIconView> SelfIconViews;
    // Each square's applied tone: -1 none yet, 0 negative, 1 positive.
    TArray<int32> SelfIconTones;
    TArray<float> SelfIconRing;
    LDHUD::FStatusRings SelfIconRings;
    UPROPERTY() TObjectPtr<UProgressBar> HealthBar;
    UPROPERTY() TObjectPtr<UTextBlock> HealthText;
    UPROPERTY() TObjectPtr<UProgressBar> ManaBar;
    UPROPERTY() TObjectPtr<UTextBlock> ManaText;
    UPROPERTY() TObjectPtr<UTextBlock> HealthRegenText;
    UPROPERTY() TObjectPtr<UTextBlock> ManaRegenText;
    UPROPERTY() TObjectPtr<UProgressBar> LevelBar;
    FString LevelTip;
    // The toast's own canvas slot, kept right above the band.
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> ToastSlot;
    UPROPERTY() TArray<FLDHUDActionSlot> ActionSlots;
    // The hovered slot's full description, popped up above it like the blind-box descriptions; it takes no input.
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> ActionDetailSlot;
    UPROPERTY() TObjectPtr<UTextBlock> ActionDetailName;
    UPROPERTY() TObjectPtr<UTextBlock> ActionDetailSummary;
    UPROPERTY() TObjectPtr<UTextBlock> ActionDetailBody;
    UPROPERTY() TMap<FName,TObjectPtr<ULDCommandButton>> Commands;
    // 开盲盒 button right above the band's left block (#97), with the growth panel it opens in the same row: disabled while
    // nothing is pending (LDHUD::GrowthPending) and no rest build window offers the rest services (#198); it pulses for a
    // few seconds after the opportunities, points or pending components rise.
    UPROPERTY() TObjectPtr<UBorder> PendingPrompt;
    UPROPERTY() TObjectPtr<UBorder> PromptFrame;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> PromptSlot;
    UPROPERTY() TObjectPtr<ULDCommandButton> PromptButton;
    UPROPERTY() TObjectPtr<UTextBlock> PromptText;
    int32 PromptOffers=0;
    int64 PromptPoints=0;
    int32 PromptComponents=0;
    double PromptPulseAt=-1;
    float AppliedPromptWave=-1;
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
    float AppliedScale=0;
    // The last known owner level and stats for the growth panel's card numbers (#147).
    LDHUD::FCardStats ChoiceStats;
    float MapAspect=0;
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
};
