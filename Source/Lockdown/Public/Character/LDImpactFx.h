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
    // Applies a zone look (LDHitVfx::FZone) to a looping NS_LDZone component.
    static void ApplyZone(UNiagaraComponent& Effect, const LDHitVfx::FZone& Zone, float RadiusCm, const FVector& Velocity);
    // The catalog's saved layer tuning times the live impact controls (LDHitVfx::TuningOf).
    LDHitVfx::FLayerTuning LayerTuning() const;
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
    static constexpr int32 MaxLayers = 48;
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
    void Release(int32 Index);
    void ReleaseLayer(int32 Index);
    struct FLiveGhost
    {
        TWeakObjectPtr<const ULDHumanoidVisualComponent> Owner;
        int32 Slot = INDEX_NONE;
        float Age = 0;
        LDHitVfx::FGhosts Look;
    };
    void ReleaseGhost(int32 Index);
    bool LoadLayerAssets();
    int32 TakeSlot(bool bShell);
    void Configure(UStaticMeshComponent& Mesh, bool bShell);
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
    TArray<bool> SlotShell;
    TArray<int32> FreeSlots;
    TArray<FLiveLayer> LiveLayers;
    UPROPERTY() TObjectPtr<UMaterialInterface> GhostMaterial;
    UPROPERTY() TArray<TObjectPtr<UPoseableMeshComponent>> GhostSlots;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> GhostPaints;
    TArray<int32> FreeGhosts;
    TArray<FLiveGhost> LiveGhosts;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    bool bContentTried = false;
    bool bLayerAssetsTried = false, bLayerAssetsReady = false;
    LDHitVfx::FScreenFlash Flash;
    double FlashAt = -1;
    UPROPERTY() TObjectPtr<UNiagaraSystem> ImpactSystem;
    UPROPERTY() TObjectPtr<UNiagaraSystem> BurstSystem;
    UPROPERTY() TObjectPtr<UNiagaraSystem> ZoneSystem;
    void Track(UNiagaraComponent* Effect, const ULDHumanoidVisualComponent* Source, float AgeSeconds, const AActor* OwnerActor = nullptr);
    UNiagaraComponent* SpawnKit(const FVector& At, const LDHitVfx::FBurst& Burst);
    TArray<FLive> Live;
};
