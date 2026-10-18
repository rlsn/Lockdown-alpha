#include "Tests/LDViewNetworkProbe.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/CoreNet.h"
#include "AIController.h"

namespace
{
    const FVector NearA(-1500,-1000,100), NearB(2000,-900,100), PublicTarget(1200,1100,100);
    bool Contains(const FLDWorldView& View,const FVector& Point)
    { return View.Entities.ContainsByPredicate([&](const FLDObservedEntity& E){return FVector::DistSquared2D(E.Position,Point)<4;}); }
    void Move(ALDPlayerController* PC,const FVector& Point)
    { if (ALDCharacter* Body=Cast<ALDCharacter>(PC->GetPawn())) { Body->SetActorLocation(Point,false,nullptr,ETeleportType::TeleportPhysics); Body->GetCharacterMovement()->DisableMovement(); } }
}
ALDViewNetworkProbe::ALDViewNetworkProbe() { bReplicates=false; PrimaryActorTick.bCanEverTick=true; }
void ALDViewNetworkProbe::BeginPlay()
{
    Super::BeginPlay(); Started=PhaseAt=FPlatformTime::Seconds();
    FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);
    if (Role.IsEmpty()) { Role=TEXT("Server"); }
    if (GetNetMode()==NM_Client) { if (ALDPlayerController* PC=Cast<ALDPlayerController>(GetOwner())) { PC->ServerProbeHello(Role==TEXT("A") ? 1 : 2); } }
    UE_LOG(LogTemp,Display,TEXT("LD_VIEW_PROBE Fixture only: %s"),*Role);
}
void ALDViewNetworkProbe::Check(bool Pass,const FString& Description)
{ Checks.Add((Pass ? TEXT("PASS ") : TEXT("FAIL "))+Description); if (!Pass) { ++Failures; } UE_LOG(LogTemp,Display,TEXT("LD_VIEW_PROBE %s"),*Checks.Last()); }
void ALDViewNetworkProbe::EnterPhase(int32 Next)
{
    Phase=Next; PhaseAt=FPlatformTime::Seconds(); bChecked=false; bMoved=false; bDuplicate=false;
    ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    ALDVisionDirector* Vision=Mode->Vision;
    if (Next==1)
    {
        Vision->SetPublicSensor(false); Move(ClientA.Get(),FVector(-1700,-1000,100)); Move(ClientB.Get(),FVector(1700,-900,100));
        for (const FVector Point : {NearA,NearB,PublicTarget})
        {
            ALDCharacter* Target=GetWorld()->SpawnActorDeferred<ALDCharacter>(ALDCharacter::StaticClass(),FTransform(Point));
            Target->SetReplicates(false); Target->FinishSpawning(FTransform(Point)); Target->GetCharacterMovement()->DisableMovement(); Targets.Add(Target);
        }
        Check(!Vision->Ray({FVector(-900,-700,0),720,0},FVector(-900,-200,0)),TEXT("Low wall blocks"));
        Check(!Vision->Ray({FVector(-900,-700,0),720,1},FVector(-900,-200,0)),TEXT("High wall blocks"));
        Check(!Vision->Ray({FVector(-1400,500,0),720,1},FVector(-1000,500,0)),TEXT("Tree blocks elevated source"));
        Check(!Vision->Ray({FVector(500,-400,0),720,0},FVector(800,-400,0)),TEXT("Low source cannot see into plateau"));
        Check(!Vision->Ray({FVector(400,300,0),2000,0},FVector(1500,-600,0)),TEXT("Low source cannot see across plateau"));
        Check(Vision->Ray({FVector(1000,-400,200),720,1},FVector(500,-400,0)),TEXT("High source sees down ramp"));
        Check(Vision->Ray({FVector(-1700,-1000,0),720,0},FVector(-980,-1000,0)) && !Vision->Ray({FVector(-1700,-1000,0),720,0},FVector(-979.9,-1000,0)),TEXT("Exact planar radius boundary"));
    }
    if (Next==3 || Next==5 || Next==9) { Vision->SetPublicSensor(true); }
    if (Next==4 || Next==8) { Vision->SetPublicSensor(false); }
    if (Next==6) { Vision->bPublicationEnabled=false; }
    if (Next==7) { Vision->bPublicationEnabled=true; }
    if (Next==8)
    {
        Mode->StateFor(ClientA.Get())->bActive=false; Mode->SyncOwner(ClientA.Get());
        for (TActorIterator<ALDWard> It(GetWorld());It;++It) { It->ExpiresAt=GetWorld()->GetTimeSeconds()+.2f; }
    }
    ClientA->ClientProbePhase(uint8(Next)); ClientB->ClientProbePhase(uint8(Next));
}
void ALDViewNetworkProbe::ServerTick()
{
    ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>(); if (!Mode || !Mode->Vision) { return; }
    if (!Phase)
    {
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {
            ALDPlayerController* PC=Cast<ALDPlayerController>(It->Get());
            if (PC && PC->GetPawn() && PC->PlayerState)
            { if (PC->ProbeRole==1) { ClientA=PC; } if (PC->ProbeRole==2) { ClientB=PC; } }
        }
        if (ClientA.IsValid() && ClientB.IsValid() && FPlatformTime::Seconds()-Started>2) { EnterPhase(1); }
        return;
    }
    const double Elapsed=FPlatformTime::Seconds()-PhaseAt;
    if (Phase==9) { if (Elapsed>1.8) { Finish(); } return; }
    if (!ClientA.IsValid() || !ClientB.IsValid()) { Check(false,TEXT("Client disconnected before final phase")); Finish(); return; }
    if (Phase==2 && !bMoved && Mode->StateFor(ClientA.Get())->WardCharges==0)
    { Move(ClientA.Get(),FVector(-1700,300,100)); bMoved=true; }
    if (Elapsed<1.3) { return; }
    if (Phase==2)
    {
        int32 Count=0; float Lifetime=0;
        for (TActorIterator<ALDWard> It(GetWorld());It;++It) { ++Count; Lifetime=It->ExpiresAt-GetWorld()->GetTimeSeconds(); }
        Check(Count==1 && Lifetime>57 && Lifetime<=60 && Mode->StateFor(ClientA.Get())->WardCharges==0 && Mode->StateFor(ClientB.Get())->WardCharges==1,TEXT("Owning RPC: one ward, one charge; retries and invalid requests do not duplicate"));
    }
    if (Phase==4)
    {
        const auto* A=Mode->Vision->AuthorityViewFor(ClientA.Get()); const auto* B=Mode->Vision->AuthorityViewFor(ClientB.Get());
        Check(A && B && !Contains(*A,PublicTarget) && !Contains(*B,PublicTarget),TEXT("First publication after source loss contains no fresh hidden target pose"));
        Check(!Mode->Vision->ResolveObserved(ClientA.Get(),PublicId),TEXT("A formerly visible observed ID no longer authorizes the hidden target"));
    }
    if (Phase==3)
    {
        const auto* A=Mode->Vision->AuthorityViewFor(ClientA.Get()); const auto* B=Mode->Vision->AuthorityViewFor(ClientB.Get());
        FGuid Other;
        if(A){for(const auto& E:A->Entities){if(FVector::DistSquared2D(E.Position,PublicTarget)<4){PublicId=E.Id;}}}
        if(B){for(const auto& E:B->Entities){if(FVector::DistSquared2D(E.Position,PublicTarget)<4){Other=E.Id;}}}
        Check(PublicId.IsValid() && Other.IsValid() && PublicId!=Other && Mode->Vision->ResolveObserved(ClientA.Get(),PublicId) && !Mode->Vision->ResolveObserved(ClientB.Get(),PublicId),TEXT("Opaque IDs are per connection; cross-owner lookup fails"));
    }
    if (Phase==8)
    {
        int32 Count=0; for(TActorIterator<ALDWard> It(GetWorld());It;++It){++Count;}
        Check(Count==0,TEXT("Authority ward expiry removes the real object"));
    }
    EnterPhase(Phase+1);
}
void ALDViewNetworkProbe::ClientTick()
{
    ALDPlayerController* PC=Cast<ALDPlayerController>(GetOwner()); if (!PC) { return; }
    ALDWorldViewRelay* Relay=PC->LocalView(); if (!Relay || !PC->ProbePhase) { return; }
    if (Phase!=PC->ProbePhase)
    {
        Phase=PC->ProbePhase; PhaseAt=FPlatformTime::Seconds(); bChecked=false; bDuplicate=false; bMoved=false;
        if (Phase==2)
        {
            if (Role==TEXT("A")) { PC->ServerPlaceWard(PC->OwnerState.Match,PC->OwnerState.Life,100,FVector(-1650,-1000,0)); }
            else { PC->ServerPlaceWard(PC->OwnerState.Match,PC->OwnerState.Life,100,FVector(100000,0,0)); }
        }
    }
    const double Elapsed=FPlatformTime::Seconds()-PhaseAt;
    if (Phase==2 && Role==TEXT("A") && Elapsed>.35 && !bDuplicate)
    { bDuplicate=true; PC->ServerPlaceWard(PC->OwnerState.Match,PC->OwnerState.Life,100,FVector(-1650,-1000,0)); }
    if (Phase==2 && Role==TEXT("A") && Elapsed>.7 && !bMoved)
    { bMoved=true; PC->ServerPlaceWard(PC->OwnerState.Match,PC->OwnerState.Life+1,101,FVector(-1650,-1000,0)); }
    if (bChecked || Elapsed<.8) { return; }
    bChecked=true;
    int32 Canonical=0,States=0,Relays=0,Controllers=0,Wards=0,Directors=0,AI=0;
    bool bOwnerOnly=true;
    for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){++Canonical; bOwnerOnly &= *It==PC->GetPawn();}
    for(TActorIterator<APlayerState> It(GetWorld());It;++It){++States; bOwnerOnly &= *It==PC->PlayerState;}
    for(TActorIterator<ALDWorldViewRelay> It(GetWorld());It;++It){++Relays; bOwnerOnly &= It->GetOwner()==PC;}
    for(TActorIterator<APlayerController> It(GetWorld());It;++It){++Controllers;}
    for(TActorIterator<ALDWard> It(GetWorld());It;++It){++Wards;}
    for(TActorIterator<ALDVisionDirector> It(GetWorld());It;++It){++Directors;}
    for(TActorIterator<AAIController> It(GetWorld());It;++It){++AI;}
    Check(Canonical==1 && States==1 && Relays==1 && Controllers==1 && Wards==0 && Directors==0 && AI==0 && bOwnerOnly && GetWorld()->GetGameState()->PlayerArray.Num()==1,FString::Printf(TEXT("Phase %d restricted registry: own body/state/controller/relay only; no ward/director/AI"),Phase));
    FLDWorldView View=Relay->LastView();
    FNetBitWriter Writer(nullptr,65536); bool bSerialized=true; View.NetSerialize(Writer,nullptr,bSerialized);
    MaxPayload=FMath::Max(MaxPayload,int32(Writer.GetNumBytes()));
    Check(bSerialized && Writer.GetNumBytes()<4096,FString::Printf(TEXT("Phase %d bounded serialized full view (%lld bytes)"),Phase,Writer.GetNumBytes()));
    if (Phase==1) { Check(Contains(View,Role==TEXT("A") ? NearA : NearB) && !Contains(View,Role==TEXT("A") ? NearB : NearA) && !Contains(View,PublicTarget),TEXT("Body sight differs between clients")); }
    if (Phase==2) { Check(Contains(View,NearA)==(Role==TEXT("A")) && View.WardSources==(Role==TEXT("A") ? 1 : 0),TEXT("Distant ward sight is private to its owner")); }
    if (Phase==3) { Check(Contains(View,PublicTarget) && View.PublicSources==1,TEXT("Public source helps both clients")); SavedPublic=View; }
    if (Phase==4)
    {
        Check(!Contains(View,PublicTarget) && !LDVision::Bit(View.Current,LDVision::Cell(PublicTarget)) && LDVision::Bit(View.Explored,LDVision::Cell(PublicTarget)),TEXT("Source loss conceals target and keeps only explored terrain"));
        ConcealedRevision=Relay->AcceptedRevision(); Relay->ApplyFullView(SavedPublic);
        Check(Relay->AcceptedRevision()==ConcealedRevision && !Contains(Relay->LastView(),PublicTarget),TEXT("Delayed older full view cannot resurrect concealed target"));
    }
    if (Phase==5)
    {
        TArray<FGuid> Ids; for(const auto& E:View.Entities){if(FVector::DistSquared2D(E.Position,PublicTarget)<4){Ids.Add(E.Id);}}
        Relay->ApplyConceal(View.Generation,ConcealedRevision,Ids);
        Check(Contains(Relay->LastView(),PublicTarget),TEXT("Delayed concealment cannot erase authorized reappearance"));
    }
    if (Phase==6) { Check(Relay->IsStale() && Relay->ProxyCount()==0 && Relay->LastView().Entities.IsEmpty(),TEXT("250ms missing full view clears all remote proxies and effects")); }
    if (Phase==7) { Check(!Relay->IsStale() && Contains(View,PublicTarget),TEXT("Fresh full view restores state after dropped views")); }
    if (Phase==8 && Role==TEXT("A")) { Check(View.BodySources==0 && View.WardSources==0 && View.PublicSources==0 && View.Entities.IsEmpty(),TEXT("Waiting view has no body or expired ward source")); }
    if (Phase==9)
    {
        if(Role==TEXT("A")){Check(View.BodySources==0 && View.PublicSources==1 && Contains(View,PublicTarget),TEXT("Waiting contender still receives legitimate shared sight"));}
        if(ALDCharacter* Body=Cast<ALDCharacter>(PC->GetPawn())){Body->Zoom(-100);}
        Check(Relay->LastView().Current==View.Current,TEXT("Camera zoom adds no sight source or permission"));
    }
    TSharedRef<FJsonObject> Capture=MakeShared<FJsonObject>(); Capture->SetNumberField(TEXT("phase"),Phase); Capture->SetNumberField(TEXT("revision"),double(View.Revision)); Capture->SetNumberField(TEXT("bodySources"),View.BodySources); Capture->SetNumberField(TEXT("wardSources"),View.WardSources); Capture->SetNumberField(TEXT("publicSources"),View.PublicSources);
    TArray<TSharedPtr<FJsonValue>> Records;
    for(const auto& E:Relay->LastView().Entities){TSharedRef<FJsonObject> R=MakeShared<FJsonObject>();R->SetStringField(TEXT("observedId"),E.Id.ToString());R->SetNumberField(TEXT("x"),E.Position.X);R->SetNumberField(TEXT("y"),E.Position.Y);R->SetNumberField(TEXT("presentation"),E.Presentation);R->SetNumberField(TEXT("kind"),uint8(E.Kind));Records.Add(MakeShared<FJsonValueObject>(R));}
    Capture->SetArrayField(TEXT("permittedRecords"),Records); Captures.Add(MakeShared<FJsonValueObject>(Capture));
    if (Phase==9) { Finish(); }
}
void ALDViewNetworkProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(FPlatformTime::Seconds()-Started>55){Check(false,TEXT("Probe timed out before complete network evidence"));Finish();return;}
    if(GetNetMode()!=NM_Client){ServerTick();} else {ClientTick();}
#endif
}
void ALDViewNetworkProbe::Finish()
{
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("failures"),Failures);Json->SetNumberField(TEXT("maxViewPayloadBytes"),MaxPayload);
    TArray<TSharedPtr<FJsonValue>> Lines;for(const FString& Line:Checks){Lines.Add(MakeShared<FJsonValueString>(Line));}Json->SetArrayField(TEXT("checks"),Lines);Json->SetArrayField(TEXT("captures"),Captures);
    FString Output,Directory;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);
    if(Directory.IsEmpty() || !FFileHelper::SaveStringToFile(Output,*(Directory/(Role+TEXT(".json"))))){++Failures;}
    UE_LOG(LogTemp,Display,TEXT("LD_VIEW_PROBE_FINISHED %s failures=%d"),*Role,Failures);SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,Failures ? 1 : 0);
}
