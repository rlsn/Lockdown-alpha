#include "Character/LDHumanoidVisualComponent.h"
#include "Core/LDLog.h"
#include "Animation/AnimSequence.h"
#include "Art/LDArtCatalog.h"
#include "Art/LDArtSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/LDCharacter.h"
#include "Character/LDModelPicking.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Development/LDDemoContentIds.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Framework/LDPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "UI/LDControlSettings.h"
#include "UObject/ConstructorHelpers.h"

namespace LDHumanoidVisualDetail
{
    // Status markers and effect shapes around the skeletal body. Part order is fixed; indices below are used by Layout,
    // Animate and ApplyLook.
    enum EPart : int32
    {
        RootRing, RootRim, ShackleLeft, ShackleRight, ShieldHalo,
        ShieldBar0, ShieldBar1, ShieldBar2, ShieldBar3, ShieldBar4, ShieldBar5, ShieldBar6, ShieldBar7,
        ProtectionBeam, ProtectionOrb0, ProtectionOrb1, ProtectionOrb2, ProtectionOrb3, ReturnSign,
        Burst, Charge, SpawnColumn, HitSpark, PartCount
    };
    constexpr int32 ShieldBars = 8, ProtectionOrbs = 4;
    enum EShape : uint8 { Cube, Sphere, Cylinder };
    struct FPartSpec
    {
        const TCHAR* Name;
        EShape Shape;
        FVector Scale; // BasicShapes units (100 cm); Layout fits the shield bars to the body's height
    };
    // Status markers: root is a pulsing disc on a dark rim at the feet with ankle shackles, shield a turning cage of bars
    // under a halo, protection a beam over the head with orbiting orbs, camp return a sign on the back. Effects: the
    // impact spark, the cast orb at the hand and the appear column. The feet discs stand above the own-body marker's.
    const FPartSpec Specs[PartCount] =
    {
        {TEXT("RootRing"), Cylinder, {1.05,1.05,.03}},
        {TEXT("RootRim"), Cylinder, {1.25,1.25,.02}},
        {TEXT("ShackleLeft"), Cylinder, {.3,.3,.08}},
        {TEXT("ShackleRight"), Cylinder, {.3,.3,.08}},
        {TEXT("ShieldHalo"), Cylinder, {.62,.62,.025}},
        {TEXT("ShieldBar0"), Cylinder, {.06,.06,1.8}},
        {TEXT("ShieldBar1"), Cylinder, {.06,.06,1.8}},
        {TEXT("ShieldBar2"), Cylinder, {.06,.06,1.8}},
        {TEXT("ShieldBar3"), Cylinder, {.06,.06,1.8}},
        {TEXT("ShieldBar4"), Cylinder, {.06,.06,1.8}},
        {TEXT("ShieldBar5"), Cylinder, {.06,.06,1.8}},
        {TEXT("ShieldBar6"), Cylinder, {.06,.06,1.8}},
        {TEXT("ShieldBar7"), Cylinder, {.06,.06,1.8}},
        {TEXT("ProtectionBeam"), Cylinder, {.14,.14,1.3}},
        {TEXT("ProtectionOrb0"), Sphere, {.13,.13,.13}},
        {TEXT("ProtectionOrb1"), Sphere, {.13,.13,.13}},
        {TEXT("ProtectionOrb2"), Sphere, {.13,.13,.13}},
        {TEXT("ProtectionOrb3"), Sphere, {.13,.13,.13}},
        {TEXT("ReturnSign"), Cube, {.04,.34,.3}},
        {TEXT("Burst"), Sphere, {.1,.1,.1}},
        {TEXT("Charge"), Sphere, {.1,.1,.1}},
        {TEXT("SpawnColumn"), Cylinder, {.9,.9,3.2}},
        {TEXT("HitSpark"), Sphere, {.1,.1,.1}},
    };
    // One fixed colour per status on every unit, apart from all six seat tints, the turf and the red track:
    // root hot magenta, shield bright cyan, protection white-gold; each edged in near-black.
    const FLinearColor RootColour(1.f,.02f,.58f), EdgeColour(.012f,.01f,.016f), ShieldColour(0.f,.78f,1.f);
    const FLinearColor ProtectionColour(1.f,.92f,.62f), ReturnColour(.95f,.95f,.95f);
    const FLinearColor BurstColour(1.f,.95f,.7f), ChargeColour(.8f,.92f,1.f), ColumnColour(1.f,.9f,.55f);
    // Emissive over the whole body through the art materials' Flash: a hit flashes white, then red; an appear glows.
    const FLinearColor FlashWhite(1.f,1.f,1.f), FlashRed(1.f,.1f,.06f), AppearGlow(1.f,.92f,.6f);
    // Below this ground speed a body stands instead of running in slow motion.
    constexpr float RunFromCmPerSec = 15.f;
    // The status markers' reference body height; wider and taller bodies spread their markers in proportion.
    constexpr float ReferenceHeightCm = 175.f;
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
    FLinearColor ColourOf(int32 Index)
    {
        if (Index == RootRim) { return EdgeColour; }
        if (Index == RootRing || Index == ShackleLeft || Index == ShackleRight) { return RootColour; }
        if (Index == ShieldHalo || (Index >= ShieldBar0 && Index < ShieldBar0 + ShieldBars)) { return ShieldColour; }
        if (Index == ProtectionBeam || (Index >= ProtectionOrb0 && Index < ProtectionOrb0 + ProtectionOrbs)) { return ProtectionColour; }
        if (Index == ReturnSign) { return ReturnColour; }
        if (Index == Burst || Index == HitSpark) { return BurstColour; }
        return Index == Charge ? ChargeColour : ColumnColour;
    }
    ELDArtAnim ClipOf(LDBodyMotion::EAction Action)
    {
        switch (Action)
        {
        case LDBodyMotion::EAction::Attack: return ELDArtAnim::Attack;
        case LDBodyMotion::EAction::Cast: return ELDArtAnim::Cast;
        case LDBodyMotion::EAction::Hit: return ELDArtAnim::Hit;
        default: return ELDArtAnim::Count;
        }
    }
    float LengthOf(const FLDArtAnimation* Entry)
    {
        return Entry && Entry->Sequence ? Entry->Sequence->GetPlayLength() : 0.f;
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
    Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ArtBody"));
    Mesh->SetupAttachment(Pivot);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCanEverAffectNavigation(false);
    // A body hidden from this view (another player's authority body on the Listen Host) evaluates no pose.
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    Mesh->SetHiddenInGame(true);
    for (int32 Index = 0; Index < PartCount; ++Index)
    {
        const FPartSpec& Spec = Specs[Index];
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Spec.Name);
        Part->SetStaticMesh(Spec.Shape == Cube ? CubeMesh.Object : Spec.Shape == Sphere ? SphereMesh.Object : CylinderMesh.Object);
        Part->SetupAttachment(Pivot);
        Part->SetRelativeScale3D(Spec.Scale);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
        // Markers and effects use the hidden-in-game flag, so a visibility change of the whole body never reveals them.
        Part->SetHiddenInGame(true);
        Part->SetCastShadow(false);
        Parts.Add(Part);
    }
    for (const FEdgeSpec& Edge : EdgeSpecs)
    {
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(FName(FString(Specs[Edge.Marker].Name) + TEXT("Edge")));
        Part->SetStaticMesh(Parts[Edge.Marker]->GetStaticMesh());
        Part->SetupAttachment(Pivot);
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
    if (!Mesh->IsRegistered()) { Mesh->RegisterComponent(); }
    for (UStaticMeshComponent* Part : Parts)
    {
        if (!Part->IsRegistered()) { Part->RegisterComponent(); }
    }
    for (UStaticMeshComponent* Edge : Edges)
    {
        if (!Edge->IsRegistered()) { Edge->RegisterComponent(); }
    }
    for (UMeshComponent* Part : Worn)
    {
        if (Part && !Part->IsRegistered()) { Part->RegisterComponent(); }
    }
}

void ULDHumanoidVisualComponent::WearParts(const FLinearColor& Identity, const FString& What)
{
    for (UMeshComponent* Part : Worn)
    {
        if (Part) { Part->DestroyComponent(); }
    }
    Worn.Reset();
    if (!Art) { return; }
    UObject* Outer = GetOwner() ? static_cast<UObject*>(GetOwner()) : static_cast<UObject*>(this);
    for (const FLDArtAttachment& Spec : Art->Attachments)
    {
        const FName Name = MakeUniqueObjectName(Outer, UMeshComponent::StaticClass(), FName(*(TEXT("Worn_") + Spec.Id)));
        UMeshComponent* Part = nullptr;
        if (Spec.StaticMesh)
        {
            auto* Rigid = NewObject<UStaticMeshComponent>(Outer, Name, RF_Transient);
            Rigid->SetStaticMesh(Spec.StaticMesh);
            Rigid->SetupAttachment(Mesh, Spec.Socket);
            Part = Rigid;
        }
        else
        {
            // A garment on the body skeleton: it takes the body's pose and simulates its own cloth on top.
            auto* Garment = NewObject<USkeletalMeshComponent>(Outer, Name, RF_Transient);
            Garment->SetSkeletalMeshAsset(Spec.SkinnedMesh);
            Garment->SetupAttachment(Mesh);
            Garment->SetLeaderPoseComponent(Mesh);
            Garment->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
            Part = Garment;
        }
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false);
        // A part worn later follows the body's own visibility, which its owner sets once per change for the whole tree.
        Part->SetVisibility(GetVisibleFlag());
        if (IsRegistered()) { Part->RegisterComponent(); }
        for (int32 Index = 0; Index < Part->GetNumMaterials(); ++Index)
        {
            UMaterialInstanceDynamic* Paint = Part->CreateDynamicMaterialInstance(Index);
            if (!ensureMsgf(Paint, TEXT("%s: art part %s.%s has no material %d; rerun the art import"), *What, *Art->Id, *Spec.Id, Index)) { continue; }
            Paint->SetVectorParameterValue(ULDArtCatalog::FlashParameter, FLinearColor::Black);
            Paints.Add(Paint);
        }
        for (const FName Slot : Spec.TintSlots)
        {
            const int32 Index = Part->GetMaterialIndex(Slot);
            auto* Paint = Index == INDEX_NONE ? nullptr : Cast<UMaterialInstanceDynamic>(Part->GetMaterial(Index));
            if (ensureMsgf(Paint, TEXT("%s: art part %s.%s has no tint slot %s; rerun the art import"), *What, *Art->Id, *Spec.Id, *Slot.ToString()))
            {
                Paint->SetVectorParameterValue(ULDArtCatalog::TintParameter, Identity);
            }
        }
        Worn.Add(Part);
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
    // D26 画饼老板: a dark gold that reads apart from both camp tiers.
    case ELDDemoMonsterId::MonsterDean: return FLinearColor(.55f,.38f,.04f);
    // #97 Elite 催毕设的导师: a deep violet, apart from the navy Low suit and the red High hoodie.
    case ELDDemoMonsterId::MonsterAdvisor: return FLinearColor(.22f,.04f,.38f);
    default: return FLinearColor(.3f,.3f,.3f);
    }
}

