#include "Character/LDHumanoidVisualComponent.h"
#include "Audio/LDAudioFeel.h"
#include "Components/AudioComponent.h"
#include "Core/LDLog.h"
#include "Animation/AnimSequence.h"
#include "Art/LDArtCatalog.h"
#include "Art/LDArtSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/LDCharacter.h"
#include "Character/LDBasketballAnimInstance.h"
#include "Character/LDCombatVfxCatalog.h"
#include "Character/LDHitFeel.h"
#include "Character/LDImpactFx.h"
#include "Character/LDModelPicking.h"
#include "Combat/LDCombatElements.h"
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
#include "Misc/App.h"
#include "NiagaraComponent.h"
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
        Burst, SpawnColumn, PartCount
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
    // release spark and the appear column. The feet discs stand above the own-body marker's.
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
        {TEXT("SpawnColumn"), Cylinder, {.9,.9,3.2}},
    };
    // One fixed colour per status on every unit, apart from all eight seat tints, the turf and the red track:
    // root hot magenta, shield bright cyan, protection white-gold; each edged in near-black.
    const FLinearColor RootColour(1.f,.02f,.58f), EdgeColour(.012f,.01f,.016f), ShieldColour(0.f,.78f,1.f);
    const FLinearColor ProtectionColour(1.f,.92f,.62f), ReturnColour(.95f,.95f,.95f);
    const FLinearColor BurstColour(1.f,.95f,.7f), ColumnColour(1.f,.9f,.55f);
    // Emissive over the whole body through the art materials' Flash: a typed hit flashes, then white; an appear glows.
    const FLinearColor FlashWhite(1.f,1.f,1.f), AppearGlow(1.f,.92f,.6f);
    FLinearColor HitColour(ELDObservedHitLook Look)
    {
        switch (Look)
        {
        case ELDObservedHitLook::Magic: return FLinearColor(.62f,.22f,1.f);
        case ELDObservedHitLook::Fire: return FLinearColor(1.f,.22f,.025f);
        case ELDObservedHitLook::Water: return FLinearColor(.05f,.62f,1.f);
        case ELDObservedHitLook::Electric: return FLinearColor(1.f,.78f,.05f);
        default: return BurstColour;
        }
    }
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
        if (Index == Burst) { return BurstColour; }
        return ColumnColour;
    }
    ELDArtAnim ClipOf(LDBodyMotion::EAction Action)
    {
        switch (Action)
        {
        case LDBodyMotion::EAction::Attack: return ELDArtAnim::Attack;
        case LDBodyMotion::EAction::Cast: return ELDArtAnim::Cast;
        default: return ELDArtAnim::Count;
        }
    }
    // Status markers and effects toggle hidden often, and every toggle recreates the part's render proxy: kept out of
    // Lumen's surface cache and the distance-field scene, a toggle never asks for a card capture or a distance-field
    // object update. They are small tinted cues that light nothing around them.
    void KeepOutOfSceneLighting(UPrimitiveComponent& Part)
    {
        Part.bAffectDynamicIndirectLighting = false;
        Part.bAffectDistanceFieldLighting = false;
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
    // Most bodies are never shown on this machine: the mesh ticks only once a look is applied (ApplyLook activates it).
    // Auto-activation would turn the tick back on.
    Mesh->bAutoActivate = false;
    Mesh->PrimaryComponentTick.bStartWithTickEnabled = false;
    Basketball = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Basketball"));
    Basketball->SetupAttachment(Mesh);
    Basketball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Basketball->SetGenerateOverlapEvents(false);
    Basketball->SetCanEverAffectNavigation(false);
    Basketball->SetHiddenInGame(true);
    KeepOutOfSceneLighting(*Basketball);
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
        KeepOutOfSceneLighting(*Part);
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
        KeepOutOfSceneLighting(*Part);
        Edges.Add(Part);
    }
}

