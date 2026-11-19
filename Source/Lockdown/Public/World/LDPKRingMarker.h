#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDPKRingMarker.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// Local presentation of one PK arena (#198 session D): flat painted rings (LDGroundRing) around the public arena centre for
// the current safe circle, the next circle while it is previewed, and every telegraphed spectator throw until it lands.
// The HUD owns one per PK (ULDHUD::SyncPKRing) during the PKReady and PK steps and feeds it the radii of the pure ring rules
// (LDPKRules) at the same-match authority time. It is never replicated and has no collision, navigation, sight or height
// effect; the authority judges the ring from the same approved parameters.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDPKRingMarker : public AActor
{
    GENERATED_BODY()
public:
    // Throw telegraphs shown at once; more spectators than this cannot have throws in the air together.
    static constexpr int32 MaxThrows = 6;
    ALDPKRingMarker();
    // Paints the rings for one PK of one match generation; false with a reason when the cooked mesh or material is missing.
    bool Build(const FVector& CentreCm, uint32 InGeneration, uint32 InSerial, FString& Error);
    // The safe circle, the previewed next circle (bPreview) and the throws in flight (ground points, ThrowRadiusCm each;
    // beyond MaxThrows are not drawn). A radius the ground ring cannot draw hides that ring.
    void Show(double SafeRadiusCm, bool bPreview, double PreviewRadiusCm, const TArray<FVector>& ThrowsCm, double ThrowRadiusCm);
    static FLinearColor SafeColor();
    static FLinearColor PreviewColor();
    static FLinearColor ThrowColor();
    uint32 GetGeneration() const { return Generation; }
    uint32 GetSerial() const { return Serial; }
    const FVector& GetCentre() const { return Centre; }
    double GetSafeRadius() const { return SafeRadius; }
    bool IsPreviewShown() const;
    int32 GetShownThrows() const;
    bool IsPresentationOnly() const;
private:
    // Re-lays Ring at RadiusCm when it changed by at least a centimetre; false (ring hidden) when it cannot be drawn.
    static bool Relay(UInstancedStaticMeshComponent* Ring, double RadiusCm, double& InOutLaidCm);
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Safe;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Preview;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Throws;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SafePaint;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PreviewPaint;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ThrowPaint;
    FVector Centre = FVector::ZeroVector;
    double SafeRadius = 0;
    double LaidSafeCm = 0;
    double LaidPreviewCm = 0;
    TArray<double> LaidThrowCm;
    uint32 Generation = 0;
    uint32 Serial = 0;
};
