#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Region/LDRegionDirector.h"
#include "Components/CapsuleComponent.h"
#include "Core/LDGroundQueries.h"
#include "Development/LDV08Loadout.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "World/LDWorldDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"

using namespace LDEnvironment;
bool ALDCombatDirector::EnsureEnvironmentPool()
{
    while(EnvironmentPool.Num()<256){auto* F=GetWorld()->SpawnActor<ALDTacticalField>();if(!F){return false;}F->EnvironmentPool=true;F->Kind=ELDTacticalField::WetSurface;EnvironmentPool.Add(F);}return true;
}
int32 ALDCombatDirector::HeldEnvironment(FGuid SponsorId) const
{int32 Count=0;for(const auto& P:EnvironmentLeases){if(!SponsorId.IsValid()||P.Value.Command.State.BudgetOwner==SponsorId){Count+=P.Value.Count;}}return Count;}
void ALDCombatDirector::RetireEnvironment(int64 At)
{
    for(auto It=EnvironmentLeases.CreateIterator();It;++It){if(It.Value().Expires<=At){It.RemoveCurrent();}}
    for(auto It=RiverCells.CreateIterator();It;++It){It.Value().Expire(At);if(!It.Value().Mask(At)){if(It.Value().Cue.IsValid()){It.Value().Cue->ExpiresAt=0;It.Value().Cue->Conditions=0;}It.RemoveCurrent();}}
}
ALDCombatDirector::FRiverCommand ALDCombatDirector::EnvironmentCommand(ALDCharacter* Body,EInput Input,int64 At,int64 Duration)
{
    FRiverCommand Q;Q.Input=Input;const auto* S=StateFor(Body);const auto* B=BuildFor(Body);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Body?Cast<ALDPlayerController>(Body->GetController()):nullptr;
    Q.State.Owner=PC?M->ContenderId(PC):S?S->Identity:FGuid();Q.State.BudgetOwner=Q.State.Owner;Q.State.Life=S?S->Identity:FGuid();Q.State.Cast=FGuid::NewGuid();Q.State.Created=At;Q.State.Until=At+Duration;Q.State.Revision=++EnvironmentRevision;
    if(HasModifier(Body,TEXT("frost_extent"),At)){Q.FrozenBonus+=1000;}if(HasModifier(Body,TEXT("reaction_duration"),At)){Q.ElectricBonus+=1000;Q.SteamBonus+=1000;}if(B){Q.FrozenBonus+=LDV08::TraitValue(*B,TEXT("frozen_ms"),0);Q.BurningBonus=LDV08::TraitValue(*B,TEXT("burning_ms"),0);Q.ElectricBonus+=LDV08::TraitValue(*B,TEXT("electrified_ms"),0);}return Q;
}
TArray<FIntPoint> ALDCombatDirector::EnvironmentMask(ALDCharacter* Body,FVector Aim,bool Disk,double Length,double Width,int32 Limit,bool Cone) const
{
    TArray<FIntPoint> Mask;if(!Body){return Mask;}for(auto Id:LDEnvironment::Mask(Body->GetActorLocation(),Aim,Length,Width,Disk,Limit,Cone))
    {FVector Ground;if(LDGround::MapSurface(GetWorld(),LDRiver::Centre(Id),Ground)&&LDGround::ClearSegment(GetWorld(),Disk?Aim+FVector(0,0,30):Body->GetActorLocation(),Ground+FVector(0,0,30))){Mask.Add(Id);}}return Mask;
}
bool ALDCombatDirector::QueueEnvironment(FRiverCommand Q,int64 At,bool Reserved)
{
    RetireEnvironment(At);if(RiverTransitionAt!=At/100){RiverTransitionAt=At/100;RiverEvaluations=0;}
    if(!Reserved&&RiverEvaluations+HeldEnvironment()+Q.Cells.Num()>512){return false;}
    RiverEvaluations+=Q.Cells.Num();
    auto Projected=RiverCommands.IsEmpty()?RiverCells:ProjectedEnvironment;const bool River=GetWorld()->GetMapName().Contains(TEXT("L_V08Region"));
    for(auto Id:Q.Cells){FCell C=Projected.FindRef(Id);const bool Ice=C.Frozen.Until>At;if(Transition(C,Q,LDEnvironment::Substrate(Id,River),At)){if(Q.Input==EInput::Shatter&&Ice){Q.Shattered.Add(Id);}Q.Outcomes.Add(Id,C);if(C.Mask(At)){Projected.Add(Id,C);}else{Projected.Remove(Id);}}}
    const FGuid Sponsor=Q.State.BudgetOwner.IsValid()?Q.State.BudgetOwner:Q.State.Owner;int32 Owned=0;for(const auto& P:Projected){Owned+=P.Value.Owned(Sponsor,At);}
    if(!Reserved&&(Projected.Num()+HeldEnvironment()>256||Owned+HeldEnvironment(Sponsor)>64)){return false;}
    if(!EnsureEnvironmentPool()){return false;}ProjectedEnvironment=MoveTemp(Projected);RiverCommands.Add(MoveTemp(Q));return true;
}
bool ALDCombatDirector::StageEnvironment(ALDCharacter* Body,FVector Aim,EInput Input,bool Disk,double Length,double Width,int64 At,int64 Duration,int32 Limit,bool Cone)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->IsV08Profile()||!BuildFor(Body)){return false;}
    auto Q=EnvironmentCommand(Body,Input,At,Duration);Q.Cells=EnvironmentMask(Body,Aim,Disk,Length,Input==EInput::Frost&&HasModifier(Body,TEXT("frost_radius"),At)?Width*1.1:Width,Limit,Cone);return QueueEnvironment(MoveTemp(Q),At);
}
bool ALDCombatDirector::StageRiver(ALDCharacter* Body,FVector Aim,bool Disk,bool Frozen,double Length,double Width,int64 At,int64 Duration)
{return StageEnvironment(Body,Aim,Frozen?EInput::Frost:EInput::Water,Disk,Length,Width,At,Duration);}
FGuid ALDCombatDirector::ReserveEnvironment(ALDCharacter* Body,int32 Count,int64 At,int64 Duration)
{
    RetireEnvironment(At);if(RiverTransitionAt!=At/100){RiverTransitionAt=At/100;RiverEvaluations=0;}auto Q=EnvironmentCommand(Body,EInput::Fire,At,Duration);const FGuid Sponsor=Q.State.BudgetOwner;
    TSet<FIntPoint> All,Owned;for(const auto& P:RiverCells){All.Add(P.Key);if(P.Value.Owned(Sponsor,At)){Owned.Add(P.Key);}}for(const auto& P:RiverCommands){for(auto Id:P.Cells){All.Add(Id);if(P.State.BudgetOwner==Sponsor){Owned.Add(Id);}}}
    if(Count<1||Count>32||All.Num()+HeldEnvironment()+Count>256||Owned.Num()+HeldEnvironment(Sponsor)+Count>64||RiverEvaluations+HeldEnvironment()+Count>512||!EnsureEnvironmentPool()){return {};}
    const FGuid Id=FGuid::NewGuid();EnvironmentLeases.Add(Id,{Q,Count,At+10000});return Id;
}
void ALDCombatDirector::ReleaseEnvironment(FGuid Lease){EnvironmentLeases.Remove(Lease);}
bool ALDCombatDirector::ImpactEnvironment(FGuid Lease,FVector Point,ALDCharacter* Source,int64 At)
{
    auto* Entry=EnvironmentLeases.Find(Lease);if(!Entry){return false;}auto Q=Entry->Command;const int32 Count=Entry->Count;const int64 Span=Q.State.Until-Q.State.Created;
    Q.State.Created=At;Q.State.Until=At+Span;
    if(const auto* S=StateFor(Source);S&&S->Identity!=Q.State.Life){auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Cast<ALDPlayerController>(Source->GetController());Q.State.Owner=PC?M->ContenderId(PC):S->Identity;Q.State.Life=S->Identity;}
    for(auto Id:LDEnvironment::Mask(Point,Point,0,100,true,Count)){FVector G;if(LDGround::MapSurface(GetWorld(),LDRiver::Centre(Id),G)&&LDGround::ClearSegment(GetWorld(),Point+FVector(0,0,30),G+FVector(0,0,30))){Q.Cells.Add(Id);}}EnvironmentLeases.Remove(Lease);
    return QueueEnvironment(MoveTemp(Q),At,true);
}
void ALDCombatDirector::PublishEnvironmentCues(int64 At)
{
    ObscuredCells.Reset();for(auto& P:RiverCells)
    {
        auto& C=P.Value;const uint8 Bits=C.Mask(At);if(Bits&(1<<4)){ObscuredCells.Add(P.Key);}if(!C.Cue.IsValid()){continue;}
        C.Cue->Conditions=Bits;const int32 Priority[]={2,3,1,4,5,0,6};int32 Main=0;for(int32 I:Priority){if(Bits&(1<<I)){Main=I;break;}}
        const ELDTacticalField Kinds[]={ELDTacticalField::WetSurface,ELDTacticalField::FrozenSurface,ELDTacticalField::BurningSurface,ELDTacticalField::ElectricSurface,ELDTacticalField::SmokeSurface,ELDTacticalField::GrowthSurface,ELDTacticalField::OilSurface};C.Cue->Kind=Kinds[Main];C.Cue->ExpiresAt=0;for(uint8 I=0;I<7;++I){C.Cue->ExpiresAt=FMath::Max(C.Cue->ExpiresAt,C.Get(EState(I)).Until);}
    }
}
void ALDCombatDirector::CommitRiver(int64 At)
{
    if(RiverCommands.IsEmpty()){return;}const bool River=GetWorld()->GetMapName().Contains(TEXT("L_V08Region"));
    for(const auto& Q:RiverCommands)
    {
        TSet<FGuid> Shattered;
        for(auto Id:Q.Cells)
        {
            auto* Existing=RiverCells.Find(Id);const auto* Planned=Q.Outcomes.Find(Id);if(!Planned){continue;}FCell Candidate=*Planned;Candidate.Cue=Existing?Existing->Cue:TWeakObjectPtr<ALDTacticalField>();const bool Ice=Q.Shattered.Contains(Id);
            if(Q.Input==EInput::Shatter&&Ice){for(const auto& P:Bodies){auto* B=P.Key.Get();if(B&&P.Value.Hp>0&&LDRiver::Cell(B->GetActorLocation())==Id&&P.Value.Identity!=Q.State.Life&&!Shattered.Contains(P.Value.Identity)&&At>=LDTactical::Milliseconds(P.Value.ProtectionUntil)){Shattered.Add(P.Value.Identity);FLDCombatHit H{nullptr,B,double(Q.State.Magnitude?Q.State.Magnitude:80),LDTactical::EDamage::Physical,false,0,0,Q.State.Life};H.CreditOwner=Q.State.Owner;H.TargetLife=P.Value.Identity;SurfaceBursts.Add(H);}}}
            if(!Candidate.Mask(At)){if(Existing&&Existing->Cue.IsValid()){Existing->Cue->ExpiresAt=0;}RiverCells.Remove(Id);continue;}
            if(!Candidate.Cue.IsValid())
            {for(auto& F:EnvironmentPool){if(F.IsValid()&&F->ExpiresAt<=At){Candidate.Cue=F;F->ExpiresAt=MAX_int64;FVector G;LDGround::MapSurface(GetWorld(),LDRiver::Centre(Id),G);F->SetActorLocation(G+FVector(0,0,5));break;}}check(Candidate.Cue.IsValid());}
            RiverCells.Add(Id,Candidate);
        }
    }
    for(const auto& Q:RiverCommands){auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&M->ContenderId(PC)==Q.State.Owner){if(auto* O=M->StateFor(PC)){O->LastSurfaceMask=Q.Cells;O->LastSurfaceAt=At;}break;}}}
    RiverCommands.Reset();ProjectedEnvironment.Reset();PublishEnvironmentCues(At);
    for(auto& P:Bodies){if(P.Key.IsValid()){RefreshRiverContact(P.Key.Get(),At);}}
    if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M->Vision){M->Vision->RefreshEnvironment();}
}
bool ALDCombatDirector::EnvironmentOccludes(FVector A,FVector B,bool SurfaceCue) const{if(!LDEnvironment::Occludes(A,B,ObscuredCells)){return false;}if(!SurfaceCue){return true;}auto Boundary=ObscuredCells;Boundary.Remove(LDRiver::Cell(B));return LDEnvironment::Occludes(A,B,Boundary);}
void ALDCombatDirector::SampleRiver(int64 At)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->IsV08Profile()){return;}RetireEnvironment(At);PublishEnvironmentCues(At);
    for(auto It=BurningUnits.CreateIterator();It;++It){if(!It.Key().IsValid()||It.Value().Until<=At){It.RemoveCurrent();}}
    for(auto It=FrostReceipts.CreateIterator();It;++It){if(It.Value()<=At){It.RemoveCurrent();}}
    const bool Boundary=At/100>RiverBoundary;if(Boundary){RiverBoundary=At/100;}for(auto& P:Bodies){if(P.Key.IsValid()){RefreshRiverContact(P.Key.Get(),At,Boundary);}}
    for(auto It=RiverContacts.CreateIterator();It;++It){if(!It.Key().IsValid()){It.RemoveCurrent();}}
}
void ALDCombatDirector::RefreshRiverContact(ALDCharacter* Body,int64 At,bool Boundary)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* S=StateFor(Body);if(!M||!M->IsV08Profile()||!S||!Body){return;}auto& Last=RiverContacts.FindOrAdd(Body);if(At<Last.SampledAt){return;}
    const FVector Feet=Body->GetActorLocation()-FVector(0,0,Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());const auto* C=RiverCells.Find(LDRiver::Cell(Feet));FVector G;const bool Floor=LDGround::MapSurface(GetWorld(),Feet,G)&&FMath::Abs(Feet.Z-G.Z)<=12;
    const bool WaterContact=Floor&&((GetWorld()->GetMapName().Contains(TEXT("L_V08Region"))&&LDRiver::InRiver(Feet))||(C&&(C->Wet.Until>At||(C->Frozen.Until>At&&C->Frozen.WetSubstrate))));
    LDRiver::FContactInput Input;
    Input.Floor=Floor; Input.Water=WaterContact; Input.Frozen=C&&C->Frozen.Until>At; Input.Growth=C&&C->Overgrown.Until>At;
    Input.Alive=S->Hp>0; Input.Protected=At<LDTactical::Milliseconds(S->ProtectionUntil); Input.Immune=Body->IsA<ALDWildSentinel>();
    Input.Wading=HasModifier(Body,TEXT("river_wading"),At); Input.IceGrip=HasModifier(Body,TEXT("ice_grip"),At);
    if(LDRiver::RefreshContact(S->Tactical,Last.Wet,Last.SampledAt,Input,At,Boundary)) { BurningUnits.Remove(Body); }
}
void ALDCombatDirector::EnvironmentDamage(int64 At,TArray<FLDCombatHit>& Hits)
{
    Hits.Append(SurfaceBursts);SurfaceBursts.Reset();if(At/500<=EnvironmentDamageAt){return;}EnvironmentDamageAt=At/500;
    for(auto& P:Bodies)
    {
        auto* B=P.Key.Get();auto& S=P.Value;if(!B||S.Hp<=0||At<LDTactical::Milliseconds(S.ProtectionUntil)){continue;}const auto* C=RiverCells.Find(LDRiver::Cell(B->GetActorLocation()));
        FVector Ground;const FVector Feet=B->GetActorLocation()-FVector(0,0,B->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());const bool Floor=LDGround::MapSurface(GetWorld(),Feet,Ground)&&FMath::Abs(Feet.Z-Ground.Z)<=12;
        auto Add=[&](const FRecord& R,int32 Raw){FLDCombatHit H{nullptr,B,Raw/2.,LDTactical::EDamage::Magical,false,0,0,R.Life};H.Periodic=true;H.EnvironmentKind=Raw==18?2:Raw==12?3:1;H.TargetLife=S.Identity;H.CreditOwner=R.Owner;Hits.Add(H);};
        const auto* Unit=BurningUnits.Find(B);if(!S.Tactical.Has(LDTactical::EStatus::Burning,At)){BurningUnits.Remove(B);Unit=nullptr;}
        if(Floor&&C&&C->Burning.Until>At){Add(C->Burning,20);}else if(Unit&&Unit->Until>At){Add(*Unit,12);}
        if(Floor&&C&&C->Electrified.Until>At){Add(C->Electrified,18);}
    }
}
void ALDArcProjectile::EndPlay(const EEndPlayReason::Type Reason)
{if(EnvironmentLease.IsValid()){if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Combat){M->Combat->ReleaseEnvironment(EnvironmentLease);}}Super::EndPlay(Reason);}
