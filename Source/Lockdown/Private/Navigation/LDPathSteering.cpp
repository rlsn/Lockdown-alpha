#include "Navigation/LDPathSteering.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavigationData.h"
#include "Engine/World.h"

namespace LDPathSteering
{
    void HaltLocomotion(ACharacter* Body)
    {
        if (!Body) { return; }
        Body->ConsumeMovementInputVector();
        auto* Move=Body->GetCharacterMovement();
        if (Move->IsFalling()) { Move->Velocity.X=Move->Velocity.Y=0; }
        else if (!Move->HasRootMotionSources()) { Move->StopMovementImmediately(); }
    }

    double AlongPath(const TArray<FVector>& Path, const FVector& Position)
    {
        double Length=0, BestDistance=DBL_MAX, Progress=0;
        for (int32 Index=1; Index<Path.Num(); ++Index)
        {
            const FVector A=Path[Index-1], Direction=Path[Index]-A;
            const double Segment=Direction.Size2D();
            if (Segment<UE_SMALL_NUMBER) { continue; }
            const double T=FMath::Clamp(FVector::DotProduct(FVector(Position.X-A.X,Position.Y-A.Y,0),
                FVector(Direction.X,Direction.Y,0))/(Segment*Segment),0.,1.);
            const double Distance=FVector::DistSquared2D(Position,A+Direction*T);
            if (Distance<BestDistance) { BestDistance=Distance; Progress=Length+T*Segment; }
            Length+=Segment;
        }
        return Progress;
    }

    FVector Steering(const TArray<FVector>& Path, int32& Index, const ACharacter& Body)
    {
        if (!Path.IsValidIndex(Index)) { return FVector::ZeroVector; }
        const FVector Foot=Body.GetNavAgentLocation();
        // A corner is passed once reached or once the foot has crossed the plane through it normal to the incoming leg, so a
        // step longer than the tolerance never turns the body back towards a corner it already went past.
        while (Index<Path.Num()-1)
        {
            const FVector Corner=Path[Index];
            const FVector Incoming=Index>0?(Corner-Path[Index-1]).GetSafeNormal2D():FVector::ZeroVector;
            if (FVector::DistSquared2D(Foot,Corner)>FMath::Square(WaypointToleranceCm)
                && FVector::DotProduct(FVector(Foot.X-Corner.X,Foot.Y-Corner.Y,0),Incoming)<0) { break; }
            ++Index;
        }
        const FVector Delta=Path[Index]-Foot;
        // Only the final point is an arrival: intermediate corners are taken at full speed.
        const double Scale=Index<Path.Num()-1?1.:
            FMath::Clamp(Delta.Size2D()/FMath::Max(1.,Body.GetCharacterMovement()->GetMaxSpeed()*.2),.08,1.);
        return Delta.GetSafeNormal2D()*Scale;
    }

    bool AtGoal(const ACharacter& Body, const FVector& Goal, double AcceptanceCm)
    {
        const auto* Movement=Body.GetCharacterMovement();
        return Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor()
            && !Movement->HasRootMotionSources()
            && FVector::DistSquared2D(Body.GetNavAgentLocation(),Goal)<=FMath::Square(AcceptanceCm);
    }

    FVector AvoidVisibleBodies(const FVector& Foot, const FVector& Waypoint, const FVector& Preferred, double Radius,
        TConstArrayView<FBodyDisc> Others)
    {
        const FVector Direction=Preferred.GetSafeNormal2D();
        const double Remaining=FVector::Dist2D(Foot,Waypoint);
        if (Direction.IsNearlyZero() || Remaining<=UE_KINDA_SMALL_NUMBER) { return Preferred; }
        double BestAlong=DBL_MAX, BestCross=0, BestClearance=0;
        for (const FBodyDisc& Other : Others)
        {
            const FVector2D Delta=Other.Foot-FVector2D(Foot.X,Foot.Y);
            const double Along=Delta.X*Direction.X+Delta.Y*Direction.Y;
            const double Cross=Direction.X*Delta.Y-Direction.Y*Delta.X;
            const double Clearance=Radius+Other.Radius+WaypointToleranceCm;
            if (Along<=0 || Along>=Remaining || Along>=Clearance*3 || FMath::Abs(Cross)>=Clearance) { continue; }
            if (Along<BestAlong || (Along==BestAlong && Cross<BestCross))
            {
                BestAlong=Along;
                BestCross=Cross;
                BestClearance=Clearance;
            }
        }
        if (BestAlong==DBL_MAX) { return Preferred; }
        const FVector Left(-Direction.Y,Direction.X,0);
        const double Side=BestCross>=0 ? -1. : 1.;
        const double Offset=(BestClearance-FMath::Abs(BestCross))*1.5;
        return (Direction*FMath::Max(BestAlong,BestClearance)+Left*Side*Offset).GetSafeNormal2D()*Preferred.Size2D();
    }

