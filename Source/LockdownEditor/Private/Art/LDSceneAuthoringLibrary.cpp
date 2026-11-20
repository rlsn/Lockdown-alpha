#include "LDSceneAuthoringLibrary.h"
#include "AssetCompilingManager.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavAreas/NavArea_Default.h"
#include "NavLinkComponent.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Region/LDMapRegistry.h"
#include "World/LDNativeGround.h"
#include "World/LDSceneTags.h"
#include "Settings/LevelEditorMiscSettings.h"
#include "UObject/UnrealType.h"
#include "Misc/ScopeExit.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "World/LDCampLifecycle.h"
#include "World/LDSpawnPlacement.h"
#include "Vision/LDVisionDirector.h"
#include "Framework/LDPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "UI/LDUIRoot.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
const FName LedgeLinksTag(TEXT("LDDerivedLedgeLinks"));

bool BakeLedgeLinks(UWorld& World, UNavigationSystemV1& Nav, const FBox& Envelope,
    const UCharacterMovementComponent& Movement, FString& Error)
{
    if (!Movement.bCanWalkOffLedges) { return true; }
    TArray<FNavigationLink> Links;
    TSet<FString> Seen;
    const auto Query=LDNativeGround::Query(World);
    for (TActorIterator<ARecastNavMesh> It(&World); It; ++It)
    {
        FRecastDebugGeometry Geometry;
        Geometry.bGatherNavMeshEdges=true;
        if (!It->GetDebugGeometryForTile(Geometry,FNavTileRef()))
        { Error=TEXT("Could not read current native navigation edges."); return false; }
        const double Radius=It->AgentRadius, HalfHeight=It->AgentHeight*.5;
        const double Cell=It->GetCellSize(ENavigationDataResolution::Default);
        const auto Capsule=FCollisionShape::MakeCapsule(Radius,HalfHeight);
        const double FloorGap=(UCharacterMovementComponent::MIN_FLOOR_DIST+UCharacterMovementComponent::MAX_FLOOR_DIST)*.5;
        for (int32 Edge=0; Edge+1<Geometry.NavMeshEdges.Num(); Edge+=2)
        {
            const FVector A=Geometry.NavMeshEdges[Edge], B=Geometry.NavMeshEdges[Edge+1];
            const FVector Along=(B-A).GetSafeNormal2D();
            if (Along.IsNearlyZero()) { continue; }
            const FVector Normal(-Along.Y,Along.X,0);
            const int32 Samples=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(A,B)/(Radius*2)));
            for (int32 Sample=0; Sample<Samples; ++Sample)
            {
                const FVector Mid=FMath::Lerp(A,B,(Sample+.5)/Samples);
                for (double Side : {-1.,1.})
                {
                    const FVector Outward=Normal*Side;
                    FVector From=Mid-Outward*Cell, To=Mid+Outward*(Radius*2+Cell*2);
                    FNavLocation FromNav, ToNav;
                    if (!Nav.ProjectPointToNavigation(From,FromNav,FVector(1,1,Movement.MaxStepHeight),*It)
                        || FVector::DistSquared2D(From,FromNav.Location)>1) { continue; }
                    FHitResult Start, Landing;
                    if (!LDNativeGround::Trace(World,Start,From+FVector(0,0,Movement.MaxStepHeight),
                        From-FVector(0,0,Movement.MaxStepHeight+Cell*2),Query)
                        || Start.ImpactNormal.Z<Movement.GetWalkableFloorZ()) { continue; }
                    From.Z=Start.ImpactPoint.Z;
                    To.Z=From.Z;
                    if (!LDNativeGround::Trace(World,Landing,To+FVector(0,0,Movement.MaxStepHeight),
                        FVector(To.X,To.Y,Envelope.Min.Z),Query)
                        || Landing.ImpactNormal.Z<Movement.GetWalkableFloorZ()
                        || From.Z-Landing.ImpactPoint.Z<=Movement.MaxStepHeight) { continue; }
                    To.Z=Landing.ImpactPoint.Z;
                    if (!Nav.ProjectPointToNavigation(To,ToNav,FVector(1,1,Movement.MaxStepHeight),*It)
                        || FVector::DistSquared2D(To,ToNav.Location)>1
                        || FMath::Abs(ToNav.Location.Z-To.Z)>Movement.MaxStepHeight) { continue; }
                    const FVector StartCentre=From+FVector(0,0,HalfHeight+FloorGap);
                    const FVector EndCentre=To+FVector(0,0,HalfHeight+FloorGap);
                    const FVector OverEdge(EndCentre.X,EndCentre.Y,StartCentre.Z);
                    FHitResult Block;
                    // Walk clear of the physical lip, then fall. The whole capsule must fit, including below an overhang.
                    if (LDNativeGround::Overlaps(World,StartCentre,Capsule,Query)
                        || LDNativeGround::Overlaps(World,EndCentre,Capsule,Query)
                        || LDNativeGround::Sweep(World,Block,StartCentre,OverEdge,Capsule,Query)
                        || LDNativeGround::Sweep(World,Block,OverEdge,EndCentre,Capsule,Query)) { continue; }
                    const FString Key=FString::Printf(TEXT("%lld,%lld,%lld:%lld,%lld,%lld"),
                        FMath::RoundToInt64(From.X),FMath::RoundToInt64(From.Y),FMath::RoundToInt64(From.Z),
                        FMath::RoundToInt64(To.X),FMath::RoundToInt64(To.Y),FMath::RoundToInt64(To.Z));
                    if (Seen.Contains(Key)) { continue; }
                    Seen.Add(Key);
                    FNavigationLink Link(FromNav.Location,ToNav.Location);
                    Link.Direction=ENavLinkDirection::LeftToRight;
                    Link.LeftProjectHeight=Link.MaxFallDownLength=0;
                    Link.SnapRadius=Cell*2;
                    Link.bUseSnapHeight=true;
                    Link.SnapHeight=Movement.MaxStepHeight;
                    Link.SetAreaClass(UNavArea_Default::StaticClass());
                    Links.Add(Link);
                }
            }
        }
    }
    if (Links.IsEmpty()) { return true; }
    auto* Actor=World.SpawnActor<AActor>();
    if (!Actor) { Error=TEXT("Could not create derived navigation link data."); return false; }
    Actor->Tags.Add(LedgeLinksTag);
    Actor->SetActorLabel(TEXT("导航派生数据_单向下落_烘焙自动更新"));
    Actor->SetFolderPath(TEXT("Navigation/Derived"));
    Actor->SetActorHiddenInGame(true);
    auto* Component=NewObject<UNavLinkComponent>(Actor,NAME_None,RF_Transactional);
    Actor->SetRootComponent(Component);
    Actor->AddInstanceComponent(Component);
    Component->SetMobility(EComponentMobility::Static);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->Links=MoveTemp(Links);
    Component->RegisterComponentWithWorld(&World);
    Nav.UpdateActorInNavOctree(*Actor);
    Nav.Build();
    for (TActorIterator<ARecastNavMesh> It(&World); It; ++It) { It->EnsureBuildCompletion(); }
    return true;
}
}

