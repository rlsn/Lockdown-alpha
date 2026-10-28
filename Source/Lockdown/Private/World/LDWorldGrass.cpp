#include "World/LDWorldObjects.h"
#include "World/LDEnvironmentDirector.h"
#include "World/LDB91GrassContract.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "EngineUtils.h"
#include "Engine/World.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

void ALDWorldObject::ApplyGrassVisuals(uint8 State)
{
    // The source bounds stay500x500x150; sparse cosmetic blades keep the own
    // character readable. These meshes supply no collision, sight or authority.
    Proxy->SetRenderInMainPass(false);Proxy->SetRenderInDepthPass(false);Proxy->bUseAsOccluder=false;Proxy->SetCastShadow(false);
    if(GetNetMode()==NM_DedicatedServer){return;}
    if(GrassVisuals.IsEmpty())
    {
        for(int N=0;N<9;++N)
        {
            auto* Blade=NewObject<UStaticMeshComponent>(this);AddInstanceComponent(Blade);Blade->SetupAttachment(Proxy);Blade->SetAbsolute(false,false,true);Blade->SetMobility(EComponentMobility::Movable);Blade->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,N?TEXT("/Engine/BasicShapes/Cube.Cube"):TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));Blade->SetCollisionEnabled(ECollisionEnabled::NoCollision);Blade->SetCanEverAffectNavigation(false);Blade->SetCastShadow(N!=0);
            const double Angle=(N-1)*PI/4,Radius=N%2?190:140;Blade->SetRelativeLocation(N?FVector(FMath::Cos(Angle)*Radius/5,FMath::Sin(Angle)*Radius/5,0):FVector(0,0,-50));Blade->SetWorldScale3D(N?FVector(.12,.18,1.5):FVector(5,5,.03));Blade->RegisterComponent();GrassVisuals.Add(Blade);
        }
    }
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
    if(Base){auto* Tint=UMaterialInstanceDynamic::Create(Base,this);Tint->SetVectorParameterValue(TEXT("Tint"),State&32?FLinearColor(1,.25f,0):FLinearColor(.12f,.38f,.08f));for(const auto& Blade:GrassVisuals){Blade->SetMaterial(0,Tint);Blade->SetVisibility(!(State&5));}}
}

bool ALDWorldObjects::InitializeGrass(FString& Error)
{
    FString Json;if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/grass_b91.json")))){Error=TEXT("Missing authored grass contract");return false;}
    Json=Json.Replace(TEXT("\r\n"),TEXT("\n"));FTCHARToUTF8 Bytes(*Json);FSHA256Signature Hash;SHA256(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length(),Hash.Signature);if(Hash.ToString().ToLower()!=LD_B91_GRASS_SHA){Error=TEXT("Stale authored grass contract");return false;}
    TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||Root->GetArrayField(TEXT("rows")).Num()!=96){return false;}
    for(const auto& V:Root->GetArrayField(TEXT("rows")))
    {const auto R=V->AsObject();const auto& P=R->GetArrayField(TEXT("position"));auto* O=GetWorld()->SpawnActor<ALDWorldObject>();O->Kind=ELDObjectKind::TallGrass;O->SourceId=FName(*R->GetStringField(TEXT("id")));O->SourcePosition=FVector(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber());FVector Ground;
     if(!FitSource(O,O->SourcePosition,Ground)){Error=TEXT("No supported grass pad: ")+O->SourceId.ToString();O->Destroy();return false;}
     O->LastClear=Ground;O->Apply(Ground+FVector(0,0,78),0,1);Objects.Add(O);}
    return true;
}
bool ALDWorldObjects::InsideGrass(const ALDCharacter* Body) const
{
    if(!Body){return false;}const FVector Feet=Body->GetActorLocation()-FVector(0,0,Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    for(auto W:Objects){const auto* O=W.Get();if(O&&O->Kind==ELDObjectKind::TallGrass&&!O->Broken&&!O->BurnReceipt.IsValid()&&FVector::DistSquared2D(Feet,O->LastClear)<=250.*250&&FMath::Abs(Feet.Z-O->LastClear.Z)<=80){return true;}}
    return false;
}
void ALDWorldObjects::TickGrass(double Now)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    for(auto I=GrassBodies.CreateIterator();I;++I){if(!I.Key().IsValid()){I.RemoveCurrent();}}
    for(TActorIterator<ALDCharacter> I(GetWorld());I;++I)
    {auto* B=*I;auto* PC=Cast<ALDPlayerController>(B->GetController());const auto* O=PC?M->StateFor(PC):nullptr;const auto* C=M->Combat->StateFor(B);if(!O||!O->bActive||!C||C->Hp<=0){GrassBodies.Remove(B);continue;}
     auto& G=GrassBodies.FindOrAdd(B);if(G.Life!=C->Identity){G=FGrassBody();G.Life=C->Identity;}
     const bool Offense=C->Action==1||C->Action==2||C->Action==6||C->Action==7||C->Action==9||C->Action==10;
     if(Offense){G.RevealUntil=FMath::Max(G.RevealUntil,Now+2);G.ReadyAt=G.RevealUntil+1;}
     const bool Inside=InsideGrass(B)&&O->ChannelKind!=3;
     if(!Inside||!G.Inside){G.ReadyAt=FMath::Max(Now,G.RevealUntil)+1;}G.Inside=Inside;}
}
bool ALDWorldObjects::GrassConcealed(const ALDCharacter* Body) const
{
    const auto* G=GrassBodies.Find(Body);const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* C=Body?M->Combat->StateFor(Body):nullptr;auto* PC=Body?Cast<ALDPlayerController>(Body->GetController()):nullptr;const auto* O=PC?const_cast<ALDGameMode*>(M)->StateFor(PC):nullptr;
    return G&&C&&O&&O->bActive&&O->ChannelKind!=3&&C->Hp>0&&(C->Action==0||C->Action==8)&&G->Life==C->Identity&&G->Inside&&GetWorld()->GetTimeSeconds()+1.e-7>=G->ReadyAt&&InsideGrass(Body);
}
void ALDWorldObjects::InterruptGrass(ALDCharacter* Body,bool Offense)
{
    const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* C=Body?M->Combat->StateFor(Body):nullptr;if(!C){return;}auto& G=GrassBodies.FindOrAdd(Body);if(G.Life!=C->Identity){G=FGrassBody();G.Life=C->Identity;}
    const double Now=GetWorld()->GetTimeSeconds();if(Offense){G.RevealUntil=FMath::Max(G.RevealUntil,Now+2);}G.ReadyAt=FMath::Max(Now,G.RevealUntil)+1;
}
