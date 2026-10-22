#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LDUIRoot.generated.h"

class UBorder;
class UTextBlock;
class UEditableTextBox;
class UProgressBar;
UCLASS()
class LOCKDOWN_API ULDUIRoot : public UUserWidget
{
    GENERATED_BODY()
public:
    void ToggleMenu();
    bool PointerOverUI() const;
    void DismissMenu();
    bool IsMenuOpen() const {return bMenuOpen;}
    void ReturnToMenu(){Leave();}
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
private:
    bool bMenuOpen = false;
    UPROPERTY() TObjectPtr<UBorder> MenuPanel;
    UPROPERTY() TObjectPtr<UBorder> HeaderPanel;
    UPROPERTY() TObjectPtr<UBorder> ActionPanel;
    UPROPERTY() TObjectPtr<UTextBlock> ControlsInfo;
    UPROPERTY() TObjectPtr<UTextBlock> SkillBar;
    UPROPERTY() TObjectPtr<UTextBlock> MenuHelp;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UEditableTextBox> Address;
    UPROPERTY() TObjectPtr<UTextBlock> ObjectiveInfo;
    UPROPERTY() TObjectPtr<UProgressBar> ChannelProgress;
    UPROPERTY() TObjectPtr<UBorder> FixturePanel;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> DefenceLabels;
    UPROPERTY() TObjectPtr<UBorder> LobbyPanel;
    UPROPERTY() TObjectPtr<UTextBlock> LobbyInfo;
    UPROPERTY() TObjectPtr<class UButton> LobbyStart;
    bool bInLobby = false;
    UFUNCTION() void Laborer();
    UFUNCTION() void Rogue();
    UFUNCTION() void Scholar();
    UFUNCTION() void Ready();
    UFUNCTION() void StartMatch();
    UFUNCTION() void Host();
    UFUNCTION() void Join();
    UFUNCTION() void Resume();
    UFUNCTION() void Leave();
    UFUNCTION() void Exit();
    UFUNCTION() void ToggleSensor();
    UFUNCTION() void Fixtures();
    UFUNCTION() void FixtureBack();
    UFUNCTION() void SeedSeal();
    UFUNCTION() void DefenceWest();
    UFUNCTION() void DefenceEast();
    UFUNCTION() void DefenceSouth();
    UFUNCTION() void DefenceCentral();
};
