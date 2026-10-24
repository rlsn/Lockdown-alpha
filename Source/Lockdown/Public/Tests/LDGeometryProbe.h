#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDGeometryProbe.generated.h"
class ALDPlayerController;
UCLASS()
class ALDGeometryProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDGeometryProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    FString Role;
    TArray<FString> Checks;
    TWeakObjectPtr<ALDPlayerController> A,B;
    double Started=0,At=0,LastPing=0;
    int32 Phase=0,Failures=0;
    bool Sent=false,Checked=false;
    void Check(bool Value,const FString& Message);
    void Enter(int32 Next);
    void ServerTick();
    void ClientTick();
    void Finish();
    void AuthorityEdges();
    uint32 Sequence=0;
    double ClaimStarted=0;
};
