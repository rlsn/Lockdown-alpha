#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDShowdownRingMarker.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// Local presentation of one painted circle pair (LDGroundRing) around a public centre: the current circle and, while it is
// previewed, the next one. The HUD owns one for the final showdown's street closure (ULDHUD::SyncShowdownRing, radii from
// LDShowdownRing at the same-match authority time) and one for a mid-field objective's circle (the mini flag or the arena).
// It is never replicated and has no collision, navigation, sight or height effect; the authority judges every circle from
// the same approved parameters.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDShowdownRingMarker : public AActor
{
    GENERATED_BODY()
public:
    ALDShowdownRingMarker();
    // Paints the rings for one circle (Key: the showdown or an objective serial) of one match generation; false with a
    // reason when the cooked mesh or material is missing.
    bool Build(const FVector& CentreCm, uint32 InGeneration, uint32 InKey, const FLinearColor& Tint, FString& Error);
    // The current circle and the previewed next circle (bPreview). A radius the ground ring cannot draw hides that ring.
    void Show(double SafeRadiusCm, bool bPreview, double PreviewRadiusCm);
    static FLinearColor SafeColor();
    static FLinearColor PreviewColor();
    uint32 GetGeneration() const { return Generation; }
    uint32 GetKey() const { return Key; }
    const FVector& GetCentre() const { return Centre; }
    double GetSafeRadius() const { return SafeRadius; }
    bool IsPreviewShown() const;
    bool IsPresentationOnly() const;
private:
    // Re-lays Ring at RadiusCm when it changed by at least a centimetre; false (ring hidden) when it cannot be drawn.
    static bool Relay(UInstancedStaticMeshComponent* Ring, double RadiusCm, double& InOutLaidCm);
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Safe;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Preview;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SafePaint;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PreviewPaint;
    // The designed looks over the lines: the danger side of the safe ring, the broken line of the next one.
    UPROPERTY() TObjectPtr<class UNiagaraComponent> SafeGlow;
    UPROPERTY() TObjectPtr<class UNiagaraComponent> PreviewGlow;
    FVector Centre = FVector::ZeroVector;
    double SafeRadius = 0;
    double LaidSafeCm = 0;
    double LaidPreviewCm = 0;
    uint32 Generation = 0;
    uint32 Key = 0;
};
