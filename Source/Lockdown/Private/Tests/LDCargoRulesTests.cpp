#include "Inventory/LDInventoryTypes.h"
#include "Vision/LDViewTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreNet.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCargoRulesTest,"Lockdown.Rules.CargoCapacity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDCargoRulesTest::RunTest(const FString& Parameters)
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
    TestTrue(TEXT("Trade removes outgoing mass before checking incoming mass"),LDInventory::Fits(Bag,Build,{Bag.Items[0].Id},{ELDItemKind::Crystal}));
    TestFalse(TEXT("Net trade overload fails before either side mutates"),LDInventory::Fits(Bag,Build,{Bag.Items[0].Id},{ELDItemKind::Core}));
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
    FLDWorldView View;View.Generation=1;View.Current.Init(0,LDVision::MaskBytes);View.Explored.Init(0,LDVision::MaskBytes);
    FLDObservedEntity Entity;Entity.Id=FGuid::NewGuid();Entity.Kind=ELDObservedKind::Cargo;Entity.Position=FVector(0,0,12);View.Entities.Add(Entity);
    FNetBitWriter Writer(nullptr,65536);bool Good=true;View.NetSerialize(Writer,nullptr,Good);
    FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits());FLDWorldView Read;Read.NetSerialize(Reader,nullptr,Good);
    TestTrue(TEXT("Dropped cargo uses the existing observation fields and a temporary pickup handle"),Good&&Read.Entities.Num()==1&&LDVision::PickupKind(Read.Entities[0].Kind));
    return true;
}
#endif
