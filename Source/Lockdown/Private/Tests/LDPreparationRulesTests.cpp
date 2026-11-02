#include "Development/LDCharacterBuild.h"
#include "Development/LDV08Loadout.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"
#include "Combat/LDTacticalCatalog.h"
#include "Inventory/LDEquipmentCatalog.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPreparationWireTest,"Lockdown.Rules.PreparationWire",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDPreparationWireTest::RunTest(const FString& Parameters)
{
    auto B=LDDevelopment::StartingBuild(1);auto P=LDDevelopment::PlanFrom(B);P.ExpectedRevision=39;P.Attributes[0]=12;P.Ranks[3]=2;P.Slots[3]=ELDSkill::Spark;
    FNetBitWriter Writer(nullptr,4096);bool Good=false;P.NetSerialize(Writer,nullptr,Good);
    TestTrue(TEXT("Plan has a constant bounded wire representation"),Good&&Writer.GetNumBytes()<32);
    FLDBuildPlan Read;FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits());Read.NetSerialize(Reader,nullptr,Good);
    TestTrue(TEXT("Plan round trip preserves draft and revision"),Good&&Read.ExpectedRevision==39&&Read.Attributes==P.Attributes&&Read.Ranks==P.Ranks&&Read.Slots==P.Slots);
    P.Attributes.Add(10);FNetBitWriter Invalid(nullptr,4096);P.NetSerialize(Invalid,nullptr,Good);TestFalse(TEXT("Oversized draft rejected before unbounded serialization"),Good);
    FLDBuildPlan Empty;FNetBitWriter EmptyWriter(nullptr,4096);Empty.NetSerialize(EmptyWriter,nullptr,Good);
    FNetBitReader EmptyReader(nullptr,EmptyWriter.GetData(),EmptyWriter.GetNumBits());Read.NetSerialize(EmptyReader,nullptr,Good);
    TestTrue(TEXT("Non-preparation requests carry no arbitrary draft arrays"),Good&&Read.Attributes.IsEmpty()&&Read.Ranks.IsEmpty()&&Read.Slots.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPreparationProjectionTest,"Lockdown.Rules.PreparationProjection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDPreparationProjectionTest::RunTest(const FString& Parameters)
{
    const auto* Skills=ULDTacticalCatalog::Load(7);
    const auto* Equipment=ULDEquipmentCatalog::Load(7);
    if(!TestNotNull(TEXT("Retained preparation fixture has its skill catalog"),Skills)||!TestNotNull(TEXT("Retained preparation fixture has its equipment gates"),Equipment)){return false;}
    auto Current=LDDevelopment::TacticalStart(3,Skills->Checksum,7);
    Current.Level=6;Current.AttributePoints=3;Current.SkillPoints=4;
    Current.Cooldowns[uint8(Current.Slots[0])]=91;
    auto Plan=LDDevelopment::PlanFrom(Current);
    Plan.Attributes[LDDevelopment::STR]+=2;
    const auto Learned=uint8(Current.Slots[0]);Plan.Ranks[Learned]=2;
    FLDCharacterBuild Out;FString Why;
    if(!TestTrue(TEXT("Prepare projects natural investment and learned-rank cost together"),LDDevelopment::Prepare(Current,Plan,Out,Why))){return false;}
    TestTrue(TEXT("Prepared output applies both changes and exact point debits"),Out.Attributes==Plan.Attributes&&Out.Ranks==Plan.Ranks&&Out.AttributePoints==1&&Out.SkillPoints==3);
    TestTrue(TEXT("Prepared output increments once and preserves unrelated slots and deadlines"),Out.Revision==Current.Revision+1&&Out.Slots==Current.Slots&&Out.Cooldowns==Current.Cooldowns);
    auto CrossClass=Current;CrossClass.Attributes[LDDevelopment::INT]=8;CrossClass.Attributes[LDDevelopment::WIS]=8;
    CrossClass.AttributePoints=4;CrossClass.SkillPoints=4;CrossClass.Ranks[uint8(ELDSkill::BreakSigil)]=1;
    auto CrossPlan=LDDevelopment::PlanFrom(CrossClass);CrossPlan.Attributes[LDDevelopment::INT]=11;CrossPlan.Attributes[LDDevelopment::WIS]=9;
    CrossPlan.Ranks[uint8(ELDSkill::BreakSigil)]=2;CrossPlan.Slots[4]=ELDSkill::BreakSigil;
    TestTrue(TEXT("Prepare atomically funds and equips previously learned cross-class mastery"),LDDevelopment::Prepare(CrossClass,CrossPlan,Out,Why)&&Out.Attributes==CrossPlan.Attributes&&Out.Ranks==CrossPlan.Ranks&&Out.Slots==CrossPlan.Slots&&Out.AttributePoints==0&&Out.SkillPoints==3&&Out.Cooldowns==CrossClass.Cooldowns);
    const auto Sentinel=Out;
    auto SameOutput=[&](){return FLDCharacterBuild::StaticStruct()->CompareScriptStruct(&Out,&Sentinel,0);};
    auto Stale=Plan;--Stale.ExpectedRevision;
    TestFalse(TEXT("Prepare rejects a stale expected build revision"),LDDevelopment::Prepare(Current,Stale,Out,Why));
    TestTrue(TEXT("Stale preparation preserves every reflected output field"),SameOutput()&&!Why.IsEmpty());
    auto WrongCatalog=Plan;WrongCatalog.CatalogDigest+=TEXT("-stale");
    TestFalse(TEXT("Prepare rejects a different catalog identity"),LDDevelopment::Prepare(Current,WrongCatalog,Out,Why));
    TestTrue(TEXT("Catalog refusal preserves the full prior output"),SameOutput());
    auto Unfunded=Current;Unfunded.AttributePoints=1;
    auto UnfundedPlan=LDDevelopment::PlanFrom(Unfunded);UnfundedPlan.Attributes[LDDevelopment::STR]+=2;
    TestFalse(TEXT("Prepare refuses a draft without enough natural investment points"),LDDevelopment::Prepare(Unfunded,UnfundedPlan,Out,Why));
    TestTrue(TEXT("Point refusal does not partially publish projected attributes"),SameOutput());
    auto Capstone=Current;Capstone.Level=15;Capstone.Attributes.Init(16,6);
    auto CapstonePlan=LDDevelopment::PlanFrom(Capstone);CapstonePlan.Slots[4]=ELDSkill::TemporalLock;
    TestTrue(TEXT("Capstone fixture is otherwise valid and meets the natural gate"),LDDevelopment::Valid(Capstone)&&LDGear::NaturalGate(Capstone,ELDSkill::TemporalLock));
    TestEqual(TEXT("Capstone fixture has not learned Temporal Lock"),LDDevelopment::Rank(Capstone,ELDSkill::TemporalLock),0);
    TestFalse(TEXT("Unlearned capstone cannot be activated through preparation"),LDDevelopment::Prepare(Capstone,CapstonePlan,Out,Why));
    TestTrue(TEXT("Unlearned capstone refusal preserves all output fields"),SameOutput());
    const auto AliasedBefore=Capstone;
    TestFalse(TEXT("Unlearned capstone also rejects when input and output alias"),LDDevelopment::Prepare(Capstone,CapstonePlan,Capstone,Why));
    TestTrue(TEXT("Aliased refusal leaves build, points and cooldowns unchanged"),FLDCharacterBuild::StaticStruct()->CompareScriptStruct(&Capstone,&AliasedBefore,0));
    auto ClassStart=LDDevelopment::StartingBuild(1);ClassStart.Level=2;ClassStart.SkillPoints=2;
    auto ClassPlan=LDDevelopment::PlanFrom(ClassStart);ClassPlan.Ranks[uint8(ELDSkill::Step)]=2;ClassPlan.Slots[2]=ELDSkill::Step;
    FLDCharacterBuild Trained;
    if(!TestTrue(TEXT("Class learning plus rank one-to-two charges both point costs"),LDDevelopment::Prepare(ClassStart,ClassPlan,Trained,Why)&&Trained.Ranks[uint8(ELDSkill::Step)]==2&&Trained.SkillPoints==0)){return false;}
    auto RankTwo=Trained;RankTwo.SkillPoints=1;RankTwo.Cooldowns[uint8(ELDSkill::Step)]=77;
    auto RankThreePlan=LDDevelopment::PlanFrom(RankTwo);RankThreePlan.Ranks[uint8(ELDSkill::Step)]=3;
    const auto BeforeRankRefusal=Trained;
    TestFalse(TEXT("Rank two-to-three refuses only one remaining skill point"),LDDevelopment::Prepare(RankTwo,RankThreePlan,Trained,Why));
    TestTrue(TEXT("Unaffordable rank-three draft leaves the full output unchanged"),FLDCharacterBuild::StaticStruct()->CompareScriptStruct(&Trained,&BeforeRankRefusal,0));
    RankTwo.SkillPoints=2;
    TestTrue(TEXT("Rank two-to-three consumes exactly two skill points"),LDDevelopment::Prepare(RankTwo,RankThreePlan,Trained,Why)&&Trained.Ranks[uint8(ELDSkill::Step)]==3&&Trained.SkillPoints==0);
    const auto BeforeUnequip=Trained;auto UnequippedPlan=LDDevelopment::PlanFrom(BeforeUnequip);UnequippedPlan.Slots[2]=ELDSkill::None;
    TestTrue(TEXT("Unequipping a learned slot preserves its absolute cooldown"),LDDevelopment::Prepare(BeforeUnequip,UnequippedPlan,Trained,Why)&&!LDDevelopment::Equipped(Trained,ELDSkill::Step)&&Trained.Cooldowns==BeforeUnequip.Cooldowns&&Trained.Cooldowns[uint8(ELDSkill::Step)]==77);
    return true;
}
#endif