    bool BlockedAtHeldGoal(const FVector2D& Foot, double Radius, const FVector2D& Goal, TConstArrayView<FBodyDisc> Others)
    {
        const auto Touch=[](const FVector2D& A, double RA, const FBodyDisc& B)
        { return FVector2D::DistSquared(A,B.Foot)<=FMath::Square(RA+B.Radius+WaypointToleranceCm); };
        TArray<bool> Holds;
        Holds.Init(false,Others.Num());
        TArray<int32> Open;
        for (int32 I=0; I<Others.Num(); ++I)
        {
            if (FVector2D::DistSquared(Others[I].Foot,Goal)<FMath::Square(Others[I].Radius+Radius)) { Holds[I]=true; Open.Add(I); }
        }
        while (!Open.IsEmpty())
        {
            const int32 Holder=Open.Pop(EAllowShrinking::No);
            if (Touch(Foot,Radius,Others[Holder])) { return true; }
            for (int32 I=0; I<Others.Num(); ++I)
            {
                if (!Holds[I] && Touch(Others[Holder].Foot,Others[Holder].Radius,Others[I])) { Holds[I]=true; Open.Add(I); }
            }
        }
        return false;
    }

    bool BlockedAtHeldGoal(const ACharacter& Body, const FVector& Goal)
    {
        const auto* Movement=Body.GetCharacterMovement();
        const auto* World=Body.GetWorld();
        if (!World || !Movement->IsMovingOnGround() || Movement->HasRootMotionSources()) { return false; }
        TArray<FBodyDisc> Others;
        for (TActorIterator<ACharacter> It(World); It; ++It)
        {
            const auto* Capsule=It->GetCapsuleComponent();
            if (*It==&Body || !IsValid(*It) || !Capsule || !Capsule->IsCollisionEnabled()
                || Capsule->GetCollisionResponseToChannel(ECC_Pawn)!=ECR_Block) { continue; }
            const FVector Other=It->GetNavAgentLocation();
            Others.Add({FVector2D(Other.X,Other.Y),Capsule->GetScaledCapsuleRadius()});
        }
        const FVector Foot=Body.GetNavAgentLocation();
        return BlockedAtHeldGoal(FVector2D(Foot.X,Foot.Y),Body.GetCapsuleComponent()->GetScaledCapsuleRadius(),
            FVector2D(Goal.X,Goal.Y),Others);
    }

    // #97: the registered terrain's standing height at an XY (ground, plateau or ramp), where a foot there rests.
    FVector TerrainFoot(const ULDMapRegistry& Registry, double X, double Y)
    {
        double Z=0;
        FVector Normal;
        Registry.GetTerrain().Ground(FVector2D(X,Y),Z,Normal);
        return FVector(X,Y,Z);
    }