void ULDHumanoidVisualComponent::ApplyPalette(uint8 Slot)
{
    LookSlot = Slot;
    LookCreature = 0;
    const FLDArtResolved* Catalog = ULDArtSubsystem::Get(this);
    ApplyLook(Catalog ? Catalog->ForSeat(Slot) : nullptr, ContenderTint(Slot), FString::Printf(TEXT("Seat %u"), uint32(Slot)));
}

void ULDHumanoidVisualComponent::ApplyMonsterLook(uint8 CreatureId)
{
    LookSlot = MAX_uint8;
    LookCreature = CreatureId;
    const FLDArtResolved* Catalog = ULDArtSubsystem::Get(this);
    ApplyLook(Catalog ? Catalog->ForMonster(CreatureId) : nullptr, MonsterTint(CreatureId),
        FString::Printf(TEXT("Camp tier %u"), uint32(CreatureId)));
}

void ULDHumanoidVisualComponent::CopyLook(const ULDHumanoidVisualComponent& Source)
{
    if (Source.LookCreature != 0) { ApplyMonsterLook(Source.LookCreature); }
    else if (Source.LookSlot != MAX_uint8) { ApplyPalette(Source.LookSlot); }
}

void ULDHumanoidVisualComponent::ApplyLook(const FLDArtCharacter* Look, const FLinearColor& Identity, const FString& What)
{
    using namespace LDHumanoidVisualDetail;
    Art = Look && Look->Mesh ? Look : nullptr;
    Paints.Reset();
    Mesh->EmptyOverrideMaterials();
    Clip = ELDArtAnim::Count;
    bFlashing = false;
    PickingFrame = MAX_uint64;
    PickingModel.Reset();
    PickingPose = {};
    if (!Art)
    {
        // The art subsystem has logged why its catalog is unavailable; this body draws nothing rather than a stand-in.
        UE_LOG(LogLDContent, Error, TEXT("LD_BODY_ART_UNAVAILABLE %s has no body in the Demo art catalog; nothing is drawn."), *What);
        WearParts(Identity, What);
        Mesh->SetSkeletalMeshAsset(nullptr);
        Mesh->SetHiddenInGame(true);
        for (UStaticMeshComponent* Part : Parts) { Part->SetHiddenInGame(true); }
        for (UStaticMeshComponent* Edge : Edges) { Edge->SetHiddenInGame(true); }
        return;
    }
    HalfHeight = GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    HeightCm = Art->HeightCm;
    Mesh->SetSkeletalMeshAsset(Art->Mesh);
    Mesh->SetRelativeTransform(Art->MeshTransform);
    Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    // Every slot takes the hit flash; only the catalog's tint slots take the identity colour.
    for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
    {
        UMaterialInstanceDynamic* Paint = Mesh->CreateDynamicMaterialInstance(Index);
        if (!ensureMsgf(Paint, TEXT("%s: art body %s has no material %d; rerun the art import"), *What, *Art->Id, Index)) { continue; }
        Paint->SetVectorParameterValue(ULDArtCatalog::FlashParameter, FLinearColor::Black);
        Paints.Add(Paint);
    }
    for (const FName Slot : Art->TintSlots)
    {
        const int32 Index = Mesh->GetMaterialIndex(Slot);
        auto* Paint = Index == INDEX_NONE ? nullptr : Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Index));
        if (ensureMsgf(Paint, TEXT("%s: art body %s has no tint slot %s; rerun the art import"), *What, *Art->Id, *Slot.ToString()))
        {
            Paint->SetVectorParameterValue(ULDArtCatalog::TintParameter, Identity);
        }
    }
    WearParts(Identity, What);
    Mesh->SetHiddenInGame(false);
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
    if (!ensureMsgf(Material, TEXT("Character material missing; rerun asset bootstrap"))) { return; }
    for (int32 Index = 0; Index < Parts.Num(); ++Index)
    {
        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
        Instance->SetVectorParameterValue(TEXT("Tint"), ColourOf(Index));
        Parts[Index]->SetMaterial(0, Instance);
    }
    UMaterialInstanceDynamic* Dark = UMaterialInstanceDynamic::Create(Material, this);
    Dark->SetVectorParameterValue(TEXT("Tint"), EdgeColour);
    for (UStaticMeshComponent* Edge : Edges) { Edge->SetMaterial(0, Dark); }
    Layout();
}

