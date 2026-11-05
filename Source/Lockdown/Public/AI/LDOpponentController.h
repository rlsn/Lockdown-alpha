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

// Requests one adapter sent in its match, counted where each leaves the adapter. Evidence that waits, refusals
// and retries stay bounded; never a policy input.
struct FLDOpponentRequestTally
{
    uint32 Orders = 0;
    uint32 OrdersRefused = 0;
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
    uint32 ShopOpens = 0;
    uint32 Choices = 0;
    uint32 Trades = 0;
    uint32 BuildsRefused = 0;
    // D12 evidence from this adapter's own frames and decisions: decisions taken while it held progress a contest paused,
    // and while it stood inside a contest against a rival's paused progress. Kept means it stayed in the fight; released
    // means a stop-loss exit. HolderAttacks counts attack orders sent at the visible paused holder.
    uint32 PausedHolds = 0;
    uint32 PausedHoldsKept = 0;
    uint32 PausedHoldsReleased = 0;
    uint32 Challenges = 0;
    uint32 ChallengesKept = 0;
    uint32 ChallengesReleased = 0;
    uint32 HolderAttacks = 0;
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
    // Starts when a shop approach completes and ends with any newer body request or life change.
    struct FShopVisit
    {
        bool bActive = false;
        bool bOpenSent = false;
        bool bOpened = false;
        FName ShopId;
        FLDOpponentRetryKey Key;
        int64 OpenSentAtMs = 0;
    };
    // The one choice or trade awaiting its final receipt; a second build change is never sent meanwhile.
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
        FLDShopOpenResult Shop;
        // D26: serial of the public airdrop this contender holds as claimed, zero when none.
        uint64 ClaimedAirdrop = 0;
    };

    bool BuildFrame(ALDGameMode& Mode, int64 NowMs, FReads& Reads, FLDOpponentDecisionFrame& Frame, FString& Error) const;
    bool ReadCapturePath(const FVector& FootCm, double& OutCm, FString& Error) const;
    void SettleOrders(const FReads& Reads, const FLDOpponentSelf& Self, int64 NowMs, FString& Problem);
    void FinishIncumbent(ELDOpponentSettle Settle, const FLDOpponentSelf& Self, int64 NowMs);
    void SettleDirectAction(ALDGameMode& Mode, const FReads& Reads, const FLDOpponentSelf& Self, int64 NowMs, FString& Problem);
    void SettleShop(ALDGameMode& Mode, const FReads& Reads, const FLDOpponentSelf& Self, int64 NowMs, FString& Problem);
    void SettleBuild(ALDGameMode& Mode, const FLDOpponentSelf& Self, int64 NowMs, FString& Problem);
    void ComposeFrame(FLDOpponentDecisionFrame& Frame, const FReads& Reads) const;
    void DispatchBody(ALDGameMode& Mode, const FReads& Reads, const FLDOpponentDecisionFrame& Frame,
        const FLDOpponentBodyIntent& Intent, int64 NowMs, FString& Problem);
    void DispatchBuild(ALDGameMode& Mode, const FReads& Reads, const FLDOpponentDecisionFrame& Frame,
        const FLDOpponentBuildIntent& Intent, int64 NowMs, FString& Problem);
    void Learn(const FLDOpponentRetryKey& Key, ELDOpponentSettle Settle, const FLDOpponentSelf& Self, int64 NowMs);
    FLDOrderCaller OrderCaller(const FReads& Reads);
    void ForgetBodyWork();
    void Report(const FLDOpponentDecisionFrame& Frame, const FLDOpponentDecision& Decision, const FString& Problem);
    void NoteContest(const FLDOpponentDecisionFrame& Frame, const FLDOpponentDecision& Decision);
    void ReportTally();

    FLDAIContenderBinding Binding;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TArray<FLDOpponentPOI> POIs;
    FLDOpponentPolicyState Policy;
    FOrderTrack Incumbent;
    FOrderTrack Candidate;
    FStopTrack StopTrack;
    FActionTrack Action;
    FShopVisit Visit;
    FBuildTrack Build;
    FLDMoveIdentity LastIdentity;
    // Life whose body-scoped requests are tracked; a change drops them without teaching the policy.
    uint32 BodyLife = 0;
    // Match-wide: Combat keeps one direct high-water mark per contender across lives.
    uint64 DirectSequence = 0;
    // Per growth family, from 1, consumed only when the domain reserved the sequence.
    uint64 ChoiceRequestId = 1;
    uint64 ShopRequestId = 1;
    int64 NextDecisionAtMs = 0;
    bool bClosed = false;
    bool bRetiring = false;
    bool bTallyReported = false;
    bool bWasInDorm = false;
    uint64 LastClaimedAirdrop = 0;
    FLDOpponentRequestTally Tally;
    FLDMatchContext RetiringMatch;
    ELDOpponentMode ReportedMode = ELDOpponentMode::Stopped;
    ELDOpponentReason ReportedReason = ELDOpponentReason::None;
    FString ReportedProblem;
};
