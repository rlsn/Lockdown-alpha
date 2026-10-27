#include "Region/LDMapSurfaceSubsystem.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDB90Preview.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void ULDMapSurfaceSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    if(World.GetNetMode()==NM_Standalone&&World.GetAuthGameMode<ALDB90PreviewMode>()) { Definition=ULDMapDefinition::Get(&World); }
}
TStatId ULDMapSurfaceSubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(ULDMapSurfaceSubsystem,STATGROUP_Tickables); }
bool ULDMapSurfaceSubsystem::Cell(FVector P,FIntPoint& Out)
{
    if(P.ContainsNaN()||P.X<-120000||P.Y<-120000||P.X>=120000||P.Y>=120000) { return false; }
    Out=FIntPoint(FMath::FloorToInt((P.X+120000)/200),FMath::FloorToInt((P.Y+120000)/200)); return true;
}
const FLDMapContact* ULDMapSurfaceSubsystem::Contact(const ALDCharacter* Body) const
{ return Contacts.Find(const_cast<ALDCharacter*>(Body)); }
void ULDMapSurfaceSubsystem::Refresh(ALDCharacter* Body,int64 At,bool Boundary)
{
    if(!Definition||!Body||!Body->HasAuthority()) { return; }
    auto& Contact=Contacts.FindOrAdd(Body); if(At<Contact.SampledAt) { return; }
    const FVector Feet=Body->GetActorLocation()-FVector(0,0,Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    FCollisionQueryParams Query(SCENE_QUERY_STAT(LDMapContact),false,Body); FHitResult Hit;
    const bool Floor=GetWorld()->LineTraceSingleByChannel(Hit,Feet+FVector(0,0,10),Feet-FVector(0,0,40),ECC_GameTraceChannel1,Query)
        &&FMath::Abs(Feet.Z-Hit.ImpactPoint.Z)<=12&&Hit.ImpactNormal.Z>=FMath::Cos(FMath::DegreesToRadians(40.));
    FIntPoint Id; const auto* Surface=Floor&&Cell(Hit.ImpactPoint,Id)?Cells.Find(Id):nullptr;
    const bool SameLayer=Surface&&FMath::Abs(Hit.ImpactPoint.Z-Surface->Z)<12;
    LDRiver::FContactInput Input; Input.Floor=Floor; Input.Water=Floor&&Definition->RiverAt(Hit.ImpactPoint);
    Input.Frozen=SameLayer&&Surface->Conditions.Frozen.Until>At;
    LDRiver::RefreshContact(Contact.State,Contact.Wet,Contact.SampledAt,Input,At,Boundary);
    Contact.Frozen=Input.Frozen;
}
void ULDMapSurfaceSubsystem::Tick(float Delta)
{
    if(!Definition) { return; }
    const int64 At=LDTactical::Milliseconds(GetWorld()->GetTimeSeconds());
    if(At/100==BoundaryAt) { return; } BoundaryAt=At/100;
    for(auto It=Cells.CreateIterator();It;++It) { It.Value().Conditions.Expire(At); if(!It.Value().Conditions.Mask(At)) { It.RemoveCurrent(); } }
    for(TActorIterator<ALDCharacter> It(GetWorld());It;++It) { Refresh(*It,At,true); }
    for(auto It=Contacts.CreateIterator();It;++It) { if(!It.Key().IsValid()) { It.RemoveCurrent(); } }
}
bool ULDMapSurfaceSubsystem::SeedFrozenForProbe(FVector Surface,FGuid Owner,int64 At,FString& Why)
{
    const bool Probe=FParse::Param(FCommandLine::Get(),TEXT("LDB90RiverProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDB90ClosureProbe"));
    if(!Definition||!Probe||!Owner.IsValid()||!Definition->RiverAt(Surface))
    { Why=TEXT("Frozen fixture requires authored water and explicit river-probe mode"); return false; }
    FIntPoint Centre; if(!Cell(Surface,Centre)) { return false; }
    TMap<FIntPoint,FSurface> Candidate=Cells; int32 Owned=0;
    for(const auto& P:Candidate) { if(P.Value.Conditions.Owned(Owner,At)) { ++Owned; } }
    for(int32 Y=-1;Y<=1;++Y) { for(int32 X=-1;X<=1;++X)
    {
        const FIntPoint Id=Centre+FIntPoint(X,Y); const FVector P(-119900+Id.X*200,-119900+Id.Y*200,Surface.Z);
        if(Id.X<0||Id.Y<0||Id.X>=1200||Id.Y>=1200||!Definition->RiverAt(P)) { continue; }
        auto* Old=Candidate.Find(Id);
        if((!Old||!Old->Conditions.Owned(Owner,At))&&++Owned>64) { Why=TEXT("Owner surface budget exhausted"); return false; }
        auto& C=Candidate.FindOrAdd(Id); C.Z=Surface.Z;
        LDEnvironment::FCommand Q; Q.Input=LDEnvironment::EInput::Frost; Q.State.Owner=Q.State.BudgetOwner=Owner;
        Q.State.Created=At; Q.State.Until=At+10000;
        LDEnvironment::Transition(C.Conditions,Q,LDEnvironment::Water,At);
    } }
    if(Candidate.Num()>1024) { Why=TEXT("Full-map surface budget exhausted"); return false; }
    Cells=MoveTemp(Candidate); Why=TEXT("Diagnostic water/Frost transition seeded; no paid action claimed"); return true;
}
void ULDMapSurfaceSubsystem::ResetProbe()
{ if(FParse::Param(FCommandLine::Get(),TEXT("LDB90RiverProbe"))) { Cells.Reset(); Contacts.Reset(); BoundaryAt=-1; } }
