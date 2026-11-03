#include "Character/LDHumanoidVisualComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Development/LDDemoContentIds.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace LDHumanoidVisualDetail
{
    // Part order is fixed; indices below are used by Animate and ApplyLook.
    enum EPart : int32
    {
        Torso, Head, ArmLeft, ArmRight, LegLeft, LegRight, Facing,
        RootRing, ShieldHalo, ProtectionBeam, ReturnSign, Armband, Clipboard, CapCrown, CapBill, Whistle,
        ShackleLeft, ShackleRight, RootRim,
        ShieldBar0, ShieldBar1, ShieldBar2, ShieldBar3, ShieldBar4, ShieldBar5, ShieldBar6, ShieldBar7,
        ProtectionOrb0, ProtectionOrb1, ProtectionOrb2, ProtectionOrb3,
        Burst, Charge, SpawnColumn, PartCount
    };
    constexpr int32 ShieldBars = 8, ProtectionOrbs = 4;
    enum EShape : uint8 { Cube, Sphere, Cylinder };
    struct FPartSpec
    {
        const TCHAR* Name;
        EShape Shape;
        int32 Parent;     // INDEX_NONE: the pivot
        FVector Location; // pivot space at rest
        FVector Scale;    // world-relative size in BasicShapes units (100 cm)
    };
    const FPartSpec Specs[PartCount] =
    {
        {TEXT("Torso"), Cube, INDEX_NONE, {0,0,9}, {.34,.4,.58}},
        {TEXT("Head"), Sphere, INDEX_NONE, {0,0,65}, {.32,.3,.34}},
        {TEXT("ArmLeft"), Cube, INDEX_NONE, {0,-25,6}, {.16,.15,.55}},
        {TEXT("ArmRight"), Cube, INDEX_NONE, {0,25,6}, {.16,.15,.55}},
        {TEXT("LegLeft"), Cube, INDEX_NONE, {0,-11,-53}, {.18,.18,.65}},
        {TEXT("LegRight"), Cube, INDEX_NONE, {0,11,-53}, {.18,.18,.65}},
        {TEXT("Facing"), Cube, INDEX_NONE, {19,0,65}, {.12,.12,.12}},
        // Status markers: root is a pulsing disc on a dark rim at the feet with ankle shackles, shield a turning cage of
        // bars under a halo, protection a beam with orbiting orbs, camp return a sign on the back. The feet discs stand
        // above the own-body marker's discs.
        {TEXT("RootRing"), Cylinder, INDEX_NONE, {0,0,-87}, {1.05,1.05,.03}},
        {TEXT("ShieldHalo"), Cylinder, INDEX_NONE, {0,0,97}, {.62,.62,.025}},
        {TEXT("ProtectionBeam"), Cylinder, INDEX_NONE, {0,0,160}, {.14,.14,1.3}},
        {TEXT("ReturnSign"), Cube, INDEX_NONE, {-24,0,92}, {.04,.34,.3}},
        // Camp NPC accessories; the armband and clipboard follow the arms, the cap and whistle the head and torso.
        {TEXT("Armband"), Cube, ArmLeft, {0,-25,22}, {.19,.18,.1}},
        {TEXT("Clipboard"), Cube, ArmRight, {12,27,-8}, {.03,.26,.34}},
        {TEXT("CapCrown"), Cylinder, Head, {0,0,80}, {.34,.34,.1}},
        {TEXT("CapBill"), Cube, Head, {18,0,76}, {.2,.3,.03}},
        {TEXT("Whistle"), Sphere, Torso, {18,0,30}, {.07,.07,.07}},
        {TEXT("ShackleLeft"), Cylinder, INDEX_NONE, {0,-11,-74}, {.3,.3,.08}},
        {TEXT("ShackleRight"), Cylinder, INDEX_NONE, {0,11,-74}, {.3,.3,.08}},
        {TEXT("RootRim"), Cylinder, INDEX_NONE, {0,0,-87.2}, {1.25,1.25,.02}},
        {TEXT("ShieldBar0"), Cylinder, INDEX_NONE, {56,0,2}, {.06,.06,1.8}},
        {TEXT("ShieldBar1"), Cylinder, INDEX_NONE, {39.6,39.6,2}, {.06,.06,1.8}},
        {TEXT("ShieldBar2"), Cylinder, INDEX_NONE, {0,56,2}, {.06,.06,1.8}},
        {TEXT("ShieldBar3"), Cylinder, INDEX_NONE, {-39.6,39.6,2}, {.06,.06,1.8}},
        {TEXT("ShieldBar4"), Cylinder, INDEX_NONE, {-56,0,2}, {.06,.06,1.8}},
        {TEXT("ShieldBar5"), Cylinder, INDEX_NONE, {-39.6,-39.6,2}, {.06,.06,1.8}},
        {TEXT("ShieldBar6"), Cylinder, INDEX_NONE, {0,-56,2}, {.06,.06,1.8}},
        {TEXT("ShieldBar7"), Cylinder, INDEX_NONE, {39.6,-39.6,2}, {.06,.06,1.8}},
        {TEXT("ProtectionOrb0"), Sphere, INDEX_NONE, {62,0,-5}, {.13,.13,.13}},
        {TEXT("ProtectionOrb1"), Sphere, INDEX_NONE, {0,62,-5}, {.13,.13,.13}},
        {TEXT("ProtectionOrb2"), Sphere, INDEX_NONE, {-62,0,-5}, {.13,.13,.13}},
        {TEXT("ProtectionOrb3"), Sphere, INDEX_NONE, {0,-62,-5}, {.13,.13,.13}},
        // Action and life effects: the impact spark, the cast orb and the appear column.
        {TEXT("Burst"), Sphere, INDEX_NONE, {35,25,40}, {.1,.1,.1}},
        {TEXT("Charge"), Sphere, INDEX_NONE, {25,0,100}, {.1,.1,.1}},
        {TEXT("SpawnColumn"), Cylinder, INDEX_NONE, {0,0,70}, {.9,.9,3.2}},
    };
    const FLinearColor Skin(.68f,.47f,.3f);
    // One fixed colour per status on every unit, apart from all six seat tints, the turf and the red track:
    // root hot magenta, shield bright cyan, protection white-gold; each edged in near-black.
    const FLinearColor RootColour(1.f,.02f,.58f), EdgeColour(.012f,.01f,.016f), ShieldColour(0.f,.78f,1.f);
    const FLinearColor ProtectionColour(1.f,.92f,.62f), ReturnColour(.95f,.95f,.95f);
    const FLinearColor ArmbandColour(.8f,.05f,.04f), PaperColour(.9f,.9f,.85f), CapColour(.92f,.92f,.92f), WhistleColour(.75f,.75f,.8f);
    const FLinearColor BurstColour(1.f,.95f,.7f), ChargeColour(.8f,.92f,1.f), ColumnColour(1.f,.9f,.55f);
    const FLinearColor FlashWhite(1.f,1.f,1.f), FlashRed(1.f,.1f,.06f), AppearGlow(1.f,.92f,.6f);
    // Joints in pivot space at rest: the arms hang from the shoulders and the legs from the hips; the upper body
    // bends about the waist and a fall topples about the feet.
    const FVector ShoulderLeft(0,-25,33.5), ShoulderRight(0,25,33.5), HipLeft(0,-11,-20.5), HipRight(0,11,-20.5);
    const FVector Waist(0,0,-20), Feet(0,0,-88);
    constexpr float ArmHalf = 27.5f, LegHalf = 32.5f;
    bool IsOptional(int32 Index) { return Index >= RootRing; }
    bool IsBody(int32 Index) { return Index <= Facing; }
    // Markers drawn with a near-black edge (the root disc has its own rim): a slightly larger twin of each sits behind
    // it on the local view ray, so only a rim shows around the marker, over any seat tint, the turf or the track.
    struct FEdgeSpec { EPart Marker; FVector Grow; };
    const FEdgeSpec EdgeSpecs[] =
    {
        {ShieldHalo, {.06,.06,0}},
        {ShieldBar0, {.06,.06,.06}}, {ShieldBar1, {.06,.06,.06}}, {ShieldBar2, {.06,.06,.06}}, {ShieldBar3, {.06,.06,.06}},
        {ShieldBar4, {.06,.06,.06}}, {ShieldBar5, {.06,.06,.06}}, {ShieldBar6, {.06,.06,.06}}, {ShieldBar7, {.06,.06,.06}},
        {ProtectionBeam, {.06,.06,.06}},
        {ProtectionOrb0, {.06,.06,.06}}, {ProtectionOrb1, {.06,.06,.06}}, {ProtectionOrb2, {.06,.06,.06}},
        {ProtectionOrb3, {.06,.06,.06}},
    };
    constexpr float EdgeBehindCm = 30.f;
    FVector Blend(const FVector& Direction, const FVector& Toward, float Weight)
    {
        return FMath::Lerp(Direction, Toward.GetSafeNormal(), FMath::Clamp(Weight, 0.f, 1.f));
    }
}

