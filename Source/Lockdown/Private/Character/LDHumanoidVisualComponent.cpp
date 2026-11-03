#include "Character/LDHumanoidVisualComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Development/LDDemoContentIds.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace LDHumanoidVisualDetail
{
    // Part order is fixed; indices below are used by Animate and ApplyLook.
    enum EPart : int32
    {
        Torso, Head, ArmLeft, ArmRight, LegLeft, LegRight, Facing,
        RootRing, ShieldHalo, ProtectionBeam, ReturnSign, Armband, Clipboard, CapCrown, CapBill, Whistle, PartCount
    };
    enum EShape : uint8 { Cube, Sphere, Cylinder };
    struct FPartSpec
    {
        const TCHAR* Name;
        EShape Shape;
        int32 Parent;     // INDEX_NONE: the component itself
        FVector Location; // component space, before any pose
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
        // Status markers: a ring at the feet, a halo, a vertical beam and a sign on the back.
        {TEXT("RootRing"), Cylinder, INDEX_NONE, {0,0,-87.5}, {1.05,1.05,.03}},
        {TEXT("ShieldHalo"), Cylinder, INDEX_NONE, {0,0,97}, {.62,.62,.025}},
        {TEXT("ProtectionBeam"), Cylinder, INDEX_NONE, {0,0,160}, {.14,.14,1.3}},
        {TEXT("ReturnSign"), Cube, INDEX_NONE, {-24,0,92}, {.04,.34,.3}},
        // Camp NPC accessories; the armband and clipboard follow the arms, the cap and whistle the head and torso.
        {TEXT("Armband"), Cube, ArmLeft, {0,-25,22}, {.19,.18,.1}},
        {TEXT("Clipboard"), Cube, ArmRight, {12,27,-8}, {.03,.26,.34}},
        {TEXT("CapCrown"), Cylinder, Head, {0,0,80}, {.34,.34,.1}},
        {TEXT("CapBill"), Cube, Head, {18,0,76}, {.2,.3,.03}},
        {TEXT("Whistle"), Sphere, Torso, {18,0,30}, {.07,.07,.07}},
    };
    const FLinearColor Skin(.68f,.47f,.3f);
    const FLinearColor RootColour(.55f,.08f,.02f), ShieldColour(.55f,.9f,1.f), ProtectionColour(1.f,.8f,.2f), ReturnColour(.95f,.95f,.95f);
    const FLinearColor ArmbandColour(.8f,.05f,.04f), PaperColour(.9f,.9f,.85f), CapColour(.92f,.92f,.92f), WhistleColour(.75f,.75f,.8f);
    bool IsOptional(int32 Index) { return Index >= RootRing; }
}

