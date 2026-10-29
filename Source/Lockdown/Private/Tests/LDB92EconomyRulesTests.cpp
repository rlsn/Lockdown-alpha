#include "Economy/LDEconomyCatalog.h"
#include "Development/LDCharacterBuild.h"
#include "Combat/LDTacticalCatalog.h"
#include "Vision/LDViewTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB92CatalogTest,"Lockdown.Rules.B92EconomyCatalog",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB92CatalogTest::RunTest(const FString& Parameters)
{
    LDEconomy::FCatalog Full,Legacy;FString Error,Json;
    if(!TestTrue(TEXT("Selected full catalog loads with its exact pinned digest"),Full.LoadFull(Error))){AddError(Error);return false;}
    TestTrue(TEXT("Legacy catalog remains an independent schema1 domain"),Legacy.Load(Error));
    TestEqual(TEXT("Legacy pool count retained"),Legacy.Markets.Num(),3);
    TestEqual(TEXT("Legacy courier budget retained"),Legacy.CourierLimit,8);
    TestEqual(TEXT("Full catalog has five pools"),Full.Markets.Num(),5);
    TestEqual(TEXT("Full catalog fixed N"),Full.Contenders,24);
    TestEqual(TEXT("Full duration"),Full.Duration,5400);
    TestEqual(TEXT("Full deposit charges"),Full.Nodes*Full.Charges,54);
    TestEqual(TEXT("Full opportunities"),Full.InstitutionTimes.Num(),29);
    TestEqual(TEXT("Full courier paid cap"),Full.CourierLimit,20);
    TestEqual(TEXT("Full courier issued cap"),Full.CourierIssuanceLimit,40);
    int32 Stock=0,States=0;int64 Gold=0;
    for(const auto& M:Full.Markets){Gold+=M.Gold;for(const auto& G:M.Goods){Stock+=G.Target;for(int32 S=0;S<=G.Cap;++S)
    {
        ++States;for(const auto& Ratio:TArray<TPair<int32,int32>>{{1,1},{5,4}})
        {
            const int32 A=LDEconomy::Ask(G,S,Ratio.Key,Ratio.Value),B=LDEconomy::Bid(G,S,Ratio.Key,Ratio.Value);
            TestTrue(TEXT("Unit prices bounded and nonprofitable"),A>0&&B>=0&&B<A&&(S==0||LDEconomy::Bid(G,S-1,Ratio.Key,Ratio.Value)<A)&&(S==G.Cap||B<LDEconomy::Ask(G,S+1,Ratio.Key,Ratio.Value)));
        }
    }}}
    TestEqual(TEXT("Five finite reserves"),Gold,int64(3000));TestEqual(TEXT("Initial physical commodity count"),Stock,426);TestEqual(TEXT("All stock states checked at normal and shortage ratios"),States,872);
    FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/economy_b92.json")));const auto Digest=Full.Checksum;
    for(const auto& Mutation:TArray<TPair<FString,FString>>{{TEXT("\"contendersMax\": 24"),TEXT("\"contendersMax\": 12")},{TEXT("\"perContender\": 20"),TEXT("\"perContender\": 21")},{TEXT("\"issuance_limit\": 40"),TEXT("\"issuance_limit\": 41")},{TEXT("\"gold\": 600"),TEXT("\"gold\": 601")},{TEXT("\"initialTotalGold\": 3288"),TEXT("\"initialTotalGold\": 3289")},{TEXT("\"maxProducerBurn\": 145"),TEXT("\"maxProducerBurn\": 146")},{TEXT("\"shared_per_contender_account\": true"),TEXT("\"shared_per_contender_account\": false")},{TEXT("5220"),TEXT("5221")}})
    {const auto Changed=Json.Replace(*Mutation.Key,*Mutation.Value);TestTrue(TEXT("Mutation addresses real catalog bytes"),Changed!=Json);TestFalse(TEXT("Invalid full-profile economics rejects"),Full.Parse(Changed,Error));TestEqual(TEXT("Failed parse leaves current catalog intact"),Full.Checksum,Digest);}
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB92StarterAuditTest,"Lockdown.Rules.B92InheritedStarterAudit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB92StarterAuditTest::RunTest(const FString& Parameters)
{
    const auto* Catalog=ULDTacticalCatalog::Load(8);if(!TestNotNull(TEXT("Inherited actual v08 tactical catalog"),Catalog)){return false;}
    FLDOwnerState O;TestEqual(TEXT("Owner wallet default"),O.Gold,int64(0));TestEqual(TEXT("Owner bank default"),O.BankGold,int64(0));TestTrue(TEXT("Owner has no physical starter inventory"),O.Inventory.Items.IsEmpty());
    for(uint8 Preset=0;Preset<9;++Preset)
    {
        const auto B=LDDevelopment::TacticalStart(Preset,Catalog->Checksum,8);
        TestEqual(TEXT("Starts at level1"),int32(B.Level),1);TestEqual(TEXT("Six mixed action slots retained"),B.Actions.Num(),6);
        TestTrue(TEXT("Starter abilities exist in learned ranks, not cloned books/items"),B.Slots.ContainsByPredicate([](ELDSkill S){return S!=ELDSkill::None;}));
    }
    TestEqual(TEXT("Heave inherited channel seconds"),LDDevelopment::ChannelSeconds(ELDSkill::Heave),2.);
    TestEqual(TEXT("Parley inherited channel seconds"),LDDevelopment::ChannelSeconds(ELDSkill::Parley),2.);
    TestEqual(TEXT("Analyze inherited channel seconds"),LDDevelopment::ChannelSeconds(ELDSkill::Analyze),2.);
    TestEqual(TEXT("Survey inherited channel seconds"),LDDevelopment::ChannelSeconds(ELDSkill::Survey),1.);
    TestEqual(TEXT("Heave inherited cooldown seconds"),LDDevelopment::Cooldown(ELDSkill::Heave),6.);
    TestEqual(TEXT("Parley inherited cooldown seconds"),LDDevelopment::Cooldown(ELDSkill::Parley),8.);
    return true;
}
#endif