bool ULDSceneAuthoringLibrary::BakeNavigation(UWorld* World, FString& Error)
{
    if (!World || World->IsGameWorld()) { Error = TEXT("Bake navigation in the editor world after stopping Play."); return false; }
    LDNativeGround::RetireLegacyCliffGuards(*World);
    LDSceneTags::PrepareEditorNavigation(*World);
    TArray<AActor*> PreviousLinks;
    for (TActorIterator<AActor> It(World); It; ++It)
    { if (It->ActorHasTag(LedgeLinksTag)) { PreviousLinks.Add(*It); } }
    for (auto* Actor : PreviousLinks) { World->EditorDestroyActor(Actor,true); }
    auto* Settings = GetMutableDefault<ULevelEditorMiscSettings>();
    Settings->bNavigationAutoUpdate = true;
    Settings->SaveConfig();
    auto* Nav = Cast<UNavigationSystemV1>(World->GetNavigationSystem());
    if (!Nav) { Error = TEXT("The editor world has no native navigation system."); return false; }
    UNavigationSystemV1::SetNavigationAutoUpdateEnabled(true, Nav);
    // PIE preparation temporarily locks editor updates. This explicit synchronous editor bake finishes before duplication.
    const bool bRestorePIELock = Nav->IsNavigationBuildingLocked(ENavigationBuildLock::NoUpdateInPIE);
    ON_SCOPE_EXIT { if (bRestorePIELock) { Nav->AddNavigationBuildLock(ENavigationBuildLock::NoUpdateInPIE); } };
    Nav->RemoveNavigationBuildLock(ENavigationBuildLock::NoUpdateInPIE, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    FBox NavigationEnvelope(ForceInit);
    for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
    { NavigationEnvelope += It->GetComponentsBoundingBox(true); }
    if (!NavigationEnvelope.IsValid) { Error = TEXT("The editor world has no navigation bounds."); return false; }
    const auto* Movement = GetDefault<ALDCharacter>()->GetCharacterMovement();
    for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
    {
        auto* Mode = FindFProperty<FEnumProperty>(ARecastNavMesh::StaticClass(), TEXT("RuntimeGeneration"));
        if (!Mode) { Error=TEXT("Recast runtime generation setting is unavailable."); return false; }
        It->Modify();
        // Tiles are baked in world coordinates. A designer-translated Recast data actor offsets them again on PIE load.
        // This actor is derived navigation, not authored ground or a navigation bounds volume.
        It->SetActorTransform(FTransform::Identity);
        Mode->GetUnderlyingProperty()->SetIntPropertyValue(Mode->ContainerPtrToValuePtr<void>(*It), uint64(ERuntimeGenerationType::Static));
        for (uint8 Resolution=0; Resolution<uint8(ENavigationDataResolution::MAX); ++Resolution)
        { It->SetAgentMaxStepHeight(ENavigationDataResolution(Resolution), Movement->MaxStepHeight); }
        It->AgentMaxSlope=Movement->GetWalkableFloorAngle();
        // No experimental jump arcs; optional drop links obey the native walking policy.
        It->bGenerateNavLinks=false;
        It->PostEditChange();
    }
    FlushAsyncLoading();
    FAssetCompilingManager::Get().FinishAllCompilation();
    Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    if (Nav->IsNavigationBuildingLocked()) { Error=TEXT("Navigation build has an initial or custom lock; saved navigation was not refreshed."); return false; }
    Nav->Build();
    for (TActorIterator<ARecastNavMesh> It(World); It; ++It) { It->EnsureBuildCompletion(); }
    if (!BakeLedgeLinks(*World,*Nav,NavigationEnvelope,*Movement,Error)) { return false; }
    if (!Nav->IsNavigationBuilt(World->GetWorldSettings())) { Error = TEXT("Native navigation build did not complete."); return false; }
    World->MarkPackageDirty();
    Error.Reset();
    return true;
}

bool ULDSceneAuthoringLibrary::ValidateGround(UWorld* World, FString& Error)
{
    auto* Registry = World ? World->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Registry) { Error = TEXT("The Demo editor world has no map registry."); return false; }
    return Registry->Rebuild(Error);
}