bool ULDHumanoidVisualComponent::TracePresentedModel(const FVector& Start, const FVector& End, double& OutFraction) const
{
    OutFraction = 1.;
    if (!Art || !IsVisible() || (GetOwner() && GetOwner()->IsHidden()) || Mesh->bHiddenInGame || FallAt >= 0 || !Mesh->GetSkeletalMeshAsset()
        || Start.ContainsNaN() || End.ContainsNaN()) { return false; }
    const FBox BroadBounds = Mesh->Bounds.GetBox().ExpandBy(HeightCm);
    if (!FMath::LineBoxIntersection(BroadBounds, Start, End, End - Start)) { return false; }
    const FSkeletalMeshRenderData* RenderData = Mesh->GetSkeletalMeshRenderData();
    const int32 LODIndex = Mesh->GetPredictedLODLevel();
    if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex)) { return false; }
    const FSkeletalMeshLODRenderData& LOD = RenderData->LODRenderData[LODIndex];
    FSkinWeightVertexBuffer* Weights = Mesh->GetSkinWeightBuffer(LODIndex);
    const FSkeletalMeshLODInfo* LODInfo=Mesh->GetSkeletalMeshAsset()->GetLODInfo(LODIndex);
    if (!ensureMsgf(Weights && LODInfo && LODInfo->bAllowCPUAccess,
        TEXT("Presented model picking needs CPU mesh data; rerun ImportDemoArt.ps1 -Mode Import."))) { return false; }
    if (PickingFrame != GFrameCounter)
    {
        PickingFrame = MAX_uint64;
        PickingModel = LDModelPicking::SharedSkinnedModel(*Mesh->GetSkeletalMeshAsset(), LODIndex, LOD, *Weights);
        if (!ensureMsgf(PickingModel, TEXT("Presented model picking needs CPU mesh data; rerun ImportDemoArt.ps1 -Mode Import.")))
        {
            return false;
        }
        // Unlike GetCPUSkinnedVertices this never flushes the render thread or changes the skinning mode; only the
        // leaves a ray reaches are skinned.
        Mesh->CacheRefToLocalMatrices(PickingPose.RefToLocal);
        if (!ensureMsgf(LDModelPicking::PoseBounds(*PickingModel, PickingPose),
            TEXT("%s has fewer bone matrices than its picking model needs."), *Mesh->GetSkeletalMeshAsset()->GetName())) { return false; }
        PickingFrame = GFrameCounter;
    }
    const FTransform& Transform = Mesh->GetComponentTransform();
    return LDModelPicking::TraceSkinned(*PickingModel, PickingPose, Transform.InverseTransformPosition(Start),
        Transform.InverseTransformPosition(End), [this, LODIndex](int32 Material) { return Mesh->IsMaterialSectionShown(Material, LODIndex); },
        OutFraction);
}

