#include "Character/LDImpactFx.h"
#include "Character/LDCombatVfxCatalog.h"
#include "Character/LDHitFeel.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Core/LDLog.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/LDPlayerController.h"
#include "UI/LDControlSettings.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
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
    if (HitFlags & LDObservation::Overdriven) { return Kind == EKind::Ember ? FLinearColor(.8f, .1f, .55f) : FLinearColor(1.f, .18f, .75f); }
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
    const bool bOverdrive = (HitFlags & LDObservation::Overdriven) != 0;
    const float Weight = bOverdrive ? 1.9f : bCritical ? 1.6f : bBroken ? 1.4f : 1.f;
    const float Glow = FMath::IsFinite(Tuning.ImpactGlow) ? FMath::Max(Tuning.ImpactGlow, 0.f) : 0.f;
    const float Speed = FMath::IsFinite(Tuning.ImpactSpeed) ? FMath::Max(Tuning.ImpactSpeed, 0.f) : 0.f;
    Result.bEnabled = true;
    Result.Sparks = FMath::RoundToInt(FMath::Min(float(FMath::Max(Tuning.ImpactSparks, 0)) * Weight * Thin, 64.f));
    Result.Smoke = FMath::RoundToInt(5.f * Thin);
    Result.Shards = bShield ? FMath::RoundToInt((bBroken ? 14.f : 6.f) * Thin) : 0;
    Result.Scale = Tuning.ImpactScale * FMath::Sqrt(Thin);
    Result.LifeScale = Tuning.ImpactSeconds / ReferenceLifeSeconds;
    Result.SpeedScale = Speed / ReferenceSpeed * (bOverdrive ? 1.4f : bCritical ? 1.25f : 1.f);
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
    BurstSystem = Catalog && Catalog->Burst ? Catalog->Burst.Get() : LoadObject<UNiagaraSystem>(nullptr, ULDCombatVfxCatalog::DefaultBurst);
    ZoneSystem = Catalog && Catalog->Zone ? Catalog->Zone.Get() : LoadObject<UNiagaraSystem>(nullptr, ULDCombatVfxCatalog::DefaultZone);
#if WITH_EDITOR
    // Start editor-only Niagara compilation during world startup rather than on the first swing.
    if (ImpactSystem) { ImpactSystem->RequestCompile(false); }
    if (Catalog && Catalog->SwingTrail) { Catalog->SwingTrail->RequestCompile(false); }
    if (Catalog && Catalog->CastCharge) { Catalog->CastCharge->RequestCompile(false); }
    if (BurstSystem) { BurstSystem->RequestCompile(false); }
    if (ZoneSystem) { ZoneSystem->RequestCompile(false); }
#endif
    WarmUp();
    if (!BurstSystem || !ZoneSystem)
    {
        UE_LOG(LogLDContent, Error, TEXT("LD_PARTICLE_KIT asset=missing burst=%s zone=%s"),
            BurstSystem ? TEXT("ok") : ULDCombatVfxCatalog::DefaultBurst, ZoneSystem ? TEXT("ok") : ULDCombatVfxCatalog::DefaultZone);
    }
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
    while (!LiveLayers.IsEmpty()) { ReleaseLayer(LiveLayers.Num() - 1); }
    while (!LiveGhosts.IsEmpty()) { ReleaseGhost(LiveGhosts.Num() - 1); }
    GhostSlots.Reset();
    GhostPaints.Reset();
    FreeGhosts.Reset();
    if (IsValid(LayerHost)) { LayerHost->Destroy(); }
    LayerHost = nullptr;
    Slots.Reset();
    SlotShell.Reset();
    FreeSlots.Reset();
    ImpactSystem = nullptr;
    BurstSystem = nullptr;
    ZoneSystem = nullptr;
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
    Track(Effect, &Source, AgeSeconds);
}

void ULDImpactFxSubsystem::Track(UNiagaraComponent* Effect, const ULDHumanoidVisualComponent* Source, float AgeSeconds,
    const AActor* OwnerActor)
{
    FLive& Spawned = Live.AddDefaulted_GetRef();
    Spawned.Source = Source;
    Spawned.OwnerActor = OwnerActor;
    Spawned.bBound = Source != nullptr || OwnerActor != nullptr;
    Spawned.Effect = Effect;
    Spawned.InitialAge = FMath::Max(AgeSeconds, 0.f);
    if (Effect->IsActive())
    {
        Spawned.bStarted = true;
        if (Spawned.InitialAge > 0.f) { Effect->AdvanceSimulationByTime(Spawned.InitialAge, 1.f / 60.f); }
    }
}

