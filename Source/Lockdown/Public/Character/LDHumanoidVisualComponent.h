#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LDHumanoidVisualComponent.generated.h"

class UStaticMeshComponent;
// Placeholder body shared by the owner's own body and every observed proxy. It only draws what it is told:
// an identity look and one cue byte (ALDCharacter::PresentationCue), never gameplay state of its own.
UCLASS()
class LOCKDOWN_API ULDHumanoidVisualComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    // Cue byte: the low nibble is the action pose (1 attack, 2 cast, 4 hit, 5 lying); each status bit adds its own
    // marker shape, so no status is told apart by colour alone (APP_SPEC 8).
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
    void Animate(const FVector& Velocity, float Delta);
    void SetCue(uint8 Value){Cue=Value;}
private:
    void ApplyLook(const FLinearColor& Clothes, uint8 CreatureId);
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    float Phase = 0;
    uint8 Cue=0;
};
