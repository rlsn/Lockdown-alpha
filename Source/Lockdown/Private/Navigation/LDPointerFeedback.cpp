#include "Navigation/LDPointerFeedback.h"
#include "Character/LDCharacter.h"
#include "Character/LDCameraSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Framework/LDPlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Vision/LDWorldViewRelay.h"

namespace LDPointerFeedbackDetail
{
    // Dashes per ring; a dash is a flat tangential bar over a slightly larger near-black shade that keeps it readable
    // on the turf, the track and any body tint.
    // The selection ring is short dense dashes (almost a dotted circle) so it never reads as the turning hover ring.
    // The attack-move ring is the move ring's twin in the attack red.
    constexpr int32 DashCount[] = {8, 8, 12, 20, 8};
    const FVector DashScale(.2f, .06f, .02f), ShadeScale(.28f, .14f, .01f);
    const FVector SelectedDashScale(.1f, .06f, .02f), SelectedShadeScale(.16f, .13f, .01f);
    // The hover ring is the attack marker's red: a target a right click would hit. The selection ring is a pale neutral,
    // since any visible unit (own body included) can be inspected.
    const FLinearColor MoveColour(.15f, 1.f, .25f), AttackColour(1.f, .06f, .04f), HoverColour = AttackColour, SelectedColour(.86f, .9f, 1.f);
    const FLinearColor ShadeColour(.012f, .01f, .016f);
    // Ring radii (cm): the hover ring stands outside every status disc at the feet; click rings shrink onto their point.
    constexpr float HoverRadius = 76.f, SelectedRadius = 64.f, MoveFromRadius = 50.f, MoveToRadius = 18.f, AttackFromRadius = 95.f, AttackToRadius = 55.f;
    // Above the ground and the own-body marker; dashes and shades never touch each other.
    constexpr float DashLift = 2.4f, ShadeLift = 1.4f;
    // Shift queue: a waypoint is a small static ring; the path between waypoints is a dash every QueueDashStepCm.
    constexpr int32 QueueRingDashes = 6;
    constexpr float QueueRingRadius = 24.f, QueueDashStepCm = 45.f;
    const FVector QueueRingScale(.12f, .05f, .02f), QueueRingShadeScale(.17f, .1f, .01f);
    const FVector QueuePathScale(.16f, .035f, .02f), QueuePathShadeScale(.2f, .075f, .01f);
}

ALDPointerFeedback::ALDPointerFeedback()
{
    using namespace LDPointerFeedbackDetail;
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = true;
    SetActorEnableCollision(false);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube"));
    UStaticMesh* Cube = CubeMesh.Object;
    const auto Make = [this, Cube](const FString& Name)
    {
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(FName(*Name));
        Part->SetupAttachment(RootComponent);
        Part->SetStaticMesh(Cube);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
        Part->SetCastShadow(false);
        Part->SetUsingAbsoluteLocation(true);
        Part->SetUsingAbsoluteRotation(true);
        Part->SetUsingAbsoluteScale(true);
        Part->SetHiddenInGame(true);
        Parts.Add(Part);
        return Part;
    };
    for (int32 Ring = 0; Ring < int32(ERing::Count); ++Ring)
    {
        for (int32 Dash = 0; Dash < DashCount[Ring]; ++Dash)
        {
            Rings[Ring].Dashes.Add(Make(FString::Printf(TEXT("Ring%dDash%d"), Ring, Dash)));
            Rings[Ring].Shades.Add(Make(FString::Printf(TEXT("Ring%dShade%d"), Ring, Dash)));
        }
    }
    for (int32 Bar = 0; Bar < 4; ++Bar) { Cross.Add(Make(FString::Printf(TEXT("Cross%d"), Bar))); }
    const auto MakeQueue = [this, Cube](const TCHAR* Name)
    {
        auto* Part = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
        Part->SetupAttachment(RootComponent);
        Part->SetStaticMesh(Cube);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
        Part->SetCastShadow(false);
        Part->SetUsingAbsoluteLocation(true);
        Part->SetUsingAbsoluteRotation(true);
        Part->SetUsingAbsoluteScale(true);
        return Part;
    };
    QueueMove = MakeQueue(TEXT("QueueMove"));
    QueueAttack = MakeQueue(TEXT("QueueAttack"));
    QueueShade = MakeQueue(TEXT("QueueShade"));
}

