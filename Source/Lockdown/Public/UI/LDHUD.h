#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Development/LDGrowthProtocol.h"
#include "Audio/LDAudioCueTracker.h"
#include "UI/LDWorldFeedback.h"
#include "UI/LDHUDModel.h"
#include "LDHUD.generated.h"
class UBorder;
class UHorizontalBox;
class UCanvasPanelSlot;
class UTextBlock;
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
// the icon, the rank and the mana cost; the state is the frame and icon colour, and the tooltip keeps the words.
USTRUCT()
struct FLDHUDActionSlot
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<USizeBox> Box;
    UPROPERTY() TObjectPtr<ULDCommandButton> Button;
    UPROPERTY() TObjectPtr<UBorder> Frame;
    // Placeholder icon until real art lands (#93): a rounded square in the entry kind's colour with the name's first glyph.
    UPROPERTY() TObjectPtr<UBorder> Icon;
    UPROPERTY() TObjectPtr<UTextBlock> Glyph;
    UPROPERTY() TObjectPtr<UTextBlock> Key;
    UPROPERTY() TObjectPtr<UBorder> RankChip;
    UPROPERTY() TObjectPtr<UTextBlock> Rank;
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
    UPROPERTY() TObjectPtr<ULDMinimap> Minimap;
    // The two normal main panels (UI 3.1). The HUD owns and hit-tests them; ULDUIRoot opens one at a time.
    UPROPERTY() TObjectPtr<ULDAttributesPanel> AttributesPanel;
    UPROPERTY() TObjectPtr<ULDChoicesPanel> ChoicesPanel;
    // DEV test session only (#78): the 测试 panel; its entry button and the 测试模式 marker show only in such a room.
    UPROPERTY() TObjectPtr<ULDDevTestPanel> DevTestPanel;
protected:
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
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
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> DragGhostSlot;
    void UpdateDrag();
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override;
private:
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
    // The visible contender's Q/W tiles: key and name, then rank with the Lv3 mark.
    UPROPERTY() TObjectPtr<UWidget> TargetActivesRow;
    UPROPERTY() TArray<TObjectPtr<UBorder>> TargetActiveTiles;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> TargetActiveNames;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> TargetActiveRanks;
    UPROPERTY() TObjectPtr<UBorder> PhaseBanner;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseText;
    UPROPERTY() TObjectPtr<UTextBlock> DevMarkerText;
    UPROPERTY() TObjectPtr<UProgressBar> CircleProgress;
    UPROPERTY() TObjectPtr<UTextBlock> NextStepText;
    UPROPERTY() TObjectPtr<UTextBlock> AirdropText;
    UPROPERTY() TObjectPtr<UTextBlock> LeaderText;
    UPROPERTY() TObjectPtr<UTextBlock> BossText;
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
    // #153: the centre-screen opening countdown ("5" .. "1", then "开淫！"); click-through.
    UPROPERTY() TObjectPtr<UTextBlock> OpeningCountdownText;
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
    // Blind-box button right above the band's left block (#97), with the three-choice panel it opens in the same row:
    // disabled without a pending choice; it pulses for a few seconds after the count rises, then stays steady.
    UPROPERTY() TObjectPtr<UBorder> PendingPrompt;
    UPROPERTY() TObjectPtr<UBorder> PromptFrame;
    UPROPERTY() TObjectPtr<UCanvasPanelSlot> PromptSlot;
    float AppliedChoicesDock=-1,AppliedChoicesLeft=-1;
    UPROPERTY() TObjectPtr<ULDCommandButton> PromptButton;
    UPROPERTY() TObjectPtr<UTextBlock> PromptText;
    int32 PromptCount=0;
    double PromptPulseAt=-1;
    float AppliedPromptWave=-1;
    UPROPERTY() TObjectPtr<UBorder> ToastPanel;
    UPROPERTY() TObjectPtr<UTextBlock> Toast;
    UPROPERTY() TArray<TObjectPtr<UBorder>> HitPanels;
    double FeedbackAt=0;
    // The replacement pick's held prompt (RefreshReadModel): whether it showed last frame and since when.
    bool bReplacePromptShown=false;
    double ReplacePromptSince=0;
    // The toast shows controller intent feedback and local panel notices; SeenIntent tracks only the former.
    FString LastFeedback;
    FString SeenIntent;
    uint32 SeenFeedback=0;
    void ShowToast(const FString& Text);
    uint32 SeenCombatReceipt=0;
    uint64 ToldProtectionLifeRevision=0;
    // The match generation whose opening three-choice was offered its one automatic panel opening (D10).
    uint32 AutoChoicesMatch=0;
    uint32 AutoChoicesPending=0;
    double AutoChoicesSince=-1;
    // Own-body hits (the same health and shield losses the world feedback floats), which fold the three-choice panel.
    LDWorldFeedback::FTracker OwnHitWatch;
    void HandleGrowthSubmit(const FLDSubmitResult& Result);
    void HandleChoiceReceipt(const FLDChoiceReceiptQueryResult& Result);
    void RefreshScoreboard(const class ALDGameState& Public,const FLDOwnerState& Own);
    // Authority time at which the current life was first seen alive; the death banner takes its cause only from notices
    // at or after it, so a previous death never stands in while this one's notice is still on its way.
    uint32 LifeSeenMatch=0;
    uint32 LifeSeenLife=0;
    int64 LifeSeenAliveMs=0;
    FGuid ShownResultId;
    uint64 ShownResultSerial=0;
    float AppliedScale=0;
    // The last known owner level and stats for the blind-box card numbers (#147).
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