void ULDImpactFxSubsystem::Kit(const FVector& At, const LDHitVfx::FBurst& Burst, const ULDHumanoidVisualComponent* Source)
{
    if (Source && !Source->CanShowImpact()) { return; }
    if (UNiagaraComponent* Effect = SpawnKit(At, Burst)) { Track(Effect, Source, 0.f); }
}

void ULDImpactFxSubsystem::Kit(const FVector& At, const LDHitVfx::FBurst& Burst, const AActor& Owner)
{
    if (!IsValid(&Owner) || Owner.IsActorBeingDestroyed()) { return; }
    if (UNiagaraComponent* Effect = SpawnKit(At, Burst)) { Track(Effect, nullptr, 0.f, &Owner); }
}

UNiagaraComponent* ULDImpactFxSubsystem::SpawnKit(const FVector& At, const LDHitVfx::FBurst& Burst)
{
    if (!FApp::CanEverRender() || !BurstSystem || Burst.IsEmpty() || At.ContainsNaN()) { return nullptr; }
    if (Live.Num() >= LDImpactFx::MaxBursts) { Release(0); }
    UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), BurstSystem, At,
        FRotator::ZeroRotator, FVector(1.f), false, false, ENCPoolMethod::ManualRelease, false);
    if (!Effect)
    {
        UE_LOG(LogLDContent, Warning, TEXT("LD_PARTICLE_KIT spawn=failed"));
        return nullptr;
    }
    const float Count = Catalog ? FMath::Max(Catalog->KitCount, 0.f) : 1.f;
    const float Size = Catalog ? FMath::Max(Catalog->KitSize, 0.f) : 1.f;
    Effect->SetVariableInt(TEXT("User.SparkCount"), FMath::RoundToInt(float(Burst.Sparks) * Count));
    Effect->SetVariableInt(TEXT("User.SmokeCount"), FMath::RoundToInt(float(Burst.Smoke) * Count));
    Effect->SetVariableInt(TEXT("User.ShardCount"), FMath::RoundToInt(float(Burst.Shards) * Count));
    Effect->SetVariableFloat(TEXT("User.Scale"), Burst.Scale * Size);
    Effect->SetVariableFloat(TEXT("User.LifeScale"), Burst.Life);
    Effect->SetVariableFloat(TEXT("User.SpeedScale"), Burst.Speed);
    Effect->SetVariableFloat(TEXT("User.Drag"), Burst.Drag);
    Effect->SetVariableVec3(TEXT("User.Gravity"), Burst.Gravity);
    Effect->SetVariableLinearColor(TEXT("User.SparkColor"), Burst.SparkColour);
    Effect->SetVariableLinearColor(TEXT("User.CoreColor"), Burst.CoreColour);
    Effect->SetVariableLinearColor(TEXT("User.SmokeColor"), Burst.SmokeColour);
    Effect->SetVariableLinearColor(TEXT("User.ShardColor"), Burst.ShardColour);
    Effect->Activate(true);
    return Effect;
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
    for (int32 Index = LiveLayers.Num() - 1; Index >= 0; --Index)
    {
        if (LiveLayers[Index].bBound && LiveLayers[Index].Source.Get() == &Source) { ReleaseLayer(Index); }
    }
    for (int32 Index = LiveGhosts.Num() - 1; Index >= 0; --Index)
    {
        if (LiveGhosts[Index].Owner.Get() == &Source) { ReleaseGhost(Index); }
    }
}

