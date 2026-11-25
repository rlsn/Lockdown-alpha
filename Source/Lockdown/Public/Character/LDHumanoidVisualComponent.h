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
class UNiagaraComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class ULDBasketballAnimInstance;
class UAnimSequence;
struct FLDArtAnimation;
struct FLDArtCharacter;
// Body shared by the owner's own body, every observed proxy and fall puppets: the art catalog's skeletal character
// (ULDArtSubsystem) with authored clips/basketball blending, status markers and effect shapes around it. It only draws what it is told:
// an identity look, one cue byte (ALDCharacter::PresentationCue) with the age of its action, and the appear and fall
// events; never gameplay state of its own. Every motion hangs from an inner pivot, so the owner keeps the component's
// own transform and scale.
UCLASS()
class LOCKDOWN_API ULDHumanoidVisualComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    // Cue byte: the low nibble is the action (1 attack, 2 cast); each status bit adds its own marker shape, so
    // no status is told apart by colour alone (APP_SPEC 8).
    enum ECueBits : uint8 { ActionMask = 15, Rooted = 16, Returning = 32, Shielded = 64, Protected = 128 };
    ULDHumanoidVisualComponent();
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void OnVisibilityChanged() override;
    virtual void OnHiddenInGameChanged() override;
    // One identity tint per locked roster slot 0..7, shared by bodies, portraits and the minimap.
    static FLinearColor ContenderTint(uint8 Slot);
    // Camp NPC clothing; never one of the contender tints.
    static FLinearColor MonsterTint(uint8 CreatureId);
    // Contender look: the catalog's seat body in the roster-slot tint.
    void ApplyPalette(uint8 Slot);
    // Camp NPC look (APP_SPEC 1.1): the catalog's body of the camp tier (discipline monitor, PE teacher) in its tint.
    void ApplyMonsterLook(uint8 CreatureId);
#if WITH_EDITOR
    void ApplyEditorPreview(const FLDArtCharacter& Look, const FLinearColor& Identity);
