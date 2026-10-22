#include "Inventory/LDInventoryTypes.h"
#include "Misc/AutomationTest.h"
#include "Vision/LDViewTypes.h"
#include "UObject/CoreNet.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDInventoryRulesTest,"Lockdown.Rules.InventoryIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDInventoryRulesTest::RunTest(const FString& Parameters)
{
    FLDInventory I;for(uint8 N=0;N<24;++N){FLDInventoryItem Item;Item.Id=FGuid::NewGuid();Item.Kind=ELDItemKind::Book;Item.Skill=ELDSkill::Spark;Item.Slot=N;I.Items.Add(Item);}const FGuid Book=I.Items[3].Id;
    TestTrue(TEXT("24 unique books fit the common bag without a four-book cap"),LDInventory::Valid(I));FString Why;
    TestTrue(TEXT("Own book binds"),LDInventory::BindQuick(I,Book,0,Why));LDInventory::BindQuick(I,Book,2,Why);TestTrue(TEXT("Moving one reference clears its old position atomically"),!I.Quick[0].IsValid()&&I.Quick[2]==Book);
    const auto Revision=I.Items[3].Revision;I.Items.Swap(0,3);Swap(I.Items[0].Slot,I.Items[3].Slot);TestTrue(TEXT("Reorder keeps the identity reference and availability revision"),LDInventory::Valid(I)&&I.Quick[2]==Book&&I.Find(Book)->Revision==Revision);
    I.Items[0].Kind=ELDItemKind::Seal;TestFalse(TEXT("Seal cannot be assigned"),LDInventory::BindQuick(I,Book,0,Why));I.Items[0].Kind=ELDItemKind::Book;I.Items[0].bReserved=true;TestFalse(TEXT("Reserved item cannot be assigned"),LDInventory::BindQuick(I,Book,0,Why));TestTrue(TEXT("A reserved quick reference remains valid but locked until cancellation or transfer"),LDInventory::Valid(I)&&I.Quick[2]==Book);I.Items[0].bReserved=false;
    TestFalse(TEXT("A foreign or dropped identity not in the bag cannot bind"),LDInventory::BindQuick(I,FGuid::NewGuid(),0,Why));LDInventory::Remove(I,Book);TestTrue(TEXT("Removal clears the exact reference in the same mutation"),!I.Find(Book)&&!I.Quick[2].IsValid()&&LDInventory::Valid(I));
    FLDInventoryItem Extra;Extra.Id=FGuid::NewGuid();Extra.Slot=24;I.Items.Add(Extra);TestFalse(TEXT("Out-of-bounds slot rejected"),LDInventory::Valid(I));
    FLDWorldView View;View.Generation=1;View.Current.Init(0,LDVision::MaskBytes);View.Explored.Init(0,LDVision::MaskBytes);FLDObservedEntity E;E.Id=FGuid::NewGuid();E.Kind=ELDObservedKind::WardCharge;E.Position=FVector(0,0,12);View.Entities.Add(E);FNetBitWriter Writer(nullptr,65536);bool Good=true;View.NetSerialize(Writer,nullptr,Good);FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits());FLDWorldView Read;Read.NetSerialize(Reader,nullptr,Good);TestTrue(TEXT("Dropped charge uses the existing eight-field observation without canonical item identity"),Good&&Read.Entities.Num()==1&&Read.Entities[0].Kind==ELDObservedKind::WardCharge);
    return true;
}
#endif
