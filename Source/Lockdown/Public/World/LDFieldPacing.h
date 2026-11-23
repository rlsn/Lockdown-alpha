#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Framework/LDPacingTypes.h"
#include "LDFieldPacing.generated.h"

struct FLDDemoContentParameters;

// Spec §10.3 (P6) mid-field objectives. Timed field segment 0 and 2 hold a spray mini flag at a mini_flag_* point drawn
// uniformly; segment 1 holds the arena around the flagpole. Each opens OBJECTIVE.StartSec into its segment (field
// progress T), announced OBJECTIVE.AnnounceLeadSec earlier, and closes after its window. Every instant is field progress T,
// so a rest pauses it like every other field timer.
UENUM()
enum class ELDObjectiveKind : uint8 { None = 0, MiniFlag = 1, Arena = 2 };

UENUM()
enum class ELDObjectivePhase : uint8 { None = 0, Announced = 1, Open = 2, Won = 3, Expired = 4 };

// Public read model (inside FLDPublicMatchView). Serial is the segment + 1 (0: none this segment yet).
USTRUCT()
struct LOCKDOWN_API FLDObjectiveView
{
    GENERATED_BODY()
    UPROPERTY() uint8 Serial = 0;
    UPROPERTY() ELDObjectiveKind Kind = ELDObjectiveKind::None;
    UPROPERTY() ELDObjectivePhase Phase = ELDObjectivePhase::None;
    UPROPERTY() FName AnchorId;
    UPROPERTY() FVector CentreCm = FVector::ZeroVector;
    UPROPERTY() float RadiusCm = 0;
    // Field progress T of the opening and of the close.
    UPROPERTY() int64 OpensAtT = 0;
    UPROPERTY() int64 ClosesAtT = 0;
    // Mini flag: the sole sprayer and its accumulated spray (public like the flagpole's spray); bContested while two or
    // more stand inside.
    UPROPERTY() FGuid Sprayer;
    UPROPERTY() int64 SprayMs = 0;
    UPROPERTY() int64 SprayNeedMs = 0;
    UPROPERTY() bool bContested = false;
    UPROPERTY() FGuid Winner;
};

// Public view of one delivery locker that is out now (inside FLDPublicMatchView): where it stands, the field progress T at
// which it leaves unopened, and whether someone is holding it (who is never named).
USTRUCT()
struct LOCKDOWN_API FLDLockerView
{
    GENERATED_BODY()
    UPROPERTY() FName AnchorId;
    UPROPERTY() FVector PositionCm = FVector::ZeroVector;
    UPROPERTY() int64 ClosesAtT = 0;
    UPROPERTY() bool bChanneling = false;
};

// Owner-only P6 projection: the death screen's spawn candidates and choice (spec §10.4).
USTRUCT()
struct LOCKDOWN_API FLDOwnerPacingView
{
    GENERATED_BODY()
    UPROPERTY() uint32 SpawnLife = 0;
    UPROPERTY() TArray<FName> SpawnCandidates;
    UPROPERTY() TArray<FVector> SpawnCandidateCm;
    UPROPERTY() int8 SpawnChosen = INDEX_NONE;
    UPROPERTY() int64 SpawnChoiceEndsAtMs = 0;
};

namespace LDFieldPacing
{
    struct FPoint
    {
        FName Id;
        FVector PositionCm = FVector::ZeroVector;
    };

    // A living contender as the field rules see it at a boundary: its committed foot, whether it is protected, whether a
    // hard control (root, stun, taunt, knockback) landed on it and whether it took damage since the last boundary.
    struct FPresence
    {
        FGuid Contender;
        FVector FootCm = FVector::ZeroVector;
        bool bProtected = false;
        bool bControlled = false;
        bool bDamaged = false;
    };

    // The objective kind of timed segment Segment (0..2).
    LOCKDOWN_API ELDObjectiveKind KindOfSegment(int32 Segment);
    // splitmix-style index draw in [0, Count) from a seed and a salt; INDEX_NONE when Count is not positive.
    LOCKDOWN_API int32 Draw(uint64 Seed, uint64 Salt, int32 Count);
}

// The match's mid-field objective (one at a time).
struct LOCKDOWN_API FLDObjectiveState
{
    int64 SegmentMs = 0, StartMs = 0, AnnounceLeadMs = 0, FlagWindowMs = 0, FlagSprayMs = 0, ArenaMs = 0;
    double FlagRadiusCm = 0, ArenaRadiusCm = 0;
    TArray<LDFieldPacing::FPoint> MiniFlags;
    FVector ArenaCentreCm = FVector::ZeroVector;
    uint64 Seed = 0;
    FLDObjectiveView View;
    // Field progress T of the last spray accounting.
    int64 LastStepT = 0;
    // Arena: damage each contender dealt to contenders while both stood in the circle, and its last such hit (field T).
    TMap<FGuid, int64> ArenaDamageP2;
    TMap<FGuid, int64> ArenaLastHitT;

