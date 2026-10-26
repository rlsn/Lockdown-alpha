#include "Misc/AutomationTest.h"
#include "Combat/LDRiverRules.h"
#include "Combat/LDTacticalCatalog.h"
#include "Development/LDV08Loadout.h"
#include "Inventory/LDVendorDirector.h"
#include "Net/DataBunch.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRiverReactionTest,"Lockdown.Rules.V08WetFrostBoundary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDRiverReactionTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;FState S;int64 Ready=0;const auto Id=FGuid::NewGuid();
    TestFalse(TEXT("Dry Frost does not reserve a lock"),LDRiver::AttemptFrost(S,Ready,1000));TestEqual(TEXT("Dry lock unchanged"),Ready,int64(0));
    Apply(S,EStatus::Wet,4000,Id);TestTrue(TEXT("Wet hit attempts once"),LDRiver::AttemptFrost(S,Ready,1000));
    TestEqual(TEXT("Exactly six seconds"),Ready,int64(7000));TestFalse(TEXT("Wet consumed"),S.Has(EStatus::Wet,1000));
    Apply(S,EStatus::Wet,10000,Id);TestFalse(TEXT("One millisecond before readiness cannot react"),LDRiver::AttemptFrost(S,Ready,6999));
    TestTrue(TEXT("Locked hit leaves Wet"),S.Has(EStatus::Wet,6999));TestEqual(TEXT("Denied hit cannot extend readiness"),Ready,int64(7000));
    TestTrue(TEXT("Exact endpoint inclusive"),LDRiver::AttemptFrost(S,Ready,7000));TestEqual(TEXT("Fresh six second endpoint"),Ready,int64(13000));
    Dispel(S,true);TestEqual(TEXT("Strong dispel cannot touch contender endpoint"),Ready,int64(13000));
    S={};Ready=0;Apply(S,EStatus::Wet,4000,Id);S.BlockUntil=2000;S.GuardUntil=2000;
    if(!Block(S,1000)&&LDRiver::AttemptFrost(S,Ready,1000)){GuardControl(S,EStatus::Stun,1000);}
    TestTrue(TEXT("Packet block preserves Wet"),S.Has(EStatus::Wet,1000));TestEqual(TEXT("Block preserves guard"),S.GuardUntil,int64(2000));TestEqual(TEXT("Block preserves lock"),Ready,int64(0));
    TestTrue(TEXT("Guard still permits valid reaction attempt"),LDRiver::AttemptFrost(S,Ready,1000));TestTrue(TEXT("One guard suppresses control"),GuardControl(S,EStatus::Stun,1000));TestEqual(TEXT("Guard consumed once"),S.GuardUntil,int64(0));
    TestFalse(TEXT("No Stun added by surface or suppressed attempt"),S.Has(EStatus::Stun,1000));TestEqual(TEXT("Guarded attempt still locks"),Ready,int64(7000));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRiverGeometryTest,"Lockdown.Rules.V08RiverFootprintsAndCatalog",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDRiverGeometryTest::RunTest(const FString& Parameters)
{
    const auto Line=LDRiver::Footprint(FVector(-1900,-700,92),FVector(-1300,-700,0),650,90,false);
    TestEqual(TEXT("Rectangle selects four aligned centres"),Line.Num(),4);
    TestEqual(TEXT("First selected cell is nearest along line"),LDRiver::Centre(Line[0]),FVector(-1900,-700,0));
    TestEqual(TEXT("Disk selection around grid corner"),LDRiver::Footprint({},FVector(-1600,-800,0),0,180,true).Num(),4);
    TestEqual(TEXT("Candidate footprint capped before eligibility"),LDRiver::Footprint({},FVector::ZeroVector,0,5000,true).Num(),16);
    TestTrue(TEXT("City and Heart have underlying water"),LDRiver::InRiver(FVector(-1600,-700,1))&&LDRiver::InRiver(FVector(-400,-400,1)));
    TestFalse(TEXT("Adjacent dry detour remains dry"),LDRiver::InRiver(FVector(-1600,-1400,1)));
    const auto* C=ULDTacticalCatalog::Load(8);if(!TestNotNull(TEXT("Current river catalog"),C)){return false;}
    auto B=LDDevelopment::TacticalStart(5,C->Checksum,8);TestTrue(TEXT("Arcanist knows authored Frostline"),LDDevelopment::Rank(B,ELDSkill::Frostline)==1&&B.Ranks.Num()==41);
    TestTrue(TEXT("No substituted Firebolt identity"),uint8(ELDSkill::WaterJet)==40&&!C->Find(ELDSkill(39)));
    TestTrue(TEXT("Both books reachable through active Guild catalog"),ALDVendorDirector::OfferedSkills(true,8).Contains(ELDSkill::Frostline)&&ALDVendorDirector::OfferedSkills(true,8).Contains(ELDSkill::WaterJet));
    TestEqual(TEXT("Field Practice adds once"),LDV08::TraitValue(B,TEXT("frozen_ms"),5000),6000);
    TestEqual(TEXT("Field Practice respects cap"),LDV08::TraitValue(B,TEXT("frozen_ms"),10000),10000);
    auto P=LDDevelopment::PlanFrom(B);P.Bindings.SetNum(11);FNetBitWriter Writer(nullptr,16384);bool Good=false;P.NetSerialize(Writer,nullptr,Good);TestTrue(TEXT("Extended sparse ranks serialize"),Good&&!Writer.IsError());
    FLDBuildPlan Copy;FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits());Copy.NetSerialize(Reader,nullptr,Good);TestTrue(TEXT("Frostline rank and action round-trip"),Good&&Copy.Ranks==P.Ranks&&Copy.Actions==P.Actions);
    return true;
}
