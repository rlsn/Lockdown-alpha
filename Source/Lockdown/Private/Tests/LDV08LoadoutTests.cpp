#include "Misc/AutomationTest.h"
#include "Development/LDV08Loadout.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDLoadoutRequest.h"
#include "Combat/LDTacticalCatalog.h"
#include "Combat/LDTacticalRequest.h"
#include "UI/LDControlSettings.h"
#include "Net/DataBunch.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB81Test,"Lockdown.Rules.V08SourcesTraitsAndWire",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB81Test::RunTest(const FString& Parameters)
{
    FString Why;const auto* C=ULDEquipmentCatalog::Load(8);const auto* Skills=ULDTacticalCatalog::Load(8);
    if(!TestNotNull(TEXT("Separate native schema8 equipment"),C)||!TestNotNull(TEXT("Separate native schema8 combat"),Skills)){return false;}
    TestTrue(TEXT("Enabled payloads validate and retain v07 asset separately"),C->Validate(Why)&&Skills->Validate(Why)&&C!=ULDEquipmentCatalog::Load());
    TestEqual(TEXT("Only supported items exposed"),C->Items.Num(),54);
    TestEqual(TEXT("Only supported abilities exposed"),Skills->Abilities.Num(),27);
    TestNull(TEXT("Ember Line hidden until its v08 Burning surface is implemented"),Skills->Find(ELDSkill::EmberLine));
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
    TestFalse(TEXT("Equipped identity cannot be trade or recipe input"),LDInventory::Fits(I,B,{Mail},{},&Why));
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
    TestFalse(TEXT("Unknown preset cannot enable"),LDV08::PresetEnabled(9));TestFalse(TEXT("Arcanist kit waits for actual Frostline support"),LDV08::PresetEnabled(5));
    // Positive maximum case, as well as malformed over-cap requests.
    {auto MaxBuild=LDDevelopment::TacticalStart(0,Skills->Checksum,8);FLDInventory MaxBag;MaxBag.RulesVersion=8;MaxBag.Quick.Reset();MaxBag.Bindings.SetNum(11);const ELDSkill Learned[]={ELDSkill::Heave,ELDSkill::Patch,ELDSkill::Step,ELDSkill::Ambush,ELDSkill::Spark,ELDSkill::Survey};for(int32 N=0;N<6;++N){MaxBuild.Ranks[uint8(Learned[N])]=1;MaxBuild.Slots[N]=Learned[N];MaxBuild.Actions[N].Skill=Learned[N];}const ELDItemKind Accessory[]={ELDItemKind::SmokeFlask,ELDItemKind::WardTotem,ELDItemKind::HealingSalve,ELDItemKind::ManaDraught};for(int32 N=0;N<4;++N){FLDInventoryItem V;V.Id=FGuid::NewGuid();V.Kind=Accessory[N];V.Slot=N;V.BindingMask=uint16(1<<(7+N));MaxBag.Items.Add(V);MaxBag.Bindings[7+N]=V.Id;}TestTrue(TEXT("Exactly six learned plus four physically bound active accessories is legal"),LDDevelopment::Valid(MaxBuild)&&LDInventory::Valid(MaxBag)&&LDV08::Validate(MaxBag,MaxBuild,Why));FLDInventoryItem BagOnly;BagOnly.Id=FGuid::NewGuid();BagOnly.Kind=ELDItemKind::SmokeFlask;BagOnly.Slot=4;MaxBag.Items.Add(BagOnly);TestFalse(TEXT("Backpack identity cannot activate through an accessory source"),LDV08::ItemSource(MaxBag,MaxBuild,BagOnly.Id,0,true));}
    auto* Controls=NewObject<ULDControlSettings>();Controls->ResetBindings();TestTrue(TEXT("Existing G preference accepted"),Controls->Rebind(TEXT("PanUp"),EKeys::G,Why));
    TestTrue(TEXT("Six/four migration is collision-free"),Controls->MigrateHybrid()&&Controls->Valid());TestEqual(TEXT("Existing G preference retained"),Controls->Key(TEXT("PanUp")),FKey(EKeys::G));TestFalse(TEXT("Skill6 relocates instead of overwriting preference"),Controls->Key(TEXT("Skill6"))==EKeys::G);TestFalse(TEXT("Context F cannot overwrite Skill1"),Controls->Rebind(TEXT("Context"),Controls->Key(TEXT("Skill1")),Why));
    B=LDDevelopment::TacticalStart(3,Skills->Checksum,8);P=LDDevelopment::PlanFrom(B);P.InventoryRevision=1;P.Bindings.SetNum(11);P.Actions[5].Source=ELDActionSource::Gear;P.Actions[5].Instance=Mail;P.Actions[5].Definition=TEXT("reflective_mail");
    FNetBitWriter Writer(nullptr,16384);bool Good=false;P.NetSerialize(Writer,nullptr,Good);TestTrue(TEXT("Bounded schema8 build draft serializes"),Good&&!Writer.IsError());
    FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits());FLDBuildPlan Decoded;Decoded.NetSerialize(Reader,nullptr,Good);TestTrue(TEXT("Typed source + eleven refs roundtrip"),Good&&Decoded.RulesVersion==8&&Decoded.Actions.Num()==6&&Decoded.Actions[5].Instance==Mail&&Decoded.Bindings.Num()==11);
    FLDLoadoutRequest R;R.Version=2;R.Match=R.Life=R.Token=R.InventoryRevision=R.ItemRevision=R.BuildRevision=1;R.Item=Charm;R.Command=LDInventory::Equip;R.Binding=10;R.EquipmentDigest=C->Checksum;
    FNetBitWriter W2(nullptr,4096);R.NetSerialize(W2,nullptr,Good);TestTrue(TEXT("Version2 accepts physical accessory4"),Good);R.Binding=11;FNetBitWriter W3(nullptr,4096);R.NetSerialize(W3,nullptr,Good);TestFalse(TEXT("Out-of-bounds physical binding rejected before authority"),Good);
    FLDTacticalRequest Action;Action.Schema=8;Action.WireVersion=4;Action.SourceKind=1;Action.SourceDefinition=TEXT("reflective_mail");Action.Item=Mail;Action.Match=Action.Life=Action.Token=Action.BuildRevision=Action.InventoryRevision=Action.ItemRevision=1;Action.Slot=5;Action.CatalogDigest=Skills->Checksum;
    FNetBitWriter W4(nullptr,8192);Action.NetSerialize(W4,nullptr,Good);TestTrue(TEXT("Wire4 body item source uses shared action6"),Good&&!W4.IsError());FNetBitReader Read4(nullptr,W4.GetData(),W4.GetNumBits());FLDTacticalRequest Copy;Copy.NetSerialize(Read4,nullptr,Good);TestTrue(TEXT("Wire4 source identity roundtrip"),Good&&Copy.SourceKind==1&&Copy.Slot==5&&Copy.SourceDefinition==Action.SourceDefinition&&Copy.Item==Mail);
    return true;
}
