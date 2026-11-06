#include "Region/LDMapGeometry.h"

#include "LDMapGeometryJson.h"
#include "../Development/LDDemoContentMath.h"

bool FLDDemoWorldPoint::ToVector(FVector& Out, FString& Error) const
{
    if (X.Denominator <= 0 || Y.Denominator <= 0 || Z.Denominator <= 0)
    {
        Error = TEXT("World coordinate has a nonpositive denominator.");
        return false;
    }
    const FVector Candidate(double(X.Numerator) / double(X.Denominator),
        double(Y.Numerator) / double(Y.Denominator), double(Z.Numerator) / double(Z.Denominator));
    if (Candidate.ContainsNaN())
    {
        Error = TEXT("World coordinate cannot be represented by the native geometry.");
        return false;
    }
    Out = Candidate;
    Error.Reset();
    return true;
}

namespace LDDemoWorld
{
bool Footprint(const FLDDemoWorldBlocker& Blocker, TArray<FVector2D>& Out, FString& Error)
{
    Out.Reset();
    if (Blocker.Shape == ELDDemoWorldBlockerShape::Polygon)
    {
        for (const FLDDemoWorldPoint& Source : Blocker.PolygonCm)
        {
            FVector Point;
            if (!Source.ToVector(Point, Error)) { Out.Reset(); return false; }
            Out.Add(FVector2D(Point.X, Point.Y));
        }
        if (Out.Num() < 3) { Error = TEXT("Blocker polygon has fewer than three vertices."); Out.Reset(); return false; }
        return true;
    }
    FVector Centre;
    if (!Blocker.CenterCm.ToVector(Centre, Error)) { return false; }
    if (Blocker.RadiusCm.Denominator <= 0 || Blocker.RadiusCm.Numerator <= 0)
    {
        Error = TEXT("Blocker cylinder has a nonpositive radius.");
        return false;
    }
    const double Radius = double(Blocker.RadiusCm.Numerator) / double(Blocker.RadiusCm.Denominator);
    const double Vertex = Radius / FMath::Cos(UE_PI / CylinderSides);
    for (int32 Index = 0; Index < CylinderSides; ++Index)
    {
        const double Angle = 2 * UE_PI * Index / CylinderSides;
        Out.Add(FVector2D(Centre.X + Vertex * FMath::Cos(Angle), Centre.Y + Vertex * FMath::Sin(Angle)));
    }
    Error.Reset();
    return true;
}

bool InsideConvex(const TArray<FVector2D>& Polygon, const FVector2D& Point)
{
    for (int32 Index = 0; Index < Polygon.Num(); ++Index)
    {
        const FVector2D& A = Polygon[Index];
        const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
        if ((B.X - A.X) * (Point.Y - A.Y) - (B.Y - A.Y) * (Point.X - A.X) < 0) { return false; }
    }
    return Polygon.Num() >= 3;
}

bool FTerrain::Build(const FLDDemoWorldGeometry& Geometry, FString& Error)
{
    *this = FTerrain();
    const auto Ring = [&Error](const TArray<FLDDemoWorldPoint>& Source, TArray<FVector2D>& Out)
    {
        for (const auto& Point : Source)
        {
            FVector Value;
            if (!Point.ToVector(Value, Error)) { return false; }
            Out.Add(FVector2D(Value.X, Value.Y));
        }
        return true;
    };
    if (Geometry.LevelStepCm.Denominator <= 0 || Geometry.LevelStepCm.Numerator <= 0)
    {
        Error = TEXT("Terrain needs a positive level step.");
        return false;
    }
    const double Step = double(Geometry.LevelStepCm.Numerator) / double(Geometry.LevelStepCm.Denominator);
    TArray<FRegion> Candidate;
    for (const auto& Plateau : Geometry.Plateaus)
    {
        FRegion& Region = Candidate.AddDefaulted_GetRef();
        Region.Tier = 2 * Plateau.Level;
        Region.Base = Step * Plateau.Level;
        if (!Ring(Plateau.PolygonCm, Region.Polygon)) { return false; }
    }
    for (const auto& Ramp : Geometry.Ramps)
    {
        FRegion& Region = Candidate.AddDefaulted_GetRef();
        Region.Tier = 2 * Ramp.LowerLevel + 1;
        Region.Base = Step * Ramp.LowerLevel;
        Region.Rise = Step;
        if (!Ring(Ramp.PolygonCm, Region.Polygon) || Region.Polygon.Num() != 4) { Error = TEXT("Invalid ramp ") + Ramp.Id; return false; }
        Region.Origin = Region.Polygon[0];
        Region.Along = (Region.Polygon[1] - Region.Polygon[0]).GetSafeNormal();
        Region.Width = FVector2D::CrossProduct(Region.Along, Region.Polygon[3] - Region.Polygon[0]);
        if (Region.Along.IsZero() || !(Region.Width > 0)) { Error = TEXT("Degenerate ramp ") + Ramp.Id; return false; }
    }
    StepCm = Step;
    Regions = MoveTemp(Candidate);
    Error.Reset();
    return true;
}

int32 FTerrain::Tier(const FVector2D& Point) const
{
    int32 Best = 0;
    for (const FRegion& Region : Regions)
    {
        if (Region.Tier > Best && InsideConvex(Region.Polygon, Point)) { Best = Region.Tier; }
    }
    return Best;
}

void FTerrain::Ground(const FVector2D& Point, double& OutZ, FVector& OutNormal) const
{
    const FRegion* Best = nullptr;
    for (const FRegion& Region : Regions)
    {
        if ((!Best || Region.Tier > Best->Tier) && InsideConvex(Region.Polygon, Point)) { Best = &Region; }
    }
    OutNormal = FVector::UpVector;
    if (!Best) { OutZ = 0; return; }
    if (Best->Rise == 0) { OutZ = Best->Base; return; }
    const double Gradient = Best->Rise / Best->Width;
    OutZ = Best->Base + Gradient * FMath::Clamp(FVector2D::CrossProduct(Best->Along, Point - Best->Origin), 0.0, Best->Width);
    const FVector2D Uphill(-Best->Along.Y, Best->Along.X);
    OutNormal = FVector(-Uphill.X * Gradient, -Uphill.Y * Gradient, 1).GetSafeNormal();
}

namespace
{
using namespace LDDemoContent;
using FPoint = FLDDemoWorldPoint;

struct FXY
{
    FExact X, Y;
};

FXY XY(const FPoint& Point, FNumbers& N) { return {N.Value(Point.X), N.Value(Point.Y)}; }
FXY Add(const FXY& A, const FXY& B, FNumbers& N) { return {N.Add(A.X, B.X), N.Add(A.Y, B.Y)}; }
FXY Sub(const FXY& A, const FXY& B, FNumbers& N) { return {N.Sub(A.X, B.X), N.Sub(A.Y, B.Y)}; }
FExact Dot(const FXY& A, const FXY& B, FNumbers& N) { return N.Add(N.Mul(A.X, B.X), N.Mul(A.Y, B.Y)); }
FExact Cross(const FXY& A, const FXY& B, FNumbers& N) { return N.Sub(N.Mul(A.X, B.Y), N.Mul(A.Y, B.X)); }
FExact DistanceSquared(const FXY& A, const FXY& B, FNumbers& N)
{
    const FXY Delta = Sub(A, B, N);
    return Dot(Delta, Delta, N);
}

bool Point(const FValue& Value, FPoint& Out, FCheck& Check, const FString& Path)
{
    FFields Fields(Check, Value, Path);
    return Rational(Fields.Take(TEXT("x")), Out.X, Check, Path + TEXT(".x"), nullptr, false, true)
        && Rational(Fields.Take(TEXT("y")), Out.Y, Check, Path + TEXT(".y"), nullptr, false, true)
        && Rational(Fields.Take(TEXT("z")), Out.Z, Check, Path + TEXT(".z"), nullptr, false, true)
        && Fields.Done();
}

bool Points(const FValue& Value, TArray<FPoint>& Out, FCheck& Check, const FString& Path, int32 Min, int32 Max)
{
    const TArray<FValue>* Rows;
    if (!Array(Value, Rows, Check, Path, Min, Max)) { return false; }
    for (const FValue& Row : *Rows)
    {
        FPoint Candidate;
        if (!Point(Row, Candidate, Check, Path)) { return false; }
        Out.Add(Candidate);
    }
    return true;
}

bool InSurface(const FPoint& Position, const FLDDemoWorldSurface& Surface, FNumbers& N)
{
    return N.LE(N.Value(Surface.MinCm.X), N.Value(Position.X)) && N.LE(N.Value(Position.X), N.Value(Surface.MaxCm.X))
        && N.LE(N.Value(Surface.MinCm.Y), N.Value(Position.Y)) && N.LE(N.Value(Position.Y), N.Value(Surface.MaxCm.Y));
}

// Strictly convex counterclockwise in plan, winding once: every vertex on or left of every edge, strictly left of
// the next one.
bool ConvexRing(const TArray<FPoint>& Ring, const FString& Id, FCheck& Check, FNumbers& N)
{
    const int32 Count = Ring.Num();
    for (int32 I = 0; I < Count; ++I)
    {
        const FXY A = XY(Ring[I], N);
        const FXY AB = Sub(XY(Ring[(I + 1) % Count], N), A, N);
        for (int32 J = 0; J < Count; ++J)
        {
            const FExact Side = Cross(AB, Sub(XY(Ring[J], N), A, N), N);
            if (!Check.Require(J == (I + 2) % Count ? N.LT(N.Value(0), Side) : N.LE(N.Value(0), Side),
                Id, TEXT("polygon must be a strictly convex counterclockwise ground polygon")))
            {
                return false;
            }
        }
    }
    return true;
}

bool CircleInSurface(const FXY& Position, const FExact& Radius, const FLDDemoWorldSurface& Surface, FNumbers& N)
{
    return N.LE(N.Add(N.Value(Surface.MinCm.X), Radius), Position.X)
        && N.LE(Position.X, N.Sub(N.Value(Surface.MaxCm.X), Radius))
        && N.LE(N.Add(N.Value(Surface.MinCm.Y), Radius), Position.Y)
        && N.LE(Position.Y, N.Sub(N.Value(Surface.MaxCm.Y), Radius));
}

bool CircleTouchesBlocker(const FXY& Position, const FExact& Radius,
    const FLDDemoWorldBlocker& Blocker, FNumbers& N)
{
    if (Blocker.Shape == ELDDemoWorldBlockerShape::Cylinder)
    {
        const FExact Reach = N.Add(Radius, N.Value(Blocker.RadiusCm));
        return N.LE(DistanceSquared(Position, XY(Blocker.CenterCm, N), N), N.Mul(Reach, Reach));
    }
    const FExact RadiusSquared = N.Mul(Radius, Radius);
    bool Inside = true;
    for (int32 I = 0; I < Blocker.PolygonCm.Num(); ++I)
    {
        const FXY A = XY(Blocker.PolygonCm[I], N), B = XY(Blocker.PolygonCm[(I + 1) % Blocker.PolygonCm.Num()], N);
        const FXY Edge = Sub(B, A, N), Relative = Sub(Position, A, N);
        const FExact Side = Cross(Edge, Relative, N);
        Inside &= N.LE(N.Value(0), Side);
        const FExact Projection = Dot(Relative, Edge, N), LengthSquared = Dot(Edge, Edge, N);
        if (N.LE(Projection, N.Value(0)))
        {
            if (N.LE(DistanceSquared(Position, A, N), RadiusSquared)) { return true; }
        }
        else if (N.LE(LengthSquared, Projection))
        {
            if (N.LE(DistanceSquared(Position, B, N), RadiusSquared)) { return true; }
        }
        else if (N.LE(N.Mul(Side, Side), N.Mul(RadiusSquared, LengthSquared)))
        {
            return true;
        }
    }
    return Inside;
}

// Closed containment of a point in a counterclockwise convex ring.
bool ExactInside(const TArray<FPoint>& Ring, const FXY& P, FNumbers& N)
{
    for (int32 I = 0; I < Ring.Num(); ++I)
    {
        const FXY A = XY(Ring[I], N), B = XY(Ring[(I + 1) % Ring.Num()], N);
        if (N.LT(Cross(Sub(B, A, N), Sub(P, A, N), N), N.Value(0))) { return false; }
    }
    return true;
}

// The closed disc lies in the closed ring: its centre inside and at least Radius from every edge line.
bool DiscInside(const TArray<FPoint>& Ring, const FXY& Centre, const FExact& Radius, FNumbers& N)
{
    for (int32 I = 0; I < Ring.Num(); ++I)
    {
        const FXY A = XY(Ring[I], N), B = XY(Ring[(I + 1) % Ring.Num()], N);
        const FXY Edge = Sub(B, A, N);
        const FExact Side = Cross(Edge, Sub(Centre, A, N), N);
        if (N.LT(Side, N.Value(0)) || N.LT(N.Mul(Side, Side), N.Mul(N.Mul(Radius, Radius), Dot(Edge, Edge, N)))) { return false; }
    }
    return true;
}

// The open disc meets the ring's open interior: the centre strictly inside, or closer than Radius to the outline.
bool DiscOverlaps(const TArray<FPoint>& Ring, const FXY& Centre, const FExact& Radius, FNumbers& N)
{
    bool Inside = true;
    const FExact Squared = N.Mul(Radius, Radius);
    for (int32 I = 0; I < Ring.Num(); ++I)
    {
        const FXY A = XY(Ring[I], N), B = XY(Ring[(I + 1) % Ring.Num()], N);
        const FXY Edge = Sub(B, A, N), Relative = Sub(Centre, A, N);
        const FExact Side = Cross(Edge, Relative, N);
        Inside &= N.LT(N.Value(0), Side);
        const FExact Projection = Dot(Relative, Edge, N), Length = Dot(Edge, Edge, N);
        if (N.LE(Projection, N.Value(0)))
        {
            if (N.LT(DistanceSquared(Centre, A, N), Squared)) { return true; }
        }
        else if (N.LE(Length, Projection))
        {
            if (N.LT(DistanceSquared(Centre, B, N), Squared)) { return true; }
        }
        else if (N.LT(N.Mul(Side, Side), N.Mul(Squared, Length))) { return true; }
    }
    return Inside;
}

// Two convex rings share interior points unless an edge of one has the whole other ring on or outside its line.
bool InteriorsOverlap(const TArray<FPoint>& First, const TArray<FPoint>& Second, FNumbers& N)
{
    for (const auto* Pair : {&First, &Second})
    {
        const TArray<FPoint>& Ring = *Pair;
        const TArray<FPoint>& Other = Pair == &First ? Second : First;
        for (int32 I = 0; I < Ring.Num(); ++I)
        {
            const FXY A = XY(Ring[I], N), Edge = Sub(XY(Ring[(I + 1) % Ring.Num()], N), A, N);
            bool Separates = true;
            for (const FPoint& Vertex : Other) { Separates &= N.LE(Cross(Edge, Sub(XY(Vertex, N), A, N), N), N.Value(0)); }
            if (Separates) { return false; }
        }
    }
    return true;
}

FExact LevelHeight(const FLDDemoWorldGeometry& World, int32 Level, FNumbers& N)
{
    return N.Mul(N.Value(World.LevelStepCm), N.Value(Level));
}

// The flat level a disc stands on wholly: inside one plateau of that level (or on the ground), and meeting no higher
// plateau and no ramp. INDEX_NONE when it straddles a cliff or a ramp.
int32 DiscLevel(const FLDDemoWorldGeometry& World, const FXY& Centre, const FExact& Radius, FNumbers& N)
{
    int32 Level = 0;
    for (const auto& Plateau : World.Plateaus)
    {
        if (Plateau.Level > Level && DiscInside(Plateau.PolygonCm, Centre, Radius, N)) { Level = Plateau.Level; }
    }
    for (const auto& Plateau : World.Plateaus)
    {
        if (Plateau.Level > Level && DiscOverlaps(Plateau.PolygonCm, Centre, Radius, N)) { return INDEX_NONE; }
    }
    for (const auto& Ramp : World.Ramps)
    {
        if (DiscOverlaps(Ramp.PolygonCm, Centre, Radius, N)) { return INDEX_NONE; }
    }
    return Level;
}

// The same for a convex footprint: every vertex inside one plateau of the level, no higher plateau or ramp met.
int32 RingLevel(const FLDDemoWorldGeometry& World, const TArray<FPoint>& Ring, FNumbers& N)
{
    int32 Level = 0;
    for (const auto& Plateau : World.Plateaus)
    {
        bool All = Plateau.Level > Level;
        for (const FPoint& Vertex : Ring) { All = All && ExactInside(Plateau.PolygonCm, XY(Vertex, N), N); }
        if (All) { Level = Plateau.Level; }
    }
    for (const auto& Plateau : World.Plateaus)
    {
        if (Plateau.Level > Level && InteriorsOverlap(Plateau.PolygonCm, Ring, N)) { return INDEX_NONE; }
    }
    for (const auto& Ramp : World.Ramps)
    {
        if (InteriorsOverlap(Ramp.PolygonCm, Ring, N)) { return INDEX_NONE; }
    }
    return Level;
}

bool ClearOfBlockers(const FXY& Position, const FExact& Radius, const FLDDemoWorldGeometry& World, FNumbers& N)
{
    if (!CircleInSurface(Position, Radius, World.Surfaces[0], N)) { return false; }
    for (const auto& Blocker : World.Blockers)
    {
        if (Blocker.BlocksMovement && CircleTouchesBlocker(Position, Radius, Blocker, N)) { return false; }
    }
    return true;
}

// A disc of standing ground at height Z: wholly on one flat level at that height, clear of every movement blocker.
bool ClearCircle(const FXY& Position, const FExact& Z, const FExact& Radius, const FLDDemoWorldGeometry& World, FNumbers& N)
{
    const int32 Level = DiscLevel(World, Position, Radius, N);
    return Level != INDEX_NONE && N.EQ(Z, LevelHeight(World, Level, N)) && ClearOfBlockers(Position, Radius, World, N);
}

bool Elevation(FFields& Fields, FLDDemoWorldGeometry& Out, FNumbers& N)
{
    FCheck& Check = Fields.Check;
    FFields Root(Check, Fields.Take(TEXT("elevation")), TEXT("world.elevation"));
    const TArray<FValue>* Plateaus = nullptr;
    const TArray<FValue>* Ramps = nullptr;
    if (!Rational(Root.Take(TEXT("level_step_cm")), Out.LevelStepCm, Check, Root.Path + TEXT(".level_step_cm"), nullptr, true)
        || !Array(Root.Take(TEXT("plateaus")), Plateaus, Check, Root.Path + TEXT(".plateaus"), 0, 64)
        || !Array(Root.Take(TEXT("ramps")), Ramps, Check, Root.Path + TEXT(".ramps"), 0, 64))
    {
        return false;
    }
    const auto& Surface = Out.Surfaces[0];
    TSet<FString> Ids;
    for (const FValue& Value : *Plateaus)
    {
        FFields Row(Check, Value, TEXT("world.plateau"));
        FLDDemoWorldPlateau Plateau;
        if (!Row.Identifier(TEXT("id"), Plateau.Id) || !Row.SmallInt(TEXT("level"), Plateau.Level, 1, LDDemoWorld::MaxLevel)
            || !Points(Row.Take(TEXT("polygon_cm")), Plateau.PolygonCm, Check, Row.Path + TEXT(".polygon_cm"), 3, 16)
            || !Row.Done() || !Check.Require(!Ids.Contains(Plateau.Id), Row.Path, TEXT("duplicate elevation id"))
            || !ConvexRing(Plateau.PolygonCm, Plateau.Id, Check, N))
        {
            return false;
        }
        const FExact Top = LevelHeight(Out, Plateau.Level, N);
        for (const FPoint& Vertex : Plateau.PolygonCm)
        {
            if (!Check.Require(N.EQ(N.Value(Vertex.Z), Top) && InSurface(Vertex, Surface, N),
                Plateau.Id, TEXT("plateau vertex must lie in the surface at its level's height"))) { return false; }
        }
        Ids.Add(Plateau.Id);
        Out.Plateaus.Add(MoveTemp(Plateau));
    }
    for (const FValue& Value : *Ramps)
    {
        FFields Row(Check, Value, TEXT("world.ramp"));
        FLDDemoWorldRamp Ramp;
        if (!Row.Identifier(TEXT("id"), Ramp.Id) || !Row.SmallInt(TEXT("lower_level"), Ramp.LowerLevel, 0, LDDemoWorld::MaxLevel - 1)
            || !Points(Row.Take(TEXT("polygon_cm")), Ramp.PolygonCm, Check, Row.Path + TEXT(".polygon_cm"), 4, 4)
            || !Row.Done() || !Check.Require(!Ids.Contains(Ramp.Id), Row.Path, TEXT("duplicate elevation id"))
            || !ConvexRing(Ramp.PolygonCm, Ramp.Id, Check, N))
        {
            return false;
        }
        const FExact Low = LevelHeight(Out, Ramp.LowerLevel, N), High = LevelHeight(Out, Ramp.LowerLevel + 1, N);
        const auto& P = Ramp.PolygonCm;
        bool Heights = true;
        for (int32 I = 0; I < 4; ++I) { Heights &= N.EQ(N.Value(P[I].Z), I < 2 ? Low : High) && InSurface(P[I], Surface, N); }
        const FXY A = XY(P[0], N), B = XY(P[1], N), C = XY(P[2], N), D = XY(P[3], N);
        const FXY LowEdge = Sub(B, A, N), HighEdge = Sub(C, D, N);
        // Run times |LowEdge|: twice the area of the parallelogram the low edge spans up to the high edge's line.
        const FExact Run = Cross(LowEdge, Sub(D, A, N), N), Rise = N.Value(Out.LevelStepCm);
        if (!Check.Require(Heights, Ramp.Id, TEXT("ramp low edge must stand at its lower level and high edge at the next"))
            || !Check.Require(N.EQ(Cross(LowEdge, HighEdge, N), N.Value(0)) && N.LT(N.Value(0), Dot(LowEdge, HighEdge, N)),
                Ramp.Id, TEXT("ramp low and high edges must be parallel"))
            || !Check.Require(N.LE(N.Mul(N.Value(LDDemoWorld::RampRun * LDDemoWorld::RampRun), N.Mul(N.Mul(Rise, Rise), Dot(LowEdge, LowEdge, N))),
                N.Mul(N.Value(LDDemoWorld::RampRise * LDDemoWorld::RampRise), N.Mul(Run, Run))),
                Ramp.Id, TEXT("ramp is steeper than bodies can walk")))
        {
            return false;
        }
        bool Joined = false, Supported = Ramp.LowerLevel == 0;
        for (const auto& Plateau : Out.Plateaus)
        {
            if (Plateau.Level > Ramp.LowerLevel && InteriorsOverlap(Plateau.PolygonCm, P, N))
            {
                return Check.Require(false, Ramp.Id, TEXT("ramp overlaps higher ground"));
            }
            if (Plateau.Level == Ramp.LowerLevel)
            {
                bool All = true;
                for (const FPoint& Vertex : P) { All &= ExactInside(Plateau.PolygonCm, XY(Vertex, N), N); }
                Supported |= All;
            }
            if (Plateau.Level != Ramp.LowerLevel + 1) { continue; }
            const auto& Ring = Plateau.PolygonCm;
            for (int32 I = 0; I < Ring.Num() && !Joined; ++I)
            {
                const FXY Start = XY(Ring[I], N), Edge = Sub(XY(Ring[(I + 1) % Ring.Num()], N), Start, N);
                const auto Along = [&](const FXY& Q)
                {
                    const FExact T = Dot(Sub(Q, Start, N), Edge, N);
                    return N.EQ(Cross(Edge, Sub(Q, Start, N), N), N.Value(0)) && N.LE(N.Value(0), T) && N.LE(T, Dot(Edge, Edge, N));
                };
                Joined = Along(C) && Along(D) && N.LT(Cross(Edge, Sub(A, Start, N), N), N.Value(0));
            }
        }
        if (!Check.Require(Joined, Ramp.Id, TEXT("ramp high edge must lie along an edge of a plateau one level up"))
            || !Check.Require(Supported, Ramp.Id, TEXT("ramp above the ground must stand wholly on a plateau of its lower level")))
        {
            return false;
        }
        for (const auto& Other : Out.Ramps)
        {
            if (!Check.Require(!InteriorsOverlap(Other.PolygonCm, P, N), Ramp.Id, TEXT("ramps overlap"))) { return false; }
        }
        Ids.Add(Ramp.Id);
        Out.Ramps.Add(MoveTemp(Ramp));
    }
    return Root.Done();
}

bool Geometry(FFields& Root, FLDDemoWorldGeometry& Out, FNumbers& N)
{
    FCheck& Check = Root.Check;
    FFields Fields(Check, Root.Take(TEXT("geometry")), TEXT("world.geometry"));
    const TArray<FValue>* Surfaces;
    if (!Array(Fields.Take(TEXT("surfaces")), Surfaces, Check, Fields.Path, 1, 1)) { return false; }
    FFields Surface(Check, (*Surfaces)[0], TEXT("world.surface"));
    FLDDemoWorldSurface Ground;
    if (!Surface.Identifier(TEXT("id"), Ground.Id)
        || !Check.Require(Ground.Id.Equals(TEXT("demo_ground"), ESearchCase::CaseSensitive), Surface.Path, TEXT("unknown Demo surface"))
        || !Point(Surface.Take(TEXT("min_cm")), Ground.MinCm, Check, Surface.Path + TEXT(".min_cm"))
        || !Point(Surface.Take(TEXT("max_cm")), Ground.MaxCm, Check, Surface.Path + TEXT(".max_cm"))
        || !Check.Require(N.LT(N.Value(Ground.MinCm.X), N.Value(Ground.MaxCm.X))
            && N.LT(N.Value(Ground.MinCm.Y), N.Value(Ground.MaxCm.Y))
            && N.EQ(N.Value(Ground.MinCm.Z), N.Value(0)) && N.EQ(N.Value(Ground.MaxCm.Z), N.Value(0)),
            Surface.Path, TEXT("Demo requires a nonempty single flat ground at Z=0")) || !Surface.Done())
    {
        return false;
    }
    Out.Surfaces.Add(MoveTemp(Ground));
    if (!Elevation(Fields, Out, N)) { return false; }
    const TArray<FValue>* Blockers;
    if (!Array(Fields.Take(TEXT("blockers")), Blockers, Check, Fields.Path, 4, 256)) { return false; }
    TSet<FString> Seen;
    // Each ground edge is closed by exactly one boundary wall: an axis-aligned rectangle outside the surface.
    TSet<int32> ClosedEdges;
    const auto& GroundSurface = Out.Surfaces[0];
    for (const FValue& Value : *Blockers)
    {
        FFields Row(Check, Value, TEXT("world.blocker"));
        FLDDemoWorldBlocker Blocker;
        FString Shape;
        if (!Row.Identifier(TEXT("id"), Blocker.Id) || !Row.Identifier(TEXT("surface_id"), Blocker.SurfaceId)
            || !Row.Text(TEXT("shape"), Shape)
            || !Check.Require(!Seen.Contains(Blocker.Id) && Blocker.SurfaceId == GroundSurface.Id,
                Row.Path, TEXT("duplicate blocker or unknown surface")))
        {
            return false;
        }
        if (Shape.Equals(TEXT("polygon"), ESearchCase::CaseSensitive))
        {
            Blocker.Shape = ELDDemoWorldBlockerShape::Polygon;
            if (!Points(Row.Take(TEXT("polygon_cm")), Blocker.PolygonCm, Check, Row.Path + TEXT(".polygon_cm"), 3, 16)) { return false; }
            if (!ConvexRing(Blocker.PolygonCm, Blocker.Id, Check, N)) { return false; }
            // A wall stands wholly on one flat level, its footprint at that level's height.
            const int32 Level = RingLevel(Out, Blocker.PolygonCm, N);
            bool Seated = Level != INDEX_NONE;
            for (const FPoint& Vertex : Blocker.PolygonCm) { Seated = Seated && N.EQ(N.Value(Vertex.Z), LevelHeight(Out, Level, N)); }
            if (!Check.Require(Seated, Blocker.Id, TEXT("blocker must stand wholly on one flat level at its height"))) { return false; }
        }
        else if (Shape.Equals(TEXT("cylinder"), ESearchCase::CaseSensitive))
        {
            Blocker.Shape = ELDDemoWorldBlockerShape::Cylinder;
            if (!Point(Row.Take(TEXT("center_cm")), Blocker.CenterCm, Check, Row.Path + TEXT(".center_cm"))
                || !Rational(Row.Take(TEXT("radius_cm")), Blocker.RadiusCm, Check, Row.Path + TEXT(".radius_cm"), nullptr, true))
            {
                return false;
            }
            const FExact Radius = N.Value(Blocker.RadiusCm);
            const FXY Centre = XY(Blocker.CenterCm, N);
            const int32 Level = N.LT(N.Value(0), Radius) ? DiscLevel(Out, Centre, Radius, N) : INDEX_NONE;
            if (!Check.Require(Level != INDEX_NONE && N.EQ(N.Value(Blocker.CenterCm.Z), LevelHeight(Out, Level, N))
                    && CircleInSurface(Centre, Radius, GroundSurface, N),
                Blocker.Id, TEXT("cylinder must stand wholly on one flat level of its ground with a positive radius")))
            {
                return false;
            }
        }
        else { return Check.Require(false, Blocker.Id, TEXT("unknown blocker shape")); }
        if (!Rational(Row.Take(TEXT("height_cm")), Blocker.HeightCm, Check, Row.Path + TEXT(".height_cm"), nullptr, true)
            || !Row.Flag(TEXT("blocks_movement"), Blocker.BlocksMovement) || !Row.Flag(TEXT("blocks_sight"), Blocker.BlocksSight)
            || !Check.Require(Blocker.BlocksMovement, Row.Path, TEXT("every blocker stops movement")) || !Row.Done())
        {
            return false;
        }
        if (Blocker.Shape == ELDDemoWorldBlockerShape::Polygon)
        {
            bool AllInside = true, AnyInside = false;
            for (const auto& Vertex : Blocker.PolygonCm)
            {
                const bool Inside = InSurface(Vertex, GroundSurface, N);
                AllInside &= Inside;
                AnyInside |= Inside && N.LT(N.Value(GroundSurface.MinCm.X), N.Value(Vertex.X)) && N.LT(N.Value(Vertex.X), N.Value(GroundSurface.MaxCm.X))
                    && N.LT(N.Value(GroundSurface.MinCm.Y), N.Value(Vertex.Y)) && N.LT(N.Value(Vertex.Y), N.Value(GroundSurface.MaxCm.Y));
            }
            if (!AllInside)
            {
                FLDDemoRational MinX = Blocker.PolygonCm[0].X, MaxX = MinX;
                FLDDemoRational MinY = Blocker.PolygonCm[0].Y, MaxY = MinY;
                for (const auto& Vertex : Blocker.PolygonCm)
                {
                    if (N.LT(N.Value(Vertex.X), N.Value(MinX))) { MinX = Vertex.X; }
                    if (N.LT(N.Value(MaxX), N.Value(Vertex.X))) { MaxX = Vertex.X; }
                    if (N.LT(N.Value(Vertex.Y), N.Value(MinY))) { MinY = Vertex.Y; }
                    if (N.LT(N.Value(MaxY), N.Value(Vertex.Y))) { MaxY = Vertex.Y; }
                }
                const bool CoversX = N.LE(N.Value(MinX), N.Value(GroundSurface.MinCm.X)) && N.LE(N.Value(GroundSurface.MaxCm.X), N.Value(MaxX));
                const bool CoversY = N.LE(N.Value(MinY), N.Value(GroundSurface.MinCm.Y)) && N.LE(N.Value(GroundSurface.MaxCm.Y), N.Value(MaxY));
                // 0 north, 1 south, 2 east, 3 west: which ground edge the rectangle closes from outside.
                const int32 Edge = CoversX && N.EQ(N.Value(MinY), N.Value(GroundSurface.MaxCm.Y)) ? 0
                    : CoversX && N.EQ(N.Value(MaxY), N.Value(GroundSurface.MinCm.Y)) ? 1
                    : CoversY && N.EQ(N.Value(MinX), N.Value(GroundSurface.MaxCm.X)) ? 2
                    : CoversY && N.EQ(N.Value(MaxX), N.Value(GroundSurface.MinCm.X)) ? 3 : INDEX_NONE;
                bool Rectangle = Blocker.PolygonCm.Num() == 4;
                for (const auto& Vertex : Blocker.PolygonCm)
                {
                    Rectangle &= (N.EQ(N.Value(Vertex.X), N.Value(MinX)) || N.EQ(N.Value(Vertex.X), N.Value(MaxX)))
                        && (N.EQ(N.Value(Vertex.Y), N.Value(MinY)) || N.EQ(N.Value(Vertex.Y), N.Value(MaxY)));
                }
                if (!Check.Require(!AnyInside && Edge != INDEX_NONE && Rectangle && Blocker.BlocksSight && !ClosedEdges.Contains(Edge),
                    Blocker.Id, TEXT("a blocker leaving its surface must be one sight-blocking axis-aligned boundary wall closing a ground edge")))
                {
                    return false;
                }
                ClosedEdges.Add(Edge);
            }
        }
        Seen.Add(Blocker.Id);
        Out.Blockers.Add(MoveTemp(Blocker));
    }
    if (!Check.Require(ClosedEdges.Num() == 4, Fields.Path, TEXT("every ground edge needs its boundary wall"))) { return false; }
    if (!Rational(Fields.Take(TEXT("sight_sample_height_cm")), Out.SightSampleHeightCm, Check,
        Fields.Path + TEXT(".sight_sample_height_cm"), nullptr, true)
        || !Rational(Fields.Take(TEXT("camp_clearance_cm")), Out.CampClearanceCm, Check,
            Fields.Path + TEXT(".camp_clearance_cm"), nullptr, true) || !Fields.Done())
    {
        return false;
    }
    int32 Highest = 0;
    for (const auto& Plateau : Out.Plateaus) { Highest = FMath::Max(Highest, Plateau.Level); }
    // The boundary stands above the highest ground by more than the sight sample, so nothing walks or sees over it.
    const FExact Rim = N.Add(LevelHeight(Out, Highest, N), N.Value(Out.SightSampleHeightCm));
    for (const auto& Blocker : Out.Blockers)
    {
        if (!Check.Require(Blocker.BlocksSight
            ? N.LT(N.Value(Out.SightSampleHeightCm), N.Value(Blocker.HeightCm))
            : N.LT(N.Value(Blocker.HeightCm), N.Value(Out.SightSampleHeightCm)),
            Blocker.Id, TEXT("sight sample height disagrees with the declared occlusion")))
        {
            return false;
        }
        bool Boundary = Blocker.Shape == ELDDemoWorldBlockerShape::Polygon;
        for (const FPoint& Vertex : Blocker.PolygonCm)
        {
            Boundary = Boundary && !(N.LT(N.Value(Out.Surfaces[0].MinCm.X), N.Value(Vertex.X)) && N.LT(N.Value(Vertex.X), N.Value(Out.Surfaces[0].MaxCm.X))
                && N.LT(N.Value(Out.Surfaces[0].MinCm.Y), N.Value(Vertex.Y)) && N.LT(N.Value(Vertex.Y), N.Value(Out.Surfaces[0].MaxCm.Y)));
        }
        if (!Check.Require(!Boundary || N.LT(Rim, N.Value(Blocker.HeightCm)), Blocker.Id,
            TEXT("boundary wall must stand taller than the highest ground plus the sight sample"))) { return false; }
    }
    return Check.Good;
}

bool Anchors(FFields& Root, const FLDDemoContentDefinitions& Content, FLDDemoWorldGeometry& Out, FNumbers& N)
{
    FCheck& Check = Root.Check;
    const TArray<FValue>* Rows;
    if (!Array(Root.Take(TEXT("anchors")), Rows, Check, TEXT("world.anchors"), 26, 26)) { return false; }
    TSet<FString> Ids, MemberIds;
    const FExact Radius = N.Value(Content.Parameters.CollisionRadiusCm);
    for (const FValue& Value : *Rows)
    {
        FFields Row(Check, Value, TEXT("world.anchor"));
        FLDDemoWorldAnchor Anchor;
        FString Kind;
        if (!Row.Identifier(TEXT("id"), Anchor.Id) || !Row.Identifier(TEXT("surface_id"), Anchor.SurfaceId)
            || !Row.Text(TEXT("kind"), Kind)
            || !Check.Require(!Ids.Contains(Anchor.Id) && Anchor.SurfaceId == Out.Surfaces[0].Id,
                Row.Path, TEXT("duplicate anchor or unknown surface"))
            || !Point(Row.Take(TEXT("position_cm")), Anchor.PositionCm, Check, Row.Path + TEXT(".position_cm"))
            || !Check.Require(InSurface(Anchor.PositionCm, Out.Surfaces[0], N),
                Anchor.Id, TEXT("position is outside its declared surface")))
        {
            return false;
        }
        if (Kind.Equals(TEXT("Spawn"), ESearchCase::CaseSensitive)) { Anchor.Kind = ELDDemoWorldAnchorKind::Spawn; }
        else if (Kind.Equals(TEXT("Camp"), ESearchCase::CaseSensitive)) { Anchor.Kind = ELDDemoWorldAnchorKind::Camp; }
        else if (Kind.Equals(TEXT("Shop"), ESearchCase::CaseSensitive)) { Anchor.Kind = ELDDemoWorldAnchorKind::Shop; }
        else if (Kind.Equals(TEXT("Capture"), ESearchCase::CaseSensitive)) { Anchor.Kind = ELDDemoWorldAnchorKind::Capture; }
        else if (Kind.Equals(TEXT("Dorm"), ESearchCase::CaseSensitive)) { Anchor.Kind = ELDDemoWorldAnchorKind::Dorm; }
        else if (Kind.Equals(TEXT("Airdrop"), ESearchCase::CaseSensitive)) { Anchor.Kind = ELDDemoWorldAnchorKind::Airdrop; }
        else if (Kind.Equals(TEXT("BossWaypoint"), ESearchCase::CaseSensitive)) { Anchor.Kind = ELDDemoWorldAnchorKind::BossWaypoint; }
        else { return Check.Require(false, Anchor.Id, TEXT("unknown anchor kind")); }
        bool KnownId = false;
        if (Anchor.Kind == ELDDemoWorldAnchorKind::Spawn)
        {
            for (int32 I = 0; I < 10; ++I) { KnownId |= Anchor.Id.Equals(FString::Printf(TEXT("s%02d"), I), ESearchCase::CaseSensitive); }
        }
        else if (Anchor.Kind == ELDDemoWorldAnchorKind::Camp)
        {
            KnownId = Anchor.Id == TEXT("l1") || Anchor.Id == TEXT("h1") || Anchor.Id == TEXT("l2")
                || Anchor.Id == TEXT("h2") || Anchor.Id == TEXT("l3");
        }
        else if (Anchor.Kind == ELDDemoWorldAnchorKind::Dorm)
        {
            KnownId = Anchor.Id == TEXT("dorm_nw") || Anchor.Id == TEXT("dorm_se");
        }
        else if (Anchor.Kind == ELDDemoWorldAnchorKind::Airdrop)
        {
            KnownId = Anchor.Id == TEXT("airdrop_ne") || Anchor.Id == TEXT("airdrop_nw") || Anchor.Id == TEXT("airdrop_s");
        }
        else if (Anchor.Kind == ELDDemoWorldAnchorKind::BossWaypoint)
        {
            for (int32 I = 1; I <= 4; ++I) { KnownId |= Anchor.Id.Equals(FString::Printf(TEXT("boss_patrol_%d"), I), ESearchCase::CaseSensitive); }
        }
        else { KnownId = Anchor.Id == (Anchor.Kind == ELDDemoWorldAnchorKind::Shop ? TEXT("shop") : TEXT("capture")); }
        if (!Check.Require(KnownId, Anchor.Id, TEXT("unknown canonical Demo anchor"))) { return false; }
        if (Anchor.Kind == ELDDemoWorldAnchorKind::Camp)
        {
            const TArray<FValue>* Members;
            if (!Array(Row.Take(TEXT("members")), Members, Check, Anchor.Id, 1, 2)) { return false; }
            for (const FValue& MemberValue : *Members)
            {
                FFields Member(Check, MemberValue, Anchor.Id + TEXT(".member"));
                FLDDemoWorldMemberHome Home;
                if (!Member.Identifier(TEXT("id"), Home.Id)
                    || !Check.Require(!MemberIds.Contains(Home.Id)
                        && Home.Id.Equals(Anchor.Id + TEXT("_") + LexToString(Anchor.Members.Num()), ESearchCase::CaseSensitive),
                        Member.Path, TEXT("duplicate or noncanonical member home ID"))
                    || !Point(Member.Take(TEXT("position_cm")), Home.PositionCm, Check, Member.Path + TEXT(".position_cm"))
                    || !Check.Require(InSurface(Home.PositionCm, Out.Surfaces[0], N), Home.Id, TEXT("member outside its surface"))
                    || !Member.Done()
                    || !Check.Require(ClearCircle(XY(Home.PositionCm, N), N.Value(Home.PositionCm.Z), N.Value(Content.Parameters.CampResetArrivalRadiusCm), Out, N),
                        Home.Id, TEXT("member return region is not clear")))
                {
                    return false;
                }
                MemberIds.Add(Home.Id);
                Anchor.Members.Add(MoveTemp(Home));
            }
        }
        if (!Row.Done()) { return false; }
        // A shop anchor stands in its stall; its point still sits at the height of the flat level beneath it.
        if (Anchor.Kind == ELDDemoWorldAnchorKind::Shop)
        {
            const int32 Level = DiscLevel(Out, XY(Anchor.PositionCm, N), N.Value(0), N);
            if (!Check.Require(Level != INDEX_NONE && N.EQ(N.Value(Anchor.PositionCm.Z), LevelHeight(Out, Level, N)),
                Anchor.Id, TEXT("shop anchor is off its level's height"))) { return false; }
        }
        if (Anchor.Kind != ELDDemoWorldAnchorKind::Shop)
        {
            const FExact Clearance = Anchor.Kind == ELDDemoWorldAnchorKind::Camp ? N.Value(Out.CampClearanceCm)
                : Anchor.Kind == ELDDemoWorldAnchorKind::Capture ? N.Value(Content.Parameters.CaptureRadiusCm)
                : Anchor.Kind == ELDDemoWorldAnchorKind::Dorm ? N.Add(Radius, N.Value(Content.Parameters.DormRadiusCm))
                : Anchor.Kind == ELDDemoWorldAnchorKind::Airdrop ? N.Add(Radius, N.Value(Content.Parameters.AirdropPickupRadiusCm)) : Radius;
            if (!Check.Require(ClearCircle(XY(Anchor.PositionCm, N), N.Value(Anchor.PositionCm.Z), Clearance, Out, N), Anchor.Id,
                TEXT("anchor clearance intersects static geometry, a cliff or a ramp, or is off its level's height")))
            {
                return false;
            }
        }
        Ids.Add(Anchor.Id);
        Out.Anchors.Add(MoveTemp(Anchor));
    }
    TSet<FString> Referenced;
    const auto Reference = [&Out, &Referenced, &Check](const FString& Id, ELDDemoWorldAnchorKind Kind)
    {
        const auto* Found = Out.Anchors.FindByPredicate([&](const auto& A) { return A.Id == Id; });
        if (!Check.Require(Found && Found->Kind == Kind && !Referenced.Contains(Id),
            Id, TEXT("unknown, repeated or wrong-kind anchor reference"))) { return false; }
        Referenced.Add(Id);
        return true;
    };
    for (const FString& Id : Content.World.SpawnIds)
    {
        if (!Reference(Id, ELDDemoWorldAnchorKind::Spawn)) { return false; }
    }
    for (const FString& Id : Content.World.CampIds)
    {
        if (!Reference(Id, ELDDemoWorldAnchorKind::Camp)) { return false; }
    }
    if (!Reference(Content.World.ShopId, ELDDemoWorldAnchorKind::Shop)
        || !Reference(Content.World.CaptureId, ELDDemoWorldAnchorKind::Capture)
        || !Check.Require(Referenced.Num() == 17, Root.Path, TEXT("unreferenced anchor")))
    {
        return false;
    }
    // D21 dorms and D26 airdrop points are the anchors content does not name; each event disk stays apart from the
    // circle, the shop, every camp clearing and every other event disk, so no two zone rules overlap.
    auto ZoneRadius = [&](ELDDemoWorldAnchorKind Kind) -> TOptional<FExact>
    {
        switch (Kind)
        {
        case ELDDemoWorldAnchorKind::Capture: return N.Value(Content.Parameters.CaptureRadiusCm);
        case ELDDemoWorldAnchorKind::Shop: return N.Value(Content.Parameters.ShopInteractRadiusCm);
        case ELDDemoWorldAnchorKind::Camp: return N.Value(Out.CampClearanceCm);
        case ELDDemoWorldAnchorKind::Dorm: return N.Value(Content.Parameters.DormRadiusCm);
        case ELDDemoWorldAnchorKind::Airdrop: return N.Value(Content.Parameters.AirdropPickupRadiusCm);
        default: return {};
        }
    };
    int32 Dorms = 0, Airdrops = 0, Waypoints = 0;
    for (const auto& Zone : Out.Anchors)
    {
        Waypoints += Zone.Kind == ELDDemoWorldAnchorKind::BossWaypoint ? 1 : 0;
        if (Zone.Kind != ELDDemoWorldAnchorKind::Dorm && Zone.Kind != ELDDemoWorldAnchorKind::Airdrop) { continue; }
        ++(Zone.Kind == ELDDemoWorldAnchorKind::Dorm ? Dorms : Airdrops);
        const FExact Own = ZoneRadius(Zone.Kind).GetValue();
        for (const auto& Other : Out.Anchors)
        {
            const TOptional<FExact> OtherRadius = ZoneRadius(Other.Kind);
            if (&Other == &Zone || !OtherRadius.IsSet()) { continue; }
            const FExact Reach = N.Add(Own, OtherRadius.GetValue());
            if (!Check.Require(N.LT(N.Mul(Reach, Reach), DistanceSquared(XY(Zone.PositionCm, N), XY(Other.PositionCm, N), N)),
                Zone.Id, TEXT("event disk overlaps the capture, shop, a camp clearing or another event disk")))
            {
                return false;
            }
        }
    }
    if (!Check.Require(Dorms == 2 && Airdrops == 3 && Waypoints == 4 && N.LT(N.Value(0), N.Value(Content.Parameters.DormRadiusCm))
        && N.LT(N.Value(0), N.Value(Content.Parameters.AirdropPickupRadiusCm)),
        Root.Path, TEXT("exactly two dorms, three airdrop points and four Boss waypoints, with positive radii, are required")))
    {
        return false;
    }
    TSet<FString> Placed;
    TSet<ELDDemoCampTier> Tiers;
    for (const auto& Placement : Content.Camps)
    {
        const auto* Monster = Content.Monsters.FindByPredicate([&](const auto& M) { return M.Id == Placement.MonsterId; });
        const auto* Anchor = Out.Anchors.FindByPredicate([&](const auto& A) { return A.Id == Placement.AnchorId; });
        if (!Check.Require(Monster && Anchor && Anchor->Kind == ELDDemoWorldAnchorKind::Camp && !Placed.Contains(Placement.AnchorId),
                Placement.AnchorId, TEXT("invalid camp/monster reference"))
            || !Check.Require(Monster->CountPerCamp == Anchor->Members.Num(), Placement.AnchorId, TEXT("member-home count differs from content")))
        {
            return false;
        }
        Placed.Add(Placement.AnchorId);
        Tiers.Add(Monster->Tier);
    }
    return Check.Require(Placed.Num() == 5 && Tiers.Num() == 2 && MemberIds.Num() == 8,
        Root.Path, TEXT("five camps must bind eight members across both tiers"));
}

bool Placement(FFields& Root, const FLDDemoContentDefinitions& Content, FLDDemoWorldGeometry& Out, FNumbers& N)
{
    FCheck& Check = Root.Check;
    FFields Fields(Check, Root.Take(TEXT("placement")), TEXT("world.placement"));
    if (!Points(Fields.Take(TEXT("spawn_offsets_cm")), Out.SpawnOffsetsCm, Check, Fields.Path + TEXT(".spawn_offsets_cm"), 7, 7)
        || !Rational(Fields.Take(TEXT("spawn_validation_radius_cm")), Out.SpawnValidationRadiusCm, Check,
            Fields.Path + TEXT(".spawn_validation_radius_cm"), nullptr, true) || !Fields.Done())
    {
        return false;
    }
    const FExact Radius = N.Value(Content.Parameters.CollisionRadiusCm);
    const FExact Diameter = N.Mul(Radius, N.Value(2)), DiameterSquared = N.Mul(Diameter, Diameter);
    const FExact Envelope = N.Sub(N.Value(Out.SpawnValidationRadiusCm), Radius);
    if (!Check.Require(N.LT(N.Value(0), Envelope), Fields.Path, TEXT("spawn envelope is smaller than the body"))) { return false; }
    for (int32 I = 0; I < Out.SpawnOffsetsCm.Num(); ++I)
    {
        const auto& Offset = Out.SpawnOffsetsCm[I];
        const FXY Delta = XY(Offset, N);
        if (!Check.Require(N.EQ(N.Value(Offset.Z), N.Value(0)) && N.LE(Dot(Delta, Delta, N), N.Mul(Envelope, Envelope)),
            Fields.Path, TEXT("spawn offset escapes its validation envelope")))
        {
            return false;
        }
        if (I == 0 && !Check.Require(N.EQ(Delta.X, N.Value(0)) && N.EQ(Delta.Y, N.Value(0)),
            Fields.Path, TEXT("first spawn placement must be the selected anchor"))) { return false; }
        for (int32 J = 0; J < I; ++J)
        {
            if (!Check.Require(N.LE(DiameterSquared, DistanceSquared(Delta, XY(Out.SpawnOffsetsCm[J], N), N)),
                Fields.Path, TEXT("local spawn placements overlap"))) { return false; }
        }
        for (const auto& Anchor : Out.Anchors)
        {
            if (Anchor.Kind == ELDDemoWorldAnchorKind::Spawn
                && !Check.Require(ClearCircle(Add(XY(Anchor.PositionCm, N), Delta, N), N.Value(Anchor.PositionCm.Z), Radius, Out, N),
                    Anchor.Id, TEXT("local spawn placement is not statically clear")))
            {
                return false;
            }
        }
    }
    return Check.Good;
}

}

bool Project(const LDDemoContent::FValue& Value, const FLDDemoContentDefinitions& Content,
    FLDDemoWorldGeometry& Out, LDDemoContent::FCheck& Check)
{
    FFields Root(Check, Value, TEXT("world"));
    FNumbers N(Check);
    FLDDemoWorldGeometry Candidate;
    int64 Revision;
    if (!Root.Int(TEXT("schema_version"), Revision, 3, 3) || !Root.Int(TEXT("projection_revision"), Revision, 3, 3)
        || !Root.Identifier(TEXT("map_id"), Candidate.MapId)
        || !Check.Require(Candidate.MapId == Content.World.MapId && Candidate.MapId.Equals(TEXT("demo_world"), ESearchCase::CaseSensitive),
            Root.Path, TEXT("content references another map"))
        || !Root.SmallInt(TEXT("layout_revision"), Candidate.LayoutRevision, 1, MAX_int32)
        || !Root.SmallInt(TEXT("geometry_revision"), Candidate.GeometryRevision, 1, MAX_int32)
        || !Root.Digest(TEXT("source_sha256"), Candidate.SourceDigest)
        || !Root.Digest(TEXT("author_schema_sha256"), Candidate.AuthorSchemaDigest))
    {
        return false;
    }
    FFields Approval(Check, Root.Take(TEXT("approval")), TEXT("world.approval"));
    if (!Approval.Literal(TEXT("status"), TEXT("approved")) || !Approval.Text(TEXT("decision"), Candidate.ApprovalDecision)
        || !Check.Require(!Candidate.ApprovalDecision.TrimStartAndEnd().IsEmpty(),
            Approval.Path, TEXT("empty decision reference")) || !Approval.Done())
    {
        return false;
    }
    const TArray<FValue>* Refs;
    const TCHAR* Expected[] = {
        TEXT("AIRDROP.PickupRadiusCm"), TEXT("CAMP.ResetArrivalRadiusCm"), TEXT("CAPTURE.RadiusCm"), TEXT("DORM.RadiusCm"),
        TEXT("SHOP.InteractRadiusCm"),
        TEXT("UNIT.BasicAttackRangeCm"), TEXT("UNIT.CollisionRadiusCm"), TEXT("VISION.RadiusCm")};
    constexpr int32 RefCount = UE_ARRAY_COUNT(Expected);
    if (!Array(Root.Take(TEXT("parameter_refs")), Refs, Check, TEXT("world.parameter_refs"), RefCount, RefCount)) { return false; }
    for (int32 I = 0; I < RefCount; ++I)
    {
        FString Ref;
        if (!String((*Refs)[I], Ref, Check, TEXT("world.parameter_refs"))
            || !Check.Require(Ref.Equals(Expected[I], ESearchCase::CaseSensitive),
                TEXT("world.parameter_refs"), TEXT("missing, duplicate or unsorted shared parameter reference"))) { return false; }
        Candidate.ParameterRefs.Add(MoveTemp(Ref));
    }
    if (!Geometry(Root, Candidate, N) || !Anchors(Root, Content, Candidate, N)
        || !Placement(Root, Content, Candidate, N) || !Root.Done() || !Check.Good)
    {
        return false;
    }
    Out = MoveTemp(Candidate);
    return true;
}
}
