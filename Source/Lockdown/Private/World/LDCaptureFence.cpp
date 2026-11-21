#include "World/LDCaptureFence.h"
#include "Art/LDArtCatalog.h"
#include "Art/LDArtSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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
    // The blocks only collide; the panels draw the fence.
    Posts->SetVisibility(false);
    Panels = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Panels"));
    Panels->SetupAttachment(Posts);
    Panels->SetMobility(EComponentMobility::Movable);
    Panels->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Panels->SetGenerateOverlapEvents(false);
    Panels->SetCanEverAffectNavigation(false);
}

bool ALDCaptureFence::Build(const FVector& CenterCm, double RadiusCm, int64 InUntilMs, FString& Error)
{
    const FLDArtResolved* Art = ULDArtSubsystem::Get(this);
    UStaticMesh* Panel = Art && Art->CageFence ? Art->CageFence->Mesh.Get() : nullptr;
    // A dedicated server draws nothing: it raises only the collision.
    const bool bDrawn = !IsRunningDedicatedServer();
    if (!Posts || !Posts->GetStaticMesh() || !Panels || (bDrawn && !Panel) || CenterCm.ContainsNaN() || !FMath::IsFinite(RadiusCm)
        || RadiusCm <= LDFenceThicknessCm || InUntilMs <= 0)
    {
        Error = TEXT("The cage fence needs the cooked post mesh, the art catalog's fence panel, a positive zone radius and its end.");
        return false;
    }
    Center = CenterCm;
    Radius = RadiusCm;
    UntilMs = InUntilMs;
    SetActorLocationAndRotation(CenterCm, FRotator::ZeroRotator);
    Posts->ClearInstances();
    PanelTransforms.Reset();
    // The panel spans its wider horizontal axis; it is turned so that axis runs along the ring.
    const FBox Bounds = Panel ? Panel->GetBoundingBox() : FBox(FVector(-50, -4, 0), FVector(50, 4, LDFenceHeightCm));
    if (Panel) { Panels->SetStaticMesh(Panel); }
    const FVector Size = Bounds.GetSize();
    const bool bAlongY = Size.Y > Size.X;
    const double PanelWidth = FMath::Max(bAlongY ? Size.Y : Size.X, 1.);
    const double PanelBase = Bounds.Min.Z;
    // Overlapping chords on the boundary circle, centred on it so the inside and the outside lose the same width.
    const double Step = UE_DOUBLE_TWO_PI / LDFenceSegments;
    const double Chord = 2. * RadiusCm * FMath::Sin(Step * .5);
    const double Mid = RadiusCm * FMath::Cos(Step * .5);
    for (int32 Index = 0; Index < LDFenceSegments; ++Index)
    {
        const double Angle = Step * (Index + .5);
        const double Yaw = FMath::RadiansToDegrees(Angle) + 90.;
        const FVector Local(FMath::Cos(Angle) * Mid, FMath::Sin(Angle) * Mid, LDFenceHeightCm * .5);
        const FVector Scale(Chord * LDFenceJointOverlap / 100., LDFenceThicknessCm / 100., LDFenceHeightCm / 100.);
        Posts->AddInstance(FTransform(FRotator(0., Yaw, 0.), Local, Scale));
        const double Across = Chord * LDFenceJointOverlap / PanelWidth;
        PanelTransforms.Add(FTransform(FRotator(0., Yaw + (bAlongY ? 90. : 0.), 0.), FVector(Local.X, Local.Y, -PanelBase),
            bAlongY ? FVector(1., Across, 1.) : FVector(Across, 1., 1.)));
    }
    ShownIntegrityBp = -1;
    SetIntegrity(10000);
    Error.Reset();
    return true;
}

void ALDCaptureFence::SetIntegrity(int32 IntegrityBp)
{
    IntegrityBp = FMath::Clamp(IntegrityBp, 0, 10000);
    if (!Panels || IntegrityBp == ShownIntegrityBp) { return; }
    ShownIntegrityBp = IntegrityBp;
    // A fixed shuffle of the ring: the same panels fall first on every machine; a standing fence keeps at least a third.
    const int32 Count = PanelTransforms.Num();
    const int32 Shown = FMath::Clamp(FMath::CeilToInt32(Count * (1. + 2. * IntegrityBp / 10000.) / 3.), 0, Count);
    TArray<int32> Order;
    for (int32 Index = 0; Index < Count; ++Index) { Order.Add(Index); }
    FRandomStream Shuffle(0x0CA6E);
    for (int32 Index = Count - 1; Index > 0; --Index) { Order.Swap(Index, Shuffle.RandRange(0, Index)); }
    TArray<bool> Keep;
    Keep.Init(false, Count);
    for (int32 Index = 0; Index < Shown; ++Index) { Keep[Order[Index]] = true; }
    Panels->ClearInstances();
    for (int32 Index = 0; Index < Count; ++Index)
    {
        if (Keep[Index] && Panels->GetStaticMesh()) { Panels->AddInstance(PanelTransforms[Index]); }
    }
}

int32 ALDCaptureFence::GetPostCount() const
{
    return Posts ? Posts->GetInstanceCount() : 0;
}

int32 ALDCaptureFence::GetShownPanelCount() const
{
    return Panels ? Panels->GetInstanceCount() : 0;
}
