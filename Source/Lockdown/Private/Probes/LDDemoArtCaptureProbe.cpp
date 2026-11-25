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
#include "NiagaraSystem.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "Vision/LDWorldViewRelay.h"

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
        // Overdrive: 1 holds a charge through the shot, 2 releases a held charge as a cast.
        uint8 Overdrive = 0;
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
        double VfxReleaseAt = -1.;
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
            UNiagaraSystem* Impact = Effects ? Effects->Impact.Get() : nullptr;
            if (!Check(Impact != nullptr, TEXT("The authored impact effect is available"))) { return false; }
#if WITH_EDITORONLY_DATA
            // Editor builds may still be compiling the system; a cooked package carries it compiled.
            Impact->WaitForCompilationComplete(true, false);
#endif
            if (!Check(Impact->IsReadyToRun(), TEXT("The authored impact effect compiled"))) { return false; }
            UNiagaraSystem* Trail = Effects ? Effects->SwingTrail.Get() : nullptr;
            if (!Check(Trail != nullptr, TEXT("The authored swing trail is available"))) { return false; }
#if WITH_EDITORONLY_DATA
            Trail->WaitForCompilationComplete(true, false);
#endif
            if (!Check(Trail->IsReadyToRun(), TEXT("The authored swing trail compiled"))) { return false; }
            UNiagaraSystem* Charge = Effects ? Effects->CastCharge.Get() : nullptr;
            if (!Check(Charge != nullptr, TEXT("The authored cast charge is available"))) { return false; }
#if WITH_EDITORONLY_DATA
            Charge->WaitForCompilationComplete(true, false);