ULDHumanoidVisualComponent::ULDHumanoidVisualComponent()
{
    using namespace LDHumanoidVisualDetail;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder"));
    Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
    Pivot->SetupAttachment(this);
    for (int32 Index = 0; Index < PartCount; ++Index)
    {
        const FPartSpec& Spec = Specs[Index];
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Spec.Name);
        Part->SetStaticMesh(Spec.Shape == Cube ? CubeMesh.Object : Spec.Shape == Sphere ? SphereMesh.Object : CylinderMesh.Object);
        if (Spec.Parent == INDEX_NONE)
        {
            Part->SetupAttachment(Pivot);
            Part->SetRelativeLocation(Spec.Location);
            Part->SetRelativeScale3D(Spec.Scale);
        }
        else
        {
            // Parents are unrotated and scaled at construction: express the wanted size and place in their space.
            const FPartSpec& ParentSpec = Specs[Spec.Parent];
            Part->SetupAttachment(Parts[Spec.Parent]);
            Part->SetRelativeLocation((Spec.Location - ParentSpec.Location) / ParentSpec.Scale);
            Part->SetRelativeScale3D(Spec.Scale / ParentSpec.Scale);
        }
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
        if (IsOptional(Index))
        {
            // Optional parts use the hidden-in-game flag, so a visibility change of the whole body never reveals them.
            Part->SetHiddenInGame(true);
            Part->SetCastShadow(false);
        }
        Parts.Add(Part);
    }
    for (const FEdgeSpec& Edge : EdgeSpecs)
    {
        const FPartSpec& Spec = Specs[Edge.Marker];
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(FName(FString(Spec.Name) + TEXT("Edge")));
        Part->SetStaticMesh(Parts[Edge.Marker]->GetStaticMesh());
        Part->SetupAttachment(Pivot);
        Part->SetRelativeScale3D(Spec.Scale + Edge.Grow);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
        Part->SetHiddenInGame(true);
        Part->SetCastShadow(false);
        Edges.Add(Part);
    }
}

