#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vision/LDViewTypes.h"
#include "LDViewNetworkProbe.generated.h"
class ALDPlayerController;
class ALDCharacter;
UCLASS()
class ALDViewNetworkProbe : public AActor
{
    GENERATED_BODY()
public:
    ALDViewNetworkProbe();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
private:
    TWeakObjectPtr<ALDPlayerController> ClientA,ClientB;
    TArray<TWeakObjectPtr<ALDCharacter>> Targets;
    TArray<FString> Checks;
    TArray<TSharedPtr<class FJsonValue>> Captures;
    FLDWorldView SavedPublic;
    FGuid PublicId;
    uint64 ConcealedRevision = 0;
    int32 Phase = 0, Failures = 0, MaxPayload = 0;
    double Started = 0, PhaseAt = 0;
    bool bChecked = false, bMoved = false, bDuplicate = false;
    FString Role;
    void Check(bool bPass,const FString& Description);
    void EnterPhase(int32 Next);
    void ServerTick();
    void ClientTick();
    void Finish();
};
