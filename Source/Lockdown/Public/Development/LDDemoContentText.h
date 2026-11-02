#pragma once

#include "CoreMinimal.h"
#include "Internationalization/StringTableCoreFwd.h"

struct FLDDemoContentDefinitions;
class FLDDemoContentSnapshot;

// Player-visible content names and descriptions, keyed `<contentId>.name` / `<contentId>.description`
// in namespace DemoContent. The table is built only from a validated catalog; UI resolves entries through
// FStringTableRegistry with TableId() and never substitutes text for a missing entry.
namespace LDDemoContentText
{
    LOCKDOWN_API FName TableId();
    // Builds the complete table without touching the registry; rejects missing or duplicate entries.
    LOCKDOWN_API bool Build(const FLDDemoContentDefinitions& Definitions, FStringTablePtr& Out, FString& Error);
    // Registers the verified table once per process. Identical text is a no-op; different text under the same ID fails.
    LOCKDOWN_API bool Register(const FLDDemoContentSnapshot& Snapshot, FString& Error);
}
