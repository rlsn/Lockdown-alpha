#include "Combat/LDCombatDirector.h"
#include "Combat/LDRiverRules.h"
#include "Character/LDCharacter.h"
#include "Region/LDRegionDirector.h"
#include "Components/CapsuleComponent.h"
#include "Core/LDGroundQueries.h"
#include "Development/LDV08Loadout.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "World/LDWorldDirector.h"
#include "Engine/World.h"

void ALDCombatDirector::SampleRiver(int64 At)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->IsV08Profile()){return;}
    for(auto It=RiverCells.CreateIterator();It;++It)
    {
        auto& C=It.Value();if(C.Wet.Until<=At){C.Wet={};}if(C.Frozen.Until<=At){C.Frozen={};}
        if(!C.Wet.Until&&!C.Frozen.Until){if(C.Cue.IsValid()){C.Cue->Destroy();}It.RemoveCurrent();}
        else if(C.Cue.IsValid()){C.Cue->Kind=C.Frozen.Until?ELDTacticalField::FrozenSurface:ELDTacticalField::WetSurface;C.Cue->ExpiresAt=FMath::Max(C.Wet.Until,C.Frozen.Until);}
    }
    for(auto It=FrostReceipts.CreateIterator();It;++It){if(It.Value()<=At){It.RemoveCurrent();}}
    const bool Boundary=At/100>RiverBoundary;if(Boundary){RiverBoundary=At/100;}
    for(auto& Pair:Bodies){if(Pair.Key.IsValid()){RefreshRiverContact(Pair.Key.Get(),At,Boundary);}}
    for(auto It=RiverContacts.CreateIterator();It;++It){if(!It.Key().IsValid()){It.RemoveCurrent();}}
}

void ALDCombatDirector::RefreshRiverContact(ALDCharacter* Body,int64 At,bool Boundary)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* S=StateFor(Body);
    if(!M||!M->IsV08Profile()||!S||!Body){return;}
    auto& Last=RiverContacts.FindOrAdd(Body);if(At<Last.SampledAt){return;}
    const FVector Feet=Body->GetActorLocation()-FVector(0,0,Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const auto* C=RiverCells.Find(LDRiver::Cell(Feet));FVector Surface;
    const bool OnFloor=LDGround::MapSurface(GetWorld(),Feet,Surface)&&FMath::Abs(Feet.Z-Surface.Z)<=12;
    const bool Wet=OnFloor&&((GetWorld()->GetMapName().Contains(TEXT("L_V08Region"))&&LDRiver::InRiver(Feet))||(C&&C->Wet.Until>At));
    auto* PC=Cast<ALDPlayerController>(Body->GetController());auto* O=PC?M->StateFor(PC):nullptr;
    const auto* Waders=O?O->Inventory.Find(O->Inventory.Bindings.IsValidIndex(2)?O->Inventory.Bindings[2]:FGuid()):nullptr;
    const auto* Treads=O?O->Inventory.Find(O->Inventory.Bindings.IsValidIndex(3)?O->Inventory.Bindings[3]:FGuid()):nullptr;
    if(S->Hp>0&&((Wet&&(Boundary||!Last.Wet))||(!Wet&&Last.Wet&&S->Tactical.Has(LDTactical::EStatus::Wet,At))))
    {LDTactical::Apply(S->Tactical,LDTactical::EStatus::Wet,At+(Waders&&Waders->Kind==ELDItemKind::RiverWaders?1500:3000),FGuid());}
    S->Tactical.SurfaceSlow=S->Hp>0&&At>=LDTactical::Milliseconds(S->ProtectionUntil)&&OnFloor&&C&&C->Frozen.Until>At&&!Body->IsA<ALDWildSentinel>()&&!(Cast<ALDWorldPawn>(Body)&&Cast<ALDWorldPawn>(Body)->ImmuneTo(LDTactical::EStatus::Slow))?(Treads&&Treads->Kind==ELDItemKind::RiverTreads?15:30):0;
    Last.Wet=Wet;Last.SampledAt=At;
}

