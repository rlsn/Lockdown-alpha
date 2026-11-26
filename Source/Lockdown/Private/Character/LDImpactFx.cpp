#include "Character/LDImpactFx.h"
#include "Combat/LDCombatElements.h"
#include "Character/LDCombatVfxCatalog.h"
#include "Character/LDHitFeel.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Core/LDLog.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDDemoEntry.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/LDPlayerController.h"
#include "UI/LDControlSettings.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/UObjectIterator.h"

namespace LDImpactFxDetail
{
    constexpr float MaxWaitSeconds = .5f;
    // The generated systems' user parameters, named once rather than on every call.
    const FName UserStart(TEXT("User.Start")), UserEnd(TEXT("User.End")), UserTint(TEXT("User.Tint")), UserWidth(TEXT("User.Width")),
        UserIntensity(TEXT("User.Intensity")), UserDuration(TEXT("User.Duration")), UserFocus(TEXT("User.Focus"));
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
    BurstSystem = Catalog && Catalog->Burst ? Catalog->Burst.Get() : LoadObject<UNiagaraSystem>(nullptr, ULDCombatVfxCatalog::DefaultBurst);
    ZoneSystem = Catalog && Catalog->Zone ? Catalog->Zone.Get() : LoadObject<UNiagaraSystem>(nullptr, ULDCombatVfxCatalog::DefaultZone);
    GeneratedSystems.SetNum(int32(LDSliceVfx::ESystem::Count));
    for (int32 System = 0; System < GeneratedSystems.Num(); ++System)
    {
        const FString Package = LDSliceVfx::SystemPackage(LDSliceVfx::ESystem(System));
        GeneratedSystems[System] = LoadObject<UNiagaraSystem>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package)));
    }
    BodyGlowMaterial = LoadObject<UMaterialInterface>(nullptr, *(FString(LDSliceVfx::BodyGlowMaterial) + TEXT(".") + FPackageName::GetShortName(LDSliceVfx::BodyGlowMaterial)));
    if (!BodyGlowMaterial) { UE_LOG(LogLDContent, Error, TEXT("LD_CHAIN_FX asset=missing %s (run Scripts\\ImportDemoArt.ps1 -Mode ImportVFX)"), LDSliceVfx::BodyGlowMaterial); }
#if WITH_EDITOR
    // Start editor-only Niagara compilation during world startup rather than on the first swing.
    if (Catalog && Catalog->SwingTrail) { Catalog->SwingTrail->RequestCompile(false); }
    if (BurstSystem) { BurstSystem->RequestCompile(false); }
    if (ZoneSystem) { ZoneSystem->RequestCompile(false); }
    for (UNiagaraSystem* Generated : GeneratedSystems) { if (Generated) { Generated->RequestCompile(false); } }
    // The rim shell first draws on a struck body; finish its shaders now rather than skip its first hits.
    if (BodyGlowMaterial && BodyGlowMaterial->GetMaterial())
    {
        if (FMaterialResource* Resource = BodyGlowMaterial->GetMaterial()->GetMaterialResource(GMaxRHIShaderPlatform)) { Resource->FinishCompilation(); }
    }
#endif
    WarmUp();
    if (!BurstSystem || !ZoneSystem)
    {
        UE_LOG(LogLDContent, Error, TEXT("LD_PARTICLE_KIT asset=missing burst=%s zone=%s"),
            BurstSystem ? TEXT("ok") : ULDCombatVfxCatalog::DefaultBurst, ZoneSystem ? TEXT("ok") : ULDCombatVfxCatalog::DefaultZone);
    }
    for (int32 System = 0; System < GeneratedSystems.Num(); ++System)
    {
        if (!GeneratedSystems[System])
        {
            UE_LOG(LogLDContent, Error, TEXT("LD_GENERATED_FX asset=missing %s (run Scripts\\ImportDemoArt.ps1 -Mode ImportVFX)"), *LDSliceVfx::SystemPackage(LDSliceVfx::ESystem(System)));
        }
    }
    if (Catalog && !Catalog->SwingTrail)
    {
        UE_LOG(LogLDContent, Error, TEXT("LD_SWING_FX asset=missing catalog=%s"), ULDCombatVfxCatalog::AssetPath);
    }
}

void ULDImpactFxSubsystem::Deinitialize()
{
    while (!Live.IsEmpty()) { Release(Live.Num() - 1); }
    while (!LiveLayers.IsEmpty()) { ReleaseLayer(LiveLayers.Num() - 1); }
    while (!LiveSlices.IsEmpty()) { ReleaseSlice(LiveSlices.Num() - 1); }
    Strikes.Reset();
    while (!LiveGhosts.IsEmpty()) { ReleaseGhost(LiveGhosts.Num() - 1); }
    GhostSlots.Reset();
    GhostPaints.Reset();
    ShellPaints.Reset();
    FreeGhosts.Reset();
    if (IsValid(LayerHost)) { LayerHost->Destroy(); }
    LayerHost = nullptr;
    Slots.Reset();
    SlotKinds.Reset();
    FreeSlots.Reset();
    BurstSystem = nullptr;
    ZoneSystem = nullptr;
    GeneratedSystems.Reset();
    Catalog = nullptr;
    Super::Deinitialize();
}

TStatId ULDImpactFxSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(ULDImpactFxSubsystem, STATGROUP_Tickables);
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
    MakeRoom();
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

void ULDImpactFxSubsystem::MakeRoom()
{
    int32 Short = 0, Oldest = INDEX_NONE;
    for (int32 Index = 0; Index < Live.Num(); ++Index)
    {
        if (Live[Index].Ground != 0) { continue; }
        if (Oldest == INDEX_NONE) { Oldest = Index; }
        ++Short;
    }
    if (Short >= LDImpactFx::MaxBursts) { Release(Oldest); }
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
    for (int32 Index = LiveSlices.Num() - 1; Index >= 0; --Index)
    {
        if (LiveSlices[Index].bBound && LiveSlices[Index].Source.Get() == &Source) { ReleaseSlice(Index); }
    }
    for (int32 Index = LiveGhosts.Num() - 1; Index >= 0; --Index)
    {
        if (LiveGhosts[Index].Owner.Get() == &Source) { ReleaseGhost(Index); }
    }
    // A hidden or fallen body's strikes no longer give a later hit its tier.
    Strikes.RemoveAll([&Source](const FStrike& Strike) { return Strike.Source.Get() == &Source; });
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
        // A pooled copy ticks only while it is shown; registration activated its tick.
        Copy->SetComponentTickEnabled(false);
        Slot = GhostSlots.Add(Copy);
        GhostPaints.Add(UMaterialInstanceDynamic::Create(GhostMaterial, this));
    }
    UPoseableMeshComponent* Copy = GhostSlots[Slot];
    UMaterialInstanceDynamic* Paint = PaintOf(Slot, Ghosts.bShell);
    if (!IsValid(Copy) || !Paint) { return; }
    if (Copy->GetSkinnedAsset() != Source.GetSkinnedAsset()) { Copy->SetSkinnedAssetAndUpdate(Source.GetSkinnedAsset()); }
    for (int32 Index = 0; Index < Copy->GetNumMaterials(); ++Index) { Copy->SetMaterial(Index, Paint); }
    Copy->SetWorldTransform(Source.GetComponentTransform());
    // The engine reads the source pose only; its parameter is not const.
    Copy->CopyPoseFromSkeletalComponent(const_cast<USkeletalMeshComponent*>(&Source));
    Paint->SetVectorParameterValue(LDHitVfx::GlowParameter, Ghosts.Glow);
    Paint->SetScalarParameterValue(LDHitVfx::FadeParameter, LDHitVfx::GhostFade(Ghosts, 0.f));
    Copy->SetHiddenInGame(false);
    Copy->SetComponentTickEnabled(true);
    FLiveGhost& Added = LiveGhosts.AddDefaulted_GetRef();
    Added.Owner = &Owner;
    Added.Source = &Source;
    Added.Slot = Slot;
    Added.Look = Ghosts;
}