void ULDImpactFxSubsystem::Ghost(const USkeletalMeshComponent& Source, const LDHitVfx::FGhosts& Ghosts, const ULDHumanoidVisualComponent& Owner)
{
    if (!FApp::CanEverRender() || !Ghosts.bShow || !Owner.CanShowImpact() || !Source.GetSkinnedAsset() || !LoadLayerAssets()) { return; }
    if (LiveGhosts.Num() >= MaxGhosts) { ReleaseGhost(0); }
    int32 Slot = INDEX_NONE;
    if (!FreeGhosts.IsEmpty()) { Slot = FreeGhosts.Pop(EAllowShrinking::No); }
    else
    {
        auto* Copy = NewObject<UPoseableMeshComponent>(LayerHost, NAME_None, RF_Transient);
        Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Copy->SetGenerateOverlapEvents(false);
        Copy->SetCanEverAffectNavigation(false);
        Copy->SetCastShadow(false);
        Copy->bReceivesDecals = false;
        Copy->SetUsingAbsoluteLocation(true);
        Copy->SetUsingAbsoluteRotation(true);
        Copy->SetUsingAbsoluteScale(true);
        Copy->SetHiddenInGame(true);
        Copy->RegisterComponent();
        Slot = GhostSlots.Add(Copy);
        GhostPaints.Add(UMaterialInstanceDynamic::Create(GhostMaterial, this));
    }
    UPoseableMeshComponent* Copy = GhostSlots[Slot];
    UMaterialInstanceDynamic* Paint = GhostPaints[Slot];
    if (!IsValid(Copy) || !Paint) { return; }
    if (Copy->GetSkinnedAsset() != Source.GetSkinnedAsset()) { Copy->SetSkinnedAssetAndUpdate(Source.GetSkinnedAsset()); }
    for (int32 Index = 0; Index < Copy->GetNumMaterials(); ++Index) { Copy->SetMaterial(Index, Paint); }
    Copy->SetWorldTransform(Source.GetComponentTransform());
    // The engine reads the source pose only; its parameter is not const.
    Copy->CopyPoseFromSkeletalComponent(const_cast<USkeletalMeshComponent*>(&Source));
    Paint->SetVectorParameterValue(LDHitVfx::GlowParameter, Ghosts.Glow);
    Paint->SetScalarParameterValue(LDHitVfx::FadeParameter, LDHitVfx::GhostFade(Ghosts, 0.f));
    Copy->SetHiddenInGame(false);
    FLiveGhost& Added = LiveGhosts.AddDefaulted_GetRef();
    Added.Owner = &Owner;
    Added.Slot = Slot;
    Added.Look = Ghosts;
}

void ULDImpactFxSubsystem::ReleaseGhost(int32 Index)
{
    const int32 Slot = LiveGhosts[Index].Slot;
    if (GhostSlots.IsValidIndex(Slot) && IsValid(GhostSlots[Slot]))
    {
        GhostSlots[Slot]->SetHiddenInGame(true);
        FreeGhosts.Add(Slot);
    }
    LiveGhosts.RemoveAt(Index, EAllowShrinking::No);
}

float ULDImpactFxSubsystem::OverdriveHoldSeconds()
{
    if (!bContentTried)
    {
        bContentTried = true;
        FString Error;
        if (!LDDemoEntry::LoadRuntimeContent(Content, Error))
        {
            Content.Reset();
            UE_LOG(LogLDContent, Error, TEXT("LD_OVERDRIVE_FX content=unavailable %s"), *Error);
        }
    }
    return Content ? float(Content->Get().Parameters.Overdrive.HoldMs) / 1000.f : 0.f;
}

void ULDImpactFxSubsystem::ApplyZone(UNiagaraComponent& Effect, const LDHitVfx::FZone& Zone, float RadiusCm, const FVector& Velocity)
{
    Effect.SetVariableFloat(TEXT("User.Radius"), RadiusCm);
    Effect.SetVariableVec3(TEXT("User.Velocity"), Velocity);
    Effect.SetVariableFloat(TEXT("User.Lifetime"), Zone.Lifetime);
    Effect.SetVariableFloat(TEXT("User.SparkRate"), Zone.Rate);
    Effect.SetVariableFloat(TEXT("User.Scale"), Zone.Scale);
    Effect.SetVariableLinearColor(TEXT("User.Colour"), Zone.Colour);
}

void ULDImpactFxSubsystem::ClearForActor(const AActor& Owner)
{
    for (int32 Index = Live.Num() - 1; Index >= 0; --Index)
    {
        if (Live[Index].OwnerActor.Get() == &Owner) { Release(Index); }
    }
}

