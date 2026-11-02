#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LDUIRoot.generated.h"

class UBorder;
class UTextBlock;
class UEditableTextBox;
UCLASS()
class LOCKDOWN_API ULDUIRoot : public UUserWidget
{
    GENERATED_BODY()
public:
    void ToggleMenu();
    bool PointerOverUI() const;
    void DismissMenu();
    bool IsMenuOpen() const {return bMenuOpen;}
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    bool bMenuOpen = false;
    UPROPERTY() TObjectPtr<UBorder> MenuPanel;
    UPROPERTY() TObjectPtr<UBorder> HeaderPanel;
    UPROPERTY() TObjectPtr<UTextBlock> MenuHelp;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UEditableTextBox> Address;
    UPROPERTY() TObjectPtr<UBorder> LobbyPanel;
    UPROPERTY() TObjectPtr<UTextBlock> LobbyInfo;
    UPROPERTY() TObjectPtr<class UVerticalBox> AIControls;
    UPROPERTY() TObjectPtr<class UButton> LobbyStart;
    bool bInLobby = false;
    UFUNCTION() void Ready();
    UFUNCTION() void StartMatch();
    UFUNCTION() void Host();
    UFUNCTION() void Join();
    UFUNCTION() void Resume();
    UFUNCTION() void Leave();
    UFUNCTION() void Exit();
};
