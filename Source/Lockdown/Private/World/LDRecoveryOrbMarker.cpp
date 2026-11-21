#include "World/LDRecoveryOrbMarker.h"
#include "World/LDCaptureMarker.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

ALDRecoveryOrbMarker::ALDRecoveryOrbMarker()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetCanBeDamaged(false);
    Ring = LDGroundRing::Create(*this, TEXT("Ring"));
    RootComponent = Ring;
    Core = LDGroundRing::Create(*this, TEXT("Core"));
    Core->SetupAttachment(Ring);
}

bool ALDRecoveryOrbMarker::Build(const FVector& CenterCm, double RadiusCm, uint64 InSerial, uint32 InGeneration, FString& Error)
{
    // Two painted rings, the pickup ring and a small bright core, in the healing green.
    if (!InSerial || !InGeneration || CenterCm.ContainsNaN() || !LDGroundRing::Lay(Ring, RadiusCm) || !LDGroundRing::Lay(Core, RadiusCm * .35))
    {
        Error = TEXT("The recovery orb marker needs a public orb, the match generation, the cooked ring mesh and a positive radius.");
        return false;
    }
    UMaterialInstanceDynamic* NewRing = nullptr;
    UMaterialInstanceDynamic* NewCore = nullptr;
    if (!LDGroundRing::Paint(*this, *Ring, NewRing, Error) || !LDGroundRing::Paint(*this, *Core, NewCore, Error)) { return false; }
    RingPaint = NewRing;
    CorePaint = NewCore;
    RingPaint->SetVectorParameterValue(TEXT("Tint"), FLinearColor(.25f, 1.f, .45f));
    CorePaint->SetVectorParameterValue(TEXT("Tint"), FLinearColor(.7f, 1.f, .8f));
    Serial = InSerial;
    Generation = InGeneration;
    SetActorLocationAndRotation(CenterCm, FRotator::ZeroRotator);
    Error.Reset();
    return true;
}
