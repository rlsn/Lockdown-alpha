#include "Misc/AutomationTest.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Combat/LDTacticalCatalog.h"
#include "Development/LDV08Modifiers.h"
#include "Combat/LDEnvironmentRules.h"
#include "Combat/LDTacticalRequest.h"
#include "Net/DataBunch.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB84CatalogTest,"Lockdown.Rules.V08CompleteCatalogAndRecipes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB84CatalogTest::RunTest(const FString& Parameters)
{
    const auto* C=ULDAcquisitionCatalog::Load(8);const auto* G=ULDEquipmentCatalog::Load(8);const auto* S=ULDTacticalCatalog::Load(8);FString Why;if(!TestTrue(TEXT("All checksum-closed native payloads load"),C&&G&&S)){return false;}TestTrue(TEXT("Validate independently reimports complete derived fields"),C->Validate(Why)&&G->Validate(Why)&&S->Validate(Why));TestEqual(TEXT("Full active ability catalog"),S->Abilities.Num(),52);TestEqual(TEXT("Full item catalog"),G->Items.Num(),80);TestEqual(TEXT("Complete recipe graph"),C->Recipes.Num(),20);
    auto B=LDDevelopment::TacticalStart(5,S->Checksum,8);FLDInventory I;I.RulesVersion=8;I.Bindings.SetNum(11);I.Quick.Reset();
    for(int32 N=0;N<C->Recipes.Num();++N){const auto& R=C->Recipes[N];FLDInventory Bag=I;for(auto K:R.Inputs){FLDInventoryItem E;E.Id=FGuid::NewGuid();E.Kind=K;E.Slot=Bag.Items.Num();Bag.Items.Add(E);}const uint8 Code=C->RecipeCode(N),Site=R.Station==2?5:R.Station==1?4:2;TestEqual(TEXT("Recipe code avoids quality/cache command aliases"),C->RecipeIndex(Code),N);FLDProductionPlan P;TestTrue(*R.Id,ALDAcquisitionDirector::Preview(Bag,B,Code,0,Site,{},P,Why));TestEqual(TEXT("Every exact input reserved once"),P.Inputs.Num(),R.Inputs.Num());if(!Bag.Items.IsEmpty()){Bag.Items[0].BindingMask=1;TestFalse(TEXT("Equipped inputs cannot enter assembly"),ALDAcquisitionDirector::Preview(Bag,B,Code,0,Site,{},P,Why));}}
    FLDInventoryItem D;D.Kind=ELDItemKind::ResonanceRing;D.Id=FGuid::NewGuid();I.Items.Add(D);FLDProductionPlan P;TestFalse(TEXT("Modifier-only ring cannot spend quality materials"),ALDAcquisitionDirector::Preview(I,B,32,0,2,D.Id,P,Why));
    FLDTacticalRequest R;R.Schema=8;R.WireVersion=4;R.TargetMode=10;R.Match=R.Life=R.Token=R.BuildRevision=1;R.CatalogDigest=S->Checksum;R.SourceDefinition=TEXT("sabotage");FNetBitWriter W(nullptr,8192);bool Good=false;R.NetSerialize(W,nullptr,Good);TestTrue(TEXT("Construct targeting crosses schema8 bounded wire"),Good);FLDTacticalRequest Read;FNetBitReader Reader(nullptr,W.GetData(),W.GetNumBits());Read.NetSerialize(Reader,nullptr,Good);TestTrue(TEXT("Construct targeting preserves source identity and mode"),Good&&Read.TargetMode==10&&Read.SourceDefinition==TEXT("sabotage"));R.Schema=7;R.WireVersion=3;FNetBitWriter Bad(nullptr,8192);R.NetSerialize(Bad,nullptr,Good);TestFalse(TEXT("Schema7 does not acquire new target modes"),Good);
    TestEqual(TEXT("Schema7 recipes remain isolated"),ULDAcquisitionCatalog::Load()->Recipes.Num(),12);TestEqual(TEXT("Schema7 rank wire remains38"),LDDevelopment::V07SkillCount,38);TestEqual(TEXT("Schema8 reserves all62 identities"),LDDevelopment::V08SkillCount,62);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB84SurfaceUtilities,"Lockdown.Rules.V08SelectiveTerrainUtilities",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB84SurfaceUtilities::RunTest(const FString& Parameters)
{
    using namespace LDEnvironment;FCell C;FCommand Q;Q.State.Created=1000;Q.State.Until=6000;Q.State.Owner=Q.State.BudgetOwner=FGuid::NewGuid();Q.State.Life=FGuid::NewGuid();Q.Input=EInput::Water;Transition(C,Q,Stone,1000);Q.Input=EInput::Frost;Q.FrozenBonus=2000;Transition(C,Q,Stone,1000);TestEqual(TEXT("Independent trait and glove families snapshot once"),C.Frozen.Until,int64(8000));Q.Input=EInput::Thaw;Q.SteamBonus=1000;TestTrue(TEXT("Thaw consumes only ice, adds Wet/Steam and no direct Fire"),Transition(C,Q,Stone,1000)&&C.Frozen.Until==0&&C.Burning.Until==0&&C.Obscured.Until==5000);
    Q.Input=EInput::Lightning;Transition(C,Q,Stone,1000);Q.Input=EInput::Purify;TestTrue(TEXT("Purifier removes electricity while preserving Wet/Steam"),Transition(C,Q,Stone,1000)&&!C.Electrified.Until&&C.Wet.Until>1000&&C.Obscured.Until>1000);Q.Input=EInput::Thaw;TestFalse(TEXT("Thaw cannot fabricate smoke on dry/unfrozen material"),Transition(C,Q,Stone,1000));return true;
}
