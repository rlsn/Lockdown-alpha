#include "World/LDWorldDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Core/LDGroundQueries.h"
#include "Engine/World.h"
void ALDWorldDirector::Reserve(ALDPlayerController* PC,uint8 Route,bool Value)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);if(!O){return;}if(Value){FPending P;P.Route=Route;P.Build=O->Build.Revision;P.Inventory=O->Inventory.Revision;if(Route==9){P.Key=KeyTarget(PC);if(P.Key.IsValid()){P.KeyRevision=P.Key->Revision;P.Key->bReserved=true;}}for(const auto& I:O->Inventory.Items){if(Route==5&&I.Kind>=ELDItemKind::AncientKeyA&&I.Kind<=ELDItemKind::AncientKeyC||Route==6&&I.Kind==ELDItemKind::Heartbreaker){P.Items.Add(I);}}Pending.Add(PC,P);}
    const auto* P=Pending.Find(PC);if(!P){KeyCandidates.Remove(PC);return;}if(!Value&&P->Key.IsValid()){P->Key->bReserved=false;}for(auto& I:O->Inventory.Items){if(P->Items.ContainsByPredicate([&](const auto& V){return V.Id==I.Id;})){I.bReserved=Value;if(auto* A=M->Inventory->Owned(PC,I.Id)){A->bReserved=Value;}}}if(!Value){Pending.Remove(PC);KeyCandidates.Remove(PC);}M->Inventory->Changed(PC);
}
void ALDWorldDirector::InventoryChanged(ALDPlayerController* PC)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);if(!O){return;}for(const auto& I:O->Inventory.Items){if(I.Kind>=ELDItemKind::AncientKeyA&&I.Kind<=ELDItemKind::AncientKeyC){O->KeysReadyAt=FMath::Max(O->KeysReadyAt,I.ReadyAt);}}
    if(const auto* L=Leases.Find(PC);L&&L->Route==5){for(const auto& Id:L->Keys){if(!O->Inventory.Find(Id)){UE_LOG(LogTemp,Display,TEXT("LD_B76_LEASE_REVOKE owner=%s life=%u route=5 missing=%s at=%.6f"),*M->ContenderId(PC).ToString(),O->Life,*Id.ToString(),GetWorld()->GetTimeSeconds());Leases.Remove(PC);break;}}}
    const auto* L=Leases.Find(PC);O->BreachRoute=L?L->Route:0;O->BreachOpenAt=L?L->Open:0;O->BreachCloseAt=L?L->Close:0;
}
void ALDWorldDirector::EndLife(ALDPlayerController* PC){Reserve(PC,0,false);Leases.Remove(PC);InventoryChanged(PC);}
uint8 ALDWorldDirector::Authorization(ALDPlayerController* PC,double At,double Through) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);const auto* G=M->GetGameState<ALDGameState>();if(!O||!O->bActive||Through>G->MatchDeadline+1.e-7){return 0;}if(G->DefensiveFlags==0){return 2;}
    struct FInterval{double A,B;uint8 Route;};TArray<FInterval> Intervals;for(const auto& W:CharterHistory){Intervals.Add({W.Open,W.Close,4});}if(G->CharterWindow.IsValid()){Intervals.Add({G->CharterOpenAt,G->CharterCloseAt,4});}
    const auto* L=Leases.Find(PC);if(L&&L->Life==O->Life){bool Good=true;for(auto Id:L->Keys){Good&=O->Inventory.Find(Id)!=nullptr;}if(Good){Intervals.Add({L->Open,L->Close,L->Route});}}
    Intervals.Sort([](const auto& A,const auto& B){return A.A<B.A;});double Covered=At;uint8 Result=0;for(const auto& I:Intervals){if(I.A<=Covered+1.e-7&&I.B>=Covered-1.e-7){Covered=FMath::Max(Covered,I.B);if(!Result){Result=I.Route;}if(Covered+1.e-7>=Through){return Result;}}}return 0;
}
bool ALDWorldDirector::CanActivate(ALDPlayerController* PC,uint8 Route,double End,FString& Why) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);const auto* B=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=M->Combat->StateFor(B);const double Now=GetWorld()->GetTimeSeconds();auto Fail=[&](){Why=TEXT("Activation unavailable: life, quiet, range, deadline, source or cooldown changed");return false;};
    if(!O||!O->bActive||!S||S->Hp<=0||Now<O->ProtectedUntil||Now<S->ProtectionUntil||!M->IsPlaying()||!M->Combat->Allows(const_cast<ALDCharacter*>(B),LDTactical::EAction::Service)||!M->Combat->QuietForPreparation(const_cast<ALDCharacter*>(B))||O->DevelopmentKind||S->Action){return Fail();}
    if(Route==9){const auto* Item=KeyTarget(PC);const auto* Hold=Pending.Find(PC);if(!Item||Item->State!=0||Item->Holder.IsValid()||Item->bReserved&&(!Hold||Hold->Key.Get()!=Item)||Item->Kind<ELDItemKind::AncientKeyA||Item->Kind>ELDItemKind::AncientKeyC||!M->Inventory->HasSpace(PC,Item->Kind)||FVector::DistSquared2D(B->GetActorLocation(),Item->GetActorLocation())>80*80||!M->Vision->CanObserve(PC,const_cast<ALDItem*>(Item))||!LDGround::ClearSegment(GetWorld(),B->GetActorLocation(),Item->GetActorLocation())){return Fail();}return End<=M->GetGameState<ALDGameState>()->MatchDeadline;}
    const FVector Place=Position(Route==7?TEXT("ShrineWest"):Route==8?TEXT("ShrineEast"):TEXT("HeartAntechamber"));if(FVector::DistSquared2D(B->GetActorLocation(),Place)>120*120||!M->Vision->Ray({B->GetActorLocation(),120,M->Vision->LevelAt(B->GetActorLocation())},Place)||!LDGround::ClearSegment(GetWorld(),B->GetActorLocation(),Place)){return Fail();}
    if(Route>=7){return End<=M->GetGameState<ALDGameState>()->MatchDeadline&&Now>=O->ShrineReadyAt;}
    if(End+90>M->GetGameState<ALDGameState>()->MatchDeadline+1.e-7||(Leases.Contains(PC)&&Now<Leases.FindChecked(PC).Close-1.e-7)){return Fail();}
    if(Route==5){TSet<ELDItemKind> K;for(const auto& I:O->Inventory.Items){if(I.Kind>=ELDItemKind::AncientKeyA&&I.Kind<=ELDItemKind::AncientKeyC&&I.ReadyAt<=LDTactical::Milliseconds(Now)&&(!I.bReserved||Pending.Contains(PC))){K.Add(I.Kind);}}if(K.Num()!=3||O->KeysReadyAt>LDTactical::Milliseconds(Now)){return Fail();}}
    else if(Route==6){if(!O->Inventory.Items.ContainsByPredicate([&](const auto& I){return I.Kind==ELDItemKind::Heartbreaker&&(!I.bReserved||Pending.Contains(PC));})){return Fail();}}else{return Fail();}return true;
}
bool ALDWorldDirector::Activate(ALDPlayerController* PC,uint8 Route,double At,FString& Why)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);const auto* P=Pending.Find(PC);if(!P||P->Route!=Route||P->Build!=O->Build.Revision||P->Inventory!=O->Inventory.Revision||!CanActivate(PC,Route,At,Why)){return false;}
    if(Route==9){auto* Key=P->Key.Get();if(!Key||Key->Revision!=P->KeyRevision||!Key->bReserved){return false;}Key->bReserved=false;const bool Good=M->Inventory->Adopt(PC,Key);Reserve(PC,Route,false);return Good;}
    for(const auto& V:P->Items){const auto* A=M->Inventory->Owned(PC,V.Id);if(!A||!A->bReserved||A->Revision!=V.Revision||A->Kind!=V.Kind){return false;}}
    auto* E=M->Economy.Get();LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=Route==5?TEXT("KeysActivation"):Route==6?TEXT("HeartbreakerActivation"):TEXT("ShrineRecovery");
    if(Route==6){if(P->Items.Num()!=1){return false;}const auto& V=P->Items[0];const auto* L=E->Ledger.Item(V.Id);LDEconomy::FItemDelta D;D.Id=V.Id;D.Kind=V.Kind;D.Op=LDEconomy::EItemOp::Consume;D.From=L->Location;D.Expected=L->Revision;T.Items.Add(D);}
    if(!E->Ledger.Commit(T,Why)){return false;}
    if(Route>=7){auto* S=M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()));S->Hp=LDDevelopment::MaxHP(O->Build);S->Tactical.MP=LDTactical::MaxMP(O->Build);O->Hp=S->Hp;O->Mp=S->Tactical.MP;O->ShrineReadyAt=At+90;Reserve(PC,Route,false);M->SyncOwner(PC);return true;}
    FLease L;L.Id=T.Id;L.Route=Route;L.Life=O->Life;L.Open=At+30;L.Close=At+90;
    const auto Items=P->Items;Reserve(PC,Route,false);
    if(Route==5){O->KeysReadyAt=LDTactical::Milliseconds(At)+90000;for(const auto& V:Items){auto* A=M->Inventory->Owned(PC,V.Id);A->ReadyAt=FMath::Max(A->ReadyAt,O->KeysReadyAt);++A->Revision;auto* I=const_cast<FLDInventoryItem*>(O->Inventory.Find(V.Id));I->ReadyAt=A->ReadyAt;I->Revision=A->Revision;L.Keys.Add(V.Id);}++O->Inventory.Revision;}
    else{auto* A=M->Inventory->Owned(PC,Items[0].Id);LDInventory::Remove(O->Inventory,A->Identity);A->Holder.Reset();A->State=2;A->Destroy();}
    UE_LOG(LogTemp,Display,TEXT("LD_B76_LEASE owner=%s life=%u route=%u receipt=%s committed=%.6f open=%.6f close=%.6f keys=%d"),*M->ContenderId(PC).ToString(),L.Life,Route,*L.Id.ToString(),At,L.Open,L.Close,L.Keys.Num());Leases.Add(PC,L);auto* G=M->GetGameState<ALDGameState>();FLDBreachNotice Notice;Notice.Route=Route;Notice.WarningAt=At;Notice.OpenAt=L.Open;Notice.CloseAt=L.Close;G->Breaches.Add(Notice);G->ForceNetUpdate();M->Inventory->Changed(PC);M->SyncOwner(PC);return E->Audit(Why);
}

ALDItem* ALDWorldDirector::KeyTarget(ALDPlayerController* PC) const{return KeyCandidates.FindRef(PC).Get();}
bool ALDWorldDirector::SelectKey(ALDPlayerController* PC,ALDItem* Item,FString& Why)
{if(!Item||Item->State!=0||Item->bReserved||Item->Kind<ELDItemKind::AncientKeyA||Item->Kind>ELDItemKind::AncientKeyC){Why=TEXT("Dropped key unavailable");return false;}KeyCandidates.Add(PC,Item);if(!CanActivate(PC,9,GetWorld()->GetTimeSeconds()+3,Why)){KeyCandidates.Remove(PC);return false;}return true;}
