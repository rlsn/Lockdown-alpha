#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LDCombatVfxCatalog.generated.h"

class UNiagaraSystem;

// Artist-editable presentation assets. Runtime parameters are supplied by the hit-feel controls.
UCLASS(BlueprintType)
class LOCKDOWN_API ULDCombatVfxCatalog : public UDataAsset
{
    GENERATED_BODY()
public:
    static constexpr const TCHAR* AssetPath = TEXT("/Game/Lockdown/Art/VFX/DA_CombatVfx.DA_CombatVfx");
    UPROPERTY(EditAnywhere, Category="Combat VFX") TObjectPtr<UNiagaraSystem> Impact;
    UPROPERTY(EditAnywhere, Category="Combat VFX") TObjectPtr<UNiagaraSystem> SwingTrail;
    UPROPERTY(EditAnywhere, Category="Combat VFX") TObjectPtr<UNiagaraSystem> CastCharge;
    // The particle kit (LDHitVfx::FBurst / FZone): skill releases, element hits, launches, landings, dashes and falls.
    UPROPERTY(EditAnywhere, Category="Combat VFX") TObjectPtr<UNiagaraSystem> Burst;
    UPROPERTY(EditAnywhere, Category="Combat VFX") TObjectPtr<UNiagaraSystem> Zone;
    // Saved artist tuning of the shaped hit layers (LDHitVfx::FLayerTuning) and of the particle kit; the live ld.Feel.*
    // controls multiply these.
    UPROPERTY(EditAnywhere, Category="Hit Layers", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="3")) float LayerSize = 1.f;
    UPROPERTY(EditAnywhere, Category="Hit Layers", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="3")) float LayerSeconds = 1.f;
    UPROPERTY(EditAnywhere, Category="Hit Layers", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="3")) float LayerGlow = 1.f;
    UPROPERTY(EditAnywhere, Category="Particle Kit", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="3")) float KitCount = 1.f;
    UPROPERTY(EditAnywhere, Category="Particle Kit", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="3")) float KitSize = 1.f;
    static constexpr const TCHAR* DefaultBurst = TEXT("/Game/Lockdown/Art/VFX/NS_LDBurst.NS_LDBurst");
    static constexpr const TCHAR* DefaultZone = TEXT("/Game/Lockdown/Art/VFX/NS_LDZone.NS_LDZone");
    UPROPERTY(EditAnywhere, Category="Combat VFX") FLinearColor SwingTint = FLinearColor(1.f, .18f, .025f, .85f);
    UPROPERTY(EditAnywhere, Category="Combat VFX") FLinearColor CastTint = FLinearColor(.16f, .62f, 1.f, .8f);
};
