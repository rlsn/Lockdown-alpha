#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Character/LDCharacter.h"
#include "Development/LDCharacterBuild.h"
#include "LDRegionDirector.generated.h"
class ALDPlayerController;
class ULDRegionDefinition;
enum class ELDSkillTaskPhase : uint8;

UCLASS()
class LOCKDOWN_API ALDWatchControl : public AActor
{
    GENERATED_BODY()
public:
    ALDWatchControl();
};
UCLASS()
class LOCKDOWN_API ALDWildSentinel : public ALDCharacter
{
    GENERATED_BODY()
public:
    explicit ALDWildSentinel(const FObjectInitializer& Initializer);
};
UCLASS()
class LOCKDOWN_API ALDRegionDirector : public AActor
{
    GENERATED_BODY()
public:
    ALDRegionDirector();
    virtual void BeginPlay() override;
    void Sample(double Now);
    void SetupResident(ALDResident* Body,bool Trained);
    bool UpdateResident(ALDResident* Body);
    bool SentinelCanTarget(const ALDCharacter* Target) const;
    void SentinelDefeated(const TMap<ALDPlayerController*,double>& LethalDamage,double Now);
    ALDWildSentinel* SentinelBody() const {return Sentinel.Get();}
    ALDWatchControl* WatchControl() const {return Control.Get();}
    uint32 PeacefulDeaths=0;
    double SentinelReturnAt() const {return ReturnAt;}
    bool Sabotage(ALDCharacter* Source,const FLDCharacterBuild& Snapshot,uint32 Token,ELDSkillTaskPhase Phase,FString& Feedback);
private:
    struct FTraversal{TWeakObjectPtr<ALDCharacter> Body;FVector Previous=FVector::ZeroVector;double At=0;bool Eligible=false;};
    TMap<TWeakObjectPtr<ALDPlayerController>,FTraversal> Traversal;
    UPROPERTY() TObjectPtr<ULDRegionDefinition> Definition;
    TWeakObjectPtr<ALDWildSentinel> Sentinel;
    TWeakObjectPtr<ALDCharacter> Victim;
    TWeakObjectPtr<ALDWatchControl> Control;
    FGuid Encounter,SentinelWallet;
    double ReturnAt=0,LastLegalTarget=0;
    bool Defeated=false;
    uint32 NPCToken=1;
    void SpawnSentinel();
    void Visits(double Now);
};
