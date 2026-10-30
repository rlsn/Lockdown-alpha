#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDB92Deposits.generated.h"
class ALDB92Controller;
UCLASS()
class LOCKDOWN_API ALDB92Deposits:public AActor
{
    GENERATED_BODY()
public:
    ALDB92Deposits();
    virtual void Tick(float Delta) override;
    static const TArray<FName>& Nodes();
    static FName Budget(int32 Node);
    static ELDItemKind Kind(int32 Node);
    static FVector Position(const UWorld* World,int32 Node);
    void Stop(ALDB92Controller* PC);
    bool Active(ALDB92Controller* PC) const;
private:
    struct FState {int32 Node=INDEX_NONE,Blocked=INDEX_NONE;uint32 Life=0;FGuid Body;FVector Anchor;double Ends=0;uint32 Sequence=0;};
    TMap<FGuid,FState> States;
    uint32 NextSequence=0;
    double Next=0;
    int32 Nearby(ALDB92Controller* PC) const;
    bool Eligible(ALDB92Controller* PC,int32 Node) const;
    void Progress(ALDB92Controller* PC,FState& State,bool Active);
};