FString ULDSceneAuthoringLibrary::GroundValidationError(UWorld* World)
{
    FString Error;
    if (!ValidateGround(World, Error) && Error.IsEmpty()) { return TEXT("Ground validation failed without a diagnostic."); }
    return Error;
}

FString ULDSceneAuthoringLibrary::NavigationBakeError(UWorld* World)
{
    FString Error;
    if (!BakeNavigation(World, Error) && Error.IsEmpty()) { return TEXT("Navigation bake failed without a diagnostic."); }
    return Error;
}

FString ULDSceneAuthoringLibrary::GameplayPlacementAudit(UWorld* World, bool bIncludePaths)
{
    auto Result=MakeShared<FJsonObject>();
    const auto Finish=[&]() { FString Text; auto Writer=TJsonWriterFactory<>::Create(&Text); FJsonSerializer::Serialize(Result,Writer); return Text; };
    Result->SetBoolField(TEXT("ready"),false);
    auto* Registry=World ? World->GetSubsystem<ULDMapRegistry>() : nullptr;
    FString Error;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FLDCampRoster Roster;
    FCollisionShape Capsule;
    if (!Registry || !Registry->Rebuild(Error) || !LDDemoEntry::LoadRuntimeContent(Content,Error)
        || !ULDMapRegistry::ContentCapsule(*Content,Capsule,Error)
        || !LDCamp::BuildRoster(Content->Get(),*Registry->GetGeometry(),Roster,Error))
    { Result->SetStringField(TEXT("error"),Error.IsEmpty()?TEXT("No Demo map registry"):Error); return Finish(); }
    Result->SetBoolField(TEXT("ready"),true);
    Result->SetObjectField(TEXT("registry"),Registry->Report());
    Result->SetNumberField(TEXT("camp_count"),Roster.Camps.Num());
    Result->SetNumberField(TEXT("capsule_radius_cm"),Capsule.GetCapsuleRadius());
    Result->SetNumberField(TEXT("capsule_half_height_cm"),Capsule.GetCapsuleHalfHeight());
    TArray<TSharedPtr<FJsonValue>> Rows;
    const auto* Capture=Registry->GetGeometry()->Anchors.FindByPredicate([](const auto& Anchor) { return Anchor.Kind==ELDDemoWorldAnchorKind::Capture; });
    const auto* FieldHub=Capture ? Registry->Resolve(FName(*Capture->Id),Registry->Generation) : nullptr;
    FLDLegalGround FieldGround;
    ELDWorldQueryFailure HubFailure;
    const bool bFieldHub=bIncludePaths && FieldHub && Registry->QueryGround(Registry->Revision(),FieldHub->Position,Capsule,FieldHub->Surface,FieldGround,HubFailure,Error);
    int32 Clear=0;
    for (const auto& Camp : Roster.Camps)
    {
        const auto* Centre=Registry->Resolve(Camp.CampId,Registry->Generation);
        FLDLegalGround CentreGround;
        ELDWorldQueryFailure Failure;
        const bool bCentre=bIncludePaths && Centre && Registry->QueryGround(Registry->Revision(),Centre->Position,Capsule,Camp.SurfaceId,CentreGround,Failure,Error);
        for (const auto& Member : Camp.Members)
        {
            auto Row=MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("camp"),Camp.CampId.ToString());Row->SetStringField(TEXT("member"),Member.MemberId.ToString());
            Row->SetStringField(TEXT("prototype"),Member.DefinitionId.ToString());
            // The member's species is drawn from its tier pool on each whole-group spawn; report the pool, not one species.
            TArray<TSharedPtr<FJsonValue>> Species;
            if (Camp.Pools.IsValidIndex(Member.Pool))
            {
                Row->SetNumberField(TEXT("tier"),uint8(Camp.Pools[Member.Pool].Tier));
                for (const FName Id : Camp.Pools[Member.Pool].Species) { Species.Add(MakeShared<FJsonValueString>(Id.ToString())); }
            }
            Row->SetArrayField(TEXT("species_pool"),Species);
            const auto Point=[](const FVector& P) { return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)}; };
            Row->SetArrayField(TEXT("home_cm"),Point(Member.HomeCm));
            FLDLegalGround Ground;
            Error.Reset();
            const bool bGround=Registry->QueryGround(Registry->Revision(),Member.HomeCm,Capsule,Camp.SurfaceId,Ground,Failure,Error);
            Row->SetBoolField(TEXT("grounded"),bGround);Row->SetStringField(TEXT("ground_error"),Error);
            const bool bClear=bGround && !LDSpawnPlacement::OverlapsStatic(*World,LDSpawnPlacement::StandingOrigin(Ground),Capsule);
            Row->SetBoolField(TEXT("clear"),bClear);Clear+=bClear;
            if (bGround)
            {
                Row->SetArrayField(TEXT("standing_origin_cm"),Point(LDSpawnPlacement::StandingOrigin(Ground)));
                Row->SetStringField(TEXT("support"),GetPathNameSafe(Ground.Floor.Get()));
            }
            if (bIncludePaths)
            {
                FLDCompletePath Path;
                Error.Reset();
                const bool bPath=bCentre && bGround && Registry->QueryCompletePath(Registry->Revision(),CentreGround,Ground,Capsule,Path,Failure,Error);
                Row->SetBoolField(TEXT("centre_path"),bPath);Row->SetStringField(TEXT("path_error"),Error);
                Error.Reset();
                const bool bFieldPath=bFieldHub && bGround && Registry->QueryCompletePath(Registry->Revision(),FieldGround,Ground,Capsule,Path,Failure,Error);
                Row->SetBoolField(TEXT("field_path"),bFieldPath);Row->SetStringField(TEXT("field_path_error"),Error);
            }
            Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
    }
    Result->SetNumberField(TEXT("member_count"),Rows.Num());Result->SetNumberField(TEXT("clear_member_count"),Clear);
    Result->SetArrayField(TEXT("members"),Rows);
    if (bIncludePaths)
    {
        Error.Reset();Result->SetBoolField(TEXT("field_spawns_reachable"),Registry->ValidateFieldSpawnPaths(false,Error));Result->SetStringField(TEXT("field_spawns_error"),Error);
        Error.Reset();Result->SetBoolField(TEXT("final_spawns_reachable"),Registry->ValidateFieldSpawnPaths(true,Error));Result->SetStringField(TEXT("final_spawns_error"),Error);
    }
    return Finish();
}