void ULDHumanoidVisualComponent::OnRegister()
{
    Super::OnRegister();
    if (!GetWorld()) { return; }
    // Non-body layout points also own this hidden tree. Mark every child before validation/registration,
    // even when no character look has ever been applied.
    if (IsEditorOnly())
    {
        Pivot->bIsEditorOnly = true;
        Mesh->bIsEditorOnly = true;
        Basketball->bIsEditorOnly = true;
        for (UStaticMeshComponent* Part : Parts) { Part->bIsEditorOnly = true; }
        for (UStaticMeshComponent* Edge : Edges) { Edge->bIsEditorOnly = true; }
        for (UMeshComponent* Part : Worn) { if (Part) { Part->bIsEditorOnly = true; } }
    }
    if (!Pivot->IsRegistered()) { Pivot->RegisterComponent(); }
    if (!Mesh->IsRegistered()) { Mesh->RegisterComponent(); }
    if (!Basketball->IsRegistered()) { Basketball->RegisterComponent(); }
    if (GetOwner()) { Mesh->AddTickPrerequisiteActor(GetOwner()); }
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

void ULDHumanoidVisualComponent::OnUnregister()
{
    ClearImpacts();
    Super::OnUnregister();
}

void ULDHumanoidVisualComponent::OnVisibilityChanged()
{
    Super::OnVisibilityChanged();
    if (!GetVisibleFlag()) { ClearImpacts(); }
    if (!GetVisibleFlag()) { Basketball->SetHiddenInGame(true); }
}

void ULDHumanoidVisualComponent::OnHiddenInGameChanged()
{
    Super::OnHiddenInGameChanged();
    if (bHiddenInGame) { ClearImpacts(); }
    if (bHiddenInGame) { Basketball->SetHiddenInGame(true); }
}

void ULDHumanoidVisualComponent::ClearImpacts()
{
    PendingImpacts.Reset();
    UWorld* World = GetWorld();
    if (auto* Impacts = World ? World->GetSubsystem<ULDImpactFxSubsystem>() : nullptr) { Impacts->ClearFor(*this); }
    if (IsValid(CastCharge)) { CastCharge->DeactivateImmediate(); }
    if (IsValid(DashTrail)) { DashTrail->DeactivateImmediate(); }
    if (IsValid(ChargeColumn)) { ChargeColumn->DeactivateImmediate(); }
    bTelegraphShown = false;
    bCastEmitting = false;
    bDashing = false;
    bChargeShown = false;
    bChargeReady = false;
}

bool ULDHumanoidVisualComponent::CanShowImpact() const
{
    const AActor* Owner = GetOwner();
    return FallAt < 0 && Owner && !Owner->IsHidden() && IsVisible() && !bHiddenInGame
        && Mesh && Mesh->IsVisible() && !Mesh->bHiddenInGame;
}

namespace LDHumanoidVisualSlice
{
    // The skills with a designed presentation (LDSliceVfx); every other cast keeps its charge and release burst.
    bool IsSliceCast(uint8 Ability)
    {
        return Ability == uint8(ELDDemoAbilityId::ActiveHoseDown) || Ability == uint8(ELDDemoAbilityId::ActiveZap)
            || Ability == uint8(ELDDemoAbilityId::ActiveSeethe) || Ability == uint8(ELDDemoAbilityId::ActiveColaShake);
    }
}

void ULDHumanoidVisualComponent::UpdateSlice(float EffectScale)
{
    UWorld* World = GetWorld();
    ULDImpactFxSubsystem* Impacts = World ? World->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
    const bool bAttack = Action == LDBodyMotion::EAction::Attack;
    const bool bSkill = Action == LDBodyMotion::EAction::Cast && !bOverdriveCast && LDHumanoidVisualSlice::IsSliceCast(CastAbility);
    if (!Impacts || !Art || !FApp::CanEverRender() || !CanShowImpact() || EffectScale <= 0.f || (!bAttack && !bSkill))
    {
        // A withdrawn, cancelled or ended windup takes its telegraph with it.
        if (bTelegraphShown && Impacts) { Impacts->ClearTelegraphs(*this); }
        bTelegraphShown = false;
        return;
    }
    const FTransform& Space = Pivot->GetComponentTransform();
    const FVector Feet = Space.TransformPosition(FVector(0, 0, -HalfHeight));
    const FVector Facing = GetOwner() ? GetOwner()->GetActorForwardVector() : Space.GetUnitAxis(EAxis::X);
    const float Age = ShownAge();
    const uint8 Step = FMath::Clamp<uint8>(ComboStep, 1, 3);
    if (TelegraphOnset != ActionOnset)
    {
        TelegraphOnset = ActionOnset;
        // Anticipation of what is coming, from the windup's start to its release (a swing's arc on the floor, the hose's
        // band, the zap's charge at the hand); a windup first seen after its release shows none.
        if (Age < ActionImpactSeconds)
        {
            const LDSliceVfx::FLook Look = Impacts->SliceLook(ULDImpactFxSubsystem::ESliceTuning::Telegraph, bOwnBody, bOwnBody);
            if (bAttack)
            {
                Impacts->SwingTell(*this, Step, Feet, Facing, ActionImpactSeconds, Look, Age);
                bTelegraphShown = true;
            }
            else if (CastAbility == uint8(ELDDemoAbilityId::ActiveHoseDown))
            {
                // The band it will soak, at the approved reach and width (danger information: never scaled by tuning).
                float Range = 0.f, HalfWidth = 0.f;
                const FVector Ahead = FVector(Facing.X, Facing.Y, 0.).GetSafeNormal();
                if (Impacts->SkillReach(CastAbility, Range, HalfWidth) && !Ahead.IsNearlyZero())
                {
                    const FVector Floor = Impacts->FloorUnder(Feet);
                    bTelegraphShown |= Impacts->Play(LDSliceVfx::ESystem::HoseTell, *this, Floor, Floor + Ahead * Range,
                        LDSliceVfx::ElementInk(uint8(ELDCombatElement::Water)), HalfWidth, ActionImpactSeconds, Look, Age, true) != nullptr;
                }
            }
            else if (CastAbility == uint8(ELDDemoAbilityId::ActiveColaShake))
            {
                // The cone it will blast, at the approved reach and angle.
                float Range = 0.f, HalfWidth = 0.f;
                const float Degrees = Impacts->SkillConeDegrees(CastAbility);
                const FVector Ahead = FVector(Facing.X, Facing.Y, 0.).GetSafeNormal();
                if (Impacts->SkillReach(CastAbility, Range, HalfWidth) && Degrees > 0.f && !Ahead.IsNearlyZero())
                {
                    const FVector Floor = Impacts->FloorUnder(Feet);
                    bTelegraphShown |= Impacts->Play(LDSliceVfx::ESystem::ColaTell, *this, Floor, Floor + Ahead * Range,
                        LDSliceVfx::ElementInk(uint8(ELDCombatElement::Chill)), .5f * Degrees, ActionImpactSeconds, Look, Age, true) != nullptr;
                }
            }
            else if (CastAbility == uint8(ELDDemoAbilityId::ActiveSeethe))
            {
                // Its self area at the approved radius, heat gathering at the feet.
                const float Radius = Impacts->SkillEffectRadius(CastAbility, uint16(ELDDemoEffectOp::SelfArea));
                const FVector Floor = Impacts->FloorUnder(Feet);
                bTelegraphShown |= Radius > 0.f && Impacts->Play(LDSliceVfx::ESystem::SeetheTell, *this, Floor, Floor,
                    LDSliceVfx::ElementInk(uint8(ELDCombatElement::Fire)), Radius, ActionImpactSeconds, Look, Age, true) != nullptr;
            }
            else if (Mesh)
            {
                // A unit-targeted chain: its target is never public, so only the casting hand charges.
                bTelegraphShown |= Impacts->Play(LDSliceVfx::ESystem::ZapCharge, *this, Mesh->GetSocketLocation(Art->HandBone), Mesh->GetSocketLocation(Art->HandBone),
                    LDSliceVfx::ElementInk(uint8(ELDCombatElement::Shock)), 40.f * Look.Size, ActionImpactSeconds, Look, Age, true) != nullptr;
            }
        }
    }
    // Release of a basic swing at its authoritative impact time: the slash at the chest (skills release in UpdateRelease).
    if (bAttack && SwingReleasedOnset != ActionOnset && Age >= ActionImpactSeconds)
    {
        SwingReleasedOnset = ActionOnset;
        bTelegraphShown = false;
        const float Late = Age - ActionImpactSeconds;
        if (Late < .2f)
        {
            const FVector Centre = Space.TransformPosition(FVector(0, 0, .55f * HeightCm - HalfHeight));
            Impacts->Swing(*this, Step, Centre, Facing, Impacts->SliceLook(ULDImpactFxSubsystem::ESliceTuning::Swing, bOwnBody, bOwnBody), Late);
            Impacts->NoteStrike(*this, Step >= 3 ? LDSliceVfx::EStrike::Finisher : LDSliceVfx::EStrike::Basic, Centre, Facing, bOwnBody);
        }
    }
}
void ULDHumanoidVisualComponent::SetElementStates(const TArray<FLDObservedStatus>& Statuses)
{
    static constexpr ELDDemoStatusId Ids[ElementStates] = {ELDDemoStatusId::Burn, ELDDemoStatusId::Wet, ELDDemoStatusId::Electrified,
        ELDDemoStatusId::Chilled, ELDDemoStatusId::Rotten};
    for (int32 Index = 0; Index < ElementStates; ++Index)
    {
        const FLDObservedStatus* Found = Statuses.FindByPredicate([Index](const FLDObservedStatus& Status) { return Status.StatusId == uint8(Ids[Index]); });
        StateStacks[Index] = Found ? FMath::Max<uint8>(Found->Stacks, 1) : 0;
    }
}

void ULDHumanoidVisualComponent::UpdateStates(bool bFalling, float EffectScale)
{
    static constexpr LDSliceVfx::ESystem Systems[ElementStates] = {LDSliceVfx::ESystem::BurnState, LDSliceVfx::ESystem::WetState,
        LDSliceVfx::ESystem::ShockState, LDSliceVfx::ESystem::ChillState, LDSliceVfx::ESystem::RotState};
    UWorld* World = GetWorld();
    ULDImpactFxSubsystem* Impacts = World ? World->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
    const bool bShow = Impacts && Art && FApp::CanEverRender() && CanShowImpact() && !bFalling && EffectScale > 0.f;
    const FTransform& Space = Pivot->GetComponentTransform();
    const FVector Feet = Space.TransformPosition(FVector(0, 0, -HalfHeight)), Head = Space.TransformPosition(FVector(0, 0, HeightCm - HalfHeight));
    for (int32 Index = 0; Index < ElementStates; ++Index)
    {
        // A state starting on a body the local player sees stings once, whatever the effect strength.
        const bool bStarted = StateStacks[Index] > 0 && !bFalling;
        if (bStarted && !StateSounded[Index] && CanShowImpact()) { LDAudioFeel::StateOnset(*this, bOwnBody, Index, Pivot->GetComponentLocation()); }
        StateSounded[Index] = bStarted;
        const bool bOn = bShow && StateStacks[Index] > 0;
        if (!bOn)
        {
            if (IsValid(StateEffects[Index])) { StateEffects[Index]->DestroyComponent(); }
            StateEffects[Index] = nullptr;
            continue;
        }
        // Stacks raise the strength (others' bodies a little dimmer); the radius follows the body's height.
        const float Strength = 2.2f * FMath::Min(1.f + .25f * (StateStacks[Index] - 1), 1.75f) * (bOwnBody ? 1.f : .75f) * FMath::Clamp(EffectScale, .45f, 1.f);
        StateEffects[Index] = Impacts->ShowLoop(*Pivot, StateEffects[Index], Systems[Index], Feet, Head,
            LDSliceVfx::ElementInk(uint8(Index == 0 ? ELDCombatElement::Fire : Index == 1 ? ELDCombatElement::Water : Index == 2 ? ELDCombatElement::Shock
                : Index == 3 ? ELDCombatElement::Chill : ELDCombatElement::Rot)), .17f * HeightCm, Strength, 1.e4f, bOwnBody);
    }
}

void ULDHumanoidVisualComponent::UpdateCastCharge(float Charge, float EffectScale)
{
    const LDHitFeel::FTuning Tuning = LDHitFeel::Read();
    const bool bWindow = FApp::CanEverRender() && CanShowImpact() && Art
        && Action == LDBodyMotion::EAction::Cast && Charge > .02f && EffectScale > 0.f
        && Tuning.CastScale > 0.f && Tuning.CastGlow > 0.f && (bOverdriveCast || !LDHumanoidVisualSlice::IsSliceCast(CastAbility));
    if (!bWindow)
    {
        if (bCastEmitting && IsValid(CastCharge)) { CastCharge->Deactivate(); }
        bCastEmitting = false;
        return;
    }
    const UWorld* World = GetWorld();
    const ULDImpactFxSubsystem* Effects = World ? World->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
    const ULDCombatVfxCatalog* Catalog = Effects ? Effects->GetCatalog() : nullptr;
    if (!Catalog || !Catalog->CastCharge) { return; }
    if (!IsValid(CastCharge))
    {
        CastCharge = NewObject<UNiagaraComponent>(GetOwner(), NAME_None, RF_Transient);
        if (!CastCharge)
        {
            UE_LOG(LogLDContent, Warning, TEXT("LD_CAST_FX spawn=failed"));
            return;
        }
        CastCharge->SetAutoActivate(false);
        CastCharge->SetAsset(Catalog->CastCharge);
        // The hand bone can carry import scale; Niagara sizes are authored in world centimeters.
        CastCharge->SetAbsolute(false, false, true);
        CastCharge->SetupAttachment(Mesh, Art->HandBone);
        CastCharge->RegisterComponent();
    }
    else if (CastCharge->GetAttachSocketName() != Art->HandBone)
    {
        CastCharge->DeactivateImmediate();
        CastCharge->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Art->HandBone);
        bCastEmitting = false;
    }

    const float Strength = FMath::Clamp(Charge, 0.f, 1.f);
    const float Thin = FMath::Clamp(EffectScale, 0.f, 1.f);
    const bool bStart = !bCastEmitting || ActionOnset != CastActionOnset;
    if (bStart && bCastEmitting) { CastCharge->DeactivateImmediate(); }
    const LDHitVfx::FSkillLook* Skill = LDHitVfx::SkillLookOf(CastAbility);
    FLinearColor Colour = (Skill ? Skill->Tint : Catalog->CastTint) * Tuning.CastGlow;
    Colour.A = Catalog->CastTint.A * Strength * Thin;
    CastCharge->SetVariableFloat(TEXT("User.Scale"), Tuning.CastScale * (.25f + .75f * Strength) * FMath::Sqrt(Thin));
    CastCharge->SetVariableFloat(TEXT("User.SparkRate"), Tuning.CastSparksPerSecond * Strength * Thin);
    CastCharge->SetVariableLinearColor(TEXT("User.Colour"), Colour);
    if (bStart)
    {
        CastActionOnset = ActionOnset;
        bCastEmitting = true;
        CastCharge->Activate(true);
    }
}