bool ULDHumanoidVisualComponent::PresentedPickVolume(FVector& OutFoot, FVector& OutTop, double& OutRadiusCm) const
{
    // The root ring's engine cylinder is 100 cm across at scale 1.05: the footprint shown under a body.
    constexpr double FootprintRadiusCm = 52.5;
    if (!Art || !IsVisible() || (GetOwner() && GetOwner()->IsHidden()) || Mesh->bHiddenInGame || FallAt >= 0
        || !Mesh->GetSkeletalMeshAsset() || HeightCm <= 0) { return false; }
    const FTransform& Transform = GetComponentTransform();
    OutFoot = Transform.TransformPosition(FVector(0, 0, -HalfHeight));
    OutTop = Transform.TransformPosition(FVector(0, 0, HeightCm - HalfHeight));
    OutRadiusCm = FootprintRadiusCm * Transform.GetMaximumAxisScale();
    return !OutFoot.ContainsNaN() && !OutTop.ContainsNaN();
}

void ULDHumanoidVisualComponent::Layout()
{
    using namespace LDHumanoidVisualDetail;
    // Pivot space at rest: the capsule centre, the feet HalfHeight below it and the top of the head HeightCm above them.
    const float Foot = -HalfHeight, Top = Foot + HeightCm, Girth = HeightCm / ReferenceHeightCm;
    Parts[RootRing]->SetRelativeLocation(FVector(0, 0, Foot + 3.f));
    Parts[RootRim]->SetRelativeLocation(FVector(0, 0, Foot + 2.8f));
    Parts[ShackleLeft]->SetRelativeLocation(FVector(0, -11.f * Girth, Foot + 16.f));
    Parts[ShackleRight]->SetRelativeLocation(FVector(0, 11.f * Girth, Foot + 16.f));
    Parts[ShieldHalo]->SetRelativeLocation(FVector(0, 0, Top + 14.f));
    for (int32 Bar = 0; Bar < ShieldBars; ++Bar)
    {
        Parts[ShieldBar0 + Bar]->SetRelativeScale3D(FVector(.06f, .06f, (HeightCm + 14.f) / 100.f));
    }
    Parts[ProtectionBeam]->SetRelativeLocation(FVector(0, 0, Top + 79.f));
    Parts[ReturnSign]->SetRelativeLocation(FVector(-24.f * Girth, 0, Foot + .74f * HeightCm));
    Parts[SpawnColumn]->SetRelativeLocation(FVector(0, 0, Foot + 160.f));
}

