#include "Economy/LDEconomyLedger.h"
#include "Economy/LDEconomyDirector.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDEconomyUninitializedRefusalTest,"Lockdown.Rules.EconomyUninitializedRefusal",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDEconomyUninitializedRefusalTest::RunTest(const FString& Parameters)
{
    auto* Director=GetMutableDefault<ALDEconomyDirector>();
    const int32 Before=Director->Accounting().ReceiptCount();
    AddExpectedError(TEXT("LD_ECONOMY_NOT_INITIALIZED"),EAutomationExpectedErrorFlags::Contains,1);
    TestFalse(TEXT("An uninitialized economy cannot report a committed transaction"),Director->Record(TEXT("Fixture")));
    TestEqual(TEXT("Rejected uninitialized call creates no receipt"),Director->Accounting().ReceiptCount(),Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDEconomyAccountingTest,"Lockdown.Rules.EconomyAccounting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDEconomyAccountingTest::RunTest(const FString& Parameters)
{
    using namespace LDEconomy;FString Error;FLedger L;
    const FGuid Match=FGuid::NewGuid();
    if(!TestTrue(TEXT("Initialize accounting fixture, not Demo endowments"),L.Initialize(Match,{{TEXT("A"),100},{TEXT("B"),0}},{{TEXT("Global"),2},{TEXT("Owner"),1}},Error))){return false;}
    const FString Initial=L.ExportJson(TEXT("Same"));
    TestFalse(TEXT("A repeated initialization cannot refill the match"),L.Initialize(Match,{{TEXT("A"),200}}, {},Error));
    TestEqual(TEXT("Rejected initialization preserves the complete domain"),L.ExportJson(TEXT("Same")),Initial);
    TestTrue(TEXT("Reopening an existing account cannot reset it"),L.OpenAccount(TEXT("A"))&&L.Account(TEXT("A"))->Gold==100&&L.Account(TEXT("A"))->Revision==1);
    TestTrue(TEXT("New account starts at zero without minting"),L.OpenAccount(TEXT("C"))&&L.Account(TEXT("C"))->Gold==0&&L.Minted()==0);
    TestFalse(TEXT("Missing account identity is rejected"),L.OpenAccount(NAME_None));
    FTransaction Transfer;Transfer.Id=FGuid::NewGuid();Transfer.Cause=TEXT("FixtureTransfer");Transfer.Gold={{TEXT("A"),1,-10},{TEXT("B"),1,10}};
    if(!TestTrue(TEXT("Conserved two-account transaction commits"),L.Commit(Transfer,Error))){return false;}
    const FString Transferred=L.ExportJson(TEXT("Same"));
    TestTrue(TEXT("Exact completed request replays"),L.Commit(Transfer,Error));
    TestEqual(TEXT("Replay preserves every balance, revision and receipt"),L.ExportJson(TEXT("Same")),Transferred);
    auto Changed=Transfer;Changed.Cause=TEXT("ChangedTerms");
    TestFalse(TEXT("Same request ID cannot change its cause"),L.Commit(Changed,Error));
    Changed=Transfer;Changed.Id=FGuid::NewGuid();
    TestFalse(TEXT("A fresh ID cannot reuse stale account revisions"),L.Commit(Changed,Error));
    TestEqual(TEXT("Rejected replays leave the domain intact"),L.ExportJson(TEXT("Same")),Transferred);
    FTransaction Unbacked;Unbacked.Id=FGuid::NewGuid();Unbacked.Cause=TEXT("FixtureUnbacked");Unbacked.Gold={{TEXT("A"),2,1}};
    TestFalse(TEXT("Unclassified positive balance creation is rejected"),L.Commit(Unbacked,Error));
    TestEqual(TEXT("Unclassified creation changes no balance, revision or receipt"),L.ExportJson(TEXT("Same")),Transferred);
    FTransaction Reward;Reward.Id=FGuid::NewGuid();Reward.Cause=TEXT("FixtureReward");Reward.Mint=5;Reward.Gold={{TEXT("A"),2,5}};Reward.Budget=TEXT("Global");Reward.Units=1;Reward.Budgets.Add({TEXT("Owner"),1});
    TestTrue(TEXT("Multiple bounds and reward commit together"),L.Commit(Reward,Error));
    const FString Rewarded=L.ExportJson(TEXT("Same"));
    Changed=Reward;Changed.Id=FGuid::NewGuid();Changed.Gold[0].Expected=3;
    TestFalse(TEXT("An exhausted bound rejects before another bound or wallet changes"),L.Commit(Changed,Error));
    TestTrue(TEXT("Failed bounded reward preserves all state"),L.ExportJson(TEXT("Same"))==Rewarded&&L.Remaining(TEXT("Global"))==1&&L.Remaining(TEXT("Owner"))==0);
    FTransaction Spend;Spend.Id=FGuid::NewGuid();Spend.Cause=TEXT("FixtureSpend");Spend.Burn=5;Spend.Gold={{TEXT("A"),3,-5}};
    Changed=Spend;Changed.Burn=0;TestFalse(TEXT("Unclassified resource destruction is rejected"),L.Commit(Changed,Error));
    TestTrue(TEXT("Explicit burn commits once"),L.Commit(Spend,Error));
    TestTrue(TEXT("Retirement retains accounting and is idempotent"),L.Retire(TEXT("B"))&&L.Retire(TEXT("B")));
    const FString Retired=L.ExportJson(TEXT("Same"));
    Changed=Reward;Changed.Id=FGuid::NewGuid();Changed.Budget=NAME_None;Changed.Units=0;Changed.Budgets.Reset();Changed.Gold={{TEXT("B"),3,5}};
    TestFalse(TEXT("Retired account cannot accept a new reward"),L.Commit(Changed,Error));
    TestEqual(TEXT("Retired rejection changes no receipt or balance"),L.ExportJson(TEXT("Same")),Retired);
    TestTrue(TEXT("Live and retired accounts conserve all classified money"),L.TotalGold()==100&&L.Minted()==5&&L.Burned()==5);
    TestTrue(TEXT("Independent replay reconstructs revisions, bounds and retirement"),L.Reconcile(Error));
    FLedger Endowments;
    if(!TestTrue(TEXT("Initialize isolated late-endowment fixture"),Endowments.Initialize(FGuid::NewGuid(),{}, {},Error))){return false;}
    TestTrue(TEXT("A previously unseen account receives its finite endowment once"),Endowments.EndowAccount(TEXT("LateOwner"),36)&&Endowments.TotalGold()==36&&Endowments.Minted()==0);
    const FString Endowed=Endowments.ExportJson(TEXT("Same"));
    TestFalse(TEXT("Reusing an endowed account identity cannot refill it"),Endowments.EndowAccount(TEXT("LateOwner"),36));
    TestEqual(TEXT("Duplicate endowment preserves the complete accounting domain"),Endowments.ExportJson(TEXT("Same")),Endowed);
    TestTrue(TEXT("Late endowment is retained by independent replay"),Endowments.Reconcile(Error));
    FLedger BoundedItems;
    if(!TestTrue(TEXT("Initialize a finite item-creation budget fixture"),BoundedItems.Initialize(FGuid::NewGuid(),{{TEXT("Owner"),0}},{{TEXT("Once"),1}},Error))){return false;}
    FTransaction CreateOnce;CreateOnce.Id=FGuid::NewGuid();CreateOnce.Cause=TEXT("FixtureBoundedCreation");CreateOnce.Budget=TEXT("Once");CreateOnce.Units=1;
    CreateOnce.Mint=5;CreateOnce.Gold={{TEXT("Owner"),1,5}};
    FItemDelta Created;Created.Id=FGuid::NewGuid();Created.Op=EItemOp::Create;Created.Kind=ELDItemKind::Fiber;Created.To=TEXT("World");CreateOnce.Items={Created};
    TestTrue(TEXT("One finite budget debit and one item identity commit together"),BoundedItems.Commit(CreateOnce,Error)&&BoundedItems.Remaining(TEXT("Once"))==0&&BoundedItems.Item(Created.Id)&&BoundedItems.Item(Created.Id)->Origin==CreateOnce.Id&&BoundedItems.Account(TEXT("Owner"))->Gold==5&&BoundedItems.Minted()==5);
    const FString Exhausted=BoundedItems.ExportJson(TEXT("Same"));
    CreateOnce.Id=FGuid::NewGuid();CreateOnce.Items[0].Id=FGuid::NewGuid();CreateOnce.Gold[0].Expected=2;
    TestFalse(TEXT("A fresh request identity cannot refill an exhausted creation budget"),BoundedItems.Commit(CreateOnce,Error));
    TestEqual(TEXT("Exhausted creation leaves items, budgets and receipts unchanged"),BoundedItems.ExportJson(TEXT("Same")),Exhausted);
    TestTrue(TEXT("Finite item creation independently reconciles"),BoundedItems.Reconcile(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDEconomyAtomicItemTransferTest,"Lockdown.Rules.EconomyAtomicItemTransfer",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDEconomyAtomicItemTransferTest::RunTest(const FString& Parameters)
{
    using namespace LDEconomy;
    FString Error;FLedger Ledger;
    if(!TestTrue(TEXT("Initialize transaction fixture, not product prices"),Ledger.Initialize(FGuid::NewGuid(),{{TEXT("Buyer"),100},{TEXT("Vendor"),0}}, {},Error))){return false;}
    FTransaction Stock;Stock.Id=FGuid::NewGuid();Stock.Cause=TEXT("FixtureStock");
    FItemDelta Item;Item.Id=FGuid::NewGuid();Item.Op=EItemOp::Create;Item.Kind=ELDItemKind::Fiber;Item.Quality=2;Item.To=TEXT("Vendor");Stock.Items={Item};
    if(!TestTrue(TEXT("Create one authoritative item identity"),Ledger.Commit(Stock,Error))){return false;}
    FTransaction Buy;Buy.Id=FGuid::NewGuid();Buy.Cause=TEXT("FixturePurchase");Buy.Gold={{TEXT("Buyer"),1,-7},{TEXT("Vendor"),1,7}};
    Item.Op=EItemOp::Transfer;Item.Expected=1;Item.From=TEXT("Vendor");Item.To=TEXT("Buyer");Buy.Items={Item};
    auto Unchanged=[&](const FTransaction& Request,const TCHAR* Label)
    {
        const FString Before=Ledger.ExportJson(TEXT("Atomicity"));
        const uint32 ItemRevision=Ledger.Item(Item.Id)->Revision;
        TestFalse(Label,Ledger.Commit(Request,Error));
        TestFalse(TEXT("Rejected transaction returns an explicit reason"),Error.IsEmpty());
        TestEqual(TEXT("Rejected transaction leaves balances, receipts and item ownership unchanged"),Ledger.ExportJson(TEXT("Atomicity")),Before);
        TestEqual(TEXT("Rejected transaction leaves item revision unchanged"),Ledger.Item(Item.Id)->Revision,ItemRevision);
    };
    auto Invalid=Buy;Invalid.Items[0].Expected=2;Unchanged(Invalid,TEXT("Stale item rejects before either gold delta"));
    Invalid=Buy;Invalid.Items[0].From=TEXT("Buyer");Unchanged(Invalid,TEXT("Foreign item ownership rejects before payment"));
    Invalid=Buy;Invalid.Gold[0].Expected=2;Unchanged(Invalid,TEXT("Stale wallet rejects before item transfer"));
    Invalid=Buy;Invalid.Items[0]=Stock.Items[0];Unchanged(Invalid,TEXT("Duplicate creation fails before charging gold"));
    Invalid=Buy;Invalid.Items[0]=Stock.Items[0];Invalid.Items[0].Id.Invalidate();Unchanged(Invalid,TEXT("Missing allocated output identity rejects before charging gold"));
    Invalid=Buy;Invalid.Gold={{TEXT("Buyer"),1,-101},{TEXT("Vendor"),1,101}};Unchanged(Invalid,TEXT("Insufficient funds cannot transfer an item"));
    if(!TestTrue(TEXT("Current terms commit gold and item together"),Ledger.Commit(Buy,Error))){return false;}
    TestEqual(TEXT("One debit"),Ledger.Account(TEXT("Buyer"))->Gold,int64(93));
    TestEqual(TEXT("One credit"),Ledger.Account(TEXT("Vendor"))->Gold,int64(7));
    TestTrue(TEXT("Transfer preserves identity, quality and origin"),Ledger.Item(Item.Id)->Location==TEXT("Buyer")&&Ledger.Item(Item.Id)->Quality==2&&Ledger.Item(Item.Id)->Origin==Stock.Id);
    const FString Committed=Ledger.ExportJson(TEXT("Atomicity"));
    const uint32 Revision=Ledger.Item(Item.Id)->Revision;
    TestTrue(TEXT("Identical final request replays its receipt"),Ledger.Commit(Buy,Error));
    TestEqual(TEXT("Replay cannot move gold or items a second time"),Ledger.ExportJson(TEXT("Atomicity")),Committed);
    TestEqual(TEXT("Replay cannot advance item revision"),Ledger.Item(Item.Id)->Revision,Revision);
    TestNotNull(TEXT("Stable request identity locates the committed receipt"),Ledger.Receipt(Buy.Id));
    Invalid=Buy;Invalid.Gold[0].Delta=-8;Invalid.Gold[1].Delta=8;Unchanged(Invalid,TEXT("Reusing a receipt with altered prices rejects"));
    Invalid=Buy;Invalid.Items[0].Quality=1;Unchanged(Invalid,TEXT("Reusing a receipt with altered item terms rejects"));
    FTransaction Return;Return.Id=FGuid::NewGuid();Return.Cause=TEXT("FixtureReturn");Item.From=TEXT("Buyer");Item.To=TEXT("Vendor");Item.Expected=Revision;Return.Items={Item};
    TestTrue(TEXT("A fresh source transition preserves the original item"),Ledger.Commit(Return,Error));
    const auto ReturnedRevision=Ledger.Item(Item.Id)->Revision;
    TestTrue(TEXT("Old completed purchase still only replays after ownership changes"),Ledger.Commit(Buy,Error));
    TestTrue(TEXT("Old receipt cannot reacquire or downgrade the returned source"),Ledger.Item(Item.Id)->Location==TEXT("Vendor")&&Ledger.Item(Item.Id)->Revision==ReturnedRevision&&Ledger.Item(Item.Id)->Quality==2&&Ledger.Item(Item.Id)->Origin==Stock.Id);
    TestTrue(TEXT("All accepted and rejected paths independently reconcile"),Ledger.Reconcile(Error));
    FLedger Bounds;
    TestTrue(TEXT("Initialize signed-bound fixture"),Bounds.Initialize(FGuid::NewGuid(),{{TEXT("Full"),MAX_int64}}, {},Error));
    FTransaction Overflow;Overflow.Id=FGuid::NewGuid();Overflow.Cause=TEXT("FixtureOverflow");Overflow.Mint=1;Overflow.Gold={{TEXT("Full"),1,1}};
    TestFalse(TEXT("Overflow is rejected before arithmetic or mutation"),Bounds.Commit(Overflow,Error));
    TestTrue(TEXT("Overflow leaves balance, revision and receipts intact"),Bounds.Account(TEXT("Full"))->Gold==MAX_int64&&Bounds.Account(TEXT("Full"))->Revision==1&&Bounds.ReceiptCount()==0&&Bounds.Reconcile(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDEconomyAtomicReplacementTest,"Lockdown.Rules.EconomyAtomicReplacement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDEconomyAtomicReplacementTest::RunTest(const FString& Parameters)
{
    using namespace LDEconomy;FLedger Ledger;FString Error;
    if(!TestTrue(TEXT("Initialize isolated replacement fixture"),Ledger.Initialize(FGuid::NewGuid(),{{TEXT("Owner"),100}}, {},Error))){return false;}
    FTransaction Seed;Seed.Id=FGuid::NewGuid();Seed.Cause=TEXT("FixtureInputs");
    for(int32 N=0;N<2;++N){FItemDelta Item;Item.Id=FGuid::NewGuid();Item.Op=EItemOp::Create;Item.Kind=ELDItemKind::Fiber;Item.To=TEXT("Owner");Seed.Items.Add(Item);}
    if(!TestTrue(TEXT("Two distinct input identities are recorded"),Ledger.Commit(Seed,Error))){return false;}
    FTransaction Replace;Replace.Id=FGuid::NewGuid();Replace.Cause=TEXT("FixtureReplacement");Replace.Burn=3;Replace.Gold={{TEXT("Owner"),1,-3}};
    for(const auto& Input:Seed.Items){auto Consume=Input;Consume.Op=EItemOp::Consume;Consume.Expected=1;Consume.From=TEXT("Owner");Consume.To=NAME_None;Replace.Items.Add(Consume);}
    FItemDelta Output;Output.Id=FGuid::NewGuid();Output.Op=EItemOp::Create;Output.Kind=ELDItemKind::Fiber;Output.To=TEXT("Owner");Output.Parents={Seed.Items[0].Id,Seed.Items[1].Id};Replace.Items.Add(Output);
    auto RejectUnchanged=[&](const FTransaction& Request,const TCHAR* Reason)
    {
        const FString Before=Ledger.ExportJson(TEXT("Same"));
        TestFalse(Reason,Ledger.Commit(Request,Error));
        TestFalse(TEXT("Failure returns an explicit reason"),Error.IsEmpty());
        TestEqual(TEXT("Failure cannot charge, consume an input or create a receipt"),Ledger.ExportJson(TEXT("Same")),Before);
        for(const auto& Input:Seed.Items){TestTrue(TEXT("Failed replacement retains each live input and its revision"),!Ledger.Item(Input.Id)->Consumed&&Ledger.Item(Input.Id)->Revision==1);}
    };
    auto Bad=Replace;Bad.Items.Last().Id.Invalidate();RejectUnchanged(Bad,TEXT("Unavailable output identity rejects before any mutation"));
    Bad=Replace;Bad.Items[1]=Bad.Items[0];RejectUnchanged(Bad,TEXT("A repeated exact input cannot be consumed twice"));
    Bad=Replace;Bad.Items[1].Expected=2;RejectUnchanged(Bad,TEXT("Stale input revision rejects the entire replacement"));
    Bad=Replace;Bad.Items.Last().Parents[0]=FGuid::NewGuid();RejectUnchanged(Bad,TEXT("Missing parent cannot fabricate a replacement"));
    Bad=Replace;Bad.Items.Last().Id=Seed.Items[0].Id;RejectUnchanged(Bad,TEXT("Consumed identity cannot be reused as a new output"));
    if(!TestTrue(TEXT("Valid replacement commits fee, inputs and output in one receipt"),Ledger.Commit(Replace,Error))){return false;}
    TestTrue(TEXT("All inputs retired, exactly one output live"),Ledger.Item(Seed.Items[0].Id)->Consumed&&Ledger.Item(Seed.Items[1].Id)->Consumed&&!Ledger.Item(Output.Id)->Consumed&&Ledger.Item(Output.Id)->Origin==Replace.Id&&Ledger.Account(TEXT("Owner"))->Gold==97);
    const FString Committed=Ledger.ExportJson(TEXT("Same"));
    TestTrue(TEXT("Duplicate final replacement replays"),Ledger.Commit(Replace,Error));
    TestEqual(TEXT("Replacement replay has no second fee, consumption or output"),Ledger.ExportJson(TEXT("Same")),Committed);
    FTransaction Revision;Revision.Id=FGuid::NewGuid();Revision.Cause=TEXT("FixtureItemRevision");
    auto Item=Output;Item.Op=EItemOp::Quality;Item.From=Item.To=TEXT("Owner");Item.Expected=1;Item.Parents.Reset();Item.Quality=1;Revision.Items={Item};
    TestTrue(TEXT("Existing item revision changes without replacing identity or origin"),Ledger.Commit(Revision,Error)&&Ledger.Item(Output.Id)->Origin==Replace.Id&&Ledger.Item(Output.Id)->Parents==Output.Parents);
    const uint32 Once=Ledger.Item(Output.Id)->Revision;TestTrue(TEXT("Exact item revision replay remains idempotent"),Ledger.Commit(Revision,Error)&&Ledger.Item(Output.Id)->Revision==Once);
    Revision.Id=FGuid::NewGuid();TestFalse(TEXT("Fresh request with stale item revision cannot apply twice"),Ledger.Commit(Revision,Error));
    TestTrue(TEXT("Complete accepted history independently reconciles"),Ledger.Reconcile(Error));
    FLedger TransientLedger;
    if(!TestTrue(TEXT("Initialize isolated transient-parent fixture"),TransientLedger.Initialize(FGuid::NewGuid(),{{TEXT("Owner"),100}}, {},Error))){return false;}
    FTransaction TransientReceipt;TransientReceipt.Id=FGuid::NewGuid();TransientReceipt.Cause=TEXT("EquipmentWorkOrder");TransientReceipt.Burn=3;TransientReceipt.Gold={{TEXT("Owner"),1,-3}};
    FItemDelta Transient;Transient.Id=FGuid::NewGuid();Transient.Kind=ELDItemKind::Fiber;Transient.Op=EItemOp::Transient;Transient.To=TEXT("Owner");
    FItemDelta Final;Final.Id=FGuid::NewGuid();Final.Kind=ELDItemKind::Fiber;Final.Op=EItemOp::Create;Final.To=TEXT("Owner");Final.Parents={Transient.Id};
    TransientReceipt.Items={Transient,Final};
    TestTrue(TEXT("One receipt creates a consumed transient parent and a live causal output"),TransientLedger.Commit(TransientReceipt,Error));
    TestTrue(TEXT("Transient parent is an audit record, not a second live item"),TransientLedger.Item(Transient.Id)&&TransientLedger.Item(Transient.Id)->Consumed&&TransientLedger.Item(Final.Id)&&!TransientLedger.Item(Final.Id)->Consumed&&TransientLedger.Item(Final.Id)->Parents==Final.Parents);
    const FString TransientOnce=TransientLedger.ExportJson(TEXT("Same"));
    TestTrue(TEXT("Exact transient receipt replay does not allocate or charge again"),TransientLedger.Commit(TransientReceipt,Error)&&TransientLedger.ExportJson(TEXT("Same"))==TransientOnce);
    TestTrue(TEXT("Transient parent and final output independently reconcile"),TransientLedger.Reconcile(Error));
    return true;
}
#endif
