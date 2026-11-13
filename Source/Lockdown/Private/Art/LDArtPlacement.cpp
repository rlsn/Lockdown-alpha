#include "Art/LDArtPlacement.h"
#include "Art/LDArtCatalog.h"
#include "Development/LDDemoContentCatalog.h"
#include "Engine/StaticMesh.h"
#include "Framework/LDDemoEntry.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDMapGeometry.h"

namespace
{
    bool PlaceFail(FString& Error, const FString& Message) { Error = Message; return false; }

    double PlaceValue(const FLDDemoRational& Value) { return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : -1.; }

    double PlaceDistance2D(const FBox& Box, const FVector& Point)
    {
        const double DX = FMath::Max3(Box.Min.X - Point.X, 0., Point.X - Box.Max.X);
        const double DY = FMath::Max3(Box.Min.Y - Point.Y, 0., Point.Y - Box.Max.Y);
        return FMath::Sqrt(DX * DX + DY * DY);
    }

    bool PlaceOverlap2D(const FBox& A, const FBox& B)
    {
        return A.Min.X < B.Max.X && B.Min.X < A.Max.X && A.Min.Y < B.Max.Y && B.Min.Y < A.Max.Y;
    }

    struct FPlaceKeepOut
    {
        FString Id;
        ELDDemoWorldAnchorKind Kind = ELDDemoWorldAnchorKind::Spawn;
        FVector Centre = FVector::ZeroVector;
        double Radius = 0.;
    };
}

