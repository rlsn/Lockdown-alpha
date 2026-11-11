#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDSignupMarker.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// Local presentation of the own PK sign-up disk (#198 站圈报名, user decision A on issue #198): a flat painted ring of
// AIRDROP.PickupRadiusCm around the own roster slot's pk_signup anchor, beside the private rest anchor. The HUD owns one
// during a rest's build step (ULDHUD::SyncSignupMarker): dim before the sign-up window, bright while it is open, gold while
// the public sign-ups name the own contender. It only mirrors the public timeline; it is never replicated and has no
// collision, navigation, sight or height effect. The authority judges the committed foot against the same disk.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDSignupMarker : public AActor
{
    GENERATED_BODY()
public:
    ALDSignupMarker();
    // Lays the ring for one match generation and roster slot; false with a reason when the cooked mesh or material is
    // missing or the disk cannot be drawn.
    bool Build(const FVector& CentreCm, double RadiusCm, uint32 InGeneration, int32 InSlot, FString& Error);
    void Show(bool bWindowOpen, bool bSignedUp);
    static FLinearColor WaitingColor();
    static FLinearColor OpenColor();
    static FLinearColor SignedUpColor();
    uint32 GetGeneration() const { return Generation; }
    int32 GetSlot() const { return Slot; }
    const FVector& GetCentre() const { return Centre; }
    double GetRadius() const { return Radius; }
    bool IsPresentationOnly() const;
private:
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Ring;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingPaint;
    FVector Centre = FVector::ZeroVector;
    double Radius = 0;
    uint32 Generation = 0;
    int32 Slot = INDEX_NONE;
    int32 ShownState = INDEX_NONE;
};
