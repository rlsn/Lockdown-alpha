#include "World/LDAirdropMarker.h"
#include "World/LDCaptureMarker.h"
#include "Art/LDArtSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

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
    // #97: the landed crate is the catalog's crate prop with its own art materials; without the catalog no crate is drawn.
    const FLDArtResolved* Art = ULDArtSubsystem::Get(this);
    if (!InGeneration || !View.Serial || View.Phase == ELDAirdropPhase::None || View.PositionCm.ContainsNaN()
        || !Crate || !Art || !Art->AirdropCrate || !Art->AirdropCrate->Mesh || !LDGroundRing::Lay(Ring, RadiusCm))
    {
        Error = TEXT("The airdrop marker needs a public drop, the match generation, the art crate, the cooked ring mesh and a positive approved radius.");
        return false;
    }
    UMaterialInstanceDynamic* NewRing = nullptr;
    if (!LDGroundRing::Paint(*this, *Ring, NewRing, Error)) { return false; }
    RingPaint = NewRing;
    RingPaint->SetVectorParameterValue(TEXT("Tint"), Color());
    Crate->SetStaticMesh(Art->AirdropCrate->Mesh);
    Crate->EmptyOverrideMaterials();
    Crate->ClearInstances();
    Crate->AddInstance(Art->AirdropTransform);
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
