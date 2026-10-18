#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDCombatDirector.generated.h"
class ALDCharacter;
class ALDPlayerController;

struct FLDBodyCombatState
{
    FGuid Identity=FGuid::NewGuid();
    int32 Hp=100;
    double LastDamage=-1000,LastDealt=-1000,QuietSince=0,ExposedUntil=0;
    double BasicReady=0,AssassinReady=0,DashReady=0,ProtectionUntil=0,HitUntil=0,WarningUntil=0;
    uint8 Action=0;
    double ImpactAt=0,RecoveryUntil=0,DashUntil=0;
    FVector CommittedAim=FVector::ForwardVector,Home=FVector::ZeroVector;
    TWeakObjectPtr<AActor> Target;
    uint64 Order=0;
    uint8 IncomingSector=0;
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
    FLDBodyCombatState* StateFor(ALDCharacter* Body) { return Bodies.Find(Body); }
    const FLDBodyCombatState* StateFor(const ALDCharacter* Body) const { return Bodies.Find(const_cast<ALDCharacter*>(Body)); }
    uint8 CueFor(const ALDCharacter* Body) const;
    double ExposedFor(const ALDCharacter* Body) const;
    void Sample(double Now);
private:
    struct FResidentReturn { double At; FVector Home; };
    TMap<TWeakObjectPtr<ALDCharacter>,FLDBodyCombatState> Bodies;
    TArray<FResidentReturn> Residents;
    struct FIntent { TWeakObjectPtr<ALDPlayerController> Controller;uint32 Match,Life,Request;uint8 Action;FVector Direction; };
    TArray<FIntent> Intents;
    double NextSample=0;
    uint64 NextOrder=0;
    bool LegalTarget(ALDCharacter* Source,AActor* Target,double Reach,const FVector* Aim,double Cone) const;
    AActor* Nearest(ALDCharacter* Source,double Reach,const FVector& Aim,double Cone,bool bAssassin) const;
    void Die(ALDCharacter* Body,double Now);
    void Sync(ALDCharacter* Body,FLDBodyCombatState& State);
};
