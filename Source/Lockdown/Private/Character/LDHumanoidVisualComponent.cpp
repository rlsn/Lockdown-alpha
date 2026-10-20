#include "Character/LDHumanoidVisualComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ULDHumanoidVisualComponent::ULDHumanoidVisualComponent()
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    const TCHAR* Names[] = {TEXT("Torso"), TEXT("Head"), TEXT("ArmLeft"), TEXT("ArmRight"), TEXT("LegLeft"), TEXT("LegRight"), TEXT("Facing"),TEXT("StatusMarker")};
    const FVector Positions[] = {{0,0,9}, {0,0,65}, {0,-25,6}, {0,25,6}, {0,-11,-53}, {0,11,-53}, {19,0,65},{0,0,-80}};
    const FVector Scales[] = {{.34,.4,.58}, {.32,.3,.34}, {.16,.15,.55}, {.16,.15,.55}, {.18,.18,.65}, {.18,.18,.65}, {.12,.12,.12},{.8,.8,.08}};
    for (int32 Index = 0; Index < 8; ++Index)
    {
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Names[Index]);
        Part->SetupAttachment(this);
        Part->SetStaticMesh(Index == 1 || Index==7 ? Sphere.Object : Cube.Object);
        Part->SetRelativeLocation(Positions[Index]);
        Part->SetRelativeScale3D(Scales[Index]);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
        Parts.Add(Part);
        if(Index==7){Part->SetVisibility(false);Part->SetCastShadow(false);}
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
    const uint8 Action=Cue&15;
    if(Action==1||Action==2||Action==3||Action==6||Action==7||Action==8)
    {Parts[2]->SetRelativeRotation(FRotator(Action==2?-120:-75,0,-15));Parts[3]->SetRelativeRotation(FRotator(-75,0,15));}
    SetRelativeRotation(Action==5?FRotator(0,0,90):FRotator((Cue&32)?-15:0,0,0));
    Parts[0]->SetRelativeRotation(FRotator(Action==4?-20:0,0,0));
    // The same visible geometry cues apply to residents and contenders.
    Parts[6]->SetRelativeScale3D((Cue&16)?FVector(.35):((Cue&64)?FVector(.22):FVector(.12)));
    Parts[7]->SetVisibility(Action!=5&&(Cue&240)!=0&&IsVisible());
    if(auto* Marker=Cast<UMaterialInstanceDynamic>(Parts[7]->GetMaterial(0)))
    {Marker->SetVectorParameterValue(TEXT("Tint"),(Cue&16)?FLinearColor(1,.12f,.025f):((Cue&64)?FLinearColor(.8f,.95f,1):((Cue&32)?FLinearColor(1,.05f,.02f):FLinearColor(1,.55f,.05f))));}
}