bool ULDImpactFxSubsystem::LoadLayerAssets()
{
    if (bLayerAssetsTried) { return bLayerAssetsReady; }
    bLayerAssetsTried = true;
    auto LoadPackage = [](const FString& Package) -> UObject*
    { return LoadObject<UObject>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package))); };
    PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    LayerMaterial = Cast<UMaterialInterface>(LoadPackage(LDHitVfx::LayerMaterial));
    ShellMaterial = Cast<UMaterialInterface>(LoadPackage(LDHitVfx::ShellMaterial));
    GhostMaterial = Cast<UMaterialInterface>(LoadPackage(LDHitVfx::GhostMaterial));
    Masks.Reset();
    FString Missing;
    for (int32 Mask = 0; Mask < int32(LDHitVfx::EMask::Count); ++Mask)
    {
        const FString Package = LDHitVfx::TexturePackage(LDHitVfx::MaskId(LDHitVfx::EMask(Mask)));
        UTexture2D* Texture = Cast<UTexture2D>(LoadPackage(Package));
        if (!Texture) { Missing += Package + TEXT(" "); }
        Masks.Add(Texture);
    }
    ScreenEdge = Masks[int32(LDHitVfx::EMask::ScreenEdge)];
    if (!PlaneMesh || !SphereMesh) { Missing += TEXT("/Engine/BasicShapes "); }
    if (!LayerMaterial) { Missing += FString(LDHitVfx::LayerMaterial) + TEXT(" "); }
    if (!ShellMaterial) { Missing += FString(LDHitVfx::ShellMaterial) + TEXT(" "); }
    if (!GhostMaterial) { Missing += FString(LDHitVfx::GhostMaterial) + TEXT(" "); }
    if (!Missing.IsEmpty())
    {
        UE_LOG(LogLDContent, Error, TEXT("LD_HIT_LAYERS asset=missing %s(run Scripts\\ImportDemoArt.ps1 -Mode ImportVFX)"), *Missing);
        return false;
    }
    FActorSpawnParameters Parameters;
    Parameters.ObjectFlags |= RF_Transient;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    LayerHost = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
    if (!LayerHost)
    {
        UE_LOG(LogLDContent, Error, TEXT("LD_HIT_LAYERS host=spawn_failed"));
        return false;
    }
    LayerHost->SetActorEnableCollision(false);
    bLayerAssetsReady = true;
    return true;
}

void ULDImpactFxSubsystem::Configure(UStaticMeshComponent& Mesh, bool bShell)
{
    Mesh.SetStaticMesh(bShell ? SphereMesh.Get() : PlaneMesh.Get());
    Mesh.CreateDynamicMaterialInstance(0, bShell ? ShellMaterial.Get() : LayerMaterial.Get());
}

int32 ULDImpactFxSubsystem::TakeSlot(bool bShell)
{
    // An idle slot of the same kind first; then an idle slot of the other kind, re-meshed, so the pool never holds
    // more than MaxLayers components whatever the mix of hits.
    int32 Other = INDEX_NONE;
    for (int32 Index = FreeSlots.Num() - 1; Index >= 0; --Index)
    {
        const int32 Slot = FreeSlots[Index];
        if (!IsValid(Slots[Slot])) { continue; }
        if (SlotShell[Slot] == bShell)
        {
            FreeSlots.RemoveAtSwap(Index, EAllowShrinking::No);
            return Slot;
        }
        if (Other == INDEX_NONE) { Other = Index; }
    }
    if (Other != INDEX_NONE && Slots.Num() >= MaxLayers)
    {
        const int32 Slot = FreeSlots[Other];
        FreeSlots.RemoveAtSwap(Other, EAllowShrinking::No);
        Configure(*Slots[Slot], bShell);
        SlotShell[Slot] = bShell;
        return Slot;
    }
    auto* Mesh = NewObject<UStaticMeshComponent>(LayerHost, NAME_None, RF_Transient);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCanEverAffectNavigation(false);
    Mesh->SetCastShadow(false);
    Mesh->bReceivesDecals = false;
    Mesh->SetUsingAbsoluteLocation(true);
    Mesh->SetUsingAbsoluteRotation(true);
    Mesh->SetUsingAbsoluteScale(true);
    Mesh->SetHiddenInGame(true);
    Mesh->RegisterComponent();
    Configure(*Mesh, bShell);
    SlotShell.Add(bShell);
    return Slots.Add(Mesh);
}

