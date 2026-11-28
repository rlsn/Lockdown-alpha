#include "Combat/LDGroundLanding.h"
#include "Region/LDMapRegistry.h"

bool LDGroundLanding::Resolve(const ULDMapRegistry& Registry, const FVector& CasterFoot, const FVector& Requested, double RangeCm,
    const FCollisionShape& Capsule, TOptional<FVector>& Out, FString& Error)
{
    Out.Reset();
    const auto* Geometry=Registry.GetGeometry();
    if (!Registry.Enabled() || !Geometry || Geometry->Surfaces.IsEmpty())
    { Error=TEXT("A ground skill requires the registered ground surface."); return false; }
    const FName Surface(*Geometry->Surfaces[0].Id);
    const auto Fatal=[](ELDWorldQueryFailure Failure)
    { return Failure==ELDWorldQueryFailure::MissingWorld || Failure==ELDWorldQueryFailure::StaleWorld; };
    const FVector Reach=FVector(Requested.X-CasterFoot.X,Requested.Y-CasterFoot.Y,0).GetClampedToMaxSize(RangeCm);
    const int32 Steps=FMath::Max(1,FMath::CeilToInt(Reach.Size2D()/StepCm));
    FLDLegalGround CasterGround;
    bool bCasterTried=false, bCaster=false;
    for (int32 Step=0; Step<=Steps; ++Step)
    {
        FVector Point=CasterFoot+Reach*(double(Steps-Step)/Steps);
        FVector Normal;
        if (!Registry.ResolveWalkableGround(FVector2D(Point.X,Point.Y),Point,&Normal))
        {
            Registry.GetTerrain().Ground(FVector2D(Point.X,Point.Y),Point.Z,Normal);
        }
        FLDLegalGround Ground;
        ELDWorldQueryFailure Failure=ELDWorldQueryFailure::None;
        if (!Registry.QueryGround(Registry.Revision(),Point,Capsule,Surface,Ground,Failure,Error))
        {
            if (Failure!=ELDWorldQueryFailure::Blocked && Failure!=ELDWorldQueryFailure::NotGrounded
                && Failure!=ELDWorldQueryFailure::WrongSurface && Failure!=ELDWorldQueryFailure::InvalidPosition) { return false; }
            Error.Reset();
            continue;
        }
        if (!bCasterTried)
        {
            bCasterTried=true;
            ELDWorldQueryFailure CasterFailure=ELDWorldQueryFailure::None;
            bCaster=Registry.QueryGround(Registry.Revision(),CasterFoot,Capsule,Surface,CasterGround,CasterFailure,Error);
            if (!bCaster && Fatal(CasterFailure)) { return false; }
            Error.Reset();
        }
        if (!bCaster) { return true; }
        FLDCompletePath Route;
        ELDWorldQueryFailure RouteFailure=ELDWorldQueryFailure::None;
        if (!Registry.QueryCompletePath(Registry.Revision(),CasterGround,Ground,Capsule,Route,RouteFailure,Error))
        {
            if (Fatal(RouteFailure)) { return false; }
            const bool bUnjudged=RouteFailure==ELDWorldQueryFailure::MissingNavigation;
            Error.Reset();
            if (!bUnjudged) { continue; }
        }
        Out=Ground.FootCm;
        return true;
    }
    return true;
}

bool LDGroundLanding::ResolvePush(const ULDMapRegistry& Registry, const FVector& Foot, const FVector& Direction, double DistanceCm,
    const FCollisionShape& Capsule, double& OutCm, FString& Error)
{
    OutCm=0;
    const auto* Geometry=Registry.GetGeometry();
    if (!Registry.Enabled() || !Geometry || Geometry->Surfaces.IsEmpty())
    { Error=TEXT("A push requires the registered ground surface."); return false; }
    const FVector Planar=FVector(Direction.X,Direction.Y,0).GetSafeNormal();
    if (Planar.IsNearlyZero() || !FMath::IsFinite(DistanceCm) || DistanceCm<=0)
    { Error=TEXT("A push needs a planar direction and a positive distance."); return false; }
    const FName Surface(*Geometry->Surfaces[0].Id);
    const int32 Steps=FMath::Max(1,FMath::CeilToInt(DistanceCm/PushStepCm));
    double PreviousCm=0, PreviousZ=Foot.Z;
    for (int32 Step=1; Step<=Steps; ++Step)
    {
        const double Cm=FMath::Min(DistanceCm,Step*PushStepCm);
        FVector Point=Foot+Planar*Cm;
        FVector Normal;
        if (!Registry.ResolveWalkableGround(FVector2D(Point.X,Point.Y),Point,&Normal))
        {
            Registry.GetTerrain().Ground(FVector2D(Point.X,Point.Y),Point.Z,Normal);
        }
        FLDLegalGround Ground;
        ELDWorldQueryFailure Failure=ELDWorldQueryFailure::None;
        if (!Registry.QueryGround(Registry.Revision(),Point,Capsule,Surface,Ground,Failure,Error))
        {
            if (Failure!=ELDWorldQueryFailure::Blocked && Failure!=ELDWorldQueryFailure::NotGrounded
                && Failure!=ELDWorldQueryFailure::WrongSurface && Failure!=ELDWorldQueryFailure::InvalidPosition) { return false; }
            Error.Reset();
            break;
        }
        // A rise steeper than about 50 degrees between two samples is a ledge or a platform's side, not a slope.
        if (FMath::Abs(Ground.FootCm.Z-PreviousZ)>(Cm-PreviousCm)*1.2+5.) { break; }
        PreviousCm=OutCm=Cm;
        PreviousZ=Ground.FootCm.Z;
    }
    Error.Reset();
    return true;
}
