#include "Tests/LDDemoCaptureProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDProgressionRules.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "World/LDCampDirector.h"
#include "World/LDCampLifecycle.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// Host stays at its spawn, Client1 captures, Client2 contests once mid-segment and leaves again.
// After the result the Host returns the same connections to a new Lobby generation and starts a second match.
enum class ELDCaptureProbeStage : uint8 { Join, Lobby, Starting, Match, Results, SecondLobby, SecondMatch, Done };

class FLDDemoCaptureProbe : public TSharedFromThis<FLDDemoCaptureProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output, Role, Error;
    double Deadline = 0, StageAt = 0, ReadyAt = 0;
    ELDCaptureProbeStage Stage = ELDCaptureProbeStage::Join;
    int32 ContestStep = 0;
    bool Passed = true, Written = false, ReadySent = false, IdentitySaved = false, StartSent = false, ReturnSent = false;
    bool SecondStartSent = false;
    bool SawContest = false;
    uint32 FirstGeneration = 0, SecondGeneration = 0;
    FGuid CapturerId;
    FLDCombatBodyRef FirstBody;
    TSet<FGuid> FirstCampMembers;
    int64 FirstSoloSinceMs = -1;
    int64 HoldMs = 0, DevelopmentMs = 0;
    FVector Center = FVector::ZeroVector, Staging = FVector::ZeroVector, Inside = FVector::ZeroVector;
    FName Surface;
    double RadiusCm = 0;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();

    // Stable ids let the runner require each role's checks, so a skipped stage cannot pass silently.
    bool Check(bool Value, const TCHAR* Id, const TCHAR* Name)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("id"), Id);
        Row->SetStringField(TEXT("name"), Name);
        Row->SetBoolField(TEXT("passed"), Value);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        Passed &= Value;
        if (!Value && Error.IsEmpty()) { Error = FString(Id) + TEXT(": ") + Name; }
        return Value;
    }

    bool Save(const TSharedRef<FJsonObject>& Value, const FString& Name) const
    {
        FString Json;
        const FString File = FPaths::Combine(Output, Name);
        return FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *(File + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*File, *(File + TEXT(".tmp")), true, true);
    }

    bool Mark(const FString& Name)
    {
        auto Value = MakeShared<FJsonObject>();
        Value->SetBoolField(TEXT("ready"), true);
        if (Save(Value, Name)) { return true; }
        Error = TEXT("Could not persist capture probe marker: ") + Name;
        Passed = false;
        return false;
    }

    bool AllMarkers(const TCHAR* Suffix) const
    {
        for (const TCHAR* Name : {TEXT("Host"), TEXT("Client1"), TEXT("Client2")})
        {
            if (!FPaths::FileExists(FPaths::Combine(Output, FString(Name) + Suffix))) { return false; }
        }
        return true;
    }

    // Login names are replaced by the online subsystem, so roles exchange their contender identity.
    bool SaveIdentity(const FGuid& Contender)
    {
        auto Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("contender"), Contender.ToString(EGuidFormats::Digits));
        if (Contender.IsValid() && Save(Value, Role + TEXT("Id.json"))) { return true; }
        Error = TEXT("Could not persist the role's contender identity.");
        Passed = false;
        return false;
    }

    bool LoadIdentity(const TCHAR* Name, FGuid& Out) const
    {
        FString Json, Text;
        TSharedPtr<FJsonObject> Value;
        return FFileHelper::LoadFileToString(Json, *FPaths::Combine(Output, FString(Name) + TEXT("Id.json")))
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Value) && Value.IsValid()
            && Value->TryGetStringField(TEXT("contender"), Text) && FGuid::Parse(Text, Out) && Out.IsValid();
    }

    bool Finish()
    {
        auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), Passed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetStringField(TEXT("scope"), TEXT("DEV-only normal start through providers, real owned capture, contest reset, unique result, same-world return to a new Lobby generation and a second normal match that starts clean. Not 3+3 or AI."));
        Report->SetObjectField(TEXT("summary"), Summary);
        Report->SetArrayField(TEXT("checks"), Checks);
        Written = Save(Report, Role + TEXT(".json"));
        UE_LOG(LogTemp, Display, TEXT("LD_DEMO_CAPTURE_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
        if (!Passed || !Written) { FPlatformMisc::RequestExitWithStatus(false, 2); return false; }
        return true;
    }

    // A ready raced by a roster change is refused as stale; resend until the published roster shows it.
    bool EnsureReady(ALDPlayerController& PC, const FLDLobbyView& Lobby)
    {
        const FLDLobbyMember* Self = Lobby.Members.FindByPredicate([&PC](const FLDLobbyMember& Row)
        { return Row.ContenderId == PC.OwnerState.ContenderId; });
        if (Self && Self->Ready) { return true; }
        if (!ReadySent || FPlatformTime::Seconds() - ReadyAt >= 3.)
        {
            PC.SetDemoReady(true);
            ReadySent = true;
            ReadyAt = FPlatformTime::Seconds();
        }
        return false;
    }

    static bool AllHumansReady(const FLDLobbyView& Lobby)
    {
        return Lobby.Members.Num() == 3 && !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Row)
        { return Row.Kind == ELDContenderKind::Human && !Row.Ready; });
    }

    static void CampMembers(const ALDGameMode& Mode, TSet<FGuid>& Out)
    {
        Out.Reset();
        const ALDCampDirector* Camps = Mode.Camps.Get();
        const FLDCampMatchState* State = Camps ? &Camps->GetState() : nullptr;
        if (!State || !State->IsActive()) { return; }
        for (int32 Camp = 0; Camp < State->NumCamps(); ++Camp)
        {
            for (const FLDCampMemberState& Member : State->FindCamp(Camp)->Members) { Out.Add(Member.EntityId); }
        }
    }

    void Observe(const FLDPublicMatchView& View)
    {
        if (View.Phase != ELDMatchPhase::CaptureOpen) { return; }
        SawContest |= View.CircleState == ELDCircleState::Contested;
        if (View.CircleState == ELDCircleState::Solo && View.Occupier == CapturerId && FirstSoloSinceMs < 0)
        { FirstSoloSinceMs = View.SoloSinceMs; }
    }

    bool Tick();
    bool CheckSecondMatch(ALDPlayerController& PC, ALDGameMode* Mode, const FLDLobbyView& Lobby, const FLDPublicMatchView& View,
        const FLDProgressionView& Progress, const FLDEquipmentView& Equipment);
};