void ULDHumanoidVisualComponent::OnRegister()
{
    Super::OnRegister();
    if (!GetWorld()) { return; }
    if (!Pivot->IsRegistered()) { Pivot->RegisterComponent(); }
    for (UStaticMeshComponent* Part : Parts)
    {
        if (!Part->IsRegistered()) { Part->RegisterComponent(); }
    }
    for (UStaticMeshComponent* Edge : Edges)
    {
        if (!Edge->IsRegistered()) { Edge->RegisterComponent(); }
    }
}

FLinearColor ULDHumanoidVisualComponent::ContenderTint(uint8 Slot)
{
    static const FLinearColor Palette[] =
    {
        {.05f,.2f,.75f}, {.85f,.35f,.02f}, {.1f,.55f,.12f}, {.45f,.12f,.65f}, {.85f,.75f,.05f}, {.05f,.6f,.65f}
    };
    // Validation keeps observed slots inside the six-seat roster; an out-of-range key draws a neutral grey.
    return Slot < UE_ARRAY_COUNT(Palette) ? Palette[Slot] : FLinearColor(.3f,.3f,.3f);
}

FLinearColor ULDHumanoidVisualComponent::MonsterTint(uint8 CreatureId)
{
    switch (static_cast<ELDDemoMonsterId>(CreatureId))
    {
    case ELDDemoMonsterId::MonsterScavenger: return FLinearColor(.06f,.07f,.14f);
    case ELDDemoMonsterId::MonsterGuardian: return FLinearColor(.42f,.03f,.05f);
    default: return FLinearColor(.3f,.3f,.3f);
    }
}

void ULDHumanoidVisualComponent::ApplyPalette(uint8 Slot)
{
    LookSlot = Slot;
    LookCreature = 0;
    ApplyLook(ContenderTint(Slot), 0);
}

