#include "Vision/LDWorldViewRelay.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Character/LDCharacter.h"
#include "Framework/LDPlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ALDObservedEntityProxy::ALDObservedEntityProxy()
{
    bReplicates=false;
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Body=CreateDefaultSubobject<ULDHumanoidVisualComponent>(TEXT("NeutralBody")); Body->SetupAttachment(RootComponent);
    WardMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ward")); WardMesh->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    WardMesh->SetStaticMesh(Sphere.Object); WardMesh->SetRelativeScale3D(FVector(.24));
    WardMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); WardMesh->SetCanEverAffectNavigation(false);
}
void ALDObservedEntityProxy::Apply(const FLDObservedEntity& Entity)
{
    ObservedId=Entity.Id; Destination=Entity.Position;
    Facing=float(Entity.Facing)/65535*360; Speed=Entity.Speed;
    Body->SetCue(Entity.ActionCue);
    if (bFirst || FVector::DistSquared(GetActorLocation(),Destination)>300*300) { SetActorLocation(Destination); }
    if (Palette!=Entity.Presentation)
    {
        Palette=Entity.Presentation; Body->ApplyPalette(Palette);
        UMaterialInterface* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
        UMaterialInstanceDynamic* Instance=UMaterialInstanceDynamic::Create(Material,this);
        Instance->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.6f,.65f,.2f)); WardMesh->SetMaterial(0,Instance);
    }
    Body->SetVisibility(Entity.Kind==ELDObservedKind::Body,true);
    WardMesh->SetVisibility(Entity.Kind==ELDObservedKind::Ward);
    bFirst=false;
}
void ALDObservedEntityProxy::Tick(float Delta)
{
    Super::Tick(Delta);
    SetActorLocation(FMath::VInterpTo(GetActorLocation(),Destination,Delta,18));
    SetActorRotation(FRotator(0,Facing,0)); Body->Animate(FVector(Speed,0,0),Delta);
}