void ALDPointerFeedback::Bind(ALDPlayerController& Player)
{
    using namespace LDPointerFeedbackDetail;
    Viewer = &Player;
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
    if (!ensureMsgf(Material, TEXT("Character material missing; rerun asset bootstrap"))) { return; }
    const auto Paint = [this, Material](const FLinearColor& Colour)
    {
        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
        Instance->SetVectorParameterValue(TEXT("Tint"), Colour);
        return Instance;
    };
    UMaterialInstanceDynamic* Shade = Paint(ShadeColour);
    const FLinearColor Colours[] = {MoveColour, AttackColour, HoverColour, SelectedColour, AttackColour};
    static_assert(UE_ARRAY_COUNT(Colours) == int32(ERing::Count) && UE_ARRAY_COUNT(DashCount) == int32(ERing::Count), "Every ring has a colour and a dash count.");
    for (int32 Ring = 0; Ring < int32(ERing::Count); ++Ring)
    {
        UMaterialInstanceDynamic* Colour = Paint(Colours[Ring]);
        for (UStaticMeshComponent* Dash : Rings[Ring].Dashes) { Dash->SetMaterial(0, Colour); }
        for (UStaticMeshComponent* Under : Rings[Ring].Shades) { Under->SetMaterial(0, Shade); }
    }
    UMaterialInstanceDynamic* Refused = Paint(AttackColour);
    for (int32 Bar = 0; Bar < Cross.Num(); ++Bar) { Cross[Bar]->SetMaterial(0, Bar < 2 ? Refused : Shade); }
    QueueMove->SetMaterial(0, Paint(MoveColour));
    QueueAttack->SetMaterial(0, Refused);
    QueueShade->SetMaterial(0, Shade);
}

void ALDPointerFeedback::ShowMove(const FVector& GroundCm, bool bReachable, bool bAttack)
{
    MoveAt = GroundCm;
    MoveFrom = Clock;
    bMoveReachable = bReachable;
    bMoveAttack = bAttack;
}

void ALDPointerFeedback::ShowAttack(const FGuid& Handle, uint32 ObservationEpoch)
{
    AttackHandle = Handle;
    AttackEpoch = ObservationEpoch;
    AttackFrom = Clock;
}

void ALDPointerFeedback::SetHover(const FGuid& Handle, uint32 ObservationEpoch)
{
    HoverHandle = Handle;
    HoverEpoch = Handle.IsValid() ? ObservationEpoch : 0;
}

void ALDPointerFeedback::SetSelected(const FGuid& Handle, uint32 ObservationEpoch)
{
    SelectedHandle = Handle;
    SelectedEpoch = Handle.IsValid() ? ObservationEpoch : 0;
}

void ALDPointerFeedback::SetQueue(const TArray<FVector>& Waypoints, const TArray<bool>& Attack, const FVector* CurrentGoal)
{
    QueueWaypoints = Waypoints;
    QueueAttackFlags = Attack;
    QueueAttackFlags.SetNumZeroed(QueueWaypoints.Num());
    bQueueGoal = CurrentGoal != nullptr;
    QueueGoal = CurrentGoal ? *CurrentGoal : FVector::ZeroVector;
    PlaceQueue();
}

