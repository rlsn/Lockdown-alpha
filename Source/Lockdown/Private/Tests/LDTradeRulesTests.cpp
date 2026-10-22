#include "Inventory/LDTradeTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDTradePrivacyTest,"Lockdown.Rules.TradePrivacy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDTradePrivacyTest::RunTest(const FString& Parameters)
{
    const TSet<FName> Allowed={TEXT("Session"),TEXT("Match"),TEXT("Life"),TEXT("Serial"),TEXT("Revision"),TEXT("Digest"),TEXT("Stage"),TEXT("bReceiver"),TEXT("bOwnReady"),TEXT("bOtherReady"),TEXT("bOwnConfirmed"),TEXT("bOtherConfirmed"),TEXT("bOwnValid"),TEXT("bOtherValid"),TEXT("OwnGold"),TEXT("OtherGold"),TEXT("OwnItems"),TEXT("OtherItems"),TEXT("ExpiresAt"),TEXT("Message")};
    int32 Fields=0;for(TFieldIterator<FProperty> It(FLDTradeView::StaticStruct());It;++It){++Fields;TestTrue(TEXT("Trade wire view contains only the explicit consent/offer allowlist"),Allowed.Contains(It->GetFName()));TestFalse(TEXT("No actor/controller reference enters trade view"),It->IsA<FObjectPropertyBase>());}
    TestEqual(TEXT("Trade view field boundary"),Fields,Allowed.Num());const TSet<FName> ItemFields={TEXT("Handle"),TEXT("Kind"),TEXT("Skill")};Fields=0;for(TFieldIterator<FProperty> It(FLDTradeItem::StaticStruct());It;++It){++Fields;TestTrue(TEXT("Offered wire item has only its permitted handle and definition"),ItemFields.Contains(It->GetFName()));}TestEqual(TEXT("Offered item excludes hidden owner/container/wallet fields"),Fields,3);return true;
}
#endif
