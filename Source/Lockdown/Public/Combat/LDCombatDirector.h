#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Combat/LDAttributeProjection.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"
#include "LDCombatDirector.generated.h"

class ALDCharacter;

struct FLDBodyCombatState
{
    FGuid Identity;
    FLDVictimKey Victim;
    struct FResources
    {
        int64 HpP2=0,MaxHpP2=0,MpP2=0,MaxMpP2=0,ShieldP2=0;
        uint64 Revision=0;
    } Resources;
    FLDBasicTiming BasicTiming;
    FLDAttributeProjection AttributeProjection;
    uint64 AttributeRevision=0;
    TOptional<FLDObservedPublicFacts> PublicFacts;
};

UCLASS()
class LOCKDOWN_API ALDCombatDirector : public AActor
{
    GENERATED_BODY()
public:
    ALDCombatDirector();
    virtual void Tick(float Delta) override;
    const FLDBodyCombatState* StateFor(const ALDCharacter* Body) const;
    bool AdvanceAuthority(const FLDMatchContext& Match,int64 ThroughMs,FString& Error);
private:
    friend class ALDGameMode;
    friend class FLDDemoCoreTest;
    bool PrepareInitialBody(ALDCharacter& Body,const FLDVictimKey& Victim,
        const class FLDDemoContentSnapshot& Content,const FLDGameplayWritePermit& Permit,
        FLDBodyCombatState& Out,FString& Error);
    bool InstallInitialBody(ALDCharacter& Body,FLDBodyCombatState&& Prepared,
        const FLDGameplayWritePermit& Permit,FLDCombatBodyRef& Out,FString& Error);
    void ReserveInitialBodyBatch(int32 Count) { Bodies.Reserve(Bodies.Num()+Count); }
    void RemoveRegisteredBody(const FLDCombatBodyRef& Ref);
    TMap<TWeakObjectPtr<ALDCharacter>,FLDBodyCombatState> Bodies;
    bool bAdvancing=false;
};
