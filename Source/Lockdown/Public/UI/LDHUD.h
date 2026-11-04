#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Development/LDGrowthProtocol.h"
#include "Audio/LDAudioCueTracker.h"
#include "UI/LDWorldFeedback.h"
#include "LDHUD.generated.h"
class UBorder;
class UCanvasPanelSlot;
class UTextBlock;
class UProgressBar;
class USizeBox;
class ULDCommandButton;
class ULDMinimap;
class ULDPanel;
class ULDAttributesPanel;
class ULDChoicesPanel;
class ULDShopPanel;
class ULDDevTestPanel;
struct FLDDevTestResult;
class FLDDemoContentSnapshot;
struct FLDDemoContentDefinitions;

// One Q/W/1/2 entry: a world button that sends the same activation as its hotkey. It is enabled only while an intent
// can be submitted; execution limits grey and explain it but never replace the server's decision.
USTRUCT()
struct FLDHUDActionSlot
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<USizeBox> Box;
    UPROPERTY() TObjectPtr<ULDCommandButton> Button;
    UPROPERTY() TObjectPtr<UBorder> Frame;
    UPROPERTY() TObjectPtr<UTextBlock> Key;
    UPROPERTY() TObjectPtr<UBorder> CostChip;
    UPROPERTY() TObjectPtr<UTextBlock> Cost;
    UPROPERTY() TObjectPtr<UTextBlock> Name;
    UPROPERTY() TObjectPtr<UTextBlock> State;
    // Full-slot cooldown mask, its remaining time, the control badge and the ready flash, drawn over the slot in that order.
    UPROPERTY() TObjectPtr<UProgressBar> Cooldown;
    UPROPERTY() TObjectPtr<UTextBlock> Countdown;
    UPROPERTY() TObjectPtr<UBorder> Badge;
    UPROPERTY() TObjectPtr<UBorder> Flash;
    // Local presentation only (real time): the applied look, the ready flash and the last rejected press of this slot.
    int32 AppliedLook=-1;
    float AppliedOpacity=1.f;
    float AppliedPop=1.f;
    bool bCooling=false;
    uint32 CoolingMatch=0;
    uint32 CoolingLife=0;
    double FlashAt=-1;
    FString Notice;
    double NoticeAt=-1;
};

// A shown kill feed line; it fades on local time only.
struct FLDHUDKillLine
{
    double At=0;
    FString Text;
    bool bSelf=false;
};

