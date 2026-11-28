#include "Probes/LDDemoGrowthLinkProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/LDCharacter.h"
#include "Combat/LDAutoEmpower.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatTypes.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevGrowthFixture.h"
#include "Development/LDDevTestProtocol.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDGrowthProtocol.h"
#include "Dom/JsonObject.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Framework/LDCaptureTypes.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Navigation/LDNavigationComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/LDHUD.h"
#include "UObject/UnrealType.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDWorldViewRelay.h"

// Short owned-connection growth scenario (docs/systems/VERIFICATION.md). Two roles, each driving only its own owning
// connection: Client1 sends a DEV XP grant, two production build commands and one attack order; the Host skips the
// opening rest, stands next to the client as the target and compares the authority's record with what the client saw.
// Every check is an invariant ("arrived", "rose", "equal on both ends"). No expected number is recomputed here: exact
// values belong to the rule tests. Named, not anonymous: unity builds merge files.
namespace LDDemoGrowthLink
{
// Bounded waits in seconds: one request's terminal answer and the owner publication it implies; the walk-free kill.
constexpr double RequestSeconds = 20.0;
constexpr double KillSeconds = 75.0;
// The room reaching the field after Start, the opening rest included when the skip is refused.
constexpr double FieldSeconds = 90.0;
// AttackUnit orders allowed when the server ended a lock without a death (return protection, lost sight).
constexpr int32 AttackOrders = 8;
// Where the Host asks to stand: this far from the client's body, tried at eight bearings.
constexpr double TargetDistanceCm = 300.0;
constexpr double TargetDropCm = 150.0;
constexpr int32 TargetBearings = 8;
// Every probe allocation goes to this stat, so it shows as a larger maximum HP (a probe choice, not a product rule).
const TCHAR* const PointStat = TEXT("stat_physique");

enum class EWait : uint8 { Waiting, Ready, Failed };

const FLDObservedUnitView* SeenContender(const ALDPlayerController& Viewer, const FGuid& ContenderId)
{
    const ALDWorldViewRelay* Relay = Viewer.LocalView();
    if (!Relay) { return nullptr; }
    const FLDObservedFrameRead Read = Relay->ReadObservedFrame();
    if (Read.Status != ELDObservedViewStatus::Current || !Read.Frame) { return nullptr; }
    return Read.Frame->Units.FindByPredicate([&ContenderId](const FLDObservedUnitView& Unit)
        { return Unit.Kind == ELDObservedUnitKind::Contender && Unit.ContenderId == ContenderId; });
}

int32 PointsOf(const FLDProgressionView& View, FName StatId)
{
    const FLDStatPointsView* Row = View.StatPoints.FindByPredicate([StatId](const FLDStatPointsView& Candidate)
        { return Candidate.StatId == StatId; });
    return Row ? Row->Points : -1;
}

// The rendered HUD's level badge, read through reflection without changing the HUD for the probe.
FString HudLevelText(const ALDPlayerController& PC)
{
    const ULDHUD* Hud = PC.HUDUI;
    const FObjectProperty* Property = Hud ? FindFProperty<FObjectProperty>(ULDHUD::StaticClass(), TEXT("LevelBadgeText")) : nullptr;
    const UTextBlock* Text = Property ? Cast<UTextBlock>(Property->GetObjectPropertyValue_InContainer(Hud)) : nullptr;
    return Text ? Text->GetText().ToString() : FString();
}
}

class FLDDemoGrowthLinkProbe : public TSharedFromThis<FLDDemoGrowthLinkProbe>
{
public:
    using EWait = LDDemoGrowthLink::EWait;
    enum class EStage : uint8
    {
        Join, Lobby, EnterField,
        // Host
        HostGrant, HostHud, HostAwaitBuilt, HostStand, HostAwaitDeath, HostAudit,
        // Client1
        ClientGrant, ClientChoice, ClientPoints, ClientEmpower, ClientSeek, ClientKill, ClientReward,
    };

    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output, Role, Error;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TSharedRef<FJsonObject> Facts = MakeShared<FJsonObject>();
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    EStage Stage = EStage::Join;
    double Deadline = 0, StageAt = 0, NextCommandAt = 0;
    bool Passed = true, Written = false, bJoined = false, bSent = false, bSkipDone = false, bPaused = false;
    FGuid HostId, ClientId;

    // Owner answers in arrival order; the module's own handlers stay bound as well.
    TArray<FLDSubmitResult> Submits;
    TArray<FLDBuildReceiptQueryResult> BuildResults;
    TArray<FLDDevTestResult> DevResults;
    FDelegateHandle SubmitHandle, BuildHandle, DevHandle;

    // The request in flight.
    FString CallName;
    uint32 CallGeneration = 0;
    uint64 CallRequest = 0;
    int32 CallMark = 0, CallBuildMark = 0;
    double CallAt = 0;

    // What the owner view showed when the step's request was sent.
    FLDProgressionView Before;
    int64 MaxHPBeforeP2 = 0, GrantXP = 0, KillXPBefore = 0, ClientXPAfterKill = -1;
    FName Option;
    int32 Orders = 0, Bearing = 0, Skips = 0;
    FLDMoveIdentity AttackOrder;
    uint32 LifeBefore = 0;
    // Auto-empower (on only with -LDEmpower): the host command's steps, the sequence in flight, whether the client's own view
    // ever showed a ready bar, and the Host's count of the client's public ready marks and of marks without authorization.
    int32 EmpowerStep = 0;
    uint64 EmpowerSeq = 0;
    double EmpowerAt = 0;
    bool bEmpowerOn = false, bSawReady = false;
    int32 ReadyMarks = 0, UnauthorizedMarks = 0;

    bool IsHost() const { return Role == TEXT("Host"); }

    void SetStage(EStage Next)
    {
        Stage = Next;
        StageAt = FPlatformTime::Seconds();
        bSent = false;
    }

