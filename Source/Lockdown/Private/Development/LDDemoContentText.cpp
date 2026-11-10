#include "Development/LDDemoContentText.h"

#include "Development/LDDemoContentCatalog.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"

namespace
{
const TCHAR* const DemoContentTextTable = TEXT("DemoContent");

bool SameDemoContentEntries(const FStringTable& Registered, const FStringTable& Expected, FString& Error)
{
    bool bSame = Registered.GetNamespace().Equals(Expected.GetNamespace(), ESearchCase::CaseSensitive);
    int32 RegisteredCount = 0;
    const auto CountRegistered = [&RegisteredCount](const FString&, const FString&)
    {
        ++RegisteredCount;
        return true;
    };
    Registered.EnumerateSourceStrings(TFunctionRef<bool(const FString&, const FString&)>(CountRegistered));
    int32 ExpectedCount = 0;
    const auto CompareExpected = [&Registered, &bSame, &ExpectedCount](const FString& Key, const FString& Text)
    {
        ++ExpectedCount;
        FString Current;
        bSame = bSame && Registered.GetSourceString(FTextKey(Key), Current) && Current.Equals(Text, ESearchCase::CaseSensitive);
        return bSame;
    };
    Expected.EnumerateSourceStrings(TFunctionRef<bool(const FString&, const FString&)>(CompareExpected));
    if (!bSame || RegisteredCount != ExpectedCount)
    {
        Error = TEXT("DemoContent display text is already registered with different entries. "
            "Restart after regenerating content; mixed display text is never used.");
        return false;
    }
    return true;
}
}

FName LDDemoContentText::TableId()
{
    return FName(DemoContentTextTable);
}

bool LDDemoContentText::Build(const FLDDemoContentDefinitions& Definitions, FStringTablePtr& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    if (Definitions.Display.IsEmpty())
    {
        Error = TEXT("DemoContent display text: the validated catalog has no display rows.");
        return false;
    }
    const FStringTableRef Table = FStringTable::NewStringTable();
    Table->SetNamespace(FTextKey(DemoContentTextTable));
    for (const FLDDemoDisplayDefinition& Row : Definitions.Display)
    {
        const TPair<const FString*, const FString*> Entries[] = {{&Row.NameKey, &Row.NameText}, {&Row.DescriptionKey, &Row.DescriptionText}};
        for (const TPair<const FString*, const FString*>& Entry : Entries)
        {
            FString Existing;
            if (!Row.Namespace.Equals(DemoContentTextTable, ESearchCase::CaseSensitive) || Entry.Key->IsEmpty()
                || Entry.Value->IsEmpty() || Table->GetSourceString(FTextKey(*Entry.Key), Existing))
            {
                Error = FString::Printf(TEXT("DemoContent display text: missing, duplicate or foreign entry for %s."), *Row.Id);
                return false;
            }
#if WITH_EDITORONLY_DATA
            Table->SetSourceString(FTextKey(*Entry.Key), *Entry.Value, FString());
#else
            Table->SetSourceString(FTextKey(*Entry.Key), *Entry.Value);
#endif
        }
    }
    Out = Table;
    return true;
}

bool LDDemoContentText::Register(const FLDDemoContentSnapshot& Snapshot, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread())
    {
        Error = TEXT("DemoContent display text registration requires the game thread.");
        return false;
    }
    FStringTablePtr Built;
    if (!Build(Snapshot.Get(), Built, Error))
    {
        return false;
    }
    FStringTableRegistry& Registry = FStringTableRegistry::Get();
    if (const FStringTableConstPtr Registered = Registry.FindStringTable(TableId()))
    {
        return SameDemoContentEntries(*Registered, *Built, Error);
    }
    Registry.RegisterStringTable(TableId(), Built.ToSharedRef());
    return true;
}
