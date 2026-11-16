#include "Framework/LDGameState.h"
#include "Core/LDLog.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "Region/LDMapRegistry.h"
#include "UI/LDHUDModel.h"
#include "World/LDAirdrop.h"
#include "World/LDCharms.h"
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
    if (Result==ELDClockProjectionResult::Accepted) { return; }
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
    SyncDormMarkers();
    SyncAirdropMarker();
    SyncCharmMarkers();
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

TArray<const ALDCharmMarker*> ALDGameState::GetCharmMarkers() const
{
    TArray<const ALDCharmMarker*> Out;
    for (const TWeakObjectPtr<ALDCharmMarker>& Marker : CharmMarkers)
    {
        if (const ALDCharmMarker* Live=Marker.Get()) { Out.Add(Live); }
    }
    return Out;
}

void ALDGameState::ClearCharmMarkers()
{
    for (const TWeakObjectPtr<ALDCharmMarker>& Marker : CharmMarkers)
    {
        if (ALDCharmMarker* Live=Marker.Get()) { Live->Destroy(); }
    }
    CharmMarkers.Reset();
}

void ALDGameState::SyncCharmMarkers()
{
    // #226: every lying public charm gets its own ring and token; a claimed, expired or rest-cleared charm leaves the
    // public view and its marker goes with it. Nothing here reads who took it.
    const bool bShow=!PublicMatch.Charms.IsEmpty()&&ShowsWorldEvents();
    TArray<TWeakObjectPtr<ALDCharmMarker>> Kept;
    for (const TWeakObjectPtr<ALDCharmMarker>& Weak : CharmMarkers)
    {
        ALDCharmMarker* Marker=Weak.Get();
        if (!Marker) { continue; }
        const bool bLying=bShow&&Marker->GetGeneration()==PublicMatch.Generation&&PublicMatch.Charms.ContainsByPredicate(
            [Marker](const FLDCharmView& Charm){return Charm.Serial==Marker->GetSerial();});
        if (bLying) { Kept.Add(Marker); }
        else { Marker->Destroy(); }
    }
    CharmMarkers=MoveTemp(Kept);
    if (!bShow||CharmMarkerFailedGeneration==PublicMatch.Generation) { return; }
    UWorld* World=GetWorld();
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    double Radius=0;
    for (const FLDCharmView& Charm : PublicMatch.Charms)
    {
        if (CharmMarkers.ContainsByPredicate([&Charm](const TWeakObjectPtr<ALDCharmMarker>& Marker)
            {return Marker.Get()&&Marker->GetSerial()==Charm.Serial;})) { continue; }
        FString Error;
        if (!Content&&LDDemoEntry::LoadRuntimeContent(Content,Error)&&Content)
        {
            const FLDDemoRational& Approved=Content->Get().Parameters.CharmPickupRadiusCm;
            Radius=Approved.Denominator>0?double(Approved.Numerator)/double(Approved.Denominator):0.;
        }
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags|=RF_Transient;
        ALDCharmMarker* Marker=Content&&World?World->SpawnActor<ALDCharmMarker>(Spawn):nullptr;
        if (!Marker||!Marker->Build(Charm,Radius,LDHUD::CharmColor(Charm.Kind),PublicMatch.Generation,Error))
        {
            if (Marker) { Marker->Destroy(); }
            if (Error.IsEmpty()) { Error=TEXT("The charm marker actor could not be spawned."); }
            CharmMarkerFailedGeneration=PublicMatch.Generation;
            ClearCharmMarkers();
            UE_LOG(LogLDMatch,Error,TEXT("LD_CHARM_MARKER_UNAVAILABLE generation=%u serial=%llu %s"),PublicMatch.Generation,Charm.Serial,*Error);
            return;
        }
        CharmMarkers.Add(Marker);
    }
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
    if (Marker&&(!bShow||Marker->GetGeneration()!=PublicMatch.Generation
        ||(PublicMatch.Showdown.bStarted&&!Marker->GetCenter().Equals(PublicMatch.Showdown.CentreCm,1.))))
    {
        Marker->Destroy();
        Marker=nullptr;
        CaptureMarker.Reset();
    }
    if (!bShow) { return; }
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
        if (!Marker||!Marker->Build(Capture->Position,RadiusCm,PublicMatch.Generation,Error))
        {
            if (Marker) { Marker->Destroy(); }
            CaptureMarkerFailedGeneration=PublicMatch.Generation;
            UE_LOG(LogLDMatch,Error,TEXT("LD_CAPTURE_MARKER_UNAVAILABLE generation=%u %s"),PublicMatch.Generation,*Error);
            return;
        }
        CaptureMarker=Marker;
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