ULDHumanoidVisualComponent::ULDHumanoidVisualComponent()
{
    using namespace LDHumanoidVisualDetail;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder"));
    for (int32 Index = 0; Index < PartCount; ++Index)
    {
        const FPartSpec& Spec = Specs[Index];
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Spec.Name);
        Part->SetStaticMesh(Spec.Shape == Cube ? CubeMesh.Object : Spec.Shape == Sphere ? SphereMesh.Object : CylinderMesh.Object);
        if (Spec.Parent == INDEX_NONE)
        {
            Part->SetupAttachment(this);
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
}

void ULDHumanoidVisualComponent::OnRegister()
{
    Super::OnRegister();
    for (UStaticMeshComponent* Part : Parts)
    {
        if (GetWorld() && !Part->IsRegistered()) { Part->RegisterComponent(); }
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

void ULDHumanoidVisualComponent::ApplyPalette(uint8 Slot) { ApplyLook(ContenderTint(Slot), 0); }

void ULDHumanoidVisualComponent::ApplyMonsterLook(uint8 CreatureId) { ApplyLook(MonsterTint(CreatureId), CreatureId); }

void ULDHumanoidVisualComponent::ApplyLook(const FLinearColor& Clothes, uint8 CreatureId)
{
    using namespace LDHumanoidVisualDetail;
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
    if (!ensureMsgf(Material, TEXT("Character material missing; rerun asset bootstrap"))) { return; }
    const bool bMonitor = CreatureId == uint8(ELDDemoMonsterId::MonsterScavenger);
    const bool bTeacher = CreatureId == uint8(ELDDemoMonsterId::MonsterGuardian);
    for (int32 Index = 0; Index < Parts.Num(); ++Index)
    {
        FLinearColor Colour = Clothes;
        switch (Index)
        {
        case Head: case Facing: Colour = Skin; break;
        case RootRing: Colour = RootColour; break;
        case ShieldHalo: Colour = ShieldColour; break;
        case ProtectionBeam: Colour = ProtectionColour; break;
        case ReturnSign: Colour = ReturnColour; break;
        case Armband: Colour = ArmbandColour; break;
        case Clipboard: Colour = PaperColour; break;
        case CapCrown: case CapBill: Colour = CapColour; break;
        case Whistle: Colour = WhistleColour; break;
        default: break;
        }
        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
        Instance->SetVectorParameterValue(TEXT("Tint"), Colour);
        Parts[Index]->SetMaterial(0, Instance);
    }
    Parts[Armband]->SetHiddenInGame(!bMonitor);
    Parts[Clipboard]->SetHiddenInGame(!bMonitor);
    Parts[CapCrown]->SetHiddenInGame(!bTeacher);
    Parts[CapBill]->SetHiddenInGame(!bTeacher);
    Parts[Whistle]->SetHiddenInGame(!bTeacher);
}

void ULDHumanoidVisualComponent::Animate(const FVector& Velocity, float Delta)
{
    using namespace LDHumanoidVisualDetail;
    const uint8 Action = Cue & ActionMask;
    const bool bLying = Action == 5;
    // A rooted body keeps both legs locked even while a correction still moves it.
    const float Speed = (Cue & Rooted) ? 0.f : Velocity.Size2D();
    Phase += Delta * Speed / 32.0f;
    const float Swing = Speed > 3 ? FMath::Sin(Phase) * 22.0f : 0;
    Parts[ArmLeft]->SetRelativeRotation(FRotator(Swing,0,0));
    Parts[ArmRight]->SetRelativeRotation(FRotator(-Swing,0,0));
    Parts[LegLeft]->SetRelativeRotation(FRotator(-Swing,0,0));
    Parts[LegRight]->SetRelativeRotation(FRotator(Swing,0,0));
    // Arms pivot at their centre, so poses use horizontal or splayed arms: one arm forward is the 弹脑瓜崩 flick,
    // both arms forward in a V is a cast, arms flung outward is a hit.
    if (Action == 1) { Parts[ArmRight]->SetRelativeRotation(FRotator(-90,0,10)); }
    else if (Action == 2) { Parts[ArmLeft]->SetRelativeRotation(FRotator(-90,0,-30)); Parts[ArmRight]->SetRelativeRotation(FRotator(-90,0,30)); }
    else if (Action == 3 || (Action >= 6 && Action <= 9))
    { Parts[ArmLeft]->SetRelativeRotation(FRotator(-75,0,-15)); Parts[ArmRight]->SetRelativeRotation(FRotator(-75,0,15)); }
    else if (Action == 4) { Parts[ArmLeft]->SetRelativeRotation(FRotator(0,0,-40)); Parts[ArmRight]->SetRelativeRotation(FRotator(0,0,40)); }
    SetRelativeRotation(bLying ? FRotator(0,0,90) : FRotator::ZeroRotator);
    Parts[Torso]->SetRelativeRotation(FRotator(Action == 4 ? -20 : 0,0,0));
    Parts[RootRing]->SetHiddenInGame(bLying || !(Cue & Rooted));
    Parts[ShieldHalo]->SetHiddenInGame(bLying || !(Cue & Shielded));
    Parts[ProtectionBeam]->SetHiddenInGame(bLying || !(Cue & Protected));
    Parts[ReturnSign]->SetHiddenInGame(bLying || !(Cue & Returning));
}
