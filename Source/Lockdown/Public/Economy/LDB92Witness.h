#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Development/LDWorldChecks.h"
#include "LDB92Witness.generated.h"
UCLASS()
class LOCKDOWN_API ALDB92Witness:public AActor
{
    GENERATED_BODY()
public:
    ALDB92Witness();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Delta) override;
    static const TArray<FName>& Guilds();
    static bool Profession(ELDSkill Skill);
    static FVector Position(const UWorld* World,int32 Guild,uint8 Task);
private:
    UPROPERTY() TArray<TObjectPtr<ALDCharterTask>> Targets;
    TMap<TWeakObjectPtr<class ALDB92Controller>,uint32> Channels;
    bool Execute(class ALDCharacter* Body,int32 Index,const FLDCharacterBuild& Build,ELDSkill Skill,uint32 Token,ELDSkillTaskPhase Phase,FString& Error);
};
