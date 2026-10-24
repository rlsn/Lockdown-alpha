#include "Misc/AutomationTest.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDTacticalGeometry.h"
#include "Combat/LDTacticalCatalog.h"
#include "Combat/LDTacticalRequest.h"
#include "Net/DataBunch.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDSmokeTest,"Lockdown.Rules.SmokeUnionGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDSmokeTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;const TArray<FSmokeCircle> C={{{0,0,0},100},{{150,0,0},100}};
    TestTrue(TEXT("Entry occludes"),SmokeOccludes({-200,0,0},{0,0,0},C));
    TestTrue(TEXT("Outside to outside transit occludes"),SmokeOccludes({-200,0,0},{300,0,0},C));
    TestFalse(TEXT("Connected overlap union permits internal sight"),SmokeOccludes({-90,0,0},{240,0,0},C));
    TestFalse(TEXT("Tangent is not an interior crossing"),SmokeOccludes({-200,100,0},{300,100,0},C));
    TestTrue(TEXT("Gap between separate clouds crosses boundary"),SmokeOccludes({0,0,0},{400,0,0},{{{0,0,0},100},{{400,0,0},100}}));
    TestFalse(TEXT("Empty space remains clear"),SmokeOccludes({-200,200,0},{300,200,0},C));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlTest,"Lockdown.Rules.ControlProtectionAndBlind",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDControlTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;FState S;const auto Source=FGuid::NewGuid();S.ConcealedUntil=20000;
    TestTrue(TEXT("Preguard activates"),ActivateGuard(S,1000));TestEqual(TEXT("Activation breaks concealment"),S.ConcealedUntil,int64(0));
    TestFalse(TEXT("No blanket silence immunity"),GuardControl(S,EStatus::Silence,1100));
    TestTrue(TEXT("Next hard component stripped"),GuardControl(S,EStatus::Stun,1100));TestFalse(TEXT("Second hard component survives"),GuardControl(S,EStatus::Hex,1100));
    TestFalse(TEXT("Eight second family persists after consumption"),ActivateGuard(S,8999));TestTrue(TEXT("Exact family endpoint ready"),ActivateGuard(S,9000));TestFalse(TEXT("Guard expires at 1500ms"),GuardControl(S,EStatus::Hex,10500));
    Apply(S,EStatus::Hex,20000,Source);TestTrue(TEXT("Hex permits walk"),Allows(S,EAction::Move,12000));TestFalse(TEXT("Hex blocks skills/items/services"),Allows(S,EAction::Skill,12000)||Allows(S,EAction::Item,12000)||Allows(S,EAction::Service,12000));
    Dispel(S,true);Apply(S,EStatus::Break,20000,Source);TestFalse(TEXT("Only tagged breakable passive suppressed"),PassiveActive(S,true,12000));TestTrue(TEXT("Ordinary stats and untagged passives retained"),PassiveActive(S,false,12000));
    Apply(S,EStatus::Blind,20000,Source);TestEqual(TEXT("Blind owns body radius only"),BodySight(S,12000),200.f);TestEqual(TEXT("Blind endpoint restores body"),BodySight(S,20000),720.f);
    S.ConcealedUntil=30000;S.GuardUntil=20000;S.AnchorUntil=20000;S.Barrier=100;Dispel(S,false,true);
    TestTrue(TEXT("Basic positive dispel removes ordinary barrier and Anchor only"),S.Barrier==0&&S.AnchorUntil==0&&S.ConcealedUntil==30000&&S.GuardUntil==20000);
    S.GroundedUntil=20000;Dispel(S,false);TestFalse(TEXT("Source-bound grounding resists basic dispel"),Allows(S,EAction::Dash,12000));TestTrue(TEXT("Grounding permits walking"),Allows(S,EAction::Move,12000));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGroundWireTest,"Lockdown.Rules.GroundWireAndNaturalGates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDGroundWireTest::RunTest(const FString& Parameters)
{
    const auto* C=ULDTacticalCatalog::Load();if(!TestNotNull(TEXT("B72 catalog"),C)){return false;}TestEqual(TEXT("Only fourteen delivered definitions"),C->Abilities.Num(),14);
    auto B=LDDevelopment::TacticalStart(0,C->Checksum);B.Attributes={8,8,8,9,8,8};
    TestTrue(TEXT("OR INT branch alone qualifies Force Wave"),ULDTacticalCatalog::NaturalGate(*C->Find(ELDSkill::ForceWave),B));
    B.Attributes[3]=8;TestFalse(TEXT("Below both natural branches"),ULDTacticalCatalog::NaturalGate(*C->Find(ELDSkill::ForceWave),B));
    B.Attributes[4]=9;TestTrue(TEXT("AND Remedy clause all met"),ULDTacticalCatalog::NaturalGate(*C->Find(ELDSkill::RemedyBind),B));B.Attributes[2]=6;TestFalse(TEXT("Missing one AND threshold rejects"),ULDTacticalCatalog::NaturalGate(*C->Find(ELDSkill::RemedyBind),B));
    FLDTacticalRequest R;R.CatalogDigest=C->Checksum;R.Match=R.Life=R.Token=R.BuildRevision=1;R.TargetMode=3;R.Point=FVector(123.4,-99.2,0);bool Good=false;FNetBitWriter W(nullptr,4096);R.NetSerialize(W,nullptr,Good);FLDTacticalRequest Copy;FNetBitReader Reader(nullptr,W.GetData(),W.GetNumBits());Copy.NetSerialize(Reader,nullptr,Good);TestTrue(TEXT("Ground request roundtrip without hidden actor identity"),Good&&Copy.TargetMode==3&&!Copy.Observed.IsValid()&&FVector(Copy.Point).Equals(R.Point,.11));
    R.WireVersion=1;FNetBitWriter Old(nullptr,4096);R.NetSerialize(Old,nullptr,Good);TestFalse(TEXT("Old wire cannot alias ground schema"),Good);
    R.WireVersion=2;R.Point=FVector(25000,0,0);FNetBitWriter Huge(nullptr,4096);R.NetSerialize(Huge,nullptr,Good);TestFalse(TEXT("Unbounded point rejects"),Good);return true;
}
