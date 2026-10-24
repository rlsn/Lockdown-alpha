#include "Misc/AutomationTest.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDTacticalCatalog.h"
#include "Combat/LDTacticalRequest.h"
#include "Net/DataBunch.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDTacticalStatusTest,"Lockdown.Rules.TacticalStatus",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDTacticalStatusTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;FState S;const FGuid A=FGuid::NewGuid(),B=FGuid::NewGuid();
    Apply(S,EStatus::Silence,1200,A);
    TestFalse(TEXT("Silence blocks Purge Touch"),Allows(S,EAction::Skill,0));TestTrue(TEXT("Silence allows Null Bell"),Allows(S,EAction::Item,0));
    Dispel(S,false);Apply(S,EStatus::Mute,1200,A);
    TestTrue(TEXT("Mute allows Purge Touch"),Allows(S,EAction::Skill,0));TestFalse(TEXT("Mute blocks Null Bell"),Allows(S,EAction::Item,0));
    Apply(S,EStatus::Stun,1200,A);Apply(S,EStatus::Stun,1800,B);Dispel(S,false);
    TestFalse(TEXT("Basic dispel cannot remove stun"),Allows(S,EAction::Skill,1200));TestFalse(TEXT("Stunned self item forbidden"),Allows(S,EAction::Item,1200));
    TestTrue(TEXT("Overlap ends at maximum endpoint, not summed duration"),Allows(S,EAction::Skill,1800));
    Dispel(S,true);TestTrue(TEXT("External strong dispel removes hard status"),Allows(S,EAction::Skill,0));
    Apply(S,EStatus::Root,2000,A);TestFalse(TEXT("Root blocks movement"),Allows(S,EAction::Move,0));TestFalse(TEXT("Root blocks dash"),Allows(S,EAction::Dash,0));
    TestTrue(TEXT("Root permits attack"),Allows(S,EAction::Attack,0));TestTrue(TEXT("Root permits casting"),Allows(S,EAction::Skill,0));
    Apply(S,EStatus::Disarm,2000,A);TestFalse(TEXT("Disarm blocks attack"),Allows(S,EAction::Attack,0));TestTrue(TEXT("Disarm permits skills"),Allows(S,EAction::Skill,0));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDTacticalResourceTest,"Lockdown.Rules.TacticalResources",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDTacticalResourceTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;auto B=LDDevelopment::TacticalStart(0,FString::ChrN(64,TEXT('a')));FState S;double HP=650;
    TestEqual(TEXT("Profile HP"),LDDevelopment::MaxHP(B),650.);TestEqual(TEXT("Profile MP"),MaxMP(B),180.);
    Barrier(S,1000,650,3000,0);TestEqual(TEXT("Barrier capped at 40 percent"),S.Barrier,260.);
    Barrier(S,200,650,4000,0);TestEqual(TEXT("Weaker pool does not refill or stack"),S.Barrier,260.);
    Damage(HP,S,100,EDamage::Physical,1000,.15,0);TestEqual(TEXT("60 percent mitigation cap precedes barrier"),S.Barrier,220.);TestEqual(TEXT("Barrier before HP"),HP,650.);
    Damage(HP,S,300,EDamage::Pure,1000,.6,0);TestEqual(TEXT("Pure still hits barrier"),HP,570.);
    S.BlockUntil=1000;TestTrue(TEXT("One hostile targeted packet blocked"),Block(S,0));TestFalse(TEXT("No second block"),Block(S,0));
    S.MP=100;S.LastRegen=0;Regenerate(HP,S,650,180,7);Regenerate(HP,S,650,180,1000);
    TestEqual(TEXT("Elapsed fixed-point HP regen"),HP,571.);TestEqual(TEXT("Elapsed MP regen"),S.MP,102.);
    int64 XP=0;LDDevelopment::AwardXP(B,XP,300);TestEqual(TEXT("300 XP still level 4 in selected profile"),B.Level,4);TestEqual(TEXT("One attribute per earned level"),B.AttributePoints,3);
    const auto Legacy=LDDevelopment::StartingBuild(0);TestEqual(TEXT("Regression HP unchanged"),LDDevelopment::MaxHP(Legacy),110.);TestEqual(TEXT("Regression four slots"),Legacy.Slots.Num(),4);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDTacticalWireTest,"Lockdown.Rules.TacticalWireAndCatalog",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDTacticalWireTest::RunTest(const FString& Parameters)
{
    const auto* Catalog=ULDTacticalCatalog::Load();FString Why;TestNotNull(TEXT("Native imported B71 catalog"),Catalog);if(!Catalog){return false;}
    TestTrue(TEXT("Imported fields agree with checked source"),Catalog->Validate(Why));
    auto B=LDDevelopment::TacticalStart(2,Catalog->Checksum);B.Ranks[uint8(ELDSkill::PurgeTouch)]=1;B.Slots[4]=ELDSkill::PurgeTouch;
    auto Plan=LDDevelopment::PlanFrom(B);FNetBitWriter Writer(nullptr,4096);bool Good=false;Plan.NetSerialize(Writer,nullptr,Good);TestTrue(TEXT("Version 7 plan accepts high skill ID and slot five"),Good);
    FLDBuildPlan Read;FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits());Read.NetSerialize(Reader,nullptr,Good);
    TestTrue(TEXT("Versioned plan roundtrip"),Good&&Read.RulesVersion==7&&Read.CatalogDigest==B.CatalogDigest&&Read.Slots[4]==ELDSkill::PurgeTouch&&Read.Ranks.Num()==38);
    Plan.Slots[4]=ELDSkill(255);FNetBitWriter Invalid(nullptr,4096);Plan.NetSerialize(Invalid,nullptr,Good);TestFalse(TEXT("Unknown 16-bit skill ID rejects"),Good);
    FLDTacticalRequest Request;Request.CatalogDigest=Catalog->Checksum;Request.Match=Request.Life=Request.Token=Request.BuildRevision=1;Request.Slot=4;
    FNetBitWriter ActionWriter(nullptr,4096);Request.NetSerialize(ActionWriter,nullptr,Good);FLDTacticalRequest Copy;FNetBitReader ActionReader(nullptr,ActionWriter.GetData(),ActionWriter.GetNumBits());Copy.NetSerialize(ActionReader,nullptr,Good);
    TestTrue(TEXT("Bounded action header/digest roundtrip"),Good&&Copy.Slot==4&&Copy.CatalogDigest==Catalog->Checksum);
    Request.Schema=6;FNetBitWriter WrongSchema(nullptr,4096);Request.NetSerialize(WrongSchema,nullptr,Good);TestFalse(TEXT("Mixed-profile schema rejects"),Good);
    auto* Bad=NewObject<ULDTacticalCatalog>();FString Corrupt=Catalog->SourceJson.Replace(TEXT("projectile_damage"),TEXT("unimplemented_operation"));TestFalse(TEXT("Unknown operation fails import"),Bad->Import(Corrupt,Why));
    FLDCharacterBuild Result;Plan=LDDevelopment::PlanFrom(B);Plan.CatalogDigest=FString::ChrN(64,TEXT('b'));TestFalse(TEXT("Cross-catalog build cannot commit"),LDDevelopment::Prepare(B,Plan,Result,Why));
    return true;
}
