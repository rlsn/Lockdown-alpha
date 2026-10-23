#include "Tests/LDEconomyProbe.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDBankDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDVendorDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Vision/LDVisionDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDWorldChecks.h"
#include "Region/LDRegionDefinition.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
ALDEconomyProbe::ALDEconomyProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDEconomyProbe::Tick(float Delta)
{
#if !UE_BUILD_SHIPPING
    if(Done){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Cast<ALDPlayerController>(GetWorld()->GetFirstPlayerController());if(!PC||!PC->GetPawn()){return;}Done=true;
    int32 Failures=0;TArray<TSharedPtr<FJsonValue>> Checks;auto Check=[&](bool Good,const FString& Text){Failures+=!Good;Checks.Add(MakeShared<FJsonValueString>((Good?TEXT("PASS "):TEXT("FAIL "))+Text));UE_LOG(LogTemp,Display,TEXT("LD_ECONOMY_PROBE %s %s"),Good?TEXT("PASS"):TEXT("FAIL"),*Text);};FString Why;
    M->SetLobbyChoice(PC,0,true,Why);if(!M->StartDevelopmentMatch(PC,Why)){Check(false,Why);FPlatformMisc::RequestExitWithStatus(false,1);return;}
    auto* E=M->Economy.Get();auto* I=M->Inventory.Get();auto* B=M->Bank.Get();auto* O=M->StateFor(PC);auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());const double Now=GetWorld()->GetTimeSeconds();
    Check(E->Audit(Why),TEXT("Initial market stock, normal ward and all balances reconcile: ")+Why);
    int64 MarketGold=0;for(const auto& Market:E->Rules().Markets){MarketGold+=E->Accounting().Account(Market.Id)->Gold;}Check(MarketGold==450,TEXT("Market endowment450 is separate from actual NPC purses"));
    Check(E->Accounting().Remaining(TEXT("Outcrop1"))==3&&E->Accounting().Remaining(TEXT("Outcrop2"))==3&&E->Accounting().Remaining(TEXT("CoreOrders"))==3,TEXT("Node/order budgets initialized from locked roster"));
    const int32 ReceiptsBefore=E->Accounting().ReceiptCount();Check(!E->Initialize(Why)&&E->Accounting().ReceiptCount()==ReceiptsBefore,TEXT("Repeated economy initialization cannot refill markets or budgets"));
    const int32 Before=O->Inventory.Items.Num();I->Initialize(PC);Check(O->Inventory.Items.Num()==Before,TEXT("Repeated inventory initialization grants nothing"));
    Body->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(-60,30,92));O->ProtectedUntil=0;auto* S=M->Combat->StateFor(Body);S->ProtectionUntil=0;S->LastDamage=S->LastDealt=-1000;
    O->Gold=200;E->GoldChanged(TEXT("DiagnosticReward"),200);Check(E->Audit(Why),TEXT("Explicit labelled diagnostic mint reconciles"));
    B->Open(PC,O->Match,O->Life,1000);auto Q=B->Clients.FindChecked(PC).View;Check(Q.Session.IsValid(),TEXT("Physical Treasury session opens on actual map sight/range"));
    B->Queue(PC,O->Match,O->Life,1001,Q.Session,100,true,Q.WalletRevision,Q.BankRevision);B->Queue(PC,O->Match,O->Life,1001,Q.Session,100,true,Q.WalletRevision,Q.BankRevision);B->Sample(Now);
    Check(O->Gold==100&&O->BankGold==98,TEXT("Queued duplicate deposit100 debits once and banks98"));
    B->Queue(PC,O->Match,O->Life,1001,Q.Session,100,true,Q.WalletRevision,Q.BankRevision);B->Sample(Now);Check(O->Gold==100&&O->BankGold==98,TEXT("Committed deposit replay is idempotent"));
    B->Queue(PC,O->Match,O->Life,1002,Q.Session,20,false,Q.WalletRevision,Q.BankRevision);B->Sample(Now);Check(O->Gold==100&&O->BankGold==98,TEXT("Stale revision withdrawal changes nothing"));
    Q=B->Clients.FindChecked(PC).View;B->Queue(PC,O->Match,O->Life,1003,Q.Session,30,false,Q.WalletRevision,Q.BankRevision);B->Sample(Now);Check(O->Gold==130&&O->BankGold==68,TEXT("Physical withdrawal30 leaves68 and wallet130"));
    Q=B->Clients.FindChecked(PC).View;B->Clients.FindChecked(PC).Tokens=4;B->Queue(PC,O->Match,O->Life,1004,Q.Session,10,true,Q.WalletRevision,Q.BankRevision);Body->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(500,0,92));B->Sample(Now);Check(O->Gold==130&&O->BankGold==68,TEXT("Leaving range before queued commit spends nothing"));
    Body->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(-60,30,92));
    auto* Book=GetWorld()->SpawnActor<ALDBook>();Book->State=0;Book->Skill=ELDSkill::Spark;
    Check(I->Adopt(PC,Book,TEXT("OfficialBookPurchase"),false),TEXT("Paid output allocated before charge"));O->Gold-=40;E->GoldChanged(TEXT("OfficialBookPurchase"),0,40);
    Check(Book->OriginReceipt.IsValid()&&E->Audit(Why),TEXT("Book and payment share a causal receipt"));
    const FGuid BookId=Book->Identity;I->Remove(PC,Book,true,TEXT("BookRead"));Check(E->Accounting().Item(BookId)->Consumed&&E->Audit(Why),TEXT("Book read is a once-only explicit sink"));
    auto* Cargo=GetWorld()->SpawnActor<ALDItem>();Cargo->Kind=ELDItemKind::Ore;Check(I->Adopt(PC,Cargo,TEXT("DiagnosticItemCreation")),TEXT("Labelled cargo fixture enters real inventory"));
    const FGuid Origin=Cargo->OriginReceipt;Cargo->OriginReceipt=FGuid::NewGuid();Check(!E->Audit(Why),TEXT("Audit detects altered provenance"));Cargo->OriginReceipt=Origin;
    auto* Duplicate=GetWorld()->SpawnActor<ALDItem>();Duplicate->Identity=Cargo->Identity;Check(!E->Audit(Why),TEXT("Audit detects duplicate physical identity"));Duplicate->Destroy();
    const FGuid Ward=O->Inventory.Items.FindByPredicate([](const auto& X){return X.Kind==ELDItemKind::WardCharge;})->Id;
    const bool Placed=M->Vision->PlaceWard(PC,LDRegion::Site(GetWorld(),1)+FVector(-130,30,0),Why);Check(Placed,TEXT("Actual ward placement creates timed source: ")+Why);
    Check(!O->Inventory.Find(Ward)&&!E->Accounting().Item(Ward)->Consumed&&E->Audit(Why),TEXT("Ward source and spent charge count once with same lineage"));
    for(TActorIterator<ALDWard> It(GetWorld());It;++It){It->ExpiresAt=float(Now-1);}M->Vision->Tick(0);
    Check(E->Accounting().Item(Ward)->Consumed&&E->Audit(Why),TEXT("Ward expiry is an explicit sink"));
    M->EndContenderLife(PC,Now);Check(O->BankGold==68&&O->Gold==76,TEXT("Death taxes only wallet and retains bank"));
    Check(Cargo->State==0&&E->Audit(Why),TEXT("Death drops same cargo identity outside corpse cleanup"));
    M->ReturnDueContenders(Now+100);Check(O->bActive&&O->BankGold==68&&O->Inventory.Items.IsEmpty(),TEXT("Return retains bank and does not refill inventory"));
    Check(I->Adopt(PC,Cargo)&&E->Audit(Why),TEXT("Recovered cargo transfers existing identity without creation"));
    FGuid Npc;for(TActorIterator<ALDResident> It(GetWorld());It;++It){Npc=M->WorldChecks->WalletId(*It);break;}Check(E->Accounting().Account(ALDEconomyDirector::Wallet(Npc))->Gold==36,TEXT("Lazy NPC endowment recorded once"));
    const int64 NpcTotal=E->Accounting().TotalGold();auto* Returned=GetWorld()->SpawnActor<ALDResident>();M->WorldChecks->BindReturnedBody(Returned,Npc);Check(M->WorldChecks->WalletId(Returned)==Npc&&E->Accounting().TotalGold()==NpcTotal,TEXT("Recreated NPC body reuses purse without another endowment"));Returned->Destroy();
    Check(E->AuditFailures==0&&E->Audit(Why),TEXT("All actual mutation batches reconciled without repair"));
    Check(E->Export(TEXT("B61Verified"),Why),TEXT("Export complete receipt/account/item/budget evidence"));
    const int64 Total=E->Accounting().TotalGold();E->Retire(PC);Check(E->Accounting().TotalGold()==Total&&E->Accounting().Reconcile(Why),TEXT("Disconnected/retired balances remain in total"));
    auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("failures"),Failures);Report->SetArrayField(TEXT("checks"),Checks);Report->SetStringField(TEXT("scope"),TEXT("Standalone authority fixture; not UI/network/normal earning evidence"));FString Json,Path;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));FParse::Value(FCommandLine::Get(),TEXT("LDProbeReport="),Path);if(!FFileHelper::SaveStringToFile(Json,*Path)){++Failures;}
    FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
#endif
}
