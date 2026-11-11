#include "World/LDTrainingRoomDisplay.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

ALDTrainingRoomDisplay::ALDTrainingRoomDisplay()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(false);
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    for (int32 Index = 0; Index < 4; ++Index)
    {
        UStaticMeshComponent* Line = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("TrainingZoneLine%d"), Index));
        Line->SetupAttachment(Root);
        Line->SetStaticMesh(Cube.Object);
        Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (Index < 2)
        {
            Line->SetRelativeLocation(FVector(7500, Index == 0 ? 500 : 900, 1));
            Line->SetRelativeScale3D(FVector(24, .04, .02));
        }
        else
        {
            Line->SetRelativeLocation(FVector(Index == 2 ? 6300 : 8700, 700, 1));
            Line->SetRelativeScale3D(FVector(.04, 4, .02));
        }
        ZoneLines.Add(Line);
    }
}