void ULDImpactFxSubsystem::ReleaseGhost(int32 Index)
{
    const int32 Slot = LiveGhosts[Index].Slot;
    if (GhostSlots.IsValidIndex(Slot) && IsValid(GhostSlots[Slot]))
    {
        GhostSlots[Slot]->SetHiddenInGame(true);
        GhostSlots[Slot]->SetComponentTickEnabled(false);
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
    for (int32 Index = LiveSlices.Num() - 1; Index >= 0; --Index)
    {
        if (LiveSlices[Index].OwnerActor.Get() == &Owner) { ReleaseSlice(Index); }
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

void ULDImpactFxSubsystem::Configure(UStaticMeshComponent& Mesh, uint8 Kind)
{
    UStaticMesh* Shape = Kind == SlotPlane ? PlaneMesh.Get() : Kind == SlotShell ? SphereMesh.Get() : Kind == SlotQuad ? SliceQuad.Get()
        : SlashMeshes.IsValidIndex(Kind - SlotSlash) ? SlashMeshes[Kind - SlotSlash].Get() : nullptr;
    UMaterialInterface* Paint = Kind == SlotPlane ? LayerMaterial.Get() : Kind == SlotShell ? ShellMaterial.Get() : SliceMaterial.Get();
    Mesh.SetStaticMesh(Shape);
    Mesh.CreateDynamicMaterialInstance(0, Paint);
}

int32 ULDImpactFxSubsystem::TakeSlot(uint8 Kind)
{
    // An idle slot of the same kind first; then an idle slot of another kind, re-meshed, so the pool never holds
    // more than MaxLayers components whatever the mix of effects.
    int32 Other = INDEX_NONE;
    for (int32 Index = FreeSlots.Num() - 1; Index >= 0; --Index)
    {
        const int32 Slot = FreeSlots[Index];
        if (!IsValid(Slots[Slot])) { continue; }
        if (SlotKinds[Slot] == Kind)
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
        Configure(*Slots[Slot], Kind);
        SlotKinds[Slot] = Kind;
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
    Configure(*Mesh, Kind);
    SlotKinds.Add(Kind);
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
        if (LiveLayers.Num() + LiveSlices.Num() >= MaxLayers) { LiveLayers.IsEmpty() ? ReleaseSlice(0) : ReleaseLayer(0); }
        const int32 Slot = TakeSlot(Layer.Place == LDHitVfx::EPlace::Shell ? SlotShell : SlotPlane);
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
    if (FlashAt >= 0. && !LDHitVfx::ScreenFlashReplaces(Flash, float(GetWorld()->GetRealTimeSeconds() - FlashAt), InFlash)) { return; }
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
    // Fields due to reach for the bodies in them; their hops are spawned after this pass, which they would reorder.
    TArray<TWeakObjectPtr<UNiagaraComponent>, TInlineAllocator<4>> Biting;
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
        else
        {
            Burst.Age += Step;
            if (Burst.StopAfter >= 0.f && Burst.Age + Burst.InitialAge >= Burst.StopAfter && Effect->IsActive()) { Effect->Deactivate(); }
            if (IsField(Effect) && Burst.Age >= Burst.NextBite && Effect->IsActive())
            {
                // Others' fields reach less often (decoration).
                const float Focus = Effect->GetOverrideParameters().GetParameterValue<float>(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Focus")));
                Burst.NextBite = Burst.Age + (Focus > .5f ? .4f : .8f);
                Biting.Add(Effect);
            }
        }
    }
    for (const TWeakObjectPtr<UNiagaraComponent>& Field : Biting) { if (const UNiagaraComponent* Shown = Field.Get()) { Bite(*Shown); } }
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
    for (int32 Index = LiveSlices.Num() - 1; Index >= 0; --Index)
    {
        FLiveSlice& Shown = LiveSlices[Index];
        Shown.Age += Step;
        const ULDHumanoidVisualComponent* Source = Shown.Source.Get();
        const bool bGone = Shown.bBound && (Shown.OwnerActor.IsExplicitlyNull() ? (!Source || !Source->CanShowImpact())
            : (!Shown.OwnerActor.IsValid() || Shown.OwnerActor->IsActorBeingDestroyed()));
        if (bGone || LDSliceVfx::IsOver(Shown.Layer, Shown.Age))
        {
            ReleaseSlice(Index);
            continue;
        }
        PlaceSlice(Shown);
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
        if (UMaterialInstanceDynamic* Paint = PaintOf(Ghost.Slot, Ghost.Look.bShell)) { Paint->SetScalarParameterValue(LDHitVfx::FadeParameter, Fade); }
        const USkeletalMeshComponent* Body = Ghost.Look.bShell ? Ghost.Source.Get() : nullptr;
        if (Body && GhostSlots.IsValidIndex(Ghost.Slot) && IsValid(GhostSlots[Ghost.Slot]))
        {
            GhostSlots[Ghost.Slot]->SetWorldTransform(Body->GetComponentTransform());
            // The engine reads the source pose only; its parameter is not const.
            GhostSlots[Ghost.Slot]->CopyPoseFromSkeletalComponent(const_cast<USkeletalMeshComponent*>(Body));
        }
    }
}

UMaterialInstanceDynamic* ULDImpactFxSubsystem::PaintOf(int32 Slot, bool bShell)
{
    if (!bShell) { return GhostPaints.IsValidIndex(Slot) ? GhostPaints[Slot].Get() : nullptr; }
    if (!BodyGlowMaterial || Slot < 0) { return nullptr; }
    if (ShellPaints.Num() <= Slot) { ShellPaints.SetNum(Slot + 1); }
    if (!ShellPaints[Slot]) { ShellPaints[Slot] = UMaterialInstanceDynamic::Create(BodyGlowMaterial, this); }
    return ShellPaints[Slot];
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
    FLinearColor Ink;
    if (const LDSliceVfx::ESystem Designed = LDSliceVfx::EffectSystemOf(uint8(Look), Ink); Designed != LDSliceVfx::ESystem::Count)
    {
        // The look's own looping system at the proxy (an area on the floor under it), following it through MoveEffect.
        if (Thin <= 0.f)
        {
            if (IsValid(Existing)) { Existing->DestroyComponent(); }
            return nullptr;
        }
        const bool bArea = Designed != LDSliceVfx::ESystem::FlightChalk && Designed != LDSliceVfx::ESystem::FlightSock && Designed != LDSliceVfx::ESystem::FlightFist
            && Designed != LDSliceVfx::ESystem::WindZone;
        const FVector At = bArea ? FloorUnder(Owner.GetComponentLocation()) : Owner.GetComponentLocation();
        return ShowLoop(Owner, Existing, Designed, At, At + Heading.Vector() * 100., Ink, FMath::Max(RadiusCm, 40.f), 2.2f * FMath::Clamp(Thin, .45f, 1.f), 1.e4f, true);
    }
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
        Effect->SetVariableFloat(LDImpactFxDetail::UserWidth, Trail.WidthCm);
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

void ULDImpactFxSubsystem::MoveEffect(UNiagaraComponent* Effect, const FVector& At, const FRotator& Heading) const
{
    if (!IsValid(Effect) || At.ContainsNaN() || !GeneratedSystems.Contains(Effect->GetAsset())) { return; }
    const bool bArea = IsGround(Effect) || Effect->GetAsset() == GetSystem(LDSliceVfx::ESystem::AreaPlain);
    const FVector Start = bArea ? FloorUnder(At) : At;
    Effect->SetVariableVec3(LDImpactFxDetail::UserStart, Start);
    Effect->SetVariableVec3(LDImpactFxDetail::UserEnd, Start + Heading.Vector() * 100.);
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
    // The frontend has no fights; warming every system there only froze each return to the title screen for seconds.
    if (UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) == LDDemoEntry::FrontendMap) { return; }
    // Textures, materials, meshes and the approved hold time load now rather than on the first hit.
    const bool bLayers = LoadLayerAssets();
    OverdriveHoldSeconds();
    if (bLayers)
    {
        FreeSlots.Add(TakeSlot(SlotPlane));
        FreeSlots.Add(TakeSlot(SlotShell));
    }
    // One slot of each slice shape: their PSOs precache at registration, before the first swing or skill.
    const bool bSlices = bLayers && LoadSliceAssets();
    if (bSlices)
    {
        for (uint8 Kind = SlotQuad; Kind < SlotSlash + uint8(LDSliceVfx::SlashMeshCount); ++Kind) { FreeSlots.Add(TakeSlot(Kind)); }
    }
    const FVector Under(0., 0., -20000.);
    TArray<UNiagaraSystem*> Systems{BurstSystem.Get(), ZoneSystem.Get()};
    for (UNiagaraSystem* Generated : GeneratedSystems) { if (Generated) { Systems.Add(Generated); } }
    if (Catalog) { Systems.Add(Catalog->SwingTrail.Get()); }
    for (UNiagaraSystem* System : Systems)
    {
        if (!System) { continue; }
        UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), System, Under, FRotator::ZeroRotator,
            FVector(1.f), false, true, ENCPoolMethod::None, false);
        if (!Effect) { continue; }
        Warming.Add(Effect);
        ++WarmedCount;
    }
    UE_LOG(LogLDContent, Display, TEXT("LD_VFX_WARMUP systems=%d layers=%d slices=%d"), WarmedCount, bLayers ? 2 : 0,
        bSlices ? 1 + LDSliceVfx::SlashMeshCount : 0);
}

void ULDImpactFxSubsystem::WarmGhost(const USkeletalMeshComponent& Source)
{
    const UObject* Asset = Source.GetSkinnedAsset();
    Bodies.AddUnique(&Source);
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
    Copy->SetComponentTickEnabled(false);
    UMaterialInstanceDynamic* Paint = UMaterialInstanceDynamic::Create(GhostMaterial, this);
    Copy->SetSkinnedAssetAndUpdate(Source.GetSkinnedAsset());
    for (int32 Index = 0; Index < Copy->GetNumMaterials(); ++Index) { Copy->SetMaterial(Index, Paint); }
    FreeGhosts.Add(GhostSlots.Add(Copy));
    GhostPaints.Add(Paint);
}

UStaticMesh* ULDImpactFxSubsystem::PropMesh(LDSliceVfx::EMesh Mesh)
{
    return FApp::CanEverRender() && LoadSliceAssets() && SlashMeshes.IsValidIndex(int32(Mesh)) ? SlashMeshes[int32(Mesh)].Get() : nullptr;
}

UPoseableMeshComponent* ULDImpactFxSubsystem::StandIn(AActor& Host, const FVector& At, const FLinearColor& Glow, float ReachCm)
{
    if (!FApp::CanEverRender() || At.ContainsNaN() || !Host.GetRootComponent() || !LoadLayerAssets() || !GhostMaterial) { return nullptr; }
    const USkeletalMeshComponent* Near = nullptr;
    double Best = FMath::Square(double(ReachCm));
    for (int32 Index = Bodies.Num() - 1; Index >= 0; --Index)
    {
        const USkeletalMeshComponent* Body = Bodies[Index].Get();
        if (!Body) { Bodies.RemoveAtSwap(Index); continue; }
        if (Body->GetWorld() != GetWorld() || !Body->GetSkinnedAsset() || !Body->GetOwner() || Body->GetOwner()->IsHidden()) { continue; }
        const double Far = FVector::DistSquared2D(Body->GetComponentLocation(), At);
        if (Far < Best) { Best = Far; Near = Body; }
    }
    if (!Near) { return nullptr; }
    auto* Copy = NewObject<UPoseableMeshComponent>(&Host, NAME_None, RF_Transient);
    Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Copy->SetGenerateOverlapEvents(false);
    Copy->SetCanEverAffectNavigation(false);
    Copy->SetCastShadow(false);
    Copy->bReceivesDecals = false;
    Copy->SetupAttachment(Host.GetRootComponent());
    Copy->RegisterComponent();
    Copy->SetSkinnedAssetAndUpdate(Near->GetSkinnedAsset());
    UMaterialInstanceDynamic* Paint = UMaterialInstanceDynamic::Create(GhostMaterial, Copy);
    Paint->SetVectorParameterValue(LDHitVfx::GlowParameter, Glow);
    Paint->SetScalarParameterValue(LDHitVfx::FadeParameter, .8f);
    for (int32 Index = 0; Index < Copy->GetNumMaterials(); ++Index) { Copy->SetMaterial(Index, Paint); }
    // The body's own height over its floor, at the stand-in's place.
    FTransform Pose = Near->GetComponentTransform();
    const FVector From = Near->GetComponentLocation();
    Pose.SetLocation(FVector(At.X, At.Y, FloorUnder(At).Z + From.Z - FloorUnder(From).Z));
    Copy->SetWorldTransform(Pose);
    // The engine reads the source pose only; its parameter is not const.
    Copy->CopyPoseFromSkeletalComponent(const_cast<USkeletalMeshComponent*>(Near));
    return Copy;
}

bool ULDImpactFxSubsystem::LoadSliceAssets()
{
    if (bSliceAssetsTried) { return bSliceAssetsReady; }
    bSliceAssetsTried = true;
    auto LoadPackage = [](const FString& Package) -> UObject*
    { return LoadObject<UObject>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package))); };
    FString Missing;
    SliceMaterial = Cast<UMaterialInterface>(LoadPackage(LDSliceVfx::Material));
    if (!SliceMaterial) { Missing += FString(LDSliceVfx::Material) + TEXT(" "); }
    SliceQuad = Cast<UStaticMesh>(LoadPackage(LDSliceVfx::QuadMesh));
    if (!SliceQuad) { Missing += FString(LDSliceVfx::QuadMesh) + TEXT(" "); }
    SlashMeshes.Reset();
    for (int32 Mesh = 0; Mesh < int32(LDSliceVfx::EMesh::Count); ++Mesh)
    {
        const FString Package = LDSliceVfx::MeshPackage(LDSliceVfx::EMesh(Mesh));
        UStaticMesh* Loaded = Cast<UStaticMesh>(LoadPackage(Package));
        if (!Loaded) { Missing += Package + TEXT(" "); }
        SlashMeshes.Add(Loaded);
    }
    SliceTextures.Reset();
    for (int32 Tex = 0; Tex < int32(LDSliceVfx::ETex::Count); ++Tex)
    {
        const FString Package = LDSliceVfx::TexturePackage(LDSliceVfx::ETex(Tex));
        UTexture2D* Loaded = Cast<UTexture2D>(LoadPackage(Package));
        if (!Loaded) { Missing += Package + TEXT(" "); }
        SliceTextures.Add(Loaded);
    }
    if (!Missing.IsEmpty())
    {
        UE_LOG(LogLDContent, Error, TEXT("LD_SLICE_VFX asset=missing %s(run Scripts\\ImportDemoArt.ps1 -Mode ImportVFX)"), *Missing);
        return false;
    }
    bSliceAssetsReady = true;
    return true;
}