bool FLDDemoCaptureProbe::Tick()
{
    if (Written)
    {
        if (Role != TEXT("Host") && FPaths::FileExists(FPaths::Combine(Output, TEXT("FinishClients"))))
        { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
        if (Role == TEXT("Host") && FPaths::FileExists(FPaths::Combine(Output, TEXT("Finish"))))
        { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
        return true;
    }
    if (FPlatformTime::Seconds() >= Deadline)
    {
        Check(false, TEXT("T01"), TEXT("Capture scenario completed within its bounded deadline"));
        Error = FString::Printf(TEXT("Capture probe timed out at stage %u, contest step %d."), uint8(Stage), ContestStep);
        return Finish();
    }
    auto* PC = Controller.Get();
    auto* World = PC ? PC->GetWorld() : nullptr;
    auto* Public = World ? World->GetGameState<ALDGameState>() : nullptr;
    auto* Registry = World ? World->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!PC || !World || !Public || !Registry || !Registry->Enabled()) { return true; }
    if (!Content)
    {
        const FLDMapBinding* Capture = nullptr;
        if (LDDemoEntry::LoadRuntimeContent(Content, Error))
        { Capture = Registry->Resolve(FName(*Content->Get().World.CaptureId), Registry->Generation); }
        const auto& Radius = Content ? Content->Get().Parameters.CaptureRadiusCm : FLDDemoRational();
        if (!Capture || Radius.Denominator <= 0)
        {
            Check(false, TEXT("P01"), TEXT("Approved content and the registered capture anchor resolve"));
            return Finish();
        }
        Center = Capture->Position;
        Surface = Capture->Surface;
        RadiusCm = double(Radius.Numerator) / double(Radius.Denominator);
        HoldMs = Content->Get().Parameters.CaptureHoldMs;
        DevelopmentMs = Content->Get().Parameters.DevelopmentMs;
    }
    const FLDLobbyView& Lobby = Public->DemoLobby;
    const FLDPublicMatchView& View = Public->PublicMatch;
    Observe(View);
    auto* Nav = PC->Navigation.Get();
    // Only the Listen Host has the authority; clients read their own owner views only.
    ALDGameMode* Mode = World->GetAuthGameMode<ALDGameMode>();
    switch (Stage)
    {
    case ELDCaptureProbeStage::Join:
        if (!PC->IsLocalController() || !Lobby.Generation) { return true; }
        Check(Role == TEXT("Host") ? World->GetNetMode() == NM_ListenServer
            : World->GetNetMode() == NM_Client && PC->GetNetConnection() && !PC->HasAuthority(),
            TEXT("C01"), TEXT("Each role is a real Listen Host or owning client connection"));
        FirstGeneration = Lobby.Generation;
        if (!Mark(Role + TEXT("Joined.json"))) { return Finish(); }
        Stage = ELDCaptureProbeStage::Lobby;
        return true;
    case ELDCaptureProbeStage::Lobby:
    {
        if (!AllMarkers(TEXT("Joined.json")) || Lobby.Members.Num() != 3 || !EnsureReady(*PC, Lobby)) { return true; }
        if (!IdentitySaved)
        {
            if (!SaveIdentity(PC->OwnerState.ContenderId)) { return Finish(); }
            IdentitySaved = true;
        }
        if (Role != TEXT("Host")) { Stage = ELDCaptureProbeStage::Starting; return true; }
        if (!AllHumansReady(Lobby) || StartSent || !AllMarkers(TEXT("Id.json"))) { return true; }
        if (!Check(Lobby.CanStart && Lobby.StartBlocker.IsEmpty(), TEXT("C02"), TEXT("Every provider is wired: the ready room may start normally")))
        {
            Error = TEXT("Normal start is still blocked: ") + Lobby.StartBlocker;
            return Finish();
        }
        PC->StartDemoMatch();
        StartSent = true;
        Stage = ELDCaptureProbeStage::Starting;
        return true;
    }
    case ELDCaptureProbeStage::Starting:
    {
        const auto* Body = Cast<ALDCharacter>(PC->GetPawn());
        if (View.Phase != ELDMatchPhase::Development || View.Generation != Lobby.Generation || !Body
            || PC->OwnerState.LifePhase != ELDLifePhase::Alive || !PC->CanWorldCommand()) { return true; }
        Check(LoadIdentity(TEXT("Client1"), CapturerId)
            && Lobby.Members.ContainsByPredicate([this](const FLDLobbyMember& Row) { return Row.ContenderId == CapturerId; }),
            TEXT("C03"), TEXT("The capturing contender is on the locked roster"));
        Check(View.Generation == FirstGeneration && View.OpenAtMs == View.StartedAtMs + DevelopmentMs
            && View.CircleState == ELDCircleState::NotOpen && !View.Occupier.IsValid(),
            TEXT("C04"), TEXT("Normal start opens the approved development phase with a closed circle"));
        FVector Direction = Body->GetActorLocation() - Center;
        Direction.Z = 0;
        Direction = Direction.GetSafeNormal();
        Check(!Direction.IsNearlyZero(), TEXT("C05"), TEXT("Spawned body is outside the circle centre"));
        Staging = Center + Direction * (RadiusCm + 300.);
        Inside = Center + Direction * (RadiusCm - 250.);
        // The second match proves freshness against these first-match identities.
        FirstBody = PC->OwnerState.BodyRef;
        if (Mode) { CampMembers(*Mode, FirstCampMembers); }
        if (!Passed) { return Finish(); }
        if (Role == TEXT("Client1")) { Nav->NavigateGround(Center, Surface); }
        else if (Role == TEXT("Client2")) { Nav->NavigateGround(Staging, Surface); }
        Stage = ELDCaptureProbeStage::Match;
        return true;
    }
    case ELDCaptureProbeStage::Match:
    {
        int64 NowMs = 0;
        PC->ReadDemoAuthorityTimeMs(NowMs);
        if (Role == TEXT("Client2") && View.Phase == ELDMatchPhase::CaptureOpen)
        {
            const bool CapturerSolo = View.CircleState == ELDCircleState::Solo && View.Occupier == CapturerId;
            if (ContestStep == 0 && CapturerSolo && !Nav->HasMoveOrder() && NowMs - View.SoloSinceMs >= 3000)
            {
                Nav->NavigateGround(Inside, Surface);
                ContestStep = 1;
            }
            else if (ContestStep == 1 && View.CircleState == ELDCircleState::Contested)
            {
                StageAt = FPlatformTime::Seconds();
                ContestStep = 2;
            }
            else if (ContestStep == 2 && FPlatformTime::Seconds() - StageAt >= 1.5)
            {
                Nav->NavigateGround(Staging, Surface);
                ContestStep = 3;
            }
        }
        if (View.Phase != ELDMatchPhase::Results) { return true; }
        const FLDResultView& Result = Public->MatchResult;
        if (Result.Generation != View.Generation || Result.ResultId != View.ResultId) { return true; }
        Check(Result.Reason == ELDMatchEndReason::CaptureCompleted && Result.Winner == CapturerId,
            TEXT("C06"), TEXT("Continuous exclusive capture locks the capturer as the unique winner"));
        Check(SawContest && FirstSoloSinceMs >= 0 && View.SoloSinceMs > FirstSoloSinceMs,
            TEXT("C07"), TEXT("A real contest broke the first segment and the occupier restarted a complete hold"));
        Check(Result.EndedAtMs == View.DueAtMs && View.DueAtMs - View.SoloSinceMs == HoldMs
            && Result.DurationMs == Result.EndedAtMs - Result.StartedAtMs,
            TEXT("C08"), TEXT("Completion is the approved hold after the last uncontested start, timed by the server"));
        Check(Result.Rows.Num() == 3 && !Result.Rows.ContainsByPredicate([](const FLDResultRow& Row)
            { return Row.FinalLevel < 1 || Row.bLeft; }),
            TEXT("C09"), TEXT("The frozen result lists every contender with a committed final level"));
        Summary->SetNumberField(TEXT("first_solo_since_ms"), double(FirstSoloSinceMs));
        Summary->SetNumberField(TEXT("final_solo_since_ms"), double(View.SoloSinceMs));
        Summary->SetNumberField(TEXT("ended_at_ms"), double(Result.EndedAtMs));
        Summary->SetNumberField(TEXT("epoch"), double(View.CaptureEpoch));
        Summary->SetStringField(TEXT("winner"), Result.Winner.ToString(EGuidFormats::Digits));
        if (Mode)
        {
            Check(!Mode->IsGameplayWriteOpen(Mode->GetMatchContext()) && Mode->ReadPublicMatch().ResultId == Result.ResultId,
                TEXT("C10"), TEXT("The authority gate is closed by the same result it published"));
        }
        if (!Passed || !Mark(Role + TEXT("Results.json"))) { return Finish(); }
        Stage = ELDCaptureProbeStage::Results;
        return true;
    }
    case ELDCaptureProbeStage::Results:
        if (Role == TEXT("Host") && !ReturnSent && AllMarkers(TEXT("Results.json")))
        {
            PC->ReturnDemoToLobby();
            ReturnSent = true;
        }
        if (Lobby.Generation == FirstGeneration || Lobby.Phase != ELDMatchPhase::Lobby
            || View.Generation != Lobby.Generation || View.Phase != ELDMatchPhase::Lobby || PC->GetPawn()) { return true; }
        Check(Lobby.Members.Num() == 3 && !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Row) { return Row.Ready; }),
            TEXT("C11"), TEXT("The same connections return to a new, unready Lobby generation"));
        Check(View.CircleState == ELDCircleState::NotOpen && !View.ResultId.IsValid() && !View.StartedAtMs,
            TEXT("C12"), TEXT("The new Lobby carries no capture state or result"));
        SecondGeneration = Lobby.Generation;
        Summary->SetNumberField(TEXT("next_generation"), double(SecondGeneration));
        if (!Passed) { return Finish(); }
        ReadySent = false;
        Stage = ELDCaptureProbeStage::SecondLobby;
        return true;
    case ELDCaptureProbeStage::SecondLobby:
    case ELDCaptureProbeStage::SecondMatch:
    {
        // An aborted start would retire the room into yet another generation.
        if (Lobby.Generation != SecondGeneration || (View.Generation && View.Generation != SecondGeneration))
        {
            Check(false, Mode ? TEXT("R01") : TEXT("R02"), TEXT("The second match runs in the new generation without an aborted start"));
            Error = FString::Printf(TEXT("The room left generation %u for %u before the second match was checked."),
                SecondGeneration, Lobby.Generation);
            return Finish();
        }
        if (Stage == ELDCaptureProbeStage::SecondLobby)
        {
            if (View.Generation == SecondGeneration && View.Phase == ELDMatchPhase::Development)
            {
                Stage = ELDCaptureProbeStage::SecondMatch;
                return true;
            }
            if (Lobby.Phase != ELDMatchPhase::Lobby || Lobby.Members.Num() != 3 || !EnsureReady(*PC, Lobby)) { return true; }
            if (!Mode || SecondStartSent || !AllHumansReady(Lobby)) { return true; }
            if (!Lobby.CanStart || !Lobby.StartBlocker.IsEmpty())
            {
                Check(false, TEXT("R01"), TEXT("The ready second Lobby starts the second match normally"));
                Error = TEXT("The second normal start is blocked: ") + Lobby.StartBlocker;
                return Finish();
            }
            PC->StartDemoMatch();
            SecondStartSent = true;
            return true;
        }
        // The first frame on which the owner's own views all describe the new match's living first body.
        const FLDProgressionView* Progress = PC->ReadOwnProgression();
        const FLDEquipmentView* Equipment = PC->ReadOwnEquipment();
        const FLDCombatOwnerView& Own = PC->CombatView;
        if (View.Phase != ELDMatchPhase::Development || !Cast<ALDCharacter>(PC->GetPawn())
            || PC->OwnerState.Match != SecondGeneration || PC->OwnerState.LifePhase != ELDLifePhase::Alive
            || !PC->OwnerState.BodyRef.IsValid() || !Progress || !Equipment
            || Own.Generation != SecondGeneration || Own.Life != PC->OwnerState.Life || !Own.bHasBody) { return true; }
        return CheckSecondMatch(*PC, Mode, Lobby, View, *Progress, *Equipment);
    }
    default:
        return true;
    }
}