void ULDHumanoidVisualComponent::ApplyMonsterLook(uint8 CreatureId)
{
    LookSlot = MAX_uint8;
    LookCreature = CreatureId;
    ApplyLook(MonsterTint(CreatureId), CreatureId);
}

void ULDHumanoidVisualComponent::CopyLook(const ULDHumanoidVisualComponent& Source)
{
    if (Source.LookCreature != 0) { ApplyMonsterLook(Source.LookCreature); }
    else if (Source.LookSlot != MAX_uint8) { ApplyPalette(Source.LookSlot); }
}

void ULDHumanoidVisualComponent::ApplyLook(const FLinearColor& Clothes, uint8 CreatureId)
{
    using namespace LDHumanoidVisualDetail;
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
    if (!ensureMsgf(Material, TEXT("Character material missing; rerun asset bootstrap"))) { return; }
    const bool bMonitor = CreatureId == uint8(ELDDemoMonsterId::MonsterScavenger);
    const bool bTeacher = CreatureId == uint8(ELDDemoMonsterId::MonsterGuardian);
    Tints.SetNum(Parts.Num());
    BaseColours.SetNum(Parts.Num());
    for (int32 Index = 0; Index < Parts.Num(); ++Index)
    {
        FLinearColor Colour = Clothes;
        if (Index == Head || Index == Facing) { Colour = Skin; }
        else if (Index == RootRing || Index == ShackleLeft || Index == ShackleRight) { Colour = RootColour; }
        else if (Index == RootRim) { Colour = EdgeColour; }
        else if (Index == ShieldHalo || (Index >= ShieldBar0 && Index < ShieldBar0 + ShieldBars)) { Colour = ShieldColour; }
        else if (Index == ProtectionBeam || (Index >= ProtectionOrb0 && Index < ProtectionOrb0 + ProtectionOrbs)) { Colour = ProtectionColour; }
        else if (Index == ReturnSign) { Colour = ReturnColour; }
        else if (Index == Armband) { Colour = ArmbandColour; }
        else if (Index == Clipboard) { Colour = PaperColour; }
        else if (Index == CapCrown || Index == CapBill) { Colour = CapColour; }
        else if (Index == Whistle) { Colour = WhistleColour; }
        else if (Index == Burst) { Colour = BurstColour; }
        else if (Index == Charge) { Colour = ChargeColour; }
        else if (Index == SpawnColumn) { Colour = ColumnColour; }
        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
        Instance->SetVectorParameterValue(TEXT("Tint"), Colour);
        Parts[Index]->SetMaterial(0, Instance);
        Tints[Index] = Instance;
        BaseColours[Index] = Colour;
    }
    UMaterialInstanceDynamic* Dark = UMaterialInstanceDynamic::Create(Material, this);
    Dark->SetVectorParameterValue(TEXT("Tint"), EdgeColour);
    for (UStaticMeshComponent* Edge : Edges) { Edge->SetMaterial(0, Dark); }
    bTinted = false;
    Parts[Armband]->SetHiddenInGame(!bMonitor);
    Parts[Clipboard]->SetHiddenInGame(!bMonitor);
    Parts[CapCrown]->SetHiddenInGame(!bTeacher);
    Parts[CapBill]->SetHiddenInGame(!bTeacher);
    Parts[Whistle]->SetHiddenInGame(!bTeacher);
}

void ULDHumanoidVisualComponent::SetCue(uint8 Value, float ActionAgeSeconds)
{
    using LDBodyMotion::EAction;
    const uint8 Code = Value & ActionMask;
    const EAction Next = Code == 1 ? EAction::Attack : Code == 2 ? EAction::Cast : Code == 4 ? EAction::Hit : EAction::None;
    const bool bAgeKnown = ActionAgeSeconds >= 0.f && FMath::IsFinite(ActionAgeSeconds);
    const double Onset = bAgeKnown ? Clock - ActionAgeSeconds : Clock;
    if (LDBodyMotion::IsNewInstance(Action, ActionOnset, Next, Onset, bAgeKnown)) { ActionOnset = Onset; }
    Action = Next;
    if (!bCueSeen)
    {
        bCueSeen = true;
        if (Value & Protected) { PlayAppear(); }
    }
    Cue = Value;
}

