#include "Tests/LDTestApprovedDisplay.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Development/LDDemoContentCatalog.h"
#include "Misc/AutomationTest.h"
#include "Tests/LDTestApprovedFixture.h"

bool LDTestApprovedDisplay::Names(FAutomationTestBase& Test,TMap<FString,FString>& Out)
{
    TSharedPtr<const FLDDemoContentSnapshot> Snapshot;
    if(!LDTestApprovedFixture::Read(Test,Snapshot)){return false;}
    Out.Reset();
    for(const FLDDemoDisplayDefinition& Display:Snapshot->Get().Display){Out.Add(Display.Id,Display.NameText);}
    if(Out.IsEmpty()){Test.AddError(TEXT("The approved catalog has no display rows."));return false;}
    return true;
}
#endif
