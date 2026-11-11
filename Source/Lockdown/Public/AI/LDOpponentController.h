#pragma once

#include "CoreMinimal.h"
#include "AI/LDOpponentPolicy.h"
#include "AI/LDOpponentRequests.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchProviders.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDOrderTypes.h"
#include "LDOpponentController.generated.h"

class ALDGameMode;
class FLDDemoContentSnapshot;
#if WITH_DEV_AUTOMATION_TESTS
struct FLDEvaluationRun;
#endif

// Requests one adapter sent in its match, counted where each leaves the adapter. Evidence that waits, refusals
// and retries stay bounded; never a policy input.
struct FLDOpponentRequestTally
{
    uint32 Orders = 0;
    uint32 OrdersRefused = 0;
    // Orders accepted on receipt that navigation then refused when it came to admit them (#160).
    uint32 OrdersRejected = 0;
    uint32 Stops = 0;
    uint32 Actions = 0;
    uint32 ActionsRefused = 0;
    // D19 evidence: innate dash activations among Actions, and those Combat finished (the body really moved).
    uint32 Dashes = 0;
    uint32 DashesLanded = 0;
    // D21 evidence: times this adapter's living body stepped into a dorm, read from its own owner view.
    uint32 DormEntries = 0;
    // D26 evidence: public airdrops this adapter's contender claimed, read from the public match view.
    uint32 AirdropClaims = 0;
    // D27 evidence: times this adapter's decisions turned to hunting a named 上热搜 leader.
    uint32 LeaderHunts = 0;
    // D26 evidence: times this adapter's decisions turned to stepping away from the Boss.
    uint32 BossAvoids = 0;
    // #198 build commands sent (active choices, blueprint learns and point allocations) and those refused on admission.
    uint32 Builds = 0;
    uint32 BuildsRefused = 0;
    // Spray evidence from this adapter's own frames and decisions: decisions taken while it held spray progress a contest
    // paused, and while it stood inside a contest against a rival's paused progress. Kept means it stayed in the fight;
    // released means a stop-loss exit. HolderAttacks counts attack orders sent at the visible paused holder,
    // OwnerAttacks attack orders and actions aimed at the visible rival graffiti owner (the siege of APP_SPEC 2.2).
    uint32 PausedHolds = 0;
    uint32 PausedHoldsKept = 0;
    uint32 PausedHoldsReleased = 0;
    uint32 Challenges = 0;
    uint32 ChallengesKept = 0;
    uint32 ChallengesReleased = 0;
    uint32 HolderAttacks = 0;
    uint32 OwnerAttacks = 0;
    // #198 D: PK actions sent through the player's entries (walks into the own sign-up disk, bets, throws) and the bets
    // and throws the authority refused.
    uint32 PKSignUps = 0;
    uint32 PKBets = 0;
    uint32 PKThrows = 0;
    uint32 PKRefused = 0;
    uint32 BodyRequests() const { return Orders + Stops + Actions; }
};

// Server-only opponent adapter (docs/demo/OPPONENT_AI.md §3). It has no local player, connection, input or
// UI. Each decision interval it reads only what the core and the domains publish for this contender, runs
// the pure policy once and submits at most one body and one build request through the same server entries
// a human reaches over its owning connection. Receipts advance only this adapter's cursors and the
// policy's bounded failure memory.
UCLASS(NotBlueprintable, Transient)
class LOCKDOWN_API ALDOpponentController : public ALDPlayerController
{
    GENERATED_BODY()
public:
    ALDOpponentController();
    // Called once by the OpponentAI provider after the core registered this controller for a frozen slot.
    bool BindRegisteredSelf(const FLDAIContenderBinding& InBinding, FString& Error);
    virtual void Tick(float DeltaSeconds) override;
    void HandleLifeCommitted(const FLDLifeNotice& Notice);
    void HandleMatchClosed(const FLDMatchClosed& Closed);
    // Stops all activity. The adapter destroys itself once the core retired its body and dropped its record.
    void HandleMatchReset(const FLDMatchReset& Reset);
    // Client RPCs run locally on a connectionless server controller: the AI never travels the host process.
    virtual void ClientTravelInternal_Implementation(const FString& URL, ETravelType TravelType, bool bSeamless,
        FGuid MapPackageGuid) override;
    const FLDOpponentRequestTally& GetRequestTally() const { return Tally; }
    // Reason of the latest decision; evidence for checkers only, never a policy input.
    ELDOpponentReason GetReportedReason() const { return ReportedReason; }

private:
    // One spatial order exactly as it was sent. The incumbent is the last order Navigation admitted; a newer
    // candidate replaces it only on admission, because a refused replacement leaves the incumbent running.
    struct FOrderTrack
    {
        bool bHas = false;
        bool bFinal = false;
        FLDOpponentBodyIntent Intent;
        FLDSpatialOrderRequest Request;
        ELDOpponentOrderStatus Status = ELDOpponentOrderStatus::None;
        int64 SentAtMs = 0;
    };
    // A Stop awaiting the Actions phase that applies it; never re-sent for the same orders while unapplied.
    struct FStopTrack
    {
        bool bHas = false;
        FLDMoveIdentity Identity;
        int64 SentAtMs = 0;
        int32 Attempts = 0;
    };
    // The latest direct action; it owns the body until its receipt is final.
    struct FActionTrack
    {
        bool bHas = false;
        bool bFinal = false;
        FLDOpponentBodyIntent Intent;
        uint64 DirectSequence = 0;
        int64 SentAtMs = 0;
    };
    // The one build command awaiting its final receipt; a second build change is never sent meanwhile.
    struct FBuildTrack
    {
        bool bHas = false;
        FLDReceiptQuery Query;
        FLDOpponentRetryKey Key;
        int64 SentAtMs = 0;
    };
    // Authorized inputs of one decision, all read after the same completed publication.
    struct FReads
    {
        FLDOwnerState Owner;
        bool bCombat = false;
        FLDCombatCaller CombatCaller;
        FLDCombatOwnerView Combat;
        // D26: serial of the public airdrop this contender holds as claimed, zero when none.
        uint64 ClaimedAirdrop = 0;
    };

