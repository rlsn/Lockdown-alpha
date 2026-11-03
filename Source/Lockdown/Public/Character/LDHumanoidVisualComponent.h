#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Character/LDBodyMotion.h"
#include "LDHumanoidVisualComponent.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
// Placeholder body shared by the owner's own body, every observed proxy and fall puppets. It only draws what it is
// told: an identity look, one cue byte (ALDCharacter::PresentationCue) with the age of its action, and the appear and
// fall events; never gameplay state of its own. Every motion hangs from an inner pivot, so the owner keeps the
// component's own transform and scale.
UCLASS()
class LOCKDOWN_API ULDHumanoidVisualComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    // Cue byte: the low nibble is the action (1 attack, 2 cast, 4 hit); each status bit adds its own marker shape, so
    // no status is told apart by colour alone (APP_SPEC 8).
    enum ECueBits : uint8 { ActionMask = 15, Rooted = 16, Returning = 32, Shielded = 64, Protected = 128 };
    ULDHumanoidVisualComponent();
    virtual void OnRegister() override;
    // One identity tint per locked roster slot 0..5, shared by bodies, portraits and the minimap.
    static FLinearColor ContenderTint(uint8 Slot);
    // Camp NPC clothing; never one of the contender tints.
    static FLinearColor MonsterTint(uint8 CreatureId);
    // Contender look: roster-slot clothing and no NPC accessories.
    void ApplyPalette(uint8 Slot);
    // Camp NPC look (APP_SPEC 1.1): the low tier is a discipline monitor with armband and clipboard, the high tier a
    // PE teacher with cap and whistle.
    void ApplyMonsterLook(uint8 CreatureId);
    // Source's look, for a puppet that replaces it.
    void CopyLook(const ULDHumanoidVisualComponent& Source);
    void Animate(const FVector& Velocity, float Delta);
    // The cue byte and the seconds its action has already run on the presentation clock; a negative age is unknown and
    // counts the action from now. The same action kind reported with a later start restarts it (a repeated hit).
    // The first cue showing respawn protection also plays the appear effect.
    void SetCue(uint8 Value, float ActionAgeSeconds = -1.f);
    void PlayAppear();
    // Falls on its back, sinks and shrinks away; markers and actions stop. IsFallen once it has vanished.
    void PlayFall();
    bool IsFallen() const;
private:
    void ApplyLook(const FLinearColor& Clothes, uint8 CreatureId);
    // Places a limb box hanging from Joint along Direction, without twisting it about its own length.
    void PlaceLimb(int32 Index, const FVector& Joint, const FVector& Direction, float HalfLength);
    void Tint(int32 Index, const FLinearColor& Colour);
    // Places each shown marker's near-black twin behind it on the local view ray.
    void PlaceEdges();
    UPROPERTY() TObjectPtr<USceneComponent> Pivot;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Edges;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Tints;
    TArray<FLinearColor> BaseColours;
    // Eased toward the action timeline, so an ending cue settles instead of snapping to rest.
    LDBodyMotion::FChannels Shown;
    LDBodyMotion::EAction Action = LDBodyMotion::EAction::None;
    double Clock = 0, ActionOnset = 0, AppearAt = -1, FallAt = -1;
    float Phase = 0;
    uint8 Cue = 0;
    uint8 LookSlot = MAX_uint8, LookCreature = 0;
    bool bCueSeen = false, bTinted = false;
};
