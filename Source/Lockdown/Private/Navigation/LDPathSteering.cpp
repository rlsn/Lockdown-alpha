#include "Navigation/LDPathSteering.h"
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
        while (Index<Path.Num()-1 && FVector::DistSquared2D(Foot,Path[Index])<=FMath::Square(WaypointToleranceCm)) { ++Index; }
        const FVector Delta=Path[Index]-Foot;
        const double Scale=FMath::Clamp(Delta.Size2D()/FMath::Max(1.,Body.GetCharacterMovement()->GetMaxSpeed()*.2),.08,1.);
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

    bool PlanRoute(const ULDMapRegistry& Registry, const FLDLegalGround& BodyGround, const FVector& Goal, FName SurfaceId,
        EGoal GoalKind, const FCollisionShape& Capsule, FLDCompletePath& OutPath, FLDLegalGround& OutGoal,
        ELDWorldQueryFailure& Failure, FString& Error)
    {
        FLDLegalGround From, GoalGround;
        if (!PlanStart(Registry,BodyGround,Capsule,From,Failure,Error)
            || !Registry.QueryGround(Registry.Revision(),Goal,Capsule,SurfaceId,GoalGround,Failure,Error)) { return false; }
        if (GoalKind==EGoal::Body)
        {
            if (!PlanStart(Registry,GoalGround,Capsule,OutGoal,Failure,Error)) { return false; }
        }
        else { OutGoal=GoalGround; }
        return Registry.QueryCompletePath(Registry.Revision(),From,OutGoal,Capsule,OutPath,Failure,Error);
    }
}