void ULDImpactFxSubsystem::Layers(const ULDHumanoidVisualComponent* Source, const FVector& Chest, const FVector& Feet,
    const LDHitVfx::FRecipe& Recipe, float AgeSeconds)
{
    if (!FApp::CanEverRender() || Recipe.Layers.IsEmpty() || Chest.ContainsNaN() || Feet.ContainsNaN() || !FMath::IsFinite(AgeSeconds)
        || (Source && !Source->CanShowImpact()) || !LoadLayerAssets())
    { return; }
    for (const LDHitVfx::FLayer& Layer : Recipe.Layers)
    {
        if (LiveLayers.Num() >= MaxLayers) { ReleaseLayer(0); }
        const int32 Slot = TakeSlot(Layer.Place == LDHitVfx::EPlace::Shell);
        UStaticMeshComponent* Mesh = Slots[Slot];
        if (auto* Paint = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
        {
            Paint->SetTextureParameterValue(LDHitVfx::MaskParameter, Masks[int32(Layer.Mask)]);
            Paint->SetVectorParameterValue(LDHitVfx::GlowParameter, Layer.Glow);
            Paint->SetVectorParameterValue(LDHitVfx::InkParameter, Layer.Ink);
        }
        // Sprites draw after the ground marks and the shell so the flash stays on top.
        Mesh->SetTranslucentSortPriority(Layer.Place == LDHitVfx::EPlace::Ground ? 1 : Layer.Place == LDHitVfx::EPlace::Shell ? 2 : 3);
        FLiveLayer& Added = LiveLayers.AddDefaulted_GetRef();
        Added.Source = Source;
        Added.bBound = Source != nullptr;
        Added.Slot = Slot;
        Added.Layer = Layer;
        Added.Age = FMath::Max(AgeSeconds, 0.f);
        Added.At = Chest;
        if (Layer.Place == LDHitVfx::EPlace::Ground)
        {
            // Lay the mark on the supporting surface under the feet (slopes included), lifted along its normal.
            FHitResult Ground;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(LDHitLayerGround), false);
            Query.bTraceComplex = false;
            const bool bGround = GetWorld()->LineTraceSingleByObjectType(Ground, Feet + FVector(0, 0, 60.f), Feet - FVector(0, 0, 120.f),
                FCollisionObjectQueryParams(ECC_WorldStatic), Query);
            Added.Normal = bGround && Ground.ImpactNormal.Z > .3f ? FVector(Ground.ImpactNormal) : FVector::UpVector;
            Added.At = (bGround ? FVector(Ground.ImpactPoint) : Feet) + Added.Normal * 2.f;
        }
        PlaceLayer(Added, LDHitVfx::Brightness(Layer, Added.Age));
    }
}

