#include "Misc/AutomationTest.h"
#include "Development/LDV08Loadout.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDLoadoutRequest.h"
#include "Combat/LDTacticalCatalog.h"
#include "Net/DataBunch.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB81Test,"Lockdown.Rules.V08SourcesTraitsAndWire",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB81Test::RunTest(const FString& Parameters)
{
    FString Why;const auto* C=ULDEquipmentCatalog::Load(8);const auto* Skills=ULDTacticalCatalog::Load(8);
    if(!TestNotNull(TEXT("Separate native schema8 equipment"),C)||!TestNotNull(TEXT("Separate native schema8 combat"),Skills)){return false;}
    TestTrue(TEXT("Enabled payloads validate and retain v07 asset separately"),C->Validate(Why)&&Skills->Validate(Why)&&C!=ULDEquipmentCatalog::Load());
    TestEqual(TEXT("Only supported items exposed"),C->Items.Num(),80);
    TestEqual(TEXT("Only supported abilities exposed"),Skills->Abilities.Num(),52);
    TestNotNull(TEXT("Ember Line includes the v08 material surface and persistent damage zone"),Skills->Find(ELDSkill::EmberLine));
    auto B=LDDevelopment::TacticalStart(3,Skills->Checksum,8);FLDInventory I;I.RulesVersion=8;I.Quick.Reset();I.Bindings.SetNum(11);
    auto Add=[&](ELDItemKind K){FLDInventoryItem Item;Item.Id=FGuid::NewGuid();Item.Kind=K;Item.Slot=I.Items.Num();I.Items.Add(Item);return Item.Id;};
    const auto Mail=Add(ELDItemKind::ReflectiveMail),Charm=Add(ELDItemKind::PurityCharm),Halberd=Add(ELDItemKind::IronHalberd),Gloves=Add(ELDItemKind::LaborersGrip);
    auto Plan=[&](){auto P=LDDevelopment::PlanFrom(B);P.InventoryRevision=I.Revision;P.Bindings=I.Bindings;return P;};
    auto P=Plan();P.Bindings[1]=Mail;P.Bindings[7]=Charm;P.Bindings[4]=Gloves;P.Actions[5].Source=ELDActionSource::Gear;P.Actions[5].Instance=Mail;P.Actions[5].Definition=TEXT("reflective_mail");
    FLDInventory Out;FLDCharacterBuild Build;
    TestTrue(TEXT("One projected transaction equips gear and shared action"),LDV08::Project(I,B,P,Out,Build,Why));
    TestTrue(TEXT("Eleven bits survive including accessory; no independent quick references"),Out.Bindings.Num()==11&&Out.Quick.IsEmpty()&&Out.Find(Charm)->BindingMask==128&&LDInventory::Valid(Out));
    TestFalse(TEXT("Source must be equipped in projected state"),[&](){auto Bad=P;Bad.Bindings[1].Invalidate();return LDV08::Project(I,B,Bad,Out,Build,Why);}());
    TestFalse(TEXT("A body action cannot masquerade as an accessory"),[&](){auto Bad=P;Bad.Bindings[7]=Mail;return LDV08::Project(I,B,Bad,Out,Build,Why);}());
    TestFalse(TEXT("One instance cannot fill two accessories"),[&](){auto Bad=P;Bad.Bindings[8]=Charm;return LDV08::Project(I,B,Bad,Out,Build,Why);}());
    TestFalse(TEXT("Stale inventory draft rejects"),[&](){auto Bad=P;++Bad.InventoryRevision;return LDV08::Project(I,B,Bad,Out,Build,Why);}());
    TestFalse(TEXT("Two-handed main hand must reserve offhand"),[&](){auto Bad=P;Bad.Bindings[5]=Halberd;return LDV08::Project(I,B,Bad,Out,Build,Why);}());
    TestFalse(TEXT("Seventh shared action rejects"),[&](){auto Bad=P;Bad.Actions.Add(FLDActionRef());return LDV08::Project(I,B,Bad,Out,Build,Why);}());
    TestFalse(TEXT("Fifth accessory has no binding"),[&](){auto Bad=P;Bad.Bindings.Add(Charm);return LDV08::Project(I,B,Bad,Out,Build,Why);}());
    TestTrue(TEXT("Pure gear allocation allowed through Silence"),LDV08::PreparationPermissions(B,P,false,true));
    TestFalse(TEXT("Mute blocks gear allocation"),LDV08::PreparationPermissions(B,P,true,false));
    TestTrue(TEXT("Projected binding/action still commits after rejected drafts"),LDV08::Project(I,B,P,Out,Build,Why));I=Out;B=Build;
    TestFalse(TEXT("Equipped identity cannot be an outgoing inventory input"),LDInventory::Fits(I,B,{Mail},{},&Why));
    B.Cooldowns[10]=12345;const auto End=B.Cooldowns[10];I.Bindings[1].Invalidate();I.Items.FindByPredicate([&](const auto& V){return V.Id==Mail;})->BindingMask=0;
    TestTrue(TEXT("Source invalidation clears typed slot atomically"),LDV08::InvalidateSources(B,I)&&B.Actions[5].Source==ELDActionSource::Learned&&B.Slots[5]==ELDSkill::None);
    TestEqual(TEXT("Source invalidation preserves learned cooldowns"),B.Cooldowns[10],End);
    TArray<int32> Requested,Granted;Requested.Init(2,66);const auto Primary=LDGear::Allocate(Requested,Granted);int32 Total=0;for(auto Value:Primary){TestTrue(TEXT("Per attribute cap across eleven bindings"),Value<=3);Total+=Value;}
    TestTrue(TEXT("Head-first total cap remains six; later slots suppressed"),Total==6&&Granted[0]==2&&Granted[1]==2&&Granted[2]==2&&Granted[60]==0);
    TestEqual(TEXT("Vanguard arc"),LDV08::TraitValue(B,TEXT("brace_arc"),120),140);
    for(uint8 Preset=0;Preset<9;++Preset){B.Preset=Preset;TestTrue(TEXT("One trait definition per known preset"),FString(LDV08::TraitName(B))!=TEXT("None"));}
    B.Preset=0;TestEqual(TEXT("Laborer capacity trait, no new gold"),LDV08::TraitValue(B,TEXT("capacity"),0),4);
    B.Preset=6;TestEqual(TEXT("Seeker sight shares total cap"),LDV08::TraitValue(B,TEXT("sight"),40),50);
    B.Preset=7;TestEqual(TEXT("Broker discount strongest only"),LDV08::TraitValue(B,TEXT("assembly_discount"),1),1);
    B.Preset=2;TestEqual(TEXT("Future elemental lifetime hook shares global cap"),LDV08::TraitValue(B,TEXT("frozen_ms"),9800),10000);
    TestFalse(TEXT("Unknown preset cannot enable"),LDV08::PresetEnabled(9));TestTrue(TEXT("Arcanist kit includes native Frostline"),LDV08::PresetEnabled(5));
    // Positive maximum case, as well as malformed over-cap requests.
    {auto MaxBuild=LDDevelopment::TacticalStart(0,Skills->Checksum,8);FLDInventory MaxBag;MaxBag.RulesVersion=8;MaxBag.Quick.Reset();MaxBag.Bindings.SetNum(11);const ELDSkill Learned[]={ELDSkill::Heave,ELDSkill::Patch,ELDSkill::Step,ELDSkill::Ambush,ELDSkill::Spark,ELDSkill::Survey};for(int32 N=0;N<6;++N){MaxBuild.Ranks[uint8(Learned[N])]=1;MaxBuild.Slots[N]=Learned[N];MaxBuild.Actions[N].Skill=Learned[N];}const ELDItemKind Accessory[]={ELDItemKind::SmokeFlask,ELDItemKind::WardTotem,ELDItemKind::HealingSalve,ELDItemKind::ManaDraught};for(int32 N=0;N<4;++N){FLDInventoryItem V;V.Id=FGuid::NewGuid();V.Kind=Accessory[N];V.Slot=N;V.BindingMask=uint16(1<<(7+N));MaxBag.Items.Add(V);MaxBag.Bindings[7+N]=V.Id;}TestTrue(TEXT("Exactly six learned plus four physically bound active accessories is legal"),LDDevelopment::Valid(MaxBuild)&&LDInventory::Valid(MaxBag)&&LDV08::Validate(MaxBag,MaxBuild,Why));FLDInventoryItem BagOnly;BagOnly.Id=FGuid::NewGuid();BagOnly.Kind=ELDItemKind::SmokeFlask;BagOnly.Slot=4;MaxBag.Items.Add(BagOnly);TestFalse(TEXT("Backpack identity cannot activate through an accessory source"),LDV08::ItemSource(MaxBag,MaxBuild,BagOnly.Id,0,true));}
    B=LDDevelopment::TacticalStart(3,Skills->Checksum,8);P=LDDevelopment::PlanFrom(B);P.InventoryRevision=1;P.Bindings.SetNum(11);P.Actions[5].Source=ELDActionSource::Gear;P.Actions[5].Instance=Mail;P.Actions[5].Definition=TEXT("reflective_mail");
    FNetBitWriter Writer(nullptr,16384);bool Good=false;P.NetSerialize(Writer,nullptr,Good);TestTrue(TEXT("Bounded schema8 build draft serializes"),Good&&!Writer.IsError());
    FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits());FLDBuildPlan Decoded;Decoded.NetSerialize(Reader,nullptr,Good);TestTrue(TEXT("Typed source + eleven refs roundtrip"),Good&&Decoded.RulesVersion==8&&Decoded.Actions.Num()==6&&Decoded.Actions[5].Instance==Mail&&Decoded.Bindings.Num()==11);
    FLDLoadoutRequest R;R.Version=2;R.Match=R.Life=R.Token=R.InventoryRevision=R.ItemRevision=R.BuildRevision=1;R.Item=Charm;R.Command=LDInventory::Equip;R.Binding=10;R.EquipmentDigest=C->Checksum;
    FNetBitWriter W2(nullptr,4096);R.NetSerialize(W2,nullptr,Good);TestTrue(TEXT("Version2 accepts physical accessory4"),Good);R.Binding=11;FNetBitWriter W3(nullptr,4096);R.NetSerialize(W3,nullptr,Good);TestFalse(TEXT("Out-of-bounds physical binding rejected before authority"),Good);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLoadoutProjectionOutputsTest,"Lockdown.Rules.LoadoutProjectionFailureOutputs",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDLoadoutProjectionOutputsTest::RunTest(const FString& Parameters)
{
    const auto* Skills=ULDTacticalCatalog::Load(8);const auto* Gear=ULDEquipmentCatalog::Load(8);
    if(!TestNotNull(TEXT("Projection fixture has its skill catalog"),Skills)||!TestNotNull(TEXT("Projection fixture has its gear catalog"),Gear)){return false;}
    auto Current=LDDevelopment::TacticalStart(3,Skills->Checksum,8);
    FLDInventory Bag;Bag.RulesVersion=8;Bag.Quick.Reset();Bag.Bindings.SetNum(11);
    FLDInventoryItem Mail;Mail.Id=FGuid::NewGuid();Mail.Kind=ELDItemKind::ReflectiveMail;Mail.Slot=0;Bag.Items.Add(Mail);
    auto Plan=LDDevelopment::PlanFrom(Current);Plan.InventoryRevision=Bag.Revision;Plan.Bindings=Bag.Bindings;
    Plan.Bindings[1]=Mail.Id;Plan.Actions[5].Source=ELDActionSource::Gear;Plan.Actions[5].Instance=Mail.Id;Plan.Actions[5].Definition=TEXT("reflective_mail");
    FLDInventory Out;FLDCharacterBuild Build;FString Why;
    Out.Revision=731;Build.Revision=919;
    const auto InventorySentinel=Out;const auto BuildSentinel=Build;
    auto SameOutputs=[&](){return FLDInventory::StaticStruct()->CompareScriptStruct(&Out,&InventorySentinel,0)&&FLDCharacterBuild::StaticStruct()->CompareScriptStruct(&Build,&BuildSentinel,0);};
    auto Stale=Plan;++Stale.InventoryRevision;
    TestFalse(TEXT("Project rejects stale inventory before materializing outputs"),LDV08::Project(Bag,Current,Stale,Out,Build,Why));
    TestTrue(TEXT("Stale inventory leaves both full output sentinels unchanged"),SameOutputs()&&!Why.IsEmpty());
    auto Malformed=Plan;Malformed.Bindings.Pop();
    TestFalse(TEXT("Project rejects malformed binding shape before preparing output"),LDV08::Project(Bag,Current,Malformed,Out,Build,Why));
    TestTrue(TEXT("Malformed binding shape leaves both outputs unchanged"),SameOutputs());
    auto StaleBuild=Plan;--StaleBuild.ExpectedRevision;
    TestFalse(TEXT("Project rejects a stale build through Prepare"),LDV08::Project(Bag,Current,StaleBuild,Out,Build,Why));
    TestTrue(TEXT("Prepare refusal leaves both Project outputs unchanged"),SameOutputs());
    auto MissingSource=Plan;MissingSource.Bindings[1].Invalidate();
    TestFalse(TEXT("Project rejects an action whose projected source is unequipped"),LDV08::Project(Bag,Current,MissingSource,Out,Build,Why));
    // Late validation owns scratch outputs; callers must discard both when Project returns false.
    TestTrue(TEXT("Late refusal leaves the actual rejected inventory candidate, not the old sentinel"),Out.Bindings==MissingSource.Bindings&&Out.Find(Mail.Id)&&Out.Find(Mail.Id)->BindingMask==0&&Out.Revision==Bag.Revision);
    TestTrue(TEXT("Late refusal leaves the prepared build candidate with its rejected action"),Build.Revision==Current.Revision+1&&Build.Actions==MissingSource.Actions&&Build.Cooldowns==Current.Cooldowns);
    TestFalse(TEXT("Rejected scratch outputs are not a valid publishable pair"),LDV08::Validate(Out,Build,Why));
    TestTrue(TEXT("A later valid projection replaces both rejected scratch outputs"),LDV08::Project(Bag,Current,Plan,Out,Build,Why));
    TestTrue(TEXT("Successful output binds the exact source and validates as a pair"),Out.Find(Mail.Id)->BindingMask==uint16(1<<1)&&Out.Bindings[1]==Mail.Id&&LDV08::Validate(Out,Build,Why));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGearProjectionTest,"Lockdown.Rules.GearProjection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDGearProjectionTest::RunTest(const FString& Parameters)
{
    const auto* Skills=ULDTacticalCatalog::Load(8);const auto* Gear=ULDEquipmentCatalog::Load(8);
    if(!TestNotNull(TEXT("Gear proposal fixture has its skill catalog"),Skills)||!TestNotNull(TEXT("Gear proposal fixture has its gear catalog"),Gear)){return false;}
    auto Build=LDDevelopment::TacticalStart(3,Skills->Checksum,8);
    FLDInventory Bag;Bag.RulesVersion=8;Bag.Quick.Reset();Bag.Bindings.SetNum(11);
    FLDInventoryItem Mail;Mail.Id=FGuid::NewGuid();Mail.Kind=ELDItemKind::ReflectiveMail;Mail.Slot=0;Mail.Revision=9;Mail.ReadyAt=12345;
    FLDInventoryItem Weapon;Weapon.Id=FGuid::NewGuid();Weapon.Kind=ELDItemKind::IronHalberd;Weapon.Slot=1;
    Bag.Items={Mail,Weapon};FLDInventory Out;FString Why;
    if(!TestTrue(TEXT("Propose equips one owned physical torso source"),LDGear::Propose(Bag,Build,LDInventory::Equip,Mail.Id,1,Out,Why))){return false;}
    TestTrue(TEXT("Equip output changes bindings and inventory revision, not item identity or deadline"),Out.Bindings[1]==Mail.Id&&Out.Find(Mail.Id)->BindingMask==uint16(1<<1)&&Out.Revision==Bag.Revision+1&&Out.Find(Mail.Id)->Revision==Mail.Revision&&Out.Find(Mail.Id)->ReadyAt==Mail.ReadyAt);
    const auto Sentinel=Out;
    auto SameOutput=[&](){return FLDInventory::StaticStruct()->CompareScriptStruct(&Out,&Sentinel,0);};
    auto Reserved=Bag;Reserved.Items[0].bReserved=true;
    TestFalse(TEXT("Propose refuses a reserved physical source"),LDGear::Propose(Reserved,Build,LDInventory::Equip,Mail.Id,1,Out,Why));
    TestTrue(TEXT("Reserved proposal refuses without changing the prior output"),SameOutput()&&!Why.IsEmpty());
    TestFalse(TEXT("Propose refuses a foreign physical identity"),LDGear::Propose(Bag,Build,LDInventory::Equip,FGuid::NewGuid(),1,Out,Why));
    TestTrue(TEXT("Foreign proposal leaves all output fields unchanged"),SameOutput());
    TestFalse(TEXT("Propose refuses an out-of-bounds binding"),LDGear::Propose(Bag,Build,LDInventory::Equip,Mail.Id,11,Out,Why));
    TestTrue(TEXT("Bounds refusal leaves the full output unchanged"),SameOutput());
    TestTrue(TEXT("Two-handed proposal reserves both hands for the same instance"),LDGear::Propose(Bag,Build,LDInventory::Equip,Weapon.Id,5,Out,Why)&&Out.Bindings[5]==Weapon.Id&&Out.Bindings[6]==Weapon.Id&&Out.Find(Weapon.Id)->BindingMask==uint16((1<<5)|(1<<6)));
    const auto Equipped=Out;
    TestTrue(TEXT("Unequip releases both hand references in one proposed output"),LDGear::Propose(Equipped,Build,LDInventory::Unequip,Weapon.Id,5,Out,Why)&&!Out.Bindings[5].IsValid()&&!Out.Bindings[6].IsValid()&&Out.Find(Weapon.Id)->BindingMask==0&&Out.Revision==Equipped.Revision+1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDSourceInvalidationTest,"Lockdown.Rules.SourceInvalidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDSourceInvalidationTest::RunTest(const FString& Parameters)
{
    const auto* Skills=ULDTacticalCatalog::Load(8);const auto* Gear=ULDEquipmentCatalog::Load(8);
    if(!TestNotNull(TEXT("Source invalidation fixture has its skill catalog"),Skills)||!TestNotNull(TEXT("Source invalidation fixture has its gear catalog"),Gear)){return false;}
    auto Current=LDDevelopment::TacticalStart(3,Skills->Checksum,8);
    FLDInventory Bag;Bag.RulesVersion=8;Bag.Quick.Reset();Bag.Bindings.SetNum(11);
    FLDInventoryItem Mail;Mail.Id=FGuid::NewGuid();Mail.Kind=ELDItemKind::ReflectiveMail;Mail.Slot=0;Bag.Items.Add(Mail);
    auto Plan=LDDevelopment::PlanFrom(Current);Plan.InventoryRevision=Bag.Revision;Plan.Bindings=Bag.Bindings;Plan.Bindings[1]=Mail.Id;
    Plan.Actions[5].Source=ELDActionSource::Gear;Plan.Actions[5].Instance=Mail.Id;Plan.Actions[5].Definition=TEXT("reflective_mail");
    FLDInventory Equipped;FLDCharacterBuild Build;FString Why;
    if(!TestTrue(TEXT("Source invalidation starts with a valid projected action"),LDV08::Project(Bag,Current,Plan,Equipped,Build,Why))){return false;}
    Build.Cooldowns[uint8(Build.Slots[0])]=9876;
    const auto Before=Build;
    TestFalse(TEXT("Current equipped source causes no invalidation"),LDV08::InvalidateSources(Build,Equipped));
    TestTrue(TEXT("No-op invalidation leaves every build field unchanged"),FLDCharacterBuild::StaticStruct()->CompareScriptStruct(&Build,&Before,0));
    auto Unbound=Equipped;Unbound.Bindings[1].Invalidate();Unbound.Items[0].BindingMask=0;auto UnboundBuild=Build;
    TestTrue(TEXT("Unequipped source clears its dependent action without resetting cooldowns"),LDV08::InvalidateSources(UnboundBuild,Unbound)&&!Unbound.Bindings[1].IsValid()&&UnboundBuild.Actions[5]==FLDActionRef()&&UnboundBuild.Slots[5]==ELDSkill::None&&UnboundBuild.Revision==Before.Revision+1&&UnboundBuild.Cooldowns==Before.Cooldowns);
    const auto Replacement=FGuid::NewGuid();Equipped.Items[0].Id=Replacement;Equipped.Bindings[1]=Replacement;
    TestTrue(TEXT("Same-kind replacement identity invalidates the old action source"),LDV08::InvalidateSources(Build,Equipped));
    TestTrue(TEXT("Invalidation clears only the stale action and increments revision exactly once"),Build.Actions[5]==FLDActionRef()&&Build.Slots[5]==ELDSkill::None&&Build.Actions[0]==Before.Actions[0]&&Build.Revision==Before.Revision+1);
    TestTrue(TEXT("Source loss preserves every learned deadline and investment"),Build.Cooldowns==Before.Cooldowns&&Build.Ranks==Before.Ranks&&Build.Attributes==Before.Attributes&&Build.SkillPoints==Before.SkillPoints&&Build.AttributePoints==Before.AttributePoints);
    const auto Invalidated=Build;
    TestFalse(TEXT("Repeated source invalidation is a no-op"),LDV08::InvalidateSources(Build,Equipped));
    TestTrue(TEXT("Repeated invalidation cannot increment revision or erase other state"),FLDCharacterBuild::StaticStruct()->CompareScriptStruct(&Build,&Invalidated,0));
    return true;
}
