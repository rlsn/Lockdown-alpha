#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Framework/LDMatchProviders.h"
#include "PK/LDPKRules.h"
#include "PK/LDPKTypes.h"
#include "LDPKDirector.generated.h"

class ALDGameMode;
class FLDGameplayWritePermit;

// #198 session D: the PK inside a rest and its spectators (approved outer parameters, issue #198 comment 6752601175 §2-§4
// and §6; AI spec §16.1-§16.3). The optional match provider ELDMatchProvider::PK spawns one director per match
// (ALDGameMode::PKDirector). It hears every rest step (FLDMatchProviderHooks::RestStep) and drives:
//   lock (Transactions)        the locked entrants and the eligible spectators (present non-entrants) are fixed;
//   Draw (Transactions)        format, sides and spawn anchors are drawn once from the director's match stream;
//   Betting                    eligible spectators bet on one side (changeable until the step ends);
//   PKReady (RespawnsAndWorld) every entrant's rest life retires and a PK life stands at its drawn anchor in the PK
//                              isolation group with its side; inputs are locked (ALDGameMode::IsInputLocked);
//   PK                         the ring shrinks and burns; throws land; the last side alive wins, the deadline or a
//                              simultaneous wipe is a draw (FinishRestPK ends the step early);
//   Settlement                 the result freezes, PK lives return to their rest spaces and the points are paid once in
//                              the same boundary's Transactions (ALDDevelopmentDirector::GrantPKPoints).
// A PK death never respawns in the arena: the eliminated contender stands up in its rest space and watches. Nothing of the
// field runs for PK lives: no capture eligibility, no kill/assist reward, no K/D (they are rest-phase lives).
UCLASS()
class LOCKDOWN_API ALDPKDirector : public AActor
{
    GENERATED_BODY()
public:
    ALDPKDirector();

    bool BeginMatch(const FLDMatchStartContext& Start, FString& Error);
    void EndMatch();

    // ---- Provider hook (every rest step entry and the build lock) ----
    bool OnRestStep(const FLDRestStepNotice& Notice, const FLDGameplayWritePermit& Permit, FString& Error);

    // ---- Driver (Combat/LDCombatDriver.cpp) ----
    // The next ring tick, throw landing or Exposed end the driver must stop at; MAX_int64 when none.
    int64 NextDueMs() const;
    // Effects: due throws land (slow + Exposed on entrants inside the radius in sight of the landing point), then the ring
    // settles every elapsed tick of outside time as environmental damage.
    bool AdvanceEffects(const FLDGameplayWritePermit& Permit, FString& Error);
    // DeathsAndRewards, after the deaths of the boundary: forfeits are applied and the fight is judged; a decided PK ends
    // the step at this boundary (ALDGameMode::FinishRestPK).
    bool JudgeAfterDeaths(const FLDGameplayWritePermit& Permit, FString& Error);
    // Transactions: the frozen awards of a settled PK are paid once.
    bool ProcessTransactions(const FLDGameplayWritePermit& Permit, FString& Error);

    // ---- Owner requests (ALDPlayerController::ServerPKBet / ServerPKThrow and the opponent AI, same entry) ----
    ELDPKBetResult SubmitBet(const FGuid& Contender, uint32 Generation, uint32 Serial, uint8 Side);
    // OutReadyAtMs: when the contender may throw again (also on CoolingDown).
    ELDPKThrowResult SubmitThrow(const FGuid& Contender, uint32 Generation, uint32 Serial, const FVector& TargetCm, int64& OutReadyAtMs);

    // ---- Reads ----
    const FLDPKView& GetView() const { return View; }
    const FLDPKParameters& GetParameters() const { return Parameters; }
    // A fight (PKReady or PK step) of this rest is running with PK lives.
    bool IsArenaActive() const { return bArenaActive; }
    // The PK isolation group of the running arena; invalid when none.
    const FGuid& GetGroup() const { return Group; }
    // 1..SideCount for a locked entrant of the running arena, 0 otherwise.
    uint8 SideOf(const FGuid& Contender) const;
    // Living PK teammates of Contender (same side, not Contender), sorted.
    void LivingTeammates(const FGuid& Contender, TArray<FGuid>& Out) const;
    // Observer may watch the whole arena now: the arena is active and Observer is not a living PK entrant (spectators and
    // the eliminated). The camera, the fog and the units it sees follow this explicit permission.
    bool CanSpectate(const FGuid& Observer) const;

private:
    ALDGameMode* AuthorityMode() const;
    bool CheckPermit(const FLDGameplayWritePermit& Permit, ELDCommitPhase Phase, FString& Error) const;
    void ResetRest();
    void Publish();
    bool LockRoster(const FLDRestStepNotice& Notice, FString& Error);
    bool DrawFormat(FString& Error);
    bool EnterArena(const FLDGameplayWritePermit& Permit, FString& Error);
    bool StartFight(const FLDGameplayWritePermit& Permit, FString& Error);
    bool Settle(const FLDGameplayWritePermit& Permit, FString& Error);
    bool LandThrows(const FLDGameplayWritePermit& Permit, FString& Error);
    bool BurnRing(const FLDGameplayWritePermit& Permit, FString& Error);
    // The living entrants of each seat (a living PK life and still present).
    void AliveOfSeats(TArray<bool>& Out) const;
    int32 SeatOf(const FGuid& Contender) const;

    TWeakObjectPtr<ALDGameMode> Mode;
    FLDMatchContext Match;
    FLDPKParameters Parameters;
    FRandomStream Random;
    FVector ArenaCentreCm = FVector::ZeroVector;
    FName ArenaSurface;
    FVector ThrowAnchorCm = FVector::ZeroVector;
    TArray<FName> SpawnAnchors;
    uint32 Serial = 0;
    FLDPKView View;

    // ---- Current rest ----
    TArray<FGuid> Entrants;          // seat order (sorted id)
    TArray<FGuid> Bettors;           // eligible spectators, sorted id
    FLDPKDraw Drawn;
    TArray<uint8> BetSides;          // parallel to Bettors, 0 = none
    TArray<bool> Forfeited;          // parallel to Entrants
    FGuid Group;
    bool bArenaActive = false;
    bool bFightRunning = false;
    bool bDecided = false;
    // FinishRestPK was asked for this PK (once).
    bool bFinishRequested = false;
    int64 FightStartedAtMs = 0;
    // Per seat: max HP at the PK start (P2) and the ring damage accumulator (P2 x 1e7); the instant outside time was
    // settled through and the next damage tick.
    TArray<int64> StartMaxHpP2;
    TArray<int64> RingAccumulatorE7;
    int64 RingSettledThroughMs = 0;
    int64 NextRingTickAtMs = MAX_int64;
    struct FPendingThrow
    {
        uint32 Serial = 0;
        FGuid Thrower;
        FVector TargetCm = FVector::ZeroVector;
        int64 LandsAtMs = 0;
    };
    TArray<FPendingThrow> Throws;
    uint32 NextThrowSerial = 1;
    TMap<FGuid, int64> ThrowReadyAtMs;
    // Awards frozen at the settlement and not yet paid (paid once in Transactions), with their business identities.
    TArray<FLDPKAwardView> PendingAwards;
    TArray<FGuid> AwardIds;
    bool bAwardsPaid = false;
};
