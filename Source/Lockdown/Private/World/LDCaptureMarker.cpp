#include "World/LDCaptureMarker.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Character/LDHumanoidVisualComponent.h"
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
    // The graffiti board on the flagpole: a thin upright panel above head height at the anchor.
    const FVector LDGraffitiBoardCm(8., 120., 90.);
    constexpr double LDGraffitiBoardLiftCm = 210.;
}

ALDCaptureMarker::ALDCaptureMarker()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetCanBeDamaged(false);
    // The cooked engine cube the generated world and the humanoid visuals already use.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    Ring = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Ring"));
    RootComponent = Ring;
    Ring->SetMobility(EComponentMobility::Movable);
    Ring->SetStaticMesh(Cube.Object);
    Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ring->SetCollisionResponseToAllChannels(ECR_Ignore);
    Ring->SetGenerateOverlapEvents(false);
    Ring->SetCanEverAffectNavigation(false);
    Ring->SetCastShadow(false);
    Graffiti = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Graffiti"));
    Graffiti->SetupAttachment(Ring);
    Graffiti->SetMobility(EComponentMobility::Movable);
    Graffiti->SetStaticMesh(Cube.Object);
    Graffiti->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Graffiti->SetCollisionResponseToAllChannels(ECR_Ignore);
    Graffiti->SetGenerateOverlapEvents(false);
    Graffiti->SetCanEverAffectNavigation(false);
    Graffiti->SetCastShadow(false);
    Graffiti->SetRelativeLocation(FVector(0., 0., LDGraffitiBoardLiftCm));
    Graffiti->SetRelativeScale3D(LDGraffitiBoardCm / 100.);
    Graffiti->SetVisibility(false);
}

bool ALDCaptureMarker::Build(const FVector& CenterCm, double RadiusCm, uint32 InGeneration, FString& Error)
{
    if (!Ring || !Ring->GetStaticMesh() || !InGeneration || CenterCm.ContainsNaN()
        || !FMath::IsFinite(RadiusCm) || RadiusCm <= LDCaptureMarkerWidthCm)
    {
        Error = TEXT("The flagpole boundary needs the cooked ring mesh, the match generation and a positive approved radius.");
        return false;
    }
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
    if (!Base)
    {
        Error = TEXT("The generated tint material is missing; rerun asset bootstrap.");
        return false;
    }
    Paint = UMaterialInstanceDynamic::Create(Base, this);
    Ring->SetMaterial(0, Paint);
    GraffitiPaint = UMaterialInstanceDynamic::Create(Base, this);
    if (Graffiti) { Graffiti->SetMaterial(0, GraffitiPaint); }
    Center = CenterCm;
    Radius = RadiusCm;
    Generation = InGeneration;
    SetActorLocationAndRotation(CenterCm, FRotator::ZeroRotator);
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
    ShowState(ELDCircleState::NotOpen);
    ShowGraffiti(false, 0);
    Error.Reset();
    return true;
}

void ALDCaptureMarker::ShowState(ELDCircleState State)
{
    Shown = State;
    if (Paint) { Paint->SetVectorParameterValue(TEXT("Tint"), ColorFor(State)); }
}

void ALDCaptureMarker::ShowGraffiti(bool bOwned, uint8 PresentationSlot)
{
    GraffitiSlot = bOwned ? PresentationSlot : 0;
    if (GraffitiPaint && bOwned) { GraffitiPaint->SetVectorParameterValue(TEXT("Tint"), ULDHumanoidVisualComponent::ContenderTint(PresentationSlot)); }
    if (Graffiti) { Graffiti->SetVisibility(bOwned); }
}

bool ALDCaptureMarker::IsGraffitiShown() const
{
    return Graffiti && Graffiti->IsVisible();
}

FLinearColor ALDCaptureMarker::ColorFor(ELDCircleState State)
{
    switch (State)
    {
    case ELDCircleState::Empty: return FLinearColor(1.f, .82f, .1f);
    case ELDCircleState::Solo: return FLinearColor(.12f, .78f, .3f);
    case ELDCircleState::Contested: return FLinearColor(1.f, .12f, .05f);
    case ELDCircleState::Guarded: return FLinearColor(.2f, .55f, 1.f);
    default: return FLinearColor(.55f, .58f, .62f);
    }
}

int32 ALDCaptureMarker::GetSegmentCount() const
{
    return Ring ? Ring->GetInstanceCount() : 0;
}

bool ALDCaptureMarker::IsPresentationOnly() const
{
    return Ring && Ring->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Ring->GetGenerateOverlapEvents()
        && !Ring->CanEverAffectNavigation() && Graffiti && Graffiti->GetCollisionEnabled() == ECollisionEnabled::NoCollision
        && !Graffiti->GetGenerateOverlapEvents() && !Graffiti->CanEverAffectNavigation();
}
