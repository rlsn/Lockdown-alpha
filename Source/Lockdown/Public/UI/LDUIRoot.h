#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/SlateEnums.h"
#include "LDUIRoot.generated.h"

class UBorder;
class UCanvasPanel;
class UEditableTextBox;
class UHorizontalBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class ULDCommandButton;
class ULDPanel;
class ULDWorldLabels;
class ULDWorldFeedback;
class ALDPlayerController;
class ALDGameState;

// Local navigation only (UI_AND_SETTINGS 3.1/4): one main panel at a time, each with at most one child confirmation.
UENUM()
enum class ELDUIMainPanel : uint8 { None, Menu, Settings, Attributes, Choices, DevTest };
enum class ELDUIInputLayer : uint8 { None, TextEditing, ChildConfirmation, MainPanel };
enum class ELDFrontendAttempt : uint8 { None, Host, Join };
enum class ELDRootConfirm : uint8 { None, Exit, LeaveRoom };

// UE reloads the frontend map after a failed join or host, so the typed endpoint, the open page and the attempt
// outcome must outlive the root widget. Held by the GameInstance for this process only; never written to disk.
UCLASS()
class ULDStartPageDraft : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    FString Address, Port, Endpoint, Notice, SeenConnection;
    ELDFrontendAttempt Attempt = ELDFrontendAttempt::None;
    bool bStartPage = false;
};

