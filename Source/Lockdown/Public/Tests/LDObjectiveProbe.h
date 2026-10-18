#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDObjectiveProbe.generated.h"
class ALDPlayerController;
class ALDObjectiveDirector;
UCLASS()
class ALDObjectiveProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDObjectiveProbe();
    virtual void Tick(float Delta) override;
private:
    ALDPlayerController* PC=nullptr;
    ALDObjectiveDirector* Director=nullptr;
    double Now=0;
    uint32 Token=0,Sequence=0;
    int32 Failures=0;
    TArray<FString> Checks;
    void Run();
    void Check(bool Pass,const FString& Text);
    void Place(FVector Point);
    void Press(uint8 Target,FGuid Id=FGuid());
    void Hold(double Seconds);
    void Refresh();
    void Release();
    FGuid VisibleSeal();
    void Finish();
};
