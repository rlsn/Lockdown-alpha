#include "LDB90BootstrapCommandlet.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDB90Preview.h"
#include "Region/LDRegionNavBounds.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/BrushComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Materials/Material.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "Builders/CubeBuilder.h"
#include "Model.h"
#include "FileHelpers.h"
#include "AssetCompilingManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
const FString B90AssetRoot = TEXT("/Game/Lockdown/Generated/V09/");
const FString MapPath = B90AssetRoot + TEXT("L_V09Full");
const FString DefinitionPath = B90AssetRoot + TEXT("DA_FullMap");
const TCHAR* Stamp = TEXT("LDB90Checksum");

bool SaveAsset(UObject* Object, const FString& Path)
{
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public|RF_Standalone;
    return UPackage::SavePackage(Object->GetOutermost(), Object,
        *FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()), Args);
}

UMaterial* InstancedMaterial(const TCHAR* SourceName)
{
    const FString Name=FString(SourceName)+TEXT("Instanced"), Path=B90AssetRoot+Name;
    auto* Material=FPackageName::DoesPackageExist(Path)?LoadObject<UMaterial>(nullptr,*(Path+TEXT(".")+Name)):nullptr;
    if(!Material)
    {
        auto* Original=LoadObject<UMaterial>(nullptr,*(FString(TEXT("/Game/Lockdown/Generated/Materials/"))+SourceName+TEXT(".")+SourceName));
        if(!Original) { return nullptr; }
        Material=Cast<UMaterial>(StaticDuplicateObject(Original,CreatePackage(*Path),*Name));
        Material->SetFlags(RF_Public|RF_Standalone);
    }
    Material->SetMaterialUsage(MATUSAGE_InstancedStaticMeshes); Material->PostEditChange();
    return SaveAsset(Material,Path)?Material:nullptr;
}

AStaticMeshActor* Place(UWorld* W, FName Id, UStaticMesh* Mesh, const FVector& Position,
                        const FVector& Scale, bool Floor, const FRotator& Rotation = FRotator::ZeroRotator)
{
    auto* A = W->SpawnActor<AStaticMeshActor>(Position, Rotation);
    A->SetActorLabel(Id.ToString()); A->Tags.Add(Id);
    auto* C = A->GetStaticMeshComponent(); C->SetStaticMesh(Mesh); A->SetActorScale3D(Scale);
    C->SetCollisionProfileName(TEXT("BlockAll"));
    C->SetCollisionResponseToChannel(ECC_GameTraceChannel1, Floor?ECR_Block:ECR_Ignore);
    C->SetCollisionResponseToChannel(ECC_GameTraceChannel2, Floor?ECR_Ignore:ECR_Block);
    const TCHAR* Material = Floor?TEXT("M_Ground"):TEXT("M_Stone");
    if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr,
        *(FString(TEXT("/Game/Lockdown/Generated/Materials/"))+Material+TEXT(".")+Material))) { C->SetMaterial(0, M); }
    return A;
}