FVector ULDImpactFxSubsystem::GroundUnder(const FVector& At, FVector& OutNormal) const
{
    FHitResult Ground;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(LDSliceGround), false);
    const bool bGround = GetWorld() && GetWorld()->LineTraceSingleByObjectType(Ground, At + FVector(0, 0, 60.f), At - FVector(0, 0, 160.f),
        FCollisionObjectQueryParams(ECC_WorldStatic), Query);
    OutNormal = bGround && Ground.ImpactNormal.Z > .3f ? FVector(Ground.ImpactNormal) : FVector::UpVector;
    return bGround ? FVector(Ground.ImpactPoint) : At;
}

void ULDImpactFxSubsystem::Slice(const ULDHumanoidVisualComponent* Source, const LDSliceVfx::FRecipe& Recipe, float AgeSeconds)
{
    if (!FApp::CanEverRender() || Recipe.Layers.IsEmpty() || !FMath::IsFinite(AgeSeconds) || (Source && !Source->CanShowImpact())
        || !LoadLayerAssets() || !LoadSliceAssets())
    { return; }
    using LDSliceVfx::EPlace;
    for (const LDSliceVfx::FLayer& Layer : Recipe.Layers)
    {
        if (Layer.At.ContainsNaN() || Layer.End.ContainsNaN() || Layer.Dir.ContainsNaN() || LDSliceVfx::IsOver(Layer, FMath::Max(AgeSeconds, 0.f))) { continue; }
        if (LiveLayers.Num() + LiveSlices.Num() >= MaxLayers) { LiveSlices.IsEmpty() ? ReleaseLayer(0) : ReleaseSlice(0); }
        const bool bMesh = Layer.Place == EPlace::Slash || Layer.Place == EPlace::GroundSlash;
        const uint8 Kind = bMesh ? uint8(SlotSlash + uint8(Layer.Mesh)) : uint8(SlotQuad);
        if (bMesh && !SlashMeshes.IsValidIndex(int32(Layer.Mesh))) { continue; }
        const int32 Slot = TakeSlot(Kind);
        UStaticMeshComponent* Mesh = Slots[Slot];
        if (auto* Paint = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
        {
            Paint->SetTextureParameterValue(LDSliceVfx::MaskParameter, SliceTextures[int32(Layer.Tex)]);
            Paint->SetTextureParameterValue(LDSliceVfx::NoiseParameter, SliceTextures[int32(LDSliceVfx::ETex::DissolveNoise)]);
            Paint->SetVectorParameterValue(LDSliceVfx::GlowParameter, Layer.Glow);
            Paint->SetVectorParameterValue(LDSliceVfx::InkParameter, Layer.Ink);
            Paint->SetVectorParameterValue(LDSliceVfx::CoreParameter, Layer.Core);

        }
        // Ground marks first, then the slash, then sprites and strips; the tier's burst on top.
        const bool bGround = Layer.Place == EPlace::Flat || Layer.Place == EPlace::GroundStrip || Layer.Place == EPlace::GroundSlash;
        Mesh->SetTranslucentSortPriority(bGround ? 1 : Layer.Place == EPlace::Slash ? 2 : Layer.bTop ? 4 : 3);
        FLiveSlice& Added = LiveSlices.AddDefaulted_GetRef();
        Added.Source = Source;
        Added.bBound = Source != nullptr;
        Added.Slot = Slot;
        Added.Layer = Layer;
        Added.Age = FMath::Max(AgeSeconds, 0.f);
        if (bGround) { Added.Ground = GroundUnder(Layer.At, Added.Normal); }
        PlaceSlice(Added);
    }
}

void ULDImpactFxSubsystem::ClearTelegraphs(const ULDHumanoidVisualComponent& Source)
{
    for (int32 Index = Live.Num() - 1; Index >= 0; --Index)
    {
        if (Live[Index].bTelegraph && Live[Index].Source.Get() == &Source) { Release(Index); }
    }
    for (int32 Index = LiveSlices.Num() - 1; Index >= 0; --Index)
    {
        if (LiveSlices[Index].Layer.bTelegraph && LiveSlices[Index].Source.Get() == &Source) { ReleaseSlice(Index); }
    }
}

void ULDImpactFxSubsystem::ReleaseSlice(int32 Index)
{
    const int32 Slot = LiveSlices[Index].Slot;
    if (Slots.IsValidIndex(Slot) && IsValid(Slots[Slot]))
    {
        Slots[Slot]->SetHiddenInGame(true);
        FreeSlots.Add(Slot);
    }
    LiveSlices.RemoveAt(Index, EAllowShrinking::No);
}

void ULDImpactFxSubsystem::PlaceSlice(FLiveSlice& Shown)
{
    using LDSliceVfx::EPlace;
    UStaticMeshComponent* Mesh = Slots.IsValidIndex(Shown.Slot) ? Slots[Shown.Slot].Get() : nullptr;
    if (!Mesh) { return; }
    const LDSliceVfx::FLayer& Layer = Shown.Layer;
    const LDSliceVfx::FState State = LDSliceVfx::Evaluate(Layer, Shown.Age);
    const float Fade = State.Fade * FMath::Clamp(Shown.ExternalAlpha, 0.f, 1.f);
    const bool bVisible = State.bVisible && Fade > .002f;
    Mesh->SetHiddenInGame(!bVisible);
    if (!bVisible) { return; }
    const APlayerController* Viewer = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    const APlayerCameraManager* Camera = Viewer ? Viewer->PlayerCameraManager.Get() : nullptr;
    const FVector Eye = Camera ? Camera->GetCameraLocation() : Layer.At + FVector(0, 0, 1000.f);
    const float Size = Layer.SizeCm * State.Scale;
    FVector Location = Layer.At;
    FQuat Rotation = FQuat::Identity;
    FVector Scale(Size / 100.f, Size / 100.f, 1.f);
    float TileU = 1.f;
    const FVector Planar = FVector(Layer.Dir.X, Layer.Dir.Y, 0.).GetSafeNormal();
    const FQuat Yaw = FRotationMatrix::MakeFromX(Planar.IsNearlyZero() ? FVector::ForwardVector : Planar).ToQuat();
    switch (Layer.Place)
    {
    case EPlace::Facing:
    {
        FVector ToEye = (Eye - Layer.At).GetSafeNormal();
        if (ToEye.IsNearlyZero()) { ToEye = FVector::UpVector; }
        // +U follows the strike as seen on screen: its direction projected onto the sprite plane (else screen right).
        FVector Along = Layer.Dir - ToEye * FVector::DotProduct(Layer.Dir, ToEye);
        if (Along.SizeSquared() < 1e-4) { Along = Camera ? Camera->GetCameraRotation().Quaternion().GetRightVector() : FVector::RightVector; }
        Along.Normalize();
        const float Drawn = Size * (Camera ? LDHitVfx::SpriteCompensation(FVector::Dist(Eye, Layer.At), Camera->GetFOVAngle()) : 1.f);
        Scale = FVector(Drawn / 100.f, Drawn / 100.f, 1.f);
        // Pulled towards the camera so it draws over the body, and shifted so the hit point sits on At.
        Location = Layer.At + ToEye * 35.f + Along * (.5f - Layer.Pivot) * Drawn;
        Rotation = FRotationMatrix::MakeFromZX(ToEye, Along).ToQuat();
        break;
    }
    case EPlace::Flat:
        Location = Shown.Ground + Shown.Normal * 2.5f;
        Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Shown.Normal) * Yaw;
        break;
    case EPlace::Strip:
    case EPlace::GroundStrip:
    {
        const bool bOnGround = Layer.Place == EPlace::GroundStrip;
        const FVector Lift = bOnGround ? FVector(0, 0, Shown.Ground.Z - Layer.At.Z + 3.f) : FVector::ZeroVector;
        const FVector From = Layer.At + Lift, To = Layer.End + Lift;
        const FVector Axis = To - From;
        const float Length = FMath::Max(float(Axis.Size()), 1.f);
        const FVector X = Axis / Length;
        FVector Face = bOnGround ? FVector::UpVector : (Eye - (From + To) * .5f).GetSafeNormal();
        if (FMath::Abs(FVector::DotProduct(Face, X)) > .98f) { Face = FVector::UpVector; }
        Location = (From + To) * .5f;
        Rotation = FRotationMatrix::MakeFromXZ(X, Face).ToQuat();
        Scale = FVector(Length / 100.f, Size / 100.f, 1.f);
        TileU = Layer.TileCm > 0.f ? Length / Layer.TileCm : 1.f;
        break;
    }
    case EPlace::Slash:
        Rotation = Yaw;
        Scale = FVector(Size / 100.f);
        break;
    case EPlace::GroundSlash:
        Location = Shown.Ground + Shown.Normal * 3.f;
        Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Shown.Normal) * Yaw;
        Scale = FVector(Size / 100.f);
        break;
    }
    if (auto* Paint = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
    {
        const bool bFlip = LDSliceVfx::IsFlipbook(Layer.Tex);
        Paint->SetScalarParameterValue(LDSliceVfx::FadeParameter, Fade);
        Paint->SetVectorParameterValue(LDSliceVfx::FlipParameter, FLinearColor(bFlip ? 4.f : 1.f, bFlip ? 4.f : float(FMath::Max(Layer.Variants, 1)),
            State.Frame, TileU));
        Paint->SetVectorParameterValue(LDSliceVfx::ShapeParameter, FLinearColor(State.WindowLo, State.WindowHi, State.Dissolve, State.Scroll));
    }
    Mesh->SetWorldLocationAndRotation(Location, Rotation);
    Mesh->SetWorldScale3D(Scale);
}

