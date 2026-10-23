#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/LDRules.h"
#include "Vision/LDViewTypes.h"
#include "LDWorldViewRelay.generated.h"
class ULDHumanoidVisualComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture2D;
class UMaterialInstanceDynamic;

UCLASS()
class LOCKDOWN_API ALDObservedEntityProxy : public AActor
{
    GENERATED_BODY()
public:
    ALDObservedEntityProxy();
    virtual void Tick(float Delta) override;
    void Apply(const FLDObservedEntity& Entity);
    FGuid ObservedId;
private:
    UPROPERTY() TObjectPtr<ULDHumanoidVisualComponent> Body;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> WardMesh;
    UPROPERTY() TObjectPtr<UTextRenderComponent> TaskLabel;
    FVector Destination = FVector::ZeroVector;
    float Facing = 0, Speed = 0;
    uint8 Palette = 255;
    bool bFirst = true;
    bool bInstitution=false;
    float InstitutionWorkUntil=0;
};

UCLASS()
class LOCKDOWN_API ALDWorldViewRelay : public AActor
{
    GENERATED_BODY()
public:
    ALDWorldViewRelay();
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(Client,Unreliable) void ClientFullView(const FLDWorldView& View);
    UFUNCTION(Client,Reliable) void ClientConceal(uint32 Generation, uint64 Revision, const TArray<FGuid>& Removed);
    void ApplyFullView(const FLDWorldView& View);
    void ApplyConceal(uint32 Generation, uint64 Revision, const TArray<FGuid>& Removed);
    bool IsStale() const { return bStale; }
    int32 ProxyCount() const { return Proxies.Num(); }
    uint64 AcceptedRevision() const { return Cursor.Revision; }
    const FLDWorldView& LastView() const { return Received; }
private:
    LDRules::FViewCursor Cursor;
    FLDWorldView Received;
    UPROPERTY() TMap<FGuid,TObjectPtr<ALDObservedEntityProxy>> Proxies;
    UPROPERTY() TObjectPtr<UTexture2D> FogTexture;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FogMaterial;
    TWeakObjectPtr<AActor> CameraOwner;
    double LastFullViewAt = -1;
    bool bStale = true;
    void ClearProxies();
    void UploadFog();
    void AttachFog();
};
