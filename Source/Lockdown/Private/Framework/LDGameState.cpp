#include "Framework/LDGameState.h"
#include "Core/LDLog.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "Region/LDMapRegistry.h"
#include "World/LDAirdrop.h"
#include "World/LDDormZones.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

ALDPlayerState::ALDPlayerState() { bOnlyRelevantToOwner=true; bAlwaysRelevant=false; }

void ALDGameState::OnRep_DemoLobby()
{
    FString Error;
    const auto Result=LDMatchLifecycle::AcceptLobbyView(DemoLobby,LatestDemoLobby,Error);
    if (Result==ELDLobbyProjectionResult::Accepted) { return; }
    DemoLobby=LatestDemoLobby;
    if (Result==ELDLobbyProjectionResult::Malformed)
    { UE_LOG(LogLDMatch,Warning,TEXT("LD_LOBBY_PROJECTION_REJECTED %s"),*Error); }
    else { UE_LOG(LogLDMatch,Verbose,TEXT("LD_LOBBY_PROJECTION_STALE")); }
}

void ALDGameState::OnRep_DemoClock()
{
    FString Error;
    const auto Result=LDMatchLifecycle::AcceptClockView(DemoClock,LatestDemoClock,Error);
    if (Result==ELDClockProjectionResult::Accepted) { SyncCaptureFence(); return; }
    DemoClock=LatestDemoClock;
    if (Result==ELDClockProjectionResult::Malformed)
    { UE_LOG(LogLDMatch,Warning,TEXT("LD_CLOCK_PROJECTION_REJECTED %s"),*Error); }
}

void ALDGameState::OnRep_PublicMatch()
{
    FString Error;
    const auto Result=LDCapture::AcceptPublicMatchView(PublicMatch,LatestPublicMatch,Error);
    if (Result!=ELDLobbyProjectionResult::Accepted)
    {
        PublicMatch=LatestPublicMatch;
        if (Result==ELDLobbyProjectionResult::Malformed)
        { UE_LOG(LogLDMatch,Warning,TEXT("LD_PUBLIC_MATCH_REJECTED %s"),*Error); }
    }
    SyncCaptureMarker();
    SyncCaptureFence();
    SyncDormMarkers();
    SyncAirdropMarker();
}

TArray<const ALDCaptureMarker*> ALDGameState::GetPrivateMarkers() const
{
    TArray<const ALDCaptureMarker*> Out;
    for (const TWeakObjectPtr<ALDCaptureMarker>& Marker : PrivateMarkers) { if (Marker.IsValid()) { Out.Add(Marker.Get()); } }
    return Out;
}

void ALDGameState::SyncCaptureFence()
{
    UWorld* World=GetWorld();
    ALDCaptureFence* Fence=CaptureFence.Get();
    // The fence ends at its public instant by the synchronized authority clock, even before the cleared view arrives.
    const bool bExpired=DemoClock.IsValid()&&DemoClock.Generation==PublicMatch.Generation&&DemoClock.NowMs>=PublicMatch.CageUntilMs;
    const bool bStands=PublicMatch.CageUntilMs!=0&&!bExpired&&World&&!World->bIsTearingDown;
    if (Fence&&(!bStands||Fence->GetUntilMs()!=PublicMatch.CageUntilMs))
    {
        Fence->Destroy();
        Fence=nullptr;
        CaptureFence.Reset();
    }
    if (Fence) { Fence->SetIntegrity(PublicMatch.CageIntegrityBp); }
    if (!bStands||Fence) { return; }
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags|=RF_Transient;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Fence=World->SpawnActor<ALDCaptureFence>(Spawn);
    FString Error;
    if (!Fence||!Fence->Build(PublicMatch.CageCentreCm,PublicMatch.CageRadiusCm,PublicMatch.CageUntilMs,Error))
    {
        if (Fence) { Fence->Destroy(); }
        UE_LOG(LogLDMatch,Error,TEXT("LD_CAPTURE_FENCE_UNAVAILABLE generation=%u %s"),PublicMatch.Generation,
            Error.IsEmpty()?TEXT("The cage fence actor could not be spawned."):*Error);
        return;
    }
    Fence->SetIntegrity(PublicMatch.CageIntegrityBp);
    CaptureFence=Fence;
}

