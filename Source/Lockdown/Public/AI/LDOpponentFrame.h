#pragma once

#include "CoreMinimal.h"
#include "AI/LDOpponentPolicy.h"

struct FLDWorldView;
struct FLDDemoWorldGeometry;
struct FLDProgressionView;
struct FLDCombatOwnerView;
enum class ELDOrderStatus : uint8;

// Read-only projection of the AI's authorized inputs into one policy frame (OPPONENT_AI.md §3.2).
// Every source is a projection a human contender also receives: its own owner views, its own observation
// frame and the static public world. Nothing here reaches a world, actor or another contender.
namespace LDOpponentFrame
{
    // Static public places: approved anchors joined with the content's camp placement and monster tier.
    LOCKDOWN_API bool ReadPublicPOIs(const FLDDemoWorldGeometry& World, const FLDDemoContentSnapshot& Catalog,
        TArray<FLDOpponentPOI>& Out, FString& Error);
    // Currently authorized units of one observation frame; fields outside the authorized mask stay unset.
    LOCKDOWN_API bool ReadObservedUnits(const FLDWorldView& View, uint32 MatchGeneration,
        const FLDDemoContentSnapshot& Catalog, TArray<FLDOpponentUnit>& Out, FString& Error);
    // Own actives, blueprints, stat points and the current active offer; derived attributes come from the shared pure
    // projection over the allocated points.
    LOCKDOWN_API bool ReadOwnBuild(const FLDProgressionView& Progression, const FLDDemoContentSnapshot& Catalog,
        FLDOpponentSelf& Out, FString& Error);
    // Own resources, basic readiness, Root, the build safe window and active-source readiness exactly as Combat reports them.
    LOCKDOWN_API bool ReadOwnCombat(const FLDCombatOwnerView& Combat, int64 NowMs, FLDOpponentSelf& Out, FString& Error);
    LOCKDOWN_API ELDOpponentOrderStatus OrderStatus(ELDOrderStatus Status);
    // Content ids are lowercase by contract and FNames compare case-insensitively, but a game build prints an FName with
    // whichever spelling was registered first. Every id the policy compares as text passes here;
    // an absent id is empty.
    LOCKDOWN_API FString CanonicalId(FName Id);
}
