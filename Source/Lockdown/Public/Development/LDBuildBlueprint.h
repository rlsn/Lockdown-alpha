#pragma once

#include "CoreMinimal.h"

struct FLDBuildProjectionInput;
struct FLDDemoContentDefinitions;

// #198 §10 end-of-match build blueprint (构筑蓝图): a versioned, read-only UTF-8 JSON record of one player's committed build
// revisions and the real facts each revision produced. It only records the recipe; it never grants an ability, restores
// a forgotten active or writes runtime state. File handling lives in LDBuildBlueprintStore.h.
namespace LDBuildBlueprint
{
    inline constexpr TCHAR Schema[] = TEXT("lockdown.build-blueprint");
    inline constexpr int32 SchemaVersion = 1;
    inline constexpr TCHAR Extension[] = TEXT(".ldbuild.json");
    inline constexpr TCHAR OriginAuto[] = TEXT("auto");
    inline constexpr TCHAR OriginExport[] = TEXT("export");
    inline constexpr TCHAR ModeDemo[] = TEXT("demo");
    // One file at most; a larger record is never truncated (the automatic save fails instead).
    inline constexpr int64 MaxFileBytes = 2 * 1024 * 1024;
    // Automatic retention: the newest MaxAutoFiles automatic records of this schema, MaxAutoBytes in total.
    inline constexpr int32 MaxAutoFiles = 50;
    inline constexpr int64 MaxAutoBytes = 100 * 1024 * 1024;
    // Owner delivery (ULDBuildBlueprintComponent): pulled in chunks; a record above the transfer bound is not sent.
    inline constexpr int32 ChunkBytes = 16 * 1024;
    inline constexpr int64 MaxTransferBytes = 8 * 1024 * 1024;
}

// Facts summed per build version. Every fact has a producer in Combat (LDCombatBlueprintLedger.cpp); a record written by an
// earlier build may still mark one as not collected (null), which is never read as 0. Private training (§16.3) adds nothing.
enum class ELDBlueprintFact : uint8
{
    // Committed positive damage of hits credited to the player against another body, once per hit (shield-absorbed part
    // included), and the part of it against other contenders.
    DamageDealt, DamageToContenders,
    // Committed positive damage the player's bodies took from another body, the part their own shield absorbed and the
    // HP actually lost.
    DamageTaken, ShieldAbsorbed, HealthLost,
    // One-time heals actually applied to the player's body (lifesteal, charms); natural regeneration is separate.
    Healing,
    // Shield amount granted to the player's body.
    ShieldGranted,
    // Released basic swings and ability releases.
    BasicAttacks, AbilityCasts,
    // HP natural (and dorm) regeneration actually restored; M12 debt heals actually repaid; what the player's own cuts
    // (S16 stance, primary guard) took off hostile hits; procs of installed components that committed a real result;
    // own combat requests refused for a gameplay gate (busy, cooldown, cadence, mana, control, target, direction, range,
    // sight).
    Regeneration, DebtRepaid, DamageReduced, Triggers, Rejections,
    Count
};

namespace LDBuildBlueprint
{
    // Stable JSON key of a fact (snake case, P2 amounts end in _p2).
    LOCKDOWN_API const TCHAR* FactKey(ELDBlueprintFact Fact);
    // Whether this build has a producer for the fact (every fact now; a parsed record keeps what its writer stated).
    LOCKDOWN_API bool IsFactCollected(ELDBlueprintFact Fact);
}

struct FLDBlueprintStat
{
    FString Stat;
    int32 Points = 0;
    bool operator==(const FLDBlueprintStat& Other) const { return Stat == Other.Stat && Points == Other.Points; }
};

struct FLDBlueprintActive
{
    // "Q", "W" or "E".
    FString Slot;
    FString Ability;
    bool operator==(const FLDBlueprintActive& Other) const { return Slot == Other.Slot && Ability == Other.Ability; }
};

struct FLDBlueprintParameter
{
    FString Key;
    FString Value;
    bool operator==(const FLDBlueprintParameter& Other) const { return Key == Other.Key && Value == Other.Value; }
};

