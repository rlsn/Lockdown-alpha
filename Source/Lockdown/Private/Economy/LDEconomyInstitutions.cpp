#include "Economy/LDEconomyDirector.h"
#include "Economy/LDInstitutionSite.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Engine/World.h"

void ALDEconomyDirector::EnsureInstitutionSites()
{
    if(!HasAuthority()||!Initialized){return;}InstitutionSites.SetNum(5);
    for(uint8 Site=0;Site<5;++Site){if(IsValid(InstitutionSites[Site])){continue;}auto* A=GetWorld()->SpawnActor<ALDInstitutionSite>();if(A){A->Site=Site;A->SetActorLocation(A->Position(GetWorld(),Site));InstitutionSites[Site]=A;}}
}
bool ALDEconomyDirector::ShortageActive(double Now) const
{const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const double Elapsed=M?Now-M->StartedAt():-1;return Initialized&&M&&M->IsPlaying()&&Elapsed>=Catalog.ShortageStart&&Elapsed<Catalog.ShortageEnd;}
bool ALDEconomyDirector::ShortageAvailable(double CompletionAt) const
{
    const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* G=GetWorld()->GetGameState<ALDGameState>();const double Now=GetWorld()->GetTimeSeconds();
    return ShortageActive(Now)&&M&&G&&Now<G->MatchDeadline&&CompletionAt>=Now&&CompletionAt-M->StartedAt()<Catalog.ShortageEnd&&Ledger.Remaining(TEXT("ShortageOrder"))==1;
}
uint8 ALDEconomyDirector::ApplyInstitution(uint8 Row,int32 Tick,double Now,FString& Error)
{
    const auto& Rule=Catalog.Institutions[Row];const FName Market=MarketId(Rule.Service);const auto* Account=Ledger.Account(Market);auto Stock=MarketItems(Rule.Service,Rule.Kind);
    const auto* Def=Catalog.Markets.FindByPredicate([&](const auto& M){return M.Id==Market;});const auto* Good=Def?Def->Goods.FindByPredicate([&](const auto& G){return G.Kind==Rule.Kind;}):nullptr;
    if(!Account||!Good||MarketRevisions.FindRef(Rule.Service)>=MAX_uint32-4){Error=TEXT("Invalid institution state/revision");return 5;}
    LDEconomy::FTransaction T;T.Id=FGuid(ScheduleId.A,ScheduleId.B,uint32(Tick+1),uint32(Row+1));T.Cause=Rule.Producer?TEXT("InstitutionProduction"):TEXT("InstitutionConsumption");
    if(Ledger.Receipt(T.Id)){Error=TEXT("Scheduled receipt already committed");return 0;}
    ALDItem* Physical=nullptr;
    if(Rule.Producer)
    {
        if(Stock.Num()>=Good->Cap){return 4;}if(Account->Gold<Rule.Cost){return 3;}
        T.Budget=Rule.Service==2?TEXT("ProducerWest"):TEXT("ProducerEast");T.Units=1;if(Ledger.Remaining(T.Budget)<=0){return 0;}
#if !UE_BUILD_SHIPPING
        if(bFailNextInstitution&&AllowDiagnosticItems()){bFailNextInstitution=false;return 5;}
#endif
        Physical=GetWorld()->SpawnActor<ALDItem>();if(!Physical){return 5;}Physical->Kind=Rule.Kind;
        T.Burn=Rule.Cost;T.Gold.Add({Market,Account->Revision,-int64(Rule.Cost)});LDEconomy::FItemDelta D;D.Id=Physical->Identity;D.Kind=Rule.Kind;D.Op=LDEconomy::EItemOp::Create;D.To=Market;T.Items.Add(D);
    }
    else
    {
        if(Stock.IsEmpty()){return 2;}Physical=Stock[0];const auto* I=Ledger.Item(Physical->Identity);if(!I||I->Consumed){Error=TEXT("Institution stock missing causal record");return 5;}
        LDEconomy::FItemDelta D;D.Id=Physical->Identity;D.Kind=Rule.Kind;D.Op=LDEconomy::EItemOp::Consume;D.From=Market;D.Expected=I->Revision;T.Items.Add(D);
    }
    if(!Ledger.Commit(T,Error)){if(Rule.Producer){Physical->Destroy();}return 5;}
    if(Rule.Producer){Physical->State=1;Physical->Holder=this;Physical->Institution=Market;Physical->OriginReceipt=T.Id;InstitutionalItems.Add(Physical);}
    else{Physical->State=2;Physical->Holder.Reset();InstitutionalItems.Remove(Physical);Physical->Destroy();}
    ++MarketRevisions.FindChecked(Rule.Service);
    UE_LOG(LogTemp,Display,TEXT("LD_INSTITUTION_RECEIPT scheduled=%d sampled=%.3f row=%u receipt=%s cause=%s item=%s burn=%d"),Catalog.InstitutionTimes[Tick],Now,Row,*T.Id.ToString(),*T.Cause.ToString(),*T.Items[0].Id.ToString(),Rule.Cost);return 1;
}
void ALDEconomyDirector::SampleInstitutions(double Now)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=GetWorld()->GetGameState<ALDGameState>();if(!HasAuthority()||!Initialized||!M||!G){return;}
    if(bScheduleClosed||!M->IsPlaying()||Now>=G->MatchDeadline)
    {if(G->bMatchStarted){bScheduleClosed=true;G->EconomyNextTick=0;G->ForceNetUpdate();}return;}
    EnsureInstitutionSites();const double Elapsed=Now-M->StartedAt();bool Changed=false;
    if(G->ShortagePhase==0&&Elapsed>=Catalog.ShortageStart){G->ShortagePhase=1;Changed=true;UE_LOG(LogTemp,Display,TEXT("LD_SHORTAGE_OPEN EastTown Toolkit 5/4; one shared Toolkit order 40 gold / 20 XP; closes at %d"),Catalog.ShortageEnd);}
    if(G->ShortagePhase==1&&Elapsed>=Catalog.ShortageEnd){G->ShortagePhase=2;Changed=true;UE_LOG(LogTemp,Display,TEXT("LD_SHORTAGE_CLOSED expired; prices normal; remaining=%d"),Ledger.Remaining(TEXT("ShortageOrder")));}
    while(InstitutionCursor<Catalog.InstitutionTimes.Num()&&Elapsed>=Catalog.InstitutionTimes[InstitutionCursor])
    {
        const int32 Tick=InstitutionCursor++;Changed=true;
        // Every due opportunity is consumed once, including skips. No skipped
        // stock/cash obligation is carried to another tick; producers precede demand.
        for(uint8 Row=0;Row<Catalog.Institutions.Num();++Row)
        {
            FString Error;const uint8 Status=ApplyInstitution(Row,Tick,Now,Error);const uint8 Site=Row<2?Row+3:Row==4?0:Row-1;
            if(IsValid(InstitutionSites[Site])){auto* A=InstitutionSites[Site].Get();A->Status=Status;A->WorkUntil=Status==1?Now+3:0;}
            UE_LOG(LogTemp,Display,TEXT("LD_INSTITUTION_TICK tick=%d row=%u status=%s detail=%s"),Catalog.InstitutionTimes[Tick],Row,*ALDInstitutionSite::StatusName(Status),*Error);
        }
    }
    const float Next=InstitutionCursor<Catalog.InstitutionTimes.Num()?float(M->StartedAt()+Catalog.InstitutionTimes[InstitutionCursor]):0;
    if(G->EconomyNextTick!=Next){G->EconomyNextTick=Next;Changed=true;}const uint8 Remaining=uint8(FMath::Max(0,Ledger.Remaining(TEXT("ShortageOrder"))));if(G->ShortageRemaining!=Remaining){G->ShortageRemaining=Remaining;Changed=true;}
    if(Changed){G->ForceNetUpdate();FString Error;if(!Audit(Error)){++AuditFailures;UE_LOG(LogTemp,Error,TEXT("LD_INSTITUTION_AUDIT %s"),*Error);}}
}
bool ALDEconomyDirector::CommitShortage(ALDPlayerController* PC,const FLDInventoryItem& Input,FString& Error)
{
    auto Reject=[&](const TCHAR* Why){Error=Why;return false;};auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(PC):nullptr;const double Now=GetWorld()->GetTimeSeconds();
    if(!O||!O->bActive||!ShortageAvailable(Now)){return Reject(TEXT("Shortage order exhausted or expired; input retained"));}
    auto* Physical=M->Inventory->Owned(PC,Input.Id);const auto* Own=O->Inventory.Find(Input.Id);const FName W=Wallet(M->ContenderId(PC));const auto* A=Ledger.Account(W);const auto* Item=Ledger.Item(Input.Id);
    if(Input.Kind!=ELDItemKind::Toolkit||!Physical||!Own||Physical->Kind!=ELDItemKind::Toolkit||!Physical->bReserved||!Own->bReserved||Physical->Revision!=Input.Revision||Own->Revision!=Input.Revision||!Item||Item->Consumed||Item->Location!=W||!A||A->Gold!=O->Gold||O->Gold>MAX_int64-Catalog.ShortageGold){return Reject(TEXT("Shortage input/wallet changed"));}
    if(!Audit(Error)){return false;}LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("ShortageOrder");T.Mint=Catalog.ShortageGold;T.Budget=TEXT("ShortageOrder");T.Units=1;T.Gold.Add({W,A->Revision,Catalog.ShortageGold});LDEconomy::FItemDelta D;D.Id=Input.Id;D.Kind=Input.Kind;D.Op=LDEconomy::EItemOp::Consume;D.From=W;D.Expected=Item->Revision;T.Items.Add(D);
    if(!Ledger.Commit(T,Error)){return false;}LDInventory::Remove(O->Inventory,Input.Id);Physical->State=2;Physical->Holder.Reset();Physical->bReserved=false;Physical->Destroy();O->Gold=Ledger.Account(W)->Gold;GetWorld()->GetGameState<ALDGameState>()->ShortageRemaining=0;GetWorld()->GetGameState<ALDGameState>()->ForceNetUpdate();M->Inventory->Changed(PC);M->AwardExperience(PC,Catalog.ShortageXp);
    UE_LOG(LogTemp,Display,TEXT("LD_SHORTAGE_RECEIPT receipt=%s item=%s mint=%d xp=%d"),*T.Id.ToString(),*Input.Id.ToString(),Catalog.ShortageGold,Catalog.ShortageXp);return true;
}
