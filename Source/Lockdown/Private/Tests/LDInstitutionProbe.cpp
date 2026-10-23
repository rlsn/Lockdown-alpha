#include "Tests/LDInstitutionProbe.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDInstitutionSite.h"
#include "Economy/LDProductionDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
ALDInstitutionProbe::ALDInstitutionProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDInstitutionProbe::Check(bool Good,const FString& Message)
{Failures+=!Good;Checks.Add(MakeShared<FJsonValueString>((Good?TEXT("PASS "):TEXT("FAIL "))+Message));UE_LOG(LogTemp,Display,TEXT("LD_INSTITUTION_PROBE %s %s"),Good?TEXT("PASS"):TEXT("FAIL"),*Message);}
void ALDInstitutionProbe::Finish(const FString& Scope)
{Done=true;auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("failures"),Failures);R->SetArrayField(TEXT("checks"),Checks);R->SetArrayField(TEXT("travel"),Travel);R->SetStringField(TEXT("scope"),Scope);FString Json,Path;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FParse::Value(FCommandLine::Get(),TEXT("LDProbeReport="),Path);if(!FFileHelper::SaveStringToFile(Json,*Path)){++Failures;}FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);}
void ALDInstitutionProbe::Tick(float Delta)
{
#if !UE_BUILD_SHIPPING
    if(Done){return;}auto* PC=Cast<ALDPlayerController>(GetWorld()->GetFirstPlayerController());if(!PC||!PC->GetPawn()){return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDGeography"))){Geography(Delta);}else if(!Started){Started=true;Authority();}
#endif
}
void ALDInstitutionProbe::Authority()
{
#if !UE_BUILD_SHIPPING
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* A=Cast<ALDPlayerController>(W->GetFirstPlayerController());auto* B=Cast<ALDPlayerController>(UGameplayStatics::CreatePlayer(this,1,true));
    if(!B||!B->GetPawn()){Check(false,TEXT("Second native owner admitted"));Finish(TEXT("Startup failure"));return;}FString Error;M->SetLobbyChoice(A,0,true,Error);M->SetLobbyChoice(B,1,true,Error);
    if(!M->StartDevelopmentMatch(A,Error)){Check(false,Error);Finish(TEXT("Startup failure"));return;}auto* E=M->Economy.Get();auto* C=M->Combat.Get();auto* D=M->Production.Get();auto* I=M->Inventory.Get();auto* G=W->GetGameState<ALDGameState>();auto* OA=M->StateFor(A);auto* OB=M->StateFor(B);C->SetActorTickEnabled(false);OA->Gold=OB->Gold=1000;E->GoldChanged(TEXT("DiagnosticReward"),2000);uint32 Token=50000;
    auto At=[&](double Elapsed){W->TimeSeconds=M->StartedAt()+Elapsed;E->SampleInstitutions(W->GetTimeSeconds());};
    auto Cash=[&](uint8 Service,int64 Desired){const FName Market=E->MarketId(Service),Wallet=E->Wallet(M->ContenderId(A));const int64 Difference=E->Ledger.Account(Market)->Gold-Desired;LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("DiagnosticReserveTransfer");T.Gold={{Market,E->Ledger.Account(Market)->Revision,-Difference},{Wallet,E->Ledger.Account(Wallet)->Revision,Difference}};const bool Good=E->Ledger.Commit(T,Error);OA->Gold=E->Ledger.Account(Wallet)->Gold;return Good;};
    auto Stock=[&](uint8 Service,ELDItemKind Kind,int32 Desired){auto Items=E->MarketItems(Service,Kind);for(int32 N=Items.Num();N<Desired;++N){auto* P=W->SpawnActor<ALDItem>();P->Kind=Kind;P->Holder=E;P->State=1;P->Institution=E->MarketId(Service);E->Record(TEXT("DiagnosticItemCreation"));}Items=E->MarketItems(Service,Kind);while(Items.Num()>Desired){auto* P=Items[0];Items.RemoveAt(0);P->State=2;P->Holder.Reset();E->Record(TEXT("DiagnosticCleanup"),0,0,{P->Identity});P->Destroy();}++E->MarketRevisions.FindChecked(Service);};
    Check(E->Rules().InstitutionTimes.Num()==8&&E->InstitutionCursor==0&&G->EconomyNextTick==float(M->StartedAt()+180),TEXT("Match starts with eight scheduled opportunities and public next-tick cue"));
    Stock(2,ELDItemKind::Fiber,36);const auto BeforeFull=E->Ledger.ReceiptCount();Check(E->ApplyInstitution(0,0,W->GetTimeSeconds(),Error)==4&&E->Ledger.ReceiptCount()==BeforeFull,TEXT("Full producer stock skips without charge, output or debt"));Stock(2,ELDItemKind::Fiber,18);
    Check(Cash(2,0),TEXT("Empty reserve fixture conserves all wallets"));const auto EmptyGold=E->Ledger.TotalGold();Check(E->ApplyInstitution(0,0,W->GetTimeSeconds(),Error)==3&&E->Ledger.TotalGold()==EmptyGold,TEXT("Unfunded producer skips without negative cash or hidden credit"));Check(Cash(2,150),TEXT("Reserve fixture restoration remains a transfer"));
    E->bFailNextInstitution=true;const auto AllocationBefore=E->Ledger.ReceiptCount();Check(E->ApplyInstitution(0,0,W->GetTimeSeconds(),Error)==5&&E->Ledger.ReceiptCount()==AllocationBefore&&E->Ledger.Account(TEXT("WestTown"))->Gold==150,TEXT("Failed producer allocation changes neither reserve nor stock"));
    Stock(1,ELDItemKind::Toolkit,0);const auto EmptyBefore=E->Ledger.ReceiptCount();Check(E->ApplyInstitution(4,0,W->GetTimeSeconds(),Error)==2&&E->Ledger.ReceiptCount()==EmptyBefore,TEXT("Empty consumer skips with no phantom item, cash or deferred debt"));Stock(1,ELDItemKind::Toolkit,2);
    const FGuid Oldest=E->MarketItems(2,ELDItemKind::Fiber)[0]->Identity;At(179.999);Check(E->InstitutionCursor==0,TEXT("No institution commits before the first scheduled endpoint"));At(180);
    Check(E->InstitutionCursor==1&&E->Ledger.Burned()==5&&E->Ledger.Account(TEXT("WestTown"))->Gold==148&&E->Ledger.Account(TEXT("EastTown"))->Gold==147,TEXT("First tick burns actual producer fees of two and three gold"));
    Check(E->MarketItems(2,ELDItemKind::Fiber).Num()==18&&E->MarketItems(3,ELDItemKind::Ore).Num()==18&&E->MarketItems(1,ELDItemKind::Toolkit).Num()==1,TEXT("Funded creation precedes separate actual consumption; net raw stock is unchanged"));
    Check(E->Ledger.Item(Oldest)->Consumed,TEXT("Institution consumes oldest origin receipt then GUID deterministically"));
    Check(E->InstitutionSites[0]->Status==1&&FMath::IsNearlyEqual(E->InstitutionSites[0]->WorkUntil-W->GetTimeSeconds(),3.),TEXT("Consumer three-second work cue accompanies already committed receipt"));
    const auto Once=E->Ledger.ReceiptCount();At(180.1);Check(E->Ledger.ReceiptCount()==Once,TEXT("Repeated sample cannot replay a scheduled tick"));
    for(const auto& Site:E->InstitutionSites){Check(!Site->CanBeDamaged()&&!Site->IsA(ALDCharacter::StaticClass()),TEXT("Marked institution is protected and has no combat body or lootable personal purse"));Site->Destroy();}At(180.2);Check(E->InstitutionSites.Num()==5&&IsValid(E->InstitutionSites[0])&&E->Ledger.ReceiptCount()==Once&&E->InstitutionCursor==1,TEXT("Recreating service markers does not reset stock, cash, cursor or endowment"));
    At(360);At(540);Check(E->InstitutionSites[0]->Status==2&&E->MarketItems(1,ELDItemKind::Toolkit).IsEmpty(),TEXT("City consumes only its two actual Toolkits; later tick logs empty skip"));
    auto Move=[&](ALDPlayerController* PC){PC->GetPawn()->SetActorLocation(D->Position(W,4)+FVector(-50,30,92));auto* O=M->StateFor(PC);O->ProtectedUntil=0;auto* S=C->StateFor(Cast<ALDCharacter>(PC->GetPawn()));S->ProtectionUntil=0;S->Action=0;S->LastDamage=S->LastDealt=-1000;Cast<ALDCharacter>(PC->GetPawn())->GetCharacterMovement()->StopMovementImmediately();};
    At(599.9);Move(A);auto* Commerce=M->Commerce.Get();Commerce->Open(A,OA->Match,OA->Life,++Token,3);const auto Quote=E->CommerceQuote(3);const auto OldToolkit=*Quote.Offers.FindByPredicate([](const auto& F){return F.Kind==ELDItemKind::Toolkit;});const int64 PreGold=OA->Gold;
    const uint32 QuoteRequest=++Token;Commerce->Queue(A,OA->Match,OA->Life,QuoteRequest,Commerce->Clients.FindChecked(A).View.Session,Quote.Revision,1,ELDItemKind::Toolkit,FGuid(),0,OA->Inventory.Revision,OldToolkit.Ask);
    W->TimeSeconds=M->StartedAt()+600;C->Sample(W->GetTimeSeconds());const auto Premium=E->CommerceQuote(3);const auto Toolkit=*Premium.Offers.FindByPredicate([](const auto& F){return F.Kind==ELDItemKind::Toolkit;});
    Check(G->ShortagePhase==1&&Toolkit.Ask==33&&Premium.Revision!=Quote.Revision,TEXT("Exactly at 600 East Toolkit quote changes to 5/4 before fixed base clamps"));
    Check(OA->Gold==PreGold&&Commerce->Clients.FindChecked(A).Receipts.FindRef(QuoteRequest).Contains(TEXT("Quote changed")),TEXT("Queued old quote rejects after scheduled boundary instead of charging changed terms"));
    Check(Premium.Offers[0].Ask==Quote.Offers[0].Ask&&E->CommerceQuote(2).Offers[3].Ask==26,TEXT("Shortage leaves other East goods and other markets unchanged"));
    Check(!E->ShortageAvailable(M->StartedAt()+780)&&E->ShortageAvailable(M->StartedAt()+779.999),TEXT("Scheduled shortage completion is strictly before 780 while the global order is still available"));
    auto Create=[&](ALDPlayerController* PC){auto* P=W->SpawnActor<ALDItem>();P->Kind=ELDItemKind::Toolkit;I->Adopt(PC,P,TEXT("DiagnosticItemCreation"));return P->Identity;};const auto ToolA=Create(A),ToolB=Create(B);Move(B);
    auto Plan=[&](ALDPlayerController* PC,FGuid Id){FLDProductionPlan P;P.Site=4;P.Recipe=5;auto* O=M->StateFor(PC);P.InventoryRevision=O->Inventory.Revision;P.Inputs.Add(*O->Inventory.Find(Id));return P;};
    auto Start=[&](ALDPlayerController* PC,FGuid Id){D->States.FindOrAdd(PC).Tokens=3;auto* O=M->StateFor(PC);const auto T=++Token;D->Queue(PC,O->Match,O->Life,T,Plan(PC,Id));D->Sample(W->GetTimeSeconds());return T;};
    auto Id=Start(A,ToolA);D->Release(A,OA->Life,Id);Check(I->Owned(A,ToolA)&&!I->Owned(A,ToolA)->bReserved&&E->Ledger.Remaining(TEXT("ShortageOrder"))==1,TEXT("Released shortage channel retains Toolkit and shared order quota"));
    const auto IA=Start(A,ToolA),IB=Start(B,ToolB);const int64 StartA=OA->Gold,StartB=OB->Gold,XA=OA->Xp,XB=OB->Xp;
    for(int32 N=0;N<31;++N){W->TimeSeconds+=.1;D->RefreshHold(A,OA->Life,IA,N+1,false);D->RefreshHold(B,OB->Life,IB,N+1,false);E->SampleInstitutions(W->GetTimeSeconds());D->Sample(W->GetTimeSeconds());}
    Check(E->Ledger.Remaining(TEXT("ShortageOrder"))==0&&G->ShortageRemaining==0&&OA->Gold+OB->Gold==StartA+StartB+40&&OA->Xp+OB->Xp==XA+XB+20,TEXT("Two accepted shortage channels race to one forty-gold/twenty-XP reward globally"));
    Check((I->Owned(A,ToolA)!=nullptr)+(I->Owned(B,ToolB)!=nullptr)==1&&OA->Commissions==0&&OB->Commissions==0,TEXT("Only winning Toolkit consumed; loser retains input and neither gets legacy credit/endorsement"));
    Check(E->ShortageActive(W->GetTimeSeconds())&&E->CommerceQuote(3).Offers[3].Ask==33,TEXT("Filling bonus order does not end shortage price premium"));
    At(779.999);const auto EndQuote=E->CommerceQuote(3);At(780);Check(G->ShortagePhase==2&&E->CommerceQuote(3).Offers[3].Ask==26&&E->CommerceQuote(3).Revision!=EndQuote.Revision,TEXT("Exactly at 780 price reverts with another quote revision"));
    const auto AfterOrder=E->Ledger.ReceiptCount();if(auto* Retained=I->Owned(A,ToolA)){E->CommitShortage(A,*OA->Inventory.Find(ToolA),Error);}else{E->CommitShortage(B,*OB->Inventory.Find(ToolB),Error);}Check(E->Ledger.ReceiptCount()==AfterOrder,TEXT("Expired/exhausted order cannot consume another Toolkit or mint again"));
    At(1440);Check(E->InstitutionCursor==8&&E->Ledger.Burned()==40&&E->Ledger.Remaining(TEXT("ProducerWest"))==0&&E->Ledger.Remaining(TEXT("ProducerEast"))==0,TEXT("All eight funded opportunities burn at most forty total gold and exhaust producer budgets"));
    Check(E->Ledger.Account(TEXT("WestTown"))->Gold==134&&E->Ledger.Account(TEXT("EastTown"))->Gold==126&&E->MarketItems(2,ELDItemKind::Fiber).Num()==18&&E->MarketItems(3,ELDItemKind::Ore).Num()==18,TEXT("Final market reserves and independently created/consumed physical stock reconcile"));
    const auto FinalCount=E->Ledger.ReceiptCount();At(1499);Check(E->Ledger.ReceiptCount()==FinalCount&&G->EconomyNextTick==0,TEXT("No ninth tick, hidden restock, refill or animation-triggered mutation"));
    // Explicit terminal fixture: no late scheduled work may commit after results.
    G->EndReason=2;At(1501);Check(E->bScheduleClosed&&E->Ledger.ReceiptCount()==FinalCount&&!E->ShortageActive(W->GetTimeSeconds()),TEXT("Terminal result closes all future institution/event commits"));
    Check(E->AuditFailures==0&&E->Audit(Error),TEXT("Whole B64 authority sequence reconciles actual wallets, items, budgets and receipts"));Check(E->Export(TEXT("B64Verified"),Error),TEXT("Exported B64 ledger for independent replay"));Finish(TEXT("Two local owners, controlled clock, labelled diagnostic stock/cash/item fixtures; no remote or human economy claim"));
#endif
}