void ULDHumanoidVisualComponent::SetCue(uint8 Value, float ActionAgeSeconds, float ImpactDelaySeconds)
{
    using LDBodyMotion::EAction;
    const uint8 Code = Value & ActionMask;
    const EAction Next = Code == 1 ? EAction::Attack : Code == 2 ? EAction::Cast : Code == 4 ? EAction::Hit : EAction::None;
    const bool bAgeKnown = ActionAgeSeconds >= 0.f && FMath::IsFinite(ActionAgeSeconds);
    const double Onset = bAgeKnown ? Clock - ActionAgeSeconds : Clock;
    if (LDBodyMotion::IsNewInstance(Action, ActionOnset, Next, Onset, bAgeKnown)) { ActionOnset = Onset; }
    ActionImpactSeconds = FMath::IsFinite(ImpactDelaySeconds) && ImpactDelaySeconds >= 0.f
        ? ImpactDelaySeconds : LDBodyMotion::StrikeAt;
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

void ULDHumanoidVisualComponent::SetHitReaction(float AgeSeconds)
{
    if (!FMath::IsFinite(AgeSeconds) || AgeSeconds<0.f || AgeSeconds>=.3f || FallAt>=0) { return; }
    const double Onset=Clock-AgeSeconds;
    if (!bHitSeen || Onset>HitOnset+LDBodyMotion::SameInstanceSeconds) { HitOnset=Onset; bHitSeen=true; }
}

void ULDHumanoidVisualComponent::PlayFall()
{
    FallAt = Clock;
    Action = LDBodyMotion::EAction::None;
}

bool ULDHumanoidVisualComponent::IsFallen() const
{
    return FallAt >= 0 && LDBodyMotion::Fall(float(Clock - FallAt), LieSeconds()).bGone;
}

float ULDHumanoidVisualComponent::LieSeconds() const
{
    const FLDArtAnimation* Dying = Art ? Art->Find(ELDArtAnim::Death) : nullptr;
    return Dying && Dying->PlayRate > 0.f ? LDHumanoidVisualDetail::LengthOf(Dying) / Dying->PlayRate : 0.f;
}

void ULDHumanoidVisualComponent::SetFlash(const FLinearColor& Emissive)
{
    for (UMaterialInstanceDynamic* Paint : Paints) { Paint->SetVectorParameterValue(ULDArtCatalog::FlashParameter, Emissive); }
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
        Edges[Index]->SetRelativeScale3D(Marker->GetRelativeScale3D() + EdgeSpecs[Index].Grow);
    }
}