bool ALDCombatDirector::StageRiver(ALDCharacter* Body,FVector Aim,bool Disk,bool Frozen,double Length,double Width,int64 At,int64 Duration)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* B=BuildFor(Body);const auto* S=StateFor(Body);
    if(!M||!M->IsV08Profile()||!B||!S){return false;}
    FRiverCommand Command;Command.Frozen=Frozen;Command.State.Life=S->Identity;Command.State.Cast=FGuid::NewGuid();
    auto* PC=Cast<ALDPlayerController>(Body->GetController());Command.State.Owner=PC?M->ContenderId(PC):S->Identity;
    Command.State.Created=At;Command.State.Until=At+FMath::Clamp<int64>(Frozen?LDV08::TraitValue(*B,TEXT("frozen_ms"),Duration):Duration,0,10000);
    const FVector Start=Body->GetActorLocation();
    const auto Mask=LDRiver::Footprint(Start,Aim,Length,Width,Disk);
    if(RiverTransitionAt!=At/100){RiverTransitionAt=At/100;RiverEvaluations=0;}
    if(RiverEvaluations+Mask.Num()>512){return false;}RiverEvaluations+=Mask.Num();
    for(const auto Cell:Mask)
    {
        FVector Ground;const FVector P=LDRiver::Centre(Cell);const auto* Existing=RiverCells.Find(Cell);
        bool Substrate=(GetWorld()->GetMapName().Contains(TEXT("L_V08Region"))&&LDRiver::InRiver(P))||(Existing&&Existing->Wet.Until>At);
        for(const auto& Pending:RiverCommands){if(!Pending.Frozen&&Pending.Cells.Contains(Cell)){Substrate=true;}}
        if(Frozen&&!Substrate){continue;}
        if(!LDGround::MapSurface(GetWorld(),P,Ground)||!LDGround::ClearSegment(GetWorld(),Disk?Aim+FVector(0,0,30):Start,Ground+FVector(0,0,30))){continue;}
        Command.Cells.Add(Cell);
    }
    // Include this sample's accepted commands when reserving all distinct cells.
    TSet<FIntPoint> All,Owned;
    for(const auto& Pair:RiverCells){All.Add(Pair.Key);if((Pair.Value.Wet.Until>At&&Pair.Value.Wet.Owner==Command.State.Owner)||(Pair.Value.Frozen.Until>At&&Pair.Value.Frozen.Owner==Command.State.Owner)){Owned.Add(Pair.Key);}}
    for(const auto& Pending:RiverCommands){for(auto C:Pending.Cells){All.Add(C);if(Pending.State.Owner==Command.State.Owner){Owned.Add(C);}}}
    for(auto C:Command.Cells){All.Add(C);Owned.Add(C);}
    if(All.Num()>LDRiver::MatchCap||Owned.Num()>LDRiver::OwnerCap||RiverCommands.Num()>=32){return false;}
    // Allocate every new presentation actor before spending; canonical records stay private.
    TArray<FIntPoint> Allocated;
    for(auto Cell:Command.Cells)
    {
        if(RiverCells.Contains(Cell)){continue;}FVector Ground;LDGround::MapSurface(GetWorld(),LDRiver::Centre(Cell),Ground);
        auto* Cue=GetWorld()->SpawnActor<ALDTacticalField>(Ground+FVector(0,0,5),FRotator::ZeroRotator);
        if(!Cue){for(auto C:Allocated){if(RiverCells[C].Cue.IsValid()){RiverCells[C].Cue->Destroy();}RiverCells.Remove(C);}return false;}
        Cue->Kind=Frozen?ELDTacticalField::FrozenSurface:ELDTacticalField::WetSurface;Cue->ExpiresAt=At;Cue->Hp=1;
        RiverCells.Add(Cell).Cue=Cue;Allocated.Add(Cell);
    }
    RiverCommands.Add(MoveTemp(Command));return true;
}

void ALDCombatDirector::CommitRiver(int64 At)
{
    if(RiverCommands.IsEmpty()){return;}
    for(const auto& Command:RiverCommands){for(auto Id:Command.Cells)
    {auto& C=RiverCells.FindChecked(Id);auto& State=Command.Frozen?C.Frozen:C.Wet;const int64 Old=State.Until;State=Command.State;State.Until=FMath::Min(At+10000,FMath::Max(State.Until,Old));
        if(Command.Frozen&&C.Wet.Until>At){C.Wet.Until=FMath::Max(C.Wet.Until,C.Frozen.Until);}
        if(C.Cue.IsValid()){C.Cue->Kind=C.Frozen.Until>At?ELDTacticalField::FrozenSurface:ELDTacticalField::WetSurface;C.Cue->ExpiresAt=FMath::Max(C.Wet.Until,C.Frozen.Until);}}}
    RiverCommands.Reset();
    // Newly created contact is after this batch's direct packets; never field-triggered stun.
    for(auto& Pair:Bodies){if(Pair.Key.IsValid()){RefreshRiverContact(Pair.Key.Get(),At);}}
}

void ALDCombatDirector::ElementContact(const FLDCombatHit& Hit,ALDCharacter* Victim,int64 At)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* T=StateFor(Victim);const auto* S=StateFor(Hit.Source);
    if(!T||!S||(Hit.TargetLife.IsValid()&&Hit.TargetLife!=T->Identity)||Hit.Source==Victim||Hit.Periodic||At<LDTactical::Milliseconds(T->ProtectionUntil)){return;}
    if(Hit.DirectWater){LDTactical::Apply(T->Tactical,LDTactical::EStatus::Wet,At+3000,FGuid());}
    if(!Hit.DirectFrost){return;}
    const FString Receipt=S->Identity.ToString()+TEXT(":")+FString::FromInt(Hit.Token)+TEXT(":")+T->Identity.ToString();
    if(FrostReceipts.Contains(Receipt)){return;}FrostReceipts.Add(Receipt,At+10000);
    auto* PC=Cast<ALDPlayerController>(Victim->GetController());auto* O=PC?M->StateFor(PC):nullptr;int64& Ready=O?O->WetFrostReadyAt:T->WetFrostReadyAt;
    if(LDRiver::AttemptFrost(T->Tactical,Ready,At))
    {
        T->WetFrostReadyAt=Ready;ApplyStatus(Victim,LDTactical::EStatus::Stun,At+600,S->Identity);
        UE_LOG(LogTemp,Display,TEXT("LD_B82_FROST at=%lld ready=%lld applied=%d token=%u"),At,Ready,T->Tactical.Has(LDTactical::EStatus::Stun,At),Hit.Token);
    }
}
