#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/LDWorldQueryTypes.h"
#include "Vision/LDViewTypes.h"
#include "LDWorldViewRelay.generated.h"
class ULDHumanoidVisualComponent;
class UTexture2D;
class UMaterialInstanceDynamic;
class FLDDemoContentSnapshot;

enum class ELDObservedViewStatus : uint8 { Unavailable, Current, Stale };
enum class ELDObservedClearReason : uint8
{
    Conceal, FullSetRemoved, ObservedDeath, Timeout, ObserverReset, MatchReset, Disconnected, MatchEnded, ProtocolRejected
};

struct FLDObservedClear
{
    FGuid Handle;
    uint32 ObservationEpoch = 0;
    ELDObservedClearReason Reason = ELDObservedClearReason::Conceal;
};

struct FLDObservedFrameRead
{
    const FLDWorldView* Frame = nullptr;
    ELDObservedViewStatus Status = ELDObservedViewStatus::Unavailable;
    uint64 LocalSerial = 0;
};

// Approved public definitions that a received unit may reference; unknown references are protocol errors.
struct LOCKDOWN_API FLDObservedCatalog
{
    uint8 MaxLevel = 0;
    TSet<uint8> Equipment;
    TSet<uint8> Creatures;
    bool Build(const FLDDemoContentSnapshot& Content, FString& Error);
    bool Accepts(const FLDObservedUnitView& Unit, FString& Error) const;
};

// Owner-local acceptance of full views, concealments and terminations under one ordering cursor.
class LOCKDOWN_API FLDObservedFrameReceiver
{
public:
    enum class EResult : uint8 { Applied, Stale, Rejected };
    static constexpr double StaleAfterSeconds = 0.25;
    EResult AcceptFull(const FLDWorldView& View, const FLDWorldRevision& ExpectedWorld, const FLDObservedCatalog& Catalog,
        double NowSeconds, TArray<FLDObservedClear>& Cleared, FString& Error);
    EResult AcceptConceal(const FLDObservedConceal& Notice, TArray<FLDObservedClear>& Cleared, FString& Error);
    EResult AcceptTermination(const FLDObservedTerminationNotice& Notice, TArray<FLDObservedClear>& Cleared, FString& Error);
    bool Tick(double NowSeconds, TArray<FLDObservedClear>& Cleared);
    void Reset(ELDObservedClearReason Reason, TArray<FLDObservedClear>& Cleared);
    FLDObservedFrameRead Read() const;
    bool FindUnit(const FGuid& Handle, uint32 ObservationEpoch, FLDObservedUnitView& Out) const;
    uint32 CursorMatch() const { return Match; }
private:
    bool Advance(uint32 InMatch, uint32 InGeneration, uint32 InRevision, TArray<FLDObservedClear>& Cleared);
    void ClearUnits(ELDObservedClearReason Reason, TArray<FLDObservedClear>& Cleared);
    void ApplyTermination(const FLDObservedUnitTermination& Termination, TArray<FLDObservedClear>& Cleared);
    FLDWorldView Frame;
    ELDObservedViewStatus Status = ELDObservedViewStatus::Unavailable;
    uint32 Match = 0, Generation = 0, Revision = 0, LastTermination = 0;
    double LastFullAt = -1;
    uint64 Serial = 0;
};

// Client-only presentation of one authorized unit. It never collides, navigates or owns gameplay state.
UCLASS()
class LOCKDOWN_API ALDObservedEntityProxy : public AActor
{
    GENERATED_BODY()
public:
    ALDObservedEntityProxy();
    virtual void Tick(float Delta) override;
    void Apply(const FLDObservedUnitView& Unit, double VisualHalfHeightCm);
    FGuid ObservedHandle;
    uint32 ObservedEpoch = 0;
private:
    UPROPERTY() TObjectPtr<ULDHumanoidVisualComponent> Body;
    FVector Destination = FVector::ZeroVector;
    FVector DisplayVelocity = FVector::ZeroVector;
    double LastApplyAt = -1;
    float FacingDegrees = 0;
    uint8 Palette = 255;
    bool bPlaced = false;
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
    UFUNCTION(Client,Reliable) void ClientConceal(const FLDObservedConceal& Notice);
    UFUNCTION(Client,Reliable) void ClientTermination(const FLDObservedTerminationNotice& Notice);
    FLDObservedFrameRead ReadObservedFrame() const { return Receiver.Read(); }
    bool ReadUnit(const FGuid& Handle, uint32 ObservationEpoch, FLDObservedUnitView& Out) const
    { return Receiver.FindUnit(Handle, ObservationEpoch, Out); }
    const TArray<FLDObservedClear>& RecentClears() const { return LastClears; }
    int32 ProxyCount() const { return Proxies.Num(); }
private:
    void Present(const TArray<FLDObservedClear>& Cleared);
    void Refuse(const FString& Error);
    bool EnsureCatalog(FString& Error);
    bool ExpectedWorld(FLDWorldRevision& Out) const;
    void SyncProxies();
    void AttachFog();
    void UploadFog();
    FLDObservedFrameReceiver Receiver;
    FLDObservedCatalog Catalog;
    bool bCatalogReady = false;
    TArray<FLDObservedClear> LastClears;
    UPROPERTY() TMap<FGuid,TObjectPtr<ALDObservedEntityProxy>> Proxies;
    UPROPERTY() TObjectPtr<UTexture2D> FogTexture;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FogMaterial;
    TWeakObjectPtr<AActor> CameraOwner;
    uint64 FogSerial = MAX_uint64;
};