LDSliceVfx::FLook ULDImpactFxSubsystem::SliceLook(ESliceTuning Kind, bool bOwn, bool bFocus) const
{
    LDSliceVfx::FLook Look;
    Look.bOwn = bOwn;
    Look.bFocus = bFocus || bOwn;

    Look.EffectScale = LocalEffectScale();
    const LDHitFeel::FTuning Feel = LDHitFeel::Read();
    const auto Ratio = [](float Value, const TCHAR* Field)
    {
        for (const LDHitFeel::FField& Each : LDHitFeel::Fields())
        {
            if (FCString::Strcmp(Each.ConsoleName, Field) == 0) { return Each.Default > 0.f ? Value / Each.Default : 0.f; }
        }
        return 1.f;
    };
    switch (Kind)
    {
    case ESliceTuning::Impact:
    {
        const LDHitVfx::FLayerTuning Tuning = LayerTuning();
        Look.Size = Tuning.Size;
        Look.Seconds = Tuning.Seconds;
        Look.Glow = Tuning.Glow;
        break;
    }
    case ESliceTuning::Swing:
        // ld.Feel.TrailWidth and TrailReach both size the slash; TrailMs times it; TrailGlow lights it.
        Look.Size = Ratio(Feel.TrailWidthCm, TEXT("ld.Feel.TrailWidth")) * Ratio(Feel.TrailReach, TEXT("ld.Feel.TrailReach"))
            * (Catalog ? Catalog->SwingSize : 1.f);
        Look.Seconds = Ratio(Feel.TrailSeconds * 1000.f, TEXT("ld.Feel.TrailMs")) * (Catalog ? Catalog->SwingSeconds : 1.f);
        Look.Glow = Ratio(Feel.TrailGlow, TEXT("ld.Feel.TrailGlow"));
        break;
    case ESliceTuning::Telegraph:
        Look.Size = Ratio(Feel.CastScale, TEXT("ld.Feel.CastScale"));
        Look.Glow = Ratio(Feel.CastGlow, TEXT("ld.Feel.CastGlow"));
        break;
    }
    return Look;
}