bool ALDGameState::ShowsWorldEvents() const
{
    const UWorld* World=GetWorld();
    const bool bActive=PublicMatch.Phase==ELDMatchPhase::Rest||PublicMatch.Phase==ELDMatchPhase::Development||PublicMatch.Phase==ELDMatchPhase::CaptureOpen
        ||PublicMatch.Phase==ELDMatchPhase::Results;
    return bActive&&World&&!World->bIsTearingDown&&GetNetMode()!=NM_DedicatedServer
        &&UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==LDDemoEntry::WorldMap;
}

void ALDGameState::SyncAirdropMarker()
{
    UWorld* World=GetWorld();
    const FLDAirdropView& Drop=PublicMatch.Airdrop;
    const bool bShow=Drop.Phase!=ELDAirdropPhase::None&&ShowsWorldEvents();
    ALDAirdropMarker* Marker=AirdropMarker.Get();
    if (Marker&&(!bShow||Marker->GetGeneration()!=PublicMatch.Generation||Marker->GetSerial()!=Drop.Serial))
    {
        Marker->Destroy();
        AirdropMarker.Reset();
        Marker=nullptr;
    }
    if (!bShow) { return; }
    if (Marker)
    {
        Marker->ShowPhase(Drop.Phase);
        return;
    }
    if (AirdropMarkerFailedGeneration==PublicMatch.Generation) { return; }
    FString Error;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    double Radius=0;
    if (LDDemoEntry::LoadRuntimeContent(Content,Error)&&Content)
    {
        const FLDDemoRational& Approved=Content->Get().Parameters.AirdropPickupRadiusCm;
        Radius=Approved.Denominator>0?double(Approved.Numerator)/double(Approved.Denominator):0.;
    }
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags|=RF_Transient;
    Marker=Content?World->SpawnActor<ALDAirdropMarker>(Spawn):nullptr;
    if (!Marker||!Marker->Build(Drop,Radius,PublicMatch.Generation,Error))
    {
        if (Marker) { Marker->Destroy(); }
        if (Error.IsEmpty()) { Error=TEXT("The airdrop marker actor could not be spawned."); }
        AirdropMarkerFailedGeneration=PublicMatch.Generation;
        UE_LOG(LogLDMatch,Error,TEXT("LD_AIRDROP_MARKER_UNAVAILABLE generation=%u %s"),PublicMatch.Generation,*Error);
        return;
    }
    AirdropMarker=Marker;
}

TArray<const ALDDormMarker*> ALDGameState::GetDormMarkers() const
{
    TArray<const ALDDormMarker*> Out;
    for (const TWeakObjectPtr<ALDDormMarker>& Marker : DormMarkers)
    {
        if (const ALDDormMarker* Live=Marker.Get()) { Out.Add(Live); }
    }
    return Out;
}

void ALDGameState::ClearDormMarkers()
{
    for (const TWeakObjectPtr<ALDDormMarker>& Marker : DormMarkers)
    {
        if (ALDDormMarker* Live=Marker.Get()) { Live->Destroy(); }
    }
    DormMarkers.Reset();
}

