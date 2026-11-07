#pragma once
#include "CoreMinimal.h"

class FLDDemoContentSnapshot;
class ULDArtCatalog;
struct FLDDemoWorldGeometry;

namespace LDArt
{
    // Checks every decoration against the approved D02 layout so that no prop can read as an obstacle or hide a place:
    // Outside stays off the arena floor; WallBase stays low (below the sight sample height), hugs an outer wall and keeps
    // clear of every spawn, camp, the shop and the capture circle; Ground follows the WallBase rules anywhere on the floor;
    // Capture and Shop are thin landmarks covering their anchor, and the shop landmark stays below OccluderHeightCm;
    // a Blocker prop stands exactly on its block (#97: collision follows the art). Hidden blockers lie off the floor. Collision, sight and navigation are untouched by
    // construction (ALDDecorationSet::IsPresentationOnly).
    LOCKDOWN_API bool CheckPlacements(const ULDArtCatalog& Catalog, const FLDDemoWorldGeometry& World,
        const FLDDemoContentSnapshot& Content, FString& Error);
    // Loads the approved content and the saved DA_DemoWorld projection.
    LOCKDOWN_API bool LoadDemoWorld(TSharedPtr<const FLDDemoContentSnapshot>& Content, TSharedPtr<const FLDDemoWorldGeometry>& World, FString& Error);
    // Loads the approved content and the saved DA_DemoWorld projection, then runs CheckPlacements.
    LOCKDOWN_API bool CheckPlacementsAgainstDemoWorld(const ULDArtCatalog& Catalog, FString& Error);
    // Limits of the zone rules (cm).
    constexpr double WallBaseReachCm = 300.;
    constexpr double CaptureLandmarkHalfExtentCm = 100.;
    constexpr double ShopLandmarkHalfExtentCm = 150.;
    // Above this height a prop can hide a standing body from the fixed -60 degree camera (APP_SPEC 8). Only Outside
    // props, which never stand between that camera and the arena floor, and the thin capture flagpole may exceed it.
    constexpr double OccluderHeightCm = 150.;
    // #97: a blocker's prop matches the block's base, height and polygon corners within this slack.
    constexpr double BlockerFitToleranceCm = 2.;
    // A landmark's footprint box must contain its anchor up to this slack.
    constexpr double LandmarkCentreToleranceCm = 1.;
}
