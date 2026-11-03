#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDBodyPuppet.generated.h"

class ULDHumanoidVisualComponent;
class ALDPlayerController;

// Client presentation of a body that just died: a non-replicated, collision-free actor (never a pawn, never attached
// to a proxy or a body) that copies the body's look and placement, falls on its back and removes itself.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDBodyPuppet : public AActor
{
    GENERATED_BODY()
public:
    ALDBodyPuppet();
    virtual void Tick(float Delta) override;
    // An observed body's published death: the fall plays at once. Null when the world cannot spawn it.
    static ALDBodyPuppet* SpawnFall(const ULDHumanoidVisualComponent& Source);
    // The own body removed by the authority: it falls only once Viewer's projection shows that Life dead, so a match
    // end or a departure, which also remove bodies, shows no fall.
    static ALDBodyPuppet* SpawnOwnFall(const ULDHumanoidVisualComponent& Source, const ALDPlayerController& Viewer, uint32 Life);
    // How long the own body's removal waits for its death to be confirmed.
    static constexpr double ConfirmSeconds = 1.0;
private:
    static ALDBodyPuppet* Spawn(const ULDHumanoidVisualComponent& Source);
    void Fall();
    UPROPERTY() TObjectPtr<ULDHumanoidVisualComponent> Body;
    TWeakObjectPtr<const ALDPlayerController> Watched;
    uint32 WatchedLife = 0;
    double ConfirmUntil = 0;
    bool bFalling = false;
};
