#include "Character/LDImpactFx.h"
#include "Character/LDCombatVfxCatalog.h"
#include "Character/LDHitFeel.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Core/LDLog.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace LDImpactFxDetail
{
    const FLinearColor WhiteHot(1.f, .28f, .045f), EmberOrange(1.f, .3f, .05f), ShieldCyan(0.f, .78f, 1.f), CritGold(1.f, .55f, .08f);
    constexpr float ReferenceLifeSeconds = .28f, ReferenceSpeed = 900.f, MaxWaitSeconds = .5f;

    FLinearColor LookColour(ELDObservedHitLook Look)
    {
        switch (Look)
        {
        case ELDObservedHitLook::Magic: return FLinearColor(.62f, .22f, 1.f);
        case ELDObservedHitLook::Fire: return FLinearColor(1.f, .22f, .025f);
        case ELDObservedHitLook::Water: return FLinearColor(.05f, .62f, 1.f);
        case ELDObservedHitLook::Electric: return FLinearColor(1.f, .78f, .05f);
        default: return WhiteHot;
        }
    }

    FLinearColor Lit(FLinearColor Colour, float Glow, float Alpha)
    {
        Colour.R *= Glow;
        Colour.G *= Glow;
        Colour.B *= Glow;
        Colour.A = Alpha;
        return Colour;
    }
}

FLinearColor LDImpactFx::Colour(ELDObservedHitLook Look, uint8 HitFlags, EKind Kind)
{
    using namespace LDImpactFxDetail;
    if (Kind == EKind::Shard || (HitFlags & LDObservation::ShieldAbsorbed)) { return ShieldCyan; }
    if (HitFlags & LDObservation::Critical) { return Kind == EKind::Ember ? EmberOrange : CritGold; }
    if (Kind == EKind::Ember) { return Look == ELDObservedHitLook::None ? EmberOrange : LookColour(Look) * .7f; }
    return LookColour(Look);
}

LDImpactFx::FPlan LDImpactFx::Plan(ELDObservedHitLook Look, uint8 HitFlags, const LDHitFeel::FTuning& Tuning, float EffectScale)
{
    using namespace LDImpactFxDetail;
    FPlan Result;
    if (!FMath::IsFinite(Tuning.ImpactScale) || !FMath::IsFinite(Tuning.ImpactSeconds)
        || !FMath::IsFinite(EffectScale) || Tuning.ImpactScale <= 0.f || Tuning.ImpactSeconds <= 0.f || EffectScale <= 0.f)
    { return Result; }
    const float Thin = FMath::Clamp(EffectScale, 0.f, 1.f);
    const bool bCritical = (HitFlags & LDObservation::Critical) != 0;
    const bool bShield = (HitFlags & LDObservation::ShieldAbsorbed) != 0;
    const bool bBroken = (HitFlags & LDObservation::ShieldBroken) != 0;
    const float Weight = bCritical ? 1.6f : bBroken ? 1.4f : 1.f;
    const float Glow = FMath::IsFinite(Tuning.ImpactGlow) ? FMath::Max(Tuning.ImpactGlow, 0.f) : 0.f;
    const float Speed = FMath::IsFinite(Tuning.ImpactSpeed) ? FMath::Max(Tuning.ImpactSpeed, 0.f) : 0.f;
    Result.bEnabled = true;
    Result.Sparks = FMath::RoundToInt(FMath::Min(float(FMath::Max(Tuning.ImpactSparks, 0)) * Weight * Thin, 64.f));
    Result.Smoke = FMath::RoundToInt(5.f * Thin);
    Result.Shards = bShield ? FMath::RoundToInt((bBroken ? 14.f : 6.f) * Thin) : 0;
    Result.Scale = Tuning.ImpactScale * FMath::Sqrt(Thin);
    Result.LifeScale = Tuning.ImpactSeconds / ReferenceLifeSeconds;
    Result.SpeedScale = Speed / ReferenceSpeed * (bCritical ? 1.25f : 1.f);
    Result.SparkColour = Lit(Colour(Look, HitFlags, EKind::Spark), Glow, Thin);
    Result.CoreColour = Lit(Colour(Look, HitFlags, EKind::Spark), Glow * .7f, .8f * Thin);
    Result.SmokeColour = FLinearColor(.10f, .08f, .06f, .55f * Thin);
    Result.ShardColour = Lit(Colour(Look, HitFlags, EKind::Shard), Glow * .35f, Thin);
    return Result;
}

bool ULDImpactFxSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && !IsRunningDedicatedServer() && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE)
        && Super::ShouldCreateSubsystem(Outer);
}

void ULDImpactFxSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    Catalog = LoadObject<ULDCombatVfxCatalog>(nullptr, ULDCombatVfxCatalog::AssetPath);
    ImpactSystem = Catalog ? Catalog->Impact.Get() : nullptr;
#if WITH_EDITOR
    // Start editor-only Niagara compilation during world startup rather than on the first swing.
    if (ImpactSystem) { ImpactSystem->RequestCompile(false); }
    if (Catalog && Catalog->SwingTrail) { Catalog->SwingTrail->RequestCompile(false); }
    if (Catalog && Catalog->CastCharge) { Catalog->CastCharge->RequestCompile(false); }
