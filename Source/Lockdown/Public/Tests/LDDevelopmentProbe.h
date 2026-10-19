#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vision/LDViewTypes.h"
#include "LDDevelopmentProbe.generated.h"

class ALDPlayerController;

struct FLDDevelopmentResetEvidence
{
    int32 Round=0,Failures=0;
    uint32 Generation=0;
    FLDOwnerState PreviousOwner;
    TArray<TWeakObjectPtr<AActor>> OldActors;
    TArray<FString> Checks;
};

UCLASS()
class ALDDevelopmentProbe : public AActor
{
    GENERATED_BODY()
public:
    ALDDevelopmentProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    FString Role;
    TArray<FString> Checks;
    TWeakObjectPtr<ALDPlayerController> A,B;
    double Started=0,PhaseAt=0;
    double LastPing=0;
    int32 Phase=0,Failures=0;
    bool bChoiceSent=false,bReadySent=false,bStartSent=false,bChecked=false;
    void Check(bool Pass,const FString& Description);
    void LocalCheck();
    void ResetCheck();
    void ServerTick();
    void ClientTick();
    void Enter(int32 Next);
    void Finish();
};