void Box(UWorld* W, const FString& Id, const FVector& Centre, const FVector& Size, bool Floor = false, double Yaw = 0)
{ Place(W, FName(*Id), LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")), Centre, Size/100., Floor, FRotator(0,Yaw,0)); }

UStaticMesh* Terrain(const ULDMapDefinition* D, int32 TX, int32 TY)
{
    const FString Name = FString::Printf(TEXT("SM_Terrain_%02d_%02d"), TX, TY), Path = B90AssetRoot+TEXT("Terrain/")+Name;
    auto* Mesh = FPackageName::DoesPackageExist(Path)?LoadObject<UStaticMesh>(nullptr, *(Path+TEXT(".")+Name)):nullptr;
    if (!Mesh) { Mesh = NewObject<UStaticMesh>(CreatePackage(*Path), *Name, RF_Public|RF_Standalone); }
    Mesh->GetOutermost()->FullyLoad(); Mesh->SetNumSourceModels(1);
    FMeshDescription Description; FStaticMeshAttributes Attributes(Description); Attributes.Register();
    const auto Positions = Attributes.GetVertexPositions(); auto UVs = Attributes.GetVertexInstanceUVs(); UVs.SetNumChannels(1);
    const auto Group = Description.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Ground");
    constexpr int32 N = 50; constexpr double Step = 400;
    TArray<FVertexID> Vertices; Vertices.Reserve((N+1)*(N+1));
    for (int32 Y = 0; Y <= N; ++Y)
    {
        for (int32 X = 0; X <= N; ++X)
        {
            const FVector2D XY(-120000+TX*20000+X*Step, -120000+TY*20000+Y*Step);
            const auto V = Description.CreateVertex(); Positions[V] = FVector3f(X*Step, Y*Step, D->Height(XY)); Vertices.Add(V);
        }
    }
    auto Triangle = [&](int32 A, int32 B, int32 C)
    {
        TArray<FVertexInstanceID> Instances;
        for (int32 Index : {A,B,C})
        {
            const auto V = Description.CreateVertexInstance(Vertices[Index]);
            UVs.Set(V, 0, FVector2f(Positions[Vertices[Index]].X/20000, Positions[Vertices[Index]].Y/20000)); Instances.Add(V);
        }
        Description.CreatePolygon(Group, Instances);
    };
    for (int32 Y = 0; Y < N; ++Y) { for (int32 X = 0; X < N; ++X)
    { const int32 A = Y*(N+1)+X; Triangle(A,A+N+2,A+1); Triangle(A,A+N+1,A+N+2); } }
    auto& Settings = Mesh->GetSourceModel(0).BuildSettings; Settings.bRecomputeNormals = true; Settings.bRecomputeTangents = true;
    Mesh->CreateMeshDescription(0, MoveTemp(Description)); Mesh->CommitMeshDescription(0);
    Mesh->GetStaticMaterials().Reset(); Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr,TEXT("Ground")));
    Mesh->CreateBodySetup(); Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseComplexAsSimple;
    Mesh->Build(false); Mesh->GetBodySetup()->CreatePhysicsMeshes(); Mesh->CreateNavCollision(true);
    Mesh->GetOutermost()->GetMetaData().SetValue(Mesh, Stamp, *D->Checksum);
    return SaveAsset(Mesh, Path)?Mesh:nullptr;
}

void BuildBridge(UWorld* W, const ULDMapDefinition* D, FName Id)
{
    const auto* POI = D->Find(Id); FVector Centre = POI->Position, Tangent(0,1,0); double Best = DBL_MAX, Width = 2200;
    for (const auto& River : D->Rivers) { for (int32 I = 1; I < River.Points.Num(); ++I)
    {
        const FVector A = River.Points[I-1], Delta = River.Points[I]-A;
        const double T = FMath::Clamp(FVector::DotProduct(POI->Position-A, Delta)/Delta.SizeSquared(), 0., 1.);
        const FVector P = A+T*Delta; const double Distance = FVector::DistSquared2D(P,POI->Position);
        if (Distance < Best) { Best = Distance; Centre = P; Tangent = Delta.GetSafeNormal2D(); Width = River.Width; }
    } }
    const FVector Across(-Tangent.Y, Tangent.X, 0); const double Half = Width*.5+6000;
    const double Deck = D->NavigationTarget(Id).Z;
    auto Point = [&](double Along)
    {
        FVector P = Centre+Across*Along;
        const double T = FMath::Clamp((FMath::Abs(Along)-Width*.5)/6000.,0.,1.);
        P.Z = FMath::Lerp(Deck,D->Height(FVector2D(Centre+Across*(Along<0?-Half:Half))),T);
        return P;
    };
    const int32 Count = FMath::CeilToInt(2*Half/400);
    for (int32 I = 0; I < Count; ++I)
    {
        const FVector A = Point(-Half+2*Half*I/Count), B = Point(-Half+2*Half*(I+1)/Count), Delta = B-A;
        Place(W,FName(*FString::Printf(TEXT("bridge_%s_%03d"),*Id.ToString(),I)),
            LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")),
            (A+B)*.5-FVector(0,0,20), FVector(Delta.Size()+4,800,40)/100., true,
            FRotator(FMath::RadiansToDegrees(FMath::Atan2(Delta.Z,Delta.Size2D())),Delta.Rotation().Yaw,0));
        // Bank approaches can be entered laterally. Fit broad side aprons rather
        // than exposing a near-step-height vertical slab edge to a capsule.
        if(FMath::Abs(-Half+2*Half*(I+.5)/Count)>Width*.5+200)
        {
            for(int32 Side:{-1,1})
            {
                const FVector Inner=(A+B)*.5+Tangent*(Side*400);
                FVector Outer=Inner+Tangent*(Side*2400); Outer.Z=D->Height(FVector2D(Outer));
                const FVector SideDelta=Outer-Inner;
                Place(W,FName(*FString::Printf(TEXT("bridge_%s_apron_%03d_%d"),*Id.ToString(),I,Side)),
                    LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")),
                    (Inner+Outer)*.5-FVector(0,0,20),FVector(SideDelta.Size()+4,Delta.Size2D()+40,40)/100.,true,
                    FRotator(FMath::RadiansToDegrees(FMath::Atan2(SideDelta.Z,SideDelta.Size2D())),SideDelta.Rotation().Yaw,0));
            }
        }
    }
}

