#include "Tests/LDDemoCaptureProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// Host stays at its spawn, Client1 captures, Client2 contests once mid-segment and leaves again.
enum class ELDCaptureProbeStage : uint8 { Join, Lobby, Starting, Match, Results, Done };

class FLDDemoCaptureProbe : public TSharedFromThis<FLDDemoCaptureProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output, Role, Error;
    double Deadline = 0, StageAt = 0;
    ELDCaptureProbeStage Stage = ELDCaptureProbeStage::Join;
    int32 ContestStep = 0;
    bool Passed = true, Written = false, ReadySent = false, IdentitySaved = false, StartSent = false, ReturnSent = false;
    bool SawContest = false;
    uint32 FirstGeneration = 0;
    FGuid CapturerId;
    int64 FirstSoloSinceMs = -1;
    int64 HoldMs = 0, DevelopmentMs = 0;
    FVector Center = FVector::ZeroVector, Staging = FVector::ZeroVector, Inside = FVector::ZeroVector;
    FName Surface;
    double RadiusCm = 0;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();

    bool Check(bool Value, const TCHAR* Name)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Name);
        Row->SetBoolField(TEXT("passed"), Value);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        Passed &= Value;
        if (!Value && Error.IsEmpty()) { Error = Name; }
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
        Report->SetStringField(TEXT("scope"), TEXT("DEV-only normal start through providers, real owned capture, contest reset, unique result and same-world return to a new Lobby generation. Not 3+3 or AI."));
        Report->SetObjectField(TEXT("summary"), Summary);
        Report->SetArrayField(TEXT("checks"), Checks);
        Written = Save(Report, Role + TEXT(".json"));
        UE_LOG(LogTemp, Display, TEXT("LD_DEMO_CAPTURE_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
        if (!Passed || !Written) { FPlatformMisc::RequestExitWithStatus(false, 2); return false; }
        return true;
    }

    void Observe(const FLDPublicMatchView& View)
    {
        if (View.Phase != ELDMatchPhase::CaptureOpen) { return; }
        SawContest |= View.CircleState == ELDCircleState::Contested;
        if (View.CircleState == ELDCircleState::Solo && View.Occupier == CapturerId && FirstSoloSinceMs < 0)
        { FirstSoloSinceMs = View.SoloSinceMs; }
    }

    bool Tick();
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
        Check(false, TEXT("Capture scenario completed within its bounded deadline"));
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
            Check(false, TEXT("Approved content and the registered capture anchor resolve"));
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
    switch (Stage)
    {
    case ELDCaptureProbeStage::Join:
        if (!PC->IsLocalController() || !Lobby.Generation) { return true; }
        Check(Role == TEXT("Host") ? World->GetNetMode() == NM_ListenServer
            : World->GetNetMode() == NM_Client && PC->GetNetConnection() && !PC->HasAuthority(),
            TEXT("Each role is a real Listen Host or owning client connection"));
        FirstGeneration = Lobby.Generation;
        if (!Mark(Role + TEXT("Joined.json"))) { return Finish(); }
        Stage = ELDCaptureProbeStage::Lobby;
        return true;
    case ELDCaptureProbeStage::Lobby:
    {
        if (!AllMarkers(TEXT("Joined.json")) || Lobby.Members.Num() != 3) { return true; }
        const FLDLobbyMember* Self = Lobby.Members.FindByPredicate([PC](const FLDLobbyMember& Row)
        { return Row.ContenderId == PC->OwnerState.ContenderId; });
        if (!Self || !Self->Ready)
        {
            // A ready raced by the last join is refused as stale; send it again for the final roster.
            if (!ReadySent || FPlatformTime::Seconds() - StageAt >= 3.)
            {
                PC->SetDemoReady(true);
                ReadySent = true;
                StageAt = FPlatformTime::Seconds();
            }
            return true;
        }
        if (!IdentitySaved)
        {
            if (!SaveIdentity(PC->OwnerState.ContenderId)) { return Finish(); }
            IdentitySaved = true;
        }
        if (Role != TEXT("Host")) { Stage = ELDCaptureProbeStage::Starting; return true; }
        const bool AllReady = !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Row)
        { return Row.Kind == ELDContenderKind::Human && !Row.Ready; });
        if (!AllReady || StartSent || !AllMarkers(TEXT("Id.json"))) { return true; }
        if (!Check(Lobby.CanStart && Lobby.StartBlocker.IsEmpty(), TEXT("Every provider is wired: the ready room may start normally")))
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
            TEXT("The capturing contender is on the locked roster"));
        Check(View.Generation == FirstGeneration && View.OpenAtMs == View.StartedAtMs + DevelopmentMs
            && View.CircleState == ELDCircleState::NotOpen && !View.Occupier.IsValid(),
            TEXT("Normal start opens the approved development phase with a closed circle"));
        FVector Direction = Body->GetActorLocation() - Center;
        Direction.Z = 0;
        Direction = Direction.GetSafeNormal();
        Check(!Direction.IsNearlyZero(), TEXT("Spawned body is outside the circle centre"));
        Staging = Center + Direction * (RadiusCm + 300.);
        Inside = Center + Direction * (RadiusCm - 250.);
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
            TEXT("Continuous exclusive capture locks the capturer as the unique winner"));
        Check(SawContest && FirstSoloSinceMs >= 0 && View.SoloSinceMs > FirstSoloSinceMs,
            TEXT("A real contest broke the first segment and the occupier restarted a complete hold"));
        Check(Result.EndedAtMs == View.DueAtMs && View.DueAtMs - View.SoloSinceMs == HoldMs
            && Result.DurationMs == Result.EndedAtMs - Result.StartedAtMs,
            TEXT("Completion is the approved hold after the last uncontested start, timed by the server"));
        Check(Result.Rows.Num() == 3 && !Result.Rows.ContainsByPredicate([](const FLDResultRow& Row)
            { return Row.FinalLevel < 1 || Row.bLeft; }),
            TEXT("The frozen result lists every contender with a committed final level"));
        Summary->SetNumberField(TEXT("first_solo_since_ms"), double(FirstSoloSinceMs));
        Summary->SetNumberField(TEXT("final_solo_since_ms"), double(View.SoloSinceMs));
        Summary->SetNumberField(TEXT("ended_at_ms"), double(Result.EndedAtMs));
        Summary->SetNumberField(TEXT("epoch"), double(View.CaptureEpoch));
        Summary->SetStringField(TEXT("winner"), Result.Winner.ToString(EGuidFormats::Digits));
        if (auto* Mode = World->GetAuthGameMode<ALDGameMode>())
        {
            Check(!Mode->IsGameplayWriteOpen(Mode->GetMatchContext()) && Mode->ReadPublicMatch().ResultId == Result.ResultId,
                TEXT("The authority gate is closed by the same result it published"));
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
            TEXT("The same connections return to a new, unready Lobby generation"));
        Check(View.CircleState == ELDCircleState::NotOpen && !View.ResultId.IsValid() && !View.StartedAtMs,
            TEXT("The new Lobby carries no capture state or result"));
        Summary->SetNumberField(TEXT("next_generation"), double(Lobby.Generation));
        Stage = ELDCaptureProbeStage::Done;
        return Finish();
    default:
        return true;
    }
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