    bool IsActive() const { return SegmentMs > 0 && Seed != 0; }
    bool Begin(const FLDDemoContentParameters& Parameters, TConstArrayView<LDFieldPacing::FPoint> Flags, const FVector& Arena,
        uint64 InSeed, FString& Error);
    // Field progress T of the next announce, open or close of a timed segment's objective, MAX_int64 when none is left.
    int64 NextDueT(int64 NowT) const;
    // Moves to the objective NowT falls in: announces, opens and expires at inclusive boundaries. True when the view changed.
    bool Advance(int64 NowT);
    // Mini flag spray between the last step and NowT (spec §10.3: alone in the circle accumulates; two or more pause it;
    // leaving or a hard control clears it). True when the sprayer won at this step (View.Winner set).
    bool StepFlag(int64 NowT, TConstArrayView<LDFieldPacing::FPresence> Living, bool& bChanged);
    // Arena: one committed contender-on-contender hit (both feet inside the open circle).
    void NoteArenaDamage(const FGuid& Attacker, int64 DamageP2, int64 NowT);
    // Arena close: among the contenders alive inside the circle, the most damage dealt inside, ties by the earlier last hit
    // (then the smaller id); invalid when nobody qualifies.
    FGuid ArenaWinner(TConstArrayView<LDFieldPacing::FPresence> Living) const;
    bool IsInside(const FVector& FootCm) const;
};

// Spec §10.4 (P6): the dead contender's candidate spawns (drawn without replacement from the spawn anchors) and its choice.
struct LOCKDOWN_API FLDSpawnChoice
{
    uint32 Life = 0;
    TArray<FName> Candidates;
    int32 Chosen = INDEX_NONE;
    int64 DeadlineMs = 0;
    // The anchor the next life uses: the chosen one, else the first.
    FName Resolved() const { return Candidates.IsValidIndex(Chosen) ? Candidates[Chosen] : Candidates.IsEmpty() ? NAME_None : Candidates[0]; }
};

namespace LDFieldPacing
{
    // Count distinct anchors drawn uniformly without replacement (Fisher-Yates prefix of the seeded stream).
    LOCKDOWN_API void DrawCandidates(uint64 Seed, uint64 Salt, TConstArrayView<FName> Anchors, int32 Count, TArray<FName>& Out);
    // Spec §13: the AI chooses the candidate nearer the nearest public goal (an open/announced objective centre or the flagpole).
    LOCKDOWN_API int32 NearestCandidate(TConstArrayView<FVector> Candidates, TConstArrayView<FVector> Goals);
}

// Spec §11.3 delivery lockers: like the airdrop, a public random drop, but held rather than touched. EXPLORE.LockerCount of
// the locker_* points are drawn per match (server-only), each coming out at its own uniform field T in
// [EXPLORE.LockerEarliestSec, EXPLORE.LockerLatestSec] and leaving unopened EXPLORE.LockerStaySec later. While it is out it
// is public; standing within EXPLORE.LockerRadiusCm for EXPLORE.LockerChannelSec opens it; damage or a hard control
// interrupts. Each opens once per match.
struct LOCKDOWN_API FLDLockerState
{
    struct FLocker
    {
        LDFieldPacing::FPoint Point;
        int64 AppearT = 0;
        int64 LeaveT = 0;
        bool bOpened = false;
        // Whether the last Step saw it out (its public presence changes when this flips).
        bool bShown = false;
        // The contender channelling it and the field T the channel started (invalid: none).
        FGuid Opener;
        int64 ChannelSinceT = 0;

        bool IsOut(int64 NowT) const { return !bOpened && NowT >= AppearT && NowT < LeaveT; }
    };
    TArray<FLocker> Lockers;
    double RadiusCm = 0;
    int64 ChannelMs = 0;

    bool Begin(const FLDDemoContentParameters& Parameters, TConstArrayView<LDFieldPacing::FPoint> Points, uint64 Seed, FString& Error);
    // One boundary of channels: a locker that is out and has no channel starts one for the inside contender with the smallest
    // id; the channel breaks when its opener leaves the disk, takes damage or is hard-controlled, or the locker leaves, and
    // opens the locker once it ran ChannelMs. Opened lists the lockers opened at NowT and their openers; bChanged when any
    // channel or presence changed.
    void Step(int64 NowT, TConstArrayView<LDFieldPacing::FPresence> Living, TArray<TPair<int32, FGuid>>& Opened, bool& bChanged);
    // Earliest field T after NowT at which a locker comes out or leaves, or a running channel completes (MAX_int64 when none).
    int64 NextDueT(int64 NowT) const;
    // The lockers out at NowT.
    void View(int64 NowT, TArray<FLDLockerView>& Out) const;
};

// Spec §11.3 second-hand dealer: one per match at a uniform field T in [EXPLORE.PeddlerEarliestSec, PeddlerLatestSec], beside a
// uniform camp anchor, leaving EXPLORE.PeddlerStaySec later.
struct LOCKDOWN_API FLDPeddlerPlan
{
    int64 AppearT = 0;
    int64 LeaveT = 0;
    FName CampAnchor;
    double OffsetAngleRad = 0;
    bool bSpawned = false;
    bool bGone = false;
    // Field T before which a spawn whose placement was blocked is not attempted again.
    int64 RetryT = 0;

    bool Plan(const FLDDemoContentParameters& Parameters, TConstArrayView<FName> Camps, uint64 Seed, FString& Error);
};
