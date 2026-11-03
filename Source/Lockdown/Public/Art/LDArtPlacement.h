#pragma once
#include "CoreMinimal.h"

class FLDDemoContentSnapshot;
class ULDArtCatalog;
struct FLDDemoWorldGeometry;

namespace LDArt
{
    // Checks every decoration against the approved D02 layout so that no prop can read as an obstacle or hide a place:
    // Outside stays off the arena floor; WallBase stays low (below the sight sample height), hugs an outer wall and keeps
    // clear of every spawn, camp, the shop and the capture circle; Capture and Shop are thin landmarks centred on their
    // anchor. Collision, sight and navigation are untouched by construction (ALDDecorationSet::IsPresentationOnly).
    LOCKDOWN_API bool CheckPlacements(const ULDArtCatalog& Catalog, const FLDDemoWorldGeometry& World,
        const FLDDemoContentSnapshot& Content, FString& Error);
    // Loads the approved content and the saved DA_DemoWorld projection, then runs CheckPlacements.
    LOCKDOWN_API bool CheckPlacementsAgainstDemoWorld(const ULDArtCatalog& Catalog, FString& Error);
    // Limits of the zone rules (cm).
    constexpr double WallBaseReachCm = 300.;
    constexpr double CaptureLandmarkHalfExtentCm = 100.;
    constexpr double ShopLandmarkHalfExtentCm = 150.;
    // A landmark's footprint box must contain its anchor up to this slack.
    constexpr double LandmarkCentreToleranceCm = 1.;
}