void ALDGameState::SyncDormMarkers()
{
    UWorld* World=GetWorld();
    const bool bActive=PublicMatch.Phase==ELDMatchPhase::Rest||PublicMatch.Phase==ELDMatchPhase::Development||PublicMatch.Phase==ELDMatchPhase::CaptureOpen
        ||PublicMatch.Phase==ELDMatchPhase::Results;
    // Like the flagpole boundary: Demo world presentation only, rebuilt for each match generation.
    const bool bShow=bActive&&World&&!World->bIsTearingDown&&GetNetMode()!=NM_DedicatedServer
        &&UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==LDDemoEntry::WorldMap;
    const bool bStale=DormMarkers.ContainsByPredicate([this](const TWeakObjectPtr<ALDDormMarker>& Marker)
        { return !Marker.IsValid()||Marker->GetGeneration()!=PublicMatch.Generation; });
    if (!bShow||bStale) { ClearDormMarkers(); }
    if (!bShow||!DormMarkers.IsEmpty()||DormMarkerFailedGeneration==PublicMatch.Generation) { return; }
    FString Error;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    const auto* Registry=World->GetSubsystem<ULDMapRegistry>();
    FLDDormZones Zones;
    if (!Registry||!Registry->Enabled()) { Error=TEXT("The Demo world registry is not loaded."); }
    else if (LDDemoEntry::LoadRuntimeContent(Content,Error)&&Content) { LDDormZones::Resolve(*Registry,*Content,Zones,Error); }
    bool bBuilt=Zones.IsValid();
    for (const FLDDormZones::FDorm& Dorm : Zones.Dorms)
    {
        if (!bBuilt) { break; }
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags|=RF_Transient;
        ALDDormMarker* Marker=World->SpawnActor<ALDDormMarker>(Spawn);
        const FLDMapBinding* Binding=Registry->Resolve(Dorm.Id,Registry->Generation);
        if (!Marker||!Binding||!Marker->Build(Dorm.Id,Binding->Position,Zones.RadiusCm,PublicMatch.Generation,Error))
        {
            if (Marker) { Marker->Destroy(); }
            if (Error.IsEmpty()) { Error=TEXT("A dorm boundary actor could not be spawned."); }
            bBuilt=false;
            break;
        }
        DormMarkers.Add(Marker);
    }
    if (!bBuilt)
    {
        ClearDormMarkers();
        DormMarkerFailedGeneration=PublicMatch.Generation;
        UE_LOG(LogLDMatch,Error,TEXT("LD_DORM_MARKER_UNAVAILABLE generation=%u %s"),PublicMatch.Generation,*Error);
    }
}

