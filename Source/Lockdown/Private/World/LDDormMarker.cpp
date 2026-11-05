#include "World/LDDormMarker.h"
#include "World/LDCaptureMarker.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

ALDDormMarker::ALDDormMarker()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetCanBeDamaged(false);
    Ring = LDGroundRing::Create(*this, TEXT("Ring"));
    RootComponent = Ring;
}

bool ALDDormMarker::Build(FName InDormId, const FVector& CenterCm, double RadiusCm, uint32 InGeneration, FString& Error)
{
    if (InDormId.IsNone() || !InGeneration || CenterCm.ContainsNaN() || !LDGroundRing::Lay(Ring, RadiusCm))
    {
        Error = TEXT("The dorm boundary needs its dorm id, the match generation, the cooked ring mesh and a positive approved radius.");
        return false;
    }
    UMaterialInstanceDynamic* NewPaint = nullptr;
    if (!LDGroundRing::Paint(*this, *Ring, NewPaint, Error)) { return false; }
    Paint = NewPaint;
    Paint->SetVectorParameterValue(TEXT("Tint"), Color());
    DormId = InDormId;
    Center = CenterCm;
    Radius = RadiusCm;
    Generation = InGeneration;
    SetActorLocationAndRotation(CenterCm, FRotator::ZeroRotator);
    Error.Reset();
    return true;
}

FLinearColor ALDDormMarker::Color()
{
    // A calm blue, distinct from every capture circle state.
    return FLinearColor(.25f, .62f, 1.f);
}

int32 ALDDormMarker::GetSegmentCount() const
{
    return Ring ? Ring->GetInstanceCount() : 0;
}

bool ALDDormMarker::IsPresentationOnly() const
{
    return LDGroundRing::IsPresentationOnly(Ring);
}
