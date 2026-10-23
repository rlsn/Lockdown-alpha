#include "Tests/LDCommerceProbe.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDCommerceDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
ALDCommerceProbe::ALDCommerceProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDCommerceProbe::Tick(float Delta)
{
#if !UE_BUILD_SHIPPING
    if(Done){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* A=Cast<ALDPlayerController>(GetWorld()->GetFirstPlayerController());if(!A||!A->GetPawn()){return;}Done=true;
    int32 Failures=0;TArray<TSharedPtr<FJsonValue>> Checks;auto Check=[&](bool Good,const FString& Message){Failures+=!Good;Checks.Add(MakeShared<FJsonValueString>((Good?TEXT("PASS "):TEXT("FAIL "))+Message));UE_LOG(LogTemp,Display,TEXT("LD_COMMERCE_PROBE %s %s"),Good?TEXT("PASS"):TEXT("FAIL"),*Message);};
    auto Finish=[&](){auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("failures"),Failures);Report->SetArrayField(TEXT("checks"),Checks);Report->SetStringField(TEXT("scope"),TEXT("Two local owners on one native authority; labelled diagnostic seed/cleanup; no remote/UI/human evidence"));FString Json,Path;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));FParse::Value(FCommandLine::Get(),TEXT("LDProbeReport="),Path);if(!FFileHelper::SaveStringToFile(Json,*Path)){++Failures;}FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);};
    auto* B=Cast<ALDPlayerController>(UGameplayStatics::CreatePlayer(this,1,true));Check(B&&B->GetPawn()&&M->StateFor(B),TEXT("Two distinct local owner records admitted before roster lock"));if(!B||!B->GetPawn()||!M->StateFor(B)){Finish();return;}
    FString Error;M->SetLobbyChoice(A,0,true,Error);M->SetLobbyChoice(B,0,true,Error);if(!M->StartDevelopmentMatch(A,Error)){Check(false,Error);Finish();return;}
    auto* E=M->Economy.Get();auto* C=M->Commerce.Get();auto* I=M->Inventory.Get();auto* OA=M->StateFor(A);auto* OB=M->StateFor(B);const double Now=GetWorld()->GetTimeSeconds();uint32 Token=10000,Request=20000;
    OA->Gold=OB->Gold=2000;E->GoldChanged(TEXT("DiagnosticReward"),4000);
    auto Move=[&](ALDPlayerController* PC,uint8 Service){PC->GetPawn()->SetActorLocation(C->Position(GetWorld(),Service)+FVector(-60,30,92));auto* O=M->StateFor(PC);O->ProtectedUntil=0;auto* S=M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()));S->ProtectionUntil=0;S->Action=0;S->LastDamage=S->LastDealt=-1000;};
    auto Open=[&](ALDPlayerController* PC,uint8 Service){Move(PC,Service);C->Clients.FindOrAdd(PC).Tokens=4;C->Open(PC,M->StateFor(PC)->Match,M->StateFor(PC)->Life,++Token,Service);return C->Clients.FindChecked(PC).View;};
    auto Quote=[&](uint8 Service){return E->CommerceQuote(Service);};
    auto Offer=[&](uint8 Service,ELDItemKind Kind){return *Quote(Service).Offers.FindByPredicate([&](const auto& F){return F.Kind==Kind;});};
    auto Send=[&](ALDPlayerController* PC,uint8 Command,ELDItemKind Kind,FGuid Id=FGuid())
    {auto& Client=C->Clients.FindChecked(PC);Client.Tokens=4;const auto Terms=E->CommerceQuote(Client.View.Service);const auto* F=Terms.Offers.FindByPredicate([&](const auto& V){return V.Kind==Kind;});const auto* O=M->StateFor(PC);const auto* Item=O->Inventory.Find(Id);const uint32 IdRequest=++Request;C->Queue(PC,O->Match,O->Life,IdRequest,Client.View.Session,Terms.Revision,Command,Kind,Id,Item?Item->Revision:0,O->Inventory.Revision,F?(Command==1?F->Ask:F->Bid):0);C->Sample(Now);return C->Clients.FindChecked(PC).Receipts.FindRef(IdRequest);};
    auto Clear=[&](ALDPlayerController* PC){const auto Items=M->StateFor(PC)->Inventory.Items;for(const auto& Item:Items){if(Item.Kind!=ELDItemKind::WardCharge){I->Remove(PC,I->Owned(PC,Item.Id),true,TEXT("DiagnosticCleanup"));}}};
    auto Q=Open(A,0);Check(Q.Session.IsValid()&&Offer(0,ELDItemKind::Fiber).Ask==4&&Offer(0,ELDItemKind::Ore).Ask==6,TEXT("Official material service uses versioned fixed 4/6 prices"));
    E->bFailNextSupply=true;const auto AllocationCount=OA->Inventory.Items.Num();Check(Send(A,1,ELDItemKind::Fiber).Contains(TEXT("allocation failed"))&&OA->Gold==2000&&OA->Inventory.Items.Num()==AllocationCount,TEXT("Failed supply allocation cannot charge wallet or create output"));
    const int64 Total=E->Accounting().TotalGold();Send(A,1,ELDItemKind::Fiber);const auto Fiber=OA->Inventory.Items.Last();Send(A,1,ELDItemKind::Ore);
    Check(OA->Gold==1990&&E->Accounting().TotalGold()==Total-10&&E->Accounting().Burned()==10,TEXT("Official purchases create exact instances and burn exactly ten gold"));
    Check(E->Accounting().Item(Fiber.Id)&&I->Owned(A,Fiber.Id)->OriginReceipt.IsValid(),TEXT("Paid commodity has causal origin receipt"));
    const int32 Before=OA->Inventory.Items.Num();Q=C->Clients.FindChecked(A).View;const uint32 Replay=++Request;C->Clients.FindChecked(A).Tokens=4;const auto Inv=OA->Inventory.Revision;
    for(int32 N=0;N<2;++N){C->Queue(A,OA->Match,OA->Life,Replay,Q.Session,Q.Revision,1,ELDItemKind::Fiber,FGuid(),0,Inv,4);}C->Sample(Now);C->Queue(A,OA->Match,OA->Life,Replay,Q.Session,Q.Revision,1,ELDItemKind::Fiber,FGuid(),0,Inv,4);C->Sample(Now);
    Check(OA->Inventory.Items.Num()==Before+1&&OA->Gold==1986,TEXT("Pending and committed purchase replay allocate and charge once"));
    Q=Open(A,1);const FGuid First=E->MarketItems(1,ELDItemKind::Fiber)[0]->Identity;const FGuid Origin=E->MarketItems(1,ELDItemKind::Fiber)[0]->OriginReceipt;const int64 GoldBefore=E->Accounting().TotalGold();Send(A,1,ELDItemKind::Fiber);
    Check(OA->Inventory.Find(First)&&I->Owned(A,First)->OriginReceipt==Origin,TEXT("Regional buy transfers oldest existing identity and preserves origin"));
    Check(E->Accounting().TotalGold()==GoldBefore&&Offer(1,ELDItemKind::Fiber).Stock==7,TEXT("Regional payment transfers cash without mint/burn or restock"));
    const int64 WalletBefore=OA->Gold;Send(A,2,ELDItemKind::Fiber,First);Check(!OA->Inventory.Find(First)&&E->Accounting().TotalGold()==GoldBefore&&OA->Gold<WalletBefore+6,TEXT("Regional resale returns same item and same-market cycle loses gold"));
    Open(B,1);Q=Open(A,1);const auto QB=C->Clients.FindChecked(B).View;const uint32 RA=++Request,RB=++Request;const int32 Ask=Offer(1,ELDItemKind::Fiber).Ask;C->Clients.FindChecked(A).Tokens=C->Clients.FindChecked(B).Tokens=4;
    C->Queue(A,OA->Match,OA->Life,RA,Q.Session,Q.Revision,1,ELDItemKind::Fiber,FGuid(),0,OA->Inventory.Revision,Ask);C->Queue(B,OB->Match,OB->Life,RB,QB.Session,QB.Revision,1,ELDItemKind::Fiber,FGuid(),0,OB->Inventory.Revision,Ask);const auto CountB=OB->Inventory.Items.Num();C->Sample(Now);
    Check(C->Clients.FindChecked(B).Receipts.FindRef(RB).Contains(TEXT("Quote changed"))&&OB->Inventory.Items.Num()==CountB,TEXT("Two queued owners on one quote serialize; stale loser keeps funds and bag"));
    const auto CountA=OA->Inventory.Items.Num();C->Clients.FindChecked(A).Tokens=4;C->Queue(A,OA->Match,OA->Life,++Request,QB.Session,Q.Revision,1,ELDItemKind::Fiber,FGuid(),0,OA->Inventory.Revision,Ask);C->Sample(Now);Check(OA->Inventory.Items.Num()==CountA,TEXT("Foreign owner session cannot authorize a purchase"));
    auto* Physical=I->Owned(A,Fiber.Id);Physical->bReserved=true;auto* Entry=OA->Inventory.Items.FindByPredicate([&](const auto& V){return V.Id==Fiber.Id;});Entry->bReserved=true;
    Check(Send(A,2,ELDItemKind::Fiber,Fiber.Id).Contains(TEXT("reserved"))&&OA->Inventory.Find(Fiber.Id),TEXT("Shared reservation blocks market sale"));Physical->bReserved=false;Entry->bReserved=false;
    auto* Core=GetWorld()->SpawnActor<ALDItem>();Core->Kind=ELDItemKind::Core;I->Adopt(A,Core,TEXT("DiagnosticItemCreation"));Check(Send(A,2,ELDItemKind::Core,Core->Identity).Contains(TEXT("no Buy/Sell")),TEXT("Core has no regional buyback"));I->Remove(A,Core,true,TEXT("DiagnosticCleanup"));
    for(int32 Guard=0;Guard<20&&Offer(1,ELDItemKind::Fiber).Stock<Offer(1,ELDItemKind::Fiber).Cap;++Guard){Open(A,0);Send(A,1,ELDItemKind::Fiber);const FGuid Item=OA->Inventory.Items.Last().Id;Open(A,1);Send(A,2,ELDItemKind::Fiber,Item);}
    const int64 FullGold=OA->Gold;Check(Send(A,2,ELDItemKind::Fiber,Fiber.Id).Contains(TEXT("stock full"))&&OA->Gold==FullGold&&OA->Inventory.Find(Fiber.Id),TEXT("Full market rejects sale without destroying owned item"));
    // Drain/restore finite reserve through explicit fixture transfers, never a mint.
    auto Cash=[&](int64 Amount){LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("DiagnosticReserveTransfer");const FName W=E->Wallet(M->ContenderId(A));const auto* MA=E->Ledger.Account(TEXT("City"));const auto* WA=E->Ledger.Account(W);T.Gold={{TEXT("City"),MA->Revision,-Amount},{W,WA->Revision,Amount}};Check(E->Ledger.Commit(T,Error),TEXT("Labelled cash fixture remains a conserved transfer"));OA->Gold=E->Ledger.Account(W)->Gold;};
    // Ore stock has room even though Fiber is full.
    const auto Ore=OA->Inventory.Items.FindByPredicate([](const auto& V){return V.Kind==ELDItemKind::Ore;})->Id;const auto OldRevision=Quote(1).Revision;const int64 Reserve=Quote(1).Cash;Cash(Reserve);
    Check(Quote(1).Revision!=OldRevision,TEXT("Cash-only changes invalidate the entire institution quote"));Check(Send(A,2,ELDItemKind::Ore,Ore).Contains(TEXT("insufficient cash"))&&OA->Inventory.Find(Ore),TEXT("Empty reserve rejects sale without minting payment"));Cash(-Reserve);
    Clear(A);Open(A,1);for(int32 Guard=0;Guard<20&&Offer(1,ELDItemKind::Crystal).Stock>0;++Guard){Send(A,1,ELDItemKind::Crystal);}const auto EmptyCount=OA->Inventory.Items.Num();const int64 EmptyGold=OA->Gold;
    Check(Send(A,1,ELDItemKind::Crystal).Contains(TEXT("stock empty"))&&OA->Gold==EmptyGold&&OA->Inventory.Items.Num()==EmptyCount,TEXT("Empty stock never creates phantom material"));
    Clear(A);Open(A,0);for(int32 Guard=0;Guard<24&&OA->Inventory.Items.Num()<24;++Guard){Send(A,1,ELDItemKind::Fiber);}const auto Receipts=E->Accounting().ReceiptCount();Check(Send(A,1,ELDItemKind::Fiber).Contains(TEXT("Inventory full"))&&E->Accounting().ReceiptCount()==Receipts,TEXT("Full bag purchase rejects before payment or allocation"));
    Clear(A);for(int32 Guard=0;Guard<24&&LDInventory::Weight(OA->Inventory)+3<=LDInventory::WeightCapacity(OA->Build);++Guard){Send(A,1,ELDItemKind::Ore);}const int64 HeavyGold=OA->Gold;
    Check(OA->Inventory.Items.Num()<24&&Send(A,1,ELDItemKind::Ore).Contains(TEXT("heavy"))&&OA->Gold==HeavyGold,TEXT("Weight cap rejects with spare slots and no charge"));
    Clear(A);Q=Open(A,1);C->Clients.FindChecked(A).Tokens=4;C->Queue(A,OA->Match,OA->Life,++Request,Q.Session,Q.Revision,1,ELDItemKind::Ore,FGuid(),0,OA->Inventory.Revision,Offer(1,ELDItemKind::Ore).Ask);Move(A,2);const int64 RemoteGold=OA->Gold;C->Sample(Now);Check(OA->Gold==RemoteGold&&!C->Clients.FindChecked(A).View.Session.IsValid(),TEXT("Range loss before commit cancels and stops remote quote updates"));
    for(uint8 Service:{uint8(2),uint8(3)}){Q=Open(A,Service);Check(Q.Session.IsValid(),TEXT("Native body reaches ")+C->ServiceName(Service));}
    Check(E->AuditFailures==0&&E->Audit(Error),TEXT("All commerce paths conserve actual wallets/items with no repair"));Check(E->Export(TEXT("B62Verified"),Error),TEXT("Exported complete B62 receipt evidence"));Finish();
#endif
}