ALDWorldViewRelay::ALDWorldViewRelay()
{
    bReplicates=true; bOnlyRelevantToOwner=true; bAlwaysRelevant=false;
    SetNetUpdateFrequency(10); PrimaryActorTick.bCanEverTick=true;
}
void ALDWorldViewRelay::ClearProxies()
{ for (auto& Pair : Proxies) { if (IsValid(Pair.Value)) { Pair.Value->Destroy(); } } Proxies.Empty(); }
void ALDWorldViewRelay::EndPlay(const EEndPlayReason::Type Reason)
{ ClearProxies(); Super::EndPlay(Reason); }
void ALDWorldViewRelay::ClientFullView_Implementation(const FLDWorldView& View) { ApplyFullView(View); }
void ALDWorldViewRelay::ClientConceal_Implementation(uint32 Generation,uint64 Revision,const TArray<FGuid>& Removed) { ApplyConceal(Generation,Revision,Removed); }
void ALDWorldViewRelay::ApplyFullView(const FLDWorldView& View)
{
    if (View.Current.Num()!=LDVision::MaskBytes || View.Explored.Num()!=LDVision::MaskBytes || View.Entities.Num()>LDVision::MaxEntities) { return; }
    const uint32 PreviousGeneration=Cursor.Generation;
    if (!Cursor.Accept(View.Generation,View.Revision)) { return; }
    if (PreviousGeneration!=View.Generation) { ClearProxies(); }
    Received=View; LastFullViewAt=FPlatformTime::Seconds(); bStale=false;
    TSet<FGuid> Present;
    for (const auto& Entity : View.Entities)
    {
        Present.Add(Entity.Id);
        TObjectPtr<ALDObservedEntityProxy>& Proxy=Proxies.FindOrAdd(Entity.Id);
        if (!IsValid(Proxy)) { Proxy=GetWorld()->SpawnActor<ALDObservedEntityProxy>(); }
        if (Proxy) { Proxy->Apply(Entity); }
    }
    for (auto It=Proxies.CreateIterator();It;++It) { if (!Present.Contains(It.Key())) { if (IsValid(It.Value())) { It.Value()->Destroy(); } It.RemoveCurrent(); } }
    UploadFog();
}
void ALDWorldViewRelay::ApplyConceal(uint32 Generation,uint64 Revision,const TArray<FGuid>& Removed)
{
    const uint32 PreviousGeneration=Cursor.Generation;
    if (Removed.Num()>LDVision::MaxEntities || !Cursor.Accept(Generation,Revision)) { return; }
    if (PreviousGeneration!=Generation) { ClearProxies(); Received=FLDWorldView(); bStale=true; LastFullViewAt=-1; UploadFog(); }
    for (const FGuid& Id : Removed)
    { if (TObjectPtr<ALDObservedEntityProxy>* Proxy=Proxies.Find(Id)) { if (IsValid(*Proxy)) { (*Proxy)->Destroy(); } Proxies.Remove(Id); } }
    Received.Entities.RemoveAll([&](const FLDObservedEntity& Entity){return Removed.Contains(Entity.Id);});
}
void ALDWorldViewRelay::AttachFog()
{
    ALDPlayerController* PC=Cast<ALDPlayerController>(GetOwner());
    AActor* Target=PC?PC->GetViewTarget():nullptr;
    UCameraComponent* Camera=Target?Target->FindComponentByClass<UCameraComponent>():nullptr;
    if (!PC || !PC->IsLocalController() || !Camera || CameraOwner==Target) { return; }
    if (!FogMaterial)
    {
        UMaterialInterface* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Lockdown/Generated/Materials/M_Fog.M_Fog"));
        if (!ensureMsgf(Base,TEXT("Missing native fog material; regenerate content"))) { return; }
        FogTexture=UTexture2D::CreateTransient(60,40,PF_B8G8R8A8);
        FogTexture->SRGB=false; FogTexture->Filter=TF_Nearest;
        FogTexture->AddressX=TA_Clamp; FogTexture->AddressY=TA_Clamp; FogTexture->NeverStream=true;
        void* Pixels=FogTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memzero(Pixels,60*40*4); FogTexture->GetPlatformData()->Mips[0].BulkData.Unlock(); FogTexture->UpdateResource();
        FogMaterial=UMaterialInstanceDynamic::Create(Base,this); FogMaterial->SetTextureParameterValue(TEXT("FogMask"),FogTexture);
    }
    Camera->PostProcessSettings.AddBlendable(FogMaterial,1);
    CameraOwner=Target; UploadFog();
}
void ALDWorldViewRelay::UploadFog()
{
    if (!FogTexture || !FogTexture->GetResource()) { return; }
    uint8* Pixels=new uint8[LDVision::Cells*4];
    for (int32 I=0;I<LDVision::Cells;++I)
    {
        const uint8 Value=(!bStale && LDVision::Bit(Received.Current,I)) ? 255 : (LDVision::Bit(Received.Explored,I) ? 64 : 0);
        Pixels[I*4]=Pixels[I*4+1]=Pixels[I*4+2]=Value; Pixels[I*4+3]=255;
    }
    FogTexture->UpdateTextureRegions(0,1,new FUpdateTextureRegion2D(0,0,0,0,60,40),60*4,4,Pixels,
        [](uint8* Data,const FUpdateTextureRegion2D* Regions){delete[] Data;delete Regions;});
}
void ALDWorldViewRelay::Tick(float Delta)
{
    Super::Tick(Delta);
    const ALDPlayerController* PC=Cast<ALDPlayerController>(GetOwner());
    if (!PC || !PC->IsLocalController()) { return; }
    AttachFog();
    if (!bStale && FPlatformTime::Seconds()-LastFullViewAt>.25)
    { bStale=true; ClearProxies(); Received.Entities.Empty(); Received.Current.Init(0,LDVision::MaskBytes); UploadFog(); }
}