void ULDImpactFxSubsystem::NoteStrike(const ULDHumanoidVisualComponent& Source, LDSliceVfx::EStrike Strike, const FVector& From, const FVector& Dir, bool bOwn,
    uint8 Ability)
{
    if (!GetWorld() || Strike == LDSliceVfx::EStrike::None || From.ContainsNaN() || Dir.ContainsNaN()) { return; }
    const double Now = GetWorld()->GetTimeSeconds();
    Strikes.RemoveAll([Now](const FStrike& Old) { return Now - Old.At > 1.5 || !Old.Source.IsValid(); });
    if (Strikes.Num() >= 32) { Strikes.RemoveAt(0); }
    FStrike& Added = Strikes.AddDefaulted_GetRef();
    Added.Source = &Source;
    Added.Strike = Strike;
    Added.From = Added.Last = From;
    Added.Dir = FVector(Dir.X, Dir.Y, 0.).GetSafeNormal();
    Added.At = Now;
    Added.bOwn = bOwn;
    Added.Ability = Ability;
}

ULDImpactFxSubsystem::FStrikeMatch ULDImpactFxSubsystem::MatchHit(const ULDHumanoidVisualComponent& Victim, const FVector& Chest,
    ELDObservedHitLook Look, float HitAgeSeconds)
{
    using LDSliceVfx::EStrike;
    FStrikeMatch Out;
    if (!GetWorld() || Chest.ContainsNaN() || !FMath::IsFinite(HitAgeSeconds)) { return Out; }
    const double HitAt = GetWorld()->GetTimeSeconds() - FMath::Max(HitAgeSeconds, 0.f);
    int32 Best = INDEX_NONE;
    double BestGap = TNumericLimits<double>::Max();
    for (int32 Index = 0; Index < Strikes.Num(); ++Index)
    {
        const FStrike& Strike = Strikes[Index];
        if (Strike.Source.Get() == &Victim || !Strike.Source.IsValid()) { continue; }
        const bool bFits = Look == ELDObservedHitLook::Water ? Strike.Strike == EStrike::Hose
            : Look == ELDObservedHitLook::Electric ? Strike.Strike == EStrike::Zap
            : Look == ELDObservedHitLook::Magic || Look == ELDObservedHitLook::Fire ? Strike.Strike == EStrike::Skill
            : Look == ELDObservedHitLook::None && (Strike.Strike == EStrike::Basic || Strike.Strike == EStrike::Finisher
                || (Strike.Strike == EStrike::Skill && IsPhysicalSkill(Strike.Ability)));
        if (!bFits) { continue; }
        const double Gap = HitAt - Strike.At;
        // A chain's later links land one interval after another (chain_bounce.interval) from the last body struck.
        // A skill's flight (a thrown shoe at 1650 cm/s over 850 cm) lands up to about half a second after its release.
        const double Late = Strike.Strike == EStrike::Zap ? .45 + .25 * Strike.Hits : Strike.Strike == EStrike::Skill ? .9 : .45;
        const double Reach = Strike.Strike == EStrike::Hose ? 900. : Strike.Strike == EStrike::Zap ? (Strike.Hits ? 600. : 950.)
            : Strike.Strike == EStrike::Skill ? 950. : 420.;
        const FVector Origin = Strike.Strike == EStrike::Zap ? Strike.Last : Strike.From;
        if (Gap < -.25 || Gap > Late || FVector::Dist2D(Origin, Chest) > Reach) { continue; }
        if (FMath::Abs(Gap) < BestGap)
        {
            BestGap = FMath::Abs(Gap);
            Best = Index;
        }
    }
    if (Best == INDEX_NONE) { return Out; }
    FStrike& Strike = Strikes[Best];
    Out.Strike = Strike.Strike;
    Out.bOwn = Strike.bOwn;
    Out.bChained = Strike.Strike == EStrike::Zap && Strike.Hits > 0;
    Out.Ability = Strike.Ability;
    Out.From = Strike.Strike == EStrike::Zap ? Strike.Last : Strike.From;
    const FVector Toward = FVector(Chest.X - Out.From.X, Chest.Y - Out.From.Y, 0.);
    Out.Dir = Toward.SizeSquared() > 1.f ? Toward.GetSafeNormal() : Strike.Dir;
    Strike.Last = Chest;
    ++Strike.Hits;
    return Out;
}

bool ULDImpactFxSubsystem::IsField(const UNiagaraComponent* Effect) const
{
    const UNiagaraSystem* Asset = Effect ? Effect->GetAsset() : nullptr;
    return Asset && Asset == GetSystem(LDSliceVfx::ESystem::Field);
}

bool ULDImpactFxSubsystem::IsGround(const UNiagaraComponent* Effect) const
{
    const UNiagaraSystem* Asset = Effect ? Effect->GetAsset() : nullptr;
    for (uint8 Kind = 1; Asset && Kind < LDGroundStateKindCount; ++Kind)
    {
        const LDSliceVfx::ESystem System = GroundSystem(Kind);
        if (System != LDSliceVfx::ESystem::Count && Asset == GetSystem(System)) { return true; }
    }
    return false;
}

LDSliceVfx::ESystem ULDImpactFxSubsystem::GroundSystem(uint8 Kind)
{
    switch (static_cast<ELDGroundStateKind>(Kind))
    {
    case ELDGroundStateKind::Field: return LDSliceVfx::ESystem::Field;
    case ELDGroundStateKind::Puddle: return LDSliceVfx::ESystem::PuddleGround;
    case ELDGroundStateKind::Ice: return LDSliceVfx::ESystem::IceGround;
    case ELDGroundStateKind::Fire: return LDSliceVfx::ESystem::FireGround;
    case ELDGroundStateKind::Miasma: return LDSliceVfx::ESystem::MiasmaGround;
    default: return LDSliceVfx::ESystem::Count;
    }
}

void ULDImpactFxSubsystem::ShowGround(const AActor& Owner, uint8 Kind, const FVector& Centre, float RadiusCm, float Alpha, uint8 OwnerSeat)
{
    const LDSliceVfx::ESystem System = GroundSystem(Kind);
    UNiagaraSystem* Asset = System == LDSliceVfx::ESystem::Count ? nullptr : GetSystem(System);
    int32 Found = Live.IndexOfByPredicate([&Owner](const FLive& Shown) { return Shown.Ground != 0 && Shown.OwnerActor.Get() == &Owner; });
    // A hidden owner shows nothing, like a state that ended.
    if (Found != INDEX_NONE && (!Asset || Live[Found].Ground != Kind || !Live[Found].Effect.IsValid() || Owner.IsHidden()))
    {
        Release(Found);
        Found = INDEX_NONE;
    }
    if (!FApp::CanEverRender() || !Asset || Owner.IsHidden() || !FMath::IsFinite(Alpha) || Alpha <= 0.f || !FMath::IsFinite(RadiusCm) || RadiusCm <= 0.f
        || Centre.ContainsNaN())
    {
        if (Found != INDEX_NONE) { Release(Found); }
        return;
    }
    // The reduced-effects setting dims a ground state like every other effect.
    const float Strength = 2.2f * FMath::Clamp(Alpha, 0.f, 1.f) * (.55f + .45f * LocalEffectScale());
    if (Found == INDEX_NONE)
    {
        // A ground state lives until its owner says otherwise: the system's own life is far beyond any state's. The local
        // player's own state (its seat is the own body's) shows its decoration in full; others' states thin it.
        bool bOwn = false;
        for (TObjectIterator<ULDHumanoidVisualComponent> It; It && !bOwn && OwnerSeat != MAX_uint8; ++It)
        { bOwn = It->GetWorld() == GetWorld() && It->IsOwnBody() && It->PaletteSlot() == OwnerSeat; }
        const uint8 Element = Kind == uint8(ELDGroundStateKind::Field) ? uint8(ELDCombatElement::Shock)
            : Kind == uint8(ELDGroundStateKind::Puddle) ? uint8(ELDCombatElement::Water) : Kind == uint8(ELDGroundStateKind::Ice) ? uint8(ELDCombatElement::Chill)
            : Kind == uint8(ELDGroundStateKind::Fire) ? uint8(ELDCombatElement::Fire) : uint8(ELDCombatElement::Rot);
        FVector Normal;
        const FVector Floor = GroundUnder(Centre, Normal);
        UNiagaraComponent* Effect = SpawnGenerated(System, Floor, Floor, LDSliceVfx::ElementInk(Element), RadiusCm, Strength, 1.e4f, bOwn);
        if (!Effect) { return; }
        Track(Effect, nullptr, 0.f, &Owner);
        FLive& Laid = Live.Last();
        Laid.Ground = Kind;
        Laid.GroundCentre = Centre;
        Laid.GroundRadius = RadiusCm;
        Laid.GroundStrength = Strength;
        return;
    }
    // Called every frame: the floor is traced and a parameter written only when its value changed.
    FLive& Shown = Live[Found];
    UNiagaraComponent* Effect = Shown.Effect.Get();
    if (!Shown.GroundCentre.Equals(Centre, 1.))
    {
        FVector Normal;
        Effect->SetVariableVec3(LDImpactFxDetail::UserStart, GroundUnder(Centre, Normal));
        Shown.GroundCentre = Centre;
    }
    if (Shown.GroundRadius != RadiusCm) { Effect->SetVariableFloat(LDImpactFxDetail::UserWidth, RadiusCm); Shown.GroundRadius = RadiusCm; }
    if (Shown.GroundStrength != Strength) { Effect->SetVariableFloat(LDImpactFxDetail::UserIntensity, Strength); Shown.GroundStrength = Strength; }
}

