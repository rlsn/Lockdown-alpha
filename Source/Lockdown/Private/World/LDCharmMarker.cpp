#include "World/LDCharmMarker.h"
#include "World/LDCaptureMarker.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
    // Presentation-only token: a cube stood on a corner, floating at chest height so it reads over the ring from the
    // MOBA camera. It does not change the approved pickup radius or any rule.
    constexpr double LDCharmTokenSizeCm = 40.;
    constexpr double LDCharmTokenLiftCm = 110.;
}

ALDCharmMarker::ALDCharmMarker()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetCanBeDamaged(false);
    Ring = LDGroundRing::Create(*this, TEXT("Ring"));
    RootComponent = Ring;
    Token = LDGroundRing::Create(*this, TEXT("Token"));
    Token->SetupAttachment(Ring);
}

bool ALDCharmMarker::Build(const FLDCharmView& View, double RadiusCm, const FLinearColor& InTint, uint32 InGeneration, FString& Error)
{
    if (!InGeneration || !View.Serial || View.Kind == ELDCharmKind::None || View.PointId.IsNone() || View.PositionCm.ContainsNaN()
        || !Token || !Token->GetStaticMesh() || !LDGroundRing::Lay(Ring, RadiusCm))
    {
        Error = TEXT("The charm marker needs a lying public charm, the match generation, the cooked ring mesh and a positive approved radius.");
        return false;
    }
    UMaterialInstanceDynamic* NewPaint = nullptr;
    if (!LDGroundRing::Paint(*this, *Ring, NewPaint, Error)) { return false; }
    Paint = NewPaint;
    Paint->SetVectorParameterValue(TEXT("Tint"), InTint);
    Token->SetMaterial(0, Paint);
    Token->ClearInstances();
    const double Scale = LDCharmTokenSizeCm / 100.;
    Token->AddInstance(FTransform(FRotator(45., 0., 45.), FVector(0., 0., LDCharmTokenLiftCm), FVector(Scale)));
    Center = View.PositionCm;
    Radius = RadiusCm;
    Tint = InTint;
    Generation = InGeneration;
    Serial = View.Serial;
    Kind = View.Kind;
    SetActorLocationAndRotation(Center, FRotator::ZeroRotator);
    Error.Reset();
    return true;
}

int32 ALDCharmMarker::GetSegmentCount() const
{
    return Ring ? Ring->GetInstanceCount() : 0;
}

bool ALDCharmMarker::IsTokenVisible() const
{
    return Token && Token->IsVisible() && Token->GetInstanceCount() == 1;
}

bool ALDCharmMarker::IsPresentationOnly() const
{
    return LDGroundRing::IsPresentationOnly(Ring) && LDGroundRing::IsPresentationOnly(Token);
}
