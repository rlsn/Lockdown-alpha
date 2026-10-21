#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDInputProbe.generated.h"
class ALDPlayerController;
class ALDCharacter;
UCLASS()
class LOCKDOWN_API ALDInputProbe : public AActor
{
    GENERATED_BODY()
public:
    ALDInputProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    FString Role;
    int32 Phase=0,Step=0,Failures=0;
    double Started=0,PhaseAt=0,LastPing=0;
    bool bChoice=false,bReady=false;
    TArray<FString> Checks;
    TWeakObjectPtr<ALDPlayerController> A,B;
    TArray<TWeakObjectPtr<ALDCharacter>> Far,Near;
    TArray<double> FarHp,NearHp;
    FVector CameraAnchor=FVector::ZeroVector,BodyAnchor=FVector::ZeroVector;
    TArray<uint8> CameraMask;
    FGuid Target;
    uint32 InitialRevision=0;
    int32 InitialStrength=0;
    void Key(FKey Input,bool Down);
    void Check(bool Pass,const FString& Text);
    void Enter(int32 Next);
    void ServerTick();
    void ClientTick();
    void Finish();
};
