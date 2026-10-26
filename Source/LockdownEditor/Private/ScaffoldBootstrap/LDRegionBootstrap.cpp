#include "LDRegionBootstrap.h"
#include "Region/LDRegionDefinition.h"
#include "Region/LDRegionNavBounds.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Materials/Material.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Model.h"
#include "AssetCompilingManager.h"

namespace
{
const TCHAR* RegionStampKey=TEXT("LockdownRegionSignature");
bool Save(UObject* Object,const FString& Path,const FString& Signature)
{
    Object->GetOutermost()->GetMetaData().SetValue(Object,RegionStampKey,*Signature);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
    const FString File=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);return UPackage::SavePackage(Object->GetOutermost(),Object,*File,Args);
}
bool Mesh(UWorld* W,const FString& Name,const FString& Shape,const FVector& Position,const FVector& Size,const FRotator& Rotation,bool Floor,bool High,bool Road)
{
    auto* A=W->SpawnActor<AStaticMeshActor>(Position,Rotation);if(!A){return false;}A->SetActorLabel(Name);A->Tags.Add(FName(*Name));auto* C=A->GetStaticMeshComponent();C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,*(TEXT("/Engine/BasicShapes/")+Shape)));if(!C->GetStaticMesh()){return false;}
    A->SetActorScale3D(Size/100.);C->SetCollisionProfileName(TEXT("BlockAll"));C->SetCollisionResponseToChannel(ECC_GameTraceChannel1,Floor?ECR_Block:ECR_Ignore);C->SetCollisionResponseToChannel(ECC_GameTraceChannel2,Floor?ECR_Ignore:ECR_Block);
    if(High){C->ComponentTags.Add(TEXT("LDHighGround"));}if(Road){C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);}
    const FString Material=Name.StartsWith(TEXT("RiverFloor"))?TEXT("M_River"):Floor&&!Road?TEXT("M_Ground"):Shape==TEXT("Cylinder")?TEXT("M_Tree"):TEXT("M_Stone");C->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,*(TEXT("/Game/Lockdown/Generated/Materials/")+Material+TEXT(".")+Material)));return true;
}
bool BuildMap(UWorld* W,ULDRegionDefinition* D,const FString& Path)
{
    auto* Anchor=W->SpawnActor<ALDRegionAnchor>();Anchor->Definition=D;Anchor->SetActorLabel(TEXT("PublicRegionDefinition"));
    for(const auto& G:D->Geometry){if(!Mesh(W,G.Name,G.Mesh,G.Position,G.Size,FRotator(G.Pitch,0,0),G.Floor,G.High,false)){return false;}}
    for(int32 R=0;R<D->Routes.Num();++R){const auto& Route=D->Routes[R];for(int32 I=1;I<Route.Points.Num();++I){const FVector A=Route.Points[I-1],B=Route.Points[I],Delta=B-A;const FVector Mid=(A+B)*.5+FVector(0,0,2);if(!Mesh(W,FString::Printf(TEXT("Route_%d_%d"),R,I),TEXT("Cube"),Mid,FVector(Delta.Size2D()+15,Route.Covered?140:240,4),FRotator(0,Delta.Rotation().Yaw,0),false,false,true)){return false;}}}
    auto* Start=W->SpawnActor<APlayerStart>(D->Spawns[0],FRotator::ZeroRotator);if(!Start){return false;}Start->SetActorLabel(TEXT("CitySpawn"));
    for(int32 I=0;I<2;++I){auto* Light=W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-60,I?145:-35,0));Light->GetLightComponent()->SetIntensity(I?1:5);Light->GetLightComponent()->SetCastShadows(!I);CastChecked<UDirectionalLightComponent>(Light->GetLightComponent())->SetForwardShadingPriority(I?1:2);}
    const FVector Extent(60*D->Grid.CellSize,40*D->Grid.CellSize,800),Centre(D->Grid.Min.X+Extent.X/2,D->Grid.Min.Y+Extent.Y/2,200);
    auto* Bounds=W->SpawnActor<ALDRegionNavBounds>(Centre,FRotator::ZeroRotator);Bounds->Configure(Extent);Bounds->SetActorLabel(TEXT("RegionalNavigationBounds"));Bounds->Brush=NewObject<UModel>(Bounds,NAME_None,RF_Transactional);Bounds->Brush->Initialize(Bounds,false);Bounds->GetBrushComponent()->Brush=Bounds->Brush;
    auto* Builder=NewObject<UCubeBuilder>(Bounds,NAME_None,RF_Transactional);Bounds->BrushBuilder=Builder;Builder->X=Extent.X;Builder->Y=Extent.Y;Builder->Z=Extent.Z;if(!Builder->Build(W,Bounds)){return false;}Bounds->Brush->BuildBound();Bounds->GetBrushComponent()->BuildSimpleBrushCollision();Bounds->GetBrushComponent()->UpdateBounds();Bounds->PostEditChange();Bounds->PostEditMove(true);Bounds->GetBrushComponent()->SetCanEverAffectNavigation(false);
    FNavigationSystem::AddNavigationSystemToWorld(*W,FNavigationSystemRunMode::EditorMode,W->GetWorldSettings()->GetNavigationSystemConfig());
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);if(!Nav){UE_LOG(LogTemp,Error,TEXT("Region navigation system absent"));return false;}FlushAsyncLoading();FAssetCompilingManager::Get().FinishAllCompilation();
    Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);Nav->OnNavigationBoundsUpdated(Bounds);Nav->Build();
    const auto Towns=D->Towns();auto* NavData=Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);if(!NavData){UE_LOG(LogTemp,Error,TEXT("Region navigation data absent"));return false;}
    if(Path.Contains(TEXT("L_V07Region"))||Path.Contains(TEXT("L_V08Region"))){if(auto* Property=FindFProperty<FProperty>(NavData->GetClass(),TEXT("RuntimeGeneration"))){*Property->ContainerPtrToValuePtr<uint8>(NavData)=uint8(ERuntimeGenerationType::Dynamic);}}
    const auto* Route=UNavigationSystemV1::FindPathToLocationSynchronously(W,D->Node(Towns[0])->Position,D->Node(Towns[1])->Position);
    if(!Route||!Route->IsValid()||Route->IsPartial()){UE_LOG(LogTemp,Error,TEXT("Region authoring failed town-to-town navigation"));return false;}
    W->GetOutermost()->GetMetaData().SetValue(W,RegionStampKey,*(D->Signature+TEXT(":cooked-bounds")));return UEditorLoadingAndSavingUtils::SaveMap(W,Path);
}
bool ValidateMap(UWorld* W,const ULDRegionDefinition* D)
{
    if(!W||!W->PersistentLevel||W->GetOutermost()->GetMetaData().GetValue(W,RegionStampKey)!=D->Signature+TEXT(":cooked-bounds")){return false;}int32 Anchors=0,Bounds=0,Data=0;
    for(AActor* A:W->PersistentLevel->Actors){if(const auto* Anchor=Cast<ALDRegionAnchor>(A)){if(Anchor->Definition!=D){return false;}++Anchors;}if(const auto* B=Cast<ANavMeshBoundsVolume>(A)){const FVector Expected(60*D->Grid.CellSize,40*D->Grid.CellSize,800);if(!B->IsA<ALDRegionNavBounds>()||!B->GetComponentsBoundingBox(true).GetSize().Equals(Expected,1)){return false;}++Bounds;}if(A&&A->IsA<ARecastNavMesh>()){++Data;}}
    if(Anchors!=1||Bounds!=1||Data!=1){UE_LOG(LogTemp,Error,TEXT("Regional anchor/bounds/navdata counts %d/%d/%d"),Anchors,Bounds,Data);return false;}
    for(const auto& G:D->Geometry){int32 Found=0;for(AActor* A:W->PersistentLevel->Actors){const auto* M=Cast<AStaticMeshActor>(A);if(!M||M->GetActorLabel()!=G.Name){continue;}const auto* C=M->GetStaticMeshComponent();if(!M->GetActorLocation().Equals(G.Position,.1)||!M->GetActorScale3D().Equals(G.Size/100.,.001)||!M->GetActorRotation().Equals(FRotator(G.Pitch,0,0),.01)||!C->GetStaticMesh()||C->GetStaticMesh()->GetName()!=G.Mesh||C->GetCollisionResponseToChannel(ECC_GameTraceChannel1)!=(G.Floor?ECR_Block:ECR_Ignore)||C->GetCollisionResponseToChannel(ECC_GameTraceChannel2)!=(G.Floor?ECR_Ignore:ECR_Block)||C->ComponentHasTag(TEXT("LDHighGround"))!=G.High){return false;}++Found;}if(Found!=1){return false;}}
    for(int32 R=0;R<D->Routes.Num();++R){for(int32 I=1;I<D->Routes[R].Points.Num();++I){const auto& Route=D->Routes[R];const FVector A=Route.Points[I-1],B=Route.Points[I],Delta=B-A;int32 Found=0;for(AActor* Actor:W->PersistentLevel->Actors){const auto* M=Cast<AStaticMeshActor>(Actor);if(!M||M->GetActorLabel()!=FString::Printf(TEXT("Route_%d_%d"),R,I)){continue;}if(!M->GetActorLocation().Equals((A+B)*.5+FVector(0,0,2),.1)||!M->GetActorScale3D().Equals(FVector(Delta.Size2D()+15,Route.Covered?140:240,4)/100.,.001)||M->GetStaticMeshComponent()->GetCollisionEnabled()!=ECollisionEnabled::NoCollision){return false;}++Found;}if(Found!=1){return false;}}}return true;
}
}
bool LDRegionBootstrap::Run(bool Validate,bool Regenerate,bool RebuildMaps)
{
    FString Json;TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Definitions=nullptr;
    if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectConfigDir()/TEXT("RegionLayout.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||!Root->TryGetArrayField(TEXT("regions"),Definitions)||Definitions->Num()!=2){return false;}
    FString V07Json;TSharedPtr<FJsonObject> V07Root;if(!FFileHelper::LoadFileToString(V07Json,*(FPaths::ProjectConfigDir()/TEXT("V07RegionLayout.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(V07Json),V07Root)){return false;}TArray<TSharedPtr<FJsonValue>> All=*Definitions;All.Add(V07Root->GetArrayField(TEXT("regions"))[0]);Definitions=&All;
    FString V08Json;TSharedPtr<FJsonObject> V08Root;if(!FFileHelper::LoadFileToString(V08Json,*(FPaths::ProjectConfigDir()/TEXT("V08RegionLayout.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(V08Json),V08Root)){return false;}All.Add(V08Root->GetArrayField(TEXT("regions"))[0]);
    for(int32 I=0;I<Definitions->Num();++I)
    {
        FString Source,Error;FJsonSerializer::Serialize((*Definitions)[I]->AsObject().ToSharedRef(),TJsonWriterFactory<>::Create(&Source));auto* Expected=NewObject<ULDRegionDefinition>();if(!Expected->Import(Source,Error)){UE_LOG(LogTemp,Error,TEXT("Invalid region: %s"),*Error);return false;}
        const FString AssetName=I==3?TEXT("DA_ElementalRegion"):I==2?TEXT("DA_TacticalRegion"):I?TEXT("DA_AlternateRegion"):TEXT("DA_CivicRegion"),AssetPath=TEXT("/Game/Lockdown/Generated/Data/")+AssetName,MapPath=TEXT("/Game/Lockdown/Generated/Maps/")+FString(I==3?TEXT("L_V08Region"):I==2?TEXT("L_V07Region"):I?TEXT("L_RegionAlternate"):TEXT("L_DevelopmentRegion"));
        if(Regenerate||(!Validate&&!FPackageName::DoesPackageExist(AssetPath))){auto* D=NewObject<ULDRegionDefinition>(CreatePackage(*AssetPath),*AssetName,RF_Public|RF_Standalone);if(!D->Import(Source,Error)||!Save(D,AssetPath,D->Signature)){return false;}}
        auto* Definition=LoadObject<ULDRegionDefinition>(nullptr,*(AssetPath+TEXT(".")+AssetName));if(!Definition||!Definition->Validate(Error)){UE_LOG(LogTemp,Error,TEXT("Missing/invalid regional asset: %s"),*Error);return false;}
        for(TFieldIterator<FProperty> P(ULDRegionDefinition::StaticClass(),EFieldIteratorFlags::ExcludeSuper);P;++P){if(!P->Identical_InContainer(Definition,Expected)){UE_LOG(LogTemp,Error,TEXT("Saved region field mismatch: %s"),*P->GetName());return false;}}
        UWorld* W=nullptr;if(Regenerate||RebuildMaps||(!Validate&&!FPackageName::DoesPackageExist(MapPath))){W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W||!BuildMap(W,Definition,MapPath)){return false;}}
        else{W=UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(MapPath,FPackageName::GetMapPackageExtension()));}
        if(!ValidateMap(W,Definition)){UE_LOG(LogTemp,Error,TEXT("Saved region map validation failed: %s"),*MapPath);return false;}UE_LOG(LogTemp,Display,TEXT("LD_REGION_VALIDATED %s signature=%s nodes=%d edges=%d"),*MapPath,*Definition->Signature,Definition->Nodes.Num(),Definition->Routes.Num());
    }
    return true;
}
