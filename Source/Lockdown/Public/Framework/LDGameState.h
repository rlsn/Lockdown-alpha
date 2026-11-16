#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDCaptureTypes.h"
#include "World/LDAirdropMarker.h"
#include "World/LDCaptureMarker.h"
#include "World/LDDormMarker.h"
#include "LDGameState.generated.h"

UCLASS()
class LOCKDOWN_API ALDPlayerState : public APlayerState
{
    GENERATED_BODY()
public:
    ALDPlayerState();
};

UCLASS()
class LOCKDOWN_API ALDGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    UPROPERTY(ReplicatedUsing=OnRep_DemoLobby) FLDLobbyView DemoLobby;
    UFUNCTION() void OnRep_DemoLobby();
    UPROPERTY(ReplicatedUsing=OnRep_DemoClock) FLDDemoClockView DemoClock;
    UFUNCTION() void OnRep_DemoClock();
    // Capture/endgame public read models: the circle, the frozen result and the kill feed.
    UPROPERTY(ReplicatedUsing=OnRep_PublicMatch) FLDPublicMatchView PublicMatch;
    UFUNCTION() void OnRep_PublicMatch();
    UPROPERTY(ReplicatedUsing=OnRep_MatchResult) FLDResultView MatchResult;
    UFUNCTION() void OnRep_MatchResult();
    UPROPERTY(ReplicatedUsing=OnRep_KillFeed) FLDKillFeedView KillFeed;
    UFUNCTION() void OnRep_KillFeed();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    // Local presentation only: the visible flagpole boundary of the current match, never replicated.
    const ALDCaptureMarker* GetCaptureMarker() const { return CaptureMarker.Get(); }
    // Local presentation only: the dorm boundaries of the current match (D21, #79), never replicated.
    TArray<const ALDDormMarker*> GetDormMarkers() const;
    // Local presentation only: the current public airdrop (D26, #79), never replicated.
    const ALDAirdropMarker* GetAirdropMarker() const { return AirdropMarker.Get(); }
private:
    // The match phases whose public world events (airdrop, charms) are drawn in the scene of this machine.
    bool ShowsWorldEvents() const;
    // Builds the boundary from this machine's world data for an active public match, and removes it otherwise.
    void SyncCaptureMarker();
    TWeakObjectPtr<ALDCaptureMarker> CaptureMarker;
    uint32 CaptureMarkerFailedGeneration = 0;
    // The same for every dorm ring.
    void SyncDormMarkers();
    void ClearDormMarkers();
    TArray<TWeakObjectPtr<ALDDormMarker>> DormMarkers;
    uint32 DormMarkerFailedGeneration = 0;
    // The airdrop ring and crate, rebuilt for each drop serial.
    void SyncAirdropMarker();
    TWeakObjectPtr<ALDAirdropMarker> AirdropMarker;
    uint32 AirdropMarkerFailedGeneration = 0;
    FLDLobbyView LatestDemoLobby;
    FLDDemoClockView LatestDemoClock;
    FLDPublicMatchView LatestPublicMatch;
    FLDResultView LatestMatchResult;
    FLDKillFeedView LatestKillFeed;
};