void SurfaceGeometry(UWorld* W, const ULDMapDefinition* D)
{
    auto Layer = [&](const TCHAR* Name, const TCHAR* Material)
    {
        auto* A = W->SpawnActor<AActor>(); A->SetActorLabel(Name);
        auto* C = NewObject<UInstancedStaticMeshComponent>(A,Name,RF_Transactional);
        A->SetRootComponent(C); A->AddInstanceComponent(C);
        C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        C->SetMaterial(0,InstancedMaterial(Material));
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCanEverAffectNavigation(false); C->RegisterComponent(); return C;
    };
    auto* Roads = Layer(TEXT("FittedRoadSurfaces"),TEXT("M_Soil"));
    auto* Water = Layer(TEXT("PermanentRiverSurfaces"),TEXT("M_River"));
    auto Strip = [&](UInstancedStaticMeshComponent* C, FVector A, FVector B, double Width, bool Ground)
    {
        const int32 Steps=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(A,B)/200));
        for(int32 I=0;I<Steps;++I)
        {
            FVector P=FMath::Lerp(A,B,double(I)/Steps), Q=FMath::Lerp(A,B,double(I+1)/Steps);
            if(Ground)
            {
                for(FVector* V:{&P,&Q}) { FHitResult Hit;
                    if(W->LineTraceSingleByChannel(Hit,*V+FVector(0,0,80),*V-FVector(0,0,100),ECC_GameTraceChannel1)) { V->Z=Hit.ImpactPoint.Z; }
                }
            }
            const FVector Delta=Q-P;
            const FRotator Rotation(FMath::RadiansToDegrees(FMath::Atan2(Delta.Z,Delta.Size2D())),Delta.Rotation().Yaw,0);
            C->AddInstance(FTransform(Rotation,(P+Q)*.5+FVector(0,0,Ground?2:5),FVector(Delta.Size()+3,Width,2)/100.));
        }
    };
    for(const auto& R:D->Rivers) { for(int32 I=1;I<R.Points.Num();++I) { Strip(Water,R.Points[I-1],R.Points[I],R.Width,false); } }
    Water->AddInstance(FTransform(FRotator::ZeroRotator,D->WetInset+FVector(0,0,5),FVector(D->WetSize.X,D->WetSize.Y,2)/100.));
    for(const auto& R:D->Routes) { for(int32 I=1;I<R.Points.Num();++I)
    {
        const FVector A=I==1?D->NavigationTarget(R.From):R.Points[I-1];
        const FVector B=I==R.Points.Num()-1?D->NavigationTarget(R.To):R.Points[I];
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(W,A,B);
        if(!Path||!Path->IsValid()||Path->IsPartial()) { continue; } // Evidence() rejects any missing native route.
        for(int32 J=1;J<Path->PathPoints.Num();++J) { Strip(Roads,Path->PathPoints[J-1],Path->PathPoints[J],R.Width,true); }
    } }
}

