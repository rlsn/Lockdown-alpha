#include "UI/LDChainCodex.h"
#include "Combat/LDChainRules.h"
#include "Kismet/GameplayStatics.h"

bool LDChainCodex::Record(TArray<FLDChainCodexEntry>& Entries, const FString& Key, int32 Steps, int64 NowUnix)
{
    if (Key.IsEmpty()) { return false; }
    if (FLDChainCodexEntry* Known = Entries.FindByPredicate([&Key](const FLDChainCodexEntry& Row) { return Row.Key == Key; }))
    {
        Known->LongestSteps = FMath::Max(Known->LongestSteps, Steps);
        return false;
    }
    FLDChainCodexEntry& Entry = Entries.AddDefaulted_GetRef();
    Entry.Key = Key;
    Entry.FirstSeenUnix = NowUnix;
    Entry.LongestSteps = Steps;
    while (Entries.Num() > LDChain::CodexCapacity)
    {
        int32 Oldest = 0;
        for (int32 Index = 1; Index < Entries.Num(); ++Index)
        {
            if (Entries[Index].FirstSeenUnix < Entries[Oldest].FirstSeenUnix) { Oldest = Index; }
        }
        Entries.RemoveAt(Oldest);
    }
    return true;
}

TArray<FLDChainCodexEntry> LDChainCodex::Load()
{
    if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0)) { return {}; }
    const ULDChainCodexSave* Save = Cast<ULDChainCodexSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
    return Save ? Save->Entries : TArray<FLDChainCodexEntry>();
}

bool LDChainCodex::Save(const TArray<FLDChainCodexEntry>& Entries)
{
    ULDChainCodexSave* Save = Cast<ULDChainCodexSave>(UGameplayStatics::CreateSaveGameObject(ULDChainCodexSave::StaticClass()));
    if (!Save) { return false; }
    Save->Entries = Entries;
    return UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
}
