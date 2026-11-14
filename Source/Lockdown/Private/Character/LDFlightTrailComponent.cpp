#include "Character/LDFlightTrailComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace LDFlightTrailDetail
{
    const FLinearColor Dust(.98f, .98f, .95f), FadedDust(.86f, .88f, .9f);
    const FLinearColor ReturnDust(.78f, .3f, 1.f), FadedReturnDust(.42f, .12f, .8f);
    // /Engine/BasicShapes/Sphere is 100 cm across.
    constexpr float SphereDiameterCm = 100.f;

    FLinearColor Tint(bool bReturning, float Along)
    {
        return FMath::Lerp(bReturning ? ReturnDust : Dust, bReturning ? FadedReturnDust : FadedDust, Along);
    }
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
        Instance->SetVectorParameterValue(TEXT("Tint"), Tint(bReturning, float(Index) / float(Puffs.Num() - 1)));
        Puffs[Index]->SetMaterial(0, Instance);
    }
    bTinted = true;
}

void ULDFlightTrailComponent::SetReturning(bool bReturn)
{
    using namespace LDFlightTrailDetail;
    if (bReturning == bReturn) { return; }
    bReturning = bReturn;
    if (!bTinted) { return; }
    for (int32 Index = 0; Index < Puffs.Num(); ++Index)
    {
        auto* Material = Cast<UMaterialInstanceDynamic>(Puffs[Index]->GetMaterial(0));
        if (!ensureMsgf(Material, TEXT("A flight trail lost its tint material"))) { return; }
        Material->SetVectorParameterValue(TEXT("Tint"), Tint(bReturning, float(Index) / float(Puffs.Num() - 1)));
    }
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