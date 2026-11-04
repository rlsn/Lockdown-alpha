#include "World/LDAirdropMarker.h"
#include "World/LDCaptureMarker.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
    // Presentation-only crate size on the engine's 100 cm cube; it does not change the pickup radius or any rule.
    constexpr double LDAirdropCrateCm = 80.;
}

ALDAirdropMarker::ALDAirdropMarker()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetCanBeDamaged(false);
    Ring = LDGroundRing::Create(*this, TEXT("Ring"));
    RootComponent = Ring;
    Crate = LDGroundRing::Create(*this, TEXT("Crate"));
    Crate->SetupAttachment(Ring);
}

bool ALDAirdropMarker::Build(const FLDAirdropView& View, double RadiusCm, uint32 InGeneration, FString& Error)
{
    if (!InGeneration || !View.Serial || View.Phase == ELDAirdropPhase::None || View.PositionCm.ContainsNaN()
        || !Crate || !Crate->GetStaticMesh() || !LDGroundRing::Lay(Ring, RadiusCm))
    {
        Error = TEXT("The airdrop marker needs a public drop, the match generation, the cooked ring mesh and a positive approved radius.");
        return false;
    }
    UMaterialInstanceDynamic* NewRing = nullptr;
    UMaterialInstanceDynamic* NewCrate = nullptr;
    if (!LDGroundRing::Paint(*this, *Ring, NewRing, Error) || !LDGroundRing::Paint(*this, *Crate, NewCrate, Error)) { return false; }
    RingPaint = NewRing;
    CratePaint = NewCrate;
    RingPaint->SetVectorParameterValue(TEXT("Tint"), Color());
    CratePaint->SetVectorParameterValue(TEXT("Tint"), FLinearColor(.55f, .36f, .16f));
    Crate->ClearInstances();
    Crate->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0., 0., LDAirdropCrateCm * .5), FVector(LDAirdropCrateCm / 100.)));
    Center = View.PositionCm;
    Radius = RadiusCm;
    Generation = InGeneration;
    Serial = View.Serial;
    SetActorLocationAndRotation(Center, FRotator::ZeroRotator);
    ShowPhase(View.Phase);
    Error.Reset();
    return true;
}

void ALDAirdropMarker::ShowPhase(ELDAirdropPhase Phase)
{
    Shown = Phase;
    const bool bOnGround = Phase == ELDAirdropPhase::Announced || Phase == ELDAirdropPhase::Landed;
    Ring->SetVisibility(bOnGround);
    Crate->SetVisibility(Phase == ELDAirdropPhase::Landed);
}

FLinearColor ALDAirdropMarker::Color()
{
    // Warm orange, distinct from the capture states and the dorm blue.
    return FLinearColor(1.f, .45f, .05f);
}

bool ALDAirdropMarker::IsCrateVisible() const
{
    return Crate && Crate->IsVisible();
}

bool ALDAirdropMarker::IsPresentationOnly() const
{
    return LDGroundRing::IsPresentationOnly(Ring) && LDGroundRing::IsPresentationOnly(Crate);
}
