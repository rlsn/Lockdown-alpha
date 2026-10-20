#include "Region/LDRegionNavBounds.h"
#include "Components/BoxComponent.h"
ALDRegionNavBounds::ALDRegionNavBounds()
{
    CookedBounds=CreateDefaultSubobject<UBoxComponent>(TEXT("CookedNavigationEnvelope"));CookedBounds->SetupAttachment(GetRootComponent());CookedBounds->SetBoxExtent(FVector(1));CookedBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);CookedBounds->SetGenerateOverlapEvents(false);CookedBounds->SetCanEverAffectNavigation(false);CookedBounds->SetHiddenInGame(true);
}
void ALDRegionNavBounds::Configure(const FVector& Size){CookedBounds->SetBoxExtent(Size*.5,true);CookedBounds->UpdateBounds();}
