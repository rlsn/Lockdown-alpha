#include "Misc/AutomationTest.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDLoadoutRequest.h"
#include "UI/LDControlSettings.h"
#include "Combat/LDTacticalRules.h"
#include "Net/DataBunch.h"
namespace
{
    FLDInventory Bag(){FLDInventory I;I.RulesVersion=7;I.Quick.SetNum(3);return I;}
    FGuid Add(FLDInventory& I,ELDItemKind K){FLDInventoryItem V;V.Id=FGuid::NewGuid();V.Kind=K;V.Slot=I.Items.Num();I.Items.Add(V);return V.Id;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLoadoutCapsTest,"Lockdown.Rules.EquipmentCapsAndCapacity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDLoadoutCapsTest::RunTest(const FString& Parameters)
{
    TArray<int32> R;R.Init(2,36);TArray<int32> G;const auto Tot=LDGear::Allocate(R,G);int32 Sum=0;for(int32 V:Tot){Sum+=V;TestTrue(TEXT("Per-attribute cap"),V<=3);}TestEqual(TEXT("Deterministic first binding consumes total six"),Sum,6);TestTrue(TEXT("Allocation order STR DEX CON then later slots inactive"),G[0]==2&&G[1]==2&&G[2]==2&&G[3]==0&&G[6]==0);
    auto I=Bag();auto B=LDDevelopment::TacticalStart(0,FString::ChrN(64,TEXT('a')));const FGuid Halberd=Add(I,ELDItemKind::IronHalberd);const FGuid Harness=Add(I,ELDItemKind::MerchantHarness);FLDInventory Out;FString Why;
    TestTrue(TEXT("Two-hand proposal"),LDGear::Propose(I,B,LDInventory::Equip,Halberd,0,Out,Why));I=Out;TestTrue(TEXT("One instance reserves two bindings and weighs once"),I.Bindings[0]==Halberd&&I.Bindings[1]==Halberd&&I.Items.Num()==2&&LDInventory::Weight(I)==9);
    TestFalse(TEXT("Invalid offhand binding rejects"),LDGear::Propose(I,B,LDInventory::Equip,Harness,1,Out,Why));
    TestTrue(TEXT("Harness capacity equipped"),LDGear::Propose(I,B,LDInventory::Equip,Harness,2,Out,Why));I=Out;B.Gear=LDGear::Derive(I);TestEqual(TEXT("Natural STR controls base capacity; primary gear STR does not"),LDInventory::WeightCapacity(B),72);
    while(LDInventory::Weight(I)<=62){Add(I,ELDItemKind::Core);}TestTrue(TEXT("Cargo fits with harness"),LDInventory::Fits(I,B,{}, {},&Why));TestFalse(TEXT("Removal cannot store overweight state"),LDGear::Propose(I,B,LDInventory::Unequip,Harness,0,Out,Why));TestFalse(TEXT("Bound equipment cannot become an outgoing trade/recipe projection"),LDInventory::Fits(I,B,{Halberd},{},&Why));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDProgression73Test,"Lockdown.Rules.NaturalGatesMasteryAndPotency",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDProgression73Test::RunTest(const FString& Parameters)
{
    auto B=LDDevelopment::TacticalStart(0,FString::ChrN(64,TEXT('a')));B.Attributes[3]=9;B.Gear.Primary[3]=3;TestEqual(TEXT("Effective INT includes gear"),LDDevelopment::Effective(B)[3],12);TestFalse(TEXT("Temporal Lock natural gate cannot use gear"),LDGear::NaturalGate(B,ELDSkill::TemporalLock));
    B.Attributes[3]=13;B.Attributes[4]=10;TestTrue(TEXT("Natural cross-class investment qualifies independently of class"),LDGear::NaturalGate(B,ELDSkill::TemporalLock));
    int64 XP=0;B=LDDevelopment::TacticalStart(0,B.CatalogDigest);LDDevelopment::AwardXP(B,XP,3575);TestEqual(TEXT("Level15 XP"),B.Level,15);TestEqual(TEXT("Fourteen natural points"),B.AttributePoints,14);TestEqual(TEXT("Fourteen mastery points"),B.SkillPoints,14);LDDevelopment::AwardXP(B,XP,500);TestEqual(TEXT("Cap telemetry has no new points"),B.SkillPoints,14);
    auto P=LDDevelopment::PlanFrom(B);P.Ranks[uint8(ELDSkill::ArcBolt)]=1;FLDCharacterBuild Result;FString Why;TestFalse(TEXT("Unknown skill cannot be point-trained without a book"),LDDevelopment::Prepare(B,P,Result,Why));
    B.Ranks[uint8(ELDSkill::Counterpulse)]=1;P=LDDevelopment::PlanFrom(B);P.Ranks[15]=3;TestTrue(TEXT("Known cross-class skill trains to R3 for three points"),LDDevelopment::Prepare(B,P,Result,Why));TestEqual(TEXT("Mastery point cost"),Result.SkillPoints,11);
    B.Ranks[uint8(ELDSkill::GroundingField)]=1;B.Gear.ControlPotency=1;TestEqual(TEXT("Declared control anchor HP potency"),LDGear::Magnitude(B,ELDSkill::GroundingField,TEXT("anchor_hp"),{90,104,117}),104);TestEqual(TEXT("Cooldown cannot receive potency"),LDGear::Magnitude(B,ELDSkill::Counterpulse,TEXT("cooldown_ms"),{20000,19000,18000}),20000);
    TestEqual(TEXT("New death delay level15 at 35min"),LDDevelopment::TacticalDeathDelay(15,2100),55.);TestEqual(TEXT("Death delay cap"),LDDevelopment::TacticalDeathDelay(15,100000),90.);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControls73Test,"Lockdown.Rules.TacticalControlMigration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDControls73Test::RunTest(const FString& Parameters)
{
    auto* C=NewObject<ULDControlSettings>();C->ResetBindings();FString Why;TestTrue(TEXT("Legacy D remap permitted"),C->Rebind(TEXT("PanUp"),EKeys::D,Why));C->TextScale=1.25;C->Zoom=1600;
    TestTrue(TEXT("Migration yields valid conflict-free v07 profile"),C->MigrateTactical());TestEqual(TEXT("Fifth skill default D"),C->Key(TEXT("Skill5")),FKey(EKeys::D));TestFalse(TEXT("Removed quick4 absent"),C->Bindings.Contains(TEXT("Item4")));TestEqual(TEXT("Compatible attack preference retained"),C->Key(TEXT("Attack")),FKey(EKeys::A));TestTrue(TEXT("D conflict relocated; display preferences retained"),C->Key(TEXT("PanUp"))!=EKeys::D&&C->TextScale==1.25&&C->Zoom==1600);TestFalse(TEXT("Conflict with fifth binding rejects"),C->Rebind(TEXT("Context"),EKeys::D,Why));const auto Before=C->Bindings;TestTrue(TEXT("Migration idempotent"),C->MigrateTactical()&&C->Bindings.OrderIndependentCompareEqual(Before));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLoadoutWireTest,"Lockdown.Rules.LoadoutWireAndQuickShape",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDLoadoutWireTest::RunTest(const FString& Parameters)
{
    const auto* C=ULDEquipmentCatalog::Load();FString Why;if(!TestNotNull(TEXT("Native B73 catalog"),C)){return false;}TestTrue(TEXT("Closed native data matches derived fields"),C->Validate(Why));TestEqual(TEXT("All40 item effects"),C->Items.Num(),40);
    auto I=Bag();auto Book=Add(I,ELDItemKind::Book);auto Bell=Add(I,ELDItemKind::NullBell);TestFalse(TEXT("Book is not a combat reference"),LDInventory::BindQuick(I,Book,0,Why));TestTrue(TEXT("Bell quick reference"),LDInventory::BindQuick(I,Bell,2,Why));TestFalse(TEXT("Removed quick4 rejected by core rules"),LDInventory::BindQuick(I,Bell,3,Why));TestTrue(TEXT("Three-reference bag validates"),LDInventory::Valid(I));
    FLDLoadoutRequest R;R.Match=R.Life=R.Token=R.InventoryRevision=R.BuildRevision=R.ItemRevision=1;R.Item=Bell;R.Command=LDInventory::Bind;R.Binding=2;R.EquipmentDigest=C->Checksum;FNetBitWriter W(nullptr,2048);bool Good=false;R.NetSerialize(W,nullptr,Good);FLDLoadoutRequest Read;FNetBitReader Reader(nullptr,W.GetData(),W.GetNumBits());Read.NetSerialize(Reader,nullptr,Good);TestTrue(TEXT("Bounded loadout wire roundtrip"),Good&&Read.Item==Bell&&Read.EquipmentDigest==C->Checksum);R.Binding=6;FNetBitWriter Bad(nullptr,2048);R.NetSerialize(Bad,nullptr,Good);TestFalse(TEXT("Unknown binding rejected"),Good);return true;
}