#endif
    if (!ImpactSystem)
    {
        UE_LOG(LogLDContent, Error, TEXT("LD_IMPACT_FX asset=missing catalog=%s"), ULDCombatVfxCatalog::AssetPath);
    }
    if (Catalog && !Catalog->SwingTrail)
    {
        UE_LOG(LogLDContent, Error, TEXT("LD_SWING_FX asset=missing catalog=%s"), ULDCombatVfxCatalog::AssetPath);
    }
    if (Catalog && !Catalog->CastCharge)
    {
        UE_LOG(LogLDContent, Error, TEXT("LD_CAST_FX asset=missing catalog=%s"), ULDCombatVfxCatalog::AssetPath);
    }
}

void ULDImpactFxSubsystem::Deinitialize()
{
    while (!Live.IsEmpty()) { Release(Live.Num() - 1); }
    ImpactSystem = nullptr;
    Catalog = nullptr;
    Super::Deinitialize();
}

TStatId ULDImpactFxSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(ULDImpactFxSubsystem, STATGROUP_Tickables);
}

void ULDImpactFxSubsystem::Burst(ULDHumanoidVisualComponent& Source, const FVector& At, ELDObservedHitLook Look,
    uint8 HitFlags, float AgeSeconds, float EffectScale)
{
    if (!FApp::CanEverRender() || !Source.CanShowImpact() || At.ContainsNaN() || !FMath::IsFinite(AgeSeconds) || !ImpactSystem) { return; }
    const LDImpactFx::FPlan Plan = LDImpactFx::Plan(Look, HitFlags, LDHitFeel::Read(), EffectScale);
    if (!Plan.bEnabled) { return; }
    if (Live.Num() >= LDImpactFx::MaxBursts) { Release(0); }
    UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), ImpactSystem, At,
        FRotator::ZeroRotator, FVector(1.f), false, false, ENCPoolMethod::ManualRelease, false);
    if (!Effect)
    {
        UE_LOG(LogLDContent, Warning, TEXT("LD_IMPACT_FX spawn=failed"));
        return;
    }
    Effect->SetVariableInt(TEXT("User.SparkCount"), Plan.Sparks);
    Effect->SetVariableInt(TEXT("User.SmokeCount"), Plan.Smoke);
    Effect->SetVariableInt(TEXT("User.ShardCount"), Plan.Shards);
    Effect->SetVariableFloat(TEXT("User.Scale"), Plan.Scale);
    Effect->SetVariableFloat(TEXT("User.LifeScale"), Plan.LifeScale);
    Effect->SetVariableFloat(TEXT("User.SpeedScale"), Plan.SpeedScale);
    Effect->SetVariableLinearColor(TEXT("User.SparkColor"), Plan.SparkColour);
    Effect->SetVariableLinearColor(TEXT("User.CoreColor"), Plan.CoreColour);
    Effect->SetVariableLinearColor(TEXT("User.SmokeColor"), Plan.SmokeColour);
    Effect->SetVariableLinearColor(TEXT("User.ShardColor"), Plan.ShardColour);
    Effect->Activate(true);
    FLive& Spawned = Live.AddDefaulted_GetRef();
    Spawned.Source = &Source;
    Spawned.Effect = Effect;
    Spawned.InitialAge = FMath::Max(AgeSeconds, 0.f);
    if (Effect->IsActive())
    {
        Spawned.bStarted = true;
        Effect->AdvanceSimulationByTime(Spawned.InitialAge, 1.f / 60.f);
    }
}

void ULDImpactFxSubsystem::Release(int32 Index)
{
    if (UNiagaraComponent* Effect = Live[Index].Effect.Get())
    {
        Effect->DeactivateImmediate();
        if (Effect->PoolingMethod == ENCPoolMethod::ManualRelease) { Effect->ReleaseToPool(); }
        else { Effect->DestroyComponent(); }
    }
    Live.RemoveAt(Index, EAllowShrinking::No);
}

void ULDImpactFxSubsystem::ClearFor(const ULDHumanoidVisualComponent& Source)
{
    for (int32 Index = Live.Num() - 1; Index >= 0; --Index)
    {
        if (Live[Index].Source.Get() == &Source) { Release(Index); }
    }
}

void ULDImpactFxSubsystem::Tick(float DeltaTime)
{
    const float Step = FMath::IsFinite(DeltaTime) ? FMath::Max(DeltaTime, 0.f) : 0.f;
    for (int32 Index = Live.Num() - 1; Index >= 0; --Index)
    {
        FLive& Burst = Live[Index];
        const ULDHumanoidVisualComponent* Source = Burst.Source.Get();
        UNiagaraComponent* Effect = Burst.Effect.Get();
        if (!Source || !Source->CanShowImpact() || !Effect)
        {
            Release(Index);
            continue;
        }
        if (!Burst.bStarted)
        {
            Burst.WaitingSeconds += Step;
            if (Effect->IsActive())
            {
                Burst.bStarted = true;
                Effect->AdvanceSimulationByTime(Burst.InitialAge + Burst.WaitingSeconds, 1.f / 60.f);
            }
            else if (Burst.WaitingSeconds >= LDImpactFxDetail::MaxWaitSeconds)
            {
                UE_LOG(LogLDContent, Warning, TEXT("LD_IMPACT_FX activation=timed_out catalog=%s"), ULDCombatVfxCatalog::AssetPath);
                Release(Index);
            }
        }
        else if (Effect->IsComplete()) { Release(Index); }
    }
}
