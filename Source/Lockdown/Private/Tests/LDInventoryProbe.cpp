#include "Tests/LDInventoryProbe.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDRegionDefinition.h"
#include "UI/LDInventoryPanel.h"
#include "UI/LDControlSettings.h"
#include "UI/LDHUD.h"
#include "UI/LDMinimap.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/CoreNet.h"

ALDInventoryProbe::ALDInventoryProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDInventoryProbe::BeginPlay(){Super::BeginPlay();Started=PhaseAt=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}}
void ALDInventoryProbe::Check(bool Pass,const FString& Text){Checks.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Text);Failures+=!Pass;UE_LOG(LogTemp,Display,TEXT("LD_INVENTORY_PROBE %s"),*Checks.Last());if(!Pass&&GetNetMode()==NM_Client){const auto* PC=CastChecked<ALDPlayerController>(GetOwner());UE_LOG(LogTemp,Display,TEXT("LD_INVENTORY_DIAGNOSTIC own_count=%d armed=%d reason=%s"),PC->OwnerState.Inventory.Items.Num(),PC->IsTargeting(),*PC->IntentFeedback);}}
void ALDInventoryProbe::Enter(int32 Next){Phase=Next;Step=0;PhaseAt=FPlatformTime::Seconds();A->ClientProbePhase(uint8(Next));B->ClientProbePhase(uint8(Next));}
void ALDInventoryProbe::Key(FKey Input,bool Down){CastChecked<ALDPlayerController>(GetOwner())->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),Input,Down?IE_Pressed:IE_Released,Down?1.f:0.f,false,FPlatformTime::Cycles64()));}
void ALDInventoryProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(FPlatformTime::Seconds()-Started>150){Check(false,TEXT("Inventory probe timed out"));Finish();return;}if(GetNetMode()==NM_Client){ClientTick();}else{ServerTick();}
#endif
}
void ALDInventoryProbe::ServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=GetWorld()->GetGameState<ALDGameState>();auto* Inv=M->Inventory.Get();const double E=FPlatformTime::Seconds()-PhaseAt;
    if(Phase==14){if(E>3){Finish();}return;}
    if(!Phase){for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){if(auto* PC=Cast<ALDPlayerController>(It->Get());PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}if(A.IsValid()&&B.IsValid()){Check(G->bUIControlProfile&&G->LobbyCount==2,TEXT("Independent inventory clients retain the private V05 lobby"));Enter(1);}return;}
    if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("Inventory client disconnected"));Finish();return;}
    if(Phase==1&&G->bMatchStarted&&E>1.5)
    {
        int32 N=0;for(auto PC:{A,B}){auto* O=M->StateFor(PC.Get());const FVector P=LDRegion::Site(GetWorld(),1)+FVector(N++?90:-90,-30,92);auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);Body->SetFacing(0);Body->GetCharacterMovement()->StopMovementImmediately();O->ProtectedUntil=0;O->Gold=1000;M->Combat->StateFor(Body)->ProtectionUntil=0;
            Check(O->Inventory.bInitialized&&O->Inventory.Capacity==24&&O->Inventory.Items.Num()==1&&O->WardCharges==1&&O->Inventory.Quick.Num()==4,TEXT("Clean V05 match migrates exactly one starting ward into 24-slot bag"));InitialWards.Add(O->Inventory.Items[0].Id);Inv->Initialize(PC.Get());Check(O->Inventory.Items.Num()==1,TEXT("Repeated migration cannot create another ward"));
            auto* Item=GetWorld()->SpawnActor<ALDBook>(P+FVector(0,30,-80),FRotator::ZeroRotator);Item->State=0;Item->Skill=PC==A?ELDSkill::Spark:ELDSkill::Step;InitialBooks.Add(Item->Identity);M->SyncOwner(PC.Get());}Enter(2);
    }
    else if(Phase==2&&E>5){int32 N=0;for(auto PC:{A,B}){const auto& I=M->StateFor(PC.Get())->Inventory;Check(I.Items.Num()==2&&I.Find(InitialBooks[N])&&I.Quick[0]==InitialWards[N]&&I.Quick[1]==InitialBooks[N],TEXT("Observed pickup and independent book/charge bindings commit on authority"));++N;}Enter(3);}
    else if(Phase==3&&E>5){int32 N=0;for(auto PC:{A,B}){const auto& I=M->StateFor(PC.Get())->Inventory;Check(I.Quick[2]==InitialBooks[N]&&!I.Quick[1].IsValid()&&I.Quick[0]==InitialWards[N]&&I.Find(InitialBooks[N])->Slot==23&&LDInventory::Valid(I),TEXT("Sort and move preserve IDs; rebinding clears the old reference"));++N;}Enter(4);}
    else if(Phase==4&&E>2){int32 N=0;for(auto PC:{A,B}){const auto* O=M->StateFor(PC.Get());Check(Inv->Owned(PC.Get(),InitialBooks[N])&&LDDevelopment::Rank(O->Build,PC==A?ELDSkill::Spark:ELDSkill::Step)==0&&!O->DevelopmentKind,TEXT("Early quick-key release cancels real reading without consuming"));++N;}Enter(5);}
    else if(Phase==5&&E>5)
    {int32 N=0;for(auto PC:{A,B}){auto* O=M->StateFor(PC.Get());const auto Skill=PC==A?ELDSkill::Spark:ELDSkill::Step;Check(!Inv->Owned(PC.Get(),InitialBooks[N])&&LDDevelopment::Rank(O->Build,Skill)==1&&!O->Build.Slots.Contains(Skill)&&!O->Inventory.Quick[2].IsValid(),TEXT("Actual three-second reading consumes once, teaches R1, clears reference and never equips"));auto* Item=GetWorld()->SpawnActor<ALDBook>();Item->Skill=Skill;Item->State=0;Check(Inv->Adopt(PC.Get(),Item),TEXT("Duplicate-knowledge case materializes one labelled fixture book"));DuplicateBooks.Add(Item->Identity);++N;}Enter(6);}
    else if(Phase==6&&E>4.5){int32 N=0;for(auto PC:{A,B}){auto* O=M->StateFor(PC.Get());Check(Inv->Owned(PC.Get(),DuplicateBooks[N])&&!O->DevelopmentKind&&O->Inventory.Quick[3]==DuplicateBooks[N],TEXT("Known book remains owned and bound without a reading channel"));Check(O->Gold==960&&O->Books.Num()==2,TEXT("Purchase and replay debit forty gold once and create one book"));++N;}Enter(7);}
    else if(Phase==7&&E>4.5){int32 N=0;for(auto PC:{A,B}){Check(Inv->Owned(PC.Get(),InitialWards[N++])&&M->StateFor(PC.Get())->WardCharges==1,TEXT("Ward cancel and invalid range preserve the finite charge"));}Inv->FailNextWardForProbe();Enter(8);}
    else if(Phase==8&&E>3){Check(Inv->Owned(A.Get(),InitialWards[0])&&M->StateFor(A.Get())->WardCharges==1,TEXT("Injected source-creation failure leaves charge and reference intact"));Enter(9);}
    else if(Phase==9&&E>4)
    {int32 N=0;for(auto PC:{A,B}){int32 Sources=0;for(TActorIterator<ALDWard> It(GetWorld());It;++It){Sources+=It->Controller==PC;}const auto* O=M->StateFor(PC.Get());Check(Sources==1&&!Inv->Owned(PC.Get(),InitialWards[N++])&&!O->WardCharges&&!O->Inventory.Quick[0].IsValid(),TEXT("Successful placement plus replay creates one source and consumes exactly one charge"));}FullBagCases();Enter(10);}
    else if(Phase==10&&E>8)
    {int32 N=0;for(auto PC:{A,B}){auto* O=M->StateFor(PC.Get());Check(O->Inventory.Items.Num()==24&&Inv->Owned(PC.Get(),DuplicateBooks[N])&&!O->Inventory.Quick[3].IsValid(),TEXT("Confirmed drop and explicit versioned pickup preserve one item and clear old shortcut"));
        const auto Sacrifice=*O->Inventory.Items.FindByPredicate([&](const auto& Item){return Item.Kind==ELDItemKind::Book&&Item.Id!=DuplicateBooks[N];});Inv->Remove(PC.Get(),Inv->Owned(PC.Get(),Sacrifice.Id),true);auto* Charge=GetWorld()->SpawnActor<ALDItem>();Check(Inv->Adopt(PC.Get(),Charge),TEXT("Death charge is an explicit isolated fixture, not a respawn grant"));DeathCharges.Add(Charge->Identity);FString Why;LDInventory::BindQuick(O->Inventory,Charge->Identity,0,Why);LDInventory::BindQuick(O->Inventory,DuplicateBooks[N],1,Why);Inv->Changed(PC.Get());M->EndContenderLife(PC.Get(),GetWorld()->GetTimeSeconds());
        Check(O->Inventory.Items.IsEmpty()&&O->Inventory.Quick.FilterByPredicate([](FGuid Id){return Id.IsValid();}).IsEmpty()&&!Inv->Owned(PC.Get(),Charge->Identity),TEXT("Death drops books/Seal, destroys unused charge and clears every lost reference atomically"));++N;}Enter(11);}
    else if(Phase==11&&E>3){Enter(12);}
    else if(Phase==12&&M->StateFor(A.Get())->Life==2&&M->StateFor(A.Get())->bActive&&M->StateFor(B.Get())->Life==2&&M->StateFor(B.Get())->bActive)
    {for(auto PC:{A,B}){const auto* O=M->StateFor(PC.Get());Check(O->Inventory.Items.IsEmpty()&&!O->WardCharges&&LDDevelopment::Rank(O->Build,PC==A?ELDSkill::Spark:ELDSkill::Step)==1,TEXT("Real return retains learning but grants no carried supplies"));}
        for(const auto& Id:DuplicateBooks){int32 Count=0;for(TActorIterator<ALDItem> It(GetWorld());It;++It){Count+=It->Identity==Id&&It->State==0&&!It->Holder.IsValid();}Check(Count==1,TEXT("Dropped identity survives independently of the old body"));}
        int32 Dropped=0;for(TActorIterator<ALDItem> It(GetWorld());It;++It){Dropped+=It->State==0;}for(int32 I=Dropped;I<96;++I){auto* Item=GetWorld()->SpawnActor<ALDBook>(LDRegion::Site(GetWorld(),1)+FVector((I%12)*8-44,(I/12)*8-28,12),FRotator::ZeroRotator);Item->State=0;Item->Skill=ELDSkill::Step;}
        for(auto PC:{A,B}){PC->GetPawn()->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(PC==A?-90:90,-30,92),false,nullptr,ETeleportType::TeleportPhysics);}G->MatchDeadline=float(GetWorld()->GetTimeSeconds()+1);Enter(13);}
    else if(Phase==13&&E>3){for(auto PC:{A,B}){int32 Items=0;for(const auto& V:M->Vision->AuthorityViewFor(PC.Get())->Entities){Items+=V.Kind==ELDObservedKind::Book||V.Kind==ELDObservedKind::Seal||V.Kind==ELDObservedKind::WardCharge;}Check(Items>=96,TEXT("Authorized view can contain four full bags of dropped physical items without truncation"));}Check(G->EndReason==2,TEXT("Actual deadline terminates inventory gameplay with No acquisition"));Enter(14);}
    else if(Phase==14&&E>3){Finish();}
}
void ALDInventoryProbe::FullBagCases()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* Inv=M->Inventory.Get();int32 N=0;uint32 FixtureRequest=500000;
    for(auto PC:{A,B})
    {
        auto* O=M->StateFor(PC.Get());while(O->Inventory.Items.Num()<(PC==A?23:24)){auto* Item=GetWorld()->SpawnActor<ALDBook>();Item->State=0;Item->Skill=ELDSkill::Spark;check(Inv->Adopt(PC.Get(),Item));}if(PC==A){auto* Seal=GetWorld()->SpawnActor<ALDSeal>();check(Inv->Adopt(PC.Get(),Seal));}
        Check(O->Inventory.Items.Num()==24&&LDInventory::Valid(O->Inventory)&&(PC==A?O->SealId.IsValid():O->Books.Num()==24),TEXT("General capacity holds 24 books or 23 books plus one Seal"));const auto Gold=O->Gold;const auto InventoryBefore=O->Inventory.Revision;
        auto Request=[&](uint8 Command,FGuid Id,uint32 ItemRevision,uint8 Slot,uint32 ContainerRevision){Inv->Request({PC,O->Match,O->Life,++FixtureRequest,ContainerRevision,Command,Id,ItemRevision,Slot,FVector::ZeroVector},GetWorld()->GetTimeSeconds());};
        Request(LDInventory::Buy,FGuid(),0,uint8(ELDSkill::Patch),O->Inventory.Revision);Check(O->Gold==Gold&&O->Inventory.Revision==InventoryBefore,TEXT("Full-bag purchase spends no gold and creates no item"));
        PC->ServerDevelopmentRequest_Implementation(O->Match,O->Life,++FixtureRequest,LDDevelopmentTransaction::Read,ELDSkill::None,DuplicateBooks[N],FLDBuildPlan());Check(!O->DevelopmentKind&&Inv->Owned(PC.Get(),DuplicateBooks[N]),TEXT("Retired direct book RPC cannot bypass inventory versions or item routing"));
        PC->ServerPlaceWard_Implementation(O->Match,O->Life,++FixtureRequest,PC->GetPawn()->GetActorLocation());Check(O->Inventory.Revision==InventoryBefore,TEXT("Retired direct ward RPC cannot create or consume a V05 item"));
        Request(LDInventory::Sort,FGuid(),0,0,O->Inventory.Revision-1);Check(O->Inventory.Revision==InventoryBefore,TEXT("Stale inventory revision rejects without partial reorder"));
        const auto Target=O->Inventory.Items[0];Request(LDInventory::Move,Target.Id,Target.Revision-1,2,O->Inventory.Revision);Check(O->Inventory.Revision==InventoryBefore,TEXT("Stale item revision rejects without mutation"));
        Request(LDInventory::Move,Target.Id,Target.Revision,255,O->Inventory.Revision);Check(O->Inventory.Revision==InventoryBefore,TEXT("Out-of-range organization rejects"));
        const auto* Foreign=M->StateFor(PC==A?B.Get():A.Get());const auto ForeignItem=Foreign->Inventory.Items[0];Request(LDInventory::Bind,ForeignItem.Id,ForeignItem.Revision,0,O->Inventory.Revision);Check(O->Inventory.Revision==InventoryBefore,TEXT("Forged foreign item identity cannot bind or expose another bag"));
        auto* Dropped=GetWorld()->SpawnActor<ALDBook>(PC->GetPawn()->GetActorLocation()+FVector(0,30,-80),FRotator::ZeroRotator);Dropped->State=0;Dropped->Skill=ELDSkill::Patch;M->Vision->Sample(true);FGuid Observed;for(const auto& V:M->Vision->AuthorityViewFor(PC.Get())->Entities){if(M->Vision->ResolveObserved(PC.Get(),V.Id)==Dropped){Observed=V.Id;}}
        Inv->Request({PC,O->Match,O->Life,++FixtureRequest,O->Inventory.Revision,LDInventory::Pickup,Observed,Dropped->Revision,0,FVector::ZeroVector},GetWorld()->GetTimeSeconds());Check(Observed.IsValid()&&Dropped->State==0&&!Dropped->Holder.IsValid()&&O->Inventory.Items.Num()==24,TEXT("Full-bag observed pickup keeps the same dropped physical item"));
        if(PC==A){const auto Seal=*O->Inventory.Find(O->SealId);Request(LDInventory::Bind,Seal.Id,Seal.Revision,2,O->Inventory.Revision);Check(O->Inventory.Revision==InventoryBefore&&!O->Inventory.Quick.Contains(Seal.Id),TEXT("Seal has capacity but cannot become a quick-use reference"));const auto CapacityBefore=O->Inventory.Capacity;++O->Inventory.Capacity;Check(!Inv->HasSpace(PC.Get(),ELDItemKind::Seal)&&Inv->HasSpace(PC.Get(),ELDItemKind::Book),TEXT("Projected extra capacity cannot bypass one-Seal limit"));O->Inventory.Capacity=CapacityBefore;}
        auto* Locked=Inv->Owned(PC.Get(),O->Inventory.Items.Last().Id);Locked->bReserved=true;for(auto& Item:O->Inventory.Items){if(Item.Id==Locked->Identity){Item.bReserved=true;}}Request(LDInventory::Move,Locked->Identity,Locked->Revision,0,O->Inventory.Revision);Check(O->Inventory.Revision==InventoryBefore,TEXT("Reserved item cannot be moved"));Locked->bReserved=false;for(auto& Item:O->Inventory.Items){Item.bReserved=false;}Inv->Changed(PC.Get());++N;
    }
}
void ALDInventoryProbe::ClientTick()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());if(FPlatformTime::Seconds()-LastPing>.25){LastPing=FPlatformTime::Seconds();PC->ServerProbePing(LastPing);}if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;Step=0;PhaseAt=FPlatformTime::Seconds();}
    const double E=FPlatformTime::Seconds()-PhaseAt;const auto& O=PC->OwnerState;const auto& I=O.Inventory;
    if(Phase==1){if(!bChoice){PC->Controls->ResetBindings();PC->RebuildV05Mappings();PC->ChooseLobbyClass(Role==TEXT("A")?1:2);bChoice=true;}if(!bReady&&O.Build.ClassId==(Role==TEXT("A")?1:2)&&E>.4){PC->ToggleLobbyReady();bReady=true;}if(O.bLobbyHost&&O.bReady&&E>1){PC->StartLobby();}return;}
    if(Phase==2)
    {
        if(!Step&&E>.7){const auto* R=PC->LocalView();FGuid Handle;const FVector Expected=LDRegion::Site(GetWorld(),1)+FVector(Role==TEXT("A")?-90:90,0,12);for(const auto& V:R->LastView().Entities){if(V.Kind==ELDObservedKind::Book&&FVector::Dist2D(V.Position,Expected)<15){Handle=V.Id;}}Check(Handle.IsValid(),TEXT("Pickup starts from an authorized observation, not a canonical item"));PC->Inspect(Handle);Key(EKeys::F,true);Step=1;}
        if(Step==1&&E>1){Key(EKeys::F,false);Step=2;}if(Step==2&&E>1.8){for(const auto& Item:I.Items){if(Item.Kind==ELDItemKind::Book){Book=Item.Id;}else if(Item.Kind==ELDItemKind::WardCharge){Ward=Item.Id;}}Check(Book.IsValid()&&Ward.IsValid(),TEXT("Own committed bag receives distinct physical book and charge identities"));PC->InventoryCommand(LDInventory::Bind,Book,1);Step=3;}
        if(Step==3&&E>2.8){PC->InventoryCommand(LDInventory::Bind,Ward,0);InitialBuild=O.Build.Revision;Step=4;}
        if(Step==4&&E>4){int32 Canonical=0;for(TActorIterator<ALDItem> It(GetWorld());It;++It){++Canonical;}Check(Canonical==0&&I.Items.Num()==2&&I.Quick[0]==Ward&&I.Quick[1]==Book,TEXT("Restricted client receives only own bag snapshot and no canonical carried/dropped actors"));Step=5;}
    }
    if(Phase==3){if(!Step&&E>.5){PC->InventoryCommand(LDInventory::Bind,Book,2);Step=1;}if(Step==1&&E>1.5){PC->InventoryCommand(LDInventory::Sort);Step=2;}if(Step==2&&E>2.5){PC->InventoryCommand(LDInventory::Move,Book,23);Step=3;}if(Step==3&&E>4){Check(I.Quick[2]==Book&&!I.Quick[1].IsValid()&&I.Find(Book)&&I.Find(Book)->Slot==23&&O.Build.Revision==InitialBuild,TEXT("Organization changes neither quick identity nor skill build revision"));Step=4;}}
    if(Phase==4){if(!Step&&E>.4){Key(EKeys::Three,true);Step=1;}if(Step==1&&E>.9){Key(EKeys::Three,false);Step=2;}if(Step==2&&E>1.5){Check(I.Find(Book)&&!O.DevelopmentKind,TEXT("Quick book release keeps the same unread book"));Step=3;}}
    if(Phase==5)
    {
        if(!Step&&E>.4){BeforeInventory=I.Revision;BeforeItem=I.Find(Book)?I.Find(Book)->Revision:0;if(Role==TEXT("B")){PC->ToggleInventoryPanel();PC->InventoryUI->Select(Book);PC->InventoryUI->Use();}else{Key(EKeys::Three,true);}Step=1;}
        if(Step==1&&E>.8){if(Role==TEXT("B")){Key(EKeys::F,true);}Step=2;}
        if(Step==2&&E>1.5){Check(O.DevelopmentKind==2&&PC->HeldInputLabel()==PC->BindingLabel(Role==TEXT("A")?TEXT("Item3"):TEXT("Context"))&&PC->IntentFeedback.Contains(TEXT("holding ")+PC->HeldInputLabel()),TEXT("Live reading progress and accepted receipt name the initiating quick or context mapping"));Replay=PC->DevelopmentHoldToken;PC->InventoryCommand(LDInventory::Sort);Step=3;}
        if(Step==3&&E>4.2){Key(Role==TEXT("B")?EKeys::F:EKeys::Three,false);Check(!I.Find(Book)&&!I.Quick[2].IsValid()&&LDDevelopment::Rank(O.Build,Role==TEXT("A")?ELDSkill::Spark:ELDSkill::Step)==1,TEXT("Quick or inventory-F reading shares the real timer; sorting preserves the hold and completed learning clears references"));PC->ServerInventoryRequest(O.Match,O.Life,Replay,BeforeInventory,LDInventory::Read,Book,BeforeItem,0,FVector::ZeroVector);Step=4;}
        if(Step==4&&E>4.7){Check(!I.Find(Book)&&PC->IntentFeedback.Contains(TEXT("Book consumed once")),TEXT("Read request replay returns its committed receipt without learning or consuming twice"));Step=5;}
    }
    if(Phase==6){if(!Step&&E>.6){for(const auto& Item:I.Items){if(Item.Kind==ELDItemKind::Book){Book=Item.Id;}}PC->InventoryCommand(LDInventory::Bind,Book,3);Step=1;}if(Step==1&&E>1.6){Key(EKeys::Four,true);Step=2;}if(Step==2&&E>2.6){Key(EKeys::Four,false);Check(I.Find(Book)&&!O.DevelopmentKind&&PC->IntentFeedback.Contains(TEXT("Already learned")),TEXT("Known book shortcut rejects learning without consuming or casting"));Step=3;}if(Step==3&&E>3){BeforeInventory=I.Revision;PC->BuyBook(ELDSkill::Patch);Replay=PC->NextRequest;Step=4;}if(Step==4&&E>3.5){PC->ServerInventoryRequest(O.Match,O.Life,Replay,BeforeInventory,LDInventory::Buy,FGuid(),0,uint8(ELDSkill::Patch),FVector::ZeroVector);Step=5;}if(Step==5&&E>4){Check(O.Gold==960&&I.Items.FilterByPredicate([](const auto& Item){return Item.Kind==ELDItemKind::Book&&Item.Skill==ELDSkill::Patch;}).Num()==1,TEXT("Committed purchase replay preserves one book and a single forty-gold debit"));Step=6;}}
    if(Phase==7){if(!Step&&E>.5){Key(EKeys::One,true);Step=1;}if(Step==1&&E>.8){Key(EKeys::One,false);Check(PC->ArmedItem()==Ward,TEXT("Ward shortcut arms its exact owned instance without cost"));const auto Before=PC->Navigation->LocalIdentity();PC->HUDUI->Minimap->Gesture(FVector2D(.3,.3),true);Check(!PC->IsTargeting()&&PC->Navigation->LocalIdentity()==Before&&I.Find(Ward),TEXT("First minimap RMB cancels ward preview and consumes no charge or move"));Step=2;}if(Step==2&&E>1.5){Key(EKeys::One,true);Step=3;}if(Step==3&&E>1.8){Key(EKeys::One,false);PC->ConfirmArmed(PC->GetPawn()->GetActorLocation()+FVector(400,0,0),FGuid());Step=4;}if(Step==4&&E>3){Check(I.Find(Ward)&&I.Quick[0]==Ward&&O.WardCharges==1,TEXT("Rejected ward geometry preserves identity and shortcut"));Step=5;}}
    if(Phase==8){if(!Step&&E>.5){if(Role==TEXT("A")){PC->ToggleInventoryPanel();PC->InventoryUI->Select(Ward);PC->InventoryUI->Use();}Step=1;}if(Step==1&&E>1){if(Role==TEXT("A")){PC->ConfirmArmed(PC->GetPawn()->GetActorLocation()+FVector(30,0,0),FGuid());}Step=2;}}
    if(Phase==9){if(!Step&&E>.5){BeforeInventory=I.Revision;BeforeItem=I.Find(Ward)?I.Find(Ward)->Revision:0;PC->SetQuickItemHeld(0,true,2);PC->ConfirmArmed(PC->GetPawn()->GetActorLocation()+FVector(30,0,0),FGuid());Replay=PC->NextRequest;Step=1;}if(Step==1&&E>1.5){PC->ServerInventoryRequest(O.Match,O.Life,Replay,BeforeInventory,LDInventory::Place,Ward,BeforeItem,0,PC->GetPawn()->GetActorLocation()+FVector(30,0,0));Step=2;}if(Step==2&&E>2.5){Check(!I.Find(Ward)&&!I.Quick[0].IsValid()&&!O.WardCharges,TEXT("Placement acknowledgement removes one charge and its reference even after replay"));Step=3;}}
    if(Phase==10)
    {
        if(!Step&&E>.7){PC->ToggleInventoryPanel();PC->InventoryUI->Select(Book);PC->InventoryUI->Refresh();Check(PC->InventoryUI->IsOpen()&&!PC->CanWorldCommand()&&I.Items.Num()==24&&PC->InventoryUI->DetailText().Contains(TEXT("Book:")),TEXT("Native 24-slot bag shows own item details and suppresses world commands"));Step=1;}
        if(Step==1&&E>1.5){auto* Panel=PC->InventoryUI.Get();const auto Revision=I.Revision;Panel->bPointerDown=Panel->bDragging=true;Panel->Dragged=Book;const FPointerEvent Release(0,FVector2D(-100,-100),FVector2D(-90,-90),TSet<FKey>(),EKeys::LeftMouseButton,0,FModifierKeysState());const auto Reply=Panel->NativeOnMouseButtonUp(FGeometry(),Release);Check(Reply.IsEventHandled()&&!Panel->IsDragging()&&I.Revision==Revision&&I.Find(Book)&&!PC->IsTargeting(),TEXT("Native outside-release handler consumes drag cancellation without moving, dropping or casting"));Panel->AskDrop();Check(I.Find(Book)&&I.Items.Num()==24,TEXT("Opening drop confirmation does not mutate ownership"));Step=2;}if(Step==2&&E>2){PC->InventoryUI->ConfirmDrop();Step=3;}
        if(Step==3&&E>3.3){Check(!I.Find(Book)&&!I.Quick[3].IsValid()&&I.Items.Num()==23,TEXT("Explicit modal drop clears the exact shortcut and frees one bag slot"));PC->InventoryUI->Close();Step=4;}
        if(Step==4&&E>4.2){FGuid Handle;const auto* R=PC->LocalView();const FVector Expected=PC->GetPawn()->GetActorLocation()+FVector(50,0,-80);for(const auto& V:R->LastView().Entities){if(V.Kind==ELDObservedKind::Book&&FVector::Dist2D(V.Position,Expected)<15){Handle=V.Id;}}PC->BeginPickup(Handle);Step=5;}
        if(Step==5&&E>6){Check(I.Find(Book)&&I.Items.Num()==24&&!I.Quick[3].IsValid(),TEXT("Fresh observed pickup restores the same identity without restoring an old shortcut"));Step=6;}
    }
    if(Phase==11&&!Step&&E>.7){PC->ToggleInventoryPanel();PC->InventoryCommand(LDInventory::Sort);Check(!O.bActive&&!PC->CanWorldCommand()&&I.Items.IsEmpty()&&I.Quick.FilterByPredicate([](FGuid Id){return Id.IsValid();}).IsEmpty(),TEXT("Death leaves an inspectable empty bag with no stale item reference"));Step=1;}
    if(Phase==12&&!Step&&E>.5){Check(PC->InventoryUI&&PC->InventoryUI->IsOpen()&&!PC->CanWorldCommand(),TEXT("Waiting bag remains a major panel and permits organization only"));PC->InventoryUI->Close();Step=1;}
    if(Phase==13&&!Step&&E>2){Check(O.Life==2&&I.Items.IsEmpty()&&!O.WardCharges&&!PC->CanWorldCommand()&&PC->HUDUI->TerminalText().Contains(TEXT("NO ACQUISITION")),TEXT("New life has no free supplies; terminal receipt closes inventory gameplay"));auto View=PC->LocalView()->LastView();int32 Items=0;for(const auto& V:View.Entities){Items+=V.Kind==ELDObservedKind::Book||V.Kind==ELDObservedKind::Seal||V.Kind==ELDObservedKind::WardCharge;}FNetBitWriter Writer(nullptr,65536);bool Good=true;View.NetSerialize(Writer,nullptr,Good);MaxViewBytes=int32(Writer.GetNumBytes());Check(Good&&Items>=96&&MaxViewBytes<8192,TEXT("Restricted client receives the labelled 96-drop batch through the same eight-field mask within 8 KiB"));Step=1;}
    if(Phase==14&&E>1){Check(true,TEXT("All B54 inventory phases completed"));Finish();}
}
void ALDInventoryProbe::Finish()
{
    SetActorTickEnabled(false);auto Report=MakeShared<FJsonObject>();Report->SetStringField(TEXT("profile"),TEXT("B54 engineering: native keys plus explicit widget/direct authority calls; fixture positions/protection, 1000 gold, dropped/known/full-bag books and Seal, one ward creation failure, reserved-item setup, death-only extra charge and terminal deadline; actual read/hold/return timers and ownership commits; not earned vendor progression or human evidence"));
    Report->SetStringField(TEXT("viewCapacityFixture"),TEXT("After actual death/return, top up dropped world items to 96 and relocate both bodies for a four-full-bag view test; no supplies granted to a returned bag"));Report->SetNumberField(TEXT("maxViewPayloadBytes"),MaxViewBytes);
    if(GetNetMode()==NM_Client){auto Echo=CastChecked<ALDPlayerController>(GetOwner())->ProbeRtts;Echo.Sort();Check(Echo.Num()>=8,TEXT("Independent inventory client measured transport echo"));Report->SetNumberField(TEXT("medianEchoRttMs"),Echo.IsEmpty()?0:Echo[Echo.Num()/2]);}Report->SetNumberField(TEXT("failures"),Failures);TArray<TSharedPtr<FJsonValue>> Values;for(const auto& C:Checks){Values.Add(MakeShared<FJsonValueString>(C));}Report->SetArrayField(TEXT("checks"),Values);FString Directory,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Output));if(!FFileHelper::SaveStringToFile(Output,*FPaths::Combine(Directory,Role+TEXT(".json")))){++Failures;}FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
