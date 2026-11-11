#include "Tests/LDDemoArtCaptureProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Art/LDArtCatalog.h"
#include "Art/LDArtPlacement.h"
#include "Art/LDArtSubsystem.h"
#include "Art/LDDecorationSet.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/LDCharacter.h"
#include "Character/LDCameraSettings.h"
#include "Character/LDHumanoidVisualComponent.h"
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
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDMapGeometry.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace
{
    struct FArtShot
    {
        FString Name;
        FVector Focus = FVector::ZeroVector;
        float ArmCm = LDCamera::DefaultArmLength;
        // Straight down from ArmCm above Focus, +X to the right, +Y down (the editor top view).
        bool bTopDown = false;
    };

    // Floor points a body can stand on, with the camera arm lengths the player can zoom between.
    constexpr double ArtSweepStepCm = 100.;
    const float ArtSweepArms[] = { LDCamera::MinArmLength, LDCamera::DefaultArmLength, LDCamera::MaxArmLength };
    // Heights on a standing body that must stay visible: upper chest and top of the head.
    const double ArtSweepBodyCm[] = { 130., 170. };

    struct FArtCaptureProbe
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<ACameraActor> Camera;
        TWeakObjectPtr<ALDPlayerController> CaptureController;
        FString Directory, PendingImage;
        TArray<TSharedPtr<FJsonValue>> Checks, Images, Bodies, Decorations, Occluders, OccludedPoints;
        TArray<FArtShot> Shots;
        double StartedAt = FPlatformTime::Seconds(), NextAt = FPlatformTime::Seconds() + 3.;
        int32 Stage = 0, Shot = 0;
        bool Failed = false;

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
            Report->SetStringField(TEXT("scope"), TEXT("DEV art fixture in the Demo map Lobby: catalog bodies posed per clip and the decorated field rendered from the game camera angle; decoration collision, sight-trace and navigation checks on the Listen Host. Not a gameplay, combat wiring or network acceptance."));
            Report->SetArrayField(TEXT("checks"), Checks);
            Report->SetArrayField(TEXT("screenshots"), Images);
            Report->SetArrayField(TEXT("bodies"), Bodies);
            Report->SetArrayField(TEXT("decorations"), Decorations);
            Report->SetArrayField(TEXT("occluders"), Occluders);
            Report->SetArrayField(TEXT("occluded_points"), OccludedPoints);
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
            const auto* Subsystem = Target.GetSubsystem<ULDArtWorldSubsystem>();
            const ALDDecorationSet* Set = Subsystem ? Subsystem->GetDecorations() : nullptr;
            const FLDArtResolved* Art = ULDArtSubsystem::Get(&Target);
            const auto* Instance = Target.GetGameInstance();
            const auto* ArtSubsystem = Instance ? Instance->GetSubsystem<ULDArtSubsystem>() : nullptr;
            if (!Check(Set && Art && ArtSubsystem && ArtSubsystem->GetCatalog(), TEXT("The resolved catalog placed the decoration set in the Demo map"))) { return; }
            Check(Set->GetInstanceCount() == ArtSubsystem->GetCatalog()->Decorations.Num(), FString::Printf(TEXT("All %d catalog decorations are placed"), ArtSubsystem->GetCatalog()->Decorations.Num()));
            Check(Set->IsPresentationOnly(), TEXT("Every decoration part has no collision, no overlap and no navigation relevance"));
            FString Error;
            Check(LDArt::CheckPlacementsAgainstDemoWorld(*ArtSubsystem->GetCatalog(), Error), TEXT("Decorations respect the D02 placement zones ") + Error);
            const ECollisionChannel Channels[] = { ECC_GameTraceChannel2, ECC_GameTraceChannel1, ECC_Pawn, ECC_Visibility, ECC_Camera };
            auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&Target);
            int32 Traces = 0, TraceHits = 0, NavChecks = 0, NavMisses = 0;
            for (const UInstancedStaticMeshComponent* Part : Set->GetParts())
            {
                const FBox Local = Part->GetStaticMesh()->GetBoundingBox();
                for (int32 Index = 0; Index < Part->GetInstanceCount(); ++Index)
                {
                    FTransform Placement;
                    Part->GetInstanceTransform(Index, Placement, true);
                    const FBox Box = Local.TransformBy(Placement);
                    const FVector Centre = Box.GetCenter(), Extent = Box.GetExtent() + FVector(100.);
                    for (const ECollisionChannel Channel : Channels)
                    {
                        for (const FVector& Axis : {FVector(0, 0, Extent.Z), FVector(Extent.X, 0, 0), FVector(0, Extent.Y, 0)})
                        {
                            TArray<FHitResult> Hits;
                            Target.LineTraceMultiByChannel(Hits, Centre + Axis, Centre - Axis, Channel, FCollisionQueryParams(SCENE_QUERY_STAT(LDArtCapture), true));
                            ++Traces;
                            TraceHits += Hits.FilterByPredicate([Set](const FHitResult& Hit) { return Hit.GetActor() == Set; }).Num();
                        }
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

        bool Setup(UWorld& Target)
        {
            const FLDArtResolved* Art = ULDArtSubsystem::Get(&Target);
            if (!Check(Art != nullptr, TEXT("The art catalog resolved at startup"))) { return false; }
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
            if (Now - StartedAt > 600.) { return Finish(TEXT("Art capture timed out")); }
            if (!World.IsValid()) { return Finish(TEXT("The Demo world ended before the art capture completed")); }
            if (!PendingImage.IsEmpty())
            {
                if (IFileManager::Get().FileSize(*PendingImage) <= 0) { return true; }
                Check(true, TEXT("Rendered screenshot: ") + FPaths::GetCleanFilename(PendingImage));
                PendingImage.Reset();
                ++Shot;
                NextAt = Now + .5;
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
            if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { NextAt = Now + 1.; return true; }
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
            PendingImage = Directory / (Current.Name + TEXT(".png"));
            Images.Add(MakeShared<FJsonValueString>(FPaths::GetCleanFilename(PendingImage)));
            FScreenshotRequest::RequestScreenshot(PendingImage, false, false);
            Stage = 1;
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
    Probe->Directory = FPaths::ConvertRelativePathToFull(Directory);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Delta) { return Probe->Tick(Delta); }));
}
#endif
