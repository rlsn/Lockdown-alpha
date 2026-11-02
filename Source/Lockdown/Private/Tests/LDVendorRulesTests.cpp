#include "Inventory/LDVendorDirector.h"
#include "Inventory/LDVendorTypes.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDVendorPrivacyTest,"Lockdown.Rules.VendorPrivacySchema",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDVendorPrivacyTest::RunTest(const FString& Parameters)
{
    auto Fields=[&](UStruct* Struct,const TSet<FName>& Allowed)
    {
        int32 Count=0;
        for(TFieldIterator<FProperty> It(Struct);It;++It)
        {
            ++Count;
            TestTrue(TEXT("Vendor wire view contains only the explicit quote allowlist"),Allowed.Contains(It->GetFName()));
            TestFalse(TEXT("No actor or controller reference enters the quote"),It->IsA<FObjectPropertyBase>());
        }
        TestEqual(TEXT("Complete vendor wire field boundary"),Count,Allowed.Num());
    };
    Fields(FLDVendorQuote::StaticStruct(),{TEXT("Match"),TEXT("Life"),TEXT("Token"),TEXT("Session"),TEXT("CatalogRevision"),TEXT("Offers")});
    Fields(FLDVendorOffer::StaticStruct(),{TEXT("Skill"),TEXT("Price")});
    const auto* Offers=FindFProperty<FArrayProperty>(FLDVendorQuote::StaticStruct(),TEXT("Offers"));
    const auto* OfferType=Offers?CastField<FStructProperty>(Offers->Inner):nullptr;
    TestTrue(TEXT("Nested offers cannot carry another inventory or canonical item"),OfferType&&OfferType->Struct==FLDVendorOffer::StaticStruct());
    TestFalse(TEXT("Authoritative vendor state is not replicated"),GetDefault<ALDVendorDirector>()->GetIsReplicated());
    FLDVendorQuote Quote;Quote.Match=3;Quote.Life=5;Quote.Token=7;Quote.Session=FGuid::NewGuid();Quote.CatalogRevision=11;
    FLDVendorOffer Offer;Offer.Skill=ELDSkill::Spark;Offer.Price=13;Quote.Offers.Add(Offer);
    TArray<uint8> Bytes;FMemoryWriter Writer(Bytes);FLDVendorQuote::StaticStruct()->SerializeItem(Writer,&Quote,nullptr);
    FLDVendorQuote Read;FMemoryReader Reader(Bytes);FLDVendorQuote::StaticStruct()->SerializeItem(Reader,&Read,nullptr);
    TestTrue(TEXT("Quote roundtrip preserves exact request, life, session, revision and displayed terms"),!Writer.IsError()&&!Reader.IsError()&&Read.Match==Quote.Match&&Read.Life==Quote.Life&&Read.Token==Quote.Token&&Read.Session==Quote.Session&&Read.CatalogRevision==Quote.CatalogRevision&&Read.Offers.Num()==1&&Read.Offers[0].Skill==Offer.Skill&&Read.Offers[0].Price==Offer.Price);
    return true;
}
#endif