void ULDHumanoidVisualComponent::PlayClip(bool bFalling, float Speed)
{
    using namespace LDHumanoidVisualDetail;
    using LDBodyMotion::EAction;
    // The state that shows now: dying, an action, the respawn wave while standing, then running or standing. One-shots
    // are posed at a clip time from the presentation clock; loops play at a rate.
    ELDArtAnim State = Speed >= RunFromCmPerSec ? ELDArtAnim::Run : ELDArtAnim::Idle;
    float Time = 0.f;
    const auto Pose = [this, &State, &Time](ELDArtAnim Candidate, float At, bool bHold)
    {
        const float Length = LengthOf(Art->Find(Candidate));
        if (Length <= 0.f || (!bHold && At >= Length)) { return false; }
        State = Candidate;
        Time = FMath::Min(At, Length);
        return true;
    };
    const FLDArtAnimation* Dying = Art->Find(ELDArtAnim::Death);
    const FLDArtAnimation* Acting = Action == EAction::None ? nullptr : Art->Find(ClipOf(Action));
    const float ActionAge=LDBodyMotion::ImpactPoseAge(Action,float(Clock-ActionOnset),bHitSeen ? float(Clock-HitOnset) : -1.f);
    const FLDArtAnimation* Waving = Art->Find(ELDArtAnim::Spawn);
    const bool bPosed = bFalling
        ? Dying && Pose(ELDArtAnim::Death, float(Clock - FallAt) * Dying->PlayRate, true)
        : (Acting && Pose(ClipOf(Action), LDBodyMotion::ClipTime(Action, ActionAge, Acting->ImpactSec, ActionImpactSeconds, Acting->PlayRate), false))
            || (Waving && AppearAt >= 0 && Speed < RunFromCmPerSec && Pose(ELDArtAnim::Spawn, float(Clock - AppearAt) * Waving->PlayRate, false));
    const FLDArtAnimation* Entry = Art->Find(State);
    if (!Entry || !Entry->Sequence) { return; }
    if (State != Clip)
    {
        Mesh->PlayAnimation(Entry->Sequence, Entry->bLoop);
        Clip = State;
    }
    if (bPosed)
    {
        // Posed explicitly every frame, so a late age or a slow frame never drifts from the timeline.
        Mesh->Stop();
        Mesh->SetPosition(Time, false);
        return;
    }
    Mesh->SetPlayRate(State == ELDArtAnim::Run ? Speed / FMath::Max(Art->RunSpeedCmPerSec, 1.f) : Entry->PlayRate);
    if (!Mesh->IsPlaying()) { Mesh->Play(true); }
}

