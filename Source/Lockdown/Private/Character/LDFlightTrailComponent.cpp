#include "Character/LDFlightTrailComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace LDFlightTrailDetail
{
    constexpr int32 PuffCount = 7;
    // Spacing follows the recent speed (a chalk at 1600 cm/s strings out to its cap), up to this many cm.
    constexpr float SpacingPerSpeed = .022f, MaxSpacingCm = 26.f, MinSpacingCm = 2.f;
    const FLinearColor Dust(.96f, .96f, .9f), FadedDust(.62f, .68f, .78f);
}

ULDFlightTrailComponent::ULDFlightTrailComponent()
{
    using namespace LDFlightTrailDetail;
    PrimaryComponentTick.bCanEverTick = true;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
    for (int32 Index = 0; Index < PuffCount; ++Index)
    {
        UStaticMeshComponent* Puff = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("TrailPuff%d"), Index));
        Puff->SetStaticMesh(SphereMesh.Object);
        Puff->SetupAttachment(this);
        Puff->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Puff->SetCanEverAffectNavigation(false);
        Puff->SetCastShadow(false);
        Puff->SetHiddenInGame(true);
        Puffs.Add(Puff);
    }
}

void ULDFlightTrailComponent::OnRegister()
{
    using namespace LDFlightTrailDetail;
    Super::OnRegister();
    if (!GetWorld()) { return; }
    for (UStaticMeshComponent* Puff : Puffs)
    {
        if (!Puff->IsRegistered()) { Puff->RegisterComponent(); }
    }
    if (bTinted) { return; }
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
    if (!ensureMsgf(Material, TEXT("Character material missing; rerun asset bootstrap"))) { return; }
    for (int32 Index = 0; Index < Puffs.Num(); ++Index)
    {
        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
        Instance->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(Dust, FadedDust, float(Index) / float(PuffCount - 1)));
        Puffs[Index]->SetMaterial(0, Instance);
    }
    bTinted = true;
}

void ULDFlightTrailComponent::TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    using namespace LDFlightTrailDetail;
    Super::TickComponent(Delta, TickType, ThisTickFunction);
    const FVector Here = GetComponentLocation();
    if (bHasLast && Delta > 0.f)
    {
        const float Instant = FVector::Dist(Here, LastWorld) / Delta;
        SpeedCmPerSec = FMath::Lerp(SpeedCmPerSec, Instant, 1.f - FMath::Exp(-20.f * Delta));
    }
    LastWorld = Here;
    bHasLast = true;
    const float Spacing = FMath::Min(SpeedCmPerSec * SpacingPerSpeed, MaxSpacingCm);
    const bool bShown = Spacing >= MinSpacingCm;
    for (int32 Index = 0; Index < Puffs.Num(); ++Index)
    {
        Puffs[Index]->SetHiddenInGame(!bShown);
        if (!bShown) { continue; }
        // Backward along the host's facing, thinning away from the chalk.
        Puffs[Index]->SetRelativeLocation(FVector(-Spacing * float(Index + 1), 0, 0));
        Puffs[Index]->SetRelativeScale3D(FVector(.13f - .015f * float(Index)));
    }
}
