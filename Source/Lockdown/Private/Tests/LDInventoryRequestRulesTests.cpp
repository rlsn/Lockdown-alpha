#include "Inventory/LDInventoryDirector.h"
#include "Vision/LDVisionDirector.h"
#include "World/LDWorldQueryTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/EnumProperty.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace LDInventoryRequestTests
{
FLDInventoryItem MakeEntry(ELDItemKind Kind, uint8 Slot, uint32 Revision)
{
    FLDInventoryItem Out;
    Out.Id = FGuid::NewGuid();
    Out.Kind = Kind;
    Out.Slot = Slot;
    Out.Revision = Revision;
    return Out;
}
// Moves every reflected field the test does not drive off its default, so the whole-struct comparisons also see
// references (quick and binding slots point at held items), cooldowns and any field added later. A field type the
// fixture cannot seed fails the test instead of being skipped.
template <typename TStruct>
bool SeedUndriven(TStruct& Value, const TArray<FGuid>& Held, const TSet<FName>& Driven, FString& Error)
{
    const TStruct Default = TStruct();
    int32 Salt = 0;
    for (TFieldIterator<FProperty> It(TStruct::StaticStruct()); It; ++It)
    {
        const FProperty* Property = *It;
        if (Driven.Contains(Property->GetFName())) { continue; }
        void* Storage = Property->ContainerPtrToValuePtr<void>(&Value);
        ++Salt;
        const FNumericProperty* NumberField = CastField<FNumericProperty>(Property);
        const FStructProperty* StructField = CastField<FStructProperty>(Property);
        const FArrayProperty* ArrayField = CastField<FArrayProperty>(Property);
        const FStructProperty* ElementField = ArrayField ? CastField<FStructProperty>(ArrayField->Inner) : nullptr;
        if (const FBoolProperty* BoolField = CastField<FBoolProperty>(Property)) { BoolField->SetPropertyValue(Storage, !BoolField->GetPropertyValue(Storage)); }
        else if (const FEnumProperty* EnumField = CastField<FEnumProperty>(Property))
        {
            const FNumericProperty* Underlying = EnumField->GetUnderlyingProperty();
            Underlying->SetIntPropertyValue(Storage, Underlying->GetSignedIntPropertyValue(Storage) + 1);
        }
        else if (NumberField && NumberField->IsInteger()) { NumberField->SetIntPropertyValue(Storage, NumberField->GetSignedIntPropertyValue(Storage) + Salt); }
        else if (StructField && StructField->Struct == TBaseStructure<FGuid>::Get()) { *static_cast<FGuid*>(Storage) = FGuid::NewGuid(); }
        else if (ElementField && ElementField->Struct == TBaseStructure<FGuid>::Get() && !Held.IsEmpty())
        {
            FScriptArrayHelper References(ArrayField, Storage);
            if (References.Num() == 0) { References.AddValues(1); }
            for (int32 Index = 0; Index < References.Num(); ++Index)
            {
                *reinterpret_cast<FGuid*>(References.GetRawPtr(Index)) = Held[(Index + Salt) % Held.Num()];
            }
        }
        else
        {
            Error = FString::Printf(TEXT("the fixture has no seed for %s.%s"), *TStruct::StaticStruct()->GetName(), *Property->GetName());
            return false;
        }
        if (Property->Identical_InContainer(&Value, &Default))
        {
            Error = FString::Printf(TEXT("%s.%s kept its default value"), *TStruct::StaticStruct()->GetName(), *Property->GetName());
            return false;
        }
    }
    return true;
}
// Three held items of distinct kinds in a four-slot bag; every field the test does not drive is off its default.
bool MakeBag(FLDInventory& Out, FString& Error)
{
    Out = FLDInventory();
    Out.Capacity = 4;
    Out.Revision = 7;
    Out.Items = {MakeEntry(ELDItemKind::Crystal, 0, 3), MakeEntry(ELDItemKind::Fiber, 1, 5), MakeEntry(ELDItemKind::Ore, 2, 9)};
    TArray<FGuid> Held;
    for (const FLDInventoryItem& Entry : Out.Items) { Held.Add(Entry.Id); }
    TSet<FName> ItemDriven;
    ItemDriven.Add(GET_MEMBER_NAME_CHECKED(FLDInventoryItem, Id));
    ItemDriven.Add(GET_MEMBER_NAME_CHECKED(FLDInventoryItem, Kind));
    ItemDriven.Add(GET_MEMBER_NAME_CHECKED(FLDInventoryItem, Slot));
    ItemDriven.Add(GET_MEMBER_NAME_CHECKED(FLDInventoryItem, Revision));
    ItemDriven.Add(GET_MEMBER_NAME_CHECKED(FLDInventoryItem, bReserved));
    for (FLDInventoryItem& Entry : Out.Items)
    {
        if (!SeedUndriven(Entry, Held, ItemDriven, Error)) { return false; }
    }
    TSet<FName> BagDriven;
    BagDriven.Add(GET_MEMBER_NAME_CHECKED(FLDInventory, Revision));
    BagDriven.Add(GET_MEMBER_NAME_CHECKED(FLDInventory, Capacity));
    BagDriven.Add(GET_MEMBER_NAME_CHECKED(FLDInventory, Items));
    return SeedUndriven(Out, Held, BagDriven, Error);
}
LDInventoryRequest::FItemFacts FactsOf(const FLDInventoryItem& Entry)
{
    LDInventoryRequest::FItemFacts Out;
    Out.bOwned = true;
    Out.Revision = Entry.Revision;
    Out.bReserved = Entry.bReserved;
    Out.Slot = Entry.Slot;
    return Out;
}
// Every reflected field of the bag and of each item, in order: all that a refusal must leave exactly as it was.
bool SameBag(const FLDInventory& A, const FLDInventory& B)
{
    return FLDInventory::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDInventoryRequestAdmissionTest,"Lockdown.Rules.InventoryRequestAdmission",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDInventoryRequestAdmissionTest::RunTest(const FString& Parameters)
{
    using namespace LDInventoryRequestTests;
    using LDInventoryRequest::EResult;
    FLDInventory Fixture;
    FString FixtureError;
    if (!TestTrue(TEXT("Fixture: every bag and item field the test does not drive holds a non-default value, with references to held items"),
        MakeBag(Fixture, FixtureError)))
    {
        AddError(FixtureError);
        return false;
    }
    const FLDInventory Original = Fixture;
    const FLDInventoryItem First = Original.Items[0];
    const uint8 MoveCommand = LDInventory::Move, SortCommand = LDInventory::Sort, DropCommand = LDInventory::Drop;

    // M0684: a stale bag revision refuses Move, Sort and Drop before any reorder or version change.
    for (const uint32 Stale : {Original.Revision - 1, Original.Revision + 1})
    {
        FLDInventory Moved = Original, Sorted = Original;
        TestTrue(FString::Printf(TEXT("Stale inventory revision %u refuses a move without partial reorder"), Stale),
            LDInventoryRequest::Organize(Moved, MoveCommand, Stale, First.Id, First.Revision, FactsOf(First), 3) == EResult::StaleInventory && SameBag(Moved, Original));
        TestTrue(FString::Printf(TEXT("Stale inventory revision %u refuses a sort without partial reorder"), Stale),
            LDInventoryRequest::Organize(Sorted, SortCommand, Stale, FGuid(), 0, LDInventoryRequest::FItemFacts(), 0) == EResult::StaleInventory && SameBag(Sorted, Original));
        TestTrue(FString::Printf(TEXT("Stale inventory revision %u refuses a drop at admission"), Stale),
            LDInventoryRequest::Admit(Original, DropCommand, Stale, First.Revision, FactsOf(First)) == EResult::StaleInventory);
    }

    // M0685: a stale or missing item refuses without mutation.
    FLDInventory StaleItem = Original;
    TestTrue(TEXT("Stale item revision refuses a move without mutation"),
        LDInventoryRequest::Organize(StaleItem, MoveCommand, Original.Revision, First.Id, First.Revision - 1, FactsOf(First), 3) == EResult::StaleItem && SameBag(StaleItem, Original));
    TestTrue(TEXT("Stale item revision refuses a drop at admission"),
        LDInventoryRequest::Admit(Original, DropCommand, Original.Revision, First.Revision + 1, FactsOf(First)) == EResult::StaleItem);
    TestTrue(TEXT("An item the caller no longer holds refuses a move without mutation"),
        LDInventoryRequest::Organize(StaleItem, MoveCommand, Original.Revision, First.Id, First.Revision, LDInventoryRequest::FItemFacts(), 3) == EResult::StaleItem && SameBag(StaleItem, Original));
    TestTrue(TEXT("An item the caller no longer holds refuses a drop at admission"),
        LDInventoryRequest::Admit(Original, DropCommand, Original.Revision, First.Revision, LDInventoryRequest::FItemFacts()) == EResult::StaleItem);

    // M0686: organizing outside the bag capacity refuses without mutation.
    for (const uint8 Outside : {uint8(4), uint8(255)})
    {
        FLDInventory OutOfRange = Original;
        TestTrue(FString::Printf(TEXT("Out-of-range organization to slot %u rejects without mutation"), Outside),
            LDInventoryRequest::Organize(OutOfRange, MoveCommand, Original.Revision, First.Id, First.Revision, FactsOf(First), Outside) == EResult::InvalidSlot && SameBag(OutOfRange, Original));
    }

    // M0691: a reserved source refuses Move and Drop before any ownership, reference or version change.
    LDInventoryRequest::FItemFacts Held = FactsOf(First);
    Held.bReserved = true;
    FLDInventory ReservedSource = Original;
    TestTrue(TEXT("Reserved source rejects a move before any reference or version mutation"),
        LDInventoryRequest::Organize(ReservedSource, MoveCommand, Original.Revision, First.Id, First.Revision, Held, 3) == EResult::ReservedItem && SameBag(ReservedSource, Original));
    TestTrue(TEXT("Reserved source rejects a drop at admission, before any ownership change"),
        LDInventoryRequest::Admit(Original, DropCommand, Original.Revision, First.Revision, Held) == EResult::ReservedItem);
    FLDInventory Locked = Original;
    Locked.Items[1].bReserved = true;
    const FLDInventory LockedBefore = Locked;
    TestTrue(TEXT("Any reserved entry blocks sorting the bag without mutation"),
        LDInventoryRequest::Organize(Locked, SortCommand, Locked.Revision, FGuid(), 0, LDInventoryRequest::FItemFacts(), 0) == EResult::ReservedBag && SameBag(Locked, LockedBefore));
    TestTrue(TEXT("Any reserved entry blocks moving another item without mutation"),
        LDInventoryRequest::Organize(Locked, MoveCommand, Locked.Revision, First.Id, First.Revision, FactsOf(Locked.Items[0]), 3) == EResult::ReservedBag && SameBag(Locked, LockedBefore));

    // Controls: current requests are admitted, and organizing changes exactly the slots and the bag revision.
    FLDInventory Moved = Original, MovedExpected = Original;
    MovedExpected.Items[0].Slot = 1;
    MovedExpected.Items[1].Slot = 0;
    ++MovedExpected.Revision;
    TestTrue(TEXT("A current move swaps exactly the two slots and advances the bag revision once"),
        LDInventoryRequest::Organize(Moved, MoveCommand, Original.Revision, First.Id, First.Revision, FactsOf(First), 1) == EResult::Accepted
        && SameBag(Moved, MovedExpected));
    // Kind order is Fiber, Ore, Crystal; items keep their identity, revisions and references.
    FLDInventory Sorted = Original, SortedExpected = Original;
    SortedExpected.Items = {Original.Items[1], Original.Items[2], Original.Items[0]};
    for (int32 Index = 0; Index < SortedExpected.Items.Num(); ++Index) { SortedExpected.Items[Index].Slot = uint8(Index); }
    ++SortedExpected.Revision;
    TestTrue(TEXT("A current sort orders by kind, renumbers every slot and advances the bag revision once"),
        LDInventoryRequest::Organize(Sorted, SortCommand, Original.Revision, FGuid(), 0, LDInventoryRequest::FItemFacts(), 0) == EResult::Accepted
        && SameBag(Sorted, SortedExpected));
    TestTrue(TEXT("A current drop is admitted"), LDInventoryRequest::Admit(Original, DropCommand, Original.Revision, First.Revision, FactsOf(First)) == EResult::Accepted);
    FLDInventory Unsupported = Original;
    TestTrue(TEXT("Only move and sort organize the bag"),
        LDInventoryRequest::Organize(Unsupported, DropCommand, Original.Revision, First.Id, First.Revision, FactsOf(First), 3) == EResult::Unsupported && SameBag(Unsupported, Original));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDInventoryDropFaultTest,"Lockdown.Rules.InventoryDropFaults",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDInventoryDropFaultTest::RunTest(const FString& Parameters)
{
    using LDInventoryDrop::ECheck;
    // Client coordinates: nonfinite, off the registered ground, blocked or unsupported points are ordinary refusals,
    // so no request coordinate can close the match.
    for(const ELDWorldQueryFailure Input:{ELDWorldQueryFailure::InvalidPosition,ELDWorldQueryFailure::Blocked,ELDWorldQueryFailure::NotGrounded,ELDWorldQueryFailure::WrongSurface})
    {TestTrue(FString::Printf(TEXT("Requested ground failure %d refuses without a fault"),int32(Input)),LDInventoryDrop::FromGround(false,Input)==ECheck::Refuse);}
    // The registered world itself, the actual body's capsule and non-ground results are not input: they fault.
    for(const ELDWorldQueryFailure World:{ELDWorldQueryFailure::MissingWorld,ELDWorldQueryFailure::StaleWorld,ELDWorldQueryFailure::InvalidCapsule,ELDWorldQueryFailure::InvalidSurface,
        ELDWorldQueryFailure::MissingNavigation,ELDWorldQueryFailure::IncompletePath,ELDWorldQueryFailure::Unreachable,static_cast<ELDWorldQueryFailure>(250)})
    {TestTrue(FString::Printf(TEXT("World ground failure %d is an authority fault"),int32(World)),LDInventoryDrop::FromGround(false,World)==ECheck::Fault);}
    TestTrue(TEXT("Legal ground continues to the sight check"),LDInventoryDrop::FromGround(true,ELDWorldQueryFailure::None)==ECheck::Accept);
    TestTrue(TEXT("A failed query without a failure code is a fault, never legal ground"),LDInventoryDrop::FromGround(false,ELDWorldQueryFailure::None)==ECheck::Fault);
    TestTrue(TEXT("A legal result carrying a failure code is a fault"),LDInventoryDrop::FromGround(true,ELDWorldQueryFailure::Blocked)==ECheck::Fault);
    // Sight between the actual body and already validated ground uses observation's classification.
    TestTrue(TEXT("Visible validated ground accepts the drop"),LDInventoryDrop::FromSight(ELDSightResult::Visible)==ECheck::Accept);
    TestTrue(TEXT("Out-of-range validated ground refuses"),LDInventoryDrop::FromSight(ELDSightResult::OutOfRange)==ECheck::Refuse);
    TestTrue(TEXT("Occluded validated ground refuses"),LDInventoryDrop::FromSight(ELDSightResult::Blocked)==ECheck::Refuse);
    TestTrue(TEXT("Off-world sight after validation is an authority fault"),LDInventoryDrop::FromSight(ELDSightResult::InvalidPoint)==ECheck::Fault);
    TestTrue(TEXT("Stale sight geometry is an authority fault"),LDInventoryDrop::FromSight(ELDSightResult::StaleWorld)==ECheck::Fault);
    TestTrue(TEXT("Unknown sight result never accepts"),LDInventoryDrop::FromSight(static_cast<ELDSightResult>(250))==ECheck::Fault);
    return true;
}
#endif
