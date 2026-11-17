#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Vision/LDViewTypes.h"
#include "LDImpactFx.generated.h"

class ULDHumanoidVisualComponent;
class ULDCombatVfxCatalog;
class UNiagaraComponent;
class UNiagaraSystem;
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
    void ClearFor(const ULDHumanoidVisualComponent& Source);
    int32 LiveCount() const { return Live.Num(); }
    const ULDCombatVfxCatalog* GetCatalog() const { return Catalog; }
private:
    struct FLive
    {
        TWeakObjectPtr<ULDHumanoidVisualComponent> Source;
        TWeakObjectPtr<UNiagaraComponent> Effect;
        float InitialAge = 0;
        float WaitingSeconds = 0;
        bool bStarted = false;
    };
    void Release(int32 Index);
    UPROPERTY() TObjectPtr<ULDCombatVfxCatalog> Catalog;
    UPROPERTY() TObjectPtr<UNiagaraSystem> ImpactSystem;
    TArray<FLive> Live;
};