bool Build(UWorld* W, ULDMapDefinition* D)
{
    W->GetWorldSettings()->DefaultGameMode = ALDB90PreviewMode::StaticClass();
    auto* Anchor = W->SpawnActor<ALDMapAnchor>(); Anchor->Definition = D; Anchor->SetActorLabel(TEXT("FullMapDefinition"));
    for (int32 Y = 0; Y < 12; ++Y) { for (int32 X = 0; X < 12; ++X)
    {
        auto* Mesh = Terrain(D,X,Y); if (!Mesh) { return false; }
        Place(W,FName(*FString::Printf(TEXT("terrain_%02d_%02d"),X,Y)),Mesh,FVector(-120000+X*20000,-120000+Y*20000,0),FVector(1),true);
    } }
    // Closed shells and open structural footprints. Interactive doors/interiors belong to B91.
    for (const auto& B : D->Buildings)
    {
        if (!B.Enterable) { Box(W,B.Id.ToString(),B.Position+FVector(0,0,300),B.Size); continue; }
        for (int32 Side : {-1,1})
        {
            Box(W,B.Id.ToString()+FString::Printf(TEXT("_wall_x%d"),Side),B.Position+FVector(Side*680,0,300),FVector(40,1800,600));
            for (int32 Wing : {-1,1})
            { Box(W,B.Id.ToString()+FString::Printf(TEXT("_wall_y%d_%d"),Side,Wing),B.Position+FVector(Wing*425,Side*880,300),FVector(550,40,600)); }
        }
    }
    for (FName Id : D->Bridges) { BuildBridge(W,D,Id); }
    for (const auto& P : D->Spawns)
    {
        auto* Start = W->SpawnActor<APlayerStart>(P.Position+FVector(0,0,95),FRotator::ZeroRotator);
        Start->SetActorLabel(P.Id.ToString()); Start->PlayerStartTag = P.Id; Start->Tags.Add(P.Id);
    }
    // Safety wall begins at the ordinary-body centre limit, outside the playable land.
    for (int32 Sign : {-1,1})
    {
        Box(W,FString::Printf(TEXT("border_x%d"),Sign),FVector(Sign*118200,0,600),FVector(400,240000,3000));
        Box(W,FString::Printf(TEXT("border_y%d"),Sign),FVector(0,Sign*118200,600),FVector(240000,400,3000));
    }
    for (int32 I = 0; I < 2; ++I)
    {
        auto* Light = W->SpawnActor<ADirectionalLight>(FVector(0,0,3000),FRotator(-60,I?145:-35,0));
        Light->GetLightComponent()->SetIntensity(I?1:5); Light->GetLightComponent()->SetCastShadows(!I);
        CastChecked<UDirectionalLightComponent>(Light->GetLightComponent())->SetForwardShadingPriority(I?1:2);
    }
    auto* Bounds = W->SpawnActor<ALDRegionNavBounds>(FVector(0,0,600),FRotator::ZeroRotator);
    Bounds->Configure(FVector(240000,240000,4000)); Bounds->SetActorLabel(TEXT("FullMapNavigationBounds"));
    Bounds->Brush = NewObject<UModel>(Bounds,NAME_None,RF_Transactional); Bounds->Brush->Initialize(Bounds,false);
    Bounds->GetBrushComponent()->Brush = Bounds->Brush;
    auto* Builder = NewObject<UCubeBuilder>(Bounds,NAME_None,RF_Transactional); Bounds->BrushBuilder = Builder;
    Builder->X = 240000; Builder->Y = 240000; Builder->Z = 4000;
    if (!Builder->Build(W,Bounds)) { return false; }
    Bounds->Brush->BuildBound(); Bounds->GetBrushComponent()->BuildSimpleBrushCollision(); Bounds->GetBrushComponent()->UpdateBounds();
    Bounds->PostEditChange(); Bounds->PostEditMove(true); Bounds->GetBrushComponent()->SetCanEverAffectNavigation(false);
    FNavigationSystem::AddNavigationSystemToWorld(*W,FNavigationSystemRunMode::EditorMode,W->GetWorldSettings()->GetNavigationSystemConfig());
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W); if (!Nav) { return false; }
    if (auto* Recast=Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::Create)))
    {
        // Full-map settings are saved on this map only, never written to legacy config.
        Recast->TileSizeUU=4096; Recast->DefaultMaxSearchNodes=65536;
        Recast->AgentHeight=180;
        for (uint8 I=0;I<uint8(ENavigationDataResolution::MAX);++I)
        { Recast->SetCellSize(ENavigationDataResolution(I),40); Recast->SetCellHeight(ENavigationDataResolution(I),10); }
        Recast->PostEditChange();
        Recast->AgentRadius=60; // Two-voxel conservative clearance for the unchanged 28 cm capsule.
        for(uint8 I=0;I<uint8(ENavigationDataResolution::MAX);++I)
        { Recast->SetAgentMaxStepHeight(ENavigationDataResolution(I),30); }
    }
    FlushAsyncLoading(); FAssetCompilingManager::Get().FinishAllCompilation();
    Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    Nav->OnNavigationBoundsUpdated(Bounds); Nav->Build();
    SurfaceGeometry(W,D);
    W->GetOutermost()->GetMetaData().SetValue(W,Stamp,*D->Checksum);
    return UEditorLoadingAndSavingUtils::SaveMap(W,MapPath);
}