void ULDImpactFxSubsystem::FieldPulse(const ULDHumanoidVisualComponent& Source, const FVector& Feet, float RadiusCm, const LDSliceVfx::FLook& Look,
    float AgeSeconds)
{
    if (!Source.CanShowImpact() || !FMath::IsFinite(RadiusCm) || RadiusCm <= 0.f || !FMath::IsFinite(AgeSeconds) || AgeSeconds > .8f) { return; }
    FVector Normal;
    const FVector Floor = GroundUnder(Feet, Normal);
    const float Seconds = .8f * Look.Seconds;
    UNiagaraComponent* Effect = SpawnGenerated(LDSliceVfx::ESystem::Field, Floor, Floor, LDSliceVfx::ElementInk(uint8(ELDCombatElement::Shock)), RadiusCm,
        2.2f * Look.Glow * (Look.bFocus ? 1.f : .7f) * (.55f + .45f * FMath::Clamp(Look.EffectScale, 0.f, 1.f)), Seconds, Look.bFocus);
    if (!Effect) { return; }
    Track(Effect, &Source, AgeSeconds);
    Live.Last().StopAfter = Seconds;
}

void ULDImpactFxSubsystem::Bite(const UNiagaraComponent& Field)
{
    // The field's arcs reach for up to three visible bodies standing in it: a short, reduced chain hop from the floor
    // nearest the body's side of the field to its knees, with the chain's own (others') body crawl and sparks.
    const FNiagaraParameterStore& Parameters = Field.GetOverrideParameters();
    const FVector Centre = FVector(Parameters.GetParameterValue<FVector3f>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.Start"))));
    const float Radius = Parameters.GetParameterValue<float>(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Width")));
    const float Strength = Parameters.GetParameterValue<float>(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Intensity")));
    const float Focus = Parameters.GetParameterValue<float>(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Focus")));
    if (!GetWorld() || Radius <= 0.f || Strength <= .2f) { return; }
    // Decoration: others' fields reach for one body at a time.
    const int32 Most = Focus > .5f ? 3 : 1;
    int32 Bitten = 0;
    for (TObjectIterator<ULDHumanoidVisualComponent> It; It && Bitten < Most; ++It)
    {
        const ULDHumanoidVisualComponent* Body = *It;
        const AActor* Owner = Body ? Body->GetOwner() : nullptr;
        if (!Owner || Body->GetWorld() != GetWorld() || !Body->CanShowImpact() || !Body->IsVisible()) { continue; }
        const FVector At = Owner->GetActorLocation();
        const FVector Out = FVector(At.X - Centre.X, At.Y - Centre.Y, 0.);
        if (Out.Size() > Radius * .95f) { continue; }
        const float Turn = FMath::FRandRange(-.6f, .6f);
        const FVector Side = (Out.IsNearlyZero() ? FVector::ForwardVector : Out.GetSafeNormal()).RotateAngleAxis(FMath::RadiansToDegrees(Turn), FVector::UpVector);
        LDSliceVfx::FLook Look = SliceLook(ESliceTuning::Impact, false, false);
        Look.Size *= .45f;
        Look.Glow *= FMath::Clamp(Strength / 2.2f, 0.f, 1.f);
        Chain(*Body, Centre + Side * Radius * .85f + FVector(0, 0, 10.f), At - FVector(0, 0, 45.f), true, Look, 0.f);
        ++Bitten;
    }
}

UNiagaraComponent* ULDImpactFxSubsystem::SpawnGenerated(LDSliceVfx::ESystem System, const FVector& Start, const FVector& End, const FLinearColor& Tint,
    float Width, float Intensity, float Seconds, bool bFocus)
{
    UNiagaraSystem* Asset = GetSystem(System);
    if (!FApp::CanEverRender() || !Asset || Start.ContainsNaN() || End.ContainsNaN() || !FMath::IsFinite(Width) || !FMath::IsFinite(Intensity)) { return nullptr; }
    MakeRoom();
    UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Asset, End, FRotator::ZeroRotator, FVector(1.f),
        false, false, ENCPoolMethod::ManualRelease, false);
    if (!Effect)
    {
        UE_LOG(LogLDContent, Warning, TEXT("LD_GENERATED_FX spawn=failed %s"), LDSliceVfx::SystemName(System));
        return nullptr;
    }
    Effect->SetVariableVec3(LDImpactFxDetail::UserStart, Start);
    Effect->SetVariableVec3(LDImpactFxDetail::UserEnd, End);
    Effect->SetVariableLinearColor(LDImpactFxDetail::UserTint, Tint);
    Effect->SetVariableFloat(LDImpactFxDetail::UserWidth, Width);
    Effect->SetVariableFloat(LDImpactFxDetail::UserIntensity, Intensity);
    Effect->SetVariableFloat(LDImpactFxDetail::UserDuration, Seconds);
    Effect->SetVariableFloat(LDImpactFxDetail::UserFocus, bFocus ? 1.f : 0.f);
    Effect->Activate(true);
    return Effect;
}

void ULDImpactFxSubsystem::SwingTell(const ULDHumanoidVisualComponent& Owner, uint8 Step, const FVector& Feet, const FVector& Facing,
    float WindupSeconds, const LDSliceVfx::FLook& Look, float AgeSeconds)
{
    // Too short a windup to read (or one already past) draws nothing rather than a flash.
    if (Step == 0 || !Owner.CanShowImpact() || !FMath::IsFinite(WindupSeconds) || WindupSeconds < .08f || !FMath::IsFinite(AgeSeconds)
        || AgeSeconds >= WindupSeconds || Look.Size <= 0.f || Look.EffectScale <= 0.f) { return; }
    const LDSliceVfx::ESystem System = Step >= 3 ? LDSliceVfx::ESystem::Tell3 : Step == 2 ? LDSliceVfx::ESystem::Tell2 : LDSliceVfx::ESystem::Tell1;
    const FVector Ahead = FVector(Facing.X, Facing.Y, 0.).GetSafeNormal();
    FVector Normal;
    const FVector Floor = GroundUnder(Feet, Normal);
    UNiagaraComponent* Effect = SpawnGenerated(System, Floor, Floor + (Ahead.IsNearlyZero() ? FVector::ForwardVector : Ahead) * 100.f,
        LDSliceVfx::StrikeTint(Look), 70.f * Look.Size, 2.2f * Look.Glow * (Look.bFocus ? 1.f : .7f), WindupSeconds, Look.bFocus);
    if (!Effect) { return; }
    Track(Effect, &Owner, AgeSeconds);
    Live.Last().bTelegraph = true;
    Live.Last().StopAfter = WindupSeconds;
}

void ULDImpactFxSubsystem::Swing(const ULDHumanoidVisualComponent& Owner, uint8 Step, const FVector& Centre, const FVector& Facing,
    const LDSliceVfx::FLook& Look, float AgeSeconds)
{
    if (Step == 0 || !Owner.CanShowImpact() || !FMath::IsFinite(AgeSeconds) || Look.Size <= 0.f || Look.EffectScale <= 0.f) { return; }
    const LDSliceVfx::ESystem System = Step >= 3 ? LDSliceVfx::ESystem::Swing3 : Step == 2 ? LDSliceVfx::ESystem::Swing2 : LDSliceVfx::ESystem::Swing1;
    const FVector Ahead = FVector(Facing.X, Facing.Y, 0.).GetSafeNormal();
    UNiagaraComponent* Effect = SpawnGenerated(System, Centre, Centre + (Ahead.IsNearlyZero() ? FVector::ForwardVector : Ahead) * 100.f,
        LDSliceVfx::StrikeTint(Look), 70.f * Look.Size, 2.2f * Look.Glow * (Look.bFocus ? 1.f : .7f), .4f * Look.Seconds, Look.bFocus);
    if (!Effect) { return; }
    Track(Effect, &Owner, AgeSeconds);
    // Its ribbons spawn once; stopping the emitters lets the system complete when they have faded.
    Live.Last().StopAfter = .4f * Look.Seconds;
}

void ULDImpactFxSubsystem::Hit(const ULDHumanoidVisualComponent& Victim, const FVector& From, const FVector& Chest, bool bFinisher,
    const LDSliceVfx::FLook& Look, float AgeSeconds)
{
    if (!Victim.CanShowImpact() || !FMath::IsFinite(AgeSeconds) || Look.Size <= 0.f || Look.EffectScale <= 0.f) { return; }
    UNiagaraComponent* Effect = SpawnGenerated(bFinisher ? LDSliceVfx::ESystem::HitFinisher : LDSliceVfx::ESystem::HitBasic, From, Chest,
        LDSliceVfx::StrikeTint(Look), 70.f * Look.Size, 2.2f * Look.Glow * (Look.bFocus ? 1.f : .7f), .5f * Look.Seconds, Look.bFocus);
    if (!Effect) { return; }
    Track(Effect, &Victim, AgeSeconds);
    Live.Last().StopAfter = .5f * Look.Seconds;
}

UNiagaraComponent* ULDImpactFxSubsystem::Play(LDSliceVfx::ESystem System, const ULDHumanoidVisualComponent& Owner, const FVector& Start,
    const FVector& End, const FLinearColor& Tint, float Width, float Seconds, const LDSliceVfx::FLook& Look, float AgeSeconds, bool bTelegraph)
{
    if (!FApp::CanEverRender() || !Owner.CanShowImpact() || Start.ContainsNaN() || End.ContainsNaN() || !FMath::IsFinite(AgeSeconds)
        || !FMath::IsFinite(Seconds) || Seconds <= 0.f || AgeSeconds >= Seconds || Look.Size <= 0.f || Look.Seconds <= 0.f || Look.Glow <= 0.f
        || Look.EffectScale <= 0.f) { return nullptr; }
    const float Thin = FMath::Clamp(Look.EffectScale, 0.f, 1.f);
    UNiagaraComponent* Effect = SpawnGenerated(System, Start, End, Tint, Width, 2.2f * Look.Glow * (Look.bFocus ? 1.f : .7f) * (.55f + .45f * Thin),
        Seconds, Look.bFocus);
    if (!Effect) { return nullptr; }
    Track(Effect, &Owner, AgeSeconds);
    Live.Last().bTelegraph = bTelegraph;
    Live.Last().StopAfter = Seconds;
    return Effect;
}

UNiagaraComponent* ULDImpactFxSubsystem::ShowLoop(USceneComponent& Owner, UNiagaraComponent* Existing, LDSliceVfx::ESystem System,
    const FVector& Start, const FVector& End, const FLinearColor& Tint, float Width, float Intensity, float Seconds, bool bFocus)
{
    UNiagaraSystem* Wanted = FApp::CanEverRender() && System != LDSliceVfx::ESystem::Count ? GetSystem(System) : nullptr;
    if (IsValid(Existing) && (!Wanted || Existing->GetAsset() != Wanted))
    {
        Existing->DestroyComponent();
        Existing = nullptr;
    }
    if (!Wanted || !Owner.GetOwner()) { return nullptr; }
    UNiagaraComponent* Effect = Existing;
    if (!IsValid(Effect))
    {
        Effect = NewObject<UNiagaraComponent>(Owner.GetOwner(), NAME_None, RF_Transient);
        if (!Effect) { return nullptr; }
        Effect->SetAutoActivate(false);
        Effect->SetAsset(Wanted);
        // The parameters are world positions; the component only follows Owner for culling and lifetime.
        Effect->SetAbsolute(true, true, true);
        Effect->SetupAttachment(&Owner);
        Effect->RegisterComponent();
    }
    Effect->SetVariableVec3(LDImpactFxDetail::UserStart, Start);
    Effect->SetVariableVec3(LDImpactFxDetail::UserEnd, End);
    Effect->SetVariableLinearColor(LDImpactFxDetail::UserTint, Tint);
    Effect->SetVariableFloat(LDImpactFxDetail::UserWidth, Width);
    Effect->SetVariableFloat(LDImpactFxDetail::UserIntensity, Intensity);
    Effect->SetVariableFloat(LDImpactFxDetail::UserDuration, Seconds);
    Effect->SetVariableFloat(LDImpactFxDetail::UserFocus, bFocus ? 1.f : 0.f);
    if (!Effect->IsActive()) { Effect->Activate(true); }
    return Effect;
}

UNiagaraComponent* ULDImpactFxSubsystem::PlayFree(LDSliceVfx::ESystem System, const FVector& Start, const FVector& End, const FLinearColor& Tint,
    float Width, float Seconds, const LDSliceVfx::FLook& Look)
{
    if (!FApp::CanEverRender() || Start.ContainsNaN() || End.ContainsNaN() || !FMath::IsFinite(Seconds) || Seconds <= 0.f || Look.Size <= 0.f
        || Look.Glow <= 0.f || Look.EffectScale <= 0.f) { return nullptr; }
    const float Thin = FMath::Clamp(Look.EffectScale, 0.f, 1.f);
    UNiagaraComponent* Effect = SpawnGenerated(System, Start, End, Tint, Width, 2.2f * Look.Glow * (Look.bFocus ? 1.f : .7f) * (.55f + .45f * Thin), Seconds, Look.bFocus);
    if (!Effect) { return nullptr; }
    Track(Effect, nullptr, 0.f);
    Live.Last().StopAfter = Seconds;
    return Effect;
}

UNiagaraComponent* ULDImpactFxSubsystem::PlayPack(LDSliceVfx::EPack Pack, const FVector& At, float Scale, const FRotator& Rotation)
{
    const int32 Index = int32(Pack);
    if (!FApp::CanEverRender() || Index < 0 || Index >= int32(LDSliceVfx::EPack::Count) || At.ContainsNaN() || !FMath::IsFinite(Scale) || Scale <= 0.f) { return nullptr; }
    if (!bPackTried[Index])
    {
        bPackTried[Index] = true;
        PackSystems[Index] = LoadObject<UNiagaraSystem>(nullptr, LDSliceVfx::PackPath(Pack), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!PackSystems[Index]) { UE_LOG(LogLDContent, Warning, TEXT("LD_PACK_FX missing %s; its layer is not shown"), LDSliceVfx::PackPath(Pack)); }
        else
        {
            FString Names;
            for (const FNiagaraEmitterHandle& Handle : PackSystems[Index]->GetEmitterHandles()) { Names += Handle.GetName().ToString() + TEXT(" "); }
            UE_LOG(LogLDContent, Display, TEXT("LD_PACK_FX loaded %s emitters=[%s]"), LDSliceVfx::PackPath(Pack), *Names);
        }
    }
    const float Strength = FMath::Clamp(LocalEffectScale(), 0.f, 1.f);
    if (!PackSystems[Index] || Strength <= 0.f) { return nullptr; }
    UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), PackSystems[Index], At, Rotation, FVector(Scale * (.6f + .4f * Strength)),
        true, false, ENCPoolMethod::AutoRelease, false);
    if (!Effect) { return nullptr; }
    // Layers of the shipped system that don't fit the project's look are switched off by name; the rest plays as shipped.
    // (The instance exists once activated; an emitter disabled before its first tick never spawns.)
    Effect->Activate(true);
    for (const TCHAR* Muted : LDSliceVfx::PackMutedEmitters(Pack)) { Effect->SetEmitterEnable(Muted, false); }
    return Effect;
}

bool ULDImpactFxSubsystem::SkillReach(uint8 Ability, float& OutRangeCm, float& OutHalfWidthCm)
{
    OutRangeCm = OutHalfWidthCm = 0.f;
    OverdriveHoldSeconds();
    if (!Content) { return false; }
    const auto Cm = [](const FLDDemoRational& Value) { return Value.Denominator > 0 ? float(double(Value.Numerator) / double(Value.Denominator)) : 0.f; };
    for (const FLDDemoAbilityDefinition& Definition : Content->Get().Abilities)
    {
        if (Definition.NativeId != Ability) { continue; }
        // The level-1 target: a viewer never knows another contender's rank, so every body shows the approved base reach.
        OutRangeCm = Cm(Definition.Target.RangeCm);
        OutHalfWidthCm = Cm(Definition.Target.LineHalfWidthCm);
        return OutRangeCm > 0.f;
    }
    return false;
}

float ULDImpactFxSubsystem::SkillDashCm(uint8 Ability)
{
    OverdriveHoldSeconds();
    if (!Content || Ability == 0) { return 0.f; }
    for (const FLDDemoAbilityDefinition& Definition : Content->Get().Abilities)
    {
        if (Definition.NativeId != Ability) { continue; }
        for (const FLDDemoEffectDefinition& Effect : Definition.Values.Effects)
        {
            if (Effect.Opcode == uint16(ELDDemoEffectOp::Dash) && Effect.DistanceCm.Denominator > 0) { return float(double(Effect.DistanceCm.Numerator) / double(Effect.DistanceCm.Denominator)); }
        }
    }
    return 0.f;
}

bool ULDImpactFxSubsystem::IsElementalSkill(uint8 Ability)
{
    OverdriveHoldSeconds();
    if (!Content || Ability == 0) { return false; }
    const FLDDemoAbilityDefinition* Definition = Content->Get().Abilities.FindByPredicate([Ability](const FLDDemoAbilityDefinition& Row) { return Row.NativeId == Ability; });
    return Definition && Definition->Element != ELDDemoSkillElement::None;
}

bool ULDImpactFxSubsystem::IsSelfSkill(uint8 Ability)
{
    OverdriveHoldSeconds();
    if (!Content || Ability == 0) { return false; }
    const FLDDemoAbilityDefinition* Definition = Content->Get().Abilities.FindByPredicate([Ability](const FLDDemoAbilityDefinition& Row) { return Row.NativeId == Ability; });
    return Definition && Definition->Target.Mode == ELDDemoTargetMode::Self;
}

bool ULDImpactFxSubsystem::IsPhysicalSkill(uint8 Ability)
{
    OverdriveHoldSeconds();
    if (!Content || Ability == 0) { return false; }
    for (const FLDDemoAbilityDefinition& Definition : Content->Get().Abilities)
    {
        if (Definition.NativeId != Ability) { continue; }
        // Its own damage is physical (a physical hit is observed with no look).
        return Definition.Values.Effects.ContainsByPredicate([](const FLDDemoEffectDefinition& Effect)
            { return Effect.Opcode == uint16(ELDDemoEffectOp::Damage) && Effect.DamageType == ELDDemoDamageType::Physical; });
    }
    return false;
}

float ULDImpactFxSubsystem::SkillConeDegrees(uint8 Ability)
{
    OverdriveHoldSeconds();
    if (!Content) { return 0.f; }
    for (const FLDDemoAbilityDefinition& Definition : Content->Get().Abilities)
    {
        const FLDDemoRational& Angle = Definition.Target.AngleDegrees;
        if (Definition.NativeId == Ability && Angle.Denominator > 0) { return float(double(Angle.Numerator) / double(Angle.Denominator)); }
    }
    return 0.f;
}

float ULDImpactFxSubsystem::SkillEffectRadius(uint8 Ability, uint16 Opcode)
{
    OverdriveHoldSeconds();
    if (!Content) { return 0.f; }
    for (const FLDDemoAbilityDefinition& Definition : Content->Get().Abilities)
    {
        if (Definition.NativeId != Ability) { continue; }
        for (const FLDDemoEffectDefinition& Effect : Definition.Values.Effects)
        {
            if (Effect.Opcode == Opcode && Effect.RadiusCm.Denominator > 0) { return float(double(Effect.RadiusCm.Numerator) / double(Effect.RadiusCm.Denominator)); }
        }
    }
    return 0.f;
}

float ULDImpactFxSubsystem::SkillSelfRadius(uint8 Ability)
{
    OverdriveHoldSeconds();
    if (!Content) { return 0.f; }
    for (const FLDDemoAbilityDefinition& Definition : Content->Get().Abilities)
    {
        if (Definition.NativeId != Ability) { continue; }
        for (const FLDDemoEffectDefinition& Effect : Definition.Values.Effects)
        {
            const ELDDemoEffectOp Op = static_cast<ELDDemoEffectOp>(Effect.Opcode);
            if ((Op == ELDDemoEffectOp::ParryStance || Op == ELDDemoEffectOp::Taunt || Op == ELDDemoEffectOp::SelfArea || Op == ELDDemoEffectOp::Brace)
                && Effect.RadiusCm.Denominator > 0) { return float(double(Effect.RadiusCm.Numerator) / double(Effect.RadiusCm.Denominator)); }
        }
    }
    return 0.f;
}

float ULDImpactFxSubsystem::SkillHoldSeconds(uint8 Ability)
{
    OverdriveHoldSeconds();
    if (!Content) { return 0.f; }
    int64 Longest = 0;
    for (const FLDDemoAbilityDefinition& Definition : Content->Get().Abilities)
    {
        if (Definition.NativeId != Ability) { continue; }
        for (const FLDDemoEffectDefinition& Effect : Definition.Values.Effects)
        {
            const ELDDemoEffectOp Op = static_cast<ELDDemoEffectOp>(Effect.Opcode);
            if (Op == ELDDemoEffectOp::ParryStance || Op == ELDDemoEffectOp::TimedStatBonus || Op == ELDDemoEffectOp::FrenzyWaves || Op == ELDDemoEffectOp::Brace)
            { Longest = FMath::Max(Longest, Effect.DurationMs); }
        }
    }
    return float(double(Longest) / 1000.);
}

float ULDImpactFxSubsystem::ReactionRadiusCm(uint8 Reaction)
{
    OverdriveHoldSeconds();
    if (!Content) { return 0.f; }
    const auto& Parameters = Content->Get().Parameters;
    const FLDDemoRational* Radius = Reaction == uint8(ELDCombatReaction::Fog) ? &Parameters.FogRadiusCm
        : Reaction == uint8(ELDCombatReaction::Overload) ? &Parameters.OverloadRadiusCm
        : Reaction == uint8(ELDCombatReaction::Methane) ? &Parameters.Elements.MethaneRadiusCm : nullptr;
    return Radius && Radius->Denominator > 0 ? float(double(Radius->Numerator) / double(Radius->Denominator)) : 0.f;
}

float ULDImpactFxSubsystem::FreezeRootSeconds()
{
    OverdriveHoldSeconds();
    return Content ? float(double(Content->Get().Parameters.Elements.FreezeRootMs) / 1000.) : 0.f;
}

float ULDImpactFxSubsystem::ConductRadiusCm()
{
    OverdriveHoldSeconds();
    if (!Content) { return 0.f; }
    const FLDDemoRational& Radius = Content->Get().Parameters.ConductRadiusCm;
    return Radius.Denominator > 0 ? float(double(Radius.Numerator) / double(Radius.Denominator)) : 0.f;
}

void ULDImpactFxSubsystem::Chain(const ULDHumanoidVisualComponent& Victim, const FVector& From, const FVector& To, bool bChained,
    const LDSliceVfx::FLook& Look, float AgeSeconds)
{
    if (!FApp::CanEverRender() || !Victim.CanShowImpact() || From.ContainsNaN() || To.ContainsNaN() || !FMath::IsFinite(AgeSeconds)) { return; }
    const float Thin = FMath::Clamp(Look.EffectScale, 0.f, 1.f);
    if (Thin <= 0.f || Look.Size <= 0.f || Look.Seconds <= 0.f || Look.Glow <= 0.f) { return; }
    // Others' links are thinner and dimmer, a bounce thinner than the first hop; reduced effects dim them further.
    const float Focus = Look.bFocus ? 1.f : .7f;
    const float Seconds = .26f * Look.Seconds;
    UNiagaraComponent* Effect = SpawnGenerated(LDSliceVfx::ESystem::Chain, From, To, LDSliceVfx::ElementInk(uint8(ELDCombatElement::Shock)),
        (bChained ? 56.f : 70.f) * Look.Size * (Look.bFocus ? 1.f : .8f), 2.2f * Look.Glow * Focus * (.55f + .45f * Thin), Seconds, Look.bFocus);
    if (!Effect) { return; }
    Track(Effect, &Victim, AgeSeconds);
    Live.Last().StopAfter = Seconds;
}
void ULDImpactFxSubsystem::ForEachGenerated(TFunctionRef<void(UNiagaraComponent&)> Visit) const
{
    for (const FLive& Shown : Live)
    {
        UNiagaraComponent* Effect = Shown.Effect.Get();
        if (Effect && GeneratedSystems.Contains(Effect->GetAsset())) { Visit(*Effect); }
    }
}