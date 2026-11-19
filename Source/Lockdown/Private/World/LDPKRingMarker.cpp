#include "World/LDPKRingMarker.h"
#include "World/LDCaptureMarker.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

ALDPKRingMarker::ALDPKRingMarker()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetCanBeDamaged(false);
    Safe = LDGroundRing::Create(*this, TEXT("Safe"));
    RootComponent = Safe;
    Preview = LDGroundRing::Create(*this, TEXT("Preview"));
    Preview->SetupAttachment(Safe);
    for (int32 Index = 0; Index < MaxThrows; ++Index)
    {
        UInstancedStaticMeshComponent* Throw = LDGroundRing::Create(*this, FName(*FString::Printf(TEXT("Throw%d"), Index)));
        Throw->SetupAttachment(Safe);
        Throws.Add(Throw);
    }
    LaidThrowCm.SetNumZeroed(MaxThrows);
}

bool ALDPKRingMarker::Build(const FVector& CentreCm, uint32 InGeneration, uint32 InSerial, FString& Error)
{
    if (!InGeneration || !InSerial || CentreCm.ContainsNaN() || !Safe || !Safe->GetStaticMesh() || !Preview
        || Throws.Num() != MaxThrows || Throws.ContainsByPredicate([](const TObjectPtr<UInstancedStaticMeshComponent>& Throw) { return !Throw; }))
    {
        Error = TEXT("The PK ring needs the match generation, the PK serial, a public arena centre and the cooked ring mesh.");
        return false;
    }
    UMaterialInstanceDynamic* NewSafe = nullptr;
    UMaterialInstanceDynamic* NewPreview = nullptr;
    UMaterialInstanceDynamic* NewThrow = nullptr;
    if (!LDGroundRing::Paint(*this, *Safe, NewSafe, Error) || !LDGroundRing::Paint(*this, *Preview, NewPreview, Error)
        || !LDGroundRing::Paint(*this, *Throws[0], NewThrow, Error))
    {
        return false;
    }
    SafePaint = NewSafe;
    PreviewPaint = NewPreview;
    ThrowPaint = NewThrow;
    SafePaint->SetVectorParameterValue(TEXT("Tint"), SafeColor());
    PreviewPaint->SetVectorParameterValue(TEXT("Tint"), PreviewColor());
    ThrowPaint->SetVectorParameterValue(TEXT("Tint"), ThrowColor());
    for (int32 Index = 1; Index < Throws.Num(); ++Index) { Throws[Index]->SetMaterial(0, ThrowPaint); }
    Centre = CentreCm;
    Generation = InGeneration;
    Serial = InSerial;
    SetActorLocationAndRotation(CentreCm, FRotator::ZeroRotator);
    Safe->SetVisibility(false);
    Preview->SetVisibility(false);
    for (UInstancedStaticMeshComponent* Throw : Throws) { Throw->SetVisibility(false); }
    Error.Reset();
    return true;
}

bool ALDPKRingMarker::Relay(UInstancedStaticMeshComponent* Ring, double RadiusCm, double& InOutLaidCm)
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

void ALDPKRingMarker::Show(double SafeRadiusCm, bool bPreview, double PreviewRadiusCm, const TArray<FVector>& ThrowsCm, double ThrowRadiusCm)
{
    SafeRadius = SafeRadiusCm;
    // The root's own visibility does not propagate, so the preview and throws keep theirs.
    Safe->SetVisibility(Relay(Safe, SafeRadiusCm, LaidSafeCm));
    Preview->SetVisibility(bPreview && Relay(Preview, PreviewRadiusCm, LaidPreviewCm));
    for (int32 Index = 0; Index < Throws.Num(); ++Index)
    {
        UInstancedStaticMeshComponent* Ring = Throws[Index];
        const bool bShown = ThrowsCm.IsValidIndex(Index) && !ThrowsCm[Index].ContainsNaN() && Relay(Ring, ThrowRadiusCm, LaidThrowCm[Index]);
        if (bShown) { Ring->SetRelativeLocation(ThrowsCm[Index] - Centre); }
        Ring->SetVisibility(bShown);
    }
}

FLinearColor ALDPKRingMarker::SafeColor()
{
    // Violet, distinct from the capture states, the dorm blue and the airdrop orange.
    return FLinearColor(.72f, .22f, 1.f);
}

FLinearColor ALDPKRingMarker::PreviewColor()
{
    return FLinearColor(.92f, .85f, 1.f);
}

FLinearColor ALDPKRingMarker::ThrowColor()
{
    return FLinearColor(1.f, .2f, .55f);
}

bool ALDPKRingMarker::IsPreviewShown() const
{
    return Preview && Preview->IsVisible();
}

int32 ALDPKRingMarker::GetShownThrows() const
{
    int32 Shown = 0;
    for (const UInstancedStaticMeshComponent* Throw : Throws) { Shown += Throw && Throw->IsVisible() ? 1 : 0; }
    return Shown;
}

bool ALDPKRingMarker::IsPresentationOnly() const
{
    bool bOnly = LDGroundRing::IsPresentationOnly(Safe) && LDGroundRing::IsPresentationOnly(Preview);
    for (const UInstancedStaticMeshComponent* Throw : Throws) { bOnly = bOnly && LDGroundRing::IsPresentationOnly(Throw); }
    return bOnly;
}