bool Evidence(UWorld* W, const ULDMapDefinition* D, const FString& Output)
{
    IFileManager::Get().MakeDirectory(*Output,true);
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W); if (!Nav) { return false; }
    FlushAsyncLoading(); FAssetCompilingManager::Get().FinishAllCompilation();
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Fitted;
    TMap<FName,FVector> Destinations; int32 Failed = 0, TerrainCount = 0, SpawnCount = 0;
    for (AActor* A : W->PersistentLevel->Actors)
    { if (A && A->GetActorLabel().StartsWith(TEXT("terrain_"))) { ++TerrainCount; } if (Cast<APlayerStart>(A)) { ++SpawnCount; } }
    auto Fit = [&](const FLDMapPoint& P)
    {
        FHitResult Ground;
        const bool HasGround=W->LineTraceSingleByChannel(Ground,P.Position+FVector(0,0,1000),P.Position-FVector(0,0,1200),ECC_GameTraceChannel1);
        FNavLocation Location;
        const FVector Target=D->Find(P.Id)?D->NavigationTarget(P.Id):P.Position;
        const bool Found = Nav->ProjectPointToNavigation(Target,Location,FVector(60,60,D->Bridges.Contains(P.Id)?80:900));
        if (!Found || FVector::DistSquared2D(P.Position,Location.Location)>60*60) { ++Failed; }
        else { Destinations.Add(P.Id,Location.Location); }
        auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("id"),P.Id.ToString()); Row->SetBoolField(TEXT("projected"),Found);
        Row->SetStringField(TEXT("source_cm"),P.Position.ToString()); Row->SetStringField(TEXT("fitted_cm"),Found?Location.Location.ToString():TEXT("Unavailable"));
        Row->SetNumberField(TEXT("vertical_fit_cm"),Found?Location.Location.Z-P.Position.Z:0); Fitted.Add(MakeShared<FJsonValueObject>(Row));
        Row->SetBoolField(TEXT("ground_collision"),HasGround);
        Row->SetNumberField(TEXT("ground_normal_z"),HasGround?Ground.ImpactNormal.Z:0);
    };
    for (const auto& P : D->POIs) { Fit(P); } for (const auto& P : D->Spawns) { Fit(P); }
    FString Paths = TEXT("from,to,native_path_length_cm,nominal_seconds_at_420,query_ms,complete\n"); int32 Queries = 0;
    int32 GroundSamples=0,MissingGround=0,BlockedSweeps=0,SteppedContacts=0; double MaxGrade=0;
    TArray<TSharedPtr<FJsonValue>> SweepFailures;
    auto Query = [&](FName A, FName B, bool Scan=false)
    {
        ++Queries; bool Complete = false; double Length = 0; const double Start = FPlatformTime::Seconds();
        if(Destinations.Contains(A)&&Destinations.Contains(B)&&FVector::DistSquared2D(Destinations[A],Destinations[B])<1)
        { Complete=true; }
        else if (Destinations.Contains(A) && Destinations.Contains(B))
        {
            auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(W,Destinations[A],Destinations[B]);
            Complete = Path && Path->IsValid() && !Path->IsPartial();
            if (Complete) { for (int32 I = 1; I < Path->PathPoints.Num(); ++I)
            {
                const FVector StartPoint=Path->PathPoints[I-1], EndPoint=Path->PathPoints[I];
                Length += FVector::Distance(StartPoint,EndPoint);
                if(Scan)
                {
                    const int32 Count=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(StartPoint,EndPoint)/100));
                    FVector Last=FVector::ZeroVector; bool Previous=false;
                    for(int32 J=0;J<=Count;++J)
                    {
                        const FVector P=FMath::Lerp(StartPoint,EndPoint,double(J)/Count); FHitResult Ground; FNavLocation Local;
                        ++GroundSamples;
                        FVector Search=P;
                        if(Previous) { Search.Z=Last.Z-72; } // Follow the same surface, not a nearer lower bridge layer.
                        if(!Nav->ProjectPointToNavigation(Search,Local,FVector(60,60,1200))
                           ||!W->LineTraceSingleByChannel(Ground,Local.Location+FVector(0,0,25),Local.Location-FVector(0,0,80),ECC_GameTraceChannel1))
                        { ++MissingGround; Previous=false; continue; }
                        const FVector Current=Ground.ImpactPoint+FVector(0,0,92);
                        const double Normal=FMath::Clamp(double(Ground.ImpactNormal.Z),.001,1.);
                        MaxGrade=FMath::Max(MaxGrade,FMath::Sqrt(1-Normal*Normal)/Normal);
                        if(Previous)
                        {
                            FHitResult Hit;
                            // Native walking resolves walkable floor contacts. Count blocking walls/ceilings,
                            // not ordinary contacts with the same fitted floor being followed.
                            if(W->SweepSingleByChannel(Hit,Last,Current,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(28,90))
                               &&Hit.ImpactNormal.Z<FMath::Cos(FMath::DegreesToRadians(40.)))
                            {
                                // A raw horizontal sweep also hits legal steps. Exercise the same
                                // bounded up/forward/down shape sequence as ordinary 35 cm stepping.
                                FHitResult Up,Across,Down; const FVector Lift(0,0,35);
                                const auto Capsule=FCollisionShape::MakeCapsule(28,90);
                                const bool CanStep= !W->SweepSingleByChannel(Up,Last,Last+Lift,FQuat::Identity,ECC_Pawn,Capsule)
                                    &&!W->SweepSingleByChannel(Across,Last+Lift,Current+Lift,FQuat::Identity,ECC_Pawn,Capsule)
                                    &&W->SweepSingleByChannel(Down,Current+Lift,Current-FVector(0,0,35),FQuat::Identity,ECC_Pawn,Capsule)
                                    &&Down.ImpactNormal.Z>=FMath::Cos(FMath::DegreesToRadians(40.))
                                    &&Down.Location.Z-Last.Z<=37; // Existing floor gap is 2 cm.
                                if(CanStep) { ++SteppedContacts; Last=Current; Previous=true; continue; }
                                ++BlockedSweeps; auto Failure=MakeShared<FJsonObject>();
                                Failure->SetStringField(TEXT("from"),A.ToString()); Failure->SetStringField(TEXT("to"),B.ToString());
                                Failure->SetStringField(TEXT("actor"),Hit.GetActor()?Hit.GetActor()->GetActorLabel():TEXT("Unknown"));
                                Failure->SetStringField(TEXT("start"),Last.ToString()); Failure->SetStringField(TEXT("end"),Current.ToString());
                                Failure->SetStringField(TEXT("normal"),Hit.ImpactNormal.ToString());
                                SweepFailures.Add(MakeShared<FJsonValueObject>(Failure));
                            }
                        }
                        Last=Current; Previous=true;
                    }
                }
            } }
        }
        const double Ms = (FPlatformTime::Seconds()-Start)*1000;
        if (!Complete) { ++Failed; UE_LOG(LogTemp,Error,TEXT("B90 native path failed %s -> %s"),*A.ToString(),*B.ToString()); }
        Paths += FString::Printf(TEXT("%s,%s,%.3f,%.3f,%.4f,%s\n"),*A.ToString(),*B.ToString(),Length,Length/420,Ms,Complete?TEXT("true"):TEXT("false"));
    };
    // All spawns to all POIs, plus both directions of every declared approach.
    for (const auto& Spawn : D->Spawns) { for (const auto& P : D->POIs) { Query(Spawn.Id,P.Id); } }
    for (const auto& R : D->Routes) { Query(R.From,R.To,true); Query(R.To,R.From); }
    TSharedPtr<FJsonObject> Source;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(D->SourceJson),Source);
    for (const auto& V:Source->GetArrayField(TEXT("travel")))
    { const auto Trip=V->AsObject(); Query(FName(*Trip->GetStringField(TEXT("start"))),FName(*Trip->GetStringField(TEXT("end")))); }
    Query(TEXT("antechamber"),TEXT("heart"));
    Report->SetStringField(TEXT("stage"),TEXT("B90")); Report->SetStringField(TEXT("payload_sha256"),D->Checksum);
    Report->SetNumberField(TEXT("seed"),D->Seed); Report->SetNumberField(TEXT("layout_revision"),D->LayoutRevision);
    Report->SetNumberField(TEXT("fitter_revision"),D->FitterRevision); Report->SetNumberField(TEXT("terrain_tiles"),TerrainCount);
    Report->SetNumberField(TEXT("spawn_actors"),SpawnCount); Report->SetNumberField(TEXT("native_path_queries"),Queries);
    Report->SetNumberField(TEXT("failures"),Failed); Report->SetArrayField(TEXT("fitted_pads"),Fitted);
    Report->SetNumberField(TEXT("route_ground_samples"),GroundSamples); Report->SetNumberField(TEXT("missing_ground_samples"),MissingGround);
    Report->SetNumberField(TEXT("candidate_capsule_contacts"),BlockedSweeps); Report->SetNumberField(TEXT("max_sampled_grade"),MaxGrade);
    Report->SetNumberField(TEXT("legal_step_contacts"),SteppedContacts);
    Report->SetArrayField(TEXT("candidate_contacts"),SweepFailures);
    TArray<FString> Geometry;
    for(AActor* A:W->PersistentLevel->Actors)
    {
        if(A&&(A->IsA<AStaticMeshActor>()||A->IsA<APlayerStart>()))
        { Geometry.Add(A->GetActorLabel()+TEXT("|")+A->GetActorTransform().ToString()); }
        if(A) { if(const auto* Instances=A->FindComponentByClass<UInstancedStaticMeshComponent>())
        { for(int32 I=0;I<Instances->GetInstanceCount();++I)
          { FTransform Transform; Instances->GetInstanceTransform(I,Transform,true);
            Geometry.Add(FString::Printf(TEXT("%s_%05d|"),*A->GetActorLabel(),I)+Transform.ToString()); } } }
    }
    Geometry.Sort(); FFileHelper::SaveStringArrayToFile(Geometry,*(Output/TEXT("FittedGeometry.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    Report->SetStringField(TEXT("scope"),TEXT("Editor Recast paths/projections against saved native collision; path times are estimates, not walked time. No economy, combat, NPC, network or human acceptance."));
    FString Json; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    IFileManager::Get().MakeDirectory(*Output,true);
    FFileHelper::SaveStringToFile(Json,*(Output/TEXT("NativePaths.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FFileHelper::SaveStringToFile(Paths,*(Output/TEXT("NativeTravel.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("LD_B90_PATHS queries=%d failures=%d terrain=%d spawns=%d"),Queries,Failed,TerrainCount,SpawnCount);
    return Failed==0 && TerrainCount==144 && SpawnCount==24;
}
}

ULDB90BootstrapCommandlet::ULDB90BootstrapCommandlet()
{ IsClient=false; IsEditor=true; LogToConsole=true; }
int32 ULDB90BootstrapCommandlet::Main(const FString& Params)
{
    const bool Validate = FParse::Param(*Params,TEXT("Validate"));
    const bool PresentationOnly=FParse::Param(*Params,TEXT("PresentationOnly")); FString Output;
    if(Validate&&PresentationOnly) { return 1; }
    FParse::Value(*Params,TEXT("Output="),Output); if (Output.IsEmpty()) { Output=FPaths::ProjectDir()/TEXT("Artifacts/B90"); }
    FString Source, Error;
    if (!FFileHelper::LoadFileToString(Source,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/map_b90.json")))) { return 1; }
    TStrongObjectPtr<ULDMapDefinition> Expected(NewObject<ULDMapDefinition>());
    if (!Expected->Import(Source,Error)) { UE_LOG(LogTemp,Error,TEXT("%s"),*Error); return 2; }
    auto* D = FPackageName::DoesPackageExist(DefinitionPath)?LoadObject<ULDMapDefinition>(nullptr,*(DefinitionPath+TEXT(".DA_FullMap"))):nullptr;
    UWorld* W = nullptr;
    if (!Validate)
    {
        if (!D) { D=NewObject<ULDMapDefinition>(CreatePackage(*DefinitionPath),TEXT("DA_FullMap"),RF_Public|RF_Standalone); }
        D->GetOutermost()->FullyLoad(); if (!D->Import(Source,Error) || !SaveAsset(D,DefinitionPath)) { return 3; }
        if(PresentationOnly)
        {
            W=UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(MapPath,FPackageName::GetMapPackageExtension()));
            if(!W) { return 4; } int32 Layers=0;
            for(AActor* A:W->PersistentLevel->Actors) { if(A) { if(auto* C=A->FindComponentByClass<UInstancedStaticMeshComponent>())
            {
                const FString Label=A->GetActorLabel();
                if(Label==TEXT("FittedRoadSurfaces")||Label==TEXT("PermanentRiverSurfaces"))
                { auto* M=InstancedMaterial(Label==TEXT("FittedRoadSurfaces")?TEXT("M_Soil"):TEXT("M_River"));
                  if(!M) { return 4; } C->SetMaterial(0,M); ++Layers; }
            } } }
            if(Layers!=2||!UEditorLoadingAndSavingUtils::SaveMap(W,MapPath)) { return 4; }
        }
        else { W=UEditorLoadingAndSavingUtils::NewBlankMap(false); if (!W || !Build(W,D)) { return 4; } }
    }
    else { W=UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(MapPath,FPackageName::GetMapPackageExtension())); }
    if (!D || !W || !D->Validate(Error) || W->GetOutermost()->GetMetaData().GetValue(W,Stamp)!=D->Checksum) { return 5; }
    for (TFieldIterator<FProperty> P(ULDMapDefinition::StaticClass(),EFieldIteratorFlags::ExcludeSuper); P; ++P)
    { if (!P->Identical_InContainer(D,Expected.Get())) { UE_LOG(LogTemp,Error,TEXT("B90 saved definition mismatch %s"),*P->GetName()); return 6; } }
    if (!Evidence(W,D,Output)) { return 7; }
    UE_LOG(LogTemp,Display,TEXT("LD_B90_SUCCESS mode=%s checksum=%s"),Validate?TEXT("Validate"):TEXT("Build"),*D->Checksum); return 0;
}
