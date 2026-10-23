#include "Economy/LDEconomyLedger.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    bool Bounded(int64 N){return N>=0&&N<=LDEconomy::MaxBalance;}
    bool Add(int64 A,int64 B,int64& Out)
    {if((B>0&&A>MAX_int64-B)||(B<0&&A<MIN_int64-B)){return false;}Out=A+B;return true;}
    bool Same(const LDEconomy::FTransaction& A,const LDEconomy::FTransaction& B)
    {
        if(A.Id!=B.Id||A.Cause!=B.Cause||A.Mint!=B.Mint||A.Burn!=B.Burn||A.Budget!=B.Budget||A.Units!=B.Units||A.Gold.Num()!=B.Gold.Num()||A.Items.Num()!=B.Items.Num()){return false;}
        for(int32 I=0;I<A.Gold.Num();++I){const auto& X=A.Gold[I];const auto& Y=B.Gold[I];if(X.Account!=Y.Account||X.Expected!=Y.Expected||X.Delta!=Y.Delta){return false;}}
        for(int32 I=0;I<A.Items.Num();++I){const auto& X=A.Items[I];const auto& Y=B.Items[I];if(X.Id!=Y.Id||X.Op!=Y.Op||X.Kind!=Y.Kind||X.Expected!=Y.Expected||X.From!=Y.From||X.To!=Y.To||X.Parents!=Y.Parents){return false;}}
        return true;
    }
}
bool LDEconomy::FLedger::Initialize(FGuid Epoch,const TMap<FName,int64>& Initial,const TMap<FName,int32>& Limits,FString& Error)
{
    Error.Reset();if(Match.IsValid()||!Epoch.IsValid()){Error=TEXT("Ledger already initialized or invalid match");return false;}
    int64 Total=0;
    for(const auto& P:Initial){if(P.Key.IsNone()||!Bounded(P.Value)||Total>MaxBalance-P.Value){Error=TEXT("Invalid endowment");return false;}Total+=P.Value;}
    for(const auto& P:Limits){if(P.Key.IsNone()||P.Value<0){Error=TEXT("Invalid finite budget");return false;}}
    Match=Epoch;Start=Total;Budgets=Limits;InitialAccounts=Initial;InitialBudgets=Limits;
    for(const auto& P:Initial){FAccount A;A.Gold=P.Value;Accounts.Add(P.Key,A);}return true;
}
bool LDEconomy::FLedger::OpenAccount(FName Id)
{if(!Match.IsValid()||Id.IsNone()){return false;}if(!Accounts.Contains(Id)){Accounts.Add(Id,FAccount());}return true;}
bool LDEconomy::FLedger::EndowAccount(FName Id,int64 Gold)
{if(!Match.IsValid()||Id.IsNone()||Accounts.Contains(Id)||!Bounded(Gold)||Start>MaxBalance-Gold||Start+Gold>MaxBalance-Mints||TotalGold()>MaxBalance-Gold){return false;}FAccount A;A.Gold=Gold;Accounts.Add(Id,A);InitialAccounts.Add(Id,Gold);Start+=Gold;return true;}
bool LDEconomy::FLedger::Retire(FName Id)
{auto* A=Accounts.Find(Id);if(!A){return false;}if(!A->Retired){if(A->Revision==MAX_uint32){return false;}A->Retired=true;++A->Revision;}return true;}
int64 LDEconomy::FLedger::TotalGold() const
{int64 Sum=0;for(const auto& P:Accounts){if(!Bounded(P.Value.Gold)||Sum>MaxBalance-P.Value.Gold){return -1;}Sum+=P.Value.Gold;}return Sum;}
bool LDEconomy::FLedger::Commit(const FTransaction& T,FString& Error)
{
    Error.Reset();auto Reject=[&](const TCHAR* Why){Error=Why;return false;};
    if(!Match.IsValid()||!T.Id.IsValid()||T.Cause.IsNone()){return Reject(TEXT("Invalid transaction identity"));}
    if(const auto* R=Receipts.Find(T.Id)){return Same(R->Transaction,T)?true:Reject(TEXT("Receipt identity reused with different terms"));}
    if(!Bounded(T.Mint)||!Bounded(T.Burn)||Mints>MaxBalance-T.Mint||Burns>MaxBalance-T.Burn||Start>MaxBalance-(Mints+T.Mint)){return Reject(TEXT("Invalid gold classification"));}
    if(T.Budget.IsNone()!= (T.Units==0)||T.Units<0){return Reject(TEXT("Invalid budget debit"));}
    if(!T.Budget.IsNone()){const auto* N=Budgets.Find(T.Budget);if(!N||*N<T.Units){return Reject(TEXT("Finite budget exhausted"));}}
    TSet<FName> SeenAccounts;int64 Net=0;
    for(const auto& D:T.Gold)
    {
        const auto* A=Accounts.Find(D.Account);int64 After=0;
        if(!A||A->Retired||SeenAccounts.Contains(D.Account)||A->Revision!=D.Expected||A->Revision==MAX_uint32||!Add(A->Gold,D.Delta,After)||!Bounded(After)){return Reject(TEXT("Stale, retired or insufficient account"));}
        SeenAccounts.Add(D.Account);
        // Bounded intermediate sums also reject pathological server requests.
        int64 Next=0;if(!Add(Net,D.Delta,Next)){return Reject(TEXT("Gold delta overflow"));}Net=Next;
    }
    const int64 Total=TotalGold();
    int64 Final=0;if(Net!=T.Mint-T.Burn||Total<0||!Add(Total,Net,Final)||!Bounded(Final)){return Reject(TEXT("Gold deltas do not reconcile"));}
    TSet<FGuid> SeenItems,Consumed;
    for(const auto& D:T.Items){if(D.Op==EItemOp::Consume){Consumed.Add(D.Id);}}
    for(const auto& D:T.Items)
    {
        if(!D.Id.IsValid()||SeenItems.Contains(D.Id)||LDInventory::UnitWeight(D.Kind)<0){return Reject(TEXT("Invalid or duplicate item transition"));}SeenItems.Add(D.Id);
        const auto* I=Items.Find(D.Id);
        if(D.Op==EItemOp::Create)
        {
            if(I||D.Expected||!D.From.IsNone()||D.To.IsNone()){return Reject(TEXT("Item creation identity already exists or invalid location"));}
            TSet<FGuid> Parents;
            for(auto Parent:D.Parents){const auto* P=Items.Find(Parent);if(!P||P->Consumed||!Consumed.Contains(Parent)||Parents.Contains(Parent)){return Reject(TEXT("Output parents must be distinct inputs consumed in this receipt"));}Parents.Add(Parent);}
        }
        else
        {
            if((D.Op!=EItemOp::Transfer&&D.Op!=EItemOp::Consume)||!I||I->Consumed||I->Kind!=D.Kind||I->Location!=D.From||I->Revision!=D.Expected||I->Revision==MAX_uint32||!D.Parents.IsEmpty()){return Reject(TEXT("Item ownership or revision changed"));}
            if(D.Op==EItemOp::Transfer?(D.To.IsNone()||D.To==D.From):!D.To.IsNone()){return Reject(TEXT("Invalid item destination"));}
        }
    }
    // All validation completes before any mutation. Allocations of physical output
    // actors must be completed by the caller before committing this domain receipt.
    for(const auto& D:T.Gold){auto& A=Accounts.FindChecked(D.Account);A.Gold+=D.Delta;++A.Revision;}
    for(const auto& D:T.Items)
    {
        if(D.Op==EItemOp::Create){FItem I;I.Kind=D.Kind;I.Location=D.To;I.Origin=T.Id;I.Parents=D.Parents;Items.Add(D.Id,MoveTemp(I));}
        else{auto& I=Items.FindChecked(D.Id);I.Location=D.To;I.Consumed=D.Op==EItemOp::Consume;++I.Revision;}
    }
    if(!T.Budget.IsNone()){Budgets.FindChecked(T.Budget)-=T.Units;}Mints+=T.Mint;Burns+=T.Burn;
    FReceipt R;R.Transaction=T;R.Serial=uint64(Receipts.Num())+1;Receipts.Add(T.Id,MoveTemp(R));return true;
}
bool LDEconomy::FLedger::Reconcile(FString& Error) const
{
    Error.Reset();if(!Match.IsValid()){Error=TEXT("Ledger not initialized");return false;}
    // Replay causal history independently, checking every revision and item lineage.
    FLedger Replay;if(!Replay.Initialize(Match,InitialAccounts,InitialBudgets,Error)){return false;}
    for(const auto& P:Accounts){Replay.OpenAccount(P.Key);}
    TArray<const FReceipt*> Ordered;for(const auto& P:Receipts){Ordered.Add(&P.Value);}
    Ordered.Sort([](const FReceipt& A,const FReceipt& B){return A.Serial<B.Serial;});
    for(const auto* R:Ordered){if(!Replay.Commit(R->Transaction,Error)){return false;}}
    for(const auto& P:Accounts){if(P.Value.Retired){Replay.Retire(P.Key);}const auto* A=Replay.Account(P.Key);if(!A||A->Gold!=P.Value.Gold||A->Revision!=P.Value.Revision||A->Retired!=P.Value.Retired){Error=TEXT("Account replay mismatch");return false;}}
    for(const auto& P:Budgets){if(Replay.Remaining(P.Key)!=P.Value){Error=TEXT("Finite budget replay mismatch");return false;}}
    if(TotalGold()<0||Start+Mints-Burns!=TotalGold()||Replay.TotalGold()!=TotalGold()||Replay.Mints!=Mints||Replay.Burns!=Burns||Replay.Items.Num()!=Items.Num()){Error=TEXT("Aggregate conservation mismatch");return false;}
    for(const auto& P:Items){const auto* I=Replay.Items.Find(P.Key);const auto& J=P.Value;if(!I||I->Kind!=J.Kind||I->Location!=J.Location||I->Revision!=J.Revision||I->Origin!=J.Origin||I->Parents!=J.Parents||I->Consumed!=J.Consumed){Error=TEXT("Item lineage mismatch");return false;}}
    return true;
}
int64 LDEconomy::DepositFee(int64 Gross)
{return Gross>0&&Gross<=MaxBalance?Gross/50+(Gross%50!=0):-1;}
bool LDEconomy::BankTransaction(const FLedger& L,FGuid Request,FName Wallet,FName Bank,uint32 WR,uint32 BR,int64 Amount,bool Deposit,FTransaction& Out,FString& Error)
{
    Error.Reset();const auto* W=L.Account(Wallet);const auto* B=L.Account(Bank);const int64 Fee=Deposit?DepositFee(Amount):0;
    if(!Request.IsValid()||Wallet==Bank||!W||!B||W->Retired||B->Retired||WR!=W->Revision||BR!=B->Revision||Amount<=0||Amount>MaxBalance||Fee<0||(Deposit&&Amount<=Fee)) {Error=TEXT("Bank terms changed or invalid amount");return false;}
    const int64 WD=Deposit?-Amount:Amount,BD=Deposit?Amount-Fee:-Amount;
    int64 WalletAfter=0,BankAfter=0;if(!Add(W->Gold,WD,WalletAfter)||!Add(B->Gold,BD,BankAfter)||!Bounded(WalletAfter)||!Bounded(BankAfter)){Error=TEXT("Insufficient funds or bank overflow");return false;}
    FTransaction T;T.Id=Request;T.Cause=Deposit?TEXT("BankDeposit"):TEXT("BankWithdrawal");T.Burn=Fee;T.Gold={{Wallet,WR,WD},{Bank,BR,BD}};Out=MoveTemp(T);return true;
}

