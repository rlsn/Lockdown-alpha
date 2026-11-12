#include "Tests/LDDemoPerfBudgetProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Art/LDArtCatalog.h"
#include "Art/LDArtSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/LDCameraSettings.h"
#include "Character/LDCharacter.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Development/LDDemoContentIds.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "Scalability.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace
{
    // Frame budgets approved for this machine (docs/demo/CONTENT_AND_BALANCE.md, 性能预算): the low preset holds
    // 120 FPS and the high preset 60 FPS with a full crowd on screen.
    struct FPerfTier { const TCHAR* Name; int32 Level; double BudgetMs; };
    const FPerfTier PerfTiers[] = { {TEXT("low"), 0, 1000. / 120.}, {TEXT("high"), 2, 1000. / 60.} };
    // Open floor south of the flagpole that the art capture also frames.
    const FVector PerfFocus(0., -1100., 0.);
    constexpr double WarmSeconds = 3., SampleSeconds = 6.;
    // The most opponent AI a room takes (FLDSessionCommand::SetAICount).
    constexpr uint8 MatchAI = 3;

    struct FPerfBody
    {
        TWeakObjectPtr<AActor> Actor;
        TWeakObjectPtr<ULDHumanoidVisualComponent> Visual;
        FVector Centre = FVector::ZeroVector;
        double Phase = 0.;
        bool bRuns = false;
    };

    struct FPerfSample { double FrameMs, GameMs, RenderMs, RhiMs, GpuMs; };

    struct FPerfBudgetProbe
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<ALDPlayerController> Controller;
        TWeakObjectPtr<ACameraActor> Camera;
        FString Directory;
        TArray<FPerfBody> Bodies;
        TArray<TSharedPtr<FJsonValue>> Windows, Checks;
        TArray<FPerfSample> Samples;
        double StartedAt = FPlatformTime::Seconds(), PhaseAt = 0., Clock = 0., SentAt = 0.;
        FVector Focus = PerfFocus;
        uint32 StartedGeneration = 0;
        // Match mode: the Host plays a real match against the AI under its own game camera; the crowd joins around it.
        bool bMatch = false;
        // Windows run per tier: the empty field, then the crowd.
        int32 Stage = 0, Window = 0;
        bool Failed = false, BudgetFailed = false;

        bool Check(bool Passed, const FString& Name)
        {
            auto Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Name);
            Entry->SetBoolField(TEXT("passed"), Passed);
            Checks.Add(MakeShared<FJsonValueObject>(Entry));
            Failed |= !Passed;
            if (!Passed) { UE_LOG(LogLDTest, Error, TEXT("LD_PERF_BUDGET_CHECK_FAILED %s"), *Name); }
            return Passed;
        }

        bool Finish(const FString& Error = FString())
        {
            if (!Error.IsEmpty()) { Check(false, Error); }
            if (auto* Local = Controller.Get()) { Local->SetDevArtCaptureCamera(nullptr); }
            auto Report = MakeShared<FJsonObject>();
            Report->SetBoolField(TEXT("passed"), !Failed);
            Report->SetBoolField(TEXT("budget_passed"), !Failed && !BudgetFailed);
            Report->SetStringField(TEXT("scope"), TEXT("DEV frame budget fixture in the Demo map Lobby on the Listen Host: the game camera over open floor, uncapped, with the empty field and a crowd of real presented bodies at the low and high presets. Measures this machine only; not a gameplay, network or minimum-spec acceptance."));
            Report->SetStringField(TEXT("rhi"), GDynamicRHI ? FString(GDynamicRHI->GetName()) : FString());
            Report->SetStringField(TEXT("adapter"), GRHIAdapterName);
            Report->SetNumberField(TEXT("width"), GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport ? GEngine->GameViewport->Viewport->GetSizeXY().X : 0);
            Report->SetNumberField(TEXT("height"), GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport ? GEngine->GameViewport->Viewport->GetSizeXY().Y : 0);
            Report->SetStringField(TEXT("mode"), bMatch ? TEXT("match") : TEXT("lobby"));
            Report->SetNumberField(TEXT("bodies"), Bodies.Num());
            Report->SetArrayField(TEXT("windows"), Windows);
            Report->SetArrayField(TEXT("checks"), Checks);
            FString Json;
            const FString Path = Directory / TEXT("PerfBudget.json");
            const bool Written = FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
                && FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_PERF_BUDGET passed=%d budget_passed=%d report=%s"), !Failed && Written, !Failed && !BudgetFailed, *Path);
            FPlatformMisc::RequestExitWithStatus(false, !Failed && Written ? 0 : 1);
            return false;
        }

        void Spawn(UWorld& Target, uint8 Seat, uint8 Creature, const FVector& Centre, bool bRuns)
        {
            FActorSpawnParameters Params;
            Params.ObjectFlags |= RF_Transient;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            AActor* Actor = Target.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
            if (!Actor) { return; }
            auto* Visual = NewObject<ULDHumanoidVisualComponent>(Actor, TEXT("PerfBody"));
            Actor->SetRootComponent(Visual);
            Visual->RegisterComponent();
            if (Creature) { Visual->ApplyMonsterLook(Creature); }
            else { Visual->ApplyPalette(Seat); }
            const float HalfHeight = GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
            FPerfBody& Body = Bodies.AddDefaulted_GetRef();
            Body.Actor = Actor;
            Body.Visual = Visual;
            Body.Centre = Centre + FVector(0., 0., HalfHeight);
            Body.Phase = Bodies.Num() * 0.7;
            Body.bRuns = bRuns;
            Actor->SetActorLocationAndRotation(Body.Centre, FRotator(0., 135., 0.));
        }

        bool Setup(UWorld& Target)
        {
            const FLDArtResolved* Art = ULDArtSubsystem::Get(&Target);
            if (!Check(Art != nullptr, TEXT("The art catalog resolved at startup"))) { return false; }
            // Every contender seat and every camp tier, so each worn part and garment is drawn (the Dean wears the cloth cape).
            const uint8 Tiers[] = { uint8(ELDDemoMonsterId::MonsterScavenger), uint8(ELDDemoMonsterId::MonsterGuardian),
                uint8(ELDDemoMonsterId::MonsterDean), uint8(ELDDemoMonsterId::MonsterAdvisor) };
            const FVector Across(UE_INV_SQRT_2, UE_INV_SQRT_2, 0.), Down(UE_INV_SQRT_2, -UE_INV_SQRT_2, 0.);
            int32 Index = 0;
            for (uint8 Seat = 0; Seat < LDArt::SeatCount; ++Seat, ++Index)
            {
                Spawn(Target, Seat, 0, Focus + Across * ((Index % 5 - 2) * 260.) + Down * ((Index / 5) * 300. - 150.), Index % 2 == 0);
            }
            for (const uint8 Tier : Tiers)
            {
                Spawn(Target, 0, Tier, Focus + Across * ((Index % 5 - 2) * 260.) + Down * ((Index / 5) * 300. - 150.), Index % 2 == 0);
                ++Index;
            }
            if (!Check(Bodies.Num() == LDArt::SeatCount + UE_ARRAY_COUNT(Tiers), FString::Printf(TEXT("Spawned %d presented bodies"), Bodies.Num()))) { return false; }
            auto* Local = Cast<ALDPlayerController>(Target.GetFirstPlayerController());
            if (!Check(Local != nullptr, TEXT("Budget fixture has a local Lockdown controller"))) { return false; }
            Controller = Local;
            Uncap(Target);
            if (bMatch) { return true; }
            FActorSpawnParameters Params;
            Params.ObjectFlags |= RF_Transient;
            ACameraActor* View = Target.SpawnActor<ACameraActor>(Params);
            if (!Check(View != nullptr, TEXT("Budget camera spawned"))) { return false; }
            const UCameraComponent* Game = GetDefault<ALDCharacter>()->Camera;
            View->GetCameraComponent()->SetFieldOfView(Game->FieldOfView);
            View->GetCameraComponent()->PostProcessSettings = Game->PostProcessSettings;
            View->GetCameraComponent()->PostProcessBlendWeight = 1.f;
            View->GetCameraComponent()->bConstrainAspectRatio = false;
            View->SetActorTransform(ALDCharacter::DefaultArmCameraPose(Focus, LDCamera::DefaultArmLength));
            Camera = View;
            Local->SetDevArtCaptureCamera(View);
            Local->SetViewTarget(View);
            return true;
        }

        // Uncapped: the frame time is the work, not the display or a configured limit.
        static void Uncap(UWorld& Target)
        {
            GEngine->bSmoothFrameRate = false;
            GEngine->bUseFixedFrameRate = false;
            for (const TCHAR* Command : { TEXT("r.VSync 0"), TEXT("t.MaxFPS 0") }) { GEngine->Exec(&Target, Command); }
        }

        // Drives the normal lobby as the only human: the most AI the room takes, ready, start; done once the Host's own
        // body stands in the Development phase. Each command is resent at most once a second until the room shows it.
        bool JoinMatch(UWorld& Target, double Now)
        {
            auto* Local = Cast<ALDPlayerController>(Target.GetFirstPlayerController());
            const auto* Public = Target.GetGameState<ALDGameState>();
            if (!Local || !Public || !Local->IsLocalController() || !Public->DemoLobby.Generation) { return false; }
            if (Target.GetNetMode() != NM_ListenServer) { Finish(TEXT("Match mode runs on a Listen Host")); return false; }
            const FLDLobbyView& Lobby = Public->DemoLobby;
            const FLDPublicMatchView& View = Public->PublicMatch;
            const bool bResend = Now - SentAt > 1.;
            if (!StartedGeneration)
            {
                if (Lobby.Phase != ELDMatchPhase::Lobby) { return false; }
                if (Lobby.AICount != MatchAI) { if (bResend) { Local->SetDemoAICount(MatchAI); SentAt = Now; } return false; }
                const FLDLobbyMember* Self = Lobby.Members.FindByPredicate([Local](const FLDLobbyMember& Row) { return Row.ContenderId == Local->OwnerState.ContenderId; });
                if (!Self || !Self->Ready) { if (bResend) { Local->SetDemoReady(true); SentAt = Now; } return false; }
                if (!Lobby.CanStart) { Finish(TEXT("The ready room cannot start: ") + Lobby.StartBlocker); return false; }
                StartedGeneration = Lobby.Generation;
                Local->StartDemoMatch();
                return false;
            }
            if (View.Phase != ELDMatchPhase::Development || View.Generation != Lobby.Generation || !Local->GetPawn()) { return false; }
            // The crowd stands around the Host's spawn, in front of its game camera.
            Focus = Local->GetPawn()->GetActorLocation() - FVector(0., 0., GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
            return true;
        }

        void SetCrowd(bool bShown)
        {
            for (const FPerfBody& Body : Bodies) { if (Body.Actor.IsValid()) { Body.Actor->SetActorHiddenInGame(!bShown); } }
        }

        void Animate(float Delta)
        {
            Clock += Delta;
            for (const FPerfBody& Body : Bodies)
            {
                if (!Body.Actor.IsValid() || !Body.Visual.IsValid()) { continue; }
                FVector Velocity = FVector::ZeroVector;
                if (Body.bRuns)
                {
                    // A run lap of 120 cm radius at about 400 cm/s keeps the run clip, the turn and the cape moving.
                    const double Angle = Clock * 3.3 + Body.Phase;
                    const FVector Offset(FMath::Cos(Angle) * 120., FMath::Sin(Angle) * 120., 0.);
                    Velocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.) * 400.;
                    Body.Actor->SetActorLocationAndRotation(Body.Centre + Offset, Velocity.Rotation());
                }
                Body.Visual->Animate(Velocity, Delta);
            }
        }

        void ApplyTier(const FPerfTier& Tier)
        {
            Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
            Levels.SetFromSingleQualityLevel(Tier.Level);
            Scalability::SetQualityLevels(Levels, true);
        }

        void CloseWindow(const FPerfTier& Tier, bool bCrowd)
        {
            auto Mean = [this](double FPerfSample::* Field)
            { double Sum = 0.; for (const FPerfSample& Sample : Samples) { Sum += Sample.*Field; } return Samples.IsEmpty() ? 0. : Sum / Samples.Num(); };
            TArray<double> Frames;
            for (const FPerfSample& Sample : Samples) { Frames.Add(Sample.FrameMs); }
            Frames.Sort();
            auto Percentile = [&Frames](double P) { return Frames.IsEmpty() ? 0. : Frames[FMath::Clamp(int32(FMath::CeilToDouble(P * Frames.Num())) - 1, 0, Frames.Num() - 1)]; };
            const double Frame = Mean(&FPerfSample::FrameMs);
            auto Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("tier"), Tier.Name);
            Entry->SetNumberField(TEXT("quality_level"), Tier.Level);
            Entry->SetStringField(TEXT("scene"), bCrowd ? TEXT("crowd") : TEXT("empty"));
            Entry->SetNumberField(TEXT("frames"), Samples.Num());
            Entry->SetNumberField(TEXT("frame_ms"), Frame);
            Entry->SetNumberField(TEXT("fps"), Frame > 0. ? 1000. / Frame : 0.);
            Entry->SetNumberField(TEXT("frame_p50_ms"), Percentile(.5));
            Entry->SetNumberField(TEXT("frame_p95_ms"), Percentile(.95));
            Entry->SetNumberField(TEXT("frame_p99_ms"), Percentile(.99));
            Entry->SetNumberField(TEXT("frame_max_ms"), Frames.IsEmpty() ? 0. : Frames.Last());
            Entry->SetNumberField(TEXT("game_ms"), Mean(&FPerfSample::GameMs));
            Entry->SetNumberField(TEXT("render_ms"), Mean(&FPerfSample::RenderMs));
            Entry->SetNumberField(TEXT("rhi_ms"), Mean(&FPerfSample::RhiMs));
            Entry->SetNumberField(TEXT("gpu_ms"), Mean(&FPerfSample::GpuMs));
            Entry->SetNumberField(TEXT("budget_ms"), Tier.BudgetMs);
            const bool bWithin = Frame > 0. && Frame <= Tier.BudgetMs;
            Entry->SetBoolField(TEXT("within_budget"), bWithin);
            Windows.Add(MakeShared<FJsonValueObject>(Entry));
            Check(Samples.Num() >= 30, FString::Printf(TEXT("%s %s window sampled %d frames"), Tier.Name, bCrowd ? TEXT("crowd") : TEXT("empty"), Samples.Num()));
            // The budget holds for the crowd; the empty field is the baseline the bodies add to.
            if (bCrowd) { BudgetFailed |= !bWithin; }
            UE_LOG(LogLDTest, Display, TEXT("LD_PERF_BUDGET_WINDOW tier=%s scene=%s frames=%d frame_ms=%.2f p95_ms=%.2f game_ms=%.2f render_ms=%.2f rhi_ms=%.2f gpu_ms=%.2f budget_ms=%.2f within=%d"),
                Tier.Name, bCrowd ? TEXT("crowd") : TEXT("empty"), Samples.Num(), Frame, Percentile(.95), Mean(&FPerfSample::GameMs),
                Mean(&FPerfSample::RenderMs), Mean(&FPerfSample::RhiMs), Mean(&FPerfSample::GpuMs), Tier.BudgetMs, bWithin);
            Samples.Reset();
        }

        bool Tick(float Delta)
        {
            const double Now = FPlatformTime::Seconds();
            if (Now - StartedAt > 900.) { return Finish(TEXT("Frame budget fixture timed out")); }
            if (!World.IsValid()) { return Finish(TEXT("The Demo world ended before the frame budget completed")); }
            UWorld& Target = *World.Get();
            if (Stage == 0)
            {
                if (Now - StartedAt < 3.) { return true; }
                if (bMatch && !JoinMatch(Target, Now)) { return !Failed; }
                if (!Setup(Target)) { return Finish(); }
                Stage = 1;
                return true;
            }
            Animate(Delta);
            auto* Local = Controller.Get();
            if (!Local || (!bMatch && !Camera.IsValid())) { return Finish(TEXT("No budget camera or original local player controller")); }
            if (!bMatch && Local->GetViewTarget() != Camera.Get()) { Local->SetViewTarget(Camera.Get()); }
            if (Stage == 1)
            {
                // Every tier's shaders compile before any window: each preset and the crowd are drawn once first.
                const int32 Primer = int32((Now - StartedAt) / 2.) % (UE_ARRAY_COUNT(PerfTiers) * 2);
                const FPerfTier& Tier = PerfTiers[Primer / 2];
                if (Scalability::GetQualityLevels().GetSingleQualityLevel() != Tier.Level) { ApplyTier(Tier); }
                SetCrowd(true);
                if (Now - StartedAt < 3. + 2. * UE_ARRAY_COUNT(PerfTiers) * 2 || (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()))
                { return true; }
                Stage = 2;
                Window = 0;
                PhaseAt = -1.;
            }
            const int32 WindowCount = UE_ARRAY_COUNT(PerfTiers) * 2;
            if (Window >= WindowCount) { return Finish(); }
            const FPerfTier& Tier = PerfTiers[Window / 2];
            const bool bCrowd = Window % 2 == 1;
            if (PhaseAt < 0.)
            {
                ApplyTier(Tier);
                SetCrowd(bCrowd);
                Samples.Reset();
                PhaseAt = Now;
                return true;
            }
            if (Now - PhaseAt < WarmSeconds) { return true; }
            if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
            {
                // A compile in the window would measure the compiler; restart the warm-up instead.
                Samples.Reset();
                PhaseAt = Now;
                return true;
            }
            Samples.Add({ double(Delta) * 1000., FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
                FPlatformTime::ToMilliseconds(GRHIThreadTime), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)) });
            if (Now - PhaseAt < WarmSeconds + SampleSeconds) { return true; }
            CloseWindow(Tier, bCrowd);
            ++Window;
            PhaseAt = -1.;
            return true;
        }
    };
}

void LDDemoTests::StartPerfBudgetProbe(UWorld& World)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoPerfBudget"))) { return; }
    FString Directory;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoPerfBudgetOutput="), Directory)
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Directory), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Artifacts"))))
    {
        UE_LOG(LogLDTest, Error, TEXT("The frame budget fixture requires an output directory under Artifacts."));
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    auto Probe = MakeShared<FPerfBudgetProbe>();
    Probe->World = &World;
    Probe->Directory = FPaths::ConvertRelativePathToFull(Directory);
    Probe->bMatch = FParse::Param(FCommandLine::Get(), TEXT("LDDemoPerfBudgetMatch"));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Delta) { return Probe->Tick(Delta); }));
}
#endif
