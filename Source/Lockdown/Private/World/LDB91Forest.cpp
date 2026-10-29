#include "World/LDB91Forest.h"
#include "World/LDB91ForestContract.h"
#include "World/LDWorldObjects.h"
#include "Core/LDGroundQueries.h"
#include "Region/LDMapDefinition.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/Material.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "NavigationSystem.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
namespace
{
FString ForestHash(const FString& S){FTCHARToUTF8 B(*S);FSHA256Signature H;SHA256(reinterpret_cast<const uint8*>(B.Get()),B.Length(),H.Signature);return H.ToString().ToLower();}
FVector ForestPoint(const TSharedPtr<FJsonObject>& R,const TCHAR* K){const auto& V=R->GetArrayField(K);return FVector(V[0]->AsNumber(),V[1]->AsNumber(),V[2]->AsNumber());}
TSharedRef<FJsonValue> ForestVec(FVector P){return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)});}
}
ALDB91Forest::ALDB91Forest()
{
    bReplicates=false;Trunks=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PermanentForestTrunks"));SetRootComponent(Trunks);
    Trunks->SetMobility(EComponentMobility::Movable);Trunks->SetCollisionProfileName(TEXT("BlockAll"));Trunks->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);Trunks->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);Trunks->SetCanEverAffectNavigation(false);
}
bool ALDB91Forest::Ensure(UWorld* World,FString& Error)
{
    for(TActorIterator<ALDB91Forest> I(World);I;++I){return true;}
    auto* Forest=World->SpawnActor<ALDB91Forest>();if(!Forest||!Forest->Initialize(Error)){if(Forest){Forest->Destroy();}return false;}return true;
}
bool ALDB91Forest::Initialize(FString& Error)
{
    Trunks->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
    // The engine default supports instancing without changing any saved game material.
    Trunks->SetMaterial(0,UMaterial::GetDefaultMaterial(MD_Surface));
    FString Fit,Probe;FParse::Value(FCommandLine::Get(),TEXT("LDB91ForestFit="),Fit);FParse::Value(FCommandLine::Get(),TEXT("LDB91ForestProbe="),Probe);
    TSharedPtr<FJsonObject> Root;FString Json;TArray<TSharedPtr<FJsonValue>> Rows;
    auto Add=[&](FVector Floor){Trunks->AddInstance(FTransform(FQuat::Identity,Floor+FVector(0,0,200),FVector(1.2,1.2,4)),true);};
    if(!Fit.IsEmpty())
    {
        auto* Objects=ALDWorldObjects::Get(GetWorld());if(GetNetMode()!=NM_DedicatedServer||!Objects||!Objects->Ready||GetWorld()->GetFirstPlayerController()){Error=TEXT("Forest fitter requires a ready ownerless dedicated environment");return false;}
        if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/forest_patches_b91.json")))){return false;}Json=Json.Replace(TEXT("\r\n"),TEXT("\n"));if(ForestHash(Json)!=LD_B91_FOREST_PATCH_SHA||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)){Error=TEXT("Forest patch contract rejected");return false;}
        auto* Pad=GetWorld()->SpawnActor<ALDWorldObject>();Pad->Kind=ELDObjectKind::Sapling;TArray<FVector> Floors;
        for(const auto& V:Root->GetArrayField(TEXT("patches")))
        {
            const auto R=V->AsObject();const FVector Centre=ForestPoint(R,TEXT("center"));const FString Id=R->GetStringField(TEXT("id"));const double Radius=R->GetNumberField(TEXT("radius"));FRandomStream Random(9001+Rows.Num()*7919);int Count=0;
            for(int Attempt=0;Attempt<20000&&Count<250;++Attempt)
            {
                const double Angle=Random.FRand()*2*PI,Distance=FMath::Sqrt(Random.FRand())*(Radius-100);FVector Want=Centre+FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,0),Floor;
                Want.Z=ULDMapDefinition::Get(GetWorld())->Height(FVector2D(Want));bool Near=false;for(auto P:Floors){if(FVector::DistSquared2D(P,Want)<650.*650){Near=true;break;}}if(Near||!Objects->Placement(Pad,Want,Floor)){continue;}
                auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("id"),Id+FString::Printf(TEXT("_tree_%03d"),Count++));Row->SetStringField(TEXT("patch"),Id);Row->SetField(TEXT("floor"),ForestVec(Floor));Rows.Add(MakeShared<FJsonValueObject>(Row));Floors.Add(Floor);Add(Floor);
            }
            if(Count!=250){Error=TEXT("Forest patch cannot fit250 safe trunks: ")+Id;Pad->Destroy();return false;}
        }
        Pad->Destroy();auto Out=MakeShared<FJsonObject>();Out->SetNumberField(TEXT("schema"),9);Out->SetStringField(TEXT("patch_contract"),LD_B91_FOREST_PATCH_SHA);Out->SetArrayField(TEXT("rows"),Rows);FString Text;FJsonSerializer::Serialize(Out,TJsonWriterFactory<>::Create(&Text));if(!FFileHelper::SaveStringToFile(Text,*Fit,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)){return false;}
        UE_LOG(LogTemp,Display,TEXT("LD_B91_FOREST fit count=%d"),Rows.Num());FPlatformMisc::RequestExitWithStatus(false,0);return Rows.Num()==3000;
    }
    if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/forest_b91.json")))){Error=TEXT("Missing fitted forest");return false;}Json=Json.Replace(TEXT("\r\n"),TEXT("\n"));if(ForestHash(Json)!=LD_B91_FOREST_SHA||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||Root->GetArrayField(TEXT("rows")).Num()!=3000){Error=TEXT("Fitted forest contract rejected");return false;}
    Rows=Root->GetArrayField(TEXT("rows"));for(auto V:Rows){const FVector Floor=ForestPoint(V->AsObject(),TEXT("floor"));FVector Actual;if(!LDGround::MapSurface(GetWorld(),Floor,Actual)||FMath::Abs(Actual.Z-Floor.Z)>80){Error=TEXT("Fitted forest lost support");return false;}Add(Floor);}
    Trunks->SetMobility(EComponentMobility::Static);Trunks->SetCanEverAffectNavigation(true);FNavigationSystem::UpdateComponentData(*Trunks);
    if(!Probe.IsEmpty()&&GetNetMode()==NM_DedicatedServer)
    {
        int Walk=0,Sight=0;for(auto V:Rows){const FVector P=ForestPoint(V->AsObject(),TEXT("floor"))+FVector(0,0,90);FHitResult H;FCollisionQueryParams Q;
            Walk+=GetWorld()->LineTraceSingleByChannel(H,P-FVector(80,0,0),P+FVector(80,0,0),ECC_Pawn,Q)&&H.GetActor()==this;
            Sight+=GetWorld()->LineTraceSingleByChannel(H,P-FVector(0,80,0),P+FVector(0,80,0),ECC_GameTraceChannel2,Q)&&H.GetActor()==this;
        }
        auto Out=MakeShared<FJsonObject>();Out->SetNumberField(TEXT("count"),Trunks->GetInstanceCount());Out->SetNumberField(TEXT("walking_blockers"),Walk);Out->SetNumberField(TEXT("sight_blockers"),Sight);Out->SetBoolField(TEXT("not_replicated"),!GetIsReplicated());Out->SetBoolField(TEXT("no_tick"),!PrimaryActorTick.bCanEverTick);Out->SetBoolField(TEXT("navigation"),Trunks->CanEverAffectNavigation());Out->SetStringField(TEXT("contract"),LD_B91_FOREST_SHA);Out->SetArrayField(TEXT("rows"),Rows);FString Text;FJsonSerializer::Serialize(Out,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*Probe,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);FPlatformMisc::RequestExitWithStatus(false,Walk==3000&&Sight==3000?0:3);
    }
    return true;
}
