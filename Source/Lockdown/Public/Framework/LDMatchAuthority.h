#pragma once
#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"

// The authority slice of ALDGameMode that domain code reads without the whole GameMode: the running match, the
// authority clock, write permits and fault reports. ALDGameMode is the only implementation and owns every value;
// domains reach it through their director (for example ALDCombatDirector::Authority()).
class ILDMatchAuthority
{
public:
    virtual const FLDMatchContext& GetMatchContext() const = 0;
    virtual bool IsGameplayWriteOpen(const FLDMatchContext& Context) const = 0;
    // The rest return countdown and the PK ready countdown lock every input; while the PK runs only PK lives act.
    // Contender: the acting contender (invalid: judged as a non-PK life).
    virtual bool IsInputLocked(const FLDMatchContext& Context, const FGuid& Contender = FGuid()) const = 0;
    virtual bool IsShowdown() const = 0;
    virtual const FLDAuthorityStep& GetAuthorityStep() const = 0;
    virtual double GetAuthorityNowSeconds() const = 0;
    virtual int64 GetAuthorityNowMs() const = 0;
    virtual ELDCommitReject ValidateWritePermit(const FLDGameplayWritePermit& Permit, ELDCommitPhase Phase) const = 0;
    virtual bool ReportAuthorityFault(const FLDMatchContext& Match, const FString& Diagnostic) = 0;
protected:
    ~ILDMatchAuthority() = default;
};