    bool PlanStart(const ULDMapRegistry& Registry, const FLDLegalGround& BodyGround, const FCollisionShape& Capsule,
        FLDLegalGround& Out, ELDWorldQueryFailure& Failure, FString& Error)
    {
        UWorld* World=Registry.GetWorld();
        auto* Nav=World?FNavigationSystem::GetCurrent<UNavigationSystemV1>(World):nullptr;
        const ANavigationData* Data=Nav?Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate):nullptr;
        const double Reach=Capsule.GetCapsuleRadius();
        FNavLocation OnMesh;
        if (!Data || !Nav->ProjectPointToNavigation(BodyGround.SurfaceCm,OnMesh,FVector(Reach,Reach,Capsule.GetCapsuleHalfHeight()),Data)
            || FVector::DistSquared2D(OnMesh.Location,BodyGround.SurfaceCm)<=UE_KINDA_SMALL_NUMBER
            || FVector::DistSquared2D(OnMesh.Location,BodyGround.SurfaceCm)>FMath::Square(Reach))
        {
            Out=BodyGround;
            Failure=ELDWorldQueryFailure::None;
            Error.Reset();
            return true;
        }
        // Only the planar start moves: same support id, the terrain's height there, and it must itself be legal ground.
        return Registry.QueryGround(Registry.Revision(),TerrainFoot(Registry,OnMesh.Location.X,OnMesh.Location.Y),
            Capsule,BodyGround.SurfaceId,Out,Failure,Error);
    }

    // The navmesh point nearest an XY. Detour ranks only the first 128 polygons a query box gathers, in tile order, so a
    // map-wide box over a forest-cut navmesh answers with an arbitrary far point. The box grows from the body's width
    // instead; once one finds the navmesh, a last box as wide as that distance keeps any nearer point just past its edge.
    bool NearestNavPoint(UNavigationSystemV1& Nav, const ANavigationData& Data, const FVector& At, double Limit, double HalfHeight,
        FNavLocation& Out)
    {
        for (double Half=FMath::Min(Limit,100.);; Half=FMath::Min(Limit,Half*2))
        {
            if (Nav.ProjectPointToNavigation(At,Out,FVector(Half,Half,HalfHeight),&Data))
            {
                const double Found=FVector::Dist2D(At,Out.Location);
                FNavLocation Nearer;
                if (Found>Half && Nav.ProjectPointToNavigation(At,Nearer,FVector(Found,Found,HalfHeight),&Data)
                    && FVector::Dist2D(At,Nearer.Location)<Found) { Out=Nearer; }
                return true;
            }
            if (Half>=Limit) { return false; }
        }
    }

    bool ProjectableGoalFailure(ELDWorldQueryFailure Failure)
    {
        // Only a clicked point that is not itself a legal, reachable stand: never a stale/missing world or a bad request.
        return Failure==ELDWorldQueryFailure::Blocked || Failure==ELDWorldQueryFailure::NotGrounded
            || Failure==ELDWorldQueryFailure::WrongSurface || Failure==ELDWorldQueryFailure::Unreachable
            || Failure==ELDWorldQueryFailure::IncompletePath;
    }

    bool ProjectGroundGoal(const ULDMapRegistry& Registry, const FLDLegalGround& From, const FVector& Clicked, FName SurfaceId,
        const FCollisionShape& Capsule, FLDCompletePath& OutPath, FLDLegalGround& Out,
        ELDWorldQueryFailure& Failure, FString& Error)
    {
        UWorld* World=Registry.GetWorld();
        auto* Nav=World?FNavigationSystem::GetCurrent<UNavigationSystemV1>(World):nullptr;
        const ANavigationData* Data=Nav?Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate):nullptr;
        const auto* Layout=Registry.GetGeometry();
        FVector Min, Max;
        if (!Data || !Layout || Layout->Surfaces.IsEmpty() || Clicked.ContainsNaN()
            || !Layout->Surfaces[0].MinCm.ToVector(Min,Error) || !Layout->Surfaces[0].MaxCm.ToVector(Max,Error))
        {
            Failure=ELDWorldQueryFailure::MissingNavigation;
            Error=TEXT("MissingNavigation: no navigation data to project the destination onto.");
            return false;
        }
        // Search at the terrain's height under the click, across the whole surface, for the nearest navmesh point to
        // the clicked XY (a wall, a box top or a spot too close to an obstacle).
        const FVector Flat=TerrainFoot(Registry,Clicked.X,Clicked.Y);
        const double Reach=FMath::Max(Max.X-Min.X,Max.Y-Min.Y);
        FNavLocation OnMesh;
        if (!NearestNavPoint(*Nav,*Data,Flat,Reach,Capsule.GetCapsuleHalfHeight(),OnMesh))
        {
            Failure=ELDWorldQueryFailure::Unreachable;
            Error=TEXT("Unreachable: no navmesh point near the clicked destination.");
            return false;
        }

        FPathFindingQuery ReachableQuery(&Registry,*Data,From.SurfaceCm,OnMesh.Location);
        ReachableQuery.SetAllowPartialPaths(true);
        const FPathFindingResult Reachable=Nav->FindPathSync(Data->GetConfig(),ReachableQuery);
        if ((!Reachable.IsSuccessful() && !Reachable.IsPartial()) || !Reachable.Path.IsValid()
            || Reachable.Path->GetPathPoints().IsEmpty())
        {
            Failure=ELDWorldQueryFailure::Unreachable;
            Error=TEXT("Unreachable: no reachable route to a navmesh destination.");
            return false;
        }

        TArray<FVector> Candidates;
        if (Reachable.Path->IsPartial())
        {
            for (const FNavPathPoint& Point : Reachable.Path->GetPathPoints()) { Candidates.Add(Point.Location); }
            Candidates.Sort([&Flat](const FVector& A, const FVector& B)
            {
                return FVector::DistSquared2D(A,Flat)<FVector::DistSquared2D(B,Flat);
            });
        }
        else { Candidates.Add(OnMesh.Location); }

        // A partial path is confined to the start's connected navmesh. Try its closest points to the click first;
        // each candidate must still pass the same legal-ground and complete-route checks as an ordinary destination.
        for (const FVector& Base : Candidates)
        {
            const FVector Inward=(Base-Flat).GetSafeNormal2D();
            for (const double Step : { 0., 2., 5., 10. })
            {
                if (Step>0 && Inward.IsNearlyZero()) { break; }
                FNavLocation Candidate;
                if (Step==0) { Candidate.Location=Base; }
                else if (!Nav->ProjectPointToNavigation(
                    TerrainFoot(Registry,Base.X+Inward.X*Step,Base.Y+Inward.Y*Step),Candidate,
                    FVector(1.,1.,Capsule.GetCapsuleHalfHeight()),Data)) { continue; }
                if (!Registry.QueryGround(Registry.Revision(),TerrainFoot(Registry,Candidate.Location.X,Candidate.Location.Y),
                    Capsule,SurfaceId,Out,Failure,Error)) { continue; }
                FLDCompletePath CandidatePath;
                if (!Registry.QueryCompletePath(Registry.Revision(),From,Out,Capsule,CandidatePath,Failure,Error)) { continue; }
                OutPath=MoveTemp(CandidatePath);
                Failure=ELDWorldQueryFailure::None;
                Error.Reset();
                return true;
            }
        }
        Failure=ELDWorldQueryFailure::Unreachable;
        Error=TEXT("Unreachable: no legal navmesh destination has a complete route from the current body.");
        return false;
    }

    bool PlanRoute(const ULDMapRegistry& Registry, const FLDLegalGround& BodyGround, const FVector& Goal, FName SurfaceId,
        EGoal GoalKind, const FCollisionShape& Capsule, FLDCompletePath& OutPath, FLDLegalGround& OutGoal,
        ELDWorldQueryFailure& Failure, FString& Error)
    {
        FLDLegalGround From, GoalGround;
        if (!PlanStart(Registry,BodyGround,Capsule,From,Failure,Error)) { return false; }
        if (GoalKind==EGoal::Body)
        {
            return Registry.QueryGround(Registry.Revision(),Goal,Capsule,SurfaceId,GoalGround,Failure,Error)
                && PlanStart(Registry,GoalGround,Capsule,OutGoal,Failure,Error)
                && Registry.QueryCompletePath(Registry.Revision(),From,OutGoal,Capsule,OutPath,Failure,Error);
        }
        if (Registry.QueryGround(Registry.Revision(),Goal,Capsule,SurfaceId,GoalGround,Failure,Error)
            && Registry.QueryCompletePath(Registry.Revision(),From,GoalGround,Capsule,OutPath,Failure,Error))
        {
            OutGoal=GoalGround;
            return true;
        }
        if (!ProjectableGoalFailure(Failure)) { return false; }
        const FString Strict=Error;
        if (!ProjectGroundGoal(Registry,From,Goal,SurfaceId,Capsule,OutPath,GoalGround,Failure,Error))
        {
            Error=TEXT("Destination projection failed (")+Error+TEXT("); clicked point: ")+Strict;
            return false;
        }
        OutGoal=GoalGround;
        return true;
    }
}
