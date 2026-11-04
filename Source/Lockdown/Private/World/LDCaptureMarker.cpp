#include "World/LDCaptureMarker.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    // Presentation-only ring dimensions; they do not change the approved capture radius or any rule.
    constexpr int32 LDCaptureMarkerSegments = 48;
    constexpr double LDCaptureMarkerWidthCm = 24.;
    constexpr double LDCaptureMarkerHeightCm = 2.;
    constexpr double LDCaptureMarkerLiftCm = .3;
    constexpr double LDCaptureMarkerJointOverlap = 1.04;
}

UInstancedStaticMeshComponent* LDGroundRing::Create(AActor& Owner, FName Name)
{
    // The cooked engine cube the generated world and the humanoid visuals already use.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    UInstancedStaticMeshComponent* Ring = Owner.CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
    Ring->SetMobility(EComponentMobility::Movable);
    Ring->SetStaticMesh(Cube.Object);
    Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ring->SetCollisionResponseToAllChannels(ECR_Ignore);
    Ring->SetGenerateOverlapEvents(false);
    Ring->SetCanEverAffectNavigation(false);
    Ring->SetCastShadow(false);
    return Ring;
}

bool LDGroundRing::Lay(UInstancedStaticMeshComponent* Ring, double RadiusCm)
{
    if (!Ring || !Ring->GetStaticMesh() || !FMath::IsFinite(RadiusCm) || RadiusCm <= LDCaptureMarkerWidthCm) { return false; }
    Ring->ClearInstances();
    // Chords whose ends lie on the approved circle; each joint overlaps slightly so the line reads as closed.
    const double Step = UE_DOUBLE_TWO_PI / LDCaptureMarkerSegments;
    const double Chord = 2. * RadiusCm * FMath::Sin(Step * .5);
    const double Mid = RadiusCm * FMath::Cos(Step * .5);
    for (int32 Index = 0; Index < LDCaptureMarkerSegments; ++Index)
    {
        const double Angle = Step * (Index + .5);
        const FVector Local(FMath::Cos(Angle) * Mid, FMath::Sin(Angle) * Mid, LDCaptureMarkerLiftCm + LDCaptureMarkerHeightCm * .5);
        const FVector Scale(Chord * LDCaptureMarkerJointOverlap / 100., LDCaptureMarkerWidthCm / 100., LDCaptureMarkerHeightCm / 100.);
        Ring->AddInstance(FTransform(FRotator(0., FMath::RadiansToDegrees(Angle) + 90., 0.), Local, Scale));
    }
    return true;
}

bool LDGroundRing::Paint(AActor& Owner, UInstancedStaticMeshComponent& Ring, UMaterialInstanceDynamic*& OutPaint, FString& Error)
{
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
    if (!Base)
    {
        Error = TEXT("The generated tint material is missing; rerun asset bootstrap.");
        return false;
    }
    OutPaint = UMaterialInstanceDynamic::Create(Base, &Owner);
    Ring.SetMaterial(0, OutPaint);
    return true;
}

bool LDGroundRing::IsPresentationOnly(const UInstancedStaticMeshComponent* Ring)
{
    return Ring && Ring->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Ring->GetGenerateOverlapEvents()
        && !Ring->CanEverAffectNavigation();
}

ALDCaptureMarker::ALDCaptureMarker()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetCanBeDamaged(false);
    Ring = LDGroundRing::Create(*this, TEXT("Ring"));
    RootComponent = Ring;
}

bool ALDCaptureMarker::Build(const FVector& CenterCm, double RadiusCm, uint32 InGeneration, FString& Error)
{
    if (!Ring || !Ring->GetStaticMesh() || !InGeneration || CenterCm.ContainsNaN()
        || !FMath::IsFinite(RadiusCm) || RadiusCm <= LDCaptureMarkerWidthCm)
    {
        Error = TEXT("The flagpole boundary needs the cooked ring mesh, the match generation and a positive approved radius.");
        return false;
    }
    UMaterialInstanceDynamic* NewPaint = nullptr;
    if (!LDGroundRing::Paint(*this, *Ring, NewPaint, Error)) { return false; }
    Paint = NewPaint;
    Center = CenterCm;
    Radius = RadiusCm;
    Generation = InGeneration;
    SetActorLocationAndRotation(CenterCm, FRotator::ZeroRotator);
    LDGroundRing::Lay(Ring, RadiusCm);
    ShowState(ELDCircleState::NotOpen);
    Error.Reset();
    return true;
}

void ALDCaptureMarker::ShowState(ELDCircleState State)
{
    Shown = State;
    if (Paint) { Paint->SetVectorParameterValue(TEXT("Tint"), ColorFor(State)); }
}

FLinearColor ALDCaptureMarker::ColorFor(ELDCircleState State)
{
    switch (State)
    {
    case ELDCircleState::Empty: return FLinearColor(1.f, .82f, .1f);
    case ELDCircleState::Solo: return FLinearColor(.12f, .78f, .3f);
    case ELDCircleState::Contested: return FLinearColor(1.f, .12f, .05f);
    default: return FLinearColor(.55f, .58f, .62f);
    }
}

int32 ALDCaptureMarker::GetSegmentCount() const
{
    return Ring ? Ring->GetInstanceCount() : 0;
}

bool ALDCaptureMarker::IsPresentationOnly() const
{
    return LDGroundRing::IsPresentationOnly(Ring);
}
