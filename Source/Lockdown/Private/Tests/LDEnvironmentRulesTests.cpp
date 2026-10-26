#include "Misc/AutomationTest.h"
#include "Combat/LDEnvironmentRules.h"
#include "Vision/LDViewTypes.h"
#include "Net/DataBunch.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDEnvironmentReactions,"Lockdown.Rules.V08TenEnvironmentalReactions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDEnvironmentReactions::RunTest(const FString& Parameters)
{
    using namespace LDEnvironment;FCell C;FCommand Q;Q.State.Owner=FGuid::NewGuid();Q.State.BudgetOwner=Q.State.Owner;Q.State.Life=FGuid::NewGuid();int64 At=1000;
    auto Apply=[&](EInput I,uint8 Base,int32 Duration=5000){Q.Input=I;Q.State.Cast=FGuid::NewGuid();Q.State.Created=At;Q.State.Until=At+Duration;return Transition(C,Q,Base,At);};
    TestFalse(TEXT("Dry Frost is invalid"),Apply(EInput::Frost,Stone));
    TestTrue(TEXT("Water applies Wet"),Apply(EInput::Water,Stone));TestTrue(TEXT("water_frost"),Apply(EInput::Frost,Stone)&&C.Frozen.Until>At&&C.Wet.Until>At);
    TestTrue(TEXT("ice_fire makes Wet/Steam without recursive Burning"),Apply(EInput::Fire,Flammable)&&C.Frozen.Until==0&&C.Obscured.Until==At+3000&&C.Burning.Until==0);
    C={};TestTrue(TEXT("water_fire"),Apply(EInput::Fire,Water)&&C.Obscured.Until==At+3000&&C.Burning.Until==0);
    C={};TestTrue(TEXT("water_lightning"),Apply(EInput::Lightning,Water,3000)&&C.Electrified.Magnitude==18);
    Apply(EInput::Frost,Water);TestFalse(TEXT("Frozen rejects electrical state"),Apply(EInput::Lightning,Water));TestEqual(TEXT("Frost removed old electricity"),C.Electrified.Until,int64(0));
    C={};Apply(EInput::Oil,Stone,6000);TestTrue(TEXT("oil_fire consumes finite Oil"),Apply(EInput::Fire,Stone)&&C.Oil.Until==0&&C.Burning.Until==At+6000);
    TestTrue(TEXT("water_extinguish does not restore Oil"),Apply(EInput::Water,Stone)&&C.Burning.Until==0&&C.Oil.Until==0);
    C={};TestTrue(TEXT("growth_soil"),Apply(EInput::Nature,Soil,6000)&&C.Overgrown.Until==At+6000);
    TestTrue(TEXT("plant_fire"),Apply(EInput::Fire,Soil)&&C.Overgrown.Until==0&&C.Burning.Until==At+3000);
    C={};TestFalse(TEXT("Native scenery is not arbitrary fuel"),Apply(EInput::Fire,Stone));TestEqual(TEXT("Invalid fuel creates no Burning"),C.Burning.Until,int64(0));
    C={};Apply(EInput::Smoke,Stone,5000);TestTrue(TEXT("wind_obscured"),Apply(EInput::Wind,Stone)&&C.Obscured.Until==0);
    C={};Apply(EInput::Frost,Water);TestTrue(TEXT("ice_impact"),Apply(EInput::Shatter,Water)&&C.Frozen.Until==0&&C.Wet.Until==At+3000);
    C={};TestFalse(TEXT("Nature requires authored soil"),Apply(EInput::Nature,Stone));TestFalse(TEXT("Oil cannot cover a river"),Apply(EInput::Oil,Water));
    C={};Apply(EInput::Water,Stone);TestFalse(TEXT("Oil requires dry substrate, including temporary Wet"),Apply(EInput::Oil,Stone));
    C={};Apply(EInput::Water,Stone,1000);Apply(EInput::Frost,Stone,5000);C.Expire(2500);TestTrue(TEXT("Frozen retains temporary Wet substrate without extending an expired Wet record"),C.Wet.Until==0&&C.Frozen.WetSubstrate&&(C.Mask(2500)&1));
    C={};Q.FrozenBonus=1000;Apply(EInput::Frost,Water,10000);TestEqual(TEXT("Snapshot bonuses clamp to state cap"),C.Frozen.Until,At+10000);
    const auto FirstOwner=Q.State.BudgetOwner;Q.State.Owner=FGuid::NewGuid();Q.State.BudgetOwner=FGuid::NewGuid();Apply(EInput::Water,Stone,10000);TestTrue(TEXT("Different records in a cell charge distinct sponsors"),C.Owned(FirstOwner,At)&&C.Owned(Q.State.BudgetOwner,At));
    Q.State.BudgetOwner=FGuid::NewGuid();const uint32 Before=C.Wet.Revision;Apply(EInput::Water,Stone,1000);TestTrue(TEXT("Overwrite changes sponsor once and advances revision without adding intensity"),C.Wet.Revision>Before&&C.Wet.BudgetOwner==Q.State.BudgetOwner&&C.Wet.Until<=At+10000);
    C.Expire(11000);TestEqual(TEXT("Inclusive expiry clears all allocations"),C.Mask(11000),uint8(0));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDEnvironmentSight,"Lockdown.Rules.V08ObscuredUnionAndBoundedWire",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDEnvironmentSight::RunTest(const FString& Parameters)
{
    using namespace LDEnvironment;TSet<FIntPoint> Cloud={LDRiver::Cell(FVector(-1300,-900,0)),LDRiver::Cell(FVector(-1100,-900,0))};
    const FVector A(-1300,-900,0),B(-1100,-900,0),Outside(-1600,-900,0),Far(-800,-900,0);
    TestFalse(TEXT("Adjacent clouds union, internal shared boundary is transparent"),Occludes(A,B,Cloud));
    TestTrue(TEXT("External boundary symmetric"),Occludes(Outside,A,Cloud)&&Occludes(A,Outside,Cloud));
    TestTrue(TEXT("Both endpoints outside cannot see through cloud"),Occludes(Outside,Far,Cloud));
    Cloud.Remove(LDRiver::Cell(B));TestTrue(TEXT("Partial clearing does not grant inside immunity"),Occludes(A,B,Cloud));Cloud.Reset();TestFalse(TEXT("Cleared path restores sight"),Occludes(Outside,Far,Cloud));
    TestEqual(TEXT("Smoke/global footprint capped32"),Mask({},FVector::ZeroVector,0,2000,true,32).Num(),32);
    TestTrue(TEXT("Cone is bounded and deterministic"),Mask(FVector(-1900,-700,0),FVector(-1500,-700,0),450,60,false,16,true)==Mask(FVector(-1900,-700,0),FVector(-1500,-700,0),450,60,false,16,true));
    FLDWorldView V;V.Generation=1;V.Revision=1;V.Current.Init(0,LDVision::MaskBytes);V.Explored.Init(0,LDVision::MaskBytes);
    for(int32 I=0;I<512;++I){FLDObservedEntity E;E.Id=FGuid::NewGuid();E.Kind=ELDObservedKind::TacticalField;E.Position=FVector(0,0,5);E.Presentation=18;E.ActionCue=20;V.Entities.Add(E);}
    FNetBitWriter Writer(nullptr,1<<20);bool Good=false;V.NetSerialize(Writer,nullptr,Good);TestTrue(TEXT("Bounded extended authorized tuple count serializes"),Good);
    FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits());FLDWorldView Read;Read.NetSerialize(Reader,nullptr,Good);TestTrue(TEXT("Filtered cues retain only public condition bits"),Good&&Read.Entities.Num()==512&&Read.Entities[0].ActionCue==20);
    const auto Extra=V.Entities[0];V.Entities.Add(Extra);FNetBitWriter Bad(nullptr,1<<20);V.NetSerialize(Bad,nullptr,Good);TestFalse(TEXT("Excess observation count fails before allocation"),Good);return true;
}