void ULDImpactFxSubsystem::PlaceLayer(FLiveLayer& Shown, float Brightness)
{
    UStaticMeshComponent* Mesh = Slots.IsValidIndex(Shown.Slot) ? Slots[Shown.Slot].Get() : nullptr;
    if (!Mesh) { return; }
    const bool bVisible = Brightness > .002f;
    Mesh->SetHiddenInGame(!bVisible);
    if (!bVisible) { return; }
    if (auto* Paint = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0))) { Paint->SetScalarParameterValue(LDHitVfx::FadeParameter, Brightness); }
    const LDHitVfx::FLayer& Layer = Shown.Layer;
    float Size = Layer.SizeCm * LDHitVfx::ScaleAt(Layer, Shown.Age);
    const float Spin = FMath::DegreesToRadians(Layer.SpinDegPerSec * Shown.Age);
    const APlayerController* Viewer = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    const APlayerCameraManager* Camera = Viewer ? Viewer->PlayerCameraManager.Get() : nullptr;
    FVector Location = Shown.At;
    FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Shown.Normal) * FQuat(FVector::UpVector, Spin);
    FVector Scale = Layer.Place == LDHitVfx::EPlace::Shell ? FVector(Size / 100.f) : FVector(Size / 100.f, Size / 100.f, 1.f);
    if ((Layer.Place == LDHitVfx::EPlace::Sprite || Layer.Place == LDHitVfx::EPlace::Upright) && Camera)
    {
        const FVector Eye = Camera->GetCameraLocation();
        const FVector ToEye = (Eye - Shown.At).GetSafeNormal();
        if (!ToEye.IsNearlyZero())
        {
            // Pulled towards the camera so the flash draws over the body it strikes.
            Location = Shown.At + ToEye * 35.f;
            if (Layer.Place == LDHitVfx::EPlace::Sprite)
            {
                Size *= LDHitVfx::SpriteCompensation(FVector::Dist(Eye, Location), Camera->GetFOVAngle());
                Scale = FVector(Size / 100.f, Size / 100.f, 1.f);
                Rotation = FRotationMatrix::MakeFromZ(ToEye).ToQuat() * FQuat(FVector::UpVector, Spin);
            }
            else
            {
                // The strike stands upright: the texture's height runs along the plane's X, turned to world up.
                Rotation = FRotationMatrix::MakeFromZX(ToEye, FVector::UpVector).ToQuat();
                Scale = FVector(Size * 4.f / 100.f, Size / 100.f, 1.f);
                Location += FVector(0, 0, Size * 1.2f);
            }
        }
    }
    Mesh->SetWorldLocationAndRotation(Location, Rotation);
    Mesh->SetWorldScale3D(Scale);
}

void ULDImpactFxSubsystem::ReleaseLayer(int32 Index)
{
    const int32 Slot = LiveLayers[Index].Slot;
    if (Slots.IsValidIndex(Slot) && IsValid(Slots[Slot]))
    {
        Slots[Slot]->SetHiddenInGame(true);
        FreeSlots.Add(Slot);
    }
    LiveLayers.RemoveAt(Index, EAllowShrinking::No);
}

void ULDImpactFxSubsystem::FlashScreen(const LDHitVfx::FScreenFlash& InFlash)
{
    if (InFlash.Seconds <= 0.f || !GetWorld()) { return; }
    // The edge mask may be needed before any visible hit has loaded the layer assets.
    LoadLayerAssets();
    Flash = InFlash;
    FlashAt = GetWorld()->GetRealTimeSeconds();
}

float ULDImpactFxSubsystem::ScreenFlashAlpha(FLinearColor& OutColour) const
{
    if (FlashAt < 0. || !GetWorld()) { return 0.f; }
    OutColour = Flash.Colour;
    return LDHitVfx::ScreenFlashAlpha(Flash, float(GetWorld()->GetRealTimeSeconds() - FlashAt));
}

float ULDImpactFxSubsystem::ScreenFringe() const
{
    FLinearColor Colour;
    return Flash.Colour.A > 0.f ? Flash.Fringe * ScreenFlashAlpha(Colour) / Flash.Colour.A : 0.f;
}

