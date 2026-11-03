#include "Navigation/LDPointerFeedback.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
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
    constexpr int32 DashCount[] = {8, 8, 12};
    const FVector DashScale(.2f, .06f, .02f), ShadeScale(.28f, .14f, .01f);
    // The hover ring is the attack marker's red: a target a right click would hit.
    const FLinearColor MoveColour(.15f, 1.f, .25f), AttackColour(1.f, .06f, .04f), HoverColour = AttackColour;
    const FLinearColor ShadeColour(.012f, .01f, .016f);
    // Ring radii (cm): the hover ring stands outside every status disc at the feet; click rings shrink onto their point.
    constexpr float HoverRadius = 76.f, MoveFromRadius = 50.f, MoveToRadius = 18.f, AttackFromRadius = 95.f, AttackToRadius = 55.f;
    // Above the ground and the own-body marker; dashes and shades never touch each other.
    constexpr float DashLift = 2.4f, ShadeLift = 1.4f;
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
    const FLinearColor Colours[] = {MoveColour, AttackColour, HoverColour};
    for (int32 Ring = 0; Ring < int32(ERing::Count); ++Ring)
    {
        UMaterialInstanceDynamic* Colour = Paint(Colours[Ring]);
        for (UStaticMeshComponent* Dash : Rings[Ring].Dashes) { Dash->SetMaterial(0, Colour); }
        for (UStaticMeshComponent* Under : Rings[Ring].Shades) { Under->SetMaterial(0, Shade); }
    }
    UMaterialInstanceDynamic* Refused = Paint(AttackColour);
    for (int32 Bar = 0; Bar < Cross.Num(); ++Bar) { Cross[Bar]->SetMaterial(0, Bar < 2 ? Refused : Shade); }
}

void ALDPointerFeedback::ShowMove(const FVector& GroundCm, bool bReachable)
{
    MoveAt = GroundCm;
    MoveFrom = Clock;
    bMoveReachable = bReachable;
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
    for (int32 Dash = 0; Dash < Count; ++Dash)
    {
        const float Angle = Turn + 360.f * float(Dash) / float(Count);
        const FVector Out = FRotator(0, Angle, 0).Vector() * Radius;
        const FRotator Along(0, Angle + 90.f, 0);
        Shown.Dashes[Dash]->SetWorldLocationAndRotation(Centre + Out + FVector(0, 0, DashLift), Along);
        Shown.Dashes[Dash]->SetWorldScale3D(DashScale * Scale);
        Shown.Dashes[Dash]->SetHiddenInGame(Scale <= 0.f);
        Shown.Shades[Dash]->SetWorldLocationAndRotation(Centre + Out + FVector(0, 0, ShadeLift), Along);
        Shown.Shades[Dash]->SetWorldScale3D(ShadeScale * Scale);
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
    // A move: a green ring shrinking onto the clicked point, or a red cross where the route is already known to fail.
    const float MoveAge = MoveFrom >= 0 ? float(Clock - MoveFrom) : -1.f;
    bMoveShown = MoveAge >= 0.f && bMoveReachable && MoveAge < MoveSeconds;
    if (bMoveShown)
    {
        const float K = MoveAge / MoveSeconds;
        PlaceRing(ERing::Move, MoveAt, FMath::Lerp(MoveFromRadius, MoveToRadius, K), 140.f * MoveAge, 1.f - K * K);
    }
    else { HideRing(ERing::Move); }
    const bool bCross = MoveAge >= 0.f && !bMoveReachable && MoveAge < CrossSeconds;
    for (int32 Bar = 0; Bar < Cross.Num(); ++Bar)
    {
        Cross[Bar]->SetHiddenInGame(!bCross);
        if (!bCross) { continue; }
        const float Fade = 1.f - FMath::Square(MoveAge / CrossSeconds);
        const bool bShade = Bar >= 2;
        Cross[Bar]->SetWorldLocationAndRotation(MoveAt + FVector(0, 0, bShade ? ShadeLift : DashLift), FRotator(0, -45.f + 90.f * (Bar % 2), 0));
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
    bHoverShown = TargetFeet(HoverHandle, HoverEpoch, Feet);
    if (bHoverShown) { PlaceRing(ERing::Hover, Feet, HoverRadius, 30.f * Time, 1.f); }
    else { HideRing(ERing::Hover); }
}