void ULDHumanoidVisualComponent::PlayAppear()
{
    AppearAt = Clock;
}

void ULDHumanoidVisualComponent::PlayFall()
{
    FallAt = Clock;
    Action = LDBodyMotion::EAction::None;
}

bool ULDHumanoidVisualComponent::IsFallen() const
{
    return FallAt >= 0 && LDBodyMotion::Fall(float(Clock - FallAt)).bGone;
}

void ULDHumanoidVisualComponent::PlaceLimb(int32 Index, const FVector& Joint, const FVector& Direction, float HalfLength)
{
    const FVector Along = Direction.GetSafeNormal();
    Parts[Index]->SetRelativeLocationAndRotation(Joint + Along * HalfLength, FQuat::FindBetweenNormals(FVector(0, 0, -1), Along));
}

void ULDHumanoidVisualComponent::Tint(int32 Index, const FLinearColor& Colour)
{
    if (Tints.IsValidIndex(Index) && Tints[Index]) { Tints[Index]->SetVectorParameterValue(TEXT("Tint"), Colour); }
}

void ULDHumanoidVisualComponent::PlaceEdges()
{
    using namespace LDHumanoidVisualDetail;
    // The local player's view; a world without one (a server without a local player) draws no edges.
    const APlayerController* Viewer = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
    const APlayerCameraManager* View = Viewer ? Viewer->PlayerCameraManager.Get() : nullptr;
    const FTransform& Space = Pivot->GetComponentTransform();
    for (int32 Index = 0; Index < Edges.Num(); ++Index)
    {
        const UStaticMeshComponent* Marker = Parts[EdgeSpecs[Index].Marker];
        const bool bShown = View && !Marker->bHiddenInGame;
        Edges[Index]->SetHiddenInGame(!bShown);
        if (!bShown) { continue; }
        const FVector At = Marker->GetComponentLocation();
        const FVector Ray = (At - View->GetCameraLocation()).GetSafeNormal();
        Edges[Index]->SetRelativeLocation(Space.InverseTransformPosition(At + Ray * EdgeBehindCm));
    }
}

