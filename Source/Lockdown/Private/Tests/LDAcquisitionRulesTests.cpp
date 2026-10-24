#include "Misc/AutomationTest.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Inventory/LDSupplyRequest.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Economy/LDEconomyLedger.h"
#include "Net/DataBunch.h"
namespace
{
    FLDInventory Bag74(){FLDInventory I;I.RulesVersion=7;I.Quick.SetNum(3);return I;}
    FGuid Add74(FLDInventory& I,ELDItemKind K){FLDInventoryItem E;E.Kind=K;E.Id=FGuid::NewGuid();E.Slot=I.Items.Num();I.Items.Add(E);return E.Id;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAcquisitionProjectionTest,"Lockdown.Rules.AcquisitionExactInputsAndWorkOrders",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAcquisitionProjectionTest::RunTest(const FString& Parameters)
{
    const auto* C=ULDAcquisitionCatalog::Load();FString Why;TestTrue(TEXT("Closed native acquisition source"),C&&C->Validate(Why));if(!C){return false;}TestEqual(TEXT("All40 acquisition definitions"),C->Items.Num(),40);TestEqual(TEXT("All12 public recipes"),C->Recipes.Num(),12);
    auto B=LDDevelopment::TacticalStart(0,FString::ChrN(64,TEXT('a')));auto I=Bag74();FLDProductionPlan P;
    Add74(I,ELDItemKind::Core);const FGuid Catalyst=Add74(I,ELDItemKind::ArcaneCatalyst);
    TestTrue(TEXT("Scarce inputs supplied; missing RunicStaff bought immediately"),ALDAcquisitionDirector::Preview(I,B,28,1,4,FGuid(),P,Why));TestEqual(TEXT("Runic48 plus recipe10"),P.Fee,58);TestEqual(TEXT("Only two owned inputs reserve; transient does not occupy bag"),P.Inputs.Num(),2);
    B.Gear.FeeDiscount=1;TestTrue(TEXT("Seal applies once"),ALDAcquisitionDirector::Preview(I,B,28,1,4,FGuid(),P,Why));TestEqual(TEXT("Only fee discounted"),P.Fee,57);
    LDInventory::Remove(I,Catalyst);TestFalse(TEXT("Work order cannot fabricate catalyst"),ALDAcquisitionDirector::Preview(I,B,28,1,4,FGuid(),P,Why));
    for(uint8 N=0;N<C->Recipes.Num();++N){I=Bag74();for(auto K:C->Recipes[N].Inputs){Add74(I,K);}const uint8 Site=C->Recipes[N].Station==2?5:C->Recipes[N].Station==1?4:2;TestTrue(TEXT("Every exact authored recipe projects"),ALDAcquisitionDirector::Preview(I,B,20+N,0,Site,FGuid(),P,Why));I.Items[0].BindingMask=1;TestFalse(TEXT("Equipped input cannot be consumed"),ALDAcquisitionDirector::Preview(I,B,20+N,0,Site,FGuid(),P,Why));}
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDQuality74Test,"Lockdown.Rules.QualityEligibilityCapsAndIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDQuality74Test::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Fine floored weapon32*1.07"),ULDAcquisitionCatalog::QualityValue(32,1),34);TestEqual(TEXT("Masterwork50*1.14"),ULDAcquisitionCatalog::QualityValue(50,2),57);
    auto I=Bag74();auto B=LDDevelopment::TacticalStart(0,FString::ChrN(64,TEXT('a')));const auto Staff=Add74(I,ELDItemKind::SimpleStaff);Add74(I,ELDItemKind::Toolkit);FLDProductionPlan P;FString Why;
    TestTrue(TEXT("Standard eligible weapon upgrades with one Toolkit"),ALDAcquisitionDirector::Preview(I,B,32,0,2,Staff,P,Why));TestEqual(TEXT("Fine fee6"),P.Fee,6);I.Items[0].Quality=1;TestFalse(TEXT("Cannot repeat Standard to Fine"),ALDAcquisitionDirector::Preview(I,B,32,0,2,Staff,P,Why));Add74(I,ELDItemKind::Crystal);TestTrue(TEXT("Fine to Masterwork needs both materials"),ALDAcquisitionDirector::Preview(I,B,33,0,4,Staff,P,Why));
    auto J=Bag74();const auto Satchel=Add74(J,ELDItemKind::WeightedSatchel);Add74(J,ELDItemKind::Toolkit);TestFalse(TEXT("No charge for a field with no craftsmanship benefit"),ALDAcquisitionDirector::Preview(J,B,32,0,2,Satchel,P,Why));I.Bindings[0]=Staff;I.Items[0].BindingMask=1;const auto Gear=LDGear::Derive(I);TestEqual(TEXT("Fine changes only weapon magnitude"),Gear.WeaponBase,34.);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorkOrderLedgerTest,"Lockdown.Rules.AtomicWorkOrderLineageAndQualityReplay",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorkOrderLedgerTest::RunTest(const FString& Parameters)
{
    using namespace LDEconomy;FLedger L;FString Why;TestTrue(TEXT("Initialize"),L.Initialize(FGuid::NewGuid(),{{TEXT("W"),100}}, {},Why));FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("EquipmentWorkOrder");T.Burn=26;T.Gold.Add({TEXT("W"),1,-26});FItemDelta Transient;Transient.Id=FGuid::NewGuid();Transient.Kind=ELDItemKind::SimpleStaff;Transient.Op=EItemOp::Transient;Transient.To=TEXT("W");T.Items.Add(Transient);FItemDelta Out;Out.Id=FGuid::NewGuid();Out.Kind=ELDItemKind::NullStaff;Out.Op=EItemOp::Create;Out.To=TEXT("W");Out.Parents.Add(Transient.Id);T.Items.Add(Out);if(!TestTrue(TEXT("Single boundary creates+consumes purchased input and creates parented output"),L.Commit(T,Why))){AddError(Why);return false;}TestTrue(TEXT("Transient consumed without world actor"),L.Item(Transient.Id)->Consumed);TestTrue(TEXT("Replay"),L.Reconcile(Why));
    FTransaction Q;Q.Id=FGuid::NewGuid();Q.Cause=TEXT("EquipmentQuality");FItemDelta Upgrade;Upgrade.Id=Out.Id;Upgrade.Kind=Out.Kind;Upgrade.Op=EItemOp::Quality;Upgrade.From=Upgrade.To=TEXT("W");Upgrade.Expected=1;Upgrade.Quality=1;Q.Items.Add(Upgrade);TestTrue(TEXT("Same-ID quality mutation"),L.Commit(Q,Why));TestEqual(TEXT("Parent lineage unchanged"),L.Item(Out.Id)->Parents.Num(),1);TestTrue(TEXT("Idempotent replay"),L.Commit(Q,Why));Q.Id=FGuid::NewGuid();TestFalse(TEXT("Stale quality cannot spend twice"),L.Commit(Q,Why));TestTrue(TEXT("Final reconcile"),L.Reconcile(Why));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAcquisitionWireTest,"Lockdown.Rules.AcquisitionBoundedWire",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDAcquisitionWireTest::RunTest(const FString& Parameters)
{
    FLDProductionPlan P;P.Site=4;P.Recipe=28;P.Mode=1;P.CatalogDigest=FString::ChrN(64,TEXT('a'));for(int32 N=0;N<4;++N){FLDInventoryItem I;I.Id=FGuid::NewGuid();P.Inputs.Add(I);}FNetBitWriter W(nullptr,8192);bool Good=false;P.NetSerialize(W,nullptr,Good);TestTrue(TEXT("Four physical inputs bounded"),Good);FLDProductionPlan Copy;FNetBitReader R(nullptr,W.GetData(),W.GetNumBits());Copy.NetSerialize(R,nullptr,Good);TestTrue(TEXT("Round trip digest and inputs"),Good&&Copy.Inputs.Num()==4&&Copy.CatalogDigest==P.CatalogDigest);P.Inputs.Add(FLDInventoryItem());FNetBitWriter Bad(nullptr,8192);P.NetSerialize(Bad,nullptr,Good);TestFalse(TEXT("Fifth input rejected before allocation"),Good);
    FLDSupplyRequest S;S.Match=S.Life=S.Token=S.InventoryRevision=1;S.Digest=FString::ChrN(65,TEXT('a'));FNetBitWriter B(nullptr,8192);S.NetSerialize(B,nullptr,Good);TestFalse(TEXT("Oversized digest rejected"),Good);return true;
}
