#include "Economy/LDB92Services.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB92ServicePayloadTest,"Lockdown.Rules.B92ServicePayload",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB92ServicePayloadTest::RunTest(const FString& Parameters)
{
    auto Fields=[&](UStruct* Struct,const TSet<FName>& Allowed)
    {
        int32 Count=0;
        for(TFieldIterator<FProperty> It(Struct);It;++It)
        {
            ++Count;TestTrue(TEXT("Counter payload exposes only approved facts"),Allowed.Contains(It->GetFName()));
            TestFalse(TEXT("No authority actor/controller pointer in counter payload"),It->IsA<FObjectPropertyBase>());
        }
        TestEqual(TEXT("Every intended field participates in serialization"),Count,Allowed.Num());
    };
    // Work offers are local terms; Inputs are the requesting owner's exact bag
    // stamps. Neither adds remote inventories, authority IDs or global balances.
    Fields(FLDB92Offer::StaticStruct(),{TEXT("Kind"),TEXT("Skill"),TEXT("Stock"),TEXT("Ask"),TEXT("Bid"),TEXT("Recipe")});
    Fields(FLDB92MarketView::StaticStruct(),{TEXT("Session"),TEXT("POI"),TEXT("Match"),TEXT("Life"),TEXT("WalletRevision"),TEXT("Revision"),TEXT("BankRevision"),TEXT("Offers"),TEXT("WorkRevision"),TEXT("WorkOffers")});
    Fields(FLDB92MarketOrder::StaticStruct(),{TEXT("Match"),TEXT("Life"),TEXT("Sequence"),TEXT("InventoryRevision"),TEXT("ItemRevision"),TEXT("WalletRevision"),TEXT("Session"),TEXT("Item"),TEXT("Revision"),TEXT("Kind"),TEXT("Buy"),TEXT("Price"),TEXT("BankAction"),TEXT("Amount"),TEXT("BankRevision"),TEXT("Skill"),TEXT("Supply"),TEXT("WorkRecipe"),TEXT("Inputs")});
    FLDB92MarketOrder Order;Order.Match=7;Order.Life=3;Order.Sequence=19;Order.InventoryRevision=23;Order.ItemRevision=29;Order.WalletRevision=31;Order.Session=FGuid::NewGuid();Order.Item=FGuid::NewGuid();Order.Revision=(uint64(1)<<40)+37;Order.Kind=ELDItemKind::Ore;Order.Buy=false;Order.Price=41;Order.BankAction=2;Order.Amount=123456789012LL;Order.BankRevision=53;Order.Skill=ELDSkill::Spark;Order.Supply=true;
    Order.WorkRecipe=1;FLDInventoryItem Input;Input.Id=FGuid::NewGuid();Input.Kind=ELDItemKind::Fiber;Input.Revision=67;Order.Inputs.Add(Input);
    TArray<uint8> Bytes;FMemoryWriter Writer(Bytes);FLDB92MarketOrder::StaticStruct()->SerializeItem(Writer,&Order,nullptr);
    FLDB92MarketOrder Copy;FMemoryReader Reader(Bytes);FLDB92MarketOrder::StaticStruct()->SerializeItem(Reader,&Copy,nullptr);
    TestTrue(TEXT("Remote confirmation preserves exact item, price, life, session and all revision stamps including64-bit quote"),!Writer.IsError()&&!Reader.IsError()&&Copy==Order);
    Copy.Inputs[0].Id=FGuid::NewGuid();TestFalse(TEXT("Changing an exact work input cannot replay the original confirmation"),Copy==Order);
    return true;
}
#endif
