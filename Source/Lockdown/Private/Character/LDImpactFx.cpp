#include "Character/LDImpactFx.h"
#include "Character/LDCombatVfxCatalog.h"
#include "Character/LDHitFeel.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Core/LDLog.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
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
    while (!LiveLayers.IsEmpty()) { ReleaseLayer(LiveLayers.Num() - 1); }
    if (IsValid(LayerHost)) { LayerHost->Destroy(); }
    LayerHost = nullptr;
    Slots.Reset();
    SlotShell.Reset();
    FreeSlots.Reset();
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
    for (int32 Index = LiveLayers.Num() - 1; Index >= 0; --Index)
    {
        if (LiveLayers[Index].bBound && LiveLayers[Index].Source.Get() == &Source) { ReleaseLayer(Index); }
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

int32 ULDImpactFxSubsystem::TakeSlot(bool bShell)
{
    for (int32 Index = FreeSlots.Num() - 1; Index >= 0; --Index)
    {
        const int32 Slot = FreeSlots[Index];
        if (SlotShell[Slot] == bShell && IsValid(Slots[Slot]))
        {
            FreeSlots.RemoveAtSwap(Index, EAllowShrinking::No);
            return Slot;
        }
    }
    auto* Mesh = NewObject<UStaticMeshComponent>(LayerHost, NAME_None, RF_Transient);
    Mesh->SetStaticMesh(bShell ? SphereMesh.Get() : PlaneMesh.Get());
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
    Mesh->CreateDynamicMaterialInstance(0, bShell ? ShellMaterial.Get() : LayerMaterial.Get());
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
        Added.At = Layer.Place == LDHitVfx::EPlace::Ground ? Feet + FVector(0, 0, 2.f) : Chest;
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
    FQuat Rotation(FVector::UpVector, Spin);
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
                Size = LDHitVfx::SpriteCm(Size, FVector::Dist(Eye, Location), Camera->GetFOVAngle());
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
}
