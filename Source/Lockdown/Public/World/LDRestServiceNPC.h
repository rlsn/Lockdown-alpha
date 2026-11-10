#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Framework/LDMatchTimeline.h"
#include "LDRestServiceNPC.generated.h"

UCLASS()
class LOCKDOWN_API ALDRestServiceNPC : public AActor
{
    GENERATED_BODY()
public:
    ALDRestServiceNPC();
    void SetService(ELDRestService Value);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
    UPROPERTY(ReplicatedUsing=OnRep_Service) ELDRestService Service = ELDRestService::None;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Body;
    UPROPERTY() TObjectPtr<class UTextRenderComponent> Label;
    UFUNCTION() void OnRep_Service();
};