bool FLDDemoCaptureProbe::CheckSecondMatch(ALDPlayerController& PC, ALDGameMode* Mode, const FLDLobbyView& Lobby,
    const FLDPublicMatchView& View, const FLDProgressionView& Progress, const FLDEquipmentView& Equipment)
{
    const FLDDemoContentDefinitions& Defs = Content->Get();
    const FLDOwnerState& Self = PC.OwnerState;
    const FLDCombatOwnerView& Own = PC.CombatView;
    if (Mode)
    {
        Check(SecondStartSent && Lobby.Phase == ELDMatchPhase::Development,
            TEXT("R01"), TEXT("The Host's normal start opened the second match in the new generation without aborting"));
    }
    Check(Self.Match == SecondGeneration && Self.Life == 1 && Progress.Life == Self.Life
        && FirstBody.IsValid() && Self.BodyRef != FirstBody,
        TEXT("R02"), TEXT("The first Alive frame is the new generation's first life in a fresh body"));
    uint8 StartLevel = 0;
    const bool bStartLevel = LDProgression::LevelForXP(Defs.Progression, Defs.Progression.XPStart, StartLevel) == ELDGrowthReason::None;
    Check(bStartLevel && Progress.Level == StartLevel && Progress.CumulativeXP == Defs.Progression.XPStart
        && Equipment.Gold == Defs.Parameters.StartingGold,
        TEXT("R03"), TEXT("Level, XP and gold are the content's opening values (start level, XP.Start, SHOP.StartingGold)"));
    bool bSlotsEmpty = Equipment.Slots.Num() == Defs.Progression.EquipmentSlots;
    for (const FLDEquipmentSlotView& Slot : Equipment.Slots) { bSlotsEmpty &= !Slot.bOccupied; }
    const bool bNoEntries = !Own.Entries.ContainsByPredicate([](const FLDCombatEntryView& Entry) { return !Entry.DefinitionId.IsNone(); });
    Check(Progress.Skills.IsEmpty() && Progress.AttributeCards.IsEmpty() && Progress.EarnedThrough == 0
        && Progress.ConsumedThrough == 0 && Progress.PendingChoiceCount == 0 && !Progress.bHasOffer
        && bSlotsEmpty && bNoEntries && Own.EquipmentPassives.IsEmpty() && PC.ReadOwnShop() == nullptr,
        TEXT("R04"), TEXT("No learned skill, attribute card, equipment, pending choice or shop session"));
    Check(Self.ProtectedUntilMs == 0 && Own.Statuses.IsEmpty() && Own.ShieldP2 == 0 && !Own.bRooted
        && Own.MaxHPP2 > 0 && Own.HPP2 == Own.MaxHPP2,
        TEXT("R05"), TEXT("The initial body has full HP and no shield, root, protection or other status"));
    const bool bTracksReady = !Own.Entries.ContainsByPredicate([&Own](const FLDCombatEntryView& Entry)
    { return Entry.IssuedCooldownId.IsValid() || Entry.ReadyAtMs > Own.AsOfMs; });
    Check(bTracksReady && Own.NextAttackAtMs <= Own.AsOfMs && Own.ActionEntry == ELDCombatEntry::None
        && Own.ActionPhase == ELDCombatPhase::Finished,
        TEXT("R06"), TEXT("Every cooldown source is ready with no issued cooldown track carried from the first match"));
    Summary->SetNumberField(TEXT("second_generation"), double(SecondGeneration));
    if (Mode)
    {
        // Host only, through the public camp, combat and core read APIs.
        const ALDCampDirector* Camps = Mode->Camps.Get();
        const FLDCampMatchState* State = Camps ? &Camps->GetState() : nullptr;
        const FLDCampRoster* Roster = State ? State->GetRoster() : nullptr;
        int32 Expected = 0, Fresh = 0;
        bool bCamps = State && State->IsActive() && State->GetMatch() == Mode->GetMatchContext() && Roster && Mode->Combat
            && State->NumCamps() == 5 && Roster->Camps.Num() == State->NumCamps() && Defs.Camps.Num() == State->NumCamps();
        for (int32 Camp = 0; bCamps && Camp < State->NumCamps(); ++Camp)
        {
            const FLDCampGroupState& Group = *State->FindCamp(Camp);
            Expected += Roster->Camps[Camp].Members.Num();
            bCamps &= Group.Generation == 1 && !Group.bRespawnPending && Group.ClearedAtMs == 0 && Group.RespawnDueMs == 0
                && Group.Members.Num() == Roster->Camps[Camp].Members.Num();
            for (const FLDCampMemberState& Member : Group.Members)
            {
                FLDCombatBodyRead Read;
                Fresh += Member.bAlive && Member.Life == 1 && !FirstCampMembers.Contains(Member.EntityId)
                    && Member.Brain.Phase == ELDCampBrainPhase::Idle && !Member.Brain.LostTargetSinceSeconds.IsSet()
                    && Member.HostileSources.IsEmpty() && Member.ReturnEpoch == 0 && Member.ResetThroughEpoch == 0
                    && Mode->Combat->ReadCombatBody(Member.Body, Read) && !Read.bPendingDeath && Read.MaxHPP2 > 0
                    && Read.HPP2 == Read.MaxHPP2 && Read.ShieldP2 == 0 && Read.RootedUntilMs == 0 ? 1 : 0;
            }
        }
        Check(bCamps && Expected > 0 && Fresh == Expected && FirstCampMembers.Num() == Expected,
            TEXT("R07"), TEXT("All five camps hold fresh first lives at full HP, idle, with no aggro or first-match identity"));
        Summary->SetNumberField(TEXT("second_fresh_camp_members"), Fresh);
        Summary->SetNumberField(TEXT("first_camp_members"), FirstCampMembers.Num());
        const FLDPublicMatchView Authority = Mode->ReadPublicMatch();
        auto Clean = [this](const FLDPublicMatchView& Match)
        {
            return Match.Generation == SecondGeneration && Match.Phase == ELDMatchPhase::Development
                && Match.CircleState == ELDCircleState::NotOpen && !Match.Occupier.IsValid() && !Match.SoloSinceMs
                && !Match.DueAtMs && !Match.CaptureEpoch && !Match.ResultId.IsValid()
                && Match.OpenAtMs == Match.StartedAtMs + DevelopmentMs;
        };
        const auto* Public = PC.GetWorld()->GetGameState<ALDGameState>();
        Check(Clean(View) && Clean(Authority) && Public && Public->KillFeed.Generation == SecondGeneration
            && Public->KillFeed.Notices.IsEmpty()
            && !(Public->MatchResult.Generation == SecondGeneration && Public->MatchResult.ResultId.IsValid())
            && Mode->IsGameplayWriteOpen(Mode->GetMatchContext()),
            TEXT("R08"), TEXT("Capture restarts in Development with no holder, progress, contest, kill or result"));
        // A request stamped with the first match's generation would end the second match if it were accepted.
        const FLDLobbyView LobbyBefore = Mode->ReadLobby();
        const uint64 OwnerRevision = Self.Revision;
        FLDSessionRequest Stale;
        Stale.Key.Generation = FirstGeneration;
        Stale.Key.RequestSequence = 1;
        Stale.Command = ELDSessionCommand::EndMatch;
        Stale.ExpectedRosterRevision = LobbyBefore.RosterRevision;
        const FLDSessionReceipt Receipt = Mode->ProcessSessionCommand(&PC, Stale);
        const FLDPublicMatchView After = Mode->ReadPublicMatch();
        const FLDLobbyView LobbyAfter = Mode->ReadLobby();
        Check(Receipt.Outcome == ELDSessionOutcome::Rejected && Receipt.Reason == ELDSessionReject::StaleMatch
            && After.Generation == Authority.Generation && After.PublicRevision == Authority.PublicRevision
            && After.Phase == ELDMatchPhase::Development && After.CaptureEpoch == Authority.CaptureEpoch
            && LobbyAfter.RosterRevision == LobbyBefore.RosterRevision && LobbyAfter.PublicRevision == LobbyBefore.PublicRevision
            && PC.OwnerState.Revision == OwnerRevision && Mode->IsGameplayWriteOpen(Mode->GetMatchContext()),
            TEXT("R09"), TEXT("A first-match EndMatch request is rejected as StaleMatch and changes nothing"));
    }
    Stage = ELDCaptureProbeStage::Done;
    return Finish();
}

void LDDemoTests::StartCaptureProbe(ALDPlayerController& Controller)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoCaptureProbe")) || !Controller.IsLocalController()
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap) { return; }
    auto Probe = MakeShared<FLDDemoCaptureProbe>();
    int32 Timeout = 480;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoCaptureTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoCaptureOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoCaptureRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1") && Probe->Role != TEXT("Client2"))
        || Timeout < 120 || Timeout > 900
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_CAPTURE_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    Probe->Controller = &Controller;
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .1f);
}
#endif