FString LDEconomy::FLedger::ExportJson(FName Event) const
{
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("schemaVersion"),1);Root->SetStringField(TEXT("event"),Event.ToString());Root->SetStringField(TEXT("match"),Match.ToString());
    auto Number=[](const TSharedRef<FJsonObject>& O,const TCHAR* Key,int64 N){O->SetStringField(Key,LexToString(N));};
    Number(Root,TEXT("start"),Start);Number(Root,TEXT("minted"),Mints);Number(Root,TEXT("burned"),Burns);Number(Root,TEXT("end"),TotalGold());
    TArray<TSharedPtr<FJsonValue>> AccountRows,ItemRows,BudgetRows,History;
    for(const auto& P:Accounts){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),P.Key.ToString());Number(O,TEXT("gold"),P.Value.Gold);Number(O,TEXT("endowment"),InitialAccounts.FindRef(P.Key));O->SetBoolField(TEXT("retired"),P.Value.Retired);O->SetNumberField(TEXT("revision"),P.Value.Revision);AccountRows.Add(MakeShared<FJsonValueObject>(O));}
    for(const auto& P:Items){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),P.Key.ToString());O->SetNumberField(TEXT("kind"),uint8(P.Value.Kind));O->SetStringField(TEXT("location"),P.Value.Location.ToString());O->SetStringField(TEXT("origin"),P.Value.Origin.ToString());O->SetBoolField(TEXT("consumed"),P.Value.Consumed);TArray<TSharedPtr<FJsonValue>> Parents;for(auto Id:P.Value.Parents){Parents.Add(MakeShared<FJsonValueString>(Id.ToString()));}O->SetArrayField(TEXT("parents"),Parents);ItemRows.Add(MakeShared<FJsonValueObject>(O));}
    for(const auto& P:Budgets){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),P.Key.ToString());O->SetNumberField(TEXT("initial"),InitialBudgets.FindRef(P.Key));O->SetNumberField(TEXT("remaining"),P.Value);BudgetRows.Add(MakeShared<FJsonValueObject>(O));}
    TArray<const FReceipt*> Ordered;for(const auto& P:Receipts){Ordered.Add(&P.Value);}Ordered.Sort([](const FReceipt& A,const FReceipt& B){return A.Serial<B.Serial;});
    for(const auto* R:Ordered)
    {
        const auto& T=R->Transaction;auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),T.Id.ToString());O->SetStringField(TEXT("cause"),T.Cause.ToString());Number(O,TEXT("serial"),R->Serial);Number(O,TEXT("mint"),T.Mint);Number(O,TEXT("burn"),T.Burn);O->SetStringField(TEXT("budget"),T.Budget.ToString());O->SetNumberField(TEXT("units"),T.Units);
        TArray<TSharedPtr<FJsonValue>> Gold,Transitions;
        for(const auto& D:T.Gold){auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("account"),D.Account.ToString());Number(V,TEXT("delta"),D.Delta);V->SetNumberField(TEXT("expected"),D.Expected);Gold.Add(MakeShared<FJsonValueObject>(V));}
        for(const auto& D:T.Items){auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("id"),D.Id.ToString());V->SetNumberField(TEXT("op"),uint8(D.Op));V->SetNumberField(TEXT("kind"),uint8(D.Kind));V->SetNumberField(TEXT("expected"),D.Expected);V->SetStringField(TEXT("from"),D.From.ToString());V->SetStringField(TEXT("to"),D.To.ToString());TArray<TSharedPtr<FJsonValue>> Parents;for(auto Id:D.Parents){Parents.Add(MakeShared<FJsonValueString>(Id.ToString()));}V->SetArrayField(TEXT("parents"),Parents);Transitions.Add(MakeShared<FJsonValueObject>(V));}
        O->SetArrayField(TEXT("gold"),Gold);O->SetArrayField(TEXT("items"),Transitions);History.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("accounts"),AccountRows);Root->SetArrayField(TEXT("items"),ItemRows);Root->SetArrayField(TEXT("budgets"),BudgetRows);Root->SetArrayField(TEXT("receipts"),History);
    FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
}
