#include "World/LDShowdownRingMarker.h"
#include "World/LDCaptureMarker.h"
#include "Character/LDHitVfx.h"
#include "NiagaraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

ALDShowdownRingMarker::ALDShowdownRingMarker()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetCanBeDamaged(false);
    Safe = LDGroundRing::Create(*this, TEXT("Safe"));
    RootComponent = Safe;
    Preview = LDGroundRing::Create(*this, TEXT("Preview"));
    Preview->SetupAttachment(Safe);
}

bool ALDShowdownRingMarker::Build(const FVector& CentreCm, uint32 InGeneration, uint32 InKey, const FLinearColor& Tint, FString& Error)
{
    if (!InGeneration || !InKey || CentreCm.ContainsNaN() || !Safe || !Safe->GetStaticMesh() || !Preview)
    {
        Error = TEXT("The ground circle needs the match generation, its key, a public centre and the cooked ring mesh.");
        return false;
    }
    UMaterialInstanceDynamic* NewSafe = nullptr;
    UMaterialInstanceDynamic* NewPreview = nullptr;
    if (!LDGroundRing::Paint(*this, *Safe, NewSafe, Error) || !LDGroundRing::Paint(*this, *Preview, NewPreview, Error)) { return false; }
    SafePaint = NewSafe;
    PreviewPaint = NewPreview;
    SafePaint->SetVectorParameterValue(TEXT("Tint"), Tint);
    PreviewPaint->SetVectorParameterValue(TEXT("Tint"), PreviewColor());
    Centre = CentreCm;
    Generation = InGeneration;
    Key = InKey;
    SetActorLocationAndRotation(CentreCm, FRotator::ZeroRotator);
    Safe->SetVisibility(false);
    Preview->SetVisibility(false);
    Error.Reset();
    return true;
}

bool ALDShowdownRingMarker::Relay(UInstancedStaticMeshComponent* Ring, double RadiusCm, double& InOutLaidCm)
{
    if (!Ring) { return false; }
    // The shrinking ring moves every frame; a sub-centimetre change is not worth re-laying the chords.
    if (InOutLaidCm > 0 && FMath::IsFinite(RadiusCm) && FMath::Abs(RadiusCm - InOutLaidCm) < 1.) { return true; }
    if (!LDGroundRing::Lay(Ring, RadiusCm))
    {
        Ring->ClearInstances();
        InOutLaidCm = 0;
        return false;
    }
    InOutLaidCm = RadiusCm;
    return true;
}

void ALDShowdownRingMarker::Show(double SafeRadiusCm, bool bPreview, double PreviewRadiusCm)
{
    SafeRadius = SafeRadiusCm;
    // The root's own visibility does not propagate, so the preview keeps its own.
    Safe->SetVisibility(Relay(Safe, SafeRadiusCm, LaidSafeCm));
    Preview->SetVisibility(bPreview && Relay(Preview, PreviewRadiusCm, LaidPreviewCm));
    SafeGlow = LDGroundRing::Glow(*Safe, SafeGlow, Safe->IsVisible(), uint8(LDSliceVfx::ESystem::RingDanger), GetActorLocation(), SafeRadiusCm, SafeColor());
    PreviewGlow = LDGroundRing::Glow(*Safe, PreviewGlow, Preview->IsVisible(), uint8(LDSliceVfx::ESystem::RingNext), GetActorLocation(), PreviewRadiusCm, PreviewColor());
}

FLinearColor ALDShowdownRingMarker::SafeColor()
{
    // Violet, distinct from the capture states, the dorm blue and the airdrop orange.
    return FLinearColor(.72f, .22f, 1.f);
}

FLinearColor ALDShowdownRingMarker::PreviewColor()
{
    // A muted lilac: quieter than the safe ring, and no broad white on the floor.
    return FLinearColor(.5f, .44f, .72f);
}

bool ALDShowdownRingMarker::IsPreviewShown() const
{
    return Preview && Preview->IsVisible();
}

bool ALDShowdownRingMarker::IsPresentationOnly() const
{
    return LDGroundRing::IsPresentationOnly(Safe) && LDGroundRing::IsPresentationOnly(Preview);
}