UCLASS()
class LOCKDOWN_API ULDHUD : public UUserWidget
{
    GENERATED_BODY()
public:
    void RefreshReadModel();
    bool PointerOverUI() const;
    UPROPERTY() TObjectPtr<ULDMinimap> Minimap;
    // The three normal main panels (UI 3.1). The HUD owns and hit-tests them; ULDUIRoot opens one at a time.
    UPROPERTY() TObjectPtr<ULDAttributesPanel> AttributesPanel;
    UPROPERTY() TObjectPtr<ULDChoicesPanel> ChoicesPanel;
    UPROPERTY() TObjectPtr<ULDShopPanel> ShopPanel;
    // DEV test session only (#78): the 测试 panel; its entry button and the 测试模式 marker show only in such a room.
    UPROPERTY() TObjectPtr<ULDDevTestPanel> DevTestPanel;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override;
private:
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
    UPROPERTY() TObjectPtr<UTextBlock> TargetEquipment;
    UPROPERTY() TObjectPtr<UBorder> PhaseBanner;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseText;
    UPROPERTY() TObjectPtr<UTextBlock> DevMarkerText;
    UPROPERTY() TObjectPtr<UProgressBar> CircleProgress;
    UPROPERTY() TObjectPtr<UTextBlock> NextStepText;
    UPROPERTY() TObjectPtr<UTextBlock> AirdropText;
    UPROPERTY() TObjectPtr<UTextBlock> LeaderText;
    UPROPERTY() TObjectPtr<UBorder> KillFeedPanel;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> KillFeedRows;
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
    UPROPERTY() TObjectPtr<UBorder> MapColumn;
    UPROPERTY() TObjectPtr<USizeBox> MapFrame;
    UPROPERTY() TObjectPtr<UTextBlock> SightText;
    UPROPERTY() TObjectPtr<UBorder> Band;
    UPROPERTY() TObjectPtr<UBorder> Portrait;
    UPROPERTY() TObjectPtr<UTextBlock> PortraitGlyph;
    UPROPERTY() TObjectPtr<UTextBlock> LevelBadgeText;
    UPROPERTY() TObjectPtr<UTextBlock> SelfName;
    UPROPERTY() TObjectPtr<UTextBlock> LifeText;
    UPROPERTY() TObjectPtr<UTextBlock> SelfStatusText;
    UPROPERTY() TObjectPtr<UTextBlock> PassiveText;
    UPROPERTY() TObjectPtr<UBorder> ArmedPanel;
    UPROPERTY() TObjectPtr<UTextBlock> ArmedText;
    UPROPERTY() TObjectPtr<UProgressBar> HealthBar;
    UPROPERTY() TObjectPtr<UTextBlock> HealthText;
    UPROPERTY() TObjectPtr<UProgressBar> ManaBar;
    UPROPERTY() TObjectPtr<UTextBlock> ManaText;
    UPROPERTY() TObjectPtr<UProgressBar> LevelBar;
    UPROPERTY() TObjectPtr<UTextBlock> LevelText;
    UPROPERTY() TObjectPtr<UTextBlock> GoldText;
    UPROPERTY() TObjectPtr<UTextBlock> CameraText;
    UPROPERTY() TArray<FLDHUDActionSlot> ActionSlots;
    TArray<FString> ActionTips;
    UPROPERTY() TMap<FName,TObjectPtr<ULDCommandButton>> Commands;
    // Pending-choice pill over the Q/W group: it pulses for a few seconds after the count rises, then stays steady.
    UPROPERTY() TObjectPtr<UBorder> PendingPrompt;
    UPROPERTY() TObjectPtr<UTextBlock> PromptText;
    int32 PromptCount=0;
    double PromptPulseAt=-1;
    float AppliedPromptWave=-1;
    UPROPERTY() TObjectPtr<UBorder> ToastPanel;
    UPROPERTY() TObjectPtr<UTextBlock> Toast;
    UPROPERTY() TArray<TObjectPtr<UBorder>> HitPanels;
    double FeedbackAt=0;
    // The toast shows controller intent feedback and local panel notices; SeenIntent tracks only the former.
    FString LastFeedback;
    FString SeenIntent;
    uint32 SeenFeedback=0;
    void ShowToast(const FString& Text);
    uint32 SeenCombatReceipt=0;
    uint64 ToldProtectionLifeRevision=0;
    // The latest Opened result of the player's current shop interaction. A remote client usually receives it before the
    // owner projection that carries the session, so it is shown until the next growth publication arrives.
    FLDShopOpenResult OpenedShop;
    uint64 OpenedShopPublish=0;
    bool bOpenedShop=false;
    // The match generation whose opening three-choice was offered its one automatic panel opening (D10).
    uint32 AutoChoicesMatch=0;
    uint32 AutoChoicesPending=0;
    double AutoChoicesSince=-1;
    // Own-body hits (the same 面子 and 淡定 losses the world feedback floats), which fold the three-choice panel.
    LDWorldFeedback::FTracker OwnHitWatch;
    void HandleGrowthSubmit(const FLDSubmitResult& Result);
    void HandleChoiceReceipt(const FLDChoiceReceiptQueryResult& Result);
    void HandleShopReceipt(const FLDShopReceiptQueryResult& Result);
    void HandleShopOpen(const FLDShopOpenResult& Result);
    // Kill notices already shown, by public serial within one match generation; each fades on local time only.
    uint32 KillGeneration=0;
    uint64 KillSerial=0;
    TArray<FLDHUDKillLine> KillLines;
    // Authority time at which the current life was first seen alive; the death banner takes its cause only from notices
    // at or after it, so a previous death never stands in while this one's notice is still on its way.
    uint32 LifeSeenMatch=0;
    uint32 LifeSeenLife=0;
    int64 LifeSeenAliveMs=0;
    FGuid ShownResultId;
    uint64 ShownResultSerial=0;
    float AppliedScale=0;
    float MapAspect=0;
    // Approved public definitions used only for display labels; loaded once per widget.
    TSharedPtr<const FLDDemoContentSnapshot> DisplayContent;
    bool bDisplayContentTried=false;
    const FLDDemoContentDefinitions* DisplayDefinitions();
    void RefreshKillFeed(const class ALDGameState& Public,const FGuid& Self,const FLDDemoContentDefinitions* Content,bool bClock,int64 NowMs);
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
