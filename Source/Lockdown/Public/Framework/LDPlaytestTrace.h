#pragma once

#include "CoreMinimal.h"
#include "Core/LDLog.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "UI/LDHUDModel.h"
#include "UI/LDWorldFeedback.h"

class ALDPlayerController;
class FLDDemoContentSnapshot;
class UNetConnection;

namespace LDPlaytest
{
    // Whether a line or mark for Match belongs to the running match: it is the current match generation and that match is in
    // Development or CaptureOpen. The lobby, start, results, a reset and every earlier match are outside it. Both the client's
    // own-view lines and mark key and the Host's record of a mark use it.
    LOCKDOWN_API bool InRunningMatch(uint32 Match, uint32 CurrentMatch, ELDMatchPhase Phase);
}

// Playtest log of one game process, owned by its local controller (docs/DEVELOPMENT.md, playtest troubleshooting).
// Every process: ten-second frame time and connection summaries (the Host also one line per client connection) and
// rate-limited hitch lines, independent of the HUD diagnostics switches. A remote client additionally logs its own view:
// the match it joined, a once-per-second own-state line, own damage, shield, statuses, rewards, kills, the public kill
// feed and result, and visible casts. That view reads only what this player may already see (the own projections,
// room-public views and the current authorized frame); the Host log holds the authority record.
class LOCKDOWN_API FLDPlaytestTrace
{
public:
    static constexpr double SummarySeconds = 10;
    static constexpr double HitchSeconds = .1;
    // Every frame of the local controller in the Demo world.
    void Tick(ALDPlayerController& PC);
    // The mark key: the next mark sequence number (from 1); a remote client also writes LD_PLAYER_MARK with its own state
    // (bLogOwnView). The Host's record of every mark comes from ALDGameMode::LogPlaytestMark.
    uint32 Mark(ALDPlayerController& PC, bool bLogOwnView);
    // The own-state fields of the self and mark lines: authority time, life, position, resources, level, gold, order and
    // action, from the own projections only.
    static FString OwnState(const ALDPlayerController& PC);

private:
    void TickPerformance(ALDPlayerController& PC, double Now);
    void LogConnections(ALDPlayerController& PC);
    void TickSelf(ALDPlayerController& PC, double Now);
    void LogJoin(const ALDPlayerController& PC);
    void LogFeedback(const ALDPlayerController& PC, const TArray<LDWorldFeedback::FEvent>& Events, const struct FLDWorldView* Frame);
    void LogStatuses(const ALDPlayerController& PC);
    void LogKillFeed(const ALDPlayerController& PC);
    // Own offer, learned build and equipment as the own growth projection shows them, whenever they change.
    void LogGrowth(const ALDPlayerController& PC);

    // ---- Performance and connections (every process).
    LDHUD::FFrameDiagnostics Frames{SummarySeconds, HitchSeconds};
    double LastFrameAt = -1;
    LDLog::FRateLimit HitchLimit{1.};
    uint32 OwnCorrections = 0;
    TWeakObjectPtr<UObject> OwnCorrectionsBody;
    // Host: per client connection, the body last counted and its server correction count then.
    struct FBodyCorrections
    {
        TWeakObjectPtr<UObject> Body;
        uint32 Count = 0;
    };
    TMap<TWeakObjectPtr<UObject>, FBodyCorrections> ServerCorrections;

    // ---- Own view (remote clients).
    uint32 Marks = 0;
    uint32 JoinedMatch = 0;
    uint32 SeenLifeMatch = 0;
    uint32 SeenLife = 0;
    uint8 SeenPhase = 0;
    LDLog::FPeriodicGate StateGate{1000};
    LDWorldFeedback::FTracker Feedback;
    TSharedPtr<const FLDDemoContentSnapshot> DisplayContent;
    bool bDisplayContentTried = false;
    uint32 StatusMatch = 0, StatusLife = 0;
    uint64 StatusRevision = 0;
    TArray<uint8> SeenStatuses;
    uint32 FeedMatch = 0;
    uint64 FeedSerial = 0;
    uint32 ResultMatch = 0;
    uint32 GrowthMatch = 0;
    FGuid SeenOffer;
    uint64 SeenBuildRevision = 0;
    uint64 SeenInventoryRevision = 0;
};