void ULDHumanoidVisualComponent::Animate(const FVector& Velocity, float Delta)
{
    using namespace LDHumanoidVisualDetail;
    Clock += FMath::Max(Delta, 0.f);
    PickingFrame = MAX_uint64;
    if (!Art) { return; }
    const bool bFalling = FallAt >= 0;
    LDBodyMotion::FLife Life;
    if (bFalling) { Life = LDBodyMotion::Fall(float(Clock - FallAt), LieSeconds()); }
    else if (AppearAt >= 0) { Life = LDBodyMotion::Appear(float(Clock - AppearAt)); }
    // The current action's effects: the lunge and the orb ease toward the timeline, the spark and the flash follow it.
    const LDBodyMotion::FChannels Target = bFalling ? LDBodyMotion::FChannels()
        : LDBodyMotion::WithHit(Action, float(Clock - ActionOnset),bHitSeen ? float(Clock-HitOnset) : -1.f,ActionImpactSeconds);
    const float Ease = 1.f - FMath::Exp(-40.f * FMath::Max(Delta, 0.f));
    Shown.Lunge = FMath::Lerp(Shown.Lunge, Target.Lunge, Ease);
    Shown.Charge = FMath::Lerp(Shown.Charge, Target.Charge, Ease);
    const APlayerController* Viewer = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
    const ALDPlayerController* LocalController = Cast<ALDPlayerController>(Viewer);
    const float EffectScale = LocalController && LocalController->Controls && LocalController->Controls->bReduceCombatEffects ? .45f : 1.f;
    Shown.Burst = Target.Burst * EffectScale;
    Shown.Flash = Target.Flash * EffectScale;
    Shown.HitBurst = Target.HitBurst * EffectScale;

    // Ease visual facing independently from the capsule so authority turns stay immediate without snapping the art.
    const AActor* Owner = GetOwner();
    const FRotator ActorRotation = Owner ? Owner->GetActorRotation() : FRotator::ZeroRotator;
    const float TargetYaw = ActorRotation.Yaw;
    if (!bVisualPoseInitialized)
    {
        VisualYaw = TargetYaw;
        PreviousVelocity = Velocity;
        bVisualPoseInitialized = true;
    }
    else if (Delta > SMALL_NUMBER)
    {
        VisualYaw = FMath::FixedTurn(VisualYaw, TargetYaw, 540.f * Delta);
        const FVector LocalAcceleration = ActorRotation.UnrotateVector((Velocity - PreviousVelocity) / Delta);
        LeanPitch = FMath::FInterpTo(LeanPitch, FMath::Clamp(LocalAcceleration.X / 8000.f, -1.f, 1.f) * 8.f, Delta, 7.f);
        LeanRoll = FMath::FInterpTo(LeanRoll, FMath::Clamp(LocalAcceleration.Y / 8000.f, -1.f, 1.f) * 8.f, Delta, 7.f);
        PreviousVelocity = Velocity;
    }
    const FRotator VisualLean(LeanPitch, FRotator::NormalizeAxis(VisualYaw - TargetYaw), LeanRoll);
    Mesh->SetRelativeRotation(Art->MeshTransform.GetRotation() * FQuat(VisualLean));

    // The whole body: a lunge or knock-back, the appear scale and the final sink, all about the feet.
    const FVector Feet(0, 0, -HalfHeight);
    Pivot->SetRelativeLocation(Feet * (1.f - Life.Scale) + FVector(Shown.Lunge, 0, -Life.Sink));
    Pivot->SetRelativeScale3D(FVector(FMath::Max(Life.Scale, .01f)));

    // A rooted body stands even while a correction still moves it.
    const bool bRooted = (Cue & Rooted) != 0;
    PlayClip(bFalling, bRooted || bFalling ? 0.f : float(Velocity.Size2D()));

    // Effects: the spark at the striking or casting hand, or at the chest of a hit body; the orb at the casting hand.
    const FTransform& Space = Pivot->GetComponentTransform();
    const FVector Hand = Space.InverseTransformPosition(Mesh->GetSocketLocation(Art->HandBone));
    const bool bBurst = Shown.Burst > .02f;
    Parts[Burst]->SetHiddenInGame(!bBurst);
    if (bBurst)
    {
        Parts[Burst]->SetRelativeLocation(Action == LDBodyMotion::EAction::Hit
            ? FVector(18.f, 0, -HalfHeight + .72f * HeightCm) : Hand + FVector(8.f, 0, 0));
        Parts[Burst]->SetRelativeScale3D(FVector(.55f * Shown.Burst));
    }
    Parts[HitSpark]->SetHiddenInGame(Shown.HitBurst<=.02f);
    if (Shown.HitBurst>.02f)
    {
        Parts[HitSpark]->SetRelativeLocation(FVector(18.f,0,-HalfHeight+.72f*HeightCm));
        Parts[HitSpark]->SetRelativeScale3D(FVector(.55f*Shown.HitBurst));
    }
    const bool bCharge = Shown.Charge > .02f;
    Parts[Charge]->SetHiddenInGame(!bCharge);
    if (bCharge)
    {
        Parts[Charge]->SetRelativeLocation(Hand);
        Parts[Charge]->SetRelativeScale3D(FVector(.1f + .25f * Shown.Charge * EffectScale));
    }
    const bool bColumn = Life.Column > .02f;
    Parts[SpawnColumn]->SetHiddenInGame(!bColumn);
    if (bColumn) { Parts[SpawnColumn]->SetRelativeScale3D(FVector(.15f + .75f * Life.Column, .15f + .75f * Life.Column, 3.2f)); }

    // Status markers, one shape each; a falling body shows none.
    const float Time = float(FMath::Fmod(Clock, 3600.0));
    const float Foot = -HalfHeight, Girth = HeightCm / ReferenceHeightCm;
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
        Parts[ShieldBar0 + Bar]->SetRelativeLocation(
            FVector(56.f * Girth * FMath::Cos(Angle), 56.f * Girth * FMath::Sin(Angle), Foot + .5f * (HeightCm + 14.f)));
    }
    const bool bProtected = !bFalling && (Cue & Protected);
    Parts[ProtectionBeam]->SetHiddenInGame(!bProtected);
    for (int32 Orb = 0; Orb < ProtectionOrbs; ++Orb)
    {
        Parts[ProtectionOrb0 + Orb]->SetHiddenInGame(!bProtected);
        if (!bProtected) { continue; }
        const float Angle = FMath::DegreesToRadians(90.f * Orb + 200.f * Time);
        Parts[ProtectionOrb0 + Orb]->SetRelativeLocation(FVector(62.f * Girth * FMath::Cos(Angle), 62.f * Girth * FMath::Sin(Angle),
            Foot + .48f * HeightCm + 8.f * FMath::Sin(4.f * Time + 1.6f * Orb)));
    }
    Parts[ReturnSign]->SetHiddenInGame(bFalling || !(Cue & Returning));
    PlaceEdges();

    // Body flash: a hit flashes white, then red; an appear glows. Cleared once both have passed.
    const float Glow = bFalling ? 0.f : Life.Glow;
    if (Shown.Flash > 0.f || Glow > 0.f)
    {
        const bool bHitFlash = Shown.Flash >= Glow;
        const float Amount = FMath::Min(1.f, bHitFlash ? 1.15f * Shown.Flash : Glow);
        const FLinearColor Over = bHitFlash
            ? FMath::Lerp(FlashRed, FlashWhite, FMath::Clamp((Shown.Flash - .6f) / .4f, 0.f, 1.f)) : AppearGlow;
        SetFlash(Over * Amount);
        bFlashing = true;
    }
    else if (bFlashing)
    {
        SetFlash(FLinearColor::Black);
        bFlashing = false;
    }
}