bool LDArt::CheckPlacements(const ULDArtCatalog& Catalog, const FLDDemoWorldGeometry& World, const FLDDemoContentSnapshot& Content, FString& Error)
{
    if (World.Surfaces.Num() != 1) { return PlaceFail(Error, TEXT("The Demo world must have its single floor surface.")); }
    FVector FloorMin, FloorMax;
    if (!World.Surfaces[0].MinCm.ToVector(FloorMin, Error) || !World.Surfaces[0].MaxCm.ToVector(FloorMax, Error)) { return false; }
    const auto& Parameters = Content.Get().Parameters;
    const double Body = PlaceValue(Parameters.CollisionRadiusCm), CaptureRadius = PlaceValue(Parameters.CaptureRadiusCm);
    const double SightHeight = PlaceValue(World.SightSampleHeightCm);
    const double AirdropRadius = PlaceValue(Parameters.AirdropPickupRadiusCm);
    const double CampClearance = PlaceValue(World.CampClearanceCm), SpawnRadius = PlaceValue(World.SpawnValidationRadiusCm);
    if (!(Body > 0 && CaptureRadius > 0 && SightHeight > 0 && CampClearance > 0 && SpawnRadius > 0 && AirdropRadius > 0))
    {
        return PlaceFail(Error, TEXT("The approved body, capture, sight, camp and spawn sizes are required to check decorations."));
    }
    double SpawnReach = 0.;
    for (const FLDDemoWorldPoint& Offset : World.SpawnOffsetsCm)
    {
        FVector Value;
        if (!Offset.ToVector(Value, Error)) { return false; }
        SpawnReach = FMath::Max(SpawnReach, Value.Size2D());
    }
    // Places a body must reach or a player must read: a decoration on the floor keeps clear of all of them.
    TArray<FPlaceKeepOut> KeepOuts;
    for (const FLDDemoWorldAnchor& Anchor : World.Anchors)
    {
        // D21: a dorm is only a judgment disk at the wall base; collision-free low furniture there neither blocks a body
        // nor hides one, and the dorm's own ring marks it.
        // D26: a Boss waypoint is a patrol target only; props keep the path open through the navmesh like anywhere else.
        // #198 session D: a PK spawn or the throw anchor is a placement target only; collision-free decorations never block
        // the body the registry places there.
        if (Anchor.Kind == ELDDemoWorldAnchorKind::Dorm || Anchor.Kind == ELDDemoWorldAnchorKind::BossWaypoint
            || Anchor.Kind == ELDDemoWorldAnchorKind::PKSpawn || Anchor.Kind == ELDDemoWorldAnchorKind::PKThrow
            || Anchor.Kind == ELDDemoWorldAnchorKind::PKSignup) { continue; }
        FPlaceKeepOut& KeepOut = KeepOuts.AddDefaulted_GetRef();
        KeepOut.Id = Anchor.Id;
        KeepOut.Kind = Anchor.Kind;
        if (!Anchor.PositionCm.ToVector(KeepOut.Centre, Error)) { return false; }
        switch (Anchor.Kind)
        {
        case ELDDemoWorldAnchorKind::Spawn:
        case ELDDemoWorldAnchorKind::RestSpawn: KeepOut.Radius = SpawnRadius + SpawnReach + Body; break;
        case ELDDemoWorldAnchorKind::FinalSpawn: KeepOut.Radius = SpawnRadius + SpawnReach + Body; break;
        case ELDDemoWorldAnchorKind::Camp: KeepOut.Radius = CampClearance; break;
        case ELDDemoWorldAnchorKind::Capture: KeepOut.Radius = CaptureRadius + Body; break;
        case ELDDemoWorldAnchorKind::FinalCapture: KeepOut.Radius = CaptureRadius + Body; break;
        // The pickup and service disks stay clear so their occupants and approaching bodies read plainly.
        case ELDDemoWorldAnchorKind::Airdrop:
        case ELDDemoWorldAnchorKind::RestService: KeepOut.Radius = AirdropRadius + Body; break;
        case ELDDemoWorldAnchorKind::TrainingDummy: KeepOut.Radius = SpawnRadius + Body; break;
        default: return PlaceFail(Error, TEXT("Unknown Demo world anchor kind: ") + Anchor.Id);
        }
    }
    TArray<FBox> Blockers;
    TArray<FString> BlockerIds;
    TMap<FString, const FLDDemoWorldBlocker*> BlockersById;
    for (const FLDDemoWorldBlocker& Blocker : World.Blockers)
    {
        BlockersById.Add(Blocker.Id, &Blocker);
        TArray<FVector2D> Footprint;
        if (!LDDemoWorld::Footprint(Blocker, Footprint, Error)) { return false; }
        FBox Box(ForceInit);
        for (const FVector2D& Point : Footprint) { Box += FVector(Point.X, Point.Y, 0); }
        Blockers.Add(Box);
        BlockerIds.Add(Blocker.Id);
    }
    // The east-side rest room extends the shared floor beyond the field wall. Decorations in the closed-off strip between
    // them remain background, but neither the field nor the accessible rest room may be covered by outside scenery.
    FBox FieldFloor(FloorMin, FloorMax), RestFloor(ForceInit);
    if (World.Anchors.ContainsByPredicate([](const FLDDemoWorldAnchor& Anchor) { return Anchor.Kind == ELDDemoWorldAnchorKind::RestSpawn; }))
    {
        const auto WallBox = [&](const TCHAR* Id) -> const FBox*
        {
            const FLDDemoWorldBlocker* Wall = BlockersById.FindRef(Id);
            const int32 Index = BlockerIds.IndexOfByKey(FString(Id));
            return Wall && Wall->BlocksMovement && Wall->BlocksSight && Index != INDEX_NONE ? &Blockers[Index] : nullptr;
        };
        const FBox* FieldEast = WallBox(TEXT("wall_e"));
        const FBox* RestWest = WallBox(TEXT("rest_wall_w"));
        const FBox* RestEast = WallBox(TEXT("rest_wall_e"));
        const FBox* RestSouth = WallBox(TEXT("rest_wall_s"));
        const FBox* RestNorth = WallBox(TEXT("rest_wall_n"));
        if (!FieldEast || !RestWest || !RestEast || !RestSouth || !RestNorth)
        {
            return PlaceFail(Error, TEXT("The field and shared rest room need their sight- and movement-blocking walls for art placement."));
        }
        FieldFloor.Max.X = FieldEast->Min.X;
        RestFloor = FBox(FVector(RestWest->Max.X, RestSouth->Max.Y, FloorMin.Z),
            FVector(RestEast->Min.X, RestNorth->Min.Y, FloorMax.Z));
        if (!(FieldFloor.Max.X > FieldFloor.Min.X && FieldFloor.Max.X <= RestFloor.Min.X
            && RestFloor.Min.X < RestFloor.Max.X && RestFloor.Min.Y < RestFloor.Max.Y
            && RestFloor.Min.X >= FloorMin.X && RestFloor.Max.X <= FloorMax.X
            && RestFloor.Min.Y >= FloorMin.Y && RestFloor.Max.Y <= FloorMax.Y
            && FieldEast->Min.Y <= FloorMin.Y && FieldEast->Max.Y >= FloorMax.Y
            && RestWest->Min.Y <= RestFloor.Min.Y && RestWest->Max.Y >= RestFloor.Max.Y
            && RestEast->Min.Y <= RestFloor.Min.Y && RestEast->Max.Y >= RestFloor.Max.Y
            && RestSouth->Min.X <= RestFloor.Min.X && RestSouth->Max.X >= RestFloor.Max.X
            && RestNorth->Min.X <= RestFloor.Min.X && RestNorth->Max.X >= RestFloor.Max.X))
        {
            return PlaceFail(Error, TEXT("The field and shared rest room walls do not enclose valid separate art placement areas."));
        }
    }
    // #97: a wall-base decoration stands wholly on one flat level, at that level's height.
    LDDemoWorld::FTerrain Terrain;
    if (!Terrain.Build(World, Error)) { return false; }
    TMap<FString, const FLDArtProp*> Props;
    for (const FLDArtProp& Prop : Catalog.Props) { Props.Add(Prop.Id, &Prop); }
    for (int32 Index = 0; Index < Catalog.Decorations.Num(); ++Index)
    {
        const FLDArtDecoration& Decoration = Catalog.Decorations[Index];
        const FString Label = FString::Printf(TEXT("Decoration %d (%s, %s)"), Index, *Decoration.Prop, *LDArt::ZoneId(Decoration.Zone));
        const FLDArtProp* Prop = Props.FindRef(Decoration.Prop);
        if (!Prop || !Prop->Mesh) { return PlaceFail(Error, Label + TEXT(" has no prop mesh.")); }
        const FBox Box = Prop->Mesh->GetBoundingBox().TransformBy(Decoration.Transform);
        switch (Decoration.Zone)
        {
        case ELDArtZone::Outside:
            if (PlaceOverlap2D(Box, FieldFloor) || (RestFloor.IsValid && PlaceOverlap2D(Box, RestFloor)))
            { return PlaceFail(Error, Label + TEXT(" reaches over the arena floor or shared rest room.")); }
            break;
        case ELDArtZone::WallBase:
        case ELDArtZone::Ground:
        {
            // Ground cover follows every wall-base rule except hugging an outer wall: low, flat, clear of places and blocks.
            const bool bOnFloor = Box.Min.X >= FloorMin.X && Box.Max.X <= FloorMax.X && Box.Min.Y >= FloorMin.Y && Box.Max.Y <= FloorMax.Y;
            if (!bOnFloor) { return PlaceFail(Error, Label + TEXT(" must stand on the arena floor.")); }
            if (Decoration.Zone == ELDArtZone::WallBase)
            {
                const auto NearWall = [&Box](const FBox& Area)
                {
                    if (!Area.IsValid || Box.Min.X < Area.Min.X || Box.Max.X > Area.Max.X
                        || Box.Min.Y < Area.Min.Y || Box.Max.Y > Area.Max.Y) { return false; }
                    const double Reach = FMath::Min(FMath::Min(Box.Min.X - Area.Min.X, Area.Max.X - Box.Max.X),
                        FMath::Min(Box.Min.Y - Area.Min.Y, Area.Max.Y - Box.Max.Y));
                    return Reach <= WallBaseReachCm;
                };
                if (!NearWall(FieldFloor) && !NearWall(RestFloor))
                { return PlaceFail(Error, FString::Printf(TEXT("%s must stand on the floor within %.0f cm of an outer wall."), *Label, WallBaseReachCm)); }
            }
            const int32 Tier = Terrain.Tier(FVector2D(Box.GetCenter()));
            bool bFlat = Tier % 2 == 0;
            for (const FVector2D Corner : { FVector2D(Box.Min.X, Box.Min.Y), FVector2D(Box.Max.X, Box.Min.Y),
                                             FVector2D(Box.Max.X, Box.Max.Y), FVector2D(Box.Min.X, Box.Max.Y) })
            {
                bFlat &= Terrain.Tier(Corner) == Tier;
            }
            const double Floor = Terrain.StepCm * (Tier / 2);
            // The prop's footprint pivot puts its lowest point on the floor it names.
            if (!bFlat || FMath::Abs(Box.Min.Z - Floor) > 1.)
            { return PlaceFail(Error, FString::Printf(TEXT("%s must stand wholly on one flat level, at its %.0f cm floor."), *Label, Floor)); }
            if (Box.Max.Z - Box.Min.Z > SightHeight)
            { return PlaceFail(Error, FString::Printf(TEXT("%s is %.0f cm tall, above the %.0f cm sight sample; it would read as a sight blocker."), *Label, Box.Max.Z - Box.Min.Z, SightHeight)); }
            for (const FPlaceKeepOut& KeepOut : KeepOuts)
            {
                if (PlaceDistance2D(Box, KeepOut.Centre) < KeepOut.Radius)
                { return PlaceFail(Error, FString::Printf(TEXT("%s is within %.0f cm of %s."), *Label, KeepOut.Radius, *KeepOut.Id)); }
            }
            for (int32 Blocker = 0; Blocker < Blockers.Num(); ++Blocker)
            {
                if (PlaceOverlap2D(Box, Blockers[Blocker])) { return PlaceFail(Error, Label + TEXT(" overlaps ") + BlockerIds[Blocker]); }
            }
            break;
        }
        case ELDArtZone::Capture:
        {
            // The landmark sits on the nearest capture anchor.
            const FPlaceKeepOut* Anchor = nullptr;
            for (const FPlaceKeepOut& KeepOut : KeepOuts)
            {
                if (KeepOut.Kind == ELDDemoWorldAnchorKind::Capture && (!Anchor || PlaceDistance2D(Box, KeepOut.Centre) < PlaceDistance2D(Box, Anchor->Centre)))
                {
                    Anchor = &KeepOut;
                }
            }
            // The transform carries the footprint pivot, so the anchor must lie within the landmark itself.
            if (!Anchor || PlaceDistance2D(Box, Anchor->Centre) > LandmarkCentreToleranceCm)
            { return PlaceFail(Error, Label + TEXT(" is not centred on its world anchor.")); }
            if (FMath::Max(Box.GetExtent().X, Box.GetExtent().Y) > CaptureLandmarkHalfExtentCm)
            { return PlaceFail(Error, FString::Printf(TEXT("%s is wider than a %.0f cm landmark around its anchor."), *Label, CaptureLandmarkHalfExtentCm)); }
            break;
        }
        case ELDArtZone::Blocker:
        {
            // #97: collision follows the art. The prop stands on the block's base at the block's height, and its own
            // footprint rectangle, turned with it, is the block's polygon; a tree's prop stands over its trunk.
            const FLDDemoWorldBlocker* Target = BlockersById.FindRef(Decoration.Blocker);
            if (!Target) { return PlaceFail(Error, Label + TEXT(" names no world blocker: ") + Decoration.Blocker); }
            const bool bTree = Target->Shape == ELDDemoWorldBlockerShape::Cylinder;
            if (!bTree && Target->PolygonCm.IsEmpty()) { return PlaceFail(Error, Label + TEXT(" draws a block without a polygon.")); }
            FVector Base;
            if (!(bTree ? Target->CenterCm : Target->PolygonCm[0]).ToVector(Base, Error)) { return false; }
            const double Height = PlaceValue(Target->HeightCm);
            if (FMath::Abs(Box.Min.Z - Base.Z) > BlockerFitToleranceCm || FMath::Abs(Box.Max.Z - Box.Min.Z - Height) > BlockerFitToleranceCm)
            {
                return PlaceFail(Error, FString::Printf(TEXT("%s spans %.1f..%.1f cm; %s stands %.1f..%.1f cm."), *Label, Box.Min.Z, Box.Max.Z,
                    *Target->Id, Base.Z, Base.Z + Height));
            }
            if (bTree)
            {
                if (!Box.IsInsideXY(Base)) { return PlaceFail(Error, Label + TEXT(" does not stand over its tree ") + Target->Id); }
                break;
            }
            const FBox Local = Prop->Mesh->GetBoundingBox();
            TArray<FVector2D> Corners;
            for (const FVector2D Corner : { FVector2D(Local.Min.X, Local.Min.Y), FVector2D(Local.Max.X, Local.Min.Y),
                                             FVector2D(Local.Max.X, Local.Max.Y), FVector2D(Local.Min.X, Local.Max.Y) })
            {
                Corners.Add(FVector2D(Decoration.Transform.TransformPosition(FVector(Corner, Local.Min.Z))));
            }
            TArray<FVector2D> Polygon;
            if (!LDDemoWorld::Footprint(*Target, Polygon, Error)) { return false; }
            bool bFits = Polygon.Num() == Corners.Num();
            for (const FVector2D& Vertex : Polygon)
            {
                bFits &= Corners.ContainsByPredicate([&Vertex](const FVector2D& Corner) { return FVector2D::Distance(Corner, Vertex) <= BlockerFitToleranceCm; });
            }
            if (!bFits)
            {
                return PlaceFail(Error, FString::Printf(TEXT("%s footprint (%s / %s / %s / %s) is not the polygon of %s."), *Label, *Corners[0].ToString(),
                    *Corners[1].ToString(), *Corners[2].ToString(), *Corners[3].ToString(), *Target->Id));
            }
            break;
        }
        default:
            return PlaceFail(Error, Label + TEXT(" has no placement zone."));
        }
    }
    // A hidden block is drawn by Outside props instead, so it must not cross either playable area.
    for (const FString& Id : Catalog.HiddenBlockers)
    {
        const int32 Index = BlockerIds.IndexOfByKey(Id);
        if (Index == INDEX_NONE) { return PlaceFail(Error, TEXT("Hidden blocker is not a world blocker: ") + Id); }
        const FBox& Box = Blockers[Index];
        if (PlaceOverlap2D(Box, FieldFloor) || (RestFloor.IsValid && PlaceOverlap2D(Box, RestFloor)))
        {
            return PlaceFail(Error, TEXT("Hidden blocker reaches over the arena floor or shared rest room: ") + Id);
        }
    }
    Error.Reset();
    return true;
}

bool LDArt::LoadDemoWorld(TSharedPtr<const FLDDemoContentSnapshot>& Content, TSharedPtr<const FLDDemoWorldGeometry>& World, FString& Error)
{
    if (!LDDemoEntry::LoadRuntimeContent(Content, Error) || !Content)
    {
        return PlaceFail(Error, TEXT("Approved Demo content is unavailable for decoration checks: ") + Error);
    }
    const auto* Definition = LoadObject<ULDMapDefinition>(nullptr, *(FString(LDDemoEntry::WorldDefinition) + TEXT(".DA_DemoWorld")));
    if (!Definition || !Definition->Read(*Content, World, Error) || !World)
    {
        return PlaceFail(Error, TEXT("The saved Demo world projection is unavailable for decoration checks: ") + Error);
    }
    return true;
}

bool LDArt::CheckPlacementsAgainstDemoWorld(const ULDArtCatalog& Catalog, FString& Error)
{
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TSharedPtr<const FLDDemoWorldGeometry> World;
    return LoadDemoWorld(Content, World, Error) && CheckPlacements(Catalog, *World, *Content, Error);
}
