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
    UPROPERTY(EditAnywhere, Category="Combat VFX") FLinearColor SwingTint = FLinearColor(1.f, .18f, .025f, .85f);
    UPROPERTY(EditAnywhere, Category="Combat VFX") FLinearColor CastTint = FLinearColor(.16f, .62f, 1.f, .8f);
};
