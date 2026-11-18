#pragma once
#include "CoreMinimal.h"
#include "Framework/LDMatchAuthority.h"
#include "Framework/LDPacingTypes.h"

class ALDCharacter;
class UWorld;
class ALDVisionDirector;
struct FLDDormZones;

// The life slice of ALDGameMode: which bodies are the current lives of which entities, their protection and phase
// spaces, and the vision director that decides who sees them. ALDGameMode is the only implementation and owner of
// every life record; domain code reads it through its director (for example ALDCombatDirector::Authority()).
class ILDLifeAuthority : public ILDMatchAuthority
{
public:
    virtual ELDCommitReject ResolveBody(const FLDMatchContext& Match, const FLDCombatBodyRef& Body, ALDCharacter*& Out) const = 0;
    virtual bool ReadBodyRef(const ALDCharacter& Body, FLDCombatBodyRef& Out) const = 0;
    virtual bool ReadContenderLife(const FLDMatchContext& Match, const FGuid& ContenderId, FLDContenderLifeRead& Out,
        FString& Error) const = 0;
    // The current living body of a contender and its committed foot; false when it has none.
    virtual bool ReadLivingBody(const FGuid& Contender, FLDCombatBodyRef& OutBody, FVector& OutFootCm) const = 0;
    // A room member (contender or camp member) whose life record is present.
    virtual bool IsContenderPresent(const FGuid& Contender) const = 0;
    // Life-domain protection (immune to damage and control); Combat reads it at the hit point.
    virtual bool IsContenderProtected(const FGuid& ContenderId) const = 0;
    // Whether a contender currently has a shared-room rest life, not a field life.
    virtual bool IsInRestSpace(const FGuid& Contender) const = 0;
    // Shared training dummies keep each slot's generation-time level chassis and no build; they grant nothing.
    virtual bool IsTrainingDummy(const FGuid& Entity) const = 0;
    // D21 (#79): the public dorm disks of this match; empty outside a running match.
    virtual const FLDDormZones& GetDormZones() const = 0;
    // The match's vision director; null outside a match.
    virtual ALDVisionDirector* GetVisionAuthority() const = 0;
    // Spec §11.1: whether the running match drew this daily modifier.
    virtual bool HasDaily(ELDDailyModifier Modifier) const = 0;
#if WITH_DEV_AUTOMATION_TESTS
    virtual bool IsDevPeaceful(const FGuid& Contender) const = 0;
#endif
protected:
    ~ILDLifeAuthority() = default;
};

namespace LDAuthority
{
    // The world's ALDGameMode through its life slice; null on clients and in worlds without it.
    LOCKDOWN_API ILDLifeAuthority* Find(const UWorld* World);
}
