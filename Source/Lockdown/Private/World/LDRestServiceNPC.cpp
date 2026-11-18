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
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Trainer(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    TrainerMesh=Trainer.Object;
    Body->SetStaticMesh(TrainerMesh);
    Body->SetRelativeScale3D(FVector(.75, .75, .75));
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
    // The trainer is the one rest service left in the shared room (the shop lives in the workshop).
    if(Body){Body->SetStaticMesh(TrainerMesh);Body->SetRelativeScale3D(FVector(.75,.75,.75));}
}

void ALDRestServiceNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ALDRestServiceNPC, Service);
}
