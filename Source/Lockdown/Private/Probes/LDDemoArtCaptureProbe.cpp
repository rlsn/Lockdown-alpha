#include "Probes/LDDemoArtCaptureProbe.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Audio/LDAudioFeel.h"
#include "Audio/LDAudioSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Character/LDHitFeel.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Art/LDArtCatalog.h"
#include "Art/LDArtPlacement.h"
#include "Art/LDArtSubsystem.h"
#include "Art/LDStaticScene.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/LDCharacter.h"
#include "Character/LDCameraSettings.h"
#include "Character/LDCombatVfxCatalog.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Character/LDImpactFx.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "UI/LDWorldFeedback.h"
#include "Combat/LDCombatCues.h"
#include "Combat/LDCombatElements.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "Misc/App.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "Vision/LDWorldViewRelay.h"
#include "World/LDCaptureMarker.h"
#include "World/LDPKRingMarker.h"
#include "World/LDRecoveryOrbMarker.h"
#include "HAL/IConsoleManager.h"

namespace
{
    struct FArtShot
    {
        FString Name;
        FVector Focus = FVector::ZeroVector;
        float ArmCm = LDCamera::DefaultArmLength;
        // Straight down from ArmCm above Focus, +X to the right, +Y down (the editor top view).
        bool bTopDown = false;
        uint8 Action = 0;
        ELDObservedHitLook HitLook = ELDObservedHitLook::None;
        uint8 HitFlags = 0;
        bool bHit = false;
        float HitAgeSeconds = .08f;
        bool bKill = false;
        // The particle kit: a skill cast reaching its release, a dash streaking across, a visible skill effect.
        uint8 CastAbility = 0;
        bool bDash = false;
        bool bEffect = false;
        ELDObservedProjectileLook Effect = ELDObservedProjectileLook::Chalk;
        float EffectRadius = 0.f;
        float CaptureDelay = 0.f;
        // An element reaction on the victim (ELDCombatElement), a public ground state (ELDGroundStateKind), a flight that
        // keeps flying through the shot, and a shot that keeps the interface (the screen edge flash).
        uint8 ReactionElement = 0;
        uint8 GroundKind = 0;
        bool bFlying = false;
        bool bShowUI = false;
    };

    // Floor points a body can stand on, with the camera arm lengths the player can zoom between.
    constexpr double ArtSweepStepCm = 100.;
    const float ArtSweepArms[] = { LDCamera::MinArmLength, LDCamera::DefaultArmLength, LDCamera::MaxArmLength };
    // Heights on a standing body that must stay visible: upper chest and top of the head.
    const double ArtSweepBodyCm[] = { 130., 170. };
    // How far the combat VFX dash shot streaks the attacker before it holds still; the stage search clears this run.
    constexpr double VfxDashRunCm = 600.;

    struct FArtCaptureProbe
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<ACameraActor> Camera;
        TWeakObjectPtr<ALDPlayerController> CaptureController;
        TWeakObjectPtr<ULDHumanoidVisualComponent> VfxAttacker, VfxVictim;
        FString Directory, PendingImage;
        TArray<TSharedPtr<FJsonValue>> Checks, Images, Bodies, Decorations, Occluders, OccludedPoints;
        TArray<FArtShot> Shots;
        double StartedAt = FPlatformTime::Seconds(), NextAt = FPlatformTime::Seconds() + 3., VfxCaptureAt = 0.;
        int32 Stage = 0, Shot = 0;
        uint64 VfxCaptureFrame = 0;
        bool bVfxPrepared = false;
        bool bVfxDash = false;
        TWeakObjectPtr<ALDObservedGroundProxy> VfxGround;
        FLDObservedProjectileView VfxFlight;
        double VfxFlightAt = -1.;
        FVector VfxDashFrom = FVector::ZeroVector;
        TWeakObjectPtr<ALDObservedProjectileProxy> VfxEffect;
        bool Failed = false, bCombatVfx = false;
        // Keyframe sequences of the designed slice effects (LDSliceVfx), played in slowed world time so every phase
        // (anticipation, release, impact, follow-through, dissipation) is captured at its own moment; time stands still
        // while a frame is written. Extra bodies on the stage: two more victims for the chain and two more attackers.
        struct FSeqStep { float At = 0; TFunction<void()> Do; };
        struct FSeqShot { float At = 0; FString Label; };
        struct FSequence { FString Name; TArray<FSeqStep> Steps; TArray<FSeqShot> Shots; };
        TArray<TWeakObjectPtr<ULDHumanoidVisualComponent>> VfxExtras;
        TArray<FSequence> Sequences;
        int32 SeqIndex = 0, SeqStep = 0, SeqShot = 0;
        bool bSeqStarted = false, bSeqWarmed = false;
        double SeqClock = 0., SeqWarmUntil = -1.;
        static constexpr float SeqDilation = .1f, SeqFreeze = .0001f;
        TWeakObjectPtr<ALDObservedGroundProxy> SeqGround;
        // Effect proof harness (-LDDemoVfxProof=chain,hit,swing,field): per category, its judging scenarios captured frame by
        // frame at real game speed (a fixed 1/30 s step, the world held while each frame is written), then three frame-cost
        // runs of eight casters against an idle baseline. Results in DemoArt.json chain_runs / chain_perf, each tagged with
        // its category ("look") and -LDDemoVfxProofLabel (old / new).
        struct FChainJob
        {
            FString Category;
            FString Scenario;
            TArray<FSeqStep> Steps;
            TFunction<void()> Setup, Teardown;
            bool bPerf = false;
            // The moment the comparison sheet judges (its frame is the second after it).
            float ShowAt = .27f;
            // Frames captured from From seconds on.
            float From = .2f;
            int32 Frames = 24;
            // Audiovisual feel sample (-LDDemoVfxProof=feel): played at real game speed with the audio device on; the master
            // submix is recorded to <folder>/game_audio.wav (the game's own mix, not a re-render) for Seconds.
            bool bRealtime = false;
            float Seconds = 0.f;
            // A frame-cost run (bPerf) of this job's own steps, replayed every LoadEvery seconds, in place of the eight casters.
            float LoadEvery = 0.f;
        };
        FString ProofLabel;
        TArray<FChainJob> ChainJobs;
        int32 ChainJob = 0, ChainPhase = 0, ChainFrame = 0, ChainStep = 0;
        double ChainClock = 0., ChainPhaseAt = 0., ChainNextWave = 0.;
        int32 ChainWave = 0;
        TArray<TSharedPtr<FJsonValue>> ChainRuns, ChainPerf, ChainFrameTimes;
        TArray<TWeakObjectPtr<ULDHumanoidVisualComponent>> PerfCasters, PerfVictims;
        struct FFrameStats { int32 Frames = 0; double RealMs = 0, GameMs = 0, RenderMs = 0, GpuMs = 0; int32 MaxComponents = 0, MaxParticles = 0, MaxLayers = 0, MaxLive = 0; };
        TArray<TWeakObjectPtr<ALDObservedGroundProxy>> ProofGrounds;
        // The scenario clock at which each proof ground state was laid: its life runs on that clock (Present).
        TArray<double> ProofGroundsAt;
        FFrameStats PerfIdle, PerfLoad;
        TSharedPtr<FJsonObject> ChainCheck;
        static constexpr float ChainStep_ = 1.f / 30.f;
        FVector VfxFocus = FVector::ZeroVector;
        float BasicWindupSeconds = .18f;