void ULDHumanoidVisualComponent::UpdateRelease(float EffectScale)
{
    // Once per cast, when its timeline reaches the release: the skill's own burst at the casting hand (an aura or a
    // dash at the chest).
    if (Action != LDBodyMotion::EAction::Cast || !Art || ReleasedOnset == ActionOnset || ShownAge() < ActionImpactSeconds) { return; }
    ReleasedOnset = ActionOnset;
    const LDHitVfx::FSkillLook* Skill = LDHitVfx::SkillLookOf(CastAbility);
    UWorld* World = GetWorld();
    ULDImpactFxSubsystem* Impacts = World ? World->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
    if (!Impacts || !CanShowImpact()) { return; }
    if (bOverdriveCast)
    {
        // An overdrive release: the shockwave and its burst over the skill's own release.
        const FTransform& Space = Pivot->GetComponentTransform();
        const FVector Chest = Space.TransformPosition(FVector(0, 0, .5f * HeightCm - HalfHeight));
        Impacts->Layers(this, Chest, Space.TransformPosition(FVector(0, 0, -HalfHeight)),
            LDHitVfx::ForOverdriveRelease(EffectScale, Impacts->LayerTuning()), 0.f);
        Impacts->Kit(Chest, LDHitVfx::BurstOf(LDHitVfx::EBurst::Overdrive, LDHitVfx::OverdriveTint(), EffectScale), this);
    }
    if (!Skill) { return; }
    const bool bBody = Skill->Release == LDHitVfx::EBurst::Aura || Skill->Release == LDHitVfx::EBurst::Dash;
    const FVector At = bBody ? Pivot->GetComponentTransform().TransformPosition(FVector(0, 0, .45f * HeightCm - HalfHeight))
        : Mesh->GetSocketLocation(Art->HandBone);
    const FVector Facing = GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector;
    const float Late = FMath::Max(ShownAge() - ActionImpactSeconds, 0.f);
    const LDSliceVfx::FLook Look = Impacts->SliceLook(ULDImpactFxSubsystem::ESliceTuning::Impact, bOwnBody, bOwnBody);
    if (CastAbility == uint8(ELDDemoAbilityId::ActiveHoseDown) && !bOverdriveCast)
    {
        // Release: the stream over the approved band from the hand along the cast direction; its hits splash.
        float Range = 0.f, HalfWidth = 0.f;
        const FVector Ahead = FVector(Facing.X, Facing.Y, 0.).GetSafeNormal();
        if (Impacts->SkillReach(CastAbility, Range, HalfWidth) && !Ahead.IsNearlyZero())
        {
            const FVector Floor = Impacts->FloorUnder(Pivot->GetComponentTransform().TransformPosition(FVector(0, 0, -HalfHeight)));
            Impacts->Play(LDSliceVfx::ESystem::HoseJet, *this, Floor, Floor + Ahead * Range, LDSliceVfx::ElementInk(uint8(ELDCombatElement::Water)),
                HalfWidth, 1.4f * Look.Seconds, Look, Late);
        }
        Impacts->NoteStrike(*this, LDSliceVfx::EStrike::Hose, At, Facing, bOwnBody, CastAbility);
    }
    else if (CastAbility == uint8(ELDDemoAbilityId::ActiveColaShake) && !bOverdriveCast)
    {
        // Release: the icy fizz blast over the approved cone and its frosted floor.
        float Range = 0.f, HalfWidth = 0.f;
        const float Degrees = Impacts->SkillConeDegrees(CastAbility);
        const FVector Ahead = FVector(Facing.X, Facing.Y, 0.).GetSafeNormal();
        if (Impacts->SkillReach(CastAbility, Range, HalfWidth) && Degrees > 0.f && !Ahead.IsNearlyZero())
        {
            const FVector Floor = Impacts->FloorUnder(Pivot->GetComponentTransform().TransformPosition(FVector(0, 0, -HalfHeight)));
            Impacts->Play(LDSliceVfx::ESystem::ColaBlast, *this, Floor, Floor + Ahead * Range, LDSliceVfx::ElementInk(uint8(ELDCombatElement::Chill)),
                .5f * Degrees, 1.5f * Look.Seconds, Look, Late);
        }
        Impacts->NoteStrike(*this, LDSliceVfx::EStrike::Skill, At, Facing, bOwnBody, CastAbility);
    }
    else if (CastAbility == uint8(ELDDemoAbilityId::ActiveSeethe) && !bOverdriveCast)
    {
        // Release: the flame wall out to the approved radius and the scorched floor; its hits burn.
        const float Radius = Impacts->SkillEffectRadius(CastAbility, uint16(ELDDemoEffectOp::SelfArea));
        const FVector Floor = Impacts->FloorUnder(Pivot->GetComponentTransform().TransformPosition(FVector(0, 0, -HalfHeight)));
        if (Radius > 0.f)
        {
            Impacts->Play(LDSliceVfx::ESystem::SeetheBurst, *this, Floor, Floor, LDSliceVfx::ElementInk(uint8(ELDCombatElement::Fire)), Radius,
                1.8f * Look.Seconds, Look, Late);
        }
        Impacts->NoteStrike(*this, LDSliceVfx::EStrike::Skill, At, Facing, bOwnBody, CastAbility);
    }
    else if (CastAbility == uint8(ELDDemoAbilityId::ActiveZap) && !bOverdriveCast)
    {
        // Release: the spark at the hand; each hit of the chain draws its arc from here or the previous body struck.
        Impacts->Slice(this, LDSliceVfx::ZapRelease(At, Look), Late);
        Impacts->NoteStrike(*this, LDSliceVfx::EStrike::Zap, At, Facing, bOwnBody, CastAbility);
    }
    else
    {
        Impacts->Kit(At, LDHitVfx::BurstOf(Skill->Release, Skill->Tint, EffectScale), this);
        Impacts->NoteStrike(*this, LDSliceVfx::EStrike::Skill, At, Facing, bOwnBody, CastAbility);
    }
}