void ULDHumanoidVisualComponent::Animate(const FVector& Velocity, float Delta)
{
    using namespace LDHumanoidVisualDetail;
    Clock += FMath::Max(Delta, 0.f);
    const bool bFalling = FallAt >= 0;
    LDBodyMotion::FLife Life;
    if (bFalling) { Life = LDBodyMotion::Fall(float(Clock - FallAt)); }
    else if (AppearAt >= 0) { Life = LDBodyMotion::Appear(float(Clock - AppearAt)); }
    // The current action's timeline; pose channels ease toward it, the spark and the flash follow it exactly.
    const LDBodyMotion::FChannels Target = bFalling ? LDBodyMotion::FChannels()
        : LDBodyMotion::Evaluate(Action, float(Clock - ActionOnset));
    const float Ease = 1.f - FMath::Exp(-40.f * FMath::Max(Delta, 0.f));
    Shown.WindBack = FMath::Lerp(Shown.WindBack, Target.WindBack, Ease);
    Shown.Strike = FMath::Lerp(Shown.Strike, Target.Strike, Ease);
    Shown.Raise = FMath::Lerp(Shown.Raise, Target.Raise, Ease);
    Shown.Thrust = FMath::Lerp(Shown.Thrust, Target.Thrust, Ease);
    Shown.Flinch = FMath::Lerp(Shown.Flinch, Target.Flinch, Ease);
    Shown.Lunge = FMath::Lerp(Shown.Lunge, Target.Lunge, Ease);
    Shown.Lean = FMath::Lerp(Shown.Lean, Target.Lean, Ease);
    Shown.Charge = FMath::Lerp(Shown.Charge, Target.Charge, Ease);
    Shown.Burst = Target.Burst;
    Shown.Flash = Target.Flash;

    // The whole body: a lunge or knock-back, an appear scale and a fall, both about the feet.
    const FQuat Topple = FRotator(Life.Tilt, 0, 0).Quaternion();
    const float Lying = FMath::Clamp(Life.Tilt / 90.f, 0.f, 1.f);
    const FVector Offset(Shown.Lunge, 0, 17.f * Lying - Life.Sink);
    Pivot->SetRelativeLocationAndRotation(Feet - Topple.RotateVector(Feet * Life.Scale) + Offset, Topple);
    Pivot->SetRelativeScale3D(FVector(FMath::Max(Life.Scale, .01f)));

    // A rooted body keeps both legs locked even while a correction still moves it.
    const bool bRooted = (Cue & Rooted) != 0;
    const float Speed = bRooted || bFalling ? 0.f : Velocity.Size2D();
    Phase += Delta * Speed / 32.0f;
    const float Swing = Speed > 3 ? FMath::DegreesToRadians(22.f) * FMath::Sin(Phase) : 0.f;
    // The upper body bends about the waist: forward for a strike or a push, back for a wind-up or a hit.
    const FRotator Bend(-Shown.Lean, 0, 0);
    const auto Upper = [&Bend](const FVector& Point) { return Waist + Bend.RotateVector(Point - Waist); };
    Parts[Torso]->SetRelativeLocationAndRotation(Upper(Specs[Torso].Location), Bend);
    Parts[Head]->SetRelativeLocation(Upper(Specs[Head].Location));
    Parts[Facing]->SetRelativeLocation(Upper(Specs[Facing].Location));
    // Arms: the walk swing, then each action pose by its weight. The right hand strikes (the 弹脑瓜崩 flick) while the
    // left counter-swings; a cast raises both over the head and pushes them forward; a hit throws them outward.
    FVector Left(FMath::Sin(Swing), -.08f, -FMath::Cos(Swing));
    FVector Right(-FMath::Sin(Swing), .08f, -FMath::Cos(Swing));
    Left = Blend(Left, FVector(.4f, -.2f, -.9f), .5f * Shown.WindBack);
    Left = Blend(Left, FVector(-.4f, -.2f, -.9f), .5f * Shown.Strike);
    Right = Blend(Right, FVector(-.75f, .3f, -.15f), Shown.WindBack);
    Right = Blend(Right, FVector(1.f, .1f, .25f), Shown.Strike);
    Left = Blend(Left, FVector(.3f, -.4f, .85f), Shown.Raise);
    Right = Blend(Right, FVector(.3f, .4f, .85f), Shown.Raise);
    Left = Blend(Left, FVector(1.f, -.15f, .15f), Shown.Thrust);
    Right = Blend(Right, FVector(1.f, .15f, .15f), Shown.Thrust);
    Left = Blend(Left, FVector(-.3f, -.85f, -.15f), Shown.Flinch);
    Right = Blend(Right, FVector(-.3f, .85f, -.15f), Shown.Flinch);
    const FVector LeftArm = Bend.RotateVector(Left).GetSafeNormal(), RightArm = Bend.RotateVector(Right).GetSafeNormal();
    PlaceLimb(ArmLeft, Upper(ShoulderLeft), LeftArm, ArmHalf);
    PlaceLimb(ArmRight, Upper(ShoulderRight), RightArm, ArmHalf);
    // Legs: the walk stride, and a forward step under a lunge.
    const float Stride = bRooted ? 0.f : Swing;
    const float Step = FMath::Clamp(Shown.Lunge / 30.f, 0.f, 1.f);
    const FVector LegL = Blend(FVector(-FMath::Sin(Stride), 0, -FMath::Cos(Stride)), FVector(-.25f, 0, -.97f), .7f * Step);
    const FVector LegR = Blend(FVector(FMath::Sin(Stride), 0, -FMath::Cos(Stride)), FVector(.35f, 0, -.94f), .7f * Step);
    PlaceLimb(LegLeft, HipLeft, LegL, LegHalf);
    PlaceLimb(LegRight, HipRight, LegR, LegHalf);

    // Effects: the spark at the striking hand, the chest or the pushing hands; the orb over a cast.
    const bool bBurst = Shown.Burst > .02f;
    Parts[Burst]->SetHiddenInGame(!bBurst);
    if (bBurst)
    {
        const FVector Spark = Action == LDBodyMotion::EAction::Attack ? Upper(ShoulderRight) + RightArm * (2.f * ArmHalf + 6.f)
            : Action == LDBodyMotion::EAction::Cast ? Upper(FVector(48, 0, 45)) : Upper(FVector(22, 0, 25));
        Parts[Burst]->SetRelativeLocation(Spark);
        Parts[Burst]->SetRelativeScale3D(FVector(.55f * Shown.Burst));
    }
    const bool bCharge = Shown.Charge > .02f;
    Parts[Charge]->SetHiddenInGame(!bCharge);
    if (bCharge)
    {
        Parts[Charge]->SetRelativeLocation(Upper(Specs[Charge].Location));
        Parts[Charge]->SetRelativeScale3D(FVector(.1f + .25f * Shown.Charge));
    }
    const bool bColumn = Life.Column > .02f;
    Parts[SpawnColumn]->SetHiddenInGame(!bColumn);
    if (bColumn) { Parts[SpawnColumn]->SetRelativeScale3D(FVector(.15f + .75f * Life.Column, .15f + .75f * Life.Column, 3.2f)); }

    // Status markers, one shape each; a falling body shows none.
    const float Time = float(FMath::Fmod(Clock, 3600.0));
    const bool bRoot = !bFalling && bRooted;
    Parts[RootRing]->SetHiddenInGame(!bRoot);
    Parts[RootRim]->SetHiddenInGame(!bRoot);
    Parts[ShackleLeft]->SetHiddenInGame(!bRoot);
    Parts[ShackleRight]->SetHiddenInGame(!bRoot);
    if (bRoot)
    {
        const float Pulse = 1.05f * (1.f + .07f * FMath::Sin(Time * 9.f));
        Parts[RootRing]->SetRelativeScale3D(FVector(Pulse, Pulse, .03f));
        Parts[RootRim]->SetRelativeScale3D(FVector(Pulse + .2f, Pulse + .2f, .02f));
    }
    const bool bShield = !bFalling && (Cue & Shielded);
    Parts[ShieldHalo]->SetHiddenInGame(!bShield);
    for (int32 Bar = 0; Bar < ShieldBars; ++Bar)
    {
        Parts[ShieldBar0 + Bar]->SetHiddenInGame(!bShield);
        if (!bShield) { continue; }
        const float Angle = FMath::DegreesToRadians(45.f * Bar + 40.f * Time);
        Parts[ShieldBar0 + Bar]->SetRelativeLocation(FVector(56.f * FMath::Cos(Angle), 56.f * FMath::Sin(Angle), 2.f));
    }
    const bool bProtected = !bFalling && (Cue & Protected);
    Parts[ProtectionBeam]->SetHiddenInGame(!bProtected);
    for (int32 Orb = 0; Orb < ProtectionOrbs; ++Orb)
    {
        Parts[ProtectionOrb0 + Orb]->SetHiddenInGame(!bProtected);
        if (!bProtected) { continue; }
        const float Angle = FMath::DegreesToRadians(90.f * Orb + 200.f * Time);
        Parts[ProtectionOrb0 + Orb]->SetRelativeLocation(
            FVector(62.f * FMath::Cos(Angle), 62.f * FMath::Sin(Angle), -5.f + 8.f * FMath::Sin(4.f * Time + 1.6f * Orb)));
    }
    Parts[ReturnSign]->SetHiddenInGame(bFalling || !(Cue & Returning));
    PlaceEdges();

    // Body tint: a hit flashes white, then red; an appear glows. Restored once both have passed.
    const float Glow = bFalling ? 0.f : Life.Glow;
    if (Shown.Flash > 0.f || Glow > 0.f)
    {
        const bool bHitTint = Shown.Flash >= Glow;
        const float Amount = FMath::Min(1.f, bHitTint ? 1.15f * Shown.Flash : Glow);
        const FLinearColor Over = bHitTint
            ? FMath::Lerp(FlashRed, FlashWhite, FMath::Clamp((Shown.Flash - .6f) / .4f, 0.f, 1.f)) : AppearGlow;
        for (int32 Index = 0; Index < PartCount; ++Index)
        {
            if (IsBody(Index) && BaseColours.IsValidIndex(Index)) { Tint(Index, FMath::Lerp(BaseColours[Index], Over, Amount)); }
        }
        bTinted = true;
    }
    else if (bTinted)
    {
        for (int32 Index = 0; Index < PartCount; ++Index)
        {
            if (IsBody(Index) && BaseColours.IsValidIndex(Index)) { Tint(Index, BaseColours[Index]); }
        }
        bTinted = false;
    }
}