void ALDPointerFeedback::PlaceQueue()
{
    using namespace LDPointerFeedbackDetail;
    QueueMove->ClearInstances();
    QueueAttack->ClearInstances();
    QueueShade->ClearInstances();
    if (QueueWaypoints.IsEmpty()) { return; }
    TArray<FTransform> Move, Attack, Shade;
    const auto Dash = [&Shade](TArray<FTransform>& Into, const FVector& At, float Yaw, const FVector& Scale, const FVector& UnderScale)
    {
        const FQuat Along = FRotator(0, Yaw, 0).Quaternion();
        Into.Emplace(Along, At + FVector(0, 0, DashLift), Scale);
        Shade.Emplace(Along, At + FVector(0, 0, ShadeLift), UnderScale);
    };
    // The line starts at the own feet (when the body is presented) and runs through the current goal and each waypoint;
    // a segment takes the colour of the waypoint it leads to.
    TArray<FVector> Path;
    const APawn* Body = Viewer.IsValid() ? Viewer->GetPawn() : nullptr;
    if (const auto* Character = Cast<ALDCharacter>(Body))
    { Path.Add(Character->GetActorLocation() - FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())); }
    if (bQueueGoal) { Path.Add(QueueGoal); }
    const int32 Lead = Path.Num();
    Path.Append(QueueWaypoints);
    for (int32 Index = 1; Index < Path.Num(); ++Index)
    {
        const FVector From = Path[Index - 1], To = Path[Index];
        const FVector Span(To.X - From.X, To.Y - From.Y, To.Z - From.Z);
        const float Length = float(Span.Size2D());
        if (Length < QueueDashStepCm) { continue; }
        const float Yaw = float(FMath::RadiansToDegrees(FMath::Atan2(Span.Y, Span.X)));
        const int32 Waypoint = Index - Lead;
        TArray<FTransform>& Into = Waypoint >= 0 && QueueAttackFlags[Waypoint] ? Attack : Move;
        // Dashes stop short of both ends, so neither the feet nor a waypoint ring is crossed.
        for (float Along = QueueDashStepCm; Along <= Length - QueueDashStepCm * .5f; Along += QueueDashStepCm)
        { Dash(Into, From + Span * (Along / Length), Yaw, QueuePathScale, QueuePathShadeScale); }
    }
    for (int32 Index = 0; Index < QueueWaypoints.Num(); ++Index)
    {
        TArray<FTransform>& Into = QueueAttackFlags[Index] ? Attack : Move;
        for (int32 Part = 0; Part < QueueRingDashes; ++Part)
        {
            const float Angle = 360.f * float(Part) / float(QueueRingDashes);
            Dash(Into, QueueWaypoints[Index] + FRotator(0, Angle, 0).Vector() * QueueRingRadius, Angle + 90.f,
                QueueRingScale, QueueRingShadeScale);
        }
    }
    QueueMove->AddInstances(Move, false, true, false);
    QueueAttack->AddInstances(Attack, false, true, false);
    QueueShade->AddInstances(Shade, false, true, false);
}

