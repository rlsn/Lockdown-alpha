#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Navigation/LDNavigationComponent.h"
#include "LDNavigationProbe.generated.h"

class ALDPlayerController;
class ALDCharacter;
UCLASS()
class LOCKDOWN_API ALDNavigationProbe : public AActor
{
    GENERATED_BODY()
public:
    ALDNavigationProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    FString Role;
    TWeakObjectPtr<ALDPlayerController> A,B;
    TWeakObjectPtr<ALDCharacter> TargetA,TargetB;
    TArray<FString> Checks;
    int32 Phase=0,Failures=0,Step=0;
    double Started=0,PhaseAt=0,LastPing=0,MaxSpeed=0;
    bool bReady=false,bSent=false;
    FVector Anchor=FVector::ZeroVector;
    FLDMoveIdentity Old;
    uint32 QueryStart=0;
    uint32 Corrections=0,LastCorrections=0;
    double MaxCorrection=0;
    TWeakObjectPtr<ALDCharacter> CorrectionBody;
    TArray<TWeakObjectPtr<AActor>> Blockers;
    TArray<TWeakObjectPtr<class ALDBook>> PickupBooks;
    uint32 ReplanStart=0,RegularCorrections=0;
    uint32 ServerQueries=0,ServerRejectedMoves=0;
    double MaxRegularCorrection=0;
    TMap<int32,uint32> PhaseCorrections;
    TMap<int32,double> PhaseMaxCorrection;
    void Check(bool Pass,const FString& Text);
    void Enter(int32 Next);
    void Place(const FVector& First,const FVector& Second);
    void ServerTick();
    void ClientTick();
    void Finish();
};
