#include "World/LDCaptureFence.h"
#include "World/LDCaptureMarker.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    // Presentation dimensions of the posts; the rule is only that bodies cannot cross the approved zone boundary.
    constexpr int32 LDFenceSegments = 48;
    constexpr double LDFenceThicknessCm = 16.;
    constexpr double LDFenceHeightCm = 180.;
    constexpr double LDFenceJointOverlap = 1.06;
}

ALDCaptureFence::ALDCaptureFence()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetCanBeDamaged(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    Posts = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Posts"));
    RootComponent = Posts;
    Posts->SetMobility(EComponentMobility::Movable);
    Posts->SetStaticMesh(Cube.Object);
    Posts->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Posts->SetCollisionObjectType(ECC_WorldStatic);
    Posts->SetCollisionResponseToAllChannels(ECR_Ignore);
    Posts->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    Posts->SetGenerateOverlapEvents(false);
    Posts->SetCanEverAffectNavigation(false);
    Posts->SetCastShadow(false);
}

bool ALDCaptureFence::Build(const FVector& CenterCm, double RadiusCm, int64 InUntilMs, FString& Error)
{
    if (!Posts || !Posts->GetStaticMesh() || CenterCm.ContainsNaN() || !FMath::IsFinite(RadiusCm) || RadiusCm <= LDFenceThicknessCm
        || InUntilMs <= 0)
    {
        Error = TEXT("The cage fence needs the cooked post mesh, a positive zone radius and its end.");
        return false;
    }
    if (!Paint)
    {
        UMaterialInstanceDynamic* NewPaint = nullptr;
        if (!LDGroundRing::Paint(*this, *Posts, NewPaint, Error)) { return false; }
        Paint = NewPaint;
        Paint->SetVectorParameterValue(TEXT("Tint"), FLinearColor(.85f, .3f, .95f));
    }
    Center = CenterCm;
    Radius = RadiusCm;
    UntilMs = InUntilMs;
    SetActorLocationAndRotation(CenterCm, FRotator::ZeroRotator);
    Posts->ClearInstances();
    // Overlapping chords on the boundary circle, centred on it so the inside and the outside lose the same width.
    const double Step = UE_DOUBLE_TWO_PI / LDFenceSegments;
    const double Chord = 2. * RadiusCm * FMath::Sin(Step * .5);
    const double Mid = RadiusCm * FMath::Cos(Step * .5);
    for (int32 Index = 0; Index < LDFenceSegments; ++Index)
    {
        const double Angle = Step * (Index + .5);
        const FVector Local(FMath::Cos(Angle) * Mid, FMath::Sin(Angle) * Mid, LDFenceHeightCm * .5);
        const FVector Scale(Chord * LDFenceJointOverlap / 100., LDFenceThicknessCm / 100., LDFenceHeightCm / 100.);
        Posts->AddInstance(FTransform(FRotator(0., FMath::RadiansToDegrees(Angle) + 90., 0.), Local, Scale));
    }
    Error.Reset();
    return true;
}

int32 ALDCaptureFence::GetPostCount() const
{
    return Posts ? Posts->GetInstanceCount() : 0;
}