void ULDImpactFxSubsystem::Tick(float DeltaTime)
{
    const float Step = FMath::IsFinite(DeltaTime) ? FMath::Max(DeltaTime, 0.f) : 0.f;
    if (!Warming.IsEmpty())
    {
        WarmSeconds += Step;
        if (WarmSeconds >= 1.f)
        {
            for (const TWeakObjectPtr<UNiagaraComponent>& Effect : Warming) { if (UNiagaraComponent* Warmed = Effect.Get()) { Warmed->DestroyComponent(); } }
            Warming.Reset();
        }
    }
    for (int32 Index = Live.Num() - 1; Index >= 0; --Index)
    {
        FLive& Burst = Live[Index];
        const ULDHumanoidVisualComponent* Source = Burst.Source.Get();
        const AActor* OwnerActor = Burst.OwnerActor.Get();
        UNiagaraComponent* Effect = Burst.Effect.Get();
        const bool bOwnerGone = Burst.bBound && (Burst.OwnerActor.IsExplicitlyNull()
            ? (!Source || !Source->CanShowImpact()) : (!OwnerActor || OwnerActor->IsActorBeingDestroyed()));
        if (bOwnerGone || !Effect)
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
    for (int32 Index = LiveLayers.Num() - 1; Index >= 0; --Index)
    {
        FLiveLayer& Shown = LiveLayers[Index];
        const ULDHumanoidVisualComponent* Source = Shown.Source.Get();
        Shown.Age += Step;
        if ((Shown.bBound && (!Source || !Source->CanShowImpact())) || Shown.Age >= Shown.Layer.DelaySeconds + Shown.Layer.Seconds)
        {
            ReleaseLayer(Index);
            continue;
        }
        PlaceLayer(Shown, LDHitVfx::Brightness(Shown.Layer, Shown.Age));
    }
    for (int32 Index = LiveGhosts.Num() - 1; Index >= 0; --Index)
    {
        FLiveGhost& Ghost = LiveGhosts[Index];
        const ULDHumanoidVisualComponent* Owner = Ghost.Owner.Get();
        Ghost.Age += Step;
        const float Fade = LDHitVfx::GhostFade(Ghost.Look, Ghost.Age);
        if (!Owner || !Owner->CanShowImpact() || Fade <= 0.f)
        {
            ReleaseGhost(Index);
            continue;
        }
        if (GhostPaints.IsValidIndex(Ghost.Slot) && GhostPaints[Ghost.Slot]) { GhostPaints[Ghost.Slot]->SetScalarParameterValue(LDHitVfx::FadeParameter, Fade); }
    }
}

LDHitVfx::FLayerTuning ULDImpactFxSubsystem::LayerTuning() const
{
    return LDHitVfx::TuningOf(LDHitFeel::Read(), Catalog ? Catalog->LayerSize : 1.f, Catalog ? Catalog->LayerSeconds : 1.f,
        Catalog ? Catalog->LayerGlow : 1.f);
}

float ULDImpactFxSubsystem::LocalEffectScale() const
{
    // The world's own controller list: a world without a game instance (a test fixture) has no engine world context.
    const UWorld* World = GetWorld();
    if (!World) { return 1.f; }
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const ALDPlayerController* Local = Cast<ALDPlayerController>(It->Get());
        if (Local && Local->IsLocalController()) { return Local->Controls && Local->Controls->bReduceCombatEffects ? .45f : 1.f; }
    }
    return 1.f;
}

UNiagaraComponent* ULDImpactFxSubsystem::ShowEffect(USceneComponent& Owner, UNiagaraComponent* Existing,
    ELDObservedProjectileLook Look, float RadiusCm, const FRotator& Heading)
{
    if (!FApp::CanEverRender()) { return Existing; }
    const float Thin = LocalEffectScale();
    const LDHitVfx::FTrail Trail = LDHitVfx::TrailOf(Look, Thin);
    const LDHitVfx::FZone Zone = LDHitVfx::ZoneOf(Look, RadiusCm, Thin);
    UNiagaraSystem* Wanted = Trail.bShow && Catalog ? Catalog->SwingTrail.Get() : Zone.bShow ? ZoneSystem.Get() : nullptr;
    if (IsValid(Existing) && (!Wanted || Existing->GetAsset() != Wanted))
    {
        Existing->DestroyComponent();
        Existing = nullptr;
    }
    if (!Wanted) { return nullptr; }
    UNiagaraComponent* Effect = Existing;
    if (!IsValid(Effect))
    {
        Effect = NewObject<UNiagaraComponent>(Owner.GetOwner(), NAME_None, RF_Transient);
        if (!Effect) { return nullptr; }
        Effect->SetAutoActivate(false);
        Effect->SetAsset(Wanted);
        Effect->SetupAttachment(&Owner);
        Effect->RegisterComponent();
    }
    if (Trail.bShow)
    {
        Effect->SetVariableFloat(TEXT("User.Width"), Trail.WidthCm);
        Effect->SetVariableFloat(TEXT("User.Lifetime"), Trail.Lifetime);
        Effect->SetVariableFloat(TEXT("User.SpawnRate"), Trail.Rate);
        Effect->SetVariableLinearColor(TEXT("User.Colour"), Trail.Colour);
    }
    else
    {
        // A cone streams outwards from its near third; every other zone fills its disc.
        const bool bCone = Look == ELDObservedProjectileLook::SpeechCone;
        Effect->SetRelativeLocation(FVector(bCone ? RadiusCm * .3f : 0.f, 0, 6.f));
        ApplyZone(*Effect, Zone, bCone ? RadiusCm * .25f : RadiusCm * .9f, Heading.RotateVector(Zone.Velocity));
    }
    if (!Effect->IsActive()) { Effect->Activate(true); }
    return Effect;
}

