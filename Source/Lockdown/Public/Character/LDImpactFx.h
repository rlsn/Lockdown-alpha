#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Character/LDHitVfx.h"
#include "Vision/LDViewTypes.h"
#include "LDImpactFx.generated.h"

class ULDHumanoidVisualComponent;
class ULDCombatVfxCatalog;
class UNiagaraComponent;
class UNiagaraSystem;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UPoseableMeshComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class FLDDemoContentSnapshot;
class UTexture2D;
namespace LDHitFeel { struct FTuning; }

namespace LDImpactFx
{
    enum class EKind : uint8 { Spark, Ember, Shard };
    struct FPlan
    {
        bool bEnabled = false;
        int32 Sparks = 0, Smoke = 0, Shards = 0;
        float Scale = 0, LifeScale = 0, SpeedScale = 0;
        FLinearColor SparkColour, CoreColour, SmokeColour, ShardColour;
    };
    constexpr int32 MaxBursts = 32;
    LOCKDOWN_API FLinearColor Colour(ELDObservedHitLook Look, uint8 HitFlags, EKind Kind);
    LOCKDOWN_API FPlan Plan(ELDObservedHitLook Look, uint8 HitFlags, const LDHitFeel::FTuning& Tuning, float EffectScale);
}

// Local, observation-owned Niagara bursts. Hiding or removing the presented source immediately removes its effects.
UCLASS()
class LOCKDOWN_API ULDImpactFxSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    void Burst(ULDHumanoidVisualComponent& Source, const FVector& At, ELDObservedHitLook Look, uint8 HitFlags,
        float AgeSeconds, float EffectScale);
    // The shaped layers (LDHitVfx) of one hit or fall: Chest is the struck point, Feet the ground under the body. A
    // layer bound to Source goes with it (hidden, fallen or removed); an unbound one (a fall's crack) plays out alone.
    void Layers(const ULDHumanoidVisualComponent* Source, const FVector& Chest, const FVector& Feet,
        const LDHitVfx::FRecipe& Recipe, float AgeSeconds);
    // The own screen edge flash and lens fringe of a fresh receipt (drawn by the world feedback layer).
    void FlashScreen(const LDHitVfx::FScreenFlash& Flash);
    float ScreenFlashAlpha(FLinearColor& OutColour) const;
    float ScreenFringe() const;
    UTexture2D* GetScreenEdge() const { return ScreenEdge; }
    // One particle-kit burst at a world point. Bound to Source (cleared with it) or free-standing when Source is null.
    void Kit(const FVector& At, const LDHitVfx::FBurst& Burst, const ULDHumanoidVisualComponent* Source = nullptr);
    // A burst that lives only while Owner does (a visible skill effect's launch or landing): a reset or visibility removal
    // that destroys the proxy removes it at once.
    void Kit(const FVector& At, const LDHitVfx::FBurst& Burst, const AActor& Owner);
    // 0.45 while the local player reduces combat effects, else 1.
    float LocalEffectScale() const;
    // One afterimage of Source's current pose (LDHitVfx::FGhosts), cleared with Owner. Pooled up to MaxGhosts.
    void Ghost(const USkeletalMeshComponent& Source, const LDHitVfx::FGhosts& Ghosts, const ULDHumanoidVisualComponent& Owner);
    int32 LiveGhostCount() const { return LiveGhosts.Num(); }
    static constexpr int32 MaxGhosts = 24;
    // OVERDRIVE.HoldSec from the approved content (when a charge is full, LDHitVfx::ForChargeReady); 0 if unavailable.
    float OverdriveHoldSeconds();
    // A looping zone (LDHitVfx::FZone) on Owner, centred Offset from it: created, updated, or removed when the zone shows nothing.
    UNiagaraComponent* ShowZone(USceneComponent& Owner, UNiagaraComponent* Existing, const LDHitVfx::FZone& Zone, float RadiusCm,
        const FVector& Velocity, const FVector& Offset = FVector::ZeroVector);
    // Shader and asset warm-up, so the first effect of a match does not hitch: at world start every Niagara system
    // plays once far under the ground and one layer plane and shell are registered (their PSOs precache at registration);
    // WarmGhost registers an afterimage copy of a body's mesh the first time that mesh is shown.
    void WarmGhost(const USkeletalMeshComponent& Source);
    int32 WarmedSystems() const { return WarmedCount; }
    // Applies a zone look (LDHitVfx::FZone) to a looping NS_LDZone component.
    static void ApplyZone(UNiagaraComponent& Effect, const LDHitVfx::FZone& Zone, float RadiusCm, const FVector& Velocity);
    // The catalog's saved layer tuning times the live impact controls (LDHitVfx::TuningOf).
    LDHitVfx::FLayerTuning LayerTuning() const;
    // The designed slice layers (LDSliceVfx) of one recipe. A recipe bound to Source goes with it (hidden, fallen or
    // removed); an unbound one plays out alone. AgeSeconds starts it part way (a late-seen event).
    void Slice(const ULDHumanoidVisualComponent* Source, const LDSliceVfx::FRecipe& Recipe, float AgeSeconds);
    // Removes Source's telegraphs (a withdrawn or cancelled windup).
    void ClearTelegraphs(const ULDHumanoidVisualComponent& Source);
    // The look of one kind of slice layer: own or another body's, the reduced-effects scale and the live tuning
    // (impacts: ld.Feel.Impact* x DA_CombatVfx Layer*; swings: ld.Feel.Trail* x SwingSize/SwingSeconds; telegraphs: Cast*).
    enum class ESliceTuning : uint8 { Impact, Swing, Telegraph };
    LDSliceVfx::FLook SliceLook(ESliceTuning Kind, bool bOwn, bool bFocus) const;
    // A visible body's strike reached its release: hits that follow on visible bodies take its tier, direction and
    // owner (MatchHit). Only visible bodies note strikes, so a hit never reveals a hidden source.
    // Ability: the visible cast's ELDDemoAbilityId for a skill strike (its hits then show that skill's hit), 0 otherwise.
    void NoteStrike(const ULDHumanoidVisualComponent& Source, LDSliceVfx::EStrike Strike, const FVector& From, const FVector& Dir, bool bOwn,
        uint8 Ability = 0);
    struct FStrikeMatch
    {
        LDSliceVfx::EStrike Strike = LDSliceVfx::EStrike::None;
        FVector From = FVector(NAN);
        FVector Dir = FVector::ZeroVector;
        bool bOwn = false;
        bool bChained = false;   // a later link of a chain: its arc starts at the previous body struck
        uint8 Ability = 0;       // the matched skill strike's ability (ELDDemoAbilityId), 0 for others
    };
    // The strike a hit HitAgeSeconds old on Victim's Chest belongs to: the nearest in time of the strikes whose kind
    // fits the hit's look, timing and reach; None when no visible strike fits.
    FStrikeMatch MatchHit(const ULDHumanoidVisualComponent& Victim, const FVector& Chest, ELDObservedHitLook Look, float HitAgeSeconds);
    // One hop of a chain (active_zap) as the Niagara Chain: the bolt from From to To and its end sparks, in Look's size
    // and brightness, bound to Victim (cleared with it) and stopped after its duration.
    void Chain(const ULDHumanoidVisualComponent& Victim, const FVector& From, const FVector& To, bool bChained, const LDSliceVfx::FLook& Look, float AgeSeconds);
    // A generated system (LDSliceVfx::ESystem); null when its asset is missing (logged at world start).
    UNiagaraSystem* GetSystem(LDSliceVfx::ESystem System) const { return GeneratedSystems.IsValidIndex(int32(System)) ? GeneratedSystems[int32(System)].Get() : nullptr; }
    // Every live component of a generated system (capture probes count their particles and read their user parameters).
    void ForEachGenerated(TFunctionRef<void(UNiagaraComponent&)> Visit) const;
    // A basic swing's anticipation (combo Step 1-3) on the floor round Feet along Facing over WindupSeconds, bound to
    // Owner and cleared with its telegraphs.
    void SwingTell(const ULDHumanoidVisualComponent& Owner, uint8 Step, const FVector& Feet, const FVector& Facing, float WindupSeconds,
        const LDSliceVfx::FLook& Look, float AgeSeconds);
    // A basic swing's arc of motion (combo Step 1-3) round Centre along Facing, bound to Owner.
    void Swing(const ULDHumanoidVisualComponent& Owner, uint8 Step, const FVector& Centre, const FVector& Facing, const LDSliceVfx::FLook& Look, float AgeSeconds);
    // One generated system (a skill's telegraph, release or hit) on Owner: pooled, its user parameters set (Intensity from
    // Look, 70 % for others' effects), cleared with Owner and stopped after Seconds; a telegraph also goes with
    // ClearTelegraphs. Width is the system's own measure (a band's half width, a radius), never scaled here. Null when it
    // cannot show.
    UNiagaraComponent* Play(LDSliceVfx::ESystem System, const ULDHumanoidVisualComponent& Owner, const FVector& Start, const FVector& End,
        const FLinearColor& Tint, float Width, float Seconds, const LDSliceVfx::FLook& Look, float AgeSeconds, bool bTelegraph = false);
    // A looping generated system owned by the caller (a ground state, a status on a body, a flight): made on Owner's actor
    // and attached to Owner, its user parameters (world space) set on every call. ESystem::Count, a missing asset or a
    // render-less process removes Existing and returns null. The caller keeps the result and destroys it with itself.
    UNiagaraComponent* ShowLoop(USceneComponent& Owner, UNiagaraComponent* Existing, LDSliceVfx::ESystem System, const FVector& Start,
        const FVector& End, const FLinearColor& Tint, float Width, float Intensity, float Seconds, bool bFocus);
    // The supporting floor under At (the point itself when nothing is below it within reach).
    FVector FloorUnder(const FVector& At) const { FVector Normal; return GroundUnder(At, Normal); }
    // A basic or finisher hit on Victim's Chest struck from From's side, bound to Victim.
    void Hit(const ULDHumanoidVisualComponent& Victim, const FVector& From, const FVector& Chest, bool bFinisher, const LDSliceVfx::FLook& Look, float AgeSeconds);
    // A public electric ground state's field on Owner at its authoritative radius (fades with Alpha; Alpha <= 0 removes it).
    // OwnerSeat is the state's public owner seat: the own body's seat shows the field's decoration in full.
    void ShowGroundField(const AActor& Owner, const FVector& Centre, float RadiusCm, float Alpha, uint8 OwnerSeat);
    // Whether an effect is the electric field (NS_LDField).
    bool IsField(const UNiagaraComponent* Effect) const;
    // The conduct reaction's splash field round Source's feet at the authoritative radius, for 0.8 s.
    void FieldPulse(const ULDHumanoidVisualComponent& Source, const FVector& Feet, float RadiusCm, const LDSliceVfx::FLook& Look, float AgeSeconds);
    // The approved reach of a skill's level-1 target (its range and, for a direction line, the band's half width) and
    // the conduct reaction's splash radius; 0 when the content is unavailable.
    bool SkillReach(uint8 Ability, float& OutRangeCm, float& OutHalfWidthCm);
    // The approved full angle of a skill's level-1 cone target (degrees); 0 when it has none.
    float SkillConeDegrees(uint8 Ability);
    float ConductRadiusCm();
    // The approved level-1 radius of one of a skill's effects (its self area, zone, ground impact...); 0 when it has none
    // or the content is unavailable.
    float SkillEffectRadius(uint8 Ability, uint16 Opcode);
    int32 LiveSliceCount() const { return LiveSlices.Num(); }
    // A visible skill effect's own particles: its flight trail or ground zone on Owner, and its appear burst.
    UNiagaraComponent* ShowEffect(USceneComponent& Owner, UNiagaraComponent* Existing, ELDObservedProjectileLook Look,
        float RadiusCm, const FRotator& Heading);
    UNiagaraSystem* GetBurstSystem() const { return BurstSystem; }
    UNiagaraSystem* GetZoneSystem() const { return ZoneSystem; }
    void ClearFor(const ULDHumanoidVisualComponent& Source);
    // Removes the bursts bound to Owner at once (a skill-effect proxy removed by a reset or visibility change).
    void ClearForActor(const AActor& Owner);
    int32 LiveCount() const { return Live.Num(); }
    int32 LiveLayerCount() const { return LiveLayers.Num(); }
    // The pooled layer components shared by the hit layers and the slice layers.
    static constexpr int32 MaxLayers = 96;
    const ULDCombatVfxCatalog* GetCatalog() const { return Catalog; }
