#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB92RosterProbe.generated.h"
class FJsonValue;
class ALDPlayerController;
class ALDCharacter;
class ALDWorldObject;
UCLASS()
class LOCKDOWN_API ALDB92RosterProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB92RosterProbe();
    virtual void Tick(float Delta) override;
private:
    int32 Phase=0,Failures=0;
    double Began=0;
    FGuid Seat,Tool,LifeBody;
    TWeakObjectPtr<ALDCharacter> Original;
    TWeakObjectPtr<ALDPlayerController> ReturnedHuman;
    TWeakObjectPtr<ALDWorldObject> Heavy;
    TArray<TSharedPtr<FJsonValue>> Checks;
    void Check(bool Good,const FString& Name);
    void Finish();
};