    bool Check(bool bValue, const FString& Name, const FString& Detail = FString())
    {
        const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Name);
        Row->SetBoolField(TEXT("passed"), bValue);
        Row->SetStringField(TEXT("detail"), Detail);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        Passed &= bValue;
        if (!bValue && Error.IsEmpty()) { Error = Name + (Detail.IsEmpty() ? FString() : TEXT(": ") + Detail); }
        return bValue;
    }

    bool Fail(const FString& Why, const FString& Detail = FString())
    {
        Check(false, Why, Detail);
        return Finish();
    }

    bool Save(const TSharedRef<FJsonObject>& Value, const FString& Name) const
    {
        FString Text;
        const FString File = FPaths::Combine(Output, Name);
        return FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Text))
            && FFileHelper::SaveStringToFile(Text, *(File + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*File, *(File + TEXT(".tmp")), true, true);
    }

    bool Mark(const FString& Name, const TSharedRef<FJsonObject>& Value = MakeShared<FJsonObject>())
    {
        return Save(Value, Name) || Check(false, TEXT("The scenario could not persist its marker ") + Name);
    }

    bool Exists(const FString& Name) const { return FPaths::FileExists(FPaths::Combine(Output, Name)); }

    static FString ViewText(const FLDProgressionView& View)
    {
        return FString::Printf(TEXT("level=%u xp=%lld opportunities=%u/%u offer=%d unspent=%lld actives=%d"), uint32(View.Level),
            View.CumulativeXP, uint32(View.ActiveOpportunitiesConsumed), uint32(View.ActiveOpportunitiesEarned),
            View.bHasActiveOffer ? 1 : 0, View.UnspentPoints, View.Actives.Num());
    }

    void Bind(ALDPlayerController& Subject)
    {
        const TWeakPtr<FLDDemoGrowthLinkProbe> Weak = AsShared();
        SubmitHandle = Subject.OnGrowthSubmit.AddLambda([Weak](const FLDSubmitResult& Received)
            { if (const auto Pinned = Weak.Pin()) { Pinned->Submits.Add(Received); } });
        BuildHandle = Subject.OnBuildReceipt.AddLambda([Weak](const FLDBuildReceiptQueryResult& Received)
            { if (const auto Pinned = Weak.Pin()) { Pinned->BuildResults.Add(Received); } });
        DevHandle = Subject.OnDevTestResult.AddLambda([Weak](const FLDDevTestResult& Received)
            { if (const auto Pinned = Weak.Pin()) { Pinned->DevResults.Add(Received); } });
    }

    void Unbind()
    {
        ALDPlayerController* Subject = Controller.Get();
        if (!Subject) { return; }
        Subject->OnGrowthSubmit.Remove(SubmitHandle);
        Subject->OnBuildReceipt.Remove(BuildHandle);
        Subject->OnDevTestResult.Remove(DevHandle);
    }

    // ---- DEV test commands, only through SubmitDevTest on the own owning connection ----

    bool SendDev(ALDPlayerController& PC, const FString& Name, ELDDevTestOp Op, int64 Amount,
        const FVector& Position = FVector::ZeroVector)
    {
        CallName = Name;
        CallMark = DevResults.Num();
        CallAt = FPlatformTime::Seconds();
        CallGeneration = PC.OwnerState.Match;
        ELDGrowthReason Local = ELDGrowthReason::None;
        CallRequest = PC.SubmitDevTest(Op, Amount, Local, NAME_None, 0, 0, Position);
        bSent = true;
        return Check(CallRequest != 0, Name + TEXT(": sent through SubmitDevTest on the owning connection"),
            LDGrowthWire::ReasonName(Local));
    }

    // The terminal answer to the DEV request in flight; bApplied only for a final receipt with outcome Applied.
    EWait AwaitDev(double Now, bool& bApplied, FString& Detail) const
    {
        for (int32 Index = CallMark; Index < DevResults.Num(); ++Index)
        {
            const FLDDevTestResult& Answer = DevResults[Index];
            if (Answer.Generation != CallGeneration || Answer.RequestId != CallRequest
                || (Answer.Status != ELDAdmissionStatus::Final && Answer.Status != ELDAdmissionStatus::AdmissionRejected))
            { continue; }
            bApplied = Answer.Status == ELDAdmissionStatus::Final && Answer.bHasReceipt
                && Answer.Receipt.Header.Outcome == ELDReceiptOutcome::Applied && Answer.Receipt.Header.RequestId == CallRequest;
            Detail = FString::Printf(TEXT("status %u reason %s: %s"), uint32(Answer.Status),
                LDGrowthWire::ReasonName(LDGrowthWire::Reason(Answer.bHasReceipt ? Answer.Receipt.Header.Reason : Answer.Reason)),
                Answer.bHasReceipt ? *Answer.Receipt.Detail : *Answer.Detail);
            return EWait::Ready;
        }
        Detail = TEXT("no terminal answer arrived");
        return Now - CallAt <= LDDemoGrowthLink::RequestSeconds ? EWait::Waiting : EWait::Failed;
    }

    // ---- Production build commands, only through SubmitBuild on the own owning connection ----

    bool SendBuild(ALDPlayerController& PC, const FString& Name, FLDBuildCommand Command)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        CallName = Name;
        bSent = true;
        if (!Check(View != nullptr, Name + TEXT(": the owner has a growth view to build against"))) { return false; }
        Before = *View;
        MaxHPBeforeP2 = PC.CombatView.MaxHPP2;
        Command.ExpectedBuildRevision = View->BuildRevision;
        Command.CatalogDigest = View->CatalogDigest;
        Command.Generation = PC.OwnerState.Match;
        Command.Life = PC.OwnerState.Life;
        Command.ExpectedLifePhase = PC.OwnerState.LifePhase;
        CallMark = Submits.Num();
        CallBuildMark = BuildResults.Num();
        CallAt = FPlatformTime::Seconds();
        CallGeneration = Command.Generation;
        ELDGrowthReason Local = ELDGrowthReason::None;
        CallRequest = PC.SubmitBuild(Command, Local);
        return Check(CallRequest != 0 && Local == ELDGrowthReason::None,
            Name + TEXT(": sent through SubmitBuild on the owning connection"), LDGrowthWire::ReasonName(Local));
    }

    // The final receipt of the build request in flight, from its submit answer or its receipt delivery.
    EWait AwaitBuild(double Now, const FLDBuildReceipt*& Out, FString& Detail) const
    {
        Out = nullptr;
        for (int32 Index = CallMark; Index < Submits.Num(); ++Index)
        {
            const FLDSubmitResult& Answer = Submits[Index];
            if (Answer.Family != ELDGrowthFamily::Build || Answer.Generation != CallGeneration || Answer.RequestId != CallRequest)
            { continue; }
            if (Answer.Status == ELDAdmissionStatus::AdmissionRejected)
            {
                Detail = FString(TEXT("refused at admission: ")) + LDGrowthWire::ReasonName(LDGrowthWire::Reason(Answer.Reason));
                return EWait::Failed;
            }
            if (Answer.Status == ELDAdmissionStatus::Final && Answer.bHasBuildReceipt) { Out = &Answer.BuildReceipt; }
        }
        for (int32 Index = CallBuildMark; !Out && Index < BuildResults.Num(); ++Index)
        {
            const FLDBuildReceiptQueryResult& Answer = BuildResults[Index];
            if (Answer.Query.Family == ELDGrowthFamily::Build && Answer.Query.Generation == CallGeneration
                && Answer.Query.RequestId == CallRequest && Answer.Lookup == ELDReceiptLookup::Final && Answer.bHasReceipt)
            { Out = &Answer.Receipt; }
        }
        if (Out)
        {
            Detail = FString::Printf(TEXT("outcome %u reason %s"), uint32(Out->Header.Outcome),
                LDGrowthWire::ReasonName(LDGrowthWire::Reason(Out->Header.Reason)));
            return EWait::Ready;
        }
        Detail = TEXT("no final receipt arrived");
        return Now - CallAt <= LDDemoGrowthLink::RequestSeconds ? EWait::Waiting : EWait::Failed;
    }

    // The DEV XP amount that reaches the first level paying an active opportunity: a setup amount read from the approved
    // thresholds, so the scenario follows content changes. It is never an expected value of a check.
    int64 OfferLevelGrant(const FLDProgressionView& View) const
    {
        const auto& Progression = Catalog->Get().Progression;
        int64 Target = View.bHasNextThreshold ? View.NextThresholdXP : View.CumulativeXP + 1;
        for (const int32 Level : Progression.ActiveOfferLevels)
        {
            if (Level > int32(View.Level) && Progression.XPThresholds.IsValidIndex(Level - 1))
            {
                Target = Progression.XPThresholds[Level - 1];
                break;
            }
        }
        return FMath::Max<int64>(1, Target - View.CumulativeXP);
    }

    // ---- Shared stages ----

    bool Tick()
    {
        const double Now = FPlatformTime::Seconds();
        if (Written) { return TickAfterReport(); }
        if (Now >= Deadline)
        {
            return Fail(FString::Printf(TEXT("The scenario did not finish within its bounded deadline (stage %d)"), int32(Stage)));
        }
        ALDPlayerController* PC = Controller.Get();
        UWorld* World = PC ? PC->GetWorld() : nullptr;
        if (!PC || !World) { return Fail(TEXT("The owning controller or its world ended before the scenario finished")); }
        const ALDGameState* Public = World->GetGameState<ALDGameState>();
        if (!Public) { return true; }
        if (!Catalog)
        {
            FString LoadError;
            if (!LDDemoEntry::LoadRuntimeContent(Catalog, LoadError) || !Catalog)
            {
                return Fail(TEXT("The approved runtime content is unavailable"), LoadError);
            }
        }
        if (Public->PublicMatch.Phase == ELDMatchPhase::Faulted) { return Fail(TEXT("The match faulted")); }
        const UNetDriver* Driver = World->GetNetDriver();
        if (Stage != EStage::Join)
        {
            const bool bConnected = IsHost() ? Driver && Driver->ClientConnections.Num() == 1 : Driver && Driver->ServerConnection != nullptr;
            if (!bConnected) { return Fail(TEXT("The owning connection dropped before the scenario finished")); }
        }
        // The other role stopped on a failed check: close this report instead of waiting for its markers.
        if (IsHost() ? Exists(TEXT("Client1.json")) && !Exists(TEXT("ClientKill.json")) : Exists(TEXT("Host.json")))
        {
            return Fail(TEXT("The other role ended its scenario early; its report names the failed check"));
        }
        switch (Stage)
        {
        case EStage::Join: return TickJoin(*World, *PC, Public->DemoLobby, Driver);
        case EStage::Lobby: return TickLobby(Now, *PC, Public->DemoLobby);
        case EStage::EnterField: return TickEnterField(Now, *PC, *Public);
        case EStage::HostGrant: case EStage::ClientGrant: return TickGrant(Now, *PC);
        case EStage::HostHud: return TickHostHud(Now, *PC);
        case EStage::HostAwaitBuilt: return TickHostAwaitBuilt(Now);
        case EStage::HostStand: return TickHostStand(Now, *PC, *World);
        case EStage::HostAwaitDeath: return TickHostAwaitDeath(Now, *PC, *Public);
        case EStage::HostAudit: return TickHostAudit(Now, *World);
        case EStage::ClientChoice: return TickClientChoice(Now, *PC);
        case EStage::ClientPoints: return TickClientPoints(Now, *PC);
        case EStage::ClientEmpower: return TickClientEmpower(Now, *PC);
        case EStage::ClientSeek: return TickClientSeek(Now, *PC);
        case EStage::ClientKill: return TickClientKill(Now, *PC);
        case EStage::ClientReward: return TickClientReward(Now, *PC);
        default: return true;
        }
    }

    bool TickJoin(const UWorld& World, ALDPlayerController& PC, const FLDLobbyView& Lobby, const UNetDriver* Driver)
    {
        if (!PC.IsLocalController() || !PC.GetLocalPlayer() || !Lobby.Generation) { return true; }
        if (!bJoined)
        {
            if (!Mark(Role + TEXT("Joined.json"))) { return Finish(); }
            bJoined = true;
        }
        if (IsHost())
        {
            if (!Exists(TEXT("Client1Joined.json")) || Lobby.Members.Num() != 2 || !Driver || Driver->ClientConnections.Num() != 1)
            { return true; }
            Check(World.GetNetMode() == NM_ListenServer && PC.HasAuthority() && LDDevGrowthFixture::IsSwitchPresent(),
                TEXT("A participating Listen Host with the DEV growth fixture serves one real owning connection"));
        }
        else
        {
            Check(World.GetNetMode() == NM_Client && !PC.HasAuthority() && PC.GetNetConnection() != nullptr
                && !LDDevGrowthFixture::IsSwitchPresent(), TEXT("Client1 plays through a real owning remote connection, without the fixture"));
        }
        if (!Passed) { return Finish(); }
        SetStage(EStage::Lobby);
        return true;
    }

    // The ordinary lobby through owning session RPCs: no AI seats, both humans ready, the Host presses Start.
    bool TickLobby(double Now, ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        if (Lobby.Phase != ELDMatchPhase::Lobby)
        {
            for (const FLDLobbyMember& Member : Lobby.Members)
            {
                if (Member.Kind == ELDContenderKind::Human) { (Member.Host ? HostId : ClientId) = Member.ContenderId; }
            }
            if (!Check(HostId.IsValid() && ClientId.IsValid() && (IsHost() ? HostId : ClientId) == PC.OwnerState.ContenderId,
                TEXT("The started room holds exactly the Host and the owning client, and this role is its own contender")))
            { return Finish(); }
            SetStage(EStage::EnterField);
            return true;
        }
        if (Now - StageAt > LDDemoGrowthLink::FieldSeconds) { return Fail(TEXT("The lobby never started the match"), Lobby.StartBlocker); }
        if (Now < NextCommandAt) { return true; }
        NextCommandAt = Now + 1.0;
        if (IsHost() && Lobby.AICount != 0)
        {
            PC.SetDemoAICount(0);
            return true;
        }
        if (!PC.OwnerState.bReady)
        {
            PC.SetDemoReady(true);
            return true;
        }
        const bool bAllReady = Lobby.Members.Num() == 2 && !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Member)
            { return Member.Kind != ELDContenderKind::Human || !Member.Ready; });
        if (IsHost() && bAllReady && Lobby.AICount == 0 && Lobby.CanStart) { PC.StartDemoMatch(); }
        return true;
    }

    // Both roles wait for a living field body. The Host gets the room there with two DEV commands of its own connection:
    // it skips the opening rest and then freezes the stage countdown, so no mid rest can interrupt the short scenario.
    bool TickEnterField(double Now, ALDPlayerController& PC, const ALDGameState& Public)
    {
        const FLDPublicMatchView& Match = Public.PublicMatch;
        if (Now - StageAt > LDDemoGrowthLink::FieldSeconds)
        {
            return Fail(TEXT("The match never reached the field with a living body for this role"),
                FString::Printf(TEXT("phase %d life phase %d"), int32(Match.Phase), int32(PC.OwnerState.LifePhase)));
        }
        const bool bField = Match.Phase == ELDMatchPhase::Development && Match.Generation == PC.OwnerState.Match;
        if (IsHost() && bSent)
        {
            bool bApplied = false;
            FString Detail;
            const EWait Wait = AwaitDev(Now, bApplied, Detail);
            if (Wait == EWait::Waiting) { return true; }
            if (Wait == EWait::Failed) { return Fail(CallName, Detail); }
            bSent = false;
            if (!bSkipDone)
            {
                // A refusal means the rest was not in its build step yet (or already over): ask again shortly.
                bSkipDone = bApplied;
                if (!bApplied && !bField) { NextCommandAt = Now + 1.0; }
                Facts->SetStringField(TEXT("skip_preparation"), Detail);
            }
            else if (!Check(bApplied, CallName, Detail)) { return Finish(); }
            else { bPaused = true; }
            return true;
        }
        if (IsHost() && !bSkipDone && !bField)
        {
            if (Match.Phase != ELDMatchPhase::Rest || !PC.ReadOwnProgression() || Now < NextCommandAt) { return true; }
            ++Skips;
            return SendDev(PC, TEXT("The Listen Host's DEV skip of the opening rest"), ELDDevTestOp::SkipPreparation, 0) ? true : Finish();
        }
        if (!bField || PC.OwnerState.LifePhase != ELDLifePhase::Alive || !Cast<ALDCharacter>(PC.GetPawn()) || !PC.CanWorldCommand()
            || !PC.ReadOwnProgression() || PC.CombatView.Generation != PC.OwnerState.Match)
        { return true; }
        if (IsHost() && !bPaused)
        {
            return SendDev(PC, TEXT("The Listen Host's DEV stage countdown pause is applied"), ELDDevTestOp::SetCountdownPaused, 1)
                ? true : Finish();
        }
        Facts->SetNumberField(TEXT("skip_requests"), Skips);
        SetStage(IsHost() ? EStage::HostGrant : EStage::ClientGrant);
        return true;
    }

    // Both roles: a DEV XP grant through the own connection (Client1 remote, the Host as Listen Host) answers Applied and
    // the owner's own view rises: more XP, a higher level and one more active opportunity.
    bool TickGrant(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        if (!View) { return Fail(TEXT("The owner's growth view vanished in the field")); }
        if (!bSent)
        {
            Before = *View;
            GrantXP = OfferLevelGrant(*View);
            return SendDev(PC, TEXT("A DEV XP grant"), ELDDevTestOp::GrantExperience, GrantXP) ? true : Finish();
        }
        bool bApplied = false;
        FString Detail;
        const EWait Wait = AwaitDev(Now, bApplied, Detail);
        const bool bShown = View->CumulativeXP > Before.CumulativeXP && View->Level > Before.Level
            && View->ActiveOpportunitiesEarned > Before.ActiveOpportunitiesEarned;
        if (Wait == EWait::Waiting || (Wait == EWait::Ready && bApplied && !bShown && Now - CallAt <= LDDemoGrowthLink::RequestSeconds))
        { return true; }
        Detail += FString::Printf(TEXT("; granted %lld; before [%s] after [%s]"), GrantXP, *ViewText(Before), *ViewText(*View));
        Check(Wait == EWait::Ready && bApplied, TEXT("The DEV XP grant reaches the server and its receipt returns Applied"), Detail);
        Check(bShown, TEXT("The grant's XP, level and active opportunity appear in the owner's own view"), Detail);
        Facts->SetNumberField(TEXT("grant_xp"), double(GrantXP));
        Facts->SetNumberField(TEXT("level_after_grant"), View->Level);
        if (!Passed) { return Finish(); }
        SetStage(IsHost() ? EStage::HostHud : EStage::ClientChoice);
        return true;
    }

    // ---- Host: HUD, target, death and the authority's record ----

    bool TickHostHud(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        const FString Shown = LDDemoGrowthLink::HudLevelText(PC);
        const bool bMatches = View && !Shown.IsEmpty() && Shown == FString::FromInt(View->Level);
        if (!bMatches && Now - StageAt <= LDDemoGrowthLink::RequestSeconds) { return true; }
        Check(bMatches, TEXT("The Host's rendered HUD level badge shows the level of its own view"),
            FString::Printf(TEXT("HUD \"%s\", own view level %d"), *Shown, View ? int32(View->Level) : -1));
        Facts->SetStringField(TEXT("hud_level"), Shown);
        if (!Passed) { return Finish(); }
        SetStage(EStage::HostAwaitBuilt);
        return true;
    }

    bool TickHostAwaitBuilt(double Now)
    {
        if (Exists(TEXT("ClientBuilt.json")))
        {
            SetStage(EStage::HostStand);
            return true;
        }
        return Now - StageAt <= 3 * LDDemoGrowthLink::RequestSeconds ? true : Fail(TEXT("The client never finished its build steps"));
    }

    // The Host puts its own body next to the client's with the DEV teleport of its own connection: scenario setup, so the
    // one real kill needs no walk across the map. The client's position is read on the authority for that setup only.
    bool TickHostStand(double Now, ALDPlayerController& PC, UWorld& World)
    {
        const APawn* Own = PC.GetPawn();
        const APawn* Other = nullptr;
        for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
        {
            if (It->Get() && It->Get() != &PC) { Other = It->Get()->GetPawn(); }
        }
        if (!Own || !Other || PC.OwnerState.LifePhase != ELDLifePhase::Alive)
        {
            return Now - StageAt <= LDDemoGrowthLink::RequestSeconds ? true : Fail(TEXT("The Host found no living bodies to bring together"));
        }
        if (bSent)
        {
            bool bApplied = false;
            FString Detail;
            const EWait Wait = AwaitDev(Now, bApplied, Detail);
            if (Wait == EWait::Waiting) { return true; }
            if (Wait == EWait::Failed) { return Fail(CallName, Detail); }
            bSent = false;
            if (!bApplied)
            {
                // No standable ground or no room at this bearing: try the next one.
                return Bearing < LDDemoGrowthLink::TargetBearings ? true
                    : Fail(TEXT("The DEV teleport found no place to stand next to the client"), Detail);
            }
            const double Distance = FVector::Dist2D(Own->GetActorLocation(), Other->GetActorLocation());
            LifeBefore = PC.OwnerState.Life;
            Facts->SetNumberField(TEXT("target_distance_cm"), Distance);
            Facts->SetNumberField(TEXT("teleport_requests"), Bearing);
            if (!Check(Distance <= 3 * LDDemoGrowthLink::TargetDistanceCm, TEXT("The Host stands next to the client as its target"),
                FString::Printf(TEXT("%.0f cm apart after %d teleport request(s)"), Distance, Bearing))
                || !Mark(TEXT("HostInPlace.json")))
            { return Finish(); }
            SetStage(EStage::HostAwaitDeath);
            return true;
        }
        const double Angle = 2.0 * UE_DOUBLE_PI * double(Bearing++) / double(LDDemoGrowthLink::TargetBearings);
        const FVector Wanted = Other->GetActorLocation() + FVector(FMath::Cos(Angle) * LDDemoGrowthLink::TargetDistanceCm,
            FMath::Sin(Angle) * LDDemoGrowthLink::TargetDistanceCm, LDDemoGrowthLink::TargetDropCm);
        return SendDev(PC, TEXT("The Host's DEV teleport next to the client"), ELDDevTestOp::TeleportToCamera, 0, Wanted) ? true : Finish();
    }

    bool TickHostAwaitDeath(double Now, ALDPlayerController& PC, const ALDGameState& Public)
    {
        // Auto-empower: an opponent's public ready mark rides only its sight-authorized momentum group.
        bEmpowerOn = PC.CombatView.bEmpowerEnabled;
        if (const ALDWorldViewRelay* Relay = bEmpowerOn ? PC.LocalView() : nullptr)
        {
            const FLDObservedFrameRead Read = Relay->ReadObservedFrame();
            for (int32 Index = 0; Read.Status == ELDObservedViewStatus::Current && Read.Frame && Index < Read.Frame->Units.Num(); ++Index)
            {
                const FLDObservedUnitView& Unit = Read.Frame->Units[Index];
                if (Unit.EmpowerReady == 0) { continue; }
                const bool bAuthorized = Unit.Kind == ELDObservedUnitKind::Contender && (Unit.AuthorizedFieldMask & LDObservation::Momentum) != 0
                    && Unit.EmpowerReady <= uint8(ELDEmpowerProcessor::Ward);
                UnauthorizedMarks += bAuthorized ? 0 : 1;
                ReadyMarks += bAuthorized && Unit.ContenderId == ClientId ? 1 : 0;
            }
        }
        const bool bDied = PC.OwnerState.Life == LifeBefore ? PC.OwnerState.LifePhase == ELDLifePhase::Dead : PC.OwnerState.Life > LifeBefore;
        const bool bNoticed = Public.KillFeed.Notices.ContainsByPredicate([this](const FLDKillNotice& Notice)
            { return Notice.Victim == HostId && Notice.Killer == ClientId && Notice.Cause == ELDKillCause::Contender; });
        if ((!bDied || !bNoticed) && Now - StageAt <= LDDemoGrowthLink::KillSeconds + LDDemoGrowthLink::RequestSeconds) { return true; }
        Check(bDied, TEXT("The Host's own life ended under the client's attack"),
            FString::Printf(TEXT("life %u->%u phase %d hp %.0f"), LifeBefore, PC.OwnerState.Life, int32(PC.OwnerState.LifePhase), PC.OwnerState.Hp));
        Check(bNoticed, TEXT("The public kill feed names the client as the Host's killer"));
        if (bEmpowerOn)
        {
            Check(UnauthorizedMarks == 0, TEXT("An opponent's empower ready mark appears only with its sight-authorized momentum group"),
                FString::Printf(TEXT("%d unauthorized, %d frames with the client's mark"), UnauthorizedMarks, ReadyMarks));
            // The client's bar crossed the threshold next to the Host before its empowered punches (ClientReward checks them).
            Check(ReadyMarks > 0, TEXT("The Host saw the attacking client's public ready mark"),
                FString::Printf(TEXT("%d frames with the client's mark"), ReadyMarks));
            Facts->SetNumberField(TEXT("host_seen_client_ready_frames"), ReadyMarks);
        }
        if (!Passed) { return Finish(); }
        SetStage(EStage::HostAudit);
        return true;
    }

    // The XP the client read in its own view after the kill equals the authority's record for that contender.
    bool TickHostAudit(double Now, UWorld& World)
    {
        ALDGameMode* Mode = World.GetAuthGameMode<ALDGameMode>();
        if (ClientXPAfterKill < 0 && Exists(TEXT("ClientKill.json")))
        {
            FString Text;
            TSharedPtr<FJsonObject> Marker;
            double Value = -1;
            if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(Output, TEXT("ClientKill.json")))
                && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Marker) && Marker.IsValid()
                && Marker->TryGetNumberField(TEXT("xp"), Value))
            { ClientXPAfterKill = int64(Value); }
        }
        const FLDProgressionView Record = Mode && Mode->Development ? Mode->Development->ReadProgression(ClientId) : FLDProgressionView();
        const bool bEqual = ClientXPAfterKill >= 0 && Record.bValid && Record.CumulativeXP == ClientXPAfterKill;
        if (!bEqual && Now - StageAt <= 2 * LDDemoGrowthLink::RequestSeconds) { return true; }
        Check(bEqual, TEXT("The client's own view XP after the kill equals the authority's record for that contender"),
            FString::Printf(TEXT("client view %lld, authority record %lld"), ClientXPAfterKill, Record.bValid ? Record.CumulativeXP : -1));
        Facts->SetNumberField(TEXT("authority_client_xp"), double(Record.bValid ? Record.CumulativeXP : -1));
        if (bEmpowerOn)
        {
            // Every empowered punch paid EMPOWER.Cost exactly once (no resent action paid again), and the owner saw them.
            double Seen = -1;
            FString Text;
            TSharedPtr<FJsonObject> Marker;
            if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(Output, TEXT("ClientKill.json")))
                && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Marker) && Marker.IsValid())
            { Marker->TryGetNumberField(TEXT("empowerments"), Seen); }
            int32 Count = 0;
            int64 SpentP2 = 0;
            const bool bRead = Mode && Mode->Combat && Mode->Combat->ReadEmpowerLedger(ClientId, Count, SpentP2);
            const int64 CostP2 = Catalog->Get().Parameters.Empower.CostP2;
            Check(bRead && Count >= 1 && SpentP2 == int64(Count) * CostP2 && Seen >= 1 && int32(Seen) <= Count,
                TEXT("Each of the client's empowerments paid its price exactly once, and its own view counted them"),
                FString::Printf(TEXT("authority %d paid %lld (cost %lld each); client view %.0f"), Count, SpentP2, CostP2, Seen));
            Facts->SetNumberField(TEXT("authority_client_empowerments"), Count);
        }
        return Finish();
    }

    // ---- Client1: two production build commands, then one real kill ----

    bool TickClientChoice(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        if (!View) { return Fail(TEXT("The owner's growth view vanished in the field")); }
        if (!bSent)
        {
            if (!View->bHasActiveOffer || View->ActiveOffer.Options.IsEmpty())
            {
                return Now - StageAt <= LDDemoGrowthLink::RequestSeconds ? true
                    : Fail(TEXT("The owner's view never offered an active to choose"), ViewText(*View));
            }
            Option = View->ActiveOffer.Options[0];
            FLDBuildCommand Command;
            Command.Kind = ELDBuildCommandKind::ActiveChoice;
            Command.OfferId = View->ActiveOffer.OfferId;
            Command.OfferRevision = View->ActiveOffer.OfferRevision;
            FLDDefinitionWire::Encode(Option, Command.Option);
            Command.bLearnIfPossible = true;
            Command.Slot = FLDBuildCommand::AnySlot;
            return SendBuild(PC, TEXT("The active choice"), Command) ? true : Finish();
        }
        const FLDBuildReceipt* Final = nullptr;
        FString Detail;
        const EWait Wait = AwaitBuild(Now, Final, Detail);
        const FName Chosen = Option;
        const bool bApplied = Final && Final->Header.Outcome == ELDReceiptOutcome::Applied && Final->Kind == ELDBuildCommandKind::ActiveChoice;
        const bool bView = View->Actives.Num() > Before.Actives.Num()
            && View->Actives.ContainsByPredicate([Chosen](const FLDActiveSlotView& Row) { return Row.DefinitionId == Chosen; });
        const bool bCombat = PC.CombatView.Generation == PC.OwnerState.Match
            && PC.CombatView.Entries.ContainsByPredicate([Chosen](const FLDCombatEntryView& Row) { return Row.DefinitionId == Chosen; });
        if (Wait == EWait::Waiting || (bApplied && !(bView && bCombat) && Now - CallAt <= LDDemoGrowthLink::RequestSeconds)) { return true; }
        Detail += FString::Printf(TEXT("; chose %s; before [%s] after [%s]"), *Chosen.ToString(), *ViewText(Before), *ViewText(*View));
        Check(bApplied, TEXT("The client's SubmitBuild active choice returns an Applied receipt"), Detail);
        Check(bView, TEXT("The chosen active appears among the learned actives of the client's own view"), Detail);
        Check(bCombat, TEXT("The client's combat projection carries the chosen active as an entry"), Detail);
        Facts->SetStringField(TEXT("chosen_active"), Chosen.ToString());
        if (!Passed) { return Finish(); }
        SetStage(EStage::ClientPoints);
        return true;
    }

    bool TickClientPoints(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        if (!View) { return Fail(TEXT("The owner's growth view vanished in the field")); }
        const FName Stat(LDDemoGrowthLink::PointStat);
        if (!bSent)
        {
            const int32 Index = Catalog->Get().StatDesignIndex(LDDemoGrowthLink::PointStat);
            if (!Check(Index != INDEX_NONE && View->UnspentPoints >= 1, TEXT("The client has an unspent point and the catalog has the probe's stat"),
                ViewText(*View)))
            { return Finish(); }
            FLDBuildCommand Command;
            Command.Kind = ELDBuildCommandKind::AllocatePoints;
            Command.PointCounts.Init(0, Catalog->Get().Stats.Num());
            Command.PointCounts[Index] = 1;
            return SendBuild(PC, TEXT("The point allocation"), Command) ? true : Finish();
        }
        const FLDBuildReceipt* Final = nullptr;
        FString Detail;
        const EWait Wait = AwaitBuild(Now, Final, Detail);
        const bool bApplied = Final && Final->Header.Outcome == ELDReceiptOutcome::Applied && Final->Kind == ELDBuildCommandKind::AllocatePoints;
        const int32 PointsBefore = LDDemoGrowthLink::PointsOf(Before, Stat), PointsAfter = LDDemoGrowthLink::PointsOf(*View, Stat);
        const bool bView = PointsBefore >= 0 && PointsAfter > PointsBefore;
        const bool bCombat = PC.CombatView.Generation == PC.OwnerState.Match && PC.CombatView.MaxHPP2 > MaxHPBeforeP2;
        if (Wait == EWait::Waiting || (bApplied && !(bView && bCombat) && Now - CallAt <= LDDemoGrowthLink::RequestSeconds)) { return true; }
        Detail += FString::Printf(TEXT("; %s points %d->%d, projected max HP (P2) %lld->%lld"), LDDemoGrowthLink::PointStat, PointsBefore,
            PointsAfter, MaxHPBeforeP2, PC.CombatView.MaxHPP2);
        Check(bApplied, TEXT("The client's SubmitBuild point allocation returns an Applied receipt"), Detail);
        Check(bView, TEXT("The allocated stat's points rise in the client's own view"), Detail);
        Check(bCombat, TEXT("The client's combat projection follows the allocation with a larger maximum HP"), Detail);
        if (!Passed) { return Finish(); }
        SetStage(EStage::ClientEmpower);
        return true;
    }

    static FString EmpowerText(const FLDCombatOwnerView& View)
    {
        return FString::Printf(TEXT("on=%d host=%d chosen=%d sequence=%llu threshold=%lld cost=%lld momentum=%lld empowerments=%d"),
            View.bEmpowerEnabled ? 1 : 0, int32(View.EmpowerHost), View.bEmpowerHostChosen ? 1 : 0, View.EmpowerHostSequence,
            View.EmpowerThresholdP2, View.EmpowerCostP2, View.MomentumP2, View.Empowerments);
    }

    // Auto-empower host commands on this owner's own connection: an accepted request shows in the owner view under its
    // sequence, an invalid one comes back refused with its reason, and a replayed sequence changes nothing; the kill then
    // runs on the basic host. Off (no -LDEmpower), one request is refused and changes nothing.
    bool TickClientEmpower(double Now, ALDPlayerController& PC)
    {
        const FLDCombatOwnerView& View = PC.CombatView;
        const bool bLate = Now - EmpowerAt > LDDemoGrowthLink::RequestSeconds;
        const auto Next = [this, Now]() { ++EmpowerStep; EmpowerAt = Now; };
        switch (EmpowerStep)
        {
        case 0:
            EmpowerAt = Now;
            bEmpowerOn = View.bEmpowerEnabled;
            Facts->SetBoolField(TEXT("empower"), bEmpowerOn);
            if (bEmpowerOn)
            {
                Check(View.EmpowerHost == ELDCombatEntry::Basic && !View.bEmpowerHostChosen && View.EmpowerThresholdP2 > 0
                    && View.EmpowerCostP2 > 0 && View.EmpowerCostP2 <= View.EmpowerThresholdP2,
                    TEXT("The owner view starts on the basic host with the experiment's threshold and price"), EmpowerText(View));
            }
            PC.RequestEmpowerHost(ELDCombatEntry::Q);
            EmpowerSeq = PC.SentEmpowerHostSequence();
            EmpowerStep = bEmpowerOn ? 1 : 10;
            return true;
        case 1:
        {
            const bool bShown = View.EmpowerHost == ELDCombatEntry::Q && View.bEmpowerHostChosen && View.EmpowerHostSequence == EmpowerSeq;
            if (!bShown && !bLate) { return true; }
            if (!Check(bShown, TEXT("A host request's receipt is the owner view naming it under its sequence"), EmpowerText(View)))
            { return Finish(); }
            // A replay of the same sequence (naming another host) must change nothing; the invalid request after it is
            // refused, and its refusal, ordered after the replay on this connection, bounds the wait.
            PC.ServerSetEmpowerHost(View.Generation, EmpowerSeq, ELDCombatEntry::Dash);
            PC.RequestEmpowerHost(ELDCombatEntry::None);
            Next();
            return true;
        }
        case 2:
        {
            FString Reason;
            const bool bRefused = PC.RefusedEmpowerHostSequence(&Reason) == PC.SentEmpowerHostSequence();
            if (!bRefused && !bLate) { return true; }
            if (!Check(bRefused && !Reason.IsEmpty(), TEXT("An invalid host request comes back refused with its reason"), Reason))
            { return Finish(); }
            Next();
            return true;
        }
        case 3:
            // One second for any view change the replay could have caused to arrive.
            if (Now - EmpowerAt < 1.0) { return true; }
            Check(View.EmpowerHost == ELDCombatEntry::Q && View.EmpowerHostSequence == EmpowerSeq,
                TEXT("A replayed host sequence changes nothing"), EmpowerText(View));
            PC.RequestEmpowerHost(ELDCombatEntry::Basic);
            EmpowerSeq = PC.SentEmpowerHostSequence();
            Next();
            return true;
        case 4:
        {
            const bool bShown = View.EmpowerHost == ELDCombatEntry::Basic && View.EmpowerHostSequence == EmpowerSeq;
            if (!bShown && !bLate) { return true; }
            Check(bShown, TEXT("A newer host request moves the host to the basic for the kill"), EmpowerText(View));
            break;
        }
        case 10:
        {
            FString Reason;
            const bool bRefused = PC.RefusedEmpowerHostSequence(&Reason) == EmpowerSeq;
            if (!bRefused && !bLate) { return true; }
            Check(bRefused && !Reason.IsEmpty() && View.EmpowerHostSequence == 0,
                TEXT("Off, a host request comes back refused with its reason and changes nothing"), Reason);
            break;
        }
        default:
            return Fail(TEXT("The empower step is unknown"));
        }
        if (!Passed || !Mark(TEXT("ClientBuilt.json"))) { return Finish(); }
        SetStage(EStage::ClientSeek);
        return true;
    }

    // The target must be in this owner's own observed frame; the attack is the ordinary AttackUnit intent on its observer
    // handle (what a right click on the unit sends).
    bool TickClientSeek(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        const FLDObservedUnitView* Target = Exists(TEXT("HostInPlace.json")) ? LDDemoGrowthLink::SeenContender(PC, HostId) : nullptr;
        if (Orders == 0 && Target && View)
        {
            KillXPBefore = View->CumulativeXP;
            // The kill budget starts when the target is first seen, not while the Host is still being placed.
            StageAt = Now;
        }
        if (!Target || !View || Now < NextCommandAt)
        {
            return Now - StageAt <= LDDemoGrowthLink::KillSeconds ? true
                : Fail(Orders ? TEXT("The Host did not die within the kill budget") : TEXT("The Host never appeared in the client's own observed frame"),
                    PC.Navigation->Status);
        }
        if (Orders >= LDDemoGrowthLink::AttackOrders)
        {
            return Fail(TEXT("The client's attack orders kept ending without the Host's death"), PC.Navigation->Status);
        }
        PC.Navigation->AttackUnit(Target->Handle, Target->ObservationEpoch);
        AttackOrder = PC.Navigation->LocalIdentity();
        ++Orders;
        const double Budget = StageAt;
        SetStage(EStage::ClientKill);
        StageAt = Budget;
        return true;
    }

    bool TickClientKill(double Now, ALDPlayerController& PC)
    {
        bSawReady |= bEmpowerOn && PC.CombatView.EmpowerThresholdP2 > 0 && PC.CombatView.MomentumP2 >= PC.CombatView.EmpowerThresholdP2;
        const FLDOrderReceipt& Latest = PC.Navigation->LatestReceipt();
        const bool bEnded = Latest.Identity == AttackOrder && (Latest.Status == ELDOrderStatus::Rejected
            || Latest.Status == ELDOrderStatus::Cancelled || Latest.Status == ELDOrderStatus::Completed);
        if (bEnded && Latest.Status == ELDOrderStatus::Completed && Latest.Failure == ELDOrderFailure::TargetDied)
        {
            Check(true, TEXT("The client's own attack order ended with the Host's death"),
                FString::Printf(TEXT("%d attack order(s), %.1f s after the Host was first seen"), Orders, Now - StageAt));
            Facts->SetNumberField(TEXT("attack_orders"), Orders);
            Facts->SetNumberField(TEXT("kill_seconds"), Now - StageAt);
            SetStage(EStage::ClientReward);
            return true;
        }
        const double Budget = StageAt;
        if (bEnded)
        {
            // The server ended the lock without a death (return protection, lost sight): seek the same target again.
            NextCommandAt = Now + 1.0;
            SetStage(EStage::ClientSeek);
            StageAt = Budget;
            return true;
        }
        return Now - StageAt <= LDDemoGrowthLink::KillSeconds ? true
            : Fail(TEXT("The client's attack order did not end with the Host's death within the kill budget"),
                FString::Printf(TEXT("order status %d failure %d; %s"), int32(Latest.Status), int32(Latest.Failure), *PC.Navigation->Status));
    }

    bool TickClientReward(double Now, ALDPlayerController& PC)
    {
        const FLDProgressionView* View = PC.ReadOwnProgression();
        const bool bRose = View && View->CumulativeXP > KillXPBefore;
        if (!bRose && Now - StageAt <= LDDemoGrowthLink::RequestSeconds) { return true; }
        Check(bRose, TEXT("The kill raises the XP in the client's own view"),
            FString::Printf(TEXT("xp %lld->%lld"), KillXPBefore, View ? View->CumulativeXP : -1));
        if (!Passed) { return Finish(); }
        const TSharedRef<FJsonObject> Marker = MakeShared<FJsonObject>();
        Marker->SetNumberField(TEXT("xp"), double(View->CumulativeXP));
        if (bEmpowerOn)
        {
            // The kill ran on the basic host against a contender: the bar crossed the threshold and punches were empowered.
            Check(bSawReady, TEXT("The client's own view showed its bar at the empower threshold during the kill"), EmpowerText(PC.CombatView));
            Check(PC.CombatView.Empowerments >= 1, TEXT("The client's empowered punches are counted in its own view"), EmpowerText(PC.CombatView));
            Marker->SetNumberField(TEXT("empowerments"), double(PC.CombatView.Empowerments));
            Facts->SetNumberField(TEXT("client_empowerments"), double(PC.CombatView.Empowerments));
        }
        Facts->SetNumberField(TEXT("xp_before_kill"), double(KillXPBefore));
        Facts->SetNumberField(TEXT("xp_after_kill"), double(View->CumulativeXP));
        Mark(TEXT("ClientKill.json"), Marker);
        return Finish();
    }

    // After the reports each process exits on the runner's signal: the client on FinishClients, the Host on Finish.
    bool TickAfterReport() const
    {
        if (!Exists(IsHost() ? TEXT("Finish") : TEXT("FinishClients"))) { return true; }
        FPlatformMisc::RequestExitWithStatus(false, 0);
        return false;
    }

    bool Finish()
    {
        if (Written) { return true; }
        Unbind();
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), Passed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetNumberField(TEXT("stage"), int32(Stage));
        Report->SetBoolField(TEXT("growth_fixture"), LDDevGrowthFixture::IsSwitchPresent());
        Report->SetStringField(TEXT("scope"), IsHost()
            ? TEXT("DEV-only short owned-connection growth scenario, Listen Host role: skips the opening rest and pauses the stage countdown with its own DEV commands, takes its own DEV XP grant and reads its rendered HUD level badge, teleports next to the client as the target of one real kill, and compares the authority's XP record of the client with what the client saw. Invariants only; no expected number is recomputed.")
            : TEXT("DEV-only short owned-connection growth scenario, owning client role: a DEV XP grant, an active choice and a point allocation through its own connection, each with its Applied receipt and the own view and combat projection following, then one real kill of the Host by its own attack order. Invariants only; no expected number is recomputed."));
        Report->SetArrayField(TEXT("checks"), Checks);
        Report->SetObjectField(TEXT("facts"), Facts);
        if (const ALDPlayerController* PC = Controller.Get())
        {
            Report->SetNumberField(TEXT("generation"), PC->OwnerState.Match);
            if (const FLDProgressionView* View = PC->ReadOwnProgression())
            {
                Report->SetNumberField(TEXT("final_level"), View->Level);
                Report->SetNumberField(TEXT("final_xp"), double(View->CumulativeXP));
            }
        }
        Written = Save(Report, Role + TEXT(".json"));
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_GROWTH_LINK_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
        if (!Passed || !Written)
        {
            FPlatformMisc::RequestExitWithStatus(false, 2);
            return false;
        }
        return true;
    }
};

void LDDemoTests::StartGrowthLinkProbe(ALDPlayerController& Controller)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoGrowthLinkProbe")) || !Controller.IsLocalController()
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap)
    {
        return;
    }
    const TSharedRef<FLDDemoGrowthLinkProbe> Probe = MakeShared<FLDDemoGrowthLinkProbe>();
    int32 Timeout = 240;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoGrowthLinkTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoGrowthLinkOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoGrowthLinkRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1")) || Timeout < 60 || Timeout > 600
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_GROWTH_LINK_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    Probe->Controller = &Controller;
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    Probe->StageAt = FPlatformTime::Seconds();
    Probe->Bind(Controller);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .1f);
}
#endif