#endif
    // Source's look, for a puppet that replaces it.
    void CopyLook(const ULDHumanoidVisualComponent& Source);
    // Presentation stills (lobby seat portraits, DEV captures): hold this body's basketball pose at one clip and time,
    // the ball following the hand; a null clip returns to live motion. False when the body has no basketball graph.
    bool HoldPose(UAnimSequence* Still, float TimeSeconds);
    void Animate(const FVector& Velocity, float Delta);
    bool TracePresentedModel(const FVector& Start, const FVector& End, double& OutFraction) const;
    // Upright lenient pick volume of the shown model: its displayed feet to the top of its head, as wide as the root
    // ring footprint. False whenever TracePresentedModel could not select the model (hidden, fallen, no body).
    bool PresentedPickVolume(FVector& OutFoot, FVector& OutTop, double& OutRadiusCm) const;
    // The cue byte and the seconds its action has already run on the presentation clock; a negative age is unknown and
    // counts the action from now. The same action kind reported with a later start restarts it (a new swing).
    // The first cue showing respawn protection also plays the appear effect. bAdoptOnset takes a known age as the action's
    // start even within the jitter window (a confirmed prediction hands over to the authority's timeline).
    void SetCue(uint8 Value, float ActionAgeSeconds = -1.f, float ImpactDelaySeconds = -1.f, bool bKnockback = false,
        bool bAdoptOnset = false);
    // The body's public element states (burn, wet, electrified, chilled, rotten; ELDDemoStatusId) and their stacks: each
    // shows its own looping effect on the body while it lasts (LDSliceVfx::ESystem BurnState..RotState).
    void SetElementStates(const TArray<FLDObservedStatus>& Statuses);
    // The victim's overlay only (flash and impact particles); the body's pose and position never react to a hit.
    bool SetHitReaction(float AgeSeconds, ELDObservedHitLook HitLook = ELDObservedHitLook::None, uint8 HitFlags = 0,
        const FGuid& HitId = FGuid(), int64 HitAtMs = 0);
    // The own body's hit confirmation AgeSeconds ago: the striking pose holds StopSeconds and catches up, and the hand
    // flares at SparkScale. Presentation only; input, movement and authority time never pause.
    void SetConfirm(float AgeSeconds, float StopSeconds, float SparkScale);
    // The own predicted swing (LDHitFeel::AdvanceSwing): a non-negative HoldSeconds holds the current action's pose at
    // that age until it is cleared with a negative value; Retract rewinds the shown attack to rest over
    // LDBodyMotion::RetractSeconds once the next cue ends it, instead of snapping to idle.
    void SetHold(float HoldSeconds);
    void Retract();
    // The ability of the shown cast (ELDDemoAbilityId; 0 = none or the innate dash): its own tint on the cast charge,
    // its release burst at the action's release and its dash trail colour (LDHitVfx::SkillLookOf).
    void SetCastAbility(uint8 Ability) { CastAbility = Ability; }
    // The combo step (1..3) of the shown basic attack (FLDObservedUnitView::ActionComboStep; the own body's from its
    // projection): its slash shape and, on the third, the finisher's hit tier. 0 shows the first stage's shape.
    void SetComboStep(uint8 Step) { ComboStep = Step; }
    // The local player's own body: its strikes show at full size with the ball orange; every other body's are smaller,
    // fainter and white (LDSliceVfx::FLook).
    void SetOwnBody(bool bOwn) { bOwnBody = bOwn; }
    bool IsOwnBody() const { return bOwnBody; }
    // The roster seat whose palette the body wears (MAX_uint8 for none).
    uint8 PaletteSlot() const { return LookSlot; }
    // The public overdrive charge (FLDObservedUnitView::bCharging; the own body's ChargingEntry): an energy column while
    // held, a ready flare once OVERDRIVE.HoldSec passes, and the release shockwave when it ends as a cast begins.
    // AgeSeconds: how long the charge has been held (authority age for a proxy); Progress: the own key's hold progress
    // 0..1 when known (ALDPlayerController::OverdriveHoldProgress), which then drives the column and the ready flare.
    void SetCharging(bool bInCharging, float AgeSeconds = 0.f, float Progress = -1.f)
    { bCharging = bInCharging; ChargeAge = AgeSeconds; ChargeProgress = Progress; }
    // A public element reaction on this body AgeSeconds ago (ELDCombatReaction, the element that set it off), committed at
    // AtServerSec on the authority clock: a fresh one flashes its cross and ring and bursts the element's particles once.
    // An old one, or the same reaction at the same authority time seen again, shows nothing.
    void SetReaction(uint8 Reaction, uint8 Element, float AgeSeconds, double AtServerSec);
    // The shown cast is an overdrive release (public, FLDObservedUnitView::bOverdriveCast): its release plays the shockwave.
    void SetOverdriveCast(bool bInOverdrive) { bOverdriveCast = bInOverdrive; }
    void PlayAppear();
    // Plays the death clip, then sinks and shrinks away; markers and actions stop. IsFallen once it has vanished.
    void PlayFall();
    bool IsFallen() const;
    bool CanShowImpact() const;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FLDPresentedModelPickingTest;
    friend class FLDBodyMotionTimelinesTest;
    friend class FLDBasketballRuntimeTest;
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
    // The current action's age for its pose and effects, held back by a hit confirmation's stop or a predicted hold.
    float PoseAge() const;
    // The action and age that show now: the current action, else a withdrawn swing still rewinding.
    LDBodyMotion::EAction ShownAction() const;
    float ShownAge() const;
    void SetFlash(const FLinearColor& Emissive);
    void ClearImpacts();
    void QueueImpact(double Onset, ELDObservedHitLook HitLook, uint8 HitFlags);
    // The designed swing and skill presentation (LDSliceVfx): telegraph from the windup's start, release at its impact.
    void UpdateSlice(float EffectScale);
    void UpdateRelease(float EffectScale);
    void UpdateDash(const FVector& Velocity, bool bFalling, float EffectScale);
    void UpdateOverdrive(float EffectScale);
    UPROPERTY() TObjectPtr<USceneComponent> Pivot;
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> Mesh;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Basketball;
    // One looping effect per element state (burn, wet, electrified, chilled, rotten) while it shows.
    static constexpr int32 ElementStates = 5;
    uint8 StateStacks[ElementStates] = {0, 0, 0, 0, 0};
    UPROPERTY() TObjectPtr<UNiagaraComponent> StateEffects[ElementStates];
    void UpdateStates(bool bFalling, float EffectScale);
    UPROPERTY() TObjectPtr<UNiagaraComponent> DashTrail;
    UPROPERTY() TObjectPtr<UNiagaraComponent> ChargeColumn;
    // Built by the constructor from the fixed part specs; never saved, so a map saved with another part list cannot
    // restore entries for subobjects that no longer exist.
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Edges;
    // The catalog body's worn parts (FLDArtAttachment), rebuilt with every look; they hang from Mesh.
    UPROPERTY() TArray<TObjectPtr<UMeshComponent>> Worn;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Paints;
    // The resolved catalog entry; valid while the game instance's art catalog lives.
    const FLDArtCharacter* Art = nullptr;
    // Eased toward the action timeline, so an ending cue settles instead of snapping to rest.
    LDBodyMotion::FChannels Shown;
    LDBodyMotion::EAction Action = LDBodyMotion::EAction::None;
    ELDArtAnim Clip = ELDArtAnim::Count;
    double Clock = 0, ActionOnset = 0, AppearAt = -1, FallAt = -1, HitOnset = -1, KnockbackAt = -1, ConfirmOnset = -1, RetractAt = -1;
    float HalfHeight = 0, HeightCm = 0, ActionImpactSeconds = LDBodyMotion::StrikeAt;
    float ConfirmStopSeconds = 0, ConfirmSparkScale = 0, HoldSeconds = -1, RetractFrom = 0, RetractImpact = LDBodyMotion::StrikeAt;
    float VisualYaw = 0, LeanPitch = 0, LeanRoll = 0;
    FVector PreviousVelocity = FVector::ZeroVector;
    double TelegraphOnset = -1, SwingReleasedOnset = -1;
    bool bTelegraphShown = false;
    uint8 ComboStep = 0;
    bool bOwnBody = false;
    // When a strike of the local player last landed on this body (a reaction right after it is the local player's).
    double OwnStrikeAt = -1;
    double ReleasedOnset = -1;
    // Body sounds (LDAudioFeel) on the same hooks as the effects: the swing and cast onsets already sounded, and the
    // playing cast charge that the release cuts.
    double SwingSoundOnset = -1, ChargeSoundOnset = -1, ReleaseSoundOnset = -1;
    // The skill whose visible strike the current impact was matched to (0 = none), for its own hit sound.
    uint8 HitAbility = 0;
    // The element states already sounded (their onset sting plays once per start).
    bool StateSounded[5] = {false, false, false, false, false};
    TWeakObjectPtr<class UAudioComponent> ChargeSound;
    void UpdateSound();
    uint8 CastAbility = 0;
    bool bDashing = false;
    double LastGhostAt = -1;
    bool bCharging = false, bChargeShown = false, bChargeReady = false, bOverdriveCast = false;
    double ChargeOnset = -1;
    float ChargeAge = 0, ChargeProgress = -1;
    double ReactionOnset = -1, ReactionAtServerSec = -1;
    uint8 ShownReaction = 0, PendingReactionElement = 0;
    bool bPendingReaction = false, bGhostWarmed = false;
    uint8 Cue = 0;
    ELDObservedHitLook HitLook = ELDObservedHitLook::None;
    uint8 HitFlags = 0;
    int64 LatestHitAtMs = 0;
    TArray<FGuid, TInlineAllocator<16>> SeenHitIds;
    uint8 LookSlot = MAX_uint8, LookCreature = 0;
    bool bCueSeen = false, bFlashing = false, bHitSeen = false, bVisualPoseInitialized = false, bKnockback = false;
    struct FPendingImpact
    {
        double Onset;
        ELDObservedHitLook Look;
        uint8 Flags;
    };
    // A hit the designed slice presentation shows (an unflagged plain, water, electric or fire hit); others keep the hit layers.
    // True when the hit was matched to a combo finisher (false also when no effect showed).
    bool ShowSliceImpact(const FPendingImpact& Impact, const FVector& Chest, const FVector& Feet, float EffectScale);
    // A plain hit matched to a visible skill cast whose skill has its own designed hit (ULDImpactFxSubsystem::MatchHit);
    // false when it has none, so the hit keeps its look's layers.
    bool ShowSkillHit(const FPendingImpact& Impact, const FVector& Chest, float EffectScale);
    TArray<FPendingImpact, TInlineAllocator<4>> PendingImpacts;
    // The shown mesh's shared picking model and this body's pose of it, posed at most once per frame.
    mutable TSharedPtr<const LDModelPicking::FSkinnedModel> PickingModel;
    mutable LDModelPicking::FSkinnedPose PickingPose;
    mutable uint64 PickingFrame = MAX_uint64;
};
