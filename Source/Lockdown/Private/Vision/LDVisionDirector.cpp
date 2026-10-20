#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Combat/LDCombatDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Objectives/LDObjectiveRules.h"

ALDWard::ALDWard()
{
    bReplicates = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}
ALDVisionDirector::ALDVisionDirector()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
}
void ALDVisionDirector::BeginPlay()
{
    Super::BeginPlay();
    check(HasAuthority());
    for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
    {
        if (It->GetStaticMeshComponent()->ComponentHasTag(TEXT("LDHighGround")))
        { HighGround.Add(It->GetComponentsBoundingBox()); }
    }
}
uint8 ALDVisionDirector::LevelAt(const FVector& P) const
{
    // Authoring marks the plateau footprint. Ramp remains low until its top.
    for (const FBox& Box : HighGround)
    { if (P.X >= Box.Min.X && P.X < Box.Max.X && P.Y >= Box.Min.Y && P.Y < Box.Max.Y) { return 1; } }
    return 0;
}
bool ALDVisionDirector::Ray(const FLDSightSource& Source, const FVector& Target) const
{
    if (!LDVision::InWorld(Target) || FVector::DistSquared2D(Source.Position,Target) > FMath::Square(Source.Radius)) { return false; }
    const FVector From(Source.Position.X, Source.Position.Y,120), To(Target.X,Target.Y,120);
    if (!Source.Level)
    {
        for (FBox Box : HighGround)
        {
            Box.Min.Z=0; Box.Max.Z=240;
            if (FMath::LineBoxIntersection(Box,From,To,To-From)) { return false; }
        }
    }
    // Both levels use the same planar blocker sample: a high source cannot see over opaque walls/trees.
    return !GetWorld()->LineTraceTestByChannel(From,To,ECC_GameTraceChannel2,FCollisionQueryParams(SCENE_QUERY_STAT(LDSight),false));
}
TArray<FLDSightSource> ALDVisionDirector::SourcesFor(ALDPlayerController* PC) const
{
    TArray<FLDSightSource> Sources;
    if (bPublicSensor) { Sources.Add({PublicPosition(),600,1}); }
    const auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const double Now=Mode->RuleTime();
    if(Mode->Objectives&&Mode->Objectives->HeartSensorActive(Now)){Sources.Add({LDObjective::Position(LDObjective::Heart),360,1});}
    if (PC && PC->OwnerState.bActive)
    {
        if (const ALDCharacter* Body = Cast<ALDCharacter>(PC->GetPawn()))
        { Sources.Add({Body->GetActorLocation(),720,LevelAt(Body->GetActorLocation())}); }
    }
    for (const auto& Weak : Wards)
    {
        const ALDWard* Ward=Weak.Get();
        if (Ward && Ward->Controller==PC && Ward->Hp>0 && Ward->ExpiresAt>Now)
        { Sources.Add({Ward->GetActorLocation(),560,LevelAt(Ward->GetActorLocation())}); }
    }
    return Sources;
}
bool ALDVisionDirector::CanSee(ALDPlayerController* PC, const FVector& Point) const
{
    const int32 Cell=LDVision::Cell(Point);
    if (Cell==INDEX_NONE) { return false; }
    const auto Sources=SourcesFor(PC);
    return Sources.ContainsByPredicate([&](const FLDSightSource& S){return Ray(S,LDVision::Centre(Cell));})
        && Sources.ContainsByPredicate([&](const FLDSightSource& S){return Ray(S,Point);});
}
void ALDVisionDirector::Register(ALDPlayerController* PC)
{
    if (!PC || Observers.Contains(PC)) { return; }
    FActorSpawnParameters Params; Params.Owner=PC;
    FObserver Observer;
    Observer.Relay=GetWorld()->SpawnActor<ALDWorldViewRelay>(ALDWorldViewRelay::StaticClass(),FTransform::Identity,Params);
    Observer.Explored.SetNumZeroed(LDVision::MaskBytes);
    Observers.Add(PC,MoveTemp(Observer));
}
void ALDVisionDirector::Unregister(ALDPlayerController* PC)
{
    if (FObserver* Observer=Observers.Find(PC)) { if (Observer->Relay.IsValid()) { Observer->Relay->Destroy(); } }
    Observers.Remove(PC);
    for (auto& Weak : Wards) { if (Weak.IsValid() && Weak->Controller==PC) { Weak->Destroy(); } }
}
ALDWorldViewRelay* ALDVisionDirector::RelayFor(ALDPlayerController* PC) const
{ const FObserver* Observer=Observers.Find(PC); return Observer ? Observer->Relay.Get() : nullptr; }
const FLDWorldView* ALDVisionDirector::AuthorityViewFor(ALDPlayerController* PC) const
{ const FObserver* Observer=Observers.Find(PC); return Observer ? &Observer->Pending : nullptr; }
AActor* ALDVisionDirector::ResolveObserved(ALDPlayerController* PC,const FGuid& Id) const
{
    const FObserver* Observer=Observers.Find(PC);
    if (!Observer || !Observer->Visible.Contains(Id)) { return nullptr; }
    for (const auto& Pair : Observer->Ids)
    { if (Pair.Value==Id && Pair.Key.IsValid() && CanSee(PC,Pair.Key->GetActorLocation())) { return Pair.Key.Get(); } }
    return nullptr;
}
void ALDVisionDirector::SetPublicSensor(bool bActive)
{
    check(HasAuthority()); bPublicSensor=bActive;
    if (ALDGameState* State=GetWorld()->GetGameState<ALDGameState>()) { State->bPublicSensorActive=bActive; State->ForceNetUpdate(); }
    UE_LOG(LogTemp,Display,TEXT("LD_FIXTURE public sensor=%d (acquisition/destruction fixture; shared sight is real)"),bActive);
    NextSample=0; NextPublish=0;
}
bool ALDVisionDirector::PlaceWard(ALDPlayerController* PC, const FVector& Desired, FString& Reason)
{
    ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    FLDOwnerState* State=Mode ? Mode->StateFor(PC) : nullptr;
    const APawn* Body=PC ? PC->GetPawn() : nullptr;
    if(Mode&&(!Mode->IsPlaying()||(Mode->Objectives&&Mode->Objectives->HasChannel(PC)))){Reason=TEXT("Ward blocked in lobby, during an interaction or after results.");return false;}
    if (!State || !Body || !State->bActive || !State->WardCharges) { Reason=TEXT("No ward charge or active body."); return false; }
    if(GetWorld()->GetTimeSeconds()<State->ProtectedUntil||(State->ActionCue&15)){Reason=TEXT("Ward blocked during protection or a combat action.");return false;}
    if (!LDVision::InWorld(Desired) || FVector::DistSquared2D(Desired,Body->GetActorLocation())>96*96) { Reason=TEXT("Place within 96 cm of your body."); return false; }
    int32 Active=0;
    for (const auto& Weak : Wards) { if (Weak.IsValid() && Weak->Controller==PC && Weak->Hp>0 && Weak->ExpiresAt>GetWorld()->GetTimeSeconds()) { ++Active; } }
    if (Active>=2) { Reason=TEXT("Two wards are already active."); return false; }
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit,FVector(Desired.X,Desired.Y,600),FVector(Desired.X,Desired.Y,-100),ECC_GameTraceChannel1)
        || Hit.ImpactNormal.Z < FMath::Cos(FMath::DegreesToRadians(40.f))
        || LevelAt(Hit.ImpactPoint)!=LevelAt(Body->GetActorLocation())
        || !Ray({Body->GetActorLocation(),96,LevelAt(Body->GetActorLocation())},Hit.ImpactPoint)
        || GetWorld()->OverlapAnyTestByObjectType(Hit.ImpactPoint+FVector(0,0,15),FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(12)))
    { Reason=TEXT("Ward needs clear, reachable ground on your level."); return false; }
    ALDWard* Ward=GetWorld()->SpawnActor<ALDWard>(Hit.ImpactPoint+FVector(0,0,15),FRotator::ZeroRotator);
    if (!Ward) { Reason=TEXT("Ward could not be placed."); return false; }
    Ward->Identity=FGuid::NewGuid(); Ward->Controller=PC; Ward->ExpiresAt=GetWorld()->GetTimeSeconds()+60;
    Wards.Add(Ward); --State->WardCharges; Mode->SyncOwner(PC);
    Reason=TEXT("Ward placed: private sight for 60 seconds.");
    return true;
}
void ALDVisionDirector::Tick(float Delta)
{
    Super::Tick(Delta);
    const double Now=GetWorld()->GetTimeSeconds();
    const double RuleNow=GetWorld()->GetAuthGameMode<ALDGameMode>()->RuleTime();
    for (int32 Index=Wards.Num()-1;Index>=0;--Index)
    {
        ALDWard* Ward=Wards[Index].Get();
        if (!Ward || Ward->Hp<=0 || RuleNow>=Ward->ExpiresAt)
        { if (Ward) { Ward->Destroy(); } Wards.RemoveAtSwap(Index); }
    }
    if (Now<NextSample) { return; }
    if (NextSample>0 && Now-NextSample>.1) { UE_LOG(LogTemp,Warning,TEXT("LD authority sight sample late by %.3fs; sampling current pose once"),Now-NextSample); }
    NextSample=Now+.05;
    const bool bPublish=Now>=NextPublish;
    if (bPublish) { NextPublish=Now+.1; }
    const double Begin=FPlatformTime::Seconds();Sample(bPublish && bPublicationEnabled);LastSampleMs=(FPlatformTime::Seconds()-Begin)*1000;++SampleSerial;
}
void ALDVisionDirector::Sample(bool bPublish)
{
    // Reserve an earlier revision for concealment and a later one for this
    // sample's full view, so a reliable removal cannot starve equal-revision snapshots.
    Revision+=2;
    TArray<AActor*> Entities;
    for (TActorIterator<ALDCharacter> It(GetWorld()); It; ++It)
    {
        Entities.Add(*It);
        if (GetNetMode()!=NM_DedicatedServer)
        {
            const ALDPlayerController* Local=Cast<ALDPlayerController>(It->GetController());
            It->Body->SetVisibility(Local && Local->IsLocalController() && Local->OwnerState.bActive,true);
        }
    }
    for (auto Weak : Wards) { if (Weak.IsValid()) { Entities.Add(Weak.Get()); } }
    for(TActorIterator<ALDCorpse> It(GetWorld());It;++It){Entities.Add(*It);}
    for(TActorIterator<ALDSeal> It(GetWorld());It;++It){if(It->State==0){Entities.Add(*It);}}
    for(TActorIterator<ALDBook> It(GetWorld());It;++It){if(It->State==0){Entities.Add(*It);}}
    for (auto& Pair : Observers)
    {
        ALDPlayerController* PC=Pair.Key.Get(); FObserver& Observer=Pair.Value;
        if (!PC || !Observer.Relay.IsValid()) { continue; }
        const auto Sources=SourcesFor(PC);
        FLDWorldView View;
        View.Generation=Generation; View.Revision=Revision+1; View.AuthoritySeconds=GetWorld()->GetTimeSeconds();
        View.BodySources=(PC->GetPawn() && PC->OwnerState.bActive) ? 1 : 0;
        const auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
        View.PublicSources=uint8(bPublicSensor?1:0)+uint8(Mode->Objectives&&Mode->Objectives->HeartSensorActive(Mode->RuleTime())?1:0);
        View.WardSources=uint8(Sources.Num()-View.BodySources-View.PublicSources);
        View.Current.SetNumZeroed(LDVision::MaskBytes);
        for (const auto& Source : Sources)
        {
            const int32 X0=FMath::Clamp(int32(FMath::FloorToDouble((Source.Position.X-Source.Radius+2400)/80)),0,59);
            const int32 X1=FMath::Clamp(int32((Source.Position.X+Source.Radius+2400)/80),0,59);
            const int32 Y0=FMath::Clamp(int32(FMath::FloorToDouble((Source.Position.Y-Source.Radius+1600)/80)),0,39);
            const int32 Y1=FMath::Clamp(int32((Source.Position.Y+Source.Radius+1600)/80),0,39);
            for (int32 Y=Y0;Y<=Y1;++Y) { for (int32 X=X0;X<=X1;++X)
            { const int32 I=Y*60+X; if (!LDVision::Bit(View.Current,I) && Ray(Source,LDVision::Centre(I))) { LDVision::Set(View.Current,I); } } }
        }
        for (int32 I=0;I<LDVision::MaskBytes;++I) { Observer.Explored[I]|=View.Current[I]; }
        View.Explored=Observer.Explored;
        TSet<FGuid> Visible;
        for (AActor* Actor : Entities)
        {
            const FVector Position=Actor->GetActorLocation();
            if (Actor==PC->GetPawn() || !LDVision::Bit(View.Current,LDVision::Cell(Position))
                || !Sources.ContainsByPredicate([&](const FLDSightSource& S){return Ray(S,Position);})) { continue; }
            if (View.Entities.Num()>=LDVision::MaxEntities) { UE_LOG(LogTemp,Error,TEXT("LD view capacity exceeded")); break; }
            FGuid& Id=Observer.Ids.FindOrAdd(Actor);
            if (!Id.IsValid()) { Id=FGuid::NewGuid(); }
            FLDObservedEntity Record; Record.Id=Id; Record.Position=Position;
            Record.Facing=uint16(FRotator::ClampAxis(Actor->GetActorRotation().Yaw)/360.0*65535);
            Record.Speed=uint16(FMath::Clamp(Actor->GetVelocity().Size2D(),0.,1000.));
            if (const ALDCharacter* Body=Cast<ALDCharacter>(Actor))
            {
                Record.Presentation=Body->PresentationKey%4;
                if(Mode->Combat){Record.ActionCue=Mode->Combat->CueFor(Body);Record.ExposedUntil=float(Mode->Combat->ExposedFor(Body));}
            }
            else if(const ALDCorpse* Corpse=Cast<ALDCorpse>(Actor)){Record.Presentation=Corpse->Presentation%4;Record.ActionCue=5;}
            else if(Actor->IsA<ALDSeal>()){Record.Kind=ELDObservedKind::Seal;}
            else if(Actor->IsA<ALDBook>()){Record.Kind=ELDObservedKind::Book;}
            else { Record.Kind=ELDObservedKind::Ward; }
            View.Entities.Add(Record); Visible.Add(Id);
        }
        TArray<FGuid> Removed;
        for (const FGuid& Id : Observer.Visible) { if (!Visible.Contains(Id)) { Removed.Add(Id); } }
        if (Removed.Num()) { Observer.Relay->ClientConceal(Generation,Revision,Removed); }
        Observer.Visible=MoveTemp(Visible);
        for (auto It=Observer.Ids.CreateIterator();It;++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
        Observer.Pending=View;
        if (bPublish) { Observer.Relay->ClientFullView(View); }
    }
}