bool ALDPointerFeedback::TargetFeet(const FGuid& Handle, uint32 Epoch, FVector& Out) const
{
    const ALDPlayerController* PC = Viewer.Get();
    const ALDWorldViewRelay* Relay = PC ? PC->LocalView() : nullptr;
    FVector Presented;
    if (!Handle.IsValid() || !Epoch || !Relay || !Relay->ReadPresentedLocation(Handle, Epoch, Presented)) { return false; }
    Out = Presented - FVector(0, 0, GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
    return true;
}

void ALDPointerFeedback::PlaceRing(ERing Ring, const FVector& Centre, float Radius, float Turn, float Scale)
{
    using namespace LDPointerFeedbackDetail;
    FRing& Shown = Rings[int32(Ring)];
    const int32 Count = Shown.Dashes.Num();
    const bool bSelectedRing = Ring == ERing::Selected;
    const FVector& Bar = bSelectedRing ? SelectedDashScale : DashScale;
    const FVector& Under = bSelectedRing ? SelectedShadeScale : ShadeScale;
    for (int32 Dash = 0; Dash < Count; ++Dash)
    {
        const float Angle = Turn + 360.f * float(Dash) / float(Count);
        const FVector Out = FRotator(0, Angle, 0).Vector() * Radius;
        const FRotator Along(0, Angle + 90.f, 0);
        Shown.Dashes[Dash]->SetWorldLocationAndRotation(Centre + Out + FVector(0, 0, DashLift), Along);
        Shown.Dashes[Dash]->SetWorldScale3D(Bar * Scale);
        Shown.Dashes[Dash]->SetHiddenInGame(Scale <= 0.f);
        Shown.Shades[Dash]->SetWorldLocationAndRotation(Centre + Out + FVector(0, 0, ShadeLift), Along);
        Shown.Shades[Dash]->SetWorldScale3D(Under * Scale);
        Shown.Shades[Dash]->SetHiddenInGame(Scale <= 0.f);
    }
}

void ALDPointerFeedback::HideRing(ERing Ring)
{
    for (UStaticMeshComponent* Dash : Rings[int32(Ring)].Dashes) { Dash->SetHiddenInGame(true); }
    for (UStaticMeshComponent* Under : Rings[int32(Ring)].Shades) { Under->SetHiddenInGame(true); }
}

void ALDPointerFeedback::Tick(float Delta)
{
    using namespace LDPointerFeedbackDetail;
    Super::Tick(Delta);
    Clock += FMath::Max(Delta, 0.f);
    const float Time = float(FMath::Fmod(Clock, 3600.0));
    // A move: a green ring shrinking onto the clicked point (red for an attack-move), or a red cross where the route is
    // already known to fail.
    const float MoveAge = MoveFrom >= 0 ? float(Clock - MoveFrom) : -1.f;
    bMoveShown = MoveAge >= 0.f && bMoveReachable && MoveAge < MoveSeconds;
    const ERing MoveRing = bMoveAttack ? ERing::AttackMove : ERing::Move;
    HideRing(bMoveAttack ? ERing::Move : ERing::AttackMove);
    if (bMoveShown)
    {
        const float K = MoveAge / MoveSeconds;
        PlaceRing(MoveRing, MoveAt, FMath::Lerp(MoveFromRadius, MoveToRadius, K), 140.f * MoveAge, 1.f - K * K);
    }
    else { HideRing(MoveRing); }
    const bool bCross = MoveAge >= 0.f && !bMoveReachable && MoveAge < CrossSeconds;
    for (int32 Bar = 0; Bar < Cross.Num(); ++Bar)
    {
        Cross[Bar]->SetHiddenInGame(!bCross);
        if (!bCross) { continue; }
        const float Fade = 1.f - FMath::Square(MoveAge / CrossSeconds);
        const bool bShade = Bar >= 2;
        Cross[Bar]->SetWorldLocationAndRotation(MoveAt + FVector(0, 0, bShade ? ShadeLift : DashLift), FRotator(0, LDCamera::Yaw + 90.f * (Bar % 2), 0));
        Cross[Bar]->SetWorldScale3D((bShade ? FVector(.6f, .15f, .01f) : FVector(.55f, .09f, .02f)) * Fade);
    }
    // An attack: a red ring closing on the target, wherever its proxy is presented.
    FVector Feet;
    const float AttackAge = AttackFrom >= 0 ? float(Clock - AttackFrom) : -1.f;
    bAttackShown = AttackAge >= 0.f && AttackAge < AttackSeconds && TargetFeet(AttackHandle, AttackEpoch, Feet);
    if (bAttackShown)
    {
        const float K = AttackAge / AttackSeconds;
        PlaceRing(ERing::Attack, Feet, FMath::Lerp(AttackFromRadius, AttackToRadius, K), -160.f * AttackAge, 1.f - K * K * K);
    }
    else { HideRing(ERing::Attack); }
    // The queue line follows the walking body.
    if (!QueueWaypoints.IsEmpty()) { PlaceQueue(); }
    bHoverShown = TargetFeet(HoverHandle, HoverEpoch, Feet);
    if (bHoverShown) { PlaceRing(ERing::Hover, Feet, HoverRadius, 30.f * Time, 1.f); }
    else { HideRing(ERing::Hover); }
    bSelectedShown = TargetFeet(SelectedHandle, SelectedEpoch, Feet);
    if (bSelectedShown) { PlaceRing(ERing::Selected, Feet, SelectedRadius, 0.f, 1.f); }
    else { HideRing(ERing::Selected); }
}