struct FLDBlueprintComponent
{
    FString Component;
    // What the install sits on: the learned active of a mod/connector on an active or of a B18 binding, or the host
    // component of a mod/connector on a component; empty for a base without binding, the innate D or the holder itself.
    FString Host;
    // The install parameters, sorted by key, only those the install has: target_kind (active, component or dash, when a
    // mod/connector names one; a base and a holder-self mod have none, their definition tells which), element (B18: fire,
    // water or electric) and bound_component (C12's repeated base).
    TArray<FLDBlueprintParameter> Parameters;
    bool operator==(const FLDBlueprintComponent& Other) const
    { return Component == Other.Component && Host == Other.Host && Parameters == Other.Parameters; }
};

// The committed build structure a version stands for: allocated stat points (positive counts, by stat id), the Q/W/E
// actives (by slot) and the installed components (by id) with their binding parameters. Level is not structure; an
// install, move, rebinding or uninstall is, so it starts a new version.
struct FLDBlueprintStructure
{
    TArray<FLDBlueprintStat> Stats;
    TArray<FLDBlueprintActive> Actives;
    // A record written before components were recorded states false and null items; this build always writes true.
    bool bComponentsCollected = false;
    TArray<FLDBlueprintComponent> Components;
    bool operator==(const FLDBlueprintStructure& Other) const
    {
        return Stats == Other.Stats && Actives == Other.Actives && bComponentsCollected == Other.bComponentsCollected
            && Components == Other.Components;
    }
    bool operator!=(const FLDBlueprintStructure& Other) const { return !(*this == Other); }
};

struct FLDBlueprintFactValue
{
    bool bCollected = false;
    int64 Value = 0;
};

// One build version: the structure installed from FromMs until ToMs (authority ms of the match, the countdown included)
// over the Combat build revisions FirstRevision..LastRevision (a revision that only changed the level stays in it).
struct FLDBlueprintVersion
{
    uint64 FirstRevision = 0;
    uint64 LastRevision = 0;
    int64 FromMs = 0;
    int64 ToMs = 0;
    uint8 LevelFrom = 1;
    uint8 LevelTo = 1;
    // A DEV test layer (FLDBuildProjectionInput::DevBonuses) was installed during this version.
    bool bDevLayer = false;
    FLDBlueprintStructure Structure;
    FLDBlueprintFactValue Facts[int32(ELDBlueprintFact::Count)];
    FLDBlueprintVersion()
    {
        for (int32 Index = 0; Index < int32(ELDBlueprintFact::Count); ++Index)
        {
            Facts[Index].bCollected = LDBuildBlueprint::IsFactCollected(ELDBlueprintFact(Index));
        }
    }
};

// The whole record of one player and match.
struct FLDBlueprintRecord
{
    FString Origin;
    // False marks a record its writer knew to be incomplete; never set true for a partial record.
    bool bComplete = false;
    FString ProductVersion;
    int32 WireProtocol = 0;
    FString CatalogRoot;
    int32 CatalogSchemaVersion = 0;
    int32 HandlerRevision = 0;
    int32 ProjectionRevision = 0;
    // The public result id of the match (32 hex digits); never an account, address or ledger secret.
    FString MatchId;
    FString Mode;
    // "guard_succeeded" or "host_ended"; Outcome "won", "lost" or "no_winner".
    FString EndReason;
    FString Outcome;
    // The owning machine's UTC time when it received the record; retention orders automatic records by it.
    FDateTime RecordedAtUtc;
    int64 StartedAtMs = 0;
    int64 EndedAtMs = 0;
    int64 DurationMs = 0;
    uint8 FinalLevel = 0;
    int64 Kills = 0;
    int64 Deaths = 0;
    int64 Assists = 0;
    uint64 FinalRevision = 0;
    FLDBlueprintStructure FinalBuild;
    TArray<FLDBlueprintVersion> Versions;
};

enum class ELDBlueprintParseError : uint8
{
    None, TooLarge, NotUtf8, NotJson, WrongSchema, UnknownVersion, MissingField, BadField
};