FString ULDSceneAuthoringLibrary::GameplayVisibilityAudit(UWorld* World, bool bSampleNavigation)
{
    auto Result=MakeShared<FJsonObject>();
    const auto Finish=[&]() { FString Text; FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text)); return Text; };
    auto* Registry=World?World->GetSubsystem<ULDMapRegistry>():nullptr;
    FString Error;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FCollisionShape Capsule;
    FLDSightGeometry Sight;
    if (!Registry || !(Registry->Enabled() || Registry->Rebuild(Error)) || !LDDemoEntry::LoadRuntimeContent(Content,Error)
        || !ULDMapRegistry::ContentCapsule(*Content,Capsule,Error)
        || !Sight.Build(*Registry->GetGeometry(),Registry->Revision(),Content->Get().Parameters.VisionRadiusCm,Error,World))
    { Result->SetBoolField(TEXT("ready"),false);Result->SetStringField(TEXT("error"),Error);return Finish(); }
    Result->SetBoolField(TEXT("ready"),true);
    Result->SetNumberField(TEXT("cell_width_cm"),(Sight.MaxCm().X-Sight.MinCm().X)/LDObservation::CoverageSide);
    Result->SetNumberField(TEXT("cell_height_cm"),(Sight.MaxCm().Y-Sight.MinCm().Y)/LDObservation::CoverageSide);
    Result->SetObjectField(TEXT("registry"),Registry->Report());
    TArray<TSharedPtr<FJsonValue>> Rows;
    auto Sample=[&](const FString& Id,const FVector& Foot,FName Surface)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("id"),Id);Row->SetStringField(TEXT("foot"),Foot.ToString());
        Row->SetNumberField(TEXT("top_tier"),Registry->GetTerrain().Tier(FVector2D(Foot)));
        double Top;FVector Normal;Registry->GetTerrain().Ground(FVector2D(Foot),Top,Normal);
        if (FMath::IsFinite(Top)) {Row->SetNumberField(TEXT("top_ground_z"),Top);}
        Row->SetNumberField(TEXT("self_sight"),int32(Sight.Trace(Foot,Foot)));
        TArray<uint8> Bits;Sight.Coverage(Foot,Bits);
        int32 Covered=0,Near=0;
        for(int32 I=0;I<LDObservation::CoverageCells;++I)
        {
            const bool bSet=(Bits[I/8]&(1<<(I%8)))!=0;
            Covered+=bSet;
            if(bSet&&FVector2D::DistSquared(Sight.CellCentre(I),FVector2D(Foot))<250.*250.){++Near;}
        }
        Row->SetNumberField(TEXT("covered_cells"),Covered);Row->SetNumberField(TEXT("nearby_covered_cells"),Near);
        TArray<TSharedPtr<FJsonValue>> Neighbours;
        for(int32 I=0;I<8;++I)
        {
            const double Angle=I*PI/4.;
            FLDLegalGround Ground;ELDWorldQueryFailure Failure;FString Why;
            const FVector At=Foot+FVector(FMath::Cos(Angle)*150.,FMath::Sin(Angle)*150.,0);
            if(!Registry->QueryGround(Registry->Revision(),At,Capsule,Surface,Ground,Failure,Why)){continue;}
            auto N=MakeShared<FJsonObject>();N->SetStringField(TEXT("foot"),Ground.FootCm.ToString());
            N->SetStringField(TEXT("support"),GetPathNameSafe(Ground.Floor.Get()));
            N->SetNumberField(TEXT("sight"),int32(Sight.Trace(Foot,LDSpawnPlacement::StandingOrigin(Ground)-FVector(0,0,Capsule.GetCapsuleHalfHeight()))));
            Neighbours.Add(MakeShared<FJsonValueObject>(N));
        }
        Row->SetArrayField(TEXT("neighbours"),Neighbours);Rows.Add(MakeShared<FJsonValueObject>(Row));
    };
    for(const auto& Site:Registry->Sites())
    {
        FLDLegalGround Ground;ELDWorldQueryFailure Failure;
        if(Registry->QueryGround(Registry->Revision(),Site.Position,Capsule,Site.Surface,Ground,Failure,Error))
        { Sample(Site.Id.ToString(),LDSpawnPlacement::StandingOrigin(Ground)-FVector(0,0,Capsule.GetCapsuleHalfHeight()),Site.Surface); }
    }
    FLDCampRoster Roster;
    if(LDCamp::BuildRoster(Content->Get(),*Registry->GetGeometry(),Roster,Error))
    {
        for(const auto& Camp:Roster.Camps)
        {
            for(const auto& Member:Camp.Members)
            {
                FLDLegalGround Ground;ELDWorldQueryFailure Failure;
                bool bPlaced=Registry->QueryGround(Registry->Revision(),Member.HomeCm,Capsule,Camp.SurfaceId,Ground,Failure,Error)
                    &&!LDSpawnPlacement::OverlapsStatic(*World,LDSpawnPlacement::StandingOrigin(Ground),Capsule);
                for(int32 I=0;!bPlaced&&I<Registry->GetGeometry()->SpawnOffsetsCm.Num();++I)
                {
                    FVector Offset;Registry->GetGeometry()->SpawnOffsetsCm[I].ToVector(Offset,Error);
                    bPlaced=Registry->QueryGround(Registry->Revision(),Member.HomeCm+Offset,Capsule,Camp.SurfaceId,Ground,Failure,Error)
                        &&!LDSpawnPlacement::OverlapsStatic(*World,LDSpawnPlacement::StandingOrigin(Ground),Capsule);
                }
                if(bPlaced)
                {Sample(TEXT("member:")+Camp.CampId.ToString()+TEXT("/")+Member.MemberId.ToString(),LDSpawnPlacement::StandingOrigin(Ground)-FVector(0,0,Capsule.GetCapsuleHalfHeight()),Camp.SurfaceId);}
            }
        }
    }
    int32 NavigationSamples=0;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if(bSampleNavigation&&Nav)
    {
        const FName Surface(*Registry->GetGeometry()->Surfaces[0].Id);
        for(double Y=Sight.MinCm().Y+200;Y<Sight.MaxCm().Y;Y+=400)
        {
            for(double X=Sight.MinCm().X+200;X<Sight.MaxCm().X;X+=400)
            {
                FNavLocation At;
                if(!Nav->ProjectPointToNavigation(FVector(X,Y,0),At,FVector(40,40,10000))){continue;}
                FLDLegalGround Ground;ELDWorldQueryFailure Failure;
                if(!Registry->QueryGround(Registry->Revision(),At.Location,Capsule,Surface,Ground,Failure,Error)
                    ||LDSpawnPlacement::OverlapsStatic(*World,LDSpawnPlacement::StandingOrigin(Ground),Capsule)){continue;}
                Sample(FString::Printf(TEXT("nav:%d"),NavigationSamples++),LDSpawnPlacement::StandingOrigin(Ground)-FVector(0,0,Capsule.GetCapsuleHalfHeight()),Surface);
            }
        }
    }
    Result->SetNumberField(TEXT("navigation_sample_count"),NavigationSamples);
    Result->SetArrayField(TEXT("samples"),Rows);
    TArray<TSharedPtr<FJsonValue>> Cameras;
    for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
    {
        auto* PC=Cast<ALDPlayerController>(It->Get());if(!PC||!PC->GetLocalPlayer()){continue;}
        auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("pawn"),GetPathNameSafe(PC->GetPawn()));
        C->SetStringField(TEXT("view_target"),GetPathNameSafe(PC->GetViewTarget()));
        C->SetBoolField(TEXT("can_camera"),PC->CanCameraCommand());C->SetBoolField(TEXT("can_world"),PC->CanWorldCommand());
        C->SetBoolField(TEXT("input_suppressed"),PC->IsWorldInputSuppressed());
        C->SetNumberField(TEXT("input_layer"),PC->RootUI?int32(PC->RootUI->TopInputLayer()):0);
        if(PC->PlayerCameraManager){C->SetStringField(TEXT("camera"),PC->PlayerCameraManager->GetCameraLocation().ToString());}
        Cameras.Add(MakeShared<FJsonValueObject>(C));
    }
    Result->SetArrayField(TEXT("cameras"),Cameras);
    return Finish();
}
