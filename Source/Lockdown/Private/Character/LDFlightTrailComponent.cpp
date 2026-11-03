#include "Character/LDFlightTrailComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace LDFlightTrailDetail
{
    // Chalk white near the chalk, a pale cool grey at the tail: both stay far brighter than the lawn.
    const FLinearColor Dust(.98f, .98f, .95f), FadedDust(.86f, .88f, .9f);
    // /Engine/BasicShapes/Sphere is 100 cm across.
    constexpr float SphereDiameterCm = 100.f;
}

float LDFlightTrail::SpacingCm(float SpeedCmPerSec)
{
    return FMath::Min(FMath::Max(SpeedCmPerSec, 0.f) * SpacingPerSpeed, MaxSpacingCm);
}

void LDFlightTrail::Layout(float SpeedCmPerSec, TArray<FPuff>& Out)
{
    Out.Reset();
    const float Spacing = SpacingCm(SpeedCmPerSec);
    if (Spacing < MinSpacingCm) { return; }
    for (int32 Index = 0; Index < PuffCount; ++Index)
    {
        const float Along = float(Index) / float(PuffCount - 1);
        FPuff& Puff = Out.AddDefaulted_GetRef();
        Puff.DiameterCm = FMath::Lerp(HeadDiameterCm, TailDiameterCm, Along);
        Puff.OffsetCm = Spacing * (float(Index) + .5f);
    }
}

ULDFlightTrailComponent::ULDFlightTrailComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
    for (int32 Index = 0; Index < LDFlightTrail::PuffCount; ++Index)
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
        Instance->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(Dust, FadedDust, float(Index) / float(Puffs.Num() - 1)));
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
    TArray<LDFlightTrail::FPuff> Layout;
    LDFlightTrail::Layout(SpeedCmPerSec, Layout);
    Shown = Layout.Num();
    ShownLengthCm = Layout.IsEmpty() ? 0.f : Layout.Last().OffsetCm + Layout.Last().DiameterCm * .5f;
    for (int32 Index = 0; Index < Puffs.Num(); ++Index)
    {
        const bool bShown = Layout.IsValidIndex(Index);
        Puffs[Index]->SetHiddenInGame(!bShown);
        if (!bShown) { continue; }
        // Backward along the host's facing, thinning away from the chalk.
        Puffs[Index]->SetRelativeLocation(FVector(-Layout[Index].OffsetCm, 0, 0));
        Puffs[Index]->SetRelativeScale3D(FVector(Layout[Index].DiameterCm / SphereDiameterCm));
    }
}