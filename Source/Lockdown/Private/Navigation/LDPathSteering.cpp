#include "Navigation/LDPathSteering.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
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
        // Only the planar start moves: same support id and foot height, and it must itself be legal ground.
        return Registry.QueryGround(Registry.Revision(),FVector(OnMesh.Location.X,OnMesh.Location.Y,BodyGround.FootCm.Z),
            Capsule,BodyGround.SurfaceId,Out,Failure,Error);
    }

    bool ProjectableGoalFailure(ELDWorldQueryFailure Failure)
    {
        // Only a clicked point that is not itself a legal, reachable stand: never a stale/missing world or a bad request.
        return Failure==ELDWorldQueryFailure::Blocked || Failure==ELDWorldQueryFailure::NotGrounded
            || Failure==ELDWorldQueryFailure::WrongSurface || Failure==ELDWorldQueryFailure::Unreachable
            || Failure==ELDWorldQueryFailure::IncompletePath;
    }

    bool ProjectGroundGoal(const ULDMapRegistry& Registry, const FLDLegalGround& From, const FVector& Clicked, FName SurfaceId,
        const FCollisionShape& Capsule, FLDLegalGround& Out, ELDWorldQueryFailure& Failure, FString& Error)
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
        // The single registered surface is flat: search at its height, across the whole surface, for the nearest
        // navmesh point to the clicked XY (a wall, a box top or a spot too close to an obstacle).
        const FVector Flat(Clicked.X,Clicked.Y,From.FootCm.Z);
        const double Reach=FMath::Max(Max.X-Min.X,Max.Y-Min.Y);
        FNavLocation OnMesh;
        if (!Nav->ProjectPointToNavigation(Flat,OnMesh,FVector(Reach,Reach,Capsule.GetCapsuleHalfHeight()),Data))
        {
            Failure=ELDWorldQueryFailure::Unreachable;
            Error=TEXT("Unreachable: no navmesh point near the clicked destination.");
            return false;
        }
        // Recast erodes conservatively; if the nearest edge point still touches geometry, step further into the mesh
        // (away from the clicked point) and re-project so the goal stays exactly on the mesh.
        const FVector Inward=(OnMesh.Location-Flat).GetSafeNormal2D();
        for (const double Step : { 0., 2., 5., 10. })
        {
            if (Step>0 && Inward.IsNearlyZero()) { break; }
            FNavLocation Candidate=OnMesh;
            if (Step>0 && !Nav->ProjectPointToNavigation(FVector(OnMesh.Location.X,OnMesh.Location.Y,From.FootCm.Z)+Inward*Step,
                Candidate,FVector(1.,1.,Capsule.GetCapsuleHalfHeight()),Data)) { continue; }
            if (Registry.QueryGround(Registry.Revision(),FVector(Candidate.Location.X,Candidate.Location.Y,From.FootCm.Z),
                Capsule,SurfaceId,Out,Failure,Error)) { return true; }
        }
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
        if (!ProjectGroundGoal(Registry,From,Goal,SurfaceId,Capsule,GoalGround,Failure,Error)
            || !Registry.QueryCompletePath(Registry.Revision(),From,GoalGround,Capsule,OutPath,Failure,Error))
        {
            Error=TEXT("Destination projection failed (")+Error+TEXT("); clicked point: ")+Strict;
            return false;
        }
        OutGoal=GoalGround;
        return true;
    }
}
