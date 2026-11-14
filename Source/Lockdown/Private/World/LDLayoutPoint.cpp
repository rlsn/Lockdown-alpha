#include "World/LDLayoutPoint.h"

#if WITH_EDITOR
#include "Art/LDArtCatalog.h"
#include "Character/LDCharacter.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "World/LDCaptureMarker.h"
#include "World/LDNativeGround.h"
#endif

ALDLayoutPoint::ALDLayoutPoint()
{
#if WITH_EDITOR
    BodyPreview = CreateEditorOnlyDefaultSubobject<ULDHumanoidVisualComponent>(TEXT("BodyPreview"));
    PropPreview = CreateEditorOnlyDefaultSubobject<UStaticMeshComponent>(TEXT("PropPreview"));
    RangePreview = CreateEditorOnlyDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RangePreview"));
    // The editor binary also runs -game clients, where editor-only subobjects are deliberately absent.
    if (!BodyPreview || !PropPreview || !RangePreview) { return; }
    BodyPreview->SetupAttachment(GetRootComponent());
    PropPreview->SetupAttachment(GetRootComponent());
    RangePreview->SetupAttachment(GetRootComponent());
    BodyPreview->SetHiddenInGame(true, true);
    for (UPrimitiveComponent* Part : {static_cast<UPrimitiveComponent*>(PropPreview.Get()), static_cast<UPrimitiveComponent*>(RangePreview.Get())})
    {
        Part->bIsEditorOnly = true;
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCollisionResponseToAllChannels(ECR_Ignore);
        Part->SetCanEverAffectNavigation(false);
        Part->SetGenerateOverlapEvents(false);
        Part->SetHiddenInGame(true);
        Part->SetCastShadow(false);
    }
#endif
}

void ALDLayoutPoint::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
#if WITH_EDITOR
    if (!BodyPreview || !GetWorld() || GetWorld()->IsGameWorld()) { return; }
    PreviewCatalog = LoadObject<ULDArtCatalog>(nullptr, ULDArtCatalog::AssetPath);
    const auto* Look = PreviewCatalog ? PreviewCatalog->Characters.FindByPredicate(
        [this](const FLDArtCharacter& C) { return C.Id == CharacterId; }) : nullptr;
    FHitResult Support;
    const FVector Position = GetActorLocation();
    const double GroundOffset = LDNativeGround::Trace(*GetWorld(), Support,
        Position + FVector(0,0,100000), Position - FVector(0,0,100000), LDNativeGround::Query(*GetWorld()))
        ? Support.ImpactPoint.Z - Position.Z : 0;
    BodyPreview->SetVisibility(Look != nullptr, true);
    if (Look)
    {
        BodyPreview->SetRelativeLocation(FVector(0, 0, GroundOffset + GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
        BodyPreview->ApplyEditorPreview(*Look, PreviewTint);
    }
    const auto* Prop = PreviewCatalog ? PreviewCatalog->Props.FindByPredicate(
        [this](const FLDArtProp& P) { return P.Id == PropId; }) : nullptr;
    UStaticMesh* Shape = Prop ? Prop->Mesh.Get() : BasicShape.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr,
        *(TEXT("/Engine/BasicShapes/") + BasicShape + TEXT(".") + BasicShape));
    PropPreview->SetStaticMesh(Shape);
    PropPreview->SetRelativeLocation(PropOffset + FVector(0,0,GroundOffset));
    PropPreview->SetRelativeScale3D(PropScale);
    PropPreview->SetVisibility(Shape != nullptr);
    if (RadiusCm > 0)
    {
        RangePreview->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        LDGroundRing::Lay(RangePreview, RadiusCm);
        if (auto* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character")))
        {
            auto* Paint = UMaterialInstanceDynamic::Create(Base, this);
            Paint->SetVectorParameterValue(ULDArtCatalog::TintParameter, PreviewTint);
            RangePreview->SetMaterial(0, Paint);
        }
    }
    else { RangePreview->ClearInstances(); }
    RangePreview->SetVisibility(RadiusCm > 0);
    RangePreview->SetRelativeLocation(FVector(0,0,GroundOffset));
#endif
}

#if WITH_EDITOR
void ALDLayoutPoint::ConfigureTrainingCopy()
{
    if (!GetWorld() || GetWorld()->IsGameWorld()) { return; }
    for (FName Tag : Tags)
    {
        if (Tag.ToString().StartsWith(TEXT("LDSceneAnchor:training_dummy_")))
        { bSharedTrainingDummy = true; break; }
    }
}

void ALDLayoutPoint::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
    Super::PostDuplicate(DuplicateMode);
    if (DuplicateMode == EDuplicateMode::Normal) { ConfigureTrainingCopy(); }
}

void ALDLayoutPoint::PostEditImport()
{
    Super::PostEditImport();
    ConfigureTrainingCopy();
}
#endif