void ULDHumanoidVisualComponent::UpdateSound()
{
    // Body sounds on the presented timeline (AUDIO_AND_FEEL §2), whatever the effect strength: a whoosh peaking on the
    // swing's release, a charge over a long enough cast windup cut at its release, and the skill's release. Only a body
    // the local player sees sounds, and an onset first seen well after its release stays silent (as its effects do).
    const bool bAudible = CanShowImpact() && Pivot;
    const float Age = ShownAge();
    const FVector At = Pivot ? Pivot->GetComponentLocation() : FVector::ZeroVector;
    if (Action == LDBodyMotion::EAction::Attack && SwingSoundOnset != ActionOnset
        && Age >= ActionImpactSeconds - LDAudioFeel::SwingPreRollSeconds(ActionImpactSeconds))
    {
        SwingSoundOnset = ActionOnset;
        if (bAudible && Age - ActionImpactSeconds < .2f) { LDAudioFeel::Swing(*this, bOwnBody, FMath::Clamp<uint8>(ComboStep, 1, 3), At); }
    }
    if (Action != LDBodyMotion::EAction::Cast)
    {
        // A cast cancelled before its release takes its charge with it.
        if (ChargeSound.IsValid()) { LDAudioFeel::CutCharge(ChargeSound.Get()); }
        ChargeSound = nullptr;
        return;
    }
    if (ChargeSoundOnset != ActionOnset)
    {
        ChargeSoundOnset = ActionOnset;
        if (ChargeSound.IsValid()) { LDAudioFeel::CutCharge(ChargeSound.Get()); }
        ChargeSound = bAudible && ActionImpactSeconds >= LDAudioFeel::ChargeMinSeconds && Age < ActionImpactSeconds - .05f
            ? LDAudioFeel::Charge(*this, bOwnBody, At) : nullptr;
    }
    if (ReleaseSoundOnset != ActionOnset && Age >= ActionImpactSeconds)
    {
        ReleaseSoundOnset = ActionOnset;
        if (ChargeSound.IsValid()) { LDAudioFeel::CutCharge(ChargeSound.Get()); }
        ChargeSound = nullptr;
        if (bAudible && Age - ActionImpactSeconds < .2f) { LDAudioFeel::Release(*this, bOwnBody, CastAbility, bOverdriveCast, At); }
    }
}

void ULDHumanoidVisualComponent::UpdateDash(const FVector& Velocity, bool bFalling, float EffectScale)
{
    const bool bNow = FApp::CanEverRender() && CanShowImpact() && Art && EffectScale > 0.f
        && LDHitVfx::IsDashing(float(Velocity.Size2D()), bKnockback, bFalling);
    if (!bNow)
    {
        if (bDashing && IsValid(DashTrail)) { DashTrail->Deactivate(); }
        bDashing = false;
        return;
    }
    UWorld* World = GetWorld();
    ULDImpactFxSubsystem* Impacts = World ? World->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
    const ULDCombatVfxCatalog* Catalog = Impacts ? Impacts->GetCatalog() : nullptr;
    if (!Catalog || !Catalog->SwingTrail) { return; }
    const LDHitVfx::FSkillLook* Skill = LDHitVfx::SkillLookOf(CastAbility);
    const FLinearColor Tint = Skill ? Skill->Tint : FLinearColor(.48f, .90f, 1.f);
    const LDHitVfx::FTrail Trail = LDHitVfx::DashTrailOf(Tint, EffectScale);
    if (!IsValid(DashTrail))
    {
        DashTrail = NewObject<UNiagaraComponent>(GetOwner(), NAME_None, RF_Transient);
        if (!DashTrail) { return; }
        DashTrail->SetAutoActivate(false);
        DashTrail->SetAsset(Catalog->SwingTrail);
        DashTrail->SetupAttachment(Pivot);
        DashTrail->SetRelativeLocation(FVector(0, 0, .55f * HeightCm - HalfHeight));
        DashTrail->RegisterComponent();
    }
    DashTrail->SetVariableFloat(TEXT("User.Width"), Trail.WidthCm);
    DashTrail->SetVariableFloat(TEXT("User.Lifetime"), Trail.Lifetime);
    DashTrail->SetVariableFloat(TEXT("User.SpawnRate"), Trail.Rate);
    DashTrail->SetVariableLinearColor(TEXT("User.Colour"), Trail.Colour);
    // Afterimages: a posed translucent copy of the body every interval while the dash lasts.
    const LDHitVfx::FGhosts Ghosts = LDHitVfx::GhostsOf(Tint, EffectScale);
    if (Ghosts.bShow && (!bDashing || Clock - LastGhostAt >= Ghosts.IntervalSeconds))
    {
        LastGhostAt = Clock;
        Impacts->Ghost(*Mesh, Ghosts, *this);
    }
    if (!bDashing)
    {
        bDashing = true;
        DashTrail->Activate(true);
        // #335 §3.1: ground shockwave ring + dust burst at the dash launch spot.
        const FVector LaunchGround = Pivot->GetComponentTransform().TransformPosition(FVector(0, 0, -HalfHeight + 6.f));
        const FVector LaunchChest = Pivot->GetComponentTransform().TransformPosition(FVector(0, 0, .48f * HeightCm - HalfHeight));
        Impacts->Layers(this, LaunchChest, LaunchGround, LDHitVfx::ForDashStart(Tint, EffectScale, Impacts->LayerTuning()), 0.f);
        Impacts->Kit(LaunchGround, LDHitVfx::BurstOf(LDHitVfx::EBurst::Dash, Tint, EffectScale), this);
    }
}

