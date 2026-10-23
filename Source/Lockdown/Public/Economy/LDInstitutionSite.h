#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDInstitutionSite.generated.h"
// Deliberately not a combat body/purse: a public protected service marker.
UCLASS()
class LOCKDOWN_API ALDInstitutionSite:public AActor
{
    GENERATED_BODY()
public:
    ALDInstitutionSite();
    uint8 Site=0,Status=0; // 0 idle, 1 work, 2 empty, 3 no cash, 4 full, 5 allocation failure
    double WorkUntil=0;
    static FVector Position(const UWorld* World,uint8 Site);
    static FString Name(uint8 Site);
    static FString StatusName(uint8 Status);
};
