#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDPointerFeedback.generated.h"

class ALDPlayerController;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

// The owning player's pointer feedback (control feel): the ground marker of an own move order (green, or a red cross
// where the local route check already fails) and of an own attack-move (the same marker in red), the attack marker on the own attack order's target, the hover ring
// under the visible unit a right click would attack and the selection ring under the inspected unit. Local presentation only: never replicated, no collision, no
// navigation, and a target is placed only from the owner's authorized observation (ALDWorldViewRelay).
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDPointerFeedback : public AActor
{
    GENERATED_BODY()
public:
    ALDPointerFeedback();
    virtual void Tick(float Delta) override;
    void Bind(ALDPlayerController& Player);
    // bAttack draws the attack-move's red ring instead of the move's green one; an unreachable point is the same red cross.
    void ShowMove(const FVector& GroundCm, bool bReachable, bool bAttack = false);
    void ShowAttack(const FGuid& Handle, uint32 ObservationEpoch);
    // An invalid handle clears the ring.
    void SetHover(const FGuid& Handle, uint32 ObservationEpoch);
    // The inspected unit: a still, dense, pale dotted ring inside the turning red hover ring. An invalid handle clears it.
    void SetSelected(const FGuid& Handle, uint32 ObservationEpoch);
    // Shift queue: a small ring at each waiting ground step (red for an attack-move), joined by a dashed line from the
    // own body through the current order's ground goal (when it has one) and the waypoints in turn. Empty hides them.
    void SetQueue(const TArray<FVector>& Waypoints, const TArray<bool>& Attack, const FVector* CurrentGoal);
    // Seconds the click markers show.
    static constexpr float MoveSeconds = .45f;
    static constexpr float CrossSeconds = .6f;
    static constexpr float AttackSeconds = .5f;
    // What the last frame placed, for DEV captures.
    bool ShowsMove() const { return bMoveShown; }
    bool ShowsAttack() const { return bAttackShown; }
    bool ShowsHover() const { return bHoverShown; }
    bool ShowsSelected() const { return bSelectedShown; }
    int32 ShownWaypoints() const { return QueueWaypoints.Num(); }
private:
    enum class ERing : uint8 { Move, Attack, Hover, Selected, AttackMove, Count };
    struct FRing
    {
        TArray<TObjectPtr<UStaticMeshComponent>> Dashes, Shades;
    };
    UStaticMeshComponent* NewPart(const TCHAR* Name, UMaterialInstanceDynamic* Paint);
    // Places Ring's dashes around Centre at Radius, turned by Turn degrees; a zero scale hides them.
    void PlaceRing(ERing Ring, const FVector& Centre, float Radius, float Turn, float Scale);
    void HideRing(ERing Ring);
    bool TargetFeet(const FGuid& Handle, uint32 Epoch, FVector& Out) const;
    // Rebuilds the queue rings and path dashes from the body's current feet.
    void PlaceQueue();
    FRing Rings[int32(ERing::Count)];
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Cross;
    // Queue rings and path dashes: one instanced part per paint.
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> QueueMove;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> QueueAttack;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> QueueShade;
    TArray<FVector> QueueWaypoints;
    TArray<bool> QueueAttackFlags;
    FVector QueueGoal = FVector::ZeroVector;
    bool bQueueGoal = false;
    TWeakObjectPtr<ALDPlayerController> Viewer;
    FVector MoveAt = FVector::ZeroVector;
    FGuid AttackHandle, HoverHandle, SelectedHandle;
    uint32 AttackEpoch = 0, HoverEpoch = 0, SelectedEpoch = 0;
    double Clock = 0, MoveFrom = -1, AttackFrom = -1;
    bool bMoveReachable = true;
    bool bMoveAttack = false;
    bool bMoveShown = false, bAttackShown = false, bHoverShown = false, bSelectedShown = false;
};
