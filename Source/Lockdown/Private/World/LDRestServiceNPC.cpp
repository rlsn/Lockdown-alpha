#include "World/LDRestServiceNPC.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "Engine/StaticMesh.h"

ALDRestServiceNPC::ALDRestServiceNPC()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    SetRootComponent(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Body->SetStaticMesh(Mesh.Object);
    Body->SetWorldScale3D(FVector(.6, .6, 1.6));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Service"));
    Label->SetupAttachment(Body);
    Label->SetAbsolute(false, false, true);
    Label->SetRelativeLocation(FVector(0, 0, 90));
    Label->SetRelativeRotation(FRotator(60, 0, 0));
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetWorldSize(32);
}

void ALDRestServiceNPC::SetService(ELDRestService Value)
{
    Service = Value;
    OnRep_Service();
    ForceNetUpdate();
}

void ALDRestServiceNPC::OnRep_Service()
{
    const TCHAR* Name = Service == ELDRestService::SkillMerchant ? TEXT("1  SKILLS")
        : Service == ELDRestService::ComponentArtisan ? TEXT("2  COMPONENTS")
        : Service == ELDRestService::Trainer ? TEXT("3  TRAINER") : TEXT("");
    Label->SetText(FText::FromString(Name));
}

void ALDRestServiceNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ALDRestServiceNPC, Service);
}
