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
    const double ShopRadius = PlaceValue(Parameters.ShopInteractRadiusCm), SightHeight = PlaceValue(World.SightSampleHeightCm);
    const double CampClearance = PlaceValue(World.CampClearanceCm), SpawnRadius = PlaceValue(World.SpawnValidationRadiusCm);
    if (!(Body > 0 && CaptureRadius > 0 && ShopRadius > 0 && SightHeight > 0 && CampClearance > 0 && SpawnRadius > 0))
    {
        return PlaceFail(Error, TEXT("The approved body, capture, shop, sight, camp and spawn sizes are required to check decorations."));
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
        if (Anchor.Kind == ELDDemoWorldAnchorKind::Dorm) { continue; }
        FPlaceKeepOut& KeepOut = KeepOuts.AddDefaulted_GetRef();
        KeepOut.Id = Anchor.Id;
        KeepOut.Kind = Anchor.Kind;
        if (!Anchor.PositionCm.ToVector(KeepOut.Centre, Error)) { return false; }
        switch (Anchor.Kind)
        {
        case ELDDemoWorldAnchorKind::Spawn: KeepOut.Radius = SpawnRadius + SpawnReach + Body; break;
        case ELDDemoWorldAnchorKind::Camp: KeepOut.Radius = CampClearance; break;
        case ELDDemoWorldAnchorKind::Shop: KeepOut.Radius = ShopRadius + Body; break;
        case ELDDemoWorldAnchorKind::Capture: KeepOut.Radius = CaptureRadius + Body; break;
        default: return PlaceFail(Error, TEXT("Unknown Demo world anchor kind: ") + Anchor.Id);
        }
    }
    TArray<FBox> Blockers;
    for (const FLDDemoWorldBlocker& Blocker : World.Blockers)
    {
        FBox Box(ForceInit);
        for (const FLDDemoWorldPoint& Point : Blocker.PolygonCm)
        {
            FVector Value;
            if (!Point.ToVector(Value, Error)) { return false; }
            Box += Value;
        }
        Blockers.Add(Box);
    }
    TMap<FString, const FLDArtProp*> Props;
    for (const FLDArtProp& Prop : Catalog.Props) { Props.Add(Prop.Id, &Prop); }
    for (int32 Index = 0; Index < Catalog.Decorations.Num(); ++Index)
    {
        const FLDArtDecoration& Decoration = Catalog.Decorations[Index];
        const FString Label = FString::Printf(TEXT("Decoration %d (%s, %s)"), Index, *Decoration.Prop, *LDArt::ZoneId(Decoration.Zone));
        const FLDArtProp* Prop = Props.FindRef(Decoration.Prop);
        if (!Prop || !Prop->Mesh) { return PlaceFail(Error, Label + TEXT(" has no prop mesh.")); }
        const FBox Box = Prop->Mesh->GetBoundingBox().TransformBy(Decoration.Transform);
        const bool bOverFloor = Box.Max.X > FloorMin.X && Box.Min.X < FloorMax.X && Box.Max.Y > FloorMin.Y && Box.Min.Y < FloorMax.Y;
        switch (Decoration.Zone)
        {
        case ELDArtZone::Outside:
            if (bOverFloor) { return PlaceFail(Error, Label + TEXT(" reaches over the arena floor.")); }
            break;
        case ELDArtZone::WallBase:
        {
            const bool bOnFloor = Box.Min.X >= FloorMin.X && Box.Max.X <= FloorMax.X && Box.Min.Y >= FloorMin.Y && Box.Max.Y <= FloorMax.Y;
            const double Reach = FMath::Min(FMath::Min(Box.Min.X - FloorMin.X, FloorMax.X - Box.Max.X), FMath::Min(Box.Min.Y - FloorMin.Y, FloorMax.Y - Box.Max.Y));
            if (!bOnFloor || Reach > WallBaseReachCm)
            { return PlaceFail(Error, FString::Printf(TEXT("%s must stand on the floor within %.0f cm of an outer wall."), *Label, WallBaseReachCm)); }
            if (Box.Max.Z > SightHeight)
            { return PlaceFail(Error, FString::Printf(TEXT("%s is %.0f cm tall, above the %.0f cm sight sample; it would read as a sight blocker."), *Label, Box.Max.Z, SightHeight)); }
            for (const FPlaceKeepOut& KeepOut : KeepOuts)
            {
                if (PlaceDistance2D(Box, KeepOut.Centre) < KeepOut.Radius)
                { return PlaceFail(Error, FString::Printf(TEXT("%s is within %.0f cm of %s."), *Label, KeepOut.Radius, *KeepOut.Id)); }
            }
            for (int32 Blocker = 0; Blocker < Blockers.Num(); ++Blocker)
            {
                if (PlaceOverlap2D(Box, Blockers[Blocker])) { return PlaceFail(Error, Label + TEXT(" overlaps ") + World.Blockers[Blocker].Id); }
            }
            break;
        }
        case ELDArtZone::Capture:
        case ELDArtZone::Shop:
        {
            const bool bCapture = Decoration.Zone == ELDArtZone::Capture;
            const ELDDemoWorldAnchorKind Kind = bCapture ? ELDDemoWorldAnchorKind::Capture : ELDDemoWorldAnchorKind::Shop;
            const FPlaceKeepOut* Anchor = KeepOuts.FindByPredicate([Kind](const FPlaceKeepOut& KeepOut) { return KeepOut.Kind == Kind; });
            const double Limit = bCapture ? CaptureLandmarkHalfExtentCm : ShopLandmarkHalfExtentCm;
            // The transform carries the footprint pivot, so the anchor must lie within the landmark itself.
            if (!Anchor || PlaceDistance2D(Box, Anchor->Centre) > LandmarkCentreToleranceCm)
            { return PlaceFail(Error, Label + TEXT(" is not centred on its world anchor.")); }
            if (FMath::Max(Box.GetExtent().X, Box.GetExtent().Y) > Limit)
            { return PlaceFail(Error, FString::Printf(TEXT("%s is wider than a %.0f cm landmark around its anchor."), *Label, Limit)); }
            if (!bCapture && Box.Max.Z > OccluderHeightCm)
            { return PlaceFail(Error, FString::Printf(TEXT("%s is %.0f cm tall; at the shop it would hide bodies above %.0f cm."), *Label, Box.Max.Z, OccluderHeightCm)); }
            break;
        }
        default:
            return PlaceFail(Error, Label + TEXT(" has no placement zone."));
        }
    }
    Error.Reset();
    return true;
}

bool LDArt::CheckPlacementsAgainstDemoWorld(const ULDArtCatalog& Catalog, FString& Error)
{
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if (!LDDemoEntry::LoadRuntimeContent(Content, Error) || !Content)
    {
        return PlaceFail(Error, TEXT("Approved Demo content is unavailable for decoration checks: ") + Error);
    }
    const auto* Definition = LoadObject<ULDMapDefinition>(nullptr, *(FString(LDDemoEntry::WorldDefinition) + TEXT(".DA_DemoWorld")));
    TSharedPtr<const FLDDemoWorldGeometry> World;
    if (!Definition || !Definition->Read(*Content, World, Error) || !World)
    {
        return PlaceFail(Error, TEXT("The saved Demo world projection is unavailable for decoration checks: ") + Error);
    }
    return CheckPlacements(Catalog, *World, *Content, Error);
}