UCLASS()
class LOCKDOWN_API ULDUIRoot : public UUserWidget
{
    GENERATED_BODY()
public:
    // Highest active local layer. Back consumes exactly one layer per press:
    // CloseTopModal -> cancel Armed -> CloseTopPanel -> ToggleMainPanel(Menu).
    ELDUIInputLayer TopInputLayer() const;
    // Ends text editing, else closes the topmost child confirmation (the root's own, else the open main panel's).
    // True when that layer consumed Back, including a panel prompt that must be answered explicitly.
    bool CloseTopModal();
    // The open main panel's child confirmation when it waits without capturing input (ULDPanel::ChildConfirmationYields) and
    // nothing above it (text editing, the root's own confirmation) holds input; null otherwise.
    ULDPanel* YieldingConfirmation() const;
    // Closes the open main panel; the frontend Start page first cancels its in-flight join.
    // True also when the panel kept itself open by showing its own confirmation.
    bool CloseTopPanel();
    ELDUIMainPanel ActiveMainPanel() const;
    // One main panel at a time: closes the others first and toggles the active one closed.
    void ToggleMainPanel(ELDUIMainPanel Panel);
    // Opens a registered panel without toggling (such as the growth panel's automatic open for a new offer or new points).
    // False when it is not registered, the root confirmation is up, or another panel keeps itself open.
    bool ShowMainPanel(ELDUIMainPanel Panel);
    // HUD-created Attributes/Choices/DevTest panels, held weakly; nullptr unregisters.
    void RegisterMainPanel(ELDUIMainPanel Panel, ULDPanel* Widget);
    // The single leave path (lobby, in-match menu and the HUD result screen): back to the frontend.
    void LeaveRoom();
    void ToggleMenu();
    bool PointerOverUI() const;
    void DismissMenu();
    bool IsMenuOpen() const {return bMenuOpen;}
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    // Overhead unit names: the bottom layer of the world canvas, shown exactly while the in-match HUD is.
    UPROPERTY() TObjectPtr<ULDWorldLabels> WorldLabels;
    // Floating numbers, own rewards and level-up/kill confirmations over the bodies; above the names, shown like them.
    UPROPERTY() TObjectPtr<ULDWorldFeedback> WorldFeedback;
    UPROPERTY() TObjectPtr<UBorder> HeaderPanel;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<USizeBox> FrontendPage;
    UPROPERTY() TObjectPtr<UVerticalBox> MainMenu;
    UPROPERTY() TObjectPtr<UVerticalBox> StartPage;
    UPROPERTY() TObjectPtr<UEditableTextBox> NameBox;
    UPROPERTY() TObjectPtr<UEditableTextBox> AddressBox;
    UPROPERTY() TObjectPtr<UEditableTextBox> PortBox;
    UPROPERTY() TObjectPtr<ULDCommandButton> StartButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> HostButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> JoinButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> CancelJoinButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> StartBackButton;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UTextBlock> SettingsStatus;
    UPROPERTY() TObjectPtr<USizeBox> LobbyPage;
    // Full-screen picture behind the lobby page (LDUIBackgrounds), shown exactly while the lobby is; null when missing.
    UPROPERTY() TObjectPtr<UWidget> LobbyBackground;
    UPROPERTY() TObjectPtr<UVerticalBox> RosterRows;
    UPROPERTY() TObjectPtr<UHorizontalBox> AIButtonRow;
    UPROPERTY() TArray<TObjectPtr<ULDCommandButton>> AIButtons;
    UPROPERTY() TObjectPtr<UTextBlock> AIReadOnly;
    UPROPERTY() TObjectPtr<UTextBlock> LobbyDevMarker;
    UPROPERTY() TObjectPtr<ULDCommandButton> ReadyButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> StartMatchButton;
    UPROPERTY() TObjectPtr<UTextBlock> LobbyBlocker;
    UPROPERTY() TObjectPtr<UTextBlock> LobbyFeedback;
    UPROPERTY() TObjectPtr<USizeBox> MenuPage;
    UPROPERTY() TObjectPtr<UTextBlock> MenuPhase;
    UPROPERTY() TObjectPtr<UTextBlock> MenuPause;
    UPROPERTY() TObjectPtr<UTextBlock> MenuFeedback;
    UPROPERTY() TObjectPtr<UBorder> ConfirmLayer;
    UPROPERTY() TObjectPtr<UTextBlock> ConfirmText;
    // Indexed by ELDRootConfirm: the button row of each confirmation and its safe (cancel) focus target.
    UPROPERTY() TArray<TObjectPtr<UWidget>> ConfirmRows;
    UPROPERTY() TArray<TObjectPtr<UWidget>> ConfirmCancels;
    UPROPERTY() TArray<TObjectPtr<USizeBox>> Pages;
    TWeakObjectPtr<ULDPanel> MainPanels[3];
    ELDRootConfirm Confirm = ELDRootConfirm::None;
    ELDFrontendAttempt AppliedAttempt = ELDFrontendAttempt::None;
    bool bMenuOpen = false;
    bool bInLobby = false;
    bool bInResults = false;
    bool bStartPage = false;
    bool bFrontendHidden = false;
    bool bSettingsWasOpen = false;
    bool bListenPortValid = false;
    bool bRosterBuilt = false;
    bool bSeenHost = false;
    uint32 SeenLobbyGeneration = 0;
    uint64 SeenLobbyRoster = 0;
    uint64 SeenLobbyPublic = 0;
    FGuid SeenSelf;
    float AppliedTextScale = 1;
    float AppliedPageHeight = 0;
    UFUNCTION() void Ready();
    UFUNCTION() void StartMatch();
    UFUNCTION() void Host();
    UFUNCTION() void Join();
    UFUNCTION() void Resume();
    UFUNCTION() void Leave();
    UFUNCTION() void Exit();
    UFUNCTION() void TextCommitted(const FText& Text, ETextCommit::Type Method);
    void CancelJoin();
    void BuildFrontend(ALDPlayerController& PC, UCanvasPanel& Canvas);
    void BuildWorld(UCanvasPanel& Canvas);
    void BuildConfirm(UCanvasPanel& Canvas, bool bFrontend);
    void TickFrontend(ALDPlayerController& PC);
    void TickWorld(ALDPlayerController& PC);
    void RefreshLobby(const ALDPlayerController& PC, const ALDGameState& Public);
    void RebuildRoster(const ALDGameState& Public, const FGuid& Self);
    void SetFrontendPage(bool bStart);
    void ShowStartPage(bool bShow);
    bool CloseStartPage();
    void UpdateStartControls();
    void RestoreFrontendPage(ALDPlayerController& PC);
    void OpenMenu();
    void CloseMenu();
    void OpenConfirm(ELDRootConfirm Kind);
    void CloseConfirm();
    bool CloseMainPanelsExcept(ELDUIMainPanel Keep);
    bool ApplyPlayerName(ALDPlayerController& PC, FString& Name);
    bool TravelInFlight() const;
    ULDPanel* OpenMainPanel() const;
    UUserWidget* TextEditingOwner() const;
    bool EndTextEditing();
    ULDStartPageDraft* StartDraft() const;
    FString LocalSettingsProblem(const ALDPlayerController& PC) const;
    int32 Scaled(int32 Size) const;
    void ApplyTextScale(float Scale);
    UTextBlock* Label(const FString& Value, int32 Size, const FLinearColor& Color = FLinearColor(.9f, .94f, .96f));
    ULDCommandButton* MakeButton(const FString& Text, FName Name, TFunction<void()> Action, int32 Size = 18);
    UEditableTextBox* MakeTextBox(const FString& Hint, FName Name);
    USizeBox* MakePage(UCanvasPanel& Canvas, float Width, UVerticalBox*& Content);
    // A picture filling the whole viewport (cropped, never stretched) under every page of this root; null with
    // LD_UI_BACKGROUND_MISSING when the texture is not there.
    UWidget* AddBackground(UCanvasPanel& Canvas, const TCHAR* AssetPath);
};