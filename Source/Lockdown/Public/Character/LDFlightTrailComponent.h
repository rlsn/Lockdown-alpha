#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LDFlightTrailComponent.generated.h"

class UStaticMeshComponent;

// Layout of the chalk-dust trail, a pure function of the host's recent speed so it can be checked without a world.
namespace LDFlightTrail
{
    struct FPuff
    {
        float OffsetCm = 0;    // backward from the trail's origin along the host's facing
        float DiameterCm = 0;
    };
    inline constexpr int32 PuffCount = 14;
    // Spacing follows the recent speed (a chalk at 1600 cm/s strings out to its cap); below MinSpacingCm the host is
    // treated as standing still and no puff shows.
    inline constexpr float SpacingPerSpeed = .008f, MaxSpacingCm = 11.f, MinSpacingCm = 2.f;
    // Puffs taper from about the chalk's own thickness to a wisp, about 1.5 m long at full speed: the thick half overlaps
    // into one streak, so the trail reads as a solid white line over the light lawn at 720p, and the tail breaks into dust.
    inline constexpr float HeadDiameterCm = 20.f, TailDiameterCm = 8.f;
    LOCKDOWN_API float SpacingCm(float SpeedCmPerSec);
    // The puffs to draw, nearest first; empty while the host stands still.
    LOCKDOWN_API void Layout(float SpeedCmPerSec, TArray<FPuff>& Out);
}

// A chalk-dust trail behind a visible flight: puffs strung out backward from the component along the host actor's
// facing, as long as its recent speed, and gone while it stands still. It reads only its own host's motion.
UCLASS()
class LOCKDOWN_API ULDFlightTrailComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    ULDFlightTrailComponent();
    virtual void OnRegister() override;
    virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    // Puffs drawn on the last tick and the trail's length from its origin to the far edge of the last one.
    int32 ShownPuffs() const { return Shown; }
    float LengthCm() const { return ShownLengthCm; }
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Puffs;
    FVector LastWorld = FVector::ZeroVector;
    float SpeedCmPerSec = 0;
    int32 Shown = 0;
    float ShownLengthCm = 0;
    bool bHasLast = false, bTinted = false;
};