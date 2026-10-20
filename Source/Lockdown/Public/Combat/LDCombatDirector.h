#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Development/LDCharacterBuild.h"
#include "LDCombatDirector.generated.h"
class ALDCharacter;
class ALDPlayerController;
enum class ELDSkillTaskPhase : uint8 { Validate, Accept, Commit };

struct FLDBodyCombatState
{
    FGuid Identity=FGuid::NewGuid();
    double Hp=100;
    double CommittedDamage=20;
    double LastDamage=-1000,LastDealt=-1000,QuietSince=0,ExposedUntil=0;
    double BasicReady=0,AssassinReady=0,DashReady=0,ProtectionUntil=0,HitUntil=0,WarningUntil=0;
    uint8 Action=0;
    double ImpactAt=0,RecoveryUntil=0,DashUntil=0;
    FVector CommittedAim=FVector::ForwardVector,Home=FVector::ZeroVector;
    TWeakObjectPtr<AActor> Target;
    uint64 Order=0;
    uint8 IncomingSector=0;
    // Server-only action snapshot. Skill IDs never double as public pose codes.
    ELDSkill Skill=ELDSkill::None;
    FLDCharacterBuild Snapshot,NPCBuild;
    bool bHasNPCBuild=false,bHoldMoving=false;
    uint32 SkillToken=0,HoldSequence=0;
    double LastSkillHold=-1000;
    FVector SkillAnchor=FVector::ZeroVector;
};

UCLASS()
class LOCKDOWN_API ALDCorpse : public AActor
{
    GENERATED_BODY()
public:
    ALDCorpse();
    uint8 Presentation=0;
};

UCLASS()
class LOCKDOWN_API ALDCombatDirector : public AActor
{
    GENERATED_BODY()
public:
    ALDCombatDirector();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    void Register(ALDCharacter* Body);
    void Queue(ALDPlayerController* Controller,uint32 Match,uint32 Life,uint32 Request,uint8 Action,const FVector& Direction);
    bool Request(ALDPlayerController* Controller,uint8 Action,const FVector& Direction,FString& Result);
    bool RequestBody(ALDCharacter* Body,uint8 Action,const FVector& Direction,FString& Result);
    void QueueSkill(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Request,uint32 BuildRevision,uint8 Slot,const FVector& Direction);
    bool RequestEquipped(ALDCharacter* Body,uint32 BuildRevision,uint8 Slot,uint32 Token,const FVector& Direction,FString& Result);
    void RefreshSkillHold(ALDCharacter* Body,uint32 Token,uint32 Sequence,bool Moving);
    void ReleaseSkillHold(ALDPlayerController* PC,uint32 Token);
    bool RegisterNPCBuild(ALDCharacter* Body,const FLDCharacterBuild& Build);
    // Validate is read-only. Accept may reserve a retry window for this token;
    // Commit runs once after damage and final validation, using the source snapshot.
    using FSkillTask = TFunction<bool(ALDCharacter*,ELDSkill,const FLDCharacterBuild&,uint32,ELDSkillTaskPhase,FString&)>;
    void RegisterSkillTask(AActor* Target,const TSet<ELDSkill>& Skills,FSkillTask Task);
    bool HasSkillTask(AActor* Target) const {return SkillTasks.Contains(Target);}
    FLDBodyCombatState* StateFor(ALDCharacter* Body) { return Bodies.Find(Body); }
    const FLDBodyCombatState* StateFor(const ALDCharacter* Body) const { return Bodies.Find(const_cast<ALDCharacter*>(Body)); }
    uint8 CueFor(const ALDCharacter* Body) const;
    double ExposedFor(const ALDCharacter* Body) const;
    void Sample(double Now);
    double LastSampleMs=0;
    uint64 SampleSerial=0;
private:
    struct FResidentReturn { double At; FVector Home; FGuid Wallet;bool HasBuild=false;FLDCharacterBuild Build; };
    TMap<TWeakObjectPtr<ALDCharacter>,FLDBodyCombatState> Bodies;
    TArray<FResidentReturn> Residents;
    struct FIntent { TWeakObjectPtr<ALDPlayerController> Controller;uint32 Match,Life,Request;uint8 Action;FVector Direction;uint32 BuildRevision=0; };
    TArray<FIntent> Intents;
    double NextSample=0;
    uint64 NextOrder=0;
    struct FTask { FGuid Identity=FGuid::NewGuid(); TSet<ELDSkill> Skills; FSkillTask Execute; };
    TMap<TWeakObjectPtr<AActor>,FTask> SkillTasks;
    TMap<TWeakObjectPtr<ALDPlayerController>,uint32> ReleasedSkills;
    FLDCharacterBuild* BuildFor(ALDCharacter* Body);
    bool SkillTargetLegal(ALDCharacter* Source,AActor* Target,ELDSkill Skill) const;
    void CompleteSkillChannels(double Now);
    void CancelSkill(ALDCharacter* Body,FLDBodyCombatState& State);
    bool LegalTarget(ALDCharacter* Source,AActor* Target,double Reach,const FVector* Aim,double Cone) const;
    AActor* Nearest(ALDCharacter* Source,double Reach,const FVector& Aim,double Cone,bool bAssassin,bool bBodiesOnly=false) const;
    void Die(ALDCharacter* Body,double Now);
    void Sync(ALDCharacter* Body,FLDBodyCombatState& State);
};