    bool BuildFrame(ALDGameMode& Mode, int64 NowMs, FReads& Reads, FLDOpponentDecisionFrame& Frame, FString& Error) const;
    bool ReadCapturePath(const FVector& FootCm, double& OutCm, FString& Error) const;
    void SettleOrders(const FReads& Reads, const FLDOpponentSelf& Self, int64 NowMs, FString& Problem);
    void FinishIncumbent(ELDOpponentSettle Settle, const FLDOpponentSelf& Self, int64 NowMs);
    void SettleDirectAction(ALDGameMode& Mode, const FReads& Reads, const FLDOpponentSelf& Self, int64 NowMs, FString& Problem);
    void SettleBuild(ALDGameMode& Mode, const FLDOpponentSelf& Self, int64 NowMs, FString& Problem);
    void ComposeFrame(FLDOpponentDecisionFrame& Frame) const;
    void DispatchBody(ALDGameMode& Mode, const FReads& Reads, const FLDOpponentDecisionFrame& Frame,
        const FLDOpponentBodyIntent& Intent, int64 NowMs, FString& Problem);
    void DispatchBuild(ALDGameMode& Mode, const FReads& Reads, const FLDOpponentDecisionFrame& Frame,
        const FLDOpponentBuildIntent& Intent, int64 NowMs, FString& Problem);
    // #198 D: one PK request through the same server entries a player's controller reaches; every refusal is reported.
    void DispatchPK(ALDGameMode& Mode, const FLDOpponentDecisionFrame& Frame, const FLDOpponentPKIntent& Intent, int64 NowMs,
        FString& Problem);
    void Learn(const FLDOpponentRetryKey& Key, ELDOpponentSettle Settle, const FLDOpponentSelf& Self, int64 NowMs);
    FLDOrderCaller OrderCaller(const FReads& Reads);
    void ForgetBodyWork();
    void Report(const FLDOpponentDecisionFrame& Frame, const FLDOpponentDecision& Decision, const FString& Problem);
    void NoteContest(const FLDOpponentDecisionFrame& Frame, const FLDOpponentDecision& Decision);
    void ReportTally();

    FLDAIContenderBinding Binding;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TArray<FLDOpponentPOI> POIs;
    // #198 站圈报名: the own PK sign-up disk (this contender's roster slot); only rest frames carry it.
    FLDOpponentPOI SignupPOI;
    TArray<FLDOpponentPOI> RestServicePOIs;
    FLDOpponentPolicyState Policy;
    FOrderTrack Incumbent;
    FOrderTrack Candidate;
    FStopTrack StopTrack;
    FActionTrack Action;
    FBuildTrack Build;
    FLDMoveIdentity LastIdentity;
    // Life whose body-scoped requests are tracked; a change drops them without teaching the policy.
    uint32 BodyLife = 0;
    // Match-wide: Combat keeps one direct high-water mark per contender across lives.
    uint64 DirectSequence = 0;
    // Build request ids from 1, consumed only when the domain reserved the sequence.
    uint64 BuildRequestId = 1;
    int64 NextDecisionAtMs = 0;
    bool bClosed = false;
    bool bRetiring = false;
    bool bTallyReported = false;
    bool bWasInDorm = false;
    uint64 LastClaimedAirdrop = 0;
    // #198 D: the PK serial of this adapter's latest throw answer and when it may throw again in that PK.
    uint32 PKThrowSerial = 0;
    int64 PKThrowReadyAtMs = 0;
    // #153: drawn once per match from the core's server random stream: the moment after the countdown began from which
    // this adapter makes its countdown build decisions (1-4 s).
    int64 OpeningPickAfterMs = 0;
    FLDOpponentRequestTally Tally;
#if WITH_DEV_AUTOMATION_TESTS
    // DEV build evaluation (-LDBuildEvaluation only): the profile the Host probe staged for this contender, taken at bind.
    // It replaces only the decision; frames, requests, receipts and failure memory stay this adapter's.
    TSharedPtr<FLDEvaluationRun> Evaluation;
#endif
    FLDMatchContext RetiringMatch;
    ELDOpponentMode ReportedMode = ELDOpponentMode::Stopped;
    ELDOpponentReason ReportedReason = ELDOpponentReason::None;
    FString ReportedProblem;
};
