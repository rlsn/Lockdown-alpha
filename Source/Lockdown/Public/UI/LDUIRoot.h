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
class FLDDemoContentSnapshot;
struct FLDDemoContentDefinitions;

// Local navigation only (VISION_AND_UI navigation; MOVEMENT_INPUT_AND_CAMERA input ownership): one main panel at a time, each with at most one child confirmation.
UENUM()
enum class ELDUIMainPanel : uint8 { None, Menu, Settings, Choices, DevTest };
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
    // The attempt Notice reports, so the start page shows it on that card; None shows it under the page.
    ELDFrontendAttempt NoticeFor = ELDFrontendAttempt::None;
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
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
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
    // The title screen keeps the picture's people clear: the logo in the sky, the buttons on the ground, the version and the
    // slang note in the bottom corners; the page panel only frames the start page.
    UPROPERTY() TObjectPtr<UWidget> FrontendLogo;
    UPROPERTY() TArray<TObjectPtr<UWidget>> FrontendCorners;
    UPROPERTY() TObjectPtr<UWidget> FrontendBackground;
    UPROPERTY() TObjectPtr<class UBorder> FrontendPanel;
    // The title's low fog layer and the logo's neon glow copies, animated in TickFrontend (interface motion only).
    UPROPERTY() TObjectPtr<UWidget> FrontendFog;
    UPROPERTY() TArray<TObjectPtr<UWidget>> FrontendNeon;
    double FrontendShownAt=-1.;
    UPROPERTY() TObjectPtr<UEditableTextBox> NameBox;
    UPROPERTY() TObjectPtr<UEditableTextBox> AddressBox;
    UPROPERTY() TObjectPtr<UEditableTextBox> PortBox;
    UPROPERTY() TObjectPtr<ULDCommandButton> StartButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> HostButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> JoinButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> CancelJoinButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> StartBackButton;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    // The start page's two cards: each shows its own attempt's notice and, while that attempt runs, a turning ring.
    UPROPERTY() TObjectPtr<UTextBlock> HostFeedback;
    UPROPERTY() TObjectPtr<UTextBlock> JoinFeedback;
    UPROPERTY() TObjectPtr<UWidget> HostRing;
    UPROPERTY() TObjectPtr<UWidget> JoinRing;
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
    // Breathing glow behind the host's start button while the room can start.
    UPROPERTY() TObjectPtr<UWidget> StartMatchGlow;
    UPROPERTY() TObjectPtr<UTextBlock> LobbyBlocker;
    UPROPERTY() TObjectPtr<UTextBlock> LobbyFeedback;
    UPROPERTY() TObjectPtr<USizeBox> MenuPage;
    // Spec §6.5 连锁图鉴: the main menu's page of the local codex (a menu page, so its list may scroll).
    UPROPERTY() TObjectPtr<USizeBox> CodexPage;
    UPROPERTY() TObjectPtr<UTextBlock> CodexCount;
    UPROPERTY() TObjectPtr<UVerticalBox> CodexRows;
    UPROPERTY() TObjectPtr<ULDCommandButton> CodexBack;
    bool bCodexOpen = false;
    // 怎么玩: the main menu's page of six short cards (one title and one line each), paged with 上一张/下一张. It opens by
    // itself the first time this computer shows the main menu (a local flag) and from its button afterwards.
    UPROPERTY() TObjectPtr<USizeBox> HowToPage;
    UPROPERTY() TObjectPtr<UTextBlock> HowToTitle;
    UPROPERTY() TObjectPtr<UTextBlock> HowToBody;
    UPROPERTY() TObjectPtr<UTextBlock> HowToCount;
    UPROPERTY() TObjectPtr<ULDCommandButton> HowToBack;
    bool bHowToOpen = false;
    int32 HowToIndex = 0;
    UPROPERTY() TObjectPtr<UTextBlock> MenuPhase;
    UPROPERTY() TObjectPtr<UTextBlock> MenuPause;
    UPROPERTY() TObjectPtr<UTextBlock> MenuFeedback;
    UPROPERTY() TObjectPtr<UBorder> ConfirmLayer;
    UPROPERTY() TObjectPtr<UTextBlock> ConfirmText;
    // Indexed by ELDRootConfirm: the button row of each confirmation and its safe (cancel) focus target.
    UPROPERTY() TArray<TObjectPtr<UWidget>> ConfirmRows;
    UPROPERTY() TArray<TObjectPtr<UWidget>> ConfirmCancels;
    UPROPERTY() TArray<TObjectPtr<USizeBox>> Pages;
    TWeakObjectPtr<ULDPanel> MainPanels[2];
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
    FGuid SeenLobbyResult;
    uint64 SeenLobbyResultSerial = 0;
    TSharedPtr<const FLDDemoContentSnapshot> ResultContent;
    bool bResultContentTried = false;
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
    const FLDDemoContentDefinitions* ResultDefinitions();
    void SetFrontendPage(bool bStart);
    void ShowStartPage(bool bShow);
    bool CloseStartPage();
    void OpenCodex();
    bool CloseCodex();
    void OpenHowTo();
    bool CloseHowTo();
    void ShowHowTo(int32 Index);
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