void ULDHumanoidVisualComponent::UpdateOverdrive(float EffectScale)
{
    UWorld* World = GetWorld();
    ULDImpactFxSubsystem* Impacts = World ? World->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
    if (!Impacts || !Art || !FApp::CanEverRender()) { return; }
    const bool bShow = bCharging && CanShowImpact() && EffectScale > 0.f;
    const FTransform& Space = Pivot->GetComponentTransform();
    const FVector Feet = Space.TransformPosition(FVector(0, 0, -HalfHeight + 4.f));
    const FVector Chest = Space.TransformPosition(FVector(0, 0, .5f * HeightCm - HalfHeight));
    const float Hold = Impacts->OverdriveHoldSeconds();
    if (bShow)
    {
        if (!bChargeShown)
        {
            bChargeShown = true;
            bChargeReady = false;
            // Start from the charge's real age: a body seen mid-hold shows the charge where it is.
            ChargeOnset = Clock - (FMath::IsFinite(ChargeAge) ? FMath::Max(ChargeAge, 0.f) : 0.f);
        }
        const float Progress = ChargeProgress >= 0.f ? ChargeProgress : Hold > 0.f ? float(Clock - ChargeOnset) / Hold : 1.f;
        const LDHitVfx::FZone Column = LDHitVfx::ChargeZoneOf(Progress, EffectScale);
        UNiagaraSystem* Zone = Impacts->GetZoneSystem();
        if (Zone && !IsValid(ChargeColumn))
        {
            ChargeColumn = NewObject<UNiagaraComponent>(GetOwner(), NAME_None, RF_Transient);
            if (ChargeColumn)
            {
                ChargeColumn->SetAutoActivate(false);
                ChargeColumn->SetAsset(Zone);
                ChargeColumn->SetupAttachment(Pivot);
                ChargeColumn->RegisterComponent();
            }
        }
        if (IsValid(ChargeColumn))
        {
            ChargeColumn->SetWorldLocation(Feet);
            ULDImpactFxSubsystem::ApplyZone(*ChargeColumn, Column, 45.f, Column.Velocity);
            if (!ChargeColumn->IsActive()) { ChargeColumn->Activate(true); }
        }
        if (!bChargeReady && Progress >= 1.f)
        {
            bChargeReady = true;
            Impacts->Layers(this, Chest, Feet, LDHitVfx::ForChargeReady(EffectScale, Impacts->LayerTuning()), 0.f);
        }
        return;
    }
    if (bChargeShown)
    {
        // The charge ended (released, let go or cancelled); the release itself is the cast's (UpdateRelease).
        bChargeShown = false;
        if (IsValid(ChargeColumn)) { ChargeColumn->Deactivate(); }
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
        Part->bIsEditorOnly = IsEditorOnly();
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
        {.05f,.2f,.75f}, {.85f,.35f,.02f}, {.1f,.55f,.12f}, {.45f,.12f,.65f}, {.85f,.75f,.05f}, {.05f,.6f,.65f},
        {.9f,.15f,.5f}, {.8f,.8f,.82f}
    };
    static_assert(UE_ARRAY_COUNT(Palette) == LDArt::SeatCount, "One tint per roster seat.");
    // Validation keeps observed slots inside the eight-seat roster; an out-of-range key draws a neutral grey.
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
    case ELDDemoMonsterId::MonsterPromoter:return FLinearColor(.04f,.30f,.14f);
    case ELDDemoMonsterId::MonsterCourier:return FLinearColor(.55f,.40f,.02f);
    case ELDDemoMonsterId::MonsterAgent:return FLinearColor(.10f,.30f,.45f);
    case ELDDemoMonsterId::MonsterGuard:return FLinearColor(.12f,.14f,.18f);
    case ELDDemoMonsterId::MonsterAuditor:return FLinearColor(.04f,.38f,.34f);
    case ELDDemoMonsterId::MonsterClient:return FLinearColor(.45f,.10f,.28f);
    case ELDDemoMonsterId::MonsterDirector:return FLinearColor(.60f,.27f,.02f);
    case ELDDemoMonsterId::MonsterFinalClient:return FLinearColor(.50f,.04f,.08f);
    case ELDDemoMonsterId::MonsterProperty:return FLinearColor(.30f,.34f,.04f);
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

#if WITH_EDITOR
void ULDHumanoidVisualComponent::ApplyEditorPreview(const FLDArtCharacter& Look, const FLinearColor& Identity)
{
    ApplyLook(&Look, Identity, TEXT("Layout preview"));
    for (UStaticMeshComponent* Part : Parts) { Part->SetVisibility(false); Part->bIsEditorOnly = true; }
    for (UStaticMeshComponent* Part : Edges) { Part->SetVisibility(false); Part->bIsEditorOnly = true; }
    Pivot->bIsEditorOnly = true;
    Mesh->bIsEditorOnly = true;
    Mesh->SetVisibility(true);
    Mesh->SetHiddenInGame(true);
    if (const auto* Idle = Look.Find(ELDArtAnim::Idle))
    {
        Mesh->PlayAnimation(Idle->Sequence, true);
        Mesh->SetPosition(0.f);
        Mesh->TickAnimation(0.f, false);
        Mesh->RefreshBoneTransforms();
    }
    for (UMeshComponent* Part : Worn)
    {
        Part->bIsEditorOnly = true;
        Part->SetVisibility(true);
        Part->SetHiddenInGame(true);
    }
}
#endif

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
    Basketball->SetHiddenInGame(true);
    Basketball->SetStaticMesh(nullptr);
    if (!Art)
    {
        ClearImpacts();
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
    if (Art->BasketballMotion)
    {
        Basketball->SetStaticMesh(Art->BasketballMotion->BallMesh);
        Basketball->SetMaterial(0,Art->BasketballMotion->BallMaterial);
        // The basic sphere's bounding box, unlike SphereRadius, is its physical radius.
        const float Radius=Art->BasketballMotion->BallMesh->GetBoundingBox().GetExtent().GetMax();
        Basketball->SetWorldScale3D(FVector(Art->BasketballMotion->BallRadiusCm/FMath::Max(Radius,1.f)));
        Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        Mesh->SetAnimInstanceClass(ULDBasketballAnimInstance::StaticClass());
        auto* Animation=Cast<ULDBasketballAnimInstance>(Mesh->GetAnimInstance());
        if (ensureMsgf(Animation,TEXT("The declared basketball body needs its runtime animation graph.")))
        {
            Animation->Bind(Art->BasketballMotion,Basketball,
                Art->MeshTransform.GetRotation().UnrotateVector(FVector::ForwardVector));
            UE_LOG(LogLDContent,Display,TEXT("LD_BASKETBALL_PRESENTATION body=%s owner_class=%s graph=%s source=%s"),
                *Art->Id,*GetOwner()->GetClass()->GetName(),*Animation->GetClass()->GetPathName(),*Art->BasketballMotion->SourceSha256);
        }
    }
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
    Mesh->Activate();
    Mesh->SetHiddenInGame(false);
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
    if (!ensureMsgf(Material, TEXT("Character material missing; rerun asset bootstrap"))) { return; }
    // Marker colours are fixed and identical on every body, so their instances are shared.
    for (int32 Index = 0; Index < Parts.Num(); ++Index)
    {
        UMaterialInstanceDynamic* Instance = ULDArtSubsystem::SharedTint(this, Material, ColourOf(Index));
        Parts[Index]->SetMaterial(0, Instance);
    }
    UMaterialInstanceDynamic* Dark = ULDArtSubsystem::SharedTint(this, Material, EdgeColour);
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

void ULDHumanoidVisualComponent::SetCue(uint8 Value, float ActionAgeSeconds, float ImpactDelaySeconds, bool bInKnockback,
    bool bAdoptOnset)
{
    using LDBodyMotion::EAction;
    const uint8 Code = Value & ActionMask;
    const EAction Next = Code == 1 ? EAction::Attack : Code == 2 ? EAction::Cast : EAction::None;
    const bool bAgeKnown = ActionAgeSeconds >= 0.f && FMath::IsFinite(ActionAgeSeconds);
    const double Onset = bAgeKnown ? Clock - ActionAgeSeconds : Clock;
    if ((bAdoptOnset && bAgeKnown && Next != EAction::None) || LDBodyMotion::IsNewInstance(Action, ActionOnset, Next, Onset, bAgeKnown))
    { ActionOnset = Onset; }
    ActionImpactSeconds = FMath::IsFinite(ImpactDelaySeconds) && ImpactDelaySeconds >= 0.f
        ? ImpactDelaySeconds : LDBodyMotion::StrikeAt;
    Action = Next;
    if (Next != EAction::None) { RetractAt = -1; }
    if (bInKnockback && !bKnockback) { KnockbackAt = Clock; }
    bKnockback = bInKnockback;
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

void ULDHumanoidVisualComponent::QueueImpact(double Onset, ELDObservedHitLook InHitLook, uint8 InHitFlags)
{
    if (PendingImpacts.Num() >= LDImpactFx::MaxBursts) { PendingImpacts.RemoveAt(0); }
    PendingImpacts.Add({Onset, InHitLook, InHitFlags});
}

bool ULDHumanoidVisualComponent::SetHitReaction(float AgeSeconds, ELDObservedHitLook InHitLook, uint8 InHitFlags,
    const FGuid& HitId, int64 HitAtMs)
{
    if (!FMath::IsFinite(AgeSeconds) || AgeSeconds<0.f || AgeSeconds>=.3f || FallAt>=0) { return false; }
    const double Onset=Clock-AgeSeconds;
    if (HitId.IsValid())
    {
        if (SeenHitIds.Contains(HitId)) { return false; }
        SeenHitIds.Add(HitId);
        if (SeenHitIds.Num() > 128) { SeenHitIds.RemoveAt(0); }
        if (HitAtMs > 0 && LatestHitAtMs > 1 && HitAtMs < LatestHitAtMs - 1) { return false; }
        if (HitAtMs <= 0 && bHitSeen && Onset + LDBodyMotion::SameInstanceSeconds < HitOnset) { return false; }
        if (HitAtMs > 0) { LatestHitAtMs = FMath::Max(LatestHitAtMs, HitAtMs); }
        HitLook=InHitLook; HitOnset=Onset; HitFlags=InHitFlags; bHitSeen=true;
        QueueImpact(Onset, InHitLook, InHitFlags);
        return true;
    }
    HitLook = InHitLook;
    // Different resolved outcomes are distinct hits even inside the clock-jitter tolerance.
    if (!bHitSeen || InHitFlags!=HitFlags || Onset>HitOnset+LDBodyMotion::SameInstanceSeconds)
    { HitOnset=Onset; HitFlags=InHitFlags; bHitSeen=true; QueueImpact(Onset, InHitLook, InHitFlags); return true; }
    return false;
}

void ULDHumanoidVisualComponent::SetReaction(uint8 Reaction, uint8 Element, float AgeSeconds, double AtServerSec)
{
    if (Reaction == 0 || !FMath::IsFinite(AgeSeconds) || AgeSeconds < 0.f || AgeSeconds >= .4f || FallAt >= 0
        || !FMath::IsFinite(AtServerSec)) { return; }
    // Identity is the authority commit time (millisecond resolution): two reactions 50 ms apart are two events.
    if (ShownReaction == Reaction && FMath::Abs(AtServerSec - ReactionAtServerSec) < .002) { return; }
    ShownReaction = Reaction;
    ReactionAtServerSec = AtServerSec;
    ReactionOnset = Clock - AgeSeconds;
    PendingReactionElement = Element;
    bPendingReaction = true;
}

void ULDHumanoidVisualComponent::SetConfirm(float AgeSeconds, float StopSeconds, float SparkScale)
{
    if (!FMath::IsFinite(AgeSeconds) || AgeSeconds < 0.f || FallAt >= 0) { return; }
    ConfirmOnset = Clock - AgeSeconds;
    ConfirmStopSeconds = FMath::IsFinite(StopSeconds) ? FMath::Max(StopSeconds, 0.f) : 0.f;
    ConfirmSparkScale = FMath::IsFinite(SparkScale) ? FMath::Max(SparkScale, 0.f) : 0.f;
}

float ULDHumanoidVisualComponent::PoseAge() const
{
    const float Age = HoldSeconds >= 0.f ? FMath::Min(float(Clock - ActionOnset), HoldSeconds) : float(Clock - ActionOnset);
    const bool bStriking = Action == LDBodyMotion::EAction::Attack || Action == LDBodyMotion::EAction::Cast;
    return bStriking && ConfirmOnset >= 0 ? LDBodyMotion::HitStopPoseAge(Age, float(Clock - ConfirmOnset), ConfirmStopSeconds) : Age;
}

void ULDHumanoidVisualComponent::SetHold(float InHoldSeconds)
{
    HoldSeconds = FMath::IsFinite(InHoldSeconds) && InHoldSeconds >= 0.f ? InHoldSeconds : -1.f;
}

void ULDHumanoidVisualComponent::Retract()
{
    if (Action != LDBodyMotion::EAction::Attack || FallAt >= 0) { return; }
    RetractFrom = PoseAge();
    RetractImpact = ActionImpactSeconds;
    RetractAt = Clock;
}

LDBodyMotion::EAction ULDHumanoidVisualComponent::ShownAction() const
{
    if (Action != LDBodyMotion::EAction::None || RetractAt < 0) { return Action; }
    return LDBodyMotion::RetractAge(RetractFrom, float(Clock - RetractAt)) >= 0.f ? LDBodyMotion::EAction::Attack : LDBodyMotion::EAction::None;
}

float ULDHumanoidVisualComponent::ShownAge() const
{
    return Action == LDBodyMotion::EAction::None && RetractAt >= 0 ? LDBodyMotion::RetractAge(RetractFrom, float(Clock - RetractAt)) : PoseAge();
}

bool ULDHumanoidVisualComponent::ShowSkillHit(const FPendingImpact& Impact, const FVector& Chest, float EffectScale)
{
    ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
    if (!Impacts || EffectScale <= 0.f) { return false; }
    const float Age = float(Clock - Impact.Onset);
    const ULDImpactFxSubsystem::FStrikeMatch Match = Impacts->MatchHit(*this, Chest, Impact.Look, Age);
    HitAbility = Match.Ability;
    if (Match.Ability != uint8(ELDDemoAbilityId::ActiveColaShake)) { return false; }
    if (Match.bOwn) { OwnStrikeAt = Clock; }
    const LDSliceVfx::FLook Look = Impacts->SliceLook(ULDImpactFxSubsystem::ESliceTuning::Impact, Match.bOwn, bOwnBody || Match.bOwn);
    Impacts->Play(LDSliceVfx::ESystem::ChillHit, *this, Match.From.ContainsNaN() ? Chest - Match.Dir * 100.f : Match.From, Chest,
        LDSliceVfx::ElementInk(uint8(ELDCombatElement::Chill)), 70.f * Look.Size, 1.2f * Look.Seconds, Look, Age);
    return true;
}

bool ULDHumanoidVisualComponent::ShowSliceImpact(const FPendingImpact& Impact, const FVector& Chest, const FVector& Feet, float EffectScale)
{
    ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
    if (!Impacts || EffectScale <= 0.f) { return false; }
    const float Age = float(Clock - Impact.Onset);
    // The visible strike this hit belongs to gives its tier, direction and owner; with none in sight the hit shows the
    // tier its look implies, struck from the front.
    const ULDImpactFxSubsystem::FStrikeMatch Match = Impacts->MatchHit(*this, Chest, Impact.Look, Age);
    HitAbility = Match.Ability;
    const LDSliceVfx::EStrike Strike = Match.Strike != LDSliceVfx::EStrike::None ? Match.Strike
        : Impact.Look == ELDObservedHitLook::Water ? LDSliceVfx::EStrike::Hose
        : Impact.Look == ELDObservedHitLook::Electric ? LDSliceVfx::EStrike::Zap : LDSliceVfx::EStrike::Basic;
    const FVector Dir = !Match.Dir.IsNearlyZero() ? Match.Dir : (GetOwner() ? -GetOwner()->GetActorForwardVector() : FVector::ForwardVector);
    if (Match.bOwn) { OwnStrikeAt = Clock; }
    const LDSliceVfx::FLook Look = Impacts->SliceLook(ULDImpactFxSubsystem::ESliceTuning::Impact, Match.bOwn, bOwnBody || Match.bOwn);
    // A fire hit (a fire skill, a burn tick, a fire floor) is the Niagara FireHit whatever struck it.
    if (Impact.Look == ELDObservedHitLook::Fire)
    {
        Impacts->Play(LDSliceVfx::ESystem::FireHit, *this, Chest - Dir * 100.f, Chest, LDSliceVfx::ElementInk(uint8(ELDCombatElement::Fire)),
            70.f * Look.Size, .9f * Look.Seconds, Look, Age);
        return false;
    }
    // A plain hit of a physical skill (its look is None) shows the heavier Niagara Hit, not the old skill burst; the return
    // value still names only a combo finisher.
    const LDSliceVfx::EStrike Visual = Strike == LDSliceVfx::EStrike::Skill && Impact.Look == ELDObservedHitLook::None ? LDSliceVfx::EStrike::Finisher : Strike;
    Impacts->Slice(this, LDSliceVfx::Impact(Visual, Chest, Feet, Dir, Match.From, Match.bChained, Look), Age);
    // A basic or finisher hit: the Niagara Hit along the strike, and in the local player's own fight a brief rim on the body.
    if (Visual == LDSliceVfx::EStrike::Basic || Visual == LDSliceVfx::EStrike::Finisher)
    {
        const bool bFinisher = Visual == LDSliceVfx::EStrike::Finisher;
        Impacts->Hit(*this, Chest - Dir * 100.f, Chest, bFinisher, Look, Age);
        if (Look.bFocus && Mesh && Age < .1f && Look.EffectScale > 0.f)
        {
            LDHitVfx::FGhosts Rim;
            Rim.bShow = true;
            Rim.bShell = true;
            Rim.Seconds = bFinisher ? .18f : .12f;
            Rim.Glow = LDSliceVfx::StrikeTint(Look) * (bFinisher ? 1.5f : 1.f);
            Impacts->Ghost(*Mesh, Rim, *this);
        }
    }
    // A water hit: the splash at the chest away from the stream and the soaked floor under the body.
    if (Strike == LDSliceVfx::EStrike::Hose)
    {
        Impacts->Play(LDSliceVfx::ESystem::WaterHit, *this, Match.From.ContainsNaN() ? Chest - Dir * 100.f : Match.From, Chest,
            LDSliceVfx::ElementInk(uint8(ELDCombatElement::Water)), 70.f * Look.Size, 1.6f * Look.Seconds, Look, Age);
    }
    // A zap hop: the Niagara Chain from the matched hand or previous body to this one; with no visible source only its
    // contact sparks show (a short link straight down onto the body).
    if (Strike == LDSliceVfx::EStrike::Zap)
    {
        Impacts->Chain(*this, Match.From.ContainsNaN() ? Chest + FVector(0, 0, 60.f) : Match.From, Chest, Match.bChained, Look, Age);
        // The struck body lights a warm rim shell, only in the local player's own fights (others' targets get none); a
        // bounce target (smaller link) a little less.
        if (Look.bFocus && Mesh && Age < .15f && Look.EffectScale > 0.f)
        {
            LDHitVfx::FGhosts Warm;
            Warm.bShow = true;
            Warm.bShell = true;
            Warm.Seconds = .22f;
            Warm.Glow = FLinearColor(1.f, .74f, .36f) * (Match.bChained ? 1.3f : 1.6f);
            Impacts->Ghost(*Mesh, Warm, *this);
        }
    }
    return Strike == LDSliceVfx::EStrike::Finisher;
}

void ULDHumanoidVisualComponent::PlayFall()
{
    ClearImpacts();
    // A visible fall cracks the ground under it; the crack plays out on its own while the body sinks.
    UWorld* World = GetWorld();
    if (auto* Impacts = World && FallAt < 0 ? World->GetSubsystem<ULDImpactFxSubsystem>() : nullptr)
    {
        const APlayerController* Viewer = GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
        const ALDPlayerController* LocalController = Cast<ALDPlayerController>(Viewer);
        const float EffectScale = LocalController && LocalController->Controls && LocalController->Controls->bReduceCombatEffects ? .45f : 1.f;
        const FTransform& Space = GetComponentTransform();
        Impacts->Layers(nullptr, Space.TransformPosition(FVector(0, 0, .5f * HeightCm - HalfHeight)),
            Space.TransformPosition(FVector(0, 0, -HalfHeight)), LDHitVfx::ForKill(EffectScale, Impacts->LayerTuning()), 0.f);
        Impacts->Kit(Space.TransformPosition(FVector(0, 0, .3f * HeightCm - HalfHeight)),
            LDHitVfx::BurstOf(LDHitVfx::EBurst::Kill, FLinearColor(1.f, .25f, .08f), EffectScale));
    }
    FallAt = Clock;
    Action = LDBodyMotion::EAction::None;
    RetractAt = -1;
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
    const EAction Showing = ShownAction();
    const float ImpactSeconds = Action == EAction::None ? RetractImpact : ActionImpactSeconds;
    const FLDArtAnimation* Acting = Showing == EAction::None ? nullptr : Art->Find(ClipOf(Showing));
    const FLDArtAnimation* Knocked = bKnockback ? Art->Find(ELDArtAnim::Hit) : nullptr;
    const float ActionAge = ShownAge();
    const FLDArtAnimation* Waving = Art->Find(ELDArtAnim::Spawn);
    const bool bPosed = bFalling
        ? Dying && Pose(ELDArtAnim::Death, float(Clock - FallAt) * Dying->PlayRate, true)
        : (Knocked && Pose(ELDArtAnim::Hit, float(Clock - KnockbackAt) * Knocked->PlayRate, true))
            || (Acting && Pose(ClipOf(Showing), LDBodyMotion::ClipTime(Showing, ActionAge, Acting->ImpactSec, ImpactSeconds, Acting->PlayRate), false))
            || (Waving && AppearAt >= 0 && Speed < RunFromCmPerSec && Pose(ELDArtAnim::Spawn, float(Clock - AppearAt) * Waving->PlayRate, false));
    const FLDArtAnimation* Entry = Art->Find(State);
    if (!Entry || !Entry->Sequence) { return; }
    if (Art->BasketballMotion)
    {
        if (auto* Animation=Cast<ULDBasketballAnimInstance>(Mesh->GetAnimInstance()))
        {
            const bool bOverride=bPosed&&State!=ELDArtAnim::Attack&&State!=ELDArtAnim::Cast;
            Animation->Prepare(Clock,Speed,GetOwner()?GetOwner()->GetActorRotation().Yaw:0.f,Showing,ActionAge,
                ImpactSeconds,ActionOnset,bOverride?Entry->Sequence.Get():nullptr,Time,
                Showing==EAction::Cast&&CastAbility==0&&LDHitVfx::IsDashing(Speed,bKnockback,bFalling),CanShowImpact());
        }
        return;
    }
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
        : LDBodyMotion::WithHit(ShownAction(), ShownAge(), bHitSeen ? float(Clock-HitOnset) : -1.f,
            Action == LDBodyMotion::EAction::None ? RetractImpact : ActionImpactSeconds);
    // The own body's hit confirmation flares the striking hand over the action's own release flash.
    const float Confirm = !bFalling && ConfirmOnset >= 0 ? LDBodyMotion::ConfirmSpark(float(Clock - ConfirmOnset)) * ConfirmSparkScale : 0.f;
    const float Ease = 1.f - FMath::Exp(-40.f * FMath::Max(Delta, 0.f));
    Shown.Lunge = FMath::Lerp(Shown.Lunge, Target.Lunge, Ease);
    Shown.Charge = FMath::Lerp(Shown.Charge, Target.Charge, Ease);
    const APlayerController* Viewer = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
    const ALDPlayerController* LocalController = Cast<ALDPlayerController>(Viewer);
    const float EffectScale = LocalController && LocalController->Controls && LocalController->Controls->bReduceCombatEffects ? .45f : 1.f;
    Shown.Burst = FMath::Max(Target.Burst, Confirm) * EffectScale;
    Shown.Flash = Target.Flash * EffectScale;

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
        // #335 §3.2: lock toward attack/cast windup facing at 1080 deg/s so rapid turn-attacks never swing sideways.
        const float TurnDegPerSec = ShownAction() != LDBodyMotion::EAction::None ? 1080.f : 540.f;
        VisualYaw = FMath::FixedTurn(VisualYaw, TargetYaw, TurnDegPerSec * Delta);
        const FVector LocalAcceleration = ActorRotation.UnrotateVector((Velocity - PreviousVelocity) / Delta);
        const FQuat VisualFacing = ActorRotation.Quaternion() * Art->MeshTransform.GetRotation()
            * FRotator(0, FRotator::NormalizeAxis(VisualYaw - TargetYaw), 0).Quaternion();
        const FRotator PushLean = bKnockback && !bFalling
            ? LDBodyMotion::KnockbackLean(VisualFacing.Inverse().RotateVector(Velocity)) : FRotator::ZeroRotator;
        LeanPitch = FMath::FInterpTo(LeanPitch, FMath::Clamp(LocalAcceleration.X / 8000.f, -1.f, 1.f) * 8.f + PushLean.Pitch, Delta, 7.f);
        LeanRoll = FMath::FInterpTo(LeanRoll, FMath::Clamp(LocalAcceleration.Y / 8000.f, -1.f, 1.f) * 8.f + PushLean.Roll, Delta, 7.f);
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
    UpdateSlice(EffectScale);
    UpdateStates(bFalling, EffectScale);
    UpdateCastCharge(Shown.Charge, EffectScale);
    UpdateRelease(EffectScale);
    UpdateDash(Velocity, bFalling, EffectScale);
    UpdateOverdrive(EffectScale);
    UpdateSound();

    // The release spark follows the striking or casting hand.
    const FTransform& Space = Pivot->GetComponentTransform();
    const FVector Hand = Space.InverseTransformPosition(Mesh->GetSocketLocation(Art->HandBone));
    const bool bBurst = Shown.Burst > .02f;
    Parts[Burst]->SetHiddenInGame(!bBurst);
    if (bBurst)
    {
        Parts[Burst]->SetRelativeLocation(Hand + FVector(8.f + 22.f * Confirm, 0, 0));
        Parts[Burst]->SetRelativeScale3D(FVector(.16f * Shown.Burst));
    }
    if (!PendingImpacts.IsEmpty())
    {
        ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
        if (CanShowImpact() && Impacts)
        {
            const FVector At = Space.TransformPosition(FVector(18.f, 0, -HalfHeight + .72f * HeightCm));
            const FVector Ground = Space.TransformPosition(Feet);
            for (const FPendingImpact& Impact : PendingImpacts)
            {
                const float Age = float(Clock - Impact.Onset);
                bool bSlice = LDSliceVfx::ShowsHit(Impact.Look, Impact.Flags);
                bool bFinisherHit = false;
                HitAbility = 0;
                // A plain magic hit of a skill with its own designed hit (a chill cone's icy hit) shows that instead of the
                // old magic layers; other magic hits keep them until their skills are designed.
                if (!bSlice && Impact.Look == ELDObservedHitLook::Magic && (Impact.Flags & LDObservation::HitFlagsMask) == 0
                    && ShowSkillHit(Impact, At, EffectScale)) { bSlice = true; }
                else if (bSlice) { bFinisherHit = ShowSliceImpact(Impact, At, Ground, EffectScale); }
                else
                {
                    Impacts->Burst(*this, At, Impact.Look, Impact.Flags, Age, EffectScale);
                    Impacts->Layers(this, At, Ground, LDHitVfx::ForHit(Impact.Look, Impact.Flags, EffectScale, Impacts->LayerTuning()), Age);
                }
                LDHitVfx::EBurst Element;
                FLinearColor Tint;
                // The extra hit particles follow the same impact controls as the Niagara burst and the layers.
                const LDHitFeel::FTuning Feel = LDHitFeel::Read();
                const bool bImpactOn = Feel.ImpactScale > 0.f && Feel.ImpactSeconds > 0.f;
                if (bImpactOn && Age < .3f && (Impact.Flags & LDObservation::Dodged))
                {
                    const LDHitVfx::FGhosts GoldGhost = LDHitVfx::GhostsOf(FLinearColor(1.f, .86f, .2f), EffectScale);
                    if (GoldGhost.bShow) { Impacts->Ghost(*Mesh, GoldGhost, *this); }
                    Impacts->Kit(At, LDHitVfx::BurstOf(LDHitVfx::EBurst::Dash, FLinearColor(1.f, .86f, .2f), EffectScale), this);
                }
                else if (bImpactOn && Age < .3f && (Impact.Flags & LDObservation::Parried))
                {
                    Impacts->Kit(At, LDHitVfx::BurstOf(LDHitVfx::EBurst::Aura, FLinearColor(.55f, .95f, 1.f), EffectScale), this);
                }
                if (!bSlice && bImpactOn && Age < .3f && LDHitVfx::HitBurstOf(Impact.Look, Element, Tint))
                { Impacts->Kit(At, LDHitVfx::BurstOf(Element, Tint, EffectScale), this); }
                if (bImpactOn && Age < .3f && (Impact.Flags & LDObservation::Overdriven))
                { Impacts->Kit(At, LDHitVfx::BurstOf(LDHitVfx::EBurst::Overdrive, LDHitVfx::OverdriveTint(), EffectScale), this); }
                // The struck body's impact sound at the hit's own time; the own body's hurt sounds from the owner tracker.
                if (Age < .25f && !bOwnBody)
                { LDAudioFeel::Impact(*this, Impact.Look, Impact.Flags, bFinisherHit, HitAbility, At); }
            }
        }
        PendingImpacts.Reset();
    }
    if (ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr)
    {
        if (!bGhostWarmed) { bGhostWarmed = true; Impacts->WarmGhost(*Mesh); }
        if (bPendingReaction && CanShowImpact())
        {
            // The reaction sounds at its own time whatever the effect strength.
            LDAudioFeel::Reaction(*this, bOwnBody, ShownReaction, Space.TransformPosition(FVector(0, 0, -HalfHeight + .6f * HeightCm)));
            const LDHitFeel::FTuning Feel = LDHitFeel::Read();
            if (Feel.ImpactScale > 0.f && Feel.ImpactSeconds > 0.f)
            {
                const FVector Chest = Space.TransformPosition(FVector(0, 0, -HalfHeight + .6f * HeightCm));
                if (ShownReaction == uint8(ELDCombatReaction::Conduct))
                {
                    // Conduct (wet + shock): the electric field flashes out to the splash radius round the body (its hit is
                    // the zap's). It is the local player's fight when it happens to the own body or right after the own
                    // strike landed here.
                    const bool bMine = OwnStrikeAt >= 0 && Clock - OwnStrikeAt < .6;
                    Impacts->FieldPulse(*this, Space.TransformPosition(Feet), Impacts->ConductRadiusCm(),
                        Impacts->SliceLook(ULDImpactFxSubsystem::ESliceTuning::Impact, bMine, bOwnBody || bMine), float(Clock - ReactionOnset));
                }
                else
                {
                    Impacts->Layers(this, Chest, Space.TransformPosition(Feet),
                        LDHitVfx::ForReaction(PendingReactionElement, EffectScale, Impacts->LayerTuning()), float(Clock - ReactionOnset));
                    Impacts->Kit(Chest, LDHitVfx::BurstOf(LDHitVfx::ElementBurst(PendingReactionElement),
                        LDHitVfx::ElementTint(PendingReactionElement), EffectScale), this);
                }
            }
        }
        bPendingReaction = false;
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
            FVector(56.f * Girth * FMath::Cos(Angle), 56.f * Girth * FMath::Sin(Angle),
                Foot + .5f * (HeightCm + 14.f)));
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
            ? FMath::Lerp(HitColour(HitLook), FlashWhite, FMath::Clamp((Shown.Flash - .6f) / .4f, 0.f, 1.f)) : AppearGlow;
        SetFlash(Over * Amount);
        bFlashing = true;
    }
    else if (bFlashing)
    {
        SetFlash(FLinearColor::Black);
        bFlashing = false;
    }
}
