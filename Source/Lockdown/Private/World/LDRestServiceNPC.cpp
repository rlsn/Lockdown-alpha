#include "World/LDRestServiceNPC.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "Engine/StaticMesh.h"

ALDRestServiceNPC::ALDRestServiceNPC()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    SetRootComponent(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Merchant(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Artisan(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Trainer(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    MerchantMesh=Merchant.Object;ArtisanMesh=Artisan.Object;TrainerMesh=Trainer.Object;
    Body->SetStaticMesh(MerchantMesh);
    Body->SetRelativeScale3D(FVector(.75, .75, 1.35));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ALDRestServiceNPC::SetService(ELDRestService Value)
{
    Service = Value;
    OnRep_Service();
    ForceNetUpdate();
}

void ALDRestServiceNPC::OnRep_Service()
{
    UStaticMesh* Mesh=MerchantMesh;
    FVector Scale(.75,.75,1.35);
    if(Service==ELDRestService::SkillMerchant)
    {Mesh=MerchantMesh;Scale=FVector(.75,.75,1.35);}
    else if(Service==ELDRestService::ComponentArtisan)
    {Mesh=ArtisanMesh;Scale=FVector(.8,.8,.9);}
    else if(Service==ELDRestService::Trainer)
    {Mesh=TrainerMesh;Scale=FVector(.75,.75,.75);}
    if(Body){Body->SetStaticMesh(Mesh);Body->SetRelativeScale3D(Scale);}
}

void ALDRestServiceNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ALDRestServiceNPC, Service);
}
