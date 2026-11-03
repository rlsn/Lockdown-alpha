#include "Art/LDDecorationSet.h"
#include "Art/LDArtCatalog.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

ALDDecorationSet::ALDDecorationSet()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetCanBeDamaged(false);
    SetActorEnableCollision(false);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent->SetMobility(EComponentMobility::Movable);
}

bool ALDDecorationSet::Build(const FLDArtResolved& Art, FString& Error)
{
    if (!Parts.IsEmpty() || Art.DecorationProps.IsEmpty() || Art.DecorationProps.Num() != Art.DecorationTransforms.Num())
    {
        Error = TEXT("The decoration set needs one resolved, not yet placed decoration list.");
        return false;
    }
    TArray<const FLDArtProp*> Order;
    TMap<const FLDArtProp*, TArray<FTransform>> Placements;
    for (int32 Index = 0; Index < Art.DecorationProps.Num(); ++Index)
    {
        const FLDArtProp* Prop = Art.DecorationProps[Index];
        if (!Prop || !Prop->Mesh)
        {
            Error = FString::Printf(TEXT("Decoration %d has no resolved prop mesh."), Index);
            return false;
        }
        if (!Placements.Contains(Prop)) { Order.Add(Prop); }
        Placements.FindOrAdd(Prop).Add(Art.DecorationTransforms[Index]);
    }
    SetActorTransform(FTransform::Identity);
    for (const FLDArtProp* Prop : Order)
    {
        auto* Part = NewObject<UInstancedStaticMeshComponent>(this, FName(*(TEXT("Prop_") + Prop->Id)));
        Part->SetupAttachment(RootComponent);
        Part->SetMobility(EComponentMobility::Movable);
        Part->SetStaticMesh(Prop->Mesh);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCollisionResponseToAllChannels(ECR_Ignore);
        Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false);
        Part->bFillCollisionUnderneathForNavmesh = false;
        if (Prop->bFadeable) { Part->ComponentTags.Add(ULDArtCatalog::FadeableTag); }
        Part->RegisterComponent();
        // The actor sits at the world origin, so the catalog's world transforms are also local ones.
        Part->AddInstances(Placements[Prop], false, false, false);
        Parts.Add(Part);
    }
    Error.Reset();
    return true;
}

bool ALDDecorationSet::IsPresentationOnly() const
{
    TInlineComponentArray<UPrimitiveComponent*> Primitives(this);
    if (Primitives.IsEmpty()) { return false; }
    for (const UPrimitiveComponent* Primitive : Primitives)
    {
        if (Primitive->GetCollisionEnabled() != ECollisionEnabled::NoCollision || Primitive->GetGenerateOverlapEvents()
            || Primitive->CanEverAffectNavigation() || Primitive->IsNavigationRelevant())
        {
            return false;
        }
        for (int32 Channel = 0; Channel < int32(ECC_MAX); ++Channel)
        {
            if (Primitive->GetCollisionResponseToChannel(ECollisionChannel(Channel)) != ECR_Ignore) { return false; }
        }
    }
    return true;
}

int32 ALDDecorationSet::GetInstanceCount() const
{
    int32 Count = 0;
    for (const UInstancedStaticMeshComponent* Part : Parts) { Count += Part ? Part->GetInstanceCount() : 0; }
    return Count;
}
