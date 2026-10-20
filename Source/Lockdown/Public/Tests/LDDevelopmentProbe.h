#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vision/LDViewTypes.h"
#include "LDDevelopmentProbe.generated.h"

class ALDPlayerController;
class ALDCharacter;

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
    double SkillRefreshAt=0;
    int32 Phase=0,Failures=0;
    bool bChoiceSent=false,bReadySent=false,bStartSent=false,bChecked=false;
    bool bSawSkillWarning=false;
    void Check(bool Pass,const FString& Description);
    void LocalCheck();
    void SkillCheck();
    void SkillServerTick();
    void SkillClientTick();
    void PreparationCheck();
    void PreparationServerTick();
    void PreparationClientTick();
    void PreparationNormalTick();
    void RegionCheck();
    void RegionNormalTick();
    void RegionServerTick();
    void RegionClientTick();
    TWeakObjectPtr<ALDCharacter> RegionNPC;
    FVector RegionNPCStart=FVector::ZeroVector;
    double RegionNPCHp=0;
    bool RegionSawNPCTravel=false;
    void WorldCheck();
    void WorldServerTick();
    void WorldClientTick();
    void WorldNormalTick();
    TArray<FVector> NormalPath;
    int32 NormalPathIndex=0,NormalDeliveries=0;
    double NormalHoldAt=0;
    double NormalTravel=0;
    FVector NormalLastPosition=FVector::ZeroVector;
    void ResetCheck();
    void ServerTick();
    void ClientTick();
    void Enter(int32 Next);
    void Finish();
};
