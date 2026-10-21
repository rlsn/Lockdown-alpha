#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/LDNavigationComponent.h"
#include "LDCharacterMovementComponent.generated.h"

struct FLDNetworkMoveData : FCharacterNetworkMoveData
{
    FLDMoveIdentity Navigation;
    virtual void ClientFillNetworkMoveData(const FSavedMove_Character& Move,ENetworkMoveType Type) override;
    virtual bool Serialize(UCharacterMovementComponent& Movement,FArchive& Ar,UPackageMap* Map,ENetworkMoveType Type) override;
};
struct FLDNetworkMoveDataContainer : FCharacterNetworkMoveDataContainer
{
    FLDNetworkMoveData Moves[3];
    FLDNetworkMoveDataContainer(){NewMoveData=&Moves[0];PendingMoveData=&Moves[1];OldMoveData=&Moves[2];}
};

UCLASS()
class LOCKDOWN_API ULDCharacterMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    ULDCharacterMovementComponent();
    bool bWantsSprint = false;
    float SprintSpeed = 300.0f;
    bool bCombatWalkOnly = false;
    bool StartAuthorityDash(const FVector& Direction,double Distance=360);
    virtual float GetMaxSpeed() const override;
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
    virtual void MoveAutonomous(float TimeStamp,float Delta,uint8 Flags,const FVector& Accel) override;
    virtual void OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,float TimeStamp,FVector NewLocation,FVector NewVelocity,FMovementBaseInterfaceData* NewBase,FName NewBaseBoneName,bool bHasBase,bool bBaseRelativePosition,uint8 ServerMovementMode,FVector ServerGravityDirection) override;
    uint32 CorrectionCount=0;
    double MaxCorrectionCm=0;
    double LastCorrectionCm=0;
private:
    FLDNetworkMoveDataContainer NetworkMoves;
};
