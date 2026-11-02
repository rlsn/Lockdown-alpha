#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
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
class FLDDemoContentSnapshot;
struct FLDDemoContentDefinitions;

// One Q/W/1/2 entry. It stays a non-interactive indicator until the Combat activation entry is wired:
// the slot must never look clickable without submitting a real intent.
USTRUCT()
struct FLDHUDActionSlot
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<UBorder> Frame;
    UPROPERTY() TObjectPtr<UTextBlock> Key;
    UPROPERTY() TObjectPtr<UTextBlock> Name;
    UPROPERTY() TObjectPtr<UTextBlock> State;
    UPROPERTY() TObjectPtr<UProgressBar> Cooldown;
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
    UPROPERTY() TObjectPtr<UProgressBar> CircleProgress;
    UPROPERTY() TObjectPtr<UBorder> KillFeedPanel;
    UPROPERTY() TObjectPtr<UTextBlock> KillFeedText;
    UPROPERTY() TObjectPtr<UBorder> ResultPanel;
    UPROPERTY() TObjectPtr<UTextBlock> ResultTitle;
    UPROPERTY() TObjectPtr<UTextBlock> ResultSubtitle;
    UPROPERTY() TObjectPtr<class UVerticalBox> ResultRows;
    UPROPERTY() TObjectPtr<ULDCommandButton> ReturnButton;
    UPROPERTY() TObjectPtr<UBorder> DeathBanner;
    UPROPERTY() TObjectPtr<UTextBlock> DeathTitle;
    UPROPERTY() TObjectPtr<UTextBlock> DeathCountdown;
    UPROPERTY() TObjectPtr<UTextBlock> DeathNote;
    UPROPERTY() TObjectPtr<UBorder> MapColumn;
    UPROPERTY() TObjectPtr<USizeBox> MapFrame;
    UPROPERTY() TObjectPtr<UTextBlock> SightText;
    UPROPERTY() TObjectPtr<UBorder> Band;
    UPROPERTY() TObjectPtr<UBorder> Portrait;
    UPROPERTY() TObjectPtr<UTextBlock> PortraitGlyph;
    UPROPERTY() TObjectPtr<UTextBlock> SelfName;
    UPROPERTY() TObjectPtr<UTextBlock> LifeText;
    UPROPERTY() TObjectPtr<UProgressBar> HealthBar;
    UPROPERTY() TObjectPtr<UTextBlock> HealthText;
    UPROPERTY() TObjectPtr<UProgressBar> ManaBar;
    UPROPERTY() TObjectPtr<UTextBlock> ManaText;
    UPROPERTY() TObjectPtr<UProgressBar> LevelBar;
    UPROPERTY() TObjectPtr<UTextBlock> LevelText;
    UPROPERTY() TObjectPtr<UTextBlock> GoldText;
    UPROPERTY() TObjectPtr<UTextBlock> CameraText;
    UPROPERTY() TArray<FLDHUDActionSlot> ActionSlots;
    UPROPERTY() TMap<FName,TObjectPtr<ULDCommandButton>> Commands;
    UPROPERTY() TObjectPtr<UBorder> ToastPanel;
    UPROPERTY() TObjectPtr<UTextBlock> Toast;
    UPROPERTY() TArray<TObjectPtr<UBorder>> HitPanels;
    double FeedbackAt=0;
    // The toast shows controller intent feedback and local panel notices; SeenIntent tracks only the former.
    FString LastFeedback;
    FString SeenIntent;
    uint32 SeenFeedback=0;
    void ShowToast(const FString& Text);
    // Kill notices already shown, by public serial within one match generation; each fades on local time only.
    uint32 KillGeneration=0;
    uint64 KillSerial=0;
    TArray<TPair<double,FString>> KillLines;
    FGuid ShownResultId;
    uint64 ShownResultSerial=0;
    float AppliedScale=0;
    float MapAspect=0;
    // Approved public definitions used only for display labels; loaded once per widget.
    TSharedPtr<const FLDDemoContentSnapshot> DisplayContent;
    bool bDisplayContentTried=false;
    const FLDDemoContentDefinitions* DisplayDefinitions();
    void RefreshKillFeed(const class ALDGameState& Public,const FLDDemoContentDefinitions* Content,bool bClock,int64 NowMs);
    void RefreshResult(class ALDPlayerController& PC,const class ALDGameState& Public);
    bool IsUIAt(FVector2D Absolute) const;
    TArray<ULDPanel*,TInlineAllocator<3>> MainPanels() const;
};
