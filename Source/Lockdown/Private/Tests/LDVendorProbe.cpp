#include "Tests/LDVendorProbe.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Inventory/LDVendorDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Navigation/LDNavigationComponent.h"
#include "UI/LDInventoryPanel.h"
#include "UI/LDControlSettings.h"
#include "Vision/LDWorldViewRelay.h"
#include "Region/LDRegionDefinition.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
ALDVendorProbe::ALDVendorProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDVendorProbe::BeginPlay(){Super::BeginPlay();Started=PhaseAt=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}}
void ALDVendorProbe::Check(bool Pass,const FString& Text){Checks.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Text);Failures+=!Pass;UE_LOG(LogTemp,Display,TEXT("LD_VENDOR_PROBE %s"),*Checks.Last());}
void ALDVendorProbe::Enter(int32 Next){Phase=Next;Step=0;PhaseAt=FPlatformTime::Seconds();A->ClientProbePhase(uint8(Next));B->ClientProbePhase(uint8(Next));}
void ALDVendorProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(FPlatformTime::Seconds()-Started>420){Check(false,TEXT("Vendor probe timed out"));Finish();return;}if(GetNetMode()==NM_Client){ClientTick();}else{ServerTick();}
#endif
}
void ALDVendorProbe::ServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=GetWorld()->GetGameState<ALDGameState>();auto* V=M->Vendor.Get();const double E=FPlatformTime::Seconds()-PhaseAt;
    if(Phase==10){if(E>3){Finish();}return;}
    if(!Phase){for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){if(auto* PC=Cast<ALDPlayerController>(It->Get());PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}if(A.IsValid()&&B.IsValid()){Check(G->LobbyCount==2&&G->bUIControlProfile,TEXT("Two independent vendor clients retain the ordinary private V05 lobby"));Enter(1);}return;}
    if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("Vendor client disconnected"));Finish();return;}
    if(Phase==1&&G->bMatchStarted&&E>1){Check(M->StateFor(A.Get())->Gold==0&&M->StateFor(A.Get())->PaidJobs==0,TEXT("Earned route begins with zero gold and no paid jobs or seeded progression"));Enter(2);}
    else if(Phase==2)
    {
        const auto* O=M->StateFor(A.Get());if(O->PaidJobs==2&&O->Gold==10&&LDDevelopment::Rank(O->Build,ELDSkill::Spark)==1&&O->Books.IsEmpty()&&!O->DevelopmentKind&&E>15){if(!Step){Step=1;ActionAt=FPlatformTime::Seconds();}if(FPlatformTime::Seconds()-ActionAt<1){return;}Check(O->Inventory.Items.Num()==1&&O->WardCharges==1,TEXT("Two normal courier rewards fund one forty-gold purchase and actual read; original ward remains"));Check(!O->Build.Slots.Contains(ELDSkill::Spark)&&O->PaidJobs==2,TEXT("Earned purchase/read does not equip, cast or grant another paid job"));Enter(3);}
    }
    else if(Phase==3&&E>7)
    {
        for(auto PC:{A,B}){const auto* O=M->StateFor(PC.Get());Check(V->Clients.FindChecked(PC).Session.IsValid()&&O->Inventory.Items.Num()==1&&O->Gold==(PC==A?10:0),TEXT("Independent quotes and insufficient-gold rejection conserve each own bag and wallet"));}
        for(auto PC:{A,B}){auto* O=M->StateFor(PC.Get());O->Gold=80;M->SyncOwner(PC.Get());}++V->CatalogRevision;V->BookPrice=41;Enter(4);
    }
    else if(Phase==4&&E>5)
    {
        for(auto PC:{A,B}){const auto* O=M->StateFor(PC.Get());Check(O->Gold==39&&O->Books.Num()==1&&LDInventory::Valid(O->Inventory),TEXT("Stale quote rejects; a fresh explicit forty-one-gold purchase and replay commit exactly one book"));}
        V->BookPrice=40;++V->CatalogRevision;for(auto PC:{A,B}){auto* O=M->StateFor(PC.Get());O->Gold=80;while(O->Inventory.Items.Num()<24){auto* Item=GetWorld()->SpawnActor<ALDBook>();Item->Skill=ELDSkill::Patch;Item->State=0;check(M->Inventory->Adopt(PC.Get(),Item));}M->SyncOwner(PC.Get());}Enter(5);
    }
    else if(Phase==5&&E>4)
    {
        uint32 Fixture=100000;for(auto PC:{A,B}){auto* O=M->StateFor(PC.Get());const auto& C=V->Clients.FindChecked(PC);Check(O->Gold==80&&O->Inventory.Items.Num()==24,TEXT("Full bag cannot spend quoted gold or create another physical book"));V->Request({PC,O->Match,O->Life,++Fixture,FGuid::NewGuid(),V->CatalogRevision,40,O->Inventory.Revision,ELDSkill::Spark});Check(O->Gold==80&&O->Inventory.Items.Num()==24,TEXT("A forged vendor session cannot buy or disclose another owner's state"));V->Request({PC,O->Match,O->Life,++Fixture,C.Session,V->CatalogRevision,40,O->Inventory.Revision-1,ELDSkill::Spark});Check(O->Gold==80&&O->Inventory.Items.Num()==24,TEXT("Stale bag revision rejects before creation or debit"));const auto Item=*O->Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::Book;});M->Inventory->Remove(PC.Get(),M->Inventory->Owned(PC.Get(),Item.Id),true);}
        V->FailNextCreationForProbe();Enter(6);
    }
    else if(Phase==6)
    {
        if(!Step&&E>3){for(auto PC:{A,B}){const auto* O=M->StateFor(PC.Get());PC->ServerInventoryRequest_Implementation(O->Match,O->Life,200000,O->Inventory.Revision,LDInventory::Buy,FGuid(),0,uint8(ELDSkill::Spark),FVector::ZeroVector);}Step=1;}
        if(E>4){for(auto PC:{A,B}){const auto* O=M->StateFor(PC.Get());Check(O->Gold==80&&O->Inventory.Items.Num()==23,TEXT("Injected creation failure or unsupported definition creates no item and spends nothing"));Check(O->Gold==80&&O->Inventory.Items.Num()==23,TEXT("Retired unquoted inventory-buy RPC cannot bypass the current vendor quote with a free slot"));}Enter(7);}
    }
    else if(Phase==7)
    {
        if(!Step&&E>1){A->GetPawn()->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(500,0,92),false,nullptr,ETeleportType::TeleportPhysics);Step=1;}
        if(E>4){Check(!V->Clients.FindChecked(A).Session.IsValid(),TEXT("Leaving body reach invalidates the vendor session on authority"));int32 N=0;for(auto PC:{A,B}){auto* O=M->StateFor(PC.Get());PC->GetPawn()->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(N++?35:-35,0,92),false,nullptr,ETeleportType::TeleportPhysics);O->ProtectedUntil=0;M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;M->SyncOwner(PC.Get());}Enter(8);}
    }
    else if(Phase==8)
    {
        if(!Step&&E>2){M->Combat->Queue(B.Get(),M->StateFor(B.Get())->Match,M->StateFor(B.Get())->Life,100000,0,-FVector::ForwardVector);Step=1;}
        if(E>4){Check(M->StateFor(A.Get())->Hp<LDDevelopment::MaxHP(M->StateFor(A.Get())->Build)&&!V->Clients.FindChecked(A).Session.IsValid(),TEXT("A real positive combat hit closes the victim's vendor session"));Check(!V->Clients.FindChecked(B).Session.IsValid(),TEXT("An occupied combat action closes the actor's vendor session"));Enter(9);}
    }
    else if(Phase==9)
    {
        if(!Step&&E>2){M->EndContenderLife(A.Get(),GetWorld()->GetTimeSeconds());G->MatchDeadline=float(GetWorld()->GetTimeSeconds()+.2);Step=1;}
        if(E>4){Check(!V->Clients.FindChecked(A).Session.IsValid()&&!V->Clients.FindChecked(B).Session.IsValid()&&G->EndReason==2,TEXT("Life end and actual No acquisition terminal receipt invalidate both vendor sessions"));Check(V->Requests.IsEmpty(),TEXT("Vendor queue drains after terminal state without a late purchase"));Enter(10);}
    }
}
void ALDVendorProbe::EarnedTick()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());const auto& O=PC->OwnerState;auto* N=PC->Navigation.Get();const auto* D=LDRegion::Definition(GetWorld());
    const bool Guild=FVector::Dist2D(PC->GetPawn()->GetActorLocation(),D->Guild)<=119;const double E=FPlatformTime::Seconds()-PhaseAt;
    if(E>180&&Step<13){Check(false,FString::Printf(TEXT("Earned route stalled at step %d: %s"),Step,*N->Status));Finish();return;}
    if(!Step&&E>4.5){N->Navigate(D->Guild,FGuid(),1);Step=1;}
    if(Step==1&&Guild&&!N->IsFollowing()&&GetWorld()->GetGameState<ALDGameState>()->GetServerWorldTimeSeconds()>=O.WorkReadyAt){PC->SetInteractionHeld(true);Step=2;}
    if(Step==2&&O.ContractStage==1){PC->SetInteractionHeld(false);DestinationSite=D->Node(O.ContractDestination)->Site;N->Navigate(D->Node(O.ContractDestination)->Position,FGuid(),DestinationSite);Step=3;}
    if(Step==3&&FVector::Dist2D(PC->GetPawn()->GetActorLocation(),D->Node(O.ContractDestination)->Position)<=119&&!N->IsFollowing()){PC->SetInteractionHeld(true);Step=4;}
    if(Step==4&&O.ContractStage==2){PC->SetInteractionHeld(false);N->Navigate(D->Guild,FGuid(),1);Step=5;}
    if(Step==5&&Guild&&!N->IsFollowing()){PriorJobs=O.PaidJobs;PC->SetInteractionHeld(true);Step=6;}
    if(Step==6&&O.PaidJobs>PriorJobs){PC->SetInteractionHeld(false);if(O.PaidJobs<2){Step=1;}else{Check(O.Gold==50&&O.Inventory.Items.Num()==1,TEXT("Native travel and two actual courier holds earn fifty gold without grants or teleports"));PC->OpenVendor();Step=8;}}
    if(Step==8&&PC->VendorQuote.Session.IsValid())
    {Check(PC->InventoryUI->IsOpen()&&!PC->CanWorldCommand()&&PC->VendorQuote.Offers.Num()==4,TEXT("Visible Guild service opens the nonpausing catalog beside the own bag"));PC->InventoryUI->SelectOffer(ELDSkill::Spark);BeforeInventory=O.Inventory.Revision;Session=PC->VendorQuote.Session;OldQuote=PC->VendorQuote.CatalogRevision;PC->InventoryUI->BuyOffer();Replay=PC->NextRequest;Check(PC->VendorPending()&&O.Gold==50&&O.Inventory.Items.Num()==1,TEXT("Pending Buy disables repeats and never displays an optimistic book or balance"));PC->InventoryUI->BuyOffer();Check(PC->NextRequest==Replay,TEXT("Repeated native Buy while pending does not submit a second request"));Step=9;}
    if(Step==9&&O.Gold==10&&O.Books.Num()==1){Book=O.Books[0].Id;PC->ServerVendorBuy(O.Match,O.Life,Replay,Session,OldQuote,40,BeforeInventory,ELDSkill::Spark);ActionAt=FPlatformTime::Seconds();Step=10;}
    if(Step==10&&FPlatformTime::Seconds()-ActionAt>.6){Check(O.Gold==10&&O.Books.Num()==1&&O.Inventory.Find(Book),TEXT("Earned purchase replay retains exactly one committed physical book"));PC->InventoryUI->Select(Book);PC->InventoryUI->Use();ActionAt=FPlatformTime::Seconds();Step=11;}
    if(Step==11&&FPlatformTime::Seconds()-ActionAt>.3){PC->SetInteractionHeld(true);Step=12;}
    if(Step==12&&LDDevelopment::Rank(O.Build,ELDSkill::Spark)==1&&!O.Inventory.Find(Book)){PC->SetInteractionHeld(false);Check(O.Gold==10&&!O.Build.Slots.Contains(ELDSkill::Spark),TEXT("Earned vendor book reaches the actual three-second read without automatic equipment or cast"));Step=13;}
}
void ALDVendorProbe::ClientTick()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());if(FPlatformTime::Seconds()-LastPing>.3){LastPing=FPlatformTime::Seconds();PC->ServerProbePing(LastPing);}if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;Step=0;PhaseAt=FPlatformTime::Seconds();}
    const auto& O=PC->OwnerState;const double E=FPlatformTime::Seconds()-PhaseAt;
    if(Phase==1){if(!bChoice){PC->Controls->ResetBindings();PC->RebuildV05Mappings();PC->ChooseLobbyClass(Role==TEXT("A")?1:2);bChoice=true;}if(!bReady&&O.Build.ClassId==(Role==TEXT("A")?1:2)&&E>.4){PC->ToggleLobbyReady();bReady=true;}if(O.bLobbyHost&&O.bReady&&E>1){PC->StartLobby();}return;}
    if(Phase==2&&Role==TEXT("A")){EarnedTick();return;}
    if(Phase==2&&Role==TEXT("B")&&!Step&&E>3){int32 Canonical=0;for(TActorIterator<ALDVendorDirector> It(GetWorld());It;++It){++Canonical;}for(TActorIterator<ALDItem> It(GetWorld());It;++It){++Canonical;}Check(!Canonical&&O.Gold==0&&O.Inventory.Items.Num()==1,TEXT("Restricted peer sees no canonical vendor/item actors or another contender's earnings"));Step=1;}
    if(Phase==3)
    {
        if(!Step){PC->Navigation->Navigate(LDRegion::Site(GetWorld(),1),FGuid(),1);Step=1;}
        if(Step==1&&PC->NearGuildVendor()&&!PC->Navigation->IsFollowing()){PC->OpenVendor();Step=2;}
        if(Step==2&&PC->VendorQuote.Session.IsValid()){OldQuote=PC->VendorQuote.CatalogRevision;Check(PC->VendorQuote.Offers.Num()==4&&PC->VendorQuote.Offers[0].Price==40,TEXT("Own live quote exposes four public unlimited-stock book definitions at forty gold"));PC->InventoryUI->SelectOffer(ELDSkill::Spark);Check(PC->InventoryUI->VendorText().Contains(TEXT("Already learned")),TEXT("Known knowledge is labelled while intentional duplicate purchase remains supported"));PC->BuyVendorBook(ELDSkill::Spark,OldQuote,40);Step=3;}
        if(Step==3&&E>5){Check(O.Gold==(Role==TEXT("A")?10:0)&&O.Inventory.Items.Num()==1&&PC->VendorFeedback.Contains(TEXT("Not enough gold")),TEXT("Insufficient gold rejection leaves both own inventory and gold unchanged"));Step=4;}
    }
    if(Phase==4)
    {
        if(!Step&&E>.6){PC->BuyVendorBook(ELDSkill::Spark,OldQuote,40);Step=1;}
        if(Step==1&&E>1.6){Check(O.Gold==80&&O.Inventory.Items.Num()==1&&PC->VendorFeedback.Contains(TEXT("Quote changed"))&&PC->VendorQuote.Offers[0].Price==41,TEXT("Stale quote refreshes the displayed price without charging it or retrying automatically"));PC->InventoryUI->SelectOffer(ELDSkill::Spark);BeforeInventory=O.Inventory.Revision;Session=PC->VendorQuote.Session;OldQuote=PC->VendorQuote.CatalogRevision;const auto BeforeReview=PC->NextRequest;PC->InventoryUI->BuyOffer();Check(PC->NextRequest==BeforeReview&&!PC->VendorPending()&&O.Gold==80,TEXT("A price-update click reviews the new offer without submitting a purchase"));Step=5;}
        if(Step==5&&E>2.1){PC->InventoryUI->BuyOffer();Replay=PC->NextRequest;Step=2;}
        if(Step==2&&E>3.1){PC->ServerVendorBuy(O.Match,O.Life,Replay,Session,OldQuote,41,BeforeInventory,ELDSkill::Spark);Step=3;}
        if(Step==3&&E>4){Check(O.Gold==39&&O.Books.Num()==1&&PC->VendorFeedback.Contains(TEXT("Purchased one")),TEXT("Fresh explicit quote confirmation creates one book and one debit even after replay"));Step=4;}
    }
    if(Phase==5){if(!Step&&E>.6){PC->BuyVendorBook(ELDSkill::Spark,PC->VendorQuote.CatalogRevision,40);Step=1;}if(Step==1&&E>2){Check(O.Gold==80&&O.Inventory.Items.Num()==24&&PC->VendorFeedback.Contains(TEXT("Inventory full")),TEXT("Full own bag shows authority rejection without spending gold or hiding a ghost item"));Step=2;}}
    if(Phase==6){if(!Step&&E>.6){PC->BuyVendorBook(Role==TEXT("A")?ELDSkill::Spark:ELDSkill::Ambush,PC->VendorQuote.CatalogRevision,40);Step=1;}if(Step==1&&E>2){Check(O.Gold==80&&O.Inventory.Items.Num()==23&&PC->VendorFeedback.Contains(Role==TEXT("A")?TEXT("creation failed"):TEXT("not sold")),TEXT("Creation failure or invalid catalog definition returns a visible conserved outcome"));Step=2;}}
    if(Phase==7)
    {
        if(Role==TEXT("B")&&!Step&&E>.4){PC->InventoryUI->Close();PC->OpenVendor();PC->InventoryUI->Close();Step=1;}
        if(Role==TEXT("B")&&Step==1&&E>1.5){Check(!PC->VendorIsOpen()&&!PC->InventoryUI->IsOpen(),TEXT("Closing during quote flight discards the late reply and never reopens the panel"));PC->OpenVendor();Step=2;}
        if(Role==TEXT("A")&&!Step&&E>2.5){Check(!PC->VendorIsOpen()&&!PC->InventoryUI->IsOpen()&&O.Gold==80,TEXT("Out-of-range invalidation closes the actual major vendor panel"));Step=1;}
    }
    if(Phase==8){if(!Step&&E>.5){if(!PC->VendorIsOpen()){PC->OpenVendor();}Step=1;}if(Step==1&&E>3.5){Check(!PC->VendorIsOpen()&&!PC->InventoryUI->IsOpen(),TEXT("Positive damage or occupied action closes the native vendor panel"));Step=2;}}
    if(Phase==9){if(!Step&&E>.5){PC->OpenVendor();Step=1;}if(Step==1&&E>3.5){Check(!PC->VendorIsOpen()&&!PC->InventoryUI->IsOpen(),TEXT("Life or match terminal state cannot retain a live shop"));Step=2;}}
    if(Phase==10&&E>1){Check(true,TEXT("All vendor phases completed"));Finish();}
}
void ALDVendorProbe::Finish()
{
    SetActorTickEnabled(false);auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("profile"),TEXT("B55 independent native clients: A earns two actual courier jobs from zero gold using native navigation and real holds, buys and reads one book; subsequent 80-gold/full-bag/price-41/creation-failure/range/combat/death/deadline setup are explicit adversarial fixtures, not earned economy or human evidence"));
    if(GetNetMode()==NM_Client){auto Echo=CastChecked<ALDPlayerController>(GetOwner())->ProbeRtts;Echo.Sort();Check(Echo.Num()>=8,TEXT("Independent vendor client measured transport echo"));R->SetNumberField(TEXT("medianEchoRttMs"),Echo.IsEmpty()?0:Echo[Echo.Num()/2]);}
    R->SetNumberField(TEXT("failures"),Failures);TArray<TSharedPtr<FJsonValue>> Values;for(const auto& C:Checks){Values.Add(MakeShared<FJsonValueString>(C));}R->SetArrayField(TEXT("checks"),Values);FString Dir,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Dir);FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Output));if(!FFileHelper::SaveStringToFile(Output,*FPaths::Combine(Dir,Role+TEXT(".json")))){++Failures;}FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
