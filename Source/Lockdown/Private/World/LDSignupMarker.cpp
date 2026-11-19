#include "World/LDSignupMarker.h"
#include "World/LDCaptureMarker.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

ALDSignupMarker::ALDSignupMarker()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetCanBeDamaged(false);
    Ring = LDGroundRing::Create(*this, TEXT("Signup"));
    RootComponent = Ring;
}

bool ALDSignupMarker::Build(const FVector& CentreCm, double RadiusCm, uint32 InGeneration, int32 InSlot, FString& Error)
{
    if (!InGeneration || InSlot < 0 || CentreCm.ContainsNaN() || !FMath::IsFinite(RadiusCm) || RadiusCm <= 0 || !Ring
        || !Ring->GetStaticMesh())
    {
        Error = TEXT("The sign-up disk needs the match generation, the own roster slot, its registered centre and the cooked ring mesh.");
        return false;
    }
    UMaterialInstanceDynamic* Paint = nullptr;
    if (!LDGroundRing::Paint(*this, *Ring, Paint, Error)) { return false; }
    RingPaint = Paint;
    SetActorLocationAndRotation(CentreCm, FRotator::ZeroRotator);
    if (!LDGroundRing::Lay(Ring, RadiusCm))
    {
        Error = FString::Printf(TEXT("The sign-up disk of %.0f cm cannot be drawn."), RadiusCm);
        return false;
    }
    Centre = CentreCm;
    Radius = RadiusCm;
    Generation = InGeneration;
    Slot = InSlot;
    ShownState = INDEX_NONE;
    Show(false, false);
    Error.Reset();
    return true;
}

void ALDSignupMarker::Show(bool bWindowOpen, bool bSignedUp)
{
    const int32 State = bSignedUp ? 2 : bWindowOpen ? 1 : 0;
    if (State == ShownState || !RingPaint) { return; }
    ShownState = State;
    RingPaint->SetVectorParameterValue(TEXT("Tint"), State == 2 ? SignedUpColor() : State == 1 ? OpenColor() : WaitingColor());
    Ring->SetVisibility(true);
}

FLinearColor ALDSignupMarker::WaitingColor()
{
    return FLinearColor(.45f, .4f, .55f);
}

FLinearColor ALDSignupMarker::OpenColor()
{
    // The PK violet of the arena ring: this disk leads into that PK.
    return FLinearColor(.72f, .22f, 1.f);
}

FLinearColor ALDSignupMarker::SignedUpColor()
{
    return FLinearColor(1.f, .78f, .2f);
}

bool ALDSignupMarker::IsPresentationOnly() const
{
    return LDGroundRing::IsPresentationOnly(Ring);
}