        bool Check(bool Passed, const FString& Name)
        {
            auto Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Name);
            Entry->SetBoolField(TEXT("passed"), Passed);
            Checks.Add(MakeShared<FJsonValueObject>(Entry));
            Failed |= !Passed;
            if (!Passed) { UE_LOG(LogLDTest, Error, TEXT("LD_ART_CAPTURE_CHECK_FAILED %s"), *Name); }
            return Passed;
        }

        bool Finish(const FString& Error = FString())
        {
            if (!Error.IsEmpty()) { Check(false, Error); }
            if (auto* Controller = CaptureController.Get()) { Controller->SetDevArtCaptureCamera(nullptr); }
            auto Report = MakeShared<FJsonObject>();
            Report->SetBoolField(TEXT("passed"), !Failed);
            Report->SetStringField(TEXT("scope"), bCombatVfx
                ? TEXT("DEV visual fixture: actual body presentation components receive timed attack and hit cues at the normal game camera distance. Not gameplay, network or artistic acceptance.")
                : TEXT("DEV art fixture in the Demo map Lobby: catalog bodies posed per clip and the decorated field rendered from the game camera angle; decoration collision, sight-trace and navigation checks on the Listen Host. Not a gameplay, combat wiring or network acceptance."));
            Report->SetArrayField(TEXT("checks"), Checks);
            Report->SetArrayField(TEXT("screenshots"), Images);
            Report->SetArrayField(TEXT("bodies"), Bodies);
            Report->SetArrayField(TEXT("decorations"), Decorations);
            Report->SetArrayField(TEXT("occluders"), Occluders);
            Report->SetArrayField(TEXT("occluded_points"), OccludedPoints);
            if (!ChainRuns.IsEmpty()) { Report->SetArrayField(TEXT("chain_runs"), ChainRuns); }
            if (!ChainPerf.IsEmpty()) { Report->SetArrayField(TEXT("chain_perf"), ChainPerf); }
            FApp::SetUseFixedTimeStep(false);
            FString Json;
            const FString Path = Directory / TEXT("DemoArt.json");
            const bool Written = FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
                && FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_ART_CAPTURE passed=%d report=%s"), !Failed && Written, *Path);
            FPlatformMisc::RequestExitWithStatus(false, !Failed && Written ? 0 : 1);
            return false;
        }

        AActor* SpawnBody(UWorld& Target, const FLDArtCharacter& Look, const FLinearColor& Tint, const FVector& Foot, float Yaw, ELDArtAnim State)
        {
            FActorSpawnParameters Spawn;
            Spawn.ObjectFlags |= RF_Transient;
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            AActor* Actor = Target.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
            if (!Actor) { return nullptr; }
            auto* Capsule = NewObject<USceneComponent>(Actor, TEXT("Capsule"));
            Actor->SetRootComponent(Capsule);
            Capsule->RegisterComponent();
            const float HalfHeight = GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
            Actor->SetActorLocationAndRotation(Foot + FVector(0, 0, HalfHeight), FRotator(0, Yaw, 0));
            auto* Mesh = NewObject<USkeletalMeshComponent>(Actor, TEXT("Body"));
            Mesh->SetupAttachment(Capsule);
            Mesh->SetSkeletalMeshAsset(Look.Mesh);
            Mesh->SetRelativeTransform(Look.MeshTransform);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->SetCanEverAffectNavigation(false);
            Mesh->RegisterComponent();
            for (const FName Slot : Look.TintSlots)
            {
                if (UMaterialInstanceDynamic* Paint = Mesh->CreateDynamicMaterialInstance(Mesh->GetMaterialIndex(Slot)))
                {
                    Paint->SetVectorParameterValue(ULDArtCatalog::TintParameter, Tint);
                }
            }
            // Worn parts as the body component wears them: rigid parts on their socket, garments in the body pose.
            for (const FLDArtAttachment& Part : Look.Attachments)
            {
                UMeshComponent* Worn = nullptr;
                if (Part.StaticMesh)
                {
                    auto* Rigid = NewObject<UStaticMeshComponent>(Actor);
                    Rigid->SetStaticMesh(Part.StaticMesh);
                    Rigid->SetupAttachment(Mesh, Part.Socket);
                    Worn = Rigid;
                }
                else
                {
                    auto* Garment = NewObject<USkeletalMeshComponent>(Actor);
                    Garment->SetSkeletalMeshAsset(Part.SkinnedMesh);
                    Garment->SetupAttachment(Mesh);
                    Garment->SetLeaderPoseComponent(Mesh);
                    Worn = Garment;
                }
                Worn->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Worn->SetCanEverAffectNavigation(false);
                Worn->RegisterComponent();
                for (const FName Slot : Part.TintSlots)
                {
                    if (UMaterialInstanceDynamic* Paint = Worn->CreateDynamicMaterialInstance(Worn->GetMaterialIndex(Slot)))
                    {
                        Paint->SetVectorParameterValue(ULDArtCatalog::TintParameter, Tint);
                    }
                }
            }
            if (const FLDArtAnimation* Clip = Look.Find(State))
            {
                Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
                Mesh->PlayAnimation(Clip->Sequence, Clip->bLoop);
                Mesh->SetPlayRate(Clip->PlayRate);
                if (!Clip->bLoop)
                {
                    // One-shot clips are frozen on their telling frame: the blow, the cast, mid flinch or the fallen body.
                    const float Length = Clip->Sequence->GetPlayLength();
                    Mesh->SetPosition(LDArt::HasImpact(State) ? Clip->ImpactSec : State == ELDArtAnim::Death ? Length : Length * .5f, false);
                    Mesh->Stop();
                }
            }
            auto Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("character"), Look.Id);
            Entry->SetStringField(TEXT("state"), LDArt::AnimId(State));
            Entry->SetStringField(TEXT("mesh"), Look.Mesh->GetPathName());
            Entry->SetNumberField(TEXT("height_cm"), Look.HeightCm);
            Entry->SetNumberField(TEXT("x"), Foot.X);
            Entry->SetNumberField(TEXT("y"), Foot.Y);
            Bodies.Add(MakeShared<FJsonValueObject>(Entry));
            return Actor;
        }

        void CheckDecorations(UWorld& Target)
        {
            const auto Parts = LDStaticScene::Visuals(Target);
            const auto* ArtSubsystem = Target.GetGameInstance()->GetSubsystem<ULDArtSubsystem>();
            if (!Check(!Parts.IsEmpty() && ArtSubsystem && ArtSubsystem->GetCatalog(), TEXT("Saved static scene actors are present"))) { return; }
            Check(Parts.Num() == ArtSubsystem->GetCatalog()->Decorations.Num(), TEXT("Saved scene matches its derived index"));
            FString Error;
            Check(LDArt::CheckPlacementsAgainstDemoWorld(*ArtSubsystem->GetCatalog(), Error), TEXT("Decorations respect the D02 placement zones ") + Error);
            const ECollisionChannel Channels[] = { ECC_GameTraceChannel2, ECC_GameTraceChannel1, ECC_Pawn, ECC_Visibility, ECC_Camera };
            auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&Target);
            int32 Traces = 0, TraceHits = 0, NavChecks = 0, NavMisses = 0;
            for (const UStaticMeshComponent* Part : Parts)
            {
                Check(Part->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Part->GetGenerateOverlapEvents()
                    && !Part->CanEverAffectNavigation(), TEXT("Saved visual adds no collision or navigation"));
                const FBox Box = Part->GetStaticMesh()->GetBoundingBox().TransformBy(Part->GetComponentTransform());
                const FVector Centre = Box.GetCenter(), Extent = Box.GetExtent() + FVector(100.);
                for (const ECollisionChannel Channel : Channels)
                {
                    for (const FVector& Axis : {FVector(0, 0, Extent.Z), FVector(Extent.X, 0, 0), FVector(0, Extent.Y, 0)})
                    {
                        TArray<FHitResult> Hits;
                        Target.LineTraceMultiByChannel(Hits, Centre + Axis, Centre - Axis, Channel, FCollisionQueryParams(SCENE_QUERY_STAT(LDArtCapture), true));
                        ++Traces;
                        TraceHits += Hits.FilterByPredicate([Part](const FHitResult& Hit) { return Hit.GetComponent() == Part; }).Num();
                    }
                }
            }
            // The floor under a decoration on the arena stays walkable: the navmesh was not carved by it. A blocker's
            // prop (#97) stands on its block, which the navmesh rightly leaves out.
            for (const FLDArtDecoration& Decoration : ArtSubsystem->GetCatalog()->Decorations)
            {
                const FVector Floor(Decoration.Transform.GetLocation().X, Decoration.Transform.GetLocation().Y, 0.);
                if (Navigation && Decoration.Zone != ELDArtZone::Blocker && FMath::Abs(Floor.X) < 5000. && FMath::Abs(Floor.Y) < 5000.
                    && FVector::Dist2D(Floor, FVector(778, 778, 0)) > 200.)
                {
                    FNavLocation Projected;
                    ++NavChecks;
                    const bool bMiss = !(Navigation->ProjectPointToNavigation(Floor, Projected, FVector(50., 50., 200.)) && FVector::Dist2D(Projected.Location, Floor) <= 50.);
                    NavMisses += bMiss;
                    if (bMiss && NavMisses <= 5) { UE_LOG(LogLDContent, Warning, TEXT("LD_ART_CAPTURE_NAV_MISS %s %s at %s"), *Decoration.Prop, *LDArt::ZoneId(Decoration.Zone), *Floor.ToString()); }
                }
            }
            Check(Traces > 0 && TraceHits == 0, FString::Printf(TEXT("%d sight, ground-aim, pawn, visibility and camera traces through decorations hit none of them"), Traces));
            Check(Navigation && NavChecks > 0 && NavMisses == 0, FString::Printf(TEXT("Navmesh still covers the floor under all %d on-floor decorations"), NavChecks));
        }

        // Sweeps every standable floor point at every zoom: a prop hides a body when it cuts the camera's line to the
        // body's upper chest or head. Records each decoration's footprint and every hidden point for the layout map.
        void CheckOcclusion(const ULDArtCatalog& Catalog)
        {
            FString Error;
            TSharedPtr<const FLDDemoContentSnapshot> Content;
            TSharedPtr<const FLDDemoWorldGeometry> Layout;
            const auto* Definition = LoadObject<ULDMapDefinition>(nullptr, *(FString(LDDemoEntry::WorldDefinition) + TEXT(".DA_DemoWorld")));
            FVector FloorMin = FVector::ZeroVector, FloorMax = FVector::ZeroVector;
            if (!Check(LDDemoEntry::LoadRuntimeContent(Content, Error) && Content && Definition && Definition->Read(*Content, Layout, Error) && Layout
                && Layout->Surfaces.Num() == 1 && Layout->Surfaces[0].MinCm.ToVector(FloorMin, Error) && Layout->Surfaces[0].MaxCm.ToVector(FloorMax, Error),
                TEXT("The approved world geometry is readable for the occlusion sweep ") + Error))
            {
                return;
            }
            const FLDDemoRational& Radius = Content->Get().Parameters.CollisionRadiusCm;
            const double Body = Radius.Denominator > 0 ? double(Radius.Numerator) / double(Radius.Denominator) : 0.;
            // #97: feet stand on the terrain; ramps are left out of the sweep.
            LDDemoWorld::FTerrain Terrain;
            if (!Check(Terrain.Build(*Layout, Error), TEXT("The world terrain builds for the occlusion sweep ") + Error)) { return; }
            TArray<FBox> Blocked, HiddenBlocks;
            for (const FLDDemoWorldBlocker& Blocker : Layout->Blockers)
            {
                TArray<FVector2D> Footprint;
                FBox Box(ForceInit);
                if (LDDemoWorld::Footprint(Blocker, Footprint, Error)) { for (const FVector2D& Point : Footprint) { Box += FVector(Point, 0.); } }
                Blocked.Add(Box.ExpandBy(FVector(Body, Body, 0.)));
                if (Catalog.HiddenBlockers.Contains(Blocker.Id)) { HiddenBlocks.Add(Box.ExpandBy(FVector(1., 1., 0.))); }
            }
            TMap<FString, const FLDArtProp*> Props;
            for (const FLDArtProp& Prop : Catalog.Props) { Props.Add(Prop.Id, &Prop); }
            TArray<FBox> Boxes;
            for (const FLDArtDecoration& Decoration : Catalog.Decorations)
            {
                const FBox Box = Props.FindRef(Decoration.Prop)->Mesh->GetBoundingBox().TransformBy(Decoration.Transform);
                Boxes.Add(Box);
                auto Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("prop"), Decoration.Prop);
                Entry->SetStringField(TEXT("zone"), LDArt::ZoneId(Decoration.Zone));
                Entry->SetArrayField(TEXT("min"), { MakeShared<FJsonValueNumber>(Box.Min.X), MakeShared<FJsonValueNumber>(Box.Min.Y), MakeShared<FJsonValueNumber>(Box.Min.Z) });
                Entry->SetArrayField(TEXT("max"), { MakeShared<FJsonValueNumber>(Box.Max.X), MakeShared<FJsonValueNumber>(Box.Max.Y), MakeShared<FJsonValueNumber>(Box.Max.Z) });
                Decorations.Add(MakeShared<FJsonValueObject>(Entry));
            }
            const double HalfHeight = GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
            TArray<int32> Counts;
            Counts.SetNumZeroed(Boxes.Num());
            int32 Points = 0, Hidden = 0;
            for (double X = FloorMin.X + Body; X <= FloorMax.X - Body; X += ArtSweepStepCm)
            {
                for (double Y = FloorMin.Y + Body; Y <= FloorMax.Y - Body; Y += ArtSweepStepCm)
                {
                    if (Terrain.Tier(FVector2D(X, Y)) % 2) { continue; }
                    double Ground = 0.;
                    FVector Normal;
                    Terrain.Ground(FVector2D(X, Y), Ground, Normal);
                    const FVector Foot(X, Y, Ground);
                    if (Blocked.ContainsByPredicate([&Foot](const FBox& Box) { return Box.IsInsideXY(Foot); })) { continue; }
                    ++Points;
                    TSet<int32> Hiders;
                    for (const float Arm : ArtSweepArms)
                    {
                        const FVector Eye = ALDCharacter::DefaultArmCameraPose(Foot + FVector(0., 0., HalfHeight), Arm).GetLocation();
                        for (const double Height : ArtSweepBodyCm)
                        {
                            const FVector Target = Foot + FVector(0., 0., Height);
                            for (int32 Index = 0; Index < Boxes.Num(); ++Index)
                            {
                                if (FMath::LineBoxIntersection(Boxes[Index], Eye, Target, Target - Eye)) { Hiders.Add(Index); }
                            }
                        }
                    }
                    if (Hiders.IsEmpty()) { continue; }
                    ++Hidden;
                    TArray<TSharedPtr<FJsonValue>> Names;
                    for (const int32 Index : Hiders)
                    {
                        ++Counts[Index];
                        Names.Add(MakeShared<FJsonValueString>(Catalog.Decorations[Index].Prop));
                    }
                    auto Entry = MakeShared<FJsonObject>();
                    Entry->SetNumberField(TEXT("x"), X);
                    Entry->SetNumberField(TEXT("y"), Y);
                    Entry->SetArrayField(TEXT("props"), Names);
                    OccludedPoints.Add(MakeShared<FJsonValueObject>(Entry));
                }
            }
            // Allowed to hide a body: the capture flagpole, and the art that draws world blocks (#97): a blocker's own prop and
            // the Outside props standing on a hidden boundary block. Those blocks hid the same bodies before art drew them.
            bool bOnlyAllowed = true;
            for (int32 Index = 0; Index < Boxes.Num(); ++Index)
            {
                if (!Counts[Index]) { continue; }
                const FLDArtDecoration& Decoration = Catalog.Decorations[Index];
                const FBox& Box = Boxes[Index];
                const bool bOnHiddenBlock = Decoration.Zone == ELDArtZone::Outside && HiddenBlocks.ContainsByPredicate([&Box](const FBox& Block)
                    { return Block.IsInsideXY(FVector(Box.Min.X, Box.Min.Y, 0.)) && Block.IsInsideXY(FVector(Box.Max.X, Box.Max.Y, 0.)); });
                const bool bAllowed = Decoration.Zone == ELDArtZone::Capture || Decoration.Zone == ELDArtZone::Blocker || bOnHiddenBlock;
                bOnlyAllowed &= bAllowed;
                if (!bAllowed && Counts[Index]) { UE_LOG(LogLDContent, Warning, TEXT("LD_ART_CAPTURE_OCCLUDER %s %s hides %d points"), *Decoration.Prop, *LDArt::ZoneId(Decoration.Zone), Counts[Index]); }
                auto Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("prop"), Decoration.Prop);
                Entry->SetStringField(TEXT("zone"), LDArt::ZoneId(Decoration.Zone));
                Entry->SetNumberField(TEXT("height_cm"), Boxes[Index].Max.Z);
                Entry->SetNumberField(TEXT("hidden_points"), Counts[Index]);
                Occluders.Add(MakeShared<FJsonValueObject>(Entry));
            }
            Check(Points > 0 && bOnlyAllowed, FString::Printf(TEXT("Occlusion sweep over %d standable floor points at %d zooms: %d points have the upper chest or head behind a prop, and only the capture flagpole or art drawing a world block does it"),
                Points, int32(UE_ARRAY_COUNT(ArtSweepArms)), Hidden));
        }

        ULDHumanoidVisualComponent* SpawnVfxBody(UWorld& Target, const FVector& Foot, float Yaw, uint8 Slot)
        {
            FActorSpawnParameters Spawn;
            Spawn.ObjectFlags |= RF_Transient;
            AActor* Actor = Target.SpawnActor<AActor>(Spawn);
            if (!Actor) { return nullptr; }
            auto* Visual = NewObject<ULDHumanoidVisualComponent>(Actor, NAME_None, RF_Transient);
            Actor->SetRootComponent(Visual);
            Actor->AddInstanceComponent(Visual);
            Visual->RegisterComponent();
            const float HalfHeight = GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
            Actor->SetActorLocationAndRotation(Foot + FVector(0, 0, HalfHeight), FRotator(0, Yaw, 0));
            Visual->ApplyPalette(Slot);
            Visual->Animate(FVector::ZeroVector, 0.f);
            return Visual;
        }

        // The stage of the combat VFX bodies: the first open floor around the flagpole, nearest ring first, found in the
        // loaded level, so the fixture follows the authored map. Every sampled point (both bodies and a ring around them)
        // is legal ground for a body on the flagpole's surface, level with the centre; the dash run is swept by the body
        // capsule and keeps level walkable support along its whole length. From the game camera at its default arm, no
        // collision hides a body's feet, chest or head, and no visual decoration (which has no collision) hides its chest
        // or head, tested on the current visual components' bounds.
        bool FindVfxStage(UWorld& Target, FVector& OutFocus, FString& Error) const
        {
            auto* Registry = Target.GetSubsystem<ULDMapRegistry>();
            TSharedPtr<const FLDDemoContentSnapshot> Content;
            FCollisionShape Capsule;
            if (!Registry || !Registry->Ensure(Error) || !LDDemoEntry::LoadRuntimeContent(Content, Error) || !Content
                || !ULDMapRegistry::ContentCapsule(*Content, Capsule, Error))
            {
                return false;
            }
            const FLDMapBinding* Flag = Registry->Resolve(FName(*Content->Get().World.CaptureId), Registry->Generation);
            if (!Flag)
            {
                Error = TEXT("The level has no capture anchor to search around.");
                return false;
            }
            const FLDDemoRational& Reach = Content->Get().Parameters.CaptureRadiusCm;
            const double FlagReach = Reach.Denominator > 0 ? double(Reach.Numerator) / double(Reach.Denominator) : 0.;
            // The visuals as the loaded level places them now (an author may move, copy or delete them without
            // refreshing the art catalog): each component's world bounds.
            TArray<FBox> Visuals;
            for (const UStaticMeshComponent* Part : LDStaticScene::Visuals(Target))
            {
                if (Part) { Visuals.Add(Part->Bounds.GetBox()); }
            }
            const FVector2D Across(UE_INV_SQRT_2, UE_INV_SQRT_2);
            const FVector2D DashFrom = -Across * 95.;
            TArray<FVector2D> Samples = { FVector2D::ZeroVector, -Across * 95., Across * 95. };
            for (int32 Turn = 0; Turn < 8; ++Turn) { Samples.Add(FVector2D(320., 0.).GetRotated(45. * Turn)); }
            const double HalfHeight = Capsule.GetCapsuleHalfHeight();
            FCollisionQueryParams Sight(SCENE_QUERY_STAT(LDArtCapture), true);
            const auto Seen = [&](const FVector& Eye, const FVector& Surface, bool bFeet)
            {
                for (const double Height : { 10., ArtSweepBodyCm[0], ArtSweepBodyCm[1] })
                {
                    if (!bFeet && Height < ArtSweepBodyCm[0]) { continue; }
                    const FVector Point = Surface + FVector(0., 0., Height);
                    if (Target.LineTraceTestByChannel(Eye, Point, ECC_Visibility, Sight)
                        || Target.LineTraceTestByChannel(Eye, Point, ECC_Camera, Sight))
                    {
                        return false;
                    }
                    if (Height >= ArtSweepBodyCm[0] && Visuals.ContainsByPredicate([&](const FBox& Box)
                        { return FMath::LineBoxIntersection(Box, Eye, Point, Point - Eye); }))
                    {
                        return false;
                    }
                }
                return true;
            };
            const auto Level = [&](const FVector2D& Point, const FVector& Focus, FVector& Surface)
            {
                return Registry->ResolveWalkableGround(Point, Surface) && FMath::Abs(Surface.Z - Focus.Z) <= 15.;
            };
            const auto Open = [&](const FVector2D& Centre, FVector& Focus)
            {
                if (!Registry->ResolveWalkableGround(Centre, Focus)) { return false; }
                const FVector Eye = ALDCharacter::DefaultArmCameraPose(Focus, LDCamera::DefaultArmLength).GetLocation();
                for (const FVector2D& Sample : Samples)
                {
                    FVector Surface;
                    FLDLegalGround Legal;
                    ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
                    FString Why;
                    if (!Level(Centre + Sample, Focus, Surface)
                        || !Registry->QueryGround(Registry->Revision(), Surface, Capsule, Flag->Surface, Legal, Failure, Why)
                        || !Seen(Eye, Surface, true))
                    {
                        return false;
                    }
                }
                // The dash run: one continuous capsule sweep over level support, its body seen from the camera.
                const FVector RunStart(Centre + DashFrom, Focus.Z + HalfHeight + 5.);
                if (Target.SweepTestByChannel(RunStart, RunStart + FVector(VfxDashRunCm, 0., 0.), FQuat::Identity, ECC_Pawn, Capsule, Sight))
                {
                    return false;
                }
                for (int32 Step = 0; Step * 25. <= VfxDashRunCm; ++Step)
                {
                    FVector Surface;
                    FLDLegalGround Legal;
                    ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
                    FString Why;
                    if (!Level(Centre + DashFrom + FVector2D(Step * 25., 0.), Focus, Surface)
                        || !Registry->QueryGround(Registry->Revision(), Surface, Capsule, Flag->Surface, Legal, Failure, Why)
                        || (Step % 4 == 0 && !Seen(Eye, Surface, false)))
                    {
                        return false;
                    }
                }
                return true;
            };
            for (int32 Ring = 0; Ring < 24; ++Ring)
            {
                for (int32 Bearing = 0; Bearing < 16; ++Bearing)
                {
                    const FVector2D Centre = FVector2D(Flag->Position) + FVector2D(FlagReach + 400. + Ring * 250., 0.).GetRotated(22.5 * Bearing);
                    if (Open(Centre, OutFocus)) { return true; }
                }
            }
            Error = FString::Printf(TEXT("No open floor within %.0f cm of the flagpole at %s."), FlagReach + 400. + 23 * 250., *Flag->Position.ToCompactString());
            return false;
        }

        bool SetupVfx(UWorld& Target)
        {
            auto* Impacts = Target.GetSubsystem<ULDImpactFxSubsystem>();
            const ULDCombatVfxCatalog* Effects = Impacts ? Impacts->GetCatalog() : nullptr;
            UNiagaraSystem* Trail = Effects ? Effects->SwingTrail.Get() : nullptr;
            if (!Check(Trail != nullptr, TEXT("The authored swing trail is available"))) { return false; }
#if WITH_EDITORONLY_DATA
            Trail->WaitForCompilationComplete(true, false);
#endif
            if (!Check(Trail->IsReadyToRun(), TEXT("The authored swing trail compiled"))) { return false; }
            FVector Focus = FVector::ZeroVector;
            FString StageError;
            if (!Check(FindVfxStage(Target, Focus, StageError), TEXT("The level has open floor around the flagpole for the combat VFX stage ") + StageError)) { return false; }
            Check(true, TEXT("Combat VFX stage at ") + Focus.ToCompactString());
            const FVector Across(UE_INV_SQRT_2, UE_INV_SQRT_2, 0.);
            VfxFocus = Focus;
            VfxAttacker = SpawnVfxBody(Target, Focus - Across * 95., 45.f, 0);
            VfxVictim = SpawnVfxBody(Target, Focus + Across * 95., 225.f, 1);
            if (!Check(VfxAttacker.IsValid() && VfxVictim.IsValid(), TEXT("Combat VFX bodies spawned"))) { return false; }
            // The attacker stands in for the local player's own body (full size, ball orange).
            VfxAttacker->SetOwnBody(true);
            {
                TSharedPtr<const FLDDemoContentSnapshot> Content;
                FString Ignored;
                if (LDDemoEntry::LoadRuntimeContent(Content, Ignored) && Content && Content->Get().Parameters.BasicAttackWindupMs > 0)
                { BasicWindupSeconds = float(Content->Get().Parameters.BasicAttackWindupMs) / 1000.f; }
            }
            // A skill without a windup of its own shows its family's (a thrown shoe: the hand gather and the aim line), bound to
            // the caster: hiding the caster clears it at once.
            const int32 BeforeCast = Impacts ? Impacts->LiveCount() : 0;
            VfxAttacker->SetCastAbility(uint8(ELDDemoAbilityId::ActiveSnarkBolt));
            VfxAttacker->SetCue(2, .05f, .18f);
            VfxAttacker->Animate(FVector::ZeroVector, .02f);
            Check(Impacts && Impacts->LiveCount() > BeforeCast, TEXT("A visible cast starts its family windup"));
            VfxAttacker->SetVisibility(false, true);
            Check(Impacts && Impacts->LiveCount() == BeforeCast, TEXT("Hiding the caster immediately clears its windup"));
            VfxAttacker->SetVisibility(true, true);
            VfxAttacker->SetCue(0);
            VfxAttacker->SetCastAbility(0);
            // Every live effect of either kind: Niagara bursts, hit layers and slice layers.
            const auto LiveEffects = [Impacts]() { return Impacts ? Impacts->LiveCount() + Impacts->LiveLayerCount() + Impacts->LiveSliceCount() : 0; };
            VfxAttacker->SetHitReaction(.03f);
            VfxAttacker->Animate(FVector::ZeroVector, 0.f);
            const int32 OtherParticles = LiveEffects();
            VfxVictim->SetHitReaction(.03f);
            VfxVictim->Animate(FVector::ZeroVector, 0.f);
            const int32 BothParticles = LiveEffects();
            Check(OtherParticles > 0 && BothParticles > OtherParticles, TEXT("Two visible bodies produce independent impact effects"));
            VfxVictim->SetVisibility(false, true);
            Check(LiveEffects() == OtherParticles, TEXT("Hiding one body immediately clears only its impact effects"));
            VfxVictim->SetVisibility(true, true);

            auto* Proxy = Target.SpawnActor<ALDObservedEntityProxy>();
            if (Check(Proxy != nullptr, TEXT("Observed proxy spawned for impact lifecycle")))
            {
                FLDObservedUnitView Unit;
                Unit.Handle = FGuid::NewGuid();
                Unit.ObservationEpoch = 1;
                Unit.RecentHitAgePlusOneMs = 31;
                Unit.RecentHitId = FGuid::NewGuid();
                Proxy->Apply(Unit, GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), 100.);
                Proxy->Present(100., 0.f);
                const int32 BeforeConceal = LiveEffects();
                Proxy->Destroy();
                Check(BeforeConceal > OtherParticles && LiveEffects() == OtherParticles,
                    TEXT("Removing an observed proxy immediately clears only its impact effects"));
            }
            if (Impacts)
            {
                Impacts->ClearFor(*VfxAttacker.Get());
                Impacts->ClearFor(*VfxVictim.Get());
            }
            Shots = {
                {TEXT("VFX-Idle"), Focus, LDCamera::DefaultArmLength},
                {TEXT("VFX-Swing"), Focus, LDCamera::DefaultArmLength, false, 1},
                {TEXT("VFX-Impact"), Focus, LDCamera::DefaultArmLength, false, 1, ELDObservedHitLook::None, 0, true, .04f},
                {TEXT("VFX-ImpactLate"), Focus, LDCamera::DefaultArmLength, false, 1, ELDObservedHitLook::None, 0, true, .16f},
                {TEXT("VFX-Critical"), Focus, LDCamera::DefaultArmLength, false, 1, ELDObservedHitLook::None, LDObservation::Critical, true},
                {TEXT("VFX-ShieldBreak"), Focus, LDCamera::DefaultArmLength, false, 1, ELDObservedHitLook::None,
                    LDObservation::ShieldAbsorbed | LDObservation::ShieldBroken, true},
                {TEXT("VFX-Fire"), Focus, LDCamera::DefaultArmLength, false, 1, ELDObservedHitLook::Fire, 0, true},
                {TEXT("VFX-Water"), Focus, LDCamera::DefaultArmLength, false, 1, ELDObservedHitLook::Water, 0, true},
                {TEXT("VFX-Electric"), Focus, LDCamera::DefaultArmLength, false, 1, ELDObservedHitLook::Electric, 0, true},
                {TEXT("VFX-Magic"), Focus, LDCamera::DefaultArmLength, false, 1, ELDObservedHitLook::Magic, 0, true},
                {TEXT("VFX-Shield"), Focus, LDCamera::DefaultArmLength, false, 1, ELDObservedHitLook::None, LDObservation::ShieldAbsorbed, true},
                {TEXT("VFX-Cast"), Focus, LDCamera::DefaultArmLength, false, 2},
                {TEXT("VFX-SkillZap"), Focus, LDCamera::DefaultArmLength, false, 2, ELDObservedHitLook::None, 0, false, .08f, false,
                    uint8(ELDDemoAbilityId::ActiveZap), false, false, ELDObservedProjectileLook::Chalk, 0.f, .24f},
                {TEXT("VFX-SkillSeethe"), Focus, LDCamera::DefaultArmLength, false, 2, ELDObservedHitLook::None, 0, false, .08f, false,
                    uint8(ELDDemoAbilityId::ActiveSeethe), false, false, ELDObservedProjectileLook::Chalk, 0.f, .26f},
                {TEXT("VFX-SkillNotMad"), Focus, LDCamera::DefaultArmLength, false, 2, ELDObservedHitLook::None, 0, false, .08f, false,
                    uint8(ELDDemoAbilityId::ActiveNotMad), false, false, ELDObservedProjectileLook::Chalk, 0.f, .26f},
                {TEXT("VFX-Dash"), Focus, LDCamera::DefaultArmLength, false, 0, ELDObservedHitLook::None, 0, false, .08f, false,
                    uint8(ELDDemoAbilityId::ActiveBailOut), true, false, ELDObservedProjectileLook::Chalk, 0.f, .2f},
                {TEXT("VFX-ZoneFire"), Focus, LDCamera::DefaultArmLength, false, 0, ELDObservedHitLook::None, 0, false, .08f, false,
                    0, false, true, ELDObservedProjectileLook::AreaFire, 220.f, .9f},
                {TEXT("VFX-ZoneWater"), Focus, LDCamera::DefaultArmLength, false, 0, ELDObservedHitLook::None, 0, false, .08f, false,
                    0, false, true, ELDObservedProjectileLook::AreaWater, 220.f, .9f},
                {TEXT("VFX-ZoneElectric"), Focus, LDCamera::DefaultArmLength, false, 0, ELDObservedHitLook::None, 0, false, .08f, false,
                    0, false, true, ELDObservedProjectileLook::AreaElectric, 220.f, .6f},
                {TEXT("VFX-Chalk"), Focus, LDCamera::DefaultArmLength, false, 0, ELDObservedHitLook::None, 0, false, .08f, false,
                    0, false, true, ELDObservedProjectileLook::Chalk, 0.f, .1f},
                // Last: the fall leaves the victim fallen.
                {TEXT("VFX-Kill"), Focus, LDCamera::DefaultArmLength, false, 1, ELDObservedHitLook::None, 0, false, .08f, true},
            };
            {
                // The follow-up shots sit before the final kill shot.
                const auto Add = [this, &Focus](const TCHAR* Name, TFunctionRef<void(FArtShot&)> Fill)
                {
                    FArtShot Shot;
                    Shot.Name = Name;
                    Shot.Focus = Focus;
                    Fill(Shot);
                    Shots.Insert(Shot, Shots.Num() - 1);
                };
                Add(TEXT("VFX-ReactionFire"), [](FArtShot& S) { S.ReactionElement = uint8(ELDCombatElement::Fire); S.CaptureDelay = .08f; });
                Add(TEXT("VFX-ReactionChill"), [](FArtShot& S) { S.ReactionElement = uint8(ELDCombatElement::Chill); S.CaptureDelay = .08f; });
                Add(TEXT("VFX-GroundFire"), [](FArtShot& S) { S.GroundKind = uint8(ELDGroundStateKind::Fire); S.CaptureDelay = .9f; });
                Add(TEXT("VFX-GroundField"), [](FArtShot& S) { S.GroundKind = uint8(ELDGroundStateKind::Field); S.CaptureDelay = .6f; });
                Add(TEXT("VFX-GroundMiasma"), [](FArtShot& S) { S.GroundKind = uint8(ELDGroundStateKind::Miasma); S.CaptureDelay = 1.2f; });
                Add(TEXT("VFX-ChalkFlying"), [](FArtShot& S) { S.bEffect = true; S.Effect = ELDObservedProjectileLook::Chalk; S.bFlying = true; S.CaptureDelay = .3f; });
                Add(TEXT("VFX-EdgeFlash"), [](FArtShot& S) { S.Action = 1; S.bHit = true; S.HitFlags = LDObservation::Critical; S.bShowUI = true; });
            }
            if (FParse::Param(FCommandLine::Get(), TEXT("LDDemoCastVfxCapture")))
            {
                Shots.RemoveAll([](const FArtShot& Shot) { return Shot.Name != TEXT("VFX-Idle") && Shot.Name != TEXT("VFX-Cast"); });
            }
            else if (!BuildSequences(Target, Focus)) { return false; }
            FString Categories;
            if (FParse::Value(FCommandLine::Get(), TEXT("LDDemoVfxProof="), Categories, false))
            {
                // The proof harness replaces the keyframe sequences and the single shots (only the idle frame stays).
                Sequences.Reset();
                Shots.RemoveAll([](const FArtShot& Shot) { return Shot.Name != TEXT("VFX-Idle"); });
                FParse::Value(FCommandLine::Get(), TEXT("LDDemoVfxProofLabel="), ProofLabel);
                if (!BuildChainHarness(Target, Focus, Categories)) { return false; }
            }
            FActorSpawnParameters Spawn;
            Spawn.ObjectFlags |= RF_Transient;
            ACameraActor* View = Target.SpawnActor<ACameraActor>(Spawn);
            auto* Controller = Cast<ALDPlayerController>(Target.GetFirstPlayerController());
            if (!Check(View && Controller, TEXT("Combat VFX capture has a camera and local controller"))) { return false; }
            const UCameraComponent* Game = GetDefault<ALDCharacter>()->Camera;
            View->GetCameraComponent()->SetFieldOfView(Game->FieldOfView);
            View->GetCameraComponent()->PostProcessSettings = Game->PostProcessSettings;
            View->GetCameraComponent()->PostProcessBlendWeight = 1.f;
            View->GetCameraComponent()->bConstrainAspectRatio = false;
            Camera = View;
            CaptureController = Controller;
            Controller->SetDevArtCaptureCamera(View);
            return true;
        }

        // Extra stage bodies on the stage search's checked ring (320 cm around the focus) and the keyframe sequences.
        bool BuildSequences(UWorld& Target, const FVector& Focus)
        {
            const auto Ring = [&Focus](float Degrees) { return Focus + FVector(320., 0., 0.).RotateAngleAxis(Degrees, FVector::UpVector); };
            VfxExtras = { SpawnVfxBody(Target, Ring(0.f), 180.f, 2), SpawnVfxBody(Target, Ring(45.f), 225.f, 3),
                SpawnVfxBody(Target, Ring(180.f), 0.f, 4), SpawnVfxBody(Target, Ring(270.f), 90.f, 5) };
            if (!Check(!VfxExtras.ContainsByPredicate([](const TWeakObjectPtr<ULDHumanoidVisualComponent>& Body) { return !Body.IsValid(); }),
                TEXT("Combat VFX sequence bodies spawned on the checked stage ring"))) { return false; }
            const TWeakObjectPtr<ULDHumanoidVisualComponent> A = VfxAttacker, V1 = VfxVictim, V2 = VfxExtras[0], V3 = VfxExtras[1], A2 = VfxExtras[2], A3 = VfxExtras[3];
            const float W = BasicWindupSeconds;
            const auto Swing = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, uint8 Step, float Windup)
            { return [Body, Step, Windup]() { if (auto* B = Body.Get()) { B->SetComboStep(Step); B->SetCastAbility(0); B->SetCue(1, 0.f, Windup); } }; };
            const auto Cast = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, ELDDemoAbilityId Ability, float Windup)
            { return [Body, Ability, Windup]() { if (auto* B = Body.Get()) { B->SetComboStep(0); B->SetCastAbility(uint8(Ability)); B->SetCue(2, 0.f, Windup); } }; };
            const auto Hit = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, ELDObservedHitLook Look)
            { return [Body, Look]() { if (auto* B = Body.Get()) { B->SetHitReaction(0.f, Look, 0, FGuid::NewGuid()); } }; };
            const auto Stop = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body)
            { return [Body]() { if (auto* B = Body.Get()) { B->SetCue(0); B->SetCastAbility(0); B->SetComboStep(0); } }; };
            const auto Conduct = [this](TWeakObjectPtr<ULDHumanoidVisualComponent> Body)
            { return [this, Body]() { if (auto* B = Body.Get()) { B->SetReaction(uint8(ELDCombatReaction::Conduct), uint8(ELDCombatElement::Shock), 0.f, 500. + SeqIndex); } }; };
            const auto Shots_ = [](std::initializer_list<float> Times)
            {
                TArray<FSeqShot> Out;
                int32 Index = 0;
                for (const float At : Times) { Out.Add({At, FString::Printf(TEXT("%02d"), ++Index)}); }
                return Out;
            };
            for (uint8 Step = 1; Step <= 3; ++Step)
            {
                // Anticipation (chalk), release (slash), impact (the step's tier), follow-through, dissipation, clear.
                Sequences.Add({FString::Printf(TEXT("combo%d"), Step), {{0.f, Swing(A, Step, W)}, {W + .02f, Hit(V1, ELDObservedHitLook::None)}, {1.2f, Stop(A)}},
                    Shots_({W * .5f, W + .01f, W + .06f, W + .16f, W + .34f, W + .7f})});
            }
            Sequences.Add({TEXT("hose_down"), {{0.f, Cast(A, ELDDemoAbilityId::ActiveHoseDown, .2f)}, {.23f, Hit(V1, ELDObservedHitLook::Water)}, {1.2f, Stop(A)}},
                Shots_({.1f, .215f, .27f, .4f, .8f, 1.5f})});
            // The zap chain is the Niagara Chain template: the own cast (full width and brightness), then another body's cast
            // over the same bodies (thinner and dimmer), so the user parameters show in the frames.
            Sequences.Add({TEXT("zap-niagara"), {{0.f, Cast(A, ELDDemoAbilityId::ActiveZap, .25f)}, {.27f, Hit(V1, ELDObservedHitLook::Electric)},
                {.42f, Hit(V2, ELDObservedHitLook::Electric)}, {.57f, Hit(V3, ELDObservedHitLook::Electric)}, {1.2f, Stop(A)}},
                Shots_({.12f, .26f, .29f, .33f, .45f, .6f, .75f, .95f})});
            Sequences.Add({TEXT("zap-niagara-others"), {{0.f, Cast(A3, ELDDemoAbilityId::ActiveZap, .25f)}, {.27f, Hit(V3, ELDObservedHitLook::Electric)},
                {.42f, Hit(V2, ELDObservedHitLook::Electric)}, {.57f, Hit(V1, ELDObservedHitLook::Electric)}, {1.2f, Stop(A3)}},
                Shots_({.12f, .26f, .29f, .33f, .45f, .6f, .75f, .95f})});
            Sequences.Add({TEXT("conduct"), {{0.f, Cast(A, ELDDemoAbilityId::ActiveZap, .25f)}, {.27f, Hit(V1, ELDObservedHitLook::Electric)}, {.28f, Conduct(V1)},
                {1.2f, Stop(A)}}, Shots_({.2f, .32f, .4f, .6f, 1.f, 1.6f})});
            Sequences.Add({TEXT("ground_field"), {{0.f, [this, V1]()
                {
                    FActorSpawnParameters Spawn;
                    Spawn.ObjectFlags |= RF_Transient;
                    ALDObservedGroundProxy* Ground = World->SpawnActor<ALDObservedGroundProxy>(Spawn);
                    if (!Check(Ground != nullptr && V1.IsValid(), TEXT("Sequence ground state proxy spawned"))) { return; }
                    FLDObservedGroundState State;
                    State.Id = 2;
                    State.Kind = uint8(ELDGroundStateKind::Field);
                    State.CentreCm = V1->GetOwner()->GetActorLocation() - FVector(0, 0, 90);
                    State.RadiusCm = 220.f;
                    State.RemainingMs = State.TotalMs = 6000;
                    Ground->Apply(State, 100.);
                    SeqGround = Ground;
                }}, {2.3f, [this]() { if (ALDObservedGroundProxy* Old = SeqGround.Get()) { Old->Destroy(); } }}},
                Shots_({.1f, .4f, .8f, 1.3f, 2.f, 2.4f})});
            // Several casters at once: the own finisher and two other bodies' hose and zap, then a conduct on the first victim.
            Sequences.Add({TEXT("crowded"), {{0.f, Swing(A, 3, W)}, {0.f, Cast(A2, ELDDemoAbilityId::ActiveHoseDown, .2f)},
                {0.f, Cast(A3, ELDDemoAbilityId::ActiveZap, .25f)}, {W + .02f, Hit(V1, ELDObservedHitLook::None)},
                {.23f, Hit(V1, ELDObservedHitLook::Water)}, {.27f, Hit(V3, ELDObservedHitLook::Electric)}, {.42f, Hit(V2, ELDObservedHitLook::Electric)},
                {.45f, Conduct(V1)}, {1.2f, Stop(A)}, {1.2f, Stop(A2)}, {1.2f, Stop(A3)}},
                Shots_({.1f, .22f, .3f, .48f, .7f, 1.1f})});
            return true;
        }

        void SeqBodies(TFunctionRef<void(ULDHumanoidVisualComponent&)> Each)
        {
            for (const TWeakObjectPtr<ULDHumanoidVisualComponent>& Body : {VfxAttacker, VfxVictim}) { if (Body.IsValid()) { Each(*Body.Get()); } }
            for (const TWeakObjectPtr<ULDHumanoidVisualComponent>& Body : VfxExtras) { if (Body.IsValid()) { Each(*Body.Get()); } }
        }

        // One tick of the keyframe sequences; false ends the probe. Runs before the single shots.
        bool TickSequence(UWorld& Target, double Now, float SimDelta)
        {
            auto* Impacts = Target.GetSubsystem<ULDImpactFxSubsystem>();
            if (!Impacts) { return Finish(TEXT("No impact effect subsystem for the sequences")); }
            if (!bSeqWarmed)
            {
                // Show every slice recipe once and wait for its shaders, so no keyframe shows a default material.
                if (SeqWarmUntil < 0.)
                {
                    LDSliceVfx::FLook Look;
                    Look.bOwn = Look.bFocus = true;
                    const FVector At = VfxFocus + FVector(0, 0, 100);
                    for (uint8 Strike = uint8(LDSliceVfx::EStrike::Basic); Strike <= uint8(LDSliceVfx::EStrike::Zap); ++Strike)
                    { Impacts->Slice(nullptr, LDSliceVfx::Impact(LDSliceVfx::EStrike(Strike), At, VfxFocus, FVector::ForwardVector, VfxFocus, false, Look), 0.f); }
                    SeqWarmUntil = Now + 3.;
                }
                if (Now < SeqWarmUntil || (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())) { return true; }
                Check(Impacts->LiveSliceCount() == 0, TEXT("The warm-up slice layers expired on their own"));
                bSeqWarmed = true;
                return true;
            }
            FSequence& Sequence = Sequences[SeqIndex];
            if (!PendingImage.IsEmpty())
            {
                if (IFileManager::Get().FileSize(*PendingImage) <= 0) { return true; }
                Check(true, TEXT("Rendered keyframe: ") + PendingImage.RightChop(Directory.Len() + 1));
                PendingImage.Reset();
                UGameplayStatics::SetGlobalTimeDilation(&Target, SeqDilation);
                return true;
            }
            if (!bSeqStarted)
            {
                // A clean stage: every body idle and visible, no effect from the previous sequence.
                SeqBodies([Impacts](ULDHumanoidVisualComponent& Body)
                {
                    Body.SetCue(0);
                    Body.SetCastAbility(0);
                    Body.SetComboStep(0);
                    Impacts->ClearFor(Body);
                });
                if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { return true; }
                IFileManager::Get().MakeDirectory(*(Directory / (TEXT("seq-") + Sequence.Name)), true);
                bSeqStarted = true;
                SeqClock = 0.;
                SeqStep = SeqShot = 0;
                UGameplayStatics::SetGlobalTimeDilation(&Target, SeqDilation);
                return true;
            }
            SeqClock += SimDelta;
            const int32 Fired = SeqStep;
            while (SeqStep < Sequence.Steps.Num() && Sequence.Steps[SeqStep].At <= SeqClock) { Sequence.Steps[SeqStep++].Do(); }
            // A step fired now reaches the bodies on their next frame: never capture before it shows.
            if (SeqStep != Fired) { return true; }
            if (SeqShot < Sequence.Shots.Num() && SeqClock >= Sequence.Shots[SeqShot].At)
            {
                // Time stands (almost) still while this frame renders and is written.
                UGameplayStatics::SetGlobalTimeDilation(&Target, SeqFreeze);
                PendingImage = Directory / (TEXT("seq-") + Sequence.Name) / (Sequence.Shots[SeqShot].Label + TEXT(".png"));
                Images.Add(MakeShared<FJsonValueString>(PendingImage.RightChop(Directory.Len() + 1)));
                FScreenshotRequest::RequestScreenshot(PendingImage, false, false);
                ++SeqShot;
                return true;
            }
            if (SeqShot >= Sequence.Shots.Num() && SeqStep >= Sequence.Steps.Num())
            {
                UGameplayStatics::SetGlobalTimeDilation(&Target, 1.f);
                bSeqStarted = false;
                // The single shots that follow stage only the attacker and the first victim, as the earlier captures did.
                if (++SeqIndex == Sequences.Num()) { for (const TWeakObjectPtr<ULDHumanoidVisualComponent>& Extra : VfxExtras) { if (Extra.IsValid()) { Extra->SetVisibility(false, true); } } }
            }
            return true;
        }

        static int32 ParticlesOf(UNiagaraComponent& Effect)
        {
            int32 Count = 0;
            if (FNiagaraSystemInstanceControllerConstPtr Controller = Effect.GetSystemInstanceController())
            {
                if (const FNiagaraSystemInstance* Instance = Controller->GetSystemInstance_Unsafe())
                {
                    for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters()) { Count += Emitter->GetNumParticles(); }
                }
            }
            return Count;
        }

        // The judging scenarios of every requested category, then its frame-cost runs. Real casts, swings, hits and ground
        // states on the stage bodies: A (own) at the near diagonal, V1 across it, V2 and V3 on the checked ring, A2 and A3
        // the other casters.
        bool BuildChainHarness(UWorld& Target, const FVector& Focus, const FString& Categories)
        {
            TArray<FString> Wanted;
            Categories.ParseIntoArray(Wanted, TEXT(","), true);
            for (const FString& Name : Wanted)
            {
                // One category per line, so streams adding categories never edit the same line.
                static const TCHAR* const Known[] = {
                    TEXT("chain"),
                    TEXT("hit"),
                    TEXT("swing"),
                    TEXT("field"),
                    TEXT("water"),
                    TEXT("fire"),
                    TEXT("chill"),
                    TEXT("states"),
                    TEXT("feel"),
                    TEXT("grounds"),
                    TEXT("reel"),
                    TEXT("poses"),
                    TEXT("kill"),
                    TEXT("casts"),
                    TEXT("skills"),
                    TEXT("self"),
                    TEXT("packs"),
                    TEXT("reactions"),
                    TEXT("flights"),
                    TEXT("world"),
                    TEXT("load"),
                };
                bool bKnown = false;
                for (const TCHAR* Category : Known) { bKnown |= Name == Category; }
                if (!Check(bKnown, TEXT("Proof category known: ") + Name)) { return false; }
            }
            const TWeakObjectPtr<ULDHumanoidVisualComponent> A = VfxAttacker, V1 = VfxVictim, V2 = VfxExtras[0], V3 = VfxExtras[1], A2 = VfxExtras[2], A3 = VfxExtras[3];
            const auto Cast = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body)
            { return [Body]() { if (auto* B = Body.Get()) { B->SetComboStep(0); B->SetCastAbility(uint8(ELDDemoAbilityId::ActiveZap)); B->SetCue(2, 0.f, .25f); } }; };
            const auto Hit = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body)
            { return [Body]() { if (auto* B = Body.Get()) { B->SetHitReaction(0.f, ELDObservedHitLook::Electric, 0, FGuid::NewGuid()); } }; };
            const auto Own = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, bool bOwn) { if (auto* B = Body.Get()) { B->SetOwnBody(bOwn); } };
            const FVector V1Home = V1.IsValid() ? V1->GetOwner()->GetActorLocation() : FVector::ZeroVector;
            const FVector AHome = A.IsValid() ? A->GetOwner()->GetActorLocation() : FVector::ZeroVector;
            const FRotator AFacing = A.IsValid() ? A->GetOwner()->GetActorRotation() : FRotator::ZeroRotator;
            UWorld* const SunWorld = &Target;
            // Both bodies of an own exchange moved onto the warm sunlit floor: the floor points under two screen points of
            // the sunlit band below the stage, 140 px (about 250 cm) apart.
            const auto Sunlit = [SunWorld, A, V1, AHome]()
            {
                APlayerController* Viewer = UGameplayStatics::GetPlayerController(SunWorld, 0);
                const auto Floor = [Viewer, AHome](const FVector2D& Pixel)
                {
                    FVector Origin, Dir;
                    if (!Viewer || !Viewer->DeprojectScreenPositionToWorld(Pixel.X, Pixel.Y, Origin, Dir) || FMath::IsNearlyZero(Dir.Z)) { return AHome; }
                    return Origin + Dir * ((AHome.Z - Origin.Z) / Dir.Z);
                };
                if (auto* B = A.Get()) { B->GetOwner()->SetActorLocation(Floor(FVector2D(640., 820.))); B->GetOwner()->SetActorRotation(FRotator(0, 0.f, 0)); }
                if (auto* B = V1.Get()) { B->GetOwner()->SetActorLocation(Floor(FVector2D(780., 820.))); B->GetOwner()->SetActorRotation(FRotator(0, 180.f, 0)); }
            };
            const auto Home = [A, V1, AHome, AFacing, V1Home]()
            {
                if (auto* B = A.Get()) { B->GetOwner()->SetActorLocation(AHome); B->GetOwner()->SetActorRotation(AFacing); }
                if (auto* B = V1.Get()) { B->GetOwner()->SetActorLocation(V1Home); B->GetOwner()->SetActorRotation(FRotator(0, 225.f, 0)); }
            };
            // V1 right in front of A (140 cm), facing it, for the melee exchanges.
            const auto Close = [A, V1, AHome, AFacing]()
            {
                if (auto* B = V1.Get()) { B->GetOwner()->SetActorLocation(AHome + AFacing.Vector() * 140.); B->GetOwner()->SetActorRotation((-AFacing.Vector()).Rotation()); }
            };
            const auto CloseSunlit = [Sunlit, A, V1]()
            {
                Sunlit();
                if (A.IsValid() && V1.IsValid())
                {
                    const FVector From = A->GetOwner()->GetActorLocation(), To = V1->GetOwner()->GetActorLocation();
                    A->GetOwner()->SetActorRotation((To - From).Rotation());
                    V1->GetOwner()->SetActorLocation(From + (To - From).GetSafeNormal() * 140.);
                }
            };
            const float W = BasicWindupSeconds;
            const auto Swing = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, uint8 Step, float Windup)
            { return [Body, Step, Windup]() { if (auto* B = Body.Get()) { B->SetComboStep(Step); B->SetCastAbility(0); B->SetCue(1, 0.f, Windup); } }; };
            const auto Plain = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body)
            { return [Body]() { if (auto* B = Body.Get()) { B->SetHitReaction(0.f, ELDObservedHitLook::None, 0, FGuid::NewGuid()); } }; };
            const auto Conduct = [this](TWeakObjectPtr<ULDHumanoidVisualComponent> Body)
            { return [this, Body]() { if (auto* B = Body.Get()) { B->SetReaction(uint8(ELDCombatReaction::Conduct), uint8(ELDCombatElement::Shock), 0.f, 900. + ChainJob); } }; };
            // A ground electric field of this radius under a body for TotalMs (its authoritative state as the server sends it),
            // laid by the contender in seat OwnerSeat (255 none); its life runs on the scenario clock to its end.
            const auto Field = [this](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, float RadiusCm, int32 TotalMs, uint32 Id, uint8 OwnerSeat = 255)
            {
                return [this, Body, RadiusCm, TotalMs, Id, OwnerSeat]()
                {
                    FActorSpawnParameters Spawn;
                    Spawn.ObjectFlags |= RF_Transient;
                    ALDObservedGroundProxy* Ground = World->SpawnActor<ALDObservedGroundProxy>(Spawn);
                    if (!Check(Ground != nullptr && Body.IsValid(), TEXT("Proof ground state proxy spawned"))) { return; }
                    FLDObservedGroundState State;
                    State.Id = Id;
                    State.Kind = uint8(ELDGroundStateKind::Field);
                    State.CentreCm = Body->GetOwner()->GetActorLocation() - FVector(0, 0, 90);
                    State.RadiusCm = RadiusCm;
                    State.OwnerSeat = OwnerSeat;
                    State.RemainingMs = State.TotalMs = TotalMs;
                    Ground->Apply(State, 100.);
                    ProofGrounds.Add(Ground);
                    ProofGroundsAt.Add(ChainClock);
                };
            };
            const auto ClearGrounds = [this]()
            {
                for (const TWeakObjectPtr<ALDObservedGroundProxy>& Ground : ProofGrounds) { if (ALDObservedGroundProxy* Old = Ground.Get()) { Old->Destroy(); } }
                ProofGrounds.Reset();
                ProofGroundsAt.Reset();
            };
            // Any active cast with its windup (the approved level-1 timing a viewer sees), and a hit of a look.
            const auto CastOf = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, ELDDemoAbilityId Ability, float Windup)
            { return [Body, Ability, Windup]() { if (auto* B = Body.Get()) { B->SetComboStep(0); B->SetCastAbility(uint8(Ability)); B->SetCue(2, 0.f, Windup); } }; };
            const auto HitOf = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, ELDObservedHitLook HitLook)
            { return [Body, HitLook]() { if (auto* B = Body.Get()) { B->SetHitReaction(0.f, HitLook, 0, FGuid::NewGuid()); } }; };
            // V1 at Range ahead of A (A's facing), facing it.
            const auto Ahead = [A, V1, AHome, AFacing](double Range)
            {
                return [A, V1, AHome, AFacing, Range]()
                { if (auto* B = V1.Get()) { B->GetOwner()->SetActorLocation(AHome + AFacing.Vector() * Range); B->GetOwner()->SetActorRotation((-AFacing.Vector()).Rotation()); } };
            };
            // The own contender's seat (A is the local player's body) and another's.
            const uint8 OwnSeat = A.IsValid() ? A->PaletteSlot() : 255, OtherSeat = V2.IsValid() ? V2->PaletteSlot() : 255;
            for (const FString& Look : Wanted)
            {
                // The reel rides on the feel category (feel,reel replaces the feel sample with the reel).
                if (Look == TEXT("reel")) { continue; }
                if (Look == TEXT("hit") || Look == TEXT("swing"))
                {
                    // The same exchanges judged at the hit (hit) or just after the release (swing): combo steps 1-3 on V1 in
                    // front of A, the finisher on the sunlit floor, and A's finisher with A2 and A3 swinging at V2 and V3.
                    const bool bHit = Look == TEXT("hit");
                    for (uint8 Step = 1; Step <= 3; ++Step)
                    {
                        ChainJobs.Add({Look, FString::Printf(TEXT("%s-combo%d"), *Look, Step), {{0.f, Swing(A, Step, W)}, {W + .02f, Plain(V1)}},
                            Close, Home, false, bHit ? W + .02f : W - .02f, .1f, 24});
                    }
                    ChainJobs.Add({Look, Look + TEXT("-sunlit"), {{0.f, Swing(A, 3, W)}, {W + .02f, Plain(V1)}}, CloseSunlit, Home, false, bHit ? W + .02f : W - .02f, .1f, 24});
                    // The anticipation: the finisher's floor arc judged in the middle of its windup.
                    if (!bHit) { ChainJobs.Add({Look, TEXT("swing-tell"), {{0.f, Swing(A, 3, W)}, {W + .02f, Plain(V1)}}, Close, Home, false, W * .6f - .06f, 0.f, 24}); }
                    // The local player as the attacker (its own soft orange): A's finisher on V1, A own and V1 not for once.
                    const bool bVictimOwn = V1.IsValid() && V1->IsOwnBody();
                    ChainJobs.Add({Look, Look + TEXT("-own"), {{0.f, Swing(A, 3, W)}, {W + .02f, Plain(V1)}},
                        [Close, A, V1]() { Close(); if (auto* B = A.Get()) { B->SetOwnBody(true); } if (auto* B = V1.Get()) { B->SetOwnBody(false); } },
                        [Home, A, V1, bVictimOwn]() { Home(); if (auto* B = A.Get()) { B->SetOwnBody(false); } if (auto* B = V1.Get()) { B->SetOwnBody(bVictimOwn); } },
                        false, bHit ? W + .02f : W - .02f, .1f, 24});
                    ChainJobs.Add({Look, Look + TEXT("-crowd"), {{0.f, Swing(A, 3, W)}, {.05f, Swing(A2, 1, W)}, {.1f, Swing(A3, 2, W)},
                        {W + .02f, Plain(V1)}, {W + .07f, Plain(V2)}, {W + .12f, Plain(V3)}}, Close, Home, false, bHit ? W + .07f : W + .03f, .1f, 24});
                }
                else if (Look == TEXT("field"))
                {
                    // The persistent electric field at its authoritative radius from its start to its end (1.2 s here), with
                    // bodies standing in it; on the sunlit floor; the conduct reaction's splash field; two fields with an
                    // exchange over them.
                    ChainJobs.Add({Look, TEXT("field-ground"), {{.05f, Field(V1, 220.f, 1200, 21, OwnSeat)}}, nullptr, ClearGrounds, false, .6f, 0.f, 48});
                    ChainJobs.Add({Look, TEXT("field-sunlit"), {{.05f, Field(V1, 220.f, 1200, 22, OwnSeat)}}, Sunlit, [Home, ClearGrounds]() { ClearGrounds(); Home(); }, false, .6f, 0.f, 48});
                    ChainJobs.Add({Look, TEXT("field-conduct"), {{0.f, [A]() { if (auto* B = A.Get()) { B->SetComboStep(0); B->SetCastAbility(uint8(ELDDemoAbilityId::ActiveZap)); B->SetCue(2, 0.f, .25f); } }},
                        {.27f, [V1]() { if (auto* B = V1.Get()) { B->SetHitReaction(0.f, ELDObservedHitLook::Electric, 0, FGuid::NewGuid()); } }}, {.28f, Conduct(V1)}},
                        nullptr, nullptr, false, .45f, .2f, 36});
                    ChainJobs.Add({Look, TEXT("field-crowd"), {{.05f, Field(V1, 220.f, 1500, 23, OwnSeat)}, {.05f, Field(V3, 220.f, 1500, 24, OtherSeat)}, {.2f, Swing(A, 3, W)},
                        {.2f + W + .02f, Plain(V1)}}, nullptr, ClearGrounds, false, .45f, 0.f, 36});
                    // The own field beside another contender's, both at the state's radius with a body in each.
                    ChainJobs.Add({Look, TEXT("field-own-other"), {{.05f, Field(V1, 220.f, 1200, 25, OwnSeat)}, {.05f, Field(V2, 220.f, 1200, 26, OtherSeat)}},
                        nullptr, ClearGrounds, false, .6f, 0.f, 48});
                }
                else if (Look == TEXT("water"))
                {
                    // active_hose_down (windup 0.2 s): its band during the windup, the stream and a hit 3 m ahead, the same on the
                    // sunlit floor and another player's; active_zap's hand charge during its windup (0.25 s).
                    const float Hose = .2f, Zap = .25f;
                    const auto Sun = [Sunlit, A, V1]()
                    {
                        Sunlit();
                        if (A.IsValid() && V1.IsValid())
                        {
                            const FVector From = A->GetOwner()->GetActorLocation(), To = V1->GetOwner()->GetActorLocation();
                            A->GetOwner()->SetActorRotation((To - From).Rotation());
                            V1->GetOwner()->SetActorLocation(From + (To - From).GetSafeNormal2D() * 300.);
                        }
                    };
                    ChainJobs.Add({Look, TEXT("water-tell"), {{0.f, CastOf(A, ELDDemoAbilityId::ActiveHoseDown, Hose)}, {Hose + .03f, HitOf(V1, ELDObservedHitLook::Water)}},
                        Ahead(300.), Home, false, Hose * .6f, 0.f, 12});
                    ChainJobs.Add({Look, TEXT("water-hose"), {{0.f, CastOf(A, ELDDemoAbilityId::ActiveHoseDown, Hose)}, {Hose + .03f, HitOf(V1, ELDObservedHitLook::Water)}},
                        Ahead(300.), Home, false, Hose + .06f, 0.f, 45});
                    ChainJobs.Add({Look, TEXT("water-sunlit"), {{0.f, CastOf(A, ELDDemoAbilityId::ActiveHoseDown, Hose)}, {Hose + .03f, HitOf(V1, ELDObservedHitLook::Water)}},
                        Sun, Home, false, Hose + .06f, .1f, 30});
                    ChainJobs.Add({Look, TEXT("water-other"), {{0.f, CastOf(A3, ELDDemoAbilityId::ActiveHoseDown, Hose)}, {Hose + .03f, HitOf(V3, ELDObservedHitLook::Water)}},
                        nullptr, nullptr, false, Hose + .06f, .1f, 30});
                    ChainJobs.Add({Look, TEXT("zap-charge"), {{0.f, CastOf(A, ELDDemoAbilityId::ActiveZap, Zap)}, {Zap + .02f, HitOf(V1, ELDObservedHitLook::Electric)}},
                        Close, Home, false, Zap * .7f, 0.f, 15});
                }
                else if (Look == TEXT("fire"))
                {
                    // active_seethe (windup 0.25 s, radius 300): its windup, the burst with V1 burnt 2 m away, the scorch
                    // fading, the same on the sunlit floor, and another player's.
                    const float Seethe = .25f;
                    const auto Burnt = [&](TWeakObjectPtr<ULDHumanoidVisualComponent> Caster, TWeakObjectPtr<ULDHumanoidVisualComponent> Victim)
                    { return TArray<FSeqStep>{{0.f, CastOf(Caster, ELDDemoAbilityId::ActiveSeethe, Seethe)}, {Seethe + .03f, HitOf(Victim, ELDObservedHitLook::Fire)}}; };
                    ChainJobs.Add({Look, TEXT("fire-tell"), Burnt(A, V1), Ahead(200.), Home, false, Seethe * .7f, 0.f, 12});
                    ChainJobs.Add({Look, TEXT("fire-seethe"), Burnt(A, V1), Ahead(200.), Home, false, Seethe + .1f, 0.f, 60});
                    ChainJobs.Add({Look, TEXT("fire-sunlit"), Burnt(A, V1), Sunlit, Home, false, Seethe + .1f, .15f, 30});
                    ChainJobs.Add({Look, TEXT("fire-other"), Burnt(A3, V3), nullptr, nullptr, false, Seethe + .1f, .15f, 30});
                }
                else if (Look == TEXT("skills"))
                {
                    // Skills with their own release and hit, each from its windup through its hit on V1 in front of A, then
                    // the same as another player's cast (A3 on V3). The approved level-1 windups.
                    struct FSkillShot { const TCHAR* Name; ELDDemoAbilityId Ability; float Windup; ELDObservedHitLook Hit; float HitAt; double Gap; };
                    for (const FSkillShot& Skill : {FSkillShot{TEXT("elbow"), ELDDemoAbilityId::ActiveElbow, .15f, ELDObservedHitLook::None, .17f, 150.},
                        FSkillShot{TEXT("pants"), ELDDemoAbilityId::ActivePantsDown, .14f, ELDObservedHitLook::None, .17f, 300.},
                        FSkillShot{TEXT("grab"), ELDDemoAbilityId::ActiveCrotchGrab, .12f, ELDObservedHitLook::None, .26f, 220.},
                        FSkillShot{TEXT("bail"), ELDDemoAbilityId::ActiveBailOut, .0f, ELDObservedHitLook::None, -1.f, 300.},
                        FSkillShot{TEXT("ram"), ELDDemoAbilityId::ActiveChargedRam, .1f, ELDObservedHitLook::None, .3f, 300.}})
                    {
                        TArray<FSeqStep> OwnSteps{{0.f, CastOf(A, Skill.Ability, Skill.Windup)}}, OtherSteps{{0.f, CastOf(A3, Skill.Ability, Skill.Windup)}};
                        if (Skill.HitAt > 0.f) { OwnSteps.Add({Skill.HitAt, HitOf(V1, Skill.Hit)}); OtherSteps.Add({Skill.HitAt, HitOf(V3, Skill.Hit)}); }
                        ChainJobs.Add({Look, FString(TEXT("skills-")) + Skill.Name, OwnSteps, Ahead(Skill.Gap), Home, false, Skill.Windup + .05f, 0.f, 36});
                        ChainJobs.Add({Look, FString(TEXT("skills-")) + Skill.Name + TEXT("-other"), OtherSteps, nullptr, nullptr, false, Skill.Windup + .05f, 0.f, 30});
                    }
                }
                else if (Look == TEXT("load"))
                {
                    // Frame cost of the heaviest scenes on the six stage bodies, each replayed for five seconds after three
                    // idle: every body casting active_seethe and taking its fire hit; six different reactions going off at
                    // once; a PK arena with its next ring and four throw landings.
                    UWorld* const LoadWorld = &Target;
                    const TSharedRef<TArray<TWeakObjectPtr<AActor>>> Rings = MakeShared<TArray<TWeakObjectPtr<AActor>>>();
                    const TSharedRef<double> Stamp = MakeShared<double>(500.);
                    const TArray<TWeakObjectPtr<ULDHumanoidVisualComponent>> LoadBodies{A, V1, V2, V3, A2, A3};
                    FChainJob Seethe;
                    Seethe.Category = Look;
                    Seethe.Scenario = TEXT("load-seethe-six");
                    Seethe.bPerf = true;
                    Seethe.LoadEvery = 1.f;
                    for (const auto& Body : LoadBodies) { Seethe.Steps.Add({0.f, CastOf(Body, ELDDemoAbilityId::ActiveSeethe, .25f)}); }
                    for (const auto& Body : LoadBodies) { Seethe.Steps.Add({.28f, HitOf(Body, ELDObservedHitLook::Fire)}); }
                    FChainJob React;
                    React.Category = Look;
                    React.Scenario = TEXT("load-reactions-six");
                    React.bPerf = true;
                    React.LoadEvery = 1.2f;
                    static constexpr ELDCombatReaction Kinds[6] = {ELDCombatReaction::Fog, ELDCombatReaction::Freeze, ELDCombatReaction::Overload, ELDCombatReaction::Melt,
                        ELDCombatReaction::Methane, ELDCombatReaction::Shatter};
                    static constexpr ELDCombatElement Elements[6] = {ELDCombatElement::Water, ELDCombatElement::Chill, ELDCombatElement::Shock, ELDCombatElement::Fire,
                        ELDCombatElement::Fire, ELDCombatElement::Shock};
                    for (int32 Index = 0; Index < 6; ++Index)
                    {
                        React.Steps.Add({0.f, [Body = LoadBodies[Index], Kind = Kinds[Index], Element = Elements[Index], Stamp]()
                            { if (auto* B = Body.Get()) { *Stamp += 1.; B->SetReaction(uint8(Kind), uint8(Element), 0.f, *Stamp); } }});
                    }
                    FChainJob Arena;
                    Arena.Category = Look;
                    Arena.Scenario = TEXT("load-arena-throws");
                    Arena.bPerf = true;
                    Arena.LoadEvery = 1.f;
                    // The arena is laid by the first wave, so the idle seconds before it are measured without it.
                    Arena.Steps.Add({0.f, [LoadWorld, Rings, A, V1, V2, V3, A2]()
                    {
                        if (!Rings->IsEmpty()) { return; }
                        const auto FeetOf = [](const TWeakObjectPtr<ULDHumanoidVisualComponent>& Body)
                        { return Body.IsValid() && Body->GetOwner() ? Body->GetOwner()->GetActorLocation() - FVector(0, 0, 90) : FVector::ZeroVector; };
                        ALDPKRingMarker* Mark = LoadWorld->SpawnActor<ALDPKRingMarker>();
                        FString Why;
                        if (!Mark || !Mark->Build(FeetOf(A), 1, 1, Why)) { return; }
                        Mark->Show(560., true, 400., {FeetOf(V1), FeetOf(V2), FeetOf(V3), FeetOf(A2)}, 150.);
                        Rings->Add(Mark);
                    }});
                    Arena.Teardown = [Rings]()
                    {
                        for (const TWeakObjectPtr<AActor>& Ring : *Rings) { if (AActor* Old = Ring.Get()) { Old->Destroy(); } }
                        Rings->Reset();
                    };
                    for (const FChainJob& Job : {Seethe, React, Arena}) { for (int32 Run = 0; Run < 2; ++Run) { ChainJobs.Add(Job); } }
                }
                else if (Look == TEXT("world"))
                {
                    // World marks on their real actors: the own body's mark under A, a flagpole circle held by one (round V1)
                    // and a contested one (round A3); then a shrinking arena round A with its next ring and a throw landing
                    // by V2.
                    UWorld* const MarkWorld = &Target;
                    const TSharedRef<TArray<TWeakObjectPtr<AActor>>> Marks = MakeShared<TArray<TWeakObjectPtr<AActor>>>();
                    const auto ClearMarks = [Marks]()
                    {
                        for (const TWeakObjectPtr<AActor>& Mark : *Marks) { if (AActor* Old = Mark.Get()) { Old->Destroy(); } }
                        Marks->Reset();
                    };
                    const auto FeetOf = [](const TWeakObjectPtr<ULDHumanoidVisualComponent>& Body)
                    { return Body.IsValid() && Body->GetOwner() ? Body->GetOwner()->GetActorLocation() - FVector(0, 0, 90) : FVector::ZeroVector; };
                    const auto OwnMark = [MarkWorld, Marks, A]()
                    {
                        APawn* Pawn = A.IsValid() ? ::Cast<APawn>(A->GetOwner()) : nullptr;
                        ALDOwnBodyMarker* Mark = Pawn ? MarkWorld->SpawnActor<ALDOwnBodyMarker>() : nullptr;
                        UE_LOG(LogLDContent, Display, TEXT("LD_PROOF_OWN_MARK pawn=%d marker=%d"), Pawn ? 1 : 0, Mark ? 1 : 0);
                        if (!Mark) { return; }
                        Mark->Track(Pawn, 2);
                        Marks->Add(Mark);
                    };
                    const auto Circle = [MarkWorld, Marks, FeetOf](TWeakObjectPtr<ULDHumanoidVisualComponent> At, double Radius, ELDCircleState State)
                    {
                        return [MarkWorld, Marks, FeetOf, At, Radius, State]()
                        {
                            ALDCaptureMarker* Mark = MarkWorld->SpawnActor<ALDCaptureMarker>();
                            FString Why;
                            if (!Mark || !Mark->Build(FeetOf(At), Radius, 1, Why)) { return; }
                            Mark->ShowState(State);
                            Marks->Add(Mark);
                        };
                    };
                    const auto Arena = [MarkWorld, Marks, FeetOf, A, V2]()
                    {
                        ALDPKRingMarker* Mark = MarkWorld->SpawnActor<ALDPKRingMarker>();
                        FString Why;
                        if (!Mark || !Mark->Build(FeetOf(A), 1, 1, Why)) { return; }
                        Mark->Show(560., true, 400., {FeetOf(V2) + FVector(0, 160, 0)}, 150.);
                        Marks->Add(Mark);
                    };
                    ChainJobs.Add({Look, TEXT("world-circles"), {{0.f, OwnMark}, {0.f, Circle(V1, 330., ELDCircleState::Solo)}, {0.f, Circle(A3, 300., ELDCircleState::Contested)}},
                        nullptr, ClearMarks, false, 1.f, .6f, 24});
                    ChainJobs.Add({Look, TEXT("world-arena"), {{0.f, Arena}}, nullptr, ClearMarks, false, 1.f, .6f, 24});
                    // A recovery orb by V1, and the flagged hits at 0.3 s: a critical on V2, a broken shield on V3, an absorbed
                    // one on A2 and a parry on A3.
                    const auto Orb = [MarkWorld, Marks, FeetOf, V1]()
                    {
                        ALDRecoveryOrbMarker* Mark = MarkWorld->SpawnActor<ALDRecoveryOrbMarker>();
                        FString Why;
                        if (!Mark || !Mark->Build(FeetOf(V1) + FVector(0, 170, 0), 90., 1, 1, Why)) { return; }
                        Marks->Add(Mark);
                    };
                    const auto Flagged = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, uint8 Flags)
                    { return [Body, Flags]() { if (auto* B = Body.Get()) { B->SetHitReaction(0.f, ELDObservedHitLook::None, Flags, FGuid::NewGuid()); } }; };
                    ChainJobs.Add({Look, TEXT("world-orb-flags"), {{0.f, Orb}, {.3f, Flagged(V2, LDObservation::Critical)}, {.3f, Flagged(V3, LDObservation::ShieldBroken)},
                        {.3f, Flagged(A2, LDObservation::ShieldAbsorbed)}, {.3f, Flagged(A3, LDObservation::Parried)}},
                        nullptr, ClearMarks, false, .4f, .25f, 20});
                }
                else if (Look == TEXT("flights"))
                {
                    // Visible flights and placed skill areas on their real proxies. Three flights leave three bodies and fly
                    // straight at 900 cm/s for a second (chalk, the takeout, a fist wind); then six areas stand on the stage
                    // (plain, magic, fire, water, electric and the fan's wind).
                    UWorld* const FlightWorld = &Target;
                    const TSharedRef<TArray<TWeakObjectPtr<ALDObservedProjectileProxy>>> Proxies = MakeShared<TArray<TWeakObjectPtr<ALDObservedProjectileProxy>>>();
                    const TSharedRef<TArray<FLDObservedProjectileView>> Views = MakeShared<TArray<FLDObservedProjectileView>>();
                    const auto ClearProxies = [Proxies, Views]()
                    {
                        for (const TWeakObjectPtr<ALDObservedProjectileProxy>& Proxy : *Proxies) { if (ALDObservedProjectileProxy* Old = Proxy.Get()) { Old->Destroy(); } }
                        Proxies->Reset();
                        Views->Reset();
                    };
                    const auto Place = [FlightWorld, Proxies, Views](TWeakObjectPtr<ULDHumanoidVisualComponent> At, ELDObservedProjectileLook ProxyLook, float Radius, float YawDegrees, double Ahead)
                    {
                        return [FlightWorld, Proxies, Views, At, ProxyLook, Radius, YawDegrees, Ahead]()
                        {
                            const ULDHumanoidVisualComponent* Body = At.Get();
                            if (!Body) { return; }
                            FActorSpawnParameters Spawn;
                            Spawn.ObjectFlags |= RF_Transient;
                            ALDObservedProjectileProxy* Proxy = FlightWorld->SpawnActor<ALDObservedProjectileProxy>(Spawn);
                            if (!Proxy) { return; }
                            FLDObservedProjectileView View;
                            View.Handle = FGuid::NewGuid();
                            View.Look = ProxyLook;
                            View.RadiusCm = Radius;
                            View.Heading = uint16(YawDegrees / 360.f * 65536.f);
                            View.PositionCm = Body->GetOwner()->GetActorLocation() - FVector(0, 0, 90) + FRotator(0, YawDegrees, 0).Vector() * Ahead;
                            Proxy->Apply(View, 300.);
                            Proxy->Present(300.);
                            Proxies->Add(Proxy);
                            Views->Add(View);
                        };
                    };
                    const auto Fly = [Proxies, Views](float Seconds)
                    {
                        return [Proxies, Views, Seconds]()
                        {
                            for (int32 Index = 0; Index < Proxies->Num(); ++Index)
                            {
                                ALDObservedProjectileProxy* Proxy = (*Proxies)[Index].Get();
                                if (!Proxy) { continue; }
                                FLDObservedProjectileView Next = (*Views)[Index];
                                Next.PositionCm = FVector(Next.PositionCm) + FRotator(0, float(Next.Heading) / 65536.f * 360.f, 0).Vector() * 900. * Seconds;
                                Proxy->Apply(Next, 300. + Seconds);
                                Proxy->Present(300. + Seconds);
                            }
                        };
                    };
                    TArray<FSeqStep> Flying{{0.f, Place(A, ELDObservedProjectileLook::Chalk, 20.f, 20.f, 60.)}, {0.f, Place(A2, ELDObservedProjectileLook::Sock, 30.f, -20.f, 60.)},
                        {0.f, Place(A3, ELDObservedProjectileLook::FistWind, 65.f, 60.f, 60.)}};
                    for (int32 Frame = 1; Frame <= 30; ++Frame) { Flying.Add({Frame / 30.f, Fly(Frame / 30.f)}); }
                    ChainJobs.Add({Look, TEXT("flights-three"), Flying, nullptr, ClearProxies, false, .4f, 0.f, 30});
                    ChainJobs.Add({Look, TEXT("flights-areas"), {{0.f, Place(V1, ELDObservedProjectileLook::AreaPlain, 170.f, 0.f, 0.)}, {0.f, Place(V2, ELDObservedProjectileLook::AreaMagic, 170.f, 0.f, 0.)},
                        {0.f, Place(V3, ELDObservedProjectileLook::AreaFire, 170.f, 0.f, 0.)}, {0.f, Place(A2, ELDObservedProjectileLook::AreaWater, 170.f, 0.f, 0.)},
                        {0.f, Place(A3, ELDObservedProjectileLook::AreaElectric, 170.f, 0.f, 0.)}, {0.f, Place(A, ELDObservedProjectileLook::Wind, 220.f, 30.f, 0.)}},
                        nullptr, ClearProxies, false, .9f, .5f, 30});
                    // Placed props and one-shots on their real proxies: two bags and the wall between them, a dropped slipper,
                    // a cordon tape, a shard burst, a brick falling (landing at 0.7 s)
                    // and an armed trap.
                    const auto Put = [FlightWorld, Proxies, Views](TWeakObjectPtr<ULDHumanoidVisualComponent> At, ELDObservedProjectileLook ProxyLook, float Radius, float TowardDegrees,
                        double Ahead, float FaceDegrees, float Length, uint32 WarningMs)
                    {
                        return [FlightWorld, Proxies, Views, At, ProxyLook, Radius, TowardDegrees, Ahead, FaceDegrees, Length, WarningMs]()
                        {
                            const ULDHumanoidVisualComponent* Body = At.Get();
                            if (!Body) { return; }
                            FActorSpawnParameters Spawn;
                            Spawn.ObjectFlags |= RF_Transient;
                            ALDObservedProjectileProxy* Proxy = FlightWorld->SpawnActor<ALDObservedProjectileProxy>(Spawn);
                            if (!Proxy) { return; }
                            FLDObservedProjectileView View;
                            View.Handle = FGuid::NewGuid();
                            View.Look = ProxyLook;
                            View.RadiusCm = Radius;
                            View.LengthCm = Length;
                            View.WarningRemainingMs = WarningMs;
                            View.AngleDegrees = 70.f;
                            View.Heading = uint16(FMath::Fmod(FaceDegrees + 360.f, 360.f) / 360.f * 65536.f);
                            View.PositionCm = Body->GetOwner()->GetActorLocation() - FVector(0, 0, 90) + FRotator(0, TowardDegrees, 0).Vector() * Ahead;
                            Proxy->Apply(View, 300.);
                            Proxy->Present(300.);
                            Proxies->Add(Proxy);
                            Views->Add(View);
                        };
                    };
                    const auto Hold = [Proxies](float Seconds)
                    {
                        return [Proxies, Seconds]() { for (const TWeakObjectPtr<ALDObservedProjectileProxy>& Proxy : *Proxies) { if (ALDObservedProjectileProxy* Shown = Proxy.Get()) { Shown->Present(300. + Seconds); } } };
                    };
                    const auto GoOff = [Proxies](int32 Index)
                    {
                        return [Proxies, Index]()
                        {
                            ALDObservedProjectileProxy* Shown = Proxies->IsValidIndex(Index) ? (*Proxies)[Index].Get() : nullptr;
                            if (!Shown) { return; }
                            Shown->End(0., true);
                            Shown->PlayBurst(true);
                        };
                    };
                    TArray<FSeqStep> Props{{0.f, Put(V1, ELDObservedProjectileLook::Bag, 50.f, 60.f, 220., 0.f, 0.f, 0)}, {0.f, Put(V1, ELDObservedProjectileLook::Bag, 50.f, -60.f, 220., 0.f, 0.f, 0)},
                        {0.f, Put(V1, ELDObservedProjectileLook::BagWall, 25.f, 0.f, 110., 90.f, 280.f, 0)}, {0.f, Put(V2, ELDObservedProjectileLook::SlipperDrop, 45.f, 180.f, 150., 0.f, 0.f, 0)},
                        {0.f, Put(V3, ELDObservedProjectileLook::CordonTape, 30.f, 180.f, 170., 90.f, 500.f, 0)},
                        {0.f, Put(A3, ELDObservedProjectileLook::ShardBurst, 250.f, 0.f, 0., 0.f, 0.f, 0)}, {0.f, Put(A, ELDObservedProjectileLook::BookShadow, 200.f, 180.f, 260., 0.f, 0.f, 700)},
                        {0.f, Put(A, ELDObservedProjectileLook::FlyerArmed, 180.f, 90.f, 260., 0.f, 0.f, 0)}};
                    for (int32 Frame = 1; Frame <= 40; ++Frame)
                    {
                        Props.Add({Frame / 30.f, Hold(Frame / 30.f)});
                        if (Frame == 21) { Props.Add({.7f, GoOff(6)}); }
                    }
                    ChainJobs.Add({Look, TEXT("flights-props"), Props, nullptr, ClearProxies, false, .5f, 0.f, 40});
                    // The three warnings a second before they land: a circle by V1, a cone from V3 and a line from A2.
                    TArray<FSeqStep> Warnings{{0.f, Put(V1, ELDObservedProjectileLook::AreaMagic, 220.f, 0.f, 0., 0.f, 0.f, 1000)},
                        {0.f, Put(V3, ELDObservedProjectileLook::ConeWarning, 320.f, 0.f, 0., 200.f, 0.f, 1000)},
                        {0.f, Put(A2, ELDObservedProjectileLook::LineWarning, 60.f, 0.f, 0., 20.f, 500.f, 1000)}};
                    for (int32 Frame = 1; Frame <= 30; ++Frame) { Warnings.Add({Frame / 30.f, Hold(Frame / 30.f)}); }
                    ChainJobs.Add({Look, TEXT("flights-warnings"), Warnings, nullptr, ClearProxies, false, .5f, 0.f, 30});
                }
                else if (Look == TEXT("reactions"))
                {
                    // The reactions with their own event, each on V1 in the own fight (right after A's strike lands on it) and
                    // on V3 as another player's: fog (a burning body gets wet), freeze (a wet body is chilled), overload (a
                    // burning body is shocked). The held element shows first, as it does in play.
                    struct FReactShot { const TCHAR* Name; ELDCombatReaction Kind; ELDDemoStatusId Held; ELDCombatElement Element; ELDObservedHitLook Hit; };
                    const auto HeldOf = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, ELDDemoStatusId Id)
                    {
                        return [Body, Id]()
                        {
                            if (auto* B = Body.Get())
                            {
                                TArray<FLDObservedStatus> Statuses;
                                if (Id != ELDDemoStatusId(0)) { Statuses.AddDefaulted_GetRef().StatusId = uint8(Id); }
                                B->SetElementStates(Statuses);
                            }
                        };
                    };
                    const auto ReactOf = [this](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, ELDCombatReaction Kind, ELDCombatElement Element, double Stamp)
                    { return [this, Body, Kind, Element, Stamp]() { if (auto* B = Body.Get()) { B->SetReaction(uint8(Kind), uint8(Element), 0.f, Stamp + ChainJob); } }; };
                    const auto DryOff = [HeldOf, V1, V3, Home]() { HeldOf(V1, ELDDemoStatusId(0))(); HeldOf(V3, ELDDemoStatusId(0))(); Home(); };
                    int32 CaseNumber = 0;
                    for (const FReactShot& Case : {
                        FReactShot{TEXT("fog"), ELDCombatReaction::Fog, ELDDemoStatusId::Burn, ELDCombatElement::Water, ELDObservedHitLook::Water},
                        FReactShot{TEXT("freeze"), ELDCombatReaction::Freeze, ELDDemoStatusId::Wet, ELDCombatElement::Chill, ELDObservedHitLook::Magic},
                        FReactShot{TEXT("overload"), ELDCombatReaction::Overload, ELDDemoStatusId::Burn, ELDCombatElement::Shock, ELDObservedHitLook::Electric},
                        FReactShot{TEXT("melt"), ELDCombatReaction::Melt, ELDDemoStatusId::Chilled, ELDCombatElement::Fire, ELDObservedHitLook::Fire},
                        FReactShot{TEXT("methane"), ELDCombatReaction::Methane, ELDDemoStatusId::Rotten, ELDCombatElement::Fire, ELDObservedHitLook::Fire},
                        FReactShot{TEXT("ferment"), ELDCombatReaction::Ferment, ELDDemoStatusId::Wet, ELDCombatElement::Rot, ELDObservedHitLook::Magic},
                        FReactShot{TEXT("shatter"), ELDCombatReaction::Shatter, ELDDemoStatusId::Chilled, ELDCombatElement::Shock, ELDObservedHitLook::Electric},
                        FReactShot{TEXT("corrode"), ELDCombatReaction::Corrode, ELDDemoStatusId::Rotten, ELDCombatElement::Shock, ELDObservedHitLook::Electric},
                        FReactShot{TEXT("frostbite"), ELDCombatReaction::Frostbite, ELDDemoStatusId::Rotten, ELDCombatElement::Chill, ELDObservedHitLook::Magic}})
                    {
                        ++CaseNumber;
                        ChainJobs.Add({Look, FString(TEXT("react-")) + Case.Name, {{0.f, HeldOf(V1, Case.Held)}, {.5f, HitOf(V1, Case.Hit)}, {.52f, HeldOf(V1, ELDDemoStatusId(0))},
                            {.52f, ReactOf(V1, Case.Kind, Case.Element, 2000. + 10. * CaseNumber)}}, Ahead(220.), DryOff, false, .62f, .4f, 54});
                        ChainJobs.Add({Look, FString(TEXT("react-")) + Case.Name + TEXT("-other"), {{0.f, HeldOf(V3, Case.Held)}, {.52f, HeldOf(V3, ELDDemoStatusId(0))},
                            {.52f, ReactOf(V3, Case.Kind, Case.Element, 2005. + 10. * CaseNumber)}}, nullptr, DryOff, false, .62f, .4f, 54});
                    }
                }
                else if (Look == TEXT("packs"))
                {
                    // A survey of the third-party CC-BY packs (Content/ThirdParty/CCBY): five of their systems at a time, one at
                    // each stage body's feet, as shipped, to judge their look and scale at the gameplay camera before a
                    // project effect is built on them. A missing system is logged and skipped.
                    UWorld* const PackWorld = &Target;
                    const TSharedRef<TArray<TWeakObjectPtr<UNiagaraComponent>>> Shown = MakeShared<TArray<TWeakObjectPtr<UNiagaraComponent>>>();
                    const auto Clear = [Shown]()
                    {
                        for (const TWeakObjectPtr<UNiagaraComponent>& Effect : *Shown) { if (UNiagaraComponent* Old = Effect.Get()) { Old->DestroyComponent(); } }
                        Shown->Reset();
                    };
                    const TArray<TWeakObjectPtr<ULDHumanoidVisualComponent>> Spots{A, V1, V2, V3, A2};
                    const auto Show = [PackWorld, Shown, Spots](TArray<FString> Paths, TArray<float> Scales = {})
                    {
                        return [PackWorld, Shown, Spots, Paths, Scales]()
                        {
                            for (int32 Index = 0; Index < Paths.Num() && Index < Spots.Num(); ++Index)
                            {
                                const ULDHumanoidVisualComponent* Spot = Spots[Index].Get();
                                const FString Path = FString(TEXT("/Game/ThirdParty/CCBY/")) + Paths[Index];
                                UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Path);
                                if (!Spot || !System) { UE_LOG(LogLDTest, Warning, TEXT("LD_PACK_SURVEY missing %s"), *Path); continue; }
                                TArray<FNiagaraVariable> Exposed;
                                System->GetExposedParameters().GetParameters(Exposed);
                                FString Names;
                                for (const FNiagaraVariable& Variable : Exposed) { Names += Variable.GetName().ToString() + TEXT(":") + Variable.GetType().GetName() + TEXT(" "); }
                                UE_LOG(LogLDTest, Display, TEXT("LD_PACK_SURVEY %s user=[%s]"), *Paths[Index], *Names);
                                Shown->Add(UNiagaraFunctionLibrary::SpawnSystemAtLocation(PackWorld, System, Spot->GetOwner()->GetActorLocation() + FVector(120., 0., -60.),
                                    Spot->GetOwner()->GetActorRotation(), FVector(Scales.IsValidIndex(Index) ? Scales[Index] : 1.f), false, true, ENCPoolMethod::None));
                            }
                        };
                    };
                    struct FPackSet { const TCHAR* Name; TArray<FString> Paths; };
                    const FString Mixed = TEXT("MixedVFX/Particles/");
                    const auto Of = [](const FString& Folder, const TCHAR* Name) { return Folder + Name + TEXT(".") + Name; };
                    for (const FPackSet& Set : {
                        FPackSet{TEXT("explosions"), {Of(Mixed + TEXT("Explosions/"), TEXT("NS_ExplosionGroundSmall")), Of(Mixed + TEXT("Explosions/"), TEXT("NS_ExplosionGroundMedium")),
                            Of(Mixed + TEXT("Explosions/"), TEXT("NS_ExplosionGroundBig")), Of(Mixed + TEXT("Explosions/"), TEXT("NS_ExplosionMidAirSmall")), Of(Mixed + TEXT("Explosions/"), TEXT("NS_ExplosionMidAirBig"))}},
                        FPackSet{TEXT("slash-hits"), {Of(Mixed + TEXT("Slashes/SeparateParts/Hits/"), TEXT("NS_FireSlash_Hit")), Of(Mixed + TEXT("Slashes/SeparateParts/Hits/"), TEXT("NS_HolySlash_Hit")),
                            Of(Mixed + TEXT("Slashes/SeparateParts/Hits/"), TEXT("NS_LightningSlash_Hit")), Of(Mixed + TEXT("Slashes/SeparateParts/Hits/"), TEXT("NS_GalaxySlash_Hit")), Of(Mixed + TEXT("Slashes/SeparateParts/Hits/"), TEXT("NS_VampireSlash_Hit"))}},
                        FPackSet{TEXT("slashes"), {Of(Mixed + TEXT("Slashes/SeparateParts/Slashes/"), TEXT("NS_FireSlash_OnlySlash")), Of(Mixed + TEXT("Slashes/SeparateParts/Slashes/"), TEXT("NS_HolySlash_OnlySlash")),
                            Of(Mixed + TEXT("Slashes/SeparateParts/Slashes/"), TEXT("NS_LightningSlash_OnlySlash")), Of(Mixed + TEXT("Slashes/SeparateParts/Slashes/"), TEXT("NS_GalaxySlash_OnlySlash")), Of(Mixed + TEXT("Slashes/"), TEXT("NS_VampireSlash"))}},
                        FPackSet{TEXT("fires"), {Of(Mixed + TEXT("Fires/"), TEXT("NS_TorchFire")), Of(Mixed + TEXT("Fires/"), TEXT("NS_Campfire")), Of(Mixed + TEXT("Fires/"), TEXT("NS_BigFire")),
                            Of(Mixed + TEXT("Fires/"), TEXT("NS_FireBarrier")), Of(Mixed + TEXT("Fires/"), TEXT("NS_FlameThrower"))}},
                        FPackSet{TEXT("mix"), {Of(Mixed + TEXT("Mix/"), TEXT("NS_ElectricField")), Of(Mixed + TEXT("Mix/"), TEXT("NS_Wind")), Of(Mixed + TEXT("Mix/"), TEXT("NS_Mix_01")),
                            Of(Mixed + TEXT("Mix/"), TEXT("NS_Mix_02")), Of(Mixed + TEXT("Mix/"), TEXT("NS_Mix_03"))}},
                        FPackSet{TEXT("projectile-hits"), {Of(Mixed + TEXT("Projectiles/Hits/"), TEXT("NS_LightningBolt_Hit")), Of(Mixed + TEXT("Projectiles/Hits/"), TEXT("NS_Projectile_01_Hit")),
                            Of(Mixed + TEXT("Projectiles/Hits/"), TEXT("NS_Projectile_03_Hit")), Of(Mixed + TEXT("Projectiles/Hits/"), TEXT("NS_Shuriken_01_Hit")), Of(Mixed + TEXT("Projectiles/Hits/"), TEXT("NS_FireArrow_Hit"))}},
                        FPackSet{TEXT("projectiles"), {Of(Mixed + TEXT("Projectiles/"), TEXT("NS_LightningBolt")), Of(Mixed + TEXT("Projectiles/"), TEXT("NS_Projectile_01")),
                            Of(Mixed + TEXT("Projectiles/"), TEXT("NS_Projectile_03")), Of(Mixed + TEXT("Projectiles/"), TEXT("NS_Shuriken_01")), Of(Mixed + TEXT("Projectiles/"), TEXT("NS_FireArrow"))}},
                        FPackSet{TEXT("smoke"), {Of(TEXT("PotaVFX_Smoke/VFX/System/ColorSmoke/"), TEXT("NS_SmokeGradient_Loop")), Of(TEXT("PotaVFX_Smoke/VFX/System/ColorSmoke/"), TEXT("NS_SmokeGradient_Loop_Masked")),
                            Of(TEXT("PotaVFX_Smoke/VFX/System/SmokeBurst/"), TEXT("NS_MagicSmokeBurstAir")), Of(TEXT("PotaVFX_Smoke/VFX/System/SmokeRing/"), TEXT("NS_SmokeRing")), Of(TEXT("PotaVFX_Smoke/VFX/System/SmokeRing/"), TEXT("NS_SmokeRing_Masked"))}},
                        FPackSet{TEXT("sparkles"), {Of(TEXT("FreeParticle_SoftTofu/Niagara/"), TEXT("NS_Sparkling")), Of(TEXT("FreeParticle_SoftTofu/Niagara/"), TEXT("NS_Sparkling_Glow")),
                            Of(TEXT("FreeParticle_SoftTofu/Niagara/"), TEXT("NS_Sparkling_Star")), Of(TEXT("FreeParticle_SoftTofu/Niagara/"), TEXT("NS_Sparkling_Noise")), Of(TEXT("FreeParticle_SoftTofu/Niagara/"), TEXT("NS_feather"))}}})
                    {
                        ChainJobs.Add({Look, FString(TEXT("packs-")) + Set.Name, {{0.f, Show(Set.Paths)}}, nullptr, Clear, false, .4f, 0.f, 45});
                    }
                    // Whether the component's scale carries through: the smoke ring at 1, 0.5 and 0.3, an explosion and a
                    // lightning hit at 0.35.
                    ChainJobs.Add({Look, TEXT("packs-scaled"), {{0.f, Show({Of(TEXT("PotaVFX_Smoke/VFX/System/SmokeRing/"), TEXT("NS_SmokeRing")),
                        Of(TEXT("PotaVFX_Smoke/VFX/System/SmokeRing/"), TEXT("NS_SmokeRing")), Of(TEXT("PotaVFX_Smoke/VFX/System/SmokeRing/"), TEXT("NS_SmokeRing")),
                        Of(Mixed + TEXT("Explosions/"), TEXT("NS_ExplosionGroundSmall")), Of(Mixed + TEXT("Projectiles/Hits/"), TEXT("NS_LightningBolt_Hit"))},
                        {1.f, .5f, .3f, .35f, .35f})}}, nullptr, Clear, false, .4f, 0.f, 45});
                }
                else if (Look == TEXT("self"))
                {
                    // The self and stance skills: each cast by A (its hit, where it has one, on V1 in front) and as another
                    // player's by A3 (on V3), A3 also wearing the state its cast leaves (a shield from the cue, braced, the
                    // empowered basics; A's own statuses come from its character). Then the brace running out on A3 and its
                    // shove. The approved level-1 windups.
                    const auto Wear = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, ELDDemoStatusId Status)
                    {
                        return [Body, Status]()
                        {
                            if (auto* B = Body.Get())
                            {
                                TArray<FLDObservedStatus> Statuses;
                                if (Status != ELDDemoStatusId(0)) { Statuses.AddDefaulted_GetRef().StatusId = uint8(Status); }
                                B->SetElementStates(Statuses);
                            }
                        };
                    };
                    const auto Undress = [Home, A3]() { Home(); if (auto* B = A3.Get()) { B->SetCue(0); B->SetElementStates({}); } };
                    struct FSelfShot { const TCHAR* Name; ELDDemoAbilityId Ability; float Windup; uint8 CueBits; ELDDemoStatusId Status; ELDObservedHitLook Hit; float HitAt; double Gap; float ShowAt; int32 Frames; };
                    const ELDDemoStatusId NoStatus = ELDDemoStatusId(0);
                    for (const FSelfShot& Skill : {
                        FSelfShot{TEXT("unplug"), ELDDemoAbilityId::ActiveUnplug, 0.f, 0, NoStatus, ELDObservedHitLook::None, .2f, 150., .15f, 24},
                        FSelfShot{TEXT("notmad"), ELDDemoAbilityId::ActiveNotMad, 0.f, ULDHumanoidVisualComponent::Shielded, NoStatus, ELDObservedHitLook::None, -1.f, 250., .1f, 36},
                        FSelfShot{TEXT("staredown"), ELDDemoAbilityId::ActiveStareDown, .2f, 0, NoStatus, ELDObservedHitLook::None, -1.f, 250., .45f, 45},
                        FSelfShot{TEXT("brace"), ELDDemoAbilityId::ActiveBrace, .1f, 0, ELDDemoStatusId::Braced, ELDObservedHitLook::None, -1.f, 250., .25f, 36},
                        FSelfShot{TEXT("triple"), ELDDemoAbilityId::ActiveTripleCombo, 0.f, 0, ELDDemoStatusId::Empowered, ELDObservedHitLook::None, -1.f, 250., .1f, 36},
                        FSelfShot{TEXT("openmic"), ELDDemoAbilityId::ActiveOpenMic, .15f, ULDHumanoidVisualComponent::Shielded, NoStatus, ELDObservedHitLook::Magic, .2f, 250., .3f, 36},
                        FSelfShot{TEXT("flurry"), ELDDemoAbilityId::ActiveFlurryRush, 0.f, 0, NoStatus, ELDObservedHitLook::None, -1.f, 250., .6f, 45}})
                    {
                        // The cast with its cue's status bits (a shield) from its start.
                        const auto OtherCast = [Body = A3, Skill]() { if (auto* B = Body.Get()) { B->SetComboStep(0); B->SetCastAbility(uint8(Skill.Ability)); B->SetCue(2 | Skill.CueBits, 0.f, Skill.Windup); } };
                        TArray<FSeqStep> OwnSteps{{0.f, CastOf(A, Skill.Ability, Skill.Windup)}}, OtherSteps{{0.f, OtherCast}};
                        if (Skill.Status != NoStatus) { OtherSteps.Add({Skill.Windup + .05f, Wear(A3, Skill.Status)}); }
                        if (Skill.HitAt > 0.f) { OwnSteps.Add({Skill.HitAt, HitOf(V1, Skill.Hit)}); OtherSteps.Add({Skill.HitAt, HitOf(V3, Skill.Hit)}); }
                        ChainJobs.Add({Look, FString(TEXT("self-")) + Skill.Name, OwnSteps, Ahead(Skill.Gap), Home, false, Skill.ShowAt, 0.f, Skill.Frames});
                        ChainJobs.Add({Look, FString(TEXT("self-")) + Skill.Name + TEXT("-other"), OtherSteps, nullptr, Undress, false, Skill.ShowAt + .3f, 0.f, Skill.Frames});
                    }
                    ChainJobs.Add({Look, TEXT("self-brace-end"), {{0.f, CastOf(A3, ELDDemoAbilityId::ActiveBrace, .1f)}, {.15f, Wear(A3, ELDDemoStatusId::Braced)},
                        {3.1f, Wear(A3, NoStatus)}}, nullptr, Undress, false, 3.18f, 2.95f, 24});
                }
                else if (Look == TEXT("poses"))
                {
                    // The retargeted Quaternius clips (Scripts/ImportSkillAnimations.py) held on the own body A at 40 % of
                    // their length, one still each, to judge the retarget before any clip is used in play.
                    static const TCHAR* const Clips[] = {TEXT("QN_Push_Loop"), TEXT("QN_Punch_Jab"), TEXT("QN_Punch_Cross"), TEXT("QN_Spell_Simple_Shoot"),
                        TEXT("QN_Dance_Loop"), TEXT("QN_Sword_Idle"), TEXT("QN_Idle_Shield_Loop"), TEXT("QN_Sword_Block"), TEXT("QN_Shield_OneShot"),
                        TEXT("QN_Shield_Dash"), TEXT("QN_Melee_Hook"), TEXT("QN_OverhandThrow"), TEXT("QN_Slide_Start"), TEXT("QN_Idle_FoldArms_Loop"),
                        TEXT("QN_Yes"), TEXT("QN_Idle_TalkingPhone_Loop"), TEXT("MX_Standing_Taunt_Chest_Thump"), TEXT("MX_Yelling_Out"), TEXT("MX_Hands_On_Hips"),
                        TEXT("MX_Angry_Point"), TEXT("MX_Stomp")};
                    // A DEV three-quarter front view of A (about 3 m out, just above chest height) for the poses, restored after.
                    const TSharedRef<FTransform> Saved = MakeShared<FTransform>();
                    const auto Front = [this, A, Saved]()
                    {
                        ACameraActor* View = Camera.Get();
                        auto* B = A.Get();
                        if (!View || !B) { return; }
                        *Saved = View->GetActorTransform();
                        const AActor* Body = B->GetOwner();
                        const FVector Chest = Body->GetActorLocation() + FVector(0, 0, 30);
                        const FVector Eye = Chest + Body->GetActorForwardVector() * 280. + Body->GetActorRightVector() * 110. + FVector(0, 0, 40);
                        View->SetActorLocationAndRotation(Eye, (Chest - Eye).Rotation());
                    };
                    const auto Back = [this, A, Saved]()
                    {
                        if (auto* B = A.Get()) { B->HoldPose(nullptr, 0.f); }
                        if (ACameraActor* View = Camera.Get()) { View->SetActorTransform(*Saved); }
                    };
                    const auto Hold = [this, A, Front](UAnimSequence* Clip, float At)
                    { return [A, Front, Clip, At]() { Front(); if (auto* B = A.Get()) { B->HoldPose(Clip, At); } }; };
                    if (UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Lockdown/Art/Characters/hooper/Hooper_Idle.Hooper_Idle")))
                    { ChainJobs.Add({Look, TEXT("poses-0-BasketballIdle"), {{0.f, Hold(Idle, 0.f)}}, nullptr, Back, false, .1f, 0.f, 3}); }
                    for (const TCHAR* Name : Clips)
                    {
                        const FString Folder = FString(Name).StartsWith(TEXT("MX_")) ? TEXT("/Game/ThirdParty/Fab/Mixamo/Hooper") : TEXT("/Game/Lockdown/Art/Characters/hooper/Quaternius/Hooper");
                        UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Folder, Name, Name));
                        if (!Check(Clip != nullptr, FString::Printf(TEXT("Retargeted clip %s loads"), Name))) { continue; }
                        ChainJobs.Add({Look, FString(TEXT("poses-")) + FString(Name).RightChop(3), {{0.f, Hold(Clip, Clip->GetPlayLength() * .4f)}},
                            nullptr, Back, false, .1f, 0.f, 3});
                    }
                }
                else if (Look == TEXT("casts"))
                {
                    // One skill of each cast family without a windup of its own, from its windup through its hit on V1 2.5 m
                    // ahead: a throw (active_snark_bolt, 0.18 s), a two-hand line (active_pants_down, 0.14 s), a ground slam
                    // (active_calculus_drop, 0.25 s, magic) and a self-centred one (active_stare_down, 0.2 s).
                    struct FFamily { const TCHAR* Name; ELDDemoAbilityId Ability; float Windup; ELDObservedHitLook Hit; float HitAt; };
                    for (const FFamily& Family : {FFamily{TEXT("casts-throw"), ELDDemoAbilityId::ActiveSnarkBolt, .18f, ELDObservedHitLook::None, .33f},
                        FFamily{TEXT("casts-twohand"), ELDDemoAbilityId::ActivePantsDown, .14f, ELDObservedHitLook::None, .17f},
                        FFamily{TEXT("casts-slam"), ELDDemoAbilityId::ActiveCalculusDrop, .25f, ELDObservedHitLook::Magic, .82f},
                        FFamily{TEXT("casts-self"), ELDDemoAbilityId::ActiveStareDown, .2f, ELDObservedHitLook::None, -1.f}})
                    {
                        TArray<FSeqStep> Steps{{0.f, CastOf(A, Family.Ability, Family.Windup)}};
                        if (Family.HitAt > 0.f) { Steps.Add({Family.HitAt, HitOf(V1, Family.Hit)}); }
                        ChainJobs.Add({Look, Family.Name, Steps, Ahead(250.), Home, false, Family.Windup * .7f, 0.f, 36});
                    }
                }
                else if (Look == TEXT("kill"))
                {
                    // A visible fall (the knockout): a fallen body stays fallen, so each scenario spawns its own body 1.5 m
                    // beside V1 (or V3), drops it and removes it afterwards; one in the own fight, one in another's.
                    UWorld* const KillWorld = &Target;
                    const auto FallNear = [this, KillWorld](TWeakObjectPtr<ULDHumanoidVisualComponent> Near, uint8 Slot, bool bOwn)
                    {
                        const TSharedRef<TWeakObjectPtr<ULDHumanoidVisualComponent>> Body = MakeShared<TWeakObjectPtr<ULDHumanoidVisualComponent>>();
                        TFunction<void()> Setup = [this, KillWorld, Near, Slot, bOwn, Body]()
                        {
                            if (!Near.IsValid()) { return; }
                            const FVector Foot = Near->GetOwner()->GetActorLocation() + FVector(150., 0., -90.);
                            *Body = SpawnVfxBody(*KillWorld, Foot, 180.f, Slot);
                            if (auto* B = Body->Get()) { B->SetOwnBody(bOwn); }
                        };
                        TFunction<void()> Drop = [Body]() { if (auto* B = Body->Get()) { B->PlayFall(); } };
                        TFunction<void()> Remove = [Body]() { if (auto* B = Body->Get()) { B->GetOwner()->Destroy(); } };
                        return MakeTuple(Setup, Drop, Remove);
                    };
                    const auto OwnFall = FallNear(V1, 5, true), OtherFall = FallNear(V3, 6, false);
                    ChainJobs.Add({Look, TEXT("kill-own"), {{.05f, OwnFall.Get<1>()}}, OwnFall.Get<0>(), OwnFall.Get<2>(), false, .3f, 0.f, 60});
                    ChainJobs.Add({Look, TEXT("kill-other"), {{.05f, OtherFall.Get<1>()}}, OtherFall.Get<0>(), OtherFall.Get<2>(), false, .3f, 0.f, 40});
                }
                else if (Look == TEXT("grounds"))
                {
                    // The four other ground states at their usual 220 cm radius under four bodies, own and others' seats,
                    // judged in the middle of a 2.4 s life (so the fade-out is captured at the end): a puddle under V1 and ice
                    // under V2 (own), a fire floor under V3 and miasma under A2 (another player's).
                    const auto GroundOf = [this](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, ELDGroundStateKind Kind, uint32 Id, uint8 Seat)
                    {
                        return [this, Body, Kind, Id, Seat]()
                        {
                            FActorSpawnParameters Spawn;
                            Spawn.ObjectFlags |= RF_Transient;
                            ALDObservedGroundProxy* Ground = World->SpawnActor<ALDObservedGroundProxy>(Spawn);
                            if (!Check(Ground != nullptr && Body.IsValid(), TEXT("Proof ground state proxy spawned"))) { return; }
                            FLDObservedGroundState State;
                            State.Id = Id;
                            State.Kind = uint8(Kind);
                            State.CentreCm = Body->GetOwner()->GetActorLocation() - FVector(0, 0, 90);
                            State.RadiusCm = 220.f;
                            State.OwnerSeat = Seat;
                            State.RemainingMs = State.TotalMs = 2400;
                            Ground->Apply(State, 100.);
                            ProofGrounds.Add(Ground);
                            ProofGroundsAt.Add(ChainClock);
                        };
                    };
                    ChainJobs.Add({Look, TEXT("grounds-four"), {{.05f, GroundOf(V1, ELDGroundStateKind::Puddle, 31, OwnSeat)}, {.05f, GroundOf(V2, ELDGroundStateKind::Ice, 32, OwnSeat)},
                        {.05f, GroundOf(V3, ELDGroundStateKind::Fire, 33, OtherSeat)}, {.05f, GroundOf(A2, ELDGroundStateKind::Miasma, 34, OtherSeat)}},
                        nullptr, ClearGrounds, false, 1.2f, 0.f, 75});
                    ChainJobs.Add({Look, TEXT("grounds-sunlit"), {{.05f, GroundOf(V1, ELDGroundStateKind::Fire, 35, OwnSeat)}, {.05f, GroundOf(A, ELDGroundStateKind::Miasma, 36, OwnSeat)}},
                        Sunlit, [Home, ClearGrounds]() { ClearGrounds(); Home(); }, false, 1.2f, .6f, 30});
                }
                else if (Look == TEXT("states"))
                {
                    // The element states on the stage bodies no character drives (A and V1 are characters whose own statuses
                    // overwrite a probe's): V2 burn, V3 wet, A2 electrified, A3 chilled at one stack; then V2 rotten and V3 at
                    // three stacks of burn; cleared at the end.
                    const auto State = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, ELDDemoStatusId Id, uint8 Stacks)
                    {
                        return [Body, Id, Stacks]()
                        {
                            if (auto* B = Body.Get())
                            {
                                TArray<FLDObservedStatus> Statuses;
                                if (Stacks > 0) { FLDObservedStatus& S = Statuses.AddDefaulted_GetRef(); S.StatusId = uint8(Id); S.Stacks = Stacks; }
                                B->SetElementStates(Statuses);
                            }
                        };
                    };
                    const auto Clear = [State, V1, V2, V3, A2, A3]()
                    { for (const auto& B : {V1, V2, V3, A2, A3}) { State(B, ELDDemoStatusId::Burn, 0)(); } };
                    ChainJobs.Add({Look, TEXT("states-four"), {{0.f, State(V2, ELDDemoStatusId::Burn, 1)}, {0.f, State(V3, ELDDemoStatusId::Wet, 1)},
                        {0.f, State(A2, ELDDemoStatusId::Electrified, 1)}, {0.f, State(A3, ELDDemoStatusId::Chilled, 1)}},
                        nullptr, Clear, false, 1.f, .6f, 45});
                    ChainJobs.Add({Look, TEXT("states-rot-burn3"), {{0.f, State(V2, ELDDemoStatusId::Rotten, 1)}, {0.f, State(V3, ELDDemoStatusId::Burn, 3)}},
                        nullptr, Clear, false, 1.f, .6f, 30});
                    // The control states, one each: root, respawn protection, slow and stun; attack slow, disarm, silence and
                    // taunt; then exposed, root with silence on one body, and a camp member's walk back on A2.
                    const auto Both = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, ELDDemoStatusId First, ELDDemoStatusId Second)
                    {
                        return [Body, First, Second]()
                        {
                            if (auto* B = Body.Get())
                            {
                                TArray<FLDObservedStatus> Statuses;
                                Statuses.AddDefaulted_GetRef().StatusId = uint8(First);
                                Statuses.AddDefaulted_GetRef().StatusId = uint8(Second);
                                B->SetElementStates(Statuses);
                            }
                        };
                    };
                    ChainJobs.Add({Look, TEXT("states-control-a"), {{0.f, State(V2, ELDDemoStatusId::Root, 1)}, {0.f, State(V3, ELDDemoStatusId::RespawnProtection, 1)},
                        {0.f, State(A2, ELDDemoStatusId::Slow, 1)}, {0.f, State(A3, ELDDemoStatusId::Stun, 1)}},
                        nullptr, Clear, false, 1.f, .6f, 30});
                    ChainJobs.Add({Look, TEXT("states-control-b"), {{0.f, State(V2, ELDDemoStatusId::AttackSlowed, 1)}, {0.f, State(V3, ELDDemoStatusId::Disarm, 1)},
                        {0.f, State(A2, ELDDemoStatusId::Silence, 1)}, {0.f, State(A3, ELDDemoStatusId::Taunt, 1)}},
                        nullptr, Clear, false, 1.f, .6f, 30});
                    // A level gained on V2 and V3.
                    const auto Level = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body, uint8 To) { return [Body, To]() { if (auto* B = Body.Get()) { B->SetLevel(To); } }; };
                    ChainJobs.Add({Look, TEXT("states-level"), {{0.f, Level(V2, 1)}, {0.f, Level(V3, 1)}, {.05f, Level(V2, 2)}, {.35f, Level(V3, 2)}},
                        nullptr, Clear, false, .5f, .1f, 36});
                    // A basic swing from a body in the dean's look (A3), and an empowered basic at combo step 1 (V2), which swings
                    // as the finisher does; the dean is enraged and V3 stands on guard as a camp member does.
                    const auto SwingOf = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Body)
                    { return [Body]() { if (auto* B = Body.Get()) { B->SetComboStep(1); B->SetCastAbility(0); B->SetCue(1, 0.f, .18f); } }; };
                    ChainJobs.Add({Look, TEXT("states-dean-empowered-swing"), {{0.f, State(V2, ELDDemoStatusId::Empowered, 1)}, {0.f, State(A3, ELDDemoStatusId::CampEnraged, 1)},
                        {0.f, State(V3, ELDDemoStatusId::CampGuarding, 1)}, {.1f, SwingOf(A3)}, {.1f, SwingOf(V2)}},
                        [A3]() { if (auto* B = A3.Get()) { B->ApplyMonsterLook(uint8(ELDDemoMonsterId::MonsterDean)); } },
                        [Clear, A3]() { Clear(); if (auto* B = A3.Get()) { B->ApplyPalette(5); } }, false, .3f, .1f, 16});
                    ChainJobs.Add({Look, TEXT("states-control-c"), {{0.f, State(V2, ELDDemoStatusId::Exposed, 1)}, {0.f, Both(V3, ELDDemoStatusId::Root, ELDDemoStatusId::Silence)},
                        {0.f, [A2]() { if (auto* B = A2.Get()) { B->SetCue(ULDHumanoidVisualComponent::Returning); } }}},
                        nullptr, Clear, false, 1.f, .6f, 30});
                }
                else if (Look == TEXT("chill"))
                {
                    // active_cola_shake (windup 0.15 s, cone 400 cm / 60 degrees): its windup, the blast hitting V1 2.5 m ahead,
                    // the same on the sunlit floor, and another player's.
                    const float Cola = .15f;
                    const auto Blast = [&](TWeakObjectPtr<ULDHumanoidVisualComponent> Caster, TWeakObjectPtr<ULDHumanoidVisualComponent> Victim)
                    { return TArray<FSeqStep>{{0.f, CastOf(Caster, ELDDemoAbilityId::ActiveColaShake, Cola)}, {Cola + .05f, HitOf(Victim, ELDObservedHitLook::Magic)}}; };
                    ChainJobs.Add({Look, TEXT("chill-tell"), Blast(A, V1), Ahead(250.), Home, false, Cola * .7f, 0.f, 8});
                    ChainJobs.Add({Look, TEXT("chill-blast"), Blast(A, V1), Ahead(250.), Home, false, Cola + .12f, 0.f, 50});
                    ChainJobs.Add({Look, TEXT("chill-sunlit"), Blast(A, V1), Sunlit, Home, false, Cola + .12f, .1f, 30});
                    ChainJobs.Add({Look, TEXT("chill-other"), Blast(A3, V3), nullptr, nullptr, false, Cola + .12f, .1f, 30});
                }
                else if (Look == TEXT("feel"))
                {
                    // The own body A runs a full basic combo on V1 at the base cadence (1/0.85 s apart), each blow landing at
                    // its release with the owner's confirmation, then casts the approved chain: V1, then two bounces 150 ms
                    // apart (active_zap data). Every sound comes from the same presentation hooks as in a match.
                    const float Interval = 1.f / .85f;
                    const auto Confirmed = [](TWeakObjectPtr<ULDHumanoidVisualComponent> Attacker, uint8 Flags)
                    {
                        return [Attacker, Flags]()
                        {
                            if (auto* B = Attacker.Get())
                            {
                                const LDHitFeel::FTuning Feel = LDHitFeel::Read();
                                const LDHitFeel::FConfirm Confirm = LDHitFeel::ConfirmOf(Feel, Flags);
                                B->SetConfirm(0.f, Confirm.StopSeconds, Confirm.SparkScale);
                                LDAudioFeel::Confirm(*B, Flags, Feel.ConfirmVolume);
                            }
                        };
                    };
                    FChainJob Feel{Look, TEXT("feel-combo-zap"), {}, [Close, A, V1]() { Close(); if (auto* B = A.Get()) { B->SetOwnBody(true); } if (auto* B = V1.Get()) { B->SetOwnBody(false); } },
                        [Home, A]() { Home(); if (auto* B = A.Get()) { B->SetOwnBody(false); } }};
                    for (uint8 Step = 1; Step <= 3; ++Step)
                    {
                        const float At = .5f + (Step - 1) * Interval;
                        Feel.Steps.Add({At, Swing(A, Step, W)});
                        Feel.Steps.Add({At + W + .02f, Plain(V1)});
                        Feel.Steps.Add({At + W + .02f, Confirmed(A, 0)});
                    }
                    const float ZapAt = .5f + 3.f * Interval + .3f;
                    Feel.Steps.Add({ZapAt, Cast(A)});
                    Feel.Steps.Add({ZapAt + .27f, Hit(V1)});
                    Feel.Steps.Add({ZapAt + .27f, Confirmed(A, 0)});
                    Feel.Steps.Add({ZapAt + .42f, Hit(V2)});
                    Feel.Steps.Add({ZapAt + .57f, Hit(V3)});
                    Feel.bRealtime = true;
                    Feel.Seconds = ZapAt + 1.6f;
                    ChainJobs.Add(Feel);
                    // The listening reel (-LDDemoVfxProof=reel): every skill with its own hit cast by A at its real windup and
                    // landing on V1 at its release with the look the server gives it (LDCombatCues::ClassifyHit: physical None,
                    // magical Magic, burning Fire, water Water), then every element reaction and element state on V1, each step labelled
                    // in the log (LD_FEEL_LABEL) for the evidence video.
                    if (Wanted.Contains(TEXT("reel")))
                    {
                        ChainJobs.Pop();
                        FChainJob Reel{Look, TEXT("feel-reel"), {}, Feel.Setup, Feel.Teardown};
                        const auto Label = [](const FString& Text) { return [Text]() { UE_LOG(LogLDTest, Display, TEXT("LD_FEEL_LABEL %s"), *Text); }; };
                        struct FReelSkill { ELDDemoAbilityId Ability; float Windup; ELDObservedHitLook Look; const TCHAR* Name; };
                        const FReelSkill Skills[] = {
                            {ELDDemoAbilityId::ActivePantsDown, .14f, ELDObservedHitLook::None, TEXT("扒裤子")},
                            {ELDDemoAbilityId::ActiveCrotchGrab, .12f, ELDObservedHitLook::None, TEXT("掏蛋子")},
                            {ELDDemoAbilityId::ActiveSnarkBolt, .18f, ELDObservedHitLook::None, TEXT("甩你一鞋")},
                            {ELDDemoAbilityId::ActiveSeethe, .25f, ELDObservedHitLook::Fire, TEXT("烫你屁股")},
                            {ELDDemoAbilityId::ActiveElbow, .15f, ELDObservedHitLook::None, TEXT("滚远点")},
                            {ELDDemoAbilityId::ActiveQuickMeeting, .25f, ELDObservedHitLook::Fire, TEXT("脚底开锅")},
                            {ELDDemoAbilityId::ActiveUnplug, .1f, ELDObservedHitLook::None, TEXT("拧你手腕")},
                            {ELDDemoAbilityId::ActiveOpenMic, .15f, ELDObservedHitLook::Magic, TEXT("扯嗓子喷你")},
                            {ELDDemoAbilityId::ActiveCalculusDrop, .25f, ELDObservedHitLook::Magic, TEXT("板砖糊脸")},
                            {ELDDemoAbilityId::ActiveFlyerTrap, .15f, ELDObservedHitLook::Magic, TEXT("踩一脚试试")},
                            {ELDDemoAbilityId::ActiveHoseDown, .2f, ELDObservedHitLook::Water, TEXT("滋你一脸")},
                            {ELDDemoAbilityId::ActiveChargedRam, .1f, ELDObservedHitLook::None, TEXT("蛮牛顶")},
                            {ELDDemoAbilityId::ActiveColaShake, .15f, ELDObservedHitLook::Magic, TEXT("冰可乐摇一摇")},
                            {ELDDemoAbilityId::ActiveDeskFan, .2f, ELDObservedHitLook::None, TEXT("电风扇")},
                            {ELDDemoAbilityId::ActiveStaleTakeout, .2f, ELDObservedHitLook::Magic, TEXT("过期外卖")},
                            {ELDDemoAbilityId::ActiveSchoolBag, .2f, ELDObservedHitLook::Magic, TEXT("占座书包")},
                            {ELDDemoAbilityId::ActiveFlurryRush, .1f, ELDObservedHitLook::None, TEXT("疯狗乱拳")},
                            {ELDDemoAbilityId::ActiveCordonTape, .16f, ELDObservedHitLook::Magic, TEXT("警戒封条")}};
                        float At = .5f;
                        for (const FReelSkill& Skill : Skills)
                        {
                            const ELDDemoAbilityId Ability = Skill.Ability;
                            const float Windup = Skill.Windup;
                            const ELDObservedHitLook HitLook = Skill.Look;
                            Reel.Steps.Add({At, Label(FString::Printf(TEXT("技能 %s：出手 + 命中"), Skill.Name))});
                            Reel.Steps.Add({At, [A, Ability, Windup]() { if (auto* B = A.Get()) { B->SetComboStep(0); B->SetCastAbility(uint8(Ability)); B->SetCue(2, 0.f, Windup); } }});
                            Reel.Steps.Add({At + Windup + .05f, [V1, HitLook]() { if (auto* B = V1.Get()) { B->SetHitReaction(0.f, HitLook, 0, FGuid::NewGuid()); } }});
                            At += 1.3f;
                        }
                        struct FReelReaction { ELDCombatReaction Reaction; ELDCombatElement Element; const TCHAR* Name; };
                        const FReelReaction Reactions[] = {
                            {ELDCombatReaction::Fog, ELDCombatElement::Fire, TEXT("反应 雾（湿+火）")}, {ELDCombatReaction::Conduct, ELDCombatElement::Shock, TEXT("反应 导电（湿+电）")},
                            {ELDCombatReaction::Overload, ELDCombatElement::Shock, TEXT("反应 超载（火+电）")}, {ELDCombatReaction::Melt, ELDCombatElement::Fire, TEXT("反应 融化（火+冰）")},
                            {ELDCombatReaction::Methane, ELDCombatElement::Fire, TEXT("反应 沼气（火+腐）")}, {ELDCombatReaction::Freeze, ELDCombatElement::Chill, TEXT("反应 冻结（湿+冰）")},
                            {ELDCombatReaction::Ferment, ELDCombatElement::Rot, TEXT("反应 发酵（湿+腐）")}, {ELDCombatReaction::Shatter, ELDCombatElement::Chill, TEXT("反应 碎冰")},
                            {ELDCombatReaction::Corrode, ELDCombatElement::Rot, TEXT("反应 腐蚀（电+腐）")}, {ELDCombatReaction::Frostbite, ELDCombatElement::Chill, TEXT("反应 冻伤（冰+腐）")}};
                        for (const FReelReaction& Item : Reactions)
                        {
                            const uint8 Reaction = uint8(Item.Reaction), Element = uint8(Item.Element);
                            const double Stamp = 2000. + At;
                            Reel.Steps.Add({At, Label(Item.Name)});
                            Reel.Steps.Add({At, [V1, Reaction, Element, Stamp]() { if (auto* B = V1.Get()) { B->SetReaction(Reaction, Element, 0.f, Stamp); } }});
                            At += 1.1f;
                        }
                        struct FReelState { ELDDemoStatusId Status; const TCHAR* Name; };
                        const FReelState States[] = {{ELDDemoStatusId::Burn, TEXT("状态 着火")}, {ELDDemoStatusId::Wet, TEXT("状态 湿身")},
                            {ELDDemoStatusId::Electrified, TEXT("状态 带电")}, {ELDDemoStatusId::Chilled, TEXT("状态 冰冷")}, {ELDDemoStatusId::Rotten, TEXT("状态 腐烂")}};
                        for (const FReelState& Item : States)
                        {
                            const uint8 Status = uint8(Item.Status);
                            Reel.Steps.Add({At, Label(Item.Name)});
                            Reel.Steps.Add({At, [V1, Status]() { if (auto* B = V1.Get()) { FLDObservedStatus On; On.StatusId = Status; B->SetElementStates({On}); } }});
                            Reel.Steps.Add({At + .8f, [V1]() { if (auto* B = V1.Get()) { B->SetElementStates({}); } }});
                            At += 1.f;
                        }
                        // Each ground state's loop: a 220 cm state under V1 shown for 2 s, then removed (its loop fades out).
                        struct FReelGround { ELDGroundStateKind Kind; const TCHAR* Name; };
                        const FReelGround Grounds[] = {{ELDGroundStateKind::Puddle, TEXT("地面 水洼")}, {ELDGroundStateKind::Ice, TEXT("地面 冰面")},
                            {ELDGroundStateKind::Fire, TEXT("地面 火")}, {ELDGroundStateKind::Miasma, TEXT("地面 瘴气")}, {ELDGroundStateKind::Field, TEXT("地面 电场")}};
                        uint8 GroundId = 60;
                        for (const FReelGround& Item : Grounds)
                        {
                            const uint8 Kind = uint8(Item.Kind), Id = GroundId++;
                            const TSharedRef<TWeakObjectPtr<ALDObservedGroundProxy>> Shown = MakeShared<TWeakObjectPtr<ALDObservedGroundProxy>>();
                            Reel.Steps.Add({At, Label(Item.Name)});
                            Reel.Steps.Add({At, [this, V1, Kind, Id, Shown]()
                            {
                                auto* B = V1.Get();
                                FActorSpawnParameters Spawn;
                                Spawn.ObjectFlags |= RF_Transient;
                                ALDObservedGroundProxy* Ground = B ? World->SpawnActor<ALDObservedGroundProxy>(Spawn) : nullptr;
                                if (!Check(Ground != nullptr, TEXT("Reel ground state proxy spawned"))) { return; }
                                FLDObservedGroundState State;
                                State.Id = Id;
                                State.Kind = Kind;
                                State.CentreCm = B->GetOwner()->GetActorLocation() - FVector(0, 0, 90);
                                State.RadiusCm = 220.f;
                                State.RemainingMs = State.TotalMs = 2400;
                                Ground->Apply(State, 100.);
                                *Shown = Ground;
                            }});
                            Reel.Steps.Add({At + 2.f, [Shown]() { if (ALDObservedGroundProxy* Ground = Shown->Get()) { Ground->Destroy(); } }});
                            At += 2.6f;
                        }
                        // Six bodies brawling for 12 s (the mix and readability worst case of AUDIO_AND_FEEL §3): the own A
                        // combos V1 with its confirmations, A3 and V1 trade basics, A2 and V2 cast skills, V3 reacts, V2 burns,
                        // and a fire floor and a puddle lie under them. A fixed schedule, so every run is the same brawl.
                        {
                            const float Start = At;
                            Reel.Steps.Add({Start, Label(TEXT("6 人混战 12 秒"))});
                            for (int32 Cycle = 0; Cycle < 3; ++Cycle)
                            {
                                for (uint8 Step = 1; Step <= 3; ++Step)
                                {
                                    const float T = Start + .3f + Cycle * 4.f + (Step - 1) * (1.f / .85f);
                                    Reel.Steps.Add({T, Swing(A, Step, W)});
                                    Reel.Steps.Add({T + W + .02f, Plain(V1)});
                                    Reel.Steps.Add({T + W + .02f, Confirmed(A, 0)});
                                    Reel.Steps.Add({T + .45f, Swing(A3, Step, W)});
                                    Reel.Steps.Add({T + .45f + W + .02f, Plain(V3)});
                                    Reel.Steps.Add({T + .8f, Swing(V1, Step, W)});
                                    Reel.Steps.Add({T + .8f + W + .02f, Plain(A3)});
                                }
                            }
                            const ELDDemoAbilityId BrawlSkills[] = {ELDDemoAbilityId::ActiveSnarkBolt, ELDDemoAbilityId::ActiveCalculusDrop,
                                ELDDemoAbilityId::ActiveHoseDown, ELDDemoAbilityId::ActiveChargedRam, ELDDemoAbilityId::ActiveColaShake,
                                ELDDemoAbilityId::ActiveSchoolBag, ELDDemoAbilityId::ActiveFlurryRush, ELDDemoAbilityId::ActiveSeethe};
                            for (int32 Index = 0; Index < 8; ++Index)
                            {
                                const ELDDemoAbilityId Ability = BrawlSkills[Index];
                                const auto Caster = Index % 2 == 0 ? A2 : V2;
                                const auto Struck = Index % 2 == 0 ? V2 : A2;
                                const float T = Start + .6f + Index * 1.4f;
                                Reel.Steps.Add({T, [Caster, Ability]() { if (auto* B = Caster.Get()) { B->SetComboStep(0); B->SetCastAbility(uint8(Ability)); B->SetCue(2, 0.f, .18f); } }});
                                Reel.Steps.Add({T + .23f, [Struck]() { if (auto* B = Struck.Get()) { B->SetHitReaction(0.f, ELDObservedHitLook::Magic, 0, FGuid::NewGuid()); } }});
                            }
                            Reel.Steps.Add({Start + 5.2f, Cast(V2)});
                            Reel.Steps.Add({Start + 5.47f, Hit(A2)});
                            Reel.Steps.Add({Start + 5.62f, Hit(V3)});
                            // Each reaction with the element that triggers it, as in the reaction section above.
                            const TPair<ELDCombatReaction, ELDCombatElement> BrawlReactions[] = {{ELDCombatReaction::Conduct, ELDCombatElement::Shock},
                                {ELDCombatReaction::Melt, ELDCombatElement::Fire}, {ELDCombatReaction::Overload, ELDCombatElement::Shock},
                                {ELDCombatReaction::Shatter, ELDCombatElement::Chill}};
                            for (int32 Index = 0; Index < 4; ++Index)
                            {
                                const uint8 Reaction = uint8(BrawlReactions[Index].Key), Element = uint8(BrawlReactions[Index].Value);
                                const double Stamp = 3000. + Index;
                                Reel.Steps.Add({Start + 1.5f + Index * 2.7f, [V3, Reaction, Element, Stamp]() { if (auto* B = V3.Get()) { B->SetReaction(Reaction, Element, 0.f, Stamp); } }});
                            }
                            Reel.Steps.Add({Start + 2.f, [V2]() { if (auto* B = V2.Get()) { FLDObservedStatus On; On.StatusId = uint8(ELDDemoStatusId::Burn); B->SetElementStates({On}); } }});
                            Reel.Steps.Add({Start + 8.f, [V2]() { if (auto* B = V2.Get()) { B->SetElementStates({}); } }});
                            const auto Floor = [this](TWeakObjectPtr<ULDHumanoidVisualComponent> Under, ELDGroundStateKind Kind, uint8 Id,
                                TSharedRef<TWeakObjectPtr<ALDObservedGroundProxy>> Shown)
                            {
                                return [this, Under, Kind, Id, Shown]()
                                {
                                    auto* B = Under.Get();
                                    FActorSpawnParameters Spawn;
                                    Spawn.ObjectFlags |= RF_Transient;
                                    ALDObservedGroundProxy* Ground = B ? World->SpawnActor<ALDObservedGroundProxy>(Spawn) : nullptr;
                                    if (!Check(Ground != nullptr, TEXT("Brawl ground state proxy spawned"))) { return; }
                                    FLDObservedGroundState State;
                                    State.Id = Id;
                                    State.Kind = uint8(Kind);
                                    State.CentreCm = B->GetOwner()->GetActorLocation() - FVector(0, 0, 90);
                                    State.RadiusCm = 220.f;
                                    State.RemainingMs = State.TotalMs = 8000;
                                    Ground->Apply(State, 100.);
                                    *Shown = Ground;
                                };
                            };
                            const auto FireFloor = MakeShared<TWeakObjectPtr<ALDObservedGroundProxy>>();
                            const auto Puddle = MakeShared<TWeakObjectPtr<ALDObservedGroundProxy>>();
                            Reel.Steps.Add({Start + 1.f, Floor(V3, ELDGroundStateKind::Fire, 70, FireFloor)});
                            Reel.Steps.Add({Start + 3.f, Floor(V2, ELDGroundStateKind::Puddle, 71, Puddle)});
                            Reel.Steps.Add({Start + 11.f, [FireFloor, Puddle]()
                            {
                                if (ALDObservedGroundProxy* Ground = FireFloor->Get()) { Ground->Destroy(); }
                                if (ALDObservedGroundProxy* Ground = Puddle->Get()) { Ground->Destroy(); }
                            }});
                            At = Start + 12.5f;
                        }
                        // The steps run in list order: the brawl's overlapping schedules are put in time order (stable, so steps
                        // at one moment keep their written order).
                        Reel.Steps.StableSort([](const FSeqStep& L, const FSeqStep& R) { return L.At < R.At; });
                        Reel.bRealtime = true;
                        Reel.Seconds = At + .5f;
                        ChainJobs.Add(Reel);
                    }
                    continue;
                }
                else
                {
                // (a) a short hop of 250 cm along the checked dash line in front of A.
                ChainJobs.Add({Look, TEXT("a-short-hop"), {{0.f, Cast(A)}, {.27f, Hit(V1)}},
                    [V1, AHome]() { if (auto* B = V1.Get()) { B->GetOwner()->SetActorLocation(AHome + FVector(250., 0., 0.)); B->GetOwner()->SetActorRotation(FRotator(0, 180.f, 0)); } },
                    [V1, V1Home]() { if (auto* B = V1.Get()) { B->GetOwner()->SetActorLocation(V1Home); B->GetOwner()->SetActorRotation(FRotator(0, 225.f, 0)); } }});
                // (b) the longest real hop: A2 across the ring to V2, 640 cm (zap's range is 650 cm), shown as the own cast.
                ChainJobs.Add({Look, TEXT("b-long-hop"), {{0.f, Cast(A2)}, {.27f, Hit(V2)}}, [Own, A2]() { Own(A2, true); }, [Own, A2]() { Own(A2, false); }});
                // (c) a first hit only.
                ChainJobs.Add({Look, TEXT("c-first-hit"), {{0.f, Cast(A)}, {.27f, Hit(V1)}}, nullptr, nullptr});
                // (d) a chain with two bounces.
                ChainJobs.Add({Look, TEXT("d-two-bounces"), {{0.f, Cast(A)}, {.27f, Hit(V1)}, {.42f, Hit(V2)}, {.57f, Hit(V3)}}, nullptr, nullptr});
                // (e) the own cast and another player's 50 ms later. A hit carries no caster: the strike registry gives it to
                // the strike whose release is closest in time, so the casts are staggered for each hit to find its own.
                ChainJobs.Add({Look, TEXT("e-own-vs-other"), {{0.f, Cast(A)}, {.05f, Cast(A3)}, {.27f, Hit(V1)}, {.32f, Hit(V3)}}, nullptr, nullptr, false, .32f});
                // (f) three casters 60 ms apart, each hitting its own target 40 ms after its release (a hit in the same frame
                // as the release would find no strike yet); A is the own one.
                ChainJobs.Add({Look, TEXT("f-several-casters"), {{0.f, Cast(A)}, {.06f, Cast(A2)}, {.12f, Cast(A3)}, {.29f, Hit(V1)}, {.35f, Hit(V2)},
                    {.41f, Hit(V3)}}, nullptr, nullptr, false, .35f});
                // (g) a short own hop with both bodies on the warm sunlit floor: the floor points under two screen points of
                // the sunlit band below the stage, 140 px (about 250 cm) apart.
                ChainJobs.Add({Look, TEXT("g-sunlit-hop"), {{0.f, Cast(A)}, {.27f, Hit(V1)}}, Sunlit, Home});
                }
                // New categories are judged on their looks first; their frame cost is measured once a look is chosen.
                static const TCHAR* const LookOnly[] = {
                    TEXT("water"),
                    TEXT("fire"),
                    TEXT("chill"),
                    TEXT("states"),
                    TEXT("grounds"),
                    TEXT("poses"),
                    TEXT("kill"),
                    TEXT("casts"),
                    TEXT("skills"),
                    TEXT("self"),
                    TEXT("packs"),
                    TEXT("reactions"),
                    TEXT("flights"),
                    TEXT("world"),
                    TEXT("load"),
                };
                bool bLookOnly = false;
                for (const TCHAR* Category : LookOnly) { bLookOnly |= Look == Category; }
                if (bLookOnly) { continue; }
                FChainJob Perf;
                Perf.Category = Look;
                Perf.Scenario = TEXT("perf-eight-casters");
                Perf.bPerf = true;
                // Three frame-cost runs per look, so the spread can be read.
                for (int32 Run = 0; Run < 3; ++Run) { ChainJobs.Add(Perf); }
            }
            // Eight casters and eight victims for the frame-cost run, hidden outside it.
            for (int32 Index = 0; Index < 8; ++Index)
            {
                const double X = -350. + Index * 100.;
                PerfCasters.Add(SpawnVfxBody(Target, Focus + FVector(X, -260., 0.), 90.f, uint8(Index)));
                PerfVictims.Add(SpawnVfxBody(Target, Focus + FVector(X, 140., 0.), 270.f, uint8((Index + 4) % 8)));
            }
            for (const TWeakObjectPtr<ULDHumanoidVisualComponent>& Body : PerfCasters) { if (Body.IsValid()) { Body->SetVisibility(false, true); } }
            for (const TWeakObjectPtr<ULDHumanoidVisualComponent>& Body : PerfVictims) { if (Body.IsValid()) { Body->SetVisibility(false, true); } }
            return Check(PerfCasters.Num() == 8 && PerfVictims.Num() == 8, TEXT("Chain harness: eight casters and eight victims for the frame-cost run"));
        }

        void SampleFrame(UWorld& Target, FFrameStats& Stats)
        {
            auto* Impacts = Target.GetSubsystem<ULDImpactFxSubsystem>();
            ++Stats.Frames;
            Stats.RealMs += FApp::GetDeltaTime() * 1000.;
            Stats.GameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
            Stats.RenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
            Stats.GpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
            if (!Impacts) { return; }
            int32 Components = 0, Particles = 0;
            Impacts->ForEachGenerated([&](UNiagaraComponent& Effect) { ++Components; Particles += ParticlesOf(Effect); });
            Stats.MaxComponents = FMath::Max(Stats.MaxComponents, Components);
            Stats.MaxParticles = FMath::Max(Stats.MaxParticles, Particles);
            Stats.MaxLayers = FMath::Max(Stats.MaxLayers, Impacts->LiveSliceCount());
            Stats.MaxLive = FMath::Max(Stats.MaxLive, Impacts->LiveCount());
        }

        static TSharedPtr<FJsonObject> StatsJson(const FFrameStats& Stats)
        {
            auto Out = MakeShared<FJsonObject>();
            const double N = FMath::Max(Stats.Frames, 1);
            Out->SetNumberField(TEXT("frames"), Stats.Frames);
            Out->SetNumberField(TEXT("frame_ms"), Stats.RealMs / N);
            Out->SetNumberField(TEXT("game_ms"), Stats.GameMs / N);
            Out->SetNumberField(TEXT("render_ms"), Stats.RenderMs / N);
            Out->SetNumberField(TEXT("gpu_ms"), Stats.GpuMs / N);
            Out->SetNumberField(TEXT("max_chain_components"), Stats.MaxComponents);
            Out->SetNumberField(TEXT("max_chain_particles"), Stats.MaxParticles);
            Out->SetNumberField(TEXT("max_slice_layers"), Stats.MaxLayers);
            Out->SetNumberField(TEXT("max_live_niagara"), Stats.MaxLive);
            return Out;
        }

        // One tick of the chain harness; false ends the probe.
        bool TickChain(UWorld& Target, double Now, float SimDelta)
        {
            auto* Impacts = Target.GetSubsystem<ULDImpactFxSubsystem>();
            if (!Impacts) { return Finish(TEXT("No impact effect subsystem for the chain harness")); }
            FChainJob& Job = ChainJobs[ChainJob];
            const FString Folder = FString::Printf(TEXT("proof/%s/%s"), *Job.Category, *Job.Scenario);
            if (!PendingImage.IsEmpty())
            {
                if (IFileManager::Get().FileSize(*PendingImage) <= 0) { return true; }
                PendingImage.Reset();
                UGameplayStatics::SetGlobalTimeDilation(&Target, 1.f);
                return true;
            }
            if (ChainPhase == 0)
            {
                // A clean stage on a wall clock: every body idle, no effect left, shaders done.
                FApp::SetUseFixedTimeStep(false);
                UGameplayStatics::SetGlobalTimeDilation(&Target, 1.f);
                SeqBodies([Impacts](ULDHumanoidVisualComponent& Body) { Body.SetCue(0); Body.SetCastAbility(0); Impacts->ClearFor(Body); });
                if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { return true; }
                if (Job.Setup) { Job.Setup(); }
                ChainClock = 0.;
                ChainFrame = ChainStep = 0;
                ChainPhaseAt = Now;
                ChainFrameTimes.Reset();
                ChainCheck = MakeShared<FJsonObject>();
                if (Job.bRealtime)
                {
                    IFileManager::Get().MakeDirectory(*(Directory / Folder), true);
                    // The window shows the stage as the real-time recording sees it: no lobby or HUD widgets over it.
                    if (UGameViewportClient* Viewport = Target.GetGameViewport()) { Viewport->RemoveAllViewportWidgets(); }
                    // Only the music bus is muted so the combat sounds can be judged on their own; every other bus plays as set.
                    if (UGameInstance* Instance = Target.GetGameInstance())
                    { if (auto* Audio = Instance->GetSubsystem<ULDAudioSubsystem>()) { Audio->ApplyBusVolume(ELDAudioBus::Music, 0.f); } }
                    UAudioMixerBlueprintLibrary::StartRecordingOutput(&Target, Job.Seconds + 1.f);
                    // The wall clock of the recording start lets the window recording be aligned to the game audio.
                    UE_LOG(LogLDTest, Display, TEXT("LD_FEEL_RECORDING_START utc=%s"), *FDateTime::UtcNow().ToIso8601());
                    // Phase 5: the real-time recording (phases 3 and 4 are a frame-cost run's idle and load).
                    ChainPhase = 5;
                    return true;
                }
                if (Job.bPerf)
                {
                    for (const auto* List : {&PerfCasters, &PerfVictims}) { for (const auto& Body : *List) { if (Body.IsValid()) { Body->SetVisibility(true, true); } } }
                    PerfIdle = PerfLoad = FFrameStats();
                    ChainPhase = 3;
                    return true;
                }
                IFileManager::Get().MakeDirectory(*(Directory / Folder), true);
                // Real game speed: every frame advances exactly 1/30 s of game time and is captured.
                FApp::SetUseFixedTimeStep(true);
                FApp::SetFixedDeltaTime(ChainStep_);
                ChainPhase = 1;
                return true;
            }
            if (ChainPhase == 1)
            {
                ChainClock += SimDelta;
                while (ChainStep < Job.Steps.Num() && Job.Steps[ChainStep].At <= ChainClock) { Job.Steps[ChainStep++].Do(); }
                // Proof ground states age on the scenario clock (Apply sampled them at server time 100), so they end.
                for (int32 Index = 0; Index < ProofGrounds.Num() && Index < ProofGroundsAt.Num(); ++Index)
                { if (ALDObservedGroundProxy* Ground = ProofGrounds[Index].Get()) { Ground->Present(100. + ChainClock - ProofGroundsAt[Index]); } }
                // The user parameters of every live link: own links full width, others' thinner; each starts at its caster
                // or the previous body struck.
                Impacts->ForEachGenerated([this](UNiagaraComponent& Effect)
                {
                    const float Width = Effect.GetOverrideParameters().GetParameterValue<float>(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Width")));
                    const FVector3f Start = Effect.GetOverrideParameters().GetParameterValue<FVector3f>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.Start")));
                    const FString Key = FString::Printf(TEXT("%p %s"), &Effect, *FVector(Start).ToCompactString());
                    if (!ChainCheck->HasField(Key))
                    {
                        auto Entry = MakeShared<FJsonObject>();
                        Entry->SetNumberField(TEXT("width"), Width);
                        Entry->SetStringField(TEXT("start"), FVector(Start).ToCompactString());
                        Entry->SetStringField(TEXT("end"), FVector(Effect.GetOverrideParameters().GetParameterValue<FVector3f>(
                            FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.End")))).ToCompactString());
                        Entry->SetNumberField(TEXT("intensity"), Effect.GetOverrideParameters().GetParameterValue<float>(
                            FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Intensity"))));
                        Entry->SetNumberField(TEXT("focus"), Effect.GetOverrideParameters().GetParameterValue<float>(
                            FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Focus"))));
                        ChainCheck->SetObjectField(Key, Entry);
                    }
                });
                if (ChainClock + .5 * ChainStep_ < Job.From) { return true; }
                if (ChainFrame < Job.Frames)
                {
                    UGameplayStatics::SetGlobalTimeDilation(&Target, .0001f);
                    PendingImage = Directory / Folder / FString::Printf(TEXT("%03d.png"), ChainFrame);
                    Images.Add(MakeShared<FJsonValueString>(Folder / FString::Printf(TEXT("%03d.png"), ChainFrame)));
                    ChainFrameTimes.Add(MakeShared<FJsonValueNumber>(ChainClock));
                    FScreenshotRequest::RequestScreenshot(PendingImage, false, false);
                    ++ChainFrame;
                    return true;
                }
                // Every link and layer is released once its effect ends (a scenario's ground states end with its teardown).
                FApp::SetUseFixedTimeStep(false);
                if (Job.Teardown) { Job.Teardown(); }
                ChainPhase = 2;
                ChainPhaseAt = Now;
                return true;
            }
            if (ChainPhase == 5)
            {
                // Real game speed: the scenario clock is the world's own delta, the steps fire on it, and the audio device
                // plays and records exactly what a match would.
                ChainClock += SimDelta;
                while (ChainStep < Job.Steps.Num() && Job.Steps[ChainStep].At <= ChainClock)
                {
                    UE_LOG(LogLDTest, Display, TEXT("LD_FEEL_STEP t=%.4f world=%.4f step=%d"), ChainClock, Target.GetTimeSeconds(), ChainStep);
                    Job.Steps[ChainStep++].Do();
                }
                if (ChainClock < Job.Seconds) { return true; }
                const FString Wav = FPaths::ConvertRelativePathToFull(Directory / Folder);
                UAudioMixerBlueprintLibrary::StopRecordingOutput(&Target, EAudioRecordingExportType::WavFile, TEXT("game_audio"), Wav);
                UE_LOG(LogLDTest, Display, TEXT("LD_FEEL_RECORDING_STOP utc=%s file=%s/game_audio.wav"), *FDateTime::UtcNow().ToIso8601(), *Wav);
                if (Job.Teardown) { Job.Teardown(); }
                ChainPhaseAt = Now;
                ChainPhase = 2;
                return true;
            }
            if (ChainPhase == 2)
            {
                const bool bReleased = Impacts->LiveCount() == 0 && Impacts->LiveSliceCount() == 0;
                if (!bReleased && Now - ChainPhaseAt < 4.) { return true; }
                Check(bReleased, FString::Printf(TEXT("Proof %s %s: every effect released afterwards"), *Job.Category, *Job.Scenario));
                auto Run = MakeShared<FJsonObject>();
                Run->SetStringField(TEXT("look"), Job.Category);
                Run->SetStringField(TEXT("label"), ProofLabel);
                Run->SetStringField(TEXT("scenario"), Job.Scenario);
                Run->SetStringField(TEXT("folder"), Folder);
                Run->SetArrayField(TEXT("frame_game_seconds"), ChainFrameTimes);
                Run->SetNumberField(TEXT("first_hit_seconds"), Job.ShowAt);
                Run->SetBoolField(TEXT("released"), bReleased);
                Run->SetNumberField(TEXT("release_seconds"), Now - ChainPhaseAt);
                Run->SetObjectField(TEXT("links"), ChainCheck);
                ChainRuns.Add(MakeShared<FJsonValueObject>(Run));
                ChainPhase = 0;
                ++ChainJob;
                return true;
            }
            // Frame cost: 3 s idle, then 5 s of all eight casters acting every 0.6 s: chain a zap with one bounce; hit and
            // swing the combo steps in turn on their victims; field the same with an electric field under every victim.
            const double Age = Now - ChainPhaseAt;
            const bool bChain = Job.Category == TEXT("chain"), bField = Job.Category == TEXT("field");
            if (ChainPhase == 3)
            {
                if (Age > 1.) { SampleFrame(Target, PerfIdle); }
                if (Age >= 4.)
                {
                    ChainPhase = 4; ChainPhaseAt = Now; ChainNextWave = 0.; ChainWave = 0;
                    for (int32 Index = 0; bField && Index < PerfVictims.Num(); ++Index)
                    {
                        FActorSpawnParameters Spawn;
                        Spawn.ObjectFlags |= RF_Transient;
                        ALDObservedGroundProxy* Ground = PerfVictims[Index].IsValid() ? World->SpawnActor<ALDObservedGroundProxy>(Spawn) : nullptr;
                        if (!Ground) { continue; }
                        FLDObservedGroundState State;
                        State.Id = 300 + Index;
                        State.Kind = uint8(ELDGroundStateKind::Field);
                        State.CentreCm = PerfVictims[Index]->GetOwner()->GetActorLocation() - FVector(0, 0, 90);
                        State.RadiusCm = 220.f;
                        State.RemainingMs = State.TotalMs = 8000;
                        Ground->Apply(State, 100.);
                        ProofGrounds.Add(Ground);
                    }
                }
                return true;
            }
            const float W = BasicWindupSeconds;
            if (Job.LoadEvery > 0.f)
            {
                if (Age >= ChainNextWave) { ChainNextWave += Job.LoadEvery; ++ChainWave; ChainStep = 0; ChainClock = Age; }
                while (ChainStep < Job.Steps.Num() && Job.Steps[ChainStep].At <= Age - ChainClock) { Job.Steps[ChainStep++].Do(); }
            }
            else if (Age >= ChainNextWave * 1.)
            {
                // Waves on the wall clock: casts or swings now, the hits and bounces on their own delays below.
                ChainNextWave += .6;
                ++ChainWave;
                for (const auto& Body : PerfCasters)
                {
                    if (auto* B = Body.Get())
                    {
                        if (bChain) { B->SetCastAbility(uint8(ELDDemoAbilityId::ActiveZap)); B->SetCue(2, 0.f, .25f); }
                        else { B->SetComboStep(uint8(1 + (ChainWave - 1) % 3)); B->SetCastAbility(0); B->SetCue(1, 0.f, W); }
                    }
                }
                ChainClock = Age;
            }
            const double WaveAge = Age - ChainClock;
            const double HitAt = bChain ? .27 : W + .02;
            for (int32 Index = 0; Job.LoadEvery <= 0.f && Index < PerfVictims.Num(); ++Index)
            {
                if (auto* Victim = PerfVictims[Index].Get())
                {
                    if (WaveAge >= HitAt && WaveAge - SimDelta < HitAt) { Victim->SetHitReaction(0.f, bChain ? ELDObservedHitLook::Electric : ELDObservedHitLook::None, 0, FGuid::NewGuid()); }
                }
                if (auto* Next = bChain ? PerfVictims[(Index + 1) % PerfVictims.Num()].Get() : nullptr)
                {
                    if (WaveAge >= .42 && WaveAge - SimDelta < .42) { Next->SetHitReaction(0.f, ELDObservedHitLook::Electric, 0, FGuid::NewGuid()); }
                }
            }
            for (const auto& Body : PerfCasters) { if (auto* B = Body.Get()) { B->Animate(FVector::ZeroVector, SimDelta); } }
            for (const auto& Body : PerfVictims) { if (auto* B = Body.Get()) { B->Animate(FVector::ZeroVector, SimDelta); } }
            if (Age > 1.) { SampleFrame(Target, PerfLoad); }
            if (Age < 6.) { return true; }
            auto Run = MakeShared<FJsonObject>();
            Run->SetStringField(TEXT("look"), Job.Category);
            Run->SetStringField(TEXT("label"), ProofLabel);
            Run->SetObjectField(TEXT("idle"), StatsJson(PerfIdle));
            Run->SetObjectField(TEXT("eight_casters"), StatsJson(PerfLoad));
            Run->SetNumberField(TEXT("waves"), ChainWave);
            Run->SetStringField(TEXT("scenario"), Job.Scenario);
            ChainPerf.Add(MakeShared<FJsonValueObject>(Run));
            if (Job.LoadEvery > 0.f && Job.Teardown) { Job.Teardown(); }
            for (const TWeakObjectPtr<ALDObservedGroundProxy>& Ground : ProofGrounds) { if (ALDObservedGroundProxy* Old = Ground.Get()) { Old->Destroy(); } }
            ProofGrounds.Reset();
            ProofGroundsAt.Reset();
            for (const auto* List : {&PerfCasters, &PerfVictims})
            {
                for (const auto& Body : *List) { if (auto* B = Body.Get()) { B->SetCue(0); Impacts->ClearFor(*B); B->SetVisibility(false, true); } }
            }
            ChainPhase = 0;
            ++ChainJob;
            return true;
        }

        bool PrepareVfxShot(const FArtShot& Current)
        {
            auto* Attacker = VfxAttacker.Get();
            auto* Victim = VfxVictim.Get();
            if (!Attacker || !Victim) { return Check(false, TEXT("Combat VFX bodies remain available")); }
            auto* Impacts = World->GetSubsystem<ULDImpactFxSubsystem>();
            if (Impacts) { Impacts->ClearFor(*Victim); Impacts->ClearFor(*Attacker); }
            if (ALDObservedProjectileProxy* Old = VfxEffect.Get()) { Old->Destroy(); }
            VfxEffect.Reset();
            if (ALDObservedGroundProxy* Old = VfxGround.Get()) { Old->Destroy(); }
            VfxGround.Reset();
            VfxFlightAt = -1.;
            Check(Impacts && Impacts->WarmedSystems() >= 6, TEXT("Every effect system, the Niagara Chain included, was warmed up at world start"));
            if (bVfxDash) { Attacker->GetOwner()->SetActorLocation(VfxDashFrom); }
            bVfxDash = false;
            Attacker->SetCastAbility(Current.CastAbility);
            Victim->Animate(FVector::ZeroVector, .5f);
            Attacker->SetCue(Current.Action, Current.Action ? (Current.bHit ? .36f : .22f) : -1.f, .4f);
            Attacker->Animate(FVector::ZeroVector, 0.f);
            if (Current.bHit)
            {
                Victim->SetHitReaction(Current.HitAgeSeconds, Current.HitLook, Current.HitFlags);
                Victim->Animate(FVector::ZeroVector, 0.f);
                const bool bSlice = LDSliceVfx::ShowsHit(Current.HitLook, 0);
                if (!Check(Impacts != nullptr, TEXT("The impact effects subsystem exists for a shown hit"))) { return false; }
                Check(!bSlice || Impacts->LiveSliceCount() > 0, TEXT("A plain, water or electric hit shows its designed slice burst"));
                // The own receipt of the same tier flashes the screen edge (drawn by the world feedback layer).
                Impacts->FlashScreen(LDHitVfx::ScreenFlashOf(Current.HitFlags, false));
            }
            if (Current.bDash)
            {
                // Streak the attacker across the shot at dash speed (moved by Tick).
                bVfxDash = true;
                VfxDashFrom = Attacker->GetOwner()->GetActorLocation();
                Attacker->Animate(FVector(1500.f, 0.f, 0.f), 0.f);
            }
            if (Current.bEffect)
            {
                FActorSpawnParameters Spawn;
                Spawn.ObjectFlags |= RF_Transient;
                ALDObservedProjectileProxy* Effect = World->SpawnActor<ALDObservedProjectileProxy>(Spawn);
                if (!Check(Effect != nullptr, TEXT("Capture effect proxy spawned"))) { return false; }
                FLDObservedProjectileView View;
                View.Handle = FGuid::NewGuid();
                View.Look = Current.Effect;
                View.RadiusCm = Current.EffectRadius;
                View.PositionCm = Victim->GetOwner()->GetActorLocation() - FVector(0, 0, 90);
                View.Heading = uint16(225.f / 360.f * 65536.f);
                if (Current.bFlying) { View.PositionCm = Attacker->GetOwner()->GetActorLocation() - FVector(0, 0, 90); }
                Effect->Apply(View, 100.);
                Effect->Present(100.);
                VfxEffect = Effect;
                if (Current.bFlying) { VfxFlight = View; VfxFlightAt = FPlatformTime::Seconds(); }
            }
            if (Current.bShowUI)
            {
                // The screen edge flash is drawn by the world feedback layer: show that layer alone over the scene.
                UWidgetLayoutLibrary::RemoveAllWidgets(World.Get());
                auto* Controller = Cast<ALDPlayerController>(World->GetFirstPlayerController());
                ULDWorldFeedback* Feedback = Controller ? CreateWidget<ULDWorldFeedback>(Controller) : nullptr;
                if (!Check(Feedback != nullptr, TEXT("World feedback layer created for the edge flash"))) { return false; }
                Feedback->AddToViewport();
                const LDHitVfx::FScreenFlash Flash = LDHitVfx::ScreenFlashOf(LDCombatCues::DealtLethal, false);
                Impacts->FlashScreen(Flash);
                FLinearColor Colour;
                Check(Impacts->GetScreenEdge() != nullptr && Impacts->ScreenFlashAlpha(Colour) > 0.f && Impacts->ScreenFringe() > 0.f,
                    TEXT("A kill receipt starts the screen edge flash and the lens fringe with its mask loaded"));
            }
            if (Current.ReactionElement != 0)
            {
                const int32 Before = Impacts ? Impacts->LiveCount() : 0;
                Victim->SetReaction(uint8(ELDCombatReaction::Methane), Current.ReactionElement, .03f, 100. + Shot);
                Victim->Animate(FVector::ZeroVector, 0.f);
                Check(Impacts && Impacts->LiveCount() - Before == 1, TEXT("A fresh element reaction plays its own effect once"));
                const int32 After = Impacts ? Impacts->LiveCount() : 0;
                Victim->SetReaction(uint8(ELDCombatReaction::Methane), Current.ReactionElement, .05f, 100. + Shot);
                Victim->Animate(FVector::ZeroVector, 0.f);
                Check(Impacts && Impacts->LiveCount() == After, TEXT("The same reaction seen again adds nothing"));
            }
            if (Current.GroundKind != 0)
            {
                FActorSpawnParameters Spawn;
                Spawn.ObjectFlags |= RF_Transient;
                ALDObservedGroundProxy* Ground = World->SpawnActor<ALDObservedGroundProxy>(Spawn);
                if (!Check(Ground != nullptr, TEXT("Capture ground state proxy spawned"))) { return false; }
                FLDObservedGroundState State;
                State.Id = 1;
                State.Kind = Current.GroundKind;
                State.Shape = ELDObservedGroundShape::Circle;
                State.CentreCm = Victim->GetOwner()->GetActorLocation() - FVector(0, 0, 90);
                State.RadiusCm = 220.f;
                State.RemainingMs = State.TotalMs = 6000;
                Ground->Apply(State, 100.);
                // The electric field shows its designed crackle (LDSliceVfx::GroundField); every other kind its particles.
                Check(Current.GroundKind == uint8(ELDGroundStateKind::Field) ? Impacts && Impacts->LiveSliceCount() > 0 : Ground->GetParticles() != nullptr,
                    TEXT("A ground state shows its kind's particles or its field over its pattern"));
                VfxGround = Ground;
            }
            if (Current.bKill && Impacts)
            {
                Victim->PlayFall();
                Check(Impacts->LiveCount() > 0, TEXT("A visible fall knocks a ring of dust over the floor"));
                Impacts->FlashScreen(LDHitVfx::ScreenFlashOf(LDCombatCues::DealtLethal, false));
            }
            return true;
        }

        bool Setup(UWorld& Target)
        {
            const FLDArtResolved* Art = ULDArtSubsystem::Get(&Target);
            if (!Check(Art != nullptr, TEXT("The art catalog resolved at startup"))) { return false; }
            if (bCombatVfx) { return SetupVfx(Target); }
            CheckDecorations(Target);
#if WITH_EDITORONLY_DATA
            // The art master is authored by the import commandlet; its graph must load where shaders are compiled.
            const auto* Master = LoadObject<UMaterial>(nullptr, TEXT("/Game/Lockdown/Art/Materials/M_ArtSurface.M_ArtSurface"));
            const UMaterialEditorOnlyData* Inputs = Master ? Master->GetEditorOnlyData() : nullptr;
            Check(Inputs && Inputs->BaseColor.IsConnected() && Inputs->EmissiveColor.IsConnected() && Master->GetExpressions().Num() >= 5,
                FString::Printf(TEXT("M_ArtSurface loads its BaseTexture x Tint colour and Flash emissive graph (%d expressions)"), Master ? Master->GetExpressions().Num() : -1));
#endif
            CheckOcclusion(*Target.GetGameInstance()->GetSubsystem<ULDArtSubsystem>()->GetCatalog());
            // Lineups on open floor (core south of the flagpole, outer ring north of S07/S08), across the -45 degree camera.
            const FVector Across(UE_INV_SQRT_2, UE_INV_SQRT_2, 0.);
            const float FaceCamera = 135.f;
            const FVector Identity(0., -1100., 0.), States(0., -3300., 0.);
            const uint8 Tiers[] = { uint8(ELDDemoMonsterId::MonsterScavenger), uint8(ELDDemoMonsterId::MonsterGuardian),
                uint8(ELDDemoMonsterId::MonsterDean), uint8(ELDDemoMonsterId::MonsterAdvisor) };
            int32 Slot = 0;
            for (uint8 Seat = 0; Seat < LDArt::SeatCount; ++Seat, ++Slot)
            {
                SpawnBody(Target, *Art->ForSeat(Seat), ULDHumanoidVisualComponent::ContenderTint(Seat), Identity + Across * ((Slot - 4.5f) * 125.f), FaceCamera, ELDArtAnim::Idle);
            }
            for (const uint8 Tier : Tiers)
            {
                SpawnBody(Target, *Art->ForMonster(Tier), ULDHumanoidVisualComponent::MonsterTint(Tier), Identity + Across * ((Slot++ - 4.5f) * 125.f), FaceCamera, ELDArtAnim::Idle);
            }
            const FLDArtCharacter* Rows[] = { Art->ForSeat(0), Art->ForMonster(Tiers[0]), Art->ForMonster(Tiers[1]), Art->ForMonster(Tiers[2]) };
            const FLinearColor RowTints[] = { ULDHumanoidVisualComponent::ContenderTint(0), ULDHumanoidVisualComponent::MonsterTint(Tiers[0]),
                ULDHumanoidVisualComponent::MonsterTint(Tiers[1]), ULDHumanoidVisualComponent::MonsterTint(Tiers[2]) };
            const FVector Down(UE_INV_SQRT_2, -UE_INV_SQRT_2, 0.);
            for (int32 Row = 0; Row < UE_ARRAY_COUNT(Rows); ++Row)
            {
                for (int32 State = 0; State < LDArt::AnimCount; ++State)
                {
                    if (Rows[Row]->Find(ELDArtAnim(State)))
                    {
                        SpawnBody(Target, *Rows[Row], RowTints[Row], States + Across * ((State - 3.f) * 140.f) + Down * (Row * 260.f), FaceCamera, ELDArtAnim(State));
                    }
                }
            }
            // Bodies standing on routes next to every kind of prop: the five gates, around the flagpole and the north-east car,
            // and in front of wall-base benches. The one behind the flagpole (south-east of it) shows the listed occluder.
            const FVector Routes[] = {
                {1979., 643., 0.}, {0., 2081., 0.}, {-1979., 643., 0.}, {-1223., -1684., 0.}, {1223., -1684., 0.},
                {247., 247., 0.}, {-247., 247., 0.}, {247., -247., 0.}, {-247., -247., 0.},
                {428., 778., 0.}, {778., 428., 0.}, {1128., 778., 0.},
                {4720., 3000., 0.}, {4720., 2400., 0.}, {3000., 4720., 0.}, {-4720., -3000., 0.}, {-3000., -4720., 0.} };
            for (int32 Index = 0; Index < UE_ARRAY_COUNT(Routes); ++Index)
            {
                const uint8 Seat = uint8(Index % LDArt::SeatCount);
                SpawnBody(Target, *Art->ForSeat(Seat), ULDHumanoidVisualComponent::ContenderTint(Seat), Routes[Index], FaceCamera + 30.f * Index, ELDArtAnim::Idle);
            }
            Shots = {
                {TEXT("Characters-Identity"), Identity, LDCamera::MinArmLength},
                {TEXT("Characters-States"), States + Down * 260.f, 1300.f},
                {TEXT("Characters-GameZoom"), Identity, LDCamera::DefaultArmLength},
                {TEXT("Route-Capture"), FVector(0., 0., 0.), LDCamera::DefaultArmLength},
                {TEXT("Route-NorthEastCar"), FVector(778., 778., 0.), LDCamera::DefaultArmLength},
                {TEXT("Route-GateNorth"), FVector(0., 2081., 0.), LDCamera::DefaultArmLength},
                {TEXT("Route-EastBenches"), FVector(4400., 2800., 0.), LDCamera::DefaultArmLength},
                {TEXT("Field-Overview"), FVector(0., 0., 0.), 9000.f},
                {TEXT("Map-TopDown"), FVector(0., 0., 0.), 24000.f, true},
            };
            FActorSpawnParameters Spawn;
            Spawn.ObjectFlags |= RF_Transient;
            ACameraActor* View = Target.SpawnActor<ACameraActor>(Spawn);
            if (!Check(View != nullptr, TEXT("Capture camera spawned"))) { return false; }
            const UCameraComponent* Game = GetDefault<ALDCharacter>()->Camera;
            View->GetCameraComponent()->SetFieldOfView(Game->FieldOfView);
            View->GetCameraComponent()->PostProcessSettings = Game->PostProcessSettings;
            View->GetCameraComponent()->PostProcessBlendWeight = 1.f;
            View->GetCameraComponent()->bConstrainAspectRatio = false;
            Camera = View;
            auto* Controller = Cast<ALDPlayerController>(Target.GetFirstPlayerController());
            if (!Check(Controller != nullptr, TEXT("Art capture has a local Lockdown controller"))) { return false; }
            CaptureController = Controller;
            Controller->SetDevArtCaptureCamera(View);
            return true;
        }

        bool Tick(float Delta)
        {
            const double Now = FPlatformTime::Seconds();
            if (Now - StartedAt > (ChainJobs.IsEmpty() ? 600. : 1800.)) { return Finish(TEXT("Art capture timed out")); }
            if (!World.IsValid()) { return Finish(TEXT("The Demo world ended before the art capture completed")); }
            // The stage bodies run on the world's own (dilated) frame time, which the keyframe sequences slow down, so a
            // slow frame moves the bodies, the effects and the sequence clock by the same step.
            const float SimDelta = World.IsValid() ? World->GetDeltaSeconds() : Delta;
            if (bCombatVfx)
            {
                for (const TWeakObjectPtr<ULDHumanoidVisualComponent>& Extra : VfxExtras) { if (auto* Body = Extra.Get()) { Body->Animate(FVector::ZeroVector, SimDelta); } }
                if (ALDObservedProjectileProxy* Flight = VfxEffect.Get(); Flight && VfxFlightAt > 0.)
                {
                    // The chalk keeps flying towards the victim at 900 cm/s on its own authority timeline.
                    const double Elapsed = FPlatformTime::Seconds() - VfxFlightAt;
                    FLDObservedProjectileView Next = VfxFlight;
                    Next.PositionCm = FVector(VfxFlight.PositionCm) + FRotator(0, 45.f, 0).Vector() * 900. * Elapsed;
                    Next.Heading = uint16(45.f / 360.f * 65536.f);
                    Flight->Apply(Next, 100. + Elapsed);
                    Flight->Present(100. + Elapsed);
                }

                if (auto* Body = VfxAttacker.Get())
                {
                    // The streak holds still at the end of the run the stage search cleared.
                    const double Left = bVfxDash ? VfxDashRunCm - FVector::Dist2D(Body->GetOwner()->GetActorLocation(), VfxDashFrom) : 0.;
                    const bool bRunning = Left > 0.;
                    const FVector Dash(bRunning ? 1500.f : 0.f, 0.f, 0.f);
                    if (bRunning) { Body->GetOwner()->AddActorWorldOffset(FVector(FMath::Min(1500. * Delta, Left), 0., 0.)); }
                    Body->Animate(Dash, SimDelta);
                }
                if (auto* Body = VfxVictim.Get()) { Body->Animate(FVector::ZeroVector, SimDelta); }
            }
            if (bCombatVfx && Stage == 2 && ChainJob < ChainJobs.Num() && World.IsValid()) { return TickChain(*World.Get(), Now, SimDelta); }
            if (bCombatVfx && Stage == 2 && SeqIndex < Sequences.Num() && World.IsValid()) { return TickSequence(*World.Get(), Now, SimDelta); }
            if (!PendingImage.IsEmpty())
            {
                if (IFileManager::Get().FileSize(*PendingImage) <= 0) { return true; }
                Check(true, TEXT("Rendered screenshot: ") + FPaths::GetCleanFilename(PendingImage));
                PendingImage.Reset();
                ++Shot;
                NextAt = Now + (bCombatVfx ? 1. : .5);
                return true;
            }
            if (Now < NextAt) { return true; }
            UWorld& Target = *World.Get();
            if (Stage == 0)
            {
                if (!Setup(Target)) { return Finish(); }
                Stage = 1;
                NextAt = Now + 5.;
                return true;
            }
            // Wait for the new art materials to compile so that no screenshot shows the default material.
            if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
            {
                // A just-spawned effect may start compiling a material. Re-create the shot after compilation instead
                // of capturing its already-expired particles.
                if (bCombatVfx) { bVfxPrepared = false; }
                NextAt = Now + 1.;
                return true;
            }
            if (Shot >= Shots.Num()) { return Finish(); }
            auto* Controller = Cast<ALDPlayerController>(Target.GetFirstPlayerController());
            if (!Camera.IsValid() || !Controller || Controller != CaptureController.Get())
            { return Finish(TEXT("No capture camera or original local player controller")); }
            const FArtShot& Current = Shots[Shot];
            Camera->SetActorTransform(Current.bTopDown
                ? FTransform(FRotator(-90., -90., 0.), FVector(Current.Focus.X, Current.Focus.Y, Current.ArmCm))
                : ALDCharacter::DefaultArmCameraPose(Current.Focus, Current.ArmCm));
            if (Controller->GetViewTarget() != Camera.Get() || Stage == 1)
            {
                Controller->SetViewTarget(Camera.Get());
                Stage = 2;
                NextAt = Now + 2.;
                return true;
            }
            if (bCombatVfx && !bVfxPrepared)
            {
                if (!PrepareVfxShot(Current)) { return Finish(); }
                bVfxPrepared = true;
                VfxCaptureFrame = GFrameCounter + 2;
                VfxCaptureAt = Current.CaptureDelay > 0.f ? Now + Current.CaptureDelay
                    : Current.Action == 1 && !Current.bHit ? Now + .17 : Current.Action == 2 ? Now + .11 : Now;
                return true;
            }
            if (bCombatVfx && (GFrameCounter < VfxCaptureFrame || Now < VfxCaptureAt)) { return true; }
            bVfxPrepared = false;
            if (bCombatVfx)
            {
                const auto* Impacts = Target.GetSubsystem<ULDImpactFxSubsystem>();
                if (Current.bDash) { Check(Impacts && Impacts->LiveGhostCount() > 0, TEXT("A dash leaves afterimages")); }
                if (Current.ReactionElement != 0)
                { Check(Impacts && Impacts->LiveLayerCount() > 0 && Impacts->LiveCount() > 0, TEXT("The reaction's layers and particles are still showing at the capture")); }
            }
            PendingImage = Directory / (Current.Name + TEXT(".png"));
            Images.Add(MakeShared<FJsonValueString>(FPaths::GetCleanFilename(PendingImage)));
            FScreenshotRequest::RequestScreenshot(PendingImage, Current.bShowUI, false);
            if (!bCombatVfx) { Stage = 1; }
            return true;
        }
    };
}

void LDDemoTests::StartArtCaptureProbe(UWorld& World)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoArtCapture"))) { return; }
    FString Directory;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoArtCaptureOutput="), Directory)
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Directory), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Artifacts"))))
    {
        UE_LOG(LogLDTest, Error, TEXT("The art capture fixture requires an output directory under Artifacts."));
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    auto Probe = MakeShared<FArtCaptureProbe>();
    Probe->World = &World;
    Probe->bCombatVfx = FParse::Param(FCommandLine::Get(), TEXT("LDDemoCombatVfxCapture"));
    Probe->Directory = FPaths::ConvertRelativePathToFull(Directory);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Delta) { return Probe->Tick(Delta); }));
}
#endif