#endif
            if (!Check(Charge->IsReadyToRun(), TEXT("The authored cast charge compiled"))) { return false; }
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
            VfxAttacker->SetCue(2, .2f, .4f);
            VfxAttacker->Animate(FVector::ZeroVector, .04f);
            TInlineComponentArray<UNiagaraComponent*> EffectsOnAttacker;
            VfxAttacker->GetOwner()->GetComponents(EffectsOnAttacker);
            UNiagaraComponent* CastEffect = nullptr;
            for (UNiagaraComponent* Effect : EffectsOnAttacker)
            {
                if (Effect && Effect->GetAsset() == Charge) { CastEffect = Effect; break; }
            }
            Check(CastEffect && CastEffect->IsActive(), TEXT("A visible cast starts its authored hand effect"));
            if (!Check(CastEffect && CastEffect->GetComponentScale().Equals(FVector::OneVector, .001f),
                TEXT("The cast effect uses world scale independently of the hand bone"))) { return false; }
            VfxAttacker->SetVisibility(false, true);
            Check(CastEffect && !CastEffect->IsActive(), TEXT("Hiding the caster immediately clears its cast effect"));
            VfxAttacker->SetVisibility(true, true);
            VfxAttacker->SetCue(0);
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
                {TEXT("VFX-OverdriveHit"), Focus, LDCamera::DefaultArmLength, false, 1, ELDObservedHitLook::None, LDObservation::Overdriven, true},
                {TEXT("VFX-OverdriveCharge"), Focus, LDCamera::DefaultArmLength, false, 0, ELDObservedHitLook::None, 0, false, .08f, false,
                    0, false, false, ELDObservedProjectileLook::Chalk, 0.f, .45f, 1},
                {TEXT("VFX-OverdriveRelease"), Focus, LDCamera::DefaultArmLength, false, 0, ELDObservedHitLook::None, 0, false, .08f, false,
                    uint8(ELDDemoAbilityId::ActiveZap), false, false, ELDObservedProjectileLook::Chalk, 0.f, .5f, 2},
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
                if (!Check(Name == TEXT("chain") || Name == TEXT("hit") || Name == TEXT("swing") || Name == TEXT("field") || Name == TEXT("water") || Name == TEXT("fire") || Name == TEXT("chill") || Name == TEXT("feel"),
                    TEXT("Proof category known: ") + Name)) { return false; }
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
                if (Look == TEXT("water") || Look == TEXT("fire") || Look == TEXT("chill")) { continue; }
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
                    ChainPhase = 4;
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
            if (ChainPhase == 4)
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
            if (Age >= ChainNextWave * 1.)
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
            for (int32 Index = 0; Index < PerfVictims.Num(); ++Index)
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
            ChainPerf.Add(MakeShared<FJsonValueObject>(Run));
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
            Attacker->SetCharging(Current.Overdrive != 0);
            Attacker->SetOverdriveCast(false);
            VfxReleaseAt = Current.Overdrive == 2 ? FPlatformTime::Seconds() + .4 : -1.;
            Victim->Animate(FVector::ZeroVector, .5f);
            Attacker->SetCue(Current.Action, Current.Action ? (Current.bHit ? .36f : .22f) : -1.f, .4f);
            Attacker->Animate(FVector::ZeroVector, 0.f);
            if (Current.bHit)
            {
                Victim->SetHitReaction(Current.HitAgeSeconds, Current.HitLook, Current.HitFlags);
                Victim->Animate(FVector::ZeroVector, 0.f);
                LDHitVfx::EBurst Element;
                FLinearColor ElementTint;
                const bool bSlice = LDSliceVfx::ShowsHit(Current.HitLook, Current.HitFlags);
                // A slice hit spawns no burst system; an electric one the Niagara Chain of its hop.
                const int32 Expected = bSlice ? (Current.HitLook == ELDObservedHitLook::Electric ? 1 : 0) : 1 + (LDHitVfx::HitBurstOf(Current.HitLook, Element, ElementTint) ? 1 : 0)
                    + ((Current.HitFlags & LDObservation::Overdriven) ? 1 : 0);
                if (!Check(Impacts && Impacts->LiveCount() == Expected,
                    TEXT("A fresh shown hit creates one impact effect, plus its element and overdrive particles; a slice hit none"))) { return false; }
                Check(Impacts->LiveLayerCount() == LDHitVfx::ForHit(Current.HitLook, Current.HitFlags, 1.f, Impacts->LayerTuning()).Layers.Num(),
                    TEXT("A fresh shown hit spawns its shaped layers"));
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
                const int32 Before = Impacts ? Impacts->LiveLayerCount() : 0;
                Victim->SetReaction(uint8(ELDCombatReaction::Overload), Current.ReactionElement, .03f, 100. + Shot);
                Victim->Animate(FVector::ZeroVector, 0.f);
                Check(Impacts && Impacts->LiveLayerCount() - Before == LDHitVfx::ForReaction(Current.ReactionElement, 1.f, Impacts->LayerTuning()).Layers.Num(),
                    TEXT("A fresh element reaction flashes its layers once"));
                const int32 After = Impacts ? Impacts->LiveLayerCount() : 0;
                Victim->SetReaction(uint8(ELDCombatReaction::Overload), Current.ReactionElement, .05f, 100. + Shot);
                Victim->Animate(FVector::ZeroVector, 0.f);
                Check(Impacts && Impacts->LiveLayerCount() == After, TEXT("The same reaction seen again adds nothing"));
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
                Check(Impacts->LiveLayerCount() == LDHitVfx::ForKill(1.f, Impacts->LayerTuning()).Layers.Num(), TEXT("A visible fall cracks the ground"));
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
                if (auto* Body = VfxAttacker.Get(); Body && VfxReleaseAt > 0. && FPlatformTime::Seconds() >= VfxReleaseAt)
                {
                    // The held charge ends as the authority's overdrive cast begins (public bOverdriveCast) and releases.
                    VfxReleaseAt = -1.;
                    Body->SetCharging(false);
                    Body->SetOverdriveCast(true);
                    Body->SetCue(2, .1f, .1f);
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
                if (Current.Overdrive == 2)
                {
                    Check(Impacts && Impacts->LiveLayerCount() >= LDHitVfx::ForOverdriveRelease(1.f, Impacts->LayerTuning()).Layers.Num(),
                        TEXT("A held charge released as a cast shows the overdrive shockwave"));
                }
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
