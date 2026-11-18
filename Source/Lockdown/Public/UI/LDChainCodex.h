#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "LDChainCodex.generated.h"

// Spec §6.5 连锁图鉴: a local record of the chain keys this player has found (no gameplay value), kept apart from the settings.
USTRUCT()
struct LOCKDOWN_API FLDChainCodexEntry
{
    GENERATED_BODY()

    // The key's steps joined by ">" (LDChain::Text).
    UPROPERTY() FString Key;
    UPROPERTY() int64 FirstSeenUnix = 0;
    UPROPERTY() int32 LongestSteps = 0;
};

UCLASS()
class LOCKDOWN_API ULDChainCodexSave : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() TArray<FLDChainCodexEntry> Entries;
};

namespace LDChainCodex
{
    inline const TCHAR* const SlotName = TEXT("LDChainCodex");
    // Records one ended chain: a new key joins (the oldest first seen leaves past LDChain::CodexCapacity), a known one keeps its
    // longest length. True when the key is new.
    LOCKDOWN_API bool Record(TArray<FLDChainCodexEntry>& Entries, const FString& Key, int32 Steps, int64 NowUnix);
    // The local codex (empty when none was saved or it cannot be read) and saving it back; false when the save failed.
    LOCKDOWN_API TArray<FLDChainCodexEntry> Load();
    LOCKDOWN_API bool Save(const TArray<FLDChainCodexEntry>& Entries);
}
