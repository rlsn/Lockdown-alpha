#include "Navigation/LDPathSteering.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

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
}
