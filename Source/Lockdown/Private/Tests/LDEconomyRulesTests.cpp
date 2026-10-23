#include "Economy/LDEconomyLedger.h"
#include "Economy/LDEconomyCatalog.h"
#include "Economy/LDProductionDirector.h"
#include "Economy/LDCommerceDirector.h"
#include "Economy/LDBankDirector.h"
#include "Vision/LDViewTypes.h"
#include "UObject/UnrealType.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDEconomyAccountingTest,"Lockdown.Rules.EconomyAccounting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDEconomyAccountingTest::RunTest(const FString& Parameters)
{
    using namespace LDEconomy;FString Error;FLedger L;
    TestTrue(TEXT("Initialize finite match once"),L.Initialize(FGuid::NewGuid(),{{TEXT("Wallet"),100},{TEXT("Bank"),0},{TEXT("Market"),150}},{{TEXT("Node"),1},{TEXT("Reward"),1}},Error));
    TestFalse(TEXT("Duplicate initialization never refills"),L.Initialize(FGuid::NewGuid(),{}, {},Error));
    FTransaction Deposit;
    TestTrue(TEXT("Build explicit deposit"),BankTransaction(L,FGuid::NewGuid(),TEXT("Wallet"),TEXT("Bank"),1,1,100,true,Deposit,Error));
    TestTrue(TEXT("Commit deposit"),L.Commit(Deposit,Error));
    TestEqual(TEXT("Deposit burns two"),L.Burned(),int64(2));
    TestEqual(TEXT("Deposit credits 98"),L.Account(TEXT("Bank"))->Gold,int64(98));
    TestTrue(TEXT("Identical old receipt replay succeeds without revision mutation"),L.Commit(Deposit,Error));
    TestEqual(TEXT("Replay charged once"),L.Account(TEXT("Bank"))->Revision,uint32(2));
    auto Altered=Deposit;Altered.Burn=3;
    TestFalse(TEXT("Replay with altered terms rejects"),L.Commit(Altered,Error));
    FTransaction Withdraw;
    TestTrue(TEXT("Build withdrawal"),BankTransaction(L,FGuid::NewGuid(),TEXT("Wallet"),TEXT("Bank"),2,2,30,false,Withdraw,Error));
    TestTrue(TEXT("Withdraw commits"),L.Commit(Withdraw,Error));
    TestEqual(TEXT("Withdrawal leaves 68 protected gold"),L.Account(TEXT("Bank"))->Gold,int64(68));
    TestEqual(TEXT("Withdrawal gives 30 wallet gold"),L.Account(TEXT("Wallet"))->Gold,int64(30));
    TestFalse(TEXT("Stale balance confirmation fails"),BankTransaction(L,FGuid::NewGuid(),TEXT("Wallet"),TEXT("Bank"),2,2,10,true,Withdraw,Error));
    for(int64 Amount:{int64(-1),int64(0),int64(1),MAX_int64}){TestFalse(TEXT("Negative/zero/no-credit/overflow deposits fail"),BankTransaction(L,FGuid::NewGuid(),TEXT("Wallet"),TEXT("Bank"),3,3,Amount,true,Withdraw,Error));}
    TestEqual(TEXT("Fee ceiling at 51"),DepositFee(51),int64(2));
    TestEqual(TEXT("Fee calculation avoids multiply overflow"),DepositFee(MaxBalance),MaxBalance/50+(MaxBalance%50!=0));
    FTransaction Reward;Reward.Id=FGuid::NewGuid();Reward.Cause=TEXT("Work");Reward.Mint=25;Reward.Gold={{TEXT("Wallet"),3,25}};Reward.Budget=TEXT("Reward");Reward.Units=1;
    TestTrue(TEXT("Bounded reward commits"),L.Commit(Reward,Error));
    auto Excess=Reward;Excess.Id=FGuid::NewGuid();Excess.Gold[0].Expected=4;
    TestFalse(TEXT("Exhausted budget cannot mint"),L.Commit(Excess,Error));
    TestEqual(TEXT("Reward budget stays exhausted"),L.Remaining(TEXT("Reward")),0);
    FTransaction Transfer;Transfer.Id=FGuid::NewGuid();Transfer.Cause=TEXT("MarketSale");Transfer.Gold={{TEXT("Market"),1,-12},{TEXT("Wallet"),4,12}};
    TestTrue(TEXT("Market payment is a transfer"),L.Commit(Transfer,Error));
    TestEqual(TEXT("Market payment did not mint"),L.Minted(),int64(25));
    auto Invalid=Transfer;Invalid.Id=FGuid::NewGuid();Invalid.Gold={{TEXT("Wallet"),5,1}};
    const auto Before=L.TotalGold();const auto Receipts=L.ReceiptCount();
    TestFalse(TEXT("Unclassified money creation fails atomically"),L.Commit(Invalid,Error));
    TestEqual(TEXT("Failed transaction changes no balance"),L.TotalGold(),Before);
    TestEqual(TEXT("Failed transaction adds no committed receipt"),L.ReceiptCount(),Receipts);
    const FGuid Input=FGuid::NewGuid(),Output=FGuid::NewGuid();
    FTransaction Gather;Gather.Id=FGuid::NewGuid();Gather.Cause=TEXT("Gather");Gather.Budget=TEXT("Node");Gather.Units=1;
    FItemDelta Create;Create.Id=Input;Create.Op=EItemOp::Create;Create.Kind=ELDItemKind::Crystal;Create.To=TEXT("Wallet");Gather.Items={Create};
    TestTrue(TEXT("Extraction creates one real identity and consumes charge together"),L.Commit(Gather,Error));
    auto Again=Gather;Again.Id=FGuid::NewGuid();Again.Items[0].Id=FGuid::NewGuid();TestFalse(TEXT("Node cannot refill through another receipt"),L.Commit(Again,Error));
    FTransaction Craft;Craft.Id=FGuid::NewGuid();Craft.Cause=TEXT("Craft");Craft.Burn=6;Craft.Gold={{TEXT("Wallet"),5,-6}};
    FItemDelta Consume;Consume.Id=Input;Consume.Op=EItemOp::Consume;Consume.Kind=ELDItemKind::Crystal;Consume.Expected=1;Consume.From=TEXT("Wallet");
    Create.Id=Output;Create.Kind=ELDItemKind::Core;Create.Parents={Input};Craft.Items={Consume,Create};
    auto BadCraft=Craft;BadCraft.Items[1].Parents={FGuid::NewGuid()};
    TestFalse(TEXT("Missing parent fails before fee or input consumption"),L.Commit(BadCraft,Error));
    TestFalse(TEXT("Failed craft keeps input live"),L.Item(Input)->Consumed);
    TestTrue(TEXT("Causal transformation commits once"),L.Commit(Craft,Error));
    TestTrue(TEXT("Input retained as consumed audit record"),L.Item(Input)->Consumed);
    TestEqual(TEXT("Output has origin receipt"),L.Item(Output)->Origin,Craft.Id);
    TestTrue(TEXT("Output retains exact parent"),L.Item(Output)->Parents==TArray<FGuid>{Input});
    TestTrue(TEXT("Lazy NPC endowment counted once"),L.EndowAccount(TEXT("NPC"),36));
    TestFalse(TEXT("Recreated body cannot repeat endowment"),L.EndowAccount(TEXT("NPC"),36));
    TestTrue(TEXT("Wallet can retire without destruction"),L.Retire(TEXT("Wallet")));
    TestTrue(TEXT("Retirement is idempotent"),L.Retire(TEXT("Wallet")));
    TestEqual(TEXT("All banks/markets/retired/NPC gold reconciles"),L.TotalGold(),int64(303));
    TestTrue(TEXT("Full independent receipt replay reconciles"),L.Reconcile(Error));
    AddInfo(Error);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDEconomyCatalogTest,"Lockdown.Rules.EconomyCatalog",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDEconomyCatalogTest::RunTest(const FString& Parameters)
{
    using namespace LDEconomy;FCatalog C;FString Error;
    if(!TestTrue(TEXT("Runtime economy catalog loads"),C.Load(Error))){AddError(Error);return false;}
    int32 Count=0,Stocks=0;int64 Gold=0;
    for(const auto& Market:C.Markets){Gold+=Market.Gold;for(const auto& Good:Market.Goods)
    {
        Stocks+=Good.Target;
        for(int32 S=0;S<=Good.Cap;++S)
        {
            ++Count;const int32 A=Ask(Good,S),B=Bid(Good,S);
            TestTrue(TEXT("Prices bounded and valid"),A>0&&B>=0&&B<A);
            if(S>0){TestTrue(TEXT("Buy then sell same instance cannot profit"),Bid(Good,S-1)<A);}
            if(S<Good.Cap){TestTrue(TEXT("Sell then rebuy cannot profit"),B<Ask(Good,S+1));}
        }
    }}
    TestEqual(TEXT("Initial market cash"),Gold,int64(450));TestEqual(TEXT("Initial unique market items"),Stocks,84);
    TestEqual(TEXT("Enumerated stock states"),Count,216);
    const auto* City=C.Markets.FindByPredicate([](const auto& M){return M.Id==TEXT("City");});
    const auto* Crystal=City->Goods.FindByPredicate([](const auto& G){return G.Kind==ELDItemKind::Crystal;});
    TestEqual(TEXT("First City Crystal"),Ask(*Crystal,8),12);TestEqual(TEXT("Second City Crystal"),Ask(*Crystal,7),13);TestEqual(TEXT("Third City Crystal"),Ask(*Crystal,6),14);
    for(const auto& M:C.Markets){for(const auto& G:M.Goods){for(int32 S=0;S<=G.Cap;++S){TestTrue(TEXT("Shortage ratio preserves nonprofitable cycles"),S==0||Bid(G,S-1,5,4)<Ask(G,S,5,4));}}}
    const FString Checksum=C.Checksum;FString Json;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/economy_rules.json")));
    TestFalse(TEXT("Malformed JSON fails"),C.Parse(TEXT("{}"),Error));
    TestFalse(TEXT("Fractional stock fails closed"),C.Parse(Json.Replace(TEXT("\"stock\": 18"),TEXT("\"stock\": 18.5")),Error));
    TestFalse(TEXT("Unknown schema fails"),C.Parse(Json.Replace(TEXT("\"schemaVersion\": 1"),TEXT("\"schemaVersion\": 2")),Error));
    TestFalse(TEXT("Recipe count cannot create extra outputs"),C.Parse(Json.Replace(TEXT("\"outputCount\": 1"),TEXT("\"outputCount\": 2")),Error));
    TestFalse(TEXT("Gather budget cannot reset on respawn"),C.Parse(Json.Replace(TEXT("\"resetOnRespawn\": false"),TEXT("\"resetOnRespawn\": true")),Error));
    TestFalse(TEXT("Coupon cannot stack"),C.Parse(Json.Replace(TEXT("\"stacking\": false"),TEXT("\"stacking\": true")),Error));
    TestEqual(TEXT("Failed reload leaves live definitions intact"),C.Checksum,Checksum);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDEconomyPayloadTest,"Lockdown.Rules.EconomyPayload",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDEconomyPayloadTest::RunTest(const FString& Parameters)
{
    auto Fields=[&](UStruct* Struct,const TArray<FName>& Names){int32 Count=0;for(TFieldIterator<FProperty> It(Struct,EFieldIteratorFlags::ExcludeSuper);It;++It){++Count;TestTrue(TEXT("Only intended economy payload fields"),Names.Contains(It->GetFName()));}TestEqual(TEXT("Complete reflected payload field count"),Count,Names.Num());for(FName Name:Names){TestNotNull(TEXT("Field participates in RPC/replication serialization"),Struct->FindPropertyByName(Name));}};
    Fields(FLDProductionPlan::StaticStruct(),{TEXT("Site"),TEXT("Recipe"),TEXT("RecipeRevision"),TEXT("InventoryRevision"),TEXT("BuildRevision"),TEXT("SkillSlot"),TEXT("Fee"),TEXT("Coupon"),TEXT("Inputs")});
    Fields(FLDCommerceOffer::StaticStruct(),{TEXT("Kind"),TEXT("Stock"),TEXT("Cap"),TEXT("Ask"),TEXT("Bid")});
    Fields(FLDCommerceView::StaticStruct(),{TEXT("Session"),TEXT("Token"),TEXT("Match"),TEXT("Life"),TEXT("Revision"),TEXT("Service"),TEXT("Cash"),TEXT("QuotedAt"),TEXT("Offers")});
    Fields(FLDBankView::StaticStruct(),{TEXT("Session"),TEXT("Token"),TEXT("Match"),TEXT("Life"),TEXT("WalletRevision"),TEXT("BankRevision"),TEXT("Wallet"),TEXT("Bank")});
    for(FName N:{FName(TEXT("CraftCoupon")),FName(TEXT("CouponSite")),FName(TEXT("CouponUntil")),FName(TEXT("ProductionSite")),FName(TEXT("ProductionSeconds"))}){TestNotNull(TEXT("Private production owner field reflected"),FLDOwnerState::StaticStruct()->FindPropertyByName(N));}
    FLDProductionPlan Plan;Plan.Site=4;Plan.Recipe=2;Plan.RecipeRevision=7;Plan.InventoryRevision=19;Plan.BuildRevision=11;Plan.SkillSlot=3;Plan.Fee=4;Plan.Coupon=FGuid::NewGuid();FLDInventoryItem Item;Item.Id=FGuid::NewGuid();Item.Kind=ELDItemKind::Toolkit;Item.Revision=17;Plan.Inputs.Add(Item);
    TArray<uint8> Bytes;FMemoryWriter Writer(Bytes);FLDProductionPlan::StaticStruct()->SerializeItem(Writer,&Plan,nullptr);FLDProductionPlan Read;FMemoryReader Reader(Bytes);FLDProductionPlan::StaticStruct()->SerializeItem(Reader,&Read,nullptr);
    TestTrue(TEXT("Reflected production plan preserves exact fee, site, recipe, stamps, coupon and input revisions"),!Writer.IsError()&&!Reader.IsError()&&Read.Site==4&&Read.Recipe==2&&Read.RecipeRevision==7&&Read.InventoryRevision==19&&Read.BuildRevision==11&&Read.SkillSlot==3&&Read.Fee==4&&Read.Coupon==Plan.Coupon&&Read.Inputs.Num()==1&&Read.Inputs[0].Id==Item.Id&&Read.Inputs[0].Revision==17);
    return true;
}
#endif