void ALDGameState::SyncCaptureMarker()
{
    UWorld* World=GetWorld();
    const bool bActive=PublicMatch.Phase==ELDMatchPhase::Rest||PublicMatch.Phase==ELDMatchPhase::Development||PublicMatch.Phase==ELDMatchPhase::CaptureOpen
        ||PublicMatch.Phase==ELDMatchPhase::Results;
    // The boundary belongs to the Demo world's presentation; fixture worlds without its registry show none.
    const bool bShow=bActive&&World&&!World->bIsTearingDown&&GetNetMode()!=NM_DedicatedServer
        &&UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==LDDemoEntry::WorldMap;
    ALDCaptureMarker* Marker=CaptureMarker.Get();
    // A new match generation, a return to the Lobby or a stopped room never keeps the previous ring.
    if (Marker&&(!bShow||Marker->GetGeneration()!=PublicMatch.Generation))
    {
        Marker->Destroy();
        Marker=nullptr;
        CaptureMarker.Reset();
    }
    const int32 PrivateCount=bShow&&PublicMatch.Showdown.bStarted?PublicMatch.PrivatePoles.Num():0;
    for (int32 Index=PrivateMarkers.Num()-1;Index>=0;--Index)
    {
        ALDCaptureMarker* Private=PrivateMarkers[Index].Get();
        if (Index<PrivateCount&&Private&&Private->GetGeneration()==PublicMatch.Generation) { continue; }
        if (Private) { Private->Destroy(); }
        PrivateMarkers.RemoveAt(Index);
    }
    if (!bShow) { return; }
    // Private flagpoles: built in the public order, each in its holder's tint.
    for (int32 Index=0;Index<PrivateCount;++Index)
    {
        const FLDPrivatePoleView& Pole=PublicMatch.PrivatePoles[Index];
        if (!PrivateMarkers.IsValidIndex(Index))
        {
            FActorSpawnParameters Spawn;
            Spawn.ObjectFlags|=RF_Transient;
            ALDCaptureMarker* Private=World->SpawnActor<ALDCaptureMarker>(Spawn);
            FString Error;
            if (!Private||!Private->Build(Pole.CentreCm,Pole.RadiusCm,PublicMatch.Generation,Error))
            {
                if (Private) { Private->Destroy(); }
                UE_LOG(LogLDMatch,Error,TEXT("LD_PRIVATE_POLE_MARKER_UNAVAILABLE generation=%u %s"),PublicMatch.Generation,*Error);
                break;
            }
            Private->ShowPole(true);
            PrivateMarkers.Add(Private);
        }
        ALDCaptureMarker* Private=PrivateMarkers[Index].Get();
        Private->ShowState(Pole.CircleState);
        Private->ShowGraffiti(Pole.bGraffiti,Pole.Presentation);
    }
    if (!Marker)
    {
        if (CaptureMarkerFailedGeneration==PublicMatch.Generation) { return; }
        FString Error;
        TSharedPtr<const FLDDemoContentSnapshot> Content;
        const auto* Registry=World->GetSubsystem<ULDMapRegistry>();
        const FLDMapBinding* Capture=nullptr;
        double RadiusCm=0;
        if (!Registry||!Registry->Enabled()) { Error=TEXT("The Demo world registry is not loaded."); }
        else if (LDDemoEntry::LoadRuntimeContent(Content,Error)&&Content)
        {
            const FLDDemoRational& Radius=Content->Get().Parameters.CaptureRadiusCm;
            RadiusCm=Radius.Denominator>0?double(Radius.Numerator)/double(Radius.Denominator):0.;
            Capture=Registry->Resolve(PublicMatch.Showdown.bStarted?FName(TEXT("final_capture")):FName(*Content->Get().World.CaptureId),Registry->Generation);
            if (!Capture) { Error=TEXT("The registered world has no capture anchor."); }
        }
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags|=RF_Transient;
        Marker=Capture?World->SpawnActor<ALDCaptureMarker>(Spawn):nullptr;
        if (Capture&&!Marker) { Error=TEXT("The flagpole boundary actor could not be spawned."); }
        const bool bFinal=PublicMatch.Showdown.bStarted;
        if (!Marker||!Marker->Build(bFinal?PublicMatch.PoleCentreCm:Capture->Position,bFinal?double(PublicMatch.PoleRadiusCm):RadiusCm,
            PublicMatch.Generation,Error))
        {
            if (Marker) { Marker->Destroy(); }
            CaptureMarkerFailedGeneration=PublicMatch.Generation;
            UE_LOG(LogLDMatch,Error,TEXT("LD_CAPTURE_MARKER_UNAVAILABLE generation=%u %s"),PublicMatch.Generation,*Error);
            return;
        }
        CaptureMarker=Marker;
    }
    // The final field moves the zone with a carried flagpole and resizes it under final_double_yellow.
    if (PublicMatch.Showdown.bStarted)
    {
        Marker->Place(PublicMatch.PoleCentreCm,PublicMatch.PoleRadiusCm);
        Marker->ShowPole(!PublicMatch.PoleCentreCm.Equals(PublicMatch.Showdown.CentreCm,1.));
    }
    Marker->ShowState(PublicMatch.CircleState);
    Marker->ShowGraffiti(PublicMatch.GraffitiOwner.IsValid(),PublicMatch.GraffitiPresentation);
}

void ALDGameState::OnRep_MatchResult()
{
    FString Error;
    const auto Result=LDCapture::AcceptResultView(MatchResult,LatestMatchResult,Error);
    if (Result==ELDLobbyProjectionResult::Accepted) { return; }
    MatchResult=LatestMatchResult;
    if (Result==ELDLobbyProjectionResult::Malformed)
    { UE_LOG(LogLDMatch,Warning,TEXT("LD_MATCH_RESULT_REJECTED %s"),*Error); }
}

void ALDGameState::OnRep_KillFeed()
{
    FString Error;
    const auto Result=LDCapture::AcceptKillFeed(KillFeed,LatestKillFeed,Error);
    if (Result==ELDLobbyProjectionResult::Accepted) { return; }
    KillFeed=LatestKillFeed;
    if (Result==ELDLobbyProjectionResult::Malformed)
    { UE_LOG(LogLDMatch,Warning,TEXT("LD_KILL_FEED_REJECTED %s"),*Error); }
}

void ALDGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ALDGameState,DemoLobby);
    DOREPLIFETIME(ALDGameState,DemoClock);
    DOREPLIFETIME(ALDGameState,PublicMatch);
    DOREPLIFETIME(ALDGameState,MatchResult);
    DOREPLIFETIME(ALDGameState,KillFeed);
}
