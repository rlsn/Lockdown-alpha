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
    // The ribbon of dashes and flights (the basic swing is the designed slash, LDSliceVfx).
    UPROPERTY(EditAnywhere, Category="Combat VFX") TObjectPtr<UNiagaraSystem> SwingTrail;
    UPROPERTY(EditAnywhere, Category="Combat VFX") TObjectPtr<UNiagaraSystem> CastCharge;
    // The particle kit (LDHitVfx::FBurst / FZone): skill releases, element hits, launches, landings, dashes and falls.
    UPROPERTY(EditAnywhere, Category="Combat VFX") TObjectPtr<UNiagaraSystem> Burst;
    UPROPERTY(EditAnywhere, Category="Combat VFX") TObjectPtr<UNiagaraSystem> Zone;
    // The Chain template (one link of a chain, tether or beam between User.Start and User.End; LDSliceVfx::Chain*):
    // built by the VFX import commandlet as NS_LDChain; active_zap draws each hop of its chain with it.
    UPROPERTY(EditAnywhere, Category="Combat VFX") TObjectPtr<UNiagaraSystem> Chain;
    // Saved artist tuning of the shaped hit layers (LDHitVfx::FLayerTuning) and of the particle kit; the live ld.Feel.*
    // controls multiply these.
    UPROPERTY(EditAnywhere, Category="Hit Layers", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="3")) float LayerSize = 1.f;
    UPROPERTY(EditAnywhere, Category="Hit Layers", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="3")) float LayerSeconds = 1.f;
    UPROPERTY(EditAnywhere, Category="Hit Layers", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="3")) float LayerGlow = 1.f;
    // Saved artist tuning of the designed basic swings (LDSliceVfx::SwingRelease, SwingTelegraph); the live ld.Feel.Trail*
    // controls multiply these. The slice hit bursts follow the Hit Layers tuning above.
    UPROPERTY(EditAnywhere, Category="Slice Swings", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="3")) float SwingSize = 1.f;
    UPROPERTY(EditAnywhere, Category="Slice Swings", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="3")) float SwingSeconds = 1.f;
    UPROPERTY(EditAnywhere, Category="Particle Kit", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="3")) float KitCount = 1.f;
    UPROPERTY(EditAnywhere, Category="Particle Kit", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="3")) float KitSize = 1.f;
    static constexpr const TCHAR* DefaultBurst = TEXT("/Game/Lockdown/Art/VFX/NS_LDBurst.NS_LDBurst");
    static constexpr const TCHAR* DefaultZone = TEXT("/Game/Lockdown/Art/VFX/NS_LDZone.NS_LDZone");
    static constexpr const TCHAR* DefaultChain = TEXT("/Game/Lockdown/Art/VFX/Niagara/NS_LDChain.NS_LDChain");
    UPROPERTY(EditAnywhere, Category="Combat VFX") FLinearColor CastTint = FLinearColor(.16f, .62f, 1.f, .8f);
};
