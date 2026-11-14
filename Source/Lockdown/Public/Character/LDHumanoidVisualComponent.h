#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Art/LDArtTypes.h"
#include "Character/LDBodyMotion.h"
#include "Character/LDModelPicking.h"
#include "Vision/LDViewTypes.h"
#include "LDHumanoidVisualComponent.generated.h"

class UMaterialInstanceDynamic;
class UMeshComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FLDArtAnimation;
struct FLDArtCharacter;
// Body shared by the owner's own body, every observed proxy and fall puppets: the art catalog's skeletal character
// (ULDArtSubsystem) with single-node clips, status markers and effect shapes around it. It only draws what it is told:
// an identity look, one cue byte (ALDCharacter::PresentationCue) with the age of its action, and the appear and fall
// events; never gameplay state of its own. Every motion hangs from an inner pivot, so the owner keeps the component's
// own transform and scale.
UCLASS()
class LOCKDOWN_API ULDHumanoidVisualComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    // Cue byte: the low nibble is the action (1 attack, 2 cast, 4 hit); each status bit adds its own marker shape, so
    // no status is told apart by colour alone (APP_SPEC 8).
    enum ECueBits : uint8 { ActionMask = 15, Rooted = 16, Returning = 32, Shielded = 64, Protected = 128 };
    ULDHumanoidVisualComponent();
    virtual void OnRegister() override;
    // One identity tint per locked roster slot 0..5, shared by bodies, portraits and the minimap.
    static FLinearColor ContenderTint(uint8 Slot);
    // Camp NPC clothing; never one of the contender tints.
    static FLinearColor MonsterTint(uint8 CreatureId);
    // Contender look: the catalog's seat body in the roster-slot tint.
    void ApplyPalette(uint8 Slot);
    // Camp NPC look (APP_SPEC 1.1): the catalog's body of the camp tier (discipline monitor, PE teacher) in its tint.
    void ApplyMonsterLook(uint8 CreatureId);
    // Source's look, for a puppet that replaces it.
    void CopyLook(const ULDHumanoidVisualComponent& Source);
    void Animate(const FVector& Velocity, float Delta);
    bool TracePresentedModel(const FVector& Start, const FVector& End, double& OutFraction) const;
    // Upright lenient pick volume of the shown model: its displayed feet to the top of its head, as wide as the root
    // ring footprint. False whenever TracePresentedModel could not select the model (hidden, fallen, no body).
    bool PresentedPickVolume(FVector& OutFoot, FVector& OutTop, double& OutRadiusCm) const;
    // The cue byte and the seconds its action has already run on the presentation clock; a negative age is unknown and
    // counts the action from now. The same action kind reported with a later start restarts it (a repeated hit).
    // The first cue showing respawn protection also plays the appear effect.
    void SetCue(uint8 Value, float ActionAgeSeconds = -1.f, float ImpactDelaySeconds = -1.f,
        ELDObservedHitLook HitLook = ELDObservedHitLook::None, bool bKnockback = false);
    void SetHitReaction(float AgeSeconds, ELDObservedHitLook HitLook = ELDObservedHitLook::None, uint8 HitFlags = 0);
    void PlayAppear();
    // Plays the death clip, then sinks and shrinks away; markers and actions stop. IsFallen once it has vanished.
    void PlayFall();
    bool IsFallen() const;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FLDPresentedModelPickingTest;
    friend class FLDBodyMotionTimelinesTest;
#endif
    // Resolves the catalog body and paints its tint slots; without a body nothing is drawn and the error is logged.
    void ApplyLook(const FLDArtCharacter* Look, const FLinearColor& Identity, const FString& What);
    // Builds the look's worn parts on the body mesh and adds their paints; drops the previous look's parts.
    void WearParts(const FLinearColor& Identity, const FString& What);
    // Places the markers and effects for the body's displayed height.
    void Layout();
    // Shows the clip of the current state: dying, an action, the respawn, running or standing.
    void PlayClip(bool bFalling, float Speed);
    // Places each shown marker's near-black twin behind it on the local view ray.
    void PlaceEdges();
    // Seconds the death clip takes to lie down; zero without one.
    float LieSeconds() const;
    void SetFlash(const FLinearColor& Emissive);
    UPROPERTY() TObjectPtr<USceneComponent> Pivot;
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> Mesh;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Edges;
    // The catalog body's worn parts (FLDArtAttachment), rebuilt with every look; they hang from Mesh.
    UPROPERTY() TArray<TObjectPtr<UMeshComponent>> Worn;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Paints;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HitSparkTint;
    // The resolved catalog entry; valid while the game instance's art catalog lives.
    const FLDArtCharacter* Art = nullptr;
    // Eased toward the action timeline, so an ending cue settles instead of snapping to rest.
    LDBodyMotion::FChannels Shown;
    LDBodyMotion::EAction Action = LDBodyMotion::EAction::None;
    ELDArtAnim Clip = ELDArtAnim::Count;
    double Clock = 0, ActionOnset = 0, AppearAt = -1, FallAt = -1, HitOnset = -1, KnockbackAt = -1;
    float HalfHeight = 0, HeightCm = 0, ActionImpactSeconds = LDBodyMotion::StrikeAt;
    float VisualYaw = 0, LeanPitch = 0, LeanRoll = 0;
    FVector PreviousVelocity = FVector::ZeroVector;
    uint8 Cue = 0;
    ELDObservedHitLook HitLook = ELDObservedHitLook::None;
    uint8 HitFlags = 0;
    uint8 LookSlot = MAX_uint8, LookCreature = 0;
    bool bCueSeen = false, bFlashing = false, bHitSeen = false, bVisualPoseInitialized = false, bKnockback = false;
    // The shown mesh's shared picking model and this body's pose of it, posed at most once per frame.
    mutable TSharedPtr<const LDModelPicking::FSkinnedModel> PickingModel;
    mutable LDModelPicking::FSkinnedPose PickingPose;
    mutable uint64 PickingFrame = MAX_uint64;
};
