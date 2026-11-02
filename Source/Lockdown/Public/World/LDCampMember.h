#pragma once

#include "CoreMinimal.h"
#include "Character/LDCharacter.h"
#include "LDCampMember.generated.h"

struct FLDDemoMonsterDefinition;
class FLDDemoContentSnapshot;

// Authority-only camp member body. It never replicates: observers see camp members only through
// authorized observation, and its native movement runs only inside the core Motion step.
UCLASS(NotBlueprintable)
class LOCKDOWN_API ALDCampMemberCharacter : public ALDCharacter
{
    GENERATED_BODY()
public:
    explicit ALDCampMemberCharacter(const FObjectInitializer& ObjectInitializer);
    virtual void BeginPlay() override;
    // Called on the deferred actor before FinishSpawning. Camp members share the single approved
    // navigation agent, so their collision radius must equal the contender radius.
    bool PrepareMonster(const FLDDemoContentSnapshot& Content, const FLDDemoMonsterDefinition& Monster, FString& Error);
    FName GetMonsterId() const { return MonsterId; }

private:
    FName MonsterId;
    float MonsterSpeed = 0;
};
