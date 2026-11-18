#include "Combat/LDCombatBagBlocker.h"
#include "Components/BoxComponent.h"

ALDCombatBagBlocker::ALDCombatBagBlocker()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
    SetHidden(true);
    Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
    RootComponent = Box;
    Box->SetCollisionObjectType(ECC_WorldDynamic);
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    Box->SetCanEverAffectNavigation(false);
    Box->SetGenerateOverlapEvents(false);
    Box->SetHiddenInGame(true);
}

void ALDCombatBagBlocker::Shape(const FVector& Foot, const FVector2D& Heading, double HalfLengthCm, double HalfWidthCm, double Height)
{
    const FVector2D Along = Heading.IsNearlyZero() ? FVector2D(1, 0) : Heading.GetSafeNormal();
    SetActorLocationAndRotation(Foot + FVector(0, 0, Height * .5), FRotator(0, FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X)), 0));
    Box->SetBoxExtent(FVector(FMath::Max(HalfLengthCm, 1.), FMath::Max(HalfWidthCm, 1.), Height * .5));
}
