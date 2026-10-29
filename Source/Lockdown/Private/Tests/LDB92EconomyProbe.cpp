#include "Tests/LDB92EconomyProbe.h"
#include "Economy/LDB92Economy.h"
#include "Economy/LDB92EconomyContract.h"
#include "Framework/LDGameMode.h"
#include "Inventory/LDInventoryDirector.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include <limits>

ALDB92EconomyProbe::ALDB92EconomyProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDB92EconomyProbe::Check(bool Passed,const FString& Name)
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("case"),Name);Row->SetBoolField(TEXT("passed"),Passed);Checks.Add(MakeShared<FJsonValueObject>(Row));Failures+=!Passed;
    UE_LOG(LogTemp,Display,TEXT("LD_B92_ECONOMY_PROBE passed=%d %s"),Passed,*Name);
}
void ALDB92EconomyProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Done||GetWorld()->GetTimeSeconds()<2){return;}Done=true;
    FString Output,Error;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Output);
    const auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    Check(HasAuthority()&&GetNetMode()==NM_DedicatedServer&&Mode&&Mode->IsEnvironmentProfile()&&!Mode->Economy,TEXT("Isolated native authority; legacy economy is absent; no full-match admission claimed"));
    TArray<FGuid> Seats,Civilians;for(int32 I=0;I<24;++I){Seats.Add(FGuid::NewGuid());}for(int32 I=0;I<96;++I){Civilians.Add(FGuid::NewGuid());}
    auto* E=GetWorld()->SpawnActor<ALDB92Economy>();auto* Poor=GetWorld()->SpawnActor<ALDB92Economy>();auto* Late=GetWorld()->SpawnActor<ALDB92Economy>();
    Check(E&&Poor&&Late,TEXT("Allocate independent test domains"));
    if(E&&Poor&&Late)
    {
        const FGuid Match=FGuid::NewGuid();
        auto Duplicate=Seats;Duplicate[23]=Duplicate[0];
        Check(!E->Initialize(Match,Duplicate,Civilians,Error)&&E->Custody.IsEmpty(),TEXT("Duplicate roster identity rejects before any allocation"));
        Duplicate=Seats;Duplicate[23]=Civilians[0];
        Check(!E->Initialize(Match,Duplicate,Civilians,Error),TEXT("A civilian cannot also own a contender endowment"));
        Duplicate=Seats;Duplicate.Pop();Check(!E->Initialize(Match,Duplicate,Civilians,Error),TEXT("23 seats cannot shrink fixed N budgets"));
        Check(E->Initialize(Match,Seats,Civilians,Error),TEXT("Initialize selected full-profile economics once"));
        if(!E->Ready){UE_LOG(LogTemp,Error,TEXT("LD_B92_INITIALIZATION %s"),*Error);}
        else
        {
            Check(!E->GetIsReplicated()&&!E->PrimaryActorTick.bCanEverTick,TEXT("Economy stays authority-only and has one explicit clock"));
            Check(E->Ledger.TotalGold()==3288&&E->Ledger.Minted()==0&&E->Ledger.Burned()==0,TEXT("Initial cash exactly 3000 market plus 288 civilian; no contender grant"));
            Check(E->Ledger.AccountRecords().Num()==149&&E->Seats.Num()==24,TEXT("Five market, 96 civilian, 24 wallet and 24 shared bank accounting identities"));
            Check(E->Custody.Num()==426&&E->Ledger.ItemRecords().Num()==426,TEXT("426 distinct physical commodity stock instances; no common-cache or rare source clone"));
            bool Wallets=true,Bounds=true;
            for(FGuid Id:Seats)
            {
                Wallets&=E->Ledger.Account(E->Wallet(Id))->Gold==0&&E->Ledger.Account(E->Bank(Id))->Gold==0;
                const FString S=Id.ToString(EGuidFormats::Digits);
                Bounds&=E->Ledger.Remaining(FName(*(TEXT("Courier:")+S)))==20&&E->Ledger.Remaining(FName(*(TEXT("CourierIssuance:")+S)))==40;
            }
            Check(Wallets,TEXT("All 24 contenders have zero wallet/bank endowment"));
            Check(Bounds&&E->Ledger.Remaining(TEXT("CoreOrders"))==72&&E->Ledger.Remaining(TEXT("SupplyOrders"))==24,TEXT("20 paid/40 issued courier and 72/24 global procurement budgets fixed to 24"));
            const FString Before=E->Ledger.ExportJson(TEXT("Before"));
            Check(!E->Initialize(FGuid::NewGuid(),Seats,Civilians,Error)&&E->Ledger.ExportJson(TEXT("Before"))==Before,TEXT("Repeated initialization cannot refill cash, stock, identities or budgets"));
            auto Request=[&](ALDB92Economy* D,int32 Seat,FName Market,ELDItemKind Kind,bool Buy,FGuid Item=FGuid())
            {
                FLDB92Trade R;R.Request=FGuid::NewGuid();R.Contender=Seats[Seat];R.Market=Market;R.Kind=Kind;R.Buy=Buy;R.Item=Item;
                const auto Q=D->Quote(Market);R.QuoteRevision=Q.Revision;R.WalletRevision=D->Ledger.Account(D->Wallet(R.Contender))->Revision;
                for(const auto& G:Q.Goods){if(G.Kind==Kind){R.Price=Buy?G.Ask:G.Bid;}}
                for(auto P:D->Custody){if(P->Identity==Item){R.ItemRevision=P->Revision;}}return R;
            };
            FGuid Received;
            auto Zero=Request(E,0,TEXT("capital"),ELDItemKind::Fiber,true);
            Check(!E->Trade(Zero,Received,Error)&&E->Ledger.ExportJson(TEXT("Before"))==Before,TEXT("Zero-wallet purchase rejects without mutations"));
            auto Fund=[&](ALDB92Economy* D,FName From,FName To,int64 Amount)
            {
                // Funding fixture transfers existing reserves; never passed off as
                // player earnings, work completion, theft or a new gold source.
                LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("DiagnosticFixtureTransfer");
                T.Gold={{From,D->Ledger.Account(From)->Revision,-Amount},{To,D->Ledger.Account(To)->Revision,Amount}};
                return D->Ledger.Commit(T,Error);
            };
            const TArray<FName> Markets={TEXT("capital"),TEXT("highfort"),TEXT("scholars"),TEXT("riverport"),TEXT("ironworks")};
            const int32 FiberAsk[]={4,8,6,4,8};
            for(int32 I=0;I<5;++I)
            {
                const FName Market=Markets[I];const FString Prefix=Market.ToString()+TEXT(": ");
                const auto Initial=E->Quote(Market);const auto* Fiber=Initial.Goods.FindByPredicate([](const auto& G){return G.Kind==ELDItemKind::Fiber;});
                Check(Initial.Cash==600&&Fiber&&Fiber->Ask==FiberAsk[I],Prefix+TEXT("exact initial pool and unit quote"));
                Check(Fund(E,Market,E->Wallet(Seats[I]),100),Prefix+TEXT("diagnostic funding transfers 100 existing gold"));
                auto Buy=Request(E,I,Market,ELDItemKind::Fiber,true);const uint64 Other=E->Quote(Markets[(I+1)%5]).Revision;
                Check(E->Trade(Buy,Received,Error)&&Received.IsValid(),Prefix+TEXT("native Buy transfers one real stock identity"));
                const FGuid Bought=Received;const auto* Item=E->Ledger.Item(Bought);const FGuid Origin=Item?Item->Origin:FGuid();
                Check(Item&&Item->Location==E->Wallet(Seats[I])&&!Item->Consumed&&Origin.IsValid(),Prefix+TEXT("item lineage and account custody retained"));
                const int32 Count=E->Ledger.ReceiptCount();FGuid Replay;
                Check(E->Trade(Buy,Replay,Error)&&Replay==Bought&&E->Ledger.ReceiptCount()==Count,Prefix+TEXT("identical confirmation replays without a second item or debit"));
                auto Changed=Buy;++Changed.Price;Check(!E->Trade(Changed,Replay,Error)&&E->Ledger.ReceiptCount()==Count,Prefix+TEXT("altered replay terms reject"));
                Changed=Buy;Changed.Request=FGuid::NewGuid();Changed.WalletRevision=E->Ledger.Account(E->Wallet(Seats[I]))->Revision;
                Check(!E->Trade(Changed,Replay,Error)&&E->Ledger.ReceiptCount()==Count,Prefix+TEXT("stale pool quote rejects with current wallet"));
                Check(E->Quote(Markets[(I+1)%5]).Revision==Other,Prefix+TEXT("trade does not invalidate another pool"));
                auto Sell=Request(E,I,Market,ELDItemKind::Fiber,false,Bought);auto Stale=Sell;++Stale.ItemRevision;
                Check(!E->Trade(Stale,Replay,Error),Prefix+TEXT("stale physical item revision rejects"));
                Check(Sell.Price<Buy.Price&&E->Trade(Sell,Replay,Error)&&E->Ledger.Item(Bought)->Origin==Origin&&E->Ledger.Item(Bought)->Location==Market,Prefix+TEXT("Sell returns same instance without buy/sell profit or lineage reset"));
            }
            auto A=Request(E,0,TEXT("capital"),ELDItemKind::Fiber,true),B=Request(E,1,TEXT("capital"),ELDItemKind::Fiber,true);
            Check(E->Trade(A,Received,Error),TEXT("First contender wins a current pool quote"));
            const int32 Receipts=E->Ledger.ReceiptCount();
            Check(!E->Trade(B,Received,Error)&&E->Ledger.ReceiptCount()==Receipts,TEXT("Second authority-stack contender with same quote loses atomically"));
            auto Forbidden=Request(E,0,TEXT("capital"),ELDItemKind::Book,true);
            Check(!E->Trade(Forbidden,Received,Error),TEXT("Commodity market cannot create or buy back an unlisted book"));
            Forbidden=A;Forbidden.Request=FGuid::NewGuid();Forbidden.Contender=Civilians[0];Check(!E->Trade(Forbidden,Received,Error),TEXT("Civilian accounting identity is not a contender commerce permit"));
            Check(E->Ledger.TotalGold()==3288&&E->Ledger.Minted()==0&&E->Audit(Error),TEXT("All five Buy/Sell pools conserve every physical identity and all cash"));
            const uint64 ShortageBefore=E->Quote(TEXT("ironworks")).Revision;
            for(int32 Tick=1;Tick<=29;++Tick)
            {
                Check(E->Sample(180.*Tick,Error),FString::Printf(TEXT("Scheduled opportunity %d at %d seconds"),Tick,Tick*180));
                if(Tick==10){Check(E->Quote(TEXT("ironworks")).Revision%4==1&&E->Quote(TEXT("ironworks")).Revision!=ShortageBefore,TEXT("1800s shortage has its own quote revision"));}
                if(Tick==11){Check(E->Quote(TEXT("ironworks")).Revision%4==2,TEXT("1980s shortage expires at the exclusive endpoint"));}
            }
            Check(E->Events.Num()==145&&E->Cursor==29,TEXT("Exactly 29 opportunities for each of five rows"));
            int32 Produced=0,Consumed=0,Empty=0;
            for(const auto& V:E->Events){Produced+=V.Row<2&&V.Status==TEXT("Committed");Consumed+=V.Row>=2&&V.Status==TEXT("Committed");Empty+=V.Status==TEXT("Empty");}
            Check(Produced==58&&Consumed==64&&Empty==23,TEXT("58 funded creations and 64 actual consumes; 23 empty Capital Toolkit skips"));
            Check(E->Ledger.Burned()==145&&E->Ledger.Minted()==0&&E->Ledger.TotalGold()==3143,TEXT("Full schedule burns exactly 145, never mints or subsidizes reserves"));
            Check(E->Custody.Num()==420&&E->Audit(Error),TEXT("426 initial plus 58 created minus 64 consumed equals 420 exact physical items"));
            Check(E->Sample(5220,Error)&&E->Events.Num()==145&&E->Ledger.Burned()==145,TEXT("Repeated sample cannot repeat scheduled work"));
            Check(E->Sample(5400,Error)&&!E->Quote(TEXT("capital")).Revision,TEXT("5400s closes trade and scheduled institutions"));
            Check(!E->Trade(Request(E,0,TEXT("capital"),ELDItemKind::Fiber,true),Received,Error),TEXT("Fresh trade after match deadline rejects"));
            Check(E->Audit(Error),TEXT("Native final full-schedule receipt replay closes"));
        }
        auto MoveStock=[&](ALDB92Economy* D,FName From,FName To,ELDItemKind Kind,int32 Count)
        {
            auto Stock=D->Stock(From,Kind);if(Stock.Num()<Count){return false;}
            LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("DiagnosticFixtureStockTransfer");
            for(int32 I=0;I<Count;++I){const auto* Old=D->Ledger.Item(Stock[I]->Identity);LDEconomy::FItemDelta Change;Change.Id=Stock[I]->Identity;Change.Kind=Kind;Change.Op=LDEconomy::EItemOp::Transfer;Change.From=From;Change.To=To;Change.Expected=Old->Revision;T.Items.Add(Change);}
            if(!D->Ledger.Commit(T,Error)){return false;}for(int32 I=0;I<Count;++I){Stock[I]->Institution=To;++Stock[I]->Revision;}return true;
        };
        Check(Poor->Initialize(FGuid::NewGuid(),Seats,Civilians,Error),TEXT("Independent poor/full/empty institution fixture initializes"));
        if(Poor->Ready)
        {
            LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("DiagnosticFixtureTransfer");T.Gold={{TEXT("riverport"),1,-600},{Poor->Wallet(Seats[0]),1,600}};
            Check(Poor->Ledger.Commit(T,Error)&&MoveStock(Poor,TEXT("highfort"),TEXT("ironworks"),ELDItemKind::Ore,54)&&MoveStock(Poor,TEXT("capital"),Poor->Wallet(Seats[1]),ELDItemKind::Toolkit,6),TEXT("Fixtures move existing money/stock; no deletion, creation or unearned reward"));
            Check(Poor->Sample(180,Error)&&Poor->Events.Num()==5&&Poor->Events[0].Status==TEXT("InsufficientCash")&&Poor->Events[1].Status==TEXT("StockFull")&&Poor->Events[4].Status==TEXT("Empty"),TEXT("Poor, full and empty endpoints skip independently before consumption"));
            Check(Poor->Ledger.Burned()==0&&Poor->Custody.Num()==424&&Poor->Audit(Error),TEXT("Skipped producers spend nothing; two real consumption identities destroyed"));
            T.Id=FGuid::NewGuid();T.Gold={{Poor->Wallet(Seats[0]),2,-600},{TEXT("riverport"),2,600}};
            Check(Poor->Ledger.Commit(T,Error)&&Poor->Sample(180,Error)&&Poor->Ledger.Burned()==0,TEXT("Refill cannot retry a skipped opportunity within its interval"));
            Check(Poor->Sample(360,Error)&&Poor->Ledger.Burned()==5&&Poor->Events.Num()==10&&Poor->Audit(Error),TEXT("Only the next opportunity may produce after funds/capacity return"));
        }
        Check(Late->Initialize(FGuid::NewGuid(),Seats,Civilians,Error),TEXT("Independent late-wake clock fixture initializes"));
        if(Late->Ready)
        {
            Check(Late->Sample(540,Error)&&Late->Events.Num()==15&&Late->Ledger.Burned()==5,TEXT("Late wake discards 180/360s opportunities; no compressed catch-up production"));
            int32 Missed=0;for(const auto& Event:Late->Events){Missed+=Event.Status==TEXT("Missed");}Check(Missed==10,TEXT("Every missed row remains explicit in the timeline"));
            Check(!Late->Sample(180,Error)&&!Late->Sample(std::numeric_limits<double>::quiet_NaN(),Error),TEXT("Backward and nonfinite authority clocks reject"));
            Check(Late->Sample(5400,Error)&&Late->Ledger.Burned()==5&&Late->Audit(Error),TEXT("Deadline cannot replay offline opportunities or reset receipts"));
        }
        for(auto Pair:TArray<TPair<FString,ALDB92Economy*>>{{TEXT("Schedule"),E},{TEXT("Poor"),Poor},{TEXT("Late"),Late}})
        {
            if(!Pair.Value->Ready){continue;}
            Check(FFileHelper::SaveStringToFile(Pair.Value->Ledger.ExportJson(TEXT("B92AuthorityFoundation")),*(Output/(Pair.Key+TEXT("Ledger.json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),Pair.Key+TEXT(" ledger saved"));
            TArray<TSharedPtr<FJsonValue>> Timeline;
            for(const auto& V:Pair.Value->Events){auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("at"),V.At);J->SetNumberField(TEXT("row"),V.Row);J->SetStringField(TEXT("status"),V.Status.ToString());J->SetStringField(TEXT("receipt"),V.Receipt.ToString());J->SetStringField(TEXT("item"),V.Item.ToString());Timeline.Add(MakeShared<FJsonValueObject>(J));}
            auto J=MakeShared<FJsonObject>();J->SetArrayField(TEXT("events"),Timeline);FString Json;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));
            Check(FFileHelper::SaveStringToFile(Json,*(Output/(Pair.Key+TEXT("Timeline.json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),Pair.Key+TEXT(" timeline saved"));
        }
    }
    auto Result=MakeShared<FJsonObject>();Result->SetStringField(TEXT("stage"),TEXT("B92"));Result->SetStringField(TEXT("scope"),TEXT("isolated_native_economy_authority"));Result->SetStringField(TEXT("supplied_economy_sha256"),LDB92Contract::Sha256);
    Result->SetBoolField(TEXT("completed"),true);Result->SetBoolField(TEXT("full_stage_acceptance"),false);Result->SetNumberField(TEXT("failures"),Failures);Result->SetArrayField(TEXT("checks"),Checks);
    FString Json;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));const bool Saved=FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Economy.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FPlatformMisc::RequestExitWithStatus(false,Failures||!Saved?3:0);
#endif
}
