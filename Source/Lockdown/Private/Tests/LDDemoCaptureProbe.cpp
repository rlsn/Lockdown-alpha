#include "Tests/LDDemoCaptureProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/LDCharacter.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "UI/LDChoicesPanel.h"
#include "UI/LDHUD.h"
#include "Vision/LDWorldViewRelay.h"
#include "World/LDCaptureMarker.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

enum class ELDShowdownProbeStage : uint8 { Join, Lobby, Running, Results, Returned, SecondStart };

// Three real owners and three production opponents run the normal timeline. Only normal ready, navigation, attack
// and session intents are sent; no teleport, clock skip, damage injection or result override is used.
class FLDDemoCaptureProbe : public TSharedFromThis<FLDDemoCaptureProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output, Role, Error;
    double Deadline = 0, NextInputAt = 0, CoreRadiusCm = 0;
    ELDShowdownProbeStage Stage = ELDShowdownProbeStage::Join;
    bool bPassed = true, bWritten = false, bStartSent = false, bReturnSent = false;
    bool bOpeningSeen = false, bFinalRestSeen = false, bFinalSeen = false, bMarkerSeen = false;
    bool bFinalModOffered = false, bFinalModSent = false;
    uint8 FieldMask = 0;
    uint32 Generation = 0, NextGeneration = 0;
    int64 FinalXP = 0, FinalEndsAtMs = 0;
    uint64 FinalBuildRevision = 0;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();

    static const TCHAR* Name(const FString& Which)
    {
        return Which == TEXT("Host") ? TEXT("抢圈 房主") : Which == TEXT("Client1") ? TEXT("抢圈甲") : TEXT("抢圈乙");
    }

    bool Save(const TSharedRef<FJsonObject>& Object, const FString& File) const
    {
        FString Text;
        return FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text))
            && FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Output, File));
    }

    bool Mark(const TCHAR* Suffix)
    {
        if (Save(MakeShared<FJsonObject>(), Role + Suffix)) { return true; }
        bPassed = false;
        Error = TEXT("Cannot save the role barrier.");
        return false;
    }

    bool AllMarked(const TCHAR* Suffix) const
    {
        for (const TCHAR* Which : {TEXT("Host"), TEXT("Client1"), TEXT("Client2")})
        {
            if (!FPaths::FileExists(FPaths::Combine(Output, FString(Which) + Suffix))) { return false; }
        }
        return true;
    }

    bool Check(bool bValue, const TCHAR* Id, const TCHAR* Description)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("id"), Id);
        Row->SetStringField(TEXT("name"), Description);
        Row->SetBoolField(TEXT("passed"), bValue);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        if (!bValue) { bPassed = false; if (Error.IsEmpty()) { Error = FString(Id) + TEXT(": ") + Description; } }
        return bValue;
    }

    bool Finish()
    {
        auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), bPassed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetStringField(TEXT("scope"), TEXT("Normal three-field timeline, three owning connections plus three production AI, final-rest active selection and build lock for all six, registered six-entry final arena, objective termination, growth freeze, Lobby return and fresh second start. Not distinct FIN effects, graphical quality or exhaustive respawn/cutoff choreography."));
        Report->SetArrayField(TEXT("checks"), Checks);
        Report->SetObjectField(TEXT("summary"), Summary);
        bWritten = Save(Report, Role + TEXT(".json"));
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_CAPTURE_PROBE role=%s passed=%d detail=%s"), *Role, bPassed && bWritten, *Error);
        if (!bPassed || !bWritten) { FPlatformMisc::RequestExitWithStatus(false, 2); return false; }
        return true;
    }

    bool Tick()
    {
        if (bWritten)
        {
            const TCHAR* FinishFile = Role == TEXT("Host") ? TEXT("Finish") : TEXT("FinishClients");
            if (FPaths::FileExists(FPaths::Combine(Output, FinishFile)))
            { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
            return true;
        }
        const double Local = FPlatformTime::Seconds();
        if (Local >= Deadline)
        {
            Check(false, TEXT("T01"), TEXT("The six-contender showdown completes within the bounded scenario deadline"));
            Error += FString::Printf(TEXT(" stage=%u field_mask=%u final_seen=%d"), uint8(Stage), FieldMask, bFinalSeen);
            return Finish();
        }
        auto* PC = Controller.Get();
        auto* World = PC ? PC->GetWorld() : nullptr;
        auto* Public = World ? World->GetGameState<ALDGameState>() : nullptr;
        if (!PC || !Public) { return true; }
        const auto& Lobby = Public->DemoLobby;
        const auto& View = Public->PublicMatch;
        auto* Mode = World->GetAuthGameMode<ALDGameMode>();
        const bool bHost = Role == TEXT("Host");
        const bool bInput = Local >= NextInputAt;
        const auto CountKind = [&Lobby](ELDContenderKind Kind)
        { return Lobby.Members.FilterByPredicate([Kind](const FLDLobbyMember& Row) { return Row.Kind == Kind; }).Num(); };

        if (Stage == ELDShowdownProbeStage::Join)
        {
            if (!Lobby.Generation || !PC->OwnerState.ContenderId.IsValid()) { return true; }
            Check(bHost ? World->GetNetMode() == NM_ListenServer && Mode != nullptr
                : World->GetNetMode() == NM_Client && PC->GetNetConnection() && !PC->HasAuthority(),
                TEXT("C01"), TEXT("A real Listen Host or remote owning connection runs this role"));
            Generation = Lobby.Generation;
            if (!Mark(TEXT("Joined.json")) || !bPassed) { return Finish(); }
            Stage = ELDShowdownProbeStage::Lobby;
        }
        if (Stage == ELDShowdownProbeStage::Lobby || Stage == ELDShowdownProbeStage::Returned)
        {
            const bool bSecond = Stage == ELDShowdownProbeStage::Returned;
            if (!AllMarked(bSecond ? TEXT("Lobby2.json") : TEXT("Joined.json")) || CountKind(ELDContenderKind::Human) != 3) { return true; }
            if (Lobby.AICount != 3)
            {
                if (bHost && bInput) { PC->SetDemoAICount(3); NextInputAt = Local + 2.; }
                return true;
            }
            const auto* Self = Lobby.Members.FindByPredicate([PC](const FLDLobbyMember& Row)
            { return Row.ContenderId == PC->OwnerState.ContenderId; });
            if (!Self || !Self->Ready)
            {
                if (bInput) { PC->SetDemoReady(true); NextInputAt = Local + 2.; }
                return true;
            }
            if (bHost && !bStartSent)
            {
                if (Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Row)
                    { return Row.Kind == ELDContenderKind::Human && !Row.Ready; })) { return true; }
                if (!Lobby.CanStart) { Error = Lobby.StartBlocker; Check(false, TEXT("P01"), TEXT("The ready room is startable")); return Finish(); }
                PC->StartDemoMatch();
                bStartSent = true;
            }
            if (!bSecond)
            {
                Check(Lobby.Members.Num() == 6 && CountKind(ELDContenderKind::OpponentAI) == 3
                    && Lobby.Members.ContainsByPredicate([this](const FLDLobbyMember& Row) { return Row.Name == Name(Role); }),
                    TEXT("C02"), TEXT("The locked room has three named humans and three distinct production AI"));
            }
            Stage = bSecond ? ELDShowdownProbeStage::SecondStart : ELDShowdownProbeStage::Running;
        }
        if (Stage == ELDShowdownProbeStage::SecondStart)
        {
            if (View.Generation != NextGeneration || View.Phase != ELDMatchPhase::Rest || PC->OwnerState.Match != NextGeneration
                || PC->OwnerState.LifePhase != ELDLifePhase::Alive) { return true; }
            Check(View.Timeline.RestIndex == 0 && !View.Showdown.bStarted && !View.GraffitiOwner.IsValid()
                && !View.ResultId.IsValid() && Public->KillFeed.Notices.IsEmpty(),
                TEXT("C13"), TEXT("A second normal match starts in fresh R0 without stale showdown, result or kill state"));
            Summary->SetNumberField(TEXT("second_generation"), NextGeneration);
            return Finish();
        }
        if (Stage == ELDShowdownProbeStage::Results)
        {
            if (bHost && !bReturnSent && AllMarked(TEXT("Results.json"))) { PC->ReturnDemoToLobby(); bReturnSent = true; }
            if (Lobby.Generation == Generation || Lobby.Phase != ELDMatchPhase::Lobby || View.Phase != ELDMatchPhase::Lobby
                || View.Generation != Lobby.Generation || PC->GetPawn()) { return true; }
            Check(Lobby.Members.Num() == 6 && !View.Showdown.bStarted && !View.GraffitiOwner.IsValid() && !View.ResultId.IsValid()
                && !Public->GetCaptureMarker(),
                TEXT("C12"), TEXT("The same six-member room returns to a new Lobby without a body or stale final state"));
            NextGeneration = Lobby.Generation;
            Summary->SetNumberField(TEXT("next_generation"), NextGeneration);
            bStartSent = false;
            NextInputAt = 0;
            if (!Mark(TEXT("Lobby2.json")) || !bPassed) { return Finish(); }
            Stage = ELDShowdownProbeStage::Returned;
            return true;
        }
        if (Stage != ELDShowdownProbeStage::Running || View.Generation != Generation) { return true; }
        if (!bOpeningSeen && View.Phase == ELDMatchPhase::Rest)
        {
            Check(View.Timeline.RestIndex == 0 && View.Timeline.Step == ELDRestStep::Build && !View.Showdown.bStarted,
                TEXT("C03"), TEXT("Normal start enters opening R0 rather than a final-field shortcut"));
            bOpeningSeen = true;
        }
        if (View.Timeline.Stage == ELDMatchStage::Field && View.Timeline.FieldSegment < 3)
        { FieldMask |= uint8(1u << View.Timeline.FieldSegment); }
        if (View.Phase == ELDMatchPhase::Rest && View.Timeline.RestIndex == 3) { bFinalRestSeen = true; }
        const auto* Growth = PC->ReadOwnProgression();
        ULDChoicesPanel* Panel = PC->HUDUI ? PC->HUDUI->ChoicesPanel.Get() : nullptr;
        if (View.Phase == ELDMatchPhase::Rest && Growth && Growth->Generation == Generation && Growth->bHasActiveOffer
            && bInput && Panel)
        {
            if (Panel->ChooseOffer(0)) { NextInputAt = Local + 1.; }
        }
        if (View.Phase == ELDMatchPhase::Rest && View.Timeline.RestIndex == 3 && Growth && Growth->Generation == Generation
            && Growth->bFinalModPending && Growth->FinalModOptions.Num() == 3)
        {
            bFinalModOffered = true;
            if (!bFinalModSent && bInput && Panel && !Growth->bHasActiveOffer)
            {
                for (int32 Index = 0; Index < Growth->FinalModOptions.Num(); ++Index)
                {
                    const FName Option = Growth->FinalModOptions[Index];
                    if (!Growth->PendingBlueprints.Contains(Option) && !Growth->Actives.ContainsByPredicate(
                        [Option](const FLDActiveSlotView& Active) { return Active.DefinitionId == Option; }))
                    {
                        bFinalModSent = Panel->ChooseFinalMod(Index);
                        break;
                    }
                }
                NextInputAt = Local + 1.;
            }
        }
        if (View.Phase == ELDMatchPhase::Rest && PC->OwnerState.bCanRestReady && !PC->OwnerState.bRestReady && bInput
            && (View.Timeline.RestIndex != 3 || (Growth && Growth->bBuildLocked && !Growth->FinalModChosen.IsNone())))
        { PC->SubmitRestReady(true); NextInputAt = Local + 2.; }
        if (View.Showdown.bStarted && !bFinalSeen && PC->OwnerState.Match == Generation
            && PC->OwnerState.LifePhase == ELDLifePhase::Alive && Growth && Growth->Generation == Generation)
        {
            const auto* Body = Cast<ALDCharacter>(PC->GetPawn());
            if (!Body || FVector::DistSquared2D(Body->GetActorLocation(), View.Showdown.CentreCm)
                > FMath::Square(View.Showdown.InitialRadiusCm)) { return true; }
            Check(bOpeningSeen && FieldMask == 7 && bFinalRestSeen && View.Timeline.Stage == ELDMatchStage::FinalField,
                TEXT("C04"), TEXT("Three normal growth fields and final rest precede final-field entry"));
            Check(bFinalModOffered && bFinalModSent && Growth->bBuildLocked && !Growth->bFinalModPending
                && !Growth->FinalModChosen.IsNone() && Growth->Actives.ContainsByPredicate(
                    [Growth](const FLDActiveSlotView& Active) { return Active.DefinitionId == Growth->FinalModChosen && Active.Slot == Growth->FinalModSlot; }),
                TEXT("C04F"), TEXT("The owner chose a final-rest active into its slot and the build is locked"));
            TSharedPtr<const FLDDemoContentSnapshot> Content;
            FLDShowdownParameters Settings;
            const bool bConfigured=LDDemoEntry::LoadRuntimeContent(Content,Error)
                && FLDShowdownParameters::FromContent(Content->Get().Parameters,Settings,Error);
            if (bConfigured)
            {
                const auto& Radius=Content->Get().Parameters.CaptureRadiusCm;
                CoreRadiusCm=double(Radius.Numerator)/double(Radius.Denominator);
            }
            Check(bConfigured && View.OpenAtMs - View.Showdown.StartedAtMs == Settings.UnlockDelayMs
                && View.Showdown.EndsAtMs - View.Showdown.StartedAtMs == Settings.DurationMs
                && View.Showdown.GuardMs == Settings.GuardMs,
                TEXT("C05"), TEXT("Published unlock, guard and terminal clocks match the configured showdown"));
            FinalXP = Growth->CumulativeXP;
            FinalBuildRevision = Growth->BuildRevision;
            FinalEndsAtMs = View.Showdown.EndsAtMs;
            Check(PC->OwnerState.BodyRef.IsValid(), TEXT("C07"), TEXT("The owner stands as a live authoritative body inside the separate final arena"));
            if (Mode)
            {
                TArray<FVector> Feet;
                bool bSix = Mode->GetRosterOrder().Num() == 6;
                bool bSixChoices = bSix && Mode->Development;
                for (const FGuid& Id : Mode->GetRosterOrder())
                {
                    FLDCombatBodyRef Ref;
                    FVector Foot = FVector::ZeroVector;
                    bSix &= Mode->ReadLivingBody(Id, Ref, Foot) && Ref.IsValid()
                        && FVector::DistSquared2D(Foot, View.Showdown.CentreCm) <= FMath::Square(View.Showdown.InitialRadiusCm);
                    bSix &= !Feet.ContainsByPredicate([&Foot](const FVector& Other) { return FVector::DistSquared2D(Foot, Other) < 1.; });
                    Feet.Add(Foot);
                    if (Mode->Development)
                    {
                        const FLDProgressionView Build = Mode->Development->ReadProgression(Id);
                        bSixChoices &= Build.bValid && Build.bBuildLocked && !Build.bFinalModPending
                            && !Build.FinalModChosen.IsNone() && Build.Actives.ContainsByPredicate(
                                [&Build](const FLDActiveSlotView& Active)
                                { return Active.DefinitionId == Build.FinalModChosen && Active.Slot == Build.FinalModSlot; });
                    }
                }
                Check(bSix, TEXT("A01"), TEXT("All six real authority bodies enter distinct legal positions inside the final ring"));
                Check(bSixChoices, TEXT("A03"), TEXT("All six contenders chose a final-rest active and locked their build"));
            }
            bFinalSeen = true;
            if (!bPassed) { return Finish(); }
        }
        if (bFinalSeen)
        {
            const auto* Marker = Public->GetCaptureMarker();
            bMarkerSeen |= Marker && Marker->GetGeneration() == Generation
                && Marker->GetCenter().Equals(View.Showdown.CentreCm, 1.) && Marker->IsPresentationOnly();
            if (View.Phase != ELDMatchPhase::Results && bInput && PC->CanWorldCommand() && PC->Navigation)
            {
                const auto* Relay = PC->LocalView();
                const FLDObservedFrameRead Read = Relay ? Relay->ReadObservedFrame() : FLDObservedFrameRead();
                const auto* Target = Read.Frame && Read.Status == ELDObservedViewStatus::Current
                    ? Read.Frame->Units.FindByPredicate([&View,PC,this](const FLDObservedUnitView& Unit)
                    { return Unit.Kind == ELDObservedUnitKind::Contender && Unit.ContenderId != PC->OwnerState.ContenderId
                        && Unit.Health.CurrentP2 > 0
                        && FVector::DistSquared2D(Unit.FeetCm, View.Showdown.CentreCm) <= FMath::Square(CoreRadiusCm); }) : nullptr;
                const auto* Registry = World->GetSubsystem<ULDMapRegistry>();
                const auto* Anchor = Registry ? Registry->Resolve(TEXT("final_capture"), Registry->Generation) : nullptr;
                if (Target) { PC->Navigation->AttackUnit(Target->Handle, Target->ObservationEpoch); }
                else if (Anchor) { PC->Navigation->NavigateGround(Anchor->Position, Anchor->Surface); }
                NextInputAt = Local + 2.;
            }
        }
        if (View.Phase != ELDMatchPhase::Results || Public->MatchResult.ResultId != View.ResultId) { return true; }
        const auto& Result = Public->MatchResult;
        Check(bFinalSeen && Growth && Growth->CumulativeXP == FinalXP && Growth->BuildRevision == FinalBuildRevision,
            TEXT("C06"), TEXT("Final combat grants no ordinary XP or build mutation"));
        Check(bMarkerSeen, TEXT("C08"), TEXT("The client draws the registered final flag boundary as presentation only"));
        Check(Result.Reason == ELDMatchEndReason::GuardSucceeded || Result.Reason == ELDMatchEndReason::ObjectiveTimeout
            || Result.Reason == ELDMatchEndReason::NoChampion,
            TEXT("C09"), TEXT("The six-player FFA terminates through its objective or bounded no-champion rule, never Host end"));
        Check(Result.Rows.Num() == 6 && Result.EndedAtMs <= FinalEndsAtMs
            && !Result.Rows.ContainsByPredicate([](const FLDResultRow& Row) { return Row.FinalLevel < 1 || Row.BankedGuardMs < 0; }),
            TEXT("C10"), TEXT("The server freezes six result rows and objective contributions no later than its deadline"));
        Check(!Result.Winner.IsValid() || Result.Rows.ContainsByPredicate([&Result](const FLDResultRow& Row)
            { return Row.ContenderId == Result.Winner && !Row.bLeft && (Row.BankedGuardMs > 0 || Row.CompletedSprays > 0); }),
            TEXT("C11"), TEXT("A winner has legal objective contribution; kills alone cannot crown a survivor"));
        if (Mode)
        { Check(!Mode->IsGameplayWriteOpen(Mode->GetMatchContext()), TEXT("A02"), TEXT("The unique published result closes the authority write gate")); }
        Summary->SetStringField(TEXT("winner"), Result.Winner.ToString(EGuidFormats::Digits));
        Summary->SetNumberField(TEXT("ended_at_ms"), double(Result.EndedAtMs));
        Summary->SetNumberField(TEXT("reason"), uint8(Result.Reason));
        if (!bPassed || !Mark(TEXT("Results.json"))) { return Finish(); }
        Stage = ELDShowdownProbeStage::Results;
        return true;
    }
};

void LDDemoTests::StartCaptureProbe(ALDPlayerController& Controller)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoCaptureProbe")) || !Controller.IsLocalController()
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap) { return; }
    auto Probe = MakeShared<FLDDemoCaptureProbe>();
    int32 Timeout = 1800;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoCaptureTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoCaptureOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoCaptureRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1") && Probe->Role != TEXT("Client2"))
        || Timeout < 120 || Timeout > 1800
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_CAPTURE_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    Probe->Controller = &Controller;
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .1f);
}
#endif
