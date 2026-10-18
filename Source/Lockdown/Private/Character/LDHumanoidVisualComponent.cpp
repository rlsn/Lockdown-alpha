#include "Character/LDHumanoidVisualComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ULDHumanoidVisualComponent::ULDHumanoidVisualComponent()
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    const TCHAR* Names[] = {TEXT("Torso"), TEXT("Head"), TEXT("ArmLeft"), TEXT("ArmRight"), TEXT("LegLeft"), TEXT("LegRight"), TEXT("Facing")};
    const FVector Positions[] = {{0,0,9}, {0,0,65}, {0,-25,6}, {0,25,6}, {0,-11,-53}, {0,11,-53}, {19,0,65}};
    const FVector Scales[] = {{.34,.4,.58}, {.32,.3,.34}, {.16,.15,.55}, {.16,.15,.55}, {.18,.18,.65}, {.18,.18,.65}, {.12,.12,.12}};
    for (int32 Index = 0; Index < 7; ++Index)
    {
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Names[Index]);
        Part->SetupAttachment(this);
        Part->SetStaticMesh(Index == 1 ? Sphere.Object : Cube.Object);
        Part->SetRelativeLocation(Positions[Index]);
        Part->SetRelativeScale3D(Scales[Index]);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
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

void ULDHumanoidVisualComponent::ApplyPalette(uint8 Key)
{
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
    if (!ensureMsgf(Material, TEXT("Character material missing; rerun asset bootstrap"))) { return; }
    const FLinearColor Palette[] = {{.15f,.38f,.4f}, {.35f,.23f,.15f}, {.27f,.3f,.45f}, {.35f,.37f,.21f}};
    for (int32 Index = 0; Index < Parts.Num(); ++Index)
    {
        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
        Instance->SetVectorParameterValue(TEXT("Tint"), Index == 1 || Index == 6
            ? FLinearColor(.68f,.47f,.3f) : Palette[Key % 4]);
        Parts[Index]->SetMaterial(0, Instance);
    }
}

void ULDHumanoidVisualComponent::Animate(const FVector& Velocity, float Delta)
{
    const float Speed = Velocity.Size2D();
    Phase += Delta * Speed / 32.0f;
    const float Swing = Speed > 3 ? FMath::Sin(Phase) * 22.0f : 0;
    Parts[2]->SetRelativeRotation(FRotator(Swing,0,0));
    Parts[3]->SetRelativeRotation(FRotator(-Swing,0,0));
    Parts[4]->SetRelativeRotation(FRotator(-Swing,0,0));
    Parts[5]->SetRelativeRotation(FRotator(Swing,0,0));
}