private:
    struct FLive
    {
        TWeakObjectPtr<const ULDHumanoidVisualComponent> Source;
        TWeakObjectPtr<const AActor> OwnerActor;
        bool bBound = true;
        TWeakObjectPtr<UNiagaraComponent> Effect;
        float InitialAge = 0;
        float WaitingSeconds = 0;
        bool bStarted = false;
        // A looping effect (the Chain) is deactivated once it has run this long; negative never.
        float StopAfter = -1, Age = 0;
        // An anticipation (a swing's telegraph), cleared with ClearTelegraphs.
        bool bTelegraph = false;
        // A field: the next time (its age) its arcs reach for the bodies standing in it.
        float NextBite = 0;
    };
    struct FLiveLayer
    {
        TWeakObjectPtr<const ULDHumanoidVisualComponent> Source;
        bool bBound = false;
        int32 Slot = INDEX_NONE;
        LDHitVfx::FLayer Layer;
        float Age = 0;
        FVector At = FVector::ZeroVector;
        FVector Normal = FVector::UpVector;  // a ground mark's supporting surface at spawn
    };
    struct FLiveSlice
    {
        TWeakObjectPtr<const ULDHumanoidVisualComponent> Source;
        TWeakObjectPtr<const AActor> OwnerActor;
        bool bBound = false;
        int32 Slot = INDEX_NONE;
        LDSliceVfx::FLayer Layer;
        float Age = 0;
        float ExternalAlpha = 1;
        FVector Ground = FVector::ZeroVector;   // flat layers: the supporting surface under At at spawn
        FVector Normal = FVector::UpVector;
    };
    TArray<FLiveSlice> LiveSlices;
    struct FStrike
    {
        TWeakObjectPtr<const ULDHumanoidVisualComponent> Source;
        LDSliceVfx::EStrike Strike = LDSliceVfx::EStrike::None;
        FVector From = FVector::ZeroVector, Dir = FVector::ZeroVector, Last = FVector::ZeroVector;
        double At = 0;
        bool bOwn = false;
        int32 Hits = 0;
        uint8 Ability = 0;
    };
    TArray<FStrike> Strikes;
    void Release(int32 Index);
    void ReleaseLayer(int32 Index);
    void ReleaseSlice(int32 Index);
    void PlaceSlice(FLiveSlice& Shown);
    FVector GroundUnder(const FVector& At, FVector& OutNormal) const;
    // A field's arcs reaching for the bodies standing in it (Tick, every 0.4 s of the field's life).
    void Bite(const UNiagaraComponent& Field);
    // A pooled generated system with the common user parameters set and activated; null when it cannot show.
    UNiagaraComponent* SpawnGenerated(LDSliceVfx::ESystem System, const FVector& Start, const FVector& End, const FLinearColor& Tint, float Width,
        float Intensity, float Seconds, bool bFocus);
    bool LoadSliceAssets();
    bool bSliceAssetsTried = false, bSliceAssetsReady = false;
    UPROPERTY() TObjectPtr<UMaterialInterface> SliceMaterial;
    UPROPERTY() TObjectPtr<UStaticMesh> SliceQuad;
    UPROPERTY() TArray<TObjectPtr<UStaticMesh>> SlashMeshes;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> SliceTextures;
    struct FLiveGhost
    {
        TWeakObjectPtr<const ULDHumanoidVisualComponent> Owner;
        // A rim shell (FGhosts::bShell) keeps following this body's pose; an afterimage stays where it was left.
        TWeakObjectPtr<const USkeletalMeshComponent> Source;
        int32 Slot = INDEX_NONE;
        float Age = 0;
        LDHitVfx::FGhosts Look;
    };
    void ReleaseGhost(int32 Index);
    bool LoadLayerAssets();
    // Slot kinds: a hit layer plane, a shield shell sphere, a slice quad, then one per slash mesh.
    enum ESlotKind : uint8 { SlotPlane, SlotShell, SlotQuad, SlotSlash };
    int32 TakeSlot(uint8 Kind);
    void Configure(UStaticMeshComponent& Mesh, uint8 Kind);
    void PlaceLayer(FLiveLayer& Live, float Brightness);
    UPROPERTY() TObjectPtr<ULDCombatVfxCatalog> Catalog;
    UPROPERTY() TObjectPtr<AActor> LayerHost;
    UPROPERTY() TObjectPtr<UStaticMesh> PlaneMesh;
    UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> LayerMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> ShellMaterial;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> Masks;
    UPROPERTY() TObjectPtr<UTexture2D> ScreenEdge;
    // Pooled layer components; a slot is free while no live layer uses it.
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Slots;
    TArray<uint8> SlotKinds;
    TArray<int32> FreeSlots;
    TArray<FLiveLayer> LiveLayers;
    UPROPERTY() TObjectPtr<UMaterialInterface> GhostMaterial;
    UPROPERTY() TArray<TObjectPtr<UPoseableMeshComponent>> GhostSlots;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> GhostPaints;
    // The rim shell paint of each ghost slot (FGhosts::bShell), made on first use.
    UPROPERTY() TObjectPtr<UMaterialInterface> BodyGlowMaterial;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> ShellPaints;
    UMaterialInstanceDynamic* PaintOf(int32 Slot, bool bShell);
    TArray<int32> FreeGhosts;
    TArray<FLiveGhost> LiveGhosts;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    bool bContentTried = false;
    void WarmUp();
    TArray<TWeakObjectPtr<UNiagaraComponent>> Warming;
    TSet<TWeakObjectPtr<const UObject>> WarmedMeshes;
    float WarmSeconds = 0;
    int32 WarmedCount = 0;
    bool bLayerAssetsTried = false, bLayerAssetsReady = false;
    LDHitVfx::FScreenFlash Flash;
    double FlashAt = -1;
    UPROPERTY() TObjectPtr<UNiagaraSystem> ImpactSystem;
    UPROPERTY() TObjectPtr<UNiagaraSystem> BurstSystem;
    UPROPERTY() TObjectPtr<UNiagaraSystem> ZoneSystem;
    UPROPERTY() TArray<TObjectPtr<UNiagaraSystem>> GeneratedSystems;
    void Track(UNiagaraComponent* Effect, const ULDHumanoidVisualComponent* Source, float AgeSeconds, const AActor* OwnerActor = nullptr);
    UNiagaraComponent* SpawnKit(const FVector& At, const LDHitVfx::FBurst& Burst);
    TArray<FLive> Live;
};