UNiagaraComponent* ULDImpactFxSubsystem::ShowZone(USceneComponent& Owner, UNiagaraComponent* Existing, const LDHitVfx::FZone& Zone,
    float RadiusCm, const FVector& Velocity, const FVector& Offset)
{
    if (!FApp::CanEverRender() || !ZoneSystem || !Zone.bShow)
    {
        if (IsValid(Existing)) { Existing->DestroyComponent(); }
        return nullptr;
    }
    UNiagaraComponent* Effect = Existing;
    if (!IsValid(Effect) || Effect->GetAsset() != ZoneSystem)
    {
        if (IsValid(Effect)) { Effect->DestroyComponent(); }
        Effect = NewObject<UNiagaraComponent>(Owner.GetOwner(), NAME_None, RF_Transient);
        if (!Effect) { return nullptr; }
        Effect->SetAutoActivate(false);
        Effect->SetAsset(ZoneSystem);
        Effect->SetupAttachment(&Owner);
        Effect->RegisterComponent();
    }
    Effect->SetRelativeLocation(Offset + FVector(0, 0, 6.f));
    ApplyZone(*Effect, Zone, RadiusCm, Velocity);
    if (!Effect->IsActive()) { Effect->Activate(true); }
    return Effect;
}

void ULDImpactFxSubsystem::WarmUp()
{
    if (!FApp::CanEverRender() || !GetWorld()) { return; }
    // Textures, materials, meshes and the approved hold time load now rather than on the first hit.
    const bool bLayers = LoadLayerAssets();
    OverdriveHoldSeconds();
    if (bLayers)
    {
        FreeSlots.Add(TakeSlot(false));
        FreeSlots.Add(TakeSlot(true));
    }
    const FVector Under(0., 0., -20000.);
    TArray<UNiagaraSystem*> Systems{ImpactSystem.Get(), BurstSystem.Get(), ZoneSystem.Get()};
    if (Catalog) { Systems.Append({Catalog->SwingTrail.Get(), Catalog->CastCharge.Get()}); }
    for (UNiagaraSystem* System : Systems)
    {
        if (!System) { continue; }
        UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), System, Under, FRotator::ZeroRotator,
            FVector(1.f), false, true, ENCPoolMethod::None, false);
        if (!Effect) { continue; }
        Warming.Add(Effect);
        ++WarmedCount;
    }
    UE_LOG(LogLDContent, Display, TEXT("LD_VFX_WARMUP systems=%d layers=%d"), WarmedCount, bLayers ? 2 : 0);
}

void ULDImpactFxSubsystem::WarmGhost(const USkeletalMeshComponent& Source)
{
    const UObject* Asset = Source.GetSkinnedAsset();
    if (!FApp::CanEverRender() || !Asset || WarmedMeshes.Contains(Asset) || !LoadLayerAssets() || GhostSlots.Num() >= MaxGhosts) { return; }
    WarmedMeshes.Add(Asset);
    // One hidden copy with the ghost material: its PSOs precache at registration, and the slot serves the first dash.
    auto* Copy = NewObject<UPoseableMeshComponent>(LayerHost, NAME_None, RF_Transient);
    Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Copy->SetGenerateOverlapEvents(false);
    Copy->SetCanEverAffectNavigation(false);
    Copy->SetCastShadow(false);
    Copy->bReceivesDecals = false;
    Copy->SetUsingAbsoluteLocation(true);
    Copy->SetUsingAbsoluteRotation(true);
    Copy->SetUsingAbsoluteScale(true);
    Copy->SetHiddenInGame(true);
    Copy->RegisterComponent();
    UMaterialInstanceDynamic* Paint = UMaterialInstanceDynamic::Create(GhostMaterial, this);
    Copy->SetSkinnedAssetAndUpdate(Source.GetSkinnedAsset());
    for (int32 Index = 0; Index < Copy->GetNumMaterials(); ++Index) { Copy->SetMaterial(Index, Paint); }
    FreeGhosts.Add(GhostSlots.Add(Copy));
    GhostPaints.Add(Paint);
}
