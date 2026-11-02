#include "Inventory/LDInventoryTypes.h"
#include "Misc/AutomationTest.h"
#include "Vision/LDViewTypes.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"
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
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDInventoryPrivacyTest,"Lockdown.Rules.InventoryPrivacySchema",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDInventoryPrivacyTest::RunTest(const FString& Parameters)
{
    const TSet<FName> Allowed={TEXT("Id"),TEXT("Kind"),TEXT("Skill"),TEXT("Slot"),TEXT("Revision"),TEXT("bReserved"),TEXT("BindingMask"),TEXT("ReadyAt"),TEXT("ProcReadyAt"),TEXT("Quality")};
    int32 Fields=0;
    for(TFieldIterator<FProperty> It(FLDInventoryItem::StaticStruct());It;++It)
    {
        ++Fields;
        TestTrue(TEXT("Own inventory item exposes only its declared projection"),Allowed.Contains(It->GetFName()));
        TestFalse(TEXT("Canonical actor and holder references do not enter the item projection"),It->IsA<FObjectPropertyBase>());
    }
    TestEqual(TEXT("Inventory projection excludes origin, parents and hidden ownership"),Fields,Allowed.Num());
    TestFalse(TEXT("Canonical physical items do not replicate to clients"),GetDefault<ALDItem>()->GetIsReplicated());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDInventoryProjectionIdentityTest,"Lockdown.Rules.InventoryProjectionIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDInventoryProjectionIdentityTest::RunTest(const FString& Parameters)
{
    FLDCharacterBuild Build;FLDInventory Bag;Bag.Capacity=2;
    for(uint8 N=0;N<Bag.Capacity;++N){FLDInventoryItem Item;Item.Id=FGuid::NewGuid();Item.Kind=ELDItemKind::Fiber;Item.Slot=N;Bag.Items.Add(Item);}
    const FGuid First=Bag.Items[0].Id;const auto Revision=Bag.Revision;const auto Mass=LDInventory::Weight(Bag);FString Why;
    TestTrue(TEXT("Final projection removes the exact outgoing identity before capacity checks"),LDInventory::Fits(Bag,Build,{First},{ELDItemKind::Fiber},&Why));
    TestFalse(TEXT("A full inventory cannot gain an unaccounted slot"),LDInventory::Fits(Bag,Build,{}, {ELDItemKind::Fiber},&Why));
    TestFalse(TEXT("Capacity failure provides a reason"),Why.IsEmpty());
    TestFalse(TEXT("Duplicate outgoing identity cannot free capacity twice"),LDInventory::Fits(Bag,Build,{First,First},{ELDItemKind::Fiber},&Why));
    TestFalse(TEXT("Foreign identity cannot free this owner's capacity"),LDInventory::Fits(Bag,Build,{FGuid::NewGuid()},{ELDItemKind::Fiber},&Why));
    Bag.Items[0].BindingMask=1;
    TestFalse(TEXT("A bound source cannot enter a consuming projection"),LDInventory::Fits(Bag,Build,{First},{},&Why));
    Bag.Items[0].BindingMask=0;Bag.Items[0].bReserved=true;
    TestEqual(TEXT("Reservation does not duplicate the physical instance's mass"),LDInventory::Weight(Bag),Mass);
    Bag.Items[0].bReserved=false;
    TestTrue(TEXT("Preview and rejection never move, consume or revise authoritative items"),Bag.Revision==Revision&&Bag.Items.Num()==2&&Bag.Items[0].Id==First&&LDInventory::Weight(Bag)==Mass&&LDInventory::Valid(Bag));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDInventoryMassProjectionTest,"Lockdown.Rules.InventoryMassProjection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDInventoryMassProjectionTest::RunTest(const FString& Parameters)
{
    FLDCharacterBuild Build;Build.Attributes[0]=10;FLDInventory Bag;
    auto Add=[&](ELDItemKind Kind){FLDInventoryItem Item;Item.Id=FGuid::NewGuid();Item.Kind=Kind;Item.Slot=uint8(Bag.Items.Num());Bag.Items.Add(Item);return Item.Id;};
    const ELDItemKind Kinds[]={ELDItemKind::Book,ELDItemKind::WardCharge,ELDItemKind::Seal,ELDItemKind::Fiber,ELDItemKind::Ore,ELDItemKind::Crystal,ELDItemKind::Toolkit,ELDItemKind::Core};
    const int32 Weights[]={1,1,2,1,3,2,4,6};
    for(int32 N=0;N<UE_ARRAY_COUNT(Kinds);++N){TestEqual(TEXT("Adopted per-definition weight"),LDInventory::UnitWeight(Kinds[N]),Weights[N]);}
    for(int32 N=0;N<20;++N){Add(ELDItemKind::Ore);}
    TestTrue(TEXT("Twenty Ore fit exactly 60 weight with four free slots"),LDInventory::Fits(Bag,Build,{},{}));
    const auto Revision=Bag.Revision;FString Why;
    TestFalse(TEXT("Pickup or purchase cannot exceed weight despite free slots"),LDInventory::Fits(Bag,Build,{}, {ELDItemKind::Fiber},&Why));
    TestTrue(TEXT("Weight rejection has precise reason and changes no revision"),Why.Contains(TEXT("heavy"))&&Bag.Revision==Revision&&Bag.Items.Num()==20);
    TestTrue(TEXT("Final projection removes outgoing mass before checking incoming mass"),LDInventory::Fits(Bag,Build,{Bag.Items[0].Id},{ELDItemKind::Crystal}));
    TestFalse(TEXT("Net inventory overload rejects without mutating the source"),LDInventory::Fits(Bag,Build,{Bag.Items[0].Id},{ELDItemKind::Core}));
    Bag.Items[0].bReserved=true;TestEqual(TEXT("Reservation does not duplicate carried mass"),LDInventory::Weight(Bag),60);
    Build.Attributes[0]=8;TestEqual(TEXT("STR8 capacity"),LDInventory::WeightCapacity(Build),56);
    TestFalse(TEXT("Preparation lowering capacity below cargo fails"),LDInventory::Fits(Bag,Build,{},{}));
    Build.Attributes[0]=16;TestEqual(TEXT("STR16 capacity"),LDInventory::WeightCapacity(Build),72);
    TestTrue(TEXT("Higher capacity allows load without a movement penalty"),LDInventory::Fits(Bag,Build,{},{}));
    Bag.Items.Reset();Build.Attributes[0]=10;
    const FGuid FiberA=Add(ELDItemKind::Fiber),FiberB=Add(ELDItemKind::Fiber),Ore=Add(ELDItemKind::Ore);
    for(int32 N=0;N<21;++N){Add(ELDItemKind::Book);}
    TestTrue(TEXT("Full 24-slot bag can craft Toolkit after removing three inputs"),LDInventory::Fits(Bag,Build,{FiberA,FiberB,Ore},{ELDItemKind::Toolkit}));
    TestFalse(TEXT("No input removal means full-slot acquisition fails"),LDInventory::Fits(Bag,Build,{}, {ELDItemKind::Fiber}));
    TestFalse(TEXT("Duplicate consumed ID cannot free imaginary slots or mass"),LDInventory::Fits(Bag,Build,{FiberA,FiberA},{ELDItemKind::Toolkit}));
    TestFalse(TEXT("Foreign input cannot free capacity"),LDInventory::Fits(Bag,Build,{FGuid::NewGuid()},{ELDItemKind::Toolkit}));
    for(const auto Kind:{ELDItemKind::Fiber,ELDItemKind::Ore,ELDItemKind::Crystal,ELDItemKind::Toolkit,ELDItemKind::Core})
    {
        Bag.Items[0].Kind=Kind;
        TestTrue(TEXT("Unique commercial instance is a valid carried item"),LDInventory::Valid(Bag));
        TestFalse(TEXT("Cargo has no quick-use shortcut"),LDInventory::BindQuick(Bag,FiberA,0,Why));
        TestFalse(TEXT("Cargo remains unusable through an injected reference"),LDInventory::QuickUsable(Kind));
    }
    Bag.Items[0].Kind=ELDItemKind(255);TestFalse(TEXT("Unknown weight fails closed"),LDInventory::Fits(Bag,Build,{},{}));
    return true;
}
#endif