// Authority-side per-contender ledger of build versions and their facts, owned by ALDCombatDirector for one match. It
// reads the build Combat installs; it is no second copy of the build's truth.
class LOCKDOWN_API FLDBuildLedger
{
public:
    void Reset();
    // The version current for Contender at AtMs with Build installed; a new version starts at AtMs when Build's structure
    // (points, actives, installed components and their binding parameters) differs from the latest one. The first version
    // of a contender starts at the authority start (0). A level-only or DEV-layer-only change stays in the current version.
    // Returns its index.
    int32 Observe(const FGuid& Contender, const FLDBuildProjectionInput& Build, int64 AtMs);
    // An effect released by Contender while Version was current; its later hits are credited to that version.
    void NoteEffect(const FGuid& EffectId, const FGuid& Contender, int32 Version);
    // The version that created EffectId for Contender, or INDEX_NONE when Contender did not release it.
    int32 EffectVersion(const FGuid& EffectId, const FGuid& Contender) const;
    // Adds a nonnegative amount to a collected fact of an existing version.
    void Add(const FGuid& Contender, int32 Version, ELDBlueprintFact Fact, int64 Amount);
    const TArray<FLDBlueprintVersion>* Find(const FGuid& Contender) const;
    // A copy of Contender's versions with the last one closed at EndMs; false when the contender has none.
    bool Close(const FGuid& Contender, int64 EndMs, TArray<FLDBlueprintVersion>& Out) const;
    static FLDBlueprintStructure StructureOf(const FLDBuildProjectionInput& Build);

private:
    TMap<FGuid, TArray<FLDBlueprintVersion>> Contenders;
    struct FEffect
    {
        FGuid Contender;
        int32 Version = INDEX_NONE;
    };
    TMap<FGuid, FEffect> Effects;
};

namespace LDBuildBlueprint
{
    // The match-level part of a record the authority fills from the frozen result.
    struct FHeader
    {
        FString ProductVersion;
        int32 WireProtocol = 0;
        FString CatalogRoot;
        int32 CatalogSchemaVersion = 0;
        int32 HandlerRevision = 0;
        int32 ProjectionRevision = 0;
        FGuid MatchId;
        FString EndReason;
        FString Outcome;
        FDateTime RecordedAtUtc;
        int64 StartedAtMs = 0;
        int64 EndedAtMs = 0;
        uint8 FinalLevel = 0;
        int64 Kills = 0;
        int64 Deaths = 0;
        int64 Assists = 0;
    };
    // A complete automatic record from the header and the contender's closed versions; the final build is the last
    // version's structure. False with Error when the versions are missing or out of order.
    LOCKDOWN_API bool Compose(const FHeader& Header, const TArray<FLDBlueprintVersion>& Versions, FLDBlueprintRecord& Out,
        FString& Error);
    // Canonical UTF-8 JSON (sorted keys, two-space indent, integer numbers only). False only for an unserializable record.
    LOCKDOWN_API bool Serialize(const FLDBlueprintRecord& Record, TArray<uint8>& Out, FString& Error);
    // Strict read: at most MaxBytes, strict UTF-8, one JSON object without duplicate keys or fractional numbers, the exact
    // schema and version, every field present and typed, no unknown field, consistent versions. Detail explains a refusal.
    LOCKDOWN_API ELDBlueprintParseError Parse(const TArray<uint8>& Bytes, int64 MaxBytes, FLDBlueprintRecord& Out,
        FString& Detail);
    // Player-facing reason of a refusal (Chinese), with Detail appended where it helps.
    LOCKDOWN_API FString DescribeError(ELDBlueprintParseError Error, const FString& Detail);
    // Explicit notices of a valid record against this machine's catalog and product: another catalog root or product
    // version, ids the local catalog no longer has (失效安装), an incomplete record. Local null adds a notice that the
    // catalog is unavailable; an empty LocalProduct skips the product comparison.
    LOCKDOWN_API void Notices(const FLDBlueprintRecord& Record, const FLDDemoContentDefinitions* Local,
        const FString& LocalProduct, TArray<FString>& Out);
}
