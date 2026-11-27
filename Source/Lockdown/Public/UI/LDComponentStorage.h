#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDProgressionRules.h"
#include "UI/LDHUDModel.h"
#include "LDComponentStorage.generated.h"
struct FLDDemoContentDefinitions;

// The card storage's command layer behind the build ring (APP_SPEC §7; ULDWorkshop draws the library and the installed
// cards): the install, uninstall, move and replace commands of the owned cards, each keyed by LDHUD::CardKey (its definition
// numbered by its serial). Y25 asks for an element first (the ring shows the chooser while IsChoosing). One request is in
// flight at a time; a move sends its install half once the own view shows the uninstall. Local state over the own
// progression view; the server decides every command and every result or refusal reaches the HUD's toast (Notify).
UCLASS()
class LOCKDOWN_API ULDComponentStorage : public UObject
{
    GENERATED_BODY()
public:
    // Called on every HUD refresh. bShown: the gameplay HUD is shown for the own match (a hidden HUD closes the chooser).
    void Present(const FLDProgressionView* View,uint32 Match,LDProgression::EMatchStage Stage,ELDLifePhase LifePhase,
        const FLDDemoContentDefinitions* Content,bool bShown);
    bool HasPendingRequest() const {return PendingRequest!=0||Follow.State!=EFollow::None;}
    // Back: closes the chooser. False when it was not open.
    bool CancelInteraction();
    void HandleSubmit(const FLDSubmitResult& Result);
    void HandleReceipt(const FLDBuildReceiptQueryResult& Result);
    TFunction<uint64(const FLDBuildCommand&,ELDGrowthReason&)> SubmitBuild;
    TFunction<uint64()> PeekRequestId;
    TFunction<void(const FString&)> Notify;
    // The card Component put on Column (on the installed card Host there, or None), or back in the storage. Returns whether a
    // command was sent or the chooser opened.
    bool DropOn(FName Component,LDHUD::EStorageColumn Column,FName Host);
    bool DropInStorage(FName Component);
    // The open chooser's option at Index (an element of Y25), which sends the command.
    bool ChooseOption(int32 Index);
    bool IsChoosing() const {return Chooser.bOpen;}
    // The open chooser's elements, in option order (empty when it is closed).
    TArray<ELDDemoElement> ChooserElements() const {return Chooser.bOpen?Chooser.Drop.Elements:TArray<ELDDemoElement>();}
    // A card preview marks this card (and every copy it stands for) read for the current match.
    void MarkSeen(FName Component);
    const LDHUD::FStorageView& Model() const {return LatestModel;}
private:
    enum class EFollow : uint8 { None, AwaitUninstall, AwaitView };
    struct FChooser
    {
        bool bOpen=false;
        FName Component;
        LDHUD::FCardDrop Drop;
    };
    // A move's install half, sent once the uninstall is applied and the own view carries its build revision.
    struct FFollowUp
    {
        EFollow State=EFollow::None;
        FName Component;
        FLDBuildCommand Install;
        uint64 AfterRevision=0;
        double Since=0;
    };
    FLDProgressionView Latest;
    LDHUD::FStorageView LatestModel;
    LDHUD::FBuildGateView Gate;
    const FLDDemoContentDefinitions* LatestContent=nullptr;
    // The owned cards of the last view, so a newly received card plays its cue once; a new match only takes its first view.
    TSet<FName> KnownOwned;
    uint32 KnownMatch=0;
    uint32 LatestMatch=0;
    TSet<FName> SeenComponents;
    // The install in flight merges an element medal into a copy already in place (its cue).
    bool bPendingMedalFuse=false;
    FChooser Chooser;
    FFollowUp Follow;
    uint64 PendingRequest=0;
    uint32 Answers=0;

    double UiNow() const;
    FString ComponentName(FName Component) const;
    FString PlaceName(LDHUD::EStorageColumn Column,FName Host) const;
    void OpenChooser(FName Component,const LDHUD::FCardDrop& Drop);
    void CloseChooser();
    bool Commit(FName Component,const LDHUD::FCardDrop& Drop,ELDDemoElement Element);
    bool Send(FLDBuildCommand Command);
    void ShowFinal(const FLDBuildReceipt& Receipt);
    void FinishRequest(const FString& Text,bool bRefused=false);
};
